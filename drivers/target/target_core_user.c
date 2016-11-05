/*
 * Copyright (C) 2013 Shaohua Li <shli@kernel.org>
 * Copyright (C) 2014 Red Hat, Inc.
 * Copyright (C) 2015 Arrikto, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/parser.h>
#include <linux/vmalloc.h>
#include <linux/uio_driver.h>
#include <linux/stringify.h>
#include <linux/bitops.h>
#include <net/genetlink.h>
#include <scsi/scsi_common.h>
#include <scsi/scsi_proto.h>
#include <target/target_core_base.h>
#include <target/target_core_fabric.h>
#include <target/target_core_backend.h>

#include <linux/target_core_user.h>

/*
 * Define a shared-memory interface for LIO to pass SCSI commands and
 * data to userspace for processing. This is to allow backends that
 * are too complex for in-kernel support to be possible.
 *
 * It uses the UIO framework to do a lot of the device-creation and
 * introspection work for us.
 *
 * See the .h file for how the ring is laid out. Note that while the
 * command ring is defined, the particulars of the data area are
 * not. Offset values in the command entry point to other locations
 * internal to the mmap()ed area. There is separate space outside the
 * command ring for data buffers. This leaves maximum flexibility for
 * moving buffer allocations, or even page flipping or other
 * allocation techniques, without altering the command ring layout.
 *
 * SECURITY:
 * The user process must be assumed to be malicious. There's no way to
 * prevent it breaking the command ring protocol if it wants, but in
 * order to prevent other issues we must only ever read *data* from
 * the shared memory area, not offsets or sizes. This applies to
 * command ring entries as well as the mailbox. Extra code needed for
 * this may have a 'UAM' comment.
 */


#define TCMU_TIME_OUT (30 * MSEC_PER_SEC)

#define DATA_BLOCK_BITS 256
#define DATA_BLOCK_SIZE 4096

#define CMDR_SIZE (16 * 4096)
#define DATA_SIZE (DATA_BLOCK_BITS * DATA_BLOCK_SIZE)

#define TCMU_RING_SIZE (CMDR_SIZE + DATA_SIZE)

static struct device *tcmu_root_device;

struct tcmu_hba {
	u32 host_id;
};

#define TCMU_CONFIG_LEN 256

struct tcmu_dev {
	struct se_device se_dev;

	char *name;
	struct se_hba *hba;

#define TCMU_DEV_BIT_OPEN 0
#define TCMU_DEV_BIT_BROKEN 1
	unsigned long flags;

	struct uio_info uio_info;

	struct tcmu_mailbox *mb_addr;
	size_t dev_size;
	u32 cmdr_size;
	u32 cmdr_last_cleaned;
	/* Offset of data area from start of mb */
	/* Must add data_off and mb_addr to get the address */
	size_t data_off;
	size_t data_size;

	DECLARE_BITMAP(data_bitmap, DATA_BLOCK_BITS);

	wait_queue_head_t wait_cmdr;
	/* TODO should this be a mutex? */
	spinlock_t cmdr_lock;

	struct idr commands;
	spinlock_t commands_lock;

	struct timer_list timeout;

	char dev_config[TCMU_CONFIG_LEN];
};

#define TCMU_DEV(_se_dev) container_of(_se_dev, struct tcmu_dev, se_dev)

#define CMDR_OFF sizeof(struct tcmu_mailbox)

struct tcmu_cmd {
	struct se_cmd *se_cmd;
	struct tcmu_dev *tcmu_dev;

	uint16_t cmd_id;

	/* Can't use se_cmd when cleaning up expired cmds, because if
	   cmd has been completed then accessing se_cmd is off limits */
	DECLARE_BITMAP(data_bitmap, DATA_BLOCK_BITS);

	unsigned long deadline;

#define TCMU_CMD_BIT_EXPIRED 0
	unsigned long flags;
};

static struct kmem_cache *tcmu_cmd_cache;

/* multicast group */
enum tcmu_multicast_groups {
	TCMU_MCGRP_CONFIG,
};

static const struct genl_multicast_group tcmu_mcgrps[] = {
	[TCMU_MCGRP_CONFIG] = { .name = "config", },
};

/* Our generic netlink family */
static struct genl_family tcmu_genl_family = {
	.id = GENL_ID_GENERATE,
	.hdrsize = 0,
	.name = "TCM-USER",
	.version = 1,
	.maxattr = TCMU_ATTR_MAX,
	.mcgrps = tcmu_mcgrps,
	.n_mcgrps = ARRAY_SIZE(tcmu_mcgrps),
	.netnsok = true,
};

static struct tcmu_cmd *tcmu_alloc_cmd(struct se_cmd *se_cmd)
{
	struct se_device *se_dev = se_cmd->se_dev;
	struct tcmu_dev *udev = TCMU_DEV(se_dev);
	struct tcmu_cmd *tcmu_cmd;
	int cmd_id;

	tcmu_cmd = kmem_cache_zalloc(tcmu_cmd_cache, GFP_KERNEL);
	if (!tcmu_cmd)
		return NULL;

	tcmu_cmd->se_cmd = se_cmd;
	tcmu_cmd->tcmu_dev = udev;
	tcmu_cmd->deadline = jiffies + msecs_to_jiffies(TCMU_TIME_OUT);

