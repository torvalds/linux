/*
 *
 * (C) COPYRIGHT 2018-2020 ARM Limited. All rights reserved.
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

/* Definitions (types, defines, etcs) common to the command stream frontend.
 * They are placed here to allow the hierarchy of header files to work.
 */

#ifndef _KBASE_CSF_DEFS_H_
#define _KBASE_CSF_DEFS_H_

#include <linux/types.h>
#include <linux/wait.h>

#include "mali_kbase_csf_firmware.h"

/* Maximum number of KCPU command queues to be created per GPU address space.
 */
#define KBASEP_MAX_KCPU_QUEUES ((size_t)256)

/* Maximum number of GPU command queue groups to be created per GPU address
 * space.
 */
#define MAX_QUEUE_GROUP_NUM (256)

/* Maximum number of GPU tiler heaps to allow to be created per GPU address
 * space.
 */
#define MAX_TILER_HEAPS (128)

/**
 * enum kbase_csf_bind_state - bind state of the queue
 *
 * @KBASE_CSF_QUEUE_UNBOUND: Set when the queue is registered or when the link
 * between queue and the group to which it was bound or being bound is removed.
 * @KBASE_CSF_QUEUE_BIND_IN_PROGRESS: Set when the first part of bind operation
 * has completed i.e. CS_QUEUE_BIND ioctl.
 * @KBASE_CSF_QUEUE_BOUND: Set when the bind operation has completed i.e. IO
 * pages have been mapped in the process address space.
 */
enum kbase_csf_queue_bind_state {
	KBASE_CSF_QUEUE_UNBOUND,
	KBASE_CSF_QUEUE_BIND_IN_PROGRESS,
	KBASE_CSF_QUEUE_BOUND,
};

/**
 * enum kbase_csf_reset_gpu_state - state of the gpu reset
 *
 * @KBASE_CSF_RESET_GPU_NOT_PENDING: Set when the GPU reset isn't pending
 * @KBASE_CSF_RESET_GPU_HAPPENING: Set when the GPU reset process is occurring
 * @KBASE_CSF_RESET_GPU_SILENT: Set when the GPU reset process is occurring,
 * used when resetting the GPU as part of normal behavior (e.g. when exiting
 * protected mode).
 * @KBASE_CSF_RESET_GPU_FAILED: Set when an error is encountered during the
 * GPU reset process. No more work could then be executed on GPU, unloading
 * the Driver module is the only option.
 */
enum kbase_csf_reset_gpu_state {
	KBASE_CSF_RESET_GPU_NOT_PENDING,
	KBASE_CSF_RESET_GPU_HAPPENING,
	KBASE_CSF_RESET_GPU_SILENT,
	KBASE_CSF_RESET_GPU_FAILED,
};

/**
 * enum kbase_csf_group_state - state of the GPU command queue group
 *
 * @KBASE_CSF_GROUP_INACTIVE:          Group is inactive and won't be
 *                                     considered by scheduler for running on
 *                                     command stream group slot.
 * @KBASE_CSF_GROUP_RUNNABLE:          Group is in the list of runnable groups
 *                                     and is subjected to time-slice based
 *                                     scheduling. A start request would be
 *                                     sent (or already has been sent) if the
 *                                     group is assigned the command stream
 *                                     group slot for the fist time.
 * @KBASE_CSF_GROUP_IDLE:              Group is currently on a command stream
 *                                     group slot but all the command streams
 *                                     bound to the group have become either
 *                                     idle or waiting on sync object.
 *                                     Group could be evicted from the slot on
 *                                     the next tick if there are no spare
 *                                     slots left after scheduling non-idle
 *                                     queue groups. If the group is kept on
 *                                     slot then it would be moved to the
 *                                     RUNNABLE state, also if one of the
 *                                     queues bound to the group is kicked it
 *                                     would be moved to the RUNNABLE state.
 *                                     If the group is evicted from the slot it
 *                                     would be moved to either
 *                                     KBASE_CSF_GROUP_SUSPENDED_ON_IDLE or
 *                                     KBASE_CSF_GROUP_SUSPENDED_ON_WAIT_SYNC
 *                                     state.
 * @KBASE_CSF_GROUP_SUSPENDED:         Group was evicted from the command
 *                                     stream group slot and is not running but
 *                                     is still in the list of runnable groups
 *                                     and subjected to time-slice based
 *                                     scheduling. A resume request would be
 *                                     sent when a command stream group slot is
 *                                     re-assigned to the group and once the
 *                                     resume is complete group would be moved
 *                                     back to the RUNNABLE state.
 * @KBASE_CSF_GROUP_SUSPENDED_ON_IDLE: Same as KBASE_CSF_GROUP_SUSPENDED except
 *                                     that queue group also became idle before
 *                                     the suspension. This state helps
 *                                     Scheduler avoid scheduling the idle
 *                                     groups over the non-idle groups in the
 *                                     subsequent ticks. If one of the queues
 *                                     bound to the group is kicked it would be
 *                                     moved to the SUSPENDED state.
 * @KBASE_CSF_GROUP_SUSPENDED_ON_WAIT_SYNC: Same as GROUP_SUSPENDED_ON_IDLE
 *                                          except that at least one command
 *                                          stream bound to this group was
 *                                          waiting for synchronization object
 *                                          before the suspension.
 * @KBASE_CSF_GROUP_FAULT_EVICTED:     Group is evicted from the scheduler due
 *                                     to a fault condition, pending to be
 *                                     terminated.
 * @KBASE_CSF_GROUP_TERMINATED:        Group is no longer schedulable and is
 *                                     pending to be deleted by Client, all the
 *                                     queues bound to it have been unbound.
 */
