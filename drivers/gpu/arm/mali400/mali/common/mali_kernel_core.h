/*
 * Copyright (C) 2010-2016 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_KERNEL_CORE_H__
#define __MALI_KERNEL_CORE_H__

#include "mali_osk.h"

typedef enum {
	_MALI_PRODUCT_ID_UNKNOWN,
	_MALI_PRODUCT_ID_MALI200,
	_MALI_PRODUCT_ID_MALI300,
	_MALI_PRODUCT_ID_MALI400,
	_MALI_PRODUCT_ID_MALI450,
	_MALI_PRODUCT_ID_MALI470,
} _mali_product_id_t;

extern mali_bool mali_gpu_class_is_mali450;
extern mali_bool mali_gpu_class_is_mali470;

_mali_osk_errcode_t mali_initialize_subsystems(void);

void mali_terminate_subsystems(void);

_mali_product_id_t mali_kernel_core_get_product_id(void);

u32 mali_kernel_core_get_gpu_major_version(void);

u32 mali_kernel_core_get_gpu_minor_version(void);

u32 _mali_kernel_core_dump_state(char *buf, u32 size);

MALI_STATIC_INLINE mali_bool mali_is_mali470(void)
{
	return mali_gpu_class_is_mali470;
}

MALI_STATIC_INLINE mali_bool mali_is_mali450(void)
{
	return mali_gpu_class_is_mali450;
}

MALI_STATIC_INLINE mali_bool mali_is_mali400(void)
{
	if (mali_gpu_class_is_mali450 || mali_gpu_class_is_mali470)
		return MALI_FALSE;

	return MALI_TRUE;
}
#endif /* __MALI_KERNEL_CORE_H__ */
