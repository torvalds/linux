/*
 * Copyright (c) 2000, Boris Popov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 *
 * $Id: mbuf.c,v 1.6 2001/02/24 15:56:04 bp Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/endian.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netsmb/smb_lib.h>

#define MBERROR(format, args...) printf("%s(%d): "format, __FUNCTION__ , \
				    __LINE__ ,## args)

static int
m_get(size_t len, struct mbuf **mpp)
{
	struct mbuf *m;

	len = M_ALIGN(len);
	if (len < M_MINSIZE)
		len = M_MINSIZE;
	m = malloc(M_BASESIZE + len);
	if (m == NULL)
		return ENOMEM;
	bzero(m, M_BASESIZE + len);
	m->m_maxlen = len;
	m->m_data = M_TOP(m);
	*mpp = m;
	return 0;
}

static void
m_free(struct mbuf *m)
{
	free(m);
}

static void
m_freem(struct mbuf *m0)
{
	struct mbuf *m;

	while (m0) {
		m = m0->m_next;
		m_free(m0);
		m0 = m;
	}
}

static size_t
m_totlen(struct mbuf *m0)
{
	struct mbuf *m = m0;
	int len = 0;

	while (m) {
		len += m->m_len;
		m = m->m_next;
	}
	return len;
}

int
m_lineup(struct mbuf *m0, struct mbuf **mpp)
{
	struct mbuf *nm, *m;
	char *dp;
	size_t len;
	int error;

	if (m0->m_next == NULL) {
		*mpp = m0;
		return 0;
	}
	if ((error = m_get(m_totlen(m0), &nm)) != 0)
		return error;
	dp = mtod(nm, char *);
	while (m0) {
		len = m0->m_len;
		bcopy(m0->m_data, dp, len);
		dp += len;
		m = m0->m_next;
		m_free(m0);
		m0 = m;
	}
	*mpp = nm;
	return 0;
}

int
mb_init(struct mbdata *mbp, size_t size)
{
	struct mbuf *m;
	int error;

	if ((error = m_get(size, &m)) != 0)
		return error;
	return mb_initm(mbp, m);
}

int
mb_initm(struct mbdata *mbp, struct mbuf *m)
{
	bzero(mbp, sizeof(*mbp));
	mbp->mb_top = mbp->mb_cur = m;
	mbp->mb_pos = mtod(m, char *);
	return 0;
}

int
mb_done(struct mbdata *mbp)
{
	if (mbp->mb_top) {
		m_freem(mbp->mb_top);
		mbp->mb_top = NULL;
	}
	return 0;
}

/*
int
mb_fixhdr(struct mbdata *mbp)
{
	struct mbuf *m = mbp->mb_top;
	int len = 0;

	while (m) {
		len += m->m_len;
		m = m->m_next;
	}
	mbp->mb_top->m_pkthdr.len = len;
	return len;
}
*/
int
m_getm(struct mbuf *top, size_t len, struct mbuf **mpp)
{
	struct mbuf *m, *mp;
	int error;
	
	for (mp = top; ; mp = mp->m_next) {
		len -= M_TRAILINGSPACE(mp);
		if (mp->m_next == NULL)
			break;
		
	}
	if (len > 0) {
		if ((error = m_get(len, &m)) != 0)
			return error;
		mp->m_next = m;
	}
	*mpp = top;
	return 0;
}

/*
 * Routines to put data in a buffer
 */
#define	MB_PUT(t)	int error; t *p; \
			if ((error = mb_fit(mbp, sizeof(t), (char**)&p)) != 0) \
				return error

/*
 * Check if object of size 'size' fit to the current position and
 * allocate new mbuf if not. Advance pointers and increase length of mbuf(s).
 * Return pointer to the object placeholder or NULL if any error occured.
 */
int
mb_fit(struct mbdata *mbp, size_t size, char **pp)
{
	struct mbuf *m, *mn;
	int error;

	m = mbp->mb_cur;
	if (M_TRAILINGSPACE(m) < (int)size) {
		if ((error = m_get(size, &mn)) != 0)
			return error;
		mbp->mb_pos = mtod(mn, char *);
		mbp->mb_cur = m->m_next = mn;
		m = mn;
	}
	m->m_len += size;
	*pp = mbp->mb_pos;
	mbp->mb_pos += size;
	mbp->mb_count += size;
	return 0;
}

int
mb_put_uint8(struct mbdata *mbp, u_int8_t x)
{
	MB_PUT(u_int8_t);
	*p = x;
	return 0;
}

int
mb_put_uint16be(struct mbdata *mbp, u_int16_t x)
{
	MB_PUT(u_int16_t);
	setwbe(p, 0, x);
	return 0;
}

int
mb_put_uint16le(struct mbdata *mbp, u_int16_t x)
{
	MB_PUT(u_int16_t);
	setwle(p, 0, x);
	return 0;
}

int
mb_put_uint32be(struct mbdata *mbp, u_int32_t x)
{
	MB_PUT(u_int32_t);
	setdbe(p, 0, x);
	return 0;
}

