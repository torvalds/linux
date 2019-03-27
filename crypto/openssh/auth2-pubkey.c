/* $OpenBSD: auth2-pubkey.c,v 1.84 2018/08/23 03:01:08 djm Exp $ */
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

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#ifdef HAVE_PATHS_H
# include <paths.h>
#endif
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

#include "xmalloc.h"
#include "ssh.h"
#include "ssh2.h"
#include "packet.h"
#include "sshbuf.h"
#include "log.h"
#include "misc.h"
#include "servconf.h"
#include "compat.h"
#include "sshkey.h"
#include "hostfile.h"
#include "auth.h"
#include "pathnames.h"
#include "uidswap.h"
#include "auth-options.h"
#include "canohost.h"
#ifdef GSSAPI
#include "ssh-gss.h"
#endif
#include "monitor_wrap.h"
#include "authfile.h"
#include "match.h"
#include "ssherr.h"
#include "channels.h" /* XXX for session.h */
#include "session.h" /* XXX for child_set_env(); refactor? */

/* import */
extern ServerOptions options;
extern u_char *session_id2;
extern u_int session_id2_len;

static char *
format_key(const struct sshkey *key)
{
	char *ret, *fp = sshkey_fingerprint(key,
	    options.fingerprint_hash, SSH_FP_DEFAULT);

	xasprintf(&ret, "%s %s", sshkey_type(key), fp);
	free(fp);
	return ret;
}

