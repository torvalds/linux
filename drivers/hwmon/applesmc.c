/*
 * drivers/hwmon/applesmc.c - driver for Apple's SMC (accelerometer, temperature
 * sensors, fan control, keyboard backlight control) used in Intel-based Apple
 * computers.
 *
 * Copyright (C) 2007 Nicolas Boichat <nicolas@boichat.ch>
 *
 * Based on hdaps.c driver:
 * Copyright (C) 2005 Robert Love <rml@novell.com>
 * Copyright (C) 2005 Jesper Juhl <jesper.juhl@gmail.com>
 *
 * Fan control based on smcFanControl:
 * Copyright (C) 2006 Hendrik Holtmann <holtmann@mac.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/dmi.h>
#include <linux/mutex.h>
#include <linux/hwmon-sysfs.h>
#include <asm/io.h>
#include <linux/leds.h>
#include <linux/hwmon.h>
#include <linux/workqueue.h>

/* data port used by Apple SMC */
#define APPLESMC_DATA_PORT	0x300
/* command/status port used by Apple SMC */
#define APPLESMC_CMD_PORT	0x304

#define APPLESMC_NR_PORTS	32 /* 0x300-0x31f */

#define APPLESMC_MAX_DATA_LENGTH 32

#define APPLESMC_STATUS_MASK	0x0f
#define APPLESMC_READ_CMD	0x10
#define APPLESMC_WRITE_CMD	0x11
#define APPLESMC_GET_KEY_BY_INDEX_CMD	0x12
#define APPLESMC_GET_KEY_TYPE_CMD	0x13

#define KEY_COUNT_KEY		"#KEY" /* r-o ui32 */

#define LIGHT_SENSOR_LEFT_KEY	"ALV0" /* r-o {alv (6 bytes) */
#define LIGHT_SENSOR_RIGHT_KEY	"ALV1" /* r-o {alv (6 bytes) */
#define BACKLIGHT_KEY 		"LKSB" /* w-o {lkb (2 bytes) */

#define CLAMSHELL_KEY 		"MSLD" /* r-o ui8 (unused) */

#define MOTION_SENSOR_X_KEY	"MO_X" /* r-o sp78 (2 bytes) */
#define MOTION_SENSOR_Y_KEY	"MO_Y" /* r-o sp78 (2 bytes) */
#define MOTION_SENSOR_Z_KEY	"MO_Z" /* r-o sp78 (2 bytes) */
#define MOTION_SENSOR_KEY	"MOCN" /* r/w ui16 */

#define FANS_COUNT		"FNum" /* r-o ui8 */
#define FANS_MANUAL		"FS! " /* r-w ui16 */
#define FAN_ACTUAL_SPEED	"F0Ac" /* r-o fpe2 (2 bytes) */
#define FAN_MIN_SPEED		"F0Mn" /* r-o fpe2 (2 bytes) */
#define FAN_MAX_SPEED		"F0Mx" /* r-o fpe2 (2 bytes) */
#define FAN_SAFE_SPEED		"F0Sf" /* r-o fpe2 (2 bytes) */
#define FAN_TARGET_SPEED	"F0Tg" /* r-w fpe2 (2 bytes) */
#define FAN_POSITION		"F0ID" /* r-o char[16] */

/*
 * Temperature sensors keys (sp78 - 2 bytes).
 * First set for Macbook(Pro), second for Macmini.
 */
static const char* temperature_sensors_sets[][13] = {
	{ "TA0P", "TB0T", "TC0D", "TC0P", "TG0H", "TG0P", "TG0T", "Th0H",
	  "Th1H", "Tm0P", "Ts0P", "Ts1P", NULL },
	{ "TC0D", "TC0P", NULL }
};

/* List of keys used to read/write fan speeds */
static const char* fan_speed_keys[] = {
	FAN_ACTUAL_SPEED,
	FAN_MIN_SPEED,
	FAN_MAX_SPEED,
	FAN_SAFE_SPEED,
	FAN_TARGET_SPEED
};

#define INIT_TIMEOUT_MSECS	5000	/* wait up to 5s for device init ... */
#define INIT_WAIT_MSECS		50	/* ... in 50ms increments */

#define APPLESMC_POLL_PERIOD	(HZ/20)	/* poll for input every 1/20s */
#define APPLESMC_INPUT_FUZZ	4	/* input event threshold */
#define APPLESMC_INPUT_FLAT	4

#define SENSOR_X 0
#define SENSOR_Y 1
#define SENSOR_Z 2

/* Structure to be passed to DMI_MATCH function */
struct dmi_match_data {
/* Indicates whether this computer has an accelerometer. */
	int accelerometer;
/* Indicates whether this computer has light sensors and keyboard backlight. */
	int light;
/* Indicates which temperature sensors set to use. */
	int temperature_set;
};

static const int debug;
static struct platform_device *pdev;
static s16 rest_x;
static s16 rest_y;
static struct timer_list applesmc_timer;
static struct input_dev *applesmc_idev;
static struct class_device *hwmon_class_dev;

/* Indicates whether this computer has an accelerometer. */
static unsigned int applesmc_accelerometer;

/* Indicates whether this computer has light sensors and keyboard backlight. */
static unsigned int applesmc_light;

/* Indicates which temperature sensors set to use. */
static unsigned int applesmc_temperature_set;

static struct mutex applesmc_lock;

/*
 * Last index written to key_at_index sysfs file, and value to use for all other
 * key_at_index_* sysfs files.
 */
static unsigned int key_at_index;

static struct workqueue_struct *applesmc_led_wq;

