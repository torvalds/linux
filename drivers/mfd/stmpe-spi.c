/*
 * ST Microelectronics MFD: stmpe's spi client specific driver
 *
 * Copyright (C) ST Microelectronics SA 2011
 *
 * License Terms: GNU General Public License, version 2
 * Author: Viresh Kumar <vireshk@kernel.org> for ST Microelectronics
 */

#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/types.h>
#include "stmpe.h"

#define READ_CMD	(1 << 7)

static int spi_reg_read(struct stmpe *stmpe, u8 reg)
{
	struct spi_device *spi = stmpe->client;
	int status = spi_w8r16(spi, reg | READ_CMD);

	return (status < 0) ? status : status >> 8;
}

static int spi_reg_write(struct stmpe *stmpe, u8 reg, u8 val)
{
	struct spi_device *spi = stmpe->client;
	u16 cmd = (val << 8) | reg;

	return spi_write(spi, (const u8 *)&cmd, 2);
}

static int spi_block_read(struct stmpe *stmpe, u8 reg, u8 length, u8 *values)
{
	int ret, i;

	for (i = 0; i < length; i++) {
		ret = spi_reg_read(stmpe, reg + i);
		if (ret < 0)
			return ret;
		*(values + i) = ret;
	}

	return 0;
}

static int spi_block_write(struct stmpe *stmpe, u8 reg, u8 length,
		const u8 *values)
{
	int ret = 0, i;

	for (i = length; i > 0; i--, reg++) {
		ret = spi_reg_write(stmpe, reg, *(values + i - 1));
		if (ret < 0)
			return ret;
	}

	return ret;
}

static void spi_init(struct stmpe *stmpe)
{
	struct spi_device *spi = stmpe->client;

	spi->bits_per_word = 8;

	/* This register is only present for stmpe811 */
	if (stmpe->variant->id_val == 0x0811)
		spi_reg_write(stmpe, STMPE811_REG_SPI_CFG, spi->mode);

	if (spi_setup(spi) < 0)
		dev_dbg(&spi->dev, "spi_setup failed\n");
}

static struct stmpe_client_info spi_ci = {
	.read_byte = spi_reg_read,
	.write_byte = spi_reg_write,
	.read_block = spi_block_read,
	.write_block = spi_block_write,
	.init = spi_init,
};

static int
stmpe_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);

	/* don't exceed max specified rate - 1MHz - Limitation of STMPE */
	if (spi->max_speed_hz > 1000000) {
		dev_dbg(&spi->dev, "f(sample) %d KHz?\n",
				(spi->max_speed_hz/1000));
		return -EINVAL;
	}

	spi_ci.irq = spi->irq;
	spi_ci.client = spi;
	spi_ci.dev = &spi->dev;

	return stmpe_probe(&spi_ci, id->driver_data);
}

static int stmpe_spi_remove(struct spi_device *spi)
{
	struct stmpe *stmpe = spi_get_drvdata(spi);

	return stmpe_remove(stmpe);
}

static const struct of_device_id stmpe_spi_of_match[] = {
	{ .compatible = "st,stmpe610", },
	{ .compatible = "st,stmpe801", },
	{ .compatible = "st,stmpe811", },
	{ .compatible = "st,stmpe1601", },
	{ .compatible = "st,stmpe2401", },
	{ .compatible = "st,stmpe2403", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, stmpe_spi_of_match);

static const struct spi_device_id stmpe_spi_id[] = {
	{ "stmpe610", STMPE610 },
	{ "stmpe801", STMPE801 },
	{ "stmpe811", STMPE811 },
	{ "stmpe1601", STMPE1601 },
	{ "stmpe2401", STMPE2401 },
	{ "stmpe2403", STMPE2403 },
	{ }
};
MODULE_DEVICE_TABLE(spi, stmpe_id);

static struct spi_driver stmpe_spi_driver = {
	.driver = {
		.name	= "stmpe-spi",
		.of_match_table = of_match_ptr(stmpe_spi_of_match),
#ifdef CONFIG_PM
		.pm	= &stmpe_dev_pm_ops,
#endif
	},
	.probe		= stmpe_spi_probe,
	.remove		= stmpe_spi_remove,
	.id_table	= stmpe_spi_id,
};

static int __init stmpe_init(void)
{
	return spi_register_driver(&stmpe_spi_driver);
}
subsys_initcall(stmpe_init);

static void __exit stmpe_exit(void)
{
	spi_unregister_driver(&stmpe_spi_driver);
}
module_exit(stmpe_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("STMPE MFD SPI Interface Driver");
MODULE_AUTHOR("Viresh Kumar <vireshk@kernel.org>");
