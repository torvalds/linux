// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Core, IRQ and I2C device driver for DA9061 and DA9062 PMICs
 * Copyright (C) 2015-2017  Dialog Semiconductor
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/irq.h>
#include <linux/mfd/core.h>
#include <linux/i2c.h>
#include <linux/mfd/da9062/core.h>
#include <linux/mfd/da9062/registers.h>
#include <linux/regulator/of_regulator.h>

#define	DA9062_REG_EVENT_A_OFFSET	0
#define	DA9062_REG_EVENT_B_OFFSET	1
#define	DA9062_REG_EVENT_C_OFFSET	2

#define	DA9062_IRQ_LOW	0
#define	DA9062_IRQ_HIGH	1

static struct regmap_irq da9061_irqs[] = {
	/* EVENT A */
	[DA9061_IRQ_ONKEY] = {
		.reg_offset = DA9062_REG_EVENT_A_OFFSET,
		.mask = DA9062AA_M_NONKEY_MASK,
	},
	[DA9061_IRQ_WDG_WARN] = {
		.reg_offset = DA9062_REG_EVENT_A_OFFSET,
		.mask = DA9062AA_M_WDG_WARN_MASK,
	},
	[DA9061_IRQ_SEQ_RDY] = {
		.reg_offset = DA9062_REG_EVENT_A_OFFSET,
		.mask = DA9062AA_M_SEQ_RDY_MASK,
	},
	/* EVENT B */
	[DA9061_IRQ_TEMP] = {
		.reg_offset = DA9062_REG_EVENT_B_OFFSET,
		.mask = DA9062AA_M_TEMP_MASK,
	},
	[DA9061_IRQ_LDO_LIM] = {
		.reg_offset = DA9062_REG_EVENT_B_OFFSET,
		.mask = DA9062AA_M_LDO_LIM_MASK,
	},
	[DA9061_IRQ_DVC_RDY] = {
		.reg_offset = DA9062_REG_EVENT_B_OFFSET,
		.mask = DA9062AA_M_DVC_RDY_MASK,
	},
	[DA9061_IRQ_VDD_WARN] = {
		.reg_offset = DA9062_REG_EVENT_B_OFFSET,
		.mask = DA9062AA_M_VDD_WARN_MASK,
	},
	/* EVENT C */
	[DA9061_IRQ_GPI0] = {
		.reg_offset = DA9062_REG_EVENT_C_OFFSET,
		.mask = DA9062AA_M_GPI0_MASK,
	},
	[DA9061_IRQ_GPI1] = {
		.reg_offset = DA9062_REG_EVENT_C_OFFSET,
		.mask = DA9062AA_M_GPI1_MASK,
	},
	[DA9061_IRQ_GPI2] = {
		.reg_offset = DA9062_REG_EVENT_C_OFFSET,
		.mask = DA9062AA_M_GPI2_MASK,
	},
	[DA9061_IRQ_GPI3] = {
		.reg_offset = DA9062_REG_EVENT_C_OFFSET,
		.mask = DA9062AA_M_GPI3_MASK,
	},
	[DA9061_IRQ_GPI4] = {
		.reg_offset = DA9062_REG_EVENT_C_OFFSET,
		.mask = DA9062AA_M_GPI4_MASK,
	},
};

static struct regmap_irq_chip da9061_irq_chip = {
	.name = "da9061-irq",
	.irqs = da9061_irqs,
	.num_irqs = DA9061_NUM_IRQ,
	.num_regs = 3,
	.status_base = DA9062AA_EVENT_A,
	.mask_base = DA9062AA_IRQ_MASK_A,
	.ack_base = DA9062AA_EVENT_A,
};

