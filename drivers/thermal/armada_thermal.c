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
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

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

/* Errata fields */
#define CONTROL0_TSEN_TC_TRIM_MASK	0x7
#define CONTROL0_TSEN_TC_TRIM_VAL	0x3

#define CONTROL0_TSEN_START		BIT(0)
#define CONTROL0_TSEN_RESET		BIT(1)
#define CONTROL0_TSEN_ENABLE		BIT(2)
#define CONTROL0_TSEN_AVG_BYPASS	BIT(6)
#define CONTROL0_TSEN_CHAN_SHIFT	13
#define CONTROL0_TSEN_CHAN_MASK		0xF
#define CONTROL0_TSEN_OSR_SHIFT		24
#define CONTROL0_TSEN_OSR_MAX		0x3
#define CONTROL0_TSEN_MODE_SHIFT	30
#define CONTROL0_TSEN_MODE_EXTERNAL	0x2
#define CONTROL0_TSEN_MODE_MASK		0x3

#define CONTROL1_TSEN_AVG_SHIFT		0
#define CONTROL1_TSEN_AVG_MASK		0x7
#define CONTROL1_EXT_TSEN_SW_RESET	BIT(7)
#define CONTROL1_EXT_TSEN_HW_RESETn	BIT(8)

#define STATUS_POLL_PERIOD_US		1000
#define STATUS_POLL_TIMEOUT_US		100000

struct armada_thermal_data;

/* Marvell EBU Thermal Sensor Dev Structure */
struct armada_thermal_priv {
	struct device *dev;
	struct regmap *syscon;
	char zone_name[THERMAL_NAME_LENGTH];
	/* serialize temperature reads/updates */
	struct mutex update_lock;
	struct armada_thermal_data *data;
	int current_channel;
};

struct armada_thermal_data {
	/* Initialize the thermal IC */
	void (*init)(struct platform_device *pdev,
		     struct armada_thermal_priv *priv);

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

	/* Syscon access */
	unsigned int syscon_control0_off;
	unsigned int syscon_control1_off;
	unsigned int syscon_status_off;

	/* One sensor is in the thermal IC, the others are in the CPUs if any */
	unsigned int cpu_nr;
};

struct armada_drvdata {
	enum drvtype {
		LEGACY,
		SYSCON
	} type;
	union {
		struct armada_thermal_priv *priv;
		struct thermal_zone_device *tz;
	} data;
};

/*
 * struct armada_thermal_sensor - hold the information of one thermal sensor
 * @thermal: pointer to the local private structure
 * @tzd: pointer to the thermal zone device
 * @id: identifier of the thermal sensor
 */
struct armada_thermal_sensor {
	struct armada_thermal_priv *priv;
	int id;
};

static void armadaxp_init(struct platform_device *pdev,
			  struct armada_thermal_priv *priv)
{
	struct armada_thermal_data *data = priv->data;
	u32 reg;

	regmap_read(priv->syscon, data->syscon_control1_off, &reg);
	reg |= PMU_TDC0_OTF_CAL_MASK;

	/* Reference calibration value */
	reg &= ~PMU_TDC0_REF_CAL_CNT_MASK;
	reg |= (0xf1 << PMU_TDC0_REF_CAL_CNT_OFFS);

	/* Reset the sensor */
	reg |= PMU_TDC0_SW_RST_MASK;

	regmap_write(priv->syscon, data->syscon_control1_off, reg);

	/* Enable the sensor */
	regmap_read(priv->syscon, data->syscon_status_off, &reg);
	reg &= ~PMU_TM_DISABLE_MASK;
	regmap_write(priv->syscon, data->syscon_status_off, reg);
}

static void armada370_init(struct platform_device *pdev,
			   struct armada_thermal_priv *priv)
{
	struct armada_thermal_data *data = priv->data;
	u32 reg;

	regmap_read(priv->syscon, data->syscon_control1_off, &reg);
	reg |= PMU_TDC0_OTF_CAL_MASK;

