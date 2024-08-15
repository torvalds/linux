// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 *  Copyright (C) 2004 Aurelien Alleaume <slts@free.fr>
 */

/*

   This source file is specifically designed to interface with the
   saa711x support that is available in the v4l available starting
   with linux 2.6.15.

*/

#include "pvrusb2-video-v4l.h"



#include "pvrusb2-hdw-internal.h"
#include "pvrusb2-debug.h"
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/i2c/saa7115.h>
#include <linux/errno.h>

struct routing_scheme {
	const int *def;
	unsigned int cnt;
};


static const int routing_scheme0[] = {
	[PVR2_CVAL_INPUT_TV] = SAA7115_COMPOSITE4,
	/* In radio mode, we mute the video, but point at one
	   spot just to stay consistent */
	[PVR2_CVAL_INPUT_RADIO] = SAA7115_COMPOSITE5,
	[PVR2_CVAL_INPUT_COMPOSITE] = SAA7115_COMPOSITE5,
	[PVR2_CVAL_INPUT_SVIDEO] =  SAA7115_SVIDEO2,
};

static const struct routing_scheme routing_def0 = {
	.def = routing_scheme0,
	.cnt = ARRAY_SIZE(routing_scheme0),
};

static const int routing_scheme1[] = {
	[PVR2_CVAL_INPUT_TV] = SAA7115_COMPOSITE4,
	[PVR2_CVAL_INPUT_RADIO] = SAA7115_COMPOSITE5,
	[PVR2_CVAL_INPUT_COMPOSITE] = SAA7115_COMPOSITE3,
	[PVR2_CVAL_INPUT_SVIDEO] =  SAA7115_SVIDEO2, /* or SVIDEO0, it seems */
};

static const struct routing_scheme routing_def1 = {
	.def = routing_scheme1,
	.cnt = ARRAY_SIZE(routing_scheme1),
};

static const struct routing_scheme *routing_schemes[] = {
	[PVR2_ROUTING_SCHEME_HAUPPAUGE] = &routing_def0,
	[PVR2_ROUTING_SCHEME_ONAIR] = &routing_def1,
};

void pvr2_saa7115_subdev_update(struct pvr2_hdw *hdw, struct v4l2_subdev *sd)
{
	if (hdw->input_dirty || hdw->force_dirty) {
		const struct routing_scheme *sp;
		unsigned int sid = hdw->hdw_desc->signal_routing_scheme;
		u32 input;

		pvr2_trace(PVR2_TRACE_CHIPS, "subdev v4l2 set_input(%d)",
			   hdw->input_val);

		sp = (sid < ARRAY_SIZE(routing_schemes)) ?
			routing_schemes[sid] : NULL;
		if ((sp == NULL) ||
		    (hdw->input_val < 0) ||
		    (hdw->input_val >= sp->cnt)) {
			pvr2_trace(PVR2_TRACE_ERROR_LEGS,
				   "*** WARNING *** subdev v4l2 set_input: Invalid routing scheme (%u) and/or input (%d)",
				   sid, hdw->input_val);
			return;
		}
		input = sp->def[hdw->input_val];
		sd->ops->video->s_routing(sd, input, 0, 0);
	}
}
