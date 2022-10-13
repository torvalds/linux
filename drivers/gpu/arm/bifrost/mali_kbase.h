/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2010-2022 ARM Limited. All rights reserved.
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

#ifndef _KBASE_H_
#define _KBASE_H_

#include <mali_malisw.h>

#include <mali_kbase_debug.h>

#include <linux/atomic.h>
#include <linux/highmem.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#if (KERNEL_VERSION(4, 11, 0) <= LINUX_VERSION_CODE)
#include <linux/sched/mm.h>
#endif
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>

#include <uapi/gpu/arm/bifrost/mali_base_kernel.h>
#include <mali_kbase_linux.h>

/*
 * Include mali_kbase_defs.h first as this provides types needed by other local
 * header files.
 */
#include "mali_kbase_defs.h"

#include "debug/mali_kbase_debug_ktrace.h"
#include "context/mali_kbase_context.h"
#include "mali_kbase_strings.h"
#include "mali_kbase_mem_lowlevel.h"
#include "mali_kbase_utility.h"
#include "mali_kbase_mem.h"
#include "mmu/mali_kbase_mmu.h"
#include "mali_kbase_gpu_memory_debugfs.h"
#include "mali_kbase_mem_profile_debugfs.h"
#include "mali_kbase_gpuprops.h"
#include <uapi/gpu/arm/bifrost/mali_kbase_ioctl.h>
#if !MALI_USE_CSF
#include "mali_kbase_debug_job_fault.h"
#include "mali_kbase_jd_debugfs.h"
#include "mali_kbase_jm.h"
#include "mali_kbase_js.h"
#else /* !MALI_USE_CSF */
#include "csf/mali_kbase_debug_csf_fault.h"
#endif /* MALI_USE_CSF */

#include "ipa/mali_kbase_ipa.h"

#if IS_ENABLED(CONFIG_GPU_TRACEPOINTS)
#include <trace/events/gpu.h>
#endif

#include "mali_linux_trace.h"

#if MALI_USE_CSF
#include "csf/mali_kbase_csf.h"

/* Physical memory group ID for CSF user I/O.
 */
#define KBASE_MEM_GROUP_CSF_IO BASE_MEM_GROUP_DEFAULT

/* Physical memory group ID for CSF firmware.
 */
#define KBASE_MEM_GROUP_CSF_FW BASE_MEM_GROUP_DEFAULT
#endif

/* Physical memory group ID for a special page which can alias several regions.
 */
#define KBASE_MEM_GROUP_SINK BASE_MEM_GROUP_DEFAULT

/*
 * Kernel-side Base (KBase) APIs
 */

struct kbase_device *kbase_device_alloc(void);
/*
 * note: configuration attributes member of kbdev needs to have
 * been setup before calling kbase_device_init
 */

int kbase_device_misc_init(struct kbase_device *kbdev);
void kbase_device_misc_term(struct kbase_device *kbdev);

#if !MALI_USE_CSF
void kbase_enable_quick_reset(struct kbase_device *kbdev);
void kbase_disable_quick_reset(struct kbase_device *kbdev);
bool kbase_is_quick_reset_enabled(struct kbase_device *kbdev);
#endif

void kbase_device_free(struct kbase_device *kbdev);
int kbase_device_has_feature(struct kbase_device *kbdev, u32 feature);

/* Needed for gator integration and for reporting vsync information */
struct kbase_device *kbase_find_device(int minor);
void kbase_release_device(struct kbase_device *kbdev);

/**
 * kbase_context_get_unmapped_area() - get an address range which is currently
 *                                     unmapped.
 * @kctx: A kernel base context (which has its own GPU address space).
 * @addr: CPU mapped address (set to 0 since MAP_FIXED mapping is not allowed
 *        as Mali GPU driver decides about the mapping).
 * @len: Length of the address range.
 * @pgoff: Page offset within the GPU address space of the kbase context.
 * @flags: Flags for the allocation.
 *
 * Finds the unmapped address range which satisfies requirements specific to
 * GPU and those provided by the call parameters.
 *
 * 1) Requirement for allocations greater than 2MB:
 * - alignment offset is set to 2MB and the alignment mask to 2MB decremented
 * by 1.
 *
 * 2) Requirements imposed for the shader memory alignment:
 * - alignment is decided by the number of GPU pc bits which can be read from
 * GPU properties of the device associated with this kbase context; alignment
 * offset is set to this value in bytes and the alignment mask to the offset
 * decremented by 1.
 * - allocations must not to be at 4GB boundaries. Such cases are indicated
 * by the flag KBASE_REG_GPU_NX not being set (check the flags of the kbase
 * region). 4GB boundaries can be checked against @ref BASE_MEM_MASK_4GB.
 *
 * 3) Requirements imposed for tiler memory alignment, cases indicated by
 * the flag @ref KBASE_REG_TILER_ALIGN_TOP (check the flags of the kbase
 * region):
 * - alignment offset is set to the difference between the kbase region
 * extension (converted from the original value in pages to bytes) and the kbase
 * region initial_commit (also converted from the original value in pages to
 * bytes); alignment mask is set to the kbase region extension in bytes and
 * decremented by 1.
 *
 * Return: if successful, address of the unmapped area aligned as required;
 *         error code (negative) in case of failure;
 */
