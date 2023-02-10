// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2022 ARM Limited. All rights reserved.
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

#include "mali_kbase_csf_sync_debugfs.h"
#include "mali_kbase_csf_csg_debugfs.h"
#include <mali_kbase.h>
#include <linux/seq_file.h>

#if IS_ENABLED(CONFIG_SYNC_FILE)
#include "mali_kbase_sync.h"
#endif

#if IS_ENABLED(CONFIG_DEBUG_FS)

#define CQS_UNREADABLE_LIVE_VALUE "(unavailable)"

/* GPU queue related values */
#define GPU_CSF_MOVE_OPCODE ((u64)0x1)
#define GPU_CSF_MOVE32_OPCODE ((u64)0x2)
#define GPU_CSF_SYNC_ADD_OPCODE ((u64)0x25)
#define GPU_CSF_SYNC_SET_OPCODE ((u64)0x26)
#define GPU_CSF_SYNC_WAIT_OPCODE ((u64)0x27)
#define GPU_CSF_SYNC_ADD64_OPCODE ((u64)0x33)
#define GPU_CSF_SYNC_SET64_OPCODE ((u64)0x34)
#define GPU_CSF_SYNC_WAIT64_OPCODE ((u64)0x35)
#define GPU_CSF_CALL_OPCODE ((u64)0x20)

#define MAX_NR_GPU_CALLS (5)
#define INSTR_OPCODE_MASK ((u64)0xFF << 56)
#define INSTR_OPCODE_GET(value) ((value & INSTR_OPCODE_MASK) >> 56)
#define MOVE32_IMM_MASK ((u64)0xFFFFFFFFFUL)
#define MOVE_DEST_MASK ((u64)0xFF << 48)
#define MOVE_DEST_GET(value) ((value & MOVE_DEST_MASK) >> 48)
#define MOVE_IMM_MASK ((u64)0xFFFFFFFFFFFFUL)
#define SYNC_SRC0_MASK ((u64)0xFF << 40)
#define SYNC_SRC1_MASK ((u64)0xFF << 32)
#define SYNC_SRC0_GET(value) (u8)((value & SYNC_SRC0_MASK) >> 40)
#define SYNC_SRC1_GET(value) (u8)((value & SYNC_SRC1_MASK) >> 32)
#define SYNC_WAIT_CONDITION_MASK ((u64)0xF << 28)
#define SYNC_WAIT_CONDITION_GET(value) (u8)((value & SYNC_WAIT_CONDITION_MASK) >> 28)

/* Enumeration for types of GPU queue sync events for
 * the purpose of dumping them through debugfs.
 */
enum debugfs_gpu_sync_type {
	DEBUGFS_GPU_SYNC_WAIT,
	DEBUGFS_GPU_SYNC_SET,
	DEBUGFS_GPU_SYNC_ADD,
	NUM_DEBUGFS_GPU_SYNC_TYPES
};

/**
 * kbasep_csf_debugfs_get_cqs_live_u32() - Obtain live (u32) value for a CQS object.
 *
 * @kctx:     The context of the queue.
 * @obj_addr: Pointer to the CQS live 32-bit value.
 * @live_val: Pointer to the u32 that will be set to the CQS object's current, live
 *            value.
 *
 * Return: 0 if successful or a negative error code on failure.
 */
static int kbasep_csf_debugfs_get_cqs_live_u32(struct kbase_context *kctx, u64 obj_addr,
					       u32 *live_val)
{
	struct kbase_vmap_struct *mapping;
	u32 *const cpu_ptr = (u32 *)kbase_phy_alloc_mapping_get(kctx, obj_addr, &mapping);

	if (!cpu_ptr)
		return -1;

	*live_val = *cpu_ptr;
	kbase_phy_alloc_mapping_put(kctx, mapping);
	return 0;
}

