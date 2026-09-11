// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * cgbc-hwmon - Congatec Board Controller hardware monitoring driver
 *
 * Copyright (C) 2024 Thomas Richard <thomas.richard@bootlin.com>
 */

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/hwmon.h>
#include <linux/mfd/cgbc.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define CGBC_HWMON_CMD_SENSOR		0x77
#define CGBC_HWMON_CMD_SENSOR_DATA_SIZE	0x05

#define CGBC_HWMON_TYPE_MASK	GENMASK(6, 5)
#define CGBC_HWMON_ID_MASK	GENMASK(4, 0)
#define CGBC_HWMON_ACTIVE_BIT	BIT(7)

struct cgbc_hwmon_sensor {
	enum hwmon_sensor_types type;
	bool active;
	unsigned int index;
	unsigned int channel;
	const char *label;
};

struct cgbc_hwmon_data {
	struct cgbc_device_data *cgbc;
	unsigned int nb_sensors;
	struct cgbc_hwmon_sensor *sensors;
};

enum cgbc_sensor_types {
	CGBC_HWMON_TYPE_TEMP = 1,
	CGBC_HWMON_TYPE_IN,
	CGBC_HWMON_TYPE_FAN
};

static const char * const cgbc_hwmon_labels_temp[] = {
	"CPU Temperature",
	"Case Temperature",
	"Ambient Temperature",
	"CPU Board Temperature",
	"Carrier Board Temperature",
	"System Chipset Temperature",
	"Video Controller/Board Temperature",
	"Other Temperature",
	"Top DIMM 0 Temperature",
	"Bottom DIMM 0 Temperature",
	"Alternate Board Temperature",
	"Top DIMM 1 Temperature",
	"Top DIMM 2 Temperature",
	"Top DIMM 3 Temperature",
	"Top DIMM 4 Temperature",
	"Top DIMM 5 Temperature",
	"Top DIMM 6 Temperature",
	"Top DIMM 7 Temperature",
	"Bottom DIMM 1 Temperature",
};

static const char * const cgbc_hwmon_labels_in[] = {
	"CPU Core Voltage",
	"DC Runtime Voltage",
	"DC Standby Voltage",
	"CMOS Battery Voltage",
	"Battery Supply Voltage",
	"AC Voltage",
	"Other Voltage",
	"5V Runtime Voltage",
	"5V Standby Voltage",
	"3V3 Runtime Voltage",
	"3V3 Standby Voltage",
	"VCore A Voltage",
	"VCore B Voltage",
	"12V Runtime Voltage",
	"12V Standby Voltage",
};

/*
 * Current sensors are a bit special, they don't have consecutive IDs like
 * other types of sensors. So they need to be defined explicitly.
 */
static const struct {
	const char *label;
	int id;
} cgbc_hwmon_labels_curr[] = {
	{ "DC Runtime Current", 0x12 },
	{ "5V Runtime Current", 0x18 },
	{ "12V Runtime Current", 0x1E },
};

static const char * const cgbc_hwmon_labels_fan[] = {
	"CPU Fan",
	"Case Fan",
	"Ambient Fan",
	"Chipset Fan",
	"Video Fan",
	"Other Fan",
};

static int cgbc_hwmon_cmd(struct cgbc_device_data *cgbc, u8 index, u8 *data)
{
	u8 cmd[2] = {CGBC_HWMON_CMD_SENSOR, index};

	return cgbc_command(cgbc, cmd, sizeof(cmd), data, CGBC_HWMON_CMD_SENSOR_DATA_SIZE, NULL);
}

static int cgbc_hwmon_probe_sensors(struct device *dev, struct cgbc_hwmon_data *hwmon)
{
	struct cgbc_device_data *cgbc = hwmon->cgbc;
	struct cgbc_hwmon_sensor *sensor = hwmon->sensors;
	u8 data[CGBC_HWMON_CMD_SENSOR_DATA_SIZE], nb_sensors, i;
	int ret;

	ret = cgbc_hwmon_cmd(cgbc, 0, &data[0]);
	if (ret)
		return ret;

	nb_sensors = data[0];

	hwmon->sensors = devm_kzalloc(dev, sizeof(*hwmon->sensors) * nb_sensors, GFP_KERNEL);
	if (!hwmon->sensors)
		return -ENOMEM;

	sensor = hwmon->sensors;

	for (i = 0; i < nb_sensors; i++) {
		enum cgbc_sensor_types type;
		unsigned int channel, id;
		int j;

		/*
		 * No need to request data for the first sensor.
		 * We got data for the first sensor when we ask the number of sensors to the Board
		 * Controller.
		 */
		if (i) {
			ret = cgbc_hwmon_cmd(cgbc, i, &data[0]);
			if (ret)
				return ret;
		}

		type = FIELD_GET(CGBC_HWMON_TYPE_MASK, data[1]);
		id = FIELD_GET(CGBC_HWMON_ID_MASK, data[1]);
		channel = id - 1;

		if (type == CGBC_HWMON_TYPE_TEMP && channel < ARRAY_SIZE(cgbc_hwmon_labels_temp)) {
			sensor->type = hwmon_temp;
			sensor->label = cgbc_hwmon_labels_temp[channel];
		} else if (type == CGBC_HWMON_TYPE_IN) {
			/*
			 * The Board Controller doesn't differentiate current and voltage sensors.
			 * First check if it is a current sensor.
			 */
			for (j = 0; j < ARRAY_SIZE(cgbc_hwmon_labels_curr); j++) {
				if (id == cgbc_hwmon_labels_curr[j].id) {
					sensor->type = hwmon_curr;
					sensor->label = cgbc_hwmon_labels_curr[j].label;
					channel = j;
				}
			}

			/* If it's not a current sensor, it may be a voltage sensor. */
			if (!sensor->label && channel < ARRAY_SIZE(cgbc_hwmon_labels_in)) {
				sensor->type = hwmon_in;
				sensor->label = cgbc_hwmon_labels_in[channel];
			}
		} else if (type == CGBC_HWMON_TYPE_FAN &&
			   channel < ARRAY_SIZE(cgbc_hwmon_labels_fan)) {
			sensor->type = hwmon_fan;
			sensor->label = cgbc_hwmon_labels_fan[channel];
		}

		if (!sensor->label) {
			dev_warn(dev, "Board Controller returned an unknown sensor (bc_type=%d, bc_id=%d), ignore it",
				 type, id);
			continue;
		}

		sensor->active = FIELD_GET(CGBC_HWMON_ACTIVE_BIT, data[1]);
		sensor->channel = channel;
		sensor->index = i;

		dev_dbg(dev, "Found sensor: bc_type=%d, bc_id=%d, hwmon_type=%d, hwmon_channel=%d, hwmon_label='%s', active=%d\n",
			type, id, sensor->type, sensor->channel, sensor->label, sensor->active);

		sensor++;
		hwmon->nb_sensors++;
	}

	return 0;
}

