/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2019-2022 ARM Limited. All rights reserved.
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
 * Definitions (types, defines, etcs) specific to Job Manager Kbase.
 * They are placed here to allow the hierarchy of header files to work.
 */

#ifndef _KBASE_JM_DEFS_H_
#define _KBASE_JM_DEFS_H_

#include "mali_kbase_js_defs.h"

/* Dump Job slot trace on error (only active if KBASE_KTRACE_ENABLE != 0) */
#define KBASE_KTRACE_DUMP_ON_JOB_SLOT_ERROR 1

/*
 * Number of milliseconds before resetting the GPU when a job cannot be "zapped"
 *  from the hardware. Note that the time is actually
 * ZAP_TIMEOUT+SOFT_STOP_RESET_TIMEOUT between the context zap starting and
 * the GPU actually being reset to give other contexts time for their jobs
 * to be soft-stopped and removed from the hardware before resetting.
 */
#define ZAP_TIMEOUT             1000

/*
 * Prevent soft-stops from occurring in scheduling situations
 *
 * This is not due to HW issues, but when scheduling is desired to be more
 * predictable.
 *
 * Therefore, soft stop may still be disabled due to HW issues.
 *
 * Soft stop will still be used for non-scheduling purposes e.g. when
 * terminating a context.
 *
 * if not in use, define this value to 0 instead of being undefined.
 */
#define KBASE_DISABLE_SCHEDULING_SOFT_STOPS 0

/*
 * Prevent hard-stops from occurring in scheduling situations
 *
 * This is not due to HW issues, but when scheduling is desired to be more
 * predictable.
 *
 * Hard stop will still be used for non-scheduling purposes e.g. when
 * terminating a context.
 *
 * if not in use, define this value to 0 instead of being undefined.
 */
#define KBASE_DISABLE_SCHEDULING_HARD_STOPS 0

/* Atom has been previously soft-stopped */
#define KBASE_KATOM_FLAG_BEEN_SOFT_STOPPED (1<<1)
/* Atom has been previously retried to execute */
#define KBASE_KATOM_FLAGS_RERUN (1<<2)
/* Atom submitted with JOB_CHAIN_FLAG bit set in JS_CONFIG_NEXT register, helps
 * to disambiguate short-running job chains during soft/hard stopping of jobs
 */
#define KBASE_KATOM_FLAGS_JOBCHAIN (1<<3)
/* Atom has been previously hard-stopped. */
#define KBASE_KATOM_FLAG_BEEN_HARD_STOPPED (1<<4)
/* Atom has caused us to enter disjoint state */
#define KBASE_KATOM_FLAG_IN_DISJOINT (1<<5)
/* Atom blocked on cross-slot dependency */
#define KBASE_KATOM_FLAG_X_DEP_BLOCKED (1<<7)
/* Atom has fail dependency on cross-slot dependency */
#define KBASE_KATOM_FLAG_FAIL_BLOCKER (1<<8)
/* Atom is currently in the list of atoms blocked on cross-slot dependencies */
#define KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST (1<<9)
/* Atom requires GPU to be in protected mode */
#define KBASE_KATOM_FLAG_PROTECTED (1<<11)
/* Atom has been stored in runnable_tree */
#define KBASE_KATOM_FLAG_JSCTX_IN_TREE (1<<12)
/* Atom is waiting for L2 caches to power up in order to enter protected mode */
#define KBASE_KATOM_FLAG_HOLDING_L2_REF_PROT (1<<13)

/* SW related flags about types of JS_COMMAND action
 * NOTE: These must be masked off by JS_COMMAND_MASK
 */

/* This command causes a disjoint event */
#define JS_COMMAND_SW_CAUSES_DISJOINT 0x100

/* Bitmask of all SW related flags */
#define JS_COMMAND_SW_BITS  (JS_COMMAND_SW_CAUSES_DISJOINT)

#if (JS_COMMAND_SW_BITS & JS_COMMAND_MASK)
#error "JS_COMMAND_SW_BITS not masked off by JS_COMMAND_MASK." \
	"Must update JS_COMMAND_SW_<..> bitmasks"
#endif

/* Soft-stop command that causes a Disjoint event. This of course isn't
 * entirely masked off by JS_COMMAND_MASK
 */
#define JS_COMMAND_SOFT_STOP_WITH_SW_DISJOINT \
		(JS_COMMAND_SW_CAUSES_DISJOINT | JS_COMMAND_SOFT_STOP)

#define KBASEP_ATOM_ID_INVALID BASE_JD_ATOM_COUNT