static struct regmap_irq da9062_irqs[] = {
	/* EVENT A */
	[DA9062_IRQ_ONKEY] = {
		.reg_offset = DA9062_REG_EVENT_A_OFFSET,
		.mask = DA9062AA_M_NONKEY_MASK,
	},
	[DA9062_IRQ_ALARM] = {
		.reg_offset = DA9062_REG_EVENT_A_OFFSET,
		.mask = DA9062AA_M_ALARM_MASK,
	},
	[DA9062_IRQ_TICK] = {
		.reg_offset = DA9062_REG_EVENT_A_OFFSET,
		.mask = DA9062AA_M_TICK_MASK,
	},
	[DA9062_IRQ_WDG_WARN] = {
		.reg_offset = DA9062_REG_EVENT_A_OFFSET,
		.mask = DA9062AA_M_WDG_WARN_MASK,
	},
	[DA9062_IRQ_SEQ_RDY] = {
		.reg_offset = DA9062_REG_EVENT_A_OFFSET,
		.mask = DA9062AA_M_SEQ_RDY_MASK,
	},
	/* EVENT B */
	[DA9062_IRQ_TEMP] = {
		.reg_offset = DA9062_REG_EVENT_B_OFFSET,
		.mask = DA9062AA_M_TEMP_MASK,
	},
	[DA9062_IRQ_LDO_LIM] = {
		.reg_offset = DA9062_REG_EVENT_B_OFFSET,
		.mask = DA9062AA_M_LDO_LIM_MASK,
	},
	[DA9062_IRQ_DVC_RDY] = {
		.reg_offset = DA9062_REG_EVENT_B_OFFSET,
		.mask = DA9062AA_M_DVC_RDY_MASK,
	},
	[DA9062_IRQ_VDD_WARN] = {
		.reg_offset = DA9062_REG_EVENT_B_OFFSET,
		.mask = DA9062AA_M_VDD_WARN_MASK,
	},
	/* EVENT C */
	[DA9062_IRQ_GPI0] = {
		.reg_offset = DA9062_REG_EVENT_C_OFFSET,
		.mask = DA9062AA_M_GPI0_MASK,
	},
	[DA9062_IRQ_GPI1] = {
		.reg_offset = DA9062_REG_EVENT_C_OFFSET,
		.mask = DA9062AA_M_GPI1_MASK,
	},
	[DA9062_IRQ_GPI2] = {
		.reg_offset = DA9062_REG_EVENT_C_OFFSET,
		.mask = DA9062AA_M_GPI2_MASK,
	},
	[DA9062_IRQ_GPI3] = {
		.reg_offset = DA9062_REG_EVENT_C_OFFSET,
		.mask = DA9062AA_M_GPI3_MASK,
	},
	[DA9062_IRQ_GPI4] = {
		.reg_offset = DA9062_REG_EVENT_C_OFFSET,
		.mask = DA9062AA_M_GPI4_MASK,
	},
};

static struct regmap_irq_chip da9062_irq_chip = {
	.name = "da9062-irq",
	.irqs = da9062_irqs,
	.num_irqs = DA9062_NUM_IRQ,
	.num_regs = 3,
	.status_base = DA9062AA_EVENT_A,
	.mask_base = DA9062AA_IRQ_MASK_A,
	.ack_base = DA9062AA_EVENT_A,
};

static struct resource da9061_core_resources[] = {
	DEFINE_RES_IRQ_NAMED(DA9061_IRQ_VDD_WARN, "VDD_WARN"),
};

static struct resource da9061_regulators_resources[] = {
	DEFINE_RES_IRQ_NAMED(DA9061_IRQ_LDO_LIM, "LDO_LIM"),
};

static struct resource da9061_thermal_resources[] = {
	DEFINE_RES_IRQ_NAMED(DA9061_IRQ_TEMP, "THERMAL"),
};

static struct resource da9061_wdt_resources[] = {
	DEFINE_RES_IRQ_NAMED(DA9061_IRQ_WDG_WARN, "WD_WARN"),
};

static struct resource da9061_onkey_resources[] = {
	DEFINE_RES_IRQ_NAMED(DA9061_IRQ_ONKEY, "ONKEY"),
};

static const struct mfd_cell da9061_devs[] = {
	{
		.name		= "da9061-core",
		.num_resources	= ARRAY_SIZE(da9061_core_resources),
		.resources	= da9061_core_resources,
	},
	{
		.name		= "da9062-regulators",
		.num_resources	= ARRAY_SIZE(da9061_regulators_resources),
		.resources	= da9061_regulators_resources,
	},
	{
		.name		= "da9061-watchdog",
		.num_resources	= ARRAY_SIZE(da9061_wdt_resources),
		.resources	= da9061_wdt_resources,
		.of_compatible  = "dlg,da9061-watchdog",
	},
	{
		.name		= "da9061-thermal",
		.num_resources	= ARRAY_SIZE(da9061_thermal_resources),
		.resources	= da9061_thermal_resources,
		.of_compatible  = "dlg,da9061-thermal",
	},
	{
		.name		= "da9061-onkey",
		.num_resources	= ARRAY_SIZE(da9061_onkey_resources),
		.resources	= da9061_onkey_resources,
		.of_compatible = "dlg,da9061-onkey",
	},
};

static struct resource da9062_core_resources[] = {
	DEFINE_RES_NAMED(DA9062_IRQ_VDD_WARN, 1, "VDD_WARN", IORESOURCE_IRQ),
};

