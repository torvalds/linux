// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices Generic AXI ADC IP core
 * Link: https://wiki.analog.com/resources/fpga/docs/axi_adc_ip
 *
 * Copyright 2012-2020 Analog Devices Inc.
 */

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <linux/fpga/adi-axi-common.h>

#include <linux/iio/backend.h>
#include <linux/iio/buffer-dmaengine.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>

/*
 * Register definitions:
 *   https://wiki.analog.com/resources/fpga/docs/axi_adc_ip#register_map
 */

/* ADC controls */

#define ADI_AXI_REG_RSTN			0x0040
#define   ADI_AXI_REG_RSTN_CE_N			BIT(2)
#define   ADI_AXI_REG_RSTN_MMCM_RSTN		BIT(1)
#define   ADI_AXI_REG_RSTN_RSTN			BIT(0)

#define ADI_AXI_ADC_REG_CTRL			0x0044
#define    ADI_AXI_ADC_CTRL_DDR_EDGESEL_MASK	BIT(1)

#define ADI_AXI_ADC_REG_DRP_STATUS		0x0074
#define   ADI_AXI_ADC_DRP_LOCKED		BIT(17)

/* ADC Channel controls */

#define ADI_AXI_REG_CHAN_CTRL(c)		(0x0400 + (c) * 0x40)
#define   ADI_AXI_REG_CHAN_CTRL_LB_OWR		BIT(11)
#define   ADI_AXI_REG_CHAN_CTRL_PN_SEL_OWR	BIT(10)
#define   ADI_AXI_REG_CHAN_CTRL_IQCOR_EN	BIT(9)
#define   ADI_AXI_REG_CHAN_CTRL_DCFILT_EN	BIT(8)
#define   ADI_AXI_REG_CHAN_CTRL_FMT_MASK	GENMASK(6, 4)
#define   ADI_AXI_REG_CHAN_CTRL_FMT_SIGNEXT	BIT(6)
#define   ADI_AXI_REG_CHAN_CTRL_FMT_TYPE	BIT(5)
#define   ADI_AXI_REG_CHAN_CTRL_FMT_EN		BIT(4)
#define   ADI_AXI_REG_CHAN_CTRL_PN_TYPE_OWR	BIT(1)
#define   ADI_AXI_REG_CHAN_CTRL_ENABLE		BIT(0)

#define ADI_AXI_ADC_REG_CHAN_STATUS(c)		(0x0404 + (c) * 0x40)
#define   ADI_AXI_ADC_CHAN_STAT_PN_MASK		GENMASK(2, 1)

#define ADI_AXI_ADC_REG_CHAN_CTRL_3(c)		(0x0418 + (c) * 0x40)
#define   ADI_AXI_ADC_CHAN_PN_SEL_MASK		GENMASK(19, 16)

/* IO Delays */
#define ADI_AXI_ADC_REG_DELAY(l)		(0x0800 + (l) * 0x4)
#define   AXI_ADC_DELAY_CTRL_MASK		GENMASK(4, 0)

#define ADI_AXI_ADC_MAX_IO_NUM_LANES		15

#define ADI_AXI_REG_CHAN_CTRL_DEFAULTS		\
	(ADI_AXI_REG_CHAN_CTRL_FMT_SIGNEXT |	\
	 ADI_AXI_REG_CHAN_CTRL_FMT_EN |		\
	 ADI_AXI_REG_CHAN_CTRL_ENABLE)

struct adi_axi_adc_state {
	struct regmap *regmap;
	struct device *dev;
	/* lock to protect multiple accesses to the device registers */
	struct mutex lock;
};

static int axi_adc_enable(struct iio_backend *back)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);
	unsigned int __val;
	int ret;

	guard(mutex)(&st->lock);
	ret = regmap_set_bits(st->regmap, ADI_AXI_REG_RSTN,
			      ADI_AXI_REG_RSTN_MMCM_RSTN);
	if (ret)
		return ret;

	/*
	 * Make sure the DRP (Dynamic Reconfiguration Port) is locked. Not all
	 * designs really use it but if they don't we still get the lock bit
	 * set. So let's do it all the time so the code is generic.
	 */
	ret = regmap_read_poll_timeout(st->regmap, ADI_AXI_ADC_REG_DRP_STATUS,
				       __val, __val & ADI_AXI_ADC_DRP_LOCKED,
				       100, 1000);
	if (ret)
		return ret;

	return regmap_set_bits(st->regmap, ADI_AXI_REG_RSTN,
			       ADI_AXI_REG_RSTN_RSTN | ADI_AXI_REG_RSTN_MMCM_RSTN);
}

