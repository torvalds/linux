/* cyanblkdev_block.c - West Bridge Linux Block Driver source file
## ===========================
## Copyright (C) 2010  Cypress Semiconductor
##
## This program is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License
## as published by the Free Software Foundation; either version 2
## of the License, or (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin Street, Fifth Floor
## Boston, MA  02110-1301, USA.
## ===========================
*/

/*
 * Linux block driver implementation for Cypress West Bridge.
 * Based on the mmc block driver implementation by Andrew Christian
 * for the linux 2.6.26 kernel.
 * mmc_block.c, 5/28/2002
 */

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
 *		  28 May 2002
 */

#include <linux/moduleparam.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include <linux/kdev_t.h>
#include <linux/blkdev.h>

#include <asm/system.h>
#include <linux/uaccess.h>

#include <linux/scatterlist.h>
#include <linux/time.h>
#include <linux/signal.h>
#include <linux/delay.h>

#include "cyasblkdev_queue.h"

#define CYASBLKDEV_SHIFT	0 /* Only a single partition. */
#define CYASBLKDEV_MAX_REQ_LEN	(256)
#define CYASBLKDEV_NUM_MINORS	(256 >> CYASBLKDEV_SHIFT)
#define CY_AS_TEST_NUM_BLOCKS   (64)
#define CYASBLKDEV_MINOR_0 1
#define CYASBLKDEV_MINOR_1 2
#define CYASBLKDEV_MINOR_2 3

static int major;
module_param(major, int, 0444);
MODULE_PARM_DESC(major,
	"specify the major device number for cyasblkdev block driver");

