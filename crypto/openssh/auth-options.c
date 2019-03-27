/* $OpenBSD: auth-options.c,v 1.83 2018/06/19 02:59:41 djm Exp $ */
/*
 * Copyright (c) 2018 Damien Miller <djm@mindrot.org>
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

#include "includes.h"

#include <sys/types.h>

#include <netdb.h>
#include <pwd.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <limits.h>

#include "openbsd-compat/sys-queue.h"

#include "xmalloc.h"
#include "ssherr.h"
#include "log.h"
#include "sshbuf.h"
#include "misc.h"
#include "sshkey.h"
#include "match.h"
#include "ssh2.h"
#include "auth-options.h"

/*
 * Match flag 'opt' in *optsp, and if allow_negate is set then also match
 * 'no-opt'. Returns -1 if option not matched, 1 if option matches or 0
 * if negated option matches.
 * If the option or negated option matches, then *optsp is updated to
 * point to the first character after the option.
 */
static int
opt_flag(const char *opt, int allow_negate, const char **optsp)
{
	size_t opt_len = strlen(opt);
	const char *opts = *optsp;
	int negate = 0;

	if (allow_negate && strncasecmp(opts, "no-", 3) == 0) {
		opts += 3;
		negate = 1;
	}
	if (strncasecmp(opts, opt, opt_len) == 0) {
		*optsp = opts + opt_len;
		return negate ? 0 : 1;
	}
	return -1;
}

static char *
opt_dequote(const char **sp, const char **errstrp)
{
	const char *s = *sp;
	char *ret;
	size_t i;

	*errstrp = NULL;
	if (*s != '"') {
		*errstrp = "missing start quote";
		return NULL;
	}
	s++;
	if ((ret = malloc(strlen((s)) + 1)) == NULL) {
		*errstrp = "memory allocation failed";
		return NULL;
	}
	for (i = 0; *s != '\0' && *s != '"';) {
		if (s[0] == '\\' && s[1] == '"')
			s++;
		ret[i++] = *s++;
	}
	if (*s == '\0') {
		*errstrp = "missing end quote";
		free(ret);
		return NULL;
	}
	ret[i] = '\0';
	s++;
	*sp = s;
	return ret;
}

static int
opt_match(const char **opts, const char *term)
{
	if (strncasecmp((*opts), term, strlen(term)) == 0 &&
	    (*opts)[strlen(term)] == '=') {
		*opts += strlen(term) + 1;
		return 1;
	}
	return 0;
}

static int
dup_strings(char ***dstp, size_t *ndstp, char **src, size_t nsrc)
{
	char **dst;
	size_t i, j;

	*dstp = NULL;
	*ndstp = 0;
	if (nsrc == 0)
		return 0;

	if ((dst = calloc(nsrc, sizeof(*src))) == NULL)
		return -1;
	for (i = 0; i < nsrc; i++) {
		if ((dst[i] = strdup(src[i])) == NULL) {
			for (j = 0; j < i; j++)
				free(dst[j]);
			free(dst);
			return -1;
		}
	}
	/* success */
	*dstp = dst;
	*ndstp = nsrc;
	return 0;
}

