/* $OpenBSD: auth.h,v 1.96 2018/04/10 00:10:49 djm Exp $ */

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
 *
 */

#ifndef AUTH_H
#define AUTH_H

#include <signal.h>

#include <openssl/rsa.h>

#ifdef HAVE_LOGIN_CAP
#include <login_cap.h>
#endif
#ifdef BSD_AUTH
#include <bsd_auth.h>
#endif
#ifdef KRB5
#include <krb5.h>
#endif

struct passwd;
struct ssh;
struct sshbuf;
struct sshkey;
struct sshauthopt;

typedef struct Authctxt Authctxt;
typedef struct Authmethod Authmethod;
typedef struct KbdintDevice KbdintDevice;

struct Authctxt {
	sig_atomic_t	 success;
	int		 authenticated;	/* authenticated and alarms cancelled */
	int		 postponed;	/* authentication needs another step */
	int		 valid;		/* user exists and is allowed to login */
	int		 attempt;
	int		 failures;
	int		 server_caused_failure;
	int		 force_pwchange;
	char		*user;		/* username sent by the client */
	char		*service;
	struct passwd	*pw;		/* set if 'valid' */
	char		*style;

	/* Method lists for multiple authentication */
	char		**auth_methods;	/* modified from server config */
	u_int		 num_auth_methods;

	/* Authentication method-specific data */
	void		*methoddata;
	void		*kbdintctxt;
#ifdef BSD_AUTH
	auth_session_t	*as;
#endif
#ifdef KRB5
	krb5_context	 krb5_ctx;
	krb5_ccache	 krb5_fwd_ccache;
	krb5_principal	 krb5_user;
	char		*krb5_ticket_file;
	char		*krb5_ccname;
#endif
	struct sshbuf	*loginmsg;

	/* Authentication keys already used; these will be refused henceforth */
	struct sshkey	**prev_keys;
	u_int		 nprev_keys;

	/* Last used key and ancillary information from active auth method */
	struct sshkey	*auth_method_key;
	char		*auth_method_info;

	/* Information exposed to session */
	struct sshbuf	*session_info;	/* Auth info for environment */
};

/*
 * Every authentication method has to handle authentication requests for
 * non-existing users, or for users that are not allowed to login. In this
 * case 'valid' is set to 0, but 'user' points to the username requested by
 * the client.
 */

struct Authmethod {
	char	*name;
	int	(*userauth)(struct ssh *);
	int	*enabled;
};

/*
 * Keyboard interactive device:
 * init_ctx	returns: non NULL upon success
 * query	returns: 0 - success, otherwise failure
 * respond	returns: 0 - success, 1 - need further interaction,
 *		otherwise - failure
 */
struct KbdintDevice
{
	const char *name;
	void*	(*init_ctx)(Authctxt*);
	int	(*query)(void *ctx, char **name, char **infotxt,
		    u_int *numprompts, char ***prompts, u_int **echo_on);
	int	(*respond)(void *ctx, u_int numresp, char **responses);
	void	(*free_ctx)(void *ctx);
};

int
auth_rhosts2(struct passwd *, const char *, const char *, const char *);

int      auth_password(struct ssh *, const char *);

int	 hostbased_key_allowed(struct passwd *, const char *, char *,
	    struct sshkey *);
int	 user_key_allowed(struct ssh *, struct passwd *, struct sshkey *, int,
    struct sshauthopt **);
int	 auth2_key_already_used(Authctxt *, const struct sshkey *);

/*
 * Handling auth method-specific information for logging and prevention
 * of key reuse during multiple authentication.
 */
void	 auth2_authctxt_reset_info(Authctxt *);
void	 auth2_record_key(Authctxt *, int, const struct sshkey *);
void	 auth2_record_info(Authctxt *authctxt, const char *, ...)
	    __attribute__((__format__ (printf, 2, 3)))
	    __attribute__((__nonnull__ (2)));
void	 auth2_update_session_info(Authctxt *, const char *, const char *);

