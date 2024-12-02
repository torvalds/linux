/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 Jonas Gorski <jonas.gorski@gmail.com>
 */

#include <linux/init.h>
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <bcm63xx_cpu.h>
#include <bcm63xx_io.h>
#include <bcm63xx_regs.h>
#include <bcm63xx_reset.h>

#define __GEN_RESET_BITS_TABLE(__cpu)					\
	[BCM63XX_RESET_SPI]		= BCM## __cpu ##_RESET_SPI,	\
	[BCM63XX_RESET_ENET]		= BCM## __cpu ##_RESET_ENET,	\
	[BCM63XX_RESET_USBH]		= BCM## __cpu ##_RESET_USBH,	\
	[BCM63XX_RESET_USBD]		= BCM## __cpu ##_RESET_USBD,	\
	[BCM63XX_RESET_DSL]		= BCM## __cpu ##_RESET_DSL,	\
	[BCM63XX_RESET_SAR]		= BCM## __cpu ##_RESET_SAR,	\
	[BCM63XX_RESET_EPHY]		= BCM## __cpu ##_RESET_EPHY,	\
	[BCM63XX_RESET_ENETSW]		= BCM## __cpu ##_RESET_ENETSW,	\
	[BCM63XX_RESET_PCM]		= BCM## __cpu ##_RESET_PCM,	\
	[BCM63XX_RESET_MPI]		= BCM## __cpu ##_RESET_MPI,	\
	[BCM63XX_RESET_PCIE]		= BCM## __cpu ##_RESET_PCIE,	\
	[BCM63XX_RESET_PCIE_EXT]	= BCM## __cpu ##_RESET_PCIE_EXT,

#define BCM3368_RESET_SPI	SOFTRESET_3368_SPI_MASK
#define BCM3368_RESET_ENET	SOFTRESET_3368_ENET_MASK
#define BCM3368_RESET_USBH	0
#define BCM3368_RESET_USBD	SOFTRESET_3368_USBS_MASK
#define BCM3368_RESET_DSL	0
#define BCM3368_RESET_SAR	0
#define BCM3368_RESET_EPHY	SOFTRESET_3368_EPHY_MASK
#define BCM3368_RESET_ENETSW	0
#define BCM3368_RESET_PCM	SOFTRESET_3368_PCM_MASK
#define BCM3368_RESET_MPI	SOFTRESET_3368_MPI_MASK
#define BCM3368_RESET_PCIE	0
#define BCM3368_RESET_PCIE_EXT	0

#define BCM6328_RESET_SPI	SOFTRESET_6328_SPI_MASK
#define BCM6328_RESET_ENET	0
#define BCM6328_RESET_USBH	SOFTRESET_6328_USBH_MASK
#define BCM6328_RESET_USBD	SOFTRESET_6328_USBS_MASK
#define BCM6328_RESET_DSL	0
#define BCM6328_RESET_SAR	SOFTRESET_6328_SAR_MASK
#define BCM6328_RESET_EPHY	SOFTRESET_6328_EPHY_MASK
#define BCM6328_RESET_ENETSW	SOFTRESET_6328_ENETSW_MASK
#define BCM6328_RESET_PCM	SOFTRESET_6328_PCM_MASK
#define BCM6328_RESET_MPI	0
#define BCM6328_RESET_PCIE	\
				(SOFTRESET_6328_PCIE_MASK |		\
				 SOFTRESET_6328_PCIE_CORE_MASK |	\
				 SOFTRESET_6328_PCIE_HARD_MASK)
#define BCM6328_RESET_PCIE_EXT	SOFTRESET_6328_PCIE_EXT_MASK

#define BCM6338_RESET_SPI	SOFTRESET_6338_SPI_MASK
#define BCM6338_RESET_ENET	SOFTRESET_6338_ENET_MASK
#define BCM6338_RESET_USBH	SOFTRESET_6338_USBH_MASK
#define BCM6338_RESET_USBD	SOFTRESET_6338_USBS_MASK
#define BCM6338_RESET_DSL	SOFTRESET_6338_ADSL_MASK
#define BCM6338_RESET_SAR	SOFTRESET_6338_SAR_MASK
#define BCM6338_RESET_EPHY	0
#define BCM6338_RESET_ENETSW	0
#define BCM6338_RESET_PCM	0
#define BCM6338_RESET_MPI	0
#define BCM6338_RESET_PCIE	0
#define BCM6338_RESET_PCIE_EXT	0

#define BCM6348_RESET_SPI	SOFTRESET_6348_SPI_MASK
#define BCM6348_RESET_ENET	SOFTRESET_6348_ENET_MASK
#define BCM6348_RESET_USBH	SOFTRESET_6348_USBH_MASK
#define BCM6348_RESET_USBD	SOFTRESET_6348_USBS_MASK
#define BCM6348_RESET_DSL	SOFTRESET_6348_ADSL_MASK
#define BCM6348_RESET_SAR	SOFTRESET_6348_SAR_MASK
#define BCM6348_RESET_EPHY	0
#define BCM6348_RESET_ENETSW	0
#define BCM6348_RESET_PCM	0
#define BCM6348_RESET_MPI	0
#define BCM6348_RESET_PCIE	0
#define BCM6348_RESET_PCIE_EXT	0

