// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014-2015 Pengutronix, Markus Pargmann <mpa@pengutronix.de>
 *
 * This is the driver for the imx25 GCQ (Generic Conversion Queue)
 * connected to the imx25 ADC.
 */

#include <dt-bindings/iio/adc/fsl-imx25-gcq.h>
#include <linux/clk.h>
#include <linux/iio/iio.h>
#include <linux/interrupt.h>
#include <linux/mfd/imx25-tsadc.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#define MX25_GCQ_TIMEOUT (msecs_to_jiffies(2000))

static const char * const driver_name = "mx25-gcq";

enum mx25_gcq_cfgs {
	MX25_CFG_XP = 0,
	MX25_CFG_YP,
	MX25_CFG_XN,
	MX25_CFG_YN,
	MX25_CFG_WIPER,
	MX25_CFG_INAUX0,
	MX25_CFG_INAUX1,
	MX25_CFG_INAUX2,
	MX25_NUM_CFGS,
};

struct mx25_gcq_priv {
	struct regmap *regs;
	struct completion completed;
	struct clk *clk;
	int irq;
	struct regulator *vref[4];
	u32 channel_vref_mv[MX25_NUM_CFGS];
	/*
	 * Lock to protect the device state during a potential concurrent
	 * read access from userspace. Reading a raw value requires a sequence
	 * of register writes, then a wait for a completion callback,
	 * and finally a register read, during which userspace could issue
	 * another read request. This lock protects a read access from
	 * ocurring before another one has finished.
	 */
	struct mutex lock;
};

#define MX25_CQG_CHAN(chan, id) {\
	.type = IIO_VOLTAGE,\
	.indexed = 1,\
	.channel = chan,\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			      BIT(IIO_CHAN_INFO_SCALE),\
	.datasheet_name = id,\
}

static const struct iio_chan_spec mx25_gcq_channels[MX25_NUM_CFGS] = {
	MX25_CQG_CHAN(MX25_CFG_XP, "xp"),
	MX25_CQG_CHAN(MX25_CFG_YP, "yp"),
	MX25_CQG_CHAN(MX25_CFG_XN, "xn"),
	MX25_CQG_CHAN(MX25_CFG_YN, "yn"),
	MX25_CQG_CHAN(MX25_CFG_WIPER, "wiper"),
	MX25_CQG_CHAN(MX25_CFG_INAUX0, "inaux0"),
	MX25_CQG_CHAN(MX25_CFG_INAUX1, "inaux1"),
	MX25_CQG_CHAN(MX25_CFG_INAUX2, "inaux2"),
};

static const char * const mx25_gcq_refp_names[] = {
	[MX25_ADC_REFP_YP] = "yp",
	[MX25_ADC_REFP_XP] = "xp",
	[MX25_ADC_REFP_INT] = "int",
	[MX25_ADC_REFP_EXT] = "ext",
};

static irqreturn_t mx25_gcq_irq(int irq, void *data)
{
	struct mx25_gcq_priv *priv = data;
	u32 stats;

	regmap_read(priv->regs, MX25_ADCQ_SR, &stats);

	if (stats & MX25_ADCQ_SR_EOQ) {
		regmap_update_bits(priv->regs, MX25_ADCQ_MR,
				   MX25_ADCQ_MR_EOQ_IRQ, MX25_ADCQ_MR_EOQ_IRQ);
		complete(&priv->completed);
	}

	/* Disable conversion queue run */
	regmap_update_bits(priv->regs, MX25_ADCQ_CR, MX25_ADCQ_CR_FQS, 0);

	/* Acknowledge all possible irqs */
	regmap_write(priv->regs, MX25_ADCQ_SR, MX25_ADCQ_SR_FRR |
		     MX25_ADCQ_SR_FUR | MX25_ADCQ_SR_FOR |
		     MX25_ADCQ_SR_EOQ | MX25_ADCQ_SR_PD);

	return IRQ_HANDLED;
}

static int mx25_gcq_get_raw_value(struct device *dev,
				  struct iio_chan_spec const *chan,
				  struct mx25_gcq_priv *priv,
				  int *val)
{
	long timeout;
	u32 data;

	/* Setup the configuration we want to use */
	regmap_write(priv->regs, MX25_ADCQ_ITEM_7_0,
		     MX25_ADCQ_ITEM(0, chan->channel));

	regmap_update_bits(priv->regs, MX25_ADCQ_MR, MX25_ADCQ_MR_EOQ_IRQ, 0);

	/* Trigger queue for one run */
	regmap_update_bits(priv->regs, MX25_ADCQ_CR, MX25_ADCQ_CR_FQS,
			   MX25_ADCQ_CR_FQS);

	timeout = wait_for_completion_interruptible_timeout(
		&priv->completed, MX25_GCQ_TIMEOUT);
	if (timeout < 0) {
		dev_err(dev, "ADC wait for measurement failed\n");
		return timeout;
	} else if (timeout == 0) {
		dev_err(dev, "ADC timed out\n");
		return -ETIMEDOUT;
	}

	regmap_read(priv->regs, MX25_ADCQ_FIFO, &data);

	*val = MX25_ADCQ_FIFO_DATA(data);

	return IIO_VAL_INT;
}