/* Serialize atoms within a slot (ie only one atom per job slot) */
#define KBASE_SERIALIZE_INTRA_SLOT (1 << 0)
/* Serialize atoms between slots (ie only one job slot running at any time) */
#define KBASE_SERIALIZE_INTER_SLOT (1 << 1)
/* Reset the GPU after each atom completion */
#define KBASE_SERIALIZE_RESET (1 << 2)

/**
 * enum kbase_timeout_selector - The choice of which timeout to get scaled
 *                               using the lowest GPU frequency.
 * @KBASE_TIMEOUT_SELECTOR_COUNT: Number of timeout selectors. Must be last in
 *                                the enum.
 */
enum kbase_timeout_selector {

	/* Must be the last in the enum */
	KBASE_TIMEOUT_SELECTOR_COUNT
};

#if IS_ENABLED(CONFIG_DEBUG_FS)
/**
 * struct base_job_fault_event - keeps track of the atom which faulted or which
 *                               completed after the faulty atom but before the
 *                               debug data for faulty atom was dumped.
 *
 * @event_code:     event code for the atom, should != BASE_JD_EVENT_DONE for
 *                  the atom which faulted.
 * @katom:          pointer to the atom for which job fault occurred or which
 *                  completed after the faulty atom.
 * @job_fault_work: work item, queued only for the faulty atom, which waits for
 *                  the dumping to get completed and then does the bottom half
 *                  of job done for the atoms which followed the faulty atom.
 * @head:           List head used to store the atom in the global list of
 *                  faulty atoms or context specific list of atoms which got
 *                  completed during the dump.
 * @reg_offset:     offset of the register to be dumped next, only applicable
 *                  for the faulty atom.
 */
struct base_job_fault_event {

	u32 event_code;
	struct kbase_jd_atom *katom;
	struct work_struct job_fault_work;
	struct list_head head;
	int reg_offset;
};
#endif

/**
 * struct kbase_jd_atom_dependency - Contains the dependency info for an atom.
 * @atom:          pointer to the dependee atom.
 * @dep_type:      type of dependency on the dependee @atom, i.e. order or data
 *                 dependency. BASE_JD_DEP_TYPE_INVALID indicates no dependency.
 */
struct kbase_jd_atom_dependency {
	struct kbase_jd_atom *atom;
	u8 dep_type;
};

/**
 * kbase_jd_katom_dep_atom - Retrieves a read-only reference to the
 *                           dependee atom.
 * @dep:   pointer to the dependency info structure.
 *
 * Return: readonly reference to dependee atom.
 */
static inline const struct kbase_jd_atom *
kbase_jd_katom_dep_atom(const struct kbase_jd_atom_dependency *dep)
{
	return (const struct kbase_jd_atom *)(dep->atom);
}

/**
 * kbase_jd_katom_dep_type -  Retrieves the dependency type info
 *
 * @dep:   pointer to the dependency info structure.
 *
 * Return: the type of dependency there is on the dependee atom.
 */
static inline u8 kbase_jd_katom_dep_type(
		const struct kbase_jd_atom_dependency *dep)
{
	return dep->dep_type;
}

/**
 * kbase_jd_katom_dep_set - sets up the dependency info structure
 *                          as per the values passed.
 * @const_dep:    pointer to the dependency info structure to be setup.
 * @a:            pointer to the dependee atom.
 * @type:         type of dependency there is on the dependee atom.
 */
static inline void kbase_jd_katom_dep_set(
		const struct kbase_jd_atom_dependency *const_dep,
		struct kbase_jd_atom *a, u8 type)
{
	struct kbase_jd_atom_dependency *dep;

	dep = (struct kbase_jd_atom_dependency *)const_dep;

	dep->atom = a;
	dep->dep_type = type;
}

/**
 * kbase_jd_katom_dep_clear - resets the dependency info structure
 *
 * @const_dep:    pointer to the dependency info structure to be setup.
 */
static inline void kbase_jd_katom_dep_clear(
		const struct kbase_jd_atom_dependency *const_dep)
{
	struct kbase_jd_atom_dependency *dep;

	dep = (struct kbase_jd_atom_dependency *)const_dep;

	dep->atom = NULL;
	dep->dep_type = BASE_JD_DEP_TYPE_INVALID;
}

