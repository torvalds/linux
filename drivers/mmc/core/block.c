// SPDX-License-Identifier: GPL-2.0
/*
 * Block driver for media (i.e., flash cards)
 *
 * Copyright 2002 Hewlett-Packard Company
 * Copyright 2005-2008 Pierre Ossman
 *
 * Use consistent with the GNU GPL is permitted,
 * provided that this copyright notice is
 * preserved in its entirety in all copies and derived works.
 *
 * HEWLETT-PACKARD COMPANY MAKES NO WARRANTIES, EXPRESSED OR IMPLIED,
 * AS TO THE USEFULNESS OR CORRECTNESS OF THIS CODE OR ITS
 * FITNESS FOR ANY PARTICULAR PURPOSE.
 *
 * Many thanks to Alessandro Rubini and Jonathan Corbet!
 *
 * Author:  Andrew Christian
 *          28 May 2002
 */
#include <linux/moduleparam.h>
#include <linux/module.h>
#include <linux/init.h>

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include <linux/kdev_t.h>
#include <linux/kref.h>
#include <linux/blkdev.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>
#include <linux/string_helpers.h>
#include <linux/delay.h>
#include <linux/capability.h>
#include <linux/compat.h>
#include <linux/pm_runtime.h>
#include <linux/idr.h>
#include <linux/debugfs.h>

#include <linux/mmc/ioctl.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>

#include <linux/uaccess.h>

#include "queue.h"
#include "block.h"
#include "core.h"
#include "card.h"
#include "crypto.h"
#include "host.h"
#include "bus.h"
#include "mmc_ops.h"
#include "quirks.h"
#include "sd_ops.h"

MODULE_ALIAS("mmc:block");
#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "mmcblk."

/*
 * Set a 10 second timeout for polling write request busy state. Note, mmc core
 * is setting a 3 second timeout for SD cards, and SDHCI has long had a 10
 * second software timer to timeout the whole request, so 10 seconds should be
 * ample.
 */
#define MMC_BLK_TIMEOUT_MS  (10 * 1000)
#define MMC_EXTRACT_INDEX_FROM_ARG(x) ((x & 0x00FF0000) >> 16)
#define MMC_EXTRACT_VALUE_FROM_ARG(x) ((x & 0x0000FF00) >> 8)

#define mmc_req_rel_wr(req)	((req->cmd_flags & REQ_FUA) && \
				  (rq_data_dir(req) == WRITE))
static DEFINE_MUTEX(block_mutex);

/*
 * The defaults come from config options but can be overriden by module
 * or bootarg options.
 */
static int perdev_minors = CONFIG_MMC_BLOCK_MINORS;

/*
 * We've only got one major, so number of mmcblk devices is
 * limited to (1 << 20) / number of minors per device.  It is also
 * limited by the MAX_DEVICES below.
 */
static int max_devices;

#define MAX_DEVICES 256

static DEFINE_IDA(mmc_blk_ida);
static DEFINE_IDA(mmc_rpmb_ida);

struct mmc_blk_busy_data {
	struct mmc_card *card;
	u32 status;
};

/*
 * There is one mmc_blk_data per slot.
 */
struct mmc_blk_data {
	struct device	*parent;
	struct gendisk	*disk;
	struct mmc_queue queue;
	struct list_head part;
	struct list_head rpmbs;

	unsigned int	flags;
#define MMC_BLK_CMD23	(1 << 0)	/* Can do SET_BLOCK_COUNT for multiblock */
#define MMC_BLK_REL_WR	(1 << 1)	/* MMC Reliable write support */

	struct kref	kref;
	unsigned int	read_only;
	unsigned int	part_type;
	unsigned int	reset_done;
#define MMC_BLK_READ		BIT(0)
#define MMC_BLK_WRITE		BIT(1)
#define MMC_BLK_DISCARD		BIT(2)
#define MMC_BLK_SECDISCARD	BIT(3)
#define MMC_BLK_CQE_RECOVERY	BIT(4)
#define MMC_BLK_TRIM		BIT(5)

	/*
	 * Only set in main mmc_blk_data associated
	 * with mmc_card with dev_set_drvdata, and keeps
	 * track of the current selected device partition.
	 */
	unsigned int	part_curr;
#define MMC_BLK_PART_INVALID	UINT_MAX	/* Unknown partition active */
	int	area_type;

	/* debugfs files (only in main mmc_blk_data) */
	struct dentry *status_dentry;
	struct dentry *ext_csd_dentry;
};

/* Device type for RPMB character devices */
static dev_t mmc_rpmb_devt;

/* Bus type for RPMB character devices */
static struct bus_type mmc_rpmb_bus_type = {
	.name = "mmc_rpmb",
};

/**
 * struct mmc_rpmb_data - special RPMB device type for these areas
 * @dev: the device for the RPMB area
 * @chrdev: character device for the RPMB area
 * @id: unique device ID number
 * @part_index: partition index (0 on first)
 * @md: parent MMC block device
 * @node: list item, so we can put this device on a list
 */
struct mmc_rpmb_data {
	struct device dev;
	struct cdev chrdev;
	int id;
	unsigned int part_index;
	struct mmc_blk_data *md;
	struct list_head node;
};

static DEFINE_MUTEX(open_lock);

module_param(perdev_minors, int, 0444);
MODULE_PARM_DESC(perdev_minors, "Minors numbers to allocate per device");

static inline int mmc_blk_part_switch(struct mmc_card *card,
				      unsigned int part_type);
static void mmc_blk_rw_rq_prep(struct mmc_queue_req *mqrq,
			       struct mmc_card *card,
			       int recovery_mode,
			       struct mmc_queue *mq);
static void mmc_blk_hsq_req_done(struct mmc_request *mrq);

static struct mmc_blk_data *mmc_blk_get(struct gendisk *disk)
{
	struct mmc_blk_data *md;

	mutex_lock(&open_lock);
	md = disk->private_data;
	if (md && !kref_get_unless_zero(&md->kref))
		md = NULL;
	mutex_unlock(&open_lock);

	return md;
}

static inline int mmc_get_devidx(struct gendisk *disk)
{
	int devidx = disk->first_minor / perdev_minors;
	return devidx;
}

static void mmc_blk_kref_release(struct kref *ref)
{
	struct mmc_blk_data *md = container_of(ref, struct mmc_blk_data, kref);
	int devidx;

	devidx = mmc_get_devidx(md->disk);
	ida_simple_remove(&mmc_blk_ida, devidx);

	mutex_lock(&open_lock);
	md->disk->private_data = NULL;
	mutex_unlock(&open_lock);

	put_disk(md->disk);
	kfree(md);
}

static void mmc_blk_put(struct mmc_blk_data *md)
{
	kref_put(&md->kref, mmc_blk_kref_release);
}

static ssize_t power_ro_lock_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	struct mmc_blk_data *md = mmc_blk_get(dev_to_disk(dev));
	struct mmc_card *card = md->queue.card;
	int locked = 0;

	if (card->ext_csd.boot_ro_lock & EXT_CSD_BOOT_WP_B_PERM_WP_EN)
		locked = 2;
	else if (card->ext_csd.boot_ro_lock & EXT_CSD_BOOT_WP_B_PWR_WP_EN)
		locked = 1;

	ret = snprintf(buf, PAGE_SIZE, "%d\n", locked);

	mmc_blk_put(md);

	return ret;
}

static ssize_t power_ro_lock_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	struct mmc_blk_data *md, *part_md;
	struct mmc_queue *mq;
	struct request *req;
	unsigned long set;

	if (kstrtoul(buf, 0, &set))
		return -EINVAL;

	if (set != 1)
		return count;

	md = mmc_blk_get(dev_to_disk(dev));
	mq = &md->queue;

	/* Dispatch locking to the block layer */
	req = blk_mq_alloc_request(mq->queue, REQ_OP_DRV_OUT, 0);
	if (IS_ERR(req)) {
		count = PTR_ERR(req);
		goto out_put;
	}
	req_to_mmc_queue_req(req)->drv_op = MMC_DRV_OP_BOOT_WP;
	blk_execute_rq(req, false);
	ret = req_to_mmc_queue_req(req)->drv_op_result;
	blk_mq_free_request(req);

	if (!ret) {
		pr_info("%s: Locking boot partition ro until next power on\n",
			md->disk->disk_name);
		set_disk_ro(md->disk, 1);

		list_for_each_entry(part_md, &md->part, part)
			if (part_md->area_type == MMC_BLK_DATA_AREA_BOOT) {
				pr_info("%s: Locking boot partition ro until next power on\n", part_md->disk->disk_name);
				set_disk_ro(part_md->disk, 1);
			}
	}
out_put:
	mmc_blk_put(md);
	return count;
}

static DEVICE_ATTR(ro_lock_until_next_power_on, 0,
		power_ro_lock_show, power_ro_lock_store);

static ssize_t force_ro_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	int ret;
	struct mmc_blk_data *md = mmc_blk_get(dev_to_disk(dev));

	ret = snprintf(buf, PAGE_SIZE, "%d\n",
		       get_disk_ro(dev_to_disk(dev)) ^
		       md->read_only);
	mmc_blk_put(md);
	return ret;
}

static ssize_t force_ro_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	int ret;
	char *end;
	struct mmc_blk_data *md = mmc_blk_get(dev_to_disk(dev));
	unsigned long set = simple_strtoul(buf, &end, 0);
	if (end == buf) {
		ret = -EINVAL;
		goto out;
	}

	set_disk_ro(dev_to_disk(dev), set || md->read_only);
	ret = count;
out:
	mmc_blk_put(md);
	return ret;
}

static DEVICE_ATTR(force_ro, 0644, force_ro_show, force_ro_store);

static struct attribute *mmc_disk_attrs[] = {
	&dev_attr_force_ro.attr,
	&dev_attr_ro_lock_until_next_power_on.attr,
	NULL,
};

static umode_t mmc_disk_attrs_is_visible(struct kobject *kobj,
		struct attribute *a, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct mmc_blk_data *md = mmc_blk_get(dev_to_disk(dev));
	umode_t mode = a->mode;

	if (a == &dev_attr_ro_lock_until_next_power_on.attr &&
	    (md->area_type & MMC_BLK_DATA_AREA_BOOT) &&
	    md->queue.card->ext_csd.boot_ro_lockable) {
		mode = S_IRUGO;
		if (!(md->queue.card->ext_csd.boot_ro_lock &
				EXT_CSD_BOOT_WP_B_PWR_WP_DIS))
			mode |= S_IWUSR;
	}

	mmc_blk_put(md);
	return mode;
}

static const struct attribute_group mmc_disk_attr_group = {
	.is_visible	= mmc_disk_attrs_is_visible,
	.attrs		= mmc_disk_attrs,
};

static const struct attribute_group *mmc_disk_attr_groups[] = {
	&mmc_disk_attr_group,
	NULL,
};

static int mmc_blk_open(struct block_device *bdev, fmode_t mode)
{
	struct mmc_blk_data *md = mmc_blk_get(bdev->bd_disk);
	int ret = -ENXIO;

	mutex_lock(&block_mutex);
	if (md) {
		ret = 0;
		if ((mode & FMODE_WRITE) && md->read_only) {
			mmc_blk_put(md);
			ret = -EROFS;
		}
	}
	mutex_unlock(&block_mutex);

	return ret;
}

static void mmc_blk_release(struct gendisk *disk, fmode_t mode)
{
	struct mmc_blk_data *md = disk->private_data;

	mutex_lock(&block_mutex);
	mmc_blk_put(md);
	mutex_unlock(&block_mutex);
}

static int
mmc_blk_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	geo->cylinders = get_capacity(bdev->bd_disk) / (4 * 16);
	geo->heads = 4;
	geo->sectors = 16;
	return 0;
}

struct mmc_blk_ioc_data {
	struct mmc_ioc_cmd ic;
	unsigned char *buf;
	u64 buf_bytes;
	struct mmc_rpmb_data *rpmb;
};

static struct mmc_blk_ioc_data *mmc_blk_ioctl_copy_from_user(
	struct mmc_ioc_cmd __user *user)
{
	struct mmc_blk_ioc_data *idata;
	int err;

	idata = kmalloc(sizeof(*idata), GFP_KERNEL);
	if (!idata) {
		err = -ENOMEM;
		goto out;
	}

	if (copy_from_user(&idata->ic, user, sizeof(idata->ic))) {
		err = -EFAULT;
		goto idata_err;
	}

	idata->buf_bytes = (u64) idata->ic.blksz * idata->ic.blocks;
	if (idata->buf_bytes > MMC_IOC_MAX_BYTES) {
		err = -EOVERFLOW;
		goto idata_err;
	}

	if (!idata->buf_bytes) {
		idata->buf = NULL;
		return idata;
	}

