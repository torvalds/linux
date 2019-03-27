/* $OpenBSD: sshconnect2.c,v 1.284 2018/08/13 02:41:05 djm Exp $ */
/*
 * Copyright (c) 2000 Markus Friedl.  All rights reserved.
 * Copyright (c) 2008 Damien Miller.  All rights reserved.
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#if defined(HAVE_STRNVIS) && defined(HAVE_VIS_H) && !defined(BROKEN_STRNVIS)
#include <vis.h>
#endif

#include "openbsd-compat/sys-queue.h"

#include "xmalloc.h"
#include "ssh.h"
#include "ssh2.h"
#include "sshbuf.h"
#include "packet.h"
#include "compat.h"
#include "cipher.h"
#include "sshkey.h"
#include "kex.h"
#include "myproposal.h"
#include "sshconnect.h"
#include "authfile.h"
#include "dh.h"
#include "authfd.h"
#include "log.h"
#include "misc.h"
#include "readconf.h"
#include "match.h"
#include "dispatch.h"
#include "canohost.h"
#include "msg.h"
#include "pathnames.h"
#include "uidswap.h"
#include "hostfile.h"
#include "ssherr.h"
#include "utf8.h"

#ifdef GSSAPI
#include "ssh-gss.h"
#endif

/* import */
extern char *client_version_string;
extern char *server_version_string;
extern Options options;

/*
 * SSH2 key exchange
 */

u_char *session_id2 = NULL;
u_int session_id2_len = 0;

char *xxx_host;
struct sockaddr *xxx_hostaddr;

static int
verify_host_key_callback(struct sshkey *hostkey, struct ssh *ssh)
{
	if (verify_host_key(xxx_host, xxx_hostaddr, hostkey) == -1)
		fatal("Host key verification failed.");
	return 0;
}

static char *
order_hostkeyalgs(char *host, struct sockaddr *hostaddr, u_short port)
{
	char *oavail, *avail, *first, *last, *alg, *hostname, *ret;
	size_t maxlen;
	struct hostkeys *hostkeys;
	int ktype;
	u_int i;

	/* Find all hostkeys for this hostname */
	get_hostfile_hostname_ipaddr(host, hostaddr, port, &hostname, NULL);
	hostkeys = init_hostkeys();
	for (i = 0; i < options.num_user_hostfiles; i++)
		load_hostkeys(hostkeys, hostname, options.user_hostfiles[i]);
	for (i = 0; i < options.num_system_hostfiles; i++)
		load_hostkeys(hostkeys, hostname, options.system_hostfiles[i]);

	oavail = avail = xstrdup(KEX_DEFAULT_PK_ALG);
	maxlen = strlen(avail) + 1;
	first = xmalloc(maxlen);
	last = xmalloc(maxlen);
	*first = *last = '\0';

#define ALG_APPEND(to, from) \
	do { \
		if (*to != '\0') \
			strlcat(to, ",", maxlen); \
		strlcat(to, from, maxlen); \
	} while (0)

	while ((alg = strsep(&avail, ",")) && *alg != '\0') {
		if ((ktype = sshkey_type_from_name(alg)) == KEY_UNSPEC)
			fatal("%s: unknown alg %s", __func__, alg);
		if (lookup_key_in_hostkeys_by_type(hostkeys,
		    sshkey_type_plain(ktype), NULL))
			ALG_APPEND(first, alg);
		else
			ALG_APPEND(last, alg);
	}
#undef ALG_APPEND
	xasprintf(&ret, "%s%s%s", first,
	    (*first == '\0' || *last == '\0') ? "" : ",", last);
	if (*first != '\0')
		debug3("%s: prefer hostkeyalgs: %s", __func__, first);

	free(first);
	free(last);
	free(hostname);
	free(oavail);
	free_hostkeys(hostkeys);

	return ret;
}

void
ssh_kex2(char *host, struct sockaddr *hostaddr, u_short port)
{
	char *myproposal[PROPOSAL_MAX] = { KEX_CLIENT };
	char *s, *all_key;
	struct kex *kex;
	int r;

	xxx_host = host;
	xxx_hostaddr = hostaddr;

	if ((s = kex_names_cat(options.kex_algorithms, "ext-info-c")) == NULL)
		fatal("%s: kex_names_cat", __func__);
	myproposal[PROPOSAL_KEX_ALGS] = compat_kex_proposal(s);
	myproposal[PROPOSAL_ENC_ALGS_CTOS] =
	    compat_cipher_proposal(options.ciphers);
	myproposal[PROPOSAL_ENC_ALGS_STOC] =
	    compat_cipher_proposal(options.ciphers);
	myproposal[PROPOSAL_COMP_ALGS_CTOS] =
	    myproposal[PROPOSAL_COMP_ALGS_STOC] = options.compression ?
	    "zlib@openssh.com,zlib,none" : "none,zlib@openssh.com,zlib";
	myproposal[PROPOSAL_MAC_ALGS_CTOS] =
	    myproposal[PROPOSAL_MAC_ALGS_STOC] = options.macs;
	if (options.hostkeyalgorithms != NULL) {
		all_key = sshkey_alg_list(0, 0, 1, ',');
		if (kex_assemble_names(&options.hostkeyalgorithms,
		    KEX_DEFAULT_PK_ALG, all_key) != 0)
			fatal("%s: kex_assemble_namelist", __func__);
		free(all_key);
		myproposal[PROPOSAL_SERVER_HOST_KEY_ALGS] =
		    compat_pkalg_proposal(options.hostkeyalgorithms);
	} else {
		/* Enforce default */
		options.hostkeyalgorithms = xstrdup(KEX_DEFAULT_PK_ALG);
		/* Prefer algorithms that we already have keys for */
		myproposal[PROPOSAL_SERVER_HOST_KEY_ALGS] =
		    compat_pkalg_proposal(
		    order_hostkeyalgs(host, hostaddr, port));
	}

	if (options.rekey_limit || options.rekey_interval)
		packet_set_rekey_limits(options.rekey_limit,
		    options.rekey_interval);

	/* start key exchange */
	if ((r = kex_setup(active_state, myproposal)) != 0)
		fatal("kex_setup: %s", ssh_err(r));
	kex = active_state->kex;
#ifdef WITH_OPENSSL
	kex->kex[KEX_DH_GRP1_SHA1] = kexdh_client;
	kex->kex[KEX_DH_GRP14_SHA1] = kexdh_client;
	kex->kex[KEX_DH_GRP14_SHA256] = kexdh_client;
	kex->kex[KEX_DH_GRP16_SHA512] = kexdh_client;
	kex->kex[KEX_DH_GRP18_SHA512] = kexdh_client;
	kex->kex[KEX_DH_GEX_SHA1] = kexgex_client;
	kex->kex[KEX_DH_GEX_SHA256] = kexgex_client;
# ifdef OPENSSL_HAS_ECC
	kex->kex[KEX_ECDH_SHA2] = kexecdh_client;
# endif
#endif
	kex->kex[KEX_C25519_SHA256] = kexc25519_client;
	kex->client_version_string=client_version_string;
	kex->server_version_string=server_version_string;
	kex->verify_host_key=&verify_host_key_callback;

	ssh_dispatch_run_fatal(active_state, DISPATCH_BLOCK, &kex->done);

	/* remove ext-info from the KEX proposals for rekeying */
	myproposal[PROPOSAL_KEX_ALGS] =
	    compat_kex_proposal(options.kex_algorithms);
	if ((r = kex_prop2buf(kex->my, myproposal)) != 0)
		fatal("kex_prop2buf: %s", ssh_err(r));

	session_id2 = kex->session_id;
	session_id2_len = kex->session_id_len;

#ifdef DEBUG_KEXDH
	/* send 1st encrypted/maced/compressed message */
	if ((r = sshpkt_start(ssh, SSH2_MSG_IGNORE)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, "markus")) != 0 ||
	    (r = sshpkt_send(ssh)) != 0 ||
	    (r = ssh_packet_write_wait(ssh)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));
#endif
}

/*
 * Authenticate user
 */

typedef struct cauthctxt Authctxt;
typedef struct cauthmethod Authmethod;
typedef struct identity Identity;
typedef struct idlist Idlist;

struct identity {
	TAILQ_ENTRY(identity) next;
	int	agent_fd;		/* >=0 if agent supports key */
	struct sshkey	*key;		/* public/private key */
	char	*filename;		/* comment for agent-only keys */
	int	tried;
	int	isprivate;		/* key points to the private key */
	int	userprovided;
};
TAILQ_HEAD(idlist, identity);

struct cauthctxt {
	const char *server_user;
	const char *local_user;
	const char *host;
	const char *service;
	struct cauthmethod *method;
	sig_atomic_t success;
	char *authlist;
	int attempt;
	/* pubkey */
	struct idlist keys;
	int agent_fd;
	/* hostbased */
	Sensitive *sensitive;
	char *oktypes, *ktypes;
	const char *active_ktype;
	/* kbd-interactive */
	int info_req_seen;
	/* generic */
	void *methoddata;
};