/* parameters passed from the user space */
static int vfat_search;
module_param(vfat_search, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(vfat_search,
	"dynamically find the location of the first sector");

static int private_partition_bus = -1;
module_param(private_partition_bus, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(private_partition_bus,
	"bus number for private partition");

static int private_partition_size = -1;
module_param(private_partition_size, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(private_partition_size,
	"size of the private partition");

/*
 * There is one cyasblkdev_blk_data per slot.
 */
struct cyasblkdev_blk_data {
	spinlock_t	  lock;
	int media_count[2];
	const struct block_device_operations *blkops;
	unsigned int	usage;
	unsigned int	suspended;

	/* handle to the west bridge device this handle, typdefed as *void  */
	cy_as_device_handle		dev_handle;

	/* our custom structure, in addition to request queue,
	 * adds lock & semaphore items*/
	struct cyasblkdev_queue queue;

	/* 16 entries is enough given max request size
	 * 16 * 4K (64 K per request)*/
	struct scatterlist	  sg[16];

	/* non-zero enables printk of executed reqests */
	unsigned int	dbgprn_flags;

	/*gen_disk for private, system disk */
	struct gendisk  *system_disk;
	cy_as_media_type   system_disk_type;
	cy_bool			 system_disk_read_only;
	cy_bool			 system_disk_bus_num;

	/* sector size for the medium */
	unsigned int	system_disk_blk_size;
	unsigned int	system_disk_first_sector;
	unsigned int	system_disk_unit_no;

	/*gen_disk for bus 0 */
	struct gendisk  *user_disk_0;
	cy_as_media_type   user_disk_0_type;
	cy_bool			 user_disk_0_read_only;
	cy_bool			 user_disk_0_bus_num;

	/* sector size for the medium */
	unsigned int	user_disk_0_blk_size;
	unsigned int	user_disk_0_first_sector;
	unsigned int	user_disk_0_unit_no;

	/*gen_disk for bus 1 */
	struct gendisk  *user_disk_1;
	cy_as_media_type   user_disk_1_type;
	cy_bool			 user_disk_1_read_only;
	cy_bool			 user_disk_1_bus_num;

	/* sector size for the medium */
	unsigned int	user_disk_1_blk_size;
	unsigned int	user_disk_1_first_sector;
	unsigned int	user_disk_1_unit_no;
};

/* pointer to west bridge block data device superstructure */
static struct cyasblkdev_blk_data *gl_bd;

static DECLARE_MUTEX(open_lock);

/* local forwardd declarationss  */
static cy_as_device_handle *cyas_dev_handle;
static void cyasblkdev_blk_deinit(struct cyasblkdev_blk_data *bd);

/*change debug print options */
 #define DBGPRN_RD_RQ	   (1 < 0)
 #define DBGPRN_WR_RQ		(1 < 1)
 #define DBGPRN_RQ_END	  (1 < 2)

int blkdev_ctl_dbgprn(
						int prn_flags
						)
{
	int cur_options = gl_bd->dbgprn_flags;

	DBGPRN_FUNC_NAME;

	/* set new debug print options */
	gl_bd->dbgprn_flags = prn_flags;

	/* return previous */
	return cur_options;
}
EXPORT_SYMBOL(blkdev_ctl_dbgprn);

static struct cyasblkdev_blk_data *cyasblkdev_blk_get(
							struct gendisk *disk
							)
{
	struct cyasblkdev_blk_data *bd;

	DBGPRN_FUNC_NAME;

	down(&open_lock);

	bd = disk->private_data;

	if (bd && (bd->usage == 0))
		bd = NULL;

	if (bd) {
		bd->usage++;
		#ifndef NBDEBUG
		cy_as_hal_print_message(
			"cyasblkdev_blk_get: usage = %d\n", bd->usage);
		#endif
	}
	up(&open_lock);

	return bd;
}

static void cyasblkdev_blk_put(
			struct cyasblkdev_blk_data *bd
			)
{
	DBGPRN_FUNC_NAME;

	down(&open_lock);

	if (bd) {
		bd->usage--;
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
			" cyasblkdev_blk_put , bd->usage= %d\n", bd->usage);
		#endif
	} else  {
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
			"cyasblkdev: blk_put(bd) on bd = NULL!: usage = %d\n",
			bd->usage);
		#endif
		up(&open_lock);
		return;
	}

	if (bd->usage == 0) {
		put_disk(bd->user_disk_0);
		put_disk(bd->user_disk_1);
		put_disk(bd->system_disk);
		cyasblkdev_cleanup_queue(&bd->queue);

		if (CY_AS_ERROR_SUCCESS !=
			cy_as_storage_release(bd->dev_handle, 0, 0, 0, 0)) {
			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message(
				"cyasblkdev: cannot release bus 0\n");
			#endif
		}

		if (CY_AS_ERROR_SUCCESS !=
			cy_as_storage_release(bd->dev_handle, 1, 0, 0, 0)) {
			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message(
				"cyasblkdev: cannot release bus 1\n");
			#endif
		}

		if (CY_AS_ERROR_SUCCESS !=
			cy_as_storage_stop(bd->dev_handle, 0, 0)) {
			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message(
				"cyasblkdev: cannot stop storage stack\n");
			#endif
		}

	#ifdef __CY_ASTORIA_SCM_KERNEL_HAL__
		/* If the SCM Kernel HAL is being used, disable the use
		 * of scatter/gather lists at the end of block driver usage.
		 */
		cy_as_hal_disable_scatter_list(cyasdevice_gethaltag());
	#endif

		/*ptr to global struct cyasblkdev_blk_data */
		gl_bd = NULL;
		kfree(bd);
	}

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message(
		"cyasblkdev (blk_put): usage = %d\n",
		bd->usage);
	#endif
	up(&open_lock);
}

static int cyasblkdev_blk_open(
					struct block_device *bdev,
					fmode_t mode
					)
{
	struct cyasblkdev_blk_data *bd = cyasblkdev_blk_get(bdev->bd_disk);
	int ret = -ENXIO;

	DBGPRN_FUNC_NAME;

	if (bd) {
		if (bd->usage == 2)
			check_disk_change(bdev);

		ret = 0;

		if (bdev->bd_disk == bd->user_disk_0) {
			if ((mode & FMODE_WRITE) && bd->user_disk_0_read_only) {
				#ifndef WESTBRIDGE_NDEBUG
				cy_as_hal_print_message(
					"device marked as readonly "
					"and write requested\n");
				#endif

				cyasblkdev_blk_put(bd);
				ret = -EROFS;
			}
		} else if (bdev->bd_disk == bd->user_disk_1) {
			if ((mode & FMODE_WRITE) && bd->user_disk_1_read_only) {
				#ifndef WESTBRIDGE_NDEBUG
				cy_as_hal_print_message(
					"device marked as readonly "
					"and write requested\n");
				#endif

				cyasblkdev_blk_put(bd);
				ret = -EROFS;
			}
		} else if (bdev->bd_disk == bd->system_disk) {
			if ((mode & FMODE_WRITE) && bd->system_disk_read_only) {
				#ifndef WESTBRIDGE_NDEBUG
				cy_as_hal_print_message(
					"device marked as readonly "
					"and write requested\n");
				#endif

				cyasblkdev_blk_put(bd);
				ret = -EROFS;
			}
		}
	}

	return ret;
}

static int cyasblkdev_blk_release(
					struct gendisk *disk,
					fmode_t mode
					)
{
	struct cyasblkdev_blk_data *bd = disk->private_data;

	DBGPRN_FUNC_NAME;

	cyasblkdev_blk_put(bd);
	return 0;
}

static int cyasblkdev_blk_ioctl(
			struct block_device *bdev,
			fmode_t mode,
			unsigned int cmd,
			unsigned long arg
			)
{
	DBGPRN_FUNC_NAME;

	if (cmd == HDIO_GETGEO) {
		/*for now  we only process geometry IOCTL*/
		struct hd_geometry geo;

		memset(&geo, 0, sizeof(struct hd_geometry));

		geo.cylinders	= get_capacity(bdev->bd_disk) / (4 * 16);
		geo.heads	= 4;
		geo.sectors	= 16;
		geo.start	= get_start_sect(bdev);

		/* copy to user space */
		return copy_to_user((void __user *)arg, &geo, sizeof(geo))
			? -EFAULT : 0;
	}

	return -ENOTTY;
}

/* Media_changed block_device opp
 * this one is called by kernel to confirm if the media really changed
 * as we indicated by issuing check_disk_change() call */
int cyasblkdev_media_changed(struct gendisk *gd)
{
	struct cyasblkdev_blk_data *bd;

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("cyasblkdev_media_changed() is called\n");
	#endif

	if (gd)
		bd = gd->private_data;
	else {
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
			"cyasblkdev_media_changed() is called, "
			"but gd is null\n");
		#endif
	}

	/* return media change state "1" yes, 0 no */
	return 1;
}

/*  this one called by kernel to give us a chence
 * to prep the new media before it starts to rescaning
 * of the newlly inserted SD media */
int cyasblkdev_revalidate_disk(struct gendisk *gd)
{
	/*int (*revalidate_disk) (struct gendisk *); */

	#ifndef WESTBRIDGE_NDEBUG
	if (gd)
		cy_as_hal_print_message(
			"cyasblkdev_revalidate_disk() is called, "
			"(gl_bd->usage:%d)\n", gl_bd->usage);
	#endif

	/* 0 means ok, kern can go ahead with partition rescan */
	return 0;
}


/*standard block device driver interface */
static struct block_device_operations cyasblkdev_bdops = {
	.open			= cyasblkdev_blk_open,
	.release		= cyasblkdev_blk_release,
	.ioctl			= cyasblkdev_blk_ioctl,
	/* .getgeo		= cyasblkdev_blk_getgeo, */
	/* added to support media removal( real and simulated) media */
	.media_changed  = cyasblkdev_media_changed,
	/* added to support media removal( real and simulated) media */
	.revalidate_disk = cyasblkdev_revalidate_disk,
	.owner			= THIS_MODULE,
};

