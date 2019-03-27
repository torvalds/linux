/*	$OpenBSD: sshbuf-getput-basic.c,v 1.7 2017/06/01 04:51:58 djm Exp $	*/
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

#define SSHBUF_INTERNAL
#include "includes.h"

#include <sys/types.h>

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "xmalloc.h"
#include "ssherr.h"
#include "sshbuf.h"

int
sshbuf_get(struct sshbuf *buf, void *v, size_t len)
{
	const u_char *p = sshbuf_ptr(buf);
	int r;

	if ((r = sshbuf_consume(buf, len)) < 0)
		return r;
	if (v != NULL && len != 0)
		memcpy(v, p, len);
	return 0;
}

int
sshbuf_get_u64(struct sshbuf *buf, u_int64_t *valp)
{
	const u_char *p = sshbuf_ptr(buf);
	int r;

	if ((r = sshbuf_consume(buf, 8)) < 0)
		return r;
	if (valp != NULL)
		*valp = PEEK_U64(p);
	return 0;
}

int
sshbuf_get_u32(struct sshbuf *buf, u_int32_t *valp)
{
	const u_char *p = sshbuf_ptr(buf);
	int r;

	if ((r = sshbuf_consume(buf, 4)) < 0)
		return r;
	if (valp != NULL)
		*valp = PEEK_U32(p);
	return 0;
}

int
sshbuf_get_u16(struct sshbuf *buf, u_int16_t *valp)
{
	const u_char *p = sshbuf_ptr(buf);
	int r;

	if ((r = sshbuf_consume(buf, 2)) < 0)
		return r;
	if (valp != NULL)
		*valp = PEEK_U16(p);
	return 0;
}

int
sshbuf_get_u8(struct sshbuf *buf, u_char *valp)
{
	const u_char *p = sshbuf_ptr(buf);
	int r;

	if ((r = sshbuf_consume(buf, 1)) < 0)
		return r;
	if (valp != NULL)
		*valp = (u_int8_t)*p;
	return 0;
}

int
sshbuf_get_string(struct sshbuf *buf, u_char **valp, size_t *lenp)
{
	const u_char *val;
	size_t len;
	int r;

	if (valp != NULL)
		*valp = NULL;
	if (lenp != NULL)
		*lenp = 0;
	if ((r = sshbuf_get_string_direct(buf, &val, &len)) < 0)
		return r;
	if (valp != NULL) {
		if ((*valp = malloc(len + 1)) == NULL) {
			SSHBUF_DBG(("SSH_ERR_ALLOC_FAIL"));
			return SSH_ERR_ALLOC_FAIL;
		}
		if (len != 0)
			memcpy(*valp, val, len);
		(*valp)[len] = '\0';
	}
	if (lenp != NULL)
		*lenp = len;
	return 0;
}

int
sshbuf_get_string_direct(struct sshbuf *buf, const u_char **valp, size_t *lenp)
{
	size_t len;
	const u_char *p;
	int r;

	if (valp != NULL)
		*valp = NULL;
	if (lenp != NULL)
		*lenp = 0;
	if ((r = sshbuf_peek_string_direct(buf, &p, &len)) < 0)
		return r;
	if (valp != NULL)
		*valp = p;
	if (lenp != NULL)
		*lenp = len;
	if (sshbuf_consume(buf, len + 4) != 0) {
		/* Shouldn't happen */
		SSHBUF_DBG(("SSH_ERR_INTERNAL_ERROR"));
		SSHBUF_ABORT();
		return SSH_ERR_INTERNAL_ERROR;
	}
	return 0;
}

int
sshbuf_peek_string_direct(const struct sshbuf *buf, const u_char **valp,
    size_t *lenp)
{
	u_int32_t len;
	const u_char *p = sshbuf_ptr(buf);

	if (valp != NULL)
		*valp = NULL;
	if (lenp != NULL)
		*lenp = 0;
	if (sshbuf_len(buf) < 4) {
		SSHBUF_DBG(("SSH_ERR_MESSAGE_INCOMPLETE"));
		return SSH_ERR_MESSAGE_INCOMPLETE;
	}
	len = PEEK_U32(p);
	if (len > SSHBUF_SIZE_MAX - 4) {
		SSHBUF_DBG(("SSH_ERR_STRING_TOO_LARGE"));
		return SSH_ERR_STRING_TOO_LARGE;
	}
	if (sshbuf_len(buf) - 4 < len) {
		SSHBUF_DBG(("SSH_ERR_MESSAGE_INCOMPLETE"));
		return SSH_ERR_MESSAGE_INCOMPLETE;
	}
	if (valp != NULL)
		*valp = p + 4;
	if (lenp != NULL)
		*lenp = len;
	return 0;
}

