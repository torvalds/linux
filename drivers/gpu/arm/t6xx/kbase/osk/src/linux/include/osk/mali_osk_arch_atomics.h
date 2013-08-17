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

#ifndef _OSK_ARCH_ATOMICS_H_
#define _OSK_ARCH_ATOMICS_H_

#ifndef _OSK_H_
#error "Include mali_osk.h directly"
#endif

OSK_STATIC_INLINE u32 osk_atomic_sub(osk_atomic * atom, u32 value)
{
	OSK_ASSERT(NULL != atom);
	return atomic_sub_return(value, atom);
}

OSK_STATIC_INLINE u32 osk_atomic_add(osk_atomic * atom, u32 value)
{
	OSK_ASSERT(NULL != atom);
	return atomic_add_return(value, atom);
}

OSK_STATIC_INLINE u32 osk_atomic_dec(osk_atomic * atom)
{
	OSK_ASSERT(NULL != atom);
	return osk_atomic_sub(atom, 1);
}

OSK_STATIC_INLINE u32 osk_atomic_inc(osk_atomic * atom)
{
	OSK_ASSERT(NULL != atom);
	return osk_atomic_add(atom, 1);
}

OSK_STATIC_INLINE void osk_atomic_set(osk_atomic * atom, u32 value)
{
	OSK_ASSERT(NULL != atom);
	atomic_set(atom, value);
}

OSK_STATIC_INLINE u32 osk_atomic_get(osk_atomic * atom)
{
	OSK_ASSERT(NULL != atom);
	return atomic_read(atom);
}

OSK_STATIC_INLINE u32 osk_atomic_compare_and_swap(osk_atomic * atom, u32 old_value, u32 new_value)
{
	OSK_ASSERT(NULL != atom);
	return atomic_cmpxchg(atom, old_value, new_value);
}

#endif /* _OSK_ARCH_ATOMICS_H_ */