/**
 * kbasep_csf_debugfs_get_cqs_live_u64() - Obtain live (u64) value for a CQS object.
 *
 * @kctx:     The context of the queue.
 * @obj_addr: Pointer to the CQS live value (32 or 64-bit).
 * @live_val: Pointer to the u64 that will be set to the CQS object's current, live
 *            value.
 *
 * Return: 0 if successful or a negative error code on failure.
 */
static int kbasep_csf_debugfs_get_cqs_live_u64(struct kbase_context *kctx, u64 obj_addr,
					       u64 *live_val)
{
	struct kbase_vmap_struct *mapping;
	u64 *cpu_ptr = (u64 *)kbase_phy_alloc_mapping_get(kctx, obj_addr, &mapping);

	if (!cpu_ptr)
		return -1;

	*live_val = *cpu_ptr;
	kbase_phy_alloc_mapping_put(kctx, mapping);
	return 0;
}

/**
 * kbasep_csf_sync_print_kcpu_fence_wait_or_signal() - Print details of a CSF SYNC Fence Wait
 *                                                     or Fence Signal command, contained in a
 *                                                     KCPU queue.
 *
 * @file:     The seq_file for printing to.
 * @cmd:      The KCPU Command to be printed.
 * @cmd_name: The name of the command: indicates either a fence SIGNAL or WAIT.
 */
static void kbasep_csf_sync_print_kcpu_fence_wait_or_signal(struct seq_file *file,
							    struct kbase_kcpu_command *cmd,
							    const char *cmd_name)
{
#if (KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE)
	struct fence *fence = NULL;
#else
	struct dma_fence *fence = NULL;
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0) */

	struct kbase_sync_fence_info info;
	const char *timeline_name = NULL;
	bool is_signaled = false;

	fence = cmd->info.fence.fence;
	if (WARN_ON(!fence))
		return;

	kbase_sync_fence_info_get(cmd->info.fence.fence, &info);
	timeline_name = fence->ops->get_timeline_name(fence);
	is_signaled = info.status > 0;

	seq_printf(file, "cmd:%s obj:0x%pK live_value:0x%.8x | ", cmd_name, cmd->info.fence.fence,
		   is_signaled);

	/* Note: fence->seqno was u32 until 5.1 kernel, then u64 */
	seq_printf(file, "timeline_name:%s timeline_context:0x%.16llx fence_seqno:0x%.16llx",
		   timeline_name, fence->context, (u64)fence->seqno);
}

/**
 * kbasep_csf_sync_print_kcpu_cqs_wait() - Print details of a CSF SYNC CQS Wait command,
 *                                         contained in a KCPU queue.
 *
 * @file: The seq_file for printing to.
 * @cmd:  The KCPU Command to be printed.
 */
static void kbasep_csf_sync_print_kcpu_cqs_wait(struct seq_file *file,
						struct kbase_kcpu_command *cmd)
{
	struct kbase_context *kctx = file->private;
	size_t i;

	for (i = 0; i < cmd->info.cqs_wait.nr_objs; i++) {
		struct base_cqs_wait_info *cqs_obj = &cmd->info.cqs_wait.objs[i];

		u32 live_val;
		int ret = kbasep_csf_debugfs_get_cqs_live_u32(kctx, cqs_obj->addr, &live_val);
		bool live_val_valid = (ret >= 0);

		seq_printf(file, "cmd:CQS_WAIT_OPERATION obj:0x%.16llx live_value:", cqs_obj->addr);

		if (live_val_valid)
			seq_printf(file, "0x%.16llx", (u64)live_val);
		else
			seq_puts(file, CQS_UNREADABLE_LIVE_VALUE);

		seq_printf(file, " | op:gt arg_value:0x%.8x", cqs_obj->val);
	}
}

/**
 * kbasep_csf_sync_print_kcpu_cqs_set() - Print details of a CSF SYNC CQS
 *                                        Set command, contained in a KCPU queue.
 *
 * @file: The seq_file for printing to.
 * @cmd:  The KCPU Command to be printed.
 */