#ifdef KRB5
int	auth_krb5(Authctxt *authctxt, krb5_data *auth, char **client, krb5_data *);
int	auth_krb5_tgt(Authctxt *authctxt, krb5_data *tgt);
int	auth_krb5_password(Authctxt *authctxt, const char *password);
void	krb5_cleanup_proc(Authctxt *authctxt);
#endif /* KRB5 */

#if defined(USE_SHADOW) && defined(HAS_SHADOW_EXPIRE)
#include <shadow.h>
int auth_shadow_acctexpired(struct spwd *);
int auth_shadow_pwexpired(Authctxt *);
#endif

#include "auth-pam.h"
#include "audit.h"
void remove_kbdint_device(const char *);

void	do_authentication2(Authctxt *);

void	auth_log(Authctxt *, int, int, const char *, const char *);
void	auth_maxtries_exceeded(Authctxt *) __attribute__((noreturn));
void	userauth_finish(struct ssh *, int, const char *, const char *);
int	auth_root_allowed(struct ssh *, const char *);

void	userauth_send_banner(const char *);

char	*auth2_read_banner(void);
int	 auth2_methods_valid(const char *, int);
int	 auth2_update_methods_lists(Authctxt *, const char *, const char *);
int	 auth2_setup_methods_lists(Authctxt *);
int	 auth2_method_allowed(Authctxt *, const char *, const char *);

void	privsep_challenge_enable(void);

int	auth2_challenge(struct ssh *, char *);
void	auth2_challenge_stop(struct ssh *);
int	bsdauth_query(void *, char **, char **, u_int *, char ***, u_int **);
int	bsdauth_respond(void *, u_int, char **);

int	allowed_user(struct passwd *);
struct passwd * getpwnamallow(const char *user);

char	*expand_authorized_keys(const char *, struct passwd *pw);
char	*authorized_principals_file(struct passwd *);

FILE	*auth_openkeyfile(const char *, struct passwd *, int);
FILE	*auth_openprincipals(const char *, struct passwd *, int);
int	 auth_key_is_revoked(struct sshkey *);

const char	*auth_get_canonical_hostname(struct ssh *, int);

HostStatus
check_key_in_hostfiles(struct passwd *, struct sshkey *, const char *,
    const char *, const char *);

/* hostkey handling */
struct sshkey	*get_hostkey_by_index(int);
struct sshkey	*get_hostkey_public_by_index(int, struct ssh *);
struct sshkey	*get_hostkey_public_by_type(int, int, struct ssh *);
struct sshkey	*get_hostkey_private_by_type(int, int, struct ssh *);
int	 get_hostkey_index(struct sshkey *, int, struct ssh *);
int	 sshd_hostkey_sign(struct sshkey *, struct sshkey *, u_char **,
	     size_t *, const u_char *, size_t, const char *, u_int);

/* Key / cert options linkage to auth layer */
const struct sshauthopt *auth_options(struct ssh *);
int	 auth_activate_options(struct ssh *, struct sshauthopt *);
void	 auth_restrict_session(struct ssh *);
int	 auth_authorise_keyopts(struct ssh *, struct passwd *pw,
    struct sshauthopt *, int, const char *);
void	 auth_log_authopts(const char *, const struct sshauthopt *, int);

/* debug messages during authentication */
void	 auth_debug_add(const char *fmt,...)
    __attribute__((format(printf, 1, 2)));
void	 auth_debug_send(void);
void	 auth_debug_reset(void);

struct passwd *fakepw(void);

#define	SSH_SUBPROCESS_STDOUT_DISCARD  (1)     /* Discard stdout */
#define	SSH_SUBPROCESS_STDOUT_CAPTURE  (1<<1)  /* Redirect stdout */
#define	SSH_SUBPROCESS_STDERR_DISCARD  (1<<2)  /* Discard stderr */
pid_t	subprocess(const char *, struct passwd *,
    const char *, int, char **, FILE **, u_int flags);

int	 sys_auth_passwd(struct ssh *, const char *);

#if defined(KRB5) && !defined(HEIMDAL)
#include <krb5.h>
krb5_error_code ssh_krb5_cc_gen(krb5_context, krb5_ccache *);
#endif
#endif
