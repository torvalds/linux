/*
    smsc47m1.c - Part of lm_sensors, Linux kernel modules
                 for hardware monitoring

    Supports the SMSC LPC47B27x, LPC47M10x, LPC47M112, LPC47M13x,
    LPC47M14x, LPC47M15x, LPC47M192, LPC47M292 and LPC47M997
    Super-I/O chips.

    Copyright (C) 2002 Mark D. Studebaker <mdsxyz123@yahoo.com>
    Copyright (C) 2004-2007 Jean Delvare <khali@linux-fr.org>
    Ported to Linux 2.6 by Gabriele Gorla <gorlik@yahoo.com>
                        and Jean Delvare

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <asm/io.h>

static struct platform_device *pdev;

#define DRVNAME "smsc47m1"
enum chips { smsc47m1, smsc47m2 };

/* Super-I/0 registers and commands */

#define	REG	0x2e	/* The register to read/write */
#define	VAL	0x2f	/* The value to read/write */

static inline void
superio_outb(int reg, int val)
{
	outb(reg, REG);
	outb(val, VAL);
}

static inline int
superio_inb(int reg)
{
	outb(reg, REG);
	return inb(VAL);
}

/* logical device for fans is 0x0A */
#define superio_select() superio_outb(0x07, 0x0A)

static inline void
superio_enter(void)
{
	outb(0x55, REG);
}

static inline void
superio_exit(void)
{
	outb(0xAA, REG);
}

#define SUPERIO_REG_ACT		0x30
#define SUPERIO_REG_BASE	0x60
#define SUPERIO_REG_DEVID	0x20

/* Logical device registers */

#define SMSC_EXTENT		0x80

/* nr is 0 or 1 in the macros below */
#define SMSC47M1_REG_ALARM		0x04
#define SMSC47M1_REG_TPIN(nr)		(0x34 - (nr))
#define SMSC47M1_REG_PPIN(nr)		(0x36 - (nr))
#define SMSC47M1_REG_FANDIV		0x58

static const u8 SMSC47M1_REG_FAN[3]		= { 0x59, 0x5a, 0x6b };
static const u8 SMSC47M1_REG_FAN_PRELOAD[3]	= { 0x5b, 0x5c, 0x6c };
static const u8 SMSC47M1_REG_PWM[3]		= { 0x56, 0x57, 0x69 };

#define SMSC47M2_REG_ALARM6		0x09
#define SMSC47M2_REG_TPIN1		0x38
#define SMSC47M2_REG_TPIN2		0x37
#define SMSC47M2_REG_TPIN3		0x2d
#define SMSC47M2_REG_PPIN3		0x2c
#define SMSC47M2_REG_FANDIV3		0x6a

#define MIN_FROM_REG(reg,div)		((reg)>=192 ? 0 : \
					 983040/((192-(reg))*(div)))
#define FAN_FROM_REG(reg,div,preload)	((reg)<=(preload) || (reg)==255 ? 0 : \
					 983040/(((reg)-(preload))*(div)))
#define DIV_FROM_REG(reg)		(1 << (reg))
#define PWM_FROM_REG(reg)		(((reg) & 0x7E) << 1)
#define PWM_EN_FROM_REG(reg)		((~(reg)) & 0x01)
#define PWM_TO_REG(reg)			(((reg) >> 1) & 0x7E)

struct smsc47m1_data {
	unsigned short addr;
	const char *name;
	enum chips type;
	struct class_device *class_dev;

	struct mutex update_lock;
	unsigned long last_updated;	/* In jiffies */

	u8 fan[3];		/* Register value */
	u8 fan_preload[3];	/* Register value */
	u8 fan_div[3];		/* Register encoding, shifted right */
	u8 alarms;		/* Register encoding */
	u8 pwm[3];		/* Register value (bit 0 is disable) */
};

struct smsc47m1_sio_data {
	enum chips type;
};


static int smsc47m1_probe(struct platform_device *pdev);
static int __devexit smsc47m1_remove(struct platform_device *pdev);
static struct smsc47m1_data *smsc47m1_update_device(struct device *dev,
		int init);

