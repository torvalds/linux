/*
 *
 * Copyright (c) 2001 Gert Doering.  All rights reserved.
 * Copyright (c) 2003,2004,2005,2006 Darren Tucker.  All rights reserved.
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
 *
 */
#include "includes.h"

#include "xmalloc.h"
#include "sshbuf.h"
#include "ssherr.h"
#include "sshkey.h"
#include "hostfile.h"
#include "auth.h"
#include "ssh.h"
#include "ssh_api.h"
#include "log.h"

#ifdef _AIX

#include <errno.h>
#if defined(HAVE_NETDB_H)
# include <netdb.h>
#endif
#include <uinfo.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#ifdef WITH_AIXAUTHENTICATE
# include <login.h>
# include <userpw.h>
# if defined(HAVE_SYS_AUDIT_H) && defined(AIX_LOGINFAILED_4ARG)
#  include <sys/audit.h>
# endif
# include <usersec.h>
#endif

#include "port-aix.h"

static char *lastlogin_msg = NULL;

# ifdef HAVE_SETAUTHDB
static char old_registry[REGISTRY_SIZE] = "";
# endif

/*
 * AIX has a "usrinfo" area where logname and other stuff is stored -
 * a few applications actually use this and die if it's not set
 *
 * NOTE: TTY= should be set, but since no one uses it and it's hard to
 * acquire due to privsep code.  We will just drop support.
 */
void
aix_usrinfo(struct passwd *pw)
{
	u_int i;
	size_t len;
	char *cp;

	len = sizeof("LOGNAME= NAME= ") + (2 * strlen(pw->pw_name));
	cp = xmalloc(len);

	i = snprintf(cp, len, "LOGNAME=%s%cNAME=%s%c", pw->pw_name, '\0',
	    pw->pw_name, '\0');
	if (usrinfo(SETUINFO, cp, i) == -1)
		fatal("Couldn't set usrinfo: %s", strerror(errno));
	debug3("AIX/UsrInfo: set len %d", i);

	free(cp);
}

# ifdef WITH_AIXAUTHENTICATE
/*
 * Remove embedded newlines in string (if any).
 * Used before logging messages returned by AIX authentication functions
 * so the message is logged on one line.
 */
void
aix_remove_embedded_newlines(char *p)
{
	if (p == NULL)
		return;

	for (; *p; p++) {
		if (*p == '\n')
			*p = ' ';
	}
	/* Remove trailing whitespace */
	if (*--p == ' ')
		*p = '\0';
}

/*
 * Test specifically for the case where SYSTEM == NONE and AUTH1 contains
 * anything other than NONE or SYSTEM, which indicates that the admin has
 * configured the account for purely AUTH1-type authentication.
 *
 * Since authenticate() doesn't check AUTH1, and sshd can't sanely support
 * AUTH1 itself, in such a case authenticate() will allow access without
 * authentation, which is almost certainly not what the admin intends.
 *
 * (The native tools, eg login, will process the AUTH1 list in addition to
 * the SYSTEM list by using ckuserID(), however ckuserID() and AUTH1 methods
 * have been deprecated since AIX 4.2.x and would be very difficult for sshd
 * to support.
 *
 * Returns 0 if an unsupportable combination is found, 1 otherwise.
 */
static int
aix_valid_authentications(const char *user)
{
	char *auth1, *sys, *p;
	int valid = 1;

	if (getuserattr((char *)user, S_AUTHSYSTEM, &sys, SEC_CHAR) != 0) {
		logit("Can't retrieve attribute SYSTEM for %s: %.100s",
		    user, strerror(errno));
		return 0;
	}

	debug3("AIX SYSTEM attribute %s", sys);
	if (strcmp(sys, "NONE") != 0)
		return 1;	/* not "NONE", so is OK */

	if (getuserattr((char *)user, S_AUTH1, &auth1, SEC_LIST) != 0) {
		logit("Can't retrieve attribute auth1 for %s: %.100s",
		    user, strerror(errno));
		return 0;
	}

	p = auth1;
	/* A SEC_LIST is concatenated strings, ending with two NULs. */
	while (p[0] != '\0' && p[1] != '\0') {
		debug3("AIX auth1 attribute list member %s", p);
		if (strcmp(p, "NONE") != 0 && strcmp(p, "SYSTEM")) {
			logit("Account %s has unsupported auth1 value '%s'",
			    user, p);
			valid = 0;
		}
		p += strlen(p) + 1;
	}

	return (valid);
}

/*
 * Do authentication via AIX's authenticate routine.  We loop until the
 * reenter parameter is 0, but normally authenticate is called only once.
 *
 * Note: this function returns 1 on success, whereas AIX's authenticate()
 * returns 0.
 */
