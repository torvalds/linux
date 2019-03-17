/*
 *
 * (C) COPYRIGHT 2011-2018 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */



/**
 * @file mali_kbase_defs.h
 *
 * Defintions (types, defines, etcs) common to Kbase. They are placed here to
 * allow the hierarchy of header files to work.
 */

#ifndef _KBASE_DEFS_H_
#define _KBASE_DEFS_H_

#include <mali_kbase_config.h>
#include <mali_base_hwconfig_features.h>
#include <mali_base_hwconfig_issues.h>
#include <mali_kbase_mem_lowlevel.h>
#include <mali_kbase_mmu_hw.h>
#include <mali_kbase_instr_defs.h>
#include <mali_kbase_pm.h>
#include <mali_kbase_gpuprops_types.h>
#include <protected_mode_switcher.h>

#include <linux/atomic.h>
#include <linux/mempool.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/sizes.h>

#ifdef CONFIG_MALI_FPGA_BUS_LOGGER
#include <linux/bus_logger.h>
#endif


#if defined(CONFIG_SYNC)
#include <sync.h>
#else
#include "mali_kbase_fence_defs.h"
#endif

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif				/* CONFIG_DEBUG_FS */

#ifdef CONFIG_MALI_BIFROST_DEVFREQ
#include <linux/devfreq.h>
#endif /* CONFIG_MALI_BIFROST_DEVFREQ */

#include <linux/clk.h>
#include <linux/regulator/consumer.h>

#if defined(CONFIG_PM_RUNTIME) || \
	(defined(CONFIG_PM) && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0))
#define KBASE_PM_RUNTIME 1
#endif

/** Enable SW tracing when set */
#ifdef CONFIG_MALI_BIFROST_ENABLE_TRACE
#define KBASE_TRACE_ENABLE 1
#endif

#ifndef KBASE_TRACE_ENABLE
#ifdef CONFIG_MALI_BIFROST_DEBUG
#define KBASE_TRACE_ENABLE 1
#else
#define KBASE_TRACE_ENABLE 0
#endif				/* CONFIG_MALI_BIFROST_DEBUG */
#endif				/* KBASE_TRACE_ENABLE */

/** Dump Job slot trace on error (only active if KBASE_TRACE_ENABLE != 0) */
#define KBASE_TRACE_DUMP_ON_JOB_SLOT_ERROR 1

/**
 * Number of milliseconds before resetting the GPU when a job cannot be "zapped" from the hardware.
 * Note that the time is actually ZAP_TIMEOUT+SOFT_STOP_RESET_TIMEOUT between the context zap starting and the GPU
 * actually being reset to give other contexts time for their jobs to be soft-stopped and removed from the hardware
 * before resetting.
 */
#define ZAP_TIMEOUT             1000

/** Number of milliseconds before we time out on a GPU soft/hard reset */
#define RESET_TIMEOUT           500

/**
 * Prevent soft-stops from occuring in scheduling situations
 *
 * This is not due to HW issues, but when scheduling is desired to be more predictable.
 *
 * Therefore, soft stop may still be disabled due to HW issues.
 *
 * @note Soft stop will still be used for non-scheduling purposes e.g. when terminating a context.
 *
 * @note if not in use, define this value to 0 instead of \#undef'ing it
 */
#define KBASE_DISABLE_SCHEDULING_SOFT_STOPS 0

/**
 * Prevent hard-stops from occuring in scheduling situations
 *
 * This is not due to HW issues, but when scheduling is desired to be more predictable.
 *
 * @note Hard stop will still be used for non-scheduling purposes e.g. when terminating a context.
 *
 * @note if not in use, define this value to 0 instead of \#undef'ing it
 */
#define KBASE_DISABLE_SCHEDULING_HARD_STOPS 0

/**
 * The maximum number of Job Slots to support in the Hardware.
 *
 * You can optimize this down if your target devices will only ever support a
 * small number of job slots.
 */
#define BASE_JM_MAX_NR_SLOTS        3

/**
 * The maximum number of Address Spaces to support in the Hardware.
 *
 * You can optimize this down if your target devices will only ever support a
 * small number of Address Spaces
 */
#define BASE_MAX_NR_AS              16

/* mmu */
#define MIDGARD_MMU_VA_BITS 48

#define MIDGARD_MMU_LEVEL(x) (x)

#define MIDGARD_MMU_TOPLEVEL    MIDGARD_MMU_LEVEL(0)

#define MIDGARD_MMU_BOTTOMLEVEL MIDGARD_MMU_LEVEL(3)

#define GROWABLE_FLAGS_REQUIRED (KBASE_REG_PF_GROW | KBASE_REG_GPU_WR)

/** setting in kbase_context::as_nr that indicates it's invalid */
#define KBASEP_AS_NR_INVALID     (-1)

#define KBASE_LOCK_REGION_MAX_SIZE (63)
#define KBASE_LOCK_REGION_MIN_SIZE (11)

#define KBASE_TRACE_SIZE_LOG2 8	/* 256 entries */
#define KBASE_TRACE_SIZE (1 << KBASE_TRACE_SIZE_LOG2)
#define KBASE_TRACE_MASK ((1 << KBASE_TRACE_SIZE_LOG2)-1)

#include "mali_kbase_js_defs.h"
#include "mali_kbase_hwaccess_defs.h"

#define KBASEP_FORCE_REPLAY_DISABLED 0

/* Maximum force replay limit when randomization is enabled */
#define KBASEP_FORCE_REPLAY_RANDOM_LIMIT 16

/** Atom has been previously soft-stoppped */
#define KBASE_KATOM_FLAG_BEEN_SOFT_STOPPPED (1<<1)
/** Atom has been previously retried to execute */
#define KBASE_KATOM_FLAGS_RERUN (1<<2)
/* Atom submitted with JOB_CHAIN_FLAG bit set in JS_CONFIG_NEXT register, helps to
 * disambiguate short-running job chains during soft/hard stopping of jobs
 */
#define KBASE_KATOM_FLAGS_JOBCHAIN (1<<3)
/** Atom has been previously hard-stopped. */
#define KBASE_KATOM_FLAG_BEEN_HARD_STOPPED (1<<4)
/** Atom has caused us to enter disjoint state */
#define KBASE_KATOM_FLAG_IN_DISJOINT (1<<5)
/* Atom blocked on cross-slot dependency */
#define KBASE_KATOM_FLAG_X_DEP_BLOCKED (1<<7)
/* Atom has fail dependency on cross-slot dependency */
#define KBASE_KATOM_FLAG_FAIL_BLOCKER (1<<8)
/* Atom is currently in the list of atoms blocked on cross-slot dependencies */
#define KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST (1<<9)
/* Atom is currently holding a context reference */
#define KBASE_KATOM_FLAG_HOLDING_CTX_REF (1<<10)
/* Atom requires GPU to be in protected mode */
#define KBASE_KATOM_FLAG_PROTECTED (1<<11)
/* Atom has been stored in runnable_tree */
#define KBASE_KATOM_FLAG_JSCTX_IN_TREE (1<<12)

/* SW related flags about types of JS_COMMAND action
 * NOTE: These must be masked off by JS_COMMAND_MASK */

/** This command causes a disjoint event */
#define JS_COMMAND_SW_CAUSES_DISJOINT 0x100

/** Bitmask of all SW related flags */
#define JS_COMMAND_SW_BITS  (JS_COMMAND_SW_CAUSES_DISJOINT)

#if (JS_COMMAND_SW_BITS & JS_COMMAND_MASK)
#error JS_COMMAND_SW_BITS not masked off by JS_COMMAND_MASK. Must update JS_COMMAND_SW_<..> bitmasks
#endif

/** Soft-stop command that causes a Disjoint event. This of course isn't
 *  entirely masked off by JS_COMMAND_MASK */
#define JS_COMMAND_SOFT_STOP_WITH_SW_DISJOINT \
		(JS_COMMAND_SW_CAUSES_DISJOINT | JS_COMMAND_SOFT_STOP)

#define KBASEP_ATOM_ID_INVALID BASE_JD_ATOM_COUNT

/* Serialize atoms within a slot (ie only one atom per job slot) */
#define KBASE_SERIALIZE_INTRA_SLOT (1 << 0)
/* Serialize atoms between slots (ie only one job slot running at any time) */
#define KBASE_SERIALIZE_INTER_SLOT (1 << 1)
/* Reset the GPU after each atom completion */
#define KBASE_SERIALIZE_RESET (1 << 2)

/* Forward declarations */
struct kbase_context;
struct kbase_device;
struct kbase_as;
struct kbase_mmu_setup;
struct kbase_ipa_model_vinstr_data;

#ifdef CONFIG_DEBUG_FS
/**
 * struct base_job_fault_event - keeps track of the atom which faulted or which
 *                               completed after the faulty atom but before the
 *                               debug data for faulty atom was dumped.
 *
 * @event_code:     event code for the atom, should != BASE_JD_EVENT_DONE for the
 *                  atom which faulted.
 * @katom:          pointer to the atom for which job fault occurred or which completed
 *                  after the faulty atom.
 * @job_fault_work: work item, queued only for the faulty atom, which waits for
 *                  the dumping to get completed and then does the bottom half
 *                  of job done for the atoms which followed the faulty atom.
 * @head:           List head used to store the atom in the global list of faulty
 *                  atoms or context specific list of atoms which got completed
 *                  during the dump.
 * @reg_offset:     offset of the register to be dumped next, only applicable for
 *                  the faulty atom.
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
 * struct kbase_io_access - holds information about 1 register access
 *
 * @addr: first bit indicates r/w (r=0, w=1)
 * @value: value written or read
 */
struct kbase_io_access {
	uintptr_t addr;
	u32 value;
};

/**
 * struct kbase_io_history - keeps track of all recent register accesses
 *
 * @enabled: true if register accesses are recorded, false otherwise
 * @lock: spinlock protecting kbase_io_access array
 * @count: number of registers read/written
 * @size: number of elements in kbase_io_access array
 * @buf: array of kbase_io_access
 */
struct kbase_io_history {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	bool enabled;
#else
	u32 enabled;
#endif

	spinlock_t lock;
	size_t count;
	u16 size;
	struct kbase_io_access *buf;
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
	LOCAL_ASSERT(dep != NULL);

	return (const struct kbase_jd_atom *)(dep->atom);
}

/**
 * kbase_jd_katom_dep_type -  Retrieves the dependency type info
 *
 * @dep:   pointer to the dependency info structure.
 *
 * Return: the type of dependency there is on the dependee atom.
 */
static inline u8 kbase_jd_katom_dep_type(const struct kbase_jd_atom_dependency *dep)
{
	LOCAL_ASSERT(dep != NULL);

	return dep->dep_type;
}

/**
 * kbase_jd_katom_dep_set - sets up the dependency info structure
 *                          as per the values passed.
 * @const_dep:    pointer to the dependency info structure to be setup.
 * @a:            pointer to the dependee atom.
 * @type:         type of dependency there is on the dependee atom.
 */
static inline void kbase_jd_katom_dep_set(const struct kbase_jd_atom_dependency *const_dep,
		struct kbase_jd_atom *a, u8 type)
{
	struct kbase_jd_atom_dependency *dep;

	LOCAL_ASSERT(const_dep != NULL);

	dep = (struct kbase_jd_atom_dependency *)const_dep;

	dep->atom = a;
	dep->dep_type = type;
}