	idata->buf = memdup_user((void __user *)(unsigned long)
				 idata->ic.data_ptr, idata->buf_bytes);
	if (IS_ERR(idata->buf)) {
		err = PTR_ERR(idata->buf);
		goto idata_err;
	}

	return idata;

idata_err:
	kfree(idata);
out:
	return ERR_PTR(err);
}

static int mmc_blk_ioctl_copy_to_user(struct mmc_ioc_cmd __user *ic_ptr,
				      struct mmc_blk_ioc_data *idata)
{
	struct mmc_ioc_cmd *ic = &idata->ic;

	if (copy_to_user(&(ic_ptr->response), ic->response,
			 sizeof(ic->response)))
		return -EFAULT;

	if (!idata->ic.write_flag) {
		if (copy_to_user((void __user *)(unsigned long)ic->data_ptr,
				 idata->buf, idata->buf_bytes))
			return -EFAULT;
	}

	return 0;
}

static int __mmc_blk_ioctl_cmd(struct mmc_card *card, struct mmc_blk_data *md,
			       struct mmc_blk_ioc_data *idata)
{
	struct mmc_command cmd = {}, sbc = {};
	struct mmc_data data = {};
	struct mmc_request mrq = {};
	struct scatterlist sg;
	bool r1b_resp, use_r1b_resp = false;
	unsigned int busy_timeout_ms;
	int err;
	unsigned int target_part;

	if (!card || !md || !idata)
		return -EINVAL;

	/*
	 * The RPMB accesses comes in from the character device, so we
	 * need to target these explicitly. Else we just target the
	 * partition type for the block device the ioctl() was issued
	 * on.
	 */
	if (idata->rpmb) {
		/* Support multiple RPMB partitions */
		target_part = idata->rpmb->part_index;
		target_part |= EXT_CSD_PART_CONFIG_ACC_RPMB;
	} else {
		target_part = md->part_type;
	}

	cmd.opcode = idata->ic.opcode;
	cmd.arg = idata->ic.arg;
	cmd.flags = idata->ic.flags;

	if (idata->buf_bytes) {
		data.sg = &sg;
		data.sg_len = 1;
		data.blksz = idata->ic.blksz;
		data.blocks = idata->ic.blocks;

		sg_init_one(data.sg, idata->buf, idata->buf_bytes);

		if (idata->ic.write_flag)
			data.flags = MMC_DATA_WRITE;
		else
			data.flags = MMC_DATA_READ;

		/* data.flags must already be set before doing this. */
		mmc_set_data_timeout(&data, card);

		/* Allow overriding the timeout_ns for empirical tuning. */
		if (idata->ic.data_timeout_ns)
			data.timeout_ns = idata->ic.data_timeout_ns;

		mrq.data = &data;
	}

	mrq.cmd = &cmd;

	err = mmc_blk_part_switch(card, target_part);
	if (err)
		return err;

	if (idata->ic.is_acmd) {
		err = mmc_app_cmd(card->host, card);
		if (err)
			return err;
	}

	if (idata->rpmb) {
		sbc.opcode = MMC_SET_BLOCK_COUNT;
		/*
		 * We don't do any blockcount validation because the max size
		 * may be increased by a future standard. We just copy the
		 * 'Reliable Write' bit here.
		 */
		sbc.arg = data.blocks | (idata->ic.write_flag & BIT(31));
		sbc.flags = MMC_RSP_R1 | MMC_CMD_AC;
		mrq.sbc = &sbc;
	}

	if ((MMC_EXTRACT_INDEX_FROM_ARG(cmd.arg) == EXT_CSD_SANITIZE_START) &&
	    (cmd.opcode == MMC_SWITCH))
		return mmc_sanitize(card, idata->ic.cmd_timeout_ms);

	/* If it's an R1B response we need some more preparations. */
	busy_timeout_ms = idata->ic.cmd_timeout_ms ? : MMC_BLK_TIMEOUT_MS;
	r1b_resp = (cmd.flags & MMC_RSP_R1B) == MMC_RSP_R1B;
	if (r1b_resp)
		use_r1b_resp = mmc_prepare_busy_cmd(card->host, &cmd,
						    busy_timeout_ms);

	mmc_wait_for_req(card->host, &mrq);
	memcpy(&idata->ic.response, cmd.resp, sizeof(cmd.resp));

	if (cmd.error) {
		dev_err(mmc_dev(card->host), "%s: cmd error %d\n",
						__func__, cmd.error);
		return cmd.error;
	}
	if (data.error) {
		dev_err(mmc_dev(card->host), "%s: data error %d\n",
						__func__, data.error);
		return data.error;
	}

	/*
	 * Make sure the cache of the PARTITION_CONFIG register and
	 * PARTITION_ACCESS bits is updated in case the ioctl ext_csd write
	 * changed it successfully.
	 */
	if ((MMC_EXTRACT_INDEX_FROM_ARG(cmd.arg) == EXT_CSD_PART_CONFIG) &&
	    (cmd.opcode == MMC_SWITCH)) {
		struct mmc_blk_data *main_md = dev_get_drvdata(&card->dev);
		u8 value = MMC_EXTRACT_VALUE_FROM_ARG(cmd.arg);

		/*
		 * Update cache so the next mmc_blk_part_switch call operates
		 * on up-to-date data.
		 */
		card->ext_csd.part_config = value;
		main_md->part_curr = value & EXT_CSD_PART_CONFIG_ACC_MASK;
	}

	/*
	 * Make sure to update CACHE_CTRL in case it was changed. The cache
	 * will get turned back on if the card is re-initialized, e.g.
	 * suspend/resume or hw reset in recovery.
	 */
	if ((MMC_EXTRACT_INDEX_FROM_ARG(cmd.arg) == EXT_CSD_CACHE_CTRL) &&
	    (cmd.opcode == MMC_SWITCH)) {
		u8 value = MMC_EXTRACT_VALUE_FROM_ARG(cmd.arg) & 1;

		card->ext_csd.cache_ctrl = value;
	}

	/*
	 * According to the SD specs, some commands require a delay after
	 * issuing the command.
	 */
	if (idata->ic.postsleep_min_us)
		usleep_range(idata->ic.postsleep_min_us, idata->ic.postsleep_max_us);

	/* No need to poll when using HW busy detection. */
	if ((card->host->caps & MMC_CAP_WAIT_WHILE_BUSY) && use_r1b_resp)
		return 0;

	/* Ensure RPMB/R1B command has completed by polling with CMD13. */
	if (idata->rpmb || r1b_resp)
		err = mmc_poll_for_busy(card, busy_timeout_ms, false,
					MMC_BUSY_IO);

	return err;
}

static int mmc_blk_ioctl_cmd(struct mmc_blk_data *md,
			     struct mmc_ioc_cmd __user *ic_ptr,
			     struct mmc_rpmb_data *rpmb)
{
	struct mmc_blk_ioc_data *idata;
	struct mmc_blk_ioc_data *idatas[1];
	struct mmc_queue *mq;
	struct mmc_card *card;
	int err = 0, ioc_err = 0;
	struct request *req;

	idata = mmc_blk_ioctl_copy_from_user(ic_ptr);
	if (IS_ERR(idata))
		return PTR_ERR(idata);
	/* This will be NULL on non-RPMB ioctl():s */
	idata->rpmb = rpmb;

	card = md->queue.card;
	if (IS_ERR(card)) {
		err = PTR_ERR(card);
		goto cmd_done;
	}

	/*
	 * Dispatch the ioctl() into the block request queue.
	 */
	mq = &md->queue;
	req = blk_mq_alloc_request(mq->queue,
		idata->ic.write_flag ? REQ_OP_DRV_OUT : REQ_OP_DRV_IN, 0);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto cmd_done;
	}
	idatas[0] = idata;
	req_to_mmc_queue_req(req)->drv_op =
		rpmb ? MMC_DRV_OP_IOCTL_RPMB : MMC_DRV_OP_IOCTL;
	req_to_mmc_queue_req(req)->drv_op_data = idatas;
	req_to_mmc_queue_req(req)->ioc_count = 1;
	blk_execute_rq(req, false);
	ioc_err = req_to_mmc_queue_req(req)->drv_op_result;
	err = mmc_blk_ioctl_copy_to_user(ic_ptr, idata);
	blk_mq_free_request(req);

cmd_done:
	kfree(idata->buf);
	kfree(idata);
	return ioc_err ? ioc_err : err;
}

static int mmc_blk_ioctl_multi_cmd(struct mmc_blk_data *md,
				   struct mmc_ioc_multi_cmd __user *user,
				   struct mmc_rpmb_data *rpmb)
{
	struct mmc_blk_ioc_data **idata = NULL;
	struct mmc_ioc_cmd __user *cmds = user->cmds;
	struct mmc_card *card;
	struct mmc_queue *mq;
	int err = 0, ioc_err = 0;
	__u64 num_of_cmds;
	unsigned int i, n;
	struct request *req;

	if (copy_from_user(&num_of_cmds, &user->num_of_cmds,
			   sizeof(num_of_cmds)))
		return -EFAULT;

	if (!num_of_cmds)
		return 0;

	if (num_of_cmds > MMC_IOC_MAX_CMDS)
		return -EINVAL;

	n = num_of_cmds;
	idata = kcalloc(n, sizeof(*idata), GFP_KERNEL);
	if (!idata)
		return -ENOMEM;

	for (i = 0; i < n; i++) {
		idata[i] = mmc_blk_ioctl_copy_from_user(&cmds[i]);
		if (IS_ERR(idata[i])) {
			err = PTR_ERR(idata[i]);
			n = i;
			goto cmd_err;
		}
		/* This will be NULL on non-RPMB ioctl():s */
		idata[i]->rpmb = rpmb;
	}

	card = md->queue.card;
	if (IS_ERR(card)) {
		err = PTR_ERR(card);
		goto cmd_err;
	}


	/*
	 * Dispatch the ioctl()s into the block request queue.
	 */
	mq = &md->queue;
	req = blk_mq_alloc_request(mq->queue,
		idata[0]->ic.write_flag ? REQ_OP_DRV_OUT : REQ_OP_DRV_IN, 0);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto cmd_err;
	}
	req_to_mmc_queue_req(req)->drv_op =
		rpmb ? MMC_DRV_OP_IOCTL_RPMB : MMC_DRV_OP_IOCTL;
	req_to_mmc_queue_req(req)->drv_op_data = idata;
	req_to_mmc_queue_req(req)->ioc_count = n;
	blk_execute_rq(req, false);
	ioc_err = req_to_mmc_queue_req(req)->drv_op_result;

	/* copy to user if data and response */
	for (i = 0; i < n && !err; i++)
		err = mmc_blk_ioctl_copy_to_user(&cmds[i], idata[i]);

	blk_mq_free_request(req);

cmd_err:
	for (i = 0; i < n; i++) {
		kfree(idata[i]->buf);
		kfree(idata[i]);
	}
	kfree(idata);
	return ioc_err ? ioc_err : err;
}

static int mmc_blk_check_blkdev(struct block_device *bdev)
{
	/*
	 * The caller must have CAP_SYS_RAWIO, and must be calling this on the
	 * whole block device, not on a partition.  This prevents overspray
	 * between sibling partitions.
	 */
	if (!capable(CAP_SYS_RAWIO) || bdev_is_partition(bdev))
		return -EPERM;
	return 0;
}

static int mmc_blk_ioctl(struct block_device *bdev, fmode_t mode,
	unsigned int cmd, unsigned long arg)
{
	struct mmc_blk_data *md;
	int ret;

	switch (cmd) {
	case MMC_IOC_CMD:
		ret = mmc_blk_check_blkdev(bdev);
		if (ret)
			return ret;
		md = mmc_blk_get(bdev->bd_disk);
		if (!md)
			return -EINVAL;
		ret = mmc_blk_ioctl_cmd(md,
					(struct mmc_ioc_cmd __user *)arg,
					NULL);
		mmc_blk_put(md);
		return ret;
	case MMC_IOC_MULTI_CMD:
		ret = mmc_blk_check_blkdev(bdev);
		if (ret)
			return ret;
		md = mmc_blk_get(bdev->bd_disk);
		if (!md)
			return -EINVAL;
		ret = mmc_blk_ioctl_multi_cmd(md,
					(struct mmc_ioc_multi_cmd __user *)arg,
					NULL);
		mmc_blk_put(md);
		return ret;
	default:
		return -EINVAL;
	}
}

#ifdef CONFIG_COMPAT
static int mmc_blk_compat_ioctl(struct block_device *bdev, fmode_t mode,
	unsigned int cmd, unsigned long arg)
{
	return mmc_blk_ioctl(bdev, mode, cmd, (unsigned long) compat_ptr(arg));
}
#endif

