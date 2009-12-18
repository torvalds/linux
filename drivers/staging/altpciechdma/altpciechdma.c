/**
 * Driver for Altera PCIe core chaining DMA reference design.
 *
 * Copyright (C) 2008 Leon Woestenberg  <leon.woestenberg@axon.tv>
 * Copyright (C) 2008 Nickolas Heppermann  <heppermannwdt@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 * Rationale: This driver exercises the chaining DMA read and write engine
 * in the reference design. It is meant as a complementary reference
 * driver that can be used for testing early designs as well as a basis to
 * write your custom driver.
 *
 * Status: Test results from Leon Woestenberg  <leon.woestenberg@axon.tv>:
 *
 * Sendero Board w/ Cyclone II EP2C35F672C6N, PX1011A PCIe x1 PHY on a
 * Dell Precision 370 PC, x86, kernel 2.6.20 from Ubuntu 7.04.
 *
 * Sendero Board w/ Cyclone II EP2C35F672C6N, PX1011A PCIe x1 PHY on a
 * Freescale MPC8313E-RDB board, PowerPC, 2.6.24 w/ Freescale patches.
 *
 * Driver tests passed with PCIe Compiler 8.1. With PCIe 8.0 the DMA
 * loopback test had reproducable compare errors. I assume a change
 * in the compiler or reference design, but could not find evidence nor
 * documentation on a change or fix in that direction.
 *
 * The reference design does not have readable locations and thus a
 * dummy read, used to flush PCI posted writes, cannot be performed.
 *
 */

#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/pci.h>


/* by default do not build the character device interface */
/* XXX It is non-functional yet */
#ifndef ALTPCIECHDMA_CDEV
#  define ALTPCIECHDMA_CDEV 0
#endif

/* build the character device interface? */
#if ALTPCIECHDMA_CDEV
#  define MAX_CHDMA_SIZE (8 * 1024 * 1024)
#  include "mapper_user_to_sg.h"
#endif

/** driver name, mimicks Altera naming of the reference design */
#define DRV_NAME "altpciechdma"
/** number of BARs on the device */
#define APE_BAR_NUM (6)
/** BAR number where the RCSLAVE memory sits */
#define APE_BAR_RCSLAVE (0)
/** BAR number where the Descriptor Header sits */
#define APE_BAR_HEADER (2)

/** maximum size in bytes of the descriptor table, chdma logic limit */
#define APE_CHDMA_TABLE_SIZE (4096)
/* single transfer must not exceed 255 table entries. worst case this can be
 * achieved by 255 scattered pages, with only a single byte in the head and
 * tail pages. 253 * PAGE_SIZE is a safe upper bound for the transfer size.
 */
#define APE_CHDMA_MAX_TRANSFER_LEN (253 * PAGE_SIZE)

/**
 * Specifies those BARs to be mapped and the length of each mapping.
 *
 * Zero (0) means do not map, otherwise specifies the BAR lengths to be mapped.
 * If the actual BAR length is less, this is considered an error; then
 * reconfigure your PCIe core.
 *
 * @see ug_pci_express 8.0, table 7-2 at page 7-13.
 */
static const unsigned long bar_min_len[APE_BAR_NUM] =
	{ 32768, 0, 256, 0, 32768, 0 };

/**
 * Descriptor Header, controls the DMA read engine or write engine.
 *
 * The descriptor header is the main data structure for starting DMA transfers.
 *
 * It sits in End Point (FPGA) memory BAR[2] for 32-bit or BAR[3:2] for 64-bit.
 * It references a descriptor table which exists in Root Complex (PC) memory.
 * Writing the rclast field starts the DMA operation, thus all other structures
 * and fields must be setup before doing so.
 *
 * @see ug_pci_express 8.0, tables 7-3, 7-4 and 7-5 at page 7-14.
 * @note This header must be written in four 32-bit (PCI DWORD) writes.
 */
struct ape_chdma_header {
	/**
	 * w0 consists of two 16-bit fields:
	 * lsb u16 number; number of descriptors in ape_chdma_table
	 * msb u16 control; global control flags
	 */
	u32 w0;
	/* bus address to ape_chdma_table in Root Complex memory */
	u32 bdt_addr_h;
	u32 bdt_addr_l;
	/**
	 * w3 consists of two 16-bit fields:
	 * - lsb u16 rclast; last descriptor number available in Root Complex
	 *    - zero (0) means the first descriptor is ready,
	 *    - one (1) means two descriptors are ready, etc.
	 * - msb u16 reserved;
	 *
	 * @note writing to this memory location starts the DMA operation!
	 */
	u32 w3;
} __attribute__ ((packed));

/**
 * Descriptor Entry, describing a (non-scattered) single memory block transfer.
 *
 * There is one descriptor for each memory block involved in the transfer, a
 * block being a contiguous address range on the bus.
 *
 * Multiple descriptors are chained by means of the ape_chdma_table data
 * structure.
 *
 * @see ug_pci_express 8.0, tables 7-6, 7-7 and 7-8 at page 7-14 and page 7-15.
 */
struct ape_chdma_desc {
	/**
	 * w0 consists of two 16-bit fields:
	 * number of DWORDS to transfer
	 * - lsb u16 length;
	 * global control
	 * - msb u16 control;
	 */
	u32 w0;
	/* address of memory in the End Point */
	u32 ep_addr;
	/* bus address of source or destination memory in the Root Complex */
	u32 rc_addr_h;
	u32 rc_addr_l;
} __attribute__ ((packed));

