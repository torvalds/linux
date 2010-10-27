/*
 * NAND Flash Controller Device Driver
 * Copyright (c) 2009, Intel Corporation and its suppliers.
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
 *
 */

#include "ffsport.h"
#include "flash.h"
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/blkdev.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/log2.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/slab.h>

/**** Helper functions used for Div, Remainder operation on u64 ****/

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_Calc_Used_Bits
* Inputs:       Power of 2 number
* Outputs:      Number of Used Bits
*               0, if the argument is 0
* Description:  Calculate the number of bits used by a given power of 2 number
*               Number can be upto 32 bit
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
int GLOB_Calc_Used_Bits(u32 n)
{
	int tot_bits = 0;

	if (n >= 1 << 16) {
		n >>= 16;
		tot_bits += 16;
	}

	if (n >= 1 << 8) {
		n >>=  8;
		tot_bits +=  8;
	}

	if (n >= 1 << 4) {
		n >>=  4;
		tot_bits +=  4;
	}

	if (n >= 1 << 2) {
		n >>=  2;
		tot_bits +=  2;
	}

	if (n >= 1 << 1)
		tot_bits +=  1;

	return ((n == 0) ? (0) : tot_bits);
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_u64_Div
* Inputs:       Number of u64
*               A power of 2 number as Division
* Outputs:      Quotient of the Divisor operation
* Description:  It divides the address by divisor by using bit shift operation
*               (essentially without explicitely using "/").
*               Divisor is a power of 2 number and Divided is of u64
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u64 GLOB_u64_Div(u64 addr, u32 divisor)
{
	return  (u64)(addr >> GLOB_Calc_Used_Bits(divisor));
}

/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
* Function:     GLOB_u64_Remainder
* Inputs:       Number of u64
*               Divisor Type (1 -PageAddress, 2- BlockAddress)
* Outputs:      Remainder of the Division operation
* Description:  It calculates the remainder of a number (of u64) by
*               divisor(power of 2 number ) by using bit shifting and multiply
*               operation(essentially without explicitely using "/").
*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
u64 GLOB_u64_Remainder(u64 addr, u32 divisor_type)
{
	u64 result = 0;

	if (divisor_type == 1) { /* Remainder -- Page */
		result = (addr >> DeviceInfo.nBitsInPageDataSize);
		result = result * DeviceInfo.wPageDataSize;
	} else if (divisor_type == 2) { /* Remainder -- Block */
		result = (addr >> DeviceInfo.nBitsInBlockDataSize);
		result = result * DeviceInfo.wBlockDataSize;
	}

	result = addr - result;

	return result;
}

#define NUM_DEVICES             1
#define PARTITIONS              8

#define GLOB_SBD_NAME          "nd"
#define GLOB_SBD_IRQ_NUM       (29)

#define GLOB_SBD_IOCTL_GC                        (0x7701)
#define GLOB_SBD_IOCTL_WL                        (0x7702)
#define GLOB_SBD_IOCTL_FORMAT                    (0x7703)
#define GLOB_SBD_IOCTL_ERASE_FLASH               (0x7704)
#define GLOB_SBD_IOCTL_FLUSH_CACHE               (0x7705)
#define GLOB_SBD_IOCTL_COPY_BLK_TABLE            (0x7706)
#define GLOB_SBD_IOCTL_COPY_WEAR_LEVELING_TABLE  (0x7707)
#define GLOB_SBD_IOCTL_GET_NAND_INFO             (0x7708)
#define GLOB_SBD_IOCTL_WRITE_DATA                (0x7709)
#define GLOB_SBD_IOCTL_READ_DATA                 (0x770A)

static int reserved_mb = 0;
module_param(reserved_mb, int, 0);
MODULE_PARM_DESC(reserved_mb, "Reserved space for OS image, in MiB (default 25 MiB)");

int nand_debug_level;
module_param(nand_debug_level, int, 0644);
MODULE_PARM_DESC(nand_debug_level, "debug level value: 1-3");

MODULE_LICENSE("GPL");

struct spectra_nand_dev {
	struct pci_dev *dev;
	u64 size;
	u16 users;
	spinlock_t qlock;
	void __iomem *ioaddr;  /* Mapped address */
	struct request_queue *queue;
	struct task_struct *thread;
	struct gendisk *gd;
	u8 *tmp_buf;
};


static int GLOB_SBD_majornum;

static char *GLOB_version = GLOB_VERSION;

static struct spectra_nand_dev nand_device[NUM_DEVICES];

static struct mutex spectra_lock;

static int res_blks_os = 1;

struct spectra_indentfy_dev_tag IdentifyDeviceData;

static int force_flush_cache(void)
{
	nand_dbg_print(NAND_DBG_DEBUG, "%s, Line %d, Function: %s\n",
		__FILE__, __LINE__, __func__);

	if (ERR == GLOB_FTL_Flush_Cache()) {
		printk(KERN_ERR "Fail to Flush FTL Cache!\n");
		return -EFAULT;
	}
#if CMD_DMA
		if (glob_ftl_execute_cmds())
			return -EIO;
		else
			return 0;
#endif
	return 0;
}

struct ioctl_rw_page_info {
	u8 *data;
	unsigned int page;
};

static int ioctl_read_page_data(unsigned long arg)
{
	u8 *buf;
	struct ioctl_rw_page_info info;
	int result = PASS;

	if (copy_from_user(&info, (void __user *)arg, sizeof(info)))
		return -EFAULT;

	buf = kmalloc(IdentifyDeviceData.PageDataSize, GFP_ATOMIC);
	if (!buf) {
		printk(KERN_ERR "ioctl_read_page_data: "
		       "failed to allocate memory\n");
		return -ENOMEM;
	}

	mutex_lock(&spectra_lock);
	result = GLOB_FTL_Page_Read(buf,
		(u64)info.page * IdentifyDeviceData.PageDataSize);
	mutex_unlock(&spectra_lock);

	if (copy_to_user((void __user *)info.data, buf,
			   IdentifyDeviceData.PageDataSize)) {
		printk(KERN_ERR "ioctl_read_page_data: "
		       "failed to copy user data\n");
		kfree(buf);
		return -EFAULT;
	}

	kfree(buf);
	return result;
}

static int ioctl_write_page_data(unsigned long arg)
{
	u8 *buf;
	struct ioctl_rw_page_info info;
	int result = PASS;

	if (copy_from_user(&info, (void __user *)arg, sizeof(info)))
		return -EFAULT;

	buf = kmalloc(IdentifyDeviceData.PageDataSize, GFP_ATOMIC);
	if (!buf) {
		printk(KERN_ERR "ioctl_write_page_data: "
		       "failed to allocate memory\n");
		return -ENOMEM;
	}

	if (copy_from_user(buf, (void __user *)info.data,
			   IdentifyDeviceData.PageDataSize)) {
		printk(KERN_ERR "ioctl_write_page_data: "
		       "failed to copy user data\n");
		kfree(buf);
		return -EFAULT;
	}

	mutex_lock(&spectra_lock);
	result = GLOB_FTL_Page_Write(buf,
		(u64)info.page * IdentifyDeviceData.PageDataSize);
	mutex_unlock(&spectra_lock);

	kfree(buf);
	return result;
}

/* Return how many blocks should be reserved for bad block replacement */
static int get_res_blk_num_bad_blk(void)
{
	return IdentifyDeviceData.wDataBlockNum / 10;
}

/* Return how many blocks should be reserved for OS image */
static int get_res_blk_num_os(void)
{
	u32 res_blks, blk_size;

	blk_size = IdentifyDeviceData.PageDataSize *
		IdentifyDeviceData.PagesPerBlock;

	res_blks = (reserved_mb * 1024 * 1024) / blk_size;

	if ((res_blks < 1) || (res_blks >= IdentifyDeviceData.wDataBlockNum))
		res_blks = 1; /* Reserved 1 block for block table */

	return res_blks;
}

/* Transfer a full request. */
static int do_transfer(struct spectra_nand_dev *tr, struct request *req)
{
	u64 start_addr, addr;
	u32 logical_start_sect, hd_start_sect;
	u32 nsect, hd_sects;
	u32 rsect, tsect = 0;
	char *buf;
	u32 ratio = IdentifyDeviceData.PageDataSize >> 9;

	start_addr = (u64)(blk_rq_pos(req)) << 9;
	/* Add a big enough offset to prevent the OS Image from
	*  being accessed or damaged by file system */
	start_addr += IdentifyDeviceData.PageDataSize *
			IdentifyDeviceData.PagesPerBlock *
			res_blks_os;

	if (req->cmd_type & REQ_FLUSH) {
		if (force_flush_cache()) /* Fail to flush cache */
			return -EIO;
		else
			return 0;
	}

	if (req->cmd_type != REQ_TYPE_FS)
		return -EIO;

	if (blk_rq_pos(req) + blk_rq_cur_sectors(req) > get_capacity(tr->gd)) {
		printk(KERN_ERR "Spectra error: request over the NAND "
			"capacity!sector %d, current_nr_sectors %d, "
			"while capacity is %d\n",
			(int)blk_rq_pos(req),
			blk_rq_cur_sectors(req),
			(int)get_capacity(tr->gd));
		return -EIO;
	}

	logical_start_sect = start_addr >> 9;
	hd_start_sect = logical_start_sect / ratio;
	rsect = logical_start_sect - hd_start_sect * ratio;

	addr = (u64)hd_start_sect * ratio * 512;
	buf = req->buffer;
	nsect = blk_rq_cur_sectors(req);

	if (rsect)
		tsect =  (ratio - rsect) < nsect ? (ratio - rsect) : nsect;

	switch (rq_data_dir(req)) {
	case READ:
		/* Read the first NAND page */
		if (rsect) {
			if (GLOB_FTL_Page_Read(tr->tmp_buf, addr)) {
				printk(KERN_ERR "Error in %s, Line %d\n",
					__FILE__, __LINE__);
				return -EIO;
			}
			memcpy(buf, tr->tmp_buf + (rsect << 9), tsect << 9);
			addr += IdentifyDeviceData.PageDataSize;
			buf += tsect << 9;
			nsect -= tsect;
		}

		/* Read the other NAND pages */
		for (hd_sects = nsect / ratio; hd_sects > 0; hd_sects--) {
			if (GLOB_FTL_Page_Read(buf, addr)) {
				printk(KERN_ERR "Error in %s, Line %d\n",
					__FILE__, __LINE__);
				return -EIO;
			}
			addr += IdentifyDeviceData.PageDataSize;
			buf += IdentifyDeviceData.PageDataSize;
		}

		/* Read the last NAND pages */
		if (nsect % ratio) {
			if (GLOB_FTL_Page_Read(tr->tmp_buf, addr)) {
				printk(KERN_ERR "Error in %s, Line %d\n",
					__FILE__, __LINE__);
				return -EIO;
			}
			memcpy(buf, tr->tmp_buf, (nsect % ratio) << 9);
		}
#if CMD_DMA
		if (glob_ftl_execute_cmds())
			return -EIO;
		else
			return 0;
#endif
		return 0;

	case WRITE:
		/* Write the first NAND page */
		if (rsect) {
			if (GLOB_FTL_Page_Read(tr->tmp_buf, addr)) {
				printk(KERN_ERR "Error in %s, Line %d\n",
					__FILE__, __LINE__);
				return -EIO;
			}
			memcpy(tr->tmp_buf + (rsect << 9), buf, tsect << 9);
			if (GLOB_FTL_Page_Write(tr->tmp_buf, addr)) {
				printk(KERN_ERR "Error in %s, Line %d\n",
					__FILE__, __LINE__);
				return -EIO;
			}
			addr += IdentifyDeviceData.PageDataSize;
			buf += tsect << 9;
			nsect -= tsect;
		}

		/* Write the other NAND pages */
		for (hd_sects = nsect / ratio; hd_sects > 0; hd_sects--) {
			if (GLOB_FTL_Page_Write(buf, addr)) {
				printk(KERN_ERR "Error in %s, Line %d\n",
					__FILE__, __LINE__);
				return -EIO;
			}
			addr += IdentifyDeviceData.PageDataSize;
			buf += IdentifyDeviceData.PageDataSize;
		}

		/* Write the last NAND pages */
		if (nsect % ratio) {
			if (GLOB_FTL_Page_Read(tr->tmp_buf, addr)) {
				printk(KERN_ERR "Error in %s, Line %d\n",
					__FILE__, __LINE__);
				return -EIO;
			}
			memcpy(tr->tmp_buf, buf, (nsect % ratio) << 9);
			if (GLOB_FTL_Page_Write(tr->tmp_buf, addr)) {
				printk(KERN_ERR "Error in %s, Line %d\n",
					__FILE__, __LINE__);
				return -EIO;
			}
		}
#if CMD_DMA
		if (glob_ftl_execute_cmds())
			return -EIO;
		else
			return 0;
#endif
		return 0;

	default:
		printk(KERN_NOTICE "Unknown request %u\n", rq_data_dir(req));
		return -EIO;
	}
}

/* This function is copied from drivers/mtd/mtd_blkdevs.c */
static int spectra_trans_thread(void *arg)
{
	struct spectra_nand_dev *tr = arg;
	struct request_queue *rq = tr->queue;
	struct request *req = NULL;

	/* we might get involved when memory gets low, so use PF_MEMALLOC */
	current->flags |= PF_MEMALLOC;

	spin_lock_irq(rq->queue_lock);
	while (!kthread_should_stop()) {
		int res;

		if (!req) {
			req = blk_fetch_request(rq);
			if (!req) {
				set_current_state(TASK_INTERRUPTIBLE);
				spin_unlock_irq(rq->queue_lock);
				schedule();
				spin_lock_irq(rq->queue_lock);
				continue;
			}
		}

		spin_unlock_irq(rq->queue_lock);

		mutex_lock(&spectra_lock);
		res = do_transfer(tr, req);
		mutex_unlock(&spectra_lock);

		spin_lock_irq(rq->queue_lock);

		if (!__blk_end_request_cur(req, res))
			req = NULL;
	}

	if (req)
		__blk_end_request_all(req, -EIO);

	spin_unlock_irq(rq->queue_lock);

	return 0;
}


/* Request function that "handles clustering". */
static void GLOB_SBD_request(struct request_queue *rq)
{
	struct spectra_nand_dev *pdev = rq->queuedata;
	wake_up_process(pdev->thread);
}

static int GLOB_SBD_open(struct block_device *bdev, fmode_t mode)

{
	nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);
	return 0;
}