unsigned long kbase_context_get_unmapped_area(struct kbase_context *kctx,
		const unsigned long addr, const unsigned long len,
		const unsigned long pgoff, const unsigned long flags);


int assign_irqs(struct kbase_device *kbdev);

int kbase_sysfs_init(struct kbase_device *kbdev);
void kbase_sysfs_term(struct kbase_device *kbdev);


int kbase_protected_mode_init(struct kbase_device *kbdev);
void kbase_protected_mode_term(struct kbase_device *kbdev);

/**
 * kbase_device_pm_init() - Performs power management initialization and
 * Verifies device tree configurations.
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Return: 0 if successful, otherwise a standard Linux error code
 */
int kbase_device_pm_init(struct kbase_device *kbdev);

/**
 * kbase_device_pm_term() - Performs power management deinitialization and
 * Free resources.
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Clean up all the resources
 */
void kbase_device_pm_term(struct kbase_device *kbdev);


int power_control_init(struct kbase_device *kbdev);
void power_control_term(struct kbase_device *kbdev);

#if IS_ENABLED(CONFIG_DEBUG_FS)
void kbase_device_debugfs_term(struct kbase_device *kbdev);
int kbase_device_debugfs_init(struct kbase_device *kbdev);
#else /* CONFIG_DEBUG_FS */
static inline int kbase_device_debugfs_init(struct kbase_device *kbdev)
{
	return 0;
}

static inline void kbase_device_debugfs_term(struct kbase_device *kbdev) { }
#endif /* CONFIG_DEBUG_FS */

int registers_map(struct kbase_device *kbdev);
void registers_unmap(struct kbase_device *kbdev);

int kbase_device_coherency_init(struct kbase_device *kbdev);


#if !MALI_USE_CSF
int kbase_jd_init(struct kbase_context *kctx);
void kbase_jd_exit(struct kbase_context *kctx);

/**
 * kbase_jd_submit - Submit atoms to the job dispatcher
 *
 * @kctx: The kbase context to submit to
 * @user_addr: The address in user space of the struct base_jd_atom array
 * @nr_atoms: The number of atoms in the array
 * @stride: sizeof(struct base_jd_atom)
 * @uk6_atom: true if the atoms are legacy atoms (struct base_jd_atom_v2_uk6)
 *
 * Return: 0 on success or error code
 */
int kbase_jd_submit(struct kbase_context *kctx,
		void __user *user_addr, u32 nr_atoms, u32 stride,
		bool uk6_atom);

/**
 * kbase_jd_done_worker - Handle a job completion
 * @data: a &struct work_struct
 *
 * This function requeues the job from the runpool (if it was soft-stopped or
 * removed from NEXT registers).
 *
 * Removes it from the system if it finished/failed/was cancelled.
 *
 * Resolves dependencies to add dependent jobs to the context, potentially
 * starting them if necessary (which may add more references to the context)
 *
 * Releases the reference to the context from the no-longer-running job.
 *
 * Handles retrying submission outside of IRQ context if it failed from within
 * IRQ context.
 */
void kbase_jd_done_worker(struct work_struct *data);

void kbase_jd_done(struct kbase_jd_atom *katom, int slot_nr, ktime_t *end_timestamp,
		kbasep_js_atom_done_code done_code);
void kbase_jd_cancel(struct kbase_device *kbdev, struct kbase_jd_atom *katom);
void kbase_jd_zap_context(struct kbase_context *kctx);

