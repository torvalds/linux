// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021 Intel Corporation.*/

#include <linux/bitfield.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>

#include <linux/i3c/device.h>
#include <linux/i3c/master.h>

#define I3C_HUB_TP_MAX_COUNT				0x08

/* I3C HUB REGISTERS */

/*
 * In this driver Controller - Target convention is used. All the abbreviations are
 * based on this convention. For instance: CP - Controller Port, TP - Target Port.
 */

/* Device Information Registers */
#define I3C_HUB_DEV_INFO_0				0x00
#define I3C_HUB_DEV_INFO_1				0x01
#define I3C_HUB_PID_5					0x02
#define I3C_HUB_PID_4					0x03
#define I3C_HUB_PID_3					0x04
#define I3C_HUB_PID_2					0x05
#define I3C_HUB_PID_1					0x06
#define I3C_HUB_PID_0					0x07
#define I3C_HUB_BCR					0x08
#define I3C_HUB_DCR					0x09
#define I3C_HUB_DEV_CAPAB				0x0A
#define I3C_HUB_DEV_REV					0x0B

/* Device Configuration Registers */
#define I3C_HUB_PROTECTION_CODE				0x10
#define  REGISTERS_LOCK_CODE				0x00
#define  REGISTERS_UNLOCK_CODE				0x69
#define  CP1_REGISTERS_UNLOCK_CODE			0x6A

#define I3C_HUB_CP_CONF					0x11
#define I3C_HUB_TP_ENABLE				0x12
#define  TPn_ENABLE(n)					BIT(n)

#define I3C_HUB_DEV_CONF				0x13
#define I3C_HUB_IO_STRENGTH				0x14
#define I3C_HUB_NET_OPER_MODE_CONF			0x15
#define I3C_HUB_LDO_CONF				0x16
#define  CP0_LDO_VOLTAGE_MASK				GENMASK(1, 0)
#define  CP0_LDO_VOLTAGE(x)				(((x) << 0) & CP0_LDO_VOLTAGE_MASK)
#define  CP1_LDO_VOLTAGE_MASK				GENMASK(3, 2)
#define  CP1_LDO_VOLTAGE(x)				(((x) << 2) & CP1_LDO_VOLTAGE_MASK)
#define  TP0145_LDO_VOLTAGE_MASK			GENMASK(5, 4)
#define  TP0145_LDO_VOLTAGE(x)				(((x) << 4) & TP0145_LDO_VOLTAGE_MASK)
#define  TP2367_LDO_VOLTAGE_MASK			GENMASK(7, 6)
#define  TP2367_LDO_VOLTAGE(x)				(((x) << 6) & TP2367_LDO_VOLTAGE_MASK)
#define  LDO_VOLTAGE_1_0V				0x00
#define  LDO_VOLTAGE_1_1V				0x01
#define  LDO_VOLTAGE_1_2V				0x02
#define  LDO_VOLTAGE_1_8V				0x03

#define I3C_HUB_TP_IO_MODE_CONF				0x17
#define I3C_HUB_TP_SMBUS_AGNT_EN			0x18
#define  TPn_SMBUS_MODE_EN(n)				BIT(n)

#define I3C_HUB_LDO_AND_PULLUP_CONF			0x19
#define  CP0_LDO_EN					BIT(0)
#define  CP1_LDO_EN					BIT(1)
/*
 * I3C HUB does not provide a way to control LDO or pull-up for individual ports. It is possible
 * for group of ports TP0/TP1/TP4/TP5 and TP2/TP3/TP6/TP7.
 */
#define  TP0145_LDO_EN					BIT(2)
#define  TP2367_LDO_EN					BIT(3)
#define  TP0145_PULLUP_CONF_MASK			GENMASK(7, 6)
#define  TP0145_PULLUP_CONF(x)				(((x) << 6) & TP0145_PULLUP_CONF_MASK)
#define  TP2367_PULLUP_CONF_MASK			GENMASK(5, 4)
#define  TP2367_PULLUP_CONF(x)				(((x) << 4) & TP2367_PULLUP_CONF_MASK)
#define  PULLUP_250R					0x00
#define  PULLUP_500R					0x01
#define  PULLUP_1K					0x02
#define  PULLUP_2K					0x03

