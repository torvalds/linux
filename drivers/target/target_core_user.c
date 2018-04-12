/*
 * Copyright (C) 2013 Shaohua Li <shli@kernel.org>
 * Copyright (C) 2014 Red Hat, Inc.
 * Copyright (C) 2015 Arrikto, Inc.
 * Copyright (C) 2017 Chinamobile, Inc.
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
#include <linux/radix-tree.h>
#include <linux/stringify.h>
#include <linux/bitops.h>
#include <linux/highmem.h>
#include <linux/configfs.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
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

/* For cmd area, the size is fixed 8MB */
#define CMDR_SIZE (8 * 1024 * 1024)

/*
 * For data area, the block size is PAGE_SIZE and
 * the total size is 256K * PAGE_SIZE.
 */
#define DATA_BLOCK_SIZE PAGE_SIZE
#define DATA_BLOCK_BITS (256 * 1024)
#define DATA_SIZE (DATA_BLOCK_BITS * DATA_BLOCK_SIZE)
#define DATA_BLOCK_INIT_BITS 128

/* The total size of the ring is 8M + 256K * PAGE_SIZE */
#define TCMU_RING_SIZE (CMDR_SIZE + DATA_SIZE)

/* Default maximum of the global data blocks(512K * PAGE_SIZE) */
#define TCMU_GLOBAL_MAX_BLOCKS (512 * 1024)

static u8 tcmu_kern_cmd_reply_supported;

static struct device *tcmu_root_device;

struct tcmu_hba {
	u32 host_id;
};

#define TCMU_CONFIG_LEN 256

struct tcmu_nl_cmd {
	/* wake up thread waiting for reply */
	struct completion complete;
	int cmd;
	int status;
};

struct tcmu_dev {
	struct list_head node;
	struct kref kref;
	struct se_device se_dev;

	char *name;
	struct se_hba *hba;

#define TCMU_DEV_BIT_OPEN 0
#define TCMU_DEV_BIT_BROKEN 1
	unsigned long flags;

	struct uio_info uio_info;

	struct inode *inode;

	struct tcmu_mailbox *mb_addr;
	size_t dev_size;
	u32 cmdr_size;
	u32 cmdr_last_cleaned;
	/* Offset of data area from start of mb */
	/* Must add data_off and mb_addr to get the address */
	size_t data_off;
	size_t data_size;

	wait_queue_head_t wait_cmdr;
	struct mutex cmdr_lock;

	bool waiting_global;
	uint32_t dbi_max;
	uint32_t dbi_thresh;
	DECLARE_BITMAP(data_bitmap, DATA_BLOCK_BITS);
	struct radix_tree_root data_blocks;

	struct idr commands;
	spinlock_t commands_lock;

	struct timer_list timeout;
	unsigned int cmd_time_out;

	spinlock_t nl_cmd_lock;
	struct tcmu_nl_cmd curr_nl_cmd;
	/* wake up threads waiting on curr_nl_cmd */
	wait_queue_head_t nl_cmd_wq;

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
	uint32_t dbi_cnt;
	uint32_t dbi_cur;
	uint32_t *dbi;

	unsigned long deadline;

#define TCMU_CMD_BIT_EXPIRED 0
	unsigned long flags;
};

static struct task_struct *unmap_thread;
static wait_queue_head_t unmap_wait;
static DEFINE_MUTEX(root_udev_mutex);
static LIST_HEAD(root_udev);

static atomic_t global_db_count = ATOMIC_INIT(0);

static struct kmem_cache *tcmu_cmd_cache;

/* multicast group */
enum tcmu_multicast_groups {
	TCMU_MCGRP_CONFIG,
};

static const struct genl_multicast_group tcmu_mcgrps[] = {
	[TCMU_MCGRP_CONFIG] = { .name = "config", },
};

static struct nla_policy tcmu_attr_policy[TCMU_ATTR_MAX+1] = {
	[TCMU_ATTR_DEVICE]	= { .type = NLA_STRING },
	[TCMU_ATTR_MINOR]	= { .type = NLA_U32 },
	[TCMU_ATTR_CMD_STATUS]	= { .type = NLA_S32 },
	[TCMU_ATTR_DEVICE_ID]	= { .type = NLA_U32 },
	[TCMU_ATTR_SUPP_KERN_CMD_REPLY] = { .type = NLA_U8 },
};

static int tcmu_genl_cmd_done(struct genl_info *info, int completed_cmd)
{
	struct se_device *dev;
	struct tcmu_dev *udev;
	struct tcmu_nl_cmd *nl_cmd;
	int dev_id, rc, ret = 0;
	bool is_removed = (completed_cmd == TCMU_CMD_REMOVED_DEVICE);

	if (!info->attrs[TCMU_ATTR_CMD_STATUS] ||
	    !info->attrs[TCMU_ATTR_DEVICE_ID]) {
		printk(KERN_ERR "TCMU_ATTR_CMD_STATUS or TCMU_ATTR_DEVICE_ID not set, doing nothing\n");
                return -EINVAL;
        }

	dev_id = nla_get_u32(info->attrs[TCMU_ATTR_DEVICE_ID]);
	rc = nla_get_s32(info->attrs[TCMU_ATTR_CMD_STATUS]);

	dev = target_find_device(dev_id, !is_removed);
	if (!dev) {
		printk(KERN_ERR "tcmu nl cmd %u/%u completion could not find device with dev id %u.\n",
		       completed_cmd, rc, dev_id);
		return -ENODEV;
	}
	udev = TCMU_DEV(dev);

	spin_lock(&udev->nl_cmd_lock);
	nl_cmd = &udev->curr_nl_cmd;

	pr_debug("genl cmd done got id %d curr %d done %d rc %d\n", dev_id,
		 nl_cmd->cmd, completed_cmd, rc);

	if (nl_cmd->cmd != completed_cmd) {
		printk(KERN_ERR "Mismatched commands (Expecting reply for %d. Current %d).\n",
		       completed_cmd, nl_cmd->cmd);
		ret = -EINVAL;
	} else {
		nl_cmd->status = rc;
	}

	spin_unlock(&udev->nl_cmd_lock);
	if (!is_removed)
		 target_undepend_item(&dev->dev_group.cg_item);
	if (!ret)
		complete(&nl_cmd->complete);
	return ret;
}

static int tcmu_genl_rm_dev_done(struct sk_buff *skb, struct genl_info *info)
{
	return tcmu_genl_cmd_done(info, TCMU_CMD_REMOVED_DEVICE);
}

static int tcmu_genl_add_dev_done(struct sk_buff *skb, struct genl_info *info)
{
	return tcmu_genl_cmd_done(info, TCMU_CMD_ADDED_DEVICE);
}

static int tcmu_genl_reconfig_dev_done(struct sk_buff *skb,
				       struct genl_info *info)
{
	return tcmu_genl_cmd_done(info, TCMU_CMD_RECONFIG_DEVICE);
}