struct cauthmethod {
	char	*name;		/* string to compare against server's list */
	int	(*userauth)(Authctxt *authctxt);
	void	(*cleanup)(Authctxt *authctxt);
	int	*enabled;	/* flag in option struct that enables method */
	int	*batch_flag;	/* flag in option struct that disables method */
};

int	input_userauth_service_accept(int, u_int32_t, struct ssh *);
int	input_userauth_ext_info(int, u_int32_t, struct ssh *);
int	input_userauth_success(int, u_int32_t, struct ssh *);
int	input_userauth_success_unexpected(int, u_int32_t, struct ssh *);
int	input_userauth_failure(int, u_int32_t, struct ssh *);
int	input_userauth_banner(int, u_int32_t, struct ssh *);
int	input_userauth_error(int, u_int32_t, struct ssh *);
int	input_userauth_info_req(int, u_int32_t, struct ssh *);
int	input_userauth_pk_ok(int, u_int32_t, struct ssh *);
int	input_userauth_passwd_changereq(int, u_int32_t, struct ssh *);

int	userauth_none(Authctxt *);
int	userauth_pubkey(Authctxt *);
int	userauth_passwd(Authctxt *);
int	userauth_kbdint(Authctxt *);
int	userauth_hostbased(Authctxt *);

#ifdef GSSAPI
int	userauth_gssapi(Authctxt *authctxt);
int	input_gssapi_response(int type, u_int32_t, struct ssh *);
int	input_gssapi_token(int type, u_int32_t, struct ssh *);
int	input_gssapi_hash(int type, u_int32_t, struct ssh *);
int	input_gssapi_error(int, u_int32_t, struct ssh *);
int	input_gssapi_errtok(int, u_int32_t, struct ssh *);
#endif

void	userauth(Authctxt *, char *);

static int sign_and_send_pubkey(struct ssh *ssh, Authctxt *, Identity *);
static void pubkey_prepare(Authctxt *);
static void pubkey_cleanup(Authctxt *);
static void pubkey_reset(Authctxt *);
static struct sshkey *load_identity_file(Identity *);

static Authmethod *authmethod_get(char *authlist);
static Authmethod *authmethod_lookup(const char *name);
static char *authmethods_get(void);

Authmethod authmethods[] = {
#ifdef GSSAPI
	{"gssapi-with-mic",
		userauth_gssapi,
		NULL,
		&options.gss_authentication,
		NULL},
#endif
	{"hostbased",
		userauth_hostbased,
		NULL,
		&options.hostbased_authentication,
		NULL},
	{"publickey",
		userauth_pubkey,
		NULL,
		&options.pubkey_authentication,
		NULL},
	{"keyboard-interactive",
		userauth_kbdint,
		NULL,
		&options.kbd_interactive_authentication,
		&options.batch_mode},
	{"password",
		userauth_passwd,
		NULL,
		&options.password_authentication,
		&options.batch_mode},
	{"none",
		userauth_none,
		NULL,
		NULL,
		NULL},
	{NULL, NULL, NULL, NULL, NULL}
};

void
ssh_userauth2(const char *local_user, const char *server_user, char *host,
    Sensitive *sensitive)
{
	struct ssh *ssh = active_state;
	Authctxt authctxt;
	int r;

	if (options.challenge_response_authentication)
		options.kbd_interactive_authentication = 1;
	if (options.preferred_authentications == NULL)
		options.preferred_authentications = authmethods_get();

	/* setup authentication context */
	memset(&authctxt, 0, sizeof(authctxt));
	pubkey_prepare(&authctxt);
	authctxt.server_user = server_user;
	authctxt.local_user = local_user;
	authctxt.host = host;
	authctxt.service = "ssh-connection";		/* service name */
	authctxt.success = 0;
	authctxt.method = authmethod_lookup("none");
	authctxt.authlist = NULL;
	authctxt.methoddata = NULL;
	authctxt.sensitive = sensitive;
	authctxt.active_ktype = authctxt.oktypes = authctxt.ktypes = NULL;
	authctxt.info_req_seen = 0;
	authctxt.agent_fd = -1;
	if (authctxt.method == NULL)
		fatal("ssh_userauth2: internal error: cannot send userauth none request");

	if ((r = sshpkt_start(ssh, SSH2_MSG_SERVICE_REQUEST)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, "ssh-userauth")) != 0 ||
	    (r = sshpkt_send(ssh)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));

	ssh->authctxt = &authctxt;
	ssh_dispatch_init(ssh, &input_userauth_error);
	ssh_dispatch_set(ssh, SSH2_MSG_EXT_INFO, &input_userauth_ext_info);
	ssh_dispatch_set(ssh, SSH2_MSG_SERVICE_ACCEPT, &input_userauth_service_accept);
	ssh_dispatch_run_fatal(ssh, DISPATCH_BLOCK, &authctxt.success);	/* loop until success */
	ssh->authctxt = NULL;

	pubkey_cleanup(&authctxt);
	ssh_dispatch_range(ssh, SSH2_MSG_USERAUTH_MIN, SSH2_MSG_USERAUTH_MAX, NULL);

	if (!authctxt.success)
		fatal("Authentication failed.");
	debug("Authentication succeeded (%s).", authctxt.method->name);
}

/* ARGSUSED */
int
input_userauth_service_accept(int type, u_int32_t seq, struct ssh *ssh)
{
	Authctxt *authctxt = ssh->authctxt;
	int r;

	if (ssh_packet_remaining(ssh) > 0) {
		char *reply;

		if ((r = sshpkt_get_cstring(ssh, &reply, NULL)) != 0)
			goto out;
		debug2("service_accept: %s", reply);
		free(reply);
	} else {
		debug2("buggy server: service_accept w/o service");
	}
	if ((r = sshpkt_get_end(ssh)) != 0)
		goto out;
	debug("SSH2_MSG_SERVICE_ACCEPT received");

	/* initial userauth request */
	userauth_none(authctxt);

	ssh_dispatch_set(ssh, SSH2_MSG_EXT_INFO, &input_userauth_error);
	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_SUCCESS, &input_userauth_success);
	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_FAILURE, &input_userauth_failure);
	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_BANNER, &input_userauth_banner);
	r = 0;
 out:
	return r;
}

/* ARGSUSED */
int
input_userauth_ext_info(int type, u_int32_t seqnr, struct ssh *ssh)
{
	return kex_input_ext_info(type, seqnr, ssh);
}

void
userauth(Authctxt *authctxt, char *authlist)
{
	struct ssh *ssh = active_state; /* XXX */

	if (authctxt->method != NULL && authctxt->method->cleanup != NULL)
		authctxt->method->cleanup(authctxt);

	free(authctxt->methoddata);
	authctxt->methoddata = NULL;
	if (authlist == NULL) {
		authlist = authctxt->authlist;
	} else {
		free(authctxt->authlist);
		authctxt->authlist = authlist;
	}
	for (;;) {
		Authmethod *method = authmethod_get(authlist);
		if (method == NULL)
			fatal("%s@%s: Permission denied (%s).",
			    authctxt->server_user, authctxt->host, authlist);
		authctxt->method = method;

		/* reset the per method handler */
		ssh_dispatch_range(ssh, SSH2_MSG_USERAUTH_PER_METHOD_MIN,
		    SSH2_MSG_USERAUTH_PER_METHOD_MAX, NULL);

		/* and try new method */
		if (method->userauth(authctxt) != 0) {
			debug2("we sent a %s packet, wait for reply", method->name);
			break;
		} else {
			debug2("we did not send a packet, disable method");
			method->enabled = NULL;
		}
	}
}

/* ARGSUSED */
int
input_userauth_error(int type, u_int32_t seq, struct ssh *ssh)
{
	fatal("input_userauth_error: bad message during authentication: "
	    "type %d", type);
	return 0;
}

/* ARGSUSED */
int
input_userauth_banner(int type, u_int32_t seq, struct ssh *ssh)
{
	char *msg, *lang;
	u_int len;

	debug3("%s", __func__);
	msg = packet_get_string(&len);
	lang = packet_get_string(NULL);
	if (len > 0 && options.log_level >= SYSLOG_LEVEL_INFO)
		fmprintf(stderr, "%s", msg);
	free(msg);
	free(lang);
	return 0;
}

/* ARGSUSED */
int
input_userauth_success(int type, u_int32_t seq, struct ssh *ssh)
{
	Authctxt *authctxt = ssh->authctxt;

	if (authctxt == NULL)
		fatal("input_userauth_success: no authentication context");
	free(authctxt->authlist);
	authctxt->authlist = NULL;
	if (authctxt->method != NULL && authctxt->method->cleanup != NULL)
		authctxt->method->cleanup(authctxt);
	free(authctxt->methoddata);
	authctxt->methoddata = NULL;
	authctxt->success = 1;			/* break out */
	return 0;
}

int
input_userauth_success_unexpected(int type, u_int32_t seq, struct ssh *ssh)
{
	Authctxt *authctxt = ssh->authctxt;

	if (authctxt == NULL)
		fatal("%s: no authentication context", __func__);

	fatal("Unexpected authentication success during %s.",
	    authctxt->method->name);
	return 0;
}

