/*
 *
 * (C) COPYRIGHT 2010-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



#ifndef _KBASE_H_
#define _KBASE_H_

#include <malisw/mali_malisw.h>
#include <osk/mali_osk.h>
#include <kbase/mali_ukk.h>

#include <kbase/mali_base_kernel.h>
#include <kbase/src/common/mali_kbase_uku.h>
#include <kbase/src/linux/mali_kbase_linux.h>

#include "mali_kbase_pm.h"
#include "mali_kbase_defs.h"
#include "mali_kbase_js.h"
#include "mali_kbase_mem.h"
#include "mali_kbase_security.h"

#include "mali_kbase_cpuprops.h"
#include "mali_kbase_gpuprops.h"

/**
 * @page page_base_kernel_main Kernel-side Base (KBase) APIs
 *
 * The Kernel-side Base (KBase) APIs are divided up as follows:
 * - @subpage page_kbase_js_policy
 */

/**
 * @defgroup base_kbase_api Kernel-side Base (KBase) APIs
 */

kbase_device *kbase_device_alloc(void);
/* note: configuration attributes member of kbdev needs to have been setup before calling kbase_device_init */
mali_error kbase_device_init(kbase_device *kbdev);
void kbase_device_term(kbase_device *kbdev);
void kbase_device_free(kbase_device *kbdev);
int kbase_device_has_feature(kbase_device *kbdev, u32 feature);
kbase_midgard_type kbase_device_get_type(kbase_device *kbdev);
struct kbase_device *kbase_find_device(int minor); /* Only needed for gator integration */

/**
 * Ensure that all IRQ handlers have completed execution
 *
 * @param kbdev     The kbase device
 */
void kbase_synchronize_irqs(kbase_device *kbdev);

kbase_context *kbase_create_context(kbase_device *kbdev);
void kbase_destroy_context(kbase_context *kctx);
mali_error kbase_context_set_create_flags(kbase_context *kctx, u32 flags);

mali_error kbase_instr_hwcnt_setup(kbase_context * kctx, kbase_uk_hwcnt_setup * setup);
mali_error kbase_instr_hwcnt_enable(kbase_context * kctx, kbase_uk_hwcnt_setup * setup);
mali_error kbase_instr_hwcnt_disable(kbase_context * kctx);
mali_error kbase_instr_hwcnt_clear(kbase_context * kctx);
mali_error kbase_instr_hwcnt_dump(kbase_context * kctx);
mali_error kbase_instr_hwcnt_dump_irq(kbase_context * kctx);
mali_bool kbase_instr_hwcnt_dump_complete(kbase_context * kctx, mali_bool *success);

void kbase_clean_caches_done(kbase_device *kbdev);

/**
 * The GPU has completed performance count sampling successfully.
 */
void kbase_instr_hwcnt_sample_done(kbase_device *kbdev);

mali_error kbase_create_os_context(kbase_os_context *osctx);
void kbase_destroy_os_context(kbase_os_context *osctx);

mali_error kbase_jd_init(kbase_context *kctx);
void kbase_jd_exit(kbase_context *kctx);
mali_error kbase_jd_submit(kbase_context *kctx, const kbase_uk_job_submit *user_bag);
void kbase_jd_done(kbase_jd_atom *katom, int slot_nr, ktime_t *end_timestamp, mali_bool start_new_jobs);
void kbase_jd_cancel(kbase_jd_atom *katom);
void kbase_jd_flush_workqueues(kbase_context *kctx);
void kbase_jd_zap_context(kbase_context *kctx);
mali_bool jd_done_nolock(kbase_jd_atom *katom);
void kbase_jd_free_external_resources(kbase_jd_atom *katom);

mali_error kbase_job_slot_init(kbase_device *kbdev);
void kbase_job_slot_halt(kbase_device *kbdev);
void kbase_job_slot_term(kbase_device *kbdev);
void kbase_job_done(kbase_device *kbdev, u32 done);
void kbase_job_zap_context(kbase_context *kctx);

