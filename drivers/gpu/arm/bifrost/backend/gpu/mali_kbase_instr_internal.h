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





/*
 * Backend-specific HW access instrumentation APIs
 */

#ifndef _KBASE_INSTR_INTERNAL_H_
#define _KBASE_INSTR_INTERNAL_H_

/**
 * kbasep_cache_clean_worker() - Workqueue for handling cache cleaning
 * @data: a &struct work_struct
 */
void kbasep_cache_clean_worker(struct work_struct *data);

/**
 * kbase_clean_caches_done() - Cache clean interrupt received
 * @kbdev: Kbase device
 */
void kbase_clean_caches_done(struct kbase_device *kbdev);

/**
 * kbase_instr_hwcnt_sample_done() - Dump complete interrupt received
 * @kbdev: Kbase device
 */
void kbase_instr_hwcnt_sample_done(struct kbase_device *kbdev);

#endif /* _KBASE_INSTR_INTERNAL_H_ */
