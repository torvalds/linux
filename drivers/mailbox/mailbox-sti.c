/*
 * STi Mailbox
 *
 * Copyright (C) 2015 ST Microelectronics
 *
 * Author: Lee Jones <lee.jones@linaro.org> for ST Microelectronics
 *
 * Based on the original driver written by;
 *   Alexandre Torgue, Olivier Lebreton and Loic Pallardy
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "mailbox.h"

#define STI_MBOX_INST_MAX	4      /* RAM saving: Max supported instances */
#define STI_MBOX_CHAN_MAX	20     /* RAM saving: Max supported channels  */

#define STI_IRQ_VAL_OFFSET	0x04   /* Read interrupt status	              */
#define STI_IRQ_SET_OFFSET	0x24   /* Generate a Tx channel interrupt     */
#define STI_IRQ_CLR_OFFSET	0x44   /* Clear pending Rx interrupts	      */
#define STI_ENA_VAL_OFFSET	0x64   /* Read enable status		      */
#define STI_ENA_SET_OFFSET	0x84   /* Enable a channel		      */
#define STI_ENA_CLR_OFFSET	0xa4   /* Disable a channel		      */

#define MBOX_BASE(mdev, inst)   ((mdev)->base + ((inst) * 4))

/**
 * STi Mailbox device data
 *
 * An IP Mailbox is currently composed of 4 instances
 * Each instance is currently composed of 32 channels
 * This means that we have 128 channels per Mailbox
 * A channel an be used for TX or RX
 *
 * @dev:	Device to which it is attached
 * @mbox:	Representation of a communication channel controller
 * @base:	Base address of the register mapping region
 * @name:	Name of the mailbox
 * @enabled:	Local copy of enabled channels
 * @lock:	Mutex protecting enabled status
 */
struct sti_mbox_device {
	struct device		*dev;
	struct mbox_controller	*mbox;
	void __iomem		*base;
	const char		*name;
	u32			enabled[STI_MBOX_INST_MAX];
	spinlock_t		lock;
};

/**
 * STi Mailbox platform specific configuration
 *
 * @num_inst:	Maximum number of instances in one HW Mailbox
 * @num_chan:	Maximum number of channel per instance
 */
struct sti_mbox_pdata {
	unsigned int		num_inst;
	unsigned int		num_chan;
};

/**
 * STi Mailbox allocated channel information
 *
 * @mdev:	Pointer to parent Mailbox device
 * @instance:	Instance number channel resides in
 * @channel:	Channel number pertaining to this container
 */
struct sti_channel {
	struct sti_mbox_device	*mdev;
	unsigned int		instance;
	unsigned int		channel;
};

static inline bool sti_mbox_channel_is_enabled(struct mbox_chan *chan)
{
	struct sti_channel *chan_info = chan->con_priv;
	struct sti_mbox_device *mdev = chan_info->mdev;
	unsigned int instance = chan_info->instance;
	unsigned int channel = chan_info->channel;

	return mdev->enabled[instance] & BIT(channel);
}

static inline
struct mbox_chan *sti_mbox_to_channel(struct mbox_controller *mbox,
				      unsigned int instance,
				      unsigned int channel)
{
	struct sti_channel *chan_info;
	int i;

	for (i = 0; i < mbox->num_chans; i++) {
		chan_info = mbox->chans[i].con_priv;
		if (chan_info &&
		    chan_info->instance == instance &&
		    chan_info->channel == channel)
			return &mbox->chans[i];
	}

	dev_err(mbox->dev,
		"Channel not registered: instance: %d channel: %d\n",
		instance, channel);

	return NULL;
}

static void sti_mbox_enable_channel(struct mbox_chan *chan)
{
	struct sti_channel *chan_info = chan->con_priv;
	struct sti_mbox_device *mdev = chan_info->mdev;
	unsigned int instance = chan_info->instance;
	unsigned int channel = chan_info->channel;
	unsigned long flags;
	void __iomem *base = MBOX_BASE(mdev, instance);

	spin_lock_irqsave(&mdev->lock, flags);
	mdev->enabled[instance] |= BIT(channel);
	writel_relaxed(BIT(channel), base + STI_ENA_SET_OFFSET);
	spin_unlock_irqrestore(&mdev->lock, flags);
}

