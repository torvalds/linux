// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * dell-smm-hwmon.c -- Linux driver for accessing the SMM BIOS on Dell laptops.
 *
 * Copyright (C) 2001  Massimo Dal Zotto <dz@debian.org>
 *
 * Hwmon integration:
 * Copyright (C) 2011  Jean Delvare <jdelvare@suse.de>
 * Copyright (C) 2013, 2014  Guenter Roeck <linux@roeck-us.net>
 * Copyright (C) 2014, 2015  Pali Rohár <pali@kernel.org>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/capability.h>
#include <linux/cpu.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/kconfig.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <linux/thermal.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include <linux/i8k.h>

#define I8K_SMM_FN_STATUS	0x0025
#define I8K_SMM_POWER_STATUS	0x0069
#define I8K_SMM_SET_FAN		0x01a3
#define I8K_SMM_GET_FAN		0x00a3
#define I8K_SMM_GET_SPEED	0x02a3
#define I8K_SMM_GET_FAN_TYPE	0x03a3
#define I8K_SMM_GET_NOM_SPEED	0x04a3
#define I8K_SMM_GET_TEMP	0x10a3
#define I8K_SMM_GET_TEMP_TYPE	0x11a3
#define I8K_SMM_GET_DELL_SIG1	0xfea3
#define I8K_SMM_GET_DELL_SIG2	0xffa3

/* in usecs */
#define DELL_SMM_MAX_DURATION  250000

#define I8K_FAN_MULT		30
#define I8K_FAN_RPM_THRESHOLD	1000
#define I8K_MAX_TEMP		127

#define I8K_FN_NONE		0x00
#define I8K_FN_UP		0x01
#define I8K_FN_DOWN		0x02
#define I8K_FN_MUTE		0x04
#define I8K_FN_MASK		0x07
#define I8K_FN_SHIFT		8

#define I8K_POWER_AC		0x05
#define I8K_POWER_BATTERY	0x01

#define DELL_SMM_NO_TEMP	10
#define DELL_SMM_NO_FANS	3

struct dell_smm_data {
	struct mutex i8k_mutex; /* lock for sensors writes */
	char bios_version[4];
	char bios_machineid[16];
	uint i8k_fan_mult;
	uint i8k_pwm_mult;
	uint i8k_fan_max;
	bool disallow_fan_type_call;
	bool disallow_fan_support;
	unsigned int manual_fan;
	unsigned int auto_fan;
	int temp_type[DELL_SMM_NO_TEMP];
	bool fan[DELL_SMM_NO_FANS];
	int fan_type[DELL_SMM_NO_FANS];
	int *fan_nominal_speed[DELL_SMM_NO_FANS];
};

struct dell_smm_cooling_data {
	u8 fan_num;
	struct dell_smm_data *data;
};

