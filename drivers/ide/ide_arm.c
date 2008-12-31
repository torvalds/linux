/*
 * ARM default IDE host driver
 *
 * Copyright (C) 2004 Bartlomiej Zolnierkiewicz
 * Based on code by: Russell King, Ian Molton and Alexander Schulz.
 *
 * May be copied or modified under the terms of the GNU General Public License.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ide.h>

#include <asm/irq.h>

#define DRV_NAME "ide_arm"

#define IDE_ARM_IO	0x1f0
#define IDE_ARM_IRQ	IRQ_HARDDISK

static int __init ide_arm_init(void)
{
	unsigned long base = IDE_ARM_IO, ctl = IDE_ARM_IO + 0x206;
	hw_regs_t hw, *hws[] = { &hw, NULL, NULL, NULL };

	if (!request_region(base, 8, DRV_NAME)) {
		printk(KERN_ERR "%s: I/O resource 0x%lX-0x%lX not free.\n",
				DRV_NAME, base, base + 7);
		return -EBUSY;
	}

	if (!request_region(ctl, 1, DRV_NAME)) {
		printk(KERN_ERR "%s: I/O resource 0x%lX not free.\n",
				DRV_NAME, ctl);
		release_region(base, 8);
		return -EBUSY;
	}

	memset(&hw, 0, sizeof(hw));
	ide_std_init_ports(&hw, base, ctl);
	hw.irq = IDE_ARM_IRQ;
	hw.chipset = ide_generic;

	return ide_host_add(NULL, hws, NULL);
}

module_init(ide_arm_init);

MODULE_LICENSE("GPL");