static int
userauth_pubkey(struct ssh *ssh)
{
	Authctxt *authctxt = ssh->authctxt;
	struct passwd *pw = authctxt->pw;
	struct sshbuf *b = NULL;
	struct sshkey *key = NULL;
	char *pkalg = NULL, *userstyle = NULL, *key_s = NULL, *ca_s = NULL;
	u_char *pkblob = NULL, *sig = NULL, have_sig;
	size_t blen, slen;
	int r, pktype;
	int authenticated = 0;
	struct sshauthopt *authopts = NULL;

	if ((r = sshpkt_get_u8(ssh, &have_sig)) != 0 ||
	    (r = sshpkt_get_cstring(ssh, &pkalg, NULL)) != 0 ||
	    (r = sshpkt_get_string(ssh, &pkblob, &blen)) != 0)
		fatal("%s: parse request failed: %s", __func__, ssh_err(r));
	pktype = sshkey_type_from_name(pkalg);
	if (pktype == KEY_UNSPEC) {
		/* this is perfectly legal */
		verbose("%s: unsupported public key algorithm: %s",
		    __func__, pkalg);
		goto done;
	}
	if ((r = sshkey_from_blob(pkblob, blen, &key)) != 0) {
		error("%s: could not parse key: %s", __func__, ssh_err(r));
		goto done;
	}
	if (key == NULL) {
		error("%s: cannot decode key: %s", __func__, pkalg);
		goto done;
	}
	if (key->type != pktype) {
		error("%s: type mismatch for decoded key "
		    "(received %d, expected %d)", __func__, key->type, pktype);
		goto done;
	}
	if (sshkey_type_plain(key->type) == KEY_RSA &&
	    (ssh->compat & SSH_BUG_RSASIGMD5) != 0) {
		logit("Refusing RSA key because client uses unsafe "
		    "signature scheme");
		goto done;
	}
	if (auth2_key_already_used(authctxt, key)) {
		logit("refusing previously-used %s key", sshkey_type(key));
		goto done;
	}
	if (match_pattern_list(pkalg, options.pubkey_key_types, 0) != 1) {
		logit("%s: key type %s not in PubkeyAcceptedKeyTypes",
		    __func__, sshkey_ssh_name(key));
		goto done;
	}

	key_s = format_key(key);
	if (sshkey_is_cert(key))
		ca_s = format_key(key->cert->signature_key);

	if (have_sig) {
		debug3("%s: have %s signature for %s%s%s",
		    __func__, pkalg, key_s,
		    ca_s == NULL ? "" : " CA ",
		    ca_s == NULL ? "" : ca_s);
		if ((r = sshpkt_get_string(ssh, &sig, &slen)) != 0 ||
		    (r = sshpkt_get_end(ssh)) != 0)
			fatal("%s: %s", __func__, ssh_err(r));
		if ((b = sshbuf_new()) == NULL)
			fatal("%s: sshbuf_new failed", __func__);
		if (ssh->compat & SSH_OLD_SESSIONID) {
			if ((r = sshbuf_put(b, session_id2,
			    session_id2_len)) != 0)
				fatal("%s: sshbuf_put session id: %s",
				    __func__, ssh_err(r));
		} else {
			if ((r = sshbuf_put_string(b, session_id2,
			    session_id2_len)) != 0)
				fatal("%s: sshbuf_put_string session id: %s",
				    __func__, ssh_err(r));
		}
		if (!authctxt->valid || authctxt->user == NULL) {
			debug2("%s: disabled because of invalid user",
			    __func__);
			goto done;
		}
		/* reconstruct packet */
		xasprintf(&userstyle, "%s%s%s", authctxt->user,
		    authctxt->style ? ":" : "",
		    authctxt->style ? authctxt->style : "");
		if ((r = sshbuf_put_u8(b, SSH2_MSG_USERAUTH_REQUEST)) != 0 ||
		    (r = sshbuf_put_cstring(b, userstyle)) != 0 ||
		    (r = sshbuf_put_cstring(b, authctxt->service)) != 0 ||
		    (r = sshbuf_put_cstring(b, "publickey")) != 0 ||
		    (r = sshbuf_put_u8(b, have_sig)) != 0 ||
		    (r = sshbuf_put_cstring(b, pkalg) != 0) ||
		    (r = sshbuf_put_string(b, pkblob, blen)) != 0)
			fatal("%s: build packet failed: %s",
			    __func__, ssh_err(r));
#ifdef DEBUG_PK
		sshbuf_dump(b, stderr);
#endif
		/* test for correct signature */
		authenticated = 0;
		if (PRIVSEP(user_key_allowed(ssh, pw, key, 1, &authopts)) &&
		    PRIVSEP(sshkey_verify(key, sig, slen,
		    sshbuf_ptr(b), sshbuf_len(b),
		    (ssh->compat & SSH_BUG_SIGTYPE) == 0 ? pkalg : NULL,
		    ssh->compat)) == 0) {
			authenticated = 1;
		}
		auth2_record_key(authctxt, authenticated, key);
	} else {
		debug("%s: test pkalg %s pkblob %s%s%s",
		    __func__, pkalg, key_s,
		    ca_s == NULL ? "" : " CA ",
		    ca_s == NULL ? "" : ca_s);

		if ((r = sshpkt_get_end(ssh)) != 0)
			fatal("%s: %s", __func__, ssh_err(r));

		if (!authctxt->valid || authctxt->user == NULL) {
			debug2("%s: disabled because of invalid user",
			    __func__);
			goto done;
		}
		/* XXX fake reply and always send PK_OK ? */
		/*
		 * XXX this allows testing whether a user is allowed
		 * to login: if you happen to have a valid pubkey this
		 * message is sent. the message is NEVER sent at all
		 * if a user is not allowed to login. is this an
		 * issue? -markus
		 */
		if (PRIVSEP(user_key_allowed(ssh, pw, key, 0, NULL))) {
			if ((r = sshpkt_start(ssh, SSH2_MSG_USERAUTH_PK_OK))
			    != 0 ||
			    (r = sshpkt_put_cstring(ssh, pkalg)) != 0 ||
			    (r = sshpkt_put_string(ssh, pkblob, blen)) != 0 ||
			    (r = sshpkt_send(ssh)) != 0 ||
			    (r = ssh_packet_write_wait(ssh)) != 0)
				fatal("%s: %s", __func__, ssh_err(r));
			authctxt->postponed = 1;
		}
	}
done:
	if (authenticated == 1 && auth_activate_options(ssh, authopts) != 0) {
		debug("%s: key options inconsistent with existing", __func__);
		authenticated = 0;
	}
	debug2("%s: authenticated %d pkalg %s", __func__, authenticated, pkalg);

	sshbuf_free(b);
	sshauthopt_free(authopts);
	sshkey_free(key);
	free(userstyle);
	free(pkalg);
	free(pkblob);
	free(key_s);
	free(ca_s);
	free(sig);
	return authenticated;
}

static int
match_principals_option(const char *principal_list, struct sshkey_cert *cert)
{
	char *result;
	u_int i;

	/* XXX percent_expand() sequences for authorized_principals? */

	for (i = 0; i < cert->nprincipals; i++) {
		if ((result = match_list(cert->principals[i],
		    principal_list, NULL)) != NULL) {
			debug3("matched principal from key options \"%.100s\"",
			    result);
			free(result);
			return 1;
		}
	}
	return 0;
}