MODULE_AUTHOR("Massimo Dal Zotto (dz@debian.org)");
MODULE_AUTHOR("Pali Rohár <pali@kernel.org>");
MODULE_DESCRIPTION("Dell laptop SMM BIOS hwmon driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("i8k");

static bool force;
module_param_unsafe(force, bool, 0);
MODULE_PARM_DESC(force, "Force loading without checking for supported models and features");

static bool ignore_dmi;
module_param(ignore_dmi, bool, 0);
MODULE_PARM_DESC(ignore_dmi, "Continue probing hardware even if DMI data does not match");

#if IS_ENABLED(CONFIG_I8K)
static bool restricted = true;
module_param(restricted, bool, 0);
MODULE_PARM_DESC(restricted, "Restrict fan control and serial number to CAP_SYS_ADMIN (default: 1)");

static bool power_status;
module_param(power_status, bool, 0600);
MODULE_PARM_DESC(power_status, "Report power status in /proc/i8k (default: 0)");
#endif

static uint fan_mult;
module_param(fan_mult, uint, 0);
MODULE_PARM_DESC(fan_mult, "Factor to multiply fan speed with (default: autodetect)");

static uint fan_max;
module_param(fan_max, uint, 0);
MODULE_PARM_DESC(fan_max, "Maximum configurable fan speed (default: autodetect)");

struct smm_regs {
	unsigned int eax;
	unsigned int ebx;
	unsigned int ecx;
	unsigned int edx;
	unsigned int esi;
	unsigned int edi;
};

static const char * const temp_labels[] = {
	"CPU",
	"GPU",
	"SODIMM",
	"Other",
	"Ambient",
	"Other",
};

static const char * const fan_labels[] = {
	"Processor Fan",
	"Motherboard Fan",
	"Video Fan",
	"Power Supply Fan",
	"Chipset Fan",
	"Other Fan",
};

static const char * const docking_labels[] = {
	"Docking Processor Fan",
	"Docking Motherboard Fan",
	"Docking Video Fan",
	"Docking Power Supply Fan",
	"Docking Chipset Fan",
	"Docking Other Fan",
};

static inline const char __init *i8k_get_dmi_data(int field)
{
	const char *dmi_data = dmi_get_system_info(field);

	return dmi_data && *dmi_data ? dmi_data : "?";
}

/*
 * Call the System Management Mode BIOS. Code provided by Jonathan Buzzard.
 */
static int i8k_smm_func(void *par)
{
	ktime_t calltime = ktime_get();
	struct smm_regs *regs = par;
	int eax = regs->eax;
	int ebx = regs->ebx;
	unsigned char carry;
	long long duration;

	/* SMM requires CPU 0 */
	if (smp_processor_id() != 0)
		return -EBUSY;

	asm volatile("out %%al,$0xb2\n\t"
		     "out %%al,$0x84\n\t"
		     "setc %0\n"
		     : "=mr" (carry),
		       "+a" (regs->eax),
		       "+b" (regs->ebx),
		       "+c" (regs->ecx),
		       "+d" (regs->edx),
		       "+S" (regs->esi),
		       "+D" (regs->edi));

	duration = ktime_us_delta(ktime_get(), calltime);
	pr_debug("smm(0x%.4x 0x%.4x) = 0x%.4x carry: %d (took %7lld usecs)\n",
		 eax, ebx, regs->eax & 0xffff, carry, duration);

	if (duration > DELL_SMM_MAX_DURATION)
		pr_warn_once("SMM call took %lld usecs!\n", duration);

	if (carry || (regs->eax & 0xffff) == 0xffff || regs->eax == eax)
		return -EINVAL;

	return 0;
}

/*
 * Call the System Management Mode BIOS.
 */
static int i8k_smm(struct smm_regs *regs)
{
	int ret;

	cpus_read_lock();
	ret = smp_call_on_cpu(0, i8k_smm_func, regs, true);
	cpus_read_unlock();

	return ret;
}

/*
 * Read the fan status.
 */
static int i8k_get_fan_status(const struct dell_smm_data *data, u8 fan)
{
	struct smm_regs regs = {
		.eax = I8K_SMM_GET_FAN,
		.ebx = fan,
	};

	if (data->disallow_fan_support)
		return -EINVAL;

	return i8k_smm(&regs) ? : regs.eax & 0xff;
}

/*
 * Read the fan speed in RPM.
 */
static int i8k_get_fan_speed(const struct dell_smm_data *data, u8 fan)
{
	struct smm_regs regs = {
		.eax = I8K_SMM_GET_SPEED,
		.ebx = fan,
	};

	if (data->disallow_fan_support)
		return -EINVAL;

	return i8k_smm(&regs) ? : (regs.eax & 0xffff) * data->i8k_fan_mult;
}

/*
 * Read the fan type.
 */
static int _i8k_get_fan_type(const struct dell_smm_data *data, u8 fan)
{
	struct smm_regs regs = {
		.eax = I8K_SMM_GET_FAN_TYPE,
		.ebx = fan,
	};

	if (data->disallow_fan_support || data->disallow_fan_type_call)
		return -EINVAL;

	return i8k_smm(&regs) ? : regs.eax & 0xff;
}

static int i8k_get_fan_type(struct dell_smm_data *data, u8 fan)
{
	/* I8K_SMM_GET_FAN_TYPE SMM call is expensive, so cache values */
	if (data->fan_type[fan] == INT_MIN)
		data->fan_type[fan] = _i8k_get_fan_type(data, fan);

	return data->fan_type[fan];
}

/*
 * Read the fan nominal rpm for specific fan speed.
 */
static int __init i8k_get_fan_nominal_speed(const struct dell_smm_data *data, u8 fan, int speed)
{
	struct smm_regs regs = {
		.eax = I8K_SMM_GET_NOM_SPEED,
		.ebx = fan | (speed << 8),
	};

	if (data->disallow_fan_support)
		return -EINVAL;

	return i8k_smm(&regs) ? : (regs.eax & 0xffff);
}

/*
 * Enable or disable automatic BIOS fan control support
 */
static int i8k_enable_fan_auto_mode(const struct dell_smm_data *data, bool enable)
{
	struct smm_regs regs = { };

	if (data->disallow_fan_support)
		return -EINVAL;

	regs.eax = enable ? data->auto_fan : data->manual_fan;
	return i8k_smm(&regs);
}

/*
 * Set the fan speed (off, low, high, ...).
 */
static int i8k_set_fan(const struct dell_smm_data *data, u8 fan, int speed)
{
	struct smm_regs regs = { .eax = I8K_SMM_SET_FAN, };

	if (data->disallow_fan_support)
		return -EINVAL;

	speed = (speed < 0) ? 0 : ((speed > data->i8k_fan_max) ? data->i8k_fan_max : speed);
	regs.ebx = fan | (speed << 8);

	return i8k_smm(&regs);
}

static int __init i8k_get_temp_type(u8 sensor)
{
	struct smm_regs regs = {
		.eax = I8K_SMM_GET_TEMP_TYPE,
		.ebx = sensor,
	};

	return i8k_smm(&regs) ? : regs.eax & 0xff;
}

/*
 * Read the cpu temperature.
 */
static int _i8k_get_temp(u8 sensor)
{
	struct smm_regs regs = {
		.eax = I8K_SMM_GET_TEMP,
		.ebx = sensor,
	};

	return i8k_smm(&regs) ? : regs.eax & 0xff;
}

static int i8k_get_temp(u8 sensor)
{
	int temp = _i8k_get_temp(sensor);

	/*
	 * Sometimes the temperature sensor returns 0x99, which is out of range.
	 * In this case we retry (once) before returning an error.
	 # 1003655137 00000058 00005a4b
	 # 1003655138 00000099 00003a80 <--- 0x99 = 153 degrees
	 # 1003655139 00000054 00005c52
	 */
	if (temp == 0x99) {
		msleep(100);
		temp = _i8k_get_temp(sensor);
	}
	/*
	 * Return -ENODATA for all invalid temperatures.
	 *
	 * Known instances are the 0x99 value as seen above as well as
	 * 0xc1 (193), which may be returned when trying to read the GPU
	 * temperature if the system supports a GPU and it is currently
	 * turned off.
	 */
	if (temp > I8K_MAX_TEMP)
		return -ENODATA;

	return temp;
}

static int __init i8k_get_dell_signature(int req_fn)
{
	struct smm_regs regs = { .eax = req_fn, };
	int rc;

	rc = i8k_smm(&regs);
	if (rc < 0)
		return rc;

	return regs.eax == 1145651527 && regs.edx == 1145392204 ? 0 : -1;
}

#if IS_ENABLED(CONFIG_I8K)

/*
 * Read the Fn key status.
 */
static int i8k_get_fn_status(void)
{
	struct smm_regs regs = { .eax = I8K_SMM_FN_STATUS, };
	int rc;

	rc = i8k_smm(&regs);
	if (rc < 0)
		return rc;

	switch ((regs.eax >> I8K_FN_SHIFT) & I8K_FN_MASK) {
	case I8K_FN_UP:
		return I8K_VOL_UP;
	case I8K_FN_DOWN:
		return I8K_VOL_DOWN;
	case I8K_FN_MUTE:
		return I8K_VOL_MUTE;
	default:
		return 0;
	}
}

/*
 * Read the power status.
 */
static int i8k_get_power_status(void)
{
	struct smm_regs regs = { .eax = I8K_SMM_POWER_STATUS, };
	int rc;

	rc = i8k_smm(&regs);
	if (rc < 0)
		return rc;

	return (regs.eax & 0xff) == I8K_POWER_AC ? I8K_AC : I8K_BATTERY;
}

/*
 * Procfs interface
 */

static long i8k_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	struct dell_smm_data *data = pde_data(file_inode(fp));
	int __user *argp = (int __user *)arg;
	int speed, err;
	int val = 0;

	if (!argp)
		return -EINVAL;

	switch (cmd) {
	case I8K_BIOS_VERSION:
		if (!isdigit(data->bios_version[0]) || !isdigit(data->bios_version[1]) ||
		    !isdigit(data->bios_version[2]))
			return -EINVAL;

		val = (data->bios_version[0] << 16) |
				(data->bios_version[1] << 8) | data->bios_version[2];

		if (copy_to_user(argp, &val, sizeof(val)))
			return -EFAULT;

		return 0;
	case I8K_MACHINE_ID:
		if (restricted && !capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (copy_to_user(argp, data->bios_machineid, sizeof(data->bios_machineid)))
			return -EFAULT;

		return 0;
	case I8K_FN_STATUS:
		val = i8k_get_fn_status();
		break;

	case I8K_POWER_STATUS:
		val = i8k_get_power_status();
		break;

	case I8K_GET_TEMP:
		val = i8k_get_temp(0);
		break;

	case I8K_GET_SPEED:
		if (copy_from_user(&val, argp, sizeof(int)))
			return -EFAULT;

		if (val > U8_MAX || val < 0)
			return -EINVAL;

		val = i8k_get_fan_speed(data, val);
		break;

	case I8K_GET_FAN:
		if (copy_from_user(&val, argp, sizeof(int)))
			return -EFAULT;

		if (val > U8_MAX || val < 0)
			return -EINVAL;

		val = i8k_get_fan_status(data, val);
		break;

	case I8K_SET_FAN:
		if (restricted && !capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (copy_from_user(&val, argp, sizeof(int)))
			return -EFAULT;

		if (val > U8_MAX || val < 0)
			return -EINVAL;

		if (copy_from_user(&speed, argp + 1, sizeof(int)))
			return -EFAULT;

		mutex_lock(&data->i8k_mutex);
		err = i8k_set_fan(data, val, speed);
		if (err < 0)
			val = err;
		else
			val = i8k_get_fan_status(data, val);
		mutex_unlock(&data->i8k_mutex);
		break;

	default:
		return -ENOIOCTLCMD;
	}

	if (val < 0)
		return val;

	if (copy_to_user(argp, &val, sizeof(int)))
		return -EFAULT;

	return 0;
}

/*
 * Print the information for /proc/i8k.
 */
static int i8k_proc_show(struct seq_file *seq, void *offset)
{
	struct dell_smm_data *data = seq->private;
	int fn_key, cpu_temp, ac_power;
	int left_fan, right_fan, left_speed, right_speed;

	cpu_temp	= i8k_get_temp(0);				/* 11100 µs */
	left_fan	= i8k_get_fan_status(data, I8K_FAN_LEFT);	/*   580 µs */
	right_fan	= i8k_get_fan_status(data, I8K_FAN_RIGHT);	/*   580 µs */
	left_speed	= i8k_get_fan_speed(data, I8K_FAN_LEFT);	/*   580 µs */
	right_speed	= i8k_get_fan_speed(data, I8K_FAN_RIGHT);	/*   580 µs */
	fn_key		= i8k_get_fn_status();				/*   750 µs */
	if (power_status)
		ac_power = i8k_get_power_status();			/* 14700 µs */
	else
		ac_power = -1;

	/*
	 * Info:
	 *
	 * 1)  Format version (this will change if format changes)
	 * 2)  BIOS version
	 * 3)  BIOS machine ID
	 * 4)  Cpu temperature
	 * 5)  Left fan status
	 * 6)  Right fan status
	 * 7)  Left fan speed
	 * 8)  Right fan speed
	 * 9)  AC power
	 * 10) Fn Key status
	 */
	seq_printf(seq, "%s %s %s %d %d %d %d %d %d %d\n",
		   I8K_PROC_FMT,
		   data->bios_version,
		   (restricted && !capable(CAP_SYS_ADMIN)) ? "-1" : data->bios_machineid,
		   cpu_temp,
		   left_fan, right_fan, left_speed, right_speed,
		   ac_power, fn_key);

	return 0;
}

static int i8k_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, i8k_proc_show, pde_data(inode));
}