#define I3C_HUB_CP_IBI_CONF				0x1A
#define I3C_HUB_TP_IBI_CONF				0x1B
#define I3C_HUB_IBI_MDB_CUSTOM				0x1C
#define I3C_HUB_JEDEC_CONTEXT_ID			0x1D
#define I3C_HUB_TP_GPIO_MODE_EN				0x1E
#define  TPn_GPIO_MODE_EN(n)				BIT(n)

/* Device Status and IBI Registers */
#define I3C_HUB_DEV_AND_IBI_STS				0x20
#define I3C_HUB_TP_SMBUS_AGNT_IBI_STS			0x21

/* Controller Port Control/Status Registers */
#define I3C_HUB_CP_MUX_SET				0x38
#define I3C_HUB_CP_MUX_STS				0x39

/* Target Ports Control Registers */
#define I3C_HUB_TP_SMBUS_AGNT_TRANS_START		0x50
#define I3C_HUB_TP_NET_CON_CONF				0x51
#define  TPn_NET_CON(n)					BIT(n)

#define I3C_HUB_TP_PULLUP_EN				0x53
#define  TPn_PULLUP_EN(n)				BIT(n)

#define I3C_HUB_TP_SCL_OUT_EN				0x54
#define I3C_HUB_TP_SDA_OUT_EN				0x55
#define I3C_HUB_TP_SCL_OUT_LEVEL			0x56
#define I3C_HUB_TP_SDA_OUT_LEVEL			0x57
#define I3C_HUB_TP_IN_DETECT_MODE_CONF			0x58
#define I3C_HUB_TP_SCL_IN_DETECT_IBI_EN			0x59
#define I3C_HUB_TP_SDA_IN_DETECT_IBI_EN			0x5A

/* Target Ports Status Registers */
#define I3C_HUB_TP_SCL_IN_LEVEL_STS			0x60
#define I3C_HUB_TP_SDA_IN_LEVEL_STS			0x61
#define I3C_HUB_TP_SCL_IN_DETECT_FLG			0x62
#define I3C_HUB_TP_SDA_IN_DETECT_FLG			0x63

/* SMBus Agent Configuration and Status Registers */
#define I3C_HUB_TP0_SMBUS_AGNT_STS			0x64
#define I3C_HUB_TP1_SMBUS_AGNT_STS			0x65
#define I3C_HUB_TP2_SMBUS_AGNT_STS			0x66
#define I3C_HUB_TP3_SMBUS_AGNT_STS			0x67
#define I3C_HUB_TP4_SMBUS_AGNT_STS			0x68
#define I3C_HUB_TP5_SMBUS_AGNT_STS			0x69
#define I3C_HUB_TP6_SMBUS_AGNT_STS			0x6A
#define I3C_HUB_TP7_SMBUS_AGNT_STS			0x6B
#define I3C_HUB_ONCHIP_TD_AND_SMBUS_AGNT_CONF		0x6C

/* Special Function Registers */
#define I3C_HUB_LDO_AND_CPSEL_STS			0x79
#define I3C_HUB_BUS_RESET_SCL_TIMEOUT			0x7A
#define I3C_HUB_ONCHIP_TD_PROTO_ERR_FLG			0x7B
#define I3C_HUB_DEV_CMD					0x7C
#define I3C_HUB_ONCHIP_TD_STS				0x7D
#define I3C_HUB_ONCHIP_TD_ADDR_CONF			0x7E
#define I3C_HUB_PAGE_PTR				0x7F

