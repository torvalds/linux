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

#include "pvrusb2.h"
#include "pvrusb2-util.h"
#include "pvrusb2-tuner.h"
#include "pvrusb2-hdw-internal.h"
#include "pvrusb2-debug.h"
#include <linux/videodev2.h>
#include <media/tuner.h>
#include <media/v4l2-common.h>

struct pvr2_tuner_handler {
	struct pvr2_hdw *hdw;
	struct pvr2_i2c_client *client;
	struct pvr2_i2c_handler i2c_handler;
	int type_update_fl;
};


static void set_type(struct pvr2_tuner_handler *ctxt)
{
	struct pvr2_hdw *hdw = ctxt->hdw;
	struct tuner_setup setup;
	pvr2_trace(PVR2_TRACE_CHIPS,"i2c tuner set_type(%d)",hdw->tuner_type);
	if (((int)(hdw->tuner_type)) < 0) return;

	setup.addr = ADDR_UNSET;
	setup.type = hdw->tuner_type;
	setup.mode_mask = T_RADIO | T_ANALOG_TV;
	/* We may really want mode_mask to be T_ANALOG_TV for now */
	pvr2_i2c_client_cmd(ctxt->client,TUNER_SET_TYPE_ADDR,&setup);
	ctxt->type_update_fl = 0;
}


static int tuner_check(struct pvr2_tuner_handler *ctxt)
{
	struct pvr2_hdw *hdw = ctxt->hdw;
	if (hdw->tuner_updated) ctxt->type_update_fl = !0;
	return ctxt->type_update_fl != 0;
}


static void tuner_update(struct pvr2_tuner_handler *ctxt)
{
	if (ctxt->type_update_fl) set_type(ctxt);
}


static void pvr2_tuner_detach(struct pvr2_tuner_handler *ctxt)
{
	ctxt->client->handler = NULL;
	kfree(ctxt);
}


static unsigned int pvr2_tuner_describe(struct pvr2_tuner_handler *ctxt,char *buf,unsigned int cnt)
{
	return scnprintf(buf,cnt,"handler: pvrusb2-tuner");
}


static const struct pvr2_i2c_handler_functions tuner_funcs = {
	.detach = (void (*)(void *))pvr2_tuner_detach,
	.check = (int (*)(void *))tuner_check,
	.update = (void (*)(void *))tuner_update,
	.describe = (unsigned int (*)(void *,char *,unsigned int))pvr2_tuner_describe,
};


int pvr2_i2c_tuner_setup(struct pvr2_hdw *hdw,struct pvr2_i2c_client *cp)
{
	struct pvr2_tuner_handler *ctxt;
	if (cp->handler) return 0;

	ctxt = kzalloc(sizeof(*ctxt),GFP_KERNEL);
	if (!ctxt) return 0;

	ctxt->i2c_handler.func_data = ctxt;
	ctxt->i2c_handler.func_table = &tuner_funcs;
	ctxt->type_update_fl = !0;
	ctxt->client = cp;
	ctxt->hdw = hdw;
	cp->handler = &ctxt->i2c_handler;
	pvr2_trace(PVR2_TRACE_CHIPS,"i2c 0x%x tuner handler set up",
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
