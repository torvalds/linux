// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
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

#include "mali_kbase_csf_csg_debugfs.h"
#include <mali_kbase.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <csf/mali_kbase_csf_trace_buffer.h>
#include <backend/gpu/mali_kbase_pm_internal.h>

#if IS_ENABLED(CONFIG_DEBUG_FS)
#include "mali_kbase_csf_tl_reader.h"

#define MAX_SCHED_STATE_STRING_LEN (16)
static const char *scheduler_state_to_string(struct kbase_device *kbdev,
			enum kbase_csf_scheduler_state sched_state)
{
	switch (sched_state) {
	case SCHED_BUSY:
		return "BUSY";
	case SCHED_INACTIVE:
		return "INACTIVE";
	case SCHED_SUSPENDED:
		return "SUSPENDED";
#ifdef KBASE_PM_RUNTIME
	case SCHED_SLEEPING:
		return "SLEEPING";
#endif
	default:
		dev_warn(kbdev->dev, "Unknown Scheduler state %d", sched_state);
		return NULL;
	}
}

/**
 * blocked_reason_to_string() - Convert blocking reason id to a string
 *
 * @reason_id: blocked_reason
 *
 * Return: Suitable string
 */
static const char *blocked_reason_to_string(u32 reason_id)
{
	/* possible blocking reasons of a cs */
	static const char *const cs_blocked_reason[] = {
		[CS_STATUS_BLOCKED_REASON_REASON_UNBLOCKED] = "UNBLOCKED",
		[CS_STATUS_BLOCKED_REASON_REASON_WAIT] = "WAIT",
		[CS_STATUS_BLOCKED_REASON_REASON_PROGRESS_WAIT] =
			"PROGRESS_WAIT",
		[CS_STATUS_BLOCKED_REASON_REASON_SYNC_WAIT] = "SYNC_WAIT",
		[CS_STATUS_BLOCKED_REASON_REASON_DEFERRED] = "DEFERRED",
		[CS_STATUS_BLOCKED_REASON_REASON_RESOURCE] = "RESOURCE",
		[CS_STATUS_BLOCKED_REASON_REASON_FLUSH] = "FLUSH"
	};

	if (WARN_ON(reason_id >= ARRAY_SIZE(cs_blocked_reason)))
		return "UNKNOWN_BLOCKED_REASON_ID";

	return cs_blocked_reason[reason_id];
}

static void kbasep_csf_scheduler_dump_active_queue_cs_status_wait(
	struct seq_file *file, u32 wait_status, u32 wait_sync_value,
	u64 wait_sync_live_value, u64 wait_sync_pointer, u32 sb_status,
	u32 blocked_reason)
{
#define WAITING "Waiting"
#define NOT_WAITING "Not waiting"

	seq_printf(file, "SB_MASK: %d\n",
			CS_STATUS_WAIT_SB_MASK_GET(wait_status));
	seq_printf(file, "PROGRESS_WAIT: %s\n",
			CS_STATUS_WAIT_PROGRESS_WAIT_GET(wait_status) ?
			WAITING : NOT_WAITING);
	seq_printf(file, "PROTM_PEND: %s\n",
			CS_STATUS_WAIT_PROTM_PEND_GET(wait_status) ?
			WAITING : NOT_WAITING);
	seq_printf(file, "SYNC_WAIT: %s\n",
			CS_STATUS_WAIT_SYNC_WAIT_GET(wait_status) ?
			WAITING : NOT_WAITING);
	seq_printf(file, "WAIT_CONDITION: %s\n",
			CS_STATUS_WAIT_SYNC_WAIT_CONDITION_GET(wait_status) ?
			"greater than" : "less or equal");
	seq_printf(file, "SYNC_POINTER: 0x%llx\n", wait_sync_pointer);
	seq_printf(file, "SYNC_VALUE: %d\n", wait_sync_value);
	seq_printf(file, "SYNC_LIVE_VALUE: 0x%016llx\n", wait_sync_live_value);
	seq_printf(file, "SB_STATUS: %u\n",
		   CS_STATUS_SCOREBOARDS_NONZERO_GET(sb_status));
	seq_printf(file, "BLOCKED_REASON: %s\n",
		   blocked_reason_to_string(CS_STATUS_BLOCKED_REASON_REASON_GET(
			   blocked_reason)));
}