static inline int smsc47m1_read_value(struct smsc47m1_data *data, u8 reg)
{
	return inb_p(data->addr + reg);
}

static inline void smsc47m1_write_value(struct smsc47m1_data *data, u8 reg,
		u8 value)
{
	outb_p(value, data->addr + reg);
}

static struct platform_driver smsc47m1_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= DRVNAME,
	},
	.probe		= smsc47m1_probe,
	.remove		= __devexit_p(smsc47m1_remove),
};

static ssize_t get_fan(struct device *dev, struct device_attribute
		       *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct smsc47m1_data *data = smsc47m1_update_device(dev, 0);
	int nr = attr->index;
	/* This chip (stupidly) stops monitoring fan speed if PWM is
	   enabled and duty cycle is 0%. This is fine if the monitoring
	   and control concern the same fan, but troublesome if they are
	   not (which could as well happen). */
	int rpm = (data->pwm[nr] & 0x7F) == 0x00 ? 0 :
		  FAN_FROM_REG(data->fan[nr],
			       DIV_FROM_REG(data->fan_div[nr]),
			       data->fan_preload[nr]);
	return sprintf(buf, "%d\n", rpm);
}

static ssize_t get_fan_min(struct device *dev, struct device_attribute
			   *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct smsc47m1_data *data = smsc47m1_update_device(dev, 0);
	int nr = attr->index;
	int rpm = MIN_FROM_REG(data->fan_preload[nr],
			       DIV_FROM_REG(data->fan_div[nr]));
	return sprintf(buf, "%d\n", rpm);
}

static ssize_t get_fan_div(struct device *dev, struct device_attribute
			   *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct smsc47m1_data *data = smsc47m1_update_device(dev, 0);
	return sprintf(buf, "%d\n", DIV_FROM_REG(data->fan_div[attr->index]));
}

static ssize_t get_pwm(struct device *dev, struct device_attribute
		       *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct smsc47m1_data *data = smsc47m1_update_device(dev, 0);
	return sprintf(buf, "%d\n", PWM_FROM_REG(data->pwm[attr->index]));
}

static ssize_t get_pwm_en(struct device *dev, struct device_attribute
			  *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct smsc47m1_data *data = smsc47m1_update_device(dev, 0);
	return sprintf(buf, "%d\n", PWM_EN_FROM_REG(data->pwm[attr->index]));
}

static ssize_t get_alarms(struct device *dev, struct device_attribute
			  *devattr, char *buf)
{
	struct smsc47m1_data *data = smsc47m1_update_device(dev, 0);
	return sprintf(buf, "%d\n", data->alarms);
}