static struct resource da9062_regulators_resources[] = {
	DEFINE_RES_NAMED(DA9062_IRQ_LDO_LIM, 1, "LDO_LIM", IORESOURCE_IRQ),
};

static struct resource da9062_thermal_resources[] = {
	DEFINE_RES_NAMED(DA9062_IRQ_TEMP, 1, "THERMAL", IORESOURCE_IRQ),
};

static struct resource da9062_wdt_resources[] = {
	DEFINE_RES_NAMED(DA9062_IRQ_WDG_WARN, 1, "WD_WARN", IORESOURCE_IRQ),
};

static struct resource da9062_rtc_resources[] = {
	DEFINE_RES_NAMED(DA9062_IRQ_ALARM, 1, "ALARM", IORESOURCE_IRQ),
	DEFINE_RES_NAMED(DA9062_IRQ_TICK, 1, "TICK", IORESOURCE_IRQ),
};

static struct resource da9062_onkey_resources[] = {
	DEFINE_RES_NAMED(DA9062_IRQ_ONKEY, 1, "ONKEY", IORESOURCE_IRQ),
};

static struct resource da9062_gpio_resources[] = {
	DEFINE_RES_NAMED(DA9062_IRQ_GPI0, 1, "GPI0", IORESOURCE_IRQ),
	DEFINE_RES_NAMED(DA9062_IRQ_GPI1, 1, "GPI1", IORESOURCE_IRQ),
	DEFINE_RES_NAMED(DA9062_IRQ_GPI2, 1, "GPI2", IORESOURCE_IRQ),
	DEFINE_RES_NAMED(DA9062_IRQ_GPI3, 1, "GPI3", IORESOURCE_IRQ),
	DEFINE_RES_NAMED(DA9062_IRQ_GPI4, 1, "GPI4", IORESOURCE_IRQ),
};

static const struct mfd_cell da9062_devs[] = {
	{
		.name		= "da9062-core",
		.num_resources	= ARRAY_SIZE(da9062_core_resources),
		.resources	= da9062_core_resources,
	},
	{
		.name		= "da9062-regulators",
		.num_resources	= ARRAY_SIZE(da9062_regulators_resources),
		.resources	= da9062_regulators_resources,
	},
	{
		.name		= "da9062-watchdog",
		.num_resources	= ARRAY_SIZE(da9062_wdt_resources),
		.resources	= da9062_wdt_resources,
		.of_compatible  = "dlg,da9062-watchdog",
	},
	{
		.name		= "da9062-thermal",
		.num_resources	= ARRAY_SIZE(da9062_thermal_resources),
		.resources	= da9062_thermal_resources,
		.of_compatible  = "dlg,da9062-thermal",
	},
	{
		.name		= "da9062-rtc",
		.num_resources	= ARRAY_SIZE(da9062_rtc_resources),
		.resources	= da9062_rtc_resources,
		.of_compatible  = "dlg,da9062-rtc",
	},
	{
		.name		= "da9062-onkey",
		.num_resources	= ARRAY_SIZE(da9062_onkey_resources),
		.resources	= da9062_onkey_resources,
		.of_compatible	= "dlg,da9062-onkey",
	},
	{
		.name		= "da9062-gpio",
		.num_resources	= ARRAY_SIZE(da9062_gpio_resources),
		.resources	= da9062_gpio_resources,
		.of_compatible	= "dlg,da9062-gpio",
	},
};

static int da9062_clear_fault_log(struct da9062 *chip)
{
	int ret;
	int fault_log;

	ret = regmap_read(chip->regmap, DA9062AA_FAULT_LOG, &fault_log);
	if (ret < 0)
		return ret;

	if (fault_log) {
		if (fault_log & DA9062AA_TWD_ERROR_MASK)
			dev_dbg(chip->dev, "Fault log entry detected: TWD_ERROR\n");
		if (fault_log & DA9062AA_POR_MASK)
			dev_dbg(chip->dev, "Fault log entry detected: POR\n");
		if (fault_log & DA9062AA_VDD_FAULT_MASK)
			dev_dbg(chip->dev, "Fault log entry detected: VDD_FAULT\n");
		if (fault_log & DA9062AA_VDD_START_MASK)
			dev_dbg(chip->dev, "Fault log entry detected: VDD_START\n");
		if (fault_log & DA9062AA_TEMP_CRIT_MASK)
			dev_dbg(chip->dev, "Fault log entry detected: TEMP_CRIT\n");
		if (fault_log & DA9062AA_KEY_RESET_MASK)
			dev_dbg(chip->dev, "Fault log entry detected: KEY_RESET\n");
		if (fault_log & DA9062AA_NSHUTDOWN_MASK)
			dev_dbg(chip->dev, "Fault log entry detected: NSHUTDOWN\n");
		if (fault_log & DA9062AA_WAIT_SHUT_MASK)
			dev_dbg(chip->dev, "Fault log entry detected: WAIT_SHUT\n");

		ret = regmap_write(chip->regmap, DA9062AA_FAULT_LOG,
				   fault_log);
	}

	return ret;
}