static void kbasep_csf_scheduler_dump_active_cs_trace(struct seq_file *file,
			struct kbase_csf_cmd_stream_info const *const stream)
{
	u32 val = kbase_csf_firmware_cs_input_read(stream,
			CS_INSTR_BUFFER_BASE_LO);
	u64 addr = ((u64)kbase_csf_firmware_cs_input_read(stream,
				CS_INSTR_BUFFER_BASE_HI) << 32) | val;
	val = kbase_csf_firmware_cs_input_read(stream,
				CS_INSTR_BUFFER_SIZE);

	seq_printf(file, "CS_TRACE_BUF_ADDR: 0x%16llx, SIZE: %u\n", addr, val);

	/* Write offset variable address (pointer) */
	val = kbase_csf_firmware_cs_input_read(stream,
			CS_INSTR_BUFFER_OFFSET_POINTER_LO);
	addr = ((u64)kbase_csf_firmware_cs_input_read(stream,
			CS_INSTR_BUFFER_OFFSET_POINTER_HI) << 32) | val;
	seq_printf(file, "CS_TRACE_BUF_OFFSET_PTR: 0x%16llx\n", addr);

	/* EVENT_SIZE and EVENT_STATEs */
	val = kbase_csf_firmware_cs_input_read(stream, CS_INSTR_CONFIG);
	seq_printf(file, "TRACE_EVENT_SIZE: 0x%x, TRACE_EVENT_STAES 0x%x\n",
			CS_INSTR_CONFIG_EVENT_SIZE_GET(val),
			CS_INSTR_CONFIG_EVENT_STATE_GET(val));
}

/**
 * kbasep_csf_scheduler_dump_active_queue() - Print GPU command queue
 *                                            debug information
 *
 * @file:  seq_file for printing to
 * @queue: Address of a GPU command queue to examine
 */
static void kbasep_csf_scheduler_dump_active_queue(struct seq_file *file,
		struct kbase_queue *queue)
{
	u32 *addr;
	u64 cs_extract;
	u64 cs_insert;
	u32 cs_active;
	u64 wait_sync_pointer;
	u32 wait_status, wait_sync_value;
	u32 sb_status;
	u32 blocked_reason;
	struct kbase_vmap_struct *mapping;
	u64 *evt;
	u64 wait_sync_live_value;

	if (!queue)
		return;

	if (WARN_ON(queue->csi_index == KBASEP_IF_NR_INVALID ||
		    !queue->group))
		return;

	addr = (u32 *)queue->user_io_addr;
	cs_insert = addr[CS_INSERT_LO/4] | ((u64)addr[CS_INSERT_HI/4] << 32);

	addr = (u32 *)(queue->user_io_addr + PAGE_SIZE);
	cs_extract = addr[CS_EXTRACT_LO/4] | ((u64)addr[CS_EXTRACT_HI/4] << 32);
	cs_active = addr[CS_ACTIVE/4];

#define KBASEP_CSF_DEBUGFS_CS_HEADER_USER_IO \
	"Bind Idx,     Ringbuf addr,     Size, Prio,    Insert offset,   Extract offset, Active, Doorbell\n"

	seq_printf(file, KBASEP_CSF_DEBUGFS_CS_HEADER_USER_IO "%8d, %16llx, %8x, %4u, %16llx, %16llx, %6u, %8d\n",
			queue->csi_index, queue->base_addr,
			queue->size,
			queue->priority, cs_insert, cs_extract, cs_active, queue->doorbell_nr);

