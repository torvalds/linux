/*
 * Copyright (c) 2000-2002, Boris Popov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: ctx.c,v 1.24 2002/04/13 14:35:28 bp Exp $
 * $FreeBSD$
 */
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <sys/iconv.h>

#define NB_NEEDRESOLVER

#include <netsmb/smb_lib.h>
#include <netsmb/netbios.h>
#include <netsmb/nb_lib.h>
#include <netsmb/smb_conn.h>
#include <cflib.h>

/*
 * Prescan command line for [-U user] argument
 * and fill context with defaults
 */
int
smb_ctx_init(struct smb_ctx *ctx, int argc, char *argv[],
	int minlevel, int maxlevel, int sharetype)
{
	int  opt, error = 0;
	uid_t euid;
	const char *arg, *cp;
	struct passwd *pwd;

	bzero(ctx,sizeof(*ctx));
	error = nb_ctx_create(&ctx->ct_nb);
	if (error)
		return error;
	ctx->ct_fd = -1;
	ctx->ct_parsedlevel = SMBL_NONE;
	ctx->ct_minlevel = minlevel;
	ctx->ct_maxlevel = maxlevel;
	ctx->ct_smbtcpport = SMB_TCP_PORT;

	ctx->ct_ssn.ioc_opt = SMBVOPT_CREATE;
	ctx->ct_ssn.ioc_timeout = 15;
	ctx->ct_ssn.ioc_retrycount = 4;
	ctx->ct_ssn.ioc_owner = SMBM_ANY_OWNER;
	ctx->ct_ssn.ioc_group = SMBM_ANY_GROUP;
	ctx->ct_ssn.ioc_mode = SMBM_EXEC;
	ctx->ct_ssn.ioc_rights = SMBM_DEFAULT;

	ctx->ct_sh.ioc_opt = SMBVOPT_CREATE;
	ctx->ct_sh.ioc_owner = SMBM_ANY_OWNER;
	ctx->ct_sh.ioc_group = SMBM_ANY_GROUP;
	ctx->ct_sh.ioc_mode = SMBM_EXEC;
	ctx->ct_sh.ioc_rights = SMBM_DEFAULT;
	ctx->ct_sh.ioc_owner = SMBM_ANY_OWNER;
	ctx->ct_sh.ioc_group = SMBM_ANY_GROUP;

	nb_ctx_setscope(ctx->ct_nb, "");
	euid = geteuid();
	if ((pwd = getpwuid(euid)) != NULL) {
		smb_ctx_setuser(ctx, pwd->pw_name);
		endpwent();
	} else if (euid == 0)
		smb_ctx_setuser(ctx, "root");
	else
		return 0;
	if (argv == NULL)
		return 0;
	for (opt = 1; opt < argc; opt++) {
		cp = argv[opt];
		if (strncmp(cp, "//", 2) != 0)
			continue;
		error = smb_ctx_parseunc(ctx, cp, sharetype, (const char**)&cp);
		if (error)
			return error;
		ctx->ct_uncnext = cp;
		break;
	}
	while (error == 0 && (opt = cf_getopt(argc, argv, ":E:L:U:")) != -1) {
		arg = cf_optarg;
		switch (opt) {
		    case 'E':
			error = smb_ctx_setcharset(ctx, arg);
			if (error)
				return error;
			break;
		    case 'L':
			error = nls_setlocale(arg);
			if (error)
				break;
			break;
		    case 'U':
			error = smb_ctx_setuser(ctx, arg);
			break;
		}
	}
	cf_optind = cf_optreset = 1;
	return error;
}

void
smb_ctx_done(struct smb_ctx *ctx)
{
	if (ctx->ct_ssn.ioc_server)
		nb_snbfree(ctx->ct_ssn.ioc_server);
	if (ctx->ct_ssn.ioc_local)
		nb_snbfree(ctx->ct_ssn.ioc_local);
	if (ctx->ct_srvaddr)
		free(ctx->ct_srvaddr);
	if (ctx->ct_nb)
		nb_ctx_done(ctx->ct_nb);
}

