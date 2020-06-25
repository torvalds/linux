// SPDX-License-Identifier: GPL-2.0-only
/* MCP23S08 SPI GPIO driver */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "pinctrl-mcp23s08.h"

#define MCP_MAX_DEV_PER_CS	8

/*
 * A given spi_device can represent up to eight mcp23sxx chips
 * sharing the same chipselect but using different addresses
 * (e.g. chips #0 and #3 might be populated, but not #1 or #2).
 * Driver data holds all the per-chip data.
 */
struct mcp23s08_driver_data {
	unsigned		ngpio;
	struct mcp23s08		*mcp[8];
	struct mcp23s08		chip[];
};

static int mcp23sxx_spi_write(void *context, const void *data, size_t count)
{
	struct mcp23s08 *mcp = context;
	struct spi_device *spi = to_spi_device(mcp->dev);
	struct spi_message m;
	struct spi_transfer t[2] = { { .tx_buf = &mcp->addr, .len = 1, },
				     { .tx_buf = data, .len = count, }, };

	spi_message_init(&m);
	spi_message_add_tail(&t[0], &m);
	spi_message_add_tail(&t[1], &m);

	return spi_sync(spi, &m);
}

static int mcp23sxx_spi_gather_write(void *context,
				const void *reg, size_t reg_size,
				const void *val, size_t val_size)
{
	struct mcp23s08 *mcp = context;
	struct spi_device *spi = to_spi_device(mcp->dev);
	struct spi_message m;
	struct spi_transfer t[3] = { { .tx_buf = &mcp->addr, .len = 1, },
				     { .tx_buf = reg, .len = reg_size, },
				     { .tx_buf = val, .len = val_size, }, };

	spi_message_init(&m);
	spi_message_add_tail(&t[0], &m);
	spi_message_add_tail(&t[1], &m);
	spi_message_add_tail(&t[2], &m);

	return spi_sync(spi, &m);
}

static int mcp23sxx_spi_read(void *context, const void *reg, size_t reg_size,
				void *val, size_t val_size)
{
	struct mcp23s08 *mcp = context;
	struct spi_device *spi = to_spi_device(mcp->dev);
	u8 tx[2];

	if (reg_size != 1)
		return -EINVAL;

	tx[0] = mcp->addr | 0x01;
	tx[1] = *((u8 *) reg);

	return spi_write_then_read(spi, tx, sizeof(tx), val, val_size);
}

static const struct regmap_bus mcp23sxx_spi_regmap = {
	.write = mcp23sxx_spi_write,
	.gather_write = mcp23sxx_spi_gather_write,
	.read = mcp23sxx_spi_read,
};

static int mcp23s08_spi_regmap_init(struct mcp23s08 *mcp, struct device *dev,
				    unsigned int addr, unsigned int type)
{
	const struct regmap_config *config;
	struct regmap_config *copy;
	const char *name;

	switch (type) {
	case MCP_TYPE_S08:
		mcp->reg_shift = 0;
		mcp->chip.ngpio = 8;
		mcp->chip.label = devm_kasprintf(dev, GFP_KERNEL, "mcp23s08.%d", addr);

		config = &mcp23x08_regmap;
		name = devm_kasprintf(dev, GFP_KERNEL, "%d", addr);
		break;

	case MCP_TYPE_S17:
		mcp->reg_shift = 1;
		mcp->chip.ngpio = 16;
		mcp->chip.label = devm_kasprintf(dev, GFP_KERNEL, "mcp23s17.%d", addr);

		config = &mcp23x17_regmap;
		name = devm_kasprintf(dev, GFP_KERNEL, "%d", addr);
		break;

	case MCP_TYPE_S18:
		mcp->reg_shift = 1;
		mcp->chip.ngpio = 16;
		mcp->chip.label = "mcp23s18";

		config = &mcp23x17_regmap;
		name = config->name;
		break;

	default:
		dev_err(dev, "invalid device type (%d)\n", type);
		return -EINVAL;
	}

	copy = devm_kmemdup(dev, &config, sizeof(config), GFP_KERNEL);
	if (!copy)
		return -ENOMEM;

	copy->name = name;

	mcp->regmap = devm_regmap_init(dev, &mcp23sxx_spi_regmap, mcp, copy);
	return PTR_ERR_OR_ZERO(mcp->regmap);
}