/**
 * Descriptor Table, an array of descriptors describing a chained transfer.
 *
 * An array of descriptors, preceded by workspace for the End Point.
 * It exists in Root Complex memory.
 *
 * The End Point can update its last completed descriptor number in the
 * eplast field if requested by setting the EPLAST_ENA bit either
 * globally in the header's or locally in any descriptor's control field.
 *
 * @note this structure may not exceed 4096 bytes. This results in a
 * maximum of 4096 / (4 * 4) - 1 = 255 descriptors per chained transfer.
 *
 * @see ug_pci_express 8.0, tables 7-9, 7-10 and 7-11 at page 7-17 and page 7-18.
 */
struct ape_chdma_table {
	/* workspace 0x00-0x0b, reserved */
	u32 reserved1[3];
	/* workspace 0x0c-0x0f, last descriptor handled by End Point */
	u32 w3;
	/* the actual array of descriptors
    * 0x10-0x1f, 0x20-0x2f, ... 0xff0-0xfff (255 entries)
    */
	struct ape_chdma_desc desc[255];
} __attribute__ ((packed));

/**
 * Altera PCI Express ('ape') board specific book keeping data
 *
 * Keeps state of the PCIe core and the Chaining DMA controller
 * application.
 */
struct ape_dev {
	/** the kernel pci device data structure provided by probe() */
	struct pci_dev *pci_dev;
	/**
	 * kernel virtual address of the mapped BAR memory and IO regions of
	 * the End Point. Used by map_bars()/unmap_bars().
	 */
	void * __iomem bar[APE_BAR_NUM];
	/** kernel virtual address for Descriptor Table in Root Complex memory */
	struct ape_chdma_table *table_virt;
	/**
	 * bus address for the Descriptor Table in Root Complex memory, in
	 * CPU-native endianess
	 */
	dma_addr_t table_bus;
	/* if the device regions could not be allocated, assume and remember it
	 * is in use by another driver; this driver must not disable the device.
	 */
	int in_use;
	/* whether this driver enabled msi for the device */
	int msi_enabled;
	/* whether this driver could obtain the regions */
	int got_regions;
	/* irq line successfully requested by this driver, -1 otherwise */
	int irq_line;
	/* board revision */
	u8 revision;
	/* interrupt count, incremented by the interrupt handler */
	int irq_count;
#if ALTPCIECHDMA_CDEV
	/* character device */
	dev_t cdevno;
	struct cdev cdev;
	/* user space scatter gather mapper */
	struct sg_mapping_t *sgm;
#endif
};

/**
 * Using the subsystem vendor id and subsystem id, it is possible to
 * distinguish between different cards bases around the same
 * (third-party) logic core.
 *
 * Default Altera vendor and device ID's, and some (non-reserved)
 * ID's are now used here that are used amongst the testers/developers.
 */
static const struct pci_device_id ids[] = {
	{ PCI_DEVICE(0x1172, 0xE001), },
	{ PCI_DEVICE(0x2071, 0x2071), },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, ids);

#if ALTPCIECHDMA_CDEV
/* prototypes for character device */
static int sg_init(struct ape_dev *ape);
static void sg_exit(struct ape_dev *ape);
#endif

/**
 * altpciechdma_isr() - Interrupt handler
 *
 */
static irqreturn_t altpciechdma_isr(int irq, void *dev_id)
{
	struct ape_dev *ape = (struct ape_dev *)dev_id;
	if (!ape)
		return IRQ_NONE;
	ape->irq_count++;
	return IRQ_HANDLED;
}

static int __devinit scan_bars(struct ape_dev *ape, struct pci_dev *dev)
{
	int i;
	for (i = 0; i < APE_BAR_NUM; i++) {
		unsigned long bar_start = pci_resource_start(dev, i);
		if (bar_start) {
			unsigned long bar_end = pci_resource_end(dev, i);
			unsigned long bar_flags = pci_resource_flags(dev, i);
			printk(KERN_DEBUG "BAR%d 0x%08lx-0x%08lx flags 0x%08lx\n",
			  i, bar_start, bar_end, bar_flags);
		}
	}
	return 0;
}

/**
 * Unmap the BAR regions that had been mapped earlier using map_bars()
 */
static void unmap_bars(struct ape_dev *ape, struct pci_dev *dev)
{
	int i;
	for (i = 0; i < APE_BAR_NUM; i++) {
	  /* is this BAR mapped? */
		if (ape->bar[i]) {
			/* unmap BAR */
			pci_iounmap(dev, ape->bar[i]);
			ape->bar[i] = NULL;
		}
	}
}

/**
 * Map the device memory regions into kernel virtual address space after
 * verifying their sizes respect the minimum sizes needed, given by the
 * bar_min_len[] array.
 */
static int __devinit map_bars(struct ape_dev *ape, struct pci_dev *dev)
{
	int rc;
	int i;
	/* iterate through all the BARs */
	for (i = 0; i < APE_BAR_NUM; i++) {
		unsigned long bar_start = pci_resource_start(dev, i);
		unsigned long bar_end = pci_resource_end(dev, i);
		unsigned long bar_length = bar_end - bar_start + 1;
		ape->bar[i] = NULL;
		/* do not map, and skip, BARs with length 0 */
		if (!bar_min_len[i])
			continue;
		/* do not map BARs with address 0 */
		if (!bar_start || !bar_end) {
			printk(KERN_DEBUG "BAR #%d is not present?!\n", i);
			rc = -1;
			goto fail;
		}
		bar_length = bar_end - bar_start + 1;
		/* BAR length is less than driver requires? */
		if (bar_length < bar_min_len[i]) {
			printk(KERN_DEBUG "BAR #%d length = %lu bytes but driver "
			"requires at least %lu bytes\n",
			i, bar_length, bar_min_len[i]);
			rc = -1;
			goto fail;
		}
		/* map the device memory or IO region into kernel virtual
		 * address space */
		ape->bar[i] = pci_iomap(dev, i, bar_min_len[i]);
		if (!ape->bar[i]) {
			printk(KERN_DEBUG "Could not map BAR #%d.\n", i);
			rc = -1;
			goto fail;
		}
		printk(KERN_DEBUG "BAR[%d] mapped at 0x%p with length %lu(/%lu).\n", i,
		ape->bar[i], bar_min_len[i], bar_length);
	}
	/* successfully mapped all required BAR regions */
	rc = 0;
	goto success;
fail:
	/* unmap any BARs that we did map */
	unmap_bars(ape, dev);
success:
	return rc;
}

