/*
 * abituguru.c Copyright (c) 2005-2006 Hans de Goede <hdegoede@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
/*
 * This driver supports the sensor part of the first and second revision of
 * the custom Abit uGuru chip found on Abit uGuru motherboards. Note: because
 * of lack of specs the CPU/RAM voltage & frequency control is not supported!
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/dmi.h>
#include <linux/io.h>

/* Banks */
#define ABIT_UGURU_ALARM_BANK			0x20 /* 1x 3 bytes */
#define ABIT_UGURU_SENSOR_BANK1			0x21 /* 16x volt and temp */
#define ABIT_UGURU_FAN_PWM			0x24 /* 3x 5 bytes */
#define ABIT_UGURU_SENSOR_BANK2			0x26 /* fans */
/* max nr of sensors in bank1, a bank1 sensor can be in, temp or nc */
#define ABIT_UGURU_MAX_BANK1_SENSORS		16
/*
 * Warning if you increase one of the 2 MAX defines below to 10 or higher you
 * should adjust the belonging _NAMES_LENGTH macro for the 2 digit number!
 */
/* max nr of sensors in bank2, currently mb's with max 6 fans are known */
#define ABIT_UGURU_MAX_BANK2_SENSORS		6
/* max nr of pwm outputs, currently mb's with max 5 pwm outputs are known */
#define ABIT_UGURU_MAX_PWMS			5
/* uGuru sensor bank 1 flags */			     /* Alarm if: */
#define ABIT_UGURU_TEMP_HIGH_ALARM_ENABLE	0x01 /*  temp over warn */
#define ABIT_UGURU_VOLT_HIGH_ALARM_ENABLE	0x02 /*  volt over max */
#define ABIT_UGURU_VOLT_LOW_ALARM_ENABLE	0x04 /*  volt under min */
#define ABIT_UGURU_TEMP_HIGH_ALARM_FLAG		0x10 /* temp is over warn */
#define ABIT_UGURU_VOLT_HIGH_ALARM_FLAG		0x20 /* volt is over max */
#define ABIT_UGURU_VOLT_LOW_ALARM_FLAG		0x40 /* volt is under min */
/* uGuru sensor bank 2 flags */			     /* Alarm if: */
#define ABIT_UGURU_FAN_LOW_ALARM_ENABLE		0x01 /*   fan under min */
/* uGuru sensor bank common flags */
#define ABIT_UGURU_BEEP_ENABLE			0x08 /* beep if alarm */
#define ABIT_UGURU_SHUTDOWN_ENABLE		0x80 /* shutdown if alarm */
/* uGuru fan PWM (speed control) flags */
#define ABIT_UGURU_FAN_PWM_ENABLE		0x80 /* enable speed control */
/* Values used for conversion */
#define ABIT_UGURU_FAN_MAX			15300 /* RPM */
/* Bank1 sensor types */
#define ABIT_UGURU_IN_SENSOR			0
#define ABIT_UGURU_TEMP_SENSOR			1
#define ABIT_UGURU_NC				2
/*
 * In many cases we need to wait for the uGuru to reach a certain status, most
 * of the time it will reach this status within 30 - 90 ISA reads, and thus we
 * can best busy wait. This define gives the total amount of reads to try.
 */
#define ABIT_UGURU_WAIT_TIMEOUT			125
/*
 * However sometimes older versions of the uGuru seem to be distracted and they
 * do not respond for a long time. To handle this we sleep before each of the
 * last ABIT_UGURU_WAIT_TIMEOUT_SLEEP tries.
 */
#define ABIT_UGURU_WAIT_TIMEOUT_SLEEP		5
/*
 * Normally all expected status in abituguru_ready, are reported after the
 * first read, but sometimes not and we need to poll.
 */
#define ABIT_UGURU_READY_TIMEOUT		5
/* Maximum 3 retries on timedout reads/writes, delay 200 ms before retrying */
#define ABIT_UGURU_MAX_RETRIES			3
#define ABIT_UGURU_RETRY_DELAY			(HZ/5)
/* Maximum 2 timeouts in abituguru_update_device, iow 3 in a row is an error */
#define ABIT_UGURU_MAX_TIMEOUTS			2
/* utility macros */
#define ABIT_UGURU_NAME				"abituguru"
#define ABIT_UGURU_DEBUG(level, format, arg...)				\
	if (level <= verbose)						\
		printk(KERN_DEBUG ABIT_UGURU_NAME ": "	format , ## arg)
/* Macros to help calculate the sysfs_names array length */
/*
 * sum of strlen of: in??_input\0, in??_{min,max}\0, in??_{min,max}_alarm\0,
 * in??_{min,max}_alarm_enable\0, in??_beep\0, in??_shutdown\0
 */
#define ABITUGURU_IN_NAMES_LENGTH	(11 + 2 * 9 + 2 * 15 + 2 * 22 + 10 + 14)
/*
 * sum of strlen of: temp??_input\0, temp??_max\0, temp??_crit\0,
 * temp??_alarm\0, temp??_alarm_enable\0, temp??_beep\0, temp??_shutdown\0
 */
#define ABITUGURU_TEMP_NAMES_LENGTH	(13 + 11 + 12 + 13 + 20 + 12 + 16)
/*
 * sum of strlen of: fan?_input\0, fan?_min\0, fan?_alarm\0,
 * fan?_alarm_enable\0, fan?_beep\0, fan?_shutdown\0
 */
#define ABITUGURU_FAN_NAMES_LENGTH	(11 + 9 + 11 + 18 + 10 + 14)
/*
 * sum of strlen of: pwm?_enable\0, pwm?_auto_channels_temp\0,
 * pwm?_auto_point{1,2}_pwm\0, pwm?_auto_point{1,2}_temp\0
 */
#define ABITUGURU_PWM_NAMES_LENGTH	(12 + 24 + 2 * 21 + 2 * 22)
/* IN_NAMES_LENGTH > TEMP_NAMES_LENGTH so assume all bank1 sensors are in */
#define ABITUGURU_SYSFS_NAMES_LENGTH	( \
	ABIT_UGURU_MAX_BANK1_SENSORS * ABITUGURU_IN_NAMES_LENGTH + \
	ABIT_UGURU_MAX_BANK2_SENSORS * ABITUGURU_FAN_NAMES_LENGTH + \
	ABIT_UGURU_MAX_PWMS * ABITUGURU_PWM_NAMES_LENGTH)

/*
 * All the macros below are named identical to the oguru and oguru2 programs
 * reverse engineered by Olle Sandberg, hence the names might not be 100%
 * logical. I could come up with better names, but I prefer keeping the names
 * identical so that this driver can be compared with his work more easily.
 */
/* Two i/o-ports are used by uGuru */
#define ABIT_UGURU_BASE				0x00E0
/* Used to tell uGuru what to read and to read the actual data */
#define ABIT_UGURU_CMD				0x00
/* Mostly used to check if uGuru is busy */
#define ABIT_UGURU_DATA				0x04
#define ABIT_UGURU_REGION_LENGTH		5
/* uGuru status' */
#define ABIT_UGURU_STATUS_WRITE			0x00 /* Ready to be written */
#define ABIT_UGURU_STATUS_READ			0x01 /* Ready to be read */
#define ABIT_UGURU_STATUS_INPUT			0x08 /* More input */
#define ABIT_UGURU_STATUS_READY			0x09 /* Ready to be written */

/* Constants */
/* in (Volt) sensors go up to 3494 mV, temp to 255000 millidegrees Celsius */
static const int abituguru_bank1_max_value[2] = { 3494, 255000 };
/*
 * Min / Max allowed values for sensor2 (fan) alarm threshold, these values
 * correspond to 300-3000 RPM
 */
static const u8 abituguru_bank2_min_threshold = 5;
static const u8 abituguru_bank2_max_threshold = 50;
/*
 * Register 0 is a bitfield, 1 and 2 are pwm settings (255 = 100%), 3 and 4
 * are temperature trip points.
 */
