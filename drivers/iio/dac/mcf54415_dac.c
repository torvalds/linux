// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NXP mcf54415 DAC driver
 *
 * Copyright 2026 BayLibre - adureghello@baylibre.com
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include <linux/iio/iio.h>

#define MCF54415_DAC_CR			0x00
#define MCF54415_DAC_CR_PDN		BIT(0)
#define MCF54415_DAC_CR_HSLS		BIT(6)
#define MCF54415_DAC_CR_WMLVL		GENMASK(9, 8)
#define MCF54415_DAC_CR_FILT		BIT(12)

#define MCF54415_DAC_DATA		0x02

struct mcf54415_dac {
	struct regmap *map;
	struct clk *clk;
};

static const struct regmap_config mcf54415_dac_regmap_config = {
	.reg_bits = 16,
	.reg_stride = 2,
	.val_bits = 16,
	.max_register = 0x0c, /* DACX_FILTCNT,  R.M. Table 30-2 */
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
};

static int mcf54415_dac_init(struct mcf54415_dac *info)
{
	u16 val = MCF54415_DAC_CR_FILT | FIELD_PREP(MCF54415_DAC_CR_WMLVL, 1);
	int ret;

	/* Fixed defaults and enable DAC (bit 0 set to 0) */
	ret = regmap_write(info->map, MCF54415_DAC_CR, val);
	if (ret)
		return ret;

	/* DAC is ready after 12us, from RM table 40-3  */
	fsleep(12);

	return 0;
}

static void mcf54415_dac_exit(void *data)
{
	struct mcf54415_dac *info = data;

	regmap_set_bits(info->map, MCF54415_DAC_CR, MCF54415_DAC_CR_PDN);
}

static const struct iio_chan_spec mcf54415_dac_iio_channel = {
	.type = IIO_VOLTAGE,
	.output = 1,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
};

static int mcf54415_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct mcf54415_dac *info = iio_priv(indio_dev);
	unsigned int reg;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = regmap_read(info->map, MCF54415_DAC_DATA, &reg);
		if (ret)
			return ret;
		*val = reg & GENMASK(11, 0);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		/* Reference voltage as per ColdFire datasheet is 3.3V */
		*val = 3300 /* mV */;
		*val2 = 12;
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static int mcf54415_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	struct mcf54415_dac *info = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		/* Check based on RM 30.3.2 (DACn_DATA) reg. resolution */
		if (val < 0 || val > 4095)
			return -EINVAL;
		return regmap_write(info->map, MCF54415_DAC_DATA, val);
	default:
		return -EINVAL;
	}
}

static const struct iio_info mcf54415_dac_iio_info = {
	.read_raw = &mcf54415_read_raw,
	.write_raw = &mcf54415_write_raw,
};

static int mcf54415_dac_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	struct mcf54415_dac *info;
	void __iomem *regs;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*info));
	if (!indio_dev)
		return -ENOMEM;

	info = iio_priv(indio_dev);

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return dev_err_probe(dev, PTR_ERR(regs), "failed to get io regs\n");

	info->map = devm_regmap_init_mmio(dev, regs, &mcf54415_dac_regmap_config);
	if (IS_ERR(info->map))
		return PTR_ERR(info->map);

	info->clk = devm_clk_get_enabled(dev, "dac");
	if (IS_ERR(info->clk))
		return dev_err_probe(dev, PTR_ERR(info->clk), "failed getting clock\n");

	indio_dev->name = "mcf54415";
	indio_dev->info = &mcf54415_dac_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = &mcf54415_dac_iio_channel;
	indio_dev->num_channels = 1;

	ret = mcf54415_dac_init(info);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, mcf54415_dac_exit, info);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct platform_device_id mcf54415_dac_ids[] = {
	{ .name = "mcfdac" },
	{ }
};
MODULE_DEVICE_TABLE(platform, mcf54415_dac_ids);

static struct platform_driver mcf54415_dac_driver = {
	.driver = {
		.name = "mcf54415_dac",
	},
	.probe = mcf54415_dac_probe,
	.id_table = mcf54415_dac_ids,
};
module_platform_driver(mcf54415_dac_driver);

MODULE_AUTHOR("Angelo Dureghello <angelo@kernel-space.org>");
MODULE_DESCRIPTION("NXP MCF54415 DAC driver");
MODULE_LICENSE("GPL");
