/*
 * via-cputemp.c - Driver for VIA CPU core temperature monitoring
 * Copyright (C) 2009 VIA Technologies, Inc.
 *
 * based on existing coretemp.c, which is
 *
 * Copyright (C) 2007 Rudolf Marek <r.marek@assembler.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/hwmon.h>
#include <linux/hwmon-vid.h>
#include <linux/sysfs.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/cpu.h>
#include <asm/msr.h>
#include <asm/processor.h>

#define DRVNAME	"via_cputemp"

enum { SHOW_TEMP, SHOW_LABEL, SHOW_NAME };

/*
 * Functions declaration
 */

struct via_cputemp_data {
	struct device *hwmon_dev;
	const char *name;
	u8 vrm;
	u32 id;
	u32 msr_temp;
	u32 msr_vid;
};

/*
 * Sysfs stuff
 */

static ssize_t show_name(struct device *dev, struct device_attribute
			  *devattr, char *buf)
{
	int ret;
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct via_cputemp_data *data = dev_get_drvdata(dev);

	if (attr->index == SHOW_NAME)
		ret = sprintf(buf, "%s\n", data->name);
	else	/* show label */
		ret = sprintf(buf, "Core %d\n", data->id);
	return ret;
}

static ssize_t show_temp(struct device *dev,
			 struct device_attribute *devattr, char *buf)
{
	struct via_cputemp_data *data = dev_get_drvdata(dev);
	u32 eax, edx;
	int err;

	err = rdmsr_safe_on_cpu(data->id, data->msr_temp, &eax, &edx);
	if (err)
		return -EAGAIN;

	return sprintf(buf, "%lu\n", ((unsigned long)eax & 0xffffff) * 1000);
}

static ssize_t show_cpu_vid(struct device *dev,
			    struct device_attribute *devattr, char *buf)
{
	struct via_cputemp_data *data = dev_get_drvdata(dev);
	u32 eax, edx;
	int err;

	err = rdmsr_safe_on_cpu(data->id, data->msr_vid, &eax, &edx);
	if (err)
		return -EAGAIN;

	return sprintf(buf, "%d\n", vid_from_reg(~edx & 0x7f, data->vrm));
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL,
			  SHOW_TEMP);
static SENSOR_DEVICE_ATTR(temp1_label, S_IRUGO, show_name, NULL, SHOW_LABEL);
static SENSOR_DEVICE_ATTR(name, S_IRUGO, show_name, NULL, SHOW_NAME);

static struct attribute *via_cputemp_attributes[] = {
	&sensor_dev_attr_name.dev_attr.attr,
	&sensor_dev_attr_temp1_label.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	NULL
};

static const struct attribute_group via_cputemp_group = {
	.attrs = via_cputemp_attributes,
};

/* Optional attributes */
static DEVICE_ATTR(cpu0_vid, S_IRUGO, show_cpu_vid, NULL);

static int __devinit via_cputemp_probe(struct platform_device *pdev)
{
	struct via_cputemp_data *data;
	struct cpuinfo_x86 *c = &cpu_data(pdev->id);
	int err;
	u32 eax, edx;

	data = kzalloc(sizeof(struct via_cputemp_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		dev_err(&pdev->dev, "Out of memory\n");
		goto exit;
	}

	data->id = pdev->id;
	data->name = "via_cputemp";

	switch (c->x86_model) {
	case 0xA:
		/* C7 A */
	case 0xD:
		/* C7 D */
		data->msr_temp = 0x1169;
		data->msr_vid = 0x198;
		break;
	case 0xF:
		/* Nano */
		data->msr_temp = 0x1423;
		break;
	default:
		err = -ENODEV;
		goto exit_free;
	}

	/* test if we can access the TEMPERATURE MSR */
	err = rdmsr_safe_on_cpu(data->id, data->msr_temp, &eax, &edx);
	if (err) {
		dev_err(&pdev->dev,
			"Unable to access TEMPERATURE MSR, giving up\n");
		goto exit_free;
	}

	platform_set_drvdata(pdev, data);

	err = sysfs_create_group(&pdev->dev.kobj, &via_cputemp_group);
	if (err)
		goto exit_free;

	if (data->msr_vid)
		data->vrm = vid_which_vrm();

	if (data->vrm) {
		err = device_create_file(&pdev->dev, &dev_attr_cpu0_vid);
		if (err)
			goto exit_remove;
	}

	data->hwmon_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		dev_err(&pdev->dev, "Class registration failed (%d)\n",
			err);
		goto exit_remove;
	}

	return 0;

exit_remove:
	if (data->vrm)
		device_remove_file(&pdev->dev, &dev_attr_cpu0_vid);
	sysfs_remove_group(&pdev->dev.kobj, &via_cputemp_group);
exit_free:
	platform_set_drvdata(pdev, NULL);
	kfree(data);
exit:
	return err;
}

