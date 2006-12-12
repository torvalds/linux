/* $Id: old_checksum.c,v 1.3 2003/10/27 08:04:32 starvik Exp $
 *
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		IP/TCP/UDP checksumming routines
 *
 * Authors:	Jorge Cwik, <jorge@laser.satlink.net>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *		Tom May, <ftom@netcom.com>
 *		Lots of code moved from tcp.c and ip.c; see those files
 *		for more names.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <net/checksum.h>
#include <net/module.h>

#undef PROFILE_CHECKSUM

#ifdef PROFILE_CHECKSUM
/* these are just for profiling the checksum code with an oscillioscope.. uh */
#if 0
#define BITOFF *((unsigned char *)0xb0000030) = 0xff
#define BITON *((unsigned char *)0xb0000030) = 0x0
#endif
#include <asm/io.h>
#define CBITON LED_ACTIVE_SET(1)
#define CBITOFF LED_ACTIVE_SET(0)
#define BITOFF
#define BITON
#else
#define BITOFF
#define BITON
#define CBITOFF
#define CBITON
#endif

/*
 * computes a partial checksum, e.g. for TCP/UDP fragments
 */

#include <asm/delay.h>

__wsum csum_partial(const void *p, int len, __wsum __sum)
{
	u32 sum = (__force u32)__sum;
	const u16 *buff = p;
	/*
	* Experiments with ethernet and slip connections show that buff
	* is aligned on either a 2-byte or 4-byte boundary.
	*/
	const void *endMarker = p + len;
	const void *marker = endMarker - (len % 16);
#if 0
	if((int)buff & 0x3)
		printk("unaligned buff %p\n", buff);
	__delay(900); /* extra delay of 90 us to test performance hit */
#endif
	BITON;
	while (buff < marker) {
		sum += *buff++;
		sum += *buff++;
		sum += *buff++;
		sum += *buff++;
		sum += *buff++;
		sum += *buff++;
		sum += *buff++;
		sum += *buff++;
	}
	marker = endMarker - (len % 2);
	while (buff < marker)
		sum += *buff++;

	if (endMarker > buff)
		sum += *(const u8 *)buff;	/* add extra byte seperately */

	BITOFF;
	return (__force __wsum)sum;
}

EXPORT_SYMBOL(csum_partial);
