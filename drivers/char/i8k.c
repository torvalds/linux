/*
 * i8k.c -- Linux driver for accessing the SMM BIOS on Dell laptops.
 *	    See http://www.debian.org/~dz/i8k/ for more information
 *	    and for latest version of this driver.
 *
 * Copyright (C) 2001  Massimo Dal Zotto <dz@debian.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/dmi.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include <linux/i8k.h>

#define I8K_VERSION		"1.13 14/05/2002"

#define I8K_SMM_FN_STATUS	0x0025
#define I8K_SMM_POWER_STATUS	0x0069
#define I8K_SMM_SET_FAN		0x01a3
#define I8K_SMM_GET_FAN		0x00a3
#define I8K_SMM_GET_SPEED	0x02a3
#define I8K_SMM_GET_TEMP	0x10a3
#define I8K_SMM_GET_DELL_SIG	0xffa3
#define I8K_SMM_BIOS_VERSION	0x00a6

#define I8K_FAN_MULT		30
#define I8K_MAX_TEMP		127

#define I8K_FN_NONE		0x00
#define I8K_FN_UP		0x01
#define I8K_FN_DOWN		0x02
#define I8K_FN_MUTE		0x04
#define I8K_FN_MASK		0x07
#define I8K_FN_SHIFT		8

#define I8K_POWER_AC		0x05
#define I8K_POWER_BATTERY	0x01

#define I8K_TEMPERATURE_BUG	1

static char bios_version[4];

MODULE_AUTHOR("Massimo Dal Zotto (dz@debian.org)");
MODULE_DESCRIPTION("Driver for accessing SMM BIOS on Dell laptops");
MODULE_LICENSE("GPL");

static int force;
module_param(force, bool, 0);
MODULE_PARM_DESC(force, "Force loading without checking for supported models");

static int ignore_dmi;
module_param(ignore_dmi, bool, 0);
MODULE_PARM_DESC(ignore_dmi, "Continue probing hardware even if DMI data does not match");

static int restricted;
module_param(restricted, bool, 0);
MODULE_PARM_DESC(restricted, "Allow fan control if SYS_ADMIN capability set");

static int power_status;
module_param(power_status, bool, 0600);
MODULE_PARM_DESC(power_status, "Report power status in /proc/i8k");

static ssize_t i8k_read(struct file *, char __user *, size_t, loff_t *);
static int i8k_ioctl(struct inode *, struct file *, unsigned int,
		     unsigned long);

static struct file_operations i8k_fops = {
	.read = i8k_read,
	.ioctl = i8k_ioctl,
};

typedef struct {
	unsigned int eax;
	unsigned int ebx __attribute__ ((packed));
	unsigned int ecx __attribute__ ((packed));
	unsigned int edx __attribute__ ((packed));
	unsigned int esi __attribute__ ((packed));
	unsigned int edi __attribute__ ((packed));
} SMMRegisters;

static inline char *i8k_get_dmi_data(int field)
{
	return dmi_get_system_info(field) ? : "N/A";
}

/*
 * Call the System Management Mode BIOS. Code provided by Jonathan Buzzard.
 */
static int i8k_smm(SMMRegisters * regs)
{
	int rc;
	int eax = regs->eax;

	asm("pushl %%eax\n\t"
	    "movl 0(%%eax),%%edx\n\t"
	    "push %%edx\n\t"
	    "movl 4(%%eax),%%ebx\n\t"
	    "movl 8(%%eax),%%ecx\n\t"
	    "movl 12(%%eax),%%edx\n\t"
	    "movl 16(%%eax),%%esi\n\t"
	    "movl 20(%%eax),%%edi\n\t"
	    "popl %%eax\n\t"
	    "out %%al,$0xb2\n\t"
	    "out %%al,$0x84\n\t"
	    "xchgl %%eax,(%%esp)\n\t"
	    "movl %%ebx,4(%%eax)\n\t"
	    "movl %%ecx,8(%%eax)\n\t"
	    "movl %%edx,12(%%eax)\n\t"
	    "movl %%esi,16(%%eax)\n\t"
	    "movl %%edi,20(%%eax)\n\t"
	    "popl %%edx\n\t"
	    "movl %%edx,0(%%eax)\n\t"
	    "lahf\n\t"
	    "shrl $8,%%eax\n\t"
	    "andl $1,%%eax\n":"=a"(rc)
	    :    "a"(regs)
	    :    "%ebx", "%ecx", "%edx", "%esi", "%edi", "memory");

	if ((rc != 0) || ((regs->eax & 0xffff) == 0xffff) || (regs->eax == eax)) {
		return -EINVAL;
	}

	return 0;
}

