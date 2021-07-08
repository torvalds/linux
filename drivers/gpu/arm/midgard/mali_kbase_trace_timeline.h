/*
 *
 * (C) COPYRIGHT 2012-2016 ARM Limited. All rights reserved.
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





#if !defined(_KBASE_TRACE_TIMELINE_H)
#define _KBASE_TRACE_TIMELINE_H

#ifdef CONFIG_MALI_TRACE_TIMELINE

enum kbase_trace_timeline_code {
	#define KBASE_TIMELINE_TRACE_CODE(enum_val, desc, format, format_desc) enum_val
	#include "mali_kbase_trace_timeline_defs.h"
	#undef KBASE_TIMELINE_TRACE_CODE
};

#ifdef CONFIG_DEBUG_FS

/** Initialize Timeline DebugFS entries */
void kbasep_trace_timeline_debugfs_init(struct kbase_device *kbdev);

#else /* CONFIG_DEBUG_FS */

#define kbasep_trace_timeline_debugfs_init CSTD_NOP

#endif /* CONFIG_DEBUG_FS */

/* mali_timeline.h defines kernel tracepoints used by the KBASE_TIMELINE
 * functions.
 * Output is timestamped by either sched_clock() (default), local_clock(), or
 * cpu_clock(), depending on /sys/kernel/debug/tracing/trace_clock */
#include "mali_timeline.h"

/* Trace number of atoms in flight for kctx (atoms either not completed, or in
   process of being returned to user */
#define KBASE_TIMELINE_ATOMS_IN_FLIGHT(kctx, count)                          \
	do {                                                                 \
		struct timespec64 ts;                                          \
		ktime_get_raw_ts64(&ts);                                        \
		trace_mali_timeline_atoms_in_flight(ts.tv_sec, ts.tv_nsec,   \
				(int)kctx->timeline.owner_tgid,              \
				count);                                      \
	} while (0)

/* Trace atom_id being Ready to Run */
#define KBASE_TIMELINE_ATOM_READY(kctx, atom_id)                             \
	do {                                                                 \
		struct timespec64 ts;                                          \
		ktime_get_raw_ts64(&ts);                                        \
		trace_mali_timeline_atom(ts.tv_sec, ts.tv_nsec,              \
				CTX_FLOW_ATOM_READY,                         \
				(int)kctx->timeline.owner_tgid,              \
				atom_id);                                    \
	} while (0)

/* Trace number of atoms submitted to job slot js
 *
 * NOTE: This uses a different tracepoint to the head/next/soft-stop actions,
 * so that those actions can be filtered out separately from this
 *
 * This is because this is more useful, as we can use it to calculate general
 * utilization easily and accurately */
#define KBASE_TIMELINE_ATOMS_SUBMITTED(kctx, js, count)                      \
	do {                                                                 \
		struct timespec64 ts;                                          \
		ktime_get_raw_ts64(&ts);                                        \
		trace_mali_timeline_gpu_slot_active(ts.tv_sec, ts.tv_nsec,   \
				SW_SET_GPU_SLOT_ACTIVE,                      \
				(int)kctx->timeline.owner_tgid,              \
				js, count);                                  \
	} while (0)


/* Trace atoms present in JS_NEXT */
#define KBASE_TIMELINE_JOB_START_NEXT(kctx, js, count)                       \
	do {                                                                 \
		struct timespec64 ts;                                          \
		ktime_get_raw_ts64(&ts);                                        \
		trace_mali_timeline_gpu_slot_action(ts.tv_sec, ts.tv_nsec,   \
				SW_SET_GPU_SLOT_NEXT,                        \
				(int)kctx->timeline.owner_tgid,              \
				js, count);                                  \
	} while (0)

