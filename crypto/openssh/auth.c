/* $OpenBSD: auth.c,v 1.132 2018/07/11 08:19:35 martijn Exp $ */
/*
 * Copyright (c) 2000 Markus Friedl.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "includes.h"
__RCSID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <netinet/in.h>

#include <errno.h>
#include <fcntl.h>
#ifdef HAVE_PATHS_H
# include <paths.h>
#endif
#include <pwd.h>
#ifdef HAVE_LOGIN_H
#include <login.h>
#endif
#ifdef USE_SHADOW
#include <shadow.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <netdb.h>

#include "xmalloc.h"
#include "match.h"
#include "groupaccess.h"
#include "log.h"
#include "sshbuf.h"
#include "misc.h"
#include "servconf.h"
#include "sshkey.h"
#include "hostfile.h"
#include "auth.h"
#include "auth-options.h"
#include "canohost.h"
#include "uidswap.h"
#include "packet.h"
#include "loginrec.h"
#ifdef GSSAPI
#include "ssh-gss.h"
#endif
#include "authfile.h"
#include "monitor_wrap.h"
#include "authfile.h"
#include "ssherr.h"
#include "compat.h"
#include "channels.h"
#include "blacklist_client.h"

/* import */
extern ServerOptions options;
extern int use_privsep;
extern struct sshbuf *loginmsg;
extern struct passwd *privsep_pw;
extern struct sshauthopt *auth_opts;

/* Debugging messages */
static struct sshbuf *auth_debug;

/*
 * Check if the user is allowed to log in via ssh. If user is listed
 * in DenyUsers or one of user's groups is listed in DenyGroups, false
 * will be returned. If AllowUsers isn't empty and user isn't listed
 * there, or if AllowGroups isn't empty and one of user's groups isn't
 * listed there, false will be returned.
 * If the user's shell is not executable, false will be returned.
 * Otherwise true is returned.
 */
int
allowed_user(struct passwd * pw)
{
	struct ssh *ssh = active_state; /* XXX */
	struct stat st;
	const char *hostname = NULL, *ipaddr = NULL, *passwd = NULL;
	u_int i;
	int r;
#ifdef USE_SHADOW
	struct spwd *spw = NULL;
#endif

	/* Shouldn't be called if pw is NULL, but better safe than sorry... */
	if (!pw || !pw->pw_name)
		return 0;

#ifdef USE_SHADOW
	if (!options.use_pam)
		spw = getspnam(pw->pw_name);
#ifdef HAS_SHADOW_EXPIRE
	if (!options.use_pam && spw != NULL && auth_shadow_acctexpired(spw))
		return 0;
#endif /* HAS_SHADOW_EXPIRE */
#endif /* USE_SHADOW */

	/* grab passwd field for locked account check */
	passwd = pw->pw_passwd;
#ifdef USE_SHADOW
	if (spw != NULL)
#ifdef USE_LIBIAF
		passwd = get_iaf_password(pw);
#else
		passwd = spw->sp_pwdp;
#endif /* USE_LIBIAF */
#endif

	/* check for locked account */
	if (!options.use_pam && passwd && *passwd) {
		int locked = 0;

#ifdef LOCKED_PASSWD_STRING
		if (strcmp(passwd, LOCKED_PASSWD_STRING) == 0)
			 locked = 1;
#endif
#ifdef LOCKED_PASSWD_PREFIX
		if (strncmp(passwd, LOCKED_PASSWD_PREFIX,
		    strlen(LOCKED_PASSWD_PREFIX)) == 0)
			 locked = 1;
#endif
#ifdef LOCKED_PASSWD_SUBSTR
		if (strstr(passwd, LOCKED_PASSWD_SUBSTR))
			locked = 1;
#endif
#ifdef USE_LIBIAF
		free((void *) passwd);
#endif /* USE_LIBIAF */
		if (locked) {
			logit("User %.100s not allowed because account is locked",
			    pw->pw_name);
			return 0;
		}
	}

	/*
	 * Deny if shell does not exist or is not executable unless we
	 * are chrooting.
	 */
	if (options.chroot_directory == NULL ||
	    strcasecmp(options.chroot_directory, "none") == 0) {
		char *shell = xstrdup((pw->pw_shell[0] == '\0') ?
		    _PATH_BSHELL : pw->pw_shell); /* empty = /bin/sh */

		if (stat(shell, &st) != 0) {
			logit("User %.100s not allowed because shell %.100s "
			    "does not exist", pw->pw_name, shell);
			free(shell);
			return 0;
		}
		if (S_ISREG(st.st_mode) == 0 ||
		    (st.st_mode & (S_IXOTH|S_IXUSR|S_IXGRP)) == 0) {
			logit("User %.100s not allowed because shell %.100s "
			    "is not executable", pw->pw_name, shell);
			free(shell);
			return 0;
		}
		free(shell);
	}

	if (options.num_deny_users > 0 || options.num_allow_users > 0 ||
	    options.num_deny_groups > 0 || options.num_allow_groups > 0) {
		hostname = auth_get_canonical_hostname(ssh, options.use_dns);
		ipaddr = ssh_remote_ipaddr(ssh);
	}

	/* Return false if user is listed in DenyUsers */
	if (options.num_deny_users > 0) {
		for (i = 0; i < options.num_deny_users; i++) {
			r = match_user(pw->pw_name, hostname, ipaddr,
			    options.deny_users[i]);
			if (r < 0) {
				fatal("Invalid DenyUsers pattern \"%.100s\"",
				    options.deny_users[i]);
			} else if (r != 0) {
				logit("User %.100s from %.100s not allowed "
				    "because listed in DenyUsers",
				    pw->pw_name, hostname);
				return 0;
			}
		}
	}
	/* Return false if AllowUsers isn't empty and user isn't listed there */
	if (options.num_allow_users > 0) {
		for (i = 0; i < options.num_allow_users; i++) {
			r = match_user(pw->pw_name, hostname, ipaddr,
			    options.allow_users[i]);
			if (r < 0) {
				fatal("Invalid AllowUsers pattern \"%.100s\"",
				    options.allow_users[i]);
			} else if (r == 1)
				break;
		}
		/* i < options.num_allow_users iff we break for loop */
		if (i >= options.num_allow_users) {
			logit("User %.100s from %.100s not allowed because "
			    "not listed in AllowUsers", pw->pw_name, hostname);
			return 0;
		}
	}
	if (options.num_deny_groups > 0 || options.num_allow_groups > 0) {
		/* Get the user's group access list (primary and supplementary) */
		if (ga_init(pw->pw_name, pw->pw_gid) == 0) {
			logit("User %.100s from %.100s not allowed because "
			    "not in any group", pw->pw_name, hostname);
			return 0;
		}

		/* Return false if one of user's groups is listed in DenyGroups */
		if (options.num_deny_groups > 0)
			if (ga_match(options.deny_groups,
			    options.num_deny_groups)) {
				ga_free();
				logit("User %.100s from %.100s not allowed "
				    "because a group is listed in DenyGroups",
				    pw->pw_name, hostname);
				return 0;
			}
		/*
		 * Return false if AllowGroups isn't empty and one of user's groups
		 * isn't listed there
		 */
		if (options.num_allow_groups > 0)
			if (!ga_match(options.allow_groups,
			    options.num_allow_groups)) {
				ga_free();
				logit("User %.100s from %.100s not allowed "
				    "because none of user's groups are listed "
				    "in AllowGroups", pw->pw_name, hostname);
				return 0;
			}
		ga_free();
	}

#ifdef CUSTOM_SYS_AUTH_ALLOWED_USER
	if (!sys_auth_allowed_user(pw, &loginmsg))
		return 0;
#endif

	/* We found no reason not to let this user try to log on... */
	return 1;
}