/*
 * kbase_jd_done_nolock - Perform the necessary handling of an atom that has completed
 *                  the execution.
 *
 * @katom: Pointer to the atom that completed the execution
 * @post_immediately: Flag indicating that completion event can be posted
 *                    immediately for @katom and the other atoms depdendent
 *                    on @katom which also completed execution. The flag is
 *                    false only for the case where the function is called by
 *                    kbase_jd_done_worker() on the completion of atom running
 *                    on the GPU.
 *
 * Note that if this is a soft-job that has had kbase_prepare_soft_job called on it then the caller
 * is responsible for calling kbase_finish_soft_job *before* calling this function.
 *
 * The caller must hold the kbase_jd_context.lock.
 */
bool kbase_jd_done_nolock(struct kbase_jd_atom *katom, bool post_immediately);

void kbase_jd_free_external_resources(struct kbase_jd_atom *katom);
void kbase_jd_dep_clear_locked(struct kbase_jd_atom *katom);

/**
 * kbase_job_done - Process completed jobs from job interrupt
 * @kbdev: Pointer to the kbase device.
 * @done: Bitmask of done or failed jobs, from JOB_IRQ_STAT register
 *
 * This function processes the completed, or failed, jobs from the GPU job
 * slots, for the bits set in the @done bitmask.
 *
 * The hwaccess_lock must be held when calling this function.
 */
void kbase_job_done(struct kbase_device *kbdev, u32 done);

/**
 * kbase_job_slot_ctx_priority_check_locked(): - Check for lower priority atoms
 *                                               and soft stop them
 * @kctx: Pointer to context to check.
 * @katom: Pointer to priority atom.
 *
 * Atoms from @kctx on the same job slot as @katom, which have lower priority
 * than @katom will be soft stopped and put back in the queue, so that atoms
 * with higher priority can run.
 *
 * The hwaccess_lock must be held when calling this function.
 */
void kbase_job_slot_ctx_priority_check_locked(struct kbase_context *kctx,
				struct kbase_jd_atom *katom);

/**
 * kbase_job_slot_softstop_start_rp() - Soft-stop the atom at the start
 *                                      of a renderpass.
 * @kctx: Pointer to a kernel base context.
 * @reg:  Reference of a growable GPU memory region in the same context.
 *        Takes ownership of the reference if successful.
 *
 * Used to switch to incremental rendering if we have nearly run out of
 * virtual address space in a growable memory region and the atom currently
 * executing on a job slot is the tiler job chain at the start of a renderpass.
 *
 * Return: 0 if successful, otherwise a negative error code.
 */
int kbase_job_slot_softstop_start_rp(struct kbase_context *kctx,
		struct kbase_va_region *reg);

/**
 * kbase_job_slot_softstop - Soft-stop the specified job slot
 *
 * @kbdev:         The kbase device
 * @js:            The job slot to soft-stop
 * @target_katom:  The job that should be soft-stopped (or NULL for any job)
 * Context:
 *   The job slot lock must be held when calling this function.
 *   The job slot must not already be in the process of being soft-stopped.
 *
 * Where possible any job in the next register is evicted before the soft-stop.
 */
void kbase_job_slot_softstop(struct kbase_device *kbdev, int js,
		struct kbase_jd_atom *target_katom);

void kbase_job_slot_softstop_swflags(struct kbase_device *kbdev, int js,
		struct kbase_jd_atom *target_katom, u32 sw_flags);

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
 * kbase_job_check_enter_disjoint - potentiall enter disjoint mode
 * @kbdev: kbase device
 * @action: the event which has occurred
 * @core_reqs: core requirements of the atom
 * @target_katom: the atom which is being affected
 *
 * For a certain soft-stop action, work out whether to enter disjoint
 * state.
 *
 * This does not register multiple disjoint events if the atom has already
 * started a disjoint period
 *
 * @core_reqs can be supplied as 0 if the atom had not started on the hardware
 * (and so a 'real' soft/hard-stop was not required, but it still interrupted
 * flow, perhaps on another context)
 *
 * kbase_job_check_leave_disjoint() should be used to end the disjoint
 * state when the soft/hard-stop action is complete
 */
void kbase_job_check_enter_disjoint(struct kbase_device *kbdev, u32 action,
		base_jd_core_req core_reqs, struct kbase_jd_atom *target_katom);

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

#endif /* !MALI_USE_CSF */

