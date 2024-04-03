// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Apple Motion Sensor driver
 *
 * Copyright (C) 2005 Stelian Pop (stelian@popies.net)
 * Copyright (C) 2006 Michael Hanselmann (linux-kernel@hansmi.ch)
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <asm/pmac_pfunc.h>

#include "ams.h"

/* There is only one motion sensor per machine */
struct ams ams_info;

static bool verbose;
module_param(verbose, bool, 0644);
MODULE_PARM_DESC(verbose, "Show free falls and shocks in kernel output");

/* Call with ams_info.lock held! */
void ams_sensors(s8 *x, s8 *y, s8 *z)
{
	u32 orient = ams_info.vflag? ams_info.orient1 : ams_info.orient2;

	if (orient & 0x80)
		/* X and Y swapped */
		ams_info.get_xyz(y, x, z);
	else
		ams_info.get_xyz(x, y, z);

	if (orient & 0x04)
		*z = ~(*z);
	if (orient & 0x02)
		*y = ~(*y);
	if (orient & 0x01)
		*x = ~(*x);
}

static ssize_t ams_show_current(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	s8 x, y, z;

	mutex_lock(&ams_info.lock);
	ams_sensors(&x, &y, &z);
	mutex_unlock(&ams_info.lock);

	return sysfs_emit(buf, "%d %d %d\n", x, y, z);
}

static DEVICE_ATTR(current, S_IRUGO, ams_show_current, NULL);

static void ams_handle_irq(void *data)
{
	enum ams_irq irq = *((enum ams_irq *)data);

	spin_lock(&ams_info.irq_lock);

	ams_info.worker_irqs |= irq;
	schedule_work(&ams_info.worker);

	spin_unlock(&ams_info.irq_lock);
}

static enum ams_irq ams_freefall_irq_data = AMS_IRQ_FREEFALL;
static struct pmf_irq_client ams_freefall_client = {
	.owner = THIS_MODULE,
	.handler = ams_handle_irq,
	.data = &ams_freefall_irq_data,
};

static enum ams_irq ams_shock_irq_data = AMS_IRQ_SHOCK;
static struct pmf_irq_client ams_shock_client = {
	.owner = THIS_MODULE,
	.handler = ams_handle_irq,
	.data = &ams_shock_irq_data,
};

/* Once hard disk parking is implemented in the kernel, this function can
 * trigger it.
 */
static void ams_worker(struct work_struct *work)
{
	unsigned long flags;
	u8 irqs_to_clear;

	mutex_lock(&ams_info.lock);

	spin_lock_irqsave(&ams_info.irq_lock, flags);
	irqs_to_clear = ams_info.worker_irqs;

	if (ams_info.worker_irqs & AMS_IRQ_FREEFALL) {
		if (verbose)
			printk(KERN_INFO "ams: freefall detected!\n");

		ams_info.worker_irqs &= ~AMS_IRQ_FREEFALL;
	}

	if (ams_info.worker_irqs & AMS_IRQ_SHOCK) {
		if (verbose)
			printk(KERN_INFO "ams: shock detected!\n");

		ams_info.worker_irqs &= ~AMS_IRQ_SHOCK;
	}

	spin_unlock_irqrestore(&ams_info.irq_lock, flags);

	ams_info.clear_irq(irqs_to_clear);

	mutex_unlock(&ams_info.lock);
}

/* Call with ams_info.lock held! */
int ams_sensor_attach(void)
{
	int result;
	const u32 *prop;

	/* Get orientation */
	prop = of_get_property(ams_info.of_node, "orientation", NULL);
	if (!prop)
		return -ENODEV;
	ams_info.orient1 = *prop;
	ams_info.orient2 = *(prop + 1);

	/* Register freefall interrupt handler */
	result = pmf_register_irq_client(ams_info.of_node,
			"accel-int-1",
			&ams_freefall_client);
	if (result < 0)
		return -ENODEV;

	/* Reset saved irqs */
	ams_info.worker_irqs = 0;

	/* Register shock interrupt handler */
	result = pmf_register_irq_client(ams_info.of_node,
			"accel-int-2",
			&ams_shock_client);
	if (result < 0)
		goto release_freefall;

	/* Create device */
	ams_info.of_dev = of_platform_device_create(ams_info.of_node, "ams", NULL);
	if (!ams_info.of_dev) {
		result = -ENODEV;
		goto release_shock;
	}

	/* Create attributes */
	result = device_create_file(&ams_info.of_dev->dev, &dev_attr_current);
	if (result)
		goto release_of;

	ams_info.vflag = !!(ams_info.get_vendor() & 0x10);

	/* Init input device */
	result = ams_input_init();
	if (result)
		goto release_device_file;

	return result;
release_device_file:
	device_remove_file(&ams_info.of_dev->dev, &dev_attr_current);
release_of:
	of_device_unregister(ams_info.of_dev);
release_shock:
	pmf_unregister_irq_client(&ams_shock_client);
release_freefall:
	pmf_unregister_irq_client(&ams_freefall_client);
	return result;
}

static int __init ams_init(void)
{
	struct device_node *np;

	spin_lock_init(&ams_info.irq_lock);
	mutex_init(&ams_info.lock);
	INIT_WORK(&ams_info.worker, ams_worker);

#ifdef CONFIG_SENSORS_AMS_I2C
	np = of_find_node_by_name(NULL, "accelerometer");
	if (np && of_device_is_compatible(np, "AAPL,accelerometer_1"))
		/* Found I2C motion sensor */
		return ams_i2c_init(np);
#endif

#ifdef CONFIG_SENSORS_AMS_PMU
	np = of_find_node_by_name(NULL, "sms");
	if (np && of_device_is_compatible(np, "sms"))
		/* Found PMU motion sensor */
		return ams_pmu_init(np);
#endif
	return -ENODEV;
}

void ams_sensor_detach(void)
{
	/* Remove input device */
	ams_input_exit();

	/* Remove attributes */
	device_remove_file(&ams_info.of_dev->dev, &dev_attr_current);

	/* Flush interrupt worker
	 *
	 * We do this after ams_info.exit(), because an interrupt might
	 * have arrived before disabling them.
	 */
	flush_work(&ams_info.worker);

	/* Remove device */
	of_device_unregister(ams_info.of_dev);

	/* Remove handler */
	pmf_unregister_irq_client(&ams_shock_client);
	pmf_unregister_irq_client(&ams_freefall_client);
}

static void __exit ams_exit(void)
{
	/* Shut down implementation */
	ams_info.exit();
}

MODULE_AUTHOR("Stelian Pop, Michael Hanselmann");
MODULE_DESCRIPTION("Apple Motion Sensor driver");
MODULE_LICENSE("GPL");

module_init(ams_init);
module_exit(ams_exit);
