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

#include <linux/types.h>

struct vsp1_device;
struct vsp1_dl_list;

/**
 * struct vsp1_dl_manager - Display List manager
 * @vsp1: the VSP1 device
 * @lock: protects the active, queued and pending lists
 * @free: array of all free display lists
 * @active: list currently being processed (loaded) by hardware
 * @queued: list queued to the hardware (written to the DL registers)
 * @pending: list waiting to be queued to the hardware
 */
struct vsp1_dl_manager {
	struct vsp1_device *vsp1;

	spinlock_t lock;
	struct list_head free;
	struct vsp1_dl_list *active;
	struct vsp1_dl_list *queued;
	struct vsp1_dl_list *pending;
};

void vsp1_dlm_setup(struct vsp1_device *vsp1);

int vsp1_dlm_init(struct vsp1_device *vsp1, struct vsp1_dl_manager *dlm,
		  unsigned int prealloc);
void vsp1_dlm_cleanup(struct vsp1_dl_manager *dlm);
void vsp1_dlm_reset(struct vsp1_dl_manager *dlm);
void vsp1_dlm_irq_display_start(struct vsp1_dl_manager *dlm);
void vsp1_dlm_irq_frame_end(struct vsp1_dl_manager *dlm);

struct vsp1_dl_list *vsp1_dl_list_get(struct vsp1_dl_manager *dlm);
void vsp1_dl_list_put(struct vsp1_dl_list *dl);
void vsp1_dl_list_write(struct vsp1_dl_list *dl, u32 reg, u32 data);
void vsp1_dl_list_commit(struct vsp1_dl_list *dl);

#endif /* __VSP1_DL_H__ */
