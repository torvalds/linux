/* $OpenBSD: auth2.c,v 1.149 2018/07/11 18:53:29 markus Exp $ */
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
#include <sys/uio.h>

#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "atomicio.h"
#include "xmalloc.h"
#include "ssh2.h"
#include "packet.h"
#include "log.h"
#include "sshbuf.h"
#include "misc.h"
#include "servconf.h"
#include "compat.h"
#include "sshkey.h"
#include "hostfile.h"
#include "auth.h"
#include "dispatch.h"
#include "pathnames.h"
#include "sshbuf.h"
#include "ssherr.h"
#include "blacklist_client.h"

#ifdef GSSAPI
#include "ssh-gss.h"
#endif
#include "monitor_wrap.h"
#include "ssherr.h"
#include "digest.h"

/* import */
extern ServerOptions options;
extern u_char *session_id2;
extern u_int session_id2_len;
extern struct sshbuf *loginmsg;

/* methods */

extern Authmethod method_none;
extern Authmethod method_pubkey;
extern Authmethod method_passwd;
extern Authmethod method_kbdint;
extern Authmethod method_hostbased;
#ifdef GSSAPI
extern Authmethod method_gssapi;
#endif

Authmethod *authmethods[] = {
	&method_none,
	&method_pubkey,
#ifdef GSSAPI
	&method_gssapi,
#endif
	&method_passwd,
	&method_kbdint,
	&method_hostbased,
	NULL
};

/* protocol */

static int input_service_request(int, u_int32_t, struct ssh *);
static int input_userauth_request(int, u_int32_t, struct ssh *);

/* helper */
static Authmethod *authmethod_lookup(Authctxt *, const char *);
static char *authmethods_get(Authctxt *authctxt);

#define MATCH_NONE	0	/* method or submethod mismatch */
#define MATCH_METHOD	1	/* method matches (no submethod specified) */
#define MATCH_BOTH	2	/* method and submethod match */
#define MATCH_PARTIAL	3	/* method matches, submethod can't be checked */
static int list_starts_with(const char *, const char *, const char *);

char *
auth2_read_banner(void)
{
	struct stat st;
	char *banner = NULL;
	size_t len, n;
	int fd;

	if ((fd = open(options.banner, O_RDONLY)) == -1)
		return (NULL);
	if (fstat(fd, &st) == -1) {
		close(fd);
		return (NULL);
	}
	if (st.st_size <= 0 || st.st_size > 1*1024*1024) {
		close(fd);
		return (NULL);
	}

	len = (size_t)st.st_size;		/* truncate */
	banner = xmalloc(len + 1);
	n = atomicio(read, fd, banner, len);
	close(fd);

	if (n != len) {
		free(banner);
		return (NULL);
	}
	banner[n] = '\0';

	return (banner);
}

void
userauth_send_banner(const char *msg)
{
	packet_start(SSH2_MSG_USERAUTH_BANNER);
	packet_put_cstring(msg);
	packet_put_cstring("");		/* language, unused */
	packet_send();
	debug("%s: sent", __func__);
}

static void
userauth_banner(void)
{
	char *banner = NULL;

	if (options.banner == NULL)
		return;

	if ((banner = PRIVSEP(auth2_read_banner())) == NULL)
		goto done;
	userauth_send_banner(banner);

done:
	free(banner);
}

/*
 * loop until authctxt->success == TRUE
 */
void
do_authentication2(Authctxt *authctxt)
{
	struct ssh *ssh = active_state;		/* XXX */
	ssh->authctxt = authctxt;		/* XXX move to caller */
	ssh_dispatch_init(ssh, &dispatch_protocol_error);
	ssh_dispatch_set(ssh, SSH2_MSG_SERVICE_REQUEST, &input_service_request);
	ssh_dispatch_run_fatal(ssh, DISPATCH_BLOCK, &authctxt->success);
	ssh->authctxt = NULL;
}

