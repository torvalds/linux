/*	$OpenBSD: sshbuf.h,v 1.11 2018/07/09 21:56:06 markus Exp $	*/
/*
 * Copyright (c) 2011 Damien Miller
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

#ifndef _SSHBUF_H
#define _SSHBUF_H

#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>
#ifdef WITH_OPENSSL
# include <openssl/bn.h>
# ifdef OPENSSL_HAS_ECC
#  include <openssl/ec.h>
# endif /* OPENSSL_HAS_ECC */
#endif /* WITH_OPENSSL */

#define SSHBUF_SIZE_MAX		0x8000000	/* Hard maximum size */
#define SSHBUF_REFS_MAX		0x100000	/* Max child buffers */
#define SSHBUF_MAX_BIGNUM	(16384 / 8)	/* Max bignum *bytes* */
#define SSHBUF_MAX_ECPOINT	((528 * 2 / 8) + 1) /* Max EC point *bytes* */

/*
 * NB. do not depend on the internals of this. It will be made opaque
 * one day.
 */
struct sshbuf {
	u_char *d;		/* Data */
	const u_char *cd;	/* Const data */
	size_t off;		/* First available byte is buf->d + buf->off */
	size_t size;		/* Last byte is buf->d + buf->size - 1 */
	size_t max_size;	/* Maximum size of buffer */
	size_t alloc;		/* Total bytes allocated to buf->d */
	int readonly;		/* Refers to external, const data */
	int dont_free;		/* Kludge to support sshbuf_init */
	u_int refcount;		/* Tracks self and number of child buffers */
	struct sshbuf *parent;	/* If child, pointer to parent */
};

/*
 * Create a new sshbuf buffer.
 * Returns pointer to buffer on success, or NULL on allocation failure.
 */
struct sshbuf *sshbuf_new(void);

/*
 * Create a new, read-only sshbuf buffer from existing data.
 * Returns pointer to buffer on success, or NULL on allocation failure.
 */
struct sshbuf *sshbuf_from(const void *blob, size_t len);

/*
 * Create a new, read-only sshbuf buffer from the contents of an existing
 * buffer. The contents of "buf" must not change in the lifetime of the
 * resultant buffer.
 * Returns pointer to buffer on success, or NULL on allocation failure.
 */
struct sshbuf *sshbuf_fromb(struct sshbuf *buf);

/*
 * Create a new, read-only sshbuf buffer from the contents of a string in
 * an existing buffer (the string is consumed in the process).
 * The contents of "buf" must not change in the lifetime of the resultant
 * buffer.
 * Returns pointer to buffer on success, or NULL on allocation failure.
 */
int	sshbuf_froms(struct sshbuf *buf, struct sshbuf **bufp);

/*
 * Clear and free buf
 */
void	sshbuf_free(struct sshbuf *buf);

/*
 * Reset buf, clearing its contents. NB. max_size is preserved.
 */
void	sshbuf_reset(struct sshbuf *buf);

/*
 * Return the maximum size of buf
 */
size_t	sshbuf_max_size(const struct sshbuf *buf);

/*
 * Set the maximum size of buf
 * Returns 0 on success, or a negative SSH_ERR_* error code on failure.
 */
int	sshbuf_set_max_size(struct sshbuf *buf, size_t max_size);

/*
 * Returns the length of data in buf
 */
size_t	sshbuf_len(const struct sshbuf *buf);

/*
 * Returns number of bytes left in buffer before hitting max_size.
 */
size_t	sshbuf_avail(const struct sshbuf *buf);

/*
 * Returns a read-only pointer to the start of the data in buf
 */
const u_char *sshbuf_ptr(const struct sshbuf *buf);

/*
 * Returns a mutable pointer to the start of the data in buf, or
 * NULL if the buffer is read-only.
 */
u_char *sshbuf_mutable_ptr(const struct sshbuf *buf);

/*
 * Check whether a reservation of size len will succeed in buf
 * Safer to use than direct comparisons again sshbuf_avail as it copes
 * with unsigned overflows correctly.
 * Returns 0 on success, or a negative SSH_ERR_* error code on failure.
 */
int	sshbuf_check_reserve(const struct sshbuf *buf, size_t len);

/*
 * Preallocates len additional bytes in buf.
 * Useful for cases where the caller knows how many bytes will ultimately be
 * required to avoid realloc in the buffer code.
 * Returns 0 on success, or a negative SSH_ERR_* error code on failure.
 */
int	sshbuf_allocate(struct sshbuf *buf, size_t len);

/*
 * Reserve len bytes in buf.
 * Returns 0 on success and a pointer to the first reserved byte via the
 * optional dpp parameter or a negative * SSH_ERR_* error code on failure.
 */
int	sshbuf_reserve(struct sshbuf *buf, size_t len, u_char **dpp);

/*
 * Consume len bytes from the start of buf
 * Returns 0 on success, or a negative SSH_ERR_* error code on failure.
 */
int	sshbuf_consume(struct sshbuf *buf, size_t len);

/*
 * Consume len bytes from the end of buf
 * Returns 0 on success, or a negative SSH_ERR_* error code on failure.
 */