	/* Print status information for blocked group waiting for sync object. For on-slot queues,
	 * if cs_trace is enabled, dump the interface's cs_trace configuration.
	 */
	if (kbase_csf_scheduler_group_get_slot(queue->group) < 0) {
		seq_printf(file, "SAVED_CMD_PTR: 0x%llx\n", queue->saved_cmd_ptr);
		if (CS_STATUS_WAIT_SYNC_WAIT_GET(queue->status_wait)) {
			wait_status = queue->status_wait;
			wait_sync_value = queue->sync_value;
			wait_sync_pointer = queue->sync_ptr;
			sb_status = queue->sb_status;
			blocked_reason = queue->blocked_reason;

			evt = (u64 *)kbase_phy_alloc_mapping_get(queue->kctx, wait_sync_pointer, &mapping);
			if (evt) {
				wait_sync_live_value = evt[0];
				kbase_phy_alloc_mapping_put(queue->kctx, mapping);
			} else {
				wait_sync_live_value = U64_MAX;
			}

			kbasep_csf_scheduler_dump_active_queue_cs_status_wait(
				file, wait_status, wait_sync_value,
				wait_sync_live_value, wait_sync_pointer,
				sb_status, blocked_reason);
		}
	} else {
		struct kbase_device const *const kbdev =
			queue->group->kctx->kbdev;
		struct kbase_csf_cmd_stream_group_info const *const ginfo =
			&kbdev->csf.global_iface.groups[queue->group->csg_nr];
		struct kbase_csf_cmd_stream_info const *const stream =
			&ginfo->streams[queue->csi_index];
		u64 cmd_ptr;
		u32 req_res;

		if (WARN_ON(!stream))
			return;

		cmd_ptr = kbase_csf_firmware_cs_output(stream,
				CS_STATUS_CMD_PTR_LO);
		cmd_ptr |= (u64)kbase_csf_firmware_cs_output(stream,
				CS_STATUS_CMD_PTR_HI) << 32;
		req_res = kbase_csf_firmware_cs_output(stream,
					CS_STATUS_REQ_RESOURCE);

		seq_printf(file, "CMD_PTR: 0x%llx\n", cmd_ptr);
		seq_printf(file, "REQ_RESOURCE [COMPUTE]: %d\n",
			CS_STATUS_REQ_RESOURCE_COMPUTE_RESOURCES_GET(req_res));
		seq_printf(file, "REQ_RESOURCE [FRAGMENT]: %d\n",
			CS_STATUS_REQ_RESOURCE_FRAGMENT_RESOURCES_GET(req_res));
		seq_printf(file, "REQ_RESOURCE [TILER]: %d\n",
			CS_STATUS_REQ_RESOURCE_TILER_RESOURCES_GET(req_res));
		seq_printf(file, "REQ_RESOURCE [IDVS]: %d\n",
			CS_STATUS_REQ_RESOURCE_IDVS_RESOURCES_GET(req_res));

		wait_status = kbase_csf_firmware_cs_output(stream,
				CS_STATUS_WAIT);
		wait_sync_value = kbase_csf_firmware_cs_output(stream,
					CS_STATUS_WAIT_SYNC_VALUE);
		wait_sync_pointer = kbase_csf_firmware_cs_output(stream,
					CS_STATUS_WAIT_SYNC_POINTER_LO);
		wait_sync_pointer |= (u64)kbase_csf_firmware_cs_output(stream,
					CS_STATUS_WAIT_SYNC_POINTER_HI) << 32;

		sb_status = kbase_csf_firmware_cs_output(stream,
							 CS_STATUS_SCOREBOARDS);
		blocked_reason = kbase_csf_firmware_cs_output(
			stream, CS_STATUS_BLOCKED_REASON);

		evt = (u64 *)kbase_phy_alloc_mapping_get(queue->kctx, wait_sync_pointer, &mapping);
		if (evt) {
			wait_sync_live_value = evt[0];
			kbase_phy_alloc_mapping_put(queue->kctx, mapping);
		} else {
			wait_sync_live_value = U64_MAX;
		}

		kbasep_csf_scheduler_dump_active_queue_cs_status_wait(
			file, wait_status, wait_sync_value,
			wait_sync_live_value, wait_sync_pointer, sb_status,
			blocked_reason);
		/* Dealing with cs_trace */
		if (kbase_csf_scheduler_queue_has_trace(queue))
			kbasep_csf_scheduler_dump_active_cs_trace(file, stream);
		else
			seq_puts(file, "NO CS_TRACE\n");
	}

