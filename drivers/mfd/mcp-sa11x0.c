/*
 *  linux/drivers/mfd/mcp-sa11x0.c
 *
 *  Copyright (C) 2001-2005 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 *  SA11x0 MCP (Multimedia Communications Port) driver.
 *
 *  MCP read/write timeouts from Jordi Colomer, rehacked by rmk.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/mfd/mcp.h>
#include <linux/io.h>

#include <mach/dma.h>
#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/system.h>
#include <mach/mcp.h>

/* Register offsets */
#define MCCR0	0x00
#define MCDR0	0x08
#define MCDR1	0x0C
#define MCDR2	0x10
#define MCSR	0x18
#define MCCR1	0x00

struct mcp_sa11x0 {
	u32		mccr0;
	u32		mccr1;
	unsigned char	*mccr0_base;
	unsigned char	*mccr1_base;
};

#define priv(mcp)	((struct mcp_sa11x0 *)mcp_priv(mcp))

static void
mcp_sa11x0_set_telecom_divisor(struct mcp *mcp, unsigned int divisor)
{
	struct mcp_sa11x0 *priv = priv(mcp);

	divisor /= 32;

	priv->mccr0 &= ~0x00007f00;
	priv->mccr0 |= divisor << 8;
	__raw_writel(priv->mccr0, priv->mccr0_base + MCCR0);
}

static void
mcp_sa11x0_set_audio_divisor(struct mcp *mcp, unsigned int divisor)
{
	struct mcp_sa11x0 *priv = priv(mcp);

	divisor /= 32;

	priv->mccr0 &= ~0x0000007f;
	priv->mccr0 |= divisor;
	__raw_writel(priv->mccr0, priv->mccr0_base + MCCR0);
}

/*
 * Write data to the device.  The bit should be set after 3 subframe
 * times (each frame is 64 clocks).  We wait a maximum of 6 subframes.
 * We really should try doing something more productive while we
 * wait.
 */
static void
mcp_sa11x0_write(struct mcp *mcp, unsigned int reg, unsigned int val)
{
	int ret = -ETIME;
	int i;
	u32 mcpreg;
	struct mcp_sa11x0 *priv = priv(mcp);

	mcpreg = reg << 17 | MCDR2_Wr | (val & 0xffff);
	__raw_writel(mcpreg, priv->mccr0_base + MCDR2);

	for (i = 0; i < 2; i++) {
		udelay(mcp->rw_timeout);
		mcpreg = __raw_readl(priv->mccr0_base + MCSR);
		if (mcpreg & MCSR_CWC) {
			ret = 0;
			break;
		}
	}

	if (ret < 0)
		printk(KERN_WARNING "mcp: write timed out\n");
}

/*
 * Read data from the device.  The bit should be set after 3 subframe
 * times (each frame is 64 clocks).  We wait a maximum of 6 subframes.
 * We really should try doing something more productive while we
 * wait.
 */
static unsigned int
mcp_sa11x0_read(struct mcp *mcp, unsigned int reg)
{
	int ret = -ETIME;
	int i;
	u32 mcpreg;
	struct mcp_sa11x0 *priv = priv(mcp);

	mcpreg = reg << 17 | MCDR2_Rd;
	__raw_writel(mcpreg, priv->mccr0_base + MCDR2);

	for (i = 0; i < 2; i++) {
		udelay(mcp->rw_timeout);
		mcpreg = __raw_readl(priv->mccr0_base + MCSR);
		if (mcpreg & MCSR_CRC) {
			ret = __raw_readl(priv->mccr0_base + MCDR2)
				& 0xffff;
			break;
		}
	}

	if (ret < 0)
		printk(KERN_WARNING "mcp: read timed out\n");

	return ret;
}

static void mcp_sa11x0_enable(struct mcp *mcp)
{
	struct mcp_sa11x0 *priv = priv(mcp);

	__raw_writel(-1, priv->mccr0_base + MCSR);
	priv->mccr0 |= MCCR0_MCE;
	__raw_writel(priv->mccr0, priv->mccr0_base + MCCR0);
}

static void mcp_sa11x0_disable(struct mcp *mcp)
{
	struct mcp_sa11x0 *priv = priv(mcp);

	priv->mccr0 &= ~MCCR0_MCE;
	__raw_writel(priv->mccr0, priv->mccr0_base + MCCR0);
}

/*
 * Our methods.
 */
static struct mcp_ops mcp_sa11x0 = {
	.set_telecom_divisor	= mcp_sa11x0_set_telecom_divisor,
	.set_audio_divisor	= mcp_sa11x0_set_audio_divisor,
	.reg_write		= mcp_sa11x0_write,
	.reg_read		= mcp_sa11x0_read,
	.enable			= mcp_sa11x0_enable,
	.disable		= mcp_sa11x0_disable,
};