	idr_preload(GFP_KERNEL);
	spin_lock_irq(&udev->commands_lock);
	cmd_id = idr_alloc(&udev->commands, tcmu_cmd, 0,
		USHRT_MAX, GFP_NOWAIT);
	spin_unlock_irq(&udev->commands_lock);
	idr_preload_end();

	if (cmd_id < 0) {
		kmem_cache_free(tcmu_cmd_cache, tcmu_cmd);
		return NULL;
	}
	tcmu_cmd->cmd_id = cmd_id;

	return tcmu_cmd;
}

static inline void tcmu_flush_dcache_range(void *vaddr, size_t size)
{
	unsigned long offset = offset_in_page(vaddr);

	size = round_up(size+offset, PAGE_SIZE);
	vaddr -= offset;

	while (size) {
		flush_dcache_page(virt_to_page(vaddr));
		size -= PAGE_SIZE;
	}
}

/*
 * Some ring helper functions. We don't assume size is a power of 2 so
 * we can't use circ_buf.h.
 */
static inline size_t spc_used(size_t head, size_t tail, size_t size)
{
	int diff = head - tail;

	if (diff >= 0)
		return diff;
	else
		return size + diff;
}

static inline size_t spc_free(size_t head, size_t tail, size_t size)
{
	/* Keep 1 byte unused or we can't tell full from empty */
	return (size - spc_used(head, tail, size) - 1);
}

static inline size_t head_to_end(size_t head, size_t size)
{
	return size - head;
}

static inline void new_iov(struct iovec **iov, int *iov_cnt,
			   struct tcmu_dev *udev)
{
	struct iovec *iovec;

	if (*iov_cnt != 0)
		(*iov)++;
	(*iov_cnt)++;

	iovec = *iov;
	memset(iovec, 0, sizeof(struct iovec));
}

#define UPDATE_HEAD(head, used, size) smp_store_release(&head, ((head % size) + used) % size)

/* offset is relative to mb_addr */
static inline size_t get_block_offset(struct tcmu_dev *dev,
		int block, int remaining)
{
	return dev->data_off + block * DATA_BLOCK_SIZE +
		DATA_BLOCK_SIZE - remaining;
}

static inline size_t iov_tail(struct tcmu_dev *udev, struct iovec *iov)
{
	return (size_t)iov->iov_base + iov->iov_len;
}

static void alloc_and_scatter_data_area(struct tcmu_dev *udev,
	struct scatterlist *data_sg, unsigned int data_nents,
	struct iovec **iov, int *iov_cnt, bool copy_data)
{
	int i, block;
	int block_remaining = 0;
	void *from, *to;
	size_t copy_bytes, to_offset;
	struct scatterlist *sg;

	for_each_sg(data_sg, sg, data_nents, i) {
		int sg_remaining = sg->length;
		from = kmap_atomic(sg_page(sg)) + sg->offset;
		while (sg_remaining > 0) {
			if (block_remaining == 0) {
				block = find_first_zero_bit(udev->data_bitmap,
						DATA_BLOCK_BITS);
				block_remaining = DATA_BLOCK_SIZE;
				set_bit(block, udev->data_bitmap);
			}
			copy_bytes = min_t(size_t, sg_remaining,
					block_remaining);
			to_offset = get_block_offset(udev, block,
					block_remaining);
			to = (void *)udev->mb_addr + to_offset;
			if (*iov_cnt != 0 &&
			    to_offset == iov_tail(udev, *iov)) {
				(*iov)->iov_len += copy_bytes;
			} else {
				new_iov(iov, iov_cnt, udev);
				(*iov)->iov_base = (void __user *) to_offset;
				(*iov)->iov_len = copy_bytes;
			}
			if (copy_data) {
				memcpy(to, from + sg->length - sg_remaining,
					copy_bytes);
				tcmu_flush_dcache_range(to, copy_bytes);
			}
			sg_remaining -= copy_bytes;
			block_remaining -= copy_bytes;
		}
		kunmap_atomic(from - sg->offset);
	}
}

static void free_data_area(struct tcmu_dev *udev, struct tcmu_cmd *cmd)
{
	bitmap_xor(udev->data_bitmap, udev->data_bitmap, cmd->data_bitmap,
		   DATA_BLOCK_BITS);
}

static void gather_data_area(struct tcmu_dev *udev, unsigned long *cmd_bitmap,
		struct scatterlist *data_sg, unsigned int data_nents)
{
	int i, block;
	int block_remaining = 0;
	void *from, *to;
	size_t copy_bytes, from_offset;
	struct scatterlist *sg;

	for_each_sg(data_sg, sg, data_nents, i) {
		int sg_remaining = sg->length;
		to = kmap_atomic(sg_page(sg)) + sg->offset;
		while (sg_remaining > 0) {
			if (block_remaining == 0) {
				block = find_first_bit(cmd_bitmap,
						DATA_BLOCK_BITS);
				block_remaining = DATA_BLOCK_SIZE;
				clear_bit(block, cmd_bitmap);
			}
			copy_bytes = min_t(size_t, sg_remaining,
					block_remaining);
			from_offset = get_block_offset(udev, block,
					block_remaining);
			from = (void *) udev->mb_addr + from_offset;
			tcmu_flush_dcache_range(from, copy_bytes);
			memcpy(to + sg->length - sg_remaining, from,
					copy_bytes);

			sg_remaining -= copy_bytes;
			block_remaining -= copy_bytes;
		}
		kunmap_atomic(to - sg->offset);
	}
}

