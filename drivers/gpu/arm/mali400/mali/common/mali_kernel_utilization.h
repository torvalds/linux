/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2010-2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#ifndef __MALI_KERNEL_UTILIZATION_H__
#define __MALI_KERNEL_UTILIZATION_H__

#include <linux/mali/mali_utgard.h>
#include "mali_osk.h"

extern void (*mali_utilization_callback)(struct mali_gpu_utilization_data *data);

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
 * Should be called when a job is about to execute a GP job
 */
void mali_utilization_gp_start(void);

/**
 * Should be called when a job has completed executing a GP job
 */
void mali_utilization_gp_end(void);

/**
 * Should be called when a job is about to execute a PP job
 */
void mali_utilization_pp_start(void);

/**
 * Should be called when a job has completed executing a PP job
 */
void mali_utilization_pp_end(void);

/**
 * Should be called to stop the utilization timer during system suspend
 */
void mali_utilization_suspend(void);


#endif /* __MALI_KERNEL_UTILIZATION_H__ */
