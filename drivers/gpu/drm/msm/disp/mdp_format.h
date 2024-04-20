/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#ifndef __MSM_FORMAT_H__
#define __MSM_FORMAT_H__

#include "mdp_common.xml.h"

enum msm_format_flags {
	MSM_FORMAT_FLAG_YUV_BIT,
	MSM_FORMAT_FLAG_DX_BIT,
	MSM_FORMAT_FLAG_COMPRESSED_BIT,
};

#define MSM_FORMAT_FLAG_YUV		BIT(MSM_FORMAT_FLAG_YUV_BIT)
#define MSM_FORMAT_FLAG_DX		BIT(MSM_FORMAT_FLAG_DX_BIT)
#define MSM_FORMAT_FLAG_COMPRESSED	BIT(MSM_FORMAT_FLAG_COMPRESSED_BIT)

struct msm_format {
	uint32_t pixel_format;
	unsigned long flags;
	enum mdp_fetch_mode fetch_mode;
};

#define MSM_FORMAT_IS_YUV(X)		((X)->flags & MSM_FORMAT_FLAG_YUV)
#define MSM_FORMAT_IS_DX(X)		((X)->flags & MSM_FORMAT_FLAG_DX)
#define MSM_FORMAT_IS_LINEAR(X)		((X)->fetch_mode == MDP_FETCH_LINEAR)
#define MSM_FORMAT_IS_TILE(X) \
	(((X)->fetch_mode == MDP_FETCH_UBWC) && \
	 !((X)->flags & MSM_FORMAT_FLAG_COMPRESSED))
#define MSM_FORMAT_IS_UBWC(X) \
	(((X)->fetch_mode == MDP_FETCH_UBWC) && \
	 ((X)->flags & MSM_FORMAT_FLAG_COMPRESSED))

#endif
