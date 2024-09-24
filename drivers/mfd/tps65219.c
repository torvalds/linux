// SPDX-License-Identifier: GPL-2.0
//
// Driver for TPS65219 Integrated Power Management Integrated Chips (PMIC)
//
// Copyright (C) 2022 BayLibre Incorporated - https://www.baylibre.com/

#include <linux/i2c.h>
#include <linux/reboot.h>
#include <linux/regmap.h>

#include <linux/mfd/core.h>
#include <linux/mfd/tps65219.h>

static int tps65219_warm_reset(struct tps65219 *tps)
{
	return regmap_update_bits(tps->regmap, TPS65219_REG_MFP_CTRL,
				  TPS65219_MFP_WARM_RESET_I2C_CTRL_MASK,
				  TPS65219_MFP_WARM_RESET_I2C_CTRL_MASK);
}

static int tps65219_cold_reset(struct tps65219 *tps)
{
	return regmap_update_bits(tps->regmap, TPS65219_REG_MFP_CTRL,
				  TPS65219_MFP_COLD_RESET_I2C_CTRL_MASK,
				  TPS65219_MFP_COLD_RESET_I2C_CTRL_MASK);
}

static int tps65219_soft_shutdown(struct tps65219 *tps)
{
	return regmap_update_bits(tps->regmap, TPS65219_REG_MFP_CTRL,
				  TPS65219_MFP_I2C_OFF_REQ_MASK,
				  TPS65219_MFP_I2C_OFF_REQ_MASK);
}

static int tps65219_power_off_handler(struct sys_off_data *data)
{
	tps65219_soft_shutdown(data->cb_data);
	return NOTIFY_DONE;
}

static int tps65219_restart(struct tps65219 *tps, unsigned long reboot_mode)
{
	if (reboot_mode == REBOOT_WARM)
		tps65219_warm_reset(tps);
	else
		tps65219_cold_reset(tps);

	return NOTIFY_DONE;
}

static int tps65219_restart_handler(struct sys_off_data *data)
{
	tps65219_restart(data->cb_data, data->mode);
	return NOTIFY_DONE;
}

static const struct resource tps65219_pwrbutton_resources[] = {
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_PB_FALLING_EDGE_DETECT, "falling"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_PB_RISING_EDGE_DETECT, "rising"),
};

static const struct resource tps65219_regulator_resources[] = {
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_LDO3_SCG, "LDO3_SCG"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_LDO3_OC, "LDO3_OC"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_LDO3_UV, "LDO3_UV"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_LDO4_SCG, "LDO4_SCG"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_LDO4_OC, "LDO4_OC"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_LDO4_UV, "LDO4_UV"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_LDO1_SCG, "LDO1_SCG"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_LDO1_OC, "LDO1_OC"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_LDO1_UV, "LDO1_UV"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_LDO2_SCG, "LDO2_SCG"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_LDO2_OC, "LDO2_OC"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_LDO2_UV, "LDO2_UV"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_BUCK3_SCG, "BUCK3_SCG"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_BUCK3_OC, "BUCK3_OC"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_BUCK3_NEG_OC, "BUCK3_NEG_OC"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_BUCK3_UV, "BUCK3_UV"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_BUCK1_SCG, "BUCK1_SCG"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_BUCK1_OC, "BUCK1_OC"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_BUCK1_NEG_OC, "BUCK1_NEG_OC"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_BUCK1_UV, "BUCK1_UV"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_BUCK2_SCG, "BUCK2_SCG"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_BUCK2_OC, "BUCK2_OC"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_BUCK2_NEG_OC, "BUCK2_NEG_OC"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_BUCK2_UV, "BUCK2_UV"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_BUCK1_RV, "BUCK1_RV"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_BUCK2_RV, "BUCK2_RV"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_BUCK3_RV, "BUCK3_RV"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_LDO1_RV, "LDO1_RV"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_LDO2_RV, "LDO2_RV"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_LDO3_RV, "LDO3_RV"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_LDO4_RV, "LDO4_RV"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_BUCK1_RV_SD, "BUCK1_RV_SD"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_BUCK2_RV_SD, "BUCK2_RV_SD"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_BUCK3_RV_SD, "BUCK3_RV_SD"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_LDO1_RV_SD, "LDO1_RV_SD"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_LDO2_RV_SD, "LDO2_RV_SD"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_LDO3_RV_SD, "LDO3_RV_SD"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_LDO4_RV_SD, "LDO4_RV_SD"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_TIMEOUT, "TIMEOUT"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_SENSOR_3_WARM, "SENSOR_3_WARM"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_SENSOR_2_WARM, "SENSOR_2_WARM"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_SENSOR_1_WARM, "SENSOR_1_WARM"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_SENSOR_0_WARM, "SENSOR_0_WARM"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_SENSOR_3_HOT, "SENSOR_3_HOT"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_SENSOR_2_HOT, "SENSOR_2_HOT"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_SENSOR_1_HOT, "SENSOR_1_HOT"),
	DEFINE_RES_IRQ_NAMED(TPS65219_INT_SENSOR_0_HOT, "SENSOR_0_HOT"),
};