	seq_puts(file, "\n");
}

static void update_active_group_status(struct seq_file *file,
		struct kbase_queue_group *const group)
{
	struct kbase_device *const kbdev = group->kctx->kbdev;
	struct kbase_csf_cmd_stream_group_info const *const ginfo =
		&kbdev->csf.global_iface.groups[group->csg_nr];
	long remaining = kbase_csf_timeout_in_jiffies(kbdev->csf.fw_timeout_ms);
	unsigned long flags;

	/* Global doorbell ring for CSG STATUS_UPDATE request or User doorbell
	 * ring for Extract offset update, shall not be made when MCU has been
	 * put to sleep otherwise it will undesirably make MCU exit the sleep
	 * state. Also it isn't really needed as FW will implicitly update the
	 * status of all on-slot groups when MCU sleep request is sent to it.
	 */
	if (kbdev->csf.scheduler.state == SCHED_SLEEPING)
		return;

	/* Ring the User doobell shared between the queues bound to this
	 * group, to have FW update the CS_EXTRACT for all the queues
	 * bound to the group. Ring early so that FW gets adequate time
	 * for the handling.
	 */
	kbase_csf_ring_doorbell(kbdev, group->doorbell_nr);

	kbase_csf_scheduler_spin_lock(kbdev, &flags);
	kbase_csf_firmware_csg_input_mask(ginfo, CSG_REQ,
			~kbase_csf_firmware_csg_output(ginfo, CSG_ACK),
			CSG_REQ_STATUS_UPDATE_MASK);
	kbase_csf_scheduler_spin_unlock(kbdev, flags);
	kbase_csf_ring_csg_doorbell(kbdev, group->csg_nr);

	remaining = wait_event_timeout(kbdev->csf.event_wait,
		!((kbase_csf_firmware_csg_input_read(ginfo, CSG_REQ) ^
		kbase_csf_firmware_csg_output(ginfo, CSG_ACK)) &
		CSG_REQ_STATUS_UPDATE_MASK), remaining);

	if (!remaining) {
		dev_err(kbdev->dev,
			"Timed out for STATUS_UPDATE on group %d on slot %d",
			group->handle, group->csg_nr);

		seq_printf(file, "*** Warn: Timed out for STATUS_UPDATE on slot %d\n",
			group->csg_nr);
		seq_puts(file, "*** The following group-record is likely stale\n");
	}
}