void kbase_event_post(struct kbase_context *ctx, struct kbase_jd_atom *event);
#if !MALI_USE_CSF
int kbase_event_dequeue(struct kbase_context *ctx, struct base_jd_event_v2 *uevent);
#endif /* !MALI_USE_CSF */
int kbase_event_pending(struct kbase_context *ctx);
int kbase_event_init(struct kbase_context *kctx);
void kbase_event_close(struct kbase_context *kctx);
void kbase_event_cleanup(struct kbase_context *kctx);
void kbase_event_wakeup(struct kbase_context *kctx);

/**
 * kbasep_jit_alloc_validate() - Validate the JIT allocation info.
 *
 * @kctx:	Pointer to the kbase context within which the JIT
 *		allocation is to be validated.
 * @info:	Pointer to struct @base_jit_alloc_info
 *			which is to be validated.
 * Return: 0 if jit allocation is valid; negative error code otherwise
 */
int kbasep_jit_alloc_validate(struct kbase_context *kctx,
					struct base_jit_alloc_info *info);

/**
 * kbase_jit_retry_pending_alloc() - Retry blocked just-in-time memory
 *                                   allocations.
 *
 * @kctx:	Pointer to the kbase context within which the just-in-time
 *		memory allocations are to be retried.
 */
void kbase_jit_retry_pending_alloc(struct kbase_context *kctx);

/**
 * kbase_free_user_buffer() - Free memory allocated for struct
 *		@kbase_debug_copy_buffer.
 *
 * @buffer:	Pointer to the memory location allocated for the object
 *		of the type struct @kbase_debug_copy_buffer.
 */
static inline void kbase_free_user_buffer(
		struct kbase_debug_copy_buffer *buffer)
{
	struct page **pages = buffer->extres_pages;
	int nr_pages = buffer->nr_extres_pages;

	if (pages) {
		int i;

		for (i = 0; i < nr_pages; i++) {
			struct page *pg = pages[i];

			if (pg)
				put_page(pg);
		}
		kfree(pages);
	}
}

/**
 * kbase_mem_copy_from_extres() - Copy from external resources.
 *
 * @kctx:	kbase context within which the copying is to take place.
 * @buf_data:	Pointer to the information about external resources:
 *		pages pertaining to the external resource, number of
 *		pages to copy.
 *
 * Return:      0 on success, error code otherwise.
 */
int kbase_mem_copy_from_extres(struct kbase_context *kctx,
		struct kbase_debug_copy_buffer *buf_data);

#if !MALI_USE_CSF
int kbase_process_soft_job(struct kbase_jd_atom *katom);
int kbase_prepare_soft_job(struct kbase_jd_atom *katom);
void kbase_finish_soft_job(struct kbase_jd_atom *katom);
void kbase_cancel_soft_job(struct kbase_jd_atom *katom);
void kbase_resume_suspended_soft_jobs(struct kbase_device *kbdev);
void kbasep_remove_waiting_soft_job(struct kbase_jd_atom *katom);
#if IS_ENABLED(CONFIG_SYNC_FILE)
void kbase_soft_event_wait_callback(struct kbase_jd_atom *katom);
#endif
int kbase_soft_event_update(struct kbase_context *kctx,
			    u64 event,
			    unsigned char new_status);

void kbasep_soft_job_timeout_worker(struct timer_list *timer);
void kbasep_complete_triggered_soft_events(struct kbase_context *kctx, u64 evt);
#endif /* !MALI_USE_CSF */

void kbasep_as_do_poke(struct work_struct *work);

/**
 * kbase_pm_is_suspending - Check whether a system suspend is in progress,
 * or has already been suspended
 *
 * @kbdev: The kbase device structure for the device
 *
 * The caller should ensure that either kbdev->pm.active_count_lock is held, or
 * a dmb was executed recently (to ensure the value is most
 * up-to-date). However, without a lock the value could change afterwards.
 *
 * Return:
 * * false if a suspend is not in progress
 * * !=false otherwise
 */
static inline bool kbase_pm_is_suspending(struct kbase_device *kbdev)
{
	return kbdev->pm.suspending;
}

#ifdef CONFIG_MALI_ARBITER_SUPPORT
/*
 * Check whether a gpu lost is in progress
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Indicates whether a gpu lost has been received and jobs are no longer
 * being scheduled
 *
 * Return: false if gpu is lost
 * Return: != false otherwise
 */
static inline bool kbase_pm_is_gpu_lost(struct kbase_device *kbdev)
{
	return (atomic_read(&kbdev->pm.gpu_lost) == 0 ? false : true);
}

