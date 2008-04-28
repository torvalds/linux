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

#include <asm/mach-types.h>
#include <asm/irq.h>

#define DRV_NAME "ide_arm"

#ifdef CONFIG_ARCH_CLPS7500
# include <asm/arch/hardware.h>
#
# define IDE_ARM_IO	(ISASLOT_IO + 0x1f0)
# define IDE_ARM_IRQ	IRQ_ISA_14
#else
# define IDE_ARM_IO	0x1f0
# define IDE_ARM_IRQ	IRQ_HARDDISK
#endif

static int __init ide_arm_init(void)
{
	ide_hwif_t *hwif;
	hw_regs_t hw;
	unsigned long base = IDE_ARM_IO, ctl = IDE_ARM_IO + 0x206;
	u8 idx[4] = { 0xff, 0xff, 0xff, 0xff };

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

	hwif = ide_find_port();
	if (hwif) {
		ide_init_port_hw(hwif, &hw);
		idx[0] = hwif->index;

		ide_device_add(idx, NULL);
	}

	return 0;
}

module_init(ide_arm_init);

MODULE_LICENSE("GPL");