/**
 * kbase_jd_katom_dep_clear - resets the dependency info structure
 *
 * @const_dep:    pointer to the dependency info structure to be setup.
 */
static inline void kbase_jd_katom_dep_clear(const struct kbase_jd_atom_dependency *const_dep)
{
	struct kbase_jd_atom_dependency *dep;

	LOCAL_ASSERT(const_dep != NULL);

	dep = (struct kbase_jd_atom_dependency *)const_dep;

	dep->atom = NULL;
	dep->dep_type = BASE_JD_DEP_TYPE_INVALID;
}

/**
 * enum kbase_atom_gpu_rb_state - The state of an atom, pertinent after it becomes
 *                                runnable, with respect to job slot ringbuffer/fifo.
 * @KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB: Atom not currently present in slot fifo, which
 *                                implies that either atom has not become runnable
 *                                due to dependency or has completed the execution
 *                                on GPU.
 * @KBASE_ATOM_GPU_RB_WAITING_BLOCKED: Atom has been added to slot fifo but is blocked
 *                                due to cross slot dependency, can't be submitted to GPU.
 * @KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_PREV: Atom has been added to slot fifo but
 *                                is waiting for the completion of previously added atoms
 *                                in current & other slots, as their protected mode
 *                                requirements do not match with the current atom.
 * @KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_TRANSITION: Atom is in slot fifo and is
 *                                waiting for completion of protected mode transition,
 *                                needed before the atom is submitted to GPU.
 * @KBASE_ATOM_GPU_RB_WAITING_FOR_CORE_AVAILABLE: Atom is in slot fifo but is waiting
 *                                for the cores, which are needed to execute the job
 *                                chain represented by the atom, to become available
 * @KBASE_ATOM_GPU_RB_WAITING_AFFINITY: Atom is in slot fifo but is blocked on
 *                                affinity due to rmu workaround for Hw issue 8987.
 * @KBASE_ATOM_GPU_RB_READY:      Atom is in slot fifo and can be submitted to GPU.
 * @KBASE_ATOM_GPU_RB_SUBMITTED:  Atom is in slot fifo and has been submitted to GPU.
 * @KBASE_ATOM_GPU_RB_RETURN_TO_JS: Atom must be returned to JS due to some failure,
 *                                but only after the previously added atoms in fifo
 *                                have completed or have also been returned to JS.
 */
enum kbase_atom_gpu_rb_state {
	KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB,
	KBASE_ATOM_GPU_RB_WAITING_BLOCKED,
	KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_PREV,
	KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_TRANSITION,
	KBASE_ATOM_GPU_RB_WAITING_FOR_CORE_AVAILABLE,
	KBASE_ATOM_GPU_RB_WAITING_AFFINITY,
	KBASE_ATOM_GPU_RB_READY,
	KBASE_ATOM_GPU_RB_SUBMITTED,
	KBASE_ATOM_GPU_RB_RETURN_TO_JS = -1
};

/**
 * enum kbase_atom_enter_protected_state - The state of an atom with respect to the
 *                      preparation for GPU's entry into protected mode, becomes
 *                      pertinent only after atom's state with respect to slot
 *                      ringbuffer is KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_TRANSITION
 * @KBASE_ATOM_ENTER_PROTECTED_CHECK:  Starting state. Check if there are any atoms
 *                      currently submitted to GPU and protected mode transition is
 *                      not already in progress.
 * @KBASE_ATOM_ENTER_PROTECTED_VINSTR: Wait for vinstr to suspend before entry into
 *                      protected mode.
 * @KBASE_ATOM_ENTER_PROTECTED_IDLE_L2: Wait for the L2 to become idle in preparation
 *                      for the coherency change. L2 shall be powered down and GPU shall
 *                      come out of fully coherent mode before entering protected mode.
 * @KBASE_ATOM_ENTER_PROTECTED_FINISHED: End state; Prepare coherency change and switch
 *                      GPU to protected mode.
 */
enum kbase_atom_enter_protected_state {
	/**
	 * NOTE: The integer value of this must match KBASE_ATOM_EXIT_PROTECTED_CHECK.
	 */
	KBASE_ATOM_ENTER_PROTECTED_CHECK = 0,
	KBASE_ATOM_ENTER_PROTECTED_VINSTR,
	KBASE_ATOM_ENTER_PROTECTED_IDLE_L2,
	KBASE_ATOM_ENTER_PROTECTED_FINISHED,
};

/**
 * enum kbase_atom_exit_protected_state - The state of an atom with respect to the
 *                      preparation for GPU's exit from protected mode, becomes
 *                      pertinent only after atom's state with respect to slot
 *                      ringbuffer is KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_TRANSITION
 * @KBASE_ATOM_EXIT_PROTECTED_CHECK: Starting state. Check if there are any atoms
 *                      currently submitted to GPU and protected mode transition is
 *                      not already in progress.
 * @KBASE_ATOM_EXIT_PROTECTED_IDLE_L2: Wait for the L2 to become idle in preparation
 *                      for the reset, as exiting protected mode requires a reset.
 * @KBASE_ATOM_EXIT_PROTECTED_RESET: Issue the reset to trigger exit from protected mode
 * @KBASE_ATOM_EXIT_PROTECTED_RESET_WAIT: End state, Wait for the reset to complete
 */
enum kbase_atom_exit_protected_state {
	/**
	 * NOTE: The integer value of this must match KBASE_ATOM_ENTER_PROTECTED_CHECK.
	 */
	KBASE_ATOM_EXIT_PROTECTED_CHECK = 0,
	KBASE_ATOM_EXIT_PROTECTED_IDLE_L2,
	KBASE_ATOM_EXIT_PROTECTED_RESET,
	KBASE_ATOM_EXIT_PROTECTED_RESET_WAIT,
};

/**
 * struct kbase_ext_res - Contains the info for external resources referred
 *                        by an atom, which have been mapped on GPU side.
 * @gpu_address:          Start address of the memory region allocated for
 *                        the resource from GPU virtual address space.
 * @alloc:                pointer to physical pages tracking object, set on
 *                        mapping the external resource on GPU side.
 */
struct kbase_ext_res {
	u64 gpu_address;
	struct kbase_mem_phy_alloc *alloc;
};

/**
 * struct kbase_jd_atom  - object representing the atom, containing the complete
 *                         state and attributes of an atom.
 * @work:                  work item for the bottom half processing of the atom,
 *                         by JD or JS, after it got executed on GPU or the input
 *                         fence got signaled
 * @start_timestamp:       time at which the atom was submitted to the GPU, by
 *                         updating the JS_HEAD_NEXTn register.
 * @udata:                 copy of the user data sent for the atom in base_jd_submit.
 * @kctx:                  Pointer to the base context with which the atom is associated.
 * @dep_head:              Array of 2 list heads, pointing to the two list of atoms
 *                         which are blocked due to dependency on this atom.
 * @dep_item:              Array of 2 list heads, used to store the atom in the list of
 *                         other atoms depending on the same dependee atom.
 * @dep:                   Array containing the dependency info for the 2 atoms on which
 *                         the atom depends upon.
 * @jd_item:               List head used during job dispatch job_done processing - as
 *                         dependencies may not be entirely resolved at this point,
 *                         we need to use a separate list head.
 * @in_jd_list:            flag set to true if atom's @jd_item is currently on a list,
 *                         prevents atom being processed twice.
 * @nr_extres:             number of external resources referenced by the atom.
 * @extres:                pointer to the location containing info about @nr_extres
 *                         external resources referenced by the atom.
 * @device_nr:             indicates the coregroup with which the atom is associated,
 *                         when BASE_JD_REQ_SPECIFIC_COHERENT_GROUP specified.
 * @affinity:              bitmask of the shader cores on which the atom can execute.
 * @jc:                    GPU address of the job-chain.
 * @softjob_data:          Copy of data read from the user space buffer that @jc
 *                         points to.
 * @coreref_state:         state of the atom with respect to retention of shader
 *                         cores for affinity & power management.
 * @fence:                 Stores either an input or output sync fence, depending
 *                         on soft-job type
 * @sync_waiter:           Pointer to the sync fence waiter structure passed to the
 *                         callback function on signaling of the input fence.
 * @dma_fence:             object containing pointers to both input & output fences
 *                         and other related members used for explicit sync through
 *                         soft jobs and for the implicit synchronization required
 *                         on access to external resources.
 * @event_code:            Event code for the job chain represented by the atom, both
 *                         HW and low-level SW events are represented by event codes.
 * @core_req:              bitmask of BASE_JD_REQ_* flags specifying either Hw or Sw
 *                         requirements for the job chain represented by the atom.
 * @ticks:                 Number of scheduling ticks for which atom has been running
 *                         on the GPU.
 * @sched_priority:        Priority of the atom for Job scheduling, as per the
 *                         KBASE_JS_ATOM_SCHED_PRIO_*.
 * @poking:                Indicates whether poking of MMU is ongoing for the atom,
 *                         as a WA for the issue HW_ISSUE_8316.
 * @completed:             Wait queue to wait upon for the completion of atom.
 * @status:                Indicates at high level at what stage the atom is in,
 *                         as per KBASE_JD_ATOM_STATE_*, that whether it is not in
 *                         use or its queued in JD or given to JS or submitted to Hw
 *                         or it completed the execution on Hw.
 * @work_id:               used for GPU tracepoints, its a snapshot of the 'work_id'
 *                         counter in kbase_jd_context which is incremented on
 *                         every call to base_jd_submit.
 * @slot_nr:               Job slot chosen for the atom.
 * @atom_flags:            bitmask of KBASE_KATOM_FLAG* flags capturing the exact
 *                         low level state of the atom.
 * @retry_count:           Number of times this atom has been retried. Used by replay
 *                         soft job.
 * @gpu_rb_state:          bitmnask of KBASE_ATOM_GPU_RB_* flags, precisely tracking
 *                         atom's state after it has entered Job scheduler on becoming
 *                         runnable. Atom could be blocked due to cross slot dependency
 *                         or waiting for the shader cores to become available or
 *                         waiting for protected mode transitions to complete.
 * @need_cache_flush_cores_retained: flag indicating that manual flush of GPU
 *                         cache is needed for the atom and the shader cores used
 *                         for atom have been kept on.
 * @blocked:               flag indicating that atom's resubmission to GPU is
 *                         blocked till the work item is scheduled to return the
 *                         atom to JS.
 * @pre_dep:               Pointer to atom that this atom has same-slot dependency on
 * @post_dep:              Pointer to atom that has same-slot dependency on this atom
 * @x_pre_dep:             Pointer to atom that this atom has cross-slot dependency on
 * @x_post_dep:            Pointer to atom that has cross-slot dependency on this atom
 * @flush_id:              The GPU's flush count recorded at the time of submission,
 *                         used for the cache flush optimisation
 * @fault_event:           Info for dumping the debug data on Job fault.
 * @queue:                 List head used for 4 different purposes :
 *                         Adds atom to the list of dma-buf fence waiting atoms.
 *                         Adds atom to the list of atoms blocked due to cross
 *                         slot dependency.
 *                         Adds atom to the list of softjob atoms for which JIT
 *                         allocation has been deferred
 *                         Adds atom to the list of softjob atoms waiting for the
 *                         signaling of fence.
 * @jit_node:              Used to keep track of all JIT free/alloc jobs in submission order
 * @jit_blocked:           Flag indicating that JIT allocation requested through
 *                         softjob atom will be reattempted after the impending
 *                         free of other active JIT allocations.
 * @will_fail_event_code:  If non-zero, this indicates that the atom will fail
 *                         with the set event_code when the atom is processed.
 *                         Used for special handling of atoms, which have a data
 *                         dependency on the failed atoms.
 * @protected_state:       State of the atom, as per KBASE_ATOM_(ENTER|EXIT)_PROTECTED_*,
 *                         when transitioning into or out of protected mode. Atom will
 *                         be either entering or exiting the protected mode.
 * @runnable_tree_node:    The node added to context's job slot specific rb tree
 *                         when the atom becomes runnable.
 * @age:                   Age of atom relative to other atoms in the context, is
 *                         snapshot of the age_count counter in kbase context.
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

	u16 nr_extres;
	struct kbase_ext_res *extres;

	u32 device_nr;
	u64 affinity;
	u64 jc;
	void *softjob_data;
	enum kbase_atom_coreref_state coreref_state;
#if defined(CONFIG_SYNC)
	struct sync_fence *fence;
	struct sync_fence_waiter sync_waiter;
#endif				/* CONFIG_SYNC */
#if defined(CONFIG_MALI_BIFROST_DMA_FENCE) || defined(CONFIG_SYNC_FILE)
	struct {
		/* Use the functions/API defined in mali_kbase_fence.h to
		 * when working with this sub struct */
#if defined(CONFIG_SYNC_FILE)
		/* Input fence */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0))
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
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0))
		struct fence *fence;
