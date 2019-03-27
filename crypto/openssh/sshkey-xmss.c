/* $OpenBSD: sshkey-xmss.c,v 1.3 2018/07/09 21:59:10 markus Exp $ */
/*
 * Copyright (c) 2017 Markus Friedl.  All rights reserved.
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
#ifdef WITH_XMSS

#include <sys/types.h>
#include <sys/uio.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#ifdef HAVE_SYS_FILE_H
# include <sys/file.h>
#endif

#include "ssh2.h"
#include "ssherr.h"
#include "sshbuf.h"
#include "cipher.h"
#include "sshkey.h"
#include "sshkey-xmss.h"
#include "atomicio.h"

#include "xmss_fast.h"

/* opaque internal XMSS state */
#define XMSS_MAGIC		"xmss-state-v1"
#define XMSS_CIPHERNAME		"aes256-gcm@openssh.com"
struct ssh_xmss_state {
	xmss_params	params;
	u_int32_t	n, w, h, k;

	bds_state	bds;
	u_char		*stack;
	u_int32_t	stackoffset;
	u_char		*stacklevels;
	u_char		*auth;
	u_char		*keep;
	u_char		*th_nodes;
	u_char		*retain;
	treehash_inst	*treehash;

	u_int32_t	idx;		/* state read from file */
	u_int32_t	maxidx;		/* restricted # of signatures */
	int		have_state;	/* .state file exists */
	int		lockfd;		/* locked in sshkey_xmss_get_state() */
	int		allow_update;	/* allow sshkey_xmss_update_state() */
	char		*enc_ciphername;/* encrypt state with cipher */
	u_char		*enc_keyiv;	/* encrypt state with key */
	u_int32_t	enc_keyiv_len;	/* length of enc_keyiv */
};

int	 sshkey_xmss_init_bds_state(struct sshkey *);
int	 sshkey_xmss_init_enc_key(struct sshkey *, const char *);
void	 sshkey_xmss_free_bds(struct sshkey *);
int	 sshkey_xmss_get_state_from_file(struct sshkey *, const char *,
	    int *, sshkey_printfn *);
int	 sshkey_xmss_encrypt_state(const struct sshkey *, struct sshbuf *,
	    struct sshbuf **);
int	 sshkey_xmss_decrypt_state(const struct sshkey *, struct sshbuf *,
	    struct sshbuf **);
int	 sshkey_xmss_serialize_enc_key(const struct sshkey *, struct sshbuf *);
int	 sshkey_xmss_deserialize_enc_key(struct sshkey *, struct sshbuf *);

#define PRINT(s...) do { if (pr) pr(s); } while (0)

int
sshkey_xmss_init(struct sshkey *key, const char *name)
{
	struct ssh_xmss_state *state;

	if (key->xmss_state != NULL)
		return SSH_ERR_INVALID_FORMAT;
	if (name == NULL)
		return SSH_ERR_INVALID_FORMAT;
	state = calloc(sizeof(struct ssh_xmss_state), 1);
	if (state == NULL)
		return SSH_ERR_ALLOC_FAIL;
	if (strcmp(name, XMSS_SHA2_256_W16_H10_NAME) == 0) {
		state->n = 32;
		state->w = 16;
		state->h = 10;
	} else if (strcmp(name, XMSS_SHA2_256_W16_H16_NAME) == 0) {
		state->n = 32;
		state->w = 16;
		state->h = 16;
	} else if (strcmp(name, XMSS_SHA2_256_W16_H20_NAME) == 0) {
		state->n = 32;
		state->w = 16;
		state->h = 20;
	} else {
		free(state);
		return SSH_ERR_KEY_TYPE_UNKNOWN;
	}
	if ((key->xmss_name = strdup(name)) == NULL) {
		free(state);
		return SSH_ERR_ALLOC_FAIL;
	}
	state->k = 2;	/* XXX hardcoded */
	state->lockfd = -1;
	if (xmss_set_params(&state->params, state->n, state->h, state->w,
	    state->k) != 0) {
		free(state);
		return SSH_ERR_INVALID_FORMAT;
	}
	key->xmss_state = state;
	return 0;
}

void
sshkey_xmss_free_state(struct sshkey *key)
{
	struct ssh_xmss_state *state = key->xmss_state;

	sshkey_xmss_free_bds(key);
	if (state) {
		if (state->enc_keyiv) {
			explicit_bzero(state->enc_keyiv, state->enc_keyiv_len);
			free(state->enc_keyiv);
		}
		free(state->enc_ciphername);
		free(state);
	}
	key->xmss_state = NULL;
}