static int da9062_get_device_type(struct da9062 *chip)
{
	int device_id, variant_id, variant_mrc, variant_vrc;
	char *type;
	int ret;

	ret = regmap_read(chip->regmap, DA9062AA_DEVICE_ID, &device_id);
	if (ret < 0) {
		dev_err(chip->dev, "Cannot read chip ID.\n");
		return -EIO;
	}
	if (device_id != DA9062_PMIC_DEVICE_ID) {
		dev_err(chip->dev, "Invalid device ID: 0x%02x\n", device_id);
		return -ENODEV;
	}

	ret = regmap_read(chip->regmap, DA9062AA_VARIANT_ID, &variant_id);
	if (ret < 0) {
		dev_err(chip->dev, "Cannot read chip variant id.\n");
		return -EIO;
	}

	variant_vrc = (variant_id & DA9062AA_VRC_MASK) >> DA9062AA_VRC_SHIFT;

	switch (variant_vrc) {
	case DA9062_PMIC_VARIANT_VRC_DA9061:
		type = "DA9061";
		break;
	case DA9062_PMIC_VARIANT_VRC_DA9062:
		type = "DA9062";
		break;
	default:
		type = "Unknown";
		break;
	}

	dev_info(chip->dev,
		 "Device detected (device-ID: 0x%02X, var-ID: 0x%02X, %s)\n",
		 device_id, variant_id, type);

	variant_mrc = (variant_id & DA9062AA_MRC_MASK) >> DA9062AA_MRC_SHIFT;

	if (variant_mrc < DA9062_PMIC_VARIANT_MRC_AA) {
		dev_err(chip->dev,
			"Cannot support variant MRC: 0x%02X\n", variant_mrc);
		return -ENODEV;
	}

	return ret;
}

static u32 da9062_configure_irq_type(struct da9062 *chip, int irq, u32 *trigger)
{
	u32 irq_type = 0;
	struct irq_data *irq_data = irq_get_irq_data(irq);

	if (!irq_data) {
		dev_err(chip->dev, "Invalid IRQ: %d\n", irq);
		return -EINVAL;
	}
	*trigger = irqd_get_trigger_type(irq_data);

	switch (*trigger) {
	case IRQ_TYPE_LEVEL_HIGH:
		irq_type = DA9062_IRQ_HIGH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		irq_type = DA9062_IRQ_LOW;
		break;
	default:
		dev_warn(chip->dev, "Unsupported IRQ type: %d\n", *trigger);
		return -EINVAL;
	}
	return regmap_update_bits(chip->regmap, DA9062AA_CONFIG_A,
			DA9062AA_IRQ_TYPE_MASK,
			irq_type << DA9062AA_IRQ_TYPE_SHIFT);
}

