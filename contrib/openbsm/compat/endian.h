/*-
 * Copyright (c) 2002 Thomas Moestl <tmm@FreeBSD.org>
 * Copyright (c) 2005 Robert N. M. Watson
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
 * Derived from FreeBSD src/sys/sys/endian.h:1.6.
 */

#ifndef _COMPAT_ENDIAN_H_
#define _COMPAT_ENDIAN_H_

/*
 * Some systems will have the uint/int types defined here already, others
 * will need stdint.h.
 */
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

/*
 * Some operating systems do not yet have the more recent endian APIs that
 * permit encoding to and decoding from byte streams.  For those systems, we
 * implement local non-optimized versions.
 */

static __inline uint16_t
bswap16(uint16_t int16)
{
	const unsigned char *from;
	unsigned char *to;
	uint16_t t;

	from = (const unsigned char *) &int16;
	to = (unsigned char *) &t;

	to[0] = from[1];
	to[1] = from[0];

	return (t);
}

static __inline uint32_t
bswap32(uint32_t int32)
{
	const unsigned char *from;
	unsigned char *to;
	uint32_t t;

	from = (const unsigned char *) &int32;
	to = (unsigned char *) &t;

	to[0] = from[3];
	to[1] = from[2];
	to[2] = from[1];
	to[3] = from[0];

	return (t);
}

static __inline uint64_t
bswap64(uint64_t int64)
{
	const unsigned char *from;
	unsigned char *to;
	uint64_t t;

	from = (const unsigned char *) &int64;
	to = (unsigned char *) &t;

	to[0] = from[7];
	to[1] = from[6];
	to[2] = from[5];
	to[3] = from[4];
	to[4] = from[3];
	to[5] = from[2];
	to[6] = from[1];
	to[7] = from[0];

	return (t);
}

#if defined(BYTE_ORDER) && !defined(_BYTE_ORDER)
#define	_BYTE_ORDER	BYTE_ORDER
#endif
#if !defined(_BYTE_ORDER)
#error "Neither BYTE_ORDER nor _BYTE_ORDER defined"
#endif

#if defined(BIG_ENDIAN) && !defined(_BIG_ENDIAN)
#define	_BIG_ENDIAN	BIG_ENDIAN
#endif

#if defined(LITTLE_ENDIAN) && !defined(_LITTLE_ENDIAN)
#define	_LITTLE_ENDIAN	LITTLE_ENDIAN
#endif

/* XXX: Hack. */
#ifndef htobe16
/*
 * Host to big endian, host to little endian, big endian to host, and little
 * endian to host byte order functions as detailed in byteorder(9).
 */
#if _BYTE_ORDER == _LITTLE_ENDIAN
#define	htobe16(x)	bswap16((x))
#define	htobe32(x)	bswap32((x))
#define	htobe64(x)	bswap64((x))
#define	htole16(x)	((uint16_t)(x))
#define	htole32(x)	((uint32_t)(x))
#define	htole64(x)	((uint64_t)(x))

#define	be16toh(x)	bswap16((x))
#define	be32toh(x)	bswap32((x))
#define	be64toh(x)	bswap64((x))
#define	le16toh(x)	((uint16_t)(x))
#define	le32toh(x)	((uint32_t)(x))
#define	le64toh(x)	((uint64_t)(x))
#else /* _BYTE_ORDER != _LITTLE_ENDIAN */
#define	htobe16(x)	((uint16_t)(x))
#define	htobe32(x)	((uint32_t)(x))
#define	htobe64(x)	((uint64_t)(x))
#define	htole16(x)	bswap16((x))
#define	htole32(x)	bswap32((x))
#define	htole64(x)	bswap64((x))

#define	be16toh(x)	((uint16_t)(x))
#define	be32toh(x)	((uint32_t)(x))
#define	be64toh(x)	((uint64_t)(x))
#define	le16toh(x)	bswap16((x))
#define	le32toh(x)	bswap32((x))
#define	le64toh(x)	bswap64((x))
#endif /* _BYTE_ORDER == _LITTLE_ENDIAN */
#endif

#endif	/* _COMPAT_ENDIAN_H_ */
