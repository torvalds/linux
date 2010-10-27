/* -*- mode: c; c-basic-offset: 8 -*- */

/* NCR Quad 720 MCA SCSI Driver
 *
 * Copyright (C) 2003 by James.Bottomley@HansenPartnership.com
 */

#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mca.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/io.h>

#include "scsi.h"
#include <scsi/scsi_host.h>

#include "ncr53c8xx.h"

#include "NCR_Q720.h"

static struct ncr_chip q720_chip __initdata = {
	.revision_id =	0x0f,
	.burst_max =	3,
	.offset_max =	8,
	.nr_divisor =	4,
	.features =	FE_WIDE | FE_DIFF | FE_VARCLK,
};

MODULE_AUTHOR("James Bottomley");
MODULE_DESCRIPTION("NCR Quad 720 SCSI Driver");
MODULE_LICENSE("GPL");

#define NCR_Q720_VERSION		"0.9"

/* We needs this helper because we have up to four hosts per struct device */
struct NCR_Q720_private {
	struct device		*dev;
	void __iomem *		mem_base;
	__u32			phys_mem_base;
	__u32			mem_size;
	__u8			irq;
	__u8			siops;
	__u8			irq_enable;
	struct Scsi_Host	*hosts[4];
};

static struct scsi_host_template NCR_Q720_tpnt = {
	.module			= THIS_MODULE,
	.proc_name		= "NCR_Q720",
};

static irqreturn_t
NCR_Q720_intr(int irq, void *data)
{
	struct NCR_Q720_private *p = (struct NCR_Q720_private *)data;
	__u8 sir = (readb(p->mem_base + 0x0d) & 0xf0) >> 4;
	__u8 siop;

	sir |= ~p->irq_enable;

	if(sir == 0xff)
		return IRQ_NONE;


	while((siop = ffz(sir)) < p->siops) {
		sir |= 1<<siop;
		ncr53c8xx_intr(irq, p->hosts[siop]);
	}
	return IRQ_HANDLED;
}

static int __init
NCR_Q720_probe_one(struct NCR_Q720_private *p, int siop,
		int irq, int slot, __u32 paddr, void __iomem *vaddr)
{
	struct ncr_device device;
	__u8 scsi_id;
	static int unit = 0;
	__u8 scsr1 = readb(vaddr + NCR_Q720_SCSR_OFFSET + 1);
	__u8 differential = readb(vaddr + NCR_Q720_SCSR_OFFSET) & 0x20;
	__u8 version;
	int error;

	scsi_id = scsr1 >> 4;
	/* enable burst length 16 (FIXME: should allow this) */
	scsr1 |= 0x02;
	/* force a siop reset */
	scsr1 |= 0x04;
	writeb(scsr1, vaddr + NCR_Q720_SCSR_OFFSET + 1);
	udelay(10);
	version = readb(vaddr + 0x18) >> 4;

	memset(&device, 0, sizeof(struct ncr_device));
		/* Initialise ncr_device structure with items required by ncr_attach. */
	device.chip		= q720_chip;
	device.chip.revision_id	= version;
	device.host_id		= scsi_id;
	device.dev		= p->dev;
	device.slot.base	= paddr;
	device.slot.base_c	= paddr;
	device.slot.base_v	= vaddr;
	device.slot.irq		= irq;
	device.differential	= differential ? 2 : 0;
	printk("Q720 probe unit %d (siop%d) at 0x%lx, diff = %d, vers = %d\n", unit, siop,
	       (unsigned long)paddr, differential, version);

	p->hosts[siop] = ncr_attach(&NCR_Q720_tpnt, unit++, &device);
	
	if (!p->hosts[siop]) 
		goto fail;

	p->irq_enable |= (1<<siop);
	scsr1 = readb(vaddr + NCR_Q720_SCSR_OFFSET + 1);
	/* clear the disable interrupt bit */
	scsr1 &= ~0x01;
	writeb(scsr1, vaddr + NCR_Q720_SCSR_OFFSET + 1);

	error = scsi_add_host(p->hosts[siop], p->dev);
	if (error)
		ncr53c8xx_release(p->hosts[siop]);
	else
		scsi_scan_host(p->hosts[siop]);
	return error;

 fail:
	return -ENODEV;
}

