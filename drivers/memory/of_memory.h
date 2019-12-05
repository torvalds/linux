/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenFirmware helpers for memory drivers
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 */

#ifndef __LINUX_MEMORY_OF_REG_H
#define __LINUX_MEMORY_OF_REG_H

#if defined(CONFIG_OF) && defined(CONFIG_DDR)
extern const struct lpddr2_min_tck *of_get_min_tck(struct device_node *np,
		struct device *dev);
extern const struct lpddr2_timings
	*of_get_ddr_timings(struct device_node *np_ddr, struct device *dev,
	u32 device_type, u32 *nr_frequencies);
extern const struct lpddr3_min_tck
	*of_lpddr3_get_min_tck(struct device_node *np, struct device *dev);
extern const struct lpddr3_timings
	*of_lpddr3_get_ddr_timings(struct device_node *np_ddr,
	struct device *dev, u32 device_type, u32 *nr_frequencies);
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
#endif /* CONFIG_OF && CONFIG_DDR */

#endif /* __LINUX_MEMORY_OF_REG_ */
