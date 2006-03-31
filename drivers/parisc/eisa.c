/*
 * eisa.c - provide support for EISA adapters in PA-RISC machines
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Copyright (c) 2001 Matthew Wilcox for Hewlett Packard
 * Copyright (c) 2001 Daniel Engstrom <5116@telia.com>
 *
 * There are two distinct EISA adapters.  Mongoose is found in machines
 * before the 712; then the Wax ASIC is used.  To complicate matters, the
 * Wax ASIC also includes a PS/2 and RS-232 controller, but those are
 * dealt with elsewhere; this file is concerned only with the EISA portions
 * of Wax.
 * 
 * 
 * HINT:
 * -----
 * To allow an ISA card to work properly in the EISA slot you need to
 * set an edge trigger level. This may be done on the palo command line 
 * by adding the kernel parameter "eisa_irq_edge=n,n2,[...]]", with 
 * n and n2 as the irq levels you want to use.
 * 
 * Example: "eisa_irq_edge=10,11" allows ISA cards to operate at 
 * irq levels 10 and 11.
 */

#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/eisa.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/processor.h>
#include <asm/parisc-device.h>
#include <asm/delay.h>
#include <asm/eisa_bus.h>
#include <asm/eisa_eeprom.h>

#if 0
#define EISA_DBG(msg, arg... ) printk(KERN_DEBUG "eisa: " msg , ## arg )
#else
#define EISA_DBG(msg, arg... )  
#endif

#define SNAKES_EEPROM_BASE_ADDR 0xF0810400
#define MIRAGE_EEPROM_BASE_ADDR 0xF00C0400

static DEFINE_SPINLOCK(eisa_irq_lock);

void __iomem *eisa_eeprom_addr __read_mostly;

/* We can only have one EISA adapter in the system because neither
 * implementation can be flexed.
 */
static struct eisa_ba {
	struct pci_hba_data	hba;
	unsigned long eeprom_addr;
	struct eisa_root_device root;
} eisa_dev;

/* Port ops */

static inline unsigned long eisa_permute(unsigned short port)
{
	if (port & 0x300) {
		return 0xfc000000 | ((port & 0xfc00) >> 6)
			| ((port & 0x3f8) << 9) | (port & 7);
	} else {
		return 0xfc000000 | port;
	}
}

unsigned char eisa_in8(unsigned short port)
{
	if (EISA_bus)
		return gsc_readb(eisa_permute(port));
	return 0xff;
}

unsigned short eisa_in16(unsigned short port)
{
	if (EISA_bus)
		return le16_to_cpu(gsc_readw(eisa_permute(port)));
	return 0xffff;
}

unsigned int eisa_in32(unsigned short port)
{
	if (EISA_bus)
		return le32_to_cpu(gsc_readl(eisa_permute(port)));
	return 0xffffffff;
}

void eisa_out8(unsigned char data, unsigned short port)
{
	if (EISA_bus)
		gsc_writeb(data, eisa_permute(port));
}

void eisa_out16(unsigned short data, unsigned short port)
{
	if (EISA_bus)	
		gsc_writew(cpu_to_le16(data), eisa_permute(port));
}

void eisa_out32(unsigned int data, unsigned short port)
{
	if (EISA_bus)
		gsc_writel(cpu_to_le32(data), eisa_permute(port));
}

#ifndef CONFIG_PCI
/* We call these directly without PCI.  See asm/io.h. */
EXPORT_SYMBOL(eisa_in8);
EXPORT_SYMBOL(eisa_in16);
EXPORT_SYMBOL(eisa_in32);
EXPORT_SYMBOL(eisa_out8);
EXPORT_SYMBOL(eisa_out16);
EXPORT_SYMBOL(eisa_out32);
#endif

/* Interrupt handling */

/* cached interrupt mask registers */
static int master_mask;
static int slave_mask;

/* the trig level can be set with the
 * eisa_irq_edge=n,n,n commandline parameter 
 * We should really read this from the EEPROM 
 * in the furure. 
 */