static int mx25_gcq_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int *val,
			     int *val2, long mask)
{
	struct mx25_gcq_priv *priv = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&priv->lock);
		ret = mx25_gcq_get_raw_value(&indio_dev->dev, chan, priv, val);
		mutex_unlock(&priv->lock);
		return ret;

	case IIO_CHAN_INFO_SCALE:
		*val = priv->channel_vref_mv[chan->channel];
		*val2 = 12;
		return IIO_VAL_FRACTIONAL_LOG2;

	default:
		return -EINVAL;
	}
}

static const struct iio_info mx25_gcq_iio_info = {
	.read_raw = mx25_gcq_read_raw,
};

static const struct regmap_config mx25_gcq_regconfig = {
	.max_register = 0x5c,
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static int mx25_gcq_setup_cfgs(struct platform_device *pdev,
			       struct mx25_gcq_priv *priv)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child;
	struct device *dev = &pdev->dev;
	unsigned int refp_used[4] = {};
	int ret, i;

	/*
	 * Setup all configurations registers with a default conversion
	 * configuration for each input
	 */
	for (i = 0; i < MX25_NUM_CFGS; ++i)
		regmap_write(priv->regs, MX25_ADCQ_CFG(i),
			     MX25_ADCQ_CFG_YPLL_OFF |
			     MX25_ADCQ_CFG_XNUR_OFF |
			     MX25_ADCQ_CFG_XPUL_OFF |
			     MX25_ADCQ_CFG_REFP_INT |
			     MX25_ADCQ_CFG_IN(i) |
			     MX25_ADCQ_CFG_REFN_NGND2);

	/*
	 * First get all regulators to store them in channel_vref_mv if
	 * necessary. Later we use that information for proper IIO scale
	 * information.
	 */
	priv->vref[MX25_ADC_REFP_INT] = NULL;
	priv->vref[MX25_ADC_REFP_EXT] =
		devm_regulator_get_optional(dev, "vref-ext");
	priv->vref[MX25_ADC_REFP_XP] =
		devm_regulator_get_optional(dev, "vref-xp");
	priv->vref[MX25_ADC_REFP_YP] =
		devm_regulator_get_optional(dev, "vref-yp");

	for_each_child_of_node(np, child) {
		u32 reg;
		u32 refp = MX25_ADCQ_CFG_REFP_INT;
		u32 refn = MX25_ADCQ_CFG_REFN_NGND2;

		ret = of_property_read_u32(child, "reg", &reg);
		if (ret) {
			dev_err(dev, "Failed to get reg property\n");
			of_node_put(child);
			return ret;
		}

		if (reg >= MX25_NUM_CFGS) {
			dev_err(dev,
				"reg value is greater than the number of available configuration registers\n");
			of_node_put(child);
			return -EINVAL;
		}

		of_property_read_u32(child, "fsl,adc-refp", &refp);
		of_property_read_u32(child, "fsl,adc-refn", &refn);

		switch (refp) {
		case MX25_ADC_REFP_EXT:
		case MX25_ADC_REFP_XP:
		case MX25_ADC_REFP_YP:
			if (IS_ERR(priv->vref[refp])) {
				dev_err(dev, "Error, trying to use external voltage reference without a vref-%s regulator.",
					mx25_gcq_refp_names[refp]);
				of_node_put(child);
				return PTR_ERR(priv->vref[refp]);
			}
			priv->channel_vref_mv[reg] =
				regulator_get_voltage(priv->vref[refp]);
			/* Conversion from uV to mV */
			priv->channel_vref_mv[reg] /= 1000;
			break;
		case MX25_ADC_REFP_INT:
			priv->channel_vref_mv[reg] = 2500;
			break;
		default:
			dev_err(dev, "Invalid positive reference %d\n", refp);
			of_node_put(child);
			return -EINVAL;
		}

		++refp_used[refp];

		/*
		 * Shift the read values to the correct positions within the
		 * register.
		 */
		refp = MX25_ADCQ_CFG_REFP(refp);
		refn = MX25_ADCQ_CFG_REFN(refn);

		if ((refp & MX25_ADCQ_CFG_REFP_MASK) != refp) {
			dev_err(dev, "Invalid fsl,adc-refp property value\n");
			of_node_put(child);
			return -EINVAL;
		}
		if ((refn & MX25_ADCQ_CFG_REFN_MASK) != refn) {
			dev_err(dev, "Invalid fsl,adc-refn property value\n");
			of_node_put(child);
			return -EINVAL;
		}

		regmap_update_bits(priv->regs, MX25_ADCQ_CFG(reg),
				   MX25_ADCQ_CFG_REFP_MASK |
				   MX25_ADCQ_CFG_REFN_MASK,
				   refp | refn);
	}
	regmap_update_bits(priv->regs, MX25_ADCQ_CR,
			   MX25_ADCQ_CR_FRST | MX25_ADCQ_CR_QRST,
			   MX25_ADCQ_CR_FRST | MX25_ADCQ_CR_QRST);

	regmap_write(priv->regs, MX25_ADCQ_CR,
		     MX25_ADCQ_CR_PDMSK | MX25_ADCQ_CR_QSM_FQS);

	/* Remove unused regulators */
	for (i = 0; i != 4; ++i) {
		if (!refp_used[i]) {
			if (!IS_ERR_OR_NULL(priv->vref[i]))
				devm_regulator_put(priv->vref[i]);
			priv->vref[i] = NULL;
		}
	}

	return 0;
}

