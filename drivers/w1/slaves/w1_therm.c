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

/* Nb of try for an operation */
#define W1_THERM_MAX_TRY		5

/* ms delay to retry bus mutex */
#define W1_THERM_RETRY_DELAY		20

/* Helpers Macros */

/*
 * return the power mode of the sl slave : 1-ext, 0-parasite, <0 unknown
 * always test family data existence before using this macro
 */
#define SLAVE_POWERMODE(sl) \
	(((struct w1_therm_family_data *)(sl->family_data))->external_powered)

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
 * @precision: pointer to the device precision function
 * @eeprom: pointer to eeprom function
 */
struct w1_therm_family_converter {
	u8		broken;
	u16		reserved;
	struct w1_family	*f;
	int		(*convert)(u8 rom[9]);
	int		(*precision)(struct device *device, int val);
	int		(*eeprom)(struct device *device);
};

/**
 * struct w1_therm_family_data - device data
 * @rom: ROM device id (64bit Lasered ROM code + 1 CRC byte)
 * @refcnt: ref count
 * @external_powered:	1 device powered externally,
 *				0 device parasite powered,
 *				-x error or undefined
 */
struct w1_therm_family_data {
	uint8_t rom[9];
	atomic_t refcnt;
	int external_powered;
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

/* Sysfs interface declaration */

static ssize_t w1_slave_show(struct device *device,
	struct device_attribute *attr, char *buf);

static ssize_t w1_slave_store(struct device *device,
	struct device_attribute *attr, const char *buf, size_t size);

static ssize_t w1_seq_show(struct device *device,
	struct device_attribute *attr, char *buf);

static ssize_t ext_power_show(struct device *device,
	struct device_attribute *attr, char *buf);

/* Attributes declarations */

static DEVICE_ATTR_RW(w1_slave);
static DEVICE_ATTR_RO(w1_seq);
static DEVICE_ATTR_RO(ext_power);

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
	&dev_attr_ext_power.attr,
	NULL,
};

static struct attribute *w1_ds28ea00_attrs[] = {
	&dev_attr_w1_slave.attr,
	&dev_attr_w1_seq.attr,
	&dev_attr_ext_power.attr,
	NULL,
};

/* Attribute groups */

ATTRIBUTE_GROUPS(w1_therm);
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

static struct w1_family_ops w1_ds28ea00_fops = {
	.add_slave	= w1_therm_add_slave,
	.remove_slave	= w1_therm_remove_slave,
	.groups		= w1_ds28ea00_groups,
	.chip_info	= W1_CHIPINFO,
};

/* Family binding operations struct */

static struct w1_family w1_therm_family_DS18S20 = {
	.fid = W1_THERM_DS18S20,
	.fops = &w1_therm_fops,
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

/* write configuration to eeprom */
static inline int w1_therm_eeprom(struct device *device);

/* DS18S20 does not feature configuration register */
static inline int w1_DS18S20_precision(struct device *device, int val)
{
	return 0;
}

/* Set precision for conversion */
static inline int w1_DS18B20_precision(struct device *device, int val)
{
	struct w1_slave *sl = dev_to_w1_slave(device);
	struct w1_master *dev = sl->master;
	u8 rom[9], crc;
	int ret, max_trying = 10;
	u8 *family_data = sl->family_data;
	uint8_t precision_bits;
	uint8_t mask = 0x60;

	if (val > 12 || val < 9) {
		pr_warn("Unsupported precision\n");
		ret = -EINVAL;
		goto error;
	}

	if (!sl->family_data) {
		ret = -ENODEV;
		goto error;
	}

	/* prevent the slave from going away in sleep */
	atomic_inc(THERM_REFCNT(family_data));

	ret = mutex_lock_interruptible(&dev->bus_mutex);
	if (ret != 0)
		goto dec_refcnt;

	memset(rom, 0, sizeof(rom));

	/* translate precision to bitmask (see datasheet page 9) */
	switch (val) {
	case 9:
		precision_bits = 0x00;
		break;
	case 10:
		precision_bits = 0x20;
		break;
	case 11:
		precision_bits = 0x40;
		break;
	case 12:
	default:
		precision_bits = 0x60;
		break;
	}

	while (max_trying--) {
		crc = 0;

		if (!reset_select_slave(sl)) {
			int count = 0;

			/* read values to only alter precision bits */
			w1_write_8(dev, W1_READ_SCRATCHPAD);
			count = w1_read_block(dev, rom, 9);
			if (count != 9)
				dev_warn(device, "w1_read_block() returned %u instead of 9.\n",	count);

			crc = w1_calc_crc8(rom, 8);
			if (rom[8] == crc) {
				rom[4] = (rom[4] & ~mask) | (precision_bits & mask);

				if (!reset_select_slave(sl)) {
					w1_write_8(dev, W1_WRITE_SCRATCHPAD);
					w1_write_8(dev, rom[2]);
					w1_write_8(dev, rom[3]);
					w1_write_8(dev, rom[4]);

					break;
				}
			}
		}
	}

	mutex_unlock(&dev->bus_mutex);
dec_refcnt:
	atomic_dec(THERM_REFCNT(family_data));
error:
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

	if (!rom[7])
		return 0;

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
		.f		= &w1_therm_family_DS18S20,
		.convert	= w1_DS18S20_convert_temp,
		.precision	= w1_DS18S20_precision,
		.eeprom		= w1_therm_eeprom
	},
	{
		.f		= &w1_therm_family_DS1822,
		.convert	= w1_DS18B20_convert_temp,
		.precision	= w1_DS18S20_precision,
		.eeprom		= w1_therm_eeprom
	},
	{
		.f		= &w1_therm_family_DS18B20,
		.convert	= w1_DS18B20_convert_temp,
		.precision	= w1_DS18B20_precision,
		.eeprom		= w1_therm_eeprom
	},
	{
		.f		= &w1_therm_family_DS28EA00,
		.convert	= w1_DS18B20_convert_temp,
		.precision	= w1_DS18S20_precision,
		.eeprom		= w1_therm_eeprom
	},
	{
		.f		= &w1_therm_family_DS1825,
		.convert	= w1_DS18B20_convert_temp,
		.precision	= w1_DS18S20_precision,
		.eeprom		= w1_therm_eeprom
	}
};

