/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		MIPS specific IP/TCP/UDP checksumming routines
 *
 * Authors:	Ralf Baechle, <ralf@waldorf-gmbh.de>
 *		Lots of code moved from tcp.c and ip.c; see those files
 *		for more names.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * $Id: checksum.c,v 1.1 2002/09/28 14:58:40 gerg Exp $
 */
#include <net/checksum.h>
#include <linux/module.h>
#include <linux/types.h>
#include <asm/byteorder.h>
#include <asm/string.h>
#include <asm/uaccess.h>

static inline unsigned short from32to16 (unsigned long sum)
{
	unsigned int result;
	/*
			        %0		%1
	      hsw %1, %0	H     L		L     H
	      add %1, %0	H     L		H+L+C H+L
	*/
	asm ("hsw %1, %0; add %1, %0" : "=&r" (result) : "r" (sum));
	return result >> 16;
}

static inline unsigned int do_csum(const unsigned char * buff, int len)
{
	int odd, count;
	unsigned int result = 0;

	if (len <= 0)
		goto out;
	odd = 1 & (unsigned long) buff;
	if (odd) {
		result = be16_to_cpu(*buff);
		len--;
		buff++;
	}
	count = len >> 1;		/* nr of 16-bit words.. */
	if (count) {
		if (2 & (unsigned long) buff) {
			result += *(unsigned short *) buff;
			count--;
			len -= 2;
			buff += 2;
		}
		count >>= 1;		/* nr of 32-bit words.. */
		if (count) {
			unsigned int carry = 0;
			do {
				unsigned int w = *(unsigned int *) buff;
				count--;
				buff += 4;
				result += carry;
				result += w;
				carry = (w > result);
			} while (count);
			result += carry;
			result = (result & 0xffff) + (result >> 16);
		}
		if (len & 2) {
			result += *(unsigned short *) buff;
			buff += 2;
		}
	}
	if (len & 1)
		result += le16_to_cpu(*buff);
	result = from32to16(result);
	if (odd)
		result = ((result >> 8) & 0xff) | ((result & 0xff) << 8);
out:
	return result;
}

/*
 *	This is a version of ip_compute_csum() optimized for IP headers,
 *	which always checksum on 4 octet boundaries.
 */
unsigned short ip_fast_csum(unsigned char * iph, unsigned int ihl)
{
	return ~do_csum(iph,ihl*4);
}

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
unsigned short ip_compute_csum(const unsigned char * buff, int len)
{
	return ~do_csum(buff,len);
}

/*
 * computes a partial checksum, e.g. for TCP/UDP fragments
 */
unsigned int csum_partial(const unsigned char *buff, int len, unsigned int sum)
{
	unsigned int result = do_csum(buff, len);

	/* add in old sum, and carry.. */
	result += sum;
	if(sum > result)
		result += 1;
	return result;
}

EXPORT_SYMBOL(csum_partial);

/*
 * copy while checksumming, otherwise like csum_partial
 */
unsigned int csum_partial_copy(const unsigned char *src, unsigned char *dst,
                               int len, unsigned int sum)
{
	/*
	 * It's 2:30 am and I don't feel like doing it real ...
	 * This is lots slower than the real thing (tm)
	 */
	sum = csum_partial(src, len, sum);
	memcpy(dst, src, len);

	return sum;
}

/*
 * Copy from userspace and compute checksum.  If we catch an exception
 * then zero the rest of the buffer.
 */
unsigned int csum_partial_copy_from_user (const unsigned char *src, unsigned char *dst,
                                          int len, unsigned int sum,
                                          int *err_ptr)
{
	int missing;

	missing = copy_from_user(dst, src, len);
	if (missing) {
		memset(dst + len - missing, 0, missing);
		*err_ptr = -EFAULT;
	}
		
	return csum_partial(dst, len, sum);
}