#define OPTIONS_CRITICAL	1
#define OPTIONS_EXTENSIONS	2
static int
cert_option_list(struct sshauthopt *opts, struct sshbuf *oblob,
    u_int which, int crit)
{
	char *command, *allowed;
	char *name = NULL;
	struct sshbuf *c = NULL, *data = NULL;
	int r, ret = -1, found;

	if ((c = sshbuf_fromb(oblob)) == NULL) {
		error("%s: sshbuf_fromb failed", __func__);
		goto out;
	}

	while (sshbuf_len(c) > 0) {
		sshbuf_free(data);
		data = NULL;
		if ((r = sshbuf_get_cstring(c, &name, NULL)) != 0 ||
		    (r = sshbuf_froms(c, &data)) != 0) {
			error("Unable to parse certificate options: %s",
			    ssh_err(r));
			goto out;
		}
		debug3("found certificate option \"%.100s\" len %zu",
		    name, sshbuf_len(data));
		found = 0;
		if ((which & OPTIONS_EXTENSIONS) != 0) {
			if (strcmp(name, "permit-X11-forwarding") == 0) {
				opts->permit_x11_forwarding_flag = 1;
				found = 1;
			} else if (strcmp(name,
			    "permit-agent-forwarding") == 0) {
				opts->permit_agent_forwarding_flag = 1;
				found = 1;
			} else if (strcmp(name,
			    "permit-port-forwarding") == 0) {
				opts->permit_port_forwarding_flag = 1;
				found = 1;
			} else if (strcmp(name, "permit-pty") == 0) {
				opts->permit_pty_flag = 1;
				found = 1;
			} else if (strcmp(name, "permit-user-rc") == 0) {
				opts->permit_user_rc = 1;
				found = 1;
			}
		}
		if (!found && (which & OPTIONS_CRITICAL) != 0) {
			if (strcmp(name, "force-command") == 0) {
				if ((r = sshbuf_get_cstring(data, &command,
				    NULL)) != 0) {
					error("Unable to parse \"%s\" "
					    "section: %s", name, ssh_err(r));
					goto out;
				}
				if (opts->force_command != NULL) {
					error("Certificate has multiple "
					    "force-command options");
					free(command);
					goto out;
				}
				opts->force_command = command;
				found = 1;
			}
			if (strcmp(name, "source-address") == 0) {
				if ((r = sshbuf_get_cstring(data, &allowed,
				    NULL)) != 0) {
					error("Unable to parse \"%s\" "
					    "section: %s", name, ssh_err(r));
					goto out;
				}
				if (opts->required_from_host_cert != NULL) {
					error("Certificate has multiple "
					    "source-address options");
					free(allowed);
					goto out;
				}
				/* Check syntax */
				if (addr_match_cidr_list(NULL, allowed) == -1) {
					error("Certificate source-address "
					    "contents invalid");
					goto out;
				}
				opts->required_from_host_cert = allowed;
				found = 1;
			}
		}

		if (!found) {
			if (crit) {
				error("Certificate critical option \"%s\" "
				    "is not supported", name);
				goto out;
			} else {
				logit("Certificate extension \"%s\" "
				    "is not supported", name);
			}
		} else if (sshbuf_len(data) != 0) {
			error("Certificate option \"%s\" corrupt "
			    "(extra data)", name);
			goto out;
		}
		free(name);
		name = NULL;
	}
	/* successfully parsed all options */
	ret = 0;

 out:
	free(name);
	sshbuf_free(data);
	sshbuf_free(c);
	return ret;
}

struct sshauthopt *
sshauthopt_new(void)
{
	struct sshauthopt *ret;

	if ((ret = calloc(1, sizeof(*ret))) == NULL)
		return NULL;
	ret->force_tun_device = -1;
	return ret;
}

void
sshauthopt_free(struct sshauthopt *opts)
{
	size_t i;

	if (opts == NULL)
		return;

	free(opts->cert_principals);
	free(opts->force_command);
	free(opts->required_from_host_cert);
	free(opts->required_from_host_keys);

	for (i = 0; i < opts->nenv; i++)
		free(opts->env[i]);
	free(opts->env);

	for (i = 0; i < opts->npermitopen; i++)
		free(opts->permitopen[i]);
	free(opts->permitopen);

	for (i = 0; i < opts->npermitlisten; i++)
		free(opts->permitlisten[i]);
	free(opts->permitlisten);

	explicit_bzero(opts, sizeof(*opts));
	free(opts);
}

struct sshauthopt *
sshauthopt_new_with_keys_defaults(void)
{
	struct sshauthopt *ret = NULL;

	if ((ret = sshauthopt_new()) == NULL)
		return NULL;

	/* Defaults for authorized_keys flags */
	ret->permit_port_forwarding_flag = 1;
	ret->permit_agent_forwarding_flag = 1;
	ret->permit_x11_forwarding_flag = 1;
	ret->permit_pty_flag = 1;
	ret->permit_user_rc = 1;
	return ret;
}

/*
 * Parse and record a permitopen/permitlisten directive.
 * Return 0 on success. Return -1 on failure and sets *errstrp to error reason.
 */
