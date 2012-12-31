/*
 *
 * (C) COPYRIGHT 2008-2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



/**
 * @file
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#ifndef _OSK_ARCH_BITOPS_H_
#define _OSK_ARCH_BITOPS_H_

#ifndef _OSK_H_
#error "Include mali_osk.h directly"
#endif

#include <linux/bitops.h>

OSK_STATIC_INLINE long osk_clz(unsigned long val)
{
	return OSK_BITS_PER_LONG - fls_long(val);
}

OSK_STATIC_INLINE long osk_clz_64(u64 val)
{
	return 64 - fls64(val);
}

OSK_STATIC_INLINE int osk_count_set_bits(unsigned long val)
{
	/* note: __builtin_popcountl() not available in kernel */
	int count = 0;
	while (val)
	{
		count++;
		val &= (val-1);
	}
	return count;
}

#endif /* _OSK_ARCH_BITOPS_H_ */
