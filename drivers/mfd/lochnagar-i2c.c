// SPDX-License-Identifier: GPL-2.0
/*
 * Lochnagar I2C bus interface
 *
 * Copyright (c) 2012-2018 Cirrus Logic, Inc. and
 *                         Cirrus Logic International Semiconductor Ltd.
 *
 * Author: Charles Keepax <ckeepax@opensource.cirrus.com>
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/lockdep.h>
#include <linux/mfd/core.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>

#include <linux/mfd/lochnagar.h>
#include <linux/mfd/lochnagar1_regs.h>
#include <linux/mfd/lochnagar2_regs.h>

#define LOCHNAGAR_BOOT_RETRIES		10
#define LOCHNAGAR_BOOT_DELAY_MS		350

#define LOCHNAGAR_CONFIG_POLL_US	10000

static bool lochnagar1_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case LOCHNAGAR_SOFTWARE_RESET:
	case LOCHNAGAR_FIRMWARE_ID1...LOCHNAGAR_FIRMWARE_ID2:
	case LOCHNAGAR1_CDC_AIF1_SEL...LOCHNAGAR1_CDC_AIF3_SEL:
	case LOCHNAGAR1_CDC_MCLK1_SEL...LOCHNAGAR1_CDC_MCLK2_SEL:
	case LOCHNAGAR1_CDC_AIF_CTRL1...LOCHNAGAR1_CDC_AIF_CTRL2:
	case LOCHNAGAR1_EXT_AIF_CTRL:
	case LOCHNAGAR1_DSP_AIF1_SEL...LOCHNAGAR1_DSP_AIF2_SEL:
	case LOCHNAGAR1_DSP_CLKIN_SEL:
	case LOCHNAGAR1_DSP_AIF:
	case LOCHNAGAR1_GF_AIF1...LOCHNAGAR1_GF_AIF2:
	case LOCHNAGAR1_PSIA_AIF:
	case LOCHNAGAR1_PSIA1_SEL...LOCHNAGAR1_PSIA2_SEL:
	case LOCHNAGAR1_SPDIF_AIF_SEL:
	case LOCHNAGAR1_GF_AIF3_SEL...LOCHNAGAR1_GF_AIF4_SEL:
	case LOCHNAGAR1_GF_CLKOUT1_SEL:
	case LOCHNAGAR1_GF_AIF1_SEL...LOCHNAGAR1_GF_AIF2_SEL:
	case LOCHNAGAR1_GF_GPIO2...LOCHNAGAR1_GF_GPIO7:
	case LOCHNAGAR1_RST:
	case LOCHNAGAR1_LED1...LOCHNAGAR1_LED2:
	case LOCHNAGAR1_I2C_CTRL:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config lochnagar1_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,

	.max_register = 0x50,
	.readable_reg = lochnagar1_readable_register,

	.use_single_read = true,
	.use_single_write = true,

	.cache_type = REGCACHE_RBTREE,
};

static const struct reg_sequence lochnagar1_patch[] = {
	{ 0x40, 0x0083 },
	{ 0x47, 0x0018 },
	{ 0x50, 0x0000 },
};

static bool lochnagar2_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case LOCHNAGAR_SOFTWARE_RESET:
	case LOCHNAGAR_FIRMWARE_ID1...LOCHNAGAR_FIRMWARE_ID2:
	case LOCHNAGAR2_CDC_AIF1_CTRL...LOCHNAGAR2_CDC_AIF3_CTRL:
	case LOCHNAGAR2_DSP_AIF1_CTRL...LOCHNAGAR2_DSP_AIF2_CTRL:
	case LOCHNAGAR2_PSIA1_CTRL...LOCHNAGAR2_PSIA2_CTRL:
	case LOCHNAGAR2_GF_AIF3_CTRL...LOCHNAGAR2_GF_AIF4_CTRL:
	case LOCHNAGAR2_GF_AIF1_CTRL...LOCHNAGAR2_GF_AIF2_CTRL:
	case LOCHNAGAR2_SPDIF_AIF_CTRL:
	case LOCHNAGAR2_USB_AIF1_CTRL...LOCHNAGAR2_USB_AIF2_CTRL:
	case LOCHNAGAR2_ADAT_AIF_CTRL:
	case LOCHNAGAR2_CDC_MCLK1_CTRL...LOCHNAGAR2_CDC_MCLK2_CTRL:
	case LOCHNAGAR2_DSP_CLKIN_CTRL:
	case LOCHNAGAR2_PSIA1_MCLK_CTRL...LOCHNAGAR2_PSIA2_MCLK_CTRL:
	case LOCHNAGAR2_SPDIF_MCLK_CTRL:
	case LOCHNAGAR2_GF_CLKOUT1_CTRL...LOCHNAGAR2_GF_CLKOUT2_CTRL:
	case LOCHNAGAR2_ADAT_MCLK_CTRL:
	case LOCHNAGAR2_SOUNDCARD_MCLK_CTRL:
	case LOCHNAGAR2_GPIO_FPGA_GPIO1...LOCHNAGAR2_GPIO_FPGA_GPIO6:
	case LOCHNAGAR2_GPIO_CDC_GPIO1...LOCHNAGAR2_GPIO_CDC_GPIO8:
	case LOCHNAGAR2_GPIO_DSP_GPIO1...LOCHNAGAR2_GPIO_DSP_GPIO6:
	case LOCHNAGAR2_GPIO_GF_GPIO2...LOCHNAGAR2_GPIO_GF_GPIO7:
	case LOCHNAGAR2_GPIO_CDC_AIF1_BCLK...LOCHNAGAR2_GPIO_CDC_AIF3_TXDAT:
	case LOCHNAGAR2_GPIO_DSP_AIF1_BCLK...LOCHNAGAR2_GPIO_DSP_AIF2_TXDAT:
	case LOCHNAGAR2_GPIO_PSIA1_BCLK...LOCHNAGAR2_GPIO_PSIA2_TXDAT:
	case LOCHNAGAR2_GPIO_GF_AIF3_BCLK...LOCHNAGAR2_GPIO_GF_AIF4_TXDAT:
	case LOCHNAGAR2_GPIO_GF_AIF1_BCLK...LOCHNAGAR2_GPIO_GF_AIF2_TXDAT:
	case LOCHNAGAR2_GPIO_DSP_UART1_RX...LOCHNAGAR2_GPIO_DSP_UART2_TX:
	case LOCHNAGAR2_GPIO_GF_UART2_RX...LOCHNAGAR2_GPIO_GF_UART2_TX:
	case LOCHNAGAR2_GPIO_USB_UART_RX:
	case LOCHNAGAR2_GPIO_CDC_PDMCLK1...LOCHNAGAR2_GPIO_CDC_PDMDAT2:
	case LOCHNAGAR2_GPIO_CDC_DMICCLK1...LOCHNAGAR2_GPIO_CDC_DMICDAT4:
	case LOCHNAGAR2_GPIO_DSP_DMICCLK1...LOCHNAGAR2_GPIO_DSP_DMICDAT2:
	case LOCHNAGAR2_GPIO_I2C2_SCL...LOCHNAGAR2_GPIO_I2C4_SDA:
	case LOCHNAGAR2_GPIO_DSP_STANDBY:
	case LOCHNAGAR2_GPIO_CDC_MCLK1...LOCHNAGAR2_GPIO_CDC_MCLK2:
	case LOCHNAGAR2_GPIO_DSP_CLKIN:
	case LOCHNAGAR2_GPIO_PSIA1_MCLK...LOCHNAGAR2_GPIO_PSIA2_MCLK:
	case LOCHNAGAR2_GPIO_GF_GPIO1...LOCHNAGAR2_GPIO_GF_GPIO5:
	case LOCHNAGAR2_GPIO_DSP_GPIO20:
	case LOCHNAGAR2_GPIO_CHANNEL1...LOCHNAGAR2_GPIO_CHANNEL16:
	case LOCHNAGAR2_MINICARD_RESETS:
	case LOCHNAGAR2_ANALOGUE_PATH_CTRL1...LOCHNAGAR2_ANALOGUE_PATH_CTRL2:
	case LOCHNAGAR2_COMMS_CTRL4:
	case LOCHNAGAR2_SPDIF_CTRL:
	case LOCHNAGAR2_IMON_CTRL1...LOCHNAGAR2_IMON_CTRL4:
	case LOCHNAGAR2_IMON_DATA1...LOCHNAGAR2_IMON_DATA2:
	case LOCHNAGAR2_POWER_CTRL:
	case LOCHNAGAR2_MICVDD_CTRL1:
	case LOCHNAGAR2_MICVDD_CTRL2:
	case LOCHNAGAR2_VDDCORE_CDC_CTRL1:
	case LOCHNAGAR2_VDDCORE_CDC_CTRL2:
	case LOCHNAGAR2_SOUNDCARD_AIF_CTRL:
		return true;
	default:
		return false;
	}
}

static bool lochnagar2_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case LOCHNAGAR2_GPIO_CHANNEL1...LOCHNAGAR2_GPIO_CHANNEL16:
	case LOCHNAGAR2_ANALOGUE_PATH_CTRL1:
	case LOCHNAGAR2_IMON_CTRL3...LOCHNAGAR2_IMON_CTRL4:
	case LOCHNAGAR2_IMON_DATA1...LOCHNAGAR2_IMON_DATA2:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config lochnagar2_i2c_regmap = {
	.reg_bits = 16,
	.val_bits = 16,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,

	.max_register = 0x1F1F,
	.readable_reg = lochnagar2_readable_register,
	.volatile_reg = lochnagar2_volatile_register,

	.cache_type = REGCACHE_RBTREE,
};

static const struct reg_sequence lochnagar2_patch[] = {
	{ 0x00EE, 0x0000 },
};

struct lochnagar_config {
	int id;
	const char * const name;
	enum lochnagar_type type;
	const struct regmap_config *regmap;
	const struct reg_sequence *patch;
	int npatch;
};

static struct lochnagar_config lochnagar_configs[] = {
	{
		.id = 0x50,
		.name = "lochnagar1",
		.type = LOCHNAGAR1,
		.regmap = &lochnagar1_i2c_regmap,
		.patch = lochnagar1_patch,
		.npatch = ARRAY_SIZE(lochnagar1_patch),
	},
	{
		.id = 0xCB58,
		.name = "lochnagar2",
		.type = LOCHNAGAR2,
		.regmap = &lochnagar2_i2c_regmap,
		.patch = lochnagar2_patch,
		.npatch = ARRAY_SIZE(lochnagar2_patch),
	},
};

static const struct of_device_id lochnagar_of_match[] = {
	{ .compatible = "cirrus,lochnagar1", .data = &lochnagar_configs[0] },
	{ .compatible = "cirrus,lochnagar2", .data = &lochnagar_configs[1] },
	{},
};

static int lochnagar_wait_for_boot(struct regmap *regmap, unsigned int *id)
{
	int i, ret;

	for (i = 0; i < LOCHNAGAR_BOOT_RETRIES; ++i) {
		msleep(LOCHNAGAR_BOOT_DELAY_MS);

		/* The reset register will return the device ID when read */
		ret = regmap_read(regmap, LOCHNAGAR_SOFTWARE_RESET, id);
		if (!ret)
			return ret;
	}

	return -ETIMEDOUT;
}