static int GLOB_SBD_release(struct gendisk *disk, fmode_t mode)
{
	int ret;

	nand_dbg_print(NAND_DBG_WARN, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	mutex_lock(&spectra_lock);
	ret = force_flush_cache();
	mutex_unlock(&spectra_lock);

	return 0;
}

static int GLOB_SBD_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	geo->heads = 4;
	geo->sectors = 16;
	geo->cylinders = get_capacity(bdev->bd_disk) / (4 * 16);

	nand_dbg_print(NAND_DBG_DEBUG,
		"heads: %d, sectors: %d, cylinders: %d\n",
		geo->heads, geo->sectors, geo->cylinders);

	return 0;
}

int GLOB_SBD_ioctl(struct block_device *bdev, fmode_t mode,
		unsigned int cmd, unsigned long arg)
{
	int ret;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	switch (cmd) {
	case GLOB_SBD_IOCTL_GC:
		nand_dbg_print(NAND_DBG_DEBUG,
			       "Spectra IOCTL: Garbage Collection "
			       "being performed\n");
		if (PASS != GLOB_FTL_Garbage_Collection())
			return -EFAULT;
		return 0;

	case GLOB_SBD_IOCTL_WL:
		nand_dbg_print(NAND_DBG_DEBUG,
			       "Spectra IOCTL: Static Wear Leveling "
			       "being performed\n");
		if (PASS != GLOB_FTL_Wear_Leveling())
			return -EFAULT;
		return 0;

	case GLOB_SBD_IOCTL_FORMAT:
		nand_dbg_print(NAND_DBG_DEBUG, "Spectra IOCTL: Flash format "
			       "being performed\n");
		if (PASS != GLOB_FTL_Flash_Format())
			return -EFAULT;
		return 0;

	case GLOB_SBD_IOCTL_FLUSH_CACHE:
		nand_dbg_print(NAND_DBG_DEBUG, "Spectra IOCTL: Cache flush "
			       "being performed\n");
		mutex_lock(&spectra_lock);
		ret = force_flush_cache();
		mutex_unlock(&spectra_lock);
		return ret;

	case GLOB_SBD_IOCTL_COPY_BLK_TABLE:
		nand_dbg_print(NAND_DBG_DEBUG, "Spectra IOCTL: "
			       "Copy block table\n");
		if (copy_to_user((void __user *)arg,
			get_blk_table_start_addr(),
			get_blk_table_len()))
			return -EFAULT;
		return 0;

	case GLOB_SBD_IOCTL_COPY_WEAR_LEVELING_TABLE:
		nand_dbg_print(NAND_DBG_DEBUG, "Spectra IOCTL: "
			       "Copy wear leveling table\n");
		if (copy_to_user((void __user *)arg,
			get_wear_leveling_table_start_addr(),
			get_wear_leveling_table_len()))
			return -EFAULT;
		return 0;

	case GLOB_SBD_IOCTL_GET_NAND_INFO:
		nand_dbg_print(NAND_DBG_DEBUG, "Spectra IOCTL: "
			       "Get NAND info\n");
		if (copy_to_user((void __user *)arg, &IdentifyDeviceData,
			sizeof(IdentifyDeviceData)))
			return -EFAULT;
		return 0;

	case GLOB_SBD_IOCTL_WRITE_DATA:
		nand_dbg_print(NAND_DBG_DEBUG, "Spectra IOCTL: "
			       "Write one page data\n");
		return ioctl_write_page_data(arg);

	case GLOB_SBD_IOCTL_READ_DATA:
		nand_dbg_print(NAND_DBG_DEBUG, "Spectra IOCTL: "
			       "Read one page data\n");
		return ioctl_read_page_data(arg);
	}

	return -ENOTTY;
}