static const struct regmap_range da9061_aa_readable_ranges[] = {
	regmap_reg_range(DA9062AA_PAGE_CON, DA9062AA_STATUS_B),
	regmap_reg_range(DA9062AA_STATUS_D, DA9062AA_EVENT_C),
	regmap_reg_range(DA9062AA_IRQ_MASK_A, DA9062AA_IRQ_MASK_C),
	regmap_reg_range(DA9062AA_CONTROL_A, DA9062AA_GPIO_4),
	regmap_reg_range(DA9062AA_GPIO_WKUP_MODE, DA9062AA_GPIO_OUT3_4),
	regmap_reg_range(DA9062AA_BUCK1_CONT, DA9062AA_BUCK4_CONT),
	regmap_reg_range(DA9062AA_BUCK3_CONT, DA9062AA_BUCK3_CONT),
	regmap_reg_range(DA9062AA_LDO1_CONT, DA9062AA_LDO4_CONT),
	regmap_reg_range(DA9062AA_DVC_1, DA9062AA_DVC_1),
	regmap_reg_range(DA9062AA_SEQ, DA9062AA_ID_4_3),
	regmap_reg_range(DA9062AA_ID_12_11, DA9062AA_ID_16_15),
	regmap_reg_range(DA9062AA_ID_22_21, DA9062AA_ID_32_31),
	regmap_reg_range(DA9062AA_SEQ_A, DA9062AA_WAIT),
	regmap_reg_range(DA9062AA_RESET, DA9062AA_BUCK_ILIM_C),
	regmap_reg_range(DA9062AA_BUCK1_CFG, DA9062AA_BUCK3_CFG),
	regmap_reg_range(DA9062AA_VBUCK1_A, DA9062AA_VBUCK4_A),
	regmap_reg_range(DA9062AA_VBUCK3_A, DA9062AA_VBUCK3_A),
	regmap_reg_range(DA9062AA_VLDO1_A, DA9062AA_VLDO4_A),
	regmap_reg_range(DA9062AA_CONFIG_A, DA9062AA_CONFIG_A),
	regmap_reg_range(DA9062AA_VBUCK1_B, DA9062AA_VBUCK4_B),
	regmap_reg_range(DA9062AA_VBUCK3_B, DA9062AA_VBUCK3_B),
	regmap_reg_range(DA9062AA_VLDO1_B, DA9062AA_VLDO4_B),
	regmap_reg_range(DA9062AA_INTERFACE, DA9062AA_CONFIG_E),
	regmap_reg_range(DA9062AA_CONFIG_G, DA9062AA_CONFIG_K),
	regmap_reg_range(DA9062AA_CONFIG_M, DA9062AA_CONFIG_M),
	regmap_reg_range(DA9062AA_GP_ID_0, DA9062AA_GP_ID_19),
	regmap_reg_range(DA9062AA_DEVICE_ID, DA9062AA_CONFIG_ID),
};

static const struct regmap_range da9061_aa_writeable_ranges[] = {
	regmap_reg_range(DA9062AA_PAGE_CON, DA9062AA_PAGE_CON),
	regmap_reg_range(DA9062AA_FAULT_LOG, DA9062AA_EVENT_C),
	regmap_reg_range(DA9062AA_IRQ_MASK_A, DA9062AA_IRQ_MASK_C),
	regmap_reg_range(DA9062AA_CONTROL_A, DA9062AA_GPIO_4),
	regmap_reg_range(DA9062AA_GPIO_WKUP_MODE, DA9062AA_GPIO_OUT3_4),
	regmap_reg_range(DA9062AA_BUCK1_CONT, DA9062AA_BUCK4_CONT),
	regmap_reg_range(DA9062AA_BUCK3_CONT, DA9062AA_BUCK3_CONT),
	regmap_reg_range(DA9062AA_LDO1_CONT, DA9062AA_LDO4_CONT),
	regmap_reg_range(DA9062AA_DVC_1, DA9062AA_DVC_1),
	regmap_reg_range(DA9062AA_SEQ, DA9062AA_ID_4_3),
	regmap_reg_range(DA9062AA_ID_12_11, DA9062AA_ID_16_15),
	regmap_reg_range(DA9062AA_ID_22_21, DA9062AA_ID_32_31),
	regmap_reg_range(DA9062AA_SEQ_A, DA9062AA_WAIT),
	regmap_reg_range(DA9062AA_RESET, DA9062AA_BUCK_ILIM_C),
	regmap_reg_range(DA9062AA_BUCK1_CFG, DA9062AA_BUCK3_CFG),
	regmap_reg_range(DA9062AA_VBUCK1_A, DA9062AA_VBUCK4_A),
	regmap_reg_range(DA9062AA_VBUCK3_A, DA9062AA_VBUCK3_A),
	regmap_reg_range(DA9062AA_VLDO1_A, DA9062AA_VLDO4_A),
	regmap_reg_range(DA9062AA_CONFIG_A, DA9062AA_CONFIG_A),
	regmap_reg_range(DA9062AA_VBUCK1_B, DA9062AA_VBUCK4_B),
	regmap_reg_range(DA9062AA_VBUCK3_B, DA9062AA_VBUCK3_B),
	regmap_reg_range(DA9062AA_VLDO1_B, DA9062AA_VLDO4_B),
	regmap_reg_range(DA9062AA_GP_ID_0, DA9062AA_GP_ID_19),
};

static const struct regmap_range da9061_aa_volatile_ranges[] = {
	regmap_reg_range(DA9062AA_PAGE_CON, DA9062AA_STATUS_B),
	regmap_reg_range(DA9062AA_STATUS_D, DA9062AA_EVENT_C),
	regmap_reg_range(DA9062AA_CONTROL_A, DA9062AA_CONTROL_B),
	regmap_reg_range(DA9062AA_CONTROL_E, DA9062AA_CONTROL_F),
	regmap_reg_range(DA9062AA_BUCK1_CONT, DA9062AA_BUCK4_CONT),
	regmap_reg_range(DA9062AA_BUCK3_CONT, DA9062AA_BUCK3_CONT),
	regmap_reg_range(DA9062AA_LDO1_CONT, DA9062AA_LDO4_CONT),
	regmap_reg_range(DA9062AA_DVC_1, DA9062AA_DVC_1),
	regmap_reg_range(DA9062AA_SEQ, DA9062AA_SEQ),
};