#if 0 /* not yet implemented fully FIXME add opcode */
static void __devinit rcslave_test(struct ape_dev *ape, struct pci_dev *dev)
{
	u32 *rcslave_mem = (u32 *)ape->bar[APE_BAR_RCSLAVE];
	u32 result = 0;
	/** this number is assumed to be different each time this test runs */
	u32 seed = (u32)jiffies;
	u32 value = seed;
	int i;

	/* write loop */
	value = seed;
	for (i = 1024; i < 32768 / 4 ; i++) {
		printk(KERN_DEBUG "Writing 0x%08x to 0x%p.\n",
			(u32)value, (void *)rcslave_mem + i);
		iowrite32(value, rcslave_mem + i);
		value++;
	}
	/* read-back loop */
	value = seed;
	for (i = 1024; i < 32768 / 4; i++) {
		result = ioread32(rcslave_mem + i);
		if (result != value) {
			printk(KERN_DEBUG "Wrote 0x%08x to 0x%p, but read back 0x%08x.\n",
				(u32)value, (void *)rcslave_mem + i, (u32)result);
			break;
		}
		value++;
	}
}
#endif

/* obtain the 32 most significant (high) bits of a 32-bit or 64-bit address */
#define pci_dma_h(addr) ((addr >> 16) >> 16)
/* obtain the 32 least significant (low) bits of a 32-bit or 64-bit address */
#define pci_dma_l(addr) (addr & 0xffffffffUL)

/* ape_fill_chdma_desc() - Fill a Altera PCI Express Chaining DMA descriptor
 *
 * @desc pointer to descriptor to be filled
 * @addr root complex address
 * @ep_addr end point address
 * @len number of bytes, must be a multiple of 4.
 */
static inline void ape_chdma_desc_set(struct ape_chdma_desc *desc, dma_addr_t addr, u32 ep_addr, int len)
{
  BUG_ON(len & 3);
	desc->w0 = cpu_to_le32(len / 4);
	desc->ep_addr = cpu_to_le32(ep_addr);
	desc->rc_addr_h = cpu_to_le32(pci_dma_h(addr));
	desc->rc_addr_l = cpu_to_le32(pci_dma_l(addr));
}

#if ALTPCIECHDMA_CDEV
/*
 * ape_sg_to_chdma_table() - Create a device descriptor table from a scatterlist.
 *
 * The scatterlist must have been mapped by pci_map_sg(sgm->sgl).
 *
 * @sgl scatterlist.
 * @nents Number of entries in the scatterlist.
 * @first Start index in the scatterlist sgm->sgl.
 * @ep_addr End Point address for the scatter/gather transfer.
 * @desc pointer to first descriptor
 *
 * Returns Number of entries in the table on success, -1 on error.
 */
static int ape_sg_to_chdma_table(struct scatterlist *sgl, int nents, int first, struct ape_chdma_desc *desc, u32 ep_addr)
{
	int i = first, j = 0;
	/* inspect first entry */
	dma_addr_t addr = sg_dma_address(&sgl[i]);
	unsigned int len = sg_dma_len(&sgl[i]);
	/* contiguous block */
	dma_addr_t cont_addr = addr;
	unsigned int cont_len = len;
	/* iterate over remaining entries */
	for (; j < 25 && i < nents - 1; i++) {
		/* bus address of next entry i + 1 */
		dma_addr_t next = sg_dma_address(&sgl[i + 1]);
		/* length of this entry i */
		len = sg_dma_len(&sgl[i]);
		printk(KERN_DEBUG "%04d: addr=0x%Lx length=0x%08x\n", i,
			(unsigned long long)addr, len);
		/* entry i + 1 is non-contiguous with entry i? */
		if (next != addr + len) {
			/* TODO create entry here (we could overwrite i) */
			printk(KERN_DEBUG "%4d: cont_addr=0x%Lx cont_len=0x%08x\n", j,
				(unsigned long long)cont_addr, cont_len);
			/* set descriptor for contiguous transfer */
			ape_chdma_desc_set(&desc[j], cont_addr, ep_addr, cont_len);
			/* next end point memory address */
			ep_addr += cont_len;
			/* start new contiguous block */
			cont_addr = next;
			cont_len = 0;
			j++;
		}
		/* add entry i + 1 to current contiguous block */
		cont_len += len;
		/* goto entry i + 1 */
		addr = next;
	}
	/* TODO create entry here  (we could overwrite i) */
	printk(KERN_DEBUG "%04d: addr=0x%Lx length=0x%08x\n", i,
		(unsigned long long)addr, len);
	printk(KERN_DEBUG "%4d: cont_addr=0x%Lx length=0x%08x\n", j,
		(unsigned long long)cont_addr, cont_len);
	j++;
	return j;
}
#endif

/* compare buffers */
static inline int compare(u32 *p, u32 *q, int len)
{
	int result = -1;
	int fail = 0;
	int i;
	for (i = 0; i < len / 4; i++) {
		if (*p == *q) {
			/* every so many u32 words, show equals */
			if ((i & 255) == 0)
				printk(KERN_DEBUG "[%p] = 0x%08x    [%p] = 0x%08x\n", p, *p, q, *q);
		} else {
			fail++;
			/* show the first few miscompares */
			if (fail < 10)
				printk(KERN_DEBUG "[%p] = 0x%08x != [%p] = 0x%08x ?!\n", p, *p, q, *q);
				/* but stop after a while */
			else if (fail == 10)
				printk(KERN_DEBUG "---more errors follow! not printed---\n");
			else
				/* stop compare after this many errors */
			break;
		}
		p++;
		q++;
	}
	if (!fail)
		result = 0;
	return result;
}