/* Trace atoms present in JS_HEAD */
#define KBASE_TIMELINE_JOB_START_HEAD(kctx, js, count)                       \
	do {                                                                 \
		struct timespec64 ts;                                          \
		ktime_get_raw_ts64(&ts);                                        \
		trace_mali_timeline_gpu_slot_action(ts.tv_sec, ts.tv_nsec,   \
				SW_SET_GPU_SLOT_HEAD,                        \
				(int)kctx->timeline.owner_tgid,              \
				js, count);                                  \
	} while (0)

/* Trace that a soft stop/evict from next is being attempted on a slot */
#define KBASE_TIMELINE_TRY_SOFT_STOP(kctx, js, count) \
	do {                                                                 \
		struct timespec64 ts;                                          \
		ktime_get_raw_ts64(&ts);                                        \
		trace_mali_timeline_gpu_slot_action(ts.tv_sec, ts.tv_nsec,   \
				SW_SET_GPU_SLOT_STOPPING,                    \
				(kctx) ? (int)kctx->timeline.owner_tgid : 0, \
				js, count);                                  \
	} while (0)



/* Trace state of overall GPU power */
#define KBASE_TIMELINE_GPU_POWER(kbdev, active)                              \
	do {                                                                 \
		struct timespec64 ts;                                          \
		ktime_get_raw_ts64(&ts);                                        \
		trace_mali_timeline_gpu_power_active(ts.tv_sec, ts.tv_nsec,  \
				SW_SET_GPU_POWER_ACTIVE, active);            \
	} while (0)

/* Trace state of tiler power */
#define KBASE_TIMELINE_POWER_TILER(kbdev, bitmap)                            \
	do {                                                                 \
		struct timespec64 ts;                                          \
		ktime_get_raw_ts64(&ts);                                        \
		trace_mali_timeline_gpu_power_active(ts.tv_sec, ts.tv_nsec,  \
				SW_SET_GPU_POWER_TILER_ACTIVE,               \
				hweight64(bitmap));                          \
	} while (0)

/* Trace number of shaders currently powered */
#define KBASE_TIMELINE_POWER_SHADER(kbdev, bitmap)                           \
	do {                                                                 \
		struct timespec64 ts;                                          \
		ktime_get_raw_ts64(&ts);                                        \
		trace_mali_timeline_gpu_power_active(ts.tv_sec, ts.tv_nsec,  \
				SW_SET_GPU_POWER_SHADER_ACTIVE,              \
				hweight64(bitmap));                          \
	} while (0)

/* Trace state of L2 power */
#define KBASE_TIMELINE_POWER_L2(kbdev, bitmap)                               \
	do {                                                                 \
		struct timespec64 ts;                                          \
		ktime_get_raw_ts64(&ts);                                        \
		trace_mali_timeline_gpu_power_active(ts.tv_sec, ts.tv_nsec,  \
				SW_SET_GPU_POWER_L2_ACTIVE,                  \
				hweight64(bitmap));                          \
	} while (0)

/* Trace state of L2 cache*/
#define KBASE_TIMELINE_POWERING_L2(kbdev)                                    \
	do {                                                                 \
		struct timespec64 ts;                                          \
		ktime_get_raw_ts64(&ts);                                        \
		trace_mali_timeline_l2_power_active(ts.tv_sec, ts.tv_nsec,   \
				SW_FLOW_GPU_POWER_L2_POWERING,               \
				1);                                          \
	} while (0)

#define KBASE_TIMELINE_POWERED_L2(kbdev)                                     \
	do {                                                                 \
		struct timespec64 ts;                                          \
		ktime_get_raw_ts64(&ts);                                        \
		trace_mali_timeline_l2_power_active(ts.tv_sec, ts.tv_nsec,   \
				SW_FLOW_GPU_POWER_L2_ACTIVE,                 \
				1);                                          \
	} while (0)

/* Trace kbase_pm_send_event message send */
#define KBASE_TIMELINE_PM_SEND_EVENT(kbdev, event_type, pm_event_id)         \
	do {                                                                 \
		struct timespec64 ts;                                          \
		ktime_get_raw_ts64(&ts);                                        \
		trace_mali_timeline_pm_event(ts.tv_sec, ts.tv_nsec,          \
				SW_FLOW_PM_SEND_EVENT,                       \
				event_type, pm_event_id);                    \
	} while (0)

