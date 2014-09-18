/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 * Support code for the main lib/checksum.c.
 */

#include <net/checksum.h>
#include <linux/module.h>

__wsum do_csum(const unsigned char *buff, int len)
{
	int odd, count;
	unsigned long result = 0;

	if (len <= 0)
		goto out;
	odd = 1 & (unsigned long) buff;
	if (odd) {
		result = (*buff << 8);
		len--;
		buff++;
	}
	count = len >> 1;		/* nr of 16-bit words.. */
	if (count) {
		if (2 & (unsigned long) buff) {
			result += *(const unsigned short *)buff;
			count--;
			len -= 2;
			buff += 2;
		}
		count >>= 1;		/* nr of 32-bit words.. */
		if (count) {
#ifdef __tilegx__
			if (4 & (unsigned long) buff) {
				unsigned int w = *(const unsigned int *)buff;
				result = __insn_v2sadau(result, w, 0);
				count--;
				len -= 4;
				buff += 4;
			}
			count >>= 1;		/* nr of 64-bit words.. */
#endif

			/*
			 * This algorithm could wrap around for very
			 * large buffers, but those should be impossible.
			 */
			BUG_ON(count >= 65530);

			while (count) {
				unsigned long w = *(const unsigned long *)buff;
				count--;
				buff += sizeof(w);
#ifdef __tilegx__
				result = __insn_v2sadau(result, w, 0);
#else
				result = __insn_sadah_u(result, w, 0);
#endif
			}
#ifdef __tilegx__
			if (len & 4) {
				unsigned int w = *(const unsigned int *)buff;
				result = __insn_v2sadau(result, w, 0);
				buff += 4;
			}
#endif
		}
		if (len & 2) {
			result += *(const unsigned short *) buff;
			buff += 2;
		}
	}
	if (len & 1)
		result += *buff;
	result = csum_long(result);
	if (odd)
		result = swab16(result);
out:
	return result;
}
