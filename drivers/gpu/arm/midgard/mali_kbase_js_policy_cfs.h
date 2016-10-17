/*
 *
 * (C) COPYRIGHT 2011-2016 ARM Limited. All rights reserved.
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
 * Completely Fair Job Scheduler Policy structure definitions
 */

#ifndef _KBASE_JS_POLICY_CFS_H_
#define _KBASE_JS_POLICY_CFS_H_

#define KBASEP_JS_RUNTIME_EMPTY ((u64)-1)

/**
 * struct kbasep_js_policy_cfs - Data for the CFS policy
 * @head_runtime_us:  Number of microseconds the least-run context has been
 *                    running for. The kbasep_js_device_data.queue_mutex must
 *                    be held whilst updating this.
 *                    Reads are possible without this mutex, but an older value
 *                    might be read if no memory barries are issued beforehand.
 * @least_runtime_us: Number of microseconds the least-run context in the
 *                    context queue has been running for.
 *                    -1 if context queue is empty.
 * @rt_least_runtime_us: Number of microseconds the least-run context in the
 *                       realtime (priority) context queue has been running for.
 *                       -1 if realtime context queue is empty
 */
struct kbasep_js_policy_cfs {
	u64 head_runtime_us;
	atomic64_t least_runtime_us;
	atomic64_t rt_least_runtime_us;
};

/**
 * struct kbasep_js_policy_cfs_ctx - a single linked list of all contexts
 * @runtime_us:        microseconds this context has been running for
 * @process_rt_policy: set if calling process policy scheme is a realtime
 *                     scheduler and will use the priority queue. Non-mutable
 *                     after ctx init
 * @process_priority:  calling process NICE priority, in the range -20..19
 *
 * hwaccess_lock must be held when updating @runtime_us. Initializing will occur
 * on context init and context enqueue (which can only occur in one thread at a
 * time), but multi-thread access only occurs while the context is in the
 * runpool.
 *
 * Reads are possible without the spinlock, but an older value might be read if
 * no memory barries are issued beforehand.
 */
struct kbasep_js_policy_cfs_ctx {
	u64 runtime_us;
	bool process_rt_policy;
	int process_priority;
};

/**
 * struct kbasep_js_policy_cfs_job - per job information for CFS
 * @ticks: number of ticks that this job has been executing for
 *
 * hwaccess_lock must be held when accessing @ticks.
 */
struct kbasep_js_policy_cfs_job {
	u32 ticks;
};

#endif
