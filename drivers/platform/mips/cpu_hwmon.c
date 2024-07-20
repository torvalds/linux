// SPDX-License-Identifier: GPL-2.0-only
#include <linux/err.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/jiffies.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>

#include <loongson.h>
#include <boot_param.h>
#include <loongson_hwmon.h>
#include <loongson_regs.h>

static int csr_temp_enable;

/*
 * Loongson-3 series cpu has two sensors inside,
 * each of them from 0 to 255,
 * if more than 127, that is dangerous.
 * here only provide sensor1 data, because it always hot than sensor0
 */
int loongson3_cpu_temp(int cpu)
{
	u32 reg, prid_rev;

	if (csr_temp_enable) {
		reg = (csr_readl(LOONGSON_CSR_CPUTEMP) & 0xff);
		goto out;
	}

	reg = LOONGSON_CHIPTEMP(cpu);
	prid_rev = read_c0_prid() & PRID_REV_MASK;

	switch (prid_rev) {
	case PRID_REV_LOONGSON3A_R1:
		reg = (reg >> 8) & 0xff;
		break;
	case PRID_REV_LOONGSON3B_R1:
	case PRID_REV_LOONGSON3B_R2:
	case PRID_REV_LOONGSON3A_R2_0:
	case PRID_REV_LOONGSON3A_R2_1:
		reg = ((reg >> 8) & 0xff) - 100;
		break;
	case PRID_REV_LOONGSON3A_R3_0:
	case PRID_REV_LOONGSON3A_R3_1:
	default:
		reg = (reg & 0xffff) * 731 / 0x4000 - 273;
		break;
	}

out:
	return (int)reg * 1000;
}

static int nr_packages;
static struct device *cpu_hwmon_dev;

static ssize_t cpu_temp_label(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int id = (to_sensor_dev_attr(attr))->index - 1;

	return sprintf(buf, "CPU %d Temperature\n", id);
}

static ssize_t get_cpu_temp(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int id = (to_sensor_dev_attr(attr))->index - 1;
	int value = loongson3_cpu_temp(id);

	return sprintf(buf, "%d\n", value);
}

static SENSOR_DEVICE_ATTR(temp1_input, 0444, get_cpu_temp, NULL, 1);
static SENSOR_DEVICE_ATTR(temp1_label, 0444, cpu_temp_label, NULL, 1);
static SENSOR_DEVICE_ATTR(temp2_input, 0444, get_cpu_temp, NULL, 2);
static SENSOR_DEVICE_ATTR(temp2_label, 0444, cpu_temp_label, NULL, 2);
static SENSOR_DEVICE_ATTR(temp3_input, 0444, get_cpu_temp, NULL, 3);
static SENSOR_DEVICE_ATTR(temp3_label, 0444, cpu_temp_label, NULL, 3);
static SENSOR_DEVICE_ATTR(temp4_input, 0444, get_cpu_temp, NULL, 4);
static SENSOR_DEVICE_ATTR(temp4_label, 0444, cpu_temp_label, NULL, 4);

static struct attribute *cpu_hwmon_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_label.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_label.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp3_label.dev_attr.attr,
	&sensor_dev_attr_temp4_input.dev_attr.attr,
	&sensor_dev_attr_temp4_label.dev_attr.attr,
	NULL
};

static umode_t cpu_hwmon_is_visible(struct kobject *kobj,
				    struct attribute *attr, int i)
{
	int id = i / 2;

	if (id < nr_packages)
		return attr->mode;
	return 0;
}

static struct attribute_group cpu_hwmon_group = {
	.attrs = cpu_hwmon_attributes,
	.is_visible = cpu_hwmon_is_visible,
};

static const struct attribute_group *cpu_hwmon_groups[] = {
	&cpu_hwmon_group,
	NULL
};

#define CPU_THERMAL_THRESHOLD 90000
static struct delayed_work thermal_work;

static void do_thermal_timer(struct work_struct *work)
{
	int i, value;

	for (i = 0; i < nr_packages; i++) {
		value = loongson3_cpu_temp(i);
		if (value > CPU_THERMAL_THRESHOLD) {
			pr_emerg("Power off due to high temp: %d\n", value);
			orderly_poweroff(true);
		}
	}

	schedule_delayed_work(&thermal_work, msecs_to_jiffies(5000));
}

static int __init loongson_hwmon_init(void)
{
	pr_info("Loongson Hwmon Enter...\n");

	if (cpu_has_csr())
		csr_temp_enable = csr_readl(LOONGSON_CSR_FEATURES) &
				  LOONGSON_CSRF_TEMP;

	if (!csr_temp_enable && !loongson_chiptemp[0])
		return -ENODEV;

	nr_packages = loongson_sysconf.nr_cpus /
		loongson_sysconf.cores_per_package;

	cpu_hwmon_dev = hwmon_device_register_with_groups(NULL, "cpu_hwmon",
							  NULL, cpu_hwmon_groups);
	if (IS_ERR(cpu_hwmon_dev)) {
		pr_err("hwmon_device_register fail!\n");
		return PTR_ERR(cpu_hwmon_dev);
	}

	INIT_DEFERRABLE_WORK(&thermal_work, do_thermal_timer);
	schedule_delayed_work(&thermal_work, msecs_to_jiffies(20000));

	return 0;
}

static void __exit loongson_hwmon_exit(void)
{
	cancel_delayed_work_sync(&thermal_work);
	hwmon_device_unregister(cpu_hwmon_dev);
}

module_init(loongson_hwmon_init);
module_exit(loongson_hwmon_exit);

MODULE_AUTHOR("Yu Xiang <xiangy@lemote.com>");
MODULE_AUTHOR("Huacai Chen <chenhc@lemote.com>");
MODULE_DESCRIPTION("Loongson CPU Hwmon driver");
MODULE_LICENSE("GPL");
