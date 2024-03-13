// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices Generic AXI ADC IP core
 * Link: https://wiki.analog.com/resources/fpga/docs/axi_adc_ip
 *
 * Copyright 2012-2020 Analog Devices Inc.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/buffer-dmaengine.h>

#include <linux/fpga/adi-axi-common.h>
#include <linux/iio/adc/adi-axi-adc.h>

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
#define   ADI_AXI_REG_CHAN_CTRL_FMT_SIGNEXT	BIT(6)
#define   ADI_AXI_REG_CHAN_CTRL_FMT_TYPE	BIT(5)
#define   ADI_AXI_REG_CHAN_CTRL_FMT_EN		BIT(4)
#define   ADI_AXI_REG_CHAN_CTRL_PN_TYPE_OWR	BIT(1)
#define   ADI_AXI_REG_CHAN_CTRL_ENABLE		BIT(0)

#define ADI_AXI_REG_CHAN_CTRL_DEFAULTS		\
	(ADI_AXI_REG_CHAN_CTRL_FMT_SIGNEXT |	\
	 ADI_AXI_REG_CHAN_CTRL_FMT_EN |		\
	 ADI_AXI_REG_CHAN_CTRL_ENABLE)

struct adi_axi_adc_core_info {
	unsigned int				version;
};

struct adi_axi_adc_state {
	struct mutex				lock;

	struct adi_axi_adc_client		*client;
	struct regmap				*regmap;
};

struct adi_axi_adc_client {
	struct list_head			entry;
	struct adi_axi_adc_conv			conv;
	struct adi_axi_adc_state		*state;
	struct device				*dev;
	const struct adi_axi_adc_core_info	*info;
};

static LIST_HEAD(registered_clients);
static DEFINE_MUTEX(registered_clients_lock);

static struct adi_axi_adc_client *conv_to_client(struct adi_axi_adc_conv *conv)
{
	return container_of(conv, struct adi_axi_adc_client, conv);
}

void *adi_axi_adc_conv_priv(struct adi_axi_adc_conv *conv)
{
	struct adi_axi_adc_client *cl = conv_to_client(conv);

	return (char *)cl + ALIGN(sizeof(struct adi_axi_adc_client),
				  IIO_DMA_MINALIGN);
}
EXPORT_SYMBOL_NS_GPL(adi_axi_adc_conv_priv, IIO_ADI_AXI);

static int adi_axi_adc_config_dma_buffer(struct device *dev,
					 struct iio_dev *indio_dev)
{
	const char *dma_name;

	if (!device_property_present(dev, "dmas"))
		return 0;

	if (device_property_read_string(dev, "dma-names", &dma_name))
		dma_name = "rx";

	return devm_iio_dmaengine_buffer_setup(indio_dev->dev.parent,
					       indio_dev, dma_name);
}

static int adi_axi_adc_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	struct adi_axi_adc_state *st = iio_priv(indio_dev);
	struct adi_axi_adc_conv *conv = &st->client->conv;

	if (!conv->read_raw)
		return -EOPNOTSUPP;

	return conv->read_raw(conv, chan, val, val2, mask);
}

static int adi_axi_adc_write_raw(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan,
				 int val, int val2, long mask)
{
	struct adi_axi_adc_state *st = iio_priv(indio_dev);
	struct adi_axi_adc_conv *conv = &st->client->conv;

	if (!conv->write_raw)
		return -EOPNOTSUPP;

	return conv->write_raw(conv, chan, val, val2, mask);
}

static int adi_axi_adc_read_avail(struct iio_dev *indio_dev,
				  struct iio_chan_spec const *chan,
				  const int **vals, int *type, int *length,
				  long mask)
{
	struct adi_axi_adc_state *st = iio_priv(indio_dev);
	struct adi_axi_adc_conv *conv = &st->client->conv;

	if (!conv->read_avail)
		return -EOPNOTSUPP;

	return conv->read_avail(conv, chan, vals, type, length, mask);
}