/* dma_test() - Perform DMA loop back test to end point and back to root complex.
 *
 * Allocate a cache-coherent buffer in host memory, consisting of four pages.
 *
 * Fill the four memory pages such that each 32-bit word contains its own address.
 *
 * Now perform a loop back test, have the end point device copy the first buffer
 * half to end point memory, then have it copy back into the second half.
 *
 *   Create a descriptor table to copy the first buffer half into End Point
 *   memory. Instruct the End Point to do a DMA read using that table.
 *
 *   Create a descriptor table to copy End Point memory to the second buffer
 *   half. Instruct the End Point to do a DMA write using that table.
 *
 * Compare results, fail or pass.
 *
 */
static int __devinit dma_test(struct ape_dev *ape, struct pci_dev *dev)
{
	/* test result; guilty until proven innocent */
	int result = -1;
	/* the DMA read header sits at address 0x00 of the DMA engine BAR */
	struct ape_chdma_header *write_header = (struct ape_chdma_header *)ape->bar[APE_BAR_HEADER];
	/* the write DMA header sits after the read header at address 0x10 */
	struct ape_chdma_header *read_header = write_header + 1;
	/* virtual address of the allocated buffer */
	u8 *buffer_virt = 0;
	/* bus address of the allocated buffer */
	dma_addr_t buffer_bus = 0;
	int i, n = 0, irq_count;

	/* temporary value used to construct 32-bit data words */
	u32 w;

	printk(KERN_DEBUG "bar_tests(), PAGE_SIZE = 0x%0x\n", (int)PAGE_SIZE);
	printk(KERN_DEBUG "write_header = 0x%p.\n", write_header);
	printk(KERN_DEBUG "read_header = 0x%p.\n", read_header);
	printk(KERN_DEBUG "&write_header->w3 = 0x%p\n", &write_header->w3);
	printk(KERN_DEBUG "&read_header->w3 = 0x%p\n", &read_header->w3);
	printk(KERN_DEBUG "ape->table_virt = 0x%p.\n", ape->table_virt);

	if (!write_header || !read_header || !ape->table_virt)
		goto fail;

	/* allocate and map coherently-cached memory for a DMA-able buffer */
	/* @see Documentation/PCI/PCI-DMA-mapping.txt, near line 318 */
	buffer_virt = (u8 *)pci_alloc_consistent(dev, PAGE_SIZE * 4, &buffer_bus);
	if (!buffer_virt) {
		printk(KERN_DEBUG "Could not allocate coherent DMA buffer.\n");
		goto fail;
	}
	printk(KERN_DEBUG "Allocated cache-coherent DMA buffer (virtual address = %p, bus address = 0x%016llx).\n",
	       buffer_virt, (u64)buffer_bus);

	/* fill first half of buffer with its virtual address as data */
	for (i = 0; i < 4 * PAGE_SIZE; i += 4)
#if 0
		*(u32 *)(buffer_virt + i) = i / PAGE_SIZE + 1;
#else
		*(u32 *)(buffer_virt + i) = (u32)(unsigned long)(buffer_virt + i);
#endif
#if 0
  compare((u32 *)buffer_virt, (u32 *)(buffer_virt + 2 * PAGE_SIZE), 8192);
#endif

#if 0
	/* fill second half of buffer with zeroes */
	for (i = 2 * PAGE_SIZE; i < 4 * PAGE_SIZE; i += 4)
		*(u32 *)(buffer_virt + i) = 0;
#endif

	/* invalidate EPLAST, outside 0-255, 0xFADE is from the testbench */
	ape->table_virt->w3 = cpu_to_le32(0x0000FADE);

	/* fill in first descriptor */
	n = 0;
	/* read 8192 bytes from RC buffer to EP address 4096 */
	ape_chdma_desc_set(&ape->table_virt->desc[n], buffer_bus, 4096, 2 * PAGE_SIZE);
#if 1
	for (i = 0; i < 255; i++)
		ape_chdma_desc_set(&ape->table_virt->desc[i], buffer_bus, 4096, 2 * PAGE_SIZE);
	/* index of last descriptor */
	n = i - 1;
#endif
#if 0
	/* fill in next descriptor */
	n++;
	/* read 1024 bytes from RC buffer to EP address 4096 + 1024 */
	ape_chdma_desc_set(&ape->table_virt->desc[n], buffer_bus + 1024, 4096 + 1024, 1024);
#endif

#if 1
	/* enable MSI after the last descriptor is completed */
	if (ape->msi_enabled)
		ape->table_virt->desc[n].w0 |= cpu_to_le32(1UL << 16)/*local MSI*/;
#endif
#if 0
	/* dump descriptor table for debugging */
	printk(KERN_DEBUG "Descriptor Table (Read, in Root Complex Memory, # = %d)\n", n + 1);
	for (i = 0; i < 4 + (n + 1) * 4; i += 4) {
		u32 *p = (u32 *)ape->table_virt;
		p += i;
		printk(KERN_DEBUG "0x%08x/0x%02x: 0x%08x (LEN=0x%x)\n", (u32)p, (u32)p & 15, *p, 4 * le32_to_cpu(*p));
		p++;
		printk(KERN_DEBUG "0x%08x/0x%02x: 0x%08x (EPA=0x%x)\n", (u32)p, (u32)p & 15, *p, le32_to_cpu(*p));
		p++;
		printk(KERN_DEBUG "0x%08x/0x%02x: 0x%08x (RCH=0x%x)\n", (u32)p, (u32)p & 15, *p, le32_to_cpu(*p));
		p++;
		printk(KERN_DEBUG "0x%08x/0x%02x: 0x%08x (RCL=0x%x)\n", (u32)p, (u32)p & 15, *p, le32_to_cpu(*p));
	}
#endif
	/* set available number of descriptors in table */
	w = (u32)(n + 1);
	w |= (1UL << 18)/*global EPLAST_EN*/;
#if 0
	if (ape->msi_enabled)
		w |= (1UL << 17)/*global MSI*/;
#endif
	printk(KERN_DEBUG "writing 0x%08x to 0x%p\n", w, (void *)&read_header->w0);
	iowrite32(w, &read_header->w0);

	/* write table address (higher 32-bits) */
	printk(KERN_DEBUG "writing 0x%08x to 0x%p\n", (u32)((ape->table_bus >> 16) >> 16), (void *)&read_header->bdt_addr_h);
	iowrite32(pci_dma_h(ape->table_bus), &read_header->bdt_addr_h);

	/* write table address (lower 32-bits) */
	printk(KERN_DEBUG "writing 0x%08x to 0x%p\n", (u32)(ape->table_bus & 0xffffffffUL), (void *)&read_header->bdt_addr_l);
	iowrite32(pci_dma_l(ape->table_bus), &read_header->bdt_addr_l);

	/* memory write barrier */
	wmb();
	printk(KERN_DEBUG "Flush posted writes\n");
	/** FIXME Add dummy read to flush posted writes but need a readable location! */
#if 0
	(void)ioread32();
#endif

	/* remember IRQ count before the transfer */
	irq_count = ape->irq_count;
	/* write number of descriptors - this starts the DMA */
	printk(KERN_DEBUG "\nStart DMA read\n");
	printk(KERN_DEBUG "writing 0x%08x to 0x%p\n", (u32)n, (void *)&read_header->w3);
	iowrite32(n, &read_header->w3);
	printk(KERN_DEBUG "EPLAST = %lu\n", le32_to_cpu(*(u32 *)&ape->table_virt->w3) & 0xffffUL);

	/** memory write barrier */
	wmb();
	/* dummy read to flush posted writes */
	/* FIXME Need a readable location! */
#if 0
	(void)ioread32();
#endif
	printk(KERN_DEBUG "POLL FOR READ:\n");
	/* poll for chain completion, 1000 times 1 millisecond */
	for (i = 0; i < 100; i++) {
		volatile u32 *p = &ape->table_virt->w3;
		u32 eplast = le32_to_cpu(*p) & 0xffffUL;
		printk(KERN_DEBUG "EPLAST = %u, n = %d\n", eplast, n);
		if (eplast == n) {
			printk(KERN_DEBUG "DONE\n");
			/* print IRQ count before the transfer */
			printk(KERN_DEBUG "#IRQs during transfer: %d\n", ape->irq_count - irq_count);
			break;
		}
		udelay(100);
	}

	/* invalidate EPLAST, outside 0-255, 0xFADE is from the testbench */
	ape->table_virt->w3 = cpu_to_le32(0x0000FADE);

	/* setup first descriptor */
	n = 0;
	ape_chdma_desc_set(&ape->table_virt->desc[n], buffer_bus + 8192, 4096, 2 * PAGE_SIZE);
#if 1
	for (i = 0; i < 255; i++)
		ape_chdma_desc_set(&ape->table_virt->desc[i], buffer_bus + 8192, 4096, 2 * PAGE_SIZE);

	/* index of last descriptor */
	n = i - 1;
#endif
#if 1 /* test variable, make a module option later */
	if (ape->msi_enabled)
		ape->table_virt->desc[n].w0 |= cpu_to_le32(1UL << 16)/*local MSI*/;
#endif
#if 0
	/* dump descriptor table for debugging */
	printk(KERN_DEBUG "Descriptor Table (Write, in Root Complex Memory, # = %d)\n", n + 1);
	for (i = 0; i < 4 + (n + 1) * 4; i += 4) {
		u32 *p = (u32 *)ape->table_virt;
		p += i;
		printk(KERN_DEBUG "0x%08x/0x%02x: 0x%08x (LEN=0x%x)\n", (u32)p, (u32)p & 15, *p, 4 * le32_to_cpu(*p));
		p++;
		printk(KERN_DEBUG "0x%08x/0x%02x: 0x%08x (EPA=0x%x)\n", (u32)p, (u32)p & 15, *p, le32_to_cpu(*p));
		p++;
		printk(KERN_DEBUG "0x%08x/0x%02x: 0x%08x (RCH=0x%x)\n", (u32)p, (u32)p & 15, *p, le32_to_cpu(*p));
		p++;
		printk(KERN_DEBUG "0x%08x/0x%02x: 0x%08x (RCL=0x%x)\n", (u32)p, (u32)p & 15, *p, le32_to_cpu(*p));
	}
#endif

	/* set number of available descriptors in the table */
	w = (u32)(n + 1);
	/* enable updates of eplast for each descriptor completion */
	w |= (u32)(1UL << 18)/*global EPLAST_EN*/;
#if 0   /* test variable, make a module option later */
	/* enable MSI for each descriptor completion */
	if (ape->msi_enabled)
		w |= (1UL << 17)/*global MSI*/;
#endif
	iowrite32(w, &write_header->w0);
	iowrite32(pci_dma_h(ape->table_bus), &write_header->bdt_addr_h);
	iowrite32(pci_dma_l(ape->table_bus), &write_header->bdt_addr_l);

	/** memory write barrier and flush posted writes */
	wmb();
	/* dummy read to flush posted writes */
	/* FIXME Need a readable location! */
#if 0
	(void)ioread32();
#endif
	irq_count = ape->irq_count;

	printk(KERN_DEBUG "\nStart DMA write\n");
	iowrite32(n, &write_header->w3);

	/** memory write barrier */
	wmb();
	/** dummy read to flush posted writes */
	/* (void) ioread32(); */

	printk(KERN_DEBUG "POLL FOR WRITE:\n");
	/* poll for completion, 1000 times 1 millisecond */
	for (i = 0; i < 100; i++) {
		volatile u32 *p = &ape->table_virt->w3;
		u32 eplast = le32_to_cpu(*p) & 0xffffUL;
		printk(KERN_DEBUG "EPLAST = %u, n = %d\n", eplast, n);
		if (eplast == n) {
			printk(KERN_DEBUG "DONE\n");
			/* print IRQ count before the transfer */
			printk(KERN_DEBUG "#IRQs during transfer: %d\n", ape->irq_count - irq_count);
			break;
		}
		udelay(100);
	}
	/* soft-reset DMA write engine */
	iowrite32(0x0000ffffUL, &write_header->w0);
	/* soft-reset DMA read engine */
	iowrite32(0x0000ffffUL, &read_header->w0);

	/** memory write barrier */
	wmb();
	/* dummy read to flush posted writes */
	/* FIXME Need a readable location! */
#if 0
	(void)ioread32();
#endif
	/* compare first half of buffer with second half, should be identical */
	result = compare((u32 *)buffer_virt, (u32 *)(buffer_virt + 2 * PAGE_SIZE), 8192);
	printk(KERN_DEBUG "DMA loop back test %s.\n", result ? "FAILED" : "PASSED");

	pci_free_consistent(dev, 4 * PAGE_SIZE, buffer_virt, buffer_bus);
fail:
	printk(KERN_DEBUG "bar_tests() end, result %d\n", result);
	return result;
}

