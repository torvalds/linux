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
#include <linux/slab.h>
#include <linux/device.h>

#include <asm/dma.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/system.h>
#include <asm/arch/mcp.h>

#include <asm/arch/assabet.h>

#include "mcp.h"

struct mcp_sa11x0 {
	u32	mccr0;
	u32	mccr1;
};

#define priv(mcp)	((struct mcp_sa11x0 *)mcp_priv(mcp))

static void
mcp_sa11x0_set_telecom_divisor(struct mcp *mcp, unsigned int divisor)
{
	unsigned int mccr0;

	divisor /= 32;

	mccr0 = Ser4MCCR0 & ~0x00007f00;
	mccr0 |= divisor << 8;
	Ser4MCCR0 = mccr0;
}

static void
mcp_sa11x0_set_audio_divisor(struct mcp *mcp, unsigned int divisor)
{
	unsigned int mccr0;

	divisor /= 32;

	mccr0 = Ser4MCCR0 & ~0x0000007f;
	mccr0 |= divisor;
	Ser4MCCR0 = mccr0;
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

	Ser4MCDR2 = reg << 17 | MCDR2_Wr | (val & 0xffff);

	for (i = 0; i < 2; i++) {
		udelay(mcp->rw_timeout);
		if (Ser4MCSR & MCSR_CWC) {
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

	Ser4MCDR2 = reg << 17 | MCDR2_Rd;

	for (i = 0; i < 2; i++) {
		udelay(mcp->rw_timeout);
		if (Ser4MCSR & MCSR_CRC) {
			ret = Ser4MCDR2 & 0xffff;
			break;
		}
	}

	if (ret < 0)
		printk(KERN_WARNING "mcp: read timed out\n");

	return ret;
}

static void mcp_sa11x0_enable(struct mcp *mcp)
{
	Ser4MCSR = -1;
	Ser4MCCR0 |= MCCR0_MCE;
}

static void mcp_sa11x0_disable(struct mcp *mcp)
{
	Ser4MCCR0 &= ~MCCR0_MCE;
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

static int mcp_sa11x0_probe(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mcp_plat_data *data = pdev->dev.platform_data;
	struct mcp *mcp;
	int ret;

	if (!data)
		return -ENODEV;

	if (!request_mem_region(0x80060000, 0x60, "sa11x0-mcp"))
		return -EBUSY;

	mcp = mcp_host_alloc(&pdev->dev, sizeof(struct mcp_sa11x0));
	if (!mcp) {
		ret = -ENOMEM;
		goto release;
	}

	mcp->owner		= THIS_MODULE;
	mcp->ops		= &mcp_sa11x0;
	mcp->sclk_rate		= data->sclk_rate;
	mcp->dma_audio_rd	= DMA_Ser4MCP0Rd;
	mcp->dma_audio_wr	= DMA_Ser4MCP0Wr;
	mcp->dma_telco_rd	= DMA_Ser4MCP1Rd;
	mcp->dma_telco_wr	= DMA_Ser4MCP1Wr;

	dev_set_drvdata(dev, mcp);

	if (machine_is_assabet()) {
		ASSABET_BCR_set(ASSABET_BCR_CODEC_RST);
	}

	/*
	 * Setup the PPC unit correctly.
	 */
	PPDR &= ~PPC_RXD4;
	PPDR |= PPC_TXD4 | PPC_SCLK | PPC_SFRM;
	PSDR |= PPC_RXD4;
	PSDR &= ~(PPC_TXD4 | PPC_SCLK | PPC_SFRM);
	PPSR &= ~(PPC_TXD4 | PPC_SCLK | PPC_SFRM);

	/*
	 * Initialise device.  Note that we initially
	 * set the sampling rate to minimum.
	 */
	Ser4MCSR = -1;
	Ser4MCCR1 = data->mccr1;
	Ser4MCCR0 = data->mccr0 | 0x7f7f;

	/*
	 * Calculate the read/write timeout (us) from the bit clock
	 * rate.  This is the period for 3 64-bit frames.  Always
	 * round this time up.
	 */
	mcp->rw_timeout = (64 * 3 * 1000000 + mcp->sclk_rate - 1) /
			  mcp->sclk_rate;

	ret = mcp_host_register(mcp);
	if (ret == 0)
		goto out;

 release:
	release_mem_region(0x80060000, 0x60);
	dev_set_drvdata(dev, NULL);

 out:
	return ret;
}

static int mcp_sa11x0_remove(struct device *dev)
{
	struct mcp *mcp = dev_get_drvdata(dev);

	dev_set_drvdata(dev, NULL);
	mcp_host_unregister(mcp);
	release_mem_region(0x80060000, 0x60);

	return 0;
}

static int mcp_sa11x0_suspend(struct device *dev, pm_message_t state, u32 level)
{
	struct mcp *mcp = dev_get_drvdata(dev);

	if (level == SUSPEND_DISABLE) {
		priv(mcp)->mccr0 = Ser4MCCR0;
		priv(mcp)->mccr1 = Ser4MCCR1;
		Ser4MCCR0 &= ~MCCR0_MCE;
	}
	return 0;
}

static int mcp_sa11x0_resume(struct device *dev, u32 level)
{
	struct mcp *mcp = dev_get_drvdata(dev);

	if (level == RESUME_RESTORE_STATE) {
		Ser4MCCR1 = priv(mcp)->mccr1;
		Ser4MCCR0 = priv(mcp)->mccr0;
	}
	return 0;
}

/*
 * The driver for the SA11x0 MCP port.
 */
static struct device_driver mcp_sa11x0_driver = {
	.name		= "sa11x0-mcp",
	.bus		= &platform_bus_type,
	.probe		= mcp_sa11x0_probe,
	.remove		= mcp_sa11x0_remove,
	.suspend	= mcp_sa11x0_suspend,
	.resume		= mcp_sa11x0_resume,
};

/*
 * This needs re-working
 */
static int __init mcp_sa11x0_init(void)
{
	return driver_register(&mcp_sa11x0_driver);
}

static void __exit mcp_sa11x0_exit(void)
{
	driver_unregister(&mcp_sa11x0_driver);
}

module_init(mcp_sa11x0_init);
module_exit(mcp_sa11x0_exit);

MODULE_AUTHOR("Russell King <rmk@arm.linux.org.uk>");
MODULE_DESCRIPTION("SA11x0 multimedia communications port driver");
MODULE_LICENSE("GPL");