/*
 * Formats any key left in authctxt->auth_method_key for inclusion in
 * auth_log()'s message. Also includes authxtct->auth_method_info if present.
 */
static char *
format_method_key(Authctxt *authctxt)
{
	const struct sshkey *key = authctxt->auth_method_key;
	const char *methinfo = authctxt->auth_method_info;
	char *fp, *ret = NULL;

	if (key == NULL)
		return NULL;

	if (sshkey_is_cert(key)) {
		fp = sshkey_fingerprint(key->cert->signature_key,
		    options.fingerprint_hash, SSH_FP_DEFAULT);
		xasprintf(&ret, "%s ID %s (serial %llu) CA %s %s%s%s",
		    sshkey_type(key), key->cert->key_id,
		    (unsigned long long)key->cert->serial,
		    sshkey_type(key->cert->signature_key),
		    fp == NULL ? "(null)" : fp,
		    methinfo == NULL ? "" : ", ",
		    methinfo == NULL ? "" : methinfo);
		free(fp);
	} else {
		fp = sshkey_fingerprint(key, options.fingerprint_hash,
		    SSH_FP_DEFAULT);
		xasprintf(&ret, "%s %s%s%s", sshkey_type(key),
		    fp == NULL ? "(null)" : fp,
		    methinfo == NULL ? "" : ", ",
		    methinfo == NULL ? "" : methinfo);
		free(fp);
	}
	return ret;
}

