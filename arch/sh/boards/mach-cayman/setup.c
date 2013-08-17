/*
 * arch/sh/mach-cayman/setup.c
 *
 * SH5 Cayman support
 *
 * Copyright (C) 2002  David J. Mckay & Benedict Gaster
 * Copyright (C) 2003 - 2007  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <cpu/irq.h>

/*
 * Platform Dependent Interrupt Priorities.
 */

/* Using defaults defined in irq.h */
#define	RES NO_PRIORITY		/* Disabled */
#define IR0 IRL0_PRIORITY	/* IRLs */
#define IR1 IRL1_PRIORITY
#define IR2 IRL2_PRIORITY
#define IR3 IRL3_PRIORITY
#define PCA INTA_PRIORITY	/* PCI Ints */
#define PCB INTB_PRIORITY
#define PCC INTC_PRIORITY
#define PCD INTD_PRIORITY
#define SER TOP_PRIORITY
#define ERR TOP_PRIORITY
#define PW0 TOP_PRIORITY
#define PW1 TOP_PRIORITY
#define PW2 TOP_PRIORITY
#define PW3 TOP_PRIORITY
#define DM0 NO_PRIORITY		/* DMA Ints */
#define DM1 NO_PRIORITY
#define DM2 NO_PRIORITY
#define DM3 NO_PRIORITY
#define DAE NO_PRIORITY
#define TU0 TIMER_PRIORITY	/* TMU Ints */
#define TU1 NO_PRIORITY
#define TU2 NO_PRIORITY
#define TI2 NO_PRIORITY
#define ATI NO_PRIORITY		/* RTC Ints */
#define PRI NO_PRIORITY
#define CUI RTC_PRIORITY
#define ERI SCIF_PRIORITY	/* SCIF Ints */
#define RXI SCIF_PRIORITY
#define BRI SCIF_PRIORITY
#define TXI SCIF_PRIORITY
#define ITI TOP_PRIORITY	/* WDT Ints */

/* Setup for the SMSC FDC37C935 */
#define SMSC_SUPERIO_BASE	0x04000000
#define SMSC_CONFIG_PORT_ADDR	0x3f0
#define SMSC_INDEX_PORT_ADDR	SMSC_CONFIG_PORT_ADDR
#define SMSC_DATA_PORT_ADDR	0x3f1

#define SMSC_ENTER_CONFIG_KEY	0x55
#define SMSC_EXIT_CONFIG_KEY	0xaa

#define SMCS_LOGICAL_DEV_INDEX	0x07
#define SMSC_DEVICE_ID_INDEX	0x20
#define SMSC_DEVICE_REV_INDEX	0x21
#define SMSC_ACTIVATE_INDEX	0x30
#define SMSC_PRIMARY_BASE_INDEX  0x60
#define SMSC_SECONDARY_BASE_INDEX 0x62
#define SMSC_PRIMARY_INT_INDEX	0x70
#define SMSC_SECONDARY_INT_INDEX 0x72

#define SMSC_IDE1_DEVICE	1
#define SMSC_KEYBOARD_DEVICE	7
#define SMSC_CONFIG_REGISTERS	8

#define SMSC_SUPERIO_READ_INDEXED(index) ({ \
	outb((index), SMSC_INDEX_PORT_ADDR); \
	inb(SMSC_DATA_PORT_ADDR); })
#define SMSC_SUPERIO_WRITE_INDEXED(val, index) ({ \
	outb((index), SMSC_INDEX_PORT_ADDR); \
	outb((val),   SMSC_DATA_PORT_ADDR); })

#define IDE1_PRIMARY_BASE	0x01f0
#define IDE1_SECONDARY_BASE	0x03f6

unsigned long smsc_superio_virt;

int platform_int_priority[NR_INTC_IRQS] = {
	IR0, IR1, IR2, IR3, PCA, PCB, PCC, PCD,	/* IRQ  0- 7 */
	RES, RES, RES, RES, SER, ERR, PW3, PW2,	/* IRQ  8-15 */
	PW1, PW0, DM0, DM1, DM2, DM3, DAE, RES,	/* IRQ 16-23 */
	RES, RES, RES, RES, RES, RES, RES, RES,	/* IRQ 24-31 */
	TU0, TU1, TU2, TI2, ATI, PRI, CUI, ERI,	/* IRQ 32-39 */
	RXI, BRI, TXI, RES, RES, RES, RES, RES,	/* IRQ 40-47 */
	RES, RES, RES, RES, RES, RES, RES, RES,	/* IRQ 48-55 */
	RES, RES, RES, RES, RES, RES, RES, ITI,	/* IRQ 56-63 */
};

