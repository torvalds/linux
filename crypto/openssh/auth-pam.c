/*-
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by ThinkSec AS and
 * NAI Labs, the Security Research Division of Network Associates, Inc.
 * under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the
 * DARPA CHATS research program.
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
 */
/*
 * Copyright (c) 2003,2004 Damien Miller <djm@mindrot.org>
 * Copyright (c) 2003,2004 Darren Tucker <dtucker@zip.com.au>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Based on FreeBSD: src/crypto/openssh/auth2-pam-freebsd.c,v 1.11 2003/03/31 13:48:18 des */

#include "includes.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#ifdef USE_PAM
#if defined(HAVE_SECURITY_PAM_APPL_H)
#include <security/pam_appl.h>
#elif defined (HAVE_PAM_PAM_APPL_H)
#include <pam/pam_appl.h>
#endif

#if !defined(SSHD_PAM_SERVICE)
extern char *__progname;
# define SSHD_PAM_SERVICE		__progname
#endif

/* OpenGroup RFC86.0 and XSSO specify no "const" on arguments */
#ifdef PAM_SUN_CODEBASE
# define sshpam_const		/* Solaris, HP-UX, SunOS */
#else
# define sshpam_const	const	/* LinuxPAM, OpenPAM, AIX */
#endif

/* Ambiguity in spec: is it an array of pointers or a pointer to an array? */
#ifdef PAM_SUN_CODEBASE
# define PAM_MSG_MEMBER(msg, n, member) ((*(msg))[(n)].member)
#else
# define PAM_MSG_MEMBER(msg, n, member) ((msg)[(n)]->member)
#endif

#include "xmalloc.h"
#include "sshbuf.h"
#include "ssherr.h"
#include "hostfile.h"
#include "auth.h"
#include "auth-pam.h"
#include "canohost.h"
#include "log.h"
#include "msg.h"
#include "packet.h"
#include "misc.h"
#include "servconf.h"
#include "ssh2.h"
#include "auth-options.h"
#ifdef GSSAPI
#include "ssh-gss.h"
#endif
#include "monitor_wrap.h"
#include "blacklist_client.h"

extern ServerOptions options;
extern struct sshbuf *loginmsg;
extern u_int utmp_len;

/* so we don't silently change behaviour */
#ifdef USE_POSIX_THREADS
# error "USE_POSIX_THREADS replaced by UNSUPPORTED_POSIX_THREADS_HACK"
#endif

/*
 * Formerly known as USE_POSIX_THREADS, using this is completely unsupported
 * and generally a bad idea.  Use at own risk and do not expect support if
 * this breaks.
 */
#ifdef UNSUPPORTED_POSIX_THREADS_HACK
#include <pthread.h>
/*
 * Avoid namespace clash when *not* using pthreads for systems *with*
 * pthreads, which unconditionally define pthread_t via sys/types.h
 * (e.g. Linux)
 */
typedef pthread_t sp_pthread_t;
#else
typedef pid_t sp_pthread_t;
#define pthread_exit	fake_pthread_exit
#define pthread_create	fake_pthread_create
#define pthread_cancel	fake_pthread_cancel
#define pthread_join	fake_pthread_join
#endif

struct pam_ctxt {
	sp_pthread_t	 pam_thread;
	int		 pam_psock;
	int		 pam_csock;
	int		 pam_done;
};

static void sshpam_free_ctx(void *);
static struct pam_ctxt *cleanup_ctxt;

#ifndef UNSUPPORTED_POSIX_THREADS_HACK
/*
 * Simulate threads with processes.
 */

static int sshpam_thread_status = -1;
static mysig_t sshpam_oldsig;

static void
sshpam_sigchld_handler(int sig)
{
	signal(SIGCHLD, SIG_DFL);
	if (cleanup_ctxt == NULL)
		return;	/* handler called after PAM cleanup, shouldn't happen */
	if (waitpid(cleanup_ctxt->pam_thread, &sshpam_thread_status, WNOHANG)
	    <= 0) {
		/* PAM thread has not exitted, privsep slave must have */
		kill(cleanup_ctxt->pam_thread, SIGTERM);
		while (waitpid(cleanup_ctxt->pam_thread,
		    &sshpam_thread_status, 0) == -1) {
			if (errno == EINTR)
				continue;
			return;
		}
	}
	if (WIFSIGNALED(sshpam_thread_status) &&
	    WTERMSIG(sshpam_thread_status) == SIGTERM)
		return;	/* terminated by pthread_cancel */
	if (!WIFEXITED(sshpam_thread_status))
		sigdie("PAM: authentication thread exited unexpectedly");
	if (WEXITSTATUS(sshpam_thread_status) != 0)
		sigdie("PAM: authentication thread exited uncleanly");
}

/* ARGSUSED */
static void
pthread_exit(void *value)
{
	_exit(0);
}

/* ARGSUSED */
static int
pthread_create(sp_pthread_t *thread, const void *attr,
    void *(*thread_start)(void *), void *arg)
{
	pid_t pid;
	struct pam_ctxt *ctx = arg;

	sshpam_thread_status = -1;
	switch ((pid = fork())) {
	case -1:
		error("fork(): %s", strerror(errno));
		return (-1);
	case 0:
		close(ctx->pam_psock);
		ctx->pam_psock = -1;
		thread_start(arg);
		_exit(1);
	default:
		*thread = pid;
		close(ctx->pam_csock);
		ctx->pam_csock = -1;
		sshpam_oldsig = signal(SIGCHLD, sshpam_sigchld_handler);
		return (0);
	}
}

