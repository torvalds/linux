/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2018-2022 ARM Limited. All rights reserved.
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

/* Definitions (types, defines, etcs) common to the CSF.
 * They are placed here to allow the hierarchy of header files to work.
 */

#ifndef _KBASE_CSF_DEFS_H_
#define _KBASE_CSF_DEFS_H_

#include <linux/types.h>
#include <linux/wait.h>

#include "mali_kbase_csf_firmware.h"
#include "mali_kbase_csf_event.h"

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

#define CSF_FIRMWARE_ENTRY_READ       (1ul << 0)
#define CSF_FIRMWARE_ENTRY_WRITE      (1ul << 1)
#define CSF_FIRMWARE_ENTRY_EXECUTE    (1ul << 2)
#define CSF_FIRMWARE_ENTRY_CACHE_MODE (3ul << 3)
#define CSF_FIRMWARE_ENTRY_PROTECTED  (1ul << 5)
#define CSF_FIRMWARE_ENTRY_SHARED     (1ul << 30)
#define CSF_FIRMWARE_ENTRY_ZERO       (1ul << 31)

/**
 * enum kbase_csf_queue_bind_state - bind state of the queue
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
 *
 * @KBASE_CSF_RESET_GPU_PREPARED: Set when kbase_prepare_to_reset_gpu() has
 * been called. This is just for debugging checks to encourage callers to call
 * kbase_prepare_to_reset_gpu() before kbase_reset_gpu().
 *
 * @KBASE_CSF_RESET_GPU_COMMITTED: Set when the GPU reset process has been
 * committed and so will definitely happen, but the procedure to reset the GPU
 * has not yet begun. Other threads must finish accessing the HW before we
 * reach %KBASE_CSF_RESET_GPU_HAPPENING.
 *
 * @KBASE_CSF_RESET_GPU_HAPPENING: Set when the GPU reset process is occurring
 * (silent or otherwise), and is actively accessing the HW. Any changes to the
 * HW in other threads might get lost, overridden, or corrupted.
 *
 * @KBASE_CSF_RESET_GPU_COMMITTED_SILENT: Set when the GPU reset process has
 * been committed but has not started happening. This is used when resetting
 * the GPU as part of normal behavior (e.g. when exiting protected mode).
 * Other threads must finish accessing the HW before we reach
 * %KBASE_CSF_RESET_GPU_HAPPENING.
 *
 * @KBASE_CSF_RESET_GPU_FAILED: Set when an error is encountered during the
 * GPU reset process. No more work could then be executed on GPU, unloading
 * the Driver module is the only option.
 */
enum kbase_csf_reset_gpu_state {
	KBASE_CSF_RESET_GPU_NOT_PENDING,
	KBASE_CSF_RESET_GPU_PREPARED,
	KBASE_CSF_RESET_GPU_COMMITTED,
	KBASE_CSF_RESET_GPU_HAPPENING,
	KBASE_CSF_RESET_GPU_COMMITTED_SILENT,
	KBASE_CSF_RESET_GPU_FAILED,
};

/**
 * enum kbase_csf_group_state - state of the GPU command queue group
 *
 * @KBASE_CSF_GROUP_INACTIVE:          Group is inactive and won't be
 *                                     considered by scheduler for running on
 *                                     CSG slot.
 * @KBASE_CSF_GROUP_RUNNABLE:          Group is in the list of runnable groups
 *                                     and is subjected to time-slice based
 *                                     scheduling. A start request would be
 *                                     sent (or already has been sent) if the
 *                                     group is assigned the CS
 *                                     group slot for the fist time.
 * @KBASE_CSF_GROUP_IDLE:              Group is currently on a CSG slot
 *                                     but all the CSs bound to the group have
 *                                     become either idle or waiting on sync
 *                                     object.
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
 * @KBASE_CSF_GROUP_SUSPENDED:         Group was evicted from the CSG slot
 *                                     and is not running but is still in the
 *                                     list of runnable groups and subjected
 *                                     to time-slice based scheduling. A resume
 *                                     request would be sent when a CSG slot is
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
 *                                          except that at least one CS
 *                                          bound to this group was
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
 *                      operations, the state of CSG slots
 *                      can't be changed.
 * @SCHED_INACTIVE:     The scheduler is inactive, it is allowed to modify the
 *                      state of CSG slots by in-cycle
 *                      priority scheduling.
 * @SCHED_SUSPENDED:    The scheduler is in low-power mode with scheduling
 *                      operations suspended and is not holding the power
 *                      management reference. This can happen if the GPU
 *                      becomes idle for a duration exceeding a threshold,
 *                      or due to a system triggered suspend action.
 * @SCHED_SLEEPING:     The scheduler is in low-power mode with scheduling
 *                      operations suspended and is not holding the power
 *                      management reference. This state is set, only for the
 *                      GPUs that supports the sleep feature, when GPU idle
 *                      notification is received. The state is changed to
 *                      @SCHED_SUSPENDED from the runtime suspend callback
 *                      function after the suspend of CSGs.
 */
enum kbase_csf_scheduler_state {
	SCHED_BUSY,
	SCHED_INACTIVE,
	SCHED_SUSPENDED,
	SCHED_SLEEPING,
};

