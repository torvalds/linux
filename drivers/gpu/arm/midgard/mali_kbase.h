/*
 *
 * (C) COPYRIGHT 2010-2015 ARM Limited. All rights reserved.
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





#ifndef _KBASE_H_
#define _KBASE_H_

#include <mali_malisw.h>

#include <mali_kbase_debug.h>

#include <asm/page.h>

#include <linux/atomic.h>
#include <linux/highmem.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/mm_types.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include "mali_base_kernel.h"
#include <mali_kbase_uku.h>
#include <mali_kbase_linux.h>

#include "mali_kbase_pm.h"
#include "mali_kbase_mem_lowlevel.h"
#include "mali_kbase_defs.h"
#include "mali_kbase_trace_timeline.h"
#include "mali_kbase_js.h"
#include "mali_kbase_mem.h"
#include "mali_kbase_utility.h"
#include "mali_kbase_gpu_memory_debugfs.h"
#include "mali_kbase_mem_profile_debugfs.h"
#include "mali_kbase_debug_job_fault.h"
#include "mali_kbase_jd_debugfs.h"
#include "mali_kbase_gpuprops.h"
#include "mali_kbase_jm.h"
#include "mali_kbase_vinstr.h"
#include "mali_kbase_ipa.h"
#ifdef CONFIG_GPU_TRACEPOINTS
#include <trace/events/gpu.h>
#endif
/**
 * @page page_base_kernel_main Kernel-side Base (KBase) APIs
 *
 * The Kernel-side Base (KBase) APIs are divided up as follows:
 * - @subpage page_kbase_js_policy
 */

/**
 * @defgroup base_kbase_api Kernel-side Base (KBase) APIs
 */

struct kbase_device *kbase_device_alloc(void);
/*
* note: configuration attributes member of kbdev needs to have
* been setup before calling kbase_device_init
*/

/*
* API to acquire device list semaphore and return pointer
* to the device list head
*/
const struct list_head *kbase_dev_list_get(void);
/* API to release the device list semaphore */
void kbase_dev_list_put(const struct list_head *dev_list);

int kbase_device_init(struct kbase_device * const kbdev);
void kbase_device_term(struct kbase_device *kbdev);
void kbase_device_free(struct kbase_device *kbdev);
int kbase_device_has_feature(struct kbase_device *kbdev, u32 feature);

/* Needed for gator integration and for reporting vsync information */
struct kbase_device *kbase_find_device(int minor);
void kbase_release_device(struct kbase_device *kbdev);

void kbase_set_profiling_control(struct kbase_device *kbdev, u32 control, u32 value);

u32 kbase_get_profiling_control(struct kbase_device *kbdev, u32 control);

struct kbase_context *
kbase_create_context(struct kbase_device *kbdev, bool is_compat);
void kbase_destroy_context(struct kbase_context *kctx);
int kbase_context_set_create_flags(struct kbase_context *kctx, u32 flags);

int kbase_jd_init(struct kbase_context *kctx);
void kbase_jd_exit(struct kbase_context *kctx);
#ifdef BASE_LEGACY_UK6_SUPPORT
int kbase_jd_submit(struct kbase_context *kctx,
		const struct kbase_uk_job_submit *submit_data,
		int uk6_atom);
#else
int kbase_jd_submit(struct kbase_context *kctx,
		const struct kbase_uk_job_submit *submit_data);
#endif

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
void kbase_jd_evict(struct kbase_device *kbdev, struct kbase_jd_atom *katom);
void kbase_jd_zap_context(struct kbase_context *kctx);
bool jd_done_nolock(struct kbase_jd_atom *katom,
		struct list_head *completed_jobs_ctx);
void kbase_jd_free_external_resources(struct kbase_jd_atom *katom);
bool jd_submit_atom(struct kbase_context *kctx,
			 const struct base_jd_atom_v2 *user_atom,
			 struct kbase_jd_atom *katom);

void kbase_job_done(struct kbase_device *kbdev, u32 done);

void kbase_gpu_cacheclean(struct kbase_device *kbdev,
					struct kbase_jd_atom *katom);
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
 * The js_data.runpool_irq.lock must be held when calling this function.
 */
void kbase_job_slot_ctx_priority_check_locked(struct kbase_context *kctx,
				struct kbase_jd_atom *katom);

void kbase_job_slot_softstop(struct kbase_device *kbdev, int js,
		struct kbase_jd_atom *target_katom);
void kbase_job_slot_softstop_swflags(struct kbase_device *kbdev, int js,
		struct kbase_jd_atom *target_katom, u32 sw_flags);
void kbase_job_slot_hardstop(struct kbase_context *kctx, int js,
		struct kbase_jd_atom *target_katom);
