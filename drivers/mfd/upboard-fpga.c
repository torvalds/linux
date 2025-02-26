// SPDX-License-Identifier: GPL-2.0-only
/*
 * UP Board FPGA driver.
 *
 * FPGA provides more GPIO driving power, LEDS and pin mux function.
 *
 * Copyright (c) AAEON. All rights reserved.
 * Copyright (C) 2024 Bootlin
 *
 * Author: Gary Wang <garywang@aaeon.com.tw>
 * Author: Thomas Richard <thomas.richard@bootlin.com>
 */

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/mfd/core.h>
#include <linux/mfd/upboard-fpga.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/sysfs.h>

#define UPBOARD_AAEON_MANUFACTURER_ID	0x01
#define UPBOARD_MANUFACTURER_ID_MASK	GENMASK(7, 0)

#define UPBOARD_ADDRESS_SIZE  7
#define UPBOARD_REGISTER_SIZE 16

#define UPBOARD_READ_FLAG     BIT(UPBOARD_ADDRESS_SIZE)

#define UPBOARD_FW_ID_MAJOR_SUPPORTED	0x0

#define UPBOARD_FW_ID_BUILD_MASK	GENMASK(15, 12)
#define UPBOARD_FW_ID_MAJOR_MASK	GENMASK(11, 8)
#define UPBOARD_FW_ID_MINOR_MASK	GENMASK(7, 4)
#define UPBOARD_FW_ID_PATCH_MASK	GENMASK(3, 0)

static int upboard_fpga_read(void *context, unsigned int reg, unsigned int *val)
{
	struct upboard_fpga *fpga = context;
	int i;

	/* Clear to start new transaction */
	gpiod_set_value(fpga->clear_gpio, 0);
	gpiod_set_value(fpga->clear_gpio, 1);

	reg |= UPBOARD_READ_FLAG;

	/* Send clock and addr from strobe & datain pins */
	for (i = UPBOARD_ADDRESS_SIZE; i >= 0; i--) {
		gpiod_set_value(fpga->strobe_gpio, 0);
		gpiod_set_value(fpga->datain_gpio, !!(reg & BIT(i)));
		gpiod_set_value(fpga->strobe_gpio, 1);
	}

	gpiod_set_value(fpga->strobe_gpio, 0);
	*val = 0;

	/* Read data from dataout pin */
	for (i = UPBOARD_REGISTER_SIZE - 1; i >= 0; i--) {
		gpiod_set_value(fpga->strobe_gpio, 1);
		gpiod_set_value(fpga->strobe_gpio, 0);
		*val |= gpiod_get_value(fpga->dataout_gpio) << i;
	}

	gpiod_set_value(fpga->strobe_gpio, 1);

	return 0;
}

static int upboard_fpga_write(void *context, unsigned int reg, unsigned int val)
{
	struct upboard_fpga *fpga = context;
	int i;

	/* Clear to start new transcation */
	gpiod_set_value(fpga->clear_gpio, 0);
	gpiod_set_value(fpga->clear_gpio, 1);

	/* Send clock and addr from strobe & datain pins */
	for (i = UPBOARD_ADDRESS_SIZE; i >= 0; i--) {
		gpiod_set_value(fpga->strobe_gpio, 0);
		gpiod_set_value(fpga->datain_gpio, !!(reg & BIT(i)));
		gpiod_set_value(fpga->strobe_gpio, 1);
	}

	gpiod_set_value(fpga->strobe_gpio, 0);

	/* Write data to datain pin */
	for (i = UPBOARD_REGISTER_SIZE - 1; i >= 0; i--) {
		gpiod_set_value(fpga->datain_gpio, !!(val & BIT(i)));
		gpiod_set_value(fpga->strobe_gpio, 1);
		gpiod_set_value(fpga->strobe_gpio, 0);
	}

	gpiod_set_value(fpga->strobe_gpio, 1);

	return 0;
}

static const struct regmap_range upboard_up_readable_ranges[] = {
	regmap_reg_range(UPBOARD_REG_PLATFORM_ID, UPBOARD_REG_FIRMWARE_ID),
	regmap_reg_range(UPBOARD_REG_FUNC_EN0, UPBOARD_REG_FUNC_EN0),
	regmap_reg_range(UPBOARD_REG_GPIO_EN0, UPBOARD_REG_GPIO_EN1),
	regmap_reg_range(UPBOARD_REG_GPIO_DIR0, UPBOARD_REG_GPIO_DIR1),
};