enum kbase_csf_group_state {
	KBASE_CSF_GROUP_INACTIVE,
	KBASE_CSF_GROUP_RUNNABLE,
	KBASE_CSF_GROUP_IDLE,
	KBASE_CSF_GROUP_SUSPENDED,
	KBASE_CSF_GROUP_SUSPENDED_ON_IDLE,
	KBASE_CSF_GROUP_SUSPENDED_ON_WAIT_SYNC,
	KBASE_CSF_GROUP_FAULT_EVICTED,
	KBASE_CSF_GROUP_TERMINATED,
};

/**
 * enum kbase_csf_csg_slot_state - state of the command queue group slots under
 *                                 the scheduler control.
 *
 * @CSG_SLOT_READY:     The slot is clean and ready to be programmed with a
 *                      queue group.
 * @CSG_SLOT_READY2RUN: The slot has been programmed with a queue group, i.e. a
 *                      start or resume request has been sent to the firmware.
 * @CSG_SLOT_RUNNING:   The queue group is running on the slot, acknowledgment
 *                      of a start or resume request has been obtained from the
 *                      firmware.
 * @CSG_SLOT_DOWN2STOP: The suspend or terminate request for the queue group on
 *                      the slot has been sent to the firmware.
 * @CSG_SLOT_STOPPED:   The queue group is removed from the slot, acknowledgment
 *                      of suspend or terminate request has been obtained from
 *                      the firmware.
 * @CSG_SLOT_READY2RUN_TIMEDOUT: The start or resume request sent on the slot
 *                               for the queue group timed out.
 * @CSG_SLOT_DOWN2STOP_TIMEDOUT: The suspend or terminate request for queue
 *                               group on the slot timed out.
 */
enum kbase_csf_csg_slot_state {
	CSG_SLOT_READY,
	CSG_SLOT_READY2RUN,
	CSG_SLOT_RUNNING,
	CSG_SLOT_DOWN2STOP,
	CSG_SLOT_STOPPED,
	CSG_SLOT_READY2RUN_TIMEDOUT,
	CSG_SLOT_DOWN2STOP_TIMEDOUT,
};

/**
 * enum kbase_csf_scheduler_state - state of the scheduler operational phases.
 *
 * @SCHED_BUSY:         The scheduler is busy performing on tick schedule
 *                      operations, the state of command stream group slots
 *                      can't be changed.
 * @SCHED_INACTIVE:     The scheduler is inactive, it is allowed to modify the
 *                      state of command stream group slots by in-cycle
 *                      priority scheduling.
 * @SCHED_SUSPENDED:    The scheduler is in low-power mode with scheduling
 *                      operations suspended and is not holding the power
 *                      management reference. This can happen if the GPU
 *                      becomes idle for a duration exceeding a threshold,
 *                      or due to a system triggered suspend action.
 */
enum kbase_csf_scheduler_state {
	SCHED_BUSY,
	SCHED_INACTIVE,
	SCHED_SUSPENDED,
};

/**
 * struct kbase_csf_notification - Event or error generated as part of command
 *                                 queue execution
 *
 * @data:      Event or error data returned to userspace
 * @link:      Link to the linked list, &struct_kbase_csf_context.error_list.
 */
struct kbase_csf_notification {
	struct base_csf_notification data;
	struct list_head link;
};