/*
 * __wait_status - Wait up to 2ms for the status port to get a certain value
 * (masked with 0x0f), returning zero if the value is obtained.  Callers must
 * hold applesmc_lock.
 */
static int __wait_status(u8 val)
{
	unsigned int i;

	val = val & APPLESMC_STATUS_MASK;

	for (i = 0; i < 200; i++) {
		if ((inb(APPLESMC_CMD_PORT) & APPLESMC_STATUS_MASK) == val) {
			if (debug)
				printk(KERN_DEBUG
						"Waited %d us for status %x\n",
						i*10, val);
			return 0;
		}
		udelay(10);
	}

	printk(KERN_WARNING "applesmc: wait status failed: %x != %x\n",
						val, inb(APPLESMC_CMD_PORT));

	return -EIO;
}

/*
 * applesmc_read_key - reads len bytes from a given key, and put them in buffer.
 * Returns zero on success or a negative error on failure. Callers must
 * hold applesmc_lock.
 */
static int applesmc_read_key(const char* key, u8* buffer, u8 len)
{
	int i;

	if (len > APPLESMC_MAX_DATA_LENGTH) {
		printk(KERN_ERR	"applesmc_read_key: cannot read more than "
					"%d bytes\n", APPLESMC_MAX_DATA_LENGTH);
		return -EINVAL;
	}

	outb(APPLESMC_READ_CMD, APPLESMC_CMD_PORT);
	if (__wait_status(0x0c))
		return -EIO;

	for (i = 0; i < 4; i++) {
		outb(key[i], APPLESMC_DATA_PORT);
		if (__wait_status(0x04))
			return -EIO;
	}
	if (debug)
		printk(KERN_DEBUG "<%s", key);

	outb(len, APPLESMC_DATA_PORT);
	if (debug)
		printk(KERN_DEBUG ">%x", len);

	for (i = 0; i < len; i++) {
		if (__wait_status(0x05))
			return -EIO;
		buffer[i] = inb(APPLESMC_DATA_PORT);
		if (debug)
			printk(KERN_DEBUG "<%x", buffer[i]);
	}
	if (debug)
		printk(KERN_DEBUG "\n");

	return 0;
}

/*
 * applesmc_write_key - writes len bytes from buffer to a given key.
 * Returns zero on success or a negative error on failure. Callers must
 * hold applesmc_lock.
 */
static int applesmc_write_key(const char* key, u8* buffer, u8 len)
{
	int i;

	if (len > APPLESMC_MAX_DATA_LENGTH) {
		printk(KERN_ERR	"applesmc_write_key: cannot write more than "
					"%d bytes\n", APPLESMC_MAX_DATA_LENGTH);
		return -EINVAL;
	}

	outb(APPLESMC_WRITE_CMD, APPLESMC_CMD_PORT);
	if (__wait_status(0x0c))
		return -EIO;

	for (i = 0; i < 4; i++) {
		outb(key[i], APPLESMC_DATA_PORT);
		if (__wait_status(0x04))
			return -EIO;
	}

	outb(len, APPLESMC_DATA_PORT);

	for (i = 0; i < len; i++) {
		if (__wait_status(0x04))
			return -EIO;
		outb(buffer[i], APPLESMC_DATA_PORT);
	}

	return 0;
}

/*
 * applesmc_get_key_at_index - get key at index, and put the result in key
 * (char[6]). Returns zero on success or a negative error on failure. Callers
 * must hold applesmc_lock.
 */
static int applesmc_get_key_at_index(int index, char* key)
{
	int i;
	u8 readkey[4];
	readkey[0] = index >> 24;
	readkey[1] = index >> 16;
	readkey[2] = index >> 8;
	readkey[3] = index;

	outb(APPLESMC_GET_KEY_BY_INDEX_CMD, APPLESMC_CMD_PORT);
	if (__wait_status(0x0c))
		return -EIO;

	for (i = 0; i < 4; i++) {
		outb(readkey[i], APPLESMC_DATA_PORT);
		if (__wait_status(0x04))
			return -EIO;
	}

	outb(4, APPLESMC_DATA_PORT);

	for (i = 0; i < 4; i++) {
		if (__wait_status(0x05))
			return -EIO;
		key[i] = inb(APPLESMC_DATA_PORT);
	}
	key[4] = 0;

	return 0;
}

/*
 * applesmc_get_key_type - get key type, and put the result in type (char[6]).
 * Returns zero on success or a negative error on failure. Callers must
 * hold applesmc_lock.
 */
static int applesmc_get_key_type(char* key, char* type)
{
	int i;

	outb(APPLESMC_GET_KEY_TYPE_CMD, APPLESMC_CMD_PORT);
	if (__wait_status(0x0c))
		return -EIO;

	for (i = 0; i < 4; i++) {
		outb(key[i], APPLESMC_DATA_PORT);
		if (__wait_status(0x04))
			return -EIO;
	}

	outb(5, APPLESMC_DATA_PORT);

	for (i = 0; i < 6; i++) {
		if (__wait_status(0x05))
			return -EIO;
		type[i] = inb(APPLESMC_DATA_PORT);
	}
	type[5] = 0;

	return 0;
}

/*
 * applesmc_read_motion_sensor - Read motion sensor (X, Y or Z). Callers must
 * hold applesmc_lock.
 */