static void axi_adc_disable(struct iio_backend *back)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);

	guard(mutex)(&st->lock);
	regmap_write(st->regmap, ADI_AXI_REG_RSTN, 0);
}

static int axi_adc_data_format_set(struct iio_backend *back, unsigned int chan,
				   const struct iio_backend_data_fmt *data)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);
	u32 val;

	if (!data->enable)
		return regmap_clear_bits(st->regmap,
					 ADI_AXI_REG_CHAN_CTRL(chan),
					 ADI_AXI_REG_CHAN_CTRL_FMT_EN);

	val = FIELD_PREP(ADI_AXI_REG_CHAN_CTRL_FMT_EN, true);
	if (data->sign_extend)
		val |= FIELD_PREP(ADI_AXI_REG_CHAN_CTRL_FMT_SIGNEXT, true);
	if (data->type == IIO_BACKEND_OFFSET_BINARY)
		val |= FIELD_PREP(ADI_AXI_REG_CHAN_CTRL_FMT_TYPE, true);

	return regmap_update_bits(st->regmap, ADI_AXI_REG_CHAN_CTRL(chan),
				  ADI_AXI_REG_CHAN_CTRL_FMT_MASK, val);
}

static int axi_adc_data_sample_trigger(struct iio_backend *back,
				       enum iio_backend_sample_trigger trigger)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);

	switch (trigger) {
	case IIO_BACKEND_SAMPLE_TRIGGER_EDGE_RISING:
		return regmap_clear_bits(st->regmap, ADI_AXI_ADC_REG_CTRL,
					 ADI_AXI_ADC_CTRL_DDR_EDGESEL_MASK);
	case IIO_BACKEND_SAMPLE_TRIGGER_EDGE_FALLING:
		return regmap_set_bits(st->regmap, ADI_AXI_ADC_REG_CTRL,
				       ADI_AXI_ADC_CTRL_DDR_EDGESEL_MASK);
	default:
		return -EINVAL;
	}
}

static int axi_adc_iodelays_set(struct iio_backend *back, unsigned int lane,
				unsigned int tap)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);
	int ret;
	u32 val;

	if (tap > FIELD_MAX(AXI_ADC_DELAY_CTRL_MASK))
		return -EINVAL;
	if (lane > ADI_AXI_ADC_MAX_IO_NUM_LANES)
		return -EINVAL;

	guard(mutex)(&st->lock);
	ret = regmap_write(st->regmap, ADI_AXI_ADC_REG_DELAY(lane), tap);
	if (ret)
		return ret;
	/*
	 * If readback is ~0, that means there are issues with the
	 * delay_clk.
	 */
	ret = regmap_read(st->regmap, ADI_AXI_ADC_REG_DELAY(lane), &val);
	if (ret)
		return ret;
	if (val == U32_MAX)
		return -EIO;

	return 0;
}

static int axi_adc_test_pattern_set(struct iio_backend *back,
				    unsigned int chan,
				    enum iio_backend_test_pattern pattern)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);

	switch (pattern) {
	case IIO_BACKEND_NO_TEST_PATTERN:
		/* nothing to do */
		return 0;
	case IIO_BACKEND_ADI_PRBS_9A:
		return regmap_update_bits(st->regmap, ADI_AXI_ADC_REG_CHAN_CTRL_3(chan),
					  ADI_AXI_ADC_CHAN_PN_SEL_MASK,
					  FIELD_PREP(ADI_AXI_ADC_CHAN_PN_SEL_MASK, 0));
	default:
		return -EINVAL;
	}
}

static int axi_adc_chan_status(struct iio_backend *back, unsigned int chan,
			       bool *error)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);
	int ret;
	u32 val;

	guard(mutex)(&st->lock);
	/* reset test bits by setting them */
	ret = regmap_write(st->regmap, ADI_AXI_ADC_REG_CHAN_STATUS(chan),
			   ADI_AXI_ADC_CHAN_STAT_PN_MASK);
	if (ret)
		return ret;

	/* let's give enough time to validate or erroring the incoming pattern */
	fsleep(1000);

	ret = regmap_read(st->regmap, ADI_AXI_ADC_REG_CHAN_STATUS(chan), &val);
	if (ret)
		return ret;

	if (ADI_AXI_ADC_CHAN_STAT_PN_MASK & val)
		*error = true;
	else
		*error = false;

	return 0;
}

static int axi_adc_chan_enable(struct iio_backend *back, unsigned int chan)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);

	return regmap_set_bits(st->regmap, ADI_AXI_REG_CHAN_CTRL(chan),
			       ADI_AXI_REG_CHAN_CTRL_ENABLE);
}