/* west bridge block device prep request function */
static int cyasblkdev_blk_prep_rq(
					struct cyasblkdev_queue *bq,
					struct request *req
					)
{
	struct cyasblkdev_blk_data *bd = bq->data;
	int stat = BLKPREP_OK;

	DBGPRN_FUNC_NAME;

	/* If we have no device, we haven't finished initialising. */
	if (!bd || !bd->dev_handle) {
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(KERN_ERR
			"cyasblkdev %s: killing request - no device/host\n",
			req->rq_disk->disk_name);
		#endif
		stat = BLKPREP_KILL;
	}

	if (bd->suspended) {
		blk_plug_device(bd->queue.queue);
		stat = BLKPREP_DEFER;
	}

	/* Check for excessive requests.*/
	if (blk_rq_pos(req) + blk_rq_sectors(req) > get_capacity(req->rq_disk)) {
		cy_as_hal_print_message("cyasblkdev: bad request address\n");
		stat = BLKPREP_KILL;
	}

	return stat;
}

/*west bridge storage async api on_completed callback */
static void cyasblkdev_issuecallback(
	/* Handle to the device completing the storage operation */
	cy_as_device_handle handle,
	/* The media type completing the operation */
	cy_as_media_type type,
	/* The device completing the operation */
	uint32_t device,
	/* The unit completing the operation */
	uint32_t unit,
	/* The block number of the completed operation */
	uint32_t block_number,
	/* The type of operation */
	cy_as_oper_type op,
	/* The error status */
	cy_as_return_status_t status
	)
{
	int retry_cnt = 0;
	DBGPRN_FUNC_NAME;

	if (status != CY_AS_ERROR_SUCCESS) {
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
		  "%s: async r/w: op:%d failed with error %d at address %d\n",
			__func__, op, status, block_number);
		#endif
	}

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message(
		"%s calling blk_end_request from issue_callback "
		"req=0x%x, status=0x%x, nr_sectors=0x%x\n",
		__func__, (unsigned int) gl_bd->queue.req, status,
		(unsigned int) blk_rq_sectors(gl_bd->queue.req));
	#endif

	/* note: blk_end_request w/o __ prefix should
	 * not require spinlocks on the queue*/
	while (blk_end_request(gl_bd->queue.req,
	status, blk_rq_sectors(gl_bd->queue.req)*512)) {
		retry_cnt++;
	};

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message(
		"%s blkdev_callback: ended rq on %d sectors, "
		"with err:%d, n:%d times\n", __func__,
		(int)blk_rq_sectors(gl_bd->queue.req), status,
		retry_cnt
	);
	#endif

	spin_lock_irq(&gl_bd->lock);

	/*elevate next request, if there is one*/
	if (!blk_queue_plugged(gl_bd->queue.queue)) {
		/* queue is not plugged */
		gl_bd->queue.req = blk_fetch_request(gl_bd->queue.queue);
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s blkdev_callback: "
		"blk_fetch_request():%p\n",
			__func__, gl_bd->queue.req);
		#endif
	}

	if (gl_bd->queue.req) {
		spin_unlock_irq(&gl_bd->lock);

		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s blkdev_callback: about to "
		"call issue_fn:%p\n", __func__, gl_bd->queue.req);
		#endif

		gl_bd->queue.issue_fn(&gl_bd->queue, gl_bd->queue.req);
	} else {
		spin_unlock_irq(&gl_bd->lock);
	}
}

/* issue astoria blkdev request (issue_fn) */
static int cyasblkdev_blk_issue_rq(
					struct cyasblkdev_queue *bq,
					struct request *req
					)
{
	struct cyasblkdev_blk_data *bd = bq->data;
	int index = 0;
	int ret = CY_AS_ERROR_SUCCESS;
	uint32_t req_sector = 0;
	uint32_t req_nr_sectors = 0;
	int bus_num = 0;
	int lcl_unit_no = 0;

	DBGPRN_FUNC_NAME;

	/*
	 * will construct a scatterlist for the given request;
	 * the return value is the number of actually used
	 * entries in the resulting list. Then, this scatterlist
	 * can be used for the actual DMA prep operation.
	 */
	spin_lock_irq(&bd->lock);
	index = blk_rq_map_sg(bq->queue, req, bd->sg);

	if (req->rq_disk == bd->user_disk_0) {
		bus_num = bd->user_disk_0_bus_num;
		req_sector = blk_rq_pos(req) + gl_bd->user_disk_0_first_sector;
		req_nr_sectors = blk_rq_sectors(req);
		lcl_unit_no = gl_bd->user_disk_0_unit_no;

		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s: request made to disk 0 "
			"for sector=%d, num_sectors=%d, unit_no=%d\n",
			__func__, req_sector, (int) blk_rq_sectors(req),
			lcl_unit_no);
		#endif
	} else if (req->rq_disk == bd->user_disk_1) {
		bus_num = bd->user_disk_1_bus_num;
		req_sector = blk_rq_pos(req) + gl_bd->user_disk_1_first_sector;
		/*SECT_NUM_TRANSLATE(blk_rq_sectors(req));*/
		req_nr_sectors = blk_rq_sectors(req);
		lcl_unit_no = gl_bd->user_disk_1_unit_no;

		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s: request made to disk 1 for "
			"sector=%d, num_sectors=%d, unit_no=%d\n", __func__,
			req_sector, (int) blk_rq_sectors(req), lcl_unit_no);
		#endif
	} else if (req->rq_disk == bd->system_disk) {
		bus_num = bd->system_disk_bus_num;
		req_sector = blk_rq_pos(req) + gl_bd->system_disk_first_sector;
		req_nr_sectors = blk_rq_sectors(req);
		lcl_unit_no = gl_bd->system_disk_unit_no;

		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s: request made to system disk "
			"for sector=%d, num_sectors=%d, unit_no=%d\n", __func__,
			req_sector, (int) blk_rq_sectors(req), lcl_unit_no);
		#endif
	}
	#ifndef WESTBRIDGE_NDEBUG
	else {
		cy_as_hal_print_message(
			"%s: invalid disk used for request\n", __func__);
	}
	#endif

	spin_unlock_irq(&bd->lock);

	if (rq_data_dir(req) == READ) {
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s: calling readasync() "
			"req_sector=0x%x, req_nr_sectors=0x%x, bd->sg:%x\n\n",
			__func__, req_sector, req_nr_sectors, (uint32_t)bd->sg);
		#endif

		ret = cy_as_storage_read_async(bd->dev_handle, bus_num, 0,
			lcl_unit_no, req_sector, bd->sg, req_nr_sectors,
			(cy_as_storage_callback)cyasblkdev_issuecallback);

		if (ret != CY_AS_ERROR_SUCCESS) {
			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message("%s:readasync() error %d at "
				"address %ld, unit no %d\n", __func__, ret,
				blk_rq_pos(req), lcl_unit_no);
			cy_as_hal_print_message("%s:ending i/o request "
				"on reg:%x\n", __func__, (uint32_t)req);
			#endif

			while (blk_end_request(req,
				(ret == CY_AS_ERROR_SUCCESS),
				req_nr_sectors*512))
				;

			bq->req = NULL;
		}
	} else {
		ret = cy_as_storage_write_async(bd->dev_handle, bus_num, 0,
			lcl_unit_no, req_sector, bd->sg, req_nr_sectors,
			(cy_as_storage_callback)cyasblkdev_issuecallback);

		if (ret != CY_AS_ERROR_SUCCESS) {
			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message("%s: write failed with "
			"error %d at address %ld, unit no %d\n",
			__func__, ret, blk_rq_pos(req), lcl_unit_no);
			#endif

			/*end IO op on this request(does both
			 * end_that_request_... _first & _last) */
			while (blk_end_request(req,
				(ret == CY_AS_ERROR_SUCCESS),
				req_nr_sectors*512))
				;

			bq->req = NULL;
		}
	}

	return ret;
}

