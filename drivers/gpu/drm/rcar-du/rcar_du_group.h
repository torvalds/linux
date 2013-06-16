/*
 * rcar_du_group.c  --  R-Car Display Unit Planes and CRTCs Group
 *
 * Copyright (C) 2013 Renesas Corporation
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

#include "rcar_du_plane.h"

struct rcar_du_device;

/*
 * struct rcar_du_group - CRTCs and planes group
 * @dev: the DU device
 * @mmio_offset: registers offset in the device memory map
 * @index: group index
 * @use_count: number of users of the group (rcar_du_group_(get|put))
 * @used_crtcs: number of CRTCs currently in use
 * @planes: planes handled by the group
 */
struct rcar_du_group {
	struct rcar_du_device *dev;
	unsigned int mmio_offset;
	unsigned int index;

	unsigned int use_count;
	unsigned int used_crtcs;

	struct rcar_du_planes planes;
};

int rcar_du_group_get(struct rcar_du_group *rgrp);
void rcar_du_group_put(struct rcar_du_group *rgrp);
void rcar_du_group_start_stop(struct rcar_du_group *rgrp, bool start);
void rcar_du_group_restart(struct rcar_du_group *rgrp);
void rcar_du_group_set_routing(struct rcar_du_group *rgrp);

#endif /* __RCAR_DU_GROUP_H__ */
