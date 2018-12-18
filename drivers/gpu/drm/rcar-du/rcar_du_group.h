/*
 * rcar_du_group.c  --  R-Car Display Unit Planes and CRTCs Group
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __RCAR_DU_GROUP_H__
#define __RCAR_DU_GROUP_H__

#include <linux/mutex.h>

#include "rcar_du_plane.h"

struct rcar_du_device;

/*
 * struct rcar_du_group - CRTCs and planes group
 * @dev: the DU device
 * @mmio_offset: registers offset in the device memory map
 * @index: group index
 * @channels_mask: bitmask of populated DU channels in this group
 * @num_crtcs: number of CRTCs in this group (1 or 2)
 * @use_count: number of users of the group (rcar_du_group_(get|put))
 * @used_crtcs: number of CRTCs currently in use
 * @lock: protects the dptsr_planes field and the DPTSR register
 * @dptsr_planes: bitmask of planes driven by dot-clock and timing generator 1
 * @num_planes: number of planes in the group
 * @planes: planes handled by the group
 * @need_restart: the group needs to be restarted due to a configuration change
 */
struct rcar_du_group {
	struct rcar_du_device *dev;
	unsigned int mmio_offset;
	unsigned int index;

	unsigned int channels_mask;
	unsigned int num_crtcs;
	unsigned int use_count;
	unsigned int used_crtcs;

	struct mutex lock;
	unsigned int dptsr_planes;

	unsigned int num_planes;
	struct rcar_du_plane planes[RCAR_DU_NUM_KMS_PLANES];
	bool need_restart;
};

u32 rcar_du_group_read(struct rcar_du_group *rgrp, u32 reg);
void rcar_du_group_write(struct rcar_du_group *rgrp, u32 reg, u32 data);

int rcar_du_group_get(struct rcar_du_group *rgrp);
void rcar_du_group_put(struct rcar_du_group *rgrp);
void rcar_du_group_start_stop(struct rcar_du_group *rgrp, bool start);
void rcar_du_group_restart(struct rcar_du_group *rgrp);
int rcar_du_group_set_routing(struct rcar_du_group *rgrp);

int rcar_du_set_dpad0_vsp1_routing(struct rcar_du_device *rcdu);

#endif /* __RCAR_DU_GROUP_H__ */