/**
 * enum kbase_atom_gpu_rb_state - The state of an atom, pertinent after it
 *                                becomes runnable, with respect to job slot
 *                                ringbuffer/fifo.
 * @KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB: Atom not currently present in slot fifo,
 *                                which implies that either atom has not become
 *                                runnable due to dependency or has completed
 *                                the execution on GPU.
 * @KBASE_ATOM_GPU_RB_WAITING_BLOCKED: Atom has been added to slot fifo but is
 *                                blocked due to cross slot dependency,
 *                                can't be submitted to GPU.
 * @KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_PREV: Atom has been added to slot
 *                                fifo but is waiting for the completion of
 *                                previously added atoms in current & other
 *                                slots, as their protected mode requirements
 *                                do not match with the current atom.
 * @KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_TRANSITION: Atom is in slot fifo
 *                                and is waiting for completion of protected
 *                                mode transition, needed before the atom is
 *                                submitted to GPU.
 * @KBASE_ATOM_GPU_RB_WAITING_FOR_CORE_AVAILABLE: Atom is in slot fifo but is
 *                                waiting for the cores, which are needed to
 *                                execute the job chain represented by the atom,
 *                                to become available
 * @KBASE_ATOM_GPU_RB_READY:      Atom is in slot fifo and can be submitted to
 *                                GPU.
 * @KBASE_ATOM_GPU_RB_SUBMITTED:  Atom is in slot fifo and has been submitted
 *                                to GPU.
 * @KBASE_ATOM_GPU_RB_RETURN_TO_JS: Atom must be returned to JS due to some
 *                                failure, but only after the previously added
 *                                atoms in fifo have completed or have also
 *                                been returned to JS.
 */
enum kbase_atom_gpu_rb_state {
	KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB,
	KBASE_ATOM_GPU_RB_WAITING_BLOCKED,
	KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_PREV,
	KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_TRANSITION,
	KBASE_ATOM_GPU_RB_WAITING_FOR_CORE_AVAILABLE,
	KBASE_ATOM_GPU_RB_READY,
	KBASE_ATOM_GPU_RB_SUBMITTED,
	KBASE_ATOM_GPU_RB_RETURN_TO_JS = -1
};

/**
 * enum kbase_atom_enter_protected_state - The state of an atom with respect to
 *                      the preparation for GPU's entry into protected mode,
 *                      becomes pertinent only after atom's state with respect
 *                      to slot ringbuffer is
 *                      KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_TRANSITION
 * @KBASE_ATOM_ENTER_PROTECTED_CHECK:  Starting state. Check if there are any
 *                      atoms currently submitted to GPU and protected mode
 *                      transition is not already in progress.
 * @KBASE_ATOM_ENTER_PROTECTED_HWCNT: Wait for hardware counter context to
 *                      become disabled before entry into protected mode.
 * @KBASE_ATOM_ENTER_PROTECTED_IDLE_L2: Wait for the L2 to become idle in
 *                      preparation for the coherency change. L2 shall be
 *                      powered down and GPU shall come out of fully
 *                      coherent mode before entering protected mode.
 * @KBASE_ATOM_ENTER_PROTECTED_SET_COHERENCY: Prepare coherency change;
 *                      for BASE_HW_ISSUE_TGOX_R1_1234 also request L2 power on
 *                      so that coherency register contains correct value when
 *                      GPU enters protected mode.
 * @KBASE_ATOM_ENTER_PROTECTED_FINISHED: End state; for
 *                      BASE_HW_ISSUE_TGOX_R1_1234 check
 *                      that L2 is powered up and switch GPU to protected mode.
 */
enum kbase_atom_enter_protected_state {
	/*
	 * NOTE: The integer value of this must match
	 * KBASE_ATOM_EXIT_PROTECTED_CHECK.
	 */
	KBASE_ATOM_ENTER_PROTECTED_CHECK = 0,
	KBASE_ATOM_ENTER_PROTECTED_HWCNT,
	KBASE_ATOM_ENTER_PROTECTED_IDLE_L2,
	KBASE_ATOM_ENTER_PROTECTED_SET_COHERENCY,
	KBASE_ATOM_ENTER_PROTECTED_FINISHED,
};

/**
 * enum kbase_atom_exit_protected_state - The state of an atom with respect to
 *                      the preparation for GPU's exit from protected mode,
 *                      becomes pertinent only after atom's state with respect
 *                      to slot ngbuffer is
 *                      KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_TRANSITION
 * @KBASE_ATOM_EXIT_PROTECTED_CHECK: Starting state. Check if there are any
 *                      atoms currently submitted to GPU and protected mode
 *                      transition is not already in progress.
 * @KBASE_ATOM_EXIT_PROTECTED_IDLE_L2: Wait for the L2 to become idle in
 *                      preparation for the reset, as exiting protected mode
 *                      requires a reset.
 * @KBASE_ATOM_EXIT_PROTECTED_RESET: Issue the reset to trigger exit from
 *                      protected mode
 * @KBASE_ATOM_EXIT_PROTECTED_RESET_WAIT: End state, Wait for the reset to
 *                      complete
 */
