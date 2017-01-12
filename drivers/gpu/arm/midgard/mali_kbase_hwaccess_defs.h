/*
 *
 * (C) COPYRIGHT 2014 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */




/**
 * @file mali_kbase_hwaccess_gpu_defs.h
 * HW access common definitions
 */

#ifndef _KBASE_HWACCESS_DEFS_H_
#define _KBASE_HWACCESS_DEFS_H_

#include <mali_kbase_jm_defs.h>

/* The kbasep_js_device_data::runpool_irq::lock (a spinlock) must be held when
 * accessing this structure */
struct kbase_hwaccess_data {
	struct kbase_context *active_kctx;

	struct kbase_backend_data backend;
};

#endif /* _KBASE_HWACCESS_DEFS_H_ */