static const struct regmap_access_table da9061_aa_readable_table = {
	.yes_ranges = da9061_aa_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9061_aa_readable_ranges),
};

static const struct regmap_access_table da9061_aa_writeable_table = {
	.yes_ranges = da9061_aa_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9061_aa_writeable_ranges),
};

static const struct regmap_access_table da9061_aa_volatile_table = {
	.yes_ranges = da9061_aa_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9061_aa_volatile_ranges),
};

static const struct regmap_range_cfg da9061_range_cfg[] = {
	{
		.range_min = DA9062AA_PAGE_CON,
		.range_max = DA9062AA_CONFIG_ID,
		.selector_reg = DA9062AA_PAGE_CON,
		.selector_mask = 1 << DA9062_I2C_PAGE_SEL_SHIFT,
		.selector_shift = DA9062_I2C_PAGE_SEL_SHIFT,
		.window_start = 0,
		.window_len = 256,
	}
};

static struct regmap_config da9061_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.ranges = da9061_range_cfg,
	.num_ranges = ARRAY_SIZE(da9061_range_cfg),
	.max_register = DA9062AA_CONFIG_ID,
	.cache_type = REGCACHE_RBTREE,
	.rd_table = &da9061_aa_readable_table,
	.wr_table = &da9061_aa_writeable_table,
	.volatile_table = &da9061_aa_volatile_table,
};

static const struct regmap_range da9062_aa_readable_ranges[] = {
	regmap_reg_range(DA9062AA_PAGE_CON, DA9062AA_STATUS_B),
	regmap_reg_range(DA9062AA_STATUS_D, DA9062AA_EVENT_C),
	regmap_reg_range(DA9062AA_IRQ_MASK_A, DA9062AA_IRQ_MASK_C),
	regmap_reg_range(DA9062AA_CONTROL_A, DA9062AA_GPIO_4),
	regmap_reg_range(DA9062AA_GPIO_WKUP_MODE, DA9062AA_BUCK4_CONT),
	regmap_reg_range(DA9062AA_BUCK3_CONT, DA9062AA_BUCK3_CONT),
	regmap_reg_range(DA9062AA_LDO1_CONT, DA9062AA_LDO4_CONT),
	regmap_reg_range(DA9062AA_DVC_1, DA9062AA_DVC_1),
	regmap_reg_range(DA9062AA_COUNT_S, DA9062AA_SECOND_D),
	regmap_reg_range(DA9062AA_SEQ, DA9062AA_ID_4_3),
	regmap_reg_range(DA9062AA_ID_12_11, DA9062AA_ID_16_15),
	regmap_reg_range(DA9062AA_ID_22_21, DA9062AA_ID_32_31),
	regmap_reg_range(DA9062AA_SEQ_A, DA9062AA_BUCK3_CFG),
	regmap_reg_range(DA9062AA_VBUCK2_A, DA9062AA_VBUCK4_A),
	regmap_reg_range(DA9062AA_VBUCK3_A, DA9062AA_VBUCK3_A),
	regmap_reg_range(DA9062AA_VLDO1_A, DA9062AA_VLDO4_A),
	regmap_reg_range(DA9062AA_VBUCK2_B, DA9062AA_VBUCK4_B),
	regmap_reg_range(DA9062AA_VBUCK3_B, DA9062AA_VBUCK3_B),
	regmap_reg_range(DA9062AA_VLDO1_B, DA9062AA_VLDO4_B),
	regmap_reg_range(DA9062AA_BBAT_CONT, DA9062AA_BBAT_CONT),
	regmap_reg_range(DA9062AA_INTERFACE, DA9062AA_CONFIG_E),
	regmap_reg_range(DA9062AA_CONFIG_G, DA9062AA_CONFIG_K),
	regmap_reg_range(DA9062AA_CONFIG_M, DA9062AA_CONFIG_M),
	regmap_reg_range(DA9062AA_TRIM_CLDR, DA9062AA_GP_ID_19),
	regmap_reg_range(DA9062AA_DEVICE_ID, DA9062AA_CONFIG_ID),
};

