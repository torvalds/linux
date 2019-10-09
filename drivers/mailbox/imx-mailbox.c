// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Pengutronix, Oleksij Rempel <o.rempel@pengutronix.de>
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/slab.h>

#define IMX_MU_xSR_GIPn(x)	BIT(28 + (3 - (x)))
#define IMX_MU_xSR_RFn(x)	BIT(24 + (3 - (x)))
#define IMX_MU_xSR_TEn(x)	BIT(20 + (3 - (x)))
#define IMX_MU_xSR_BRDIP	BIT(9)

/* General Purpose Interrupt Enable */
#define IMX_MU_xCR_GIEn(x)	BIT(28 + (3 - (x)))
/* Receive Interrupt Enable */
#define IMX_MU_xCR_RIEn(x)	BIT(24 + (3 - (x)))
/* Transmit Interrupt Enable */
#define IMX_MU_xCR_TIEn(x)	BIT(20 + (3 - (x)))
/* General Purpose Interrupt Request */
#define IMX_MU_xCR_GIRn(x)	BIT(16 + (3 - (x)))

#define IMX_MU_CHANS		16
#define IMX_MU_CHAN_NAME_SIZE	20

enum imx_mu_chan_type {
	IMX_MU_TYPE_TX,		/* Tx */
	IMX_MU_TYPE_RX,		/* Rx */
	IMX_MU_TYPE_TXDB,	/* Tx doorbell */
	IMX_MU_TYPE_RXDB,	/* Rx doorbell */
};

struct imx_mu_dcfg {
	u32	xTR[4];		/* Transmit Registers */
	u32	xRR[4];		/* Receive Registers */
	u32	xSR;		/* Status Register */
	u32	xCR;		/* Control Register */
};

struct imx_mu_con_priv {
	unsigned int		idx;
	char			irq_desc[IMX_MU_CHAN_NAME_SIZE];
	enum imx_mu_chan_type	type;
	struct mbox_chan	*chan;
	struct tasklet_struct	txdb_tasklet;
};

struct imx_mu_priv {
	struct device		*dev;
	void __iomem		*base;
	spinlock_t		xcr_lock; /* control register lock */

	struct mbox_controller	mbox;
	struct mbox_chan	mbox_chans[IMX_MU_CHANS];

	struct imx_mu_con_priv  con_priv[IMX_MU_CHANS];
	const struct imx_mu_dcfg	*dcfg;
	struct clk		*clk;
	int			irq;

	bool			side_b;
};

static const struct imx_mu_dcfg imx_mu_cfg_imx6sx = {
	.xTR	= {0x0, 0x4, 0x8, 0xc},
	.xRR	= {0x10, 0x14, 0x18, 0x1c},
	.xSR	= 0x20,
	.xCR	= 0x24,
};

static const struct imx_mu_dcfg imx_mu_cfg_imx7ulp = {
	.xTR	= {0x20, 0x24, 0x28, 0x2c},
	.xRR	= {0x40, 0x44, 0x48, 0x4c},
	.xSR	= 0x60,
	.xCR	= 0x64,
};

static struct imx_mu_priv *to_imx_mu_priv(struct mbox_controller *mbox)
{
	return container_of(mbox, struct imx_mu_priv, mbox);
}

static void imx_mu_write(struct imx_mu_priv *priv, u32 val, u32 offs)
{
	iowrite32(val, priv->base + offs);
}

static u32 imx_mu_read(struct imx_mu_priv *priv, u32 offs)
{
	return ioread32(priv->base + offs);
}

static u32 imx_mu_xcr_rmw(struct imx_mu_priv *priv, u32 set, u32 clr)
{
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&priv->xcr_lock, flags);
	val = imx_mu_read(priv, priv->dcfg->xCR);
	val &= ~clr;
	val |= set;
	imx_mu_write(priv, val, priv->dcfg->xCR);
	spin_unlock_irqrestore(&priv->xcr_lock, flags);

	return val;
}

static void imx_mu_txdb_tasklet(unsigned long data)
{
	struct imx_mu_con_priv *cp = (struct imx_mu_con_priv *)data;

	mbox_chan_txdone(cp->chan, 0);
}