static unsigned long
dev_use[CYASBLKDEV_NUM_MINORS / (8 * sizeof(unsigned long))];


/* storage event callback (note: called in astoria isr context) */
static void cyasblkdev_storage_callback(
					cy_as_device_handle dev_h,
					cy_as_bus_number_t bus,
					uint32_t device,
					cy_as_storage_event evtype,
					void *evdata
					)
{
	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("%s: bus:%d, device:%d, evtype:%d, "
	"evdata:%p\n ", __func__, bus, device, evtype, evdata);
	#endif

	switch (evtype) {
	case cy_as_storage_processor:
		break;

	case cy_as_storage_removed:
		break;

	case cy_as_storage_inserted:
		break;

	default:
		break;
	}
}

#define SECTORS_TO_SCAN 4096

uint32_t cyasblkdev_get_vfat_offset(int bus_num, int unit_no)
{
	/*
	 * for sd media, vfat partition boot record is not always
	 * located at sector it greatly depends on the system and
	 * software that was used to format the sd however, linux
	 * fs layer always expects it at sector 0, this function
	 * finds the offset and then uses it in all media r/w
	 * operations
	 */
	int sect_no, stat;
	uint8_t *sect_buf;
	bool br_found = false;

	DBGPRN_FUNC_NAME;

	sect_buf = kmalloc(1024, GFP_KERNEL);

	/* since HAL layer always uses sg lists instead of the
	 * buffer (for hw dmas) we need to initialize the sg list
	 * for local buffer*/
	sg_init_one(gl_bd->sg, sect_buf, 512);

	/*
	* Check MPR partition table 1st, then try to scan through
	* 1st 384 sectors until BR signature(intel JMP istruction
	* code and ,0x55AA) is found
	*/
	#ifndef WESTBRIDGE_NDEBUG
	  cy_as_hal_print_message(
		"%s scanning media for vfat partition...\n", __func__);
	#endif

	for (sect_no = 0; sect_no < SECTORS_TO_SCAN; sect_no++) {
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s before cyasstorageread "
			"gl_bd->sg addr=0x%x\n", __func__,
			(unsigned int) gl_bd->sg);
		#endif

		stat = cy_as_storage_read(
			/* Handle to the device of interest */
			gl_bd->dev_handle,
			/* The bus to access */
			bus_num,
			/* The device to access */
			0,
			/* The unit to access */
			unit_no,
			/* absolute sector number */
			sect_no,
			/* sg structure */
			gl_bd->sg,
			/* The number of blocks to be read */
			1
		);

		/* try only sectors with boot signature */
		if ((sect_buf[510] == 0x55) && (sect_buf[511] == 0xaa)) {
			/* vfat boot record may also be located at
			 * sector 0, check it first  */
			if (sect_buf[0] == 0xEB) {
				#ifndef WESTBRIDGE_NDEBUG
				cy_as_hal_print_message(
					"%s vfat partition found "
					"at sector:%d\n",
					__func__, sect_no);
				#endif

				br_found = true;
				   break;
			}
		}

		if (stat != 0) {
			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message("%s sector scan error\n",
				__func__);
			#endif
			break;
		}
	}

	kfree(sect_buf);

	if (br_found) {
		return sect_no;
	} else {
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
			"%s vfat partition is not found, using 0 offset\n",
			__func__);
		#endif
		return 0;
	}
}

cy_as_storage_query_device_data dev_data = {0};

