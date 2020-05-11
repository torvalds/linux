// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	w1_therm.c
 *
 * Copyright (c) 2004 Evgeniy Polyakov <zbr@ioremap.net>
 */

#include <asm/types.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/hwmon.h>
#include <linux/string.h>

#include <linux/w1.h>

#define W1_THERM_DS18S20	0x10
#define W1_THERM_DS1822		0x22
#define W1_THERM_DS18B20	0x28
#define W1_THERM_DS1825		0x3B
#define W1_THERM_DS28EA00	0x42

/*
 * Allow the strong pullup to be disabled, but default to enabled.
 * If it was disabled a parasite powered device might not get the require
 * current to do a temperature conversion.  If it is enabled parasite powered
 * devices have a better chance of getting the current required.
 * In case the parasite power-detection is not working (seems to be the case
 * for some DS18S20) the strong pullup can also be forced, regardless of the
 * power state of the devices.
 *
 * Summary of options:
 * - strong_pullup = 0	Disable strong pullup completely
 * - strong_pullup = 1	Enable automatic strong pullup detection
 * - strong_pullup = 2	Force strong pullup
 */
static int w1_strong_pullup = 1;
module_param_named(strong_pullup, w1_strong_pullup, int, 0);

/* Counter for devices supporting bulk reading */
static u16 bulk_read_device_counter; /* =0 as per C standard */

/* This command should be in public header w1.h but is not */
#define W1_RECALL_EEPROM	0xB8

/* Nb of try for an operation */
#define W1_THERM_MAX_TRY		5

/* ms delay to retry bus mutex */
#define W1_THERM_RETRY_DELAY		20

/* delay in ms to write in EEPROM */
#define W1_THERM_EEPROM_WRITE_DELAY	10

#define EEPROM_CMD_WRITE    "save"	/* cmd for write eeprom sysfs */
#define EEPROM_CMD_READ     "restore"	/* cmd for read eeprom sysfs */
#define BULK_TRIGGER_CMD    "trigger"	/* cmd to trigger a bulk read */

#define MIN_TEMP	-55	/* min temperature that can be mesured */
#define MAX_TEMP	125	/* max temperature that can be mesured */

/* Helpers Macros */

/*
 * return a pointer on the slave w1_therm_family_converter struct:
 * always test family data existence before using this macro
 */
#define SLAVE_SPECIFIC_FUNC(sl) \
	(((struct w1_therm_family_data *)(sl->family_data))->specific_functions)

/*
 * return the power mode of the sl slave : 1-ext, 0-parasite, <0 unknown
 * always test family data existence before using this macro
 */
#define SLAVE_POWERMODE(sl) \
	(((struct w1_therm_family_data *)(sl->family_data))->external_powered)

/*
 * return the resolution in bit of the sl slave : <0 unknown
 * always test family data existence before using this macro
 */
#define SLAVE_RESOLUTION(sl) \
	(((struct w1_therm_family_data *)(sl->family_data))->resolution)

/*
 * return whether or not a converT command has been issued to the slave
 * * 0: no bulk read is pending
 * * -1: conversion is in progress
 * * 1: conversion done, result to be read
 */
#define SLAVE_CONVERT_TRIGGERED(sl) \
	(((struct w1_therm_family_data *)(sl->family_data))->convert_triggered)

/* return the address of the refcnt in the family data */
#define THERM_REFCNT(family_data) \
	(&((struct w1_therm_family_data *)family_data)->refcnt)

/* Structs definition */

/**
 * struct w1_therm_family_converter - bind device specific functions
 * @broken: flag for non-registred families
 * @reserved: not used here
 * @f: pointer to the device binding structure
 * @convert: pointer to the device conversion function
 * @get_conversion_time: pointer to the device conversion time function
 * @set_resolution: pointer to the device set_resolution function
 * @get_resolution: pointer to the device get_resolution function
 * @write_data: pointer to the device writing function (2 or 3 bytes)
 * @bulk_read: true if device family support bulk read, false otherwise
 */
struct w1_therm_family_converter {
	u8		broken;
	u16		reserved;
	struct w1_family	*f;
	int		(*convert)(u8 rom[9]);
	int		(*get_conversion_time)(struct w1_slave *sl);
	int		(*set_resolution)(struct w1_slave *sl, int val);
	int		(*get_resolution)(struct w1_slave *sl);
	int		(*write_data)(struct w1_slave *sl, const u8 *data);
	bool		bulk_read;
};

/**
 * struct w1_therm_family_data - device data
 * @rom: ROM device id (64bit Lasered ROM code + 1 CRC byte)
 * @refcnt: ref count
 * @external_powered:	1 device powered externally,
 *				0 device parasite powered,
 *				-x error or undefined
 * @resolution: current device resolution
 * @convert_triggered: conversion state of the device
 * @specific_functions: pointer to struct of device specific function
 */
struct w1_therm_family_data {
	uint8_t rom[9];
	atomic_t refcnt;
	int external_powered;
	int resolution;
	int convert_triggered;
	struct w1_therm_family_converter *specific_functions;
};

/**
 * struct therm_info - store temperature reading
 * @rom: read device data (8 data bytes + 1 CRC byte)
 * @crc: computed crc from rom
 * @verdict: 1 crc checked, 0 crc not matching
 */
struct therm_info {
	u8 rom[9];
	u8 crc;
	u8 verdict;
};

/* Hardware Functions declaration */

/**
 * reset_select_slave() - reset and select a slave
 * @sl: the slave to select
 *
 * Resets the bus and select the slave by sending a ROM MATCH cmd
 * w1_reset_select_slave() from w1_io.c could not be used here because
 * it sent a SKIP ROM command if only one device is on the line.
 * At the beginning of the such process, sl->master->slave_count is 1 even if
 * more devices are on the line, causing collision on the line.
 *
 * Context: The w1 master lock must be held.
 *
 * Return: 0 if success, negative kernel error code otherwise.
 */
static int reset_select_slave(struct w1_slave *sl);

/**
 * convert_t() - Query the device for temperature conversion and read
 * @sl: pointer to the slave to read
 * @info: pointer to a structure to store the read results
 *
 * Return: 0 if success, -kernel error code otherwise
 */
static int convert_t(struct w1_slave *sl, struct therm_info *info);

/**
 * read_scratchpad() - read the data in device RAM
 * @sl: pointer to the slave to read
 * @info: pointer to a structure to store the read results
 *
 * Return: 0 if success, -kernel error code otherwise
 */
static int read_scratchpad(struct w1_slave *sl, struct therm_info *info);

/**
 * write_scratchpad() - write nb_bytes in the device RAM
 * @sl: pointer to the slave to write in
 * @data: pointer to an array of 3 bytes, as 3 bytes MUST be written
 * @nb_bytes: number of bytes to be written (2 for DS18S20, 3 otherwise)
 *
 * Return: 0 if success, -kernel error code otherwise
 */
static int write_scratchpad(struct w1_slave *sl, const u8 *data, u8 nb_bytes);

/**
 * copy_scratchpad() - Copy the content of scratchpad in device EEPROM
 * @sl: slave involved
 *
 * Return: 0 if success, -kernel error code otherwise
 */