/*ARGSUSED*/
static int
input_service_request(int type, u_int32_t seq, struct ssh *ssh)
{
	Authctxt *authctxt = ssh->authctxt;
	u_int len;
	int acceptit = 0;
	char *service = packet_get_cstring(&len);
	packet_check_eom();

	if (authctxt == NULL)
		fatal("input_service_request: no authctxt");

	if (strcmp(service, "ssh-userauth") == 0) {
		if (!authctxt->success) {
			acceptit = 1;
			/* now we can handle user-auth requests */
			ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_REQUEST, &input_userauth_request);
		}
	}
	/* XXX all other service requests are denied */

	if (acceptit) {
		packet_start(SSH2_MSG_SERVICE_ACCEPT);
		packet_put_cstring(service);
		packet_send();
		packet_write_wait();
	} else {
		debug("bad service request %s", service);
		packet_disconnect("bad service request %s", service);
	}
	free(service);
	return 0;
}

#define MIN_FAIL_DELAY_SECONDS 0.005
static double
user_specific_delay(const char *user)
{
	char b[512];
	size_t len = ssh_digest_bytes(SSH_DIGEST_SHA512);
	u_char *hash = xmalloc(len);
	double delay;

	(void)snprintf(b, sizeof b, "%llu%s",
	     (unsigned long long)options.timing_secret, user);
	if (ssh_digest_memory(SSH_DIGEST_SHA512, b, strlen(b), hash, len) != 0)
		fatal("%s: ssh_digest_memory", __func__);
	/* 0-4.2 ms of delay */
	delay = (double)PEEK_U32(hash) / 1000 / 1000 / 1000 / 1000;
	freezero(hash, len);
	debug3("%s: user specific delay %0.3lfms", __func__, delay/1000);
	return MIN_FAIL_DELAY_SECONDS + delay;
}

static void
ensure_minimum_time_since(double start, double seconds)
{
	struct timespec ts;
	double elapsed = monotime_double() - start, req = seconds, remain;

	/* if we've already passed the requested time, scale up */
	while ((remain = seconds - elapsed) < 0.0)
		seconds *= 2;

	ts.tv_sec = remain;
	ts.tv_nsec = (remain - ts.tv_sec) * 1000000000;
	debug3("%s: elapsed %0.3lfms, delaying %0.3lfms (requested %0.3lfms)",
	    __func__, elapsed*1000, remain*1000, req*1000);
	nanosleep(&ts, NULL);
}

/*ARGSUSED*/
static int
input_userauth_request(int type, u_int32_t seq, struct ssh *ssh)
{
	Authctxt *authctxt = ssh->authctxt;
	Authmethod *m = NULL;
	char *user, *service, *method, *style = NULL;
	int authenticated = 0;
	double tstart = monotime_double();
#ifdef HAVE_LOGIN_CAP
	login_cap_t *lc;
	const char *from_host, *from_ip;
#endif

	if (authctxt == NULL)
		fatal("input_userauth_request: no authctxt");

	user = packet_get_cstring(NULL);
	service = packet_get_cstring(NULL);
	method = packet_get_cstring(NULL);
	debug("userauth-request for user %s service %s method %s", user, service, method);
	debug("attempt %d failures %d", authctxt->attempt, authctxt->failures);

	if ((style = strchr(user, ':')) != NULL)
		*style++ = 0;

	if (authctxt->attempt++ == 0) {
		/* setup auth context */
		authctxt->pw = PRIVSEP(getpwnamallow(user));
		authctxt->user = xstrdup(user);
		if (authctxt->pw && strcmp(service, "ssh-connection")==0) {
			authctxt->valid = 1;
			debug2("%s: setting up authctxt for %s",
			    __func__, user);
		} else {
			/* Invalid user, fake password information */
			authctxt->pw = fakepw();
#ifdef SSH_AUDIT_EVENTS
			PRIVSEP(audit_event(SSH_INVALID_USER));
#endif
		}
#ifdef USE_PAM
		if (options.use_pam)
			PRIVSEP(start_pam(authctxt));
#endif
		ssh_packet_set_log_preamble(ssh, "%suser %s",
		    authctxt->valid ? "authenticating " : "invalid ", user);
		setproctitle("%s%s", authctxt->valid ? user : "unknown",
		    use_privsep ? " [net]" : "");
		authctxt->service = xstrdup(service);
		authctxt->style = style ? xstrdup(style) : NULL;
		if (use_privsep)
			mm_inform_authserv(service, style);
		userauth_banner();
		if (auth2_setup_methods_lists(authctxt) != 0)
			packet_disconnect("no authentication methods enabled");
	} else if (strcmp(user, authctxt->user) != 0 ||
	    strcmp(service, authctxt->service) != 0) {
		packet_disconnect("Change of username or service not allowed: "
		    "(%s,%s) -> (%s,%s)",
		    authctxt->user, authctxt->service, user, service);
	}

#ifdef HAVE_LOGIN_CAP
	if (authctxt->pw != NULL &&
	    (lc = PRIVSEP(login_getpwclass(authctxt->pw))) != NULL) {
		logit("user %s login class %s", authctxt->pw->pw_name,
		    authctxt->pw->pw_class);
		from_host = auth_get_canonical_hostname(ssh, options.use_dns);
		from_ip = ssh_remote_ipaddr(ssh);
		if (!auth_hostok(lc, from_host, from_ip)) {
			logit("Denied connection for %.200s from %.200s [%.200s].",
			    authctxt->pw->pw_name, from_host, from_ip);
			packet_disconnect("Sorry, you are not allowed to connect.");
		}
		if (!auth_timeok(lc, time(NULL))) {
			logit("LOGIN %.200s REFUSED (TIME) FROM %.200s",
			    authctxt->pw->pw_name, from_host);
			packet_disconnect("Logins not available right now.");
		}
		PRIVSEP(login_close(lc));
	}
#endif  /* HAVE_LOGIN_CAP */

	/* reset state */
	auth2_challenge_stop(ssh);

#ifdef GSSAPI
	/* XXX move to auth2_gssapi_stop() */
	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_GSSAPI_TOKEN, NULL);
	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_GSSAPI_EXCHANGE_COMPLETE, NULL);
