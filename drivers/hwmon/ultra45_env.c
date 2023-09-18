// SPDX-License-Identifier: GPL-2.0-only
/*
 * ultra45_env.c: Driver for Ultra45 PIC16F747 environmental monitor.
 *
 * Copyright (C) 2008 David S. Miller <davem@davemloft.net>
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>

#define DRV_MODULE_VERSION	"0.1"

MODULE_AUTHOR("David S. Miller (davem@davemloft.net)");
MODULE_DESCRIPTION("Ultra45 environmental monitor driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

/* PIC device registers */
#define REG_CMD		0x00UL
#define  REG_CMD_RESET	0x80
#define  REG_CMD_ESTAR	0x01
#define REG_STAT	0x01UL
#define  REG_STAT_FWVER	0xf0
#define  REG_STAT_TGOOD	0x08
#define  REG_STAT_STALE	0x04
#define  REG_STAT_BUSY	0x02
#define  REG_STAT_FAULT	0x01
#define REG_DATA	0x40UL
#define REG_ADDR	0x41UL
#define REG_SIZE	0x42UL

/* Registers accessed indirectly via REG_DATA/REG_ADDR */
#define IREG_FAN0		0x00
#define IREG_FAN1		0x01
#define IREG_FAN2		0x02
#define IREG_FAN3		0x03
#define IREG_FAN4		0x04
#define IREG_FAN5		0x05
#define IREG_LCL_TEMP		0x06
#define IREG_RMT1_TEMP		0x07
#define IREG_RMT2_TEMP		0x08
#define IREG_RMT3_TEMP		0x09
#define IREG_LM95221_TEMP	0x0a
#define IREG_FIRE_TEMP		0x0b
#define IREG_LSI1064_TEMP	0x0c
#define IREG_FRONT_TEMP		0x0d
#define IREG_FAN_STAT		0x0e
#define IREG_VCORE0		0x0f
#define IREG_VCORE1		0x10
#define IREG_VMEM0		0x11
#define IREG_VMEM1		0x12
#define IREG_PSU_TEMP		0x13

struct env {
	void __iomem	*regs;
	spinlock_t	lock;

	struct device	*hwmon_dev;
};

static u8 env_read(struct env *p, u8 ireg)
{
	u8 ret;

	spin_lock(&p->lock);
	writeb(ireg, p->regs + REG_ADDR);
	ret = readb(p->regs + REG_DATA);
	spin_unlock(&p->lock);

	return ret;
}

static void env_write(struct env *p, u8 ireg, u8 val)
{
	spin_lock(&p->lock);
	writeb(ireg, p->regs + REG_ADDR);
	writeb(val, p->regs + REG_DATA);
	spin_unlock(&p->lock);
}

/*
 * There seems to be a adr7462 providing these values, thus a lot
 * of these calculations are borrowed from the adt7470 driver.
 */
#define FAN_PERIOD_TO_RPM(x)	((90000 * 60) / (x))
#define FAN_RPM_TO_PERIOD	FAN_PERIOD_TO_RPM
#define FAN_PERIOD_INVALID	(0xff << 8)
#define FAN_DATA_VALID(x)	((x) && (x) != FAN_PERIOD_INVALID)

static ssize_t show_fan_speed(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	int fan_nr = to_sensor_dev_attr(attr)->index;
	struct env *p = dev_get_drvdata(dev);
	int rpm, period;
	u8 val;

	val = env_read(p, IREG_FAN0 + fan_nr);
	period = (int) val << 8;
	if (FAN_DATA_VALID(period))
		rpm = FAN_PERIOD_TO_RPM(period);
	else
		rpm = 0;

	return sprintf(buf, "%d\n", rpm);
}

