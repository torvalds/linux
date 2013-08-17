/* linux/drivers/media/video/exynos/fimg2d/fimg2d_ctx.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *	http://www.samsung.com/
 *
 * Samsung Graphics 2D driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include "fimg2d.h"
#include "fimg2d_helper.h"

static inline void fimg2d_enqueue(struct list_head *node, struct list_head *q)
{
	list_add_tail(node, q);
}

static inline void fimg2d_dequeue(struct list_head *node)
{
	list_del(node);
}

static inline int fimg2d_queue_is_empty(struct list_head *q)
{
	return list_empty(q);
}

static inline
struct fimg2d_bltcmd *fimg2d_get_first_command(struct fimg2d_control *ctrl)
{
	if (list_empty(&ctrl->cmd_q))
		return NULL;

	return list_first_entry(&ctrl->cmd_q, struct fimg2d_bltcmd, node);
}

void fimg2d_add_context(struct fimg2d_control *ctrl,
		struct fimg2d_context *ctx);
void fimg2d_del_context(struct fimg2d_control *ctrl,
		struct fimg2d_context *ctx);
int fimg2d_add_command(struct fimg2d_control *ctrl,
		struct fimg2d_context *ctx, struct fimg2d_blit __user *buf);
void fimg2d_del_command(struct fimg2d_control *ctrl, struct fimg2d_bltcmd *cmd);
struct fimg2d_bltcmd *fimg2d_get_command(struct fimg2d_control *ctrl);
