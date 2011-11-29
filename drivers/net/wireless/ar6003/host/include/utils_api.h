//------------------------------------------------------------------------------
// <copyright file="utils_api.h" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Utility Macros & Functions common across OS.
// Author(s): ="Atheros"
//==============================================================================
#ifndef _HOST_UTILS_API_H_
#define _HOST_UTILS_API_H_

#ifdef __cplusplus
extern "C" {
#endif 

/* unaligned little endian access */
#define LE_READ_2(p)                            \
    ((A_UINT16)                            \
     ((((A_UINT8 *)(p))[0]      ) | (((A_UINT8 *)(p))[1] <<  8)))

#define LE_READ_4(p)                            \
    ((A_UINT32)                            \
     ((((A_UINT8 *)(p))[0]      ) | (((A_UINT8 *)(p))[1] <<  8) | \
      (((A_UINT8 *)(p))[2] << 16) | (((A_UINT8 *)(p))[3] << 24)))

#define WPA_GET_BE16(a) ((A_UINT16) (((a)[0] << 8) | (a)[1]))
#define WPA_PUT_BE16(a, val)			\
	do {					\
		(a)[0] = ((A_UINT16) (val)) >> 8;	\
		(a)[1] = ((A_UINT16) (val)) & 0xff;	\
	} while (0)

#define WPA_GET_LE16(a) ((A_UINT16) (((a)[1] << 8) | (a)[0]))
#define WPA_PUT_LE16(a, val)			\
	do {					\
		(a)[1] = ((A_UINT16) (val)) >> 8;	\
		(a)[0] = ((A_UINT16) (val)) & 0xff;	\
	} while (0)

#define WPA_GET_BE24(a) ((((A_UINT32) (a)[0]) << 16) | (((A_UINT32) (a)[1]) << 8) | \
			 ((A_UINT32) (a)[2]))
#define WPA_PUT_BE24(a, val)					\
	do {							\
		(a)[0] = (A_UINT8) ((((A_UINT32) (val)) >> 16) & 0xff);	\
		(a)[1] = (A_UINT8) ((((A_UINT32) (val)) >> 8) & 0xff);	\
		(a)[2] = (A_UINT8) (((A_UINT32) (val)) & 0xff);		\
	} while (0)

#define WPA_GET_BE32(a) ((((A_UINT32) (a)[0]) << 24) | (((A_UINT32) (a)[1]) << 16) | \
			 (((A_UINT32) (a)[2]) << 8) | ((A_UINT32) (a)[3]))
#define WPA_PUT_BE32(a, val)					\
	do {							\
		(a)[0] = (A_UINT8) ((((A_UINT32) (val)) >> 24) & 0xff);	\
		(a)[1] = (A_UINT8) ((((A_UINT32) (val)) >> 16) & 0xff);	\
		(a)[2] = (A_UINT8) ((((A_UINT32) (val)) >> 8) & 0xff);	\
		(a)[3] = (A_UINT8) (((A_UINT32) (val)) & 0xff);		\
	} while (0)

#define WPA_GET_LE32(a) ((((A_UINT32) (a)[3]) << 24) | (((A_UINT32) (a)[2]) << 16) | \
			 (((A_UINT32) (a)[1]) << 8) | ((A_UINT32) (a)[0]))
#define WPA_PUT_LE32(a, val)					\
	do {							\
		(a)[3] = (A_UINT8) ((((A_UINT32) (val)) >> 24) & 0xff);	\
		(a)[2] = (A_UINT8) ((((A_UINT32) (val)) >> 16) & 0xff);	\
		(a)[1] = (A_UINT8) ((((A_UINT32) (val)) >> 8) & 0xff);	\
		(a)[0] = (A_UINT8) (((A_UINT32) (val)) & 0xff);		\
	} while (0)


#define WPA_OUI             0xf25000

static int __inline
iswscoui(const A_UINT8 *frm)
{
    return frm[1] > 3 && LE_READ_4(frm+2) == ((0x04<<24)|WPA_OUI);
}

static inline int is_zero_mac_addr(const A_UINT8 *addr)
{
	return !(addr[0] | addr[1] | addr[2] | addr[3] | addr[4] | addr[5]);
}


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _HOST_UTILS_API_H_ */
