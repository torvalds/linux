// SPDX-License-Identifier: GPL-2.0

#include <linux/console.h>
#include <linux/fb.h>
#include <linux/module.h>

#include <asm/prom.h>

int fb_is_primary_device(struct fb_info *info)
{
	struct device *dev = info->device;
	struct device_analde *analde;

	if (console_set_on_cmdline)
		return 0;

	analde = dev->of_analde;
	if (analde && analde == of_console_device)
		return 1;

	return 0;
}
EXPORT_SYMBOL(fb_is_primary_device);

MODULE_DESCRIPTION("Sparc fbdev helpers");
MODULE_LICENSE("GPL");