/*
 * Process a single authorized_principals format line. Returns 0 and sets
 * authoptsp is principal is authorised, -1 otherwise. "loc" is used as a
 * log preamble for file/line information.
 */
static int
check_principals_line(struct ssh *ssh, char *cp, const struct sshkey_cert *cert,
    const char *loc, struct sshauthopt **authoptsp)
{
	u_int i, found = 0;
	char *ep, *line_opts;
	const char *reason = NULL;
	struct sshauthopt *opts = NULL;

	if (authoptsp != NULL)
		*authoptsp = NULL;

	/* Trim trailing whitespace. */
	ep = cp + strlen(cp) - 1;
	while (ep > cp && (*ep == '\n' || *ep == ' ' || *ep == '\t'))
		*ep-- = '\0';

	/*
	 * If the line has internal whitespace then assume it has
	 * key options.
	 */
	line_opts = NULL;
	if ((ep = strrchr(cp, ' ')) != NULL ||
	    (ep = strrchr(cp, '\t')) != NULL) {
		for (; *ep == ' ' || *ep == '\t'; ep++)
			;
		line_opts = cp;
		cp = ep;
	}
	if ((opts = sshauthopt_parse(line_opts, &reason)) == NULL) {
		debug("%s: bad principals options: %s", loc, reason);
		auth_debug_add("%s: bad principals options: %s", loc, reason);
		return -1;
	}
	/* Check principals in cert against those on line */
	for (i = 0; i < cert->nprincipals; i++) {
		if (strcmp(cp, cert->principals[i]) != 0)
			continue;
		debug3("%s: matched principal \"%.100s\"",
		    loc, cert->principals[i]);
		found = 1;
	}
	if (found && authoptsp != NULL) {
		*authoptsp = opts;
		opts = NULL;
	}
	sshauthopt_free(opts);
	return found ? 0 : -1;
}

static int
process_principals(struct ssh *ssh, FILE *f, const char *file,
    const struct sshkey_cert *cert, struct sshauthopt **authoptsp)
{
	char loc[256], *line = NULL, *cp, *ep;
	size_t linesize = 0;
	u_long linenum = 0;
	u_int found_principal = 0;

	if (authoptsp != NULL)
		*authoptsp = NULL;

	while (getline(&line, &linesize, f) != -1) {
		linenum++;
		/* Always consume entire input */
		if (found_principal)
			continue;

		/* Skip leading whitespace. */
		for (cp = line; *cp == ' ' || *cp == '\t'; cp++)
			;
		/* Skip blank and comment lines. */
		if ((ep = strchr(cp, '#')) != NULL)
			*ep = '\0';
		if (!*cp || *cp == '\n')
			continue;

		snprintf(loc, sizeof(loc), "%.200s:%lu", file, linenum);
		if (check_principals_line(ssh, cp, cert, loc, authoptsp) == 0)
			found_principal = 1;
	}
	free(line);
	return found_principal;
}

/* XXX remove pw args here and elsewhere once ssh->authctxt is guaranteed */

static int
match_principals_file(struct ssh *ssh, struct passwd *pw, char *file,
    struct sshkey_cert *cert, struct sshauthopt **authoptsp)
{
	FILE *f;
	int success;

	if (authoptsp != NULL)
		*authoptsp = NULL;

	temporarily_use_uid(pw);
	debug("trying authorized principals file %s", file);
	if ((f = auth_openprincipals(file, pw, options.strict_modes)) == NULL) {
		restore_uid();
		return 0;
	}
	success = process_principals(ssh, f, file, cert, authoptsp);
	fclose(f);
	restore_uid();
	return success;
}

/*
 * Checks whether principal is allowed in output of command.
 * returns 1 if the principal is allowed or 0 otherwise.
 */