/**
 * struct kbase_queue - Object representing a GPU command queue.
 *
 * @kctx:        Pointer to the base context with which this GPU command queue
 *               is associated.
 * @reg:         Pointer to the region allocated from the shared
 *               interface segment for mapping the User mode
 *               input/output pages in MCU firmware address space.
 * @phys:        Pointer to the physical pages allocated for the
 *               pair or User mode input/output page
 * @user_io_addr: Pointer to the permanent kernel mapping of User mode
 *                input/output pages. The pages can be accessed through
 *                the mapping without any cache maintenance.
 * @handle:      Handle returned with bind ioctl for creating a
 *               contiguous User mode mapping of input/output pages &
 *               the hardware doorbell page.
 * @doorbell_nr: Index of the hardware doorbell page assigned to the
 *               queue.
 * @db_file_offset: File offset value that is assigned to userspace mapping
 *                  created on bind to access the doorbell page.
 *                  It is in page units.
 * @link:        Link to the linked list of GPU command queues created per
 *               GPU address space.
 * @refcount:    Reference count, stands for the number of times the queue
 *               has been referenced. The reference is taken when it is
 *               created, when it is bound to the group and also when the
 *               @oom_event_work or @fault_event_work work item is queued
 *               for it.
 * @group:       Pointer to the group to which this queue is bound.
 * @queue_reg:   Pointer to the VA region allocated for command
 *               stream buffer.
 * @oom_event_work: Work item corresponding to the out of memory event for
 *                  chunked tiler heap being used for this queue.
 * @fault_event_work: Work item corresponding to the firmware fault event.
 * @base_addr:      Base address of the command stream buffer.
 * @size:           Size of the command stream buffer.
 * @priority:       Priority of this queue within the group.
 * @bind_state:     Bind state of the queue.
 * @csi_index:      The ID of the assigned command stream hardware interface.
 * @enabled:        Indicating whether the command stream is running, or not.
 * @status_wait:    Value of CS_STATUS_WAIT register of the command stream will
 *                  be kept when the command stream gets blocked by sync wait.
 *                  CS_STATUS_WAIT provides information on conditions queue is
 *                  blocking on. This is set when the group, to which queue is
 *                  bound, is suspended after getting blocked, i.e. in
 *                  KBASE_CSF_GROUP_SUSPENDED_ON_WAIT_SYNC state.
 * @sync_ptr:       Value of CS_STATUS_WAIT_SYNC_POINTER register of the command
 *                  stream will be kept when the command stream gets blocked by
 *                  sync wait. CS_STATUS_WAIT_SYNC_POINTER contains the address
 *                  of synchronization object being waited on.
 *                  Valid only when @status_wait is set.
 * @sync_value:     Value of CS_STATUS_WAIT_SYNC_VALUE register of the command
 *                  stream will be kept when the command stream gets blocked by
 *                  sync wait. CS_STATUS_WAIT_SYNC_VALUE contains the value
 *                  tested against the synchronization object.
 *                  Valid only when @status_wait is set.
 * @error:          GPU command queue fatal information to pass to user space.
 */
struct kbase_queue {
	struct kbase_context *kctx;
	struct kbase_va_region *reg;
	struct tagged_addr phys[2];
	char *user_io_addr;
	u64 handle;
	int doorbell_nr;
	unsigned long db_file_offset;
	struct list_head link;
	atomic_t refcount;
	struct kbase_queue_group *group;
	struct kbase_va_region *queue_reg;
	struct work_struct oom_event_work;
	struct work_struct fault_event_work;
	u64 base_addr;
	u32 size;
	u8 priority;
	u8 bind_state;
	s8 csi_index;
	bool enabled;
	u32 status_wait;
	u64 sync_ptr;
	u32 sync_value;
	struct kbase_csf_notification error;
};

/**
 * struct kbase_normal_suspend_buffer - Object representing a normal
 *		suspend buffer for queue group.
 * @reg:	Memory region allocated for the normal-mode suspend buffer.
 * @phy:	Array of physical memory pages allocated for the normal-
 *		mode suspend buffer.
 */
struct kbase_normal_suspend_buffer {
	struct kbase_va_region *reg;
	struct tagged_addr *phy;
};

/**
 * struct kbase_protected_suspend_buffer - Object representing a protected
 *		suspend buffer for queue group.
 * @reg:	Memory region allocated for the protected-mode suspend buffer.
 * @pma:	Array of pointer to protected mode allocations containing
 *		information about memory pages allocated for protected mode
 *		suspend	buffer.
 */
struct kbase_protected_suspend_buffer {
	struct kbase_va_region *reg;
	struct protected_memory_allocation **pma;
};