/* Helpers Functions */

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
 * w1_convert_temp() - temperature conversion binding function
 * @rom: data read from device RAM (8 data bytes + 1 CRC byte)
 * @fid: device family id
 *
 * The function call the temperature computation function according to
 * device family.
 *
 * Return: value in millidegrees Celsius.
 */
static inline int w1_convert_temp(u8 rom[9], u8 fid)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(w1_therm_families); ++i)
		if (w1_therm_families[i].f->fid == fid)
			return w1_therm_families[i].convert(rom);

	return 0;
}

/* Interface Functions */

static int w1_therm_add_slave(struct w1_slave *sl)
{
	sl->family_data = kzalloc(sizeof(struct w1_therm_family_data),
		GFP_KERNEL);
	if (!sl->family_data)
		return -ENOMEM;

	atomic_set(THERM_REFCNT(sl->family_data), 1);

	/* Getting the power mode of the device {external, parasite} */
	SLAVE_POWERMODE(sl) = read_powermode(sl);

	if (SLAVE_POWERMODE(sl) < 0) {
		/* no error returned as device has been added */
		dev_warn(&sl->dev,
			"%s: Device has been added, but power_mode may be corrupted. err=%d\n",
			 __func__, SLAVE_POWERMODE(sl));
	}

	return 0;
}

