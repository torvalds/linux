/*
 * Copyright (C) 2012 MPL AG, Switzerland
 * Stefan Peter <s.peter@mpl.ch>
 *
 * arch/arm/mach-kirkwood/board-mplcec4.c
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mv643xx_eth.h>
#include <linux/platform_data/mmc-mvsdio.h>
#include "common.h"
#include "mpp.h"

static struct mv643xx_eth_platform_data mplcec4_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(1),
};

static struct mv643xx_eth_platform_data mplcec4_ge01_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(2),
};

static unsigned int mplcec4_mpp_config[] __initdata = {
	MPP0_NF_IO2,
	MPP1_NF_IO3,
	MPP2_NF_IO4,
	MPP3_NF_IO5,
	MPP4_NF_IO6,
	MPP5_NF_IO7,
	MPP6_SYSRST_OUTn,
	MPP7_GPO,	/* Status LED Green High Active */
	MPP10_UART0_TXD,
	MPP11_UART0_RXD,
	MPP12_SD_CLK,
	MPP13_SD_CMD,	/* Alt UART1_TXD */
	MPP14_SD_D0,	/* Alt UART1_RXD */
	MPP15_SD_D1,
	MPP16_SD_D2,
	MPP17_SD_D3,
	MPP18_NF_IO0,
	MPP19_NF_IO1,
	MPP28_GPIO,	/* Input SYS_POR_DET (active High) */
	MPP29_GPIO,	/* Input SYS_RTC_INT (active High) */
	MPP34_SATA1_ACTn,
	MPP35_SATA0_ACTn,
	MPP40_GPIO,	/* LED User1 orange */
	MPP41_GPIO,	/* LED User1 green */
	MPP44_GPIO,	/* LED User0 orange */
	MPP45_GPIO,	/* LED User0 green */
	MPP46_GPIO,	/* Status LED Yellow High Active */
	MPP47_GPIO,	/* SD_CD# (in/IRQ)*/
	0
};


static struct mvsdio_platform_data mplcec4_mvsdio_data = {
	.gpio_card_detect = 47,	/* MPP47 used as SD card detect */
};



void __init mplcec4_init(void)
{
	/*
	 * Basic setup. Needs to be called early.
	 */
	kirkwood_mpp_conf(mplcec4_mpp_config);
	kirkwood_ehci_init();
	kirkwood_ge00_init(&mplcec4_ge00_data);
	kirkwood_ge01_init(&mplcec4_ge01_data);
	kirkwood_sdio_init(&mplcec4_mvsdio_data);
	kirkwood_pcie_init(KW_PCIE0);
}



