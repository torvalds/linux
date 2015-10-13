/*
 *  Copyright (C) 2010,2015 Broadcom
 *  Copyright (C) 2013-2014 Lubomir Rintel
 *  Copyright (C) 2013 Craig McGeachie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This device provides a mechanism for writing to the mailboxes,
 * that are shared between the ARM and the VideoCore processor
 *
 * Parts of the driver are based on:
 *  - arch/arm/mach-bcm2708/vcio.c file written by Gray Girling that was
 *    obtained from branch "rpi-3.6.y" of git://github.com/raspberrypi/
 *    linux.git
 *  - drivers/mailbox/bcm2835-ipc.c by Lubomir Rintel at
 *    https://github.com/hackerspace/rpi-linux/blob/lr-raspberry-pi/drivers/
 *    mailbox/bcm2835-ipc.c
 *  - documentation available on the following web site:
 *    https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

/* Mailboxes */
#define ARM_0_MAIL0	0x00
#define ARM_0_MAIL1	0x20

/*
 * Mailbox registers. We basically only support mailbox 0 & 1. We
 * deliver to the VC in mailbox 1, it delivers to us in mailbox 0. See
 * BCM2835-ARM-Peripherals.pdf section 1.3 for an explanation about
 * the placement of memory barriers.
 */
#define MAIL0_RD	(ARM_0_MAIL0 + 0x00)
#define MAIL0_POL	(ARM_0_MAIL0 + 0x10)
#define MAIL0_STA	(ARM_0_MAIL0 + 0x18)
#define MAIL0_CNF	(ARM_0_MAIL0 + 0x1C)
#define MAIL1_WRT	(ARM_0_MAIL1 + 0x00)
#define MAIL1_STA	(ARM_0_MAIL1 + 0x18)

/* Status register: FIFO state. */
#define ARM_MS_FULL		BIT(31)
#define ARM_MS_EMPTY		BIT(30)

/* Configuration register: Enable interrupts. */
#define ARM_MC_IHAVEDATAIRQEN	BIT(0)

struct bcm2835_mbox {
	void __iomem *regs;
	spinlock_t lock;
	struct mbox_controller controller;
};

static struct bcm2835_mbox *bcm2835_link_mbox(struct mbox_chan *link)
{
	return container_of(link->mbox, struct bcm2835_mbox, controller);
}

static irqreturn_t bcm2835_mbox_irq(int irq, void *dev_id)
{
	struct bcm2835_mbox *mbox = dev_id;
	struct device *dev = mbox->controller.dev;
	struct mbox_chan *link = &mbox->controller.chans[0];

	while (!(readl(mbox->regs + MAIL0_STA) & ARM_MS_EMPTY)) {
		u32 msg = readl(mbox->regs + MAIL0_RD);
		dev_dbg(dev, "Reply 0x%08X\n", msg);
		mbox_chan_received_data(link, &msg);
	}
	return IRQ_HANDLED;
}

static int bcm2835_send_data(struct mbox_chan *link, void *data)
{
	struct bcm2835_mbox *mbox = bcm2835_link_mbox(link);
	u32 msg = *(u32 *)data;

	spin_lock(&mbox->lock);
	writel(msg, mbox->regs + MAIL1_WRT);
	dev_dbg(mbox->controller.dev, "Request 0x%08X\n", msg);
	spin_unlock(&mbox->lock);
	return 0;
}

static int bcm2835_startup(struct mbox_chan *link)
{
	struct bcm2835_mbox *mbox = bcm2835_link_mbox(link);

	/* Enable the interrupt on data reception */
	writel(ARM_MC_IHAVEDATAIRQEN, mbox->regs + MAIL0_CNF);

	return 0;
}

static void bcm2835_shutdown(struct mbox_chan *link)
{
	struct bcm2835_mbox *mbox = bcm2835_link_mbox(link);

	writel(0, mbox->regs + MAIL0_CNF);
}

static bool bcm2835_last_tx_done(struct mbox_chan *link)
{
	struct bcm2835_mbox *mbox = bcm2835_link_mbox(link);
	bool ret;

	spin_lock(&mbox->lock);
	ret = !(readl(mbox->regs + MAIL1_STA) & ARM_MS_FULL);
	spin_unlock(&mbox->lock);
	return ret;
}

static const struct mbox_chan_ops bcm2835_mbox_chan_ops = {
	.send_data	= bcm2835_send_data,
	.startup	= bcm2835_startup,
	.shutdown	= bcm2835_shutdown,
	.last_tx_done	= bcm2835_last_tx_done
};

static struct mbox_chan *bcm2835_mbox_index_xlate(struct mbox_controller *mbox,
		    const struct of_phandle_args *sp)
{
	if (sp->args_count != 0)
		return NULL;

	return &mbox->chans[0];
}

static int bcm2835_mbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = 0;
	struct resource *iomem;
	struct bcm2835_mbox *mbox;

	mbox = devm_kzalloc(dev, sizeof(*mbox), GFP_KERNEL);
	if (mbox == NULL)
		return -ENOMEM;
	spin_lock_init(&mbox->lock);

	ret = devm_request_irq(dev, irq_of_parse_and_map(dev->of_node, 0),
			       bcm2835_mbox_irq, 0, dev_name(dev), mbox);
	if (ret) {
		dev_err(dev, "Failed to register a mailbox IRQ handler: %d\n",
			ret);
		return -ENODEV;
	}

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mbox->regs = devm_ioremap_resource(&pdev->dev, iomem);
	if (IS_ERR(mbox->regs)) {
		ret = PTR_ERR(mbox->regs);
		dev_err(&pdev->dev, "Failed to remap mailbox regs: %d\n", ret);
		return ret;
	}

	mbox->controller.txdone_poll = true;
	mbox->controller.txpoll_period = 5;
	mbox->controller.ops = &bcm2835_mbox_chan_ops;
	mbox->controller.of_xlate = &bcm2835_mbox_index_xlate;
	mbox->controller.dev = dev;
	mbox->controller.num_chans = 1;
	mbox->controller.chans = devm_kzalloc(dev,
		sizeof(*mbox->controller.chans), GFP_KERNEL);
	if (!mbox->controller.chans)
		return -ENOMEM;

	ret = mbox_controller_register(&mbox->controller);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, mbox);
	dev_info(dev, "mailbox enabled\n");

	return ret;
}

static int bcm2835_mbox_remove(struct platform_device *pdev)
{
	struct bcm2835_mbox *mbox = platform_get_drvdata(pdev);
	mbox_controller_unregister(&mbox->controller);
	return 0;
}

static const struct of_device_id bcm2835_mbox_of_match[] = {
	{ .compatible = "brcm,bcm2835-mbox", },
	{},
};
MODULE_DEVICE_TABLE(of, bcm2835_mbox_of_match);

static struct platform_driver bcm2835_mbox_driver = {
	.driver = {
		.name = "bcm2835-mbox",
		.of_match_table = bcm2835_mbox_of_match,
	},
	.probe		= bcm2835_mbox_probe,
	.remove		= bcm2835_mbox_remove,
};
module_platform_driver(bcm2835_mbox_driver);

MODULE_AUTHOR("Lubomir Rintel <lkundrak@v3.sk>");
MODULE_DESCRIPTION("BCM2835 mailbox IPC driver");
MODULE_LICENSE("GPL v2");
