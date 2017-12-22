/*
 * Marvell EBU Armada SoCs thermal sensor driver
 *
 * Copyright (C) 2013 Marvell
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/thermal.h>
#include <linux/iopoll.h>

/* Thermal Manager Control and Status Register */
#define PMU_TDC0_SW_RST_MASK		(0x1 << 1)
#define PMU_TM_DISABLE_OFFS		0
#define PMU_TM_DISABLE_MASK		(0x1 << PMU_TM_DISABLE_OFFS)
#define PMU_TDC0_REF_CAL_CNT_OFFS	11
#define PMU_TDC0_REF_CAL_CNT_MASK	(0x1ff << PMU_TDC0_REF_CAL_CNT_OFFS)
#define PMU_TDC0_OTF_CAL_MASK		(0x1 << 30)
#define PMU_TDC0_START_CAL_MASK		(0x1 << 25)

#define A375_UNIT_CONTROL_SHIFT		27
#define A375_UNIT_CONTROL_MASK		0x7
#define A375_READOUT_INVERT		BIT(15)
#define A375_HW_RESETn			BIT(8)

/* Legacy bindings */
#define LEGACY_CONTROL_MEM_LEN		0x4

/* Current bindings with the 2 control registers under the same memory area */
#define LEGACY_CONTROL1_OFFSET		0x0
#define CONTROL0_OFFSET			0x0
#define CONTROL1_OFFSET			0x4

/* Errata fields */
#define CONTROL0_TSEN_TC_TRIM_MASK	0x7
#define CONTROL0_TSEN_TC_TRIM_VAL	0x3

/* TSEN refers to the temperature sensors within the AP */
#define CONTROL0_TSEN_START		BIT(0)
#define CONTROL0_TSEN_RESET		BIT(1)
#define CONTROL0_TSEN_ENABLE		BIT(2)

/* EXT_TSEN refers to the external temperature sensors, out of the AP */
#define CONTROL1_EXT_TSEN_SW_RESET	BIT(7)
#define CONTROL1_EXT_TSEN_HW_RESETn	BIT(8)

#define STATUS_POLL_PERIOD_US		1000
#define STATUS_POLL_TIMEOUT_US		100000

struct armada_thermal_data;

/* Marvell EBU Thermal Sensor Dev Structure */
struct armada_thermal_priv {
	void __iomem *status;
	void __iomem *control0;
	void __iomem *control1;
	struct armada_thermal_data *data;
};

struct armada_thermal_data {
	/* Initialize the sensor */
	void (*init_sensor)(struct platform_device *pdev,
			    struct armada_thermal_priv *);

	/* Test for a valid sensor value (optional) */
	bool (*is_valid)(struct armada_thermal_priv *);

	/* Formula coeficients: temp = (b - m * reg) / div */
	s64 coef_b;
	s64 coef_m;
	u32 coef_div;
	bool inverted;
	bool signed_sample;

	/* Register shift and mask to access the sensor temperature */
	unsigned int temp_shift;
	unsigned int temp_mask;
	u32 is_valid_bit;
	bool needs_control0;
};

static void armadaxp_init_sensor(struct platform_device *pdev,
				 struct armada_thermal_priv *priv)
{
	u32 reg;

	reg = readl_relaxed(priv->control1);
	reg |= PMU_TDC0_OTF_CAL_MASK;
	writel(reg, priv->control1);

	/* Reference calibration value */
	reg &= ~PMU_TDC0_REF_CAL_CNT_MASK;
	reg |= (0xf1 << PMU_TDC0_REF_CAL_CNT_OFFS);
	writel(reg, priv->control1);

	/* Reset the sensor */
	reg = readl_relaxed(priv->control1);
	writel((reg | PMU_TDC0_SW_RST_MASK), priv->control1);

	writel(reg, priv->control1);

	/* Enable the sensor */
	reg = readl_relaxed(priv->status);
	reg &= ~PMU_TM_DISABLE_MASK;
	writel(reg, priv->status);
}

static void armada370_init_sensor(struct platform_device *pdev,
				  struct armada_thermal_priv *priv)
{
	u32 reg;

	reg = readl_relaxed(priv->control1);
	reg |= PMU_TDC0_OTF_CAL_MASK;
	writel(reg, priv->control1);

	/* Reference calibration value */
	reg &= ~PMU_TDC0_REF_CAL_CNT_MASK;
	reg |= (0xf1 << PMU_TDC0_REF_CAL_CNT_OFFS);
	writel(reg, priv->control1);

	reg &= ~PMU_TDC0_START_CAL_MASK;
	writel(reg, priv->control1);

