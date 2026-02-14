// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Alibaba Group Holding Limited.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* Status Register */
#define TH_1520_MBOX_STA 0x0
#define TH_1520_MBOX_CLR 0x4
#define TH_1520_MBOX_MASK 0xc

/* Transmit/receive data register:
 * INFO0 ~ INFO6
 */
#define TH_1520_MBOX_INFO_NUM 8
#define TH_1520_MBOX_DATA_INFO_NUM 7
#define TH_1520_MBOX_INFO0 0x14
/* Transmit ack register: INFO7 */
#define TH_1520_MBOX_INFO7 0x30

/* Generate remote icu IRQ Register */
#define TH_1520_MBOX_GEN 0x10
#define TH_1520_MBOX_GEN_RX_DATA BIT(6)
#define TH_1520_MBOX_GEN_TX_ACK BIT(7)

#define TH_1520_MBOX_CHAN_RES_SIZE 0x1000
#define TH_1520_MBOX_CHANS 4
#define TH_1520_MBOX_CHAN_NAME_SIZE 20

#define TH_1520_MBOX_ACK_MAGIC 0xdeadbeaf

#ifdef CONFIG_PM_SLEEP
/* store MBOX context across system-wide suspend/resume transitions */
struct th1520_mbox_context {
	u32 intr_mask[TH_1520_MBOX_CHANS];
};
#endif

enum th1520_mbox_icu_cpu_id {
	TH_1520_MBOX_ICU_KERNEL_CPU0, /* 910T */
	TH_1520_MBOX_ICU_CPU1, /* 902 */
	TH_1520_MBOX_ICU_CPU2, /* 906 */
	TH_1520_MBOX_ICU_CPU3, /* 910R */
};

struct th1520_mbox_con_priv {
	enum th1520_mbox_icu_cpu_id idx;
	void __iomem *comm_local_base;
	void __iomem *comm_remote_base;
	char irq_desc[TH_1520_MBOX_CHAN_NAME_SIZE];
	struct mbox_chan *chan;
};

struct th1520_mbox_priv {
	struct device *dev;
	void __iomem *local_icu[TH_1520_MBOX_CHANS];
	void __iomem *remote_icu[TH_1520_MBOX_CHANS - 1];
	void __iomem *cur_cpu_ch_base;
	spinlock_t mbox_lock; /* control register lock */

	struct mbox_controller mbox;
	struct mbox_chan mbox_chans[TH_1520_MBOX_CHANS];
	struct clk_bulk_data clocks[TH_1520_MBOX_CHANS];
	struct th1520_mbox_con_priv con_priv[TH_1520_MBOX_CHANS];
	int irq;
#ifdef CONFIG_PM_SLEEP
	struct th1520_mbox_context *ctx;
#endif
};

static struct th1520_mbox_priv *
to_th1520_mbox_priv(struct mbox_controller *mbox)
{
	return container_of(mbox, struct th1520_mbox_priv, mbox);
}

static void th1520_mbox_write(struct th1520_mbox_priv *priv, u32 val, u32 offs)
{
	iowrite32(val, priv->cur_cpu_ch_base + offs);
}

static u32 th1520_mbox_read(struct th1520_mbox_priv *priv, u32 offs)
{
	return ioread32(priv->cur_cpu_ch_base + offs);
}

static u32 th1520_mbox_rmw(struct th1520_mbox_priv *priv, u32 off, u32 set,
			   u32 clr)
{
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&priv->mbox_lock, flags);
	val = th1520_mbox_read(priv, off);
	val &= ~clr;
	val |= set;
	th1520_mbox_write(priv, val, off);
	spin_unlock_irqrestore(&priv->mbox_lock, flags);

	return val;
}

static void th1520_mbox_chan_write(struct th1520_mbox_con_priv *cp, u32 val,
				   u32 offs, bool is_remote)
{
	if (is_remote)
		iowrite32(val, cp->comm_remote_base + offs);
	else
		iowrite32(val, cp->comm_local_base + offs);
}

static u32 th1520_mbox_chan_read(struct th1520_mbox_con_priv *cp, u32 offs,
				 bool is_remote)
{
	if (is_remote)
		return ioread32(cp->comm_remote_base + offs);
	else
		return ioread32(cp->comm_local_base + offs);
}

