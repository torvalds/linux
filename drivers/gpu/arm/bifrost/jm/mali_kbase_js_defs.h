/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2011-2018, 2020-2021 ARM Limited. All rights reserved.
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

/**
 * DOC: Job Scheduler Type Definitions
 */

#ifndef _KBASE_JS_DEFS_H_
#define _KBASE_JS_DEFS_H_

/* Forward decls */
struct kbase_device;
struct kbase_jd_atom;


typedef u32 kbase_context_flags;

/*
 * typedef kbasep_js_ctx_job_cb - Callback function run on all of a context's
 * jobs registered with the Job Scheduler
 */
typedef void kbasep_js_ctx_job_cb(struct kbase_device *kbdev,
				  struct kbase_jd_atom *katom);

/*
 * @brief Maximum number of jobs that can be submitted to a job slot whilst
 * inside the IRQ handler.
 *
 * This is important because GPU NULL jobs can complete whilst the IRQ handler
 * is running. Otherwise, it potentially allows an unlimited number of GPU NULL
 * jobs to be submitted inside the IRQ handler, which increases IRQ latency.
 */
#define KBASE_JS_MAX_JOB_SUBMIT_PER_SLOT_PER_IRQ 2

/**
 * enum kbasep_js_ctx_attr - Context attributes
 * @KBASEP_JS_CTX_ATTR_COMPUTE: Attribute indicating a context that contains
 *                              Compute jobs.
 * @KBASEP_JS_CTX_ATTR_NON_COMPUTE: Attribute indicating a context that contains
 *                                  Non-Compute jobs.
 * @KBASEP_JS_CTX_ATTR_COMPUTE_ALL_CORES: Attribute indicating that a context
 *                                        contains compute-job atoms that aren't
 *                                        restricted to a coherent group,
 *                                        and can run on all cores.
 * @KBASEP_JS_CTX_ATTR_COUNT: Must be the last in the enum
 *
 * Each context attribute can be thought of as a boolean value that caches some
 * state information about either the runpool, or the context:
 * - In the case of the runpool, it is a cache of "Do any contexts owned by
 * the runpool have attribute X?"
 * - In the case of a context, it is a cache of "Do any atoms owned by the
 * context have attribute X?"
 *
 * The boolean value of the context attributes often affect scheduling
 * decisions, such as affinities to use and job slots to use.
 *
 * To accomodate changes of state in the context, each attribute is refcounted
 * in the context, and in the runpool for all running contexts. Specifically:
 * - The runpool holds a refcount of how many contexts in the runpool have this
 * attribute.
 * - The context holds a refcount of how many atoms have this attribute.
 *
 * KBASEP_JS_CTX_ATTR_COMPUTE:
 * Attribute indicating a context that contains Compute jobs. That is,
 * the context has jobs of type @ref BASE_JD_REQ_ONLY_COMPUTE
 *
 * @note A context can be both 'Compute' and 'Non Compute' if it contains
 * both types of jobs.
 *
 * KBASEP_JS_CTX_ATTR_NON_COMPUTE:
 * Attribute indicating a context that contains Non-Compute jobs. That is,
 * the context has some jobs that are \b not of type @ref
 * BASE_JD_REQ_ONLY_COMPUTE.
 *
 * @note A context can be both 'Compute' and 'Non Compute' if it contains
 * both types of jobs.
 *
 * KBASEP_JS_CTX_ATTR_COMPUTE_ALL_CORES:
 * Attribute indicating that a context contains compute-job atoms that
 * aren't restricted to a coherent group, and can run on all cores.
 *
 * Specifically, this is when the atom's \a core_req satisfy:
 * - (\a core_req & (BASE_JD_REQ_CS | BASE_JD_REQ_ONLY_COMPUTE | BASE_JD_REQ_T) // uses slot 1 or slot 2
 * - && !(\a core_req & BASE_JD_REQ_COHERENT_GROUP) // not restricted to coherent groups
 *
 * Such atoms could be blocked from running if one of the coherent groups
 * is being used by another job slot, so tracking this context attribute
 * allows us to prevent such situations.
 *
 * @note This doesn't take into account the 1-coregroup case, where all
 * compute atoms would effectively be able to run on 'all cores', but
 * contexts will still not always get marked with this attribute. Instead,
 * it is the caller's responsibility to take into account the number of
 * coregroups when interpreting this attribute.
 *
 * @note Whilst Tiler atoms are normally combined with
 * BASE_JD_REQ_COHERENT_GROUP, it is possible to send such atoms without
 * BASE_JD_REQ_COHERENT_GROUP set. This is an unlikely case, but it's easy
 * enough to handle anyway.
 *
 */
