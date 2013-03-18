/*
 * generic videomode helper
 *
 * Copyright (c) 2012 Steffen Trumtrar <s.trumtrar@pengutronix.de>, Pengutronix
 *
 * This file is released under the GPLv2
 */
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/of.h>
#include <video/display_timing.h>
#include <video/of_display_timing.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

/**
 * of_get_videomode - get the videomode #<index> from devicetree
 * @np - devicenode with the display_timings
 * @vm - set to return value
 * @index - index into list of display_timings
 *	    (Set this to OF_USE_NATIVE_MODE to use whatever mode is
 *	     specified as native mode in the DT.)
 *
 * DESCRIPTION:
 * Get a list of all display timings and put the one
 * specified by index into *vm. This function should only be used, if
 * only one videomode is to be retrieved. A driver that needs to work
 * with multiple/all videomodes should work with
 * of_get_display_timings instead.
 **/
int of_get_videomode(struct device_node *np, struct videomode *vm,
		     int index)
{
	struct display_timings *disp;
	int ret;

	disp = of_get_display_timings(np);
	if (!disp) {
		pr_err("%s: no timings specified\n", of_node_full_name(np));
		return -EINVAL;
	}

	if (index == OF_USE_NATIVE_MODE)
		index = disp->native_mode;

	ret = videomode_from_timing(disp, vm, index);
	if (ret)
		return ret;

	display_timings_release(disp);

	return 0;
}
EXPORT_SYMBOL_GPL(of_get_videomode);
