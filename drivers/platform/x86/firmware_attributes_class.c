// SPDX-License-Identifier: GPL-2.0-or-later

/* Firmware attributes class helper module */

#include <linux/module.h>
#include "firmware_attributes_class.h"

const struct class firmware_attributes_class = {
	.name = "firmware-attributes",
};
EXPORT_SYMBOL_GPL(firmware_attributes_class);

static __init int fw_attributes_class_init(void)
{
	return class_register(&firmware_attributes_class);
}
module_init(fw_attributes_class_init);

static __exit void fw_attributes_class_exit(void)
{
	class_unregister(&firmware_attributes_class);
}
module_exit(fw_attributes_class_exit);

MODULE_AUTHOR("Mark Pearson <markpearson@lenovo.com>");
MODULE_DESCRIPTION("Firmware attributes class helper module");
MODULE_LICENSE("GPL");