static const struct proc_ops i8k_proc_ops = {
	.proc_open	= i8k_open_fs,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
	.proc_ioctl	= i8k_ioctl,
};

static void i8k_exit_procfs(void *param)
{
	remove_proc_entry("i8k", NULL);
}

static void __init i8k_init_procfs(struct device *dev)
{
	struct dell_smm_data *data = dev_get_drvdata(dev);

	/* Only register exit function if creation was successful */
	if (proc_create_data("i8k", 0, NULL, &i8k_proc_ops, data))
		devm_add_action_or_reset(dev, i8k_exit_procfs, NULL);
}

#else

static void __init i8k_init_procfs(struct device *dev)
{
}

#endif

static int dell_smm_get_max_state(struct thermal_cooling_device *dev, unsigned long *state)
{
	struct dell_smm_cooling_data *cdata = dev->devdata;

	*state = cdata->data->i8k_fan_max;

	return 0;
}

static int dell_smm_get_cur_state(struct thermal_cooling_device *dev, unsigned long *state)
{
	struct dell_smm_cooling_data *cdata = dev->devdata;
	int ret;

	ret = i8k_get_fan_status(cdata->data, cdata->fan_num);
	if (ret < 0)
		return ret;

	*state = ret;

	return 0;
}

static int dell_smm_set_cur_state(struct thermal_cooling_device *dev, unsigned long state)
{
	struct dell_smm_cooling_data *cdata = dev->devdata;
	struct dell_smm_data *data = cdata->data;
	int ret;

	if (state > data->i8k_fan_max)
		return -EINVAL;

	mutex_lock(&data->i8k_mutex);
	ret = i8k_set_fan(data, cdata->fan_num, (int)state);
	mutex_unlock(&data->i8k_mutex);

	return ret;
}

