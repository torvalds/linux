// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DA8XX/OMAP L1XX platform device data
 *
 * Copyright (c) 2007-2009, MontaVista Software, Inc. <source@mvista.com>
 * Derived from code that was:
 *	Copyright (C) 2006 Komal Shah <komal_shah802003@yahoo.com>
 */
#include <linux/ahci_platform.h>
#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/dma-map-ops.h>
#include <linux/dmaengine.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/serial_8250.h>

#include "common.h"
#include "cputype.h"
#include "da8xx.h"
#include "irqs.h"
#include "sram.h"

#define DA8XX_TPCC_BASE			0x01c00000
#define DA8XX_TPTC0_BASE		0x01c08000
#define DA8XX_TPTC1_BASE		0x01c08400
#define DA8XX_WDOG_BASE			0x01c21000 /* DA8XX_TIMER64P1_BASE */
#define DA8XX_I2C0_BASE			0x01c22000
#define DA8XX_RTC_BASE			0x01c23000
#define DA8XX_PRUSS_MEM_BASE		0x01c30000
#define DA8XX_MMCSD0_BASE		0x01c40000
#define DA8XX_SPI0_BASE			0x01c41000
#define DA830_SPI1_BASE			0x01e12000
#define DA8XX_LCD_CNTRL_BASE		0x01e13000
#define DA850_SATA_BASE			0x01e18000
#define DA850_MMCSD1_BASE		0x01e1b000
#define DA8XX_EMAC_CPPI_PORT_BASE	0x01e20000
#define DA8XX_EMAC_CPGMACSS_BASE	0x01e22000
#define DA8XX_EMAC_CPGMAC_BASE		0x01e23000
#define DA8XX_EMAC_MDIO_BASE		0x01e24000
#define DA8XX_I2C1_BASE			0x01e28000
#define DA850_TPCC1_BASE		0x01e30000
#define DA850_TPTC2_BASE		0x01e38000
#define DA850_SPI1_BASE			0x01f0e000
#define DA8XX_DDR2_CTL_BASE		0xb0000000

#define DA8XX_EMAC_CTRL_REG_OFFSET	0x3000
#define DA8XX_EMAC_MOD_REG_OFFSET	0x2000
#define DA8XX_EMAC_RAM_OFFSET		0x0000
#define DA8XX_EMAC_CTRL_RAM_SIZE	SZ_8K

void __iomem *da8xx_syscfg0_base;
void __iomem *da8xx_syscfg1_base;

static void __iomem *da8xx_ddr2_ctlr_base;
void __iomem * __init da8xx_get_mem_ctlr(void)
{
	if (da8xx_ddr2_ctlr_base)
		return da8xx_ddr2_ctlr_base;

	da8xx_ddr2_ctlr_base = ioremap(DA8XX_DDR2_CTL_BASE, SZ_32K);
	if (!da8xx_ddr2_ctlr_base)
		pr_warn("%s: Unable to map DDR2 controller", __func__);

	return da8xx_ddr2_ctlr_base;
}
