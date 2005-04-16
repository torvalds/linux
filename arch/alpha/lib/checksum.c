/*
 * arch/alpha/lib/checksum.c
 *
 * This file contains network checksum routines that are better done
 * in an architecture-specific manner due to speed..
 * Comments in other versions indicate that the algorithms are from RFC1071
 *
 * accellerated versions (and 21264 assembly versions ) contributed by
 *	Rick Gorton	<rick.gorton@alpha-processor.com>
 */
 
#include <linux/module.h>
#include <linux/string.h>

#include <asm/byteorder.h>

static inline unsigned short from64to16(unsigned long x)
{
	/* Using extract instructions is a bit more efficient
	   than the original shift/bitmask version.  */

	union {
		unsigned long	ul;
		unsigned int	ui[2];
		unsigned short	us[4];
	} in_v, tmp_v, out_v;

	in_v.ul = x;
	tmp_v.ul = (unsigned long) in_v.ui[0] + (unsigned long) in_v.ui[1];

	/* Since the bits of tmp_v.sh[3] are going to always be zero,
	   we don't have to bother to add that in.  */
	out_v.ul = (unsigned long) tmp_v.us[0] + (unsigned long) tmp_v.us[1]
			+ (unsigned long) tmp_v.us[2];

	/* Similarly, out_v.us[2] is always zero for the final add.  */
	return out_v.us[0] + out_v.us[1];
}

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented.
 */
unsigned short int csum_tcpudp_magic(unsigned long saddr,
				   unsigned long daddr,
				   unsigned short len,
				   unsigned short proto,
				   unsigned int sum)
{
	return ~from64to16(saddr + daddr + sum +
		((unsigned long) ntohs(len) << 16) +
		((unsigned long) proto << 8));
}

unsigned int csum_tcpudp_nofold(unsigned long saddr,
				   unsigned long daddr,
				   unsigned short len,
				   unsigned short proto,
				   unsigned int sum)
{
	unsigned long result;

	result = (saddr + daddr + sum +
		  ((unsigned long) ntohs(len) << 16) +
		  ((unsigned long) proto << 8));

	/* Fold down to 32-bits so we don't lose in the typedef-less 
	   network stack.  */
	/* 64 to 33 */
	result = (result & 0xffffffff) + (result >> 32);
	/* 33 to 32 */
	result = (result & 0xffffffff) + (result >> 32);
	return result;
}

/*
 * Do a 64-bit checksum on an arbitrary memory area..
 *
 * This isn't a great routine, but it's not _horrible_ either. The
 * inner loop could be unrolled a bit further, and there are better
 * ways to do the carry, but this is reasonable.
 */
static inline unsigned long do_csum(const unsigned char * buff, int len)
{
	int odd, count;
	unsigned long result = 0;

	if (len <= 0)
		goto out;
	odd = 1 & (unsigned long) buff;
	if (odd) {
		result = *buff << 8;
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
			if (4 & (unsigned long) buff) {
				result += *(unsigned int *) buff;
				count--;
				len -= 4;
				buff += 4;
			}
			count >>= 1;	/* nr of 64-bit words.. */
			if (count) {
				unsigned long carry = 0;
				do {
					unsigned long w = *(unsigned long *) buff;
					count--;
					buff += 8;
					result += carry;
					result += w;
					carry = (w > result);
				} while (count);
				result += carry;
				result = (result & 0xffffffff) + (result >> 32);
			}
			if (len & 4) {
				result += *(unsigned int *) buff;
				buff += 4;
			}
		}
		if (len & 2) {
			result += *(unsigned short *) buff;
			buff += 2;
		}
	}
	if (len & 1)
		result += *buff;
	result = from64to16(result);
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
unsigned int csum_partial(const unsigned char * buff, int len, unsigned int sum)
{
	unsigned long result = do_csum(buff, len);

	/* add in old sum, and carry.. */
	result += sum;
	/* 32+c bits -> 32 bits */
	result = (result & 0xffffffff) + (result >> 32);
	return result;
}

EXPORT_SYMBOL(csum_partial);

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
unsigned short ip_compute_csum(unsigned char * buff, int len)
{
	return ~from64to16(do_csum(buff,len));
}