static int __devexit via_cputemp_remove(struct platform_device *pdev)
{
	struct via_cputemp_data *data = platform_get_drvdata(pdev);

	hwmon_device_unregister(data->hwmon_dev);
	if (data->vrm)
		device_remove_file(&pdev->dev, &dev_attr_cpu0_vid);
	sysfs_remove_group(&pdev->dev.kobj, &via_cputemp_group);
	platform_set_drvdata(pdev, NULL);
	kfree(data);
	return 0;
}

static struct platform_driver via_cputemp_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = DRVNAME,
	},
	.probe = via_cputemp_probe,
	.remove = __devexit_p(via_cputemp_remove),
};

struct pdev_entry {
	struct list_head list;
	struct platform_device *pdev;
	unsigned int cpu;
};

static LIST_HEAD(pdev_list);
static DEFINE_MUTEX(pdev_list_mutex);

static int __cpuinit via_cputemp_device_add(unsigned int cpu)
{
	int err;
	struct platform_device *pdev;
	struct pdev_entry *pdev_entry;

	pdev = platform_device_alloc(DRVNAME, cpu);
	if (!pdev) {
		err = -ENOMEM;
		pr_err("Device allocation failed\n");
		goto exit;
	}

	pdev_entry = kzalloc(sizeof(struct pdev_entry), GFP_KERNEL);
	if (!pdev_entry) {
		err = -ENOMEM;
		goto exit_device_put;
	}

	err = platform_device_add(pdev);
	if (err) {
		pr_err("Device addition failed (%d)\n", err);
		goto exit_device_free;
	}

	pdev_entry->pdev = pdev;
	pdev_entry->cpu = cpu;
	mutex_lock(&pdev_list_mutex);
	list_add_tail(&pdev_entry->list, &pdev_list);
	mutex_unlock(&pdev_list_mutex);

	return 0;

exit_device_free:
	kfree(pdev_entry);
exit_device_put:
	platform_device_put(pdev);
exit:
	return err;
}

static void __cpuinit via_cputemp_device_remove(unsigned int cpu)
{
	struct pdev_entry *p;

	mutex_lock(&pdev_list_mutex);
	list_for_each_entry(p, &pdev_list, list) {
		if (p->cpu == cpu) {
			platform_device_unregister(p->pdev);
			list_del(&p->list);
			mutex_unlock(&pdev_list_mutex);
			kfree(p);
			return;
		}
	}
	mutex_unlock(&pdev_list_mutex);
}

static int __cpuinit via_cputemp_cpu_callback(struct notifier_block *nfb,
				 unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long) hcpu;

	switch (action) {
	case CPU_ONLINE:
	case CPU_DOWN_FAILED:
		via_cputemp_device_add(cpu);
		break;
	case CPU_DOWN_PREPARE:
		via_cputemp_device_remove(cpu);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block via_cputemp_cpu_notifier __refdata = {
	.notifier_call = via_cputemp_cpu_callback,
};

static int __init via_cputemp_init(void)
{
	int i, err;

	if (cpu_data(0).x86_vendor != X86_VENDOR_CENTAUR) {
		printk(KERN_DEBUG DRVNAME ": Not a VIA CPU\n");
		err = -ENODEV;
		goto exit;
	}

	err = platform_driver_register(&via_cputemp_driver);
	if (err)
		goto exit;

	for_each_online_cpu(i) {
		struct cpuinfo_x86 *c = &cpu_data(i);

		if (c->x86 != 6)
			continue;

		if (c->x86_model < 0x0a)
			continue;

		if (c->x86_model > 0x0f) {
			pr_warn("Unknown CPU model 0x%x\n", c->x86_model);
			continue;
		}

		via_cputemp_device_add(i);
	}

#ifndef CONFIG_HOTPLUG_CPU
	if (list_empty(&pdev_list)) {
		err = -ENODEV;
		goto exit_driver_unreg;
	}
#endif

	register_hotcpu_notifier(&via_cputemp_cpu_notifier);
	return 0;

#ifndef CONFIG_HOTPLUG_CPU
exit_driver_unreg:
	platform_driver_unregister(&via_cputemp_driver);
#endif
exit:
	return err;
}

static void __exit via_cputemp_exit(void)
{
	struct pdev_entry *p, *n;

	unregister_hotcpu_notifier(&via_cputemp_cpu_notifier);
	mutex_lock(&pdev_list_mutex);
	list_for_each_entry_safe(p, n, &pdev_list, list) {
		platform_device_unregister(p->pdev);
		list_del(&p->list);
		kfree(p);
	}
	mutex_unlock(&pdev_list_mutex);
	platform_driver_unregister(&via_cputemp_driver);
}

MODULE_AUTHOR("Harald Welte <HaraldWelte@viatech.com>");
MODULE_DESCRIPTION("VIA CPU temperature monitor");
MODULE_LICENSE("GPL");

module_init(via_cputemp_init)
module_exit(via_cputemp_exit)