/*
 * Set or clear gpu lost state
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 * @gpu_lost: true to activate GPU lost state, FALSE is deactive it
 *
 * Puts power management code into gpu lost state or takes it out of the
 * state.  Once in gpu lost state new GPU jobs will no longer be
 * scheduled.
 */
static inline void kbase_pm_set_gpu_lost(struct kbase_device *kbdev,
	bool gpu_lost)
{
	const int new_val = (gpu_lost ? 1 : 0);
	const int cur_val = atomic_xchg(&kbdev->pm.gpu_lost, new_val);

	if (new_val != cur_val)
		KBASE_KTRACE_ADD(kbdev, ARB_GPU_LOST, NULL, new_val);
}
#endif

/**
 * kbase_pm_is_active - Determine whether the GPU is active
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * This takes into account whether there is an active context reference.
 *
 * Return: true if the GPU is active, false otherwise
 */
static inline bool kbase_pm_is_active(struct kbase_device *kbdev)
{
	return kbdev->pm.active_count > 0;
}

/**
 * kbase_pm_lowest_gpu_freq_init() - Find the lowest frequency that the GPU can
 *                                run as using the device tree, and save this
 *                                within kbdev.
 * @kbdev: Pointer to kbase device.
 *
 * This function could be called from kbase_clk_rate_trace_manager_init,
 * but is left separate as it can be called as soon as
 * dev_pm_opp_of_add_table() has been called to initialize the OPP table,
 * which occurs in power_control_init().
 *
 * Return: 0 in any case.
 */
int kbase_pm_lowest_gpu_freq_init(struct kbase_device *kbdev);

/**
 * kbase_pm_metrics_start - Start the utilization metrics timer
 * @kbdev: Pointer to the kbase device for which to start the utilization
 *         metrics calculation thread.
 *
 * Start the timer that drives the metrics calculation, runs the custom DVFS.
 */
void kbase_pm_metrics_start(struct kbase_device *kbdev);

/**
 * kbase_pm_metrics_stop - Stop the utilization metrics timer
 * @kbdev: Pointer to the kbase device for which to stop the utilization
 *         metrics calculation thread.
 *
 * Stop the timer that drives the metrics calculation, runs the custom DVFS.
 */
void kbase_pm_metrics_stop(struct kbase_device *kbdev);

#if MALI_USE_CSF && defined(KBASE_PM_RUNTIME)
/**
 * kbase_pm_handle_runtime_suspend - Handle the runtime suspend of GPU
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * This function is called from the runtime suspend callback function for
 * saving the HW state and powering down GPU, if GPU was in sleep state mode.
 * It does the following steps
 * - Powers up the L2 cache and re-activates the MCU.
 * - Suspend the CSGs
 * - Halts the MCU
 * - Powers down the L2 cache.
 * - Invokes the power_off callback to power down the GPU.
 *
 * Return: 0 if the GPU was already powered down or no error was encountered
 * in the power down, otherwise an error code.
 */
int kbase_pm_handle_runtime_suspend(struct kbase_device *kbdev);

/**
 * kbase_pm_force_mcu_wakeup_after_sleep - Force the wake up of MCU from sleep
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * This function forces the wake up of MCU from sleep state and wait for
 * MCU to become active.
 * It usually gets called from the runtime suspend callback function.
 * It also gets called from the GPU reset handler or at the time of system
 * suspend or when User tries to terminate/suspend the on-slot group.
 *
 * Note: @gpu_wakeup_override flag that forces the reactivation of MCU is
 *       set by this function and it is the caller's responsibility to
 *       clear the flag.
 *
 * Return: 0 if the wake up was successful.
 */
int kbase_pm_force_mcu_wakeup_after_sleep(struct kbase_device *kbdev);
#endif

#if !MALI_USE_CSF
/**
 * kbase_jd_atom_id - Return the atom's ID, as was originally supplied by userspace in
 * base_jd_atom::atom_number
 * @kctx:  KBase context pointer
 * @katom: Atome for which to return ID
 *
 * Return: the atom's ID.
 */
static inline int kbase_jd_atom_id(struct kbase_context *kctx,
				   const struct kbase_jd_atom *katom)
{
	int result;

	KBASE_DEBUG_ASSERT(kctx);
	KBASE_DEBUG_ASSERT(katom);
	KBASE_DEBUG_ASSERT(katom->kctx == kctx);

	result = katom - &kctx->jctx.atoms[0];
	KBASE_DEBUG_ASSERT(result >= 0 && result <= BASE_JD_ATOM_COUNT);
	return result;
}