/* Called when the PCI sub system thinks we can control the given device.
 * Inspect if we can support the device and if so take control of it.
 *
 * Return 0 when we have taken control of the given device.
 *
 * - allocate board specific bookkeeping
 * - allocate coherently-mapped memory for the descriptor table
 * - enable the board
 * - verify board revision
 * - request regions
 * - query DMA mask
 * - obtain and request irq
 * - map regions into kernel address space
 */
static int __devinit probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int rc = 0;
	struct ape_dev *ape = NULL;
	u8 irq_pin, irq_line;
	printk(KERN_DEBUG "probe(dev = 0x%p, pciid = 0x%p)\n", dev, id);

	/* allocate memory for per-board book keeping */
	ape = kzalloc(sizeof(struct ape_dev), GFP_KERNEL);
	if (!ape) {
		printk(KERN_DEBUG "Could not kzalloc()ate memory.\n");
		goto err_ape;
	}
	ape->pci_dev = dev;
	dev_set_drvdata(&dev->dev, ape);
	printk(KERN_DEBUG "probe() ape = 0x%p\n", ape);

	printk(KERN_DEBUG "sizeof(struct ape_chdma_table) = %d.\n",
		(int)sizeof(struct ape_chdma_table));
	/* the reference design has a size restriction on the table size */
	BUG_ON(sizeof(struct ape_chdma_table) > APE_CHDMA_TABLE_SIZE);

	/* allocate and map coherently-cached memory for a descriptor table */
	/* @see LDD3 page 446 */
	ape->table_virt = (struct ape_chdma_table *)pci_alloc_consistent(dev,
		APE_CHDMA_TABLE_SIZE, &ape->table_bus);
	/* could not allocate table? */
	if (!ape->table_virt) {
		printk(KERN_DEBUG "Could not dma_alloc()ate_coherent memory.\n");
		goto err_table;
	}

	printk(KERN_DEBUG "table_virt = %p, table_bus = 0x%16llx.\n",
		ape->table_virt, (u64)ape->table_bus);

	/* enable device */
	rc = pci_enable_device(dev);
	if (rc) {
		printk(KERN_DEBUG "pci_enable_device() failed\n");
		goto err_enable;
	}

	/* enable bus master capability on device */
	pci_set_master(dev);
	/* enable message signaled interrupts */
	rc = pci_enable_msi(dev);
	/* could not use MSI? */
	if (rc) {
		/* resort to legacy interrupts */
		printk(KERN_DEBUG "Could not enable MSI interrupting.\n");
		ape->msi_enabled = 0;
	/* MSI enabled, remember for cleanup */
	} else {
		printk(KERN_DEBUG "Enabled MSI interrupting.\n");
		ape->msi_enabled = 1;
	}

	pci_read_config_byte(dev, PCI_REVISION_ID, &ape->revision);