static int mmc_blk_alternative_gpt_sector(struct gendisk *disk,
					  sector_t *sector)
{
	struct mmc_blk_data *md;
	int ret;

	md = mmc_blk_get(disk);
	if (!md)
		return -EINVAL;

	if (md->queue.card)
		ret = mmc_card_alternative_gpt_sector(md->queue.card, sector);
	else
		ret = -ENODEV;

	mmc_blk_put(md);

	return ret;
}

static const struct block_device_operations mmc_bdops = {
	.open			= mmc_blk_open,
	.release		= mmc_blk_release,
	.getgeo			= mmc_blk_getgeo,
	.owner			= THIS_MODULE,
	.ioctl			= mmc_blk_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl		= mmc_blk_compat_ioctl,
#endif
	.alternative_gpt_sector	= mmc_blk_alternative_gpt_sector,
};

static int mmc_blk_part_switch_pre(struct mmc_card *card,
				   unsigned int part_type)
{
	int ret = 0;

	if (part_type == EXT_CSD_PART_CONFIG_ACC_RPMB) {
		if (card->ext_csd.cmdq_en) {
			ret = mmc_cmdq_disable(card);
			if (ret)
				return ret;
		}
		mmc_retune_pause(card->host);
	}

	return ret;
}

static int mmc_blk_part_switch_post(struct mmc_card *card,
				    unsigned int part_type)
{
	int ret = 0;

	if (part_type == EXT_CSD_PART_CONFIG_ACC_RPMB) {
		mmc_retune_unpause(card->host);
		if (card->reenable_cmdq && !card->ext_csd.cmdq_en)
			ret = mmc_cmdq_enable(card);
	}

	return ret;
}

static inline int mmc_blk_part_switch(struct mmc_card *card,
				      unsigned int part_type)
{
	int ret = 0;
	struct mmc_blk_data *main_md = dev_get_drvdata(&card->dev);

	if (main_md->part_curr == part_type)
		return 0;

	if (mmc_card_mmc(card)) {
		u8 part_config = card->ext_csd.part_config;

		ret = mmc_blk_part_switch_pre(card, part_type);
		if (ret)
			return ret;

		part_config &= ~EXT_CSD_PART_CONFIG_ACC_MASK;
		part_config |= part_type;

		ret = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL,
				 EXT_CSD_PART_CONFIG, part_config,
				 card->ext_csd.part_time);
		if (ret) {
			mmc_blk_part_switch_post(card, part_type);
			return ret;
		}

		card->ext_csd.part_config = part_config;

		ret = mmc_blk_part_switch_post(card, main_md->part_curr);
	}

	main_md->part_curr = part_type;
	return ret;
}

static int mmc_sd_num_wr_blocks(struct mmc_card *card, u32 *written_blocks)
{
	int err;
	u32 result;
	__be32 *blocks;

	struct mmc_request mrq = {};
	struct mmc_command cmd = {};
	struct mmc_data data = {};

	struct scatterlist sg;

	cmd.opcode = MMC_APP_CMD;
	cmd.arg = card->rca << 16;
	cmd.flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_AC;

	err = mmc_wait_for_cmd(card->host, &cmd, 0);
	if (err)
		return err;
	if (!mmc_host_is_spi(card->host) && !(cmd.resp[0] & R1_APP_CMD))
		return -EIO;

	memset(&cmd, 0, sizeof(struct mmc_command));

	cmd.opcode = SD_APP_SEND_NUM_WR_BLKS;
	cmd.arg = 0;
	cmd.flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_ADTC;

	data.blksz = 4;
	data.blocks = 1;
	data.flags = MMC_DATA_READ;
	data.sg = &sg;
	data.sg_len = 1;
	mmc_set_data_timeout(&data, card);

	mrq.cmd = &cmd;
	mrq.data = &data;

	blocks = kmalloc(4, GFP_KERNEL);
	if (!blocks)
		return -ENOMEM;

	sg_init_one(&sg, blocks, 4);

	mmc_wait_for_req(card->host, &mrq);

	result = ntohl(*blocks);
	kfree(blocks);

	if (cmd.error || data.error)
		return -EIO;

	*written_blocks = result;

	return 0;
}

static unsigned int mmc_blk_clock_khz(struct mmc_host *host)
{
	if (host->actual_clock)
		return host->actual_clock / 1000;

	/* Clock may be subject to a divisor, fudge it by a factor of 2. */
	if (host->ios.clock)
		return host->ios.clock / 2000;

	/* How can there be no clock */
	WARN_ON_ONCE(1);
	return 100; /* 100 kHz is minimum possible value */
}

static unsigned int mmc_blk_data_timeout_ms(struct mmc_host *host,
					    struct mmc_data *data)
{
	unsigned int ms = DIV_ROUND_UP(data->timeout_ns, 1000000);
	unsigned int khz;

	if (data->timeout_clks) {
		khz = mmc_blk_clock_khz(host);
		ms += DIV_ROUND_UP(data->timeout_clks, khz);
	}

	return ms;
}

/*
 * Attempts to reset the card and get back to the requested partition.
 * Therefore any error here must result in cancelling the block layer
 * request, it must not be reattempted without going through the mmc_blk
 * partition sanity checks.
 */
static int mmc_blk_reset(struct mmc_blk_data *md, struct mmc_host *host,
			 int type)
{
	int err;
	struct mmc_blk_data *main_md = dev_get_drvdata(&host->card->dev);

	if (md->reset_done & type)
		return -EEXIST;

	md->reset_done |= type;
	err = mmc_hw_reset(host->card);
	/*
	 * A successful reset will leave the card in the main partition, but
	 * upon failure it might not be, so set it to MMC_BLK_PART_INVALID
	 * in that case.
	 */
	main_md->part_curr = err ? MMC_BLK_PART_INVALID : main_md->part_type;
	if (err)
		return err;
	/* Ensure we switch back to the correct partition */
	if (mmc_blk_part_switch(host->card, md->part_type))
		/*
		 * We have failed to get back into the correct
		 * partition, so we need to abort the whole request.
		 */
		return -ENODEV;
	return 0;
}

static inline void mmc_blk_reset_success(struct mmc_blk_data *md, int type)
{
	md->reset_done &= ~type;
}

/*
 * The non-block commands come back from the block layer after it queued it and
 * processed it with all other requests and then they get issued in this
 * function.
 */
static void mmc_blk_issue_drv_op(struct mmc_queue *mq, struct request *req)
{
	struct mmc_queue_req *mq_rq;
	struct mmc_card *card = mq->card;
	struct mmc_blk_data *md = mq->blkdata;
	struct mmc_blk_ioc_data **idata;
	bool rpmb_ioctl;
	u8 **ext_csd;
	u32 status;
	int ret;
	int i;

	mq_rq = req_to_mmc_queue_req(req);
	rpmb_ioctl = (mq_rq->drv_op == MMC_DRV_OP_IOCTL_RPMB);

	switch (mq_rq->drv_op) {
	case MMC_DRV_OP_IOCTL:
		if (card->ext_csd.cmdq_en) {
			ret = mmc_cmdq_disable(card);
			if (ret)
				break;
		}
		fallthrough;
	case MMC_DRV_OP_IOCTL_RPMB:
		idata = mq_rq->drv_op_data;
		for (i = 0, ret = 0; i < mq_rq->ioc_count; i++) {
			ret = __mmc_blk_ioctl_cmd(card, md, idata[i]);
			if (ret)
				break;
		}
		/* Always switch back to main area after RPMB access */
		if (rpmb_ioctl)
			mmc_blk_part_switch(card, 0);
		else if (card->reenable_cmdq && !card->ext_csd.cmdq_en)
			mmc_cmdq_enable(card);
		break;
	case MMC_DRV_OP_BOOT_WP:
		ret = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_BOOT_WP,
				 card->ext_csd.boot_ro_lock |
				 EXT_CSD_BOOT_WP_B_PWR_WP_EN,
				 card->ext_csd.part_time);
		if (ret)
			pr_err("%s: Locking boot partition ro until next power on failed: %d\n",
			       md->disk->disk_name, ret);
		else
			card->ext_csd.boot_ro_lock |=
				EXT_CSD_BOOT_WP_B_PWR_WP_EN;
		break;
	case MMC_DRV_OP_GET_CARD_STATUS:
		ret = mmc_send_status(card, &status);
		if (!ret)
			ret = status;
		break;
	case MMC_DRV_OP_GET_EXT_CSD:
		ext_csd = mq_rq->drv_op_data;
		ret = mmc_get_ext_csd(card, ext_csd);
		break;
	default:
		pr_err("%s: unknown driver specific operation\n",
		       md->disk->disk_name);
		ret = -EINVAL;
		break;
	}
	mq_rq->drv_op_result = ret;
	blk_mq_end_request(req, ret ? BLK_STS_IOERR : BLK_STS_OK);
}

static void mmc_blk_issue_erase_rq(struct mmc_queue *mq, struct request *req,
				   int type, unsigned int erase_arg)
{
	struct mmc_blk_data *md = mq->blkdata;
	struct mmc_card *card = md->queue.card;
	unsigned int from, nr;
	int err = 0;
	blk_status_t status = BLK_STS_OK;

	if (!mmc_can_erase(card)) {
		status = BLK_STS_NOTSUPP;
		goto fail;
	}

	from = blk_rq_pos(req);
	nr = blk_rq_sectors(req);

	do {
		err = 0;
		if (card->quirks & MMC_QUIRK_INAND_CMD38) {
			err = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL,
					 INAND_CMD38_ARG_EXT_CSD,
					 erase_arg == MMC_TRIM_ARG ?
					 INAND_CMD38_ARG_TRIM :
					 INAND_CMD38_ARG_ERASE,
					 card->ext_csd.generic_cmd6_time);
		}
		if (!err)
			err = mmc_erase(card, from, nr, erase_arg);
	} while (err == -EIO && !mmc_blk_reset(md, card->host, type));
	if (err)
		status = BLK_STS_IOERR;
	else
		mmc_blk_reset_success(md, type);
fail:
	blk_mq_end_request(req, status);
}

static void mmc_blk_issue_trim_rq(struct mmc_queue *mq, struct request *req)
{
	mmc_blk_issue_erase_rq(mq, req, MMC_BLK_TRIM, MMC_TRIM_ARG);
}

static void mmc_blk_issue_discard_rq(struct mmc_queue *mq, struct request *req)
{
	struct mmc_blk_data *md = mq->blkdata;
	struct mmc_card *card = md->queue.card;
	unsigned int arg = card->erase_arg;

	if (mmc_card_broken_sd_discard(card))
		arg = SD_ERASE_ARG;

	mmc_blk_issue_erase_rq(mq, req, MMC_BLK_DISCARD, arg);
}

static void mmc_blk_issue_secdiscard_rq(struct mmc_queue *mq,
				       struct request *req)
{
	struct mmc_blk_data *md = mq->blkdata;
	struct mmc_card *card = md->queue.card;
	unsigned int from, nr, arg;
	int err = 0, type = MMC_BLK_SECDISCARD;
	blk_status_t status = BLK_STS_OK;

	if (!(mmc_can_secure_erase_trim(card))) {
		status = BLK_STS_NOTSUPP;
		goto out;
	}

	from = blk_rq_pos(req);
	nr = blk_rq_sectors(req);

	if (mmc_can_trim(card) && !mmc_erase_group_aligned(card, from, nr))
		arg = MMC_SECURE_TRIM1_ARG;
	else
		arg = MMC_SECURE_ERASE_ARG;

retry:
	if (card->quirks & MMC_QUIRK_INAND_CMD38) {
		err = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL,
				 INAND_CMD38_ARG_EXT_CSD,
				 arg == MMC_SECURE_TRIM1_ARG ?
				 INAND_CMD38_ARG_SECTRIM1 :
				 INAND_CMD38_ARG_SECERASE,
				 card->ext_csd.generic_cmd6_time);
		if (err)
			goto out_retry;
	}

	err = mmc_erase(card, from, nr, arg);
	if (err == -EIO)
		goto out_retry;
	if (err) {
		status = BLK_STS_IOERR;
		goto out;
	}

	if (arg == MMC_SECURE_TRIM1_ARG) {
		if (card->quirks & MMC_QUIRK_INAND_CMD38) {
			err = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL,
					 INAND_CMD38_ARG_EXT_CSD,
					 INAND_CMD38_ARG_SECTRIM2,
					 card->ext_csd.generic_cmd6_time);
			if (err)
				goto out_retry;
		}

		err = mmc_erase(card, from, nr, MMC_SECURE_TRIM2_ARG);
		if (err == -EIO)
			goto out_retry;
		if (err) {
			status = BLK_STS_IOERR;
			goto out;
		}
	}

out_retry:
	if (err && !mmc_blk_reset(md, card->host, type))
		goto retry;
	if (!err)
		mmc_blk_reset_success(md, type);
out:
	blk_mq_end_request(req, status);
}