#else
		struct dma_fence *fence;
#endif
		/* The dma-buf fence context number for this atom. A unique
		 * context number is allocated to each katom in the context on
		 * context creation.
		 */
		unsigned int context;
		/* The dma-buf fence sequence number for this atom. This is
		 * increased every time this katom uses dma-buf fence.
		 */
		atomic_t seqno;
		/* This contains a list of all callbacks set up to wait on
		 * other fences.  This atom must be held back from JS until all
		 * these callbacks have been called and dep_count have reached
		 * 0. The initial value of dep_count must be equal to the
		 * number of callbacks on this list.
		 *
		 * This list is protected by jctx.lock. Callbacks are added to
		 * this list when the atom is built and the wait are set up.
		 * All the callbacks then stay on the list until all callbacks
		 * have been called and the atom is queued, or cancelled, and
		 * then all callbacks are taken off the list and freed.
		 */
		struct list_head callbacks;
		/* Atomic counter of number of outstandind dma-buf fence
		 * dependencies for this atom. When dep_count reaches 0 the
		 * atom may be queued.
		 *
		 * The special value "-1" may only be set after the count
		 * reaches 0, while holding jctx.lock. This indicates that the
		 * atom has been handled, either queued in JS or cancelled.
		 *
		 * If anyone but the dma-fence worker sets this to -1 they must
		 * ensure that any potentially queued worker must have
		 * completed before allowing the atom to be marked as unused.
		 * This can be done by flushing the fence work queue:
		 * kctx->dma_fence.wq.
		 */
		atomic_t dep_count;
	} dma_fence;
#endif /* CONFIG_MALI_BIFROST_DMA_FENCE || CONFIG_SYNC_FILE*/

	/* Note: refer to kbasep_js_atom_retained_state, which will take a copy of some of the following members */
	enum base_jd_event_code event_code;
	base_jd_core_req core_req;

	u32 ticks;
	int sched_priority;

	int poking;

	wait_queue_head_t completed;
	enum kbase_jd_atom_state status;
#ifdef CONFIG_GPU_TRACEPOINTS
	int work_id;
#endif
	int slot_nr;

	u32 atom_flags;

	int retry_count;

	enum kbase_atom_gpu_rb_state gpu_rb_state;

	u64 need_cache_flush_cores_retained;

	atomic_t blocked;

	struct kbase_jd_atom *pre_dep;
	struct kbase_jd_atom *post_dep;

	struct kbase_jd_atom *x_pre_dep;
	struct kbase_jd_atom *x_post_dep;

	u32 flush_id;

#ifdef CONFIG_DEBUG_FS
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

static inline bool kbase_jd_katom_is_protected(const struct kbase_jd_atom *katom)
{
	return (bool)(katom->atom_flags & KBASE_KATOM_FLAG_PROTECTED);
}

/*
 * Theory of operations:
 *
 * Atom objects are statically allocated within the context structure.
 *
 * Each atom is the head of two lists, one for the "left" set of dependencies, one for the "right" set.
 */

#define KBASE_JD_DEP_QUEUE_SIZE 256

/**
 * struct kbase_jd_context  - per context object encapsulating all the Job dispatcher
 *                            related state.
 * @lock:                     lock to serialize the updates made to the Job dispatcher
 *                            state and kbase_jd_atom objects.
 * @sched_info:               Structure encapsulating all the Job scheduling info.
 * @atoms:                    Array of the objects representing atoms, containing
 *                            the complete state and attributes of an atom.
 * @job_nr:                   Tracks the number of atoms being processed by the
 *                            kbase. This includes atoms that are not tracked by
 *                            scheduler: 'not ready to run' & 'dependency-only' jobs.
 * @zero_jobs_wait:           Waitq that reflects whether there are no jobs
 *                            (including SW-only dependency jobs). This is set
 *                            when no jobs are present on the ctx, and clear when
 *                            there are jobs.
 *                            This must be updated atomically with @job_nr.
 *                            note: Job Dispatcher knows about more jobs than the
 *                            Job Scheduler as it is unaware of jobs that are
 *                            blocked on dependencies and SW-only dependency jobs.
 *                            This waitq can be waited upon to find out when the
 *                            context jobs are all done/cancelled (including those
 *                            that might've been blocked on dependencies) - and so,
 *                            whether it can be terminated. However, it should only
 *                            be terminated once it is not present in the run-pool.
 *                            Since the waitq is only set under @lock, the waiter
 *                            should also briefly obtain and drop @lock to guarantee
 *                            that the setter has completed its work on the kbase_context
 * @job_done_wq:              Workqueue to which the per atom work item is queued
 *                            for bottom half processing when the atom completes
 *                            execution on GPU or the input fence get signaled.
 * @tb_lock:                  Lock to serialize the write access made to @tb to
 *                            to store the register access trace messages.
 * @tb:                       Pointer to the Userspace accessible buffer storing
 *                            the trace messages for register read/write accesses
 *                            made by the Kbase. The buffer is filled in circular
 *                            fashion.
 * @tb_wrap_offset:           Offset to the end location in the trace buffer, the
 *                            write pointer is moved to the beginning on reaching
 *                            this offset.
 * @work_id:                  atomic variable used for GPU tracepoints, incremented
 *                            on every call to base_jd_submit.
 */
struct kbase_jd_context {
	struct mutex lock;
	struct kbasep_js_kctx_info sched_info;
	struct kbase_jd_atom atoms[BASE_JD_ATOM_COUNT];

	u32 job_nr;

	wait_queue_head_t zero_jobs_wait;

	struct workqueue_struct *job_done_wq;

	spinlock_t tb_lock;
	u32 *tb;
	size_t tb_wrap_offset;

#ifdef CONFIG_GPU_TRACEPOINTS
	atomic_t work_id;
#endif
};

struct kbase_device_info {
	u32 features;
};

/** Poking state for BASE_HW_ISSUE_8316  */
enum {
	KBASE_AS_POKE_STATE_IN_FLIGHT     = 1<<0,
	KBASE_AS_POKE_STATE_KILLING_POKE  = 1<<1
};

/** Poking state for BASE_HW_ISSUE_8316  */
typedef u32 kbase_as_poke_state;

struct kbase_mmu_setup {
	u64	transtab;
	u64	memattr;
	u64	transcfg;
};

/**
 * struct kbase_as   - object representing an address space of GPU.
 * @number:            Index at which this address space structure is present
 *                     in an array of address space structures embedded inside the
 *                     struct kbase_device.
 * @pf_wq:             Workqueue for processing work items related to Bus fault
 *                     and Page fault handling.
 * @work_pagefault:    Work item for the Page fault handling.
 * @work_busfault:     Work item for the Bus fault handling.
 * @fault_type:        Type of fault which occured for this address space,
 *                     regular/unexpected Bus or Page fault.
 * @protected_mode:    Flag indicating whether the fault occurred in protected
 *                     mode or not.
 * @fault_status:      Records the fault status as reported by Hw.
 * @fault_addr:        Records the faulting address.
 * @fault_extra_addr:  Records the secondary fault address.
 * @current_setup:     Stores the MMU configuration for this address space.
 * @poke_wq:           Workqueue to process the work items queue for poking the
 *                     MMU as a WA for BASE_HW_ISSUE_8316.
 * @poke_work:         Work item to do the poking of MMU for this address space.
 * @poke_refcount:     Refcount for the need of poking MMU. While the refcount is
 *                     non zero the poking of MMU will continue.
 *                     Protected by hwaccess_lock.
 * @poke_state:        State indicating whether poking is in progress or it has
 *                     been stopped. Protected by hwaccess_lock.
 * @poke_timer:        Timer used to schedule the poking at regular intervals.
 */
struct kbase_as {
	int number;
	struct workqueue_struct *pf_wq;
	struct work_struct work_pagefault;
	struct work_struct work_busfault;
	enum kbase_mmu_fault_type fault_type;
	bool protected_mode;
	u32 fault_status;
	u64 fault_addr;
	u64 fault_extra_addr;
	struct kbase_mmu_setup current_setup;
	struct workqueue_struct *poke_wq;
	struct work_struct poke_work;
	int poke_refcount;
	kbase_as_poke_state poke_state;
	struct hrtimer poke_timer;
};

static inline int kbase_as_has_bus_fault(struct kbase_as *as)
{
	return as->fault_type == KBASE_MMU_FAULT_TYPE_BUS;
}

static inline int kbase_as_has_page_fault(struct kbase_as *as)
{
	return as->fault_type == KBASE_MMU_FAULT_TYPE_PAGE;
}

struct kbasep_mem_device {
	atomic_t used_pages;   /* Tracks usage of OS shared memory. Updated
				   when OS memory is allocated/freed. */

};

#define KBASE_TRACE_CODE(X) KBASE_TRACE_CODE_ ## X

enum kbase_trace_code {
	/* IMPORTANT: USE OF SPECIAL #INCLUDE OF NON-STANDARD HEADER FILE
	 * THIS MUST BE USED AT THE START OF THE ENUM */
#define KBASE_TRACE_CODE_MAKE_CODE(X) KBASE_TRACE_CODE(X)
#include "mali_kbase_trace_defs.h"
#undef  KBASE_TRACE_CODE_MAKE_CODE
	/* Comma on its own, to extend the list */
	,
	/* Must be the last in the enum */
	KBASE_TRACE_CODE_COUNT
};

#define KBASE_TRACE_FLAG_REFCOUNT (((u8)1) << 0)
#define KBASE_TRACE_FLAG_JOBSLOT  (((u8)1) << 1)

