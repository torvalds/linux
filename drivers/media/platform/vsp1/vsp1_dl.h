/*
 * vsp1_dl.h  --  R-Car VSP1 Display List
 *
 * Copyright (C) 2015 Renesas Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __VSP1_DL_H__
#define __VSP1_DL_H__

#include "vsp1_entity.h"

struct vsp1_device;
struct vsp1_dl;

struct vsp1_dl *vsp1_dl_create(struct vsp1_device *vsp1);
void vsp1_dl_destroy(struct vsp1_dl *dl);

void vsp1_dl_setup(struct vsp1_device *vsp1);

void vsp1_dl_reset(struct vsp1_dl *dl);
void vsp1_dl_begin(struct vsp1_dl *dl);
void vsp1_dl_add(struct vsp1_entity *e, u32 reg, u32 data);
void vsp1_dl_commit(struct vsp1_dl *dl);

void vsp1_dl_irq_display_start(struct vsp1_dl *dl);
void vsp1_dl_irq_frame_end(struct vsp1_dl *dl);

static inline void vsp1_dl_mod_write(struct vsp1_entity *e, u32 reg, u32 data)
{
	if (e->vsp1->use_dl)
		vsp1_dl_add(e, reg, data);
	else
		vsp1_write(e->vsp1, reg, data);
}

#endif /* __VSP1_DL_H__ */