static const struct thermal_cooling_device_ops dell_smm_cooling_ops = {
	.get_max_state = dell_smm_get_max_state,
	.get_cur_state = dell_smm_get_cur_state,
	.set_cur_state = dell_smm_set_cur_state,
};

static umode_t dell_smm_is_visible(const void *drvdata, enum hwmon_sensor_types type, u32 attr,
				   int channel)
{
	const struct dell_smm_data *data = drvdata;

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			/* _i8k_get_temp() is fine since we do not care about the actual value */
			if (data->temp_type[channel] >= 0 || _i8k_get_temp(channel) >= 0)
				return 0444;

			break;
		case hwmon_temp_label:
			if (data->temp_type[channel] >= 0)
				return 0444;

			break;
		default:
			break;
		}
		break;
	case hwmon_fan:
		if (data->disallow_fan_support)
			break;

		switch (attr) {
		case hwmon_fan_input:
			if (data->fan[channel])
				return 0444;

			break;
		case hwmon_fan_label:
			if (data->fan[channel] && !data->disallow_fan_type_call)
				return 0444;

			break;
		case hwmon_fan_min:
		case hwmon_fan_max:
		case hwmon_fan_target:
			if (data->fan_nominal_speed[channel])
				return 0444;

			break;
		default:
			break;
		}
		break;
	case hwmon_pwm:
		if (data->disallow_fan_support)
			break;

		switch (attr) {
		case hwmon_pwm_input:
			if (data->fan[channel])
				return 0644;

			break;
		case hwmon_pwm_enable:
			if (data->auto_fan)
				/*
				 * There is no command for retrieve the current status
				 * from BIOS, and userspace/firmware itself can change
				 * it.
				 * Thus we can only provide write-only access for now.
				 */
				return 0200;

			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return 0;
}