	/* Reference calibration value */
	reg &= ~PMU_TDC0_REF_CAL_CNT_MASK;
	reg |= (0xf1 << PMU_TDC0_REF_CAL_CNT_OFFS);

	/* Reset the sensor */
	reg &= ~PMU_TDC0_START_CAL_MASK;

	regmap_write(priv->syscon, data->syscon_control1_off, reg);

	msleep(10);
}

static void armada375_init(struct platform_device *pdev,
			   struct armada_thermal_priv *priv)
{
	struct armada_thermal_data *data = priv->data;
	u32 reg;

	regmap_read(priv->syscon, data->syscon_control1_off, &reg);
	reg &= ~(A375_UNIT_CONTROL_MASK << A375_UNIT_CONTROL_SHIFT);
	reg &= ~A375_READOUT_INVERT;
	reg &= ~A375_HW_RESETn;
	regmap_write(priv->syscon, data->syscon_control1_off, reg);

	msleep(20);

	reg |= A375_HW_RESETn;
	regmap_write(priv->syscon, data->syscon_control1_off, reg);

	msleep(50);
}

static int armada_wait_sensor_validity(struct armada_thermal_priv *priv)
{
	u32 reg;

	return regmap_read_poll_timeout(priv->syscon,
					priv->data->syscon_status_off, reg,
					reg & priv->data->is_valid_bit,
					STATUS_POLL_PERIOD_US,
					STATUS_POLL_TIMEOUT_US);
}

static void armada380_init(struct platform_device *pdev,
			   struct armada_thermal_priv *priv)
{
	struct armada_thermal_data *data = priv->data;
	u32 reg;

	/* Disable the HW/SW reset */
	regmap_read(priv->syscon, data->syscon_control1_off, &reg);
	reg |= CONTROL1_EXT_TSEN_HW_RESETn;
	reg &= ~CONTROL1_EXT_TSEN_SW_RESET;
	regmap_write(priv->syscon, data->syscon_control1_off, reg);

	/* Set Tsen Tc Trim to correct default value (errata #132698) */
	regmap_read(priv->syscon, data->syscon_control0_off, &reg);
	reg &= ~CONTROL0_TSEN_TC_TRIM_MASK;
	reg |= CONTROL0_TSEN_TC_TRIM_VAL;
	regmap_write(priv->syscon, data->syscon_control0_off, reg);
}

static void armada_ap806_init(struct platform_device *pdev,
			      struct armada_thermal_priv *priv)
{
	struct armada_thermal_data *data = priv->data;
	u32 reg;

	regmap_read(priv->syscon, data->syscon_control0_off, &reg);
	reg &= ~CONTROL0_TSEN_RESET;
	reg |= CONTROL0_TSEN_START | CONTROL0_TSEN_ENABLE;

	/* Sample every ~2ms */
	reg |= CONTROL0_TSEN_OSR_MAX << CONTROL0_TSEN_OSR_SHIFT;

	/* Enable average (2 samples by default) */
	reg &= ~CONTROL0_TSEN_AVG_BYPASS;

	regmap_write(priv->syscon, data->syscon_control0_off, reg);
}

static void armada_cp110_init(struct platform_device *pdev,
			      struct armada_thermal_priv *priv)
{
	struct armada_thermal_data *data = priv->data;
	u32 reg;

	armada380_init(pdev, priv);

	/* Sample every ~2ms */
	regmap_read(priv->syscon, data->syscon_control0_off, &reg);
	reg |= CONTROL0_TSEN_OSR_MAX << CONTROL0_TSEN_OSR_SHIFT;
	regmap_write(priv->syscon, data->syscon_control0_off, reg);

	/* Average the output value over 2^1 = 2 samples */
	regmap_read(priv->syscon, data->syscon_control1_off, &reg);
	reg &= ~CONTROL1_TSEN_AVG_MASK << CONTROL1_TSEN_AVG_SHIFT;
	reg |= 1 << CONTROL1_TSEN_AVG_SHIFT;
	regmap_write(priv->syscon, data->syscon_control1_off, reg);
}