enum kbase_atom_exit_protected_state {
	/*
	 * NOTE: The integer value of this must match
	 * KBASE_ATOM_ENTER_PROTECTED_CHECK.
	 */
	KBASE_ATOM_EXIT_PROTECTED_CHECK = 0,
	KBASE_ATOM_EXIT_PROTECTED_IDLE_L2,
	KBASE_ATOM_EXIT_PROTECTED_RESET,
	KBASE_ATOM_EXIT_PROTECTED_RESET_WAIT,
};

/**
 * struct kbase_jd_atom  - object representing the atom, containing the complete
 *                         state and attributes of an atom.
 * @work:                  work item for the bottom half processing of the atom,
 *                         by JD or JS, after it got executed on GPU or the
 *                         input fence got signaled
 * @start_timestamp:       time at which the atom was submitted to the GPU, by
 *                         updating the JS_HEAD_NEXTn register.
 * @udata:                 copy of the user data sent for the atom in
 *                         base_jd_submit.
 * @kctx:                  Pointer to the base context with which the atom is
 *                         associated.
 * @dep_head:              Array of 2 list heads, pointing to the two list of
 *                         atoms
 *                         which are blocked due to dependency on this atom.
 * @dep_item:              Array of 2 list heads, used to store the atom in the
 *                         list of other atoms depending on the same dependee
 *                         atom.
 * @dep:                   Array containing the dependency info for the 2 atoms
 *                         on which the atom depends upon.
 * @jd_item:               List head used during job dispatch job_done
 *                         processing - as dependencies may not be entirely
 *                         resolved at this point,
 *                         we need to use a separate list head.
 * @in_jd_list:            flag set to true if atom's @jd_item is currently on
 *                         a list, prevents atom being processed twice.
 * @jit_ids:               Zero-terminated array of IDs of just-in-time memory
 *                         allocations written to by the atom. When the atom
 *                         completes, the value stored at the
 *                         &struct_base_jit_alloc_info.heap_info_gpu_addr of
 *                         each allocation is read in order to enforce an
 *                         overall physical memory usage limit.
 * @nr_extres:             number of external resources referenced by the atom.
 * @extres:                Pointer to @nr_extres VA regions containing the external
 *                         resource allocation and other information.
 *                         @nr_extres external resources referenced by the atom.
 * @device_nr:             indicates the coregroup with which the atom is
 *                         associated, when
 *                         BASE_JD_REQ_SPECIFIC_COHERENT_GROUP specified.
 * @jc:                    GPU address of the job-chain.
 * @softjob_data:          Copy of data read from the user space buffer that @jc
 *                         points to.
 * @fence:                 Stores either an input or output sync fence,
 *                         depending on soft-job type
 * @sync_waiter:           Pointer to the sync fence waiter structure passed to
 *                         the callback function on signaling of the input
 *                         fence.
 * @dma_fence:             object containing pointers to both input & output
 *                         fences and other related members used for explicit
 *                         sync through soft jobs and for the implicit
 *                         synchronization required on access to external
 *                         resources.
 * @dma_fence.fence_in:    Points to the dma-buf input fence for this atom.
 *                         The atom would complete only after the fence is
 *                         signaled.
 * @dma_fence.fence:       Points to the dma-buf output fence for this atom.
 * @dma_fence.fence_cb:    The object that is passed at the time of adding the
 *                         callback that gets invoked when @dma_fence.fence_in
 *                         is signaled.
 * @dma_fence.fence_cb_added: Flag to keep a track if the callback was successfully
 *                            added for @dma_fence.fence_in, which is supposed to be
 *                            invoked on the signaling of fence.
 * @dma_fence.context:     The dma-buf fence context number for this atom. A
 *                         unique context number is allocated to each katom in
 *                         the context on context creation.
 * @dma_fence.seqno:       The dma-buf fence sequence number for this atom. This
 *                         is increased every time this katom uses dma-buf fence
 * @event_code:            Event code for the job chain represented by the atom,
 *                         both HW and low-level SW events are represented by
 *                         event codes.
 * @core_req:              bitmask of BASE_JD_REQ_* flags specifying either
 *                         Hw or Sw requirements for the job chain represented
 *                         by the atom.
 * @ticks:                 Number of scheduling ticks for which atom has been
 *                         running on the GPU.
 * @sched_priority:        Priority of the atom for Job scheduling, as per the
 *                         KBASE_JS_ATOM_SCHED_PRIO_*.
 * @completed:             Wait queue to wait upon for the completion of atom.
 * @status:                Indicates at high level at what stage the atom is in,
 *                         as per KBASE_JD_ATOM_STATE_*, that whether it is not
 *                         in use or its queued in JD or given to JS or
 *                         submitted to Hw or it completed the execution on Hw.
 * @work_id:               used for GPU tracepoints, its a snapshot of the
 *                         'work_id' counter in kbase_jd_context which is
 *                         incremented on every call to base_jd_submit.
 * @slot_nr:               Job slot chosen for the atom.
 * @atom_flags:            bitmask of KBASE_KATOM_FLAG* flags capturing the
 *                         excat low level state of the atom.
 * @gpu_rb_state:          bitmnask of KBASE_ATOM_GPU_RB_* flags, precisely
 *                         tracking atom's state after it has entered
 *                         Job scheduler on becoming runnable. Atom
 *                         could be blocked due to cross slot dependency
 *                         or waiting for the shader cores to become available
 *                         or waiting for protected mode transitions to
 *                         complete.
 * @need_cache_flush_cores_retained: flag indicating that manual flush of GPU
 *                         cache is needed for the atom and the shader cores
 *                         used for atom have been kept on.
 * @blocked:               flag indicating that atom's resubmission to GPU is
 *                         blocked till the work item is scheduled to return the
 *                         atom to JS.
 * @seq_nr:                user-space sequence number, to order atoms in some
 *                         temporal order
 * @pre_dep:               Pointer to atom that this atom has same-slot
 *                         dependency on
 * @post_dep:              Pointer to atom that has same-slot dependency on
 *                         this atom
 * @x_pre_dep:             Pointer to atom that this atom has cross-slot
 *                         dependency on
 * @x_post_dep:            Pointer to atom that has cross-slot dependency on
 *                         this atom
 * @flush_id:              The GPU's flush count recorded at the time of
 *                         submission,
 *                         used for the cache flush optimization
 * @fault_event:           Info for dumping the debug data on Job fault.
 * @queue:                 List head used for 4 different purposes :
 *                         Adds atom to the list of dma-buf fence waiting atoms.
 *                         Adds atom to the list of atoms blocked due to cross
 *                         slot dependency.
 *                         Adds atom to the list of softjob atoms for which JIT
 *                         allocation has been deferred
 *                         Adds atom to the list of softjob atoms waiting for
 *                         the signaling of fence.
 * @jit_node:              Used to keep track of all JIT free/alloc jobs in
 *                         submission order
 * @jit_blocked:           Flag indicating that JIT allocation requested through
 *                         softjob atom will be reattempted after the impending
 *                         free of other active JIT allocations.
 * @will_fail_event_code:  If non-zero, this indicates that the atom will fail
 *                         with the set event_code when the atom is processed.
 *                         Used for special handling of atoms, which have a data
 *                         dependency on the failed atoms.
 * @protected_state:       State of the atom, as per
 *                         KBASE_ATOM_(ENTER|EXIT)_PROTECTED_*,
 *                         when transitioning into or out of protected mode.
 *                         Atom will be either entering or exiting the
 *                         protected mode.
 * @protected_state.enter: entering the protected mode.
 * @protected_state.exit:  exiting the protected mode.
 * @runnable_tree_node:    The node added to context's job slot specific rb tree
 *                         when the atom becomes runnable.
 * @age:                   Age of atom relative to other atoms in the context,
 *                         is snapshot of the age_count counter in kbase
 *                         context.
 * @jobslot: Job slot to use when BASE_JD_REQ_JOB_SLOT is specified.
 * @renderpass_id:Renderpass identifier used to associate an atom that has
 *                 BASE_JD_REQ_START_RENDERPASS set in its core requirements
 *                 with an atom that has BASE_JD_REQ_END_RENDERPASS set.
 * @jc_fragment:          Set of GPU fragment job chains
 */
