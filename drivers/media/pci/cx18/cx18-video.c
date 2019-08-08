// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  cx18 video interface functions
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 */

#include "cx18-driver.h"
#include "cx18-video.h"
#include "cx18-cards.h"

void cx18_video_set_io(struct cx18 *cx)
{
	int inp = cx->active_input;

	v4l2_subdev_call(cx->sd_av, video, s_routing,
			cx->card->video_inputs[inp].video_input, 0, 0);
}
