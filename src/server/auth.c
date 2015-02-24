#include "include.h"
#include "../cmd.h"

#include <netdb.h>

static int check_passwd(const char *passwd, const char *plain_text)
{
#ifdef HAVE_CRYPT
	char salt[3];

	if(!plain_text || !passwd || strlen(passwd)!=13)
		return 0;

	salt[0]=passwd[0];
	salt[1]=passwd[1];
	salt[2]=0;
	return !strcmp(crypt(plain_text, salt), passwd);
#endif // HAVE_CRYPT
	logp("Server compiled without crypt support - cannot use passwd option\n");
	return -1;
}

static int check_client_and_password(struct conf **globalcs,
	const char *password, struct conf **cconfs)
{
	const char *cname=get_string(cconfs[OPT_CNAME]);
	int password_check=get_int(cconfs[OPT_PASSWORD_CHECK]);
	// Cannot load it until here, because we need to have the name of the
	// client.
	if(conf_load_clientconfdir(globalcs, cconfs)) return -1;

	if(!get_string(cconfs[OPT_SSL_PEER_CN]))
	{
		logp("ssl_peer_cn unset");
		if(cname)
		{
			logp("Falling back to using '%s'\n", cname);
			if(set_string(cconfs[OPT_SSL_PEER_CN], cname))
				return -1;
		}
	}

	cname=get_string(cconfs[OPT_CNAME]);

	if(password_check)
	{
		const char *passwd=get_string(cconfs[OPT_PASSWD]);
		const char *conf_password=get_string(cconfs[OPT_PASSWORD]);
		if(password && !passwd)
		{
			logp("password rejected for client %s\n", cname);
			return -1;
		}
		// check against plain text
		if(conf_password && strcmp(conf_password, password))
		{
			logp("password rejected for client %s\n", cname);
			return -1;
		}
		// check against encypted passwd
		if(passwd && !check_passwd(passwd, password))
		{
			logp("password rejected for client %s\n", cname);
			return -1;
		}
	}

	if(!get_strlist(cconfs[OPT_KEEP]))
	{
		logp("%s: you cannot set the keep value for a client to 0!\n",
			cname);
		return -1;
	}
	return 0;
}

void version_warn(struct asfd *asfd, struct conf **confs, struct conf **cconfs)
{
	const char *cname=get_string(cconfs[OPT_CNAME]);
	const char *peer_version=get_string(cconfs[OPT_PEER_VERSION]);
	if(!peer_version || strcmp(peer_version, VERSION))
	{
		char msg[256]="";

		if(!peer_version || !*peer_version)
			snprintf(msg, sizeof(msg), "Client '%s' has an unknown version. Please upgrade.", cname?cname:"unknown");
		else
			snprintf(msg, sizeof(msg), "Client '%s' version '%s' does not match server version '%s'. An upgrade is recommended.", cname?cname:"unknown", peer_version, VERSION);
		if(confs) logw(asfd, confs, "%s", msg);
		logp("WARNING: %s\n", msg);
	}
}

int authorise_server(struct asfd *asfd,
	struct conf **globalcs, struct conf **cconfs)
{
	int ret=-1;
	char *cp=NULL;
	char *password=NULL;
	char whoareyou[256]="";
	struct iobuf *rbuf=asfd->rbuf;
	const char *cname=NULL;
	const char *peer_version=NULL;
	if(asfd->read(asfd))
	{
		logp("unable to read initial message\n");
		goto end;
	}
	if(rbuf->cmd!=CMD_GEN || strncmp_w(rbuf->buf, "hello"))
	{
		iobuf_log_unexpected(rbuf, __func__);
		goto end;
	}
	// String may look like...
	// "hello"
	// "hello:(version)"
	// (version) is a version number
	if((cp=strchr(rbuf->buf, ':')))
	{
		cp++;
		if(cp && set_string(cconfs[OPT_PEER_VERSION], cp))
			goto end;
	}
	iobuf_free_content(rbuf);

	snprintf(whoareyou, sizeof(whoareyou), "whoareyou");
	peer_version=get_string(cconfs[OPT_PEER_VERSION]);
	if(peer_version)
	{
		long min_ver=0;
		long cli_ver=0;
		if((min_ver=version_to_long("1.3.2"))<0
		  || (cli_ver=version_to_long(peer_version))<0)
			return -1;
		// Stick the server version on the end of the whoareyou string.
		// if the client version is recent enough.
		if(min_ver<=cli_ver)
		 snprintf(whoareyou, sizeof(whoareyou),
			"whoareyou:%s", VERSION);
	}

	asfd->write_str(asfd, CMD_GEN, whoareyou);
	if(asfd->read(asfd))
	{
		logp("unable to get client name\n");
		goto end;
	}
	if(set_string(cconfs[OPT_CNAME], rbuf->buf))
		goto end;
	iobuf_init(rbuf);
	cname=get_string(cconfs[OPT_CNAME]);

	asfd->write_str(asfd, CMD_GEN, "okpassword");
	if(asfd->read(asfd))
	{
		logp("unable to get password for client %s\n", cname);
		goto end;
	}
	password=rbuf->buf;
	iobuf_init(rbuf);

	if(check_client_and_password(globalcs, password, cconfs))
		goto end;

	if(get_int(cconfs[OPT_VERSION_WARN]))
		version_warn(asfd, globalcs, cconfs);

	logp("auth ok for: %s%s\n", cname,
		get_int(cconfs[OPT_PASSWORD_CHECK])?
			"":" (no password needed)");

	asfd->write_str(asfd, CMD_GEN, "ok");

	ret=0;
end:
	iobuf_free_content(rbuf);
	free_w(&password);
	return ret;
}
