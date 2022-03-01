// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OpenFirmware helpers for memory drivers
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 * Copyright (C) 2019 Samsung Electronics Co., Ltd.
 * Copyright (C) 2020 Krzysztof Kozlowski <krzk@kernel.org>
 */

#include <linux/device.h>
#include <linux/of.h>
#include <linux/gfp.h>
#include <linux/export.h>

#include "jedec_ddr.h"
#include "of_memory.h"

/**
 * of_get_min_tck() - extract min timing values for ddr
 * @np: pointer to ddr device tree node
 * @dev: device requesting for min timing values
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
	dev_warn(dev, "Using default min-tck values\n");
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
						struct device *dev,
						u32 device_type,
						u32 *nr_frequencies)
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
		dev_warn(dev, "Unsupported memory type\n");
	}

	for_each_child_of_node(np_ddr, np_tim)
		if (of_device_is_compatible(np_tim, tim_compat))
			arr_sz++;

	if (arr_sz)
		timings = devm_kcalloc(dev, arr_sz, sizeof(*timings),
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
	dev_warn(dev, "Using default memory timings\n");
	*nr_frequencies = ARRAY_SIZE(lpddr2_jedec_timings);
	return lpddr2_jedec_timings;
}
EXPORT_SYMBOL(of_get_ddr_timings);

/**
 * of_lpddr3_get_min_tck() - extract min timing values for lpddr3
 * @np: pointer to ddr device tree node
 * @dev: device requesting for min timing values
 *
 * Populates the lpddr3_min_tck structure by extracting data
 * from device tree node. Returns a pointer to the populated
 * structure. If any error in populating the structure, returns NULL.
 */
const struct lpddr3_min_tck *of_lpddr3_get_min_tck(struct device_node *np,
						   struct device *dev)
{
	int			ret = 0;
	struct lpddr3_min_tck	*min;

	min = devm_kzalloc(dev, sizeof(*min), GFP_KERNEL);
	if (!min)
		goto default_min_tck;

	ret |= of_property_read_u32(np, "tRFC-min-tck", &min->tRFC);
	ret |= of_property_read_u32(np, "tRRD-min-tck", &min->tRRD);
	ret |= of_property_read_u32(np, "tRPab-min-tck", &min->tRPab);
	ret |= of_property_read_u32(np, "tRPpb-min-tck", &min->tRPpb);
	ret |= of_property_read_u32(np, "tRCD-min-tck", &min->tRCD);
	ret |= of_property_read_u32(np, "tRC-min-tck", &min->tRC);
	ret |= of_property_read_u32(np, "tRAS-min-tck", &min->tRAS);
	ret |= of_property_read_u32(np, "tWTR-min-tck", &min->tWTR);
	ret |= of_property_read_u32(np, "tWR-min-tck", &min->tWR);
	ret |= of_property_read_u32(np, "tRTP-min-tck", &min->tRTP);
	ret |= of_property_read_u32(np, "tW2W-C2C-min-tck", &min->tW2W_C2C);
	ret |= of_property_read_u32(np, "tR2R-C2C-min-tck", &min->tR2R_C2C);
	ret |= of_property_read_u32(np, "tWL-min-tck", &min->tWL);
	ret |= of_property_read_u32(np, "tDQSCK-min-tck", &min->tDQSCK);
	ret |= of_property_read_u32(np, "tRL-min-tck", &min->tRL);
	ret |= of_property_read_u32(np, "tFAW-min-tck", &min->tFAW);
	ret |= of_property_read_u32(np, "tXSR-min-tck", &min->tXSR);
	ret |= of_property_read_u32(np, "tXP-min-tck", &min->tXP);
	ret |= of_property_read_u32(np, "tCKE-min-tck", &min->tCKE);
	ret |= of_property_read_u32(np, "tCKESR-min-tck", &min->tCKESR);
	ret |= of_property_read_u32(np, "tMRD-min-tck", &min->tMRD);

	if (ret) {
		dev_warn(dev, "Errors while parsing min-tck values\n");
		devm_kfree(dev, min);
		goto default_min_tck;
	}