static int mcp_sa11x0_probe(struct platform_device *pdev)
{
	struct mcp_plat_data *data = pdev->dev.platform_data;
	struct mcp *mcp;
	int ret;
	struct mcp_sa11x0 *priv;
	struct resource *res_mem0, *res_mem1;
	u32 size0, size1;

	if (!data)
		return -ENODEV;

	if (!data->codec)
		return -ENODEV;

	res_mem0 = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_mem0)
		return -ENODEV;
	size0 = res_mem0->end - res_mem0->start + 1;

	res_mem1 = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res_mem1)
		return -ENODEV;
	size1 = res_mem1->end - res_mem1->start + 1;

	if (!request_mem_region(res_mem0->start, size0, "sa11x0-mcp"))
		return -EBUSY;

	if (!request_mem_region(res_mem1->start, size1, "sa11x0-mcp")) {
		ret = -EBUSY;
		goto release;
	}

	mcp = mcp_host_alloc(&pdev->dev, sizeof(struct mcp_sa11x0));
	if (!mcp) {
		ret = -ENOMEM;
		goto release2;
	}

	priv = priv(mcp);

	mcp->owner		= THIS_MODULE;
	mcp->ops		= &mcp_sa11x0;
	mcp->sclk_rate		= data->sclk_rate;
	mcp->dma_audio_rd	= DDAR_DevAdd(res_mem0->start + MCDR0)
				+ DDAR_DevRd + DDAR_Brst4 + DDAR_8BitDev;
	mcp->dma_audio_wr	= DDAR_DevAdd(res_mem0->start + MCDR0)
				+ DDAR_DevWr + DDAR_Brst4 + DDAR_8BitDev;
	mcp->dma_telco_rd	= DDAR_DevAdd(res_mem0->start + MCDR1)
				+ DDAR_DevRd + DDAR_Brst4 + DDAR_8BitDev;
	mcp->dma_telco_wr	= DDAR_DevAdd(res_mem0->start + MCDR1)
				+ DDAR_DevWr + DDAR_Brst4 + DDAR_8BitDev;
	mcp->codec		= data->codec;

	platform_set_drvdata(pdev, mcp);

	/*
	 * Initialise device.  Note that we initially
	 * set the sampling rate to minimum.
	 */
	priv->mccr0_base = ioremap(res_mem0->start, size0);
	priv->mccr1_base = ioremap(res_mem1->start, size1);

	__raw_writel(-1, priv->mccr0_base + MCSR);
	priv->mccr1 = data->mccr1;
	priv->mccr0 = data->mccr0 | 0x7f7f;
	__raw_writel(priv->mccr0, priv->mccr0_base + MCCR0);
	__raw_writel(priv->mccr1, priv->mccr1_base + MCCR1);

	/*
	 * Calculate the read/write timeout (us) from the bit clock
	 * rate.  This is the period for 3 64-bit frames.  Always
	 * round this time up.
	 */
	mcp->rw_timeout = (64 * 3 * 1000000 + mcp->sclk_rate - 1) /
			  mcp->sclk_rate;

	ret = mcp_host_register(mcp, data->codec_pdata);
	if (ret == 0)
		goto out;

 release2:
	release_mem_region(res_mem1->start, size1);
 release:
	release_mem_region(res_mem0->start, size0);
	platform_set_drvdata(pdev, NULL);

 out:
	return ret;
}

static int mcp_sa11x0_remove(struct platform_device *pdev)
{
	struct mcp *mcp = platform_get_drvdata(pdev);
	struct mcp_sa11x0 *priv = priv(mcp);
	struct resource *res_mem;
	u32 size;

	platform_set_drvdata(pdev, NULL);
	mcp_host_unregister(mcp);

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res_mem) {
		size = res_mem->end - res_mem->start + 1;
		release_mem_region(res_mem->start, size);
	}
	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res_mem) {
		size = res_mem->end - res_mem->start + 1;
		release_mem_region(res_mem->start, size);
	}
	iounmap(priv->mccr0_base);
	iounmap(priv->mccr1_base);
	return 0;
}

static int mcp_sa11x0_suspend(struct platform_device *dev, pm_message_t state)
{
	struct mcp *mcp = platform_get_drvdata(dev);
	struct mcp_sa11x0 *priv = priv(mcp);
	u32 mccr0;

	mccr0 = priv->mccr0 & ~MCCR0_MCE;
	__raw_writel(mccr0, priv->mccr0_base + MCCR0);

	return 0;
}

static int mcp_sa11x0_resume(struct platform_device *dev)
{
	struct mcp *mcp = platform_get_drvdata(dev);
	struct mcp_sa11x0 *priv = priv(mcp);

	__raw_writel(priv->mccr0, priv->mccr0_base + MCCR0);
	__raw_writel(priv->mccr1, priv->mccr1_base + MCCR1);

	return 0;
}

/*
 * The driver for the SA11x0 MCP port.
 */
MODULE_ALIAS("platform:sa11x0-mcp");

static struct platform_driver mcp_sa11x0_driver = {
	.probe		= mcp_sa11x0_probe,
	.remove		= mcp_sa11x0_remove,
	.suspend	= mcp_sa11x0_suspend,
	.resume		= mcp_sa11x0_resume,
	.driver		= {
		.name	= "sa11x0-mcp",
		.owner  = THIS_MODULE,
	},
};

/*
 * This needs re-working
 */
module_platform_driver(mcp_sa11x0_driver);

MODULE_AUTHOR("Russell King <rmk@arm.linux.org.uk>");
MODULE_DESCRIPTION("SA11x0 multimedia communications port driver");
MODULE_LICENSE("GPL");