/**
 * struct kbase_queue_group - Object representing a GPU command queue group.
 *
 * @kctx:           Pointer to the kbase context with which this queue group
 *                  is associated.
 * @normal_suspend_buf:		Object representing the normal suspend buffer.
 *				Normal-mode suspend buffer that is used for
 *				group context switch.
 * @protected_suspend_buf:	Object representing the protected suspend
 *				buffer. Protected-mode suspend buffer that is
 *				used for group context switch.
 * @handle:         Handle which identifies this queue group.
 * @csg_nr:         Number/index of the command stream group to
 *                  which this queue group is mapped; KBASEP_CSG_NR_INVALID
 *                  indicates that the queue group is not scheduled.
 * @priority:       Priority of the queue group, 0 being the highest,
 *                  BASE_QUEUE_GROUP_PRIORITY_COUNT - 1 being the lowest.
 * @tiler_max:      Maximum number of tiler endpoints the group is allowed
 *                  to use.
 * @fragment_max:   Maximum number of fragment endpoints the group is
 *                  allowed to use.
 * @compute_max:    Maximum number of compute endpoints the group is
 *                  allowed to use.
 * @tiler_mask:     Mask of tiler endpoints the group is allowed to use.
 * @fragment_mask:  Mask of fragment endpoints the group is allowed to use.
 * @compute_mask:   Mask of compute endpoints the group is allowed to use.
 * @link:           Link to this queue group in the 'runnable_groups' list of
 *                  the corresponding kctx.
 * @link_to_schedule: Link to this queue group in the list of prepared groups
 *                    to be scheduled, if the group is runnable/suspended.
 *                    If the group is idle or waiting for CQS, it would be a
 *                    link to the list of idle/blocked groups list.
 * @timer_event_work: Work item corresponding to the event generated when a task
 *                    started by a queue in this group takes too long to execute
 *                    on an endpoint.
 * @run_state:      Current state of the queue group.
 * @prepared_seq_num: Indicates the position of queue group in the list of
 *                    prepared groups to be scheduled.
 * @faulted:          Indicates that a GPU fault occurred for the queue group.
 *                    This flag persists until the fault has been queued to be
 *                    reported to userspace.
 * @bound_queues:   Array of registered queues bound to this queue group.
 * @doorbell_nr:    Index of the hardware doorbell page assigned to the
 *                  group.
 * @protm_event_work:   Work item corresponding to the protected mode entry
 *                      event for this queue.
 * @protm_pending_bitmap:  Bit array to keep a track of command streams that
 *                         have pending protected mode entry requests.
 * @error_fatal: An error of type BASE_GPU_QUEUE_GROUP_ERROR_FATAL to be
 *               returned to userspace if such an error has occurred.
 * @error_timeout: An error of type BASE_GPU_QUEUE_GROUP_ERROR_TIMEOUT
 *                 to be returned to userspace if such an error has occurred.
 * @error_tiler_oom: An error of type BASE_GPU_QUEUE_GROUP_ERROR_TILER_HEAP_OOM
 *                   to be returned to userspace if such an error has occurred.
 */
struct kbase_queue_group {
	struct kbase_context *kctx;
	struct kbase_normal_suspend_buffer normal_suspend_buf;
	struct kbase_protected_suspend_buffer protected_suspend_buf;
	u8 handle;
	s8 csg_nr;
	u8 priority;

	u8 tiler_max;
	u8 fragment_max;
	u8 compute_max;

	u64 tiler_mask;
	u64 fragment_mask;
	u64 compute_mask;

	struct list_head link;
	struct list_head link_to_schedule;
	struct work_struct timer_event_work;
	enum kbase_csf_group_state run_state;
	u32 prepared_seq_num;
	bool faulted;

	struct kbase_queue *bound_queues[MAX_SUPPORTED_STREAMS_PER_GROUP];

	int doorbell_nr;
	struct work_struct protm_event_work;
	DECLARE_BITMAP(protm_pending_bitmap, MAX_SUPPORTED_STREAMS_PER_GROUP);

	struct kbase_csf_notification error_fatal;
	struct kbase_csf_notification error_timeout;
	struct kbase_csf_notification error_tiler_oom;
};

/**
 * struct kbase_csf_kcpu_queue_context - Object representing the kernel CPU
 *                                       queues for a GPU address space.
 *
 * @lock:   Lock preventing concurrent access to @array and the @in_use bitmap.
 * @array:  Array of pointers to kernel CPU command queues.
 * @in_use: Bitmap which indicates which kernel CPU command queues are in use.
 * @wq:     Dedicated workqueue for processing kernel CPU command queues.
 * @num_cmds:           The number of commands that have been enqueued across
 *                      all the KCPU command queues. This could be used as a
 *                      timestamp to determine the command's enqueueing time.
 * @jit_cmds_head:      A list of the just-in-time memory commands, both
 *                      allocate & free, in submission order, protected
 *                      by kbase_csf_kcpu_queue_context.lock.
 * @jit_blocked_queues: A list of KCPU command queues blocked by a pending
 *                      just-in-time memory allocation command which will be
 *                      reattempted after the impending free of other active
 *                      allocations.
 */
struct kbase_csf_kcpu_queue_context {
	struct mutex lock;
	struct kbase_kcpu_command_queue *array[KBASEP_MAX_KCPU_QUEUES];
	DECLARE_BITMAP(in_use, KBASEP_MAX_KCPU_QUEUES);
	struct workqueue_struct *wq;
	u64 num_cmds;

	struct list_head jit_cmds_head;
	struct list_head jit_blocked_queues;
};

/**
 * struct kbase_csf_heap_context_allocator - Allocator of heap contexts
 *
 * Heap context structures are allocated by the kernel for use by the firmware.
 * The current implementation subdivides a single GPU memory region for use as
 * a sparse array.
 *
 * @kctx:     Pointer to the kbase context with which this allocator is
 *            associated.
 * @region:   Pointer to a GPU memory region from which heap context structures
 *            are allocated. NULL if no heap contexts have been allocated.
 * @gpu_va:   GPU virtual address of the start of the region from which heap
 *            context structures are allocated. 0 if no heap contexts have been
 *            allocated.
 * @lock:     Lock preventing concurrent access to the @in_use bitmap.
 * @in_use:   Bitmap that indicates which heap context structures are currently
 *            allocated (in @region).
 */