static int applesmc_read_motion_sensor(int index, s16* value)
{
	u8 buffer[2];
	int ret;

	switch (index) {
	case SENSOR_X:
		ret = applesmc_read_key(MOTION_SENSOR_X_KEY, buffer, 2);
		break;
	case SENSOR_Y:
		ret = applesmc_read_key(MOTION_SENSOR_Y_KEY, buffer, 2);
		break;
	case SENSOR_Z:
		ret = applesmc_read_key(MOTION_SENSOR_Z_KEY, buffer, 2);
		break;
	default:
		ret = -EINVAL;
	}

	*value = ((s16)buffer[0] << 8) | buffer[1];

	return ret;
}

/*
 * applesmc_device_init - initialize the accelerometer.  Returns zero on success
 * and negative error code on failure.  Can sleep.
 */
static int applesmc_device_init(void)
{
	int total, ret = -ENXIO;
	u8 buffer[2];

	if (!applesmc_accelerometer)
		return 0;

	mutex_lock(&applesmc_lock);

	for (total = INIT_TIMEOUT_MSECS; total > 0; total -= INIT_WAIT_MSECS) {
		if (debug)
			printk(KERN_DEBUG "applesmc try %d\n", total);
		if (!applesmc_read_key(MOTION_SENSOR_KEY, buffer, 2) &&
				(buffer[0] != 0x00 || buffer[1] != 0x00)) {
			if (total == INIT_TIMEOUT_MSECS) {
				printk(KERN_DEBUG "applesmc: device has"
						" already been initialized"
						" (0x%02x, 0x%02x).\n",
						buffer[0], buffer[1]);
			} else {
				printk(KERN_DEBUG "applesmc: device"
						" successfully initialized"
						" (0x%02x, 0x%02x).\n",
						buffer[0], buffer[1]);
			}
			ret = 0;
			goto out;
		}
		buffer[0] = 0xe0;
		buffer[1] = 0x00;
		applesmc_write_key(MOTION_SENSOR_KEY, buffer, 2);
		msleep(INIT_WAIT_MSECS);
	}

	printk(KERN_WARNING "applesmc: failed to init the device\n");

out:
	mutex_unlock(&applesmc_lock);
	return ret;
}

/*
 * applesmc_get_fan_count - get the number of fans. Callers must NOT hold
 * applesmc_lock.
 */
static int applesmc_get_fan_count(void)
{
	int ret;
	u8 buffer[1];

	mutex_lock(&applesmc_lock);

	ret = applesmc_read_key(FANS_COUNT, buffer, 1);

	mutex_unlock(&applesmc_lock);
	if (ret)
		return ret;
	else
		return buffer[0];
}

/* Device model stuff */
static int applesmc_probe(struct platform_device *dev)
{
	int ret;

	ret = applesmc_device_init();
	if (ret)
		return ret;

	printk(KERN_INFO "applesmc: device successfully initialized.\n");
	return 0;
}

static int applesmc_resume(struct platform_device *dev)
{
	return applesmc_device_init();
}

static struct platform_driver applesmc_driver = {
	.probe = applesmc_probe,
	.resume = applesmc_resume,
	.driver	= {
		.name = "applesmc",
		.owner = THIS_MODULE,
	},
};

/*
 * applesmc_calibrate - Set our "resting" values.  Callers must
 * hold applesmc_lock.
 */
static void applesmc_calibrate(void)
{
	applesmc_read_motion_sensor(SENSOR_X, &rest_x);
	applesmc_read_motion_sensor(SENSOR_Y, &rest_y);
	rest_x = -rest_x;
}

static int applesmc_idev_open(struct input_dev *dev)
{
	add_timer(&applesmc_timer);

	return 0;
}

static void applesmc_idev_close(struct input_dev *dev)
{
	del_timer_sync(&applesmc_timer);
}

static void applesmc_idev_poll(unsigned long unused)
{
	s16 x, y;

	/* Cannot sleep.  Try nonblockingly.  If we fail, try again later. */
	if (!mutex_trylock(&applesmc_lock)) {
		mod_timer(&applesmc_timer, jiffies + APPLESMC_POLL_PERIOD);
		return;
	}

	if (applesmc_read_motion_sensor(SENSOR_X, &x))
		goto out;
	if (applesmc_read_motion_sensor(SENSOR_Y, &y))
		goto out;

	x = -x;
	input_report_abs(applesmc_idev, ABS_X, x - rest_x);
	input_report_abs(applesmc_idev, ABS_Y, y - rest_y);
	input_sync(applesmc_idev);

out:
	mod_timer(&applesmc_timer, jiffies + APPLESMC_POLL_PERIOD);

	mutex_unlock(&applesmc_lock);
}

/* Sysfs Files */

static ssize_t applesmc_position_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int ret;
	s16 x, y, z;

	mutex_lock(&applesmc_lock);

	ret = applesmc_read_motion_sensor(SENSOR_X, &x);
	if (ret)
		goto out;
	ret = applesmc_read_motion_sensor(SENSOR_Y, &y);
	if (ret)
		goto out;
	ret = applesmc_read_motion_sensor(SENSOR_Z, &z);
	if (ret)
		goto out;

out:
	mutex_unlock(&applesmc_lock);
	if (ret)
		return ret;
	else
		return snprintf(buf, PAGE_SIZE, "(%d,%d,%d)\n", x, y, z);
}

static ssize_t applesmc_light_show(struct device *dev,
				struct device_attribute *attr, char *sysfsbuf)
{
	int ret;
	u8 left = 0, right = 0;
	u8 buffer[6];

	mutex_lock(&applesmc_lock);

	ret = applesmc_read_key(LIGHT_SENSOR_LEFT_KEY, buffer, 6);
	left = buffer[2];
	if (ret)
		goto out;
	ret = applesmc_read_key(LIGHT_SENSOR_RIGHT_KEY, buffer, 6);
	right = buffer[2];

out:
	mutex_unlock(&applesmc_lock);
	if (ret)
		return ret;
	else
		return snprintf(sysfsbuf, PAGE_SIZE, "(%d,%d)\n", left, right);
}

