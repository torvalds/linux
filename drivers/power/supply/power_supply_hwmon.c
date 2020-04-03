// SPDX-License-Identifier: GPL-2.0
/*
 *  power_supply_hwmon.c - power supply hwmon support.
 */

#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/power_supply.h>
#include <linux/slab.h>

struct power_supply_hwmon {
	struct power_supply *psy;
	unsigned long *props;
};

static int power_supply_hwmon_in_to_property(u32 attr)
{
	switch (attr) {
	case hwmon_in_average:
		return POWER_SUPPLY_PROP_VOLTAGE_AVG;
	case hwmon_in_min:
		return POWER_SUPPLY_PROP_VOLTAGE_MIN;
	case hwmon_in_max:
		return POWER_SUPPLY_PROP_VOLTAGE_MAX;
	case hwmon_in_input:
		return POWER_SUPPLY_PROP_VOLTAGE_NOW;
	default:
		return -EINVAL;
	}
}

static int power_supply_hwmon_curr_to_property(u32 attr)
{
	switch (attr) {
	case hwmon_curr_average:
		return POWER_SUPPLY_PROP_CURRENT_AVG;
	case hwmon_curr_max:
		return POWER_SUPPLY_PROP_CURRENT_MAX;
	case hwmon_curr_input:
		return POWER_SUPPLY_PROP_CURRENT_NOW;
	default:
		return -EINVAL;
	}
}

static int power_supply_hwmon_temp_to_property(u32 attr, int channel)
{
	if (channel) {
		switch (attr) {
		case hwmon_temp_input:
			return POWER_SUPPLY_PROP_TEMP_AMBIENT;
		case hwmon_temp_min_alarm:
			return POWER_SUPPLY_PROP_TEMP_AMBIENT_ALERT_MIN;
		case hwmon_temp_max_alarm:
			return POWER_SUPPLY_PROP_TEMP_AMBIENT_ALERT_MAX;
		default:
			break;
		}
	} else {
		switch (attr) {
		case hwmon_temp_input:
			return POWER_SUPPLY_PROP_TEMP;
		case hwmon_temp_max:
			return POWER_SUPPLY_PROP_TEMP_MAX;
		case hwmon_temp_min:
			return POWER_SUPPLY_PROP_TEMP_MIN;
		case hwmon_temp_min_alarm:
			return POWER_SUPPLY_PROP_TEMP_ALERT_MIN;
		case hwmon_temp_max_alarm:
			return POWER_SUPPLY_PROP_TEMP_ALERT_MAX;
		default:
			break;
		}
	}

	return -EINVAL;
}

static int
power_supply_hwmon_to_property(enum hwmon_sensor_types type,
			       u32 attr, int channel)
{
	switch (type) {
	case hwmon_in:
		return power_supply_hwmon_in_to_property(attr);
	case hwmon_curr:
		return power_supply_hwmon_curr_to_property(attr);
	case hwmon_temp:
		return power_supply_hwmon_temp_to_property(attr, channel);
	default:
		return -EINVAL;
	}
}

static bool power_supply_hwmon_is_a_label(enum hwmon_sensor_types type,
					   u32 attr)
{
	return type == hwmon_temp && attr == hwmon_temp_label;
}

static bool power_supply_hwmon_is_writable(enum hwmon_sensor_types type,
					   u32 attr)
{
	switch (type) {
	case hwmon_in:
		return attr == hwmon_in_min ||
		       attr == hwmon_in_max;
	case hwmon_curr:
		return attr == hwmon_curr_max;
	case hwmon_temp:
		return attr == hwmon_temp_max ||
		       attr == hwmon_temp_min ||
		       attr == hwmon_temp_min_alarm ||
		       attr == hwmon_temp_max_alarm;
	default:
		return false;
	}
}

static umode_t power_supply_hwmon_is_visible(const void *data,
					     enum hwmon_sensor_types type,
					     u32 attr, int channel)
{
	const struct power_supply_hwmon *psyhw = data;
	int prop;


	if (power_supply_hwmon_is_a_label(type, attr))
		return 0444;

	prop = power_supply_hwmon_to_property(type, attr, channel);
	if (prop < 0 || !test_bit(prop, psyhw->props))
		return 0;

	if (power_supply_property_is_writeable(psyhw->psy, prop) > 0 &&
	    power_supply_hwmon_is_writable(type, attr))
		return 0644;

	return 0444;
}

static int power_supply_hwmon_read_string(struct device *dev,
					  enum hwmon_sensor_types type,
					  u32 attr, int channel,
					  const char **str)
{
	*str = channel ? "temp ambient" : "temp";
	return 0;
}

static int
power_supply_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct power_supply_hwmon *psyhw = dev_get_drvdata(dev);
	struct power_supply *psy = psyhw->psy;
	union power_supply_propval pspval;
	int ret, prop;

	prop = power_supply_hwmon_to_property(type, attr, channel);
	if (prop < 0)
		return prop;

	ret  = power_supply_get_property(psy, prop, &pspval);
	if (ret)
		return ret;

	switch (type) {
	/*
	 * Both voltage and current is reported in units of
	 * microvolts/microamps, so we need to adjust it to
	 * milliamps(volts)
	 */
	case hwmon_curr:
	case hwmon_in:
		pspval.intval = DIV_ROUND_CLOSEST(pspval.intval, 1000);
		break;
	/*
	 * Temp needs to be converted from 1/10 C to milli-C
	 */
	case hwmon_temp:
		if (check_mul_overflow(pspval.intval, 100,
				       &pspval.intval))
			return -EOVERFLOW;
		break;
	default:
		return -EINVAL;
	}

	*val = pspval.intval;

	return 0;
}