static int
handle_permit(const char **optsp, int allow_bare_port,
    char ***permitsp, size_t *npermitsp, const char **errstrp)
{
	char *opt, *tmp, *cp, *host, **permits = *permitsp;
	size_t npermits = *npermitsp;
	const char *errstr = "unknown error";

	if (npermits > INT_MAX) {
		*errstrp = "too many permission directives";
		return -1;
	}
	if ((opt = opt_dequote(optsp, &errstr)) == NULL) {
		return -1;
	}
	if (allow_bare_port && strchr(opt, ':') == NULL) {
		/*
		 * Allow a bare port number in permitlisten to indicate a
		 * listen_host wildcard.
		 */
		if (asprintf(&tmp, "*:%s", opt) < 0) {
			*errstrp = "memory allocation failed";
			return -1;
		}
		free(opt);
		opt = tmp;
	}
	if ((tmp = strdup(opt)) == NULL) {
		free(opt);
		*errstrp = "memory allocation failed";
		return -1;
	}
	cp = tmp;
	/* validate syntax before recording it. */
	host = hpdelim(&cp);
	if (host == NULL || strlen(host) >= NI_MAXHOST) {
		free(tmp);
		free(opt);
		*errstrp = "invalid permission hostname";
		return -1;
	}
	/*
	 * don't want to use permitopen_port to avoid
	 * dependency on channels.[ch] here.
	 */
	if (cp == NULL ||
	    (strcmp(cp, "*") != 0 && a2port(cp) <= 0)) {
		free(tmp);
		free(opt);
		*errstrp = "invalid permission port";
		return -1;
	}
	/* XXX - add streamlocal support */
	free(tmp);
	/* Record it */
	if ((permits = recallocarray(permits, npermits, npermits + 1,
	    sizeof(*permits))) == NULL) {
		free(opt);
		/* NB. don't update *permitsp if alloc fails */
		*errstrp = "memory allocation failed";
		return -1;
	}
	permits[npermits++] = opt;
	*permitsp = permits;
	*npermitsp = npermits;
	return 0;
}