/* Displays degree Celsius * 1000 */
static ssize_t applesmc_show_temperature(struct device *dev,
			struct device_attribute *devattr, char *sysfsbuf)
{
	int ret;
	u8 buffer[2];
	unsigned int temp;
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	const char* key =
		temperature_sensors_sets[applesmc_temperature_set][attr->index];

	mutex_lock(&applesmc_lock);

	ret = applesmc_read_key(key, buffer, 2);
	temp = buffer[0]*1000;
	temp += (buffer[1] >> 6) * 250;

	mutex_unlock(&applesmc_lock);

	if (ret)
		return ret;
	else
		return snprintf(sysfsbuf, PAGE_SIZE, "%u\n", temp);
}

static ssize_t applesmc_show_fan_speed(struct device *dev,
				struct device_attribute *attr, char *sysfsbuf)
{
	int ret;
	unsigned int speed = 0;
	char newkey[5];
	u8 buffer[2];
	struct sensor_device_attribute_2 *sensor_attr =
						to_sensor_dev_attr_2(attr);

	newkey[0] = fan_speed_keys[sensor_attr->nr][0];
	newkey[1] = '0' + sensor_attr->index;
	newkey[2] = fan_speed_keys[sensor_attr->nr][2];
	newkey[3] = fan_speed_keys[sensor_attr->nr][3];
	newkey[4] = 0;

	mutex_lock(&applesmc_lock);

	ret = applesmc_read_key(newkey, buffer, 2);
	speed = ((buffer[0] << 8 | buffer[1]) >> 2);

	mutex_unlock(&applesmc_lock);
	if (ret)
		return ret;
	else
		return snprintf(sysfsbuf, PAGE_SIZE, "%u\n", speed);
}

static ssize_t applesmc_store_fan_speed(struct device *dev,
					struct device_attribute *attr,
					const char *sysfsbuf, size_t count)
{
	int ret;
	u32 speed;
	char newkey[5];
	u8 buffer[2];
	struct sensor_device_attribute_2 *sensor_attr =
						to_sensor_dev_attr_2(attr);

	speed = simple_strtoul(sysfsbuf, NULL, 10);

	if (speed > 0x4000) /* Bigger than a 14-bit value */
		return -EINVAL;

	newkey[0] = fan_speed_keys[sensor_attr->nr][0];
	newkey[1] = '0' + sensor_attr->index;
	newkey[2] = fan_speed_keys[sensor_attr->nr][2];
	newkey[3] = fan_speed_keys[sensor_attr->nr][3];
	newkey[4] = 0;

	mutex_lock(&applesmc_lock);

	buffer[0] = (speed >> 6) & 0xff;
	buffer[1] = (speed << 2) & 0xff;
	ret = applesmc_write_key(newkey, buffer, 2);

	mutex_unlock(&applesmc_lock);
	if (ret)
		return ret;
	else
		return count;
}

static ssize_t applesmc_show_fan_manual(struct device *dev,
			struct device_attribute *devattr, char *sysfsbuf)
{
	int ret;
	u16 manual = 0;
	u8 buffer[2];
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	mutex_lock(&applesmc_lock);

	ret = applesmc_read_key(FANS_MANUAL, buffer, 2);
	manual = ((buffer[0] << 8 | buffer[1]) >> attr->index) & 0x01;

	mutex_unlock(&applesmc_lock);
	if (ret)
		return ret;
	else
		return snprintf(sysfsbuf, PAGE_SIZE, "%d\n", manual);
}

static ssize_t applesmc_store_fan_manual(struct device *dev,
					 struct device_attribute *devattr,
					 const char *sysfsbuf, size_t count)
{
	int ret;
	u8 buffer[2];
	u32 input;
	u16 val;
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	input = simple_strtoul(sysfsbuf, NULL, 10);

	mutex_lock(&applesmc_lock);

	ret = applesmc_read_key(FANS_MANUAL, buffer, 2);
	val = (buffer[0] << 8 | buffer[1]);
	if (ret)
		goto out;

	if (input)
		val = val | (0x01 << attr->index);
	else
		val = val & ~(0x01 << attr->index);

	buffer[0] = (val >> 8) & 0xFF;
	buffer[1] = val & 0xFF;

	ret = applesmc_write_key(FANS_MANUAL, buffer, 2);

out:
	mutex_unlock(&applesmc_lock);
	if (ret)
		return ret;
	else
		return count;
}

static ssize_t applesmc_show_fan_position(struct device *dev,
				struct device_attribute *attr, char *sysfsbuf)
{
	int ret;
	char newkey[5];
	u8 buffer[17];
	struct sensor_device_attribute_2 *sensor_attr =
						to_sensor_dev_attr_2(attr);

	newkey[0] = FAN_POSITION[0];
	newkey[1] = '0' + sensor_attr->index;
	newkey[2] = FAN_POSITION[2];
	newkey[3] = FAN_POSITION[3];
	newkey[4] = 0;

	mutex_lock(&applesmc_lock);

	ret = applesmc_read_key(newkey, buffer, 16);
	buffer[16] = 0;

	mutex_unlock(&applesmc_lock);
	if (ret)
		return ret;
	else
		return snprintf(sysfsbuf, PAGE_SIZE, "%s\n", buffer+4);
}

static ssize_t applesmc_calibrate_show(struct device *dev,
				struct device_attribute *attr, char *sysfsbuf)
{
	return snprintf(sysfsbuf, PAGE_SIZE, "(%d,%d)\n", rest_x, rest_y);
}