static void sti_mbox_disable_channel(struct mbox_chan *chan)
{
	struct sti_channel *chan_info = chan->con_priv;
	struct sti_mbox_device *mdev = chan_info->mdev;
	unsigned int instance = chan_info->instance;
	unsigned int channel = chan_info->channel;
	unsigned long flags;
	void __iomem *base = MBOX_BASE(mdev, instance);

	spin_lock_irqsave(&mdev->lock, flags);
	mdev->enabled[instance] &= ~BIT(channel);
	writel_relaxed(BIT(channel), base + STI_ENA_CLR_OFFSET);
	spin_unlock_irqrestore(&mdev->lock, flags);
}

static void sti_mbox_clear_irq(struct mbox_chan *chan)
{
	struct sti_channel *chan_info = chan->con_priv;
	struct sti_mbox_device *mdev = chan_info->mdev;
	unsigned int instance = chan_info->instance;
	unsigned int channel = chan_info->channel;
	void __iomem *base = MBOX_BASE(mdev, instance);

	writel_relaxed(BIT(channel), base + STI_IRQ_CLR_OFFSET);
}

static struct mbox_chan *sti_mbox_irq_to_channel(struct sti_mbox_device *mdev,
						 unsigned int instance)
{
	struct mbox_controller *mbox = mdev->mbox;
	struct mbox_chan *chan = NULL;
	unsigned int channel;
	unsigned long bits;
	void __iomem *base = MBOX_BASE(mdev, instance);

	bits = readl_relaxed(base + STI_IRQ_VAL_OFFSET);
	if (!bits)
		/* No IRQs fired in specified instance */
		return NULL;

	/* An IRQ has fired, find the associated channel */
	for (channel = 0; bits; channel++) {
		if (!test_and_clear_bit(channel, &bits))
			continue;

		chan = sti_mbox_to_channel(mbox, instance, channel);
		if (chan) {
			dev_dbg(mbox->dev,
				"IRQ fired on instance: %d channel: %d\n",
				instance, channel);
			break;
		}
	}

	return chan;
}

static irqreturn_t sti_mbox_thread_handler(int irq, void *data)
{
	struct sti_mbox_device *mdev = data;
	struct sti_mbox_pdata *pdata = dev_get_platdata(mdev->dev);
	struct mbox_chan *chan;
	unsigned int instance;

	for (instance = 0; instance < pdata->num_inst; instance++) {
keep_looking:
		chan = sti_mbox_irq_to_channel(mdev, instance);
		if (!chan)
			continue;

		mbox_chan_received_data(chan, NULL);
		sti_mbox_clear_irq(chan);
		sti_mbox_enable_channel(chan);
		goto keep_looking;
	}

	return IRQ_HANDLED;
}

static irqreturn_t sti_mbox_irq_handler(int irq, void *data)
{
	struct sti_mbox_device *mdev = data;
	struct sti_mbox_pdata *pdata = dev_get_platdata(mdev->dev);
	struct sti_channel *chan_info;
	struct mbox_chan *chan;
	unsigned int instance;
	int ret = IRQ_NONE;

	for (instance = 0; instance < pdata->num_inst; instance++) {
		chan = sti_mbox_irq_to_channel(mdev, instance);
		if (!chan)
			continue;
		chan_info = chan->con_priv;

		if (!sti_mbox_channel_is_enabled(chan)) {
			dev_warn(mdev->dev,
				 "Unexpected IRQ: %s\n"
				 "  instance: %d: channel: %d [enabled: %x]\n",
				 mdev->name, chan_info->instance,
				 chan_info->channel, mdev->enabled[instance]);

			/* Only handle IRQ if no other valid IRQs were found */
			if (ret == IRQ_NONE)
				ret = IRQ_HANDLED;
			continue;
		}

		sti_mbox_disable_channel(chan);
		ret = IRQ_WAKE_THREAD;
	}

	if (ret == IRQ_NONE)
		dev_err(mdev->dev, "Spurious IRQ - was a channel requested?\n");

	return ret;
}

