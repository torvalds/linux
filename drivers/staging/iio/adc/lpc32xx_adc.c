/*
 *  lpc32xx_adc.c - Support for ADC in LPC32XX
 *
 *  3-channel, 10-bit ADC
 *
 *  Copyright (C) 2011, 2012 Roland Stigge <stigge@antcom.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/completion.h>
#include <linux/of.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

/*
 * LPC32XX registers definitions
 */
#define LPC32XX_ADC_SELECT(x)	((x) + 0x04)
#define LPC32XX_ADC_CTRL(x)	((x) + 0x08)
#define LPC32XX_ADC_VALUE(x)	((x) + 0x48)

/* Bit definitions for LPC32XX_ADC_SELECT: */
#define AD_REFm         0x00000200 /* constant, always write this value! */
#define AD_REFp		0x00000080 /* constant, always write this value! */
#define AD_IN		0x00000010 /* multiple of this is the */
				   /* channel number: 0, 1, 2 */
#define AD_INTERNAL	0x00000004 /* constant, always write this value! */

/* Bit definitions for LPC32XX_ADC_CTRL: */
#define AD_STROBE	0x00000002
#define AD_PDN_CTRL	0x00000004

/* Bit definitions for LPC32XX_ADC_VALUE: */
#define ADC_VALUE_MASK	0x000003FF

#define MOD_NAME "lpc32xx-adc"

struct lpc32xx_adc_info {
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
	struct lpc32xx_adc_info *info = iio_priv(indio_dev);

	if (mask == IIO_CHAN_INFO_RAW) {
		mutex_lock(&indio_dev->mlock);
		clk_enable(info->clk);
		/* Measurement setup */
		__raw_writel(AD_INTERNAL | (chan->address) | AD_REFp | AD_REFm,
			LPC32XX_ADC_SELECT(info->adc_base));
		/* Trigger conversion */
		__raw_writel(AD_PDN_CTRL | AD_STROBE,
			LPC32XX_ADC_CTRL(info->adc_base));
		wait_for_completion(&info->completion); /* set by ISR */
		clk_disable(info->clk);
		*val = info->value;
		mutex_unlock(&indio_dev->mlock);

		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static const struct iio_info lpc32xx_adc_iio_info = {
	.read_raw = &lpc32xx_read_raw,
	.driver_module = THIS_MODULE,
};

#define LPC32XX_ADC_CHANNEL(_index) {			\
	.type = IIO_VOLTAGE,				\
	.indexed = 1,					\
	.channel = _index,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
	.address = AD_IN * _index,			\
	.scan_index = _index,				\
}

static const struct iio_chan_spec lpc32xx_adc_iio_channels[] = {
	LPC32XX_ADC_CHANNEL(0),
	LPC32XX_ADC_CHANNEL(1),
	LPC32XX_ADC_CHANNEL(2),
};

static irqreturn_t lpc32xx_adc_isr(int irq, void *dev_id)
{
	struct lpc32xx_adc_info *info = dev_id;

	/* Read value and clear irq */
	info->value = __raw_readl(LPC32XX_ADC_VALUE(info->adc_base)) &
				ADC_VALUE_MASK;
	complete(&info->completion);

	return IRQ_HANDLED;
}

static int lpc32xx_adc_probe(struct platform_device *pdev)
{
	struct lpc32xx_adc_info *info = NULL;
	struct resource *res;
	int retval = -ENODEV;
	struct iio_dev *iodev = NULL;
	int irq;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get platform I/O memory\n");
		return -EBUSY;
	}

	iodev = devm_iio_device_alloc(&pdev->dev, sizeof(*info));
	if (!iodev)
		return -ENOMEM;

	info = iio_priv(iodev);

	info->adc_base = devm_ioremap(&pdev->dev, res->start,
						resource_size(res));
	if (!info->adc_base) {
		dev_err(&pdev->dev, "failed mapping memory\n");
		return -EBUSY;
	}

	info->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(info->clk)) {
		dev_err(&pdev->dev, "failed getting clock\n");
		return PTR_ERR(info->clk);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(&pdev->dev, "failed getting interrupt resource\n");
		return -EINVAL;
	}

	retval = devm_request_irq(&pdev->dev, irq, lpc32xx_adc_isr, 0,
								MOD_NAME, info);
	if (retval < 0) {
		dev_err(&pdev->dev, "failed requesting interrupt\n");
		return retval;
	}

	platform_set_drvdata(pdev, iodev);

	init_completion(&info->completion);

	iodev->name = MOD_NAME;
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
		.name	= MOD_NAME,
		.of_match_table = of_match_ptr(lpc32xx_adc_match),
	},
};

module_platform_driver(lpc32xx_adc_driver);

MODULE_AUTHOR("Roland Stigge <stigge@antcom.de>");
MODULE_DESCRIPTION("LPC32XX ADC driver");
MODULE_LICENSE("GPL");