static int dell_smm_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
			 long *val)
{
	struct dell_smm_data *data = dev_get_drvdata(dev);
	int mult = data->i8k_fan_mult;
	int ret;

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			ret = i8k_get_temp(channel);
			if (ret < 0)
				return ret;

			*val = ret * 1000;

			return 0;
		default:
			break;
		}
		break;
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_input:
			ret = i8k_get_fan_speed(data, channel);
			if (ret < 0)
				return ret;

			*val = ret;

			return 0;
		case hwmon_fan_min:
			*val = data->fan_nominal_speed[channel][0] * mult;

			return 0;
		case hwmon_fan_max:
			*val = data->fan_nominal_speed[channel][data->i8k_fan_max] * mult;

			return 0;
		case hwmon_fan_target:
			ret = i8k_get_fan_status(data, channel);
			if (ret < 0)
				return ret;

			if (ret > data->i8k_fan_max)
				ret = data->i8k_fan_max;

			*val = data->fan_nominal_speed[channel][ret] * mult;

			return 0;
		default:
			break;
		}
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			ret = i8k_get_fan_status(data, channel);
			if (ret < 0)
				return ret;

			*val = clamp_val(ret * data->i8k_pwm_mult, 0, 255);

			return 0;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static const char *dell_smm_fan_label(struct dell_smm_data *data, int channel)
{
	bool dock = false;
	int type = i8k_get_fan_type(data, channel);

	if (type < 0)
		return ERR_PTR(type);

	if (type & 0x10) {
		dock = true;
		type &= 0x0F;
	}

	if (type >= ARRAY_SIZE(fan_labels))
		type = ARRAY_SIZE(fan_labels) - 1;

	return dock ? docking_labels[type] : fan_labels[type];
}

static int dell_smm_read_string(struct device *dev, enum hwmon_sensor_types type, u32 attr,
				int channel, const char **str)
{
	struct dell_smm_data *data = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_label:
			*str = temp_labels[data->temp_type[channel]];
			return 0;
		default:
			break;
		}
		break;
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_label:
			*str = dell_smm_fan_label(data, channel);
			return PTR_ERR_OR_ZERO(*str);
		default:
			break;
		}
		break;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int dell_smm_write(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
			  long val)
{
	struct dell_smm_data *data = dev_get_drvdata(dev);
	unsigned long pwm;
	bool enable;
	int err;

	switch (type) {
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			pwm = clamp_val(DIV_ROUND_CLOSEST(val, data->i8k_pwm_mult), 0,
					data->i8k_fan_max);

			mutex_lock(&data->i8k_mutex);
			err = i8k_set_fan(data, channel, pwm);
			mutex_unlock(&data->i8k_mutex);

			if (err < 0)
				return err;

			return 0;
		case hwmon_pwm_enable:
			if (!val)
				return -EINVAL;

			if (val == 1)
				enable = false;
			else
				enable = true;

			mutex_lock(&data->i8k_mutex);
			err = i8k_enable_fan_auto_mode(data, enable);
			mutex_unlock(&data->i8k_mutex);

			if (err < 0)
				return err;

			return 0;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static const struct hwmon_ops dell_smm_ops = {
	.is_visible = dell_smm_is_visible,
	.read = dell_smm_read,
	.read_string = dell_smm_read_string,
	.write = dell_smm_write,
};

static const struct hwmon_channel_info *dell_smm_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ),
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
			   HWMON_T_INPUT | HWMON_T_LABEL
			   ),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MIN | HWMON_F_MAX |
			   HWMON_F_TARGET,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MIN | HWMON_F_MAX |
			   HWMON_F_TARGET,
			   HWMON_F_INPUT | HWMON_F_LABEL | HWMON_F_MIN | HWMON_F_MAX |
			   HWMON_F_TARGET
			   ),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT
			   ),
	NULL
};

