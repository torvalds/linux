/* linux/arch/arm/mach-exynos4/dev-ahci.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4 - AHCI support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/ahci_platform.h>

#include <plat/cpu.h>

#include <mach/irqs.h>
#include <mach/map.h>
#include <mach/regs-pmu.h>

/* PHY Control Register */
#define SATA_CTRL0		0x0
/* PHY Link Control Register */
#define SATA_CTRL1		0x4
/* PHY Status Register */
#define SATA_PHY_STATUS		0x8

#define SATA_CTRL0_RX_DATA_VALID(x)	(x << 27)
#define SATA_CTRL0_SPEED_MODE		(1 << 26)
#define SATA_CTRL0_M_PHY_CAL		(1 << 19)
#define SATA_CTRL0_PHY_CMU_RST_N	(1 << 10)
#define SATA_CTRL0_M_PHY_LN_RST_N	(1 << 9)
#define SATA_CTRL0_PHY_POR_N		(1 << 8)

#define SATA_CTRL1_RST_PMALIVE_N	(1 << 8)
#define SATA_CTRL1_RST_RXOOB_N		(1 << 7)
#define SATA_CTRL1_RST_RX_N		(1 << 6)
#define SATA_CTRL1_RST_TX_N		(1 << 5)

#define SATA_PHY_STATUS_CMU_OK		(1 << 18)
#define SATA_PHY_STATUS_LANE_OK		(1 << 16)

#define LANE0		0x200
#define COM_LANE	0xA00

#define HOST_PORTS_IMPL	0xC
#define SCLK_SATA_FREQ	(67 * MHZ)

static void __iomem *phy_base, *phy_ctrl;

struct phy_reg {
	u8	reg;
	u8	val;
};

/* SATA PHY setup */
static const struct phy_reg exynos4_sataphy_cmu[] = {
	{ 0x00, 0x06 }, { 0x02, 0x80 }, { 0x22, 0xa0 }, { 0x23, 0x42 },
	{ 0x2e, 0x04 }, { 0x2f, 0x50 }, { 0x30, 0x70 }, { 0x31, 0x02 },
	{ 0x32, 0x25 }, { 0x33, 0x40 }, { 0x34, 0x01 }, { 0x35, 0x40 },
	{ 0x61, 0x2e }, { 0x63, 0x5e }, { 0x65, 0x42 }, { 0x66, 0xd1 },
	{ 0x67, 0x20 }, { 0x68, 0x28 }, { 0x69, 0x78 }, { 0x6a, 0x04 },
	{ 0x6b, 0xc8 }, { 0x6c, 0x06 },
};

static const struct phy_reg exynos4_sataphy_lane[] = {
	{ 0x00, 0x02 }, { 0x05, 0x10 }, { 0x06, 0x84 }, { 0x07, 0x04 },
	{ 0x08, 0xe0 }, { 0x10, 0x23 }, { 0x13, 0x05 }, { 0x14, 0x30 },
	{ 0x15, 0x00 }, { 0x17, 0x70 }, { 0x18, 0xf2 }, { 0x19, 0x1e },
	{ 0x1a, 0x18 }, { 0x1b, 0x0d }, { 0x1c, 0x08 }, { 0x50, 0x60 },
	{ 0x51, 0x0f },
};