static ssize_t applesmc_calibrate_store(struct device *dev,
	struct device_attribute *attr, const char *sysfsbuf, size_t count)
{
	mutex_lock(&applesmc_lock);
	applesmc_calibrate();
	mutex_unlock(&applesmc_lock);

	return count;
}

/* Store the next backlight value to be written by the work */
static unsigned int backlight_value;

static void applesmc_backlight_set(struct work_struct *work)
{
	u8 buffer[2];

	mutex_lock(&applesmc_lock);
	buffer[0] = backlight_value;
	buffer[1] = 0x00;
	applesmc_write_key(BACKLIGHT_KEY, buffer, 2);
	mutex_unlock(&applesmc_lock);
}
static DECLARE_WORK(backlight_work, &applesmc_backlight_set);

static void applesmc_brightness_set(struct led_classdev *led_cdev,
						enum led_brightness value)
{
	int ret;

	backlight_value = value;
	ret = queue_work(applesmc_led_wq, &backlight_work);

	if (debug && (!ret))
		printk(KERN_DEBUG "applesmc: work was already on the queue.\n");
}

static ssize_t applesmc_key_count_show(struct device *dev,
				struct device_attribute *attr, char *sysfsbuf)
{
	int ret;
	u8 buffer[4];
	u32 count;

	mutex_lock(&applesmc_lock);

	ret = applesmc_read_key(KEY_COUNT_KEY, buffer, 4);
	count = ((u32)buffer[0]<<24) + ((u32)buffer[1]<<16) +
						((u32)buffer[2]<<8) + buffer[3];

	mutex_unlock(&applesmc_lock);
	if (ret)
		return ret;
	else
		return snprintf(sysfsbuf, PAGE_SIZE, "%d\n", count);
}

static ssize_t applesmc_key_at_index_read_show(struct device *dev,
				struct device_attribute *attr, char *sysfsbuf)
{
	char key[5];
	char info[6];
	int ret;

	mutex_lock(&applesmc_lock);

	ret = applesmc_get_key_at_index(key_at_index, key);

	if (ret || !key[0]) {
		mutex_unlock(&applesmc_lock);

		return -EINVAL;
	}

	ret = applesmc_get_key_type(key, info);

	if (ret) {
		mutex_unlock(&applesmc_lock);

		return ret;
	}

	/*
	 * info[0] maximum value (APPLESMC_MAX_DATA_LENGTH) is much lower than
	 * PAGE_SIZE, so we don't need any checks before writing to sysfsbuf.
	 */
	ret = applesmc_read_key(key, sysfsbuf, info[0]);

	mutex_unlock(&applesmc_lock);

	if (!ret) {
		return info[0];
	}
	else {
		return ret;
	}
}

static ssize_t applesmc_key_at_index_data_length_show(struct device *dev,
				struct device_attribute *attr, char *sysfsbuf)
{
	char key[5];
	char info[6];
	int ret;

	mutex_lock(&applesmc_lock);

	ret = applesmc_get_key_at_index(key_at_index, key);

	if (ret || !key[0]) {
		mutex_unlock(&applesmc_lock);

		return -EINVAL;
	}

	ret = applesmc_get_key_type(key, info);

	mutex_unlock(&applesmc_lock);

	if (!ret)
		return snprintf(sysfsbuf, PAGE_SIZE, "%d\n", info[0]);
	else
		return ret;
}

static ssize_t applesmc_key_at_index_type_show(struct device *dev,
				struct device_attribute *attr, char *sysfsbuf)
{
	char key[5];
	char info[6];
	int ret;

	mutex_lock(&applesmc_lock);

	ret = applesmc_get_key_at_index(key_at_index, key);

	if (ret || !key[0]) {
		mutex_unlock(&applesmc_lock);

		return -EINVAL;
	}

	ret = applesmc_get_key_type(key, info);

	mutex_unlock(&applesmc_lock);

	if (!ret)
		return snprintf(sysfsbuf, PAGE_SIZE, "%s\n", info+1);
	else
		return ret;
}

static ssize_t applesmc_key_at_index_name_show(struct device *dev,
				struct device_attribute *attr, char *sysfsbuf)
{
	char key[5];
	int ret;

	mutex_lock(&applesmc_lock);

	ret = applesmc_get_key_at_index(key_at_index, key);

	mutex_unlock(&applesmc_lock);

	if (!ret && key[0])
		return snprintf(sysfsbuf, PAGE_SIZE, "%s\n", key);
	else
		return -EINVAL;
}

static ssize_t applesmc_key_at_index_show(struct device *dev,
				struct device_attribute *attr, char *sysfsbuf)
{
	return snprintf(sysfsbuf, PAGE_SIZE, "%d\n", key_at_index);
}

static ssize_t applesmc_key_at_index_store(struct device *dev,
	struct device_attribute *attr, const char *sysfsbuf, size_t count)
{
	mutex_lock(&applesmc_lock);

	key_at_index = simple_strtoul(sysfsbuf, NULL, 10);

	mutex_unlock(&applesmc_lock);

	return count;
}

static struct led_classdev applesmc_backlight = {
	.name			= "smc:kbd_backlight",
	.default_trigger	= "nand-disk",
	.brightness_set		= applesmc_brightness_set,
};

static DEVICE_ATTR(position, 0444, applesmc_position_show, NULL);
static DEVICE_ATTR(calibrate, 0644,
			applesmc_calibrate_show, applesmc_calibrate_store);