static const struct hwmon_chip_info dell_smm_chip_info = {
	.ops = &dell_smm_ops,
	.info = dell_smm_info,
};

static int __init dell_smm_init_cdev(struct device *dev, u8 fan_num)
{
	struct dell_smm_data *data = dev_get_drvdata(dev);
	struct thermal_cooling_device *cdev;
	struct dell_smm_cooling_data *cdata;
	int ret = 0;
	char *name;

	name = kasprintf(GFP_KERNEL, "dell-smm-fan%u", fan_num + 1);
	if (!name)
		return -ENOMEM;

	cdata = devm_kmalloc(dev, sizeof(*cdata), GFP_KERNEL);
	if (cdata) {
		cdata->fan_num = fan_num;
		cdata->data = data;
		cdev = devm_thermal_of_cooling_device_register(dev, NULL, name, cdata,
							       &dell_smm_cooling_ops);
		if (IS_ERR(cdev)) {
			devm_kfree(dev, cdata);
			ret = PTR_ERR(cdev);
		}
	} else {
		ret = -ENOMEM;
	}

	kfree(name);

	return ret;
}

static int __init dell_smm_init_hwmon(struct device *dev)
{
	struct dell_smm_data *data = dev_get_drvdata(dev);
	struct device *dell_smm_hwmon_dev;
	int state, err;
	u8 i;

	for (i = 0; i < DELL_SMM_NO_TEMP; i++) {
		data->temp_type[i] = i8k_get_temp_type(i);
		if (data->temp_type[i] < 0)
			continue;

		if (data->temp_type[i] >= ARRAY_SIZE(temp_labels))
			data->temp_type[i] = ARRAY_SIZE(temp_labels) - 1;
	}

	for (i = 0; i < DELL_SMM_NO_FANS; i++) {
		data->fan_type[i] = INT_MIN;
		err = i8k_get_fan_status(data, i);
		if (err < 0)
			err = i8k_get_fan_type(data, i);

		if (err < 0)
			continue;

		data->fan[i] = true;

		/* the cooling device is not critical, ignore failures */
		if (IS_REACHABLE(CONFIG_THERMAL)) {
			err = dell_smm_init_cdev(dev, i);
			if (err < 0)
				dev_warn(dev, "Failed to register cooling device for fan %u\n",
					 i + 1);
		}

		data->fan_nominal_speed[i] = devm_kmalloc_array(dev, data->i8k_fan_max + 1,
								sizeof(*data->fan_nominal_speed[i]),
								GFP_KERNEL);
		if (!data->fan_nominal_speed[i])
			continue;

		for (state = 0; state <= data->i8k_fan_max; state++) {
			err = i8k_get_fan_nominal_speed(data, i, state);
			if (err < 0) {
				/* Mark nominal speed table as invalid in case of error */
				devm_kfree(dev, data->fan_nominal_speed[i]);
				data->fan_nominal_speed[i] = NULL;
				break;
			}
			data->fan_nominal_speed[i][state] = err;
			/*
			 * Autodetect fan multiplier based on nominal rpm if multiplier
			 * was not specified as module param or in DMI. If fan reports
			 * rpm value too high then set multiplier to 1.
			 */
			if (!fan_mult && err > I8K_FAN_RPM_THRESHOLD)
				data->i8k_fan_mult = 1;
		}
	}

	dell_smm_hwmon_dev = devm_hwmon_device_register_with_info(dev, "dell_smm", data,
								  &dell_smm_chip_info, NULL);

	return PTR_ERR_OR_ZERO(dell_smm_hwmon_dev);
}

struct i8k_config_data {
	uint fan_mult;
	uint fan_max;
};

enum i8k_configs {
	DELL_LATITUDE_D520,
	DELL_PRECISION_490,
	DELL_STUDIO,
	DELL_XPS,
};

/*
 * Only use for machines which need some special configuration
 * in order to work correctly (e.g. if autoconfig fails on this machines).
 */

static const struct i8k_config_data i8k_config_data[] __initconst = {
	[DELL_LATITUDE_D520] = {
		.fan_mult = 1,
		.fan_max = I8K_FAN_TURBO,
	},
	[DELL_PRECISION_490] = {
		.fan_mult = 1,
		.fan_max = I8K_FAN_TURBO,
	},
	[DELL_STUDIO] = {
		.fan_mult = 1,
		.fan_max = I8K_FAN_HIGH,
	},
	[DELL_XPS] = {
		.fan_mult = 1,
		.fan_max = I8K_FAN_HIGH,
	},
};

