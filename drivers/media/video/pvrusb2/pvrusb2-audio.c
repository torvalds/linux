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

#include "pvrusb2-audio.h"
#include "pvrusb2-hdw-internal.h"
#include "pvrusb2-debug.h"
#include <linux/videodev2.h>
#include <media/msp3400.h>
#include <media/v4l2-common.h>

struct pvr2_msp3400_handler {
	struct pvr2_hdw *hdw;
	struct pvr2_i2c_client *client;
	struct pvr2_i2c_handler i2c_handler;
	struct pvr2_audio_stat astat;
	unsigned long stale_mask;
};


/* This function selects the correct audio input source */
static void set_stereo(struct pvr2_msp3400_handler *ctxt)
{
	struct pvr2_hdw *hdw = ctxt->hdw;
	struct v4l2_routing route;

	pvr2_trace(PVR2_TRACE_CHIPS,"i2c msp3400 v4l2 set_stereo");

	if (hdw->input_val == PVR2_CVAL_INPUT_TV) {
		struct v4l2_tuner vt;
		memset(&vt,0,sizeof(vt));
		vt.audmode = hdw->audiomode_val;
		pvr2_i2c_client_cmd(ctxt->client,VIDIOC_S_TUNER,&vt);
	}

	route.input = MSP_INPUT_DEFAULT;
	route.output = MSP_OUTPUT(MSP_SC_IN_DSP_SCART1);
	switch (hdw->input_val) {
	case PVR2_CVAL_INPUT_TV:
		break;
	case PVR2_CVAL_INPUT_RADIO:
		/* Assume that msp34xx also handle FM decoding, in which case
		   we're still using the tuner. */
		/* HV: actually it is more likely to be the SCART2 input if
		   the ivtv experience is any indication. */
		route.input = MSP_INPUT(MSP_IN_SCART2, MSP_IN_TUNER1,
				    MSP_DSP_IN_SCART, MSP_DSP_IN_SCART);
		break;
	case PVR2_CVAL_INPUT_SVIDEO:
	case PVR2_CVAL_INPUT_COMPOSITE:
		/* SCART 1 input */
		route.input = MSP_INPUT(MSP_IN_SCART1, MSP_IN_TUNER1,
				    MSP_DSP_IN_SCART, MSP_DSP_IN_SCART);
		break;
	}
	pvr2_i2c_client_cmd(ctxt->client,VIDIOC_INT_S_AUDIO_ROUTING,&route);
}


static int check_stereo(struct pvr2_msp3400_handler *ctxt)
{
	struct pvr2_hdw *hdw = ctxt->hdw;
	return (hdw->input_dirty ||
		hdw->audiomode_dirty);
}


struct pvr2_msp3400_ops {
	void (*update)(struct pvr2_msp3400_handler *);
	int (*check)(struct pvr2_msp3400_handler *);
};


static const struct pvr2_msp3400_ops msp3400_ops[] = {
	{ .update = set_stereo, .check = check_stereo},
};


static int msp3400_check(struct pvr2_msp3400_handler *ctxt)
{
	unsigned long msk;
	unsigned int idx;

	for (idx = 0; idx < sizeof(msp3400_ops)/sizeof(msp3400_ops[0]);
	     idx++) {
		msk = 1 << idx;
		if (ctxt->stale_mask & msk) continue;
		if (msp3400_ops[idx].check(ctxt)) {
			ctxt->stale_mask |= msk;
		}
	}
	return ctxt->stale_mask != 0;
}


static void msp3400_update(struct pvr2_msp3400_handler *ctxt)
{
	unsigned long msk;
	unsigned int idx;

	for (idx = 0; idx < sizeof(msp3400_ops)/sizeof(msp3400_ops[0]);
	     idx++) {
		msk = 1 << idx;
		if (!(ctxt->stale_mask & msk)) continue;
		ctxt->stale_mask &= ~msk;
		msp3400_ops[idx].update(ctxt);
	}
}


/* This reads back the current signal type */
static int get_audio_status(struct pvr2_msp3400_handler *ctxt)
{
	struct v4l2_tuner vt;
	int stat;

	memset(&vt,0,sizeof(vt));
	stat = pvr2_i2c_client_cmd(ctxt->client,VIDIOC_G_TUNER,&vt);
	if (stat < 0) return stat;

	ctxt->hdw->flag_stereo = (vt.audmode & V4L2_TUNER_MODE_STEREO) != 0;
	ctxt->hdw->flag_bilingual =
		(vt.audmode & V4L2_TUNER_MODE_LANG2) != 0;
	return 0;
}


static void pvr2_msp3400_detach(struct pvr2_msp3400_handler *ctxt)
{
	ctxt->client->handler = NULL;
	ctxt->hdw->audio_stat = NULL;
	kfree(ctxt);
}


static unsigned int pvr2_msp3400_describe(struct pvr2_msp3400_handler *ctxt,
					  char *buf,unsigned int cnt)
{
	return scnprintf(buf,cnt,"handler: pvrusb2-audio v4l2");
}


const static struct pvr2_i2c_handler_functions msp3400_funcs = {
	.detach = (void (*)(void *))pvr2_msp3400_detach,
	.check = (int (*)(void *))msp3400_check,
	.update = (void (*)(void *))msp3400_update,
	.describe = (unsigned int (*)(void *,char *,unsigned int))pvr2_msp3400_describe,
};


int pvr2_i2c_msp3400_setup(struct pvr2_hdw *hdw,struct pvr2_i2c_client *cp)
{
	struct pvr2_msp3400_handler *ctxt;
	if (hdw->audio_stat) return 0;
	if (cp->handler) return 0;

	ctxt = kmalloc(sizeof(*ctxt),GFP_KERNEL);
	if (!ctxt) return 0;
	memset(ctxt,0,sizeof(*ctxt));

	ctxt->i2c_handler.func_data = ctxt;
	ctxt->i2c_handler.func_table = &msp3400_funcs;
	ctxt->client = cp;
	ctxt->hdw = hdw;
	ctxt->astat.ctxt = ctxt;
	ctxt->astat.status = (int (*)(void *))get_audio_status;
	ctxt->astat.detach = (void (*)(void *))pvr2_msp3400_detach;
	ctxt->stale_mask = (1 << (sizeof(msp3400_ops)/
				  sizeof(msp3400_ops[0]))) - 1;
	cp->handler = &ctxt->i2c_handler;
	hdw->audio_stat = &ctxt->astat;
	pvr2_trace(PVR2_TRACE_CHIPS,"i2c 0x%x msp3400 V4L2 handler set up",
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
