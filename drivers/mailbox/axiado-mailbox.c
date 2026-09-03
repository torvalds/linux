// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2021-2026 Axiado Corporation (or its affiliates).
 */

#include <linux/atomic.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/bitfield.h>
#include <linux/debugfs.h>
#include <linux/iopoll.h>
#include <linux/mailbox_controller.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/unaligned.h>

#define AXIADO_MBOX_TX_CHANS		8	/* 0-7 */
#define AXIADO_MBOX_RX_CHANS		8	/* 8-15 */
#define AXIADO_MBOX_CHAN_STRIDE	0x4
#define AXIADO_MBOX_TX_REG_STRIDE	0x40
#define AXIADO_MBOX_RX_REG_STRIDE	0x30
#define AXIADO_MBOX_RX_POLL_US		10
#define AXIADO_MBOX_RX_TIMEOUT_US	1000

/* Mailbox CSR bit definitions */
#define AXIADO_MBOX_CSR_EMPTY		BIT(0) /* 1 = FIFO empty; 0 = data available */
#define AXIADO_MBOX_CSR_OVERFLOW	BIT(2) /* write 1 to clear (W1C) */
#define AXIADO_MBOX_CSR_UNDERFLOW	BIT(3) /* write 1 to clear (W1C) */
#define AXIADO_MBOX_CSR_FLUSH		BIT(4) /* W1TRG flush; startup/shutdown */
#define AXIADO_MBOX_CSR_LEVEL		GENMASK(11, 5)

#define AXIADO_MBOX_CSR_ERRORS		(AXIADO_MBOX_CSR_OVERFLOW | \
					 AXIADO_MBOX_CSR_UNDERFLOW)

enum axiado_mbox_frame_result {
	AXIADO_MBOX_FRAME_EMPTY,
	AXIADO_MBOX_FRAME_OK,
	AXIADO_MBOX_FRAME_BAD,		/* malformed/incomplete; FIFO flushed */
};

struct axiado_mbox_data {
	u8 num_chans;
	u16 msg_size;
};

struct axiado_channel_data {
	void __iomem *mbox_reg;
	void __iomem *csr_reg;
	void *rx_buffer;
	u8 channel_num;
	int irq;
	struct mbox_chan *chan;
	bool active;
	atomic_t fifo_errors;
};

/* mailbox side */
struct axiado_mbox {
	struct mbox_controller mbox;
	const struct axiado_mbox_data *drv_data;
	void __iomem *tx_base;
	void __iomem *rx_base;
	struct dentry *debugfs_dir;
};

/*
 * Checks OVERFLOW/UNDERFLOW, logs and W1C-clears them if set. Shared by the
 * TX and RX paths; RX additionally flushes the FIFO afterwards to resync
 * framing, which TX must not do since it could discard data the peer has
 * not read yet.
 */
static bool axiado_mbox_clear_fifo_errors(struct axiado_channel_data *priv)
{
	struct device *dev = priv->chan->mbox->dev;
	u32 errors;

	errors = readl(priv->csr_reg) & AXIADO_MBOX_CSR_ERRORS;
	if (!errors)
		return false;

	writel(errors, priv->csr_reg);
	atomic_inc(&priv->fifo_errors);

	dev_warn_ratelimited(dev, "Channel %u FIFO error: %#x\n",
			     priv->channel_num, errors);

	return true;
}

