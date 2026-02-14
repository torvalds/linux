// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 MediaTek Corporation. All rights reserved.
 * Author: Jjian Zhou <jjian.zhou.@mediatek.com>
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/mailbox/mtk-vcp-mailbox.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

struct mtk_vcp_mbox {
	struct mbox_controller mbox;
	void __iomem *base;
	struct device *dev;
	const struct mtk_vcp_mbox_cfg *cfg;
	struct mtk_ipi_info ipi_recv;
	struct mbox_chan chans;
};

struct mtk_vcp_mbox_cfg {
	u16 set_in;
	u16 clr_out;
};

static irqreturn_t mtk_vcp_mbox_irq_thread(int irq, void *data)
{
	struct mtk_vcp_mbox *priv = data;

	/* get irq status */
	priv->ipi_recv.irq_status = readl(priv->base + priv->cfg->clr_out);

	__ioread32_copy(priv->ipi_recv.msg, priv->base,
			MTK_VCP_MBOX_SLOT_MAX_SIZE / 4);

	mbox_chan_received_data(&priv->chans, &priv->ipi_recv);

	/* clear irq status */
	writel(priv->ipi_recv.irq_status, priv->base + priv->cfg->clr_out);

	return IRQ_HANDLED;
}

static struct mbox_chan *mtk_vcp_mbox_xlate(struct mbox_controller *mbox,
					    const struct of_phandle_args *sp)
{
	if (sp->args_count)
		return NULL;

	return &mbox->chans[0];
}

static int mtk_vcp_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct mtk_vcp_mbox *priv = chan->con_priv;
	struct mtk_ipi_info *ipi_info = data;
	u32 status;

	if (!ipi_info->msg) {
		dev_err(priv->dev, "msg buffer is NULL.\n");
		return -EINVAL;
	}

	status = readl(priv->base + priv->cfg->set_in);
	if (status & BIT(ipi_info->index)) {
		dev_warn(priv->dev, "mailbox IPI %d is busy.\n", ipi_info->id);
		return -EBUSY;
	}

	if (ipi_info->slot_ofs + ipi_info->len > MTK_VCP_MBOX_SLOT_MAX_SIZE)
		return -EINVAL;
	__iowrite32_copy(priv->base + ipi_info->slot_ofs, ipi_info->msg,
			 ipi_info->len);

	writel(BIT(ipi_info->index), priv->base + priv->cfg->set_in);

	return 0;
}

static bool mtk_vcp_mbox_last_tx_done(struct mbox_chan *chan)
{
	struct mtk_ipi_info *ipi_info = chan->active_req;
	struct mtk_vcp_mbox *priv = chan->con_priv;

	return !(readl(priv->base + priv->cfg->set_in) & BIT(ipi_info->index));
}

static const struct mbox_chan_ops mtk_vcp_mbox_chan_ops = {
	.send_data	= mtk_vcp_mbox_send_data,
	.last_tx_done	= mtk_vcp_mbox_last_tx_done,
};

static int mtk_vcp_mbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_vcp_mbox *priv;
	struct mbox_controller *mbox;
	int ret, irq;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->chans.con_priv = priv;
	mbox = &priv->mbox;
	mbox->dev = dev;
	mbox->ops = &mtk_vcp_mbox_chan_ops;
	mbox->txdone_irq = false;
	mbox->txdone_poll = true;
	mbox->of_xlate = mtk_vcp_mbox_xlate;
	mbox->num_chans = 1;
	mbox->chans = &priv->chans;

	priv->ipi_recv.msg = devm_kzalloc(dev, MTK_VCP_MBOX_SLOT_MAX_SIZE,
					  GFP_KERNEL);
	if (!priv->ipi_recv.msg)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->cfg = of_device_get_match_data(dev);
	if (!priv->cfg)
		return -EINVAL;

	platform_set_drvdata(pdev, priv);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(dev, irq, NULL,
					mtk_vcp_mbox_irq_thread, IRQF_ONESHOT,
					dev_name(dev), priv);
	if (ret < 0)
		return ret;

	return devm_mbox_controller_register(dev, &priv->mbox);
}

static const struct mtk_vcp_mbox_cfg mt8196_cfg = {
	.set_in		= 0x100,
	.clr_out	= 0x10c,
};

static const struct of_device_id mtk_vcp_mbox_of_match[] = {
	{ .compatible = "mediatek,mt8196-vcp-mbox", .data = &mt8196_cfg },
	{},
};
MODULE_DEVICE_TABLE(of, mtk_vcp_mbox_of_match);

static struct platform_driver mtk_vcp_mbox_driver = {
	.probe		= mtk_vcp_mbox_probe,
	.driver = {
		.name	= "mtk_vcp_mbox",
		.of_match_table = mtk_vcp_mbox_of_match,
	},
};
module_platform_driver(mtk_vcp_mbox_driver);

MODULE_AUTHOR("Jjian Zhou <jjian.zhou@mediatek.com>");
MODULE_DESCRIPTION("MTK VCP Mailbox Controller");
MODULE_LICENSE("GPL");
