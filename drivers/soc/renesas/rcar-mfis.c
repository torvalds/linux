// SPDX-License-Identifier: GPL-2.0-only
/*
 * Renesas R-Car MFIS (Multifunctional Interface) driver
 *
 * Copyright (C) Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 * Wolfram Sang <wsa+renesas@sang-engineering.com>
 */
#include <dt-bindings/soc/renesas,r8a78000-mfis.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#define MFISWPCNTR	0x0900
#define MFISWACNTR	0x0904

#define MFIS_X5H_IICR(i) ((i) * 0x1000 + 0x00)
#define MFIS_X5H_EICR(i) ((i) * 0x1000 + 0x04)

#define MFIS_UNPROTECT_KEY 0xACCE0000

struct mfis_priv;

struct mfis_reg {
	void __iomem *base;
	resource_size_t start;
	struct mfis_priv *priv;
};

struct mfis_info {
	u32 unprotect_mask;
	unsigned int mb_num_channels;
	unsigned int mb_reg_comes_from_dt:1;
	unsigned int mb_tx_uses_eicr:1;
	unsigned int mb_channels_are_unidir:1;
	u32 (*mb_calc_reg)(u32 chan_num, bool tx_uses_eicr, bool is_only_rx);
};

struct mfis_chan_priv {
	u32 reg;
	int irq;
};

struct mfis_priv {
	spinlock_t unprotect_lock; /* guards access to the unprotection reg */
	struct device *dev;
	struct mfis_reg common_reg;
	struct mfis_reg mbox_reg;
	const struct mfis_info *info;

	/* mailbox private data */
	struct mbox_controller mbox;
	struct mfis_chan_priv *chan_privs;
};

static u32 mfis_read(struct mfis_reg *mreg, unsigned int reg)
{
	return ioread32(mreg->base + reg);
}

static void mfis_write(struct mfis_reg *mreg, u32 reg, u32 val)
{
	struct mfis_priv *priv = mreg->priv;
	u32 unprotect_mask = priv->info->unprotect_mask;
	unsigned long flags;
	u32 unprotect_code;

	/*
	 * [Gen4] key: 0xACCE0000, mask: 0x0000FFFF
	 * [Gen5] key: 0xACC00000, mask: 0x000FFFFF
	 */
	unprotect_code = (MFIS_UNPROTECT_KEY & ~unprotect_mask) |
			 ((mreg->start + reg) & unprotect_mask);

	spin_lock_irqsave(&priv->unprotect_lock, flags);
	iowrite32(unprotect_code, priv->common_reg.base + MFISWACNTR);
	iowrite32(val, mreg->base + reg);
	spin_unlock_irqrestore(&priv->unprotect_lock, flags);
}

/********************************************************
 *			Mailbox				*
 ********************************************************/

#define mfis_mb_mbox_to_priv(_m) container_of((_m), struct mfis_priv, mbox)

static irqreturn_t mfis_mb_iicr_interrupt(int irq, void *data)
{
	struct mbox_chan *chan = data;
	struct mfis_priv *priv = mfis_mb_mbox_to_priv(chan->mbox);
	struct mfis_chan_priv *chan_priv = chan->con_priv;

	mbox_chan_received_data(chan, NULL);
	/* Stop remote(!) doorbell */
	mfis_write(&priv->mbox_reg, chan_priv->reg, 0);

	return IRQ_HANDLED;
}

static int mfis_mb_startup(struct mbox_chan *chan)
{
	struct mfis_chan_priv *chan_priv = chan->con_priv;

	if (!chan_priv->irq)
		return 0;

	return request_irq(chan_priv->irq, mfis_mb_iicr_interrupt, 0,
			   dev_name(chan->mbox->dev), chan);
}

static void mfis_mb_shutdown(struct mbox_chan *chan)
{
	struct mfis_chan_priv *chan_priv = chan->con_priv;

	if (chan_priv->irq)
		free_irq(chan_priv->irq, chan);
}