static void kbasep_csf_sync_print_kcpu_cqs_set(struct seq_file *file,
					       struct kbase_kcpu_command *cmd)
{
	struct kbase_context *kctx = file->private;
	size_t i;

	for (i = 0; i < cmd->info.cqs_set.nr_objs; i++) {
		struct base_cqs_set *cqs_obj = &cmd->info.cqs_set.objs[i];

		u32 live_val;
		int ret = kbasep_csf_debugfs_get_cqs_live_u32(kctx, cqs_obj->addr, &live_val);
		bool live_val_valid = (ret >= 0);

		seq_printf(file, "cmd:CQS_SET_OPERATION obj:0x%.16llx live_value:", cqs_obj->addr);

		if (live_val_valid)
			seq_printf(file, "0x%.16llx", (u64)live_val);
		else
			seq_puts(file, CQS_UNREADABLE_LIVE_VALUE);

		seq_printf(file, " | op:add arg_value:0x%.8x", 1);
	}
}

/**
 * kbasep_csf_sync_get_wait_op_name() - Print the name of a CQS Wait Operation.
 *
 * @op: The numerical value of operation.
 *
 * Return: const static pointer to the command name, or '??' if unknown.
 */
static const char *kbasep_csf_sync_get_wait_op_name(basep_cqs_wait_operation_op op)
{
	const char *string;

	switch (op) {
	case BASEP_CQS_WAIT_OPERATION_LE:
		string = "le";
		break;
	case BASEP_CQS_WAIT_OPERATION_GT:
		string = "gt";
		break;
	default:
		string = "??";
		break;
	}
	return string;
}

/**
 * kbasep_csf_sync_get_set_op_name() - Print the name of a CQS Set Operation.
 *
 * @op: The numerical value of operation.
 *
 * Return: const static pointer to the command name, or '??' if unknown.
 */
static const char *kbasep_csf_sync_get_set_op_name(basep_cqs_set_operation_op op)
{
	const char *string;

	switch (op) {
	case BASEP_CQS_SET_OPERATION_ADD:
		string = "add";
		break;
	case BASEP_CQS_SET_OPERATION_SET:
		string = "set";
		break;
	default:
		string = "???";
		break;
	}
	return string;
}

/**
 * kbasep_csf_sync_print_kcpu_cqs_wait_op() - Print details of a CSF SYNC CQS
 *                                            Wait Operation command, contained
 *                                            in a KCPU queue.
 *
 * @file: The seq_file for printing to.
 * @cmd:  The KCPU Command to be printed.
 */
static void kbasep_csf_sync_print_kcpu_cqs_wait_op(struct seq_file *file,
						   struct kbase_kcpu_command *cmd)
{
	size_t i;
	struct kbase_context *kctx = file->private;

	for (i = 0; i < cmd->info.cqs_wait.nr_objs; i++) {
		struct base_cqs_wait_operation_info *wait_op =
			&cmd->info.cqs_wait_operation.objs[i];
		const char *op_name = kbasep_csf_sync_get_wait_op_name(wait_op->operation);

		u64 live_val;
		int ret = kbasep_csf_debugfs_get_cqs_live_u64(kctx, wait_op->addr, &live_val);

		bool live_val_valid = (ret >= 0);

		seq_printf(file, "cmd:CQS_WAIT_OPERATION obj:0x%.16llx live_value:", wait_op->addr);

		if (live_val_valid)
			seq_printf(file, "0x%.16llx", live_val);
		else
			seq_puts(file, CQS_UNREADABLE_LIVE_VALUE);

		seq_printf(file, " | op:%s arg_value:0x%.16llx", op_name, wait_op->val);
	}
}

/**
 * kbasep_csf_sync_print_kcpu_cqs_set_op() - Print details of a CSF SYNC CQS
 *                                           Set Operation command, contained
 *                                           in a KCPU queue.
 *
 * @file: The seq_file for printing to.
 * @cmd:  The KCPU Command to be printed.
 */