static void w1_therm_remove_slave(struct w1_slave *sl)
{
	int refcnt = atomic_sub_return(1, THERM_REFCNT(sl->family_data));

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

static ssize_t read_therm(struct device *device,
			  struct w1_slave *sl, struct therm_info *info)
{
	struct w1_master *dev = sl->master;
	u8 external_power;
	int ret, max_trying = 10;
	u8 *family_data = sl->family_data;

	if (!family_data) {
		ret = -ENODEV;
		goto error;
	}

	/* prevent the slave from going away in sleep */
	atomic_inc(THERM_REFCNT(family_data));

	ret = mutex_lock_interruptible(&dev->bus_mutex);
	if (ret != 0)
		goto dec_refcnt;

	memset(info->rom, 0, sizeof(info->rom));

	while (max_trying--) {

		info->verdict = 0;
		info->crc = 0;

		if (!reset_select_slave(sl)) {
			int count = 0;
			unsigned int tm = 750;
			unsigned long sleep_rem;

			w1_write_8(dev, W1_READ_PSUPPLY);
			external_power = w1_read_8(dev);

			if (reset_select_slave(sl))
				continue;

			/* 750ms strong pullup (or delay) after the convert */
			if (w1_strong_pullup == 2 ||
					(!external_power && w1_strong_pullup))
				w1_next_pullup(dev, tm);

			w1_write_8(dev, W1_CONVERT_TEMP);

			if (external_power) {
				mutex_unlock(&dev->bus_mutex);

				sleep_rem = msleep_interruptible(tm);
				if (sleep_rem != 0) {
					ret = -EINTR;
					goto dec_refcnt;
				}

				ret = mutex_lock_interruptible(&dev->bus_mutex);
				if (ret != 0)
					goto dec_refcnt;
			} else if (!w1_strong_pullup) {
				sleep_rem = msleep_interruptible(tm);
				if (sleep_rem != 0) {
					ret = -EINTR;
					goto mt_unlock;
				}
			}

			if (!reset_select_slave(sl)) {

				w1_write_8(dev, W1_READ_SCRATCHPAD);
				count = w1_read_block(dev, info->rom, 9);
				if (count != 9) {
					dev_warn(device, "w1_read_block() "
						"returned %u instead of 9.\n",
						count);
				}

				info->crc = w1_calc_crc8(info->rom, 8);

				if (info->rom[8] == info->crc)
					info->verdict = 1;
			}
		}

		if (info->verdict)
			break;
	}

mt_unlock:
	mutex_unlock(&dev->bus_mutex);
dec_refcnt:
	atomic_dec(THERM_REFCNT(family_data));
error:
	return ret;
}

static inline int w1_therm_eeprom(struct device *device)
{
	struct w1_slave *sl = dev_to_w1_slave(device);
	struct w1_master *dev = sl->master;
	u8 rom[9], external_power;
	int ret, max_trying = 10;
	u8 *family_data = sl->family_data;

	if (!sl->family_data) {
		ret = -ENODEV;
		goto error;
	}

	/* prevent the slave from going away in sleep */
	atomic_inc(THERM_REFCNT(family_data));

	ret = mutex_lock_interruptible(&dev->bus_mutex);
	if (ret != 0)
		goto dec_refcnt;

	memset(rom, 0, sizeof(rom));

	while (max_trying--) {
		if (!reset_select_slave(sl)) {
			unsigned int tm = 10;
			unsigned long sleep_rem;

			/* check if in parasite mode */
			w1_write_8(dev, W1_READ_PSUPPLY);
			external_power = w1_read_8(dev);

			if (reset_select_slave(sl))
				continue;

			/* 10ms strong pullup/delay after the copy command */
			if (w1_strong_pullup == 2 ||
			    (!external_power && w1_strong_pullup))
				w1_next_pullup(dev, tm);

			w1_write_8(dev, W1_COPY_SCRATCHPAD);

			if (external_power) {
				mutex_unlock(&dev->bus_mutex);

				sleep_rem = msleep_interruptible(tm);
				if (sleep_rem != 0) {
					ret = -EINTR;
					goto dec_refcnt;
				}

				ret = mutex_lock_interruptible(&dev->bus_mutex);
				if (ret != 0)
					goto dec_refcnt;
			} else if (!w1_strong_pullup) {
				sleep_rem = msleep_interruptible(tm);
				if (sleep_rem != 0) {
					ret = -EINTR;
					goto mt_unlock;
				}
			}

			break;
		}
	}

mt_unlock:
	mutex_unlock(&dev->bus_mutex);
dec_refcnt:
	atomic_dec(THERM_REFCNT(family_data));
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

/* Sysfs Interface definition */

static ssize_t w1_slave_show(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	struct w1_slave *sl = dev_to_w1_slave(device);
	struct therm_info info;
	u8 *family_data = sl->family_data;
	int ret, i;
	ssize_t c = PAGE_SIZE;
	u8 fid = sl->family->fid;

	ret = read_therm(device, sl, &info);
	if (ret)
		return ret;

	for (i = 0; i < 9; ++i)
		c -= snprintf(buf + PAGE_SIZE - c, c, "%02x ", info.rom[i]);
	c -= snprintf(buf + PAGE_SIZE - c, c, ": crc=%02x %s\n",
		      info.crc, (info.verdict) ? "YES" : "NO");
	if (info.verdict)
		memcpy(family_data, info.rom, sizeof(info.rom));
	else
		dev_warn(device, "Read failed CRC check\n");

	for (i = 0; i < 9; ++i)
		c -= snprintf(buf + PAGE_SIZE - c, c, "%02x ",
			      ((u8 *)family_data)[i]);

	c -= snprintf(buf + PAGE_SIZE - c, c, "t=%d\n",
			w1_convert_temp(info.rom, fid));
	ret = PAGE_SIZE - c;
	return ret;
}

static ssize_t w1_slave_store(struct device *device,
			      struct device_attribute *attr, const char *buf,
			      size_t size)
{
	int val, ret;
	struct w1_slave *sl = dev_to_w1_slave(device);
	int i;

	ret = kstrtoint(buf, 0, &val);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(w1_therm_families); ++i) {
		if (w1_therm_families[i].f->fid == sl->family->fid) {
	/* zero value indicates to write current configuration to eeprom */
			if (val == 0)
				ret = w1_therm_families[i].eeprom(device);
			else
				ret = w1_therm_families[i].precision(device,
									val);
			break;
		}
	}
	return ret ? : size;
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

#if IS_REACHABLE(CONFIG_HWMON)
static int w1_read_temp(struct device *device, u32 attr, int channel,
			long *val)
{
	struct w1_slave *sl = dev_get_drvdata(device);
	struct therm_info info;
	u8 fid = sl->family->fid;
	int ret;

	switch (attr) {
	case hwmon_temp_input:
		ret = read_therm(device, sl, &info);
		if (ret)
			return ret;

		if (!info.verdict) {
			ret = -EIO;
			return ret;
		}

		*val = w1_convert_temp(info.rom, fid);
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
