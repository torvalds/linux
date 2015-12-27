/* Copyright (C) 2010 - 2015 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#ifndef __VBUSDEVICEINFO_H__
#define __VBUSDEVICEINFO_H__

#include <linux/types.h>

#pragma pack(push, 1)		/* both GCC and VC now allow this pragma */

/* An array of this struct is present in the channel area for each vbus.
 * (See vbuschannel.h.)
 * It is filled in by the client side to provide info about the device
 * and driver from the client's perspective.
 */
struct ultra_vbus_deviceinfo {
	u8 devtype[16];		/* short string identifying the device type */
	u8 drvname[16];		/* driver .sys file name */
	u8 infostrs[96];	/* sequence of tab-delimited id strings: */
	/* <DRIVER_REV> <DRIVER_VERTAG> <DRIVER_COMPILETIME> */
	u8 reserved[128];	/* pad size to 256 bytes */
};

#pragma pack(pop)

/* Reads chars from the buffer at <src> for <srcmax> bytes, and writes to
 * the buffer at <p>, which is <remain> bytes long, ensuring never to
 * overflow the buffer at <p>, using the following rules:
 * - printable characters are simply copied from the buffer at <src> to the
 *   buffer at <p>
 * - intervening streaks of non-printable characters in the buffer at <src>
 *   are replaced with a single space in the buffer at <p>
 * Note that we pay no attention to '\0'-termination.
 * Returns the number of bytes written to <p>.
 *
 * Pass <p> == NULL and <remain> == 0 for this special behavior.  In this
 * case, we simply return the number of bytes that WOULD HAVE been written
 * to a buffer at <p>, had it been infinitely big.
 */
static inline int
vbuschannel_sanitize_buffer(char *p, int remain, char *src, int srcmax)
{
	int chars = 0;
	int nonprintable_streak = 0;

	while (srcmax > 0) {
		if ((*src >= ' ') && (*src < 0x7f)) {
			if (nonprintable_streak) {
				if (remain > 0) {
					*p = ' ';
					p++;
					remain--;
					chars++;
				} else if (p == NULL) {
					chars++;
				}
				nonprintable_streak = 0;
			}
			if (remain > 0) {
				*p = *src;
				p++;
				remain--;
				chars++;
			} else if (p == NULL) {
				chars++;
			}
		} else {
			nonprintable_streak = 1;
		}
		src++;
		srcmax--;
	}
	return chars;
}

#define VBUSCHANNEL_ADDACHAR(ch, p, remain, chars) \
	do {					   \
		if (remain <= 0)		   \
			break;			   \
		*p = ch;			   \
		p++;  chars++;  remain--;	   \
	} while (0)

/* Converts the non-negative value at <num> to an ascii decimal string
 * at <p>, writing at most <remain> bytes.  Note there is NO '\0' termination
 * written to <p>.
 *
 * Returns the number of bytes written to <p>.
 *
 * Note that we create this function because we need to do this operation in
 * an environment-independent way (since we are in a common header file).
 */
static inline int
vbuschannel_itoa(char *p, int remain, int num)
{
	int digits = 0;
	char s[32];
	int i;

	if (num == 0) {
		/* '0' is a special case */
		if (remain <= 0)
			return 0;
		*p = '0';
		return 1;
	}
	/* form a backwards decimal ascii string in <s> */
	while (num > 0) {
		if (digits >= (int)sizeof(s))
			return 0;
		s[digits++] = (num % 10) + '0';
		num = num / 10;
	}
	if (remain < digits) {
		/* not enough room left at <p> to hold number, so fill with
		 * '?' */
		for (i = 0; i < remain; i++, p++)
			*p = '?';
		return remain;
	}
	/* plug in the decimal ascii string representing the number, by */
	/* reversing the string we just built in <s> */
	i = digits;
	while (i > 0) {
		i--;
		*p = s[i];
		p++;
	}
	return digits;
}

/* Reads <devInfo>, and converts its contents to a printable string at <p>,
 * writing at most <remain> bytes.  Note there is NO '\0' termination
 * written to <p>.
 *
 * Pass <devix> >= 0 if you want a device index presented.
 *
 * Returns the number of bytes written to <p>.
 */
static inline int
vbuschannel_devinfo_to_string(struct ultra_vbus_deviceinfo *devinfo,
			      char *p, int remain, int devix)
{
	char *psrc;
	int nsrc, x, i, pad;
	int chars = 0;

	psrc = &devinfo->devtype[0];
	nsrc = sizeof(devinfo->devtype);
	if (vbuschannel_sanitize_buffer(NULL, 0, psrc, nsrc) <= 0)
		return 0;

	/* emit device index */
	if (devix >= 0) {
		VBUSCHANNEL_ADDACHAR('[', p, remain, chars);
		x = vbuschannel_itoa(p, remain, devix);
		p += x;
		remain -= x;
		chars += x;
		VBUSCHANNEL_ADDACHAR(']', p, remain, chars);
	} else {
		VBUSCHANNEL_ADDACHAR(' ', p, remain, chars);
		VBUSCHANNEL_ADDACHAR(' ', p, remain, chars);
		VBUSCHANNEL_ADDACHAR(' ', p, remain, chars);
	}

	/* emit device type */
	x = vbuschannel_sanitize_buffer(p, remain, psrc, nsrc);
	p += x;
	remain -= x;
	chars += x;
	pad = 15 - x;		/* pad device type to be exactly 15 chars */
	for (i = 0; i < pad; i++)
		VBUSCHANNEL_ADDACHAR(' ', p, remain, chars);
	VBUSCHANNEL_ADDACHAR(' ', p, remain, chars);

	/* emit driver name */
	psrc = &devinfo->drvname[0];
	nsrc = sizeof(devinfo->drvname);
	x = vbuschannel_sanitize_buffer(p, remain, psrc, nsrc);
	p += x;
	remain -= x;
	chars += x;
	pad = 15 - x;		/* pad driver name to be exactly 15 chars */
	for (i = 0; i < pad; i++)
		VBUSCHANNEL_ADDACHAR(' ', p, remain, chars);
	VBUSCHANNEL_ADDACHAR(' ', p, remain, chars);

	/* emit strings */
	psrc = &devinfo->infostrs[0];
	nsrc = sizeof(devinfo->infostrs);
	x = vbuschannel_sanitize_buffer(p, remain, psrc, nsrc);
	p += x;
	remain -= x;
	chars += x;
	VBUSCHANNEL_ADDACHAR('\n', p, remain, chars);

	return chars;
}

#endif