static int
getsubstring(const char *p, u_char sep, char *dest, int maxlen, const char **next)
{
	int len;

	maxlen--;
	for (len = 0; len < maxlen && *p != sep; p++, len++, dest++) {
		if (*p == 0)
			return EINVAL;
		*dest = *p;
	}
	*dest = 0;
	*next = *p ? p + 1 : p;
	return 0;
}

/*
 * Here we expect something like "[proto:]//[user@]host[:psmb[:pnb]][/share][/path]"
 */
int
smb_ctx_parseunc(struct smb_ctx *ctx, const char *unc, int sharetype,
	const char **next)
{
	const char *p = unc;
	char *p1, *psmb, *pnb;
	char tmp[1024];
	int error ;

	ctx->ct_parsedlevel = SMBL_NONE;
	if (*p++ != '/' || *p++ != '/') {
		smb_error("UNC should start with '//'", 0);
		return EINVAL;
	}
	p1 = tmp;
	error = getsubstring(p, '@', p1, sizeof(tmp), &p);
	if (!error) {
		if (ctx->ct_maxlevel < SMBL_VC) {
			smb_error("no user name required", 0);
			return EINVAL;
		}
		error = smb_ctx_setuser(ctx, tmp);
		if (error)
			return error;
		ctx->ct_parsedlevel = SMBL_VC;
	}
	error = getsubstring(p, '/', p1, sizeof(tmp), &p);
	if (error) {
		error = getsubstring(p, '\0', p1, sizeof(tmp), &p);
		if (error) {
			smb_error("no server name found", 0);
			return error;
		}
	}
	if (*p1 == 0) {
		smb_error("empty server name", 0);
		return EINVAL;
	}
	/*
	 * Check for port number specification.
	 */
	psmb = strchr(tmp, ':');
	if (psmb) {
		*psmb++ = '\0';
		pnb = strchr(psmb, ':');
		if (pnb) {
			*pnb++ = '\0';
			error = smb_ctx_setnbport(ctx, atoi(pnb));
			if (error) {
				smb_error("Invalid NetBIOS port number", 0);
				return error;
			}
		}
		error = smb_ctx_setsmbport(ctx, atoi(psmb));
		if (error) {
			smb_error("Invalid SMB port number", 0);
			return error;
		}
	}
	error = smb_ctx_setserver(ctx, tmp);
	if (error)
		return error;
	if (sharetype == SMB_ST_NONE) {
		*next = p;
		return 0;
	}
	if (*p != 0 && ctx->ct_maxlevel < SMBL_SHARE) {
		smb_error("no share name required", 0);
		return EINVAL;
	}
	error = getsubstring(p, '/', p1, sizeof(tmp), &p);
	if (error) {
		error = getsubstring(p, '\0', p1, sizeof(tmp), &p);
		if (error) {
			smb_error("unexpected end of line", 0);
			return error;
		}
	}
	if (*p1 == 0 && ctx->ct_minlevel >= SMBL_SHARE) {
		smb_error("empty share name", 0);
		return EINVAL;
	}
	*next = p;
	if (*p1 == 0)
		return 0;
	error = smb_ctx_setshare(ctx, p1, sharetype);
	return error;
}

int
smb_ctx_setcharset(struct smb_ctx *ctx, const char *arg)
{
	char *cp, *servercs, *localcs;
	int cslen = sizeof(ctx->ct_ssn.ioc_localcs);
	int scslen, lcslen, error;

	cp = strchr(arg, ':');
	lcslen = cp ? (cp - arg) : 0;
	if (lcslen == 0 || lcslen >= cslen) {
		smb_error("invalid local charset specification (%s)", 0, arg);
		return EINVAL;
	}
	scslen = (size_t)strlen(++cp);
	if (scslen == 0 || scslen >= cslen) {
		smb_error("invalid server charset specification (%s)", 0, arg);
		return EINVAL;
	}
	localcs = memcpy(ctx->ct_ssn.ioc_localcs, arg, lcslen);
	localcs[lcslen] = 0;
	servercs = strcpy(ctx->ct_ssn.ioc_servercs, cp);
	error = nls_setrecode(localcs, servercs);
	if (error == 0)
		return 0;
	smb_error("can't initialize iconv support (%s:%s)",
	    error, localcs, servercs);
	localcs[0] = 0;
	servercs[0] = 0;
	return error;
}

