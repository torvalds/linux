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
				    unsigned int addr,
				    const struct mcp23s08_info *info)
{
	struct regmap_config *copy;
	const char *name;

	switch (info->type) {
	case MCP_TYPE_S08:
		mcp->chip.label = devm_kasprintf(dev, GFP_KERNEL, "mcp23s08.%d", addr);
		if (!mcp->chip.label)
			return -ENOMEM;

		name = devm_kasprintf(dev, GFP_KERNEL, "%d", addr);
		if (!name)
			return -ENOMEM;

		break;

	case MCP_TYPE_S17:
		mcp->chip.label = devm_kasprintf(dev, GFP_KERNEL, "mcp23s17.%d", addr);
		if (!mcp->chip.label)
			return -ENOMEM;

		name = devm_kasprintf(dev, GFP_KERNEL, "%d", addr);
		if (!name)
			return -ENOMEM;

		break;

	case MCP_TYPE_S18:
		mcp->chip.label = info->label;
		name = info->regmap->name;
		break;

	default:
		dev_err(dev, "invalid device type (%d)\n", info->type);
		return -EINVAL;
	}

	mcp->reg_shift = info->reg_shift;
	mcp->chip.ngpio = info->ngpio;
	copy = devm_kmemdup(dev, info->regmap, sizeof(*info->regmap), GFP_KERNEL);
	if (!copy)
		return -ENOMEM;

	copy->name = name;

	mcp->regmap = devm_regmap_init(dev, &mcp23sxx_spi_regmap, mcp, copy);
	if (IS_ERR(mcp->regmap))
		dev_err(dev, "regmap init failed for %s\n", mcp->chip.label);
	return PTR_ERR_OR_ZERO(mcp->regmap);
}

static int mcp23s08_probe(struct spi_device *spi)
{
	struct mcp23s08_driver_data *data;
	const struct mcp23s08_info *info;
	struct device *dev = &spi->dev;
	unsigned long spi_present_mask;
	unsigned int ngpio = 0;
	unsigned int addr;
	int chips;
	int ret;
	u32 v;

	info = spi_get_device_match_data(spi);

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

		ret = mcp23s08_spi_regmap_init(data->mcp[addr], dev, addr, info);
		if (ret)
			return ret;

		data->mcp[addr]->pinctrl_desc.name = devm_kasprintf(dev, GFP_KERNEL,
								    "mcp23xxx-pinctrl.%d",
								    addr);
		if (!data->mcp[addr]->pinctrl_desc.name)
			return -ENOMEM;

		ret = mcp23s08_probe_one(data->mcp[addr], dev, 0x40 | (addr << 1),
					 info->type, -1);
		if (ret < 0)
			return ret;

		ngpio += data->mcp[addr]->chip.ngpio;
	}
	data->ngpio = ngpio;

	return 0;
}

static const struct mcp23s08_info mcp23s08_spi = {
	.regmap = &mcp23x08_regmap,
	.type = MCP_TYPE_S08,
	.ngpio = 8,
	.reg_shift = 0,
};

static const struct mcp23s08_info mcp23s17_spi = {
	.regmap = &mcp23x17_regmap,
	.type = MCP_TYPE_S17,
	.ngpio = 16,
	.reg_shift = 1,
};

static const struct mcp23s08_info mcp23s18_spi = {
	.regmap = &mcp23x17_regmap,
	.label = "mcp23s18",
	.type = MCP_TYPE_S18,
	.ngpio = 16,
	.reg_shift = 1,
};

static const struct spi_device_id mcp23s08_ids[] = {
	{ "mcp23s08", (kernel_ulong_t)&mcp23s08_spi },
	{ "mcp23s17", (kernel_ulong_t)&mcp23s17_spi },
	{ "mcp23s18", (kernel_ulong_t)&mcp23s18_spi },
	{ }
};
MODULE_DEVICE_TABLE(spi, mcp23s08_ids);

static const struct of_device_id mcp23s08_spi_of_match[] = {
	{ .compatible = "microchip,mcp23s08", .data = &mcp23s08_spi },
	{ .compatible = "microchip,mcp23s17", .data = &mcp23s17_spi },
	{ .compatible = "microchip,mcp23s18", .data = &mcp23s18_spi },
/* NOTE: The use of the mcp prefix is deprecated and will be removed. */
	{ .compatible = "mcp,mcp23s08", .data = &mcp23s08_spi },
	{ .compatible = "mcp,mcp23s17", .data = &mcp23s17_spi },
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

MODULE_DESCRIPTION("MCP23S08 SPI GPIO driver");
MODULE_LICENSE("GPL");
