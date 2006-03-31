/*
 * arch/arm/mach-ixp23xx/espresso.c
 *
 * Double Espresso-specific routines
 *
 * Author: Lennert Buytenhek <buytenh@wantstofly.org>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/bitops.h>
#include <linux/ioport.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>
#include <linux/serial_core.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/mtd/physmap.h>

#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>

#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/pci.h>

static void __init espresso_init(void)
{
	physmap_configure(0x90000000, 0x02000000, 2, NULL);

	/*
	 * Mark flash as writeable.
	 */
	IXP23XX_EXP_CS0[0] |= IXP23XX_FLASH_WRITABLE;
	IXP23XX_EXP_CS0[1] |= IXP23XX_FLASH_WRITABLE;

	ixp23xx_sys_init();
}

MACHINE_START(ESPRESSO, "IP Fabrics Double Espresso")
	/* Maintainer: Lennert Buytenhek */
	.phys_io	= IXP23XX_PERIPHERAL_PHYS,
	.io_pg_offst	= ((IXP23XX_PERIPHERAL_VIRT >> 18)) & 0xfffc,
	.map_io		= ixp23xx_map_io,
	.init_irq	= ixp23xx_init_irq,
	.timer		= &ixp23xx_timer,
	.boot_params	= 0x00000100,
	.init_machine	= espresso_init,
MACHINE_END