static int tcmu_genl_set_features(struct sk_buff *skb, struct genl_info *info)
{
	if (info->attrs[TCMU_ATTR_SUPP_KERN_CMD_REPLY]) {
		tcmu_kern_cmd_reply_supported  =
			nla_get_u8(info->attrs[TCMU_ATTR_SUPP_KERN_CMD_REPLY]);
		printk(KERN_INFO "tcmu daemon: command reply support %u.\n",
		       tcmu_kern_cmd_reply_supported);
	}

	return 0;
}

static const struct genl_ops tcmu_genl_ops[] = {
	{
		.cmd	= TCMU_CMD_SET_FEATURES,
		.flags	= GENL_ADMIN_PERM,
		.policy	= tcmu_attr_policy,
		.doit	= tcmu_genl_set_features,
	},
	{
		.cmd	= TCMU_CMD_ADDED_DEVICE_DONE,
		.flags	= GENL_ADMIN_PERM,
		.policy	= tcmu_attr_policy,
		.doit	= tcmu_genl_add_dev_done,
	},
	{
		.cmd	= TCMU_CMD_REMOVED_DEVICE_DONE,
		.flags	= GENL_ADMIN_PERM,
		.policy	= tcmu_attr_policy,
		.doit	= tcmu_genl_rm_dev_done,
	},
	{
		.cmd	= TCMU_CMD_RECONFIG_DEVICE_DONE,
		.flags	= GENL_ADMIN_PERM,
		.policy	= tcmu_attr_policy,
		.doit	= tcmu_genl_reconfig_dev_done,
	},
};

/* Our generic netlink family */
static struct genl_family tcmu_genl_family __ro_after_init = {
	.module = THIS_MODULE,
	.hdrsize = 0,
	.name = "TCM-USER",
	.version = 2,
	.maxattr = TCMU_ATTR_MAX,
	.mcgrps = tcmu_mcgrps,
	.n_mcgrps = ARRAY_SIZE(tcmu_mcgrps),
	.netnsok = true,
	.ops = tcmu_genl_ops,
	.n_ops = ARRAY_SIZE(tcmu_genl_ops),
};

#define tcmu_cmd_set_dbi_cur(cmd, index) ((cmd)->dbi_cur = (index))
#define tcmu_cmd_reset_dbi_cur(cmd) tcmu_cmd_set_dbi_cur(cmd, 0)
#define tcmu_cmd_set_dbi(cmd, index) ((cmd)->dbi[(cmd)->dbi_cur++] = (index))
#define tcmu_cmd_get_dbi(cmd) ((cmd)->dbi[(cmd)->dbi_cur++])

static void tcmu_cmd_free_data(struct tcmu_cmd *tcmu_cmd, uint32_t len)
{
	struct tcmu_dev *udev = tcmu_cmd->tcmu_dev;
	uint32_t i;

	for (i = 0; i < len; i++)
		clear_bit(tcmu_cmd->dbi[i], udev->data_bitmap);
}

static inline bool tcmu_get_empty_block(struct tcmu_dev *udev,
					struct tcmu_cmd *tcmu_cmd)
{
	struct page *page;
	int ret, dbi;

	dbi = find_first_zero_bit(udev->data_bitmap, udev->dbi_thresh);
	if (dbi == udev->dbi_thresh)
		return false;

	page = radix_tree_lookup(&udev->data_blocks, dbi);
	if (!page) {
		if (atomic_add_return(1, &global_db_count) >
					TCMU_GLOBAL_MAX_BLOCKS) {
			atomic_dec(&global_db_count);
			return false;
		}

		/* try to get new page from the mm */
		page = alloc_page(GFP_KERNEL);
		if (!page)
			goto err_alloc;

		ret = radix_tree_insert(&udev->data_blocks, dbi, page);
		if (ret)
			goto err_insert;
	}

	if (dbi > udev->dbi_max)
		udev->dbi_max = dbi;

	set_bit(dbi, udev->data_bitmap);
	tcmu_cmd_set_dbi(tcmu_cmd, dbi);

	return true;
err_insert:
	__free_page(page);
err_alloc:
	atomic_dec(&global_db_count);
	return false;
}

static bool tcmu_get_empty_blocks(struct tcmu_dev *udev,
				  struct tcmu_cmd *tcmu_cmd)
{
	int i;

	udev->waiting_global = false;

	for (i = tcmu_cmd->dbi_cur; i < tcmu_cmd->dbi_cnt; i++) {
		if (!tcmu_get_empty_block(udev, tcmu_cmd))
			goto err;
	}
	return true;

err:
	udev->waiting_global = true;
	/* Try to wake up the unmap thread */
	wake_up(&unmap_wait);
	return false;
}

static inline struct page *
tcmu_get_block_page(struct tcmu_dev *udev, uint32_t dbi)
{
	return radix_tree_lookup(&udev->data_blocks, dbi);
}

static inline void tcmu_free_cmd(struct tcmu_cmd *tcmu_cmd)
{
	kfree(tcmu_cmd->dbi);
	kmem_cache_free(tcmu_cmd_cache, tcmu_cmd);
}

static inline size_t tcmu_cmd_get_data_length(struct tcmu_cmd *tcmu_cmd)
{
	struct se_cmd *se_cmd = tcmu_cmd->se_cmd;
	size_t data_length = round_up(se_cmd->data_length, DATA_BLOCK_SIZE);

	if (se_cmd->se_cmd_flags & SCF_BIDI) {
		BUG_ON(!(se_cmd->t_bidi_data_sg && se_cmd->t_bidi_data_nents));
		data_length += round_up(se_cmd->t_bidi_data_sg->length,
				DATA_BLOCK_SIZE);
	}

	return data_length;
}

