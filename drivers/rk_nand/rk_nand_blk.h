/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __RKNAND_BLK_H
#define __RKNAND_BLK_H

#include <linux/semaphore.h>

#define MAX_PART_COUNT 32

struct nand_part {
	unsigned char name[32];
	unsigned long offset;
	unsigned long size;
	unsigned char type;
};

struct nand_blk_dev {
	struct nand_blk_ops *nandr;
	struct list_head list;
	int devnum;
	unsigned long size;
	unsigned long off_size;
	int readonly;
	int writeonly;
	int disable_access;
	void *blkcore_priv;
};

struct nand_blk_ops {
	char *name;
	int major;
	int minorbits;
	int last_dev_index;
	struct completion thread_exit;
	int quit;
	int nand_th_quited;
	wait_queue_head_t thread_wq; /* thread wait queue */
	struct request_queue *rq;
	spinlock_t queue_lock; /* queue lock */
	struct list_head devs;
	struct module *owner;
};

extern struct device *g_nand_device;
void rknand_dev_suspend(void);
void rknand_dev_resume(void);
void rknand_dev_shutdown(void);
void rknand_dev_flush(void);
int __init rknand_dev_init(void);
int rknand_dev_exit(void);
void rknand_device_lock(void);
int rknand_device_trylock(void);
void rknand_device_unlock(void);
int nand_blk_add_whole_disk(void);
#endif