static int adi_axi_adc_update_scan_mode(struct iio_dev *indio_dev,
					const unsigned long *scan_mask)
{
	struct adi_axi_adc_state *st = iio_priv(indio_dev);
	struct adi_axi_adc_conv *conv = &st->client->conv;
	unsigned int i;
	int ret;

	for (i = 0; i < conv->chip_info->num_channels; i++) {
		if (test_bit(i, scan_mask))
			ret = regmap_set_bits(st->regmap,
					      ADI_AXI_REG_CHAN_CTRL(i),
					      ADI_AXI_REG_CHAN_CTRL_ENABLE);
		else
			ret = regmap_clear_bits(st->regmap,
						ADI_AXI_REG_CHAN_CTRL(i),
						ADI_AXI_REG_CHAN_CTRL_ENABLE);
		if (ret)
			return ret;
	}

	return 0;
}

static struct adi_axi_adc_conv *adi_axi_adc_conv_register(struct device *dev,
							  size_t sizeof_priv)
{
	struct adi_axi_adc_client *cl;
	size_t alloc_size;

	alloc_size = ALIGN(sizeof(struct adi_axi_adc_client), IIO_DMA_MINALIGN);
	if (sizeof_priv)
		alloc_size += ALIGN(sizeof_priv, IIO_DMA_MINALIGN);

	cl = kzalloc(alloc_size, GFP_KERNEL);
	if (!cl)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&registered_clients_lock);

	cl->dev = get_device(dev);

	list_add_tail(&cl->entry, &registered_clients);

	mutex_unlock(&registered_clients_lock);

	return &cl->conv;
}

static void adi_axi_adc_conv_unregister(struct adi_axi_adc_conv *conv)
{
	struct adi_axi_adc_client *cl = conv_to_client(conv);

	mutex_lock(&registered_clients_lock);

	list_del(&cl->entry);
	put_device(cl->dev);

	mutex_unlock(&registered_clients_lock);

	kfree(cl);
}

static void devm_adi_axi_adc_conv_release(void *conv)
{
	adi_axi_adc_conv_unregister(conv);
}

struct adi_axi_adc_conv *devm_adi_axi_adc_conv_register(struct device *dev,
							size_t sizeof_priv)
{
	struct adi_axi_adc_conv *conv;
	int ret;

	conv = adi_axi_adc_conv_register(dev, sizeof_priv);
	if (IS_ERR(conv))
		return conv;

	ret = devm_add_action_or_reset(dev, devm_adi_axi_adc_conv_release,
				       conv);
	if (ret)
		return ERR_PTR(ret);

	return conv;
}
EXPORT_SYMBOL_NS_GPL(devm_adi_axi_adc_conv_register, IIO_ADI_AXI);

static const struct iio_info adi_axi_adc_info = {
	.read_raw = &adi_axi_adc_read_raw,
	.write_raw = &adi_axi_adc_write_raw,
	.update_scan_mode = &adi_axi_adc_update_scan_mode,
	.read_avail = &adi_axi_adc_read_avail,
};

static const struct adi_axi_adc_core_info adi_axi_adc_10_0_a_info = {
	.version = ADI_AXI_PCORE_VER(10, 0, 'a'),
};

static struct adi_axi_adc_client *adi_axi_adc_attach_client(struct device *dev)
{
	const struct adi_axi_adc_core_info *info;
	struct adi_axi_adc_client *cl;
	struct device_node *cln;

	info = of_device_get_match_data(dev);
	if (!info)
		return ERR_PTR(-ENODEV);

	cln = of_parse_phandle(dev->of_node, "adi,adc-dev", 0);
	if (!cln) {
		dev_err(dev, "No 'adi,adc-dev' node defined\n");
		return ERR_PTR(-ENODEV);
	}

	mutex_lock(&registered_clients_lock);

	list_for_each_entry(cl, &registered_clients, entry) {
		if (!cl->dev)
			continue;

		if (cl->dev->of_node != cln)
			continue;

		if (!try_module_get(cl->dev->driver->owner)) {
			mutex_unlock(&registered_clients_lock);
			of_node_put(cln);
			return ERR_PTR(-ENODEV);
		}

		get_device(cl->dev);
		cl->info = info;
		mutex_unlock(&registered_clients_lock);
		of_node_put(cln);
		return cl;
	}

	mutex_unlock(&registered_clients_lock);
	of_node_put(cln);

	return ERR_PTR(-EPROBE_DEFER);
}

