/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2014-2022 ARM Limited. All rights reserved.
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
 * HW access job manager common APIs
 */

#ifndef _KBASE_HWACCESS_JM_H_
#define _KBASE_HWACCESS_JM_H_

/**
 * kbase_backend_run_atom() - Run an atom on the GPU
 * @kbdev:	Device pointer
 * @katom:	Atom to run
 *
 * Caller must hold the HW access lock
 */
void kbase_backend_run_atom(struct kbase_device *kbdev,
				struct kbase_jd_atom *katom);

/**
 * kbase_backend_slot_update - Update state based on slot ringbuffers
 *
 * @kbdev:  Device pointer
 *
 * Inspect the jobs in the slot ringbuffers and update state.
 *
 * This will cause jobs to be submitted to hardware if they are unblocked
 */
void kbase_backend_slot_update(struct kbase_device *kbdev);

/**
 * kbase_backend_find_and_release_free_address_space() - Release a free AS
 * @kbdev:	Device pointer
 * @kctx:	Context pointer
 *
 * This function can evict an idle context from the runpool, freeing up the
 * address space it was using.
 *
 * The address space is marked as in use. The caller must either assign a
 * context using kbase_gpu_use_ctx(), or release it using
 * kbase_ctx_sched_release()
 *
 * Return: Number of free address space, or KBASEP_AS_NR_INVALID if none
 *	   available
 */
int kbase_backend_find_and_release_free_address_space(
		struct kbase_device *kbdev, struct kbase_context *kctx);

/**
 * kbase_backend_use_ctx() - Activate a currently unscheduled context, using the
 *			     provided address space.
 * @kbdev:	Device pointer
 * @kctx:	Context pointer. May be NULL
 * @as_nr:	Free address space to use
 *
 * kbase_gpu_next_job() will pull atoms from the active context.
 *
 * Return: true if successful, false if ASID not assigned.
 */
bool kbase_backend_use_ctx(struct kbase_device *kbdev,
				struct kbase_context *kctx,
				int as_nr);

/**
 * kbase_backend_use_ctx_sched() - Activate a context.
 * @kbdev:	Device pointer
 * @kctx:	Context pointer
 * @js:         Job slot to activate context on
 *
 * kbase_gpu_next_job() will pull atoms from the active context.
 *
 * The context must already be scheduled and assigned to an address space. If
 * the context is not scheduled, then kbase_gpu_use_ctx() should be used
 * instead.
 *
 * Caller must hold hwaccess_lock
 *
 * Return: true if context is now active, false otherwise (ie if context does
 *	   not have an address space assigned)
 */
bool kbase_backend_use_ctx_sched(struct kbase_device *kbdev,
					struct kbase_context *kctx, int js);

/**
 * kbase_backend_release_ctx_irq - Release a context from the GPU. This will
 *                                 de-assign the assigned address space.
 * @kbdev: Device pointer
 * @kctx:  Context pointer
 *
 * Caller must hold kbase_device->mmu_hw_mutex and hwaccess_lock
 */
void kbase_backend_release_ctx_irq(struct kbase_device *kbdev,
				struct kbase_context *kctx);

/**
 * kbase_backend_release_ctx_noirq - Release a context from the GPU. This will
 *                                   de-assign the assigned address space.
 * @kbdev: Device pointer
 * @kctx:  Context pointer
 *
 * Caller must hold kbase_device->mmu_hw_mutex
 *
 * This function must perform any operations that could not be performed in IRQ
 * context by kbase_backend_release_ctx_irq().
 */
void kbase_backend_release_ctx_noirq(struct kbase_device *kbdev,
						struct kbase_context *kctx);

/**
 * kbase_backend_cache_clean - Perform a cache clean if the given atom requires
 *                            one
 * @kbdev:	Device pointer
 * @katom:	Pointer to the failed atom
 *
 * On some GPUs, the GPU cache must be cleaned following a failed atom. This
 * function performs a clean if it is required by @katom.
 */
void kbase_backend_cache_clean(struct kbase_device *kbdev,
		struct kbase_jd_atom *katom);


/**
 * kbase_backend_complete_wq() - Perform backend-specific actions required on
 *				 completing an atom.
 * @kbdev:	Device pointer
 * @katom:	Pointer to the atom to complete
 *
 * This function should only be called from kbase_jd_done_worker() or
 * js_return_worker().
 *
 * Return: true if atom has completed, false if atom should be re-submitted
 */
void kbase_backend_complete_wq(struct kbase_device *kbdev,
				struct kbase_jd_atom *katom);

#if !MALI_USE_CSF
/**
 * kbase_backend_complete_wq_post_sched - Perform backend-specific actions
 *                                        required on completing an atom, after
 *                                        any scheduling has taken place.
 * @kbdev:         Device pointer
 * @core_req:      Core requirements of atom
 *
 * This function should only be called from kbase_jd_done_worker() or
 * js_return_worker().
 */
void kbase_backend_complete_wq_post_sched(struct kbase_device *kbdev,
		base_jd_core_req core_req);
#endif /* !MALI_USE_CSF */

/**
 * kbase_backend_reset() - The GPU is being reset. Cancel all jobs on the GPU
 *			   and remove any others from the ringbuffers.
 * @kbdev:		Device pointer
 * @end_timestamp:	Timestamp of reset
 */
