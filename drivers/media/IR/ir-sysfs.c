/* ir-register.c - handle IR scancode->keycode tables
 *
 * Copyright (C) 2009 by Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <linux/slab.h>
#include <linux/input.h>
#include <linux/device.h>
#include <media/ir-core.h>

#define IRRCV_NUM_DEVICES	256

/* bit array to represent IR sysfs device number */
static unsigned long ir_core_dev_number;

/* class for /sys/class/irrcv */
static char *ir_devnode(struct device *dev, mode_t *mode)
{
	return kasprintf(GFP_KERNEL, "irrcv/%s", dev_name(dev));
}

struct class ir_input_class = {
	.name		= "irrcv",
	.devnode	= ir_devnode,
};

/**
 * show_protocol() - shows the current IR protocol
 * @d:		the device descriptor
 * @mattr:	the device attribute struct (unused)
 * @buf:	a pointer to the output buffer
 *
 * This routine is a callback routine for input read the IR protocol type.
 * it is trigged by reading /sys/class/irrcv/irrcv?/current_protocol.
 * It returns the protocol name, as understood by the driver.
 */
static ssize_t show_protocol(struct device *d,
			     struct device_attribute *mattr, char *buf)
{
	char *s;
	struct ir_input_dev *ir_dev = dev_get_drvdata(d);
	u64 ir_type = ir_dev->rc_tab.ir_type;

	IR_dprintk(1, "Current protocol is %lld\n", (long long)ir_type);

	/* FIXME: doesn't support multiple protocols at the same time */
	if (ir_type == IR_TYPE_UNKNOWN)
		s = "Unknown";
	else if (ir_type == IR_TYPE_RC5)
		s = "RC-5";
	else if (ir_type == IR_TYPE_PD)
		s = "Pulse/distance";
	else if (ir_type == IR_TYPE_NEC)
		s = "NEC";
	else
		s = "Other";

	return sprintf(buf, "%s\n", s);
}

/**
 * store_protocol() - shows the current IR protocol
 * @d:		the device descriptor
 * @mattr:	the device attribute struct (unused)
 * @buf:	a pointer to the input buffer
 * @len:	length of the input buffer
 *
 * This routine is a callback routine for changing the IR protocol type.
 * it is trigged by reading /sys/class/irrcv/irrcv?/current_protocol.
 * It changes the IR the protocol name, if the IR type is recognized
 * by the driver.
 * If an unknown protocol name is used, returns -EINVAL.
 */
static ssize_t store_protocol(struct device *d,
			      struct device_attribute *mattr,
			      const char *data,
			      size_t len)
{
	struct ir_input_dev *ir_dev = dev_get_drvdata(d);
	u64 ir_type = IR_TYPE_UNKNOWN;
	int rc = -EINVAL;
	unsigned long flags;
	char *buf;

	buf = strsep((char **) &data, "\n");

	if (!strcasecmp(buf, "rc-5"))
		ir_type = IR_TYPE_RC5;
	else if (!strcasecmp(buf, "pd"))
		ir_type = IR_TYPE_PD;
	else if (!strcasecmp(buf, "nec"))
		ir_type = IR_TYPE_NEC;

	if (ir_type == IR_TYPE_UNKNOWN) {
		IR_dprintk(1, "Error setting protocol to %lld\n",
			   (long long)ir_type);
		return -EINVAL;
	}

	if (ir_dev->props && ir_dev->props->change_protocol)
		rc = ir_dev->props->change_protocol(ir_dev->props->priv,
						    ir_type);

	if (rc < 0) {
		IR_dprintk(1, "Error setting protocol to %lld\n",
			   (long long)ir_type);
		return -EINVAL;
	}

	spin_lock_irqsave(&ir_dev->rc_tab.lock, flags);
	ir_dev->rc_tab.ir_type = ir_type;
	spin_unlock_irqrestore(&ir_dev->rc_tab.lock, flags);

	IR_dprintk(1, "Current protocol is %lld\n",
		   (long long)ir_type);

	return len;
}