/*
 * Read the bios version. Return the version as an integer corresponding
 * to the ascii value, for example "A17" is returned as 0x00413137.
 */
static int i8k_get_bios_version(void)
{
	SMMRegisters regs = { 0, 0, 0, 0, 0, 0 };
	int rc;

	regs.eax = I8K_SMM_BIOS_VERSION;
	if ((rc = i8k_smm(&regs)) < 0) {
		return rc;
	}

	return regs.eax;
}

/*
 * Read the Fn key status.
 */
static int i8k_get_fn_status(void)
{
	SMMRegisters regs = { 0, 0, 0, 0, 0, 0 };
	int rc;

	regs.eax = I8K_SMM_FN_STATUS;
	if ((rc = i8k_smm(&regs)) < 0) {
		return rc;
	}

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
	SMMRegisters regs = { 0, 0, 0, 0, 0, 0 };
	int rc;

	regs.eax = I8K_SMM_POWER_STATUS;
	if ((rc = i8k_smm(&regs)) < 0) {
		return rc;
	}

	switch (regs.eax & 0xff) {
	case I8K_POWER_AC:
		return I8K_AC;
	default:
		return I8K_BATTERY;
	}
}

/*
 * Read the fan status.
 */
static int i8k_get_fan_status(int fan)
{
	SMMRegisters regs = { 0, 0, 0, 0, 0, 0 };
	int rc;

	regs.eax = I8K_SMM_GET_FAN;
	regs.ebx = fan & 0xff;
	if ((rc = i8k_smm(&regs)) < 0) {
		return rc;
	}

	return (regs.eax & 0xff);
}

/*
 * Read the fan speed in RPM.
 */
static int i8k_get_fan_speed(int fan)
{
	SMMRegisters regs = { 0, 0, 0, 0, 0, 0 };
	int rc;

	regs.eax = I8K_SMM_GET_SPEED;
	regs.ebx = fan & 0xff;
	if ((rc = i8k_smm(&regs)) < 0) {
		return rc;
	}

	return (regs.eax & 0xffff) * I8K_FAN_MULT;
}

/*
 * Set the fan speed (off, low, high). Returns the new fan status.
 */
static int i8k_set_fan(int fan, int speed)
{
	SMMRegisters regs = { 0, 0, 0, 0, 0, 0 };
	int rc;

	speed = (speed < 0) ? 0 : ((speed > I8K_FAN_MAX) ? I8K_FAN_MAX : speed);

	regs.eax = I8K_SMM_SET_FAN;
	regs.ebx = (fan & 0xff) | (speed << 8);
	if ((rc = i8k_smm(&regs)) < 0) {
		return rc;
	}

	return (i8k_get_fan_status(fan));
}

/*
 * Read the cpu temperature.
 */
static int i8k_get_cpu_temp(void)
{
	SMMRegisters regs = { 0, 0, 0, 0, 0, 0 };
	int rc;
	int temp;

#ifdef I8K_TEMPERATURE_BUG
	static int prev = 0;
#endif

	regs.eax = I8K_SMM_GET_TEMP;
	if ((rc = i8k_smm(&regs)) < 0) {
		return rc;
	}
	temp = regs.eax & 0xff;

#ifdef I8K_TEMPERATURE_BUG
	/*
	 * Sometimes the temperature sensor returns 0x99, which is out of range.
	 * In this case we return (once) the previous cached value. For example:
	 # 1003655137 00000058 00005a4b
	 # 1003655138 00000099 00003a80 <--- 0x99 = 153 degrees
	 # 1003655139 00000054 00005c52
	 */
	if (temp > I8K_MAX_TEMP) {
		temp = prev;
		prev = I8K_MAX_TEMP;
	} else {
		prev = temp;
	}
#endif

	return temp;
}