enum kbasep_js_ctx_attr {
	KBASEP_JS_CTX_ATTR_COMPUTE,
	KBASEP_JS_CTX_ATTR_NON_COMPUTE,
	KBASEP_JS_CTX_ATTR_COMPUTE_ALL_CORES,
	KBASEP_JS_CTX_ATTR_COUNT
};

enum {
	/*
	 * Bit indicating that new atom should be started because this atom
	 * completed
	 */
	KBASE_JS_ATOM_DONE_START_NEW_ATOMS = (1u << 0),
	/*
	 * Bit indicating that the atom was evicted from the JS_NEXT registers
	 */
	KBASE_JS_ATOM_DONE_EVICTED_FROM_NEXT = (1u << 1)
};

/**
 * typedef kbasep_js_atom_done_code - Combination of KBASE_JS_ATOM_DONE_<...>
 * bits
 */
typedef u32 kbasep_js_atom_done_code;

/*
 * Context scheduling mode defines for kbase_device::js_ctx_scheduling_mode
 */
enum {
	/*
	 * In this mode, higher priority atoms will be scheduled first,
	 * regardless of the context they belong to. Newly-runnable higher
	 * priority atoms can preempt lower priority atoms currently running on
	 * the GPU, even if they belong to a different context.
	 */
	KBASE_JS_SYSTEM_PRIORITY_MODE = 0,

	/*
	 * In this mode, the highest-priority atom will be chosen from each
	 * context in turn using a round-robin algorithm, so priority only has
	 * an effect within the context an atom belongs to. Newly-runnable
	 * higher priority atoms can preempt the lower priority atoms currently
	 * running on the GPU, but only if they belong to the same context.
	 */
	KBASE_JS_PROCESS_LOCAL_PRIORITY_MODE,

	/* Must be the last in the enum */
	KBASE_JS_PRIORITY_MODE_COUNT,
};

/*
 * Internal atom priority defines for kbase_jd_atom::sched_prio
 */
enum {
	KBASE_JS_ATOM_SCHED_PRIO_FIRST = 0,
	KBASE_JS_ATOM_SCHED_PRIO_REALTIME = KBASE_JS_ATOM_SCHED_PRIO_FIRST,
	KBASE_JS_ATOM_SCHED_PRIO_HIGH,
	KBASE_JS_ATOM_SCHED_PRIO_MED,
	KBASE_JS_ATOM_SCHED_PRIO_LOW,
	KBASE_JS_ATOM_SCHED_PRIO_COUNT,
};

/* Invalid priority for kbase_jd_atom::sched_prio */
#define KBASE_JS_ATOM_SCHED_PRIO_INVALID -1

/* Default priority in the case of contexts with no atoms, or being lenient
 * about invalid priorities from userspace.
 */
#define KBASE_JS_ATOM_SCHED_PRIO_DEFAULT KBASE_JS_ATOM_SCHED_PRIO_MED

/* Atom priority bitmaps, where bit 0 is the highest priority, and higher bits
 * indicate successively lower KBASE_JS_ATOM_SCHED_PRIO_<...> levels.
 *
 * Must be strictly larger than the number of bits to represent a bitmap of
 * priorities, so that we can do calculations such as:
 *   (1 << KBASE_JS_ATOM_SCHED_PRIO_COUNT) - 1
 * ...without causing undefined behavior due to a shift beyond the width of the
 * type
 *
 * If KBASE_JS_ATOM_SCHED_PRIO_COUNT starts requiring 32 bits, then it's worth
 * moving to DECLARE_BITMAP()
 */