static void th1520_mbox_chan_rmw(struct th1520_mbox_con_priv *cp, u32 off,
				 u32 set, u32 clr, bool is_remote)
{
	struct th1520_mbox_priv *priv = to_th1520_mbox_priv(cp->chan->mbox);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&priv->mbox_lock, flags);
	val = th1520_mbox_chan_read(cp, off, is_remote);
	val &= ~clr;
	val |= set;
	th1520_mbox_chan_write(cp, val, off, is_remote);
	spin_unlock_irqrestore(&priv->mbox_lock, flags);
}

static void th1520_mbox_chan_rd_data(struct th1520_mbox_con_priv *cp,
				     void *data, bool is_remote)
{
	u32 off = TH_1520_MBOX_INFO0;
	u32 *arg = data;
	u32 i;

	/* read info0 ~ info6, totally 28 bytes
	 * requires data memory size is 28 bytes
	 */
	for (i = 0; i < TH_1520_MBOX_DATA_INFO_NUM; i++) {
		*arg = th1520_mbox_chan_read(cp, off, is_remote);
		off += 4;
		arg++;
	}
}

static void th1520_mbox_chan_wr_data(struct th1520_mbox_con_priv *cp,
				     void *data, bool is_remote)
{
	u32 off = TH_1520_MBOX_INFO0;
	u32 *arg = data;
	u32 i;

	/* write info0 ~ info6, totally 28 bytes
	 * requires data memory is 28 bytes valid data
	 */
	for (i = 0; i < TH_1520_MBOX_DATA_INFO_NUM; i++) {
		th1520_mbox_chan_write(cp, *arg, off, is_remote);
		off += 4;
		arg++;
	}
}

static void th1520_mbox_chan_wr_ack(struct th1520_mbox_con_priv *cp, void *data,
				    bool is_remote)
{
	u32 off = TH_1520_MBOX_INFO7;
	u32 *arg = data;

	th1520_mbox_chan_write(cp, *arg, off, is_remote);
}

static int th1520_mbox_chan_id_to_mapbit(struct th1520_mbox_con_priv *cp)
{
	int mapbit = 0;
	int i;

	for (i = 0; i < TH_1520_MBOX_CHANS; i++) {
		if (i == cp->idx)
			return mapbit;

		if (i != TH_1520_MBOX_ICU_KERNEL_CPU0)
			mapbit++;
	}

	if (i == TH_1520_MBOX_CHANS)
		dev_err(cp->chan->mbox->dev, "convert to mapbit failed\n");

	return 0;
}

static irqreturn_t th1520_mbox_isr(int irq, void *p)
{
	struct mbox_chan *chan = p;
	struct th1520_mbox_priv *priv = to_th1520_mbox_priv(chan->mbox);
	struct th1520_mbox_con_priv *cp = chan->con_priv;
	int mapbit = th1520_mbox_chan_id_to_mapbit(cp);
	u32 sta, dat[TH_1520_MBOX_DATA_INFO_NUM];
	u32 ack_magic = TH_1520_MBOX_ACK_MAGIC;
	u32 info0_data, info7_data;

	sta = th1520_mbox_read(priv, TH_1520_MBOX_STA);
	if (!(sta & BIT(mapbit)))
		return IRQ_NONE;

	/* clear chan irq bit in STA register */
	th1520_mbox_rmw(priv, TH_1520_MBOX_CLR, BIT(mapbit), 0);

	/* info0 is the protocol word, should not be zero! */
	info0_data = th1520_mbox_chan_read(cp, TH_1520_MBOX_INFO0, false);
	if (info0_data) {
		/* read info0~info6 data */
		th1520_mbox_chan_rd_data(cp, dat, false);

		/* clear local info0 */
		th1520_mbox_chan_write(cp, 0x0, TH_1520_MBOX_INFO0, false);

		/* notify remote cpu */
		th1520_mbox_chan_wr_ack(cp, &ack_magic, true);
		/* CPU1 902/906 use polling mode to monitor info7 */
		if (cp->idx != TH_1520_MBOX_ICU_CPU1 &&
		    cp->idx != TH_1520_MBOX_ICU_CPU2)
			th1520_mbox_chan_rmw(cp, TH_1520_MBOX_GEN,
					     TH_1520_MBOX_GEN_TX_ACK, 0, true);

		/* transfer the data to client */
		mbox_chan_received_data(chan, (void *)dat);
	}

	/* info7 magic value mean the real ack signal, not generate bit7 */
	info7_data = th1520_mbox_chan_read(cp, TH_1520_MBOX_INFO7, false);
	if (info7_data == TH_1520_MBOX_ACK_MAGIC) {
		/* clear local info7 */
		th1520_mbox_chan_write(cp, 0x0, TH_1520_MBOX_INFO7, false);

		/* notify framework the last TX has completed */
		mbox_chan_txdone(chan, 0);
	}

	if (!info0_data && !info7_data)
		return IRQ_NONE;

	return IRQ_HANDLED;
}

