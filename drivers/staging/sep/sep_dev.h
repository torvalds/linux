#ifndef __SEP_DEV_H__
#define __SEP_DEV_H__

/*
 *
 *  sep_dev.h - Security Processor Device Structures
 *
 *  Copyright(c) 2009,2010 Intel Corporation. All rights reserved.
 *  Contributions(c) 2009,2010 Discretix. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *  CONTACTS:
 *
 *  Mark Allyn		mark.a.allyn@intel.com
 *  Jayant Mangalampalli jayant.mangalampalli@intel.com
 *
 *  CHANGES
 *  2010.09.14  upgrade to Medfield
 */

struct sep_device {
	/* pointer to pci dev */
	struct pci_dev *pdev;

	/* character device file */
	struct cdev sep_cdev;
	struct cdev sep_daemon_cdev;
	struct cdev sep_singleton_cdev;

	/* devices (using misc dev) */
	struct miscdevice miscdev_sep;
	struct miscdevice miscdev_singleton;
	struct miscdevice miscdev_daemon;

	/* major / minor numbers of device */
	dev_t sep_devno;
	dev_t sep_daemon_devno;
	dev_t sep_singleton_devno;

	struct mutex sep_mutex;
	struct mutex ioctl_mutex;
	spinlock_t snd_rply_lck;

	/* flags to indicate use and lock status of sep */
	u32 pid_doing_transaction;
	unsigned long in_use_flags;

	/* request daemon alread open */
	unsigned long request_daemon_open;

	/* 1 = Moorestown; 0 = Medfield */
	int mrst;

	/* address of the shared memory allocated during init for SEP driver
	   (coherent alloc) */
	dma_addr_t shared_bus;
	size_t shared_size;
	void *shared_addr;

	/* start address of the access to the SEP registers from driver */
	dma_addr_t reg_physical_addr;
	dma_addr_t reg_physical_end;
	void __iomem *reg_addr;

	/* wait queue head (event) of the driver */
	wait_queue_head_t event;
	wait_queue_head_t event_request_daemon;
	wait_queue_head_t event_mmap;

	struct sep_caller_id_entry
		caller_id_table[SEP_CALLER_ID_TABLE_NUM_ENTRIES];

	/* access flag for singleton device */
	unsigned long singleton_access_flag;

	/* transaction counter that coordinates the
	   transactions between SEP and HOST */
	unsigned long send_ct;
	/* counter for the messages from sep */
	unsigned long reply_ct;
	/* counter for the number of bytes allocated in the pool for the
	   current transaction */
	long data_pool_bytes_allocated;

	u32 num_of_data_allocations;

	/* number of the lli tables created in the current transaction */
	u32     num_lli_tables_created;

	/* number of data control blocks */
	u32 nr_dcb_creat;

	struct sep_dma_resource dma_res_arr[SEP_MAX_NUM_SYNC_DMA_OPS];

};

static inline void sep_write_reg(struct sep_device *dev, int reg, u32 value)
{
	void __iomem *addr = dev->reg_addr + reg;
	writel(value, addr);
}

static inline u32 sep_read_reg(struct sep_device *dev, int reg)
{
	void __iomem *addr = dev->reg_addr + reg;
	return readl(addr);
}

/* wait for SRAM write complete(indirect write */
static inline void sep_wait_sram_write(struct sep_device *dev)
{
	u32 reg_val;
	do {
		reg_val = sep_read_reg(dev, HW_SRAM_DATA_READY_REG_ADDR);
	} while (!(reg_val & 1));
}


#endif
