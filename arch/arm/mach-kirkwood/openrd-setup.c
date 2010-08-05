/*
 * arch/arm/mach-kirkwood/openrd-setup.c
 *
 * Marvell OpenRD (Base|Client) Board Setup
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/ata_platform.h>
#include <linux/mv643xx_eth.h>
#include <linux/i2c.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/kirkwood.h>
#include <plat/mvsdio.h>
#include "common.h"
#include "mpp.h"

static struct mtd_partition openrd_nand_parts[] = {
	{
		.name		= "u-boot",
		.offset		= 0,
		.size		= SZ_1M,
		.mask_flags	= MTD_WRITEABLE
	}, {
		.name		= "uImage",
		.offset		= MTDPART_OFS_NXTBLK,
		.size		= SZ_4M
	}, {
		.name		= "root",
		.offset		= MTDPART_OFS_NXTBLK,
		.size		= MTDPART_SIZ_FULL
	},
};

static struct mv643xx_eth_platform_data openrd_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

static struct mv643xx_eth_platform_data openrd_ge01_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(24),
};

static struct mv_sata_platform_data openrd_sata_data = {
	.n_ports	= 2,
};

static struct mvsdio_platform_data openrd_mvsdio_data = {
	.gpio_card_detect = 29,	/* MPP29 used as SD card detect */
};

static unsigned int openrd_mpp_config[] __initdata = {
	MPP29_GPIO,
	0
};

static struct i2c_board_info i2c_board_info[] __initdata = {
	{
		I2C_BOARD_INFO("cs42l51", 0x4a),
	},
};

static void __init openrd_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_init();
	kirkwood_mpp_conf(openrd_mpp_config);

	kirkwood_uart0_init();
	kirkwood_nand_init(ARRAY_AND_SIZE(openrd_nand_parts), 25);

	kirkwood_ehci_init();

	kirkwood_ge00_init(&openrd_ge00_data);
	if (machine_is_openrd_client())
		kirkwood_ge01_init(&openrd_ge01_data);
	kirkwood_sata_init(&openrd_sata_data);
	kirkwood_sdio_init(&openrd_mvsdio_data);

	kirkwood_i2c_init();

	if (machine_is_openrd_client()) {
		i2c_register_board_info(0, i2c_board_info,
			ARRAY_SIZE(i2c_board_info));
		kirkwood_audio_init();
	}
}

static int __init openrd_pci_init(void)
{
	if (machine_is_openrd_base() || machine_is_openrd_client())
		kirkwood_pcie_init();

	return 0;
}
subsys_initcall(openrd_pci_init);

#ifdef CONFIG_MACH_OPENRD_BASE
MACHINE_START(OPENRD_BASE, "Marvell OpenRD Base Board")
	/* Maintainer: Dhaval Vasa <dhaval.vasa@einfochips.com> */
	.phys_io	= KIRKWOOD_REGS_PHYS_BASE,
	.io_pg_offst	= ((KIRKWOOD_REGS_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= 0x00000100,
	.init_machine	= openrd_init,
	.map_io		= kirkwood_map_io,
	.init_irq	= kirkwood_init_irq,
	.timer		= &kirkwood_timer,
MACHINE_END
#endif

#ifdef CONFIG_MACH_OPENRD_CLIENT
MACHINE_START(OPENRD_CLIENT, "Marvell OpenRD Client Board")
	/* Maintainer: Dhaval Vasa <dhaval.vasa@einfochips.com> */
	.phys_io	= KIRKWOOD_REGS_PHYS_BASE,
	.io_pg_offst	= ((KIRKWOOD_REGS_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= 0x00000100,
	.init_machine	= openrd_init,
	.map_io		= kirkwood_map_io,
	.init_irq	= kirkwood_init_irq,
	.timer		= &kirkwood_timer,
MACHINE_END
#endif