struct sshauthopt *
sshauthopt_parse(const char *opts, const char **errstrp)
{
	char **oarray, *opt, *cp, *tmp;
	int r;
	struct sshauthopt *ret = NULL;
	const char *errstr = "unknown error";
	uint64_t valid_before;

	if (errstrp != NULL)
		*errstrp = NULL;
	if ((ret = sshauthopt_new_with_keys_defaults()) == NULL)
		goto alloc_fail;

	if (opts == NULL)
		return ret;

	while (*opts && *opts != ' ' && *opts != '\t') {
		/* flag options */
		if ((r = opt_flag("restrict", 0, &opts)) != -1) {
			ret->restricted = 1;
			ret->permit_port_forwarding_flag = 0;
			ret->permit_agent_forwarding_flag = 0;
			ret->permit_x11_forwarding_flag = 0;
			ret->permit_pty_flag = 0;
			ret->permit_user_rc = 0;
		} else if ((r = opt_flag("cert-authority", 0, &opts)) != -1) {
			ret->cert_authority = r;
		} else if ((r = opt_flag("port-forwarding", 1, &opts)) != -1) {
			ret->permit_port_forwarding_flag = r == 1;
		} else if ((r = opt_flag("agent-forwarding", 1, &opts)) != -1) {
			ret->permit_agent_forwarding_flag = r == 1;
		} else if ((r = opt_flag("x11-forwarding", 1, &opts)) != -1) {
			ret->permit_x11_forwarding_flag = r == 1;
		} else if ((r = opt_flag("pty", 1, &opts)) != -1) {
			ret->permit_pty_flag = r == 1;
		} else if ((r = opt_flag("user-rc", 1, &opts)) != -1) {
			ret->permit_user_rc = r == 1;
		} else if (opt_match(&opts, "command")) {
			if (ret->force_command != NULL) {
				errstr = "multiple \"command\" clauses";
				goto fail;
			}
			ret->force_command = opt_dequote(&opts, &errstr);
			if (ret->force_command == NULL)
				goto fail;
		} else if (opt_match(&opts, "principals")) {
			if (ret->cert_principals != NULL) {
				errstr = "multiple \"principals\" clauses";
				goto fail;
			}
			ret->cert_principals = opt_dequote(&opts, &errstr);
			if (ret->cert_principals == NULL)
				goto fail;
		} else if (opt_match(&opts, "from")) {
			if (ret->required_from_host_keys != NULL) {
				errstr = "multiple \"from\" clauses";
				goto fail;
			}
			ret->required_from_host_keys = opt_dequote(&opts,
			    &errstr);
			if (ret->required_from_host_keys == NULL)
				goto fail;
		} else if (opt_match(&opts, "expiry-time")) {
			if ((opt = opt_dequote(&opts, &errstr)) == NULL)
				goto fail;
			if (parse_absolute_time(opt, &valid_before) != 0 ||
			    valid_before == 0) {
				free(opt);
				errstr = "invalid expires time";
				goto fail;
			}
			free(opt);
			if (ret->valid_before == 0 ||
			    valid_before < ret->valid_before)
				ret->valid_before = valid_before;
		} else if (opt_match(&opts, "environment")) {
			if (ret->nenv > INT_MAX) {
				errstr = "too many environment strings";
				goto fail;
			}
			if ((opt = opt_dequote(&opts, &errstr)) == NULL)
				goto fail;
			/* env name must be alphanumeric and followed by '=' */
			if ((tmp = strchr(opt, '=')) == NULL) {
				free(opt);
				errstr = "invalid environment string";
				goto fail;
			}
			for (cp = opt; cp < tmp; cp++) {
				if (!isalnum((u_char)*cp) && *cp != '_') {
					free(opt);
					errstr = "invalid environment string";
					goto fail;
				}
			}
			/* Append it. */
			oarray = ret->env;
			if ((ret->env = recallocarray(ret->env, ret->nenv,
			    ret->nenv + 1, sizeof(*ret->env))) == NULL) {
				free(opt);
				ret->env = oarray; /* put it back for cleanup */
				goto alloc_fail;
			}
			ret->env[ret->nenv++] = opt;
		} else if (opt_match(&opts, "permitopen")) {
			if (handle_permit(&opts, 0, &ret->permitopen,
			    &ret->npermitopen, &errstr) != 0)
				goto fail;
		} else if (opt_match(&opts, "permitlisten")) {
			if (handle_permit(&opts, 1, &ret->permitlisten,
			    &ret->npermitlisten, &errstr) != 0)
				goto fail;
		} else if (opt_match(&opts, "tunnel")) {
			if ((opt = opt_dequote(&opts, &errstr)) == NULL)
				goto fail;
			ret->force_tun_device = a2tun(opt, NULL);
			free(opt);
			if (ret->force_tun_device == SSH_TUNID_ERR) {
				errstr = "invalid tun device";
				goto fail;
			}
		}
		/*
		 * Skip the comma, and move to the next option
		 * (or break out if there are no more).
		 */
		if (*opts == '\0' || *opts == ' ' || *opts == '\t')
			break;		/* End of options. */
		/* Anything other than a comma is an unknown option */
		if (*opts != ',') {
			errstr = "unknown key option";
			goto fail;
		}
		opts++;
		if (*opts == '\0') {
			errstr = "unexpected end-of-options";
			goto fail;
		}
	}

	/* success */
	if (errstrp != NULL)
		*errstrp = NULL;
	return ret;

alloc_fail:
	errstr = "memory allocation failed";
fail:
	sshauthopt_free(ret);
	if (errstrp != NULL)
		*errstrp = errstr;
	return NULL;
}

struct sshauthopt *
sshauthopt_from_cert(struct sshkey *k)
{
	struct sshauthopt *ret;

	if (k == NULL || !sshkey_type_is_cert(k->type) || k->cert == NULL ||
	    k->cert->type != SSH2_CERT_TYPE_USER)
		return NULL;

	if ((ret = sshauthopt_new()) == NULL)
		return NULL;

	/* Handle options and critical extensions separately */
	if (cert_option_list(ret, k->cert->critical,
	    OPTIONS_CRITICAL, 1) == -1) {
		sshauthopt_free(ret);
		return NULL;
	}
	if (cert_option_list(ret, k->cert->extensions,
	    OPTIONS_EXTENSIONS, 0) == -1) {
		sshauthopt_free(ret);
		return NULL;
	}
	/* success */
	return ret;
}

/*
 * Merges "additional" options to "primary" and returns the result.
 * NB. Some options from primary have primacy.
 */
