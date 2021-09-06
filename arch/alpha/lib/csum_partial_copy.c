// SPDX-License-Identifier: GPL-2.0
/*
 * csum_partial_copy - do IP checksumming and copy
 *
 * (C) Copyright 1996 Linus Torvalds
 * accelerated versions (and 21264 assembly versions ) contributed by
 *	Rick Gorton	<rick.gorton@alpha-processor.com>
 *
 * Don't look at this too closely - you'll go mad. The things
 * we do for performance..
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <net/checksum.h>


#define ldq_u(x,y) \
__asm__ __volatile__("ldq_u %0,%1":"=r" (x):"m" (*(const unsigned long *)(y)))

#define stq_u(x,y) \
__asm__ __volatile__("stq_u %1,%0":"=m" (*(unsigned long *)(y)):"r" (x))

#define extql(x,y,z) \
__asm__ __volatile__("extql %1,%2,%0":"=r" (z):"r" (x),"r" (y))

#define extqh(x,y,z) \
__asm__ __volatile__("extqh %1,%2,%0":"=r" (z):"r" (x),"r" (y))

#define mskql(x,y,z) \
__asm__ __volatile__("mskql %1,%2,%0":"=r" (z):"r" (x),"r" (y))

#define mskqh(x,y,z) \
__asm__ __volatile__("mskqh %1,%2,%0":"=r" (z):"r" (x),"r" (y))

#define insql(x,y,z) \
__asm__ __volatile__("insql %1,%2,%0":"=r" (z):"r" (x),"r" (y))

#define insqh(x,y,z) \
__asm__ __volatile__("insqh %1,%2,%0":"=r" (z):"r" (x),"r" (y))

#define __get_word(insn,x,ptr)				\
({							\
	long __guu_err;					\
	__asm__ __volatile__(				\
	"1:	"#insn" %0,%2\n"			\
	"2:\n"						\
	EXC(1b,2b,%0,%1)				\
		: "=r"(x), "=r"(__guu_err)		\
		: "m"(__m(ptr)), "1"(0));		\
	__guu_err;					\
})

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
 * Ok. This isn't fun, but this is the EASY case.
 */
static inline unsigned long
csum_partial_cfu_aligned(const unsigned long __user *src, unsigned long *dst,
			 long len)
{
	unsigned long checksum = ~0U;
	unsigned long carry = 0;

	while (len >= 0) {
		unsigned long word;
		if (__get_word(ldq, word, src))
			return 0;
		checksum += carry;
		src++;
		checksum += word;
		len -= 8;
		carry = checksum < word;
		*dst = word;
		dst++;
	}
	len += 8;
	checksum += carry;
	if (len) {
		unsigned long word, tmp;
		if (__get_word(ldq, word, src))
			return 0;
		tmp = *dst;
		mskql(word, len, word);
		checksum += word;
		mskqh(tmp, len, tmp);
		carry = checksum < word;
		*dst = word | tmp;
		checksum += carry;
	}
	return checksum;
}

/*
 * This is even less fun, but this is still reasonably
 * easy.
 */
static inline unsigned long
csum_partial_cfu_dest_aligned(const unsigned long __user *src,
			      unsigned long *dst,
			      unsigned long soff,
			      long len)
{
	unsigned long first;
	unsigned long word, carry;
	unsigned long lastsrc = 7+len+(unsigned long)src;
	unsigned long checksum = ~0U;

	if (__get_word(ldq_u, first,src))
		return 0;
	carry = 0;
	while (len >= 0) {
		unsigned long second;

		if (__get_word(ldq_u, second, src+1))
			return 0;
		extql(first, soff, word);
		len -= 8;
		src++;
		extqh(second, soff, first);
		checksum += carry;
		word |= first;
		first = second;
		checksum += word;
		*dst = word;
		dst++;
		carry = checksum < word;
	}
	len += 8;
	checksum += carry;
	if (len) {
		unsigned long tmp;
		unsigned long second;
		if (__get_word(ldq_u, second, lastsrc))
			return 0;
		tmp = *dst;
		extql(first, soff, word);
		extqh(second, soff, first);
		word |= first;
		mskql(word, len, word);
		checksum += word;
		mskqh(tmp, len, tmp);
		carry = checksum < word;
		*dst = word | tmp;
		checksum += carry;
	}
	return checksum;
}

/*
 * This is slightly less fun than the above..
 */
