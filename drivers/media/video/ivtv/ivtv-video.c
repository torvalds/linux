/*
    saa7127 interface functions
    Copyright (C) 2004-2007  Hans Verkuil <hverkuil@xs4all.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ivtv-driver.h"
#include "ivtv-video.h"
#include "ivtv-i2c.h"
#include "ivtv-gpio.h"
#include "ivtv-cards.h"
#include <media/upd64031a.h>
#include <media/upd64083.h>

void ivtv_set_vps(struct ivtv *itv, int enabled, u8 vps1, u8 vps2, u8 vps3,
		  u8 vps4, u8 vps5)
{
	struct v4l2_sliced_vbi_data data;

	if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
		return;
	data.id = V4L2_SLICED_VPS;
	data.field = 0;
	data.line = enabled ? 16 : 0;
	data.data[4] = vps1;
	data.data[10] = vps2;
	data.data[11] = vps3;
	data.data[12] = vps4;
	data.data[13] = vps5;
	ivtv_saa7127(itv, VIDIOC_INT_S_VBI_DATA, &data);
}

void ivtv_set_cc(struct ivtv *itv, int mode, u8 cc1, u8 cc2, u8 cc3, u8 cc4)
{
	struct v4l2_sliced_vbi_data data;

	if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
		return;
	data.id = V4L2_SLICED_CAPTION_525;
	data.field = 0;
	data.line = (mode & 1) ? 21 : 0;
	data.data[0] = cc1;
	data.data[1] = cc2;
	ivtv_saa7127(itv, VIDIOC_INT_S_VBI_DATA, &data);
	data.field = 1;
	data.line = (mode & 2) ? 21 : 0;
	data.data[0] = cc3;
	data.data[1] = cc4;
	ivtv_saa7127(itv, VIDIOC_INT_S_VBI_DATA, &data);
}

void ivtv_set_wss(struct ivtv *itv, int enabled, int mode)
{
	struct v4l2_sliced_vbi_data data;

	if (!(itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT))
		return;
	/* When using a 50 Hz system, always turn on the
	   wide screen signal with 4x3 ratio as the default.
	   Turning this signal on and off can confuse certain
	   TVs. As far as I can tell there is no reason not to
	   transmit this signal. */
	if ((itv->std & V4L2_STD_625_50) && !enabled) {
		enabled = 1;
		mode = 0x08;  /* 4x3 full format */
	}
	data.id = V4L2_SLICED_WSS_625;
	data.field = 0;
	data.line = enabled ? 23 : 0;
	data.data[0] = mode & 0xff;
	data.data[1] = (mode >> 8) & 0xff;
	ivtv_saa7127(itv, VIDIOC_INT_S_VBI_DATA, &data);
}

void ivtv_video_set_io(struct ivtv *itv)
{
	struct v4l2_routing route;
	int inp = itv->active_input;
	u32 type;

	route.input = itv->card->video_inputs[inp].video_input;
	route.output = 0;
	itv->video_dec_func(itv, VIDIOC_INT_S_VIDEO_ROUTING, &route);

	type = itv->card->video_inputs[inp].video_type;

	if (type == IVTV_CARD_INPUT_VID_TUNER) {
		route.input = 0;  /* Tuner */
	} else if (type < IVTV_CARD_INPUT_COMPOSITE1) {
		route.input = 2;  /* S-Video */
	} else {
		route.input = 1;  /* Composite */
	}

	if (itv->card->hw_video & IVTV_HW_GPIO)
		ivtv_gpio(itv, VIDIOC_INT_S_VIDEO_ROUTING, &route);

	if (itv->card->hw_video & IVTV_HW_UPD64031A) {
		if (type == IVTV_CARD_INPUT_VID_TUNER ||
		    type >= IVTV_CARD_INPUT_COMPOSITE1) {
			/* Composite: GR on, connect to 3DYCS */
			route.input = UPD64031A_GR_ON | UPD64031A_3DYCS_COMPOSITE;
		} else {
			/* S-Video: GR bypassed, turn it off */
			route.input = UPD64031A_GR_OFF | UPD64031A_3DYCS_DISABLE;
		}
		route.input |= itv->card->gr_config;

		ivtv_upd64031a(itv, VIDIOC_INT_S_VIDEO_ROUTING, &route);
	}

	if (itv->card->hw_video & IVTV_HW_UPD6408X) {
		route.input = UPD64083_YCS_MODE;
		if (type > IVTV_CARD_INPUT_VID_TUNER &&
		    type < IVTV_CARD_INPUT_COMPOSITE1) {
			/* S-Video uses YCNR mode and internal Y-ADC, the upd64031a
			   is not used. */
			route.input |= UPD64083_YCNR_MODE;
		}
		else if (itv->card->hw_video & IVTV_HW_UPD64031A) {
		  /* Use upd64031a output for tuner and composite(CX23416GYC only) inputs */
		  if ((type == IVTV_CARD_INPUT_VID_TUNER)||
		      (itv->card->type == IVTV_CARD_CX23416GYC)) {
		    route.input |= UPD64083_EXT_Y_ADC;
		  }
		}
		ivtv_upd64083(itv, VIDIOC_INT_S_VIDEO_ROUTING, &route);
	}
}