static struct cgbc_hwmon_sensor *cgbc_hwmon_find_sensor(struct cgbc_hwmon_data *hwmon,
							enum hwmon_sensor_types type, int channel)
{
	struct cgbc_hwmon_sensor *sensor = NULL;
	int i;

	for (i = 0; i < hwmon->nb_sensors; i++) {
		if (hwmon->sensors[i].type == type && hwmon->sensors[i].channel == channel) {
			sensor = &hwmon->sensors[i];
			break;
		}
	}

	return sensor;
}

static int cgbc_hwmon_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
			   long *val)
{
	struct cgbc_hwmon_data *hwmon = dev_get_drvdata(dev);
	struct cgbc_hwmon_sensor *sensor = cgbc_hwmon_find_sensor(hwmon, type, channel);
	struct cgbc_device_data *cgbc = hwmon->cgbc;
	u8 data[CGBC_HWMON_CMD_SENSOR_DATA_SIZE];
	int ret;

	ret = cgbc_hwmon_cmd(cgbc, sensor->index, &data[0]);
	if (ret)
		return ret;

	*val = (data[3] << 8) | data[2];

	/*
	 * For the Board Controller 1lsb = 0.1 degree centigrade.
	 * Other units are as expected.
	 */
	if (sensor->type == hwmon_temp)
		*val *= 100;

	return 0;
}

static umode_t cgbc_hwmon_is_visible(const void *_data, enum hwmon_sensor_types type, u32 attr,
				     int channel)
{
	struct cgbc_hwmon_data *data = (struct cgbc_hwmon_data *)_data;
	struct cgbc_hwmon_sensor *sensor;

	sensor = cgbc_hwmon_find_sensor(data, type, channel);
	if (!sensor)
		return 0;

	return sensor->active ? 0444 : 0;
}

static int cgbc_hwmon_read_string(struct device *dev, enum hwmon_sensor_types type, u32 attr,
				  int channel, const char **str)
{
	struct cgbc_hwmon_data *hwmon = dev_get_drvdata(dev);
	struct cgbc_hwmon_sensor *sensor = cgbc_hwmon_find_sensor(hwmon, type, channel);

	*str = sensor->label;

	return 0;
}

static const struct hwmon_channel_info * const cgbc_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL, HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL, HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL, HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL, HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL, HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL, HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL, HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL, HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL, HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_LABEL, HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL, HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL, HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL, HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL, HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL, HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL, HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(curr,
			   HWMON_C_INPUT | HWMON_C_LABEL, HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_LABEL, HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL, HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL, HWMON_F_INPUT | HWMON_F_LABEL),
	NULL
};

static const struct hwmon_ops cgbc_hwmon_ops = {
	.is_visible = cgbc_hwmon_is_visible,
	.read = cgbc_hwmon_read,
	.read_string = cgbc_hwmon_read_string,
};

static const struct hwmon_chip_info cgbc_chip_info = {
	.ops = &cgbc_hwmon_ops,
	.info = cgbc_hwmon_info,
};

static int cgbc_hwmon_probe(struct platform_device *pdev)
{
	struct cgbc_device_data *cgbc = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct cgbc_hwmon_data *data;
	struct device *hwmon_dev;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->cgbc = cgbc;

	ret = cgbc_hwmon_probe_sensors(dev, data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to probe sensors");

	hwmon_dev = devm_hwmon_device_register_with_info(dev, "cgbc_hwmon", data, &cgbc_chip_info,
							 NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static struct platform_driver cgbc_hwmon_driver = {
	.driver = {
		.name = "cgbc-hwmon",
	},
	.probe = cgbc_hwmon_probe,
};

module_platform_driver(cgbc_hwmon_driver);

MODULE_AUTHOR("Thomas Richard <thomas.richard@bootlin.com>");
MODULE_DESCRIPTION("Congatec Board Controller Hardware Monitoring Driver");
MODULE_LICENSE("GPL");