	msleep(10);
}

static void armada375_init_sensor(struct platform_device *pdev,
				  struct armada_thermal_priv *priv)
{
	u32 reg;

	reg = readl(priv->control1);
	reg &= ~(A375_UNIT_CONTROL_MASK << A375_UNIT_CONTROL_SHIFT);
	reg &= ~A375_READOUT_INVERT;
	reg &= ~A375_HW_RESETn;

	writel(reg, priv->control1);
	msleep(20);

	reg |= A375_HW_RESETn;
	writel(reg, priv->control1);
	msleep(50);
}

static void armada_wait_sensor_validity(struct armada_thermal_priv *priv)
{
	u32 reg;

	readl_relaxed_poll_timeout(priv->status, reg,
				   reg & priv->data->is_valid_bit,
				   STATUS_POLL_PERIOD_US,
				   STATUS_POLL_TIMEOUT_US);
}

static void armada380_init_sensor(struct platform_device *pdev,
				  struct armada_thermal_priv *priv)
{
	u32 reg = readl_relaxed(priv->control1);

	/* Disable the HW/SW reset */
	reg |= CONTROL1_EXT_TSEN_HW_RESETn;
	reg &= ~CONTROL1_EXT_TSEN_SW_RESET;
	writel(reg, priv->control1);

	/* Set Tsen Tc Trim to correct default value (errata #132698) */
	if (priv->control0) {
		reg = readl_relaxed(priv->control0);
		reg &= ~CONTROL0_TSEN_TC_TRIM_MASK;
		reg |= CONTROL0_TSEN_TC_TRIM_VAL;
		writel(reg, priv->control0);
	}

	/* Wait the sensors to be valid or the core will warn the user */
	armada_wait_sensor_validity(priv);
}

static void armada_ap806_init_sensor(struct platform_device *pdev,
				     struct armada_thermal_priv *priv)
{
	u32 reg;

	reg = readl_relaxed(priv->control0);
	reg &= ~CONTROL0_TSEN_RESET;
	reg |= CONTROL0_TSEN_START | CONTROL0_TSEN_ENABLE;
	writel(reg, priv->control0);

	/* Wait the sensors to be valid or the core will warn the user */
	armada_wait_sensor_validity(priv);
}

static bool armada_is_valid(struct armada_thermal_priv *priv)
{
	u32 reg = readl_relaxed(priv->status);

	return reg & priv->data->is_valid_bit;
}

static int armada_get_temp(struct thermal_zone_device *thermal,
			   int *temp)
{
	struct armada_thermal_priv *priv = thermal->devdata;
	u32 reg, div;
	s64 sample, b, m;

	/* Valid check */
	if (priv->data->is_valid && !priv->data->is_valid(priv)) {
		dev_err(&thermal->device,
			"Temperature sensor reading not valid\n");
		return -EIO;
	}

	reg = readl_relaxed(priv->status);
	reg = (reg >> priv->data->temp_shift) & priv->data->temp_mask;
	if (priv->data->signed_sample)
		/* The most significant bit is the sign bit */
		sample = sign_extend32(reg, fls(priv->data->temp_mask) - 1);
	else
		sample = reg;

	/* Get formula coeficients */
	b = priv->data->coef_b;
	m = priv->data->coef_m;
	div = priv->data->coef_div;

	if (priv->data->inverted)
		*temp = div_s64((m * sample) - b, div);
	else
		*temp = div_s64(b - (m * sample), div);

	return 0;
}

static struct thermal_zone_device_ops ops = {
	.get_temp = armada_get_temp,
};

static const struct armada_thermal_data armadaxp_data = {
	.init_sensor = armadaxp_init_sensor,
	.temp_shift = 10,
	.temp_mask = 0x1ff,
	.coef_b = 3153000000ULL,
	.coef_m = 10000000ULL,
	.coef_div = 13825,
};

static const struct armada_thermal_data armada370_data = {
	.is_valid = armada_is_valid,
	.init_sensor = armada370_init_sensor,
	.is_valid_bit = BIT(9),
	.temp_shift = 10,
	.temp_mask = 0x1ff,
	.coef_b = 3153000000ULL,
	.coef_m = 10000000ULL,
	.coef_div = 13825,
};

static const struct armada_thermal_data armada375_data = {
	.is_valid = armada_is_valid,
	.init_sensor = armada375_init_sensor,
	.is_valid_bit = BIT(10),
	.temp_shift = 0,
	.temp_mask = 0x1ff,
	.coef_b = 3171900000ULL,
	.coef_m = 10000000ULL,
	.coef_div = 13616,
	.needs_control0 = true,
};