/**
 * struct kbase_trace - object representing a trace message added to trace buffer
 *                      kbase_device::trace_rbuf
 * @timestamp:          CPU timestamp at which the trace message was added.
 * @thread_id:          id of the thread in the context of which trace message
 *                      was added.
 * @cpu:                indicates which CPU the @thread_id was scheduled on when
 *                      the trace message was added.
 * @ctx:                Pointer to the kbase context for which the trace message
 *                      was added. Will be NULL for certain trace messages like
 *                      for traces added corresponding to power management events.
 *                      Will point to the appropriate context corresponding to
 *                      job-slot & context's reference count related events.
 * @katom:              indicates if the trace message has atom related info.
 * @atom_number:        id of the atom for which trace message was added.
 *                      Only valid if @katom is true.
 * @atom_udata:         Copy of the user data sent for the atom in base_jd_submit.
 *                      Only valid if @katom is true.
 * @gpu_addr:           GPU address of the job-chain represented by atom. Could
 *                      be valid even if @katom is false.
 * @info_val:           value specific to the type of event being traced. For the
 *                      case where @katom is true, will be set to atom's affinity,
 *                      i.e. bitmask of shader cores chosen for atom's execution.
 * @code:               Identifies the event, refer enum kbase_trace_code.
 * @jobslot:            job-slot for which trace message was added, valid only for
 *                      job-slot management events.
 * @refcount:           reference count for the context, valid for certain events
 *                      related to scheduler core and policy.
 * @flags:              indicates if info related to @jobslot & @refcount is present
 *                      in the trace message, used during dumping of the message.
 */
struct kbase_trace {
	struct timespec timestamp;
	u32 thread_id;
	u32 cpu;
	void *ctx;
	bool katom;
	int atom_number;
	u64 atom_udata[2];
	u64 gpu_addr;
	unsigned long info_val;
	u8 code;
	u8 jobslot;
	u8 refcount;
	u8 flags;
};

/** Event IDs for the power management framework.
 *
 * Any of these events might be missed, so they should not be relied upon to
 * find the precise state of the GPU at a particular time in the
 * trace. Overall, we should get a high percentage of these events for
 * statisical purposes, and so a few missing should not be a problem */
enum kbase_timeline_pm_event {
	/* helper for tests */
	KBASEP_TIMELINE_PM_EVENT_FIRST,

	/** Event reserved for backwards compatibility with 'init' events */
	KBASE_TIMELINE_PM_EVENT_RESERVED_0 = KBASEP_TIMELINE_PM_EVENT_FIRST,

	/** The power state of the device has changed.
	 *
	 * Specifically, the device has reached a desired or available state.
	 */
	KBASE_TIMELINE_PM_EVENT_GPU_STATE_CHANGED,

	/** The GPU is becoming active.
	 *
	 * This event is sent when the first context is about to use the GPU.
	 */
	KBASE_TIMELINE_PM_EVENT_GPU_ACTIVE,

	/** The GPU is becoming idle.
	 *
	 * This event is sent when the last context has finished using the GPU.
	 */
	KBASE_TIMELINE_PM_EVENT_GPU_IDLE,

	/** Event reserved for backwards compatibility with 'policy_change'
	 * events */
	KBASE_TIMELINE_PM_EVENT_RESERVED_4,

	/** Event reserved for backwards compatibility with 'system_suspend'
	 * events */
	KBASE_TIMELINE_PM_EVENT_RESERVED_5,

	/** Event reserved for backwards compatibility with 'system_resume'
	 * events */
	KBASE_TIMELINE_PM_EVENT_RESERVED_6,

	/** The job scheduler is requesting to power up/down cores.
	 *
	 * This event is sent when:
	 * - powered down cores are needed to complete a job
	 * - powered up cores are not needed anymore
	 */
	KBASE_TIMELINE_PM_EVENT_CHANGE_GPU_STATE,

	KBASEP_TIMELINE_PM_EVENT_LAST = KBASE_TIMELINE_PM_EVENT_CHANGE_GPU_STATE,
};

#ifdef CONFIG_MALI_BIFROST_TRACE_TIMELINE
struct kbase_trace_kctx_timeline {
	atomic_t jd_atoms_in_flight;
	u32 owner_tgid;
};

struct kbase_trace_kbdev_timeline {
	/* Note: strictly speaking, not needed, because it's in sync with
	 * kbase_device::jm_slots[]::submitted_nr
	 *
	 * But it's kept as an example of how to add global timeline tracking
	 * information
	 *
	 * The caller must hold hwaccess_lock when accessing this */
	u8 slot_atoms_submitted[BASE_JM_MAX_NR_SLOTS];

	/* Last UID for each PM event */
	atomic_t pm_event_uid[KBASEP_TIMELINE_PM_EVENT_LAST+1];
	/* Counter for generating PM event UIDs */
	atomic_t pm_event_uid_counter;
	/*
	 * L2 transition state - true indicates that the transition is ongoing
	 * Expected to be protected by hwaccess_lock */
	bool l2_transitioning;
};
#endif /* CONFIG_MALI_BIFROST_TRACE_TIMELINE */


struct kbasep_kctx_list_element {
	struct list_head link;
	struct kbase_context *kctx;
};

/**
 * Data stored per device for power management.
 *
 * This structure contains data for the power management framework. There is one
 * instance of this structure per device in the system.
 */
struct kbase_pm_device_data {
	/**
	 * The lock protecting Power Management structures accessed outside of
	 * IRQ.
	 *
	 * This lock must also be held whenever the GPU is being powered on or
	 * off.
	 */
	struct mutex lock;

	/** The reference count of active contexts on this device. */
	int active_count;
	/** Flag indicating suspending/suspended */
	bool suspending;
	/* Wait queue set when active_count == 0 */
	wait_queue_head_t zero_active_count_wait;

	/**
	 * Bit masks identifying the available shader cores that are specified
	 * via sysfs. One mask per job slot.
	 */
	u64 debug_core_mask[BASE_JM_MAX_NR_SLOTS];
	u64 debug_core_mask_all;

	/**
	 * Callback for initializing the runtime power management.
	 *
	 * @param kbdev The kbase device
	 *
	 * @return 0 on success, else error code
	 */
	 int (*callback_power_runtime_init)(struct kbase_device *kbdev);

	/**
	 * Callback for terminating the runtime power management.
	 *
	 * @param kbdev The kbase device
	 */
	void (*callback_power_runtime_term)(struct kbase_device *kbdev);

	/* Time in milliseconds between each dvfs sample */
	u32 dvfs_period;

	/* Period of GPU poweroff timer */
	ktime_t gpu_poweroff_time;

	/* Number of ticks of GPU poweroff timer before shader is powered off */
	int poweroff_shader_ticks;

	/* Number of ticks of GPU poweroff timer before GPU is powered off */
	int poweroff_gpu_ticks;

	struct kbase_pm_backend_data backend;
};

/**
 * struct kbase_mem_pool - Page based memory pool for kctx/kbdev
 * @kbdev:        Kbase device where memory is used
 * @cur_size:     Number of free pages currently in the pool (may exceed
 *                @max_size in some corner cases)
 * @max_size:     Maximum number of free pages in the pool
 * @order:        order = 0 refers to a pool of 4 KB pages
 *                order = 9 refers to a pool of 2 MB pages (2^9 * 4KB = 2 MB)
 * @pool_lock:    Lock protecting the pool - must be held when modifying
 *                @cur_size and @page_list
 * @page_list:    List of free pages in the pool
 * @reclaim:      Shrinker for kernel reclaim of free pages
 * @next_pool:    Pointer to next pool where pages can be allocated when this
 *                pool is empty. Pages will spill over to the next pool when
 *                this pool is full. Can be NULL if there is no next pool.
 * @dying:        true if the pool is being terminated, and any ongoing
 *                operations should be abandoned
 * @dont_reclaim: true if the shrinker is forbidden from reclaiming memory from
 *                this pool, eg during a grow operation
 */
struct kbase_mem_pool {
	struct kbase_device *kbdev;
	size_t              cur_size;
	size_t              max_size;
	size_t		    order;
	spinlock_t          pool_lock;
	struct list_head    page_list;
	struct shrinker     reclaim;

	struct kbase_mem_pool *next_pool;

	bool dying;
	bool dont_reclaim;
};

/**
 * struct kbase_devfreq_opp - Lookup table for converting between nominal OPP
 *                            frequency, and real frequency and core mask
 * @opp_freq:  Nominal OPP frequency
 * @real_freq: Real GPU frequency
 * @core_mask: Shader core mask
 */
struct kbase_devfreq_opp {
	u64 opp_freq;
	u64 real_freq;
	u64 core_mask;
};

struct kbase_mmu_mode {
	void (*update)(struct kbase_context *kctx);
	void (*get_as_setup)(struct kbase_context *kctx,
			struct kbase_mmu_setup * const setup);
	void (*disable_as)(struct kbase_device *kbdev, int as_nr);
	phys_addr_t (*pte_to_phy_addr)(u64 entry);
	int (*ate_is_valid)(u64 ate, unsigned int level);
	int (*pte_is_valid)(u64 pte, unsigned int level);
	void (*entry_set_ate)(u64 *entry, struct tagged_addr phy,
			unsigned long flags, unsigned int level);
	void (*entry_set_pte)(u64 *entry, phys_addr_t phy);
	void (*entry_invalidate)(u64 *entry);
};

struct kbase_mmu_mode const *kbase_mmu_mode_get_lpae(void);
struct kbase_mmu_mode const *kbase_mmu_mode_get_aarch64(void);


#define DEVNAME_SIZE	16

