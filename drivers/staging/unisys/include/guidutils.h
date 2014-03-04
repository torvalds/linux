/* Copyright © 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/* guidutils.h
 *
 * These are GUID manipulation inlines that can be used from either
 * kernel-mode or user-mode.
 *
 */
#ifndef __GUIDUTILS_H__
#define __GUIDUTILS_H__

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ctype.h>
#define GUID_STRTOUL kstrtoul
#else
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#define GUID_STRTOUL strtoul
#endif

static inline char *
GUID_format1(const GUID *guid, char *s)
{
	sprintf(s, "{%-8.8lx-%-4.4x-%-4.4x-%-2.2x%-2.2x%-2.2x%-2.2x%-2.2x%-2.2x%-2.2x%-2.2x}",
		(ulong) guid->data1,
		guid->data2,
		guid->data3,
		guid->data4[0],
		guid->data4[1],
		guid->data4[2],
		guid->data4[3],
		guid->data4[4], guid->data4[5], guid->data4[6], guid->data4[7]);
	return s;
}

/** Format a GUID in Microsoft's 'what in the world were they thinking'
 *  format.
 */
static inline char *
GUID_format2(const GUID *guid, char *s)
{
	sprintf(s, "{%-8.8lx-%-4.4x-%-4.4x-%-2.2x%-2.2x-%-2.2x%-2.2x%-2.2x%-2.2x%-2.2x%-2.2x}",
		(ulong) guid->data1,
		guid->data2,
		guid->data3,
		guid->data4[0],
		guid->data4[1],
		guid->data4[2],
		guid->data4[3],
		guid->data4[4], guid->data4[5], guid->data4[6], guid->data4[7]);
	return s;
}

/**
 * Like GUID_format2 but without the curly braces and the
 * hex digits in upper case
 */
static inline char *
GUID_format3(const GUID *guid, char *s)
{
	sprintf(s, "%-8.8lX-%-4.4X-%-4.4X-%-2.2X%-2.2X-%-2.2X%-2.2X%-2.2X%-2.2X%-2.2X%-2.2X",
		(ulong) guid->data1,
		guid->data2,
		guid->data3,
		guid->data4[0],
		guid->data4[1],
		guid->data4[2],
		guid->data4[3],
		guid->data4[4], guid->data4[5], guid->data4[6], guid->data4[7]);
	return s;
}

/** Parse a guid string in any of these forms:
 *      {11111111-2222-3333-4455-66778899aabb}
 *      {11111111-2222-3333-445566778899aabb}
 *      11111111-2222-3333-4455-66778899aabb
 *      11111111-2222-3333-445566778899aabb
 */
static inline GUID
GUID_scan(U8 *p)
{
	GUID guid = GUID0;
	U8 x[33];
	int count = 0;
	int c, i = 0;
	U8 cdata1[9];
	U8 cdata2[5];
	U8 cdata3[5];
	U8 cdata4[3];
	int dashcount = 0;
	int brace = 0;
	unsigned long uldata;

	if (!p)
		return guid;
	if (*p == '{') {
		p++;
		brace = 1;
	}
	while (count < 32) {
		if (*p == '}')
			return guid;
		if (*p == '\0')
			return guid;
		c = toupper(*p);
		p++;
		if (c == '-') {
			switch (dashcount) {
			case 0:
				if (i != 8)
					return guid;
				break;
			case 1:
				if (i != 4)
					return guid;
				break;
			case 2:
				if (i != 4)
					return guid;
				break;
			case 3:
				if (i != 4)
					return guid;
				break;
			default:
				return guid;
			}
			dashcount++;
			i = 0;
			continue;
		}
		if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))
			i++;
		else
			return guid;
		x[count++] = c;
	}
	x[count] = '\0';
	if (brace) {
		if (*p == '}')
			p++;
		else
			return guid;
	}
	if (dashcount == 3 || dashcount == 4)
		;
	else
		return guid;
	memset(cdata1, 0, sizeof(cdata1));
	memset(cdata2, 0, sizeof(cdata2));
	memset(cdata3, 0, sizeof(cdata3));
	memset(cdata4, 0, sizeof(cdata4));
	memcpy(cdata1, x + 0, 8);
	memcpy(cdata2, x + 8, 4);
	memcpy(cdata3, x + 12, 4);

	if (GUID_STRTOUL((char *) cdata1, 16, &uldata) == 0)
		guid.data1 = (U32)uldata;
	if (GUID_STRTOUL((char *) cdata2, 16, &uldata) == 0)
		guid.data2 = (U16)uldata;
	if (GUID_STRTOUL((char *) cdata3, 16, &uldata) == 0)
		guid.data3 = (U16)uldata;

	for (i = 0; i < 8; i++) {
		memcpy(cdata4, x + 16 + (i * 2), 2);
		if (GUID_STRTOUL((char *) cdata4, 16, &uldata) == 0)
			guid.data4[i] = (U8) uldata;
	}

	return guid;
}

static inline char *
GUID_sanitize(char *inputGuidStr, char *outputGuidStr)
{
	GUID g;
	GUID guid0 = GUID0;
	*outputGuidStr = '\0';
	g = GUID_scan((U8 *) inputGuidStr);
	if (memcmp(&g, &guid0, sizeof(GUID)) == 0)
		return outputGuidStr;	/* bad GUID format */
	return GUID_format1(&g, outputGuidStr);
}

#endif