struct kbase_csf_heap_context_allocator {
	struct kbase_context *kctx;
	struct kbase_va_region *region;
	u64 gpu_va;
	struct mutex lock;
	DECLARE_BITMAP(in_use, MAX_TILER_HEAPS);
};

/**
 * struct kbase_csf_tiler_heap_context - Object representing the tiler heaps
 *                                       context for a GPU address space.
 *
 * This contains all of the command-stream front-end state relating to chunked
 * tiler heaps for one @kbase_context. It is not the same as a heap context
 * structure allocated by the kernel for use by the firmware.
 *
 * @lock:      Lock preventing concurrent access to the tiler heaps.
 * @list:      List of tiler heaps.
 * @ctx_alloc: Allocator for heap context structures.
 */
struct kbase_csf_tiler_heap_context {
	struct mutex lock;
	struct list_head list;
	struct kbase_csf_heap_context_allocator ctx_alloc;
};

/**
 * struct kbase_csf_scheduler_context - Object representing the scheduler's
 *                                      context for a GPU address space.
 *
 * @runnable_groups:    Lists of runnable GPU command queue groups in the kctx,
 *                      one per queue group priority level.
 * @num_runnable_grps:  Total number of runnable groups across all priority
 *                      levels in @runnable_groups.
 * @idle_wait_groups:   A list of GPU command queue groups in which all enabled
 *                      GPU command queues are idle and at least one of them
 *                      is blocked on a sync wait operation.
 * @num_idle_wait_grps: Length of the @idle_wait_groups list.
 * @sync_update_wq:     Dedicated workqueue to process work items corresponding
 *                      to the sync_update events by sync_set/sync_add
 *                      instruction execution on command streams bound to groups
 *                      of @idle_wait_groups list.
 * @sync_update_work:   work item to process the sync_update events by
 *                      sync_set / sync_add instruction execution on command
 *                      streams bound to groups of @idle_wait_groups list.
 * @ngrp_to_schedule:	Number of groups added for the context to the
 *                      'groups_to_schedule' list of scheduler instance.
 */
struct kbase_csf_scheduler_context {
	struct list_head runnable_groups[BASE_QUEUE_GROUP_PRIORITY_COUNT];
	u32 num_runnable_grps;
	struct list_head idle_wait_groups;
	u32 num_idle_wait_grps;
	struct workqueue_struct *sync_update_wq;
	struct work_struct sync_update_work;
	u32 ngrp_to_schedule;
};

/**
 * struct kbase_csf_context - Object representing command-stream front-end
 *                            for a GPU address space.
 *
 * @event_pages_head: A list of pages allocated for the event memory used by
 *                    the synchronization objects. A separate list would help
 *                    in the fast lookup, since the list is expected to be short
 *                    as one page would provide the memory for up to 1K
 *                    synchronization objects.
 *                    KBASE_PERMANENTLY_MAPPED_MEM_LIMIT_PAGES is the upper
 *                    bound on the size of event memory.
 * @cookies:          Bitmask containing of KBASE_CSF_NUM_USER_IO_PAGES_HANDLE
 *                    bits, used for creating the User mode CPU mapping in a
 *                    deferred manner of a pair of User mode input/output pages
 *                    & a hardware doorbell page.
 *                    The pages are allocated when a GPU command queue is
 *                    bound to a command stream group in kbase_csf_queue_bind.
 *                    This helps returning unique handles to Userspace from
 *                    kbase_csf_queue_bind and later retrieving the pointer to
 *                    queue in the mmap handler.
 * @user_pages_info:  Array containing pointers to queue
 *                    structures, used in conjunction with cookies bitmask for
 *                    providing a mechansim to create a CPU mapping of
 *                    input/output pages & hardware doorbell page.
 * @lock:             Serializes accesses to all members, except for ones that
 *                    have their own locks.
 * @queue_groups:     Array of registered GPU command queue groups.
 * @queue_list:       Linked list of GPU command queues not yet deregistered.
 *                    Note that queues can persist after deregistration if the
 *                    userspace mapping created for them on bind operation
 *                    hasn't been removed.
 * @kcpu_queues:      Kernel CPU command queues.
 * @event_lock:       Lock protecting access to @event_callback_list
 * @event_callback_list: List of callbacks which are registered to serve CSF
 *                       events.
 * @tiler_heaps:      Chunked tiler memory heaps.
 * @wq:               Dedicated workqueue to process work items corresponding
 *                    to the OoM events raised for chunked tiler heaps being
 *                    used by GPU command queues, and progress timeout events.
 * @link:             Link to this csf context in the 'runnable_kctxs' list of
 *                    the scheduler instance
 * @user_reg_vma:     Pointer to the vma corresponding to the virtual mapping
 *                    of the USER register page. Currently used only for sanity
 *                    checking.
 * @sched:            Object representing the scheduler's context
 * @error_list:       List for command stream fatal errors in this context.
 *                    Link of fatal error is
 *                    &struct_kbase_csf_notification.link.
 *                    @lock needs to be held to access to this list.
 */
