// SPDX-License-Identifier: GPL-2.0+
/*
 * HWMON driver for Lenovo ThinkStation based workstations
 * via the embedded controller registers
 *
 * Copyright (C) 2024 David Ober (Lenovo) <dober@lenovo.com>
 *
 * EC provides:
 * - CPU temperature
 * - DIMM temperature
 * - Chassis zone temperatures
 * - CPU fan RPM
 * - DIMM fan RPM
 * - Chassis fans RPM
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/units.h>

#define MCHP_SING_IDX			0x0000
#define MCHP_EMI0_APPLICATION_ID	0x090C
#define MCHP_EMI0_EC_ADDRESS		0x0902
#define MCHP_EMI0_EC_DATA_BYTE0		0x0904
#define MCHP_EMI0_EC_DATA_BYTE1		0x0905
#define MCHP_EMI0_EC_DATA_BYTE2		0x0906
#define MCHP_EMI0_EC_DATA_BYTE3		0x0907
#define IO_REGION_START			0x0900
#define IO_REGION_LENGTH		0xD

static inline u8
get_ec_reg(unsigned char page, unsigned char index)
{
	u8 onebyte;
	unsigned short m_index;
	unsigned short phy_index = page * 256 + index;

	outb_p(0x01, MCHP_EMI0_APPLICATION_ID);

	m_index = phy_index & GENMASK(14, 2);
	outw_p(m_index, MCHP_EMI0_EC_ADDRESS);

	onebyte = inb_p(MCHP_EMI0_EC_DATA_BYTE0 + (phy_index & GENMASK(1, 0)));

	outb_p(0x01, MCHP_EMI0_APPLICATION_ID);  /* write 0x01 again to clean */
	return onebyte;
}

enum systems {
	LENOVO_PX,
	LENOVO_P7,
	LENOVO_P5,
	LENOVO_P8,
};