static int mcp23s08_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct mcp23s08_driver_data *data;
	unsigned long spi_present_mask;
	const void *match;
	unsigned int addr;
	unsigned int ngpio = 0;
	int chips;
	int type;
	int ret;
	u32 v;

	match = device_get_match_data(dev);
	if (match)
		type = (int)(uintptr_t)match;
	else
		type = spi_get_device_id(spi)->driver_data;

	ret = device_property_read_u32(dev, "microchip,spi-present-mask", &v);
	if (ret) {
		ret = device_property_read_u32(dev, "mcp,spi-present-mask", &v);
		if (ret) {
			dev_err(dev, "missing spi-present-mask");
			return ret;
		}
	}
	spi_present_mask = v;

	if (!spi_present_mask || spi_present_mask >= BIT(MCP_MAX_DEV_PER_CS)) {
		dev_err(dev, "invalid spi-present-mask");
		return -ENODEV;
	}

	chips = hweight_long(spi_present_mask);

	data = devm_kzalloc(dev, struct_size(data, chip, chips), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	spi_set_drvdata(spi, data);

	for_each_set_bit(addr, &spi_present_mask, MCP_MAX_DEV_PER_CS) {
		data->mcp[addr] = &data->chip[--chips];
		data->mcp[addr]->irq = spi->irq;

		ret = mcp23s08_spi_regmap_init(data->mcp[addr], dev, addr, type);
		if (ret)
			return ret;

		data->mcp[addr]->pinctrl_desc.name = devm_kasprintf(dev, GFP_KERNEL,
								    "mcp23xxx-pinctrl.%d",
								    addr);
		if (!data->mcp[addr]->pinctrl_desc.name)
			return -ENOMEM;

		ret = mcp23s08_probe_one(data->mcp[addr], dev, 0x40 | (addr << 1), type, -1);
		if (ret < 0)
			return ret;

		ngpio += data->mcp[addr]->chip.ngpio;
	}
	data->ngpio = ngpio;

	return 0;
}

static const struct spi_device_id mcp23s08_ids[] = {
	{ "mcp23s08", MCP_TYPE_S08 },
	{ "mcp23s17", MCP_TYPE_S17 },
	{ "mcp23s18", MCP_TYPE_S18 },
	{ }
};
MODULE_DEVICE_TABLE(spi, mcp23s08_ids);

static const struct of_device_id mcp23s08_spi_of_match[] = {
	{
		.compatible = "microchip,mcp23s08",
		.data = (void *) MCP_TYPE_S08,
	},
	{
		.compatible = "microchip,mcp23s17",
		.data = (void *) MCP_TYPE_S17,
	},
	{
		.compatible = "microchip,mcp23s18",
		.data = (void *) MCP_TYPE_S18,
	},
/* NOTE: The use of the mcp prefix is deprecated and will be removed. */
	{
		.compatible = "mcp,mcp23s08",
		.data = (void *) MCP_TYPE_S08,
	},
	{
		.compatible = "mcp,mcp23s17",
		.data = (void *) MCP_TYPE_S17,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, mcp23s08_spi_of_match);

static struct spi_driver mcp23s08_driver = {
	.probe		= mcp23s08_probe,
	.id_table	= mcp23s08_ids,
	.driver = {
		.name	= "mcp23s08",
		.of_match_table = mcp23s08_spi_of_match,
	},
};

static int __init mcp23s08_spi_init(void)
{
	return spi_register_driver(&mcp23s08_driver);
}

/*
 * Register after SPI postcore initcall and before
 * subsys initcalls that may rely on these GPIOs.
 */
subsys_initcall(mcp23s08_spi_init);

static void mcp23s08_spi_exit(void)
{
	spi_unregister_driver(&mcp23s08_driver);
}
module_exit(mcp23s08_spi_exit);

MODULE_LICENSE("GPL");