static int __init smsc_superio_setup(void)
{
	unsigned char devid, devrev;

	smsc_superio_virt = (unsigned long)ioremap_nocache(SMSC_SUPERIO_BASE, 1024);
	if (!smsc_superio_virt) {
		panic("Unable to remap SMSC SuperIO\n");
	}

	/* Initially the chip is in run state */
	/* Put it into configuration state */
	outb(SMSC_ENTER_CONFIG_KEY, SMSC_CONFIG_PORT_ADDR);
	outb(SMSC_ENTER_CONFIG_KEY, SMSC_CONFIG_PORT_ADDR);

	/* Read device ID info */
	devid = SMSC_SUPERIO_READ_INDEXED(SMSC_DEVICE_ID_INDEX);
	devrev = SMSC_SUPERIO_READ_INDEXED(SMSC_DEVICE_REV_INDEX);
	printk("SMSC SuperIO devid %02x rev %02x\n", devid, devrev);

	/* Select the keyboard device */
	SMSC_SUPERIO_WRITE_INDEXED(SMSC_KEYBOARD_DEVICE, SMCS_LOGICAL_DEV_INDEX);

	/* enable it */
	SMSC_SUPERIO_WRITE_INDEXED(1, SMSC_ACTIVATE_INDEX);

	/* Select the interrupts */
	/* On a PC keyboard is IRQ1, mouse is IRQ12 */
	SMSC_SUPERIO_WRITE_INDEXED(1, SMSC_PRIMARY_INT_INDEX);
	SMSC_SUPERIO_WRITE_INDEXED(12, SMSC_SECONDARY_INT_INDEX);

#ifdef CONFIG_IDE
	/*
	 * Only IDE1 exists on the Cayman
	 */

	/* Power it on */
	SMSC_SUPERIO_WRITE_INDEXED(1 << SMSC_IDE1_DEVICE, 0x22);

	SMSC_SUPERIO_WRITE_INDEXED(SMSC_IDE1_DEVICE, SMCS_LOGICAL_DEV_INDEX);
	SMSC_SUPERIO_WRITE_INDEXED(1, SMSC_ACTIVATE_INDEX);

	SMSC_SUPERIO_WRITE_INDEXED(IDE1_PRIMARY_BASE >> 8,
				   SMSC_PRIMARY_BASE_INDEX + 0);
	SMSC_SUPERIO_WRITE_INDEXED(IDE1_PRIMARY_BASE & 0xff,
				   SMSC_PRIMARY_BASE_INDEX + 1);

	SMSC_SUPERIO_WRITE_INDEXED(IDE1_SECONDARY_BASE >> 8,
				   SMSC_SECONDARY_BASE_INDEX + 0);
	SMSC_SUPERIO_WRITE_INDEXED(IDE1_SECONDARY_BASE & 0xff,
				   SMSC_SECONDARY_BASE_INDEX + 1);

	SMSC_SUPERIO_WRITE_INDEXED(14, SMSC_PRIMARY_INT_INDEX);

	SMSC_SUPERIO_WRITE_INDEXED(SMSC_CONFIG_REGISTERS,
				   SMCS_LOGICAL_DEV_INDEX);

	SMSC_SUPERIO_WRITE_INDEXED(0x00, 0xc2); /* GP42 = nIDE1_OE */
	SMSC_SUPERIO_WRITE_INDEXED(0x01, 0xc5); /* GP45 = IDE1_IRQ */
	SMSC_SUPERIO_WRITE_INDEXED(0x00, 0xc6); /* GP46 = nIOROP */
	SMSC_SUPERIO_WRITE_INDEXED(0x00, 0xc7); /* GP47 = nIOWOP */
#endif

	/* Exit the configuration state */
	outb(SMSC_EXIT_CONFIG_KEY, SMSC_CONFIG_PORT_ADDR);

	return 0;
}
device_initcall(smsc_superio_setup);

static void __iomem *cayman_ioport_map(unsigned long port, unsigned int len)
{
	if (port < 0x400) {
		extern unsigned long smsc_superio_virt;
		return (void __iomem *)((port << 2) | smsc_superio_virt);
	}

	return (void __iomem *)port;
}

extern void init_cayman_irq(void);

static struct sh_machine_vector mv_cayman __initmv = {
	.mv_name		= "Hitachi Cayman",
	.mv_nr_irqs		= 64,
	.mv_ioport_map		= cayman_ioport_map,
	.mv_init_irq		= init_cayman_irq,
};