static const struct regmap_range upboard_up_writable_ranges[] = {
	regmap_reg_range(UPBOARD_REG_FUNC_EN0, UPBOARD_REG_FUNC_EN0),
	regmap_reg_range(UPBOARD_REG_GPIO_EN0, UPBOARD_REG_GPIO_EN1),
	regmap_reg_range(UPBOARD_REG_GPIO_DIR0, UPBOARD_REG_GPIO_DIR1),
};

static const struct regmap_access_table upboard_up_readable_table = {
	.yes_ranges = upboard_up_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(upboard_up_readable_ranges),
};

static const struct regmap_access_table upboard_up_writable_table = {
	.yes_ranges = upboard_up_writable_ranges,
	.n_yes_ranges = ARRAY_SIZE(upboard_up_writable_ranges),
};

static const struct regmap_config upboard_up_regmap_config = {
	.reg_bits = UPBOARD_ADDRESS_SIZE,
	.val_bits = UPBOARD_REGISTER_SIZE,
	.max_register = UPBOARD_REG_MAX,
	.reg_read = upboard_fpga_read,
	.reg_write = upboard_fpga_write,
	.fast_io = false,
	.cache_type = REGCACHE_NONE,
	.rd_table = &upboard_up_readable_table,
	.wr_table = &upboard_up_writable_table,
};

static const struct regmap_range upboard_up2_readable_ranges[] = {
	regmap_reg_range(UPBOARD_REG_PLATFORM_ID, UPBOARD_REG_FIRMWARE_ID),
	regmap_reg_range(UPBOARD_REG_FUNC_EN0, UPBOARD_REG_FUNC_EN1),
	regmap_reg_range(UPBOARD_REG_GPIO_EN0, UPBOARD_REG_GPIO_EN2),
	regmap_reg_range(UPBOARD_REG_GPIO_DIR0, UPBOARD_REG_GPIO_DIR2),
};

static const struct regmap_range upboard_up2_writable_ranges[] = {
	regmap_reg_range(UPBOARD_REG_FUNC_EN0, UPBOARD_REG_FUNC_EN1),
	regmap_reg_range(UPBOARD_REG_GPIO_EN0, UPBOARD_REG_GPIO_EN2),
	regmap_reg_range(UPBOARD_REG_GPIO_DIR0, UPBOARD_REG_GPIO_DIR2),
};

static const struct regmap_access_table upboard_up2_readable_table = {
	.yes_ranges = upboard_up2_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(upboard_up2_readable_ranges),
};

static const struct regmap_access_table upboard_up2_writable_table = {
	.yes_ranges = upboard_up2_writable_ranges,
	.n_yes_ranges = ARRAY_SIZE(upboard_up2_writable_ranges),
};

static const struct regmap_config upboard_up2_regmap_config = {
	.reg_bits = UPBOARD_ADDRESS_SIZE,
	.val_bits = UPBOARD_REGISTER_SIZE,
	.max_register = UPBOARD_REG_MAX,
	.reg_read = upboard_fpga_read,
	.reg_write = upboard_fpga_write,
	.fast_io = false,
	.cache_type = REGCACHE_NONE,
	.rd_table = &upboard_up2_readable_table,
	.wr_table = &upboard_up2_writable_table,
};

static const struct mfd_cell upboard_up_mfd_cells[] = {
	{ .name = "upboard-pinctrl" },
	{ .name = "upboard-leds" },
};

static const struct upboard_fpga_data upboard_up_fpga_data = {
	.type = UPBOARD_UP_FPGA,
	.regmap_config = &upboard_up_regmap_config,
};

static const struct upboard_fpga_data upboard_up2_fpga_data = {
	.type = UPBOARD_UP2_FPGA,
	.regmap_config = &upboard_up2_regmap_config,
};

static int upboard_fpga_gpio_init(struct upboard_fpga *fpga)
{
	fpga->enable_gpio = devm_gpiod_get(fpga->dev, "enable", GPIOD_ASIS);
	if (IS_ERR(fpga->enable_gpio))
		return PTR_ERR(fpga->enable_gpio);

	fpga->clear_gpio = devm_gpiod_get(fpga->dev, "clear", GPIOD_OUT_LOW);
	if (IS_ERR(fpga->clear_gpio))
		return PTR_ERR(fpga->clear_gpio);

	fpga->strobe_gpio = devm_gpiod_get(fpga->dev, "strobe", GPIOD_OUT_LOW);
	if (IS_ERR(fpga->strobe_gpio))
		return PTR_ERR(fpga->strobe_gpio);

	fpga->datain_gpio = devm_gpiod_get(fpga->dev, "datain", GPIOD_OUT_LOW);
	if (IS_ERR(fpga->datain_gpio))
		return PTR_ERR(fpga->datain_gpio);

	fpga->dataout_gpio = devm_gpiod_get(fpga->dev, "dataout", GPIOD_IN);
	if (IS_ERR(fpga->dataout_gpio))
		return PTR_ERR(fpga->dataout_gpio);

	gpiod_set_value(fpga->enable_gpio, 1);

	return 0;
}

