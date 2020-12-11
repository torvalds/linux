// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Shaohua Li <shli@kernel.org>
 * Copyright (C) 2014 Red Hat, Inc.
 * Copyright (C) 2015 Arrikto, Inc.
 * Copyright (C) 2017 Chinamobile, Inc.
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
#include <linux/workqueue.h>
#include <net/genetlink.h>
#include <scsi/scsi_common.h>
#include <scsi/scsi_proto.h>
#include <target/target_core_base.h>
#include <target/target_core_fabric.h>
#include <target/target_core_backend.h>

#include <linux/target_core_user.h>

/**
 * DOC: Userspace I/O
 * Userspace I/O
 * -------------
 *
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
 * internal to the mmap-ed area. There is separate space outside the
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
#define DATA_BLOCK_SHIFT PAGE_SHIFT
#define DATA_BLOCK_BITS_DEF (256 * 1024)

#define TCMU_MBS_TO_BLOCKS(_mbs) (_mbs << (20 - DATA_BLOCK_SHIFT))
#define TCMU_BLOCKS_TO_MBS(_blocks) (_blocks >> (20 - DATA_BLOCK_SHIFT))

/*
 * Default number of global data blocks(512K * PAGE_SIZE)
 * when the unmap thread will be started.
 */
#define TCMU_GLOBAL_MAX_BLOCKS_DEF (512 * 1024)

static u8 tcmu_kern_cmd_reply_supported;
static u8 tcmu_netlink_blocked;

static struct device *tcmu_root_device;

struct tcmu_hba {
	u32 host_id;
};

#define TCMU_CONFIG_LEN 256

static DEFINE_MUTEX(tcmu_nl_cmd_mutex);
static LIST_HEAD(tcmu_nl_cmd_list);

struct tcmu_dev;

struct tcmu_nl_cmd {
	/* wake up thread waiting for reply */
	struct completion complete;
	struct list_head nl_list;
	struct tcmu_dev *udev;
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
#define TCMU_DEV_BIT_BLOCKED 2
#define TCMU_DEV_BIT_TMR_NOTIFY 3
	unsigned long flags;

	struct uio_info uio_info;

	struct inode *inode;

	struct tcmu_mailbox *mb_addr;
	uint64_t dev_size;
	u32 cmdr_size;
	u32 cmdr_last_cleaned;
	/* Offset of data area from start of mb */
	/* Must add data_off and mb_addr to get the address */
	size_t data_off;
	size_t data_size;
	uint32_t max_blocks;
	size_t ring_size;

	struct mutex cmdr_lock;
	struct list_head qfull_queue;
	struct list_head tmr_queue;

	uint32_t dbi_max;
	uint32_t dbi_thresh;
	unsigned long *data_bitmap;
	struct radix_tree_root data_blocks;

	struct idr commands;

	struct timer_list cmd_timer;
	unsigned int cmd_time_out;
	struct list_head inflight_queue;

	struct timer_list qfull_timer;
	int qfull_time_out;

	struct list_head timedout_entry;

	struct tcmu_nl_cmd curr_nl_cmd;

	char dev_config[TCMU_CONFIG_LEN];

	int nl_reply_supported;
};

#define TCMU_DEV(_se_dev) container_of(_se_dev, struct tcmu_dev, se_dev)

#define CMDR_OFF sizeof(struct tcmu_mailbox)

struct tcmu_cmd {
	struct se_cmd *se_cmd;
	struct tcmu_dev *tcmu_dev;
	struct list_head queue_entry;

	uint16_t cmd_id;

	/* Can't use se_cmd when cleaning up expired cmds, because if
	   cmd has been completed then accessing se_cmd is off limits */
	uint32_t dbi_cnt;
	uint32_t dbi_bidi_cnt;
	uint32_t dbi_cur;
	uint32_t *dbi;

	uint32_t data_len_bidi;

	unsigned long deadline;

#define TCMU_CMD_BIT_EXPIRED 0
	unsigned long flags;
};

struct tcmu_tmr {
	struct list_head queue_entry;

	uint8_t tmr_type;
	uint32_t tmr_cmd_cnt;
	int16_t tmr_cmd_ids[0];
};

/*
 * To avoid dead lock the mutex lock order should always be:
 *
 * mutex_lock(&root_udev_mutex);
 * ...
 * mutex_lock(&tcmu_dev->cmdr_lock);
 * mutex_unlock(&tcmu_dev->cmdr_lock);
 * ...
 * mutex_unlock(&root_udev_mutex);
 */
static DEFINE_MUTEX(root_udev_mutex);
static LIST_HEAD(root_udev);

static DEFINE_SPINLOCK(timed_out_udevs_lock);
static LIST_HEAD(timed_out_udevs);

static struct kmem_cache *tcmu_cmd_cache;

static atomic_t global_db_count = ATOMIC_INIT(0);
static struct delayed_work tcmu_unmap_work;
static int tcmu_global_max_blocks = TCMU_GLOBAL_MAX_BLOCKS_DEF;

static int tcmu_set_global_max_data_area(const char *str,
					 const struct kernel_param *kp)
{
	int ret, max_area_mb;

	ret = kstrtoint(str, 10, &max_area_mb);
	if (ret)
		return -EINVAL;

	if (max_area_mb <= 0) {
		pr_err("global_max_data_area must be larger than 0.\n");
		return -EINVAL;
	}

	tcmu_global_max_blocks = TCMU_MBS_TO_BLOCKS(max_area_mb);
	if (atomic_read(&global_db_count) > tcmu_global_max_blocks)
		schedule_delayed_work(&tcmu_unmap_work, 0);
	else
		cancel_delayed_work_sync(&tcmu_unmap_work);

	return 0;
}

static int tcmu_get_global_max_data_area(char *buffer,
					 const struct kernel_param *kp)
{
	return sprintf(buffer, "%d\n", TCMU_BLOCKS_TO_MBS(tcmu_global_max_blocks));
}

static const struct kernel_param_ops tcmu_global_max_data_area_op = {
	.set = tcmu_set_global_max_data_area,
	.get = tcmu_get_global_max_data_area,
};