static void mmc_blk_issue_flush(struct mmc_queue *mq, struct request *req)
{
	struct mmc_blk_data *md = mq->blkdata;
	struct mmc_card *card = md->queue.card;
	int ret = 0;

	ret = mmc_flush_cache(card->host);
	blk_mq_end_request(req, ret ? BLK_STS_IOERR : BLK_STS_OK);
}

/*
 * Reformat current write as a reliable write, supporting
 * both legacy and the enhanced reliable write MMC cards.
 * In each transfer we'll handle only as much as a single
 * reliable write can handle, thus finish the request in
 * partial completions.
 */
static inline void mmc_apply_rel_rw(struct mmc_blk_request *brq,
				    struct mmc_card *card,
				    struct request *req)
{
	if (!(card->ext_csd.rel_param & EXT_CSD_WR_REL_PARAM_EN)) {
		/* Legacy mode imposes restrictions on transfers. */
		if (!IS_ALIGNED(blk_rq_pos(req), card->ext_csd.rel_sectors))
			brq->data.blocks = 1;

		if (brq->data.blocks > card->ext_csd.rel_sectors)
			brq->data.blocks = card->ext_csd.rel_sectors;
		else if (brq->data.blocks < card->ext_csd.rel_sectors)
			brq->data.blocks = 1;
	}
}

#define CMD_ERRORS_EXCL_OOR						\
	(R1_ADDRESS_ERROR |	/* Misaligned address */		\
	 R1_BLOCK_LEN_ERROR |	/* Transferred block length incorrect */\
	 R1_WP_VIOLATION |	/* Tried to write to protected block */	\
	 R1_CARD_ECC_FAILED |	/* Card ECC failed */			\
	 R1_CC_ERROR |		/* Card controller error */		\
	 R1_ERROR)		/* General/unknown error */

#define CMD_ERRORS							\
	(CMD_ERRORS_EXCL_OOR |						\
	 R1_OUT_OF_RANGE)	/* Command argument out of range */	\

static void mmc_blk_eval_resp_error(struct mmc_blk_request *brq)
{
	u32 val;

	/*
	 * Per the SD specification(physical layer version 4.10)[1],
	 * section 4.3.3, it explicitly states that "When the last
	 * block of user area is read using CMD18, the host should
	 * ignore OUT_OF_RANGE error that may occur even the sequence
	 * is correct". And JESD84-B51 for eMMC also has a similar
	 * statement on section 6.8.3.
	 *
	 * Multiple block read/write could be done by either predefined
	 * method, namely CMD23, or open-ending mode. For open-ending mode,
	 * we should ignore the OUT_OF_RANGE error as it's normal behaviour.
	 *
	 * However the spec[1] doesn't tell us whether we should also
	 * ignore that for predefined method. But per the spec[1], section
	 * 4.15 Set Block Count Command, it says"If illegal block count
	 * is set, out of range error will be indicated during read/write
	 * operation (For example, data transfer is stopped at user area
	 * boundary)." In another word, we could expect a out of range error
	 * in the response for the following CMD18/25. And if argument of
	 * CMD23 + the argument of CMD18/25 exceed the max number of blocks,
	 * we could also expect to get a -ETIMEDOUT or any error number from
	 * the host drivers due to missing data response(for write)/data(for
	 * read), as the cards will stop the data transfer by itself per the
	 * spec. So we only need to check R1_OUT_OF_RANGE for open-ending mode.
	 */

	if (!brq->stop.error) {
		bool oor_with_open_end;
		/* If there is no error yet, check R1 response */

		val = brq->stop.resp[0] & CMD_ERRORS;
		oor_with_open_end = val & R1_OUT_OF_RANGE && !brq->mrq.sbc;

		if (val && !oor_with_open_end)
			brq->stop.error = -EIO;
	}
}

static void mmc_blk_data_prep(struct mmc_queue *mq, struct mmc_queue_req *mqrq,
			      int recovery_mode, bool *do_rel_wr_p,
			      bool *do_data_tag_p)
{
	struct mmc_blk_data *md = mq->blkdata;
	struct mmc_card *card = md->queue.card;
	struct mmc_blk_request *brq = &mqrq->brq;
	struct request *req = mmc_queue_req_to_req(mqrq);
	bool do_rel_wr, do_data_tag;

	/*
	 * Reliable writes are used to implement Forced Unit Access and
	 * are supported only on MMCs.
	 */
	do_rel_wr = (req->cmd_flags & REQ_FUA) &&
		    rq_data_dir(req) == WRITE &&
		    (md->flags & MMC_BLK_REL_WR);

	memset(brq, 0, sizeof(struct mmc_blk_request));

	mmc_crypto_prepare_req(mqrq);

	brq->mrq.data = &brq->data;
	brq->mrq.tag = req->tag;

	brq->stop.opcode = MMC_STOP_TRANSMISSION;
	brq->stop.arg = 0;

	if (rq_data_dir(req) == READ) {
		brq->data.flags = MMC_DATA_READ;
		brq->stop.flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_AC;
	} else {
		brq->data.flags = MMC_DATA_WRITE;
		brq->stop.flags = MMC_RSP_SPI_R1B | MMC_RSP_R1B | MMC_CMD_AC;
	}

	brq->data.blksz = 512;
	brq->data.blocks = blk_rq_sectors(req);
	brq->data.blk_addr = blk_rq_pos(req);

	/*
	 * The command queue supports 2 priorities: "high" (1) and "simple" (0).
	 * The eMMC will give "high" priority tasks priority over "simple"
	 * priority tasks. Here we always set "simple" priority by not setting
	 * MMC_DATA_PRIO.
	 */

	/*
	 * The block layer doesn't support all sector count
	 * restrictions, so we need to be prepared for too big
	 * requests.
	 */
	if (brq->data.blocks > card->host->max_blk_count)
		brq->data.blocks = card->host->max_blk_count;

	if (brq->data.blocks > 1) {
		/*
		 * Some SD cards in SPI mode return a CRC error or even lock up
		 * completely when trying to read the last block using a
		 * multiblock read command.
		 */
		if (mmc_host_is_spi(card->host) && (rq_data_dir(req) == READ) &&
		    (blk_rq_pos(req) + blk_rq_sectors(req) ==
		     get_capacity(md->disk)))
			brq->data.blocks--;

		/*
		 * After a read error, we redo the request one (native) sector
		 * at a time in order to accurately determine which
		 * sectors can be read successfully.
		 */
		if (recovery_mode)
			brq->data.blocks = queue_physical_block_size(mq->queue) >> 9;

		/*
		 * Some controllers have HW issues while operating
		 * in multiple I/O mode
		 */
		if (card->host->ops->multi_io_quirk)
			brq->data.blocks = card->host->ops->multi_io_quirk(card,
						(rq_data_dir(req) == READ) ?
						MMC_DATA_READ : MMC_DATA_WRITE,
						brq->data.blocks);
	}

	if (do_rel_wr) {
		mmc_apply_rel_rw(brq, card, req);
		brq->data.flags |= MMC_DATA_REL_WR;
	}

	/*
	 * Data tag is used only during writing meta data to speed
	 * up write and any subsequent read of this meta data
	 */
	do_data_tag = card->ext_csd.data_tag_unit_size &&
		      (req->cmd_flags & REQ_META) &&
		      (rq_data_dir(req) == WRITE) &&
		      ((brq->data.blocks * brq->data.blksz) >=
		       card->ext_csd.data_tag_unit_size);

	if (do_data_tag)
		brq->data.flags |= MMC_DATA_DAT_TAG;

	mmc_set_data_timeout(&brq->data, card);

	brq->data.sg = mqrq->sg;
	brq->data.sg_len = mmc_queue_map_sg(mq, mqrq);

	/*
	 * Adjust the sg list so it is the same size as the
	 * request.
	 */
	if (brq->data.blocks != blk_rq_sectors(req)) {
		int i, data_size = brq->data.blocks << 9;
		struct scatterlist *sg;

		for_each_sg(brq->data.sg, sg, brq->data.sg_len, i) {
			data_size -= sg->length;
			if (data_size <= 0) {
				sg->length += data_size;
				i++;
				break;
			}
		}
		brq->data.sg_len = i;
	}

	if (do_rel_wr_p)
		*do_rel_wr_p = do_rel_wr;

	if (do_data_tag_p)
		*do_data_tag_p = do_data_tag;
}

#define MMC_CQE_RETRIES 2

static void mmc_blk_cqe_complete_rq(struct mmc_queue *mq, struct request *req)
{
	struct mmc_queue_req *mqrq = req_to_mmc_queue_req(req);
	struct mmc_request *mrq = &mqrq->brq.mrq;
	struct request_queue *q = req->q;
	struct mmc_host *host = mq->card->host;
	enum mmc_issue_type issue_type = mmc_issue_type(mq, req);
	unsigned long flags;
	bool put_card;
	int err;

	mmc_cqe_post_req(host, mrq);

	if (mrq->cmd && mrq->cmd->error)
		err = mrq->cmd->error;
	else if (mrq->data && mrq->data->error)
		err = mrq->data->error;
	else
		err = 0;

	if (err) {
		if (mqrq->retries++ < MMC_CQE_RETRIES)
			blk_mq_requeue_request(req, true);
		else
			blk_mq_end_request(req, BLK_STS_IOERR);
	} else if (mrq->data) {
		if (blk_update_request(req, BLK_STS_OK, mrq->data->bytes_xfered))
			blk_mq_requeue_request(req, true);
		else
			__blk_mq_end_request(req, BLK_STS_OK);
	} else {
		blk_mq_end_request(req, BLK_STS_OK);
	}

	spin_lock_irqsave(&mq->lock, flags);

	mq->in_flight[issue_type] -= 1;

	put_card = (mmc_tot_in_flight(mq) == 0);

	mmc_cqe_check_busy(mq);

	spin_unlock_irqrestore(&mq->lock, flags);

	if (!mq->cqe_busy)
		blk_mq_run_hw_queues(q, true);

	if (put_card)
		mmc_put_card(mq->card, &mq->ctx);
}

void mmc_blk_cqe_recovery(struct mmc_queue *mq)
{
	struct mmc_card *card = mq->card;
	struct mmc_host *host = card->host;
	int err;

	pr_debug("%s: CQE recovery start\n", mmc_hostname(host));

	err = mmc_cqe_recovery(host);
	if (err)
		mmc_blk_reset(mq->blkdata, host, MMC_BLK_CQE_RECOVERY);
	mmc_blk_reset_success(mq->blkdata, MMC_BLK_CQE_RECOVERY);

	pr_debug("%s: CQE recovery done\n", mmc_hostname(host));
}

static void mmc_blk_cqe_req_done(struct mmc_request *mrq)
{
	struct mmc_queue_req *mqrq = container_of(mrq, struct mmc_queue_req,
						  brq.mrq);
	struct request *req = mmc_queue_req_to_req(mqrq);
	struct request_queue *q = req->q;
	struct mmc_queue *mq = q->queuedata;

	/*
	 * Block layer timeouts race with completions which means the normal
	 * completion path cannot be used during recovery.
	 */
	if (mq->in_recovery)
		mmc_blk_cqe_complete_rq(mq, req);
	else if (likely(!blk_should_fake_timeout(req->q)))
		blk_mq_complete_request(req);
}

static int mmc_blk_cqe_start_req(struct mmc_host *host, struct mmc_request *mrq)
{
	mrq->done		= mmc_blk_cqe_req_done;
	mrq->recovery_notifier	= mmc_cqe_recovery_notifier;

	return mmc_cqe_start_req(host, mrq);
}

static struct mmc_request *mmc_blk_cqe_prep_dcmd(struct mmc_queue_req *mqrq,
						 struct request *req)
{
	struct mmc_blk_request *brq = &mqrq->brq;

	memset(brq, 0, sizeof(*brq));

	brq->mrq.cmd = &brq->cmd;
	brq->mrq.tag = req->tag;

	return &brq->mrq;
}

static int mmc_blk_cqe_issue_flush(struct mmc_queue *mq, struct request *req)
{
	struct mmc_queue_req *mqrq = req_to_mmc_queue_req(req);
	struct mmc_request *mrq = mmc_blk_cqe_prep_dcmd(mqrq, req);

	mrq->cmd->opcode = MMC_SWITCH;
	mrq->cmd->arg = (MMC_SWITCH_MODE_WRITE_BYTE << 24) |
			(EXT_CSD_FLUSH_CACHE << 16) |
			(1 << 8) |
			EXT_CSD_CMD_SET_NORMAL;
	mrq->cmd->flags = MMC_CMD_AC | MMC_RSP_R1B;

	return mmc_blk_cqe_start_req(mq->card->host, mrq);
}