static struct attribute *accelerometer_attributes[] = {
	&dev_attr_position.attr,
	&dev_attr_calibrate.attr,
	NULL
};

static const struct attribute_group accelerometer_attributes_group =
	{ .attrs = accelerometer_attributes };

static DEVICE_ATTR(light, 0444, applesmc_light_show, NULL);

static DEVICE_ATTR(key_count, 0444, applesmc_key_count_show, NULL);
static DEVICE_ATTR(key_at_index, 0644,
		applesmc_key_at_index_show, applesmc_key_at_index_store);
static DEVICE_ATTR(key_at_index_name, 0444,
					applesmc_key_at_index_name_show, NULL);
static DEVICE_ATTR(key_at_index_type, 0444,
					applesmc_key_at_index_type_show, NULL);
static DEVICE_ATTR(key_at_index_data_length, 0444,
				applesmc_key_at_index_data_length_show, NULL);
static DEVICE_ATTR(key_at_index_data, 0444,
				applesmc_key_at_index_read_show, NULL);

static struct attribute *key_enumeration_attributes[] = {
	&dev_attr_key_count.attr,
	&dev_attr_key_at_index.attr,
	&dev_attr_key_at_index_name.attr,
	&dev_attr_key_at_index_type.attr,
	&dev_attr_key_at_index_data_length.attr,
	&dev_attr_key_at_index_data.attr,
	NULL
};

static const struct attribute_group key_enumeration_group =
	{ .attrs = key_enumeration_attributes };

/*
 * Macro defining SENSOR_DEVICE_ATTR for a fan sysfs entries.
 *  - show actual speed
 *  - show/store minimum speed
 *  - show maximum speed
 *  - show safe speed
 *  - show/store target speed
 *  - show/store manual mode
 */
#define sysfs_fan_speeds_offset(offset) \
static SENSOR_DEVICE_ATTR_2(fan##offset##_input, S_IRUGO, \
			applesmc_show_fan_speed, NULL, 0, offset-1); \
\
static SENSOR_DEVICE_ATTR_2(fan##offset##_min, S_IRUGO | S_IWUSR, \
	applesmc_show_fan_speed, applesmc_store_fan_speed, 1, offset-1); \
\
static SENSOR_DEVICE_ATTR_2(fan##offset##_max, S_IRUGO, \
			applesmc_show_fan_speed, NULL, 2, offset-1); \
\
static SENSOR_DEVICE_ATTR_2(fan##offset##_safe, S_IRUGO, \
			applesmc_show_fan_speed, NULL, 3, offset-1); \
\
static SENSOR_DEVICE_ATTR_2(fan##offset##_output, S_IRUGO | S_IWUSR, \
	applesmc_show_fan_speed, applesmc_store_fan_speed, 4, offset-1); \
\
static SENSOR_DEVICE_ATTR(fan##offset##_manual, S_IRUGO | S_IWUSR, \
	applesmc_show_fan_manual, applesmc_store_fan_manual, offset-1); \
\
static SENSOR_DEVICE_ATTR(fan##offset##_label, S_IRUGO, \
	applesmc_show_fan_position, NULL, offset-1); \
\
static struct attribute *fan##offset##_attributes[] = { \
	&sensor_dev_attr_fan##offset##_input.dev_attr.attr, \
	&sensor_dev_attr_fan##offset##_min.dev_attr.attr, \
	&sensor_dev_attr_fan##offset##_max.dev_attr.attr, \
	&sensor_dev_attr_fan##offset##_safe.dev_attr.attr, \
	&sensor_dev_attr_fan##offset##_output.dev_attr.attr, \
	&sensor_dev_attr_fan##offset##_manual.dev_attr.attr, \
	&sensor_dev_attr_fan##offset##_label.dev_attr.attr, \
	NULL \
};

/*
 * Create the needed functions for each fan using the macro defined above
 * (2 fans are supported)
 */
sysfs_fan_speeds_offset(1);
sysfs_fan_speeds_offset(2);

static const struct attribute_group fan_attribute_groups[] = {
	{ .attrs = fan1_attributes },
	{ .attrs = fan2_attributes }
};

/*
 * Temperature sensors sysfs entries.
 */
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO,
					applesmc_show_temperature, NULL, 0);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO,
					applesmc_show_temperature, NULL, 1);
static SENSOR_DEVICE_ATTR(temp3_input, S_IRUGO,
					applesmc_show_temperature, NULL, 2);
static SENSOR_DEVICE_ATTR(temp4_input, S_IRUGO,
					applesmc_show_temperature, NULL, 3);
static SENSOR_DEVICE_ATTR(temp5_input, S_IRUGO,
					applesmc_show_temperature, NULL, 4);
static SENSOR_DEVICE_ATTR(temp6_input, S_IRUGO,
					applesmc_show_temperature, NULL, 5);
static SENSOR_DEVICE_ATTR(temp7_input, S_IRUGO,
					applesmc_show_temperature, NULL, 6);
static SENSOR_DEVICE_ATTR(temp8_input, S_IRUGO,
					applesmc_show_temperature, NULL, 7);
static SENSOR_DEVICE_ATTR(temp9_input, S_IRUGO,
					applesmc_show_temperature, NULL, 8);
static SENSOR_DEVICE_ATTR(temp10_input, S_IRUGO,
					applesmc_show_temperature, NULL, 9);
static SENSOR_DEVICE_ATTR(temp11_input, S_IRUGO,
					applesmc_show_temperature, NULL, 10);
static SENSOR_DEVICE_ATTR(temp12_input, S_IRUGO,
					applesmc_show_temperature, NULL, 11);