/**
 * struct kbase_device   - Object representing an instance of GPU platform device,
 *                         allocated from the probe method of mali driver.
 * @hw_quirks_sc:          Configuration to be used for the shader cores as per
 *                         the HW issues present in the GPU.
 * @hw_quirks_tiler:       Configuration to be used for the Tiler as per the HW
 *                         issues present in the GPU.
 * @hw_quirks_mmu:         Configuration to be used for the MMU as per the HW
 *                         issues present in the GPU.
 * @hw_quirks_jm:          Configuration to be used for the Job Manager as per
 *                         the HW issues present in the GPU.
 * @entry:                 Links the device instance to the global list of GPU
 *                         devices. The list would have as many entries as there
 *                         are GPU device instances.
 * @dev:                   Pointer to the kernel's generic/base representation
 *                         of the GPU platform device.
 * @mdev:                  Pointer to the miscellaneous device registered to
 *                         provide Userspace access to kernel driver through the
 *                         device file /dev/malixx.
 * @reg_start:             Base address of the region in physical address space
 *                         where GPU registers have been mapped.
 * @reg_size:              Size of the region containing GPU registers
 * @reg:                   Kernel virtual address of the region containing GPU
 *                         registers, using which Driver will access the registers.
 * @irqs:                  Array containing IRQ resource info for 3 types of
 *                         interrupts : Job scheduling, MMU & GPU events (like
 *                         power management, cache etc.)
 * @clock:                 Pointer to the input clock resource (having an id of 0),
 *                         referenced by the GPU device node.
 * @regulator:             Pointer to the struct corresponding to the regulator
 *                         for GPU device
 * @devname:               string containing the name used for GPU device instance,
 *                         miscellaneous device is registered using the same name.
 * @model:                 Pointer, valid only when Driver is compiled to not access
 *                         the real GPU Hw, to the dummy model which tries to mimic
 *                         to some extent the state & behavior of GPU Hw in response
 *                         to the register accesses made by the Driver.
 * @irq_slab:              slab cache for allocating the work items queued when
 *                         model mimics raising of IRQ to cause an interrupt on CPU.
 * @irq_workq:             workqueue for processing the irq work items.
 * @serving_job_irq:       function to execute work items queued when model mimics
 *                         the raising of JS irq, mimics the interrupt handler
 *                         processing JS interrupts.
 * @serving_gpu_irq:       function to execute work items queued when model mimics
 *                         the raising of GPU irq, mimics the interrupt handler
 *                         processing GPU interrupts.
 * @serving_mmu_irq:       function to execute work items queued when model mimics
 *                         the raising of MMU irq, mimics the interrupt handler
 *                         processing MMU interrupts.
 * @reg_op_lock:           lock used by model to serialize the handling of register
 *                         accesses made by the driver.
 * @pm:                    Per device object for storing data for power management
 *                         framework.
 * @js_data:               Per device object encapsulating the current context of
 *                         Job Scheduler, which is global to the device and is not
 *                         tied to any particular struct kbase_context running on
 *                         the device
 * @mem_pool:              Object containing the state for global pool of 4KB size
 *                         physical pages which can be used by all the contexts.
 * @lp_mem_pool:           Object containing the state for global pool of 2MB size
 *                         physical pages which can be used by all the contexts.
 * @memdev:                keeps track of the in use physical pages allocated by
 *                         the Driver.
 * @mmu_mode:              Pointer to the object containing methods for programming
 *                         the MMU, depending on the type of MMU supported by Hw.
 * @as:                    Array of objects representing address spaces of GPU.
 * @as_free:               Bitpattern of free/available address space lots
 * @as_to_kctx:            Array of pointers to struct kbase_context, having
 *                         GPU adrress spaces assigned to them.
 * @mmu_mask_change:       Lock to serialize the access to MMU interrupt mask
 *                         register used in the handling of Bus & Page faults.
 * @gpu_props:             Object containing complete information about the
 *                         configuration/properties of GPU HW device in use.
 * @hw_issues_mask:        List of SW workarounds for HW issues
 * @hw_features_mask:      List of available HW features.
 * shader_inuse_bitmap:    Bitmaps of shader cores that are currently in use.
 *                         These should be kept up to date by the job scheduler.
 *                         The bit to be set in this bitmap should already be set
 *                         in the @shader_needed_bitmap.
 *                         @pm.power_change_lock should be held when accessing
 *                         these members.
 * @shader_inuse_cnt:      Usage count for each of the 64 shader cores
 * @shader_needed_bitmap:  Bitmaps of cores the JS needs for jobs ready to run
 *                         kbase_pm_check_transitions_nolock() should be called
 *                         when the bitmap is modified to update the power
 *                         management system and allow transitions to occur.
 * @shader_needed_cnt:     Count for each of the 64 shader cores, incremented
 *                         when the core is requested for use and decremented
 *                         later when the core is known to be powered up for use.
 * @tiler_inuse_cnt:       Usage count for the Tiler block. @tiler_needed_cnt
 *                         should be non zero at the time of incrementing the
 *                         usage count.
 * @tiler_needed_cnt:      Count for the Tiler block shader cores, incremented
 *                         when Tiler is requested for use and decremented
 *                         later when Tiler is known to be powered up for use.
 * @disjoint_event:        struct for keeping track of the disjoint information,
 *                         that whether the GPU is in a disjoint state and the
 *                         number of disjoint events that have occurred on GPU.
 * @l2_users_count:        Refcount for tracking users of the l2 cache, e.g.
 *                         when using hardware counter instrumentation.
 * @shader_available_bitmap: Bitmap of shader cores that are currently available,
 *                         powered up and the power policy is happy for jobs
 *                         to be submitted to these cores. These are updated
 *                         by the power management code. The job scheduler
 *                         should avoid submitting new jobs to any cores
 *                         that are not marked as available.
 * @tiler_available_bitmap: Bitmap of tiler units that are currently available.
 * @l2_available_bitmap:    Bitmap of the currently available Level 2 caches.
 * @stack_available_bitmap: Bitmap of the currently available Core stacks.
 * @shader_ready_bitmap:    Bitmap of shader cores that are ready (powered on)
 * @shader_transitioning_bitmap: Bitmap of shader cores that are currently changing
 *                         power state.
 * @nr_hw_address_spaces:  Number of address spaces actually available in the
 *                         GPU, remains constant after driver initialisation.
 * @nr_user_address_spaces: Number of address spaces available to user contexts
 * @hwcnt:                  Structure used for instrumentation and HW counters
 *                         dumping
 * @vinstr_ctx:            vinstr context created per device
 * @trace_lock:            Lock to serialize the access to trace buffer.
 * @trace_first_out:       Index/offset in the trace buffer at which the first
 *                         unread message is present.
 * @trace_next_in:         Index/offset in the trace buffer at which the new
 *                         message will be written.
 * @trace_rbuf:            Pointer to the buffer storing debug messages/prints
 *                         tracing the various events in Driver.
 *                         The buffer is filled in circular fashion.
 * @reset_timeout_ms:      Number of milliseconds to wait for the soft stop to
 *                         complete for the GPU jobs before proceeding with the
 *                         GPU reset.
 * @cacheclean_lock:       Lock to serialize the clean & invalidation of GPU caches,
 *                         between Job Manager backend & Instrumentation code.
 * @platform_context:      Platform specific private data to be accessed by
 *                         platform specific config files only.
 * @kctx_list:             List of kbase_contexts created for the device, including
 *                         the kbase_context created for vinstr_ctx.
 * @kctx_list_lock:        Lock protecting concurrent accesses to @kctx_list.
 * @devfreq_profile:       Describes devfreq profile for the Mali GPU device, passed
 *                         to devfreq_add_device() to add devfreq feature to Mali
 *                         GPU device.
 * @devfreq:               Pointer to devfreq structure for Mali GPU device,
 *                         returned on the call to devfreq_add_device().
 * @current_freq:          The real frequency, corresponding to @current_nominal_freq,
 *                         at which the Mali GPU device is currently operating, as
 *                         retrieved from @opp_table in the target callback of
 *                         @devfreq_profile.
 * @current_nominal_freq:  The nominal frequency currently used for the Mali GPU
 *                         device as retrieved through devfreq_recommended_opp()
 *                         using the freq value passed as an argument to target
 *                         callback of @devfreq_profile
 * @current_voltage:       The voltage corresponding to @current_nominal_freq, as
 *                         retrieved through dev_pm_opp_get_voltage().
 * @current_core_mask:     bitmask of shader cores that are currently desired &
 *                         enabled, corresponding to @current_nominal_freq as
 *                         retrieved from @opp_table in the target callback of
 *                         @devfreq_profile.
 * @opp_table:             Pointer to the lookup table for converting between nominal
 *                         OPP (operating performance point) frequency, and real
 *                         frequency and core mask. This table is constructed according
 *                         to operating-points-v2-mali table in devicetree.
 * @num_opps:              Number of operating performance points available for the Mali
 *                         GPU device.
 * @devfreq_cooling:       Pointer returned on registering devfreq cooling device
 *                         corresponding to @devfreq.
 * @ipa_use_configured_model: set to TRUE when configured model is used for IPA and
 *                         FALSE when fallback model is used.
 * @ipa:                   Top level structure for IPA, containing pointers to both
 *                         configured & fallback models.
 * @timeline:              Stores the global timeline tracking information.
 * @job_fault_debug:       Flag to control the dumping of debug data for job faults,
 *                         set when the 'job_fault' debugfs file is opened.
 * @mali_debugfs_directory: Root directory for the debugfs files created by the driver
 * @debugfs_ctx_directory: Directory inside the @mali_debugfs_directory containing
 *                         a sub-directory for every context.
 * @debugfs_as_read_bitmap: bitmap of address spaces for which the bus or page fault
 *                         has occurred.
 * @job_fault_wq:          Waitqueue to block the job fault dumping daemon till the
 *                         occurrence of a job fault.
 * @job_fault_resume_wq:   Waitqueue on which every context with a faulty job wait
 *                         for the job fault dumping to complete before they can
 *                         do bottom half of job done for the atoms which followed
 *                         the faulty atom.
 * @job_fault_resume_workq: workqueue to process the work items queued for the faulty
 *                         atoms, whereby the work item function waits for the dumping
 *                         to get completed.
 * @job_fault_event_list:  List of atoms, each belonging to a different context, which
 *                         generated a job fault.
 * @job_fault_event_lock:  Lock to protect concurrent accesses to @job_fault_event_list
 * @regs_dump_debugfs_data: Contains the offset of register to be read through debugfs
 *                         file "read_register".
 * @kbase_profiling_controls: Profiling controls set by gator to control frame buffer
 *                         dumping and s/w counter reporting.
 * @force_replay_limit:    Number of gpu jobs, having replay atoms associated with them,
 *                         that are run before a job is forced to fail and replay.
 *                         Set to 0 to disable forced failures.
 * @force_replay_count:    Count of gpu jobs, having replay atoms associated with them,
 *                         between forced failures. Incremented on each gpu job which
 *                         has replay atoms dependent on it. A gpu job is forced to
 *                         fail once this is greater than or equal to @force_replay_limit
 * @force_replay_core_req: Core requirements, set through the sysfs file, for the replay
 *                         job atoms to consider the associated gpu job for forceful
 *                         failure and replay. May be zero
 * @force_replay_random:   Set to 1 to randomize the @force_replay_limit, in the
 *                         range of 1 - KBASEP_FORCE_REPLAY_RANDOM_LIMIT.
 * @ctx_num:               Total number of contexts created for the device.
 * @io_history:            Pointer to an object keeping a track of all recent
 *                         register accesses. The history of register accesses
 *                         can be read through "regs_history" debugfs file.
 * @hwaccess:              Contains a pointer to active kbase context and GPU
 *                         backend specific data for HW access layer.
 * @faults_pending:        Count of page/bus faults waiting for bottom half processing
 *                         via workqueues.
 * @poweroff_pending:      Set when power off operation for GPU is started, reset when
 *                         power on for GPU is started.
 * @infinite_cache_active_default: Set to enable using infinite cache for all the
 *                         allocations of a new context.
 * @mem_pool_max_size_default: Initial/default value for the maximum size of both
 *                         types of pool created for a new context.
 * @current_gpu_coherency_mode: coherency mode in use, which can be different
 *                         from @system_coherency, when using protected mode.
 * @system_coherency:      coherency mode as retrieved from the device tree.
 * @cci_snoop_enabled:     Flag to track when CCI snoops have been enabled.
 * @snoop_enable_smc:      SMC function ID to call into Trusted firmware to
 *                         enable cache snooping. Value of 0 indicates that it
 *                         is not used.
 * @snoop_disable_smc:     SMC function ID to call disable cache snooping.
 * @protected_ops:         Pointer to the methods for switching in or out of the
 *                         protected mode, as per the @protected_dev being used.
 * @protected_dev:         Pointer to the protected mode switcher device attached
 *                         to the GPU device retrieved through device tree if
 *                         GPU do not support protected mode switching natively.
 * @protected_mode:        set to TRUE when GPU is put into protected mode
 * @protected_mode_transition: set to TRUE when GPU is transitioning into or
 *                         out of protected mode.
 * @protected_mode_support: set to true if protected mode is supported.
 * @buslogger:              Pointer to the structure required for interfacing
 *                          with the bus logger module to set the size of buffer
 *                          used by the module for capturing bus logs.
 * @irq_reset_flush:        Flag to indicate that GPU reset is in-flight and flush of
 *                          IRQ + bottom half is being done, to prevent the writes
 *                          to MMU_IRQ_CLEAR & MMU_IRQ_MASK registers.
 * @inited_subsys:          Bitmap of inited sub systems at the time of device probe.
 *                          Used during device remove or for handling error in probe.
 * @hwaccess_lock:          Lock, which can be taken from IRQ context, to serialize
 *                          the updates made to Job dispatcher + scheduler states.
 * @mmu_hw_mutex:           Protects access to MMU operations and address space
 *                          related state.
 * @serialize_jobs:         Currently used mode for serialization of jobs, both
 *                          intra & inter slots serialization is supported.
 * @backup_serialize_jobs:  Copy of the original value of @serialize_jobs taken
 *                          when GWT is enabled. Used to restore the original value
 *                          on disabling of GWT.
 * @js_ctx_scheduling_mode: Context scheduling mode currently being used by
 *                          Job Scheduler
 */