static int
pthread_cancel(sp_pthread_t thread)
{
	signal(SIGCHLD, sshpam_oldsig);
	return (kill(thread, SIGTERM));
}

/* ARGSUSED */
static int
pthread_join(sp_pthread_t thread, void **value)
{
	int status;

	if (sshpam_thread_status != -1)
		return (sshpam_thread_status);
	signal(SIGCHLD, sshpam_oldsig);
	while (waitpid(thread, &status, 0) == -1) {
		if (errno == EINTR)
			continue;
		fatal("%s: waitpid: %s", __func__, strerror(errno));
	}
	return (status);
}
#endif


static pam_handle_t *sshpam_handle = NULL;
static int sshpam_err = 0;
static int sshpam_authenticated = 0;
static int sshpam_session_open = 0;
static int sshpam_cred_established = 0;
static int sshpam_account_status = -1;
static int sshpam_maxtries_reached = 0;
static char **sshpam_env = NULL;
static Authctxt *sshpam_authctxt = NULL;
static const char *sshpam_password = NULL;

/* Some PAM implementations don't implement this */
#ifndef HAVE_PAM_GETENVLIST
static char **
pam_getenvlist(pam_handle_t *pamh)
{
	/*
	 * XXX - If necessary, we can still support envrionment passing
	 * for platforms without pam_getenvlist by searching for known
	 * env vars (e.g. KRB5CCNAME) from the PAM environment.
	 */
	 return NULL;
}
#endif

/*
 * Some platforms, notably Solaris, do not enforce password complexity
 * rules during pam_chauthtok() if the real uid of the calling process
 * is 0, on the assumption that it's being called by "passwd" run by root.
 * This wraps pam_chauthtok and sets/restore the real uid so PAM will do
 * the right thing.
 */
#ifdef SSHPAM_CHAUTHTOK_NEEDS_RUID
static int
sshpam_chauthtok_ruid(pam_handle_t *pamh, int flags)
{
	int result;

	if (sshpam_authctxt == NULL)
		fatal("PAM: sshpam_authctxt not initialized");
	if (setreuid(sshpam_authctxt->pw->pw_uid, -1) == -1)
		fatal("%s: setreuid failed: %s", __func__, strerror(errno));
	result = pam_chauthtok(pamh, flags);
	if (setreuid(0, -1) == -1)
		fatal("%s: setreuid failed: %s", __func__, strerror(errno));
	return result;
}
# define pam_chauthtok(a,b)	(sshpam_chauthtok_ruid((a), (b)))
#endif

void
sshpam_password_change_required(int reqd)
{
	extern struct sshauthopt *auth_opts;
	static int saved_port, saved_agent, saved_x11;

	debug3("%s %d", __func__, reqd);
	if (sshpam_authctxt == NULL)
		fatal("%s: PAM authctxt not initialized", __func__);
	sshpam_authctxt->force_pwchange = reqd;
	if (reqd) {
		saved_port = auth_opts->permit_port_forwarding_flag;
		saved_agent = auth_opts->permit_agent_forwarding_flag;
		saved_x11 = auth_opts->permit_x11_forwarding_flag;
		auth_opts->permit_port_forwarding_flag = 0;
		auth_opts->permit_agent_forwarding_flag = 0;
		auth_opts->permit_x11_forwarding_flag = 0;
	} else {
		if (saved_port)
			auth_opts->permit_port_forwarding_flag = saved_port;
		if (saved_agent)
			auth_opts->permit_agent_forwarding_flag = saved_agent;
		if (saved_x11)
			auth_opts->permit_x11_forwarding_flag = saved_x11;
	}
}

/* Import regular and PAM environment from subprocess */
static void
import_environments(struct sshbuf *b)
{
	char *env;
	u_int n, i, num_env;
	int r;

	debug3("PAM: %s entering", __func__);

#ifndef UNSUPPORTED_POSIX_THREADS_HACK
	/* Import variables set by do_pam_account */
	if ((r = sshbuf_get_u32(b, &n)) != 0)
		fatal("%s: buffer error: %s", __func__, ssh_err(r));
	if (n > INT_MAX)
		fatal("%s: invalid PAM account status %u", __func__, n);
	sshpam_account_status = (int)n;
	if ((r = sshbuf_get_u32(b, &n)) != 0)
		fatal("%s: buffer error: %s", __func__, ssh_err(r));
	sshpam_password_change_required(n != 0);

	/* Import environment from subprocess */
	if ((r = sshbuf_get_u32(b, &num_env)) != 0)
		fatal("%s: buffer error: %s", __func__, ssh_err(r));
	if (num_env > 1024)
		fatal("%s: received %u environment variables, expected <= 1024",
		    __func__, num_env);
	sshpam_env = xcalloc(num_env + 1, sizeof(*sshpam_env));
	debug3("PAM: num env strings %d", num_env);
	for(i = 0; i < num_env; i++) {
		if ((r = sshbuf_get_cstring(b, &(sshpam_env[i]), NULL)) != 0)
			fatal("%s: buffer error: %s", __func__, ssh_err(r));
	}
	sshpam_env[num_env] = NULL;

	/* Import PAM environment from subprocess */
	if ((r = sshbuf_get_u32(b, &num_env)) != 0)
		fatal("%s: buffer error: %s", __func__, ssh_err(r));
	debug("PAM: num PAM env strings %d", num_env);
	for (i = 0; i < num_env; i++) {
		if ((r = sshbuf_get_cstring(b, &env, NULL)) != 0)
			fatal("%s: buffer error: %s", __func__, ssh_err(r));
#ifdef HAVE_PAM_PUTENV
		/* Errors are not fatal here */
		if ((r = pam_putenv(sshpam_handle, env)) != PAM_SUCCESS) {
			error("PAM: pam_putenv: %s",
			    pam_strerror(sshpam_handle, r));
		}
#endif
		/* XXX leak env? */
	}
#endif
}

