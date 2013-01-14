/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_KERNEL_UTILIZATION_H__
#define __MALI_KERNEL_UTILIZATION_H__

#include "mali_osk.h"

extern void (*mali_utilization_callback)(unsigned int);

/**
 * Initialize/start the Mali GPU utilization metrics reporting.
 *
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t mali_utilization_init(void);

/**
 * Terminate the Mali GPU utilization metrics reporting
 */
void mali_utilization_term(void);

/**
 * Check if Mali utilization is enabled
 */
MALI_STATIC_INLINE mali_bool mali_utilization_enabled(void)
{
	return (NULL != mali_utilization_callback);
}

/**
 * Should be called when a job is about to execute a job
 */
void mali_utilization_core_start(u64 time_now);

/**
 * Should be called to stop the utilization timer during system suspend
 */
void mali_utilization_suspend(void);

/**
 * Should be called when a job has completed executing a job
 */
void mali_utilization_core_end(u64 time_now);

#endif /* __MALI_KERNEL_UTILIZATION_H__ */