struct sshauthopt *
sshauthopt_merge(const struct sshauthopt *primary,
    const struct sshauthopt *additional, const char **errstrp)
{
	struct sshauthopt *ret;
	const char *errstr = "internal error";
	const char *tmp;

	if (errstrp != NULL)
		*errstrp = NULL;

	if ((ret = sshauthopt_new()) == NULL)
		goto alloc_fail;

	/* cert_authority and cert_principals are cleared in result */

	/* Prefer access lists from primary. */
	/* XXX err is both set and mismatch? */
	tmp = primary->required_from_host_cert;
	if (tmp == NULL)
		tmp = additional->required_from_host_cert;
	if (tmp != NULL && (ret->required_from_host_cert = strdup(tmp)) == NULL)
		goto alloc_fail;
	tmp = primary->required_from_host_keys;
	if (tmp == NULL)
		tmp = additional->required_from_host_keys;
	if (tmp != NULL && (ret->required_from_host_keys = strdup(tmp)) == NULL)
		goto alloc_fail;

	/*
	 * force_tun_device, permitopen/permitlisten and environment all
	 * prefer the primary.
	 */
	ret->force_tun_device = primary->force_tun_device;
	if (ret->force_tun_device == -1)
		ret->force_tun_device = additional->force_tun_device;
	if (primary->nenv > 0) {
		if (dup_strings(&ret->env, &ret->nenv,
		    primary->env, primary->nenv) != 0)
			goto alloc_fail;
	} else if (additional->nenv) {
		if (dup_strings(&ret->env, &ret->nenv,
		    additional->env, additional->nenv) != 0)
			goto alloc_fail;
	}
	if (primary->npermitopen > 0) {
		if (dup_strings(&ret->permitopen, &ret->npermitopen,
		    primary->permitopen, primary->npermitopen) != 0)
			goto alloc_fail;
	} else if (additional->npermitopen > 0) {
		if (dup_strings(&ret->permitopen, &ret->npermitopen,
		    additional->permitopen, additional->npermitopen) != 0)
			goto alloc_fail;
	}

	if (primary->npermitlisten > 0) {
		if (dup_strings(&ret->permitlisten, &ret->npermitlisten,
		    primary->permitlisten, primary->npermitlisten) != 0)
			goto alloc_fail;
	} else if (additional->npermitlisten > 0) {
		if (dup_strings(&ret->permitlisten, &ret->npermitlisten,
		    additional->permitlisten, additional->npermitlisten) != 0)
			goto alloc_fail;
	}

	/* Flags are logical-AND (i.e. must be set in both for permission) */
#define OPTFLAG(x) ret->x = (primary->x == 1) && (additional->x == 1)
	OPTFLAG(permit_port_forwarding_flag);
	OPTFLAG(permit_agent_forwarding_flag);
	OPTFLAG(permit_x11_forwarding_flag);
	OPTFLAG(permit_pty_flag);
	OPTFLAG(permit_user_rc);
#undef OPTFLAG

	/* Earliest expiry time should win */
	if (primary->valid_before != 0)
		ret->valid_before = primary->valid_before;
	if (additional->valid_before != 0 &&
	    additional->valid_before < ret->valid_before)
		ret->valid_before = additional->valid_before;

	/*
	 * When both multiple forced-command are specified, only
	 * proceed if they are identical, otherwise fail.
	 */
	if (primary->force_command != NULL &&
	    additional->force_command != NULL) {
		if (strcmp(primary->force_command,
		    additional->force_command) == 0) {
			/* ok */
			ret->force_command = strdup(primary->force_command);
			if (ret->force_command == NULL)
				goto alloc_fail;
		} else {
			errstr = "forced command options do not match";
			goto fail;
		}
	} else if (primary->force_command != NULL) {
		if ((ret->force_command = strdup(
		    primary->force_command)) == NULL)
			goto alloc_fail;
	} else if (additional->force_command != NULL) {
		if ((ret->force_command = strdup(
		    additional->force_command)) == NULL)
			goto alloc_fail;
	}
	/* success */
	if (errstrp != NULL)
		*errstrp = NULL;
	return ret;

 alloc_fail:
	errstr = "memory allocation failed";
 fail:
	if (errstrp != NULL)
		*errstrp = errstr;
	sshauthopt_free(ret);
	return NULL;
}

/*
 * Copy options
 */
