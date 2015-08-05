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
 * Instrumentation API definitions
 */

#ifndef _KBASE_INSTR_H_
#define _KBASE_INSTR_H_

#include <mali_kbase_hwaccess_instr.h>

/**
 * kbase_instr_hwcnt_setup() - Configure HW counters collection
 * @kctx:	Kbase context
 * @setup:	&struct kbase_uk_hwcnt_setup containing configuration
 *
 * Return: 0 on success
 */
int kbase_instr_hwcnt_setup(struct kbase_context *kctx,
					struct kbase_uk_hwcnt_setup *setup);

/**
 * kbase_instr_hwcnt_enable() - Enable HW counters collection
 * @kctx:	Kbase context
 * @setup:	&struct kbase_uk_hwcnt_setup containing configuration
 *
 * Return: 0 on success
 */
int kbase_instr_hwcnt_enable(struct kbase_context *kctx,
					struct kbase_uk_hwcnt_setup *setup);

/**
 * kbase_instr_hwcnt_disable() - Disable HW counters collection
 * @kctx:	Kbase context
 *
 * Return: 0 on success
 */
int kbase_instr_hwcnt_disable(struct kbase_context *kctx);

/**
 * kbase_instr_hwcnt_dump() - Trigger dump of HW counters and wait for
 *                            completion
 * @kctx:	Kbase context
 *
 * Context: might sleep, waiting for dump to complete
 *
 * Return: 0 on success
 */
int kbase_instr_hwcnt_dump(struct kbase_context *kctx);

/**
 * kbase_instr_hwcnt_suspend() - GPU is suspending, stop HW counter collection
 * @kbdev:	Kbase device
 *
 * It's assumed that there's only one privileged context.
 *
 * Safe to do this without lock when doing an OS suspend, because it only
 * changes in response to user-space IOCTLs
 */
void kbase_instr_hwcnt_suspend(struct kbase_device *kbdev);

/**
 * kbase_instr_hwcnt_resume() - GPU is resuming, resume HW counter collection
 * @kbdev:	Kbase device
 */
void kbase_instr_hwcnt_resume(struct kbase_device *kbdev);

#endif /* _KBASE_INSTR_H_ */