/* Detect a Q720 card.  Note, because of the setup --- the chips are
 * essentially connectecd to the MCA bus independently, it is easier
 * to set them up as two separate host adapters, rather than one
 * adapter with two channels */
static int __init
NCR_Q720_probe(struct device *dev)
{
	struct NCR_Q720_private *p;
	static int banner = 1;
	struct mca_device *mca_dev = to_mca_device(dev);
	int slot = mca_dev->slot;
	int found = 0;
	int irq, i, siops;
	__u8 pos2, pos4, asr2, asr9, asr10;
	__u16 io_base;
	__u32 base_addr, mem_size;
	void __iomem *mem_base;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	pos2 = mca_device_read_pos(mca_dev, 2);
	/* enable device */
	pos2 |=  NCR_Q720_POS2_BOARD_ENABLE | NCR_Q720_POS2_INTERRUPT_ENABLE;
	mca_device_write_pos(mca_dev, 2, pos2);

	io_base = (pos2 & NCR_Q720_POS2_IO_MASK) << NCR_Q720_POS2_IO_SHIFT;


	if(banner) {
		printk(KERN_NOTICE "NCR Q720: Driver Version " NCR_Q720_VERSION "\n"
		       "NCR Q720:  Copyright (c) 2003 by James.Bottomley@HansenPartnership.com\n"
		       "NCR Q720:\n");
		banner = 0;
	}
	io_base = mca_device_transform_ioport(mca_dev, io_base);

	/* OK, this is phase one of the bootstrap, we now know the
	 * I/O space base address.  All the configuration registers
	 * are mapped here (including pos) */

	/* sanity check I/O mapping */
	i = inb(io_base) | (inb(io_base+1)<<8);
	if(i != NCR_Q720_MCA_ID) {
		printk(KERN_ERR "NCR_Q720, adapter failed to I/O map registers correctly at 0x%x(0x%x)\n", io_base, i);
		kfree(p);
		return -ENODEV;
	}

	/* Phase II, find the ram base and memory map the board register */
	pos4 = inb(io_base + 4);
	/* enable streaming data */
	pos4 |= 0x01;
	outb(pos4, io_base + 4);
	base_addr = (pos4 & 0x7e) << 20;
	base_addr += (pos4 & 0x80) << 23;
	asr10 = inb(io_base + 0x12);
	base_addr += (asr10 & 0x80) << 24;
	base_addr += (asr10 & 0x70) << 23;

	/* OK, got the base addr, now we need to find the ram size,
	 * enable and map it */
	asr9 = inb(io_base + 0x11);
	i = (asr9 & 0xc0) >> 6;
	if(i == 0)
		mem_size = 1024;
	else
		mem_size = 1 << (19 + i);

	/* enable the sram mapping */
	asr9 |= 0x20;

	/* disable the rom mapping */
	asr9 &= ~0x10;

	outb(asr9, io_base + 0x11);

	if(!request_mem_region(base_addr, mem_size, "NCR_Q720")) {
		printk(KERN_ERR "NCR_Q720: Failed to claim memory region 0x%lx\n-0x%lx",
		       (unsigned long)base_addr,
		       (unsigned long)(base_addr + mem_size));
		goto out_free;
	}
	
	if (dma_declare_coherent_memory(dev, base_addr, base_addr,
					mem_size, DMA_MEMORY_MAP)
	    != DMA_MEMORY_MAP) {
		printk(KERN_ERR "NCR_Q720: DMA declare memory failed\n");
		goto out_release_region;
	}

	/* The first 1k of the memory buffer is a memory map of the registers
	 */
	mem_base = dma_mark_declared_memory_occupied(dev, base_addr,
							    1024);
	if (IS_ERR(mem_base)) {
		printk("NCR_Q720 failed to reserve memory mapped region\n");
		goto out_release;
	}

	/* now also enable accesses in asr 2 */
	asr2 = inb(io_base + 0x0a);

	asr2 |= 0x01;

	outb(asr2, io_base + 0x0a);

	/* get the number of SIOPs (this should be 2 or 4) */
	siops = ((asr2 & 0xe0) >> 5) + 1;

	/* sanity check mapping (again) */
	i = readw(mem_base);
	if(i != NCR_Q720_MCA_ID) {
		printk(KERN_ERR "NCR_Q720, adapter failed to memory map registers correctly at 0x%lx(0x%x)\n", (unsigned long)base_addr, i);
		goto out_release;
	}

	irq = readb(mem_base + 5) & 0x0f;
	
	
	/* now do the bus related transforms */
	irq = mca_device_transform_irq(mca_dev, irq);

	printk(KERN_NOTICE "NCR Q720: found in slot %d  irq = %d  mem base = 0x%lx siops = %d\n", slot, irq, (unsigned long)base_addr, siops);
	printk(KERN_NOTICE "NCR Q720: On board ram %dk\n", mem_size/1024);

	p->dev = dev;
	p->mem_base = mem_base;
	p->phys_mem_base = base_addr;
	p->mem_size = mem_size;
	p->irq = irq;
	p->siops = siops;

	if (request_irq(irq, NCR_Q720_intr, IRQF_SHARED, "NCR_Q720", p)) {
		printk(KERN_ERR "NCR_Q720: request irq %d failed\n", irq);
		goto out_release;
	}
	/* disable all the siop interrupts */
	for(i = 0; i < siops; i++) {
		void __iomem *reg_scsr1 = mem_base + NCR_Q720_CHIP_REGISTER_OFFSET
			+ i*NCR_Q720_SIOP_SHIFT + NCR_Q720_SCSR_OFFSET + 1;
		__u8 scsr1 = readb(reg_scsr1);
		scsr1 |= 0x01;
		writeb(scsr1, reg_scsr1);
	}

	/* plumb in all 720 chips */
	for (i = 0; i < siops; i++) {
		void __iomem *siop_v_base = mem_base + NCR_Q720_CHIP_REGISTER_OFFSET
			+ i*NCR_Q720_SIOP_SHIFT;
		__u32 siop_p_base = base_addr + NCR_Q720_CHIP_REGISTER_OFFSET
			+ i*NCR_Q720_SIOP_SHIFT;
		__u16 port = io_base + NCR_Q720_CHIP_REGISTER_OFFSET
			+ i*NCR_Q720_SIOP_SHIFT;
		int err;

		outb(0xff, port + 0x40);
		outb(0x07, port + 0x41);
		if ((err = NCR_Q720_probe_one(p, i, irq, slot,
					      siop_p_base, siop_v_base)) != 0)
			printk("Q720: SIOP%d: probe failed, error = %d\n",
			       i, err);
		else
			found++;
	}

	if (!found) {
		kfree(p);
		return -ENODEV;
	}

	mca_device_set_claim(mca_dev, 1);
	mca_device_set_name(mca_dev, "NCR_Q720");
	dev_set_drvdata(dev, p);

	return 0;

 out_release:
	dma_release_declared_memory(dev);
 out_release_region:
	release_mem_region(base_addr, mem_size);
 out_free:
	kfree(p);

	return -ENODEV;
}