static int mmc_blk_hsq_issue_rw_rq(struct mmc_queue *mq, struct request *req)
{
	struct mmc_queue_req *mqrq = req_to_mmc_queue_req(req);
	struct mmc_host *host = mq->card->host;
	int err;

	mmc_blk_rw_rq_prep(mqrq, mq->card, 0, mq);
	mqrq->brq.mrq.done = mmc_blk_hsq_req_done;
	mmc_pre_req(host, &mqrq->brq.mrq);

	err = mmc_cqe_start_req(host, &mqrq->brq.mrq);
	if (err)
		mmc_post_req(host, &mqrq->brq.mrq, err);

	return err;
}

static int mmc_blk_cqe_issue_rw_rq(struct mmc_queue *mq, struct request *req)
{
	struct mmc_queue_req *mqrq = req_to_mmc_queue_req(req);
	struct mmc_host *host = mq->card->host;

	if (host->hsq_enabled)
		return mmc_blk_hsq_issue_rw_rq(mq, req);

	mmc_blk_data_prep(mq, mqrq, 0, NULL, NULL);

	return mmc_blk_cqe_start_req(mq->card->host, &mqrq->brq.mrq);
}

static void mmc_blk_rw_rq_prep(struct mmc_queue_req *mqrq,
			       struct mmc_card *card,
			       int recovery_mode,
			       struct mmc_queue *mq)
{
	u32 readcmd, writecmd;
	struct mmc_blk_request *brq = &mqrq->brq;
	struct request *req = mmc_queue_req_to_req(mqrq);
	struct mmc_blk_data *md = mq->blkdata;
	bool do_rel_wr, do_data_tag;

	mmc_blk_data_prep(mq, mqrq, recovery_mode, &do_rel_wr, &do_data_tag);

	brq->mrq.cmd = &brq->cmd;

	brq->cmd.arg = blk_rq_pos(req);
	if (!mmc_card_blockaddr(card))
		brq->cmd.arg <<= 9;
	brq->cmd.flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_ADTC;

	if (brq->data.blocks > 1 || do_rel_wr) {
		/* SPI multiblock writes terminate using a special
		 * token, not a STOP_TRANSMISSION request.
		 */
		if (!mmc_host_is_spi(card->host) ||
		    rq_data_dir(req) == READ)
			brq->mrq.stop = &brq->stop;
		readcmd = MMC_READ_MULTIPLE_BLOCK;
		writecmd = MMC_WRITE_MULTIPLE_BLOCK;
	} else {
		brq->mrq.stop = NULL;
		readcmd = MMC_READ_SINGLE_BLOCK;
		writecmd = MMC_WRITE_BLOCK;
	}
	brq->cmd.opcode = rq_data_dir(req) == READ ? readcmd : writecmd;

	/*
	 * Pre-defined multi-block transfers are preferable to
	 * open ended-ones (and necessary for reliable writes).
	 * However, it is not sufficient to just send CMD23,
	 * and avoid the final CMD12, as on an error condition
	 * CMD12 (stop) needs to be sent anyway. This, coupled
	 * with Auto-CMD23 enhancements provided by some
	 * hosts, means that the complexity of dealing
	 * with this is best left to the host. If CMD23 is
	 * supported by card and host, we'll fill sbc in and let
	 * the host deal with handling it correctly. This means
	 * that for hosts that don't expose MMC_CAP_CMD23, no
	 * change of behavior will be observed.
	 *
	 * N.B: Some MMC cards experience perf degradation.
	 * We'll avoid using CMD23-bounded multiblock writes for
	 * these, while retaining features like reliable writes.
	 */
	if ((md->flags & MMC_BLK_CMD23) && mmc_op_multi(brq->cmd.opcode) &&
	    (do_rel_wr || !(card->quirks & MMC_QUIRK_BLK_NO_CMD23) ||
	     do_data_tag)) {
		brq->sbc.opcode = MMC_SET_BLOCK_COUNT;
		brq->sbc.arg = brq->data.blocks |
			(do_rel_wr ? (1 << 31) : 0) |
			(do_data_tag ? (1 << 29) : 0);
		brq->sbc.flags = MMC_RSP_R1 | MMC_CMD_AC;
		brq->mrq.sbc = &brq->sbc;
	}
}

#define MMC_MAX_RETRIES		5
#define MMC_DATA_RETRIES	2
#define MMC_NO_RETRIES		(MMC_MAX_RETRIES + 1)

static int mmc_blk_send_stop(struct mmc_card *card, unsigned int timeout)
{
	struct mmc_command cmd = {
		.opcode = MMC_STOP_TRANSMISSION,
		.flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_AC,
		/* Some hosts wait for busy anyway, so provide a busy timeout */
		.busy_timeout = timeout,
	};

	return mmc_wait_for_cmd(card->host, &cmd, 5);
}

static int mmc_blk_fix_state(struct mmc_card *card, struct request *req)
{
	struct mmc_queue_req *mqrq = req_to_mmc_queue_req(req);
	struct mmc_blk_request *brq = &mqrq->brq;
	unsigned int timeout = mmc_blk_data_timeout_ms(card->host, &brq->data);
	int err;

	mmc_retune_hold_now(card->host);

	mmc_blk_send_stop(card, timeout);

	err = mmc_poll_for_busy(card, timeout, false, MMC_BUSY_IO);

	mmc_retune_release(card->host);

	return err;
}

#define MMC_READ_SINGLE_RETRIES	2

/* Single (native) sector read during recovery */
static void mmc_blk_read_single(struct mmc_queue *mq, struct request *req)
{
	struct mmc_queue_req *mqrq = req_to_mmc_queue_req(req);
	struct mmc_request *mrq = &mqrq->brq.mrq;
	struct mmc_card *card = mq->card;
	struct mmc_host *host = card->host;
	blk_status_t error = BLK_STS_OK;
	size_t bytes_per_read = queue_physical_block_size(mq->queue);

	do {
		u32 status;
		int err;
		int retries = 0;

		while (retries++ <= MMC_READ_SINGLE_RETRIES) {
			mmc_blk_rw_rq_prep(mqrq, card, 1, mq);

			mmc_wait_for_req(host, mrq);

			err = mmc_send_status(card, &status);
			if (err)
				goto error_exit;

			if (!mmc_host_is_spi(host) &&
			    !mmc_ready_for_data(status)) {
				err = mmc_blk_fix_state(card, req);
				if (err)
					goto error_exit;
			}

			if (!mrq->cmd->error)
				break;
		}

		if (mrq->cmd->error ||
		    mrq->data->error ||
		    (!mmc_host_is_spi(host) &&
		     (mrq->cmd->resp[0] & CMD_ERRORS || status & CMD_ERRORS)))
			error = BLK_STS_IOERR;
		else
			error = BLK_STS_OK;

	} while (blk_update_request(req, error, bytes_per_read));

	return;

error_exit:
	mrq->data->bytes_xfered = 0;
	blk_update_request(req, BLK_STS_IOERR, bytes_per_read);
	/* Let it try the remaining request again */
	if (mqrq->retries > MMC_MAX_RETRIES - 1)
		mqrq->retries = MMC_MAX_RETRIES - 1;
}

static inline bool mmc_blk_oor_valid(struct mmc_blk_request *brq)
{
	return !!brq->mrq.sbc;
}

static inline u32 mmc_blk_stop_err_bits(struct mmc_blk_request *brq)
{
	return mmc_blk_oor_valid(brq) ? CMD_ERRORS : CMD_ERRORS_EXCL_OOR;
}

/*
 * Check for errors the host controller driver might not have seen such as
 * response mode errors or invalid card state.
 */
static bool mmc_blk_status_error(struct request *req, u32 status)
{
	struct mmc_queue_req *mqrq = req_to_mmc_queue_req(req);
	struct mmc_blk_request *brq = &mqrq->brq;
	struct mmc_queue *mq = req->q->queuedata;
	u32 stop_err_bits;

	if (mmc_host_is_spi(mq->card->host))
		return false;

	stop_err_bits = mmc_blk_stop_err_bits(brq);

	return brq->cmd.resp[0]  & CMD_ERRORS    ||
	       brq->stop.resp[0] & stop_err_bits ||
	       status            & stop_err_bits ||
	       (rq_data_dir(req) == WRITE && !mmc_ready_for_data(status));
}

static inline bool mmc_blk_cmd_started(struct mmc_blk_request *brq)
{
	return !brq->sbc.error && !brq->cmd.error &&
	       !(brq->cmd.resp[0] & CMD_ERRORS);
}

/*
 * Requests are completed by mmc_blk_mq_complete_rq() which sets simple
 * policy:
 * 1. A request that has transferred at least some data is considered
 * successful and will be requeued if there is remaining data to
 * transfer.
 * 2. Otherwise the number of retries is incremented and the request
 * will be requeued if there are remaining retries.
 * 3. Otherwise the request will be errored out.
 * That means mmc_blk_mq_complete_rq() is controlled by bytes_xfered and
 * mqrq->retries. So there are only 4 possible actions here:
 *	1. do not accept the bytes_xfered value i.e. set it to zero
 *	2. change mqrq->retries to determine the number of retries
 *	3. try to reset the card
 *	4. read one sector at a time
 */
static void mmc_blk_mq_rw_recovery(struct mmc_queue *mq, struct request *req)
{
	int type = rq_data_dir(req) == READ ? MMC_BLK_READ : MMC_BLK_WRITE;
	struct mmc_queue_req *mqrq = req_to_mmc_queue_req(req);
	struct mmc_blk_request *brq = &mqrq->brq;
	struct mmc_blk_data *md = mq->blkdata;
	struct mmc_card *card = mq->card;
	u32 status;
	u32 blocks;
	int err;

	/*
	 * Some errors the host driver might not have seen. Set the number of
	 * bytes transferred to zero in that case.
	 */
	err = __mmc_send_status(card, &status, 0);
	if (err || mmc_blk_status_error(req, status))
		brq->data.bytes_xfered = 0;

	mmc_retune_release(card->host);

	/*
	 * Try again to get the status. This also provides an opportunity for
	 * re-tuning.
	 */
	if (err)
		err = __mmc_send_status(card, &status, 0);

	/*
	 * Nothing more to do after the number of bytes transferred has been
	 * updated and there is no card.
	 */
	if (err && mmc_detect_card_removed(card->host))
		return;

	/* Try to get back to "tran" state */
	if (!mmc_host_is_spi(mq->card->host) &&
	    (err || !mmc_ready_for_data(status)))
		err = mmc_blk_fix_state(mq->card, req);

	/*
	 * Special case for SD cards where the card might record the number of
	 * blocks written.
	 */
	if (!err && mmc_blk_cmd_started(brq) && mmc_card_sd(card) &&
	    rq_data_dir(req) == WRITE) {
		if (mmc_sd_num_wr_blocks(card, &blocks))
			brq->data.bytes_xfered = 0;
		else
			brq->data.bytes_xfered = blocks << 9;
	}

	/* Reset if the card is in a bad state */
	if (!mmc_host_is_spi(mq->card->host) &&
	    err && mmc_blk_reset(md, card->host, type)) {
		pr_err("%s: recovery failed!\n", req->q->disk->disk_name);
		mqrq->retries = MMC_NO_RETRIES;
		return;
	}

	/*
	 * If anything was done, just return and if there is anything remaining
	 * on the request it will get requeued.
	 */
	if (brq->data.bytes_xfered)
		return;

	/* Reset before last retry */
	if (mqrq->retries + 1 == MMC_MAX_RETRIES &&
	    mmc_blk_reset(md, card->host, type))
		return;

	/* Command errors fail fast, so use all MMC_MAX_RETRIES */
	if (brq->sbc.error || brq->cmd.error)
		return;

	/* Reduce the remaining retries for data errors */
	if (mqrq->retries < MMC_MAX_RETRIES - MMC_DATA_RETRIES) {
		mqrq->retries = MMC_MAX_RETRIES - MMC_DATA_RETRIES;
		return;
	}

	if (rq_data_dir(req) == READ && brq->data.blocks >
			queue_physical_block_size(mq->queue) >> 9) {
		/* Read one (native) sector at a time */
		mmc_blk_read_single(mq, req);
		return;
	}
}

static inline bool mmc_blk_rq_error(struct mmc_blk_request *brq)
{
	mmc_blk_eval_resp_error(brq);

	return brq->sbc.error || brq->cmd.error || brq->stop.error ||
	       brq->data.error || brq->cmd.resp[0] & CMD_ERRORS;
}

static int mmc_spi_err_check(struct mmc_card *card)
{
	u32 status = 0;
	int err;

	/*
	 * SPI does not have a TRAN state we have to wait on, instead the
	 * card is ready again when it no longer holds the line LOW.
	 * We still have to ensure two things here before we know the write
	 * was successful:
	 * 1. The card has not disconnected during busy and we actually read our
	 * own pull-up, thinking it was still connected, so ensure it
	 * still responds.
	 * 2. Check for any error bits, in particular R1_SPI_IDLE to catch a
	 * just reconnected card after being disconnected during busy.
	 */
	err = __mmc_send_status(card, &status, 0);
	if (err)
		return err;
	/* All R1 and R2 bits of SPI are errors in our case */
	if (status)
		return -EIO;
	return 0;
}