/* LDO DT settings */
#define I3C_HUB_DT_LDO_DISABLED				0x00
#define I3C_HUB_DT_LDO_1_0V				0x01
#define I3C_HUB_DT_LDO_1_1V				0x02
#define I3C_HUB_DT_LDO_1_2V				0x03
#define I3C_HUB_DT_LDO_1_8V				0x04
#define I3C_HUB_DT_LDO_NOT_DEFINED			0xFF

/* Pull-up DT settings */
#define I3C_HUB_DT_PULLUP_DISABLED			0x00
#define I3C_HUB_DT_PULLUP_250R				0x01
#define I3C_HUB_DT_PULLUP_500R				0x02
#define I3C_HUB_DT_PULLUP_1K				0x03
#define I3C_HUB_DT_PULLUP_2K				0x04
#define I3C_HUB_DT_PULLUP_NOT_DEFINED			0xFF

/* TP DT setting */
#define I3C_HUB_DT_TP_MODE_DISABLED			0x00
#define I3C_HUB_DT_TP_MODE_I3C				0x01
#define I3C_HUB_DT_TP_MODE_I3C_PERF			0x02
#define I3C_HUB_DT_TP_MODE_SMBUS			0x03
#define I3C_HUB_DT_TP_MODE_GPIO				0x04
#define I3C_HUB_DT_TP_MODE_NOT_DEFINED			0xFF

/* TP pull-up status */
#define I3C_HUB_DT_TP_PULLUP_DISABLED			0x00
#define I3C_HUB_DT_TP_PULLUP_ENABLED			0x01
#define I3C_HUB_DT_TP_PULLUP_NOT_DEFINED		0xFF

struct tp_setting {
	u8 mode;
	u8 pullup_en;
};

struct dt_settings {
	u8 cp0_ldo;
	u8 cp1_ldo;
	u8 tp0145_ldo;
	u8 tp2367_ldo;
	u8 tp0145_pullup;
	u8 tp2367_pullup;
	struct tp_setting tp[I3C_HUB_TP_MAX_COUNT];
};

struct i3c_hub {
	struct i3c_device *i3cdev;
	struct regmap *regmap;
	struct dt_settings settings;

	/* Offset for reading HUB's register. */
	u8 reg_addr;
	struct dentry *debug_dir;
};

struct hub_setting {
	const char * const name;
	const u8 value;
};

static const struct hub_setting ldo_settings[] = {
	{"disabled",	I3C_HUB_DT_LDO_DISABLED},
	{"1.0V",	I3C_HUB_DT_LDO_1_0V},
	{"1.1V",	I3C_HUB_DT_LDO_1_1V},
	{"1.2V",	I3C_HUB_DT_LDO_1_2V},
	{"1.8V",	I3C_HUB_DT_LDO_1_8V},
};

static const struct hub_setting pullup_settings[] = {
	{"disabled",	I3C_HUB_DT_PULLUP_DISABLED},
	{"250R",	I3C_HUB_DT_PULLUP_250R},
	{"500R",	I3C_HUB_DT_PULLUP_500R},
	{"1k",		I3C_HUB_DT_PULLUP_1K},
	{"2k",		I3C_HUB_DT_PULLUP_2K},
};

static const struct hub_setting tp_mode_settings[] = {
	{"disabled",	I3C_HUB_DT_TP_MODE_DISABLED},
	{"i3c",		I3C_HUB_DT_TP_MODE_I3C},
	{"i3c-perf",	I3C_HUB_DT_TP_MODE_I3C_PERF},
	{"smbus",	I3C_HUB_DT_TP_MODE_SMBUS},
	{"gpio",	I3C_HUB_DT_TP_MODE_GPIO},
};

static const struct hub_setting tp_pullup_settings[] = {
	{"disabled",	I3C_HUB_DT_TP_PULLUP_DISABLED},
	{"enabled",	I3C_HUB_DT_TP_PULLUP_ENABLED},
};

