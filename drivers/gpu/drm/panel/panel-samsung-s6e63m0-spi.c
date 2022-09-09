// SPDX-License-Identifier: GPL-2.0

#include <linux/device.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>

#include <drm/drm_mipi_dbi.h>
#include <drm/drm_print.h>

#include "panel-samsung-s6e63m0.h"

static const u8 s6e63m0_dbi_read_commands[] = {
	MCS_READ_ID1,
	MCS_READ_ID2,
	MCS_READ_ID3,
	0, /* sentinel */
};

static int s6e63m0_spi_dcs_read(struct device *dev, void *trsp,
				const u8 cmd, u8 *data)
{
	struct mipi_dbi *dbi = trsp;
	int ret;

	ret = mipi_dbi_command_read(dbi, cmd, data);
	if (ret)
		dev_err(dev, "error on DBI read command %02x\n", cmd);

	return ret;
}

static int s6e63m0_spi_dcs_write(struct device *dev, void *trsp,
				 const u8 *data, size_t len)
{
	struct mipi_dbi *dbi = trsp;
	int ret;

	ret = mipi_dbi_command_stackbuf(dbi, data[0], (data + 1), (len - 1));
	usleep_range(300, 310);

	return ret;
}

static int s6e63m0_spi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct mipi_dbi *dbi;
	int ret;

	dbi = devm_kzalloc(dev, sizeof(*dbi), GFP_KERNEL);
	if (!dbi)
		return -ENOMEM;

	ret = mipi_dbi_spi_init(spi, dbi, NULL);
	if (ret)
		return dev_err_probe(dev, ret, "MIPI DBI init failed\n");
	/* Register our custom MCS read commands */
	dbi->read_commands = s6e63m0_dbi_read_commands;

	return s6e63m0_probe(dev, dbi, s6e63m0_spi_dcs_read,
			     s6e63m0_spi_dcs_write, false);
}

static void s6e63m0_spi_remove(struct spi_device *spi)
{
	s6e63m0_remove(&spi->dev);
}

static const struct of_device_id s6e63m0_spi_of_match[] = {
	{ .compatible = "samsung,s6e63m0" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s6e63m0_spi_of_match);

static struct spi_driver s6e63m0_spi_driver = {
	.probe			= s6e63m0_spi_probe,
	.remove			= s6e63m0_spi_remove,
	.driver			= {
		.name		= "panel-samsung-s6e63m0",
		.of_match_table = s6e63m0_spi_of_match,
	},
};
module_spi_driver(s6e63m0_spi_driver);

MODULE_AUTHOR("Paweł Chmiel <pawel.mikolaj.chmiel@gmail.com>");
MODULE_DESCRIPTION("s6e63m0 LCD SPI Driver");
MODULE_LICENSE("GPL v2");
