// ----------------------------------------------------------------------
// Copyright (c) 2016, The Regents of the University of California All
// rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// 
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
// 
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
// 
//     * Neither the name of The Regents of the University of California
//       nor the names of its contributors may be used to endorse or
//       promote products derived from this software without specific
//       prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL REGENTS OF THE
// UNIVERSITY OF CALIFORNIA BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
// OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
// TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
// USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
// DAMAGE.
// ----------------------------------------------------------------------

/*
 * Filename: riffa_driver.c
 * Version: 2.0
 * Description: Linux PCIe device driver for RIFFA. Uses Linux kernel APIs in
 *  version 2.6.27+ (tested on version 2.6.32 - 3.3.0).
 * Author: Matthew Jacobsen
 * History: @mattj: Initial release. Version 2.0.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,11,0)
#include <linux/sched.h>
#else
#include <linux/sched/signal.h>
#endif

#include <linux/rwsem.h>
#include <linux/dma-mapping.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <asm/div64.h>
#include "riffa_driver.h"
#include "circ_queue.h"

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("PCIe driver for RIFFA, Linux (2.6.27+)");
MODULE_AUTHOR("Matt Jacobsen, Patrick Lai");

#ifndef __devinit
#define __devinit
#define __devexit
#define __devexit_p
#endif

#define CHNL_REG(c, o) ((c<<4) + o)
#if !defined(__LP64__) && !defined(_LP64)
#define BUILD_32 1
#endif

#define CHNL_FLAG_BUSY 0

struct sg_mapping {
	struct page ** pages;
	struct scatterlist * sgl;
	enum dma_data_direction direction;
	int num_sg;
	unsigned long num_pages;
	unsigned long long length;
	unsigned long long overflow;
};

struct chnl_dir {
	volatile unsigned long flags;
	wait_queue_head_t waitq;
	struct circ_queue * msgs;
	void * buf_addr;
	dma_addr_t buf_hw_addr;
	struct sg_mapping * sg_map_0;
	struct sg_mapping * sg_map_1;
};

struct fpga_state {
	struct pci_dev * dev;
	unsigned long long irq;
	void __iomem *bar0;
	unsigned long long bar0_addr;
	unsigned long long bar0_len;
	unsigned long long bar0_flags;  
	atomic_t intr_disabled;
	void * spill_buf_addr;
	dma_addr_t spill_buf_hw_addr;
	int num_sg;
	int sg_buf_size;
	int id;
	char name[16];
	int vendor_id;
	int device_id;
	int num_chnls;
	struct chnl_dir ** recv;
	struct chnl_dir ** send;
};

// Global variables (to this file only)
static struct class * mymodule_class;
static dev_t devt;
static atomic_t used_fpgas[NUM_FPGAS];
static struct fpga_state * fpgas[NUM_FPGAS];

static unsigned int tx_len;
static bool recv_sg_buf_populated;

///////////////////////////////////////////////////////
// MEMORY ALLOCATION & HELPER FUNCTIONS
///////////////////////////////////////////////////////

/** 
 * Returns the value at the specified address.
 */
static inline unsigned int read_reg(struct fpga_state * sc, int offset)
{
	return readl(sc->bar0 + (offset<<2));
}

/** 
 * Writes the value to the specified address.
 */
static inline void write_reg(struct fpga_state * sc, int offset, unsigned int val)
{
	writel(val, sc->bar0 + (offset<<2));
}

#ifdef BUILD_32
/**
 * Needed for 32 bit OS because dma_map_sg macro eventually does some 64 bit
 * division.
 */
unsigned long long __udivdi3(unsigned long long num, unsigned long long den)
{
	do_div(num, den);
	return num;
}
#endif


// These are not defined in the 2.x.y kernels, so just define them
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,39)
#define PCI_EXP_DEVCTL2_IDO_REQ_EN 0x100
#define PCI_EXP_DEVCTL2_IDO_CMP_EN 0x200
#else
/** 
 * These are badly named in pre-3.6.11 kernel versions.  We COULD do the same
 * check as above, however (annoyingly) linux for tegra (based on post-3.6.11)
 * picked up the header file from some pre-3.6.11 version, so we'll just make
 * our code ugly and handle the check here:
 */ 
#ifndef PCI_EXP_DEVCTL2_IDO_REQ_EN
#define PCI_EXP_DEVCTL2_IDO_REQ_EN PCI_EXP_IDO_REQ_EN
#endif
#ifndef PCI_EXP_DEVCTL2_IDO_CMP_EN
#define PCI_EXP_DEVCTL2_IDO_CMP_EN PCI_EXP_IDO_CMP_EN
#endif
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(3,6,11)
/**
 * Code used to set ETB and RCB, but not available before 3.0, or incorrectly
 * defined before 3.7. As it is peppered throughout the clean up code, it's just
 * easier to copy the declarations (not verbatim) here than a bunch of conditionals
 * everywhere else.
 */

int pcie_capability_read_word(struct pci_dev *dev, int pos, u16 *val)
{
	int ret;

	*val = 0;
	if (pos & 1)
		return -EINVAL;

	ret = pci_read_config_word(dev, pci_pcie_cap(dev) + pos, val);
	/*
	 * Reset *val to 0 if pci_read_config_word() fails, it may
	 * have been written as 0xFFFF if hardware error happens
	 * during pci_read_config_word().
	 */
	if (ret)
		*val = 0;
	return ret;
}

int pcie_capability_read_dword(struct pci_dev *dev, int pos, u32 *val)
{
	int ret;

	*val = 0;
	if (pos & 3)
		return -EINVAL;

	ret = pci_read_config_dword(dev, pci_pcie_cap(dev) + pos, val);
	/*
	 * Reset *val to 0 if pci_read_config_dword() fails, it may
	 * have been written as 0xFFFFFFFF if hardware error happens
	 * during pci_read_config_dword().
	 */
	if (ret)
		*val = 0;
	return ret;

}

int pcie_capability_write_word(struct pci_dev *dev, int pos, u16 val)
{
	if (pos & 1)
		return -EINVAL;

	return pci_write_config_word(dev, pci_pcie_cap(dev) + pos, val);
}

int pcie_capability_write_dword(struct pci_dev *dev, int pos, u32 val)
{
	if (pos & 3)
		return -EINVAL;

	return pci_write_config_dword(dev, pci_pcie_cap(dev) + pos, val);
}
#endif




///////////////////////////////////////////////////////
// INTERRUPT HANDLER
///////////////////////////////////////////////////////

/**
 * Reads the interrupt vector and processes it. If processing VECT0, off will
 * be 0. If processing VECT1, off will be 6.
 */