static ssize_t set_fan_speed(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	int fan_nr = to_sensor_dev_attr(attr)->index;
	unsigned long rpm;
	struct env *p = dev_get_drvdata(dev);
	int period;
	u8 val;
	int err;

	err = kstrtoul(buf, 10, &rpm);
	if (err)
		return err;

	if (!rpm)
		return -EINVAL;

	period = FAN_RPM_TO_PERIOD(rpm);
	val = period >> 8;
	env_write(p, IREG_FAN0 + fan_nr, val);

	return count;
}

static ssize_t show_fan_fault(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	int fan_nr = to_sensor_dev_attr(attr)->index;
	struct env *p = dev_get_drvdata(dev);
	u8 val = env_read(p, IREG_FAN_STAT);
	return sprintf(buf, "%d\n", (val & (1 << fan_nr)) ? 1 : 0);
}

#define fan(index)							\
static SENSOR_DEVICE_ATTR(fan##index##_speed, S_IRUGO | S_IWUSR,	\
		show_fan_speed, set_fan_speed, index);			\
static SENSOR_DEVICE_ATTR(fan##index##_fault, S_IRUGO,			\
		show_fan_fault, NULL, index)

fan(0);
fan(1);
fan(2);
fan(3);
fan(4);

static SENSOR_DEVICE_ATTR(psu_fan_fault, S_IRUGO, show_fan_fault, NULL, 6);

static ssize_t show_temp(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	int temp_nr = to_sensor_dev_attr(attr)->index;
	struct env *p = dev_get_drvdata(dev);
	s8 val;

	val = env_read(p, IREG_LCL_TEMP + temp_nr);
	return sprintf(buf, "%d\n", ((int) val) - 64);
}

static SENSOR_DEVICE_ATTR(adt7462_local_temp, S_IRUGO, show_temp, NULL, 0);
static SENSOR_DEVICE_ATTR(cpu0_temp, S_IRUGO, show_temp, NULL, 1);
static SENSOR_DEVICE_ATTR(cpu1_temp, S_IRUGO, show_temp, NULL, 2);
static SENSOR_DEVICE_ATTR(motherboard_temp, S_IRUGO, show_temp, NULL, 3);
static SENSOR_DEVICE_ATTR(lm95221_local_temp, S_IRUGO, show_temp, NULL, 4);
static SENSOR_DEVICE_ATTR(fire_temp, S_IRUGO, show_temp, NULL, 5);
static SENSOR_DEVICE_ATTR(lsi1064_local_temp, S_IRUGO, show_temp, NULL, 6);
static SENSOR_DEVICE_ATTR(front_panel_temp, S_IRUGO, show_temp, NULL, 7);
static SENSOR_DEVICE_ATTR(psu_temp, S_IRUGO, show_temp, NULL, 13);

static ssize_t show_stat_bit(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	int index = to_sensor_dev_attr(attr)->index;
	struct env *p = dev_get_drvdata(dev);
	u8 val;

	val = readb(p->regs + REG_STAT);
	return sprintf(buf, "%d\n", (val & (1 << index)) ? 1 : 0);
}

static SENSOR_DEVICE_ATTR(fan_failure, S_IRUGO, show_stat_bit, NULL, 0);
static SENSOR_DEVICE_ATTR(env_bus_busy, S_IRUGO, show_stat_bit, NULL, 1);
static SENSOR_DEVICE_ATTR(env_data_stale, S_IRUGO, show_stat_bit, NULL, 2);
static SENSOR_DEVICE_ATTR(tpm_self_test_passed, S_IRUGO, show_stat_bit, NULL,
			  3);

static ssize_t show_fwver(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct env *p = dev_get_drvdata(dev);
	u8 val;

	val = readb(p->regs + REG_STAT);
	return sprintf(buf, "%d\n", val >> 4);
}

static SENSOR_DEVICE_ATTR(firmware_version, S_IRUGO, show_fwver, NULL, 0);

static ssize_t show_name(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	return sprintf(buf, "ultra45\n");
}

static SENSOR_DEVICE_ATTR(name, S_IRUGO, show_name, NULL, 0);