struct kbase_csf_context {
	struct list_head event_pages_head;
	DECLARE_BITMAP(cookies, KBASE_CSF_NUM_USER_IO_PAGES_HANDLE);
	struct kbase_queue *user_pages_info[
		KBASE_CSF_NUM_USER_IO_PAGES_HANDLE];
	struct mutex lock;
	struct kbase_queue_group *queue_groups[MAX_QUEUE_GROUP_NUM];
	struct list_head queue_list;
	struct kbase_csf_kcpu_queue_context kcpu_queues;
	spinlock_t event_lock;
	struct list_head event_callback_list;
	struct kbase_csf_tiler_heap_context tiler_heaps;
	struct workqueue_struct *wq;
	struct list_head link;
	struct vm_area_struct *user_reg_vma;
	struct kbase_csf_scheduler_context sched;
	struct list_head error_list;
};

/**
 * struct kbase_csf_reset_gpu - Object containing the members required for
 *                            GPU reset handling.
 * @workq:         Workqueue to execute the GPU reset work item @work.
 * @work:          Work item for performing the GPU reset.
 * @wait:          Wait queue used to wait for the GPU reset completion.
 * @state:         Tracks if the GPU reset is in progress or not.
 */
struct kbase_csf_reset_gpu {
	struct workqueue_struct *workq;
	struct work_struct work;
	wait_queue_head_t wait;
	atomic_t state;
};

/**
 * struct kbase_csf_csg_slot - Object containing members for tracking the state
 *                             of command stream group slots.
 * @resident_group:   pointer to the queue group that is resident on the
 *                    command stream group slot.
 * @state:            state of the slot as per enum kbase_csf_csg_slot_state.
 * @trigger_jiffies:  value of jiffies when change in slot state is recorded.
 * @priority:         dynamic priority assigned to command stream group slot.
 */
struct kbase_csf_csg_slot {
	struct kbase_queue_group *resident_group;
	atomic_t state;
	unsigned long trigger_jiffies;
	u8 priority;
};

/**
 * struct kbase_csf_scheduler - Object representing the scheduler used for
 *                              command-stream front-end for an instance of
 *                              GPU platform device.
 * @lock:                  Lock to serialize the scheduler operations and
 *                         access to the data members.
 * @interrupt_lock:        Lock to protect members accessed by interrupt
 *                         handler.
 * @state:                 The operational phase the scheduler is in. Primarily
 *                         used for indicating what in-cycle schedule actions
 *                         are allowed.
 * @doorbell_inuse_bitmap: Bitmap of hardware doorbell pages keeping track of
 *                         which pages are currently available for assignment
 *                         to clients.
 * @csg_inuse_bitmap:      Bitmap to keep a track of command stream group slots
 *                         that are currently in use.
 * @csg_slots:             The array for tracking the state of command stream
 *                         group slots.
 * @runnable_kctxs:        List of Kbase contexts that have runnable command
 *                         queue groups.
 * @groups_to_schedule:    List of runnable queue groups prepared on every
 *                         scheduler tick. The dynamic priority of the command
 *                         stream group slot assigned to a group will depend
 *                         upon the position of group in the list.
 * @ngrp_to_schedule:      Number of groups in the @groups_to_schedule list,
 *                         incremented when a group is added to the list, used
 *                         to record the position of group in the list.
 * @num_active_address_spaces: Number of GPU address space slots that would get
 *                             used to program the groups in @groups_to_schedule
 *                             list on all the available command stream group
 *                             slots.
 * @num_csg_slots_for_tick:  Number of command stream group slots that can be
 *                           active in the given tick/tock. This depends on the
 *                           value of @num_active_address_spaces.
 * @idle_groups_to_schedule: List of runnable queue groups, in which all GPU
 *                           command queues became idle or are waiting for
 *                           synchronization object, prepared on every
 *                           scheduler tick. The groups in this list are
 *                           appended to the tail of @groups_to_schedule list
 *                           after the scan out so that the idle groups aren't
 *                           preferred for scheduling over the non-idle ones.
 * @total_runnable_grps:     Total number of runnable groups across all KCTXs.
 * @csgs_events_enable_mask: Use for temporary masking off asynchronous events
 *                           from firmware (such as OoM events) before a group
 *                           is suspended.
 * @csg_slots_idle_mask:     Bit array for storing the mask of command stream
 *                           group slots for which idle notification was
 *                           received.
 * @csg_slots_prio_update:  Bit array for tracking slots that have an on-slot
 *                          priority update operation.
 * @last_schedule:          Time in jiffies recorded when the last "tick" or
 *                          "tock" schedule operation concluded. Used for
 *                          evaluating the exclusion window for in-cycle
 *                          schedule operation.
 * @timer_enabled:          Whether the CSF scheduler wakes itself up for
 *                          periodic scheduling tasks. If this value is 0
 *                          then it will only perform scheduling under the
 *                          influence of external factors e.g., IRQs, IOCTLs.
 * @wq:                     Dedicated workqueue to execute the @tick_work.
 * @tick_work:              Work item that would perform the schedule on tick
 *                          operation to implement the time slice based
 *                          scheduling.
 * @tock_work:              Work item that would perform the schedule on tock
 *                          operation to implement the asynchronous scheduling.
 * @ping_work:              Work item that would ping the firmware at regular
 *                          intervals, only if there is a single active command
 *                          stream group slot, to check if firmware is alive
 *                          and would initiate a reset if the ping request
 *                          isn't acknowledged.
 * @top_ctx:                Pointer to the Kbase context corresponding to the
 *                          @top_grp.
 * @top_grp:                Pointer to queue group inside @groups_to_schedule
 *                          list that was assigned the highest slot priority.
 * @head_slot_priority:     The dynamic slot priority to be used for the
 *                          queue group at the head of @groups_to_schedule
 *                          list. Once the queue group is assigned a command
 *                          stream group slot, it is removed from the list and
 *                          priority is decremented.
 * @tock_pending_request:   A "tock" request is pending: a group that is not
 *                          currently on the GPU demands to be scheduled.
 * @active_protm_grp:       Indicates if firmware has been permitted to let GPU
 *                          enter protected mode with the given group. On exit
 *                          from protected mode the pointer is reset to NULL.
 * @gpu_idle_work:          Work item for facilitating the scheduler to bring
 *                          the GPU to a low-power mode on becoming idle.
 * @non_idle_suspended_grps: Count of suspended queue groups not idle.
 * @pm_active_count:        Count indicating if the scheduler is owning a power
 *                          management reference count. Reference is taken when
 *                          the count becomes 1 and is dropped when the count
 *                          becomes 0. It is used to enable the power up of MCU
 *                          after GPU and L2 cache have been powered up. So when
 *                          this count is zero, MCU will not be powered up.
 */