static const struct phy_reg exynos4_sataphy_comlane[] = {
	{ 0x01, 0x20 }, { 0x03, 0x40 }, { 0x04, 0x3c }, { 0x05, 0x7d },
	{ 0x06, 0x1d }, { 0x07, 0xcf }, { 0x08, 0x05 }, { 0x09, 0x63 },
	{ 0x0a, 0x29 }, { 0x0b, 0xc4 }, { 0x0c, 0x01 }, { 0x0d, 0x03 },
	{ 0x0e, 0x28 }, { 0x0f, 0x98 }, { 0x10, 0x19 }, { 0x13, 0x80 },
	{ 0x14, 0xf0 }, { 0x15, 0xd0 }, { 0x39, 0xa0 }, { 0x3a, 0xa0 },
	{ 0x3b, 0xa0 }, { 0x3c, 0xa0 }, { 0x3d, 0xa0 }, { 0x3e, 0xa0 },
	{ 0x3f, 0xa0 }, { 0x40, 0x42 }, { 0x42, 0x80 }, { 0x43, 0x58 },
	{ 0x45, 0x44 }, { 0x46, 0x5c }, { 0x47, 0x86 }, { 0x48, 0x8d },
	{ 0x49, 0xd0 }, { 0x4a, 0x09 }, { 0x4b, 0x90 }, { 0x4c, 0x07 },
	{ 0x4d, 0x40 }, { 0x51, 0x20 }, { 0x52, 0x32 }, { 0x7f, 0xd8 },
	{ 0x80, 0x1a }, { 0x81, 0xff }, { 0x82, 0x11 }, { 0x83, 0x00 },
	{ 0x87, 0xf0 }, { 0x87, 0xff }, { 0x87, 0xff }, { 0x87, 0xff },
	{ 0x87, 0xff }, { 0x8c, 0x1c }, { 0x8d, 0xc2 }, { 0x8e, 0xc3 },
	{ 0x8f, 0x3f }, { 0x90, 0x0a }, { 0x96, 0xf8 },
};

static int wait_for_phy_ready(void __iomem *reg, unsigned long bit)
{
	unsigned long timeout;

	/* wait for maximum of 3 sec */
	timeout = jiffies + msecs_to_jiffies(3000);
	while (!(__raw_readl(reg) & bit)) {
		if (time_after(jiffies, timeout))
			return -1;
		cpu_relax();
	}
	return 0;
}

static int ahci_phy_init(void __iomem *mmio)
{
	int i, ctrl0;

	for (i = 0; i < ARRAY_SIZE(exynos4_sataphy_cmu); i++)
		__raw_writeb(exynos4_sataphy_cmu[i].val,
		phy_base + (exynos4_sataphy_cmu[i].reg * 4));

	for (i = 0; i < ARRAY_SIZE(exynos4_sataphy_lane); i++)
		__raw_writeb(exynos4_sataphy_lane[i].val,
		phy_base + (LANE0 + exynos4_sataphy_lane[i].reg) * 4);

	for (i = 0; i < ARRAY_SIZE(exynos4_sataphy_comlane); i++)
		__raw_writeb(exynos4_sataphy_comlane[i].val,
		phy_base + (COM_LANE + exynos4_sataphy_comlane[i].reg) * 4);

	__raw_writeb(0x07, phy_base);

	ctrl0 = __raw_readl(phy_ctrl + SATA_CTRL0);
	ctrl0 |= SATA_CTRL0_PHY_CMU_RST_N;
	__raw_writel(ctrl0, phy_ctrl + SATA_CTRL0);

	if (wait_for_phy_ready(phy_ctrl + SATA_PHY_STATUS,
				SATA_PHY_STATUS_CMU_OK) < 0) {
		printk(KERN_ERR "PHY CMU not ready\n");
		return -EBUSY;
	}

	__raw_writeb(0x03, phy_base + (COM_LANE * 4));

	ctrl0 = __raw_readl(phy_ctrl + SATA_CTRL0);
	ctrl0 |= SATA_CTRL0_M_PHY_LN_RST_N;
	__raw_writel(ctrl0, phy_ctrl + SATA_CTRL0);

	if (wait_for_phy_ready(phy_ctrl + SATA_PHY_STATUS,
				SATA_PHY_STATUS_LANE_OK) < 0) {
		printk(KERN_ERR "PHY LANE not ready\n");
		return -EBUSY;
	}

	ctrl0 = __raw_readl(phy_ctrl + SATA_CTRL0);
	ctrl0 |= SATA_CTRL0_M_PHY_CAL;
	__raw_writel(ctrl0, phy_ctrl + SATA_CTRL0);

	return 0;
}

