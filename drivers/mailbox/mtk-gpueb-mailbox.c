// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek GPUEB mailbox driver for SoCs such as the MT8196
 *
 * Copyright (C) 2025, Collabora Ltd.
 *
 * Developers harmed in the making of this driver:
 *  - Nicolas Frattaroli <nicolas.frattaroli@collabora.com>
 */

#include <linux/atomic.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define GPUEB_MBOX_CTL_TX_STS		0x00
#define GPUEB_MBOX_CTL_IRQ_SET		0x04
#define GPUEB_MBOX_CTL_IRQ_CLR		0x74
#define GPUEB_MBOX_CTL_RX_STS		0x78

#define GPUEB_MBOX_FULL			BIT(0) /* i.e. we've received data */
#define GPUEB_MBOX_BLOCKED		BIT(1) /* i.e. the channel is shutdown */

#define GPUEB_MBOX_MAX_RX_SIZE		32 /* in bytes */

struct mtk_gpueb_mbox {
	struct device *dev;
	struct clk *clk;
	void __iomem *mbox_mmio;
	void __iomem *mbox_ctl;
	struct mbox_controller mbox;
	struct mtk_gpueb_mbox_chan *ch;
	int irq;
	const struct mtk_gpueb_mbox_variant *v;
};

/**
 * struct mtk_gpueb_mbox_chan - per-channel runtime data
 * @ebm: pointer to the parent &struct mtk_gpueb_mbox mailbox
 * @full_name: descriptive name of channel for IRQ subsystem
 * @num: channel number, starting at 0
 * @rx_status: signifies whether channel reception is turned off, or full
 * @c: pointer to the constant &struct mtk_gpueb_mbox_chan_desc channel data
 */
struct mtk_gpueb_mbox_chan {
	struct mtk_gpueb_mbox *ebm;
	char *full_name;
	u8 num;
	atomic_t rx_status;
	const struct mtk_gpueb_mbox_chan_desc *c;
};

/**
 * struct mtk_gpueb_mbox_chan_desc - per-channel constant data
 * @name: name of this channel
 * @num: index of this channel, starting at 0
 * @tx_offset: byte offset measured from mmio base for outgoing data
 * @tx_len: size, in bytes, of the outgoing data on this channel
 * @rx_offset: bytes offset measured from mmio base for incoming data
 * @rx_len: size, in bytes, of the incoming data on this channel
 */
struct mtk_gpueb_mbox_chan_desc {
	const char *name;
	const u8 num;
	const u16 tx_offset;
	const u8 tx_len;
	const u16 rx_offset;
	const u8 rx_len;
};

struct mtk_gpueb_mbox_variant {
	const u8 num_channels;
	const struct mtk_gpueb_mbox_chan_desc channels[] __counted_by(num_channels);
};

/**
 * mtk_gpueb_mbox_read_rx - read RX buffer from MMIO into channel's RX buffer
 * @buf: buffer to read into
 * @chan: pointer to the channel to read
 */
static void mtk_gpueb_mbox_read_rx(void *buf, struct mtk_gpueb_mbox_chan *chan)
{
	memcpy_fromio(buf, chan->ebm->mbox_mmio + chan->c->rx_offset, chan->c->rx_len);
}

static irqreturn_t mtk_gpueb_mbox_isr(int irq, void *data)
{
	struct mtk_gpueb_mbox_chan *ch = data;
	u32 rx_sts;

	rx_sts = readl(ch->ebm->mbox_ctl + GPUEB_MBOX_CTL_RX_STS);

	if (rx_sts & BIT(ch->num)) {
		if (!atomic_cmpxchg(&ch->rx_status, 0, GPUEB_MBOX_FULL | GPUEB_MBOX_BLOCKED))
			return IRQ_WAKE_THREAD;
	}

	return IRQ_NONE;
}