	return min;

default_min_tck:
	dev_warn(dev, "Using default min-tck values\n");
	return NULL;
}
EXPORT_SYMBOL(of_lpddr3_get_min_tck);

static int of_lpddr3_do_get_timings(struct device_node *np,
				    struct lpddr3_timings *tim)
{
	int ret;

	ret = of_property_read_u32(np, "max-freq", &tim->max_freq);
	if (ret)
		/* Deprecated way of passing max-freq as 'reg' */
		ret = of_property_read_u32(np, "reg", &tim->max_freq);
	ret |= of_property_read_u32(np, "min-freq", &tim->min_freq);
	ret |= of_property_read_u32(np, "tRFC", &tim->tRFC);
	ret |= of_property_read_u32(np, "tRRD", &tim->tRRD);
	ret |= of_property_read_u32(np, "tRPab", &tim->tRPab);
	ret |= of_property_read_u32(np, "tRPpb", &tim->tRPpb);
	ret |= of_property_read_u32(np, "tRCD", &tim->tRCD);
	ret |= of_property_read_u32(np, "tRC", &tim->tRC);
	ret |= of_property_read_u32(np, "tRAS", &tim->tRAS);
	ret |= of_property_read_u32(np, "tWTR", &tim->tWTR);
	ret |= of_property_read_u32(np, "tWR", &tim->tWR);
	ret |= of_property_read_u32(np, "tRTP", &tim->tRTP);
	ret |= of_property_read_u32(np, "tW2W-C2C", &tim->tW2W_C2C);
	ret |= of_property_read_u32(np, "tR2R-C2C", &tim->tR2R_C2C);
	ret |= of_property_read_u32(np, "tFAW", &tim->tFAW);
	ret |= of_property_read_u32(np, "tXSR", &tim->tXSR);
	ret |= of_property_read_u32(np, "tXP", &tim->tXP);
	ret |= of_property_read_u32(np, "tCKE", &tim->tCKE);
	ret |= of_property_read_u32(np, "tCKESR", &tim->tCKESR);
	ret |= of_property_read_u32(np, "tMRD", &tim->tMRD);

	return ret;
}

/**
 * of_lpddr3_get_ddr_timings() - extracts the lpddr3 timings and updates no of
 * frequencies available.
 * @np_ddr: Pointer to ddr device tree node
 * @dev: Device requesting for ddr timings
 * @device_type: Type of ddr
 * @nr_frequencies: No of frequencies available for ddr
 * (updated by this function)
 *
 * Populates lpddr3_timings structure by extracting data from device
 * tree node. Returns pointer to populated structure. If any error
 * while populating, returns NULL.
 */
const struct lpddr3_timings
*of_lpddr3_get_ddr_timings(struct device_node *np_ddr, struct device *dev,
			   u32 device_type, u32 *nr_frequencies)
{
	struct lpddr3_timings	*timings = NULL;
	u32			arr_sz = 0, i = 0;
	struct device_node	*np_tim;
	char			*tim_compat = NULL;

	switch (device_type) {
	case DDR_TYPE_LPDDR3:
		tim_compat = "jedec,lpddr3-timings";
		break;
	default:
		dev_warn(dev, "Unsupported memory type\n");
	}

	for_each_child_of_node(np_ddr, np_tim)
		if (of_device_is_compatible(np_tim, tim_compat))
			arr_sz++;

	if (arr_sz)
		timings = devm_kcalloc(dev, arr_sz, sizeof(*timings),
				       GFP_KERNEL);

	if (!timings)
		goto default_timings;

	for_each_child_of_node(np_ddr, np_tim) {
		if (of_device_is_compatible(np_tim, tim_compat)) {
			if (of_lpddr3_do_get_timings(np_tim, &timings[i])) {
				devm_kfree(dev, timings);
				goto default_timings;
			}
			i++;
		}
	}

	*nr_frequencies = arr_sz;

	return timings;

default_timings:
	dev_warn(dev, "Failed to get timings\n");
	*nr_frequencies = 0;
	return NULL;
}
EXPORT_SYMBOL(of_lpddr3_get_ddr_timings);

