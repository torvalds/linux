#include <linux/err.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/jiffies.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>

#include <loongson.h>
#include <boot_param.h>
#include <loongson_hwmon.h>

/*
 * Loongson-3 series cpu has two sensors inside,
 * each of them from 0 to 255,
 * if more than 127, that is dangerous.
 * here only provide sensor1 data, because it always hot than sensor0
 */
int loongson3_cpu_temp(int cpu)
{
	u32 reg;

	reg = LOONGSON_CHIPTEMP(cpu);
	if ((read_c0_prid() & PRID_REV_MASK) == PRID_REV_LOONGSON3A_R1)
		reg = (reg >> 8) & 0xff;
	else
		reg = ((reg >> 8) & 0xff) - 100;

	return (int)reg * 1000;
}

static struct device *cpu_hwmon_dev;

static ssize_t get_hwmon_name(struct device *dev,
			struct device_attribute *attr, char *buf);
static SENSOR_DEVICE_ATTR(name, S_IRUGO, get_hwmon_name, NULL, 0);

static struct attribute *cpu_hwmon_attributes[] = {
	&sensor_dev_attr_name.dev_attr.attr,
	NULL
};

/* Hwmon device attribute group */
static struct attribute_group cpu_hwmon_attribute_group = {
	.attrs = cpu_hwmon_attributes,
};

/* Hwmon device get name */
static ssize_t get_hwmon_name(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "cpu-hwmon\n");
}

static ssize_t get_cpu0_temp(struct device *dev,
			struct device_attribute *attr, char *buf);
static ssize_t get_cpu1_temp(struct device *dev,
			struct device_attribute *attr, char *buf);
static ssize_t cpu0_temp_label(struct device *dev,
			struct device_attribute *attr, char *buf);
static ssize_t cpu1_temp_label(struct device *dev,
			struct device_attribute *attr, char *buf);

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, get_cpu0_temp, NULL, 1);
static SENSOR_DEVICE_ATTR(temp1_label, S_IRUGO, cpu0_temp_label, NULL, 1);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, get_cpu1_temp, NULL, 2);
static SENSOR_DEVICE_ATTR(temp2_label, S_IRUGO, cpu1_temp_label, NULL, 2);

static const struct attribute *hwmon_cputemp1[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_label.dev_attr.attr,
	NULL
};

static const struct attribute *hwmon_cputemp2[] = {
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_label.dev_attr.attr,
	NULL
};

static ssize_t cpu0_temp_label(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "CPU 0 Temperature\n");
}

static ssize_t cpu1_temp_label(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "CPU 1 Temperature\n");
}

static ssize_t get_cpu0_temp(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int value = loongson3_cpu_temp(0);
	return sprintf(buf, "%d\n", value);
}

static ssize_t get_cpu1_temp(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int value = loongson3_cpu_temp(1);
	return sprintf(buf, "%d\n", value);
}

static int create_sysfs_cputemp_files(struct kobject *kobj)
{
	int ret;

	ret = sysfs_create_files(kobj, hwmon_cputemp1);
	if (ret)
		goto sysfs_create_temp1_fail;

	if (loongson_sysconf.nr_cpus <= loongson_sysconf.cores_per_package)
		return 0;

	ret = sysfs_create_files(kobj, hwmon_cputemp2);
	if (ret)
		goto sysfs_create_temp2_fail;

	return 0;

sysfs_create_temp2_fail:
	sysfs_remove_files(kobj, hwmon_cputemp1);

sysfs_create_temp1_fail:
	return -1;
}

static void remove_sysfs_cputemp_files(struct kobject *kobj)
{
	sysfs_remove_files(&cpu_hwmon_dev->kobj, hwmon_cputemp1);

	if (loongson_sysconf.nr_cpus > loongson_sysconf.cores_per_package)
		sysfs_remove_files(&cpu_hwmon_dev->kobj, hwmon_cputemp2);
}

#define CPU_THERMAL_THRESHOLD 90000
static struct delayed_work thermal_work;

static void do_thermal_timer(struct work_struct *work)
{
	int value = loongson3_cpu_temp(0);
	if (value <= CPU_THERMAL_THRESHOLD)
		schedule_delayed_work(&thermal_work, msecs_to_jiffies(5000));
	else
		orderly_poweroff(true);
}

static int __init loongson_hwmon_init(void)
{
	int ret;

	pr_info("Loongson Hwmon Enter...\n");

	cpu_hwmon_dev = hwmon_device_register(NULL);
	if (IS_ERR(cpu_hwmon_dev)) {
		ret = -ENOMEM;
		pr_err("hwmon_device_register fail!\n");
		goto fail_hwmon_device_register;
	}

	ret = sysfs_create_group(&cpu_hwmon_dev->kobj,
				&cpu_hwmon_attribute_group);
	if (ret) {
		pr_err("fail to create loongson hwmon!\n");
		goto fail_sysfs_create_group_hwmon;
	}

	ret = create_sysfs_cputemp_files(&cpu_hwmon_dev->kobj);
	if (ret) {
		pr_err("fail to create cpu temperature interface!\n");
		goto fail_create_sysfs_cputemp_files;
	}

	INIT_DEFERRABLE_WORK(&thermal_work, do_thermal_timer);
	schedule_delayed_work(&thermal_work, msecs_to_jiffies(20000));

	return ret;

fail_create_sysfs_cputemp_files:
	sysfs_remove_group(&cpu_hwmon_dev->kobj,
				&cpu_hwmon_attribute_group);

fail_sysfs_create_group_hwmon:
	hwmon_device_unregister(cpu_hwmon_dev);

fail_hwmon_device_register:
	return ret;
}

static void __exit loongson_hwmon_exit(void)
{
	cancel_delayed_work_sync(&thermal_work);
	remove_sysfs_cputemp_files(&cpu_hwmon_dev->kobj);
	sysfs_remove_group(&cpu_hwmon_dev->kobj,
				&cpu_hwmon_attribute_group);
	hwmon_device_unregister(cpu_hwmon_dev);
}

module_init(loongson_hwmon_init);
module_exit(loongson_hwmon_exit);

MODULE_AUTHOR("Yu Xiang <xiangy@lemote.com>");
MODULE_AUTHOR("Huacai Chen <chenhc@lemote.com>");
MODULE_DESCRIPTION("Loongson CPU Hwmon driver");
MODULE_LICENSE("GPL");