static inline void process_intr_vector(struct fpga_state * sc, int off, 
				unsigned int vect)
{
	// VECT_0/VECT_1 are organized from right to left (LSB to MSB) as:
	// [ 0] TX_TXN			for channel 0 in VECT_0, channel 6 in VECT_1
	// [ 1] TX_SG_BUF_RECVD	for channel 0 in VECT_0, channel 6 in VECT_1
	// [ 2] TX_TXN_DONE		for channel 0 in VECT_0, channel 6 in VECT_1
	// [ 3] RX_SG_BUF_RECVD	for channel 0 in VECT_0, channel 6 in VECT_1
	// [ 4] RX_TXN_DONE		for channel 0 in VECT_0, channel 6 in VECT_1
	// ...
	// [25] TX_TXN			for channel 5 in VECT_0, channel 11 in VECT_1
	// [26] TX_SG_BUF_RECVD	for channel 5 in VECT_0, channel 11 in VECT_1
	// [27] TX_TXN_DONE		for channel 5 in VECT_0, channel 11 in VECT_1
	// [28] RX_SG_BUF_RECVD	for channel 5 in VECT_0, channel 11 in VECT_1
	// [29] RX_TXN_DONE		for channel 5 in VECT_0, channel 11 in VECT_1
	// Positions 30 - 31 in both VECT_0 and VECT_1 are zero.

	unsigned int offlast;
	unsigned int len;
	int recv;
	int send;
	int chnl;
	int i;

//printk(KERN_INFO "riffa: intrpt_handler received:%08x\n", vect);
	if (vect & 0xC0000000) {
		printk(KERN_ERR "riffa: fpga:%d, received bad interrupt vector:%08x\n", sc->id, vect);
		return;
	}

	for (i = 0; i < 6 && (i+off) < sc->num_chnls; ++i) {
		chnl = i + off;
		recv = 0; 
		send = 0; 

		// TX (PC receive) scatter gather buffer is read.
		if (vect & (1<<((5*i)+1))) { 
			recv = 1; 
			// Keep track so the thread can handle this.
			if (push_circ_queue(sc->recv[chnl]->msgs, EVENT_SG_BUF_READ, 0)) {
				printk(KERN_ERR "riffa: fpga:%d chnl:%d, recv sg buf read msg queue full\n", sc->id, chnl);
			}
			DEBUG_MSG(KERN_INFO "riffa: fpga:%d chnl:%d, recv sg buf read\n", sc->id, chnl);
		}

		// TX (PC receive) transaction done.
		if (vect & (1<<((5*i)+2))) { 
			recv = 1; 
			// Read the transferred amount.
			len = read_reg(sc, CHNL_REG(chnl, TX_TNFR_LEN_REG_OFF));
			// Notify the thread.
			if (push_circ_queue(sc->recv[chnl]->msgs, EVENT_TXN_DONE, len)) {
				printk(KERN_ERR "riffa: fpga:%d chnl:%d, recv txn done msg queue full\n", sc->id, chnl);
			}
			DEBUG_MSG(KERN_INFO "riffa: fpga:%d chnl:%d, recv txn done\n", sc->id, chnl);
		}

		// New TX (PC receive) transaction.
		if (vect & (1<<((5*i)+0))) { 
			recv = 1; 
			recv_sg_buf_populated = 0; // resets for new transaction

			// Read the offset/last and length
			offlast = read_reg(sc, CHNL_REG(chnl, TX_OFFLAST_REG_OFF));
			tx_len = read_reg(sc, CHNL_REG(chnl, TX_LEN_REG_OFF));
			// Keep track of this transaction
			if (push_circ_queue(sc->recv[chnl]->msgs, EVENT_TXN_OFFLAST, offlast)) {
				printk(KERN_ERR "riffa: fpga:%d chnl:%d, recv txn offlast msg queue full\n", sc->id, chnl);
			}
			/*if (push_circ_queue(sc->recv[chnl]->msgs, EVENT_TXN_LEN, len)) {
				printk(KERN_ERR "riffa: fpga:%d chnl:%d, recv txn len msg queue full\n", sc->id, chnl);
			}*/
			DEBUG_MSG(KERN_INFO "riffa: fpga:%d chnl:%d, recv txn (len:%d off:%d last:%d)\n", sc->id, chnl, tx_len, (offlast>>1), (offlast & 0x1));
		}

		// RX (PC send) scatter gather buffer is read.
		if (vect & (1<<((5*i)+3))) { 
			send = 1; 
			// Keep track so the thread can handle this.
			if (push_circ_queue(sc->send[chnl]->msgs, EVENT_SG_BUF_READ, 0)) {
				printk(KERN_ERR "riffa: fpga:%d chnl:%d, send sg buf read msg queue full\n", sc->id, chnl);
			}
			DEBUG_MSG(KERN_INFO "riffa: fpga:%d chnl:%d, send sg buf read\n", sc->id, chnl);
		}

		// RX (PC send) transaction done.
		if (vect & (1<<((5*i)+4))) {
			send = 1; 
			// Read the transferred amount.
			len = read_reg(sc, CHNL_REG(chnl, RX_TNFR_LEN_REG_OFF));
			// Notify the thread.
			if (push_circ_queue(sc->send[chnl]->msgs, EVENT_TXN_DONE, len)) {
				printk(KERN_ERR "riffa: fpga:%d chnl:%d, send txn done msg queue full\n", sc->id, chnl);
			}
			DEBUG_MSG(KERN_INFO "riffa: fpga:%d chnl:%d, send txn done\n", sc->id, chnl);
		}

		// Wake up the thread?
		if (recv)
			wake_up(&sc->recv[chnl]->waitq);
		if (send)
			wake_up(&sc->send[chnl]->waitq);
	}
}

/**
 * Interrupt handler for all interrupts on all files. Reads data/values
 * from FPGA and wakes up waiting threads to process the data. Always returns
 * IRQ_HANDLED.
 */
static irqreturn_t intrpt_handler(int irq, void *dev_id) 
{
	unsigned int vect0;
	unsigned int vect1;
	struct fpga_state * sc;

	sc = (struct fpga_state *)dev_id;
	vect0 = 0;
	vect1 = 0;

	if (sc == NULL) {
		printk(KERN_ERR "riffa: invalid fpga_state pointer\n");
		return IRQ_HANDLED;
	}

	if (!atomic_read(&sc->intr_disabled)) {
		// Read the interrupt vector(s):
		vect0 = read_reg(sc, IRQ_REG0_OFF);
		if (sc->num_chnls > 6)
			vect1 = read_reg(sc, IRQ_REG1_OFF);

		// Process the vector(s)
		process_intr_vector(sc, 0, vect0);
		if (sc->num_chnls > 6)
			process_intr_vector(sc, 6, vect1);
	}

	return IRQ_HANDLED;
}


///////////////////////////////////////////////////////
// FILE OPERATION HANDLERS
///////////////////////////////////////////////////////

/**
 * Creates and returns a struct_sg_mapping that holds all the data for the user
 * pages that have been mapped into a scatterlist array. Assumes the user data
 * is 32 bit word aligned. Up to length bytes from the udata pointer will be
 * mapped. After all length bytes are mapped, up to overflow bytes will be
 * mapped using the common spill buffer for the channel. The overflow is used
 * if we run out of space in the supplied udata pointer.
 */
