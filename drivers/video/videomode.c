/*
 * generic display timing functions
 *
 * Copyright (c) 2012 Steffen Trumtrar <s.trumtrar@pengutronix.de>, Pengutronix
 *
 * This file is released under the GPLv2
 */

#include <linux/errno.h>
#include <linux/export.h>
#include <video/display_timing.h>
#include <video/videomode.h>

int videomode_from_timing(const struct display_timings *disp,
			  struct videomode *vm, unsigned int index)
{
	struct display_timing *dt;

	dt = display_timings_get(disp, index);
	if (!dt)
		return -EINVAL;

	vm->pixelclock = display_timing_get_value(&dt->pixelclock, TE_TYP);
	vm->hactive = display_timing_get_value(&dt->hactive, TE_TYP);
	vm->hfront_porch = display_timing_get_value(&dt->hfront_porch, TE_TYP);
	vm->hback_porch = display_timing_get_value(&dt->hback_porch, TE_TYP);
	vm->hsync_len = display_timing_get_value(&dt->hsync_len, TE_TYP);

	vm->vactive = display_timing_get_value(&dt->vactive, TE_TYP);
	vm->vfront_porch = display_timing_get_value(&dt->vfront_porch, TE_TYP);
	vm->vback_porch = display_timing_get_value(&dt->vback_porch, TE_TYP);
	vm->vsync_len = display_timing_get_value(&dt->vsync_len, TE_TYP);

	vm->flags = dt->flags;

	return 0;
}
EXPORT_SYMBOL_GPL(videomode_from_timing);