static const int abituguru_pwm_settings_multiplier[5] = { 0, 1, 1, 1000, 1000 };
/*
 * Min / Max allowed values for pwm_settings. Note: pwm1 (CPU fan) is a
 * special case the minium allowed pwm% setting for this is 30% (77) on
 * some MB's this special case is handled in the code!
 */
static const u8 abituguru_pwm_min[5] = { 0, 170, 170, 25, 25 };
static const u8 abituguru_pwm_max[5] = { 0, 255, 255, 75, 75 };


/* Insmod parameters */
static bool force;
module_param(force, bool, 0);
MODULE_PARM_DESC(force, "Set to one to force detection.");
static int bank1_types[ABIT_UGURU_MAX_BANK1_SENSORS] = { -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
module_param_array(bank1_types, int, NULL, 0);
MODULE_PARM_DESC(bank1_types, "Bank1 sensortype autodetection override:\n"
	"   -1 autodetect\n"
	"    0 volt sensor\n"
	"    1 temp sensor\n"
	"    2 not connected");
static int fan_sensors;
module_param(fan_sensors, int, 0);
MODULE_PARM_DESC(fan_sensors, "Number of fan sensors on the uGuru "
	"(0 = autodetect)");
static int pwms;
module_param(pwms, int, 0);
MODULE_PARM_DESC(pwms, "Number of PWMs on the uGuru "
	"(0 = autodetect)");

/* Default verbose is 2, since this driver is still in the testing phase */
static int verbose = 2;
module_param(verbose, int, 0644);
MODULE_PARM_DESC(verbose, "How verbose should the driver be? (0-3):\n"
	"   0 normal output\n"
	"   1 + verbose error reporting\n"
	"   2 + sensors type probing info\n"
	"   3 + retryable error reporting");


/*
 * For the Abit uGuru, we need to keep some data in memory.
 * The structure is dynamically allocated, at the same time when a new
 * abituguru device is allocated.
 */
struct abituguru_data {
	struct device *hwmon_dev;	/* hwmon registered device */
	struct mutex update_lock;	/* protect access to data and uGuru */
	unsigned long last_updated;	/* In jiffies */
	unsigned short addr;		/* uguru base address */
	char uguru_ready;		/* is the uguru in ready state? */
	unsigned char update_timeouts;	/*
					 * number of update timeouts since last
					 * successful update
					 */

	/*
	 * The sysfs attr and their names are generated automatically, for bank1
	 * we cannot use a predefined array because we don't know beforehand
	 * of a sensor is a volt or a temp sensor, for bank2 and the pwms its
	 * easier todo things the same way.  For in sensors we have 9 (temp 7)
	 * sysfs entries per sensor, for bank2 and pwms 6.
	 */
	struct sensor_device_attribute_2 sysfs_attr[
		ABIT_UGURU_MAX_BANK1_SENSORS * 9 +
		ABIT_UGURU_MAX_BANK2_SENSORS * 6 + ABIT_UGURU_MAX_PWMS * 6];
	/* Buffer to store the dynamically generated sysfs names */
	char sysfs_names[ABITUGURU_SYSFS_NAMES_LENGTH];

	/* Bank 1 data */
	/* number of and addresses of [0] in, [1] temp sensors */
	u8 bank1_sensors[2];
	u8 bank1_address[2][ABIT_UGURU_MAX_BANK1_SENSORS];
	u8 bank1_value[ABIT_UGURU_MAX_BANK1_SENSORS];
	/*
	 * This array holds 3 entries per sensor for the bank 1 sensor settings
	 * (flags, min, max for voltage / flags, warn, shutdown for temp).
	 */
	u8 bank1_settings[ABIT_UGURU_MAX_BANK1_SENSORS][3];
	/*
	 * Maximum value for each sensor used for scaling in mV/millidegrees
	 * Celsius.
	 */
	int bank1_max_value[ABIT_UGURU_MAX_BANK1_SENSORS];

	/* Bank 2 data, ABIT_UGURU_MAX_BANK2_SENSORS entries for bank2 */
	u8 bank2_sensors; /* actual number of bank2 sensors found */
	u8 bank2_value[ABIT_UGURU_MAX_BANK2_SENSORS];
	u8 bank2_settings[ABIT_UGURU_MAX_BANK2_SENSORS][2]; /* flags, min */

	/* Alarms 2 bytes for bank1, 1 byte for bank2 */
	u8 alarms[3];

	/* Fan PWM (speed control) 5 bytes per PWM */
	u8 pwms; /* actual number of pwms found */
	u8 pwm_settings[ABIT_UGURU_MAX_PWMS][5];
};

static const char *never_happen = "This should never happen.";
static const char *report_this =
	"Please report this to the abituguru maintainer (see MAINTAINERS)";

/* wait till the uguru is in the specified state */
static int abituguru_wait(struct abituguru_data *data, u8 state)
{
	int timeout = ABIT_UGURU_WAIT_TIMEOUT;

	while (inb_p(data->addr + ABIT_UGURU_DATA) != state) {
		timeout--;
		if (timeout == 0)
			return -EBUSY;
		/*
		 * sleep a bit before our last few tries, see the comment on
		 * this where ABIT_UGURU_WAIT_TIMEOUT_SLEEP is defined.
		 */
		if (timeout <= ABIT_UGURU_WAIT_TIMEOUT_SLEEP)
			msleep(0);
	}
	return 0;
}

/* Put the uguru in ready for input state */
static int abituguru_ready(struct abituguru_data *data)
{
	int timeout = ABIT_UGURU_READY_TIMEOUT;

	if (data->uguru_ready)
		return 0;

	/* Reset? / Prepare for next read/write cycle */
	outb(0x00, data->addr + ABIT_UGURU_DATA);

	/* Wait till the uguru is ready */
	if (abituguru_wait(data, ABIT_UGURU_STATUS_READY)) {
		ABIT_UGURU_DEBUG(1,
			"timeout exceeded waiting for ready state\n");
		return -EIO;
	}

	/* Cmd port MUST be read now and should contain 0xAC */
	while (inb_p(data->addr + ABIT_UGURU_CMD) != 0xAC) {
		timeout--;
		if (timeout == 0) {
			ABIT_UGURU_DEBUG(1,
			   "CMD reg does not hold 0xAC after ready command\n");
			return -EIO;
		}
		msleep(0);
	}

	/*
	 * After this the ABIT_UGURU_DATA port should contain
	 * ABIT_UGURU_STATUS_INPUT
	 */
	timeout = ABIT_UGURU_READY_TIMEOUT;
	while (inb_p(data->addr + ABIT_UGURU_DATA) != ABIT_UGURU_STATUS_INPUT) {
		timeout--;
		if (timeout == 0) {
			ABIT_UGURU_DEBUG(1,
				"state != more input after ready command\n");
			return -EIO;
		}
		msleep(0);
	}

	data->uguru_ready = 1;
	return 0;
}

/*
 * Send the bank and then sensor address to the uGuru for the next read/write
 * cycle. This function gets called as the first part of a read/write by
 * abituguru_read and abituguru_write. This function should never be
 * called by any other function.
 */
static int abituguru_send_address(struct abituguru_data *data,
	u8 bank_addr, u8 sensor_addr, int retries)
{
	/*
	 * assume the caller does error handling itself if it has not requested
	 * any retries, and thus be quiet.
	 */
	int report_errors = retries;

	for (;;) {
		/*
		 * Make sure the uguru is ready and then send the bank address,
		 * after this the uguru is no longer "ready".
		 */
		if (abituguru_ready(data) != 0)
			return -EIO;
		outb(bank_addr, data->addr + ABIT_UGURU_DATA);
		data->uguru_ready = 0;

		/*
		 * Wait till the uguru is ABIT_UGURU_STATUS_INPUT state again
		 * and send the sensor addr
		 */
		if (abituguru_wait(data, ABIT_UGURU_STATUS_INPUT)) {
			if (retries) {
				ABIT_UGURU_DEBUG(3, "timeout exceeded "
					"waiting for more input state, %d "
					"tries remaining\n", retries);
				set_current_state(TASK_UNINTERRUPTIBLE);
				schedule_timeout(ABIT_UGURU_RETRY_DELAY);
				retries--;
				continue;
			}
			if (report_errors)
				ABIT_UGURU_DEBUG(1, "timeout exceeded "
					"waiting for more input state "
					"(bank: %d)\n", (int)bank_addr);
			return -EBUSY;
		}
		outb(sensor_addr, data->addr + ABIT_UGURU_CMD);
		return 0;
	}
}

/*
 * Read count bytes from sensor sensor_addr in bank bank_addr and store the
 * result in buf, retry the send address part of the read retries times.
 */
static int abituguru_read(struct abituguru_data *data,
	u8 bank_addr, u8 sensor_addr, u8 *buf, int count, int retries)
{
	int i;

	/* Send the address */
	i = abituguru_send_address(data, bank_addr, sensor_addr, retries);
	if (i)
		return i;

	/* And read the data */
	for (i = 0; i < count; i++) {
		if (abituguru_wait(data, ABIT_UGURU_STATUS_READ)) {
			ABIT_UGURU_DEBUG(retries ? 1 : 3,
				"timeout exceeded waiting for "
				"read state (bank: %d, sensor: %d)\n",
				(int)bank_addr, (int)sensor_addr);
			break;
		}
		buf[i] = inb(data->addr + ABIT_UGURU_CMD);
	}

	/* Last put the chip back in ready state */
	abituguru_ready(data);

	return i;
}

/*
 * Write count bytes from buf to sensor sensor_addr in bank bank_addr, the send
 * address part of the write is always retried ABIT_UGURU_MAX_RETRIES times.
 */
static int abituguru_write(struct abituguru_data *data,
	u8 bank_addr, u8 sensor_addr, u8 *buf, int count)
{
	/*
	 * We use the ready timeout as we have to wait for 0xAC just like the
	 * ready function
	 */
	int i, timeout = ABIT_UGURU_READY_TIMEOUT;

	/* Send the address */
	i = abituguru_send_address(data, bank_addr, sensor_addr,
		ABIT_UGURU_MAX_RETRIES);
	if (i)
		return i;

	/* And write the data */
	for (i = 0; i < count; i++) {
		if (abituguru_wait(data, ABIT_UGURU_STATUS_WRITE)) {
			ABIT_UGURU_DEBUG(1, "timeout exceeded waiting for "
				"write state (bank: %d, sensor: %d)\n",
				(int)bank_addr, (int)sensor_addr);
			break;
		}
		outb(buf[i], data->addr + ABIT_UGURU_CMD);
	}

	/*
	 * Now we need to wait till the chip is ready to be read again,
	 * so that we can read 0xAC as confirmation that our write has
	 * succeeded.
	 */
	if (abituguru_wait(data, ABIT_UGURU_STATUS_READ)) {
		ABIT_UGURU_DEBUG(1, "timeout exceeded waiting for read state "
			"after write (bank: %d, sensor: %d)\n", (int)bank_addr,
			(int)sensor_addr);
		return -EIO;
	}

	/* Cmd port MUST be read now and should contain 0xAC */
	while (inb_p(data->addr + ABIT_UGURU_CMD) != 0xAC) {
		timeout--;
		if (timeout == 0) {
			ABIT_UGURU_DEBUG(1, "CMD reg does not hold 0xAC after "
				"write (bank: %d, sensor: %d)\n",
				(int)bank_addr, (int)sensor_addr);
			return -EIO;
		}
		msleep(0);
	}

	/* Last put the chip back in ready state */
	abituguru_ready(data);

	return i;
}

/*
 * Detect sensor type. Temp and Volt sensors are enabled with
 * different masks and will ignore enable masks not meant for them.
 * This enables us to test what kind of sensor we're dealing with.
 * By setting the alarm thresholds so that we will always get an
 * alarm for sensor type X and then enabling the sensor as sensor type
 * X, if we then get an alarm it is a sensor of type X.
 */
static int __devinit
abituguru_detect_bank1_sensor_type(struct abituguru_data *data,
				   u8 sensor_addr)
{
	u8 val, test_flag, buf[3];
	int i, ret = -ENODEV; /* error is the most common used retval :| */

	/* If overriden by the user return the user selected type */
	if (bank1_types[sensor_addr] >= ABIT_UGURU_IN_SENSOR &&
			bank1_types[sensor_addr] <= ABIT_UGURU_NC) {
		ABIT_UGURU_DEBUG(2, "assuming sensor type %d for bank1 sensor "
			"%d because of \"bank1_types\" module param\n",
			bank1_types[sensor_addr], (int)sensor_addr);
		return bank1_types[sensor_addr];
	}

	/* First read the sensor and the current settings */
	if (abituguru_read(data, ABIT_UGURU_SENSOR_BANK1, sensor_addr, &val,
			1, ABIT_UGURU_MAX_RETRIES) != 1)
		return -ENODEV;

	/* Test val is sane / usable for sensor type detection. */
	if ((val < 10u) || (val > 250u)) {
		pr_warn("bank1-sensor: %d reading (%d) too close to limits, "
			"unable to determine sensor type, skipping sensor\n",
			(int)sensor_addr, (int)val);
		/*
		 * assume no sensor is there for sensors for which we can't
		 * determine the sensor type because their reading is too close
		 * to their limits, this usually means no sensor is there.
		 */
		return ABIT_UGURU_NC;
	}

	ABIT_UGURU_DEBUG(2, "testing bank1 sensor %d\n", (int)sensor_addr);
	/*
	 * Volt sensor test, enable volt low alarm, set min value ridicously
	 * high, or vica versa if the reading is very high. If its a volt
	 * sensor this should always give us an alarm.
	 */
	if (val <= 240u) {
		buf[0] = ABIT_UGURU_VOLT_LOW_ALARM_ENABLE;
		buf[1] = 245;
		buf[2] = 250;
		test_flag = ABIT_UGURU_VOLT_LOW_ALARM_FLAG;
	} else {
		buf[0] = ABIT_UGURU_VOLT_HIGH_ALARM_ENABLE;
		buf[1] = 5;
		buf[2] = 10;
		test_flag = ABIT_UGURU_VOLT_HIGH_ALARM_FLAG;
	}

	if (abituguru_write(data, ABIT_UGURU_SENSOR_BANK1 + 2, sensor_addr,
			buf, 3) != 3)
		goto abituguru_detect_bank1_sensor_type_exit;
	/*
	 * Now we need 20 ms to give the uguru time to read the sensors
	 * and raise a voltage alarm
	 */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(HZ/50);
	/* Check for alarm and check the alarm is a volt low alarm. */
	if (abituguru_read(data, ABIT_UGURU_ALARM_BANK, 0, buf, 3,
			ABIT_UGURU_MAX_RETRIES) != 3)
		goto abituguru_detect_bank1_sensor_type_exit;
	if (buf[sensor_addr/8] & (0x01 << (sensor_addr % 8))) {
		if (abituguru_read(data, ABIT_UGURU_SENSOR_BANK1 + 1,
				sensor_addr, buf, 3,
				ABIT_UGURU_MAX_RETRIES) != 3)
			goto abituguru_detect_bank1_sensor_type_exit;
		if (buf[0] & test_flag) {
			ABIT_UGURU_DEBUG(2, "  found volt sensor\n");
			ret = ABIT_UGURU_IN_SENSOR;
			goto abituguru_detect_bank1_sensor_type_exit;
		} else
			ABIT_UGURU_DEBUG(2, "  alarm raised during volt "
				"sensor test, but volt range flag not set\n");
	} else
		ABIT_UGURU_DEBUG(2, "  alarm not raised during volt sensor "
			"test\n");

	/*
	 * Temp sensor test, enable sensor as a temp sensor, set beep value
	 * ridicously low (but not too low, otherwise uguru ignores it).
	 * If its a temp sensor this should always give us an alarm.
	 */
	buf[0] = ABIT_UGURU_TEMP_HIGH_ALARM_ENABLE;
	buf[1] = 5;
	buf[2] = 10;
	if (abituguru_write(data, ABIT_UGURU_SENSOR_BANK1 + 2, sensor_addr,
			buf, 3) != 3)
		goto abituguru_detect_bank1_sensor_type_exit;
	/*
	 * Now we need 50 ms to give the uguru time to read the sensors
	 * and raise a temp alarm
	 */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(HZ/20);
	/* Check for alarm and check the alarm is a temp high alarm. */
	if (abituguru_read(data, ABIT_UGURU_ALARM_BANK, 0, buf, 3,
			ABIT_UGURU_MAX_RETRIES) != 3)
		goto abituguru_detect_bank1_sensor_type_exit;
	if (buf[sensor_addr/8] & (0x01 << (sensor_addr % 8))) {
		if (abituguru_read(data, ABIT_UGURU_SENSOR_BANK1 + 1,
				sensor_addr, buf, 3,
				ABIT_UGURU_MAX_RETRIES) != 3)
			goto abituguru_detect_bank1_sensor_type_exit;
		if (buf[0] & ABIT_UGURU_TEMP_HIGH_ALARM_FLAG) {
			ABIT_UGURU_DEBUG(2, "  found temp sensor\n");
			ret = ABIT_UGURU_TEMP_SENSOR;
			goto abituguru_detect_bank1_sensor_type_exit;
		} else
			ABIT_UGURU_DEBUG(2, "  alarm raised during temp "
				"sensor test, but temp high flag not set\n");
	} else
		ABIT_UGURU_DEBUG(2, "  alarm not raised during temp sensor "
			"test\n");

	ret = ABIT_UGURU_NC;
abituguru_detect_bank1_sensor_type_exit:
	/*
	 * Restore original settings, failing here is really BAD, it has been
	 * reported that some BIOS-es hang when entering the uGuru menu with
	 * invalid settings present in the uGuru, so we try this 3 times.
	 */
	for (i = 0; i < 3; i++)
		if (abituguru_write(data, ABIT_UGURU_SENSOR_BANK1 + 2,
				sensor_addr, data->bank1_settings[sensor_addr],
				3) == 3)
			break;
	if (i == 3) {
		pr_err("Fatal error could not restore original settings. %s %s\n",
		       never_happen, report_this);
		return -ENODEV;
	}
	return ret;
}

/*
 * These functions try to find out how many sensors there are in bank2 and how
 * many pwms there are. The purpose of this is to make sure that we don't give
 * the user the possibility to change settings for non-existent sensors / pwm.
 * The uGuru will happily read / write whatever memory happens to be after the
 * memory storing the PWM settings when reading/writing to a PWM which is not
 * there. Notice even if we detect a PWM which doesn't exist we normally won't
 * write to it, unless the user tries to change the settings.
 *
 * Although the uGuru allows reading (settings) from non existing bank2
 * sensors, my version of the uGuru does seem to stop writing to them, the
 * write function above aborts in this case with:
 * "CMD reg does not hold 0xAC after write"
 *
 * Notice these 2 tests are non destructive iow read-only tests, otherwise
 * they would defeat their purpose. Although for the bank2_sensors detection a
 * read/write test would be feasible because of the reaction above, I've
 * however opted to stay on the safe side.
 */
static void __devinit
abituguru_detect_no_bank2_sensors(struct abituguru_data *data)
{
	int i;

	if (fan_sensors > 0 && fan_sensors <= ABIT_UGURU_MAX_BANK2_SENSORS) {
		data->bank2_sensors = fan_sensors;
		ABIT_UGURU_DEBUG(2, "assuming %d fan sensors because of "
			"\"fan_sensors\" module param\n",
			(int)data->bank2_sensors);
		return;
	}

	ABIT_UGURU_DEBUG(2, "detecting number of fan sensors\n");
	for (i = 0; i < ABIT_UGURU_MAX_BANK2_SENSORS; i++) {
		/*
		 * 0x89 are the known used bits:
		 * -0x80 enable shutdown
		 * -0x08 enable beep
		 * -0x01 enable alarm
		 * All other bits should be 0, but on some motherboards
		 * 0x40 (bit 6) is also high for some of the fans??
		 */
		if (data->bank2_settings[i][0] & ~0xC9) {
			ABIT_UGURU_DEBUG(2, "  bank2 sensor %d does not seem "
				"to be a fan sensor: settings[0] = %02X\n",
				i, (unsigned int)data->bank2_settings[i][0]);
			break;
		}

		/* check if the threshold is within the allowed range */
		if (data->bank2_settings[i][1] <
				abituguru_bank2_min_threshold) {
			ABIT_UGURU_DEBUG(2, "  bank2 sensor %d does not seem "
				"to be a fan sensor: the threshold (%d) is "
				"below the minimum (%d)\n", i,
				(int)data->bank2_settings[i][1],
				(int)abituguru_bank2_min_threshold);
			break;
		}
		if (data->bank2_settings[i][1] >
				abituguru_bank2_max_threshold) {
			ABIT_UGURU_DEBUG(2, "  bank2 sensor %d does not seem "
				"to be a fan sensor: the threshold (%d) is "
				"above the maximum (%d)\n", i,
				(int)data->bank2_settings[i][1],
				(int)abituguru_bank2_max_threshold);
			break;
		}
	}

	data->bank2_sensors = i;
	ABIT_UGURU_DEBUG(2, " found: %d fan sensors\n",
		(int)data->bank2_sensors);
}

static void __devinit
abituguru_detect_no_pwms(struct abituguru_data *data)
{
	int i, j;

	if (pwms > 0 && pwms <= ABIT_UGURU_MAX_PWMS) {
		data->pwms = pwms;
		ABIT_UGURU_DEBUG(2, "assuming %d PWM outputs because of "
			"\"pwms\" module param\n", (int)data->pwms);
		return;
	}

	ABIT_UGURU_DEBUG(2, "detecting number of PWM outputs\n");
	for (i = 0; i < ABIT_UGURU_MAX_PWMS; i++) {
		/*
		 * 0x80 is the enable bit and the low
		 * nibble is which temp sensor to use,
		 * the other bits should be 0
		 */
		if (data->pwm_settings[i][0] & ~0x8F) {
			ABIT_UGURU_DEBUG(2, "  pwm channel %d does not seem "
				"to be a pwm channel: settings[0] = %02X\n",
				i, (unsigned int)data->pwm_settings[i][0]);
			break;
		}

		/*
		 * the low nibble must correspond to one of the temp sensors
		 * we've found
		 */
		for (j = 0; j < data->bank1_sensors[ABIT_UGURU_TEMP_SENSOR];
				j++) {
			if (data->bank1_address[ABIT_UGURU_TEMP_SENSOR][j] ==
					(data->pwm_settings[i][0] & 0x0F))
				break;
		}
		if (j == data->bank1_sensors[ABIT_UGURU_TEMP_SENSOR]) {
			ABIT_UGURU_DEBUG(2, "  pwm channel %d does not seem "
				"to be a pwm channel: %d is not a valid temp "
				"sensor address\n", i,
				data->pwm_settings[i][0] & 0x0F);
			break;
		}

		/* check if all other settings are within the allowed range */
		for (j = 1; j < 5; j++) {
			u8 min;
			/* special case pwm1 min pwm% */
			if ((i == 0) && ((j == 1) || (j == 2)))
				min = 77;
			else
				min = abituguru_pwm_min[j];
			if (data->pwm_settings[i][j] < min) {
				ABIT_UGURU_DEBUG(2, "  pwm channel %d does "
					"not seem to be a pwm channel: "
					"setting %d (%d) is below the minimum "
					"value (%d)\n", i, j,
					(int)data->pwm_settings[i][j],
					(int)min);
				goto abituguru_detect_no_pwms_exit;
			}
			if (data->pwm_settings[i][j] > abituguru_pwm_max[j]) {
				ABIT_UGURU_DEBUG(2, "  pwm channel %d does "
					"not seem to be a pwm channel: "
					"setting %d (%d) is above the maximum "
					"value (%d)\n", i, j,
					(int)data->pwm_settings[i][j],
					(int)abituguru_pwm_max[j]);
				goto abituguru_detect_no_pwms_exit;
			}
		}

		/* check that min temp < max temp and min pwm < max pwm */
		if (data->pwm_settings[i][1] >= data->pwm_settings[i][2]) {
			ABIT_UGURU_DEBUG(2, "  pwm channel %d does not seem "
				"to be a pwm channel: min pwm (%d) >= "
				"max pwm (%d)\n", i,
				(int)data->pwm_settings[i][1],
				(int)data->pwm_settings[i][2]);
			break;
		}
		if (data->pwm_settings[i][3] >= data->pwm_settings[i][4]) {
			ABIT_UGURU_DEBUG(2, "  pwm channel %d does not seem "
				"to be a pwm channel: min temp (%d) >= "
				"max temp (%d)\n", i,
				(int)data->pwm_settings[i][3],
				(int)data->pwm_settings[i][4]);
			break;
		}
	}

abituguru_detect_no_pwms_exit:
	data->pwms = i;
	ABIT_UGURU_DEBUG(2, " found: %d PWM outputs\n", (int)data->pwms);
}

/*
 * Following are the sysfs callback functions. These functions expect:
 * sensor_device_attribute_2->index:   sensor address/offset in the bank
 * sensor_device_attribute_2->nr:      register offset, bitmask or NA.
 */
static struct abituguru_data *abituguru_update_device(struct device *dev);

static ssize_t show_bank1_value(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	struct abituguru_data *data = abituguru_update_device(dev);
	if (!data)
		return -EIO;
	return sprintf(buf, "%d\n", (data->bank1_value[attr->index] *
		data->bank1_max_value[attr->index] + 128) / 255);
}

static ssize_t show_bank1_setting(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	struct abituguru_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n",
		(data->bank1_settings[attr->index][attr->nr] *
		data->bank1_max_value[attr->index] + 128) / 255);
}