static int px_temp_map[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

static const char * const lenovo_px_ec_temp_label[] = {
	"CPU1",
	"CPU2",
	"R_DIMM1",
	"L_DIMM1",
	"R_DIMM2",
	"L_DIMM2",
	"PCH",
	"M2_R",
	"M2_Z1R",
	"M2_Z2R",
	"PCI_Z1",
	"PCI_Z2",
	"PCI_Z3",
	"PCI_Z4",
	"AMB",
};

static int gen_temp_map[] = {0, 2, 3, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

static const char * const lenovo_gen_ec_temp_label[] = {
	"CPU1",
	"R_DIMM",
	"L_DIMM",
	"PCH",
	"M2_R",
	"M2_Z1R",
	"M2_Z2R",
	"PCI_Z1",
	"PCI_Z2",
	"PCI_Z3",
	"PCI_Z4",
	"AMB",
};

static int px_fan_map[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

static const char * const px_ec_fan_label[] = {
	"CPU1_Fan",
	"CPU2_Fan",
	"Front_Fan1-1",
	"Front_Fan1-2",
	"Front_Fan2",
	"Front_Fan3",
	"MEM_Fan1",
	"MEM_Fan2",
	"Rear_Fan1",
	"Rear_Fan2",
	"Flex_Bay_Fan1",
	"Flex_Bay_Fan2",
	"Flex_Bay_Fan2",
	"PSU_HDD_Fan",
	"PSU1_Fan",
	"PSU2_Fan",
};

static int p7_fan_map[] = {0, 2, 3, 4, 5, 6, 7, 8, 10, 11, 14};

static const char * const p7_ec_fan_label[] = {
	"CPU1_Fan",
	"HP_CPU_Fan1",
	"HP_CPU_Fan2",
	"PCIE1_4_Fan",
	"PCIE5_7_Fan",
	"MEM_Fan1",
	"MEM_Fan2",
	"Rear_Fan1",
	"BCB_Fan",
	"Flex_Bay_Fan",
	"PSU_Fan",
};

static int p5_fan_map[] = {0, 5, 6, 7, 8, 10, 11, 14};

static const char * const p5_ec_fan_label[] = {
	"CPU_Fan",
	"HDD_Fan",
	"Duct_Fan1",
	"MEM_Fan",
	"Rear_Fan",
	"Front_Fan",
	"Flex_Bay_Fan",
	"PSU_Fan",
};

static int p8_fan_map[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14};

static const char * const p8_ec_fan_label[] = {
	"CPU1_Fan",
	"CPU2_Fan",
	"HP_CPU_Fan1",
	"HP_CPU_Fan2",
	"PCIE1_4_Fan",
	"PCIE5_7_Fan",
	"DIMM1_Fan1",
	"DIMM1_Fan2",
	"DIMM2_Fan1",
	"DIMM2_Fan2",
	"Rear_Fan",
	"HDD_Bay_Fan",
	"Flex_Bay_Fan",
	"PSU_Fan",
};

struct ec_sensors_data {
	struct mutex mec_mutex; /* lock for sensor data access */
	const char *const *fan_labels;
	const char *const *temp_labels;
	const int *fan_map;
	const int *temp_map;
};

static int
lenovo_ec_do_read_temp(struct ec_sensors_data *data, u32 attr, int channel, long *val)
{
	u8 lsb;

	switch (attr) {
	case hwmon_temp_input:
		mutex_lock(&data->mec_mutex);
		lsb = get_ec_reg(2, 0x81 + channel);
		mutex_unlock(&data->mec_mutex);
		if (lsb <= 0x40)
			return -ENODATA;
		*val = (lsb - 0x40) * 1000;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int
lenovo_ec_do_read_fan(struct ec_sensors_data *data, u32 attr, int channel, long *val)
{
	u8 lsb, msb;

	channel *= 2;
	switch (attr) {
	case hwmon_fan_input:
		mutex_lock(&data->mec_mutex);
		lsb = get_ec_reg(4, 0x20 + channel);
		msb = get_ec_reg(4, 0x21 + channel);
		mutex_unlock(&data->mec_mutex);
		*val = (msb << 8) + lsb;
		return 0;
	case hwmon_fan_max:
		mutex_lock(&data->mec_mutex);
		lsb = get_ec_reg(4, 0x40 + channel);
		msb = get_ec_reg(4, 0x41 + channel);
		mutex_unlock(&data->mec_mutex);
		*val = (msb << 8) + lsb;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int
lenovo_ec_hwmon_read_string(struct device *dev, enum hwmon_sensor_types type,
			    u32 attr, int channel, const char **str)
{
	struct ec_sensors_data *state = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		*str = state->temp_labels[channel];
		return 0;
	case hwmon_fan:
		*str = state->fan_labels[channel];
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int
lenovo_ec_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
		     u32 attr, int channel, long *val)
{
	struct ec_sensors_data *data = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		return lenovo_ec_do_read_temp(data, attr, data->temp_map[channel], val);
	case hwmon_fan:
		return lenovo_ec_do_read_fan(data, attr, data->fan_map[channel], val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t
lenovo_ec_hwmon_is_visible(const void *data, enum hwmon_sensor_types type,
			   u32 attr, int channel)
{
	switch (type) {
	case hwmon_temp:
		if (attr == hwmon_temp_input || attr == hwmon_temp_label)
			return 0444;
		return 0;
	case hwmon_fan:
		if (attr == hwmon_fan_input || attr == hwmon_fan_max || attr == hwmon_fan_label)
			return 0444;
		return 0;
	default:
		return 0;
	}
}

static const struct hwmon_channel_info *lenovo_ec_hwmon_info_px[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX),
	NULL
};

static const struct hwmon_channel_info *lenovo_ec_hwmon_info_p8[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX),
	NULL
};

static const struct hwmon_channel_info *lenovo_ec_hwmon_info_p7[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX),
	NULL
};

static const struct hwmon_channel_info *lenovo_ec_hwmon_info_p5[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MAX),
	NULL
};

static const struct hwmon_ops lenovo_ec_hwmon_ops = {
	.is_visible = lenovo_ec_hwmon_is_visible,
	.read = lenovo_ec_hwmon_read,
	.read_string = lenovo_ec_hwmon_read_string,
};

static struct hwmon_chip_info lenovo_ec_chip_info = {
	.ops = &lenovo_ec_hwmon_ops,
};

static const struct dmi_system_id thinkstation_dmi_table[] = {
	{
		.ident = "LENOVO_PX",
		.driver_data = (void *)(long)LENOVO_PX,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "30EU"),
		},
	},
	{
		.ident = "LENOVO_PX",
		.driver_data = (void *)(long)LENOVO_PX,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "30EV"),
		},
	},
	{
		.ident = "LENOVO_P7",
		.driver_data = (void *)(long)LENOVO_P7,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "30F2"),
		},
	},
	{
		.ident = "LENOVO_P7",
		.driver_data = (void *)(long)LENOVO_P7,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "30F3"),
		},
	},
	{
		.ident = "LENOVO_P5",
		.driver_data = (void *)(long)LENOVO_P5,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "30G9"),
		},
	},
	{
		.ident = "LENOVO_P5",
		.driver_data = (void *)(long)LENOVO_P5,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "30GA"),
		},
	},
	{
		.ident = "LENOVO_P8",
		.driver_data = (void *)(long)LENOVO_P8,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "30HH"),
		},
	},
	{
		.ident = "LENOVO_P8",
		.driver_data = (void *)(long)LENOVO_P8,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "30HJ"),
		},
	},
	{}
};
MODULE_DEVICE_TABLE(dmi, thinkstation_dmi_table);