static inline size_t spc_bitmap_free(unsigned long *bitmap)
{
	return DATA_BLOCK_SIZE * (DATA_BLOCK_BITS -
			bitmap_weight(bitmap, DATA_BLOCK_BITS));
}

/*
 * We can't queue a command until we have space available on the cmd ring *and*
 * space available on the data area.
 *
 * Called with ring lock held.
 */
static bool is_ring_space_avail(struct tcmu_dev *udev, size_t cmd_size, size_t data_needed)
{
	struct tcmu_mailbox *mb = udev->mb_addr;
	size_t space, cmd_needed;
	u32 cmd_head;

	tcmu_flush_dcache_range(mb, sizeof(*mb));

	cmd_head = mb->cmd_head % udev->cmdr_size; /* UAM */

	/*
	 * If cmd end-of-ring space is too small then we need space for a NOP plus
	 * original cmd - cmds are internally contiguous.
	 */
	if (head_to_end(cmd_head, udev->cmdr_size) >= cmd_size)
		cmd_needed = cmd_size;
	else
		cmd_needed = cmd_size + head_to_end(cmd_head, udev->cmdr_size);

	space = spc_free(cmd_head, udev->cmdr_last_cleaned, udev->cmdr_size);
	if (space < cmd_needed) {
		pr_debug("no cmd space: %u %u %u\n", cmd_head,
		       udev->cmdr_last_cleaned, udev->cmdr_size);
		return false;
	}

	space = spc_bitmap_free(udev->data_bitmap);
	if (space < data_needed) {
		pr_debug("no data space: only %zu available, but ask for %zu\n",
				space, data_needed);
		return false;
	}

	return true;
}