#endif

	auth2_authctxt_reset_info(authctxt);
	authctxt->postponed = 0;
	authctxt->server_caused_failure = 0;

	/* try to authenticate user */
	m = authmethod_lookup(authctxt, method);
	if (m != NULL && authctxt->failures < options.max_authtries) {
		debug2("input_userauth_request: try method %s", method);
		authenticated =	m->userauth(ssh);
	}
	if (!authctxt->authenticated)
		ensure_minimum_time_since(tstart,
		    user_specific_delay(authctxt->user));
	userauth_finish(ssh, authenticated, method, NULL);

	free(service);
	free(user);
	free(method);
	return 0;
}

void
userauth_finish(struct ssh *ssh, int authenticated, const char *method,
    const char *submethod)
{
	Authctxt *authctxt = ssh->authctxt;
	char *methods;
	int partial = 0;

	if (!authctxt->valid && authenticated)
		fatal("INTERNAL ERROR: authenticated invalid user %s",
		    authctxt->user);
	if (authenticated && authctxt->postponed)
		fatal("INTERNAL ERROR: authenticated and postponed");

	/* Special handling for root */
	if (authenticated && authctxt->pw->pw_uid == 0 &&
	    !auth_root_allowed(ssh, method)) {
		authenticated = 0;
#ifdef SSH_AUDIT_EVENTS
		PRIVSEP(audit_event(SSH_LOGIN_ROOT_DENIED));
#endif
	}

	if (authenticated && options.num_auth_methods != 0) {
		if (!auth2_update_methods_lists(authctxt, method, submethod)) {
			authenticated = 0;
			partial = 1;
		}
	}

	/* Log before sending the reply */
	auth_log(authctxt, authenticated, partial, method, submethod);

	/* Update information exposed to session */
	if (authenticated || partial)
		auth2_update_session_info(authctxt, method, submethod);

	if (authctxt->postponed)
		return;

#ifdef USE_PAM
	if (options.use_pam && authenticated) {
		int r;

		if (!PRIVSEP(do_pam_account())) {
			/* if PAM returned a message, send it to the user */
			if (sshbuf_len(loginmsg) > 0) {
				if ((r = sshbuf_put(loginmsg, "\0", 1)) != 0)
					fatal("%s: buffer error: %s",
					    __func__, ssh_err(r));
				userauth_send_banner(sshbuf_ptr(loginmsg));
				packet_write_wait();
			}
			fatal("Access denied for user %s by PAM account "
			    "configuration", authctxt->user);
		}
	}
#endif

	if (authenticated == 1) {
		/* turn off userauth */
		ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_REQUEST, &dispatch_protocol_ignore);
		packet_start(SSH2_MSG_USERAUTH_SUCCESS);
		packet_send();
		packet_write_wait();
		/* now we can break out */
		authctxt->success = 1;
		ssh_packet_set_log_preamble(ssh, "user %s", authctxt->user);
	} else {
		/* Allow initial try of "none" auth without failure penalty */
		if (!partial && !authctxt->server_caused_failure &&
		    (authctxt->attempt > 1 || strcmp(method, "none") != 0)) {
			authctxt->failures++;
			BLACKLIST_NOTIFY(BLACKLIST_AUTH_FAIL, "ssh");
		}
		if (authctxt->failures >= options.max_authtries) {
#ifdef SSH_AUDIT_EVENTS
			PRIVSEP(audit_event(SSH_LOGIN_EXCEED_MAXTRIES));
#endif
			auth_maxtries_exceeded(authctxt);
		}
		methods = authmethods_get(authctxt);
		debug3("%s: failure partial=%d next methods=\"%s\"", __func__,
		    partial, methods);
		packet_start(SSH2_MSG_USERAUTH_FAILURE);
		packet_put_cstring(methods);
		packet_put_char(partial);
		packet_send();
		packet_write_wait();
		free(methods);
	}
}