static int upboard_fpga_get_firmware_version(struct upboard_fpga *fpga)
{
	unsigned int platform_id, manufacturer_id;
	int ret;

	if (!fpga)
		return -ENOMEM;

	ret = regmap_read(fpga->regmap, UPBOARD_REG_PLATFORM_ID, &platform_id);
	if (ret)
		return ret;

	manufacturer_id = platform_id & UPBOARD_MANUFACTURER_ID_MASK;
	if (manufacturer_id != UPBOARD_AAEON_MANUFACTURER_ID)
		return dev_err_probe(fpga->dev, -ENODEV,
				     "driver not compatible with custom FPGA FW from manufacturer id %#02x.",
				     manufacturer_id);

	ret = regmap_read(fpga->regmap, UPBOARD_REG_FIRMWARE_ID, &fpga->firmware_version);
	if (ret)
		return ret;

	if (FIELD_GET(UPBOARD_FW_ID_MAJOR_MASK, fpga->firmware_version) !=
	    UPBOARD_FW_ID_MAJOR_SUPPORTED)
		return dev_err_probe(fpga->dev, -ENODEV,
				     "unsupported FPGA FW v%lu.%lu.%lu build %#02lx",
				     FIELD_GET(UPBOARD_FW_ID_MAJOR_MASK, fpga->firmware_version),
				     FIELD_GET(UPBOARD_FW_ID_MINOR_MASK, fpga->firmware_version),
				     FIELD_GET(UPBOARD_FW_ID_PATCH_MASK, fpga->firmware_version),
				     FIELD_GET(UPBOARD_FW_ID_BUILD_MASK, fpga->firmware_version));
	return 0;
}

static ssize_t upboard_fpga_version_show(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	struct upboard_fpga *fpga = dev_get_drvdata(dev);

	return sysfs_emit(buf, "FPGA FW v%lu.%lu.%lu build %#02lx\n",
			  FIELD_GET(UPBOARD_FW_ID_MAJOR_MASK, fpga->firmware_version),
			  FIELD_GET(UPBOARD_FW_ID_MINOR_MASK, fpga->firmware_version),
			  FIELD_GET(UPBOARD_FW_ID_PATCH_MASK, fpga->firmware_version),
			  FIELD_GET(UPBOARD_FW_ID_BUILD_MASK, fpga->firmware_version));
}

static DEVICE_ATTR_RO(upboard_fpga_version);

static struct attribute *upboard_fpga_attrs[] = {
	&dev_attr_upboard_fpga_version.attr,
	NULL
};

ATTRIBUTE_GROUPS(upboard_fpga);

static int upboard_fpga_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct upboard_fpga *fpga;
	int ret;

	fpga = devm_kzalloc(dev, sizeof(*fpga), GFP_KERNEL);
	if (!fpga)
		return -ENOMEM;

	fpga->fpga_data = device_get_match_data(dev);

	fpga->dev = dev;

	platform_set_drvdata(pdev, fpga);

	fpga->regmap = devm_regmap_init(dev, NULL, fpga, fpga->fpga_data->regmap_config);
	if (IS_ERR(fpga->regmap))
		return PTR_ERR(fpga->regmap);

	ret = upboard_fpga_gpio_init(fpga);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to initialize FPGA common GPIOs");

	ret = upboard_fpga_get_firmware_version(fpga);
	if (ret)
		return ret;

	return devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE, upboard_up_mfd_cells,
				    ARRAY_SIZE(upboard_up_mfd_cells), NULL, 0, NULL);
}

static const struct acpi_device_id upboard_fpga_acpi_match[] = {
	{ "AANT0F01", (kernel_ulong_t)&upboard_up2_fpga_data },
	{ "AANT0F04", (kernel_ulong_t)&upboard_up_fpga_data },
	{}
};
MODULE_DEVICE_TABLE(acpi, upboard_fpga_acpi_match);

static struct platform_driver upboard_fpga_driver = {
	.driver = {
		.name = "upboard-fpga",
		.acpi_match_table = ACPI_PTR(upboard_fpga_acpi_match),
		.dev_groups	= upboard_fpga_groups,
	},
	.probe = upboard_fpga_probe,
};

module_platform_driver(upboard_fpga_driver);

MODULE_AUTHOR("Gary Wang <garywang@aaeon.com.tw>");
MODULE_AUTHOR("Thomas Richard <thomas.richard@bootlin.com>");
MODULE_DESCRIPTION("UP Board FPGA driver");
MODULE_LICENSE("GPL");