#define SSH_XMSS_K2_MAGIC	"k=2"
#define num_stack(x)		((x->h+1)*(x->n))
#define num_stacklevels(x)	(x->h+1)
#define num_auth(x)		((x->h)*(x->n))
#define num_keep(x)		((x->h >> 1)*(x->n))
#define num_th_nodes(x)		((x->h - x->k)*(x->n))
#define num_retain(x)		(((1ULL << x->k) - x->k - 1) * (x->n))
#define num_treehash(x)		((x->h) - (x->k))

int
sshkey_xmss_init_bds_state(struct sshkey *key)
{
	struct ssh_xmss_state *state = key->xmss_state;
	u_int32_t i;

	state->stackoffset = 0;
	if ((state->stack = calloc(num_stack(state), 1)) == NULL ||
	    (state->stacklevels = calloc(num_stacklevels(state), 1))== NULL ||
	    (state->auth = calloc(num_auth(state), 1)) == NULL ||
	    (state->keep = calloc(num_keep(state), 1)) == NULL ||
	    (state->th_nodes = calloc(num_th_nodes(state), 1)) == NULL ||
	    (state->retain = calloc(num_retain(state), 1)) == NULL ||
	    (state->treehash = calloc(num_treehash(state),
	    sizeof(treehash_inst))) == NULL) {
		sshkey_xmss_free_bds(key);
		return SSH_ERR_ALLOC_FAIL;
	}
	for (i = 0; i < state->h - state->k; i++)
		state->treehash[i].node = &state->th_nodes[state->n*i];
	xmss_set_bds_state(&state->bds, state->stack, state->stackoffset,
	    state->stacklevels, state->auth, state->keep, state->treehash,
	    state->retain, 0);
	return 0;
}

void
sshkey_xmss_free_bds(struct sshkey *key)
{
	struct ssh_xmss_state *state = key->xmss_state;

	if (state == NULL)
		return;
	free(state->stack);
	free(state->stacklevels);
	free(state->auth);
	free(state->keep);
	free(state->th_nodes);
	free(state->retain);
	free(state->treehash);
	state->stack = NULL;
	state->stacklevels = NULL;
	state->auth = NULL;
	state->keep = NULL;
	state->th_nodes = NULL;
	state->retain = NULL;
	state->treehash = NULL;
}

void *
sshkey_xmss_params(const struct sshkey *key)
{
	struct ssh_xmss_state *state = key->xmss_state;

	if (state == NULL)
		return NULL;
	return &state->params;
}

void *
sshkey_xmss_bds_state(const struct sshkey *key)
{
	struct ssh_xmss_state *state = key->xmss_state;

	if (state == NULL)
		return NULL;
	return &state->bds;
}