struct kbase_csf_scheduler {
	struct mutex lock;
	spinlock_t interrupt_lock;
	enum kbase_csf_scheduler_state state;
	DECLARE_BITMAP(doorbell_inuse_bitmap, CSF_NUM_DOORBELL);
	DECLARE_BITMAP(csg_inuse_bitmap, MAX_SUPPORTED_CSGS);
	struct kbase_csf_csg_slot *csg_slots;
	struct list_head runnable_kctxs;
	struct list_head groups_to_schedule;
	u32 ngrp_to_schedule;
	u32 num_active_address_spaces;
	u32 num_csg_slots_for_tick;
	struct list_head idle_groups_to_schedule;
	u32 total_runnable_grps;
	DECLARE_BITMAP(csgs_events_enable_mask, MAX_SUPPORTED_CSGS);
	DECLARE_BITMAP(csg_slots_idle_mask, MAX_SUPPORTED_CSGS);
	DECLARE_BITMAP(csg_slots_prio_update, MAX_SUPPORTED_CSGS);
	unsigned long last_schedule;
	bool timer_enabled;
	struct workqueue_struct *wq;
	struct delayed_work tick_work;
	struct delayed_work tock_work;
	struct delayed_work ping_work;
	struct kbase_context *top_ctx;
	struct kbase_queue_group *top_grp;
	u8 head_slot_priority;
	bool tock_pending_request;
	struct kbase_queue_group *active_protm_grp;
	struct delayed_work gpu_idle_work;
	atomic_t non_idle_suspended_grps;
	u32 pm_active_count;
};

/**
 * Number of GPU cycles per unit of the global progress timeout.
 */
#define GLB_PROGRESS_TIMER_TIMEOUT_SCALE ((u64)1024)

/**
 * Maximum value of the global progress timeout.
 */
#define GLB_PROGRESS_TIMER_TIMEOUT_MAX \
	((GLB_PROGRESS_TIMER_TIMEOUT_MASK >> \
		GLB_PROGRESS_TIMER_TIMEOUT_SHIFT) * \
	GLB_PROGRESS_TIMER_TIMEOUT_SCALE)

