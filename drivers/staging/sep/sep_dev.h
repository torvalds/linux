#ifndef __SEP_DEV_H__
#define __SEP_DEV_H__

/*
 *
 *  sep_dev.h - Security Processor Device Structures
 *
 *  Copyright(c) 2009 Intel Corporation. All rights reserved.
 *  Copyright(c) 2009 Discretix. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
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
 *  Alan Cox		alan@linux.intel.com
 *
 */

struct sep_device {
	/* pointer to pci dev */
	struct pci_dev *sep_pci_dev_ptr;

	unsigned long io_memory_start_physical_address;
	unsigned long io_memory_end_physical_address;
	unsigned long io_memory_size;
	void *io_memory_start_virtual_address;

	/* restricted access region */
	unsigned long rar_physical_address;
	void *rar_virtual_address;

	/* shared memory region */
	unsigned long shared_physical_address;
	void *shared_virtual_address;

	/* firmware regions */
	unsigned long cache_physical_address;
	unsigned long cache_size;
	void *cache_virtual_address;

	unsigned long resident_physical_address;
	unsigned long resident_size;
	void *resident_virtual_address;

	/* device interrupt (as retrieved from PCI) */
	int sep_irq;

	unsigned long rar_region_addr;

	/* start address of the access to the SEP registers from driver */
	void __iomem *reg_base_address;
	/* transaction counter that coordinates the transactions between SEP and HOST */
	unsigned long host_to_sep_send_counter;
	/* counter for the messages from sep */
	unsigned long sep_to_host_reply_counter;
	/* counter for the number of bytes allocated in the pool for the current
	   transaction */
	unsigned long data_pool_bytes_allocated;

	/* array of pointers to the pages that represent input data for the synchronic
	   DMA action */
	struct page **in_page_array;

	/* array of pointers to the pages that represent out data for the synchronic
	   DMA action */
	struct page **out_page_array;

	/* number of pages in the sep_in_page_array */
	unsigned long in_num_pages;

	/* number of pages in the sep_out_page_array */
	unsigned long out_num_pages;

	/* global data for every flow */
	struct sep_flow_context_t flows_data_array[SEP_DRIVER_NUM_FLOWS];

	/* flag for API mode - 1 -is blocking, 0 is non-blocking */
	unsigned long block_mode_flag;

	/* pointer to the workqueue that handles the flow done interrupts */
	struct workqueue_struct *flow_wq_ptr;

	/* address of the shared memory allocated during init for SEP driver */
	unsigned long shared_area_addr;
	/* the physical address of the shared area */
	unsigned long phys_shared_area_addr;

	/* Message Shared Area start address - will be allocated during init */
	unsigned long message_shared_area_addr;
};

static struct sep_device *sep_dev;

static inline void sep_write_reg(struct sep_device *dev, int reg, u32 value)
{
	void __iomem *addr = dev->reg_base_address + reg;
	writel(value, addr);
}

static inline u32 sep_read_reg(struct sep_device *dev, int reg)
{
	void __iomem *addr = dev->reg_base_address + reg;
	return readl(addr);
}

/* wait for SRAM write complete(indirect write */
static inline void sep_wait_sram_write(struct sep_device *dev)
{
	u32 reg_val;
	do
		reg_val = sep_read_reg(dev, HW_SRAM_DATA_READY_REG_ADDR);
	while (!(reg_val & 1));
}


#endif