int
smb_ctx_setserver(struct smb_ctx *ctx, const char *name)
{
	if (strlen(name) > SMB_MAXSRVNAMELEN) {
		smb_error("server name '%s' too long", 0, name);
		return ENAMETOOLONG;
	}
	nls_str_upper(ctx->ct_ssn.ioc_srvname, name);
	return 0;
}

int
smb_ctx_setnbport(struct smb_ctx *ctx, int port)
{
	if (port < 1 || port > 0xffff)
		return EINVAL;
	ctx->ct_nb->nb_nmbtcpport = port;
	return 0;
}

int
smb_ctx_setsmbport(struct smb_ctx *ctx, int port)
{
	if (port < 1 || port > 0xffff)
		return EINVAL;
	ctx->ct_smbtcpport = port;
	ctx->ct_nb->nb_smbtcpport = port;
	return 0;
}

int
smb_ctx_setuser(struct smb_ctx *ctx, const char *name)
{
	if (strlen(name) > SMB_MAXUSERNAMELEN) {
		smb_error("user name '%s' too long", 0, name);
		return ENAMETOOLONG;
	}
	nls_str_upper(ctx->ct_ssn.ioc_user, name);
	return 0;
}

int
smb_ctx_setworkgroup(struct smb_ctx *ctx, const char *name)
{
	if (strlen(name) > SMB_MAXUSERNAMELEN) {
		smb_error("workgroup name '%s' too long", 0, name);
		return ENAMETOOLONG;
	}
	nls_str_upper(ctx->ct_ssn.ioc_workgroup, name);
	return 0;
}

int
smb_ctx_setpassword(struct smb_ctx *ctx, const char *passwd)
{
	if (passwd == NULL)
		return EINVAL;
	if (strlen(passwd) > SMB_MAXPASSWORDLEN) {
		smb_error("password too long", 0);
		return ENAMETOOLONG;
	}
	if (strncmp(passwd, "$$1", 3) == 0)
		smb_simpledecrypt(ctx->ct_ssn.ioc_password, passwd);
	else
		strcpy(ctx->ct_ssn.ioc_password, passwd);
	strcpy(ctx->ct_sh.ioc_password, ctx->ct_ssn.ioc_password);
	return 0;
}

int
smb_ctx_setshare(struct smb_ctx *ctx, const char *share, int stype)
{
	if (strlen(share) > SMB_MAXSHARENAMELEN) {
		smb_error("share name '%s' too long", 0, share);
		return ENAMETOOLONG;
	}
	nls_str_upper(ctx->ct_sh.ioc_share, share);
	if (share[0] != 0)
		ctx->ct_parsedlevel = SMBL_SHARE;
	ctx->ct_sh.ioc_stype = stype;
	return 0;
}

int
smb_ctx_setsrvaddr(struct smb_ctx *ctx, const char *addr)
{
	if (addr == NULL || addr[0] == 0)
		return EINVAL;
	if (ctx->ct_srvaddr)
		free(ctx->ct_srvaddr);
	if ((ctx->ct_srvaddr = strdup(addr)) == NULL)
		return ENOMEM;
	return 0;
}

static int
smb_parse_owner(char *pair, uid_t *uid, gid_t *gid)
{
	struct group *gr;
	struct passwd *pw;
	char *cp;

	cp = strchr(pair, ':');
	if (cp) {
		*cp++ = '\0';
		if (*cp) {
			gr = getgrnam(cp);
			if (gr) {
				*gid = gr->gr_gid;
			} else
				smb_error("Invalid group name %s, ignored",
				    0, cp);
		}
	}
	if (*pair) {
		pw = getpwnam(pair);
		if (pw) {
			*uid = pw->pw_uid;
		} else
			smb_error("Invalid user name %s, ignored", 0, pair);
	}
	endpwent();
	return 0;
}

