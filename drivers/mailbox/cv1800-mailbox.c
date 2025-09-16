// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024 Sophgo Technology Inc.
 * Copyright (C) 2024 Yuntao Dai <d1581209858@live.com>
 * Copyright (C) 2025 Junhui Liu <junhui.liu@pigmoral.tech>
 */

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kfifo.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define RECV_CPU		1

#define MAILBOX_MAX_CHAN	8
#define MAILBOX_MSG_LEN		8

#define MBOX_EN_REG(cpu)	(cpu << 2)
#define MBOX_DONE_REG(cpu)	((cpu << 2) + 2)
#define MBOX_SET_CLR_REG(cpu)	(0x10 + (cpu << 4))
#define MBOX_SET_INT_REG(cpu)	(0x18 + (cpu << 4))
#define MBOX_SET_REG		0x60

#define MAILBOX_CONTEXT_OFFSET	0x0400
#define MAILBOX_CONTEXT_SIZE	0x0040

#define MBOX_CONTEXT_BASE_INDEX(base, index) \
	((u64 __iomem *)(base + MAILBOX_CONTEXT_OFFSET) + index)

/**
 * struct cv1800_mbox_chan_priv - cv1800 mailbox channel private data
 * @idx: index of channel
 * @cpu: send to which processor
 */
struct cv1800_mbox_chan_priv {
	int idx;
	int cpu;
};

struct cv1800_mbox {
	struct mbox_controller mbox;
	struct cv1800_mbox_chan_priv priv[MAILBOX_MAX_CHAN];
	struct mbox_chan chans[MAILBOX_MAX_CHAN];
	u64 __iomem *content[MAILBOX_MAX_CHAN];
	void __iomem *mbox_base;
	int recvid;
};

static irqreturn_t cv1800_mbox_isr(int irq, void *dev_id)
{
	struct cv1800_mbox *mbox = (struct cv1800_mbox *)dev_id;
	size_t i;
	u64 msg;
	int ret = IRQ_NONE;

	for (i = 0; i < MAILBOX_MAX_CHAN; i++) {
		if (mbox->content[i] && mbox->chans[i].cl) {
			memcpy_fromio(&msg, mbox->content[i], MAILBOX_MSG_LEN);
			mbox->content[i] = NULL;
			mbox_chan_received_data(&mbox->chans[i], (void *)&msg);
			ret = IRQ_HANDLED;
		}
	}

	return ret;
}

static irqreturn_t cv1800_mbox_irq(int irq, void *dev_id)
{
	struct cv1800_mbox *mbox = (struct cv1800_mbox *)dev_id;
	u8 set, valid;
	size_t i;
	int ret = IRQ_NONE;

	set = readb(mbox->mbox_base + MBOX_SET_INT_REG(RECV_CPU));

	if (!set)
		return ret;

	for (i = 0; i < MAILBOX_MAX_CHAN; i++) {
		valid = set & BIT(i);
		if (valid) {
			mbox->content[i] =
				MBOX_CONTEXT_BASE_INDEX(mbox->mbox_base, i);
			writeb(valid, mbox->mbox_base +
				      MBOX_SET_CLR_REG(RECV_CPU));
			writeb(~valid, mbox->mbox_base + MBOX_EN_REG(RECV_CPU));
			ret = IRQ_WAKE_THREAD;
		}
	}

	return ret;
}

static int cv1800_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct cv1800_mbox_chan_priv *priv =
		(struct cv1800_mbox_chan_priv *)chan->con_priv;
	struct cv1800_mbox *mbox = dev_get_drvdata(chan->mbox->dev);
	int idx = priv->idx;
	int cpu = priv->cpu;
	u8 en, valid;

	memcpy_toio(MBOX_CONTEXT_BASE_INDEX(mbox->mbox_base, idx),
		    data, MAILBOX_MSG_LEN);

	valid = BIT(idx);
	writeb(valid, mbox->mbox_base + MBOX_SET_CLR_REG(cpu));
	en = readb(mbox->mbox_base + MBOX_EN_REG(cpu));
	writeb(en | valid, mbox->mbox_base + MBOX_EN_REG(cpu));
	writeb(valid, mbox->mbox_base + MBOX_SET_REG);

	return 0;
}

static bool cv1800_last_tx_done(struct mbox_chan *chan)
{
	struct cv1800_mbox_chan_priv *priv =
		(struct cv1800_mbox_chan_priv *)chan->con_priv;
	struct cv1800_mbox *mbox = dev_get_drvdata(chan->mbox->dev);
	u8 en;

	en = readb(mbox->mbox_base + MBOX_EN_REG(priv->cpu));

	return !(en & BIT(priv->idx));
}

static const struct mbox_chan_ops cv1800_mbox_chan_ops = {
	.send_data = cv1800_mbox_send_data,
	.last_tx_done = cv1800_last_tx_done,
};

static struct mbox_chan *cv1800_mbox_xlate(struct mbox_controller *mbox,
					   const struct of_phandle_args *spec)
{
	struct cv1800_mbox_chan_priv *priv;

	int idx = spec->args[0];
	int cpu = spec->args[1];

	if (idx >= mbox->num_chans)
		return ERR_PTR(-EINVAL);

	priv = mbox->chans[idx].con_priv;
	priv->cpu = cpu;

	return &mbox->chans[idx];
}

static const struct of_device_id cv1800_mbox_of_match[] = {
	{ .compatible = "sophgo,cv1800b-mailbox", },
	{},
};
MODULE_DEVICE_TABLE(of, cv1800_mbox_of_match);

static int cv1800_mbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cv1800_mbox *mb;
	int irq, idx, err;

	mb = devm_kzalloc(dev, sizeof(*mb), GFP_KERNEL);
	if (!mb)
		return -ENOMEM;

	mb->mbox_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mb->mbox_base))
		return dev_err_probe(dev, PTR_ERR(mb->mbox_base),
				     "Failed to map resource\n");

	mb->mbox.dev = dev;
	mb->mbox.chans = mb->chans;
	mb->mbox.txdone_poll = true;
	mb->mbox.ops = &cv1800_mbox_chan_ops;
	mb->mbox.num_chans = MAILBOX_MAX_CHAN;
	mb->mbox.of_xlate = cv1800_mbox_xlate;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	err = devm_request_threaded_irq(dev, irq, cv1800_mbox_irq,
					cv1800_mbox_isr, IRQF_ONESHOT,
					dev_name(&pdev->dev), mb);
	if (err < 0)
		return dev_err_probe(dev, err, "Failed to register irq\n");

	for (idx = 0; idx < MAILBOX_MAX_CHAN; idx++) {
		mb->priv[idx].idx = idx;
		mb->mbox.chans[idx].con_priv = &mb->priv[idx];
	}

	platform_set_drvdata(pdev, mb);

	err = devm_mbox_controller_register(dev, &mb->mbox);
	if (err)
		return dev_err_probe(dev, err, "Failed to register mailbox\n");

	return 0;
}

static struct platform_driver cv1800_mbox_driver = {
	.driver = {
		.name = "cv1800-mbox",
		.of_match_table = cv1800_mbox_of_match,
	},
	.probe	= cv1800_mbox_probe,
};

module_platform_driver(cv1800_mbox_driver);

MODULE_DESCRIPTION("cv1800 mailbox driver");
MODULE_LICENSE("GPL");
