// SPDX-License-Identifier: GPL-2.0-only
/*
 * generic display timing functions
 *
 * Copyright (c) 2012 Steffen Trumtrar <s.trumtrar@pengutronix.de>, Pengutronix
 */

#include <linux/errno.h>
#include <linux/export.h>
#include <video/display_timing.h>
#include <video/videomode.h>

void videomode_from_timing(const struct display_timing *dt,
			  struct videomode *vm)
{
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
}
EXPORT_SYMBOL_GPL(videomode_from_timing);

int videomode_from_timings(const struct display_timings *disp,
			  struct videomode *vm, unsigned int index)
{
	struct display_timing *dt;

	dt = display_timings_get(disp, index);
	if (!dt)
		return -EINVAL;

	videomode_from_timing(dt, vm);

	return 0;
}
EXPORT_SYMBOL_GPL(videomode_from_timings);
