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

#include "pvrusb2-i2c-cmd-v4l2.h"
#include "pvrusb2-hdw-internal.h"
#include "pvrusb2-debug.h"
#include <linux/videodev2.h>
#include <media/v4l2-common.h>

static void set_standard(struct pvr2_hdw *hdw)
{
	pvr2_trace(PVR2_TRACE_CHIPS,"i2c v4l2 set_standard");

	if (hdw->input_val == PVR2_CVAL_INPUT_RADIO) {
		pvr2_i2c_core_cmd(hdw,AUDC_SET_RADIO,NULL);
	} else {
		v4l2_std_id vs;
		vs = hdw->std_mask_cur;
		pvr2_i2c_core_cmd(hdw,VIDIOC_S_STD,&vs);
	}
	hdw->tuner_signal_stale = !0;
}


static int check_standard(struct pvr2_hdw *hdw)
{
	return (hdw->input_dirty != 0) || (hdw->std_dirty != 0);
}


const struct pvr2_i2c_op pvr2_i2c_op_v4l2_standard = {
	.check = check_standard,
	.update = set_standard,
	.name = "v4l2_standard",
};


static void set_bcsh(struct pvr2_hdw *hdw)
{
	struct v4l2_control ctrl;
	pvr2_trace(PVR2_TRACE_CHIPS,"i2c v4l2 set_bcsh"
		   " b=%d c=%d s=%d h=%d",
		   hdw->brightness_val,hdw->contrast_val,
		   hdw->saturation_val,hdw->hue_val);
	memset(&ctrl,0,sizeof(ctrl));
	ctrl.id = V4L2_CID_BRIGHTNESS;
	ctrl.value = hdw->brightness_val;
	pvr2_i2c_core_cmd(hdw,VIDIOC_S_CTRL,&ctrl);
	ctrl.id = V4L2_CID_CONTRAST;
	ctrl.value = hdw->contrast_val;
	pvr2_i2c_core_cmd(hdw,VIDIOC_S_CTRL,&ctrl);
	ctrl.id = V4L2_CID_SATURATION;
	ctrl.value = hdw->saturation_val;
	pvr2_i2c_core_cmd(hdw,VIDIOC_S_CTRL,&ctrl);
	ctrl.id = V4L2_CID_HUE;
	ctrl.value = hdw->hue_val;
	pvr2_i2c_core_cmd(hdw,VIDIOC_S_CTRL,&ctrl);
}


static int check_bcsh(struct pvr2_hdw *hdw)
{
	return (hdw->brightness_dirty ||
		hdw->contrast_dirty ||
		hdw->saturation_dirty ||
		hdw->hue_dirty);
}


const struct pvr2_i2c_op pvr2_i2c_op_v4l2_bcsh = {
	.check = check_bcsh,
	.update = set_bcsh,
	.name = "v4l2_bcsh",
};


static void set_volume(struct pvr2_hdw *hdw)
{
	struct v4l2_control ctrl;
	pvr2_trace(PVR2_TRACE_CHIPS,
		   "i2c v4l2 set_volume"
		   "(vol=%d bal=%d bas=%d treb=%d mute=%d)",
		   hdw->volume_val,
		   hdw->balance_val,
		   hdw->bass_val,
		   hdw->treble_val,
		   hdw->mute_val);
	memset(&ctrl,0,sizeof(ctrl));
	ctrl.id = V4L2_CID_AUDIO_MUTE;
	ctrl.value = hdw->mute_val ? 1 : 0;
	pvr2_i2c_core_cmd(hdw,VIDIOC_S_CTRL,&ctrl);
	ctrl.id = V4L2_CID_AUDIO_VOLUME;
	ctrl.value = hdw->volume_val;
	pvr2_i2c_core_cmd(hdw,VIDIOC_S_CTRL,&ctrl);
	ctrl.id = V4L2_CID_AUDIO_BALANCE;
	ctrl.value = hdw->balance_val;
	pvr2_i2c_core_cmd(hdw,VIDIOC_S_CTRL,&ctrl);
	ctrl.id = V4L2_CID_AUDIO_BASS;
	ctrl.value = hdw->bass_val;
	pvr2_i2c_core_cmd(hdw,VIDIOC_S_CTRL,&ctrl);
	ctrl.id = V4L2_CID_AUDIO_TREBLE;
	ctrl.value = hdw->treble_val;
	pvr2_i2c_core_cmd(hdw,VIDIOC_S_CTRL,&ctrl);
}