static int lenovo_ec_probe(struct platform_device *pdev)
{
	struct device *hwdev;
	struct ec_sensors_data *ec_data;
	const struct hwmon_chip_info *chip_info;
	struct device *dev = &pdev->dev;
	const struct dmi_system_id *dmi_id;
	int app_id;

	ec_data = devm_kzalloc(dev, sizeof(struct ec_sensors_data), GFP_KERNEL);
	if (!ec_data)
		return -ENOMEM;

	if (!request_region(IO_REGION_START, IO_REGION_LENGTH, "LNV-WKS")) {
		pr_err(":request fail\n");
		return -EIO;
	}

	dev_set_drvdata(dev, ec_data);

	chip_info = &lenovo_ec_chip_info;

	mutex_init(&ec_data->mec_mutex);

	mutex_lock(&ec_data->mec_mutex);
	app_id = inb_p(MCHP_EMI0_APPLICATION_ID);
	if (app_id) /* check EMI Application ID Value */
		outb_p(app_id, MCHP_EMI0_APPLICATION_ID); /* set EMI Application ID to 0 */
	outw_p(MCHP_SING_IDX, MCHP_EMI0_EC_ADDRESS);
	mutex_unlock(&ec_data->mec_mutex);

	if ((inb_p(MCHP_EMI0_EC_DATA_BYTE0) != 'M') &&
	    (inb_p(MCHP_EMI0_EC_DATA_BYTE1) != 'C') &&
	    (inb_p(MCHP_EMI0_EC_DATA_BYTE2) != 'H') &&
	    (inb_p(MCHP_EMI0_EC_DATA_BYTE3) != 'P')) {
		release_region(IO_REGION_START, IO_REGION_LENGTH);
		return -ENODEV;
	}

	dmi_id = dmi_first_match(thinkstation_dmi_table);

	switch ((long)dmi_id->driver_data) {
	case 0:
		ec_data->fan_labels = px_ec_fan_label;
		ec_data->temp_labels = lenovo_px_ec_temp_label;
		ec_data->fan_map = px_fan_map;
		ec_data->temp_map = px_temp_map;
		lenovo_ec_chip_info.info = lenovo_ec_hwmon_info_px;
		break;
	case 1:
		ec_data->fan_labels = p7_ec_fan_label;
		ec_data->temp_labels = lenovo_gen_ec_temp_label;
		ec_data->fan_map = p7_fan_map;
		ec_data->temp_map = gen_temp_map;
		lenovo_ec_chip_info.info = lenovo_ec_hwmon_info_p7;
		break;
	case 2:
		ec_data->fan_labels = p5_ec_fan_label;
		ec_data->temp_labels = lenovo_gen_ec_temp_label;
		ec_data->fan_map = p5_fan_map;
		ec_data->temp_map = gen_temp_map;
		lenovo_ec_chip_info.info = lenovo_ec_hwmon_info_p5;
		break;
	case 3:
		ec_data->fan_labels = p8_ec_fan_label;
		ec_data->temp_labels = lenovo_gen_ec_temp_label;
		ec_data->fan_map = p8_fan_map;
		ec_data->temp_map = gen_temp_map;
		lenovo_ec_chip_info.info = lenovo_ec_hwmon_info_p8;
		break;
	default:
		release_region(IO_REGION_START, IO_REGION_LENGTH);
		return -ENODEV;
	}

	hwdev = devm_hwmon_device_register_with_info(dev, "lenovo_ec",
						     ec_data,
						     chip_info, NULL);

	return PTR_ERR_OR_ZERO(hwdev);
}

static struct platform_driver lenovo_ec_sensors_platform_driver = {
	.driver = {
		.name	= "lenovo-ec-sensors",
	},
	.probe = lenovo_ec_probe,
};

static struct platform_device *lenovo_ec_sensors_platform_device;

static int __init lenovo_ec_init(void)
{
	if (!dmi_check_system(thinkstation_dmi_table))
		return -ENODEV;

	lenovo_ec_sensors_platform_device =
		platform_create_bundle(&lenovo_ec_sensors_platform_driver,
				       lenovo_ec_probe, NULL, 0, NULL, 0);

	if (IS_ERR(lenovo_ec_sensors_platform_device)) {
		release_region(IO_REGION_START, IO_REGION_LENGTH);
		return PTR_ERR(lenovo_ec_sensors_platform_device);
	}

	return 0;
}
module_init(lenovo_ec_init);

static void __exit lenovo_ec_exit(void)
{
	release_region(IO_REGION_START, IO_REGION_LENGTH);
	platform_device_unregister(lenovo_ec_sensors_platform_device);
	platform_driver_unregister(&lenovo_ec_sensors_platform_driver);
}
module_exit(lenovo_ec_exit);

MODULE_AUTHOR("David Ober <dober@lenovo.com>");
MODULE_DESCRIPTION("HWMON driver for sensors accessible via EC in LENOVO motherboards");
MODULE_LICENSE("GPL");