static u8 i3c_hub_ldo_dt_to_reg(u8 dt_value)
{
	switch (dt_value) {
	case I3C_HUB_DT_LDO_1_1V:
		return LDO_VOLTAGE_1_1V;
	case I3C_HUB_DT_LDO_1_2V:
		return LDO_VOLTAGE_1_2V;
	case I3C_HUB_DT_LDO_1_8V:
		return LDO_VOLTAGE_1_8V;
	default:
		return LDO_VOLTAGE_1_0V;
	}
}

static u8 i3c_hub_pullup_dt_to_reg(u8 dt_value)
{
	switch (dt_value) {
	case I3C_HUB_DT_PULLUP_250R:
		return PULLUP_250R;
	case I3C_HUB_DT_PULLUP_500R:
		return PULLUP_500R;
	case I3C_HUB_DT_PULLUP_1K:
		return PULLUP_1K;
	default:
		return PULLUP_2K;
	}
}

static int i3c_hub_of_get_setting(const struct device_node *node, const char *setting_name,
				  const struct hub_setting settings[], const u8 settings_count,
				  u8 *setting_value)
{
	const char *sval;
	int ret;
	int i;

	ret = of_property_read_string(node, setting_name, &sval);
	if (ret)
		return ret;

	for (i = 0; i < settings_count; ++i) {
		const struct hub_setting * const setting = &settings[i];

		if (!strcmp(setting->name, sval)) {
			*setting_value = setting->value;
			return 0;
		}
	}

	return -EINVAL;
}

static void i3c_hub_tp_of_get_setting(struct device *dev, const struct device_node *node,
				      struct tp_setting tp_setting[])
{
	struct device_node *tp_node;
	int id;

	for_each_available_child_of_node(node, tp_node) {
		int ret;

		if (!tp_node->name || of_node_cmp(tp_node->name, "target-port"))
			continue;

		if (!tp_node->full_name ||
		    (sscanf(tp_node->full_name, "target-port@%i", &id) != 1)) {
			dev_warn(dev, "Invalid target port node found in DT - %s\n",
				 tp_node->full_name);
			continue;
		}

		if (id >= I3C_HUB_TP_MAX_COUNT) {
			dev_warn(dev, "Invalid target port index found in DT - %i\n", id);
			continue;
		}
		ret = i3c_hub_of_get_setting(tp_node, "mode", tp_mode_settings,
					     ARRAY_SIZE(tp_mode_settings), &tp_setting[id].mode);
		if (ret)
			dev_warn(dev, "Invalid or not specified setting for target port[%i].mode\n",
				 id);

		ret = i3c_hub_of_get_setting(tp_node, "pullup", tp_pullup_settings,
					     ARRAY_SIZE(tp_pullup_settings),
					     &tp_setting[id].pullup_en);
		if (ret)
			dev_warn(dev,
				 "Invalid or not specified setting for target port[%i].pullup\n",
				 id);
	}
}

static void i3c_hub_of_get_configuration(struct device *dev, const struct device_node *node)
{
	struct i3c_hub *priv = dev_get_drvdata(dev);
	int ret;

	ret = i3c_hub_of_get_setting(node, "cp0-ldo", ldo_settings, ARRAY_SIZE(ldo_settings),
				     &priv->settings.cp0_ldo);
	if (ret)
		dev_warn(dev, "Invalid or not specified setting for cp0-ldo\n");

	ret = i3c_hub_of_get_setting(node, "cp1-ldo", ldo_settings, ARRAY_SIZE(ldo_settings),
				     &priv->settings.cp1_ldo);
	if (ret)
		dev_warn(dev, "Invalid or not specified setting for cp1-ldo\n");

	ret = i3c_hub_of_get_setting(node, "tp0145-ldo", ldo_settings, ARRAY_SIZE(ldo_settings),
				     &priv->settings.tp0145_ldo);
	if (ret)
		dev_warn(dev, "Invalid or not specified setting for tp0145-ldo\n");

	ret = i3c_hub_of_get_setting(node, "tp2367-ldo", ldo_settings, ARRAY_SIZE(ldo_settings),
				     &priv->settings.tp2367_ldo);
	if (ret)
		dev_warn(dev, "Invalid or not specified setting for tp2367-ldo\n");

	ret = i3c_hub_of_get_setting(node, "tp0145-pullup", pullup_settings,
				     ARRAY_SIZE(pullup_settings), &priv->settings.tp0145_pullup);
	if (ret)
		dev_warn(dev, "Invalid or not specified setting for tp0145-pullup\n");

	ret = i3c_hub_of_get_setting(node, "tp2367-pullup", pullup_settings,
				     ARRAY_SIZE(pullup_settings), &priv->settings.tp2367_pullup);
	if (ret)
		dev_warn(dev, "Invalid or not specified setting for tp2367-pullup\n");

	i3c_hub_tp_of_get_setting(dev, node, priv->settings.tp);
}