/* Trace kbase_pm_worker message receive */
#define KBASE_TIMELINE_PM_HANDLE_EVENT(kbdev, event_type, pm_event_id)       \
	do {                                                                 \
		struct timespec64 ts;                                          \
		ktime_get_raw_ts64(&ts);                                        \
		trace_mali_timeline_pm_event(ts.tv_sec, ts.tv_nsec,          \
				SW_FLOW_PM_HANDLE_EVENT,                     \
				event_type, pm_event_id);                    \
	} while (0)


/* Trace atom_id starting in JS_HEAD */
#define KBASE_TIMELINE_JOB_START(kctx, js, _consumerof_atom_number)          \
	do {                                                                 \
		struct timespec64 ts;                                          \
		ktime_get_raw_ts64(&ts);                                        \
		trace_mali_timeline_slot_atom(ts.tv_sec, ts.tv_nsec,         \
				HW_START_GPU_JOB_CHAIN_SW_APPROX,            \
				(int)kctx->timeline.owner_tgid,              \
				js, _consumerof_atom_number);                \
	} while (0)

/* Trace atom_id stopping on JS_HEAD */
#define KBASE_TIMELINE_JOB_STOP(kctx, js, _producerof_atom_number_completed) \
	do {                                                                 \
		struct timespec64 ts;                                          \
		ktime_get_raw_ts64(&ts);                                        \
		trace_mali_timeline_slot_atom(ts.tv_sec, ts.tv_nsec,         \
				HW_STOP_GPU_JOB_CHAIN_SW_APPROX,             \
				(int)kctx->timeline.owner_tgid,              \
				js, _producerof_atom_number_completed);      \
	} while (0)

/** Trace beginning/end of a call to kbase_pm_check_transitions_nolock from a
 * certin caller */
#define KBASE_TIMELINE_PM_CHECKTRANS(kbdev, trace_code)                      \
	do {                                                                 \
		struct timespec64 ts;                                          \
		ktime_get_raw_ts64(&ts);                                        \
		trace_mali_timeline_pm_checktrans(ts.tv_sec, ts.tv_nsec,     \
				trace_code, 1);                              \
	} while (0)

/* Trace number of contexts active */
#define KBASE_TIMELINE_CONTEXT_ACTIVE(kbdev, count)                          \
	do {                                                                 \
		struct timespec64 ts;                                          \
		ktime_get_raw_ts64(&ts);                                        \
		trace_mali_timeline_context_active(ts.tv_sec, ts.tv_nsec,    \
				count);                                      \
	} while (0)

/* NOTE: kbase_timeline_pm_cores_func() is in mali_kbase_pm_policy.c */

/**
 * Trace that an atom is starting on a job slot
 *
 * The caller must be holding hwaccess_lock
 */
void kbase_timeline_job_slot_submit(struct kbase_device *kbdev, struct kbase_context *kctx,
		struct kbase_jd_atom *katom, int js);

/**
 * Trace that an atom has done on a job slot
 *
 * 'Done' in this sense can occur either because:
 * - the atom in JS_HEAD finished
 * - the atom in JS_NEXT was evicted
 *
 * Whether the atom finished or was evicted is passed in @a done_code
 *
 * It is assumed that the atom has already been removed from the submit slot,
 * with either:
 * - kbasep_jm_dequeue_submit_slot()
 * - kbasep_jm_dequeue_tail_submit_slot()
 *
 * The caller must be holding hwaccess_lock
 */
void kbase_timeline_job_slot_done(struct kbase_device *kbdev, struct kbase_context *kctx,
		struct kbase_jd_atom *katom, int js,
		kbasep_js_atom_done_code done_code);


