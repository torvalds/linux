/*
 * arch/arm/mach-orion5x/kurobox_pro-setup.c
 *
 * Maintainer: Ronen Shitrit <rshitrit@marvell.com>
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
#include <linux/mv643xx_eth.h>
#include <linux/i2c.h>
#include <linux/ata_platform.h>
#include <asm/mach-types.h>
#include <asm/gpio.h>
#include <asm/mach/arch.h>
#include <asm/mach/pci.h>
#include <asm/arch/orion5x.h>
#include <asm/plat-orion/orion_nand.h>
#include "common.h"

/*****************************************************************************
 * KUROBOX-PRO Info
 ****************************************************************************/

/*
 * 256K NOR flash Device bus boot chip select
 */

#define KUROBOX_PRO_NOR_BOOT_BASE	0xf4000000
#define KUROBOX_PRO_NOR_BOOT_SIZE	SZ_256K

/*
 * 256M NAND flash on Device bus chip select 1
 */

#define KUROBOX_PRO_NAND_BASE		0xfc000000
#define KUROBOX_PRO_NAND_SIZE		SZ_2M

/*****************************************************************************
 * 256MB NAND Flash on Device bus CS0
 ****************************************************************************/

static struct mtd_partition kurobox_pro_nand_parts[] = {
	{
		.name	= "uImage",
		.offset	= 0,
		.size	= SZ_4M,
	},
	{
		.name	= "rootfs",
		.offset	= SZ_4M,
		.size	= SZ_64M,
	},
	{
		.name	= "extra",
		.offset	= SZ_4M + SZ_64M,
		.size	= SZ_256M - (SZ_4M + SZ_64M),
	},
};

static struct resource kurobox_pro_nand_resource = {
	.flags		= IORESOURCE_MEM,
	.start		= KUROBOX_PRO_NAND_BASE,
	.end		= KUROBOX_PRO_NAND_BASE + KUROBOX_PRO_NAND_SIZE - 1,
};

static struct orion_nand_data kurobox_pro_nand_data = {
	.parts		= kurobox_pro_nand_parts,
	.nr_parts	= ARRAY_SIZE(kurobox_pro_nand_parts),
	.cle		= 0,
	.ale		= 1,
	.width		= 8,
};

static struct platform_device kurobox_pro_nand_flash = {
	.name		= "orion_nand",
	.id		= -1,
	.dev		= {
		.platform_data	= &kurobox_pro_nand_data,
	},
	.resource	= &kurobox_pro_nand_resource,
	.num_resources	= 1,
};

/*****************************************************************************
 * 256KB NOR Flash on BOOT Device
 ****************************************************************************/

static struct physmap_flash_data kurobox_pro_nor_flash_data = {
	.width		= 1,
};

static struct resource kurobox_pro_nor_flash_resource = {
	.flags			= IORESOURCE_MEM,
	.start			= KUROBOX_PRO_NOR_BOOT_BASE,
	.end			= KUROBOX_PRO_NOR_BOOT_BASE + KUROBOX_PRO_NOR_BOOT_SIZE - 1,
};

static struct platform_device kurobox_pro_nor_flash = {
	.name			= "physmap-flash",
	.id			= 0,
	.dev		= {
		.platform_data	= &kurobox_pro_nor_flash_data,
	},
	.num_resources		= 1,
	.resource		= &kurobox_pro_nor_flash_resource,
};

/*****************************************************************************
 * PCI
 ****************************************************************************/

static int __init kurobox_pro_pci_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	/*
	 * PCI isn't used on the Kuro
	 */
	if (dev->bus->number == orion5x_pcie_local_bus_nr())
		return IRQ_ORION5X_PCIE0_INT;
	else
		printk(KERN_ERR "kurobox_pro_pci_map_irq failed, unknown bus\n");

	return -1;
}

static struct hw_pci kurobox_pro_pci __initdata = {
	.nr_controllers	= 1,
	.swizzle	= pci_std_swizzle,
	.setup		= orion5x_pci_sys_setup,
	.scan		= orion5x_pci_sys_scan_bus,
	.map_irq	= kurobox_pro_pci_map_irq,
};

static int __init kurobox_pro_pci_init(void)
{
	if (machine_is_kurobox_pro())
		pci_common_init(&kurobox_pro_pci);

	return 0;
}