void kbase_job_slot_softstop(kbase_device *kbdev, int js, kbase_jd_atom *target_katom);
void kbase_job_slot_hardstop(kbase_context *kctx, int js, kbase_jd_atom *target_katom);

void kbase_event_post(kbase_context *ctx, kbase_jd_atom *event);
int kbase_event_dequeue(kbase_context *ctx, base_jd_event_v2 *uevent);
int kbase_event_pending(kbase_context *ctx);
mali_error kbase_event_init(kbase_context *kctx);
void kbase_event_close(kbase_context *kctx);
void kbase_event_cleanup(kbase_context *kctx);
void kbase_event_wakeup(kbase_context *kctx);

int kbase_process_soft_job(kbase_jd_atom *katom);
mali_error kbase_prepare_soft_job(kbase_jd_atom *katom);
void kbase_finish_soft_job(kbase_jd_atom *katom);
void kbase_cancel_soft_job(kbase_jd_atom *katom);

/* api used internally for register access. Contains validation and tracing */
void kbase_reg_write(kbase_device *kbdev, u16 offset, u32 value, kbase_context * kctx);
u32 kbase_reg_read(kbase_device *kbdev, u16 offset, kbase_context * kctx);
void kbase_device_trace_register_access(kbase_context * kctx, kbase_reg_access_type type, u16 reg_offset, u32 reg_value);
void kbase_device_trace_buffer_install(kbase_context * kctx, u32 * tb, size_t size);
void kbase_device_trace_buffer_uninstall(kbase_context * kctx);

/* api to be ported per OS, only need to do the raw register access */
void kbase_os_reg_write(kbase_device *kbdev, u16 offset, u32 value);
u32 kbase_os_reg_read(kbase_device *kbdev, u16 offset);

/** Report a GPU fault.
 *
 * This function is called from the interrupt handler when a GPU fault occurs.
 * It reports the details of the fault using OSK_PRINT_WARN.
 *
 * @param kbdev     The kbase device that the GPU fault occurred from.
 * @param multiple  Zero if only GPU_FAULT was raised, non-zero if MULTIPLE_GPU_FAULTS was also set
 */
void kbase_report_gpu_fault(kbase_device *kbdev, int multiple);

/** Kill all jobs that are currently running from a context
 *
 * This is used in response to a page fault to remove all jobs from the faulting context from the hardware.
 *
 * @param kctx      The context to kill jobs from
 */
void kbase_job_kill_jobs_from_context(kbase_context *kctx);

/**
 * GPU interrupt handler
 *
 * This function is called from the interrupt handler when a GPU irq is to be handled.
 *
 * @param kbdev The kbase device to handle an IRQ for
 * @param val   The value of the GPU IRQ status register which triggered the call
 */
void kbase_gpu_interrupt(kbase_device * kbdev, u32 val);

/**
 * Prepare for resetting the GPU.
 * This function just soft-stops all the slots to ensure that as many jobs as possible are saved.
 *
 * The function returns a boolean which should be interpreted as follows:
 * - MALI_TRUE - Prepared for reset, kbase_reset_gpu should be called.
 * - MALI_FALSE - Another thread is performing a reset, kbase_reset_gpu should not be called.
 *
 * @return See description
 */
mali_bool kbase_prepare_to_reset_gpu(kbase_device *kbdev);

/**
 * Pre-locked version of @a kbase_prepare_to_reset_gpu.
 *
 * Identical to @a kbase_prepare_to_reset_gpu, except that the
 * kbasep_js_device_data::runpool_irq::lock is externally locked.
 *
 * @see kbase_prepare_to_reset_gpu
 */
mali_bool kbase_prepare_to_reset_gpu_locked(kbase_device *kbdev);

/** Reset the GPU
 *
 * This function should be called after kbase_prepare_to_reset_gpu iff it returns MALI_TRUE.
 * It should never be called without a corresponding call to kbase_prepare_to_reset_gpu.
 *
 * After this function is called (or not called if kbase_prepare_to_reset_gpu returned MALI_FALSE),
 * the caller should wait for kbdev->reset_waitq to be signalled to know when the reset has completed.
 */
