// SPDX-License-Identifier: GPL-2.0

#include <linux/console.h>
#include <linux/fb.h>
#include <linux/module.h>

#include <asm/prom.h>

int fb_is_primary_device(struct fb_info *info)
{
	struct device *dev = info->device;
	struct device_node *node;

	if (console_set_on_cmdline)
		return 0;

	node = dev->of_node;
	if (node && node == of_console_device)
		return 1;

	return 0;
}
EXPORT_SYMBOL(fb_is_primary_device);
