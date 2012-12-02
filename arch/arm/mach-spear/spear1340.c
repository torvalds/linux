/*
 * arch/arm/mach-spear13xx/spear1340.c
 *
 * SPEAr1340 machine source file
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <viresh.linux@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#define pr_fmt(fmt) "SPEAr1340: " fmt

#include <linux/ahci_platform.h>
#include <linux/amba/serial.h>
#include <linux/delay.h>
#include <linux/dw_dmac.h>
#include <linux/of_platform.h>
#include <linux/irqchip.h>
#include <asm/mach/arch.h>
#include <mach/generic.h>
#include <mach/spear.h>

#include "spear13xx-dma.h"

/* Base addresses */
#define SPEAR1340_SATA_BASE			UL(0xB1000000)
#define SPEAR1340_UART1_BASE			UL(0xB4100000)

/* Power Management Registers */
#define SPEAR1340_PCM_CFG			(VA_MISC_BASE + 0x100)
#define SPEAR1340_PCM_WKUP_CFG			(VA_MISC_BASE + 0x104)
#define SPEAR1340_SWITCH_CTR			(VA_MISC_BASE + 0x108)

#define SPEAR1340_PERIP1_SW_RST			(VA_MISC_BASE + 0x318)
#define SPEAR1340_PERIP2_SW_RST			(VA_MISC_BASE + 0x31C)
#define SPEAR1340_PERIP3_SW_RST			(VA_MISC_BASE + 0x320)

/* PCIE - SATA configuration registers */
#define SPEAR1340_PCIE_SATA_CFG			(VA_MISC_BASE + 0x424)
	/* PCIE CFG MASks */
	#define SPEAR1340_PCIE_CFG_DEVICE_PRESENT	(1 << 11)
	#define SPEAR1340_PCIE_CFG_POWERUP_RESET	(1 << 10)
	#define SPEAR1340_PCIE_CFG_CORE_CLK_EN		(1 << 9)
	#define SPEAR1340_PCIE_CFG_AUX_CLK_EN		(1 << 8)
	#define SPEAR1340_SATA_CFG_TX_CLK_EN		(1 << 4)
	#define SPEAR1340_SATA_CFG_RX_CLK_EN		(1 << 3)
	#define SPEAR1340_SATA_CFG_POWERUP_RESET	(1 << 2)
	#define SPEAR1340_SATA_CFG_PM_CLK_EN		(1 << 1)
	#define SPEAR1340_PCIE_SATA_SEL_PCIE		(0)
	#define SPEAR1340_PCIE_SATA_SEL_SATA		(1)
	#define SPEAR1340_SATA_PCIE_CFG_MASK		0xF1F
	#define SPEAR1340_PCIE_CFG_VAL	(SPEAR1340_PCIE_SATA_SEL_PCIE | \
			SPEAR1340_PCIE_CFG_AUX_CLK_EN | \
			SPEAR1340_PCIE_CFG_CORE_CLK_EN | \
			SPEAR1340_PCIE_CFG_POWERUP_RESET | \
			SPEAR1340_PCIE_CFG_DEVICE_PRESENT)
	#define SPEAR1340_SATA_CFG_VAL	(SPEAR1340_PCIE_SATA_SEL_SATA | \
			SPEAR1340_SATA_CFG_PM_CLK_EN | \
			SPEAR1340_SATA_CFG_POWERUP_RESET | \
			SPEAR1340_SATA_CFG_RX_CLK_EN | \
			SPEAR1340_SATA_CFG_TX_CLK_EN)

#define SPEAR1340_PCIE_MIPHY_CFG		(VA_MISC_BASE + 0x428)
	#define SPEAR1340_MIPHY_OSC_BYPASS_EXT		(1 << 31)
	#define SPEAR1340_MIPHY_CLK_REF_DIV2		(1 << 27)
	#define SPEAR1340_MIPHY_CLK_REF_DIV4		(2 << 27)
	#define SPEAR1340_MIPHY_CLK_REF_DIV8		(3 << 27)
	#define SPEAR1340_MIPHY_PLL_RATIO_TOP(x)	(x << 0)
	#define SPEAR1340_PCIE_SATA_MIPHY_CFG_SATA \
			(SPEAR1340_MIPHY_OSC_BYPASS_EXT | \
			SPEAR1340_MIPHY_CLK_REF_DIV2 | \
			SPEAR1340_MIPHY_PLL_RATIO_TOP(60))
	#define SPEAR1340_PCIE_SATA_MIPHY_CFG_SATA_25M_CRYSTAL_CLK \
			(SPEAR1340_MIPHY_PLL_RATIO_TOP(120))
	#define SPEAR1340_PCIE_SATA_MIPHY_CFG_PCIE \
			(SPEAR1340_MIPHY_OSC_BYPASS_EXT | \
			SPEAR1340_MIPHY_PLL_RATIO_TOP(25))