/**
 * enum kbase_queue_group_priority - Kbase internal relative priority list.
 *
 * @KBASE_QUEUE_GROUP_PRIORITY_REALTIME:  The realtime queue group priority.
 * @KBASE_QUEUE_GROUP_PRIORITY_HIGH:      The high queue group priority.
 * @KBASE_QUEUE_GROUP_PRIORITY_MEDIUM:    The medium queue group priority.
 * @KBASE_QUEUE_GROUP_PRIORITY_LOW:       The low queue group priority.
 * @KBASE_QUEUE_GROUP_PRIORITY_COUNT:     The number of priority levels.
 */
enum kbase_queue_group_priority {
	KBASE_QUEUE_GROUP_PRIORITY_REALTIME = 0,
	KBASE_QUEUE_GROUP_PRIORITY_HIGH,
	KBASE_QUEUE_GROUP_PRIORITY_MEDIUM,
	KBASE_QUEUE_GROUP_PRIORITY_LOW,
	KBASE_QUEUE_GROUP_PRIORITY_COUNT
};

/**
 * enum kbase_timeout_selector - The choice of which timeout to get scaled
 *                               using the lowest GPU frequency.
 * @CSF_FIRMWARE_TIMEOUT: Response timeout from CSF firmware.
 * @CSF_PM_TIMEOUT: Timeout for GPU Power Management to reach the desired
 *                  Shader, L2 and MCU state.
 * @CSF_GPU_RESET_TIMEOUT: Waiting timeout for GPU reset to complete.
 * @CSF_CSG_SUSPEND_TIMEOUT: Timeout given for all active CSGs to be suspended.
 * @CSF_FIRMWARE_BOOT_TIMEOUT: Maximum time to wait for firmware to boot.
 * @CSF_SCHED_PROTM_PROGRESS_TIMEOUT: Timeout used to prevent protected mode execution hang.
 * @KBASE_TIMEOUT_SELECTOR_COUNT: Number of timeout selectors. Must be last in
 *                                the enum.
 */
enum kbase_timeout_selector {
	CSF_FIRMWARE_TIMEOUT,
	CSF_PM_TIMEOUT,
	CSF_GPU_RESET_TIMEOUT,
	CSF_CSG_SUSPEND_TIMEOUT,
	CSF_FIRMWARE_BOOT_TIMEOUT,
	CSF_SCHED_PROTM_PROGRESS_TIMEOUT,