/* ARGSUSED */
int
input_userauth_failure(int type, u_int32_t seq, struct ssh *ssh)
{
	Authctxt *authctxt = ssh->authctxt;
	char *authlist = NULL;
	u_char partial;
	int r;

	if (authctxt == NULL)
		fatal("input_userauth_failure: no authentication context");

	if ((r = sshpkt_get_cstring(ssh, &authlist, NULL)) != 0 ||
	    (r = sshpkt_get_u8(ssh, &partial)) != 0 ||
	    (r = sshpkt_get_end(ssh)) != 0)
		goto out;

	if (partial != 0) {
		verbose("Authenticated with partial success.");
		/* reset state */
		pubkey_reset(authctxt);
	}
	debug("Authentications that can continue: %s", authlist);

	userauth(authctxt, authlist);
	authlist = NULL;
 out:
	free(authlist);
	return 0;
}

/* ARGSUSED */
int
input_userauth_pk_ok(int type, u_int32_t seq, struct ssh *ssh)
{
	Authctxt *authctxt = ssh->authctxt;
	struct sshkey *key = NULL;
	Identity *id = NULL;
	int pktype, sent = 0;
	size_t blen;
	char *pkalg = NULL, *fp;
	u_char *pkblob = NULL;
	int r;

	if (authctxt == NULL)
		fatal("input_userauth_pk_ok: no authentication context");

	if ((r = sshpkt_get_cstring(ssh, &pkalg, NULL)) != 0 ||
	    (r = sshpkt_get_string(ssh, &pkblob, &blen)) != 0 ||
	    (r = sshpkt_get_end(ssh)) != 0)
		goto done;

	debug("Server accepts key: pkalg %s blen %zu", pkalg, blen);

	if ((pktype = sshkey_type_from_name(pkalg)) == KEY_UNSPEC) {
		debug("unknown pkalg %s", pkalg);
		goto done;
	}
	if ((r = sshkey_from_blob(pkblob, blen, &key)) != 0) {
		debug("no key from blob. pkalg %s: %s", pkalg, ssh_err(r));
		goto done;
	}
	if (key->type != pktype) {
		error("input_userauth_pk_ok: type mismatch "
		    "for decoded key (received %d, expected %d)",
		    key->type, pktype);
		goto done;
	}
	if ((fp = sshkey_fingerprint(key, options.fingerprint_hash,
	    SSH_FP_DEFAULT)) == NULL)
		goto done;
	debug2("input_userauth_pk_ok: fp %s", fp);
	free(fp);

	/*
	 * search keys in the reverse order, because last candidate has been
	 * moved to the end of the queue.  this also avoids confusion by
	 * duplicate keys
	 */
	TAILQ_FOREACH_REVERSE(id, &authctxt->keys, idlist, next) {
		if (sshkey_equal(key, id->key)) {
			sent = sign_and_send_pubkey(ssh, authctxt, id);
			break;
		}
	}
	r = 0;
 done:
	sshkey_free(key);
	free(pkalg);
	free(pkblob);

	/* try another method if we did not send a packet */
	if (r == 0 && sent == 0)
		userauth(authctxt, NULL);
	return r;
}

#ifdef GSSAPI
int
userauth_gssapi(Authctxt *authctxt)
{
	struct ssh *ssh = active_state; /* XXX */
	Gssctxt *gssctxt = NULL;
	static gss_OID_set gss_supported = NULL;
	static u_int mech = 0;
	OM_uint32 min;
	int r, ok = 0;

	/* Try one GSSAPI method at a time, rather than sending them all at
	 * once. */

	if (gss_supported == NULL)
		gss_indicate_mechs(&min, &gss_supported);

	/* Check to see if the mechanism is usable before we offer it */
	while (mech < gss_supported->count && !ok) {
		/* My DER encoding requires length<128 */
		if (gss_supported->elements[mech].length < 128 &&
		    ssh_gssapi_check_mechanism(&gssctxt,
		    &gss_supported->elements[mech], authctxt->host)) {
			ok = 1; /* Mechanism works */
		} else {
			mech++;
		}
	}

	if (!ok)
		return 0;

	authctxt->methoddata=(void *)gssctxt;

	if ((r = sshpkt_start(ssh, SSH2_MSG_USERAUTH_REQUEST)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, authctxt->server_user)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, authctxt->service)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, authctxt->method->name)) != 0 ||
	    (r = sshpkt_put_u32(ssh, 1)) != 0 ||
	    (r = sshpkt_put_u32(ssh,
	    (gss_supported->elements[mech].length) + 2)) != 0 ||
	    (r = sshpkt_put_u8(ssh, SSH_GSS_OIDTYPE)) != 0 ||
	    (r = sshpkt_put_u8(ssh,
	    gss_supported->elements[mech].length)) != 0 ||
	    (r = sshpkt_put(ssh,
	    gss_supported->elements[mech].elements,
	    gss_supported->elements[mech].length)) != 0 ||
	    (r = sshpkt_send(ssh)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));

	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_GSSAPI_RESPONSE, &input_gssapi_response);
	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_GSSAPI_TOKEN, &input_gssapi_token);
	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_GSSAPI_ERROR, &input_gssapi_error);
	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_GSSAPI_ERRTOK, &input_gssapi_errtok);

	mech++; /* Move along to next candidate */

	return 1;
}

static OM_uint32
process_gssapi_token(struct ssh *ssh, gss_buffer_t recv_tok)
{
	Authctxt *authctxt = ssh->authctxt;
	Gssctxt *gssctxt = authctxt->methoddata;
	gss_buffer_desc send_tok = GSS_C_EMPTY_BUFFER;
	gss_buffer_desc mic = GSS_C_EMPTY_BUFFER;
	gss_buffer_desc gssbuf;
	OM_uint32 status, ms, flags;
	int r;

	status = ssh_gssapi_init_ctx(gssctxt, options.gss_deleg_creds,
	    recv_tok, &send_tok, &flags);

	if (send_tok.length > 0) {
		u_char type = GSS_ERROR(status) ?
		    SSH2_MSG_USERAUTH_GSSAPI_ERRTOK :
		    SSH2_MSG_USERAUTH_GSSAPI_TOKEN;

		if ((r = sshpkt_start(ssh, type)) != 0 ||
		    (r = sshpkt_put_string(ssh, send_tok.value,
		    send_tok.length)) != 0 ||
		    (r = sshpkt_send(ssh)) != 0)
			fatal("%s: %s", __func__, ssh_err(r));

		gss_release_buffer(&ms, &send_tok);
	}

	if (status == GSS_S_COMPLETE) {
		/* send either complete or MIC, depending on mechanism */
		if (!(flags & GSS_C_INTEG_FLAG)) {
			if ((r = sshpkt_start(ssh,
			    SSH2_MSG_USERAUTH_GSSAPI_EXCHANGE_COMPLETE)) != 0 ||
			    (r = sshpkt_send(ssh)) != 0)
				fatal("%s: %s", __func__, ssh_err(r));
		} else {
			struct sshbuf *b;

			if ((b = sshbuf_new()) == NULL)
				fatal("%s: sshbuf_new failed", __func__);
			ssh_gssapi_buildmic(b, authctxt->server_user,
			    authctxt->service, "gssapi-with-mic");

			if ((gssbuf.value = sshbuf_mutable_ptr(b)) == NULL)
				fatal("%s: sshbuf_mutable_ptr failed", __func__);
			gssbuf.length = sshbuf_len(b);

			status = ssh_gssapi_sign(gssctxt, &gssbuf, &mic);

			if (!GSS_ERROR(status)) {
				if ((r = sshpkt_start(ssh,
				    SSH2_MSG_USERAUTH_GSSAPI_MIC)) != 0 ||
				    (r = sshpkt_put_string(ssh, mic.value,
				    mic.length)) != 0 ||
				    (r = sshpkt_send(ssh)) != 0)
					fatal("%s: %s", __func__, ssh_err(r));
			}

			sshbuf_free(b);
			gss_release_buffer(&ms, &mic);
		}
	}

	return status;
}

/* ARGSUSED */
int
input_gssapi_response(int type, u_int32_t plen, struct ssh *ssh)
{
	Authctxt *authctxt = ssh->authctxt;
	Gssctxt *gssctxt;
	size_t oidlen;
	u_char *oidv = NULL;
	int r;

	if (authctxt == NULL)
		fatal("input_gssapi_response: no authentication context");
	gssctxt = authctxt->methoddata;

	/* Setup our OID */
	if ((r = sshpkt_get_string(ssh, &oidv, &oidlen)) != 0)
		goto done;

	if (oidlen <= 2 ||
	    oidv[0] != SSH_GSS_OIDTYPE ||
	    oidv[1] != oidlen - 2) {
		debug("Badly encoded mechanism OID received");
		userauth(authctxt, NULL);
		goto ok;
	}

	if (!ssh_gssapi_check_oid(gssctxt, oidv + 2, oidlen - 2))
		fatal("Server returned different OID than expected");

	if ((r = sshpkt_get_end(ssh)) != 0)
		goto done;

	if (GSS_ERROR(process_gssapi_token(ssh, GSS_C_NO_BUFFER))) {
		/* Start again with next method on list */
		debug("Trying to start again");
		userauth(authctxt, NULL);
		goto ok;
	}
 ok:
	r = 0;
 done:
	free(oidv);
	return r;
}

