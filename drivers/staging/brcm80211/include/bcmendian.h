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

#ifndef IL_BIGENDIAN
#define ltoh16_buf(buf, i)
#define htol16_buf(buf, i)
#else
#define ltoh16_buf(buf, i) bcmswap16_buf((u16 *)(buf), (i))
#define htol16_buf(buf, i) bcmswap16_buf((u16 *)(buf), (i))
#endif				/* IL_BIGENDIAN */

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

#endif				/* !__GNUC__ */
#endif				/* !_BCMENDIAN_H_ */
