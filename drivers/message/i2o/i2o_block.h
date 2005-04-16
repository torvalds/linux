/*
 *	Block OSM structures/API
 *
 * 	Copyright (C) 1999-2002	Red Hat Software
 *
 *	Written by Alan Cox, Building Number Three Ltd
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation; either version 2 of the License, or (at your
 *	option) any later version.
 *
 *	This program is distributed in the hope that it will be useful, but
 *	WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *	General Public License for more details.
 *
 *	For the purpose of avoiding doubt the preferred form of the work
 *	for making modifications shall be a standards compliant form such
 *	gzipped tar and not one requiring a proprietary or patent encumbered
 *	tool to unpack.
 *
 *	Fixes/additions:
 *		Steve Ralston:
 *			Multiple device handling error fixes,
 *			Added a queue depth.
 *		Alan Cox:
 *			FC920 has an rmw bug. Dont or in the end marker.
 *			Removed queue walk, fixed for 64bitness.
 *			Rewrote much of the code over time
 *			Added indirect block lists
 *			Handle 64K limits on many controllers
 *			Don't use indirects on the Promise (breaks)
 *			Heavily chop down the queue depths
 *		Deepak Saxena:
 *			Independent queues per IOP
 *			Support for dynamic device creation/deletion
 *			Code cleanup
 *	    		Support for larger I/Os through merge* functions
 *			(taken from DAC960 driver)
 *		Boji T Kannanthanam:
 *			Set the I2O Block devices to be detected in increasing
 *			order of TIDs during boot.
 *			Search and set the I2O block device that we boot off
 *			from as the first device to be claimed (as /dev/i2o/hda)
 *			Properly attach/detach I2O gendisk structure from the
 *			system gendisk list. The I2O block devices now appear in
 *			/proc/partitions.
 *		Markus Lidel <Markus.Lidel@shadowconnect.com>:
 *			Minor bugfixes for 2.6.
 */

#ifndef I2O_BLOCK_OSM_H
#define I2O_BLOCK_OSM_H

#define I2O_BLOCK_RETRY_TIME HZ/4
#define I2O_BLOCK_MAX_OPEN_REQUESTS 50

/* I2O Block OSM mempool struct */
struct i2o_block_mempool {
	kmem_cache_t	*slab;
	mempool_t	*pool;
};

/* I2O Block device descriptor */
struct i2o_block_device {
	struct i2o_device *i2o_dev;	/* pointer to I2O device */
	struct gendisk *gd;
	spinlock_t lock;		/* queue lock */
	struct list_head open_queue;	/* list of transfered, but unfinished
					   requests */
	unsigned int open_queue_depth;	/* number of requests in the queue */

	int rcache;			/* read cache flags */
	int wcache;			/* write cache flags */
	int flags;
	int power;			/* power state */
	int media_change_flag;		/* media changed flag */
};

/* I2O Block device request */
struct i2o_block_request
{
	struct list_head queue;
	struct request *req;		/* corresponding request */
	struct i2o_block_device *i2o_blk_dev;	/* I2O block device */
	int sg_dma_direction;		/* direction of DMA buffer read/write */
	int sg_nents;			/* number of SG elements */
	struct scatterlist sg_table[I2O_MAX_SEGMENTS]; /* SG table */
};

/* I2O Block device delayed request */
struct i2o_block_delayed_request
{
	struct work_struct work;
	struct request_queue *queue;
};

#endif