/**
 * kbase_jd_atom_from_id - Return the atom structure for the given atom ID
 * @kctx: Context pointer
 * @id:   ID of atom to retrieve
 *
 * Return: Pointer to struct kbase_jd_atom associated with the supplied ID
 */
static inline struct kbase_jd_atom *kbase_jd_atom_from_id(
		struct kbase_context *kctx, int id)
{
	return &kctx->jctx.atoms[id];
}
#endif /* !MALI_USE_CSF */

/**
 * kbase_disjoint_init - Initialize the disjoint state
 *
 * @kbdev: The kbase device
 *
 * The disjoint event count and state are both set to zero.
 *
 * Disjoint functions usage:
 *
 * The disjoint event count should be incremented whenever a disjoint event occurs.
 *
 * There are several cases which are regarded as disjoint behavior. Rather than just increment
 * the counter during disjoint events we also increment the counter when jobs may be affected
 * by what the GPU is currently doing. To facilitate this we have the concept of disjoint state.
 *
 * Disjoint state is entered during GPU reset. Increasing the disjoint state also increases
 * the count of disjoint events.
 *
 * The disjoint state is then used to increase the count of disjoint events during job submission
 * and job completion. Any atom submitted or completed while the disjoint state is greater than
 * zero is regarded as a disjoint event.
 *
 * The disjoint event counter is also incremented immediately whenever a job is soft stopped
 * and during context creation.
 *
 * Return: 0 on success and non-zero value on failure.
 */
void kbase_disjoint_init(struct kbase_device *kbdev);

/**
 * kbase_disjoint_event - Increase the count of disjoint events
 * called when a disjoint event has happened
 *
 * @kbdev: The kbase device
 */
void kbase_disjoint_event(struct kbase_device *kbdev);

/**
 * kbase_disjoint_event_potential - Increase the count of disjoint events
 * only if the GPU is in a disjoint state
 *
 * @kbdev: The kbase device
 *
 * This should be called when something happens which could be disjoint if the GPU
 * is in a disjoint state. The state refcount keeps track of this.
 */
void kbase_disjoint_event_potential(struct kbase_device *kbdev);

/**
 * kbase_disjoint_event_get - Returns the count of disjoint events
 *
 * @kbdev: The kbase device
 * Return: the count of disjoint events
 */
u32 kbase_disjoint_event_get(struct kbase_device *kbdev);

/**
 * kbase_disjoint_state_up - Increment the refcount state indicating that
 * the GPU is in a disjoint state.
 *
 * @kbdev: The kbase device
 *
 * Also Increment the disjoint event count (calls @ref kbase_disjoint_event)
 * eventually after the disjoint state has completed @ref kbase_disjoint_state_down
 * should be called
 */
void kbase_disjoint_state_up(struct kbase_device *kbdev);

/**
 * kbase_disjoint_state_down - Decrement the refcount state
 *
 * @kbdev: The kbase device
 *
 * Also Increment the disjoint event count (calls @ref kbase_disjoint_event)
 *
 * Called after @ref kbase_disjoint_state_up once the disjoint state is over
 */
void kbase_disjoint_state_down(struct kbase_device *kbdev);

/**
 * kbase_device_pcm_dev_init() - Initialize the priority control manager device
 *
 * @kbdev: Pointer to the structure for the kbase device
 *
 * Pointer to the priority control manager device is retrieved from the device
 * tree and a reference is taken on the module implementing the callbacks for
 * priority control manager operations.
 *
 * Return: 0 if successful, or an error code on failure
 */
int kbase_device_pcm_dev_init(struct kbase_device *const kbdev);

/**
 * kbase_device_pcm_dev_term() - Performs priority control manager device
 *                               deinitialization.
 *
 * @kbdev: Pointer to the structure for the kbase device
 *
 * Reference is released on the module implementing the callbacks for priority
 * control manager operations.
 */
void kbase_device_pcm_dev_term(struct kbase_device *const kbdev);

/**
 * KBASE_DISJOINT_STATE_INTERLEAVED_CONTEXT_COUNT_THRESHOLD - If a job is soft stopped
 * and the number of contexts is >= this value it is reported as a disjoint event
 */
#define KBASE_DISJOINT_STATE_INTERLEAVED_CONTEXT_COUNT_THRESHOLD 2

#if !defined(UINT64_MAX)
	#define UINT64_MAX ((uint64_t)0xFFFFFFFFFFFFFFFFULL)
#endif

#endif