static int check_volume(struct pvr2_hdw *hdw)
{
	return (hdw->volume_dirty ||
		hdw->balance_dirty ||
		hdw->bass_dirty ||
		hdw->treble_dirty ||
		hdw->mute_dirty);
}


const struct pvr2_i2c_op pvr2_i2c_op_v4l2_volume = {
	.check = check_volume,
	.update = set_volume,
	.name = "v4l2_volume",
};


static void set_audiomode(struct pvr2_hdw *hdw)
{
	struct v4l2_tuner vt;
	memset(&vt,0,sizeof(vt));
	vt.audmode = hdw->audiomode_val;
	pvr2_i2c_core_cmd(hdw,VIDIOC_S_TUNER,&vt);
}


static int check_audiomode(struct pvr2_hdw *hdw)
{
	return (hdw->input_dirty ||
		hdw->audiomode_dirty);
}


const struct pvr2_i2c_op pvr2_i2c_op_v4l2_audiomode = {
	.check = check_audiomode,
	.update = set_audiomode,
	.name = "v4l2_audiomode",
};


static void set_frequency(struct pvr2_hdw *hdw)
{
	unsigned long fv;
	struct v4l2_frequency freq;
	fv = pvr2_hdw_get_cur_freq(hdw);
	pvr2_trace(PVR2_TRACE_CHIPS,"i2c v4l2 set_freq(%lu)",fv);
	if (hdw->tuner_signal_stale) {
		pvr2_i2c_core_status_poll(hdw);
	}
	memset(&freq,0,sizeof(freq));
	if (hdw->tuner_signal_info.capability & V4L2_TUNER_CAP_LOW) {
		// ((fv * 1000) / 62500)
		freq.frequency = (fv * 2) / 125;
	} else {
		freq.frequency = fv / 62500;
	}
	/* tuner-core currently doesn't seem to care about this, but
	   let's set it anyway for completeness. */
	if (hdw->input_val == PVR2_CVAL_INPUT_RADIO) {
		freq.type = V4L2_TUNER_RADIO;
	} else {
		freq.type = V4L2_TUNER_ANALOG_TV;
	}
	freq.tuner = 0;
	pvr2_i2c_core_cmd(hdw,VIDIOC_S_FREQUENCY,&freq);
}


static int check_frequency(struct pvr2_hdw *hdw)
{
	return hdw->freqDirty != 0;
}


const struct pvr2_i2c_op pvr2_i2c_op_v4l2_frequency = {
	.check = check_frequency,
	.update = set_frequency,
	.name = "v4l2_freq",
};


static void set_size(struct pvr2_hdw *hdw)
{
	struct v4l2_format fmt;

	memset(&fmt,0,sizeof(fmt));

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = hdw->res_hor_val;
	fmt.fmt.pix.height = hdw->res_ver_val;

	pvr2_trace(PVR2_TRACE_CHIPS,"i2c v4l2 set_size(%dx%d)",
			   fmt.fmt.pix.width,fmt.fmt.pix.height);

	pvr2_i2c_core_cmd(hdw,VIDIOC_S_FMT,&fmt);
}


static int check_size(struct pvr2_hdw *hdw)
{
	return (hdw->res_hor_dirty || hdw->res_ver_dirty);
}


const struct pvr2_i2c_op pvr2_i2c_op_v4l2_size = {
	.check = check_size,
	.update = set_size,
	.name = "v4l2_size",
};


static void do_log(struct pvr2_hdw *hdw)
{
	pvr2_trace(PVR2_TRACE_CHIPS,"i2c v4l2 do_log()");
	pvr2_i2c_core_cmd(hdw,VIDIOC_LOG_STATUS,NULL);

}


static int check_log(struct pvr2_hdw *hdw)
{
	return hdw->log_requested != 0;
}


const struct pvr2_i2c_op pvr2_i2c_op_v4l2_log = {
	.check = check_log,
	.update = do_log,
	.name = "v4l2_log",
};


void pvr2_v4l2_cmd_stream(struct pvr2_i2c_client *cp,int fl)
{
	pvr2_i2c_client_cmd(cp,
			    (fl ? VIDIOC_STREAMON : VIDIOC_STREAMOFF),NULL);
}


void pvr2_v4l2_cmd_status_poll(struct pvr2_i2c_client *cp)
{
	pvr2_i2c_client_cmd(cp,VIDIOC_G_TUNER,&cp->hdw->tuner_signal_info);
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