static int
power_supply_hwmon_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	struct power_supply_hwmon *psyhw = dev_get_drvdata(dev);
	struct power_supply *psy = psyhw->psy;
	union power_supply_propval pspval;
	int prop;

	prop = power_supply_hwmon_to_property(type, attr, channel);
	if (prop < 0)
		return prop;

	pspval.intval = val;

	switch (type) {
	/*
	 * Both voltage and current is reported in units of
	 * microvolts/microamps, so we need to adjust it to
	 * milliamps(volts)
	 */
	case hwmon_curr:
	case hwmon_in:
		if (check_mul_overflow(pspval.intval, 1000,
				       &pspval.intval))
			return -EOVERFLOW;
		break;
	/*
	 * Temp needs to be converted from 1/10 C to milli-C
	 */
	case hwmon_temp:
		pspval.intval = DIV_ROUND_CLOSEST(pspval.intval, 100);
		break;
	default:
		return -EINVAL;
	}

	return power_supply_set_property(psy, prop, &pspval);
}

static const struct hwmon_ops power_supply_hwmon_ops = {
	.is_visible	= power_supply_hwmon_is_visible,
	.read		= power_supply_hwmon_read,
	.write		= power_supply_hwmon_write,
	.read_string	= power_supply_hwmon_read_string,
};

static const struct hwmon_channel_info *power_supply_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_LABEL     |
			   HWMON_T_INPUT     |
			   HWMON_T_MAX       |
			   HWMON_T_MIN       |
			   HWMON_T_MIN_ALARM |
			   HWMON_T_MIN_ALARM,

			   HWMON_T_LABEL     |
			   HWMON_T_INPUT     |
			   HWMON_T_MIN_ALARM |
			   HWMON_T_LABEL     |
			   HWMON_T_MAX_ALARM),

	HWMON_CHANNEL_INFO(curr,
			   HWMON_C_AVERAGE |
			   HWMON_C_MAX     |
			   HWMON_C_INPUT),

	HWMON_CHANNEL_INFO(in,
			   HWMON_I_AVERAGE |
			   HWMON_I_MIN     |
			   HWMON_I_MAX     |
			   HWMON_I_INPUT),
	NULL
};

static const struct hwmon_chip_info power_supply_hwmon_chip_info = {
	.ops = &power_supply_hwmon_ops,
	.info = power_supply_hwmon_info,
};

static void power_supply_hwmon_bitmap_free(void *data)
{
	bitmap_free(data);
}

int power_supply_add_hwmon_sysfs(struct power_supply *psy)
{
	const struct power_supply_desc *desc = psy->desc;
	struct power_supply_hwmon *psyhw;
	struct device *dev = &psy->dev;
	struct device *hwmon;
	int ret, i;
	const char *name;

	if (!devres_open_group(dev, power_supply_add_hwmon_sysfs,
			       GFP_KERNEL))
		return -ENOMEM;

	psyhw = devm_kzalloc(dev, sizeof(*psyhw), GFP_KERNEL);
	if (!psyhw) {
		ret = -ENOMEM;
		goto error;
	}

	psyhw->psy = psy;
	psyhw->props = bitmap_zalloc(POWER_SUPPLY_PROP_TIME_TO_FULL_AVG + 1,
				     GFP_KERNEL);
	if (!psyhw->props) {
		ret = -ENOMEM;
		goto error;
	}

	ret = devm_add_action(dev, power_supply_hwmon_bitmap_free,
			      psyhw->props);
	if (ret)
		goto error;

	for (i = 0; i < desc->num_properties; i++) {
		const enum power_supply_property prop = desc->properties[i];

		switch (prop) {
		case POWER_SUPPLY_PROP_CURRENT_AVG:
		case POWER_SUPPLY_PROP_CURRENT_MAX:
		case POWER_SUPPLY_PROP_CURRENT_NOW:
		case POWER_SUPPLY_PROP_TEMP:
		case POWER_SUPPLY_PROP_TEMP_MAX:
		case POWER_SUPPLY_PROP_TEMP_MIN:
		case POWER_SUPPLY_PROP_TEMP_ALERT_MIN:
		case POWER_SUPPLY_PROP_TEMP_ALERT_MAX:
		case POWER_SUPPLY_PROP_TEMP_AMBIENT:
		case POWER_SUPPLY_PROP_TEMP_AMBIENT_ALERT_MIN:
		case POWER_SUPPLY_PROP_TEMP_AMBIENT_ALERT_MAX:
		case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			set_bit(prop, psyhw->props);
			break;
		default:
			break;
		}
	}

	name = psy->desc->name;
	if (strchr(name, '-')) {
		char *new_name;

		new_name = devm_kstrdup(dev, name, GFP_KERNEL);
		if (!new_name) {
			ret = -ENOMEM;
			goto error;
		}
		strreplace(new_name, '-', '_');
		name = new_name;
	}
	hwmon = devm_hwmon_device_register_with_info(dev, name,
						psyhw,
						&power_supply_hwmon_chip_info,
						NULL);
	ret = PTR_ERR_OR_ZERO(hwmon);
	if (ret)
		goto error;

	devres_close_group(dev, power_supply_add_hwmon_sysfs);
	return 0;
error:
	devres_release_group(dev, NULL);
	return ret;
}

void power_supply_remove_hwmon_sysfs(struct power_supply *psy)
{
	devres_release_group(&psy->dev, power_supply_add_hwmon_sysfs);
}