static struct attribute *temperature_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp4_input.dev_attr.attr,
	&sensor_dev_attr_temp5_input.dev_attr.attr,
	&sensor_dev_attr_temp6_input.dev_attr.attr,
	&sensor_dev_attr_temp7_input.dev_attr.attr,
	&sensor_dev_attr_temp8_input.dev_attr.attr,
	&sensor_dev_attr_temp9_input.dev_attr.attr,
	&sensor_dev_attr_temp10_input.dev_attr.attr,
	&sensor_dev_attr_temp11_input.dev_attr.attr,
	&sensor_dev_attr_temp12_input.dev_attr.attr,
	NULL
};

static const struct attribute_group temperature_attributes_group =
	{ .attrs = temperature_attributes };

/* Module stuff */

/*
 * applesmc_dmi_match - found a match.  return one, short-circuiting the hunt.
 */
static int applesmc_dmi_match(struct dmi_system_id *id)
{
	int i = 0;
	struct dmi_match_data* dmi_data = id->driver_data;
	printk(KERN_INFO "applesmc: %s detected:\n", id->ident);
	applesmc_accelerometer = dmi_data->accelerometer;
	printk(KERN_INFO "applesmc:  - Model %s accelerometer\n",
				applesmc_accelerometer ? "with" : "without");
	applesmc_light = dmi_data->light;
	printk(KERN_INFO "applesmc:  - Model %s light sensors and backlight\n",
					applesmc_light ? "with" : "without");

	applesmc_temperature_set =  dmi_data->temperature_set;
	while (temperature_sensors_sets[applesmc_temperature_set][i] != NULL)
		i++;
	printk(KERN_INFO "applesmc:  - Model with %d temperature sensors\n", i);
	return 1;
}

/* Create accelerometer ressources */
static int applesmc_create_accelerometer(void)
{
	int ret;

	ret = sysfs_create_group(&pdev->dev.kobj,
					&accelerometer_attributes_group);
	if (ret)
		goto out;

	applesmc_idev = input_allocate_device();
	if (!applesmc_idev) {
		ret = -ENOMEM;
		goto out_sysfs;
	}

	/* initial calibrate for the input device */
	applesmc_calibrate();

	/* initialize the input class */
	applesmc_idev->name = "applesmc";
	applesmc_idev->id.bustype = BUS_HOST;
	applesmc_idev->dev.parent = &pdev->dev;
	applesmc_idev->evbit[0] = BIT(EV_ABS);
	applesmc_idev->open = applesmc_idev_open;
	applesmc_idev->close = applesmc_idev_close;
	input_set_abs_params(applesmc_idev, ABS_X,
			-256, 256, APPLESMC_INPUT_FUZZ, APPLESMC_INPUT_FLAT);
	input_set_abs_params(applesmc_idev, ABS_Y,
			-256, 256, APPLESMC_INPUT_FUZZ, APPLESMC_INPUT_FLAT);

	ret = input_register_device(applesmc_idev);
	if (ret)
		goto out_idev;

	/* start up our timer for the input device */
	init_timer(&applesmc_timer);
	applesmc_timer.function = applesmc_idev_poll;
	applesmc_timer.expires = jiffies + APPLESMC_POLL_PERIOD;

	return 0;

out_idev:
	input_free_device(applesmc_idev);

out_sysfs:
	sysfs_remove_group(&pdev->dev.kobj, &accelerometer_attributes_group);

out:
	printk(KERN_WARNING "applesmc: driver init failed (ret=%d)!\n", ret);
	return ret;
}

/* Release all ressources used by the accelerometer */
static void applesmc_release_accelerometer(void)
{
	del_timer_sync(&applesmc_timer);
	input_unregister_device(applesmc_idev);
	sysfs_remove_group(&pdev->dev.kobj, &accelerometer_attributes_group);
}

static __initdata struct dmi_match_data applesmc_dmi_data[] = {
/* MacBook Pro: accelerometer, backlight and temperature set 0 */
	{ .accelerometer = 1, .light = 1, .temperature_set = 0 },
/* MacBook: accelerometer and temperature set 0 */
	{ .accelerometer = 1, .light = 0, .temperature_set = 0 },
/* MacBook: temperature set 1 */
	{ .accelerometer = 0, .light = 0, .temperature_set = 1 }
};

/* Note that DMI_MATCH(...,"MacBook") will match "MacBookPro1,1".
 * So we need to put "Apple MacBook Pro" before "Apple MacBook". */
static __initdata struct dmi_system_id applesmc_whitelist[] = {
	{ applesmc_dmi_match, "Apple MacBook Pro", {
	  DMI_MATCH(DMI_BOARD_VENDOR,"Apple"),
	  DMI_MATCH(DMI_PRODUCT_NAME,"MacBookPro") },
		(void*)&applesmc_dmi_data[0]},
	{ applesmc_dmi_match, "Apple MacBook", {
	  DMI_MATCH(DMI_BOARD_VENDOR,"Apple"),
	  DMI_MATCH(DMI_PRODUCT_NAME,"MacBook") },
		(void*)&applesmc_dmi_data[1]},
	{ applesmc_dmi_match, "Apple Macmini", {
	  DMI_MATCH(DMI_BOARD_VENDOR,"Apple"),
	  DMI_MATCH(DMI_PRODUCT_NAME,"Macmini") },
		(void*)&applesmc_dmi_data[2]},
	{ .ident = NULL }
};

