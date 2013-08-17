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
 * @file mali_osk_common.h
 * This header defines macros shared by the Common Layer public interfaces for
 * all utilities, to ensure they are available even if a client does not include
 * the convenience header mali_osk.h.
 */

#ifndef _OSK_COMMON_H_
#define _OSK_COMMON_H_

#include <osk/include/mali_osk_debug.h>

/**
 * @private
 */
static INLINE mali_bool oskp_ptr_is_null(const void* ptr)
{
	CSTD_UNUSED(ptr);
	OSK_ASSERT(ptr != NULL);
	return MALI_FALSE;
}

/**
 * @brief Check if a pointer is NULL, if so an assert is triggered, otherwise the pointer itself is returned.
 *
 * @param [in] ptr Pointer to test
 *
 * @return @c ptr if it's not NULL.
 */
#define OSK_CHECK_PTR(ptr)\
	(oskp_ptr_is_null(ptr) ? NULL : ptr)

#endif /* _OSK_COMMON_H_ */
