/*
 *  Macintosh IDE Driver
 *
 *     Copyright (C) 1998 by Michael Schmitz
 *
 *  This driver was written based on information obtained from the MacOS IDE
 *  driver binary by Mikael Forselius
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/delay.h>
#include <linux/ide.h>

#include <asm/machw.h>
#include <asm/macintosh.h>
#include <asm/macints.h>
#include <asm/mac_baboon.h>

#define IDE_BASE 0x50F1A000	/* Base address of IDE controller */

/*
 * Generic IDE registers as offsets from the base
 * These match MkLinux so they should be correct.
 */

#define IDE_CONTROL	0x38	/* control/altstatus */

/*
 * Mac-specific registers
 */

/*
 * this register is odd; it doesn't seem to do much and it's
 * not word-aligned like virtually every other hardware register
 * on the Mac...
 */

#define IDE_IFR		0x101	/* (0x101) IDE interrupt flags on Quadra:
				 *
				 * Bit 0+1: some interrupt flags
				 * Bit 2+3: some interrupt enable
				 * Bit 4:   ??
				 * Bit 5:   IDE interrupt flag (any hwif)
				 * Bit 6:   maybe IDE interrupt enable (any hwif) ??
				 * Bit 7:   Any interrupt condition
				 */

volatile unsigned char *ide_ifr = (unsigned char *) (IDE_BASE + IDE_IFR);

int macide_ack_intr(ide_hwif_t* hwif)
{
	if (*ide_ifr & 0x20) {
		*ide_ifr &= ~0x20;
		return 1;
	}
	return 0;
}

static void __init macide_setup_ports(hw_regs_t *hw, unsigned long base,
				      int irq, ide_ack_intr_t *ack_intr)
{
	int i;

	memset(hw, 0, sizeof(*hw));

	for (i = 0; i < 8; i++)
		hw->io_ports[i] = base + i * 4;

	hw->io_ports[IDE_CONTROL_OFFSET] = base + IDE_CONTROL;

	hw->irq = irq;
	hw->ack_intr = ack_intr;
}

static const char *mac_ide_name[] =
	{ "Quadra", "Powerbook", "Powerbook Baboon" };

/*
 * Probe for a Macintosh IDE interface
 */

static int __init macide_init(void)
{
	ide_hwif_t *hwif;
	ide_ack_intr_t *ack_intr;
	unsigned long base;
	int irq;
	hw_regs_t hw;

	switch (macintosh_config->ide_type) {
	case MAC_IDE_QUADRA:
		base = IDE_BASE;
		ack_intr = macide_ack_intr;
		irq = IRQ_NUBUS_F;
		break;
	case MAC_IDE_PB:
		base = IDE_BASE;
		ack_intr = macide_ack_intr;
		irq = IRQ_NUBUS_C;
		break;
	case MAC_IDE_BABOON:
		base = BABOON_BASE;
		ack_intr = NULL;
		irq = IRQ_BABOON_1;
		break;
	default:
		return -ENODEV;
	}

	printk(KERN_INFO "ide: Macintosh %s IDE controller\n",
			 mac_ide_name[macintosh_config->ide_type - 1]);

	macide_setup_ports(&hw, base, irq, ack_intr);

	hwif = ide_find_port(hw.io_ports[IDE_DATA_OFFSET]);
	if (hwif) {
		u8 index = hwif->index;
		u8 idx[4] = { index, 0xff, 0xff, 0xff };

		ide_init_port_data(hwif, index);
		ide_init_port_hw(hwif, &hw);

		hwif->mmio = 1;

		ide_device_add(idx, NULL);
	}

	return 0;
}

module_init(macide_init);

MODULE_LICENSE("GPL");