static int mmc_blk_busy_cb(void *cb_data, bool *busy)
{
	struct mmc_blk_busy_data *data = cb_data;
	u32 status = 0;
	int err;

	err = mmc_send_status(data->card, &status);
	if (err)
		return err;

	/* Accumulate response error bits. */
	data->status |= status;

	*busy = !mmc_ready_for_data(status);
	return 0;
}

static int mmc_blk_card_busy(struct mmc_card *card, struct request *req)
{
	struct mmc_queue_req *mqrq = req_to_mmc_queue_req(req);
	struct mmc_blk_busy_data cb_data;
	int err;

	if (rq_data_dir(req) == READ)
		return 0;

	if (mmc_host_is_spi(card->host)) {
		err = mmc_spi_err_check(card);
		if (err)
			mqrq->brq.data.bytes_xfered = 0;
		return err;
	}

	cb_data.card = card;
	cb_data.status = 0;
	err = __mmc_poll_for_busy(card->host, 0, MMC_BLK_TIMEOUT_MS,
				  &mmc_blk_busy_cb, &cb_data);

	/*
	 * Do not assume data transferred correctly if there are any error bits
	 * set.
	 */
	if (cb_data.status & mmc_blk_stop_err_bits(&mqrq->brq)) {
		mqrq->brq.data.bytes_xfered = 0;
		err = err ? err : -EIO;
	}

	/* Copy the exception bit so it will be seen later on */
	if (mmc_card_mmc(card) && cb_data.status & R1_EXCEPTION_EVENT)
		mqrq->brq.cmd.resp[0] |= R1_EXCEPTION_EVENT;

	return err;
}

static inline void mmc_blk_rw_reset_success(struct mmc_queue *mq,
					    struct request *req)
{
	int type = rq_data_dir(req) == READ ? MMC_BLK_READ : MMC_BLK_WRITE;

	mmc_blk_reset_success(mq->blkdata, type);
}

static void mmc_blk_mq_complete_rq(struct mmc_queue *mq, struct request *req)
{
	struct mmc_queue_req *mqrq = req_to_mmc_queue_req(req);
	unsigned int nr_bytes = mqrq->brq.data.bytes_xfered;

	if (nr_bytes) {
		if (blk_update_request(req, BLK_STS_OK, nr_bytes))
			blk_mq_requeue_request(req, true);
		else
			__blk_mq_end_request(req, BLK_STS_OK);
	} else if (!blk_rq_bytes(req)) {
		__blk_mq_end_request(req, BLK_STS_IOERR);
	} else if (mqrq->retries++ < MMC_MAX_RETRIES) {
		blk_mq_requeue_request(req, true);
	} else {
		if (mmc_card_removed(mq->card))
			req->rq_flags |= RQF_QUIET;
		blk_mq_end_request(req, BLK_STS_IOERR);
	}
}

static bool mmc_blk_urgent_bkops_needed(struct mmc_queue *mq,
					struct mmc_queue_req *mqrq)
{
	return mmc_card_mmc(mq->card) && !mmc_host_is_spi(mq->card->host) &&
	       (mqrq->brq.cmd.resp[0] & R1_EXCEPTION_EVENT ||
		mqrq->brq.stop.resp[0] & R1_EXCEPTION_EVENT);
}

static void mmc_blk_urgent_bkops(struct mmc_queue *mq,
				 struct mmc_queue_req *mqrq)
{
	if (mmc_blk_urgent_bkops_needed(mq, mqrq))
		mmc_run_bkops(mq->card);
}

static void mmc_blk_hsq_req_done(struct mmc_request *mrq)
{
	struct mmc_queue_req *mqrq =
		container_of(mrq, struct mmc_queue_req, brq.mrq);
	struct request *req = mmc_queue_req_to_req(mqrq);
	struct request_queue *q = req->q;
	struct mmc_queue *mq = q->queuedata;
	struct mmc_host *host = mq->card->host;
	unsigned long flags;

	if (mmc_blk_rq_error(&mqrq->brq) ||
	    mmc_blk_urgent_bkops_needed(mq, mqrq)) {
		spin_lock_irqsave(&mq->lock, flags);
		mq->recovery_needed = true;
		mq->recovery_req = req;
		spin_unlock_irqrestore(&mq->lock, flags);

		host->cqe_ops->cqe_recovery_start(host);

		schedule_work(&mq->recovery_work);
		return;
	}

	mmc_blk_rw_reset_success(mq, req);

	/*
	 * Block layer timeouts race with completions which means the normal
	 * completion path cannot be used during recovery.
	 */
	if (mq->in_recovery)
		mmc_blk_cqe_complete_rq(mq, req);
	else if (likely(!blk_should_fake_timeout(req->q)))
		blk_mq_complete_request(req);
}

void mmc_blk_mq_complete(struct request *req)
{
	struct mmc_queue *mq = req->q->queuedata;
	struct mmc_host *host = mq->card->host;

	if (host->cqe_enabled)
		mmc_blk_cqe_complete_rq(mq, req);
	else if (likely(!blk_should_fake_timeout(req->q)))
		mmc_blk_mq_complete_rq(mq, req);
}

static void mmc_blk_mq_poll_completion(struct mmc_queue *mq,
				       struct request *req)
{
	struct mmc_queue_req *mqrq = req_to_mmc_queue_req(req);
	struct mmc_host *host = mq->card->host;

	if (mmc_blk_rq_error(&mqrq->brq) ||
	    mmc_blk_card_busy(mq->card, req)) {
		mmc_blk_mq_rw_recovery(mq, req);
	} else {
		mmc_blk_rw_reset_success(mq, req);
		mmc_retune_release(host);
	}

	mmc_blk_urgent_bkops(mq, mqrq);
}

static void mmc_blk_mq_dec_in_flight(struct mmc_queue *mq, struct request *req)
{
	unsigned long flags;
	bool put_card;

	spin_lock_irqsave(&mq->lock, flags);

	mq->in_flight[mmc_issue_type(mq, req)] -= 1;

	put_card = (mmc_tot_in_flight(mq) == 0);

	spin_unlock_irqrestore(&mq->lock, flags);

	if (put_card)
		mmc_put_card(mq->card, &mq->ctx);
}

static void mmc_blk_mq_post_req(struct mmc_queue *mq, struct request *req,
				bool can_sleep)
{
	struct mmc_queue_req *mqrq = req_to_mmc_queue_req(req);
	struct mmc_request *mrq = &mqrq->brq.mrq;
	struct mmc_host *host = mq->card->host;

	mmc_post_req(host, mrq, 0);

	/*
	 * Block layer timeouts race with completions which means the normal
	 * completion path cannot be used during recovery.
	 */
	if (mq->in_recovery) {
		mmc_blk_mq_complete_rq(mq, req);
	} else if (likely(!blk_should_fake_timeout(req->q))) {
		if (can_sleep)
			blk_mq_complete_request_direct(req, mmc_blk_mq_complete);
		else
			blk_mq_complete_request(req);
	}

	mmc_blk_mq_dec_in_flight(mq, req);
}

void mmc_blk_mq_recovery(struct mmc_queue *mq)
{
	struct request *req = mq->recovery_req;
	struct mmc_host *host = mq->card->host;
	struct mmc_queue_req *mqrq = req_to_mmc_queue_req(req);

	mq->recovery_req = NULL;
	mq->rw_wait = false;

	if (mmc_blk_rq_error(&mqrq->brq)) {
		mmc_retune_hold_now(host);
		mmc_blk_mq_rw_recovery(mq, req);
	}

	mmc_blk_urgent_bkops(mq, mqrq);

	mmc_blk_mq_post_req(mq, req, true);
}

static void mmc_blk_mq_complete_prev_req(struct mmc_queue *mq,
					 struct request **prev_req)
{
	if (mmc_host_done_complete(mq->card->host))
		return;

	mutex_lock(&mq->complete_lock);

	if (!mq->complete_req)
		goto out_unlock;

	mmc_blk_mq_poll_completion(mq, mq->complete_req);

	if (prev_req)
		*prev_req = mq->complete_req;
	else
		mmc_blk_mq_post_req(mq, mq->complete_req, true);

	mq->complete_req = NULL;

out_unlock:
	mutex_unlock(&mq->complete_lock);
}

void mmc_blk_mq_complete_work(struct work_struct *work)
{
	struct mmc_queue *mq = container_of(work, struct mmc_queue,
					    complete_work);

	mmc_blk_mq_complete_prev_req(mq, NULL);
}

static void mmc_blk_mq_req_done(struct mmc_request *mrq)
{
	struct mmc_queue_req *mqrq = container_of(mrq, struct mmc_queue_req,
						  brq.mrq);
	struct request *req = mmc_queue_req_to_req(mqrq);
	struct request_queue *q = req->q;
	struct mmc_queue *mq = q->queuedata;
	struct mmc_host *host = mq->card->host;
	unsigned long flags;

	if (!mmc_host_done_complete(host)) {
		bool waiting;

		/*
		 * We cannot complete the request in this context, so record
		 * that there is a request to complete, and that a following
		 * request does not need to wait (although it does need to
		 * complete complete_req first).
		 */
		spin_lock_irqsave(&mq->lock, flags);
		mq->complete_req = req;
		mq->rw_wait = false;
		waiting = mq->waiting;
		spin_unlock_irqrestore(&mq->lock, flags);

		/*
		 * If 'waiting' then the waiting task will complete this
		 * request, otherwise queue a work to do it. Note that
		 * complete_work may still race with the dispatch of a following
		 * request.
		 */
		if (waiting)
			wake_up(&mq->wait);
		else
			queue_work(mq->card->complete_wq, &mq->complete_work);

		return;
	}

	/* Take the recovery path for errors or urgent background operations */
	if (mmc_blk_rq_error(&mqrq->brq) ||
	    mmc_blk_urgent_bkops_needed(mq, mqrq)) {
		spin_lock_irqsave(&mq->lock, flags);
		mq->recovery_needed = true;
		mq->recovery_req = req;
		spin_unlock_irqrestore(&mq->lock, flags);
		wake_up(&mq->wait);
		schedule_work(&mq->recovery_work);
		return;
	}

	mmc_blk_rw_reset_success(mq, req);

	mq->rw_wait = false;
	wake_up(&mq->wait);

	/* context unknown */
	mmc_blk_mq_post_req(mq, req, false);
}

static bool mmc_blk_rw_wait_cond(struct mmc_queue *mq, int *err)
{
	unsigned long flags;
	bool done;

	/*
	 * Wait while there is another request in progress, but not if recovery
	 * is needed. Also indicate whether there is a request waiting to start.
	 */
	spin_lock_irqsave(&mq->lock, flags);
	if (mq->recovery_needed) {
		*err = -EBUSY;
		done = true;
	} else {
		done = !mq->rw_wait;
	}
	mq->waiting = !done;
	spin_unlock_irqrestore(&mq->lock, flags);

	return done;
}

static int mmc_blk_rw_wait(struct mmc_queue *mq, struct request **prev_req)
{
	int err = 0;

	wait_event(mq->wait, mmc_blk_rw_wait_cond(mq, &err));

	/* Always complete the previous request if there is one */
	mmc_blk_mq_complete_prev_req(mq, prev_req);

	return err;
}

static int mmc_blk_mq_issue_rw_rq(struct mmc_queue *mq,
				  struct request *req)
{
	struct mmc_queue_req *mqrq = req_to_mmc_queue_req(req);
	struct mmc_host *host = mq->card->host;
	struct request *prev_req = NULL;
	int err = 0;

	mmc_blk_rw_rq_prep(mqrq, mq->card, 0, mq);

	mqrq->brq.mrq.done = mmc_blk_mq_req_done;

	mmc_pre_req(host, &mqrq->brq.mrq);

	err = mmc_blk_rw_wait(mq, &prev_req);
	if (err)
		goto out_post_req;

	mq->rw_wait = true;

	err = mmc_start_request(host, &mqrq->brq.mrq);

	if (prev_req)
		mmc_blk_mq_post_req(mq, prev_req, true);

	if (err)
		mq->rw_wait = false;

	/* Release re-tuning here where there is no synchronization required */
	if (err || mmc_host_done_complete(host))
		mmc_retune_release(host);

out_post_req:
	if (err)
		mmc_post_req(host, &mqrq->brq.mrq, err);

	return err;
}

static int mmc_blk_wait_for_idle(struct mmc_queue *mq, struct mmc_host *host)
{
	if (host->cqe_enabled)
		return host->cqe_ops->cqe_wait_for_idle(host);

	return mmc_blk_rw_wait(mq, NULL);
}

