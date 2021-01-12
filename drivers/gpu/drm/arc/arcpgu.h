/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ARC PGU DRM driver.
 *
 * Copyright (C) 2016 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef _ARCPGU_H_
#define _ARCPGU_H_

#include <drm/drm_simple_kms_helper.h>

struct arcpgu_drm_private {
	struct drm_device	drm;
	void __iomem		*regs;
	struct clk		*clk;
	struct drm_simple_display_pipe pipe;
	struct drm_connector	sim_conn;
};

#define dev_to_arcpgu(x) container_of(x, struct arcpgu_drm_private, drm)

#define pipe_to_arcpgu_priv(x) container_of(x, struct arcpgu_drm_private, pipe)

static inline void arc_pgu_write(struct arcpgu_drm_private *arcpgu,
				 unsigned int reg, u32 value)
{
	iowrite32(value, arcpgu->regs + reg);
}

static inline u32 arc_pgu_read(struct arcpgu_drm_private *arcpgu,
			       unsigned int reg)
{
	return ioread32(arcpgu->regs + reg);
}

int arcpgu_drm_sim_init(struct drm_device *drm, struct device_node *np);

#endif
