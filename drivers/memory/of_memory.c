/*
 * OpenFirmware helpers for memory drivers
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/gfp.h>
#include <memory/jedec_ddr.h>
#include <linux/export.h>

/**
 * of_get_min_tck() - extract min timing values for ddr
 * @np: pointer to ddr device tree node
 * @device: device requesting for min timing values
 *
 * Populates the lpddr2_min_tck structure by extracting data
 * from device tree node. Returns a pointer to the populated
 * structure. If any error in populating the structure, returns
 * default min timings provided by JEDEC.
 */
const struct lpddr2_min_tck *of_get_min_tck(struct device_node *np,
		struct device *dev)
{
	int			ret = 0;
	struct lpddr2_min_tck	*min;

	min = devm_kzalloc(dev, sizeof(*min), GFP_KERNEL);
	if (!min)
		goto default_min_tck;

	ret |= of_property_read_u32(np, "tRPab-min-tck", &min->tRPab);
	ret |= of_property_read_u32(np, "tRCD-min-tck", &min->tRCD);
	ret |= of_property_read_u32(np, "tWR-min-tck", &min->tWR);
	ret |= of_property_read_u32(np, "tRASmin-min-tck", &min->tRASmin);
	ret |= of_property_read_u32(np, "tRRD-min-tck", &min->tRRD);
	ret |= of_property_read_u32(np, "tWTR-min-tck", &min->tWTR);
	ret |= of_property_read_u32(np, "tXP-min-tck", &min->tXP);
	ret |= of_property_read_u32(np, "tRTP-min-tck", &min->tRTP);
	ret |= of_property_read_u32(np, "tCKE-min-tck", &min->tCKE);
	ret |= of_property_read_u32(np, "tCKESR-min-tck", &min->tCKESR);
	ret |= of_property_read_u32(np, "tFAW-min-tck", &min->tFAW);

	if (ret) {
		devm_kfree(dev, min);
		goto default_min_tck;
	}

	return min;

default_min_tck:
	dev_warn(dev, "%s: using default min-tck values\n", __func__);
	return &lpddr2_jedec_min_tck;
}
EXPORT_SYMBOL(of_get_min_tck);

static int of_do_get_timings(struct device_node *np,
		struct lpddr2_timings *tim)
{
	int ret;

	ret = of_property_read_u32(np, "max-freq", &tim->max_freq);
	ret |= of_property_read_u32(np, "min-freq", &tim->min_freq);
	ret |= of_property_read_u32(np, "tRPab", &tim->tRPab);
	ret |= of_property_read_u32(np, "tRCD", &tim->tRCD);
	ret |= of_property_read_u32(np, "tWR", &tim->tWR);
	ret |= of_property_read_u32(np, "tRAS-min", &tim->tRAS_min);
	ret |= of_property_read_u32(np, "tRRD", &tim->tRRD);
	ret |= of_property_read_u32(np, "tWTR", &tim->tWTR);
	ret |= of_property_read_u32(np, "tXP", &tim->tXP);
	ret |= of_property_read_u32(np, "tRTP", &tim->tRTP);
	ret |= of_property_read_u32(np, "tCKESR", &tim->tCKESR);
	ret |= of_property_read_u32(np, "tDQSCK-max", &tim->tDQSCK_max);
	ret |= of_property_read_u32(np, "tFAW", &tim->tFAW);
	ret |= of_property_read_u32(np, "tZQCS", &tim->tZQCS);
	ret |= of_property_read_u32(np, "tZQCL", &tim->tZQCL);
	ret |= of_property_read_u32(np, "tZQinit", &tim->tZQinit);
	ret |= of_property_read_u32(np, "tRAS-max-ns", &tim->tRAS_max_ns);
	ret |= of_property_read_u32(np, "tDQSCK-max-derated",
		&tim->tDQSCK_max_derated);

	return ret;
}

/**
 * of_get_ddr_timings() - extracts the ddr timings and updates no of
 * frequencies available.
 * @np_ddr: Pointer to ddr device tree node
 * @dev: Device requesting for ddr timings
 * @device_type: Type of ddr(LPDDR2 S2/S4)
 * @nr_frequencies: No of frequencies available for ddr
 * (updated by this function)
 *
 * Populates lpddr2_timings structure by extracting data from device
 * tree node. Returns pointer to populated structure. If any error
 * while populating, returns default timings provided by JEDEC.
 */
const struct lpddr2_timings *of_get_ddr_timings(struct device_node *np_ddr,
		struct device *dev, u32 device_type, u32 *nr_frequencies)
{
	struct lpddr2_timings	*timings = NULL;
	u32			arr_sz = 0, i = 0;
	struct device_node	*np_tim;
	char			*tim_compat = NULL;

	switch (device_type) {
	case DDR_TYPE_LPDDR2_S2:
	case DDR_TYPE_LPDDR2_S4:
		tim_compat = "jedec,lpddr2-timings";
		break;
	default:
		dev_warn(dev, "%s: un-supported memory type\n", __func__);
	}

	for_each_child_of_node(np_ddr, np_tim)
		if (of_device_is_compatible(np_tim, tim_compat))
			arr_sz++;

	if (arr_sz)
		timings = devm_kzalloc(dev, sizeof(*timings) * arr_sz,
			GFP_KERNEL);

	if (!timings)
		goto default_timings;

	for_each_child_of_node(np_ddr, np_tim) {
		if (of_device_is_compatible(np_tim, tim_compat)) {
			if (of_do_get_timings(np_tim, &timings[i])) {
				devm_kfree(dev, timings);
				goto default_timings;
			}
			i++;
		}
	}

	*nr_frequencies = arr_sz;

	return timings;

default_timings:
	dev_warn(dev, "%s: using default timings\n", __func__);
	*nr_frequencies = ARRAY_SIZE(lpddr2_jedec_timings);
	return lpddr2_jedec_timings;
}
EXPORT_SYMBOL(of_get_ddr_timings);