static int mfis_mb_iicr_send_data(struct mbox_chan *chan, void *data)
{
	struct mfis_priv *priv = mfis_mb_mbox_to_priv(chan->mbox);
	struct mfis_chan_priv *chan_priv = chan->con_priv;

	/* Our doorbell still active? */
	if (mfis_read(&priv->mbox_reg, chan_priv->reg) & BIT(0))
		return -EBUSY;

	/* Start our doorbell */
	mfis_write(&priv->mbox_reg, chan_priv->reg, BIT(0));

	return 0;
}

static bool mfis_mb_iicr_last_tx_done(struct mbox_chan *chan)
{
	struct mfis_priv *priv = mfis_mb_mbox_to_priv(chan->mbox);
	struct mfis_chan_priv *chan_priv = chan->con_priv;

	/* Our doorbell still active? */
	return !(mfis_read(&priv->mbox_reg, chan_priv->reg) & BIT(0));
}

/* For MFIS variants using the IICR/EICR register pair */
static const struct mbox_chan_ops mfis_iicr_ops = {
	.startup = mfis_mb_startup,
	.shutdown = mfis_mb_shutdown,
	.send_data = mfis_mb_iicr_send_data,
	.last_tx_done = mfis_mb_iicr_last_tx_done,
};

static u32 mfis_mb_r8a779g0_calc_reg(u32 chan_num, bool tx_uses_eicr, bool is_only_rx)
{
	unsigned int i, k;
	u32 reg;

	i = chan_num & 3;
	k = chan_num >> 2;

	if (is_only_rx) {
		if (k < 2)
			reg = 0x9404 + 0x1020 * k + 0x08 * i;
		else
			reg = 0xb504 + 0x08 * i;
	} else {
		if (k < 2)
			reg = 0x1400 + 0x1008 * i + 0x20 * k;
		else
			reg = 0x1500 + 0x1008 * i;
	}

	return reg;
}

static u32 mfis_mb_r8a78000_calc_reg(u32 chan_num, bool tx_uses_eicr, bool is_only_rx)
{
	return (tx_uses_eicr ^ is_only_rx) ? MFIS_X5H_EICR(chan_num) :
					     MFIS_X5H_IICR(chan_num);
}

static struct mbox_chan *mfis_mb_of_xlate(struct mbox_controller *mbox,
					  const struct of_phandle_args *sp)
{
	struct mfis_priv *priv = mfis_mb_mbox_to_priv(mbox);
	struct mfis_chan_priv *chan_priv;
	bool tx_uses_eicr, is_only_rx;
	u32 chan_num, chan_flags;
	struct mbox_chan *chan;

	if (sp->args_count != 2)
		return ERR_PTR(-EINVAL);

	chan_num = sp->args[0];
	chan_flags = sp->args[1];

	if (chan_num >= priv->info->mb_num_channels)
		return ERR_PTR(-EINVAL);

	/* Channel layout is described in mfis_mb_probe() */
	if (priv->info->mb_channels_are_unidir) {
		is_only_rx = chan_flags & MFIS_CHANNEL_RX;
		chan = mbox->chans + 2 * chan_num + is_only_rx;
	} else {
		is_only_rx = false;
		chan = mbox->chans + chan_num;
	}

	if (priv->info->mb_reg_comes_from_dt) {
		tx_uses_eicr = chan_flags & MFIS_CHANNEL_EICR;
		if (tx_uses_eicr)
			chan += mbox->num_chans / 2;
	} else {
		tx_uses_eicr = priv->info->mb_tx_uses_eicr;
	}

	chan_priv = chan->con_priv;
	chan_priv->reg = priv->info->mb_calc_reg(chan_num, tx_uses_eicr, is_only_rx);

	if (!priv->info->mb_channels_are_unidir || is_only_rx) {
		char irqname[8];
		char suffix = tx_uses_eicr ? 'i' : 'e';

		/* "ch0i" or "ch0e" */
		scnprintf(irqname, sizeof(irqname), "ch%u%c", chan_num, suffix);

		chan_priv->irq = of_irq_get_byname(mbox->dev->of_node, irqname);
		if (chan_priv->irq < 0)
			return ERR_PTR(chan_priv->irq);
		if (chan_priv->irq == 0)
			return ERR_PTR(-ENOENT);
	}

	return chan;
}