	/* Must be the last in the enum */
	KBASE_TIMEOUT_SELECTOR_COUNT
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
 *               @oom_event_work work item is queued
 *               for it.
 * @group:       Pointer to the group to which this queue is bound.
 * @queue_reg:   Pointer to the VA region allocated for CS buffer.
 * @oom_event_work: Work item corresponding to the out of memory event for
 *                  chunked tiler heap being used for this queue.
 * @base_addr:      Base address of the CS buffer.
 * @size:           Size of the CS buffer.
 * @priority:       Priority of this queue within the group.
 * @bind_state:     Bind state of the queue as enum @kbase_csf_queue_bind_state
 * @csi_index:      The ID of the assigned CS hardware interface.
 * @enabled:        Indicating whether the CS is running, or not.
 * @status_wait:    Value of CS_STATUS_WAIT register of the CS will
 *                  be kept when the CS gets blocked by sync wait.
 *                  CS_STATUS_WAIT provides information on conditions queue is
 *                  blocking on. This is set when the group, to which queue is
 *                  bound, is suspended after getting blocked, i.e. in
 *                  KBASE_CSF_GROUP_SUSPENDED_ON_WAIT_SYNC state.
 * @sync_ptr:       Value of CS_STATUS_WAIT_SYNC_POINTER register of the CS
 *                  will be kept when the CS gets blocked by
 *                  sync wait. CS_STATUS_WAIT_SYNC_POINTER contains the address
 *                  of synchronization object being waited on.
 *                  Valid only when @status_wait is set.
 * @sync_value:     Value of CS_STATUS_WAIT_SYNC_VALUE register of the CS
 *                  will be kept when the CS gets blocked by
 *                  sync wait. CS_STATUS_WAIT_SYNC_VALUE contains the value
 *                  tested against the synchronization object.
 *                  Valid only when @status_wait is set.
 * @sb_status:      Value indicates which of the scoreboard entries in the queue
 *                  are non-zero
 * @blocked_reason: Value shows if the queue is blocked, and if so,
 *                  the reason why it is blocked
 * @trace_buffer_base: CS trace buffer base address.
 * @trace_offset_ptr:  Pointer to the CS trace buffer offset variable.
 * @trace_buffer_size: CS trace buffer size for the queue.
 * @trace_cfg:         CS trace configuration parameters.
 * @error:          GPU command queue fatal information to pass to user space.
 * @fatal_event_work: Work item to handle the CS fatal event reported for this
 *                    queue.
 * @cs_fatal_info:    Records additional information about the CS fatal event.
 * @cs_fatal:         Records information about the CS fatal event.
 * @pending:          Indicating whether the queue has new submitted work.
 * @extract_ofs: The current EXTRACT offset, this is updated during certain
 *               events such as GPU idle IRQ in order to help detect a
 *               queue's true idle status.
 * @saved_cmd_ptr: The command pointer value for the GPU queue, saved when the
 *                 group to which queue is bound is suspended.
 *                 This can be useful in certain cases to know that till which
 *                 point the execution reached in the Linear command buffer.
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
	u64 base_addr;
	u32 size;
	u8 priority;
	s8 csi_index;
	enum kbase_csf_queue_bind_state bind_state;
	bool enabled;
	u32 status_wait;
	u64 sync_ptr;
	u32 sync_value;
	u32 sb_status;
	u32 blocked_reason;
	u64 trace_buffer_base;
	u64 trace_offset_ptr;
	u32 trace_buffer_size;
	u32 trace_cfg;
	struct kbase_csf_notification error;
	struct work_struct fatal_event_work;
	u64 cs_fatal_info;
	u32 cs_fatal;
	atomic_t pending;
	u64 extract_ofs;
#if IS_ENABLED(CONFIG_DEBUG_FS)
	u64 saved_cmd_ptr;
#endif
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
 * @csg_nr:         Number/index of the CSG to which this queue group is
 *                  mapped; KBASEP_CSG_NR_INVALID indicates that the queue
 *                  group is not scheduled.
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
 * @group_uid:      32-bit wide unsigned identifier for the group, unique
 *                  across all kbase devices and contexts.
 * @link:           Link to this queue group in the 'runnable_groups' list of
 *                  the corresponding kctx.
 * @link_to_schedule: Link to this queue group in the list of prepared groups
 *                    to be scheduled, if the group is runnable/suspended.
 *                    If the group is idle or waiting for CQS, it would be a
 *                    link to the list of idle/blocked groups list.
 * @run_state:      Current state of the queue group.
 * @prepared_seq_num: Indicates the position of queue group in the list of
 *                    prepared groups to be scheduled.
 * @scan_seq_num:     Scan out sequence number before adjusting for dynamic
 *                    idle conditions. It is used for setting a group's
 *                    onslot priority. It could differ from prepared_seq_number
 *                    when there are idle groups.
 * @faulted:          Indicates that a GPU fault occurred for the queue group.
 *                    This flag persists until the fault has been queued to be
 *                    reported to userspace.
 * @bound_queues:   Array of registered queues bound to this queue group.
 * @doorbell_nr:    Index of the hardware doorbell page assigned to the
 *                  group.
 * @protm_event_work:   Work item corresponding to the protected mode entry
 *                      event for this queue.
 * @protm_pending_bitmap:  Bit array to keep a track of CSs that
 *                         have pending protected mode entry requests.
 * @error_fatal: An error of type BASE_GPU_QUEUE_GROUP_ERROR_FATAL to be
 *               returned to userspace if such an error has occurred.
 * @error_timeout: An error of type BASE_GPU_QUEUE_GROUP_ERROR_TIMEOUT
 *                 to be returned to userspace if such an error has occurred.
 * @error_tiler_oom: An error of type BASE_GPU_QUEUE_GROUP_ERROR_TILER_HEAP_OOM
 *                   to be returned to userspace if such an error has occurred.
 * @timer_event_work: Work item to handle the progress timeout fatal event
 *                    for the group.
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

	u32 group_uid;

	struct list_head link;
	struct list_head link_to_schedule;
	enum kbase_csf_group_state run_state;
	u32 prepared_seq_num;
	u32 scan_seq_num;
	bool faulted;

	struct kbase_queue *bound_queues[MAX_SUPPORTED_STREAMS_PER_GROUP];

	int doorbell_nr;
	struct work_struct protm_event_work;
	DECLARE_BITMAP(protm_pending_bitmap, MAX_SUPPORTED_STREAMS_PER_GROUP);

	struct kbase_csf_notification error_fatal;
	struct kbase_csf_notification error_timeout;
	struct kbase_csf_notification error_tiler_oom;