static int th1520_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct th1520_mbox_con_priv *cp = chan->con_priv;

	th1520_mbox_chan_wr_data(cp, data, true);
	th1520_mbox_chan_rmw(cp, TH_1520_MBOX_GEN, TH_1520_MBOX_GEN_RX_DATA, 0,
			     true);
	return 0;
}

static int th1520_mbox_startup(struct mbox_chan *chan)
{
	struct th1520_mbox_priv *priv = to_th1520_mbox_priv(chan->mbox);
	struct th1520_mbox_con_priv *cp = chan->con_priv;
	u32 data[8] = {};
	int mask_bit;
	int ret;

	/* clear local and remote generate and info0~info7 */
	th1520_mbox_chan_rmw(cp, TH_1520_MBOX_GEN, 0x0, 0xff, true);
	th1520_mbox_chan_rmw(cp, TH_1520_MBOX_GEN, 0x0, 0xff, false);
	th1520_mbox_chan_wr_ack(cp, &data[7], true);
	th1520_mbox_chan_wr_ack(cp, &data[7], false);
	th1520_mbox_chan_wr_data(cp, &data[0], true);
	th1520_mbox_chan_wr_data(cp, &data[0], false);

	/* enable the chan mask */
	mask_bit = th1520_mbox_chan_id_to_mapbit(cp);
	th1520_mbox_rmw(priv, TH_1520_MBOX_MASK, BIT(mask_bit), 0);

	/*
	 * Mixing devm_ managed resources with manual IRQ handling is generally
	 * discouraged due to potential complexities with resource management,
	 * especially when dealing with shared interrupts. However, in this case,
	 * the approach is safe and effective because:
	 *
	 * 1. Each mailbox channel requests its IRQ within the .startup() callback
	 *    and frees it within the .shutdown() callback.
	 * 2. During device unbinding, the devm_ managed mailbox controller first
	 *    iterates through all channels, ensuring that their IRQs are freed before
	 *    any other devm_ resources are released.
	 *
	 * This ordering guarantees that no interrupts can be triggered from the device
	 * while it is being unbound, preventing race conditions and ensuring system
	 * stability.
	 */
	ret = request_irq(priv->irq, th1520_mbox_isr,
			  IRQF_SHARED | IRQF_NO_SUSPEND, cp->irq_desc, chan);
	if (ret) {
		dev_err(priv->dev, "Unable to acquire IRQ %d\n", priv->irq);
		return ret;
	}

	return 0;
}

static void th1520_mbox_shutdown(struct mbox_chan *chan)
{
	struct th1520_mbox_priv *priv = to_th1520_mbox_priv(chan->mbox);
	struct th1520_mbox_con_priv *cp = chan->con_priv;
	int mask_bit;

	free_irq(priv->irq, chan);

	/* clear the chan mask */
	mask_bit = th1520_mbox_chan_id_to_mapbit(cp);
	th1520_mbox_rmw(priv, TH_1520_MBOX_MASK, 0, BIT(mask_bit));
}

static const struct mbox_chan_ops th1520_mbox_ops = {
	.send_data = th1520_mbox_send_data,
	.startup = th1520_mbox_startup,
	.shutdown = th1520_mbox_shutdown,
};

static int th1520_mbox_init_generic(struct th1520_mbox_priv *priv)
{
#ifdef CONFIG_PM_SLEEP
	priv->ctx = devm_kzalloc(priv->dev, sizeof(*priv->ctx), GFP_KERNEL);
	if (!priv->ctx)
		return -ENOMEM;
#endif
	/* Set default configuration */
	th1520_mbox_write(priv, 0xff, TH_1520_MBOX_CLR);
	th1520_mbox_write(priv, 0x0, TH_1520_MBOX_MASK);
	return 0;
}

