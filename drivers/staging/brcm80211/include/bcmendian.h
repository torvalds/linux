/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _BCMENDIAN_H_
#define _BCMENDIAN_H_

/* Reverse the bytes in a 16-bit value */
#define BCMSWAP16(val) \
	((u16)((((u16)(val) & (u16)0x00ffU) << 8) | \
		  (((u16)(val) & (u16)0xff00U) >> 8)))

/* Reverse the bytes in a 32-bit value */
#define BCMSWAP32(val) \
	((u32)((((u32)(val) & (u32)0x000000ffU) << 24) | \
		  (((u32)(val) & (u32)0x0000ff00U) <<  8) | \
		  (((u32)(val) & (u32)0x00ff0000U) >>  8) | \
		  (((u32)(val) & (u32)0xff000000U) >> 24)))

/* Reverse the two 16-bit halves of a 32-bit value */
#define BCMSWAP32BY16(val) \
	((u32)((((u32)(val) & (u32)0x0000ffffU) << 16) | \
		  (((u32)(val) & (u32)0xffff0000U) >> 16)))

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
#else				/* IL_BIGENDIAN */
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
#endif				/* IL_BIGENDIAN */
#endif				/* hton16 */

#ifndef IL_BIGENDIAN
#define ltoh16_buf(buf, i)
#define htol16_buf(buf, i)
#else
#define ltoh16_buf(buf, i) bcmswap16_buf((u16 *)(buf), (i))
#define htol16_buf(buf, i) bcmswap16_buf((u16 *)(buf), (i))
#endif				/* IL_BIGENDIAN */

/* Unaligned loads and stores in host byte order */
#ifndef IL_BIGENDIAN
#define load32_ua(a)		ltoh32_ua(a)
#define store32_ua(a, v)	htol32_ua_store(v, a)
#define load16_ua(a)		ltoh16_ua(a)
#define store16_ua(a, v)	htol16_ua_store(v, a)
#else
#define load32_ua(a)		ntoh32_ua(a)
#define store32_ua(a, v)	hton32_ua_store(v, a)
#define load16_ua(a)		ntoh16_ua(a)
#define store16_ua(a, v)	hton16_ua_store(v, a)
#endif				/* IL_BIGENDIAN */

#define _LTOH16_UA(cp)	((cp)[0] | ((cp)[1] << 8))
#define _LTOH32_UA(cp)	((cp)[0] | ((cp)[1] << 8) | ((cp)[2] << 16) | ((cp)[3] << 24))
#define _NTOH16_UA(cp)	(((cp)[0] << 8) | (cp)[1])
#define _NTOH32_UA(cp)	(((cp)[0] << 24) | ((cp)[1] << 16) | ((cp)[2] << 8) | (cp)[3])

#define ltoh_ua(ptr) \
	(sizeof(*(ptr)) == sizeof(u8) ? *(const u8 *)(ptr) : \
	 sizeof(*(ptr)) == sizeof(u16) ? _LTOH16_UA((const u8 *)(ptr)) : \
	 sizeof(*(ptr)) == sizeof(u32) ? _LTOH32_UA((const u8 *)(ptr)) : \
	 *(u8 *)0)

#define ntoh_ua(ptr) \
	(sizeof(*(ptr)) == sizeof(u8) ? *(const u8 *)(ptr) : \
	 sizeof(*(ptr)) == sizeof(u16) ? _NTOH16_UA((const u8 *)(ptr)) : \
	 sizeof(*(ptr)) == sizeof(u32) ? _NTOH32_UA((const u8 *)(ptr)) : \
	 *(u8 *)0)

#ifdef __GNUC__

/* GNU macro versions avoid referencing the argument multiple times, while also
 * avoiding the -fno-inline used in ROM builds.
 */

#define bcmswap16(val) ({ \
	u16 _val = (val); \
	BCMSWAP16(_val); \
})

#define bcmswap32(val) ({ \
	u32 _val = (val); \
	BCMSWAP32(_val); \
})

#define bcmswap32by16(val) ({ \
	u32 _val = (val); \
	BCMSWAP32BY16(_val); \
})

#define bcmswap16_buf(buf, len) ({ \
	u16 *_buf = (u16 *)(buf); \
	uint _wds = (len) / 2; \
	while (_wds--) { \
		*_buf = bcmswap16(*_buf); \
		_buf++; \
	} \
})