#define ADD_HOTPLUG_VAR(fmt, val...)					\
	do {								\
		int err = add_uevent_var(env, fmt, val);		\
		if (err)						\
			return err;					\
	} while (0)

static int ir_dev_uevent(struct device *device, struct kobj_uevent_env *env)
{
	struct ir_input_dev *ir_dev = dev_get_drvdata(device);

	if (ir_dev->rc_tab.name)
		ADD_HOTPLUG_VAR("NAME=\"%s\"", ir_dev->rc_tab.name);
	if (ir_dev->driver_name)
		ADD_HOTPLUG_VAR("DRV_NAME=\"%s\"", ir_dev->driver_name);

	return 0;
}

/*
 * Static device attribute struct with the sysfs attributes for IR's
 */
static DEVICE_ATTR(current_protocol, S_IRUGO | S_IWUSR,
		   show_protocol, store_protocol);

static struct attribute *ir_dev_attrs[] = {
	&dev_attr_current_protocol.attr,
	NULL,
};

static struct attribute_group ir_dev_attr_grp = {
	.attrs	= ir_dev_attrs,
};

static const struct attribute_group *ir_dev_attr_groups[] = {
	&ir_dev_attr_grp,
	NULL
};

static struct device_type ir_dev_type = {
	.groups		= ir_dev_attr_groups,
	.uevent		= ir_dev_uevent,
};

/**
 * ir_register_class() - creates the sysfs for /sys/class/irrcv/irrcv?
 * @input_dev:	the struct input_dev descriptor of the device
 *
 * This routine is used to register the syfs code for IR class
 */
int ir_register_class(struct input_dev *input_dev)
{
	int rc;
	const char *path;

	struct ir_input_dev *ir_dev = input_get_drvdata(input_dev);
	int devno = find_first_zero_bit(&ir_core_dev_number,
					IRRCV_NUM_DEVICES);

	if (unlikely(devno < 0))
		return devno;

	ir_dev->dev.type = &ir_dev_type;
	ir_dev->dev.class = &ir_input_class;
	ir_dev->dev.parent = input_dev->dev.parent;
	dev_set_name(&ir_dev->dev, "irrcv%d", devno);
	dev_set_drvdata(&ir_dev->dev, ir_dev);
	rc = device_register(&ir_dev->dev);
	if (rc)
		return rc;


	input_dev->dev.parent = &ir_dev->dev;
	rc = input_register_device(input_dev);
	if (rc < 0) {
		device_del(&ir_dev->dev);
		return rc;
	}

	__module_get(THIS_MODULE);

	path = kobject_get_path(&ir_dev->dev.kobj, GFP_KERNEL);
	printk(KERN_INFO "%s: %s as %s\n",
		dev_name(&ir_dev->dev),
		input_dev->name ? input_dev->name : "Unspecified device",
		path ? path : "N/A");
	kfree(path);

	ir_dev->devno = devno;
	set_bit(devno, &ir_core_dev_number);

	return 0;
};

/**
 * ir_unregister_class() - removes the sysfs for sysfs for
 *			   /sys/class/irrcv/irrcv?
 * @input_dev:	the struct input_dev descriptor of the device
 *
 * This routine is used to unregister the syfs code for IR class
 */
void ir_unregister_class(struct input_dev *input_dev)
{
	struct ir_input_dev *ir_dev = input_get_drvdata(input_dev);

	clear_bit(ir_dev->devno, &ir_core_dev_number);
	input_unregister_device(input_dev);
	device_del(&ir_dev->dev);

	module_put(THIS_MODULE);
}

/*
 * Init/exit code for the module. Basically, creates/removes /sys/class/irrcv
 */

static int __init ir_core_init(void)
{
	int rc = class_register(&ir_input_class);
	if (rc) {
		printk(KERN_ERR "ir_core: unable to register irrcv class\n");
		return rc;
	}

	return 0;
}

static void __exit ir_core_exit(void)
{
	class_unregister(&ir_input_class);
}

module_init(ir_core_init);
module_exit(ir_core_exit);