/** Trace a pm event starting */
void kbase_timeline_pm_send_event(struct kbase_device *kbdev,
		enum kbase_timeline_pm_event event_sent);

/** Trace a pm event finishing */
void kbase_timeline_pm_check_handle_event(struct kbase_device *kbdev, enum kbase_timeline_pm_event event);

/** Check whether a pm event was present, and if so trace finishing it */
void kbase_timeline_pm_handle_event(struct kbase_device *kbdev, enum kbase_timeline_pm_event event);

/** Trace L2 power-up start */
void kbase_timeline_pm_l2_transition_start(struct kbase_device *kbdev);

/** Trace L2 power-up done */
void kbase_timeline_pm_l2_transition_done(struct kbase_device *kbdev);

#else

#define KBASE_TIMELINE_ATOMS_IN_FLIGHT(kctx, count) CSTD_NOP()

#define KBASE_TIMELINE_ATOM_READY(kctx, atom_id) CSTD_NOP()

#define KBASE_TIMELINE_ATOMS_SUBMITTED(kctx, js, count) CSTD_NOP()

#define KBASE_TIMELINE_JOB_START_NEXT(kctx, js, count) CSTD_NOP()

#define KBASE_TIMELINE_JOB_START_HEAD(kctx, js, count) CSTD_NOP()

#define KBASE_TIMELINE_TRY_SOFT_STOP(kctx, js, count) CSTD_NOP()

#define KBASE_TIMELINE_GPU_POWER(kbdev, active) CSTD_NOP()

#define KBASE_TIMELINE_POWER_TILER(kbdev, bitmap) CSTD_NOP()

#define KBASE_TIMELINE_POWER_SHADER(kbdev, bitmap) CSTD_NOP()

#define KBASE_TIMELINE_POWER_L2(kbdev, active) CSTD_NOP()

#define KBASE_TIMELINE_POWERING_L2(kbdev) CSTD_NOP()

#define KBASE_TIMELINE_POWERED_L2(kbdev)  CSTD_NOP()

#define KBASE_TIMELINE_PM_SEND_EVENT(kbdev, event_type, pm_event_id) CSTD_NOP()

#define KBASE_TIMELINE_PM_HANDLE_EVENT(kbdev, event_type, pm_event_id) CSTD_NOP()

#define KBASE_TIMELINE_JOB_START(kctx, js, _consumerof_atom_number) CSTD_NOP()

#define KBASE_TIMELINE_JOB_STOP(kctx, js, _producerof_atom_number_completed) CSTD_NOP()

#define KBASE_TIMELINE_PM_CHECKTRANS(kbdev, trace_code) CSTD_NOP()

#define KBASE_TIMELINE_CONTEXT_ACTIVE(kbdev, count) CSTD_NOP()

static inline void kbase_timeline_job_slot_submit(struct kbase_device *kbdev, struct kbase_context *kctx,
		struct kbase_jd_atom *katom, int js)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);
}

static inline void kbase_timeline_job_slot_done(struct kbase_device *kbdev, struct kbase_context *kctx,
		struct kbase_jd_atom *katom, int js,
		kbasep_js_atom_done_code done_code)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);
}

static inline void kbase_timeline_pm_send_event(struct kbase_device *kbdev, enum kbase_timeline_pm_event event_sent)
{
}

static inline void kbase_timeline_pm_check_handle_event(struct kbase_device *kbdev, enum kbase_timeline_pm_event event)
{
}

static inline void kbase_timeline_pm_handle_event(struct kbase_device *kbdev, enum kbase_timeline_pm_event event)
{
}

static inline void kbase_timeline_pm_l2_transition_start(struct kbase_device *kbdev)
{
}

static inline void kbase_timeline_pm_l2_transition_done(struct kbase_device *kbdev)
{
}
#endif				/* CONFIG_MALI_TRACE_TIMELINE */

#endif				/* _KBASE_TRACE_TIMELINE_H */

