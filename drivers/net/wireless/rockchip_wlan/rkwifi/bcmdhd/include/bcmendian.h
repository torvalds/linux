/*
 * Byte order utilities
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 *
 * This file by default provides proper behavior on little-endian architectures.
 * On big-endian architectures, IL_BIGENDIAN should be defined.
 */

#ifndef _BCMENDIAN_H_
#define _BCMENDIAN_H_

#include <typedefs.h>

/* Reverse the bytes in a 16-bit value */
#define BCMSWAP16(val) \
	((uint16)((((uint16)(val) & (uint16)0x00ffU) << 8) | \
		  (((uint16)(val) & (uint16)0xff00U) >> 8)))

/* Reverse the bytes in a 32-bit value */
#define BCMSWAP32(val) \
	((uint32)((((uint32)(val) & (uint32)0x000000ffU) << 24) | \
		  (((uint32)(val) & (uint32)0x0000ff00U) <<  8) | \
		  (((uint32)(val) & (uint32)0x00ff0000U) >>  8) | \
		  (((uint32)(val) & (uint32)0xff000000U) >> 24)))

/* Reverse the two 16-bit halves of a 32-bit value */
#define BCMSWAP32BY16(val) \
	((uint32)((((uint32)(val) & (uint32)0x0000ffffU) << 16) | \
		  (((uint32)(val) & (uint32)0xffff0000U) >> 16)))

/* Reverse the bytes in a 64-bit value */
#define BCMSWAP64(val) \
	((uint64)((((uint64)(val) & 0x00000000000000ffULL) << 56) | \
	          (((uint64)(val) & 0x000000000000ff00ULL) << 40) | \
	          (((uint64)(val) & 0x0000000000ff0000ULL) << 24) | \
	          (((uint64)(val) & 0x00000000ff000000ULL) <<  8) | \
	          (((uint64)(val) & 0x000000ff00000000ULL) >>  8) | \
	          (((uint64)(val) & 0x0000ff0000000000ULL) >> 24) | \
	          (((uint64)(val) & 0x00ff000000000000ULL) >> 40) | \
	          (((uint64)(val) & 0xff00000000000000ULL) >> 56)))

/* Reverse the two 32-bit halves of a 64-bit value */
#define BCMSWAP64BY32(val) \
	((uint64)((((uint64)(val) & 0x00000000ffffffffULL) << 32) | \
	          (((uint64)(val) & 0xffffffff00000000ULL) >> 32)))

/* Byte swapping macros
 *    Host <=> Network (Big Endian) for 16- and 32-bit values
 *    Host <=> Little-Endian for 16- and 32-bit values
 */
#ifndef hton16
#ifndef IL_BIGENDIAN
#define HTON16(i) BCMSWAP16(i)
#define	hton16(i) bcmswap16(i)
#define	HTON32(i) BCMSWAP32(i)
#define	hton32(i) bcmswap32(i)
#define	NTOH16(i) BCMSWAP16(i)
#define	ntoh16(i) bcmswap16(i)
#define	NTOH32(i) BCMSWAP32(i)
#define	ntoh32(i) bcmswap32(i)
#define LTOH16(i) (i)
#define ltoh16(i) (i)
#define LTOH32(i) (i)
#define ltoh32(i) (i)
#define HTOL16(i) (i)
#define htol16(i) (i)
#define HTOL32(i) (i)
#define htol32(i) (i)
#define HTOL64(i) (i)
#define htol64(i) (i)
#else /* IL_BIGENDIAN */
#define HTON16(i) (i)
#define	hton16(i) (i)
#define	HTON32(i) (i)
#define	hton32(i) (i)
#define	NTOH16(i) (i)
#define	ntoh16(i) (i)
#define	NTOH32(i) (i)
#define	ntoh32(i) (i)
#define	LTOH16(i) BCMSWAP16(i)
#define	ltoh16(i) bcmswap16(i)
#define	LTOH32(i) BCMSWAP32(i)
#define	ltoh32(i) bcmswap32(i)
#define HTOL16(i) BCMSWAP16(i)
#define htol16(i) bcmswap16(i)
#define HTOL32(i) BCMSWAP32(i)
#define htol32(i) bcmswap32(i)
#define HTOL64(i) BCMSWAP64(i)
#define htol64(i) bcmswap64(i)
#endif /* IL_BIGENDIAN */
#endif /* hton16 */