static sense_reason_t
tcmu_queue_cmd_ring(struct tcmu_cmd *tcmu_cmd)
{
	struct tcmu_dev *udev = tcmu_cmd->tcmu_dev;
	struct se_cmd *se_cmd = tcmu_cmd->se_cmd;
	size_t base_command_size, command_size;
	struct tcmu_mailbox *mb;
	struct tcmu_cmd_entry *entry;
	struct iovec *iov;
	int iov_cnt;
	uint32_t cmd_head;
	uint64_t cdb_off;
	bool copy_to_data_area;
	size_t data_length;
	DECLARE_BITMAP(old_bitmap, DATA_BLOCK_BITS);

	if (test_bit(TCMU_DEV_BIT_BROKEN, &udev->flags))
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	/*
	 * Must be a certain minimum size for response sense info, but
	 * also may be larger if the iov array is large.
	 *
	 * We prepare way too many iovs for potential uses here, because it's
	 * expensive to tell how many regions are freed in the bitmap
	*/
	base_command_size = max(offsetof(struct tcmu_cmd_entry,
				req.iov[se_cmd->t_bidi_data_nents +
					se_cmd->t_data_nents]),
				sizeof(struct tcmu_cmd_entry));
	command_size = base_command_size
		+ round_up(scsi_command_size(se_cmd->t_task_cdb), TCMU_OP_ALIGN_SIZE);

	WARN_ON(command_size & (TCMU_OP_ALIGN_SIZE-1));

	spin_lock_irq(&udev->cmdr_lock);

	mb = udev->mb_addr;
	cmd_head = mb->cmd_head % udev->cmdr_size; /* UAM */
	data_length = se_cmd->data_length;
	if (se_cmd->se_cmd_flags & SCF_BIDI) {
		BUG_ON(!(se_cmd->t_bidi_data_sg && se_cmd->t_bidi_data_nents));
		data_length += se_cmd->t_bidi_data_sg->length;
	}
	if ((command_size > (udev->cmdr_size / 2)) ||
	    data_length > udev->data_size) {
		pr_warn("TCMU: Request of size %zu/%zu is too big for %u/%zu "
			"cmd ring/data area\n", command_size, data_length,
			udev->cmdr_size, udev->data_size);
		spin_unlock_irq(&udev->cmdr_lock);
		return TCM_INVALID_CDB_FIELD;
	}

	while (!is_ring_space_avail(udev, command_size, data_length)) {
		int ret;
		DEFINE_WAIT(__wait);

		prepare_to_wait(&udev->wait_cmdr, &__wait, TASK_INTERRUPTIBLE);

		pr_debug("sleeping for ring space\n");
		spin_unlock_irq(&udev->cmdr_lock);
		ret = schedule_timeout(msecs_to_jiffies(TCMU_TIME_OUT));
		finish_wait(&udev->wait_cmdr, &__wait);
		if (!ret) {
			pr_warn("tcmu: command timed out\n");
			return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		}

		spin_lock_irq(&udev->cmdr_lock);

		/* We dropped cmdr_lock, cmd_head is stale */
		cmd_head = mb->cmd_head % udev->cmdr_size; /* UAM */
	}

	/* Insert a PAD if end-of-ring space is too small */
	if (head_to_end(cmd_head, udev->cmdr_size) < command_size) {
		size_t pad_size = head_to_end(cmd_head, udev->cmdr_size);

		entry = (void *) mb + CMDR_OFF + cmd_head;
		tcmu_flush_dcache_range(entry, sizeof(*entry));
		tcmu_hdr_set_op(&entry->hdr.len_op, TCMU_OP_PAD);
		tcmu_hdr_set_len(&entry->hdr.len_op, pad_size);
		entry->hdr.cmd_id = 0; /* not used for PAD */
		entry->hdr.kflags = 0;
		entry->hdr.uflags = 0;

		UPDATE_HEAD(mb->cmd_head, pad_size, udev->cmdr_size);

		cmd_head = mb->cmd_head % udev->cmdr_size; /* UAM */
		WARN_ON(cmd_head != 0);
	}

	entry = (void *) mb + CMDR_OFF + cmd_head;
	tcmu_flush_dcache_range(entry, sizeof(*entry));
	tcmu_hdr_set_op(&entry->hdr.len_op, TCMU_OP_CMD);
	tcmu_hdr_set_len(&entry->hdr.len_op, command_size);
	entry->hdr.cmd_id = tcmu_cmd->cmd_id;
	entry->hdr.kflags = 0;
	entry->hdr.uflags = 0;

	bitmap_copy(old_bitmap, udev->data_bitmap, DATA_BLOCK_BITS);

	/* Handle allocating space from the data area */
	iov = &entry->req.iov[0];
	iov_cnt = 0;
	copy_to_data_area = (se_cmd->data_direction == DMA_TO_DEVICE
		|| se_cmd->se_cmd_flags & SCF_BIDI);
	alloc_and_scatter_data_area(udev, se_cmd->t_data_sg,
		se_cmd->t_data_nents, &iov, &iov_cnt, copy_to_data_area);
	entry->req.iov_cnt = iov_cnt;
	entry->req.iov_dif_cnt = 0;

	/* Handle BIDI commands */
	iov_cnt = 0;
	alloc_and_scatter_data_area(udev, se_cmd->t_bidi_data_sg,
		se_cmd->t_bidi_data_nents, &iov, &iov_cnt, false);
	entry->req.iov_bidi_cnt = iov_cnt;

	/* cmd's data_bitmap is what changed in process */
	bitmap_xor(tcmu_cmd->data_bitmap, old_bitmap, udev->data_bitmap,
			DATA_BLOCK_BITS);

	/* All offsets relative to mb_addr, not start of entry! */
	cdb_off = CMDR_OFF + cmd_head + base_command_size;
	memcpy((void *) mb + cdb_off, se_cmd->t_task_cdb, scsi_command_size(se_cmd->t_task_cdb));
	entry->req.cdb_off = cdb_off;
	tcmu_flush_dcache_range(entry, sizeof(*entry));

	UPDATE_HEAD(mb->cmd_head, command_size, udev->cmdr_size);
	tcmu_flush_dcache_range(mb, sizeof(*mb));

	spin_unlock_irq(&udev->cmdr_lock);

	/* TODO: only if FLUSH and FUA? */
	uio_event_notify(&udev->uio_info);

	mod_timer(&udev->timeout,
		round_jiffies_up(jiffies + msecs_to_jiffies(TCMU_TIME_OUT)));

	return TCM_NO_SENSE;
}

static sense_reason_t
tcmu_queue_cmd(struct se_cmd *se_cmd)
{
	struct se_device *se_dev = se_cmd->se_dev;
	struct tcmu_dev *udev = TCMU_DEV(se_dev);
	struct tcmu_cmd *tcmu_cmd;
	int ret;

	tcmu_cmd = tcmu_alloc_cmd(se_cmd);
	if (!tcmu_cmd)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	ret = tcmu_queue_cmd_ring(tcmu_cmd);
	if (ret != TCM_NO_SENSE) {
		pr_err("TCMU: Could not queue command\n");
		spin_lock_irq(&udev->commands_lock);
		idr_remove(&udev->commands, tcmu_cmd->cmd_id);
		spin_unlock_irq(&udev->commands_lock);

		kmem_cache_free(tcmu_cmd_cache, tcmu_cmd);
	}

	return ret;
}

