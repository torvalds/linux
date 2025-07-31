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
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/property.h>
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
		regmap_set_bits(priv->regs, MX25_ADCQ_MR,
				MX25_ADCQ_MR_EOQ_IRQ);
		complete(&priv->completed);
	}

	/* Disable conversion queue run */
	regmap_clear_bits(priv->regs, MX25_ADCQ_CR, MX25_ADCQ_CR_FQS);

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
	long time_left;
	u32 data;

	/* Setup the configuration we want to use */
	regmap_write(priv->regs, MX25_ADCQ_ITEM_7_0,
		     MX25_ADCQ_ITEM(0, chan->channel));

	regmap_clear_bits(priv->regs, MX25_ADCQ_MR, MX25_ADCQ_MR_EOQ_IRQ);

	/* Trigger queue for one run */
	regmap_set_bits(priv->regs, MX25_ADCQ_CR, MX25_ADCQ_CR_FQS);

	time_left = wait_for_completion_interruptible_timeout(
		&priv->completed, MX25_GCQ_TIMEOUT);
	if (time_left < 0) {
		dev_err(dev, "ADC wait for measurement failed\n");
		return time_left;
	} else if (time_left == 0) {
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

static int mx25_gcq_ext_regulator_setup(struct device *dev,
					struct mx25_gcq_priv *priv, u32 refp)
{
	char reg_name[12];
	int ret;

	if (priv->vref[refp])
		return 0;

	ret = snprintf(reg_name, sizeof(reg_name), "vref-%s",
		       mx25_gcq_refp_names[refp]);
	if (ret < 0)
		return ret;

	priv->vref[refp] = devm_regulator_get_optional(dev, reg_name);
	if (IS_ERR(priv->vref[refp]))
		return dev_err_probe(dev, PTR_ERR(priv->vref[refp]),
				     "Error, trying to use external voltage reference without a %s regulator.",
				     reg_name);

	return 0;
}

static int mx25_gcq_setup_cfgs(struct platform_device *pdev,
			       struct mx25_gcq_priv *priv)
{
	struct device *dev = &pdev->dev;
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

	device_for_each_child_node_scoped(dev, child) {
		u32 reg;
		u32 refp = MX25_ADCQ_CFG_REFP_INT;
		u32 refn = MX25_ADCQ_CFG_REFN_NGND2;

		ret = fwnode_property_read_u32(child, "reg", &reg);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to get reg property\n");

		if (reg >= MX25_NUM_CFGS)
			return dev_err_probe(dev, -EINVAL,
				"reg value is greater than the number of available configuration registers\n");

		fwnode_property_read_u32(child, "fsl,adc-refp", &refp);
		fwnode_property_read_u32(child, "fsl,adc-refn", &refn);

		switch (refp) {
		case MX25_ADC_REFP_EXT:
		case MX25_ADC_REFP_XP:
		case MX25_ADC_REFP_YP:
			ret = mx25_gcq_ext_regulator_setup(&pdev->dev, priv, refp);
			if (ret)
				return ret;
			priv->channel_vref_mv[reg] =
				regulator_get_voltage(priv->vref[refp]);
			/* Conversion from uV to mV */
			priv->channel_vref_mv[reg] /= 1000;
			break;
		case MX25_ADC_REFP_INT:
			priv->channel_vref_mv[reg] = 2500;
			break;
		default:
			return dev_err_probe(dev, -EINVAL,
					     "Invalid positive reference %d\n", refp);
		}

		/*
		 * Shift the read values to the correct positions within the
		 * register.
		 */
		refp = MX25_ADCQ_CFG_REFP(refp);
		refn = MX25_ADCQ_CFG_REFN(refn);

		if ((refp & MX25_ADCQ_CFG_REFP_MASK) != refp)
			return dev_err_probe(dev, -EINVAL,
					     "Invalid fsl,adc-refp property value\n");

		if ((refn & MX25_ADCQ_CFG_REFN_MASK) != refn)
			return dev_err_probe(dev, -EINVAL,
					     "Invalid fsl,adc-refn property value\n");

		regmap_update_bits(priv->regs, MX25_ADCQ_CFG(reg),
				   MX25_ADCQ_CFG_REFP_MASK |
				   MX25_ADCQ_CFG_REFN_MASK,
				   refp | refn);
	}
	regmap_set_bits(priv->regs, MX25_ADCQ_CR,
			MX25_ADCQ_CR_FRST | MX25_ADCQ_CR_QRST);

	regmap_write(priv->regs, MX25_ADCQ_CR,
		     MX25_ADCQ_CR_PDMSK | MX25_ADCQ_CR_QSM_FQS);

	return 0;
}

static void mx25_gcq_reg_disable(void *reg)
{
	regulator_disable(reg);
}

/* Custom handling needed as this driver doesn't own the clock */
static void mx25_gcq_clk_disable(void *clk)
{
	clk_disable_unprepare(clk);
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
	if (IS_ERR(priv->regs))
		return dev_err_probe(dev, PTR_ERR(priv->regs),
				     "Failed to initialize regmap\n");

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
			return ret;

		ret = devm_add_action_or_reset(dev, mx25_gcq_reg_disable,
					       priv->vref[i]);
		if (ret)
			return ret;
	}

	priv->clk = tsadc->clk;
	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable clock\n");

	ret = devm_add_action_or_reset(dev, mx25_gcq_clk_disable,
				       priv->clk);
	if (ret)
		return ret;

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;

	priv->irq = ret;
	ret = devm_request_irq(dev, priv->irq, mx25_gcq_irq, 0, pdev->name,
			       priv);
	if (ret)
		return dev_err_probe(dev, ret, "Failed requesting IRQ\n");

	indio_dev->channels = mx25_gcq_channels;
	indio_dev->num_channels = ARRAY_SIZE(mx25_gcq_channels);
	indio_dev->info = &mx25_gcq_iio_info;
	indio_dev->name = driver_name;

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register iio device\n");

	return 0;
}

static const struct of_device_id mx25_gcq_ids[] = {
	{ .compatible = "fsl,imx25-gcq", },
	{ }
};
MODULE_DEVICE_TABLE(of, mx25_gcq_ids);

static struct platform_driver mx25_gcq_driver = {
	.driver		= {
		.name	= "mx25-gcq",
		.of_match_table = mx25_gcq_ids,
	},
	.probe		= mx25_gcq_probe,
};
module_platform_driver(mx25_gcq_driver);

MODULE_DESCRIPTION("ADC driver for Freescale mx25");
MODULE_AUTHOR("Markus Pargmann <mpa@pengutronix.de>");
MODULE_LICENSE("GPL v2");