static inline unsigned long
csum_partial_cfu_src_aligned(const unsigned long __user *src,
			     unsigned long *dst,
			     unsigned long doff,
			     long len,
			     unsigned long partial_dest)
{
	unsigned long carry = 0;
	unsigned long word;
	unsigned long second_dest;
	unsigned long checksum = ~0U;

	mskql(partial_dest, doff, partial_dest);
	while (len >= 0) {
		if (__get_word(ldq, word, src))
			return 0;
		len -= 8;
		insql(word, doff, second_dest);
		checksum += carry;
		stq_u(partial_dest | second_dest, dst);
		src++;
		checksum += word;
		insqh(word, doff, partial_dest);
		carry = checksum < word;
		dst++;
	}
	len += 8;
	if (len) {
		checksum += carry;
		if (__get_word(ldq, word, src))
			return 0;
		mskql(word, len, word);
		len -= 8;
		checksum += word;
		insql(word, doff, second_dest);
		len += doff;
		carry = checksum < word;
		partial_dest |= second_dest;
		if (len >= 0) {
			stq_u(partial_dest, dst);
			if (!len) goto out;
			dst++;
			insqh(word, doff, partial_dest);
		}
		doff = len;
	}
	ldq_u(second_dest, dst);
	mskqh(second_dest, doff, second_dest);
	stq_u(partial_dest | second_dest, dst);
out:
	checksum += carry;
	return checksum;
}

/*
 * This is so totally un-fun that it's frightening. Don't
 * look at this too closely, you'll go blind.
 */
static inline unsigned long
csum_partial_cfu_unaligned(const unsigned long __user * src,
			   unsigned long * dst,
			   unsigned long soff, unsigned long doff,
			   long len, unsigned long partial_dest)
{
	unsigned long carry = 0;
	unsigned long first;
	unsigned long lastsrc;
	unsigned long checksum = ~0U;

	if (__get_word(ldq_u, first, src))
		return 0;
	lastsrc = 7+len+(unsigned long)src;
	mskql(partial_dest, doff, partial_dest);
	while (len >= 0) {
		unsigned long second, word;
		unsigned long second_dest;

		if (__get_word(ldq_u, second, src+1))
			return 0;
		extql(first, soff, word);
		checksum += carry;
		len -= 8;
		extqh(second, soff, first);
		src++;
		word |= first;
		first = second;
		insql(word, doff, second_dest);
		checksum += word;
		stq_u(partial_dest | second_dest, dst);
		carry = checksum < word;
		insqh(word, doff, partial_dest);
		dst++;
	}
	len += doff;
	checksum += carry;
	if (len >= 0) {
		unsigned long second, word;
		unsigned long second_dest;

		if (__get_word(ldq_u, second, lastsrc))
			return 0;
		extql(first, soff, word);
		extqh(second, soff, first);
		word |= first;
		first = second;
		mskql(word, len-doff, word);
		checksum += word;
		insql(word, doff, second_dest);
		carry = checksum < word;
		stq_u(partial_dest | second_dest, dst);
		if (len) {
			ldq_u(second_dest, dst+1);
			insqh(word, doff, partial_dest);
			mskqh(second_dest, len, second_dest);
			stq_u(partial_dest | second_dest, dst+1);
		}
		checksum += carry;
	} else {
		unsigned long second, word;
		unsigned long second_dest;

		if (__get_word(ldq_u, second, lastsrc))
			return 0;
		extql(first, soff, word);
		extqh(second, soff, first);
		word |= first;
		ldq_u(second_dest, dst);
		mskql(word, len-doff, word);
		checksum += word;
		mskqh(second_dest, len, second_dest);
		carry = checksum < word;
		insql(word, doff, word);
		stq_u(partial_dest | word | second_dest, dst);
		checksum += carry;
	}
	return checksum;
}

static __wsum __csum_and_copy(const void __user *src, void *dst, int len)
{
	unsigned long soff = 7 & (unsigned long) src;
	unsigned long doff = 7 & (unsigned long) dst;
	unsigned long checksum;

	if (!doff) {
		if (!soff)
			checksum = csum_partial_cfu_aligned(
				(const unsigned long __user *) src,
				(unsigned long *) dst, len-8);
		else
			checksum = csum_partial_cfu_dest_aligned(
				(const unsigned long __user *) src,
				(unsigned long *) dst,
				soff, len-8);
	} else {
		unsigned long partial_dest;
		ldq_u(partial_dest, dst);
		if (!soff)
			checksum = csum_partial_cfu_src_aligned(
				(const unsigned long __user *) src,
				(unsigned long *) dst,
				doff, len-8, partial_dest);
		else
			checksum = csum_partial_cfu_unaligned(
				(const unsigned long __user *) src,
				(unsigned long *) dst,
				soff, doff, len-8, partial_dest);
	}
	return (__force __wsum)from64to16 (checksum);
}

__wsum
csum_and_copy_from_user(const void __user *src, void *dst, int len)
{
	if (!access_ok(src, len))
		return 0;
	return __csum_and_copy(src, dst, len);
}
EXPORT_SYMBOL(csum_and_copy_from_user);

__wsum
csum_partial_copy_nocheck(const void *src, void *dst, int len)
{
	return __csum_and_copy((__force const void __user *)src,
						dst, len);
}
EXPORT_SYMBOL(csum_partial_copy_nocheck);
