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
struct vsp1_dl_fragment;
struct vsp1_dl_list;
struct vsp1_dl_manager;

#define VSP1_DL_FRAME_END_COMPLETED		BIT(0)

void vsp1_dlm_setup(struct vsp1_device *vsp1);

struct vsp1_dl_manager *vsp1_dlm_create(struct vsp1_device *vsp1,
					unsigned int index,
					unsigned int prealloc);
void vsp1_dlm_destroy(struct vsp1_dl_manager *dlm);
void vsp1_dlm_reset(struct vsp1_dl_manager *dlm);
unsigned int vsp1_dlm_irq_frame_end(struct vsp1_dl_manager *dlm);

struct vsp1_dl_list *vsp1_dl_list_get(struct vsp1_dl_manager *dlm);
void vsp1_dl_list_put(struct vsp1_dl_list *dl);
void vsp1_dl_list_write(struct vsp1_dl_list *dl, u32 reg, u32 data);
void vsp1_dl_list_commit(struct vsp1_dl_list *dl);

struct vsp1_dl_body *vsp1_dl_fragment_alloc(struct vsp1_device *vsp1,
					    unsigned int num_entries);
void vsp1_dl_fragment_free(struct vsp1_dl_body *dlb);
void vsp1_dl_fragment_write(struct vsp1_dl_body *dlb, u32 reg, u32 data);
int vsp1_dl_list_add_fragment(struct vsp1_dl_list *dl,
			      struct vsp1_dl_body *dlb);
int vsp1_dl_list_add_chain(struct vsp1_dl_list *head, struct vsp1_dl_list *dl);

#endif /* __VSP1_DL_H__ */