static int
match_principals_command(struct ssh *ssh, struct passwd *user_pw,
    const struct sshkey *key, struct sshauthopt **authoptsp)
{
	struct passwd *runas_pw = NULL;
	const struct sshkey_cert *cert = key->cert;
	FILE *f = NULL;
	int r, ok, found_principal = 0;
	int i, ac = 0, uid_swapped = 0;
	pid_t pid;
	char *tmp, *username = NULL, *command = NULL, **av = NULL;
	char *ca_fp = NULL, *key_fp = NULL, *catext = NULL, *keytext = NULL;
	char serial_s[16], uidstr[32];
	void (*osigchld)(int);

	if (authoptsp != NULL)
		*authoptsp = NULL;
	if (options.authorized_principals_command == NULL)
		return 0;
	if (options.authorized_principals_command_user == NULL) {
		error("No user for AuthorizedPrincipalsCommand specified, "
		    "skipping");
		return 0;
	}

	/*
	 * NB. all returns later this function should go via "out" to
	 * ensure the original SIGCHLD handler is restored properly.
	 */
	osigchld = signal(SIGCHLD, SIG_DFL);

	/* Prepare and verify the user for the command */
	username = percent_expand(options.authorized_principals_command_user,
	    "u", user_pw->pw_name, (char *)NULL);
	runas_pw = getpwnam(username);
	if (runas_pw == NULL) {
		error("AuthorizedPrincipalsCommandUser \"%s\" not found: %s",
		    username, strerror(errno));
		goto out;
	}

	/* Turn the command into an argument vector */
	if (argv_split(options.authorized_principals_command, &ac, &av) != 0) {
		error("AuthorizedPrincipalsCommand \"%s\" contains "
		    "invalid quotes", command);
		goto out;
	}
	if (ac == 0) {
		error("AuthorizedPrincipalsCommand \"%s\" yielded no arguments",
		    command);
		goto out;
	}
	if ((ca_fp = sshkey_fingerprint(cert->signature_key,
	    options.fingerprint_hash, SSH_FP_DEFAULT)) == NULL) {
		error("%s: sshkey_fingerprint failed", __func__);
		goto out;
	}
	if ((key_fp = sshkey_fingerprint(key,
	    options.fingerprint_hash, SSH_FP_DEFAULT)) == NULL) {
		error("%s: sshkey_fingerprint failed", __func__);
		goto out;
	}
	if ((r = sshkey_to_base64(cert->signature_key, &catext)) != 0) {
		error("%s: sshkey_to_base64 failed: %s", __func__, ssh_err(r));
		goto out;
	}
	if ((r = sshkey_to_base64(key, &keytext)) != 0) {
		error("%s: sshkey_to_base64 failed: %s", __func__, ssh_err(r));
		goto out;
	}
	snprintf(serial_s, sizeof(serial_s), "%llu",
	    (unsigned long long)cert->serial);
	snprintf(uidstr, sizeof(uidstr), "%llu",
	    (unsigned long long)user_pw->pw_uid);
	for (i = 1; i < ac; i++) {
		tmp = percent_expand(av[i],
		    "U", uidstr,
		    "u", user_pw->pw_name,
		    "h", user_pw->pw_dir,
		    "t", sshkey_ssh_name(key),
		    "T", sshkey_ssh_name(cert->signature_key),
		    "f", key_fp,
		    "F", ca_fp,
		    "k", keytext,
		    "K", catext,
		    "i", cert->key_id,
		    "s", serial_s,
		    (char *)NULL);
		if (tmp == NULL)
			fatal("%s: percent_expand failed", __func__);
		free(av[i]);
		av[i] = tmp;
	}
	/* Prepare a printable command for logs, etc. */
	command = argv_assemble(ac, av);

	if ((pid = subprocess("AuthorizedPrincipalsCommand", runas_pw, command,
	    ac, av, &f,
	    SSH_SUBPROCESS_STDOUT_CAPTURE|SSH_SUBPROCESS_STDERR_DISCARD)) == 0)
		goto out;

	uid_swapped = 1;
	temporarily_use_uid(runas_pw);

	ok = process_principals(ssh, f, "(command)", cert, authoptsp);

	fclose(f);
	f = NULL;

	if (exited_cleanly(pid, "AuthorizedPrincipalsCommand", command, 0) != 0)
		goto out;

	/* Read completed successfully */
	found_principal = ok;
 out:
	if (f != NULL)
		fclose(f);
	signal(SIGCHLD, osigchld);
	for (i = 0; i < ac; i++)
		free(av[i]);
	free(av);
	if (uid_swapped)
		restore_uid();
	free(command);
	free(username);
	free(ca_fp);
	free(key_fp);
	free(catext);
	free(keytext);
	return found_principal;
}

