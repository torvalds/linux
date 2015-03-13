/*
 * vsp1.h
 *
 * Renesas R-Car VSP1 - User-space API
 *
 * Copyright (C) 2013 Renesas Corporation
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __VSP1_USER_H__
#define __VSP1_USER_H__

#include <linux/types.h>
#include <linux/videodev2.h>

/*
 * Private IOCTLs
 *
 * VIDIOC_VSP1_LUT_CONFIG - Configure the lookup table
 */

#define VIDIOC_VSP1_LUT_CONFIG \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 1, struct vsp1_lut_config)

struct vsp1_lut_config {
	u32 lut[256];
};

#endif	/* __VSP1_USER_H__ */
