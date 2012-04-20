/*
 *  drivers/extcon/extcon_class.c
 *
 *  External connector (extcon) class driver
 *
 * Copyright (C) 2012 Samsung Electronics
 * Author: Donggeun Kim <dg77.kim@samsung.com>
 * Author: MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * based on android/drivers/switch/switch_class.c
 * Copyright (C) 2008 Google, Inc.
 * Author: Mike Lockwood <lockwood@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
*/

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/extcon.h>
#include <linux/slab.h>

struct class *extcon_class;
#if defined(CONFIG_ANDROID) && !defined(CONFIG_ANDROID_SWITCH)
static struct class_compat *switch_class;
#endif /* CONFIG_ANDROID && !defined(CONFIG_ANDROID_SWITCH) */

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct extcon_dev *edev = (struct extcon_dev *) dev_get_drvdata(dev);

	if (edev->print_state) {
		int ret = edev->print_state(edev, buf);

		if (ret >= 0)
			return ret;
		/* Use default if failed */
	}
	return sprintf(buf, "%u\n", edev->state);
}

static ssize_t name_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct extcon_dev *edev = (struct extcon_dev *) dev_get_drvdata(dev);

	/* Optional callback given by the user */
	if (edev->print_name) {
		int ret = edev->print_name(edev, buf);
		if (ret >= 0)
			return ret;
	}

	return sprintf(buf, "%s\n", dev_name(edev->dev));
}

/**
 * extcon_set_state() - Set the cable attach states of the extcon device.
 * @edev:	the extcon device
 * @state:	new cable attach status for @edev
 *
 * Changing the state sends uevent with environment variable containing
 * the name of extcon device (envp[0]) and the state output (envp[1]).
 * Tizen uses this format for extcon device to get events from ports.
 * Android uses this format as well.
 */
void extcon_set_state(struct extcon_dev *edev, u32 state)
{
	char name_buf[120];
	char state_buf[120];
	char *prop_buf;
	char *envp[3];
	int env_offset = 0;
	int length;

	if (edev->state != state) {
		edev->state = state;

		prop_buf = (char *)get_zeroed_page(GFP_KERNEL);
		if (prop_buf) {
			length = name_show(edev->dev, NULL, prop_buf);
			if (length > 0) {
				if (prop_buf[length - 1] == '\n')
					prop_buf[length - 1] = 0;
				snprintf(name_buf, sizeof(name_buf),
					"NAME=%s", prop_buf);
				envp[env_offset++] = name_buf;
			}
			length = state_show(edev->dev, NULL, prop_buf);
			if (length > 0) {
				if (prop_buf[length - 1] == '\n')
					prop_buf[length - 1] = 0;
				snprintf(state_buf, sizeof(state_buf),
					"STATE=%s", prop_buf);
				envp[env_offset++] = state_buf;
			}
			envp[env_offset] = NULL;
			kobject_uevent_env(&edev->dev->kobj, KOBJ_CHANGE, envp);
			free_page((unsigned long)prop_buf);
		} else {
			dev_err(edev->dev, "out of memory in extcon_set_state\n");
			kobject_uevent(&edev->dev->kobj, KOBJ_CHANGE);
		}
	}
}
EXPORT_SYMBOL_GPL(extcon_set_state);

static struct device_attribute extcon_attrs[] = {
	__ATTR_RO(state),
	__ATTR_RO(name),
};

static int create_extcon_class(void)
{
	if (!extcon_class) {
		extcon_class = class_create(THIS_MODULE, "extcon");
		if (IS_ERR(extcon_class))
			return PTR_ERR(extcon_class);
		extcon_class->dev_attrs = extcon_attrs;

#if defined(CONFIG_ANDROID) && !defined(CONFIG_ANDROID_SWITCH)
		switch_class = class_compat_register("switch");
		if (WARN(!switch_class, "cannot allocate"))
			return -ENOMEM;
#endif /* CONFIG_ANDROID && !defined(CONFIG_ANDROID_SWITCH) */
	}

	return 0;
}

static void extcon_cleanup(struct extcon_dev *edev, bool skip)
{
	if (!skip && get_device(edev->dev)) {
		device_unregister(edev->dev);
		put_device(edev->dev);
	}

	kfree(edev->dev);
}

static void extcon_dev_release(struct device *dev)
{
	struct extcon_dev *edev = (struct extcon_dev *) dev_get_drvdata(dev);

	extcon_cleanup(edev, true);
}

/**
 * extcon_dev_register() - Register a new extcon device
 * @edev	: the new extcon device (should be allocated before calling)
 * @dev		: the parent device for this extcon device.
 *
 * Among the members of edev struct, please set the "user initializing data"
 * in any case and set the "optional callbacks" if required. However, please
 * do not set the values of "internal data", which are initialized by
 * this function.
 */
int extcon_dev_register(struct extcon_dev *edev, struct device *dev)
{
	int ret;

	if (!extcon_class) {
		ret = create_extcon_class();
		if (ret < 0)
			return ret;
	}

	edev->dev = kzalloc(sizeof(struct device), GFP_KERNEL);
	edev->dev->parent = dev;
	edev->dev->class = extcon_class;
	edev->dev->release = extcon_dev_release;

	dev_set_name(edev->dev, edev->name ? edev->name : dev_name(dev));
	ret = device_register(edev->dev);
	if (ret) {
		put_device(edev->dev);
		goto err_dev;
	}
#if defined(CONFIG_ANDROID) && !defined(CONFIG_ANDROID_SWITCH)
	if (switch_class)
		ret = class_compat_create_link(switch_class, edev->dev,
					       dev);
#endif /* CONFIG_ANDROID && !defined(CONFIG_ANDROID_SWITCH) */

	dev_set_drvdata(edev->dev, edev);
	edev->state = 0;
	return 0;

err_dev:
	kfree(edev->dev);
	return ret;
}
EXPORT_SYMBOL_GPL(extcon_dev_register);

/**
 * extcon_dev_unregister() - Unregister the extcon device.
 * @edev:	the extcon device instance to be unregitered.
 *
 * Note that this does not call kfree(edev) because edev was not allocated
 * by this class.
 */
void extcon_dev_unregister(struct extcon_dev *edev)
{
	extcon_cleanup(edev, false);
}
EXPORT_SYMBOL_GPL(extcon_dev_unregister);

static int __init extcon_class_init(void)
{
	return create_extcon_class();
}
module_init(extcon_class_init);

static void __exit extcon_class_exit(void)
{
	class_destroy(extcon_class);
}
module_exit(extcon_class_exit);

MODULE_AUTHOR("Mike Lockwood <lockwood@android.com>");
MODULE_AUTHOR("Donggeun Kim <dg77.kim@samsung.com>");
MODULE_AUTHOR("MyungJoo Ham <myungjoo.ham@samsung.com>");
MODULE_DESCRIPTION("External connector (extcon) class driver");
MODULE_LICENSE("GPL");