static const struct regmap_range da9062_aa_writeable_ranges[] = {
	regmap_reg_range(DA9062AA_PAGE_CON, DA9062AA_PAGE_CON),
	regmap_reg_range(DA9062AA_FAULT_LOG, DA9062AA_EVENT_C),
	regmap_reg_range(DA9062AA_IRQ_MASK_A, DA9062AA_IRQ_MASK_C),
	regmap_reg_range(DA9062AA_CONTROL_A, DA9062AA_GPIO_4),
	regmap_reg_range(DA9062AA_GPIO_WKUP_MODE, DA9062AA_BUCK4_CONT),
	regmap_reg_range(DA9062AA_BUCK3_CONT, DA9062AA_BUCK3_CONT),
	regmap_reg_range(DA9062AA_LDO1_CONT, DA9062AA_LDO4_CONT),
	regmap_reg_range(DA9062AA_DVC_1, DA9062AA_DVC_1),
	regmap_reg_range(DA9062AA_COUNT_S, DA9062AA_ALARM_Y),
	regmap_reg_range(DA9062AA_SEQ, DA9062AA_ID_4_3),
	regmap_reg_range(DA9062AA_ID_12_11, DA9062AA_ID_16_15),
	regmap_reg_range(DA9062AA_ID_22_21, DA9062AA_ID_32_31),
	regmap_reg_range(DA9062AA_SEQ_A, DA9062AA_BUCK3_CFG),
	regmap_reg_range(DA9062AA_VBUCK2_A, DA9062AA_VBUCK4_A),
	regmap_reg_range(DA9062AA_VBUCK3_A, DA9062AA_VBUCK3_A),
	regmap_reg_range(DA9062AA_VLDO1_A, DA9062AA_VLDO4_A),
	regmap_reg_range(DA9062AA_VBUCK2_B, DA9062AA_VBUCK4_B),
	regmap_reg_range(DA9062AA_VBUCK3_B, DA9062AA_VBUCK3_B),
	regmap_reg_range(DA9062AA_VLDO1_B, DA9062AA_VLDO4_B),
	regmap_reg_range(DA9062AA_BBAT_CONT, DA9062AA_BBAT_CONT),
	regmap_reg_range(DA9062AA_GP_ID_0, DA9062AA_GP_ID_19),
};

static const struct regmap_range da9062_aa_volatile_ranges[] = {
	regmap_reg_range(DA9062AA_PAGE_CON, DA9062AA_STATUS_B),
	regmap_reg_range(DA9062AA_STATUS_D, DA9062AA_EVENT_C),
	regmap_reg_range(DA9062AA_CONTROL_A, DA9062AA_CONTROL_B),
	regmap_reg_range(DA9062AA_CONTROL_E, DA9062AA_CONTROL_F),
	regmap_reg_range(DA9062AA_BUCK2_CONT, DA9062AA_BUCK4_CONT),
	regmap_reg_range(DA9062AA_BUCK3_CONT, DA9062AA_BUCK3_CONT),
	regmap_reg_range(DA9062AA_LDO1_CONT, DA9062AA_LDO4_CONT),
	regmap_reg_range(DA9062AA_DVC_1, DA9062AA_DVC_1),
	regmap_reg_range(DA9062AA_COUNT_S, DA9062AA_SECOND_D),
	regmap_reg_range(DA9062AA_SEQ, DA9062AA_SEQ),
	regmap_reg_range(DA9062AA_EN_32K, DA9062AA_EN_32K),
};

static const struct regmap_access_table da9062_aa_readable_table = {
	.yes_ranges = da9062_aa_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9062_aa_readable_ranges),
};

static const struct regmap_access_table da9062_aa_writeable_table = {
	.yes_ranges = da9062_aa_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9062_aa_writeable_ranges),
};

static const struct regmap_access_table da9062_aa_volatile_table = {
	.yes_ranges = da9062_aa_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9062_aa_volatile_ranges),
};

static const struct regmap_range_cfg da9062_range_cfg[] = {
	{
		.range_min = DA9062AA_PAGE_CON,
		.range_max = DA9062AA_CONFIG_ID,
		.selector_reg = DA9062AA_PAGE_CON,
		.selector_mask = 1 << DA9062_I2C_PAGE_SEL_SHIFT,
		.selector_shift = DA9062_I2C_PAGE_SEL_SHIFT,
		.window_start = 0,
		.window_len = 256,
	}
};

static struct regmap_config da9062_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.ranges = da9062_range_cfg,
	.num_ranges = ARRAY_SIZE(da9062_range_cfg),
	.max_register = DA9062AA_CONFIG_ID,
	.cache_type = REGCACHE_RBTREE,
	.rd_table = &da9062_aa_readable_table,
	.wr_table = &da9062_aa_writeable_table,
	.volatile_table = &da9062_aa_volatile_table,
};