#ifndef IL_BIGENDIAN
#define ltoh16_buf(buf, i)
#define htol16_buf(buf, i)
#define ltoh32_buf(buf, i)
#define htol32_buf(buf, i)
#define ltoh64_buf(buf, i)
#define htol64_buf(buf, i)
#else
#define ltoh16_buf(buf, i) bcmswap16_buf((uint16 *)(buf), (i))
#define htol16_buf(buf, i) bcmswap16_buf((uint16 *)(buf), (i))
#define ltoh32_buf(buf, i) bcmswap32_buf((uint16 *)(buf), (i))
#define htol32_buf(buf, i) bcmswap32_buf((uint16 *)(buf), (i))
#define ltoh64_buf(buf, i) bcmswap64_buf((uint16 *)(buf), (i))
#define htol64_buf(buf, i) bcmswap64_buf((uint16 *)(buf), (i))
#endif /* IL_BIGENDIAN */

/* Unaligned loads and stores in host byte order */
#ifndef IL_BIGENDIAN
#define load32_ua(a)		ltoh32_ua(a)
#define store32_ua(a, v)	htol32_ua_store(v, a)
#define load16_ua(a)		ltoh16_ua(a)
#define store16_ua(a, v)	htol16_ua_store(v, a)
#define load64_ua(a)		ltoh64_ua(a)
#define store64_ua(a, v)	htol64_ua_store(v, a)
#else
#define load32_ua(a)		ntoh32_ua(a)
#define store32_ua(a, v)	hton32_ua_store(v, a)
#define load16_ua(a)		ntoh16_ua(a)
#define store16_ua(a, v)	hton16_ua_store(v, a)
#define load64_ua(a)		ntoh64_ua(a)
#define store64_ua(a, v)	hton64_ua_store(v, a)
#endif /* IL_BIGENDIAN */

#define _LTOH16_UA(cp)	((uint16)(cp)[0] | ((uint16)(cp)[1] << 8))
#define _LTOH32_UA(cp)	((uint32)(cp)[0] | ((uint32)(cp)[1] << 8) | \
	((uint32)(cp)[2] << 16) | ((uint32)(cp)[3] << 24))
#define _NTOH16_UA(cp)	(((uint16)(cp)[0] << 8) | (uint16)(cp)[1])
#define _NTOH32_UA(cp)	(((uint32)(cp)[0] << 24) | ((uint32)(cp)[1] << 16) | \
	((uint32)(cp)[2] << 8) | (uint32)(cp)[3])

#define _LTOH64_UA(cp)	((uint64)(cp)[0] | ((uint64)(cp)[1] << 8) | \
	((uint64)(cp)[2] << 16) | ((uint64)(cp)[3] << 24) | \
	((uint64)(cp)[4] << 32) | ((uint64)(cp)[5] << 40) | \
	((uint64)(cp)[6] << 48) | ((uint64)(cp)[7] << 56))

#define _NTOH64_UA(cp)	((uint64)(cp)[7] | ((uint64)(cp)[6] << 8) | \
	((uint64)(cp)[5] << 16) | ((uint64)(cp)[4] << 24) | \
	((uint64)(cp)[3] << 32) | ((uint64)(cp)[2] << 40) | \
	((uint64)(cp)[1] << 48) | ((uint64)(cp)[0] << 56))

#define ltoh_ua(ptr) \
	(sizeof(*(ptr)) == sizeof(uint8) ? *(const uint8 *)(ptr) : \
	 sizeof(*(ptr)) == sizeof(uint16) ? (uint16)_LTOH16_UA((const uint8 *)(ptr)) : \
	 sizeof(*(ptr)) == sizeof(uint32) ? (uint32)_LTOH32_UA((const uint8 *)(ptr)) : \
	 *(uint8 *)0)