static void i3c_hub_of_default_configuration(struct device *dev)
{
	struct i3c_hub *priv = dev_get_drvdata(dev);
	int id;

	priv->settings.cp0_ldo = I3C_HUB_DT_LDO_NOT_DEFINED;
	priv->settings.cp1_ldo = I3C_HUB_DT_LDO_NOT_DEFINED;
	priv->settings.tp0145_ldo = I3C_HUB_DT_LDO_NOT_DEFINED;
	priv->settings.tp2367_ldo = I3C_HUB_DT_LDO_NOT_DEFINED;
	priv->settings.tp0145_pullup = I3C_HUB_DT_PULLUP_NOT_DEFINED;
	priv->settings.tp2367_pullup = I3C_HUB_DT_PULLUP_NOT_DEFINED;

	for (id = 0; id < I3C_HUB_TP_MAX_COUNT; ++id) {
		priv->settings.tp[id].mode = I3C_HUB_DT_TP_MODE_NOT_DEFINED;
		priv->settings.tp[id].pullup_en = I3C_HUB_DT_TP_PULLUP_NOT_DEFINED;
	}
}

static int i3c_hub_hw_configure_pullup(struct device *dev)
{
	struct i3c_hub *priv = dev_get_drvdata(dev);
	u8 mask = 0, value = 0;

	if (priv->settings.tp0145_pullup != I3C_HUB_DT_PULLUP_NOT_DEFINED) {
		mask |= TP0145_PULLUP_CONF_MASK;
		value |= TP0145_PULLUP_CONF(i3c_hub_pullup_dt_to_reg(priv->settings.tp0145_pullup));
	}

	if (priv->settings.tp2367_pullup != I3C_HUB_DT_PULLUP_NOT_DEFINED) {
		mask |= TP2367_PULLUP_CONF_MASK;
		value |= TP2367_PULLUP_CONF(i3c_hub_pullup_dt_to_reg(priv->settings.tp2367_pullup));
	}

	return regmap_update_bits(priv->regmap, I3C_HUB_LDO_AND_PULLUP_CONF, mask, value);
}

