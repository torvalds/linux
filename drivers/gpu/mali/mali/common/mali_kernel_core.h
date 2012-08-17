/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
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

extern int mali_max_job_runtime;

typedef enum
{
	_MALI_PRODUCT_ID_UNKNOWN,
	_MALI_PRODUCT_ID_MALI200,
	_MALI_PRODUCT_ID_MALI300,
	_MALI_PRODUCT_ID_MALI400,
	_MALI_PRODUCT_ID_MALI450,
} _mali_product_id_t;

_mali_osk_errcode_t mali_initialize_subsystems(void);

void mali_terminate_subsystems(void);

void mali_kernel_core_wakeup(void);

_mali_product_id_t mali_kernel_core_get_product_id(void);

u32 _mali_kernel_core_dump_state(char* buf, u32 size);

#endif /* __MALI_KERNEL_CORE_H__ */