static inline struct sg_mapping * fill_sg_buf(struct fpga_state * sc, int chnl, 
					void * sg_buf, unsigned long udata, unsigned long long length, 
					unsigned long long overflow, enum dma_data_direction direction) {
	const char * dir = (direction == DMA_TO_DEVICE ? "send" : "recv");
	struct sg_mapping * sg_map;
	struct page ** pages = NULL;
	struct scatterlist * sgl = NULL;
	struct scatterlist * sg;
	unsigned long num_pages_reqd = 0;
	long num_pages = 0;
	unsigned int fp_offset;
	unsigned int len;
	unsigned int hw_len;
	dma_addr_t hw_addr;
	unsigned long long len_rem = length;
	unsigned long long overflow_rem = overflow;
	unsigned int * sg_buf_ptr = (unsigned int *)sg_buf;
	int num_sg = 0;
	int i;

	// Create the sg_mapping struct.
	if ((sg_map = (struct sg_mapping *)kmalloc(sizeof(*sg_map), GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "riffa: fpga:%d chnl:%d, %s could not allocate memory for sg_mapping struct\n", sc->id, chnl, dir);
		return NULL;
	}

	if (length > 0) {
		// Create the pages array.
		num_pages_reqd = ((udata + length - 1)>>PAGE_SHIFT) - (udata>>PAGE_SHIFT) + 1;
		num_pages_reqd = (num_pages_reqd > sc->num_sg ? sc->num_sg : num_pages_reqd);
		if ((pages = kmalloc(num_pages_reqd * sizeof(*pages), GFP_KERNEL)) == NULL) {
			printk(KERN_ERR "riffa: fpga:%d chnl:%d, %s could not allocate memory for pages array\n", sc->id, chnl, dir);
			kfree(sg_map);
			return NULL;
		}

		// Page in the user pages.
		down_read(&current->mm->mmap_sem);
		#if LINUX_VERSION_CODE < KERNEL_VERSION(4,6,0)
		num_pages = get_user_pages(current, current->mm, udata, num_pages_reqd, 1, 0, pages, NULL);
		#elsif LINUX_VERSION_CODE < KERNEL_VERSION(4,9,0)
		num_pages = get_user_pages(udata, num_pages_reqd, 1, 0, pages, NULL);
		#else
		num_pages = get_user_pages(udata, num_pages_reqd, FOLL_WRITE, pages, NULL);
		#endif
		up_read(&current->mm->mmap_sem);
		if (num_pages <= 0) {
			printk(KERN_ERR "riffa: fpga:%d chnl:%d, %s unable to pin any pages in memory\n", sc->id, chnl, dir);
			kfree(pages);
			kfree(sg_map);
			return NULL;
		}

		// Create the scatterlist array.
		if ((sgl = kcalloc(num_pages, sizeof(*sgl), GFP_KERNEL)) == NULL) {
			printk(KERN_ERR "riffa: fpga:%d chnl:%d, %s could not allocate memory for scatterlist array\n", sc->id, chnl, dir);
			for (i = 0; i < num_pages; ++i)
				#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
				page_cache_release(pages[i]);
				#else
				put_page(pages[i]);
				#endif
			kfree(pages);
			kfree(sg_map);
			return NULL;
		}

		// Set the scatterlist values
		fp_offset = (udata & (~PAGE_MASK));
		sg_init_table(sgl, num_pages);
		for (i = 0; i < num_pages; ++i) {
			len = ((fp_offset + len_rem) > PAGE_SIZE ? (PAGE_SIZE - fp_offset) : len_rem);
			sg_set_page(&sgl[i], pages[i], len, fp_offset);
			len_rem -= len;
			fp_offset = 0;
		}

		// Map the scatterlist values and write to the common buffer area
		num_sg = dma_map_sg(&sc->dev->dev, sgl, num_pages, direction);
		for_each_sg(sgl, sg, num_sg, i) {
			hw_addr = sg_dma_address(sg);
			hw_len = sg_dma_len(sg);
			sg_buf_ptr[(i*4)+0] = (hw_addr & 0xFFFFFFFF);
			sg_buf_ptr[(i*4)+1] = ((hw_addr>>32) & 0xFFFFFFFF);
			sg_buf_ptr[(i*4)+2] = hw_len>>2; // Words!
		}
	}

	// Provide scatter gather mappings for overflow data (all in spill common buffer)
	while (len_rem == 0 && overflow_rem > 0 && num_sg < sc->num_sg) {
		sg_buf_ptr[(num_sg*4)+0] = (sc->spill_buf_hw_addr & 0xFFFFFFFF);
		sg_buf_ptr[(num_sg*4)+1] = ((sc->spill_buf_hw_addr>>32) & 0xFFFFFFFF);
		sg_buf_ptr[(num_sg*4)+2] = SPILL_BUF_SIZE>>2; // Words!
		num_sg++;
		overflow_rem -= (SPILL_BUF_SIZE > overflow ? overflow : SPILL_BUF_SIZE);
	}

	// Populate the number of bytes mapped and other sg data
	sg_map->direction = direction;
	sg_map->num_pages = num_pages;
	sg_map->num_sg = num_sg;
	sg_map->length = (length - len_rem);
	sg_map->overflow = (overflow - overflow_rem);
	sg_map->pages = pages;
	sg_map->sgl = sgl;

	return sg_map;
}

/**
 * Frees the scatterlist mappings in the struct sg_mapping pointer and frees all
 * corresponding structs.
 */
static inline void free_sg_buf(struct fpga_state * sc, struct sg_mapping * sg_map) {
	int i;

	if (sg_map == NULL)
		return;

	// Unmap the pages.
	if (sg_map->sgl != NULL)
		dma_unmap_sg(&sc->dev->dev, sg_map->sgl, sg_map->num_pages, sg_map->direction);

	// Free the pages (mark dirty if necessary).
	if (sg_map->pages != NULL) {
		if (sg_map->direction == DMA_FROM_DEVICE) {
			for (i = 0; i < sg_map->num_pages; ++i) {
				if (!PageReserved(sg_map->pages[i]))
					SetPageDirty(sg_map->pages[i]);
				#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
				page_cache_release(sg_map->pages[i]);
				#else
				put_page(sg_map->pages[i]);
				#endif
			}
		}
		else {
			for (i = 0; i < sg_map->num_pages; ++i) {
				#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
				page_cache_release(sg_map->pages[i]);
				#else
				put_page(sg_map->pages[i]);
				#endif
			}
		}
	}

	// Free the structures.
	if (sg_map->pages != NULL)
		kfree(sg_map->pages);
	if (sg_map->sgl != NULL)
		kfree(sg_map->sgl);
	kfree(sg_map);
}

/**
 * Reads data from the FPGA. Will block until all the data is received from the
 * FPGA unless timeout is non-zero. If timeout is non-zero, the function will 
 * block until all the data is received or until the timeout expires. Received 
 * data will be written directly into the user buffer, bufp, by the DMA process
 * (using scatter gather). Up to len words (each word == 32 bits) will be 
 * written. On success, the number of words received are returned. On error, 
 * returns a negative value. 
 */
static inline unsigned int chnl_recv(struct fpga_state * sc, int chnl,
				char  __user * bufp, unsigned int len, unsigned long long timeout)
{
	struct sg_mapping * sg_map;
	long tymeouto;
	long tymeout;
	int nomsg;
	unsigned int msg_type;
	unsigned int msg;
	int last = -1;
	unsigned long long offset = 0;
	unsigned long long length = 0;
	unsigned long long overflow = 0;
	unsigned long long capacity = (((unsigned long long)len)<<2);
	unsigned long long recvd = 0;
	unsigned long udata = (unsigned long)bufp;
	unsigned long max_ptr;

	DEFINE_WAIT(wait);

	// Convert timeout to jiffies.
	tymeout = (timeout == 0 ? MAX_SCHEDULE_TIMEOUT : (timeout * HZ/1000 > LONG_MAX ? LONG_MAX : timeout * HZ/1000));
	tymeouto = tymeout;

	// Initialize the sg_maps
	sc->recv[chnl]->sg_map_0 = NULL;
	sc->recv[chnl]->sg_map_1 = NULL;

	// Continue until we get a message or timeout.
	while (1) {
		while ((nomsg = pop_circ_queue(sc->recv[chnl]->msgs, &msg_type, &msg))) {
			prepare_to_wait(&sc->recv[chnl]->waitq, &wait, TASK_INTERRUPTIBLE);
			// Another check before we schedule.
			if ((nomsg = pop_circ_queue(sc->recv[chnl]->msgs, &msg_type, &msg)))
				tymeout = schedule_timeout(tymeout);
			finish_wait(&sc->recv[chnl]->waitq, &wait);
			if (signal_pending(current)) {
				free_sg_buf(sc, sc->recv[chnl]->sg_map_0);
				free_sg_buf(sc, sc->recv[chnl]->sg_map_1);
				return -ERESTARTSYS;
			}
			if (!nomsg)
				break;
			if (tymeout == 0) {
				printk(KERN_ERR "riffa: fpga:%d chnl:%d, recv timed out\n", sc->id, chnl);
				/*free_sg_buf(sc, sc->recv[chnl]->sg_map_0);
				free_sg_buf(sc, sc->recv[chnl]->sg_map_1);
				return (unsigned int)(recvd>>2);*/
			}
		}
		tymeout = tymeouto;
		DEBUG_MSG(KERN_INFO "msg_type: %d\n", msg_type); // added by cheng fei
		// Process the message.
		switch (msg_type) {
		case EVENT_TXN_OFFLAST:
			// Read the offset and last flags (always before reading length)
			offset = (((unsigned long long)(msg>>1))<<2);
			last = (msg & 0x1);
			//break;

		//case EVENT_TXN_LEN:
			// Read the length
			//length = (((unsigned long long)msg)<<2);
			length = tx_len << 2;
			recvd = 0;
			overflow = 0;
			// Check for address overflow
			max_ptr = (unsigned long)(udata + offset + length - 1);
			if (max_ptr < udata) {
				printk(KERN_ERR "riffa: fpga:%d chnl:%d, recv pointer address overflow\n", sc->id, chnl);
				overflow = length;
				length = 0;
			}
			// Check for capacity overflow
			if ((offset + length) > capacity) {
				if (offset > capacity) {
					overflow = length;
					length = 0;
				}
				else {
					overflow = length + offset - capacity;
					length = capacity - offset;
				}
			}
			// Use the recv common buffer to share the scatter gather elements.
			if (length > 0 || overflow > 0) {
				udata = udata + offset;
				sg_map = fill_sg_buf(sc, chnl, sc->recv[chnl]->buf_addr, udata, length, overflow, DMA_FROM_DEVICE);
				if (sg_map == NULL || sg_map->num_sg == 0)
					return (unsigned int)(recvd>>2);
				// Update based on the sg_mapping
				udata += sg_map->length;
				length -= sg_map->length;
				overflow -= sg_map->overflow;
				sc->recv[chnl]->sg_map_1 = sg_map;
				// Let FPGA know about the scatter gather buffer.
				write_reg(sc, CHNL_REG(chnl, TX_SG_ADDR_LO_REG_OFF), (sc->recv[chnl]->buf_hw_addr & 0xFFFFFFFF));
				write_reg(sc, CHNL_REG(chnl, TX_SG_ADDR_HI_REG_OFF), ((sc->recv[chnl]->buf_hw_addr>>32) & 0xFFFFFFFF));
				write_reg(sc, CHNL_REG(chnl, TX_SG_LEN_REG_OFF), 4 * sg_map->num_sg);

				recv_sg_buf_populated = 1;
				
				DEBUG_MSG(KERN_INFO "riffa: fpga:%d chnl:%d, recv sg buf populated, %d sent\n", sc->id, chnl, sg_map->num_sg);

				wake_up(&sc->send[chnl]->waitq);  // https://elixir.bootlin.com/linux/v4.19-rc7/source/include/linux/wait.h#L476  
				// The @condition is checked each time the waitqueue @wq_head is woken up. wake_up() has to be called after changing any variable that could change the result of the wait condition.
			}
			break;

		case EVENT_SG_BUF_READ:
			// Ignore if we haven't received offlast/len.
			if (last == -1)
				break;
			// Release the previous scatter gather data.
			if (sc->recv[chnl]->sg_map_0 != NULL)
				recvd += sc->recv[chnl]->sg_map_0->length;
			free_sg_buf(sc, sc->recv[chnl]->sg_map_0);
			sc->recv[chnl]->sg_map_0 = NULL;
			// Populate the common buffer with more scatter gather data?
			if (length > 0 || overflow > 0) {
				sg_map = fill_sg_buf(sc, chnl, sc->recv[chnl]->buf_addr, udata, length, overflow, DMA_FROM_DEVICE);
				if (sg_map == NULL || sg_map->num_sg == 0) {
					free_sg_buf(sc, sc->recv[chnl]->sg_map_0);
					free_sg_buf(sc, sc->recv[chnl]->sg_map_1);
					return (unsigned int)(recvd>>2);
				}
				// Update based on the sg_mapping
				udata += sg_map->length;
				length -= sg_map->length;
				overflow -= sg_map->overflow;
				sc->recv[chnl]->sg_map_0 = sc->recv[chnl]->sg_map_1;
				sc->recv[chnl]->sg_map_1 = sg_map;
				write_reg(sc, CHNL_REG(chnl, TX_SG_ADDR_LO_REG_OFF), (sc->recv[chnl]->buf_hw_addr & 0xFFFFFFFF));
				write_reg(sc, CHNL_REG(chnl, TX_SG_ADDR_HI_REG_OFF), ((sc->recv[chnl]->buf_hw_addr>>32) & 0xFFFFFFFF));
				write_reg(sc, CHNL_REG(chnl, TX_SG_LEN_REG_OFF), 4 * sg_map->num_sg);
				DEBUG_MSG(KERN_INFO "riffa: fpga:%d chnl:%d, recv sg buf populated, %d sent\n", sc->id, chnl, sg_map->num_sg);
			}
			break;

		case EVENT_TXN_DONE:
			recv_sg_buf_populated = 0; // resets recv sg buf parameters for next transaction.

			// Ignore if we haven't received offlast/len.
			if (last == -1)
				break;
			// Update with the true value of words transferred.
			recvd = (((unsigned long long)msg)<<2);
			// Return if this was the last transaction.
			free_sg_buf(sc, sc->recv[chnl]->sg_map_0);
			free_sg_buf(sc, sc->recv[chnl]->sg_map_1);
			sc->recv[chnl]->sg_map_0 = NULL;
			sc->recv[chnl]->sg_map_1 = NULL;
			DEBUG_MSG(KERN_INFO "riffa: fpga:%d chnl:%d, received %d words\n", sc->id, chnl, (unsigned int)(recvd>>2));
			if (last)
				return (unsigned int)(recvd>>2);
			break;

		default: 
			printk(KERN_ERR "riffa: fpga:%d chnl:%d, received unknown msg: %08x\n", sc->id, chnl, msg);
			break;
		}
	}

	return 0;
}

static inline unsigned int chnl_recv_wrapcheck(struct fpga_state * sc, int chnl,
				char  __user * bufp, unsigned int len, unsigned long long timeout)
{
	unsigned long udata = (unsigned long)bufp;
	unsigned int ret;

	// Validate the parameters.
	if (chnl >= sc->num_chnls || chnl < 0) {
		printk(KERN_INFO "riffa: fpga:%d chnl:%d, recv channel invalid!\n", sc->id, chnl);
		return 0;
	}
	if (udata & 0x3) {
		printk(KERN_INFO "riffa: fpga:%d chnl:%d, recv user buffer must be 32 bit word aligned!\n", sc->id, chnl);
		return -EINVAL;
	}

	// Ensure no simultaneous operations from several threads
	if (test_and_set_bit(CHNL_FLAG_BUSY, &sc->recv[chnl]->flags) != 0) {
		printk(KERN_ERR "riffa: fpga:%d chnl:%d, recv conflict between threads!\n", sc->id, chnl);
		return -EBUSY;
	}

	ret = chnl_recv(sc, chnl, bufp, len, timeout);

	// Clear the busy flag
	clear_bit(CHNL_FLAG_BUSY, &sc->recv[chnl]->flags);

	return ret;
}

/**
 * Writes data to the FPGA channel specified. Will block until all the data is 
 * sent to the FPGA unless a non-zero timeout is configured. If timeout is non-
 * zero, then the function will block until all data is sent or when the timeout
 * ms elapses. User data from the bufp pointer will be sent, up to len words
 * (each word == 32 bits). The channel will be told how much data to expect and 
 * at what offset. If last == 1, the FPGA channel will recognize this 
 * transaction as complete after sending. If last == 0, the FPGA channel will 
 * expect additional transactions. On success, returns the number of words sent. 
 * On error, returns a negative value. 
 */
static inline unsigned int chnl_send(struct fpga_state * sc, int chnl,
				const char  __user * bufp, unsigned int len, unsigned int offset,
				unsigned int last, unsigned long long timeout)
{
	struct sg_mapping * sg_map;
	long tymeouto;
	long tymeout;
	int nomsg;
	unsigned int msg_type;
	unsigned int msg;
	unsigned long long sent = 0;
	unsigned long long length = (((unsigned long long)len)<<2);
	unsigned long udata = (unsigned long)bufp;

	DEFINE_WAIT(wait);

	// Convert timeout to jiffies.
	tymeout = (timeout == 0 ? MAX_SCHEDULE_TIMEOUT : (timeout * HZ/1000 > LONG_MAX ? LONG_MAX : timeout * HZ/1000));
	tymeouto = tymeout;

	// Clear the message queue.
	while (!pop_circ_queue(sc->send[chnl]->msgs, &msg_type, &msg));

	// Initialize the sg_maps
	sc->send[chnl]->sg_map_0 = NULL;
	sc->send[chnl]->sg_map_1 = NULL;

	// Let FPGA know about transfer.
	DEBUG_MSG(KERN_INFO "riffa: fpga:%d chnl:%d, send (len:%d off:%d last:%d)\n", sc->id, chnl, len, offset, last);
	write_reg(sc, CHNL_REG(chnl, RX_OFFLAST_REG_OFF), ((offset<<1) | last));
	write_reg(sc, CHNL_REG(chnl, RX_LEN_REG_OFF), len);
	if (len == 0)
		return 0;

	// Use the send common buffer to share the scatter gather data
	sg_map = fill_sg_buf(sc, chnl, sc->send[chnl]->buf_addr, udata, length, 0, DMA_TO_DEVICE);
	if (sg_map == NULL || sg_map->num_sg == 0)
		return (unsigned int)(sent>>2);

	// Update based on the sg_mapping
	udata += sg_map->length;
	length -= sg_map->length;
	sc->send[chnl]->sg_map_1 = sg_map;

	if(tx_len > 0) { // FPGA initiates new Tx transaction, so "yield" to software chnl_recv() thread
		
		// gives time for software chnl_recv() thread to populate recv sg buf parameter
		wait_event_interruptible_timeout(sc->send[chnl]->waitq, (recv_sg_buf_populated == 1), timeout);
	}

	// Let FPGA know about the scatter gather buffer.
	write_reg(sc, CHNL_REG(chnl, RX_SG_ADDR_LO_REG_OFF), (sc->send[chnl]->buf_hw_addr & 0xFFFFFFFF));
	write_reg(sc, CHNL_REG(chnl, RX_SG_ADDR_HI_REG_OFF), ((sc->send[chnl]->buf_hw_addr>>32) & 0xFFFFFFFF));
	write_reg(sc, CHNL_REG(chnl, RX_SG_LEN_REG_OFF), 4 * sg_map->num_sg);
	DEBUG_MSG(KERN_INFO "riffa: fpga:%d chnl:%d, send sg buf populated, %d sent\n", sc->id, chnl, sg_map->num_sg);

	// Continue until we get a message or timeout.
	while (1) {
		while ((nomsg = pop_circ_queue(sc->send[chnl]->msgs, &msg_type, &msg))) {
			prepare_to_wait(&sc->send[chnl]->waitq, &wait, TASK_INTERRUPTIBLE);
			// Another check before we schedule.
			if ((nomsg = pop_circ_queue(sc->send[chnl]->msgs, &msg_type, &msg)))
				tymeout = schedule_timeout(tymeout);
			finish_wait(&sc->send[chnl]->waitq, &wait);
			if (signal_pending(current)) {
				free_sg_buf(sc, sc->send[chnl]->sg_map_0);
				free_sg_buf(sc, sc->send[chnl]->sg_map_1);
				return -ERESTARTSYS;
			}
			if (!nomsg)
				break;
			if (tymeout == 0) {
				printk(KERN_ERR "riffa: fpga:%d chnl:%d, send timed out\n", sc->id, chnl);
				/*free_sg_buf(sc, sc->send[chnl]->sg_map_0);
				free_sg_buf(sc, sc->send[chnl]->sg_map_1);
				return (unsigned int)(sent>>2);*/
			}
		}
		tymeout = tymeouto;

		// Process the message.
		switch (msg_type) {
		case EVENT_SG_BUF_READ:
			// Release the previous scatter gather data?
			if (sc->send[chnl]->sg_map_0 != NULL)
				sent += sc->send[chnl]->sg_map_0->length;
			free_sg_buf(sc, sc->send[chnl]->sg_map_0);
			sc->send[chnl]->sg_map_0 = NULL;
			// Populate the common buffer with more scatter gather data?
			if (length > 0) {
				sg_map = fill_sg_buf(sc, chnl, sc->send[chnl]->buf_addr, udata, length, 0, DMA_TO_DEVICE);
				if (sg_map == NULL || sg_map->num_sg == 0) {
					free_sg_buf(sc, sc->send[chnl]->sg_map_0);
					free_sg_buf(sc, sc->send[chnl]->sg_map_1);
					return (unsigned int)(sent>>2);
				}
				// Update based on the sg_mapping
				udata += sg_map->length;
				length -= sg_map->length;
				sc->send[chnl]->sg_map_0 = sc->send[chnl]->sg_map_1;
				sc->send[chnl]->sg_map_1 = sg_map;
				write_reg(sc, CHNL_REG(chnl, RX_SG_ADDR_LO_REG_OFF), (sc->send[chnl]->buf_hw_addr & 0xFFFFFFFF));
				write_reg(sc, CHNL_REG(chnl, RX_SG_ADDR_HI_REG_OFF), ((sc->send[chnl]->buf_hw_addr>>32) & 0xFFFFFFFF));
				write_reg(sc, CHNL_REG(chnl, RX_SG_LEN_REG_OFF), 4 * sg_map->num_sg);
				DEBUG_MSG(KERN_INFO "riffa: fpga:%d chnl:%d, send sg buf populated, %d sent\n", sc->id, chnl, sg_map->num_sg);
			}
			break;

		case EVENT_TXN_DONE:
			// Update with the true value of words transferred.
			sent = (((unsigned long long)msg)<<2);
			// Return as this is the end of the transaction.
			free_sg_buf(sc, sc->send[chnl]->sg_map_0);
			free_sg_buf(sc, sc->send[chnl]->sg_map_1);
			DEBUG_MSG(KERN_INFO "riffa: fpga:%d chnl:%d, sent %d words\n", sc->id, chnl, (unsigned int)(sent>>2));
			return (unsigned int)(sent>>2);
			break;

		default: 
			printk(KERN_ERR "riffa: fpga:%d chnl:%d, received unknown msg: %08x\n", sc->id, chnl, msg);
			break;
		}
	}

	return 0;
}

static inline unsigned int chnl_send_wrapcheck(struct fpga_state * sc, int chnl,
				const char  __user * bufp, unsigned int len, unsigned int offset,
				unsigned int last, unsigned long long timeout)
{
	unsigned long long length = (((unsigned long long)len)<<2);
	unsigned long udata = (unsigned long)bufp;
	unsigned long max_ptr;
	unsigned int ret;

	// Validate the parameters.
	if (chnl >= sc->num_chnls || chnl < 0) {
		printk(KERN_INFO "riffa: fpga:%d chnl:%d, send channel invalid!\n", sc->id, chnl);
		return 0;
	}
	max_ptr = (unsigned long)(udata + length - 1);
	if (max_ptr < udata) {
		printk(KERN_ERR "riffa: fpga:%d chnl:%d, send pointer address overflow\n", sc->id, chnl);
		return -EINVAL;
	}
	if (udata & 0x3) {
		printk(KERN_INFO "riffa: fpga:%d chnl:%d, send user buffer must be 32 bit word aligned!\n", sc->id, chnl);
		return -EINVAL;
	}

	// Ensure no simultaneous operations from several threads
	if (test_and_set_bit(CHNL_FLAG_BUSY, &sc->send[chnl]->flags) != 0) {
		printk(KERN_ERR "riffa: fpga:%d chnl:%d, send conflict between threads!\n", sc->id, chnl);
		return -EBUSY;
	}

	ret = chnl_send(sc, chnl, bufp, len, offset, last, timeout);

	// Clear the busy flag
	clear_bit(CHNL_FLAG_BUSY, &sc->send[chnl]->flags);

	return ret;
}

/**
 * Populates the fpga_info struct with the current FPGA state information. On 
 * success, returns 0. On error, returns a negative value. 
 */
static inline int list_fpgas(fpga_info_list * list)
{
	int i;
	int num_fpgas = 0;
	struct fpga_state * sc;

	for (i = 0; i < NUM_FPGAS; ++i) {
		if (atomic_read(&used_fpgas[i])) {
			sc = fpgas[i];
			list->id[num_fpgas] = sc->id;
			list->num_chnls[num_fpgas] = sc->num_chnls;
			list->vendor_id[num_fpgas] = sc->vendor_id;
			list->device_id[num_fpgas] = sc->device_id;
			memcpy(list->name[num_fpgas], sc->name, 16*sizeof(char));
			num_fpgas++;
		}
	}
	// Zero out the rest
	for (i = num_fpgas; i < NUM_FPGAS; ++i) {
		list->id[i] = -1;
		list->num_chnls[i] = 0;
		list->vendor_id[i] = 0;
		list->device_id[i] = 0;
		memset(list->name[i], 0, 16*sizeof(char));
	}
	list->num_fpgas = num_fpgas;

	return 0;
}

/**
 * Resets the driver for the specified FPGA. The fpga_state struct for all 
 * channels will be reset as will the FPGA itself. 
 */
static inline void reset(int id)
{
	int i;
	unsigned int dummy0;
	unsigned int dummy1;
	struct fpga_state * sc;

	if (atomic_read(&used_fpgas[id])) {
		sc = fpgas[id];
		// Disable interrupts
		atomic_set(&sc->intr_disabled, 1);
		// Reset the FPGA
		read_reg(sc, INFO_REG_OFF);

		// Reset all the channels
		for (i = 0; i < sc->num_chnls; ++i) {
			while (!pop_circ_queue(sc->send[i]->msgs, &dummy0, &dummy1));
			while (!pop_circ_queue(sc->recv[i]->msgs, &dummy0, &dummy1));
			wake_up(&sc->send[i]->waitq);
			wake_up(&sc->recv[i]->waitq);
			clear_bit(CHNL_FLAG_BUSY, &sc->send[i]->flags);
			clear_bit(CHNL_FLAG_BUSY, &sc->recv[i]->flags);
		}
		// Enable interrupts
		atomic_set(&sc->intr_disabled, 0);
	}
}

/**
 * Main entry point for reading and writing on the device. Return value depends 
 * on ioctlnum and expected behavior. See code for details.
 */
static long fpga_ioctl(struct file *filp, unsigned int ioctlnum, 
		unsigned long ioctlparam)
{	
	int rc;
	fpga_chnl_io io;
	fpga_info_list list;

	switch (ioctlnum) {
	case IOCTL_SEND:
		if ((rc = copy_from_user(&io, (void *)ioctlparam, sizeof(fpga_chnl_io)))) {
			printk(KERN_ERR "riffa: cannot read ioctl user parameter.\n");
			return rc;
		}
		if (io.id < 0 || io.id >= NUM_FPGAS || !atomic_read(&used_fpgas[io.id]))
			return 0;
		return chnl_send_wrapcheck(fpgas[io.id], io.chnl, io.data, io.len, io.offset,
				io.last, io.timeout);
	case IOCTL_RECV:
		if ((rc = copy_from_user(&io, (void *)ioctlparam, sizeof(fpga_chnl_io)))) {
			printk(KERN_ERR "riffa: cannot read ioctl user parameter.\n");
			return rc;
		}
		if (io.id < 0 || io.id >= NUM_FPGAS || !atomic_read(&used_fpgas[io.id]))
			return 0;
		return chnl_recv_wrapcheck(fpgas[io.id], io.chnl, io.data, io.len, io.timeout);
	case IOCTL_LIST:
		list_fpgas(&list);
		if ((rc = copy_to_user((void *)ioctlparam, &list, sizeof(fpga_info_list))))
			printk(KERN_ERR "riffa: cannot write ioctl user parameter.\n");
		return rc;
	case IOCTL_RESET:
		reset((int)ioctlparam);
		break;
	default:
		return -ENOTTY;
		break;
	}
	return 0;
}


///////////////////////////////////////////////////////
// PCI DRIVER HANDLERS
///////////////////////////////////////////////////////

/**
 * Allocates and initializes chnl_dir structs for each channel. Returns the 
 * number of chnl_dir structs allocated.
 */
static inline int __devinit allocate_chnls(struct pci_dev *dev, struct fpga_state * sc) 
{
	int i;
	dma_addr_t hw_addr;

	for (i = 0; i < sc->num_chnls; ++i) {
		// Allocate the recv struct
		sc->recv[i] = (struct chnl_dir *) kzalloc(sizeof(struct chnl_dir), GFP_KERNEL);
		if (sc->recv[i] == NULL)
			return i;
		sc->recv[i]->flags = 0;
		init_waitqueue_head(&sc->recv[i]->waitq);
		if ((sc->recv[i]->msgs = init_circ_queue(5)) == NULL) {
			kfree(sc->recv[i]);
			return i;
		}
		sc->recv[i]->buf_addr = pci_alloc_consistent(dev, sc->sg_buf_size, &hw_addr);
		sc->recv[i]->buf_hw_addr = hw_addr;
		if (sc->recv[i]->buf_addr == NULL) {
			free_circ_queue(sc->recv[i]->msgs);
			kfree(sc->recv[i]);
			return i;
		}

		// Allocate the send struct
		sc->send[i] = (struct chnl_dir *) kzalloc(sizeof(struct chnl_dir), GFP_KERNEL);
		if (sc->send[i] == NULL) {
			pci_free_consistent(dev, sc->sg_buf_size, sc->recv[i]->buf_addr, 
					(dma_addr_t)sc->recv[i]->buf_hw_addr);
			free_circ_queue(sc->recv[i]->msgs);
			kfree(sc->recv[i]);
			return i;
		}
		sc->send[i]->flags = 0;
		init_waitqueue_head(&sc->send[i]->waitq);
		if ((sc->send[i]->msgs = init_circ_queue(4)) == NULL) {
			kfree(sc->send[i]);
			pci_free_consistent(dev, sc->sg_buf_size, sc->recv[i]->buf_addr, 
					(dma_addr_t)sc->recv[i]->buf_hw_addr);
			free_circ_queue(sc->recv[i]->msgs);
			kfree(sc->recv[i]);
			return i;
		}
		sc->send[i]->buf_addr = pci_alloc_consistent(dev, sc->sg_buf_size, &hw_addr);
		sc->send[i]->buf_hw_addr = hw_addr;
		if (sc->send[i]->buf_addr == NULL) {
			free_circ_queue(sc->send[i]->msgs);
			kfree(sc->send[i]);
			pci_free_consistent(dev, sc->sg_buf_size, sc->recv[i]->buf_addr, 
					(dma_addr_t)sc->recv[i]->buf_hw_addr);
			free_circ_queue(sc->recv[i]->msgs);
			kfree(sc->recv[i]);
			return i;
		}
	}

	return i;
}

/**
 * Called by the OS when the device is ready for access. Returns 0 on success,
 * negative value on failure.
 */
static int __devinit fpga_probe(struct pci_dev *dev, const struct pci_device_id *id) 
{
	int i;
	int j;
	int error;
	struct fpga_state * sc;
	dma_addr_t hw_addr;
	unsigned int reg;
	u32 lnkctl_result = 0;
	u32 devctl_result = 0;
	u32 devctl2_result = 0;

	// Setup the PCIe device.
	error = pci_enable_device(dev);
	if (error < 0) {
		printk(KERN_ERR "riffa: pci_enable_device returned %d\n", error);
		return (-ENODEV);
	}

	// Enable bus master
	pci_set_master(dev);

	// Set the mask size
	error = pci_set_dma_mask(dev, DMA_BIT_MASK(64));
	if (!error)
		error = pci_set_consistent_dma_mask(dev, DMA_BIT_MASK(64));
	if (error) {
		printk(KERN_ERR "riffa: cannot set 64 bit DMA mode\n");
		pci_disable_device(dev);
		return error;
	}

	// Allocate device structure.
	sc = kzalloc(sizeof(*sc), GFP_KERNEL);
	if (sc == NULL) {
		printk(KERN_ERR "riffa: not enough memory to allocate sc\n");
		pci_disable_device(dev);
		return (-ENOMEM);
	}
	atomic_set(&sc->intr_disabled, 0);
	snprintf(sc->name, sizeof(sc->name), "%s%d", pci_name(dev), 0);
	sc->vendor_id = dev->vendor;
	sc->device_id = dev->device;
	printk(KERN_INFO "riffa: found FPGA with name: %s\n", sc->name);
	printk(KERN_INFO "riffa: vendor id: 0x%04X\n", sc->vendor_id);
	printk(KERN_INFO "riffa: device id: 0x%04X\n", sc->device_id);

	// Setup the BAR memory regions
	error = pci_request_regions(dev, sc->name);
	if (error < 0) {
		printk(KERN_ERR "riffa: pci_request_regions returned error: %d\n", error);
		pci_disable_device(dev);
		kfree(sc);
		return (-ENODEV);
	}
	
	// PCI BAR 0
	sc->bar0_addr = pci_resource_start(dev, 0);
	sc->bar0_len = pci_resource_len(dev, 0);
	sc->bar0_flags = pci_resource_flags(dev, 0);
	printk(KERN_INFO "riffa: BAR 0 address: %llx\n", sc->bar0_addr);
	printk(KERN_INFO "riffa: BAR 0 length: %lld bytes\n", sc->bar0_len);
	if (sc->bar0_len != 1024) {
		printk(KERN_ERR "riffa: BAR 0 incorrect length\n");
		pci_release_regions(dev);
		pci_disable_device(dev);
		kfree(sc);
		return (-ENODEV);
	}
	sc->bar0 = ioremap(sc->bar0_addr, sc->bar0_len);
	if (!sc->bar0) {
		printk(KERN_ERR "riffa: could not ioremap BAR 0\n");
		pci_release_regions(dev);
		pci_disable_device(dev);
		kfree(sc);
		return (-ENODEV);
	}

	// Setup MSI interrupts 
	error = pci_enable_msi(dev);
	if (error != 0) {
		printk(KERN_ERR "riffa: pci_enable_msi returned error: %d\n", error);
		iounmap(sc->bar0);
		pci_release_regions(dev);
		pci_disable_device(dev);
		kfree(sc);
		return error;
	}

	// Request an interrupt
	error = request_irq(dev->irq, intrpt_handler, IRQF_SHARED, sc->name, sc);
	if (error != 0) {
		printk(KERN_ERR "riffa: request_irq(%d) returned error: %d\n", dev->irq, error);
		pci_disable_msi(dev);
		iounmap(sc->bar0);
		pci_release_regions(dev);
		pci_disable_device(dev);
		kfree(sc);
		return error;
	}
	sc->irq = dev->irq;
	printk(KERN_INFO "riffa: MSI setup on irq %d\n", dev->irq);

	// Set extended tag bit
    error = pcie_capability_read_dword(dev,PCI_EXP_DEVCTL,&devctl_result);
	if (error != 0) {
		printk(KERN_ERR "riffa: pcie_capability_read_dword returned error: %d\n", error);
		free_irq(dev->irq, sc);
		pci_disable_msi(dev);
		iounmap(sc->bar0);
		pci_release_regions(dev);
		pci_disable_device(dev);
		kfree(sc);
		return error;
	}
	printk(KERN_INFO "riffa: PCIE_EXP_DEVCTL register: %x\n",devctl_result);  

	error = pcie_capability_write_dword(dev,PCI_EXP_DEVCTL,(devctl_result|PCI_EXP_DEVCTL_EXT_TAG));
	if (error != 0) {
		printk(KERN_ERR "riffa: pcie_capability_write_dword returned error: %d\n", error);
		free_irq(dev->irq, sc);
		pci_disable_msi(dev);
		iounmap(sc->bar0);
		pci_release_regions(dev);
		pci_disable_device(dev);
		kfree(sc);
		return error;
	}

	// Set IDO bits
	error = pcie_capability_read_dword(dev,PCI_EXP_DEVCTL2,&devctl2_result);
	if (error != 0) {
		printk(KERN_ERR "riffa: pcie_capability_read_dword returned error: %d\n", error);
		pcie_capability_write_dword(dev,PCI_EXP_DEVCTL,devctl_result);
		free_irq(dev->irq, sc);
		pci_disable_msi(dev);
		iounmap(sc->bar0);
		pci_release_regions(dev);
		pci_disable_device(dev);
		kfree(sc);
		return error;
	}
	printk(KERN_INFO "riffa: PCIE_EXP_DEVCTL2 register: %x\n",devctl2_result);  

	error = pcie_capability_write_dword(dev,PCI_EXP_DEVCTL2,(devctl2_result | PCI_EXP_DEVCTL2_IDO_REQ_EN | PCI_EXP_DEVCTL2_IDO_CMP_EN));
	if (error != 0) {
		printk(KERN_ERR "riffa: pcie_capability_write_dword returned error: %d\n", error);
		pcie_capability_write_dword(dev,PCI_EXP_DEVCTL,devctl_result);
		free_irq(dev->irq, sc);
		pci_disable_msi(dev);
		iounmap(sc->bar0);
		pci_release_regions(dev);
		pci_disable_device(dev);
		kfree(sc);
		return error;
	}

	// Set RCB to 128
    error = pcie_capability_read_dword(dev,PCI_EXP_LNKCTL,&lnkctl_result);
	if (error != 0) {
		printk(KERN_ERR "riffa: pcie_capability_read_dword returned error: %d\n", error);
		pcie_capability_write_dword(dev,PCI_EXP_DEVCTL2,devctl2_result);
		pcie_capability_write_dword(dev,PCI_EXP_DEVCTL,devctl_result);
		free_irq(dev->irq, sc);
		pci_disable_msi(dev);
		iounmap(sc->bar0);
		pci_release_regions(dev);
		pci_disable_device(dev);
		kfree(sc);
		return error;
	}
	printk(KERN_INFO "riffa: PCIE_EXP_LNKCTL register: %x\n",lnkctl_result);  

	error = pcie_capability_write_dword(dev,PCI_EXP_LNKCTL,(lnkctl_result|PCI_EXP_LNKCTL_RCB));
	if (error != 0) {
		printk(KERN_ERR "riffa: pcie_capability_write_dword returned error: %d\n", error);
		pcie_capability_write_dword(dev,PCI_EXP_DEVCTL2,devctl2_result);
		pcie_capability_write_dword(dev,PCI_EXP_DEVCTL,devctl_result);
		free_irq(dev->irq, sc);
		pci_disable_msi(dev);
		iounmap(sc->bar0);
		pci_release_regions(dev);
		pci_disable_device(dev);
		kfree(sc);
		return error;
	}
	// Read device configuration
	reg = read_reg(sc, INFO_REG_OFF);
	sc->num_chnls = ((reg>>0) & 0xF);
	sc->num_sg = SG_ELEMS*((reg>>19) & 0xF);
	sc->sg_buf_size = SG_BUF_SIZE*((reg>>19) & 0xF);
	printk(KERN_INFO "riffa: number of channels: %d\n", ((reg>>0) & 0xF));
	printk(KERN_INFO "riffa: bus interface width: %d\n", ((reg>>19) & 0xF)<<5);
	printk(KERN_INFO "riffa: bus master enabled: %d\n", ((reg>>4) & 0x1));
	printk(KERN_INFO "riffa: negotiated link width: %d\n", ((reg>>5) & 0x3F));
	printk(KERN_INFO "riffa: negotiated link rate: %d MTs\n", ((reg>>11) & 0x3)*2500);
	printk(KERN_INFO "riffa: max downstream payload: %d bytes\n", 128<<((reg>>13) & 0x7) );
	printk(KERN_INFO "riffa: max upstream payload: %d bytes\n", 128<<((reg>>16) & 0x7) );

	if (((reg>>4) & 0x1) != 1) {
		printk(KERN_ERR "riffa: bus master not enabled!\n");
		pcie_capability_write_dword(dev,PCI_EXP_LNKCTL,lnkctl_result);
		pcie_capability_write_dword(dev,PCI_EXP_DEVCTL2,devctl2_result);
		pcie_capability_write_dword(dev,PCI_EXP_DEVCTL,devctl_result);
		free_irq(dev->irq, sc);
		pci_disable_msi(dev);
		iounmap(sc->bar0);
		pci_release_regions(dev);
		pci_disable_device(dev);
		kfree(sc);
		return (-ENODEV);
	}

	if (((reg>>5) & 0x3F) == 0 || ((reg>>11) & 0x3) == 0) {
		printk(KERN_ERR "riffa: bad link parameters!\n");
		pcie_capability_write_dword(dev,PCI_EXP_LNKCTL,lnkctl_result);
		pcie_capability_write_dword(dev,PCI_EXP_DEVCTL2,devctl2_result);
		pcie_capability_write_dword(dev,PCI_EXP_DEVCTL,devctl_result);
		free_irq(dev->irq, sc);
		pci_disable_msi(dev);
		iounmap(sc->bar0);
		pci_release_regions(dev);
		pci_disable_device(dev);
		kfree(sc);
		return (-ENODEV);
	}

	if ((reg & 0xF) == 0 || (reg & 0xF) > MAX_CHNLS) {
		printk(KERN_ERR "riffa: bad number of channels!\n");
		pcie_capability_write_dword(dev,PCI_EXP_LNKCTL,lnkctl_result);
		pcie_capability_write_dword(dev,PCI_EXP_DEVCTL2,devctl2_result);
		pcie_capability_write_dword(dev,PCI_EXP_DEVCTL,devctl_result);
		free_irq(dev->irq, sc);
		pci_disable_msi(dev);
		iounmap(sc->bar0);
		pci_release_regions(dev);
		pci_disable_device(dev);
		kfree(sc);
		return (-ENODEV);
	}

	if (((reg>>19) & 0xF) == 0 || ((reg>>19) & 0xF) > MAX_BUS_WIDTH_PARAM) {
		printk(KERN_ERR "riffa: bad bus width!\n");
		pcie_capability_write_dword(dev,PCI_EXP_LNKCTL,lnkctl_result);
		pcie_capability_write_dword(dev,PCI_EXP_DEVCTL2,devctl2_result);
		pcie_capability_write_dword(dev,PCI_EXP_DEVCTL,devctl_result);
		free_irq(dev->irq, sc);
		pci_disable_msi(dev);
		iounmap(sc->bar0);
		pci_release_regions(dev);
		pci_disable_device(dev);
		kfree(sc);
		return (-ENODEV);
	}

	// Create chnl_dir structs.
	sc->recv = (struct chnl_dir **) kzalloc(sc->num_chnls*sizeof(struct chnl_dir*), GFP_KERNEL);  
	sc->send = (struct chnl_dir **) kzalloc(sc->num_chnls*sizeof(struct chnl_dir*), GFP_KERNEL);  
	if (sc->recv == NULL || sc->send == NULL) {
		printk(KERN_ERR "riffa: not enough memory to allocate chnl_dir arrays\n");
		if (sc->recv != NULL)
			kfree(sc->recv);
		if (sc->send != NULL)
			kfree(sc->send);
		pcie_capability_write_dword(dev,PCI_EXP_LNKCTL,lnkctl_result);
		pcie_capability_write_dword(dev,PCI_EXP_DEVCTL2,devctl2_result);
		pcie_capability_write_dword(dev,PCI_EXP_DEVCTL,devctl_result);
		free_irq(dev->irq, sc);
		pci_disable_msi(dev);
		iounmap(sc->bar0);
		pci_release_regions(dev);
		pci_disable_device(dev);
		kfree(sc);
		return (-ENOMEM);
	}
	j = allocate_chnls(dev, sc);
	if (j < sc->num_chnls) {
		sc->num_chnls = j;
		printk(KERN_ERR "riffa: not enough memory to allocate chnl_dir structs\n");
		for (i = 0; i < sc->num_chnls; ++i) {
			pci_free_consistent(dev, sc->sg_buf_size, sc->send[i]->buf_addr, 
					(dma_addr_t)sc->send[i]->buf_hw_addr);
			pci_free_consistent(dev, sc->sg_buf_size, sc->recv[i]->buf_addr, 
					(dma_addr_t)sc->recv[i]->buf_hw_addr);
			free_circ_queue(sc->send[i]->msgs);
			free_circ_queue(sc->recv[i]->msgs);
			kfree(sc->send[i]);
			kfree(sc->recv[i]);
		}
		kfree(sc->recv);
		kfree(sc->send);
		pcie_capability_write_dword(dev,PCI_EXP_LNKCTL,lnkctl_result);
		pcie_capability_write_dword(dev,PCI_EXP_DEVCTL2,devctl2_result);
		pcie_capability_write_dword(dev,PCI_EXP_DEVCTL,devctl_result);
		free_irq(dev->irq, sc);
		pci_disable_msi(dev);
		iounmap(sc->bar0);
		pci_release_regions(dev);
		pci_disable_device(dev);
		kfree(sc);
		return (-ENOMEM);
	}

	// Create spill buffer (for overflow on receive).
	sc->spill_buf_addr = pci_alloc_consistent(dev, SPILL_BUF_SIZE, &hw_addr);
	sc->spill_buf_hw_addr = hw_addr;
	if (sc->spill_buf_addr == NULL) {
		printk(KERN_ERR "riffa: not enough memory to allocate spill buffer\n");
		for (i = 0; i < sc->num_chnls; ++i) {
			pci_free_consistent(dev, sc->sg_buf_size, sc->send[i]->buf_addr, 
					(dma_addr_t)sc->send[i]->buf_hw_addr);
			pci_free_consistent(dev, sc->sg_buf_size, sc->recv[i]->buf_addr, 
					(dma_addr_t)sc->recv[i]->buf_hw_addr);
			free_circ_queue(sc->send[i]->msgs);
			free_circ_queue(sc->recv[i]->msgs);
			kfree(sc->send[i]);
			kfree(sc->recv[i]);
		}
		pcie_capability_write_dword(dev,PCI_EXP_LNKCTL,lnkctl_result);
		pcie_capability_write_dword(dev,PCI_EXP_DEVCTL2,devctl2_result);
		pcie_capability_write_dword(dev,PCI_EXP_DEVCTL,devctl_result);
		kfree(sc->recv);
		kfree(sc->send);
		free_irq(dev->irq, sc);
		pci_disable_msi(dev);
		iounmap(sc->bar0);
		pci_release_regions(dev);
		pci_disable_device(dev);
		kfree(sc);
	}

	// Save pointer to structure 
	pci_set_drvdata(dev, sc);
	sc->dev = dev;
	sc->id = -1;
	for (i = 0; i < NUM_FPGAS; i++) {
		if (!atomic_xchg(&used_fpgas[i], 1)) {
			sc->id = i;
			fpgas[i] = sc;
			break;
		}
	}
	if (sc->id == -1) {
		printk(KERN_ERR "riffa: could not save FPGA information, %d is limit.\n", NUM_FPGAS);
		for (i = 0; i < sc->num_chnls; ++i) {
			pci_free_consistent(dev, sc->sg_buf_size, sc->send[i]->buf_addr, 
					(dma_addr_t)sc->send[i]->buf_hw_addr);
			pci_free_consistent(dev, sc->sg_buf_size, sc->recv[i]->buf_addr, 
					(dma_addr_t)sc->recv[i]->buf_hw_addr);
			free_circ_queue(sc->send[i]->msgs);
			free_circ_queue(sc->recv[i]->msgs);
			kfree(sc->send[i]);
			kfree(sc->recv[i]);
		}
		kfree(sc->recv);
		kfree(sc->send);
		pci_free_consistent(dev, SPILL_BUF_SIZE, sc->spill_buf_addr, 
				(dma_addr_t)sc->spill_buf_hw_addr);
		pcie_capability_write_dword(dev,PCI_EXP_LNKCTL,lnkctl_result);
		pcie_capability_write_dword(dev,PCI_EXP_DEVCTL2,devctl2_result);
		pcie_capability_write_dword(dev,PCI_EXP_DEVCTL,devctl_result);
		free_irq(dev->irq, sc);
		pci_disable_msi(dev);
		iounmap(sc->bar0);
		pci_release_regions(dev);
		pci_disable_device(dev);
		kfree(sc);
		return (-ENOMEM);
	}
	else {
		printk(KERN_INFO "riffa: saved FPGA with id: %d\n", sc->id);
	}

	return 0;
}

/**
 * Called when the device is unloaded.
 */
static void __devexit fpga_remove(struct pci_dev *dev) 
{
	int i;
	u32 result;
	struct fpga_state * sc;

	pcie_capability_read_dword(dev,PCI_EXP_DEVCTL,&result);
	pcie_capability_write_dword(dev,PCI_EXP_DEVCTL,result & ~(PCI_EXP_DEVCTL_EXT_TAG | PCI_EXP_DEVCTL_RELAX_EN));

	pcie_capability_read_dword(dev,PCI_EXP_DEVCTL2,&result);
	pcie_capability_write_dword(dev,PCI_EXP_DEVCTL2,result & ~(PCI_EXP_DEVCTL2_IDO_REQ_EN | PCI_EXP_DEVCTL2_IDO_CMP_EN));

	pcie_capability_read_dword(dev,PCI_EXP_DEVCTL,&result);
	pcie_capability_write_dword(dev,PCI_EXP_LNKCTL,result & (~PCI_EXP_LNKCTL_RCB));

	if ((sc = (struct fpga_state *)pci_get_drvdata(dev)) != NULL) {
		// Free structs, memory regions, etc.
		atomic_set(&used_fpgas[sc->id], 0);
		for (i = 0; i < sc->num_chnls; ++i) {
			pci_free_consistent(dev, sc->sg_buf_size, sc->send[i]->buf_addr, 
					(dma_addr_t)sc->send[i]->buf_hw_addr);
			pci_free_consistent(dev, sc->sg_buf_size, sc->recv[i]->buf_addr, 
					(dma_addr_t)sc->recv[i]->buf_hw_addr);
			free_circ_queue(sc->send[i]->msgs);
			free_circ_queue(sc->recv[i]->msgs);
			kfree(sc->send[i]);
			kfree(sc->recv[i]);
		}
		kfree(sc->recv);
		kfree(sc->send);
		pci_free_consistent(dev, SPILL_BUF_SIZE, sc->spill_buf_addr, 
				(dma_addr_t)sc->spill_buf_hw_addr);
		free_irq(dev->irq, sc);
		iounmap(sc->bar0);
		kfree(sc);
	}
	pci_disable_msi(dev);
	pci_release_regions(dev);
	pci_disable_device(dev);
	pci_set_drvdata(dev, NULL);
}


///////////////////////////////////////////////////////
// MODULE INIT/EXIT FUNCTIONS
///////////////////////////////////////////////////////

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,8,0)
static DEFINE_PCI_DEVICE_TABLE(fpga_ids) =
#else
static const struct pci_device_id fpga_ids[] =
#endif
{
	{PCI_DEVICE(VENDOR_ID0, PCI_ANY_ID)},
	{PCI_DEVICE(VENDOR_ID1, PCI_ANY_ID)},
	{0},
};