/*
 * Conversation function for authentication thread.
 */
static int
sshpam_thread_conv(int n, sshpam_const struct pam_message **msg,
    struct pam_response **resp, void *data)
{
	struct sshbuf *buffer;
	struct pam_ctxt *ctxt;
	struct pam_response *reply;
	int r, i;
	u_char status;

	debug3("PAM: %s entering, %d messages", __func__, n);
	*resp = NULL;

	if (data == NULL) {
		error("PAM: conversation function passed a null context");
		return (PAM_CONV_ERR);
	}
	ctxt = data;
	if (n <= 0 || n > PAM_MAX_NUM_MSG)
		return (PAM_CONV_ERR);

	if ((reply = calloc(n, sizeof(*reply))) == NULL)
		return PAM_CONV_ERR;
	if ((buffer = sshbuf_new()) == NULL) {
		free(reply);
		return PAM_CONV_ERR;
	}

	for (i = 0; i < n; ++i) {
		switch (PAM_MSG_MEMBER(msg, i, msg_style)) {
		case PAM_PROMPT_ECHO_OFF:
		case PAM_PROMPT_ECHO_ON:
			if ((r = sshbuf_put_cstring(buffer,
			    PAM_MSG_MEMBER(msg, i, msg))) != 0)
				fatal("%s: buffer error: %s",
				    __func__, ssh_err(r));
			if (ssh_msg_send(ctxt->pam_csock,
			    PAM_MSG_MEMBER(msg, i, msg_style), buffer) == -1)
				goto fail;

			if (ssh_msg_recv(ctxt->pam_csock, buffer) == -1)
				goto fail;
			if ((r = sshbuf_get_u8(buffer, &status)) != 0)
				fatal("%s: buffer error: %s",
				    __func__, ssh_err(r));
			if (status != PAM_AUTHTOK)
				goto fail;
			if ((r = sshbuf_get_cstring(buffer,
			    &reply[i].resp, NULL)) != 0)
				fatal("%s: buffer error: %s",
				    __func__, ssh_err(r));
			break;
		case PAM_ERROR_MSG:
		case PAM_TEXT_INFO:
			if ((r = sshbuf_put_cstring(buffer,
			    PAM_MSG_MEMBER(msg, i, msg))) != 0)
				fatal("%s: buffer error: %s",
				    __func__, ssh_err(r));
			if (ssh_msg_send(ctxt->pam_csock,
			    PAM_MSG_MEMBER(msg, i, msg_style), buffer) == -1)
				goto fail;
			break;
		default:
			goto fail;
		}
		sshbuf_reset(buffer);
	}
	sshbuf_free(buffer);
	*resp = reply;
	return (PAM_SUCCESS);

 fail:
	for(i = 0; i < n; i++) {
		free(reply[i].resp);
	}
	free(reply);
	sshbuf_free(buffer);
	return (PAM_CONV_ERR);
}

/*
 * Authentication thread.
 */
