/*
 *
 * QNAP TS-410, TS-410U, TS-419P and TS-419U Turbo NAS Board Setup
 *
 * Copyright (C) 2009-2010  Martin Michlmayr <tbm@cyrius.com>
 * Copyright (C) 2008  Byron Bradley <byron.bbradley@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/mv643xx_eth.h>
#include <linux/ata_platform.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/io.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/kirkwood.h>
#include "common.h"
#include "mpp.h"
#include "tsx1x-common.h"

/* for the PCIe reset workaround */
#include <plat/pcie.h>


#define QNAP_TS41X_JUMPER_JP1	45

static struct i2c_board_info __initdata qnap_ts41x_i2c_rtc = {
	I2C_BOARD_INFO("s35390a", 0x30),
};

static struct mv643xx_eth_platform_data qnap_ts41x_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

static struct mv643xx_eth_platform_data qnap_ts41x_ge01_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(0),
};

static struct mv_sata_platform_data qnap_ts41x_sata_data = {
	.n_ports	= 2,
};

static struct gpio_keys_button qnap_ts41x_buttons[] = {
	{
		.code		= KEY_COPY,
		.gpio		= 43,
		.desc		= "USB Copy",
		.active_low	= 1,
	},
	{
		.code		= KEY_RESTART,
		.gpio		= 37,
		.desc		= "Reset",
		.active_low	= 1,
	},
};

static struct gpio_keys_platform_data qnap_ts41x_button_data = {
	.buttons	= qnap_ts41x_buttons,
	.nbuttons	= ARRAY_SIZE(qnap_ts41x_buttons),
};

static struct platform_device qnap_ts41x_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &qnap_ts41x_button_data,
	}
};

static unsigned int qnap_ts41x_mpp_config[] __initdata = {
	MPP0_SPI_SCn,
	MPP1_SPI_MOSI,
	MPP2_SPI_SCK,
	MPP3_SPI_MISO,
	MPP6_SYSRST_OUTn,
	MPP7_PEX_RST_OUTn,
	MPP8_TW0_SDA,
	MPP9_TW0_SCK,
	MPP10_UART0_TXD,
	MPP11_UART0_RXD,
	MPP13_UART1_TXD,	/* PIC controller */
	MPP14_UART1_RXD,	/* PIC controller */
	MPP15_SATA0_ACTn,
	MPP16_SATA1_ACTn,
	MPP20_GE1_TXD0,
	MPP21_GE1_TXD1,
	MPP22_GE1_TXD2,
	MPP23_GE1_TXD3,
	MPP24_GE1_RXD0,
	MPP25_GE1_RXD1,
	MPP26_GE1_RXD2,
	MPP27_GE1_RXD3,
	MPP30_GE1_RXCTL,
	MPP31_GE1_RXCLK,
	MPP32_GE1_TCLKOUT,
	MPP33_GE1_TXCTL,
	MPP36_GPIO,		/* RAM: 0: 256 MB, 1: 512 MB */
	MPP37_GPIO,		/* Reset button */
	MPP43_GPIO,		/* USB Copy button */
	MPP44_GPIO,		/* Board ID: 0: TS-419U, 1: TS-419 */
	MPP45_GPIO,		/* JP1: 0: LCD, 1: serial console */
	MPP46_GPIO,		/* External SATA HDD1 error indicator */
	MPP47_GPIO,		/* External SATA HDD2 error indicator */
	MPP48_GPIO,		/* External SATA HDD3 error indicator */
	MPP49_GPIO,		/* External SATA HDD4 error indicator */
	0
};

static void __init qnap_ts41x_init(void)
{
	u32 dev, rev;

	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_init();
	kirkwood_mpp_conf(qnap_ts41x_mpp_config);

	kirkwood_uart0_init();
	kirkwood_uart1_init(); /* A PIC controller is connected here. */
	qnap_tsx1x_register_flash();
	kirkwood_i2c_init();
	i2c_register_board_info(0, &qnap_ts41x_i2c_rtc, 1);

	kirkwood_pcie_id(&dev, &rev);
	if (dev == MV88F6282_DEV_ID) {
		qnap_ts41x_ge00_data.phy_addr = MV643XX_ETH_PHY_ADDR(0);
		qnap_ts41x_ge01_data.phy_addr = MV643XX_ETH_PHY_ADDR(1);
	}
	kirkwood_ge00_init(&qnap_ts41x_ge00_data);
	kirkwood_ge01_init(&qnap_ts41x_ge01_data);

	kirkwood_sata_init(&qnap_ts41x_sata_data);
	kirkwood_ehci_init();
	platform_device_register(&qnap_ts41x_button_device);

	pm_power_off = qnap_tsx1x_power_off;

	if (gpio_request(QNAP_TS41X_JUMPER_JP1, "JP1") == 0)
		gpio_export(QNAP_TS41X_JUMPER_JP1, 0);
}

static int __init ts41x_pci_init(void)
{
	if (machine_is_ts41x()) {
		u32 dev, rev;

		/*
		 * Without this explicit reset, the PCIe SATA controller
		 * (Marvell 88sx7042/sata_mv) is known to stop working
		 * after a few minutes.
		 */
		orion_pcie_reset(PCIE_VIRT_BASE);

		kirkwood_pcie_id(&dev, &rev);
		if (dev == MV88F6282_DEV_ID)
			kirkwood_pcie_init(KW_PCIE1 | KW_PCIE0);
		else
			kirkwood_pcie_init(KW_PCIE0);
	}

   return 0;
}
subsys_initcall(ts41x_pci_init);

MACHINE_START(TS41X, "QNAP TS-41x")
	/* Maintainer: Martin Michlmayr <tbm@cyrius.com> */
	.atag_offset	= 0x100,
	.init_machine	= qnap_ts41x_init,
	.map_io		= kirkwood_map_io,
	.init_early	= kirkwood_init_early,
	.init_irq	= kirkwood_init_irq,
	.timer		= &kirkwood_timer,
	.restart	= kirkwood_restart,
MACHINE_END