static void kbasep_csf_scheduler_dump_active_group(struct seq_file *file,
		struct kbase_queue_group *const group)
{
	if (kbase_csf_scheduler_group_get_slot(group) >= 0) {
		struct kbase_device *const kbdev = group->kctx->kbdev;
		u32 ep_c, ep_r;
		char exclusive;
		char idle = 'N';
		struct kbase_csf_cmd_stream_group_info const *const ginfo =
			&kbdev->csf.global_iface.groups[group->csg_nr];
		u8 slot_priority =
			kbdev->csf.scheduler.csg_slots[group->csg_nr].priority;

		update_active_group_status(file, group);

		ep_c = kbase_csf_firmware_csg_output(ginfo,
				CSG_STATUS_EP_CURRENT);
		ep_r = kbase_csf_firmware_csg_output(ginfo, CSG_STATUS_EP_REQ);

		if (CSG_STATUS_EP_REQ_EXCLUSIVE_COMPUTE_GET(ep_r))
			exclusive = 'C';
		else if (CSG_STATUS_EP_REQ_EXCLUSIVE_FRAGMENT_GET(ep_r))
			exclusive = 'F';
		else
			exclusive = '0';

		if (kbase_csf_firmware_csg_output(ginfo, CSG_STATUS_STATE) &
				CSG_STATUS_STATE_IDLE_MASK)
			idle = 'Y';

		seq_puts(file, "GroupID, CSG NR, CSG Prio, Run State, Priority, C_EP(Alloc/Req), F_EP(Alloc/Req), T_EP(Alloc/Req), Exclusive, Idle\n");
		seq_printf(file, "%7d, %6d, %8d, %9d, %8d, %11d/%3d, %11d/%3d, %11d/%3d, %9c, %4c\n",
			group->handle,
			group->csg_nr,
			slot_priority,
			group->run_state,
			group->priority,
			CSG_STATUS_EP_CURRENT_COMPUTE_EP_GET(ep_c),
			CSG_STATUS_EP_REQ_COMPUTE_EP_GET(ep_r),
			CSG_STATUS_EP_CURRENT_FRAGMENT_EP_GET(ep_c),
			CSG_STATUS_EP_REQ_FRAGMENT_EP_GET(ep_r),
			CSG_STATUS_EP_CURRENT_TILER_EP_GET(ep_c),
			CSG_STATUS_EP_REQ_TILER_EP_GET(ep_r),
			exclusive,
			idle);

		/* Wait for the User doobell ring to take effect */
		if (kbdev->csf.scheduler.state != SCHED_SLEEPING)
			msleep(100);
	} else {
		seq_puts(file, "GroupID, CSG NR, Run State, Priority\n");
		seq_printf(file, "%7d, %6d, %9d, %8d\n",
			group->handle,
			group->csg_nr,
			group->run_state,
			group->priority);
	}

	if (group->run_state != KBASE_CSF_GROUP_TERMINATED) {
		unsigned int i;

		seq_puts(file, "Bound queues:\n");

		for (i = 0; i < MAX_SUPPORTED_STREAMS_PER_GROUP; i++) {
			kbasep_csf_scheduler_dump_active_queue(file,
					group->bound_queues[i]);
		}
	}

	seq_puts(file, "\n");
}

/**
 * kbasep_csf_queue_group_debugfs_show() - Print per-context GPU command queue
 *					   group debug information
 *
 * @file: The seq_file for printing to
 * @data: The debugfs dentry private data, a pointer to kbase context
 *
 * Return: Negative error code or 0 on success.
 */
static int kbasep_csf_queue_group_debugfs_show(struct seq_file *file,
		void *data)
{
	u32 gr;
	struct kbase_context *const kctx = file->private;
	struct kbase_device *const kbdev = kctx->kbdev;

	if (WARN_ON(!kctx))
		return -EINVAL;

	seq_printf(file, "MALI_CSF_CSG_DEBUGFS_VERSION: v%u\n",
			MALI_CSF_CSG_DEBUGFS_VERSION);

	mutex_lock(&kctx->csf.lock);
	kbase_csf_scheduler_lock(kbdev);
	if (kbdev->csf.scheduler.state == SCHED_SLEEPING) {
		/* Wait for the MCU sleep request to complete. Please refer the
		 * update_active_group_status() function for the explanation.
		 */
		kbase_pm_wait_for_desired_state(kbdev);
	}
	for (gr = 0; gr < MAX_QUEUE_GROUP_NUM; gr++) {
		struct kbase_queue_group *const group =
			kctx->csf.queue_groups[gr];

		if (group)
			kbasep_csf_scheduler_dump_active_group(file, group);
	}
	kbase_csf_scheduler_unlock(kbdev);
	mutex_unlock(&kctx->csf.lock);

	return 0;
}

/**
 * kbasep_csf_scheduler_dump_active_groups() - Print debug info for active
 *                                             GPU command queue groups
 *
 * @file: The seq_file for printing to
 * @data: The debugfs dentry private data, a pointer to kbase_device
 *
 * Return: Negative error code or 0 on success.
 */
