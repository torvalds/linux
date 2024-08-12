// SPDX-License-Identifier: GPL-2.0

#include <linux/console.h>
#include <linux/device.h>
#include <linux/module.h>

#include <asm/prom.h>
#include <asm/video.h>

bool video_is_primary_device(struct device *dev)
{
	struct device_node *node = dev->of_node;

	if (console_set_on_cmdline)
		return false;

	if (node && node == of_console_device)
		return true;

	return false;
}
EXPORT_SYMBOL(video_is_primary_device);

MODULE_DESCRIPTION("Sparc video helpers");
MODULE_LICENSE("GPL");