static void
skip_space(char **cpp)
{
	char *cp;

	for (cp = *cpp; *cp == ' ' || *cp == '\t'; cp++)
		;
	*cpp = cp;
}

/*
 * Advanced *cpp past the end of key options, defined as the first unquoted
 * whitespace character. Returns 0 on success or -1 on failure (e.g.
 * unterminated quotes).
 */
static int
advance_past_options(char **cpp)
{
	char *cp = *cpp;
	int quoted = 0;

	for (; *cp && (quoted || (*cp != ' ' && *cp != '\t')); cp++) {
		if (*cp == '\\' && cp[1] == '"')
			cp++;	/* Skip both */
		else if (*cp == '"')
			quoted = !quoted;
	}
	*cpp = cp;
	/* return failure for unterminated quotes */
	return (*cp == '\0' && quoted) ? -1 : 0;
}

/*
 * Check a single line of an authorized_keys-format file. Returns 0 if key
 * matches, -1 otherwise. Will return key/cert options via *authoptsp
 * on success. "loc" is used as file/line location in log messages.
 */
static int
check_authkey_line(struct ssh *ssh, struct passwd *pw, struct sshkey *key,
    char *cp, const char *loc, struct sshauthopt **authoptsp)
{
	int want_keytype = sshkey_is_cert(key) ? KEY_UNSPEC : key->type;
	struct sshkey *found = NULL;
	struct sshauthopt *keyopts = NULL, *certopts = NULL, *finalopts = NULL;
	char *key_options = NULL, *fp = NULL;
	const char *reason = NULL;
	int ret = -1;

	if (authoptsp != NULL)
		*authoptsp = NULL;

	if ((found = sshkey_new(want_keytype)) == NULL) {
		debug3("%s: keytype %d failed", __func__, want_keytype);
		goto out;
	}

	/* XXX djm: peek at key type in line and skip if unwanted */

	if (sshkey_read(found, &cp) != 0) {
		/* no key?  check for options */
		debug2("%s: check options: '%s'", loc, cp);
		key_options = cp;
		if (advance_past_options(&cp) != 0) {
			reason = "invalid key option string";
			goto fail_reason;
		}
		skip_space(&cp);
		if (sshkey_read(found, &cp) != 0) {
			/* still no key?  advance to next line*/
			debug2("%s: advance: '%s'", loc, cp);
			goto out;
		}
	}
	/* Parse key options now; we need to know if this is a CA key */
	if ((keyopts = sshauthopt_parse(key_options, &reason)) == NULL) {
		debug("%s: bad key options: %s", loc, reason);
		auth_debug_add("%s: bad key options: %s", loc, reason);
		goto out;
	}
	/* Ignore keys that don't match or incorrectly marked as CAs */
	if (sshkey_is_cert(key)) {
		/* Certificate; check signature key against CA */
		if (!sshkey_equal(found, key->cert->signature_key) ||
		    !keyopts->cert_authority)
			goto out;
	} else {
		/* Plain key: check it against key found in file */
		if (!sshkey_equal(found, key) || keyopts->cert_authority)
			goto out;
	}

	/* We have a candidate key, perform authorisation checks */
	if ((fp = sshkey_fingerprint(found,
	    options.fingerprint_hash, SSH_FP_DEFAULT)) == NULL)
		fatal("%s: fingerprint failed", __func__);

	debug("%s: matching %s found: %s %s", loc,
	    sshkey_is_cert(key) ? "CA" : "key", sshkey_type(found), fp);

	if (auth_authorise_keyopts(ssh, pw, keyopts,
	    sshkey_is_cert(key), loc) != 0) {
		reason = "Refused by key options";
		goto fail_reason;
	}
	/* That's all we need for plain keys. */
	if (!sshkey_is_cert(key)) {
		verbose("Accepted key %s %s found at %s",
		    sshkey_type(found), fp, loc);
		finalopts = keyopts;
		keyopts = NULL;
		goto success;
	}

	/*
	 * Additional authorisation for certificates.
	 */

	/* Parse and check options present in certificate */
	if ((certopts = sshauthopt_from_cert(key)) == NULL) {
		reason = "Invalid certificate options";
		goto fail_reason;
	}
	if (auth_authorise_keyopts(ssh, pw, certopts, 0, loc) != 0) {
		reason = "Refused by certificate options";
		goto fail_reason;
	}
	if ((finalopts = sshauthopt_merge(keyopts, certopts, &reason)) == NULL)
		goto fail_reason;

	/*
	 * If the user has specified a list of principals as
	 * a key option, then prefer that list to matching
	 * their username in the certificate principals list.
	 */
	if (keyopts->cert_principals != NULL &&
	    !match_principals_option(keyopts->cert_principals, key->cert)) {
		reason = "Certificate does not contain an authorized principal";
		goto fail_reason;
	}
	if (sshkey_cert_check_authority(key, 0, 0,
	   keyopts->cert_principals == NULL ? pw->pw_name : NULL, &reason) != 0)
		goto fail_reason;

	verbose("Accepted certificate ID \"%s\" (serial %llu) "
	    "signed by CA %s %s found at %s",
	    key->cert->key_id,
	    (unsigned long long)key->cert->serial,
	    sshkey_type(found), fp, loc);

 success:
	if (finalopts == NULL)
		fatal("%s: internal error: missing options", __func__);
	if (authoptsp != NULL) {
		*authoptsp = finalopts;
		finalopts = NULL;
	}
	/* success */
	ret = 0;
	goto out;

 fail_reason:
	error("%s", reason);
	auth_debug_add("%s", reason);
 out:
	free(fp);
	sshauthopt_free(keyopts);
	sshauthopt_free(certopts);
	sshauthopt_free(finalopts);
	sshkey_free(found);
	return ret;
}