static const struct armada_thermal_data armada380_data = {
	.is_valid = armada_is_valid,
	.init_sensor = armada380_init_sensor,
	.is_valid_bit = BIT(10),
	.temp_shift = 0,
	.temp_mask = 0x3ff,
	.coef_b = 1172499100ULL,
	.coef_m = 2000096ULL,
	.coef_div = 4201,
	.inverted = true,
};

static const struct armada_thermal_data armada_ap806_data = {
	.is_valid = armada_is_valid,
	.init_sensor = armada_ap806_init_sensor,
	.is_valid_bit = BIT(16),
	.temp_shift = 0,
	.temp_mask = 0x3ff,
	.coef_b = -150000LL,
	.coef_m = 423ULL,
	.coef_div = 1,
	.inverted = true,
	.signed_sample = true,
	.needs_control0 = true,
};

static const struct armada_thermal_data armada_cp110_data = {
	.is_valid = armada_is_valid,
	.init_sensor = armada380_init_sensor,
	.is_valid_bit = BIT(10),
	.temp_shift = 0,
	.temp_mask = 0x3ff,
	.coef_b = 1172499100ULL,
	.coef_m = 2000096ULL,
	.coef_div = 4201,
	.inverted = true,
	.needs_control0 = true,
};

static const struct of_device_id armada_thermal_id_table[] = {
	{
		.compatible = "marvell,armadaxp-thermal",
		.data       = &armadaxp_data,
	},
	{
		.compatible = "marvell,armada370-thermal",
		.data       = &armada370_data,
	},
	{
		.compatible = "marvell,armada375-thermal",
		.data       = &armada375_data,
	},
	{
		.compatible = "marvell,armada380-thermal",
		.data       = &armada380_data,
	},
	{
		.compatible = "marvell,armada-ap806-thermal",
		.data       = &armada_ap806_data,
	},
	{
		.compatible = "marvell,armada-cp110-thermal",
		.data       = &armada_cp110_data,
	},
	{
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(of, armada_thermal_id_table);

static int armada_thermal_probe(struct platform_device *pdev)
{
	void __iomem *control = NULL;
	struct thermal_zone_device *thermal;
	const struct of_device_id *match;
	struct armada_thermal_priv *priv;
	struct resource *res;

	match = of_match_device(armada_thermal_id_table, &pdev->dev);
	if (!match)
		return -ENODEV;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->status = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->status))
		return PTR_ERR(priv->status);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	control = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(control))
		return PTR_ERR(control);

	priv->data = (struct armada_thermal_data *)match->data;

	/*
	 * Legacy DT bindings only described "control1" register (also referred
	 * as "control MSB" on old documentation). New bindings cover
	 * "control0/control LSB" and "control1/control MSB" registers within
	 * the same resource, which is then of size 8 instead of 4.
	 */
	if (resource_size(res) == LEGACY_CONTROL_MEM_LEN) {
		/* ->control0 unavailable in this configuration */
		if (priv->data->needs_control0) {
			dev_err(&pdev->dev, "No access to control0 register\n");
			return -EINVAL;
		}

		priv->control1 = control + LEGACY_CONTROL1_OFFSET;
	} else {
		priv->control0 = control + CONTROL0_OFFSET;
		priv->control1 = control + CONTROL1_OFFSET;
	}

	priv->data->init_sensor(pdev, priv);

	thermal = thermal_zone_device_register(dev_name(&pdev->dev), 0, 0, priv,
					       &ops, NULL, 0, 0);
	if (IS_ERR(thermal)) {
		dev_err(&pdev->dev,
			"Failed to register thermal zone device\n");
		return PTR_ERR(thermal);
	}

	platform_set_drvdata(pdev, thermal);

	return 0;
}

static int armada_thermal_exit(struct platform_device *pdev)
{
	struct thermal_zone_device *armada_thermal =
		platform_get_drvdata(pdev);

	thermal_zone_device_unregister(armada_thermal);

	return 0;
}

static struct platform_driver armada_thermal_driver = {
	.probe = armada_thermal_probe,
	.remove = armada_thermal_exit,
	.driver = {
		.name = "armada_thermal",
		.of_match_table = armada_thermal_id_table,
	},
};

module_platform_driver(armada_thermal_driver);

MODULE_AUTHOR("Ezequiel Garcia <ezequiel.garcia@free-electrons.com>");
MODULE_DESCRIPTION("Marvell EBU Armada SoCs thermal driver");
MODULE_LICENSE("GPL v2");
