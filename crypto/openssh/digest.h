/* $OpenBSD: digest.h,v 1.8 2017/05/08 22:57:38 djm Exp $ */
/*
 * Copyright (c) 2013 Damien Miller <djm@mindrot.org>
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

#ifndef _DIGEST_H
#define _DIGEST_H

/* Maximum digest output length */
#define SSH_DIGEST_MAX_LENGTH	64

/* Digest algorithms */
#define SSH_DIGEST_MD5		0
#define SSH_DIGEST_SHA1		1
#define SSH_DIGEST_SHA256	2
#define SSH_DIGEST_SHA384	3
#define SSH_DIGEST_SHA512	4
#define SSH_DIGEST_MAX		5

struct sshbuf;
struct ssh_digest_ctx;

/* Looks up a digest algorithm by name */
int ssh_digest_alg_by_name(const char *name);

/* Returns the algorithm name for a digest identifier */
const char *ssh_digest_alg_name(int alg);

/* Returns the algorithm's digest length in bytes or 0 for invalid algorithm */
size_t ssh_digest_bytes(int alg);

/* Returns the block size of the digest, e.g. for implementing HMAC */
size_t ssh_digest_blocksize(struct ssh_digest_ctx *ctx);

/* Copies internal state of digest of 'from' to 'to' */
int ssh_digest_copy_state(struct ssh_digest_ctx *from,
    struct ssh_digest_ctx *to);

/* One-shot API */
int ssh_digest_memory(int alg, const void *m, size_t mlen,
    u_char *d, size_t dlen)
	__attribute__((__bounded__(__buffer__, 2, 3)))
	__attribute__((__bounded__(__buffer__, 4, 5)));
int ssh_digest_buffer(int alg, const struct sshbuf *b, u_char *d, size_t dlen)
	__attribute__((__bounded__(__buffer__, 3, 4)));

/* Update API */
struct ssh_digest_ctx *ssh_digest_start(int alg);
int ssh_digest_update(struct ssh_digest_ctx *ctx, const void *m, size_t mlen)
	__attribute__((__bounded__(__buffer__, 2, 3)));
int ssh_digest_update_buffer(struct ssh_digest_ctx *ctx,
    const struct sshbuf *b);
int ssh_digest_final(struct ssh_digest_ctx *ctx, u_char *d, size_t dlen)
	__attribute__((__bounded__(__buffer__, 2, 3)));
void ssh_digest_free(struct ssh_digest_ctx *ctx);

#endif /* _DIGEST_H */