static int cyasblkdev_add_disks(int bus_num,
	struct cyasblkdev_blk_data *bd,
	int total_media_count,
	int devidx)
{
	int ret = 0;
	uint64_t disk_cap;
	int lcl_unit_no;
	cy_as_storage_query_unit_data unit_data = {0};

	#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s:query device: "
		"type:%d, removable:%d, writable:%d, "
		"blksize %d, units:%d, locked:%d, "
		"erase_sz:%d\n",
		__func__,
		dev_data.desc_p.type,
		dev_data.desc_p.removable,
		dev_data.desc_p.writeable,
		dev_data.desc_p.block_size,
		dev_data.desc_p.number_units,
		dev_data.desc_p.locked,
		dev_data.desc_p.erase_unit_size
		);
	#endif

	/*  make sure that device is not locked  */
	if (dev_data.desc_p.locked) {
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
			"%s: device is locked\n", __func__);
		#endif
		ret = cy_as_storage_release(
			bd->dev_handle, bus_num, 0, 0, 0);
		if (ret != CY_AS_ERROR_SUCCESS) {
			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message("%s cannot release"
				" storage\n", __func__);
			#endif
			goto out;
		}
		goto out;
	}

	unit_data.device = 0;
	unit_data.unit   = 0;
	unit_data.bus    = bus_num;
	ret = cy_as_storage_query_unit(bd->dev_handle,
		&unit_data, 0, 0);
	if (ret != CY_AS_ERROR_SUCCESS) {
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s: cannot query "
			"%d device unit - reason code %d\n",
			__func__, bus_num, ret);
		#endif
		goto out;
	}

	if (private_partition_bus == bus_num) {
		if (private_partition_size > 0) {
			ret = cy_as_storage_create_p_partition(
				bd->dev_handle, bus_num, 0,
				private_partition_size, 0, 0);
			if ((ret != CY_AS_ERROR_SUCCESS) &&
			(ret != CY_AS_ERROR_ALREADY_PARTITIONED)) {
			#ifndef WESTBRIDGE_NDEBUG
				cy_as_hal_print_message("%s: cy_as_storage_"
				"create_p_partition after size > 0 check "
				"failed with error code %d\n",
				__func__, ret);
			#endif

				disk_cap = (uint64_t)
					(unit_data.desc_p.unit_size);
				lcl_unit_no = 0;

			} else if (ret == CY_AS_ERROR_ALREADY_PARTITIONED) {
				#ifndef WESTBRIDGE_NDEBUG
				cy_as_hal_print_message(
				"%s: cy_as_storage_create_p_partition "
				"indicates memory already partitioned\n",
				__func__);
				#endif

				/*check to see that partition
				 * matches size */
				if (unit_data.desc_p.unit_size !=
					private_partition_size) {
					ret = cy_as_storage_remove_p_partition(
						bd->dev_handle,
						bus_num, 0, 0, 0);
					if (ret == CY_AS_ERROR_SUCCESS) {
						ret = cy_as_storage_create_p_partition(
							bd->dev_handle, bus_num, 0,
							private_partition_size, 0, 0);
						if (ret == CY_AS_ERROR_SUCCESS) {
							unit_data.bus = bus_num;
							unit_data.device = 0;
							unit_data.unit = 1;
						} else {
							#ifndef WESTBRIDGE_NDEBUG
							cy_as_hal_print_message(
							"%s: cy_as_storage_create_p_partition "
							"after removal unexpectedly failed "
							"with error %d\n", __func__, ret);
							#endif

							/* need to requery bus
							 * seeing as delete
							 * successful and create
							 * failed we have changed
							 * the disk properties */
							unit_data.bus	= bus_num;
							unit_data.device = 0;
							unit_data.unit   = 0;
						}

						ret = cy_as_storage_query_unit(
						bd->dev_handle,
						&unit_data, 0, 0);
						if (ret != CY_AS_ERROR_SUCCESS) {
							#ifndef WESTBRIDGE_NDEBUG
							cy_as_hal_print_message(
							"%s: cannot query %d "
							"device unit - reason code %d\n",
							__func__, bus_num, ret);
							#endif
							goto out;
						} else {
							disk_cap = (uint64_t)
								(unit_data.desc_p.unit_size);
							lcl_unit_no =
								unit_data.unit;
						}
					} else {
					#ifndef WESTBRIDGE_NDEBUG
					cy_as_hal_print_message(
					"%s: cy_as_storage_remove_p_partition "
					"failed with error %d\n",
					__func__, ret);
					#endif

						unit_data.bus = bus_num;
						unit_data.device = 0;
						unit_data.unit = 1;

						ret = cy_as_storage_query_unit(
							bd->dev_handle, &unit_data, 0, 0);
						if (ret != CY_AS_ERROR_SUCCESS) {
						#ifndef WESTBRIDGE_NDEBUG
							cy_as_hal_print_message(
							"%s: cannot query %d "
							"device unit - reason "
							"code %d\n", __func__,
							bus_num, ret);
						#endif
							goto out;
						}

						disk_cap = (uint64_t)
							(unit_data.desc_p.unit_size);
						lcl_unit_no =
							unit_data.unit;
					}
				} else {
					#ifndef WESTBRIDGE_NDEBUG
					cy_as_hal_print_message("%s: partition "
						"exists and sizes equal\n",
						__func__);
					#endif

					/*partition already existed,
					 * need to query second unit*/
					unit_data.bus = bus_num;
					unit_data.device = 0;
					unit_data.unit = 1;

					ret = cy_as_storage_query_unit(
						bd->dev_handle, &unit_data, 0, 0);
					if (ret != CY_AS_ERROR_SUCCESS) {
					#ifndef WESTBRIDGE_NDEBUG
						cy_as_hal_print_message(
							"%s: cannot query %d "
							"device unit "
							"- reason code %d\n",
							__func__, bus_num, ret);
					#endif
						goto out;
					} else {
						disk_cap = (uint64_t)
						(unit_data.desc_p.unit_size);
						lcl_unit_no = unit_data.unit;
					}
				}
			} else {
				#ifndef WESTBRIDGE_NDEBUG
				cy_as_hal_print_message(
				"%s: cy_as_storage_create_p_partition "
				"created successfully\n", __func__);
				#endif

				disk_cap = (uint64_t)
				(unit_data.desc_p.unit_size -
				private_partition_size);

				lcl_unit_no = 1;
			}
		}
		#ifndef WESTBRIDGE_NDEBUG
		else {
			cy_as_hal_print_message(
			"%s: invalid partition_size%d\n", __func__,
			private_partition_size);

			disk_cap = (uint64_t)
				(unit_data.desc_p.unit_size);
			lcl_unit_no = 0;
		}
		#endif
	} else {
		disk_cap = (uint64_t)
			(unit_data.desc_p.unit_size);
		lcl_unit_no = 0;
	}

	if ((bus_num == 0) ||
		(total_media_count == 1)) {
		sprintf(bd->user_disk_0->disk_name,
			"cyasblkdevblk%d", devidx);

		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
			"%s: disk unit_sz:%lu blk_sz:%d, "
			"start_blk:%lu, capacity:%llu\n",
			__func__, (unsigned long)
			unit_data.desc_p.unit_size,
			unit_data.desc_p.block_size,
			(unsigned long)
			unit_data.desc_p.start_block,
			(uint64_t)disk_cap
		);
		#endif

		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s: setting gendisk disk "
			"capacity to %d\n", __func__, (int) disk_cap);
		#endif

		/* initializing bd->queue */
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s: init bd->queue\n",
			__func__);
		#endif

		/* this will create a
		 * queue kernel thread */
		cyasblkdev_init_queue(
			&bd->queue, &bd->lock);

		bd->queue.prep_fn = cyasblkdev_blk_prep_rq;
		bd->queue.issue_fn = cyasblkdev_blk_issue_rq;
		bd->queue.data = bd;

		/*blk_size should always
		 * be a multiple of 512,
		 * set to the max to ensure
		 * that all accesses aligned
		 * to the greatest multiple,
		 * can adjust request to
		 * smaller block sizes
		 * dynamically*/

		bd->user_disk_0_read_only = !dev_data.desc_p.writeable;
		bd->user_disk_0_blk_size = dev_data.desc_p.block_size;
		bd->user_disk_0_type = dev_data.desc_p.type;
		bd->user_disk_0_bus_num = bus_num;
		bd->user_disk_0->major = major;
		bd->user_disk_0->first_minor = devidx << CYASBLKDEV_SHIFT;
		bd->user_disk_0->minors = 8;
		bd->user_disk_0->fops = &cyasblkdev_bdops;
		bd->user_disk_0->private_data = bd;
		bd->user_disk_0->queue = bd->queue.queue;
		bd->dbgprn_flags = DBGPRN_RD_RQ;
		bd->user_disk_0_unit_no = lcl_unit_no;

		blk_queue_logical_block_size(bd->queue.queue,
			bd->user_disk_0_blk_size);

		set_capacity(bd->user_disk_0,
			disk_cap);

		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
			"%s: returned from set_capacity %d\n",
			__func__, (int) disk_cap);
		#endif

		/* need to start search from
		 * public partition beginning */
		if (vfat_search) {
			bd->user_disk_0_first_sector =
				cyasblkdev_get_vfat_offset(0,
					bd->user_disk_0_unit_no);
		} else {
			bd->user_disk_0_first_sector = 0;
		}

		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
			"%s: set user_disk_0_first "
			"sector to %d\n", __func__,
			 bd->user_disk_0_first_sector);
		cy_as_hal_print_message(
			"%s: add_disk: disk->major=0x%x\n",
			__func__,
			bd->user_disk_0->major);
		cy_as_hal_print_message(
			"%s: add_disk: "
			"disk->first_minor=0x%x\n", __func__,
			bd->user_disk_0->first_minor);
		cy_as_hal_print_message(
			"%s: add_disk: "
			"disk->minors=0x%x\n", __func__,
			bd->user_disk_0->minors);
		cy_as_hal_print_message(
			"%s: add_disk: "
			"disk->disk_name=%s\n",
			__func__,
			bd->user_disk_0->disk_name);
		cy_as_hal_print_message(
			"%s: add_disk: "
			"disk->part_tbl=0x%x\n", __func__,
			(unsigned int)
			bd->user_disk_0->part_tbl);
		cy_as_hal_print_message(
			"%s: add_disk: "
			"disk->queue=0x%x\n", __func__,
			(unsigned int)
			bd->user_disk_0->queue);
		cy_as_hal_print_message(
			"%s: add_disk: "
			"disk->flags=0x%x\n",
			__func__, (unsigned int)
			bd->user_disk_0->flags);
		cy_as_hal_print_message(
			"%s: add_disk: "
			"disk->driverfs_dev=0x%x\n",
			__func__, (unsigned int)
			bd->user_disk_0->driverfs_dev);
		cy_as_hal_print_message(
			"%s: add_disk: "
			"disk->slave_dir=0x%x\n",
			__func__, (unsigned int)
			bd->user_disk_0->slave_dir);
		cy_as_hal_print_message(
			"%s: add_disk: "
			"disk->random=0x%x\n",
			__func__, (unsigned int)
			bd->user_disk_0->random);
		cy_as_hal_print_message(
			"%s: add_disk: "
			"disk->node_id=0x%x\n",
			__func__, (unsigned int)
			bd->user_disk_0->node_id);

		#endif

		add_disk(bd->user_disk_0);

	} else if ((bus_num == 1) &&
		(total_media_count == 2)) {
		bd->user_disk_1_read_only = !dev_data.desc_p.writeable;
		bd->user_disk_1_blk_size = dev_data.desc_p.block_size;
		bd->user_disk_1_type = dev_data.desc_p.type;
		bd->user_disk_1_bus_num = bus_num;
		bd->user_disk_1->major	= major;
		bd->user_disk_1->first_minor = (devidx + 1) << CYASBLKDEV_SHIFT;
		bd->user_disk_1->minors = 8;
		bd->user_disk_1->fops = &cyasblkdev_bdops;
		bd->user_disk_1->private_data = bd;
		bd->user_disk_1->queue = bd->queue.queue;
		bd->dbgprn_flags = DBGPRN_RD_RQ;
		bd->user_disk_1_unit_no = lcl_unit_no;

		sprintf(bd->user_disk_1->disk_name,
			"cyasblkdevblk%d", (devidx + 1));

		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
			"%s: disk unit_sz:%lu "
			"blk_sz:%d, "
			"start_blk:%lu, "
			"capacity:%llu\n",
			__func__,
			(unsigned long)
			unit_data.desc_p.unit_size,
			unit_data.desc_p.block_size,
			(unsigned long)
			unit_data.desc_p.start_block,
			(uint64_t)disk_cap
		);
		#endif

		/*blk_size should always be a
		 * multiple of 512, set to the max
		 * to ensure that all accesses
		 * aligned to the greatest multiple,
		 * can adjust request to smaller
		 * block sizes dynamically*/
		if (bd->user_disk_0_blk_size >
		bd->user_disk_1_blk_size) {
			blk_queue_logical_block_size(bd->queue.queue,
				bd->user_disk_0_blk_size);
			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message(
			"%s: set hard sect_sz:%d\n",
			__func__,
			bd->user_disk_0_blk_size);
			#endif
		} else {
			blk_queue_logical_block_size(bd->queue.queue,
				bd->user_disk_1_blk_size);
			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message(
			"%s: set hard sect_sz:%d\n",
			__func__,
			bd->user_disk_1_blk_size);
			#endif
		}

		set_capacity(bd->user_disk_1, disk_cap);
		if (vfat_search) {
			bd->user_disk_1_first_sector =
				cyasblkdev_get_vfat_offset(
					1, bd->user_disk_1_unit_no);
		} else {
			bd->user_disk_1_first_sector
				= 0;
		}

		add_disk(bd->user_disk_1);
	}

	if (lcl_unit_no > 0) {
		if (bd->system_disk == NULL) {
			bd->system_disk =
				alloc_disk(CYASBLKDEV_MINOR_2
					<< CYASBLKDEV_SHIFT);
			if (bd->system_disk == NULL) {
				kfree(bd);
				bd = ERR_PTR(-ENOMEM);
				return bd;
			}
			disk_cap = (uint64_t)
				(private_partition_size);

			/* set properties of
			 * system disk */
			bd->system_disk_read_only = !dev_data.desc_p.writeable;
			bd->system_disk_blk_size = dev_data.desc_p.block_size;
			bd->system_disk_bus_num = bus_num;
			bd->system_disk->major = major;
			bd->system_disk->first_minor =
				(devidx + 2) << CYASBLKDEV_SHIFT;
			bd->system_disk->minors = 8;
			bd->system_disk->fops = &cyasblkdev_bdops;
			bd->system_disk->private_data = bd;
			bd->system_disk->queue = bd->queue.queue;
			/* don't search for vfat
			 * with system disk */
			bd->system_disk_first_sector = 0;
			sprintf(
				bd->system_disk->disk_name,
				"cyasblkdevblk%d", (devidx + 2));

			set_capacity(bd->system_disk,
				disk_cap);

			add_disk(bd->system_disk);
		}
		#ifndef WESTBRIDGE_NDEBUG
		else {
			cy_as_hal_print_message(
				"%s: system disk already allocated %d\n",
				__func__, bus_num);
		}
		#endif
	}