static int axi_adc_chan_disable(struct iio_backend *back, unsigned int chan)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);

	return regmap_clear_bits(st->regmap, ADI_AXI_REG_CHAN_CTRL(chan),
				 ADI_AXI_REG_CHAN_CTRL_ENABLE);
}

static struct iio_buffer *axi_adc_request_buffer(struct iio_backend *back,
						 struct iio_dev *indio_dev)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);
	const char *dma_name;

	if (device_property_read_string(st->dev, "dma-names", &dma_name))
		dma_name = "rx";

	return iio_dmaengine_buffer_setup(st->dev, indio_dev, dma_name);
}

static void axi_adc_free_buffer(struct iio_backend *back,
				struct iio_buffer *buffer)
{
	iio_dmaengine_buffer_free(buffer);
}

static const struct regmap_config axi_adc_regmap_config = {
	.val_bits = 32,
	.reg_bits = 32,
	.reg_stride = 4,
};

static const struct iio_backend_ops adi_axi_adc_generic = {
	.enable = axi_adc_enable,
	.disable = axi_adc_disable,
	.data_format_set = axi_adc_data_format_set,
	.chan_enable = axi_adc_chan_enable,
	.chan_disable = axi_adc_chan_disable,
	.request_buffer = axi_adc_request_buffer,
	.free_buffer = axi_adc_free_buffer,
	.data_sample_trigger = axi_adc_data_sample_trigger,
	.iodelay_set = axi_adc_iodelays_set,
	.test_pattern_set = axi_adc_test_pattern_set,
	.chan_status = axi_adc_chan_status,
};

static int adi_axi_adc_probe(struct platform_device *pdev)
{
	const unsigned int *expected_ver;
	struct adi_axi_adc_state *st;
	void __iomem *base;
	unsigned int ver;
	struct clk *clk;
	int ret;

	st = devm_kzalloc(&pdev->dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	st->dev = &pdev->dev;
	st->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					   &axi_adc_regmap_config);
	if (IS_ERR(st->regmap))
		return dev_err_probe(&pdev->dev, PTR_ERR(st->regmap),
				     "failed to init register map\n");

	expected_ver = device_get_match_data(&pdev->dev);
	if (!expected_ver)
		return -ENODEV;

	clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(clk),
				     "failed to get clock\n");

	/*
	 * Force disable the core. Up to the frontend to enable us. And we can
	 * still read/write registers...
	 */
	ret = regmap_write(st->regmap, ADI_AXI_REG_RSTN, 0);
	if (ret)
		return ret;

	ret = regmap_read(st->regmap, ADI_AXI_REG_VERSION, &ver);
	if (ret)
		return ret;

	if (ADI_AXI_PCORE_VER_MAJOR(ver) != ADI_AXI_PCORE_VER_MAJOR(*expected_ver)) {
		dev_err(&pdev->dev,
			"Major version mismatch. Expected %d.%.2d.%c, Reported %d.%.2d.%c\n",
			ADI_AXI_PCORE_VER_MAJOR(*expected_ver),
			ADI_AXI_PCORE_VER_MINOR(*expected_ver),
			ADI_AXI_PCORE_VER_PATCH(*expected_ver),
			ADI_AXI_PCORE_VER_MAJOR(ver),
			ADI_AXI_PCORE_VER_MINOR(ver),
			ADI_AXI_PCORE_VER_PATCH(ver));
		return -ENODEV;
	}

	ret = devm_iio_backend_register(&pdev->dev, &adi_axi_adc_generic, st);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register iio backend\n");

	dev_info(&pdev->dev, "AXI ADC IP core (%d.%.2d.%c) probed\n",
		 ADI_AXI_PCORE_VER_MAJOR(ver),
		 ADI_AXI_PCORE_VER_MINOR(ver),
		 ADI_AXI_PCORE_VER_PATCH(ver));

	return 0;
}

static unsigned int adi_axi_adc_10_0_a_info = ADI_AXI_PCORE_VER(10, 0, 'a');

/* Match table for of_platform binding */
static const struct of_device_id adi_axi_adc_of_match[] = {
	{ .compatible = "adi,axi-adc-10.0.a", .data = &adi_axi_adc_10_0_a_info },
	{ /* end of list */ }
};
MODULE_DEVICE_TABLE(of, adi_axi_adc_of_match);

static struct platform_driver adi_axi_adc_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = adi_axi_adc_of_match,
	},
	.probe = adi_axi_adc_probe,
};
module_platform_driver(adi_axi_adc_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices Generic AXI ADC IP core driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_DMAENGINE_BUFFER);
MODULE_IMPORT_NS(IIO_BACKEND);
