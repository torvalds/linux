/*
 * Copyright 2002, 2003 Andi Kleen, SuSE Labs.
 * Subject to the GNU Public License v.2
 *
 * Wrappers of assembly checksum functions for x86-64.
 */
#include <asm/checksum.h>
#include <linux/module.h>

/**
 * csum_partial_copy_from_user - Copy and checksum from user space.
 * @src: source address (user space)
 * @dst: destination address
 * @len: number of bytes to be copied.
 * @isum: initial sum that is added into the result (32bit unfolded)
 * @errp: set to -EFAULT for an bad source address.
 *
 * Returns an 32bit unfolded checksum of the buffer.
 * src and dst are best aligned to 64bits.
 */
__wsum
csum_partial_copy_from_user(const void __user *src, void *dst,
			    int len, __wsum isum, int *errp)
{
	might_sleep();
	*errp = 0;

	if (!likely(access_ok(VERIFY_READ, src, len)))
		goto out_err;

	/*
	 * Why 6, not 7? To handle odd addresses aligned we
	 * would need to do considerable complications to fix the
	 * checksum which is defined as an 16bit accumulator. The
	 * fix alignment code is primarily for performance
	 * compatibility with 32bit and that will handle odd
	 * addresses slowly too.
	 */
	if (unlikely((unsigned long)src & 6)) {
		while (((unsigned long)src & 6) && len >= 2) {
			__u16 val16;

			*errp = __get_user(val16, (const __u16 __user *)src);
			if (*errp)
				return isum;

			*(__u16 *)dst = val16;
			isum = (__force __wsum)add32_with_carry(
					(__force unsigned)isum, val16);
			src += 2;
			dst += 2;
			len -= 2;
		}
	}
	isum = csum_partial_copy_generic((__force const void *)src,
				dst, len, isum, errp, NULL);
	if (unlikely(*errp))
		goto out_err;

	return isum;

out_err:
	*errp = -EFAULT;
	memset(dst, 0, len);

	return isum;
}
EXPORT_SYMBOL(csum_partial_copy_from_user);

/**
 * csum_partial_copy_to_user - Copy and checksum to user space.
 * @src: source address
 * @dst: destination address (user space)
 * @len: number of bytes to be copied.
 * @isum: initial sum that is added into the result (32bit unfolded)
 * @errp: set to -EFAULT for an bad destination address.
 *
 * Returns an 32bit unfolded checksum of the buffer.
 * src and dst are best aligned to 64bits.
 */
__wsum
csum_partial_copy_to_user(const void *src, void __user *dst,
			  int len, __wsum isum, int *errp)
{
	might_sleep();

	if (unlikely(!access_ok(VERIFY_WRITE, dst, len))) {
		*errp = -EFAULT;
		return 0;
	}

	if (unlikely((unsigned long)dst & 6)) {
		while (((unsigned long)dst & 6) && len >= 2) {
			__u16 val16 = *(__u16 *)src;

			isum = (__force __wsum)add32_with_carry(
					(__force unsigned)isum, val16);
			*errp = __put_user(val16, (__u16 __user *)dst);
			if (*errp)
				return isum;
			src += 2;
			dst += 2;
			len -= 2;
		}
	}

	*errp = 0;
	return csum_partial_copy_generic(src, (void __force *)dst,
					 len, isum, NULL, errp);
}
EXPORT_SYMBOL(csum_partial_copy_to_user);

/**
 * csum_partial_copy_nocheck - Copy and checksum.
 * @src: source address
 * @dst: destination address
 * @len: number of bytes to be copied.
 * @sum: initial sum that is added into the result (32bit unfolded)
 *
 * Returns an 32bit unfolded checksum of the buffer.
 */
__wsum
csum_partial_copy_nocheck(const void *src, void *dst, int len, __wsum sum)
{
	return csum_partial_copy_generic(src, dst, len, sum, NULL, NULL);
}
EXPORT_SYMBOL(csum_partial_copy_nocheck);

__sum16 csum_ipv6_magic(const struct in6_addr *saddr,
			const struct in6_addr *daddr,
			__u32 len, unsigned short proto, __wsum sum)
{
	__u64 rest, sum64;

	rest = (__force __u64)htonl(len) + (__force __u64)htons(proto) +
		(__force __u64)sum;

	asm("	addq (%[saddr]),%[sum]\n"
	    "	adcq 8(%[saddr]),%[sum]\n"
	    "	adcq (%[daddr]),%[sum]\n"
	    "	adcq 8(%[daddr]),%[sum]\n"
	    "	adcq $0,%[sum]\n"

	    : [sum] "=r" (sum64)
	    : "[sum]" (rest), [saddr] "r" (saddr), [daddr] "r" (daddr));

	return csum_fold(
	       (__force __wsum)add32_with_carry(sum64 & 0xffffffff, sum64>>32));
}
EXPORT_SYMBOL(csum_ipv6_magic);