int
sshbuf_get_cstring(struct sshbuf *buf, char **valp, size_t *lenp)
{
	size_t len;
	const u_char *p, *z;
	int r;

	if (valp != NULL)
		*valp = NULL;
	if (lenp != NULL)
		*lenp = 0;
	if ((r = sshbuf_peek_string_direct(buf, &p, &len)) != 0)
		return r;
	/* Allow a \0 only at the end of the string */
	if (len > 0 &&
	    (z = memchr(p , '\0', len)) != NULL && z < p + len - 1) {
		SSHBUF_DBG(("SSH_ERR_INVALID_FORMAT"));
		return SSH_ERR_INVALID_FORMAT;
	}
	if ((r = sshbuf_skip_string(buf)) != 0)
		return -1;
	if (valp != NULL) {
		if ((*valp = malloc(len + 1)) == NULL) {
			SSHBUF_DBG(("SSH_ERR_ALLOC_FAIL"));
			return SSH_ERR_ALLOC_FAIL;
		}
		if (len != 0)
			memcpy(*valp, p, len);
		(*valp)[len] = '\0';
	}
	if (lenp != NULL)
		*lenp = (size_t)len;
	return 0;
}

int
sshbuf_get_stringb(struct sshbuf *buf, struct sshbuf *v)
{
	u_int32_t len;
	u_char *p;
	int r;

	/*
	 * Use sshbuf_peek_string_direct() to figure out if there is
	 * a complete string in 'buf' and copy the string directly
	 * into 'v'.
	 */
	if ((r = sshbuf_peek_string_direct(buf, NULL, NULL)) != 0 ||
	    (r = sshbuf_get_u32(buf, &len)) != 0 ||
	    (r = sshbuf_reserve(v, len, &p)) != 0 ||
	    (r = sshbuf_get(buf, p, len)) != 0)
		return r;
	return 0;
}

int
sshbuf_put(struct sshbuf *buf, const void *v, size_t len)
{
	u_char *p;
	int r;

	if ((r = sshbuf_reserve(buf, len, &p)) < 0)
		return r;
	if (len != 0)
		memcpy(p, v, len);
	return 0;
}

int
sshbuf_putb(struct sshbuf *buf, const struct sshbuf *v)
{
	return sshbuf_put(buf, sshbuf_ptr(v), sshbuf_len(v));
}

int
sshbuf_putf(struct sshbuf *buf, const char *fmt, ...)
{
	va_list ap;
	int r;

	va_start(ap, fmt);
	r = sshbuf_putfv(buf, fmt, ap);
	va_end(ap);
	return r;
}

int
sshbuf_putfv(struct sshbuf *buf, const char *fmt, va_list ap)
{
	va_list ap2;
	int r, len;
	u_char *p;

	VA_COPY(ap2, ap);
	if ((len = vsnprintf(NULL, 0, fmt, ap2)) < 0) {
		r = SSH_ERR_INVALID_ARGUMENT;
		goto out;
	}
	if (len == 0) {
		r = 0;
		goto out; /* Nothing to do */
	}
	va_end(ap2);
	VA_COPY(ap2, ap);
	if ((r = sshbuf_reserve(buf, (size_t)len + 1, &p)) < 0)
		goto out;
	if ((r = vsnprintf((char *)p, len + 1, fmt, ap2)) != len) {
		r = SSH_ERR_INTERNAL_ERROR;
		goto out; /* Shouldn't happen */
	}
	/* Consume terminating \0 */
	if ((r = sshbuf_consume_end(buf, 1)) != 0)
		goto out;
	r = 0;
 out:
	va_end(ap2);
	return r;
}

int
sshbuf_put_u64(struct sshbuf *buf, u_int64_t val)
{
	u_char *p;
	int r;

	if ((r = sshbuf_reserve(buf, 8, &p)) < 0)
		return r;
	POKE_U64(p, val);
	return 0;
}

int
sshbuf_put_u32(struct sshbuf *buf, u_int32_t val)
{
	u_char *p;
	int r;

	if ((r = sshbuf_reserve(buf, 4, &p)) < 0)
		return r;
	POKE_U32(p, val);
	return 0;
}

