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

#ifndef _OSK_ARCH_MEM_H_
#define _OSK_ARCH_MEM_H_

#ifndef _OSK_H_
#error "Include mali_osk.h directly"
#endif

#include <linux/slab.h>
#include <linux/vmalloc.h>

OSK_STATIC_INLINE void * osk_malloc(size_t size)
{
	OSK_ASSERT(0 != size);

	if(OSK_SIMULATE_FAILURE(OSK_OSK))
	{
		return NULL;
	}

	return kmalloc(size, GFP_KERNEL);
}

OSK_STATIC_INLINE void * osk_calloc(size_t size)
{
	OSK_ASSERT(0 != size);

	if(OSK_SIMULATE_FAILURE(OSK_OSK))
	{
		return NULL;
	}

	return kzalloc(size, GFP_KERNEL);
}

OSK_STATIC_INLINE void osk_free(void * ptr)
{
	kfree(ptr);
}

OSK_STATIC_INLINE void * osk_vmalloc(size_t size)
{
	OSK_ASSERT(0 != size);

	if(OSK_SIMULATE_FAILURE(OSK_OSK))
	{
		return NULL;
	}

	return vmalloc_user(size);
}

OSK_STATIC_INLINE void osk_vfree(void * ptr)
{
	vfree(ptr);
}

#define OSK_MEMCPY( dst, src, len ) memcpy(dst, src, len)

#define OSK_MEMCMP( s1, s2, len ) memcmp(s1, s2, len)

#define OSK_MEMSET( ptr, chr, size ) memset(ptr, chr, size)


#endif /* _OSK_ARCH_MEM_H_ */
