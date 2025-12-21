// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * Apple SMC hwmon driver for Apple Silicon platforms
 *
 * The System Management Controller on Apple Silicon devices is responsible for
 * measuring data from sensors across the SoC and machine. These include power,
 * temperature, voltage and current sensors. Some "sensors" actually expose
 * derived values. An example of this is the key PHPC, which is an estimate
 * of the heat energy being dissipated by the SoC.
 *
 * While each SoC only has one SMC variant, each platform exposes a different
 * set of sensors. For example, M1 MacBooks expose battery telemetry sensors
 * which are not present on the M1 Mac mini. For this reason, the available
 * sensors for a given platform are described in the device tree in a child
 * node of the SMC device. We must walk this list of available sensors and
 * populate the required hwmon data structures at runtime.
 *
 * Originally based on a concept by Jean-Francois Bortolotti <jeff@borto.fr>
 *
 * Copyright The Asahi Linux Contributors
 */

#include <linux/bitfield.h>
#include <linux/hwmon.h>
#include <linux/mfd/macsmc.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define MAX_LABEL_LENGTH	32

/* Temperature, voltage, current, power, fan(s) */
#define NUM_SENSOR_TYPES	5

#define FLT_EXP_BIAS	127
#define FLT_EXP_MASK	GENMASK(30, 23)
#define FLT_MANT_BIAS	23
#define FLT_MANT_MASK	GENMASK(22, 0)
#define FLT_SIGN_MASK	BIT(31)

static bool fan_control;
module_param_unsafe(fan_control, bool, 0644);
MODULE_PARM_DESC(fan_control,
		 "Override the SMC to set your own fan speeds on supported machines");

struct macsmc_hwmon_sensor {
	struct apple_smc_key_info info;
	smc_key macsmc_key;
	char label[MAX_LABEL_LENGTH];
	u32 attrs;
};

struct macsmc_hwmon_fan {
	struct macsmc_hwmon_sensor now;
	struct macsmc_hwmon_sensor min;
	struct macsmc_hwmon_sensor max;
	struct macsmc_hwmon_sensor set;
	struct macsmc_hwmon_sensor mode;
	char label[MAX_LABEL_LENGTH];
	u32 attrs;
	bool manual;
};

struct macsmc_hwmon_sensors {
	struct hwmon_channel_info channel_info;
	struct macsmc_hwmon_sensor *sensors;
	u32 count;
};

struct macsmc_hwmon_fans {
	struct hwmon_channel_info channel_info;
	struct macsmc_hwmon_fan *fans;
	u32 count;
};

struct macsmc_hwmon {
	struct device *dev;
	struct apple_smc *smc;
	struct device *hwmon_dev;
	struct hwmon_chip_info chip_info;
	/* Chip + sensor types + NULL */
	const struct hwmon_channel_info *channel_infos[1 + NUM_SENSOR_TYPES + 1];
	struct macsmc_hwmon_sensors temp;
	struct macsmc_hwmon_sensors volt;
	struct macsmc_hwmon_sensors curr;
	struct macsmc_hwmon_sensors power;
	struct macsmc_hwmon_fans fan;
};

