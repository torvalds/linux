/*
 * Byte order utilities
 *
 * Copyright (C) 1999-2010, Broadcom Corporation
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
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *  $Id: bcmendian.h,v 1.31.302.1.16.1 2009/02/03 18:34:31 Exp $
 *
 * This file by default provides proper behavior on little-endian architectures.
 * On big-endian architectures, IL_BIGENDIAN should be defined.
 */


#ifndef _BCMENDIAN_H_
#define _BCMENDIAN_H_

#include <typedefs.h>


#define BCMSWAP16(val) \
	((uint16)((((uint16)(val) & (uint16)0x00ffU) << 8) | \
		  (((uint16)(val) & (uint16)0xff00U) >> 8)))


#define BCMSWAP32(val) \
	((uint32)((((uint32)(val) & (uint32)0x000000ffU) << 24) | \
		  (((uint32)(val) & (uint32)0x0000ff00U) <<  8) | \
		  (((uint32)(val) & (uint32)0x00ff0000U) >>  8) | \
		  (((uint32)(val) & (uint32)0xff000000U) >> 24)))


#define BCMSWAP32BY16(val) \
	((uint32)((((uint32)(val) & (uint32)0x0000ffffU) << 16) | \
		  (((uint32)(val) & (uint32)0xffff0000U) >> 16)))


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

static INLINE uint32
bcmswap32by16(uint32 val)
{
	return BCMSWAP32BY16(val);
}




static INLINE void
bcmswap16_buf(uint16 *buf, uint len)
{
	len = len / 2;

	while (len--) {
		*buf = bcmswap16(*buf);
		buf++;
	}
}

#ifndef hton16
#ifndef IL_BIGENDIAN
#define HTON16(i) BCMSWAP16(i)
#define HTON32(i) BCMSWAP32(i)
#define	hton16(i) bcmswap16(i)
#define	hton32(i) bcmswap32(i)
#define	ntoh16(i) bcmswap16(i)
#define	ntoh32(i) bcmswap32(i)
#define HTOL16(i) (i)
#define HTOL32(i) (i)
#define ltoh16(i) (i)
#define ltoh32(i) (i)
#define htol16(i) (i)
#define htol32(i) (i)
#else
#define HTON16(i) (i)
#define HTON32(i) (i)
#define	hton16(i) (i)
#define	hton32(i) (i)
#define	ntoh16(i) (i)
#define	ntoh32(i) (i)
#define HTOL16(i) BCMSWAP16(i)
#define HTOL32(i) BCMSWAP32(i)
#define	ltoh16(i) bcmswap16(i)
#define	ltoh32(i) bcmswap32(i)
#define htol16(i) bcmswap16(i)
#define htol32(i) bcmswap32(i)
#endif 
#endif 

#ifndef IL_BIGENDIAN
#define ltoh16_buf(buf, i)
#define htol16_buf(buf, i)
#else
#define ltoh16_buf(buf, i) bcmswap16_buf((uint16 *)buf, i)
#define htol16_buf(buf, i) bcmswap16_buf((uint16 *)buf, i)
#endif 


static INLINE void
htol16_ua_store(uint16 val, uint8 *bytes)
{
	bytes[0] = val & 0xff;
	bytes[1] = val >> 8;
}


static INLINE void
htol32_ua_store(uint32 val, uint8 *bytes)
{
	bytes[0] = val & 0xff;
	bytes[1] = (val >> 8) & 0xff;
	bytes[2] = (val >> 16) & 0xff;
	bytes[3] = val >> 24;
}


static INLINE void
hton16_ua_store(uint16 val, uint8 *bytes)
{
	bytes[0] = val >> 8;
	bytes[1] = val & 0xff;
}


static INLINE void
hton32_ua_store(uint32 val, uint8 *bytes)
{
	bytes[0] = val >> 24;
	bytes[1] = (val >> 16) & 0xff;
	bytes[2] = (val >> 8) & 0xff;
	bytes[3] = val & 0xff;
}

#define _LTOH16_UA(cp)	((cp)[0] | ((cp)[1] << 8))
#define _LTOH32_UA(cp)	((cp)[0] | ((cp)[1] << 8) | ((cp)[2] << 16) | ((cp)[3] << 24))
#define _NTOH16_UA(cp)	(((cp)[0] << 8) | (cp)[1])
#define _NTOH32_UA(cp)	(((cp)[0] << 24) | ((cp)[1] << 16) | ((cp)[2] << 8) | (cp)[3])


static INLINE uint16
ltoh16_ua(const void *bytes)
{
	return _LTOH16_UA((const uint8 *)bytes);
}


static INLINE uint32
ltoh32_ua(const void *bytes)
{
	return _LTOH32_UA((const uint8 *)bytes);
}


static INLINE uint16
ntoh16_ua(const void *bytes)
{
	return _NTOH16_UA((const uint8 *)bytes);
}


static INLINE uint32
ntoh32_ua(const void *bytes)
{
	return _NTOH32_UA((const uint8 *)bytes);
}

#define ltoh_ua(ptr) \
	(sizeof(*(ptr)) == sizeof(uint8) ? *(const uint8 *)ptr : \
	 sizeof(*(ptr)) == sizeof(uint16) ? _LTOH16_UA((const uint8 *)ptr) : \
	 sizeof(*(ptr)) == sizeof(uint32) ? _LTOH32_UA((const uint8 *)ptr) : \
	 0xfeedf00d)

#define ntoh_ua(ptr) \
	(sizeof(*(ptr)) == sizeof(uint8) ? *(const uint8 *)ptr : \
	 sizeof(*(ptr)) == sizeof(uint16) ? _NTOH16_UA((const uint8 *)ptr) : \
	 sizeof(*(ptr)) == sizeof(uint32) ? _NTOH32_UA((const uint8 *)ptr) : \
	 0xfeedf00d)

#endif 