static void tcmu_handle_completion(struct tcmu_cmd *cmd, struct tcmu_cmd_entry *entry)
{
	struct se_cmd *se_cmd = cmd->se_cmd;
	struct tcmu_dev *udev = cmd->tcmu_dev;

	if (test_bit(TCMU_CMD_BIT_EXPIRED, &cmd->flags)) {
		/*
		 * cmd has been completed already from timeout, just reclaim
		 * data area space and free cmd
		 */
		free_data_area(udev, cmd);

		kmem_cache_free(tcmu_cmd_cache, cmd);
		return;
	}

	if (entry->hdr.uflags & TCMU_UFLAG_UNKNOWN_OP) {
		free_data_area(udev, cmd);
		pr_warn("TCMU: Userspace set UNKNOWN_OP flag on se_cmd %p\n",
			cmd->se_cmd);
		entry->rsp.scsi_status = SAM_STAT_CHECK_CONDITION;
	} else if (entry->rsp.scsi_status == SAM_STAT_CHECK_CONDITION) {
		memcpy(se_cmd->sense_buffer, entry->rsp.sense_buffer,
			       se_cmd->scsi_sense_length);
		free_data_area(udev, cmd);
	} else if (se_cmd->se_cmd_flags & SCF_BIDI) {
		DECLARE_BITMAP(bitmap, DATA_BLOCK_BITS);

		/* Get Data-In buffer before clean up */
		bitmap_copy(bitmap, cmd->data_bitmap, DATA_BLOCK_BITS);
		gather_data_area(udev, bitmap,
			se_cmd->t_bidi_data_sg, se_cmd->t_bidi_data_nents);
		free_data_area(udev, cmd);
	} else if (se_cmd->data_direction == DMA_FROM_DEVICE) {
		DECLARE_BITMAP(bitmap, DATA_BLOCK_BITS);

		bitmap_copy(bitmap, cmd->data_bitmap, DATA_BLOCK_BITS);
		gather_data_area(udev, bitmap,
			se_cmd->t_data_sg, se_cmd->t_data_nents);
		free_data_area(udev, cmd);
	} else if (se_cmd->data_direction == DMA_TO_DEVICE) {
		free_data_area(udev, cmd);
	} else if (se_cmd->data_direction != DMA_NONE) {
		pr_warn("TCMU: data direction was %d!\n",
			se_cmd->data_direction);
	}

	target_complete_cmd(cmd->se_cmd, entry->rsp.scsi_status);
	cmd->se_cmd = NULL;

	kmem_cache_free(tcmu_cmd_cache, cmd);
}

static unsigned int tcmu_handle_completions(struct tcmu_dev *udev)
{
	struct tcmu_mailbox *mb;
	unsigned long flags;
	int handled = 0;

	if (test_bit(TCMU_DEV_BIT_BROKEN, &udev->flags)) {
		pr_err("ring broken, not handling completions\n");
		return 0;
	}

	spin_lock_irqsave(&udev->cmdr_lock, flags);

	mb = udev->mb_addr;
	tcmu_flush_dcache_range(mb, sizeof(*mb));

	while (udev->cmdr_last_cleaned != ACCESS_ONCE(mb->cmd_tail)) {

		struct tcmu_cmd_entry *entry = (void *) mb + CMDR_OFF + udev->cmdr_last_cleaned;
		struct tcmu_cmd *cmd;

		tcmu_flush_dcache_range(entry, sizeof(*entry));

		if (tcmu_hdr_get_op(entry->hdr.len_op) == TCMU_OP_PAD) {
			UPDATE_HEAD(udev->cmdr_last_cleaned,
				    tcmu_hdr_get_len(entry->hdr.len_op),
				    udev->cmdr_size);
			continue;
		}
		WARN_ON(tcmu_hdr_get_op(entry->hdr.len_op) != TCMU_OP_CMD);

		spin_lock(&udev->commands_lock);
		cmd = idr_find(&udev->commands, entry->hdr.cmd_id);
		if (cmd)
			idr_remove(&udev->commands, cmd->cmd_id);
		spin_unlock(&udev->commands_lock);

		if (!cmd) {
			pr_err("cmd_id not found, ring is broken\n");
			set_bit(TCMU_DEV_BIT_BROKEN, &udev->flags);
			break;
		}

		tcmu_handle_completion(cmd, entry);

		UPDATE_HEAD(udev->cmdr_last_cleaned,
			    tcmu_hdr_get_len(entry->hdr.len_op),
			    udev->cmdr_size);

		handled++;
	}

	if (mb->cmd_tail == mb->cmd_head)
		del_timer(&udev->timeout); /* no more pending cmds */

	spin_unlock_irqrestore(&udev->cmdr_lock, flags);

	wake_up(&udev->wait_cmdr);

	return handled;
}

static int tcmu_check_expired_cmd(int id, void *p, void *data)
{
	struct tcmu_cmd *cmd = p;

	if (test_bit(TCMU_CMD_BIT_EXPIRED, &cmd->flags))
		return 0;

	if (!time_after(jiffies, cmd->deadline))
		return 0;

	set_bit(TCMU_CMD_BIT_EXPIRED, &cmd->flags);
	target_complete_cmd(cmd->se_cmd, SAM_STAT_CHECK_CONDITION);
	cmd->se_cmd = NULL;

	kmem_cache_free(tcmu_cmd_cache, cmd);

	return 0;
}

static void tcmu_device_timedout(unsigned long data)
{
	struct tcmu_dev *udev = (struct tcmu_dev *)data;
	unsigned long flags;
	int handled;

	handled = tcmu_handle_completions(udev);

	pr_warn("%d completions handled from timeout\n", handled);

	spin_lock_irqsave(&udev->commands_lock, flags);
	idr_for_each(&udev->commands, tcmu_check_expired_cmd, NULL);
	spin_unlock_irqrestore(&udev->commands_lock, flags);

	/*
	 * We don't need to wakeup threads on wait_cmdr since they have their
	 * own timeout.
	 */
}

static int tcmu_attach_hba(struct se_hba *hba, u32 host_id)
{
	struct tcmu_hba *tcmu_hba;

	tcmu_hba = kzalloc(sizeof(struct tcmu_hba), GFP_KERNEL);
	if (!tcmu_hba)
		return -ENOMEM;

	tcmu_hba->host_id = host_id;
	hba->hba_ptr = tcmu_hba;

	return 0;
}

