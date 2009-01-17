/*
 * File:         arch/blackfin/lib/checksum.c
 * Based on:     none - original work
 * Author:
 *
 * Created:
 * Description:  An implementation of the TCP/IP protocol suite for the LINUX
 *               operating system.  INET is implemented using the  BSD Socket
 *               interface as the means of communication with the user level.
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/module.h>
#include <net/checksum.h>
#include <asm/checksum.h>

#ifdef CONFIG_IP_CHECKSUM_L1
static unsigned short do_csum(const unsigned char *buff, int len)__attribute__((l1_text));
#endif

static unsigned short do_csum(const unsigned char *buff, int len)
{
	register unsigned long sum = 0;
	int swappem = 0;

	if (1 & (unsigned long)buff) {
		sum = *buff << 8;
		buff++;
		len--;
		++swappem;
	}

	while (len > 1) {
		sum += *(unsigned short *)buff;
		buff += 2;
		len -= 2;
	}

	if (len > 0)
		sum += *buff;

	/*  Fold 32-bit sum to 16 bits */
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);

	if (swappem)
		sum = ((sum & 0xff00) >> 8) + ((sum & 0x00ff) << 8);

	return sum;

}

/*
 *	This is a version of ip_compute_csum() optimized for IP headers,
 *	which always checksum on 4 octet boundaries.
 */
__sum16 ip_fast_csum(unsigned char *iph, unsigned int ihl)
{
	return (__force __sum16)~do_csum(iph, ihl * 4);
}
EXPORT_SYMBOL(ip_fast_csum);

/*
 * computes the checksum of a memory block at buff, length len,
 * and adds in "sum" (32-bit)
 *
 * returns a 32-bit number suitable for feeding into itself
 * or csum_tcpudp_magic
 *
 * this function must be called with even lengths, except
 * for the last fragment, which may be odd
 *
 * it's best to have buff aligned on a 32-bit boundary
 */
__wsum csum_partial(const void *buff, int len, __wsum sum)
{
	/*
	 * Just in case we get nasty checksum data...
	 * Like 0xffff6ec3 in the case of our IPv6 multicast header.
	 * We fold to begin with, as well as at the end.
	 */
	sum = (sum & 0xffff) + (sum >> 16);

	sum += do_csum(buff, len);

	sum = (sum & 0xffff) + (sum >> 16);

	return sum;
}
EXPORT_SYMBOL(csum_partial);

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
__sum16 ip_compute_csum(const void *buff, int len)
{
	return (__force __sum16)~do_csum(buff, len);
}

/*
 * copy from fs while checksumming, otherwise like csum_partial
 */

__wsum
csum_partial_copy_from_user(const void __user *src, void *dst,
			    int len, __wsum sum, int *csum_err)
{
	if (csum_err)
		*csum_err = 0;
	memcpy(dst, (__force void *)src, len);
	return csum_partial(dst, len, sum);
}

/*
 * copy from ds while checksumming, otherwise like csum_partial
 */

__wsum csum_partial_copy(const void *src, void *dst, int len, __wsum sum)
{
	memcpy(dst, src, len);
	return csum_partial(dst, len, sum);
}
EXPORT_SYMBOL(csum_partial_copy);