static struct mbox_chan *th1520_mbox_xlate(struct mbox_controller *mbox,
					   const struct of_phandle_args *sp)
{
	u32 chan;

	if (sp->args_count != 1) {
		dev_err(mbox->dev, "Invalid argument count %d\n",
			sp->args_count);
		return ERR_PTR(-EINVAL);
	}

	chan = sp->args[0]; /* comm remote channel */

	if (chan >= mbox->num_chans) {
		dev_err(mbox->dev, "Not supported channel number: %d\n", chan);
		return ERR_PTR(-EINVAL);
	}

	if (chan == TH_1520_MBOX_ICU_KERNEL_CPU0) {
		dev_err(mbox->dev, "Cannot communicate with yourself\n");
		return ERR_PTR(-EINVAL);
	}

	return &mbox->chans[chan];
}

static void __iomem *th1520_map_mmio(struct platform_device *pdev,
				     char *res_name, size_t offset)
{
	void __iomem *mapped;
	struct resource *res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, res_name);

	if (!res) {
		dev_err(&pdev->dev, "Failed to get resource: %s\n", res_name);
		return ERR_PTR(-EINVAL);
	}

	mapped = devm_ioremap(&pdev->dev, res->start + offset,
			      resource_size(res) - offset);
	if (!mapped) {
		dev_err(&pdev->dev, "Failed to map resource: %s\n", res_name);
		return ERR_PTR(-ENOMEM);
	}

	return mapped;
}

static void th1520_disable_clk(void *data)
{
	struct th1520_mbox_priv *priv = data;

	clk_bulk_disable_unprepare(ARRAY_SIZE(priv->clocks), priv->clocks);
}

static int th1520_mbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct th1520_mbox_priv *priv;
	unsigned int remote_idx = 0;
	unsigned int i;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	priv->clocks[0].id = "clk-local";
	priv->clocks[1].id = "clk-remote-icu0";
	priv->clocks[2].id = "clk-remote-icu1";
	priv->clocks[3].id = "clk-remote-icu2";

	ret = devm_clk_bulk_get(dev, ARRAY_SIZE(priv->clocks),
				priv->clocks);
	if (ret) {
		dev_err(dev, "Failed to get clocks\n");
		return ret;
	}

	ret = clk_bulk_prepare_enable(ARRAY_SIZE(priv->clocks), priv->clocks);
	if (ret) {
		dev_err(dev, "Failed to enable clocks\n");
		return ret;
	}

	ret = devm_add_action_or_reset(dev, th1520_disable_clk, priv);
	if (ret)
		return ret;

	/*
	 * The address mappings in the device tree align precisely with those
	 * outlined in the manual. However, register offsets within these
	 * mapped regions are irregular, particularly for remote-icu0.
	 * Consequently, th1520_map_mmio() requires an additional parameter to
	 * handle this quirk.
	 */
	priv->local_icu[TH_1520_MBOX_ICU_KERNEL_CPU0] =
		th1520_map_mmio(pdev, "local", 0x0);
	if (IS_ERR(priv->local_icu[TH_1520_MBOX_ICU_KERNEL_CPU0]))
		return PTR_ERR(priv->local_icu[TH_1520_MBOX_ICU_KERNEL_CPU0]);

	priv->remote_icu[0] = th1520_map_mmio(pdev, "remote-icu0", 0x4000);
	if (IS_ERR(priv->remote_icu[0]))
		return PTR_ERR(priv->remote_icu[0]);

	priv->remote_icu[1] = th1520_map_mmio(pdev, "remote-icu1", 0x0);
	if (IS_ERR(priv->remote_icu[1]))
		return PTR_ERR(priv->remote_icu[1]);

	priv->remote_icu[2] = th1520_map_mmio(pdev, "remote-icu2", 0x0);
	if (IS_ERR(priv->remote_icu[2]))
		return PTR_ERR(priv->remote_icu[2]);

	priv->local_icu[TH_1520_MBOX_ICU_CPU1] =
		priv->local_icu[TH_1520_MBOX_ICU_KERNEL_CPU0] +
		TH_1520_MBOX_CHAN_RES_SIZE;
	priv->local_icu[TH_1520_MBOX_ICU_CPU2] =
		priv->local_icu[TH_1520_MBOX_ICU_CPU1] +
		TH_1520_MBOX_CHAN_RES_SIZE;
	priv->local_icu[TH_1520_MBOX_ICU_CPU3] =
		priv->local_icu[TH_1520_MBOX_ICU_CPU2] +
		TH_1520_MBOX_CHAN_RES_SIZE;

	priv->cur_cpu_ch_base = priv->local_icu[TH_1520_MBOX_ICU_KERNEL_CPU0];

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0)
		return priv->irq;

	/* init the chans */
	for (i = 0; i < TH_1520_MBOX_CHANS; i++) {
		struct th1520_mbox_con_priv *cp = &priv->con_priv[i];

		cp->idx = i;
		cp->chan = &priv->mbox_chans[i];
		priv->mbox_chans[i].con_priv = cp;
		snprintf(cp->irq_desc, sizeof(cp->irq_desc),
			 "th1520_mbox_chan[%i]", cp->idx);

		cp->comm_local_base = priv->local_icu[i];
		if (i != TH_1520_MBOX_ICU_KERNEL_CPU0) {
			cp->comm_remote_base = priv->remote_icu[remote_idx];
			remote_idx++;
		}
	}

	spin_lock_init(&priv->mbox_lock);

	priv->mbox.dev = dev;
	priv->mbox.ops = &th1520_mbox_ops;
	priv->mbox.chans = priv->mbox_chans;
	priv->mbox.num_chans = TH_1520_MBOX_CHANS;
	priv->mbox.of_xlate = th1520_mbox_xlate;
	priv->mbox.txdone_irq = true;

	platform_set_drvdata(pdev, priv);

	ret = th1520_mbox_init_generic(priv);
	if (ret) {
		dev_err(dev, "Failed to init mailbox context\n");
		return ret;
	}

	return devm_mbox_controller_register(dev, &priv->mbox);
}