enum mmc_issued mmc_blk_mq_issue_rq(struct mmc_queue *mq, struct request *req)
{
	struct mmc_blk_data *md = mq->blkdata;
	struct mmc_card *card = md->queue.card;
	struct mmc_host *host = card->host;
	int ret;

	ret = mmc_blk_part_switch(card, md->part_type);
	if (ret)
		return MMC_REQ_FAILED_TO_START;

	switch (mmc_issue_type(mq, req)) {
	case MMC_ISSUE_SYNC:
		ret = mmc_blk_wait_for_idle(mq, host);
		if (ret)
			return MMC_REQ_BUSY;
		switch (req_op(req)) {
		case REQ_OP_DRV_IN:
		case REQ_OP_DRV_OUT:
			mmc_blk_issue_drv_op(mq, req);
			break;
		case REQ_OP_DISCARD:
			mmc_blk_issue_discard_rq(mq, req);
			break;
		case REQ_OP_SECURE_ERASE:
			mmc_blk_issue_secdiscard_rq(mq, req);
			break;
		case REQ_OP_WRITE_ZEROES:
			mmc_blk_issue_trim_rq(mq, req);
			break;
		case REQ_OP_FLUSH:
			mmc_blk_issue_flush(mq, req);
			break;
		default:
			WARN_ON_ONCE(1);
			return MMC_REQ_FAILED_TO_START;
		}
		return MMC_REQ_FINISHED;
	case MMC_ISSUE_DCMD:
	case MMC_ISSUE_ASYNC:
		switch (req_op(req)) {
		case REQ_OP_FLUSH:
			if (!mmc_cache_enabled(host)) {
				blk_mq_end_request(req, BLK_STS_OK);
				return MMC_REQ_FINISHED;
			}
			ret = mmc_blk_cqe_issue_flush(mq, req);
			break;
		case REQ_OP_READ:
		case REQ_OP_WRITE:
			if (host->cqe_enabled)
				ret = mmc_blk_cqe_issue_rw_rq(mq, req);
			else
				ret = mmc_blk_mq_issue_rw_rq(mq, req);
			break;
		default:
			WARN_ON_ONCE(1);
			ret = -EINVAL;
		}
		if (!ret)
			return MMC_REQ_STARTED;
		return ret == -EBUSY ? MMC_REQ_BUSY : MMC_REQ_FAILED_TO_START;
	default:
		WARN_ON_ONCE(1);
		return MMC_REQ_FAILED_TO_START;
	}
}

static inline int mmc_blk_readonly(struct mmc_card *card)
{
	return mmc_card_readonly(card) ||
	       !(card->csd.cmdclass & CCC_BLOCK_WRITE);
}

static struct mmc_blk_data *mmc_blk_alloc_req(struct mmc_card *card,
					      struct device *parent,
					      sector_t size,
					      bool default_ro,
					      const char *subname,
					      int area_type,
					      unsigned int part_type)
{
	struct mmc_blk_data *md;
	int devidx, ret;
	char cap_str[10];
	bool cache_enabled = false;
	bool fua_enabled = false;

	devidx = ida_simple_get(&mmc_blk_ida, 0, max_devices, GFP_KERNEL);
	if (devidx < 0) {
		/*
		 * We get -ENOSPC because there are no more any available
		 * devidx. The reason may be that, either userspace haven't yet
		 * unmounted the partitions, which postpones mmc_blk_release()
		 * from being called, or the device has more partitions than
		 * what we support.
		 */
		if (devidx == -ENOSPC)
			dev_err(mmc_dev(card->host),
				"no more device IDs available\n");

		return ERR_PTR(devidx);
	}

	md = kzalloc(sizeof(struct mmc_blk_data), GFP_KERNEL);
	if (!md) {
		ret = -ENOMEM;
		goto out;
	}

	md->area_type = area_type;

	/*
	 * Set the read-only status based on the supported commands
	 * and the write protect switch.
	 */
	md->read_only = mmc_blk_readonly(card);

	md->disk = mmc_init_queue(&md->queue, card);
	if (IS_ERR(md->disk)) {
		ret = PTR_ERR(md->disk);
		goto err_kfree;
	}

	INIT_LIST_HEAD(&md->part);
	INIT_LIST_HEAD(&md->rpmbs);
	kref_init(&md->kref);

	md->queue.blkdata = md;
	md->part_type = part_type;

	md->disk->major	= MMC_BLOCK_MAJOR;
	md->disk->minors = perdev_minors;
	md->disk->first_minor = devidx * perdev_minors;
	md->disk->fops = &mmc_bdops;
	md->disk->private_data = md;
	md->parent = parent;
	set_disk_ro(md->disk, md->read_only || default_ro);
	if (area_type & (MMC_BLK_DATA_AREA_RPMB | MMC_BLK_DATA_AREA_BOOT))
		md->disk->flags |= GENHD_FL_NO_PART;

	/*
	 * As discussed on lkml, GENHD_FL_REMOVABLE should:
	 *
	 * - be set for removable media with permanent block devices
	 * - be unset for removable block devices with permanent media
	 *
	 * Since MMC block devices clearly fall under the second
	 * case, we do not set GENHD_FL_REMOVABLE.  Userspace
	 * should use the block device creation/destruction hotplug
	 * messages to tell when the card is present.
	 */

	snprintf(md->disk->disk_name, sizeof(md->disk->disk_name),
		 "mmcblk%u%s", card->host->index, subname ? subname : "");

	set_capacity(md->disk, size);

	if (mmc_host_cmd23(card->host)) {
		if ((mmc_card_mmc(card) &&
		     card->csd.mmca_vsn >= CSD_SPEC_VER_3) ||
		    (mmc_card_sd(card) &&
		     card->scr.cmds & SD_SCR_CMD23_SUPPORT))
			md->flags |= MMC_BLK_CMD23;
	}

	if (md->flags & MMC_BLK_CMD23 &&
	    ((card->ext_csd.rel_param & EXT_CSD_WR_REL_PARAM_EN) ||
	     card->ext_csd.rel_sectors)) {
		md->flags |= MMC_BLK_REL_WR;
		fua_enabled = true;
		cache_enabled = true;
	}
	if (mmc_cache_enabled(card->host))
		cache_enabled  = true;

	blk_queue_write_cache(md->queue.queue, cache_enabled, fua_enabled);

	string_get_size((u64)size, 512, STRING_UNITS_2,
			cap_str, sizeof(cap_str));
	pr_info("%s: %s %s %s %s\n",
		md->disk->disk_name, mmc_card_id(card), mmc_card_name(card),
		cap_str, md->read_only ? "(ro)" : "");

	/* used in ->open, must be set before add_disk: */
	if (area_type == MMC_BLK_DATA_AREA_MAIN)
		dev_set_drvdata(&card->dev, md);
	ret = device_add_disk(md->parent, md->disk, mmc_disk_attr_groups);
	if (ret)
		goto err_put_disk;
	return md;

 err_put_disk:
	put_disk(md->disk);
	blk_mq_free_tag_set(&md->queue.tag_set);
 err_kfree:
	kfree(md);
 out:
	ida_simple_remove(&mmc_blk_ida, devidx);
	return ERR_PTR(ret);
}

static struct mmc_blk_data *mmc_blk_alloc(struct mmc_card *card)
{
	sector_t size;

	if (!mmc_card_sd(card) && mmc_card_blockaddr(card)) {
		/*
		 * The EXT_CSD sector count is in number or 512 byte
		 * sectors.
		 */
		size = card->ext_csd.sectors;
	} else {
		/*
		 * The CSD capacity field is in units of read_blkbits.
		 * set_capacity takes units of 512 bytes.
		 */
		size = (typeof(sector_t))card->csd.capacity
			<< (card->csd.read_blkbits - 9);
	}

	return mmc_blk_alloc_req(card, &card->dev, size, false, NULL,
					MMC_BLK_DATA_AREA_MAIN, 0);
}

static int mmc_blk_alloc_part(struct mmc_card *card,
			      struct mmc_blk_data *md,
			      unsigned int part_type,
			      sector_t size,
			      bool default_ro,
			      const char *subname,
			      int area_type)
{
	struct mmc_blk_data *part_md;

	part_md = mmc_blk_alloc_req(card, disk_to_dev(md->disk), size, default_ro,
				    subname, area_type, part_type);
	if (IS_ERR(part_md))
		return PTR_ERR(part_md);
	list_add(&part_md->part, &md->part);

	return 0;
}

/**
 * mmc_rpmb_ioctl() - ioctl handler for the RPMB chardev
 * @filp: the character device file
 * @cmd: the ioctl() command
 * @arg: the argument from userspace
 *
 * This will essentially just redirect the ioctl()s coming in over to
 * the main block device spawning the RPMB character device.
 */