static int exynos4_ahci_init(struct device *dev, void __iomem *mmio)
{
	struct clk *clk_sata, *clk_sataphy, *clk_sclk_sata;
	int val, ret;

	phy_base = ioremap(EXYNOS4_PA_SATAPHY, SZ_64K);
	if (!phy_base) {
		dev_err(dev, "failed to allocate memory for SATA PHY\n");
		return -ENOMEM;
	}

	phy_ctrl = ioremap(EXYNOS4_PA_SATAPHY_CTRL, SZ_16);
	if (!phy_ctrl) {
		dev_err(dev, "failed to allocate memory for SATA PHY CTRL\n");
		ret = -ENOMEM;
		goto err1;
	}

	clk_sata = clk_get(dev, "sata");
	if (IS_ERR(clk_sata)) {
		dev_err(dev, "failed to get sata clock\n");
		ret = PTR_ERR(clk_sata);
		clk_sata = NULL;
		goto err2;

	}
	clk_enable(clk_sata);

	clk_sataphy = clk_get(dev, "sataphy");
	if (IS_ERR(clk_sataphy)) {
		dev_err(dev, "failed to get sataphy clock\n");
		ret = PTR_ERR(clk_sataphy);
		clk_sataphy = NULL;
		goto err3;
	}
	clk_enable(clk_sataphy);

	clk_sclk_sata = clk_get(dev, "sclk_sata");
	if (IS_ERR(clk_sclk_sata)) {
		dev_err(dev, "failed to get sclk_sata\n");
		ret = PTR_ERR(clk_sclk_sata);
		clk_sclk_sata = NULL;
		goto err4;
	}
	clk_enable(clk_sclk_sata);
	clk_set_rate(clk_sclk_sata, SCLK_SATA_FREQ);

	__raw_writel(S5P_PMU_SATA_PHY_CONTROL_EN, S5P_PMU_SATA_PHY_CONTROL);

	/* Enable PHY link control */
	val = SATA_CTRL1_RST_PMALIVE_N | SATA_CTRL1_RST_RXOOB_N |
			SATA_CTRL1_RST_RX_N | SATA_CTRL1_RST_TX_N;
	__raw_writel(val, phy_ctrl + SATA_CTRL1);

	/* Set communication speed as 3Gbps and enable PHY power */
	val = SATA_CTRL0_RX_DATA_VALID(3) | SATA_CTRL0_SPEED_MODE |
			SATA_CTRL0_PHY_POR_N;
	__raw_writel(val, phy_ctrl + SATA_CTRL0);

	/* Port0 is available */
	__raw_writel(0x1, mmio + HOST_PORTS_IMPL);

	return ahci_phy_init(mmio);

err4:
	clk_disable(clk_sataphy);
	clk_put(clk_sataphy);
err3:
	clk_disable(clk_sata);
	clk_put(clk_sata);
err2:
	iounmap(phy_ctrl);
err1:
	iounmap(phy_base);

	return ret;
}

static struct ahci_platform_data exynos4_ahci_pdata = {
	.init = exynos4_ahci_init,
};

static struct resource exynos4_ahci_resource[] = {
	[0] = DEFINE_RES_MEM(EXYNOS4_PA_SATA, SZ_64K),
	[1] = DEFINE_RES_IRQ(EXYNOS4_IRQ_SATA),
};

static u64 exynos4_ahci_dmamask = DMA_BIT_MASK(32);

struct platform_device exynos4_device_ahci = {
	.name		= "ahci",
	.id		= -1,
	.resource	= exynos4_ahci_resource,
	.num_resources	= ARRAY_SIZE(exynos4_ahci_resource),
	.dev		= {
		.platform_data		= &exynos4_ahci_pdata,
		.dma_mask		= &exynos4_ahci_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};
