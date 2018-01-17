/*
 *
 * (C) COPYRIGHT 2014-2015 ARM Limited. All rights reserved.
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





/*
 * HW Access instrumentation common APIs
 */

#ifndef _KBASE_HWACCESS_INSTR_H_
#define _KBASE_HWACCESS_INSTR_H_

#include <mali_kbase_instr_defs.h>

/**
 * kbase_instr_hwcnt_enable_internal - Enable HW counters collection
 * @kbdev:	Kbase device
 * @kctx:	Kbase context
 * @setup:	HW counter setup parameters
 *
 * Context: might sleep, waiting for reset to complete
 *
 * Return: 0 on success
 */
int kbase_instr_hwcnt_enable_internal(struct kbase_device *kbdev,
					struct kbase_context *kctx,
					struct kbase_uk_hwcnt_setup *setup);

/**
 * kbase_instr_hwcnt_disable_internal - Disable HW counters collection
 * @kctx: Kbase context
 *
 * Context: might sleep, waiting for an ongoing dump to complete
 *
 * Return: 0 on success
 */
int kbase_instr_hwcnt_disable_internal(struct kbase_context *kctx);

/**
 * kbase_instr_hwcnt_request_dump() - Request HW counter dump from GPU
 * @kctx:	Kbase context
 *
 * Caller must either wait for kbase_instr_hwcnt_dump_complete() to return true,
 * of call kbase_instr_hwcnt_wait_for_dump().
 *
 * Return: 0 on success
 */
int kbase_instr_hwcnt_request_dump(struct kbase_context *kctx);

/**
 * kbase_instr_hwcnt_wait_for_dump() - Wait until pending HW counter dump has
 *				       completed.
 * @kctx:	Kbase context
 *
 * Context: will sleep, waiting for dump to complete
 *
 * Return: 0 on success
 */
int kbase_instr_hwcnt_wait_for_dump(struct kbase_context *kctx);

/**
 * kbase_instr_hwcnt_dump_complete - Tell whether the HW counters dump has
 *				     completed
 * @kctx:	Kbase context
 * @success:	Set to true if successful
 *
 * Context: does not sleep.
 *
 * Return: true if the dump is complete
 */
bool kbase_instr_hwcnt_dump_complete(struct kbase_context *kctx,
						bool * const success);

/**
 * kbase_instr_hwcnt_clear() - Clear HW counters
 * @kctx:	Kbase context
 *
 * Context: might sleep, waiting for reset to complete
 *
 * Return: 0 on success
 */
int kbase_instr_hwcnt_clear(struct kbase_context *kctx);

/**
 * kbase_instr_backend_init() - Initialise the instrumentation backend
 * @kbdev:	Kbase device
 *
 * This function should be called during driver initialization.
 *
 * Return: 0 on success
 */
int kbase_instr_backend_init(struct kbase_device *kbdev);

/**
 * kbase_instr_backend_init() - Terminate the instrumentation backend
 * @kbdev:	Kbase device
 *
 * This function should be called during driver termination.
 */
void kbase_instr_backend_term(struct kbase_device *kbdev);

#endif /* _KBASE_HWACCESS_INSTR_H_ */