out:
	return ret;
}

static struct cyasblkdev_blk_data *cyasblkdev_blk_alloc(void)
{
	struct cyasblkdev_blk_data *bd;
	int ret = 0;
	cy_as_return_status_t stat = -1;
	int bus_num = 0;
	int total_media_count = 0;
	int devidx = 0;
	DBGPRN_FUNC_NAME;

	total_media_count = 0;
	devidx = find_first_zero_bit(dev_use, CYASBLKDEV_NUM_MINORS);
	if (devidx >= CYASBLKDEV_NUM_MINORS)
		return ERR_PTR(-ENOSPC);

	__set_bit(devidx, dev_use);
	__set_bit(devidx + 1, dev_use);

	bd = kzalloc(sizeof(struct cyasblkdev_blk_data), GFP_KERNEL);
	if (bd) {
		gl_bd = bd;

		spin_lock_init(&bd->lock);
		bd->usage = 1;

		/* setup the block_dev_ops pointer*/
		bd->blkops = &cyasblkdev_bdops;

		/* Get the device handle */
		bd->dev_handle = cyasdevice_getdevhandle();
		if (0 == bd->dev_handle) {
			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message(
				"%s: get device failed\n", __func__);
			#endif
			ret = ENODEV;
			goto out;
		}

		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s west bridge device handle:%x\n",
			__func__, (uint32_t)bd->dev_handle);
		#endif

