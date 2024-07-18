// SPDX-License-Identifier: GPL-2.0
/*
 * CS40L50 Advanced Haptic Driver with waveform memory,
 * integrated DSP, and closed-loop algorithms
 *
 * Copyright 2024 Cirrus Logic, Inc.
 *
 * Author: James Ogletree <james.ogletree@cirrus.com>
 */

#include <linux/mfd/cs40l50.h>
#include <linux/spi/spi.h>

static int cs40l50_spi_probe(struct spi_device *spi)
{
	struct cs40l50 *cs40l50;

	cs40l50 = devm_kzalloc(&spi->dev, sizeof(*cs40l50), GFP_KERNEL);
	if (!cs40l50)
		return -ENOMEM;

	spi_set_drvdata(spi, cs40l50);

	cs40l50->dev = &spi->dev;
	cs40l50->irq = spi->irq;

	cs40l50->regmap = devm_regmap_init_spi(spi, &cs40l50_regmap);
	if (IS_ERR(cs40l50->regmap))
		return dev_err_probe(cs40l50->dev, PTR_ERR(cs40l50->regmap),
				     "Failed to initialize register map\n");

	return cs40l50_probe(cs40l50);
}

static void cs40l50_spi_remove(struct spi_device *spi)
{
	struct cs40l50 *cs40l50 = spi_get_drvdata(spi);

	cs40l50_remove(cs40l50);
}

static const struct spi_device_id cs40l50_id_spi[] = {
	{ "cs40l50" },
	{}
};
MODULE_DEVICE_TABLE(spi, cs40l50_id_spi);

static const struct of_device_id cs40l50_of_match[] = {
	{ .compatible = "cirrus,cs40l50" },
	{}
};
MODULE_DEVICE_TABLE(of, cs40l50_of_match);

static struct spi_driver cs40l50_spi_driver = {
	.driver = {
		.name = "cs40l50",
		.of_match_table = cs40l50_of_match,
		.pm = pm_ptr(&cs40l50_pm_ops),
	},
	.id_table = cs40l50_id_spi,
	.probe = cs40l50_spi_probe,
	.remove = cs40l50_spi_remove,
};
module_spi_driver(cs40l50_spi_driver);

MODULE_DESCRIPTION("CS40L50 SPI Driver");
MODULE_AUTHOR("James Ogletree, Cirrus Logic Inc. <james.ogletree@cirrus.com>");
MODULE_LICENSE("GPL");