struct sshauthopt *
sshauthopt_copy(const struct sshauthopt *orig)
{
	struct sshauthopt *ret;

	if ((ret = sshauthopt_new()) == NULL)
		return NULL;

#define OPTSCALAR(x) ret->x = orig->x
	OPTSCALAR(permit_port_forwarding_flag);
	OPTSCALAR(permit_agent_forwarding_flag);
	OPTSCALAR(permit_x11_forwarding_flag);
	OPTSCALAR(permit_pty_flag);
	OPTSCALAR(permit_user_rc);
	OPTSCALAR(restricted);
	OPTSCALAR(cert_authority);
	OPTSCALAR(force_tun_device);
	OPTSCALAR(valid_before);
#undef OPTSCALAR
#define OPTSTRING(x) \
	do { \
		if (orig->x != NULL && (ret->x = strdup(orig->x)) == NULL) { \
			sshauthopt_free(ret); \
			return NULL; \
		} \
	} while (0)
	OPTSTRING(cert_principals);
	OPTSTRING(force_command);
	OPTSTRING(required_from_host_cert);
	OPTSTRING(required_from_host_keys);
#undef OPTSTRING

	if (dup_strings(&ret->env, &ret->nenv, orig->env, orig->nenv) != 0 ||
	    dup_strings(&ret->permitopen, &ret->npermitopen,
	    orig->permitopen, orig->npermitopen) != 0 ||
	    dup_strings(&ret->permitlisten, &ret->npermitlisten,
	    orig->permitlisten, orig->npermitlisten) != 0) {
		sshauthopt_free(ret);
		return NULL;
	}
	return ret;
}

static int
serialise_array(struct sshbuf *m, char **a, size_t n)
{
	struct sshbuf *b;
	size_t i;
	int r;

	if (n > INT_MAX)
		return SSH_ERR_INTERNAL_ERROR;

	if ((b = sshbuf_new()) == NULL) {
		return SSH_ERR_ALLOC_FAIL;
	}
	for (i = 0; i < n; i++) {
		if ((r = sshbuf_put_cstring(b, a[i])) != 0) {
			sshbuf_free(b);
			return r;
		}
	}
	if ((r = sshbuf_put_u32(m, n)) != 0 ||
	    (r = sshbuf_put_stringb(m, b)) != 0) {
		sshbuf_free(b);
		return r;
	}
	/* success */
	return 0;
}

static int
deserialise_array(struct sshbuf *m, char ***ap, size_t *np)
{
	char **a = NULL;
	size_t i, n = 0;
	struct sshbuf *b = NULL;
	u_int tmp;
	int r = SSH_ERR_INTERNAL_ERROR;

	if ((r = sshbuf_get_u32(m, &tmp)) != 0 ||
	    (r = sshbuf_froms(m, &b)) != 0)
		goto out;
	if (tmp > INT_MAX) {
		r = SSH_ERR_INVALID_FORMAT;
		goto out;
	}
	n = tmp;
	if (n > 0 && (a = calloc(n, sizeof(*a))) == NULL) {
		r = SSH_ERR_ALLOC_FAIL;
		goto out;
	}
	for (i = 0; i < n; i++) {
		if ((r = sshbuf_get_cstring(b, &a[i], NULL)) != 0)
			goto out;
	}
	/* success */
	r = 0;
	*ap = a;
	a = NULL;
	*np = n;
	n = 0;
 out:
	for (i = 0; i < n; i++)
		free(a[i]);
	free(a);
	sshbuf_free(b);
	return r;
}

static int
serialise_nullable_string(struct sshbuf *m, const char *s)
{
	int r;

	if ((r = sshbuf_put_u8(m, s == NULL)) != 0 ||
	    (r = sshbuf_put_cstring(m, s)) != 0)
		return r;
	return 0;
}

static int
deserialise_nullable_string(struct sshbuf *m, char **sp)
{
	int r;
	u_char flag;

	*sp = NULL;
	if ((r = sshbuf_get_u8(m, &flag)) != 0 ||
	    (r = sshbuf_get_cstring(m, flag ? NULL : sp, NULL)) != 0)
		return r;
	return 0;
}