int
smb_ctx_opt(struct smb_ctx *ctx, int opt, const char *arg)
{
	int error = 0;
	char *p, *cp;

	switch(opt) {
	    case 'U':
		break;
	    case 'I':
		error = smb_ctx_setsrvaddr(ctx, arg);
		break;
	    case 'M':
		ctx->ct_ssn.ioc_rights = strtol(arg, &cp, 8);
		if (*cp == '/') {
			ctx->ct_sh.ioc_rights = strtol(cp + 1, &cp, 8);
			ctx->ct_flags |= SMBCF_SRIGHTS;
		}
		break;
	    case 'N':
		ctx->ct_flags |= SMBCF_NOPWD;
		break;
	    case 'O':
		p = strdup(arg);
		cp = strchr(p, '/');
		if (cp) {
			*cp++ = '\0';
			error = smb_parse_owner(cp, &ctx->ct_sh.ioc_owner,
			    &ctx->ct_sh.ioc_group);
		}
		if (*p && error == 0) {
			error = smb_parse_owner(p, &ctx->ct_ssn.ioc_owner,
			    &ctx->ct_ssn.ioc_group);
		}
		free(p);
		break;
	    case 'P':
/*		ctx->ct_ssn.ioc_opt |= SMBCOPT_PERMANENT;*/
		break;
	    case 'R':
		ctx->ct_ssn.ioc_retrycount = atoi(arg);
		break;
	    case 'T':
		ctx->ct_ssn.ioc_timeout = atoi(arg);
		break;
	    case 'W':
		error = smb_ctx_setworkgroup(ctx, arg);
		break;
	}
	return error;
}

#if 0
static void
smb_hexdump(const u_char *buf, int len) {
	int ofs = 0;

	while (len--) {
		if (ofs % 16 == 0)
			printf("\n%02X: ", ofs);
		printf("%02x ", *buf++);
		ofs++;
	}
	printf("\n");
}
#endif


static int
smb_addiconvtbl(const char *to, const char *from, const u_char *tbl)
{
	int error;

	error = kiconv_add_xlat_table(to, from, tbl);
	if (error && error != EEXIST) {
		smb_error("can not setup kernel iconv table (%s:%s)", error,
		    from, to);
		return error;
	}
	return 0;
}

/*
 * Verify context before connect operation(s),
 * lookup specified server and try to fill all forgotten fields.
 */
int
smb_ctx_resolve(struct smb_ctx *ctx)
{
	struct smbioc_ossn *ssn = &ctx->ct_ssn;
	struct smbioc_oshare *sh = &ctx->ct_sh;
	struct nb_name nn;
	struct sockaddr *sap;
	struct sockaddr_nb *salocal, *saserver;
	char *cp;
	int error = 0;
	
	ctx->ct_flags &= ~SMBCF_RESOLVED;
	if (ssn->ioc_srvname[0] == 0) {
		smb_error("no server name specified", 0);
		return EINVAL;
	}
	if (ctx->ct_minlevel >= SMBL_SHARE && sh->ioc_share[0] == 0) {
		smb_error("no share name specified for %s@%s",
		    0, ssn->ioc_user, ssn->ioc_srvname);
		return EINVAL;
	}
	error = nb_ctx_resolve(ctx->ct_nb);
	if (error)
		return error;
	if (ssn->ioc_localcs[0] == 0)
		strcpy(ssn->ioc_localcs, "ISO8859-1");
	error = smb_addiconvtbl("tolower", ssn->ioc_localcs, nls_lower);
	if (error)
		return error;
	error = smb_addiconvtbl("toupper", ssn->ioc_localcs, nls_upper);
	if (error)
		return error;
	if (ssn->ioc_servercs[0] != 0) {
		error = kiconv_add_xlat16_cspairs
			(ssn->ioc_servercs, ssn->ioc_localcs);
		if (error) return error;
	}
	if (ctx->ct_srvaddr) {
		error = nb_resolvehost_in(ctx->ct_srvaddr, &sap, ctx->ct_smbtcpport);
	} else {
		error = nbns_resolvename(ssn->ioc_srvname, ctx->ct_nb, &sap);
	}
	if (error) {
		smb_error("can't get server address", error);
		return error;
	}
	nn.nn_scope = ctx->ct_nb->nb_scope;
	nn.nn_type = NBT_SERVER;
	if (strlen(ssn->ioc_srvname) > NB_NAMELEN)
		return NBERROR(NBERR_NAMETOOLONG);
	strlcpy(nn.nn_name, ssn->ioc_srvname, sizeof(nn.nn_name));
	error = nb_sockaddr(sap, &nn, &saserver);
	nb_snbfree(sap);
	if (error) {
		smb_error("can't allocate server address", error);
		return error;
	}
	ssn->ioc_server = (struct sockaddr*)saserver;
	if (ctx->ct_locname[0] == 0) {
		error = nb_getlocalname(ctx->ct_locname);
		if (error) {
			smb_error("can't get local name", error);
			return error;
		}
		nls_str_upper(ctx->ct_locname, ctx->ct_locname);
	}
	/*
	 * Truncate the local host name to NB_NAMELEN-1 which gives a
	 * suffix of 0 which is "workstation name".
	 */
	strlcpy(nn.nn_name, ctx->ct_locname, NB_NAMELEN);
	nn.nn_type = NBT_WKSTA;
	nn.nn_scope = ctx->ct_nb->nb_scope;
	error = nb_sockaddr(NULL, &nn, &salocal);
	if (error) {
		nb_snbfree((struct sockaddr*)saserver);
		smb_error("can't allocate local address", error);
		return error;
	}
	ssn->ioc_local = (struct sockaddr*)salocal;
	ssn->ioc_lolen = salocal->snb_len;
	ssn->ioc_svlen = saserver->snb_len;
	if (ssn->ioc_password[0] == 0 && (ctx->ct_flags & SMBCF_NOPWD) == 0) {
		cp = getpass("Password:");
		error = smb_ctx_setpassword(ctx, cp);
		if (error)
			return error;
	}
	ctx->ct_flags |= SMBCF_RESOLVED;
	return 0;
}

