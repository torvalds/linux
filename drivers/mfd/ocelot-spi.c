// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * SPI core driver for the Ocelot chip family.
 *
 * This driver will handle everything necessary to allow for communication over
 * SPI to the VSC7511, VSC7512, VSC7513 and VSC7514 chips. The main functions
 * are to prepare the chip's SPI interface for a specific bus speed, and a host
 * processor's endianness. This will create and distribute regmaps for any
 * children.
 *
 * Copyright 2021-2022 Innovative Advantage Inc.
 *
 * Author: Colin Foster <colin.foster@in-advantage.com>
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/ioport.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/types.h>
#include <linux/units.h>

#include "ocelot.h"

#define REG_DEV_CPUORG_IF_CTRL		0x0000
#define REG_DEV_CPUORG_IF_CFGSTAT	0x0004

#define CFGSTAT_IF_NUM_VCORE		(0 << 24)
#define CFGSTAT_IF_NUM_VRAP		(1 << 24)
#define CFGSTAT_IF_NUM_SI		(2 << 24)
#define CFGSTAT_IF_NUM_MIIM		(3 << 24)

#define VSC7512_DEVCPU_ORG_RES_START	0x71000000
#define VSC7512_DEVCPU_ORG_RES_SIZE	0x38

#define VSC7512_CHIP_REGS_RES_START	0x71070000
#define VSC7512_CHIP_REGS_RES_SIZE	0x14

static const struct resource vsc7512_dev_cpuorg_resource =
	DEFINE_RES_REG_NAMED(VSC7512_DEVCPU_ORG_RES_START,
			     VSC7512_DEVCPU_ORG_RES_SIZE,
			     "devcpu_org");

static const struct resource vsc7512_gcb_resource =
	DEFINE_RES_REG_NAMED(VSC7512_CHIP_REGS_RES_START,
			     VSC7512_CHIP_REGS_RES_SIZE,
			     "devcpu_gcb_chip_regs");

static int ocelot_spi_initialize(struct device *dev)
{
	struct ocelot_ddata *ddata = dev_get_drvdata(dev);
	u32 val, check;
	int err;

	val = OCELOT_SPI_BYTE_ORDER;

	/*
	 * The SPI address must be big-endian, but we want the payload to match
	 * our CPU. These are two bits (0 and 1) but they're repeated such that
	 * the write from any configuration will be valid. The four
	 * configurations are:
	 *
	 * 0b00: little-endian, MSB first
	 * |            111111   | 22221111 | 33222222 |
	 * | 76543210 | 54321098 | 32109876 | 10987654 |
	 *
	 * 0b01: big-endian, MSB first
	 * | 33222222 | 22221111 | 111111   |          |
	 * | 10987654 | 32109876 | 54321098 | 76543210 |
	 *
	 * 0b10: little-endian, LSB first
	 * |              111111 | 11112222 | 22222233 |
	 * | 01234567 | 89012345 | 67890123 | 45678901 |
	 *
	 * 0b11: big-endian, LSB first
	 * | 22222233 | 11112222 |   111111 |          |
	 * | 45678901 | 67890123 | 89012345 | 01234567 |
	 */
	err = regmap_write(ddata->cpuorg_regmap, REG_DEV_CPUORG_IF_CTRL, val);
	if (err)
		return err;

	/*
	 * Apply the number of padding bytes between a read request and the data
	 * payload. Some registers have access times of up to 1us, so if the
	 * first payload bit is shifted out too quickly, the read will fail.
	 */
	val = ddata->spi_padding_bytes;
	err = regmap_write(ddata->cpuorg_regmap, REG_DEV_CPUORG_IF_CFGSTAT, val);
	if (err)
		return err;

	/*
	 * After we write the interface configuration, read it back here. This
	 * will verify several different things. The first is that the number of
	 * padding bytes actually got written correctly. These are found in bits
	 * 0:3.
	 *
	 * The second is that bit 16 is cleared. Bit 16 is IF_CFGSTAT:IF_STAT,
	 * and will be set if the register access is too fast. This would be in
	 * the condition that the number of padding bytes is insufficient for
	 * the SPI bus frequency.
	 *
	 * The last check is for bits 31:24, which define the interface by which
	 * the registers are being accessed. Since we're accessing them via the
	 * serial interface, it must return IF_NUM_SI.
	 */
	check = val | CFGSTAT_IF_NUM_SI;

	err = regmap_read(ddata->cpuorg_regmap, REG_DEV_CPUORG_IF_CFGSTAT, &val);
	if (err)
		return err;

	if (check != val)
		return -ENODEV;

	return 0;
}

static const struct regmap_config ocelot_spi_regmap_config = {
	.reg_bits = 24,
	.reg_stride = 4,
	.reg_shift = REGMAP_DOWNSHIFT(2),
	.val_bits = 32,

	.write_flag_mask = 0x80,

	.use_single_read = true,
	.use_single_write = true,
	.can_multi_write = false,

	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
};