#if 0 /* example */
	/* (for example) this driver does not support revision 0x42 */
    if (ape->revision == 0x42) {
		printk(KERN_DEBUG "Revision 0x42 is not supported by this driver.\n");
		rc = -ENODEV;
		goto err_rev;
	}
#endif
	/** XXX check for native or legacy PCIe endpoint? */

	rc = pci_request_regions(dev, DRV_NAME);
	/* could not request all regions? */
	if (rc) {
		/* assume device is in use (and do not disable it later!) */
		ape->in_use = 1;
		goto err_regions;
	}
	ape->got_regions = 1;

#if 1   /* @todo For now, disable 64-bit, because I do not understand the implications (DAC!) */
	/* query for DMA transfer */
	/* @see Documentation/PCI/PCI-DMA-mapping.txt */
	if (!pci_set_dma_mask(dev, DMA_BIT_MASK(64))) {
		pci_set_consistent_dma_mask(dev, DMA_BIT_MASK(64));
		/* use 64-bit DMA */
		printk(KERN_DEBUG "Using a 64-bit DMA mask.\n");
	} else
#endif
	if (!pci_set_dma_mask(dev, DMA_BIT_MASK(32))) {
		printk(KERN_DEBUG "Could not set 64-bit DMA mask.\n");
		pci_set_consistent_dma_mask(dev, DMA_BIT_MASK(32));
		/* use 32-bit DMA */
		printk(KERN_DEBUG "Using a 32-bit DMA mask.\n");
	} else {
		printk(KERN_DEBUG "No suitable DMA possible.\n");
		/** @todo Choose proper error return code */
		rc = -1;
		goto err_mask;
	}

	rc = pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &irq_pin);
	/* could not read? */
	if (rc)
		goto err_irq;
	printk(KERN_DEBUG "IRQ pin #%d (0=none, 1=INTA#...4=INTD#).\n", irq_pin);

	/* @see LDD3, page 318 */
	rc = pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &irq_line);
	/* could not read? */
	if (rc) {
		printk(KERN_DEBUG "Could not query PCI_INTERRUPT_LINE, error %d\n", rc);
		goto err_irq;
	}
	printk(KERN_DEBUG "IRQ line #%d.\n", irq_line);