int GLOB_SBD_unlocked_ioctl(struct block_device *bdev, fmode_t mode,
		unsigned int cmd, unsigned long arg)
{
	int ret;

	lock_kernel();
	ret = GLOB_SBD_ioctl(bdev, mode, cmd, arg);
	unlock_kernel();

	return ret;
}

static struct block_device_operations GLOB_SBD_ops = {
	.owner = THIS_MODULE,
	.open = GLOB_SBD_open,
	.release = GLOB_SBD_release,
	.ioctl = GLOB_SBD_unlocked_ioctl,
	.getgeo = GLOB_SBD_getgeo,
};

static int SBD_setup_device(struct spectra_nand_dev *dev, int which)
{
	int res_blks;
	u32 sects;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	memset(dev, 0, sizeof(struct spectra_nand_dev));

	nand_dbg_print(NAND_DBG_WARN, "Reserved %d blocks "
		"for OS image, %d blocks for bad block replacement.\n",
		get_res_blk_num_os(),
		get_res_blk_num_bad_blk());

	res_blks = get_res_blk_num_bad_blk() + get_res_blk_num_os();

	dev->size = (u64)IdentifyDeviceData.PageDataSize *
		IdentifyDeviceData.PagesPerBlock *
		(IdentifyDeviceData.wDataBlockNum - res_blks);

	res_blks_os = get_res_blk_num_os();

	spin_lock_init(&dev->qlock);

	dev->tmp_buf = kmalloc(IdentifyDeviceData.PageDataSize, GFP_ATOMIC);
	if (!dev->tmp_buf) {
		printk(KERN_ERR "Failed to kmalloc memory in %s Line %d, exit.\n",
			__FILE__, __LINE__);
		goto out_vfree;
	}

	dev->queue = blk_init_queue(GLOB_SBD_request, &dev->qlock);
	if (dev->queue == NULL) {
		printk(KERN_ERR
		       "Spectra: Request queue could not be initialized."
			" Aborting\n ");
		goto out_vfree;
	}
	dev->queue->queuedata = dev;

	/* As Linux block layer doens't support >4KB hardware sector,  */
	/* Here we force report 512 byte hardware sector size to Kernel */
	blk_queue_logical_block_size(dev->queue, 512);

	blk_queue_ordered(dev->queue, QUEUE_ORDERED_DRAIN_FLUSH);

	dev->thread = kthread_run(spectra_trans_thread, dev, "nand_thd");
	if (IS_ERR(dev->thread)) {
		blk_cleanup_queue(dev->queue);
		unregister_blkdev(GLOB_SBD_majornum, GLOB_SBD_NAME);
		return PTR_ERR(dev->thread);
	}

	dev->gd = alloc_disk(PARTITIONS);
	if (!dev->gd) {
		printk(KERN_ERR
		       "Spectra: Could not allocate disk. Aborting \n ");
		goto out_vfree;
	}
	dev->gd->major = GLOB_SBD_majornum;
	dev->gd->first_minor = which * PARTITIONS;
	dev->gd->fops = &GLOB_SBD_ops;
	dev->gd->queue = dev->queue;
	dev->gd->private_data = dev;
	snprintf(dev->gd->disk_name, 32, "%s%c", GLOB_SBD_NAME, which + 'a');

	sects = dev->size >> 9;
	nand_dbg_print(NAND_DBG_WARN, "Capacity sects: %d\n", sects);
	set_capacity(dev->gd, sects);

	add_disk(dev->gd);

	return 0;
out_vfree:
	return -ENOMEM;
}