void kbase_job_check_enter_disjoint(struct kbase_device *kbdev, u32 action,
		u16 core_reqs, struct kbase_jd_atom *target_katom);
void kbase_job_check_leave_disjoint(struct kbase_device *kbdev,
		struct kbase_jd_atom *target_katom);

void kbase_event_post(struct kbase_context *ctx, struct kbase_jd_atom *event);
int kbase_event_dequeue(struct kbase_context *ctx, struct base_jd_event_v2 *uevent);
int kbase_event_pending(struct kbase_context *ctx);
int kbase_event_init(struct kbase_context *kctx);
void kbase_event_close(struct kbase_context *kctx);
void kbase_event_cleanup(struct kbase_context *kctx);
void kbase_event_wakeup(struct kbase_context *kctx);

int kbase_process_soft_job(struct kbase_jd_atom *katom);
int kbase_prepare_soft_job(struct kbase_jd_atom *katom);
void kbase_finish_soft_job(struct kbase_jd_atom *katom);
void kbase_cancel_soft_job(struct kbase_jd_atom *katom);
void kbase_resume_suspended_soft_jobs(struct kbase_device *kbdev);

bool kbase_replay_process(struct kbase_jd_atom *katom);

/* api used internally for register access. Contains validation and tracing */
void kbase_device_trace_register_access(struct kbase_context *kctx, enum kbase_reg_access_type type, u16 reg_offset, u32 reg_value);
int kbase_device_trace_buffer_install(
		struct kbase_context *kctx, u32 *tb, size_t size);
void kbase_device_trace_buffer_uninstall(struct kbase_context *kctx);

/* api to be ported per OS, only need to do the raw register access */
void kbase_os_reg_write(struct kbase_device *kbdev, u16 offset, u32 value);
u32 kbase_os_reg_read(struct kbase_device *kbdev, u16 offset);


void kbasep_as_do_poke(struct work_struct *work);

/** Returns the name associated with a Mali exception code
 *
 * This function is called from the interrupt handler when a GPU fault occurs.
 * It reports the details of the fault using KBASE_DEBUG_PRINT_WARN.
 *
 * @param[in] kbdev     The kbase device that the GPU fault occurred from.
 * @param[in] exception_code  exception code
 * @return name associated with the exception code
 */
const char *kbase_exception_name(struct kbase_device *kbdev,
		u32 exception_code);

/**
 * Check whether a system suspend is in progress, or has already been suspended
 *
 * The caller should ensure that either kbdev->pm.active_count_lock is held, or
 * a dmb was executed recently (to ensure the value is most
 * up-to-date). However, without a lock the value could change afterwards.
 *
 * @return false if a suspend is not in progress
 * @return !=false otherwise
 */
static inline bool kbase_pm_is_suspending(struct kbase_device *kbdev)
{
	return kbdev->pm.suspending;
}

/**
 * Return the atom's ID, as was originally supplied by userspace in
 * base_jd_atom_v2::atom_number
 */
static inline int kbase_jd_atom_id(struct kbase_context *kctx, struct kbase_jd_atom *katom)
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

/**
 * Initialize the disjoint state
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
 * Disjoint state is entered during GPU reset and for the entire time that an atom is replaying
 * (as part of the replay workaround). Increasing the disjoint state also increases the count of
 * disjoint events.
 *
 * The disjoint state is then used to increase the count of disjoint events during job submission
 * and job completion. Any atom submitted or completed while the disjoint state is greater than
 * zero is regarded as a disjoint event.
 *
 * The disjoint event counter is also incremented immediately whenever a job is soft stopped
 * and during context creation.
 *
 * @param kbdev The kbase device
 */
void kbase_disjoint_init(struct kbase_device *kbdev);

/**
 * Increase the count of disjoint events
 * called when a disjoint event has happened
 *
 * @param kbdev The kbase device
 */
void kbase_disjoint_event(struct kbase_device *kbdev);

/**
 * Increase the count of disjoint events only if the GPU is in a disjoint state
 *
 * This should be called when something happens which could be disjoint if the GPU
 * is in a disjoint state. The state refcount keeps track of this.
 *
 * @param kbdev The kbase device
 */
void kbase_disjoint_event_potential(struct kbase_device *kbdev);

/**
 * Returns the count of disjoint events
 *
 * @param kbdev The kbase device
 * @return the count of disjoint events
 */
u32 kbase_disjoint_event_get(struct kbase_device *kbdev);

/**
 * Increment the refcount state indicating that the GPU is in a disjoint state.
 *
 * Also Increment the disjoint event count (calls @ref kbase_disjoint_event)
 * eventually after the disjoint state has completed @ref kbase_disjoint_state_down
 * should be called
 *
 * @param kbdev The kbase device
 */
void kbase_disjoint_state_up(struct kbase_device *kbdev);

