/*
 * Block driver for media (i.e., flash cards)
 *
 * Copyright 2002 Hewlett-Packard Company
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

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include <linux/kdev_t.h>
#include <linux/blkdev.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/mutex.h>

#include <linux/mmc/card.h>
#include <linux/mmc/protocol.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include "mmc_queue.h"

/*
 * max 8 partitions per card
 */
#define MMC_SHIFT	3

static int major;

/*
 * There is one mmc_blk_data per slot.
 */
struct mmc_blk_data {
	spinlock_t	lock;
	struct gendisk	*disk;
	struct mmc_queue queue;

	unsigned int	usage;
	unsigned int	block_bits;
	unsigned int	read_only;
};

static DEFINE_MUTEX(open_lock);

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

static void mmc_blk_put(struct mmc_blk_data *md)
{
	mutex_lock(&open_lock);
	md->usage--;
	if (md->usage == 0) {
		put_disk(md->disk);
		mmc_cleanup_queue(&md->queue);
		kfree(md);
	}
	mutex_unlock(&open_lock);
}

static int mmc_blk_open(struct inode *inode, struct file *filp)
{
	struct mmc_blk_data *md;
	int ret = -ENXIO;

	md = mmc_blk_get(inode->i_bdev->bd_disk);
	if (md) {
		if (md->usage == 2)
			check_disk_change(inode->i_bdev);
		ret = 0;

		if ((filp->f_mode & FMODE_WRITE) && md->read_only)
			ret = -EROFS;
	}

	return ret;
}

static int mmc_blk_release(struct inode *inode, struct file *filp)
{
	struct mmc_blk_data *md = inode->i_bdev->bd_disk->private_data;

	mmc_blk_put(md);
	return 0;
}

static int
mmc_blk_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	geo->cylinders = get_capacity(bdev->bd_disk) / (4 * 16);
	geo->heads = 4;
	geo->sectors = 16;
	return 0;
}

static struct block_device_operations mmc_bdops = {
	.open			= mmc_blk_open,
	.release		= mmc_blk_release,
	.getgeo			= mmc_blk_getgeo,
	.owner			= THIS_MODULE,
};

struct mmc_blk_request {
	struct mmc_request	mrq;
	struct mmc_command	cmd;
	struct mmc_command	stop;
	struct mmc_data		data;
};

static int mmc_blk_prep_rq(struct mmc_queue *mq, struct request *req)
{
	struct mmc_blk_data *md = mq->data;
	int stat = BLKPREP_OK;

	/*
	 * If we have no device, we haven't finished initialising.
	 */
	if (!md || !mq->card) {
		printk(KERN_ERR "%s: killing request - no device/host\n",
		       req->rq_disk->disk_name);
		stat = BLKPREP_KILL;
	}

	return stat;
}

static int mmc_blk_issue_rq(struct mmc_queue *mq, struct request *req)
{
	struct mmc_blk_data *md = mq->data;
	struct mmc_card *card = md->queue.card;
	int ret;

	if (mmc_card_claim_host(card))
		goto cmd_err;

	do {
		struct mmc_blk_request brq;
		struct mmc_command cmd;

		memset(&brq, 0, sizeof(struct mmc_blk_request));
		brq.mrq.cmd = &brq.cmd;
		brq.mrq.data = &brq.data;

		brq.cmd.arg = req->sector << 9;
		brq.cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;
		brq.data.timeout_ns = card->csd.tacc_ns * 10;
		brq.data.timeout_clks = card->csd.tacc_clks * 10;
		brq.data.blksz_bits = md->block_bits;
		brq.data.blocks = req->nr_sectors >> (md->block_bits - 9);
		brq.stop.opcode = MMC_STOP_TRANSMISSION;
		brq.stop.arg = 0;
		brq.stop.flags = MMC_RSP_R1B | MMC_CMD_AC;

		if (rq_data_dir(req) == READ) {
			brq.cmd.opcode = brq.data.blocks > 1 ? MMC_READ_MULTIPLE_BLOCK : MMC_READ_SINGLE_BLOCK;
			brq.data.flags |= MMC_DATA_READ;
		} else {
			brq.cmd.opcode = MMC_WRITE_BLOCK;
			brq.data.flags |= MMC_DATA_WRITE;
			brq.data.blocks = 1;

			/*
			 * Scale up the timeout by the r2w factor
			 */
			brq.data.timeout_ns <<= card->csd.r2w_factor;
			brq.data.timeout_clks <<= card->csd.r2w_factor;
		}

		if (brq.data.blocks > 1) {
			brq.data.flags |= MMC_DATA_MULTI;
			brq.mrq.stop = &brq.stop;
		} else {
			brq.mrq.stop = NULL;
		}

		brq.data.sg = mq->sg;
		brq.data.sg_len = blk_rq_map_sg(req->q, req, brq.data.sg);

		mmc_wait_for_req(card->host, &brq.mrq);
		if (brq.cmd.error) {
			printk(KERN_ERR "%s: error %d sending read/write command\n",
			       req->rq_disk->disk_name, brq.cmd.error);
			goto cmd_err;
		}

		if (brq.data.error) {
			printk(KERN_ERR "%s: error %d transferring data\n",
			       req->rq_disk->disk_name, brq.data.error);
			goto cmd_err;
		}

		if (brq.stop.error) {
			printk(KERN_ERR "%s: error %d sending stop command\n",
			       req->rq_disk->disk_name, brq.stop.error);
			goto cmd_err;
		}

		do {
			int err;

			cmd.opcode = MMC_SEND_STATUS;
			cmd.arg = card->rca << 16;
			cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;
			err = mmc_wait_for_cmd(card->host, &cmd, 5);
			if (err) {
				printk(KERN_ERR "%s: error %d requesting status\n",
				       req->rq_disk->disk_name, err);
				goto cmd_err;
			}
		} while (!(cmd.resp[0] & R1_READY_FOR_DATA));

#if 0
		if (cmd.resp[0] & ~0x00000900)
			printk(KERN_ERR "%s: status = %08x\n",
			       req->rq_disk->disk_name, cmd.resp[0]);
		if (mmc_decode_status(cmd.resp))
			goto cmd_err;
#endif

		/*
		 * A block was successfully transferred.
		 */
		spin_lock_irq(&md->lock);
		ret = end_that_request_chunk(req, 1, brq.data.bytes_xfered);
		if (!ret) {
			/*
			 * The whole request completed successfully.
			 */
			add_disk_randomness(req->rq_disk);
			blkdev_dequeue_request(req);
			end_that_request_last(req, 1);
		}
		spin_unlock_irq(&md->lock);
	} while (ret);

	mmc_card_release_host(card);

	return 1;

 cmd_err:
	mmc_card_release_host(card);

	/*
	 * This is a little draconian, but until we get proper
	 * error handling sorted out here, its the best we can
	 * do - especially as some hosts have no idea how much
	 * data was transferred before the error occurred.
	 */
	spin_lock_irq(&md->lock);
	do {
		ret = end_that_request_chunk(req, 0,
				req->current_nr_sectors << 9);
	} while (ret);

	add_disk_randomness(req->rq_disk);
	blkdev_dequeue_request(req);
	end_that_request_last(req, 0);
	spin_unlock_irq(&md->lock);

	return 0;
}