subsys_initcall(kurobox_pro_pci_init);

/*****************************************************************************
 * Ethernet
 ****************************************************************************/

static struct mv643xx_eth_platform_data kurobox_pro_eth_data = {
	.phy_addr	= 8,
	.force_phy_addr = 1,
};

/*****************************************************************************
 * RTC 5C372a on I2C bus
 ****************************************************************************/
static struct i2c_board_info __initdata kurobox_pro_i2c_rtc = {
	I2C_BOARD_INFO("rs5c372a", 0x32),
};

/*****************************************************************************
 * SATA
 ****************************************************************************/
static struct mv_sata_platform_data kurobox_pro_sata_data = {
	.n_ports        = 2,
};

/*****************************************************************************
 * General Setup
 ****************************************************************************/

static void __init kurobox_pro_init(void)
{
	/*
	 * Setup basic Orion functions. Need to be called early.
	 */
	orion5x_init();

	/*
	 * Setup the CPU address decode windows for our devices
	 */
	orion5x_setup_dev_boot_win(KUROBOX_PRO_NOR_BOOT_BASE,
				KUROBOX_PRO_NOR_BOOT_SIZE);
	orion5x_setup_dev0_win(KUROBOX_PRO_NAND_BASE, KUROBOX_PRO_NAND_SIZE);

	/*
	 * Open a special address decode windows for the PCIE WA.
	 */
	orion5x_setup_pcie_wa_win(ORION5X_PCIE_WA_PHYS_BASE,
				ORION5X_PCIE_WA_SIZE);

	/*
	 * Setup Multiplexing Pins --
	 * MPP[0-1] Not used
	 * MPP[2] GPIO Micon
	 * MPP[3] GPIO RTC
	 * MPP[4-5] Not used
	 * MPP[6] Nand Flash REn
	 * MPP[7] Nand Flash WEn
	 * MPP[8-11] Not used
	 * MPP[12] SATA 0 presence Indication
	 * MPP[13] SATA 1 presence Indication
	 * MPP[14] SATA 0 active Indication
	 * MPP[15] SATA 1 active indication
	 * MPP[16-19] Not used
	 */
	orion5x_write(MPP_0_7_CTRL, 0x44220003);
	orion5x_write(MPP_8_15_CTRL, 0x55550000);
	orion5x_write(MPP_16_19_CTRL, 0x0);

	orion5x_gpio_set_valid_pins(0x0000000c);

	platform_device_register(&kurobox_pro_nor_flash);
	if (machine_is_kurobox_pro())
		platform_device_register(&kurobox_pro_nand_flash);
	i2c_register_board_info(0, &kurobox_pro_i2c_rtc, 1);
	orion5x_eth_init(&kurobox_pro_eth_data);
	orion5x_sata_init(&kurobox_pro_sata_data);
}

#ifdef CONFIG_MACH_KUROBOX_PRO
MACHINE_START(KUROBOX_PRO, "Buffalo/Revogear Kurobox Pro")
	/* Maintainer: Ronen Shitrit <rshitrit@marvell.com> */
	.phys_io	= ORION5X_REGS_PHYS_BASE,
	.io_pg_offst	= ((ORION5X_REGS_VIRT_BASE) >> 18) & 0xFFFC,
	.boot_params	= 0x00000100,
	.init_machine	= kurobox_pro_init,
	.map_io		= orion5x_map_io,
	.init_irq	= orion5x_init_irq,
	.timer		= &orion5x_timer,
	.fixup		= tag_fixup_mem32,
MACHINE_END
#endif

#ifdef CONFIG_MACH_LINKSTATION_PRO
MACHINE_START(LINKSTATION_PRO, "Buffalo Linkstation Pro/Live")
	/* Maintainer: Byron Bradley <byron.bbradley@gmail.com> */
	.phys_io	= ORION5X_REGS_PHYS_BASE,
	.io_pg_offst	= ((ORION5X_REGS_VIRT_BASE) >> 18) & 0xFFFC,
	.boot_params	= 0x00000100,
	.init_machine	= kurobox_pro_init,
	.map_io		= orion5x_map_io,
	.init_irq	= orion5x_init_irq,
	.timer		= &orion5x_timer,
	.fixup		= tag_fixup_mem32,
MACHINE_END
#endif