static int ocelot_spi_regmap_bus_read(void *context, const void *reg, size_t reg_size,
				      void *val, size_t val_size)
{
	struct spi_transfer xfers[3] = {0};
	struct device *dev = context;
	struct ocelot_ddata *ddata;
	struct spi_device *spi;
	unsigned int index = 0;

	ddata = dev_get_drvdata(dev);
	spi = to_spi_device(dev);

	xfers[index].tx_buf = reg;
	xfers[index].len = reg_size;
	index++;

	if (ddata->spi_padding_bytes) {
		xfers[index].len = ddata->spi_padding_bytes;
		xfers[index].tx_buf = ddata->dummy_buf;
		xfers[index].dummy_data = 1;
		index++;
	}

	xfers[index].rx_buf = val;
	xfers[index].len = val_size;
	index++;

	return spi_sync_transfer(spi, xfers, index);
}

static int ocelot_spi_regmap_bus_write(void *context, const void *data, size_t count)
{
	struct device *dev = context;
	struct spi_device *spi = to_spi_device(dev);

	return spi_write(spi, data, count);
}

static const struct regmap_bus ocelot_spi_regmap_bus = {
	.write = ocelot_spi_regmap_bus_write,
	.read = ocelot_spi_regmap_bus_read,
};

struct regmap *ocelot_spi_init_regmap(struct device *dev, const struct resource *res)
{
	struct regmap_config regmap_config;

	memcpy(&regmap_config, &ocelot_spi_regmap_config, sizeof(regmap_config));

	regmap_config.name = res->name;
	regmap_config.max_register = resource_size(res) - 1;
	regmap_config.reg_base = res->start;

	return devm_regmap_init(dev, &ocelot_spi_regmap_bus, dev, &regmap_config);
}
EXPORT_SYMBOL_NS(ocelot_spi_init_regmap, MFD_OCELOT_SPI);

static int ocelot_spi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct ocelot_ddata *ddata;
	struct regmap *r;
	int err;

	ddata = devm_kzalloc(dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	spi_set_drvdata(spi, ddata);

	if (spi->max_speed_hz <= 500000) {
		ddata->spi_padding_bytes = 0;
	} else {
		/*
		 * Calculation taken from the manual for IF_CFGSTAT:IF_CFG.
		 * Register access time is 1us, so we need to configure and send
		 * out enough padding bytes between the read request and data
		 * transmission that lasts at least 1 microsecond.
		 */
		ddata->spi_padding_bytes = 1 + (spi->max_speed_hz / HZ_PER_MHZ + 2) / 8;

		ddata->dummy_buf = devm_kzalloc(dev, ddata->spi_padding_bytes, GFP_KERNEL);
		if (!ddata->dummy_buf)
			return -ENOMEM;
	}

	spi->bits_per_word = 8;

	err = spi_setup(spi);
	if (err)
		return dev_err_probe(&spi->dev, err, "Error performing SPI setup\n");

	r = ocelot_spi_init_regmap(dev, &vsc7512_dev_cpuorg_resource);
	if (IS_ERR(r))
		return PTR_ERR(r);

	ddata->cpuorg_regmap = r;

	r = ocelot_spi_init_regmap(dev, &vsc7512_gcb_resource);
	if (IS_ERR(r))
		return PTR_ERR(r);

	ddata->gcb_regmap = r;

	/*
	 * The chip must be set up for SPI before it gets initialized and reset.
	 * This must be done before calling init, and after a chip reset is
	 * performed.
	 */
	err = ocelot_spi_initialize(dev);
	if (err)
		return dev_err_probe(dev, err, "Error initializing SPI bus\n");

	err = ocelot_chip_reset(dev);
	if (err)
		return dev_err_probe(dev, err, "Error resetting device\n");

	/*
	 * A chip reset will clear the SPI configuration, so it needs to be done
	 * again before we can access any registers.
	 */
	err = ocelot_spi_initialize(dev);
	if (err)
		return dev_err_probe(dev, err, "Error initializing SPI bus after reset\n");

	err = ocelot_core_init(dev);
	if (err)
		return dev_err_probe(dev, err, "Error initializing Ocelot core\n");

	return 0;
}

static const struct spi_device_id ocelot_spi_ids[] = {
	{ "vsc7512", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, ocelot_spi_ids);

static const struct of_device_id ocelot_spi_of_match[] = {
	{ .compatible = "mscc,vsc7512" },
	{ }
};
MODULE_DEVICE_TABLE(of, ocelot_spi_of_match);

static struct spi_driver ocelot_spi_driver = {
	.driver = {
		.name = "ocelot-soc",
		.of_match_table = ocelot_spi_of_match,
	},
	.id_table = ocelot_spi_ids,
	.probe = ocelot_spi_probe,
};
module_spi_driver(ocelot_spi_driver);

MODULE_DESCRIPTION("SPI Controlled Ocelot Chip Driver");
MODULE_AUTHOR("Colin Foster <colin.foster@in-advantage.com>");
MODULE_LICENSE("Dual MIT/GPL");
MODULE_IMPORT_NS(MFD_OCELOT);
