/*
 *
 * (C) COPYRIGHT 2019-2020 ARM Limited. All rights reserved.
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

#include "mali_kbase_csf_csg_debugfs.h"
#include <mali_kbase.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <csf/mali_kbase_csf_trace_buffer.h>

#ifdef CONFIG_DEBUG_FS
#include "mali_kbase_csf_tl_reader.h"

static void kbasep_csf_scheduler_dump_active_queue_cs_status_wait(
		struct seq_file *file,
		u32 wait_status,
		u32 wait_sync_value,
		u64 wait_sync_live_value,
		u64 wait_sync_pointer)
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
	struct kbase_vmap_struct *mapping;
	u64 *evt;
	u64 wait_sync_live_value;

	if (!queue)
		return;

	if (WARN_ON(queue->csi_index == KBASEP_IF_NR_INVALID ||
		    !queue->group))
		return;

	/* Ring the doorbell to have firmware update CS_EXTRACT */
	kbase_csf_ring_cs_user_doorbell(queue->kctx->kbdev, queue);
	msleep(100);

	addr = (u32 *)queue->user_io_addr;
	cs_insert = addr[CS_INSERT_LO/4] | ((u64)addr[CS_INSERT_HI/4] << 32);

	addr = (u32 *)(queue->user_io_addr + PAGE_SIZE);
	cs_extract = addr[CS_EXTRACT_LO/4] | ((u64)addr[CS_EXTRACT_HI/4] << 32);
	cs_active = addr[CS_ACTIVE/4];

#define KBASEP_CSF_DEBUGFS_CS_HEADER_USER_IO \
	"Bind Idx,     Ringbuf addr, Prio,    Insert offset,   Extract offset, Active, Doorbell\n"

	seq_printf(file, KBASEP_CSF_DEBUGFS_CS_HEADER_USER_IO "%8d, %16llx, %4u, %16llx, %16llx, %6u, %8d\n",
			queue->csi_index, queue->base_addr, queue->priority,
			cs_insert, cs_extract, cs_active, queue->doorbell_nr);

	/* Print status information for blocked group waiting for sync object */
	if (kbase_csf_scheduler_group_get_slot(queue->group) < 0) {
		if (CS_STATUS_WAIT_SYNC_WAIT_GET(queue->status_wait)) {
			wait_status = queue->status_wait;
			wait_sync_value = queue->sync_value;
			wait_sync_pointer = queue->sync_ptr;

			evt = (u64 *)kbase_phy_alloc_mapping_get(queue->kctx, wait_sync_pointer, &mapping);
			if (evt) {
				wait_sync_live_value = evt[0];
				kbase_phy_alloc_mapping_put(queue->kctx, mapping);
			} else {
				wait_sync_live_value = U64_MAX;
			}

			kbasep_csf_scheduler_dump_active_queue_cs_status_wait(
				file, wait_status, wait_sync_value,
				wait_sync_live_value, wait_sync_pointer);
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

		evt = (u64 *)kbase_phy_alloc_mapping_get(queue->kctx, wait_sync_pointer, &mapping);
		if (evt) {
			wait_sync_live_value = evt[0];
			kbase_phy_alloc_mapping_put(queue->kctx, mapping);
		} else {
			wait_sync_live_value = U64_MAX;
		}

		kbasep_csf_scheduler_dump_active_queue_cs_status_wait(
			file, wait_status, wait_sync_value,
			wait_sync_live_value, wait_sync_pointer);
	}

	seq_puts(file, "\n");
}

/* Waiting timeout for STATUS_UPDATE acknowledgment, in milliseconds */
#define CSF_STATUS_UPDATE_TO_MS (100)

static void kbasep_csf_scheduler_dump_active_group(struct seq_file *file,
		struct kbase_queue_group *const group)
{
	if (kbase_csf_scheduler_group_get_slot(group) >= 0) {
		struct kbase_device *const kbdev = group->kctx->kbdev;
		unsigned long flags;
		u32 ep_c, ep_r;
		char exclusive;
		struct kbase_csf_cmd_stream_group_info const *const ginfo =
			&kbdev->csf.global_iface.groups[group->csg_nr];
		long remaining =
			kbase_csf_timeout_in_jiffies(CSF_STATUS_UPDATE_TO_MS);
		u8 slot_priority =
			kbdev->csf.scheduler.csg_slots[group->csg_nr].priority;

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

		ep_c = kbase_csf_firmware_csg_output(ginfo,
				CSG_STATUS_EP_CURRENT);
		ep_r = kbase_csf_firmware_csg_output(ginfo, CSG_STATUS_EP_REQ);

		if (CSG_STATUS_EP_REQ_EXCLUSIVE_COMPUTE_GET(ep_r))
			exclusive = 'C';
		else if (CSG_STATUS_EP_REQ_EXCLUSIVE_FRAGMENT_GET(ep_r))
			exclusive = 'F';
		else
			exclusive = '0';

		if (!remaining) {
			dev_err(kbdev->dev,
				"Timed out for STATUS_UPDATE on group %d on slot %d",
				group->handle, group->csg_nr);

			seq_printf(file, "*** Warn: Timed out for STATUS_UPDATE on slot %d\n",
				group->csg_nr);
			seq_printf(file, "*** The following group-record is likely stale\n");
		}

		seq_puts(file, "GroupID, CSG NR, CSG Prio, Run State, Priority, C_EP(Alloc/Req), F_EP(Alloc/Req), T_EP(Alloc/Req), Exclusive\n");
		seq_printf(file, "%7d, %6d, %8d, %9d, %8d, %11d/%3d, %11d/%3d, %11d/%3d, %9c\n",
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
			exclusive);
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
#if (KERNEL_VERSION(4, 7, 0) <= LINUX_VERSION_CODE)
	const mode_t mode = 0444;
#else
	const mode_t mode = 0400;
#endif

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

DEFINE_SIMPLE_ATTRIBUTE(kbasep_csf_debugfs_scheduling_timer_enabled_fops,
		&kbasep_csf_debugfs_scheduling_timer_enabled_get,
		&kbasep_csf_debugfs_scheduling_timer_enabled_set,
		"%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(kbasep_csf_debugfs_scheduling_timer_kick_fops,
		NULL,
		&kbasep_csf_debugfs_scheduling_timer_kick_set,
		"%llu\n");

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