/* irq 13,8,2,1,0 must be edge */
static unsigned int eisa_irq_level __read_mostly; /* default to edge triggered */


/* called by free irq */
static void eisa_disable_irq(unsigned int irq)
{
	unsigned long flags;

	EISA_DBG("disable irq %d\n", irq);
	/* just mask for now */
	spin_lock_irqsave(&eisa_irq_lock, flags);
        if (irq & 8) {
		slave_mask |= (1 << (irq&7));
		eisa_out8(slave_mask, 0xa1);
	} else {
		master_mask |= (1 << (irq&7));
		eisa_out8(master_mask, 0x21);
	}
	spin_unlock_irqrestore(&eisa_irq_lock, flags);
	EISA_DBG("pic0 mask %02x\n", eisa_in8(0x21));
	EISA_DBG("pic1 mask %02x\n", eisa_in8(0xa1));
}

/* called by request irq */
static void eisa_enable_irq(unsigned int irq)
{
	unsigned long flags;
	EISA_DBG("enable irq %d\n", irq);
		
	spin_lock_irqsave(&eisa_irq_lock, flags);
        if (irq & 8) {
		slave_mask &= ~(1 << (irq&7));
		eisa_out8(slave_mask, 0xa1);
	} else {
		master_mask &= ~(1 << (irq&7));
		eisa_out8(master_mask, 0x21);
	}
	spin_unlock_irqrestore(&eisa_irq_lock, flags);
	EISA_DBG("pic0 mask %02x\n", eisa_in8(0x21));
	EISA_DBG("pic1 mask %02x\n", eisa_in8(0xa1));
}

static unsigned int eisa_startup_irq(unsigned int irq)
{
	eisa_enable_irq(irq);
	return 0;
}

static struct hw_interrupt_type eisa_interrupt_type = {
	.typename =	"EISA",
	.startup =	eisa_startup_irq,
	.shutdown =	eisa_disable_irq,
	.enable =	eisa_enable_irq,
	.disable =	eisa_disable_irq,
	.ack =		no_ack_irq,
	.end =		no_end_irq,
};

