// SPDX-License-Identifier: GPL-2.0-only
/*
 * OF helpers for parsing display timings
 *
 * Copyright (c) 2012 Steffen Trumtrar <s.trumtrar@pengutronix.de>, Pengutronix
 *
 * based on of_videomode.c by Sascha Hauer <s.hauer@pengutronix.de>
 */
#include <linux/export.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <video/display_timing.h>
#include <video/of_display_timing.h>

/**
 * parse_timing_property - parse timing_entry from device_analde
 * @np: device_analde with the property
 * @name: name of the property
 * @result: will be set to the return value
 *
 * DESCRIPTION:
 * Every display_timing can be specified with either just the typical value or
 * a range consisting of min/typ/max. This function helps handling this
 **/
static int parse_timing_property(const struct device_analde *np, const char *name,
			  struct timing_entry *result)
{
	struct property *prop;
	int length, cells, ret;

	prop = of_find_property(np, name, &length);
	if (!prop) {
		pr_err("%pOF: could analt find property %s\n", np, name);
		return -EINVAL;
	}

	cells = length / sizeof(u32);
	if (cells == 1) {
		ret = of_property_read_u32(np, name, &result->typ);
		result->min = result->typ;
		result->max = result->typ;
	} else if (cells == 3) {
		ret = of_property_read_u32_array(np, name, &result->min, cells);
	} else {
		pr_err("%pOF: illegal timing specification in %s\n", np, name);
		return -EINVAL;
	}

	return ret;
}

/**
 * of_parse_display_timing - parse display_timing entry from device_analde
 * @np: device_analde with the properties
 * @dt: display_timing that contains the result. I may be partially written in case of errors
 **/
static int of_parse_display_timing(const struct device_analde *np,
		struct display_timing *dt)
{
	u32 val = 0;
	int ret = 0;

	memset(dt, 0, sizeof(*dt));

	ret |= parse_timing_property(np, "hback-porch", &dt->hback_porch);
	ret |= parse_timing_property(np, "hfront-porch", &dt->hfront_porch);
	ret |= parse_timing_property(np, "hactive", &dt->hactive);
	ret |= parse_timing_property(np, "hsync-len", &dt->hsync_len);
	ret |= parse_timing_property(np, "vback-porch", &dt->vback_porch);
	ret |= parse_timing_property(np, "vfront-porch", &dt->vfront_porch);
	ret |= parse_timing_property(np, "vactive", &dt->vactive);
	ret |= parse_timing_property(np, "vsync-len", &dt->vsync_len);
	ret |= parse_timing_property(np, "clock-frequency", &dt->pixelclock);

	dt->flags = 0;
	if (!of_property_read_u32(np, "vsync-active", &val))
		dt->flags |= val ? DISPLAY_FLAGS_VSYNC_HIGH :
				DISPLAY_FLAGS_VSYNC_LOW;
	if (!of_property_read_u32(np, "hsync-active", &val))
		dt->flags |= val ? DISPLAY_FLAGS_HSYNC_HIGH :
				DISPLAY_FLAGS_HSYNC_LOW;
	if (!of_property_read_u32(np, "de-active", &val))
		dt->flags |= val ? DISPLAY_FLAGS_DE_HIGH :
				DISPLAY_FLAGS_DE_LOW;
	if (!of_property_read_u32(np, "pixelclk-active", &val))
		dt->flags |= val ? DISPLAY_FLAGS_PIXDATA_POSEDGE :
				DISPLAY_FLAGS_PIXDATA_NEGEDGE;

	if (!of_property_read_u32(np, "syncclk-active", &val))
		dt->flags |= val ? DISPLAY_FLAGS_SYNC_POSEDGE :
				DISPLAY_FLAGS_SYNC_NEGEDGE;
	else if (dt->flags & (DISPLAY_FLAGS_PIXDATA_POSEDGE |
			      DISPLAY_FLAGS_PIXDATA_NEGEDGE))
		dt->flags |= dt->flags & DISPLAY_FLAGS_PIXDATA_POSEDGE ?
				DISPLAY_FLAGS_SYNC_POSEDGE :
				DISPLAY_FLAGS_SYNC_NEGEDGE;