static irqreturn_t imx_mu_isr(int irq, void *p)
{
	struct mbox_chan *chan = p;
	struct imx_mu_priv *priv = to_imx_mu_priv(chan->mbox);
	struct imx_mu_con_priv *cp = chan->con_priv;
	u32 val, ctrl, dat;

	ctrl = imx_mu_read(priv, priv->dcfg->xCR);
	val = imx_mu_read(priv, priv->dcfg->xSR);

	switch (cp->type) {
	case IMX_MU_TYPE_TX:
		val &= IMX_MU_xSR_TEn(cp->idx) &
			(ctrl & IMX_MU_xCR_TIEn(cp->idx));
		break;
	case IMX_MU_TYPE_RX:
		val &= IMX_MU_xSR_RFn(cp->idx) &
			(ctrl & IMX_MU_xCR_RIEn(cp->idx));
		break;
	case IMX_MU_TYPE_RXDB:
		val &= IMX_MU_xSR_GIPn(cp->idx) &
			(ctrl & IMX_MU_xCR_GIEn(cp->idx));
		break;
	default:
		break;
	}

	if (!val)
		return IRQ_NONE;

	if (val == IMX_MU_xSR_TEn(cp->idx)) {
		imx_mu_xcr_rmw(priv, 0, IMX_MU_xCR_TIEn(cp->idx));
		mbox_chan_txdone(chan, 0);
	} else if (val == IMX_MU_xSR_RFn(cp->idx)) {
		dat = imx_mu_read(priv, priv->dcfg->xRR[cp->idx]);
		mbox_chan_received_data(chan, (void *)&dat);
	} else if (val == IMX_MU_xSR_GIPn(cp->idx)) {
		imx_mu_write(priv, IMX_MU_xSR_GIPn(cp->idx), priv->dcfg->xSR);
		mbox_chan_received_data(chan, NULL);
	} else {
		dev_warn_ratelimited(priv->dev, "Not handled interrupt\n");
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

static int imx_mu_send_data(struct mbox_chan *chan, void *data)
{
	struct imx_mu_priv *priv = to_imx_mu_priv(chan->mbox);
	struct imx_mu_con_priv *cp = chan->con_priv;
	u32 *arg = data;

	switch (cp->type) {
	case IMX_MU_TYPE_TX:
		imx_mu_write(priv, *arg, priv->dcfg->xTR[cp->idx]);
		imx_mu_xcr_rmw(priv, IMX_MU_xCR_TIEn(cp->idx), 0);
		break;
	case IMX_MU_TYPE_TXDB:
		imx_mu_xcr_rmw(priv, IMX_MU_xCR_GIRn(cp->idx), 0);
		tasklet_schedule(&cp->txdb_tasklet);
		break;
	default:
		dev_warn_ratelimited(priv->dev, "Send data on wrong channel type: %d\n", cp->type);
		return -EINVAL;
	}

	return 0;
}

static int imx_mu_startup(struct mbox_chan *chan)
{
	struct imx_mu_priv *priv = to_imx_mu_priv(chan->mbox);
	struct imx_mu_con_priv *cp = chan->con_priv;
	int ret;

	if (cp->type == IMX_MU_TYPE_TXDB) {
		/* Tx doorbell don't have ACK support */
		tasklet_init(&cp->txdb_tasklet, imx_mu_txdb_tasklet,
			     (unsigned long)cp);
		return 0;
	}

	ret = request_irq(priv->irq, imx_mu_isr, IRQF_SHARED |
			  IRQF_NO_SUSPEND, cp->irq_desc, chan);
	if (ret) {
		dev_err(priv->dev,
			"Unable to acquire IRQ %d\n", priv->irq);
		return ret;
	}

	switch (cp->type) {
	case IMX_MU_TYPE_RX:
		imx_mu_xcr_rmw(priv, IMX_MU_xCR_RIEn(cp->idx), 0);
		break;
	case IMX_MU_TYPE_RXDB:
		imx_mu_xcr_rmw(priv, IMX_MU_xCR_GIEn(cp->idx), 0);
		break;
	default:
		break;
	}

	return 0;
}

static void imx_mu_shutdown(struct mbox_chan *chan)
{
	struct imx_mu_priv *priv = to_imx_mu_priv(chan->mbox);
	struct imx_mu_con_priv *cp = chan->con_priv;

	if (cp->type == IMX_MU_TYPE_TXDB) {
		tasklet_kill(&cp->txdb_tasklet);
		return;
	}

	switch (cp->type) {
	case IMX_MU_TYPE_TX:
		imx_mu_xcr_rmw(priv, 0, IMX_MU_xCR_TIEn(cp->idx));
		break;
	case IMX_MU_TYPE_RX:
		imx_mu_xcr_rmw(priv, 0, IMX_MU_xCR_RIEn(cp->idx));
		break;
	case IMX_MU_TYPE_RXDB:
		imx_mu_xcr_rmw(priv, 0, IMX_MU_xCR_GIEn(cp->idx));
		break;
	default:
		break;
	}

	free_irq(priv->irq, chan);
}

static const struct mbox_chan_ops imx_mu_ops = {
	.send_data = imx_mu_send_data,
	.startup = imx_mu_startup,
	.shutdown = imx_mu_shutdown,
};

static struct mbox_chan * imx_mu_xlate(struct mbox_controller *mbox,
				       const struct of_phandle_args *sp)
{
	u32 type, idx, chan;

	if (sp->args_count != 2) {
		dev_err(mbox->dev, "Invalid argument count %d\n", sp->args_count);
		return ERR_PTR(-EINVAL);
	}

	type = sp->args[0]; /* channel type */
	idx = sp->args[1]; /* index */
	chan = type * 4 + idx;

	if (chan >= mbox->num_chans) {
		dev_err(mbox->dev, "Not supported channel number: %d. (type: %d, idx: %d)\n", chan, type, idx);
		return ERR_PTR(-EINVAL);
	}

	return &mbox->chans[chan];
}

static void imx_mu_init_generic(struct imx_mu_priv *priv)
{
	if (priv->side_b)
		return;

	/* Set default MU configuration */
	imx_mu_write(priv, 0, priv->dcfg->xCR);
}

static int imx_mu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct imx_mu_priv *priv;
	const struct imx_mu_dcfg *dcfg;
	unsigned int i;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0)
		return priv->irq;

	dcfg = of_device_get_match_data(dev);
	if (!dcfg)
		return -EINVAL;
	priv->dcfg = dcfg;

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		if (PTR_ERR(priv->clk) != -ENOENT)
			return PTR_ERR(priv->clk);

		priv->clk = NULL;
	}

	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(dev, "Failed to enable clock\n");
		return ret;
	}

	for (i = 0; i < IMX_MU_CHANS; i++) {
		struct imx_mu_con_priv *cp = &priv->con_priv[i];

		cp->idx = i % 4;
		cp->type = i >> 2;
		cp->chan = &priv->mbox_chans[i];
		priv->mbox_chans[i].con_priv = cp;
		snprintf(cp->irq_desc, sizeof(cp->irq_desc),
			 "imx_mu_chan[%i-%i]", cp->type, cp->idx);
	}

	priv->side_b = of_property_read_bool(np, "fsl,mu-side-b");

	spin_lock_init(&priv->xcr_lock);

	priv->mbox.dev = dev;
	priv->mbox.ops = &imx_mu_ops;
	priv->mbox.chans = priv->mbox_chans;
	priv->mbox.num_chans = IMX_MU_CHANS;
	priv->mbox.of_xlate = imx_mu_xlate;
	priv->mbox.txdone_irq = true;

	platform_set_drvdata(pdev, priv);

	imx_mu_init_generic(priv);

	return devm_mbox_controller_register(dev, &priv->mbox);
}

static int imx_mu_remove(struct platform_device *pdev)
{
	struct imx_mu_priv *priv = platform_get_drvdata(pdev);

	clk_disable_unprepare(priv->clk);

	return 0;
}

static const struct of_device_id imx_mu_dt_ids[] = {
	{ .compatible = "fsl,imx7ulp-mu", .data = &imx_mu_cfg_imx7ulp },
	{ .compatible = "fsl,imx6sx-mu", .data = &imx_mu_cfg_imx6sx },
	{ },
};
MODULE_DEVICE_TABLE(of, imx_mu_dt_ids);

static struct platform_driver imx_mu_driver = {
	.probe		= imx_mu_probe,
	.remove		= imx_mu_remove,
	.driver = {
		.name	= "imx_mu",
		.of_match_table = imx_mu_dt_ids,
	},
};
module_platform_driver(imx_mu_driver);

MODULE_AUTHOR("Oleksij Rempel <o.rempel@pengutronix.de>");
MODULE_DESCRIPTION("Message Unit driver for i.MX");
MODULE_LICENSE("GPL v2");