static void kbasep_csf_sync_print_kcpu_cqs_set_op(struct seq_file *file,
						  struct kbase_kcpu_command *cmd)
{
	size_t i;
	struct kbase_context *kctx = file->private;

	for (i = 0; i < cmd->info.cqs_set_operation.nr_objs; i++) {
		struct base_cqs_set_operation_info *set_op = &cmd->info.cqs_set_operation.objs[i];
		const char *op_name = kbasep_csf_sync_get_set_op_name(
			(basep_cqs_set_operation_op)set_op->operation);

		u64 live_val;
		int ret = kbasep_csf_debugfs_get_cqs_live_u64(kctx, set_op->addr, &live_val);

		bool live_val_valid = (ret >= 0);

		seq_printf(file, "cmd:CQS_SET_OPERATION obj:0x%.16llx live_value:", set_op->addr);

		if (live_val_valid)
			seq_printf(file, "0x%.16llx", live_val);
		else
			seq_puts(file, CQS_UNREADABLE_LIVE_VALUE);

		seq_printf(file, " | op:%s arg_value:0x%.16llx", op_name, set_op->val);
	}
}

/**
 * kbasep_csf_kcpu_debugfs_print_queue() - Print debug data for a KCPU queue
 *
 * @file:  The seq_file to print to.
 * @queue: Pointer to the KCPU queue.
 */
static void kbasep_csf_sync_kcpu_debugfs_print_queue(struct seq_file *file,
						     struct kbase_kcpu_command_queue *queue)
{
	char started_or_pending;
	struct kbase_kcpu_command *cmd;
	struct kbase_context *kctx = file->private;
	size_t i;

	if (WARN_ON(!queue))
		return;

	lockdep_assert_held(&kctx->csf.kcpu_queues.lock);
	mutex_lock(&queue->lock);

	for (i = 0; i != queue->num_pending_cmds; ++i) {
		started_or_pending = ((i == 0) && queue->command_started) ? 'S' : 'P';
		seq_printf(file, "queue:KCPU-%u-%u exec:%c ", kctx->id, queue->id,
			   started_or_pending);

		cmd = &queue->commands[queue->start_offset + i];
		switch (cmd->type) {
#if IS_ENABLED(CONFIG_SYNC_FILE)
		case BASE_KCPU_COMMAND_TYPE_FENCE_SIGNAL:
			kbasep_csf_sync_print_kcpu_fence_wait_or_signal(file, cmd, "FENCE_SIGNAL");
			break;
		case BASE_KCPU_COMMAND_TYPE_FENCE_WAIT:
			kbasep_csf_sync_print_kcpu_fence_wait_or_signal(file, cmd, "FENCE_WAIT");
			break;
#endif
		case BASE_KCPU_COMMAND_TYPE_CQS_WAIT:
			kbasep_csf_sync_print_kcpu_cqs_wait(file, cmd);
			break;
		case BASE_KCPU_COMMAND_TYPE_CQS_SET:
			kbasep_csf_sync_print_kcpu_cqs_set(file, cmd);
			break;
		case BASE_KCPU_COMMAND_TYPE_CQS_WAIT_OPERATION:
			kbasep_csf_sync_print_kcpu_cqs_wait_op(file, cmd);
			break;
		case BASE_KCPU_COMMAND_TYPE_CQS_SET_OPERATION:
			kbasep_csf_sync_print_kcpu_cqs_set_op(file, cmd);
			break;
		default:
			seq_puts(file, ", U, Unknown blocking command");
			break;
		}

		seq_puts(file, "\n");
	}

	mutex_unlock(&queue->lock);
}

/**
 * kbasep_csf_sync_kcpu_debugfs_show() - Print CSF KCPU queue sync info
 *
 * @file: The seq_file for printing to.
 *
 * Return: Negative error code or 0 on success.
 */