/**
 * lochnagar_update_config - Synchronise the boards analogue configuration to
 *                           the hardware.
 *
 * @lochnagar: A pointer to the primary core data structure.
 *
 * Return: Zero on success or an appropriate negative error code on failure.
 */
int lochnagar_update_config(struct lochnagar *lochnagar)
{
	struct regmap *regmap = lochnagar->regmap;
	unsigned int done = LOCHNAGAR2_ANALOGUE_PATH_UPDATE_STS_MASK;
	int timeout_ms = LOCHNAGAR_BOOT_DELAY_MS * LOCHNAGAR_BOOT_RETRIES;
	unsigned int val = 0;
	int ret;

	lockdep_assert_held(&lochnagar->analogue_config_lock);

	if (lochnagar->type != LOCHNAGAR2)
		return 0;

	/*
	 * Toggle the ANALOGUE_PATH_UPDATE bit and wait for the device to
	 * acknowledge that any outstanding changes to the analogue
	 * configuration have been applied.
	 */
	ret = regmap_write(regmap, LOCHNAGAR2_ANALOGUE_PATH_CTRL1, 0);
	if (ret < 0)
		return ret;

	ret = regmap_write(regmap, LOCHNAGAR2_ANALOGUE_PATH_CTRL1,
			   LOCHNAGAR2_ANALOGUE_PATH_UPDATE_MASK);
	if (ret < 0)
		return ret;

	ret = regmap_read_poll_timeout(regmap,
				       LOCHNAGAR2_ANALOGUE_PATH_CTRL1, val,
				       (val & done), LOCHNAGAR_CONFIG_POLL_US,
				       timeout_ms * 1000);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(lochnagar_update_config);

static int lochnagar_i2c_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	const struct lochnagar_config *config = NULL;
	struct lochnagar *lochnagar;
	struct gpio_desc *reset, *present;
	unsigned int val;
	unsigned int firmwareid;
	unsigned int devid, rev;
	int ret;

	lochnagar = devm_kzalloc(dev, sizeof(*lochnagar), GFP_KERNEL);
	if (!lochnagar)
		return -ENOMEM;

	config = i2c_get_match_data(i2c);

	lochnagar->dev = dev;
	mutex_init(&lochnagar->analogue_config_lock);

	dev_set_drvdata(dev, lochnagar);

	reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(reset)) {
		ret = PTR_ERR(reset);
		dev_err(dev, "Failed to get reset GPIO: %d\n", ret);
		return ret;
	}

	present = devm_gpiod_get_optional(dev, "present", GPIOD_OUT_HIGH);
	if (IS_ERR(present)) {
		ret = PTR_ERR(present);
		dev_err(dev, "Failed to get present GPIO: %d\n", ret);
		return ret;
	}

	/* Leave the Lochnagar in reset for a reasonable amount of time */
	msleep(20);

	/* Bring Lochnagar out of reset */
	gpiod_set_value_cansleep(reset, 1);

	/* Identify Lochnagar */
	lochnagar->type = config->type;

	lochnagar->regmap = devm_regmap_init_i2c(i2c, config->regmap);
	if (IS_ERR(lochnagar->regmap)) {
		ret = PTR_ERR(lochnagar->regmap);
		dev_err(dev, "Failed to allocate register map: %d\n", ret);
		return ret;
	}

	/* Wait for Lochnagar to boot */
	ret = lochnagar_wait_for_boot(lochnagar->regmap, &val);
	if (ret < 0) {
		dev_err(dev, "Failed to read device ID: %d\n", ret);
		return ret;
	}

	devid = val & LOCHNAGAR_DEVICE_ID_MASK;
	rev = val & LOCHNAGAR_REV_ID_MASK;

	if (devid != config->id) {
		dev_err(dev,
			"ID does not match %s (expected 0x%x got 0x%x)\n",
			config->name, config->id, devid);
		return -ENODEV;
	}

	/* Identify firmware */
	ret = regmap_read(lochnagar->regmap, LOCHNAGAR_FIRMWARE_ID1, &val);
	if (ret < 0) {
		dev_err(dev, "Failed to read firmware id 1: %d\n", ret);
		return ret;
	}

	firmwareid = val;

	ret = regmap_read(lochnagar->regmap, LOCHNAGAR_FIRMWARE_ID2, &val);
	if (ret < 0) {
		dev_err(dev, "Failed to read firmware id 2: %d\n", ret);
		return ret;
	}

	firmwareid |= (val << config->regmap->val_bits);

	dev_info(dev, "Found %s (0x%x) revision %u firmware 0x%.6x\n",
		 config->name, devid, rev + 1, firmwareid);

	ret = regmap_register_patch(lochnagar->regmap, config->patch,
				    config->npatch);
	if (ret < 0) {
		dev_err(dev, "Failed to register patch: %d\n", ret);
		return ret;
	}

	ret = devm_of_platform_populate(dev);
	if (ret < 0) {
		dev_err(dev, "Failed to populate child nodes: %d\n", ret);
		return ret;
	}

	return ret;
}

static struct i2c_driver lochnagar_i2c_driver = {
	.driver = {
		.name = "lochnagar",
		.of_match_table = lochnagar_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = lochnagar_i2c_probe,
};

static int __init lochnagar_i2c_init(void)
{
	int ret;

	ret = i2c_add_driver(&lochnagar_i2c_driver);
	if (ret)
		pr_err("Failed to register Lochnagar driver: %d\n", ret);

	return ret;
}
subsys_initcall(lochnagar_i2c_init);