static int __init applesmc_init(void)
{
	int ret;
	int count;
	int i;

	mutex_init(&applesmc_lock);

	if (!dmi_check_system(applesmc_whitelist)) {
		printk(KERN_WARNING "applesmc: supported laptop not found!\n");
		ret = -ENODEV;
		goto out;
	}

	if (!request_region(APPLESMC_DATA_PORT, APPLESMC_NR_PORTS,
								"applesmc")) {
		ret = -ENXIO;
		goto out;
	}

	ret = platform_driver_register(&applesmc_driver);
	if (ret)
		goto out_region;

	pdev = platform_device_register_simple("applesmc", APPLESMC_DATA_PORT,
					       NULL, 0);
	if (IS_ERR(pdev)) {
		ret = PTR_ERR(pdev);
		goto out_driver;
	}

	/* Create key enumeration sysfs files */
	ret = sysfs_create_group(&pdev->dev.kobj, &key_enumeration_group);
	if (ret)
		goto out_device;

	/* create fan files */
	count = applesmc_get_fan_count();
	if (count < 0) {
		printk(KERN_ERR "applesmc: Cannot get the number of fans.\n");
	} else {
		printk(KERN_INFO "applesmc: %d fans found.\n", count);

		switch (count) {
		default:
			printk(KERN_WARNING "applesmc: More than 2 fans found,"
					" but at most 2 fans are supported"
						" by the driver.\n");
		case 2:
			ret = sysfs_create_group(&pdev->dev.kobj,
						 &fan_attribute_groups[1]);
			if (ret)
				goto out_key_enumeration;
		case 1:
			ret = sysfs_create_group(&pdev->dev.kobj,
						 &fan_attribute_groups[0]);
			if (ret)
				goto out_fan_1;
		case 0:
			;
		}
	}

	for (i = 0;
	     temperature_sensors_sets[applesmc_temperature_set][i] != NULL;
	     i++) {
		if (temperature_attributes[i] == NULL) {
			printk(KERN_ERR "applesmc: More temperature sensors "
				"in temperature_sensors_sets (at least %i)"
				"than available sysfs files in "
				"temperature_attributes (%i), please report "
				"this bug.\n", i, i-1);
			goto out_temperature;
		}
		ret = sysfs_create_file(&pdev->dev.kobj,
						temperature_attributes[i]);
		if (ret)
			goto out_temperature;
	}

	if (applesmc_accelerometer) {
		ret = applesmc_create_accelerometer();
		if (ret)
			goto out_temperature;
	}

	if (applesmc_light) {
		/* Add light sensor file */
		ret = sysfs_create_file(&pdev->dev.kobj, &dev_attr_light.attr);
		if (ret)
			goto out_accelerometer;

		/* Create the workqueue */
		applesmc_led_wq = create_singlethread_workqueue("applesmc-led");
		if (!applesmc_led_wq) {
			ret = -ENOMEM;
			goto out_light_sysfs;
		}

		/* register as a led device */
		ret = led_classdev_register(&pdev->dev, &applesmc_backlight);
		if (ret < 0)
			goto out_light_wq;
	}

	hwmon_class_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(hwmon_class_dev)) {
		ret = PTR_ERR(hwmon_class_dev);
		goto out_light_ledclass;
	}

	printk(KERN_INFO "applesmc: driver successfully loaded.\n");

	return 0;

out_light_ledclass:
	if (applesmc_light)
		led_classdev_unregister(&applesmc_backlight);
out_light_wq:
	if (applesmc_light)
		destroy_workqueue(applesmc_led_wq);
out_light_sysfs:
	if (applesmc_light)
		sysfs_remove_file(&pdev->dev.kobj, &dev_attr_light.attr);
out_accelerometer:
	if (applesmc_accelerometer)
		applesmc_release_accelerometer();
out_temperature:
	sysfs_remove_group(&pdev->dev.kobj, &temperature_attributes_group);
	sysfs_remove_group(&pdev->dev.kobj, &fan_attribute_groups[0]);
out_fan_1:
	sysfs_remove_group(&pdev->dev.kobj, &fan_attribute_groups[1]);
out_key_enumeration:
	sysfs_remove_group(&pdev->dev.kobj, &key_enumeration_group);
out_device:
	platform_device_unregister(pdev);
out_driver:
	platform_driver_unregister(&applesmc_driver);
out_region:
	release_region(APPLESMC_DATA_PORT, APPLESMC_NR_PORTS);
out:
	printk(KERN_WARNING "applesmc: driver init failed (ret=%d)!\n", ret);
	return ret;
}

static void __exit applesmc_exit(void)
{
	hwmon_device_unregister(hwmon_class_dev);
	if (applesmc_light) {
		led_classdev_unregister(&applesmc_backlight);
		destroy_workqueue(applesmc_led_wq);
		sysfs_remove_file(&pdev->dev.kobj, &dev_attr_light.attr);
	}
	if (applesmc_accelerometer)
		applesmc_release_accelerometer();
	sysfs_remove_group(&pdev->dev.kobj, &temperature_attributes_group);
	sysfs_remove_group(&pdev->dev.kobj, &fan_attribute_groups[0]);
	sysfs_remove_group(&pdev->dev.kobj, &fan_attribute_groups[1]);
	sysfs_remove_group(&pdev->dev.kobj, &key_enumeration_group);
	platform_device_unregister(pdev);
	platform_driver_unregister(&applesmc_driver);
	release_region(APPLESMC_DATA_PORT, APPLESMC_NR_PORTS);

	printk(KERN_INFO "applesmc: driver unloaded.\n");
}

module_init(applesmc_init);
module_exit(applesmc_exit);

MODULE_AUTHOR("Nicolas Boichat");
MODULE_DESCRIPTION("Apple SMC");
MODULE_LICENSE("GPL v2");
