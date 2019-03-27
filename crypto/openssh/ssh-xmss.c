/* $OpenBSD: ssh-xmss.c,v 1.1 2018/02/23 15:58:38 markus Exp $*/
/*
 * Copyright (c) 2017 Stefan-Lukas Gazdag.
 * Copyright (c) 2017 Markus Friedl.
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
#ifdef WITH_XMSS

#define SSHKEY_INTERNAL
#include <sys/types.h>
#include <limits.h>

#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include "log.h"
#include "sshbuf.h"
#include "sshkey.h"
#include "sshkey-xmss.h"
#include "ssherr.h"
#include "ssh.h"

#include "xmss_fast.h"

int
ssh_xmss_sign(const struct sshkey *key, u_char **sigp, size_t *lenp,
    const u_char *data, size_t datalen, u_int compat)
{
	u_char *sig = NULL;
	size_t slen = 0, len = 0, required_siglen;
	unsigned long long smlen;
	int r, ret;
	struct sshbuf *b = NULL;

	if (lenp != NULL)
		*lenp = 0;
	if (sigp != NULL)
		*sigp = NULL;

	if (key == NULL ||
	    sshkey_type_plain(key->type) != KEY_XMSS ||
	    key->xmss_sk == NULL ||
	    sshkey_xmss_params(key) == NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	if ((r = sshkey_xmss_siglen(key, &required_siglen)) != 0)
		return r;
	if (datalen >= INT_MAX - required_siglen)
		return SSH_ERR_INVALID_ARGUMENT;
	smlen = slen = datalen + required_siglen;
	if ((sig = malloc(slen)) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	if ((r = sshkey_xmss_get_state(key, error)) != 0)
		goto out;
	if ((ret = xmss_sign(key->xmss_sk, sshkey_xmss_bds_state(key), sig, &smlen,
	    data, datalen, sshkey_xmss_params(key))) != 0 || smlen <= datalen) {
		r = SSH_ERR_INVALID_ARGUMENT; /* XXX better error? */
		goto out;
	}
	/* encode signature */
	if ((b = sshbuf_new()) == NULL) {
		r = SSH_ERR_ALLOC_FAIL;
		goto out;
	}
	if ((r = sshbuf_put_cstring(b, "ssh-xmss@openssh.com")) != 0 ||
	    (r = sshbuf_put_string(b, sig, smlen - datalen)) != 0)
		goto out;
	len = sshbuf_len(b);
	if (sigp != NULL) {
		if ((*sigp = malloc(len)) == NULL) {
			r = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		memcpy(*sigp, sshbuf_ptr(b), len);
	}
	if (lenp != NULL)
		*lenp = len;
	/* success */
	r = 0;
 out:
	if ((ret = sshkey_xmss_update_state(key, error)) != 0) {
		/* discard signature since we cannot update the state */
		if (r == 0 && sigp != NULL && *sigp != NULL) {
			explicit_bzero(*sigp, len);
			free(*sigp);
		}
		if (sigp != NULL)
			*sigp = NULL;
		if (lenp != NULL)
			*lenp = 0;
		r = ret;
	}
	sshbuf_free(b);
	if (sig != NULL) {
		explicit_bzero(sig, slen);
		free(sig);
	}

	return r;
}

int
ssh_xmss_verify(const struct sshkey *key,
    const u_char *signature, size_t signaturelen,
    const u_char *data, size_t datalen, u_int compat)
{
	struct sshbuf *b = NULL;
	char *ktype = NULL;
	const u_char *sigblob;
	u_char *sm = NULL, *m = NULL;
	size_t len, required_siglen;
	unsigned long long smlen = 0, mlen = 0;
	int r, ret;

	if (key == NULL ||
	    sshkey_type_plain(key->type) != KEY_XMSS ||
	    key->xmss_pk == NULL ||
	    sshkey_xmss_params(key) == NULL ||
	    signature == NULL || signaturelen == 0)
		return SSH_ERR_INVALID_ARGUMENT;
	if ((r = sshkey_xmss_siglen(key, &required_siglen)) != 0)
		return r;
	if (datalen >= INT_MAX - required_siglen)
		return SSH_ERR_INVALID_ARGUMENT;

	if ((b = sshbuf_from(signature, signaturelen)) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	if ((r = sshbuf_get_cstring(b, &ktype, NULL)) != 0 ||
	    (r = sshbuf_get_string_direct(b, &sigblob, &len)) != 0)
		goto out;
	if (strcmp("ssh-xmss@openssh.com", ktype) != 0) {
		r = SSH_ERR_KEY_TYPE_MISMATCH;
		goto out;
	}
	if (sshbuf_len(b) != 0) {
		r = SSH_ERR_UNEXPECTED_TRAILING_DATA;
		goto out;
	}
	if (len != required_siglen) {
		r = SSH_ERR_INVALID_FORMAT;
		goto out;
	}
	if (datalen >= SIZE_MAX - len) {
		r = SSH_ERR_INVALID_ARGUMENT;
		goto out;
	}
	smlen = len + datalen;
	mlen = smlen;
	if ((sm = malloc(smlen)) == NULL || (m = malloc(mlen)) == NULL) {
		r = SSH_ERR_ALLOC_FAIL;
		goto out;
	}
	memcpy(sm, sigblob, len);
	memcpy(sm+len, data, datalen);
	if ((ret = xmss_sign_open(m, &mlen, sm, smlen,
	    key->xmss_pk, sshkey_xmss_params(key))) != 0) {
		debug2("%s: crypto_sign_xmss_open failed: %d",
		    __func__, ret);
	}
	if (ret != 0 || mlen != datalen) {
		r = SSH_ERR_SIGNATURE_INVALID;
		goto out;
	}
	/* XXX compare 'm' and 'data' ? */
	/* success */
	r = 0;
 out:
	if (sm != NULL) {
		explicit_bzero(sm, smlen);
		free(sm);
	}
	if (m != NULL) {
		explicit_bzero(m, smlen); /* NB mlen may be invalid if r != 0 */
		free(m);
	}
	sshbuf_free(b);
	free(ktype);
	return r;
}
#endif /* WITH_XMSS */