void
auth_log(Authctxt *authctxt, int authenticated, int partial,
    const char *method, const char *submethod)
{
	struct ssh *ssh = active_state; /* XXX */
	void (*authlog) (const char *fmt,...) = verbose;
	const char *authmsg;
	char *extra = NULL;

	if (use_privsep && !mm_is_monitor() && !authctxt->postponed)
		return;

	/* Raise logging level */
	if (authenticated == 1 ||
	    !authctxt->valid ||
	    authctxt->failures >= options.max_authtries / 2 ||
	    strcmp(method, "password") == 0)
		authlog = logit;

	if (authctxt->postponed)
		authmsg = "Postponed";
	else if (partial)
		authmsg = "Partial";
	else {
		authmsg = authenticated ? "Accepted" : "Failed";
		if (authenticated)
			BLACKLIST_NOTIFY(BLACKLIST_AUTH_OK, "ssh");
	}

	if ((extra = format_method_key(authctxt)) == NULL) {
		if (authctxt->auth_method_info != NULL)
			extra = xstrdup(authctxt->auth_method_info);
	}

	authlog("%s %s%s%s for %s%.100s from %.200s port %d ssh2%s%s",
	    authmsg,
	    method,
	    submethod != NULL ? "/" : "", submethod == NULL ? "" : submethod,
	    authctxt->valid ? "" : "invalid user ",
	    authctxt->user,
	    ssh_remote_ipaddr(ssh),
	    ssh_remote_port(ssh),
	    extra != NULL ? ": " : "",
	    extra != NULL ? extra : "");

	free(extra);

#ifdef CUSTOM_FAILED_LOGIN
	if (authenticated == 0 && !authctxt->postponed &&
	    (strcmp(method, "password") == 0 ||
	    strncmp(method, "keyboard-interactive", 20) == 0 ||
	    strcmp(method, "challenge-response") == 0))
		record_failed_login(authctxt->user,
		    auth_get_canonical_hostname(ssh, options.use_dns), "ssh");
# ifdef WITH_AIXAUTHENTICATE
	if (authenticated)
		sys_auth_record_login(authctxt->user,
		    auth_get_canonical_hostname(ssh, options.use_dns), "ssh",
		    &loginmsg);
# endif
#endif
#ifdef SSH_AUDIT_EVENTS
	if (authenticated == 0 && !authctxt->postponed)
		audit_event(audit_classify_auth(method));
#endif
}


void
auth_maxtries_exceeded(Authctxt *authctxt)
{
	struct ssh *ssh = active_state; /* XXX */

	error("maximum authentication attempts exceeded for "
	    "%s%.100s from %.200s port %d ssh2",
	    authctxt->valid ? "" : "invalid user ",
	    authctxt->user,
	    ssh_remote_ipaddr(ssh),
	    ssh_remote_port(ssh));
	packet_disconnect("Too many authentication failures");
	/* NOTREACHED */
}

/*
 * Check whether root logins are disallowed.
 */
int
auth_root_allowed(struct ssh *ssh, const char *method)
{
	switch (options.permit_root_login) {
	case PERMIT_YES:
		return 1;
	case PERMIT_NO_PASSWD:
		if (strcmp(method, "publickey") == 0 ||
		    strcmp(method, "hostbased") == 0 ||
		    strcmp(method, "gssapi-with-mic") == 0)
			return 1;
		break;
	case PERMIT_FORCED_ONLY:
		if (auth_opts->force_command != NULL) {
			logit("Root login accepted for forced command.");
			return 1;
		}
		break;
	}
	logit("ROOT LOGIN REFUSED FROM %.200s port %d",
	    ssh_remote_ipaddr(ssh), ssh_remote_port(ssh));
	return 0;
}


/*
 * Given a template and a passwd structure, build a filename
 * by substituting % tokenised options. Currently, %% becomes '%',
 * %h becomes the home directory and %u the username.
 *
 * This returns a buffer allocated by xmalloc.
 */
char *
expand_authorized_keys(const char *filename, struct passwd *pw)
{
	char *file, uidstr[32], ret[PATH_MAX];
	int i;

	snprintf(uidstr, sizeof(uidstr), "%llu",
	    (unsigned long long)pw->pw_uid);
	file = percent_expand(filename, "h", pw->pw_dir,
	    "u", pw->pw_name, "U", uidstr, (char *)NULL);

	/*
	 * Ensure that filename starts anchored. If not, be backward
	 * compatible and prepend the '%h/'
	 */
	if (*file == '/')
		return (file);

	i = snprintf(ret, sizeof(ret), "%s/%s", pw->pw_dir, file);
	if (i < 0 || (size_t)i >= sizeof(ret))
		fatal("expand_authorized_keys: path too long");
	free(file);
	return (xstrdup(ret));
}

char *
authorized_principals_file(struct passwd *pw)
{
	if (options.authorized_principals_file == NULL)
		return NULL;
	return expand_authorized_keys(options.authorized_principals_file, pw);
}