static const struct mfd_cell tps65219_cells[] = {
	{
		.name = "tps65219-regulator",
		.resources = tps65219_regulator_resources,
		.num_resources = ARRAY_SIZE(tps65219_regulator_resources),
	},
	{ .name = "tps65219-gpio", },
};

static const struct mfd_cell tps65219_pwrbutton_cell = {
	.name = "tps65219-pwrbutton",
	.resources = tps65219_pwrbutton_resources,
	.num_resources = ARRAY_SIZE(tps65219_pwrbutton_resources),
};

static const struct regmap_config tps65219_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = TPS65219_REG_FACTORY_CONFIG_2,
};

/*
 * Mapping of main IRQ register bits to sub-IRQ register offsets so that we can
 * access corect sub-IRQ registers based on bits that are set in main IRQ
 * register.
 */
/* Timeout Residual Voltage Shutdown */
static unsigned int bit0_offsets[] = { TPS65219_REG_INT_TO_RV_POS };
static unsigned int bit1_offsets[] = { TPS65219_REG_INT_RV_POS };	/* Residual Voltage */
static unsigned int bit2_offsets[] = { TPS65219_REG_INT_SYS_POS };	/* System */
static unsigned int bit3_offsets[] = { TPS65219_REG_INT_BUCK_1_2_POS };	/* Buck 1-2 */
static unsigned int bit4_offsets[] = { TPS65219_REG_INT_BUCK_3_POS };	/* Buck 3 */
static unsigned int bit5_offsets[] = { TPS65219_REG_INT_LDO_1_2_POS };	/* LDO 1-2 */
static unsigned int bit6_offsets[] = { TPS65219_REG_INT_LDO_3_4_POS };	/* LDO 3-4 */
static unsigned int bit7_offsets[] = { TPS65219_REG_INT_PB_POS };	/* Power Button */

static struct regmap_irq_sub_irq_map tps65219_sub_irq_offsets[] = {
	REGMAP_IRQ_MAIN_REG_OFFSET(bit0_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit1_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit2_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit3_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit4_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit5_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit6_offsets),
	REGMAP_IRQ_MAIN_REG_OFFSET(bit7_offsets),
};

#define TPS65219_REGMAP_IRQ_REG(int_name, register_position) \
	REGMAP_IRQ_REG(int_name, register_position, int_name##_MASK)

static const struct regmap_irq tps65219_irqs[] = {
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_LDO3_SCG, TPS65219_REG_INT_LDO_3_4_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_LDO3_OC, TPS65219_REG_INT_LDO_3_4_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_LDO3_UV, TPS65219_REG_INT_LDO_3_4_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_LDO4_SCG, TPS65219_REG_INT_LDO_3_4_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_LDO4_OC, TPS65219_REG_INT_LDO_3_4_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_LDO4_UV, TPS65219_REG_INT_LDO_3_4_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_LDO1_SCG, TPS65219_REG_INT_LDO_1_2_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_LDO1_OC, TPS65219_REG_INT_LDO_1_2_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_LDO1_UV, TPS65219_REG_INT_LDO_1_2_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_LDO2_SCG, TPS65219_REG_INT_LDO_1_2_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_LDO2_OC, TPS65219_REG_INT_LDO_1_2_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_LDO2_UV, TPS65219_REG_INT_LDO_1_2_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_BUCK3_SCG, TPS65219_REG_INT_BUCK_3_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_BUCK3_OC, TPS65219_REG_INT_BUCK_3_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_BUCK3_NEG_OC, TPS65219_REG_INT_BUCK_3_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_BUCK3_UV, TPS65219_REG_INT_BUCK_3_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_BUCK2_SCG, TPS65219_REG_INT_BUCK_1_2_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_BUCK2_OC, TPS65219_REG_INT_BUCK_1_2_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_BUCK2_NEG_OC, TPS65219_REG_INT_BUCK_1_2_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_BUCK2_UV, TPS65219_REG_INT_BUCK_1_2_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_BUCK1_SCG, TPS65219_REG_INT_BUCK_1_2_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_BUCK1_OC, TPS65219_REG_INT_BUCK_1_2_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_BUCK1_NEG_OC, TPS65219_REG_INT_BUCK_1_2_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_BUCK1_UV, TPS65219_REG_INT_BUCK_1_2_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_SENSOR_3_WARM, TPS65219_REG_INT_SYS_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_SENSOR_2_WARM, TPS65219_REG_INT_SYS_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_SENSOR_1_WARM, TPS65219_REG_INT_SYS_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_SENSOR_0_WARM, TPS65219_REG_INT_SYS_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_SENSOR_3_HOT, TPS65219_REG_INT_SYS_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_SENSOR_2_HOT, TPS65219_REG_INT_SYS_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_SENSOR_1_HOT, TPS65219_REG_INT_SYS_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_SENSOR_0_HOT, TPS65219_REG_INT_SYS_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_BUCK1_RV, TPS65219_REG_INT_RV_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_BUCK2_RV, TPS65219_REG_INT_RV_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_BUCK3_RV, TPS65219_REG_INT_RV_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_LDO1_RV, TPS65219_REG_INT_RV_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_LDO2_RV, TPS65219_REG_INT_RV_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_LDO3_RV, TPS65219_REG_INT_RV_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_LDO4_RV, TPS65219_REG_INT_RV_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_BUCK1_RV_SD, TPS65219_REG_INT_TO_RV_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_BUCK2_RV_SD, TPS65219_REG_INT_TO_RV_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_BUCK3_RV_SD, TPS65219_REG_INT_TO_RV_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_LDO1_RV_SD, TPS65219_REG_INT_TO_RV_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_LDO2_RV_SD, TPS65219_REG_INT_TO_RV_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_LDO3_RV_SD, TPS65219_REG_INT_TO_RV_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_LDO4_RV_SD, TPS65219_REG_INT_TO_RV_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_TIMEOUT, TPS65219_REG_INT_TO_RV_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_PB_FALLING_EDGE_DETECT, TPS65219_REG_INT_PB_POS),
	TPS65219_REGMAP_IRQ_REG(TPS65219_INT_PB_RISING_EDGE_DETECT, TPS65219_REG_INT_PB_POS),
};

