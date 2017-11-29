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

#define MMC_BLK_TIMEOUT_MS  (10 * 60 * 1000)        /* 10 minute timeout */
#define MMC_SANITIZE_REQ_TIMEOUT 240000
#define MMC_EXTRACT_INDEX_FROM_ARG(x) ((x & 0x00FF0000) >> 16)

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

/*
 * There is one mmc_blk_data per slot.
 */
struct mmc_blk_data {
	spinlock_t	lock;
	struct device	*parent;
	struct gendisk	*disk;
	struct mmc_queue queue;
	struct list_head part;
	struct list_head rpmbs;

	unsigned int	flags;
#define MMC_BLK_CMD23	(1 << 0)	/* Can do SET_BLOCK_COUNT for multiblock */
#define MMC_BLK_REL_WR	(1 << 1)	/* MMC Reliable write support */

	unsigned int	usage;
	unsigned int	read_only;
	unsigned int	part_type;
	unsigned int	reset_done;
#define MMC_BLK_READ		BIT(0)
#define MMC_BLK_WRITE		BIT(1)
#define MMC_BLK_DISCARD		BIT(2)
#define MMC_BLK_SECDISCARD	BIT(3)

	/*
	 * Only set in main mmc_blk_data associated
	 * with mmc_card with dev_set_drvdata, and keeps
	 * track of the current selected device partition.
	 */
	unsigned int	part_curr;
	struct device_attribute force_ro;
	struct device_attribute power_ro_lock;
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

static struct mmc_blk_data *mmc_blk_get(struct gendisk *disk)
{
	struct mmc_blk_data *md;

	mutex_lock(&open_lock);
	md = disk->private_data;
	if (md && md->usage == 0)
		md = NULL;
	if (md)
		md->usage++;
	mutex_unlock(&open_lock);