/* ARGSUSED */
int
input_gssapi_token(int type, u_int32_t plen, struct ssh *ssh)
{
	Authctxt *authctxt = ssh->authctxt;
	gss_buffer_desc recv_tok;
	u_char *p = NULL;
	size_t len;
	OM_uint32 status;
	int r;

	if (authctxt == NULL)
		fatal("input_gssapi_response: no authentication context");

	if ((r = sshpkt_get_string(ssh, &p, &len)) != 0 ||
	    (r = sshpkt_get_end(ssh)) != 0)
		goto out;

	recv_tok.value = p;
	recv_tok.length = len;
	status = process_gssapi_token(ssh, &recv_tok);

	/* Start again with the next method in the list */
	if (GSS_ERROR(status)) {
		userauth(authctxt, NULL);
		/* ok */
	}
	r = 0;
 out:
	free(p);
	return r;
}

/* ARGSUSED */
int
input_gssapi_errtok(int type, u_int32_t plen, struct ssh *ssh)
{
	Authctxt *authctxt = ssh->authctxt;
	Gssctxt *gssctxt;
	gss_buffer_desc send_tok = GSS_C_EMPTY_BUFFER;
	gss_buffer_desc recv_tok;
	OM_uint32 ms;
	u_char *p = NULL;
	size_t len;
	int r;

	if (authctxt == NULL)
		fatal("input_gssapi_response: no authentication context");
	gssctxt = authctxt->methoddata;

	if ((r = sshpkt_get_string(ssh, &p, &len)) != 0 ||
	    (r = sshpkt_get_end(ssh)) != 0) {
		free(p);
		return r;
	}

	/* Stick it into GSSAPI and see what it says */
	recv_tok.value = p;
	recv_tok.length = len;
	(void)ssh_gssapi_init_ctx(gssctxt, options.gss_deleg_creds,
	    &recv_tok, &send_tok, NULL);
	free(p);
	gss_release_buffer(&ms, &send_tok);

	/* Server will be returning a failed packet after this one */
	return 0;
}

/* ARGSUSED */
int
input_gssapi_error(int type, u_int32_t plen, struct ssh *ssh)
{
	char *msg = NULL;
	char *lang = NULL;
	int r;

	if ((r = sshpkt_get_u32(ssh, NULL)) != 0 ||	/* maj */
	    (r = sshpkt_get_u32(ssh, NULL)) != 0 ||	/* min */
	    (r = sshpkt_get_cstring(ssh, &msg, NULL)) != 0 ||
	    (r = sshpkt_get_cstring(ssh, &lang, NULL)) != 0)
		goto out;
	r = sshpkt_get_end(ssh);
	debug("Server GSSAPI Error:\n%s", msg);
 out:
	free(msg);
	free(lang);
	return r;
}
#endif /* GSSAPI */

int
userauth_none(Authctxt *authctxt)
{
	struct ssh *ssh = active_state; /* XXX */
	int r;

	/* initial userauth request */
	if ((r = sshpkt_start(ssh, SSH2_MSG_USERAUTH_REQUEST)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, authctxt->server_user)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, authctxt->service)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, authctxt->method->name)) != 0 ||
	    (r = sshpkt_send(ssh)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));
	return 1;
}

int
userauth_passwd(Authctxt *authctxt)
{
	struct ssh *ssh = active_state; /* XXX */
	static int attempt = 0;
	char prompt[256];
	char *password;
	const char *host = options.host_key_alias ?  options.host_key_alias :
	    authctxt->host;
	int r;

	if (attempt++ >= options.number_of_password_prompts)
		return 0;

	if (attempt != 1)
		error("Permission denied, please try again.");

	snprintf(prompt, sizeof(prompt), "%.30s@%.128s's password: ",
	    authctxt->server_user, host);
	password = read_passphrase(prompt, 0);
	if ((r = sshpkt_start(ssh, SSH2_MSG_USERAUTH_REQUEST)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, authctxt->server_user)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, authctxt->service)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, authctxt->method->name)) != 0 ||
	    (r = sshpkt_put_u8(ssh, 0)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, password)) != 0 ||
	    (r = sshpkt_add_padding(ssh, 64)) != 0 ||
	    (r = sshpkt_send(ssh)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));

	if (password)
		freezero(password, strlen(password));

	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_PASSWD_CHANGEREQ,
	    &input_userauth_passwd_changereq);

	return 1;
}

/*
 * parse PASSWD_CHANGEREQ, prompt user and send SSH2_MSG_USERAUTH_REQUEST
 */
/* ARGSUSED */
int
input_userauth_passwd_changereq(int type, u_int32_t seqnr, struct ssh *ssh)
{
	Authctxt *authctxt = ssh->authctxt;
	char *info = NULL, *lang = NULL, *password = NULL, *retype = NULL;
	char prompt[256];
	const char *host;
	int r;

	debug2("input_userauth_passwd_changereq");

	if (authctxt == NULL)
		fatal("input_userauth_passwd_changereq: "
		    "no authentication context");
	host = options.host_key_alias ? options.host_key_alias : authctxt->host;

	if ((r = sshpkt_get_cstring(ssh, &info, NULL)) != 0 ||
	    (r = sshpkt_get_cstring(ssh, &lang, NULL)) != 0)
		goto out;
	if (strlen(info) > 0)
		logit("%s", info);
	if ((r = sshpkt_start(ssh, SSH2_MSG_USERAUTH_REQUEST)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, authctxt->server_user)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, authctxt->service)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, authctxt->method->name)) != 0 ||
	    (r = sshpkt_put_u8(ssh, 1)) != 0)	/* additional info */
		goto out;

	snprintf(prompt, sizeof(prompt),
	    "Enter %.30s@%.128s's old password: ",
	    authctxt->server_user, host);
	password = read_passphrase(prompt, 0);
	if ((r = sshpkt_put_cstring(ssh, password)) != 0)
		goto out;

	freezero(password, strlen(password));
	password = NULL;
	while (password == NULL) {
		snprintf(prompt, sizeof(prompt),
		    "Enter %.30s@%.128s's new password: ",
		    authctxt->server_user, host);
		password = read_passphrase(prompt, RP_ALLOW_EOF);
		if (password == NULL) {
			/* bail out */
			r = 0;
			goto out;
		}
		snprintf(prompt, sizeof(prompt),
		    "Retype %.30s@%.128s's new password: ",
		    authctxt->server_user, host);
		retype = read_passphrase(prompt, 0);
		if (strcmp(password, retype) != 0) {
			freezero(password, strlen(password));
			logit("Mismatch; try again, EOF to quit.");
			password = NULL;
		}
		freezero(retype, strlen(retype));
	}
	if ((r = sshpkt_put_cstring(ssh, password)) != 0 ||
	    (r = sshpkt_add_padding(ssh, 64)) != 0 ||
	    (r = sshpkt_send(ssh)) != 0)
		goto out;

	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_PASSWD_CHANGEREQ,
	    &input_userauth_passwd_changereq);
	r = 0;
 out:
	if (password)
		freezero(password, strlen(password));
	free(info);
	free(lang);
	return r;
}

/*
 * Select an algorithm for publickey signatures.
 * Returns algorithm (caller must free) or NULL if no mutual algorithm found.
 *
 * Call with ssh==NULL to ignore server-sig-algs extension list and
 * only attempt with the key's base signature type.
 */
static char *
key_sig_algorithm(struct ssh *ssh, const struct sshkey *key)
{
	char *allowed, *oallowed, *cp, *tmp, *alg = NULL;

	/*
	 * The signature algorithm will only differ from the key algorithm
	 * for RSA keys/certs and when the server advertises support for
	 * newer (SHA2) algorithms.
	 */
	if (ssh == NULL || ssh->kex->server_sig_algs == NULL ||
	    (key->type != KEY_RSA && key->type != KEY_RSA_CERT)) {
		/* Filter base key signature alg against our configuration */
		return match_list(sshkey_ssh_name(key),
		    options.pubkey_key_types, NULL);
	}

	/*
	 * For RSA keys/certs, since these might have a different sig type:
	 * find the first entry in PubkeyAcceptedKeyTypes of the right type
	 * that also appears in the supported signature algorithms list from
	 * the server.
	 */
	oallowed = allowed = xstrdup(options.pubkey_key_types);
	while ((cp = strsep(&allowed, ",")) != NULL) {
		if (sshkey_type_from_name(cp) != key->type)
			continue;
		tmp = match_list(sshkey_sigalg_by_name(cp), ssh->kex->server_sig_algs, NULL);
		if (tmp != NULL)
			alg = xstrdup(cp);
		free(tmp);
		if (alg != NULL)
			break;
	}
	free(oallowed);
	return alg;
}