static int kbasep_csf_sync_kcpu_debugfs_show(struct seq_file *file)
{
	struct kbase_context *kctx = file->private;
	unsigned long queue_idx;

	mutex_lock(&kctx->csf.kcpu_queues.lock);
	seq_printf(file, "KCPU queues for ctx %u:\n", kctx->id);

	queue_idx = find_first_bit(kctx->csf.kcpu_queues.in_use, KBASEP_MAX_KCPU_QUEUES);

	while (queue_idx < KBASEP_MAX_KCPU_QUEUES) {
		kbasep_csf_sync_kcpu_debugfs_print_queue(file,
							 kctx->csf.kcpu_queues.array[queue_idx]);

		queue_idx = find_next_bit(kctx->csf.kcpu_queues.in_use, KBASEP_MAX_KCPU_QUEUES,
					  queue_idx + 1);
	}

	mutex_unlock(&kctx->csf.kcpu_queues.lock);
	return 0;
}

/**
 * kbasep_csf_get_move_immediate_value() - Get the immediate values for sync operations
 *                                         from a MOVE instruction.
 *
 * @move_cmd:        Raw MOVE instruction.
 * @sync_addr_reg:   Register identifier from SYNC_* instruction.
 * @compare_val_reg: Register identifier from SYNC_* instruction.
 * @sync_val:        Pointer to store CQS object address for sync operation.
 * @compare_val:     Pointer to store compare value for sync operation.
 *
 * Return: True if value is obtained by checking for correct register identifier,
 * or false otherwise.
 */
static bool kbasep_csf_get_move_immediate_value(u64 move_cmd, u64 sync_addr_reg,
						u64 compare_val_reg, u64 *sync_val,
						u64 *compare_val)
{
	u64 imm_mask;

	/* Verify MOVE instruction and get immediate mask */
	if (INSTR_OPCODE_GET(move_cmd) == GPU_CSF_MOVE32_OPCODE)
		imm_mask = MOVE32_IMM_MASK;
	else if (INSTR_OPCODE_GET(move_cmd) == GPU_CSF_MOVE_OPCODE)
		imm_mask = MOVE_IMM_MASK;
	else
		/* Error return */
		return false;

	/* Verify value from MOVE instruction and assign to variable */
	if (sync_addr_reg == MOVE_DEST_GET(move_cmd))
		*sync_val = move_cmd & imm_mask;
	else if (compare_val_reg == MOVE_DEST_GET(move_cmd))
		*compare_val = move_cmd & imm_mask;
	else
		/* Error return */
		return false;

	return true;
}

/** kbasep_csf_read_ringbuffer_value() - Reads a u64 from the ringbuffer at a provided
 *                                       offset.
 *
 * @queue:            Pointer to the queue.
 * @ringbuff_offset:  Ringbuffer offset.
 *
 * Return: the u64 in the ringbuffer at the desired offset.
 */
static u64 kbasep_csf_read_ringbuffer_value(struct kbase_queue *queue, u32 ringbuff_offset)
{
	u64 page_off = ringbuff_offset >> PAGE_SHIFT;
	u64 offset_within_page = ringbuff_offset & ~PAGE_MASK;
	struct page *page = as_page(queue->queue_reg->gpu_alloc->pages[page_off]);
	u64 *ringbuffer = kmap_atomic(page);
	u64 value = ringbuffer[offset_within_page / sizeof(u64)];

	kunmap_atomic(ringbuffer);
	return value;
}

/**
 * kbasep_csf_print_gpu_sync_op() - Print sync operation info for given sync command.
 *
 * @file:             Pointer to debugfs seq_file file struct for writing output.
 * @kctx:             Pointer to kbase context.
 * @queue:            Pointer to the GPU command queue.
 * @ringbuff_offset:  Offset to index the ring buffer with, for the given sync command.
 *                    (Useful for finding preceding MOVE commands)
 * @sync_cmd:         Entire u64 of the sync command, which has both sync address and
 *                    comparison-value encoded in it.
 * @type:             Type of GPU sync command (e.g. SYNC_SET, SYNC_ADD, SYNC_WAIT).
 * @is_64bit:         Bool to indicate if operation is 64 bit (true) or 32 bit (false).
 * @follows_wait:     Bool to indicate if the operation follows at least one wait
 *                    operation. Used to determine whether it's pending or started.
 */