	return md;
}

static inline int mmc_get_devidx(struct gendisk *disk)
{
	int devidx = disk->first_minor / perdev_minors;
	return devidx;
}

static void mmc_blk_put(struct mmc_blk_data *md)
{
	mutex_lock(&open_lock);
	md->usage--;
	if (md->usage == 0) {
		int devidx = mmc_get_devidx(md->disk);
		blk_put_queue(md->queue.queue);
		ida_simple_remove(&mmc_blk_ida, devidx);
		put_disk(md->disk);
		kfree(md);
	}
	mutex_unlock(&open_lock);
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
	req = blk_get_request(mq->queue, REQ_OP_DRV_OUT, __GFP_RECLAIM);
	if (IS_ERR(req)) {
		count = PTR_ERR(req);
		goto out_put;
	}
	req_to_mmc_queue_req(req)->drv_op = MMC_DRV_OP_BOOT_WP;
	blk_execute_rq(mq->queue, NULL, req, 0);
	ret = req_to_mmc_queue_req(req)->drv_op_result;
	blk_put_request(req);

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

static int mmc_blk_open(struct block_device *bdev, fmode_t mode)
{
	struct mmc_blk_data *md = mmc_blk_get(bdev->bd_disk);
	int ret = -ENXIO;

	mutex_lock(&block_mutex);
	if (md) {
		if (md->usage == 2)
			check_disk_change(bdev);
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

	idata->buf = kmalloc(idata->buf_bytes, GFP_KERNEL);
	if (!idata->buf) {
		err = -ENOMEM;
		goto idata_err;
	}

	if (copy_from_user(idata->buf, (void __user *)(unsigned long)
					idata->ic.data_ptr, idata->buf_bytes)) {
		err = -EFAULT;
		goto copy_err;
	}

	return idata;

copy_err:
	kfree(idata->buf);
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

static int ioctl_rpmb_card_status_poll(struct mmc_card *card, u32 *status,
				       u32 retries_max)
{
	int err;
	u32 retry_count = 0;

	if (!status || !retries_max)
		return -EINVAL;

	do {
		err = __mmc_send_status(card, status, 5);
		if (err)
			break;

		if (!R1_STATUS(*status) &&
				(R1_CURRENT_STATE(*status) != R1_STATE_PRG))
			break; /* RPMB programming operation complete */

		/*
		 * Rechedule to give the MMC device a chance to continue
		 * processing the previous command without being polled too
		 * frequently.
		 */
		usleep_range(1000, 5000);
	} while (++retry_count < retries_max);

	if (retry_count == retries_max)
		err = -EPERM;

	return err;
}

static int ioctl_do_sanitize(struct mmc_card *card)
{
	int err;

	if (!mmc_can_sanitize(card)) {
			pr_warn("%s: %s - SANITIZE is not supported\n",
				mmc_hostname(card->host), __func__);
			err = -EOPNOTSUPP;
			goto out;
	}

	pr_debug("%s: %s - SANITIZE IN PROGRESS...\n",
		mmc_hostname(card->host), __func__);

	err = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL,
					EXT_CSD_SANITIZE_START, 1,
					MMC_SANITIZE_REQ_TIMEOUT);

	if (err)
		pr_err("%s: %s - EXT_CSD_SANITIZE_START failed. err=%d\n",
		       mmc_hostname(card->host), __func__, err);

	pr_debug("%s: %s - SANITIZE COMPLETED\n", mmc_hostname(card->host),
					     __func__);
out:
	return err;
}

static int __mmc_blk_ioctl_cmd(struct mmc_card *card, struct mmc_blk_data *md,
			       struct mmc_blk_ioc_data *idata)
{
	struct mmc_command cmd = {};
	struct mmc_data data = {};
	struct mmc_request mrq = {};
	struct scatterlist sg;
	int err;
	unsigned int target_part;
	u32 status = 0;

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

		if ((cmd.flags & MMC_RSP_R1B) == MMC_RSP_R1B) {
			/*
			 * Pretend this is a data transfer and rely on the
			 * host driver to compute timeout.  When all host
			 * drivers support cmd.cmd_timeout for R1B, this
			 * can be changed to:
			 *
			 *     mrq.data = NULL;
			 *     cmd.cmd_timeout = idata->ic.cmd_timeout_ms;
			 */
			data.timeout_ns = idata->ic.cmd_timeout_ms * 1000000;
		}

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
		err = mmc_set_blockcount(card, data.blocks,
			idata->ic.write_flag & (1 << 31));
		if (err)
			return err;
	}

	if ((MMC_EXTRACT_INDEX_FROM_ARG(cmd.arg) == EXT_CSD_SANITIZE_START) &&
	    (cmd.opcode == MMC_SWITCH)) {
		err = ioctl_do_sanitize(card);

		if (err)
			pr_err("%s: ioctl_do_sanitize() failed. err = %d",
			       __func__, err);

		return err;
	}

	mmc_wait_for_req(card->host, &mrq);

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
	 * According to the SD specs, some commands require a delay after
	 * issuing the command.
	 */
	if (idata->ic.postsleep_min_us)
		usleep_range(idata->ic.postsleep_min_us, idata->ic.postsleep_max_us);

	memcpy(&(idata->ic.response), cmd.resp, sizeof(cmd.resp));

	if (idata->rpmb) {
		/*
		 * Ensure RPMB command has completed by polling CMD13
		 * "Send Status".
		 */
		err = ioctl_rpmb_card_status_poll(card, &status, 5);
		if (err)
			dev_err(mmc_dev(card->host),
					"%s: Card Status=0x%08X, error %d\n",
					__func__, status, err);
	}

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
	req = blk_get_request(mq->queue,
		idata->ic.write_flag ? REQ_OP_DRV_OUT : REQ_OP_DRV_IN,
		__GFP_RECLAIM);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto cmd_done;
	}
	idatas[0] = idata;
	req_to_mmc_queue_req(req)->drv_op =
		rpmb ? MMC_DRV_OP_IOCTL_RPMB : MMC_DRV_OP_IOCTL;
	req_to_mmc_queue_req(req)->drv_op_data = idatas;
	req_to_mmc_queue_req(req)->ioc_count = 1;
	blk_execute_rq(mq->queue, NULL, req, 0);
	ioc_err = req_to_mmc_queue_req(req)->drv_op_result;
	err = mmc_blk_ioctl_copy_to_user(ic_ptr, idata);
	blk_put_request(req);

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
	int i, err = 0, ioc_err = 0;
	__u64 num_of_cmds;
	struct request *req;

	if (copy_from_user(&num_of_cmds, &user->num_of_cmds,
			   sizeof(num_of_cmds)))
		return -EFAULT;

	if (!num_of_cmds)
		return 0;

	if (num_of_cmds > MMC_IOC_MAX_CMDS)
		return -EINVAL;

	idata = kcalloc(num_of_cmds, sizeof(*idata), GFP_KERNEL);
	if (!idata)
		return -ENOMEM;

	for (i = 0; i < num_of_cmds; i++) {
		idata[i] = mmc_blk_ioctl_copy_from_user(&cmds[i]);
		if (IS_ERR(idata[i])) {
			err = PTR_ERR(idata[i]);
			num_of_cmds = i;
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
	req = blk_get_request(mq->queue,
		idata[0]->ic.write_flag ? REQ_OP_DRV_OUT : REQ_OP_DRV_IN,
		__GFP_RECLAIM);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto cmd_err;
	}
	req_to_mmc_queue_req(req)->drv_op =
		rpmb ? MMC_DRV_OP_IOCTL_RPMB : MMC_DRV_OP_IOCTL;
	req_to_mmc_queue_req(req)->drv_op_data = idata;
	req_to_mmc_queue_req(req)->ioc_count = num_of_cmds;
	blk_execute_rq(mq->queue, NULL, req, 0);
	ioc_err = req_to_mmc_queue_req(req)->drv_op_result;

	/* copy to user if data and response */
	for (i = 0; i < num_of_cmds && !err; i++)
		err = mmc_blk_ioctl_copy_to_user(&cmds[i], idata[i]);

	blk_put_request(req);

cmd_err:
	for (i = 0; i < num_of_cmds; i++) {
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
	if ((!capable(CAP_SYS_RAWIO)) || (bdev != bdev->bd_contains))
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

static const struct block_device_operations mmc_bdops = {
	.open			= mmc_blk_open,
	.release		= mmc_blk_release,
	.getgeo			= mmc_blk_getgeo,
	.owner			= THIS_MODULE,
	.ioctl			= mmc_blk_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl		= mmc_blk_compat_ioctl,
#endif
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

static int card_busy_detect(struct mmc_card *card, unsigned int timeout_ms,
		bool hw_busy_detect, struct request *req, bool *gen_err)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(timeout_ms);
	int err = 0;
	u32 status;

	do {
		err = __mmc_send_status(card, &status, 5);
		if (err) {
			pr_err("%s: error %d requesting status\n",
			       req->rq_disk->disk_name, err);
			return err;
		}

		if (status & R1_ERROR) {
			pr_err("%s: %s: error sending status cmd, status %#x\n",
				req->rq_disk->disk_name, __func__, status);
			*gen_err = true;
		}

		/* We may rely on the host hw to handle busy detection.*/
		if ((card->host->caps & MMC_CAP_WAIT_WHILE_BUSY) &&
			hw_busy_detect)
			break;

		/*
		 * Timeout if the device never becomes ready for data and never
		 * leaves the program state.
		 */
		if (time_after(jiffies, timeout)) {
			pr_err("%s: Card stuck in programming state! %s %s\n",
				mmc_hostname(card->host),
				req->rq_disk->disk_name, __func__);
			return -ETIMEDOUT;
		}

		/*
		 * Some cards mishandle the status bits,
		 * so make sure to check both the busy
		 * indication and the card state.
		 */
	} while (!(status & R1_READY_FOR_DATA) ||
		 (R1_CURRENT_STATE(status) == R1_STATE_PRG));

	return err;
}

static int send_stop(struct mmc_card *card, unsigned int timeout_ms,
		struct request *req, bool *gen_err, u32 *stop_status)
{
	struct mmc_host *host = card->host;
	struct mmc_command cmd = {};
	int err;
	bool use_r1b_resp = rq_data_dir(req) == WRITE;

	/*
	 * Normally we use R1B responses for WRITE, but in cases where the host
	 * has specified a max_busy_timeout we need to validate it. A failure
	 * means we need to prevent the host from doing hw busy detection, which
	 * is done by converting to a R1 response instead.
	 */
	if (host->max_busy_timeout && (timeout_ms > host->max_busy_timeout))
		use_r1b_resp = false;

	cmd.opcode = MMC_STOP_TRANSMISSION;
	if (use_r1b_resp) {
		cmd.flags = MMC_RSP_SPI_R1B | MMC_RSP_R1B | MMC_CMD_AC;
		cmd.busy_timeout = timeout_ms;
	} else {
		cmd.flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_AC;
	}

	err = mmc_wait_for_cmd(host, &cmd, 5);
	if (err)
		return err;

	*stop_status = cmd.resp[0];

	/* No need to check card status in case of READ. */
	if (rq_data_dir(req) == READ)
		return 0;

	if (!mmc_host_is_spi(host) &&
		(*stop_status & R1_ERROR)) {
		pr_err("%s: %s: general error sending stop command, resp %#x\n",
			req->rq_disk->disk_name, __func__, *stop_status);
		*gen_err = true;
	}

	return card_busy_detect(card, timeout_ms, use_r1b_resp, req, gen_err);
}

#define ERR_NOMEDIUM	3
#define ERR_RETRY	2
#define ERR_ABORT	1
#define ERR_CONTINUE	0

static int mmc_blk_cmd_error(struct request *req, const char *name, int error,
	bool status_valid, u32 status)
{
	switch (error) {
	case -EILSEQ:
		/* response crc error, retry the r/w cmd */
		pr_err("%s: %s sending %s command, card status %#x\n",
			req->rq_disk->disk_name, "response CRC error",
			name, status);
		return ERR_RETRY;

	case -ETIMEDOUT:
		pr_err("%s: %s sending %s command, card status %#x\n",
			req->rq_disk->disk_name, "timed out", name, status);

		/* If the status cmd initially failed, retry the r/w cmd */
		if (!status_valid) {
			pr_err("%s: status not valid, retrying timeout\n",
				req->rq_disk->disk_name);
			return ERR_RETRY;
		}

		/*
		 * If it was a r/w cmd crc error, or illegal command
		 * (eg, issued in wrong state) then retry - we should
		 * have corrected the state problem above.
		 */
		if (status & (R1_COM_CRC_ERROR | R1_ILLEGAL_COMMAND)) {
			pr_err("%s: command error, retrying timeout\n",
				req->rq_disk->disk_name);
			return ERR_RETRY;
		}

		/* Otherwise abort the command */
		return ERR_ABORT;

	default:
		/* We don't understand the error code the driver gave us */
		pr_err("%s: unknown error %d sending read/write command, card status %#x\n",
		       req->rq_disk->disk_name, error, status);
		return ERR_ABORT;
	}
}

/*
 * Initial r/w and stop cmd error recovery.
 * We don't know whether the card received the r/w cmd or not, so try to
 * restore things back to a sane state.  Essentially, we do this as follows:
 * - Obtain card status.  If the first attempt to obtain card status fails,
 *   the status word will reflect the failed status cmd, not the failed
 *   r/w cmd.  If we fail to obtain card status, it suggests we can no
 *   longer communicate with the card.
 * - Check the card state.  If the card received the cmd but there was a
 *   transient problem with the response, it might still be in a data transfer
 *   mode.  Try to send it a stop command.  If this fails, we can't recover.
 * - If the r/w cmd failed due to a response CRC error, it was probably
 *   transient, so retry the cmd.
 * - If the r/w cmd timed out, but we didn't get the r/w cmd status, retry.
 * - If the r/w cmd timed out, and the r/w cmd failed due to CRC error or
 *   illegal cmd, retry.
 * Otherwise we don't understand what happened, so abort.
 */
static int mmc_blk_cmd_recovery(struct mmc_card *card, struct request *req,
	struct mmc_blk_request *brq, bool *ecc_err, bool *gen_err)
{
	bool prev_cmd_status_valid = true;
	u32 status, stop_status = 0;
	int err, retry;

	if (mmc_card_removed(card))
		return ERR_NOMEDIUM;

	/*
	 * Try to get card status which indicates both the card state
	 * and why there was no response.  If the first attempt fails,
	 * we can't be sure the returned status is for the r/w command.
	 */
	for (retry = 2; retry >= 0; retry--) {
		err = __mmc_send_status(card, &status, 0);
		if (!err)
			break;

		/* Re-tune if needed */
		mmc_retune_recheck(card->host);

		prev_cmd_status_valid = false;
		pr_err("%s: error %d sending status command, %sing\n",
		       req->rq_disk->disk_name, err, retry ? "retry" : "abort");
	}

	/* We couldn't get a response from the card.  Give up. */
	if (err) {
		/* Check if the card is removed */
		if (mmc_detect_card_removed(card->host))
			return ERR_NOMEDIUM;
		return ERR_ABORT;
	}

	/* Flag ECC errors */
	if ((status & R1_CARD_ECC_FAILED) ||
	    (brq->stop.resp[0] & R1_CARD_ECC_FAILED) ||
	    (brq->cmd.resp[0] & R1_CARD_ECC_FAILED))
		*ecc_err = true;

	/* Flag General errors */
	if (!mmc_host_is_spi(card->host) && rq_data_dir(req) != READ)
		if ((status & R1_ERROR) ||
			(brq->stop.resp[0] & R1_ERROR)) {
			pr_err("%s: %s: general error sending stop or status command, stop cmd response %#x, card status %#x\n",
			       req->rq_disk->disk_name, __func__,
			       brq->stop.resp[0], status);
			*gen_err = true;
		}

	/*
	 * Check the current card state.  If it is in some data transfer
	 * mode, tell it to stop (and hopefully transition back to TRAN.)
	 */
	if (R1_CURRENT_STATE(status) == R1_STATE_DATA ||
	    R1_CURRENT_STATE(status) == R1_STATE_RCV) {
		err = send_stop(card,
			DIV_ROUND_UP(brq->data.timeout_ns, 1000000),
			req, gen_err, &stop_status);
		if (err) {
			pr_err("%s: error %d sending stop command\n",
			       req->rq_disk->disk_name, err);
			/*
			 * If the stop cmd also timed out, the card is probably
			 * not present, so abort. Other errors are bad news too.
			 */
			return ERR_ABORT;
		}

		if (stop_status & R1_CARD_ECC_FAILED)
			*ecc_err = true;
	}

	/* Check for set block count errors */
	if (brq->sbc.error)
		return mmc_blk_cmd_error(req, "SET_BLOCK_COUNT", brq->sbc.error,
				prev_cmd_status_valid, status);

	/* Check for r/w command errors */
	if (brq->cmd.error)
		return mmc_blk_cmd_error(req, "r/w cmd", brq->cmd.error,
				prev_cmd_status_valid, status);

	/* Data errors */
	if (!brq->stop.error)
		return ERR_CONTINUE;

	/* Now for stop errors.  These aren't fatal to the transfer. */
	pr_info("%s: error %d sending stop command, original cmd response %#x, card status %#x\n",
	       req->rq_disk->disk_name, brq->stop.error,
	       brq->cmd.resp[0], status);

	/*
	 * Subsitute in our own stop status as this will give the error
	 * state which happened during the execution of the r/w command.
	 */
	if (stop_status) {
		brq->stop.resp[0] = stop_status;
		brq->stop.error = 0;
	}
	return ERR_CONTINUE;
}

static int mmc_blk_reset(struct mmc_blk_data *md, struct mmc_host *host,
			 int type)
{
	int err;

	if (md->reset_done & type)
		return -EEXIST;

	md->reset_done |= type;
	err = mmc_hw_reset(host);
	/* Ensure we switch back to the correct partition */
	if (err != -EOPNOTSUPP) {
		struct mmc_blk_data *main_md =
			dev_get_drvdata(&host->card->dev);
		int part_err;

		main_md->part_curr = main_md->part_type;
		part_err = mmc_blk_part_switch(host->card, md->part_type);
		if (part_err) {
			/*
			 * We have failed to get back into the correct
			 * partition, so we need to abort the whole request.
			 */
			return -ENODEV;
		}
	}
	return err;
}

static inline void mmc_blk_reset_success(struct mmc_blk_data *md, int type)
{
	md->reset_done &= ~type;
}

static void mmc_blk_end_request(struct request *req, blk_status_t error)
{
	if (req->mq_ctx)
		blk_mq_end_request(req, error);
	else
		blk_end_request_all(req, error);
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
	mmc_blk_end_request(req, ret ? BLK_STS_IOERR : BLK_STS_OK);
}

static void mmc_blk_issue_discard_rq(struct mmc_queue *mq, struct request *req)
{
	struct mmc_blk_data *md = mq->blkdata;
	struct mmc_card *card = md->queue.card;
	unsigned int from, nr, arg;
	int err = 0, type = MMC_BLK_DISCARD;
	blk_status_t status = BLK_STS_OK;

	if (!mmc_can_erase(card)) {
		status = BLK_STS_NOTSUPP;
		goto fail;
	}

	from = blk_rq_pos(req);
	nr = blk_rq_sectors(req);

	if (mmc_can_discard(card))
		arg = MMC_DISCARD_ARG;
	else if (mmc_can_trim(card))
		arg = MMC_TRIM_ARG;
	else
		arg = MMC_ERASE_ARG;
	do {
		err = 0;
		if (card->quirks & MMC_QUIRK_INAND_CMD38) {
			err = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL,
					 INAND_CMD38_ARG_EXT_CSD,
					 arg == MMC_TRIM_ARG ?
					 INAND_CMD38_ARG_TRIM :
					 INAND_CMD38_ARG_ERASE,
					 0);
		}
		if (!err)
			err = mmc_erase(card, from, nr, arg);
	} while (err == -EIO && !mmc_blk_reset(md, card->host, type));
	if (err)
		status = BLK_STS_IOERR;
	else
		mmc_blk_reset_success(md, type);
fail:
	mmc_blk_end_request(req, status);
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
				 0);
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
					 0);
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
	mmc_blk_end_request(req, status);
}

static void mmc_blk_issue_flush(struct mmc_queue *mq, struct request *req)
{
	struct mmc_blk_data *md = mq->blkdata;
	struct mmc_card *card = md->queue.card;
	int ret = 0;

	ret = mmc_flush_cache(card);
	mmc_blk_end_request(req, ret ? BLK_STS_IOERR : BLK_STS_OK);
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

#define CMD_ERRORS							\
	(R1_OUT_OF_RANGE |	/* Command argument out of range */	\
	 R1_ADDRESS_ERROR |	/* Misaligned address */		\
	 R1_BLOCK_LEN_ERROR |	/* Transferred block length incorrect */\
	 R1_WP_VIOLATION |	/* Tried to write to protected block */	\
	 R1_CARD_ECC_FAILED |	/* Card ECC failed */			\
	 R1_CC_ERROR |		/* Card controller error */		\
	 R1_ERROR)		/* General/unknown error */

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

static enum mmc_blk_status __mmc_blk_err_check(struct mmc_card *card,
					       struct mmc_queue_req *mq_mrq)
{
	struct mmc_blk_request *brq = &mq_mrq->brq;
	struct request *req = mmc_queue_req_to_req(mq_mrq);
	int need_retune = card->host->need_retune;
	bool ecc_err = false;
	bool gen_err = false;

	/*
	 * sbc.error indicates a problem with the set block count
	 * command.  No data will have been transferred.
	 *
	 * cmd.error indicates a problem with the r/w command.  No
	 * data will have been transferred.
	 *
	 * stop.error indicates a problem with the stop command.  Data
	 * may have been transferred, or may still be transferring.
	 */

	mmc_blk_eval_resp_error(brq);

	if (brq->sbc.error || brq->cmd.error ||
	    brq->stop.error || brq->data.error) {
		switch (mmc_blk_cmd_recovery(card, req, brq, &ecc_err, &gen_err)) {
		case ERR_RETRY:
			return MMC_BLK_RETRY;
		case ERR_ABORT:
			return MMC_BLK_ABORT;
		case ERR_NOMEDIUM:
			return MMC_BLK_NOMEDIUM;
		case ERR_CONTINUE:
			break;
		}
	}

	/*
	 * Check for errors relating to the execution of the
	 * initial command - such as address errors.  No data
	 * has been transferred.
	 */
	if (brq->cmd.resp[0] & CMD_ERRORS) {
		pr_err("%s: r/w command failed, status = %#x\n",
		       req->rq_disk->disk_name, brq->cmd.resp[0]);
		return MMC_BLK_ABORT;
	}

	/*
	 * Everything else is either success, or a data error of some
	 * kind.  If it was a write, we may have transitioned to
	 * program mode, which we have to wait for it to complete.
	 */
	if (!mmc_host_is_spi(card->host) && rq_data_dir(req) != READ) {
		int err;

		/* Check stop command response */
		if (brq->stop.resp[0] & R1_ERROR) {
			pr_err("%s: %s: general error sending stop command, stop cmd response %#x\n",
			       req->rq_disk->disk_name, __func__,
			       brq->stop.resp[0]);
			gen_err = true;
		}

		err = card_busy_detect(card, MMC_BLK_TIMEOUT_MS, false, req,
					&gen_err);
		if (err)
			return MMC_BLK_CMD_ERR;
	}

	/* if general error occurs, retry the write operation. */
	if (gen_err) {
		pr_warn("%s: retrying write for general error\n",
				req->rq_disk->disk_name);
		return MMC_BLK_RETRY;
	}

	/* Some errors (ECC) are flagged on the next commmand, so check stop, too */
	if (brq->data.error || brq->stop.error) {
		if (need_retune && !brq->retune_retry_done) {
			pr_debug("%s: retrying because a re-tune was needed\n",
				 req->rq_disk->disk_name);
			brq->retune_retry_done = 1;
			return MMC_BLK_RETRY;
		}
		pr_err("%s: error %d transferring data, sector %u, nr %u, cmd response %#x, card status %#x\n",
		       req->rq_disk->disk_name, brq->data.error ?: brq->stop.error,
		       (unsigned)blk_rq_pos(req),
		       (unsigned)blk_rq_sectors(req),
		       brq->cmd.resp[0], brq->stop.resp[0]);

		if (rq_data_dir(req) == READ) {
			if (ecc_err)
				return MMC_BLK_ECC_ERR;
			return MMC_BLK_DATA_ERR;
		} else {
			return MMC_BLK_CMD_ERR;
		}
	}

	if (!brq->data.bytes_xfered)
		return MMC_BLK_RETRY;

	if (blk_rq_bytes(req) != brq->data.bytes_xfered)
		return MMC_BLK_PARTIAL;

	return MMC_BLK_SUCCESS;
}

static enum mmc_blk_status mmc_blk_err_check(struct mmc_card *card,
					     struct mmc_async_req *areq)
{
	struct mmc_queue_req *mq_mrq = container_of(areq, struct mmc_queue_req,
						    areq);

	return __mmc_blk_err_check(card, mq_mrq);
}

static void mmc_blk_data_prep(struct mmc_queue *mq, struct mmc_queue_req *mqrq,
			      int disable_multi, bool *do_rel_wr_p,
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
		 * After a read error, we redo the request one sector
		 * at a time in order to accurately determine which
		 * sectors can be read successfully.
		 */
		if (disable_multi)
			brq->data.blocks = 1;

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

	mqrq->areq.mrq = &brq->mrq;

	if (do_rel_wr_p)
		*do_rel_wr_p = do_rel_wr;

	if (do_data_tag_p)
		*do_data_tag_p = do_data_tag;
}

static void mmc_blk_rw_rq_prep(struct mmc_queue_req *mqrq,
			       struct mmc_card *card,
			       int disable_multi,
			       struct mmc_queue *mq)
{
	u32 readcmd, writecmd;
	struct mmc_blk_request *brq = &mqrq->brq;
	struct request *req = mmc_queue_req_to_req(mqrq);
	struct mmc_blk_data *md = mq->blkdata;
	bool do_rel_wr, do_data_tag;

	mmc_blk_data_prep(mq, mqrq, disable_multi, &do_rel_wr, &do_data_tag);

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

	mqrq->areq.err_check = mmc_blk_err_check;
}

#define MMC_MAX_RETRIES		5
#define MMC_NO_RETRIES		(MMC_MAX_RETRIES + 1)

#define MMC_READ_SINGLE_RETRIES	2

/* Single sector read during recovery */
static void mmc_blk_read_single(struct mmc_queue *mq, struct request *req)
{
	struct mmc_queue_req *mqrq = req_to_mmc_queue_req(req);
	struct mmc_request *mrq = &mqrq->brq.mrq;
	struct mmc_card *card = mq->card;
	struct mmc_host *host = card->host;
	blk_status_t error = BLK_STS_OK;
	int retries = 0;

	do {
		u32 status;
		int err;

		mmc_blk_rw_rq_prep(mqrq, card, 1, mq);

		mmc_wait_for_req(host, mrq);

		err = mmc_send_status(card, &status);
		if (err)
			goto error_exit;

		if (!mmc_host_is_spi(host) &&
		    R1_CURRENT_STATE(status) != R1_STATE_TRAN) {
			u32 stop_status = 0;
			bool gen_err = false;

			err = send_stop(card,
					DIV_ROUND_UP(mrq->data->timeout_ns,
						     1000000),
					req, &gen_err, &stop_status);
			if (err)
				goto error_exit;
		}

		if (mrq->cmd->error && retries++ < MMC_READ_SINGLE_RETRIES)
			continue;

		retries = 0;

		if (mrq->cmd->error ||
		    mrq->data->error ||
		    (!mmc_host_is_spi(host) &&
		     (mrq->cmd->resp[0] & CMD_ERRORS || status & CMD_ERRORS)))
			error = BLK_STS_IOERR;
		else
			error = BLK_STS_OK;

	} while (blk_update_request(req, error, 512));

	return;

error_exit:
	mrq->data->bytes_xfered = 0;
	blk_update_request(req, BLK_STS_IOERR, 512);
	/* Let it try the remaining request again */
	if (mqrq->retries > MMC_MAX_RETRIES - 1)
		mqrq->retries = MMC_MAX_RETRIES - 1;
}

static void mmc_blk_mq_rw_recovery(struct mmc_queue *mq, struct request *req)
{
	int type = rq_data_dir(req) == READ ? MMC_BLK_READ : MMC_BLK_WRITE;
	struct mmc_queue_req *mqrq = req_to_mmc_queue_req(req);
	struct mmc_blk_request *brq = &mqrq->brq;
	struct mmc_blk_data *md = mq->blkdata;
	struct mmc_card *card = mq->card;
	static enum mmc_blk_status status;

	brq->retune_retry_done = mqrq->retries;

	status = __mmc_blk_err_check(card, mqrq);

	mmc_retune_release(card->host);

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
	switch (status) {
	case MMC_BLK_SUCCESS:
	case MMC_BLK_PARTIAL:
		/* Reset success, and accept bytes_xfered */
		mmc_blk_reset_success(md, type);
		break;
	case MMC_BLK_CMD_ERR:
		/*
		 * For SD cards, get bytes written, but do not accept
		 * bytes_xfered if that fails. For MMC cards accept
		 * bytes_xfered. Then try to reset. If reset fails then
		 * error out the remaining request, otherwise retry
		 * once (N.B mmc_blk_reset() will not succeed twice in a
		 * row).
		 */
		if (mmc_card_sd(card)) {
			u32 blocks;
			int err;

			err = mmc_sd_num_wr_blocks(card, &blocks);
			if (err)
				brq->data.bytes_xfered = 0;
			else
				brq->data.bytes_xfered = blocks << 9;
		}
		if (mmc_blk_reset(md, card->host, type))
			mqrq->retries = MMC_NO_RETRIES;
		else
			mqrq->retries = MMC_MAX_RETRIES - 1;
		break;
	case MMC_BLK_RETRY:
		/*
		 * Do not accept bytes_xfered, but retry up to 5 times,
		 * otherwise same as abort.
		 */
		brq->data.bytes_xfered = 0;
		if (mqrq->retries < MMC_MAX_RETRIES)
			break;
		/* Fall through */
	case MMC_BLK_ABORT:
		/*
		 * Do not accept bytes_xfered, but try to reset. If
		 * reset succeeds, try once more, otherwise error out
		 * the request.
		 */
		brq->data.bytes_xfered = 0;
		if (mmc_blk_reset(md, card->host, type))
			mqrq->retries = MMC_NO_RETRIES;
		else
			mqrq->retries = MMC_MAX_RETRIES - 1;
		break;
	case MMC_BLK_DATA_ERR: {
		int err;

		/*
		 * Do not accept bytes_xfered, but try to reset. If
		 * reset succeeds, try once more. If reset fails with
		 * ENODEV which means the partition is wrong, then error
		 * out the request. Otherwise attempt to read one sector
		 * at a time.
		 */
		brq->data.bytes_xfered = 0;
		err = mmc_blk_reset(md, card->host, type);
		if (!err) {
			mqrq->retries = MMC_MAX_RETRIES - 1;
			break;
		}
		if (err == -ENODEV) {
			mqrq->retries = MMC_NO_RETRIES;
			break;
		}
		/* Fall through */
	}
	case MMC_BLK_ECC_ERR:
		/*
		 * Do not accept bytes_xfered. If reading more than one
		 * sector, try reading one sector at a time.
		 */
		brq->data.bytes_xfered = 0;
		/* FIXME: Missing single sector read for large sector size */
		if (brq->data.blocks > 1 && !mmc_large_sector(card)) {
			/* Redo read one sector at a time */
			pr_warn("%s: retrying using single block read\n",
				req->rq_disk->disk_name);
			mmc_blk_read_single(mq, req);
		} else {
			mqrq->retries = MMC_NO_RETRIES;
		}
		break;
	case MMC_BLK_NOMEDIUM:
		/* Do not accept bytes_xfered. Error out the request */
		brq->data.bytes_xfered = 0;
		mqrq->retries = MMC_NO_RETRIES;
		break;
	default:
		/* Do not accept bytes_xfered. Error out the request */
		brq->data.bytes_xfered = 0;
		mqrq->retries = MMC_NO_RETRIES;
		pr_err("%s: Unhandled return value (%d)",
		       req->rq_disk->disk_name, status);
		break;
	}
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
		mmc_start_bkops(mq->card, true);
}

void mmc_blk_mq_complete(struct request *req)
{
	struct mmc_queue *mq = req->q->queuedata;

	mmc_blk_mq_complete_rq(mq, req);
}

static void mmc_blk_mq_poll_completion(struct mmc_queue *mq,
				       struct request *req)
{
	struct mmc_queue_req *mqrq = req_to_mmc_queue_req(req);

	mmc_blk_mq_rw_recovery(mq, req);

	mmc_blk_urgent_bkops(mq, mqrq);
}

static void mmc_blk_mq_dec_in_flight(struct mmc_queue *mq, struct request *req)
{
	struct request_queue *q = req->q;
	unsigned long flags;
	bool put_card;

	spin_lock_irqsave(q->queue_lock, flags);

	mq->in_flight[mmc_issue_type(mq, req)] -= 1;

	put_card = (mmc_tot_in_flight(mq) == 0);

	spin_unlock_irqrestore(q->queue_lock, flags);

	if (put_card)
		mmc_put_card(mq->card, &mq->ctx);
}

static void mmc_blk_mq_post_req(struct mmc_queue *mq, struct request *req)
{
	struct mmc_queue_req *mqrq = req_to_mmc_queue_req(req);
	struct mmc_request *mrq = &mqrq->brq.mrq;
	struct mmc_host *host = mq->card->host;

	mmc_post_req(host, mrq, 0);

	blk_mq_complete_request(req);

	mmc_blk_mq_dec_in_flight(mq, req);
}

static void mmc_blk_mq_complete_prev_req(struct mmc_queue *mq,
					 struct request **prev_req)
{
	mutex_lock(&mq->complete_lock);

	if (!mq->complete_req)
		goto out_unlock;

	mmc_blk_mq_poll_completion(mq, mq->complete_req);

	if (prev_req)
		*prev_req = mq->complete_req;
	else
		mmc_blk_mq_post_req(mq, mq->complete_req);

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
	unsigned long flags;
	bool waiting;

	/*
	 * We cannot complete the request in this context, so record that there
	 * is a request to complete, and that a following request does not need
	 * to wait (although it does need to complete complete_req first).
	 */
	spin_lock_irqsave(q->queue_lock, flags);
	mq->complete_req = req;
	mq->rw_wait = false;
	waiting = mq->waiting;
	spin_unlock_irqrestore(q->queue_lock, flags);

	/*
	 * If 'waiting' then the waiting task will complete this request,
	 * otherwise queue a work to do it. Note that complete_work may still
	 * race with the dispatch of a following request.
	 */
	if (waiting)
		wake_up(&mq->wait);
	else
		kblockd_schedule_work(&mq->complete_work);
}

static bool mmc_blk_rw_wait_cond(struct mmc_queue *mq, int *err)
{
	struct request_queue *q = mq->queue;
	unsigned long flags;
	bool done;

	/*
	 * Wait while there is another request in progress. Also indicate that
	 * there is a request waiting to start.
	 */
	spin_lock_irqsave(q->queue_lock, flags);
	done = !mq->rw_wait;
	mq->waiting = !done;
	spin_unlock_irqrestore(q->queue_lock, flags);

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
		mmc_blk_mq_post_req(mq, prev_req);

	if (err) {
		mq->rw_wait = false;
		mmc_retune_release(host);
	}

out_post_req:
	if (err)
		mmc_post_req(host, &mqrq->brq.mrq, err);

	return err;
}

static int mmc_blk_wait_for_idle(struct mmc_queue *mq, struct mmc_host *host)
{
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
		case REQ_OP_FLUSH:
			mmc_blk_issue_flush(mq, req);
			break;
		default:
			WARN_ON_ONCE(1);
			return MMC_REQ_FAILED_TO_START;
		}
		return MMC_REQ_FINISHED;
	case MMC_ISSUE_ASYNC:
		switch (req_op(req)) {
		case REQ_OP_READ:
		case REQ_OP_WRITE:
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

static bool mmc_blk_rw_cmd_err(struct mmc_blk_data *md, struct mmc_card *card,
			       struct mmc_blk_request *brq, struct request *req,
			       bool old_req_pending)
{
	bool req_pending;

	/*
	 * If this is an SD card and we're writing, we can first
	 * mark the known good sectors as ok.
	 *
	 * If the card is not SD, we can still ok written sectors
	 * as reported by the controller (which might be less than
	 * the real number of written sectors, but never more).
	 */
	if (mmc_card_sd(card)) {
		u32 blocks;
		int err;

		err = mmc_sd_num_wr_blocks(card, &blocks);
		if (err)
			req_pending = old_req_pending;
		else
			req_pending = blk_end_request(req, BLK_STS_OK, blocks << 9);
	} else {
		req_pending = blk_end_request(req, BLK_STS_OK, brq->data.bytes_xfered);
	}
	return req_pending;
}

static void mmc_blk_rw_cmd_abort(struct mmc_queue *mq, struct mmc_card *card,
				 struct request *req,
				 struct mmc_queue_req *mqrq)
{
	if (mmc_card_removed(card))
		req->rq_flags |= RQF_QUIET;
	while (blk_end_request(req, BLK_STS_IOERR, blk_rq_cur_bytes(req)));
	mq->qcnt--;
}

/**
 * mmc_blk_rw_try_restart() - tries to restart the current async request
 * @mq: the queue with the card and host to restart
 * @req: a new request that want to be started after the current one
 */
static void mmc_blk_rw_try_restart(struct mmc_queue *mq, struct request *req,
				   struct mmc_queue_req *mqrq)
{
	if (!req)
		return;

	/*
	 * If the card was removed, just cancel everything and return.
	 */
	if (mmc_card_removed(mq->card)) {
		req->rq_flags |= RQF_QUIET;
		blk_end_request_all(req, BLK_STS_IOERR);
		mq->qcnt--; /* FIXME: just set to 0? */
		return;
	}
	/* Else proceed and try to restart the current async request */
	mmc_blk_rw_rq_prep(mqrq, mq->card, 0, mq);
	mmc_start_areq(mq->card->host, &mqrq->areq, NULL);
}

static void mmc_blk_issue_rw_rq(struct mmc_queue *mq, struct request *new_req)
{
	struct mmc_blk_data *md = mq->blkdata;
	struct mmc_card *card = md->queue.card;
	struct mmc_blk_request *brq;
	int disable_multi = 0, retry = 0, type, retune_retry_done = 0;
	enum mmc_blk_status status;
	struct mmc_queue_req *mqrq_cur = NULL;
	struct mmc_queue_req *mq_rq;
	struct request *old_req;
	struct mmc_async_req *new_areq;
	struct mmc_async_req *old_areq;
	bool req_pending = true;

	if (new_req) {
		mqrq_cur = req_to_mmc_queue_req(new_req);
		mq->qcnt++;
	}

	if (!mq->qcnt)
		return;

	do {
		if (new_req) {
			/*
			 * When 4KB native sector is enabled, only 8 blocks
			 * multiple read or write is allowed
			 */
			if (mmc_large_sector(card) &&
				!IS_ALIGNED(blk_rq_sectors(new_req), 8)) {
				pr_err("%s: Transfer size is not 4KB sector size aligned\n",
					new_req->rq_disk->disk_name);
				mmc_blk_rw_cmd_abort(mq, card, new_req, mqrq_cur);
				return;
			}

			mmc_blk_rw_rq_prep(mqrq_cur, card, 0, mq);
			new_areq = &mqrq_cur->areq;
		} else
			new_areq = NULL;

		old_areq = mmc_start_areq(card->host, new_areq, &status);
		if (!old_areq) {
			/*
			 * We have just put the first request into the pipeline
			 * and there is nothing more to do until it is
			 * complete.
			 */
			return;
		}

		/*
		 * An asynchronous request has been completed and we proceed
		 * to handle the result of it.
		 */
		mq_rq =	container_of(old_areq, struct mmc_queue_req, areq);
		brq = &mq_rq->brq;
		old_req = mmc_queue_req_to_req(mq_rq);
		type = rq_data_dir(old_req) == READ ? MMC_BLK_READ : MMC_BLK_WRITE;

		switch (status) {
		case MMC_BLK_SUCCESS:
		case MMC_BLK_PARTIAL:
			/*
			 * Reset success, and accept bytes_xfered. For
			 * MMC_BLK_PARTIAL re-submit the remaining request. For
			 * MMC_BLK_SUCCESS error out the remaining request (it
			 * could not be re-submitted anyway if a next request
			 * had already begun).
			 */
			mmc_blk_reset_success(md, type);

			req_pending = blk_end_request(old_req, BLK_STS_OK,
						      brq->data.bytes_xfered);
			/*
			 * If the blk_end_request function returns non-zero even
			 * though all data has been transferred and no errors
			 * were returned by the host controller, it's a bug.
			 */
			if (status == MMC_BLK_SUCCESS && req_pending) {
				pr_err("%s BUG rq_tot %d d_xfer %d\n",
				       __func__, blk_rq_bytes(old_req),
				       brq->data.bytes_xfered);
				mmc_blk_rw_cmd_abort(mq, card, old_req, mq_rq);
				return;
			}
			break;
		case MMC_BLK_CMD_ERR:
			/*
			 * For SD cards, get bytes written, but do not accept
			 * bytes_xfered if that fails. For MMC cards accept
			 * bytes_xfered. Then try to reset. If reset fails then
			 * error out the remaining request, otherwise retry
			 * once (N.B mmc_blk_reset() will not succeed twice in a
			 * row).
			 */
			req_pending = mmc_blk_rw_cmd_err(md, card, brq, old_req, req_pending);
			if (mmc_blk_reset(md, card->host, type)) {
				if (req_pending)
					mmc_blk_rw_cmd_abort(mq, card, old_req, mq_rq);
				else
					mq->qcnt--;
				mmc_blk_rw_try_restart(mq, new_req, mqrq_cur);
				return;
			}
			if (!req_pending) {
				mq->qcnt--;
				mmc_blk_rw_try_restart(mq, new_req, mqrq_cur);
				return;
			}
			break;
		case MMC_BLK_RETRY:
			/*
			 * Do not accept bytes_xfered, but retry up to 5 times,
			 * otherwise same as abort.
			 */
			retune_retry_done = brq->retune_retry_done;
			if (retry++ < 5)
				break;
			/* Fall through */
		case MMC_BLK_ABORT:
			/*
			 * Do not accept bytes_xfered, but try to reset. If
			 * reset succeeds, try once more, otherwise error out
			 * the request.
			 */
			if (!mmc_blk_reset(md, card->host, type))
				break;
			mmc_blk_rw_cmd_abort(mq, card, old_req, mq_rq);
			mmc_blk_rw_try_restart(mq, new_req, mqrq_cur);
			return;
		case MMC_BLK_DATA_ERR: {
			int err;

			/*
			 * Do not accept bytes_xfered, but try to reset. If
			 * reset succeeds, try once more. If reset fails with
			 * ENODEV which means the partition is wrong, then error
			 * out the request. Otherwise attempt to read one sector
			 * at a time.
			 */
			err = mmc_blk_reset(md, card->host, type);
			if (!err)
				break;
			if (err == -ENODEV) {
				mmc_blk_rw_cmd_abort(mq, card, old_req, mq_rq);
				mmc_blk_rw_try_restart(mq, new_req, mqrq_cur);
				return;
			}
			/* Fall through */
		}
		case MMC_BLK_ECC_ERR:
			/*
			 * Do not accept bytes_xfered. If reading more than one
			 * sector, try reading one sector at a time.
			 */
			if (brq->data.blocks > 1) {
				/* Redo read one sector at a time */
				pr_warn("%s: retrying using single block read\n",
					old_req->rq_disk->disk_name);
				disable_multi = 1;
				break;
			}
			/*
			 * After an error, we redo I/O one sector at a
			 * time, so we only reach here after trying to
			 * read a single sector.
			 */
			req_pending = blk_end_request(old_req, BLK_STS_IOERR,
						      brq->data.blksz);
			if (!req_pending) {
				mq->qcnt--;
				mmc_blk_rw_try_restart(mq, new_req, mqrq_cur);
				return;
			}
			break;
		case MMC_BLK_NOMEDIUM:
			/* Do not accept bytes_xfered. Error out the request */
			mmc_blk_rw_cmd_abort(mq, card, old_req, mq_rq);
			mmc_blk_rw_try_restart(mq, new_req, mqrq_cur);
			return;
		default:
			/* Do not accept bytes_xfered. Error out the request */
			pr_err("%s: Unhandled return value (%d)",
					old_req->rq_disk->disk_name, status);
			mmc_blk_rw_cmd_abort(mq, card, old_req, mq_rq);
			mmc_blk_rw_try_restart(mq, new_req, mqrq_cur);
			return;
		}

		if (req_pending) {
			/*
			 * In case of a incomplete request
			 * prepare it again and resend.
			 */
			mmc_blk_rw_rq_prep(mq_rq, card,
					disable_multi, mq);
			mmc_start_areq(card->host,
					&mq_rq->areq, NULL);
			mq_rq->brq.retune_retry_done = retune_retry_done;
		}
	} while (req_pending);

	mq->qcnt--;
}

void mmc_blk_issue_rq(struct mmc_queue *mq, struct request *req)
{
	int ret;
	struct mmc_blk_data *md = mq->blkdata;
	struct mmc_card *card = md->queue.card;

	if (req && !mq->qcnt)
		/* claim host only for the first request */
		mmc_get_card(card, NULL);

	ret = mmc_blk_part_switch(card, md->part_type);
	if (ret) {
		if (req) {
			blk_end_request_all(req, BLK_STS_IOERR);
		}
		goto out;
	}

	if (req) {
		switch (req_op(req)) {
		case REQ_OP_DRV_IN:
		case REQ_OP_DRV_OUT:
			/*
			 * Complete ongoing async transfer before issuing
			 * ioctl()s
			 */
			if (mq->qcnt)
				mmc_blk_issue_rw_rq(mq, NULL);
			mmc_blk_issue_drv_op(mq, req);
			break;
		case REQ_OP_DISCARD:
			/*
			 * Complete ongoing async transfer before issuing
			 * discard.
			 */
			if (mq->qcnt)
				mmc_blk_issue_rw_rq(mq, NULL);
			mmc_blk_issue_discard_rq(mq, req);
			break;
		case REQ_OP_SECURE_ERASE:
			/*
			 * Complete ongoing async transfer before issuing
			 * secure erase.
			 */
			if (mq->qcnt)
				mmc_blk_issue_rw_rq(mq, NULL);
			mmc_blk_issue_secdiscard_rq(mq, req);
			break;
		case REQ_OP_FLUSH:
			/*
			 * Complete ongoing async transfer before issuing
			 * flush.
			 */
			if (mq->qcnt)
				mmc_blk_issue_rw_rq(mq, NULL);
			mmc_blk_issue_flush(mq, req);
			break;
		default:
			/* Normal request, just issue it */
			mmc_blk_issue_rw_rq(mq, req);
			card->host->context_info.is_waiting_last_req = false;
			break;
		}
	} else {
		/* No request, flushing the pipeline with NULL */
		mmc_blk_issue_rw_rq(mq, NULL);
		card->host->context_info.is_waiting_last_req = false;
	}

out:
	if (!mq->qcnt)
		mmc_put_card(card, NULL);
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
					      int area_type)
{
	struct mmc_blk_data *md;
	int devidx, ret;

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

	md->disk = alloc_disk(perdev_minors);
	if (md->disk == NULL) {
		ret = -ENOMEM;
		goto err_kfree;
	}

	spin_lock_init(&md->lock);
	INIT_LIST_HEAD(&md->part);
	INIT_LIST_HEAD(&md->rpmbs);
	md->usage = 1;

	ret = mmc_init_queue(&md->queue, card, &md->lock, subname);
	if (ret)
		goto err_putdisk;

	md->queue.blkdata = md;

	/*
	 * Keep an extra reference to the queue so that we can shutdown the
	 * queue (i.e. call blk_cleanup_queue()) while there are still
	 * references to the 'md'. The corresponding blk_put_queue() is in
	 * mmc_blk_put().
	 */
	if (!blk_get_queue(md->queue.queue)) {
		mmc_cleanup_queue(&md->queue);
		goto err_putdisk;
	}

	md->disk->major	= MMC_BLOCK_MAJOR;
	md->disk->first_minor = devidx * perdev_minors;
	md->disk->fops = &mmc_bdops;
	md->disk->private_data = md;
	md->disk->queue = md->queue.queue;
	md->parent = parent;
	set_disk_ro(md->disk, md->read_only || default_ro);
	md->disk->flags = GENHD_FL_EXT_DEVT;
	if (area_type & (MMC_BLK_DATA_AREA_RPMB | MMC_BLK_DATA_AREA_BOOT))
		md->disk->flags |= GENHD_FL_NO_PART_SCAN;

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

	if (mmc_card_mmc(card))
		blk_queue_logical_block_size(md->queue.queue,
					     card->ext_csd.data_sector_size);
	else
		blk_queue_logical_block_size(md->queue.queue, 512);

	set_capacity(md->disk, size);

	if (mmc_host_cmd23(card->host)) {
		if ((mmc_card_mmc(card) &&
		     card->csd.mmca_vsn >= CSD_SPEC_VER_3) ||
		    (mmc_card_sd(card) &&
		     card->scr.cmds & SD_SCR_CMD23_SUPPORT))
			md->flags |= MMC_BLK_CMD23;
	}

	if (mmc_card_mmc(card) &&
	    md->flags & MMC_BLK_CMD23 &&
	    ((card->ext_csd.rel_param & EXT_CSD_WR_REL_PARAM_EN) ||
	     card->ext_csd.rel_sectors)) {
		md->flags |= MMC_BLK_REL_WR;
		blk_queue_write_cache(md->queue.queue, true, true);
	}

	return md;

 err_putdisk:
	put_disk(md->disk);
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
					MMC_BLK_DATA_AREA_MAIN);
}

static int mmc_blk_alloc_part(struct mmc_card *card,
			      struct mmc_blk_data *md,
			      unsigned int part_type,
			      sector_t size,
			      bool default_ro,
			      const char *subname,
			      int area_type)
{
	char cap_str[10];
	struct mmc_blk_data *part_md;

	part_md = mmc_blk_alloc_req(card, disk_to_dev(md->disk), size, default_ro,
				    subname, area_type);
	if (IS_ERR(part_md))
		return PTR_ERR(part_md);
	part_md->part_type = part_type;
	list_add(&part_md->part, &md->part);

	string_get_size((u64)get_capacity(part_md->disk), 512, STRING_UNITS_2,
			cap_str, sizeof(cap_str));
	pr_info("%s: %s %s partition %u %s\n",
	       part_md->disk->disk_name, mmc_card_id(card),
	       mmc_card_name(card), part_md->part_type, cap_str);
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

	return 0;
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

	put_device(&rpmb->dev);
	mmc_blk_put(rpmb->md);

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

	pr_info("%s: %s %s partition %u %s, chardev (%d:%d)\n",
		rpmb_name, mmc_card_id(card),
		mmc_card_name(card), EXT_CSD_PART_CONFIG_ACC_RPMB, cap_str,
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
	struct mmc_card *card;

	if (md) {
		/*
		 * Flush remaining requests and free queues. It
		 * is freeing the queue that stops new requests
		 * from being accepted.
		 */
		card = md->queue.card;
		mmc_cleanup_queue(&md->queue);
		if (md->disk->flags & GENHD_FL_UP) {
			device_remove_file(disk_to_dev(md->disk), &md->force_ro);
			if ((md->area_type & MMC_BLK_DATA_AREA_BOOT) &&
					card->ext_csd.boot_ro_lockable)
				device_remove_file(disk_to_dev(md->disk),
					&md->power_ro_lock);

			del_gendisk(md->disk);
		}
		mmc_blk_put(md);
	}
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

static int mmc_add_disk(struct mmc_blk_data *md)
{
	int ret;
	struct mmc_card *card = md->queue.card;

	device_add_disk(md->parent, md->disk);
	md->force_ro.show = force_ro_show;
	md->force_ro.store = force_ro_store;
	sysfs_attr_init(&md->force_ro.attr);
	md->force_ro.attr.name = "force_ro";
	md->force_ro.attr.mode = S_IRUGO | S_IWUSR;
	ret = device_create_file(disk_to_dev(md->disk), &md->force_ro);
	if (ret)
		goto force_ro_fail;

	if ((md->area_type & MMC_BLK_DATA_AREA_BOOT) &&
	     card->ext_csd.boot_ro_lockable) {
		umode_t mode;

		if (card->ext_csd.boot_ro_lock & EXT_CSD_BOOT_WP_B_PWR_WP_DIS)
			mode = S_IRUGO;
		else
			mode = S_IRUGO | S_IWUSR;

		md->power_ro_lock.show = power_ro_lock_show;
		md->power_ro_lock.store = power_ro_lock_store;
		sysfs_attr_init(&md->power_ro_lock.attr);
		md->power_ro_lock.attr.mode = mode;
		md->power_ro_lock.attr.name =
					"ro_lock_until_next_power_on";
		ret = device_create_file(disk_to_dev(md->disk),
				&md->power_ro_lock);
		if (ret)
			goto power_ro_lock_fail;
	}
	return ret;

power_ro_lock_fail:
	device_remove_file(disk_to_dev(md->disk), &md->force_ro);
force_ro_fail:
	del_gendisk(md->disk);

	return ret;
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
	req = blk_get_request(mq->queue, REQ_OP_DRV_IN, __GFP_RECLAIM);
	if (IS_ERR(req))
		return PTR_ERR(req);
	req_to_mmc_queue_req(req)->drv_op = MMC_DRV_OP_GET_CARD_STATUS;
	blk_execute_rq(mq->queue, NULL, req, 0);
	ret = req_to_mmc_queue_req(req)->drv_op_result;
	if (ret >= 0) {
		*val = ret;
		ret = 0;
	}
	blk_put_request(req);

	return ret;
}
DEFINE_SIMPLE_ATTRIBUTE(mmc_dbg_card_status_fops, mmc_dbg_card_status_get,
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
	req = blk_get_request(mq->queue, REQ_OP_DRV_IN, __GFP_RECLAIM);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto out_free;
	}
	req_to_mmc_queue_req(req)->drv_op = MMC_DRV_OP_GET_EXT_CSD;
	req_to_mmc_queue_req(req)->drv_op_data = &ext_csd;
	blk_execute_rq(mq->queue, NULL, req, 0);
	err = req_to_mmc_queue_req(req)->drv_op_result;
	blk_put_request(req);
	if (err) {
		pr_err("FAILED %d\n", err);
		goto out_free;
	}

	for (i = 0; i < 512; i++)
		n += sprintf(buf + n, "%02x", ext_csd[i]);
	n += sprintf(buf + n, "\n");

	if (n != EXT_CSD_STR_LEN) {
		err = -EINVAL;
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
			debugfs_create_file("status", S_IRUSR, root, card,
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
	struct mmc_blk_data *md, *part_md;
	char cap_str[10];

	/*
	 * Check that the card supports the command class(es) we need.
	 */
	if (!(card->csd.cmdclass & CCC_BLOCK_READ))
		return -ENODEV;

	mmc_fixup_device(card, mmc_blk_fixups);

	md = mmc_blk_alloc(card);
	if (IS_ERR(md))
		return PTR_ERR(md);

	string_get_size((u64)get_capacity(md->disk), 512, STRING_UNITS_2,
			cap_str, sizeof(cap_str));
	pr_info("%s: %s %s %s %s\n",
		md->disk->disk_name, mmc_card_id(card), mmc_card_name(card),
		cap_str, md->read_only ? "(ro)" : "");

	if (mmc_blk_alloc_parts(card, md))
		goto out;

	dev_set_drvdata(&card->dev, md);

	if (mmc_add_disk(md))
		goto out;

	list_for_each_entry(part_md, &md->part, part) {
		if (mmc_add_disk(part_md))
			goto out;
	}

	/* Add two debugfs entries */
	mmc_blk_add_debugfs(card, md);

	pm_runtime_set_autosuspend_delay(&card->dev, 3000);
	pm_runtime_use_autosuspend(&card->dev);

	/*
	 * Don't enable runtime PM for SD-combo cards here. Leave that
	 * decision to be taken during the SDIO init sequence instead.
	 */
	if (card->type != MMC_TYPE_SD_COMBO) {
		pm_runtime_set_active(&card->dev);
		pm_runtime_enable(&card->dev);
	}

	return 0;

 out:
	mmc_blk_remove_parts(card, md);
	mmc_blk_remove_req(md);
	return 0;
}

static void mmc_blk_remove(struct mmc_card *card)
{
	struct mmc_blk_data *md = dev_get_drvdata(&card->dev);

	mmc_blk_remove_debugfs(card, md);
	mmc_blk_remove_parts(card, md);
	pm_runtime_get_sync(&card->dev);
	mmc_claim_host(card->host);
	mmc_blk_part_switch(card, md->part_type);
	mmc_release_host(card->host);
	if (card->type != MMC_TYPE_SD_COMBO)
		pm_runtime_disable(&card->dev);
	pm_runtime_put_noidle(&card->dev);
	mmc_blk_remove_req(md);
	dev_set_drvdata(&card->dev, NULL);
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
}

module_init(mmc_blk_init);
module_exit(mmc_blk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Multimedia Card (MMC) block device driver");