int
sys_auth_passwd(struct ssh *ssh, const char *password)
{
	Authctxt *ctxt = ssh->authctxt;
	char *authmsg = NULL, *msg = NULL, *name = ctxt->pw->pw_name;
	int r, authsuccess = 0, expired, reenter, result;

	do {
		result = authenticate((char *)name, (char *)password, &reenter,
		    &authmsg);
		aix_remove_embedded_newlines(authmsg);
		debug3("AIX/authenticate result %d, authmsg %.100s", result,
		    authmsg);
	} while (reenter);

	if (!aix_valid_authentications(name))
		result = -1;

	if (result == 0) {
		authsuccess = 1;

		/*
		 * Record successful login.  We don't have a pty yet, so just
		 * label the line as "ssh"
		 */
		aix_setauthdb(name);

		/*
		 * Check if the user's password is expired.
		 */
		expired = passwdexpired(name, &msg);
		if (msg && *msg) {
			if ((r = sshbuf_put(ctxt->loginmsg,
			    msg, strlen(msg))) != 0)
				fatal("%s: buffer error: %s",
				    __func__, ssh_err(r));
			aix_remove_embedded_newlines(msg);
		}
		debug3("AIX/passwdexpired returned %d msg %.100s", expired, msg);

		switch (expired) {
		case 0: /* password not expired */
			break;
		case 1: /* expired, password change required */
			ctxt->force_pwchange = 1;
			break;
		default: /* user can't change(2) or other error (-1) */
			logit("Password can't be changed for user %s: %.100s",
			    name, msg);
			free(msg);
			authsuccess = 0;
		}

		aix_restoreauthdb();
	}

	free(authmsg);

	return authsuccess;
}

/*
 * Check if specified account is permitted to log in.
 * Returns 1 if login is allowed, 0 if not allowed.
 */
int
sys_auth_allowed_user(struct passwd *pw, struct sshbuf *loginmsg)
{
	char *msg = NULL;
	int r, result, permitted = 0;
	struct stat st;

	/*
	 * Don't perform checks for root account (PermitRootLogin controls
	 * logins via ssh) or if running as non-root user (since
	 * loginrestrictions will always fail due to insufficient privilege).
	 */
	if (pw->pw_uid == 0 || geteuid() != 0) {
		debug3("%s: not checking", __func__);
		return 1;
	}

	result = loginrestrictions(pw->pw_name, S_RLOGIN, NULL, &msg);
	if (result == 0)
		permitted = 1;
	/*
	 * If restricted because /etc/nologin exists, the login will be denied
	 * in session.c after the nologin message is sent, so allow for now
	 * and do not append the returned message.
	 */
	if (result == -1 && errno == EPERM && stat(_PATH_NOLOGIN, &st) == 0)
		permitted = 1;
	else if (msg != NULL) {
		if ((r = sshbuf_put(loginmsg, msg, strlen(msg))) != 0)
			fatal("%s: buffer error: %s", __func__, ssh_err(r));
	}
	if (msg == NULL)
		msg = xstrdup("(none)");
	aix_remove_embedded_newlines(msg);
	debug3("AIX/loginrestrictions returned %d msg %.100s", result, msg);

	if (!permitted)
		logit("Login restricted for %s: %.100s", pw->pw_name, msg);
	free(msg);
	return permitted;
}

int
sys_auth_record_login(const char *user, const char *host, const char *ttynm,
    struct sshbuf *loginmsg)
{
	char *msg = NULL;
	int success = 0;

	aix_setauthdb(user);
	if (loginsuccess((char *)user, (char *)host, (char *)ttynm, &msg) == 0) {
		success = 1;
		if (msg != NULL) {
			debug("AIX/loginsuccess: msg %s", msg);
			if (lastlogin_msg == NULL)
				lastlogin_msg = msg;
		}
	}
	aix_restoreauthdb();
	return (success);
}

char *
sys_auth_get_lastlogin_msg(const char *user, uid_t uid)
{
	char *msg = lastlogin_msg;

	lastlogin_msg = NULL;
	return msg;
}

#  ifdef CUSTOM_FAILED_LOGIN
/*
 * record_failed_login: generic "login failed" interface function
 */
void
record_failed_login(const char *user, const char *hostname, const char *ttyname)
{
	if (geteuid() != 0)
		return;

	aix_setauthdb(user);
#   ifdef AIX_LOGINFAILED_4ARG
	loginfailed((char *)user, (char *)hostname, (char *)ttyname,
	    AUDIT_FAIL_AUTH);
#   else
	loginfailed((char *)user, (char *)hostname, (char *)ttyname);
#   endif
	aix_restoreauthdb();
}
#  endif /* CUSTOM_FAILED_LOGIN */

/*
 * If we have setauthdb, retrieve the password registry for the user's
 * account then feed it to setauthdb.  This will mean that subsequent AIX auth
 * functions will only use the specified loadable module.  If we don't have
 * setauthdb this is a no-op.
 */