void kbase_reset_gpu(kbase_device *kbdev);

/**
 * Pre-locked version of @a kbase_reset_gpu.
 *
 * Identical to @a kbase_reset_gpu, except that the
 * kbasep_js_device_data::runpool_irq::lock is externally locked.
 *
 * @see kbase_reset_gpu
 */
void kbase_reset_gpu_locked(kbase_device *kbdev);


/** Returns the name associated with a Mali exception code
 *
 * @param[in] exception_code  exception code
 * @return name associated with the exception code
 */
const char *kbase_exception_name(u32 exception_code);


void kbasep_list_trace_add(u8 tracepoint_id, kbase_device *kbdev, kbase_jd_atom *katom,
		osk_dlist *list_id, mali_bool action, u8 list_type);

void kbasep_list_trace_dump(kbase_device *kbdev);

#if KBASE_TRACE_ENABLE != 0
/** Add trace values about a job-slot
 *
 * @note Any functions called through this macro will still be evaluated in
 * Release builds (CONFIG_MALI_DEBUG not defined). Therefore, when KBASE_TRACE_ENABLE == 0 any
 * functions called to get the parameters supplied to this macro must:
 * - be static or static inline
 * - must just return 0 and have no other statements present in the body.
 */
#define KBASE_TRACE_ADD_SLOT( kbdev, code, ctx, katom, gpu_addr, jobslot ) \
    kbasep_trace_add( kbdev, KBASE_TRACE_CODE(code), ctx, katom, gpu_addr, \
                      KBASE_TRACE_FLAG_JOBSLOT, 0, jobslot, 0 )

/** Add trace values about a job-slot, with info
 *
 * @note Any functions called through this macro will still be evaluated in
 * Release builds (CONFIG_MALI_DEBUG not defined). Therefore, when KBASE_TRACE_ENABLE == 0 any
 * functions called to get the parameters supplied to this macro must:
 * - be static or static inline
 * - must just return 0 and have no other statements present in the body.
 */
#define KBASE_TRACE_ADD_SLOT_INFO( kbdev, code, ctx, katom, gpu_addr, jobslot, info_val ) \
    kbasep_trace_add( kbdev, KBASE_TRACE_CODE(code), ctx, katom, gpu_addr, \
                      KBASE_TRACE_FLAG_JOBSLOT, 0, jobslot, info_val )


/** Add trace values about a ctx refcount
 *
 * @note Any functions called through this macro will still be evaluated in
 * Release builds (CONFIG_MALI_DEBUG not defined). Therefore, when KBASE_TRACE_ENABLE == 0 any
 * functions called to get the parameters supplied to this macro must:
 * - be static or static inline
 * - must just return 0 and have no other statements present in the body.
 */
#define KBASE_TRACE_ADD_REFCOUNT( kbdev, code, ctx, katom, gpu_addr, refcount ) \
    kbasep_trace_add( kbdev, KBASE_TRACE_CODE(code), ctx, katom, gpu_addr, \
                      KBASE_TRACE_FLAG_REFCOUNT, refcount, 0, 0 )
/** Add trace values about a ctx refcount, and info
 *
 * @note Any functions called through this macro will still be evaluated in
 * Release builds (CONFIG_MALI_DEBUG not defined). Therefore, when KBASE_TRACE_ENABLE == 0 any
 * functions called to get the parameters supplied to this macro must:
 * - be static or static inline
 * - must just return 0 and have no other statements present in the body.
 */
#define KBASE_TRACE_ADD_REFCOUNT_INFO( kbdev, code, ctx, katom, gpu_addr, refcount, info_val ) \
    kbasep_trace_add( kbdev, KBASE_TRACE_CODE(code), ctx, katom, gpu_addr, \
                      KBASE_TRACE_FLAG_REFCOUNT, refcount, 0, info_val )