/*
 * Checks whether key is allowed in authorized_keys-format file,
 * returns 1 if the key is allowed or 0 otherwise.
 */
static int
check_authkeys_file(struct ssh *ssh, struct passwd *pw, FILE *f,
    char *file, struct sshkey *key, struct sshauthopt **authoptsp)
{
	char *cp, *line = NULL, loc[256];
	size_t linesize = 0;
	int found_key = 0;
	u_long linenum = 0;

	if (authoptsp != NULL)
		*authoptsp = NULL;

	while (getline(&line, &linesize, f) != -1) {
		linenum++;
		/* Always consume entire file */
		if (found_key)
			continue;

		/* Skip leading whitespace, empty and comment lines. */
		cp = line;
		skip_space(&cp);
		if (!*cp || *cp == '\n' || *cp == '#')
			continue;
		snprintf(loc, sizeof(loc), "%.200s:%lu", file, linenum);
		if (check_authkey_line(ssh, pw, key, cp, loc, authoptsp) == 0)
			found_key = 1;
	}
	free(line);
	return found_key;
}

/* Authenticate a certificate key against TrustedUserCAKeys */
static int
user_cert_trusted_ca(struct ssh *ssh, struct passwd *pw, struct sshkey *key,
    struct sshauthopt **authoptsp)
{
	char *ca_fp, *principals_file = NULL;
	const char *reason;
	struct sshauthopt *principals_opts = NULL, *cert_opts = NULL;
	struct sshauthopt *final_opts = NULL;
	int r, ret = 0, found_principal = 0, use_authorized_principals;

	if (authoptsp != NULL)
		*authoptsp = NULL;

	if (!sshkey_is_cert(key) || options.trusted_user_ca_keys == NULL)
		return 0;

	if ((ca_fp = sshkey_fingerprint(key->cert->signature_key,
	    options.fingerprint_hash, SSH_FP_DEFAULT)) == NULL)
		return 0;

	if ((r = sshkey_in_file(key->cert->signature_key,
	    options.trusted_user_ca_keys, 1, 0)) != 0) {
		debug2("%s: CA %s %s is not listed in %s: %s", __func__,
		    sshkey_type(key->cert->signature_key), ca_fp,
		    options.trusted_user_ca_keys, ssh_err(r));
		goto out;
	}
	/*
	 * If AuthorizedPrincipals is in use, then compare the certificate
	 * principals against the names in that file rather than matching
	 * against the username.
	 */
	if ((principals_file = authorized_principals_file(pw)) != NULL) {
		if (match_principals_file(ssh, pw, principals_file,
		    key->cert, &principals_opts))
			found_principal = 1;
	}
	/* Try querying command if specified */
	if (!found_principal && match_principals_command(ssh, pw, key,
	    &principals_opts))
		found_principal = 1;
	/* If principals file or command is specified, then require a match */
	use_authorized_principals = principals_file != NULL ||
            options.authorized_principals_command != NULL;
	if (!found_principal && use_authorized_principals) {
		reason = "Certificate does not contain an authorized principal";
		goto fail_reason;
	}
	if (use_authorized_principals && principals_opts == NULL)
		fatal("%s: internal error: missing principals_opts", __func__);
	if (sshkey_cert_check_authority(key, 0, 1,
	    use_authorized_principals ? NULL : pw->pw_name, &reason) != 0)
		goto fail_reason;

