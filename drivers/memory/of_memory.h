/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenFirmware helpers for memory drivers
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 * Copyright (C) 2020 Krzysztof Kozlowski <krzk@kernel.org>
 */

#ifndef __LINUX_MEMORY_OF_REG_H
#define __LINUX_MEMORY_OF_REG_H

#if defined(CONFIG_OF) && defined(CONFIG_DDR)
const struct lpddr2_min_tck *of_get_min_tck(struct device_node *np,
					    struct device *dev);
const struct lpddr2_timings *of_get_ddr_timings(struct device_node *np_ddr,
						struct device *dev,
						u32 device_type, u32 *nr_frequencies);
const struct lpddr3_min_tck *of_lpddr3_get_min_tck(struct device_node *np,
						   struct device *dev);
const struct lpddr3_timings *
of_lpddr3_get_ddr_timings(struct device_node *np_ddr,
			  struct device *dev, u32 device_type, u32 *nr_frequencies);

const struct lpddr2_info *of_lpddr2_get_info(struct device_node *np,
					     struct device *dev);
#else
static inline const struct lpddr2_min_tck
	*of_get_min_tck(struct device_node *np, struct device *dev)
{
	return NULL;
}

static inline const struct lpddr2_timings
	*of_get_ddr_timings(struct device_node *np_ddr, struct device *dev,
	u32 device_type, u32 *nr_frequencies)
{
	return NULL;
}

static inline const struct lpddr3_min_tck
	*of_lpddr3_get_min_tck(struct device_node *np, struct device *dev)
{
	return NULL;
}

static inline const struct lpddr3_timings
	*of_lpddr3_get_ddr_timings(struct device_node *np_ddr,
	struct device *dev, u32 device_type, u32 *nr_frequencies)
{
	return NULL;
}

static inline const struct lpddr2_info
	*of_lpddr2_get_info(struct device_node *np, struct device *dev)
{
	return NULL;
}
#endif /* CONFIG_OF && CONFIG_DDR */

#endif /* __LINUX_MEMORY_OF_REG_ */