		/* start the storage api and get a handle to the
		 * device we are interested in. */

		/* Error code to use if the conditions are not satisfied. */
		ret = ENOMEDIUM;

		stat = cy_as_misc_release_resource(bd->dev_handle, cy_as_bus_0);
		if ((stat != CY_AS_ERROR_SUCCESS) &&
		(stat != CY_AS_ERROR_RESOURCE_NOT_OWNED)) {
			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message("%s: cannot release "
				"resource bus 0 - reason code %d\n",
				__func__, stat);
			#endif
		}

		stat = cy_as_misc_release_resource(bd->dev_handle, cy_as_bus_1);
		if ((stat != CY_AS_ERROR_SUCCESS) &&
		(stat != CY_AS_ERROR_RESOURCE_NOT_OWNED)) {
			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message("%s: cannot release "
				"resource bus 0 - reason code %d\n",
				__func__, stat);
			#endif
		}

		/* start storage stack*/
		stat = cy_as_storage_start(bd->dev_handle, 0, 0x101);
		if (stat != CY_AS_ERROR_SUCCESS) {
			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message("%s: cannot start storage "
				"stack - reason code %d\n", __func__, stat);
			#endif
			goto out;
		}

		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s: storage started:%d ok\n",
			__func__, stat);
		#endif

		stat = cy_as_storage_register_callback(bd->dev_handle,
			cyasblkdev_storage_callback);
		if (stat != CY_AS_ERROR_SUCCESS) {
			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message("%s: cannot register callback "
				"- reason code %d\n", __func__, stat);
			#endif
			goto out;
		}

		for (bus_num = 0; bus_num < 2; bus_num++) {
			stat = cy_as_storage_query_bus(bd->dev_handle,
				bus_num, &bd->media_count[bus_num],  0, 0);
			if (stat == CY_AS_ERROR_SUCCESS) {
				total_media_count = total_media_count +
					bd->media_count[bus_num];
			} else {
				#ifndef WESTBRIDGE_NDEBUG
				cy_as_hal_print_message("%s: cannot query %d, "
					"reason code: %d\n",
					__func__, bus_num, stat);
				#endif
				goto out;
			}
		}

		if (total_media_count == 0) {
			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message(
				"%s: no storage media was found\n", __func__);
			#endif
			goto out;
		} else if (total_media_count >= 1) {
			if (bd->user_disk_0 == NULL) {

				bd->user_disk_0 =
					alloc_disk(CYASBLKDEV_MINOR_0
						<< CYASBLKDEV_SHIFT);
				if (bd->user_disk_0 == NULL) {
					kfree(bd);
					bd = ERR_PTR(-ENOMEM);
					return bd;
				}
			}
			#ifndef WESTBRIDGE_NDEBUG
			else {
				cy_as_hal_print_message("%s: no available "
					"gen_disk for disk 0, "
					"physically inconsistent\n", __func__);
			}
			#endif
		}

		if (total_media_count == 2) {
			if (bd->user_disk_1 == NULL) {
				bd->user_disk_1 =
					alloc_disk(CYASBLKDEV_MINOR_1
						<< CYASBLKDEV_SHIFT);
				if (bd->user_disk_1 == NULL) {
					kfree(bd);
					bd = ERR_PTR(-ENOMEM);
					return bd;
				}
			}
			#ifndef WESTBRIDGE_NDEBUG
			else {
				cy_as_hal_print_message("%s: no available "
					"gen_disk for media, "
					"physically inconsistent\n", __func__);
			}
			#endif
		}
		#ifndef WESTBRIDGE_NDEBUG
		else if (total_media_count > 2) {
			cy_as_hal_print_message("%s: count corrupted = 0x%d\n",
				__func__, total_media_count);
		}
		#endif

		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s: %d device(s) found\n",
			__func__, total_media_count);
		#endif

		for (bus_num = 0; bus_num <= 1; bus_num++) {
			/*claim storage for cpu */
			stat = cy_as_storage_claim(bd->dev_handle,
				bus_num, 0, 0, 0);
			if (stat != CY_AS_ERROR_SUCCESS) {
				cy_as_hal_print_message("%s: cannot claim "
					"%d bus - reason code %d\n",
					__func__, bus_num, stat);
				goto out;
			}

			dev_data.bus = bus_num;
			dev_data.device = 0;

			stat = cy_as_storage_query_device(bd->dev_handle,
				&dev_data, 0, 0);
			if (stat == CY_AS_ERROR_SUCCESS) {
				cyasblkdev_add_disks(bus_num, bd,
					total_media_count, devidx);
			} else if (stat == CY_AS_ERROR_NO_SUCH_DEVICE) {
				#ifndef WESTBRIDGE_NDEBUG
				cy_as_hal_print_message(
					"%s: no device on bus %d\n",
					__func__, bus_num);
				#endif
			} else {
				#ifndef WESTBRIDGE_NDEBUG
				cy_as_hal_print_message(
					"%s: cannot query %d device "
					"- reason code %d\n",
					__func__, bus_num, stat);
				#endif
				goto out;
			}
		} /* end for (bus_num = 0; bus_num <= 1; bus_num++)*/

		return bd;
	}