struct kbase_device {
	u32 hw_quirks_sc;
	u32 hw_quirks_tiler;
	u32 hw_quirks_mmu;
	u32 hw_quirks_jm;

	struct list_head entry;
	struct device *dev;
	struct miscdevice mdev;
	u64 reg_start;
	size_t reg_size;
	void __iomem *reg;

	struct {
		int irq;
		int flags;
	} irqs[3];

	struct clk *clock;
#ifdef CONFIG_REGULATOR
	struct regulator *regulator;
#endif
	char devname[DEVNAME_SIZE];

#ifdef CONFIG_MALI_BIFROST_NO_MALI
	void *model;
	struct kmem_cache *irq_slab;
	struct workqueue_struct *irq_workq;
	atomic_t serving_job_irq;
	atomic_t serving_gpu_irq;
	atomic_t serving_mmu_irq;
	spinlock_t reg_op_lock;
#endif	/* CONFIG_MALI_BIFROST_NO_MALI */

	struct kbase_pm_device_data pm;
	struct kbasep_js_device_data js_data;
	struct kbase_mem_pool mem_pool;
	struct kbase_mem_pool lp_mem_pool;
	struct kbasep_mem_device memdev;
	struct kbase_mmu_mode const *mmu_mode;

	struct kbase_as as[BASE_MAX_NR_AS];
	u16 as_free; /* Bitpattern of free Address Spaces */
	struct kbase_context *as_to_kctx[BASE_MAX_NR_AS];


	spinlock_t mmu_mask_change;

	struct kbase_gpu_props gpu_props;

	unsigned long hw_issues_mask[(BASE_HW_ISSUE_END + BITS_PER_LONG - 1) / BITS_PER_LONG];
	unsigned long hw_features_mask[(BASE_HW_FEATURE_END + BITS_PER_LONG - 1) / BITS_PER_LONG];

	u64 shader_inuse_bitmap;

	u32 shader_inuse_cnt[64];

	u64 shader_needed_bitmap;

	u32 shader_needed_cnt[64];

	u32 tiler_inuse_cnt;

	u32 tiler_needed_cnt;

	struct {
		atomic_t count;
		atomic_t state;
	} disjoint_event;

	u32 l2_users_count;

	u64 shader_available_bitmap;
	u64 tiler_available_bitmap;
	u64 l2_available_bitmap;
	u64 stack_available_bitmap;

	u64 shader_ready_bitmap;
	u64 shader_transitioning_bitmap;

	s8 nr_hw_address_spaces;
	s8 nr_user_address_spaces;

	struct kbase_hwcnt {
		/* The lock should be used when accessing any of the following members */
		spinlock_t lock;

		struct kbase_context *kctx;
		u64 addr;

		struct kbase_instr_backend backend;
	} hwcnt;

	struct kbase_vinstr_context *vinstr_ctx;

#if KBASE_TRACE_ENABLE
	spinlock_t              trace_lock;
	u16                     trace_first_out;
	u16                     trace_next_in;
	struct kbase_trace            *trace_rbuf;
#endif

	u32 reset_timeout_ms;

	struct mutex cacheclean_lock;

	void *platform_context;

	struct list_head        kctx_list;
	struct mutex            kctx_list_lock;

#ifdef CONFIG_MALI_BIFROST_DEVFREQ
	struct devfreq_dev_profile devfreq_profile;
	struct devfreq *devfreq;
	unsigned long current_freq;
	unsigned long current_nominal_freq;
	unsigned long current_voltage;
	u64 current_core_mask;
	struct kbase_devfreq_opp *opp_table;
	int num_opps;
	struct kbasep_pm_metrics last_devfreq_metrics;
	struct monitor_dev_info *mdev_info;
#ifdef CONFIG_DEVFREQ_THERMAL
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
	struct devfreq_cooling_device *devfreq_cooling;
#else
	struct thermal_cooling_device *devfreq_cooling;
#endif
	atomic_t ipa_use_configured_model;
	struct {
		/* Access to this struct must be with ipa.lock held */
		struct mutex lock;
		struct kbase_ipa_model *configured_model;
		struct kbase_ipa_model *fallback_model;

		/* Values of the PM utilization metrics from last time the
		 * power model was invoked. The utilization is calculated as
		 * the difference between last_metrics and the current values.
		 */
		struct kbasep_pm_metrics last_metrics;

		/*
		 * gpu_active_callback - Inform IPA that GPU is now active
		 * @model_data: Pointer to model data
		 */
		void (*gpu_active_callback)(
				struct kbase_ipa_model_vinstr_data *model_data);

		/*
		 * gpu_idle_callback - Inform IPA that GPU is now idle
		 * @model_data: Pointer to model data
		 */
		void (*gpu_idle_callback)(
				struct kbase_ipa_model_vinstr_data *model_data);

		/* Model data to pass to ipa_gpu_active/idle() */
		struct kbase_ipa_model_vinstr_data *model_data;

		/* true if IPA is currently using vinstr */
		bool vinstr_active;
	} ipa;
#endif /* CONFIG_DEVFREQ_THERMAL */
#endif /* CONFIG_MALI_BIFROST_DEVFREQ */


#ifdef CONFIG_MALI_BIFROST_TRACE_TIMELINE
	struct kbase_trace_kbdev_timeline timeline;
#endif

	bool job_fault_debug;

#ifdef CONFIG_DEBUG_FS
	struct dentry *mali_debugfs_directory;
	struct dentry *debugfs_ctx_directory;

#ifdef CONFIG_MALI_BIFROST_DEBUG
	u64 debugfs_as_read_bitmap;
#endif /* CONFIG_MALI_BIFROST_DEBUG */

	wait_queue_head_t job_fault_wq;
	wait_queue_head_t job_fault_resume_wq;
	struct workqueue_struct *job_fault_resume_workq;
	struct list_head job_fault_event_list;
	spinlock_t job_fault_event_lock;

#if !MALI_CUSTOMER_RELEASE
	struct {
		u16 reg_offset;
	} regs_dump_debugfs_data;
#endif /* !MALI_CUSTOMER_RELEASE */
#endif /* CONFIG_DEBUG_FS */

	u32 kbase_profiling_controls[FBDUMP_CONTROL_MAX];


#if MALI_CUSTOMER_RELEASE == 0
	int force_replay_limit;
	int force_replay_count;
	base_jd_core_req force_replay_core_req;
	bool force_replay_random;
#endif

	atomic_t ctx_num;

#ifdef CONFIG_DEBUG_FS
	struct kbase_io_history io_history;
#endif /* CONFIG_DEBUG_FS */

	struct kbase_hwaccess_data hwaccess;

	atomic_t faults_pending;

	bool poweroff_pending;


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	bool infinite_cache_active_default;
#else
	u32 infinite_cache_active_default;
#endif
	size_t mem_pool_max_size_default;

	u32 current_gpu_coherency_mode;
	u32 system_coherency;

	bool cci_snoop_enabled;

	u32 snoop_enable_smc;
	u32 snoop_disable_smc;

	struct protected_mode_ops *protected_ops;

	struct protected_mode_device *protected_dev;

	bool protected_mode;

	bool protected_mode_transition;

	bool protected_mode_support;

#ifdef CONFIG_MALI_FPGA_BUS_LOGGER
	struct bus_logger_client *buslogger;
#endif

	bool irq_reset_flush;

	u32 inited_subsys;

	spinlock_t hwaccess_lock;

	struct mutex mmu_hw_mutex;

	/* See KBASE_SERIALIZE_* for details */
	u8 serialize_jobs;

#ifdef CONFIG_MALI_JOB_DUMP
	u8 backup_serialize_jobs;
#endif

	/* See KBASE_JS_*_PRIORITY_MODE for details. */
	u32 js_ctx_scheduling_mode;
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


#define KBASE_API_VERSION(major, minor) ((((major) & 0xFFF) << 20)  | \
					 (((minor) & 0xFFF) << 8) | \
					 ((0 & 0xFF) << 0))

/**
 * enum kbase_context_flags - Flags for kbase contexts
 *
 * @KCTX_COMPAT: Set when the context process is a compat process, 32-bit
 * process on a 64-bit kernel.
 *
 * @KCTX_RUNNABLE_REF: Set when context is counted in
 * kbdev->js_data.nr_contexts_runnable. Must hold queue_mutex when accessing.
 *
 * @KCTX_ACTIVE: Set when the context is active.
 *
 * @KCTX_PULLED: Set when last kick() caused atoms to be pulled from this
 * context.
 *
 * @KCTX_MEM_PROFILE_INITIALIZED: Set when the context's memory profile has been
 * initialized.
 *
 * @KCTX_INFINITE_CACHE: Set when infinite cache is to be enabled for new
 * allocations. Existing allocations will not change.
 *
 * @KCTX_SUBMIT_DISABLED: Set to prevent context from submitting any jobs.
 *
 * @KCTX_PRIVILEGED:Set if the context uses an address space and should be kept
 * scheduled in.
 *
 * @KCTX_SCHEDULED: Set when the context is scheduled on the Run Pool.
 * This is only ever updated whilst the jsctx_mutex is held.
 *
 * @KCTX_DYING: Set when the context process is in the process of being evicted.
 *
 * @KCTX_NO_IMPLICIT_SYNC: Set when explicit Android fences are in use on this
 * context, to disable use of implicit dma-buf fences. This is used to avoid
 * potential synchronization deadlocks.
 *
 * @KCTX_FORCE_SAME_VA: Set when BASE_MEM_SAME_VA should be forced on memory
 * allocations. For 64-bit clients it is enabled by default, and disabled by
 * default on 32-bit clients. Being able to clear this flag is only used for
 * testing purposes of the custom zone allocation on 64-bit user-space builds,
 * where we also require more control than is available through e.g. the JIT
 * allocation mechanism. However, the 64-bit user-space client must still
 * reserve a JIT region using KBASE_IOCTL_MEM_JIT_INIT
 *
 * @KCTX_PULLED_SINCE_ACTIVE_JS0: Set when the context has had an atom pulled
 * from it for job slot 0. This is reset when the context first goes active or
 * is re-activated on that slot.
 *
 * @KCTX_PULLED_SINCE_ACTIVE_JS1: Set when the context has had an atom pulled
 * from it for job slot 1. This is reset when the context first goes active or
 * is re-activated on that slot.
 *
 * @KCTX_PULLED_SINCE_ACTIVE_JS2: Set when the context has had an atom pulled
 * from it for job slot 2. This is reset when the context first goes active or
 * is re-activated on that slot.
 *
 * All members need to be separate bits. This enum is intended for use in a
 * bitmask where multiple values get OR-ed together.
 */