static bool armada_is_valid(struct armada_thermal_priv *priv)
{
	u32 reg;

	if (!priv->data->is_valid_bit)
		return true;

	regmap_read(priv->syscon, priv->data->syscon_status_off, &reg);

	return reg & priv->data->is_valid_bit;
}

/* There is currently no board with more than one sensor per channel */
static int armada_select_channel(struct armada_thermal_priv *priv, int channel)
{
	struct armada_thermal_data *data = priv->data;
	u32 ctrl0;

	if (channel < 0 || channel > priv->data->cpu_nr)
		return -EINVAL;

	if (priv->current_channel == channel)
		return 0;

	/* Stop the measurements */
	regmap_read(priv->syscon, data->syscon_control0_off, &ctrl0);
	ctrl0 &= ~CONTROL0_TSEN_START;
	regmap_write(priv->syscon, data->syscon_control0_off, ctrl0);

	/* Reset the mode, internal sensor will be automatically selected */
	ctrl0 &= ~(CONTROL0_TSEN_MODE_MASK << CONTROL0_TSEN_MODE_SHIFT);

	/* Other channels are external and should be selected accordingly */
	if (channel) {
		/* Change the mode to external */
		ctrl0 |= CONTROL0_TSEN_MODE_EXTERNAL <<
			 CONTROL0_TSEN_MODE_SHIFT;
		/* Select the sensor */
		ctrl0 &= ~(CONTROL0_TSEN_CHAN_MASK << CONTROL0_TSEN_CHAN_SHIFT);
		ctrl0 |= (channel - 1) << CONTROL0_TSEN_CHAN_SHIFT;
	}

	/* Actually set the mode/channel */
	regmap_write(priv->syscon, data->syscon_control0_off, ctrl0);
	priv->current_channel = channel;

	/* Re-start the measurements */
	ctrl0 |= CONTROL0_TSEN_START;
	regmap_write(priv->syscon, data->syscon_control0_off, ctrl0);

	/*
	 * The IP has a latency of ~15ms, so after updating the selected source,
	 * we must absolutely wait for the sensor validity bit to ensure we read
	 * actual data.
	 */
	if (armada_wait_sensor_validity(priv)) {
		dev_err(priv->dev,
			"Temperature sensor reading not valid\n");
		return -EIO;
	}

	return 0;
}