/** Add trace values (no slot or refcount)
 *
 * @note Any functions called through this macro will still be evaluated in
 * Release builds (CONFIG_MALI_DEBUG not defined). Therefore, when KBASE_TRACE_ENABLE == 0 any
 * functions called to get the parameters supplied to this macro must:
 * - be static or static inline
 * - must just return 0 and have no other statements present in the body.
 */
#define KBASE_TRACE_ADD( kbdev, code, ctx, katom, gpu_addr, info_val )     \
    kbasep_trace_add( kbdev, KBASE_TRACE_CODE(code), ctx, katom, gpu_addr, \
                      0, 0, 0, info_val )

/** Clear the trace */
#define KBASE_TRACE_CLEAR( kbdev ) \
    kbasep_trace_clear( kbdev )

/** Dump the slot trace */
#define KBASE_TRACE_DUMP( kbdev ) \
    kbasep_trace_dump( kbdev )

/** PRIVATE - do not use directly. Use KBASE_TRACE_ADD() instead */
void kbasep_trace_add(kbase_device *kbdev, kbase_trace_code code, void *ctx, kbase_jd_atom *katom, u64 gpu_addr,
                      u8 flags, int refcount, int jobslot, u32 info_val );
/** PRIVATE - do not use directly. Use KBASE_TRACE_CLEAR() instead */
void kbasep_trace_clear(kbase_device *kbdev);
#else /* KBASE_TRACE_ENABLE != 0 */
#define KBASE_TRACE_ADD_SLOT( kbdev, code, ctx, katom, gpu_addr, jobslot )\
	do{\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(code);\
		CSTD_UNUSED(ctx);\
		CSTD_UNUSED(katom);\
		CSTD_UNUSED(gpu_addr);\
		CSTD_UNUSED(jobslot);\
	}while(0)

#define KBASE_TRACE_ADD_SLOT_INFO( kbdev, code, ctx, katom, gpu_addr, jobslot, info_val )\
	do{\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(code);\
		CSTD_UNUSED(ctx);\
		CSTD_UNUSED(katom);\
		CSTD_UNUSED(gpu_addr);\
		CSTD_UNUSED(jobslot);\
		CSTD_UNUSED(info_val);\
		CSTD_NOP(0);\
	}while(0)

#define KBASE_TRACE_ADD_REFCOUNT( kbdev, code, ctx, katom, gpu_addr, refcount )\
	do{\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(code);\
		CSTD_UNUSED(ctx);\
		CSTD_UNUSED(katom);\
		CSTD_UNUSED(gpu_addr);\
		CSTD_UNUSED(refcount);\
		CSTD_NOP(0);\
	}while(0)

#define KBASE_TRACE_ADD_REFCOUNT_INFO( kbdev, code, ctx, katom, gpu_addr, refcount, info_val )\
	do{\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(code);\
		CSTD_UNUSED(ctx);\
		CSTD_UNUSED(katom);\
		CSTD_UNUSED(gpu_addr);\
		CSTD_UNUSED(info_val);\
		CSTD_NOP(0);\
	}while(0)

#define KBASE_TRACE_ADD( kbdev, code, subcode, ctx, katom, val )\
	do{\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(code);\
		CSTD_UNUSED(subcode);\
		CSTD_UNUSED(ctx);\
		CSTD_UNUSED(katom);\
		CSTD_UNUSED(val);\
		CSTD_NOP(0);\
	}while(0)

#define KBASE_TRACE_CLEAR( kbdev )\
	do{\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(0);\
	}while(0)
#define KBASE_TRACE_DUMP( kbdev )\
	do{\
		CSTD_UNUSED(kbdev);\
		CSTD_NOP(0);\
	}while(0)

#endif /* KBASE_TRACE_ENABLE != 0 */
/** PRIVATE - do not use directly. Use KBASE_TRACE_DUMP() instead */
void kbasep_trace_dump(kbase_device *kbdev);
#endif
