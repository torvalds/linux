// SPDX-License-Identifier: GPL-2.0
/*
 * SPI access driver for TI TPS6594/TPS6593/LP8764 PMICs
 *
 * Copyright (C) 2023 BayLibre Incorporated - https://www.baylibre.com/
 */

#include <linux/crc8.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include <linux/mfd/tps6594.h>

#define TPS6594_SPI_PAGE_SHIFT	5
#define TPS6594_SPI_READ_BIT	BIT(4)

static bool enable_crc;
module_param(enable_crc, bool, 0444);
MODULE_PARM_DESC(enable_crc, "Enable CRC feature for SPI interface");

DECLARE_CRC8_TABLE(tps6594_spi_crc_table);

static int tps6594_spi_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct spi_device *spi = context;
	struct tps6594 *tps = spi_get_drvdata(spi);
	u8 buf[4] = { 0 };
	size_t count_rx = 1;
	int ret;

	buf[0] = reg;
	buf[1] = TPS6594_REG_TO_PAGE(reg) << TPS6594_SPI_PAGE_SHIFT | TPS6594_SPI_READ_BIT;

	if (tps->use_crc)
		count_rx++;

	ret = spi_write_then_read(spi, buf, 2, buf + 2, count_rx);
	if (ret < 0)
		return ret;

	if (tps->use_crc && buf[3] != crc8(tps6594_spi_crc_table, buf, 3, CRC8_INIT_VALUE))
		return -EIO;

	*val = buf[2];

	return 0;
}

static int tps6594_spi_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct spi_device *spi = context;
	struct tps6594 *tps = spi_get_drvdata(spi);
	u8 buf[4] = { 0 };
	size_t count = 3;

	buf[0] = reg;
	buf[1] = TPS6594_REG_TO_PAGE(reg) << TPS6594_SPI_PAGE_SHIFT;
	buf[2] = val;

	if (tps->use_crc)
		buf[3] = crc8(tps6594_spi_crc_table, buf, count++, CRC8_INIT_VALUE);

	return spi_write(spi, buf, count);
}

static const struct regmap_config tps6594_spi_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = TPS6594_REG_DWD_FAIL_CNT_REG,
	.volatile_reg = tps6594_is_volatile_reg,
	.reg_read = tps6594_spi_reg_read,
	.reg_write = tps6594_spi_reg_write,
	.use_single_read = true,
	.use_single_write = true,
};

static const struct of_device_id tps6594_spi_of_match_table[] = {
	{ .compatible = "ti,tps6594-q1", .data = (void *)TPS6594, },
	{ .compatible = "ti,tps6593-q1", .data = (void *)TPS6593, },
	{ .compatible = "ti,lp8764-q1",  .data = (void *)LP8764,  },
	{}
};
MODULE_DEVICE_TABLE(of, tps6594_spi_of_match_table);

static int tps6594_spi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct tps6594 *tps;
	const struct of_device_id *match;

	tps = devm_kzalloc(dev, sizeof(*tps), GFP_KERNEL);
	if (!tps)
		return -ENOMEM;

	spi_set_drvdata(spi, tps);

	tps->dev = dev;
	tps->reg = spi->chip_select;
	tps->irq = spi->irq;

	tps->regmap = devm_regmap_init(dev, NULL, spi, &tps6594_spi_regmap_config);
	if (IS_ERR(tps->regmap))
		return dev_err_probe(dev, PTR_ERR(tps->regmap), "Failed to init regmap\n");

	match = of_match_device(tps6594_spi_of_match_table, dev);
	if (!match)
		return dev_err_probe(dev, PTR_ERR(match), "Failed to find matching chip ID\n");
	tps->chip_id = (unsigned long)match->data;

	crc8_populate_msb(tps6594_spi_crc_table, TPS6594_CRC8_POLYNOMIAL);

	return tps6594_device_init(tps, enable_crc);
}

static struct spi_driver tps6594_spi_driver = {
	.driver	= {
		.name = "tps6594",
		.of_match_table = tps6594_spi_of_match_table,
	},
	.probe = tps6594_spi_probe,
};
module_spi_driver(tps6594_spi_driver);

MODULE_AUTHOR("Julien Panis <jpanis@baylibre.com>");
MODULE_DESCRIPTION("TPS6594 SPI Interface Driver");
MODULE_LICENSE("GPL");