/**
 * struct kbase_csf      -  Object representing command-stream front-end for an
 *                          instance of GPU platform device.
 *
 * @mcu_mmu:                MMU page tables for the MCU firmware
 * @firmware_interfaces:    List of interfaces defined in the firmware image
 * @firmware_config:        List of configuration options within the firmware
 *                          image
 * @firmware_timeline_metadata: List of timeline meta-data within the firmware
 *                          image
 * @fw_cfg_kobj:            Pointer to the kobject corresponding to the sysf
 *                          directory that contains a sub-directory for each
 *                          of the configuration option present in the
 *                          firmware image.
 * @firmware_trace_buffers: List of trace buffers described in the firmware
 *                          image.
 * @shared_interface:       Pointer to the interface object containing info for
 *                          the memory area shared between firmware & host.
 * @shared_reg_rbtree:      RB tree of the memory regions allocated from the
 *                          shared interface segment in MCU firmware address
 *                          space.
 * @db_filp:                Pointer to a dummy file, that alongwith
 *                          @db_file_offsets, facilitates the use of unqiue
 *                          file offset for the userspace mapping created
 *                          for Hw Doorbell pages. The userspace mapping
 *                          is made to point to this file inside the mmap
 *                          handler.
 * @db_file_offsets:        Counter that is incremented every time a GPU
 *                          command queue is bound to provide a unique file
 *                          offset range for @db_filp file, so that pte of
 *                          Doorbell page can be zapped through the kernel
 *                          function unmap_mapping_range(). It is incremented
 *                          in page units.
 * @dummy_db_page:          Address of the dummy page that is mapped in place
 *                          of the real Hw doorbell page for the active GPU
 *                          command queues after they are stopped or after the
 *                          GPU is powered down.
 * @reg_lock:               Lock to serialize the MCU firmware related actions
 *                          that affect all contexts such as allocation of
 *                          regions from shared interface area, assignment of
 *                          of hardware doorbell pages, assignment of CSGs,
 *                          sending global requests.
 * @event_wait:             Wait queue to wait for receiving csf events, i.e.
 *                          the interrupt from CSF firmware, or scheduler state
 *                          changes.
 * @interrupt_received:     Flag set when the interrupt is received from CSF fw
 * @global_iface:           The result of parsing the global interface
 *                          structure set up by the firmware, including the
 *                          CSGs, CSs, and their properties
 * @scheduler:              The command stream scheduler instance.
 * @reset:                  Contain members required for GPU reset handling.
 * @progress_timeout:       Maximum number of GPU clock cycles without forward
 *                          progress to allow, for all tasks running on
 *                          hardware endpoints (e.g. shader cores), before
 *                          terminating a GPU command queue group.
 *                          Must not exceed @GLB_PROGRESS_TIMER_TIMEOUT_MAX.
 * @pma_dev:                Pointer to protected memory allocator device.
 * @firmware_inited:        Flag for indicating that the cold-boot stage of
 *                          the MCU has completed.
 * @firmware_reloaded:      Flag for indicating a firmware reload operation
 *                          in GPU reset has completed.
 * @firmware_reload_needed: Flag for indicating that the firmware needs to be
 *                          reloaded as part of the GPU reset action.
 * @firmware_reload_work:   Work item for facilitating the procedural actions
 *                          on reloading the firmware.
 * @glb_init_request_pending: Flag to indicate that Global requests have been
 *                            sent to the FW after MCU was re-enabled and their
 *                            acknowledgement is pending.
 */
struct kbase_csf_device {
	struct kbase_mmu_table mcu_mmu;
	struct list_head firmware_interfaces;
	struct list_head firmware_config;
	struct list_head firmware_timeline_metadata;
	struct kobject *fw_cfg_kobj;
	struct kbase_csf_trace_buffers firmware_trace_buffers;
	void *shared_interface;
	struct rb_root shared_reg_rbtree;
	struct file *db_filp;
	u32 db_file_offsets;
	struct tagged_addr dummy_db_page;
	struct mutex reg_lock;
	wait_queue_head_t event_wait;
	bool interrupt_received;
	struct kbase_csf_global_iface global_iface;
	struct kbase_csf_scheduler scheduler;
	struct kbase_csf_reset_gpu reset;
	atomic64_t progress_timeout;
	struct protected_memory_allocator_device *pma_dev;
	bool firmware_inited;
	bool firmware_reloaded;
	bool firmware_reload_needed;
	struct work_struct firmware_reload_work;
	bool glb_init_request_pending;
};

/**
 * struct kbase_as   - Object representing an address space of GPU.
 * @number:            Index at which this address space structure is present
 *                     in an array of address space structures embedded inside
 *                     the &struct kbase_device.
 * @pf_wq:             Workqueue for processing work items related to
 *                     Page fault, Bus fault and GPU fault handling.
 * @work_pagefault:    Work item for the Page fault handling.
 * @work_busfault:     Work item for the Bus fault handling.
 * @work_gpufault:     Work item for the GPU fault handling.
 * @pf_data:           Data relating to Page fault.
 * @bf_data:           Data relating to Bus fault.
 * @gf_data:           Data relating to GPU fault.
 * @current_setup:     Stores the MMU configuration for this address space.
 */
struct kbase_as {
	int number;
	struct workqueue_struct *pf_wq;
	struct work_struct work_pagefault;
	struct work_struct work_busfault;
	struct work_struct work_gpufault;
	struct kbase_fault pf_data;
	struct kbase_fault bf_data;
	struct kbase_fault gf_data;
	struct kbase_mmu_setup current_setup;
};

#endif /* _KBASE_CSF_DEFS_H_ */