#define BCM6358_RESET_SPI	SOFTRESET_6358_SPI_MASK
#define BCM6358_RESET_ENET	SOFTRESET_6358_ENET_MASK
#define BCM6358_RESET_USBH	SOFTRESET_6358_USBH_MASK
#define BCM6358_RESET_USBD	0
#define BCM6358_RESET_DSL	SOFTRESET_6358_ADSL_MASK
#define BCM6358_RESET_SAR	SOFTRESET_6358_SAR_MASK
#define BCM6358_RESET_EPHY	SOFTRESET_6358_EPHY_MASK
#define BCM6358_RESET_ENETSW	0
#define BCM6358_RESET_PCM	SOFTRESET_6358_PCM_MASK
#define BCM6358_RESET_MPI	SOFTRESET_6358_MPI_MASK
#define BCM6358_RESET_PCIE	0
#define BCM6358_RESET_PCIE_EXT	0

#define BCM6362_RESET_SPI	SOFTRESET_6362_SPI_MASK
#define BCM6362_RESET_ENET	0
#define BCM6362_RESET_USBH	SOFTRESET_6362_USBH_MASK
#define BCM6362_RESET_USBD	SOFTRESET_6362_USBS_MASK
#define BCM6362_RESET_DSL	0
#define BCM6362_RESET_SAR	SOFTRESET_6362_SAR_MASK
#define BCM6362_RESET_EPHY	SOFTRESET_6362_EPHY_MASK
#define BCM6362_RESET_ENETSW	SOFTRESET_6362_ENETSW_MASK
#define BCM6362_RESET_PCM	SOFTRESET_6362_PCM_MASK
#define BCM6362_RESET_MPI	0
#define BCM6362_RESET_PCIE      (SOFTRESET_6362_PCIE_MASK | \
				 SOFTRESET_6362_PCIE_CORE_MASK)
#define BCM6362_RESET_PCIE_EXT	SOFTRESET_6362_PCIE_EXT_MASK

#define BCM6368_RESET_SPI	SOFTRESET_6368_SPI_MASK
#define BCM6368_RESET_ENET	0
#define BCM6368_RESET_USBH	SOFTRESET_6368_USBH_MASK
#define BCM6368_RESET_USBD	SOFTRESET_6368_USBS_MASK
#define BCM6368_RESET_DSL	0
#define BCM6368_RESET_SAR	SOFTRESET_6368_SAR_MASK
#define BCM6368_RESET_EPHY	SOFTRESET_6368_EPHY_MASK
#define BCM6368_RESET_ENETSW	SOFTRESET_6368_ENETSW_MASK
#define BCM6368_RESET_PCM	SOFTRESET_6368_PCM_MASK
#define BCM6368_RESET_MPI	SOFTRESET_6368_MPI_MASK
#define BCM6368_RESET_PCIE	0
#define BCM6368_RESET_PCIE_EXT	0

/*
 * core reset bits
 */
static const u32 bcm3368_reset_bits[] = {
	__GEN_RESET_BITS_TABLE(3368)
};

static const u32 bcm6328_reset_bits[] = {
	__GEN_RESET_BITS_TABLE(6328)
};

static const u32 bcm6338_reset_bits[] = {
	__GEN_RESET_BITS_TABLE(6338)
};

static const u32 bcm6348_reset_bits[] = {
	__GEN_RESET_BITS_TABLE(6348)
};

static const u32 bcm6358_reset_bits[] = {
	__GEN_RESET_BITS_TABLE(6358)
};

static const u32 bcm6362_reset_bits[] = {
	__GEN_RESET_BITS_TABLE(6362)
};

static const u32 bcm6368_reset_bits[] = {
	__GEN_RESET_BITS_TABLE(6368)
};

const u32 *bcm63xx_reset_bits;
static int reset_reg;

static int __init bcm63xx_reset_bits_init(void)
{
	if (BCMCPU_IS_3368()) {
		reset_reg = PERF_SOFTRESET_6358_REG;
		bcm63xx_reset_bits = bcm3368_reset_bits;
	} else if (BCMCPU_IS_6328()) {
		reset_reg = PERF_SOFTRESET_6328_REG;
		bcm63xx_reset_bits = bcm6328_reset_bits;
	} else if (BCMCPU_IS_6338()) {
		reset_reg = PERF_SOFTRESET_REG;
		bcm63xx_reset_bits = bcm6338_reset_bits;
	} else if (BCMCPU_IS_6348()) {
		reset_reg = PERF_SOFTRESET_REG;
		bcm63xx_reset_bits = bcm6348_reset_bits;
	} else if (BCMCPU_IS_6358()) {
		reset_reg = PERF_SOFTRESET_6358_REG;
		bcm63xx_reset_bits = bcm6358_reset_bits;
	} else if (BCMCPU_IS_6362()) {
		reset_reg = PERF_SOFTRESET_6362_REG;
		bcm63xx_reset_bits = bcm6362_reset_bits;
	} else if (BCMCPU_IS_6368()) {
		reset_reg = PERF_SOFTRESET_6368_REG;
		bcm63xx_reset_bits = bcm6368_reset_bits;
	}

	return 0;
}

static DEFINE_SPINLOCK(reset_mutex);

static void __bcm63xx_core_set_reset(u32 mask, int enable)
{
	unsigned long flags;
	u32 val;

	if (!mask)
		return;

	spin_lock_irqsave(&reset_mutex, flags);
	val = bcm_perf_readl(reset_reg);

	if (enable)
		val &= ~mask;
	else
		val |= mask;

	bcm_perf_writel(val, reset_reg);
	spin_unlock_irqrestore(&reset_mutex, flags);
}

void bcm63xx_core_set_reset(enum bcm63xx_core_reset core, int reset)
{
	__bcm63xx_core_set_reset(bcm63xx_reset_bits[core], reset);
}
EXPORT_SYMBOL(bcm63xx_core_set_reset);

postcore_initcall(bcm63xx_reset_bits_init);