void kbase_backend_reset(struct kbase_device *kbdev, ktime_t *end_timestamp);

/**
 * kbase_backend_inspect_tail - Return the atom currently at the tail of slot
 *                              @js
 * @kbdev: Device pointer
 * @js:    Job slot to inspect
 *
 * Return: Atom currently at the head of slot @js, or NULL
 */
struct kbase_jd_atom *kbase_backend_inspect_tail(struct kbase_device *kbdev,
					int js);

/**
 * kbase_backend_nr_atoms_on_slot() - Return the number of atoms currently on a
 *				      slot.
 * @kbdev:	Device pointer
 * @js:		Job slot to inspect
 *
 * Return: Number of atoms currently on slot
 */
int kbase_backend_nr_atoms_on_slot(struct kbase_device *kbdev, int js);

/**
 * kbase_backend_nr_atoms_submitted() - Return the number of atoms on a slot
 *					that are currently on the GPU.
 * @kbdev:	Device pointer
 * @js:		Job slot to inspect
 *
 * Return: Number of atoms currently on slot @js that are currently on the GPU.
 */
int kbase_backend_nr_atoms_submitted(struct kbase_device *kbdev, int js);

/**
 * kbase_backend_ctx_count_changed() - Number of contexts ready to submit jobs
 *				       has changed.
 * @kbdev:	Device pointer
 *
 * Perform any required backend-specific actions (eg starting/stopping
 * scheduling timers).
 */
void kbase_backend_ctx_count_changed(struct kbase_device *kbdev);

/**
 * kbase_backend_timeouts_changed() - Job Scheduler timeouts have changed.
 * @kbdev:	Device pointer
 *
 * Perform any required backend-specific actions (eg updating timeouts of
 * currently running atoms).
 */
void kbase_backend_timeouts_changed(struct kbase_device *kbdev);

/**
 * kbase_backend_slot_free() - Return the number of jobs that can be currently
 *			       submitted to slot @js.
 * @kbdev:	Device pointer
 * @js:		Job slot to inspect
 *
 * Return: Number of jobs that can be submitted.
 */
int kbase_backend_slot_free(struct kbase_device *kbdev, int js);

/**
 * kbase_job_check_leave_disjoint - potentially leave disjoint state
 * @kbdev: kbase device
 * @target_katom: atom which is finishing
 *
 * Work out whether to leave disjoint state when finishing an atom that was
 * originated by kbase_job_check_enter_disjoint().
 */
void kbase_job_check_leave_disjoint(struct kbase_device *kbdev,
		struct kbase_jd_atom *target_katom);

/**
 * kbase_backend_jm_kill_running_jobs_from_kctx - Kill all jobs that are
 *                               currently running on GPU from a context
 * @kctx: Context pointer
 *
 * This is used in response to a page fault to remove all jobs from the faulting
 * context from the hardware.
 *
 * Caller must hold hwaccess_lock.
 */
void kbase_backend_jm_kill_running_jobs_from_kctx(struct kbase_context *kctx);

/**
 * kbase_jm_wait_for_zero_jobs - Wait for context to have zero jobs running, and
 *                               to be descheduled.
 * @kctx: Context pointer
 *
 * This should be called following kbase_js_zap_context(), to ensure the context
 * can be safely destroyed.
 */
void kbase_jm_wait_for_zero_jobs(struct kbase_context *kctx);

/**
 * kbase_backend_get_current_flush_id - Return the current flush ID
 *
 * @kbdev: Device pointer
 *
 * Return: the current flush ID to be recorded for each job chain
 */
u32 kbase_backend_get_current_flush_id(struct kbase_device *kbdev);

/**
 * kbase_job_slot_hardstop - Hard-stop the specified job slot
 * @kctx:         The kbase context that contains the job(s) that should
 *                be hard-stopped
 * @js:           The job slot to hard-stop
 * @target_katom: The job that should be hard-stopped (or NULL for all
 *                jobs from the context)
 * Context:
 *   The job slot lock must be held when calling this function.
 */
void kbase_job_slot_hardstop(struct kbase_context *kctx, int js,
				struct kbase_jd_atom *target_katom);

/**
 * kbase_gpu_atoms_submitted_any() - Inspect whether there are any atoms
 * currently on the GPU
 * @kbdev:  Device pointer
 *
 * Return: true if there are any atoms on the GPU, false otherwise
 */
bool kbase_gpu_atoms_submitted_any(struct kbase_device *kbdev);

/**
 * kbase_backend_slot_kctx_purge_locked - Perform a purge on the slot_rb tracked
 *                                        kctx
 *
 * @kbdev:	Device pointer
 * @kctx:	The kbase context that needs to be purged from slot_rb[]
 *
 * For JM GPUs, the L1 read only caches may need a start_flush invalidation,
 * potentially on all slots (even if the kctx was only using a single slot),
 * following a context termination or address-space ID recycle. This function
 * performs a clean-up purge on the given kctx which if it has been tracked by
 * slot_rb[] objects.
 *
 * Caller must hold kbase_device->hwaccess_lock.
 */
void kbase_backend_slot_kctx_purge_locked(struct kbase_device *kbdev, struct kbase_context *kctx);

#endif /* _KBASE_HWACCESS_JM_H_ */