/*
 * Checks whether method is allowed by at least one AuthenticationMethods
 * methods list. Returns 1 if allowed, or no methods lists configured.
 * 0 otherwise.
 */
int
auth2_method_allowed(Authctxt *authctxt, const char *method,
    const char *submethod)
{
	u_int i;

	/*
	 * NB. authctxt->num_auth_methods might be zero as a result of
	 * auth2_setup_methods_lists(), so check the configuration.
	 */
	if (options.num_auth_methods == 0)
		return 1;
	for (i = 0; i < authctxt->num_auth_methods; i++) {
		if (list_starts_with(authctxt->auth_methods[i], method,
		    submethod) != MATCH_NONE)
			return 1;
	}
	return 0;
}

static char *
authmethods_get(Authctxt *authctxt)
{
	struct sshbuf *b;
	char *list;
	int i, r;

	if ((b = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new failed", __func__);
	for (i = 0; authmethods[i] != NULL; i++) {
		if (strcmp(authmethods[i]->name, "none") == 0)
			continue;
		if (authmethods[i]->enabled == NULL ||
		    *(authmethods[i]->enabled) == 0)
			continue;
		if (!auth2_method_allowed(authctxt, authmethods[i]->name,
		    NULL))
			continue;
		if ((r = sshbuf_putf(b, "%s%s", sshbuf_len(b) ? "," : "",
		    authmethods[i]->name)) != 0)
			fatal("%s: buffer error: %s", __func__, ssh_err(r));
	}
	if ((list = sshbuf_dup_string(b)) == NULL)
		fatal("%s: sshbuf_dup_string failed", __func__);
	sshbuf_free(b);
	return list;
}

static Authmethod *
authmethod_lookup(Authctxt *authctxt, const char *name)
{
	int i;

	if (name != NULL)
		for (i = 0; authmethods[i] != NULL; i++)
			if (authmethods[i]->enabled != NULL &&
			    *(authmethods[i]->enabled) != 0 &&
			    strcmp(name, authmethods[i]->name) == 0 &&
			    auth2_method_allowed(authctxt,
			    authmethods[i]->name, NULL))
				return authmethods[i];
	debug2("Unrecognized authentication method name: %s",
	    name ? name : "NULL");
	return NULL;
}

/*
 * Check a comma-separated list of methods for validity. Is need_enable is
 * non-zero, then also require that the methods are enabled.
 * Returns 0 on success or -1 if the methods list is invalid.
 */
int
auth2_methods_valid(const char *_methods, int need_enable)
{
	char *methods, *omethods, *method, *p;
	u_int i, found;
	int ret = -1;

	if (*_methods == '\0') {
		error("empty authentication method list");
		return -1;
	}
	omethods = methods = xstrdup(_methods);
	while ((method = strsep(&methods, ",")) != NULL) {
		for (found = i = 0; !found && authmethods[i] != NULL; i++) {
			if ((p = strchr(method, ':')) != NULL)
				*p = '\0';
			if (strcmp(method, authmethods[i]->name) != 0)
				continue;
			if (need_enable) {
				if (authmethods[i]->enabled == NULL ||
				    *(authmethods[i]->enabled) == 0) {
					error("Disabled method \"%s\" in "
					    "AuthenticationMethods list \"%s\"",
					    method, _methods);
					goto out;
				}
			}
			found = 1;
			break;
		}
		if (!found) {
			error("Unknown authentication method \"%s\" in list",
			    method);
			goto out;
		}
	}
	ret = 0;
 out:
	free(omethods);
	return ret;
}

/*
 * Prune the AuthenticationMethods supplied in the configuration, removing
 * any methods lists that include disabled methods. Note that this might
 * leave authctxt->num_auth_methods == 0, even when multiple required auth
 * has been requested. For this reason, all tests for whether multiple is
 * enabled should consult options.num_auth_methods directly.
 */
int
auth2_setup_methods_lists(Authctxt *authctxt)
{
	u_int i;

	if (options.num_auth_methods == 0)
		return 0;
	debug3("%s: checking methods", __func__);
	authctxt->auth_methods = xcalloc(options.num_auth_methods,
	    sizeof(*authctxt->auth_methods));
	authctxt->num_auth_methods = 0;
	for (i = 0; i < options.num_auth_methods; i++) {
		if (auth2_methods_valid(options.auth_methods[i], 1) != 0) {
			logit("Authentication methods list \"%s\" contains "
			    "disabled method, skipping",
			    options.auth_methods[i]);
			continue;
		}
		debug("authentication methods list %d: %s",
		    authctxt->num_auth_methods, options.auth_methods[i]);
		authctxt->auth_methods[authctxt->num_auth_methods++] =
		    xstrdup(options.auth_methods[i]);
	}
	if (authctxt->num_auth_methods == 0) {
		error("No AuthenticationMethods left after eliminating "
		    "disabled methods");
		return -1;
	}
	return 0;
}

static int
list_starts_with(const char *methods, const char *method,
    const char *submethod)
{
	size_t l = strlen(method);
	int match;
	const char *p;

	if (strncmp(methods, method, l) != 0)
		return MATCH_NONE;
	p = methods + l;
	match = MATCH_METHOD;
	if (*p == ':') {
		if (!submethod)
			return MATCH_PARTIAL;
		l = strlen(submethod);
		p += 1;
		if (strncmp(submethod, p, l))
			return MATCH_NONE;
		p += l;
		match = MATCH_BOTH;
	}
	if (*p != ',' && *p != '\0')
		return MATCH_NONE;
	return match;
}

/*
 * Remove method from the start of a comma-separated list of methods.
 * Returns 0 if the list of methods did not start with that method or 1
 * if it did.
 */
static int
remove_method(char **methods, const char *method, const char *submethod)
{
	char *omethods = *methods, *p;
	size_t l = strlen(method);
	int match;

	match = list_starts_with(omethods, method, submethod);
	if (match != MATCH_METHOD && match != MATCH_BOTH)
		return 0;
	p = omethods + l;
	if (submethod && match == MATCH_BOTH)
		p += 1 + strlen(submethod); /* include colon */
	if (*p == ',')
		p++;
	*methods = xstrdup(p);
	free(omethods);
	return 1;
}

/*
 * Called after successful authentication. Will remove the successful method
 * from the start of each list in which it occurs. If it was the last method
 * in any list, then authentication is deemed successful.
 * Returns 1 if the method completed any authentication list or 0 otherwise.
 */
int
auth2_update_methods_lists(Authctxt *authctxt, const char *method,
    const char *submethod)
{
	u_int i, found = 0;

	debug3("%s: updating methods list after \"%s\"", __func__, method);
	for (i = 0; i < authctxt->num_auth_methods; i++) {
		if (!remove_method(&(authctxt->auth_methods[i]), method,
		    submethod))
			continue;
		found = 1;
		if (*authctxt->auth_methods[i] == '\0') {
			debug2("authentication methods list %d complete", i);
			return 1;
		}
		debug3("authentication methods list %d remaining: \"%s\"",
		    i, authctxt->auth_methods[i]);
	}
	/* This should not happen, but would be bad if it did */
	if (!found)
		fatal("%s: method not in AuthenticationMethods", __func__);
	return 0;
}

/* Reset method-specific information */
void auth2_authctxt_reset_info(Authctxt *authctxt)
{
	sshkey_free(authctxt->auth_method_key);
	free(authctxt->auth_method_info);
	authctxt->auth_method_key = NULL;
	authctxt->auth_method_info = NULL;
}

/* Record auth method-specific information for logs */
void
auth2_record_info(Authctxt *authctxt, const char *fmt, ...)
{
	va_list ap;
        int i;

	free(authctxt->auth_method_info);
	authctxt->auth_method_info = NULL;

	va_start(ap, fmt);
	i = vasprintf(&authctxt->auth_method_info, fmt, ap);
	va_end(ap);

	if (i < 0 || authctxt->auth_method_info == NULL)
		fatal("%s: vasprintf failed", __func__);
}

/*
 * Records a public key used in authentication. This is used for logging
 * and to ensure that the same key is not subsequently accepted again for
 * multiple authentication.
 */
void
auth2_record_key(Authctxt *authctxt, int authenticated,
    const struct sshkey *key)
{
	struct sshkey **tmp, *dup;
	int r;

	if ((r = sshkey_from_private(key, &dup)) != 0)
		fatal("%s: copy key: %s", __func__, ssh_err(r));
	sshkey_free(authctxt->auth_method_key);
	authctxt->auth_method_key = dup;

	if (!authenticated)
		return;

	/* If authenticated, make sure we don't accept this key again */
	if ((r = sshkey_from_private(key, &dup)) != 0)
		fatal("%s: copy key: %s", __func__, ssh_err(r));
	if (authctxt->nprev_keys >= INT_MAX ||
	    (tmp = recallocarray(authctxt->prev_keys, authctxt->nprev_keys,
	    authctxt->nprev_keys + 1, sizeof(*authctxt->prev_keys))) == NULL)
		fatal("%s: reallocarray failed", __func__);
	authctxt->prev_keys = tmp;
	authctxt->prev_keys[authctxt->nprev_keys] = dup;
	authctxt->nprev_keys++;

}

/* Checks whether a key has already been previously used for authentication */
int
auth2_key_already_used(Authctxt *authctxt, const struct sshkey *key)
{
	u_int i;
	char *fp;

	for (i = 0; i < authctxt->nprev_keys; i++) {
		if (sshkey_equal_public(key, authctxt->prev_keys[i])) {
			fp = sshkey_fingerprint(authctxt->prev_keys[i],
			    options.fingerprint_hash, SSH_FP_DEFAULT);
			debug3("%s: key already used: %s %s", __func__,
			    sshkey_type(authctxt->prev_keys[i]),
			    fp == NULL ? "UNKNOWN" : fp);
			free(fp);
			return 1;
		}
	}
	return 0;
}

/*
 * Updates authctxt->session_info with details of authentication. Should be
 * whenever an authentication method succeeds.
 */
void
auth2_update_session_info(Authctxt *authctxt, const char *method,
    const char *submethod)
{
	int r;

	if (authctxt->session_info == NULL) {
		if ((authctxt->session_info = sshbuf_new()) == NULL)
			fatal("%s: sshbuf_new", __func__);
	}

	/* Append method[/submethod] */
	if ((r = sshbuf_putf(authctxt->session_info, "%s%s%s",
	    method, submethod == NULL ? "" : "/",
	    submethod == NULL ? "" : submethod)) != 0)
		fatal("%s: append method: %s", __func__, ssh_err(r));

	/* Append key if present */
	if (authctxt->auth_method_key != NULL) {
		if ((r = sshbuf_put_u8(authctxt->session_info, ' ')) != 0 ||
		    (r = sshkey_format_text(authctxt->auth_method_key,
		    authctxt->session_info)) != 0)
			fatal("%s: append key: %s", __func__, ssh_err(r));
	}

	if (authctxt->auth_method_info != NULL) {
		/* Ensure no ambiguity here */
		if (strchr(authctxt->auth_method_info, '\n') != NULL)
			fatal("%s: auth_method_info contains \\n", __func__);
		if ((r = sshbuf_put_u8(authctxt->session_info, ' ')) != 0 ||
		    (r = sshbuf_putf(authctxt->session_info, "%s",
		    authctxt->auth_method_info)) != 0) {
			fatal("%s: append method info: %s",
			    __func__, ssh_err(r));
		}
	}
	if ((r = sshbuf_put_u8(authctxt->session_info, '\n')) != 0)
		fatal("%s: append: %s", __func__, ssh_err(r));
}