typedef u8 kbase_js_prio_bitmap_t;

/* Ordering modification for kbase_js_atom_runs_before() */
typedef u32 kbase_atom_ordering_flag_t;

/* Atoms of the same context and priority should have their ordering decided by
 * their seq_nr instead of their age.
 *
 * seq_nr is used as a more slowly changing variant of age - it increases once
 * per group of related atoms, as determined by user-space. Hence, it can be
 * used to limit re-ordering decisions (such as pre-emption) to only re-order
 * between such groups, rather than re-order within those groups of atoms.
 */
#define KBASE_ATOM_ORDERING_FLAG_SEQNR (((kbase_atom_ordering_flag_t)1) << 0)

/**
 * struct kbasep_js_device_data - KBase Device Data Job Scheduler sub-structure
 * @runpool_irq: Sub-structure to collect together Job Scheduling data used in
 *               IRQ context. The hwaccess_lock must be held when accessing.
 * @runpool_irq.submit_allowed: Bitvector indicating whether a currently
 *                              scheduled context is allowed to submit jobs.
 *                              When bit 'N' is set in this, it indicates whether
 *                              the context bound to address space 'N' is
 *                              allowed to submit jobs.
 * @runpool_irq.ctx_attr_ref_count: Array of Context Attributes Ref_counters:
 *     Each is large enough to hold a refcount of the number of contexts
 *     that can fit into the runpool. This is currently BASE_MAX_NR_AS.
 *     Note that when BASE_MAX_NR_AS==16 we need 5 bits (not 4) to store
 *     the refcount. Hence, it's not worthwhile reducing this to
 *     bit-manipulation on u32s to save space (where in contrast, 4 bit
 *     sub-fields would be easy to do and would save space).
 *     Whilst this must not become negative, the sign bit is used for:
 *       - error detection in debug builds
 *       - Optimization: it is undefined for a signed int to overflow, and so
 *         the compiler can optimize for that never happening (thus, no masking
 *         is required on updating the variable)
 * @runpool_irq.slot_affinities: Affinity management and tracking. Bitvector
 *                               to aid affinity checking.
 *                               Element 'n' bit 'i' indicates that slot 'n'
 *                               is using core i (i.e. slot_affinity_refcount[n][i] > 0)
 * @runpool_irq.slot_affinity_refcount: Array of fefcount for each core owned
 *     by each slot. Used to generate the slot_affinities array of bitvectors.
 *     The value of the refcount will not exceed BASE_JM_SUBMIT_SLOTS,
 *     because it is refcounted only when a job is definitely about to be
 *     submitted to a slot, and is de-refcounted immediately after a job
 *     finishes
 * @schedule_sem: Scheduling semaphore. This must be held when calling
 *                kbase_jm_kick()
 * @ctx_list_pullable: List of contexts that can currently be pulled from
 * @ctx_list_unpullable: List of contexts that can not currently be pulled
 *                       from, but have jobs currently running.
 * @nr_user_contexts_running: Number of currently scheduled user contexts
 *                            (excluding ones that are not submitting jobs)
 * @nr_all_contexts_running: Number of currently scheduled contexts (including
 *                           ones that are not submitting jobs)
 * @js_reqs: Core Requirements to match up with base_js_atom's core_req memeber
 *           @note This is a write-once member, and so no locking is required to
 *           read
 * @scheduling_period_ns:	Value for JS_SCHEDULING_PERIOD_NS
 * @soft_stop_ticks:		Value for JS_SOFT_STOP_TICKS
 * @soft_stop_ticks_cl:		Value for JS_SOFT_STOP_TICKS_CL
 * @hard_stop_ticks_ss:		Value for JS_HARD_STOP_TICKS_SS
 * @hard_stop_ticks_cl:		Value for JS_HARD_STOP_TICKS_CL
 * @hard_stop_ticks_dumping:	Value for JS_HARD_STOP_TICKS_DUMPING
 * @gpu_reset_ticks_ss:		Value for JS_RESET_TICKS_SS
 * @gpu_reset_ticks_cl:		Value for JS_RESET_TICKS_CL
 * @gpu_reset_ticks_dumping:	Value for JS_RESET_TICKS_DUMPING
 * @ctx_timeslice_ns:		Value for JS_CTX_TIMESLICE_NS
 * @suspended_soft_jobs_list:	List of suspended soft jobs
 * @softstop_always:		Support soft-stop on a single context
 * @init_status:The initialized-flag is placed at the end, to avoid
 *              cache-pollution (we should only be using this during init/term paths).
 *              @note This is a write-once member, and so no locking is required to
 *              read
 * @nr_contexts_pullable:Number of contexts that can currently be pulled from
 * @nr_contexts_runnable:Number of contexts that can either be pulled from or
 *                       arecurrently running
 * @soft_job_timeout_ms:Value for JS_SOFT_JOB_TIMEOUT
 * @queue_mutex: Queue Lock, used to access the Policy's queue of contexts
 *               independently of the Run Pool.
 *               Of course, you don't need the Run Pool lock to access this.
 * @runpool_mutex: Run Pool mutex, for managing contexts within the runpool.
 *
 * This encapsulates the current context of the Job Scheduler on a particular
 * device. This context is global to the device, and is not tied to any
 * particular struct kbase_context running on the device.
 *
 * nr_contexts_running and as_free are optimized for packing together (by making
 * them smaller types than u32). The operations on them should rarely involve
 * masking. The use of signed types for arithmetic indicates to the compiler
 * that the value will not rollover (which would be undefined behavior), and so
 * under the Total License model, it is free to make optimizations based on
 * that (i.e. to remove masking).
 */