static int armada_read_sensor(struct armada_thermal_priv *priv, int *temp)
{
	u32 reg, div;
	s64 sample, b, m;

	regmap_read(priv->syscon, priv->data->syscon_status_off, &reg);
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

static int armada_get_temp_legacy(struct thermal_zone_device *thermal,
				  int *temp)
{
	struct armada_thermal_priv *priv = thermal->devdata;
	int ret;

	/* Valid check */
	if (armada_is_valid(priv)) {
		dev_err(priv->dev,
			"Temperature sensor reading not valid\n");
		return -EIO;
	}

	/* Do the actual reading */
	ret = armada_read_sensor(priv, temp);

	return ret;
}

static struct thermal_zone_device_ops legacy_ops = {
	.get_temp = armada_get_temp_legacy,
};

static int armada_get_temp(void *_sensor, int *temp)
{
	struct armada_thermal_sensor *sensor = _sensor;
	struct armada_thermal_priv *priv = sensor->priv;
	int ret;

	mutex_lock(&priv->update_lock);

	/* Select the desired channel */
	ret = armada_select_channel(priv, sensor->id);
	if (ret)
		goto unlock_mutex;

	/* Do the actual reading */
	ret = armada_read_sensor(priv, temp);

unlock_mutex:
	mutex_unlock(&priv->update_lock);

	return ret;
}

static struct thermal_zone_of_device_ops of_ops = {
	.get_temp = armada_get_temp,
};

static const struct armada_thermal_data armadaxp_data = {
	.init = armadaxp_init,
	.temp_shift = 10,
	.temp_mask = 0x1ff,
	.coef_b = 3153000000ULL,
	.coef_m = 10000000ULL,
	.coef_div = 13825,
	.syscon_status_off = 0xb0,
	.syscon_control1_off = 0xd0,
};

static const struct armada_thermal_data armada370_data = {
	.init = armada370_init,
	.is_valid_bit = BIT(9),
	.temp_shift = 10,
	.temp_mask = 0x1ff,
	.coef_b = 3153000000ULL,
	.coef_m = 10000000ULL,
	.coef_div = 13825,
	.syscon_status_off = 0x0,
	.syscon_control1_off = 0x4,
};

static const struct armada_thermal_data armada375_data = {
	.init = armada375_init,
	.is_valid_bit = BIT(10),
	.temp_shift = 0,
	.temp_mask = 0x1ff,
	.coef_b = 3171900000ULL,
	.coef_m = 10000000ULL,
	.coef_div = 13616,
	.syscon_status_off = 0x78,
	.syscon_control0_off = 0x7c,
	.syscon_control1_off = 0x80,
};

static const struct armada_thermal_data armada380_data = {
	.init = armada380_init,
	.is_valid_bit = BIT(10),
	.temp_shift = 0,
	.temp_mask = 0x3ff,
	.coef_b = 1172499100ULL,
	.coef_m = 2000096ULL,
	.coef_div = 4201,
	.inverted = true,
	.syscon_control0_off = 0x70,
	.syscon_control1_off = 0x74,
	.syscon_status_off = 0x78,
};

static const struct armada_thermal_data armada_ap806_data = {
	.init = armada_ap806_init,
	.is_valid_bit = BIT(16),
	.temp_shift = 0,
	.temp_mask = 0x3ff,
	.coef_b = -150000LL,
	.coef_m = 423ULL,
	.coef_div = 1,
	.inverted = true,
	.signed_sample = true,
	.syscon_control0_off = 0x84,
	.syscon_control1_off = 0x88,
	.syscon_status_off = 0x8C,
	.cpu_nr = 4,
};

static const struct armada_thermal_data armada_cp110_data = {
	.init = armada_cp110_init,
	.is_valid_bit = BIT(10),
	.temp_shift = 0,
	.temp_mask = 0x3ff,
	.coef_b = 1172499100ULL,
	.coef_m = 2000096ULL,
	.coef_div = 4201,
	.inverted = true,
	.syscon_control0_off = 0x70,
	.syscon_control1_off = 0x74,
	.syscon_status_off = 0x78,
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

static const struct regmap_config armada_thermal_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
};

static int armada_thermal_probe_legacy(struct platform_device *pdev,
				       struct armada_thermal_priv *priv)
{
	struct armada_thermal_data *data = priv->data;
	struct resource *res;
	void __iomem *base;

	/* First memory region points towards the status register */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EIO;

	/*
	 * Edit the resource start address and length to map over all the
	 * registers, instead of pointing at them one by one.
	 */
	res->start -= data->syscon_status_off;
	res->end = res->start + max(data->syscon_status_off,
				    max(data->syscon_control0_off,
					data->syscon_control1_off)) +
		   sizeof(unsigned int) - 1;

	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	priv->syscon = devm_regmap_init_mmio(&pdev->dev, base,
					     &armada_thermal_regmap_config);
	if (IS_ERR(priv->syscon))
		return PTR_ERR(priv->syscon);

	return 0;
}

static int armada_thermal_probe_syscon(struct platform_device *pdev,
				       struct armada_thermal_priv *priv)
{
	priv->syscon = syscon_node_to_regmap(pdev->dev.parent->of_node);
	if (IS_ERR(priv->syscon))
		return PTR_ERR(priv->syscon);

	return 0;
}

static void armada_set_sane_name(struct platform_device *pdev,
				 struct armada_thermal_priv *priv)
{
	const char *name = dev_name(&pdev->dev);
	char *insane_char;

	if (strlen(name) > THERMAL_NAME_LENGTH) {
		/*
		 * When inside a system controller, the device name has the
		 * form: f06f8000.system-controller:ap-thermal so stripping
		 * after the ':' should give us a shorter but meaningful name.
		 */
		name = strrchr(name, ':');
		if (!name)
			name = "armada_thermal";
		else
			name++;
	}

	/* Save the name locally */
	strncpy(priv->zone_name, name, THERMAL_NAME_LENGTH - 1);
	priv->zone_name[THERMAL_NAME_LENGTH - 1] = '\0';

	/* Then check there are no '-' or hwmon core will complain */
	do {
		insane_char = strpbrk(priv->zone_name, "-");
		if (insane_char)
			*insane_char = '_';
	} while (insane_char);
}

static int armada_thermal_probe(struct platform_device *pdev)
{
	struct thermal_zone_device *tz;
	struct armada_thermal_sensor *sensor;
	struct armada_drvdata *drvdata;
	const struct of_device_id *match;
	struct armada_thermal_priv *priv;
	int sensor_id;
	int ret;

	match = of_match_device(armada_thermal_id_table, &pdev->dev);
	if (!match)
		return -ENODEV;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	priv->data = (struct armada_thermal_data *)match->data;

	mutex_init(&priv->update_lock);

	/*
	 * Legacy DT bindings only described "control1" register (also referred
	 * as "control MSB" on old documentation). Then, bindings moved to cover
	 * "control0/control LSB" and "control1/control MSB" registers within
	 * the same resource, which was then of size 8 instead of 4.
	 *
	 * The logic of defining sporadic registers is broken. For instance, it
	 * blocked the addition of the overheat interrupt feature that needed
	 * another resource somewhere else in the same memory area. One solution
	 * is to define an overall system controller and put the thermal node
	 * into it, which requires the use of regmaps across all the driver.
	 */
	if (IS_ERR(syscon_node_to_regmap(pdev->dev.parent->of_node))) {
		/* Ensure device name is correct for the thermal core */
		armada_set_sane_name(pdev, priv);

		ret = armada_thermal_probe_legacy(pdev, priv);
		if (ret)
			return ret;

		priv->data->init(pdev, priv);

		/* Wait the sensors to be valid */
		armada_wait_sensor_validity(priv);

		tz = thermal_zone_device_register(priv->zone_name, 0, 0, priv,
						  &legacy_ops, NULL, 0, 0);
		if (IS_ERR(tz)) {
			dev_err(&pdev->dev,
				"Failed to register thermal zone device\n");
			return PTR_ERR(tz);
		}

		drvdata->type = LEGACY;
		drvdata->data.tz = tz;
		platform_set_drvdata(pdev, drvdata);

		return 0;
	}

	ret = armada_thermal_probe_syscon(pdev, priv);
	if (ret)
		return ret;

	priv->current_channel = -1;
	priv->data->init(pdev, priv);
	drvdata->type = SYSCON;
	drvdata->data.priv = priv;
	platform_set_drvdata(pdev, drvdata);

	/*
	 * There is one channel for the IC and one per CPU (if any), each
	 * channel has one sensor.
	 */
	for (sensor_id = 0; sensor_id <= priv->data->cpu_nr; sensor_id++) {
		sensor = devm_kzalloc(&pdev->dev,
				      sizeof(struct armada_thermal_sensor),
				      GFP_KERNEL);
		if (!sensor)
			return -ENOMEM;

		/* Register the sensor */
		sensor->priv = priv;
		sensor->id = sensor_id;
		tz = devm_thermal_zone_of_sensor_register(&pdev->dev,
							  sensor->id, sensor,
							  &of_ops);
		if (IS_ERR(tz)) {
			dev_info(&pdev->dev, "Thermal sensor %d unavailable\n",
				 sensor_id);
			devm_kfree(&pdev->dev, sensor);
			continue;
		}
	}

	return 0;
}

static int armada_thermal_exit(struct platform_device *pdev)
{
	struct armada_drvdata *drvdata = platform_get_drvdata(pdev);

	if (drvdata->type == LEGACY)
		thermal_zone_device_unregister(drvdata->data.tz);

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