struct kbase_jd_atom {
	struct work_struct work;
	ktime_t start_timestamp;

	struct base_jd_udata udata;
	struct kbase_context *kctx;

	struct list_head dep_head[2];
	struct list_head dep_item[2];
	const struct kbase_jd_atom_dependency dep[2];
	struct list_head jd_item;
	bool in_jd_list;

#if MALI_JIT_PRESSURE_LIMIT_BASE
	u8 jit_ids[2];
#endif /* MALI_JIT_PRESSURE_LIMIT_BASE */

	u16 nr_extres;
	struct kbase_va_region **extres;

	u32 device_nr;
	u64 jc;
	void *softjob_data;
#if IS_ENABLED(CONFIG_SYNC_FILE)
	struct {
		/* Use the functions/API defined in mali_kbase_fence.h to
		 * when working with this sub struct
		 */
#if IS_ENABLED(CONFIG_SYNC_FILE)
#if (KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE)
		struct fence *fence_in;
#else
		struct dma_fence *fence_in;
#endif
#endif
		/* This points to the dma-buf output fence for this atom. If
		 * this is NULL then there is no fence for this atom and the
		 * following fields related to dma_fence may have invalid data.
		 *
		 * The context and seqno fields contain the details for this
		 * fence.
		 *
		 * This fence is signaled when the katom is completed,
		 * regardless of the event_code of the katom (signal also on
		 * failure).
		 */
#if (KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE)
		struct fence *fence;
#else
		struct dma_fence *fence;
#endif