static int i3c_hub_hw_configure_ldo(struct device *dev)
{
	struct i3c_hub *priv = dev_get_drvdata(dev);
	u8 mask_all = 0, val_all = 0;
	u8 ldo_dis = 0, ldo_en = 0;
	u32 reg_val;
	u8 val;
	int ret;

	/* Get LDOs configuration to figure out what is going to be changed */
	ret = regmap_read(priv->regmap, I3C_HUB_LDO_CONF, &reg_val);
	if (ret)
		return ret;

	if (priv->settings.cp0_ldo != I3C_HUB_DT_LDO_NOT_DEFINED) {
		val = CP0_LDO_VOLTAGE(i3c_hub_ldo_dt_to_reg(priv->settings.cp0_ldo));
		if ((reg_val & CP0_LDO_VOLTAGE_MASK) != val)
			ldo_dis |= CP0_LDO_EN;
		if (priv->settings.cp0_ldo != I3C_HUB_DT_LDO_DISABLED)
			ldo_en |= CP0_LDO_EN;
		mask_all |= CP0_LDO_VOLTAGE_MASK;
		val_all |= val;
	}
	if (priv->settings.cp1_ldo != I3C_HUB_DT_LDO_NOT_DEFINED) {
		val = CP1_LDO_VOLTAGE(i3c_hub_ldo_dt_to_reg(priv->settings.cp1_ldo));
		if ((reg_val & CP1_LDO_VOLTAGE_MASK) != val)
			ldo_dis |= CP1_LDO_EN;
		if (priv->settings.cp1_ldo != I3C_HUB_DT_LDO_DISABLED)
			ldo_en |= CP1_LDO_EN;
		mask_all |= CP1_LDO_VOLTAGE_MASK;
		val_all |= val;
	}
	if (priv->settings.tp0145_ldo != I3C_HUB_DT_LDO_NOT_DEFINED) {
		val = TP0145_LDO_VOLTAGE(i3c_hub_ldo_dt_to_reg(priv->settings.tp0145_ldo));
		if ((reg_val & TP0145_LDO_VOLTAGE_MASK) != val)
			ldo_dis |= TP0145_LDO_EN;
		if (priv->settings.tp0145_ldo != I3C_HUB_DT_LDO_DISABLED)
			ldo_en |= TP0145_LDO_EN;
		mask_all |= TP0145_LDO_VOLTAGE_MASK;
		val_all |= val;
	}
	if (priv->settings.tp2367_ldo != I3C_HUB_DT_LDO_NOT_DEFINED) {
		val = TP2367_LDO_VOLTAGE(i3c_hub_ldo_dt_to_reg(priv->settings.tp2367_ldo));
		if ((reg_val & TP2367_LDO_VOLTAGE_MASK) != val)
			ldo_dis |= TP2367_LDO_EN;
		if (priv->settings.tp2367_ldo != I3C_HUB_DT_LDO_DISABLED)
			ldo_en |= TP2367_LDO_EN;
		mask_all |= TP2367_LDO_VOLTAGE_MASK;
		val_all |= val;
	}

	/* Disable all LDOs if LDO configuration is going to be changed. */
	ret = regmap_update_bits(priv->regmap, I3C_HUB_LDO_AND_PULLUP_CONF, ldo_dis, 0);
	if (ret)
		return ret;

	/* Set LDOs configuration */
	ret = regmap_update_bits(priv->regmap, I3C_HUB_LDO_CONF, mask_all, val_all);
	if (ret)
		return ret;

	/* Re-enable LDOs if needed */
	return regmap_update_bits(priv->regmap, I3C_HUB_LDO_AND_PULLUP_CONF, ldo_en, ldo_en);
}