#if 1
	irq_line = dev->irq;
	/* @see LDD3, page 259 */
	rc = request_irq(irq_line, altpciechdma_isr, IRQF_SHARED, DRV_NAME, (void *)ape);
	if (rc) {
		printk(KERN_DEBUG "Could not request IRQ #%d, error %d\n", irq_line, rc);
		ape->irq_line = -1;
		goto err_irq;
	}
	/* remember which irq we allocated */
	ape->irq_line = (int)irq_line;
	printk(KERN_DEBUG "Successfully requested IRQ #%d with dev_id 0x%p\n", irq_line, ape);
#endif
	/* show BARs */
	scan_bars(ape, dev);
	/* map BARs */
	rc = map_bars(ape, dev);
	if (rc)
		goto err_map;
#if ALTPCIECHDMA_CDEV
	/* initialize character device */
	rc = sg_init(ape);
	if (rc)
		goto err_cdev;
#endif
	/* perform DMA engines loop back test */
	rc = dma_test(ape, dev);
	(void)rc;
	/* successfully took the device */
	rc = 0;
	printk(KERN_DEBUG "probe() successful.\n");
	goto end;
#if ALTPCIECHDMA_CDEV
err_cdev:
	/* unmap the BARs */
	unmap_bars(ape, dev);
#endif
err_map:
	/* free allocated irq */
	if (ape->irq_line >= 0)
		free_irq(ape->irq_line, (void *)ape);
err_irq:
	if (ape->msi_enabled)
		pci_disable_msi(dev);
	/* disable the device iff it is not in use */
	if (!ape->in_use)
		pci_disable_device(dev);
	if (ape->got_regions)
		pci_release_regions(dev);
err_mask:
err_regions:
/*err_rev:*/
/* clean up everything before device enable() */
err_enable:
	if (ape->table_virt)
		pci_free_consistent(dev, APE_CHDMA_TABLE_SIZE, ape->table_virt, ape->table_bus);
/* clean up everything before allocating descriptor table */
err_table:
	if (ape)
		kfree(ape);
err_ape:
end:
	return rc;
}

static void __devexit remove(struct pci_dev *dev)
{
	struct ape_dev *ape = dev_get_drvdata(&dev->dev);

	printk(KERN_DEBUG "remove(0x%p)\n", dev);
	printk(KERN_DEBUG "remove(dev = 0x%p) where ape = 0x%p\n", dev, ape);

	/* remove character device */
#if ALTPCIECHDMA_CDEV
	sg_exit(ape);
#endif

	if (ape->table_virt)
		pci_free_consistent(dev, APE_CHDMA_TABLE_SIZE, ape->table_virt, ape->table_bus);

	/* free IRQ
	 * @see LDD3 page 279
	 */
	if (ape->irq_line >= 0) {
		printk(KERN_DEBUG "Freeing IRQ #%d for dev_id 0x%08lx.\n",
		ape->irq_line, (unsigned long)ape);
		free_irq(ape->irq_line, (void *)ape);
	}
	/* MSI was enabled? */
	if (ape->msi_enabled) {
		/* Disable MSI @see Documentation/MSI-HOWTO.txt */
		pci_disable_msi(dev);
		ape->msi_enabled = 0;
	}
	/* unmap the BARs */
	unmap_bars(ape, dev);
	if (!ape->in_use)
		pci_disable_device(dev);
	if (ape->got_regions)
		/* to be called after device disable */
		pci_release_regions(dev);
}

#if ALTPCIECHDMA_CDEV

/*
 * Called when the device goes from unused to used.
 */
static int sg_open(struct inode *inode, struct file *file)
{
	struct ape_dev *ape;
	printk(KERN_DEBUG DRV_NAME "_open()\n");
	/* pointer to containing data structure of the character device inode */
	ape = container_of(inode->i_cdev, struct ape_dev, cdev);
	/* create a reference to our device state in the opened file */
	file->private_data = ape;
	/* create virtual memory mapper */
	ape->sgm = sg_create_mapper(MAX_CHDMA_SIZE);
	return 0;
}

/*
 * Called when the device goes from used to unused.
 */
static int sg_close(struct inode *inode, struct file *file)
{
	/* fetch device specific data stored earlier during open */
	struct ape_dev *ape = (struct ape_dev *)file->private_data;
	printk(KERN_DEBUG DRV_NAME "_close()\n");
	/* destroy virtual memory mapper */
	sg_destroy_mapper(ape->sgm);
	return 0;
}

static ssize_t sg_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
	/* fetch device specific data stored earlier during open */
	struct ape_dev *ape = (struct ape_dev *)file->private_data;
	(void)ape;
	printk(KERN_DEBUG DRV_NAME "_read(buf=0x%p, count=%lld, pos=%llu)\n", buf, (s64)count, (u64)*pos);
	return count;
}