static int i8k_get_dell_signature(void)
{
	SMMRegisters regs = { 0, 0, 0, 0, 0, 0 };
	int rc;

	regs.eax = I8K_SMM_GET_DELL_SIG;
	if ((rc = i8k_smm(&regs)) < 0) {
		return rc;
	}

	if ((regs.eax == 1145651527) && (regs.edx == 1145392204)) {
		return 0;
	} else {
		return -1;
	}
}

static int i8k_ioctl(struct inode *ip, struct file *fp, unsigned int cmd,
		     unsigned long arg)
{
	int val = 0;
	int speed;
	unsigned char buff[16];
	int __user *argp = (int __user *)arg;

	if (!argp)
		return -EINVAL;

	switch (cmd) {
	case I8K_BIOS_VERSION:
		val = i8k_get_bios_version();
		break;

	case I8K_MACHINE_ID:
		memset(buff, 0, 16);
		strlcpy(buff, i8k_get_dmi_data(DMI_PRODUCT_SERIAL), sizeof(buff));
		break;

	case I8K_FN_STATUS:
		val = i8k_get_fn_status();
		break;

	case I8K_POWER_STATUS:
		val = i8k_get_power_status();
		break;

	case I8K_GET_TEMP:
		val = i8k_get_cpu_temp();
		break;

	case I8K_GET_SPEED:
		if (copy_from_user(&val, argp, sizeof(int))) {
			return -EFAULT;
		}
		val = i8k_get_fan_speed(val);
		break;

	case I8K_GET_FAN:
		if (copy_from_user(&val, argp, sizeof(int))) {
			return -EFAULT;
		}
		val = i8k_get_fan_status(val);
		break;

	case I8K_SET_FAN:
		if (restricted && !capable(CAP_SYS_ADMIN)) {
			return -EPERM;
		}
		if (copy_from_user(&val, argp, sizeof(int))) {
			return -EFAULT;
		}
		if (copy_from_user(&speed, argp + 1, sizeof(int))) {
			return -EFAULT;
		}
		val = i8k_set_fan(val, speed);
		break;

	default:
		return -EINVAL;
	}

	if (val < 0) {
		return val;
	}

	switch (cmd) {
	case I8K_BIOS_VERSION:
		if (copy_to_user(argp, &val, 4)) {
			return -EFAULT;
		}
		break;
	case I8K_MACHINE_ID:
		if (copy_to_user(argp, buff, 16)) {
			return -EFAULT;
		}
		break;
	default:
		if (copy_to_user(argp, &val, sizeof(int))) {
			return -EFAULT;
		}
		break;
	}

	return 0;
}

/*
 * Print the information for /proc/i8k.
 */
static int i8k_get_info(char *buffer, char **start, off_t fpos, int length)
{
	int n, fn_key, cpu_temp, ac_power;
	int left_fan, right_fan, left_speed, right_speed;

	cpu_temp	= i8k_get_cpu_temp();			/* 11100 탎 */
	left_fan	= i8k_get_fan_status(I8K_FAN_LEFT);	/*   580 탎 */
	right_fan	= i8k_get_fan_status(I8K_FAN_RIGHT);	/*   580 탎 */
	left_speed	= i8k_get_fan_speed(I8K_FAN_LEFT);	/*   580 탎 */
	right_speed	= i8k_get_fan_speed(I8K_FAN_RIGHT);	/*   580 탎 */
	fn_key		= i8k_get_fn_status();			/*   750 탎 */
	if (power_status) {
		ac_power = i8k_get_power_status();		/* 14700 탎 */
	} else {
		ac_power = -1;
	}

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
	n = sprintf(buffer, "%s %s %s %d %d %d %d %d %d %d\n",
		    I8K_PROC_FMT,
		    bios_version,
		    dmi_get_system_info(DMI_PRODUCT_SERIAL) ? : "N/A",
		    cpu_temp,
		    left_fan, right_fan, left_speed, right_speed,
		    ac_power, fn_key);

	return n;
}