static int i3c_hub_hw_configure_tp(struct device *dev)
{
	struct i3c_hub *priv = dev_get_drvdata(dev);
	u8 pullup_mask = 0, pullup_val = 0;
	u8 smbus_mask = 0, smbus_val = 0;
	u8 gpio_mask = 0, gpio_val = 0;
	u8 i3c_mask = 0, i3c_val = 0;
	int ret;
	int i;

	/* TBD: Read type of HUB from register I3C_HUB_DEV_INFO_0 to learn target ports count. */
	for (i = 0; i < I3C_HUB_TP_MAX_COUNT; ++i) {
		if (priv->settings.tp[i].mode != I3C_HUB_DT_TP_MODE_NOT_DEFINED) {
			i3c_mask |= TPn_NET_CON(i);
			smbus_mask |= TPn_SMBUS_MODE_EN(i);
			gpio_mask |= TPn_GPIO_MODE_EN(i);

			if (priv->settings.tp[i].mode == I3C_HUB_DT_TP_MODE_I3C)
				i3c_val |= TPn_NET_CON(i);
			else if (priv->settings.tp[i].mode == I3C_HUB_DT_TP_MODE_SMBUS)
				smbus_val |= TPn_SMBUS_MODE_EN(i);
			else if (priv->settings.tp[i].mode == I3C_HUB_DT_TP_MODE_GPIO)
				gpio_val |= TPn_GPIO_MODE_EN(i);
		}
		if (priv->settings.tp[i].pullup_en != I3C_HUB_DT_TP_PULLUP_NOT_DEFINED) {
			pullup_mask |= TPn_PULLUP_EN(i);
			if (priv->settings.tp[i].pullup_en == I3C_HUB_DT_TP_PULLUP_ENABLED)
				pullup_val |= TPn_PULLUP_EN(i);
		}
	}

	ret = regmap_update_bits(priv->regmap, I3C_HUB_TP_NET_CON_CONF, i3c_mask, i3c_val);
	if (ret)
		return ret;

	ret = regmap_update_bits(priv->regmap, I3C_HUB_TP_SMBUS_AGNT_EN, smbus_mask, smbus_val);
	if (ret)
		return ret;

	ret = regmap_update_bits(priv->regmap, I3C_HUB_TP_GPIO_MODE_EN, gpio_mask, gpio_val);
	if (ret)
		return ret;

	/* Enable TP here in case TP was configured */
	ret = regmap_update_bits(priv->regmap, I3C_HUB_TP_ENABLE, i3c_mask | smbus_mask | gpio_mask,
				 i3c_val | smbus_val | gpio_val);
	if (ret)
		return ret;

	return regmap_update_bits(priv->regmap, I3C_HUB_TP_PULLUP_EN, pullup_mask, pullup_val);
}

static int i3c_hub_configure_hw(struct device *dev)
{
	int ret;

	ret = i3c_hub_hw_configure_pullup(dev);
	if (ret)
		return ret;

	ret = i3c_hub_hw_configure_ldo(dev);
	if (ret)
		return ret;

	return i3c_hub_hw_configure_tp(dev);
}

static const struct i3c_device_id i3c_hub_ids[] = {
	I3C_CLASS(I3C_DCR_HUB, NULL),
	{ },
};

static int fops_access_reg_get(void *ctx, u64 *val)
{
	struct i3c_hub *priv = ctx;
	u32 reg_val;
	int ret;

	ret = regmap_read(priv->regmap, priv->reg_addr, &reg_val);
	if (ret)
		return ret;

	*val = reg_val & 0xFF;
	return 0;
}

static int fops_access_reg_set(void *ctx, u64 val)
{
	struct i3c_hub *priv = ctx;

	return regmap_write(priv->regmap, priv->reg_addr, val & 0xFF);
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_access_reg, fops_access_reg_get, fops_access_reg_set, "0x%llX\n");

static int i3c_hub_debugfs_init(struct i3c_hub *priv, const char *hub_id)
{
	struct dentry  *entry, *dt_conf_dir, *reg_dir;
	int i;

	entry = debugfs_create_dir(hub_id, NULL);
	if (IS_ERR(entry))
		return PTR_ERR(entry);

	priv->debug_dir = entry;

	entry = debugfs_create_dir("dt-conf", priv->debug_dir);
	if (IS_ERR(entry))
		goto err_remove;

	dt_conf_dir = entry;

	debugfs_create_u8("cp0-ldo", 0400, dt_conf_dir, &priv->settings.cp0_ldo);
	debugfs_create_u8("cp1-ldo", 0400, dt_conf_dir, &priv->settings.cp1_ldo);
	debugfs_create_u8("tp0145-ldo", 0400, dt_conf_dir, &priv->settings.tp0145_ldo);
	debugfs_create_u8("tp2367-ldo", 0400, dt_conf_dir, &priv->settings.tp2367_ldo);
	debugfs_create_u8("tp0145-pullup", 0400, dt_conf_dir, &priv->settings.tp0145_pullup);
	debugfs_create_u8("tp2367-pullup", 0400, dt_conf_dir, &priv->settings.tp2367_pullup);

	for (i = 0; i < I3C_HUB_TP_MAX_COUNT; ++i) {
		char file_name[32];

		sprintf(file_name, "tp%i.mode", i);
		debugfs_create_u8(file_name, 0400, dt_conf_dir, &priv->settings.tp[i].mode);
		sprintf(file_name, "tp%i.pullup_en", i);
		debugfs_create_u8(file_name, 0400, dt_conf_dir, &priv->settings.tp[i].pullup_en);
	}

	entry = debugfs_create_dir("reg", priv->debug_dir);
	if (IS_ERR(entry))
		goto err_remove;

	reg_dir = entry;

	entry = debugfs_create_file_unsafe("access", 0600, reg_dir, priv, &fops_access_reg);
	if (IS_ERR(entry))
		goto err_remove;

	debugfs_create_u8("offset", 0600, reg_dir, &priv->reg_addr);

	return 0;

err_remove:
	debugfs_remove_recursive(priv->debug_dir);
	return PTR_ERR(entry);
}