static int
identity_sign(struct identity *id, u_char **sigp, size_t *lenp,
    const u_char *data, size_t datalen, u_int compat, const char *alg)
{
	struct sshkey *prv;
	int r;

	/* The agent supports this key. */
	if (id->key != NULL && id->agent_fd != -1) {
		return ssh_agent_sign(id->agent_fd, id->key, sigp, lenp,
		    data, datalen, alg, compat);
	}

	/*
	 * We have already loaded the private key or the private key is
	 * stored in external hardware.
	 */
	if (id->key != NULL &&
	    (id->isprivate || (id->key->flags & SSHKEY_FLAG_EXT))) {
		if ((r = sshkey_sign(id->key, sigp, lenp, data, datalen,
		    alg, compat)) != 0)
			return r;
		/*
		 * PKCS#11 tokens may not support all signature algorithms,
		 * so check what we get back.
		 */
		if ((r = sshkey_check_sigtype(*sigp, *lenp, alg)) != 0)
			return r;
		return 0;
	}

	/* Load the private key from the file. */
	if ((prv = load_identity_file(id)) == NULL)
		return SSH_ERR_KEY_NOT_FOUND;
	if (id->key != NULL && !sshkey_equal_public(prv, id->key)) {
		error("%s: private key %s contents do not match public",
		   __func__, id->filename);
		return SSH_ERR_KEY_NOT_FOUND;
	}
	r = sshkey_sign(prv, sigp, lenp, data, datalen, alg, compat);
	sshkey_free(prv);
	return r;
}

static int
id_filename_matches(Identity *id, Identity *private_id)
{
	const char *suffixes[] = { ".pub", "-cert.pub", NULL };
	size_t len = strlen(id->filename), plen = strlen(private_id->filename);
	size_t i, slen;

	if (strcmp(id->filename, private_id->filename) == 0)
		return 1;
	for (i = 0; suffixes[i]; i++) {
		slen = strlen(suffixes[i]);
		if (len > slen && plen == len - slen &&
		    strcmp(id->filename + (len - slen), suffixes[i]) == 0 &&
		    memcmp(id->filename, private_id->filename, plen) == 0)
			return 1;
	}
	return 0;
}

static int
sign_and_send_pubkey(struct ssh *ssh, Authctxt *authctxt, Identity *id)
{
	struct sshbuf *b = NULL;
	Identity *private_id, *sign_id = NULL;
	u_char *signature = NULL;
	size_t slen = 0, skip = 0;
	int r, fallback_sigtype, sent = 0;
	char *alg = NULL, *fp = NULL;
	const char *loc = "";

	if ((fp = sshkey_fingerprint(id->key, options.fingerprint_hash,
	    SSH_FP_DEFAULT)) == NULL)
		return 0;

	debug3("%s: %s %s", __func__, sshkey_type(id->key), fp);

	/*
	 * If the key is an certificate, try to find a matching private key
	 * and use it to complete the signature.
	 * If no such private key exists, fall back to trying the certificate
	 * key itself in case it has a private half already loaded.
	 * This will try to set sign_id to the private key that will perform
	 * the signature.
	 */
	if (sshkey_is_cert(id->key)) {
		TAILQ_FOREACH(private_id, &authctxt->keys, next) {
			if (sshkey_equal_public(id->key, private_id->key) &&
			    id->key->type != private_id->key->type) {
				sign_id = private_id;
				break;
			}
		}
		/*
		 * Exact key matches are preferred, but also allow
		 * filename matches for non-PKCS#11/agent keys that
		 * didn't load public keys. This supports the case
		 * of keeping just a private key file and public
		 * certificate on disk.
		 */
		if (sign_id == NULL &&
		    !id->isprivate && id->agent_fd == -1 &&
		    (id->key->flags & SSHKEY_FLAG_EXT) == 0) {
			TAILQ_FOREACH(private_id, &authctxt->keys, next) {
				if (private_id->key == NULL &&
				    id_filename_matches(id, private_id)) {
					sign_id = private_id;
					break;
				}
			}
		}
		if (sign_id != NULL) {
			debug2("%s: using private key \"%s\"%s for "
			    "certificate", __func__, id->filename,
			    id->agent_fd != -1 ? " from agent" : "");
		} else {
			debug("%s: no separate private key for certificate "
			    "\"%s\"", __func__, id->filename);
		}
	}

	/*
	 * If the above didn't select another identity to do the signing
	 * then default to the one we started with.
	 */
	if (sign_id == NULL)
		sign_id = id;

	/* assemble and sign data */
	for (fallback_sigtype = 0; fallback_sigtype <= 1; fallback_sigtype++) {
		free(alg);
		slen = 0;
		signature = NULL;
		if ((alg = key_sig_algorithm(fallback_sigtype ? NULL : ssh,
		    id->key)) == NULL) {
			error("%s: no mutual signature supported", __func__);
			goto out;
		}
		debug3("%s: signing using %s", __func__, alg);

		sshbuf_free(b);
		if ((b = sshbuf_new()) == NULL)
			fatal("%s: sshbuf_new failed", __func__);
		if (datafellows & SSH_OLD_SESSIONID) {
			if ((r = sshbuf_put(b, session_id2,
			    session_id2_len)) != 0) {
				fatal("%s: sshbuf_put: %s",
				    __func__, ssh_err(r));
			}
		} else {
			if ((r = sshbuf_put_string(b, session_id2,
			    session_id2_len)) != 0) {
				fatal("%s: sshbuf_put_string: %s",
				    __func__, ssh_err(r));
			}
		}
		skip = sshbuf_len(b);
		if ((r = sshbuf_put_u8(b, SSH2_MSG_USERAUTH_REQUEST)) != 0 ||
		    (r = sshbuf_put_cstring(b, authctxt->server_user)) != 0 ||
		    (r = sshbuf_put_cstring(b, authctxt->service)) != 0 ||
		    (r = sshbuf_put_cstring(b, authctxt->method->name)) != 0 ||
		    (r = sshbuf_put_u8(b, 1)) != 0 ||
		    (r = sshbuf_put_cstring(b, alg)) != 0 ||
		    (r = sshkey_puts(id->key, b)) != 0) {
			fatal("%s: assemble signed data: %s",
			    __func__, ssh_err(r));
		}

		/* generate signature */
		r = identity_sign(sign_id, &signature, &slen,
		    sshbuf_ptr(b), sshbuf_len(b), datafellows, alg);
		if (r == 0)
			break;
		else if (r == SSH_ERR_KEY_NOT_FOUND)
			goto out; /* soft failure */
		else if (r == SSH_ERR_SIGN_ALG_UNSUPPORTED &&
		    !fallback_sigtype) {
			if (sign_id->agent_fd != -1)
				loc = "agent ";
			else if ((sign_id->key->flags & SSHKEY_FLAG_EXT) != 0)
				loc = "token ";
			logit("%skey %s %s returned incorrect signature type",
			    loc, sshkey_type(id->key), fp);
			continue;
		}
		error("%s: signing failed: %s", __func__, ssh_err(r));
		goto out;
	}
	if (slen == 0 || signature == NULL) /* shouldn't happen */
		fatal("%s: no signature", __func__);

	/* append signature */
	if ((r = sshbuf_put_string(b, signature, slen)) != 0)
		fatal("%s: append signature: %s", __func__, ssh_err(r));

#ifdef DEBUG_PK
	sshbuf_dump(b, stderr);
#endif
	/* skip session id and packet type */
	if ((r = sshbuf_consume(b, skip + 1)) != 0)
		fatal("%s: consume: %s", __func__, ssh_err(r));

	/* put remaining data from buffer into packet */
	if ((r = sshpkt_start(ssh, SSH2_MSG_USERAUTH_REQUEST)) != 0 ||
	    (r = sshpkt_putb(ssh, b)) != 0 ||
	    (r = sshpkt_send(ssh)) != 0)
		fatal("%s: enqueue request: %s", __func__, ssh_err(r));

	/* success */
	sent = 1;

 out:
	free(fp);
	free(alg);
	sshbuf_free(b);
	freezero(signature, slen);
	return sent;
}

static int
send_pubkey_test(struct ssh *ssh, Authctxt *authctxt, Identity *id)
{
	u_char *blob = NULL;
	char *alg = NULL;
	size_t bloblen;
	u_int have_sig = 0;
	int sent = 0, r;

	if ((alg = key_sig_algorithm(ssh, id->key)) == NULL) {
		debug("%s: no mutual signature algorithm", __func__);
		goto out;
	}

	if ((r = sshkey_to_blob(id->key, &blob, &bloblen)) != 0) {
		/* we cannot handle this key */
		debug3("%s: cannot handle key", __func__);
		goto out;
	}
	/* register callback for USERAUTH_PK_OK message */
	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_PK_OK, &input_userauth_pk_ok);

	if ((r = sshpkt_start(ssh, SSH2_MSG_USERAUTH_REQUEST)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, authctxt->server_user)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, authctxt->service)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, authctxt->method->name)) != 0 ||
	    (r = sshpkt_put_u8(ssh, have_sig)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, alg)) != 0 ||
	    (r = sshpkt_put_string(ssh, blob, bloblen)) != 0 ||
	    (r = sshpkt_send(ssh)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));
	sent = 1;

 out:
	free(alg);
	free(blob);
	return sent;
}

