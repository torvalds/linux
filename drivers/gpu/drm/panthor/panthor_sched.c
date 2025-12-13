// SPDX-License-Identifier: GPL-2.0 or MIT
/* Copyright 2023 Collabora ltd. */

#include <drm/drm_drv.h>
#include <drm/drm_exec.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/gpu_scheduler.h>
#include <drm/panthor_drm.h>

#include <linux/build_bug.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dma-resv.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/iosys-map.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "panthor_devfreq.h"
#include "panthor_device.h"
#include "panthor_fw.h"
#include "panthor_gem.h"
#include "panthor_gpu.h"
#include "panthor_heap.h"
#include "panthor_mmu.h"
#include "panthor_regs.h"
#include "panthor_sched.h"

/**
 * DOC: Scheduler
 *
 * Mali CSF hardware adopts a firmware-assisted scheduling model, where
 * the firmware takes care of scheduling aspects, to some extent.
 *
 * The scheduling happens at the scheduling group level, each group
 * contains 1 to N queues (N is FW/hardware dependent, and exposed
 * through the firmware interface). Each queue is assigned a command
 * stream ring buffer, which serves as a way to get jobs submitted to
 * the GPU, among other things.
 *
 * The firmware can schedule a maximum of M groups (M is FW/hardware
 * dependent, and exposed through the firmware interface). Passed
 * this maximum number of groups, the kernel must take care of
 * rotating the groups passed to the firmware so every group gets
 * a chance to have his queues scheduled for execution.
 *
 * The current implementation only supports with kernel-mode queues.
 * In other terms, userspace doesn't have access to the ring-buffer.
 * Instead, userspace passes indirect command stream buffers that are
 * called from the queue ring-buffer by the kernel using a pre-defined
 * sequence of command stream instructions to ensure the userspace driver
 * always gets consistent results (cache maintenance,
 * synchronization, ...).
 *
 * We rely on the drm_gpu_scheduler framework to deal with job
 * dependencies and submission. As any other driver dealing with a
 * FW-scheduler, we use the 1:1 entity:scheduler mode, such that each
 * entity has its own job scheduler. When a job is ready to be executed
 * (all its dependencies are met), it is pushed to the appropriate
 * queue ring-buffer, and the group is scheduled for execution if it
 * wasn't already active.
 *
 * Kernel-side group scheduling is timeslice-based. When we have less
 * groups than there are slots, the periodic tick is disabled and we
 * just let the FW schedule the active groups. When there are more
 * groups than slots, we let each group a chance to execute stuff for
 * a given amount of time, and then re-evaluate and pick new groups
 * to schedule. The group selection algorithm is based on
 * priority+round-robin.
 *
 * Even though user-mode queues is out of the scope right now, the
 * current design takes them into account by avoiding any guess on the
 * group/queue state that would be based on information we wouldn't have
 * if userspace was in charge of the ring-buffer. That's also one of the
 * reason we don't do 'cooperative' scheduling (encoding FW group slot
 * reservation as dma_fence that would be returned from the
 * drm_gpu_scheduler::prepare_job() hook, and treating group rotation as
 * a queue of waiters, ordered by job submission order). This approach
 * would work for kernel-mode queues, but would make user-mode queues a
 * lot more complicated to retrofit.
 */

#define JOB_TIMEOUT_MS				5000

#define MAX_CSG_PRIO				0xf

#define NUM_INSTRS_PER_CACHE_LINE		(64 / sizeof(u64))
#define MAX_INSTRS_PER_JOB			24

struct panthor_group;

/**
 * struct panthor_csg_slot - Command stream group slot
 *
 * This represents a FW slot for a scheduling group.
 */
struct panthor_csg_slot {
	/** @group: Scheduling group bound to this slot. */
	struct panthor_group *group;

	/** @priority: Group priority. */
	u8 priority;

	/**
	 * @idle: True if the group bound to this slot is idle.
	 *
	 * A group is idle when it has nothing waiting for execution on
	 * all its queues, or when queues are blocked waiting for something
	 * to happen (synchronization object).
	 */
	bool idle;
};

/**
 * enum panthor_csg_priority - Group priority
 */
enum panthor_csg_priority {
	/** @PANTHOR_CSG_PRIORITY_LOW: Low priority group. */
	PANTHOR_CSG_PRIORITY_LOW = 0,

	/** @PANTHOR_CSG_PRIORITY_MEDIUM: Medium priority group. */
	PANTHOR_CSG_PRIORITY_MEDIUM,

	/** @PANTHOR_CSG_PRIORITY_HIGH: High priority group. */
	PANTHOR_CSG_PRIORITY_HIGH,

	/**
	 * @PANTHOR_CSG_PRIORITY_RT: Real-time priority group.
	 *
	 * Real-time priority allows one to preempt scheduling of other
	 * non-real-time groups. When such a group becomes executable,
	 * it will evict the group with the lowest non-rt priority if
	 * there's no free group slot available.
	 */
	PANTHOR_CSG_PRIORITY_RT,

	/** @PANTHOR_CSG_PRIORITY_COUNT: Number of priority levels. */
	PANTHOR_CSG_PRIORITY_COUNT,
};

/**
 * struct panthor_scheduler - Object used to manage the scheduler
 */
struct panthor_scheduler {
	/** @ptdev: Device. */
	struct panthor_device *ptdev;

	/**
	 * @wq: Workqueue used by our internal scheduler logic and
	 * drm_gpu_scheduler.
	 *
	 * Used for the scheduler tick, group update or other kind of FW
	 * event processing that can't be handled in the threaded interrupt
	 * path. Also passed to the drm_gpu_scheduler instances embedded
	 * in panthor_queue.
	 */
	struct workqueue_struct *wq;

	/**
	 * @heap_alloc_wq: Workqueue used to schedule tiler_oom works.
	 *
	 * We have a queue dedicated to heap chunk allocation works to avoid
	 * blocking the rest of the scheduler if the allocation tries to
	 * reclaim memory.
	 */
	struct workqueue_struct *heap_alloc_wq;

	/** @tick_work: Work executed on a scheduling tick. */
	struct delayed_work tick_work;

	/**
	 * @sync_upd_work: Work used to process synchronization object updates.
	 *
	 * We use this work to unblock queues/groups that were waiting on a
	 * synchronization object.
	 */
	struct work_struct sync_upd_work;

	/**
	 * @fw_events_work: Work used to process FW events outside the interrupt path.
	 *
	 * Even if the interrupt is threaded, we need any event processing
	 * that require taking the panthor_scheduler::lock to be processed
	 * outside the interrupt path so we don't block the tick logic when
	 * it calls panthor_fw_{csg,wait}_wait_acks(). Since most of the
	 * event processing requires taking this lock, we just delegate all
	 * FW event processing to the scheduler workqueue.
	 */
	struct work_struct fw_events_work;

	/**
	 * @fw_events: Bitmask encoding pending FW events.
	 */
	atomic_t fw_events;

	/**
	 * @resched_target: When the next tick should occur.
	 *
	 * Expressed in jiffies.
	 */
	u64 resched_target;

	/**
	 * @last_tick: When the last tick occurred.
	 *
	 * Expressed in jiffies.
	 */
	u64 last_tick;

	/** @tick_period: Tick period in jiffies. */
	u64 tick_period;

	/**
	 * @lock: Lock protecting access to all the scheduler fields.
	 *
	 * Should be taken in the tick work, the irq handler, and anywhere the @groups
	 * fields are touched.
	 */
	struct mutex lock;

	/** @groups: Various lists used to classify groups. */
	struct {
		/**
		 * @runnable: Runnable group lists.
		 *
		 * When a group has queues that want to execute something,
		 * its panthor_group::run_node should be inserted here.
		 *
		 * One list per-priority.
		 */
		struct list_head runnable[PANTHOR_CSG_PRIORITY_COUNT];

		/**
		 * @idle: Idle group lists.
		 *
		 * When all queues of a group are idle (either because they
		 * have nothing to execute, or because they are blocked), the
		 * panthor_group::run_node field should be inserted here.
		 *
		 * One list per-priority.
		 */
		struct list_head idle[PANTHOR_CSG_PRIORITY_COUNT];

		/**
		 * @waiting: List of groups whose queues are blocked on a
		 * synchronization object.
		 *
		 * Insert panthor_group::wait_node here when a group is waiting
		 * for synchronization objects to be signaled.
		 *
		 * This list is evaluated in the @sync_upd_work work.
		 */
		struct list_head waiting;
	} groups;

	/**
	 * @csg_slots: FW command stream group slots.
	 */
	struct panthor_csg_slot csg_slots[MAX_CSGS];

	/** @csg_slot_count: Number of command stream group slots exposed by the FW. */
	u32 csg_slot_count;

	/** @cs_slot_count: Number of command stream slot per group slot exposed by the FW. */
	u32 cs_slot_count;

	/** @as_slot_count: Number of address space slots supported by the MMU. */
	u32 as_slot_count;

	/** @used_csg_slot_count: Number of command stream group slot currently used. */
	u32 used_csg_slot_count;

	/** @sb_slot_count: Number of scoreboard slots. */
	u32 sb_slot_count;

	/**
	 * @might_have_idle_groups: True if an active group might have become idle.
	 *
	 * This will force a tick, so other runnable groups can be scheduled if one
	 * or more active groups became idle.
	 */
	bool might_have_idle_groups;

	/** @pm: Power management related fields. */
	struct {
		/** @has_ref: True if the scheduler owns a runtime PM reference. */
		bool has_ref;
	} pm;

	/** @reset: Reset related fields. */
	struct {
		/** @lock: Lock protecting the other reset fields. */
		struct mutex lock;

		/**
		 * @in_progress: True if a reset is in progress.
		 *
		 * Set to true in panthor_sched_pre_reset() and back to false in
		 * panthor_sched_post_reset().
		 */
		atomic_t in_progress;

		/**
		 * @stopped_groups: List containing all groups that were stopped
		 * before a reset.
		 *
		 * Insert panthor_group::run_node in the pre_reset path.
		 */
		struct list_head stopped_groups;
	} reset;
};

/**
 * struct panthor_syncobj_32b - 32-bit FW synchronization object
 */
struct panthor_syncobj_32b {
	/** @seqno: Sequence number. */
	u32 seqno;

	/**
	 * @status: Status.
	 *
	 * Not zero on failure.
	 */
	u32 status;
};

/**
 * struct panthor_syncobj_64b - 64-bit FW synchronization object
 */
struct panthor_syncobj_64b {
	/** @seqno: Sequence number. */
	u64 seqno;

	/**
	 * @status: Status.
	 *
	 * Not zero on failure.
	 */
	u32 status;

	/** @pad: MBZ. */
	u32 pad;
};

/**
 * struct panthor_queue - Execution queue
 */
struct panthor_queue {
	/** @scheduler: DRM scheduler used for this queue. */
	struct drm_gpu_scheduler scheduler;

	/** @entity: DRM scheduling entity used for this queue. */
	struct drm_sched_entity entity;

	/**
	 * @remaining_time: Time remaining before the job timeout expires.
	 *
	 * The job timeout is suspended when the queue is not scheduled by the
	 * FW. Every time we suspend the timer, we need to save the remaining
	 * time so we can restore it later on.
	 */
	unsigned long remaining_time;

	/** @timeout_suspended: True if the job timeout was suspended. */
	bool timeout_suspended;

	/**
	 * @doorbell_id: Doorbell assigned to this queue.
	 *
	 * Right now, all groups share the same doorbell, and the doorbell ID
	 * is assigned to group_slot + 1 when the group is assigned a slot. But
	 * we might decide to provide fine grained doorbell assignment at some
	 * point, so don't have to wake up all queues in a group every time one
	 * of them is updated.
	 */
	u8 doorbell_id;

	/**
	 * @priority: Priority of the queue inside the group.
	 *
	 * Must be less than 16 (Only 4 bits available).
	 */
	u8 priority;
#define CSF_MAX_QUEUE_PRIO	GENMASK(3, 0)

	/** @ringbuf: Command stream ring-buffer. */
	struct panthor_kernel_bo *ringbuf;

	/** @iface: Firmware interface. */
	struct {
		/** @mem: FW memory allocated for this interface. */
		struct panthor_kernel_bo *mem;

		/** @input: Input interface. */
		struct panthor_fw_ringbuf_input_iface *input;

		/** @output: Output interface. */
		const struct panthor_fw_ringbuf_output_iface *output;

		/** @input_fw_va: FW virtual address of the input interface buffer. */
		u32 input_fw_va;

		/** @output_fw_va: FW virtual address of the output interface buffer. */
		u32 output_fw_va;
	} iface;

	/**
	 * @syncwait: Stores information about the synchronization object this
	 * queue is waiting on.
	 */
	struct {
		/** @gpu_va: GPU address of the synchronization object. */
		u64 gpu_va;

		/** @ref: Reference value to compare against. */
		u64 ref;

		/** @gt: True if this is a greater-than test. */
		bool gt;

		/** @sync64: True if this is a 64-bit sync object. */
		bool sync64;

		/** @bo: Buffer object holding the synchronization object. */
		struct drm_gem_object *obj;

		/** @offset: Offset of the synchronization object inside @bo. */
		u64 offset;

		/**
		 * @kmap: Kernel mapping of the buffer object holding the
		 * synchronization object.
		 */
		void *kmap;
	} syncwait;

	/** @fence_ctx: Fence context fields. */
	struct {
		/** @lock: Used to protect access to all fences allocated by this context. */
		spinlock_t lock;

		/**
		 * @id: Fence context ID.
		 *
		 * Allocated with dma_fence_context_alloc().
		 */
		u64 id;

		/** @seqno: Sequence number of the last initialized fence. */
		atomic64_t seqno;

		/**
		 * @last_fence: Fence of the last submitted job.
		 *
		 * We return this fence when we get an empty command stream.
		 * This way, we are guaranteed that all earlier jobs have completed
		 * when drm_sched_job::s_fence::finished without having to feed
		 * the CS ring buffer with a dummy job that only signals the fence.
		 */
		struct dma_fence *last_fence;

		/**
		 * @in_flight_jobs: List containing all in-flight jobs.
		 *
		 * Used to keep track and signal panthor_job::done_fence when the
		 * synchronization object attached to the queue is signaled.
		 */
		struct list_head in_flight_jobs;
	} fence_ctx;

	/** @profiling: Job profiling data slots and access information. */
	struct {
		/** @slots: Kernel BO holding the slots. */
		struct panthor_kernel_bo *slots;

		/** @slot_count: Number of jobs ringbuffer can hold at once. */
		u32 slot_count;

		/** @seqno: Index of the next available profiling information slot. */
		u32 seqno;
	} profiling;
};

/**
 * enum panthor_group_state - Scheduling group state.
 */
enum panthor_group_state {
	/** @PANTHOR_CS_GROUP_CREATED: Group was created, but not scheduled yet. */
	PANTHOR_CS_GROUP_CREATED,

	/** @PANTHOR_CS_GROUP_ACTIVE: Group is currently scheduled. */
	PANTHOR_CS_GROUP_ACTIVE,

	/**
	 * @PANTHOR_CS_GROUP_SUSPENDED: Group was scheduled at least once, but is
	 * inactive/suspended right now.
	 */
	PANTHOR_CS_GROUP_SUSPENDED,

	/**
	 * @PANTHOR_CS_GROUP_TERMINATED: Group was terminated.
	 *
	 * Can no longer be scheduled. The only allowed action is a destruction.
	 */
	PANTHOR_CS_GROUP_TERMINATED,

	/**
	 * @PANTHOR_CS_GROUP_UNKNOWN_STATE: Group is an unknown state.
	 *
	 * The FW returned an inconsistent state. The group is flagged unusable
	 * and can no longer be scheduled. The only allowed action is a
	 * destruction.
	 *
	 * When that happens, we also schedule a FW reset, to start from a fresh
	 * state.
	 */
	PANTHOR_CS_GROUP_UNKNOWN_STATE,
};

/**
 * struct panthor_group - Scheduling group object
 */
struct panthor_group {
	/** @refcount: Reference count */
	struct kref refcount;

	/** @ptdev: Device. */
	struct panthor_device *ptdev;

	/** @vm: VM bound to the group. */
	struct panthor_vm *vm;

	/** @compute_core_mask: Mask of shader cores that can be used for compute jobs. */
	u64 compute_core_mask;

	/** @fragment_core_mask: Mask of shader cores that can be used for fragment jobs. */
	u64 fragment_core_mask;

	/** @tiler_core_mask: Mask of tiler cores that can be used for tiler jobs. */
	u64 tiler_core_mask;

	/** @max_compute_cores: Maximum number of shader cores used for compute jobs. */
	u8 max_compute_cores;

	/** @max_fragment_cores: Maximum number of shader cores used for fragment jobs. */
	u8 max_fragment_cores;

	/** @max_tiler_cores: Maximum number of tiler cores used for tiler jobs. */
	u8 max_tiler_cores;

	/** @priority: Group priority (check panthor_csg_priority). */
	u8 priority;

	/** @blocked_queues: Bitmask reflecting the blocked queues. */
	u32 blocked_queues;

	/** @idle_queues: Bitmask reflecting the idle queues. */
	u32 idle_queues;

	/** @fatal_lock: Lock used to protect access to fatal fields. */
	spinlock_t fatal_lock;

	/** @fatal_queues: Bitmask reflecting the queues that hit a fatal exception. */
	u32 fatal_queues;

	/** @tiler_oom: Mask of queues that have a tiler OOM event to process. */
	atomic_t tiler_oom;

