// SPDX-License-Identifier: GPL-2.0
/*
 *	uvc_video.h  --  USB Video Class Gadget driver
 *
 * Copyright (C) 2009-2010
 *		Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *		Author: Andrzej Pietrasiewicz <andrzej.p@samsung.com>
 */
#ifndef __UVC_VIDEO_H__
#define __UVC_VIDEO_H__

struct uvc_video;

int uvcg_video_pump(struct uvc_video *video);

int uvcg_video_enable(struct uvc_video *video, int enable);

int uvcg_video_init(struct uvc_video *video);

#endif /* __UVC_VIDEO_H__ */