static struct sshkey *
load_identity_file(Identity *id)
{
	struct sshkey *private = NULL;
	char prompt[300], *passphrase, *comment;
	int r, perm_ok = 0, quit = 0, i;
	struct stat st;

	if (stat(id->filename, &st) < 0) {
		(id->userprovided ? logit : debug3)("no such identity: %s: %s",
		    id->filename, strerror(errno));
		return NULL;
	}
	snprintf(prompt, sizeof prompt,
	    "Enter passphrase for key '%.100s': ", id->filename);
	for (i = 0; i <= options.number_of_password_prompts; i++) {
		if (i == 0)
			passphrase = "";
		else {
			passphrase = read_passphrase(prompt, 0);
			if (*passphrase == '\0') {
				debug2("no passphrase given, try next key");
				free(passphrase);
				break;
			}
		}
		switch ((r = sshkey_load_private_type(KEY_UNSPEC, id->filename,
		    passphrase, &private, &comment, &perm_ok))) {
		case 0:
			break;
		case SSH_ERR_KEY_WRONG_PASSPHRASE:
			if (options.batch_mode) {
				quit = 1;
				break;
			}
			if (i != 0)
				debug2("bad passphrase given, try again...");
			break;
		case SSH_ERR_SYSTEM_ERROR:
			if (errno == ENOENT) {
				debug2("Load key \"%s\": %s",
				    id->filename, ssh_err(r));
				quit = 1;
				break;
			}
			/* FALLTHROUGH */
		default:
			error("Load key \"%s\": %s", id->filename, ssh_err(r));
			quit = 1;
			break;
		}
		if (!quit && private != NULL && id->agent_fd == -1 &&
		    !(id->key && id->isprivate))
			maybe_add_key_to_agent(id->filename, private, comment,
			    passphrase);
		if (i > 0)
			freezero(passphrase, strlen(passphrase));
		free(comment);
		if (private != NULL || quit)
			break;
	}
	return private;
}

static int
key_type_allowed_by_config(struct sshkey *key)
{
	if (match_pattern_list(sshkey_ssh_name(key),
	    options.pubkey_key_types, 0) == 1)
		return 1;

	/* RSA keys/certs might be allowed by alternate signature types */
	switch (key->type) {
	case KEY_RSA:
		if (match_pattern_list("rsa-sha2-512",
		    options.pubkey_key_types, 0) == 1)
			return 1;
		if (match_pattern_list("rsa-sha2-256",
		    options.pubkey_key_types, 0) == 1)
			return 1;
		break;
	case KEY_RSA_CERT:
		if (match_pattern_list("rsa-sha2-512-cert-v01@openssh.com",
		    options.pubkey_key_types, 0) == 1)
			return 1;
		if (match_pattern_list("rsa-sha2-256-cert-v01@openssh.com",
		    options.pubkey_key_types, 0) == 1)
			return 1;
		break;
	}
	return 0;
}


/*
 * try keys in the following order:
 * 	1. certificates listed in the config file
 * 	2. other input certificates
 *	3. agent keys that are found in the config file
 *	4. other agent keys
 *	5. keys that are only listed in the config file
 */
static void
pubkey_prepare(Authctxt *authctxt)
{
	struct identity *id, *id2, *tmp;
	struct idlist agent, files, *preferred;
	struct sshkey *key;
	int agent_fd = -1, i, r, found;
	size_t j;
	struct ssh_identitylist *idlist;

	TAILQ_INIT(&agent);	/* keys from the agent */
	TAILQ_INIT(&files);	/* keys from the config file */
	preferred = &authctxt->keys;
	TAILQ_INIT(preferred);	/* preferred order of keys */

	/* list of keys stored in the filesystem and PKCS#11 */
	for (i = 0; i < options.num_identity_files; i++) {
		key = options.identity_keys[i];
		if (key && key->cert && key->cert->type != SSH2_CERT_TYPE_USER)
			continue;
		options.identity_keys[i] = NULL;
		id = xcalloc(1, sizeof(*id));
		id->agent_fd = -1;
		id->key = key;
		id->filename = xstrdup(options.identity_files[i]);
		id->userprovided = options.identity_file_userprovided[i];
		TAILQ_INSERT_TAIL(&files, id, next);
	}
	/* list of certificates specified by user */
	for (i = 0; i < options.num_certificate_files; i++) {
		key = options.certificates[i];
		if (!sshkey_is_cert(key) || key->cert == NULL ||
		    key->cert->type != SSH2_CERT_TYPE_USER)
			continue;
		id = xcalloc(1, sizeof(*id));
		id->agent_fd = -1;
		id->key = key;
		id->filename = xstrdup(options.certificate_files[i]);
		id->userprovided = options.certificate_file_userprovided[i];
		TAILQ_INSERT_TAIL(preferred, id, next);
	}
	/* list of keys supported by the agent */
	if ((r = ssh_get_authentication_socket(&agent_fd)) != 0) {
		if (r != SSH_ERR_AGENT_NOT_PRESENT)
			debug("%s: ssh_get_authentication_socket: %s",
			    __func__, ssh_err(r));
	} else if ((r = ssh_fetch_identitylist(agent_fd, &idlist)) != 0) {
		if (r != SSH_ERR_AGENT_NO_IDENTITIES)
			debug("%s: ssh_fetch_identitylist: %s",
			    __func__, ssh_err(r));
		close(agent_fd);
	} else {
		for (j = 0; j < idlist->nkeys; j++) {
			found = 0;
			TAILQ_FOREACH(id, &files, next) {
				/*
				 * agent keys from the config file are
				 * preferred
				 */
				if (sshkey_equal(idlist->keys[j], id->key)) {
					TAILQ_REMOVE(&files, id, next);
					TAILQ_INSERT_TAIL(preferred, id, next);
					id->agent_fd = agent_fd;
					found = 1;
					break;
				}
			}
			if (!found && !options.identities_only) {
				id = xcalloc(1, sizeof(*id));
				/* XXX "steals" key/comment from idlist */
				id->key = idlist->keys[j];
				id->filename = idlist->comments[j];
				idlist->keys[j] = NULL;
				idlist->comments[j] = NULL;
				id->agent_fd = agent_fd;
				TAILQ_INSERT_TAIL(&agent, id, next);
			}
		}
		ssh_free_identitylist(idlist);
		/* append remaining agent keys */
		for (id = TAILQ_FIRST(&agent); id; id = TAILQ_FIRST(&agent)) {
			TAILQ_REMOVE(&agent, id, next);
			TAILQ_INSERT_TAIL(preferred, id, next);
		}
		authctxt->agent_fd = agent_fd;
	}
	/* Prefer PKCS11 keys that are explicitly listed */
	TAILQ_FOREACH_SAFE(id, &files, next, tmp) {
		if (id->key == NULL || (id->key->flags & SSHKEY_FLAG_EXT) == 0)
			continue;
		found = 0;
		TAILQ_FOREACH(id2, &files, next) {
			if (id2->key == NULL ||
			    (id2->key->flags & SSHKEY_FLAG_EXT) == 0)
				continue;
			if (sshkey_equal(id->key, id2->key)) {
				TAILQ_REMOVE(&files, id, next);
				TAILQ_INSERT_TAIL(preferred, id, next);
				found = 1;
				break;
			}
		}
		/* If IdentitiesOnly set and key not found then don't use it */
		if (!found && options.identities_only) {
			TAILQ_REMOVE(&files, id, next);
			freezero(id, sizeof(*id));
		}
	}
	/* append remaining keys from the config file */
	for (id = TAILQ_FIRST(&files); id; id = TAILQ_FIRST(&files)) {
		TAILQ_REMOVE(&files, id, next);
		TAILQ_INSERT_TAIL(preferred, id, next);
	}
	/* finally, filter by PubkeyAcceptedKeyTypes */
	TAILQ_FOREACH_SAFE(id, preferred, next, id2) {
		if (id->key != NULL && !key_type_allowed_by_config(id->key)) {
			debug("Skipping %s key %s - "
			    "not in PubkeyAcceptedKeyTypes",
			    sshkey_ssh_name(id->key), id->filename);
			TAILQ_REMOVE(preferred, id, next);
			sshkey_free(id->key);
			free(id->filename);
			memset(id, 0, sizeof(*id));
			continue;
		}
		debug2("key: %s (%p)%s%s", id->filename, id->key,
		    id->userprovided ? ", explicit" : "",
		    id->agent_fd != -1 ? ", agent" : "");
	}
}