int	sshbuf_consume_end(struct sshbuf *buf, size_t len);

/* Extract or deposit some bytes */
int	sshbuf_get(struct sshbuf *buf, void *v, size_t len);
int	sshbuf_put(struct sshbuf *buf, const void *v, size_t len);
int	sshbuf_putb(struct sshbuf *buf, const struct sshbuf *v);

/* Append using a printf(3) format */
int	sshbuf_putf(struct sshbuf *buf, const char *fmt, ...)
	    __attribute__((format(printf, 2, 3)));
int	sshbuf_putfv(struct sshbuf *buf, const char *fmt, va_list ap);

/* Functions to extract or store big-endian words of various sizes */
int	sshbuf_get_u64(struct sshbuf *buf, u_int64_t *valp);
int	sshbuf_get_u32(struct sshbuf *buf, u_int32_t *valp);
int	sshbuf_get_u16(struct sshbuf *buf, u_int16_t *valp);
int	sshbuf_get_u8(struct sshbuf *buf, u_char *valp);
int	sshbuf_put_u64(struct sshbuf *buf, u_int64_t val);
int	sshbuf_put_u32(struct sshbuf *buf, u_int32_t val);
int	sshbuf_put_u16(struct sshbuf *buf, u_int16_t val);
int	sshbuf_put_u8(struct sshbuf *buf, u_char val);

#if defined(__FreeBSD__) && defined(__i386__)
#define sshbuf_get_time(b, vp) sshbuf_get_u32((b), (u_int32_t *)(vp))
#define sshbuf_put_time(b, v) sshbuf_put_u32((b), (u_int32_t)(v))
#else
#define sshbuf_get_time(b, vp) sshbuf_get_u64((b), (u_int64_t *)(vp))
#define sshbuf_put_time(b, v) sshbuf_put_u64((b), (u_int64_t)(v))
#endif

/*
 * Functions to extract or store SSH wire encoded strings (u32 len || data)
 * The "cstring" variants admit no \0 characters in the string contents.
 * Caller must free *valp.
 */
int	sshbuf_get_string(struct sshbuf *buf, u_char **valp, size_t *lenp);
int	sshbuf_get_cstring(struct sshbuf *buf, char **valp, size_t *lenp);
int	sshbuf_get_stringb(struct sshbuf *buf, struct sshbuf *v);
int	sshbuf_put_string(struct sshbuf *buf, const void *v, size_t len);
int	sshbuf_put_cstring(struct sshbuf *buf, const char *v);
int	sshbuf_put_stringb(struct sshbuf *buf, const struct sshbuf *v);

/*
 * "Direct" variant of sshbuf_get_string, returns pointer into the sshbuf to
 * avoid an malloc+memcpy. The pointer is guaranteed to be valid until the
 * next sshbuf-modifying function call. Caller does not free.
 */
int	sshbuf_get_string_direct(struct sshbuf *buf, const u_char **valp,
	    size_t *lenp);

/* Skip past a string */
#define sshbuf_skip_string(buf) sshbuf_get_string_direct(buf, NULL, NULL)

/* Another variant: "peeks" into the buffer without modifying it */
int	sshbuf_peek_string_direct(const struct sshbuf *buf, const u_char **valp,
	    size_t *lenp);
/* XXX peek_u8 / peek_u32 */

/*
 * Functions to extract or store SSH wire encoded bignums and elliptic
 * curve points.
 */
int	sshbuf_put_bignum2_bytes(struct sshbuf *buf, const void *v, size_t len);
int	sshbuf_get_bignum2_bytes_direct(struct sshbuf *buf,
	    const u_char **valp, size_t *lenp);
#ifdef WITH_OPENSSL
int	sshbuf_get_bignum2(struct sshbuf *buf, BIGNUM *v);
int	sshbuf_get_bignum1(struct sshbuf *buf, BIGNUM *v);
int	sshbuf_put_bignum2(struct sshbuf *buf, const BIGNUM *v);
int	sshbuf_put_bignum1(struct sshbuf *buf, const BIGNUM *v);
# ifdef OPENSSL_HAS_ECC
int	sshbuf_get_ec(struct sshbuf *buf, EC_POINT *v, const EC_GROUP *g);
int	sshbuf_get_eckey(struct sshbuf *buf, EC_KEY *v);
int	sshbuf_put_ec(struct sshbuf *buf, const EC_POINT *v, const EC_GROUP *g);
int	sshbuf_put_eckey(struct sshbuf *buf, const EC_KEY *v);
# endif /* OPENSSL_HAS_ECC */
#endif /* WITH_OPENSSL */

/* Dump the contents of the buffer in a human-readable format */
void	sshbuf_dump(struct sshbuf *buf, FILE *f);

/* Dump specified memory in a human-readable format */
void	sshbuf_dump_data(const void *s, size_t len, FILE *f);

/* Return the hexadecimal representation of the contents of the buffer */
char	*sshbuf_dtob16(struct sshbuf *buf);

/* Encode the contents of the buffer as base64 */
char	*sshbuf_dtob64(struct sshbuf *buf);