	/* Check authority from options in key and from principals file/cmd */
	if ((cert_opts = sshauthopt_from_cert(key)) == NULL) {
		reason = "Invalid certificate options";
		goto fail_reason;
	}
	if (auth_authorise_keyopts(ssh, pw, cert_opts, 0, "cert") != 0) {
		reason = "Refused by certificate options";
		goto fail_reason;
	}
	if (principals_opts == NULL) {
		final_opts = cert_opts;
		cert_opts = NULL;
	} else {
		if (auth_authorise_keyopts(ssh, pw, principals_opts, 0,
		    "principals") != 0) {
			reason = "Refused by certificate principals options";
			goto fail_reason;
		}
		if ((final_opts = sshauthopt_merge(principals_opts,
		    cert_opts, &reason)) == NULL) {
 fail_reason:
			error("%s", reason);
			auth_debug_add("%s", reason);
			goto out;
		}
	}

	/* Success */
	verbose("Accepted certificate ID \"%s\" (serial %llu) signed by "
	    "%s CA %s via %s", key->cert->key_id,
	    (unsigned long long)key->cert->serial,
	    sshkey_type(key->cert->signature_key), ca_fp,
	    options.trusted_user_ca_keys);
	if (authoptsp != NULL) {
		*authoptsp = final_opts;
		final_opts = NULL;
	}
	ret = 1;
 out:
	sshauthopt_free(principals_opts);
	sshauthopt_free(cert_opts);
	sshauthopt_free(final_opts);
	free(principals_file);
	free(ca_fp);
	return ret;
}

/*
 * Checks whether key is allowed in file.
 * returns 1 if the key is allowed or 0 otherwise.
 */
static int
user_key_allowed2(struct ssh *ssh, struct passwd *pw, struct sshkey *key,
    char *file, struct sshauthopt **authoptsp)
{
	FILE *f;
	int found_key = 0;

	if (authoptsp != NULL)
		*authoptsp = NULL;

	/* Temporarily use the user's uid. */
	temporarily_use_uid(pw);

	debug("trying public key file %s", file);
	if ((f = auth_openkeyfile(file, pw, options.strict_modes)) != NULL) {
		found_key = check_authkeys_file(ssh, pw, f, file,
		    key, authoptsp);
		fclose(f);
	}

	restore_uid();
	return found_key;
}

/*
 * Checks whether key is allowed in output of command.
 * returns 1 if the key is allowed or 0 otherwise.
 */
static int
user_key_command_allowed2(struct ssh *ssh, struct passwd *user_pw,
    struct sshkey *key, struct sshauthopt **authoptsp)
{
	struct passwd *runas_pw = NULL;
	FILE *f = NULL;
	int r, ok, found_key = 0;
	int i, uid_swapped = 0, ac = 0;
	pid_t pid;
	char *username = NULL, *key_fp = NULL, *keytext = NULL;
	char uidstr[32], *tmp, *command = NULL, **av = NULL;
	void (*osigchld)(int);

	if (authoptsp != NULL)
		*authoptsp = NULL;
	if (options.authorized_keys_command == NULL)
		return 0;
	if (options.authorized_keys_command_user == NULL) {
		error("No user for AuthorizedKeysCommand specified, skipping");
		return 0;
	}

	/*
	 * NB. all returns later this function should go via "out" to
	 * ensure the original SIGCHLD handler is restored properly.
	 */
	osigchld = signal(SIGCHLD, SIG_DFL);

	/* Prepare and verify the user for the command */
	username = percent_expand(options.authorized_keys_command_user,
	    "u", user_pw->pw_name, (char *)NULL);
	runas_pw = getpwnam(username);
	if (runas_pw == NULL) {
		error("AuthorizedKeysCommandUser \"%s\" not found: %s",
		    username, strerror(errno));
		goto out;
	}