	struct work_struct timer_event_work;

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
 * struct kbase_csf_cpu_queue_context - Object representing the cpu queue
 *                                      information.
 *
 * @buffer:     Buffer containing CPU queue information provided by Userspace.
 * @buffer_size: The size of @buffer.
 * @dump_req_status:  Indicates the current status for CPU queues dump request.
 * @dump_cmp:         Dumping cpu queue completion event.
 */
struct kbase_csf_cpu_queue_context {
	char *buffer;
	size_t buffer_size;
	atomic_t dump_req_status;
	struct completion dump_cmp;
};

/**
 * struct kbase_csf_heap_context_allocator - Allocator of heap contexts
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
 *
 * Heap context structures are allocated by the kernel for use by the firmware.
 * The current implementation subdivides a single GPU memory region for use as
 * a sparse array.
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
 * @lock:        Lock to prevent the concurrent access to tiler heaps (after the
 *               initialization), a tiler heap can be terminated whilst an OoM
 *               event is being handled for it.
 * @list:        List of tiler heaps.
 * @ctx_alloc:   Allocator for heap context structures.
 * @nr_of_heaps: Total number of tiler heaps that were added during the
 *               life time of the context.
 *
 * This contains all of the CSF state relating to chunked tiler heaps for one
 * @kbase_context. It is not the same as a heap context structure allocated by
 * the kernel for use by the firmware.
 */
struct kbase_csf_tiler_heap_context {
	struct mutex lock;
	struct list_head list;
	struct kbase_csf_heap_context_allocator ctx_alloc;
	u64 nr_of_heaps;
};

/**
 * struct kbase_csf_scheduler_context - Object representing the scheduler's
 *                                      context for a GPU address space.
 *
 * @runnable_groups:    Lists of runnable GPU command queue groups in the kctx,
 *                      one per queue group  relative-priority level.
 * @num_runnable_grps:  Total number of runnable groups across all priority
 *                      levels in @runnable_groups.
 * @idle_wait_groups:   A list of GPU command queue groups in which all enabled
 *                      GPU command queues are idle and at least one of them
 *                      is blocked on a sync wait operation.
 * @num_idle_wait_grps: Length of the @idle_wait_groups list.
 * @sync_update_wq:     Dedicated workqueue to process work items corresponding
 *                      to the sync_update events by sync_set/sync_add
 *                      instruction execution on CSs bound to groups
 *                      of @idle_wait_groups list.
 * @sync_update_work:   work item to process the sync_update events by
 *                      sync_set / sync_add instruction execution on command
 *                      streams bound to groups of @idle_wait_groups list.
 * @ngrp_to_schedule:	Number of groups added for the context to the
 *                      'groups_to_schedule' list of scheduler instance.
 */
struct kbase_csf_scheduler_context {
	struct list_head runnable_groups[KBASE_QUEUE_GROUP_PRIORITY_COUNT];
	u32 num_runnable_grps;
	struct list_head idle_wait_groups;
	u32 num_idle_wait_grps;
	struct workqueue_struct *sync_update_wq;
	struct work_struct sync_update_work;
	u32 ngrp_to_schedule;
};

/**
 * enum kbase_csf_event_callback_action - return type for CSF event callbacks.
 *
 * @KBASE_CSF_EVENT_CALLBACK_FIRST: Never set explicitly.
 * It doesn't correspond to any action or type of event callback.
 *
 * @KBASE_CSF_EVENT_CALLBACK_KEEP: The callback will remain registered.
 *
 * @KBASE_CSF_EVENT_CALLBACK_REMOVE: The callback will be removed
 * immediately upon return.
 *
 * @KBASE_CSF_EVENT_CALLBACK_LAST: Never set explicitly.
 * It doesn't correspond to any action or type of event callback.
 */
enum kbase_csf_event_callback_action {
	KBASE_CSF_EVENT_CALLBACK_FIRST = 0,
	KBASE_CSF_EVENT_CALLBACK_KEEP,
	KBASE_CSF_EVENT_CALLBACK_REMOVE,
	KBASE_CSF_EVENT_CALLBACK_LAST,
};

/**
 * struct kbase_csf_event - Object representing CSF event and error
 *
 * @callback_list:	List of callbacks which are registered to serve CSF
 *			events.
 * @error_list:		List for CS fatal errors in CSF context.
 *			Link of fatal error is &struct_kbase_csf_notification.link.
 * @lock:		Lock protecting access to @callback_list and
 *			@error_list.
 */
struct kbase_csf_event {
	struct list_head callback_list;
	struct list_head error_list;
	spinlock_t lock;
};

/**
 * struct kbase_csf_context - Object representing CSF for a GPU address space.
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
 *                    bound to a CSG in kbase_csf_queue_bind.
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
 * @event:            CSF event object.
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
 * @pending_submission_work: Work item to process pending kicked GPU command queues.
 * @cpu_queue:        CPU queue information. Only be available when DEBUG_FS
 *                    is enabled.
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
	struct kbase_csf_event event;
	struct kbase_csf_tiler_heap_context tiler_heaps;
	struct workqueue_struct *wq;
	struct list_head link;
	struct vm_area_struct *user_reg_vma;
	struct kbase_csf_scheduler_context sched;
	struct work_struct pending_submission_work;
#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct kbase_csf_cpu_queue_context cpu_queue;
#endif
};

/**
 * struct kbase_csf_reset_gpu - Object containing the members required for
 *                            GPU reset handling.
 * @workq:         Workqueue to execute the GPU reset work item @work.
 * @work:          Work item for performing the GPU reset.
 * @wait:          Wait queue used to wait for the GPU reset completion.
 * @sem:           RW Semaphore to ensure no other thread attempts to use the
 *                 GPU whilst a reset is in process. Unlike traditional
 *                 semaphores and wait queues, this allows Linux's lockdep
 *                 mechanism to check for deadlocks involving reset waits.
 * @state:         Tracks if the GPU reset is in progress or not.
 *                 The state is represented by enum @kbase_csf_reset_gpu_state.
 */
struct kbase_csf_reset_gpu {
	struct workqueue_struct *workq;
	struct work_struct work;
	wait_queue_head_t wait;
	struct rw_semaphore sem;
	atomic_t state;
};

/**
 * struct kbase_csf_csg_slot - Object containing members for tracking the state
 *                             of CSG slots.
 * @resident_group:   pointer to the queue group that is resident on the CSG slot.
 * @state:            state of the slot as per enum @kbase_csf_csg_slot_state.
 * @trigger_jiffies:  value of jiffies when change in slot state is recorded.
 * @priority:         dynamic priority assigned to CSG slot.
 */
struct kbase_csf_csg_slot {
	struct kbase_queue_group *resident_group;
	atomic_t state;
	unsigned long trigger_jiffies;
	u8 priority;
};

/**
 * struct kbase_csf_scheduler - Object representing the scheduler used for
 *                              CSF for an instance of GPU platform device.
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
 * @csg_inuse_bitmap:      Bitmap to keep a track of CSG slots
 *                         that are currently in use.
 * @csg_slots:             The array for tracking the state of CS
 *                         group slots.
 * @runnable_kctxs:        List of Kbase contexts that have runnable command
 *                         queue groups.
 * @groups_to_schedule:    List of runnable queue groups prepared on every
 *                         scheduler tick. The dynamic priority of the CSG
 *                         slot assigned to a group will depend upon the
 *                         position of group in the list.
 * @ngrp_to_schedule:      Number of groups in the @groups_to_schedule list,
 *                         incremented when a group is added to the list, used
 *                         to record the position of group in the list.
 * @num_active_address_spaces: Number of GPU address space slots that would get
 *                             used to program the groups in @groups_to_schedule
 *                             list on all the available CSG
 *                             slots.
 * @num_csg_slots_for_tick:  Number of CSG slots that can be
 *                           active in the given tick/tock. This depends on the
 *                           value of @num_active_address_spaces.
 * @remaining_tick_slots:    Tracking the number of remaining available slots
 *                           for @num_csg_slots_for_tick during the scheduling
 *                           operation in a tick/tock.
 * @idle_groups_to_schedule: List of runnable queue groups, in which all GPU
 *                           command queues became idle or are waiting for
 *                           synchronization object, prepared on every
 *                           scheduler tick. The groups in this list are
 *                           appended to the tail of @groups_to_schedule list
 *                           after the scan out so that the idle groups aren't
 *                           preferred for scheduling over the non-idle ones.
 * @csg_scan_count_for_tick: CSG scanout count for assign the scan_seq_num for
 *                           each scanned out group during scheduling operation
 *                           in a tick/tock.
 * @total_runnable_grps:     Total number of runnable groups across all KCTXs.
 * @csgs_events_enable_mask: Use for temporary masking off asynchronous events
 *                           from firmware (such as OoM events) before a group
 *                           is suspended.
 * @csg_slots_idle_mask:     Bit array for storing the mask of CS
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
 * @tick_timer:             High-resolution timer employed to schedule tick
 *                          workqueue items (kernel-provided delayed_work
 *                          items do not use hrtimer and for some reason do
 *                          not provide sufficiently reliable periodicity).
 * @tick_work:              Work item that performs the "schedule on tick"
 *                          operation to implement timeslice-based scheduling.
 * @tock_work:              Work item that would perform the schedule on tock
 *                          operation to implement the asynchronous scheduling.
 * @ping_work:              Work item that would ping the firmware at regular
 *                          intervals, only if there is a single active CSG
 *                          slot, to check if firmware is alive and would
 *                          initiate a reset if the ping request isn't
 *                          acknowledged.
 * @top_ctx:                Pointer to the Kbase context corresponding to the
 *                          @top_grp.
 * @top_grp:                Pointer to queue group inside @groups_to_schedule
 *                          list that was assigned the highest slot priority.
 * @tock_pending_request:   A "tock" request is pending: a group that is not
 *                          currently on the GPU demands to be scheduled.
 * @active_protm_grp:       Indicates if firmware has been permitted to let GPU
 *                          enter protected mode with the given group. On exit
 *                          from protected mode the pointer is reset to NULL.
 *                          This pointer is set and PROTM_ENTER request is sent
 *                          atomically with @interrupt_lock held.
 *                          This pointer being set doesn't necessarily indicates
 *                          that GPU is in protected mode, kbdev->protected_mode
 *                          needs to be checked for that.
 * @idle_wq:                Workqueue for executing GPU idle notification
 *                          handler.
 * @gpu_idle_work:          Work item for facilitating the scheduler to bring
 *                          the GPU to a low-power mode on becoming idle.
 * @gpu_no_longer_idle:     Effective only when the GPU idle worker has been
 *                          queued for execution, this indicates whether the
 *                          GPU has become non-idle since the last time the
 *                          idle notification was received.
 * @non_idle_offslot_grps:  Count of off-slot non-idle groups. Reset during
 *                          the scheduler active phase in a tick. It then
 *                          tracks the count of non-idle groups across all the
 *                          other phases.
 * @non_idle_scanout_grps:  Count on the non-idle groups in the scan-out
 *                          list at the scheduling prepare stage.
 * @pm_active_count:        Count indicating if the scheduler is owning a power
 *                          management reference count. Reference is taken when
 *                          the count becomes 1 and is dropped when the count
 *                          becomes 0. It is used to enable the power up of MCU
 *                          after GPU and L2 cache have been powered up. So when
 *                          this count is zero, MCU will not be powered up.
 * @csg_scheduling_period_ms: Duration of Scheduling tick in milliseconds.
 * @tick_timer_active:      Indicates whether the @tick_timer is effectively
 *                          active or not, as the callback function of
 *                          @tick_timer will enqueue @tick_work only if this
 *                          flag is true. This is mainly useful for the case
 *                          when scheduling tick needs to be advanced from
 *                          interrupt context, without actually deactivating
 *                          the @tick_timer first and then enqueing @tick_work.
 * @tick_protm_pending_seq: Scan out sequence number of the group that has
 *                          protected mode execution pending for the queue(s)
 *                          bound to it and will be considered first for the
 *                          protected mode execution compared to other such
 *                          groups. It is updated on every tick/tock.
 *                          @interrupt_lock is used to serialize the access.
 * @protm_enter_time:       GPU protected mode enter time.
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
	u32 remaining_tick_slots;
	struct list_head idle_groups_to_schedule;
	u32 csg_scan_count_for_tick;
	u32 total_runnable_grps;
	DECLARE_BITMAP(csgs_events_enable_mask, MAX_SUPPORTED_CSGS);
	DECLARE_BITMAP(csg_slots_idle_mask, MAX_SUPPORTED_CSGS);
	DECLARE_BITMAP(csg_slots_prio_update, MAX_SUPPORTED_CSGS);
	unsigned long last_schedule;
	bool timer_enabled;
	struct workqueue_struct *wq;
	struct hrtimer tick_timer;
	struct work_struct tick_work;
	struct delayed_work tock_work;
	struct delayed_work ping_work;
	struct kbase_context *top_ctx;
	struct kbase_queue_group *top_grp;
	bool tock_pending_request;
	struct kbase_queue_group *active_protm_grp;
	struct workqueue_struct *idle_wq;
	struct work_struct gpu_idle_work;
	atomic_t gpu_no_longer_idle;
	atomic_t non_idle_offslot_grps;
	u32 non_idle_scanout_grps;
	u32 pm_active_count;
	unsigned int csg_scheduling_period_ms;
	bool tick_timer_active;
	u32 tick_protm_pending_seq;
	ktime_t protm_enter_time;
};

/*
 * Number of GPU cycles per unit of the global progress timeout.
 */
#define GLB_PROGRESS_TIMER_TIMEOUT_SCALE ((u64)1024)

/*
 * Maximum value of the global progress timeout.
 */
#define GLB_PROGRESS_TIMER_TIMEOUT_MAX \
	((GLB_PROGRESS_TIMER_TIMEOUT_MASK >> \
		GLB_PROGRESS_TIMER_TIMEOUT_SHIFT) * \
	GLB_PROGRESS_TIMER_TIMEOUT_SCALE)

/*
 * Default GLB_PWROFF_TIMER_TIMEOUT value in unit of micro-seconds.
 */
#define DEFAULT_GLB_PWROFF_TIMEOUT_US (800)

/*
 * In typical operations, the management of the shader core power transitions
 * is delegated to the MCU/firmware. However, if the host driver is configured
 * to take direct control, one needs to disable the MCU firmware GLB_PWROFF
 * timer.
 */
#define DISABLE_GLB_PWROFF_TIMER (0)

/* Index of the GPU_ACTIVE counter within the CSHW counter block */
#define GPU_ACTIVE_CNT_IDX (4)

/*
 * Maximum number of sessions that can be managed by the IPA Control component.
 */
#if MALI_UNIT_TEST
#define KBASE_IPA_CONTROL_MAX_SESSIONS ((size_t)8)
#else
#define KBASE_IPA_CONTROL_MAX_SESSIONS ((size_t)2)
#endif

/**
 * enum kbase_ipa_core_type - Type of counter block for performance counters
 *
 * @KBASE_IPA_CORE_TYPE_CSHW:   CS Hardware counters.
 * @KBASE_IPA_CORE_TYPE_MEMSYS: Memory System counters.
 * @KBASE_IPA_CORE_TYPE_TILER:  Tiler counters.
 * @KBASE_IPA_CORE_TYPE_SHADER: Shader Core counters.
 * @KBASE_IPA_CORE_TYPE_NUM:    Number of core types.
 */
enum kbase_ipa_core_type {
	KBASE_IPA_CORE_TYPE_CSHW = 0,
	KBASE_IPA_CORE_TYPE_MEMSYS,
	KBASE_IPA_CORE_TYPE_TILER,
	KBASE_IPA_CORE_TYPE_SHADER,
	KBASE_IPA_CORE_TYPE_NUM
};

/*
 * Number of configurable counters per type of block on the IPA Control
 * interface.
 */
#define KBASE_IPA_CONTROL_NUM_BLOCK_COUNTERS ((size_t)8)

/*
 * Total number of configurable counters existing on the IPA Control interface.
 */
#define KBASE_IPA_CONTROL_MAX_COUNTERS                                         \
	((size_t)KBASE_IPA_CORE_TYPE_NUM * KBASE_IPA_CONTROL_NUM_BLOCK_COUNTERS)

/**
 * struct kbase_ipa_control_prfcnt - Session for a single performance counter
 *
 * @latest_raw_value: Latest raw value read from the counter.
 * @scaling_factor:   Factor raw value shall be multiplied by.
 * @accumulated_diff: Partial sum of scaled and normalized values from
 *                    previous samples. This represent all the values
 *                    that were read before the latest raw value.
 * @type:             Type of counter block for performance counter.
 * @select_idx:       Index of the performance counter as configured on
 *                    the IPA Control interface.
 * @gpu_norm:         Indicating whether values shall be normalized by
 *                    GPU frequency. If true, returned values represent
 *                    an interval of time expressed in seconds (when the
 *                    scaling factor is set to 1).
 */
struct kbase_ipa_control_prfcnt {
	u64 latest_raw_value;
	u64 scaling_factor;
	u64 accumulated_diff;
	enum kbase_ipa_core_type type;
	u8 select_idx;
	bool gpu_norm;
};

/**
 * struct kbase_ipa_control_session - Session for an IPA Control client
 *
 * @prfcnts:        Sessions for individual performance counters.
 * @num_prfcnts:    Number of performance counters.
 * @active:         Indicates whether this slot is in use or not
 * @last_query_time:     Time of last query, in ns
 * @protm_time:     Amount of time (in ns) that GPU has been in protected
 */
struct kbase_ipa_control_session {
	struct kbase_ipa_control_prfcnt prfcnts[KBASE_IPA_CONTROL_MAX_COUNTERS];
	size_t num_prfcnts;
	bool active;
	u64 last_query_time;
	u64 protm_time;
};

/**
 * struct kbase_ipa_control_prfcnt_config - Performance counter configuration
 *
 * @idx:      Index of the performance counter inside the block, as specified
 *            in the GPU architecture.
 * @refcount: Number of client sessions bound to this counter.
 *
 * This structure represents one configurable performance counter of
 * the IPA Control interface. The entry may be mapped to a specific counter
 * by one or more client sessions. The counter is considered to be unused
 * if it isn't part of any client session.
 */
struct kbase_ipa_control_prfcnt_config {
	u8 idx;
	u8 refcount;
};

/**
 * struct kbase_ipa_control_prfcnt_block - Block of performance counters
 *
 * @select:                 Current performance counter configuration.
 * @num_available_counters: Number of counters that are not already configured.
 *
 */
struct kbase_ipa_control_prfcnt_block {
	struct kbase_ipa_control_prfcnt_config select[KBASE_IPA_CONTROL_NUM_BLOCK_COUNTERS];
	size_t num_available_counters;
};

/**
 * struct kbase_ipa_control - Manager of the IPA Control interface.
 *
 * @blocks:              Current configuration of performance counters
 *                       for the IPA Control interface.
 * @sessions:            State of client sessions, storing information
 *                       like performance counters the client subscribed to
 *                       and latest value read from each counter.
 * @lock:                Spinlock to serialize access by concurrent clients.
 * @rtm_listener_data:   Private data for allocating a GPU frequency change
 *                       listener.
 * @num_active_sessions: Number of sessions opened by clients.
 * @cur_gpu_rate:        Current GPU top-level operating frequency, in Hz.
 * @rtm_listener_data:   Private data for allocating a GPU frequency change
 *                       listener.
 * @protm_start:         Time (in ns) at which the GPU entered protected mode
 */
struct kbase_ipa_control {
	struct kbase_ipa_control_prfcnt_block blocks[KBASE_IPA_CORE_TYPE_NUM];
	struct kbase_ipa_control_session sessions[KBASE_IPA_CONTROL_MAX_SESSIONS];
	spinlock_t lock;
	void *rtm_listener_data;
	size_t num_active_sessions;
	u32 cur_gpu_rate;
	u64 protm_start;
};

/**
 * struct kbase_csf_firmware_interface - Interface in the MCU firmware
 *
 * @node:  Interface objects are on the kbase_device:csf.firmware_interfaces
 *         list using this list_head to link them
 * @phys:  Array of the physical (tagged) addresses making up this interface
 * @reuse_pages: Flag used to identify if the FW interface entry reuses
 *               physical pages allocated for another FW interface entry.
 * @is_small_page: Flag used to identify if small pages are used for
 *                 the FW interface entry.
 * @name:  NULL-terminated string naming the interface
 * @num_pages: Number of entries in @phys and @pma (and length of the interface)
 * @num_pages_aligned: Same as @num_pages except for the case when @is_small_page
 *                     is false and @reuse_pages is false and therefore will be
 *                     aligned to NUM_4K_PAGES_IN_2MB_PAGE.
 * @virtual: Starting GPU virtual address this interface is mapped at
 * @flags: bitmask of CSF_FIRMWARE_ENTRY_* conveying the interface attributes
 * @data_start: Offset into firmware image at which the interface data starts
 * @data_end: Offset into firmware image at which the interface data ends
 * @kernel_map: A kernel mapping of the memory or NULL if not required to be
 *              mapped in the kernel
 * @pma: Array of pointers to protected memory allocations.
 */
struct kbase_csf_firmware_interface {
	struct list_head node;
	struct tagged_addr *phys;
	bool reuse_pages;
	bool is_small_page;
	char *name;
	u32 num_pages;
	u32 num_pages_aligned;
	u32 virtual;
	u32 flags;
	u32 data_start;
	u32 data_end;
	void *kernel_map;
	struct protected_memory_allocation **pma;
};

/*
 * struct kbase_csf_hwcnt - Object containing members for handling the dump of
 *                          HW counters.
 *
 * @request_pending:        Flag set when HWC requested and used for HWC sample
 *                          done interrupt.
 * @enable_pending:         Flag set when HWC enable status change and used for
 *                          enable done interrupt.
 */
struct kbase_csf_hwcnt {
	bool request_pending;
	bool enable_pending;
};

/*
 * struct kbase_csf_mcu_fw - Object containing device loaded MCU firmware data.
 *
 * @size:                    Loaded firmware data size. Meaningful only when the
 *                           other field @p data is not NULL.
 * @data:                    Pointer to the device retained firmware data. If NULL
 *                           means not loaded yet or error in loading stage.
 */
struct kbase_csf_mcu_fw {
	size_t size;
	u8 *data;
};

/**
 * struct kbase_csf_device - Object representing CSF for an instance of GPU
 *                           platform device.
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
 * @dummy_user_reg_page:    Address of the dummy page that is mapped in place
 *                          of the real User register page just before the GPU
 *                          is powered down. The User register page is mapped
 *                          in the address space of every process, that created
 *                          a Base context, to enable the access to LATEST_FLUSH
 *                          register from userspace.
 * @mali_file_inode:        Pointer to the inode corresponding to mali device
 *                          file. This is needed in order to switch to the
 *                          @dummy_user_reg_page on GPU power down.
 *                          All instances of the mali device file will point to
 *                          the same inode.
 * @reg_lock:               Lock to serialize the MCU firmware related actions
 *                          that affect all contexts such as allocation of
 *                          regions from shared interface area, assignment of
 *                          hardware doorbell pages, assignment of CSGs,
 *                          sending global requests.
 * @event_wait:             Wait queue to wait for receiving csf events, i.e.
 *                          the interrupt from CSF firmware, or scheduler state
 *                          changes.
 * @interrupt_received:     Flag set when the interrupt is received from CSF fw
 * @global_iface:           The result of parsing the global interface
 *                          structure set up by the firmware, including the
 *                          CSGs, CSs, and their properties
 * @scheduler:              The CS scheduler instance.
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
 * @firmware_full_reload_needed: Flag for indicating that the firmware needs to
 *                               be fully re-loaded. This may be set when the
 *                               boot or re-init of MCU fails after a successful
 *                               soft reset.
 * @firmware_hctl_core_pwr: Flag for indicating that the host diver is in
 *                          charge of the shader core's power transitions, and
 *                          the mcu_core_pwroff timeout feature is disabled
 *                          (i.e. configured 0 in the register field). If
 *                          false, the control is delegated to the MCU.
 * @firmware_reload_work:   Work item for facilitating the procedural actions
 *                          on reloading the firmware.
 * @glb_init_request_pending: Flag to indicate that Global requests have been
 *                            sent to the FW after MCU was re-enabled and their
 *                            acknowledgement is pending.
 * @fw_error_work:          Work item for handling the firmware internal error
 *                          fatal event.
 * @ipa_control:            IPA Control component manager.
 * @mcu_core_pwroff_dur_us: Sysfs attribute for the glb_pwroff timeout input
 *                          in unit of micro-seconds. The firmware does not use
 *                          it directly.
 * @mcu_core_pwroff_dur_count: The counterpart of the glb_pwroff timeout input
 *                             in interface required format, ready to be used
 *                             directly in the firmware.
 * @mcu_core_pwroff_reg_shadow: The actual value that has been programed into
 *                              the glb_pwoff register. This is separated from
 *                              the @p mcu_core_pwroff_dur_count as an update
 *                              to the latter is asynchronous.
 * @gpu_idle_hysteresis_ms: Sysfs attribute for the idle hysteresis time
 *                          window in unit of ms. The firmware does not use it
 *                          directly.
 * @gpu_idle_dur_count:     The counterpart of the hysteresis time window in
 *                          interface required format, ready to be used
 *                          directly in the firmware.
 * @fw_timeout_ms:          Timeout value (in milliseconds) used when waiting
 *                          for any request sent to the firmware.
 * @hwcnt:                  Contain members required for handling the dump of
 *                          HW counters.
 * @fw:                     Copy of the loaded MCU firmware image.
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
	struct tagged_addr dummy_user_reg_page;
	struct inode *mali_file_inode;
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
	bool firmware_full_reload_needed;
	bool firmware_hctl_core_pwr;
	struct work_struct firmware_reload_work;
	bool glb_init_request_pending;
	struct work_struct fw_error_work;
	struct kbase_ipa_control ipa_control;
	u32 mcu_core_pwroff_dur_us;
	u32 mcu_core_pwroff_dur_count;
	u32 mcu_core_pwroff_reg_shadow;
	u32 gpu_idle_hysteresis_ms;
	u32 gpu_idle_dur_count;
	unsigned int fw_timeout_ms;
	struct kbase_csf_hwcnt hwcnt;
	struct kbase_csf_mcu_fw fw;
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
