/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * vsp1_dl.h  --  R-Car VSP1 Display List
 *
 * Copyright (C) 2015 Renesas Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */
#ifndef __VSP1_DL_H__
#define __VSP1_DL_H__

#include <linux/types.h>

struct vsp1_device;
struct vsp1_dl_body;
struct vsp1_dl_body_pool;
struct vsp1_dl_list;
struct vsp1_dl_manager;

#define VSP1_DL_FRAME_END_COMPLETED		BIT(0)
#define VSP1_DL_FRAME_END_INTERNAL		BIT(1)

/**
 * struct vsp1_dl_ext_cmd - Extended Display command
 * @pool: pool to which this command belongs
 * @free: entry in the pool of free commands list
 * @opcode: command type opcode
 * @flags: flags used by the command
 * @cmds: array of command bodies for this extended cmd
 * @num_cmds: quantity of commands in @cmds array
 * @cmd_dma: DMA address of the command body
 * @data: memory allocation for command-specific data
 * @data_dma: DMA address for command-specific data
 */
struct vsp1_dl_ext_cmd {
	struct vsp1_dl_cmd_pool *pool;
	struct list_head free;

	u8 opcode;
	u32 flags;

	struct vsp1_pre_ext_dl_body *cmds;
	unsigned int num_cmds;
	dma_addr_t cmd_dma;

	void *data;
	dma_addr_t data_dma;
};

void vsp1_dlm_setup(struct vsp1_device *vsp1);

struct vsp1_dl_manager *vsp1_dlm_create(struct vsp1_device *vsp1,
					unsigned int index,
					unsigned int prealloc);
void vsp1_dlm_destroy(struct vsp1_dl_manager *dlm);
void vsp1_dlm_reset(struct vsp1_dl_manager *dlm);
unsigned int vsp1_dlm_irq_frame_end(struct vsp1_dl_manager *dlm);
struct vsp1_dl_body *vsp1_dlm_dl_body_get(struct vsp1_dl_manager *dlm);

struct vsp1_dl_list *vsp1_dl_list_get(struct vsp1_dl_manager *dlm);
void vsp1_dl_list_put(struct vsp1_dl_list *dl);
struct vsp1_dl_body *vsp1_dl_list_get_body0(struct vsp1_dl_list *dl);
struct vsp1_dl_ext_cmd *vsp1_dl_get_pre_cmd(struct vsp1_dl_list *dl);
void vsp1_dl_list_commit(struct vsp1_dl_list *dl, bool internal);

struct vsp1_dl_body_pool *
vsp1_dl_body_pool_create(struct vsp1_device *vsp1, unsigned int num_bodies,
			 unsigned int num_entries, size_t extra_size);
void vsp1_dl_body_pool_destroy(struct vsp1_dl_body_pool *pool);
struct vsp1_dl_body *vsp1_dl_body_get(struct vsp1_dl_body_pool *pool);
void vsp1_dl_body_put(struct vsp1_dl_body *dlb);

void vsp1_dl_body_write(struct vsp1_dl_body *dlb, u32 reg, u32 data);
int vsp1_dl_list_add_body(struct vsp1_dl_list *dl, struct vsp1_dl_body *dlb);
int vsp1_dl_list_add_chain(struct vsp1_dl_list *head, struct vsp1_dl_list *dl);

#endif /* __VSP1_DL_H__ */