enum kbase_context_flags {
	KCTX_COMPAT = 1U << 0,
	KCTX_RUNNABLE_REF = 1U << 1,
	KCTX_ACTIVE = 1U << 2,
	KCTX_PULLED = 1U << 3,
	KCTX_MEM_PROFILE_INITIALIZED = 1U << 4,
	KCTX_INFINITE_CACHE = 1U << 5,
	KCTX_SUBMIT_DISABLED = 1U << 6,
	KCTX_PRIVILEGED = 1U << 7,
	KCTX_SCHEDULED = 1U << 8,
	KCTX_DYING = 1U << 9,
	KCTX_NO_IMPLICIT_SYNC = 1U << 10,
	KCTX_FORCE_SAME_VA = 1U << 11,
	KCTX_PULLED_SINCE_ACTIVE_JS0 = 1U << 12,
	KCTX_PULLED_SINCE_ACTIVE_JS1 = 1U << 13,
	KCTX_PULLED_SINCE_ACTIVE_JS2 = 1U << 14,
};

struct kbase_sub_alloc {
	struct list_head link;
	struct page *page;
	DECLARE_BITMAP(sub_pages, SZ_2M / SZ_4K);
};

/**
 * struct kbase_context - Object representing an entity, among which GPU is
 *                        scheduled and gets its own GPU address space.
 *                        Created when the device file /dev/malixx is opened.
 * @filp:                 Pointer to the struct file corresponding to device file
 *                        /dev/malixx instance, passed to the file's open method.
 * @kbdev:                Pointer to the Kbase device for which the context is created.
 * @id:                   Unique indentifier for the context, indicates the number of
 *                        contexts which have been created for the device so far.
 * @api_version:          contains the version number for User/kernel interface,
 *                        used for compatibility check.
 * @pgd:                  Physical address of the page allocated for the top level
 *                        page table of the context, this will be used for MMU Hw
 *                        programming as the address translation will start from
 *                        the top level page table.
 * @event_list:           list of posted events about completed atoms, to be sent to
 *                        event handling thread of Userpsace.
 * @event_coalesce_list:  list containing events corresponding to successive atoms
 *                        which have requested deferred delivery of the completion
 *                        events to Userspace.
 * @event_mutex:          Lock to protect the concurrent access to @event_list &
 *                        @event_mutex.
 * @event_closed:         Flag set through POST_TERM ioctl, indicates that Driver
 *                        should stop posting events and also inform event handling
 *                        thread that context termination is in progress.
 * @event_workq:          Workqueue for processing work items corresponding to atoms
 *                        that do not return an event to Userspace or have to perform
 *                        a replay job
 * @event_count:          Count of the posted events to be consumed by Userspace.
 * @event_coalesce_count: Count of the events present in @event_coalesce_list.
 * @flags:                bitmap of enums from kbase_context_flags, indicating the
 *                        state & attributes for the context.
 * @setup_complete:       Indicates if the setup for context has completed, i.e.
 *                        flags have been set for the context. Driver allows only
 *                        2 ioctls until the setup is done. Valid only for
 *                        @api_version value 0.
 * @setup_in_progress:    Indicates if the context's setup is in progress and other
 *                        setup calls during that shall be rejected.
 * @mmu_teardown_pages:   Buffer of 4 Pages in size, used to cache the entries of
 *                        top & intermediate level page tables to avoid repeated
 *                        calls to kmap_atomic during the MMU teardown.
 * @aliasing_sink_page:   Special page used for KBASE_MEM_TYPE_ALIAS allocations,
 *                        which can alias number of memory regions. The page is
 *                        represent a region where it is mapped with a write-alloc
 *                        cache setup, typically used when the write result of the
 *                        GPU isn't needed, but the GPU must write anyway.
 * @mem_partials_lock:    Lock for protecting the operations done on the elements
 *                        added to @mem_partials list.
 * @mem_partials:         List head for the list of large pages, 2MB in size, which
 *                        which have been split into 4 KB pages and are used
 *                        partially for the allocations >= 2 MB in size.
 * @mmu_lock:             Lock to serialize the accesses made to multi level GPU
 *                        page tables, maintained for every context.
 * @reg_lock:             Lock used for GPU virtual address space management operations,
 *                        like adding/freeing a memory region in the address space.
 *                        Can be converted to a rwlock ?.
 * @reg_rbtree_same:      RB tree of the memory regions allocated from the SAME_VA
 *                        zone of the GPU virtual address space. Used for allocations
 *                        having the same value for GPU & CPU virtual address.
 * @reg_rbtree_exec:      RB tree of the memory regions allocated from the EXEC
 *                        zone of the GPU virtual address space. Used for
 *                        allocations containing executable code for
 *                        shader programs.
 * @reg_rbtree_custom:    RB tree of the memory regions allocated from the CUSTOM_VA
 *                        zone of the GPU virtual address space.
 * @cookies:              Bitmask containing of BITS_PER_LONG bits, used mainly for
 *                        SAME_VA allocations to defer the reservation of memory region
 *                        (from the GPU virtual address space) from base_mem_alloc
 *                        ioctl to mmap system call. This helps returning unique
 *                        handles, disguised as GPU VA, to Userspace from base_mem_alloc
 *                        and later retrieving the pointer to memory region structure
 *                        in the mmap handler.
 * @pending_regions:      Array containing pointers to memory region structures,
 *                        used in conjunction with @cookies bitmask mainly for
 *                        providing a mechansim to have the same value for CPU &
 *                        GPU virtual address.
 * @event_queue:          Wait queue used for blocking the thread, which consumes
 *                        the base_jd_event corresponding to an atom, when there
 *                        are no more posted events.
 * @tgid:                 thread group id of the process, whose thread opened the
 *                        device file /dev/malixx instance to create a context.
 * @pid:                  id of the thread, corresponding to process @tgid, which
 *                        actually which opened the device file.
 * @jctx:                 object encapsulating all the Job dispatcher related state,
 *                        including the array of atoms.
 * @used_pages:           Keeps a track of the number of 4KB physical pages in use
 *                        for the context.
 * @nonmapped_pages:      Updated in the same way as @used_pages, except for the case
 *                        when special tracking page is freed by userspace where it
 *                        is reset to 0.
 * @mem_pool:             Object containing the state for the context specific pool of
 *                        4KB size physical pages.
 * @lp_mem_pool:          Object containing the state for the context specific pool of
 *                        2MB size physical pages.
 * @reclaim:              Shrinker object registered with the kernel containing
 *                        the pointer to callback function which is invoked under
 *                        low memory conditions. In the callback function Driver
 *                        frees up the memory for allocations marked as
 *                        evictable/reclaimable.
 * @evict_list:           List head for the list containing the allocations which
 *                        can be evicted or freed up in the shrinker callback.
 * @waiting_soft_jobs:    List head for the list containing softjob atoms, which
 *                        are either waiting for the event set operation, or waiting
 *                        for the signaling of input fence or waiting for the GPU
 *                        device to powered on so as to dump the CPU/GPU timestamps.
 * @waiting_soft_jobs_lock: Lock to protect @waiting_soft_jobs list from concurrent
 *                        accesses.
 * @dma_fence:            Object containing list head for the list of dma-buf fence
 *                        waiting atoms and the waitqueue to process the work item
 *                        queued for the atoms blocked on the signaling of dma-buf
 *                        fences.
 * @as_nr:                id of the address space being used for the scheduled in
 *                        context. This is effectively part of the Run Pool, because
 *                        it only has a valid setting (!=KBASEP_AS_NR_INVALID) whilst
 *                        the context is scheduled in. The hwaccess_lock must be held
 *                        whilst accessing this.
 *                        If the context relating to this value of as_nr is required,
 *                        then the context must be retained to ensure that it doesn't
 *                        disappear whilst it is being used. Alternatively, hwaccess_lock
 *                        can be held to ensure the context doesn't disappear (but this
 *                        has restrictions on what other locks can be taken simutaneously).
 * @refcount:             Keeps track of the number of users of this context. A user
 *                        can be a job that is available for execution, instrumentation
 *                        needing to 'pin' a context for counter collection, etc.
 *                        If the refcount reaches 0 then this context is considered
 *                        inactive and the previously programmed AS might be cleared
 *                        at any point.
 *                        Generally the reference count is incremented when the context
 *                        is scheduled in and an atom is pulled from the context's per
 *                        slot runnable tree.
 * @mm_update_lock:       lock used for handling of special tracking page.
 * @process_mm:           Pointer to the memory descriptor of the process which
 *                        created the context. Used for accounting the physical
 *                        pages used for GPU allocations, done for the context,
 *                        to the memory consumed by the process.
 * @same_va_end:          End address of the SAME_VA zone (in 4KB page units)
 * @timeline:             Object tracking the number of atoms currently in flight for
 *                        the context and thread group id of the process, i.e. @tgid.
 * @mem_profile_data:     Buffer containing the profiling information provided by
 *                        Userspace, can be read through the mem_profile debugfs file.
 * @mem_profile_size:     Size of the @mem_profile_data.
 * @mem_profile_lock:     Lock to serialize the operations related to mem_profile
 *                        debugfs file.
 * @kctx_dentry:          Pointer to the debugfs directory created for every context,
 *                        inside kbase_device::debugfs_ctx_directory, containing
 *                        context specific files.
 * @reg_dump:             Buffer containing a register offset & value pair, used
 *                        for dumping job fault debug info.
 * @job_fault_count:      Indicates that a job fault occurred for the context and
 *                        dumping of its debug info is in progress.
 * @job_fault_resume_event_list: List containing atoms completed after the faulty
 *                        atom but before the debug data for faulty atom was dumped.
 * @jsctx_queue:          Per slot & priority arrays of object containing the root
 *                        of RB-tree holding currently runnable atoms on the job slot
 *                        and the head item of the linked list of atoms blocked on
 *                        cross-slot dependencies.
 * @atoms_pulled:         Total number of atoms currently pulled from the context.
 * @atoms_pulled_slot:    Per slot count of the number of atoms currently pulled
 *                        from the context.
 * @atoms_pulled_slot_pri: Per slot & priority count of the number of atoms currently
 *                        pulled from the context. hwaccess_lock shall be held when
 *                        accessing it.
 * @blocked_js:           Indicates if the context is blocked from submitting atoms
 *                        on a slot at a given priority. This is set to true, when
 *                        the atom corresponding to context is soft/hard stopped or
 *                        removed from the HEAD_NEXT register in response to
 *                        soft/hard stop.
 * @slots_pullable:       Bitmask of slots, indicating the slots for which the
 *                        context has pullable atoms in the runnable tree.
 * @work:                 Work structure used for deferred ASID assignment.
 * @vinstr_cli:           Pointer to the legacy userspace vinstr client, there can
 *                        be only such client per kbase context.
 * @vinstr_cli_lock:      Lock used for the vinstr ioctl calls made for @vinstr_cli.
 * @completed_jobs:       List containing completed atoms for which base_jd_event is
 *                        to be posted.
 * @work_count:           Number of work items, corresponding to atoms, currently
 *                        pending on job_done workqueue of @jctx.
 * @soft_job_timeout:     Timer object used for failing/cancelling the waiting
 *                        soft-jobs which have been blocked for more than the
 *                        timeout value used for the soft-jobs
 * @jit_alloc:            Array of 256 pointers to GPU memory regions, used for
 *                        for JIT allocations.
 * @jit_max_allocations:  Maximum number of JIT allocations allowed at once.
 * @jit_current_allocations: Current number of in-flight JIT allocations.
 * @jit_current_allocations_per_bin: Current number of in-flight JIT allocations per bin
 * @jit_version:          version number indicating whether userspace is using
 *                        old or new version of interface for JIT allocations
 *	                  1 -> client used KBASE_IOCTL_MEM_JIT_INIT_OLD
 *	                  2 -> client used KBASE_IOCTL_MEM_JIT_INIT
 * @jit_active_head:      List containing the JIT allocations which are in use.
 * @jit_pool_head:        List containing the JIT allocations which have been
 *                        freed up by userpsace and so not being used by them.
 *                        Driver caches them to quickly fulfill requests for new
 *                        JIT allocations. They are released in case of memory
 *                        pressure as they are put on the @evict_list when they
 *                        are freed up by userspace.
 * @jit_destroy_head:     List containing the JIT allocations which were moved to it
 *                        from @jit_pool_head, in the shrinker callback, after freeing
 *                        their backing physical pages.
 * @jit_evict_lock:       Lock used for operations done on JIT allocations and also
 *                        for accessing @evict_list.
 * @jit_work:             Work item queued to defer the freeing of memory region when
 *                        JIT allocation is moved to @jit_destroy_head.
 * @jit_atoms_head:       A list of the JIT soft-jobs, both alloc & free, in submission
 *                        order, protected by kbase_jd_context.lock.
 * @jit_pending_alloc:    A list of JIT alloc soft-jobs for which allocation will be
 *                        reattempted after the impending free of other active JIT
 *                        allocations.
 * @ext_res_meta_head:    A list of sticky external resources which were requested to
 *                        be mapped on GPU side, through a softjob atom of type
 *                        EXT_RES_MAP or STICKY_RESOURCE_MAP ioctl.
 * @drain_pending:        Used to record that a flush/invalidate of the GPU caches was
 *                        requested from atomic context, so that the next flush request
 *                        can wait for the flush of GPU writes.
 * @age_count:            Counter incremented on every call to jd_submit_atom,
 *                        atom is assigned the snapshot of this counter, which
 *                        is used to determine the atom's age when it is added to
 *                        the runnable RB-tree.
 * @trim_level:           Level of JIT allocation trimming to perform on free (0-100%)
 * @gwt_enabled:          Indicates if tracking of GPU writes is enabled, protected by
 *                        kbase_context.reg_lock.
 * @gwt_was_enabled:      Simple sticky bit flag to know if GWT was ever enabled.
 * @gwt_current_list:     A list of addresses for which GPU has generated write faults,
 *                        after the last snapshot of it was sent to userspace.
 * @gwt_snapshot_list:    Snapshot of the @gwt_current_list for sending to user space.
 * @priority:             Indicates the context priority. Used along with @atoms_count
 *                        for context scheduling, protected by hwaccess_lock.
 * @atoms_count:          Number of gpu atoms currently in use, per priority
 */