/**
 * Decrement the refcount state
 *
 * Also Increment the disjoint event count (calls @ref kbase_disjoint_event)
 *
 * Called after @ref kbase_disjoint_state_up once the disjoint state is over
 *
 * @param kbdev The kbase device
 */
void kbase_disjoint_state_down(struct kbase_device *kbdev);

/**
 * If a job is soft stopped and the number of contexts is >= this value
 * it is reported as a disjoint event
 */
#define KBASE_DISJOINT_STATE_INTERLEAVED_CONTEXT_COUNT_THRESHOLD 2

#if !defined(UINT64_MAX)
	#define UINT64_MAX ((uint64_t)0xFFFFFFFFFFFFFFFFULL)
#endif

#if KBASE_TRACE_ENABLE
void kbasep_trace_debugfs_init(struct kbase_device *kbdev);

#ifndef CONFIG_MALI_SYSTEM_TRACE
/** Add trace values about a job-slot
 *
 * @note Any functions called through this macro will still be evaluated in
 * Release builds (CONFIG_MALI_DEBUG not defined). Therefore, when KBASE_TRACE_ENABLE == 0 any
 * functions called to get the parameters supplied to this macro must:
 * - be static or static inline
 * - must just return 0 and have no other statements present in the body.
 */
#define KBASE_TRACE_ADD_SLOT(kbdev, code, ctx, katom, gpu_addr, jobslot) \
	kbasep_trace_add(kbdev, KBASE_TRACE_CODE(code), ctx, katom, gpu_addr, \
			KBASE_TRACE_FLAG_JOBSLOT, 0, jobslot, 0)

/** Add trace values about a job-slot, with info
 *
 * @note Any functions called through this macro will still be evaluated in
 * Release builds (CONFIG_MALI_DEBUG not defined). Therefore, when KBASE_TRACE_ENABLE == 0 any
 * functions called to get the parameters supplied to this macro must:
 * - be static or static inline
 * - must just return 0 and have no other statements present in the body.
 */
#define KBASE_TRACE_ADD_SLOT_INFO(kbdev, code, ctx, katom, gpu_addr, jobslot, info_val) \
	kbasep_trace_add(kbdev, KBASE_TRACE_CODE(code), ctx, katom, gpu_addr, \
			KBASE_TRACE_FLAG_JOBSLOT, 0, jobslot, info_val)

/** Add trace values about a ctx refcount
 *
 * @note Any functions called through this macro will still be evaluated in
 * Release builds (CONFIG_MALI_DEBUG not defined). Therefore, when KBASE_TRACE_ENABLE == 0 any
 * functions called to get the parameters supplied to this macro must:
 * - be static or static inline
 * - must just return 0 and have no other statements present in the body.
 */
#define KBASE_TRACE_ADD_REFCOUNT(kbdev, code, ctx, katom, gpu_addr, refcount) \
	kbasep_trace_add(kbdev, KBASE_TRACE_CODE(code), ctx, katom, gpu_addr, \
			KBASE_TRACE_FLAG_REFCOUNT, refcount, 0, 0)
/** Add trace values about a ctx refcount, and info
 *
 * @note Any functions called through this macro will still be evaluated in
 * Release builds (CONFIG_MALI_DEBUG not defined). Therefore, when KBASE_TRACE_ENABLE == 0 any
 * functions called to get the parameters supplied to this macro must:
 * - be static or static inline
 * - must just return 0 and have no other statements present in the body.
 */
#define KBASE_TRACE_ADD_REFCOUNT_INFO(kbdev, code, ctx, katom, gpu_addr, refcount, info_val) \
	kbasep_trace_add(kbdev, KBASE_TRACE_CODE(code), ctx, katom, gpu_addr, \
			KBASE_TRACE_FLAG_REFCOUNT, refcount, 0, info_val)

/** Add trace values (no slot or refcount)
 *
 * @note Any functions called through this macro will still be evaluated in
 * Release builds (CONFIG_MALI_DEBUG not defined). Therefore, when KBASE_TRACE_ENABLE == 0 any
 * functions called to get the parameters supplied to this macro must:
 * - be static or static inline
 * - must just return 0 and have no other statements present in the body.
 */
#define KBASE_TRACE_ADD(kbdev, code, ctx, katom, gpu_addr, info_val)     \
	kbasep_trace_add(kbdev, KBASE_TRACE_CODE(code), ctx, katom, gpu_addr, \
			0, 0, 0, info_val)

/** Clear the trace */
#define KBASE_TRACE_CLEAR(kbdev) \
	kbasep_trace_clear(kbdev)

/** Dump the slot trace */
#define KBASE_TRACE_DUMP(kbdev) \
	kbasep_trace_dump(kbdev)

