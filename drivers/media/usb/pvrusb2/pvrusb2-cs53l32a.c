// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 *  Copyright (C) 2004 Aurelien Alleaume <slts@free.fr>
 */

/*

   This source file is specifically designed to interface with the
   v4l-dvb cs53l32a module.

*/

#include "pvrusb2-cs53l32a.h"


#include "pvrusb2-hdw-internal.h"
#include "pvrusb2-debug.h"
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <linux/errno.h>

struct routing_scheme {
	const int *def;
	unsigned int cnt;
};


static const int routing_scheme1[] = {
	[PVR2_CVAL_INPUT_TV] = 2,  /* 1 or 2 seems to work here */
	[PVR2_CVAL_INPUT_RADIO] = 2,
	[PVR2_CVAL_INPUT_COMPOSITE] = 0,
	[PVR2_CVAL_INPUT_SVIDEO] =  0,
};

static const struct routing_scheme routing_def1 = {
	.def = routing_scheme1,
	.cnt = ARRAY_SIZE(routing_scheme1),
};

static const struct routing_scheme *routing_schemes[] = {
	[PVR2_ROUTING_SCHEME_ONAIR] = &routing_def1,
};


void pvr2_cs53l32a_subdev_update(struct pvr2_hdw *hdw, struct v4l2_subdev *sd)
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
		sd->ops->audio->s_routing(sd, input, 0, 0);
	}
}