struct kbase_context {
	struct file *filp;
	struct kbase_device *kbdev;
	u32 id;
	unsigned long api_version;
	phys_addr_t pgd;
	struct list_head event_list;
	struct list_head event_coalesce_list;
	struct mutex event_mutex;
	atomic_t event_closed;
	struct workqueue_struct *event_workq;
	atomic_t event_count;
	int event_coalesce_count;

	atomic_t flags;

	atomic_t                setup_complete;
	atomic_t                setup_in_progress;

	u64 *mmu_teardown_pages;

	struct tagged_addr aliasing_sink_page;

	struct mutex            mem_partials_lock;
	struct list_head        mem_partials;

	struct mutex            mmu_lock;
	struct mutex            reg_lock;
	struct rb_root reg_rbtree_same;
	struct rb_root reg_rbtree_exec;
	struct rb_root reg_rbtree_custom;

	unsigned long    cookies;
	struct kbase_va_region *pending_regions[BITS_PER_LONG];

	wait_queue_head_t event_queue;
	pid_t tgid;
	pid_t pid;

	struct kbase_jd_context jctx;
	atomic_t used_pages;
	atomic_t         nonmapped_pages;

	struct kbase_mem_pool mem_pool;
	struct kbase_mem_pool lp_mem_pool;

	struct shrinker         reclaim;
	struct list_head        evict_list;

	struct list_head waiting_soft_jobs;
	spinlock_t waiting_soft_jobs_lock;
#ifdef CONFIG_MALI_BIFROST_DMA_FENCE
	struct {
		struct list_head waiting_resource;
		struct workqueue_struct *wq;
	} dma_fence;
#endif /* CONFIG_MALI_BIFROST_DMA_FENCE */

	int as_nr;

	atomic_t refcount;

	/* NOTE:
	 *
	 * Flags are in jctx.sched_info.ctx.flags
	 * Mutable flags *must* be accessed under jctx.sched_info.ctx.jsctx_mutex
	 *
	 * All other flags must be added there */
	spinlock_t         mm_update_lock;
	struct mm_struct *process_mm;
	u64 same_va_end;

#ifdef CONFIG_MALI_BIFROST_TRACE_TIMELINE
	struct kbase_trace_kctx_timeline timeline;
#endif
#ifdef CONFIG_DEBUG_FS
	char *mem_profile_data;
	size_t mem_profile_size;
	struct mutex mem_profile_lock;
	struct dentry *kctx_dentry;

	unsigned int *reg_dump;
	atomic_t job_fault_count;
	struct list_head job_fault_resume_event_list;

#endif /* CONFIG_DEBUG_FS */

	struct jsctx_queue jsctx_queue
		[KBASE_JS_ATOM_SCHED_PRIO_COUNT][BASE_JM_MAX_NR_SLOTS];

	atomic_t atoms_pulled;
	atomic_t atoms_pulled_slot[BASE_JM_MAX_NR_SLOTS];
	int atoms_pulled_slot_pri[BASE_JM_MAX_NR_SLOTS][
			KBASE_JS_ATOM_SCHED_PRIO_COUNT];

	bool blocked_js[BASE_JM_MAX_NR_SLOTS][KBASE_JS_ATOM_SCHED_PRIO_COUNT];

	u32 slots_pullable;

	struct work_struct work;

	struct kbase_vinstr_client *vinstr_cli;
	struct mutex vinstr_cli_lock;

	struct list_head completed_jobs;
	atomic_t work_count;

	struct timer_list soft_job_timeout;

	struct kbase_va_region *jit_alloc[256];
	u8 jit_max_allocations;
	u8 jit_current_allocations;
	u8 jit_current_allocations_per_bin[256];
	u8 jit_version;
	struct list_head jit_active_head;
	struct list_head jit_pool_head;
	struct list_head jit_destroy_head;
	struct mutex jit_evict_lock;
	struct work_struct jit_work;

	struct list_head jit_atoms_head;
	struct list_head jit_pending_alloc;

	struct list_head ext_res_meta_head;

	atomic_t drain_pending;

	u32 age_count;

	u8 trim_level;

#ifdef CONFIG_MALI_JOB_DUMP
	bool gwt_enabled;

	bool gwt_was_enabled;

	struct list_head gwt_current_list;

	struct list_head gwt_snapshot_list;
#endif

	int priority;
	s16 atoms_count[KBASE_JS_ATOM_SCHED_PRIO_COUNT];
};

#ifdef CONFIG_MALI_JOB_DUMP
/**
 * struct kbasep_gwt_list_element - Structure used to collect GPU
 *                                  write faults.
 * @link:                           List head for adding write faults.
 * @region:                         Details of the region where we have the
 *                                  faulting page address.
 * @page_addr:                      Page address where GPU write fault occurred.
 * @num_pages:                      The number of pages modified.
 *
 * Using this structure all GPU write faults are stored in a list.
 */
struct kbasep_gwt_list_element {
	struct list_head link;
	struct kbase_va_region *region;
	u64 page_addr;
	u64 num_pages;
};

#endif

/**
 * struct kbase_ctx_ext_res_meta - Structure which binds an external resource
 *                                 to a @kbase_context.
 * @ext_res_node:                  List head for adding the metadata to a
 *                                 @kbase_context.
 * @alloc:                         The physical memory allocation structure
 *                                 which is mapped.
 * @gpu_addr:                      The GPU virtual address the resource is
 *                                 mapped to.
 *
 * External resources can be mapped into multiple contexts as well as the same
 * context multiple times.
 * As kbase_va_region itself isn't refcounted we can't attach our extra
 * information to it as it could be removed under our feet leaving external
 * resources pinned.
 * This metadata structure binds a single external resource to a single
 * context, ensuring that per context mapping is tracked separately so it can
 * be overridden when needed and abuses by the application (freeing the resource
 * multiple times) don't effect the refcount of the physical allocation.
 */
struct kbase_ctx_ext_res_meta {
	struct list_head ext_res_node;
	struct kbase_mem_phy_alloc *alloc;
	u64 gpu_addr;
};

enum kbase_reg_access_type {
	REG_READ,
	REG_WRITE
};

enum kbase_share_attr_bits {
	/* (1ULL << 8) bit is reserved */
	SHARE_BOTH_BITS = (2ULL << 8),	/* inner and outer shareable coherency */
	SHARE_INNER_BITS = (3ULL << 8)	/* inner shareable coherency */
};

/**
 * kbase_device_is_cpu_coherent - Returns if the device is CPU coherent.
 * @kbdev: kbase device
 *
 * Return: true if the device access are coherent, false if not.
 */
static inline bool kbase_device_is_cpu_coherent(struct kbase_device *kbdev)
{
	if ((kbdev->system_coherency == COHERENCY_ACE_LITE) ||
			(kbdev->system_coherency == COHERENCY_ACE))
		return true;

	return false;
}

/* Conversion helpers for setting up high resolution timers */
#define HR_TIMER_DELAY_MSEC(x) (ns_to_ktime(((u64)(x))*1000000U))
#define HR_TIMER_DELAY_NSEC(x) (ns_to_ktime(x))

/* Maximum number of loops polling the GPU for a cache flush before we assume it must have completed */
#define KBASE_CLEAN_CACHE_MAX_LOOPS     100000
/* Maximum number of loops polling the GPU for an AS command to complete before we assume the GPU has hung */
#define KBASE_AS_INACTIVE_MAX_LOOPS     100000

/* Maximum number of times a job can be replayed */
#define BASEP_JD_REPLAY_LIMIT 15

/* JobDescriptorHeader - taken from the architecture specifications, the layout
 * is currently identical for all GPU archs. */
struct job_descriptor_header {
	u32 exception_status;
	u32 first_incomplete_task;
	u64 fault_pointer;
	u8 job_descriptor_size : 1;
	u8 job_type : 7;
	u8 job_barrier : 1;
	u8 _reserved_01 : 1;
	u8 _reserved_1 : 1;
	u8 _reserved_02 : 1;
	u8 _reserved_03 : 1;
	u8 _reserved_2 : 1;
	u8 _reserved_04 : 1;
	u8 _reserved_05 : 1;
	u16 job_index;
	u16 job_dependency_index_1;
	u16 job_dependency_index_2;
	union {
		u64 _64;
		u32 _32;
	} next_job;
};

#endif				/* _KBASE_DEFS_H_ */
