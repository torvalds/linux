/*
 * Altera Arria10 DevKit System Resource MFD Driver
 *
 * Author: Thor Thayer <tthayer@opensource.altera.com>
 *
 * Copyright Intel Corporation (C) 2014-2016. All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPI access for Altera Arria10 MAX5 System Resource Chip
 *
 * Adapted from DA9052
 */

#include <linux/mfd/altera-a10sr.h>
#include <linux/mfd/core.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/spi/spi.h>

static const struct mfd_cell altr_a10sr_subdev_info[] = {
	{
		.name = "altr_a10sr_gpio",
		.of_compatible = "altr,a10sr-gpio",
	},
};

static bool altr_a10sr_reg_readable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ALTR_A10SR_VERSION_READ:
	case ALTR_A10SR_LED_REG:
	case ALTR_A10SR_PBDSW_REG:
	case ALTR_A10SR_PBDSW_IRQ_REG:
	case ALTR_A10SR_PWR_GOOD1_REG:
	case ALTR_A10SR_PWR_GOOD2_REG:
	case ALTR_A10SR_PWR_GOOD3_REG:
	case ALTR_A10SR_FMCAB_REG:
	case ALTR_A10SR_HPS_RST_REG:
	case ALTR_A10SR_USB_QSPI_REG:
	case ALTR_A10SR_SFPA_REG:
	case ALTR_A10SR_SFPB_REG:
	case ALTR_A10SR_I2C_M_REG:
	case ALTR_A10SR_WARM_RST_REG:
	case ALTR_A10SR_WR_KEY_REG:
	case ALTR_A10SR_PMBUS_REG:
		return true;
	default:
		return false;
	}
}

static bool altr_a10sr_reg_writeable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ALTR_A10SR_LED_REG:
	case ALTR_A10SR_PBDSW_IRQ_REG:
	case ALTR_A10SR_FMCAB_REG:
	case ALTR_A10SR_HPS_RST_REG:
	case ALTR_A10SR_USB_QSPI_REG:
	case ALTR_A10SR_SFPA_REG:
	case ALTR_A10SR_SFPB_REG:
	case ALTR_A10SR_WARM_RST_REG:
	case ALTR_A10SR_WR_KEY_REG:
	case ALTR_A10SR_PMBUS_REG:
		return true;
	default:
		return false;
	}
}

static bool altr_a10sr_reg_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ALTR_A10SR_PBDSW_REG:
	case ALTR_A10SR_PBDSW_IRQ_REG:
	case ALTR_A10SR_PWR_GOOD1_REG:
	case ALTR_A10SR_PWR_GOOD2_REG:
	case ALTR_A10SR_PWR_GOOD3_REG:
	case ALTR_A10SR_HPS_RST_REG:
	case ALTR_A10SR_I2C_M_REG:
	case ALTR_A10SR_WARM_RST_REG:
	case ALTR_A10SR_WR_KEY_REG:
	case ALTR_A10SR_PMBUS_REG:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config altr_a10sr_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.cache_type = REGCACHE_NONE,

	.use_single_rw = true,
	.read_flag_mask = 1,
	.write_flag_mask = 0,

	.max_register = ALTR_A10SR_WR_KEY_REG,
	.readable_reg = altr_a10sr_reg_readable,
	.writeable_reg = altr_a10sr_reg_writeable,
	.volatile_reg = altr_a10sr_reg_volatile,

};

static int altr_a10sr_spi_probe(struct spi_device *spi)
{
	int ret;
	struct altr_a10sr *a10sr;

	a10sr = devm_kzalloc(&spi->dev, sizeof(*a10sr),
			     GFP_KERNEL);
	if (!a10sr)
		return -ENOMEM;

	spi->mode = SPI_MODE_3;
	spi->bits_per_word = 8;
	spi_setup(spi);

	a10sr->dev = &spi->dev;

	spi_set_drvdata(spi, a10sr);

	a10sr->regmap = devm_regmap_init_spi(spi, &altr_a10sr_regmap_config);
	if (IS_ERR(a10sr->regmap)) {
		ret = PTR_ERR(a10sr->regmap);
		dev_err(&spi->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	ret = devm_mfd_add_devices(a10sr->dev, PLATFORM_DEVID_AUTO,
				   altr_a10sr_subdev_info,
				   ARRAY_SIZE(altr_a10sr_subdev_info),
				   NULL, 0, NULL);
	if (ret)
		dev_err(a10sr->dev, "Failed to register sub-devices: %d\n",
			ret);

	return ret;
}

static const struct of_device_id altr_a10sr_spi_of_match[] = {
	{ .compatible = "altr,a10sr" },
	{ },
};

static struct spi_driver altr_a10sr_spi_driver = {
	.probe = altr_a10sr_spi_probe,
	.driver = {
		.name = "altr_a10sr",
		.of_match_table = of_match_ptr(altr_a10sr_spi_of_match),
	},
};
builtin_driver(altr_a10sr_spi_driver, spi_register_driver)
