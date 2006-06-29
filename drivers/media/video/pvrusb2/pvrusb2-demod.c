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
#include "pvrusb2-demod.h"
#include "pvrusb2-hdw-internal.h"
#include "pvrusb2-debug.h"
#include <linux/videodev2.h>
#include <media/tuner.h>
#include <media/v4l2-common.h>


struct pvr2_demod_handler {
	struct pvr2_hdw *hdw;
	struct pvr2_i2c_client *client;
	struct pvr2_i2c_handler i2c_handler;
	int type_update_fl;
};


static void set_config(struct pvr2_demod_handler *ctxt)
{
	struct pvr2_hdw *hdw = ctxt->hdw;
	int cfg = 0;

	switch (hdw->tuner_type) {
	case TUNER_PHILIPS_FM1216ME_MK3:
	case TUNER_PHILIPS_FM1236_MK3:
		cfg = TDA9887_PORT1_ACTIVE|TDA9887_PORT2_ACTIVE;
		break;
	default:
		break;
	}
	pvr2_trace(PVR2_TRACE_CHIPS,"i2c demod set_config(0x%x)",cfg);
	pvr2_i2c_client_cmd(ctxt->client,TDA9887_SET_CONFIG,&cfg);
	ctxt->type_update_fl = 0;
}


static int demod_check(struct pvr2_demod_handler *ctxt)
{
	struct pvr2_hdw *hdw = ctxt->hdw;
	if (hdw->tuner_updated) ctxt->type_update_fl = !0;
	return ctxt->type_update_fl != 0;
}


static void demod_update(struct pvr2_demod_handler *ctxt)
{
	if (ctxt->type_update_fl) set_config(ctxt);
}


static void demod_detach(struct pvr2_demod_handler *ctxt)
{
	ctxt->client->handler = 0;
	kfree(ctxt);
}


static unsigned int demod_describe(struct pvr2_demod_handler *ctxt,char *buf,unsigned int cnt)
{
	return scnprintf(buf,cnt,"handler: pvrusb2-demod");
}


const static struct pvr2_i2c_handler_functions tuner_funcs = {
	.detach = (void (*)(void *))demod_detach,
	.check = (int (*)(void *))demod_check,
	.update = (void (*)(void *))demod_update,
	.describe = (unsigned int (*)(void *,char *,unsigned int))demod_describe,
};


int pvr2_i2c_demod_setup(struct pvr2_hdw *hdw,struct pvr2_i2c_client *cp)
{
	struct pvr2_demod_handler *ctxt;
	if (cp->handler) return 0;

	ctxt = kmalloc(sizeof(*ctxt),GFP_KERNEL);
	if (!ctxt) return 0;
	memset(ctxt,0,sizeof(*ctxt));

	ctxt->i2c_handler.func_data = ctxt;
	ctxt->i2c_handler.func_table = &tuner_funcs;
	ctxt->type_update_fl = !0;
	ctxt->client = cp;
	ctxt->hdw = hdw;
	cp->handler = &ctxt->i2c_handler;
	pvr2_trace(PVR2_TRACE_CHIPS,"i2c 0x%x tda9887 V4L2 handler set up",
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