static void kbasep_csf_print_gpu_sync_op(struct seq_file *file, struct kbase_context *kctx,
					 struct kbase_queue *queue, u32 ringbuff_offset,
					 u64 sync_cmd, enum debugfs_gpu_sync_type type,
					 bool is_64bit, bool follows_wait)
{
	u64 sync_addr = 0, compare_val = 0, live_val = 0;
	u64 move_cmd;
	u8 sync_addr_reg, compare_val_reg, wait_condition = 0;
	int err;

	static const char *const gpu_sync_type_name[] = { "SYNC_WAIT", "SYNC_SET", "SYNC_ADD" };
	static const char *const gpu_sync_type_op[] = {
		"wait", /* This should never be printed, only included to simplify indexing */
		"set", "add"
	};

	if (type >= NUM_DEBUGFS_GPU_SYNC_TYPES) {
		dev_warn(kctx->kbdev->dev, "Expected GPU queue sync type is unknown!");
		return;
	}

	/* We expect there to be at least 2 preceding MOVE instructions, and
	 * Base will always arrange for the 2 MOVE + SYNC instructions to be
	 * contiguously located, and is therefore never expected to be wrapped
	 * around the ringbuffer boundary.
	 */
	if (unlikely(ringbuff_offset < (2 * sizeof(u64)))) {
		dev_warn(kctx->kbdev->dev,
			 "Unexpected wraparound detected between %s & MOVE instruction",
			 gpu_sync_type_name[type]);
		return;
	}

	/* 1. Get Register identifiers from SYNC_* instruction */
	sync_addr_reg = SYNC_SRC0_GET(sync_cmd);
	compare_val_reg = SYNC_SRC1_GET(sync_cmd);

	/* 2. Get values from first MOVE command */
	ringbuff_offset -= sizeof(u64);
	move_cmd = kbasep_csf_read_ringbuffer_value(queue, ringbuff_offset);
	if (!kbasep_csf_get_move_immediate_value(move_cmd, sync_addr_reg, compare_val_reg,
						 &sync_addr, &compare_val))
		return;

	/* 3. Get values from next MOVE command */
	ringbuff_offset -= sizeof(u64);
	move_cmd = kbasep_csf_read_ringbuffer_value(queue, ringbuff_offset);
	if (!kbasep_csf_get_move_immediate_value(move_cmd, sync_addr_reg, compare_val_reg,
						 &sync_addr, &compare_val))
		return;

	/* 4. Get CQS object value */
	if (is_64bit)
		err = kbasep_csf_debugfs_get_cqs_live_u64(kctx, sync_addr, &live_val);
	else
		err = kbasep_csf_debugfs_get_cqs_live_u32(kctx, sync_addr, (u32 *)(&live_val));

	if (err)
		return;

	/* 5. Print info */
	seq_printf(file, "queue:GPU-%u-%u-%u exec:%c cmd:%s ", kctx->id, queue->group->handle,
		   queue->csi_index, queue->enabled && !follows_wait ? 'S' : 'P',
		   gpu_sync_type_name[type]);

	if (queue->group->csg_nr == KBASEP_CSG_NR_INVALID)
		seq_puts(file, "slot:-");
	else
		seq_printf(file, "slot:%d", (int)queue->group->csg_nr);

	seq_printf(file, " obj:0x%.16llx live_value:0x%.16llx | ", sync_addr, live_val);

	if (type == DEBUGFS_GPU_SYNC_WAIT) {
		wait_condition = SYNC_WAIT_CONDITION_GET(sync_cmd);
		seq_printf(file, "op:%s ", kbasep_csf_sync_get_wait_op_name(wait_condition));
	} else
		seq_printf(file, "op:%s ", gpu_sync_type_op[type]);

	seq_printf(file, "arg_value:0x%.16llx\n", compare_val);
}