static ssize_t show_bank2_value(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	struct abituguru_data *data = abituguru_update_device(dev);
	if (!data)
		return -EIO;
	return sprintf(buf, "%d\n", (data->bank2_value[attr->index] *
		ABIT_UGURU_FAN_MAX + 128) / 255);
}

static ssize_t show_bank2_setting(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	struct abituguru_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n",
		(data->bank2_settings[attr->index][attr->nr] *
		ABIT_UGURU_FAN_MAX + 128) / 255);
}

static ssize_t store_bank1_setting(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	struct abituguru_data *data = dev_get_drvdata(dev);
	unsigned long val;
	ssize_t ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	ret = count;
	val = (val * 255 + data->bank1_max_value[attr->index] / 2) /
		data->bank1_max_value[attr->index];
	if (val > 255)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	if (data->bank1_settings[attr->index][attr->nr] != val) {
		u8 orig_val = data->bank1_settings[attr->index][attr->nr];
		data->bank1_settings[attr->index][attr->nr] = val;
		if (abituguru_write(data, ABIT_UGURU_SENSOR_BANK1 + 2,
				attr->index, data->bank1_settings[attr->index],
				3) <= attr->nr) {
			data->bank1_settings[attr->index][attr->nr] = orig_val;
			ret = -EIO;
		}
	}
	mutex_unlock(&data->update_lock);
	return ret;
}