static void tcmu_detach_hba(struct se_hba *hba)
{
	kfree(hba->hba_ptr);
	hba->hba_ptr = NULL;
}

static struct se_device *tcmu_alloc_device(struct se_hba *hba, const char *name)
{
	struct tcmu_dev *udev;

	udev = kzalloc(sizeof(struct tcmu_dev), GFP_KERNEL);
	if (!udev)
		return NULL;

	udev->name = kstrdup(name, GFP_KERNEL);
	if (!udev->name) {
		kfree(udev);
		return NULL;
	}

	udev->hba = hba;

	init_waitqueue_head(&udev->wait_cmdr);
	spin_lock_init(&udev->cmdr_lock);

	idr_init(&udev->commands);
	spin_lock_init(&udev->commands_lock);

	setup_timer(&udev->timeout, tcmu_device_timedout,
		(unsigned long)udev);

	return &udev->se_dev;
}

static int tcmu_irqcontrol(struct uio_info *info, s32 irq_on)
{
	struct tcmu_dev *tcmu_dev = container_of(info, struct tcmu_dev, uio_info);

	tcmu_handle_completions(tcmu_dev);

	return 0;
}

/*
 * mmap code from uio.c. Copied here because we want to hook mmap()
 * and this stuff must come along.
 */
static int tcmu_find_mem_index(struct vm_area_struct *vma)
{
	struct tcmu_dev *udev = vma->vm_private_data;
	struct uio_info *info = &udev->uio_info;

	if (vma->vm_pgoff < MAX_UIO_MAPS) {
		if (info->mem[vma->vm_pgoff].size == 0)
			return -1;
		return (int)vma->vm_pgoff;
	}
	return -1;
}

static int tcmu_vma_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct tcmu_dev *udev = vma->vm_private_data;
	struct uio_info *info = &udev->uio_info;
	struct page *page;
	unsigned long offset;
	void *addr;

	int mi = tcmu_find_mem_index(vma);
	if (mi < 0)
		return VM_FAULT_SIGBUS;

	/*
	 * We need to subtract mi because userspace uses offset = N*PAGE_SIZE
	 * to use mem[N].
	 */
	offset = (vmf->pgoff - mi) << PAGE_SHIFT;

	addr = (void *)(unsigned long)info->mem[mi].addr + offset;
	if (info->mem[mi].memtype == UIO_MEM_LOGICAL)
		page = virt_to_page(addr);
	else
		page = vmalloc_to_page(addr);
	get_page(page);
	vmf->page = page;
	return 0;
}

static const struct vm_operations_struct tcmu_vm_ops = {
	.fault = tcmu_vma_fault,
};

static int tcmu_mmap(struct uio_info *info, struct vm_area_struct *vma)
{
	struct tcmu_dev *udev = container_of(info, struct tcmu_dev, uio_info);

	vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
	vma->vm_ops = &tcmu_vm_ops;

	vma->vm_private_data = udev;

	/* Ensure the mmap is exactly the right size */
	if (vma_pages(vma) != (TCMU_RING_SIZE >> PAGE_SHIFT))
		return -EINVAL;

	return 0;
}

static int tcmu_open(struct uio_info *info, struct inode *inode)
{
	struct tcmu_dev *udev = container_of(info, struct tcmu_dev, uio_info);

	/* O_EXCL not supported for char devs, so fake it? */
	if (test_and_set_bit(TCMU_DEV_BIT_OPEN, &udev->flags))
		return -EBUSY;

	pr_debug("open\n");

	return 0;
}

static int tcmu_release(struct uio_info *info, struct inode *inode)
{
	struct tcmu_dev *udev = container_of(info, struct tcmu_dev, uio_info);

	clear_bit(TCMU_DEV_BIT_OPEN, &udev->flags);

	pr_debug("close\n");

	return 0;
}

static int tcmu_netlink_event(enum tcmu_genl_cmd cmd, const char *name, int minor)
{
	struct sk_buff *skb;
	void *msg_header;
	int ret = -ENOMEM;

	skb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return ret;

	msg_header = genlmsg_put(skb, 0, 0, &tcmu_genl_family, 0, cmd);
	if (!msg_header)
		goto free_skb;

	ret = nla_put_string(skb, TCMU_ATTR_DEVICE, name);
	if (ret < 0)
		goto free_skb;

	ret = nla_put_u32(skb, TCMU_ATTR_MINOR, minor);
	if (ret < 0)
		goto free_skb;

	genlmsg_end(skb, msg_header);

	ret = genlmsg_multicast_allns(&tcmu_genl_family, skb, 0,
				TCMU_MCGRP_CONFIG, GFP_KERNEL);

	/* We don't care if no one is listening */
	if (ret == -ESRCH)
		ret = 0;

	return ret;
free_skb:
	nlmsg_free(skb);
	return ret;
}