	/* Prepare AuthorizedKeysCommand */
	if ((key_fp = sshkey_fingerprint(key, options.fingerprint_hash,
	    SSH_FP_DEFAULT)) == NULL) {
		error("%s: sshkey_fingerprint failed", __func__);
		goto out;
	}
	if ((r = sshkey_to_base64(key, &keytext)) != 0) {
		error("%s: sshkey_to_base64 failed: %s", __func__, ssh_err(r));
		goto out;
	}

	/* Turn the command into an argument vector */
	if (argv_split(options.authorized_keys_command, &ac, &av) != 0) {
		error("AuthorizedKeysCommand \"%s\" contains invalid quotes",
		    command);
		goto out;
	}
	if (ac == 0) {
		error("AuthorizedKeysCommand \"%s\" yielded no arguments",
		    command);
		goto out;
	}
	snprintf(uidstr, sizeof(uidstr), "%llu",
	    (unsigned long long)user_pw->pw_uid);
	for (i = 1; i < ac; i++) {
		tmp = percent_expand(av[i],
		    "U", uidstr,
		    "u", user_pw->pw_name,
		    "h", user_pw->pw_dir,
		    "t", sshkey_ssh_name(key),
		    "f", key_fp,
		    "k", keytext,
		    (char *)NULL);
		if (tmp == NULL)
			fatal("%s: percent_expand failed", __func__);
		free(av[i]);
		av[i] = tmp;
	}
	/* Prepare a printable command for logs, etc. */
	command = argv_assemble(ac, av);

	/*
	 * If AuthorizedKeysCommand was run without arguments
	 * then fall back to the old behaviour of passing the
	 * target username as a single argument.
	 */
	if (ac == 1) {
		av = xreallocarray(av, ac + 2, sizeof(*av));
		av[1] = xstrdup(user_pw->pw_name);
		av[2] = NULL;
		/* Fix up command too, since it is used in log messages */
		free(command);
		xasprintf(&command, "%s %s", av[0], av[1]);
	}

	if ((pid = subprocess("AuthorizedKeysCommand", runas_pw, command,
	    ac, av, &f,
	    SSH_SUBPROCESS_STDOUT_CAPTURE|SSH_SUBPROCESS_STDERR_DISCARD)) == 0)
		goto out;

	uid_swapped = 1;
	temporarily_use_uid(runas_pw);

	ok = check_authkeys_file(ssh, user_pw, f,
	    options.authorized_keys_command, key, authoptsp);

	fclose(f);
	f = NULL;

	if (exited_cleanly(pid, "AuthorizedKeysCommand", command, 0) != 0)
		goto out;

	/* Read completed successfully */
	found_key = ok;
 out:
	if (f != NULL)
		fclose(f);
	signal(SIGCHLD, osigchld);
	for (i = 0; i < ac; i++)
		free(av[i]);
	free(av);
	if (uid_swapped)
		restore_uid();
	free(command);
	free(username);
	free(key_fp);
	free(keytext);
	return found_key;
}

/*
 * Check whether key authenticates and authorises the user.
 */
int
user_key_allowed(struct ssh *ssh, struct passwd *pw, struct sshkey *key,
    int auth_attempt, struct sshauthopt **authoptsp)
{
	u_int success, i;
	char *file;
	struct sshauthopt *opts = NULL;
	if (authoptsp != NULL)
		*authoptsp = NULL;

	if (auth_key_is_revoked(key))
		return 0;
	if (sshkey_is_cert(key) &&
	    auth_key_is_revoked(key->cert->signature_key))
		return 0;

	if ((success = user_cert_trusted_ca(ssh, pw, key, &opts)) != 0)
		goto out;
	sshauthopt_free(opts);
	opts = NULL;

	if ((success = user_key_command_allowed2(ssh, pw, key, &opts)) != 0)
		goto out;
	sshauthopt_free(opts);
	opts = NULL;

	for (i = 0; !success && i < options.num_authkeys_files; i++) {
		if (strcasecmp(options.authorized_keys_files[i], "none") == 0)
			continue;
		file = expand_authorized_keys(
		    options.authorized_keys_files[i], pw);
		success = user_key_allowed2(ssh, pw, key, file, &opts);
		free(file);
	}

 out:
	if (success && authoptsp != NULL) {
		*authoptsp = opts;
		opts = NULL;
	}
	sshauthopt_free(opts);
	return success;
}

Authmethod method_pubkey = {
	"publickey",
	userauth_pubkey,
	&options.pubkey_authentication
};