/* Decode base64 data and append it to the buffer */
int	sshbuf_b64tod(struct sshbuf *buf, const char *b64);

/*
 * Duplicate the contents of a buffer to a string (caller to free).
 * Returns NULL on buffer error, or if the buffer contains a premature
 * nul character.
 */
char *sshbuf_dup_string(struct sshbuf *buf);

/*
 * store struct pwd
 */
int sshbuf_put_passwd(struct sshbuf *buf, const struct passwd *pwent);

/*
 * extract struct pwd
 */
struct passwd *sshbuf_get_passwd(struct sshbuf *buf);

/*
 * free struct passwd obtained from sshbuf_get_passwd.
 */
void sshbuf_free_passwd(struct passwd *pwent);

/* Macros for decoding/encoding integers */
#define PEEK_U64(p) \
	(((u_int64_t)(((const u_char *)(p))[0]) << 56) | \
	 ((u_int64_t)(((const u_char *)(p))[1]) << 48) | \
	 ((u_int64_t)(((const u_char *)(p))[2]) << 40) | \
	 ((u_int64_t)(((const u_char *)(p))[3]) << 32) | \
	 ((u_int64_t)(((const u_char *)(p))[4]) << 24) | \
	 ((u_int64_t)(((const u_char *)(p))[5]) << 16) | \
	 ((u_int64_t)(((const u_char *)(p))[6]) << 8) | \
	  (u_int64_t)(((const u_char *)(p))[7]))
#define PEEK_U32(p) \
	(((u_int32_t)(((const u_char *)(p))[0]) << 24) | \
	 ((u_int32_t)(((const u_char *)(p))[1]) << 16) | \
	 ((u_int32_t)(((const u_char *)(p))[2]) << 8) | \
	  (u_int32_t)(((const u_char *)(p))[3]))
#define PEEK_U16(p) \
	(((u_int16_t)(((const u_char *)(p))[0]) << 8) | \
	  (u_int16_t)(((const u_char *)(p))[1]))

#define POKE_U64(p, v) \
	do { \
		const u_int64_t __v = (v); \
		((u_char *)(p))[0] = (__v >> 56) & 0xff; \
		((u_char *)(p))[1] = (__v >> 48) & 0xff; \
		((u_char *)(p))[2] = (__v >> 40) & 0xff; \
		((u_char *)(p))[3] = (__v >> 32) & 0xff; \
		((u_char *)(p))[4] = (__v >> 24) & 0xff; \
		((u_char *)(p))[5] = (__v >> 16) & 0xff; \
		((u_char *)(p))[6] = (__v >> 8) & 0xff; \
		((u_char *)(p))[7] = __v & 0xff; \
	} while (0)
#define POKE_U32(p, v) \
	do { \
		const u_int32_t __v = (v); \
		((u_char *)(p))[0] = (__v >> 24) & 0xff; \
		((u_char *)(p))[1] = (__v >> 16) & 0xff; \
		((u_char *)(p))[2] = (__v >> 8) & 0xff; \
		((u_char *)(p))[3] = __v & 0xff; \
	} while (0)
#define POKE_U16(p, v) \
	do { \
		const u_int16_t __v = (v); \
		((u_char *)(p))[0] = (__v >> 8) & 0xff; \
		((u_char *)(p))[1] = __v & 0xff; \
	} while (0)

/* Internal definitions follow. Exposed for regress tests */
#ifdef SSHBUF_INTERNAL

/*
 * Return the allocation size of buf
 */
size_t	sshbuf_alloc(const struct sshbuf *buf);

/*
 * Increment the reference count of buf.
 */
int	sshbuf_set_parent(struct sshbuf *child, struct sshbuf *parent);

/*
 * Return the parent buffer of buf, or NULL if it has no parent.
 */
const struct sshbuf *sshbuf_parent(const struct sshbuf *buf);

/*
 * Return the reference count of buf
 */
u_int	sshbuf_refcount(const struct sshbuf *buf);

# define SSHBUF_SIZE_INIT	256		/* Initial allocation */
# define SSHBUF_SIZE_INC	256		/* Preferred increment length */
# define SSHBUF_PACK_MIN	8192		/* Minimim packable offset */

/* # define SSHBUF_ABORT abort */
/* # define SSHBUF_DEBUG */

# ifndef SSHBUF_ABORT
#  define SSHBUF_ABORT()
# endif

# ifdef SSHBUF_DEBUG
#  define SSHBUF_TELL(what) do { \
		printf("%s:%d %s: %s size %zu alloc %zu off %zu max %zu\n", \
		    __FILE__, __LINE__, __func__, what, \
		    buf->size, buf->alloc, buf->off, buf->max_size); \
		fflush(stdout); \
	} while (0)
#  define SSHBUF_DBG(x) do { \
		printf("%s:%d %s: ", __FILE__, __LINE__, __func__); \
		printf x; \
		printf("\n"); \
		fflush(stdout); \
	} while (0)
# else
#  define SSHBUF_TELL(what)
#  define SSHBUF_DBG(x)
# endif
#endif /* SSHBUF_INTERNAL */

#endif /* _SSHBUF_H */