static ssize_t i8k_read(struct file *f, char __user * buffer, size_t len,
			loff_t * fpos)
{
	int n;
	char info[128];

	n = i8k_get_info(info, NULL, 0, 128);
	if (n <= 0) {
		return n;
	}

	if (*fpos >= n) {
		return 0;
	}

	if ((*fpos + len) >= n) {
		len = n - *fpos;
	}

	if (copy_to_user(buffer, info, len) != 0) {
		return -EFAULT;
	}

	*fpos += len;
	return len;
}

static struct dmi_system_id __initdata i8k_dmi_table[] = {
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
	{ }
};

/*
 * Probe for the presence of a supported laptop.
 */
static int __init i8k_probe(void)
{
	char buff[4];
	int version;

	/*
	 * Get DMI information
	 */
	if (!dmi_check_system(i8k_dmi_table)) {
		if (!ignore_dmi && !force)
			return -ENODEV;

		printk(KERN_INFO "i8k: not running on a supported Dell system.\n");
		printk(KERN_INFO "i8k: vendor=%s, model=%s, version=%s\n",
			i8k_get_dmi_data(DMI_SYS_VENDOR),
			i8k_get_dmi_data(DMI_PRODUCT_NAME),
			i8k_get_dmi_data(DMI_BIOS_VERSION));
	}

	strlcpy(bios_version, i8k_get_dmi_data(DMI_BIOS_VERSION), sizeof(bios_version));

	/*
	 * Get SMM Dell signature
	 */
	if (i8k_get_dell_signature() != 0) {
		printk(KERN_ERR "i8k: unable to get SMM Dell signature\n");
		if (!force)
			return -ENODEV;
	}

	/*
	 * Get SMM BIOS version.
	 */
	version = i8k_get_bios_version();
	if (version <= 0) {
		printk(KERN_WARNING "i8k: unable to get SMM BIOS version\n");
	} else {
		buff[0] = (version >> 16) & 0xff;
		buff[1] = (version >> 8) & 0xff;
		buff[2] = (version) & 0xff;
		buff[3] = '\0';
		/*
		 * If DMI BIOS version is unknown use SMM BIOS version.
		 */
		if (!dmi_get_system_info(DMI_BIOS_VERSION))
			strlcpy(bios_version, buff, sizeof(bios_version));

		/*
		 * Check if the two versions match.
		 */
		if (strncmp(buff, bios_version, sizeof(bios_version)) != 0)
			printk(KERN_WARNING "i8k: BIOS version mismatch: %s != %s\n",
				buff, bios_version);
	}

	return 0;
}

#ifdef MODULE
static
#endif
int __init i8k_init(void)
{
	struct proc_dir_entry *proc_i8k;

	/* Are we running on an supported laptop? */
	if (i8k_probe())
		return -ENODEV;

	/* Register the proc entry */
	proc_i8k = create_proc_info_entry("i8k", 0, NULL, i8k_get_info);
	if (!proc_i8k) {
		return -ENOENT;
	}
	proc_i8k->proc_fops = &i8k_fops;
	proc_i8k->owner = THIS_MODULE;

	printk(KERN_INFO
	       "Dell laptop SMM driver v%s Massimo Dal Zotto (dz@debian.org)\n",
	       I8K_VERSION);

	return 0;
}

#ifdef MODULE
int init_module(void)
{
	return i8k_init();
}

void cleanup_module(void)
{
	/* Remove the proc entry */
	remove_proc_entry("i8k", NULL);

	printk(KERN_INFO "i8k: module unloaded\n");
}
#endif

/* end of file */