int
sshkey_xmss_siglen(const struct sshkey *key, size_t *lenp)
{
	struct ssh_xmss_state *state = key->xmss_state;

	if (lenp == NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	if (state == NULL)
		return SSH_ERR_INVALID_FORMAT;
	*lenp = 4 + state->n +
	    state->params.wots_par.keysize +
	    state->h * state->n;
	return 0;
}

size_t
sshkey_xmss_pklen(const struct sshkey *key)
{
	struct ssh_xmss_state *state = key->xmss_state;

	if (state == NULL)
		return 0;
	return state->n * 2;
}

size_t
sshkey_xmss_sklen(const struct sshkey *key)
{
	struct ssh_xmss_state *state = key->xmss_state;

	if (state == NULL)
		return 0;
	return state->n * 4 + 4;
}

int
sshkey_xmss_init_enc_key(struct sshkey *k, const char *ciphername)
{
	struct ssh_xmss_state *state = k->xmss_state;
	const struct sshcipher *cipher;
	size_t keylen = 0, ivlen = 0;

	if (state == NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	if ((cipher = cipher_by_name(ciphername)) == NULL)
		return SSH_ERR_INTERNAL_ERROR;
	if ((state->enc_ciphername = strdup(ciphername)) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	keylen = cipher_keylen(cipher);
	ivlen = cipher_ivlen(cipher);
	state->enc_keyiv_len = keylen + ivlen;
	if ((state->enc_keyiv = calloc(state->enc_keyiv_len, 1)) == NULL) {
		free(state->enc_ciphername);
		state->enc_ciphername = NULL;
		return SSH_ERR_ALLOC_FAIL;
	}
	arc4random_buf(state->enc_keyiv, state->enc_keyiv_len);
	return 0;
}

int
sshkey_xmss_serialize_enc_key(const struct sshkey *k, struct sshbuf *b)
{
	struct ssh_xmss_state *state = k->xmss_state;
	int r;

	if (state == NULL || state->enc_keyiv == NULL ||
	    state->enc_ciphername == NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	if ((r = sshbuf_put_cstring(b, state->enc_ciphername)) != 0 ||
	    (r = sshbuf_put_string(b, state->enc_keyiv,
	    state->enc_keyiv_len)) != 0)
		return r;
	return 0;
}

int
sshkey_xmss_deserialize_enc_key(struct sshkey *k, struct sshbuf *b)
{
	struct ssh_xmss_state *state = k->xmss_state;
	size_t len;
	int r;

	if (state == NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	if ((r = sshbuf_get_cstring(b, &state->enc_ciphername, NULL)) != 0 ||
	    (r = sshbuf_get_string(b, &state->enc_keyiv, &len)) != 0)
		return r;
	state->enc_keyiv_len = len;
	return 0;
}

int
sshkey_xmss_serialize_pk_info(const struct sshkey *k, struct sshbuf *b,
    enum sshkey_serialize_rep opts)
{
	struct ssh_xmss_state *state = k->xmss_state;
	u_char have_info = 1;
	u_int32_t idx;
	int r;

	if (state == NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	if (opts != SSHKEY_SERIALIZE_INFO)
		return 0;
	idx = k->xmss_sk ? PEEK_U32(k->xmss_sk) : state->idx;
	if ((r = sshbuf_put_u8(b, have_info)) != 0 ||
	    (r = sshbuf_put_u32(b, idx)) != 0 ||
	    (r = sshbuf_put_u32(b, state->maxidx)) != 0)
		return r;
	return 0;
}

int
sshkey_xmss_deserialize_pk_info(struct sshkey *k, struct sshbuf *b)
{
	struct ssh_xmss_state *state = k->xmss_state;
	u_char have_info;
	int r;

	if (state == NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	/* optional */
	if (sshbuf_len(b) == 0)
		return 0;
	if ((r = sshbuf_get_u8(b, &have_info)) != 0)
		return r;
	if (have_info != 1)
		return SSH_ERR_INVALID_ARGUMENT;
	if ((r = sshbuf_get_u32(b, &state->idx)) != 0 ||
	    (r = sshbuf_get_u32(b, &state->maxidx)) != 0)
		return r;
	return 0;
}

int
sshkey_xmss_generate_private_key(struct sshkey *k, u_int bits)
{
	int r;
	const char *name;

	if (bits == 10) {
		name = XMSS_SHA2_256_W16_H10_NAME;
	} else if (bits == 16) {
		name = XMSS_SHA2_256_W16_H16_NAME;
	} else if (bits == 20) {
		name = XMSS_SHA2_256_W16_H20_NAME;
	} else {
		name = XMSS_DEFAULT_NAME;
	}
	if ((r = sshkey_xmss_init(k, name)) != 0 ||
	    (r = sshkey_xmss_init_bds_state(k)) != 0 ||
	    (r = sshkey_xmss_init_enc_key(k, XMSS_CIPHERNAME)) != 0)
		return r;
	if ((k->xmss_pk = malloc(sshkey_xmss_pklen(k))) == NULL ||
	    (k->xmss_sk = malloc(sshkey_xmss_sklen(k))) == NULL) {
		return SSH_ERR_ALLOC_FAIL;
	}
	xmss_keypair(k->xmss_pk, k->xmss_sk, sshkey_xmss_bds_state(k),
	    sshkey_xmss_params(k));
	return 0;
}

int
sshkey_xmss_get_state_from_file(struct sshkey *k, const char *filename,
    int *have_file, sshkey_printfn *pr)
{
	struct sshbuf *b = NULL, *enc = NULL;
	int ret = SSH_ERR_SYSTEM_ERROR, r, fd = -1;
	u_int32_t len;
	unsigned char buf[4], *data = NULL;

	*have_file = 0;
	if ((fd = open(filename, O_RDONLY)) >= 0) {
		*have_file = 1;
		if (atomicio(read, fd, buf, sizeof(buf)) != sizeof(buf)) {
			PRINT("%s: corrupt state file: %s", __func__, filename);
			goto done;
		}
		len = PEEK_U32(buf);
		if ((data = calloc(len, 1)) == NULL) {
			ret = SSH_ERR_ALLOC_FAIL;
			goto done;
		}
		if (atomicio(read, fd, data, len) != len) {
			PRINT("%s: cannot read blob: %s", __func__, filename);
			goto done;
		}
		if ((enc = sshbuf_from(data, len)) == NULL) {
			ret = SSH_ERR_ALLOC_FAIL;
			goto done;
		}
		sshkey_xmss_free_bds(k);
		if ((r = sshkey_xmss_decrypt_state(k, enc, &b)) != 0) {
			ret = r;
			goto done;
		}
		if ((r = sshkey_xmss_deserialize_state(k, b)) != 0) {
			ret = r;
			goto done;
		}
		ret = 0;
	}
done:
	if (fd != -1)
		close(fd);
	free(data);
	sshbuf_free(enc);
	sshbuf_free(b);
	return ret;
}

int
sshkey_xmss_get_state(const struct sshkey *k, sshkey_printfn *pr)
{
	struct ssh_xmss_state *state = k->xmss_state;
	u_int32_t idx = 0;
	char *filename = NULL;
	char *statefile = NULL, *ostatefile = NULL, *lockfile = NULL;
	int lockfd = -1, have_state = 0, have_ostate, tries = 0;
	int ret = SSH_ERR_INVALID_ARGUMENT, r;

	if (state == NULL)
		goto done;
	/*
	 * If maxidx is set, then we are allowed a limited number
	 * of signatures, but don't need to access the disk.
	 * Otherwise we need to deal with the on-disk state.
	 */
	if (state->maxidx) {
		/* xmss_sk always contains the current state */
		idx = PEEK_U32(k->xmss_sk);
		if (idx < state->maxidx) {
			state->allow_update = 1;
			return 0;
		}
		return SSH_ERR_INVALID_ARGUMENT;
	}
	if ((filename = k->xmss_filename) == NULL)
		goto done;
	if (asprintf(&lockfile, "%s.lock", filename) < 0 ||
	    asprintf(&statefile, "%s.state", filename) < 0 ||
	    asprintf(&ostatefile, "%s.ostate", filename) < 0) {
		ret = SSH_ERR_ALLOC_FAIL;
		goto done;
	}
	if ((lockfd = open(lockfile, O_CREAT|O_RDONLY, 0600)) < 0) {
		ret = SSH_ERR_SYSTEM_ERROR;
		PRINT("%s: cannot open/create: %s", __func__, lockfile);
		goto done;
	}
	while (flock(lockfd, LOCK_EX|LOCK_NB) < 0) {
		if (errno != EWOULDBLOCK) {
			ret = SSH_ERR_SYSTEM_ERROR;
			PRINT("%s: cannot lock: %s", __func__, lockfile);
			goto done;
		}
		if (++tries > 10) {
			ret = SSH_ERR_SYSTEM_ERROR;
			PRINT("%s: giving up on: %s", __func__, lockfile);
			goto done;
		}
		usleep(1000*100*tries);
	}
	/* XXX no longer const */
	if ((r = sshkey_xmss_get_state_from_file((struct sshkey *)k,
	    statefile, &have_state, pr)) != 0) {
		if ((r = sshkey_xmss_get_state_from_file((struct sshkey *)k,
		    ostatefile, &have_ostate, pr)) == 0) {
			state->allow_update = 1;
			r = sshkey_xmss_forward_state(k, 1);
			state->idx = PEEK_U32(k->xmss_sk);
			state->allow_update = 0;
		}
	}
	if (!have_state && !have_ostate) {
		/* check that bds state is initialized */
		if (state->bds.auth == NULL)
			goto done;
		PRINT("%s: start from scratch idx 0: %u", __func__, state->idx);
	} else if (r != 0) {
		ret = r;
		goto done;
	}
	if (state->idx + 1 < state->idx) {
		PRINT("%s: state wrap: %u", __func__, state->idx);
		goto done;
	}
	state->have_state = have_state;
	state->lockfd = lockfd;
	state->allow_update = 1;
	lockfd = -1;
	ret = 0;
done:
	if (lockfd != -1)
		close(lockfd);
	free(lockfile);
	free(statefile);
	free(ostatefile);
	return ret;
}

int
sshkey_xmss_forward_state(const struct sshkey *k, u_int32_t reserve)
{
	struct ssh_xmss_state *state = k->xmss_state;
	u_char *sig = NULL;
	size_t required_siglen;
	unsigned long long smlen;
	u_char data;
	int ret, r;

	if (state == NULL || !state->allow_update)
		return SSH_ERR_INVALID_ARGUMENT;
	if (reserve == 0)
		return SSH_ERR_INVALID_ARGUMENT;
	if (state->idx + reserve <= state->idx)
		return SSH_ERR_INVALID_ARGUMENT;
	if ((r = sshkey_xmss_siglen(k, &required_siglen)) != 0)
		return r;
	if ((sig = malloc(required_siglen)) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	while (reserve-- > 0) {
		state->idx = PEEK_U32(k->xmss_sk);
		smlen = required_siglen;
		if ((ret = xmss_sign(k->xmss_sk, sshkey_xmss_bds_state(k),
		    sig, &smlen, &data, 0, sshkey_xmss_params(k))) != 0) {
			r = SSH_ERR_INVALID_ARGUMENT;
			break;
		}
	}
	free(sig);
	return r;
}

int
sshkey_xmss_update_state(const struct sshkey *k, sshkey_printfn *pr)
{
	struct ssh_xmss_state *state = k->xmss_state;
	struct sshbuf *b = NULL, *enc = NULL;
	u_int32_t idx = 0;
	unsigned char buf[4];
	char *filename = NULL;
	char *statefile = NULL, *ostatefile = NULL, *nstatefile = NULL;
	int fd = -1;
	int ret = SSH_ERR_INVALID_ARGUMENT;

	if (state == NULL || !state->allow_update)
		return ret;
	if (state->maxidx) {
		/* no update since the number of signatures is limited */
		ret = 0;
		goto done;
	}
	idx = PEEK_U32(k->xmss_sk);
	if (idx == state->idx) {
		/* no signature happened, no need to update */
		ret = 0;
		goto done;
	} else if (idx != state->idx + 1) {
		PRINT("%s: more than one signature happened: idx %u state %u",
		     __func__, idx, state->idx);
		goto done;
	}
	state->idx = idx;
	if ((filename = k->xmss_filename) == NULL)
		goto done;
	if (asprintf(&statefile, "%s.state", filename) < 0 ||
	    asprintf(&ostatefile, "%s.ostate", filename) < 0 ||
	    asprintf(&nstatefile, "%s.nstate", filename) < 0) {
		ret = SSH_ERR_ALLOC_FAIL;
		goto done;
	}
	unlink(nstatefile);
	if ((b = sshbuf_new()) == NULL) {
		ret = SSH_ERR_ALLOC_FAIL;
		goto done;
	}
	if ((ret = sshkey_xmss_serialize_state(k, b)) != 0) {
		PRINT("%s: SERLIALIZE FAILED: %d", __func__, ret);
		goto done;
	}
	if ((ret = sshkey_xmss_encrypt_state(k, b, &enc)) != 0) {
		PRINT("%s: ENCRYPT FAILED: %d", __func__, ret);
		goto done;
	}
	if ((fd = open(nstatefile, O_CREAT|O_WRONLY|O_EXCL, 0600)) < 0) {
		ret = SSH_ERR_SYSTEM_ERROR;
		PRINT("%s: open new state file: %s", __func__, nstatefile);
		goto done;
	}
	POKE_U32(buf, sshbuf_len(enc));
	if (atomicio(vwrite, fd, buf, sizeof(buf)) != sizeof(buf)) {
		ret = SSH_ERR_SYSTEM_ERROR;
		PRINT("%s: write new state file hdr: %s", __func__, nstatefile);
		close(fd);
		goto done;
	}
	if (atomicio(vwrite, fd, sshbuf_mutable_ptr(enc), sshbuf_len(enc)) !=
	    sshbuf_len(enc)) {
		ret = SSH_ERR_SYSTEM_ERROR;
		PRINT("%s: write new state file data: %s", __func__, nstatefile);
		close(fd);
		goto done;
	}
	if (fsync(fd) < 0) {
		ret = SSH_ERR_SYSTEM_ERROR;
		PRINT("%s: sync new state file: %s", __func__, nstatefile);
		close(fd);
		goto done;
	}
	if (close(fd) < 0) {
		ret = SSH_ERR_SYSTEM_ERROR;
		PRINT("%s: close new state file: %s", __func__, nstatefile);
		goto done;
	}
	if (state->have_state) {
		unlink(ostatefile);
		if (link(statefile, ostatefile)) {
			ret = SSH_ERR_SYSTEM_ERROR;
			PRINT("%s: backup state %s to %s", __func__, statefile,
			    ostatefile);
			goto done;
		}
	}
	if (rename(nstatefile, statefile) < 0) {
		ret = SSH_ERR_SYSTEM_ERROR;
		PRINT("%s: rename %s to %s", __func__, nstatefile, statefile);
		goto done;
	}
	ret = 0;
done:
	if (state->lockfd != -1) {
		close(state->lockfd);
		state->lockfd = -1;
	}
	if (nstatefile)
		unlink(nstatefile);
	free(statefile);
	free(ostatefile);
	free(nstatefile);
	sshbuf_free(b);
	sshbuf_free(enc);
	return ret;
}

int
sshkey_xmss_serialize_state(const struct sshkey *k, struct sshbuf *b)
{
	struct ssh_xmss_state *state = k->xmss_state;
	treehash_inst *th;
	u_int32_t i, node;
	int r;

	if (state == NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	if (state->stack == NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	state->stackoffset = state->bds.stackoffset;	/* copy back */
	if ((r = sshbuf_put_cstring(b, SSH_XMSS_K2_MAGIC)) != 0 ||
	    (r = sshbuf_put_u32(b, state->idx)) != 0 ||
	    (r = sshbuf_put_string(b, state->stack, num_stack(state))) != 0 ||
	    (r = sshbuf_put_u32(b, state->stackoffset)) != 0 ||
	    (r = sshbuf_put_string(b, state->stacklevels, num_stacklevels(state))) != 0 ||
	    (r = sshbuf_put_string(b, state->auth, num_auth(state))) != 0 ||
	    (r = sshbuf_put_string(b, state->keep, num_keep(state))) != 0 ||
	    (r = sshbuf_put_string(b, state->th_nodes, num_th_nodes(state))) != 0 ||
	    (r = sshbuf_put_string(b, state->retain, num_retain(state))) != 0 ||
	    (r = sshbuf_put_u32(b, num_treehash(state))) != 0)
		return r;
	for (i = 0; i < num_treehash(state); i++) {
		th = &state->treehash[i];
		node = th->node - state->th_nodes;
		if ((r = sshbuf_put_u32(b, th->h)) != 0 ||
		    (r = sshbuf_put_u32(b, th->next_idx)) != 0 ||
		    (r = sshbuf_put_u32(b, th->stackusage)) != 0 ||
		    (r = sshbuf_put_u8(b, th->completed)) != 0 ||
		    (r = sshbuf_put_u32(b, node)) != 0)
			return r;
	}
	return 0;
}

int
sshkey_xmss_serialize_state_opt(const struct sshkey *k, struct sshbuf *b,
    enum sshkey_serialize_rep opts)
{
	struct ssh_xmss_state *state = k->xmss_state;
	int r = SSH_ERR_INVALID_ARGUMENT;

	if (state == NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	if ((r = sshbuf_put_u8(b, opts)) != 0)
		return r;
	switch (opts) {
	case SSHKEY_SERIALIZE_STATE:
		r = sshkey_xmss_serialize_state(k, b);
		break;
	case SSHKEY_SERIALIZE_FULL:
		if ((r = sshkey_xmss_serialize_enc_key(k, b)) != 0)
			break;
		r = sshkey_xmss_serialize_state(k, b);
		break;
	case SSHKEY_SERIALIZE_DEFAULT:
		r = 0;
		break;
	default:
		r = SSH_ERR_INVALID_ARGUMENT;
		break;
	}
	return r;
}

int
sshkey_xmss_deserialize_state(struct sshkey *k, struct sshbuf *b)
{
	struct ssh_xmss_state *state = k->xmss_state;
	treehash_inst *th;
	u_int32_t i, lh, node;
	size_t ls, lsl, la, lk, ln, lr;
	char *magic;
	int r;

	if (state == NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	if (k->xmss_sk == NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	if ((state->treehash = calloc(num_treehash(state),
	    sizeof(treehash_inst))) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	if ((r = sshbuf_get_cstring(b, &magic, NULL)) != 0 ||
	    (r = sshbuf_get_u32(b, &state->idx)) != 0 ||
	    (r = sshbuf_get_string(b, &state->stack, &ls)) != 0 ||
	    (r = sshbuf_get_u32(b, &state->stackoffset)) != 0 ||
	    (r = sshbuf_get_string(b, &state->stacklevels, &lsl)) != 0 ||
	    (r = sshbuf_get_string(b, &state->auth, &la)) != 0 ||
	    (r = sshbuf_get_string(b, &state->keep, &lk)) != 0 ||
	    (r = sshbuf_get_string(b, &state->th_nodes, &ln)) != 0 ||
	    (r = sshbuf_get_string(b, &state->retain, &lr)) != 0 ||
	    (r = sshbuf_get_u32(b, &lh)) != 0)
		return r;
	if (strcmp(magic, SSH_XMSS_K2_MAGIC) != 0)
		return SSH_ERR_INVALID_ARGUMENT;
	/* XXX check stackoffset */
	if (ls != num_stack(state) ||
	    lsl != num_stacklevels(state) ||
	    la != num_auth(state) ||
	    lk != num_keep(state) ||
	    ln != num_th_nodes(state) ||
	    lr != num_retain(state) ||
	    lh != num_treehash(state))
		return SSH_ERR_INVALID_ARGUMENT;
	for (i = 0; i < num_treehash(state); i++) {
		th = &state->treehash[i];
		if ((r = sshbuf_get_u32(b, &th->h)) != 0 ||
		    (r = sshbuf_get_u32(b, &th->next_idx)) != 0 ||
		    (r = sshbuf_get_u32(b, &th->stackusage)) != 0 ||
		    (r = sshbuf_get_u8(b, &th->completed)) != 0 ||
		    (r = sshbuf_get_u32(b, &node)) != 0)
			return r;
		if (node < num_th_nodes(state))
			th->node = &state->th_nodes[node];
	}
	POKE_U32(k->xmss_sk, state->idx);
	xmss_set_bds_state(&state->bds, state->stack, state->stackoffset,
	    state->stacklevels, state->auth, state->keep, state->treehash,
	    state->retain, 0);
	return 0;
}

int
sshkey_xmss_deserialize_state_opt(struct sshkey *k, struct sshbuf *b)
{
	enum sshkey_serialize_rep opts;
	u_char have_state;
	int r;

	if ((r = sshbuf_get_u8(b, &have_state)) != 0)
		return r;

	opts = have_state;
	switch (opts) {
	case SSHKEY_SERIALIZE_DEFAULT:
		r = 0;
		break;
	case SSHKEY_SERIALIZE_STATE:
		if ((r = sshkey_xmss_deserialize_state(k, b)) != 0)
			return r;
		break;
	case SSHKEY_SERIALIZE_FULL:
		if ((r = sshkey_xmss_deserialize_enc_key(k, b)) != 0 ||
		    (r = sshkey_xmss_deserialize_state(k, b)) != 0)
			return r;
		break;
	default:
		r = SSH_ERR_INVALID_FORMAT;
		break;
	}
	return r;
}

int
sshkey_xmss_encrypt_state(const struct sshkey *k, struct sshbuf *b,
   struct sshbuf **retp)
{
	struct ssh_xmss_state *state = k->xmss_state;
	struct sshbuf *encrypted = NULL, *encoded = NULL, *padded = NULL;
	struct sshcipher_ctx *ciphercontext = NULL;
	const struct sshcipher *cipher;
	u_char *cp, *key, *iv = NULL;
	size_t i, keylen, ivlen, blocksize, authlen, encrypted_len, aadlen;
	int r = SSH_ERR_INTERNAL_ERROR;

	if (retp != NULL)
		*retp = NULL;
	if (state == NULL ||
	    state->enc_keyiv == NULL ||
	    state->enc_ciphername == NULL)
		return SSH_ERR_INTERNAL_ERROR;
	if ((cipher = cipher_by_name(state->enc_ciphername)) == NULL) {
		r = SSH_ERR_INTERNAL_ERROR;
		goto out;
	}
	blocksize = cipher_blocksize(cipher);
	keylen = cipher_keylen(cipher);
	ivlen = cipher_ivlen(cipher);
	authlen = cipher_authlen(cipher);
	if (state->enc_keyiv_len != keylen + ivlen) {
		r = SSH_ERR_INVALID_FORMAT;
		goto out;
	}
	key = state->enc_keyiv;
	if ((encrypted = sshbuf_new()) == NULL ||
	    (encoded = sshbuf_new()) == NULL ||
	    (padded = sshbuf_new()) == NULL ||
	    (iv = malloc(ivlen)) == NULL) {
		r = SSH_ERR_ALLOC_FAIL;
		goto out;
	}

	/* replace first 4 bytes of IV with index to ensure uniqueness */
	memcpy(iv, key + keylen, ivlen);
	POKE_U32(iv, state->idx);

	if ((r = sshbuf_put(encoded, XMSS_MAGIC, sizeof(XMSS_MAGIC))) != 0 ||
	    (r = sshbuf_put_u32(encoded, state->idx)) != 0)
		goto out;

	/* padded state will be encrypted */
	if ((r = sshbuf_putb(padded, b)) != 0)
		goto out;
	i = 0;
	while (sshbuf_len(padded) % blocksize) {
		if ((r = sshbuf_put_u8(padded, ++i & 0xff)) != 0)
			goto out;
	}
	encrypted_len = sshbuf_len(padded);

	/* header including the length of state is used as AAD */
	if ((r = sshbuf_put_u32(encoded, encrypted_len)) != 0)
		goto out;
	aadlen = sshbuf_len(encoded);

	/* concat header and state */
	if ((r = sshbuf_putb(encoded, padded)) != 0)
		goto out;

	/* reserve space for encryption of encoded data plus auth tag */
	/* encrypt at offset addlen */
	if ((r = sshbuf_reserve(encrypted,
	    encrypted_len + aadlen + authlen, &cp)) != 0 ||
	    (r = cipher_init(&ciphercontext, cipher, key, keylen,
	    iv, ivlen, 1)) != 0 ||
	    (r = cipher_crypt(ciphercontext, 0, cp, sshbuf_ptr(encoded),
	    encrypted_len, aadlen, authlen)) != 0)
		goto out;

	/* success */
	r = 0;
 out:
	if (retp != NULL) {
		*retp = encrypted;
		encrypted = NULL;
	}
	sshbuf_free(padded);
	sshbuf_free(encoded);
	sshbuf_free(encrypted);
	cipher_free(ciphercontext);
	free(iv);
	return r;
}

int
sshkey_xmss_decrypt_state(const struct sshkey *k, struct sshbuf *encoded,
   struct sshbuf **retp)
{
	struct ssh_xmss_state *state = k->xmss_state;
	struct sshbuf *copy = NULL, *decrypted = NULL;
	struct sshcipher_ctx *ciphercontext = NULL;
	const struct sshcipher *cipher = NULL;
	u_char *key, *iv = NULL, *dp;
	size_t keylen, ivlen, authlen, aadlen;
	u_int blocksize, encrypted_len, index;
	int r = SSH_ERR_INTERNAL_ERROR;

	if (retp != NULL)
		*retp = NULL;
	if (state == NULL ||
	    state->enc_keyiv == NULL ||
	    state->enc_ciphername == NULL)
		return SSH_ERR_INTERNAL_ERROR;
	if ((cipher = cipher_by_name(state->enc_ciphername)) == NULL) {
		r = SSH_ERR_INVALID_FORMAT;
		goto out;
	}
	blocksize = cipher_blocksize(cipher);
	keylen = cipher_keylen(cipher);
	ivlen = cipher_ivlen(cipher);
	authlen = cipher_authlen(cipher);
	if (state->enc_keyiv_len != keylen + ivlen) {
		r = SSH_ERR_INTERNAL_ERROR;
		goto out;
	}
	key = state->enc_keyiv;

	if ((copy = sshbuf_fromb(encoded)) == NULL ||
	    (decrypted = sshbuf_new()) == NULL ||
	    (iv = malloc(ivlen)) == NULL) {
		r = SSH_ERR_ALLOC_FAIL;
		goto out;
	}

	/* check magic */
	if (sshbuf_len(encoded) < sizeof(XMSS_MAGIC) ||
	    memcmp(sshbuf_ptr(encoded), XMSS_MAGIC, sizeof(XMSS_MAGIC))) {
		r = SSH_ERR_INVALID_FORMAT;
		goto out;
	}
	/* parse public portion */
	if ((r = sshbuf_consume(encoded, sizeof(XMSS_MAGIC))) != 0 ||
	    (r = sshbuf_get_u32(encoded, &index)) != 0 ||
	    (r = sshbuf_get_u32(encoded, &encrypted_len)) != 0)
		goto out;

	/* check size of encrypted key blob */
	if (encrypted_len < blocksize || (encrypted_len % blocksize) != 0) {
		r = SSH_ERR_INVALID_FORMAT;
		goto out;
	}
	/* check that an appropriate amount of auth data is present */
	if (sshbuf_len(encoded) < encrypted_len + authlen) {
		r = SSH_ERR_INVALID_FORMAT;
		goto out;
	}

	aadlen = sshbuf_len(copy) - sshbuf_len(encoded);

	/* replace first 4 bytes of IV with index to ensure uniqueness */
	memcpy(iv, key + keylen, ivlen);
	POKE_U32(iv, index);

	/* decrypt private state of key */
	if ((r = sshbuf_reserve(decrypted, aadlen + encrypted_len, &dp)) != 0 ||
	    (r = cipher_init(&ciphercontext, cipher, key, keylen,
	    iv, ivlen, 0)) != 0 ||
	    (r = cipher_crypt(ciphercontext, 0, dp, sshbuf_ptr(copy),
	    encrypted_len, aadlen, authlen)) != 0)
		goto out;

	/* there should be no trailing data */
	if ((r = sshbuf_consume(encoded, encrypted_len + authlen)) != 0)
		goto out;
	if (sshbuf_len(encoded) != 0) {
		r = SSH_ERR_INVALID_FORMAT;
		goto out;
	}

	/* remove AAD */
	if ((r = sshbuf_consume(decrypted, aadlen)) != 0)
		goto out;
	/* XXX encrypted includes unchecked padding */

	/* success */
	r = 0;
	if (retp != NULL) {
		*retp = decrypted;
		decrypted = NULL;
	}
 out:
	cipher_free(ciphercontext);
	sshbuf_free(copy);
	sshbuf_free(decrypted);
	free(iv);
	return r;
}

u_int32_t
sshkey_xmss_signatures_left(const struct sshkey *k)
{
	struct ssh_xmss_state *state = k->xmss_state;
	u_int32_t idx;

	if (sshkey_type_plain(k->type) == KEY_XMSS && state &&
	    state->maxidx) {
		idx = k->xmss_sk ? PEEK_U32(k->xmss_sk) : state->idx;
		if (idx < state->maxidx)
			return state->maxidx - idx;
	}
	return 0;
}

int
sshkey_xmss_enable_maxsign(struct sshkey *k, u_int32_t maxsign)
{
	struct ssh_xmss_state *state = k->xmss_state;

	if (sshkey_type_plain(k->type) != KEY_XMSS)
		return SSH_ERR_INVALID_ARGUMENT;
	if (maxsign == 0)
		return 0;
	if (state->idx + maxsign < state->idx)
		return SSH_ERR_INVALID_ARGUMENT;
	state->maxidx = state->idx + maxsign;
	return 0;
}
#endif /* WITH_XMSS */