/* return ok if key exists in sysfile or userfile */
HostStatus
check_key_in_hostfiles(struct passwd *pw, struct sshkey *key, const char *host,
    const char *sysfile, const char *userfile)
{
	char *user_hostfile;
	struct stat st;
	HostStatus host_status;
	struct hostkeys *hostkeys;
	const struct hostkey_entry *found;

	hostkeys = init_hostkeys();
	load_hostkeys(hostkeys, host, sysfile);
	if (userfile != NULL) {
		user_hostfile = tilde_expand_filename(userfile, pw->pw_uid);
		if (options.strict_modes &&
		    (stat(user_hostfile, &st) == 0) &&
		    ((st.st_uid != 0 && st.st_uid != pw->pw_uid) ||
		    (st.st_mode & 022) != 0)) {
			logit("Authentication refused for %.100s: "
			    "bad owner or modes for %.200s",
			    pw->pw_name, user_hostfile);
			auth_debug_add("Ignored %.200s: bad ownership or modes",
			    user_hostfile);
		} else {
			temporarily_use_uid(pw);
			load_hostkeys(hostkeys, host, user_hostfile);
			restore_uid();
		}
		free(user_hostfile);
	}
	host_status = check_key_in_hostkeys(hostkeys, key, &found);
	if (host_status == HOST_REVOKED)
		error("WARNING: revoked key for %s attempted authentication",
		    found->host);
	else if (host_status == HOST_OK)
		debug("%s: key for %s found at %s:%ld", __func__,
		    found->host, found->file, found->line);
	else
		debug("%s: key for host %s not found", __func__, host);

	free_hostkeys(hostkeys);

	return host_status;
}

static FILE *
auth_openfile(const char *file, struct passwd *pw, int strict_modes,
    int log_missing, char *file_type)
{
	char line[1024];
	struct stat st;
	int fd;
	FILE *f;

	if ((fd = open(file, O_RDONLY|O_NONBLOCK)) == -1) {
		if (log_missing || errno != ENOENT)
			debug("Could not open %s '%s': %s", file_type, file,
			   strerror(errno));
		return NULL;
	}

	if (fstat(fd, &st) < 0) {
		close(fd);
		return NULL;
	}
	if (!S_ISREG(st.st_mode)) {
		logit("User %s %s %s is not a regular file",
		    pw->pw_name, file_type, file);
		close(fd);
		return NULL;
	}
	unset_nonblock(fd);
	if ((f = fdopen(fd, "r")) == NULL) {
		close(fd);
		return NULL;
	}
	if (strict_modes &&
	    safe_path_fd(fileno(f), file, pw, line, sizeof(line)) != 0) {
		fclose(f);
		logit("Authentication refused: %s", line);
		auth_debug_add("Ignored %s: %s", file_type, line);
		return NULL;
	}

	return f;
}


FILE *
auth_openkeyfile(const char *file, struct passwd *pw, int strict_modes)
{
	return auth_openfile(file, pw, strict_modes, 1, "authorized keys");
}

FILE *
auth_openprincipals(const char *file, struct passwd *pw, int strict_modes)
{
	return auth_openfile(file, pw, strict_modes, 0,
	    "authorized principals");
}

struct passwd *
getpwnamallow(const char *user)
{
	struct ssh *ssh = active_state; /* XXX */
#ifdef HAVE_LOGIN_CAP
	extern login_cap_t *lc;
#ifdef BSD_AUTH
	auth_session_t *as;
#endif
#endif
	struct passwd *pw;
	struct connection_info *ci = get_connection_info(1, options.use_dns);

	ci->user = user;
	parse_server_match_config(&options, ci);
	log_change_level(options.log_level);
	process_permitopen(ssh, &options);

#if defined(_AIX) && defined(HAVE_SETAUTHDB)
	aix_setauthdb(user);
#endif

	pw = getpwnam(user);

#if defined(_AIX) && defined(HAVE_SETAUTHDB)
	aix_restoreauthdb();
#endif
#ifdef HAVE_CYGWIN
	/*
	 * Windows usernames are case-insensitive.  To avoid later problems
	 * when trying to match the username, the user is only allowed to
	 * login if the username is given in the same case as stored in the
	 * user database.
	 */
	if (pw != NULL && strcmp(user, pw->pw_name) != 0) {
		logit("Login name %.100s does not match stored username %.100s",
		    user, pw->pw_name);
		pw = NULL;
	}
#endif
	if (pw == NULL) {
		BLACKLIST_NOTIFY(BLACKLIST_BAD_USER, user);
		logit("Invalid user %.100s from %.100s port %d",
		    user, ssh_remote_ipaddr(ssh), ssh_remote_port(ssh));
#ifdef CUSTOM_FAILED_LOGIN
		record_failed_login(user,
		    auth_get_canonical_hostname(ssh, options.use_dns), "ssh");
#endif
#ifdef SSH_AUDIT_EVENTS
		audit_event(SSH_INVALID_USER);
#endif /* SSH_AUDIT_EVENTS */
		return (NULL);
	}
	if (!allowed_user(pw))
		return (NULL);
#ifdef HAVE_LOGIN_CAP
	if ((lc = login_getpwclass(pw)) == NULL) {
		debug("unable to get login class: %s", user);
		return (NULL);
	}
#ifdef BSD_AUTH
	if ((as = auth_open()) == NULL || auth_setpwd(as, pw) != 0 ||
	    auth_approval(as, lc, pw->pw_name, "ssh") <= 0) {
		debug("Approval failure for %s", user);
		pw = NULL;
	}
	if (as != NULL)
		auth_close(as);
#endif
#endif
	if (pw != NULL)
		return (pwcopy(pw));
	return (NULL);
}