static ssize_t set_fan_min(struct device *dev, struct device_attribute
			   *devattr, const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct smsc47m1_data *data = dev_get_drvdata(dev);
	int nr = attr->index;
	long rpmdiv, val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	rpmdiv = val * DIV_FROM_REG(data->fan_div[nr]);

	if (983040 > 192 * rpmdiv || 2 * rpmdiv > 983040) {
		mutex_unlock(&data->update_lock);
		return -EINVAL;
	}

	data->fan_preload[nr] = 192 - ((983040 + rpmdiv / 2) / rpmdiv);
	smsc47m1_write_value(data, SMSC47M1_REG_FAN_PRELOAD[nr],
			     data->fan_preload[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

/* Note: we save and restore the fan minimum here, because its value is
   determined in part by the fan clock divider.  This follows the principle
   of least surprise; the user doesn't expect the fan minimum to change just
   because the divider changed. */
static ssize_t set_fan_div(struct device *dev, struct device_attribute
			   *devattr, const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct smsc47m1_data *data = dev_get_drvdata(dev);
	int nr = attr->index;
	long new_div = simple_strtol(buf, NULL, 10), tmp;
	u8 old_div = DIV_FROM_REG(data->fan_div[nr]);

	if (new_div == old_div) /* No change */
		return count;

	mutex_lock(&data->update_lock);
	switch (new_div) {
	case 1: data->fan_div[nr] = 0; break;
	case 2: data->fan_div[nr] = 1; break;
	case 4: data->fan_div[nr] = 2; break;
	case 8: data->fan_div[nr] = 3; break;
	default:
		mutex_unlock(&data->update_lock);
		return -EINVAL;
	}

	switch (nr) {
	case 0:
	case 1:
		tmp = smsc47m1_read_value(data, SMSC47M1_REG_FANDIV)
		      & ~(0x03 << (4 + 2 * nr));
		tmp |= data->fan_div[nr] << (4 + 2 * nr);
		smsc47m1_write_value(data, SMSC47M1_REG_FANDIV, tmp);
		break;
	case 2:
		tmp = smsc47m1_read_value(data, SMSC47M2_REG_FANDIV3) & 0xCF;
		tmp |= data->fan_div[2] << 4;
		smsc47m1_write_value(data, SMSC47M2_REG_FANDIV3, tmp);
		break;
	}

	/* Preserve fan min */
	tmp = 192 - (old_div * (192 - data->fan_preload[nr])
		     + new_div / 2) / new_div;
	data->fan_preload[nr] = SENSORS_LIMIT(tmp, 0, 191);
	smsc47m1_write_value(data, SMSC47M1_REG_FAN_PRELOAD[nr],
			     data->fan_preload[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t set_pwm(struct device *dev, struct device_attribute
		       *devattr, const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct smsc47m1_data *data = dev_get_drvdata(dev);
	int nr = attr->index;
	long val = simple_strtol(buf, NULL, 10);

	if (val < 0 || val > 255)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->pwm[nr] &= 0x81; /* Preserve additional bits */
	data->pwm[nr] |= PWM_TO_REG(val);
	smsc47m1_write_value(data, SMSC47M1_REG_PWM[nr],
			     data->pwm[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t set_pwm_en(struct device *dev, struct device_attribute
			  *devattr, const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct smsc47m1_data *data = dev_get_drvdata(dev);
	int nr = attr->index;
	long val = simple_strtol(buf, NULL, 10);
	
	if (val != 0 && val != 1)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->pwm[nr] &= 0xFE; /* preserve the other bits */
	data->pwm[nr] |= !val;
	smsc47m1_write_value(data, SMSC47M1_REG_PWM[nr],
			     data->pwm[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

#define fan_present(offset)						\
static SENSOR_DEVICE_ATTR(fan##offset##_input, S_IRUGO, get_fan,	\
		NULL, offset - 1);					\
static SENSOR_DEVICE_ATTR(fan##offset##_min, S_IRUGO | S_IWUSR,		\
		get_fan_min, set_fan_min, offset - 1);			\
static SENSOR_DEVICE_ATTR(fan##offset##_div, S_IRUGO | S_IWUSR,		\
		get_fan_div, set_fan_div, offset - 1);			\
static SENSOR_DEVICE_ATTR(pwm##offset, S_IRUGO | S_IWUSR,		\
		get_pwm, set_pwm, offset - 1);				\
static SENSOR_DEVICE_ATTR(pwm##offset##_enable, S_IRUGO | S_IWUSR,	\
		get_pwm_en, set_pwm_en, offset - 1)

fan_present(1);
fan_present(2);
fan_present(3);

static DEVICE_ATTR(alarms, S_IRUGO, get_alarms, NULL);

static ssize_t show_name(struct device *dev, struct device_attribute
			 *devattr, char *buf)
{
	struct smsc47m1_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", data->name);
}
static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

/* Almost all sysfs files may or may not be created depending on the chip
   setup so we create them individually. It is still convenient to define a
   group to remove them all at once. */
static struct attribute *smsc47m1_attributes[] = {
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan1_div.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan2_min.dev_attr.attr,
	&sensor_dev_attr_fan2_div.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	&sensor_dev_attr_fan3_min.dev_attr.attr,
	&sensor_dev_attr_fan3_div.dev_attr.attr,

	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm1_enable.dev_attr.attr,
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&sensor_dev_attr_pwm2_enable.dev_attr.attr,
	&sensor_dev_attr_pwm3.dev_attr.attr,
	&sensor_dev_attr_pwm3_enable.dev_attr.attr,

	&dev_attr_alarms.attr,
	&dev_attr_name.attr,
	NULL
};

static const struct attribute_group smsc47m1_group = {
	.attrs = smsc47m1_attributes,
};

static int __init smsc47m1_find(unsigned short *addr,
				struct smsc47m1_sio_data *sio_data)
{
	u8 val;

	superio_enter();
	val = superio_inb(SUPERIO_REG_DEVID);

	/*
	 * SMSC LPC47M10x/LPC47M112/LPC47M13x (device id 0x59), LPC47M14x
	 * (device id 0x5F) and LPC47B27x (device id 0x51) have fan control.
	 * The LPC47M15x and LPC47M192 chips "with hardware monitoring block"
	 * can do much more besides (device id 0x60).
	 * The LPC47M997 is undocumented, but seems to be compatible with
	 * the LPC47M192, and has the same device id.
	 * The LPC47M292 (device id 0x6B) is somewhat compatible, but it
	 * supports a 3rd fan, and the pin configuration registers are
	 * unfortunately different.
	 */
	switch (val) {
	case 0x51:
		pr_info(DRVNAME ": Found SMSC LPC47B27x\n");
		sio_data->type = smsc47m1;
		break;
	case 0x59:
		pr_info(DRVNAME ": Found SMSC LPC47M10x/LPC47M112/LPC47M13x\n");
		sio_data->type = smsc47m1;
		break;
	case 0x5F:
		pr_info(DRVNAME ": Found SMSC LPC47M14x\n");
		sio_data->type = smsc47m1;
		break;
	case 0x60:
		pr_info(DRVNAME ": Found SMSC LPC47M15x/LPC47M192/LPC47M997\n");
		sio_data->type = smsc47m1;
		break;
	case 0x6B:
		pr_info(DRVNAME ": Found SMSC LPC47M292\n");
		sio_data->type = smsc47m2;
		break;
	default:
		superio_exit();
		return -ENODEV;
	}

	superio_select();
	*addr = (superio_inb(SUPERIO_REG_BASE) << 8)
	      |  superio_inb(SUPERIO_REG_BASE + 1);
	val = superio_inb(SUPERIO_REG_ACT);
	if (*addr == 0 || (val & 0x01) == 0) {
		pr_info(DRVNAME ": Device is disabled, will not use\n");
		superio_exit();
		return -ENODEV;
	}

	superio_exit();
	return 0;
}

static int __devinit smsc47m1_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct smsc47m1_sio_data *sio_data = dev->platform_data;
	struct smsc47m1_data *data;
	struct resource *res;
	int err = 0;
	int fan1, fan2, fan3, pwm1, pwm2, pwm3;

	static const char *names[] = {
		"smsc47m1",
		"smsc47m2",
	};

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!request_region(res->start, SMSC_EXTENT, DRVNAME)) {
		dev_err(dev, "Region 0x%lx-0x%lx already in use!\n",
			(unsigned long)res->start,
			(unsigned long)res->end);
		return -EBUSY;
	}

	if (!(data = kzalloc(sizeof(struct smsc47m1_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto error_release;
	}

	data->addr = res->start;
	data->type = sio_data->type;
	data->name = names[sio_data->type];
	mutex_init(&data->update_lock);
	platform_set_drvdata(pdev, data);

	/* If no function is properly configured, there's no point in
	   actually registering the chip. */
	pwm1 = (smsc47m1_read_value(data, SMSC47M1_REG_PPIN(0)) & 0x05)
	       == 0x04;
	pwm2 = (smsc47m1_read_value(data, SMSC47M1_REG_PPIN(1)) & 0x05)
	       == 0x04;
	if (data->type == smsc47m2) {
		fan1 = (smsc47m1_read_value(data, SMSC47M2_REG_TPIN1)
			& 0x0d) == 0x09;
		fan2 = (smsc47m1_read_value(data, SMSC47M2_REG_TPIN2)
			& 0x0d) == 0x09;
		fan3 = (smsc47m1_read_value(data, SMSC47M2_REG_TPIN3)
			& 0x0d) == 0x0d;
		pwm3 = (smsc47m1_read_value(data, SMSC47M2_REG_PPIN3)
			& 0x0d) == 0x08;
	} else {
		fan1 = (smsc47m1_read_value(data, SMSC47M1_REG_TPIN(0))
			& 0x05) == 0x05;
		fan2 = (smsc47m1_read_value(data, SMSC47M1_REG_TPIN(1))
			& 0x05) == 0x05;
		fan3 = 0;
		pwm3 = 0;
	}
	if (!(fan1 || fan2 || fan3 || pwm1 || pwm2 || pwm3)) {
		dev_warn(dev, "Device not configured, will not use\n");
		err = -ENODEV;
		goto error_free;
	}

	/* Some values (fan min, clock dividers, pwm registers) may be
	   needed before any update is triggered, so we better read them
	   at least once here. We don't usually do it that way, but in
	   this particular case, manually reading 5 registers out of 8
	   doesn't make much sense and we're better using the existing
	   function. */
	smsc47m1_update_device(dev, 1);

	/* Register sysfs hooks */
	if (fan1) {
		if ((err = device_create_file(dev,
				&sensor_dev_attr_fan1_input.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_fan1_min.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_fan1_div.dev_attr)))
			goto error_remove_files;
	} else
		dev_dbg(dev, "Fan 1 not enabled by hardware, skipping\n");

	if (fan2) {
		if ((err = device_create_file(dev,
				&sensor_dev_attr_fan2_input.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_fan2_min.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_fan2_div.dev_attr)))
			goto error_remove_files;
	} else
		dev_dbg(dev, "Fan 2 not enabled by hardware, skipping\n");

	if (fan3) {
		if ((err = device_create_file(dev,
				&sensor_dev_attr_fan3_input.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_fan3_min.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_fan3_div.dev_attr)))
			goto error_remove_files;
	} else
		dev_dbg(dev, "Fan 3 not enabled by hardware, skipping\n");

	if (pwm1) {
		if ((err = device_create_file(dev,
				&sensor_dev_attr_pwm1.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_pwm1_enable.dev_attr)))
			goto error_remove_files;
	} else
		dev_dbg(dev, "PWM 1 not enabled by hardware, skipping\n");

	if (pwm2) {
		if ((err = device_create_file(dev,
				&sensor_dev_attr_pwm2.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_pwm2_enable.dev_attr)))
			goto error_remove_files;
	} else
		dev_dbg(dev, "PWM 2 not enabled by hardware, skipping\n");

	if (pwm3) {
		if ((err = device_create_file(dev,
				&sensor_dev_attr_pwm3.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_pwm3_enable.dev_attr)))
			goto error_remove_files;
	} else
		dev_dbg(dev, "PWM 3 not enabled by hardware, skipping\n");

	if ((err = device_create_file(dev, &dev_attr_alarms)))
		goto error_remove_files;

	data->class_dev = hwmon_device_register(dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto error_remove_files;
	}

	return 0;

error_remove_files:
	sysfs_remove_group(&dev->kobj, &smsc47m1_group);
error_free:
	platform_set_drvdata(pdev, NULL);
	kfree(data);
error_release:
	release_region(res->start, SMSC_EXTENT);
	return err;
}

static int __devexit smsc47m1_remove(struct platform_device *pdev)
{
	struct smsc47m1_data *data = platform_get_drvdata(pdev);
	struct resource *res;

	hwmon_device_unregister(data->class_dev);
	sysfs_remove_group(&pdev->dev.kobj, &smsc47m1_group);

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	release_region(res->start, SMSC_EXTENT);
	platform_set_drvdata(pdev, NULL);
	kfree(data);

	return 0;
}

static struct smsc47m1_data *smsc47m1_update_device(struct device *dev,
		int init)
{
	struct smsc47m1_data *data = dev_get_drvdata(dev);

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2) || init) {
		int i, fan_nr;
		fan_nr = data->type == smsc47m2 ? 3 : 2;

		for (i = 0; i < fan_nr; i++) {
			data->fan[i] = smsc47m1_read_value(data,
				       SMSC47M1_REG_FAN[i]);
			data->fan_preload[i] = smsc47m1_read_value(data,
					       SMSC47M1_REG_FAN_PRELOAD[i]);
			data->pwm[i] = smsc47m1_read_value(data,
				       SMSC47M1_REG_PWM[i]);
		}

		i = smsc47m1_read_value(data, SMSC47M1_REG_FANDIV);
		data->fan_div[0] = (i >> 4) & 0x03;
		data->fan_div[1] = i >> 6;

		data->alarms = smsc47m1_read_value(data,
			       SMSC47M1_REG_ALARM) >> 6;
		/* Clear alarms if needed */
		if (data->alarms)
			smsc47m1_write_value(data, SMSC47M1_REG_ALARM, 0xC0);

		if (fan_nr >= 3) {
			data->fan_div[2] = (smsc47m1_read_value(data,
					    SMSC47M2_REG_FANDIV3) >> 4) & 0x03;
			data->alarms |= (smsc47m1_read_value(data,
					 SMSC47M2_REG_ALARM6) & 0x40) >> 4;
			/* Clear alarm if needed */
			if (data->alarms & 0x04)
				smsc47m1_write_value(data,
						     SMSC47M2_REG_ALARM6,
						     0x40);
		}

		data->last_updated = jiffies;
	}

	mutex_unlock(&data->update_lock);
	return data;
}

static int __init smsc47m1_device_add(unsigned short address,
				      const struct smsc47m1_sio_data *sio_data)
{
	struct resource res = {
		.start	= address,
		.end	= address + SMSC_EXTENT - 1,
		.name	= DRVNAME,
		.flags	= IORESOURCE_IO,
	};
	int err;

	pdev = platform_device_alloc(DRVNAME, address);
	if (!pdev) {
		err = -ENOMEM;
		printk(KERN_ERR DRVNAME ": Device allocation failed\n");
		goto exit;
	}

	err = platform_device_add_resources(pdev, &res, 1);
	if (err) {
		printk(KERN_ERR DRVNAME ": Device resource addition failed "
		       "(%d)\n", err);
		goto exit_device_put;
	}

	err = platform_device_add_data(pdev, sio_data,
				       sizeof(struct smsc47m1_sio_data));
	if (err) {
		printk(KERN_ERR DRVNAME ": Platform data allocation failed\n");
		goto exit_device_put;
	}

	err = platform_device_add(pdev);
	if (err) {
		printk(KERN_ERR DRVNAME ": Device addition failed (%d)\n",
		       err);
		goto exit_device_put;
	}

	return 0;

exit_device_put:
	platform_device_put(pdev);
exit:
	return err;
}

static int __init sm_smsc47m1_init(void)
{
	int err;
	unsigned short address;
	struct smsc47m1_sio_data sio_data;

	if (smsc47m1_find(&address, &sio_data))
		return -ENODEV;

	err = platform_driver_register(&smsc47m1_driver);
	if (err)
		goto exit;

	/* Sets global pdev as a side effect */
	err = smsc47m1_device_add(address, &sio_data);
	if (err)
		goto exit_driver;

	return 0;

exit_driver:
	platform_driver_unregister(&smsc47m1_driver);
exit:
	return err;
}

static void __exit sm_smsc47m1_exit(void)
{
	platform_device_unregister(pdev);
	platform_driver_unregister(&smsc47m1_driver);
}

MODULE_AUTHOR("Mark D. Studebaker <mdsxyz123@yahoo.com>");
MODULE_DESCRIPTION("SMSC LPC47M1xx fan sensors driver");
MODULE_LICENSE("GPL");

module_init(sm_smsc47m1_init);
module_exit(sm_smsc47m1_exit);
