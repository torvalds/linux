/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_CATALOG_H_
#define _DP_CATALOG_H_

#include <drm/drm_modes.h>

#include "dp_utils.h"
#include "disp/msm_disp_snapshot.h"

#define DP_HW_VERSION_1_0	0x10000000
#define DP_HW_VERSION_1_2	0x10020000

struct msm_dp_catalog {
	bool wide_bus_en;

	void __iomem *ahb_base;
	size_t ahb_len;

	void __iomem *aux_base;
	size_t aux_len;

	void __iomem *link_base;
	size_t link_len;

	void __iomem *p0_base;
	size_t p0_len;
};

/* IO */
static inline u32 msm_dp_read_aux(struct msm_dp_catalog *msm_dp_catalog, u32 offset)
{
	return readl_relaxed(msm_dp_catalog->aux_base + offset);
}

static inline void msm_dp_write_aux(struct msm_dp_catalog *msm_dp_catalog,
			       u32 offset, u32 data)
{
	/*
	 * To make sure aux reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, msm_dp_catalog->aux_base + offset);
}

static inline u32 msm_dp_read_ahb(const struct msm_dp_catalog *msm_dp_catalog, u32 offset)
{
	return readl_relaxed(msm_dp_catalog->ahb_base + offset);
}

static inline void msm_dp_write_ahb(struct msm_dp_catalog *msm_dp_catalog,
			       u32 offset, u32 data)
{
	/*
	 * To make sure phy reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, msm_dp_catalog->ahb_base + offset);
}

static inline void msm_dp_write_p0(struct msm_dp_catalog *msm_dp_catalog,
			       u32 offset, u32 data)
{
	/*
	 * To make sure interface reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, msm_dp_catalog->p0_base + offset);
}

static inline u32 msm_dp_read_p0(struct msm_dp_catalog *msm_dp_catalog,
			       u32 offset)
{
	return readl_relaxed(msm_dp_catalog->p0_base + offset);
}

static inline u32 msm_dp_read_link(struct msm_dp_catalog *msm_dp_catalog, u32 offset)
{
	return readl_relaxed(msm_dp_catalog->link_base + offset);
}

static inline void msm_dp_write_link(struct msm_dp_catalog *msm_dp_catalog,
			       u32 offset, u32 data)
{
	/*
	 * To make sure link reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, msm_dp_catalog->link_base + offset);
}

/* Debug module */
void msm_dp_catalog_snapshot(struct msm_dp_catalog *msm_dp_catalog, struct msm_disp_state *disp_state);

struct msm_dp_catalog *msm_dp_catalog_get(struct device *dev);

#endif /* _DP_CATALOG_H_ */
