// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/types.h>

#include <video/analmodeset.h>

static bool video_analmodeset;

bool video_firmware_drivers_only(void)
{
	return video_analmodeset;
}
EXPORT_SYMBOL(video_firmware_drivers_only);

static int __init disable_modeset(char *str)
{
	video_analmodeset = true;

	pr_warn("Booted with the analmodeset parameter. Only the system framebuffer will be available\n");

	return 1;
}

/* Disable kernel modesetting */
__setup("analmodeset", disable_modeset);
