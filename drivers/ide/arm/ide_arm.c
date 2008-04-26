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
	u8 idx[4] = { 0xff, 0xff, 0xff, 0xff };

	memset(&hw, 0, sizeof(hw));
	ide_std_init_ports(&hw, IDE_ARM_IO, IDE_ARM_IO + 0x206);
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