static bool sti_mbox_tx_is_ready(struct mbox_chan *chan)
{
	struct sti_channel *chan_info = chan->con_priv;
	struct sti_mbox_device *mdev = chan_info->mdev;
	unsigned int instance = chan_info->instance;
	unsigned int channel = chan_info->channel;
	void __iomem *base = MBOX_BASE(mdev, instance);

	if (!(readl_relaxed(base + STI_ENA_VAL_OFFSET) & BIT(channel))) {
		dev_dbg(mdev->dev, "Mbox: %s: inst: %d, chan: %d disabled\n",
			mdev->name, instance, channel);
		return false;
	}

	if (readl_relaxed(base + STI_IRQ_VAL_OFFSET) & BIT(channel)) {
		dev_dbg(mdev->dev, "Mbox: %s: inst: %d, chan: %d not ready\n",
			mdev->name, instance, channel);
		return false;
	}

	return true;
}

static int sti_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct sti_channel *chan_info = chan->con_priv;
	struct sti_mbox_device *mdev = chan_info->mdev;
	unsigned int instance = chan_info->instance;
	unsigned int channel = chan_info->channel;
	void __iomem *base = MBOX_BASE(mdev, instance);

	/* Send event to co-processor */
	writel_relaxed(BIT(channel), base + STI_IRQ_SET_OFFSET);

	dev_dbg(mdev->dev,
		"Sent via Mailbox %s: instance: %d channel: %d\n",
		mdev->name, instance, channel);

	return 0;
}

static int sti_mbox_startup_chan(struct mbox_chan *chan)
{
	sti_mbox_clear_irq(chan);
	sti_mbox_enable_channel(chan);

	return 0;
}

static void sti_mbox_shutdown_chan(struct mbox_chan *chan)
{
	struct sti_channel *chan_info = chan->con_priv;
	struct mbox_controller *mbox = chan_info->mdev->mbox;
	int i;

	for (i = 0; i < mbox->num_chans; i++)
		if (chan == &mbox->chans[i])
			break;

	if (mbox->num_chans == i) {
		dev_warn(mbox->dev, "Request to free non-existent channel\n");
		return;
	}

	/* Reset channel */
	sti_mbox_disable_channel(chan);
	sti_mbox_clear_irq(chan);
	chan->con_priv = NULL;
}

static struct mbox_chan *sti_mbox_xlate(struct mbox_controller *mbox,
					const struct of_phandle_args *spec)
{
	struct sti_mbox_device *mdev = dev_get_drvdata(mbox->dev);
	struct sti_mbox_pdata *pdata = dev_get_platdata(mdev->dev);
	struct sti_channel *chan_info;
	struct mbox_chan *chan = NULL;
	unsigned int instance  = spec->args[0];
	unsigned int channel   = spec->args[1];
	int i;

	/* Bounds checking */
	if (instance >= pdata->num_inst || channel  >= pdata->num_chan) {
		dev_err(mbox->dev,
			"Invalid channel requested instance: %d channel: %d\n",
			instance, channel);
		return ERR_PTR(-EINVAL);
	}

	for (i = 0; i < mbox->num_chans; i++) {
		chan_info = mbox->chans[i].con_priv;

		/* Is requested channel free? */
		if (chan_info &&
		    mbox->dev == chan_info->mdev->dev &&
		    instance == chan_info->instance &&
		    channel == chan_info->channel) {

			dev_err(mbox->dev, "Channel in use\n");
			return ERR_PTR(-EBUSY);
		}

		/*
		 * Find the first free slot, then continue checking
		 * to see if requested channel is in use
		 */
		if (!chan && !chan_info)
			chan = &mbox->chans[i];
	}

	if (!chan) {
		dev_err(mbox->dev, "No free channels left\n");
		return ERR_PTR(-EBUSY);
	}

	chan_info = devm_kzalloc(mbox->dev, sizeof(*chan_info), GFP_KERNEL);
	if (!chan_info)
		return ERR_PTR(-ENOMEM);

	chan_info->mdev		= mdev;
	chan_info->instance	= instance;
	chan_info->channel	= channel;

	chan->con_priv = chan_info;

	dev_info(mbox->dev,
		 "Mbox: %s: Created channel: instance: %d channel: %d\n",
		 mdev->name, instance, channel);

	return chan;
}