/* Returns 1 if key is revoked by revoked_keys_file, 0 otherwise */
int
auth_key_is_revoked(struct sshkey *key)
{
	char *fp = NULL;
	int r;

	if (options.revoked_keys_file == NULL)
		return 0;
	if ((fp = sshkey_fingerprint(key, options.fingerprint_hash,
	    SSH_FP_DEFAULT)) == NULL) {
		r = SSH_ERR_ALLOC_FAIL;
		error("%s: fingerprint key: %s", __func__, ssh_err(r));
		goto out;
	}

	r = sshkey_check_revoked(key, options.revoked_keys_file);
	switch (r) {
	case 0:
		break; /* not revoked */
	case SSH_ERR_KEY_REVOKED:
		error("Authentication key %s %s revoked by file %s",
		    sshkey_type(key), fp, options.revoked_keys_file);
		goto out;
	default:
		error("Error checking authentication key %s %s in "
		    "revoked keys file %s: %s", sshkey_type(key), fp,
		    options.revoked_keys_file, ssh_err(r));
		goto out;
	}

	/* Success */
	r = 0;

 out:
	free(fp);
	return r == 0 ? 0 : 1;
}

void
auth_debug_add(const char *fmt,...)
{
	char buf[1024];
	va_list args;
	int r;

	if (auth_debug == NULL)
		return;

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	if ((r = sshbuf_put_cstring(auth_debug, buf)) != 0)
		fatal("%s: sshbuf_put_cstring: %s", __func__, ssh_err(r));
}

void
auth_debug_send(void)
{
	struct ssh *ssh = active_state;		/* XXX */
	char *msg;
	int r;

	if (auth_debug == NULL)
		return;
	while (sshbuf_len(auth_debug) != 0) {
		if ((r = sshbuf_get_cstring(auth_debug, &msg, NULL)) != 0)
			fatal("%s: sshbuf_get_cstring: %s",
			    __func__, ssh_err(r));
		ssh_packet_send_debug(ssh, "%s", msg);
		free(msg);
	}
}

