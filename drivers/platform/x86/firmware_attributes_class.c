// SPDX-License-Identifier: GPL-2.0-or-later

/* Firmware attributes class helper module */

#include <linux/mutex.h>
#include <linux/device/class.h>
#include <linux/module.h>
#include "firmware_attributes_class.h"

static DEFINE_MUTEX(fw_attr_lock);
static int fw_attr_inuse;

static const struct class firmware_attributes_class = {
	.name = "firmware-attributes",
};

int fw_attributes_class_get(const struct class **fw_attr_class)
{
	int err;

	mutex_lock(&fw_attr_lock);
	if (!fw_attr_inuse) { /*first time class is being used*/
		err = class_register(&firmware_attributes_class);
		if (err) {
			mutex_unlock(&fw_attr_lock);
			return err;
		}
	}
	fw_attr_inuse++;
	*fw_attr_class = &firmware_attributes_class;
	mutex_unlock(&fw_attr_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(fw_attributes_class_get);

int fw_attributes_class_put(void)
{
	mutex_lock(&fw_attr_lock);
	if (!fw_attr_inuse) {
		mutex_unlock(&fw_attr_lock);
		return -EINVAL;
	}
	fw_attr_inuse--;
	if (!fw_attr_inuse) /* No more consumers */
		class_unregister(&firmware_attributes_class);
	mutex_unlock(&fw_attr_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(fw_attributes_class_put);

MODULE_AUTHOR("Mark Pearson <markpearson@lenovo.com>");
MODULE_LICENSE("GPL");