/**
 * kbasep_csf_dump_active_queue_sync_info() - Print GPU command queue sync information.
 *
 * @file:  seq_file for printing to.
 * @queue: Address of a GPU command queue to examine.
 *
 * This function will iterate through each command in the ring buffer of the given GPU queue from
 * CS_EXTRACT, and if is a SYNC_* instruction it will attempt to decode the sync operation and
 * print relevant information to the debugfs file.
 * This function will stop iterating once the CS_INSERT address is reached by the cursor (i.e.
 * when there are no more commands to view) or a number of consumed GPU CALL commands have
 * been observed.
 */
static void kbasep_csf_dump_active_queue_sync_info(struct seq_file *file, struct kbase_queue *queue)
{
	struct kbase_context *kctx;
	u32 *addr;
	u64 cs_extract, cs_insert, instr, cursor;
	bool follows_wait = false;
	int nr_calls = 0;

	if (!queue)
		return;

	kctx = queue->kctx;

	addr = (u32 *)queue->user_io_addr;
	cs_insert = addr[CS_INSERT_LO / 4] | ((u64)addr[CS_INSERT_HI / 4] << 32);

	addr = (u32 *)(queue->user_io_addr + PAGE_SIZE);
	cs_extract = addr[CS_EXTRACT_LO / 4] | ((u64)addr[CS_EXTRACT_HI / 4] << 32);

	cursor = cs_extract;

	if (!is_power_of_2(queue->size)) {
		dev_warn(kctx->kbdev->dev, "GPU queue %u size of %u not a power of 2",
			 queue->csi_index, queue->size);
		return;
	}

	while ((cursor < cs_insert) && (nr_calls < MAX_NR_GPU_CALLS)) {
		bool instr_is_64_bit = false;
		/* Calculate offset into ringbuffer from the absolute cursor,
		 * by finding the remainder of the cursor divided by the
		 * ringbuffer size. The ringbuffer size is guaranteed to be
		 * a power of 2, so the remainder can be calculated without an
		 * explicit modulo. queue->size - 1 is the ringbuffer mask.
		 */
		u32 cursor_ringbuff_offset = (u32)(cursor & (queue->size - 1));

		/* Find instruction that cursor is currently on */
		instr = kbasep_csf_read_ringbuffer_value(queue, cursor_ringbuff_offset);

		switch (INSTR_OPCODE_GET(instr)) {
		case GPU_CSF_SYNC_ADD64_OPCODE:
		case GPU_CSF_SYNC_SET64_OPCODE:
		case GPU_CSF_SYNC_WAIT64_OPCODE:
			instr_is_64_bit = true;
		default:
			break;
		}

		switch (INSTR_OPCODE_GET(instr)) {
		case GPU_CSF_SYNC_ADD_OPCODE:
		case GPU_CSF_SYNC_ADD64_OPCODE:
			kbasep_csf_print_gpu_sync_op(file, kctx, queue, cursor_ringbuff_offset,
						     instr, DEBUGFS_GPU_SYNC_ADD, instr_is_64_bit,
						     follows_wait);
			break;
		case GPU_CSF_SYNC_SET_OPCODE:
		case GPU_CSF_SYNC_SET64_OPCODE:
			kbasep_csf_print_gpu_sync_op(file, kctx, queue, cursor_ringbuff_offset,
						     instr, DEBUGFS_GPU_SYNC_SET, instr_is_64_bit,
						     follows_wait);
			break;
		case GPU_CSF_SYNC_WAIT_OPCODE:
		case GPU_CSF_SYNC_WAIT64_OPCODE:
			kbasep_csf_print_gpu_sync_op(file, kctx, queue, cursor_ringbuff_offset,
						     instr, DEBUGFS_GPU_SYNC_WAIT, instr_is_64_bit,
						     follows_wait);
			follows_wait = true; /* Future commands will follow at least one wait */
			break;
		case GPU_CSF_CALL_OPCODE:
			nr_calls++;
			/* Fallthrough */
		default:
			/* Unrecognized command, skip past it */
			break;
		}

		cursor += sizeof(u64);
	}
}