int
sshauthopt_serialise(const struct sshauthopt *opts, struct sshbuf *m,
    int untrusted)
{
	int r = SSH_ERR_INTERNAL_ERROR;

	/* Flag and simple integer options */
	if ((r = sshbuf_put_u8(m, opts->permit_port_forwarding_flag)) != 0 ||
	    (r = sshbuf_put_u8(m, opts->permit_agent_forwarding_flag)) != 0 ||
	    (r = sshbuf_put_u8(m, opts->permit_x11_forwarding_flag)) != 0 ||
	    (r = sshbuf_put_u8(m, opts->permit_pty_flag)) != 0 ||
	    (r = sshbuf_put_u8(m, opts->permit_user_rc)) != 0 ||
	    (r = sshbuf_put_u8(m, opts->restricted)) != 0 ||
	    (r = sshbuf_put_u8(m, opts->cert_authority)) != 0 ||
	    (r = sshbuf_put_u64(m, opts->valid_before)) != 0)
		return r;

	/* tunnel number can be negative to indicate "unset" */
	if ((r = sshbuf_put_u8(m, opts->force_tun_device == -1)) != 0 ||
	    (r = sshbuf_put_u32(m, (opts->force_tun_device < 0) ?
	    0 : (u_int)opts->force_tun_device)) != 0)
		return r;

	/* String options; these may be NULL */
	if ((r = serialise_nullable_string(m,
	    untrusted ? "yes" : opts->cert_principals)) != 0 ||
	    (r = serialise_nullable_string(m,
	    untrusted ? "true" : opts->force_command)) != 0 ||
	    (r = serialise_nullable_string(m,
	    untrusted ? NULL : opts->required_from_host_cert)) != 0 ||
	    (r = serialise_nullable_string(m,
	     untrusted ? NULL : opts->required_from_host_keys)) != 0)
		return r;

	/* Array options */
	if ((r = serialise_array(m, opts->env,
	    untrusted ? 0 : opts->nenv)) != 0 ||
	    (r = serialise_array(m, opts->permitopen,
	    untrusted ? 0 : opts->npermitopen)) != 0 ||
	    (r = serialise_array(m, opts->permitlisten,
	    untrusted ? 0 : opts->npermitlisten)) != 0)
		return r;

	/* success */
	return 0;
}

int
sshauthopt_deserialise(struct sshbuf *m, struct sshauthopt **optsp)
{
	struct sshauthopt *opts = NULL;
	int r = SSH_ERR_INTERNAL_ERROR;
	u_char f;
	u_int tmp;

	if ((opts = calloc(1, sizeof(*opts))) == NULL)
		return SSH_ERR_ALLOC_FAIL;

#define OPT_FLAG(x) \
	do { \
		if ((r = sshbuf_get_u8(m, &f)) != 0) \
			goto out; \
		opts->x = f; \
	} while (0)
	OPT_FLAG(permit_port_forwarding_flag);
	OPT_FLAG(permit_agent_forwarding_flag);
	OPT_FLAG(permit_x11_forwarding_flag);
	OPT_FLAG(permit_pty_flag);
	OPT_FLAG(permit_user_rc);
	OPT_FLAG(restricted);
	OPT_FLAG(cert_authority);
#undef OPT_FLAG

	if ((r = sshbuf_get_u64(m, &opts->valid_before)) != 0)
		goto out;

	/* tunnel number can be negative to indicate "unset" */
	if ((r = sshbuf_get_u8(m, &f)) != 0 ||
	    (r = sshbuf_get_u32(m, &tmp)) != 0)
		goto out;
	opts->force_tun_device = f ? -1 : (int)tmp;

	/* String options may be NULL */
	if ((r = deserialise_nullable_string(m, &opts->cert_principals)) != 0 ||
	    (r = deserialise_nullable_string(m, &opts->force_command)) != 0 ||
	    (r = deserialise_nullable_string(m,
	    &opts->required_from_host_cert)) != 0 ||
	    (r = deserialise_nullable_string(m,
	    &opts->required_from_host_keys)) != 0)
		goto out;

	/* Array options */
	if ((r = deserialise_array(m, &opts->env, &opts->nenv)) != 0 ||
	    (r = deserialise_array(m,
	    &opts->permitopen, &opts->npermitopen)) != 0 ||
	    (r = deserialise_array(m,
	    &opts->permitlisten, &opts->npermitlisten)) != 0)
		goto out;

	/* success */
	r = 0;
	*optsp = opts;
	opts = NULL;
 out:
	sshauthopt_free(opts);
	return r;
}