static ssize_t store_bank2_setting(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	struct abituguru_data *data = dev_get_drvdata(dev);
	unsigned long val;
	ssize_t ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	ret = count;
	val = (val * 255 + ABIT_UGURU_FAN_MAX / 2) / ABIT_UGURU_FAN_MAX;

	/* this check can be done before taking the lock */
	if (val < abituguru_bank2_min_threshold ||
			val > abituguru_bank2_max_threshold)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	if (data->bank2_settings[attr->index][attr->nr] != val) {
		u8 orig_val = data->bank2_settings[attr->index][attr->nr];
		data->bank2_settings[attr->index][attr->nr] = val;
		if (abituguru_write(data, ABIT_UGURU_SENSOR_BANK2 + 2,
				attr->index, data->bank2_settings[attr->index],
				2) <= attr->nr) {
			data->bank2_settings[attr->index][attr->nr] = orig_val;
			ret = -EIO;
		}
	}
	mutex_unlock(&data->update_lock);
	return ret;
}

static ssize_t show_bank1_alarm(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	struct abituguru_data *data = abituguru_update_device(dev);
	if (!data)
		return -EIO;
	/*
	 * See if the alarm bit for this sensor is set, and if the
	 * alarm matches the type of alarm we're looking for (for volt
	 * it can be either low or high). The type is stored in a few
	 * readonly bits in the settings part of the relevant sensor.
	 * The bitmask of the type is passed to us in attr->nr.
	 */
	if ((data->alarms[attr->index / 8] & (0x01 << (attr->index % 8))) &&
			(data->bank1_settings[attr->index][0] & attr->nr))
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static ssize_t show_bank2_alarm(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	struct abituguru_data *data = abituguru_update_device(dev);
	if (!data)
		return -EIO;
	if (data->alarms[2] & (0x01 << attr->index))
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static ssize_t show_bank1_mask(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	struct abituguru_data *data = dev_get_drvdata(dev);
	if (data->bank1_settings[attr->index][0] & attr->nr)
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static ssize_t show_bank2_mask(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	struct abituguru_data *data = dev_get_drvdata(dev);
	if (data->bank2_settings[attr->index][0] & attr->nr)
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static ssize_t store_bank1_mask(struct device *dev,
	struct device_attribute *devattr, const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	struct abituguru_data *data = dev_get_drvdata(dev);
	ssize_t ret;
	u8 orig_val;
	unsigned long mask;

	ret = kstrtoul(buf, 10, &mask);
	if (ret)
		return ret;

	ret = count;
	mutex_lock(&data->update_lock);
	orig_val = data->bank1_settings[attr->index][0];

	if (mask)
		data->bank1_settings[attr->index][0] |= attr->nr;
	else
		data->bank1_settings[attr->index][0] &= ~attr->nr;

	if ((data->bank1_settings[attr->index][0] != orig_val) &&
			(abituguru_write(data,
			ABIT_UGURU_SENSOR_BANK1 + 2, attr->index,
			data->bank1_settings[attr->index], 3) < 1)) {
		data->bank1_settings[attr->index][0] = orig_val;
		ret = -EIO;
	}
	mutex_unlock(&data->update_lock);
	return ret;
}

static ssize_t store_bank2_mask(struct device *dev,
	struct device_attribute *devattr, const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	struct abituguru_data *data = dev_get_drvdata(dev);
	ssize_t ret;
	u8 orig_val;
	unsigned long mask;

	ret = kstrtoul(buf, 10, &mask);
	if (ret)
		return ret;

	ret = count;
	mutex_lock(&data->update_lock);
	orig_val = data->bank2_settings[attr->index][0];

	if (mask)
		data->bank2_settings[attr->index][0] |= attr->nr;
	else
		data->bank2_settings[attr->index][0] &= ~attr->nr;

	if ((data->bank2_settings[attr->index][0] != orig_val) &&
			(abituguru_write(data,
			ABIT_UGURU_SENSOR_BANK2 + 2, attr->index,
			data->bank2_settings[attr->index], 2) < 1)) {
		data->bank2_settings[attr->index][0] = orig_val;
		ret = -EIO;
	}
	mutex_unlock(&data->update_lock);
	return ret;
}

/* Fan PWM (speed control) */
static ssize_t show_pwm_setting(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	struct abituguru_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->pwm_settings[attr->index][attr->nr] *
		abituguru_pwm_settings_multiplier[attr->nr]);
}

static ssize_t store_pwm_setting(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	struct abituguru_data *data = dev_get_drvdata(dev);
	u8 min;
	unsigned long val;
	ssize_t ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	ret = count;
	val = (val + abituguru_pwm_settings_multiplier[attr->nr] / 2) /
				abituguru_pwm_settings_multiplier[attr->nr];

	/* special case pwm1 min pwm% */
	if ((attr->index == 0) && ((attr->nr == 1) || (attr->nr == 2)))
		min = 77;
	else
		min = abituguru_pwm_min[attr->nr];

	/* this check can be done before taking the lock */
	if (val < min || val > abituguru_pwm_max[attr->nr])
		return -EINVAL;

	mutex_lock(&data->update_lock);
	/* this check needs to be done after taking the lock */
	if ((attr->nr & 1) &&
			(val >= data->pwm_settings[attr->index][attr->nr + 1]))
		ret = -EINVAL;
	else if (!(attr->nr & 1) &&
			(val <= data->pwm_settings[attr->index][attr->nr - 1]))
		ret = -EINVAL;
	else if (data->pwm_settings[attr->index][attr->nr] != val) {
		u8 orig_val = data->pwm_settings[attr->index][attr->nr];
		data->pwm_settings[attr->index][attr->nr] = val;
		if (abituguru_write(data, ABIT_UGURU_FAN_PWM + 1,
				attr->index, data->pwm_settings[attr->index],
				5) <= attr->nr) {
			data->pwm_settings[attr->index][attr->nr] =
				orig_val;
			ret = -EIO;
		}
	}
	mutex_unlock(&data->update_lock);
	return ret;
}

static ssize_t show_pwm_sensor(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	struct abituguru_data *data = dev_get_drvdata(dev);
	int i;
	/*
	 * We need to walk to the temp sensor addresses to find what
	 * the userspace id of the configured temp sensor is.
	 */
	for (i = 0; i < data->bank1_sensors[ABIT_UGURU_TEMP_SENSOR]; i++)
		if (data->bank1_address[ABIT_UGURU_TEMP_SENSOR][i] ==
				(data->pwm_settings[attr->index][0] & 0x0F))
			return sprintf(buf, "%d\n", i+1);

	return -ENXIO;
}

static ssize_t store_pwm_sensor(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	struct abituguru_data *data = dev_get_drvdata(dev);
	ssize_t ret;
	unsigned long val;
	u8 orig_val;
	u8 address;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	if (val == 0 || val > data->bank1_sensors[ABIT_UGURU_TEMP_SENSOR])
		return -EINVAL;

	val -= 1;
	ret = count;
	mutex_lock(&data->update_lock);
	orig_val = data->pwm_settings[attr->index][0];
	address = data->bank1_address[ABIT_UGURU_TEMP_SENSOR][val];
	data->pwm_settings[attr->index][0] &= 0xF0;
	data->pwm_settings[attr->index][0] |= address;
	if (data->pwm_settings[attr->index][0] != orig_val) {
		if (abituguru_write(data, ABIT_UGURU_FAN_PWM + 1, attr->index,
				    data->pwm_settings[attr->index], 5) < 1) {
			data->pwm_settings[attr->index][0] = orig_val;
			ret = -EIO;
		}
	}
	mutex_unlock(&data->update_lock);
	return ret;
}

static ssize_t show_pwm_enable(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	struct abituguru_data *data = dev_get_drvdata(dev);
	int res = 0;
	if (data->pwm_settings[attr->index][0] & ABIT_UGURU_FAN_PWM_ENABLE)
		res = 2;
	return sprintf(buf, "%d\n", res);
}

static ssize_t store_pwm_enable(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	struct abituguru_data *data = dev_get_drvdata(dev);
	u8 orig_val;
	ssize_t ret;
	unsigned long user_val;

	ret = kstrtoul(buf, 10, &user_val);
	if (ret)
		return ret;

	ret = count;
	mutex_lock(&data->update_lock);
	orig_val = data->pwm_settings[attr->index][0];
	switch (user_val) {
	case 0:
		data->pwm_settings[attr->index][0] &=
			~ABIT_UGURU_FAN_PWM_ENABLE;
		break;
	case 2:
		data->pwm_settings[attr->index][0] |= ABIT_UGURU_FAN_PWM_ENABLE;
		break;
	default:
		ret = -EINVAL;
	}
	if ((data->pwm_settings[attr->index][0] != orig_val) &&
			(abituguru_write(data, ABIT_UGURU_FAN_PWM + 1,
			attr->index, data->pwm_settings[attr->index],
			5) < 1)) {
		data->pwm_settings[attr->index][0] = orig_val;
		ret = -EIO;
	}
	mutex_unlock(&data->update_lock);
	return ret;
}

static ssize_t show_name(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	return sprintf(buf, "%s\n", ABIT_UGURU_NAME);
}

/* Sysfs attr templates, the real entries are generated automatically. */
static const
struct sensor_device_attribute_2 abituguru_sysfs_bank1_templ[2][9] = {
	{
	SENSOR_ATTR_2(in%d_input, 0444, show_bank1_value, NULL, 0, 0),
	SENSOR_ATTR_2(in%d_min, 0644, show_bank1_setting,
		store_bank1_setting, 1, 0),
	SENSOR_ATTR_2(in%d_min_alarm, 0444, show_bank1_alarm, NULL,
		ABIT_UGURU_VOLT_LOW_ALARM_FLAG, 0),
	SENSOR_ATTR_2(in%d_max, 0644, show_bank1_setting,
		store_bank1_setting, 2, 0),
	SENSOR_ATTR_2(in%d_max_alarm, 0444, show_bank1_alarm, NULL,
		ABIT_UGURU_VOLT_HIGH_ALARM_FLAG, 0),
	SENSOR_ATTR_2(in%d_beep, 0644, show_bank1_mask,
		store_bank1_mask, ABIT_UGURU_BEEP_ENABLE, 0),
	SENSOR_ATTR_2(in%d_shutdown, 0644, show_bank1_mask,
		store_bank1_mask, ABIT_UGURU_SHUTDOWN_ENABLE, 0),
	SENSOR_ATTR_2(in%d_min_alarm_enable, 0644, show_bank1_mask,
		store_bank1_mask, ABIT_UGURU_VOLT_LOW_ALARM_ENABLE, 0),
	SENSOR_ATTR_2(in%d_max_alarm_enable, 0644, show_bank1_mask,
		store_bank1_mask, ABIT_UGURU_VOLT_HIGH_ALARM_ENABLE, 0),
	}, {
	SENSOR_ATTR_2(temp%d_input, 0444, show_bank1_value, NULL, 0, 0),
	SENSOR_ATTR_2(temp%d_alarm, 0444, show_bank1_alarm, NULL,
		ABIT_UGURU_TEMP_HIGH_ALARM_FLAG, 0),
	SENSOR_ATTR_2(temp%d_max, 0644, show_bank1_setting,
		store_bank1_setting, 1, 0),
	SENSOR_ATTR_2(temp%d_crit, 0644, show_bank1_setting,
		store_bank1_setting, 2, 0),
	SENSOR_ATTR_2(temp%d_beep, 0644, show_bank1_mask,
		store_bank1_mask, ABIT_UGURU_BEEP_ENABLE, 0),
	SENSOR_ATTR_2(temp%d_shutdown, 0644, show_bank1_mask,
		store_bank1_mask, ABIT_UGURU_SHUTDOWN_ENABLE, 0),
	SENSOR_ATTR_2(temp%d_alarm_enable, 0644, show_bank1_mask,
		store_bank1_mask, ABIT_UGURU_TEMP_HIGH_ALARM_ENABLE, 0),
	}
};

static const struct sensor_device_attribute_2 abituguru_sysfs_fan_templ[6] = {
	SENSOR_ATTR_2(fan%d_input, 0444, show_bank2_value, NULL, 0, 0),
	SENSOR_ATTR_2(fan%d_alarm, 0444, show_bank2_alarm, NULL, 0, 0),
	SENSOR_ATTR_2(fan%d_min, 0644, show_bank2_setting,
		store_bank2_setting, 1, 0),
	SENSOR_ATTR_2(fan%d_beep, 0644, show_bank2_mask,
		store_bank2_mask, ABIT_UGURU_BEEP_ENABLE, 0),
	SENSOR_ATTR_2(fan%d_shutdown, 0644, show_bank2_mask,
		store_bank2_mask, ABIT_UGURU_SHUTDOWN_ENABLE, 0),
	SENSOR_ATTR_2(fan%d_alarm_enable, 0644, show_bank2_mask,
		store_bank2_mask, ABIT_UGURU_FAN_LOW_ALARM_ENABLE, 0),
};

static const struct sensor_device_attribute_2 abituguru_sysfs_pwm_templ[6] = {
	SENSOR_ATTR_2(pwm%d_enable, 0644, show_pwm_enable,
		store_pwm_enable, 0, 0),
	SENSOR_ATTR_2(pwm%d_auto_channels_temp, 0644, show_pwm_sensor,
		store_pwm_sensor, 0, 0),
	SENSOR_ATTR_2(pwm%d_auto_point1_pwm, 0644, show_pwm_setting,
		store_pwm_setting, 1, 0),
	SENSOR_ATTR_2(pwm%d_auto_point2_pwm, 0644, show_pwm_setting,
		store_pwm_setting, 2, 0),
	SENSOR_ATTR_2(pwm%d_auto_point1_temp, 0644, show_pwm_setting,
		store_pwm_setting, 3, 0),
	SENSOR_ATTR_2(pwm%d_auto_point2_temp, 0644, show_pwm_setting,
		store_pwm_setting, 4, 0),
};

static struct sensor_device_attribute_2 abituguru_sysfs_attr[] = {
	SENSOR_ATTR_2(name, 0444, show_name, NULL, 0, 0),
};

static int __devinit abituguru_probe(struct platform_device *pdev)
{
	struct abituguru_data *data;
	int i, j, used, sysfs_names_free, sysfs_attr_i, res = -ENODEV;
	char *sysfs_filename;

	/*
	 * El weirdo probe order, to keep the sysfs order identical to the
	 * BIOS and window-appliction listing order.
	 */
	const u8 probe_order[ABIT_UGURU_MAX_BANK1_SENSORS] = {
		0x00, 0x01, 0x03, 0x04, 0x0A, 0x08, 0x0E, 0x02,
		0x09, 0x06, 0x05, 0x0B, 0x0F, 0x0D, 0x07, 0x0C };

	data = kzalloc(sizeof(struct abituguru_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->addr = platform_get_resource(pdev, IORESOURCE_IO, 0)->start;
	mutex_init(&data->update_lock);
	platform_set_drvdata(pdev, data);

	/* See if the uGuru is ready */
	if (inb_p(data->addr + ABIT_UGURU_DATA) == ABIT_UGURU_STATUS_INPUT)
		data->uguru_ready = 1;

	/*
	 * Completely read the uGuru this has 2 purposes:
	 * - testread / see if one really is there.
	 * - make an in memory copy of all the uguru settings for future use.
	 */
	if (abituguru_read(data, ABIT_UGURU_ALARM_BANK, 0,
			data->alarms, 3, ABIT_UGURU_MAX_RETRIES) != 3)
		goto abituguru_probe_error;

	for (i = 0; i < ABIT_UGURU_MAX_BANK1_SENSORS; i++) {
		if (abituguru_read(data, ABIT_UGURU_SENSOR_BANK1, i,
				&data->bank1_value[i], 1,
				ABIT_UGURU_MAX_RETRIES) != 1)
			goto abituguru_probe_error;
		if (abituguru_read(data, ABIT_UGURU_SENSOR_BANK1+1, i,
				data->bank1_settings[i], 3,
				ABIT_UGURU_MAX_RETRIES) != 3)
			goto abituguru_probe_error;
	}
	/*
	 * Note: We don't know how many bank2 sensors / pwms there really are,
	 * but in order to "detect" this we need to read the maximum amount
	 * anyways. If we read sensors/pwms not there we'll just read crap
	 * this can't hurt. We need the detection because we don't want
	 * unwanted writes, which will hurt!
	 */
	for (i = 0; i < ABIT_UGURU_MAX_BANK2_SENSORS; i++) {
		if (abituguru_read(data, ABIT_UGURU_SENSOR_BANK2, i,
				&data->bank2_value[i], 1,
				ABIT_UGURU_MAX_RETRIES) != 1)
			goto abituguru_probe_error;
		if (abituguru_read(data, ABIT_UGURU_SENSOR_BANK2+1, i,
				data->bank2_settings[i], 2,
				ABIT_UGURU_MAX_RETRIES) != 2)
			goto abituguru_probe_error;
	}
	for (i = 0; i < ABIT_UGURU_MAX_PWMS; i++) {
		if (abituguru_read(data, ABIT_UGURU_FAN_PWM, i,
				data->pwm_settings[i], 5,
				ABIT_UGURU_MAX_RETRIES) != 5)
			goto abituguru_probe_error;
	}
	data->last_updated = jiffies;

	/* Detect sensor types and fill the sysfs attr for bank1 */
	sysfs_attr_i = 0;
	sysfs_filename = data->sysfs_names;
	sysfs_names_free = ABITUGURU_SYSFS_NAMES_LENGTH;
	for (i = 0; i < ABIT_UGURU_MAX_BANK1_SENSORS; i++) {
		res = abituguru_detect_bank1_sensor_type(data, probe_order[i]);
		if (res < 0)
			goto abituguru_probe_error;
		if (res == ABIT_UGURU_NC)
			continue;

		/* res 1 (temp) sensors have 7 sysfs entries, 0 (in) 9 */
		for (j = 0; j < (res ? 7 : 9); j++) {
			used = snprintf(sysfs_filename, sysfs_names_free,
				abituguru_sysfs_bank1_templ[res][j].dev_attr.
				attr.name, data->bank1_sensors[res] + res)
				+ 1;
			data->sysfs_attr[sysfs_attr_i] =
				abituguru_sysfs_bank1_templ[res][j];
			data->sysfs_attr[sysfs_attr_i].dev_attr.attr.name =
				sysfs_filename;
			data->sysfs_attr[sysfs_attr_i].index = probe_order[i];
			sysfs_filename += used;
			sysfs_names_free -= used;
			sysfs_attr_i++;
		}
		data->bank1_max_value[probe_order[i]] =
			abituguru_bank1_max_value[res];
		data->bank1_address[res][data->bank1_sensors[res]] =
			probe_order[i];
		data->bank1_sensors[res]++;
	}
	/* Detect number of sensors and fill the sysfs attr for bank2 (fans) */
	abituguru_detect_no_bank2_sensors(data);
	for (i = 0; i < data->bank2_sensors; i++) {
		for (j = 0; j < ARRAY_SIZE(abituguru_sysfs_fan_templ); j++) {
			used = snprintf(sysfs_filename, sysfs_names_free,
				abituguru_sysfs_fan_templ[j].dev_attr.attr.name,
				i + 1) + 1;
			data->sysfs_attr[sysfs_attr_i] =
				abituguru_sysfs_fan_templ[j];
			data->sysfs_attr[sysfs_attr_i].dev_attr.attr.name =
				sysfs_filename;
			data->sysfs_attr[sysfs_attr_i].index = i;
			sysfs_filename += used;
			sysfs_names_free -= used;
			sysfs_attr_i++;
		}
	}
	/* Detect number of sensors and fill the sysfs attr for pwms */
	abituguru_detect_no_pwms(data);
	for (i = 0; i < data->pwms; i++) {
		for (j = 0; j < ARRAY_SIZE(abituguru_sysfs_pwm_templ); j++) {
			used = snprintf(sysfs_filename, sysfs_names_free,
				abituguru_sysfs_pwm_templ[j].dev_attr.attr.name,
				i + 1) + 1;
			data->sysfs_attr[sysfs_attr_i] =
				abituguru_sysfs_pwm_templ[j];
			data->sysfs_attr[sysfs_attr_i].dev_attr.attr.name =
				sysfs_filename;
			data->sysfs_attr[sysfs_attr_i].index = i;
			sysfs_filename += used;
			sysfs_names_free -= used;
			sysfs_attr_i++;
		}
	}
	/* Fail safe check, this should never happen! */
	if (sysfs_names_free < 0) {
		pr_err("Fatal error ran out of space for sysfs attr names. %s %s",
		       never_happen, report_this);
		res = -ENAMETOOLONG;
		goto abituguru_probe_error;
	}
	pr_info("found Abit uGuru\n");

	/* Register sysfs hooks */
	for (i = 0; i < sysfs_attr_i; i++) {
		res = device_create_file(&pdev->dev,
					 &data->sysfs_attr[i].dev_attr);
		if (res)
			goto abituguru_probe_error;
	}
	for (i = 0; i < ARRAY_SIZE(abituguru_sysfs_attr); i++) {
		res = device_create_file(&pdev->dev,
					 &abituguru_sysfs_attr[i].dev_attr);
		if (res)
			goto abituguru_probe_error;
	}

	data->hwmon_dev = hwmon_device_register(&pdev->dev);
	if (!IS_ERR(data->hwmon_dev))
		return 0; /* success */

	res = PTR_ERR(data->hwmon_dev);
abituguru_probe_error:
	for (i = 0; data->sysfs_attr[i].dev_attr.attr.name; i++)
		device_remove_file(&pdev->dev, &data->sysfs_attr[i].dev_attr);
	for (i = 0; i < ARRAY_SIZE(abituguru_sysfs_attr); i++)
		device_remove_file(&pdev->dev,
			&abituguru_sysfs_attr[i].dev_attr);
	platform_set_drvdata(pdev, NULL);
	kfree(data);
	return res;
}

static int __devexit abituguru_remove(struct platform_device *pdev)
{
	int i;
	struct abituguru_data *data = platform_get_drvdata(pdev);

	hwmon_device_unregister(data->hwmon_dev);
	for (i = 0; data->sysfs_attr[i].dev_attr.attr.name; i++)
		device_remove_file(&pdev->dev, &data->sysfs_attr[i].dev_attr);
	for (i = 0; i < ARRAY_SIZE(abituguru_sysfs_attr); i++)
		device_remove_file(&pdev->dev,
			&abituguru_sysfs_attr[i].dev_attr);
	platform_set_drvdata(pdev, NULL);
	kfree(data);

	return 0;
}

static struct abituguru_data *abituguru_update_device(struct device *dev)
{
	int i, err;
	struct abituguru_data *data = dev_get_drvdata(dev);
	/* fake a complete successful read if no update necessary. */
	char success = 1;

	mutex_lock(&data->update_lock);
	if (time_after(jiffies, data->last_updated + HZ)) {
		success = 0;
		err = abituguru_read(data, ABIT_UGURU_ALARM_BANK, 0,
				     data->alarms, 3, 0);
		if (err != 3)
			goto LEAVE_UPDATE;
		for (i = 0; i < ABIT_UGURU_MAX_BANK1_SENSORS; i++) {
			err = abituguru_read(data, ABIT_UGURU_SENSOR_BANK1,
					     i, &data->bank1_value[i], 1, 0);
			if (err != 1)
				goto LEAVE_UPDATE;
			err = abituguru_read(data, ABIT_UGURU_SENSOR_BANK1 + 1,
					     i, data->bank1_settings[i], 3, 0);
			if (err != 3)
				goto LEAVE_UPDATE;
		}
		for (i = 0; i < data->bank2_sensors; i++) {
			err = abituguru_read(data, ABIT_UGURU_SENSOR_BANK2, i,
					     &data->bank2_value[i], 1, 0);
			if (err != 1)
				goto LEAVE_UPDATE;
		}
		/* success! */
		success = 1;
		data->update_timeouts = 0;
LEAVE_UPDATE:
		/* handle timeout condition */
		if (!success && (err == -EBUSY || err >= 0)) {
			/* No overflow please */
			if (data->update_timeouts < 255u)
				data->update_timeouts++;
			if (data->update_timeouts <= ABIT_UGURU_MAX_TIMEOUTS) {
				ABIT_UGURU_DEBUG(3, "timeout exceeded, will "
					"try again next update\n");
				/* Just a timeout, fake a successful read */
				success = 1;
			} else
				ABIT_UGURU_DEBUG(1, "timeout exceeded %d "
					"times waiting for more input state\n",
					(int)data->update_timeouts);
		}
		/* On success set last_updated */
		if (success)
			data->last_updated = jiffies;
	}
	mutex_unlock(&data->update_lock);

	if (success)
		return data;
	else
		return NULL;
}

#ifdef CONFIG_PM
static int abituguru_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct abituguru_data *data = platform_get_drvdata(pdev);
	/*
	 * make sure all communications with the uguru are done and no new
	 * ones are started
	 */
	mutex_lock(&data->update_lock);
	return 0;
}

static int abituguru_resume(struct platform_device *pdev)
{
	struct abituguru_data *data = platform_get_drvdata(pdev);
	/* See if the uGuru is still ready */
	if (inb_p(data->addr + ABIT_UGURU_DATA) != ABIT_UGURU_STATUS_INPUT)
		data->uguru_ready = 0;
	mutex_unlock(&data->update_lock);
	return 0;
}
#else
#define abituguru_suspend	NULL
#define abituguru_resume	NULL
#endif /* CONFIG_PM */

static struct platform_driver abituguru_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= ABIT_UGURU_NAME,
	},
	.probe		= abituguru_probe,
	.remove		= __devexit_p(abituguru_remove),
	.suspend	= abituguru_suspend,
	.resume		= abituguru_resume,
};

static int __init abituguru_detect(void)
{
	/*
	 * See if there is an uguru there. After a reboot uGuru will hold 0x00
	 * at DATA and 0xAC, when this driver has already been loaded once
	 * DATA will hold 0x08. For most uGuru's CMD will hold 0xAC in either
	 * scenario but some will hold 0x00.
	 * Some uGuru's initially hold 0x09 at DATA and will only hold 0x08
	 * after reading CMD first, so CMD must be read first!
	 */
	u8 cmd_val = inb_p(ABIT_UGURU_BASE + ABIT_UGURU_CMD);
	u8 data_val = inb_p(ABIT_UGURU_BASE + ABIT_UGURU_DATA);
	if (((data_val == 0x00) || (data_val == 0x08)) &&
	    ((cmd_val == 0x00) || (cmd_val == 0xAC)))
		return ABIT_UGURU_BASE;

	ABIT_UGURU_DEBUG(2, "no Abit uGuru found, data = 0x%02X, cmd = "
		"0x%02X\n", (unsigned int)data_val, (unsigned int)cmd_val);

	if (force) {
		pr_info("Assuming Abit uGuru is present because of \"force\" parameter\n");
		return ABIT_UGURU_BASE;
	}

	/* No uGuru found */
	return -ENODEV;
}

static struct platform_device *abituguru_pdev;

static int __init abituguru_init(void)
{
	int address, err;
	struct resource res = { .flags = IORESOURCE_IO };
	const char *board_vendor = dmi_get_system_info(DMI_BOARD_VENDOR);

	/* safety check, refuse to load on non Abit motherboards */
	if (!force && (!board_vendor ||
			strcmp(board_vendor, "http://www.abit.com.tw/")))
		return -ENODEV;

	address = abituguru_detect();
	if (address < 0)
		return address;

	err = platform_driver_register(&abituguru_driver);
	if (err)
		goto exit;

	abituguru_pdev = platform_device_alloc(ABIT_UGURU_NAME, address);
	if (!abituguru_pdev) {
		pr_err("Device allocation failed\n");
		err = -ENOMEM;
		goto exit_driver_unregister;
	}

	res.start = address;
	res.end = address + ABIT_UGURU_REGION_LENGTH - 1;
	res.name = ABIT_UGURU_NAME;

	err = platform_device_add_resources(abituguru_pdev, &res, 1);
	if (err) {
		pr_err("Device resource addition failed (%d)\n", err);
		goto exit_device_put;
	}

	err = platform_device_add(abituguru_pdev);
	if (err) {
		pr_err("Device addition failed (%d)\n", err);
		goto exit_device_put;
	}

	return 0;

exit_device_put:
	platform_device_put(abituguru_pdev);
exit_driver_unregister:
	platform_driver_unregister(&abituguru_driver);
exit:
	return err;
}

static void __exit abituguru_exit(void)
{
	platform_device_unregister(abituguru_pdev);
	platform_driver_unregister(&abituguru_driver);
}

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION("Abit uGuru Sensor device");
MODULE_LICENSE("GPL");

module_init(abituguru_init);
module_exit(abituguru_exit);