static int adi_axi_adc_setup_channels(struct device *dev,
				      struct adi_axi_adc_state *st)
{
	struct adi_axi_adc_conv *conv = &st->client->conv;
	int i, ret;

	if (conv->preenable_setup) {
		ret = conv->preenable_setup(conv);
		if (ret)
			return ret;
	}

	for (i = 0; i < conv->chip_info->num_channels; i++) {
		ret = regmap_write(st->regmap, ADI_AXI_REG_CHAN_CTRL(i),
				   ADI_AXI_REG_CHAN_CTRL_DEFAULTS);
		if (ret)
			return ret;
	}

	return 0;
}

static int axi_adc_reset(struct adi_axi_adc_state *st)
{
	int ret;

	ret = regmap_write(st->regmap, ADI_AXI_REG_RSTN, 0);
	if (ret)
		return ret;

	mdelay(10);
	ret = regmap_write(st->regmap, ADI_AXI_REG_RSTN,
			   ADI_AXI_REG_RSTN_MMCM_RSTN);
	if (ret)
		return ret;

	mdelay(10);
	return regmap_write(st->regmap, ADI_AXI_REG_RSTN,
			    ADI_AXI_REG_RSTN_RSTN | ADI_AXI_REG_RSTN_MMCM_RSTN);
}

static void adi_axi_adc_cleanup(void *data)
{
	struct adi_axi_adc_client *cl = data;

	put_device(cl->dev);
	module_put(cl->dev->driver->owner);
}

static const struct regmap_config axi_adc_regmap_config = {
	.val_bits = 32,
	.reg_bits = 32,
	.reg_stride = 4,
	.max_register = 0x0800,
};

static int adi_axi_adc_probe(struct platform_device *pdev)
{
	struct adi_axi_adc_conv *conv;
	struct iio_dev *indio_dev;
	struct adi_axi_adc_client *cl;
	struct adi_axi_adc_state *st;
	void __iomem *base;
	unsigned int ver;
	int ret;

	cl = adi_axi_adc_attach_client(&pdev->dev);
	if (IS_ERR(cl))
		return PTR_ERR(cl);

	ret = devm_add_action_or_reset(&pdev->dev, adi_axi_adc_cleanup, cl);
	if (ret)
		return ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*st));
	if (indio_dev == NULL)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->client = cl;
	cl->state = st;
	mutex_init(&st->lock);

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	st->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					   &axi_adc_regmap_config);
	if (IS_ERR(st->regmap))
		return PTR_ERR(st->regmap);

	conv = &st->client->conv;

	ret = axi_adc_reset(st);
	if (ret)
		return ret;

	ret = regmap_read(st->regmap, ADI_AXI_REG_VERSION, &ver);
	if (ret)
		return ret;

	if (cl->info->version > ver) {
		dev_err(&pdev->dev,
			"IP core version is too old. Expected %d.%.2d.%c, Reported %d.%.2d.%c\n",
			ADI_AXI_PCORE_VER_MAJOR(cl->info->version),
			ADI_AXI_PCORE_VER_MINOR(cl->info->version),
			ADI_AXI_PCORE_VER_PATCH(cl->info->version),
			ADI_AXI_PCORE_VER_MAJOR(ver),
			ADI_AXI_PCORE_VER_MINOR(ver),
			ADI_AXI_PCORE_VER_PATCH(ver));
		return -ENODEV;
	}

	indio_dev->info = &adi_axi_adc_info;
	indio_dev->name = "adi-axi-adc";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->num_channels = conv->chip_info->num_channels;
	indio_dev->channels = conv->chip_info->channels;

	ret = adi_axi_adc_config_dma_buffer(&pdev->dev, indio_dev);
	if (ret)
		return ret;

	ret = adi_axi_adc_setup_channels(&pdev->dev, st);
	if (ret)
		return ret;

	ret = devm_iio_device_register(&pdev->dev, indio_dev);
	if (ret)
		return ret;

	dev_info(&pdev->dev, "AXI ADC IP core (%d.%.2d.%c) probed\n",
		 ADI_AXI_PCORE_VER_MAJOR(ver),
		 ADI_AXI_PCORE_VER_MINOR(ver),
		 ADI_AXI_PCORE_VER_PATCH(ver));

	return 0;
}

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