/**
 * kbasep_csf_dump_active_group_sync_state() - Prints SYNC commands in all GPU queues of
 *                                             the provided queue group.
 *
 * @file:  seq_file for printing to.
 * @group: Address of a GPU command group to iterate through.
 *
 * This function will iterate through each queue in the provided GPU queue group and
 * print its SYNC related commands.
 */
static void kbasep_csf_dump_active_group_sync_state(struct seq_file *file,
						    struct kbase_queue_group *const group)
{
	struct kbase_context *kctx = file->private;
	unsigned int i;

	seq_printf(file, "GPU queues for group %u (slot %d) of ctx %d_%d\n", group->handle,
		   group->csg_nr, kctx->tgid, kctx->id);

	for (i = 0; i < MAX_SUPPORTED_STREAMS_PER_GROUP; i++)
		kbasep_csf_dump_active_queue_sync_info(file, group->bound_queues[i]);
}

/**
 * kbasep_csf_sync_gpu_debugfs_show() - Print CSF GPU queue sync info
 *
 * @file: The seq_file for printing to.
 *
 * Return: Negative error code or 0 on success.
 */
static int kbasep_csf_sync_gpu_debugfs_show(struct seq_file *file)
{
	u32 gr;
	struct kbase_context *kctx = file->private;
	struct kbase_device *kbdev;

	if (WARN_ON(!kctx))
		return -EINVAL;

	kbdev = kctx->kbdev;
	kbase_csf_scheduler_lock(kbdev);
	kbase_csf_debugfs_update_active_groups_status(kbdev);

	for (gr = 0; gr < kbdev->csf.global_iface.group_num; gr++) {
		struct kbase_queue_group *const group =
			kbdev->csf.scheduler.csg_slots[gr].resident_group;
		if (!group || group->kctx != kctx)
			continue;
		kbasep_csf_dump_active_group_sync_state(file, group);
	}

	kbase_csf_scheduler_unlock(kbdev);
	return 0;
}

/**
 * kbasep_csf_sync_debugfs_show() - Print CSF queue sync information
 *
 * @file: The seq_file for printing to.
 * @data: The debugfs dentry private data, a pointer to kbase_context.
 *
 * Return: Negative error code or 0 on success.
 */
static int kbasep_csf_sync_debugfs_show(struct seq_file *file, void *data)
{
	seq_printf(file, "MALI_CSF_SYNC_DEBUGFS_VERSION: v%u\n", MALI_CSF_SYNC_DEBUGFS_VERSION);

	kbasep_csf_sync_kcpu_debugfs_show(file);
	kbasep_csf_sync_gpu_debugfs_show(file);
	return 0;
}

static int kbasep_csf_sync_debugfs_open(struct inode *in, struct file *file)
{
	return single_open(file, kbasep_csf_sync_debugfs_show, in->i_private);
}

static const struct file_operations kbasep_csf_sync_debugfs_fops = {
	.open = kbasep_csf_sync_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/**
 * kbase_csf_sync_debugfs_init() - Initialise debugfs file.
 *
 * @kctx: Kernel context pointer.
 */
void kbase_csf_sync_debugfs_init(struct kbase_context *kctx)
{
	struct dentry *file;
	const mode_t mode = 0444;

	if (WARN_ON(!kctx || IS_ERR_OR_NULL(kctx->kctx_dentry)))
		return;

	file = debugfs_create_file("csf_sync", mode, kctx->kctx_dentry, kctx,
				   &kbasep_csf_sync_debugfs_fops);

	if (IS_ERR_OR_NULL(file))
		dev_warn(kctx->kbdev->dev, "Unable to create CSF Sync debugfs entry");
}

#else
/*
 * Stub functions for when debugfs is disabled
 */
void kbase_csf_sync_debugfs_init(struct kbase_context *kctx)
{
}

#endif /* CONFIG_DEBUG_FS */