static const struct of_device_id da9062_dt_ids[] = {
	{ .compatible = "dlg,da9061", .data = (void *)COMPAT_TYPE_DA9061, },
	{ .compatible = "dlg,da9062", .data = (void *)COMPAT_TYPE_DA9062, },
	{ }
};
MODULE_DEVICE_TABLE(of, da9062_dt_ids);

static int da9062_i2c_probe(struct i2c_client *i2c,
	const struct i2c_device_id *id)
{
	struct da9062 *chip;
	const struct of_device_id *match;
	unsigned int irq_base;
	const struct mfd_cell *cell;
	const struct regmap_irq_chip *irq_chip;
	const struct regmap_config *config;
	int cell_num;
	u32 trigger_type = 0;
	int ret;

	chip = devm_kzalloc(&i2c->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	if (i2c->dev.of_node) {
		match = of_match_node(da9062_dt_ids, i2c->dev.of_node);
		if (!match)
			return -EINVAL;

		chip->chip_type = (uintptr_t)match->data;
	} else {
		chip->chip_type = id->driver_data;
	}

	i2c_set_clientdata(i2c, chip);
	chip->dev = &i2c->dev;

	if (!i2c->irq) {
		dev_err(chip->dev, "No IRQ configured\n");
		return -EINVAL;
	}

	switch (chip->chip_type) {
	case COMPAT_TYPE_DA9061:
		cell = da9061_devs;
		cell_num = ARRAY_SIZE(da9061_devs);
		irq_chip = &da9061_irq_chip;
		config = &da9061_regmap_config;
		break;
	case COMPAT_TYPE_DA9062:
		cell = da9062_devs;
		cell_num = ARRAY_SIZE(da9062_devs);
		irq_chip = &da9062_irq_chip;
		config = &da9062_regmap_config;
		break;
	default:
		dev_err(chip->dev, "Unrecognised chip type\n");
		return -ENODEV;
	}

	chip->regmap = devm_regmap_init_i2c(i2c, config);
	if (IS_ERR(chip->regmap)) {
		ret = PTR_ERR(chip->regmap);
		dev_err(chip->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	ret = da9062_clear_fault_log(chip);
	if (ret < 0)
		dev_warn(chip->dev, "Cannot clear fault log\n");

	ret = da9062_get_device_type(chip);
	if (ret)
		return ret;

	ret = da9062_configure_irq_type(chip, i2c->irq, &trigger_type);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to configure IRQ type\n");
		return ret;
	}

	ret = regmap_add_irq_chip(chip->regmap, i2c->irq,
			trigger_type | IRQF_SHARED | IRQF_ONESHOT,
			-1, irq_chip, &chip->regmap_irq);
	if (ret) {
		dev_err(chip->dev, "Failed to request IRQ %d: %d\n",
			i2c->irq, ret);
		return ret;
	}

	irq_base = regmap_irq_chip_get_base(chip->regmap_irq);

	ret = mfd_add_devices(chip->dev, PLATFORM_DEVID_NONE, cell,
			      cell_num, NULL, irq_base,
			      NULL);
	if (ret) {
		dev_err(chip->dev, "Cannot register child devices\n");
		regmap_del_irq_chip(i2c->irq, chip->regmap_irq);
		return ret;
	}

	return ret;
}

static int da9062_i2c_remove(struct i2c_client *i2c)
{
	struct da9062 *chip = i2c_get_clientdata(i2c);

	mfd_remove_devices(chip->dev);
	regmap_del_irq_chip(i2c->irq, chip->regmap_irq);

	return 0;
}

static const struct i2c_device_id da9062_i2c_id[] = {
	{ "da9061", COMPAT_TYPE_DA9061 },
	{ "da9062", COMPAT_TYPE_DA9062 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, da9062_i2c_id);

static struct i2c_driver da9062_i2c_driver = {
	.driver = {
		.name = "da9062",
		.of_match_table = of_match_ptr(da9062_dt_ids),
	},
	.probe    = da9062_i2c_probe,
	.remove   = da9062_i2c_remove,
	.id_table = da9062_i2c_id,
};

module_i2c_driver(da9062_i2c_driver);

MODULE_DESCRIPTION("Core device driver for Dialog DA9061 and DA9062");
MODULE_AUTHOR("Steve Twiss <stwiss.opensource@diasemi.com>");
MODULE_LICENSE("GPL");