static int kbasep_csf_scheduler_dump_active_groups(struct seq_file *file,
		void *data)
{
	u32 csg_nr;
	struct kbase_device *kbdev = file->private;
	u32 num_groups = kbdev->csf.global_iface.group_num;

	seq_printf(file, "MALI_CSF_CSG_DEBUGFS_VERSION: v%u\n",
			MALI_CSF_CSG_DEBUGFS_VERSION);

	kbase_csf_scheduler_lock(kbdev);
	if (kbdev->csf.scheduler.state == SCHED_SLEEPING) {
		/* Wait for the MCU sleep request to complete. Please refer the
		 * update_active_group_status() function for the explanation.
		 */
		kbase_pm_wait_for_desired_state(kbdev);
	}
	for (csg_nr = 0; csg_nr < num_groups; csg_nr++) {
		struct kbase_queue_group *const group =
			kbdev->csf.scheduler.csg_slots[csg_nr].resident_group;

		if (!group)
			continue;

		seq_printf(file, "\nCtx %d_%d\n", group->kctx->tgid,
				group->kctx->id);

		kbasep_csf_scheduler_dump_active_group(file, group);
	}
	kbase_csf_scheduler_unlock(kbdev);

	return 0;
}

static int kbasep_csf_queue_group_debugfs_open(struct inode *in,
		struct file *file)
{
	return single_open(file, kbasep_csf_queue_group_debugfs_show,
			in->i_private);
}

static int kbasep_csf_active_queue_groups_debugfs_open(struct inode *in,
		struct file *file)
{
	return single_open(file, kbasep_csf_scheduler_dump_active_groups,
			in->i_private);
}

