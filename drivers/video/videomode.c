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

	vm->pixelclock = dt->pixelclock.typ;
	vm->hactive = dt->hactive.typ;
	vm->hfront_porch = dt->hfront_porch.typ;
	vm->hback_porch = dt->hback_porch.typ;
	vm->hsync_len = dt->hsync_len.typ;

	vm->vactive = dt->vactive.typ;
	vm->vfront_porch = dt->vfront_porch.typ;
	vm->vback_porch = dt->vback_porch.typ;
	vm->vsync_len = dt->vsync_len.typ;

	vm->flags = dt->flags;

	return 0;
}
EXPORT_SYMBOL_GPL(videomode_from_timing);