		/* This is the callback object that is registered for the fence_in.
		 * The callback is invoked when the fence_in is signaled.
		 */
#if (KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE)
		struct fence_cb fence_cb;
#else
		struct dma_fence_cb fence_cb;
#endif
		bool fence_cb_added;

		unsigned int context;
		atomic_t seqno;
	} dma_fence;
#endif /* CONFIG_SYNC_FILE */

	/* Note: refer to kbasep_js_atom_retained_state, which will take a copy
	 * of some of the following members
	 */
	enum base_jd_event_code event_code;
	base_jd_core_req core_req;
	u8 jobslot;
	u8 renderpass_id;
	struct base_jd_fragment jc_fragment;

	u32 ticks;
	int sched_priority;

	wait_queue_head_t completed;
	enum kbase_jd_atom_state status;
#if IS_ENABLED(CONFIG_GPU_TRACEPOINTS)
	int work_id;
#endif
	int slot_nr;

	u32 atom_flags;

	enum kbase_atom_gpu_rb_state gpu_rb_state;

	bool need_cache_flush_cores_retained;

	atomic_t blocked;

	u64 seq_nr;

	struct kbase_jd_atom *pre_dep;
	struct kbase_jd_atom *post_dep;

	struct kbase_jd_atom *x_pre_dep;
	struct kbase_jd_atom *x_post_dep;

	u32 flush_id;

#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct base_job_fault_event fault_event;
#endif
	struct list_head queue;

	struct list_head jit_node;
	bool jit_blocked;

	enum base_jd_event_code will_fail_event_code;

	union {
		enum kbase_atom_enter_protected_state enter;
		enum kbase_atom_exit_protected_state exit;
	} protected_state;

	struct rb_node runnable_tree_node;

	u32 age;
};

static inline bool kbase_jd_katom_is_protected(
		const struct kbase_jd_atom *katom)
{
	return (bool)(katom->atom_flags & KBASE_KATOM_FLAG_PROTECTED);
}

/**
 * kbase_jd_atom_is_younger - query if one atom is younger by age than another
 *
 * @katom_a: the first atom
 * @katom_b: the second atom
 *
 * Return: true if the first atom is strictly younger than the second,
 *         false otherwise.
 */
static inline bool kbase_jd_atom_is_younger(const struct kbase_jd_atom *katom_a,
					    const struct kbase_jd_atom *katom_b)
{
	return ((s32)(katom_a->age - katom_b->age) < 0);
}

/**
 * kbase_jd_atom_is_earlier - Check whether the first atom has been submitted
 *                            earlier than the second one
 *
 * @katom_a: the first atom
 * @katom_b: the second atom
 *
 * Return: true if the first atom has been submitted earlier than the
 * second atom. It is used to understand if an atom that is ready has been
 * submitted earlier than the currently running atom, so that the currently
 * running atom should be preempted to allow the ready atom to run.
 */
static inline bool kbase_jd_atom_is_earlier(const struct kbase_jd_atom *katom_a,
					    const struct kbase_jd_atom *katom_b)
{
	/* No seq_nr set? */
	if (!katom_a->seq_nr || !katom_b->seq_nr)
		return false;

	/* Efficiently handle the unlikely case of wrapping.
	 * The following code assumes that the delta between the sequence number
	 * of the two atoms is less than INT64_MAX.
	 * In the extremely unlikely case where the delta is higher, the comparison
	 * defaults for no preemption.
	 * The code also assumes that the conversion from unsigned to signed types
	 * works because the signed integers are 2's complement.
	 */
	return (s64)(katom_a->seq_nr - katom_b->seq_nr) < 0;
}

