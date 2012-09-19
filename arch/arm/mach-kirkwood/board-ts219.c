/*
 *
 * QNAP TS-11x/TS-21x Turbo NAS Board Setup via DT
 *
 * Copyright (C) 2012 Andrew Lunn <andrew@lunn.ch>
 *
 * Based on the board file ts219-setup.c:
 *
 * Copyright (C) 2009  Martin Michlmayr <tbm@cyrius.com>
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
#include <linux/mv643xx_eth.h>
#include <linux/ata_platform.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/kirkwood.h>
#include "common.h"
#include "mpp.h"
#include "tsx1x-common.h"

static struct mv643xx_eth_platform_data qnap_ts219_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

static unsigned int qnap_ts219_mpp_config[] __initdata = {
	MPP0_SPI_SCn,
	MPP1_SPI_MOSI,
	MPP2_SPI_SCK,
	MPP3_SPI_MISO,
	MPP4_SATA1_ACTn,
	MPP5_SATA0_ACTn,
	MPP8_TW0_SDA,
	MPP9_TW0_SCK,
	MPP10_UART0_TXD,
	MPP11_UART0_RXD,
	MPP13_UART1_TXD,	/* PIC controller */
	MPP14_UART1_RXD,	/* PIC controller */
	MPP15_GPIO,		/* USB Copy button (on devices with 88F6281) */
	MPP16_GPIO,		/* Reset button (on devices with 88F6281) */
	MPP36_GPIO,		/* RAM: 0: 256 MB, 1: 512 MB */
	MPP37_GPIO,		/* Reset button (on devices with 88F6282) */
	MPP43_GPIO,		/* USB Copy button (on devices with 88F6282) */
	MPP44_GPIO,		/* Board ID: 0: TS-11x, 1: TS-21x */
	0
};

void __init qnap_dt_ts219_init(void)
{
	u32 dev, rev;

	kirkwood_mpp_conf(qnap_ts219_mpp_config);

	kirkwood_pcie_id(&dev, &rev);
	if (dev == MV88F6282_DEV_ID)
		qnap_ts219_ge00_data.phy_addr = MV643XX_ETH_PHY_ADDR(0);

	kirkwood_ge00_init(&qnap_ts219_ge00_data);
	kirkwood_ehci_init();

	pm_power_off = qnap_tsx1x_power_off;
}

/* FIXME: Will not work with DT. Maybe use MPP40_GPIO? */
static int __init ts219_pci_init(void)
{
	if (machine_is_ts219())
		kirkwood_pcie_init(KW_PCIE0);

	return 0;
}
subsys_initcall(ts219_pci_init);