static void __exit
NCR_Q720_remove_one(struct Scsi_Host *host)
{
	scsi_remove_host(host);
	ncr53c8xx_release(host);
}

static int __exit
NCR_Q720_remove(struct device *dev)
{
	struct NCR_Q720_private *p = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < p->siops; i++)
		if(p->hosts[i])
			NCR_Q720_remove_one(p->hosts[i]);

	dma_release_declared_memory(dev);
	release_mem_region(p->phys_mem_base, p->mem_size);
	free_irq(p->irq, p);
	kfree(p);
	return 0;
}

static short NCR_Q720_id_table[] = { NCR_Q720_MCA_ID, 0 };

static struct mca_driver NCR_Q720_driver = {
	.id_table = NCR_Q720_id_table,
	.driver = {
		.name		= "NCR_Q720",
		.bus		= &mca_bus_type,
		.probe		= NCR_Q720_probe,
		.remove		= __devexit_p(NCR_Q720_remove),
	},
};

static int __init
NCR_Q720_init(void)
{
	int ret = ncr53c8xx_init();
	if (!ret)
		ret = mca_register_driver(&NCR_Q720_driver);
	if (ret)
		ncr53c8xx_exit();
	return ret;
}

static void __exit
NCR_Q720_exit(void)
{
	mca_unregister_driver(&NCR_Q720_driver);
	ncr53c8xx_exit();
}

module_init(NCR_Q720_init);
module_exit(NCR_Q720_exit);