static int axiado_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct axiado_mbox *mb = dev_get_drvdata(chan->mbox->dev);
	struct axiado_channel_data *priv = chan->con_priv;
	unsigned int idx = priv->channel_num;
	u8 tail[sizeof(u32)] = { 0 };
	const u8 *buf = data;
	unsigned int tail_len;
	unsigned int offset;
	u32 msg_len;

	if (!data)
		return -EINVAL;

	/*
	 * The Axiado mailbox message ABI stores the total message length,
	 * including this length word, as a little-endian byte count in the
	 * first word of every message.
	 */
	msg_len = get_unaligned_le32(data);
	if (msg_len < sizeof(u32) || msg_len > mb->drv_data->msg_size)
		return -EINVAL;

	/* Only touch the hardware after validating the message. */
	axiado_mbox_clear_fifo_errors(priv);

	if (!(readl(priv->csr_reg) & AXIADO_MBOX_CSR_EMPTY)) {
		dev_warn_ratelimited(mb->mbox.dev, "Channel %u is busy\n", idx);
		return -EBUSY;
	}

	/*
	 * Per-word get_unaligned_le32()/writel(), not writesl(): @data is
	 * supplied by whatever client calls mbox_send_message(), and
	 * mbox_chan_ops.send_data(void *data) makes no alignment guarantee
	 * for that buffer. writesl() dereferences it as a native u32 *,
	 * which is unsafe unless alignment is known.
	 */
	for (offset = 0; offset + sizeof(u32) <= msg_len;
	     offset += sizeof(u32))
		writel(get_unaligned_le32(buf + offset), priv->mbox_reg);

	tail_len = msg_len - offset;
	if (tail_len) {
		memcpy(tail, buf + offset, tail_len);
		writel(get_unaligned_le32(tail), priv->mbox_reg);
	}

	dev_dbg(mb->mbox.dev, "%s: Ch-%u sent\n", __func__, idx);

	return 0;
}

/*
 * Read and, if valid, deliver exactly one length-prefixed frame currently
 * at the head of the RX FIFO. Does not loop -- the caller decides whether
 * to call this again. May clear a pending FIFO error at several points
 * below, wherever a new one could plausibly have appeared since the last
 * check; each check is a cheap readl() when nothing is pending.
 */
static enum axiado_mbox_frame_result axiado_mbox_receive_one_frame(struct axiado_channel_data *priv,
								    bool *serviced)
{
	struct mbox_chan *chan = priv->chan;
	struct axiado_mbox *mb = dev_get_drvdata(chan->mbox->dev);
	u8 *buf = priv->rx_buffer;
	unsigned int num_words;
	unsigned int remaining;
	u32 msg_len;
	u32 word;
	u32 csr;
	int ret;

	/*
	 * Don't discard data on a cleared error's account alone: a rejected
	 * overflow write doesn't corrupt what was already safely queued
	 * ahead of it, so let the length-based read below decide whether
	 * what's here is usable. Count clearing a real error as @serviced
	 * so the handler reports IRQ_HANDLED whenever it actually handled
	 * a hardware condition.
	 */
	if (axiado_mbox_clear_fifo_errors(priv))
		*serviced = true;

	csr = readl(priv->csr_reg);
	if (csr & AXIADO_MBOX_CSR_EMPTY)
		return AXIADO_MBOX_FRAME_EMPTY;

	*serviced = true;

	/*
	 * The first word contains the total message length in bytes,
	 * including the length word itself.
	 */
	word = readl(priv->mbox_reg);
	msg_len = word;
	put_unaligned_le32(word, buf);

	if (msg_len < sizeof(u32) || msg_len > mb->drv_data->msg_size) {
		dev_warn_ratelimited(chan->mbox->dev,
				     "Channel %u invalid frame length %u (max %u)\n",
				     priv->channel_num, msg_len, mb->drv_data->msg_size);
		goto bad_frame;
	}

	num_words = DIV_ROUND_UP(msg_len, sizeof(u32));
	remaining = num_words - 1;

	/*
	 * The not-empty interrupt may occur as soon as the first DW enters
	 * the FIFO. Wait until all remaining DWs of this message arrive.
	 */
	if (remaining) {
		ret = readl_poll_timeout(priv->csr_reg, csr,
					 (csr & AXIADO_MBOX_CSR_ERRORS) ||
					  FIELD_GET(AXIADO_MBOX_CSR_LEVEL, csr) >=
					  remaining,
					  AXIADO_MBOX_RX_POLL_US,
					  AXIADO_MBOX_RX_TIMEOUT_US);
		/*
		 * The wait condition above is satisfied by EITHER the
		 * expected word count landing OR a FIFO error appearing.
		 * ret == 0 only means "didn't time out" - it does not mean
		 * the words are actually there. Check @csr for the error
		 * bit explicitly; otherwise an underflow can present as
		 * "success" and the read loop below pulls words that were
		 * never pushed, handing the client garbage.
		 */
		if (ret || (csr & AXIADO_MBOX_CSR_ERRORS)) {
			dev_warn_ratelimited(chan->mbox->dev,
					     "Channel %u incomplete frame: got %lu/%u words (%s)\n",
					     priv->channel_num,
					     FIELD_GET(AXIADO_MBOX_CSR_LEVEL, csr),
					     num_words,
					     (csr & AXIADO_MBOX_CSR_ERRORS) ?
					     "FIFO error" : "timed out");
			goto bad_frame;
		}

		axiado_mbox_clear_fifo_errors(priv);
	}

	/*
	 * readsl() streams the payload words directly into rx_buffer, which
	 * is allocated by this driver via devm_kzalloc() and therefore has
	 * suitable alignment for a stream accessor.
	 */
	readsl(priv->mbox_reg, buf + sizeof(u32), remaining);

	axiado_mbox_clear_fifo_errors(priv);

	if (READ_ONCE(priv->active))
		mbox_chan_received_data(chan, priv->rx_buffer);

	return AXIADO_MBOX_FRAME_OK;

bad_frame:
	axiado_mbox_clear_fifo_errors(priv);
	writel(AXIADO_MBOX_CSR_FLUSH, priv->csr_reg);

	return AXIADO_MBOX_FRAME_BAD;
}

