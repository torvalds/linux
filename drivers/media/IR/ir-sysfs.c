/* ir-sysfs.c - sysfs interface for RC devices (/sys/class/rc)
 *
 * Copyright (C) 2009-2010 by Mauro Carvalho Chehab <mchehab@redhat.com>
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
#include "ir-core-priv.h"

#define IRRCV_NUM_DEVICES	256

/* bit array to represent IR sysfs device number */
static unsigned long ir_core_dev_number;

/* class for /sys/class/rc */
static char *ir_devnode(struct device *dev, mode_t *mode)
{
	return kasprintf(GFP_KERNEL, "rc/%s", dev_name(dev));
}

static struct class ir_input_class = {
	.name		= "rc",
	.devnode	= ir_devnode,
};

/**
 * show_protocol() - shows the current IR protocol
 * @d:		the device descriptor
 * @mattr:	the device attribute struct (unused)
 * @buf:	a pointer to the output buffer
 *
 * This routine is a callback routine for input read the IR protocol type.
 * it is trigged by reading /sys/class/rc/rc?/current_protocol.
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
		s = "rc-5";
	else if (ir_type == IR_TYPE_NEC)
		s = "nec";
	else if (ir_type == IR_TYPE_RC6)
		s = "rc6";
	else if (ir_type == IR_TYPE_JVC)
		s = "jvc";
	else if (ir_type == IR_TYPE_SONY)
		s = "sony";
	else
		s = "other";

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
 * it is trigged by reading /sys/class/rc/rc?/current_protocol.
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
	u64 ir_type = 0;
	int rc = -EINVAL;
	unsigned long flags;
	char *buf;

	while ((buf = strsep((char **) &data, " \n")) != NULL) {
		if (!strcasecmp(buf, "rc-5") || !strcasecmp(buf, "rc5"))
			ir_type |= IR_TYPE_RC5;
		if (!strcasecmp(buf, "nec"))
			ir_type |= IR_TYPE_NEC;
		if (!strcasecmp(buf, "jvc"))
			ir_type |= IR_TYPE_JVC;
		if (!strcasecmp(buf, "sony"))
			ir_type |= IR_TYPE_SONY;
	}

	if (!ir_type) {
		IR_dprintk(1, "Unknown protocol\n");
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

	IR_dprintk(1, "Current protocol(s) is(are) %lld\n",
		   (long long)ir_type);

	return len;
}

static ssize_t show_supported_protocols(struct device *d,
			     struct device_attribute *mattr, char *buf)
{
	char *orgbuf = buf;
	struct ir_input_dev *ir_dev = dev_get_drvdata(d);

	/* FIXME: doesn't support multiple protocols at the same time */
	if (ir_dev->props->allowed_protos == IR_TYPE_UNKNOWN)
		buf += sprintf(buf, "unknown ");
	if (ir_dev->props->allowed_protos & IR_TYPE_RC5)
		buf += sprintf(buf, "rc-5 ");
	if (ir_dev->props->allowed_protos & IR_TYPE_NEC)
		buf += sprintf(buf, "nec ");
	if (buf == orgbuf)
		buf += sprintf(buf, "other ");

	buf += sprintf(buf - 1, "\n");

	return buf - orgbuf;
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
		ADD_HOTPLUG_VAR("NAME=%s", ir_dev->rc_tab.name);
	if (ir_dev->driver_name)
		ADD_HOTPLUG_VAR("DRV_NAME=%s", ir_dev->driver_name);

	return 0;
}

/*
 * Static device attribute struct with the sysfs attributes for IR's
 */
static DEVICE_ATTR(protocol, S_IRUGO | S_IWUSR,
		   show_protocol, store_protocol);

static DEVICE_ATTR(supported_protocols, S_IRUGO | S_IWUSR,
		   show_supported_protocols, NULL);

static struct attribute *ir_hw_dev_attrs[] = {
	&dev_attr_protocol.attr,
	&dev_attr_supported_protocols.attr,
	NULL,
};

static struct attribute_group ir_hw_dev_attr_grp = {
	.attrs	= ir_hw_dev_attrs,
};

static const struct attribute_group *ir_hw_dev_attr_groups[] = {
	&ir_hw_dev_attr_grp,
	NULL
};

static struct device_type rc_dev_type = {
	.groups		= ir_hw_dev_attr_groups,
	.uevent		= ir_dev_uevent,
};

static struct device_type ir_raw_dev_type = {
	.uevent		= ir_dev_uevent,
};

/**
 * ir_register_class() - creates the sysfs for /sys/class/rc/rc?
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

	if (ir_dev->props) {
		if (ir_dev->props->driver_type == RC_DRIVER_SCANCODE)
			ir_dev->dev.type = &rc_dev_type;
	} else
		ir_dev->dev.type = &ir_raw_dev_type;

	ir_dev->dev.class = &ir_input_class;
	ir_dev->dev.parent = input_dev->dev.parent;
	dev_set_name(&ir_dev->dev, "rc%d", devno);
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
 *			   /sys/class/rc/rc?
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
 * Init/exit code for the module. Basically, creates/removes /sys/class/rc
 */

static int __init ir_core_init(void)
{
	int rc = class_register(&ir_input_class);
	if (rc) {
		printk(KERN_ERR "ir_core: unable to register rc class\n");
		return rc;
	}

	/* Initialize/load the decoders/keymap code that will be used */
	ir_raw_init();

	return 0;
}

static void __exit ir_core_exit(void)
{
	class_unregister(&ir_input_class);
}

module_init(ir_core_init);
module_exit(ir_core_exit);