/** PRIVATE - do not use directly. Use KBASE_TRACE_ADD() instead */
void kbasep_trace_add(struct kbase_device *kbdev, enum kbase_trace_code code, void *ctx, struct kbase_jd_atom *katom, u64 gpu_addr, u8 flags, int refcount, int jobslot, unsigned long info_val);
/** PRIVATE - do not use directly. Use KBASE_TRACE_CLEAR() instead */
void kbasep_trace_clear(struct kbase_device *kbdev);
#else /* #ifndef CONFIG_MALI_SYSTEM_TRACE */
/* Dispatch kbase trace events as system trace events */
#include <mali_linux_kbase_trace.h>
#define KBASE_TRACE_ADD_SLOT(kbdev, code, ctx, katom, gpu_addr, jobslot)\
	trace_mali_##code(jobslot, 0)

#define KBASE_TRACE_ADD_SLOT_INFO(kbdev, code, ctx, katom, gpu_addr, jobslot, info_val)\
	trace_mali_##code(jobslot, info_val)

#define KBASE_TRACE_ADD_REFCOUNT(kbdev, code, ctx, katom, gpu_addr, refcount)\
	trace_mali_##code(refcount, 0)

#define KBASE_TRACE_ADD_REFCOUNT_INFO(kbdev, code, ctx, katom, gpu_addr, refcount, info_val)\
	trace_mali_##code(refcount, info_val)

#define KBASE_TRACE_ADD(kbdev, code, ctx, katom, gpu_addr, info_val)\
	trace_mali_##code(gpu_addr, info_val)

#define KBASE_TRACE_CLEAR(kbdev)\
	do {\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(0);\
	} while (0)
#define KBASE_TRACE_DUMP(kbdev)\
	do {\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(0);\
	} while (0)

#endif /* #ifndef CONFIG_MALI_SYSTEM_TRACE */
#else
#define KBASE_TRACE_ADD_SLOT(kbdev, code, ctx, katom, gpu_addr, jobslot)\
	do {\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(code);\
		CSTD_UNUSED(ctx);\
		CSTD_UNUSED(katom);\
		CSTD_UNUSED(gpu_addr);\
		CSTD_UNUSED(jobslot);\
	} while (0)

#define KBASE_TRACE_ADD_SLOT_INFO(kbdev, code, ctx, katom, gpu_addr, jobslot, info_val)\
	do {\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(code);\
		CSTD_UNUSED(ctx);\
		CSTD_UNUSED(katom);\
		CSTD_UNUSED(gpu_addr);\
		CSTD_UNUSED(jobslot);\
		CSTD_UNUSED(info_val);\
		CSTD_NOP(0);\
	} while (0)

#define KBASE_TRACE_ADD_REFCOUNT(kbdev, code, ctx, katom, gpu_addr, refcount)\
	do {\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(code);\
		CSTD_UNUSED(ctx);\
		CSTD_UNUSED(katom);\
		CSTD_UNUSED(gpu_addr);\
		CSTD_UNUSED(refcount);\
		CSTD_NOP(0);\
	} while (0)

#define KBASE_TRACE_ADD_REFCOUNT_INFO(kbdev, code, ctx, katom, gpu_addr, refcount, info_val)\
	do {\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(code);\
		CSTD_UNUSED(ctx);\
		CSTD_UNUSED(katom);\
		CSTD_UNUSED(gpu_addr);\
		CSTD_UNUSED(info_val);\
		CSTD_NOP(0);\
	} while (0)

#define KBASE_TRACE_ADD(kbdev, code, subcode, ctx, katom, val)\
	do {\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(code);\
		CSTD_UNUSED(subcode);\
		CSTD_UNUSED(ctx);\
		CSTD_UNUSED(katom);\
		CSTD_UNUSED(val);\
		CSTD_NOP(0);\
	} while (0)

#define KBASE_TRACE_CLEAR(kbdev)\
	do {\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(0);\
	} while (0)
#define KBASE_TRACE_DUMP(kbdev)\
	do {\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(0);\
	} while (0)
#endif /* KBASE_TRACE_ENABLE */
/** PRIVATE - do not use directly. Use KBASE_TRACE_DUMP() instead */
void kbasep_trace_dump(struct kbase_device *kbdev);

#ifdef CONFIG_MALI_DEBUG
/**
 * kbase_set_driver_inactive - Force driver to go inactive
 * @kbdev:    Device pointer
 * @inactive: true if driver should go inactive, false otherwise
 *
 * Forcing the driver inactive will cause all future IOCTLs to wait until the
 * driver is made active again. This is intended solely for the use of tests
 * which require that no jobs are running while the test executes.
 */
void kbase_set_driver_inactive(struct kbase_device *kbdev, bool inactive);
#endif /* CONFIG_MALI_DEBUG */

#endif