static inline uint32_t tcmu_cmd_get_block_cnt(struct tcmu_cmd *tcmu_cmd)
{
	size_t data_length = tcmu_cmd_get_data_length(tcmu_cmd);

	return data_length / DATA_BLOCK_SIZE;
}

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
	if (udev->cmd_time_out)
		tcmu_cmd->deadline = jiffies +
					msecs_to_jiffies(udev->cmd_time_out);

	tcmu_cmd_reset_dbi_cur(tcmu_cmd);
	tcmu_cmd->dbi_cnt = tcmu_cmd_get_block_cnt(tcmu_cmd);
	tcmu_cmd->dbi = kcalloc(tcmu_cmd->dbi_cnt, sizeof(uint32_t),
				GFP_KERNEL);
	if (!tcmu_cmd->dbi) {
		kmem_cache_free(tcmu_cmd_cache, tcmu_cmd);
		return NULL;
	}

	idr_preload(GFP_KERNEL);
	spin_lock_irq(&udev->commands_lock);
	cmd_id = idr_alloc(&udev->commands, tcmu_cmd, 0,
		USHRT_MAX, GFP_NOWAIT);
	spin_unlock_irq(&udev->commands_lock);
	idr_preload_end();

	if (cmd_id < 0) {
		tcmu_free_cmd(tcmu_cmd);
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
static inline size_t get_block_offset_user(struct tcmu_dev *dev,
		int dbi, int remaining)
{
	return dev->data_off + dbi * DATA_BLOCK_SIZE +
		DATA_BLOCK_SIZE - remaining;
}

static inline size_t iov_tail(struct iovec *iov)
{
	return (size_t)iov->iov_base + iov->iov_len;
}

static int scatter_data_area(struct tcmu_dev *udev,
	struct tcmu_cmd *tcmu_cmd, struct scatterlist *data_sg,
	unsigned int data_nents, struct iovec **iov,
	int *iov_cnt, bool copy_data)
{
	int i, dbi;
	int block_remaining = 0;
	void *from, *to = NULL;
	size_t copy_bytes, to_offset, offset;
	struct scatterlist *sg;
	struct page *page;

	for_each_sg(data_sg, sg, data_nents, i) {
		int sg_remaining = sg->length;
		from = kmap_atomic(sg_page(sg)) + sg->offset;
		while (sg_remaining > 0) {
			if (block_remaining == 0) {
				if (to)
					kunmap_atomic(to);

				block_remaining = DATA_BLOCK_SIZE;
				dbi = tcmu_cmd_get_dbi(tcmu_cmd);
				page = tcmu_get_block_page(udev, dbi);
				to = kmap_atomic(page);
			}

			copy_bytes = min_t(size_t, sg_remaining,
					block_remaining);
			to_offset = get_block_offset_user(udev, dbi,
					block_remaining);

			if (*iov_cnt != 0 &&
			    to_offset == iov_tail(*iov)) {
				(*iov)->iov_len += copy_bytes;
			} else {
				new_iov(iov, iov_cnt, udev);
				(*iov)->iov_base = (void __user *)to_offset;
				(*iov)->iov_len = copy_bytes;
			}
			if (copy_data) {
				offset = DATA_BLOCK_SIZE - block_remaining;
				memcpy(to + offset,
				       from + sg->length - sg_remaining,
				       copy_bytes);
				tcmu_flush_dcache_range(to, copy_bytes);
			}
			sg_remaining -= copy_bytes;
			block_remaining -= copy_bytes;
		}
		kunmap_atomic(from - sg->offset);
	}
	if (to)
		kunmap_atomic(to);

	return 0;
}

static void gather_data_area(struct tcmu_dev *udev, struct tcmu_cmd *cmd,
			     bool bidi)
{
	struct se_cmd *se_cmd = cmd->se_cmd;
	int i, dbi;
	int block_remaining = 0;
	void *from = NULL, *to;
	size_t copy_bytes, offset;
	struct scatterlist *sg, *data_sg;
	struct page *page;
	unsigned int data_nents;
	uint32_t count = 0;

	if (!bidi) {
		data_sg = se_cmd->t_data_sg;
		data_nents = se_cmd->t_data_nents;
	} else {

		/*
		 * For bidi case, the first count blocks are for Data-Out
		 * buffer blocks, and before gathering the Data-In buffer
		 * the Data-Out buffer blocks should be discarded.
		 */
		count = DIV_ROUND_UP(se_cmd->data_length, DATA_BLOCK_SIZE);

		data_sg = se_cmd->t_bidi_data_sg;
		data_nents = se_cmd->t_bidi_data_nents;
	}

	tcmu_cmd_set_dbi_cur(cmd, count);

	for_each_sg(data_sg, sg, data_nents, i) {
		int sg_remaining = sg->length;
		to = kmap_atomic(sg_page(sg)) + sg->offset;
		while (sg_remaining > 0) {
			if (block_remaining == 0) {
				if (from)
					kunmap_atomic(from);

				block_remaining = DATA_BLOCK_SIZE;
				dbi = tcmu_cmd_get_dbi(cmd);
				page = tcmu_get_block_page(udev, dbi);
				from = kmap_atomic(page);
			}
			copy_bytes = min_t(size_t, sg_remaining,
					block_remaining);
			offset = DATA_BLOCK_SIZE - block_remaining;
			tcmu_flush_dcache_range(from, copy_bytes);
			memcpy(to + sg->length - sg_remaining, from + offset,
					copy_bytes);

			sg_remaining -= copy_bytes;
			block_remaining -= copy_bytes;
		}
		kunmap_atomic(to - sg->offset);
	}
	if (from)
		kunmap_atomic(from);
}

static inline size_t spc_bitmap_free(unsigned long *bitmap, uint32_t thresh)
{
	return DATA_BLOCK_SIZE * (thresh - bitmap_weight(bitmap, thresh));
}

/*
 * We can't queue a command until we have space available on the cmd ring *and*
 * space available on the data area.
 *
 * Called with ring lock held.
 */
static bool is_ring_space_avail(struct tcmu_dev *udev, struct tcmu_cmd *cmd,
		size_t cmd_size, size_t data_needed)
{
	struct tcmu_mailbox *mb = udev->mb_addr;
	uint32_t blocks_needed = (data_needed + DATA_BLOCK_SIZE - 1)
				/ DATA_BLOCK_SIZE;
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

	/* try to check and get the data blocks as needed */
	space = spc_bitmap_free(udev->data_bitmap, udev->dbi_thresh);
	if (space < data_needed) {
		unsigned long blocks_left = DATA_BLOCK_BITS - udev->dbi_thresh;
		unsigned long grow;

		if (blocks_left < blocks_needed) {
			pr_debug("no data space: only %lu available, but ask for %zu\n",
					blocks_left * DATA_BLOCK_SIZE,
					data_needed);
			return false;
		}

		/* Try to expand the thresh */
		if (!udev->dbi_thresh) {
			/* From idle state */
			uint32_t init_thresh = DATA_BLOCK_INIT_BITS;

			udev->dbi_thresh = max(blocks_needed, init_thresh);
		} else {
			/*
			 * Grow the data area by max(blocks needed,
			 * dbi_thresh / 2), but limited to the max
			 * DATA_BLOCK_BITS size.
			 */
			grow = max(blocks_needed, udev->dbi_thresh / 2);
			udev->dbi_thresh += grow;
			if (udev->dbi_thresh > DATA_BLOCK_BITS)
				udev->dbi_thresh = DATA_BLOCK_BITS;
		}
	}

	return tcmu_get_empty_blocks(udev, cmd);
}

static inline size_t tcmu_cmd_get_base_cmd_size(size_t iov_cnt)
{
	return max(offsetof(struct tcmu_cmd_entry, req.iov[iov_cnt]),
			sizeof(struct tcmu_cmd_entry));
}

static inline size_t tcmu_cmd_get_cmd_size(struct tcmu_cmd *tcmu_cmd,
					   size_t base_command_size)
{
	struct se_cmd *se_cmd = tcmu_cmd->se_cmd;
	size_t command_size;

	command_size = base_command_size +
		round_up(scsi_command_size(se_cmd->t_task_cdb),
				TCMU_OP_ALIGN_SIZE);

	WARN_ON(command_size & (TCMU_OP_ALIGN_SIZE-1));

	return command_size;
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
	int iov_cnt, ret;
	uint32_t cmd_head;
	uint64_t cdb_off;
	bool copy_to_data_area;
	size_t data_length = tcmu_cmd_get_data_length(tcmu_cmd);

	if (test_bit(TCMU_DEV_BIT_BROKEN, &udev->flags))
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	/*
	 * Must be a certain minimum size for response sense info, but
	 * also may be larger if the iov array is large.
	 *
	 * We prepare as many iovs as possbile for potential uses here,
	 * because it's expensive to tell how many regions are freed in
	 * the bitmap & global data pool, as the size calculated here
	 * will only be used to do the checks.
	 *
	 * The size will be recalculated later as actually needed to save
	 * cmd area memories.
	 */
	base_command_size = tcmu_cmd_get_base_cmd_size(tcmu_cmd->dbi_cnt);
	command_size = tcmu_cmd_get_cmd_size(tcmu_cmd, base_command_size);

	mutex_lock(&udev->cmdr_lock);

	mb = udev->mb_addr;
	cmd_head = mb->cmd_head % udev->cmdr_size; /* UAM */
	if ((command_size > (udev->cmdr_size / 2)) ||
	    data_length > udev->data_size) {
		pr_warn("TCMU: Request of size %zu/%zu is too big for %u/%zu "
			"cmd ring/data area\n", command_size, data_length,
			udev->cmdr_size, udev->data_size);
		mutex_unlock(&udev->cmdr_lock);
		return TCM_INVALID_CDB_FIELD;
	}

	while (!is_ring_space_avail(udev, tcmu_cmd, command_size, data_length)) {
		int ret;
		DEFINE_WAIT(__wait);

		/*
		 * Don't leave commands partially setup because the unmap
		 * thread might need the blocks to make forward progress.
		 */
		tcmu_cmd_free_data(tcmu_cmd, tcmu_cmd->dbi_cur);
		tcmu_cmd_reset_dbi_cur(tcmu_cmd);

		prepare_to_wait(&udev->wait_cmdr, &__wait, TASK_INTERRUPTIBLE);

		pr_debug("sleeping for ring space\n");
		mutex_unlock(&udev->cmdr_lock);
		if (udev->cmd_time_out)
			ret = schedule_timeout(
					msecs_to_jiffies(udev->cmd_time_out));
		else
			ret = schedule_timeout(msecs_to_jiffies(TCMU_TIME_OUT));
		finish_wait(&udev->wait_cmdr, &__wait);
		if (!ret) {
			pr_warn("tcmu: command timed out\n");
			return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		}

		mutex_lock(&udev->cmdr_lock);

		/* We dropped cmdr_lock, cmd_head is stale */
		cmd_head = mb->cmd_head % udev->cmdr_size; /* UAM */
	}

	/* Insert a PAD if end-of-ring space is too small */
	if (head_to_end(cmd_head, udev->cmdr_size) < command_size) {
		size_t pad_size = head_to_end(cmd_head, udev->cmdr_size);

		entry = (void *) mb + CMDR_OFF + cmd_head;
		tcmu_hdr_set_op(&entry->hdr.len_op, TCMU_OP_PAD);
		tcmu_hdr_set_len(&entry->hdr.len_op, pad_size);
		entry->hdr.cmd_id = 0; /* not used for PAD */
		entry->hdr.kflags = 0;
		entry->hdr.uflags = 0;
		tcmu_flush_dcache_range(entry, sizeof(*entry));

		UPDATE_HEAD(mb->cmd_head, pad_size, udev->cmdr_size);
		tcmu_flush_dcache_range(mb, sizeof(*mb));

		cmd_head = mb->cmd_head % udev->cmdr_size; /* UAM */
		WARN_ON(cmd_head != 0);
	}

	entry = (void *) mb + CMDR_OFF + cmd_head;
	memset(entry, 0, command_size);
	tcmu_hdr_set_op(&entry->hdr.len_op, TCMU_OP_CMD);
	entry->hdr.cmd_id = tcmu_cmd->cmd_id;

	/* Handle allocating space from the data area */
	tcmu_cmd_reset_dbi_cur(tcmu_cmd);
	iov = &entry->req.iov[0];
	iov_cnt = 0;
	copy_to_data_area = (se_cmd->data_direction == DMA_TO_DEVICE
		|| se_cmd->se_cmd_flags & SCF_BIDI);
	ret = scatter_data_area(udev, tcmu_cmd, se_cmd->t_data_sg,
				se_cmd->t_data_nents, &iov, &iov_cnt,
				copy_to_data_area);
	if (ret) {
		tcmu_cmd_free_data(tcmu_cmd, tcmu_cmd->dbi_cnt);
		mutex_unlock(&udev->cmdr_lock);

		pr_err("tcmu: alloc and scatter data failed\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}
	entry->req.iov_cnt = iov_cnt;

	/* Handle BIDI commands */
	iov_cnt = 0;
	if (se_cmd->se_cmd_flags & SCF_BIDI) {
		iov++;
		ret = scatter_data_area(udev, tcmu_cmd,
					se_cmd->t_bidi_data_sg,
					se_cmd->t_bidi_data_nents,
					&iov, &iov_cnt, false);
		if (ret) {
			tcmu_cmd_free_data(tcmu_cmd, tcmu_cmd->dbi_cnt);
			mutex_unlock(&udev->cmdr_lock);

			pr_err("tcmu: alloc and scatter bidi data failed\n");
			return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		}
	}
	entry->req.iov_bidi_cnt = iov_cnt;

	/*
	 * Recalaulate the command's base size and size according
	 * to the actual needs
	 */
	base_command_size = tcmu_cmd_get_base_cmd_size(entry->req.iov_cnt +
						       entry->req.iov_bidi_cnt);
	command_size = tcmu_cmd_get_cmd_size(tcmu_cmd, base_command_size);

	tcmu_hdr_set_len(&entry->hdr.len_op, command_size);

	/* All offsets relative to mb_addr, not start of entry! */
	cdb_off = CMDR_OFF + cmd_head + base_command_size;
	memcpy((void *) mb + cdb_off, se_cmd->t_task_cdb, scsi_command_size(se_cmd->t_task_cdb));
	entry->req.cdb_off = cdb_off;
	tcmu_flush_dcache_range(entry, sizeof(*entry));

	UPDATE_HEAD(mb->cmd_head, command_size, udev->cmdr_size);
	tcmu_flush_dcache_range(mb, sizeof(*mb));
	mutex_unlock(&udev->cmdr_lock);

	/* TODO: only if FLUSH and FUA? */
	uio_event_notify(&udev->uio_info);

	if (udev->cmd_time_out)
		mod_timer(&udev->timeout, round_jiffies_up(jiffies +
			  msecs_to_jiffies(udev->cmd_time_out)));

	return TCM_NO_SENSE;
}

static sense_reason_t
tcmu_queue_cmd(struct se_cmd *se_cmd)
{
	struct se_device *se_dev = se_cmd->se_dev;
	struct tcmu_dev *udev = TCMU_DEV(se_dev);
	struct tcmu_cmd *tcmu_cmd;
	sense_reason_t ret;

	tcmu_cmd = tcmu_alloc_cmd(se_cmd);
	if (!tcmu_cmd)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	ret = tcmu_queue_cmd_ring(tcmu_cmd);
	if (ret != TCM_NO_SENSE) {
		pr_err("TCMU: Could not queue command\n");
		spin_lock_irq(&udev->commands_lock);
		idr_remove(&udev->commands, tcmu_cmd->cmd_id);
		spin_unlock_irq(&udev->commands_lock);

		tcmu_free_cmd(tcmu_cmd);
	}

	return ret;
}

static void tcmu_handle_completion(struct tcmu_cmd *cmd, struct tcmu_cmd_entry *entry)
{
	struct se_cmd *se_cmd = cmd->se_cmd;
	struct tcmu_dev *udev = cmd->tcmu_dev;

	/*
	 * cmd has been completed already from timeout, just reclaim
	 * data area space and free cmd
	 */
	if (test_bit(TCMU_CMD_BIT_EXPIRED, &cmd->flags))
		goto out;

	tcmu_cmd_reset_dbi_cur(cmd);

	if (entry->hdr.uflags & TCMU_UFLAG_UNKNOWN_OP) {
		pr_warn("TCMU: Userspace set UNKNOWN_OP flag on se_cmd %p\n",
			cmd->se_cmd);
		entry->rsp.scsi_status = SAM_STAT_CHECK_CONDITION;
	} else if (entry->rsp.scsi_status == SAM_STAT_CHECK_CONDITION) {
		transport_copy_sense_to_cmd(se_cmd, entry->rsp.sense_buffer);
	} else if (se_cmd->se_cmd_flags & SCF_BIDI) {
		/* Get Data-In buffer before clean up */
		gather_data_area(udev, cmd, true);
	} else if (se_cmd->data_direction == DMA_FROM_DEVICE) {
		gather_data_area(udev, cmd, false);
	} else if (se_cmd->data_direction == DMA_TO_DEVICE) {
		/* TODO: */
	} else if (se_cmd->data_direction != DMA_NONE) {
		pr_warn("TCMU: data direction was %d!\n",
			se_cmd->data_direction);
	}

	target_complete_cmd(cmd->se_cmd, entry->rsp.scsi_status);

out:
	cmd->se_cmd = NULL;
	tcmu_cmd_free_data(cmd, cmd->dbi_cnt);
	tcmu_free_cmd(cmd);
}

static unsigned int tcmu_handle_completions(struct tcmu_dev *udev)
{
	struct tcmu_mailbox *mb;
	int handled = 0;

	if (test_bit(TCMU_DEV_BIT_BROKEN, &udev->flags)) {
		pr_err("ring broken, not handling completions\n");
		return 0;
	}

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
		cmd = idr_remove(&udev->commands, entry->hdr.cmd_id);
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

	return 0;
}

static void tcmu_device_timedout(unsigned long data)
{
	struct tcmu_dev *udev = (struct tcmu_dev *)data;
	unsigned long flags;

	spin_lock_irqsave(&udev->commands_lock, flags);
	idr_for_each(&udev->commands, tcmu_check_expired_cmd, NULL);
	spin_unlock_irqrestore(&udev->commands_lock, flags);

	/* Try to wake up the ummap thread */
	wake_up(&unmap_wait);

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
	kref_init(&udev->kref);

	udev->name = kstrdup(name, GFP_KERNEL);
	if (!udev->name) {
		kfree(udev);
		return NULL;
	}

	udev->hba = hba;
	udev->cmd_time_out = TCMU_TIME_OUT;

	init_waitqueue_head(&udev->wait_cmdr);
	mutex_init(&udev->cmdr_lock);

	idr_init(&udev->commands);
	spin_lock_init(&udev->commands_lock);

	setup_timer(&udev->timeout, tcmu_device_timedout,
		(unsigned long)udev);

	init_waitqueue_head(&udev->nl_cmd_wq);
	spin_lock_init(&udev->nl_cmd_lock);

	return &udev->se_dev;
}

static int tcmu_irqcontrol(struct uio_info *info, s32 irq_on)
{
	struct tcmu_dev *tcmu_dev = container_of(info, struct tcmu_dev, uio_info);

	mutex_lock(&tcmu_dev->cmdr_lock);
	tcmu_handle_completions(tcmu_dev);
	mutex_unlock(&tcmu_dev->cmdr_lock);

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

static struct page *tcmu_try_get_block_page(struct tcmu_dev *udev, uint32_t dbi)
{
	struct page *page;
	int ret;

	mutex_lock(&udev->cmdr_lock);
	page = tcmu_get_block_page(udev, dbi);
	if (likely(page)) {
		mutex_unlock(&udev->cmdr_lock);
		return page;
	}

	/*
	 * Normally it shouldn't be here:
	 * Only when the userspace has touched the blocks which
	 * are out of the tcmu_cmd's data iov[], and will return
	 * one zeroed page.
	 */
	pr_warn("Block(%u) out of cmd's iov[] has been touched!\n", dbi);
	pr_warn("Mostly it will be a bug of userspace, please have a check!\n");

	if (dbi >= udev->dbi_thresh) {
		/* Extern the udev->dbi_thresh to dbi + 1 */
		udev->dbi_thresh = dbi + 1;
		udev->dbi_max = dbi;
	}

	page = radix_tree_lookup(&udev->data_blocks, dbi);
	if (!page) {
		page = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!page) {
			mutex_unlock(&udev->cmdr_lock);
			return NULL;
		}

		ret = radix_tree_insert(&udev->data_blocks, dbi, page);
		if (ret) {
			mutex_unlock(&udev->cmdr_lock);
			__free_page(page);
			return NULL;
		}

		/*
		 * Since this case is rare in page fault routine, here we
		 * will allow the global_db_count >= TCMU_GLOBAL_MAX_BLOCKS
		 * to reduce possible page fault call trace.
		 */
		atomic_inc(&global_db_count);
	}
	mutex_unlock(&udev->cmdr_lock);

	return page;
}

static int tcmu_vma_fault(struct vm_fault *vmf)
{
	struct tcmu_dev *udev = vmf->vma->vm_private_data;
	struct uio_info *info = &udev->uio_info;
	struct page *page;
	unsigned long offset;
	void *addr;

	int mi = tcmu_find_mem_index(vmf->vma);
	if (mi < 0)
		return VM_FAULT_SIGBUS;

	/*
	 * We need to subtract mi because userspace uses offset = N*PAGE_SIZE
	 * to use mem[N].
	 */
	offset = (vmf->pgoff - mi) << PAGE_SHIFT;

	if (offset < udev->data_off) {
		/* For the vmalloc()ed cmd area pages */
		addr = (void *)(unsigned long)info->mem[mi].addr + offset;
		page = vmalloc_to_page(addr);
	} else {
		uint32_t dbi;

		/* For the dynamically growing data area pages */
		dbi = (offset - udev->data_off) / DATA_BLOCK_SIZE;
		page = tcmu_try_get_block_page(udev, dbi);
		if (!page)
			return VM_FAULT_NOPAGE;
	}

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

	udev->inode = inode;
	kref_get(&udev->kref);

	pr_debug("open\n");

	return 0;
}

static void tcmu_dev_call_rcu(struct rcu_head *p)
{
	struct se_device *dev = container_of(p, struct se_device, rcu_head);
	struct tcmu_dev *udev = TCMU_DEV(dev);

	kfree(udev->uio_info.name);
	kfree(udev->name);
	kfree(udev);
}

static void tcmu_dev_kref_release(struct kref *kref)
{
	struct tcmu_dev *udev = container_of(kref, struct tcmu_dev, kref);
	struct se_device *dev = &udev->se_dev;

	call_rcu(&dev->rcu_head, tcmu_dev_call_rcu);
}

static int tcmu_release(struct uio_info *info, struct inode *inode)
{
	struct tcmu_dev *udev = container_of(info, struct tcmu_dev, uio_info);

	clear_bit(TCMU_DEV_BIT_OPEN, &udev->flags);

	pr_debug("close\n");
	/* release ref from open */
	kref_put(&udev->kref, tcmu_dev_kref_release);
	return 0;
}

static void tcmu_init_genl_cmd_reply(struct tcmu_dev *udev, int cmd)
{
	struct tcmu_nl_cmd *nl_cmd = &udev->curr_nl_cmd;

	if (!tcmu_kern_cmd_reply_supported)
		return;
relock:
	spin_lock(&udev->nl_cmd_lock);

	if (nl_cmd->cmd != TCMU_CMD_UNSPEC) {
		spin_unlock(&udev->nl_cmd_lock);
		pr_debug("sleeping for open nl cmd\n");
		wait_event(udev->nl_cmd_wq, (nl_cmd->cmd == TCMU_CMD_UNSPEC));
		goto relock;
	}

	memset(nl_cmd, 0, sizeof(*nl_cmd));
	nl_cmd->cmd = cmd;
	init_completion(&nl_cmd->complete);

	spin_unlock(&udev->nl_cmd_lock);
}

static int tcmu_wait_genl_cmd_reply(struct tcmu_dev *udev)
{
	struct tcmu_nl_cmd *nl_cmd = &udev->curr_nl_cmd;
	int ret;
	DEFINE_WAIT(__wait);

	if (!tcmu_kern_cmd_reply_supported)
		return 0;

	pr_debug("sleeping for nl reply\n");
	wait_for_completion(&nl_cmd->complete);

	spin_lock(&udev->nl_cmd_lock);
	nl_cmd->cmd = TCMU_CMD_UNSPEC;
	ret = nl_cmd->status;
	nl_cmd->status = 0;
	spin_unlock(&udev->nl_cmd_lock);

	wake_up_all(&udev->nl_cmd_wq);

	return ret;;
}

static int tcmu_netlink_event(struct tcmu_dev *udev, enum tcmu_genl_cmd cmd,
			      int reconfig_attr, const void *reconfig_data)
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

	ret = nla_put_string(skb, TCMU_ATTR_DEVICE, udev->uio_info.name);
	if (ret < 0)
		goto free_skb;

	ret = nla_put_u32(skb, TCMU_ATTR_MINOR, udev->uio_info.uio_dev->minor);
	if (ret < 0)
		goto free_skb;

	ret = nla_put_u32(skb, TCMU_ATTR_DEVICE_ID, udev->se_dev.dev_index);
	if (ret < 0)
		goto free_skb;

	if (cmd == TCMU_CMD_RECONFIG_DEVICE) {
		switch (reconfig_attr) {
		case TCMU_ATTR_DEV_CFG:
			ret = nla_put_string(skb, reconfig_attr, reconfig_data);
			break;
		case TCMU_ATTR_DEV_SIZE:
			ret = nla_put_u64_64bit(skb, reconfig_attr,
						*((u64 *)reconfig_data),
						TCMU_ATTR_PAD);
			break;
		case TCMU_ATTR_WRITECACHE:
			ret = nla_put_u8(skb, reconfig_attr,
					  *((u8 *)reconfig_data));
			break;
		default:
			BUG();
		}

		if (ret < 0)
			goto free_skb;
	}

	genlmsg_end(skb, msg_header);

	tcmu_init_genl_cmd_reply(udev, cmd);

	ret = genlmsg_multicast_allns(&tcmu_genl_family, skb, 0,
				TCMU_MCGRP_CONFIG, GFP_KERNEL);
	/* We don't care if no one is listening */
	if (ret == -ESRCH)
		ret = 0;
	if (!ret)
		ret = tcmu_wait_genl_cmd_reply(udev);

	return ret;
free_skb:
	nlmsg_free(skb);
	return ret;
}

static int tcmu_update_uio_info(struct tcmu_dev *udev)
{
	struct tcmu_hba *hba = udev->hba->hba_ptr;
	struct uio_info *info;
	size_t size, used;
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

	/* If the old string exists, free it */
	kfree(info->name);
	info->name = str;

	return 0;
}

static int tcmu_configure_device(struct se_device *dev)
{
	struct tcmu_dev *udev = TCMU_DEV(dev);
	struct uio_info *info;
	struct tcmu_mailbox *mb;
	int ret = 0;

	ret = tcmu_update_uio_info(udev);
	if (ret)
		return ret;

	info = &udev->uio_info;

	udev->mb_addr = vzalloc(CMDR_SIZE);
	if (!udev->mb_addr) {
		ret = -ENOMEM;
		goto err_vzalloc;
	}

	/* mailbox fits in first part of CMDR space */
	udev->cmdr_size = CMDR_SIZE - CMDR_OFF;
	udev->data_off = CMDR_SIZE;
	udev->data_size = DATA_SIZE;
	udev->dbi_thresh = 0; /* Default in Idle state */
	udev->waiting_global = false;

	/* Initialise the mailbox of the ring buffer */
	mb = udev->mb_addr;
	mb->version = TCMU_MAILBOX_VERSION;
	mb->flags = TCMU_MAILBOX_FLAG_CAP_OOOC;
	mb->cmdr_off = CMDR_OFF;
	mb->cmdr_size = udev->cmdr_size;

	WARN_ON(!PAGE_ALIGNED(udev->data_off));
	WARN_ON(udev->data_size % PAGE_SIZE);
	WARN_ON(udev->data_size % DATA_BLOCK_SIZE);

	INIT_RADIX_TREE(&udev->data_blocks, GFP_KERNEL);

	info->version = __stringify(TCMU_MAILBOX_VERSION);

	info->mem[0].name = "tcm-user command & data buffer";
	info->mem[0].addr = (phys_addr_t)(uintptr_t)udev->mb_addr;
	info->mem[0].size = TCMU_RING_SIZE;
	info->mem[0].memtype = UIO_MEM_NONE;

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
	if (!dev->dev_attrib.hw_max_sectors)
		dev->dev_attrib.hw_max_sectors = 128;
	if (!dev->dev_attrib.emulate_write_cache)
		dev->dev_attrib.emulate_write_cache = 0;
	dev->dev_attrib.hw_queue_depth = 128;

	/*
	 * Get a ref incase userspace does a close on the uio device before
	 * LIO has initiated tcmu_free_device.
	 */
	kref_get(&udev->kref);

	ret = tcmu_netlink_event(udev, TCMU_CMD_ADDED_DEVICE, 0, NULL);
	if (ret)
		goto err_netlink;

	mutex_lock(&root_udev_mutex);
	list_add(&udev->node, &root_udev);
	mutex_unlock(&root_udev_mutex);

	return 0;

err_netlink:
	kref_put(&udev->kref, tcmu_dev_kref_release);
	uio_unregister_device(&udev->uio_info);
err_register:
	vfree(udev->mb_addr);
err_vzalloc:
	kfree(info->name);
	info->name = NULL;

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

static bool tcmu_dev_configured(struct tcmu_dev *udev)
{
	return udev->uio_info.uio_dev ? true : false;
}

static void tcmu_blocks_release(struct tcmu_dev *udev)
{
	int i;
	struct page *page;

	/* Try to release all block pages */
	mutex_lock(&udev->cmdr_lock);
	for (i = 0; i <= udev->dbi_max; i++) {
		page = radix_tree_delete(&udev->data_blocks, i);
		if (page) {
			__free_page(page);
			atomic_dec(&global_db_count);
		}
	}
	mutex_unlock(&udev->cmdr_lock);
}

static void tcmu_free_device(struct se_device *dev)
{
	struct tcmu_dev *udev = TCMU_DEV(dev);

	/* release ref from init */
	kref_put(&udev->kref, tcmu_dev_kref_release);
}

static void tcmu_destroy_device(struct se_device *dev)
{
	struct tcmu_dev *udev = TCMU_DEV(dev);
	struct tcmu_cmd *cmd;
	bool all_expired = true;
	int i;

	del_timer_sync(&udev->timeout);

	mutex_lock(&root_udev_mutex);
	list_del(&udev->node);
	mutex_unlock(&root_udev_mutex);

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

	tcmu_blocks_release(udev);

	tcmu_netlink_event(udev, TCMU_CMD_REMOVED_DEVICE, 0, NULL);

	uio_unregister_device(&udev->uio_info);

	/* release ref from configure */
	kref_put(&udev->kref, tcmu_dev_kref_release);
}

enum {
	Opt_dev_config, Opt_dev_size, Opt_hw_block_size, Opt_hw_max_sectors,
	Opt_err,
};

static match_table_t tokens = {
	{Opt_dev_config, "dev_config=%s"},
	{Opt_dev_size, "dev_size=%u"},
	{Opt_hw_block_size, "hw_block_size=%u"},
	{Opt_hw_max_sectors, "hw_max_sectors=%u"},
	{Opt_err, NULL}
};

static int tcmu_set_dev_attrib(substring_t *arg, u32 *dev_attrib)
{
	unsigned long tmp_ul;
	char *arg_p;
	int ret;

	arg_p = match_strdup(arg);
	if (!arg_p)
		return -ENOMEM;

	ret = kstrtoul(arg_p, 0, &tmp_ul);
	kfree(arg_p);
	if (ret < 0) {
		pr_err("kstrtoul() failed for dev attrib\n");
		return ret;
	}
	if (!tmp_ul) {
		pr_err("dev attrib must be nonzero\n");
		return -EINVAL;
	}
	*dev_attrib = tmp_ul;
	return 0;
}

static ssize_t tcmu_set_configfs_dev_params(struct se_device *dev,
		const char *page, ssize_t count)
{
	struct tcmu_dev *udev = TCMU_DEV(dev);
	char *orig, *ptr, *opts, *arg_p;
	substring_t args[MAX_OPT_ARGS];
	int ret = 0, token;

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
			ret = tcmu_set_dev_attrib(&args[0],
					&(dev->dev_attrib.hw_block_size));
			break;
		case Opt_hw_max_sectors:
			ret = tcmu_set_dev_attrib(&args[0],
					&(dev->dev_attrib.hw_max_sectors));
			break;
		default:
			break;
		}

		if (ret)
			break;
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

static ssize_t tcmu_cmd_time_out_show(struct config_item *item, char *page)
{
	struct se_dev_attrib *da = container_of(to_config_group(item),
					struct se_dev_attrib, da_group);
	struct tcmu_dev *udev = container_of(da->da_dev,
					struct tcmu_dev, se_dev);

	return snprintf(page, PAGE_SIZE, "%lu\n", udev->cmd_time_out / MSEC_PER_SEC);
}

static ssize_t tcmu_cmd_time_out_store(struct config_item *item, const char *page,
				       size_t count)
{
	struct se_dev_attrib *da = container_of(to_config_group(item),
					struct se_dev_attrib, da_group);
	struct tcmu_dev *udev = container_of(da->da_dev,
					struct tcmu_dev, se_dev);
	u32 val;
	int ret;

	if (da->da_dev->export_count) {
		pr_err("Unable to set tcmu cmd_time_out while exports exist\n");
		return -EINVAL;
	}

	ret = kstrtou32(page, 0, &val);
	if (ret < 0)
		return ret;

	udev->cmd_time_out = val * MSEC_PER_SEC;
	return count;
}
CONFIGFS_ATTR(tcmu_, cmd_time_out);

static ssize_t tcmu_dev_config_show(struct config_item *item, char *page)
{
	struct se_dev_attrib *da = container_of(to_config_group(item),
						struct se_dev_attrib, da_group);
	struct tcmu_dev *udev = TCMU_DEV(da->da_dev);

	return snprintf(page, PAGE_SIZE, "%s\n", udev->dev_config);
}

static ssize_t tcmu_dev_config_store(struct config_item *item, const char *page,
				     size_t count)
{
	struct se_dev_attrib *da = container_of(to_config_group(item),
						struct se_dev_attrib, da_group);
	struct tcmu_dev *udev = TCMU_DEV(da->da_dev);
	int ret, len;

	len = strlen(page);
	if (!len || len > TCMU_CONFIG_LEN - 1)
		return -EINVAL;

	/* Check if device has been configured before */
	if (tcmu_dev_configured(udev)) {
		ret = tcmu_netlink_event(udev, TCMU_CMD_RECONFIG_DEVICE,
					 TCMU_ATTR_DEV_CFG, page);
		if (ret) {
			pr_err("Unable to reconfigure device\n");
			return ret;
		}
		strlcpy(udev->dev_config, page, TCMU_CONFIG_LEN);

		ret = tcmu_update_uio_info(udev);
		if (ret)
			return ret;
		return count;
	}
	strlcpy(udev->dev_config, page, TCMU_CONFIG_LEN);

	return count;
}
CONFIGFS_ATTR(tcmu_, dev_config);

static ssize_t tcmu_dev_size_show(struct config_item *item, char *page)
{
	struct se_dev_attrib *da = container_of(to_config_group(item),
						struct se_dev_attrib, da_group);
	struct tcmu_dev *udev = TCMU_DEV(da->da_dev);

	return snprintf(page, PAGE_SIZE, "%zu\n", udev->dev_size);
}

static ssize_t tcmu_dev_size_store(struct config_item *item, const char *page,
				   size_t count)
{
	struct se_dev_attrib *da = container_of(to_config_group(item),
						struct se_dev_attrib, da_group);
	struct tcmu_dev *udev = TCMU_DEV(da->da_dev);
	u64 val;
	int ret;

	ret = kstrtou64(page, 0, &val);
	if (ret < 0)
		return ret;

	/* Check if device has been configured before */
	if (tcmu_dev_configured(udev)) {
		ret = tcmu_netlink_event(udev, TCMU_CMD_RECONFIG_DEVICE,
					 TCMU_ATTR_DEV_SIZE, &val);
		if (ret) {
			pr_err("Unable to reconfigure device\n");
			return ret;
		}
	}
	udev->dev_size = val;
	return count;
}
CONFIGFS_ATTR(tcmu_, dev_size);

static ssize_t tcmu_emulate_write_cache_show(struct config_item *item,
					     char *page)
{
	struct se_dev_attrib *da = container_of(to_config_group(item),
					struct se_dev_attrib, da_group);

	return snprintf(page, PAGE_SIZE, "%i\n", da->emulate_write_cache);
}

static ssize_t tcmu_emulate_write_cache_store(struct config_item *item,
					      const char *page, size_t count)
{
	struct se_dev_attrib *da = container_of(to_config_group(item),
					struct se_dev_attrib, da_group);
	struct tcmu_dev *udev = TCMU_DEV(da->da_dev);
	u8 val;
	int ret;

	ret = kstrtou8(page, 0, &val);
	if (ret < 0)
		return ret;

	/* Check if device has been configured before */
	if (tcmu_dev_configured(udev)) {
		ret = tcmu_netlink_event(udev, TCMU_CMD_RECONFIG_DEVICE,
					 TCMU_ATTR_WRITECACHE, &val);
		if (ret) {
			pr_err("Unable to reconfigure device\n");
			return ret;
		}
	}

	da->emulate_write_cache = val;
	return count;
}
CONFIGFS_ATTR(tcmu_, emulate_write_cache);

static struct configfs_attribute *tcmu_attrib_attrs[] = {
	&tcmu_attr_cmd_time_out,
	&tcmu_attr_dev_config,
	&tcmu_attr_dev_size,
	&tcmu_attr_emulate_write_cache,
	NULL,
};

static struct configfs_attribute **tcmu_attrs;

static struct target_backend_ops tcmu_ops = {
	.name			= "user",
	.owner			= THIS_MODULE,
	.transport_flags	= TRANSPORT_FLAG_PASSTHROUGH,
	.attach_hba		= tcmu_attach_hba,
	.detach_hba		= tcmu_detach_hba,
	.alloc_device		= tcmu_alloc_device,
	.configure_device	= tcmu_configure_device,
	.destroy_device		= tcmu_destroy_device,
	.free_device		= tcmu_free_device,
	.parse_cdb		= tcmu_parse_cdb,
	.set_configfs_dev_params = tcmu_set_configfs_dev_params,
	.show_configfs_dev_params = tcmu_show_configfs_dev_params,
	.get_device_type	= sbc_get_device_type,
	.get_blocks		= tcmu_get_blocks,
	.tb_dev_attrib_attrs	= NULL,
};

static int unmap_thread_fn(void *data)
{
	struct tcmu_dev *udev;
	loff_t off;
	uint32_t start, end, block;
	struct page *page;
	int i;

	while (!kthread_should_stop()) {
		DEFINE_WAIT(__wait);

		prepare_to_wait(&unmap_wait, &__wait, TASK_INTERRUPTIBLE);
		schedule();
		finish_wait(&unmap_wait, &__wait);

		if (kthread_should_stop())
			break;

		mutex_lock(&root_udev_mutex);
		list_for_each_entry(udev, &root_udev, node) {
			mutex_lock(&udev->cmdr_lock);

			/* Try to complete the finished commands first */
			tcmu_handle_completions(udev);

			/* Skip the udevs waiting the global pool or in idle */
			if (udev->waiting_global || !udev->dbi_thresh) {
				mutex_unlock(&udev->cmdr_lock);
				continue;
			}

			end = udev->dbi_max + 1;
			block = find_last_bit(udev->data_bitmap, end);
			if (block == udev->dbi_max) {
				/*
				 * The last bit is dbi_max, so there is
				 * no need to shrink any blocks.
				 */
				mutex_unlock(&udev->cmdr_lock);
				continue;
			} else if (block == end) {
				/* The current udev will goto idle state */
				udev->dbi_thresh = start = 0;
				udev->dbi_max = 0;
			} else {
				udev->dbi_thresh = start = block + 1;
				udev->dbi_max = block;
			}

			/* Here will truncate the data area from off */
			off = udev->data_off + start * DATA_BLOCK_SIZE;
			unmap_mapping_range(udev->inode->i_mapping, off, 0, 1);

			/* Release the block pages */
			for (i = start; i < end; i++) {
				page = radix_tree_delete(&udev->data_blocks, i);
				if (page) {
					__free_page(page);
					atomic_dec(&global_db_count);
				}
			}
			mutex_unlock(&udev->cmdr_lock);
		}

		/*
		 * Try to wake up the udevs who are waiting
		 * for the global data pool.
		 */
		list_for_each_entry(udev, &root_udev, node) {
			if (udev->waiting_global)
				wake_up(&udev->wait_cmdr);
		}
		mutex_unlock(&root_udev_mutex);
	}

	return 0;
}

static int __init tcmu_module_init(void)
{
	int ret, i, k, len = 0;

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

	for (i = 0; passthrough_attrib_attrs[i] != NULL; i++) {
		len += sizeof(struct configfs_attribute *);
	}
	for (i = 0; tcmu_attrib_attrs[i] != NULL; i++) {
		len += sizeof(struct configfs_attribute *);
	}
	len += sizeof(struct configfs_attribute *);

	tcmu_attrs = kzalloc(len, GFP_KERNEL);
	if (!tcmu_attrs) {
		ret = -ENOMEM;
		goto out_unreg_genl;
	}

	for (i = 0; passthrough_attrib_attrs[i] != NULL; i++) {
		tcmu_attrs[i] = passthrough_attrib_attrs[i];
	}
	for (k = 0; tcmu_attrib_attrs[k] != NULL; k++) {
		tcmu_attrs[i] = tcmu_attrib_attrs[k];
		i++;
	}
	tcmu_ops.tb_dev_attrib_attrs = tcmu_attrs;

	ret = transport_backend_register(&tcmu_ops);
	if (ret)
		goto out_attrs;

	init_waitqueue_head(&unmap_wait);
	unmap_thread = kthread_run(unmap_thread_fn, NULL, "tcmu_unmap");
	if (IS_ERR(unmap_thread)) {
		ret = PTR_ERR(unmap_thread);
		goto out_unreg_transport;
	}

	return 0;

out_unreg_transport:
	target_backend_unregister(&tcmu_ops);
out_attrs:
	kfree(tcmu_attrs);
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
	kthread_stop(unmap_thread);
	target_backend_unregister(&tcmu_ops);
	kfree(tcmu_attrs);
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