static int mx25_gcq_probe(struct platform_device *pdev)
{
	struct iio_dev *indio_dev;
	struct mx25_gcq_priv *priv;
	struct mx25_tsadc *tsadc = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	void __iomem *mem;
	int ret;
	int i;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*priv));
	if (!indio_dev)
		return -ENOMEM;

	priv = iio_priv(indio_dev);

	mem = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mem))
		return PTR_ERR(mem);

	priv->regs = devm_regmap_init_mmio(dev, mem, &mx25_gcq_regconfig);
	if (IS_ERR(priv->regs)) {
		dev_err(dev, "Failed to initialize regmap\n");
		return PTR_ERR(priv->regs);
	}

	mutex_init(&priv->lock);

	init_completion(&priv->completed);

	ret = mx25_gcq_setup_cfgs(pdev, priv);
	if (ret)
		return ret;

	for (i = 0; i != 4; ++i) {
		if (!priv->vref[i])
			continue;

		ret = regulator_enable(priv->vref[i]);
		if (ret)
			goto err_regulator_disable;
	}

	priv->clk = tsadc->clk;
	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(dev, "Failed to enable clock\n");
		goto err_vref_disable;
	}

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		goto err_clk_unprepare;

	priv->irq = ret;
	ret = request_irq(priv->irq, mx25_gcq_irq, 0, pdev->name, priv);
	if (ret) {
		dev_err(dev, "Failed requesting IRQ\n");
		goto err_clk_unprepare;
	}

	indio_dev->channels = mx25_gcq_channels;
	indio_dev->num_channels = ARRAY_SIZE(mx25_gcq_channels);
	indio_dev->info = &mx25_gcq_iio_info;
	indio_dev->name = driver_name;

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(dev, "Failed to register iio device\n");
		goto err_irq_free;
	}

	platform_set_drvdata(pdev, indio_dev);

	return 0;

err_irq_free:
	free_irq(priv->irq, priv);
err_clk_unprepare:
	clk_disable_unprepare(priv->clk);
err_vref_disable:
	i = 4;
err_regulator_disable:
	for (; i-- > 0;) {
		if (priv->vref[i])
			regulator_disable(priv->vref[i]);
	}
	return ret;
}

static int mx25_gcq_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct mx25_gcq_priv *priv = iio_priv(indio_dev);
	int i;

	iio_device_unregister(indio_dev);
	free_irq(priv->irq, priv);
	clk_disable_unprepare(priv->clk);
	for (i = 4; i-- > 0;) {
		if (priv->vref[i])
			regulator_disable(priv->vref[i]);
	}

	return 0;
}

static const struct of_device_id mx25_gcq_ids[] = {
	{ .compatible = "fsl,imx25-gcq", },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, mx25_gcq_ids);

static struct platform_driver mx25_gcq_driver = {
	.driver		= {
		.name	= "mx25-gcq",
		.of_match_table = mx25_gcq_ids,
	},
	.probe		= mx25_gcq_probe,
	.remove		= mx25_gcq_remove,
};
module_platform_driver(mx25_gcq_driver);

MODULE_DESCRIPTION("ADC driver for Freescale mx25");
MODULE_AUTHOR("Markus Pargmann <mpa@pengutronix.de>");
MODULE_LICENSE("GPL v2");