/*
 * Theory of operations:
 *
 * Atom objects are statically allocated within the context structure.
 *
 * Each atom is the head of two lists, one for the "left" set of dependencies,
 * one for the "right" set.
 */

#define KBASE_JD_DEP_QUEUE_SIZE 256

/**
 * enum kbase_jd_renderpass_state - State of a renderpass
 * @KBASE_JD_RP_COMPLETE: Unused or completed renderpass. Can only transition to
 *                        START.
 * @KBASE_JD_RP_START:    Renderpass making a first attempt at tiling.
 *                        Can transition to PEND_OOM or COMPLETE.
 * @KBASE_JD_RP_PEND_OOM: Renderpass whose first attempt at tiling used too much
 *                        memory and has a soft-stop pending. Can transition to
 *                        OOM or COMPLETE.
 * @KBASE_JD_RP_OOM:      Renderpass whose first attempt at tiling used too much
 *                        memory and therefore switched to incremental
 *                        rendering. The fragment job chain is forced to run.
 *                        Can only transition to RETRY.
 * @KBASE_JD_RP_RETRY:    Renderpass making a second or subsequent attempt at
 *                        tiling. Can transition to RETRY_PEND_OOM or COMPLETE.
 * @KBASE_JD_RP_RETRY_PEND_OOM: Renderpass whose second or subsequent attempt at
 *                              tiling used too much memory again and has a
 *                              soft-stop pending. Can transition to RETRY_OOM
 *                              or COMPLETE.
 * @KBASE_JD_RP_RETRY_OOM: Renderpass whose second or subsequent attempt at
 *                         tiling used too much memory again. The fragment job
 *                         chain is forced to run. Can only transition to RETRY.
 *
 * A state machine is used to control incremental rendering.
 */
enum kbase_jd_renderpass_state {
	KBASE_JD_RP_COMPLETE, /* COMPLETE => START */
	KBASE_JD_RP_START, /* START => PEND_OOM or COMPLETE */
	KBASE_JD_RP_PEND_OOM, /* PEND_OOM => OOM or COMPLETE */
	KBASE_JD_RP_OOM, /* OOM => RETRY */
	KBASE_JD_RP_RETRY, /* RETRY => RETRY_PEND_OOM or COMPLETE */
	KBASE_JD_RP_RETRY_PEND_OOM, /* RETRY_PEND_OOM => RETRY_OOM or COMPLETE */
	KBASE_JD_RP_RETRY_OOM /* RETRY_OOM => RETRY */
};

/**
 * struct kbase_jd_renderpass - Data for a renderpass
 * @state:        Current state of the renderpass. If KBASE_JD_RP_COMPLETE then
 *                all other members are invalid.
 *                Both the job dispatcher context and hwaccess_lock must be
 *                locked to modify this so that it can be read with either
 *                (or both) locked.
 * @start_katom:  Address of the atom that is the start of a renderpass.
 *                Both the job dispatcher context and hwaccess_lock must be
 *                locked to modify this so that it can be read with either
 *                (or both) locked.
 * @end_katom:    Address of the atom that is the end of a renderpass, or NULL
 *                if that atom hasn't been added to the job scheduler yet.
 *                The job dispatcher context and hwaccess_lock must be
 *                locked to modify this so that it can be read with either
 *                (or both) locked.
 * @oom_reg_list: A list of region structures which triggered out-of-memory.
 *                The hwaccess_lock must be locked to access this.
 *
 * Atoms tagged with BASE_JD_REQ_START_RENDERPASS or BASE_JD_REQ_END_RENDERPASS
 * are associated with an object of this type, which is created and maintained
 * by kbase to keep track of each renderpass.
 */
struct kbase_jd_renderpass {
	enum kbase_jd_renderpass_state state;
	struct kbase_jd_atom *start_katom;
	struct kbase_jd_atom *end_katom;
	struct list_head oom_reg_list;
};