static irqreturn_t mtk_gpueb_mbox_thread(int irq, void *data)
{
	struct mtk_gpueb_mbox_chan *ch = data;
	int status;

	status = atomic_cmpxchg(&ch->rx_status, GPUEB_MBOX_FULL | GPUEB_MBOX_BLOCKED,
				GPUEB_MBOX_FULL);
	if (status == (GPUEB_MBOX_FULL | GPUEB_MBOX_BLOCKED)) {
		u8 buf[GPUEB_MBOX_MAX_RX_SIZE] = {};

		mtk_gpueb_mbox_read_rx(buf, ch);
		writel(BIT(ch->num), ch->ebm->mbox_ctl + GPUEB_MBOX_CTL_IRQ_CLR);
		mbox_chan_received_data(&ch->ebm->mbox.chans[ch->num], buf);
		atomic_set(&ch->rx_status, 0);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int mtk_gpueb_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct mtk_gpueb_mbox_chan *ch = chan->con_priv;
	u32 *values = data;
	int i;

	if (atomic_read(&ch->rx_status))
		return -EBUSY;

	/*
	 * We don't want any fancy nonsense, just write the 32-bit values in
	 * order. memcpy_toio/__iowrite32_copy don't work here, as they may use
	 * writes of different sizes or memory ordering characteristics depending
	 * on the architecture, alignment and the current phase of the moon.
	 */
	for (i = 0; i < ch->c->tx_len; i += 4)
		writel(values[i / 4], ch->ebm->mbox_mmio + ch->c->tx_offset + i);

	writel(BIT(ch->num), ch->ebm->mbox_ctl + GPUEB_MBOX_CTL_IRQ_SET);

	return 0;
}

static int mtk_gpueb_mbox_startup(struct mbox_chan *chan)
{
	struct mtk_gpueb_mbox_chan *ch = chan->con_priv;
	int ret;

	atomic_set(&ch->rx_status, 0);

	ret = clk_enable(ch->ebm->clk);
	if (ret) {
		dev_err(ch->ebm->dev, "Failed to enable EB clock: %pe\n",
			ERR_PTR(ret));
		goto err_block;
	}

	writel(BIT(ch->num), ch->ebm->mbox_ctl + GPUEB_MBOX_CTL_IRQ_CLR);

	ret = devm_request_threaded_irq(ch->ebm->dev, ch->ebm->irq, mtk_gpueb_mbox_isr,
					mtk_gpueb_mbox_thread, IRQF_SHARED | IRQF_ONESHOT,
					ch->full_name, ch);
	if (ret) {
		dev_err(ch->ebm->dev, "Failed to request IRQ: %pe\n",
			ERR_PTR(ret));
		goto err_unclk;
	}

	return 0;

err_unclk:
	clk_disable(ch->ebm->clk);
err_block:
	atomic_set(&ch->rx_status, GPUEB_MBOX_BLOCKED);

	return ret;
}

static void mtk_gpueb_mbox_shutdown(struct mbox_chan *chan)
{
	struct mtk_gpueb_mbox_chan *ch = chan->con_priv;

	atomic_set(&ch->rx_status, GPUEB_MBOX_BLOCKED);

	devm_free_irq(ch->ebm->dev, ch->ebm->irq, ch);

	clk_disable(ch->ebm->clk);
}

static bool mtk_gpueb_mbox_last_tx_done(struct mbox_chan *chan)
{
	struct mtk_gpueb_mbox_chan *ch = chan->con_priv;

	return !(readl(ch->ebm->mbox_ctl + GPUEB_MBOX_CTL_TX_STS) & BIT(ch->num));
}

static const struct mbox_chan_ops mtk_gpueb_mbox_ops = {
	.send_data = mtk_gpueb_mbox_send_data,
	.startup = mtk_gpueb_mbox_startup,
	.shutdown = mtk_gpueb_mbox_shutdown,
	.last_tx_done = mtk_gpueb_mbox_last_tx_done,
};

static int mtk_gpueb_mbox_probe(struct platform_device *pdev)
{
	struct mtk_gpueb_mbox_chan *ch;
	struct mtk_gpueb_mbox *ebm;
	unsigned int i;

	ebm = devm_kzalloc(&pdev->dev, sizeof(*ebm), GFP_KERNEL);
	if (!ebm)
		return -ENOMEM;

	ebm->dev = &pdev->dev;
	ebm->v = of_device_get_match_data(ebm->dev);

	ebm->irq = platform_get_irq(pdev, 0);
	if (ebm->irq < 0)
		return ebm->irq;

	ebm->clk = devm_clk_get_prepared(ebm->dev, NULL);
	if (IS_ERR(ebm->clk))
		return dev_err_probe(ebm->dev, PTR_ERR(ebm->clk),
				     "Failed to get 'eb' clock\n");

	ebm->mbox_mmio = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ebm->mbox_mmio))
		return dev_err_probe(ebm->dev, PTR_ERR(ebm->mbox_mmio),
				     "Couldn't map mailbox data registers\n");

	ebm->mbox_ctl = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(ebm->mbox_ctl))
		return dev_err_probe(
			ebm->dev, PTR_ERR(ebm->mbox_ctl),
			"Couldn't map mailbox control registers\n");

	ebm->ch = devm_kmalloc_array(ebm->dev, ebm->v->num_channels,
				     sizeof(*ebm->ch), GFP_KERNEL);
	if (!ebm->ch)
		return -ENOMEM;

	ebm->mbox.chans = devm_kcalloc(ebm->dev, ebm->v->num_channels,
				       sizeof(struct mbox_chan), GFP_KERNEL);
	if (!ebm->mbox.chans)
		return -ENOMEM;

	for (i = 0; i < ebm->v->num_channels; i++) {
		ch = &ebm->ch[i];
		ch->c = &ebm->v->channels[i];
		if (ch->c->rx_len > GPUEB_MBOX_MAX_RX_SIZE) {
			dev_err(ebm->dev, "Channel %s RX size (%d) too large\n",
				ch->c->name, ch->c->rx_len);
			return -EINVAL;
		}
		ch->full_name = devm_kasprintf(ebm->dev, GFP_KERNEL, "%s:%s",
					       dev_name(ebm->dev), ch->c->name);
		if (!ch->full_name)
			return -ENOMEM;

		ch->ebm = ebm;
		ch->num = i;
		spin_lock_init(&ebm->mbox.chans[i].lock);
		ebm->mbox.chans[i].con_priv = ch;
		atomic_set(&ch->rx_status, GPUEB_MBOX_BLOCKED);
	}

	ebm->mbox.dev = ebm->dev;
	ebm->mbox.num_chans = ebm->v->num_channels;
	ebm->mbox.txdone_poll = true;
	ebm->mbox.txpoll_period = 0; /* minimum hrtimer interval */
	ebm->mbox.ops = &mtk_gpueb_mbox_ops;

	dev_set_drvdata(ebm->dev, ebm);

	return devm_mbox_controller_register(ebm->dev, &ebm->mbox);
}

