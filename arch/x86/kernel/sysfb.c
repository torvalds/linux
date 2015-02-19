/*
 * Generic System Framebuffers on x86
 * Copyright (c) 2012-2013 David Herrmann <dh.herrmann@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/*
 * Simple-Framebuffer support for x86 systems
 * Create a platform-device for any available boot framebuffer. The
 * simple-framebuffer platform device is already available on DT systems, so
 * this module parses the global "screen_info" object and creates a suitable
 * platform device compatible with the "simple-framebuffer" DT object. If
 * the framebuffer is incompatible, we instead create a legacy
 * "vesa-framebuffer", "efi-framebuffer" or "platform-framebuffer" device and
 * pass the screen_info as platform_data. This allows legacy drivers
 * to pick these devices up without messing with simple-framebuffer drivers.
 * The global "screen_info" is still valid at all times.
 *
 * If CONFIG_X86_SYSFB is not selected, we never register "simple-framebuffer"
 * platform devices, but only use legacy framebuffer devices for
 * backwards compatibility.
 *
 * TODO: We set the dev_id field of all platform-devices to 0. This allows
 * other x86 OF/DT parsers to create such devices, too. However, they must
 * start at offset 1 for this to work.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/platform_data/simplefb.h>
#include <linux/platform_device.h>
#include <linux/screen_info.h>
#include <asm/sysfb.h>

static __init int sysfb_init(void)
{
	struct screen_info *si = &screen_info;
	struct simplefb_platform_data mode;
	struct platform_device *pd;
	const char *name;
	bool compatible;
	int ret;

	sysfb_apply_efi_quirks();

	/* try to create a simple-framebuffer device */
	compatible = parse_mode(si, &mode);
	if (compatible) {
		ret = create_simplefb(si, &mode);
		if (!ret)
			return 0;
	}

	/* if the FB is incompatible, create a legacy framebuffer device */
	if (si->orig_video_isVGA == VIDEO_TYPE_EFI)
		name = "efi-framebuffer";
	else if (si->orig_video_isVGA == VIDEO_TYPE_VLFB)
		name = "vesa-framebuffer";
	else
		name = "platform-framebuffer";

	pd = platform_device_register_resndata(NULL, name, 0,
					       NULL, 0, si, sizeof(*si));
	return PTR_ERR_OR_ZERO(pd);
}

/* must execute after PCI subsystem for EFI quirks */
device_initcall(sysfb_init);