static const struct regmap_irq_chip tps65219_irq_chip = {
	.name = "tps65219_irq",
	.main_status = TPS65219_REG_INT_SOURCE,
	.num_main_regs = 1,
	.num_main_status_bits = 8,
	.irqs = tps65219_irqs,
	.num_irqs = ARRAY_SIZE(tps65219_irqs),
	.status_base = TPS65219_REG_INT_LDO_3_4,
	.ack_base = TPS65219_REG_INT_LDO_3_4,
	.clear_ack = 1,
	.num_regs = 8,
	.sub_reg_offsets = tps65219_sub_irq_offsets,
};

static int tps65219_probe(struct i2c_client *client)
{
	struct tps65219 *tps;
	unsigned int chipid;
	bool pwr_button;
	int ret;

	tps = devm_kzalloc(&client->dev, sizeof(*tps), GFP_KERNEL);
	if (!tps)
		return -ENOMEM;

	i2c_set_clientdata(client, tps);

	tps->dev = &client->dev;

	tps->regmap = devm_regmap_init_i2c(client, &tps65219_regmap_config);
	if (IS_ERR(tps->regmap)) {
		ret = PTR_ERR(tps->regmap);
		dev_err(tps->dev, "Failed to allocate register map: %d\n", ret);
		return ret;
	}

	ret = devm_regmap_add_irq_chip(&client->dev, tps->regmap, client->irq,
				       IRQF_ONESHOT, 0, &tps65219_irq_chip,
				       &tps->irq_data);
	if (ret)
		return ret;

	ret = regmap_read(tps->regmap, TPS65219_REG_TI_DEV_ID, &chipid);
	if (ret) {
		dev_err(tps->dev, "Failed to read device ID: %d\n", ret);
		return ret;
	}

	ret = devm_mfd_add_devices(tps->dev, PLATFORM_DEVID_AUTO,
				   tps65219_cells, ARRAY_SIZE(tps65219_cells),
				   NULL, 0, regmap_irq_get_domain(tps->irq_data));
	if (ret) {
		dev_err(tps->dev, "Failed to add child devices: %d\n", ret);
		return ret;
	}

	pwr_button = of_property_read_bool(tps->dev->of_node, "ti,power-button");
	if (pwr_button) {
		ret = devm_mfd_add_devices(tps->dev, PLATFORM_DEVID_AUTO,
					   &tps65219_pwrbutton_cell, 1, NULL, 0,
					   regmap_irq_get_domain(tps->irq_data));
		if (ret) {
			dev_err(tps->dev, "Failed to add power-button: %d\n", ret);
			return ret;
		}
	}

	ret = devm_register_restart_handler(tps->dev,
					    tps65219_restart_handler,
					    tps);

	if (ret) {
		dev_err(tps->dev, "cannot register restart handler, %d\n", ret);
		return ret;
	}

	ret = devm_register_power_off_handler(tps->dev,
					      tps65219_power_off_handler,
					      tps);
	if (ret) {
		dev_err(tps->dev, "failed to register power-off handler: %d\n", ret);
		return ret;
	}
	return 0;
}

static const struct of_device_id of_tps65219_match_table[] = {
	{ .compatible = "ti,tps65219", },
	{}
};
MODULE_DEVICE_TABLE(of, of_tps65219_match_table);

static struct i2c_driver tps65219_driver = {
	.driver		= {
		.name	= "tps65219",
		.of_match_table = of_tps65219_match_table,
	},
	.probe		= tps65219_probe,
};
module_i2c_driver(tps65219_driver);

MODULE_AUTHOR("Jerome Neanne <jneanne@baylibre.com>");
MODULE_DESCRIPTION("TPS65219 power management IC driver");
MODULE_LICENSE("GPL");
