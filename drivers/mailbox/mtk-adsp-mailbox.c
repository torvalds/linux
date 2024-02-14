// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Corporation. All rights reserved.
 * Author: Allen-KH Cheng <allen-kh.cheng@mediatek.com>
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

struct mtk_adsp_mbox_priv {
	struct device *dev;
	struct mbox_controller mbox;
	void __iomem *va_mboxreg;
	const struct mtk_adsp_mbox_cfg *cfg;
};

struct mtk_adsp_mbox_cfg {
	u32 set_in;
	u32 set_out;
	u32 clr_in;
	u32 clr_out;
};

static inline struct mtk_adsp_mbox_priv *get_mtk_adsp_mbox_priv(struct mbox_controller *mbox)
{
	return container_of(mbox, struct mtk_adsp_mbox_priv, mbox);
}

static irqreturn_t mtk_adsp_mbox_irq(int irq, void *data)
{
	struct mbox_chan *chan = data;
	struct mtk_adsp_mbox_priv *priv = get_mtk_adsp_mbox_priv(chan->mbox);
	u32 op = readl(priv->va_mboxreg + priv->cfg->set_out);

	writel(op, priv->va_mboxreg + priv->cfg->clr_out);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t mtk_adsp_mbox_isr(int irq, void *data)
{
	struct mbox_chan *chan = data;

	mbox_chan_received_data(chan, NULL);

	return IRQ_HANDLED;
}

static struct mbox_chan *mtk_adsp_mbox_xlate(struct mbox_controller *mbox,
					     const struct of_phandle_args *sp)
{
	return mbox->chans;
}

static int mtk_adsp_mbox_startup(struct mbox_chan *chan)
{
	struct mtk_adsp_mbox_priv *priv = get_mtk_adsp_mbox_priv(chan->mbox);

	/* Clear ADSP mbox command */
	writel(0xFFFFFFFF, priv->va_mboxreg + priv->cfg->clr_in);
	writel(0xFFFFFFFF, priv->va_mboxreg + priv->cfg->clr_out);

	return 0;
}

static void mtk_adsp_mbox_shutdown(struct mbox_chan *chan)
{
	struct mtk_adsp_mbox_priv *priv = get_mtk_adsp_mbox_priv(chan->mbox);

	/* Clear ADSP mbox command */
	writel(0xFFFFFFFF, priv->va_mboxreg + priv->cfg->clr_in);
	writel(0xFFFFFFFF, priv->va_mboxreg + priv->cfg->clr_out);
}

static int mtk_adsp_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct mtk_adsp_mbox_priv *priv = get_mtk_adsp_mbox_priv(chan->mbox);
	u32 *msg = data;

	writel(*msg, priv->va_mboxreg + priv->cfg->set_in);

	return 0;
}

static bool mtk_adsp_mbox_last_tx_done(struct mbox_chan *chan)
{
	struct mtk_adsp_mbox_priv *priv = get_mtk_adsp_mbox_priv(chan->mbox);

	return readl(priv->va_mboxreg + priv->cfg->set_in) == 0;
}

static const struct mbox_chan_ops mtk_adsp_mbox_chan_ops = {
	.send_data	= mtk_adsp_mbox_send_data,
	.startup	= mtk_adsp_mbox_startup,
	.shutdown	= mtk_adsp_mbox_shutdown,
	.last_tx_done	= mtk_adsp_mbox_last_tx_done,
};

static int mtk_adsp_mbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_adsp_mbox_priv *priv;
	const struct mtk_adsp_mbox_cfg *cfg;
	struct mbox_controller *mbox;
	int ret, irq;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mbox = &priv->mbox;
	mbox->dev = dev;
	mbox->ops = &mtk_adsp_mbox_chan_ops;
	mbox->txdone_irq = false;
	mbox->txdone_poll = true;
	mbox->of_xlate = mtk_adsp_mbox_xlate;
	mbox->num_chans = 1;
	mbox->chans = devm_kzalloc(dev, sizeof(*mbox->chans), GFP_KERNEL);
	if (!mbox->chans)
		return -ENOMEM;

	priv->va_mboxreg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->va_mboxreg))
		return PTR_ERR(priv->va_mboxreg);

	cfg = of_device_get_match_data(dev);
	if (!cfg)
		return -EINVAL;
	priv->cfg = cfg;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(dev, irq, mtk_adsp_mbox_irq,
					mtk_adsp_mbox_isr, IRQF_TRIGGER_NONE,
					dev_name(dev), mbox->chans);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, priv);

	return devm_mbox_controller_register(dev, &priv->mbox);
}

static const struct mtk_adsp_mbox_cfg mt8186_adsp_mbox_cfg = {
	.set_in		= 0x00,
	.set_out	= 0x04,
	.clr_in		= 0x08,
	.clr_out	= 0x0C,
};

static const struct mtk_adsp_mbox_cfg mt8195_adsp_mbox_cfg = {
	.set_in		= 0x00,
	.set_out	= 0x1c,
	.clr_in		= 0x04,
	.clr_out	= 0x20,
};

static const struct of_device_id mtk_adsp_mbox_of_match[] = {
	{ .compatible = "mediatek,mt8186-adsp-mbox", .data = &mt8186_adsp_mbox_cfg },
	{ .compatible = "mediatek,mt8195-adsp-mbox", .data = &mt8195_adsp_mbox_cfg },
	{},
};
MODULE_DEVICE_TABLE(of, mtk_adsp_mbox_of_match);

static struct platform_driver mtk_adsp_mbox_driver = {
	.probe		= mtk_adsp_mbox_probe,
	.driver = {
		.name	= "mtk_adsp_mbox",
		.of_match_table = mtk_adsp_mbox_of_match,
	},
};
module_platform_driver(mtk_adsp_mbox_driver);

MODULE_AUTHOR("Allen-KH Cheng <Allen-KH.Cheng@mediatek.com>");
MODULE_DESCRIPTION("MTK ADSP Mailbox Controller");
MODULE_LICENSE("GPL v2");