static void *
sshpam_thread(void *ctxtp)
{
	struct pam_ctxt *ctxt = ctxtp;
	struct sshbuf *buffer = NULL;
	struct pam_conv sshpam_conv;
	int r, flags = (options.permit_empty_passwd == 0 ?
	    PAM_DISALLOW_NULL_AUTHTOK : 0);
#ifndef UNSUPPORTED_POSIX_THREADS_HACK
	extern char **environ;
	char **env_from_pam;
	u_int i;
	const char *pam_user;
	const char **ptr_pam_user = &pam_user;
	char *tz = getenv("TZ");

	sshpam_err = pam_get_item(sshpam_handle, PAM_USER,
	    (sshpam_const void **)ptr_pam_user);
	if (sshpam_err != PAM_SUCCESS)
		goto auth_fail;

	environ[0] = NULL;
	if (tz != NULL)
		if (setenv("TZ", tz, 1) == -1)
			error("PAM: could not set TZ environment: %s",
			    strerror(errno));

	if (sshpam_authctxt != NULL) {
		setproctitle("%s [pam]",
		    sshpam_authctxt->valid ? pam_user : "unknown");
	}
#endif

	sshpam_conv.conv = sshpam_thread_conv;
	sshpam_conv.appdata_ptr = ctxt;

	if (sshpam_authctxt == NULL)
		fatal("%s: PAM authctxt not initialized", __func__);

	if ((buffer = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new failed", __func__);

	sshpam_err = pam_set_item(sshpam_handle, PAM_CONV,
	    (const void *)&sshpam_conv);
	if (sshpam_err != PAM_SUCCESS)
		goto auth_fail;
	sshpam_err = pam_authenticate(sshpam_handle, flags);
	if (sshpam_err == PAM_MAXTRIES)
		sshpam_set_maxtries_reached(1);
	if (sshpam_err != PAM_SUCCESS)
		goto auth_fail;

	if (!do_pam_account()) {
		sshpam_err = PAM_ACCT_EXPIRED;
		goto auth_fail;
	}
	if (sshpam_authctxt->force_pwchange) {
		sshpam_err = pam_chauthtok(sshpam_handle,
		    PAM_CHANGE_EXPIRED_AUTHTOK);
		if (sshpam_err != PAM_SUCCESS)
			goto auth_fail;
		sshpam_password_change_required(0);
	}

	if ((r = sshbuf_put_cstring(buffer, "OK")) != 0)
		fatal("%s: buffer error: %s", __func__, ssh_err(r));

#ifndef UNSUPPORTED_POSIX_THREADS_HACK
	/* Export variables set by do_pam_account */
	if ((r = sshbuf_put_u32(buffer, sshpam_account_status)) != 0 ||
	    (r = sshbuf_put_u32(buffer, sshpam_authctxt->force_pwchange)) != 0)
		fatal("%s: buffer error: %s", __func__, ssh_err(r));

	/* Export any environment strings set in child */
	for (i = 0; environ[i] != NULL; i++) {
		/* Count */
		if (i > INT_MAX)
			fatal("%s: too many enviornment strings", __func__);
	}
	if ((r = sshbuf_put_u32(buffer, i)) != 0)
		fatal("%s: buffer error: %s", __func__, ssh_err(r));
	for (i = 0; environ[i] != NULL; i++) {
		if ((r = sshbuf_put_cstring(buffer, environ[i])) != 0)
			fatal("%s: buffer error: %s", __func__, ssh_err(r));
	}
	/* Export any environment strings set by PAM in child */
	env_from_pam = pam_getenvlist(sshpam_handle);
	for (i = 0; env_from_pam != NULL && env_from_pam[i] != NULL; i++) {
		/* Count */
		if (i > INT_MAX)
			fatal("%s: too many PAM enviornment strings", __func__);
	}
	if ((r = sshbuf_put_u32(buffer, i)) != 0)
		fatal("%s: buffer error: %s", __func__, ssh_err(r));
	for (i = 0; env_from_pam != NULL && env_from_pam[i] != NULL; i++) {
		if ((r = sshbuf_put_cstring(buffer, env_from_pam[i])) != 0)
			fatal("%s: buffer error: %s", __func__, ssh_err(r));
	}
#endif /* UNSUPPORTED_POSIX_THREADS_HACK */

	/* XXX - can't do much about an error here */
	ssh_msg_send(ctxt->pam_csock, sshpam_err, buffer);
	sshbuf_free(buffer);
	pthread_exit(NULL);

 auth_fail:
	if ((r = sshbuf_put_cstring(buffer,
	    pam_strerror(sshpam_handle, sshpam_err))) != 0)
		fatal("%s: buffer error: %s", __func__, ssh_err(r));
	/* XXX - can't do much about an error here */
	if (sshpam_err == PAM_ACCT_EXPIRED)
		ssh_msg_send(ctxt->pam_csock, PAM_ACCT_EXPIRED, buffer);
	else if (sshpam_maxtries_reached)
		ssh_msg_send(ctxt->pam_csock, PAM_MAXTRIES, buffer);
	else
		ssh_msg_send(ctxt->pam_csock, PAM_AUTH_ERR, buffer);
	sshbuf_free(buffer);
	pthread_exit(NULL);

	return (NULL); /* Avoid warning for non-pthread case */
}

void
sshpam_thread_cleanup(void)
{
	struct pam_ctxt *ctxt = cleanup_ctxt;

	debug3("PAM: %s entering", __func__);
	if (ctxt != NULL && ctxt->pam_thread != 0) {
		pthread_cancel(ctxt->pam_thread);
		pthread_join(ctxt->pam_thread, NULL);
		close(ctxt->pam_psock);
		close(ctxt->pam_csock);
		memset(ctxt, 0, sizeof(*ctxt));
		cleanup_ctxt = NULL;
	}
}

static int
sshpam_null_conv(int n, sshpam_const struct pam_message **msg,
    struct pam_response **resp, void *data)
{
	debug3("PAM: %s entering, %d messages", __func__, n);
	return (PAM_CONV_ERR);
}

static struct pam_conv null_conv = { sshpam_null_conv, NULL };

static int
sshpam_store_conv(int n, sshpam_const struct pam_message **msg,
    struct pam_response **resp, void *data)
{
	struct pam_response *reply;
	int r, i;

	debug3("PAM: %s called with %d messages", __func__, n);
	*resp = NULL;

	if (n <= 0 || n > PAM_MAX_NUM_MSG)
		return (PAM_CONV_ERR);

	if ((reply = calloc(n, sizeof(*reply))) == NULL)
		return (PAM_CONV_ERR);

	for (i = 0; i < n; ++i) {
		switch (PAM_MSG_MEMBER(msg, i, msg_style)) {
		case PAM_ERROR_MSG:
		case PAM_TEXT_INFO:
			if ((r = sshbuf_putf(loginmsg, "%s\n",
			    PAM_MSG_MEMBER(msg, i, msg))) != 0)
				fatal("%s: buffer error: %s",
				    __func__, ssh_err(r));
			reply[i].resp_retcode = PAM_SUCCESS;
			break;
		default:
			goto fail;
		}
	}
	*resp = reply;
	return (PAM_SUCCESS);

 fail:
	for(i = 0; i < n; i++) {
		free(reply[i].resp);
	}
	free(reply);
	return (PAM_CONV_ERR);
}

static struct pam_conv store_conv = { sshpam_store_conv, NULL };

void
sshpam_cleanup(void)
{
	if (sshpam_handle == NULL || (use_privsep && !mm_is_monitor()))
		return;
	debug("PAM: cleanup");
	pam_set_item(sshpam_handle, PAM_CONV, (const void *)&null_conv);
	if (sshpam_session_open) {
		debug("PAM: closing session");
		pam_close_session(sshpam_handle, PAM_SILENT);
		sshpam_session_open = 0;
	}
	if (sshpam_cred_established) {
		debug("PAM: deleting credentials");
		pam_setcred(sshpam_handle, PAM_DELETE_CRED);
		sshpam_cred_established = 0;
	}
	sshpam_authenticated = 0;
	pam_end(sshpam_handle, sshpam_err);
	sshpam_handle = NULL;
}

static int
sshpam_init(Authctxt *authctxt)
{
	const char *pam_rhost, *pam_user, *user = authctxt->user;
	const char **ptr_pam_user = &pam_user;
	struct ssh *ssh = active_state; /* XXX */

	if (sshpam_handle != NULL) {
		/* We already have a PAM context; check if the user matches */
		sshpam_err = pam_get_item(sshpam_handle,
		    PAM_USER, (sshpam_const void **)ptr_pam_user);
		if (sshpam_err == PAM_SUCCESS && strcmp(user, pam_user) == 0)
			return (0);
		pam_end(sshpam_handle, sshpam_err);
		sshpam_handle = NULL;
	}
	debug("PAM: initializing for \"%s\"", user);
	sshpam_err =
	    pam_start(SSHD_PAM_SERVICE, user, &store_conv, &sshpam_handle);
	sshpam_authctxt = authctxt;

	if (sshpam_err != PAM_SUCCESS) {
		pam_end(sshpam_handle, sshpam_err);
		sshpam_handle = NULL;
		return (-1);
	}
	pam_rhost = auth_get_canonical_hostname(ssh, options.use_dns);
	debug("PAM: setting PAM_RHOST to \"%s\"", pam_rhost);
	sshpam_err = pam_set_item(sshpam_handle, PAM_RHOST, pam_rhost);
	if (sshpam_err != PAM_SUCCESS) {
		pam_end(sshpam_handle, sshpam_err);
		sshpam_handle = NULL;
		return (-1);
	}
#ifdef PAM_TTY_KLUDGE
	/*
	 * Some silly PAM modules (e.g. pam_time) require a TTY to operate.
	 * sshd doesn't set the tty until too late in the auth process and
	 * may not even set one (for tty-less connections)
	 */
	debug("PAM: setting PAM_TTY to \"ssh\"");
	sshpam_err = pam_set_item(sshpam_handle, PAM_TTY, "ssh");
	if (sshpam_err != PAM_SUCCESS) {
		pam_end(sshpam_handle, sshpam_err);
		sshpam_handle = NULL;
		return (-1);
	}
#endif
	return (0);
}

static void
expose_authinfo(const char *caller)
{
	char *auth_info;

	/*
	 * Expose authentication information to PAM.
	 * The environment variable is versioned. Please increment the
	 * version suffix if the format of session_info changes.
	 */
	if (sshpam_authctxt->session_info == NULL)
		auth_info = xstrdup("");
	else if ((auth_info = sshbuf_dup_string(
	    sshpam_authctxt->session_info)) == NULL)
		fatal("%s: sshbuf_dup_string failed", __func__);

	debug2("%s: auth information in SSH_AUTH_INFO_0", caller);
	do_pam_putenv("SSH_AUTH_INFO_0", auth_info);
	free(auth_info);
}

static void *
sshpam_init_ctx(Authctxt *authctxt)
{
	struct pam_ctxt *ctxt;
	int socks[2];

	debug3("PAM: %s entering", __func__);
	/*
	 * Refuse to start if we don't have PAM enabled or do_pam_account
	 * has previously failed.
	 */
	if (!options.use_pam || sshpam_account_status == 0)
		return NULL;

	/* Initialize PAM */
	if (sshpam_init(authctxt) == -1) {
		error("PAM: initialization failed");
		return (NULL);
	}

	expose_authinfo(__func__);
	ctxt = xcalloc(1, sizeof *ctxt);

	/* Start the authentication thread */
	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, socks) == -1) {
		error("PAM: failed create sockets: %s", strerror(errno));
		free(ctxt);
		return (NULL);
	}
	ctxt->pam_psock = socks[0];
	ctxt->pam_csock = socks[1];
	if (pthread_create(&ctxt->pam_thread, NULL, sshpam_thread, ctxt) == -1) {
		error("PAM: failed to start authentication thread: %s",
		    strerror(errno));
		close(socks[0]);
		close(socks[1]);
		free(ctxt);
		return (NULL);
	}
	cleanup_ctxt = ctxt;
	return (ctxt);
}

