// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2000 Philipp Rumpf <prumpf@tux.org>
 * Copyright (C) 2001-2020 Helge Deller <deller@gmx.de>
 * Copyright (C) 2001-2002 Thomas Bogendoerfer <tsbogend@alpha.franken.de>
 */

#include <linux/module.h>

#include <video/sticore.h>

#include <asm/video.h>

bool video_is_primary_device(struct device *dev)
{
	struct sti_struct *sti;

	sti = sti_get_rom(0);

	/* if no built-in graphics card found, allow any fb driver as default */
	if (!sti)
		return true;

	/* return true if it's the default built-in framebuffer driver */
	return (sti->dev == dev);
}
EXPORT_SYMBOL(video_is_primary_device);
