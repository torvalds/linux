/*-
 * Copyright (c) 2002 Thomas Moestl <tmm@FreeBSD.org>
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
 * $FreeBSD$
 *
 * Borrowed from FreeBSD's <sys/endian.h>
 */

#ifndef __LIBARCHIVE_BUILD
#error This header is only to be used internally to libarchive.
#endif

/* Note:  This is a purely internal header! */
/* Do not use this outside of libarchive internal code! */

#ifndef ARCHIVE_ENDIAN_H_INCLUDED
#define ARCHIVE_ENDIAN_H_INCLUDED


/*
 * Disabling inline keyword for compilers known to choke on it:
 * - Watcom C++ in C code.  (For any version?)
 * - SGI MIPSpro
 * - Microsoft Visual C++ 6.0 (supposedly newer versions too)
 * - IBM VisualAge 6 (XL v6)
 * - Sun WorkShop C (SunPro) before 5.9
 */
#if defined(__WATCOMC__) || defined(__sgi) || defined(__hpux) || defined(__BORLANDC__)
#define	inline
#elif defined(__IBMC__) && __IBMC__ < 700
#define	inline
#elif defined(__SUNPRO_C) && __SUNPRO_C < 0x590
#define inline
#elif defined(_MSC_VER) || defined(__osf__)
#define inline __inline
#endif

/* Alignment-agnostic encode/decode bytestream to/from little/big endian. */

static inline uint16_t
archive_be16dec(const void *pp)
{
	unsigned char const *p = (unsigned char const *)pp;

	/* Store into unsigned temporaries before left shifting, to avoid
	promotion to signed int and then left shifting into the sign bit,
	which is undefined behaviour. */
	unsigned int p1 = p[1];
	unsigned int p0 = p[0];

	return ((p0 << 8) | p1);
}

static inline uint32_t
archive_be32dec(const void *pp)
{
	unsigned char const *p = (unsigned char const *)pp;

	/* Store into unsigned temporaries before left shifting, to avoid
	promotion to signed int and then left shifting into the sign bit,
	which is undefined behaviour. */
	unsigned int p3 = p[3];
	unsigned int p2 = p[2];
	unsigned int p1 = p[1];
	unsigned int p0 = p[0];

	return ((p0 << 24) | (p1 << 16) | (p2 << 8) | p3);
}

static inline uint64_t
archive_be64dec(const void *pp)
{
	unsigned char const *p = (unsigned char const *)pp;

	return (((uint64_t)archive_be32dec(p) << 32) | archive_be32dec(p + 4));
}

static inline uint16_t
archive_le16dec(const void *pp)
{
	unsigned char const *p = (unsigned char const *)pp;

	/* Store into unsigned temporaries before left shifting, to avoid
	promotion to signed int and then left shifting into the sign bit,
	which is undefined behaviour. */
	unsigned int p1 = p[1];
	unsigned int p0 = p[0];

	return ((p1 << 8) | p0);
}

static inline uint32_t
archive_le32dec(const void *pp)
{
	unsigned char const *p = (unsigned char const *)pp;

	/* Store into unsigned temporaries before left shifting, to avoid
	promotion to signed int and then left shifting into the sign bit,
	which is undefined behaviour. */
	unsigned int p3 = p[3];
	unsigned int p2 = p[2];
	unsigned int p1 = p[1];
	unsigned int p0 = p[0];

	return ((p3 << 24) | (p2 << 16) | (p1 << 8) | p0);
}

static inline uint64_t
archive_le64dec(const void *pp)
{
	unsigned char const *p = (unsigned char const *)pp;

	return (((uint64_t)archive_le32dec(p + 4) << 32) | archive_le32dec(p));
}

static inline void
archive_be16enc(void *pp, uint16_t u)
{
	unsigned char *p = (unsigned char *)pp;

	p[0] = (u >> 8) & 0xff;
	p[1] = u & 0xff;
}

static inline void
archive_be32enc(void *pp, uint32_t u)
{
	unsigned char *p = (unsigned char *)pp;

	p[0] = (u >> 24) & 0xff;
	p[1] = (u >> 16) & 0xff;
	p[2] = (u >> 8) & 0xff;
	p[3] = u & 0xff;
}

static inline void
archive_be64enc(void *pp, uint64_t u)
{
	unsigned char *p = (unsigned char *)pp;

	archive_be32enc(p, (uint32_t)(u >> 32));
	archive_be32enc(p + 4, (uint32_t)(u & 0xffffffff));
}

static inline void
archive_le16enc(void *pp, uint16_t u)
{
	unsigned char *p = (unsigned char *)pp;

	p[0] = u & 0xff;
	p[1] = (u >> 8) & 0xff;
}

static inline void
archive_le32enc(void *pp, uint32_t u)
{
	unsigned char *p = (unsigned char *)pp;

	p[0] = u & 0xff;
	p[1] = (u >> 8) & 0xff;
	p[2] = (u >> 16) & 0xff;
	p[3] = (u >> 24) & 0xff;
}

static inline void
archive_le64enc(void *pp, uint64_t u)
{
	unsigned char *p = (unsigned char *)pp;

	archive_le32enc(p, (uint32_t)(u & 0xffffffff));
	archive_le32enc(p + 4, (uint32_t)(u >> 32));
}

#endif