	/** @queue_count: Number of queues in this group. */
	u32 queue_count;

	/** @queues: Queues owned by this group. */
	struct panthor_queue *queues[MAX_CS_PER_CSG];

	/**
	 * @csg_id: ID of the FW group slot.
	 *
	 * -1 when the group is not scheduled/active.
	 */
	int csg_id;

	/**
	 * @destroyed: True when the group has been destroyed.
	 *
	 * If a group is destroyed it becomes useless: no further jobs can be submitted
	 * to its queues. We simply wait for all references to be dropped so we can
	 * release the group object.
	 */
	bool destroyed;

	/**
	 * @timedout: True when a timeout occurred on any of the queues owned by
	 * this group.
	 *
	 * Timeouts can be reported by drm_sched or by the FW. If a reset is required,
	 * and the group can't be suspended, this also leads to a timeout. In any case,
	 * any timeout situation is unrecoverable, and the group becomes useless. We
	 * simply wait for all references to be dropped so we can release the group
	 * object.
	 */
	bool timedout;

	/**
	 * @innocent: True when the group becomes unusable because the group suspension
	 * failed during a reset.
	 *
	 * Sometimes the FW was put in a bad state by other groups, causing the group
	 * suspension happening in the reset path to fail. In that case, we consider the
	 * group innocent.
	 */
	bool innocent;

	/**
	 * @syncobjs: Pool of per-queue synchronization objects.
	 *
	 * One sync object per queue. The position of the sync object is
	 * determined by the queue index.
	 */
	struct panthor_kernel_bo *syncobjs;

	/** @fdinfo: Per-file info exposed through /proc/<process>/fdinfo */
	struct {
		/** @data: Total sampled values for jobs in queues from this group. */
		struct panthor_gpu_usage data;

		/**
		 * @fdinfo.lock: Spinlock to govern concurrent access from drm file's fdinfo
		 * callback and job post-completion processing function
		 */
		spinlock_t lock;

		/** @fdinfo.kbo_sizes: Aggregate size of private kernel BO's held by the group. */
		size_t kbo_sizes;
	} fdinfo;

	/** @task_info: Info of current->group_leader that created the group. */
	struct {
		/** @task_info.pid: pid of current->group_leader */
		pid_t pid;

		/** @task_info.comm: comm of current->group_leader */
		char comm[TASK_COMM_LEN];
	} task_info;

	/** @state: Group state. */
	enum panthor_group_state state;

	/**
	 * @suspend_buf: Suspend buffer.
	 *
	 * Stores the state of the group and its queues when a group is suspended.
	 * Used at resume time to restore the group in its previous state.
	 *
	 * The size of the suspend buffer is exposed through the FW interface.
	 */
	struct panthor_kernel_bo *suspend_buf;

	/**
	 * @protm_suspend_buf: Protection mode suspend buffer.
	 *
	 * Stores the state of the group and its queues when a group that's in
	 * protection mode is suspended.
	 *
	 * Used at resume time to restore the group in its previous state.
	 *
	 * The size of the protection mode suspend buffer is exposed through the
	 * FW interface.
	 */
	struct panthor_kernel_bo *protm_suspend_buf;

	/** @sync_upd_work: Work used to check/signal job fences. */
	struct work_struct sync_upd_work;

	/** @tiler_oom_work: Work used to process tiler OOM events happening on this group. */
	struct work_struct tiler_oom_work;

	/** @term_work: Work used to finish the group termination procedure. */
	struct work_struct term_work;

	/**
	 * @release_work: Work used to release group resources.
	 *
	 * We need to postpone the group release to avoid a deadlock when
	 * the last ref is released in the tick work.
	 */
	struct work_struct release_work;

	/**
	 * @run_node: Node used to insert the group in the
	 * panthor_group::groups::{runnable,idle} and
	 * panthor_group::reset.stopped_groups lists.
	 */
	struct list_head run_node;

	/**
	 * @wait_node: Node used to insert the group in the
	 * panthor_group::groups::waiting list.
	 */
	struct list_head wait_node;
};

struct panthor_job_profiling_data {
	struct {
		u64 before;
		u64 after;
	} cycles;

	struct {
		u64 before;
		u64 after;
	} time;
};

/**
 * group_queue_work() - Queue a group work
 * @group: Group to queue the work for.
 * @wname: Work name.
 *
 * Grabs a ref and queue a work item to the scheduler workqueue. If
 * the work was already queued, we release the reference we grabbed.
 *
 * Work callbacks must release the reference we grabbed here.
 */
