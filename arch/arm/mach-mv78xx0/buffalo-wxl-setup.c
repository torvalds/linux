// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/mach-mv78xx0/buffalo-wxl-setup.c
 *
 * Buffalo WXL (Terastation Duo) Setup routines
 *
 * sebastien requiem <sebastien@requiem.fr>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/ata_platform.h>
#include <linux/mv643xx_eth.h>
#include <linux/ethtool.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include "mv78xx0.h"
#include "common.h"
#include "mpp.h"


#define TSWXL_AUTO_SWITCH	15
#define TSWXL_USB_POWER1	30
#define TSWXL_USB_POWER2	31


/* This arch has 2 Giga Ethernet */

static struct mv643xx_eth_platform_data db78x00_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(0),
};

static struct mv643xx_eth_platform_data db78x00_ge01_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};


/* 2 SATA controller supporting HotPlug */

static struct mv_sata_platform_data db78x00_sata_data = {
	.n_ports	= 2,
};

static struct i2c_board_info __initdata db78x00_i2c_rtc = {
	I2C_BOARD_INFO("rs5c372a", 0x32),
};


static unsigned int wxl_mpp_config[] __initdata = {
	MPP0_GE1_TXCLK,
	MPP1_GE1_TXCTL,
	MPP2_GE1_RXCTL,
	MPP3_GE1_RXCLK,
	MPP4_GE1_TXD0,
	MPP5_GE1_TXD1,
	MPP6_GE1_TXD2,
	MPP7_GE1_TXD3,
	MPP8_GE1_RXD0,
	MPP9_GE1_RXD1,
	MPP10_GE1_RXD2,
	MPP11_GE1_RXD3,
	MPP12_GPIO,
	MPP13_GPIO,
	MPP14_GPIO,
	MPP15_GPIO,
	MPP16_GPIO,
	MPP17_GPIO,
	MPP18_GPIO,
	MPP19_GPIO,
	MPP20_GPIO,
	MPP21_GPIO,
	MPP22_GPIO,
	MPP23_GPIO,
	MPP24_UA2_TXD,
	MPP25_UA2_RXD,
	MPP26_UA2_CTSn,
	MPP27_UA2_RTSn,
	MPP28_GPIO,
	MPP29_GPIO,
	MPP30_GPIO,
	MPP31_GPIO,
	MPP32_GPIO,
	MPP33_GPIO,
	MPP34_GPIO,
	MPP35_GPIO,
	MPP36_GPIO,
	MPP37_GPIO,
	MPP38_GPIO,
	MPP39_GPIO,
	MPP40_GPIO,
	MPP41_GPIO,
	MPP42_GPIO,
	MPP43_GPIO,
	MPP44_GPIO,
	MPP45_GPIO,
	MPP46_GPIO,
	MPP47_GPIO,
	MPP48_GPIO,
	MPP49_GPIO,
	0
};

static struct gpio_keys_button tswxl_buttons[] = {
	{
		.code	   = KEY_OPTION,
		.gpio	   = TSWXL_AUTO_SWITCH,
		.desc	   = "Power-auto Switch",
		.active_low     = 1,
	}
};

static struct gpio_keys_platform_data tswxl_button_data = {
	.buttons	= tswxl_buttons,
	.nbuttons       = ARRAY_SIZE(tswxl_buttons),
};

static struct platform_device tswxl_button_device = {
	.name	   = "gpio-keys",
	.id	     = -1,
	.num_resources  = 0,
	.dev	    = {
		.platform_data  = &tswxl_button_data,
	},
};

static void __init wxl_init(void)
{
	/*
	 * Basic MV78xx0 setup. Needs to be called early.
	 */
	mv78xx0_init();
	mv78xx0_mpp_conf(wxl_mpp_config);

	/*
	 * Partition on-chip peripherals between the two CPU cores.
	 */
	mv78xx0_ehci0_init();
	mv78xx0_ehci1_init();
	mv78xx0_ge00_init(&db78x00_ge00_data);
	mv78xx0_ge01_init(&db78x00_ge01_data);
	mv78xx0_sata_init(&db78x00_sata_data);
	mv78xx0_uart0_init();
	mv78xx0_uart1_init();
	mv78xx0_uart2_init();
	mv78xx0_uart3_init();
	mv78xx0_xor_init();
	mv78xx0_crypto_init();
	mv78xx0_i2c_init();
	i2c_register_board_info(0, &db78x00_i2c_rtc, 1);

	//enable both usb ports
	gpio_direction_output(TSWXL_USB_POWER1, 1);
	gpio_direction_output(TSWXL_USB_POWER2, 1);

	//enable rear switch
	platform_device_register(&tswxl_button_device);
}

static int __init wxl_pci_init(void)
{
	if (machine_is_terastation_wxl() && mv78xx0_core_index() == 0)
                mv78xx0_pcie_init(1, 1);

	return 0;
}
subsys_initcall(wxl_pci_init);

MACHINE_START(TERASTATION_WXL, "Buffalo Nas WXL")
	/* Maintainer: Sebastien Requiem <sebastien@requiem.fr> */
	.atag_offset	= 0x100,
	.nr_irqs	= MV78XX0_NR_IRQS,
	.init_machine	= wxl_init,
	.map_io		= mv78xx0_map_io,
	.init_early	= mv78xx0_init_early,
	.init_irq	= mv78xx0_init_irq,
	.init_time	= mv78xx0_timer_init,
	.restart	= mv78xx0_restart,
MACHINE_END