static int
sshpam_query(void *ctx, char **name, char **info,
    u_int *num, char ***prompts, u_int **echo_on)
{
	struct ssh *ssh = active_state; /* XXX */
	struct sshbuf *buffer;
	struct pam_ctxt *ctxt = ctx;
	size_t plen;
	u_char type;
	char *msg;
	size_t len, mlen;
	int r;

	debug3("PAM: %s entering", __func__);
	if ((buffer = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new failed", __func__);
	*name = xstrdup("");
	*info = xstrdup("");
	*prompts = xmalloc(sizeof(char *));
	**prompts = NULL;
	plen = 0;
	*echo_on = xmalloc(sizeof(u_int));
	while (ssh_msg_recv(ctxt->pam_psock, buffer) == 0) {
		if ((r = sshbuf_get_u8(buffer, &type)) != 0 ||
		    (r = sshbuf_get_cstring(buffer, &msg, &mlen)) != 0)
			fatal("%s: buffer error: %s", __func__, ssh_err(r));
		switch (type) {
		case PAM_PROMPT_ECHO_ON:
		case PAM_PROMPT_ECHO_OFF:
			*num = 1;
			len = plen + mlen + 1;
			**prompts = xreallocarray(**prompts, 1, len);
			strlcpy(**prompts + plen, msg, len - plen);
			plen += mlen;
			**echo_on = (type == PAM_PROMPT_ECHO_ON);
			free(msg);
			return (0);
		case PAM_ERROR_MSG:
		case PAM_TEXT_INFO:
			/* accumulate messages */
			len = plen + mlen + 2;
			**prompts = xreallocarray(**prompts, 1, len);
			strlcpy(**prompts + plen, msg, len - plen);
			plen += mlen;
			strlcat(**prompts + plen, "\n", len - plen);
			plen++;
			free(msg);
			break;
		case PAM_ACCT_EXPIRED:
		case PAM_MAXTRIES:
			if (type == PAM_ACCT_EXPIRED)
				sshpam_account_status = 0;
			if (type == PAM_MAXTRIES)
				sshpam_set_maxtries_reached(1);
			/* FALLTHROUGH */
		case PAM_AUTH_ERR:
			debug3("PAM: %s", pam_strerror(sshpam_handle, type));
			if (**prompts != NULL && strlen(**prompts) != 0) {
				*info = **prompts;
				**prompts = NULL;
				*num = 0;
				**echo_on = 0;
				ctxt->pam_done = -1;
				free(msg);
				return 0;
			}
			/* FALLTHROUGH */
		case PAM_SUCCESS:
			if (**prompts != NULL) {
				/* drain any accumulated messages */
				debug("PAM: %s", **prompts);
				if ((r = sshbuf_put(loginmsg, **prompts,
				    strlen(**prompts))) != 0)
					fatal("%s: buffer error: %s",
					    __func__, ssh_err(r));
				free(**prompts);
				**prompts = NULL;
			}
			if (type == PAM_SUCCESS) {
				if (!sshpam_authctxt->valid ||
				    (sshpam_authctxt->pw->pw_uid == 0 &&
				    options.permit_root_login != PERMIT_YES))
					fatal("Internal error: PAM auth "
					    "succeeded when it should have "
					    "failed");
				import_environments(buffer);
				*num = 0;
				**echo_on = 0;
				ctxt->pam_done = 1;
				free(msg);
				return (0);
			}
			BLACKLIST_NOTIFY(BLACKLIST_BAD_USER,
			    sshpam_authctxt->user);
			error("PAM: %s for %s%.100s from %.100s", msg,
			    sshpam_authctxt->valid ? "" : "illegal user ",
			    sshpam_authctxt->user,
			    auth_get_canonical_hostname(ssh, options.use_dns));
			/* FALLTHROUGH */
		default:
			*num = 0;
			**echo_on = 0;
			free(msg);
			ctxt->pam_done = -1;
			return (-1);
		}
	}
	return (-1);
}

/*
 * Returns a junk password of identical length to that the user supplied.
 * Used to mitigate timing attacks against crypt(3)/PAM stacks that
 * vary processing time in proportion to password length.
 */
static char *
fake_password(const char *wire_password)
{
	const char junk[] = "\b\n\r\177INCORRECT";
	char *ret = NULL;
	size_t i, l = wire_password != NULL ? strlen(wire_password) : 0;

	if (l >= INT_MAX)
		fatal("%s: password length too long: %zu", __func__, l);

	ret = malloc(l + 1);
	if (ret == NULL)
		return NULL;
	for (i = 0; i < l; i++)
		ret[i] = junk[i % (sizeof(junk) - 1)];
	ret[i] = '\0';
	return ret;
}

/* XXX - see also comment in auth-chall.c:verify_response */
static int
sshpam_respond(void *ctx, u_int num, char **resp)
{
	struct sshbuf *buffer;
	struct pam_ctxt *ctxt = ctx;
	char *fake;
	int r;

	debug2("PAM: %s entering, %u responses", __func__, num);
	switch (ctxt->pam_done) {
	case 1:
		sshpam_authenticated = 1;
		return (0);
	case 0:
		break;
	default:
		return (-1);
	}
	if (num != 1) {
		error("PAM: expected one response, got %u", num);
		return (-1);
	}
	if ((buffer = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new failed", __func__);
	if (sshpam_authctxt->valid &&
	    (sshpam_authctxt->pw->pw_uid != 0 ||
	    options.permit_root_login == PERMIT_YES)) {
		if ((r = sshbuf_put_cstring(buffer, *resp)) != 0)
			fatal("%s: buffer error: %s", __func__, ssh_err(r));
	} else {
		fake = fake_password(*resp);
		if ((r = sshbuf_put_cstring(buffer, fake)) != 0)
			fatal("%s: buffer error: %s", __func__, ssh_err(r));
		free(fake);
	}
	if (ssh_msg_send(ctxt->pam_psock, PAM_AUTHTOK, buffer) == -1) {
		sshbuf_free(buffer);
		return (-1);
	}
	sshbuf_free(buffer);
	return (1);
}

static void
sshpam_free_ctx(void *ctxtp)
{
	struct pam_ctxt *ctxt = ctxtp;

	debug3("PAM: %s entering", __func__);
	sshpam_thread_cleanup();
	free(ctxt);
	/*
	 * We don't call sshpam_cleanup() here because we may need the PAM
	 * handle at a later stage, e.g. when setting up a session.  It's
	 * still on the cleanup list, so pam_end() *will* be called before
	 * the server process terminates.
	 */
}

KbdintDevice sshpam_device = {
	"pam",
	sshpam_init_ctx,
	sshpam_query,
	sshpam_respond,
	sshpam_free_ctx
};

KbdintDevice mm_sshpam_device = {
	"pam",
	mm_sshpam_init_ctx,
	mm_sshpam_query,
	mm_sshpam_respond,
	mm_sshpam_free_ctx
};

/*
 * This replaces auth-pam.c
 */
void
start_pam(Authctxt *authctxt)
{
	if (!options.use_pam)
		fatal("PAM: initialisation requested when UsePAM=no");

	if (sshpam_init(authctxt) == -1)
		fatal("PAM: initialisation failed");
}

void
finish_pam(void)
{
	sshpam_cleanup();
}


u_int
do_pam_account(void)
{
	debug("%s: called", __func__);
	if (sshpam_account_status != -1)
		return (sshpam_account_status);

	expose_authinfo(__func__);

	sshpam_err = pam_acct_mgmt(sshpam_handle, 0);
	debug3("PAM: %s pam_acct_mgmt = %d (%s)", __func__, sshpam_err,
	    pam_strerror(sshpam_handle, sshpam_err));

	if (sshpam_err != PAM_SUCCESS && sshpam_err != PAM_NEW_AUTHTOK_REQD) {
		sshpam_account_status = 0;
		return (sshpam_account_status);
	}

	if (sshpam_err == PAM_NEW_AUTHTOK_REQD)
		sshpam_password_change_required(1);

	sshpam_account_status = 1;
	return (sshpam_account_status);
}

void
do_pam_setcred(int init)
{
	sshpam_err = pam_set_item(sshpam_handle, PAM_CONV,
	    (const void *)&store_conv);
	if (sshpam_err != PAM_SUCCESS)
		fatal("PAM: failed to set PAM_CONV: %s",
		    pam_strerror(sshpam_handle, sshpam_err));
	if (init) {
		debug("PAM: establishing credentials");
		sshpam_err = pam_setcred(sshpam_handle, PAM_ESTABLISH_CRED);
	} else {
		debug("PAM: reinitializing credentials");
		sshpam_err = pam_setcred(sshpam_handle, PAM_REINITIALIZE_CRED);
	}
	if (sshpam_err == PAM_SUCCESS) {
		sshpam_cred_established = 1;
		return;
	}
	if (sshpam_authenticated)
		fatal("PAM: pam_setcred(): %s",
		    pam_strerror(sshpam_handle, sshpam_err));
	else
		debug("PAM: pam_setcred(): %s",
		    pam_strerror(sshpam_handle, sshpam_err));
}

static int
sshpam_tty_conv(int n, sshpam_const struct pam_message **msg,
    struct pam_response **resp, void *data)
{
	char input[PAM_MAX_MSG_SIZE];
	struct pam_response *reply;
	int i;

	debug3("PAM: %s called with %d messages", __func__, n);

	*resp = NULL;

	if (n <= 0 || n > PAM_MAX_NUM_MSG || !isatty(STDIN_FILENO))
		return (PAM_CONV_ERR);

	if ((reply = calloc(n, sizeof(*reply))) == NULL)
		return (PAM_CONV_ERR);

	for (i = 0; i < n; ++i) {
		switch (PAM_MSG_MEMBER(msg, i, msg_style)) {
		case PAM_PROMPT_ECHO_OFF:
			reply[i].resp =
			    read_passphrase(PAM_MSG_MEMBER(msg, i, msg),
			    RP_ALLOW_STDIN);
			reply[i].resp_retcode = PAM_SUCCESS;
			break;
		case PAM_PROMPT_ECHO_ON:
			fprintf(stderr, "%s\n", PAM_MSG_MEMBER(msg, i, msg));
			if (fgets(input, sizeof input, stdin) == NULL)
				input[0] = '\0';
			if ((reply[i].resp = strdup(input)) == NULL)
				goto fail;
			reply[i].resp_retcode = PAM_SUCCESS;
			break;
		case PAM_ERROR_MSG:
		case PAM_TEXT_INFO:
			fprintf(stderr, "%s\n", PAM_MSG_MEMBER(msg, i, msg));
			reply[i].resp_retcode = PAM_SUCCESS;
			break;
		default:
			goto fail;
		}
	}
	*resp = reply;
	return (PAM_SUCCESS);

 fail:
	for(i = 0; i < n; i++) {
		free(reply[i].resp);
	}
	free(reply);
	return (PAM_CONV_ERR);
}

static struct pam_conv tty_conv = { sshpam_tty_conv, NULL };

/*
 * XXX this should be done in the authentication phase, but ssh1 doesn't
 * support that
 */
void
do_pam_chauthtok(void)
{
	if (use_privsep)
		fatal("Password expired (unable to change with privsep)");
	sshpam_err = pam_set_item(sshpam_handle, PAM_CONV,
	    (const void *)&tty_conv);
	if (sshpam_err != PAM_SUCCESS)
		fatal("PAM: failed to set PAM_CONV: %s",
		    pam_strerror(sshpam_handle, sshpam_err));
	debug("PAM: changing password");
	sshpam_err = pam_chauthtok(sshpam_handle, PAM_CHANGE_EXPIRED_AUTHTOK);
	if (sshpam_err != PAM_SUCCESS)
		fatal("PAM: pam_chauthtok(): %s",
		    pam_strerror(sshpam_handle, sshpam_err));
}

void
do_pam_session(struct ssh *ssh)
{
	debug3("PAM: opening session");

	expose_authinfo(__func__);

	sshpam_err = pam_set_item(sshpam_handle, PAM_CONV,
	    (const void *)&store_conv);
	if (sshpam_err != PAM_SUCCESS)
		fatal("PAM: failed to set PAM_CONV: %s",
		    pam_strerror(sshpam_handle, sshpam_err));
	sshpam_err = pam_open_session(sshpam_handle, 0);
	if (sshpam_err == PAM_SUCCESS)
		sshpam_session_open = 1;
	else {
		sshpam_session_open = 0;
		auth_restrict_session(ssh);
		error("PAM: pam_open_session(): %s",
		    pam_strerror(sshpam_handle, sshpam_err));
	}

}

int
is_pam_session_open(void)
{
	return sshpam_session_open;
}

/*
 * Set a PAM environment string. We need to do this so that the session
 * modules can handle things like Kerberos/GSI credentials that appear
 * during the ssh authentication process.
 */
int
do_pam_putenv(char *name, char *value)
{
	int ret = 1;
#ifdef HAVE_PAM_PUTENV
	char *compound;
	size_t len;

	len = strlen(name) + strlen(value) + 2;
	compound = xmalloc(len);

	snprintf(compound, len, "%s=%s", name, value);
	ret = pam_putenv(sshpam_handle, compound);
	free(compound);
#endif

	return (ret);
}

char **
fetch_pam_child_environment(void)
{
	return sshpam_env;
}

char **
fetch_pam_environment(void)
{
	return (pam_getenvlist(sshpam_handle));
}

void
free_pam_environment(char **env)
{
	char **envp;

	if (env == NULL)
		return;

	for (envp = env; *envp; envp++)
		free(*envp);
	free(env);
}

/*
 * "Blind" conversation function for password authentication.  Assumes that
 * echo-off prompts are for the password and stores messages for later
 * display.
 */
static int
sshpam_passwd_conv(int n, sshpam_const struct pam_message **msg,
    struct pam_response **resp, void *data)
{
	struct pam_response *reply;
	int r, i;
	size_t len;

	debug3("PAM: %s called with %d messages", __func__, n);

	*resp = NULL;

	if (n <= 0 || n > PAM_MAX_NUM_MSG)
		return (PAM_CONV_ERR);

	if ((reply = calloc(n, sizeof(*reply))) == NULL)
		return (PAM_CONV_ERR);

	for (i = 0; i < n; ++i) {
		switch (PAM_MSG_MEMBER(msg, i, msg_style)) {
		case PAM_PROMPT_ECHO_OFF:
			if (sshpam_password == NULL)
				goto fail;
			if ((reply[i].resp = strdup(sshpam_password)) == NULL)
				goto fail;
			reply[i].resp_retcode = PAM_SUCCESS;
			break;
		case PAM_ERROR_MSG:
		case PAM_TEXT_INFO:
			len = strlen(PAM_MSG_MEMBER(msg, i, msg));
			if (len > 0) {
				if ((r = sshbuf_putf(loginmsg, "%s\n",
				    PAM_MSG_MEMBER(msg, i, msg))) != 0)
					fatal("%s: buffer error: %s",
					    __func__, ssh_err(r));
			}
			if ((reply[i].resp = strdup("")) == NULL)
				goto fail;
			reply[i].resp_retcode = PAM_SUCCESS;
			break;
		default:
			goto fail;
		}
	}
	*resp = reply;
	return (PAM_SUCCESS);

 fail:
	for(i = 0; i < n; i++) {
		free(reply[i].resp);
	}
	free(reply);
	return (PAM_CONV_ERR);
}

static struct pam_conv passwd_conv = { sshpam_passwd_conv, NULL };

/*
 * Attempt password authentication via PAM
 */
int
sshpam_auth_passwd(Authctxt *authctxt, const char *password)
{
	int flags = (options.permit_empty_passwd == 0 ?
	    PAM_DISALLOW_NULL_AUTHTOK : 0);
	char *fake = NULL;

	if (!options.use_pam || sshpam_handle == NULL)
		fatal("PAM: %s called when PAM disabled or failed to "
		    "initialise.", __func__);

	sshpam_password = password;
	sshpam_authctxt = authctxt;

	/*
	 * If the user logging in is invalid, or is root but is not permitted
	 * by PermitRootLogin, use an invalid password to prevent leaking
	 * information via timing (eg if the PAM config has a delay on fail).
	 */
	if (!authctxt->valid || (authctxt->pw->pw_uid == 0 &&
	    options.permit_root_login != PERMIT_YES))
		sshpam_password = fake = fake_password(password);

	sshpam_err = pam_set_item(sshpam_handle, PAM_CONV,
	    (const void *)&passwd_conv);
	if (sshpam_err != PAM_SUCCESS)
		fatal("PAM: %s: failed to set PAM_CONV: %s", __func__,
		    pam_strerror(sshpam_handle, sshpam_err));

	sshpam_err = pam_authenticate(sshpam_handle, flags);
	sshpam_password = NULL;
	free(fake);
	if (sshpam_err == PAM_MAXTRIES)
		sshpam_set_maxtries_reached(1);
	if (sshpam_err == PAM_SUCCESS && authctxt->valid) {
		debug("PAM: password authentication accepted for %.100s",
		    authctxt->user);
		return 1;
	} else {
		debug("PAM: password authentication failed for %.100s: %s",
		    authctxt->valid ? authctxt->user : "an illegal user",
		    pam_strerror(sshpam_handle, sshpam_err));
		return 0;
	}
}

int
sshpam_get_maxtries_reached(void)
{
	return sshpam_maxtries_reached;
}

void
sshpam_set_maxtries_reached(int reached)
{
	if (reached == 0 || sshpam_maxtries_reached)
		return;
	sshpam_maxtries_reached = 1;
	options.password_authentication = 0;
	options.kbd_interactive_authentication = 0;
	options.challenge_response_authentication = 0;
}
#endif /* USE_PAM */