static struct attribute *env_attributes[] = {
	&sensor_dev_attr_fan0_speed.dev_attr.attr,
	&sensor_dev_attr_fan0_fault.dev_attr.attr,
	&sensor_dev_attr_fan1_speed.dev_attr.attr,
	&sensor_dev_attr_fan1_fault.dev_attr.attr,
	&sensor_dev_attr_fan2_speed.dev_attr.attr,
	&sensor_dev_attr_fan2_fault.dev_attr.attr,
	&sensor_dev_attr_fan3_speed.dev_attr.attr,
	&sensor_dev_attr_fan3_fault.dev_attr.attr,
	&sensor_dev_attr_fan4_speed.dev_attr.attr,
	&sensor_dev_attr_fan4_fault.dev_attr.attr,
	&sensor_dev_attr_psu_fan_fault.dev_attr.attr,
	&sensor_dev_attr_adt7462_local_temp.dev_attr.attr,
	&sensor_dev_attr_cpu0_temp.dev_attr.attr,
	&sensor_dev_attr_cpu1_temp.dev_attr.attr,
	&sensor_dev_attr_motherboard_temp.dev_attr.attr,
	&sensor_dev_attr_lm95221_local_temp.dev_attr.attr,
	&sensor_dev_attr_fire_temp.dev_attr.attr,
	&sensor_dev_attr_lsi1064_local_temp.dev_attr.attr,
	&sensor_dev_attr_front_panel_temp.dev_attr.attr,
	&sensor_dev_attr_psu_temp.dev_attr.attr,
	&sensor_dev_attr_fan_failure.dev_attr.attr,
	&sensor_dev_attr_env_bus_busy.dev_attr.attr,
	&sensor_dev_attr_env_data_stale.dev_attr.attr,
	&sensor_dev_attr_tpm_self_test_passed.dev_attr.attr,
	&sensor_dev_attr_firmware_version.dev_attr.attr,
	&sensor_dev_attr_name.dev_attr.attr,
	NULL,
};

static const struct attribute_group env_group = {
	.attrs = env_attributes,
};

static int env_probe(struct platform_device *op)
{
	struct env *p = devm_kzalloc(&op->dev, sizeof(*p), GFP_KERNEL);
	int err = -ENOMEM;

	if (!p)
		goto out;

	spin_lock_init(&p->lock);

	p->regs = of_ioremap(&op->resource[0], 0, REG_SIZE, "pic16f747");
	if (!p->regs)
		goto out;

	err = sysfs_create_group(&op->dev.kobj, &env_group);
	if (err)
		goto out_iounmap;

	p->hwmon_dev = hwmon_device_register(&op->dev);
	if (IS_ERR(p->hwmon_dev)) {
		err = PTR_ERR(p->hwmon_dev);
		goto out_sysfs_remove_group;
	}

	platform_set_drvdata(op, p);
	err = 0;

out:
	return err;

out_sysfs_remove_group:
	sysfs_remove_group(&op->dev.kobj, &env_group);

out_iounmap:
	of_iounmap(&op->resource[0], p->regs, REG_SIZE);

	goto out;
}

static void env_remove(struct platform_device *op)
{
	struct env *p = platform_get_drvdata(op);

	if (p) {
		sysfs_remove_group(&op->dev.kobj, &env_group);
		hwmon_device_unregister(p->hwmon_dev);
		of_iounmap(&op->resource[0], p->regs, REG_SIZE);
	}
}

static const struct of_device_id env_match[] = {
	{
		.name = "env-monitor",
		.compatible = "SUNW,ebus-pic16f747-env",
	},
	{},
};
MODULE_DEVICE_TABLE(of, env_match);

static struct platform_driver env_driver = {
	.driver = {
		.name = "ultra45_env",
		.of_match_table = env_match,
	},
	.probe		= env_probe,
	.remove_new	= env_remove,
};

module_platform_driver(env_driver);