static const struct dmi_system_id i8k_dmi_table[] __initconst = {
	{
		.ident = "Dell G5 5590",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "G5 5590"),
		},
	},
	{
		.ident = "Dell Inspiron",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Computer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron"),
		},
	},
	{
		.ident = "Dell Latitude",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Computer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Latitude"),
		},
	},
	{
		.ident = "Dell Inspiron 2",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron"),
		},
	},
	{
		.ident = "Dell Latitude D520",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Latitude D520"),
		},
		.driver_data = (void *)&i8k_config_data[DELL_LATITUDE_D520],
	},
	{
		.ident = "Dell Latitude 2",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Latitude"),
		},
	},
	{	/* UK Inspiron 6400  */
		.ident = "Dell Inspiron 3",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MM061"),
		},
	},
	{
		.ident = "Dell Inspiron 3",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MP061"),
		},
	},
	{
		.ident = "Dell Precision 490",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME,
				  "Precision WorkStation 490"),
		},
		.driver_data = (void *)&i8k_config_data[DELL_PRECISION_490],
	},
	{
		.ident = "Dell Precision",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Precision"),
		},
	},
	{
		.ident = "Dell Vostro",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Vostro"),
		},
	},
	{
		.ident = "Dell Studio",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Studio"),
		},
		.driver_data = (void *)&i8k_config_data[DELL_STUDIO],
	},
	{
		.ident = "Dell XPS M140",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MXC051"),
		},
		.driver_data = (void *)&i8k_config_data[DELL_XPS],
	},
	{
		.ident = "Dell XPS",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "XPS"),
		},
	},
	{ }
};

MODULE_DEVICE_TABLE(dmi, i8k_dmi_table);

/*
 * On some machines once I8K_SMM_GET_FAN_TYPE is issued then CPU fan speed
 * randomly going up and down due to bug in Dell SMM or BIOS. Here is blacklist
 * of affected Dell machines for which we disallow I8K_SMM_GET_FAN_TYPE call.
 * See bug: https://bugzilla.kernel.org/show_bug.cgi?id=100121
 */
static const struct dmi_system_id i8k_blacklist_fan_type_dmi_table[] __initconst = {
	{
		.ident = "Dell Studio XPS 8000",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Studio XPS 8000"),
		},
	},
	{
		.ident = "Dell Studio XPS 8100",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Studio XPS 8100"),
		},
	},
	{
		.ident = "Dell Inspiron 580",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Inspiron 580 "),
		},
	},
	{
		.ident = "Dell Inspiron 3505",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Inspiron 3505"),
		},
	},
	{ }
};

/*
 * On some machines all fan related SMM functions implemented by Dell BIOS
 * firmware freeze kernel for about 500ms. Until Dell fixes these problems fan
 * support for affected blacklisted Dell machines stay disabled.
 * See bug: https://bugzilla.kernel.org/show_bug.cgi?id=195751
 */
static const struct dmi_system_id i8k_blacklist_fan_support_dmi_table[] __initconst = {
	{
		.ident = "Dell Inspiron 7720",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Inspiron 7720"),
		},
	},
	{
		.ident = "Dell Vostro 3360",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Vostro 3360"),
		},
	},
	{
		.ident = "Dell XPS13 9333",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "XPS13 9333"),
		},
	},
	{
		.ident = "Dell XPS 15 L502X",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Dell System XPS L502X"),
		},
	},
	{ }
};

struct i8k_fan_control_data {
	unsigned int manual_fan;
	unsigned int auto_fan;
};

enum i8k_fan_controls {
	I8K_FAN_34A3_35A3,
};

static const struct i8k_fan_control_data i8k_fan_control_data[] __initconst = {
	[I8K_FAN_34A3_35A3] = {
		.manual_fan = 0x34a3,
		.auto_fan = 0x35a3,
	},
};

static const struct dmi_system_id i8k_whitelist_fan_control[] __initconst = {
	{
		.ident = "Dell Latitude 5480",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Latitude 5480"),
		},
		.driver_data = (void *)&i8k_fan_control_data[I8K_FAN_34A3_35A3],
	},
	{
		.ident = "Dell Latitude E6440",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Latitude E6440"),
		},
		.driver_data = (void *)&i8k_fan_control_data[I8K_FAN_34A3_35A3],
	},
	{
		.ident = "Dell Latitude E7440",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Latitude E7440"),
		},
		.driver_data = (void *)&i8k_fan_control_data[I8K_FAN_34A3_35A3],
	},
	{
		.ident = "Dell Precision 5530",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Precision 5530"),
		},
		.driver_data = (void *)&i8k_fan_control_data[I8K_FAN_34A3_35A3],
	},
	{
		.ident = "Dell Precision 7510",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Precision 7510"),
		},
		.driver_data = (void *)&i8k_fan_control_data[I8K_FAN_34A3_35A3],
	},
	{
		.ident = "Dell XPS 13 7390",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "XPS 13 7390"),
		},
		.driver_data = (void *)&i8k_fan_control_data[I8K_FAN_34A3_35A3],
	},
	{ }
};

