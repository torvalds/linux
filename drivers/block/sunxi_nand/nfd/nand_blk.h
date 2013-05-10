/*
 * drivers/block/sunxi_nand/nfd/nand_blk.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/semaphore.h>
#include <plat/mbr.h>

struct nand_blk_ops;
struct list_head;
struct semaphore;
struct hd_geometry;

struct nand_blk_dev{
	struct nand_blk_ops *nandr;

	unsigned char heads;
	unsigned char sectors;
	unsigned short cylinders;
	
	int devnum;
	unsigned long size;
	unsigned long off_size;
	int readonly;
	int writeonly;
	int disable_access;
	void *blkcore_priv; 	
};
struct nand_blk_ops{
	/* blk device ID */
	char *name;
	int major;
	int minorbits;	
	
	/* add/remove nandflash devparts,use gendisk */
	int (*add_dev)(struct nand_blk_ops *nandr);
	int (*remove_dev)(struct nand_blk_dev *dev);

	/* Block layer ioctls */
	int (*getgeo)(struct nand_blk_dev *dev, struct hd_geometry *geo);
	int (*flush)(struct nand_blk_dev *dev);

	/* Called with mtd_table_mutex held; no race with add/remove */
	int (*open)(struct nand_blk_dev *dev);
	int (*release)(struct nand_blk_dev *dev);	
	
	/* synchronization variable */
	struct completion thread_exit;
	int quit;
	wait_queue_head_t thread_wq;
	struct request_queue *rq;
	spinlock_t queue_lock;	
	struct semaphore nand_ops_mutex;
	
	struct nand_blk_dev dev;
	struct module *owner;	
};