struct kbasep_js_device_data {
	struct runpool_irq {
		u16 submit_allowed;
		s8 ctx_attr_ref_count[KBASEP_JS_CTX_ATTR_COUNT];
		u64 slot_affinities[BASE_JM_MAX_NR_SLOTS];
		s8 slot_affinity_refcount[BASE_JM_MAX_NR_SLOTS][64];
	} runpool_irq;
	struct semaphore schedule_sem;
	struct list_head ctx_list_pullable[BASE_JM_MAX_NR_SLOTS]
					  [KBASE_JS_ATOM_SCHED_PRIO_COUNT];
	struct list_head ctx_list_unpullable[BASE_JM_MAX_NR_SLOTS]
					    [KBASE_JS_ATOM_SCHED_PRIO_COUNT];
	s8 nr_user_contexts_running;
	s8 nr_all_contexts_running;
	base_jd_core_req js_reqs[BASE_JM_MAX_NR_SLOTS];

	u32 scheduling_period_ns;
	u32 soft_stop_ticks;
	u32 soft_stop_ticks_cl;
	u32 hard_stop_ticks_ss;
	u32 hard_stop_ticks_cl;
	u32 hard_stop_ticks_dumping;
	u32 gpu_reset_ticks_ss;
	u32 gpu_reset_ticks_cl;
	u32 gpu_reset_ticks_dumping;
	u32 ctx_timeslice_ns;

	struct list_head suspended_soft_jobs_list;

#ifdef CONFIG_MALI_BIFROST_DEBUG
	bool softstop_always;
#endif				/* CONFIG_MALI_BIFROST_DEBUG */
	int init_status;
	u32 nr_contexts_pullable;
	atomic_t nr_contexts_runnable;
	atomic_t soft_job_timeout_ms;
	struct mutex queue_mutex;
	/*
	 * Run Pool mutex, for managing contexts within the runpool.
	 * Unless otherwise specified, you must hold this lock whilst accessing
	 * any members that follow
	 *
	 * In addition, this is used to access:
	 * * the kbasep_js_kctx_info::runpool substructure
	 */
	struct mutex runpool_mutex;
};