static int
smb_ctx_gethandle(struct smb_ctx *ctx)
{
	int fd, i;
	char buf[20];

	fd = open("/dev/"NSMB_NAME, O_RDWR);
	if (fd >= 0) {
		ctx->ct_fd = fd;
		return 0;
	}
	return ENOENT;
}

int
smb_ctx_lookup(struct smb_ctx *ctx, int level, int flags)
{
	struct smbioc_lookup rq;
	int error;

	if ((ctx->ct_flags & SMBCF_RESOLVED) == 0) {
		smb_error("smb_ctx_lookup() data is not resolved", 0);
		return EINVAL;
	}
	if (ctx->ct_fd != -1) {
		close(ctx->ct_fd);
		ctx->ct_fd = -1;
	}
	error = smb_ctx_gethandle(ctx);
	if (error) {
		smb_error("can't get handle to requester (no /dev/"NSMB_NAME"* device)", 0);
		return EINVAL;
	}
	bzero(&rq, sizeof(rq));
	bcopy(&ctx->ct_ssn, &rq.ioc_ssn, sizeof(struct smbioc_ossn));
	bcopy(&ctx->ct_sh, &rq.ioc_sh, sizeof(struct smbioc_oshare));
	rq.ioc_flags = flags;
	rq.ioc_level = level;
	if (ioctl(ctx->ct_fd, SMBIOC_LOOKUP, &rq) == -1) {
		error = errno;
		if (flags & SMBLK_CREATE)
			smb_error("unable to open connection", error);
		return error;
	}
	return 0;
}

int
smb_ctx_login(struct smb_ctx *ctx)
{
	struct smbioc_ossn *ssn = &ctx->ct_ssn;
	struct smbioc_oshare *sh = &ctx->ct_sh;
	int error;

	if ((ctx->ct_flags & SMBCF_RESOLVED) == 0) {
		smb_error("smb_ctx_resolve() should be called first", 0);
		return EINVAL;
	}
	if (ctx->ct_fd != -1) {
		close(ctx->ct_fd);
		ctx->ct_fd = -1;
	}
	error = smb_ctx_gethandle(ctx);
	if (error) {
		smb_error("can't get handle to requester", 0);
		return EINVAL;
	}
	if (ioctl(ctx->ct_fd, SMBIOC_OPENSESSION, ssn) == -1) {
		error = errno;
		smb_error("can't open session to server %s", error, ssn->ioc_srvname);
		return error;
	}
	if (sh->ioc_share[0] == 0)
		return 0;
	if (ioctl(ctx->ct_fd, SMBIOC_OPENSHARE, sh) == -1) {
		error = errno;
		smb_error("can't connect to share //%s/%s", error,
		    ssn->ioc_srvname, sh->ioc_share);
		return error;
	}
	return 0;
}