static void
pubkey_cleanup(Authctxt *authctxt)
{
	Identity *id;

	if (authctxt->agent_fd != -1)
		ssh_close_authentication_socket(authctxt->agent_fd);
	for (id = TAILQ_FIRST(&authctxt->keys); id;
	    id = TAILQ_FIRST(&authctxt->keys)) {
		TAILQ_REMOVE(&authctxt->keys, id, next);
		sshkey_free(id->key);
		free(id->filename);
		free(id);
	}
}

static void
pubkey_reset(Authctxt *authctxt)
{
	Identity *id;

	TAILQ_FOREACH(id, &authctxt->keys, next)
		id->tried = 0;
}

static int
try_identity(Identity *id)
{
	if (!id->key)
		return (0);
	if (sshkey_type_plain(id->key->type) == KEY_RSA &&
	    (datafellows & SSH_BUG_RSASIGMD5) != 0) {
		debug("Skipped %s key %s for RSA/MD5 server",
		    sshkey_type(id->key), id->filename);
		return (0);
	}
	return 1;
}

int
userauth_pubkey(Authctxt *authctxt)
{
	struct ssh *ssh = active_state; /* XXX */
	Identity *id;
	int sent = 0;
	char *fp;

	while ((id = TAILQ_FIRST(&authctxt->keys))) {
		if (id->tried++)
			return (0);
		/* move key to the end of the queue */
		TAILQ_REMOVE(&authctxt->keys, id, next);
		TAILQ_INSERT_TAIL(&authctxt->keys, id, next);
		/*
		 * send a test message if we have the public key. for
		 * encrypted keys we cannot do this and have to load the
		 * private key instead
		 */
		if (id->key != NULL) {
			if (try_identity(id)) {
				if ((fp = sshkey_fingerprint(id->key,
				    options.fingerprint_hash,
				    SSH_FP_DEFAULT)) == NULL) {
					error("%s: sshkey_fingerprint failed",
					    __func__);
					return 0;
				}
				debug("Offering public key: %s %s %s",
				    sshkey_type(id->key), fp, id->filename);
				free(fp);
				sent = send_pubkey_test(ssh, authctxt, id);
			}
		} else {
			debug("Trying private key: %s", id->filename);
			id->key = load_identity_file(id);
			if (id->key != NULL) {
				if (try_identity(id)) {
					id->isprivate = 1;
					sent = sign_and_send_pubkey(ssh,
					    authctxt, id);
				}
				sshkey_free(id->key);
				id->key = NULL;
				id->isprivate = 0;
			}
		}
		if (sent)
			return (sent);
	}
	return (0);
}

/*
 * Send userauth request message specifying keyboard-interactive method.
 */
int
userauth_kbdint(Authctxt *authctxt)
{
	struct ssh *ssh = active_state; /* XXX */
	static int attempt = 0;
	int r;

	if (attempt++ >= options.number_of_password_prompts)
		return 0;
	/* disable if no SSH2_MSG_USERAUTH_INFO_REQUEST has been seen */
	if (attempt > 1 && !authctxt->info_req_seen) {
		debug3("userauth_kbdint: disable: no info_req_seen");
		ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_INFO_REQUEST, NULL);
		return 0;
	}

	debug2("userauth_kbdint");
	if ((r = sshpkt_start(ssh, SSH2_MSG_USERAUTH_REQUEST)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, authctxt->server_user)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, authctxt->service)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, authctxt->method->name)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, "")) != 0 ||		/* lang */
	    (r = sshpkt_put_cstring(ssh, options.kbd_interactive_devices ?
	    options.kbd_interactive_devices : "")) != 0 ||
	    (r = sshpkt_send(ssh)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));

	ssh_dispatch_set(ssh, SSH2_MSG_USERAUTH_INFO_REQUEST, &input_userauth_info_req);
	return 1;
}

/*
 * parse INFO_REQUEST, prompt user and send INFO_RESPONSE
 */
int
input_userauth_info_req(int type, u_int32_t seq, struct ssh *ssh)
{
	Authctxt *authctxt = ssh->authctxt;
	char *name = NULL, *inst = NULL, *lang = NULL, *prompt = NULL;
	char *response = NULL;
	u_char echo = 0;
	u_int num_prompts, i;
	int r;

	debug2("input_userauth_info_req");

	if (authctxt == NULL)
		fatal("input_userauth_info_req: no authentication context");

	authctxt->info_req_seen = 1;

	if ((r = sshpkt_get_cstring(ssh, &name, NULL)) != 0 ||
	    (r = sshpkt_get_cstring(ssh, &inst, NULL)) != 0 ||
	    (r = sshpkt_get_cstring(ssh, &lang, NULL)) != 0)
		goto out;
	if (strlen(name) > 0)
		logit("%s", name);
	if (strlen(inst) > 0)
		logit("%s", inst);

	if ((r = sshpkt_get_u32(ssh, &num_prompts)) != 0)
		goto out;
	/*
	 * Begin to build info response packet based on prompts requested.
	 * We commit to providing the correct number of responses, so if
	 * further on we run into a problem that prevents this, we have to
	 * be sure and clean this up and send a correct error response.
	 */
	if ((r = sshpkt_start(ssh, SSH2_MSG_USERAUTH_INFO_RESPONSE)) != 0 ||
	    (r = sshpkt_put_u32(ssh, num_prompts)) != 0)
		goto out;

	debug2("input_userauth_info_req: num_prompts %d", num_prompts);
	for (i = 0; i < num_prompts; i++) {
		if ((r = sshpkt_get_cstring(ssh, &prompt, NULL)) != 0 ||
		    (r = sshpkt_get_u8(ssh, &echo)) != 0)
			goto out;
		response = read_passphrase(prompt, echo ? RP_ECHO : 0);
		if ((r = sshpkt_put_cstring(ssh, response)) != 0)
			goto out;
		freezero(response, strlen(response));
		free(prompt);
		response = prompt = NULL;
	}
	/* done with parsing incoming message. */
	if ((r = sshpkt_get_end(ssh)) != 0 ||
	    (r = sshpkt_add_padding(ssh, 64)) != 0)
		goto out;
	r = sshpkt_send(ssh);
 out:
	if (response)
		freezero(response, strlen(response));
	free(prompt);
	free(name);
	free(inst);
	free(lang);
	return r;
}

static int
ssh_keysign(struct sshkey *key, u_char **sigp, size_t *lenp,
    const u_char *data, size_t datalen)
{
	struct sshbuf *b;
	struct stat st;
	pid_t pid;
	int i, r, to[2], from[2], status, sock = packet_get_connection_in();
	u_char rversion = 0, version = 2;
	void (*osigchld)(int);

	*sigp = NULL;
	*lenp = 0;

	if (stat(_PATH_SSH_KEY_SIGN, &st) < 0) {
		error("%s: not installed: %s", __func__, strerror(errno));
		return -1;
	}
	if (fflush(stdout) != 0) {
		error("%s: fflush: %s", __func__, strerror(errno));
		return -1;
	}
	if (pipe(to) < 0) {
		error("%s: pipe: %s", __func__, strerror(errno));
		return -1;
	}
	if (pipe(from) < 0) {
		error("%s: pipe: %s", __func__, strerror(errno));
		return -1;
	}
	if ((pid = fork()) < 0) {
		error("%s: fork: %s", __func__, strerror(errno));
		return -1;
	}
	osigchld = signal(SIGCHLD, SIG_DFL);
	if (pid == 0) {
		/* keep the socket on exec */
		fcntl(sock, F_SETFD, 0);
		close(from[0]);
		if (dup2(from[1], STDOUT_FILENO) < 0)
			fatal("%s: dup2: %s", __func__, strerror(errno));
		close(to[1]);
		if (dup2(to[0], STDIN_FILENO) < 0)
			fatal("%s: dup2: %s", __func__, strerror(errno));
		close(from[1]);
		close(to[0]);
		/* Close everything but stdio and the socket */
		for (i = STDERR_FILENO + 1; i < sock; i++)
			close(i);
		closefrom(sock + 1);
		debug3("%s: [child] pid=%ld, exec %s",
		    __func__, (long)getpid(), _PATH_SSH_KEY_SIGN);
		execl(_PATH_SSH_KEY_SIGN, _PATH_SSH_KEY_SIGN, (char *)NULL);
		fatal("%s: exec(%s): %s", __func__, _PATH_SSH_KEY_SIGN,
		    strerror(errno));
	}
	close(from[1]);
	close(to[0]);

	if ((b = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new failed", __func__);
	/* send # of sock, data to be signed */
	if ((r = sshbuf_put_u32(b, sock)) != 0 ||
	    (r = sshbuf_put_string(b, data, datalen)) != 0)
		fatal("%s: buffer error: %s", __func__, ssh_err(r));
	if (ssh_msg_send(to[1], version, b) == -1)
		fatal("%s: couldn't send request", __func__);
	sshbuf_reset(b);
	r = ssh_msg_recv(from[0], b);
	close(from[0]);
	close(to[1]);
	if (r < 0) {
		error("%s: no reply", __func__);
		goto fail;
	}

	errno = 0;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR) {
			error("%s: waitpid %ld: %s",
			    __func__, (long)pid, strerror(errno));
			goto fail;
		}
	}
	if (!WIFEXITED(status)) {
		error("%s: exited abnormally", __func__);
		goto fail;
	}
	if (WEXITSTATUS(status) != 0) {
		error("%s: exited with status %d",
		    __func__, WEXITSTATUS(status));
		goto fail;
	}
	if ((r = sshbuf_get_u8(b, &rversion)) != 0) {
		error("%s: buffer error: %s", __func__, ssh_err(r));
		goto fail;
	}
	if (rversion != version) {
		error("%s: bad version", __func__);
		goto fail;
	}
	if ((r = sshbuf_get_string(b, sigp, lenp)) != 0) {
		error("%s: buffer error: %s", __func__, ssh_err(r));
 fail:
		signal(SIGCHLD, osigchld);
		sshbuf_free(b);
		return -1;
	}
	signal(SIGCHLD, osigchld);
	sshbuf_free(b);

	return 0;
}

