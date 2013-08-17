/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 * exynos5 fimc-is video functions
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FIMC_IS_SUBDEV_H
#define FIMC_IS_SUBDEV_H

struct fimc_is_group;

enum fimc_is_subdev_state {
	FIMC_IS_ISDEV_DOPEN,
	FIMC_IS_ISDEV_DSTART
};

struct fimc_is_subdev_path {
	u32					width;
	u32					height;
};

struct fimc_is_subdev {
	enum is_entry				entry;
	unsigned long				state;
	struct mutex				mutex_state;

	struct fimc_is_subdev_path		input;
	struct fimc_is_subdev_path		output;

	struct fimc_is_group			*group;
	struct fimc_is_video_ctx		*vctx;
	struct fimc_is_subdev			*leader;
};

#define GET_LEADER_FRAMEMGR(leader) \
	(((leader) && (leader)->vctx) ? (&(leader)->vctx->q_src.framemgr) : NULL)
#define GET_SUBDEV_FRAMEMGR(subdev) \
	(((subdev) && (subdev)->vctx) ? (&(subdev)->vctx->q_dst.framemgr) : NULL)

#define GET_LEADER_QUEUE(leader) \
	(((leader) && (leader)->vctx) ? (&(leader)->vctx->q_src) : NULL)
#define GET_SUBDEV_QUEUE(subdev) \
	(((subdev) && (subdev)->vctx) ? (&(subdev)->vctx->q_dst) : NULL)

#endif
