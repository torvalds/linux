// SPDX-License-Identifier: GPL-2.0+
/*
 *  lpc32xx_adc.c - Support for ADC in LPC32XX
 *
 *  3-channel, 10-bit ADC
 *
 *  Copyright (C) 2011, 2012 Roland Stigge <stigge@antcom.de>
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/*
 * LPC32XX registers definitions
 */
#define LPC32XXAD_SELECT(x)	((x) + 0x04)
#define LPC32XXAD_CTRL(x)	((x) + 0x08)
#define LPC32XXAD_VALUE(x)	((x) + 0x48)

/* Bit definitions for LPC32XXAD_SELECT: */
/* constant, always write this value! */
#define LPC32XXAD_REFm         0x00000200
/* constant, always write this value! */
#define LPC32XXAD_REFp		0x00000080
 /* multiple of this is the channel number: 0, 1, 2 */
#define LPC32XXAD_IN		0x00000010
/* constant, always write this value! */
#define LPC32XXAD_INTERNAL	0x00000004

/* Bit definitions for LPC32XXAD_CTRL: */
#define LPC32XXAD_STROBE	0x00000002
#define LPC32XXAD_PDN_CTRL	0x00000004

/* Bit definitions for LPC32XXAD_VALUE: */
#define LPC32XXAD_VALUE_MASK	0x000003FF

#define LPC32XXAD_NAME "lpc32xx-adc"

struct lpc32xx_adc_state {
	void __iomem *adc_base;
	struct clk *clk;
	struct completion completion;

	u32 value;
};

static int lpc32xx_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val,
			    int *val2,
			    long mask)
{
	struct lpc32xx_adc_state *st = iio_priv(indio_dev);
	int ret;
	if (mask == IIO_CHAN_INFO_RAW) {
		mutex_lock(&indio_dev->mlock);
		ret = clk_prepare_enable(st->clk);
		if (ret) {
			mutex_unlock(&indio_dev->mlock);
			return ret;
		}
		/* Measurement setup */
		__raw_writel(LPC32XXAD_INTERNAL | (chan->address) |
			     LPC32XXAD_REFp | LPC32XXAD_REFm,
			     LPC32XXAD_SELECT(st->adc_base));
		/* Trigger conversion */
		__raw_writel(LPC32XXAD_PDN_CTRL | LPC32XXAD_STROBE,
			     LPC32XXAD_CTRL(st->adc_base));
		wait_for_completion(&st->completion); /* set by ISR */
		clk_disable_unprepare(st->clk);
		*val = st->value;
		mutex_unlock(&indio_dev->mlock);

		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static const struct iio_info lpc32xx_adc_iio_info = {
	.read_raw = &lpc32xx_read_raw,
};

#define LPC32XX_ADC_CHANNEL(_index) {			\
	.type = IIO_VOLTAGE,				\
	.indexed = 1,					\
	.channel = _index,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
	.address = LPC32XXAD_IN * _index,		\
	.scan_index = _index,				\
}

static const struct iio_chan_spec lpc32xx_adc_iio_channels[] = {
	LPC32XX_ADC_CHANNEL(0),
	LPC32XX_ADC_CHANNEL(1),
	LPC32XX_ADC_CHANNEL(2),
};

static irqreturn_t lpc32xx_adc_isr(int irq, void *dev_id)
{
	struct lpc32xx_adc_state *st = dev_id;

	/* Read value and clear irq */
	st->value = __raw_readl(LPC32XXAD_VALUE(st->adc_base)) &
		LPC32XXAD_VALUE_MASK;
	complete(&st->completion);

	return IRQ_HANDLED;
}

static int lpc32xx_adc_probe(struct platform_device *pdev)
{
	struct lpc32xx_adc_state *st = NULL;
	struct resource *res;
	int retval = -ENODEV;
	struct iio_dev *iodev = NULL;
	int irq;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get platform I/O memory\n");
		return -ENXIO;
	}

	iodev = devm_iio_device_alloc(&pdev->dev, sizeof(*st));
	if (!iodev)
		return -ENOMEM;

	st = iio_priv(iodev);

	st->adc_base = devm_ioremap(&pdev->dev, res->start,
				    resource_size(res));
	if (!st->adc_base) {
		dev_err(&pdev->dev, "failed mapping memory\n");
		return -EBUSY;
	}

	st->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(st->clk)) {
		dev_err(&pdev->dev, "failed getting clock\n");
		return PTR_ERR(st->clk);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(&pdev->dev, "failed getting interrupt resource\n");
		return -ENXIO;
	}

	retval = devm_request_irq(&pdev->dev, irq, lpc32xx_adc_isr, 0,
				  LPC32XXAD_NAME, st);
	if (retval < 0) {
		dev_err(&pdev->dev, "failed requesting interrupt\n");
		return retval;
	}

	platform_set_drvdata(pdev, iodev);

	init_completion(&st->completion);

	iodev->name = LPC32XXAD_NAME;
	iodev->dev.parent = &pdev->dev;
	iodev->info = &lpc32xx_adc_iio_info;
	iodev->modes = INDIO_DIRECT_MODE;
	iodev->channels = lpc32xx_adc_iio_channels;
	iodev->num_channels = ARRAY_SIZE(lpc32xx_adc_iio_channels);

	retval = devm_iio_device_register(&pdev->dev, iodev);
	if (retval)
		return retval;

	dev_info(&pdev->dev, "LPC32XX ADC driver loaded, IRQ %d\n", irq);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id lpc32xx_adc_match[] = {
	{ .compatible = "nxp,lpc3220-adc" },
	{},
};
MODULE_DEVICE_TABLE(of, lpc32xx_adc_match);
#endif

static struct platform_driver lpc32xx_adc_driver = {
	.probe		= lpc32xx_adc_probe,
	.driver		= {
		.name	= LPC32XXAD_NAME,
		.of_match_table = of_match_ptr(lpc32xx_adc_match),
	},
};

module_platform_driver(lpc32xx_adc_driver);

MODULE_AUTHOR("Roland Stigge <stigge@antcom.de>");
MODULE_DESCRIPTION("LPC32XX ADC driver");
MODULE_LICENSE("GPL");