int
userauth_hostbased(Authctxt *authctxt)
{
	struct ssh *ssh = active_state; /* XXX */
	struct sshkey *private = NULL;
	struct sshbuf *b = NULL;
	u_char *sig = NULL, *keyblob = NULL;
	char *fp = NULL, *chost = NULL, *lname = NULL;
	size_t siglen = 0, keylen = 0;
	int i, r, success = 0;

	if (authctxt->ktypes == NULL) {
		authctxt->oktypes = xstrdup(options.hostbased_key_types);
		authctxt->ktypes = authctxt->oktypes;
	}

	/*
	 * Work through each listed type pattern in HostbasedKeyTypes,
	 * trying each hostkey that matches the type in turn.
	 */
	for (;;) {
		if (authctxt->active_ktype == NULL)
			authctxt->active_ktype = strsep(&authctxt->ktypes, ",");
		if (authctxt->active_ktype == NULL ||
		    *authctxt->active_ktype == '\0')
			break;
		debug3("%s: trying key type %s", __func__,
		    authctxt->active_ktype);

		/* check for a useful key */
		private = NULL;
		for (i = 0; i < authctxt->sensitive->nkeys; i++) {
			if (authctxt->sensitive->keys[i] == NULL ||
			    authctxt->sensitive->keys[i]->type == KEY_UNSPEC)
				continue;
			if (match_pattern_list(
			    sshkey_ssh_name(authctxt->sensitive->keys[i]),
			    authctxt->active_ktype, 0) != 1)
				continue;
			/* we take and free the key */
			private = authctxt->sensitive->keys[i];
			authctxt->sensitive->keys[i] = NULL;
			break;
		}
		/* Found one */
		if (private != NULL)
			break;
		/* No more keys of this type; advance */
		authctxt->active_ktype = NULL;
	}
	if (private == NULL) {
		free(authctxt->oktypes);
		authctxt->oktypes = authctxt->ktypes = NULL;
		authctxt->active_ktype = NULL;
		debug("No more client hostkeys for hostbased authentication.");
		goto out;
	}

	if ((fp = sshkey_fingerprint(private, options.fingerprint_hash,
	    SSH_FP_DEFAULT)) == NULL) {
		error("%s: sshkey_fingerprint failed", __func__);
		goto out;
	}
	debug("%s: trying hostkey %s %s",
	    __func__, sshkey_ssh_name(private), fp);

	/* figure out a name for the client host */
	if ((lname = get_local_name(packet_get_connection_in())) == NULL) {
		error("%s: cannot get local ipaddr/name", __func__);
		goto out;
	}

	/* XXX sshbuf_put_stringf? */
	xasprintf(&chost, "%s.", lname);
	debug2("%s: chost %s", __func__, chost);

	/* construct data */
	if ((b = sshbuf_new()) == NULL) {
		error("%s: sshbuf_new failed", __func__);
		goto out;
	}
	if ((r = sshkey_to_blob(private, &keyblob, &keylen)) != 0) {
		error("%s: sshkey_to_blob: %s", __func__, ssh_err(r));
		goto out;
	}
	if ((r = sshbuf_put_string(b, session_id2, session_id2_len)) != 0 ||
	    (r = sshbuf_put_u8(b, SSH2_MSG_USERAUTH_REQUEST)) != 0 ||
	    (r = sshbuf_put_cstring(b, authctxt->server_user)) != 0 ||
	    (r = sshbuf_put_cstring(b, authctxt->service)) != 0 ||
	    (r = sshbuf_put_cstring(b, authctxt->method->name)) != 0 ||
	    (r = sshbuf_put_cstring(b, sshkey_ssh_name(private))) != 0 ||
	    (r = sshbuf_put_string(b, keyblob, keylen)) != 0 ||
	    (r = sshbuf_put_cstring(b, chost)) != 0 ||
	    (r = sshbuf_put_cstring(b, authctxt->local_user)) != 0) {
		error("%s: buffer error: %s", __func__, ssh_err(r));
		goto out;
	}

#ifdef DEBUG_PK
	sshbuf_dump(b, stderr);
#endif
	r = ssh_keysign(private, &sig, &siglen,
	    sshbuf_ptr(b), sshbuf_len(b));
	if (r != 0) {
		error("sign using hostkey %s %s failed",
		    sshkey_ssh_name(private), fp);
		goto out;
	}
	if ((r = sshpkt_start(ssh, SSH2_MSG_USERAUTH_REQUEST)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, authctxt->server_user)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, authctxt->service)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, authctxt->method->name)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, sshkey_ssh_name(private))) != 0 ||
	    (r = sshpkt_put_string(ssh, keyblob, keylen)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, chost)) != 0 ||
	    (r = sshpkt_put_cstring(ssh, authctxt->local_user)) != 0 ||
	    (r = sshpkt_put_string(ssh, sig, siglen)) != 0 ||
	    (r = sshpkt_send(ssh)) != 0) {
		error("%s: packet error: %s", __func__, ssh_err(r));
		goto out;
	}
	success = 1;

 out:
	if (sig != NULL)
		freezero(sig, siglen);
	free(keyblob);
	free(lname);
	free(fp);
	free(chost);
	sshkey_free(private);
	sshbuf_free(b);

	return success;
}

/* find auth method */

/*
 * given auth method name, if configurable options permit this method fill
 * in auth_ident field and return true, otherwise return false.
 */
static int
authmethod_is_enabled(Authmethod *method)
{
	if (method == NULL)
		return 0;
	/* return false if options indicate this method is disabled */
	if  (method->enabled == NULL || *method->enabled == 0)
		return 0;
	/* return false if batch mode is enabled but method needs interactive mode */
	if  (method->batch_flag != NULL && *method->batch_flag != 0)
		return 0;
	return 1;
}

static Authmethod *
authmethod_lookup(const char *name)
{
	Authmethod *method = NULL;
	if (name != NULL)
		for (method = authmethods; method->name != NULL; method++)
			if (strcmp(name, method->name) == 0)
				return method;
	debug2("Unrecognized authentication method name: %s", name ? name : "NULL");
	return NULL;
}

/* XXX internal state */
static Authmethod *current = NULL;
static char *supported = NULL;
static char *preferred = NULL;

/*
 * Given the authentication method list sent by the server, return the
 * next method we should try.  If the server initially sends a nil list,
 * use a built-in default list.
 */
static Authmethod *
authmethod_get(char *authlist)
{
	char *name = NULL;
	u_int next;

	/* Use a suitable default if we're passed a nil list.  */
	if (authlist == NULL || strlen(authlist) == 0)
		authlist = options.preferred_authentications;

	if (supported == NULL || strcmp(authlist, supported) != 0) {
		debug3("start over, passed a different list %s", authlist);
		free(supported);
		supported = xstrdup(authlist);
		preferred = options.preferred_authentications;
		debug3("preferred %s", preferred);
		current = NULL;
	} else if (current != NULL && authmethod_is_enabled(current))
		return current;

	for (;;) {
		if ((name = match_list(preferred, supported, &next)) == NULL) {
			debug("No more authentication methods to try.");
			current = NULL;
			return NULL;
		}
		preferred += next;
		debug3("authmethod_lookup %s", name);
		debug3("remaining preferred: %s", preferred);
		if ((current = authmethod_lookup(name)) != NULL &&
		    authmethod_is_enabled(current)) {
			debug3("authmethod_is_enabled %s", name);
			debug("Next authentication method: %s", name);
			free(name);
			return current;
		}
		free(name);
	}
}

static char *
authmethods_get(void)
{
	Authmethod *method = NULL;
	struct sshbuf *b;
	char *list;
	int r;

	if ((b = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new failed", __func__);
	for (method = authmethods; method->name != NULL; method++) {
		if (authmethod_is_enabled(method)) {
			if ((r = sshbuf_putf(b, "%s%s",
			    sshbuf_len(b) ? "," : "", method->name)) != 0)
				fatal("%s: buffer error: %s",
				    __func__, ssh_err(r));
		}
	}
	if ((list = sshbuf_dup_string(b)) == NULL)
		fatal("%s: sshbuf_dup_string failed", __func__);
	sshbuf_free(b);
	return list;
}