int
smb_ctx_setflags(struct smb_ctx *ctx, int level, int mask, int flags)
{
	struct smbioc_flags fl;

	if (ctx->ct_fd == -1)
		return EINVAL;
	fl.ioc_level = level;
	fl.ioc_mask = mask;
	fl.ioc_flags = flags;
	if (ioctl(ctx->ct_fd, SMBIOC_SETFLAGS, &fl) == -1)
		return errno;
	return 0;
}

/*
 * level values:
 * 0 - default
 * 1 - server
 * 2 - server:user
 * 3 - server:user:share
 */
static int
smb_ctx_readrcsection(struct smb_ctx *ctx, const char *sname, int level)
{
	char *p;
	int error;

	if (level >= 0) {
		rc_getstringptr(smb_rc, sname, "charsets", &p);
		if (p) {
			error = smb_ctx_setcharset(ctx, p);
			if (error)
				smb_error("charset specification in the section '%s' ignored", error, sname);
		}
	}
	if (level <= 1) {
		rc_getint(smb_rc, sname, "timeout", &ctx->ct_ssn.ioc_timeout);
		rc_getint(smb_rc, sname, "retry_count", &ctx->ct_ssn.ioc_retrycount);
	}
	if (level == 1) {
		rc_getstringptr(smb_rc, sname, "addr", &p);
		if (p) {
			error = smb_ctx_setsrvaddr(ctx, p);
			if (error) {
				smb_error("invalid address specified in the section %s", 0, sname);
				return error;
			}
		}
	}
	if (level >= 2) {
		rc_getstringptr(smb_rc, sname, "password", &p);
		if (p)
			smb_ctx_setpassword(ctx, p);
	}
	rc_getstringptr(smb_rc, sname, "workgroup", &p);
	if (p)
		smb_ctx_setworkgroup(ctx, p);
	return 0;
}

/*
 * read rc file as follows:
 * 1. read [default] section
 * 2. override with [server] section
 * 3. override with [server:user:share] section
 * Since abcence of rcfile is not fatal, silently ignore this fact.
 * smb_rc file should be closed by caller.
 */
int
smb_ctx_readrc(struct smb_ctx *ctx)
{
	char sname[SMB_MAXSRVNAMELEN + SMB_MAXUSERNAMELEN + SMB_MAXSHARENAMELEN + 4];
/*	char *p;*/

	if (smb_open_rcfile() != 0)
		return 0;

	if (ctx->ct_ssn.ioc_user[0] == 0 || ctx->ct_ssn.ioc_srvname[0] == 0)
		return 0;

	smb_ctx_readrcsection(ctx, "default", 0);
	nb_ctx_readrcsection(smb_rc, ctx->ct_nb, "default", 0);
	smb_ctx_readrcsection(ctx, ctx->ct_ssn.ioc_srvname, 1);
	nb_ctx_readrcsection(smb_rc, ctx->ct_nb, ctx->ct_ssn.ioc_srvname, 1);
	/*
	 * SERVER:USER parameters
	 */
	snprintf(sname, sizeof(sname), "%s:%s", ctx->ct_ssn.ioc_srvname,
	    ctx->ct_ssn.ioc_user);
	smb_ctx_readrcsection(ctx, sname, 2);

	if (ctx->ct_sh.ioc_share[0] != 0) {
		/*
		 * SERVER:USER:SHARE parameters
	         */
		snprintf(sname, sizeof(sname), "%s:%s:%s", ctx->ct_ssn.ioc_srvname,
		    ctx->ct_ssn.ioc_user, ctx->ct_sh.ioc_share);
		smb_ctx_readrcsection(ctx, sname, 3);
	}
	return 0;
}

