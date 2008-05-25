/*
 *
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
   wm8775.

*/

#include "pvrusb2-wm8775.h"
#include "pvrusb2-i2c-cmd-v4l2.h"


#include "pvrusb2-hdw-internal.h"
#include "pvrusb2-debug.h"
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <linux/errno.h>
#include <linux/slab.h>

struct pvr2_v4l_wm8775 {
	struct pvr2_i2c_handler handler;
	struct pvr2_i2c_client *client;
	struct pvr2_hdw *hdw;
	unsigned long stale_mask;
};


static void set_input(struct pvr2_v4l_wm8775 *ctxt)
{
	struct v4l2_routing route;
	struct pvr2_hdw *hdw = ctxt->hdw;

	memset(&route,0,sizeof(route));

	switch(hdw->input_val) {
	case PVR2_CVAL_INPUT_RADIO:
		route.input = 1;
		break;
	default:
		/* All other cases just use the second input */
		route.input = 2;
		break;
	}
	pvr2_trace(PVR2_TRACE_CHIPS,"i2c wm8775 set_input(val=%d route=0x%x)",
		   hdw->input_val,route.input);

	pvr2_i2c_client_cmd(ctxt->client,VIDIOC_INT_S_AUDIO_ROUTING,&route);
}

static int check_input(struct pvr2_v4l_wm8775 *ctxt)
{
	struct pvr2_hdw *hdw = ctxt->hdw;
	return hdw->input_dirty != 0;
}


struct pvr2_v4l_wm8775_ops {
	void (*update)(struct pvr2_v4l_wm8775 *);
	int (*check)(struct pvr2_v4l_wm8775 *);
};


static const struct pvr2_v4l_wm8775_ops wm8775_ops[] = {
	{ .update = set_input, .check = check_input},
};


static unsigned int wm8775_describe(struct pvr2_v4l_wm8775 *ctxt,
				     char *buf,unsigned int cnt)
{
	return scnprintf(buf,cnt,"handler: pvrusb2-wm8775");
}


static void wm8775_detach(struct pvr2_v4l_wm8775 *ctxt)
{
	ctxt->client->handler = NULL;
	kfree(ctxt);
}


static int wm8775_check(struct pvr2_v4l_wm8775 *ctxt)
{
	unsigned long msk;
	unsigned int idx;

	for (idx = 0; idx < ARRAY_SIZE(wm8775_ops); idx++) {
		msk = 1 << idx;
		if (ctxt->stale_mask & msk) continue;
		if (wm8775_ops[idx].check(ctxt)) {
			ctxt->stale_mask |= msk;
		}
	}
	return ctxt->stale_mask != 0;
}


static void wm8775_update(struct pvr2_v4l_wm8775 *ctxt)
{
	unsigned long msk;
	unsigned int idx;

	for (idx = 0; idx < ARRAY_SIZE(wm8775_ops); idx++) {
		msk = 1 << idx;
		if (!(ctxt->stale_mask & msk)) continue;
		ctxt->stale_mask &= ~msk;
		wm8775_ops[idx].update(ctxt);
	}
}


static const struct pvr2_i2c_handler_functions hfuncs = {
	.detach = (void (*)(void *))wm8775_detach,
	.check = (int (*)(void *))wm8775_check,
	.update = (void (*)(void *))wm8775_update,
	.describe = (unsigned int (*)(void *,char *,unsigned int))wm8775_describe,
};


int pvr2_i2c_wm8775_setup(struct pvr2_hdw *hdw,struct pvr2_i2c_client *cp)
{
	struct pvr2_v4l_wm8775 *ctxt;

	if (cp->handler) return 0;

	ctxt = kzalloc(sizeof(*ctxt),GFP_KERNEL);
	if (!ctxt) return 0;

	ctxt->handler.func_data = ctxt;
	ctxt->handler.func_table = &hfuncs;
	ctxt->client = cp;
	ctxt->hdw = hdw;
	ctxt->stale_mask = (1 << ARRAY_SIZE(wm8775_ops)) - 1;
	cp->handler = &ctxt->handler;
	pvr2_trace(PVR2_TRACE_CHIPS,"i2c 0x%x wm8775 V4L2 handler set up",
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