static int copy_scratchpad(struct w1_slave *sl);

/**
 * recall_eeprom() - Restore EEPROM data to device RAM
 * @sl: slave involved
 *
 * Return: 0 if success, -kernel error code otherwise
 */
static int recall_eeprom(struct w1_slave *sl);

/**
 * read_powermode() - Query the power mode of the slave
 * @sl: slave to retrieve the power mode
 *
 * Ask the device to get its power mode (external or parasite)
 * and store the power status in the &struct w1_therm_family_data.
 *
 * Return:
 * * 0 parasite powered device
 * * 1 externally powered device
 * * <0 kernel error code
 */
static int read_powermode(struct w1_slave *sl);

/**
 * trigger_bulk_read() - function to trigger a bulk read on the bus
 * @dev_master: the device master of the bus
 *
 * Send a SKIP ROM follow by a CONVERT T commmand on the bus.
 * It also set the status flag in each slave &struct w1_therm_family_data
 * to signal that a conversion is in progress.
 *
 * Return: 0 if success, -kernel error code otherwise
 */
static int trigger_bulk_read(struct w1_master *dev_master);

/* Sysfs interface declaration */

static ssize_t w1_slave_show(struct device *device,
	struct device_attribute *attr, char *buf);

static ssize_t w1_slave_store(struct device *device,
	struct device_attribute *attr, const char *buf, size_t size);

static ssize_t w1_seq_show(struct device *device,
	struct device_attribute *attr, char *buf);

static ssize_t temperature_show(struct device *device,
	struct device_attribute *attr, char *buf);

static ssize_t ext_power_show(struct device *device,
	struct device_attribute *attr, char *buf);

static ssize_t resolution_show(struct device *device,
	struct device_attribute *attr, char *buf);

static ssize_t resolution_store(struct device *device,
	struct device_attribute *attr, const char *buf, size_t size);

static ssize_t eeprom_store(struct device *device,
	struct device_attribute *attr, const char *buf, size_t size);

static ssize_t alarms_store(struct device *device,
	struct device_attribute *attr, const char *buf, size_t size);

static ssize_t alarms_show(struct device *device,
	struct device_attribute *attr, char *buf);

static ssize_t therm_bulk_read_store(struct device *device,
	struct device_attribute *attr, const char *buf, size_t size);

static ssize_t therm_bulk_read_show(struct device *device,
	struct device_attribute *attr, char *buf);

/* Attributes declarations */

static DEVICE_ATTR_RW(w1_slave);
static DEVICE_ATTR_RO(w1_seq);
static DEVICE_ATTR_RO(temperature);
static DEVICE_ATTR_RO(ext_power);
static DEVICE_ATTR_RW(resolution);
static DEVICE_ATTR_WO(eeprom);
static DEVICE_ATTR_RW(alarms);

static DEVICE_ATTR_RW(therm_bulk_read); /* attribut at master level */

/* Interface Functions declaration */

/**
 * w1_therm_add_slave() - Called when a new slave is discovered
 * @sl: slave just discovered by the master.
 *
 * Called by the master when the slave is discovered on the bus. Used to
 * initialize slave state before the beginning of any communication.
 *
 * Return: 0 - If success, negative kernel code otherwise
 */
static int w1_therm_add_slave(struct w1_slave *sl);

/**
 * w1_therm_remove_slave() - Called when a slave is removed
 * @sl: slave to be removed.
 *
 * Called by the master when the slave is considered not to be on the bus
 * anymore. Used to free memory.
 */
static void w1_therm_remove_slave(struct w1_slave *sl);

/* Family attributes */

static struct attribute *w1_therm_attrs[] = {
	&dev_attr_w1_slave.attr,
	&dev_attr_temperature.attr,
	&dev_attr_ext_power.attr,
	&dev_attr_resolution.attr,
	&dev_attr_eeprom.attr,
	&dev_attr_alarms.attr,
	NULL,
};

static struct attribute *w1_ds18s20_attrs[] = {
	&dev_attr_w1_slave.attr,
	&dev_attr_temperature.attr,
	&dev_attr_ext_power.attr,
	&dev_attr_eeprom.attr,
	&dev_attr_alarms.attr,
	NULL,
};

static struct attribute *w1_ds28ea00_attrs[] = {
	&dev_attr_w1_slave.attr,
	&dev_attr_w1_seq.attr,
	&dev_attr_temperature.attr,
	&dev_attr_ext_power.attr,
	&dev_attr_resolution.attr,
	&dev_attr_eeprom.attr,
	&dev_attr_alarms.attr,
	NULL,
};

/* Attribute groups */

ATTRIBUTE_GROUPS(w1_therm);
ATTRIBUTE_GROUPS(w1_ds18s20);
ATTRIBUTE_GROUPS(w1_ds28ea00);

#if IS_REACHABLE(CONFIG_HWMON)
static int w1_read_temp(struct device *dev, u32 attr, int channel,
			long *val);

static umode_t w1_is_visible(const void *_data, enum hwmon_sensor_types type,
			     u32 attr, int channel)
{
	return attr == hwmon_temp_input ? 0444 : 0;
}