void
aix_setauthdb(const char *user)
{
#  ifdef HAVE_SETAUTHDB
	char *registry;

	if (setuserdb(S_READ) == -1) {
		debug3("%s: Could not open userdb to read", __func__);
		return;
	}

	if (getuserattr((char *)user, S_REGISTRY, &registry, SEC_CHAR) == 0) {
		if (setauthdb(registry, old_registry) == 0)
			debug3("AIX/setauthdb set registry '%s'", registry);
		else
			debug3("AIX/setauthdb set registry '%s' failed: %s",
			    registry, strerror(errno));
	} else
		debug3("%s: Could not read S_REGISTRY for user: %s", __func__,
		    strerror(errno));
	enduserdb();
#  endif /* HAVE_SETAUTHDB */
}

/*
 * Restore the user's registry settings from old_registry.
 * Note that if the first aix_setauthdb fails, setauthdb("") is still safe
 * (it restores the system default behaviour).  If we don't have setauthdb,
 * this is a no-op.
 */
void
aix_restoreauthdb(void)
{
#  ifdef HAVE_SETAUTHDB
	if (setauthdb(old_registry, NULL) == 0)
		debug3("%s: restoring old registry '%s'", __func__,
		    old_registry);
	else
		debug3("%s: failed to restore old registry %s", __func__,
		    old_registry);
#  endif /* HAVE_SETAUTHDB */
}

# endif /* WITH_AIXAUTHENTICATE */

# ifdef USE_AIX_KRB_NAME
/*
 * aix_krb5_get_principal_name: returns the user's kerberos client principal name if
 * configured, otherwise NULL.  Caller must free returned string.
 */
char *
aix_krb5_get_principal_name(char *pw_name)
{
	char *authname = NULL, *authdomain = NULL, *principal = NULL;

	setuserdb(S_READ);
	if (getuserattr(pw_name, S_AUTHDOMAIN, &authdomain, SEC_CHAR) != 0)
		debug("AIX getuserattr S_AUTHDOMAIN: %s", strerror(errno));
	if (getuserattr(pw_name, S_AUTHNAME, &authname, SEC_CHAR) != 0)
		debug("AIX getuserattr S_AUTHNAME: %s", strerror(errno));

	if (authdomain != NULL)
		xasprintf(&principal, "%s@%s", authname ? authname : pw_name, authdomain);
	else if (authname != NULL)
		principal = xstrdup(authname);
	enduserdb();
	return principal;
}
# endif /* USE_AIX_KRB_NAME */

# if defined(AIX_GETNAMEINFO_HACK) && !defined(BROKEN_ADDRINFO)
# undef getnameinfo
/*
 * For some reason, AIX's getnameinfo will refuse to resolve the all-zeros
 * IPv6 address into its textual representation ("::"), so we wrap it
 * with a function that will.
 */
int
sshaix_getnameinfo(const struct sockaddr *sa, size_t salen, char *host,
    size_t hostlen, char *serv, size_t servlen, int flags)
{
	struct sockaddr_in6 *sa6;
	u_int32_t *a6;

	if (flags & (NI_NUMERICHOST|NI_NUMERICSERV) &&
	    sa->sa_family == AF_INET6) {
		sa6 = (struct sockaddr_in6 *)sa;
		a6 = sa6->sin6_addr.u6_addr.u6_addr32;

		if (a6[0] == 0 && a6[1] == 0 && a6[2] == 0 && a6[3] == 0) {
			strlcpy(host, "::", hostlen);
			snprintf(serv, servlen, "%d", sa6->sin6_port);
			return 0;
		}
	}
	return getnameinfo(sa, salen, host, hostlen, serv, servlen, flags);
}
# endif /* AIX_GETNAMEINFO_HACK */

# if defined(USE_GETGRSET)
#  include <stdlib.h>
int
getgrouplist(const char *user, gid_t pgid, gid_t *groups, int *grpcnt)
{
	char *cp, *grplist, *grp;
	gid_t gid;
	int ret = 0, ngroups = 0, maxgroups;
	long l;

	maxgroups = *grpcnt;

	if ((cp = grplist = getgrset(user)) == NULL)
		return -1;

	/* handle zero-length case */
	if (maxgroups <= 0) {
		*grpcnt = 0;
		return -1;
	}

	/* copy primary group */
	groups[ngroups++] = pgid;

	/* copy each entry from getgrset into group list */
	while ((grp = strsep(&grplist, ",")) != NULL) {
		l = strtol(grp, NULL, 10);
		if (ngroups >= maxgroups || l == LONG_MIN || l == LONG_MAX) {
			ret = -1;
			goto out;
		}
		gid = (gid_t)l;
		if (gid == pgid)
			continue;	/* we have already added primary gid */
		groups[ngroups++] = gid;
	}
out:
	free(cp);
	*grpcnt = ngroups;
	return ret;
}
# endif	/* USE_GETGRSET */

#endif /* _AIX */
