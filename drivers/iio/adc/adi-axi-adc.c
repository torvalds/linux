// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices Generic AXI ADC IP core
 * Link: https://wiki.analog.com/resources/fpga/docs/axi_adc_ip
 *
 * Copyright 2012-2020 Analog Devices Inc.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/module.h>
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

#define ADI_AXI_REG_CHAN_CTRL_DEFAULTS		\
	(ADI_AXI_REG_CHAN_CTRL_FMT_SIGNEXT |	\
	 ADI_AXI_REG_CHAN_CTRL_FMT_EN |		\
	 ADI_AXI_REG_CHAN_CTRL_ENABLE)

struct adi_axi_adc_state {
	struct regmap				*regmap;
	struct device				*dev;
};

static int axi_adc_enable(struct iio_backend *back)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);
	int ret;

	ret = regmap_set_bits(st->regmap, ADI_AXI_REG_RSTN,
			      ADI_AXI_REG_RSTN_MMCM_RSTN);
	if (ret)
		return ret;

	fsleep(10000);
	return regmap_set_bits(st->regmap, ADI_AXI_REG_RSTN,
			       ADI_AXI_REG_RSTN_RSTN | ADI_AXI_REG_RSTN_MMCM_RSTN);
}

static void axi_adc_disable(struct iio_backend *back)
{
	struct adi_axi_adc_state *st = iio_backend_get_priv(back);

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
	struct iio_buffer *buffer;
	const char *dma_name;
	int ret;

	if (device_property_read_string(st->dev, "dma-names", &dma_name))
		dma_name = "rx";

	buffer = iio_dmaengine_buffer_alloc(st->dev, dma_name);
	if (IS_ERR(buffer)) {
		dev_err(st->dev, "Could not get DMA buffer, %ld\n",
			PTR_ERR(buffer));
		return ERR_CAST(buffer);
	}

	indio_dev->modes |= INDIO_BUFFER_HARDWARE;
	ret = iio_device_attach_buffer(indio_dev, buffer);
	if (ret)
		return ERR_PTR(ret);

	return buffer;
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
	.max_register = 0x0800,
};

static const struct iio_backend_ops adi_axi_adc_generic = {
	.enable = axi_adc_enable,
	.disable = axi_adc_disable,
	.data_format_set = axi_adc_data_format_set,
	.chan_enable = axi_adc_chan_enable,
	.chan_disable = axi_adc_chan_disable,
	.request_buffer = axi_adc_request_buffer,
	.free_buffer = axi_adc_free_buffer,
};

static int adi_axi_adc_probe(struct platform_device *pdev)
{
	const unsigned int *expected_ver;
	struct adi_axi_adc_state *st;
	void __iomem *base;
	unsigned int ver;
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
		return PTR_ERR(st->regmap);

	expected_ver = device_get_match_data(&pdev->dev);
	if (!expected_ver)
		return -ENODEV;

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

	if (*expected_ver > ver) {
		dev_err(&pdev->dev,
			"IP core version is too old. Expected %d.%.2d.%c, Reported %d.%.2d.%c\n",
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
		return ret;

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
