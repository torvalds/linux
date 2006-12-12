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
#include <linux/mutex.h>
#include <linux/scatterlist.h>

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/protocol.h>
#include <linux/mmc/host.h>

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

static u32 mmc_sd_num_wr_blocks(struct mmc_card *card)
{
	int err;
	u32 blocks;

	struct mmc_request mrq;
	struct mmc_command cmd;
	struct mmc_data data;
	unsigned int timeout_us;

	struct scatterlist sg;

	memset(&cmd, 0, sizeof(struct mmc_command));

	cmd.opcode = MMC_APP_CMD;
	cmd.arg = card->rca << 16;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;

	err = mmc_wait_for_cmd(card->host, &cmd, 0);
	if ((err != MMC_ERR_NONE) || !(cmd.resp[0] & R1_APP_CMD))
		return (u32)-1;

	memset(&cmd, 0, sizeof(struct mmc_command));

	cmd.opcode = SD_APP_SEND_NUM_WR_BLKS;
	cmd.arg = 0;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;

	memset(&data, 0, sizeof(struct mmc_data));

	data.timeout_ns = card->csd.tacc_ns * 100;
	data.timeout_clks = card->csd.tacc_clks * 100;

	timeout_us = data.timeout_ns / 1000;
	timeout_us += data.timeout_clks * 1000 /
		(card->host->ios.clock / 1000);

	if (timeout_us > 100000) {
		data.timeout_ns = 100000000;
		data.timeout_clks = 0;
	}

	data.blksz = 4;
	data.blocks = 1;
	data.flags = MMC_DATA_READ;
	data.sg = &sg;
	data.sg_len = 1;

	memset(&mrq, 0, sizeof(struct mmc_request));

	mrq.cmd = &cmd;
	mrq.data = &data;

	sg_init_one(&sg, &blocks, 4);

	mmc_wait_for_req(card->host, &mrq);

	if (cmd.error != MMC_ERR_NONE || data.error != MMC_ERR_NONE)
		return (u32)-1;

	blocks = ntohl(blocks);

	return blocks;
}

static int mmc_blk_issue_rq(struct mmc_queue *mq, struct request *req)
{
	struct mmc_blk_data *md = mq->data;
	struct mmc_card *card = md->queue.card;
	struct mmc_blk_request brq;
	int ret = 1;

	if (mmc_card_claim_host(card))
		goto flush_queue;

	do {
		struct mmc_command cmd;
		u32 readcmd, writecmd;

		memset(&brq, 0, sizeof(struct mmc_blk_request));
		brq.mrq.cmd = &brq.cmd;
		brq.mrq.data = &brq.data;

		brq.cmd.arg = req->sector << 9;
		brq.cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;
		brq.data.blksz = 1 << md->block_bits;
		brq.data.blocks = req->nr_sectors >> (md->block_bits - 9);
		brq.stop.opcode = MMC_STOP_TRANSMISSION;
		brq.stop.arg = 0;
		brq.stop.flags = MMC_RSP_R1B | MMC_CMD_AC;

		mmc_set_data_timeout(&brq.data, card, rq_data_dir(req) != READ);

		/*
		 * If the host doesn't support multiple block writes, force
		 * block writes to single block. SD cards are excepted from
		 * this rule as they support querying the number of
		 * successfully written sectors.
		 */
		if (rq_data_dir(req) != READ &&
		    !(card->host->caps & MMC_CAP_MULTIWRITE) &&
		    !mmc_card_sd(card))
			brq.data.blocks = 1;

		if (brq.data.blocks > 1) {
			brq.data.flags |= MMC_DATA_MULTI;
			brq.mrq.stop = &brq.stop;
			readcmd = MMC_READ_MULTIPLE_BLOCK;
			writecmd = MMC_WRITE_MULTIPLE_BLOCK;
		} else {
			brq.mrq.stop = NULL;
			readcmd = MMC_READ_SINGLE_BLOCK;
			writecmd = MMC_WRITE_BLOCK;
		}

		if (rq_data_dir(req) == READ) {
			brq.cmd.opcode = readcmd;
			brq.data.flags |= MMC_DATA_READ;
		} else {
			brq.cmd.opcode = writecmd;
			brq.data.flags |= MMC_DATA_WRITE;
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

		if (rq_data_dir(req) != READ) {
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
		}

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
 	/*
 	 * If this is an SD card and we're writing, we can first
 	 * mark the known good sectors as ok.
 	 *
	 * If the card is not SD, we can still ok written sectors
	 * if the controller can do proper error reporting.
	 *
	 * For reads we just fail the entire chunk as that should
	 * be safe in all cases.
	 */
 	if (rq_data_dir(req) != READ && mmc_card_sd(card)) {
		u32 blocks;
		unsigned int bytes;

		blocks = mmc_sd_num_wr_blocks(card);
		if (blocks != (u32)-1) {
			if (card->csd.write_partial)
				bytes = blocks << md->block_bits;
			else
				bytes = blocks << 9;
			spin_lock_irq(&md->lock);
			ret = end_that_request_chunk(req, 1, bytes);
			spin_unlock_irq(&md->lock);
		}
	} else if (rq_data_dir(req) != READ &&
		   (card->host->caps & MMC_CAP_MULTIWRITE)) {
		spin_lock_irq(&md->lock);
		ret = end_that_request_chunk(req, 1, brq.data.bytes_xfered);
		spin_unlock_irq(&md->lock);
	}

	mmc_card_release_host(card);

flush_queue:
	spin_lock_irq(&md->lock);
	while (ret) {
		ret = end_that_request_chunk(req, 0,
				req->current_nr_sectors << 9);
	}

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
	 * Both SD and MMC specifications state (although a bit
	 * unclearly in the MMC case) that a block size of 512
	 * bytes must always be supported by the card.
	 */
	md->block_bits = 9;

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

		/* Stop new requests from getting into the queue */
		del_gendisk(md->disk);

		/* Then flush out any already in there */
		mmc_cleanup_queue(&md->queue);

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

	return mmc_register_driver(&mmc_driver);

 out:
	return res;
}

static void __exit mmc_blk_exit(void)
{
	mmc_unregister_driver(&mmc_driver);
	unregister_blkdev(major, "mmc");
}

module_init(mmc_blk_init);
module_exit(mmc_blk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Multimedia Card (MMC) block device driver");

module_param(major, int, 0444);
MODULE_PARM_DESC(major, "specify the major device number for MMC block driver");
