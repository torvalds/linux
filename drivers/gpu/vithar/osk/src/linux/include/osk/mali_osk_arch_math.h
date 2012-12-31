/*
 *
 * (C) COPYRIGHT 2010-2011 ARM Limited. All rights reserved.
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

#ifndef _OSK_ARCH_MATH_H
#define _OSK_ARCH_MATH_H

#ifndef _OSK_H_
#error "Include mali_osk.h directly"
#endif

#include <asm/div64.h>

OSK_STATIC_INLINE u32 osk_divmod6432(u64 *value, u32 divisor)
{
	u64 v;
	u32 r;

	OSK_ASSERT(NULL != value);

	v = *value;
	r = do_div(v, divisor);
	*value = v;
	return r;
}
	
#endif /* _OSK_ARCH_MATH_H_ */