MODULE_DEVICE_TABLE(pci, fpga_ids);
static struct pci_driver fpga_driver = {
	.name		= DEVICE_NAME,
	.id_table	= fpga_ids,
	.probe		= fpga_probe,
	.remove		= __devexit_p(fpga_remove),
};

static const struct file_operations fpga_fops = {
	.owner			= THIS_MODULE,
	.unlocked_ioctl	= fpga_ioctl,
};

/**
 * Called to initialize the PCI device. 
 */
static int __init fpga_init(void) 
{	
	int i;
	int error;

	for (i = 0; i < NUM_FPGAS; i++)
		atomic_set(&used_fpgas[i], 0);

	error = pci_register_driver(&fpga_driver);
	if (error != 0) {
		printk(KERN_ERR "riffa: pci_module_register returned %d\n", error);
		return (error);
	}

	error = register_chrdev(MAJOR_NUM, DEVICE_NAME, &fpga_fops);
	if (error < 0) {
		printk(KERN_ERR "riffa: register_chrdev returned %d\n", error);
		return (error);
	}

	mymodule_class = class_create(THIS_MODULE, DEVICE_NAME);
	if (IS_ERR(mymodule_class)) {
		error = PTR_ERR(mymodule_class);
		printk(KERN_ERR "riffa: class_create() returned %d\n", error);
		return (error);
	}

	devt = MKDEV(MAJOR_NUM, 0);
	device_create(mymodule_class, NULL, devt, "%s", DEVICE_NAME);

	return 0;
}

/**
 * Called to destroy the PCI device.
 */
static void __exit fpga_exit(void)
{
	device_destroy(mymodule_class, devt); 
	class_destroy(mymodule_class);
	pci_unregister_driver(&fpga_driver);
	unregister_chrdev(MAJOR_NUM, DEVICE_NAME);
}

module_init(fpga_init);
module_exit(fpga_exit);