module_param_cb(global_max_data_area_mb, &tcmu_global_max_data_area_op, NULL,
		S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(global_max_data_area_mb,
		 "Max MBs allowed to be allocated to all the tcmu device's "
		 "data areas.");

static int tcmu_get_block_netlink(char *buffer,
				  const struct kernel_param *kp)
{
	return sprintf(buffer, "%s\n", tcmu_netlink_blocked ?
		       "blocked" : "unblocked");
}

static int tcmu_set_block_netlink(const char *str,
				  const struct kernel_param *kp)
{
	int ret;
	u8 val;

	ret = kstrtou8(str, 0, &val);
	if (ret < 0)
		return ret;

	if (val > 1) {
		pr_err("Invalid block netlink value %u\n", val);
		return -EINVAL;
	}

	tcmu_netlink_blocked = val;
	return 0;
}

static const struct kernel_param_ops tcmu_block_netlink_op = {
	.set = tcmu_set_block_netlink,
	.get = tcmu_get_block_netlink,
};

module_param_cb(block_netlink, &tcmu_block_netlink_op, NULL, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(block_netlink, "Block new netlink commands.");

static int tcmu_fail_netlink_cmd(struct tcmu_nl_cmd *nl_cmd)
{
	struct tcmu_dev *udev = nl_cmd->udev;

	if (!tcmu_netlink_blocked) {
		pr_err("Could not reset device's netlink interface. Netlink is not blocked.\n");
		return -EBUSY;
	}

	if (nl_cmd->cmd != TCMU_CMD_UNSPEC) {
		pr_debug("Aborting nl cmd %d on %s\n", nl_cmd->cmd, udev->name);
		nl_cmd->status = -EINTR;
		list_del(&nl_cmd->nl_list);
		complete(&nl_cmd->complete);
	}
	return 0;
}

static int tcmu_set_reset_netlink(const char *str,
				  const struct kernel_param *kp)
{
	struct tcmu_nl_cmd *nl_cmd, *tmp_cmd;
	int ret;
	u8 val;

	ret = kstrtou8(str, 0, &val);
	if (ret < 0)
		return ret;

	if (val != 1) {
		pr_err("Invalid reset netlink value %u\n", val);
		return -EINVAL;
	}

	mutex_lock(&tcmu_nl_cmd_mutex);
	list_for_each_entry_safe(nl_cmd, tmp_cmd, &tcmu_nl_cmd_list, nl_list) {
		ret = tcmu_fail_netlink_cmd(nl_cmd);
		if (ret)
			break;
	}
	mutex_unlock(&tcmu_nl_cmd_mutex);

	return ret;
}

static const struct kernel_param_ops tcmu_reset_netlink_op = {
	.set = tcmu_set_reset_netlink,
};

module_param_cb(reset_netlink, &tcmu_reset_netlink_op, NULL, S_IWUSR);
MODULE_PARM_DESC(reset_netlink, "Reset netlink commands.");

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
	struct tcmu_dev *udev = NULL;
	struct tcmu_nl_cmd *nl_cmd;
	int dev_id, rc, ret = 0;

	if (!info->attrs[TCMU_ATTR_CMD_STATUS] ||
	    !info->attrs[TCMU_ATTR_DEVICE_ID]) {
		printk(KERN_ERR "TCMU_ATTR_CMD_STATUS or TCMU_ATTR_DEVICE_ID not set, doing nothing\n");
		return -EINVAL;
        }

	dev_id = nla_get_u32(info->attrs[TCMU_ATTR_DEVICE_ID]);
	rc = nla_get_s32(info->attrs[TCMU_ATTR_CMD_STATUS]);

	mutex_lock(&tcmu_nl_cmd_mutex);
	list_for_each_entry(nl_cmd, &tcmu_nl_cmd_list, nl_list) {
		if (nl_cmd->udev->se_dev.dev_index == dev_id) {
			udev = nl_cmd->udev;
			break;
		}
	}

	if (!udev) {
		pr_err("tcmu nl cmd %u/%d completion could not find device with dev id %u.\n",
		       completed_cmd, rc, dev_id);
		ret = -ENODEV;
		goto unlock;
	}
	list_del(&nl_cmd->nl_list);

	pr_debug("%s genl cmd done got id %d curr %d done %d rc %d stat %d\n",
		 udev->name, dev_id, nl_cmd->cmd, completed_cmd, rc,
		 nl_cmd->status);

	if (nl_cmd->cmd != completed_cmd) {
		pr_err("Mismatched commands on %s (Expecting reply for %d. Current %d).\n",
		       udev->name, completed_cmd, nl_cmd->cmd);
		ret = -EINVAL;
		goto unlock;
	}

	nl_cmd->status = rc;
	complete(&nl_cmd->complete);
unlock:
	mutex_unlock(&tcmu_nl_cmd_mutex);
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

static const struct genl_small_ops tcmu_genl_ops[] = {
	{
		.cmd	= TCMU_CMD_SET_FEATURES,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.flags	= GENL_ADMIN_PERM,
		.doit	= tcmu_genl_set_features,
	},
	{
		.cmd	= TCMU_CMD_ADDED_DEVICE_DONE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.flags	= GENL_ADMIN_PERM,
		.doit	= tcmu_genl_add_dev_done,
	},
	{
		.cmd	= TCMU_CMD_REMOVED_DEVICE_DONE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.flags	= GENL_ADMIN_PERM,
		.doit	= tcmu_genl_rm_dev_done,
	},
	{
		.cmd	= TCMU_CMD_RECONFIG_DEVICE_DONE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.flags	= GENL_ADMIN_PERM,
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
	.policy = tcmu_attr_policy,
	.mcgrps = tcmu_mcgrps,
	.n_mcgrps = ARRAY_SIZE(tcmu_mcgrps),
	.netnsok = true,
	.small_ops = tcmu_genl_ops,
	.n_small_ops = ARRAY_SIZE(tcmu_genl_ops),
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

static inline int tcmu_get_empty_block(struct tcmu_dev *udev,
				       struct tcmu_cmd *tcmu_cmd,
				       int prev_dbi, int *iov_cnt)
{
	struct page *page;
	int ret, dbi;

	dbi = find_first_zero_bit(udev->data_bitmap, udev->dbi_thresh);
	if (dbi == udev->dbi_thresh)
		return -1;

	page = radix_tree_lookup(&udev->data_blocks, dbi);
	if (!page) {
		if (atomic_add_return(1, &global_db_count) >
				      tcmu_global_max_blocks)
			schedule_delayed_work(&tcmu_unmap_work, 0);

		/* try to get new page from the mm */
		page = alloc_page(GFP_NOIO);
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

	if (dbi != prev_dbi + 1)
		*iov_cnt += 1;

	return dbi;
err_insert:
	__free_page(page);
err_alloc:
	atomic_dec(&global_db_count);
	return -1;
}

static int tcmu_get_empty_blocks(struct tcmu_dev *udev,
				 struct tcmu_cmd *tcmu_cmd, int dbi_cnt)
{
	/* start value of dbi + 1 must not be a valid dbi */
	int dbi = -2;
	int i, iov_cnt = 0;

	for (i = 0; i < dbi_cnt; i++) {
		dbi = tcmu_get_empty_block(udev, tcmu_cmd, dbi, &iov_cnt);
		if (dbi < 0)
			return -1;
	}
	return iov_cnt;
}

static inline struct page *
tcmu_get_block_page(struct tcmu_dev *udev, uint32_t dbi)
{
	return radix_tree_lookup(&udev->data_blocks, dbi);
}

static inline void tcmu_free_cmd(struct tcmu_cmd *tcmu_cmd)
{
	if (tcmu_cmd->se_cmd)
		tcmu_cmd->se_cmd->priv = NULL;
	kfree(tcmu_cmd->dbi);
	kmem_cache_free(tcmu_cmd_cache, tcmu_cmd);
}

static inline void tcmu_cmd_set_block_cnts(struct tcmu_cmd *cmd)
{
	int i, len;
	struct se_cmd *se_cmd = cmd->se_cmd;

	cmd->dbi_cnt = DIV_ROUND_UP(se_cmd->data_length, DATA_BLOCK_SIZE);

	if (se_cmd->se_cmd_flags & SCF_BIDI) {
		BUG_ON(!(se_cmd->t_bidi_data_sg && se_cmd->t_bidi_data_nents));
		for (i = 0, len = 0; i < se_cmd->t_bidi_data_nents; i++)
			len += se_cmd->t_bidi_data_sg[i].length;
		cmd->dbi_bidi_cnt = DIV_ROUND_UP(len, DATA_BLOCK_SIZE);
		cmd->dbi_cnt += cmd->dbi_bidi_cnt;
		cmd->data_len_bidi = len;
	}
}

static int new_block_to_iov(struct tcmu_dev *udev, struct tcmu_cmd *cmd,
			    struct iovec **iov, int prev_dbi, int *remain)
{
	/* Get the next dbi */
	int dbi = tcmu_cmd_get_dbi(cmd);
	/* Do not add more than DATA_BLOCK_SIZE to iov */
	int len = min_t(int, DATA_BLOCK_SIZE, *remain);

	*remain -= len;
	/*
	 * The following code will gather and map the blocks to the same iovec
	 * when the blocks are all next to each other.
	 */
	if (dbi != prev_dbi + 1) {
		/* dbi is not next to previous dbi, so start new iov */
		if (prev_dbi >= 0)
			(*iov)++;
		/* write offset relative to mb_addr */
		(*iov)->iov_base = (void __user *)
				(udev->data_off + dbi * DATA_BLOCK_SIZE);
	}
	(*iov)->iov_len += len;

	return dbi;
}

static void tcmu_setup_iovs(struct tcmu_dev *udev, struct tcmu_cmd *cmd,
			    struct iovec **iov, int data_length)
{
	/* start value of dbi + 1 must not be a valid dbi */
	int dbi = -2;

	/* We prepare the IOVs for DMA_FROM_DEVICE transfer direction */
	while (data_length > 0)
		dbi = new_block_to_iov(udev, cmd, iov, dbi, &data_length);
}

static struct tcmu_cmd *tcmu_alloc_cmd(struct se_cmd *se_cmd)
{
	struct se_device *se_dev = se_cmd->se_dev;
	struct tcmu_dev *udev = TCMU_DEV(se_dev);
	struct tcmu_cmd *tcmu_cmd;

	tcmu_cmd = kmem_cache_zalloc(tcmu_cmd_cache, GFP_NOIO);
	if (!tcmu_cmd)
		return NULL;

	INIT_LIST_HEAD(&tcmu_cmd->queue_entry);
	tcmu_cmd->se_cmd = se_cmd;
	tcmu_cmd->tcmu_dev = udev;

	tcmu_cmd_set_block_cnts(tcmu_cmd);
	tcmu_cmd->dbi = kcalloc(tcmu_cmd->dbi_cnt, sizeof(uint32_t),
				GFP_NOIO);
	if (!tcmu_cmd->dbi) {
		kmem_cache_free(tcmu_cmd_cache, tcmu_cmd);
		return NULL;
	}

	return tcmu_cmd;
}

static inline void tcmu_flush_dcache_range(void *vaddr, size_t size)
{
	unsigned long offset = offset_in_page(vaddr);
	void *start = vaddr - offset;

	size = round_up(size+offset, PAGE_SIZE);

	while (size) {
		flush_dcache_page(vmalloc_to_page(start));
		start += PAGE_SIZE;
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

#define UPDATE_HEAD(head, used, size) smp_store_release(&head, ((head % size) + used) % size)

static void scatter_data_area(struct tcmu_dev *udev, struct tcmu_cmd *tcmu_cmd,
			      struct iovec **iov)
{
	struct se_cmd *se_cmd = tcmu_cmd->se_cmd;
	/* start value of dbi + 1 must not be a valid dbi */
	int i, dbi = -2;
	int block_remaining = 0;
	int data_len = se_cmd->data_length;
	void *from, *to = NULL;
	size_t copy_bytes, offset;
	struct scatterlist *sg;
	struct page *page = NULL;

	for_each_sg(se_cmd->t_data_sg, sg, se_cmd->t_data_nents, i) {
		int sg_remaining = sg->length;
		from = kmap_atomic(sg_page(sg)) + sg->offset;
		while (sg_remaining > 0) {
			if (block_remaining == 0) {
				if (to) {
					flush_dcache_page(page);
					kunmap_atomic(to);
				}

				/* get next dbi and add to IOVs */
				dbi = new_block_to_iov(udev, tcmu_cmd, iov, dbi,
						       &data_len);
				page = tcmu_get_block_page(udev, dbi);
				to = kmap_atomic(page);
				block_remaining = DATA_BLOCK_SIZE;
			}

			copy_bytes = min_t(size_t, sg_remaining,
					block_remaining);
			offset = DATA_BLOCK_SIZE - block_remaining;
			memcpy(to + offset, from + sg->length - sg_remaining,
			       copy_bytes);

			sg_remaining -= copy_bytes;
			block_remaining -= copy_bytes;
		}
		kunmap_atomic(from - sg->offset);
	}

	if (to) {
		flush_dcache_page(page);
		kunmap_atomic(to);
	}
}

static void gather_data_area(struct tcmu_dev *udev, struct tcmu_cmd *cmd,
			     bool bidi, uint32_t read_len)
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
		 * the Data-Out buffer blocks should be skipped.
		 */
		count = cmd->dbi_cnt - cmd->dbi_bidi_cnt;

		data_sg = se_cmd->t_bidi_data_sg;
		data_nents = se_cmd->t_bidi_data_nents;
	}

	tcmu_cmd_set_dbi_cur(cmd, count);

	for_each_sg(data_sg, sg, data_nents, i) {
		int sg_remaining = sg->length;
		to = kmap_atomic(sg_page(sg)) + sg->offset;
		while (sg_remaining > 0 && read_len > 0) {
			if (block_remaining == 0) {
				if (from)
					kunmap_atomic(from);

				block_remaining = DATA_BLOCK_SIZE;
				dbi = tcmu_cmd_get_dbi(cmd);
				page = tcmu_get_block_page(udev, dbi);
				from = kmap_atomic(page);
				flush_dcache_page(page);
			}
			copy_bytes = min_t(size_t, sg_remaining,
					block_remaining);
			if (read_len < copy_bytes)
				copy_bytes = read_len;
			offset = DATA_BLOCK_SIZE - block_remaining;
			memcpy(to + sg->length - sg_remaining, from + offset,
					copy_bytes);

			sg_remaining -= copy_bytes;
			block_remaining -= copy_bytes;
			read_len -= copy_bytes;
		}
		kunmap_atomic(to - sg->offset);
		if (read_len == 0)
			break;
	}
	if (from)
		kunmap_atomic(from);
}

static inline size_t spc_bitmap_free(unsigned long *bitmap, uint32_t thresh)
{
	return thresh - bitmap_weight(bitmap, thresh);
}

/*
 * We can't queue a command until we have space available on the cmd ring.
 *
 * Called with ring lock held.
 */
static bool is_ring_space_avail(struct tcmu_dev *udev, size_t cmd_size)
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
	return true;
}

/*
 * We have to allocate data buffers before we can queue a command.
 * Returns -1 on error (not enough space) or number of needed iovs on success
 *
 * Called with ring lock held.
 */
static int tcmu_alloc_data_space(struct tcmu_dev *udev, struct tcmu_cmd *cmd,
				  int *iov_bidi_cnt)
{
	int space, iov_cnt = 0, ret = 0;

	if (!cmd->dbi_cnt)
		goto wr_iov_cnts;

	/* try to check and get the data blocks as needed */
	space = spc_bitmap_free(udev->data_bitmap, udev->dbi_thresh);
	if (space < cmd->dbi_cnt) {
		unsigned long blocks_left =
				(udev->max_blocks - udev->dbi_thresh) + space;

		if (blocks_left < cmd->dbi_cnt) {
			pr_debug("no data space: only %lu available, but ask for %lu\n",
					blocks_left * DATA_BLOCK_SIZE,
					cmd->dbi_cnt * DATA_BLOCK_SIZE);
			return -1;
		}

		udev->dbi_thresh += cmd->dbi_cnt;
		if (udev->dbi_thresh > udev->max_blocks)
			udev->dbi_thresh = udev->max_blocks;
	}

	iov_cnt = tcmu_get_empty_blocks(udev, cmd,
					cmd->dbi_cnt - cmd->dbi_bidi_cnt);
	if (iov_cnt < 0)
		return -1;

	if (cmd->dbi_bidi_cnt) {
		ret = tcmu_get_empty_blocks(udev, cmd, cmd->dbi_bidi_cnt);
		if (ret < 0)
			return -1;
	}
wr_iov_cnts:
	*iov_bidi_cnt = ret;
	return iov_cnt + ret;
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

static void tcmu_setup_cmd_timer(struct tcmu_cmd *tcmu_cmd, unsigned int tmo,
				 struct timer_list *timer)
{
	if (!tmo)
		return;

	tcmu_cmd->deadline = round_jiffies_up(jiffies + msecs_to_jiffies(tmo));
	if (!timer_pending(timer))
		mod_timer(timer, tcmu_cmd->deadline);

	pr_debug("Timeout set up for cmd %p, dev = %s, tmo = %lu\n", tcmu_cmd,
		 tcmu_cmd->tcmu_dev->name, tmo / MSEC_PER_SEC);
}

static int add_to_qfull_queue(struct tcmu_cmd *tcmu_cmd)
{
	struct tcmu_dev *udev = tcmu_cmd->tcmu_dev;
	unsigned int tmo;

	/*
	 * For backwards compat if qfull_time_out is not set use
	 * cmd_time_out and if that's not set use the default time out.
	 */
	if (!udev->qfull_time_out)
		return -ETIMEDOUT;
	else if (udev->qfull_time_out > 0)
		tmo = udev->qfull_time_out;
	else if (udev->cmd_time_out)
		tmo = udev->cmd_time_out;
	else
		tmo = TCMU_TIME_OUT;

	tcmu_setup_cmd_timer(tcmu_cmd, tmo, &udev->qfull_timer);

	list_add_tail(&tcmu_cmd->queue_entry, &udev->qfull_queue);
	pr_debug("adding cmd %p on dev %s to ring space wait queue\n",
		 tcmu_cmd, udev->name);
	return 0;
}

static uint32_t ring_insert_padding(struct tcmu_dev *udev, size_t cmd_size)
{
	struct tcmu_cmd_entry_hdr *hdr;
	struct tcmu_mailbox *mb = udev->mb_addr;
	uint32_t cmd_head = mb->cmd_head % udev->cmdr_size; /* UAM */

	/* Insert a PAD if end-of-ring space is too small */
	if (head_to_end(cmd_head, udev->cmdr_size) < cmd_size) {
		size_t pad_size = head_to_end(cmd_head, udev->cmdr_size);

		hdr = (void *) mb + CMDR_OFF + cmd_head;
		tcmu_hdr_set_op(&hdr->len_op, TCMU_OP_PAD);
		tcmu_hdr_set_len(&hdr->len_op, pad_size);
		hdr->cmd_id = 0; /* not used for PAD */
		hdr->kflags = 0;
		hdr->uflags = 0;
		tcmu_flush_dcache_range(hdr, sizeof(*hdr));

		UPDATE_HEAD(mb->cmd_head, pad_size, udev->cmdr_size);
		tcmu_flush_dcache_range(mb, sizeof(*mb));

		cmd_head = mb->cmd_head % udev->cmdr_size; /* UAM */
		WARN_ON(cmd_head != 0);
	}

	return cmd_head;
}

/**
 * queue_cmd_ring - queue cmd to ring or internally
 * @tcmu_cmd: cmd to queue
 * @scsi_err: TCM error code if failure (-1) returned.
 *
 * Returns:
 * -1 we cannot queue internally or to the ring.
 *  0 success
 *  1 internally queued to wait for ring memory to free.
 */
static int queue_cmd_ring(struct tcmu_cmd *tcmu_cmd, sense_reason_t *scsi_err)
{
	struct tcmu_dev *udev = tcmu_cmd->tcmu_dev;
	struct se_cmd *se_cmd = tcmu_cmd->se_cmd;
	size_t base_command_size, command_size;
	struct tcmu_mailbox *mb = udev->mb_addr;
	struct tcmu_cmd_entry *entry;
	struct iovec *iov;
	int iov_cnt, iov_bidi_cnt, cmd_id;
	uint32_t cmd_head;
	uint64_t cdb_off;
	/* size of data buffer needed */
	size_t data_length = (size_t)tcmu_cmd->dbi_cnt * DATA_BLOCK_SIZE;

	*scsi_err = TCM_NO_SENSE;

	if (test_bit(TCMU_DEV_BIT_BLOCKED, &udev->flags)) {
		*scsi_err = TCM_LUN_BUSY;
		return -1;
	}

	if (test_bit(TCMU_DEV_BIT_BROKEN, &udev->flags)) {
		*scsi_err = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		return -1;
	}

	if (!list_empty(&udev->qfull_queue))
		goto queue;

	if (data_length > udev->data_size) {
		pr_warn("TCMU: Request of size %zu is too big for %zu data area\n",
			data_length, udev->data_size);
		*scsi_err = TCM_INVALID_CDB_FIELD;
		return -1;
	}

	iov_cnt = tcmu_alloc_data_space(udev, tcmu_cmd, &iov_bidi_cnt);
	if (iov_cnt < 0)
		goto free_and_queue;

	/*
	 * Must be a certain minimum size for response sense info, but
	 * also may be larger if the iov array is large.
	 */
	base_command_size = tcmu_cmd_get_base_cmd_size(iov_cnt);
	command_size = tcmu_cmd_get_cmd_size(tcmu_cmd, base_command_size);

	if (command_size > (udev->cmdr_size / 2)) {
		pr_warn("TCMU: Request of size %zu is too big for %u cmd ring\n",
			command_size, udev->cmdr_size);
		tcmu_cmd_free_data(tcmu_cmd, tcmu_cmd->dbi_cur);
		*scsi_err = TCM_INVALID_CDB_FIELD;
		return -1;
	}

	if (!is_ring_space_avail(udev, command_size))
		/*
		 * Don't leave commands partially setup because the unmap
		 * thread might need the blocks to make forward progress.
		 */
		goto free_and_queue;

	cmd_id = idr_alloc(&udev->commands, tcmu_cmd, 1, USHRT_MAX, GFP_NOWAIT);
	if (cmd_id < 0) {
		pr_err("tcmu: Could not allocate cmd id.\n");

		tcmu_cmd_free_data(tcmu_cmd, tcmu_cmd->dbi_cnt);
		*scsi_err = TCM_OUT_OF_RESOURCES;
		return -1;
	}
	tcmu_cmd->cmd_id = cmd_id;

	pr_debug("allocated cmd id %u for cmd %p dev %s\n", tcmu_cmd->cmd_id,
		 tcmu_cmd, udev->name);

	cmd_head = ring_insert_padding(udev, command_size);

	entry = (void *) mb + CMDR_OFF + cmd_head;
	memset(entry, 0, command_size);
	tcmu_hdr_set_op(&entry->hdr.len_op, TCMU_OP_CMD);

	/* prepare iov list and copy data to data area if necessary */
	tcmu_cmd_reset_dbi_cur(tcmu_cmd);
	iov = &entry->req.iov[0];

	if (se_cmd->data_direction == DMA_TO_DEVICE ||
	    se_cmd->se_cmd_flags & SCF_BIDI)
		scatter_data_area(udev, tcmu_cmd, &iov);
	else
		tcmu_setup_iovs(udev, tcmu_cmd, &iov, se_cmd->data_length);

	entry->req.iov_cnt = iov_cnt - iov_bidi_cnt;

	/* Handle BIDI commands */
	if (se_cmd->se_cmd_flags & SCF_BIDI) {
		iov++;
		tcmu_setup_iovs(udev, tcmu_cmd, &iov, tcmu_cmd->data_len_bidi);
		entry->req.iov_bidi_cnt = iov_bidi_cnt;
	}

	tcmu_setup_cmd_timer(tcmu_cmd, udev->cmd_time_out, &udev->cmd_timer);

	entry->hdr.cmd_id = tcmu_cmd->cmd_id;

	tcmu_hdr_set_len(&entry->hdr.len_op, command_size);

	/* All offsets relative to mb_addr, not start of entry! */
	cdb_off = CMDR_OFF + cmd_head + base_command_size;
	memcpy((void *) mb + cdb_off, se_cmd->t_task_cdb, scsi_command_size(se_cmd->t_task_cdb));
	entry->req.cdb_off = cdb_off;
	tcmu_flush_dcache_range(entry, command_size);

	UPDATE_HEAD(mb->cmd_head, command_size, udev->cmdr_size);
	tcmu_flush_dcache_range(mb, sizeof(*mb));

	list_add_tail(&tcmu_cmd->queue_entry, &udev->inflight_queue);

	/* TODO: only if FLUSH and FUA? */
	uio_event_notify(&udev->uio_info);

	return 0;

free_and_queue:
	tcmu_cmd_free_data(tcmu_cmd, tcmu_cmd->dbi_cur);
	tcmu_cmd_reset_dbi_cur(tcmu_cmd);

queue:
	if (add_to_qfull_queue(tcmu_cmd)) {
		*scsi_err = TCM_OUT_OF_RESOURCES;
		return -1;
	}

	return 1;
}

/**
 * queue_tmr_ring - queue tmr info to ring or internally
 * @udev: related tcmu_dev
 * @tmr: tcmu_tmr containing tmr info to queue
 *
 * Returns:
 *  0 success
 *  1 internally queued to wait for ring memory to free.
 */
static int
queue_tmr_ring(struct tcmu_dev *udev, struct tcmu_tmr *tmr)
{
	struct tcmu_tmr_entry *entry;
	int cmd_size;
	int id_list_sz;
	struct tcmu_mailbox *mb = udev->mb_addr;
	uint32_t cmd_head;

	if (test_bit(TCMU_DEV_BIT_BROKEN, &udev->flags))
		goto out_free;

	id_list_sz = sizeof(tmr->tmr_cmd_ids[0]) * tmr->tmr_cmd_cnt;
	cmd_size = round_up(sizeof(*entry) + id_list_sz, TCMU_OP_ALIGN_SIZE);

	if (!list_empty(&udev->tmr_queue) ||
	    !is_ring_space_avail(udev, cmd_size)) {
		list_add_tail(&tmr->queue_entry, &udev->tmr_queue);
		pr_debug("adding tmr %p on dev %s to TMR ring space wait queue\n",
			 tmr, udev->name);
		return 1;
	}

	cmd_head = ring_insert_padding(udev, cmd_size);

	entry = (void *)mb + CMDR_OFF + cmd_head;
	memset(entry, 0, cmd_size);
	tcmu_hdr_set_op(&entry->hdr.len_op, TCMU_OP_TMR);
	tcmu_hdr_set_len(&entry->hdr.len_op, cmd_size);
	entry->tmr_type = tmr->tmr_type;
	entry->cmd_cnt = tmr->tmr_cmd_cnt;
	memcpy(&entry->cmd_ids[0], &tmr->tmr_cmd_ids[0], id_list_sz);
	tcmu_flush_dcache_range(entry, cmd_size);

	UPDATE_HEAD(mb->cmd_head, cmd_size, udev->cmdr_size);
	tcmu_flush_dcache_range(mb, sizeof(*mb));

	uio_event_notify(&udev->uio_info);

out_free:
	kfree(tmr);

	return 0;
}

static sense_reason_t
tcmu_queue_cmd(struct se_cmd *se_cmd)
{
	struct se_device *se_dev = se_cmd->se_dev;
	struct tcmu_dev *udev = TCMU_DEV(se_dev);
	struct tcmu_cmd *tcmu_cmd;
	sense_reason_t scsi_ret = TCM_CHECK_CONDITION_ABORT_CMD;
	int ret = -1;

	tcmu_cmd = tcmu_alloc_cmd(se_cmd);
	if (!tcmu_cmd)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	mutex_lock(&udev->cmdr_lock);
	se_cmd->priv = tcmu_cmd;
	if (!(se_cmd->transport_state & CMD_T_ABORTED))
		ret = queue_cmd_ring(tcmu_cmd, &scsi_ret);
	if (ret < 0)
		tcmu_free_cmd(tcmu_cmd);
	mutex_unlock(&udev->cmdr_lock);
	return scsi_ret;
}

static void tcmu_set_next_deadline(struct list_head *queue,
				   struct timer_list *timer)
{
	struct tcmu_cmd *cmd;

	if (!list_empty(queue)) {
		cmd = list_first_entry(queue, struct tcmu_cmd, queue_entry);
		mod_timer(timer, cmd->deadline);
	} else
		del_timer(timer);
}

static int
tcmu_tmr_type(enum tcm_tmreq_table tmf)
{
	switch (tmf) {
	case TMR_ABORT_TASK:		return TCMU_TMR_ABORT_TASK;
	case TMR_ABORT_TASK_SET:	return TCMU_TMR_ABORT_TASK_SET;
	case TMR_CLEAR_ACA:		return TCMU_TMR_CLEAR_ACA;
	case TMR_CLEAR_TASK_SET:	return TCMU_TMR_CLEAR_TASK_SET;
	case TMR_LUN_RESET:		return TCMU_TMR_LUN_RESET;
	case TMR_TARGET_WARM_RESET:	return TCMU_TMR_TARGET_WARM_RESET;
	case TMR_TARGET_COLD_RESET:	return TCMU_TMR_TARGET_COLD_RESET;
	case TMR_LUN_RESET_PRO:		return TCMU_TMR_LUN_RESET_PRO;
	default:			return TCMU_TMR_UNKNOWN;
	}
}

static void
tcmu_tmr_notify(struct se_device *se_dev, enum tcm_tmreq_table tmf,
		struct list_head *cmd_list)
{
	int i = 0, cmd_cnt = 0;
	bool unqueued = false;
	uint16_t *cmd_ids = NULL;
	struct tcmu_cmd *cmd;
	struct se_cmd *se_cmd;
	struct tcmu_tmr *tmr;
	struct tcmu_dev *udev = TCMU_DEV(se_dev);

	mutex_lock(&udev->cmdr_lock);

	/* First we check for aborted commands in qfull_queue */
	list_for_each_entry(se_cmd, cmd_list, state_list) {
		i++;
		if (!se_cmd->priv)
			continue;
		cmd = se_cmd->priv;
		/* Commands on qfull queue have no id yet */
		if (cmd->cmd_id) {
			cmd_cnt++;
			continue;
		}
		pr_debug("Removing aborted command %p from queue on dev %s.\n",
			 cmd, udev->name);

		list_del_init(&cmd->queue_entry);
		tcmu_free_cmd(cmd);
		target_complete_cmd(se_cmd, SAM_STAT_TASK_ABORTED);
		unqueued = true;
	}
	if (unqueued)
		tcmu_set_next_deadline(&udev->qfull_queue, &udev->qfull_timer);

	if (!test_bit(TCMU_DEV_BIT_TMR_NOTIFY, &udev->flags))
		goto unlock;

	pr_debug("TMR event %d on dev %s, aborted cmds %d, afflicted cmd_ids %d\n",
		 tcmu_tmr_type(tmf), udev->name, i, cmd_cnt);

	tmr = kmalloc(sizeof(*tmr) + cmd_cnt * sizeof(*cmd_ids), GFP_KERNEL);
	if (!tmr)
		goto unlock;

	tmr->tmr_type = tcmu_tmr_type(tmf);
	tmr->tmr_cmd_cnt = cmd_cnt;

	if (cmd_cnt != 0) {
		cmd_cnt = 0;
		list_for_each_entry(se_cmd, cmd_list, state_list) {
			if (!se_cmd->priv)
				continue;
			cmd = se_cmd->priv;
			if (cmd->cmd_id)
				tmr->tmr_cmd_ids[cmd_cnt++] = cmd->cmd_id;
		}
	}

	queue_tmr_ring(udev, tmr);

unlock:
	mutex_unlock(&udev->cmdr_lock);
}

static void tcmu_handle_completion(struct tcmu_cmd *cmd, struct tcmu_cmd_entry *entry)
{
	struct se_cmd *se_cmd = cmd->se_cmd;
	struct tcmu_dev *udev = cmd->tcmu_dev;
	bool read_len_valid = false;
	uint32_t read_len;

	/*
	 * cmd has been completed already from timeout, just reclaim
	 * data area space and free cmd
	 */
	if (test_bit(TCMU_CMD_BIT_EXPIRED, &cmd->flags)) {
		WARN_ON_ONCE(se_cmd);
		goto out;
	}

	list_del_init(&cmd->queue_entry);

	tcmu_cmd_reset_dbi_cur(cmd);

	if (entry->hdr.uflags & TCMU_UFLAG_UNKNOWN_OP) {
		pr_warn("TCMU: Userspace set UNKNOWN_OP flag on se_cmd %p\n",
			cmd->se_cmd);
		entry->rsp.scsi_status = SAM_STAT_CHECK_CONDITION;
		goto done;
	}

	read_len = se_cmd->data_length;
	if (se_cmd->data_direction == DMA_FROM_DEVICE &&
	    (entry->hdr.uflags & TCMU_UFLAG_READ_LEN) && entry->rsp.read_len) {
		read_len_valid = true;
		if (entry->rsp.read_len < read_len)
			read_len = entry->rsp.read_len;
	}

	if (entry->rsp.scsi_status == SAM_STAT_CHECK_CONDITION) {
		transport_copy_sense_to_cmd(se_cmd, entry->rsp.sense_buffer);
		if (!read_len_valid )
			goto done;
		else
			se_cmd->se_cmd_flags |= SCF_TREAT_READ_AS_NORMAL;
	}
	if (se_cmd->se_cmd_flags & SCF_BIDI) {
		/* Get Data-In buffer before clean up */
		gather_data_area(udev, cmd, true, read_len);
	} else if (se_cmd->data_direction == DMA_FROM_DEVICE) {
		gather_data_area(udev, cmd, false, read_len);
	} else if (se_cmd->data_direction == DMA_TO_DEVICE) {
		/* TODO: */
	} else if (se_cmd->data_direction != DMA_NONE) {
		pr_warn("TCMU: data direction was %d!\n",
			se_cmd->data_direction);
	}

done:
	if (read_len_valid) {
		pr_debug("read_len = %d\n", read_len);
		target_complete_cmd_with_length(cmd->se_cmd,
					entry->rsp.scsi_status, read_len);
	} else
		target_complete_cmd(cmd->se_cmd, entry->rsp.scsi_status);

out:
	tcmu_cmd_free_data(cmd, cmd->dbi_cnt);
	tcmu_free_cmd(cmd);
}

static int tcmu_run_tmr_queue(struct tcmu_dev *udev)
{
	struct tcmu_tmr *tmr, *tmp;
	LIST_HEAD(tmrs);

	if (list_empty(&udev->tmr_queue))
		return 1;

	pr_debug("running %s's tmr queue\n", udev->name);

	list_splice_init(&udev->tmr_queue, &tmrs);

	list_for_each_entry_safe(tmr, tmp, &tmrs, queue_entry) {
		list_del_init(&tmr->queue_entry);

		pr_debug("removing tmr %p on dev %s from queue\n",
			 tmr, udev->name);

		if (queue_tmr_ring(udev, tmr)) {
			pr_debug("ran out of space during tmr queue run\n");
			/*
			 * tmr was requeued, so just put all tmrs back in
			 * the queue
			 */
			list_splice_tail(&tmrs, &udev->tmr_queue);
			return 0;
		}
	}

	return 1;
}

static unsigned int tcmu_handle_completions(struct tcmu_dev *udev)
{
	struct tcmu_mailbox *mb;
	struct tcmu_cmd *cmd;
	bool free_space = false;

	if (test_bit(TCMU_DEV_BIT_BROKEN, &udev->flags)) {
		pr_err("ring broken, not handling completions\n");
		return 0;
	}

	mb = udev->mb_addr;
	tcmu_flush_dcache_range(mb, sizeof(*mb));

	while (udev->cmdr_last_cleaned != READ_ONCE(mb->cmd_tail)) {

		struct tcmu_cmd_entry *entry = (void *) mb + CMDR_OFF + udev->cmdr_last_cleaned;

		/*
		 * Flush max. up to end of cmd ring since current entry might
		 * be a padding that is shorter than sizeof(*entry)
		 */
		size_t ring_left = head_to_end(udev->cmdr_last_cleaned,
					       udev->cmdr_size);
		tcmu_flush_dcache_range(entry, ring_left < sizeof(*entry) ?
					ring_left : sizeof(*entry));

		free_space = true;

		if (tcmu_hdr_get_op(entry->hdr.len_op) == TCMU_OP_PAD ||
		    tcmu_hdr_get_op(entry->hdr.len_op) == TCMU_OP_TMR) {
			UPDATE_HEAD(udev->cmdr_last_cleaned,
				    tcmu_hdr_get_len(entry->hdr.len_op),
				    udev->cmdr_size);
			continue;
		}
		WARN_ON(tcmu_hdr_get_op(entry->hdr.len_op) != TCMU_OP_CMD);

		cmd = idr_remove(&udev->commands, entry->hdr.cmd_id);
		if (!cmd) {
			pr_err("cmd_id %u not found, ring is broken\n",
			       entry->hdr.cmd_id);
			set_bit(TCMU_DEV_BIT_BROKEN, &udev->flags);
			break;
		}

		tcmu_handle_completion(cmd, entry);

		UPDATE_HEAD(udev->cmdr_last_cleaned,
			    tcmu_hdr_get_len(entry->hdr.len_op),
			    udev->cmdr_size);
	}
	if (free_space)
		free_space = tcmu_run_tmr_queue(udev);

	if (atomic_read(&global_db_count) > tcmu_global_max_blocks &&
	    idr_is_empty(&udev->commands) && list_empty(&udev->qfull_queue)) {
		/*
		 * Allocated blocks exceeded global block limit, currently no
		 * more pending or waiting commands so try to reclaim blocks.
		 */
		schedule_delayed_work(&tcmu_unmap_work, 0);
	}
	if (udev->cmd_time_out)
		tcmu_set_next_deadline(&udev->inflight_queue, &udev->cmd_timer);

	return free_space;
}

static void tcmu_check_expired_ring_cmd(struct tcmu_cmd *cmd)
{
	struct se_cmd *se_cmd;

	if (!time_after_eq(jiffies, cmd->deadline))
		return;

	set_bit(TCMU_CMD_BIT_EXPIRED, &cmd->flags);
	list_del_init(&cmd->queue_entry);
	se_cmd = cmd->se_cmd;
	se_cmd->priv = NULL;
	cmd->se_cmd = NULL;

	pr_debug("Timing out inflight cmd %u on dev %s.\n",
		 cmd->cmd_id, cmd->tcmu_dev->name);

	target_complete_cmd(se_cmd, SAM_STAT_CHECK_CONDITION);
}

static void tcmu_check_expired_queue_cmd(struct tcmu_cmd *cmd)
{
	struct se_cmd *se_cmd;

	if (!time_after_eq(jiffies, cmd->deadline))
		return;

	pr_debug("Timing out queued cmd %p on dev %s.\n",
		  cmd, cmd->tcmu_dev->name);

	list_del_init(&cmd->queue_entry);
	se_cmd = cmd->se_cmd;
	tcmu_free_cmd(cmd);

	target_complete_cmd(se_cmd, SAM_STAT_TASK_SET_FULL);
}

static void tcmu_device_timedout(struct tcmu_dev *udev)
{
	spin_lock(&timed_out_udevs_lock);
	if (list_empty(&udev->timedout_entry))
		list_add_tail(&udev->timedout_entry, &timed_out_udevs);
	spin_unlock(&timed_out_udevs_lock);

	schedule_delayed_work(&tcmu_unmap_work, 0);
}

static void tcmu_cmd_timedout(struct timer_list *t)
{
	struct tcmu_dev *udev = from_timer(udev, t, cmd_timer);

	pr_debug("%s cmd timeout has expired\n", udev->name);
	tcmu_device_timedout(udev);
}

static void tcmu_qfull_timedout(struct timer_list *t)
{
	struct tcmu_dev *udev = from_timer(udev, t, qfull_timer);

	pr_debug("%s qfull timeout has expired\n", udev->name);
	tcmu_device_timedout(udev);
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
	udev->qfull_time_out = -1;

	udev->max_blocks = DATA_BLOCK_BITS_DEF;
	mutex_init(&udev->cmdr_lock);

	INIT_LIST_HEAD(&udev->node);
	INIT_LIST_HEAD(&udev->timedout_entry);
	INIT_LIST_HEAD(&udev->qfull_queue);
	INIT_LIST_HEAD(&udev->tmr_queue);
	INIT_LIST_HEAD(&udev->inflight_queue);
	idr_init(&udev->commands);

	timer_setup(&udev->qfull_timer, tcmu_qfull_timedout, 0);
	timer_setup(&udev->cmd_timer, tcmu_cmd_timedout, 0);

	INIT_RADIX_TREE(&udev->data_blocks, GFP_KERNEL);

	return &udev->se_dev;
}

static void run_qfull_queue(struct tcmu_dev *udev, bool fail)
{
	struct tcmu_cmd *tcmu_cmd, *tmp_cmd;
	LIST_HEAD(cmds);
	sense_reason_t scsi_ret;
	int ret;

	if (list_empty(&udev->qfull_queue))
		return;

	pr_debug("running %s's cmdr queue forcefail %d\n", udev->name, fail);

	list_splice_init(&udev->qfull_queue, &cmds);

	list_for_each_entry_safe(tcmu_cmd, tmp_cmd, &cmds, queue_entry) {
		list_del_init(&tcmu_cmd->queue_entry);

		pr_debug("removing cmd %p on dev %s from queue\n",
			 tcmu_cmd, udev->name);

		if (fail) {
			/*
			 * We were not able to even start the command, so
			 * fail with busy to allow a retry in case runner
			 * was only temporarily down. If the device is being
			 * removed then LIO core will do the right thing and
			 * fail the retry.
			 */
			target_complete_cmd(tcmu_cmd->se_cmd, SAM_STAT_BUSY);
			tcmu_free_cmd(tcmu_cmd);
			continue;
		}

		ret = queue_cmd_ring(tcmu_cmd, &scsi_ret);
		if (ret < 0) {
			pr_debug("cmd %p on dev %s failed with %u\n",
				 tcmu_cmd, udev->name, scsi_ret);
			/*
			 * Ignore scsi_ret for now. target_complete_cmd
			 * drops it.
			 */
			target_complete_cmd(tcmu_cmd->se_cmd,
					    SAM_STAT_CHECK_CONDITION);
			tcmu_free_cmd(tcmu_cmd);
		} else if (ret > 0) {
			pr_debug("ran out of space during cmdr queue run\n");
			/*
			 * cmd was requeued, so just put all cmds back in
			 * the queue
			 */
			list_splice_tail(&cmds, &udev->qfull_queue);
			break;
		}
	}

	tcmu_set_next_deadline(&udev->qfull_queue, &udev->qfull_timer);
}

static int tcmu_irqcontrol(struct uio_info *info, s32 irq_on)
{
	struct tcmu_dev *udev = container_of(info, struct tcmu_dev, uio_info);

	mutex_lock(&udev->cmdr_lock);
	if (tcmu_handle_completions(udev))
		run_qfull_queue(udev, false);
	mutex_unlock(&udev->cmdr_lock);

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

	mutex_lock(&udev->cmdr_lock);
	page = tcmu_get_block_page(udev, dbi);
	if (likely(page)) {
		mutex_unlock(&udev->cmdr_lock);
		return page;
	}

	/*
	 * Userspace messed up and passed in a address not in the
	 * data iov passed to it.
	 */
	pr_err("Invalid addr to data block mapping  (dbi %u) on device %s\n",
	       dbi, udev->name);
	page = NULL;
	mutex_unlock(&udev->cmdr_lock);

	return page;
}

static vm_fault_t tcmu_vma_fault(struct vm_fault *vmf)
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
			return VM_FAULT_SIGBUS;
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
	if (vma_pages(vma) != (udev->ring_size >> PAGE_SHIFT))
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

static int tcmu_check_and_free_pending_cmd(struct tcmu_cmd *cmd)
{
	if (test_bit(TCMU_CMD_BIT_EXPIRED, &cmd->flags)) {
		kmem_cache_free(tcmu_cmd_cache, cmd);
		return 0;
	}
	return -EINVAL;
}

static void tcmu_blocks_release(struct radix_tree_root *blocks,
				int start, int end)
{
	int i;
	struct page *page;

	for (i = start; i < end; i++) {
		page = radix_tree_delete(blocks, i);
		if (page) {
			__free_page(page);
			atomic_dec(&global_db_count);
		}
	}
}

static void tcmu_remove_all_queued_tmr(struct tcmu_dev *udev)
{
	struct tcmu_tmr *tmr, *tmp;

	list_for_each_entry_safe(tmr, tmp, &udev->tmr_queue, queue_entry) {
		list_del_init(&tmr->queue_entry);
		kfree(tmr);
	}
}

static void tcmu_dev_kref_release(struct kref *kref)
{
	struct tcmu_dev *udev = container_of(kref, struct tcmu_dev, kref);
	struct se_device *dev = &udev->se_dev;
	struct tcmu_cmd *cmd;
	bool all_expired = true;
	int i;

	vfree(udev->mb_addr);
	udev->mb_addr = NULL;

	spin_lock_bh(&timed_out_udevs_lock);
	if (!list_empty(&udev->timedout_entry))
		list_del(&udev->timedout_entry);
	spin_unlock_bh(&timed_out_udevs_lock);

	/* Upper layer should drain all requests before calling this */
	mutex_lock(&udev->cmdr_lock);
	idr_for_each_entry(&udev->commands, cmd, i) {
		if (tcmu_check_and_free_pending_cmd(cmd) != 0)
			all_expired = false;
	}
	/* There can be left over TMR cmds. Remove them. */
	tcmu_remove_all_queued_tmr(udev);
	if (!list_empty(&udev->qfull_queue))
		all_expired = false;
	idr_destroy(&udev->commands);
	WARN_ON(!all_expired);

	tcmu_blocks_release(&udev->data_blocks, 0, udev->dbi_max + 1);
	bitmap_free(udev->data_bitmap);
	mutex_unlock(&udev->cmdr_lock);

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

static int tcmu_init_genl_cmd_reply(struct tcmu_dev *udev, int cmd)
{
	struct tcmu_nl_cmd *nl_cmd = &udev->curr_nl_cmd;

	if (!tcmu_kern_cmd_reply_supported)
		return 0;

	if (udev->nl_reply_supported <= 0)
		return 0;

	mutex_lock(&tcmu_nl_cmd_mutex);

	if (tcmu_netlink_blocked) {
		mutex_unlock(&tcmu_nl_cmd_mutex);
		pr_warn("Failing nl cmd %d on %s. Interface is blocked.\n", cmd,
			udev->name);
		return -EAGAIN;
	}

	if (nl_cmd->cmd != TCMU_CMD_UNSPEC) {
		mutex_unlock(&tcmu_nl_cmd_mutex);
		pr_warn("netlink cmd %d already executing on %s\n",
			 nl_cmd->cmd, udev->name);
		return -EBUSY;
	}

	memset(nl_cmd, 0, sizeof(*nl_cmd));
	nl_cmd->cmd = cmd;
	nl_cmd->udev = udev;
	init_completion(&nl_cmd->complete);
	INIT_LIST_HEAD(&nl_cmd->nl_list);

	list_add_tail(&nl_cmd->nl_list, &tcmu_nl_cmd_list);

	mutex_unlock(&tcmu_nl_cmd_mutex);
	return 0;
}

static void tcmu_destroy_genl_cmd_reply(struct tcmu_dev *udev)
{
	struct tcmu_nl_cmd *nl_cmd = &udev->curr_nl_cmd;

	if (!tcmu_kern_cmd_reply_supported)
		return;

	if (udev->nl_reply_supported <= 0)
		return;

	mutex_lock(&tcmu_nl_cmd_mutex);

	list_del(&nl_cmd->nl_list);
	memset(nl_cmd, 0, sizeof(*nl_cmd));

	mutex_unlock(&tcmu_nl_cmd_mutex);
}

static int tcmu_wait_genl_cmd_reply(struct tcmu_dev *udev)
{
	struct tcmu_nl_cmd *nl_cmd = &udev->curr_nl_cmd;
	int ret;

	if (!tcmu_kern_cmd_reply_supported)
		return 0;

	if (udev->nl_reply_supported <= 0)
		return 0;

	pr_debug("sleeping for nl reply\n");
	wait_for_completion(&nl_cmd->complete);

	mutex_lock(&tcmu_nl_cmd_mutex);
	nl_cmd->cmd = TCMU_CMD_UNSPEC;
	ret = nl_cmd->status;
	mutex_unlock(&tcmu_nl_cmd_mutex);

	return ret;
}

static int tcmu_netlink_event_init(struct tcmu_dev *udev,
				   enum tcmu_genl_cmd cmd,
				   struct sk_buff **buf, void **hdr)
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

	*buf = skb;
	*hdr = msg_header;
	return ret;

free_skb:
	nlmsg_free(skb);
	return ret;
}

static int tcmu_netlink_event_send(struct tcmu_dev *udev,
				   enum tcmu_genl_cmd cmd,
				   struct sk_buff *skb, void *msg_header)
{
	int ret;

	genlmsg_end(skb, msg_header);

	ret = tcmu_init_genl_cmd_reply(udev, cmd);
	if (ret) {
		nlmsg_free(skb);
		return ret;
	}

	ret = genlmsg_multicast_allns(&tcmu_genl_family, skb, 0,
				      TCMU_MCGRP_CONFIG, GFP_KERNEL);

	/* Wait during an add as the listener may not be up yet */
	if (ret == 0 ||
	   (ret == -ESRCH && cmd == TCMU_CMD_ADDED_DEVICE))
		return tcmu_wait_genl_cmd_reply(udev);
	else
		tcmu_destroy_genl_cmd_reply(udev);

	return ret;
}

static int tcmu_send_dev_add_event(struct tcmu_dev *udev)
{
	struct sk_buff *skb = NULL;
	void *msg_header = NULL;
	int ret = 0;

	ret = tcmu_netlink_event_init(udev, TCMU_CMD_ADDED_DEVICE, &skb,
				      &msg_header);
	if (ret < 0)
		return ret;
	return tcmu_netlink_event_send(udev, TCMU_CMD_ADDED_DEVICE, skb,
				       msg_header);
}

static int tcmu_send_dev_remove_event(struct tcmu_dev *udev)
{
	struct sk_buff *skb = NULL;
	void *msg_header = NULL;
	int ret = 0;

	ret = tcmu_netlink_event_init(udev, TCMU_CMD_REMOVED_DEVICE,
				      &skb, &msg_header);
	if (ret < 0)
		return ret;
	return tcmu_netlink_event_send(udev, TCMU_CMD_REMOVED_DEVICE,
				       skb, msg_header);
}

static int tcmu_update_uio_info(struct tcmu_dev *udev)
{
	struct tcmu_hba *hba = udev->hba->hba_ptr;
	struct uio_info *info;
	char *str;

	info = &udev->uio_info;

	if (udev->dev_config[0])
		str = kasprintf(GFP_KERNEL, "tcm-user/%u/%s/%s", hba->host_id,
				udev->name, udev->dev_config);
	else
		str = kasprintf(GFP_KERNEL, "tcm-user/%u/%s", hba->host_id,
				udev->name);
	if (!str)
		return -ENOMEM;

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

	mutex_lock(&udev->cmdr_lock);
	udev->data_bitmap = bitmap_zalloc(udev->max_blocks, GFP_KERNEL);
	mutex_unlock(&udev->cmdr_lock);
	if (!udev->data_bitmap) {
		ret = -ENOMEM;
		goto err_bitmap_alloc;
	}

	udev->mb_addr = vzalloc(CMDR_SIZE);
	if (!udev->mb_addr) {
		ret = -ENOMEM;
		goto err_vzalloc;
	}

	/* mailbox fits in first part of CMDR space */
	udev->cmdr_size = CMDR_SIZE - CMDR_OFF;
	udev->data_off = CMDR_SIZE;
	udev->data_size = udev->max_blocks * DATA_BLOCK_SIZE;
	udev->dbi_thresh = 0; /* Default in Idle state */

	/* Initialise the mailbox of the ring buffer */
	mb = udev->mb_addr;
	mb->version = TCMU_MAILBOX_VERSION;
	mb->flags = TCMU_MAILBOX_FLAG_CAP_OOOC |
		    TCMU_MAILBOX_FLAG_CAP_READ_LEN |
		    TCMU_MAILBOX_FLAG_CAP_TMR;
	mb->cmdr_off = CMDR_OFF;
	mb->cmdr_size = udev->cmdr_size;

	WARN_ON(!PAGE_ALIGNED(udev->data_off));
	WARN_ON(udev->data_size % PAGE_SIZE);
	WARN_ON(udev->data_size % DATA_BLOCK_SIZE);

	info->version = __stringify(TCMU_MAILBOX_VERSION);

	info->mem[0].name = "tcm-user command & data buffer";
	info->mem[0].addr = (phys_addr_t)(uintptr_t)udev->mb_addr;
	info->mem[0].size = udev->ring_size = udev->data_size + CMDR_SIZE;
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

	/* If user didn't explicitly disable netlink reply support, use
	 * module scope setting.
	 */
	if (udev->nl_reply_supported >= 0)
		udev->nl_reply_supported = tcmu_kern_cmd_reply_supported;

	/*
	 * Get a ref incase userspace does a close on the uio device before
	 * LIO has initiated tcmu_free_device.
	 */
	kref_get(&udev->kref);

	ret = tcmu_send_dev_add_event(udev);
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
	udev->mb_addr = NULL;
err_vzalloc:
	bitmap_free(udev->data_bitmap);
	udev->data_bitmap = NULL;
err_bitmap_alloc:
	kfree(info->name);
	info->name = NULL;

	return ret;
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

	del_timer_sync(&udev->cmd_timer);
	del_timer_sync(&udev->qfull_timer);

	mutex_lock(&root_udev_mutex);
	list_del(&udev->node);
	mutex_unlock(&root_udev_mutex);

	tcmu_send_dev_remove_event(udev);

	uio_unregister_device(&udev->uio_info);

	/* release ref from configure */
	kref_put(&udev->kref, tcmu_dev_kref_release);
}

static void tcmu_unblock_dev(struct tcmu_dev *udev)
{
	mutex_lock(&udev->cmdr_lock);
	clear_bit(TCMU_DEV_BIT_BLOCKED, &udev->flags);
	mutex_unlock(&udev->cmdr_lock);
}

static void tcmu_block_dev(struct tcmu_dev *udev)
{
	mutex_lock(&udev->cmdr_lock);

	if (test_and_set_bit(TCMU_DEV_BIT_BLOCKED, &udev->flags))
		goto unlock;

	/* complete IO that has executed successfully */
	tcmu_handle_completions(udev);
	/* fail IO waiting to be queued */
	run_qfull_queue(udev, true);

unlock:
	mutex_unlock(&udev->cmdr_lock);
}

static void tcmu_reset_ring(struct tcmu_dev *udev, u8 err_level)
{
	struct tcmu_mailbox *mb;
	struct tcmu_cmd *cmd;
	int i;

	mutex_lock(&udev->cmdr_lock);

	idr_for_each_entry(&udev->commands, cmd, i) {
		pr_debug("removing cmd %u on dev %s from ring (is expired %d)\n",
			  cmd->cmd_id, udev->name,
			  test_bit(TCMU_CMD_BIT_EXPIRED, &cmd->flags));

		idr_remove(&udev->commands, i);
		if (!test_bit(TCMU_CMD_BIT_EXPIRED, &cmd->flags)) {
			WARN_ON(!cmd->se_cmd);
			list_del_init(&cmd->queue_entry);
			if (err_level == 1) {
				/*
				 * Userspace was not able to start the
				 * command or it is retryable.
				 */
				target_complete_cmd(cmd->se_cmd, SAM_STAT_BUSY);
			} else {
				/* hard failure */
				target_complete_cmd(cmd->se_cmd,
						    SAM_STAT_CHECK_CONDITION);
			}
		}
		tcmu_cmd_free_data(cmd, cmd->dbi_cnt);
		tcmu_free_cmd(cmd);
	}

	mb = udev->mb_addr;
	tcmu_flush_dcache_range(mb, sizeof(*mb));
	pr_debug("mb last %u head %u tail %u\n", udev->cmdr_last_cleaned,
		 mb->cmd_tail, mb->cmd_head);

	udev->cmdr_last_cleaned = 0;
	mb->cmd_tail = 0;
	mb->cmd_head = 0;
	tcmu_flush_dcache_range(mb, sizeof(*mb));
	clear_bit(TCMU_DEV_BIT_BROKEN, &udev->flags);

	del_timer(&udev->cmd_timer);

	/*
	 * ring is empty and qfull queue never contains aborted commands.
	 * So TMRs in tmr queue do not contain relevant cmd_ids.
	 * After a ring reset userspace should do a fresh start, so
	 * even LUN RESET message is no longer relevant.
	 * Therefore remove all TMRs from qfull queue
	 */
	tcmu_remove_all_queued_tmr(udev);

	run_qfull_queue(udev, false);

	mutex_unlock(&udev->cmdr_lock);
}

enum {
	Opt_dev_config, Opt_dev_size, Opt_hw_block_size, Opt_hw_max_sectors,
	Opt_nl_reply_supported, Opt_max_data_area_mb, Opt_err,
};

static match_table_t tokens = {
	{Opt_dev_config, "dev_config=%s"},
	{Opt_dev_size, "dev_size=%s"},
	{Opt_hw_block_size, "hw_block_size=%d"},
	{Opt_hw_max_sectors, "hw_max_sectors=%d"},
	{Opt_nl_reply_supported, "nl_reply_supported=%d"},
	{Opt_max_data_area_mb, "max_data_area_mb=%d"},
	{Opt_err, NULL}
};

static int tcmu_set_dev_attrib(substring_t *arg, u32 *dev_attrib)
{
	int val, ret;

	ret = match_int(arg, &val);
	if (ret < 0) {
		pr_err("match_int() failed for dev attrib. Error %d.\n",
		       ret);
		return ret;
	}

	if (val <= 0) {
		pr_err("Invalid dev attrib value %d. Must be greater than zero.\n",
		       val);
		return -EINVAL;
	}
	*dev_attrib = val;
	return 0;
}

static int tcmu_set_max_blocks_param(struct tcmu_dev *udev, substring_t *arg)
{
	int val, ret;

	ret = match_int(arg, &val);
	if (ret < 0) {
		pr_err("match_int() failed for max_data_area_mb=. Error %d.\n",
		       ret);
		return ret;
	}

	if (val <= 0) {
		pr_err("Invalid max_data_area %d.\n", val);
		return -EINVAL;
	}

	mutex_lock(&udev->cmdr_lock);
	if (udev->data_bitmap) {
		pr_err("Cannot set max_data_area_mb after it has been enabled.\n");
		ret = -EINVAL;
		goto unlock;
	}

	udev->max_blocks = TCMU_MBS_TO_BLOCKS(val);
	if (udev->max_blocks > tcmu_global_max_blocks) {
		pr_err("%d is too large. Adjusting max_data_area_mb to global limit of %u\n",
		       val, TCMU_BLOCKS_TO_MBS(tcmu_global_max_blocks));
		udev->max_blocks = tcmu_global_max_blocks;
	}

unlock:
	mutex_unlock(&udev->cmdr_lock);
	return ret;
}

static ssize_t tcmu_set_configfs_dev_params(struct se_device *dev,
		const char *page, ssize_t count)
{
	struct tcmu_dev *udev = TCMU_DEV(dev);
	char *orig, *ptr, *opts;
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
			ret = match_u64(&args[0], &udev->dev_size);
			if (ret < 0)
				pr_err("match_u64() failed for dev_size=. Error %d.\n",
				       ret);
			break;
		case Opt_hw_block_size:
			ret = tcmu_set_dev_attrib(&args[0],
					&(dev->dev_attrib.hw_block_size));
			break;
		case Opt_hw_max_sectors:
			ret = tcmu_set_dev_attrib(&args[0],
					&(dev->dev_attrib.hw_max_sectors));
			break;
		case Opt_nl_reply_supported:
			ret = match_int(&args[0], &udev->nl_reply_supported);
			if (ret < 0)
				pr_err("match_int() failed for nl_reply_supported=. Error %d.\n",
				       ret);
			break;
		case Opt_max_data_area_mb:
			ret = tcmu_set_max_blocks_param(udev, &args[0]);
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
	bl += sprintf(b + bl, "Size: %llu ", udev->dev_size);
	bl += sprintf(b + bl, "MaxDataAreaMB: %u\n",
		      TCMU_BLOCKS_TO_MBS(udev->max_blocks));

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
	struct tcmu_dev *udev = TCMU_DEV(da->da_dev);

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

static ssize_t tcmu_qfull_time_out_show(struct config_item *item, char *page)
{
	struct se_dev_attrib *da = container_of(to_config_group(item),
						struct se_dev_attrib, da_group);
	struct tcmu_dev *udev = TCMU_DEV(da->da_dev);

	return snprintf(page, PAGE_SIZE, "%ld\n", udev->qfull_time_out <= 0 ?
			udev->qfull_time_out :
			udev->qfull_time_out / MSEC_PER_SEC);
}

static ssize_t tcmu_qfull_time_out_store(struct config_item *item,
					 const char *page, size_t count)
{
	struct se_dev_attrib *da = container_of(to_config_group(item),
					struct se_dev_attrib, da_group);
	struct tcmu_dev *udev = TCMU_DEV(da->da_dev);
	s32 val;
	int ret;

	ret = kstrtos32(page, 0, &val);
	if (ret < 0)
		return ret;

	if (val >= 0) {
		udev->qfull_time_out = val * MSEC_PER_SEC;
	} else if (val == -1) {
		udev->qfull_time_out = val;
	} else {
		printk(KERN_ERR "Invalid qfull timeout value %d\n", val);
		return -EINVAL;
	}
	return count;
}
CONFIGFS_ATTR(tcmu_, qfull_time_out);

static ssize_t tcmu_max_data_area_mb_show(struct config_item *item, char *page)
{
	struct se_dev_attrib *da = container_of(to_config_group(item),
						struct se_dev_attrib, da_group);
	struct tcmu_dev *udev = TCMU_DEV(da->da_dev);

	return snprintf(page, PAGE_SIZE, "%u\n",
			TCMU_BLOCKS_TO_MBS(udev->max_blocks));
}
CONFIGFS_ATTR_RO(tcmu_, max_data_area_mb);

static ssize_t tcmu_dev_config_show(struct config_item *item, char *page)
{
	struct se_dev_attrib *da = container_of(to_config_group(item),
						struct se_dev_attrib, da_group);
	struct tcmu_dev *udev = TCMU_DEV(da->da_dev);

	return snprintf(page, PAGE_SIZE, "%s\n", udev->dev_config);
}

static int tcmu_send_dev_config_event(struct tcmu_dev *udev,
				      const char *reconfig_data)
{
	struct sk_buff *skb = NULL;
	void *msg_header = NULL;
	int ret = 0;

	ret = tcmu_netlink_event_init(udev, TCMU_CMD_RECONFIG_DEVICE,
				      &skb, &msg_header);
	if (ret < 0)
		return ret;
	ret = nla_put_string(skb, TCMU_ATTR_DEV_CFG, reconfig_data);
	if (ret < 0) {
		nlmsg_free(skb);
		return ret;
	}
	return tcmu_netlink_event_send(udev, TCMU_CMD_RECONFIG_DEVICE,
				       skb, msg_header);
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
	if (target_dev_configured(&udev->se_dev)) {
		ret = tcmu_send_dev_config_event(udev, page);
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

	return snprintf(page, PAGE_SIZE, "%llu\n", udev->dev_size);
}

static int tcmu_send_dev_size_event(struct tcmu_dev *udev, u64 size)
{
	struct sk_buff *skb = NULL;
	void *msg_header = NULL;
	int ret = 0;

	ret = tcmu_netlink_event_init(udev, TCMU_CMD_RECONFIG_DEVICE,
				      &skb, &msg_header);
	if (ret < 0)
		return ret;
	ret = nla_put_u64_64bit(skb, TCMU_ATTR_DEV_SIZE,
				size, TCMU_ATTR_PAD);
	if (ret < 0) {
		nlmsg_free(skb);
		return ret;
	}
	return tcmu_netlink_event_send(udev, TCMU_CMD_RECONFIG_DEVICE,
				       skb, msg_header);
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
	if (target_dev_configured(&udev->se_dev)) {
		ret = tcmu_send_dev_size_event(udev, val);
		if (ret) {
			pr_err("Unable to reconfigure device\n");
			return ret;
		}
	}
	udev->dev_size = val;
	return count;
}
CONFIGFS_ATTR(tcmu_, dev_size);

static ssize_t tcmu_nl_reply_supported_show(struct config_item *item,
		char *page)
{
	struct se_dev_attrib *da = container_of(to_config_group(item),
						struct se_dev_attrib, da_group);
	struct tcmu_dev *udev = TCMU_DEV(da->da_dev);

	return snprintf(page, PAGE_SIZE, "%d\n", udev->nl_reply_supported);
}

static ssize_t tcmu_nl_reply_supported_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_dev_attrib *da = container_of(to_config_group(item),
						struct se_dev_attrib, da_group);
	struct tcmu_dev *udev = TCMU_DEV(da->da_dev);
	s8 val;
	int ret;

	ret = kstrtos8(page, 0, &val);
	if (ret < 0)
		return ret;

	udev->nl_reply_supported = val;
	return count;
}
CONFIGFS_ATTR(tcmu_, nl_reply_supported);

static ssize_t tcmu_emulate_write_cache_show(struct config_item *item,
					     char *page)
{
	struct se_dev_attrib *da = container_of(to_config_group(item),
					struct se_dev_attrib, da_group);

	return snprintf(page, PAGE_SIZE, "%i\n", da->emulate_write_cache);
}

static int tcmu_send_emulate_write_cache(struct tcmu_dev *udev, u8 val)
{
	struct sk_buff *skb = NULL;
	void *msg_header = NULL;
	int ret = 0;

	ret = tcmu_netlink_event_init(udev, TCMU_CMD_RECONFIG_DEVICE,
				      &skb, &msg_header);
	if (ret < 0)
		return ret;
	ret = nla_put_u8(skb, TCMU_ATTR_WRITECACHE, val);
	if (ret < 0) {
		nlmsg_free(skb);
		return ret;
	}
	return tcmu_netlink_event_send(udev, TCMU_CMD_RECONFIG_DEVICE,
				       skb, msg_header);
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
	if (target_dev_configured(&udev->se_dev)) {
		ret = tcmu_send_emulate_write_cache(udev, val);
		if (ret) {
			pr_err("Unable to reconfigure device\n");
			return ret;
		}
	}

	da->emulate_write_cache = val;
	return count;
}
CONFIGFS_ATTR(tcmu_, emulate_write_cache);

static ssize_t tcmu_tmr_notification_show(struct config_item *item, char *page)
{
	struct se_dev_attrib *da = container_of(to_config_group(item),
					struct se_dev_attrib, da_group);
	struct tcmu_dev *udev = TCMU_DEV(da->da_dev);

	return snprintf(page, PAGE_SIZE, "%i\n",
			test_bit(TCMU_DEV_BIT_TMR_NOTIFY, &udev->flags));
}

static ssize_t tcmu_tmr_notification_store(struct config_item *item,
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
	if (val > 1)
		return -EINVAL;

	if (val)
		set_bit(TCMU_DEV_BIT_TMR_NOTIFY, &udev->flags);
	else
		clear_bit(TCMU_DEV_BIT_TMR_NOTIFY, &udev->flags);
	return count;
}
CONFIGFS_ATTR(tcmu_, tmr_notification);

static ssize_t tcmu_block_dev_show(struct config_item *item, char *page)
{
	struct se_device *se_dev = container_of(to_config_group(item),
						struct se_device,
						dev_action_group);
	struct tcmu_dev *udev = TCMU_DEV(se_dev);

	if (test_bit(TCMU_DEV_BIT_BLOCKED, &udev->flags))
		return snprintf(page, PAGE_SIZE, "%s\n", "blocked");
	else
		return snprintf(page, PAGE_SIZE, "%s\n", "unblocked");
}

static ssize_t tcmu_block_dev_store(struct config_item *item, const char *page,
				    size_t count)
{
	struct se_device *se_dev = container_of(to_config_group(item),
						struct se_device,
						dev_action_group);
	struct tcmu_dev *udev = TCMU_DEV(se_dev);
	u8 val;
	int ret;

	if (!target_dev_configured(&udev->se_dev)) {
		pr_err("Device is not configured.\n");
		return -EINVAL;
	}

	ret = kstrtou8(page, 0, &val);
	if (ret < 0)
		return ret;

	if (val > 1) {
		pr_err("Invalid block value %d\n", val);
		return -EINVAL;
	}

	if (!val)
		tcmu_unblock_dev(udev);
	else
		tcmu_block_dev(udev);
	return count;
}
CONFIGFS_ATTR(tcmu_, block_dev);

static ssize_t tcmu_reset_ring_store(struct config_item *item, const char *page,
				     size_t count)
{
	struct se_device *se_dev = container_of(to_config_group(item),
						struct se_device,
						dev_action_group);
	struct tcmu_dev *udev = TCMU_DEV(se_dev);
	u8 val;
	int ret;

	if (!target_dev_configured(&udev->se_dev)) {
		pr_err("Device is not configured.\n");
		return -EINVAL;
	}

	ret = kstrtou8(page, 0, &val);
	if (ret < 0)
		return ret;

	if (val != 1 && val != 2) {
		pr_err("Invalid reset ring value %d\n", val);
		return -EINVAL;
	}

	tcmu_reset_ring(udev, val);
	return count;
}
CONFIGFS_ATTR_WO(tcmu_, reset_ring);

static struct configfs_attribute *tcmu_attrib_attrs[] = {
	&tcmu_attr_cmd_time_out,
	&tcmu_attr_qfull_time_out,
	&tcmu_attr_max_data_area_mb,
	&tcmu_attr_dev_config,
	&tcmu_attr_dev_size,
	&tcmu_attr_emulate_write_cache,
	&tcmu_attr_tmr_notification,
	&tcmu_attr_nl_reply_supported,
	NULL,
};

static struct configfs_attribute **tcmu_attrs;

static struct configfs_attribute *tcmu_action_attrs[] = {
	&tcmu_attr_block_dev,
	&tcmu_attr_reset_ring,
	NULL,
};

static struct target_backend_ops tcmu_ops = {
	.name			= "user",
	.owner			= THIS_MODULE,
	.transport_flags_default = TRANSPORT_FLAG_PASSTHROUGH,
	.transport_flags_changeable = TRANSPORT_FLAG_PASSTHROUGH_PGR |
				      TRANSPORT_FLAG_PASSTHROUGH_ALUA,
	.attach_hba		= tcmu_attach_hba,
	.detach_hba		= tcmu_detach_hba,
	.alloc_device		= tcmu_alloc_device,
	.configure_device	= tcmu_configure_device,
	.destroy_device		= tcmu_destroy_device,
	.free_device		= tcmu_free_device,
	.parse_cdb		= tcmu_parse_cdb,
	.tmr_notify		= tcmu_tmr_notify,
	.set_configfs_dev_params = tcmu_set_configfs_dev_params,
	.show_configfs_dev_params = tcmu_show_configfs_dev_params,
	.get_device_type	= sbc_get_device_type,
	.get_blocks		= tcmu_get_blocks,
	.tb_dev_action_attrs	= tcmu_action_attrs,
};

static void find_free_blocks(void)
{
	struct tcmu_dev *udev;
	loff_t off;
	u32 start, end, block, total_freed = 0;

	if (atomic_read(&global_db_count) <= tcmu_global_max_blocks)
		return;

	mutex_lock(&root_udev_mutex);
	list_for_each_entry(udev, &root_udev, node) {
		mutex_lock(&udev->cmdr_lock);

		if (!target_dev_configured(&udev->se_dev)) {
			mutex_unlock(&udev->cmdr_lock);
			continue;
		}

		/* Try to complete the finished commands first */
		if (tcmu_handle_completions(udev))
			run_qfull_queue(udev, false);

		/* Skip the udevs in idle */
		if (!udev->dbi_thresh) {
			mutex_unlock(&udev->cmdr_lock);
			continue;
		}

		end = udev->dbi_max + 1;
		block = find_last_bit(udev->data_bitmap, end);
		if (block == udev->dbi_max) {
			/*
			 * The last bit is dbi_max, so it is not possible
			 * reclaim any blocks.
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
		tcmu_blocks_release(&udev->data_blocks, start, end);
		mutex_unlock(&udev->cmdr_lock);

		total_freed += end - start;
		pr_debug("Freed %u blocks (total %u) from %s.\n", end - start,
			 total_freed, udev->name);
	}
	mutex_unlock(&root_udev_mutex);

	if (atomic_read(&global_db_count) > tcmu_global_max_blocks)
		schedule_delayed_work(&tcmu_unmap_work, msecs_to_jiffies(5000));
}

static void check_timedout_devices(void)
{
	struct tcmu_dev *udev, *tmp_dev;
	struct tcmu_cmd *cmd, *tmp_cmd;
	LIST_HEAD(devs);

	spin_lock_bh(&timed_out_udevs_lock);
	list_splice_init(&timed_out_udevs, &devs);

	list_for_each_entry_safe(udev, tmp_dev, &devs, timedout_entry) {
		list_del_init(&udev->timedout_entry);
		spin_unlock_bh(&timed_out_udevs_lock);

		mutex_lock(&udev->cmdr_lock);

		/*
		 * If cmd_time_out is disabled but qfull is set deadline
		 * will only reflect the qfull timeout. Ignore it.
		 */
		if (udev->cmd_time_out) {
			list_for_each_entry_safe(cmd, tmp_cmd,
						 &udev->inflight_queue,
						 queue_entry) {
				tcmu_check_expired_ring_cmd(cmd);
			}
			tcmu_set_next_deadline(&udev->inflight_queue,
					       &udev->cmd_timer);
		}
		list_for_each_entry_safe(cmd, tmp_cmd, &udev->qfull_queue,
					 queue_entry) {
			tcmu_check_expired_queue_cmd(cmd);
		}
		tcmu_set_next_deadline(&udev->qfull_queue, &udev->qfull_timer);

		mutex_unlock(&udev->cmdr_lock);

		spin_lock_bh(&timed_out_udevs_lock);
	}

	spin_unlock_bh(&timed_out_udevs_lock);
}

static void tcmu_unmap_work_fn(struct work_struct *work)
{
	check_timedout_devices();
	find_free_blocks();
}

static int __init tcmu_module_init(void)
{
	int ret, i, k, len = 0;

	BUILD_BUG_ON((sizeof(struct tcmu_cmd_entry) % TCMU_OP_ALIGN_SIZE) != 0);

	INIT_DELAYED_WORK(&tcmu_unmap_work, tcmu_unmap_work_fn);

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

	for (i = 0; passthrough_attrib_attrs[i] != NULL; i++)
		len += sizeof(struct configfs_attribute *);
	for (i = 0; passthrough_pr_attrib_attrs[i] != NULL; i++)
		len += sizeof(struct configfs_attribute *);
	for (i = 0; tcmu_attrib_attrs[i] != NULL; i++)
		len += sizeof(struct configfs_attribute *);
	len += sizeof(struct configfs_attribute *);

	tcmu_attrs = kzalloc(len, GFP_KERNEL);
	if (!tcmu_attrs) {
		ret = -ENOMEM;
		goto out_unreg_genl;
	}

	for (i = 0; passthrough_attrib_attrs[i] != NULL; i++)
		tcmu_attrs[i] = passthrough_attrib_attrs[i];
	for (k = 0; passthrough_pr_attrib_attrs[k] != NULL; k++)
		tcmu_attrs[i++] = passthrough_pr_attrib_attrs[k];
	for (k = 0; tcmu_attrib_attrs[k] != NULL; k++)
		tcmu_attrs[i++] = tcmu_attrib_attrs[k];
	tcmu_ops.tb_dev_attrib_attrs = tcmu_attrs;

	ret = transport_backend_register(&tcmu_ops);
	if (ret)
		goto out_attrs;

	return 0;

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
	cancel_delayed_work_sync(&tcmu_unmap_work);
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
