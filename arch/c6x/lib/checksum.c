// SPDX-License-Identifier: GPL-2.0-or-later
/*
 */
#include <linux/module.h>
#include <net/checksum.h>

#include <asm/byteorder.h>

/*
 * copy from fs while checksumming, otherwise like csum_partial
 */
__wsum
csum_partial_copy_from_user(const void __user *src, void *dst, int len,
			    __wsum sum, int *csum_err)
{
	int missing;

	missing = __copy_from_user(dst, src, len);
	if (missing) {
		memset(dst + len - missing, 0, missing);
		*csum_err = -EFAULT;
	} else
		*csum_err = 0;

	return csum_partial(dst, len, sum);
}
EXPORT_SYMBOL(csum_partial_copy_from_user);

/* These are from csum_64plus.S */
EXPORT_SYMBOL(csum_partial);
EXPORT_SYMBOL(csum_partial_copy);
EXPORT_SYMBOL(ip_compute_csum);
EXPORT_SYMBOL(ip_fast_csum);