#define group_queue_work(group, wname) \
	do { \
		group_get(group); \
		if (!queue_work((group)->ptdev->scheduler->wq, &(group)->wname ## _work)) \
			group_put(group); \
	} while (0)

/**
 * sched_queue_work() - Queue a scheduler work.
 * @sched: Scheduler object.
 * @wname: Work name.
 *
 * Conditionally queues a scheduler work if no reset is pending/in-progress.
 */
#define sched_queue_work(sched, wname) \
	do { \
		if (!atomic_read(&(sched)->reset.in_progress) && \
		    !panthor_device_reset_is_pending((sched)->ptdev)) \
			queue_work((sched)->wq, &(sched)->wname ## _work); \
	} while (0)

/**
 * sched_queue_delayed_work() - Queue a scheduler delayed work.
 * @sched: Scheduler object.
 * @wname: Work name.
 * @delay: Work delay in jiffies.
 *
 * Conditionally queues a scheduler delayed work if no reset is
 * pending/in-progress.
 */
#define sched_queue_delayed_work(sched, wname, delay) \
	do { \
		if (!atomic_read(&sched->reset.in_progress) && \
		    !panthor_device_reset_is_pending((sched)->ptdev)) \
			mod_delayed_work((sched)->wq, &(sched)->wname ## _work, delay); \
	} while (0)

/*
 * We currently set the maximum of groups per file to an arbitrary low value.
 * But this can be updated if we need more.
 */
#define MAX_GROUPS_PER_POOL 128

/**
 * struct panthor_group_pool - Group pool
 *
 * Each file get assigned a group pool.
 */
struct panthor_group_pool {
	/** @xa: Xarray used to manage group handles. */
	struct xarray xa;
};

/**
 * struct panthor_job - Used to manage GPU job
 */
struct panthor_job {
	/** @base: Inherit from drm_sched_job. */
	struct drm_sched_job base;

	/** @refcount: Reference count. */
	struct kref refcount;

	/** @group: Group of the queue this job will be pushed to. */
	struct panthor_group *group;

	/** @queue_idx: Index of the queue inside @group. */
	u32 queue_idx;

	/** @call_info: Information about the userspace command stream call. */
	struct {
		/** @start: GPU address of the userspace command stream. */
		u64 start;

		/** @size: Size of the userspace command stream. */
		u32 size;

		/**
		 * @latest_flush: Flush ID at the time the userspace command
		 * stream was built.
		 *
		 * Needed for the flush reduction mechanism.
		 */
		u32 latest_flush;
	} call_info;

	/** @ringbuf: Position of this job is in the ring buffer. */
	struct {
		/** @start: Start offset. */
		u64 start;

		/** @end: End offset. */
		u64 end;
	} ringbuf;

	/**
	 * @node: Used to insert the job in the panthor_queue::fence_ctx::in_flight_jobs
	 * list.
	 */
	struct list_head node;

	/** @done_fence: Fence signaled when the job is finished or cancelled. */
	struct dma_fence *done_fence;

	/** @profiling: Job profiling information. */
	struct {
		/** @mask: Current device job profiling enablement bitmask. */
		u32 mask;

		/** @slot: Job index in the profiling slots BO. */
		u32 slot;
	} profiling;
};

static void
panthor_queue_put_syncwait_obj(struct panthor_queue *queue)
{
	if (queue->syncwait.kmap) {
		struct iosys_map map = IOSYS_MAP_INIT_VADDR(queue->syncwait.kmap);

		drm_gem_vunmap(queue->syncwait.obj, &map);
		queue->syncwait.kmap = NULL;
	}

	drm_gem_object_put(queue->syncwait.obj);
	queue->syncwait.obj = NULL;
}

static void *
panthor_queue_get_syncwait_obj(struct panthor_group *group, struct panthor_queue *queue)
{
	struct panthor_device *ptdev = group->ptdev;
	struct panthor_gem_object *bo;
	struct iosys_map map;
	int ret;

	if (queue->syncwait.kmap)
		return queue->syncwait.kmap + queue->syncwait.offset;

	bo = panthor_vm_get_bo_for_va(group->vm,
				      queue->syncwait.gpu_va,
				      &queue->syncwait.offset);
	if (drm_WARN_ON(&ptdev->base, IS_ERR_OR_NULL(bo)))
		goto err_put_syncwait_obj;

	queue->syncwait.obj = &bo->base.base;
	ret = drm_gem_vmap(queue->syncwait.obj, &map);
	if (drm_WARN_ON(&ptdev->base, ret))
		goto err_put_syncwait_obj;

	queue->syncwait.kmap = map.vaddr;
	if (drm_WARN_ON(&ptdev->base, !queue->syncwait.kmap))
		goto err_put_syncwait_obj;

	return queue->syncwait.kmap + queue->syncwait.offset;

err_put_syncwait_obj:
	panthor_queue_put_syncwait_obj(queue);
	return NULL;
}

static void group_free_queue(struct panthor_group *group, struct panthor_queue *queue)
{
	if (IS_ERR_OR_NULL(queue))
		return;

	drm_sched_entity_destroy(&queue->entity);

	if (queue->scheduler.ops)
		drm_sched_fini(&queue->scheduler);

	panthor_queue_put_syncwait_obj(queue);

	panthor_kernel_bo_destroy(queue->ringbuf);
	panthor_kernel_bo_destroy(queue->iface.mem);
	panthor_kernel_bo_destroy(queue->profiling.slots);

	/* Release the last_fence we were holding, if any. */
	dma_fence_put(queue->fence_ctx.last_fence);

	kfree(queue);
}

static void group_release_work(struct work_struct *work)
{
	struct panthor_group *group = container_of(work,
						   struct panthor_group,
						   release_work);
	u32 i;

	for (i = 0; i < group->queue_count; i++)
		group_free_queue(group, group->queues[i]);

	panthor_kernel_bo_destroy(group->suspend_buf);
	panthor_kernel_bo_destroy(group->protm_suspend_buf);
	panthor_kernel_bo_destroy(group->syncobjs);

	panthor_vm_put(group->vm);
	kfree(group);
}

static void group_release(struct kref *kref)
{
	struct panthor_group *group = container_of(kref,
						   struct panthor_group,
						   refcount);
	struct panthor_device *ptdev = group->ptdev;

	drm_WARN_ON(&ptdev->base, group->csg_id >= 0);
	drm_WARN_ON(&ptdev->base, !list_empty(&group->run_node));
	drm_WARN_ON(&ptdev->base, !list_empty(&group->wait_node));

	queue_work(panthor_cleanup_wq, &group->release_work);
}

static void group_put(struct panthor_group *group)
{
	if (group)
		kref_put(&group->refcount, group_release);
}

static struct panthor_group *
group_get(struct panthor_group *group)
{
	if (group)
		kref_get(&group->refcount);

	return group;
}

/**
 * group_bind_locked() - Bind a group to a group slot
 * @group: Group.
 * @csg_id: Slot.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
static int
group_bind_locked(struct panthor_group *group, u32 csg_id)
{
	struct panthor_device *ptdev = group->ptdev;
	struct panthor_csg_slot *csg_slot;
	int ret;

	lockdep_assert_held(&ptdev->scheduler->lock);

	if (drm_WARN_ON(&ptdev->base, group->csg_id != -1 || csg_id >= MAX_CSGS ||
			ptdev->scheduler->csg_slots[csg_id].group))
		return -EINVAL;

	ret = panthor_vm_active(group->vm);
	if (ret)
		return ret;

	csg_slot = &ptdev->scheduler->csg_slots[csg_id];
	group_get(group);
	group->csg_id = csg_id;

	/* Dummy doorbell allocation: doorbell is assigned to the group and
	 * all queues use the same doorbell.
	 *
	 * TODO: Implement LRU-based doorbell assignment, so the most often
	 * updated queues get their own doorbell, thus avoiding useless checks
	 * on queues belonging to the same group that are rarely updated.
	 */
	for (u32 i = 0; i < group->queue_count; i++)
		group->queues[i]->doorbell_id = csg_id + 1;

	csg_slot->group = group;

	return 0;
}

/**
 * group_unbind_locked() - Unbind a group from a slot.
 * @group: Group to unbind.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
static int
group_unbind_locked(struct panthor_group *group)
{
	struct panthor_device *ptdev = group->ptdev;
	struct panthor_csg_slot *slot;

	lockdep_assert_held(&ptdev->scheduler->lock);

	if (drm_WARN_ON(&ptdev->base, group->csg_id < 0 || group->csg_id >= MAX_CSGS))
		return -EINVAL;

	if (drm_WARN_ON(&ptdev->base, group->state == PANTHOR_CS_GROUP_ACTIVE))
		return -EINVAL;

	slot = &ptdev->scheduler->csg_slots[group->csg_id];
	panthor_vm_idle(group->vm);
	group->csg_id = -1;

	/* Tiler OOM events will be re-issued next time the group is scheduled. */
	atomic_set(&group->tiler_oom, 0);
	cancel_work(&group->tiler_oom_work);

	for (u32 i = 0; i < group->queue_count; i++)
		group->queues[i]->doorbell_id = -1;

	slot->group = NULL;

	group_put(group);
	return 0;
}

/**
 * cs_slot_prog_locked() - Program a queue slot
 * @ptdev: Device.
 * @csg_id: Group slot ID.
 * @cs_id: Queue slot ID.
 *
 * Program a queue slot with the queue information so things can start being
 * executed on this queue.
 *
 * The group slot must have a group bound to it already (group_bind_locked()).
 */
static void
cs_slot_prog_locked(struct panthor_device *ptdev, u32 csg_id, u32 cs_id)
{
	struct panthor_queue *queue = ptdev->scheduler->csg_slots[csg_id].group->queues[cs_id];
	struct panthor_fw_cs_iface *cs_iface = panthor_fw_get_cs_iface(ptdev, csg_id, cs_id);

	lockdep_assert_held(&ptdev->scheduler->lock);

	queue->iface.input->extract = queue->iface.output->extract;
	drm_WARN_ON(&ptdev->base, queue->iface.input->insert < queue->iface.input->extract);

	cs_iface->input->ringbuf_base = panthor_kernel_bo_gpuva(queue->ringbuf);
	cs_iface->input->ringbuf_size = panthor_kernel_bo_size(queue->ringbuf);
	cs_iface->input->ringbuf_input = queue->iface.input_fw_va;
	cs_iface->input->ringbuf_output = queue->iface.output_fw_va;
	cs_iface->input->config = CS_CONFIG_PRIORITY(queue->priority) |
				  CS_CONFIG_DOORBELL(queue->doorbell_id);
	cs_iface->input->ack_irq_mask = ~0;
	panthor_fw_update_reqs(cs_iface, req,
			       CS_IDLE_SYNC_WAIT |
			       CS_IDLE_EMPTY |
			       CS_STATE_START |
			       CS_EXTRACT_EVENT,
			       CS_IDLE_SYNC_WAIT |
			       CS_IDLE_EMPTY |
			       CS_STATE_MASK |
			       CS_EXTRACT_EVENT);
	if (queue->iface.input->insert != queue->iface.input->extract && queue->timeout_suspended) {
		drm_sched_resume_timeout(&queue->scheduler, queue->remaining_time);
		queue->timeout_suspended = false;
	}
}

/**
 * cs_slot_reset_locked() - Reset a queue slot
 * @ptdev: Device.
 * @csg_id: Group slot.
 * @cs_id: Queue slot.
 *
 * Change the queue slot state to STOP and suspend the queue timeout if
 * the queue is not blocked.
 *
 * The group slot must have a group bound to it (group_bind_locked()).
 */
static int
cs_slot_reset_locked(struct panthor_device *ptdev, u32 csg_id, u32 cs_id)
{
	struct panthor_fw_cs_iface *cs_iface = panthor_fw_get_cs_iface(ptdev, csg_id, cs_id);
	struct panthor_group *group = ptdev->scheduler->csg_slots[csg_id].group;
	struct panthor_queue *queue = group->queues[cs_id];

	lockdep_assert_held(&ptdev->scheduler->lock);

	panthor_fw_update_reqs(cs_iface, req,
			       CS_STATE_STOP,
			       CS_STATE_MASK);

	/* If the queue is blocked, we want to keep the timeout running, so
	 * we can detect unbounded waits and kill the group when that happens.
	 */
	if (!(group->blocked_queues & BIT(cs_id)) && !queue->timeout_suspended) {
		queue->remaining_time = drm_sched_suspend_timeout(&queue->scheduler);
		queue->timeout_suspended = true;
		WARN_ON(queue->remaining_time > msecs_to_jiffies(JOB_TIMEOUT_MS));
	}

	return 0;
}

/**
 * csg_slot_sync_priority_locked() - Synchronize the group slot priority
 * @ptdev: Device.
 * @csg_id: Group slot ID.
 *
 * Group slot priority update happens asynchronously. When we receive a
 * %CSG_ENDPOINT_CONFIG, we know the update is effective, and can
 * reflect it to our panthor_csg_slot object.
 */
static void
csg_slot_sync_priority_locked(struct panthor_device *ptdev, u32 csg_id)
{
	struct panthor_csg_slot *csg_slot = &ptdev->scheduler->csg_slots[csg_id];
	struct panthor_fw_csg_iface *csg_iface;

	lockdep_assert_held(&ptdev->scheduler->lock);

	csg_iface = panthor_fw_get_csg_iface(ptdev, csg_id);
	csg_slot->priority = (csg_iface->input->endpoint_req & CSG_EP_REQ_PRIORITY_MASK) >> 28;
}

/**
 * cs_slot_sync_queue_state_locked() - Synchronize the queue slot priority
 * @ptdev: Device.
 * @csg_id: Group slot.
 * @cs_id: Queue slot.
 *
 * Queue state is updated on group suspend or STATUS_UPDATE event.
 */
static void
cs_slot_sync_queue_state_locked(struct panthor_device *ptdev, u32 csg_id, u32 cs_id)
{
	struct panthor_group *group = ptdev->scheduler->csg_slots[csg_id].group;
	struct panthor_queue *queue = group->queues[cs_id];
	struct panthor_fw_cs_iface *cs_iface =
		panthor_fw_get_cs_iface(group->ptdev, csg_id, cs_id);

	u32 status_wait_cond;

	switch (cs_iface->output->status_blocked_reason) {
	case CS_STATUS_BLOCKED_REASON_UNBLOCKED:
		if (queue->iface.input->insert == queue->iface.output->extract &&
		    cs_iface->output->status_scoreboards == 0)
			group->idle_queues |= BIT(cs_id);
		break;

	case CS_STATUS_BLOCKED_REASON_SYNC_WAIT:
		if (list_empty(&group->wait_node)) {
			list_move_tail(&group->wait_node,
				       &group->ptdev->scheduler->groups.waiting);
		}

		/* The queue is only blocked if there's no deferred operation
		 * pending, which can be checked through the scoreboard status.
		 */
		if (!cs_iface->output->status_scoreboards)
			group->blocked_queues |= BIT(cs_id);

		queue->syncwait.gpu_va = cs_iface->output->status_wait_sync_ptr;
		queue->syncwait.ref = cs_iface->output->status_wait_sync_value;
		status_wait_cond = cs_iface->output->status_wait & CS_STATUS_WAIT_SYNC_COND_MASK;
		queue->syncwait.gt = status_wait_cond == CS_STATUS_WAIT_SYNC_COND_GT;
		if (cs_iface->output->status_wait & CS_STATUS_WAIT_SYNC_64B) {
			u64 sync_val_hi = cs_iface->output->status_wait_sync_value_hi;

			queue->syncwait.sync64 = true;
			queue->syncwait.ref |= sync_val_hi << 32;
		} else {
			queue->syncwait.sync64 = false;
		}
		break;

	default:
		/* Other reasons are not blocking. Consider the queue as runnable
		 * in those cases.
		 */
		break;
	}
}

static void
csg_slot_sync_queues_state_locked(struct panthor_device *ptdev, u32 csg_id)
{
	struct panthor_csg_slot *csg_slot = &ptdev->scheduler->csg_slots[csg_id];
	struct panthor_group *group = csg_slot->group;
	u32 i;

	lockdep_assert_held(&ptdev->scheduler->lock);

	group->idle_queues = 0;
	group->blocked_queues = 0;

	for (i = 0; i < group->queue_count; i++) {
		if (group->queues[i])
			cs_slot_sync_queue_state_locked(ptdev, csg_id, i);
	}
}

static void
csg_slot_sync_state_locked(struct panthor_device *ptdev, u32 csg_id)
{
	struct panthor_csg_slot *csg_slot = &ptdev->scheduler->csg_slots[csg_id];
	struct panthor_fw_csg_iface *csg_iface;
	struct panthor_group *group;
	enum panthor_group_state new_state, old_state;
	u32 csg_state;

	lockdep_assert_held(&ptdev->scheduler->lock);

	csg_iface = panthor_fw_get_csg_iface(ptdev, csg_id);
	group = csg_slot->group;

	if (!group)
		return;

	old_state = group->state;
	csg_state = csg_iface->output->ack & CSG_STATE_MASK;
	switch (csg_state) {
	case CSG_STATE_START:
	case CSG_STATE_RESUME:
		new_state = PANTHOR_CS_GROUP_ACTIVE;
		break;
	case CSG_STATE_TERMINATE:
		new_state = PANTHOR_CS_GROUP_TERMINATED;
		break;
	case CSG_STATE_SUSPEND:
		new_state = PANTHOR_CS_GROUP_SUSPENDED;
		break;
	default:
		/* The unknown state might be caused by a FW state corruption,
		 * which means the group metadata can't be trusted anymore, and
		 * the SUSPEND operation might propagate the corruption to the
		 * suspend buffers. Flag the group state as unknown to make
		 * sure it's unusable after that point.
		 */
		drm_err(&ptdev->base, "Invalid state on CSG %d (state=%d)",
			csg_id, csg_state);
		new_state = PANTHOR_CS_GROUP_UNKNOWN_STATE;
		break;
	}

	if (old_state == new_state)
		return;

	/* The unknown state might be caused by a FW issue, reset the FW to
	 * take a fresh start.
	 */
	if (new_state == PANTHOR_CS_GROUP_UNKNOWN_STATE)
		panthor_device_schedule_reset(ptdev);

	if (new_state == PANTHOR_CS_GROUP_SUSPENDED)
		csg_slot_sync_queues_state_locked(ptdev, csg_id);

	if (old_state == PANTHOR_CS_GROUP_ACTIVE) {
		u32 i;

		/* Reset the queue slots so we start from a clean
		 * state when starting/resuming a new group on this
		 * CSG slot. No wait needed here, and no ringbell
		 * either, since the CS slot will only be re-used
		 * on the next CSG start operation.
		 */
		for (i = 0; i < group->queue_count; i++) {
			if (group->queues[i])
				cs_slot_reset_locked(ptdev, csg_id, i);
		}
	}

	group->state = new_state;
}

static int
csg_slot_prog_locked(struct panthor_device *ptdev, u32 csg_id, u32 priority)
{
	struct panthor_fw_csg_iface *csg_iface;
	struct panthor_csg_slot *csg_slot;
	struct panthor_group *group;
	u32 queue_mask = 0, i;

	lockdep_assert_held(&ptdev->scheduler->lock);

	if (priority > MAX_CSG_PRIO)
		return -EINVAL;

	if (drm_WARN_ON(&ptdev->base, csg_id >= MAX_CSGS))
		return -EINVAL;

	csg_slot = &ptdev->scheduler->csg_slots[csg_id];
	group = csg_slot->group;
	if (!group || group->state == PANTHOR_CS_GROUP_ACTIVE)
		return 0;

	csg_iface = panthor_fw_get_csg_iface(group->ptdev, csg_id);

	for (i = 0; i < group->queue_count; i++) {
		if (group->queues[i]) {
			cs_slot_prog_locked(ptdev, csg_id, i);
			queue_mask |= BIT(i);
		}
	}

	csg_iface->input->allow_compute = group->compute_core_mask;
	csg_iface->input->allow_fragment = group->fragment_core_mask;
	csg_iface->input->allow_other = group->tiler_core_mask;
	csg_iface->input->endpoint_req = CSG_EP_REQ_COMPUTE(group->max_compute_cores) |
					 CSG_EP_REQ_FRAGMENT(group->max_fragment_cores) |
					 CSG_EP_REQ_TILER(group->max_tiler_cores) |
					 CSG_EP_REQ_PRIORITY(priority);
	csg_iface->input->config = panthor_vm_as(group->vm);

	if (group->suspend_buf)
		csg_iface->input->suspend_buf = panthor_kernel_bo_gpuva(group->suspend_buf);
	else
		csg_iface->input->suspend_buf = 0;

	if (group->protm_suspend_buf) {
		csg_iface->input->protm_suspend_buf =
			panthor_kernel_bo_gpuva(group->protm_suspend_buf);
	} else {
		csg_iface->input->protm_suspend_buf = 0;
	}

	csg_iface->input->ack_irq_mask = ~0;
	panthor_fw_toggle_reqs(csg_iface, doorbell_req, doorbell_ack, queue_mask);
	return 0;
}

static void
cs_slot_process_fatal_event_locked(struct panthor_device *ptdev,
				   u32 csg_id, u32 cs_id)
{
	struct panthor_scheduler *sched = ptdev->scheduler;
	struct panthor_csg_slot *csg_slot = &sched->csg_slots[csg_id];
	struct panthor_group *group = csg_slot->group;
	struct panthor_fw_cs_iface *cs_iface;
	u32 fatal;
	u64 info;

	lockdep_assert_held(&sched->lock);

	cs_iface = panthor_fw_get_cs_iface(ptdev, csg_id, cs_id);
	fatal = cs_iface->output->fatal;
	info = cs_iface->output->fatal_info;

	if (group) {
		drm_warn(&ptdev->base, "CS_FATAL: pid=%d, comm=%s\n",
			 group->task_info.pid, group->task_info.comm);

		group->fatal_queues |= BIT(cs_id);
	}

	if (CS_EXCEPTION_TYPE(fatal) == DRM_PANTHOR_EXCEPTION_CS_UNRECOVERABLE) {
		/* If this exception is unrecoverable, queue a reset, and make
		 * sure we stop scheduling groups until the reset has happened.
		 */
		panthor_device_schedule_reset(ptdev);
		cancel_delayed_work(&sched->tick_work);
	} else {
		sched_queue_delayed_work(sched, tick, 0);
	}

	drm_warn(&ptdev->base,
		 "CSG slot %d CS slot: %d\n"
		 "CS_FATAL.EXCEPTION_TYPE: 0x%x (%s)\n"
		 "CS_FATAL.EXCEPTION_DATA: 0x%x\n"
		 "CS_FATAL_INFO.EXCEPTION_DATA: 0x%llx\n",
		 csg_id, cs_id,
		 (unsigned int)CS_EXCEPTION_TYPE(fatal),
		 panthor_exception_name(ptdev, CS_EXCEPTION_TYPE(fatal)),
		 (unsigned int)CS_EXCEPTION_DATA(fatal),
		 info);
}

static void
cs_slot_process_fault_event_locked(struct panthor_device *ptdev,
				   u32 csg_id, u32 cs_id)
{
	struct panthor_scheduler *sched = ptdev->scheduler;
	struct panthor_csg_slot *csg_slot = &sched->csg_slots[csg_id];
	struct panthor_group *group = csg_slot->group;
	struct panthor_queue *queue = group && cs_id < group->queue_count ?
				      group->queues[cs_id] : NULL;
	struct panthor_fw_cs_iface *cs_iface;
	u32 fault;
	u64 info;

	lockdep_assert_held(&sched->lock);

	cs_iface = panthor_fw_get_cs_iface(ptdev, csg_id, cs_id);
	fault = cs_iface->output->fault;
	info = cs_iface->output->fault_info;

	if (queue && CS_EXCEPTION_TYPE(fault) == DRM_PANTHOR_EXCEPTION_CS_INHERIT_FAULT) {
		u64 cs_extract = queue->iface.output->extract;
		struct panthor_job *job;

		spin_lock(&queue->fence_ctx.lock);
		list_for_each_entry(job, &queue->fence_ctx.in_flight_jobs, node) {
			if (cs_extract >= job->ringbuf.end)
				continue;

			if (cs_extract < job->ringbuf.start)
				break;

			dma_fence_set_error(job->done_fence, -EINVAL);
		}
		spin_unlock(&queue->fence_ctx.lock);
	}

	if (group) {
		drm_warn(&ptdev->base, "CS_FAULT: pid=%d, comm=%s\n",
			 group->task_info.pid, group->task_info.comm);
	}

	drm_warn(&ptdev->base,
		 "CSG slot %d CS slot: %d\n"
		 "CS_FAULT.EXCEPTION_TYPE: 0x%x (%s)\n"
		 "CS_FAULT.EXCEPTION_DATA: 0x%x\n"
		 "CS_FAULT_INFO.EXCEPTION_DATA: 0x%llx\n",
		 csg_id, cs_id,
		 (unsigned int)CS_EXCEPTION_TYPE(fault),
		 panthor_exception_name(ptdev, CS_EXCEPTION_TYPE(fault)),
		 (unsigned int)CS_EXCEPTION_DATA(fault),
		 info);
}

static int group_process_tiler_oom(struct panthor_group *group, u32 cs_id)
{
	struct panthor_device *ptdev = group->ptdev;
	struct panthor_scheduler *sched = ptdev->scheduler;
	u32 renderpasses_in_flight, pending_frag_count;
	struct panthor_heap_pool *heaps = NULL;
	u64 heap_address, new_chunk_va = 0;
	u32 vt_start, vt_end, frag_end;
	int ret, csg_id;

	mutex_lock(&sched->lock);
	csg_id = group->csg_id;
	if (csg_id >= 0) {
		struct panthor_fw_cs_iface *cs_iface;

		cs_iface = panthor_fw_get_cs_iface(ptdev, csg_id, cs_id);
		heaps = panthor_vm_get_heap_pool(group->vm, false);
		heap_address = cs_iface->output->heap_address;
		vt_start = cs_iface->output->heap_vt_start;
		vt_end = cs_iface->output->heap_vt_end;
		frag_end = cs_iface->output->heap_frag_end;
		renderpasses_in_flight = vt_start - frag_end;
		pending_frag_count = vt_end - frag_end;
	}
	mutex_unlock(&sched->lock);

	/* The group got scheduled out, we stop here. We will get a new tiler OOM event
	 * when it's scheduled again.
	 */
	if (unlikely(csg_id < 0))
		return 0;

	if (IS_ERR(heaps) || frag_end > vt_end || vt_end >= vt_start) {
		ret = -EINVAL;
	} else {
		/* We do the allocation without holding the scheduler lock to avoid
		 * blocking the scheduling.
		 */
		ret = panthor_heap_grow(heaps, heap_address,
					renderpasses_in_flight,
					pending_frag_count, &new_chunk_va);
	}

	/* If the heap context doesn't have memory for us, we want to let the
	 * FW try to reclaim memory by waiting for fragment jobs to land or by
	 * executing the tiler OOM exception handler, which is supposed to
	 * implement incremental rendering.
	 */
	if (ret && ret != -ENOMEM) {
		drm_warn(&ptdev->base, "Failed to extend the tiler heap\n");
		group->fatal_queues |= BIT(cs_id);
		sched_queue_delayed_work(sched, tick, 0);
		goto out_put_heap_pool;
	}

	mutex_lock(&sched->lock);
	csg_id = group->csg_id;
	if (csg_id >= 0) {
		struct panthor_fw_csg_iface *csg_iface;
		struct panthor_fw_cs_iface *cs_iface;

		csg_iface = panthor_fw_get_csg_iface(ptdev, csg_id);
		cs_iface = panthor_fw_get_cs_iface(ptdev, csg_id, cs_id);

		cs_iface->input->heap_start = new_chunk_va;
		cs_iface->input->heap_end = new_chunk_va;
		panthor_fw_update_reqs(cs_iface, req, cs_iface->output->ack, CS_TILER_OOM);
		panthor_fw_toggle_reqs(csg_iface, doorbell_req, doorbell_ack, BIT(cs_id));
		panthor_fw_ring_csg_doorbells(ptdev, BIT(csg_id));
	}
	mutex_unlock(&sched->lock);

	/* We allocated a chunck, but couldn't link it to the heap
	 * context because the group was scheduled out while we were
	 * allocating memory. We need to return this chunk to the heap.
	 */
	if (unlikely(csg_id < 0 && new_chunk_va))
		panthor_heap_return_chunk(heaps, heap_address, new_chunk_va);

	ret = 0;

out_put_heap_pool:
	panthor_heap_pool_put(heaps);
	return ret;
}

static void group_tiler_oom_work(struct work_struct *work)
{
	struct panthor_group *group =
		container_of(work, struct panthor_group, tiler_oom_work);
	u32 tiler_oom = atomic_xchg(&group->tiler_oom, 0);

	while (tiler_oom) {
		u32 cs_id = ffs(tiler_oom) - 1;

		group_process_tiler_oom(group, cs_id);
		tiler_oom &= ~BIT(cs_id);
	}

	group_put(group);
}

static void
cs_slot_process_tiler_oom_event_locked(struct panthor_device *ptdev,
				       u32 csg_id, u32 cs_id)
{
	struct panthor_scheduler *sched = ptdev->scheduler;
	struct panthor_csg_slot *csg_slot = &sched->csg_slots[csg_id];
	struct panthor_group *group = csg_slot->group;

	lockdep_assert_held(&sched->lock);

	if (drm_WARN_ON(&ptdev->base, !group))
		return;

	atomic_or(BIT(cs_id), &group->tiler_oom);

	/* We don't use group_queue_work() here because we want to queue the
	 * work item to the heap_alloc_wq.
	 */
	group_get(group);
	if (!queue_work(sched->heap_alloc_wq, &group->tiler_oom_work))
		group_put(group);
}

static bool cs_slot_process_irq_locked(struct panthor_device *ptdev,
				       u32 csg_id, u32 cs_id)
{
	struct panthor_fw_cs_iface *cs_iface;
	u32 req, ack, events;

	lockdep_assert_held(&ptdev->scheduler->lock);

	cs_iface = panthor_fw_get_cs_iface(ptdev, csg_id, cs_id);
	req = cs_iface->input->req;
	ack = cs_iface->output->ack;
	events = (req ^ ack) & CS_EVT_MASK;

	if (events & CS_FATAL)
		cs_slot_process_fatal_event_locked(ptdev, csg_id, cs_id);

	if (events & CS_FAULT)
		cs_slot_process_fault_event_locked(ptdev, csg_id, cs_id);

	if (events & CS_TILER_OOM)
		cs_slot_process_tiler_oom_event_locked(ptdev, csg_id, cs_id);

	/* We don't acknowledge the TILER_OOM event since its handling is
	 * deferred to a separate work.
	 */
	panthor_fw_update_reqs(cs_iface, req, ack, CS_FATAL | CS_FAULT);

	return (events & (CS_FAULT | CS_TILER_OOM)) != 0;
}

static void csg_slot_sync_idle_state_locked(struct panthor_device *ptdev, u32 csg_id)
{
	struct panthor_csg_slot *csg_slot = &ptdev->scheduler->csg_slots[csg_id];
	struct panthor_fw_csg_iface *csg_iface;

	lockdep_assert_held(&ptdev->scheduler->lock);

	csg_iface = panthor_fw_get_csg_iface(ptdev, csg_id);
	csg_slot->idle = csg_iface->output->status_state & CSG_STATUS_STATE_IS_IDLE;
}

static void csg_slot_process_idle_event_locked(struct panthor_device *ptdev, u32 csg_id)
{
	struct panthor_scheduler *sched = ptdev->scheduler;

	lockdep_assert_held(&sched->lock);

	sched->might_have_idle_groups = true;

	/* Schedule a tick so we can evict idle groups and schedule non-idle
	 * ones. This will also update runtime PM and devfreq busy/idle states,
	 * so the device can lower its frequency or get suspended.
	 */
	sched_queue_delayed_work(sched, tick, 0);
}

static void csg_slot_sync_update_locked(struct panthor_device *ptdev,
					u32 csg_id)
{
	struct panthor_csg_slot *csg_slot = &ptdev->scheduler->csg_slots[csg_id];
	struct panthor_group *group = csg_slot->group;

	lockdep_assert_held(&ptdev->scheduler->lock);

	if (group)
		group_queue_work(group, sync_upd);

	sched_queue_work(ptdev->scheduler, sync_upd);
}

static void
csg_slot_process_progress_timer_event_locked(struct panthor_device *ptdev, u32 csg_id)
{
	struct panthor_scheduler *sched = ptdev->scheduler;
	struct panthor_csg_slot *csg_slot = &sched->csg_slots[csg_id];
	struct panthor_group *group = csg_slot->group;

	lockdep_assert_held(&sched->lock);

	group = csg_slot->group;
	if (!drm_WARN_ON(&ptdev->base, !group)) {
		drm_warn(&ptdev->base, "CSG_PROGRESS_TIMER_EVENT: pid=%d, comm=%s\n",
			 group->task_info.pid, group->task_info.comm);

		group->timedout = true;
	}

	drm_warn(&ptdev->base, "CSG slot %d progress timeout\n", csg_id);

	sched_queue_delayed_work(sched, tick, 0);
}

static void sched_process_csg_irq_locked(struct panthor_device *ptdev, u32 csg_id)
{
	u32 req, ack, cs_irq_req, cs_irq_ack, cs_irqs, csg_events;
	struct panthor_fw_csg_iface *csg_iface;
	u32 ring_cs_db_mask = 0;

	lockdep_assert_held(&ptdev->scheduler->lock);

	if (drm_WARN_ON(&ptdev->base, csg_id >= ptdev->scheduler->csg_slot_count))
		return;

	csg_iface = panthor_fw_get_csg_iface(ptdev, csg_id);
	req = READ_ONCE(csg_iface->input->req);
	ack = READ_ONCE(csg_iface->output->ack);
	cs_irq_req = READ_ONCE(csg_iface->output->cs_irq_req);
	cs_irq_ack = READ_ONCE(csg_iface->input->cs_irq_ack);
	csg_events = (req ^ ack) & CSG_EVT_MASK;

	/* There may not be any pending CSG/CS interrupts to process */
	if (req == ack && cs_irq_req == cs_irq_ack)
		return;

	/* Immediately set IRQ_ACK bits to be same as the IRQ_REQ bits before
	 * examining the CS_ACK & CS_REQ bits. This would ensure that Host
	 * doesn't miss an interrupt for the CS in the race scenario where
	 * whilst Host is servicing an interrupt for the CS, firmware sends
	 * another interrupt for that CS.
	 */
	csg_iface->input->cs_irq_ack = cs_irq_req;

	panthor_fw_update_reqs(csg_iface, req, ack,
			       CSG_SYNC_UPDATE |
			       CSG_IDLE |
			       CSG_PROGRESS_TIMER_EVENT);

	if (csg_events & CSG_IDLE)
		csg_slot_process_idle_event_locked(ptdev, csg_id);

	if (csg_events & CSG_PROGRESS_TIMER_EVENT)
		csg_slot_process_progress_timer_event_locked(ptdev, csg_id);

	cs_irqs = cs_irq_req ^ cs_irq_ack;
	while (cs_irqs) {
		u32 cs_id = ffs(cs_irqs) - 1;

		if (cs_slot_process_irq_locked(ptdev, csg_id, cs_id))
			ring_cs_db_mask |= BIT(cs_id);

		cs_irqs &= ~BIT(cs_id);
	}

	if (csg_events & CSG_SYNC_UPDATE)
		csg_slot_sync_update_locked(ptdev, csg_id);

	if (ring_cs_db_mask)
		panthor_fw_toggle_reqs(csg_iface, doorbell_req, doorbell_ack, ring_cs_db_mask);

	panthor_fw_ring_csg_doorbells(ptdev, BIT(csg_id));
}

static void sched_process_idle_event_locked(struct panthor_device *ptdev)
{
	struct panthor_fw_global_iface *glb_iface = panthor_fw_get_glb_iface(ptdev);

	lockdep_assert_held(&ptdev->scheduler->lock);

	/* Acknowledge the idle event and schedule a tick. */
	panthor_fw_update_reqs(glb_iface, req, glb_iface->output->ack, GLB_IDLE);
	sched_queue_delayed_work(ptdev->scheduler, tick, 0);
}

/**
 * sched_process_global_irq_locked() - Process the scheduling part of a global IRQ
 * @ptdev: Device.
 */
static void sched_process_global_irq_locked(struct panthor_device *ptdev)
{
	struct panthor_fw_global_iface *glb_iface = panthor_fw_get_glb_iface(ptdev);
	u32 req, ack, evts;

	lockdep_assert_held(&ptdev->scheduler->lock);

	req = READ_ONCE(glb_iface->input->req);
	ack = READ_ONCE(glb_iface->output->ack);
	evts = (req ^ ack) & GLB_EVT_MASK;

	if (evts & GLB_IDLE)
		sched_process_idle_event_locked(ptdev);
}

static void process_fw_events_work(struct work_struct *work)
{
	struct panthor_scheduler *sched = container_of(work, struct panthor_scheduler,
						      fw_events_work);
	u32 events = atomic_xchg(&sched->fw_events, 0);
	struct panthor_device *ptdev = sched->ptdev;

	mutex_lock(&sched->lock);

	if (events & JOB_INT_GLOBAL_IF) {
		sched_process_global_irq_locked(ptdev);
		events &= ~JOB_INT_GLOBAL_IF;
	}

	while (events) {
		u32 csg_id = ffs(events) - 1;

		sched_process_csg_irq_locked(ptdev, csg_id);
		events &= ~BIT(csg_id);
	}

	mutex_unlock(&sched->lock);
}

/**
 * panthor_sched_report_fw_events() - Report FW events to the scheduler.
 */
void panthor_sched_report_fw_events(struct panthor_device *ptdev, u32 events)
{
	if (!ptdev->scheduler)
		return;

	atomic_or(events, &ptdev->scheduler->fw_events);
	sched_queue_work(ptdev->scheduler, fw_events);
}

static const char *fence_get_driver_name(struct dma_fence *fence)
{
	return "panthor";
}

static const char *queue_fence_get_timeline_name(struct dma_fence *fence)
{
	return "queue-fence";
}

static const struct dma_fence_ops panthor_queue_fence_ops = {
	.get_driver_name = fence_get_driver_name,
	.get_timeline_name = queue_fence_get_timeline_name,
};

struct panthor_csg_slots_upd_ctx {
	u32 update_mask;
	u32 timedout_mask;
	struct {
		u32 value;
		u32 mask;
	} requests[MAX_CSGS];
};

static void csgs_upd_ctx_init(struct panthor_csg_slots_upd_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
}

static void csgs_upd_ctx_queue_reqs(struct panthor_device *ptdev,
				    struct panthor_csg_slots_upd_ctx *ctx,
				    u32 csg_id, u32 value, u32 mask)
{
	if (drm_WARN_ON(&ptdev->base, !mask) ||
	    drm_WARN_ON(&ptdev->base, csg_id >= ptdev->scheduler->csg_slot_count))
		return;

	ctx->requests[csg_id].value = (ctx->requests[csg_id].value & ~mask) | (value & mask);
	ctx->requests[csg_id].mask |= mask;
	ctx->update_mask |= BIT(csg_id);
}

static int csgs_upd_ctx_apply_locked(struct panthor_device *ptdev,
				     struct panthor_csg_slots_upd_ctx *ctx)
{
	struct panthor_scheduler *sched = ptdev->scheduler;
	u32 update_slots = ctx->update_mask;

	lockdep_assert_held(&sched->lock);

	if (!ctx->update_mask)
		return 0;

	while (update_slots) {
		struct panthor_fw_csg_iface *csg_iface;
		u32 csg_id = ffs(update_slots) - 1;

		update_slots &= ~BIT(csg_id);
		csg_iface = panthor_fw_get_csg_iface(ptdev, csg_id);
		panthor_fw_update_reqs(csg_iface, req,
				       ctx->requests[csg_id].value,
				       ctx->requests[csg_id].mask);
	}

	panthor_fw_ring_csg_doorbells(ptdev, ctx->update_mask);

	update_slots = ctx->update_mask;
	while (update_slots) {
		struct panthor_fw_csg_iface *csg_iface;
		u32 csg_id = ffs(update_slots) - 1;
		u32 req_mask = ctx->requests[csg_id].mask, acked;
		int ret;

		update_slots &= ~BIT(csg_id);
		csg_iface = panthor_fw_get_csg_iface(ptdev, csg_id);

		ret = panthor_fw_csg_wait_acks(ptdev, csg_id, req_mask, &acked, 100);

		if (acked & CSG_ENDPOINT_CONFIG)
			csg_slot_sync_priority_locked(ptdev, csg_id);

		if (acked & CSG_STATE_MASK)
			csg_slot_sync_state_locked(ptdev, csg_id);

		if (acked & CSG_STATUS_UPDATE) {
			csg_slot_sync_queues_state_locked(ptdev, csg_id);
			csg_slot_sync_idle_state_locked(ptdev, csg_id);
		}

		if (ret && acked != req_mask &&
		    ((csg_iface->input->req ^ csg_iface->output->ack) & req_mask) != 0) {
			drm_err(&ptdev->base, "CSG %d update request timedout", csg_id);
			ctx->timedout_mask |= BIT(csg_id);
		}
	}

	if (ctx->timedout_mask)
		return -ETIMEDOUT;

	return 0;
}

struct panthor_sched_tick_ctx {
	struct list_head old_groups[PANTHOR_CSG_PRIORITY_COUNT];
	struct list_head groups[PANTHOR_CSG_PRIORITY_COUNT];
	u32 idle_group_count;
	u32 group_count;
	enum panthor_csg_priority min_priority;
	struct panthor_vm *vms[MAX_CS_PER_CSG];
	u32 as_count;
	bool immediate_tick;
	u32 csg_upd_failed_mask;
};

static bool
tick_ctx_is_full(const struct panthor_scheduler *sched,
		 const struct panthor_sched_tick_ctx *ctx)
{
	return ctx->group_count == sched->csg_slot_count;
}

static bool
group_is_idle(struct panthor_group *group)
{
	struct panthor_device *ptdev = group->ptdev;
	u32 inactive_queues;

	if (group->csg_id >= 0)
		return ptdev->scheduler->csg_slots[group->csg_id].idle;

	inactive_queues = group->idle_queues | group->blocked_queues;
	return hweight32(inactive_queues) == group->queue_count;
}

static bool
group_can_run(struct panthor_group *group)
{
	return group->state != PANTHOR_CS_GROUP_TERMINATED &&
	       group->state != PANTHOR_CS_GROUP_UNKNOWN_STATE &&
	       !group->destroyed && group->fatal_queues == 0 &&
	       !group->timedout;
}

static void
tick_ctx_pick_groups_from_list(const struct panthor_scheduler *sched,
			       struct panthor_sched_tick_ctx *ctx,
			       struct list_head *queue,
			       bool skip_idle_groups,
			       bool owned_by_tick_ctx)
{
	struct panthor_group *group, *tmp;

	if (tick_ctx_is_full(sched, ctx))
		return;

	list_for_each_entry_safe(group, tmp, queue, run_node) {
		u32 i;

		if (!group_can_run(group))
			continue;

		if (skip_idle_groups && group_is_idle(group))
			continue;

		for (i = 0; i < ctx->as_count; i++) {
			if (ctx->vms[i] == group->vm)
				break;
		}

		if (i == ctx->as_count && ctx->as_count == sched->as_slot_count)
			continue;

		if (!owned_by_tick_ctx)
			group_get(group);

		list_move_tail(&group->run_node, &ctx->groups[group->priority]);
		ctx->group_count++;
		if (group_is_idle(group))
			ctx->idle_group_count++;

		if (i == ctx->as_count)
			ctx->vms[ctx->as_count++] = group->vm;

		if (ctx->min_priority > group->priority)
			ctx->min_priority = group->priority;

		if (tick_ctx_is_full(sched, ctx))
			return;
	}
}

static void
tick_ctx_insert_old_group(struct panthor_scheduler *sched,
			  struct panthor_sched_tick_ctx *ctx,
			  struct panthor_group *group,
			  bool full_tick)
{
	struct panthor_csg_slot *csg_slot = &sched->csg_slots[group->csg_id];
	struct panthor_group *other_group;

	if (!full_tick) {
		list_add_tail(&group->run_node, &ctx->old_groups[group->priority]);
		return;
	}

	/* Rotate to make sure groups with lower CSG slot
	 * priorities have a chance to get a higher CSG slot
	 * priority next time they get picked. This priority
	 * has an impact on resource request ordering, so it's
	 * important to make sure we don't let one group starve
	 * all other groups with the same group priority.
	 */
	list_for_each_entry(other_group,
			    &ctx->old_groups[csg_slot->group->priority],
			    run_node) {
		struct panthor_csg_slot *other_csg_slot = &sched->csg_slots[other_group->csg_id];

		if (other_csg_slot->priority > csg_slot->priority) {
			list_add_tail(&csg_slot->group->run_node, &other_group->run_node);
			return;
		}
	}

	list_add_tail(&group->run_node, &ctx->old_groups[group->priority]);
}

static void
tick_ctx_init(struct panthor_scheduler *sched,
	      struct panthor_sched_tick_ctx *ctx,
	      bool full_tick)
{
	struct panthor_device *ptdev = sched->ptdev;
	struct panthor_csg_slots_upd_ctx upd_ctx;
	int ret;
	u32 i;

	memset(ctx, 0, sizeof(*ctx));
	csgs_upd_ctx_init(&upd_ctx);

	ctx->min_priority = PANTHOR_CSG_PRIORITY_COUNT;
	for (i = 0; i < ARRAY_SIZE(ctx->groups); i++) {
		INIT_LIST_HEAD(&ctx->groups[i]);
		INIT_LIST_HEAD(&ctx->old_groups[i]);
	}

	for (i = 0; i < sched->csg_slot_count; i++) {
		struct panthor_csg_slot *csg_slot = &sched->csg_slots[i];
		struct panthor_group *group = csg_slot->group;
		struct panthor_fw_csg_iface *csg_iface;

		if (!group)
			continue;

		csg_iface = panthor_fw_get_csg_iface(ptdev, i);
		group_get(group);

		/* If there was unhandled faults on the VM, force processing of
		 * CSG IRQs, so we can flag the faulty queue.
		 */
		if (panthor_vm_has_unhandled_faults(group->vm)) {
			sched_process_csg_irq_locked(ptdev, i);

			/* No fatal fault reported, flag all queues as faulty. */
			if (!group->fatal_queues)
				group->fatal_queues |= GENMASK(group->queue_count - 1, 0);
		}

		tick_ctx_insert_old_group(sched, ctx, group, full_tick);
		csgs_upd_ctx_queue_reqs(ptdev, &upd_ctx, i,
					csg_iface->output->ack ^ CSG_STATUS_UPDATE,
					CSG_STATUS_UPDATE);
	}

	ret = csgs_upd_ctx_apply_locked(ptdev, &upd_ctx);
	if (ret) {
		panthor_device_schedule_reset(ptdev);
		ctx->csg_upd_failed_mask |= upd_ctx.timedout_mask;
	}
}

static void
group_term_post_processing(struct panthor_group *group)
{
	struct panthor_job *job, *tmp;
	LIST_HEAD(faulty_jobs);
	bool cookie;
	u32 i = 0;

	if (drm_WARN_ON(&group->ptdev->base, group_can_run(group)))
		return;

	cookie = dma_fence_begin_signalling();
	for (i = 0; i < group->queue_count; i++) {
		struct panthor_queue *queue = group->queues[i];
		struct panthor_syncobj_64b *syncobj;
		int err;

		if (group->fatal_queues & BIT(i))
			err = -EINVAL;
		else if (group->timedout)
			err = -ETIMEDOUT;
		else
			err = -ECANCELED;

		if (!queue)
			continue;

		spin_lock(&queue->fence_ctx.lock);
		list_for_each_entry_safe(job, tmp, &queue->fence_ctx.in_flight_jobs, node) {
			list_move_tail(&job->node, &faulty_jobs);
			dma_fence_set_error(job->done_fence, err);
			dma_fence_signal_locked(job->done_fence);
		}
		spin_unlock(&queue->fence_ctx.lock);

		/* Manually update the syncobj seqno to unblock waiters. */
		syncobj = group->syncobjs->kmap + (i * sizeof(*syncobj));
		syncobj->status = ~0;
		syncobj->seqno = atomic64_read(&queue->fence_ctx.seqno);
		sched_queue_work(group->ptdev->scheduler, sync_upd);
	}
	dma_fence_end_signalling(cookie);

	list_for_each_entry_safe(job, tmp, &faulty_jobs, node) {
		list_del_init(&job->node);
		panthor_job_put(&job->base);
	}
}

static void group_term_work(struct work_struct *work)
{
	struct panthor_group *group =
		container_of(work, struct panthor_group, term_work);

	group_term_post_processing(group);
	group_put(group);
}

static void
tick_ctx_cleanup(struct panthor_scheduler *sched,
		 struct panthor_sched_tick_ctx *ctx)
{
	struct panthor_device *ptdev = sched->ptdev;
	struct panthor_group *group, *tmp;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(ctx->old_groups); i++) {
		list_for_each_entry_safe(group, tmp, &ctx->old_groups[i], run_node) {
			/* If everything went fine, we should only have groups
			 * to be terminated in the old_groups lists.
			 */
			drm_WARN_ON(&ptdev->base, !ctx->csg_upd_failed_mask &&
				    group_can_run(group));

			if (!group_can_run(group)) {
				list_del_init(&group->run_node);
				list_del_init(&group->wait_node);
				group_queue_work(group, term);
			} else if (group->csg_id >= 0) {
				list_del_init(&group->run_node);
			} else {
				list_move(&group->run_node,
					  group_is_idle(group) ?
					  &sched->groups.idle[group->priority] :
					  &sched->groups.runnable[group->priority]);
			}
			group_put(group);
		}
	}

	for (i = 0; i < ARRAY_SIZE(ctx->groups); i++) {
		/* If everything went fine, the groups to schedule lists should
		 * be empty.
		 */
		drm_WARN_ON(&ptdev->base,
			    !ctx->csg_upd_failed_mask && !list_empty(&ctx->groups[i]));

		list_for_each_entry_safe(group, tmp, &ctx->groups[i], run_node) {
			if (group->csg_id >= 0) {
				list_del_init(&group->run_node);
			} else {
				list_move(&group->run_node,
					  group_is_idle(group) ?
					  &sched->groups.idle[group->priority] :
					  &sched->groups.runnable[group->priority]);
			}
			group_put(group);
		}
	}
}

static void
tick_ctx_apply(struct panthor_scheduler *sched, struct panthor_sched_tick_ctx *ctx)
{
	struct panthor_group *group, *tmp;
	struct panthor_device *ptdev = sched->ptdev;
	struct panthor_csg_slot *csg_slot;
	int prio, new_csg_prio = MAX_CSG_PRIO, i;
	u32 free_csg_slots = 0;
	struct panthor_csg_slots_upd_ctx upd_ctx;
	int ret;

	csgs_upd_ctx_init(&upd_ctx);

	for (prio = PANTHOR_CSG_PRIORITY_COUNT - 1; prio >= 0; prio--) {
		/* Suspend or terminate evicted groups. */
		list_for_each_entry(group, &ctx->old_groups[prio], run_node) {
			bool term = !group_can_run(group);
			int csg_id = group->csg_id;

			if (drm_WARN_ON(&ptdev->base, csg_id < 0))
				continue;

			csg_slot = &sched->csg_slots[csg_id];
			csgs_upd_ctx_queue_reqs(ptdev, &upd_ctx, csg_id,
						term ? CSG_STATE_TERMINATE : CSG_STATE_SUSPEND,
						CSG_STATE_MASK);
		}

		/* Update priorities on already running groups. */
		list_for_each_entry(group, &ctx->groups[prio], run_node) {
			struct panthor_fw_csg_iface *csg_iface;
			int csg_id = group->csg_id;

			if (csg_id < 0) {
				new_csg_prio--;
				continue;
			}

			csg_slot = &sched->csg_slots[csg_id];
			csg_iface = panthor_fw_get_csg_iface(ptdev, csg_id);
			if (csg_slot->priority == new_csg_prio) {
				new_csg_prio--;
				continue;
			}

			panthor_fw_update_reqs(csg_iface, endpoint_req,
					       CSG_EP_REQ_PRIORITY(new_csg_prio),
					       CSG_EP_REQ_PRIORITY_MASK);
			csgs_upd_ctx_queue_reqs(ptdev, &upd_ctx, csg_id,
						csg_iface->output->ack ^ CSG_ENDPOINT_CONFIG,
						CSG_ENDPOINT_CONFIG);
			new_csg_prio--;
		}
	}

	ret = csgs_upd_ctx_apply_locked(ptdev, &upd_ctx);
	if (ret) {
		panthor_device_schedule_reset(ptdev);
		ctx->csg_upd_failed_mask |= upd_ctx.timedout_mask;
		return;
	}

	/* Unbind evicted groups. */
	for (prio = PANTHOR_CSG_PRIORITY_COUNT - 1; prio >= 0; prio--) {
		list_for_each_entry(group, &ctx->old_groups[prio], run_node) {
			/* This group is gone. Process interrupts to clear
			 * any pending interrupts before we start the new
			 * group.
			 */
			if (group->csg_id >= 0)
				sched_process_csg_irq_locked(ptdev, group->csg_id);

			group_unbind_locked(group);
		}
	}

	for (i = 0; i < sched->csg_slot_count; i++) {
		if (!sched->csg_slots[i].group)
			free_csg_slots |= BIT(i);
	}

	csgs_upd_ctx_init(&upd_ctx);
	new_csg_prio = MAX_CSG_PRIO;

	/* Start new groups. */
	for (prio = PANTHOR_CSG_PRIORITY_COUNT - 1; prio >= 0; prio--) {
		list_for_each_entry(group, &ctx->groups[prio], run_node) {
			int csg_id = group->csg_id;
			struct panthor_fw_csg_iface *csg_iface;

			if (csg_id >= 0) {
				new_csg_prio--;
				continue;
			}

			csg_id = ffs(free_csg_slots) - 1;
			if (drm_WARN_ON(&ptdev->base, csg_id < 0))
				break;

			csg_iface = panthor_fw_get_csg_iface(ptdev, csg_id);
			csg_slot = &sched->csg_slots[csg_id];
			group_bind_locked(group, csg_id);
			csg_slot_prog_locked(ptdev, csg_id, new_csg_prio--);
			csgs_upd_ctx_queue_reqs(ptdev, &upd_ctx, csg_id,
						group->state == PANTHOR_CS_GROUP_SUSPENDED ?
						CSG_STATE_RESUME : CSG_STATE_START,
						CSG_STATE_MASK);
			csgs_upd_ctx_queue_reqs(ptdev, &upd_ctx, csg_id,
						csg_iface->output->ack ^ CSG_ENDPOINT_CONFIG,
						CSG_ENDPOINT_CONFIG);
			free_csg_slots &= ~BIT(csg_id);
		}
	}

	ret = csgs_upd_ctx_apply_locked(ptdev, &upd_ctx);
	if (ret) {
		panthor_device_schedule_reset(ptdev);
		ctx->csg_upd_failed_mask |= upd_ctx.timedout_mask;
		return;
	}

	for (prio = PANTHOR_CSG_PRIORITY_COUNT - 1; prio >= 0; prio--) {
		list_for_each_entry_safe(group, tmp, &ctx->groups[prio], run_node) {
			list_del_init(&group->run_node);

			/* If the group has been destroyed while we were
			 * scheduling, ask for an immediate tick to
			 * re-evaluate as soon as possible and get rid of
			 * this dangling group.
			 */
			if (group->destroyed)
				ctx->immediate_tick = true;
			group_put(group);
		}

		/* Return evicted groups to the idle or run queues. Groups
		 * that can no longer be run (because they've been destroyed
		 * or experienced an unrecoverable error) will be scheduled
		 * for destruction in tick_ctx_cleanup().
		 */
		list_for_each_entry_safe(group, tmp, &ctx->old_groups[prio], run_node) {
			if (!group_can_run(group))
				continue;

			if (group_is_idle(group))
				list_move_tail(&group->run_node, &sched->groups.idle[prio]);
			else
				list_move_tail(&group->run_node, &sched->groups.runnable[prio]);
			group_put(group);
		}
	}

	sched->used_csg_slot_count = ctx->group_count;
	sched->might_have_idle_groups = ctx->idle_group_count > 0;
}

static u64
tick_ctx_update_resched_target(struct panthor_scheduler *sched,
			       const struct panthor_sched_tick_ctx *ctx)
{
	/* We had space left, no need to reschedule until some external event happens. */
	if (!tick_ctx_is_full(sched, ctx))
		goto no_tick;

	/* If idle groups were scheduled, no need to wake up until some external
	 * event happens (group unblocked, new job submitted, ...).
	 */
	if (ctx->idle_group_count)
		goto no_tick;

	if (drm_WARN_ON(&sched->ptdev->base, ctx->min_priority >= PANTHOR_CSG_PRIORITY_COUNT))
		goto no_tick;

	/* If there are groups of the same priority waiting, we need to
	 * keep the scheduler ticking, otherwise, we'll just wait for
	 * new groups with higher priority to be queued.
	 */
	if (!list_empty(&sched->groups.runnable[ctx->min_priority])) {
		u64 resched_target = sched->last_tick + sched->tick_period;

		if (time_before64(sched->resched_target, sched->last_tick) ||
		    time_before64(resched_target, sched->resched_target))
			sched->resched_target = resched_target;

		return sched->resched_target - sched->last_tick;
	}

no_tick:
	sched->resched_target = U64_MAX;
	return U64_MAX;
}

static void tick_work(struct work_struct *work)
{
	struct panthor_scheduler *sched = container_of(work, struct panthor_scheduler,
						      tick_work.work);
	struct panthor_device *ptdev = sched->ptdev;
	struct panthor_sched_tick_ctx ctx;
	u64 remaining_jiffies = 0, resched_delay;
	u64 now = get_jiffies_64();
	int prio, ret, cookie;

	if (!drm_dev_enter(&ptdev->base, &cookie))
		return;

	ret = panthor_device_resume_and_get(ptdev);
	if (drm_WARN_ON(&ptdev->base, ret))
		goto out_dev_exit;

	if (time_before64(now, sched->resched_target))
		remaining_jiffies = sched->resched_target - now;

	mutex_lock(&sched->lock);
	if (panthor_device_reset_is_pending(sched->ptdev))
		goto out_unlock;

	tick_ctx_init(sched, &ctx, remaining_jiffies != 0);
	if (ctx.csg_upd_failed_mask)
		goto out_cleanup_ctx;

	if (remaining_jiffies) {
		/* Scheduling forced in the middle of a tick. Only RT groups
		 * can preempt non-RT ones. Currently running RT groups can't be
		 * preempted.
		 */
		for (prio = PANTHOR_CSG_PRIORITY_COUNT - 1;
		     prio >= 0 && !tick_ctx_is_full(sched, &ctx);
		     prio--) {
			tick_ctx_pick_groups_from_list(sched, &ctx, &ctx.old_groups[prio],
						       true, true);
			if (prio == PANTHOR_CSG_PRIORITY_RT) {
				tick_ctx_pick_groups_from_list(sched, &ctx,
							       &sched->groups.runnable[prio],
							       true, false);
			}
		}
	}

	/* First pick non-idle groups */
	for (prio = PANTHOR_CSG_PRIORITY_COUNT - 1;
	     prio >= 0 && !tick_ctx_is_full(sched, &ctx);
	     prio--) {
		tick_ctx_pick_groups_from_list(sched, &ctx, &sched->groups.runnable[prio],
					       true, false);
		tick_ctx_pick_groups_from_list(sched, &ctx, &ctx.old_groups[prio], true, true);
	}

	/* If we have free CSG slots left, pick idle groups */
	for (prio = PANTHOR_CSG_PRIORITY_COUNT - 1;
	     prio >= 0 && !tick_ctx_is_full(sched, &ctx);
	     prio--) {
		/* Check the old_group queue first to avoid reprogramming the slots */
		tick_ctx_pick_groups_from_list(sched, &ctx, &ctx.old_groups[prio], false, true);
		tick_ctx_pick_groups_from_list(sched, &ctx, &sched->groups.idle[prio],
					       false, false);
	}

	tick_ctx_apply(sched, &ctx);
	if (ctx.csg_upd_failed_mask)
		goto out_cleanup_ctx;

	if (ctx.idle_group_count == ctx.group_count) {
		panthor_devfreq_record_idle(sched->ptdev);
		if (sched->pm.has_ref) {
			pm_runtime_put_autosuspend(ptdev->base.dev);
			sched->pm.has_ref = false;
		}
	} else {
		panthor_devfreq_record_busy(sched->ptdev);
		if (!sched->pm.has_ref) {
			pm_runtime_get(ptdev->base.dev);
			sched->pm.has_ref = true;
		}
	}

	sched->last_tick = now;
	resched_delay = tick_ctx_update_resched_target(sched, &ctx);
	if (ctx.immediate_tick)
		resched_delay = 0;

	if (resched_delay != U64_MAX)
		sched_queue_delayed_work(sched, tick, resched_delay);

out_cleanup_ctx:
	tick_ctx_cleanup(sched, &ctx);

out_unlock:
	mutex_unlock(&sched->lock);
	pm_runtime_mark_last_busy(ptdev->base.dev);
	pm_runtime_put_autosuspend(ptdev->base.dev);

out_dev_exit:
	drm_dev_exit(cookie);
}

static int panthor_queue_eval_syncwait(struct panthor_group *group, u8 queue_idx)
{
	struct panthor_queue *queue = group->queues[queue_idx];
	union {
		struct panthor_syncobj_64b sync64;
		struct panthor_syncobj_32b sync32;
	} *syncobj;
	bool result;
	u64 value;

	syncobj = panthor_queue_get_syncwait_obj(group, queue);
	if (!syncobj)
		return -EINVAL;

	value = queue->syncwait.sync64 ?
		syncobj->sync64.seqno :
		syncobj->sync32.seqno;

	if (queue->syncwait.gt)
		result = value > queue->syncwait.ref;
	else
		result = value <= queue->syncwait.ref;

	if (result)
		panthor_queue_put_syncwait_obj(queue);

	return result;
}

static void sync_upd_work(struct work_struct *work)
{
	struct panthor_scheduler *sched = container_of(work,
						      struct panthor_scheduler,
						      sync_upd_work);
	struct panthor_group *group, *tmp;
	bool immediate_tick = false;

	mutex_lock(&sched->lock);
	list_for_each_entry_safe(group, tmp, &sched->groups.waiting, wait_node) {
		u32 tested_queues = group->blocked_queues;
		u32 unblocked_queues = 0;

		while (tested_queues) {
			u32 cs_id = ffs(tested_queues) - 1;
			int ret;

			ret = panthor_queue_eval_syncwait(group, cs_id);
			drm_WARN_ON(&group->ptdev->base, ret < 0);
			if (ret)
				unblocked_queues |= BIT(cs_id);

			tested_queues &= ~BIT(cs_id);
		}

		if (unblocked_queues) {
			group->blocked_queues &= ~unblocked_queues;

			if (group->csg_id < 0) {
				list_move(&group->run_node,
					  &sched->groups.runnable[group->priority]);
				if (group->priority == PANTHOR_CSG_PRIORITY_RT)
					immediate_tick = true;
			}
		}

		if (!group->blocked_queues)
			list_del_init(&group->wait_node);
	}
	mutex_unlock(&sched->lock);

	if (immediate_tick)
		sched_queue_delayed_work(sched, tick, 0);
}

static void group_schedule_locked(struct panthor_group *group, u32 queue_mask)
{
	struct panthor_device *ptdev = group->ptdev;
	struct panthor_scheduler *sched = ptdev->scheduler;
	struct list_head *queue = &sched->groups.runnable[group->priority];
	u64 delay_jiffies = 0;
	bool was_idle;
	u64 now;

	if (!group_can_run(group))
		return;

	/* All updated queues are blocked, no need to wake up the scheduler. */
	if ((queue_mask & group->blocked_queues) == queue_mask)
		return;

	was_idle = group_is_idle(group);
	group->idle_queues &= ~queue_mask;

	/* Don't mess up with the lists if we're in a middle of a reset. */
	if (atomic_read(&sched->reset.in_progress))
		return;

	if (was_idle && !group_is_idle(group))
		list_move_tail(&group->run_node, queue);

	/* RT groups are preemptive. */
	if (group->priority == PANTHOR_CSG_PRIORITY_RT) {
		sched_queue_delayed_work(sched, tick, 0);
		return;
	}

	/* Some groups might be idle, force an immediate tick to
	 * re-evaluate.
	 */
	if (sched->might_have_idle_groups) {
		sched_queue_delayed_work(sched, tick, 0);
		return;
	}

	/* Scheduler is ticking, nothing to do. */
	if (sched->resched_target != U64_MAX) {
		/* If there are free slots, force immediating ticking. */
		if (sched->used_csg_slot_count < sched->csg_slot_count)
			sched_queue_delayed_work(sched, tick, 0);

		return;
	}

	/* Scheduler tick was off, recalculate the resched_target based on the
	 * last tick event, and queue the scheduler work.
	 */
	now = get_jiffies_64();
	sched->resched_target = sched->last_tick + sched->tick_period;
	if (sched->used_csg_slot_count == sched->csg_slot_count &&
	    time_before64(now, sched->resched_target))
		delay_jiffies = min_t(unsigned long, sched->resched_target - now, ULONG_MAX);

	sched_queue_delayed_work(sched, tick, delay_jiffies);
}

static void queue_stop(struct panthor_queue *queue,
		       struct panthor_job *bad_job)
{
	drm_sched_stop(&queue->scheduler, bad_job ? &bad_job->base : NULL);
}

static void queue_start(struct panthor_queue *queue)
{
	struct panthor_job *job;

	/* Re-assign the parent fences. */
	list_for_each_entry(job, &queue->scheduler.pending_list, base.list)
		job->base.s_fence->parent = dma_fence_get(job->done_fence);

	drm_sched_start(&queue->scheduler, 0);
}

static void panthor_group_stop(struct panthor_group *group)
{
	struct panthor_scheduler *sched = group->ptdev->scheduler;

	lockdep_assert_held(&sched->reset.lock);

	for (u32 i = 0; i < group->queue_count; i++)
		queue_stop(group->queues[i], NULL);

	group_get(group);
	list_move_tail(&group->run_node, &sched->reset.stopped_groups);
}

static void panthor_group_start(struct panthor_group *group)
{
	struct panthor_scheduler *sched = group->ptdev->scheduler;

	lockdep_assert_held(&group->ptdev->scheduler->reset.lock);

	for (u32 i = 0; i < group->queue_count; i++)
		queue_start(group->queues[i]);

	if (group_can_run(group)) {
		list_move_tail(&group->run_node,
			       group_is_idle(group) ?
			       &sched->groups.idle[group->priority] :
			       &sched->groups.runnable[group->priority]);
	} else {
		list_del_init(&group->run_node);
		list_del_init(&group->wait_node);
		group_queue_work(group, term);
	}

	group_put(group);
}

static void panthor_sched_immediate_tick(struct panthor_device *ptdev)
{
	struct panthor_scheduler *sched = ptdev->scheduler;

	sched_queue_delayed_work(sched, tick, 0);
}

/**
 * panthor_sched_report_mmu_fault() - Report MMU faults to the scheduler.
 */
void panthor_sched_report_mmu_fault(struct panthor_device *ptdev)
{
	/* Force a tick to immediately kill faulty groups. */
	if (ptdev->scheduler)
		panthor_sched_immediate_tick(ptdev);
}

void panthor_sched_resume(struct panthor_device *ptdev)
{
	/* Force a tick to re-evaluate after a resume. */
	panthor_sched_immediate_tick(ptdev);
}

void panthor_sched_suspend(struct panthor_device *ptdev)
{
	struct panthor_scheduler *sched = ptdev->scheduler;
	struct panthor_csg_slots_upd_ctx upd_ctx;
	struct panthor_group *group;
	u32 suspended_slots;
	u32 i;

	mutex_lock(&sched->lock);
	csgs_upd_ctx_init(&upd_ctx);
	for (i = 0; i < sched->csg_slot_count; i++) {
		struct panthor_csg_slot *csg_slot = &sched->csg_slots[i];

		if (csg_slot->group) {
			csgs_upd_ctx_queue_reqs(ptdev, &upd_ctx, i,
						group_can_run(csg_slot->group) ?
						CSG_STATE_SUSPEND : CSG_STATE_TERMINATE,
						CSG_STATE_MASK);
		}
	}

	suspended_slots = upd_ctx.update_mask;

	csgs_upd_ctx_apply_locked(ptdev, &upd_ctx);
	suspended_slots &= ~upd_ctx.timedout_mask;

	if (upd_ctx.timedout_mask) {
		u32 slot_mask = upd_ctx.timedout_mask;

		drm_err(&ptdev->base, "CSG suspend failed, escalating to termination");
		csgs_upd_ctx_init(&upd_ctx);
		while (slot_mask) {
			u32 csg_id = ffs(slot_mask) - 1;
			struct panthor_csg_slot *csg_slot = &sched->csg_slots[csg_id];

			/* If the group was still usable before that point, we consider
			 * it innocent.
			 */
			if (group_can_run(csg_slot->group))
				csg_slot->group->innocent = true;

			/* We consider group suspension failures as fatal and flag the
			 * group as unusable by setting timedout=true.
			 */
			csg_slot->group->timedout = true;

			csgs_upd_ctx_queue_reqs(ptdev, &upd_ctx, csg_id,
						CSG_STATE_TERMINATE,
						CSG_STATE_MASK);
			slot_mask &= ~BIT(csg_id);
		}

		csgs_upd_ctx_apply_locked(ptdev, &upd_ctx);

		slot_mask = upd_ctx.timedout_mask;
		while (slot_mask) {
			u32 csg_id = ffs(slot_mask) - 1;
			struct panthor_csg_slot *csg_slot = &sched->csg_slots[csg_id];

			/* Terminate command timedout, but the soft-reset will
			 * automatically terminate all active groups, so let's
			 * force the state to halted here.
			 */
			if (csg_slot->group->state != PANTHOR_CS_GROUP_TERMINATED)
				csg_slot->group->state = PANTHOR_CS_GROUP_TERMINATED;
			slot_mask &= ~BIT(csg_id);
		}
	}

	/* Flush L2 and LSC caches to make sure suspend state is up-to-date.
	 * If the flush fails, flag all queues for termination.
	 */
	if (suspended_slots) {
		bool flush_caches_failed = false;
		u32 slot_mask = suspended_slots;

		if (panthor_gpu_flush_caches(ptdev, CACHE_CLEAN, CACHE_CLEAN, 0))
			flush_caches_failed = true;

		while (slot_mask) {
			u32 csg_id = ffs(slot_mask) - 1;
			struct panthor_csg_slot *csg_slot = &sched->csg_slots[csg_id];

			if (flush_caches_failed)
				csg_slot->group->state = PANTHOR_CS_GROUP_TERMINATED;
			else
				csg_slot_sync_update_locked(ptdev, csg_id);

			slot_mask &= ~BIT(csg_id);
		}
	}

	for (i = 0; i < sched->csg_slot_count; i++) {
		struct panthor_csg_slot *csg_slot = &sched->csg_slots[i];

		group = csg_slot->group;
		if (!group)
			continue;

		group_get(group);

		if (group->csg_id >= 0)
			sched_process_csg_irq_locked(ptdev, group->csg_id);

		group_unbind_locked(group);

		drm_WARN_ON(&group->ptdev->base, !list_empty(&group->run_node));

		if (group_can_run(group)) {
			list_add(&group->run_node,
				 &sched->groups.idle[group->priority]);
		} else {
			/* We don't bother stopping the scheduler if the group is
			 * faulty, the group termination work will finish the job.
			 */
			list_del_init(&group->wait_node);
			group_queue_work(group, term);
		}
		group_put(group);
	}
	mutex_unlock(&sched->lock);
}

void panthor_sched_pre_reset(struct panthor_device *ptdev)
{
	struct panthor_scheduler *sched = ptdev->scheduler;
	struct panthor_group *group, *group_tmp;
	u32 i;

	mutex_lock(&sched->reset.lock);
	atomic_set(&sched->reset.in_progress, true);

	/* Cancel all scheduler works. Once this is done, these works can't be
	 * scheduled again until the reset operation is complete.
	 */
	cancel_work_sync(&sched->sync_upd_work);
	cancel_delayed_work_sync(&sched->tick_work);

	panthor_sched_suspend(ptdev);

	/* Stop all groups that might still accept jobs, so we don't get passed
	 * new jobs while we're resetting.
	 */
	for (i = 0; i < ARRAY_SIZE(sched->groups.runnable); i++) {
		/* All groups should be in the idle lists. */
		drm_WARN_ON(&ptdev->base, !list_empty(&sched->groups.runnable[i]));
		list_for_each_entry_safe(group, group_tmp, &sched->groups.runnable[i], run_node)
			panthor_group_stop(group);
	}

	for (i = 0; i < ARRAY_SIZE(sched->groups.idle); i++) {
		list_for_each_entry_safe(group, group_tmp, &sched->groups.idle[i], run_node)
			panthor_group_stop(group);
	}

	mutex_unlock(&sched->reset.lock);
}

void panthor_sched_post_reset(struct panthor_device *ptdev, bool reset_failed)
{
	struct panthor_scheduler *sched = ptdev->scheduler;
	struct panthor_group *group, *group_tmp;

	mutex_lock(&sched->reset.lock);

	list_for_each_entry_safe(group, group_tmp, &sched->reset.stopped_groups, run_node) {
		/* Consider all previously running group as terminated if the
		 * reset failed.
		 */
		if (reset_failed)
			group->state = PANTHOR_CS_GROUP_TERMINATED;

		panthor_group_start(group);
	}

	/* We're done resetting the GPU, clear the reset.in_progress bit so we can
	 * kick the scheduler.
	 */
	atomic_set(&sched->reset.in_progress, false);
	mutex_unlock(&sched->reset.lock);

	/* No need to queue a tick and update syncs if the reset failed. */
	if (!reset_failed) {
		sched_queue_delayed_work(sched, tick, 0);
		sched_queue_work(sched, sync_upd);
	}
}

static void update_fdinfo_stats(struct panthor_job *job)
{
	struct panthor_group *group = job->group;
	struct panthor_queue *queue = group->queues[job->queue_idx];
	struct panthor_gpu_usage *fdinfo = &group->fdinfo.data;
	struct panthor_job_profiling_data *slots = queue->profiling.slots->kmap;
	struct panthor_job_profiling_data *data = &slots[job->profiling.slot];

	scoped_guard(spinlock, &group->fdinfo.lock) {
		if (job->profiling.mask & PANTHOR_DEVICE_PROFILING_CYCLES)
			fdinfo->cycles += data->cycles.after - data->cycles.before;
		if (job->profiling.mask & PANTHOR_DEVICE_PROFILING_TIMESTAMP)
			fdinfo->time += data->time.after - data->time.before;
	}
}

void panthor_fdinfo_gather_group_samples(struct panthor_file *pfile)
{
	struct panthor_group_pool *gpool = pfile->groups;
	struct panthor_group *group;
	unsigned long i;

	if (IS_ERR_OR_NULL(gpool))
		return;

	xa_lock(&gpool->xa);
	xa_for_each(&gpool->xa, i, group) {
		guard(spinlock)(&group->fdinfo.lock);
		pfile->stats.cycles += group->fdinfo.data.cycles;
		pfile->stats.time += group->fdinfo.data.time;
		group->fdinfo.data.cycles = 0;
		group->fdinfo.data.time = 0;
	}
	xa_unlock(&gpool->xa);
}

static void group_sync_upd_work(struct work_struct *work)
{
	struct panthor_group *group =
		container_of(work, struct panthor_group, sync_upd_work);
	struct panthor_job *job, *job_tmp;
	LIST_HEAD(done_jobs);
	u32 queue_idx;
	bool cookie;

	cookie = dma_fence_begin_signalling();
	for (queue_idx = 0; queue_idx < group->queue_count; queue_idx++) {
		struct panthor_queue *queue = group->queues[queue_idx];
		struct panthor_syncobj_64b *syncobj;

		if (!queue)
			continue;

		syncobj = group->syncobjs->kmap + (queue_idx * sizeof(*syncobj));

		spin_lock(&queue->fence_ctx.lock);
		list_for_each_entry_safe(job, job_tmp, &queue->fence_ctx.in_flight_jobs, node) {
			if (syncobj->seqno < job->done_fence->seqno)
				break;

			list_move_tail(&job->node, &done_jobs);
			dma_fence_signal_locked(job->done_fence);
		}
		spin_unlock(&queue->fence_ctx.lock);
	}
	dma_fence_end_signalling(cookie);

	list_for_each_entry_safe(job, job_tmp, &done_jobs, node) {
		if (job->profiling.mask)
			update_fdinfo_stats(job);
		list_del_init(&job->node);
		panthor_job_put(&job->base);
	}

	group_put(group);
}

struct panthor_job_ringbuf_instrs {
	u64 buffer[MAX_INSTRS_PER_JOB];
	u32 count;
};

struct panthor_job_instr {
	u32 profile_mask;
	u64 instr;
};

#define JOB_INSTR(__prof, __instr) \
	{ \
		.profile_mask = __prof, \
		.instr = __instr, \
	}

static void
copy_instrs_to_ringbuf(struct panthor_queue *queue,
		       struct panthor_job *job,
		       struct panthor_job_ringbuf_instrs *instrs)
{
	u64 ringbuf_size = panthor_kernel_bo_size(queue->ringbuf);
	u64 start = job->ringbuf.start & (ringbuf_size - 1);
	u64 size, written;

	/*
	 * We need to write a whole slot, including any trailing zeroes
	 * that may come at the end of it. Also, because instrs.buffer has
	 * been zero-initialised, there's no need to pad it with 0's
	 */
	instrs->count = ALIGN(instrs->count, NUM_INSTRS_PER_CACHE_LINE);
	size = instrs->count * sizeof(u64);
	WARN_ON(size > ringbuf_size);
	written = min(ringbuf_size - start, size);

	memcpy(queue->ringbuf->kmap + start, instrs->buffer, written);

	if (written < size)
		memcpy(queue->ringbuf->kmap,
		       &instrs->buffer[written / sizeof(u64)],
		       size - written);
}

struct panthor_job_cs_params {
	u32 profile_mask;
	u64 addr_reg; u64 val_reg;
	u64 cycle_reg; u64 time_reg;
	u64 sync_addr; u64 times_addr;
	u64 cs_start; u64 cs_size;
	u32 last_flush; u32 waitall_mask;
};

static void
get_job_cs_params(struct panthor_job *job, struct panthor_job_cs_params *params)
{
	struct panthor_group *group = job->group;
	struct panthor_queue *queue = group->queues[job->queue_idx];
	struct panthor_device *ptdev = group->ptdev;
	struct panthor_scheduler *sched = ptdev->scheduler;

	params->addr_reg = ptdev->csif_info.cs_reg_count -
			   ptdev->csif_info.unpreserved_cs_reg_count;
	params->val_reg = params->addr_reg + 2;
	params->cycle_reg = params->addr_reg;
	params->time_reg = params->val_reg;

	params->sync_addr = panthor_kernel_bo_gpuva(group->syncobjs) +
			    job->queue_idx * sizeof(struct panthor_syncobj_64b);
	params->times_addr = panthor_kernel_bo_gpuva(queue->profiling.slots) +
			     (job->profiling.slot * sizeof(struct panthor_job_profiling_data));
	params->waitall_mask = GENMASK(sched->sb_slot_count - 1, 0);

	params->cs_start = job->call_info.start;
	params->cs_size = job->call_info.size;
	params->last_flush = job->call_info.latest_flush;

	params->profile_mask = job->profiling.mask;
}

#define JOB_INSTR_ALWAYS(instr) \
	JOB_INSTR(PANTHOR_DEVICE_PROFILING_DISABLED, (instr))
#define JOB_INSTR_TIMESTAMP(instr) \
	JOB_INSTR(PANTHOR_DEVICE_PROFILING_TIMESTAMP, (instr))
#define JOB_INSTR_CYCLES(instr) \
	JOB_INSTR(PANTHOR_DEVICE_PROFILING_CYCLES, (instr))

static void
prepare_job_instrs(const struct panthor_job_cs_params *params,
		   struct panthor_job_ringbuf_instrs *instrs)
{
	const struct panthor_job_instr instr_seq[] = {
		/* MOV32 rX+2, cs.latest_flush */
		JOB_INSTR_ALWAYS((2ull << 56) | (params->val_reg << 48) | params->last_flush),
		/* FLUSH_CACHE2.clean_inv_all.no_wait.signal(0) rX+2 */
		JOB_INSTR_ALWAYS((36ull << 56) | (0ull << 48) | (params->val_reg << 40) |
				 (0 << 16) | 0x233),
		/* MOV48 rX:rX+1, cycles_offset */
		JOB_INSTR_CYCLES((1ull << 56) | (params->cycle_reg << 48) |
				 (params->times_addr +
				  offsetof(struct panthor_job_profiling_data, cycles.before))),
		/* STORE_STATE cycles */
		JOB_INSTR_CYCLES((40ull << 56) | (params->cycle_reg << 40) | (1ll << 32)),
		/* MOV48 rX:rX+1, time_offset */
		JOB_INSTR_TIMESTAMP((1ull << 56) | (params->time_reg << 48) |
				    (params->times_addr +
				     offsetof(struct panthor_job_profiling_data, time.before))),
		/* STORE_STATE timer */
		JOB_INSTR_TIMESTAMP((40ull << 56) | (params->time_reg << 40) | (0ll << 32)),
		/* MOV48 rX:rX+1, cs.start */
		JOB_INSTR_ALWAYS((1ull << 56) | (params->addr_reg << 48) | params->cs_start),
		/* MOV32 rX+2, cs.size */
		JOB_INSTR_ALWAYS((2ull << 56) | (params->val_reg << 48) | params->cs_size),
		/* WAIT(0) => waits for FLUSH_CACHE2 instruction */
		JOB_INSTR_ALWAYS((3ull << 56) | (1 << 16)),
		/* CALL rX:rX+1, rX+2 */
		JOB_INSTR_ALWAYS((32ull << 56) | (params->addr_reg << 40) |
				 (params->val_reg << 32)),
		/* MOV48 rX:rX+1, cycles_offset */
		JOB_INSTR_CYCLES((1ull << 56) | (params->cycle_reg << 48) |
				 (params->times_addr +
				  offsetof(struct panthor_job_profiling_data, cycles.after))),
		/* STORE_STATE cycles */
		JOB_INSTR_CYCLES((40ull << 56) | (params->cycle_reg << 40) | (1ll << 32)),
		/* MOV48 rX:rX+1, time_offset */
		JOB_INSTR_TIMESTAMP((1ull << 56) | (params->time_reg << 48) |
			  (params->times_addr +
			   offsetof(struct panthor_job_profiling_data, time.after))),
		/* STORE_STATE timer */
		JOB_INSTR_TIMESTAMP((40ull << 56) | (params->time_reg << 40) | (0ll << 32)),
		/* MOV48 rX:rX+1, sync_addr */
		JOB_INSTR_ALWAYS((1ull << 56) | (params->addr_reg << 48) | params->sync_addr),
		/* MOV48 rX+2, #1 */
		JOB_INSTR_ALWAYS((1ull << 56) | (params->val_reg << 48) | 1),
		/* WAIT(all) */
		JOB_INSTR_ALWAYS((3ull << 56) | (params->waitall_mask << 16)),
		/* SYNC_ADD64.system_scope.propage_err.nowait rX:rX+1, rX+2*/
		JOB_INSTR_ALWAYS((51ull << 56) | (0ull << 48) | (params->addr_reg << 40) |
				 (params->val_reg << 32) | (0 << 16) | 1),
		/* ERROR_BARRIER, so we can recover from faults at job boundaries. */
		JOB_INSTR_ALWAYS((47ull << 56)),
	};
	u32 pad;

	instrs->count = 0;

	/* NEED to be cacheline aligned to please the prefetcher. */
	static_assert(sizeof(instrs->buffer) % 64 == 0,
		      "panthor_job_ringbuf_instrs::buffer is not aligned on a cacheline");

	/* Make sure we have enough storage to store the whole sequence. */
	static_assert(ALIGN(ARRAY_SIZE(instr_seq), NUM_INSTRS_PER_CACHE_LINE) ==
		      ARRAY_SIZE(instrs->buffer),
		      "instr_seq vs panthor_job_ringbuf_instrs::buffer size mismatch");

	for (u32 i = 0; i < ARRAY_SIZE(instr_seq); i++) {
		/* If the profile mask of this instruction is not enabled, skip it. */
		if (instr_seq[i].profile_mask &&
		    !(instr_seq[i].profile_mask & params->profile_mask))
			continue;

		instrs->buffer[instrs->count++] = instr_seq[i].instr;
	}

	pad = ALIGN(instrs->count, NUM_INSTRS_PER_CACHE_LINE);
	memset(&instrs->buffer[instrs->count], 0,
	       (pad - instrs->count) * sizeof(instrs->buffer[0]));
	instrs->count = pad;
}

static u32 calc_job_credits(u32 profile_mask)
{
	struct panthor_job_ringbuf_instrs instrs;
	struct panthor_job_cs_params params = {
		.profile_mask = profile_mask,
	};

	prepare_job_instrs(&params, &instrs);
	return instrs.count;
}

static struct dma_fence *
queue_run_job(struct drm_sched_job *sched_job)
{
	struct panthor_job *job = container_of(sched_job, struct panthor_job, base);
	struct panthor_group *group = job->group;
	struct panthor_queue *queue = group->queues[job->queue_idx];
	struct panthor_device *ptdev = group->ptdev;
	struct panthor_scheduler *sched = ptdev->scheduler;
	struct panthor_job_ringbuf_instrs instrs;
	struct panthor_job_cs_params cs_params;
	struct dma_fence *done_fence;
	int ret;

	/* Stream size is zero, nothing to do except making sure all previously
	 * submitted jobs are done before we signal the
	 * drm_sched_job::s_fence::finished fence.
	 */
	if (!job->call_info.size) {
		job->done_fence = dma_fence_get(queue->fence_ctx.last_fence);
		return dma_fence_get(job->done_fence);
	}

	ret = panthor_device_resume_and_get(ptdev);
	if (drm_WARN_ON(&ptdev->base, ret))
		return ERR_PTR(ret);

	mutex_lock(&sched->lock);
	if (!group_can_run(group)) {
		done_fence = ERR_PTR(-ECANCELED);
		goto out_unlock;
	}

	dma_fence_init(job->done_fence,
		       &panthor_queue_fence_ops,
		       &queue->fence_ctx.lock,
		       queue->fence_ctx.id,
		       atomic64_inc_return(&queue->fence_ctx.seqno));

	job->profiling.slot = queue->profiling.seqno++;
	if (queue->profiling.seqno == queue->profiling.slot_count)
		queue->profiling.seqno = 0;

	job->ringbuf.start = queue->iface.input->insert;

	get_job_cs_params(job, &cs_params);
	prepare_job_instrs(&cs_params, &instrs);
	copy_instrs_to_ringbuf(queue, job, &instrs);

	job->ringbuf.end = job->ringbuf.start + (instrs.count * sizeof(u64));

	panthor_job_get(&job->base);
	spin_lock(&queue->fence_ctx.lock);
	list_add_tail(&job->node, &queue->fence_ctx.in_flight_jobs);
	spin_unlock(&queue->fence_ctx.lock);

	/* Make sure the ring buffer is updated before the INSERT
	 * register.
	 */
	wmb();

	queue->iface.input->extract = queue->iface.output->extract;
	queue->iface.input->insert = job->ringbuf.end;

	if (group->csg_id < 0) {
		/* If the queue is blocked, we want to keep the timeout running, so we
		 * can detect unbounded waits and kill the group when that happens.
		 * Otherwise, we suspend the timeout so the time we spend waiting for
		 * a CSG slot is not counted.
		 */
		if (!(group->blocked_queues & BIT(job->queue_idx)) &&
		    !queue->timeout_suspended) {
			queue->remaining_time = drm_sched_suspend_timeout(&queue->scheduler);
			queue->timeout_suspended = true;
		}

		group_schedule_locked(group, BIT(job->queue_idx));
	} else {
		gpu_write(ptdev, CSF_DOORBELL(queue->doorbell_id), 1);
		if (!sched->pm.has_ref &&
		    !(group->blocked_queues & BIT(job->queue_idx))) {
			pm_runtime_get(ptdev->base.dev);
			sched->pm.has_ref = true;
		}
		panthor_devfreq_record_busy(sched->ptdev);
	}

	/* Update the last fence. */
	dma_fence_put(queue->fence_ctx.last_fence);
	queue->fence_ctx.last_fence = dma_fence_get(job->done_fence);

	done_fence = dma_fence_get(job->done_fence);

out_unlock:
	mutex_unlock(&sched->lock);
	pm_runtime_mark_last_busy(ptdev->base.dev);
	pm_runtime_put_autosuspend(ptdev->base.dev);

	return done_fence;
}

static enum drm_gpu_sched_stat
queue_timedout_job(struct drm_sched_job *sched_job)
{
	struct panthor_job *job = container_of(sched_job, struct panthor_job, base);
	struct panthor_group *group = job->group;
	struct panthor_device *ptdev = group->ptdev;
	struct panthor_scheduler *sched = ptdev->scheduler;
	struct panthor_queue *queue = group->queues[job->queue_idx];

	drm_warn(&ptdev->base, "job timeout: pid=%d, comm=%s, seqno=%llu\n",
		 group->task_info.pid, group->task_info.comm, job->done_fence->seqno);

	drm_WARN_ON(&ptdev->base, atomic_read(&sched->reset.in_progress));

	queue_stop(queue, job);

	mutex_lock(&sched->lock);
	group->timedout = true;
	if (group->csg_id >= 0) {
		sched_queue_delayed_work(ptdev->scheduler, tick, 0);
	} else {
		/* Remove from the run queues, so the scheduler can't
		 * pick the group on the next tick.
		 */
		list_del_init(&group->run_node);
		list_del_init(&group->wait_node);

		group_queue_work(group, term);
	}
	mutex_unlock(&sched->lock);

	queue_start(queue);

	return DRM_GPU_SCHED_STAT_RESET;
}

static void queue_free_job(struct drm_sched_job *sched_job)
{
	drm_sched_job_cleanup(sched_job);
	panthor_job_put(sched_job);
}

static const struct drm_sched_backend_ops panthor_queue_sched_ops = {
	.run_job = queue_run_job,
	.timedout_job = queue_timedout_job,
	.free_job = queue_free_job,
};

static u32 calc_profiling_ringbuf_num_slots(struct panthor_device *ptdev,
					    u32 cs_ringbuf_size)
{
	u32 min_profiled_job_instrs = U32_MAX;
	u32 last_flag = fls(PANTHOR_DEVICE_PROFILING_ALL);

	/*
	 * We want to calculate the minimum size of a profiled job's CS,
	 * because since they need additional instructions for the sampling
	 * of performance metrics, they might take up further slots in
	 * the queue's ringbuffer. This means we might not need as many job
	 * slots for keeping track of their profiling information. What we
	 * need is the maximum number of slots we should allocate to this end,
	 * which matches the maximum number of profiled jobs we can place
	 * simultaneously in the queue's ring buffer.
	 * That has to be calculated separately for every single job profiling
	 * flag, but not in the case job profiling is disabled, since unprofiled
	 * jobs don't need to keep track of this at all.
	 */
	for (u32 i = 0; i < last_flag; i++) {
		min_profiled_job_instrs =
			min(min_profiled_job_instrs, calc_job_credits(BIT(i)));
	}

	return DIV_ROUND_UP(cs_ringbuf_size, min_profiled_job_instrs * sizeof(u64));
}

static struct panthor_queue *
group_create_queue(struct panthor_group *group,
		   const struct drm_panthor_queue_create *args)
{
	const struct drm_sched_init_args sched_args = {
		.ops = &panthor_queue_sched_ops,
		.submit_wq = group->ptdev->scheduler->wq,
		.num_rqs = 1,
		/*
		 * The credit limit argument tells us the total number of
		 * instructions across all CS slots in the ringbuffer, with
		 * some jobs requiring twice as many as others, depending on
		 * their profiling status.
		 */
		.credit_limit = args->ringbuf_size / sizeof(u64),
		.timeout = msecs_to_jiffies(JOB_TIMEOUT_MS),
		.timeout_wq = group->ptdev->reset.wq,
		.name = "panthor-queue",
		.dev = group->ptdev->base.dev,
	};
	struct drm_gpu_scheduler *drm_sched;
	struct panthor_queue *queue;
	int ret;

	if (args->pad[0] || args->pad[1] || args->pad[2])
		return ERR_PTR(-EINVAL);

	if (args->ringbuf_size < SZ_4K || args->ringbuf_size > SZ_64K ||
	    !is_power_of_2(args->ringbuf_size))
		return ERR_PTR(-EINVAL);

	if (args->priority > CSF_MAX_QUEUE_PRIO)
		return ERR_PTR(-EINVAL);

	queue = kzalloc(sizeof(*queue), GFP_KERNEL);
	if (!queue)
		return ERR_PTR(-ENOMEM);

	queue->fence_ctx.id = dma_fence_context_alloc(1);
	spin_lock_init(&queue->fence_ctx.lock);
	INIT_LIST_HEAD(&queue->fence_ctx.in_flight_jobs);

	queue->priority = args->priority;

	queue->ringbuf = panthor_kernel_bo_create(group->ptdev, group->vm,
						  args->ringbuf_size,
						  DRM_PANTHOR_BO_NO_MMAP,
						  DRM_PANTHOR_VM_BIND_OP_MAP_NOEXEC |
						  DRM_PANTHOR_VM_BIND_OP_MAP_UNCACHED,
						  PANTHOR_VM_KERNEL_AUTO_VA,
						  "CS ring buffer");
	if (IS_ERR(queue->ringbuf)) {
		ret = PTR_ERR(queue->ringbuf);
		goto err_free_queue;
	}

	ret = panthor_kernel_bo_vmap(queue->ringbuf);
	if (ret)
		goto err_free_queue;

	queue->iface.mem = panthor_fw_alloc_queue_iface_mem(group->ptdev,
							    &queue->iface.input,
							    &queue->iface.output,
							    &queue->iface.input_fw_va,
							    &queue->iface.output_fw_va);
	if (IS_ERR(queue->iface.mem)) {
		ret = PTR_ERR(queue->iface.mem);
		goto err_free_queue;
	}

	queue->profiling.slot_count =
		calc_profiling_ringbuf_num_slots(group->ptdev, args->ringbuf_size);

	queue->profiling.slots =
		panthor_kernel_bo_create(group->ptdev, group->vm,
					 queue->profiling.slot_count *
					 sizeof(struct panthor_job_profiling_data),
					 DRM_PANTHOR_BO_NO_MMAP,
					 DRM_PANTHOR_VM_BIND_OP_MAP_NOEXEC |
					 DRM_PANTHOR_VM_BIND_OP_MAP_UNCACHED,
					 PANTHOR_VM_KERNEL_AUTO_VA,
					 "Group job stats");

	if (IS_ERR(queue->profiling.slots)) {
		ret = PTR_ERR(queue->profiling.slots);
		goto err_free_queue;
	}

	ret = panthor_kernel_bo_vmap(queue->profiling.slots);
	if (ret)
		goto err_free_queue;

	ret = drm_sched_init(&queue->scheduler, &sched_args);
	if (ret)
		goto err_free_queue;

	drm_sched = &queue->scheduler;
	ret = drm_sched_entity_init(&queue->entity, 0, &drm_sched, 1, NULL);

	return queue;

err_free_queue:
	group_free_queue(group, queue);
	return ERR_PTR(ret);
}

static void group_init_task_info(struct panthor_group *group)
{
	struct task_struct *task = current->group_leader;

	group->task_info.pid = task->pid;
	get_task_comm(group->task_info.comm, task);
}

static void add_group_kbo_sizes(struct panthor_device *ptdev,
				struct panthor_group *group)
{
	struct panthor_queue *queue;
	int i;

	if (drm_WARN_ON(&ptdev->base, IS_ERR_OR_NULL(group)))
		return;
	if (drm_WARN_ON(&ptdev->base, ptdev != group->ptdev))
		return;

	group->fdinfo.kbo_sizes += group->suspend_buf->obj->size;
	group->fdinfo.kbo_sizes += group->protm_suspend_buf->obj->size;
	group->fdinfo.kbo_sizes += group->syncobjs->obj->size;

	for (i = 0; i < group->queue_count; i++) {
		queue =	group->queues[i];
		group->fdinfo.kbo_sizes += queue->ringbuf->obj->size;
		group->fdinfo.kbo_sizes += queue->iface.mem->obj->size;
		group->fdinfo.kbo_sizes += queue->profiling.slots->obj->size;
	}
}

#define MAX_GROUPS_PER_POOL		128

int panthor_group_create(struct panthor_file *pfile,
			 const struct drm_panthor_group_create *group_args,
			 const struct drm_panthor_queue_create *queue_args)
{
	struct panthor_device *ptdev = pfile->ptdev;
	struct panthor_group_pool *gpool = pfile->groups;
	struct panthor_scheduler *sched = ptdev->scheduler;
	struct panthor_fw_csg_iface *csg_iface = panthor_fw_get_csg_iface(ptdev, 0);
	struct panthor_group *group = NULL;
	u32 gid, i, suspend_size;
	int ret;

	if (group_args->pad)
		return -EINVAL;

	if (group_args->priority >= PANTHOR_CSG_PRIORITY_COUNT)
		return -EINVAL;

	if ((group_args->compute_core_mask & ~ptdev->gpu_info.shader_present) ||
	    (group_args->fragment_core_mask & ~ptdev->gpu_info.shader_present) ||
	    (group_args->tiler_core_mask & ~ptdev->gpu_info.tiler_present))
		return -EINVAL;

	if (hweight64(group_args->compute_core_mask) < group_args->max_compute_cores ||
	    hweight64(group_args->fragment_core_mask) < group_args->max_fragment_cores ||
	    hweight64(group_args->tiler_core_mask) < group_args->max_tiler_cores)
		return -EINVAL;

	group = kzalloc(sizeof(*group), GFP_KERNEL);
	if (!group)
		return -ENOMEM;

	spin_lock_init(&group->fatal_lock);
	kref_init(&group->refcount);
	group->state = PANTHOR_CS_GROUP_CREATED;
	group->csg_id = -1;

	group->ptdev = ptdev;
	group->max_compute_cores = group_args->max_compute_cores;
	group->compute_core_mask = group_args->compute_core_mask;
	group->max_fragment_cores = group_args->max_fragment_cores;
	group->fragment_core_mask = group_args->fragment_core_mask;
	group->max_tiler_cores = group_args->max_tiler_cores;
	group->tiler_core_mask = group_args->tiler_core_mask;
	group->priority = group_args->priority;

	INIT_LIST_HEAD(&group->wait_node);
	INIT_LIST_HEAD(&group->run_node);
	INIT_WORK(&group->term_work, group_term_work);
	INIT_WORK(&group->sync_upd_work, group_sync_upd_work);
	INIT_WORK(&group->tiler_oom_work, group_tiler_oom_work);
	INIT_WORK(&group->release_work, group_release_work);

	group->vm = panthor_vm_pool_get_vm(pfile->vms, group_args->vm_id);
	if (!group->vm) {
		ret = -EINVAL;
		goto err_put_group;
	}

	suspend_size = csg_iface->control->suspend_size;
	group->suspend_buf = panthor_fw_alloc_suspend_buf_mem(ptdev, suspend_size);
	if (IS_ERR(group->suspend_buf)) {
		ret = PTR_ERR(group->suspend_buf);
		group->suspend_buf = NULL;
		goto err_put_group;
	}

	suspend_size = csg_iface->control->protm_suspend_size;
	group->protm_suspend_buf = panthor_fw_alloc_suspend_buf_mem(ptdev, suspend_size);
	if (IS_ERR(group->protm_suspend_buf)) {
		ret = PTR_ERR(group->protm_suspend_buf);
		group->protm_suspend_buf = NULL;
		goto err_put_group;
	}

	group->syncobjs = panthor_kernel_bo_create(ptdev, group->vm,
						   group_args->queues.count *
						   sizeof(struct panthor_syncobj_64b),
						   DRM_PANTHOR_BO_NO_MMAP,
						   DRM_PANTHOR_VM_BIND_OP_MAP_NOEXEC |
						   DRM_PANTHOR_VM_BIND_OP_MAP_UNCACHED,
						   PANTHOR_VM_KERNEL_AUTO_VA,
						   "Group sync objects");
	if (IS_ERR(group->syncobjs)) {
		ret = PTR_ERR(group->syncobjs);
		goto err_put_group;
	}

	ret = panthor_kernel_bo_vmap(group->syncobjs);
	if (ret)
		goto err_put_group;

	memset(group->syncobjs->kmap, 0,
	       group_args->queues.count * sizeof(struct panthor_syncobj_64b));

	for (i = 0; i < group_args->queues.count; i++) {
		group->queues[i] = group_create_queue(group, &queue_args[i]);
		if (IS_ERR(group->queues[i])) {
			ret = PTR_ERR(group->queues[i]);
			group->queues[i] = NULL;
			goto err_put_group;
		}

		group->queue_count++;
	}

	group->idle_queues = GENMASK(group->queue_count - 1, 0);

	ret = xa_alloc(&gpool->xa, &gid, group, XA_LIMIT(1, MAX_GROUPS_PER_POOL), GFP_KERNEL);
	if (ret)
		goto err_put_group;

	mutex_lock(&sched->reset.lock);
	if (atomic_read(&sched->reset.in_progress)) {
		panthor_group_stop(group);
	} else {
		mutex_lock(&sched->lock);
		list_add_tail(&group->run_node,
			      &sched->groups.idle[group->priority]);
		mutex_unlock(&sched->lock);
	}
	mutex_unlock(&sched->reset.lock);

	add_group_kbo_sizes(group->ptdev, group);
	spin_lock_init(&group->fdinfo.lock);

	group_init_task_info(group);

	return gid;

err_put_group:
	group_put(group);
	return ret;
}

int panthor_group_destroy(struct panthor_file *pfile, u32 group_handle)
{
	struct panthor_group_pool *gpool = pfile->groups;
	struct panthor_device *ptdev = pfile->ptdev;
	struct panthor_scheduler *sched = ptdev->scheduler;
	struct panthor_group *group;

	group = xa_erase(&gpool->xa, group_handle);
	if (!group)
		return -EINVAL;

	mutex_lock(&sched->reset.lock);
	mutex_lock(&sched->lock);
	group->destroyed = true;
	if (group->csg_id >= 0) {
		sched_queue_delayed_work(sched, tick, 0);
	} else if (!atomic_read(&sched->reset.in_progress)) {
		/* Remove from the run queues, so the scheduler can't
		 * pick the group on the next tick.
		 */
		list_del_init(&group->run_node);
		list_del_init(&group->wait_node);
		group_queue_work(group, term);
	}
	mutex_unlock(&sched->lock);
	mutex_unlock(&sched->reset.lock);

	group_put(group);
	return 0;
}

static struct panthor_group *group_from_handle(struct panthor_group_pool *pool,
					       u32 group_handle)
{
	struct panthor_group *group;

	xa_lock(&pool->xa);
	group = group_get(xa_load(&pool->xa, group_handle));
	xa_unlock(&pool->xa);

	return group;
}

int panthor_group_get_state(struct panthor_file *pfile,
			    struct drm_panthor_group_get_state *get_state)
{
	struct panthor_group_pool *gpool = pfile->groups;
	struct panthor_device *ptdev = pfile->ptdev;
	struct panthor_scheduler *sched = ptdev->scheduler;
	struct panthor_group *group;

	if (get_state->pad)
		return -EINVAL;

	group = group_from_handle(gpool, get_state->group_handle);
	if (!group)
		return -EINVAL;

	memset(get_state, 0, sizeof(*get_state));

	mutex_lock(&sched->lock);
	if (group->timedout)
		get_state->state |= DRM_PANTHOR_GROUP_STATE_TIMEDOUT;
	if (group->fatal_queues) {
		get_state->state |= DRM_PANTHOR_GROUP_STATE_FATAL_FAULT;
		get_state->fatal_queues = group->fatal_queues;
	}
	if (group->innocent)
		get_state->state |= DRM_PANTHOR_GROUP_STATE_INNOCENT;
	mutex_unlock(&sched->lock);

	group_put(group);
	return 0;
}

int panthor_group_pool_create(struct panthor_file *pfile)
{
	struct panthor_group_pool *gpool;

	gpool = kzalloc(sizeof(*gpool), GFP_KERNEL);
	if (!gpool)
		return -ENOMEM;

	xa_init_flags(&gpool->xa, XA_FLAGS_ALLOC1);
	pfile->groups = gpool;
	return 0;
}

void panthor_group_pool_destroy(struct panthor_file *pfile)
{
	struct panthor_group_pool *gpool = pfile->groups;
	struct panthor_group *group;
	unsigned long i;

	if (IS_ERR_OR_NULL(gpool))
		return;

	xa_for_each(&gpool->xa, i, group)
		panthor_group_destroy(pfile, i);

	xa_destroy(&gpool->xa);
	kfree(gpool);
	pfile->groups = NULL;
}

/**
 * panthor_fdinfo_gather_group_mem_info() - Retrieve aggregate size of all private kernel BO's
 * belonging to all the groups owned by an open Panthor file
 * @pfile: File.
 * @stats: Memory statistics to be updated.
 *
 */
void
panthor_fdinfo_gather_group_mem_info(struct panthor_file *pfile,
				     struct drm_memory_stats *stats)
{
	struct panthor_group_pool *gpool = pfile->groups;
	struct panthor_group *group;
	unsigned long i;

	if (IS_ERR_OR_NULL(gpool))
		return;

	xa_lock(&gpool->xa);
	xa_for_each(&gpool->xa, i, group) {
		stats->resident += group->fdinfo.kbo_sizes;
		if (group->csg_id >= 0)
			stats->active += group->fdinfo.kbo_sizes;
	}
	xa_unlock(&gpool->xa);
}

static void job_release(struct kref *ref)
{
	struct panthor_job *job = container_of(ref, struct panthor_job, refcount);

	drm_WARN_ON(&job->group->ptdev->base, !list_empty(&job->node));

	if (job->base.s_fence)
		drm_sched_job_cleanup(&job->base);

	if (job->done_fence && job->done_fence->ops)
		dma_fence_put(job->done_fence);
	else
		dma_fence_free(job->done_fence);

	group_put(job->group);

	kfree(job);
}

struct drm_sched_job *panthor_job_get(struct drm_sched_job *sched_job)
{
	if (sched_job) {
		struct panthor_job *job = container_of(sched_job, struct panthor_job, base);

		kref_get(&job->refcount);
	}

	return sched_job;
}

void panthor_job_put(struct drm_sched_job *sched_job)
{
	struct panthor_job *job = container_of(sched_job, struct panthor_job, base);

	if (sched_job)
		kref_put(&job->refcount, job_release);
}

struct panthor_vm *panthor_job_vm(struct drm_sched_job *sched_job)
{
	struct panthor_job *job = container_of(sched_job, struct panthor_job, base);

	return job->group->vm;
}

struct drm_sched_job *
panthor_job_create(struct panthor_file *pfile,
		   u16 group_handle,
		   const struct drm_panthor_queue_submit *qsubmit,
		   u64 drm_client_id)
{
	struct panthor_group_pool *gpool = pfile->groups;
	struct panthor_job *job;
	u32 credits;
	int ret;

	if (qsubmit->pad)
		return ERR_PTR(-EINVAL);

	/* If stream_addr is zero, so stream_size should be. */
	if ((qsubmit->stream_size == 0) != (qsubmit->stream_addr == 0))
		return ERR_PTR(-EINVAL);

	/* Make sure the address is aligned on 64-byte (cacheline) and the size is
	 * aligned on 8-byte (instruction size).
	 */
	if ((qsubmit->stream_addr & 63) || (qsubmit->stream_size & 7))
		return ERR_PTR(-EINVAL);

	/* bits 24:30 must be zero. */
	if (qsubmit->latest_flush & GENMASK(30, 24))
		return ERR_PTR(-EINVAL);

	job = kzalloc(sizeof(*job), GFP_KERNEL);
	if (!job)
		return ERR_PTR(-ENOMEM);

	kref_init(&job->refcount);
	job->queue_idx = qsubmit->queue_index;
	job->call_info.size = qsubmit->stream_size;
	job->call_info.start = qsubmit->stream_addr;
	job->call_info.latest_flush = qsubmit->latest_flush;
	INIT_LIST_HEAD(&job->node);

	job->group = group_from_handle(gpool, group_handle);
	if (!job->group) {
		ret = -EINVAL;
		goto err_put_job;
	}

	if (!group_can_run(job->group)) {
		ret = -EINVAL;
		goto err_put_job;
	}

	if (job->queue_idx >= job->group->queue_count ||
	    !job->group->queues[job->queue_idx]) {
		ret = -EINVAL;
		goto err_put_job;
	}

	/* Empty command streams don't need a fence, they'll pick the one from
	 * the previously submitted job.
	 */
	if (job->call_info.size) {
		job->done_fence = kzalloc(sizeof(*job->done_fence), GFP_KERNEL);
		if (!job->done_fence) {
			ret = -ENOMEM;
			goto err_put_job;
		}
	}

	job->profiling.mask = pfile->ptdev->profile_mask;
	credits = calc_job_credits(job->profiling.mask);
	if (credits == 0) {
		ret = -EINVAL;
		goto err_put_job;
	}

	ret = drm_sched_job_init(&job->base,
				 &job->group->queues[job->queue_idx]->entity,
				 credits, job->group, drm_client_id);
	if (ret)
		goto err_put_job;

	return &job->base;

err_put_job:
	panthor_job_put(&job->base);
	return ERR_PTR(ret);
}

void panthor_job_update_resvs(struct drm_exec *exec, struct drm_sched_job *sched_job)
{
	struct panthor_job *job = container_of(sched_job, struct panthor_job, base);

	panthor_vm_update_resvs(job->group->vm, exec, &sched_job->s_fence->finished,
				DMA_RESV_USAGE_BOOKKEEP, DMA_RESV_USAGE_BOOKKEEP);
}

void panthor_sched_unplug(struct panthor_device *ptdev)
{
	struct panthor_scheduler *sched = ptdev->scheduler;

	cancel_delayed_work_sync(&sched->tick_work);

	mutex_lock(&sched->lock);
	if (sched->pm.has_ref) {
		pm_runtime_put(ptdev->base.dev);
		sched->pm.has_ref = false;
	}
	mutex_unlock(&sched->lock);
}

static void panthor_sched_fini(struct drm_device *ddev, void *res)
{
	struct panthor_scheduler *sched = res;
	int prio;

	if (!sched || !sched->csg_slot_count)
		return;

	cancel_delayed_work_sync(&sched->tick_work);

	if (sched->wq)
		destroy_workqueue(sched->wq);

	if (sched->heap_alloc_wq)
		destroy_workqueue(sched->heap_alloc_wq);

	for (prio = PANTHOR_CSG_PRIORITY_COUNT - 1; prio >= 0; prio--) {
		drm_WARN_ON(ddev, !list_empty(&sched->groups.runnable[prio]));
		drm_WARN_ON(ddev, !list_empty(&sched->groups.idle[prio]));
	}

	drm_WARN_ON(ddev, !list_empty(&sched->groups.waiting));
}

int panthor_sched_init(struct panthor_device *ptdev)
{
	struct panthor_fw_global_iface *glb_iface = panthor_fw_get_glb_iface(ptdev);
	struct panthor_fw_csg_iface *csg_iface = panthor_fw_get_csg_iface(ptdev, 0);
	struct panthor_fw_cs_iface *cs_iface = panthor_fw_get_cs_iface(ptdev, 0, 0);
	struct panthor_scheduler *sched;
	u32 gpu_as_count, num_groups;
	int prio, ret;

	sched = drmm_kzalloc(&ptdev->base, sizeof(*sched), GFP_KERNEL);
	if (!sched)
		return -ENOMEM;

	/* The highest bit in JOB_INT_* is reserved for globabl IRQs. That
	 * leaves 31 bits for CSG IRQs, hence the MAX_CSGS clamp here.
	 */
	num_groups = min_t(u32, MAX_CSGS, glb_iface->control->group_num);

	/* The FW-side scheduler might deadlock if two groups with the same
	 * priority try to access a set of resources that overlaps, with part
	 * of the resources being allocated to one group and the other part to
	 * the other group, both groups waiting for the remaining resources to
	 * be allocated. To avoid that, it is recommended to assign each CSG a
	 * different priority. In theory we could allow several groups to have
	 * the same CSG priority if they don't request the same resources, but
	 * that makes the scheduling logic more complicated, so let's clamp
	 * the number of CSG slots to MAX_CSG_PRIO + 1 for now.
	 */
	num_groups = min_t(u32, MAX_CSG_PRIO + 1, num_groups);

	/* We need at least one AS for the MCU and one for the GPU contexts. */
	gpu_as_count = hweight32(ptdev->gpu_info.as_present & GENMASK(31, 1));
	if (!gpu_as_count) {
		drm_err(&ptdev->base, "Not enough AS (%d, expected at least 2)",
			gpu_as_count + 1);
		return -EINVAL;
	}

	sched->ptdev = ptdev;
	sched->sb_slot_count = CS_FEATURES_SCOREBOARDS(cs_iface->control->features);
	sched->csg_slot_count = num_groups;
	sched->cs_slot_count = csg_iface->control->stream_num;
	sched->as_slot_count = gpu_as_count;
	ptdev->csif_info.csg_slot_count = sched->csg_slot_count;
	ptdev->csif_info.cs_slot_count = sched->cs_slot_count;
	ptdev->csif_info.scoreboard_slot_count = sched->sb_slot_count;

	sched->last_tick = 0;
	sched->resched_target = U64_MAX;
	sched->tick_period = msecs_to_jiffies(10);
	INIT_DELAYED_WORK(&sched->tick_work, tick_work);
	INIT_WORK(&sched->sync_upd_work, sync_upd_work);
	INIT_WORK(&sched->fw_events_work, process_fw_events_work);

	ret = drmm_mutex_init(&ptdev->base, &sched->lock);
	if (ret)
		return ret;

	for (prio = PANTHOR_CSG_PRIORITY_COUNT - 1; prio >= 0; prio--) {
		INIT_LIST_HEAD(&sched->groups.runnable[prio]);
		INIT_LIST_HEAD(&sched->groups.idle[prio]);
	}
	INIT_LIST_HEAD(&sched->groups.waiting);

	ret = drmm_mutex_init(&ptdev->base, &sched->reset.lock);
	if (ret)
		return ret;

	INIT_LIST_HEAD(&sched->reset.stopped_groups);

	/* sched->heap_alloc_wq will be used for heap chunk allocation on
	 * tiler OOM events, which means we can't use the same workqueue for
	 * the scheduler because works queued by the scheduler are in
	 * the dma-signalling path. Allocate a dedicated heap_alloc_wq to
	 * work around this limitation.
	 *
	 * FIXME: Ultimately, what we need is a failable/non-blocking GEM
	 * allocation path that we can call when a heap OOM is reported. The
	 * FW is smart enough to fall back on other methods if the kernel can't
	 * allocate memory, and fail the tiling job if none of these
	 * countermeasures worked.
	 *
	 * Set WQ_MEM_RECLAIM on sched->wq to unblock the situation when the
	 * system is running out of memory.
	 */
	sched->heap_alloc_wq = alloc_workqueue("panthor-heap-alloc", WQ_UNBOUND, 0);
	sched->wq = alloc_workqueue("panthor-csf-sched", WQ_MEM_RECLAIM | WQ_UNBOUND, 0);
	if (!sched->wq || !sched->heap_alloc_wq) {
		panthor_sched_fini(&ptdev->base, sched);
		drm_err(&ptdev->base, "Failed to allocate the workqueues");
		return -ENOMEM;
	}

	ret = drmm_add_action_or_reset(&ptdev->base, panthor_sched_fini, sched);
	if (ret)
		return ret;

	ptdev->scheduler = sched;
	return 0;
}