static int tcmu_configure_device(struct se_device *dev)
{
	struct tcmu_dev *udev = TCMU_DEV(dev);
	struct tcmu_hba *hba = udev->hba->hba_ptr;
	struct uio_info *info;
	struct tcmu_mailbox *mb;
	size_t size;
	size_t used;
	int ret = 0;
	char *str;

	info = &udev->uio_info;

	size = snprintf(NULL, 0, "tcm-user/%u/%s/%s", hba->host_id, udev->name,
			udev->dev_config);
	size += 1; /* for \0 */
	str = kmalloc(size, GFP_KERNEL);
	if (!str)
		return -ENOMEM;

	used = snprintf(str, size, "tcm-user/%u/%s", hba->host_id, udev->name);

	if (udev->dev_config[0])
		snprintf(str + used, size - used, "/%s", udev->dev_config);

	info->name = str;

	udev->mb_addr = vzalloc(TCMU_RING_SIZE);
	if (!udev->mb_addr) {
		ret = -ENOMEM;
		goto err_vzalloc;
	}

	/* mailbox fits in first part of CMDR space */
	udev->cmdr_size = CMDR_SIZE - CMDR_OFF;
	udev->data_off = CMDR_SIZE;
	udev->data_size = TCMU_RING_SIZE - CMDR_SIZE;

	mb = udev->mb_addr;
	mb->version = TCMU_MAILBOX_VERSION;
	mb->flags = TCMU_MAILBOX_FLAG_CAP_OOOC;
	mb->cmdr_off = CMDR_OFF;
	mb->cmdr_size = udev->cmdr_size;

	WARN_ON(!PAGE_ALIGNED(udev->data_off));
	WARN_ON(udev->data_size % PAGE_SIZE);
	WARN_ON(udev->data_size % DATA_BLOCK_SIZE);

	info->version = __stringify(TCMU_MAILBOX_VERSION);

	info->mem[0].name = "tcm-user command & data buffer";
	info->mem[0].addr = (phys_addr_t)(uintptr_t)udev->mb_addr;
	info->mem[0].size = TCMU_RING_SIZE;
	info->mem[0].memtype = UIO_MEM_VIRTUAL;

	info->irqcontrol = tcmu_irqcontrol;
	info->irq = UIO_IRQ_CUSTOM;

	info->mmap = tcmu_mmap;
	info->open = tcmu_open;
	info->release = tcmu_release;

	ret = uio_register_device(tcmu_root_device, info);
	if (ret)
		goto err_register;

	/* User can set hw_block_size before enable the device */
	if (dev->dev_attrib.hw_block_size == 0)
		dev->dev_attrib.hw_block_size = 512;
	/* Other attributes can be configured in userspace */
	dev->dev_attrib.hw_max_sectors = 128;
	dev->dev_attrib.hw_queue_depth = 128;

	ret = tcmu_netlink_event(TCMU_CMD_ADDED_DEVICE, udev->uio_info.name,
				 udev->uio_info.uio_dev->minor);
	if (ret)
		goto err_netlink;

	return 0;

err_netlink:
	uio_unregister_device(&udev->uio_info);
err_register:
	vfree(udev->mb_addr);
err_vzalloc:
	kfree(info->name);

	return ret;
}

static int tcmu_check_and_free_pending_cmd(struct tcmu_cmd *cmd)
{
	if (test_bit(TCMU_CMD_BIT_EXPIRED, &cmd->flags)) {
		kmem_cache_free(tcmu_cmd_cache, cmd);
		return 0;
	}
	return -EINVAL;
}

static void tcmu_dev_call_rcu(struct rcu_head *p)
{
	struct se_device *dev = container_of(p, struct se_device, rcu_head);
	struct tcmu_dev *udev = TCMU_DEV(dev);

	kfree(udev);
}

static void tcmu_free_device(struct se_device *dev)
{
	struct tcmu_dev *udev = TCMU_DEV(dev);
	struct tcmu_cmd *cmd;
	bool all_expired = true;
	int i;

	del_timer_sync(&udev->timeout);

	vfree(udev->mb_addr);

	/* Upper layer should drain all requests before calling this */
	spin_lock_irq(&udev->commands_lock);
	idr_for_each_entry(&udev->commands, cmd, i) {
		if (tcmu_check_and_free_pending_cmd(cmd) != 0)
			all_expired = false;
	}
	idr_destroy(&udev->commands);
	spin_unlock_irq(&udev->commands_lock);
	WARN_ON(!all_expired);

	/* Device was configured */
	if (udev->uio_info.uio_dev) {
		tcmu_netlink_event(TCMU_CMD_REMOVED_DEVICE, udev->uio_info.name,
				   udev->uio_info.uio_dev->minor);

		uio_unregister_device(&udev->uio_info);
		kfree(udev->uio_info.name);
		kfree(udev->name);
	}
	call_rcu(&dev->rcu_head, tcmu_dev_call_rcu);
}

enum {
	Opt_dev_config, Opt_dev_size, Opt_hw_block_size, Opt_err,
};

static match_table_t tokens = {
	{Opt_dev_config, "dev_config=%s"},
	{Opt_dev_size, "dev_size=%u"},
	{Opt_hw_block_size, "hw_block_size=%u"},
	{Opt_err, NULL}
};

