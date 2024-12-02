// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, NVIDIA CORPORATION.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <soc/tegra/bpmp.h>

#include "bpmp-private.h"

#define TRIGGER_OFFSET		0x000
#define RESULT_OFFSET(id)	(0xc00 + id * 4)
#define TRIGGER_ID_SHIFT	16
#define TRIGGER_CMD_GET		4

#define STA_OFFSET		0
#define SET_OFFSET		4
#define CLR_OFFSET		8

#define CH_MASK(ch)	(0x3 << ((ch) * 2))
#define SL_SIGL(ch)	(0x0 << ((ch) * 2))
#define SL_QUED(ch)	(0x1 << ((ch) * 2))
#define MA_FREE(ch)	(0x2 << ((ch) * 2))
#define MA_ACKD(ch)	(0x3 << ((ch) * 2))

struct tegra210_bpmp {
	void __iomem *atomics;
	void __iomem *arb_sema;
	struct irq_data *tx_irq_data;
};

static u32 bpmp_channel_status(struct tegra_bpmp *bpmp, unsigned int index)
{
	struct tegra210_bpmp *priv = bpmp->priv;

	return __raw_readl(priv->arb_sema + STA_OFFSET) & CH_MASK(index);
}

static bool tegra210_bpmp_is_response_ready(struct tegra_bpmp_channel *channel)
{
	unsigned int index = channel->index;

	return bpmp_channel_status(channel->bpmp, index) == MA_ACKD(index);
}

static bool tegra210_bpmp_is_request_ready(struct tegra_bpmp_channel *channel)
{
	unsigned int index = channel->index;

	return bpmp_channel_status(channel->bpmp, index) == SL_SIGL(index);
}

static bool
tegra210_bpmp_is_request_channel_free(struct tegra_bpmp_channel *channel)
{
	unsigned int index = channel->index;

	return bpmp_channel_status(channel->bpmp, index) == MA_FREE(index);
}

static bool
tegra210_bpmp_is_response_channel_free(struct tegra_bpmp_channel *channel)
{
	unsigned int index = channel->index;

	return bpmp_channel_status(channel->bpmp, index) == SL_QUED(index);
}

static int tegra210_bpmp_post_request(struct tegra_bpmp_channel *channel)
{
	struct tegra210_bpmp *priv = channel->bpmp->priv;

	__raw_writel(CH_MASK(channel->index), priv->arb_sema + CLR_OFFSET);

	return 0;
}

static int tegra210_bpmp_post_response(struct tegra_bpmp_channel *channel)
{
	struct tegra210_bpmp *priv = channel->bpmp->priv;

	__raw_writel(MA_ACKD(channel->index), priv->arb_sema + SET_OFFSET);

	return 0;
}

static int tegra210_bpmp_ack_response(struct tegra_bpmp_channel *channel)
{
	struct tegra210_bpmp *priv = channel->bpmp->priv;

	__raw_writel(MA_ACKD(channel->index) ^ MA_FREE(channel->index),
		     priv->arb_sema + CLR_OFFSET);

	return 0;
}

static int tegra210_bpmp_ack_request(struct tegra_bpmp_channel *channel)
{
	struct tegra210_bpmp *priv = channel->bpmp->priv;

	__raw_writel(SL_QUED(channel->index), priv->arb_sema + SET_OFFSET);

	return 0;
}

static int tegra210_bpmp_ring_doorbell(struct tegra_bpmp *bpmp)
{
	struct tegra210_bpmp *priv = bpmp->priv;
	struct irq_data *irq_data = priv->tx_irq_data;

	/*
	 * Tegra Legacy Interrupt Controller (LIC) is used to notify BPMP of
	 * available messages
	 */
	if (irq_data->chip->irq_retrigger)
		return irq_data->chip->irq_retrigger(irq_data);

	return -EINVAL;
}

static irqreturn_t rx_irq(int irq, void *data)
{
	struct tegra_bpmp *bpmp = data;

	tegra_bpmp_handle_rx(bpmp);

	return IRQ_HANDLED;
}

static int tegra210_bpmp_channel_init(struct tegra_bpmp_channel *channel,
				      struct tegra_bpmp *bpmp,
				      unsigned int index)
{
	struct tegra210_bpmp *priv = bpmp->priv;
	u32 address;
	void *p;

	/* Retrieve channel base address from BPMP */
	writel(index << TRIGGER_ID_SHIFT | TRIGGER_CMD_GET,
	       priv->atomics + TRIGGER_OFFSET);
	address = readl(priv->atomics + RESULT_OFFSET(index));

	p = devm_ioremap(bpmp->dev, address, 0x80);
	if (!p)
		return -ENOMEM;

	channel->ib = p;
	channel->ob = p;
	channel->index = index;
	init_completion(&channel->completion);
	channel->bpmp = bpmp;

	return 0;
}

static int tegra210_bpmp_init(struct tegra_bpmp *bpmp)
{
	struct platform_device *pdev = to_platform_device(bpmp->dev);
	struct tegra210_bpmp *priv;
	unsigned int i;
	int err;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	bpmp->priv = priv;

	priv->atomics = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->atomics))
		return PTR_ERR(priv->atomics);

	priv->arb_sema = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(priv->arb_sema))
		return PTR_ERR(priv->arb_sema);

	err = tegra210_bpmp_channel_init(bpmp->tx_channel, bpmp,
					 bpmp->soc->channels.cpu_tx.offset);
	if (err < 0)
		return err;

	err = tegra210_bpmp_channel_init(bpmp->rx_channel, bpmp,
					 bpmp->soc->channels.cpu_rx.offset);
	if (err < 0)
		return err;

	for (i = 0; i < bpmp->threaded.count; i++) {
		unsigned int index = bpmp->soc->channels.thread.offset + i;

		err = tegra210_bpmp_channel_init(&bpmp->threaded_channels[i],
						 bpmp, index);
		if (err < 0)
			return err;
	}

	err = platform_get_irq_byname(pdev, "tx");
	if (err < 0) {
		dev_err(&pdev->dev, "failed to get TX IRQ: %d\n", err);
		return err;
	}

	priv->tx_irq_data = irq_get_irq_data(err);
	if (!priv->tx_irq_data) {
		dev_err(&pdev->dev, "failed to get IRQ data for TX IRQ\n");
		return -ENOENT;
	}

	err = platform_get_irq_byname(pdev, "rx");
	if (err < 0) {
		dev_err(&pdev->dev, "failed to get rx IRQ: %d\n", err);
		return err;
	}

	err = devm_request_irq(&pdev->dev, err, rx_irq,
			       IRQF_NO_SUSPEND, dev_name(&pdev->dev), bpmp);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to request IRQ: %d\n", err);
		return err;
	}

	return 0;
}

const struct tegra_bpmp_ops tegra210_bpmp_ops = {
	.init = tegra210_bpmp_init,
	.is_response_ready = tegra210_bpmp_is_response_ready,
	.is_request_ready = tegra210_bpmp_is_request_ready,
	.ack_response = tegra210_bpmp_ack_response,
	.ack_request = tegra210_bpmp_ack_request,
	.is_response_channel_free = tegra210_bpmp_is_response_channel_free,
	.is_request_channel_free = tegra210_bpmp_is_request_channel_free,
	.post_response = tegra210_bpmp_post_response,
	.post_request = tegra210_bpmp_post_request,
	.ring_doorbell = tegra210_bpmp_ring_doorbell,
};