void
auth_debug_reset(void)
{
	if (auth_debug != NULL)
		sshbuf_reset(auth_debug);
	else if ((auth_debug = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new failed", __func__);
}

struct passwd *
fakepw(void)
{
	static struct passwd fake;

	memset(&fake, 0, sizeof(fake));
	fake.pw_name = "NOUSER";
	fake.pw_passwd =
	    "$2a$06$r3.juUaHZDlIbQaO2dS9FuYxL1W9M81R1Tc92PoSNmzvpEqLkLGrK";
#ifdef HAVE_STRUCT_PASSWD_PW_GECOS
	fake.pw_gecos = "NOUSER";
#endif
	fake.pw_uid = privsep_pw == NULL ? (uid_t)-1 : privsep_pw->pw_uid;
	fake.pw_gid = privsep_pw == NULL ? (gid_t)-1 : privsep_pw->pw_gid;
#ifdef HAVE_STRUCT_PASSWD_PW_CLASS
	fake.pw_class = "";
#endif
	fake.pw_dir = "/nonexist";
	fake.pw_shell = "/nonexist";

	return (&fake);
}

/*
 * Returns the remote DNS hostname as a string. The returned string must not
 * be freed. NB. this will usually trigger a DNS query the first time it is
 * called.
 * This function does additional checks on the hostname to mitigate some
 * attacks on legacy rhosts-style authentication.
 * XXX is RhostsRSAAuthentication vulnerable to these?
 * XXX Can we remove these checks? (or if not, remove RhostsRSAAuthentication?)
 */

static char *
remote_hostname(struct ssh *ssh)
{
	struct sockaddr_storage from;
	socklen_t fromlen;
	struct addrinfo hints, *ai, *aitop;
	char name[NI_MAXHOST], ntop2[NI_MAXHOST];
	const char *ntop = ssh_remote_ipaddr(ssh);

	/* Get IP address of client. */
	fromlen = sizeof(from);
	memset(&from, 0, sizeof(from));
	if (getpeername(ssh_packet_get_connection_in(ssh),
	    (struct sockaddr *)&from, &fromlen) < 0) {
		debug("getpeername failed: %.100s", strerror(errno));
		return strdup(ntop);
	}

	ipv64_normalise_mapped(&from, &fromlen);
	if (from.ss_family == AF_INET6)
		fromlen = sizeof(struct sockaddr_in6);

	debug3("Trying to reverse map address %.100s.", ntop);
	/* Map the IP address to a host name. */
	if (getnameinfo((struct sockaddr *)&from, fromlen, name, sizeof(name),
	    NULL, 0, NI_NAMEREQD) != 0) {
		/* Host name not found.  Use ip address. */
		return strdup(ntop);
	}

	/*
	 * if reverse lookup result looks like a numeric hostname,
	 * someone is trying to trick us by PTR record like following:
	 *	1.1.1.10.in-addr.arpa.	IN PTR	2.3.4.5
	 */
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_DGRAM;	/*dummy*/
	hints.ai_flags = AI_NUMERICHOST;
	if (getaddrinfo(name, NULL, &hints, &ai) == 0) {
		logit("Nasty PTR record \"%s\" is set up for %s, ignoring",
		    name, ntop);
		freeaddrinfo(ai);
		return strdup(ntop);
	}

	/* Names are stored in lowercase. */
	lowercase(name);

	/*
	 * Map it back to an IP address and check that the given
	 * address actually is an address of this host.  This is
	 * necessary because anyone with access to a name server can
	 * define arbitrary names for an IP address. Mapping from
	 * name to IP address can be trusted better (but can still be
	 * fooled if the intruder has access to the name server of
	 * the domain).
	 */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = from.ss_family;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(name, NULL, &hints, &aitop) != 0) {
		logit("reverse mapping checking getaddrinfo for %.700s "
		    "[%s] failed.", name, ntop);
		return strdup(ntop);
	}
	/* Look for the address from the list of addresses. */
	for (ai = aitop; ai; ai = ai->ai_next) {
		if (getnameinfo(ai->ai_addr, ai->ai_addrlen, ntop2,
		    sizeof(ntop2), NULL, 0, NI_NUMERICHOST) == 0 &&
		    (strcmp(ntop, ntop2) == 0))
				break;
	}
	freeaddrinfo(aitop);
	/* If we reached the end of the list, the address was not there. */
	if (ai == NULL) {
		/* Address not found for the host name. */
		logit("Address %.100s maps to %.600s, but this does not "
		    "map back to the address.", ntop, name);
		return strdup(ntop);
	}
	return strdup(name);
}

/*
 * Return the canonical name of the host in the other side of the current
 * connection.  The host name is cached, so it is efficient to call this
 * several times.
 */

const char *
auth_get_canonical_hostname(struct ssh *ssh, int use_dns)
{
	static char *dnsname;

	if (!use_dns)
		return ssh_remote_ipaddr(ssh);
	else if (dnsname != NULL)
		return dnsname;
	else {
		dnsname = remote_hostname(ssh);
		return dnsname;
	}
}

/*
 * Runs command in a subprocess with a minimal environment.
 * Returns pid on success, 0 on failure.
 * The child stdout and stderr maybe captured, left attached or sent to
 * /dev/null depending on the contents of flags.
 * "tag" is prepended to log messages.
 * NB. "command" is only used for logging; the actual command executed is
 * av[0].
 */
pid_t
subprocess(const char *tag, struct passwd *pw, const char *command,
    int ac, char **av, FILE **child, u_int flags)
{
	FILE *f = NULL;
	struct stat st;
	int fd, devnull, p[2], i;
	pid_t pid;
	char *cp, errmsg[512];
	u_int envsize;
	char **child_env;

	if (child != NULL)
		*child = NULL;

	debug3("%s: %s command \"%s\" running as %s (flags 0x%x)", __func__,
	    tag, command, pw->pw_name, flags);

	/* Check consistency */
	if ((flags & SSH_SUBPROCESS_STDOUT_DISCARD) != 0 &&
	    (flags & SSH_SUBPROCESS_STDOUT_CAPTURE) != 0) {
		error("%s: inconsistent flags", __func__);
		return 0;
	}
	if (((flags & SSH_SUBPROCESS_STDOUT_CAPTURE) == 0) != (child == NULL)) {
		error("%s: inconsistent flags/output", __func__);
		return 0;
	}

	/*
	 * If executing an explicit binary, then verify the it exists
	 * and appears safe-ish to execute
	 */
	if (*av[0] != '/') {
		error("%s path is not absolute", tag);
		return 0;
	}
	temporarily_use_uid(pw);
	if (stat(av[0], &st) < 0) {
		error("Could not stat %s \"%s\": %s", tag,
		    av[0], strerror(errno));
		restore_uid();
		return 0;
	}
	if (safe_path(av[0], &st, NULL, 0, errmsg, sizeof(errmsg)) != 0) {
		error("Unsafe %s \"%s\": %s", tag, av[0], errmsg);
		restore_uid();
		return 0;
	}
	/* Prepare to keep the child's stdout if requested */
	if (pipe(p) != 0) {
		error("%s: pipe: %s", tag, strerror(errno));
		restore_uid();
		return 0;
	}
	restore_uid();

	switch ((pid = fork())) {
	case -1: /* error */
		error("%s: fork: %s", tag, strerror(errno));
		close(p[0]);
		close(p[1]);
		return 0;
	case 0: /* child */
		/* Prepare a minimal environment for the child. */
		envsize = 5;
		child_env = xcalloc(sizeof(*child_env), envsize);
		child_set_env(&child_env, &envsize, "PATH", _PATH_STDPATH);
		child_set_env(&child_env, &envsize, "USER", pw->pw_name);
		child_set_env(&child_env, &envsize, "LOGNAME", pw->pw_name);
		child_set_env(&child_env, &envsize, "HOME", pw->pw_dir);
		if ((cp = getenv("LANG")) != NULL)
			child_set_env(&child_env, &envsize, "LANG", cp);

		for (i = 0; i < NSIG; i++)
			signal(i, SIG_DFL);

		if ((devnull = open(_PATH_DEVNULL, O_RDWR)) == -1) {
			error("%s: open %s: %s", tag, _PATH_DEVNULL,
			    strerror(errno));
			_exit(1);
		}
		if (dup2(devnull, STDIN_FILENO) == -1) {
			error("%s: dup2: %s", tag, strerror(errno));
			_exit(1);
		}

		/* Set up stdout as requested; leave stderr in place for now. */
		fd = -1;
		if ((flags & SSH_SUBPROCESS_STDOUT_CAPTURE) != 0)
			fd = p[1];
		else if ((flags & SSH_SUBPROCESS_STDOUT_DISCARD) != 0)
			fd = devnull;
		if (fd != -1 && dup2(fd, STDOUT_FILENO) == -1) {
			error("%s: dup2: %s", tag, strerror(errno));
			_exit(1);
		}
		closefrom(STDERR_FILENO + 1);

		/* Don't use permanently_set_uid() here to avoid fatal() */
		if (setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) != 0) {
			error("%s: setresgid %u: %s", tag, (u_int)pw->pw_gid,
			    strerror(errno));
			_exit(1);
		}
		if (setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) != 0) {
			error("%s: setresuid %u: %s", tag, (u_int)pw->pw_uid,
			    strerror(errno));
			_exit(1);
		}
		/* stdin is pointed to /dev/null at this point */
		if ((flags & SSH_SUBPROCESS_STDOUT_DISCARD) != 0 &&
		    dup2(STDIN_FILENO, STDERR_FILENO) == -1) {
			error("%s: dup2: %s", tag, strerror(errno));
			_exit(1);
		}

		execve(av[0], av, child_env);
		error("%s exec \"%s\": %s", tag, command, strerror(errno));
		_exit(127);
	default: /* parent */
		break;
	}

	close(p[1]);
	if ((flags & SSH_SUBPROCESS_STDOUT_CAPTURE) == 0)
		close(p[0]);
	else if ((f = fdopen(p[0], "r")) == NULL) {
		error("%s: fdopen: %s", tag, strerror(errno));
		close(p[0]);
		/* Don't leave zombie child */
		kill(pid, SIGTERM);
		while (waitpid(pid, NULL, 0) == -1 && errno == EINTR)
			;
		return 0;
	}
	/* Success */
	debug3("%s: %s pid %ld", __func__, tag, (long)pid);
	if (child != NULL)
		*child = f;
	return pid;
}