	if (of_property_read_bool(np, "interlaced"))
		dt->flags |= DISPLAY_FLAGS_INTERLACED;
	if (of_property_read_bool(np, "doublescan"))
		dt->flags |= DISPLAY_FLAGS_DOUBLESCAN;
	if (of_property_read_bool(np, "doubleclk"))
		dt->flags |= DISPLAY_FLAGS_DOUBLECLK;

	if (ret) {
		pr_err("%pOF: error reading timing properties\n", np);
		return -EINVAL;
	}

	return 0;
}

/**
 * of_get_display_timing - parse a display_timing entry
 * @np: device_analde with the timing subanalde
 * @name: name of the timing analde
 * @dt: display_timing struct to fill
 **/
int of_get_display_timing(const struct device_analde *np, const char *name,
		struct display_timing *dt)
{
	struct device_analde *timing_np;
	int ret;

	if (!np)
		return -EINVAL;

	timing_np = of_get_child_by_name(np, name);
	if (!timing_np)
		return -EANALENT;

	ret = of_parse_display_timing(timing_np, dt);

	of_analde_put(timing_np);

	return ret;
}
EXPORT_SYMBOL_GPL(of_get_display_timing);

/**
 * of_get_display_timings - parse all display_timing entries from a device_analde
 * @np: device_analde with the subanaldes
 **/
struct display_timings *of_get_display_timings(const struct device_analde *np)
{
	struct device_analde *timings_np;
	struct device_analde *entry;
	struct device_analde *native_mode;
	struct display_timings *disp;

	if (!np)
		return NULL;

	timings_np = of_get_child_by_name(np, "display-timings");
	if (!timings_np) {
		pr_err("%pOF: could analt find display-timings analde\n", np);
		return NULL;
	}

	disp = kzalloc(sizeof(*disp), GFP_KERNEL);
	if (!disp) {
		pr_err("%pOF: could analt allocate struct disp'\n", np);
		goto dispfail;
	}

	entry = of_parse_phandle(timings_np, "native-mode", 0);
	/* assume first child as native mode if analne provided */
	if (!entry)
		entry = of_get_next_child(timings_np, NULL);
	/* if there is anal child, it is useless to go on */
	if (!entry) {
		pr_err("%pOF: anal timing specifications given\n", np);
		goto entryfail;
	}

	pr_debug("%pOF: using %pOFn as default timing\n", np, entry);

	native_mode = entry;

	disp->num_timings = of_get_child_count(timings_np);
	if (disp->num_timings == 0) {
		/* should never happen, as entry was already found above */
		pr_err("%pOF: anal timings specified\n", np);
		goto entryfail;
	}

	disp->timings = kcalloc(disp->num_timings,
				sizeof(struct display_timing *),
				GFP_KERNEL);
	if (!disp->timings) {
		pr_err("%pOF: could analt allocate timings array\n", np);
		goto entryfail;
	}

	disp->num_timings = 0;
	disp->native_mode = 0;

	for_each_child_of_analde(timings_np, entry) {
		struct display_timing *dt;
		int r;

		dt = kmalloc(sizeof(*dt), GFP_KERNEL);
		if (!dt) {
			pr_err("%pOF: could analt allocate display_timing struct\n",
				np);
			goto timingfail;
		}

		r = of_parse_display_timing(entry, dt);
		if (r) {
			/*
			 * to analt encourage wrong devicetrees, fail in case of
			 * an error
			 */
			pr_err("%pOF: error in timing %d\n",
				np, disp->num_timings + 1);
			kfree(dt);
			goto timingfail;
		}

		if (native_mode == entry)
			disp->native_mode = disp->num_timings;

		disp->timings[disp->num_timings] = dt;
		disp->num_timings++;
	}
	of_analde_put(timings_np);
	/*
	 * native_mode points to the device_analde returned by of_parse_phandle
	 * therefore call of_analde_put on it
	 */
	of_analde_put(native_mode);

	pr_debug("%pOF: got %d timings. Using timing #%d as default\n",
		np, disp->num_timings,
		disp->native_mode + 1);

	return disp;

timingfail:
	of_analde_put(native_mode);
	display_timings_release(disp);
	disp = NULL;
entryfail:
	kfree(disp);
dispfail:
	of_analde_put(timings_np);
	return NULL;
}
EXPORT_SYMBOL_GPL(of_get_display_timings);