static int i3c_hub_probe(struct i3c_device *i3cdev)
{
	struct regmap_config i3c_hub_regmap_config = {
		.reg_bits = 8,
		.val_bits = 8,
	};
	struct device *dev = &i3cdev->dev;
	struct device_node *node;
	struct regmap *regmap;
	struct i3c_hub *priv;
	char hub_id[32];
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->i3cdev = i3cdev;
	i3cdev_set_drvdata(i3cdev, priv);

	sprintf(hub_id, "i3c-hub-%d-%llx", i3cdev->bus->id, i3cdev->desc->info.pid);
	ret = i3c_hub_debugfs_init(priv, hub_id);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to initialized DebugFS.\n");

	i3c_hub_of_default_configuration(dev);

	/* TBD: Support for multiple HUBs. */
	/* Just get first hub node from DT */
	node = of_get_child_by_name(dev->parent->of_node, "hub");
	if (!node) {
		dev_warn(dev, "Failed to find DT entry for the driver. Running with defaults.\n");
	} else {
		i3c_hub_of_get_configuration(dev, node);
		of_node_put(node);
	}

	regmap = devm_regmap_init_i3c(i3cdev, &i3c_hub_regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(dev, "Failed to register I3C HUB regmap\n");
		goto error;
	}

	priv->regmap = regmap;

	/* Unlock access to protected registers */
	ret = regmap_write(priv->regmap, I3C_HUB_PROTECTION_CODE, REGISTERS_UNLOCK_CODE);
	if (ret) {
		dev_err(dev, "Failed to unlock HUB's protected registers\n");
		goto error;
	}

	ret = i3c_hub_configure_hw(dev);
	if (ret) {
		dev_err(dev, "Failed to configure the HUB\n");
		goto error;
	}

	/* Lock access to protected registers */
	ret = regmap_write(priv->regmap, I3C_HUB_PROTECTION_CODE, REGISTERS_LOCK_CODE);
	if (ret) {
		dev_err(dev, "Failed to lock HUB's protected registers\n");
		goto error;
	}

	/* TBD: Apply special/security lock here using DEV_CMD register */

	return 0;

error:
	debugfs_remove_recursive(priv->debug_dir);
	return ret;
}

static void i3c_hub_remove(struct i3c_device *i3cdev)
{
	struct i3c_hub *priv = i3cdev_get_drvdata(i3cdev);

	debugfs_remove_recursive(priv->debug_dir);
}

static struct i3c_driver i3c_hub = {
	.driver.name = "i3c-hub",
	.id_table = i3c_hub_ids,
	.probe = i3c_hub_probe,
	.remove = i3c_hub_remove,
};

module_i3c_driver(i3c_hub);

MODULE_AUTHOR("Zbigniew Lukwinski <zbigniew.lukwinski@linux.intel.com>");
MODULE_DESCRIPTION("I3C HUB driver");
MODULE_LICENSE("GPL");