static const struct of_device_id th1520_mbox_dt_ids[] = {
	{ .compatible = "thead,th1520-mbox" },
	{}
};
MODULE_DEVICE_TABLE(of, th1520_mbox_dt_ids);

#ifdef CONFIG_PM_SLEEP
static int __maybe_unused th1520_mbox_suspend_noirq(struct device *dev)
{
	struct th1520_mbox_priv *priv = dev_get_drvdata(dev);
	struct th1520_mbox_context *ctx = priv->ctx;
	u32 i;
	/*
	 * ONLY interrupt mask bit should be stored and restores.
	 * INFO data all assumed to be lost.
	 */
	for (i = 0; i < TH_1520_MBOX_CHANS; i++) {
		ctx->intr_mask[i] =
			ioread32(priv->local_icu[i] + TH_1520_MBOX_MASK);
	}
	return 0;
}

static int __maybe_unused th1520_mbox_resume_noirq(struct device *dev)
{
	struct th1520_mbox_priv *priv = dev_get_drvdata(dev);
	struct th1520_mbox_context *ctx = priv->ctx;
	u32 i;

	for (i = 0; i < TH_1520_MBOX_CHANS; i++) {
		iowrite32(ctx->intr_mask[i],
			  priv->local_icu[i] + TH_1520_MBOX_MASK);
	}

	return 0;
}
#endif

static int  __maybe_unused th1520_mbox_runtime_suspend(struct device *dev)
{
	struct th1520_mbox_priv *priv = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(ARRAY_SIZE(priv->clocks), priv->clocks);

	return 0;
}

static int __maybe_unused th1520_mbox_runtime_resume(struct device *dev)
{
	struct th1520_mbox_priv *priv = dev_get_drvdata(dev);
	int ret;

	ret = clk_bulk_prepare_enable(ARRAY_SIZE(priv->clocks), priv->clocks);
	if (ret)
		dev_err(dev, "Failed to enable clocks in runtime resume\n");

	return ret;
}

static const struct dev_pm_ops th1520_mbox_pm_ops = {
#ifdef CONFIG_PM_SLEEP
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(th1520_mbox_suspend_noirq,
				      th1520_mbox_resume_noirq)
#endif
	SET_RUNTIME_PM_OPS(th1520_mbox_runtime_suspend,
			   th1520_mbox_runtime_resume, NULL)
};

static struct platform_driver th1520_mbox_driver = {
	.probe		= th1520_mbox_probe,
	.driver = {
		.name	= "th1520-mbox",
		.of_match_table = th1520_mbox_dt_ids,
		.pm = &th1520_mbox_pm_ops,
	},
};
module_platform_driver(th1520_mbox_driver);

MODULE_DESCRIPTION("Thead TH-1520 mailbox IPC driver");
MODULE_LICENSE("GPL");
