/*
 *    Disk Array driver for Compaq SMART2 Controllers
 *    Copyright 1998 Compaq Computer Corporation
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *    NON INFRINGEMENT.  See the GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *    Questions/Comments/Bugfixes to iss_storagedev@hp.com
 *
 *    If you want to make changes, improve or add functionality to this
 *    driver, you'll probably need the Compaq Array Controller Interface
 *    Specificiation (Document number ECG086/1198)
 */
#ifndef CPQARRAY_H
#define CPQARRAY_H

#ifdef __KERNEL__
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/timer.h>
#endif

#include "ida_cmd.h"

#define IO_OK		0
#define IO_ERROR	1
#define NWD		16
#define NWD_SHIFT	4

#define IDA_TIMER	(5*HZ)
#define IDA_TIMEOUT	(10*HZ)

#define MISC_NONFATAL_WARN	0x01

typedef struct {
	unsigned blk_size;
	unsigned nr_blks;
	unsigned cylinders;
	unsigned heads;
	unsigned sectors;
	int usage_count;
} drv_info_t;

#ifdef __KERNEL__

struct ctlr_info;
typedef struct ctlr_info ctlr_info_t;

struct access_method {
	void (*submit_command)(ctlr_info_t *h, cmdlist_t *c);
	void (*set_intr_mask)(ctlr_info_t *h, unsigned long val);
	unsigned long (*fifo_full)(ctlr_info_t *h);
	unsigned long (*intr_pending)(ctlr_info_t *h);
	unsigned long (*command_completed)(ctlr_info_t *h);
};

struct board_type {
	__u32	board_id;
	char	*product_name;
	struct access_method *access;
};

struct ctlr_info {
	int	ctlr;
	char	devname[8];
	__u32	log_drv_map;
	__u32	drv_assign_map;
	__u32	drv_spare_map;
	__u32	mp_failed_drv_map;

	char	firm_rev[4];
	int	ctlr_sig;

	int	log_drives;
	int	phys_drives;

	struct pci_dev *pci_dev;    /* NULL if EISA */
	__u32	board_id;
	char	*product_name;	

	void __iomem *vaddr;
	unsigned long paddr;
	unsigned long io_mem_addr;
	unsigned long io_mem_length;
	int	intr;
	int	usage_count;
	drv_info_t	drv[NWD];
	struct proc_dir_entry *proc;

	struct access_method access;

	cmdlist_t *reqQ;
	cmdlist_t *cmpQ;
	cmdlist_t *cmd_pool;
	dma_addr_t cmd_pool_dhandle;
	unsigned long *cmd_pool_bits;
	struct request_queue *queue;
	spinlock_t lock;

	unsigned int Qdepth;
	unsigned int maxQsinceinit;

	unsigned int nr_requests;
	unsigned int nr_allocs;
	unsigned int nr_frees;
	struct timer_list timer;
	unsigned int misc_tflags;
};

#define IDA_LOCK(i)	(&hba[i]->lock)

#endif

#endif /* CPQARRAY_H */