static const struct file_operations kbasep_csf_queue_group_debugfs_fops = {
	.open = kbasep_csf_queue_group_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void kbase_csf_queue_group_debugfs_init(struct kbase_context *kctx)
{
	struct dentry *file;
	const mode_t mode = 0444;

	if (WARN_ON(!kctx || IS_ERR_OR_NULL(kctx->kctx_dentry)))
		return;

	file = debugfs_create_file("groups", mode,
		kctx->kctx_dentry, kctx, &kbasep_csf_queue_group_debugfs_fops);

	if (IS_ERR_OR_NULL(file)) {
		dev_warn(kctx->kbdev->dev,
		    "Unable to create per context queue groups debugfs entry");
	}
}

static const struct file_operations
	kbasep_csf_active_queue_groups_debugfs_fops = {
	.open = kbasep_csf_active_queue_groups_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int kbasep_csf_debugfs_scheduling_timer_enabled_get(
		void *data, u64 *val)
{
	struct kbase_device *const kbdev = data;

	*val = kbase_csf_scheduler_timer_is_enabled(kbdev);

	return 0;
}

static int kbasep_csf_debugfs_scheduling_timer_enabled_set(
		void *data, u64 val)
{
	struct kbase_device *const kbdev = data;

	kbase_csf_scheduler_timer_set_enabled(kbdev, val != 0);

	return 0;
}

static int kbasep_csf_debugfs_scheduling_timer_kick_set(
		void *data, u64 val)
{
	struct kbase_device *const kbdev = data;

	kbase_csf_scheduler_kick(kbdev);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(kbasep_csf_debugfs_scheduling_timer_enabled_fops,
			 &kbasep_csf_debugfs_scheduling_timer_enabled_get,
			 &kbasep_csf_debugfs_scheduling_timer_enabled_set, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(kbasep_csf_debugfs_scheduling_timer_kick_fops, NULL,
			 &kbasep_csf_debugfs_scheduling_timer_kick_set, "%llu\n");

/**
 * kbase_csf_debugfs_scheduler_state_get() - Get the state of scheduler.
 *
 * @file:     Object of the file that is being read.
 * @user_buf: User buffer that contains the string.
 * @count:    Length of user buffer
 * @ppos:     Offset within file object
 *
 * This function will return the current Scheduler state to Userspace
 * Scheduler may exit that state by the time the state string is received
 * by the Userspace.
 *
 * Return: 0 if Scheduler was found in an unexpected state, or the
 *         size of the state string if it was copied successfully to the
 *         User buffer or a negative value in case of an error.
 */
static ssize_t kbase_csf_debugfs_scheduler_state_get(struct file *file,
		    char __user *user_buf, size_t count, loff_t *ppos)
{
	struct kbase_device *kbdev = file->private_data;
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	const char *state_string;

	kbase_csf_scheduler_lock(kbdev);
	state_string = scheduler_state_to_string(kbdev, scheduler->state);
	kbase_csf_scheduler_unlock(kbdev);

	if (!state_string)
		count = 0;

	return simple_read_from_buffer(user_buf, count, ppos,
				       state_string, strlen(state_string));
}

/**
 * kbase_csf_debugfs_scheduler_state_set() - Set the state of scheduler.
 *
 * @file:  Object of the file that is being written to.
 * @ubuf:  User buffer that contains the string.
 * @count: Length of user buffer
 * @ppos:  Offset within file object
 *
 * This function will update the Scheduler state as per the state string
 * passed by the Userspace. Scheduler may or may not remain in new state
 * for long.
 *
 * Return: Negative value if the string doesn't correspond to a valid Scheduler
 *         state or if copy from user buffer failed, otherwise the length of
 *         the User buffer.
 */
static ssize_t kbase_csf_debugfs_scheduler_state_set(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct kbase_device *kbdev = file->private_data;
	char buf[MAX_SCHED_STATE_STRING_LEN];
	ssize_t ret = count;

	CSTD_UNUSED(ppos);

	count = min_t(size_t, sizeof(buf) - 1, count);
	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;

	buf[count] = 0;

	if (sysfs_streq(buf, "SUSPENDED"))
		kbase_csf_scheduler_pm_suspend(kbdev);
#ifdef KBASE_PM_RUNTIME
	else if (sysfs_streq(buf, "SLEEPING"))
		kbase_csf_scheduler_force_sleep(kbdev);
#endif
	else if (sysfs_streq(buf, "INACTIVE"))
		kbase_csf_scheduler_force_wakeup(kbdev);
	else {
		dev_dbg(kbdev->dev, "Bad scheduler state %s", buf);
		ret = -EINVAL;
	}

	return ret;
}

static const struct file_operations kbasep_csf_debugfs_scheduler_state_fops = {
	.owner = THIS_MODULE,
	.read = kbase_csf_debugfs_scheduler_state_get,
	.write = kbase_csf_debugfs_scheduler_state_set,
	.open = simple_open,
	.llseek = default_llseek,
};

void kbase_csf_debugfs_init(struct kbase_device *kbdev)
{
	debugfs_create_file("active_groups", 0444,
		kbdev->mali_debugfs_directory, kbdev,
		&kbasep_csf_active_queue_groups_debugfs_fops);

	debugfs_create_file("scheduling_timer_enabled", 0644,
			kbdev->mali_debugfs_directory, kbdev,
			&kbasep_csf_debugfs_scheduling_timer_enabled_fops);
	debugfs_create_file("scheduling_timer_kick", 0200,
			kbdev->mali_debugfs_directory, kbdev,
			&kbasep_csf_debugfs_scheduling_timer_kick_fops);
	debugfs_create_file("scheduler_state", 0644,
			kbdev->mali_debugfs_directory, kbdev,
			&kbasep_csf_debugfs_scheduler_state_fops);

	kbase_csf_tl_reader_debugfs_init(kbdev);
	kbase_csf_firmware_trace_buffer_debugfs_init(kbdev);
}

#else
/*
 * Stub functions for when debugfs is disabled
 */
void kbase_csf_queue_group_debugfs_init(struct kbase_context *kctx)
{
}

void kbase_csf_debugfs_init(struct kbase_device *kbdev)
{
}

#endif /* CONFIG_DEBUG_FS */