#define ntoh_ua(ptr) \
	(sizeof(*(ptr)) == sizeof(uint8) ? *(const uint8 *)(ptr) : \
	 sizeof(*(ptr)) == sizeof(uint16) ? (uint16)_NTOH16_UA((const uint8 *)(ptr)) : \
	 sizeof(*(ptr)) == sizeof(uint32) ? (uint32)_NTOH32_UA((const uint8 *)(ptr)) : \
	 *(uint8 *)0)

#ifdef __GNUC__

/* GNU macro versions avoid referencing the argument multiple times, while also
 * avoiding the -fno-inline used in ROM builds.
 */

#define bcmswap16(val) ({ \
	uint16 _val = (val); \
	BCMSWAP16(_val); \
})

#define bcmswap32(val) ({ \
	uint32 _val = (val); \
	BCMSWAP32(_val); \
})

#define bcmswap64(val) ({ \
	uint64 _val = (val); \
	BCMSWAP64(_val); \
})

#define bcmswap32by16(val) ({ \
	uint32 _val = (val); \
	BCMSWAP32BY16(_val); \
})

#define bcmswap16_buf(buf, len) ({ \
	uint16 *_buf = (uint16 *)(buf); \
	uint _wds = (len) / 2; \
	while (_wds--) { \
		*_buf = bcmswap16(*_buf); \
		_buf++; \
	} \
})

#define bcmswap32_buf(buf, len) ({ \
	uint32 *_buf = (uint32 *)(buf); \
	uint _wds = (len) / 4; \
	while (_wds--) { \
		*_buf = bcmswap32(*_buf); \
		_buf++; \
	} \
})

#define bcmswap64_buf(buf, len) ({ \
	uint64 *_buf = (uint64 *)(buf); \
	uint _wds = (len) / 8; \
	while (_wds--) { \
		*_buf = bcmswap64(*_buf); \
		_buf++; \
	} \
})

#define htol16_ua_store(val, bytes) ({ \
	uint16 _val = (val); \
	uint8 *_bytes = (uint8 *)(bytes); \
	_bytes[0] = _val & 0xff; \
	_bytes[1] = _val >> 8; \
})

#define htol32_ua_store(val, bytes) ({ \
	uint32 _val = (val); \
	uint8 *_bytes = (uint8 *)(bytes); \
	_bytes[0] = _val & 0xff; \
	_bytes[1] = (_val >> 8) & 0xff; \
	_bytes[2] = (_val >> 16) & 0xff; \
	_bytes[3] = _val >> 24; \
})

#define htol64_ua_store(val, bytes) ({ \
	uint64 _val = (val); \
	uint8 *_bytes = (uint8 *)(bytes); \
	int _ii; \
	for (_ii = 0; _ii < (int)sizeof(_val); ++_ii) { \
		*_bytes++ = _val & 0xff; \
		_val >>= 8; \
	} \
})

#define hton16_ua_store(val, bytes) ({ \
	uint16 _val = (val); \
	uint8 *_bytes = (uint8 *)(bytes); \
	_bytes[0] = _val >> 8; \
	_bytes[1] = _val & 0xff; \
})

#define hton32_ua_store(val, bytes) ({ \
	uint32 _val = (val); \
	uint8 *_bytes = (uint8 *)(bytes); \
	_bytes[0] = _val >> 24; \
	_bytes[1] = (_val >> 16) & 0xff; \
	_bytes[2] = (_val >> 8) & 0xff; \
	_bytes[3] = _val & 0xff; \
})

#define ltoh16_ua(bytes) ({ \
	const uint8 *_bytes = (const uint8 *)(bytes); \
	_LTOH16_UA(_bytes); \
})

#define ltoh32_ua(bytes) ({ \
	const uint8 *_bytes = (const uint8 *)(bytes); \
	_LTOH32_UA(_bytes); \
})

#define ltoh64_ua(bytes) ({ \
	const uint8 *_bytes = (const uint8 *)(bytes); \
	_LTOH64_UA(_bytes); \
})

#define ntoh16_ua(bytes) ({ \
	const uint8 *_bytes = (const uint8 *)(bytes); \
	_NTOH16_UA(_bytes); \
})