static ssize_t tcmu_set_configfs_dev_params(struct se_device *dev,
		const char *page, ssize_t count)
{
	struct tcmu_dev *udev = TCMU_DEV(dev);
	char *orig, *ptr, *opts, *arg_p;
	substring_t args[MAX_OPT_ARGS];
	int ret = 0, token;
	unsigned long tmp_ul;

	opts = kstrdup(page, GFP_KERNEL);
	if (!opts)
		return -ENOMEM;

	orig = opts;

	while ((ptr = strsep(&opts, ",\n")) != NULL) {
		if (!*ptr)
			continue;

		token = match_token(ptr, tokens, args);
		switch (token) {
		case Opt_dev_config:
			if (match_strlcpy(udev->dev_config, &args[0],
					  TCMU_CONFIG_LEN) == 0) {
				ret = -EINVAL;
				break;
			}
			pr_debug("TCMU: Referencing Path: %s\n", udev->dev_config);
			break;
		case Opt_dev_size:
			arg_p = match_strdup(&args[0]);
			if (!arg_p) {
				ret = -ENOMEM;
				break;
			}
			ret = kstrtoul(arg_p, 0, (unsigned long *) &udev->dev_size);
			kfree(arg_p);
			if (ret < 0)
				pr_err("kstrtoul() failed for dev_size=\n");
			break;
		case Opt_hw_block_size:
			arg_p = match_strdup(&args[0]);
			if (!arg_p) {
				ret = -ENOMEM;
				break;
			}
			ret = kstrtoul(arg_p, 0, &tmp_ul);
			kfree(arg_p);
			if (ret < 0) {
				pr_err("kstrtoul() failed for hw_block_size=\n");
				break;
			}
			if (!tmp_ul) {
				pr_err("hw_block_size must be nonzero\n");
				break;
			}
			dev->dev_attrib.hw_block_size = tmp_ul;
			break;
		default:
			break;
		}
	}

	kfree(orig);
	return (!ret) ? count : ret;
}

static ssize_t tcmu_show_configfs_dev_params(struct se_device *dev, char *b)
{
	struct tcmu_dev *udev = TCMU_DEV(dev);
	ssize_t bl = 0;

	bl = sprintf(b + bl, "Config: %s ",
		     udev->dev_config[0] ? udev->dev_config : "NULL");
	bl += sprintf(b + bl, "Size: %zu\n", udev->dev_size);

	return bl;
}

static sector_t tcmu_get_blocks(struct se_device *dev)
{
	struct tcmu_dev *udev = TCMU_DEV(dev);

	return div_u64(udev->dev_size - dev->dev_attrib.block_size,
		       dev->dev_attrib.block_size);
}

static sense_reason_t
tcmu_parse_cdb(struct se_cmd *cmd)
{
	return passthrough_parse_cdb(cmd, tcmu_queue_cmd);
}

static const struct target_backend_ops tcmu_ops = {
	.name			= "user",
	.owner			= THIS_MODULE,
	.transport_flags	= TRANSPORT_FLAG_PASSTHROUGH,
	.attach_hba		= tcmu_attach_hba,
	.detach_hba		= tcmu_detach_hba,
	.alloc_device		= tcmu_alloc_device,
	.configure_device	= tcmu_configure_device,
	.free_device		= tcmu_free_device,
	.parse_cdb		= tcmu_parse_cdb,
	.set_configfs_dev_params = tcmu_set_configfs_dev_params,
	.show_configfs_dev_params = tcmu_show_configfs_dev_params,
	.get_device_type	= sbc_get_device_type,
	.get_blocks		= tcmu_get_blocks,
	.tb_dev_attrib_attrs	= passthrough_attrib_attrs,
};

static int __init tcmu_module_init(void)
{
	int ret;

	BUILD_BUG_ON((sizeof(struct tcmu_cmd_entry) % TCMU_OP_ALIGN_SIZE) != 0);

	tcmu_cmd_cache = kmem_cache_create("tcmu_cmd_cache",
				sizeof(struct tcmu_cmd),
				__alignof__(struct tcmu_cmd),
				0, NULL);
	if (!tcmu_cmd_cache)
		return -ENOMEM;

	tcmu_root_device = root_device_register("tcm_user");
	if (IS_ERR(tcmu_root_device)) {
		ret = PTR_ERR(tcmu_root_device);
		goto out_free_cache;
	}

	ret = genl_register_family(&tcmu_genl_family);
	if (ret < 0) {
		goto out_unreg_device;
	}

	ret = transport_backend_register(&tcmu_ops);
	if (ret)
		goto out_unreg_genl;

	return 0;

out_unreg_genl:
	genl_unregister_family(&tcmu_genl_family);
out_unreg_device:
	root_device_unregister(tcmu_root_device);
out_free_cache:
	kmem_cache_destroy(tcmu_cmd_cache);

	return ret;
}

static void __exit tcmu_module_exit(void)
{
	target_backend_unregister(&tcmu_ops);
	genl_unregister_family(&tcmu_genl_family);
	root_device_unregister(tcmu_root_device);
	kmem_cache_destroy(tcmu_cmd_cache);
}

MODULE_DESCRIPTION("TCM USER subsystem plugin");
MODULE_AUTHOR("Shaohua Li <shli@kernel.org>");
MODULE_AUTHOR("Andy Grover <agrover@redhat.com>");
MODULE_LICENSE("GPL");

module_init(tcmu_module_init);
module_exit(tcmu_module_exit);
