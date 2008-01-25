/*
 *
 *  $Id$
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 *  Copyright (C) 2004 Aurelien Alleaume <slts@free.fr>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/*

   This source file is specifically designed to interface with the
   saa711x support that is available in the v4l available starting
   with linux 2.6.15.

*/

#include "pvrusb2-video-v4l.h"
#include "pvrusb2-i2c-cmd-v4l2.h"


#include "pvrusb2-hdw-internal.h"
#include "pvrusb2-debug.h"
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/saa7115.h>
#include <linux/errno.h>
#include <linux/slab.h>

struct pvr2_v4l_decoder {
	struct pvr2_i2c_handler handler;
	struct pvr2_decoder_ctrl ctrl;
	struct pvr2_i2c_client *client;
	struct pvr2_hdw *hdw;
	unsigned long stale_mask;
};


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

static const struct routing_scheme routing_schemes[] = {
	[PVR2_ROUTING_SCHEME_HAUPPAUGE] = {
		.def = routing_scheme0,
		.cnt = ARRAY_SIZE(routing_scheme0),
	},
};

static void set_input(struct pvr2_v4l_decoder *ctxt)
{
	struct pvr2_hdw *hdw = ctxt->hdw;
	struct v4l2_routing route;
	const struct routing_scheme *sp;
	unsigned int sid = hdw->hdw_desc->signal_routing_scheme;

	pvr2_trace(PVR2_TRACE_CHIPS,"i2c v4l2 set_input(%d)",hdw->input_val);

	if ((sid < ARRAY_SIZE(routing_schemes)) &&
	    ((sp = routing_schemes + sid) != 0) &&
	    (hdw->input_val >= 0) &&
	    (hdw->input_val < sp->cnt)) {
		route.input = sp->def[hdw->input_val];
	} else {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "*** WARNING *** i2c v4l2 set_input:"
			   " Invalid routing scheme (%u) and/or input (%d)",
			   sid,hdw->input_val);
		return;
	}

	route.output = 0;
	pvr2_i2c_client_cmd(ctxt->client,VIDIOC_INT_S_VIDEO_ROUTING,&route);
}


static int check_input(struct pvr2_v4l_decoder *ctxt)
{
	struct pvr2_hdw *hdw = ctxt->hdw;
	return hdw->input_dirty != 0;
}


static void set_audio(struct pvr2_v4l_decoder *ctxt)
{
	u32 val;
	struct pvr2_hdw *hdw = ctxt->hdw;

	pvr2_trace(PVR2_TRACE_CHIPS,"i2c v4l2 set_audio %d",
		   hdw->srate_val);
	switch (hdw->srate_val) {
	default:
	case V4L2_MPEG_AUDIO_SAMPLING_FREQ_48000:
		val = 48000;
		break;
	case V4L2_MPEG_AUDIO_SAMPLING_FREQ_44100:
		val = 44100;
		break;
	case V4L2_MPEG_AUDIO_SAMPLING_FREQ_32000:
		val = 32000;
		break;
	}
	pvr2_i2c_client_cmd(ctxt->client,VIDIOC_INT_AUDIO_CLOCK_FREQ,&val);
}


static int check_audio(struct pvr2_v4l_decoder *ctxt)
{
	struct pvr2_hdw *hdw = ctxt->hdw;
	return hdw->srate_dirty != 0;
}


struct pvr2_v4l_decoder_ops {
	void (*update)(struct pvr2_v4l_decoder *);
	int (*check)(struct pvr2_v4l_decoder *);
};


static const struct pvr2_v4l_decoder_ops decoder_ops[] = {
	{ .update = set_input, .check = check_input},
	{ .update = set_audio, .check = check_audio},
};


static void decoder_detach(struct pvr2_v4l_decoder *ctxt)
{
	ctxt->client->handler = NULL;
	pvr2_hdw_set_decoder(ctxt->hdw,NULL);
	kfree(ctxt);
}


static int decoder_check(struct pvr2_v4l_decoder *ctxt)
{
	unsigned long msk;
	unsigned int idx;

	for (idx = 0; idx < ARRAY_SIZE(decoder_ops); idx++) {
		msk = 1 << idx;
		if (ctxt->stale_mask & msk) continue;
		if (decoder_ops[idx].check(ctxt)) {
			ctxt->stale_mask |= msk;
		}
	}
	return ctxt->stale_mask != 0;
}


static void decoder_update(struct pvr2_v4l_decoder *ctxt)
{
	unsigned long msk;
	unsigned int idx;

	for (idx = 0; idx < ARRAY_SIZE(decoder_ops); idx++) {
		msk = 1 << idx;
		if (!(ctxt->stale_mask & msk)) continue;
		ctxt->stale_mask &= ~msk;
		decoder_ops[idx].update(ctxt);
	}
}


static int decoder_detect(struct pvr2_i2c_client *cp)
{
	/* Attempt to query the decoder - let's see if it will answer */
	struct v4l2_tuner vt;
	int ret;

	memset(&vt,0,sizeof(vt));
	ret = pvr2_i2c_client_cmd(cp,VIDIOC_G_TUNER,&vt);
	return ret == 0; /* Return true if it answered */
}


static void decoder_enable(struct pvr2_v4l_decoder *ctxt,int fl)
{
	pvr2_trace(PVR2_TRACE_CHIPS,"i2c v4l2 decoder_enable(%d)",fl);
	pvr2_v4l2_cmd_stream(ctxt->client,fl);
}


static unsigned int decoder_describe(struct pvr2_v4l_decoder *ctxt,char *buf,unsigned int cnt)
{
	return scnprintf(buf,cnt,"handler: pvrusb2-video-v4l");
}


static const struct pvr2_i2c_handler_functions hfuncs = {
	.detach = (void (*)(void *))decoder_detach,
	.check = (int (*)(void *))decoder_check,
	.update = (void (*)(void *))decoder_update,
	.describe = (unsigned int (*)(void *,char *,unsigned int))decoder_describe,
};


int pvr2_i2c_decoder_v4l_setup(struct pvr2_hdw *hdw,
			       struct pvr2_i2c_client *cp)
{
	struct pvr2_v4l_decoder *ctxt;

	if (hdw->decoder_ctrl) return 0;
	if (cp->handler) return 0;
	if (!decoder_detect(cp)) return 0;

	ctxt = kzalloc(sizeof(*ctxt),GFP_KERNEL);
	if (!ctxt) return 0;

	ctxt->handler.func_data = ctxt;
	ctxt->handler.func_table = &hfuncs;
	ctxt->ctrl.ctxt = ctxt;
	ctxt->ctrl.detach = (void (*)(void *))decoder_detach;
	ctxt->ctrl.enable = (void (*)(void *,int))decoder_enable;
	ctxt->client = cp;
	ctxt->hdw = hdw;
	ctxt->stale_mask = (1 << ARRAY_SIZE(decoder_ops)) - 1;
	pvr2_hdw_set_decoder(hdw,&ctxt->ctrl);
	cp->handler = &ctxt->handler;
	pvr2_trace(PVR2_TRACE_CHIPS,"i2c 0x%x saa711x V4L2 handler set up",
		   cp->client->addr);
	return !0;
}




/*
  Stuff for Emacs to see, in order to encourage consistent editing style:
  *** Local Variables: ***
  *** mode: c ***
  *** fill-column: 70 ***
  *** tab-width: 8 ***
  *** c-basic-offset: 8 ***
  *** End: ***
  */