#define MMC_NUM_MINORS	(256 >> MMC_SHIFT)

static unsigned long dev_use[MMC_NUM_MINORS/(8*sizeof(unsigned long))];

static inline int mmc_blk_readonly(struct mmc_card *card)
{
	return mmc_card_readonly(card) ||
	       !(card->csd.cmdclass & CCC_BLOCK_WRITE);
}

static struct mmc_blk_data *mmc_blk_alloc(struct mmc_card *card)
{
	struct mmc_blk_data *md;
	int devidx, ret;

	devidx = find_first_zero_bit(dev_use, MMC_NUM_MINORS);
	if (devidx >= MMC_NUM_MINORS)
		return ERR_PTR(-ENOSPC);
	__set_bit(devidx, dev_use);

	md = kmalloc(sizeof(struct mmc_blk_data), GFP_KERNEL);
	if (!md) {
		ret = -ENOMEM;
		goto out;
	}

	memset(md, 0, sizeof(struct mmc_blk_data));

	/*
	 * Set the read-only status based on the supported commands
	 * and the write protect switch.
	 */
	md->read_only = mmc_blk_readonly(card);

	/*
	 * Figure out a workable block size.  MMC cards have:
	 *  - two block sizes, one for read and one for write.
	 *  - may support partial reads and/or writes
	 *    (allows block sizes smaller than specified)
	 */
	md->block_bits = card->csd.read_blkbits;
	if (card->csd.write_blkbits != card->csd.read_blkbits) {
		if (card->csd.write_blkbits < card->csd.read_blkbits &&
		    card->csd.read_partial) {
			/*
			 * write block size is smaller than read block
			 * size, but we support partial reads, so choose
			 * the smaller write block size.
			 */
			md->block_bits = card->csd.write_blkbits;
		} else if (card->csd.write_blkbits > card->csd.read_blkbits &&
			   card->csd.write_partial) {
			/*
			 * read block size is smaller than write block
			 * size, but we support partial writes.  Use read
			 * block size.
			 */
		} else {
			/*
			 * We don't support this configuration for writes.
			 */
			printk(KERN_ERR "%s: unable to select block size for "
				"writing (rb%u wb%u rp%u wp%u)\n",
				md->disk->disk_name,
				1 << card->csd.read_blkbits,
				1 << card->csd.write_blkbits,
				card->csd.read_partial,
				card->csd.write_partial);
			md->read_only = 1;
		}
	}

	/*
	 * Refuse to allow block sizes smaller than 512 bytes.
	 */
	if (md->block_bits < 9) {
		printk(KERN_ERR "%s: unable to support block size %u\n",
			mmc_card_id(card), 1 << md->block_bits);
		ret = -EINVAL;
		goto err_kfree;
	}

	md->disk = alloc_disk(1 << MMC_SHIFT);
	if (md->disk == NULL) {
		ret = -ENOMEM;
		goto err_kfree;
	}

