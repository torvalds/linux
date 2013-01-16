/*
 * Copyright (C) 2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_kernel_common.h"
#include "mali_osk.h"

static _mali_osk_atomic_t mali_job_autonumber;

_mali_osk_errcode_t mali_scheduler_initialize(void)
{
	if ( _MALI_OSK_ERR_OK != _mali_osk_atomic_init(&mali_job_autonumber, 0))
	{
		MALI_DEBUG_PRINT(1,  ("Initialization of atomic job id counter failed.\n"));
		return _MALI_OSK_ERR_FAULT;
	}

	return _MALI_OSK_ERR_OK;
}

void mali_scheduler_terminate(void)
{
	_mali_osk_atomic_term(&mali_job_autonumber);
}

u32 mali_scheduler_get_new_id(void)
{
	u32 job_id = _mali_osk_atomic_inc_return(&mali_job_autonumber);
	return job_id;
}