int
sshbuf_put_u16(struct sshbuf *buf, u_int16_t val)
{
	u_char *p;
	int r;

	if ((r = sshbuf_reserve(buf, 2, &p)) < 0)
		return r;
	POKE_U16(p, val);
	return 0;
}

int
sshbuf_put_u8(struct sshbuf *buf, u_char val)
{
	u_char *p;
	int r;

	if ((r = sshbuf_reserve(buf, 1, &p)) < 0)
		return r;
	p[0] = val;
	return 0;
}

int
sshbuf_put_string(struct sshbuf *buf, const void *v, size_t len)
{
	u_char *d;
	int r;

	if (len > SSHBUF_SIZE_MAX - 4) {
		SSHBUF_DBG(("SSH_ERR_NO_BUFFER_SPACE"));
		return SSH_ERR_NO_BUFFER_SPACE;
	}
	if ((r = sshbuf_reserve(buf, len + 4, &d)) < 0)
		return r;
	POKE_U32(d, len);
	if (len != 0)
		memcpy(d + 4, v, len);
	return 0;
}

int
sshbuf_put_cstring(struct sshbuf *buf, const char *v)
{
	return sshbuf_put_string(buf, v, v == NULL ? 0 : strlen(v));
}

int
sshbuf_put_stringb(struct sshbuf *buf, const struct sshbuf *v)
{
	return sshbuf_put_string(buf, sshbuf_ptr(v), sshbuf_len(v));
}