static struct dw_dma_slave uart1_dma_param[] = {
	{
		/* Tx */
		.cfg_hi = DWC_CFGH_DST_PER(SPEAR1340_DMA_REQ_UART1_TX),
		.cfg_lo = 0,
		.src_master = DMA_MASTER_MEMORY,
		.dst_master = SPEAR1340_DMA_MASTER_UART1,
	}, {
		/* Rx */
		.cfg_hi = DWC_CFGH_SRC_PER(SPEAR1340_DMA_REQ_UART1_RX),
		.cfg_lo = 0,
		.src_master = SPEAR1340_DMA_MASTER_UART1,
		.dst_master = DMA_MASTER_MEMORY,
	}
};

static struct amba_pl011_data uart1_data = {
	.dma_filter = dw_dma_filter,
	.dma_tx_param = &uart1_dma_param[0],
	.dma_rx_param = &uart1_dma_param[1],
};

/* SATA device registration */
static int sata_miphy_init(struct device *dev, void __iomem *addr)
{
	writel(SPEAR1340_SATA_CFG_VAL, SPEAR1340_PCIE_SATA_CFG);
	writel(SPEAR1340_PCIE_SATA_MIPHY_CFG_SATA_25M_CRYSTAL_CLK,
			SPEAR1340_PCIE_MIPHY_CFG);
	/* Switch on sata power domain */
	writel((readl(SPEAR1340_PCM_CFG) | (0x800)), SPEAR1340_PCM_CFG);
	msleep(20);
	/* Disable PCIE SATA Controller reset */
	writel((readl(SPEAR1340_PERIP1_SW_RST) & (~0x1000)),
			SPEAR1340_PERIP1_SW_RST);
	msleep(20);

	return 0;
}

void sata_miphy_exit(struct device *dev)
{
	writel(0, SPEAR1340_PCIE_SATA_CFG);
	writel(0, SPEAR1340_PCIE_MIPHY_CFG);

	/* Enable PCIE SATA Controller reset */
	writel((readl(SPEAR1340_PERIP1_SW_RST) | (0x1000)),
			SPEAR1340_PERIP1_SW_RST);
	msleep(20);
	/* Switch off sata power domain */
	writel((readl(SPEAR1340_PCM_CFG) & (~0x800)), SPEAR1340_PCM_CFG);
	msleep(20);
}

int sata_suspend(struct device *dev)
{
	if (dev->power.power_state.event == PM_EVENT_FREEZE)
		return 0;

	sata_miphy_exit(dev);

	return 0;
}

int sata_resume(struct device *dev)
{
	if (dev->power.power_state.event == PM_EVENT_THAW)
		return 0;

	return sata_miphy_init(dev, NULL);
}

static struct ahci_platform_data sata_pdata = {
	.init = sata_miphy_init,
	.exit = sata_miphy_exit,
	.suspend = sata_suspend,
	.resume = sata_resume,
};

/* Add SPEAr1340 auxdata to pass platform data */
static struct of_dev_auxdata spear1340_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("arasan,cf-spear1340", MCIF_CF_BASE, NULL, &cf_dma_priv),
	OF_DEV_AUXDATA("snps,dma-spear1340", DMAC0_BASE, NULL, &dmac_plat_data),
	OF_DEV_AUXDATA("snps,dma-spear1340", DMAC1_BASE, NULL, &dmac_plat_data),
	OF_DEV_AUXDATA("arm,pl022", SSP_BASE, NULL, &pl022_plat_data),

	OF_DEV_AUXDATA("snps,spear-ahci", SPEAR1340_SATA_BASE, NULL,
			&sata_pdata),
	OF_DEV_AUXDATA("arm,pl011", SPEAR1340_UART1_BASE, NULL, &uart1_data),
	{}
};

static void __init spear1340_dt_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table,
			spear1340_auxdata_lookup, NULL);
}

static const char * const spear1340_dt_board_compat[] = {
	"st,spear1340",
	"st,spear1340-evb",
	NULL,
};

DT_MACHINE_START(SPEAR1340_DT, "ST SPEAr1340 SoC with Flattened Device Tree")
	.smp		=	smp_ops(spear13xx_smp_ops),
	.map_io		=	spear13xx_map_io,
	.init_irq	=	irqchip_init,
	.init_time	=	spear13xx_timer_init,
	.init_machine	=	spear1340_dt_init,
	.restart	=	spear_restart,
	.dt_compat	=	spear1340_dt_board_compat,
MACHINE_END