/* sg_write() - Write to the device
 *
 * @buf userspace buffer
 * @count number of bytes in the userspace buffer
 *
 * Iterate over the userspace buffer, taking at most 255 * PAGE_SIZE bytes for
 * each DMA transfer.
 *   For each transfer, get the user pages, build a sglist, map, build a
 *   descriptor table. submit the transfer. wait for the interrupt handler
 *   to wake us on completion.
 */
static ssize_t sg_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	int hwnents, tents;
	size_t transfer_len, remaining = count, done = 0;
	u64 transfer_addr = (u64)buf;
	/* fetch device specific data stored earlier during open */
	struct ape_dev *ape = (struct ape_dev *)file->private_data;
	printk(KERN_DEBUG DRV_NAME "_write(buf=0x%p, count=%lld, pos=%llu)\n",
		buf, (s64)count, (u64)*pos);
	/* TODO transfer boundaries at PAGE_SIZE granularity */
	while (remaining > 0) {
		/* limit DMA transfer size */
		transfer_len = (remaining < APE_CHDMA_MAX_TRANSFER_LEN) ? remaining :
			APE_CHDMA_MAX_TRANSFER_LEN;
		/* get all user space buffer pages and create a scattergather list */
		sgm_map_user_pages(ape->sgm, transfer_addr, transfer_len, 0/*read from userspace*/);
		printk(KERN_DEBUG DRV_NAME "mapped_pages=%d\n", ape->sgm->mapped_pages);
		/* map all entries in the scattergather list */
		hwnents = pci_map_sg(ape->pci_dev, ape->sgm->sgl, ape->sgm->mapped_pages, DMA_TO_DEVICE);
		printk(KERN_DEBUG DRV_NAME "hwnents=%d\n", hwnents);
		/* build device descriptor tables and submit them to the DMA engine */
		tents = ape_sg_to_chdma_table(ape->sgm->sgl, hwnents, 0, &ape->table_virt->desc[0], 4096);
		printk(KERN_DEBUG DRV_NAME "tents=%d\n", hwnents);
#if 0
		while (tables) {
			/* TODO build table */
			/* TODO submit table to the device */
			/* if engine stopped and unfinished work then start engine */
		}
		put ourselves on wait queue
#endif

		dma_unmap_sg(NULL, ape->sgm->sgl, ape->sgm->mapped_pages, DMA_TO_DEVICE);
		/* dirty and free the pages */
		sgm_unmap_user_pages(ape->sgm, 1/*dirtied*/);
		/* book keeping */
		transfer_addr += transfer_len;
		remaining -= transfer_len;
		done += transfer_len;
	}
	return done;
}

/*
 * character device file operations
 */
static const struct file_operations sg_fops = {
	.owner = THIS_MODULE,
	.open = sg_open,
	.release = sg_close,
	.read = sg_read,
	.write = sg_write,
};

/* sg_init() - Initialize character device
 *
 * XXX Should ideally be tied to the device, on device probe, not module init.
 */
static int sg_init(struct ape_dev *ape)
{
	int rc;
	printk(KERN_DEBUG DRV_NAME " sg_init()\n");
	/* allocate a dynamically allocated character device node */
	rc = alloc_chrdev_region(&ape->cdevno, 0/*requested minor*/, 1/*count*/, DRV_NAME);
	/* allocation failed? */
	if (rc < 0) {
		printk("alloc_chrdev_region() = %d\n", rc);
		goto fail_alloc;
	}
	/* couple the device file operations to the character device */
	cdev_init(&ape->cdev, &sg_fops);
	ape->cdev.owner = THIS_MODULE;
	/* bring character device live */
	rc = cdev_add(&ape->cdev, ape->cdevno, 1/*count*/);
	if (rc < 0) {
		printk("cdev_add() = %d\n", rc);
		goto fail_add;
	}
	printk(KERN_DEBUG "altpciechdma = %d:%d\n", MAJOR(ape->cdevno), MINOR(ape->cdevno));
	return 0;
fail_add:
	/* free the dynamically allocated character device node */
    unregister_chrdev_region(ape->cdevno, 1/*count*/);
fail_alloc:
	return -1;
}

/* sg_exit() - Cleanup character device
 *
 * XXX Should ideally be tied to the device, on device remove, not module exit.
 */

static void sg_exit(struct ape_dev *ape)
{
	printk(KERN_DEBUG DRV_NAME " sg_exit()\n");
	/* remove the character device */
	cdev_del(&ape->cdev);
	/* free the dynamically allocated character device node */
	unregister_chrdev_region(ape->cdevno, 1/*count*/);
}

#endif /* ALTPCIECHDMA_CDEV */

/* used to register the driver with the PCI kernel sub system
 * @see LDD3 page 311
 */
static struct pci_driver pci_driver = {
	.name = DRV_NAME,
	.id_table = ids,
	.probe = probe,
	.remove = __devexit_p(remove),
	/* resume, suspend are optional */
};

/**
 * alterapciechdma_init() - Module initialization, registers devices.
 */
static int __init alterapciechdma_init(void)
{
	int rc = 0;
	printk(KERN_DEBUG DRV_NAME " init(), built at " __DATE__ " " __TIME__ "\n");
	/* register this driver with the PCI bus driver */
	rc = pci_register_driver(&pci_driver);
	if (rc < 0)
		return rc;
	return 0;
}

/**
 * alterapciechdma_init() - Module cleanup, unregisters devices.
 */
static void __exit alterapciechdma_exit(void)
{
	printk(KERN_DEBUG DRV_NAME " exit(), built at " __DATE__ " " __TIME__ "\n");
	/* unregister this driver from the PCI bus driver */
	pci_unregister_driver(&pci_driver);
}

MODULE_LICENSE("GPL");

module_init(alterapciechdma_init);
module_exit(alterapciechdma_exit);