out:
	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message(
		"%s: bd failed to initialize\n", __func__);
	#endif

	kfree(bd);
	bd = ERR_PTR(-ret);
	return bd;
}


/*init west bridge block device */
static int cyasblkdev_blk_initialize(void)
{
	struct cyasblkdev_blk_data *bd;
	int res;

	DBGPRN_FUNC_NAME;

	res = register_blkdev(major, "cyasblkdev");

	if (res < 0) {
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(KERN_WARNING
			"%s unable to get major %d for cyasblkdev media: %d\n",
			__func__, major, res);
		#endif
		return res;
	}

	if (major == 0)
		major = res;

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message(
		"%s cyasblkdev registered with major number: %d\n",
		__func__, major);
	#endif

	bd = cyasblkdev_blk_alloc();
	if (IS_ERR(bd))
		return PTR_ERR(bd);

	return 0;
}

/* start block device */
static int __init cyasblkdev_blk_init(void)
{
	int res = -ENOMEM;

	DBGPRN_FUNC_NAME;

	/* get the cyasdev handle for future use*/
	cyas_dev_handle = cyasdevice_getdevhandle();

	if  (cyasblkdev_blk_initialize() == 0)
		return 0;

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("cyasblkdev init error:%d\n", res);
	#endif
	return res;
}


static void cyasblkdev_blk_deinit(struct cyasblkdev_blk_data *bd)
{
	DBGPRN_FUNC_NAME;

	if (bd) {
		int devidx;

		if (bd->user_disk_0 != NULL) {
			del_gendisk(bd->user_disk_0);
			devidx = bd->user_disk_0->first_minor
				>> CYASBLKDEV_SHIFT;
			__clear_bit(devidx, dev_use);
		}

		if (bd->user_disk_1 != NULL) {
			del_gendisk(bd->user_disk_1);
			devidx = bd->user_disk_1->first_minor
				>> CYASBLKDEV_SHIFT;
			__clear_bit(devidx, dev_use);
		}

		if (bd->system_disk != NULL) {
			del_gendisk(bd->system_disk);
			devidx = bd->system_disk->first_minor
				>> CYASBLKDEV_SHIFT;
			__clear_bit(devidx, dev_use);
		}

		cyasblkdev_blk_put(bd);
	}
}

/* block device exit */
static void __exit cyasblkdev_blk_exit(void)
{
	DBGPRN_FUNC_NAME;

	cyasblkdev_blk_deinit(gl_bd);
	unregister_blkdev(major, "cyasblkdev");

}

module_init(cyasblkdev_blk_init);
module_exit(cyasblkdev_blk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("antioch (cyasblkdev) block device driver");
MODULE_AUTHOR("cypress semiconductor");

/*[]*/