static irqreturn_t eisa_irq(int wax_irq, void *intr_dev, struct pt_regs *regs)
{
	int irq = gsc_readb(0xfc01f000); /* EISA supports 16 irqs */
	unsigned long flags;
        
	spin_lock_irqsave(&eisa_irq_lock, flags);
	/* read IRR command */
	eisa_out8(0x0a, 0x20);
	eisa_out8(0x0a, 0xa0);

	EISA_DBG("irq IAR %02x 8259-1 irr %02x 8259-2 irr %02x\n",
		   irq, eisa_in8(0x20), eisa_in8(0xa0));
   
	/* read ISR command */
	eisa_out8(0x0a, 0x20);
	eisa_out8(0x0a, 0xa0);
	EISA_DBG("irq 8259-1 isr %02x imr %02x 8259-2 isr %02x imr %02x\n",
		 eisa_in8(0x20), eisa_in8(0x21), eisa_in8(0xa0), eisa_in8(0xa1));
	
	irq &= 0xf;
	
	/* mask irq and write eoi */
	if (irq & 8) {
		slave_mask |= (1 << (irq&7));
		eisa_out8(slave_mask, 0xa1);
		eisa_out8(0x60 | (irq&7),0xa0);/* 'Specific EOI' to slave */
		eisa_out8(0x62,0x20);	/* 'Specific EOI' to master-IRQ2 */
		
	} else {
		master_mask |= (1 << (irq&7));
		eisa_out8(master_mask, 0x21);
		eisa_out8(0x60|irq,0x20);	/* 'Specific EOI' to master */
	}
	spin_unlock_irqrestore(&eisa_irq_lock, flags);

	__do_IRQ(irq, regs);
   
	spin_lock_irqsave(&eisa_irq_lock, flags);
	/* unmask */
        if (irq & 8) {
		slave_mask &= ~(1 << (irq&7));
		eisa_out8(slave_mask, 0xa1);
	} else {
		master_mask &= ~(1 << (irq&7));
		eisa_out8(master_mask, 0x21);
	}
	spin_unlock_irqrestore(&eisa_irq_lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t dummy_irq2_handler(int _, void *dev, struct pt_regs *regs)
{
	printk(KERN_ALERT "eisa: uhh, irq2?\n");
	return IRQ_HANDLED;
}

static struct irqaction irq2_action = {
	.handler = dummy_irq2_handler,
	.name = "cascade",
};

static void init_eisa_pic(void)
{
	unsigned long flags;
	
	spin_lock_irqsave(&eisa_irq_lock, flags);

	eisa_out8(0xff, 0x21); /* mask during init */
	eisa_out8(0xff, 0xa1); /* mask during init */
	
	/* master pic */
	eisa_out8(0x11,0x20); /* ICW1 */   
	eisa_out8(0x00,0x21); /* ICW2 */   
	eisa_out8(0x04,0x21); /* ICW3 */   
	eisa_out8(0x01,0x21); /* ICW4 */   
	eisa_out8(0x40,0x20); /* OCW2 */   
	
	/* slave pic */
	eisa_out8(0x11,0xa0); /* ICW1 */   
	eisa_out8(0x08,0xa1); /* ICW2 */   
        eisa_out8(0x02,0xa1); /* ICW3 */   
	eisa_out8(0x01,0xa1); /* ICW4 */   
	eisa_out8(0x40,0xa0); /* OCW2 */   
        
	udelay(100);
	
	slave_mask = 0xff; 
	master_mask = 0xfb; 
	eisa_out8(slave_mask, 0xa1); /* OCW1 */
	eisa_out8(master_mask, 0x21); /* OCW1 */
	
	/* setup trig level */
	EISA_DBG("EISA edge/level %04x\n", eisa_irq_level);
	
	eisa_out8(eisa_irq_level&0xff, 0x4d0); /* Set all irq's to edge  */
	eisa_out8((eisa_irq_level >> 8) & 0xff, 0x4d1); 
	
	EISA_DBG("pic0 mask %02x\n", eisa_in8(0x21));
	EISA_DBG("pic1 mask %02x\n", eisa_in8(0xa1));
	EISA_DBG("pic0 edge/level %02x\n", eisa_in8(0x4d0));
	EISA_DBG("pic1 edge/level %02x\n", eisa_in8(0x4d1));
	
	spin_unlock_irqrestore(&eisa_irq_lock, flags);
}

/* Device initialisation */

#define is_mongoose(dev) (dev->id.sversion == 0x00076)

static int __devinit eisa_probe(struct parisc_device *dev)
{
	int i, result;

	char *name = is_mongoose(dev) ? "Mongoose" : "Wax";

	printk(KERN_INFO "%s EISA Adapter found at 0x%08lx\n", 
		name, dev->hpa.start);

	eisa_dev.hba.dev = dev;
	eisa_dev.hba.iommu = ccio_get_iommu(dev);

	eisa_dev.hba.lmmio_space.name = "EISA";
	eisa_dev.hba.lmmio_space.start = F_EXTEND(0xfc000000);
	eisa_dev.hba.lmmio_space.end = F_EXTEND(0xffbfffff);
	eisa_dev.hba.lmmio_space.flags = IORESOURCE_MEM;
	result = ccio_request_resource(dev, &eisa_dev.hba.lmmio_space);
	if (result < 0) {
		printk(KERN_ERR "EISA: failed to claim EISA Bus address space!\n");
		return result;
	}
	eisa_dev.hba.io_space.name = "EISA";
	eisa_dev.hba.io_space.start = 0;
	eisa_dev.hba.io_space.end = 0xffff;
	eisa_dev.hba.lmmio_space.flags = IORESOURCE_IO;
	result = request_resource(&ioport_resource, &eisa_dev.hba.io_space);
	if (result < 0) {
		printk(KERN_ERR "EISA: failed to claim EISA Bus port space!\n");
		return result;
	}
	pcibios_register_hba(&eisa_dev.hba);

	result = request_irq(dev->irq, eisa_irq, SA_SHIRQ, "EISA", &eisa_dev);
	if (result) {
		printk(KERN_ERR "EISA: request_irq failed!\n");
		return result;
	}
	
	/* Reserve IRQ2 */
	irq_desc[2].action = &irq2_action;
	
	for (i = 0; i < 16; i++) {
		irq_desc[i].handler = &eisa_interrupt_type;
	}
	
	EISA_bus = 1;

	if (dev->num_addrs) {
		/* newer firmware hand out the eeprom address */
		eisa_dev.eeprom_addr = dev->addr[0];
	} else {
		/* old firmware, need to figure out the box */
		if (is_mongoose(dev)) {
			eisa_dev.eeprom_addr = SNAKES_EEPROM_BASE_ADDR;
		} else {
			eisa_dev.eeprom_addr = MIRAGE_EEPROM_BASE_ADDR;
		}
	}
	eisa_eeprom_addr = ioremap_nocache(eisa_dev.eeprom_addr, HPEE_MAX_LENGTH);
	result = eisa_enumerator(eisa_dev.eeprom_addr, &eisa_dev.hba.io_space,
			&eisa_dev.hba.lmmio_space);
	init_eisa_pic();

	if (result >= 0) {
		/* FIXME : Don't enumerate the bus twice. */
		eisa_dev.root.dev = &dev->dev;
		dev->dev.driver_data = &eisa_dev.root;
		eisa_dev.root.bus_base_addr = 0;
		eisa_dev.root.res = &eisa_dev.hba.io_space;
		eisa_dev.root.slots = result;
		eisa_dev.root.dma_mask = 0xffffffff; /* wild guess */
		if (eisa_root_register (&eisa_dev.root)) {
			printk(KERN_ERR "EISA: Failed to register EISA root\n");
			return -1;
		}
	}
	
	return 0;
}

static struct parisc_device_id eisa_tbl[] = {
	{ HPHW_BA, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x00076 }, /* Mongoose */
	{ HPHW_BA, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x00090 }, /* Wax EISA */
	{ 0, }
};

MODULE_DEVICE_TABLE(parisc, eisa_tbl);

static struct parisc_driver eisa_driver = {
	.name =		"eisa_ba",
	.id_table =	eisa_tbl,
	.probe =	eisa_probe,
};

void __init eisa_init(void)
{
	register_parisc_driver(&eisa_driver);
}


static unsigned int eisa_irq_configured;
void eisa_make_irq_level(int num)
{
	if (eisa_irq_configured& (1<<num)) {
		printk(KERN_WARNING
		       "IRQ %d polarity configured twice (last to level)\n", 
		       num);
	}
	eisa_irq_level |= (1<<num); /* set the corresponding bit */
	eisa_irq_configured |= (1<<num); /* set the corresponding bit */
}

void eisa_make_irq_edge(int num)
{
	if (eisa_irq_configured& (1<<num)) {
		printk(KERN_WARNING 
		       "IRQ %d polarity configured twice (last to edge)\n",
		       num);
	}
	eisa_irq_level &= ~(1<<num); /* clear the corresponding bit */
	eisa_irq_configured |= (1<<num); /* set the corresponding bit */
}

static int __init eisa_irq_setup(char *str)
{
	char *cur = str;
	int val;

	EISA_DBG("IRQ setup\n");
	while (cur != NULL) {
		char *pe;
		
		val = (int) simple_strtoul(cur, &pe, 0);
		if (val > 15 || val < 0) {
			printk(KERN_ERR "eisa: EISA irq value are 0-15\n");
			continue;
		}
		if (val == 2) { 
			val = 9;
		}
		eisa_make_irq_edge(val); /* clear the corresponding bit */
		EISA_DBG("setting IRQ %d to edge-triggered mode\n", val);
		
		if ((cur = strchr(cur, ','))) {
			cur++;
		} else {
			break;
		}
	}
	return 1;
}

__setup("eisa_irq_edge=", eisa_irq_setup);