#define htol16_ua_store(val, bytes) ({ \
	u16 _val = (val); \
	u8 *_bytes = (u8 *)(bytes); \
	_bytes[0] = _val & 0xff; \
	_bytes[1] = _val >> 8; \
})

#define htol32_ua_store(val, bytes) ({ \
	u32 _val = (val); \
	u8 *_bytes = (u8 *)(bytes); \
	_bytes[0] = _val & 0xff; \
	_bytes[1] = (_val >> 8) & 0xff; \
	_bytes[2] = (_val >> 16) & 0xff; \
	_bytes[3] = _val >> 24; \
})

#define hton16_ua_store(val, bytes) ({ \
	u16 _val = (val); \
	u8 *_bytes = (u8 *)(bytes); \
	_bytes[0] = _val >> 8; \
	_bytes[1] = _val & 0xff; \
})

#define hton32_ua_store(val, bytes) ({ \
	u32 _val = (val); \
	u8 *_bytes = (u8 *)(bytes); \
	_bytes[0] = _val >> 24; \
	_bytes[1] = (_val >> 16) & 0xff; \
	_bytes[2] = (_val >> 8) & 0xff; \
	_bytes[3] = _val & 0xff; \
})

#define ltoh16_ua(bytes) ({ \
	const u8 *_bytes = (const u8 *)(bytes); \
	_LTOH16_UA(_bytes); \
})

#define ltoh32_ua(bytes) ({ \
	const u8 *_bytes = (const u8 *)(bytes); \
	_LTOH32_UA(_bytes); \
})

#define ntoh16_ua(bytes) ({ \
	const u8 *_bytes = (const u8 *)(bytes); \
	_NTOH16_UA(_bytes); \
})

#define ntoh32_ua(bytes) ({ \
	const u8 *_bytes = (const u8 *)(bytes); \
	_NTOH32_UA(_bytes); \
})

#else				/* !__GNUC__ */

/* Inline versions avoid referencing the argument multiple times */
static inline u16 bcmswap16(u16 val)
{
	return BCMSWAP16(val);
}

static inline u32 bcmswap32(u32 val)
{
	return BCMSWAP32(val);
}

static inline u32 bcmswap32by16(u32 val)
{
	return BCMSWAP32BY16(val);
}

/* Reverse pairs of bytes in a buffer (not for high-performance use) */
/* buf	- start of buffer of shorts to swap */
/* len  - byte length of buffer */
static inline void bcmswap16_buf(u16 *buf, uint len)
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
static inline void htol16_ua_store(u16 val, u8 *bytes)
{
	bytes[0] = val & 0xff;
	bytes[1] = val >> 8;
}

/*
 * Store 32-bit value to unaligned little-endian byte array.
 */
static inline void htol32_ua_store(u32 val, u8 *bytes)
{
	bytes[0] = val & 0xff;
	bytes[1] = (val >> 8) & 0xff;
	bytes[2] = (val >> 16) & 0xff;
	bytes[3] = val >> 24;
}

/*
 * Store 16-bit value to unaligned network-(big-)endian byte array.
 */
static inline void hton16_ua_store(u16 val, u8 *bytes)
{
	bytes[0] = val >> 8;
	bytes[1] = val & 0xff;
}

/*
 * Store 32-bit value to unaligned network-(big-)endian byte array.
 */
static inline void hton32_ua_store(u32 val, u8 *bytes)
{
	bytes[0] = val >> 24;
	bytes[1] = (val >> 16) & 0xff;
	bytes[2] = (val >> 8) & 0xff;
	bytes[3] = val & 0xff;
}

/*
 * Load 16-bit value from unaligned little-endian byte array.
 */
static inline u16 ltoh16_ua(const void *bytes)
{
	return _LTOH16_UA((const u8 *)bytes);
}

/*
 * Load 32-bit value from unaligned little-endian byte array.
 */
static inline u32 ltoh32_ua(const void *bytes)
{
	return _LTOH32_UA((const u8 *)bytes);
}

/*
 * Load 16-bit value from unaligned big-(network-)endian byte array.
 */
static inline u16 ntoh16_ua(const void *bytes)
{
	return _NTOH16_UA((const u8 *)bytes);
}

/*
 * Load 32-bit value from unaligned big-(network-)endian byte array.
 */
static inline u32 ntoh32_ua(const void *bytes)
{
	return _NTOH32_UA((const u8 *)bytes);
}

#endif				/* !__GNUC__ */
#endif				/* !_BCMENDIAN_H_ */
