/*
 * arch/arm/mach-kirkwood/db88f6281-bp-setup.c
 *
 * Marvell DB-88F6281-BP Development Board Setup
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/irq.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/nand.h>
#include <linux/timer.h>
#include <linux/ata_platform.h>
#include <linux/mv643xx_eth.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/pci.h>
#include <mach/kirkwood.h>
#include "common.h"

static struct mv643xx_eth_platform_data db88f6281_ge00_data = {
	.phy_addr	= 8,
};

static struct mv_sata_platform_data db88f6281_sata_data = {
	.n_ports	= 2,
};

static void __init db88f6281_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_init();

	kirkwood_ehci_init();
	kirkwood_ge00_init(&db88f6281_ge00_data);
	kirkwood_rtc_init();
	kirkwood_sata_init(&db88f6281_sata_data);
	kirkwood_uart0_init();
	kirkwood_uart1_init();
}

static int __init db88f6281_pci_init(void)
{
	if (machine_is_db88f6281_bp())
		kirkwood_pcie_init();

	return 0;
}
subsys_initcall(db88f6281_pci_init);

MACHINE_START(DB88F6281_BP, "Marvell DB-88F6281-BP Development Board")
	/* Maintainer: Saeed Bishara <saeed@marvell.com> */
	.phys_io	= KIRKWOOD_REGS_PHYS_BASE,
	.io_pg_offst	= ((KIRKWOOD_REGS_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= 0x00000100,
	.init_machine	= db88f6281_init,
	.map_io		= kirkwood_map_io,
	.init_irq	= kirkwood_init_irq,
	.timer		= &kirkwood_timer,
MACHINE_END