static long mmc_rpmb_ioctl(struct file *filp, unsigned int cmd,
			   unsigned long arg)
{
	struct mmc_rpmb_data *rpmb = filp->private_data;
	int ret;

	switch (cmd) {
	case MMC_IOC_CMD:
		ret = mmc_blk_ioctl_cmd(rpmb->md,
					(struct mmc_ioc_cmd __user *)arg,
					rpmb);
		break;
	case MMC_IOC_MULTI_CMD:
		ret = mmc_blk_ioctl_multi_cmd(rpmb->md,
					(struct mmc_ioc_multi_cmd __user *)arg,
					rpmb);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long mmc_rpmb_ioctl_compat(struct file *filp, unsigned int cmd,
			      unsigned long arg)
{
	return mmc_rpmb_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static int mmc_rpmb_chrdev_open(struct inode *inode, struct file *filp)
{
	struct mmc_rpmb_data *rpmb = container_of(inode->i_cdev,
						  struct mmc_rpmb_data, chrdev);

	get_device(&rpmb->dev);
	filp->private_data = rpmb;
	mmc_blk_get(rpmb->md->disk);

	return nonseekable_open(inode, filp);
}

static int mmc_rpmb_chrdev_release(struct inode *inode, struct file *filp)
{
	struct mmc_rpmb_data *rpmb = container_of(inode->i_cdev,
						  struct mmc_rpmb_data, chrdev);

	mmc_blk_put(rpmb->md);
	put_device(&rpmb->dev);

	return 0;
}

static const struct file_operations mmc_rpmb_fileops = {
	.release = mmc_rpmb_chrdev_release,
	.open = mmc_rpmb_chrdev_open,
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.unlocked_ioctl = mmc_rpmb_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = mmc_rpmb_ioctl_compat,
#endif
};

static void mmc_blk_rpmb_device_release(struct device *dev)
{
	struct mmc_rpmb_data *rpmb = dev_get_drvdata(dev);

	ida_simple_remove(&mmc_rpmb_ida, rpmb->id);
	kfree(rpmb);
}

static int mmc_blk_alloc_rpmb_part(struct mmc_card *card,
				   struct mmc_blk_data *md,
				   unsigned int part_index,
				   sector_t size,
				   const char *subname)
{
	int devidx, ret;
	char rpmb_name[DISK_NAME_LEN];
	char cap_str[10];
	struct mmc_rpmb_data *rpmb;

	/* This creates the minor number for the RPMB char device */
	devidx = ida_simple_get(&mmc_rpmb_ida, 0, max_devices, GFP_KERNEL);
	if (devidx < 0)
		return devidx;

	rpmb = kzalloc(sizeof(*rpmb), GFP_KERNEL);
	if (!rpmb) {
		ida_simple_remove(&mmc_rpmb_ida, devidx);
		return -ENOMEM;
	}

	snprintf(rpmb_name, sizeof(rpmb_name),
		 "mmcblk%u%s", card->host->index, subname ? subname : "");

	rpmb->id = devidx;
	rpmb->part_index = part_index;
	rpmb->dev.init_name = rpmb_name;
	rpmb->dev.bus = &mmc_rpmb_bus_type;
	rpmb->dev.devt = MKDEV(MAJOR(mmc_rpmb_devt), rpmb->id);
	rpmb->dev.parent = &card->dev;
	rpmb->dev.release = mmc_blk_rpmb_device_release;
	device_initialize(&rpmb->dev);
	dev_set_drvdata(&rpmb->dev, rpmb);
	rpmb->md = md;

	cdev_init(&rpmb->chrdev, &mmc_rpmb_fileops);
	rpmb->chrdev.owner = THIS_MODULE;
	ret = cdev_device_add(&rpmb->chrdev, &rpmb->dev);
	if (ret) {
		pr_err("%s: could not add character device\n", rpmb_name);
		goto out_put_device;
	}

	list_add(&rpmb->node, &md->rpmbs);

	string_get_size((u64)size, 512, STRING_UNITS_2,
			cap_str, sizeof(cap_str));

	pr_info("%s: %s %s %s, chardev (%d:%d)\n",
		rpmb_name, mmc_card_id(card), mmc_card_name(card), cap_str,
		MAJOR(mmc_rpmb_devt), rpmb->id);

	return 0;

out_put_device:
	put_device(&rpmb->dev);
	return ret;
}

static void mmc_blk_remove_rpmb_part(struct mmc_rpmb_data *rpmb)

{
	cdev_device_del(&rpmb->chrdev, &rpmb->dev);
	put_device(&rpmb->dev);
}

/* MMC Physical partitions consist of two boot partitions and
 * up to four general purpose partitions.
 * For each partition enabled in EXT_CSD a block device will be allocatedi
 * to provide access to the partition.
 */

static int mmc_blk_alloc_parts(struct mmc_card *card, struct mmc_blk_data *md)
{
	int idx, ret;

	if (!mmc_card_mmc(card))
		return 0;

	for (idx = 0; idx < card->nr_parts; idx++) {
		if (card->part[idx].area_type & MMC_BLK_DATA_AREA_RPMB) {
			/*
			 * RPMB partitions does not provide block access, they
			 * are only accessed using ioctl():s. Thus create
			 * special RPMB block devices that do not have a
			 * backing block queue for these.
			 */
			ret = mmc_blk_alloc_rpmb_part(card, md,
				card->part[idx].part_cfg,
				card->part[idx].size >> 9,
				card->part[idx].name);
			if (ret)
				return ret;
		} else if (card->part[idx].size) {
			ret = mmc_blk_alloc_part(card, md,
				card->part[idx].part_cfg,
				card->part[idx].size >> 9,
				card->part[idx].force_ro,
				card->part[idx].name,
				card->part[idx].area_type);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static void mmc_blk_remove_req(struct mmc_blk_data *md)
{
	/*
	 * Flush remaining requests and free queues. It is freeing the queue
	 * that stops new requests from being accepted.
	 */
	del_gendisk(md->disk);
	mmc_cleanup_queue(&md->queue);
	mmc_blk_put(md);
}

static void mmc_blk_remove_parts(struct mmc_card *card,
				 struct mmc_blk_data *md)
{
	struct list_head *pos, *q;
	struct mmc_blk_data *part_md;
	struct mmc_rpmb_data *rpmb;

	/* Remove RPMB partitions */
	list_for_each_safe(pos, q, &md->rpmbs) {
		rpmb = list_entry(pos, struct mmc_rpmb_data, node);
		list_del(pos);
		mmc_blk_remove_rpmb_part(rpmb);
	}
	/* Remove block partitions */
	list_for_each_safe(pos, q, &md->part) {
		part_md = list_entry(pos, struct mmc_blk_data, part);
		list_del(pos);
		mmc_blk_remove_req(part_md);
	}
}

#ifdef CONFIG_DEBUG_FS

static int mmc_dbg_card_status_get(void *data, u64 *val)
{
	struct mmc_card *card = data;
	struct mmc_blk_data *md = dev_get_drvdata(&card->dev);
	struct mmc_queue *mq = &md->queue;
	struct request *req;
	int ret;

	/* Ask the block layer about the card status */
	req = blk_mq_alloc_request(mq->queue, REQ_OP_DRV_IN, 0);
	if (IS_ERR(req))
		return PTR_ERR(req);
	req_to_mmc_queue_req(req)->drv_op = MMC_DRV_OP_GET_CARD_STATUS;
	blk_execute_rq(req, false);
	ret = req_to_mmc_queue_req(req)->drv_op_result;
	if (ret >= 0) {
		*val = ret;
		ret = 0;
	}
	blk_mq_free_request(req);

	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(mmc_dbg_card_status_fops, mmc_dbg_card_status_get,
			 NULL, "%08llx\n");

/* That is two digits * 512 + 1 for newline */
#define EXT_CSD_STR_LEN 1025

static int mmc_ext_csd_open(struct inode *inode, struct file *filp)
{
	struct mmc_card *card = inode->i_private;
	struct mmc_blk_data *md = dev_get_drvdata(&card->dev);
	struct mmc_queue *mq = &md->queue;
	struct request *req;
	char *buf;
	ssize_t n = 0;
	u8 *ext_csd;
	int err, i;

	buf = kmalloc(EXT_CSD_STR_LEN + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Ask the block layer for the EXT CSD */
	req = blk_mq_alloc_request(mq->queue, REQ_OP_DRV_IN, 0);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto out_free;
	}
	req_to_mmc_queue_req(req)->drv_op = MMC_DRV_OP_GET_EXT_CSD;
	req_to_mmc_queue_req(req)->drv_op_data = &ext_csd;
	blk_execute_rq(req, false);
	err = req_to_mmc_queue_req(req)->drv_op_result;
	blk_mq_free_request(req);
	if (err) {
		pr_err("FAILED %d\n", err);
		goto out_free;
	}

	for (i = 0; i < 512; i++)
		n += sprintf(buf + n, "%02x", ext_csd[i]);
	n += sprintf(buf + n, "\n");

	if (n != EXT_CSD_STR_LEN) {
		err = -EINVAL;
		kfree(ext_csd);
		goto out_free;
	}

	filp->private_data = buf;
	kfree(ext_csd);
	return 0;

out_free:
	kfree(buf);
	return err;
}

static ssize_t mmc_ext_csd_read(struct file *filp, char __user *ubuf,
				size_t cnt, loff_t *ppos)
{
	char *buf = filp->private_data;

	return simple_read_from_buffer(ubuf, cnt, ppos,
				       buf, EXT_CSD_STR_LEN);
}

static int mmc_ext_csd_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

static const struct file_operations mmc_dbg_ext_csd_fops = {
	.open		= mmc_ext_csd_open,
	.read		= mmc_ext_csd_read,
	.release	= mmc_ext_csd_release,
	.llseek		= default_llseek,
};

static int mmc_blk_add_debugfs(struct mmc_card *card, struct mmc_blk_data *md)
{
	struct dentry *root;

	if (!card->debugfs_root)
		return 0;

	root = card->debugfs_root;

	if (mmc_card_mmc(card) || mmc_card_sd(card)) {
		md->status_dentry =
			debugfs_create_file_unsafe("status", 0400, root,
						   card,
						   &mmc_dbg_card_status_fops);
		if (!md->status_dentry)
			return -EIO;
	}

	if (mmc_card_mmc(card)) {
		md->ext_csd_dentry =
			debugfs_create_file("ext_csd", S_IRUSR, root, card,
					    &mmc_dbg_ext_csd_fops);
		if (!md->ext_csd_dentry)
			return -EIO;
	}

	return 0;
}

static void mmc_blk_remove_debugfs(struct mmc_card *card,
				   struct mmc_blk_data *md)
{
	if (!card->debugfs_root)
		return;

	if (!IS_ERR_OR_NULL(md->status_dentry)) {
		debugfs_remove(md->status_dentry);
		md->status_dentry = NULL;
	}

	if (!IS_ERR_OR_NULL(md->ext_csd_dentry)) {
		debugfs_remove(md->ext_csd_dentry);
		md->ext_csd_dentry = NULL;
	}
}

#else

static int mmc_blk_add_debugfs(struct mmc_card *card, struct mmc_blk_data *md)
{
	return 0;
}

static void mmc_blk_remove_debugfs(struct mmc_card *card,
				   struct mmc_blk_data *md)
{
}

#endif /* CONFIG_DEBUG_FS */

static int mmc_blk_probe(struct mmc_card *card)
{
	struct mmc_blk_data *md;
	int ret = 0;

	/*
	 * Check that the card supports the command class(es) we need.
	 */
	if (!(card->csd.cmdclass & CCC_BLOCK_READ))
		return -ENODEV;

	mmc_fixup_device(card, mmc_blk_fixups);

	card->complete_wq = alloc_workqueue("mmc_complete",
					WQ_MEM_RECLAIM | WQ_HIGHPRI, 0);
	if (!card->complete_wq) {
		pr_err("Failed to create mmc completion workqueue");
		return -ENOMEM;
	}

	md = mmc_blk_alloc(card);
	if (IS_ERR(md)) {
		ret = PTR_ERR(md);
		goto out_free;
	}

	ret = mmc_blk_alloc_parts(card, md);
	if (ret)
		goto out;

	/* Add two debugfs entries */
	mmc_blk_add_debugfs(card, md);

	pm_runtime_set_autosuspend_delay(&card->dev, 3000);
	pm_runtime_use_autosuspend(&card->dev);

	/*
	 * Don't enable runtime PM for SD-combo cards here. Leave that
	 * decision to be taken during the SDIO init sequence instead.
	 */
	if (!mmc_card_sd_combo(card)) {
		pm_runtime_set_active(&card->dev);
		pm_runtime_enable(&card->dev);
	}

	return 0;

out:
	mmc_blk_remove_parts(card, md);
	mmc_blk_remove_req(md);
out_free:
	destroy_workqueue(card->complete_wq);
	return ret;
}

static void mmc_blk_remove(struct mmc_card *card)
{
	struct mmc_blk_data *md = dev_get_drvdata(&card->dev);

	mmc_blk_remove_debugfs(card, md);
	mmc_blk_remove_parts(card, md);
	pm_runtime_get_sync(&card->dev);
	if (md->part_curr != md->part_type) {
		mmc_claim_host(card->host);
		mmc_blk_part_switch(card, md->part_type);
		mmc_release_host(card->host);
	}
	if (!mmc_card_sd_combo(card))
		pm_runtime_disable(&card->dev);
	pm_runtime_put_noidle(&card->dev);
	mmc_blk_remove_req(md);
	dev_set_drvdata(&card->dev, NULL);
	destroy_workqueue(card->complete_wq);
}

static int _mmc_blk_suspend(struct mmc_card *card)
{
	struct mmc_blk_data *part_md;
	struct mmc_blk_data *md = dev_get_drvdata(&card->dev);

	if (md) {
		mmc_queue_suspend(&md->queue);
		list_for_each_entry(part_md, &md->part, part) {
			mmc_queue_suspend(&part_md->queue);
		}
	}
	return 0;
}

static void mmc_blk_shutdown(struct mmc_card *card)
{
	_mmc_blk_suspend(card);
}

#ifdef CONFIG_PM_SLEEP
static int mmc_blk_suspend(struct device *dev)
{
	struct mmc_card *card = mmc_dev_to_card(dev);

	return _mmc_blk_suspend(card);
}

static int mmc_blk_resume(struct device *dev)
{
	struct mmc_blk_data *part_md;
	struct mmc_blk_data *md = dev_get_drvdata(dev);

	if (md) {
		/*
		 * Resume involves the card going into idle state,
		 * so current partition is always the main one.
		 */
		md->part_curr = md->part_type;
		mmc_queue_resume(&md->queue);
		list_for_each_entry(part_md, &md->part, part) {
			mmc_queue_resume(&part_md->queue);
		}
	}
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(mmc_blk_pm_ops, mmc_blk_suspend, mmc_blk_resume);

static struct mmc_driver mmc_driver = {
	.drv		= {
		.name	= "mmcblk",
		.pm	= &mmc_blk_pm_ops,
	},
	.probe		= mmc_blk_probe,
	.remove		= mmc_blk_remove,
	.shutdown	= mmc_blk_shutdown,
};

static int __init mmc_blk_init(void)
{
	int res;

	res  = bus_register(&mmc_rpmb_bus_type);
	if (res < 0) {
		pr_err("mmcblk: could not register RPMB bus type\n");
		return res;
	}
	res = alloc_chrdev_region(&mmc_rpmb_devt, 0, MAX_DEVICES, "rpmb");
	if (res < 0) {
		pr_err("mmcblk: failed to allocate rpmb chrdev region\n");
		goto out_bus_unreg;
	}

	if (perdev_minors != CONFIG_MMC_BLOCK_MINORS)
		pr_info("mmcblk: using %d minors per device\n", perdev_minors);

	max_devices = min(MAX_DEVICES, (1 << MINORBITS) / perdev_minors);

	res = register_blkdev(MMC_BLOCK_MAJOR, "mmc");
	if (res)
		goto out_chrdev_unreg;

	res = mmc_register_driver(&mmc_driver);
	if (res)
		goto out_blkdev_unreg;

	return 0;

out_blkdev_unreg:
	unregister_blkdev(MMC_BLOCK_MAJOR, "mmc");
out_chrdev_unreg:
	unregister_chrdev_region(mmc_rpmb_devt, MAX_DEVICES);
out_bus_unreg:
	bus_unregister(&mmc_rpmb_bus_type);
	return res;
}

static void __exit mmc_blk_exit(void)
{
	mmc_unregister_driver(&mmc_driver);
	unregister_blkdev(MMC_BLOCK_MAJOR, "mmc");
	unregister_chrdev_region(mmc_rpmb_devt, MAX_DEVICES);
	bus_unregister(&mmc_rpmb_bus_type);
}

module_init(mmc_blk_init);
module_exit(mmc_blk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Multimedia Card (MMC) block device driver");