/**
 * struct kbasep_js_kctx_info - KBase Context Job Scheduling information
 *	structure
 * @ctx: Job Scheduler Context information sub-structure.Its members are
 *	accessed regardless of whether the context is:
 *	- In the Policy's Run Pool
 *	- In the Policy's Queue
 *	- Not queued nor in the Run Pool.
 *	You must obtain the @ctx.jsctx_mutex before accessing any other members
 *	of this substructure.
 *	You may not access any of its members from IRQ context.
 * @ctx.jsctx_mutex: Job Scheduler Context lock
 * @ctx.nr_jobs: Number of jobs <b>ready to run</b> - does \em not include
 *	the jobs waiting in the dispatcher, and dependency-only
 *	jobs. See kbase_jd_context::job_nr for such jobs
 * @ctx.ctx_attr_ref_count: Context Attributes ref count. Each is large enough
 *	to hold a refcount of the number of atoms on the context.
 * @ctx.is_scheduled_wait: Wait queue to wait for KCTX_SHEDULED flag state
 *	changes.
 * @ctx.ctx_list_entry: Link implementing JS queues. Context can be present on
 *	one list per job slot.
 * @init_status: The initalized-flag is placed at the end, to avoid
 *	cache-pollution (we should only be using this during init/term paths)
 *
 * This is a substructure in the struct kbase_context that encapsulates all the
 * scheduling information.
 */
struct kbasep_js_kctx_info {
	struct kbase_jsctx {
		struct mutex jsctx_mutex;

		u32 nr_jobs;
		u32 ctx_attr_ref_count[KBASEP_JS_CTX_ATTR_COUNT];
		wait_queue_head_t is_scheduled_wait;
		struct list_head ctx_list_entry[BASE_JM_MAX_NR_SLOTS];
	} ctx;
	int init_status;
};

/**
 * struct kbasep_js_atom_retained_state - Subset of atom state.
 * @event_code: to determine whether the atom has finished
 * @core_req: core requirements
 * @sched_priority: priority
 * @device_nr: Core group atom was executed on
 *
 * Subset of atom state that can be available after kbase_jd_done_nolock() is called
 * on that atom. A copy must be taken via kbasep_js_atom_retained_state_copy(),
 * because the original atom could disappear.
 */
struct kbasep_js_atom_retained_state {
	/* Event code - to determine whether the atom has finished */
	enum base_jd_event_code event_code;
	/* core requirements */
	base_jd_core_req core_req;
	/* priority */
	int sched_priority;
	/* Core group atom was executed on */
	u32 device_nr;

};

/*
 * Value signifying 'no retry on a slot required' for:
 * - kbase_js_atom_retained_state::retry_submit_on_slot
 * - kbase_jd_atom::retry_submit_on_slot
 */
#define KBASEP_JS_RETRY_SUBMIT_SLOT_INVALID (-1)

/*
 * base_jd_core_req value signifying 'invalid' for a
 * kbase_jd_atom_retained_state. See kbase_atom_retained_state_is_valid()
 */
#define KBASEP_JS_ATOM_RETAINED_STATE_CORE_REQ_INVALID BASE_JD_REQ_DEP

/*
 * The JS timer resolution, in microseconds
 * Any non-zero difference in time will be at least this size.
 */
#define KBASEP_JS_TICK_RESOLUTION_US 1

/**
 * struct kbase_jsctx_slot_tracking - Job Scheduling tracking of a context's
 *                                    use of a job slot
 * @blocked: bitmap of priorities that this slot is blocked at
 * @atoms_pulled: counts of atoms that have been pulled from this slot,
 *                across all priority levels
 * @atoms_pulled_pri: counts of atoms that have been pulled from this slot, per
 *                    priority level
 *
 * Controls how a slot from the &struct kbase_context's jsctx_queue is managed,
 * for example to ensure correct ordering of atoms when atoms of different
 * priorities are unpulled.
 */
struct kbase_jsctx_slot_tracking {
	kbase_js_prio_bitmap_t blocked;
	atomic_t atoms_pulled;
	int atoms_pulled_pri[KBASE_JS_ATOM_SCHED_PRIO_COUNT];
};

#endif /* _KBASE_JS_DEFS_H_ */