static int __init dell_smm_probe(struct platform_device *pdev)
{
	struct dell_smm_data *data;
	const struct dmi_system_id *id, *fan_control;
	int ret;

	data = devm_kzalloc(&pdev->dev, sizeof(struct dell_smm_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->i8k_mutex);
	platform_set_drvdata(pdev, data);

	if (dmi_check_system(i8k_blacklist_fan_support_dmi_table)) {
		if (!force) {
			dev_notice(&pdev->dev, "Disabling fan support due to BIOS bugs\n");
			data->disallow_fan_support = true;
		} else {
			dev_warn(&pdev->dev, "Enabling fan support despite BIOS bugs\n");
		}
	}

	if (dmi_check_system(i8k_blacklist_fan_type_dmi_table)) {
		if (!force) {
			dev_notice(&pdev->dev, "Disabling fan type call due to BIOS bugs\n");
			data->disallow_fan_type_call = true;
		} else {
			dev_warn(&pdev->dev, "Enabling fan type call despite BIOS bugs\n");
		}
	}

	strscpy(data->bios_version, i8k_get_dmi_data(DMI_BIOS_VERSION),
		sizeof(data->bios_version));
	strscpy(data->bios_machineid, i8k_get_dmi_data(DMI_PRODUCT_SERIAL),
		sizeof(data->bios_machineid));

	/*
	 * Set fan multiplier and maximal fan speed from dmi config
	 * Values specified in module parameters override values from dmi
	 */
	id = dmi_first_match(i8k_dmi_table);
	if (id && id->driver_data) {
		const struct i8k_config_data *conf = id->driver_data;

		if (!fan_mult && conf->fan_mult)
			fan_mult = conf->fan_mult;

		if (!fan_max && conf->fan_max)
			fan_max = conf->fan_max;
	}

	/* All options must not be 0 */
	data->i8k_fan_mult = fan_mult ? : I8K_FAN_MULT;
	data->i8k_fan_max = fan_max ? : I8K_FAN_HIGH;
	data->i8k_pwm_mult = DIV_ROUND_UP(255, data->i8k_fan_max);

	fan_control = dmi_first_match(i8k_whitelist_fan_control);
	if (fan_control && fan_control->driver_data) {
		const struct i8k_fan_control_data *control = fan_control->driver_data;

		data->manual_fan = control->manual_fan;
		data->auto_fan = control->auto_fan;
		dev_info(&pdev->dev, "enabling support for setting automatic/manual fan control\n");
	}

	ret = dell_smm_init_hwmon(&pdev->dev);
	if (ret)
		return ret;

	i8k_init_procfs(&pdev->dev);

	return 0;
}

static struct platform_driver dell_smm_driver = {
	.driver		= {
		.name	= KBUILD_MODNAME,
	},
};

static struct platform_device *dell_smm_device;

/*
 * Probe for the presence of a supported laptop.
 */
static int __init i8k_init(void)
{
	/*
	 * Get DMI information
	 */
	if (!dmi_check_system(i8k_dmi_table)) {
		if (!ignore_dmi && !force)
			return -ENODEV;

		pr_info("not running on a supported Dell system.\n");
		pr_info("vendor=%s, model=%s, version=%s\n",
			i8k_get_dmi_data(DMI_SYS_VENDOR),
			i8k_get_dmi_data(DMI_PRODUCT_NAME),
			i8k_get_dmi_data(DMI_BIOS_VERSION));
	}

	/*
	 * Get SMM Dell signature
	 */
	if (i8k_get_dell_signature(I8K_SMM_GET_DELL_SIG1) &&
	    i8k_get_dell_signature(I8K_SMM_GET_DELL_SIG2)) {
		pr_err("unable to get SMM Dell signature\n");
		if (!force)
			return -ENODEV;
	}

	dell_smm_device = platform_create_bundle(&dell_smm_driver, dell_smm_probe, NULL, 0, NULL,
						 0);

	return PTR_ERR_OR_ZERO(dell_smm_device);
}

static void __exit i8k_exit(void)
{
	platform_device_unregister(dell_smm_device);
	platform_driver_unregister(&dell_smm_driver);
}

module_init(i8k_init);
module_exit(i8k_exit);