	spin_lock_init(&md->lock);
	md->usage = 1;

	ret = mmc_init_queue(&md->queue, card, &md->lock);
	if (ret)
		goto err_putdisk;

	md->queue.prep_fn = mmc_blk_prep_rq;
	md->queue.issue_fn = mmc_blk_issue_rq;
	md->queue.data = md;

	md->disk->major	= major;
	md->disk->first_minor = devidx << MMC_SHIFT;
	md->disk->fops = &mmc_bdops;
	md->disk->private_data = md;
	md->disk->queue = md->queue.queue;
	md->disk->driverfs_dev = &card->dev;

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

	sprintf(md->disk->disk_name, "mmcblk%d", devidx);
	sprintf(md->disk->devfs_name, "mmc/blk%d", devidx);

	blk_queue_hardsect_size(md->queue.queue, 1 << md->block_bits);

	/*
	 * The CSD capacity field is in units of read_blkbits.
	 * set_capacity takes units of 512 bytes.
	 */
	set_capacity(md->disk, card->csd.capacity << (card->csd.read_blkbits - 9));
	return md;

 err_putdisk:
	put_disk(md->disk);
 err_kfree:
	kfree(md);
 out:
	return ERR_PTR(ret);
}

static int
mmc_blk_set_blksize(struct mmc_blk_data *md, struct mmc_card *card)
{
	struct mmc_command cmd;
	int err;

	mmc_card_claim_host(card);
	cmd.opcode = MMC_SET_BLOCKLEN;
	cmd.arg = 1 << md->block_bits;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;
	err = mmc_wait_for_cmd(card->host, &cmd, 5);
	mmc_card_release_host(card);

	if (err) {
		printk(KERN_ERR "%s: unable to set block size to %d: %d\n",
			md->disk->disk_name, cmd.arg, err);
		return -EINVAL;
	}

	return 0;
}

static int mmc_blk_probe(struct mmc_card *card)
{
	struct mmc_blk_data *md;
	int err;

	/*
	 * Check that the card supports the command class(es) we need.
	 */
	if (!(card->csd.cmdclass & CCC_BLOCK_READ))
		return -ENODEV;

	md = mmc_blk_alloc(card);
	if (IS_ERR(md))
		return PTR_ERR(md);

	err = mmc_blk_set_blksize(md, card);
	if (err)
		goto out;

	printk(KERN_INFO "%s: %s %s %lluKiB %s\n",
		md->disk->disk_name, mmc_card_id(card), mmc_card_name(card),
		(unsigned long long)(get_capacity(md->disk) >> 1),
		md->read_only ? "(ro)" : "");

	mmc_set_drvdata(card, md);
	add_disk(md->disk);
	return 0;

 out:
	mmc_blk_put(md);

	return err;
}

static void mmc_blk_remove(struct mmc_card *card)
{
	struct mmc_blk_data *md = mmc_get_drvdata(card);

	if (md) {
		int devidx;

		del_gendisk(md->disk);

		/*
		 * I think this is needed.
		 */
		md->disk->queue = NULL;

		devidx = md->disk->first_minor >> MMC_SHIFT;
		__clear_bit(devidx, dev_use);

		mmc_blk_put(md);
	}
	mmc_set_drvdata(card, NULL);
}

#ifdef CONFIG_PM
static int mmc_blk_suspend(struct mmc_card *card, pm_message_t state)
{
	struct mmc_blk_data *md = mmc_get_drvdata(card);

	if (md) {
		mmc_queue_suspend(&md->queue);
	}
	return 0;
}

static int mmc_blk_resume(struct mmc_card *card)
{
	struct mmc_blk_data *md = mmc_get_drvdata(card);

	if (md) {
		mmc_blk_set_blksize(md, card);
		mmc_queue_resume(&md->queue);
	}
	return 0;
}
#else
#define	mmc_blk_suspend	NULL
#define mmc_blk_resume	NULL
#endif

static struct mmc_driver mmc_driver = {
	.drv		= {
		.name	= "mmcblk",
	},
	.probe		= mmc_blk_probe,
	.remove		= mmc_blk_remove,
	.suspend	= mmc_blk_suspend,
	.resume		= mmc_blk_resume,
};

static int __init mmc_blk_init(void)
{
	int res = -ENOMEM;

	res = register_blkdev(major, "mmc");
	if (res < 0) {
		printk(KERN_WARNING "Unable to get major %d for MMC media: %d\n",
		       major, res);
		goto out;
	}
	if (major == 0)
		major = res;

	devfs_mk_dir("mmc");
	return mmc_register_driver(&mmc_driver);

 out:
	return res;
}

static void __exit mmc_blk_exit(void)
{
	mmc_unregister_driver(&mmc_driver);
	devfs_remove("mmc");
	unregister_blkdev(major, "mmc");
}

module_init(mmc_blk_init);
module_exit(mmc_blk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Multimedia Card (MMC) block device driver");

module_param(major, int, 0444);
MODULE_PARM_DESC(major, "specify the major device number for MMC block driver");