/**
 * of_lpddr2_get_info() - extracts information about the lpddr2 chip.
 * @np: Pointer to device tree node containing lpddr2 info
 * @dev: Device requesting info
 *
 * Populates lpddr2_info structure by extracting data from device
 * tree node. Returns pointer to populated structure. If error
 * happened while populating, returns NULL. If property is missing
 * in a device-tree, then the corresponding value is set to -ENOENT.
 */
const struct lpddr2_info
*of_lpddr2_get_info(struct device_node *np, struct device *dev)
{
	struct lpddr2_info *ret_info, info = {};
	struct property *prop;
	const char *cp;
	int err;
	u32 revision_id[2];

	err = of_property_read_u32_array(np, "revision-id", revision_id, 2);
	if (!err) {
		info.revision_id1 = revision_id[0];
		info.revision_id2 = revision_id[1];
	} else {
		err = of_property_read_u32(np, "revision-id1", &info.revision_id1);
		if (err)
			info.revision_id1 = -ENOENT;

		err = of_property_read_u32(np, "revision-id2", &info.revision_id2);
		if (err)
			info.revision_id2 = -ENOENT;
	}

	err = of_property_read_u32(np, "io-width", &info.io_width);
	if (err)
		return NULL;

	info.io_width = 32 / info.io_width - 1;

	err = of_property_read_u32(np, "density", &info.density);
	if (err)
		return NULL;

	info.density = ffs(info.density) - 7;

	if (of_device_is_compatible(np, "jedec,lpddr2-s4"))
		info.arch_type = LPDDR2_TYPE_S4;
	else if (of_device_is_compatible(np, "jedec,lpddr2-s2"))
		info.arch_type = LPDDR2_TYPE_S2;
	else if (of_device_is_compatible(np, "jedec,lpddr2-nvm"))
		info.arch_type = LPDDR2_TYPE_NVM;
	else
		return NULL;

	prop = of_find_property(np, "compatible", NULL);
	for (cp = of_prop_next_string(prop, NULL); cp;
	     cp = of_prop_next_string(prop, cp)) {

#define OF_LPDDR2_VENDOR_CMP(compat, ID) \
		if (!of_compat_cmp(cp, compat ",", strlen(compat ","))) { \
			info.manufacturer_id = LPDDR2_MANID_##ID; \
			break; \
		}

		OF_LPDDR2_VENDOR_CMP("samsung", SAMSUNG)
		OF_LPDDR2_VENDOR_CMP("qimonda", QIMONDA)
		OF_LPDDR2_VENDOR_CMP("elpida", ELPIDA)
		OF_LPDDR2_VENDOR_CMP("etron", ETRON)
		OF_LPDDR2_VENDOR_CMP("nanya", NANYA)
		OF_LPDDR2_VENDOR_CMP("hynix", HYNIX)
		OF_LPDDR2_VENDOR_CMP("mosel", MOSEL)
		OF_LPDDR2_VENDOR_CMP("winbond", WINBOND)
		OF_LPDDR2_VENDOR_CMP("esmt", ESMT)
		OF_LPDDR2_VENDOR_CMP("spansion", SPANSION)
		OF_LPDDR2_VENDOR_CMP("sst", SST)
		OF_LPDDR2_VENDOR_CMP("zmos", ZMOS)
		OF_LPDDR2_VENDOR_CMP("intel", INTEL)
		OF_LPDDR2_VENDOR_CMP("numonyx", NUMONYX)
		OF_LPDDR2_VENDOR_CMP("micron", MICRON)

#undef OF_LPDDR2_VENDOR_CMP
	}

	if (!info.manufacturer_id)
		info.manufacturer_id = -ENOENT;

	ret_info = devm_kzalloc(dev, sizeof(*ret_info), GFP_KERNEL);
	if (ret_info)
		*ret_info = info;

	return ret_info;
}
EXPORT_SYMBOL(of_lpddr2_get_info);