static const struct mtk_gpueb_mbox_variant mtk_gpueb_mbox_mt8196 = {
	.num_channels = 12,
	.channels = {
		{ "fast-dvfs-event", 0, 0x0000, 16, 0x00e0, 16 },
		{ "gpufreq",         1, 0x0010, 32, 0x00f0, 32 },
		{ "sleep",           2, 0x0030, 12, 0x0110,  4 },
		{ "timer",           3, 0x003c, 24, 0x0114,  4 },
		{ "fhctl",           4, 0x0054, 36, 0x0118,  4 },
		{ "ccf",             5, 0x0078, 16, 0x011c, 16 },
		{ "gpumpu",          6, 0x0088, 24, 0x012c,  4 },
		{ "fast-dvfs",       7, 0x00a0, 24, 0x0130, 24 },
		{ "ipir-c-met",      8, 0x00b8,  4, 0x0148, 16 },
		{ "ipis-c-met",      9, 0x00bc, 16, 0x0158,  4 },
		{ "brisket",        10, 0x00cc, 16, 0x015c, 16 },
		{ "ppb",            11, 0x00dc,  4, 0x016c,  4 },
	},
};

static const struct of_device_id mtk_gpueb_mbox_of_ids[] = {
	{ .compatible = "mediatek,mt8196-gpueb-mbox", .data = &mtk_gpueb_mbox_mt8196 },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_gpueb_mbox_of_ids);

static struct platform_driver mtk_gpueb_mbox_drv = {
	.probe = mtk_gpueb_mbox_probe,
	.driver = {
		.name = "mtk-gpueb-mbox",
		.of_match_table = mtk_gpueb_mbox_of_ids,
	}
};
module_platform_driver(mtk_gpueb_mbox_drv);

MODULE_AUTHOR("Nicolas Frattaroli <nicolas.frattaroli@collabora.com>");
MODULE_DESCRIPTION("MediaTek GPUEB mailbox driver");
MODULE_LICENSE("GPL");