static irqreturn_t axiado_rx_thread(int irq, void *dev_id)
{
	struct axiado_channel_data *priv = dev_id;
	enum axiado_mbox_frame_result result;
	bool handled = false;

	/*
	 * Drain all frames currently queued in the FIFO before returning.
	 * Multiple frames may accumulate while the threaded handler is
	 * running. cond_resched() prevents a continuously-fed channel from
	 * monopolizing the CPU.
	 */
	do {
		result = axiado_mbox_receive_one_frame(priv, &handled);
		cond_resched();
	} while (result == AXIADO_MBOX_FRAME_OK);

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

static int axiado_mbox_startup(struct mbox_chan *chan)
{
	struct axiado_channel_data *priv = chan->con_priv;

	/*
	 * Only flush on a genuine error. A blind flush here would discard
	 * a message the peer legitimately sent before this side started
	 * up (e.g. during normal boot sequencing), which is not corrupt
	 * and does not need resyncing.
	 */
	if (axiado_mbox_clear_fifo_errors(priv))
		writel(AXIADO_MBOX_CSR_FLUSH, priv->csr_reg);

	WRITE_ONCE(priv->active, true);

	if (priv->channel_num >= AXIADO_MBOX_TX_CHANS)
		enable_irq(priv->irq);

	return 0;
}

static void axiado_mbox_shutdown(struct mbox_chan *chan)
{
	struct axiado_channel_data *priv = chan->con_priv;

	WRITE_ONCE(priv->active, false);

	if (priv->channel_num >= AXIADO_MBOX_TX_CHANS)
		disable_irq(priv->irq);

	if (axiado_mbox_clear_fifo_errors(priv))
		writel(AXIADO_MBOX_CSR_FLUSH, priv->csr_reg);
}

static bool axiado_mbox_last_tx_done(struct mbox_chan *chan)
{
	struct axiado_channel_data *priv = chan->con_priv;

	return !!(readl(priv->csr_reg) & AXIADO_MBOX_CSR_EMPTY);
}

static const struct mbox_chan_ops axiado_mbox_chan_ops = {
	.send_data	= axiado_mbox_send_data,
	.startup	= axiado_mbox_startup,
	.shutdown	= axiado_mbox_shutdown,
	.last_tx_done	= axiado_mbox_last_tx_done,
};

static void axiado_mbox_debugfs_remove(void *dentry)
{
	debugfs_remove_recursive(dentry);
}

static int axiado_mbox_probe(struct platform_device *pdev)
{
	const struct axiado_mbox_data *drv_data;
	struct axiado_channel_data *ch_data;
	struct device *dev = &pdev->dev;
	struct axiado_mbox *mb;
	unsigned int irq_idx = 0;
	unsigned int rx_chan;
	unsigned int i;
	int ret;

	drv_data = device_get_match_data(dev);
	if (!drv_data)
		return -ENODEV;

	mb = devm_kzalloc(dev, sizeof(*mb), GFP_KERNEL);
	if (!mb)
		return -ENOMEM;

	mb->mbox.dev = dev;
	mb->mbox.num_chans = drv_data->num_chans;

	mb->mbox.chans = devm_kcalloc(&pdev->dev,
				      drv_data->num_chans,
				      sizeof(*mb->mbox.chans),
				      GFP_KERNEL);
	if (!mb->mbox.chans)
		return -ENOMEM;

	mb->tx_base = devm_platform_ioremap_resource_byname(pdev, "tx");
	if (IS_ERR(mb->tx_base))
		return PTR_ERR(mb->tx_base);

	mb->rx_base = devm_platform_ioremap_resource_byname(pdev, "rx");
	if (IS_ERR(mb->rx_base))
		return PTR_ERR(mb->rx_base);

	ch_data = devm_kcalloc(&pdev->dev,
			       drv_data->num_chans,
			       sizeof(*ch_data),
			       GFP_KERNEL);
	if (!ch_data)
		return -ENOMEM;

	mb->debugfs_dir = debugfs_create_dir(dev_name(dev), NULL);
	ret = devm_add_action_or_reset(dev, axiado_mbox_debugfs_remove,
				       mb->debugfs_dir);
	if (ret)
		return ret;

	for (i = 0; i < drv_data->num_chans; i++) {
		char name[16];

		ch_data[i].channel_num = i;
		ch_data[i].chan = &mb->mbox.chans[i];

		mb->mbox.chans[i].con_priv = &ch_data[i];

		snprintf(name, sizeof(name), "chan%u-errors", i);
		debugfs_create_atomic_t(name, 0444, mb->debugfs_dir,
					&ch_data[i].fifo_errors);

		if (i < AXIADO_MBOX_TX_CHANS) {
			ch_data[i].mbox_reg = mb->tx_base + (i * AXIADO_MBOX_CHAN_STRIDE);
			ch_data[i].csr_reg = mb->tx_base + AXIADO_MBOX_TX_REG_STRIDE +
					       (i * AXIADO_MBOX_CHAN_STRIDE);
			ch_data[i].irq = -1;
			continue;
		}

		ch_data[i].rx_buffer = devm_kzalloc(dev, drv_data->msg_size,
						    GFP_KERNEL);
		if (!ch_data[i].rx_buffer)
			return -ENOMEM;

		rx_chan = i - AXIADO_MBOX_TX_CHANS;
		ch_data[i].mbox_reg = mb->rx_base +
				       (rx_chan * AXIADO_MBOX_CHAN_STRIDE);
		ch_data[i].csr_reg = mb->rx_base + AXIADO_MBOX_RX_REG_STRIDE +
				      (rx_chan * AXIADO_MBOX_CHAN_STRIDE);
		ch_data[i].irq = platform_get_irq(pdev, irq_idx++);
		if (ch_data[i].irq < 0)
			return dev_err_probe(dev, ch_data[i].irq,
					     "Failed to get IRQ for channel %u\n", i);

		ret = devm_request_threaded_irq(dev, ch_data[i].irq, NULL,
						axiado_rx_thread,
						IRQF_ONESHOT | IRQF_NO_AUTOEN,
						dev_name(dev), &ch_data[i]);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to request IRQ for channel %u\n", i);

		dev_dbg(dev, "RX chan %u -> irq %d\n", i, ch_data[i].irq);
	}

	platform_set_drvdata(pdev, mb);
	mb->drv_data = drv_data;
	mb->mbox.ops = &axiado_mbox_chan_ops;
	mb->mbox.txdone_irq = false;
	mb->mbox.txdone_poll = true;
	mb->mbox.txpoll_period = 5;

	return devm_mbox_controller_register(dev, &mb->mbox);
}

static const struct axiado_mbox_data axiado_drv_data = {
	.num_chans = AXIADO_MBOX_TX_CHANS + AXIADO_MBOX_RX_CHANS,
	.msg_size = 256,
};

static const struct of_device_id axiado_mbox_of_match[] = {
	{ .compatible = "axiado,ax3005-mailbox", .data = &axiado_drv_data },
	{ }
};
MODULE_DEVICE_TABLE(of, axiado_mbox_of_match);

static struct platform_driver axiado_mbox_driver = {
	.driver = {
		.name = "axiado-mailbox",
		.of_match_table = axiado_mbox_of_match,
	},
	.probe = axiado_mbox_probe,
};
module_platform_driver(axiado_mbox_driver);

MODULE_AUTHOR("Axiado Corporation");
MODULE_DESCRIPTION("Axiado Mailbox driver");
MODULE_LICENSE("GPL");