/* These functions link key/cert options to the auth framework */

/* Log sshauthopt options locally and (optionally) for remote transmission */
void
auth_log_authopts(const char *loc, const struct sshauthopt *opts, int do_remote)
{
	int do_env = options.permit_user_env && opts->nenv > 0;
	int do_permitopen = opts->npermitopen > 0 &&
	    (options.allow_tcp_forwarding & FORWARD_LOCAL) != 0;
	int do_permitlisten = opts->npermitlisten > 0 &&
	    (options.allow_tcp_forwarding & FORWARD_REMOTE) != 0;
	size_t i;
	char msg[1024], buf[64];

	snprintf(buf, sizeof(buf), "%d", opts->force_tun_device);
	/* Try to keep this alphabetically sorted */
	snprintf(msg, sizeof(msg), "key options:%s%s%s%s%s%s%s%s%s%s%s%s%s",
	    opts->permit_agent_forwarding_flag ? " agent-forwarding" : "",
	    opts->force_command == NULL ? "" : " command",
	    do_env ?  " environment" : "",
	    opts->valid_before == 0 ? "" : "expires",
	    do_permitopen ?  " permitopen" : "",
	    do_permitlisten ?  " permitlisten" : "",
	    opts->permit_port_forwarding_flag ? " port-forwarding" : "",
	    opts->cert_principals == NULL ? "" : " principals",
	    opts->permit_pty_flag ? " pty" : "",
	    opts->force_tun_device == -1 ? "" : " tun=",
	    opts->force_tun_device == -1 ? "" : buf,
	    opts->permit_user_rc ? " user-rc" : "",
	    opts->permit_x11_forwarding_flag ? " x11-forwarding" : "");

	debug("%s: %s", loc, msg);
	if (do_remote)
		auth_debug_add("%s: %s", loc, msg);

	if (options.permit_user_env) {
		for (i = 0; i < opts->nenv; i++) {
			debug("%s: environment: %s", loc, opts->env[i]);
			if (do_remote) {
				auth_debug_add("%s: environment: %s",
				    loc, opts->env[i]);
			}
		}
	}

	/* Go into a little more details for the local logs. */
	if (opts->valid_before != 0) {
		format_absolute_time(opts->valid_before, buf, sizeof(buf));
		debug("%s: expires at %s", loc, buf);
	}
	if (opts->cert_principals != NULL) {
		debug("%s: authorized principals: \"%s\"",
		    loc, opts->cert_principals);
	}
	if (opts->force_command != NULL)
		debug("%s: forced command: \"%s\"", loc, opts->force_command);
	if (do_permitopen) {
		for (i = 0; i < opts->npermitopen; i++) {
			debug("%s: permitted open: %s",
			    loc, opts->permitopen[i]);
		}
	}
	if (do_permitlisten) {
		for (i = 0; i < opts->npermitlisten; i++) {
			debug("%s: permitted listen: %s",
			    loc, opts->permitlisten[i]);
		}
	}
}

