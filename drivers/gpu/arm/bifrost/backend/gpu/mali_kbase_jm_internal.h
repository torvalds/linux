/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2011-2016, 2018-2023 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

/*
 * Job Manager backend-specific low-level APIs.
 */

#ifndef _KBASE_JM_HWACCESS_H_
#define _KBASE_JM_HWACCESS_H_

#include <mali_kbase_hw.h>
#include <mali_kbase_debug.h>
#include <linux/atomic.h>

#include <backend/gpu/mali_kbase_jm_rb.h>
#include <device/mali_kbase_device.h>

/**
 * kbase_job_done_slot() - Complete the head job on a particular job-slot
 * @kbdev:		Device pointer
 * @s:			Job slot
 * @completion_code:	Completion code of job reported by GPU
 * @job_tail:		Job tail address reported by GPU
 * @end_timestamp:	Timestamp of job completion
 */
void kbase_job_done_slot(struct kbase_device *kbdev, int s, u32 completion_code,
					u64 job_tail, ktime_t *end_timestamp);

#if IS_ENABLED(CONFIG_GPU_TRACEPOINTS)
static inline char *kbasep_make_job_slot_string(unsigned int js, char *js_string, size_t js_size)
{
	snprintf(js_string, js_size, "job_slot_%u", js);
	return js_string;
}
#endif

/**
 * kbase_job_hw_submit() - Submit a job to the GPU
 * @kbdev:	Device pointer
 * @katom:	Atom to submit
 * @js:		Job slot to submit on
 *
 * The caller must check kbasep_jm_is_submit_slots_free() != false before
 * calling this.
 *
 * The following locking conditions are made on the caller:
 * - it must hold the hwaccess_lock
 *
 * Return: 0 if the job was successfully submitted to hardware, an error otherwise.
 */
int kbase_job_hw_submit(struct kbase_device *kbdev, struct kbase_jd_atom *katom, unsigned int js);

#if !MALI_USE_CSF
/**
 * kbasep_job_slot_soft_or_hard_stop_do_action() - Perform a soft or hard stop
 *						   on the specified atom
 * @kbdev:		Device pointer
 * @js:			Job slot to stop on
 * @action:		The action to perform, either JSn_COMMAND_HARD_STOP or
 *			JSn_COMMAND_SOFT_STOP
 * @core_reqs:		Core requirements of atom to stop
 * @target_katom:	Atom to stop
 *
 * The following locking conditions are made on the caller:
 * - it must hold the hwaccess_lock
 */
void kbasep_job_slot_soft_or_hard_stop_do_action(struct kbase_device *kbdev, unsigned int js,
						 u32 action, base_jd_core_req core_reqs,
						 struct kbase_jd_atom *target_katom);
#endif /* !MALI_USE_CSF */

/**
 * kbase_backend_soft_hard_stop_slot() - Soft or hard stop jobs on a given job
 *					 slot belonging to a given context.
 * @kbdev:	Device pointer
 * @kctx:	Context pointer. May be NULL
 * @katom:	Specific atom to stop. May be NULL
 * @js:		Job slot to hard stop
 * @action:	The action to perform, either JSn_COMMAND_HARD_STOP or
 *		JSn_COMMAND_SOFT_STOP
 *
 * If no context is provided then all jobs on the slot will be soft or hard
 * stopped.
 *
 * If a katom is provided then only that specific atom will be stopped. In this
 * case the kctx parameter is ignored.
 *
 * Jobs that are on the slot but are not yet on the GPU will be unpulled and
 * returned to the job scheduler.
 *
 * Return: true if an atom was stopped, false otherwise
 */
bool kbase_backend_soft_hard_stop_slot(struct kbase_device *kbdev, struct kbase_context *kctx,
				       unsigned int js, struct kbase_jd_atom *katom, u32 action);

/**
 * kbase_job_slot_init - Initialise job slot framework
 * @kbdev: Device pointer
 *
 * Called on driver initialisation
 *
 * Return: 0 on success
 */
int kbase_job_slot_init(struct kbase_device *kbdev);

/**
 * kbase_job_slot_halt - Halt the job slot framework
 * @kbdev: Device pointer
 *
 * Should prevent any further job slot processing
 */
void kbase_job_slot_halt(struct kbase_device *kbdev);

/**
 * kbase_job_slot_term - Terminate job slot framework
 * @kbdev: Device pointer
 *
 * Called on driver termination
 */
void kbase_job_slot_term(struct kbase_device *kbdev);

/**
 * kbase_gpu_cache_clean - Cause a GPU cache clean & flush
 * @kbdev: Device pointer
 *
 * Caller must not be in IRQ context
 */
void kbase_gpu_cache_clean(struct kbase_device *kbdev);

#endif /* _KBASE_JM_HWACCESS_H_ */