/**
 * struct kbase_jd_context  - per context object encapsulating all the
 *                            Job dispatcher related state.
 * @lock:                     lock to serialize the updates made to the
 *                            Job dispatcher state and kbase_jd_atom objects.
 * @sched_info:               Structure encapsulating all the Job scheduling
 *                            info.
 * @atoms:                    Array of the objects representing atoms,
 *                            containing the complete state and attributes
 *                            of an atom.
 * @renderpasses:             Array of renderpass state for incremental
 *                            rendering, indexed by user-specified renderpass
 *                            ID.
 * @job_nr:                   Tracks the number of atoms being processed by the
 *                            kbase. This includes atoms that are not tracked by
 *                            scheduler: 'not ready to run' & 'dependency-only'
 *                            jobs.
 * @zero_jobs_wait:           Waitq that reflects whether there are no jobs
 *                            (including SW-only dependency jobs). This is set
 *                            when no jobs are present on the ctx, and clear
 *                            when there are jobs.
 *                            This must be updated atomically with @job_nr.
 *                            note: Job Dispatcher knows about more jobs than
 *                            the Job Scheduler as it is unaware of jobs that
 *                            are blocked on dependencies and SW-only dependency
 *                            jobs. This waitq can be waited upon to find out
 *                            when the context jobs are all done/cancelled
 *                            (including those that might've been blocked
 *                            on dependencies) - and so, whether it can be
 *                            terminated. However, it should only be terminated
 *                            once it is not present in the run-pool.
 *                            Since the waitq is only set under @lock,
 *                            the waiter should also briefly obtain and drop
 *                            @lock to guarantee that the setter has completed
 *                            its work on the kbase_context
 * @job_done_wq:              Workqueue to which the per atom work item is
 *                            queued for bottom half processing when the
 *                            atom completes
 *                            execution on GPU or the input fence get signaled.
 * @tb_lock:                  Lock to serialize the write access made to @tb to
 *                            store the register access trace messages.
 * @tb:                       Pointer to the Userspace accessible buffer storing
 *                            the trace messages for register read/write
 *                            accesses made by the Kbase. The buffer is filled
 *                            in circular fashion.
 * @tb_wrap_offset:           Offset to the end location in the trace buffer,
 *                            the write pointer is moved to the beginning on
 *                            reaching this offset.
 * @work_id:                  atomic variable used for GPU tracepoints,
 *                            incremented on every call to base_jd_submit.
 * @jit_atoms_head:           A list of the just-in-time memory soft-jobs, both
 *                            allocate & free, in submission order, protected
 *                            by kbase_jd_context.lock.
 * @jit_pending_alloc:        A list of just-in-time memory allocation
 *                            soft-jobs which will be reattempted after the
 *                            impending free of other active allocations.
 * @max_priority:             Max priority level allowed for this context.
 */
struct kbase_jd_context {
	struct mutex lock;
	struct kbasep_js_kctx_info sched_info;
	struct kbase_jd_atom atoms[BASE_JD_ATOM_COUNT];
	struct kbase_jd_renderpass renderpasses[BASE_JD_RP_COUNT];
	struct workqueue_struct *job_done_wq;

	wait_queue_head_t zero_jobs_wait;
	spinlock_t tb_lock;
	u32 *tb;
	u32 job_nr;
	size_t tb_wrap_offset;

#if IS_ENABLED(CONFIG_GPU_TRACEPOINTS)
	atomic_t work_id;
#endif

	struct list_head jit_atoms_head;
	struct list_head jit_pending_alloc;
	int max_priority;
};

/**
 * struct jsctx_queue - JS context atom queue
 * @runnable_tree: Root of RB-tree containing currently runnable atoms on this
 *                 job slot.
 * @x_dep_head:    Head item of the linked list of atoms blocked on cross-slot
 *                 dependencies. Atoms on this list will be moved to the
 *                 runnable_tree when the blocking atom completes.
 *
 * hwaccess_lock must be held when accessing this structure.
 */
struct jsctx_queue {
	struct rb_root runnable_tree;
	struct list_head x_dep_head;
};

/**
 * struct kbase_as   - Object representing an address space of GPU.
 * @number:            Index at which this address space structure is present
 *                     in an array of address space structures embedded inside
 *                     the &struct kbase_device.
 * @pf_wq:             Workqueue for processing work items related to
 *                     Page fault and Bus fault handling.
 * @work_pagefault:    Work item for the Page fault handling.
 * @work_busfault:     Work item for the Bus fault handling.
 * @pf_data:           Data relating to Page fault.
 * @bf_data:           Data relating to Bus fault.
 * @current_setup:     Stores the MMU configuration for this address space.
 */
struct kbase_as {
	int number;
	struct workqueue_struct *pf_wq;
	struct work_struct work_pagefault;
	struct work_struct work_busfault;
	struct kbase_fault pf_data;
	struct kbase_fault bf_data;
	struct kbase_mmu_setup current_setup;
};

#endif /* _KBASE_JM_DEFS_H_ */