static int w1_read(struct device *dev, enum hwmon_sensor_types type,
		   u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_temp:
		return w1_read_temp(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static const u32 w1_temp_config[] = {
	HWMON_T_INPUT,
	0
};

static const struct hwmon_channel_info w1_temp = {
	.type = hwmon_temp,
	.config = w1_temp_config,
};

static const struct hwmon_channel_info *w1_info[] = {
	&w1_temp,
	NULL
};

static const struct hwmon_ops w1_hwmon_ops = {
	.is_visible = w1_is_visible,
	.read = w1_read,
};

static const struct hwmon_chip_info w1_chip_info = {
	.ops = &w1_hwmon_ops,
	.info = w1_info,
};
#define W1_CHIPINFO	(&w1_chip_info)
#else
#define W1_CHIPINFO	NULL
#endif

/* Family operations */

static struct w1_family_ops w1_therm_fops = {
	.add_slave	= w1_therm_add_slave,
	.remove_slave	= w1_therm_remove_slave,
	.groups		= w1_therm_groups,
	.chip_info	= W1_CHIPINFO,
};

static struct w1_family_ops w1_ds18s20_fops = {
	.add_slave	= w1_therm_add_slave,
	.remove_slave	= w1_therm_remove_slave,
	.groups		= w1_ds18s20_groups,
	.chip_info	= W1_CHIPINFO,
};

static struct w1_family_ops w1_ds28ea00_fops = {
	.add_slave	= w1_therm_add_slave,
	.remove_slave	= w1_therm_remove_slave,
	.groups		= w1_ds28ea00_groups,
	.chip_info	= W1_CHIPINFO,
};

/* Family binding operations struct */

static struct w1_family w1_therm_family_DS18S20 = {
	.fid = W1_THERM_DS18S20,
	.fops = &w1_ds18s20_fops,
};

static struct w1_family w1_therm_family_DS18B20 = {
	.fid = W1_THERM_DS18B20,
	.fops = &w1_therm_fops,
};

static struct w1_family w1_therm_family_DS1822 = {
	.fid = W1_THERM_DS1822,
	.fops = &w1_therm_fops,
};

static struct w1_family w1_therm_family_DS28EA00 = {
	.fid = W1_THERM_DS28EA00,
	.fops = &w1_ds28ea00_fops,
};

static struct w1_family w1_therm_family_DS1825 = {
	.fid = W1_THERM_DS1825,
	.fops = &w1_therm_fops,
};

/* Device dependent func */

static inline int w1_DS18B20_convert_time(struct w1_slave *sl)
{
	int ret;

	if (!sl->family_data)
		return -ENODEV;	/* device unknown */

	/* return time in ms for conversion operation */
	switch (SLAVE_RESOLUTION(sl)) {
	case 9:
		ret = 95;
		break;
	case 10:
		ret = 190;
		break;
	case 11:
		ret = 375;
		break;
	case 12:
	default:
		ret = 750;
	}
	return ret;
}

static inline int w1_DS18S20_convert_time(struct w1_slave *sl)
{
	(void)(sl);
	return 750; /* always 750ms for DS18S20 */
}

static inline int w1_DS18B20_write_data(struct w1_slave *sl,
				const u8 *data)
{
	return write_scratchpad(sl, data, 3);
}

static inline int w1_DS18S20_write_data(struct w1_slave *sl,
				const u8 *data)
{
	/* No config register */
	return write_scratchpad(sl, data, 2);
}

static inline int w1_DS18B20_set_resolution(struct w1_slave *sl, int val)
{
	int ret = -ENODEV;
	u8 new_config_register[3];	/* array of data to be written */
	struct therm_info info;

	/* resolution of DS18B20 is in the range [9..12] bits */
	if (val < 9 || val > 12)
		return -EINVAL;

	val -= 9; /* soustract 9 the lowest resolution in bit */
	val = (val << 5); /* shift to position bit 5 & bit 6 */

	/*
	 * Read the scratchpad to change only the required bits
	 * (bit5 & bit 6 from byte 4)
	 */
	ret = read_scratchpad(sl, &info);
	if (!ret) {
		new_config_register[0] = info.rom[2];
		new_config_register[1] = info.rom[3];
		/* config register is byte 4 & mask 0b10011111*/
		new_config_register[2] = (info.rom[4] & 0x9F) |
					(u8) val;
	} else
		return ret;

	/* Write data in the device RAM */
	ret = w1_DS18B20_write_data(sl, new_config_register);

	return ret;
}

static inline int w1_DS18B20_get_resolution(struct w1_slave *sl)
{
	int ret = -ENODEV;
	u8 config_register;
	struct therm_info info;

	ret = read_scratchpad(sl, &info);

	if (!ret)	{
		config_register = info.rom[4]; /* config register is byte 4 */
		config_register &= 0x60; /* 0b01100000 keep only bit 5 & 6 */
		config_register = (config_register >> 5);	/* shift */
		config_register += 9; /* add 9 the lowest resolution in bit */
		ret = (int) config_register;
	}
	return ret;
}

/**
 * w1_DS18B20_convert_temp() - temperature computation for DS18B20
 * @rom: data read from device RAM (8 data bytes + 1 CRC byte)
 *
 * Can be called for any DS18B20 compliant device.
 *
 * Return: value in millidegrees Celsius.
 */
static inline int w1_DS18B20_convert_temp(u8 rom[9])
{
	s16 t = le16_to_cpup((__le16 *)rom);

	return t*1000/16;
}

/**
 * w1_DS18S20_convert_temp() - temperature computation for DS18S20
 * @rom: data read from device RAM (8 data bytes + 1 CRC byte)
 *
 * Can be called for any DS18S20 compliant device.
 *
 * Return: value in millidegrees Celsius.
 */
static inline int w1_DS18S20_convert_temp(u8 rom[9])
{
	int t, h;

	if (!rom[7]) {
		pr_debug("%s: Invalid argument for conversion\n", __func__);
		return 0;
	}

	if (rom[1] == 0)
		t = ((s32)rom[0] >> 1)*1000;
	else
		t = 1000*(-1*(s32)(0x100-rom[0]) >> 1);

	t -= 250;
	h = 1000*((s32)rom[7] - (s32)rom[6]);
	h /= (s32)rom[7];
	t += h;

	return t;
}

/* Device capability description */

static struct w1_therm_family_converter w1_therm_families[] = {
	{
		.f				= &w1_therm_family_DS18S20,
		.convert			= w1_DS18S20_convert_temp,
		.get_conversion_time	= w1_DS18S20_convert_time,
		.set_resolution		= NULL,	/* no config register */
		.get_resolution		= NULL,	/* no config register */
		.write_data			= w1_DS18S20_write_data,
		.bulk_read			= true
	},
	{
		.f				= &w1_therm_family_DS1822,
		.convert			= w1_DS18B20_convert_temp,
		.get_conversion_time	= w1_DS18B20_convert_time,
		.set_resolution		= w1_DS18B20_set_resolution,
		.get_resolution		= w1_DS18B20_get_resolution,
		.write_data			= w1_DS18B20_write_data,
		.bulk_read			= true
	},
	{
		.f				= &w1_therm_family_DS18B20,
		.convert			= w1_DS18B20_convert_temp,
		.get_conversion_time	= w1_DS18B20_convert_time,
		.set_resolution		= w1_DS18B20_set_resolution,
		.get_resolution		= w1_DS18B20_get_resolution,
		.write_data			= w1_DS18B20_write_data,
		.bulk_read			= true
	},
	{
		.f				= &w1_therm_family_DS28EA00,
		.convert			= w1_DS18B20_convert_temp,
		.get_conversion_time	= w1_DS18B20_convert_time,
		.set_resolution		= w1_DS18B20_set_resolution,
		.get_resolution		= w1_DS18B20_get_resolution,
		.write_data			= w1_DS18B20_write_data,
		.bulk_read			= false
	},
	{
		.f				= &w1_therm_family_DS1825,
		.convert			= w1_DS18B20_convert_temp,
		.get_conversion_time	= w1_DS18B20_convert_time,
		.set_resolution		= w1_DS18B20_set_resolution,
		.get_resolution		= w1_DS18B20_get_resolution,
		.write_data			= w1_DS18B20_write_data,
		.bulk_read			= true
	}
};

/* Helpers Functions */

/**
 * device_family() - Retrieve a pointer on &struct w1_therm_family_converter
 * @sl: slave to retrieve the device specific structure
 *
 * Return: pointer to the slaves's family converter, NULL if not known
 */
static struct w1_therm_family_converter *device_family(struct w1_slave *sl)
{
	struct w1_therm_family_converter *ret = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(w1_therm_families); ++i) {
		if (w1_therm_families[i].f->fid == sl->family->fid) {
			ret = &w1_therm_families[i];
			break;
		}
	}
	return ret;
}

/**
 * bus_mutex_lock() - Acquire the mutex
 * @lock: w1 bus mutex to acquire
 *
 * It try to acquire the mutex W1_THERM_MAX_TRY times and wait
 * W1_THERM_RETRY_DELAY between 2 attempts.
 *
 * Return: true is mutex is acquired and lock, false otherwise
 */
static inline bool bus_mutex_lock(struct mutex *lock)
{
	int max_trying = W1_THERM_MAX_TRY;

	/* try to acquire the mutex, if not, sleep retry_delay before retry) */
	while (mutex_lock_interruptible(lock) != 0 && max_trying > 0) {
		unsigned long sleep_rem;

		sleep_rem = msleep_interruptible(W1_THERM_RETRY_DELAY);
		if (!sleep_rem)
			max_trying--;
	}

	if (!max_trying)
		return false;	/* Didn't acquire the bus mutex */

	return true;
}

/**
 * support_bulk_read() - check if slave support bulk read
 * @sl: device to check the ability
 *
 * Return: true if bulk read is supported, false if not or error
 */
static inline bool bulk_read_support(struct w1_slave *sl)
{
	if (SLAVE_SPECIFIC_FUNC(sl))
		return SLAVE_SPECIFIC_FUNC(sl)->bulk_read;

	dev_info(&sl->dev,
		"%s: Device not supported by the driver\n", __func__);

	return false;  /* No device family */
}

/**
 * conversion_time() - get the Tconv for the slave
 * @sl: device to get the conversion time
 *
 * On device supporting resolution settings, conversion time depend
 * on the resolution setting. This helper function get the slave timing,
 * depending on its current setting.
 *
 * Return: conversion time in ms, negative values are kernel error code
 */
static inline int conversion_time(struct w1_slave *sl)
{
	if (SLAVE_SPECIFIC_FUNC(sl))
		return SLAVE_SPECIFIC_FUNC(sl)->get_conversion_time(sl);

	dev_info(&sl->dev,
		"%s: Device not supported by the driver\n", __func__);

	return -ENODEV;  /* No device family */
}

/**
 * temperature_from_RAM() - Convert the read info to temperature
 * @sl: device that sent the RAM data
 * @rom: read value on the slave device RAM
 *
 * Device dependent, the function bind the correct computation method.
 *
 * Return: temperature in 1/1000degC, 0 on error.
 */
static inline int temperature_from_RAM(struct w1_slave *sl, u8 rom[9])
{
	if (SLAVE_SPECIFIC_FUNC(sl))
		return SLAVE_SPECIFIC_FUNC(sl)->convert(rom);

	dev_info(&sl->dev,
		"%s: Device not supported by the driver\n", __func__);

	return 0;  /* No device family */
}

/**
 * int_to_short() - Safe casting of int to short
 *
 * @i: integer to be converted to short
 *
 * Device register use 1 byte to store signed integer.
 * This helper function convert the int in a signed short,
 * using the min/max values that device can measure as limits.
 * min/max values are defined by macro.
 *
 * Return: a short in the range of min/max value
 */
static inline s8 int_to_short(int i)
{
	/* Prepare to cast to short by eliminating out of range values */
	i = i > MAX_TEMP ? MAX_TEMP : i;
	i = i < MIN_TEMP ? MIN_TEMP : i;
	return (s8) i;
}

/* Interface Functions */

static int w1_therm_add_slave(struct w1_slave *sl)
{
	struct w1_therm_family_converter *sl_family_conv;

	/* Allocate memory */
	sl->family_data = kzalloc(sizeof(struct w1_therm_family_data),
		GFP_KERNEL);
	if (!sl->family_data)
		return -ENOMEM;

	atomic_set(THERM_REFCNT(sl->family_data), 1);

	/* Get a pointer to the device specific function struct */
	sl_family_conv = device_family(sl);
	if (!sl_family_conv) {
		kfree(sl->family_data);
		return -ENODEV;
	}
	/* save this pointer to the device structure */
	SLAVE_SPECIFIC_FUNC(sl) = sl_family_conv;

	if (bulk_read_support(sl)) {
		/*
		 * add the sys entry to trigger bulk_read
		 * at master level only the 1st time
		 */
		if (!bulk_read_device_counter) {
			int err = device_create_file(&sl->master->dev,
				&dev_attr_therm_bulk_read);

			if (err)
				dev_warn(&sl->dev,
				"%s: Device has been added, but bulk read is unavailable. err=%d\n",
				__func__, err);
		}
		/* Increment the counter */
		bulk_read_device_counter++;
	}

	/* Getting the power mode of the device {external, parasite} */
	SLAVE_POWERMODE(sl) = read_powermode(sl);

	if (SLAVE_POWERMODE(sl) < 0) {
		/* no error returned as device has been added */
		dev_warn(&sl->dev,
			"%s: Device has been added, but power_mode may be corrupted. err=%d\n",
			 __func__, SLAVE_POWERMODE(sl));
	}

	/* Getting the resolution of the device */
	if (SLAVE_SPECIFIC_FUNC(sl)->get_resolution) {
		SLAVE_RESOLUTION(sl) =
			SLAVE_SPECIFIC_FUNC(sl)->get_resolution(sl);
		if (SLAVE_RESOLUTION(sl) < 0) {
			/* no error returned as device has been added */
			dev_warn(&sl->dev,
				"%s:Device has been added, but resolution may be corrupted. err=%d\n",
				__func__, SLAVE_RESOLUTION(sl));
		}
	}

	/* Finally initialize convert_triggered flag */
	SLAVE_CONVERT_TRIGGERED(sl) = 0;

	return 0;
}

static void w1_therm_remove_slave(struct w1_slave *sl)
{
	int refcnt = atomic_sub_return(1, THERM_REFCNT(sl->family_data));

	if (bulk_read_support(sl)) {
		bulk_read_device_counter--;
		/* Delete the entry if no more device support the feature */
		if (!bulk_read_device_counter)
			device_remove_file(&sl->master->dev,
				&dev_attr_therm_bulk_read);
	}

	while (refcnt) {
		msleep(1000);
		refcnt = atomic_read(THERM_REFCNT(sl->family_data));
	}
	kfree(sl->family_data);
	sl->family_data = NULL;
}

/* Hardware Functions */

/* Safe version of reset_select_slave - avoid using the one in w_io.c */
static int reset_select_slave(struct w1_slave *sl)
{
	u8 match[9] = { W1_MATCH_ROM, };
	u64 rn = le64_to_cpu(*((u64 *)&sl->reg_num));

	if (w1_reset_bus(sl->master))
		return -ENODEV;

	memcpy(&match[1], &rn, 8);
	w1_write_block(sl->master, match, 9);

	return 0;
}

static int convert_t(struct w1_slave *sl, struct therm_info *info)
{
	struct w1_master *dev_master = sl->master;
	int max_trying = W1_THERM_MAX_TRY;
	int t_conv;
	int ret = -ENODEV;
	bool strong_pullup;

	if (!sl->family_data)
		goto error;

	strong_pullup = (w1_strong_pullup == 2 ||
					(!SLAVE_POWERMODE(sl) &&
					w1_strong_pullup));

	/* get conversion duration device and id dependent */
	t_conv = conversion_time(sl);

	memset(info->rom, 0, sizeof(info->rom));

	/* prevent the slave from going away in sleep */
	atomic_inc(THERM_REFCNT(sl->family_data));

	if (!bus_mutex_lock(&dev_master->bus_mutex)) {
		ret = -EAGAIN;	/* Didn't acquire the mutex */
		goto dec_refcnt;
	}

	while (max_trying-- && ret) { /* ret should be 0 */

		info->verdict = 0;
		info->crc = 0;
		/* safe version to select slave */
		if (!reset_select_slave(sl)) {
			unsigned long sleep_rem;

			/* 750ms strong pullup (or delay) after the convert */
			if (strong_pullup)
				w1_next_pullup(dev_master, t_conv);

			w1_write_8(dev_master, W1_CONVERT_TEMP);

			if (strong_pullup) { /*some device need pullup */
				sleep_rem = msleep_interruptible(t_conv);
				if (sleep_rem != 0) {
					ret = -EINTR;
					goto mt_unlock;
				}
				mutex_unlock(&dev_master->bus_mutex);
			} else { /*no device need pullup */
				mutex_unlock(&dev_master->bus_mutex);

				sleep_rem = msleep_interruptible(t_conv);
				if (sleep_rem != 0) {
					ret = -EINTR;
					goto dec_refcnt;
				}
			}
			ret = read_scratchpad(sl, info);
			goto dec_refcnt;
		}

	}

mt_unlock:
	mutex_unlock(&dev_master->bus_mutex);
dec_refcnt:
	atomic_dec(THERM_REFCNT(sl->family_data));
error:
	return ret;
}

static int read_scratchpad(struct w1_slave *sl, struct therm_info *info)
{
	struct w1_master *dev_master = sl->master;
	int max_trying = W1_THERM_MAX_TRY;
	int ret = -ENODEV;

	info->verdict = 0;

	if (!sl->family_data)
		goto error;

	memset(info->rom, 0, sizeof(info->rom));

	/* prevent the slave from going away in sleep */
	atomic_inc(THERM_REFCNT(sl->family_data));

	if (!bus_mutex_lock(&dev_master->bus_mutex)) {
		ret = -EAGAIN;	/* Didn't acquire the mutex */
		goto dec_refcnt;
	}

	while (max_trying-- && ret) { /* ret should be 0 */
		/* safe version to select slave */
		if (!reset_select_slave(sl)) {
			u8 nb_bytes_read;

			w1_write_8(dev_master, W1_READ_SCRATCHPAD);

			nb_bytes_read = w1_read_block(dev_master, info->rom, 9);
			if (nb_bytes_read != 9) {
				dev_warn(&sl->dev,
					"w1_read_block(): returned %u instead of 9.\n",
					nb_bytes_read);
				ret = -EIO;
			}

			info->crc = w1_calc_crc8(info->rom, 8);

			if (info->rom[8] == info->crc) {
				info->verdict = 1;
				ret = 0;
			} else
				ret = -EIO; /* CRC not checked */
		}

	}
	mutex_unlock(&dev_master->bus_mutex);

dec_refcnt:
	atomic_dec(THERM_REFCNT(sl->family_data));
error:
	return ret;
}

static int write_scratchpad(struct w1_slave *sl, const u8 *data, u8 nb_bytes)
{
	struct w1_master *dev_master = sl->master;
	int max_trying = W1_THERM_MAX_TRY;
	int ret = -ENODEV;

	if (!sl->family_data)
		goto error;

	/* prevent the slave from going away in sleep */
	atomic_inc(THERM_REFCNT(sl->family_data));

	if (!bus_mutex_lock(&dev_master->bus_mutex)) {
		ret = -EAGAIN;	/* Didn't acquire the mutex */
		goto dec_refcnt;
	}

	while (max_trying-- && ret) { /* ret should be 0 */
		/* safe version to select slave */
		if (!reset_select_slave(sl)) {
			w1_write_8(dev_master, W1_WRITE_SCRATCHPAD);
			w1_write_block(dev_master, data, nb_bytes);
			ret = 0;
		}
	}
	mutex_unlock(&dev_master->bus_mutex);

dec_refcnt:
	atomic_dec(THERM_REFCNT(sl->family_data));
error:
	return ret;
}

static int copy_scratchpad(struct w1_slave *sl)
{
	struct w1_master *dev_master = sl->master;
	int max_trying = W1_THERM_MAX_TRY;
	int t_write, ret = -ENODEV;
	bool strong_pullup;

	if (!sl->family_data)
		goto error;

	t_write = W1_THERM_EEPROM_WRITE_DELAY;
	strong_pullup = (w1_strong_pullup == 2 ||
					(!SLAVE_POWERMODE(sl) &&
					w1_strong_pullup));

	/* prevent the slave from going away in sleep */
	atomic_inc(THERM_REFCNT(sl->family_data));

	if (!bus_mutex_lock(&dev_master->bus_mutex)) {
		ret = -EAGAIN;	/* Didn't acquire the mutex */
		goto dec_refcnt;
	}

	while (max_trying-- && ret) { /* ret should be 0 */
		/* safe version to select slave */
		if (!reset_select_slave(sl)) {
			unsigned long sleep_rem;

			/* 10ms strong pullup (or delay) after the convert */
			if (strong_pullup)
				w1_next_pullup(dev_master, t_write);

			w1_write_8(dev_master, W1_COPY_SCRATCHPAD);

			if (strong_pullup) {
				sleep_rem = msleep_interruptible(t_write);
				if (sleep_rem != 0) {
					ret = -EINTR;
					goto mt_unlock;
				}
			}
			ret = 0;
		}

	}

mt_unlock:
	mutex_unlock(&dev_master->bus_mutex);
dec_refcnt:
	atomic_dec(THERM_REFCNT(sl->family_data));
error:
	return ret;
}

static int recall_eeprom(struct w1_slave *sl)
{
	struct w1_master *dev_master = sl->master;
	int max_trying = W1_THERM_MAX_TRY;
	int ret = -ENODEV;

	if (!sl->family_data)
		goto error;

	/* prevent the slave from going away in sleep */
	atomic_inc(THERM_REFCNT(sl->family_data));

	if (!bus_mutex_lock(&dev_master->bus_mutex)) {
		ret = -EAGAIN;	/* Didn't acquire the mutex */
		goto dec_refcnt;
	}

	while (max_trying-- && ret) { /* ret should be 0 */
		/* safe version to select slave */
		if (!reset_select_slave(sl)) {

			w1_write_8(dev_master, W1_RECALL_EEPROM);

			ret = 1; /* Slave will pull line to 0 */
			while (ret)
				ret = 1 - w1_touch_bit(dev_master, 1);
		}

	}

	mutex_unlock(&dev_master->bus_mutex);

dec_refcnt:
	atomic_dec(THERM_REFCNT(sl->family_data));
error:
	return ret;
}

static int read_powermode(struct w1_slave *sl)
{
	struct w1_master *dev_master = sl->master;
	int max_trying = W1_THERM_MAX_TRY;
	int  ret = -ENODEV;

	if (!sl->family_data)
		goto error;

	/* prevent the slave from going away in sleep */
	atomic_inc(THERM_REFCNT(sl->family_data));

	if (!bus_mutex_lock(&dev_master->bus_mutex)) {
		ret = -EAGAIN;	/* Didn't acquire the mutex */
		goto dec_refcnt;
	}

	while ((max_trying--) && (ret < 0)) {
		/* safe version to select slave */
		if (!reset_select_slave(sl)) {
			w1_write_8(dev_master, W1_READ_PSUPPLY);
			/*
			 * Emit a read time slot and read only one bit,
			 * 1 is externally powered,
			 * 0 is parasite powered
			 */
			ret = w1_touch_bit(dev_master, 1);
			/* ret should be either 1 either 0 */
		}
	}
	mutex_unlock(&dev_master->bus_mutex);

dec_refcnt:
	atomic_dec(THERM_REFCNT(sl->family_data));
error:
	return ret;
}

static int trigger_bulk_read(struct w1_master *dev_master)
{
	struct w1_slave *sl = NULL; /* used to iterate through slaves */
	int max_trying = W1_THERM_MAX_TRY;
	int t_conv = 0;
	int ret = -ENODEV;
	bool strong_pullup = false;

	/*
	 * Check whether there are parasite powered device on the bus,
	 * and compute duration of conversion for these devices
	 * so we can apply a strong pullup if required
	 */
	list_for_each_entry(sl, &dev_master->slist, w1_slave_entry) {
		if (!sl->family_data)
			goto error;
		if (bulk_read_support(sl)) {
			int t_cur = conversion_time(sl);

			t_conv = t_cur > t_conv ? t_cur : t_conv;
			strong_pullup = strong_pullup ||
					(w1_strong_pullup == 2 ||
					(!SLAVE_POWERMODE(sl) &&
					w1_strong_pullup));
		}
	}

	/*
	 * t_conv is the max conversion time required on the bus
	 * If its 0, no device support the bulk read feature
	 */
	if (!t_conv)
		goto error;

	if (!bus_mutex_lock(&dev_master->bus_mutex)) {
		ret = -EAGAIN;	/* Didn't acquire the mutex */
		goto error;
	}

	while ((max_trying--) && (ret < 0)) { /* ret should be either 0 */

		if (!w1_reset_bus(dev_master)) {	/* Just reset the bus */
			unsigned long sleep_rem;

			w1_write_8(dev_master, W1_SKIP_ROM);

			if (strong_pullup)	/* Apply pullup if required */
				w1_next_pullup(dev_master, t_conv);

			w1_write_8(dev_master, W1_CONVERT_TEMP);

			/* set a flag to instruct that converT pending */
			list_for_each_entry(sl,
				&dev_master->slist, w1_slave_entry) {
				if (bulk_read_support(sl))
					SLAVE_CONVERT_TRIGGERED(sl) = -1;
			}

			if (strong_pullup) { /* some device need pullup */
				sleep_rem = msleep_interruptible(t_conv);
				if (sleep_rem != 0) {
					ret = -EINTR;
					goto mt_unlock;
				}
				mutex_unlock(&dev_master->bus_mutex);
			} else {
				mutex_unlock(&dev_master->bus_mutex);
				sleep_rem = msleep_interruptible(t_conv);
				if (sleep_rem != 0) {
					ret = -EINTR;
					goto set_flag;
				}
			}
			ret = 0;
			goto set_flag;
		}
	}

mt_unlock:
	mutex_unlock(&dev_master->bus_mutex);
set_flag:
	/* set a flag to register convsersion is done */
	list_for_each_entry(sl, &dev_master->slist, w1_slave_entry) {
		if (bulk_read_support(sl))
			SLAVE_CONVERT_TRIGGERED(sl) = 1;
	}
error:
	return ret;
}

/* Sysfs Interface definition */

static ssize_t w1_slave_show(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	struct w1_slave *sl = dev_to_w1_slave(device);
	struct therm_info info;
	u8 *family_data = sl->family_data;
	int ret, i;
	ssize_t c = PAGE_SIZE;

	if (bulk_read_support(sl)) {
		if (SLAVE_CONVERT_TRIGGERED(sl) < 0) {
			dev_dbg(device,
				"%s: Conversion in progress, retry later\n",
				__func__);
			return 0;
		} else if (SLAVE_CONVERT_TRIGGERED(sl) > 0) {
			/* A bulk read has been issued, read the device RAM */
			ret = read_scratchpad(sl, &info);
			SLAVE_CONVERT_TRIGGERED(sl) = 0;
		} else
			ret = convert_t(sl, &info);
	} else
		ret = convert_t(sl, &info);

	if (ret < 0) {
		dev_dbg(device,
			"%s: Temperature data may be corrupted. err=%d\n",
			__func__, ret);
		return 0;
	}

	for (i = 0; i < 9; ++i)
		c -= snprintf(buf + PAGE_SIZE - c, c, "%02x ", info.rom[i]);
	c -= snprintf(buf + PAGE_SIZE - c, c, ": crc=%02x %s\n",
		      info.crc, (info.verdict) ? "YES" : "NO");

	if (info.verdict)
		memcpy(family_data, info.rom, sizeof(info.rom));
	else
		dev_warn(device, "%s:Read failed CRC check\n", __func__);

	for (i = 0; i < 9; ++i)
		c -= snprintf(buf + PAGE_SIZE - c, c, "%02x ",
			      ((u8 *)family_data)[i]);

	c -= snprintf(buf + PAGE_SIZE - c, c, "t=%d\n",
			temperature_from_RAM(sl, info.rom));

	ret = PAGE_SIZE - c;
	return ret;
}

static ssize_t w1_slave_store(struct device *device,
			      struct device_attribute *attr, const char *buf,
			      size_t size)
{
	int val, ret = 0;
	struct w1_slave *sl = dev_to_w1_slave(device);

	ret = kstrtoint(buf, 10, &val); /* converting user entry to int */

	if (ret) {	/* conversion error */
		dev_info(device,
			"%s: conversion error. err= %d\n", __func__, ret);
		return size;	/* return size to avoid call back again */
	}

	if ((!sl->family_data) || (!SLAVE_SPECIFIC_FUNC(sl))) {
		dev_info(device,
			"%s: Device not supported by the driver\n", __func__);
		return size;  /* No device family */
	}

	if (val == 0)	/* val=0 : trigger a EEPROM save */
		ret = copy_scratchpad(sl);
	else {
		if (SLAVE_SPECIFIC_FUNC(sl)->set_resolution)
			ret = SLAVE_SPECIFIC_FUNC(sl)->set_resolution(sl, val);
	}

	if (ret) {
		dev_info(device,
			"%s: writing error %d\n", __func__, ret);
		/* return size to avoid call back again */
	} else
		SLAVE_RESOLUTION(sl) = val;

	return size; /* always return size to avoid infinite calling */
}

static ssize_t temperature_show(struct device *device,
	struct device_attribute *attr, char *buf)
{
	struct w1_slave *sl = dev_to_w1_slave(device);
	struct therm_info info;
	int ret = 0;

	if ((!sl->family_data) || (!SLAVE_SPECIFIC_FUNC(sl))) {
		dev_info(device,
			"%s: Device not supported by the driver\n", __func__);
		return 0;  /* No device family */
	}

	if (bulk_read_support(sl)) {
		if (SLAVE_CONVERT_TRIGGERED(sl) < 0) {
			dev_dbg(device,
				"%s: Conversion in progress, retry later\n",
				__func__);
			return 0;
		} else if (SLAVE_CONVERT_TRIGGERED(sl) > 0) {
			/* A bulk read has been issued, read the device RAM */
			ret = read_scratchpad(sl, &info);
			SLAVE_CONVERT_TRIGGERED(sl) = 0;
		} else
			ret = convert_t(sl, &info);
	} else
		ret = convert_t(sl, &info);

	if (ret < 0) {
		dev_dbg(device,
			"%s: Temperature data may be corrupted. err=%d\n",
			__func__, ret);
		return 0;
	}

	return sprintf(buf, "%d\n", temperature_from_RAM(sl, info.rom));
}

static ssize_t ext_power_show(struct device *device,
	struct device_attribute *attr, char *buf)
{
	struct w1_slave *sl = dev_to_w1_slave(device);

	if (!sl->family_data) {
		dev_info(device,
			"%s: Device not supported by the driver\n", __func__);
		return 0;  /* No device family */
	}

	/* Getting the power mode of the device {external, parasite} */
	SLAVE_POWERMODE(sl) = read_powermode(sl);

	if (SLAVE_POWERMODE(sl) < 0) {
		dev_dbg(device,
			"%s: Power_mode may be corrupted. err=%d\n",
			__func__, SLAVE_POWERMODE(sl));
	}
	return sprintf(buf, "%d\n", SLAVE_POWERMODE(sl));
}

static ssize_t resolution_show(struct device *device,
	struct device_attribute *attr, char *buf)
{
	struct w1_slave *sl = dev_to_w1_slave(device);

	if ((!sl->family_data) || (!SLAVE_SPECIFIC_FUNC(sl))) {
		dev_info(device,
			"%s: Device not supported by the driver\n", __func__);
		return 0;  /* No device family */
	}

	/* get the correct function depending on the device */
	SLAVE_RESOLUTION(sl) = SLAVE_SPECIFIC_FUNC(sl)->get_resolution(sl);
	if (SLAVE_RESOLUTION(sl) < 0) {
		dev_dbg(device,
			"%s: Resolution may be corrupted. err=%d\n",
			__func__, SLAVE_RESOLUTION(sl));
	}

	return sprintf(buf, "%d\n", SLAVE_RESOLUTION(sl));
}

static ssize_t resolution_store(struct device *device,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct w1_slave *sl = dev_to_w1_slave(device);
	int val;
	int ret = 0;

	ret = kstrtoint(buf, 10, &val); /* converting user entry to int */

	if (ret) {	/* conversion error */
		dev_info(device,
			"%s: conversion error. err= %d\n", __func__, ret);
		return size;	/* return size to avoid call back again */
	}

	if ((!sl->family_data) || (!SLAVE_SPECIFIC_FUNC(sl))) {
		dev_info(device,
			"%s: Device not supported by the driver\n", __func__);
		return size;  /* No device family */
	}

	/*
	 * Don't deal with the val enterd by user,
	 * only device knows what is correct or not
	 */

	/* get the correct function depending on the device */
	ret = SLAVE_SPECIFIC_FUNC(sl)->set_resolution(sl, val);

	if (ret) {
		dev_info(device,
			"%s: writing error %d\n", __func__, ret);
		/* return size to avoid call back again */
	} else
		SLAVE_RESOLUTION(sl) = val;

	return size;
}

static ssize_t eeprom_store(struct device *device,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct w1_slave *sl = dev_to_w1_slave(device);
	int ret = -EINVAL; /* Invalid argument */

	if (size == sizeof(EEPROM_CMD_WRITE)) {
		if (!strncmp(buf, EEPROM_CMD_WRITE, sizeof(EEPROM_CMD_WRITE)-1))
			ret = copy_scratchpad(sl);
	} else if (size == sizeof(EEPROM_CMD_READ)) {
		if (!strncmp(buf, EEPROM_CMD_READ, sizeof(EEPROM_CMD_READ)-1))
			ret = recall_eeprom(sl);
	}

	if (ret)
		dev_info(device, "%s: error in process %d\n", __func__, ret);

	return size;
}

static ssize_t alarms_show(struct device *device,
	struct device_attribute *attr, char *buf)
{
	struct w1_slave *sl = dev_to_w1_slave(device);
	int ret = -ENODEV;
	s8 th = 0, tl = 0;
	struct therm_info scratchpad;

	ret = read_scratchpad(sl, &scratchpad);

	if (!ret)	{
		th = scratchpad.rom[2]; /* TH is byte 2 */
		tl = scratchpad.rom[3]; /* TL is byte 3 */
	} else {
		dev_info(device,
			"%s: error reading alarms register %d\n",
			__func__, ret);
	}

	return sprintf(buf, "%hd %hd\n", tl, th);
}

static ssize_t alarms_store(struct device *device,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct w1_slave *sl = dev_to_w1_slave(device);
	struct therm_info info;
	u8 new_config_register[3];	/* array of data to be written */
	int temp, ret = -EINVAL;
	char *token = NULL;
	s8 tl, th, tt;	/* 1 byte per value + temp ring order */
	char *p_args = kmalloc(size, GFP_KERNEL);

	/* Safe string copys as buf is const */
	if (!p_args) {
		dev_warn(device,
			"%s: error unable to allocate memory %d\n",
			__func__, -ENOMEM);
		return size;
	}
	strcpy(p_args, buf);

	/* Split string using space char */
	token = strsep(&p_args, " ");

	if (!token)	{
		dev_info(device,
			"%s: error parsing args %d\n", __func__, -EINVAL);
		goto free_m;
	}

	/* Convert 1st entry to int */
	ret = kstrtoint (token, 10, &temp);
	if (ret) {
		dev_info(device,
			"%s: error parsing args %d\n", __func__, ret);
		goto free_m;
	}

	tl = int_to_short(temp);

	/* Split string using space char */
	token = strsep(&p_args, " ");
	if (!token)	{
		dev_info(device,
			"%s: error parsing args %d\n", __func__, -EINVAL);
		goto free_m;
	}
	/* Convert 2nd entry to int */
	ret = kstrtoint (token, 10, &temp);
	if (ret) {
		dev_info(device,
			"%s: error parsing args %d\n", __func__, ret);
		goto free_m;
	}

	/* Prepare to cast to short by eliminating out of range values */
	th = int_to_short(temp);

	/* Reorder if required th and tl */
	if (tl > th) {
		tt = tl; tl = th; th = tt;
	}

	/*
	 * Read the scratchpad to change only the required bits
	 * (th : byte 2 - tl: byte 3)
	 */
	ret = read_scratchpad(sl, &info);
	if (!ret) {
		new_config_register[0] = th;	/* Byte 2 */
		new_config_register[1] = tl;	/* Byte 3 */
		new_config_register[2] = info.rom[4];/* Byte 4 */
	} else {
		dev_info(device,
			"%s: error reading from the slave device %d\n",
			__func__, ret);
		goto free_m;
	}

	/* Write data in the device RAM */
	if (!SLAVE_SPECIFIC_FUNC(sl)) {
		dev_info(device,
			"%s: Device not supported by the driver %d\n",
			__func__, -ENODEV);
		goto free_m;
	}

	ret = SLAVE_SPECIFIC_FUNC(sl)->write_data(sl, new_config_register);
	if (ret)
		dev_info(device,
			"%s: error writing to the slave device %d\n",
			__func__, ret);

free_m:
	/* free allocated memory */
	kfree(p_args);

	return size;
}

static ssize_t therm_bulk_read_store(struct device *device,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct w1_master *dev_master = dev_to_w1_master(device);
	int ret = -EINVAL; /* Invalid argument */

	if (size == sizeof(BULK_TRIGGER_CMD))
		if (!strncmp(buf, BULK_TRIGGER_CMD,
				sizeof(BULK_TRIGGER_CMD)-1))
			ret = trigger_bulk_read(dev_master);

	if (ret)
		dev_info(device,
			"%s: unable to trigger a bulk read on the bus. err=%d\n",
			__func__, ret);

	return size;
}

static ssize_t therm_bulk_read_show(struct device *device,
	struct device_attribute *attr, char *buf)
{
	struct w1_master *dev_master = dev_to_w1_master(device);
	struct w1_slave *sl = NULL;
	int ret = 0;

	list_for_each_entry(sl, &dev_master->slist, w1_slave_entry) {
		if (sl->family_data) {
			if (bulk_read_support(sl)) {
				if (SLAVE_CONVERT_TRIGGERED(sl) == -1) {
					ret = -1;
					goto show_result;
				}
				if (SLAVE_CONVERT_TRIGGERED(sl) == 1)
					/* continue to check other slaves */
					ret = 1;
			}
		}
	}
show_result:
	return sprintf(buf, "%d\n", ret);
}

#if IS_REACHABLE(CONFIG_HWMON)
static int w1_read_temp(struct device *device, u32 attr, int channel,
			long *val)
{
	struct w1_slave *sl = dev_get_drvdata(device);
	struct therm_info info;
	int ret;

	switch (attr) {
	case hwmon_temp_input:
		ret = convert_t(sl, &info);
		if (ret)
			return ret;

		if (!info.verdict) {
			ret = -EIO;
			return ret;
		}

		*val = temperature_from_RAM(sl, info.rom);
		ret = 0;
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}
#endif

#define W1_42_CHAIN	0x99
#define W1_42_CHAIN_OFF	0x3C
#define W1_42_CHAIN_OFF_INV	0xC3
#define W1_42_CHAIN_ON	0x5A
#define W1_42_CHAIN_ON_INV	0xA5
#define W1_42_CHAIN_DONE 0x96
#define W1_42_CHAIN_DONE_INV 0x69
#define W1_42_COND_READ	0x0F
#define W1_42_SUCCESS_CONFIRM_BYTE 0xAA
#define W1_42_FINISHED_BYTE 0xFF
static ssize_t w1_seq_show(struct device *device,
	struct device_attribute *attr, char *buf)
{
	struct w1_slave *sl = dev_to_w1_slave(device);
	ssize_t c = PAGE_SIZE;
	int rv;
	int i;
	u8 ack;
	u64 rn;
	struct w1_reg_num *reg_num;
	int seq = 0;

	mutex_lock(&sl->master->bus_mutex);
	/* Place all devices in CHAIN state */
	if (w1_reset_bus(sl->master))
		goto error;
	w1_write_8(sl->master, W1_SKIP_ROM);
	w1_write_8(sl->master, W1_42_CHAIN);
	w1_write_8(sl->master, W1_42_CHAIN_ON);
	w1_write_8(sl->master, W1_42_CHAIN_ON_INV);
	msleep(sl->master->pullup_duration);

	/* check for acknowledgment */
	ack = w1_read_8(sl->master);
	if (ack != W1_42_SUCCESS_CONFIRM_BYTE)
		goto error;

	/* In case the bus fails to send 0xFF, limit */
	for (i = 0; i <= 64; i++) {
		if (w1_reset_bus(sl->master))
			goto error;

		w1_write_8(sl->master, W1_42_COND_READ);
		rv = w1_read_block(sl->master, (u8 *)&rn, 8);
		reg_num = (struct w1_reg_num *) &rn;
		if (reg_num->family == W1_42_FINISHED_BYTE)
			break;
		if (sl->reg_num.id == reg_num->id)
			seq = i;

		w1_write_8(sl->master, W1_42_CHAIN);
		w1_write_8(sl->master, W1_42_CHAIN_DONE);
		w1_write_8(sl->master, W1_42_CHAIN_DONE_INV);
		w1_read_block(sl->master, &ack, sizeof(ack));

		/* check for acknowledgment */
		ack = w1_read_8(sl->master);
		if (ack != W1_42_SUCCESS_CONFIRM_BYTE)
			goto error;

	}

	/* Exit from CHAIN state */
	if (w1_reset_bus(sl->master))
		goto error;
	w1_write_8(sl->master, W1_SKIP_ROM);
	w1_write_8(sl->master, W1_42_CHAIN);
	w1_write_8(sl->master, W1_42_CHAIN_OFF);
	w1_write_8(sl->master, W1_42_CHAIN_OFF_INV);

	/* check for acknowledgment */
	ack = w1_read_8(sl->master);
	if (ack != W1_42_SUCCESS_CONFIRM_BYTE)
		goto error;
	mutex_unlock(&sl->master->bus_mutex);

	c -= snprintf(buf + PAGE_SIZE - c, c, "%d\n", seq);
	return PAGE_SIZE - c;
error:
	mutex_unlock(&sl->master->bus_mutex);
	return -EIO;
}

static int __init w1_therm_init(void)
{
	int err, i;

	for (i = 0; i < ARRAY_SIZE(w1_therm_families); ++i) {
		err = w1_register_family(w1_therm_families[i].f);
		if (err)
			w1_therm_families[i].broken = 1;
	}

	return 0;
}

static void __exit w1_therm_fini(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(w1_therm_families); ++i)
		if (!w1_therm_families[i].broken)
			w1_unregister_family(w1_therm_families[i].f);
}

module_init(w1_therm_init);
module_exit(w1_therm_fini);

MODULE_AUTHOR("Evgeniy Polyakov <zbr@ioremap.net>");
MODULE_DESCRIPTION("Driver for 1-wire Dallas network protocol, temperature family.");
MODULE_LICENSE("GPL");
MODULE_ALIAS("w1-family-" __stringify(W1_THERM_DS18S20));
MODULE_ALIAS("w1-family-" __stringify(W1_THERM_DS1822));
MODULE_ALIAS("w1-family-" __stringify(W1_THERM_DS18B20));
MODULE_ALIAS("w1-family-" __stringify(W1_THERM_DS1825));
MODULE_ALIAS("w1-family-" __stringify(W1_THERM_DS28EA00));