/* Activate a new set of key/cert options; merging with what is there. */
int
auth_activate_options(struct ssh *ssh, struct sshauthopt *opts)
{
	struct sshauthopt *old = auth_opts;
	const char *emsg = NULL;

	debug("%s: setting new authentication options", __func__);
	if ((auth_opts = sshauthopt_merge(old, opts, &emsg)) == NULL) {
		error("Inconsistent authentication options: %s", emsg);
		return -1;
	}
	return 0;
}

/* Disable forwarding, etc for the session */
void
auth_restrict_session(struct ssh *ssh)
{
	struct sshauthopt *restricted;

	debug("%s: restricting session", __func__);

	/* A blank sshauthopt defaults to permitting nothing */
	restricted = sshauthopt_new();
	restricted->permit_pty_flag = 1;
	restricted->restricted = 1;

	if (auth_activate_options(ssh, restricted) != 0)
		fatal("%s: failed to restrict session", __func__);
	sshauthopt_free(restricted);
}

int
auth_authorise_keyopts(struct ssh *ssh, struct passwd *pw,
    struct sshauthopt *opts, int allow_cert_authority, const char *loc)
{
	const char *remote_ip = ssh_remote_ipaddr(ssh);
	const char *remote_host = auth_get_canonical_hostname(ssh,
	    options.use_dns);
	time_t now = time(NULL);
	char buf[64];

	/*
	 * Check keys/principals file expiry time.
	 * NB. validity interval in certificate is handled elsewhere.
	 */
	if (opts->valid_before && now > 0 &&
	    opts->valid_before < (uint64_t)now) {
		format_absolute_time(opts->valid_before, buf, sizeof(buf));
		debug("%s: entry expired at %s", loc, buf);
		auth_debug_add("%s: entry expired at %s", loc, buf);
		return -1;
	}
	/* Consistency checks */
	if (opts->cert_principals != NULL && !opts->cert_authority) {
		debug("%s: principals on non-CA key", loc);
		auth_debug_add("%s: principals on non-CA key", loc);
		/* deny access */
		return -1;
	}
	/* cert-authority flag isn't valid in authorized_principals files */
	if (!allow_cert_authority && opts->cert_authority) {
		debug("%s: cert-authority flag invalid here", loc);
		auth_debug_add("%s: cert-authority flag invalid here", loc);
		/* deny access */
		return -1;
	}

	/* Perform from= checks */
	if (opts->required_from_host_keys != NULL) {
		switch (match_host_and_ip(remote_host, remote_ip,
		    opts->required_from_host_keys )) {
		case 1:
			/* Host name matches. */
			break;
		case -1:
		default:
			debug("%s: invalid from criteria", loc);
			auth_debug_add("%s: invalid from criteria", loc);
			/* FALLTHROUGH */
		case 0:
			logit("%s: Authentication tried for %.100s with "
			    "correct key but not from a permitted "
			    "host (host=%.200s, ip=%.200s, required=%.200s).",
			    loc, pw->pw_name, remote_host, remote_ip,
			    opts->required_from_host_keys);
			auth_debug_add("%s: Your host '%.200s' is not "
			    "permitted to use this key for login.",
			    loc, remote_host);
			/* deny access */
			return -1;
		}
	}
	/* Check source-address restriction from certificate */
	if (opts->required_from_host_cert != NULL) {
		switch (addr_match_cidr_list(remote_ip,
		    opts->required_from_host_cert)) {
		case 1:
			/* accepted */
			break;
		case -1:
		default:
			/* invalid */
			error("%s: Certificate source-address invalid",
			    loc);
			/* FALLTHROUGH */
		case 0:
			logit("%s: Authentication tried for %.100s with valid "
			    "certificate but not from a permitted source "
			    "address (%.200s).", loc, pw->pw_name, remote_ip);
			auth_debug_add("%s: Your address '%.200s' is not "
			    "permitted to use this certificate for login.",
			    loc, remote_ip);
			return -1;
		}
	}
	/*
	 *
	 * XXX this is spammy. We should report remotely only for keys
	 *     that are successful in actual auth attempts, and not PK_OK
	 *     tests.
	 */
	auth_log_authopts(loc, opts, 1);

	return 0;
}