static const struct mbox_chan_ops sti_mbox_ops = {
	.startup	= sti_mbox_startup_chan,
	.shutdown	= sti_mbox_shutdown_chan,
	.send_data	= sti_mbox_send_data,
	.last_tx_done	= sti_mbox_tx_is_ready,
};

static const struct sti_mbox_pdata mbox_stih407_pdata = {
	.num_inst	= 4,
	.num_chan	= 32,
};

static const struct of_device_id sti_mailbox_match[] = {
	{
		.compatible = "st,stih407-mailbox",
		.data = (void *)&mbox_stih407_pdata
	},
	{ }
};
MODULE_DEVICE_TABLE(of, sti_mailbox_match);

static int sti_mbox_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct mbox_controller *mbox;
	struct sti_mbox_device *mdev;
	struct device_node *np = pdev->dev.of_node;
	struct mbox_chan *chans;
	struct resource *res;
	int irq;
	int ret;

	match = of_match_device(sti_mailbox_match, &pdev->dev);
	if (!match) {
		dev_err(&pdev->dev, "No configuration found\n");
		return -ENODEV;
	}
	pdev->dev.platform_data = (struct sti_mbox_pdata *) match->data;

	mdev = devm_kzalloc(&pdev->dev, sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return -ENOMEM;

	platform_set_drvdata(pdev, mdev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mdev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mdev->base))
		return PTR_ERR(mdev->base);

	ret = of_property_read_string(np, "mbox-name", &mdev->name);
	if (ret)
		mdev->name = np->full_name;

	mbox = devm_kzalloc(&pdev->dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	chans = devm_kcalloc(&pdev->dev,
			     STI_MBOX_CHAN_MAX, sizeof(*chans), GFP_KERNEL);
	if (!chans)
		return -ENOMEM;

	mdev->dev		= &pdev->dev;
	mdev->mbox		= mbox;

	spin_lock_init(&mdev->lock);

	/* STi Mailbox does not have a Tx-Done or Tx-Ready IRQ */
	mbox->txdone_irq	= false;
	mbox->txdone_poll	= true;
	mbox->txpoll_period	= 100;
	mbox->ops		= &sti_mbox_ops;
	mbox->dev		= mdev->dev;
	mbox->of_xlate		= sti_mbox_xlate;
	mbox->chans		= chans;
	mbox->num_chans		= STI_MBOX_CHAN_MAX;

	ret = devm_mbox_controller_register(&pdev->dev, mbox);
	if (ret)
		return ret;

	/* It's okay for Tx Mailboxes to not supply IRQs */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_info(&pdev->dev,
			 "%s: Registered Tx only Mailbox\n", mdev->name);
		return 0;
	}

	ret = devm_request_threaded_irq(&pdev->dev, irq,
					sti_mbox_irq_handler,
					sti_mbox_thread_handler,
					IRQF_ONESHOT, mdev->name, mdev);
	if (ret) {
		dev_err(&pdev->dev, "Can't claim IRQ %d\n", irq);
		return -EINVAL;
	}

	dev_info(&pdev->dev, "%s: Registered Tx/Rx Mailbox\n", mdev->name);

	return 0;
}

static struct platform_driver sti_mbox_driver = {
	.probe = sti_mbox_probe,
	.driver = {
		.name = "sti-mailbox",
		.of_match_table = sti_mailbox_match,
	},
};
module_platform_driver(sti_mbox_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("STMicroelectronics Mailbox Controller");
MODULE_AUTHOR("Lee Jones <lee.jones@linaro.org");
MODULE_ALIAS("platform:mailbox-sti");