#define ntoh32_ua(bytes) ({ \
	const uint8 *_bytes = (const uint8 *)(bytes); \
	_NTOH32_UA(_bytes); \
})

#define ntoh64_ua(bytes) ({ \
	const uint8 *_bytes = (const uint8 *)(bytes); \
	_NTOH64_UA(_bytes); \
})

#else /* !__GNUC__ */

/* Inline versions avoid referencing the argument multiple times */
static INLINE uint16
bcmswap16(uint16 val)
{
	return BCMSWAP16(val);
}

static INLINE uint32
bcmswap32(uint32 val)
{
	return BCMSWAP32(val);
}

static INLINE uint64
bcmswap64(uint64 val)
{
	return BCMSWAP64(val);
}

static INLINE uint32
bcmswap32by16(uint32 val)
{
	return BCMSWAP32BY16(val);
}

/* Reverse pairs of bytes in a buffer (not for high-performance use) */
/* buf	- start of buffer of shorts to swap */
/* len  - byte length of buffer */
static INLINE void
bcmswap16_buf(uint16 *buf, uint len)
{
	len = len / 2;

	while (len--) {
		*buf = bcmswap16(*buf);
		buf++;
	}
}

/*
 * Store 16-bit value to unaligned little-endian byte array.
 */
static INLINE void
htol16_ua_store(uint16 val, uint8 *bytes)
{
	bytes[0] = val & 0xff;
	bytes[1] = val >> 8;
}

/*
 * Store 32-bit value to unaligned little-endian byte array.
 */
static INLINE void
htol32_ua_store(uint32 val, uint8 *bytes)
{
	bytes[0] = val & 0xff;
	bytes[1] = (val >> 8) & 0xff;
	bytes[2] = (val >> 16) & 0xff;
	bytes[3] = val >> 24;
}

/*
 * Store 64-bit value to unaligned little-endian byte array.
 */
static INLINE void
htol64_ua_store(uint64 val, uint8 *bytes)
{
	int i;
	for (i = 0; i < sizeof(val); ++i) {
		*bytes++ = (uint8)(val & 0xff);
		val >>= 8;
	}
}

/*
 * Store 16-bit value to unaligned network-(big-)endian byte array.
 */
static INLINE void
hton16_ua_store(uint16 val, uint8 *bytes)
{
	bytes[0] = val >> 8;
	bytes[1] = val & 0xff;
}

/*
 * Store 32-bit value to unaligned network-(big-)endian byte array.
 */
static INLINE void
hton32_ua_store(uint32 val, uint8 *bytes)
{
	bytes[0] = val >> 24;
	bytes[1] = (val >> 16) & 0xff;
	bytes[2] = (val >> 8) & 0xff;
	bytes[3] = val & 0xff;
}

/*
 * Load 16-bit value from unaligned little-endian byte array.
 */
static INLINE uint16
ltoh16_ua(const void *bytes)
{
	return _LTOH16_UA((const uint8 *)bytes);
}

/*
 * Load 32-bit value from unaligned little-endian byte array.
 */
static INLINE uint32
ltoh32_ua(const void *bytes)
{
	return _LTOH32_UA((const uint8 *)bytes);
}

/*
 * Load 64-bit value from unaligned little-endian byte array.
 */
static INLINE uint64
ltoh64_ua(const void *bytes)
{
	return _LTOH64_UA((const uint8 *)bytes);
}

/*
 * Load 16-bit value from unaligned big-(network-)endian byte array.
 */
static INLINE uint16
ntoh16_ua(const void *bytes)
{
	return _NTOH16_UA((const uint8 *)bytes);
}

/*
 * Load 32-bit value from unaligned big-(network-)endian byte array.
 */
static INLINE uint32
ntoh32_ua(const void *bytes)
{
	return _NTOH32_UA((const uint8 *)bytes);
}

/*
 * Load 64-bit value from unaligned big-(network-)endian byte array.
 */
static INLINE uint64
ntoh64_ua(const void *bytes)
{
	return _NTOH64_UA((const uint8 *)bytes);
}

#endif /* !__GNUC__ */
#endif /* !_BCMENDIAN_H_ */