static int macsmc_hwmon_read_label(struct device *dev,
				   enum hwmon_sensor_types type, u32 attr,
				   int channel, const char **str)
{
	struct macsmc_hwmon *hwmon = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		*str = hwmon->temp.sensors[channel].label;
		break;
	case hwmon_in:
		*str = hwmon->volt.sensors[channel].label;
		break;
	case hwmon_curr:
		*str = hwmon->curr.sensors[channel].label;
		break;
	case hwmon_power:
		*str = hwmon->power.sensors[channel].label;
		break;
	case hwmon_fan:
		*str = hwmon->fan.fans[channel].label;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

/*
 * A number of sensors report data in a 48.16 fixed-point decimal format that is
 * not used by any other function of the SMC.
 */
static int macsmc_hwmon_read_ioft_scaled(struct apple_smc *smc, smc_key key,
					 u64 *p, int scale)
{
	u64 val;
	int ret;

	ret = apple_smc_read_u64(smc, key, &val);
	if (ret < 0)
		return ret;

	*p = mult_frac(val, scale, 65536);

	return 0;
}

/*
 * Many sensors report their data as IEEE-754 floats. No other SMC function uses
 * them.
 */
static int macsmc_hwmon_read_f32_scaled(struct apple_smc *smc, smc_key key,
					int *p, int scale)
{
	u32 fval;
	u64 val;
	int ret, exp;

	ret = apple_smc_read_u32(smc, key, &fval);
	if (ret < 0)
		return ret;

	val = ((u64)((fval & FLT_MANT_MASK) | BIT(23)));
	exp = ((fval >> 23) & 0xff) - FLT_EXP_BIAS - FLT_MANT_BIAS;

	/* We never have negatively scaled SMC floats */
	val *= scale;

	if (exp > 63)
		val = U64_MAX;
	else if (exp < -63)
		val = 0;
	else if (exp < 0)
		val >>= -exp;
	else if (exp != 0 && (val & ~((1UL << (64 - exp)) - 1))) /* overflow */
		val = U64_MAX;
	else
		val <<= exp;

	if (fval & FLT_SIGN_MASK) {
		if (val > (-(s64)INT_MIN))
			*p = INT_MIN;
		else
			*p = -val;
	} else {
		if (val > INT_MAX)
			*p = INT_MAX;
		else
			*p = val;
	}

	return 0;
}

/*
 * The SMC has keys of multiple types, denoted by a FourCC of the same format
 * as the key ID. We don't know what data type a key encodes until we poke at it.
 */
static int macsmc_hwmon_read_key(struct apple_smc *smc,
				 struct macsmc_hwmon_sensor *sensor, int scale,
				 long *val)
{
	int ret;

	switch (sensor->info.type_code) {
	/* 32-bit IEEE 754 float */
	case __SMC_KEY('f', 'l', 't', ' '): {
		u32 flt_ = 0;

		ret = macsmc_hwmon_read_f32_scaled(smc, sensor->macsmc_key,
						   &flt_, scale);
		if (ret)
			return ret;

		*val = flt_;
		break;
	}
	/* 48.16 fixed point decimal */
	case __SMC_KEY('i', 'o', 'f', 't'): {
		u64 ioft = 0;

		ret = macsmc_hwmon_read_ioft_scaled(smc, sensor->macsmc_key,
						    &ioft, scale);
		if (ret)
			return ret;

		*val = (long)ioft;
		break;
	}
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int macsmc_hwmon_write_f32(struct apple_smc *smc, smc_key key, int value)
{
	u64 val;
	u32 fval = 0;
	int exp = 0, neg;

	val = abs(value);
	neg = val != value;

	if (val) {
		int msb = __fls(val) - exp;

		if (msb > 23) {
			val >>= msb - FLT_MANT_BIAS;
			exp -= msb - FLT_MANT_BIAS;
		} else if (msb < 23) {
			val <<= FLT_MANT_BIAS - msb;
			exp += msb;
		}

		fval = FIELD_PREP(FLT_SIGN_MASK, neg) |
		       FIELD_PREP(FLT_EXP_MASK, exp + FLT_EXP_BIAS) |
		       FIELD_PREP(FLT_MANT_MASK, val);
	}

	return apple_smc_write_u32(smc, key, fval);
}

static int macsmc_hwmon_write_key(struct apple_smc *smc,
				  struct macsmc_hwmon_sensor *sensor, long val)
{
	switch (sensor->info.type_code) {
	/* 32-bit IEEE 754 float */
	case __SMC_KEY('f', 'l', 't', ' '):
		return macsmc_hwmon_write_f32(smc, sensor->macsmc_key, val);
	/* unsigned 8-bit integer */
	case __SMC_KEY('u', 'i', '8', ' '):
		return apple_smc_write_u8(smc, sensor->macsmc_key, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int macsmc_hwmon_read_fan(struct macsmc_hwmon *hwmon, u32 attr, int chan,
				 long *val)
{
	switch (attr) {
	case hwmon_fan_input:
		return macsmc_hwmon_read_key(hwmon->smc,
					     &hwmon->fan.fans[chan].now, 1, val);
	case hwmon_fan_min:
		return macsmc_hwmon_read_key(hwmon->smc,
					     &hwmon->fan.fans[chan].min, 1, val);
	case hwmon_fan_max:
		return macsmc_hwmon_read_key(hwmon->smc,
					     &hwmon->fan.fans[chan].max, 1, val);
	case hwmon_fan_target:
		return macsmc_hwmon_read_key(hwmon->smc,
					     &hwmon->fan.fans[chan].set, 1, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int macsmc_hwmon_write_fan(struct device *dev, u32 attr, int channel,
				  long val)
{
	struct macsmc_hwmon *hwmon = dev_get_drvdata(dev);
	long min, max;
	int ret;

	if (!fan_control || hwmon->fan.fans[channel].mode.macsmc_key == 0)
		return -EOPNOTSUPP;

	/*
	 * The SMC does no sanity checks on requested fan speeds, so we need to.
	 */
	ret = macsmc_hwmon_read_key(hwmon->smc, &hwmon->fan.fans[channel].min,
				    1, &min);
	if (ret)
		return ret;

	ret = macsmc_hwmon_read_key(hwmon->smc, &hwmon->fan.fans[channel].max,
				    1, &max);
	if (ret)
		return ret;

	if (val >= min && val <= max) {
		if (!hwmon->fan.fans[channel].manual) {
			/* Write 1 to mode key for manual control */
			ret = macsmc_hwmon_write_key(hwmon->smc,
						     &hwmon->fan.fans[channel].mode, 1);
			if (ret < 0)
				return ret;

			hwmon->fan.fans[channel].manual = true;
		}
		return macsmc_hwmon_write_key(hwmon->smc,
					      &hwmon->fan.fans[channel].set, val);
	} else if (!val) {
		if (hwmon->fan.fans[channel].manual) {
			ret = macsmc_hwmon_write_key(hwmon->smc,
						     &hwmon->fan.fans[channel].mode, 0);
			if (ret < 0)
				return ret;

			hwmon->fan.fans[channel].manual = false;
		}
	} else {
		return -EINVAL;
	}

	return 0;
}

static int macsmc_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long *val)
{
	struct macsmc_hwmon *hwmon = dev_get_drvdata(dev);
	int ret = 0;

	switch (type) {
	case hwmon_temp:
		ret = macsmc_hwmon_read_key(hwmon->smc,
					    &hwmon->temp.sensors[channel], 1000, val);
		break;
	case hwmon_in:
		ret = macsmc_hwmon_read_key(hwmon->smc,
					    &hwmon->volt.sensors[channel], 1000, val);
		break;
	case hwmon_curr:
		ret = macsmc_hwmon_read_key(hwmon->smc,
					    &hwmon->curr.sensors[channel], 1000, val);
		break;
	case hwmon_power:
		/* SMC returns power in Watts with acceptable precision to scale to uW */
		ret = macsmc_hwmon_read_key(hwmon->smc,
					    &hwmon->power.sensors[channel],
					    1000000, val);
		break;
	case hwmon_fan:
		ret = macsmc_hwmon_read_fan(hwmon, attr, channel, val);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return ret;
}

static int macsmc_hwmon_write(struct device *dev, enum hwmon_sensor_types type,
			      u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_fan:
		return macsmc_hwmon_write_fan(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t macsmc_hwmon_fan_is_visible(const struct macsmc_hwmon_fan *fan,
					   u32 attr)
{
	if (fan->attrs & BIT(attr)) {
		if (attr == hwmon_fan_target && fan_control && fan->mode.macsmc_key)
			return 0644;

		return 0444;
	}

	return 0;
}

static umode_t macsmc_hwmon_is_visible(const void *data,
				       enum hwmon_sensor_types type, u32 attr,
				       int channel)
{
	const struct macsmc_hwmon *hwmon = data;
	struct macsmc_hwmon_sensor *sensor;

	switch (type) {
	case hwmon_in:
		sensor = &hwmon->volt.sensors[channel];
		break;
	case hwmon_curr:
		sensor = &hwmon->curr.sensors[channel];
		break;
	case hwmon_power:
		sensor = &hwmon->power.sensors[channel];
		break;
	case hwmon_temp:
		sensor = &hwmon->temp.sensors[channel];
		break;
	case hwmon_fan:
		return macsmc_hwmon_fan_is_visible(&hwmon->fan.fans[channel], attr);
	default:
		return 0;
	}

	/* Sensors only register ro attributes */
	if (sensor->attrs & BIT(attr))
		return 0444;

	return 0;
}

static const struct hwmon_ops macsmc_hwmon_ops = {
	.is_visible = macsmc_hwmon_is_visible,
	.read = macsmc_hwmon_read,
	.read_string = macsmc_hwmon_read_label,
	.write = macsmc_hwmon_write,
};

/*
 * Get the key metadata, including key data type, from the SMC.
 */
static int macsmc_hwmon_parse_key(struct device *dev, struct apple_smc *smc,
				  struct macsmc_hwmon_sensor *sensor,
				  const char *key)
{
	int ret;

	ret = apple_smc_get_key_info(smc, _SMC_KEY(key), &sensor->info);
	if (ret) {
		dev_dbg(dev, "Failed to retrieve key info for %s\n", key);
		return ret;
	}

	sensor->macsmc_key = _SMC_KEY(key);

	return 0;
}

/*
 * A sensor is a single key-value pair as made available by the SMC.
 * The devicetree gives us the SMC key ID and a friendly name where the
 * purpose of the sensor is known.
 */
static int macsmc_hwmon_create_sensor(struct device *dev, struct apple_smc *smc,
				      struct device_node *sensor_node,
				      struct macsmc_hwmon_sensor *sensor)
{
	const char *key, *label;
	int ret;

	ret = of_property_read_string(sensor_node, "apple,key-id", &key);
	if (ret) {
		dev_dbg(dev, "Could not find apple,key-id in sensor node\n");
		return ret;
	}

	ret = macsmc_hwmon_parse_key(dev, smc, sensor, key);
	if (ret)
		return ret;

	ret = of_property_read_string(sensor_node, "label", &label);
	if (ret)
		dev_dbg(dev, "No label found for sensor %s\n", key);
	else
		strscpy_pad(sensor->label, label, sizeof(sensor->label));

	return 0;
}

/*
 * Fan data is exposed by the SMC as multiple sensors.
 *
 * The devicetree schema reuses apple,key-id for the actual fan speed sensor.
 * Min, max and target keys do not need labels, so we can reuse label
 * for naming the entire fan.
 */
static int macsmc_hwmon_create_fan(struct device *dev, struct apple_smc *smc,
				   struct device_node *fan_node,
				   struct macsmc_hwmon_fan *fan)
{
	const char *label, *now, *min, *max, *set, *mode;
	int ret;

	ret = of_property_read_string(fan_node, "apple,key-id", &now);
	if (ret) {
		dev_err(dev, "apple,key-id not found in fan node!\n");
		return ret;
	}

	ret = macsmc_hwmon_parse_key(dev, smc, &fan->now, now);
	if (ret)
		return ret;

	fan->attrs = HWMON_F_INPUT;

	ret = of_property_read_string(fan_node, "label", &label);
	if (ret) {
		dev_dbg(dev, "No label found for fan %s\n", now);
	} else {
		strscpy_pad(fan->label, label, sizeof(fan->label));
		fan->attrs |= HWMON_F_LABEL;
	}

	/* The following keys are not required to simply monitor fan speed */
	if (!of_property_read_string(fan_node, "apple,fan-minimum", &min)) {
		ret = macsmc_hwmon_parse_key(dev, smc, &fan->min, min);
		if (ret)
			return ret;

		fan->attrs |= HWMON_F_MIN;
	}

	if (!of_property_read_string(fan_node, "apple,fan-maximum", &max)) {
		ret = macsmc_hwmon_parse_key(dev, smc, &fan->max, max);
		if (ret)
			return ret;

		fan->attrs |= HWMON_F_MAX;
	}

	if (!of_property_read_string(fan_node, "apple,fan-target", &set)) {
		ret = macsmc_hwmon_parse_key(dev, smc, &fan->set, set);
		if (ret)
			return ret;

		fan->attrs |= HWMON_F_TARGET;
	}

	if (!of_property_read_string(fan_node, "apple,fan-mode", &mode)) {
		ret = macsmc_hwmon_parse_key(dev, smc, &fan->mode, mode);
		if (ret)
			return ret;
	}

	/* Initialise fan control mode to automatic */
	fan->manual = false;

	return 0;
}

static int macsmc_hwmon_populate_sensors(struct macsmc_hwmon *hwmon,
					 struct device_node *hwmon_node)
{
	struct device_node *key_node __maybe_unused;
	struct macsmc_hwmon_sensor *sensor;
	u32 n_current = 0, n_fan = 0, n_power = 0, n_temperature = 0, n_voltage = 0;

	for_each_child_of_node_with_prefix(hwmon_node, key_node, "current-") {
		n_current++;
	}

	if (n_current) {
		hwmon->curr.sensors = devm_kcalloc(hwmon->dev, n_current,
						   sizeof(struct macsmc_hwmon_sensor), GFP_KERNEL);
		if (!hwmon->curr.sensors)
			return -ENOMEM;

		for_each_child_of_node_with_prefix(hwmon_node, key_node, "current-") {
			sensor = &hwmon->curr.sensors[hwmon->curr.count];
			if (!macsmc_hwmon_create_sensor(hwmon->dev, hwmon->smc, key_node, sensor)) {
				sensor->attrs = HWMON_C_INPUT;

				if (*sensor->label)
					sensor->attrs |= HWMON_C_LABEL;

				hwmon->curr.count++;
			}
		}
	}

	for_each_child_of_node_with_prefix(hwmon_node, key_node, "fan-") {
		n_fan++;
	}

	if (n_fan) {
		hwmon->fan.fans = devm_kcalloc(hwmon->dev, n_fan,
					       sizeof(struct macsmc_hwmon_fan), GFP_KERNEL);
		if (!hwmon->fan.fans)
			return -ENOMEM;

		for_each_child_of_node_with_prefix(hwmon_node, key_node, "fan-") {
			if (!macsmc_hwmon_create_fan(hwmon->dev, hwmon->smc, key_node,
						     &hwmon->fan.fans[hwmon->fan.count]))
				hwmon->fan.count++;
		}
	}

	for_each_child_of_node_with_prefix(hwmon_node, key_node, "power-") {
		n_power++;
	}

	if (n_power) {
		hwmon->power.sensors = devm_kcalloc(hwmon->dev, n_power,
						    sizeof(struct macsmc_hwmon_sensor), GFP_KERNEL);
		if (!hwmon->power.sensors)
			return -ENOMEM;

		for_each_child_of_node_with_prefix(hwmon_node, key_node, "power-") {
			sensor = &hwmon->power.sensors[hwmon->power.count];
			if (!macsmc_hwmon_create_sensor(hwmon->dev, hwmon->smc, key_node, sensor)) {
				sensor->attrs = HWMON_P_INPUT;

				if (*sensor->label)
					sensor->attrs |= HWMON_P_LABEL;

				hwmon->power.count++;
			}
		}
	}

	for_each_child_of_node_with_prefix(hwmon_node, key_node, "temperature-") {
		n_temperature++;
	}

	if (n_temperature) {
		hwmon->temp.sensors = devm_kcalloc(hwmon->dev, n_temperature,
						   sizeof(struct macsmc_hwmon_sensor), GFP_KERNEL);
		if (!hwmon->temp.sensors)
			return -ENOMEM;

		for_each_child_of_node_with_prefix(hwmon_node, key_node, "temperature-") {
			sensor = &hwmon->temp.sensors[hwmon->temp.count];
			if (!macsmc_hwmon_create_sensor(hwmon->dev, hwmon->smc, key_node, sensor)) {
				sensor->attrs = HWMON_T_INPUT;

				if (*sensor->label)
					sensor->attrs |= HWMON_T_LABEL;

				hwmon->temp.count++;
			}
		}
	}

	for_each_child_of_node_with_prefix(hwmon_node, key_node, "voltage-") {
		n_voltage++;
	}

	if (n_voltage) {
		hwmon->volt.sensors = devm_kcalloc(hwmon->dev, n_voltage,
						   sizeof(struct macsmc_hwmon_sensor), GFP_KERNEL);
		if (!hwmon->volt.sensors)
			return -ENOMEM;

		for_each_child_of_node_with_prefix(hwmon_node, key_node, "volt-") {
			sensor = &hwmon->temp.sensors[hwmon->temp.count];
			if (!macsmc_hwmon_create_sensor(hwmon->dev, hwmon->smc, key_node, sensor)) {
				sensor->attrs = HWMON_I_INPUT;

				if (*sensor->label)
					sensor->attrs |= HWMON_I_LABEL;

				hwmon->volt.count++;
			}
		}
	}

	return 0;
}

/* Create NULL-terminated config arrays */
static void macsmc_hwmon_populate_configs(u32 *configs, const struct macsmc_hwmon_sensors *sensors)
{
	int idx;

	for (idx = 0; idx < sensors->count; idx++)
		configs[idx] = sensors->sensors[idx].attrs;
}

static void macsmc_hwmon_populate_fan_configs(u32 *configs, const struct macsmc_hwmon_fans *fans)
{
	int idx;

	for (idx = 0; idx < fans->count; idx++)
		configs[idx] = fans->fans[idx].attrs;
}

static const struct hwmon_channel_info *const macsmc_chip_channel_info =
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ);

static int macsmc_hwmon_create_infos(struct macsmc_hwmon *hwmon)
{
	struct hwmon_channel_info *channel_info;
	int i = 0;

	/* chip */
	hwmon->channel_infos[i++] = macsmc_chip_channel_info;

	if (hwmon->curr.count) {
		channel_info = &hwmon->curr.channel_info;
		channel_info->type = hwmon_curr;
		channel_info->config = devm_kcalloc(hwmon->dev, hwmon->curr.count + 1,
						    sizeof(u32), GFP_KERNEL);
		if (!channel_info->config)
			return -ENOMEM;

		macsmc_hwmon_populate_configs((u32 *)channel_info->config, &hwmon->curr);
		hwmon->channel_infos[i++] = channel_info;
	}

	if (hwmon->fan.count) {
		channel_info = &hwmon->fan.channel_info;
		channel_info->type = hwmon_fan;
		channel_info->config = devm_kcalloc(hwmon->dev, hwmon->fan.count + 1,
						    sizeof(u32), GFP_KERNEL);
		if (!channel_info->config)
			return -ENOMEM;

		macsmc_hwmon_populate_fan_configs((u32 *)channel_info->config, &hwmon->fan);
		hwmon->channel_infos[i++] = channel_info;
	}

	if (hwmon->power.count) {
		channel_info = &hwmon->power.channel_info;
		channel_info->type = hwmon_power;
		channel_info->config = devm_kcalloc(hwmon->dev, hwmon->power.count + 1,
						    sizeof(u32), GFP_KERNEL);
		if (!channel_info->config)
			return -ENOMEM;

		macsmc_hwmon_populate_configs((u32 *)channel_info->config, &hwmon->power);
		hwmon->channel_infos[i++] = channel_info;
	}

	if (hwmon->temp.count) {
		channel_info = &hwmon->temp.channel_info;
		channel_info->type = hwmon_temp;
		channel_info->config = devm_kcalloc(hwmon->dev, hwmon->temp.count + 1,
						    sizeof(u32), GFP_KERNEL);
		if (!channel_info->config)
			return -ENOMEM;

		macsmc_hwmon_populate_configs((u32 *)channel_info->config, &hwmon->temp);
		hwmon->channel_infos[i++] = channel_info;
	}

	if (hwmon->volt.count) {
		channel_info = &hwmon->volt.channel_info;
		channel_info->type = hwmon_in;
		channel_info->config = devm_kcalloc(hwmon->dev, hwmon->volt.count + 1,
						    sizeof(u32), GFP_KERNEL);
		if (!channel_info->config)
			return -ENOMEM;

		macsmc_hwmon_populate_configs((u32 *)channel_info->config, &hwmon->volt);
		hwmon->channel_infos[i++] = channel_info;
	}

	return 0;
}

static int macsmc_hwmon_probe(struct platform_device *pdev)
{
	struct apple_smc *smc = dev_get_drvdata(pdev->dev.parent);
	struct macsmc_hwmon *hwmon;
	int ret;

	/*
	 * The MFD driver will try to probe us unconditionally. Some devices
	 * with the SMC do not have hwmon capabilities. Only probe if we have
	 * a hwmon node.
	 */
	if (!pdev->dev.of_node)
		return -ENODEV;

	hwmon = devm_kzalloc(&pdev->dev, sizeof(*hwmon),
			     GFP_KERNEL);
	if (!hwmon)
		return -ENOMEM;

	hwmon->dev = &pdev->dev;
	hwmon->smc = smc;

	ret = macsmc_hwmon_populate_sensors(hwmon, hwmon->dev->of_node);
	if (ret) {
		dev_err(hwmon->dev, "Could not parse sensors\n");
		return ret;
	}

	if (!hwmon->curr.count && !hwmon->fan.count &&
	    !hwmon->power.count && !hwmon->temp.count &&
	    !hwmon->volt.count) {
		dev_err(hwmon->dev,
			"No valid sensors found of any supported type\n");
		return -ENODEV;
	}

	ret = macsmc_hwmon_create_infos(hwmon);
	if (ret)
		return ret;

	hwmon->chip_info.ops = &macsmc_hwmon_ops;
	hwmon->chip_info.info =
		(const struct hwmon_channel_info *const *)&hwmon->channel_infos;

	hwmon->hwmon_dev = devm_hwmon_device_register_with_info(&pdev->dev,
								"macsmc_hwmon", hwmon,
								&hwmon->chip_info, NULL);
	if (IS_ERR(hwmon->hwmon_dev))
		return dev_err_probe(hwmon->dev, PTR_ERR(hwmon->hwmon_dev),
				     "Probing SMC hwmon device failed\n");

	dev_dbg(hwmon->dev, "Registered SMC hwmon device. Sensors:\n");
	dev_dbg(hwmon->dev,
		"Current: %d, Fans: %d, Power: %d, Temperature: %d, Voltage: %d",
		hwmon->curr.count, hwmon->fan.count,
		hwmon->power.count, hwmon->temp.count,
		hwmon->volt.count);

	return 0;
}

static const struct of_device_id macsmc_hwmon_of_table[] = {
	{ .compatible = "apple,smc-hwmon" },
	{}
};
MODULE_DEVICE_TABLE(of, macsmc_hwmon_of_table);

static struct platform_driver macsmc_hwmon_driver = {
	.probe = macsmc_hwmon_probe,
	.driver = {
		.name = "macsmc-hwmon",
		.of_match_table = macsmc_hwmon_of_table,
	},
};
module_platform_driver(macsmc_hwmon_driver);

MODULE_DESCRIPTION("Apple Silicon SMC hwmon driver");
MODULE_AUTHOR("James Calligeros <jcalligeros99@gmail.com>");
MODULE_LICENSE("Dual MIT/GPL");
