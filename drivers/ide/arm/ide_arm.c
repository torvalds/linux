/*
 * ARM/ARM26 default IDE host driver
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

#ifdef CONFIG_ARM26
# define IDE_ARM_HOST	(machine_is_a5k())
#else
# define IDE_ARM_HOST	(1)
#endif

#ifdef CONFIG_ARCH_CLPS7500
# include <asm/arch/hardware.h>
#
# define IDE_ARM_IO	(ISASLOT_IO + 0x1f0)
# define IDE_ARM_IRQ	IRQ_ISA_14
#else
# define IDE_ARM_IO	0x1f0
# define IDE_ARM_IRQ	IRQ_HARDDISK
#endif

void __init ide_arm_init(void)
{
	if (IDE_ARM_HOST) {
		hw_regs_t hw;

		memset(&hw, 0, sizeof(hw));
		ide_std_init_ports(&hw, IDE_ARM_IO, IDE_ARM_IO + 0x206);
		hw.irq = IDE_ARM_IRQ;
		ide_register_hw(&hw, 1, NULL);
	}
}