int
sshbuf_froms(struct sshbuf *buf, struct sshbuf **bufp)
{
	const u_char *p;
	size_t len;
	struct sshbuf *ret;
	int r;

	if (buf == NULL || bufp == NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	*bufp = NULL;
	if ((r = sshbuf_peek_string_direct(buf, &p, &len)) != 0)
		return r;
	if ((ret = sshbuf_from(p, len)) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	if ((r = sshbuf_consume(buf, len + 4)) != 0 ||  /* Shouldn't happen */
	    (r = sshbuf_set_parent(ret, buf)) != 0) {
		sshbuf_free(ret);
		return r;
	}
	*bufp = ret;
	return 0;
}

int
sshbuf_put_bignum2_bytes(struct sshbuf *buf, const void *v, size_t len)
{
	u_char *d;
	const u_char *s = (const u_char *)v;
	int r, prepend;

	if (len > SSHBUF_SIZE_MAX - 5) {
		SSHBUF_DBG(("SSH_ERR_NO_BUFFER_SPACE"));
		return SSH_ERR_NO_BUFFER_SPACE;
	}
	/* Skip leading zero bytes */
	for (; len > 0 && *s == 0; len--, s++)
		;
	/*
	 * If most significant bit is set then prepend a zero byte to
	 * avoid interpretation as a negative number.
	 */
	prepend = len > 0 && (s[0] & 0x80) != 0;
	if ((r = sshbuf_reserve(buf, len + 4 + prepend, &d)) < 0)
		return r;
	POKE_U32(d, len + prepend);
	if (prepend)
		d[4] = 0;
	if (len != 0)
		memcpy(d + 4 + prepend, s, len);
	return 0;
}

int
sshbuf_get_bignum2_bytes_direct(struct sshbuf *buf,
    const u_char **valp, size_t *lenp)
{
	const u_char *d;
	size_t len, olen;
	int r;

	if ((r = sshbuf_peek_string_direct(buf, &d, &olen)) < 0)
		return r;
	len = olen;
	/* Refuse negative (MSB set) bignums */
	if ((len != 0 && (*d & 0x80) != 0))
		return SSH_ERR_BIGNUM_IS_NEGATIVE;
	/* Refuse overlong bignums, allow prepended \0 to avoid MSB set */
	if (len > SSHBUF_MAX_BIGNUM + 1 ||
	    (len == SSHBUF_MAX_BIGNUM + 1 && *d != 0))
		return SSH_ERR_BIGNUM_TOO_LARGE;
	/* Trim leading zeros */
	while (len > 0 && *d == 0x00) {
		d++;
		len--;
	}
	if (valp != NULL)
		*valp = d;
	if (lenp != NULL)
		*lenp = len;
	if (sshbuf_consume(buf, olen + 4) != 0) {
		/* Shouldn't happen */
		SSHBUF_DBG(("SSH_ERR_INTERNAL_ERROR"));
		SSHBUF_ABORT();
		return SSH_ERR_INTERNAL_ERROR;
	}
	return 0;
}

/*
 * store struct pwd
 */
int
sshbuf_put_passwd(struct sshbuf *buf, const struct passwd *pwent)
{
	int r;

	/*
	 * We never send pointer values of struct passwd.
	 * It is safe from wild pointer even if a new pointer member is added.
	 */

	if ((r = sshbuf_put_u64(buf, sizeof(*pwent)) != 0) ||
	    (r = sshbuf_put_cstring(buf, pwent->pw_name)) != 0 ||
	    (r = sshbuf_put_cstring(buf, "*")) != 0 ||
	    (r = sshbuf_put_u32(buf, pwent->pw_uid)) != 0 ||
	    (r = sshbuf_put_u32(buf, pwent->pw_gid)) != 0 ||
#ifdef HAVE_STRUCT_PASSWD_PW_CHANGE
	    (r = sshbuf_put_time(buf, pwent->pw_change)) != 0 ||
#endif
#ifdef HAVE_STRUCT_PASSWD_PW_GECOS
	    (r = sshbuf_put_cstring(buf, pwent->pw_gecos)) != 0 ||
#endif
#ifdef HAVE_STRUCT_PASSWD_PW_CLASS
	    (r = sshbuf_put_cstring(buf, pwent->pw_class)) != 0 ||
#endif
	    (r = sshbuf_put_cstring(buf, pwent->pw_dir)) != 0 ||
	    (r = sshbuf_put_cstring(buf, pwent->pw_shell)) != 0 ||
#ifdef HAVE_STRUCT_PASSWD_PW_EXPIRE
	    (r = sshbuf_put_time(buf, pwent->pw_expire)) != 0 ||
#endif
	    (r = sshbuf_put_u32(buf, pwent->pw_fields)) != 0) {
		return r;
	}
	return 0;
}

/*
 * extract struct pwd
 */
struct passwd *
sshbuf_get_passwd(struct sshbuf *buf)
{
	struct passwd *pw;
	u_int64_t len;
	int r;

	/* check if size of struct passwd is as same as sender's size */
	r = sshbuf_get_u64(buf, &len);
	if (r != 0 || len != sizeof(*pw))
		return NULL;

	pw = xcalloc(1, sizeof(*pw));
	if (sshbuf_get_cstring(buf, &pw->pw_name, NULL) != 0 ||
	    sshbuf_get_cstring(buf, &pw->pw_passwd, NULL) != 0 ||
	    sshbuf_get_u32(buf, &pw->pw_uid) != 0 ||
	    sshbuf_get_u32(buf, &pw->pw_gid) != 0 ||
#ifdef HAVE_STRUCT_PASSWD_PW_CHANGE
	    sshbuf_get_time(buf, &pw->pw_change) != 0 ||
#endif
#ifdef HAVE_STRUCT_PASSWD_PW_GECOS
	    sshbuf_get_cstring(buf, &pw->pw_gecos, NULL) != 0 ||
#endif
#ifdef HAVE_STRUCT_PASSWD_PW_CLASS
	    sshbuf_get_cstring(buf, &pw->pw_class, NULL) != 0 ||
#endif
	    sshbuf_get_cstring(buf, &pw->pw_dir, NULL) != 0 ||
	    sshbuf_get_cstring(buf, &pw->pw_shell, NULL) != 0 ||
#ifdef HAVE_STRUCT_PASSWD_PW_EXPIRE
	    sshbuf_get_time(buf, &pw->pw_expire) != 0 ||
#endif
	    sshbuf_get_u32(buf, &pw->pw_fields) != 0) {
		sshbuf_free_passwd(pw);
		return NULL;
	}
	return pw;
}

/*
 * free struct passwd obtained from sshbuf_get_passwd.
 */
void
sshbuf_free_passwd(struct passwd *pwent)
{
	if (pwent == NULL)
		return;
	free(pwent->pw_shell);
	free(pwent->pw_dir);
#ifdef HAVE_STRUCT_PASSWD_PW_CLASS
	free(pwent->pw_class);
#endif
#ifdef HAVE_STRUCT_PASSWD_PW_GECOS
	free(pwent->pw_gecos);
#endif
	free(pwent->pw_passwd);
	free(pwent->pw_name);
	free(pwent);
}