/*
static ssize_t show_nand_block_num(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",
		(int)IdentifyDeviceData.wDataBlockNum);
}

static ssize_t show_nand_pages_per_block(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",
		(int)IdentifyDeviceData.PagesPerBlock);
}

static ssize_t show_nand_page_size(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",
		(int)IdentifyDeviceData.PageDataSize);
}

static DEVICE_ATTR(nand_block_num, 0444, show_nand_block_num, NULL);
static DEVICE_ATTR(nand_pages_per_block, 0444, show_nand_pages_per_block, NULL);
static DEVICE_ATTR(nand_page_size, 0444, show_nand_page_size, NULL);

static void create_sysfs_entry(struct device *dev)
{
	if (device_create_file(dev, &dev_attr_nand_block_num))
		printk(KERN_ERR "Spectra: "
			"failed to create sysfs entry nand_block_num.\n");
	if (device_create_file(dev, &dev_attr_nand_pages_per_block))
		printk(KERN_ERR "Spectra: "
		"failed to create sysfs entry nand_pages_per_block.\n");
	if (device_create_file(dev, &dev_attr_nand_page_size))
		printk(KERN_ERR "Spectra: "
		"failed to create sysfs entry nand_page_size.\n");
}
*/

static int GLOB_SBD_init(void)
{
	int i;

	/* Set debug output level (0~3) here. 3 is most verbose */
	printk(KERN_ALERT "Spectra: %s\n", GLOB_version);

	mutex_init(&spectra_lock);

	GLOB_SBD_majornum = register_blkdev(0, GLOB_SBD_NAME);
	if (GLOB_SBD_majornum <= 0) {
		printk(KERN_ERR "Unable to get the major %d for Spectra",
		       GLOB_SBD_majornum);
		return -EBUSY;
	}

	if (PASS != GLOB_FTL_Flash_Init()) {
		printk(KERN_ERR "Spectra: Unable to Initialize Flash Device. "
		       "Aborting\n");
		goto out_flash_register;
	}

	/* create_sysfs_entry(&dev->dev); */

	if (PASS != GLOB_FTL_IdentifyDevice(&IdentifyDeviceData)) {
		printk(KERN_ERR "Spectra: Unable to Read Flash Device. "
		       "Aborting\n");
		goto out_flash_register;
	} else {
		nand_dbg_print(NAND_DBG_WARN, "In GLOB_SBD_init: "
			       "Num blocks=%d, pagesperblock=%d, "
			       "pagedatasize=%d, ECCBytesPerSector=%d\n",
		       (int)IdentifyDeviceData.NumBlocks,
		       (int)IdentifyDeviceData.PagesPerBlock,
		       (int)IdentifyDeviceData.PageDataSize,
		       (int)IdentifyDeviceData.wECCBytesPerSector);
	}

	printk(KERN_ALERT "Spectra: searching block table, please wait ...\n");
	if (GLOB_FTL_Init() != PASS) {
		printk(KERN_ERR "Spectra: Unable to Initialize FTL Layer. "
		       "Aborting\n");
		goto out_ftl_flash_register;
	}
	printk(KERN_ALERT "Spectra: block table has been found.\n");

	for (i = 0; i < NUM_DEVICES; i++)
		if (SBD_setup_device(&nand_device[i], i) == -ENOMEM)
			goto out_ftl_flash_register;

	nand_dbg_print(NAND_DBG_DEBUG,
		       "Spectra: module loaded with major number %d\n",
		       GLOB_SBD_majornum);

	return 0;

out_ftl_flash_register:
	GLOB_FTL_Cache_Release();
out_flash_register:
	GLOB_FTL_Flash_Release();
	unregister_blkdev(GLOB_SBD_majornum, GLOB_SBD_NAME);
	printk(KERN_ERR "Spectra: Module load failed.\n");

	return -ENOMEM;
}

static void __exit GLOB_SBD_exit(void)
{
	int i;

	nand_dbg_print(NAND_DBG_TRACE, "%s, Line %d, Function: %s\n",
		       __FILE__, __LINE__, __func__);

	for (i = 0; i < NUM_DEVICES; i++) {
		struct spectra_nand_dev *dev = &nand_device[i];
		if (dev->gd) {
			del_gendisk(dev->gd);
			put_disk(dev->gd);
		}
		if (dev->queue)
			blk_cleanup_queue(dev->queue);
		kfree(dev->tmp_buf);
	}

	unregister_blkdev(GLOB_SBD_majornum, GLOB_SBD_NAME);

	mutex_lock(&spectra_lock);
	force_flush_cache();
	mutex_unlock(&spectra_lock);

	GLOB_FTL_Cache_Release();

	GLOB_FTL_Flash_Release();

	nand_dbg_print(NAND_DBG_DEBUG,
		       "Spectra FTL module (major number %d) unloaded.\n",
		       GLOB_SBD_majornum);
}

module_init(GLOB_SBD_init);
module_exit(GLOB_SBD_exit);