int
mb_put_uint32le(struct mbdata *mbp, u_int32_t x)
{
	MB_PUT(u_int32_t);
	setdle(p, 0, x);
	return 0;
}

int
mb_put_int64be(struct mbdata *mbp, int64_t x)
{
	MB_PUT(int64_t);
	*p = htobe64(x);
	return 0;
}

int
mb_put_int64le(struct mbdata *mbp, int64_t x)
{
	MB_PUT(int64_t);
	*p = htole64(x);
	return 0;
}

int
mb_put_mem(struct mbdata *mbp, const char *source, size_t size)
{
	struct mbuf *m;
	char * dst;
	size_t cplen;
	int error;

	if (size == 0)
		return 0;
	m = mbp->mb_cur;
	if ((error = m_getm(m, size, &m)) != 0)
		return error;
	while (size > 0) {
		cplen = M_TRAILINGSPACE(m);
		if (cplen == 0) {
			m = m->m_next;
			continue;
		}
		if (cplen > size)
			cplen = size;
		dst = mtod(m, char *) + m->m_len;
		if (source) {
			bcopy(source, dst, cplen);
			source += cplen;
		} else
			bzero(dst, cplen);
		size -= cplen;
		m->m_len += cplen;
		mbp->mb_count += cplen;
	}
	mbp->mb_pos = mtod(m, char *) + m->m_len;
	mbp->mb_cur = m;
	return 0;
}

int
mb_put_mbuf(struct mbdata *mbp, struct mbuf *m)
{
	mbp->mb_cur->m_next = m;
	while (m) {
		mbp->mb_count += m->m_len;
		if (m->m_next == NULL)
			break;
		m = m->m_next;
	}
	mbp->mb_pos = mtod(m, char *) + m->m_len;
	mbp->mb_cur = m;
	return 0;
}

int 
mb_put_pstring(struct mbdata *mbp, const char *s)
{
	int error, len = strlen(s);

	if (len > 255) {
		len = 255;
	}
	if ((error = mb_put_uint8(mbp, len)) != 0)
		return error;
	return mb_put_mem(mbp, s, len);
}

/*
 * Routines for fetching data from an mbuf chain
 */
#define mb_left(m,p)	(mtod(m, char *) + (m)->m_len - (p))

int
mb_get_uint8(struct mbdata *mbp, u_int8_t *x)
{
	return mb_get_mem(mbp, x, 1);
}

int
mb_get_uint16(struct mbdata *mbp, u_int16_t *x)
{
	return mb_get_mem(mbp, (char *)x, 2);
}

int
mb_get_uint16le(struct mbdata *mbp, u_int16_t *x)
{
	u_int16_t v;
	int error = mb_get_uint16(mbp, &v);

	*x = le16toh(v);
	return error;
}

int
mb_get_uint16be(struct mbdata *mbp, u_int16_t *x) {
	u_int16_t v;
	int error = mb_get_uint16(mbp, &v);

	*x = be16toh(v);
	return error;
}

int
mb_get_uint32(struct mbdata *mbp, u_int32_t *x)
{
	return mb_get_mem(mbp, (char *)x, 4);
}

int
mb_get_uint32be(struct mbdata *mbp, u_int32_t *x)
{
	u_int32_t v;
	int error;

	error = mb_get_uint32(mbp, &v);
	*x = be32toh(v);
	return error;
}

int
mb_get_uint32le(struct mbdata *mbp, u_int32_t *x)
{
	u_int32_t v;
	int error;

	error = mb_get_uint32(mbp, &v);
	*x = le32toh(v);
	return error;
}

int
mb_get_int64(struct mbdata *mbp, int64_t *x)
{
	return mb_get_mem(mbp, (char *)x, 8);
}

int
mb_get_int64be(struct mbdata *mbp, int64_t *x)
{
	int64_t v;
	int error;

	error = mb_get_int64(mbp, &v);
	*x = be64toh(v);
	return error;
}

int
mb_get_int64le(struct mbdata *mbp, int64_t *x)
{
	int64_t v;
	int error;

	error = mb_get_int64(mbp, &v);
	*x = le64toh(v);
	return error;
}

int
mb_get_mem(struct mbdata *mbp, char * target, size_t size)
{
	struct mbuf *m = mbp->mb_cur;
	u_int count;
	
	while (size > 0) {
		if (m == NULL) {
			MBERROR("incomplete copy\n");
			return EBADRPC;
		}
		count = mb_left(m, mbp->mb_pos);
		if (count == 0) {
			mbp->mb_cur = m = m->m_next;
			if (m)
				mbp->mb_pos = mtod(m, char *);
			continue;
		}
		if (count > size)
			count = size;
		size -= count;
		if (target) {
			if (count == 1) {
				*target++ = *mbp->mb_pos;
			} else {
				bcopy(mbp->mb_pos, target, count);
				target += count;
			}
		}
		mbp->mb_pos += count;
	}
	return 0;
}