static int mfis_mb_probe(struct mfis_priv *priv)
{
	unsigned int num_chan = priv->info->mb_num_channels;
	struct device *dev = priv->dev;
	struct mbox_controller *mbox;
	struct mbox_chan *chan;

	if (priv->info->mb_channels_are_unidir) {
		/* Channel layout: Ch0-TX, Ch0-RX, Ch1-TX... */
		num_chan *= 2;
	}

	if (priv->info->mb_reg_comes_from_dt) {
		/* Channel layout: <n> IICR channels, <n> EICR channels */
		num_chan *= 2;
	}

	chan  = devm_kcalloc(dev, num_chan, sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;

	priv->chan_privs = devm_kcalloc(dev, num_chan, sizeof(*priv->chan_privs),
					    GFP_KERNEL);
	if (!priv->chan_privs)
		return -ENOMEM;

	mbox = &priv->mbox;

	for (unsigned int i = 0; i < num_chan; i++)
		chan[i].con_priv = &priv->chan_privs[i];

	mbox->chans = chan;
	mbox->num_chans = num_chan;
	mbox->txdone_poll = true;
	mbox->ops = &mfis_iicr_ops;
	mbox->dev = dev;
	mbox->of_xlate = mfis_mb_of_xlate;

	return devm_mbox_controller_register(dev, mbox);
}

/********************************************************
 *			Common				*
 ********************************************************/
static int mfis_reg_probe(struct platform_device *pdev, struct mfis_priv *priv,
			  struct mfis_reg *mreg, const char *name, bool required)
{
	struct resource *res;
	void __iomem *base;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);

	/* If there is no mailbox resource, registers are in the common space */
	if (!res && !required) {
		*mreg = priv->common_reg;
	} else {
		base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(base))
			return PTR_ERR(base);

		mreg->base = base;
		mreg->start = res->start;
		mreg->priv = priv;
	}

	return 0;
}

static int mfis_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mfis_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->info = of_device_get_match_data(dev);
	if (!priv->info)
		return -ENOENT;

	spin_lock_init(&priv->unprotect_lock);

	ret = mfis_reg_probe(pdev, priv, &priv->common_reg, "common", true);
	if (ret)
		return ret;

	ret = mfis_reg_probe(pdev, priv, &priv->mbox_reg, "mboxes", false);
	if (ret)
		return ret;

	return mfis_mb_probe(priv);
}

static const struct mfis_info mfis_info_r8a779g0 = {
	.unprotect_mask	= 0x0000ffff,
	.mb_num_channels = 12,
	.mb_channels_are_unidir = true,
	.mb_calc_reg = mfis_mb_r8a779g0_calc_reg,
};

static const struct mfis_info mfis_info_r8a78000 = {
	.unprotect_mask	= 0x000fffff,
	.mb_num_channels = 64,
	.mb_reg_comes_from_dt = true,
	.mb_channels_are_unidir = true,
	.mb_calc_reg = mfis_mb_r8a78000_calc_reg,
};

static const struct mfis_info mfis_info_r8a78000_scp = {
	.unprotect_mask	= 0x000fffff,
	.mb_num_channels = 32,
	.mb_tx_uses_eicr = true,
	.mb_channels_are_unidir = true,
	.mb_calc_reg = mfis_mb_r8a78000_calc_reg,
};

static const struct of_device_id mfis_mfd_of_match[] = {
	{ .compatible = "renesas,r8a779g0-mfis", .data = &mfis_info_r8a779g0, },
	{ .compatible = "renesas,r8a779h0-mfis", .data = &mfis_info_r8a779g0, },
	{ .compatible = "renesas,r8a78000-mfis", .data = &mfis_info_r8a78000, },
	{ .compatible = "renesas,r8a78000-mfis-scp", .data = &mfis_info_r8a78000_scp, },
	{}
};
MODULE_DEVICE_TABLE(of, mfis_mfd_of_match);

static struct platform_driver mfis_driver = {
	.driver = {
		.name = "rcar-mfis",
		.of_match_table = mfis_mfd_of_match,
		.suppress_bind_attrs = true,
	},
	.probe	= mfis_probe,
};
module_platform_driver(mfis_driver);

MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
MODULE_AUTHOR("Wolfram Sang <wsa+renesas@sang-engineering.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Renesas R-Car MFIS driver");
