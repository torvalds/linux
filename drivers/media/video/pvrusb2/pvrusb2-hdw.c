/*
 *
 *  $Id$
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
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

#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <asm/semaphore.h>
#include "pvrusb2.h"
#include "pvrusb2-std.h"
#include "pvrusb2-util.h"
#include "pvrusb2-hdw.h"
#include "pvrusb2-i2c-core.h"
#include "pvrusb2-tuner.h"
#include "pvrusb2-eeprom.h"
#include "pvrusb2-hdw-internal.h"
#include "pvrusb2-encoder.h"
#include "pvrusb2-debug.h"

struct usb_device_id pvr2_device_table[] = {
	[PVR2_HDW_TYPE_29XXX] = { USB_DEVICE(0x2040, 0x2900) },
	[PVR2_HDW_TYPE_24XXX] = { USB_DEVICE(0x2040, 0x2400) },
	{ }
};

MODULE_DEVICE_TABLE(usb, pvr2_device_table);

static const char *pvr2_device_names[] = {
	[PVR2_HDW_TYPE_29XXX] = "WinTV PVR USB2 Model Category 29xxxx",
	[PVR2_HDW_TYPE_24XXX] = "WinTV PVR USB2 Model Category 24xxxx",
};

struct pvr2_string_table {
	const char **lst;
	unsigned int cnt;
};

// Names of other client modules to request for 24xxx model hardware
static const char *pvr2_client_24xxx[] = {
	"cx25840",
	"tuner",
	"wm8775",
};

// Names of other client modules to request for 29xxx model hardware
static const char *pvr2_client_29xxx[] = {
	"msp3400",
	"saa7115",
	"tuner",
};

static struct pvr2_string_table pvr2_client_lists[] = {
	[PVR2_HDW_TYPE_29XXX] = {
		pvr2_client_29xxx,
		sizeof(pvr2_client_29xxx)/sizeof(pvr2_client_29xxx[0]),
	},
	[PVR2_HDW_TYPE_24XXX] = {
		pvr2_client_24xxx,
		sizeof(pvr2_client_24xxx)/sizeof(pvr2_client_24xxx[0]),
	},
};

static struct pvr2_hdw *unit_pointers[PVR_NUM] = {[ 0 ... PVR_NUM-1 ] = NULL};
static DECLARE_MUTEX(pvr2_unit_sem);

static int ctlchg = 0;
static int initusbreset = 1;
static int procreload = 0;
static int tuner[PVR_NUM] = { [0 ... PVR_NUM-1] = -1 };
static int tolerance[PVR_NUM] = { [0 ... PVR_NUM-1] = 0 };
static int video_std[PVR_NUM] = { [0 ... PVR_NUM-1] = 0 };
static int init_pause_msec = 0;

module_param(ctlchg, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ctlchg, "0=optimize ctl change 1=always accept new ctl value");
module_param(init_pause_msec, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(init_pause_msec, "hardware initialization settling delay");
module_param(initusbreset, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(initusbreset, "Do USB reset device on probe");
module_param(procreload, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(procreload,
		 "Attempt init failure recovery with firmware reload");
module_param_array(tuner,    int, NULL, 0444);
MODULE_PARM_DESC(tuner,"specify installed tuner type");
module_param_array(video_std,    int, NULL, 0444);
MODULE_PARM_DESC(video_std,"specify initial video standard");
module_param_array(tolerance,    int, NULL, 0444);
MODULE_PARM_DESC(tolerance,"specify stream error tolerance");

#define PVR2_CTL_WRITE_ENDPOINT  0x01
#define PVR2_CTL_READ_ENDPOINT   0x81

#define PVR2_GPIO_IN 0x9008
#define PVR2_GPIO_OUT 0x900c
#define PVR2_GPIO_DIR 0x9020

#define trace_firmware(...) pvr2_trace(PVR2_TRACE_FIRMWARE,__VA_ARGS__)

#define PVR2_FIRMWARE_ENDPOINT   0x02

/* size of a firmware chunk */
#define FIRMWARE_CHUNK_SIZE 0x2000

/* Define the list of additional controls we'll dynamically construct based
   on query of the cx2341x module. */
struct pvr2_mpeg_ids {
	const char *strid;
	int id;
};
static const struct pvr2_mpeg_ids mpeg_ids[] = {
	{
		.strid = "audio_layer",
		.id = V4L2_CID_MPEG_AUDIO_ENCODING,
	},{
		.strid = "audio_bitrate",
		.id = V4L2_CID_MPEG_AUDIO_L2_BITRATE,
	},{
		/* Already using audio_mode elsewhere :-( */
		.strid = "mpeg_audio_mode",
		.id = V4L2_CID_MPEG_AUDIO_MODE,
	},{
		.strid = "mpeg_audio_mode_extension",
		.id = V4L2_CID_MPEG_AUDIO_MODE_EXTENSION,
	},{
		.strid = "audio_emphasis",
		.id = V4L2_CID_MPEG_AUDIO_EMPHASIS,
	},{
		.strid = "audio_crc",
		.id = V4L2_CID_MPEG_AUDIO_CRC,
	},{
		.strid = "video_aspect",
		.id = V4L2_CID_MPEG_VIDEO_ASPECT,
	},{
		.strid = "video_b_frames",
		.id = V4L2_CID_MPEG_VIDEO_B_FRAMES,
	},{
		.strid = "video_gop_size",
		.id = V4L2_CID_MPEG_VIDEO_GOP_SIZE,
	},{
		.strid = "video_gop_closure",
		.id = V4L2_CID_MPEG_VIDEO_GOP_CLOSURE,
	},{
		.strid = "video_pulldown",
		.id = V4L2_CID_MPEG_VIDEO_PULLDOWN,
	},{
		.strid = "video_bitrate_mode",
		.id = V4L2_CID_MPEG_VIDEO_BITRATE_MODE,
	},{
		.strid = "video_bitrate",
		.id = V4L2_CID_MPEG_VIDEO_BITRATE,
	},{
		.strid = "video_bitrate_peak",
		.id = V4L2_CID_MPEG_VIDEO_BITRATE_PEAK,
	},{
		.strid = "video_temporal_decimation",
		.id = V4L2_CID_MPEG_VIDEO_TEMPORAL_DECIMATION,
	},{
		.strid = "stream_type",
		.id = V4L2_CID_MPEG_STREAM_TYPE,
	},{
		.strid = "video_spatial_filter_mode",
		.id = V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE,
	},{
		.strid = "video_spatial_filter",
		.id = V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER,
	},{
		.strid = "video_luma_spatial_filter_type",
		.id = V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE,
	},{
		.strid = "video_chroma_spatial_filter_type",
		.id = V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE,
	},{
		.strid = "video_temporal_filter_mode",
		.id = V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE,
	},{
		.strid = "video_temporal_filter",
		.id = V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER,
	},{
		.strid = "video_median_filter_type",
		.id = V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE,
	},{
		.strid = "video_luma_median_filter_top",
		.id = V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_TOP,
	},{
		.strid = "video_luma_median_filter_bottom",
		.id = V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_BOTTOM,
	},{
		.strid = "video_chroma_median_filter_top",
		.id = V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_TOP,
	},{
		.strid = "video_chroma_median_filter_bottom",
		.id = V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_BOTTOM,
	}
};
#define MPEGDEF_COUNT (sizeof(mpeg_ids)/sizeof(mpeg_ids[0]))


static const char *control_values_srate[] = {
	[V4L2_MPEG_AUDIO_SAMPLING_FREQ_44100]   = "44.1 kHz",
	[V4L2_MPEG_AUDIO_SAMPLING_FREQ_48000]   = "48 kHz",
	[V4L2_MPEG_AUDIO_SAMPLING_FREQ_32000]   = "32 kHz",
};



static const char *control_values_input[] = {
	[PVR2_CVAL_INPUT_TV]        = "television",  /*xawtv needs this name*/
	[PVR2_CVAL_INPUT_RADIO]     = "radio",
	[PVR2_CVAL_INPUT_SVIDEO]    = "s-video",
	[PVR2_CVAL_INPUT_COMPOSITE] = "composite",
};


static const char *control_values_audiomode[] = {
	[V4L2_TUNER_MODE_MONO]   = "Mono",
	[V4L2_TUNER_MODE_STEREO] = "Stereo",
	[V4L2_TUNER_MODE_LANG1]  = "Lang1",
	[V4L2_TUNER_MODE_LANG2]  = "Lang2",
	[V4L2_TUNER_MODE_LANG1_LANG2] = "Lang1+Lang2",
};


static const char *control_values_hsm[] = {
	[PVR2_CVAL_HSM_FAIL] = "Fail",
	[PVR2_CVAL_HSM_HIGH] = "High",
	[PVR2_CVAL_HSM_FULL] = "Full",
};


static const char *control_values_subsystem[] = {
	[PVR2_SUBSYS_B_ENC_FIRMWARE]  = "enc_firmware",
	[PVR2_SUBSYS_B_ENC_CFG] = "enc_config",
	[PVR2_SUBSYS_B_DIGITIZER_RUN] = "digitizer_run",
	[PVR2_SUBSYS_B_USBSTREAM_RUN] = "usbstream_run",
	[PVR2_SUBSYS_B_ENC_RUN] = "enc_run",
};

static int pvr2_hdw_cmd_usbstream(struct pvr2_hdw *hdw,int runFl);
static int pvr2_hdw_commit_ctl_internal(struct pvr2_hdw *hdw);
static int pvr2_hdw_get_eeprom_addr(struct pvr2_hdw *hdw);
static unsigned int pvr2_hdw_get_signal_status_internal(struct pvr2_hdw *hdw);
static void pvr2_hdw_internal_find_stdenum(struct pvr2_hdw *hdw);
static void pvr2_hdw_internal_set_std_avail(struct pvr2_hdw *hdw);
static void pvr2_hdw_render_useless_unlocked(struct pvr2_hdw *hdw);
static void pvr2_hdw_subsys_bit_chg_no_lock(struct pvr2_hdw *hdw,
					    unsigned long msk,
					    unsigned long val);
static void pvr2_hdw_subsys_stream_bit_chg_no_lock(struct pvr2_hdw *hdw,
						   unsigned long msk,
						   unsigned long val);
static int pvr2_send_request_ex(struct pvr2_hdw *hdw,
				unsigned int timeout,int probe_fl,
				void *write_data,unsigned int write_len,
				void *read_data,unsigned int read_len);
static int pvr2_write_u16(struct pvr2_hdw *hdw, u16 data, int res);
static int pvr2_write_u8(struct pvr2_hdw *hdw, u8 data, int res);

static int ctrl_channelfreq_get(struct pvr2_ctrl *cptr,int *vp)
{
	struct pvr2_hdw *hdw = cptr->hdw;
	if ((hdw->freqProgSlot > 0) && (hdw->freqProgSlot <= FREQTABLE_SIZE)) {
		*vp = hdw->freqTable[hdw->freqProgSlot-1];
	} else {
		*vp = 0;
	}
	return 0;
}

static int ctrl_channelfreq_set(struct pvr2_ctrl *cptr,int m,int v)
{
	struct pvr2_hdw *hdw = cptr->hdw;
	if ((hdw->freqProgSlot > 0) && (hdw->freqProgSlot <= FREQTABLE_SIZE)) {
		hdw->freqTable[hdw->freqProgSlot-1] = v;
	}
	return 0;
}

static int ctrl_channelprog_get(struct pvr2_ctrl *cptr,int *vp)
{
	*vp = cptr->hdw->freqProgSlot;
	return 0;
}

static int ctrl_channelprog_set(struct pvr2_ctrl *cptr,int m,int v)
{
	struct pvr2_hdw *hdw = cptr->hdw;
	if ((v >= 0) && (v <= FREQTABLE_SIZE)) {
		hdw->freqProgSlot = v;
	}
	return 0;
}

static int ctrl_channel_get(struct pvr2_ctrl *cptr,int *vp)
{
	*vp = cptr->hdw->freqSlot;
	return 0;
}

static int ctrl_channel_set(struct pvr2_ctrl *cptr,int m,int v)
{
	unsigned freq = 0;
	struct pvr2_hdw *hdw = cptr->hdw;
	hdw->freqSlot = v;
	if ((hdw->freqSlot > 0) && (hdw->freqSlot <= FREQTABLE_SIZE)) {
		freq = hdw->freqTable[hdw->freqSlot-1];
	}
	if (freq && (freq != hdw->freqVal)) {
		hdw->freqVal = freq;
		hdw->freqDirty = !0;
	}
	return 0;
}

static int ctrl_freq_get(struct pvr2_ctrl *cptr,int *vp)
{
	*vp = cptr->hdw->freqVal;
	return 0;
}

static int ctrl_freq_is_dirty(struct pvr2_ctrl *cptr)
{
	return cptr->hdw->freqDirty != 0;
}

static void ctrl_freq_clear_dirty(struct pvr2_ctrl *cptr)
{
	cptr->hdw->freqDirty = 0;
}

static int ctrl_freq_set(struct pvr2_ctrl *cptr,int m,int v)
{
	struct pvr2_hdw *hdw = cptr->hdw;
	hdw->freqVal = v;
	hdw->freqDirty = !0;
	hdw->freqSlot = 0;
	return 0;
}

static int ctrl_hres_max_get(struct pvr2_ctrl *cptr,int *vp)
{
	/* If we're dealing with a 24xxx device, force the horizontal
	   maximum to be 720 no matter what, since we can't get the device
	   to work properly with any other value.  Otherwise just return
	   the normal value. */
	*vp = cptr->info->def.type_int.max_value;
	if (cptr->hdw->hdw_type == PVR2_HDW_TYPE_24XXX) *vp = 720;
	return 0;
}

static int ctrl_hres_min_get(struct pvr2_ctrl *cptr,int *vp)
{
	/* If we're dealing with a 24xxx device, force the horizontal
	   minimum to be 720 no matter what, since we can't get the device
	   to work properly with any other value.  Otherwise just return
	   the normal value. */
	*vp = cptr->info->def.type_int.min_value;
	if (cptr->hdw->hdw_type == PVR2_HDW_TYPE_24XXX) *vp = 720;
	return 0;
}

static int ctrl_vres_max_get(struct pvr2_ctrl *cptr,int *vp)
{
	/* Actual maximum depends on the video standard in effect. */
	if (cptr->hdw->std_mask_cur & V4L2_STD_525_60) {
		*vp = 480;
	} else {
		*vp = 576;
	}
	return 0;
}

static int ctrl_vres_min_get(struct pvr2_ctrl *cptr,int *vp)
{
	/* Actual minimum depends on device type. */
	if (cptr->hdw->hdw_type == PVR2_HDW_TYPE_24XXX) {
		*vp = 75;
	} else {
		*vp = 17;
	}
	return 0;
}

static int ctrl_cx2341x_is_dirty(struct pvr2_ctrl *cptr)
{
	return cptr->hdw->enc_stale != 0;
}

static void ctrl_cx2341x_clear_dirty(struct pvr2_ctrl *cptr)
{
	cptr->hdw->enc_stale = 0;
}

static int ctrl_cx2341x_get(struct pvr2_ctrl *cptr,int *vp)
{
	int ret;
	struct v4l2_ext_controls cs;
	struct v4l2_ext_control c1;
	memset(&cs,0,sizeof(cs));
	memset(&c1,0,sizeof(c1));
	cs.controls = &c1;
	cs.count = 1;
	c1.id = cptr->info->v4l_id;
	ret = cx2341x_ext_ctrls(&cptr->hdw->enc_ctl_state,&cs,
				VIDIOC_G_EXT_CTRLS);
	if (ret) return ret;
	*vp = c1.value;
	return 0;
}

static int ctrl_cx2341x_set(struct pvr2_ctrl *cptr,int m,int v)
{
	int ret;
	struct v4l2_ext_controls cs;
	struct v4l2_ext_control c1;
	memset(&cs,0,sizeof(cs));
	memset(&c1,0,sizeof(c1));
	cs.controls = &c1;
	cs.count = 1;
	c1.id = cptr->info->v4l_id;
	c1.value = v;
	ret = cx2341x_ext_ctrls(&cptr->hdw->enc_ctl_state,&cs,
				VIDIOC_S_EXT_CTRLS);
	if (ret) return ret;
	cptr->hdw->enc_stale = !0;
	return 0;
}

static unsigned int ctrl_cx2341x_getv4lflags(struct pvr2_ctrl *cptr)
{
	struct v4l2_queryctrl qctrl;
	struct pvr2_ctl_info *info;
	qctrl.id = cptr->info->v4l_id;
	cx2341x_ctrl_query(&cptr->hdw->enc_ctl_state,&qctrl);
	/* Strip out the const so we can adjust a function pointer.  It's
	   OK to do this here because we know this is a dynamically created
	   control, so the underlying storage for the info pointer is (a)
	   private to us, and (b) not in read-only storage.  Either we do
	   this or we significantly complicate the underlying control
	   implementation. */
	info = (struct pvr2_ctl_info *)(cptr->info);
	if (qctrl.flags & V4L2_CTRL_FLAG_READ_ONLY) {
		if (info->set_value) {
			info->set_value = NULL;
		}
	} else {
		if (!(info->set_value)) {
			info->set_value = ctrl_cx2341x_set;
		}
	}
	return qctrl.flags;
}

static int ctrl_streamingenabled_get(struct pvr2_ctrl *cptr,int *vp)
{
	*vp = cptr->hdw->flag_streaming_enabled;
	return 0;
}

static int ctrl_hsm_get(struct pvr2_ctrl *cptr,int *vp)
{
	int result = pvr2_hdw_is_hsm(cptr->hdw);
	*vp = PVR2_CVAL_HSM_FULL;
	if (result < 0) *vp = PVR2_CVAL_HSM_FAIL;
	if (result) *vp = PVR2_CVAL_HSM_HIGH;
	return 0;
}

static int ctrl_stdavail_get(struct pvr2_ctrl *cptr,int *vp)
{
	*vp = cptr->hdw->std_mask_avail;
	return 0;
}

static int ctrl_stdavail_set(struct pvr2_ctrl *cptr,int m,int v)
{
	struct pvr2_hdw *hdw = cptr->hdw;
	v4l2_std_id ns;
	ns = hdw->std_mask_avail;
	ns = (ns & ~m) | (v & m);
	if (ns == hdw->std_mask_avail) return 0;
	hdw->std_mask_avail = ns;
	pvr2_hdw_internal_set_std_avail(hdw);
	pvr2_hdw_internal_find_stdenum(hdw);
	return 0;
}

static int ctrl_std_val_to_sym(struct pvr2_ctrl *cptr,int msk,int val,
			       char *bufPtr,unsigned int bufSize,
			       unsigned int *len)
{
	*len = pvr2_std_id_to_str(bufPtr,bufSize,msk & val);
	return 0;
}

static int ctrl_std_sym_to_val(struct pvr2_ctrl *cptr,
			       const char *bufPtr,unsigned int bufSize,
			       int *mskp,int *valp)
{
	int ret;
	v4l2_std_id id;
	ret = pvr2_std_str_to_id(&id,bufPtr,bufSize);
	if (ret < 0) return ret;
	if (mskp) *mskp = id;
	if (valp) *valp = id;
	return 0;
}

static int ctrl_stdcur_get(struct pvr2_ctrl *cptr,int *vp)
{
	*vp = cptr->hdw->std_mask_cur;
	return 0;
}

static int ctrl_stdcur_set(struct pvr2_ctrl *cptr,int m,int v)
{
	struct pvr2_hdw *hdw = cptr->hdw;
	v4l2_std_id ns;
	ns = hdw->std_mask_cur;
	ns = (ns & ~m) | (v & m);
	if (ns == hdw->std_mask_cur) return 0;
	hdw->std_mask_cur = ns;
	hdw->std_dirty = !0;
	pvr2_hdw_internal_find_stdenum(hdw);
	return 0;
}

static int ctrl_stdcur_is_dirty(struct pvr2_ctrl *cptr)
{
	return cptr->hdw->std_dirty != 0;
}

static void ctrl_stdcur_clear_dirty(struct pvr2_ctrl *cptr)
{
	cptr->hdw->std_dirty = 0;
}

static int ctrl_signal_get(struct pvr2_ctrl *cptr,int *vp)
{
	*vp = ((pvr2_hdw_get_signal_status_internal(cptr->hdw) &
		PVR2_SIGNAL_OK) ? 1 : 0);
	return 0;
}

static int ctrl_subsys_get(struct pvr2_ctrl *cptr,int *vp)
{
	*vp = cptr->hdw->subsys_enabled_mask;
	return 0;
}

static int ctrl_subsys_set(struct pvr2_ctrl *cptr,int m,int v)
{
	pvr2_hdw_subsys_bit_chg_no_lock(cptr->hdw,m,v);
	return 0;
}

static int ctrl_subsys_stream_get(struct pvr2_ctrl *cptr,int *vp)
{
	*vp = cptr->hdw->subsys_stream_mask;
	return 0;
}

static int ctrl_subsys_stream_set(struct pvr2_ctrl *cptr,int m,int v)
{
	pvr2_hdw_subsys_stream_bit_chg_no_lock(cptr->hdw,m,v);
	return 0;
}

static int ctrl_stdenumcur_set(struct pvr2_ctrl *cptr,int m,int v)
{
	struct pvr2_hdw *hdw = cptr->hdw;
	if (v < 0) return -EINVAL;
	if (v > hdw->std_enum_cnt) return -EINVAL;
	hdw->std_enum_cur = v;
	if (!v) return 0;
	v--;
	if (hdw->std_mask_cur == hdw->std_defs[v].id) return 0;
	hdw->std_mask_cur = hdw->std_defs[v].id;
	hdw->std_dirty = !0;
	return 0;
}


static int ctrl_stdenumcur_get(struct pvr2_ctrl *cptr,int *vp)
{
	*vp = cptr->hdw->std_enum_cur;
	return 0;
}


static int ctrl_stdenumcur_is_dirty(struct pvr2_ctrl *cptr)
{
	return cptr->hdw->std_dirty != 0;
}


static void ctrl_stdenumcur_clear_dirty(struct pvr2_ctrl *cptr)
{
	cptr->hdw->std_dirty = 0;
}


#define DEFINT(vmin,vmax) \
	.type = pvr2_ctl_int, \
	.def.type_int.min_value = vmin, \
	.def.type_int.max_value = vmax

#define DEFENUM(tab) \
	.type = pvr2_ctl_enum, \
	.def.type_enum.count = (sizeof(tab)/sizeof((tab)[0])), \
	.def.type_enum.value_names = tab

#define DEFBOOL \
	.type = pvr2_ctl_bool

#define DEFMASK(msk,tab) \
	.type = pvr2_ctl_bitmask, \
	.def.type_bitmask.valid_bits = msk, \
	.def.type_bitmask.bit_names = tab

#define DEFREF(vname) \
	.set_value = ctrl_set_##vname, \
	.get_value = ctrl_get_##vname, \
	.is_dirty = ctrl_isdirty_##vname, \
	.clear_dirty = ctrl_cleardirty_##vname


#define VCREATE_FUNCS(vname) \
static int ctrl_get_##vname(struct pvr2_ctrl *cptr,int *vp) \
{*vp = cptr->hdw->vname##_val; return 0;} \
static int ctrl_set_##vname(struct pvr2_ctrl *cptr,int m,int v) \
{cptr->hdw->vname##_val = v; cptr->hdw->vname##_dirty = !0; return 0;} \
static int ctrl_isdirty_##vname(struct pvr2_ctrl *cptr) \
{return cptr->hdw->vname##_dirty != 0;} \
static void ctrl_cleardirty_##vname(struct pvr2_ctrl *cptr) \
{cptr->hdw->vname##_dirty = 0;}

VCREATE_FUNCS(brightness)
VCREATE_FUNCS(contrast)
VCREATE_FUNCS(saturation)
VCREATE_FUNCS(hue)
VCREATE_FUNCS(volume)
VCREATE_FUNCS(balance)
VCREATE_FUNCS(bass)
VCREATE_FUNCS(treble)
VCREATE_FUNCS(mute)
VCREATE_FUNCS(input)
VCREATE_FUNCS(audiomode)
VCREATE_FUNCS(res_hor)
VCREATE_FUNCS(res_ver)
VCREATE_FUNCS(srate)

#define MIN_FREQ 55250000L
#define MAX_FREQ 850000000L

/* Table definition of all controls which can be manipulated */
static const struct pvr2_ctl_info control_defs[] = {
	{
		.v4l_id = V4L2_CID_BRIGHTNESS,
		.desc = "Brightness",
		.name = "brightness",
		.default_value = 128,
		DEFREF(brightness),
		DEFINT(0,255),
	},{
		.v4l_id = V4L2_CID_CONTRAST,
		.desc = "Contrast",
		.name = "contrast",
		.default_value = 68,
		DEFREF(contrast),
		DEFINT(0,127),
	},{
		.v4l_id = V4L2_CID_SATURATION,
		.desc = "Saturation",
		.name = "saturation",
		.default_value = 64,
		DEFREF(saturation),
		DEFINT(0,127),
	},{
		.v4l_id = V4L2_CID_HUE,
		.desc = "Hue",
		.name = "hue",
		.default_value = 0,
		DEFREF(hue),
		DEFINT(-128,127),
	},{
		.v4l_id = V4L2_CID_AUDIO_VOLUME,
		.desc = "Volume",
		.name = "volume",
		.default_value = 65535,
		DEFREF(volume),
		DEFINT(0,65535),
	},{
		.v4l_id = V4L2_CID_AUDIO_BALANCE,
		.desc = "Balance",
		.name = "balance",
		.default_value = 0,
		DEFREF(balance),
		DEFINT(-32768,32767),
	},{
		.v4l_id = V4L2_CID_AUDIO_BASS,
		.desc = "Bass",
		.name = "bass",
		.default_value = 0,
		DEFREF(bass),
		DEFINT(-32768,32767),
	},{
		.v4l_id = V4L2_CID_AUDIO_TREBLE,
		.desc = "Treble",
		.name = "treble",
		.default_value = 0,
		DEFREF(treble),
		DEFINT(-32768,32767),
	},{
		.v4l_id = V4L2_CID_AUDIO_MUTE,
		.desc = "Mute",
		.name = "mute",
		.default_value = 0,
		DEFREF(mute),
		DEFBOOL,
	},{
		.desc = "Video Source",
		.name = "input",
		.internal_id = PVR2_CID_INPUT,
		.default_value = PVR2_CVAL_INPUT_TV,
		DEFREF(input),
		DEFENUM(control_values_input),
	},{
		.desc = "Audio Mode",
		.name = "audio_mode",
		.internal_id = PVR2_CID_AUDIOMODE,
		.default_value = V4L2_TUNER_MODE_STEREO,
		DEFREF(audiomode),
		DEFENUM(control_values_audiomode),
	},{
		.desc = "Horizontal capture resolution",
		.name = "resolution_hor",
		.internal_id = PVR2_CID_HRES,
		.default_value = 720,
		DEFREF(res_hor),
		DEFINT(19,720),
		/* Hook in check for clamp on horizontal resolution in
		   order to avoid unsolved problem involving cx25840. */
		.get_max_value = ctrl_hres_max_get,
		.get_min_value = ctrl_hres_min_get,
	},{
		.desc = "Vertical capture resolution",
		.name = "resolution_ver",
		.internal_id = PVR2_CID_VRES,
		.default_value = 480,
		DEFREF(res_ver),
		DEFINT(17,576),
		/* Hook in check for video standard and adjust maximum
		   depending on the standard. */
		.get_max_value = ctrl_vres_max_get,
		.get_min_value = ctrl_vres_min_get,
	},{
		.v4l_id = V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ,
		.default_value = V4L2_MPEG_AUDIO_SAMPLING_FREQ_48000,
		.desc = "Audio Sampling Frequency",
		.name = "srate",
		DEFREF(srate),
		DEFENUM(control_values_srate),
	},{
		.desc = "Tuner Frequency (Hz)",
		.name = "frequency",
		.internal_id = PVR2_CID_FREQUENCY,
		.default_value = 175250000L,
		.set_value = ctrl_freq_set,
		.get_value = ctrl_freq_get,
		.is_dirty = ctrl_freq_is_dirty,
		.clear_dirty = ctrl_freq_clear_dirty,
		DEFINT(MIN_FREQ,MAX_FREQ),
	},{
		.desc = "Channel",
		.name = "channel",
		.set_value = ctrl_channel_set,
		.get_value = ctrl_channel_get,
		DEFINT(0,FREQTABLE_SIZE),
	},{
		.desc = "Channel Program Frequency",
		.name = "freq_table_value",
		.set_value = ctrl_channelfreq_set,
		.get_value = ctrl_channelfreq_get,
		DEFINT(MIN_FREQ,MAX_FREQ),
	},{
		.desc = "Channel Program ID",
		.name = "freq_table_channel",
		.set_value = ctrl_channelprog_set,
		.get_value = ctrl_channelprog_get,
		DEFINT(0,FREQTABLE_SIZE),
	},{
		.desc = "Streaming Enabled",
		.name = "streaming_enabled",
		.get_value = ctrl_streamingenabled_get,
		DEFBOOL,
	},{
		.desc = "USB Speed",
		.name = "usb_speed",
		.get_value = ctrl_hsm_get,
		DEFENUM(control_values_hsm),
	},{
		.desc = "Signal Present",
		.name = "signal_present",
		.get_value = ctrl_signal_get,
		DEFBOOL,
	},{
		.desc = "Video Standards Available Mask",
		.name = "video_standard_mask_available",
		.internal_id = PVR2_CID_STDAVAIL,
		.skip_init = !0,
		.get_value = ctrl_stdavail_get,
		.set_value = ctrl_stdavail_set,
		.val_to_sym = ctrl_std_val_to_sym,
		.sym_to_val = ctrl_std_sym_to_val,
		.type = pvr2_ctl_bitmask,
	},{
		.desc = "Video Standards In Use Mask",
		.name = "video_standard_mask_active",
		.internal_id = PVR2_CID_STDCUR,
		.skip_init = !0,
		.get_value = ctrl_stdcur_get,
		.set_value = ctrl_stdcur_set,
		.is_dirty = ctrl_stdcur_is_dirty,
		.clear_dirty = ctrl_stdcur_clear_dirty,
		.val_to_sym = ctrl_std_val_to_sym,
		.sym_to_val = ctrl_std_sym_to_val,
		.type = pvr2_ctl_bitmask,
	},{
		.desc = "Subsystem enabled mask",
		.name = "debug_subsys_mask",
		.skip_init = !0,
		.get_value = ctrl_subsys_get,
		.set_value = ctrl_subsys_set,
		DEFMASK(PVR2_SUBSYS_ALL,control_values_subsystem),
	},{
		.desc = "Subsystem stream mask",
		.name = "debug_subsys_stream_mask",
		.skip_init = !0,
		.get_value = ctrl_subsys_stream_get,
		.set_value = ctrl_subsys_stream_set,
		DEFMASK(PVR2_SUBSYS_ALL,control_values_subsystem),
	},{
		.desc = "Video Standard Name",
		.name = "video_standard",
		.internal_id = PVR2_CID_STDENUM,
		.skip_init = !0,
		.get_value = ctrl_stdenumcur_get,
		.set_value = ctrl_stdenumcur_set,
		.is_dirty = ctrl_stdenumcur_is_dirty,
		.clear_dirty = ctrl_stdenumcur_clear_dirty,
		.type = pvr2_ctl_enum,
	}
};

#define CTRLDEF_COUNT (sizeof(control_defs)/sizeof(control_defs[0]))


const char *pvr2_config_get_name(enum pvr2_config cfg)
{
	switch (cfg) {
	case pvr2_config_empty: return "empty";
	case pvr2_config_mpeg: return "mpeg";
	case pvr2_config_vbi: return "vbi";
	case pvr2_config_radio: return "radio";
	}
	return "<unknown>";
}


struct usb_device *pvr2_hdw_get_dev(struct pvr2_hdw *hdw)
{
	return hdw->usb_dev;
}


unsigned long pvr2_hdw_get_sn(struct pvr2_hdw *hdw)
{
	return hdw->serial_number;
}

int pvr2_hdw_get_unit_number(struct pvr2_hdw *hdw)
{
	return hdw->unit_number;
}


/* Attempt to locate one of the given set of files.  Messages are logged
   appropriate to what has been found.  The return value will be 0 or
   greater on success (it will be the index of the file name found) and
   fw_entry will be filled in.  Otherwise a negative error is returned on
   failure.  If the return value is -ENOENT then no viable firmware file
   could be located. */
static int pvr2_locate_firmware(struct pvr2_hdw *hdw,
				const struct firmware **fw_entry,
				const char *fwtypename,
				unsigned int fwcount,
				const char *fwnames[])
{
	unsigned int idx;
	int ret = -EINVAL;
	for (idx = 0; idx < fwcount; idx++) {
		ret = request_firmware(fw_entry,
				       fwnames[idx],
				       &hdw->usb_dev->dev);
		if (!ret) {
			trace_firmware("Located %s firmware: %s;"
				       " uploading...",
				       fwtypename,
				       fwnames[idx]);
			return idx;
		}
		if (ret == -ENOENT) continue;
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "request_firmware fatal error with code=%d",ret);
		return ret;
	}
	pvr2_trace(PVR2_TRACE_ERROR_LEGS,
		   "***WARNING***"
		   " Device %s firmware"
		   " seems to be missing.",
		   fwtypename);
	pvr2_trace(PVR2_TRACE_ERROR_LEGS,
		   "Did you install the pvrusb2 firmware files"
		   " in their proper location?");
	if (fwcount == 1) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "request_firmware unable to locate %s file %s",
			   fwtypename,fwnames[0]);
	} else {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "request_firmware unable to locate"
			   " one of the following %s files:",
			   fwtypename);
		for (idx = 0; idx < fwcount; idx++) {
			pvr2_trace(PVR2_TRACE_ERROR_LEGS,
				   "request_firmware: Failed to find %s",
				   fwnames[idx]);
		}
	}
	return ret;
}


/*
 * pvr2_upload_firmware1().
 *
 * Send the 8051 firmware to the device.  After the upload, arrange for
 * device to re-enumerate.
 *
 * NOTE : the pointer to the firmware data given by request_firmware()
 * is not suitable for an usb transaction.
 *
 */
static int pvr2_upload_firmware1(struct pvr2_hdw *hdw)
{
	const struct firmware *fw_entry = NULL;
	void  *fw_ptr;
	unsigned int pipe;
	int ret;
	u16 address;
	static const char *fw_files_29xxx[] = {
		"v4l-pvrusb2-29xxx-01.fw",
	};
	static const char *fw_files_24xxx[] = {
		"v4l-pvrusb2-24xxx-01.fw",
	};
	static const struct pvr2_string_table fw_file_defs[] = {
		[PVR2_HDW_TYPE_29XXX] = {
			fw_files_29xxx,
			sizeof(fw_files_29xxx)/sizeof(fw_files_29xxx[0]),
		},
		[PVR2_HDW_TYPE_24XXX] = {
			fw_files_24xxx,
			sizeof(fw_files_24xxx)/sizeof(fw_files_24xxx[0]),
		},
	};
	hdw->fw1_state = FW1_STATE_FAILED; // default result

	trace_firmware("pvr2_upload_firmware1");

	ret = pvr2_locate_firmware(hdw,&fw_entry,"fx2 controller",
				   fw_file_defs[hdw->hdw_type].cnt,
				   fw_file_defs[hdw->hdw_type].lst);
	if (ret < 0) {
		if (ret == -ENOENT) hdw->fw1_state = FW1_STATE_MISSING;
		return ret;
	}

	usb_settoggle(hdw->usb_dev, 0 & 0xf, !(0 & USB_DIR_IN), 0);
	usb_clear_halt(hdw->usb_dev, usb_sndbulkpipe(hdw->usb_dev, 0 & 0x7f));

	pipe = usb_sndctrlpipe(hdw->usb_dev, 0);

	if (fw_entry->size != 0x2000){
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,"wrong fx2 firmware size");
		release_firmware(fw_entry);
		return -ENOMEM;
	}

	fw_ptr = kmalloc(0x800, GFP_KERNEL);
	if (fw_ptr == NULL){
		release_firmware(fw_entry);
		return -ENOMEM;
	}

	/* We have to hold the CPU during firmware upload. */
	pvr2_hdw_cpureset_assert(hdw,1);

	/* upload the firmware to address 0000-1fff in 2048 (=0x800) bytes
	   chunk. */

	ret = 0;
	for(address = 0; address < fw_entry->size; address += 0x800) {
		memcpy(fw_ptr, fw_entry->data + address, 0x800);
		ret += usb_control_msg(hdw->usb_dev, pipe, 0xa0, 0x40, address,
				       0, fw_ptr, 0x800, HZ);
	}

	trace_firmware("Upload done, releasing device's CPU");

	/* Now release the CPU.  It will disconnect and reconnect later. */
	pvr2_hdw_cpureset_assert(hdw,0);

	kfree(fw_ptr);
	release_firmware(fw_entry);

	trace_firmware("Upload done (%d bytes sent)",ret);

	/* We should have written 8192 bytes */
	if (ret == 8192) {
		hdw->fw1_state = FW1_STATE_RELOAD;
		return 0;
	}

	return -EIO;
}


/*
 * pvr2_upload_firmware2()
 *
 * This uploads encoder firmware on endpoint 2.
 *
 */

int pvr2_upload_firmware2(struct pvr2_hdw *hdw)
{
	const struct firmware *fw_entry = NULL;
	void  *fw_ptr;
	unsigned int pipe, fw_len, fw_done;
	int actual_length;
	int ret = 0;
	int fwidx;
	static const char *fw_files[] = {
		CX2341X_FIRM_ENC_FILENAME,
	};

	trace_firmware("pvr2_upload_firmware2");

	ret = pvr2_locate_firmware(hdw,&fw_entry,"encoder",
				   sizeof(fw_files)/sizeof(fw_files[0]),
				   fw_files);
	if (ret < 0) return ret;
	fwidx = ret;
	ret = 0;
	/* Since we're about to completely reinitialize the encoder,
	   invalidate our cached copy of its configuration state.  Next
	   time we configure the encoder, then we'll fully configure it. */
	hdw->enc_cur_valid = 0;

	/* First prepare firmware loading */
	ret |= pvr2_write_register(hdw, 0x0048, 0xffffffff); /*interrupt mask*/
	ret |= pvr2_hdw_gpio_chg_dir(hdw,0xffffffff,0x00000088); /*gpio dir*/
	ret |= pvr2_hdw_gpio_chg_out(hdw,0xffffffff,0x00000008); /*gpio output state*/
	ret |= pvr2_hdw_cmd_deep_reset(hdw);
	ret |= pvr2_write_register(hdw, 0xa064, 0x00000000); /*APU command*/
	ret |= pvr2_hdw_gpio_chg_dir(hdw,0xffffffff,0x00000408); /*gpio dir*/
	ret |= pvr2_hdw_gpio_chg_out(hdw,0xffffffff,0x00000008); /*gpio output state*/
	ret |= pvr2_write_register(hdw, 0x9058, 0xffffffed); /*VPU ctrl*/
	ret |= pvr2_write_register(hdw, 0x9054, 0xfffffffd); /*reset hw blocks*/
	ret |= pvr2_write_register(hdw, 0x07f8, 0x80000800); /*encoder SDRAM refresh*/
	ret |= pvr2_write_register(hdw, 0x07fc, 0x0000001a); /*encoder SDRAM pre-charge*/
	ret |= pvr2_write_register(hdw, 0x0700, 0x00000000); /*I2C clock*/
	ret |= pvr2_write_register(hdw, 0xaa00, 0x00000000); /*unknown*/
	ret |= pvr2_write_register(hdw, 0xaa04, 0x00057810); /*unknown*/
	ret |= pvr2_write_register(hdw, 0xaa10, 0x00148500); /*unknown*/
	ret |= pvr2_write_register(hdw, 0xaa18, 0x00840000); /*unknown*/
	ret |= pvr2_write_u8(hdw, 0x52, 0);
	ret |= pvr2_write_u16(hdw, 0x0600, 0);

	if (ret) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "firmware2 upload prep failed, ret=%d",ret);
		release_firmware(fw_entry);
		return ret;
	}

	/* Now send firmware */

	fw_len = fw_entry->size;

	if (fw_len % FIRMWARE_CHUNK_SIZE) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "size of %s firmware"
			   " must be a multiple of 8192B",
			   fw_files[fwidx]);
		release_firmware(fw_entry);
		return -1;
	}

	fw_ptr = kmalloc(FIRMWARE_CHUNK_SIZE, GFP_KERNEL);
	if (fw_ptr == NULL){
		release_firmware(fw_entry);
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "failed to allocate memory for firmware2 upload");
		return -ENOMEM;
	}

	pipe = usb_sndbulkpipe(hdw->usb_dev, PVR2_FIRMWARE_ENDPOINT);

	for (fw_done = 0 ; (fw_done < fw_len) && !ret ;
	     fw_done += FIRMWARE_CHUNK_SIZE ) {
		int i;
		memcpy(fw_ptr, fw_entry->data + fw_done, FIRMWARE_CHUNK_SIZE);
		/* Usbsnoop log  shows that we must swap bytes... */
		for (i = 0; i < FIRMWARE_CHUNK_SIZE/4 ; i++)
			((u32 *)fw_ptr)[i] = ___swab32(((u32 *)fw_ptr)[i]);

		ret |= usb_bulk_msg(hdw->usb_dev, pipe, fw_ptr,
				    FIRMWARE_CHUNK_SIZE,
				    &actual_length, HZ);
		ret |= (actual_length != FIRMWARE_CHUNK_SIZE);
	}

	trace_firmware("upload of %s : %i / %i ",
		       fw_files[fwidx],fw_done,fw_len);

	kfree(fw_ptr);
	release_firmware(fw_entry);

	if (ret) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "firmware2 upload transfer failure");
		return ret;
	}

	/* Finish upload */

	ret |= pvr2_write_register(hdw, 0x9054, 0xffffffff); /*reset hw blocks*/
	ret |= pvr2_write_register(hdw, 0x9058, 0xffffffe8); /*VPU ctrl*/
	ret |= pvr2_write_u16(hdw, 0x0600, 0);

	if (ret) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "firmware2 upload post-proc failure");
	} else {
		hdw->subsys_enabled_mask |= (1<<PVR2_SUBSYS_B_ENC_FIRMWARE);
	}
	return ret;
}


#define FIRMWARE_RECOVERY_BITS \
	((1<<PVR2_SUBSYS_B_ENC_CFG) | \
	 (1<<PVR2_SUBSYS_B_ENC_RUN) | \
	 (1<<PVR2_SUBSYS_B_ENC_FIRMWARE) | \
	 (1<<PVR2_SUBSYS_B_USBSTREAM_RUN))

/*

  This single function is key to pretty much everything.  The pvrusb2
  device can logically be viewed as a series of subsystems which can be
  stopped / started or unconfigured / configured.  To get things streaming,
  one must configure everything and start everything, but there may be
  various reasons over time to deconfigure something or stop something.
  This function handles all of this activity.  Everything EVERYWHERE that
  must affect a subsystem eventually comes here to do the work.

  The current state of all subsystems is represented by a single bit mask,
  known as subsys_enabled_mask.  The bit positions are defined by the
  PVR2_SUBSYS_xxxx macros, with one subsystem per bit position.  At any
  time the set of configured or active subsystems can be queried just by
  looking at that mask.  To change bits in that mask, this function here
  must be called.  The "msk" argument indicates which bit positions to
  change, and the "val" argument defines the new values for the positions
  defined by "msk".

  There is a priority ordering of starting / stopping things, and for
  multiple requested changes, this function implements that ordering.
  (Thus we will act on a request to load encoder firmware before we
  configure the encoder.)  In addition to priority ordering, there is a
  recovery strategy implemented here.  If a particular step fails and we
  detect that failure, this function will clear the affected subsystem bits
  and restart.  Thus we have a means for recovering from a dead encoder:
  Clear all bits that correspond to subsystems that we need to restart /
  reconfigure and start over.

*/
static void pvr2_hdw_subsys_bit_chg_no_lock(struct pvr2_hdw *hdw,
					    unsigned long msk,
					    unsigned long val)
{
	unsigned long nmsk;
	unsigned long vmsk;
	int ret;
	unsigned int tryCount = 0;

	if (!hdw->flag_ok) return;

	msk &= PVR2_SUBSYS_ALL;
	nmsk = (hdw->subsys_enabled_mask & ~msk) | (val & msk);
	nmsk &= PVR2_SUBSYS_ALL;

	for (;;) {
		tryCount++;
		if (!((nmsk ^ hdw->subsys_enabled_mask) &
		      PVR2_SUBSYS_ALL)) break;
		if (tryCount > 4) {
			pvr2_trace(PVR2_TRACE_ERROR_LEGS,
				   "Too many retries when configuring device;"
				   " giving up");
			pvr2_hdw_render_useless(hdw);
			break;
		}
		if (tryCount > 1) {
			pvr2_trace(PVR2_TRACE_ERROR_LEGS,
				   "Retrying device reconfiguration");
		}
		pvr2_trace(PVR2_TRACE_INIT,
			   "subsys mask changing 0x%lx:0x%lx"
			   " from 0x%lx to 0x%lx",
			   msk,val,hdw->subsys_enabled_mask,nmsk);

		vmsk = (nmsk ^ hdw->subsys_enabled_mask) &
			hdw->subsys_enabled_mask;
		if (vmsk) {
			if (vmsk & (1<<PVR2_SUBSYS_B_ENC_RUN)) {
				pvr2_trace(PVR2_TRACE_CTL,
					   "/*---TRACE_CTL----*/"
					   " pvr2_encoder_stop");
				ret = pvr2_encoder_stop(hdw);
				if (ret) {
					pvr2_trace(PVR2_TRACE_ERROR_LEGS,
						   "Error recovery initiated");
					hdw->subsys_enabled_mask &=
						~FIRMWARE_RECOVERY_BITS;
					continue;
				}
			}
			if (vmsk & (1<<PVR2_SUBSYS_B_USBSTREAM_RUN)) {
				pvr2_trace(PVR2_TRACE_CTL,
					   "/*---TRACE_CTL----*/"
					   " pvr2_hdw_cmd_usbstream(0)");
				pvr2_hdw_cmd_usbstream(hdw,0);
			}
			if (vmsk & (1<<PVR2_SUBSYS_B_DIGITIZER_RUN)) {
				pvr2_trace(PVR2_TRACE_CTL,
					   "/*---TRACE_CTL----*/"
					   " decoder disable");
				if (hdw->decoder_ctrl) {
					hdw->decoder_ctrl->enable(
						hdw->decoder_ctrl->ctxt,0);
				} else {
					pvr2_trace(PVR2_TRACE_ERROR_LEGS,
						   "WARNING:"
						   " No decoder present");
				}
				hdw->subsys_enabled_mask &=
					~(1<<PVR2_SUBSYS_B_DIGITIZER_RUN);
			}
			if (vmsk & PVR2_SUBSYS_CFG_ALL) {
				hdw->subsys_enabled_mask &=
					~(vmsk & PVR2_SUBSYS_CFG_ALL);
			}
		}
		vmsk = (nmsk ^ hdw->subsys_enabled_mask) & nmsk;
		if (vmsk) {
			if (vmsk & (1<<PVR2_SUBSYS_B_ENC_FIRMWARE)) {
				pvr2_trace(PVR2_TRACE_CTL,
					   "/*---TRACE_CTL----*/"
					   " pvr2_upload_firmware2");
				ret = pvr2_upload_firmware2(hdw);
				if (ret) {
					pvr2_trace(PVR2_TRACE_ERROR_LEGS,
						   "Failure uploading encoder"
						   " firmware");
					pvr2_hdw_render_useless(hdw);
					break;
				}
			}
			if (vmsk & (1<<PVR2_SUBSYS_B_ENC_CFG)) {
				pvr2_trace(PVR2_TRACE_CTL,
					   "/*---TRACE_CTL----*/"
					   " pvr2_encoder_configure");
				ret = pvr2_encoder_configure(hdw);
				if (ret) {
					pvr2_trace(PVR2_TRACE_ERROR_LEGS,
						   "Error recovery initiated");
					hdw->subsys_enabled_mask &=
						~FIRMWARE_RECOVERY_BITS;
					continue;
				}
			}
			if (vmsk & (1<<PVR2_SUBSYS_B_DIGITIZER_RUN)) {
				pvr2_trace(PVR2_TRACE_CTL,
					   "/*---TRACE_CTL----*/"
					   " decoder enable");
				if (hdw->decoder_ctrl) {
					hdw->decoder_ctrl->enable(
						hdw->decoder_ctrl->ctxt,!0);
				} else {
					pvr2_trace(PVR2_TRACE_ERROR_LEGS,
						   "WARNING:"
						   " No decoder present");
				}
				hdw->subsys_enabled_mask |=
					(1<<PVR2_SUBSYS_B_DIGITIZER_RUN);
			}
			if (vmsk & (1<<PVR2_SUBSYS_B_USBSTREAM_RUN)) {
				pvr2_trace(PVR2_TRACE_CTL,
					   "/*---TRACE_CTL----*/"
					   " pvr2_hdw_cmd_usbstream(1)");
				pvr2_hdw_cmd_usbstream(hdw,!0);
			}
			if (vmsk & (1<<PVR2_SUBSYS_B_ENC_RUN)) {
				pvr2_trace(PVR2_TRACE_CTL,
					   "/*---TRACE_CTL----*/"
					   " pvr2_encoder_start");
				ret = pvr2_encoder_start(hdw);
				if (ret) {
					pvr2_trace(PVR2_TRACE_ERROR_LEGS,
						   "Error recovery initiated");
					hdw->subsys_enabled_mask &=
						~FIRMWARE_RECOVERY_BITS;
					continue;
				}
			}
		}
	}
}


void pvr2_hdw_subsys_bit_chg(struct pvr2_hdw *hdw,
			     unsigned long msk,unsigned long val)
{
	LOCK_TAKE(hdw->big_lock); do {
		pvr2_hdw_subsys_bit_chg_no_lock(hdw,msk,val);
	} while (0); LOCK_GIVE(hdw->big_lock);
}


unsigned long pvr2_hdw_subsys_get(struct pvr2_hdw *hdw)
{
	return hdw->subsys_enabled_mask;
}


unsigned long pvr2_hdw_subsys_stream_get(struct pvr2_hdw *hdw)
{
	return hdw->subsys_stream_mask;
}


static void pvr2_hdw_subsys_stream_bit_chg_no_lock(struct pvr2_hdw *hdw,
						   unsigned long msk,
						   unsigned long val)
{
	unsigned long val2;
	msk &= PVR2_SUBSYS_ALL;
	val2 = ((hdw->subsys_stream_mask & ~msk) | (val & msk));
	pvr2_trace(PVR2_TRACE_INIT,
		   "stream mask changing 0x%lx:0x%lx from 0x%lx to 0x%lx",
		   msk,val,hdw->subsys_stream_mask,val2);
	hdw->subsys_stream_mask = val2;
}


void pvr2_hdw_subsys_stream_bit_chg(struct pvr2_hdw *hdw,
				    unsigned long msk,
				    unsigned long val)
{
	LOCK_TAKE(hdw->big_lock); do {
		pvr2_hdw_subsys_stream_bit_chg_no_lock(hdw,msk,val);
	} while (0); LOCK_GIVE(hdw->big_lock);
}


static int pvr2_hdw_set_streaming_no_lock(struct pvr2_hdw *hdw,int enableFl)
{
	if ((!enableFl) == !(hdw->flag_streaming_enabled)) return 0;
	if (enableFl) {
		pvr2_trace(PVR2_TRACE_START_STOP,
			   "/*--TRACE_STREAM--*/ enable");
		pvr2_hdw_subsys_bit_chg_no_lock(hdw,~0,~0);
	} else {
		pvr2_trace(PVR2_TRACE_START_STOP,
			   "/*--TRACE_STREAM--*/ disable");
		pvr2_hdw_subsys_bit_chg_no_lock(hdw,hdw->subsys_stream_mask,0);
	}
	if (!hdw->flag_ok) return -EIO;
	hdw->flag_streaming_enabled = enableFl != 0;
	return 0;
}


int pvr2_hdw_get_streaming(struct pvr2_hdw *hdw)
{
	return hdw->flag_streaming_enabled != 0;
}


int pvr2_hdw_set_streaming(struct pvr2_hdw *hdw,int enable_flag)
{
	int ret;
	LOCK_TAKE(hdw->big_lock); do {
		ret = pvr2_hdw_set_streaming_no_lock(hdw,enable_flag);
	} while (0); LOCK_GIVE(hdw->big_lock);
	return ret;
}


static int pvr2_hdw_set_stream_type_no_lock(struct pvr2_hdw *hdw,
					    enum pvr2_config config)
{
	unsigned long sm = hdw->subsys_enabled_mask;
	if (!hdw->flag_ok) return -EIO;
	pvr2_hdw_subsys_bit_chg_no_lock(hdw,hdw->subsys_stream_mask,0);
	hdw->config = config;
	pvr2_hdw_subsys_bit_chg_no_lock(hdw,~0,sm);
	return 0;
}


int pvr2_hdw_set_stream_type(struct pvr2_hdw *hdw,enum pvr2_config config)
{
	int ret;
	if (!hdw->flag_ok) return -EIO;
	LOCK_TAKE(hdw->big_lock);
	ret = pvr2_hdw_set_stream_type_no_lock(hdw,config);
	LOCK_GIVE(hdw->big_lock);
	return ret;
}


static int get_default_tuner_type(struct pvr2_hdw *hdw)
{
	int unit_number = hdw->unit_number;
	int tp = -1;
	if ((unit_number >= 0) && (unit_number < PVR_NUM)) {
		tp = tuner[unit_number];
	}
	if (tp < 0) return -EINVAL;
	hdw->tuner_type = tp;
	return 0;
}


static v4l2_std_id get_default_standard(struct pvr2_hdw *hdw)
{
	int unit_number = hdw->unit_number;
	int tp = 0;
	if ((unit_number >= 0) && (unit_number < PVR_NUM)) {
		tp = video_std[unit_number];
	}
	return tp;
}


static unsigned int get_default_error_tolerance(struct pvr2_hdw *hdw)
{
	int unit_number = hdw->unit_number;
	int tp = 0;
	if ((unit_number >= 0) && (unit_number < PVR_NUM)) {
		tp = tolerance[unit_number];
	}
	return tp;
}


static int pvr2_hdw_check_firmware(struct pvr2_hdw *hdw)
{
	/* Try a harmless request to fetch the eeprom's address over
	   endpoint 1.  See what happens.  Only the full FX2 image can
	   respond to this.  If this probe fails then likely the FX2
	   firmware needs be loaded. */
	int result;
	LOCK_TAKE(hdw->ctl_lock); do {
		hdw->cmd_buffer[0] = 0xeb;
		result = pvr2_send_request_ex(hdw,HZ*1,!0,
					   hdw->cmd_buffer,1,
					   hdw->cmd_buffer,1);
		if (result < 0) break;
	} while(0); LOCK_GIVE(hdw->ctl_lock);
	if (result) {
		pvr2_trace(PVR2_TRACE_INIT,
			   "Probe of device endpoint 1 result status %d",
			   result);
	} else {
		pvr2_trace(PVR2_TRACE_INIT,
			   "Probe of device endpoint 1 succeeded");
	}
	return result == 0;
}

static void pvr2_hdw_setup_std(struct pvr2_hdw *hdw)
{
	char buf[40];
	unsigned int bcnt;
	v4l2_std_id std1,std2;

	std1 = get_default_standard(hdw);

	bcnt = pvr2_std_id_to_str(buf,sizeof(buf),hdw->std_mask_eeprom);
	pvr2_trace(PVR2_TRACE_INIT,
		   "Supported video standard(s) reported by eeprom: %.*s",
		   bcnt,buf);

	hdw->std_mask_avail = hdw->std_mask_eeprom;

	std2 = std1 & ~hdw->std_mask_avail;
	if (std2) {
		bcnt = pvr2_std_id_to_str(buf,sizeof(buf),std2);
		pvr2_trace(PVR2_TRACE_INIT,
			   "Expanding supported video standards"
			   " to include: %.*s",
			   bcnt,buf);
		hdw->std_mask_avail |= std2;
	}

	pvr2_hdw_internal_set_std_avail(hdw);

	if (std1) {
		bcnt = pvr2_std_id_to_str(buf,sizeof(buf),std1);
		pvr2_trace(PVR2_TRACE_INIT,
			   "Initial video standard forced to %.*s",
			   bcnt,buf);
		hdw->std_mask_cur = std1;
		hdw->std_dirty = !0;
		pvr2_hdw_internal_find_stdenum(hdw);
		return;
	}

	if (hdw->std_enum_cnt > 1) {
		// Autoselect the first listed standard
		hdw->std_enum_cur = 1;
		hdw->std_mask_cur = hdw->std_defs[hdw->std_enum_cur-1].id;
		hdw->std_dirty = !0;
		pvr2_trace(PVR2_TRACE_INIT,
			   "Initial video standard auto-selected to %s",
			   hdw->std_defs[hdw->std_enum_cur-1].name);
		return;
	}

	pvr2_trace(PVR2_TRACE_ERROR_LEGS,
		   "Unable to select a viable initial video standard");
}


static void pvr2_hdw_setup_low(struct pvr2_hdw *hdw)
{
	int ret;
	unsigned int idx;
	struct pvr2_ctrl *cptr;
	int reloadFl = 0;
	if (!reloadFl) {
		reloadFl = (hdw->usb_intf->cur_altsetting->desc.bNumEndpoints
			    == 0);
		if (reloadFl) {
			pvr2_trace(PVR2_TRACE_INIT,
				   "USB endpoint config looks strange"
				   "; possibly firmware needs to be loaded");
		}
	}
	if (!reloadFl) {
		reloadFl = !pvr2_hdw_check_firmware(hdw);
		if (reloadFl) {
			pvr2_trace(PVR2_TRACE_INIT,
				   "Check for FX2 firmware failed"
				   "; possibly firmware needs to be loaded");
		}
	}
	if (reloadFl) {
		if (pvr2_upload_firmware1(hdw) != 0) {
			pvr2_trace(PVR2_TRACE_ERROR_LEGS,
				   "Failure uploading firmware1");
		}
		return;
	}
	hdw->fw1_state = FW1_STATE_OK;

	if (initusbreset) {
		pvr2_hdw_device_reset(hdw);
	}
	if (!pvr2_hdw_dev_ok(hdw)) return;

	for (idx = 0; idx < pvr2_client_lists[hdw->hdw_type].cnt; idx++) {
		request_module(pvr2_client_lists[hdw->hdw_type].lst[idx]);
	}

	pvr2_hdw_cmd_powerup(hdw);
	if (!pvr2_hdw_dev_ok(hdw)) return;

	if (pvr2_upload_firmware2(hdw)){
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,"device unstable!!");
		pvr2_hdw_render_useless(hdw);
		return;
	}

	// This step MUST happen after the earlier powerup step.
	pvr2_i2c_core_init(hdw);
	if (!pvr2_hdw_dev_ok(hdw)) return;

	for (idx = 0; idx < CTRLDEF_COUNT; idx++) {
		cptr = hdw->controls + idx;
		if (cptr->info->skip_init) continue;
		if (!cptr->info->set_value) continue;
		cptr->info->set_value(cptr,~0,cptr->info->default_value);
	}

	// Do not use pvr2_reset_ctl_endpoints() here.  It is not
	// thread-safe against the normal pvr2_send_request() mechanism.
	// (We should make it thread safe).

	ret = pvr2_hdw_get_eeprom_addr(hdw);
	if (!pvr2_hdw_dev_ok(hdw)) return;
	if (ret < 0) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "Unable to determine location of eeprom, skipping");
	} else {
		hdw->eeprom_addr = ret;
		pvr2_eeprom_analyze(hdw);
		if (!pvr2_hdw_dev_ok(hdw)) return;
	}

	pvr2_hdw_setup_std(hdw);

	if (!get_default_tuner_type(hdw)) {
		pvr2_trace(PVR2_TRACE_INIT,
			   "pvr2_hdw_setup: Tuner type overridden to %d",
			   hdw->tuner_type);
	}

	hdw->tuner_updated = !0;
	pvr2_i2c_core_check_stale(hdw);
	hdw->tuner_updated = 0;

	if (!pvr2_hdw_dev_ok(hdw)) return;

	pvr2_hdw_commit_ctl_internal(hdw);
	if (!pvr2_hdw_dev_ok(hdw)) return;

	hdw->vid_stream = pvr2_stream_create();
	if (!pvr2_hdw_dev_ok(hdw)) return;
	pvr2_trace(PVR2_TRACE_INIT,
		   "pvr2_hdw_setup: video stream is %p",hdw->vid_stream);
	if (hdw->vid_stream) {
		idx = get_default_error_tolerance(hdw);
		if (idx) {
			pvr2_trace(PVR2_TRACE_INIT,
				   "pvr2_hdw_setup: video stream %p"
				   " setting tolerance %u",
				   hdw->vid_stream,idx);
		}
		pvr2_stream_setup(hdw->vid_stream,hdw->usb_dev,
				  PVR2_VID_ENDPOINT,idx);
	}

	if (!pvr2_hdw_dev_ok(hdw)) return;

	/* Make sure everything is up to date */
	pvr2_i2c_core_sync(hdw);

	if (!pvr2_hdw_dev_ok(hdw)) return;

	hdw->flag_init_ok = !0;
}


int pvr2_hdw_setup(struct pvr2_hdw *hdw)
{
	pvr2_trace(PVR2_TRACE_INIT,"pvr2_hdw_setup(hdw=%p) begin",hdw);
	LOCK_TAKE(hdw->big_lock); do {
		pvr2_hdw_setup_low(hdw);
		pvr2_trace(PVR2_TRACE_INIT,
			   "pvr2_hdw_setup(hdw=%p) done, ok=%d init_ok=%d",
			   hdw,hdw->flag_ok,hdw->flag_init_ok);
		if (pvr2_hdw_dev_ok(hdw)) {
			if (pvr2_hdw_init_ok(hdw)) {
				pvr2_trace(
					PVR2_TRACE_INFO,
					"Device initialization"
					" completed successfully.");
				break;
			}
			if (hdw->fw1_state == FW1_STATE_RELOAD) {
				pvr2_trace(
					PVR2_TRACE_INFO,
					"Device microcontroller firmware"
					" (re)loaded; it should now reset"
					" and reconnect.");
				break;
			}
			pvr2_trace(
				PVR2_TRACE_ERROR_LEGS,
				"Device initialization was not successful.");
			if (hdw->fw1_state == FW1_STATE_MISSING) {
				pvr2_trace(
					PVR2_TRACE_ERROR_LEGS,
					"Giving up since device"
					" microcontroller firmware"
					" appears to be missing.");
				break;
			}
		}
		if (procreload) {
			pvr2_trace(
				PVR2_TRACE_ERROR_LEGS,
				"Attempting pvrusb2 recovery by reloading"
				" primary firmware.");
			pvr2_trace(
				PVR2_TRACE_ERROR_LEGS,
				"If this works, device should disconnect"
				" and reconnect in a sane state.");
			hdw->fw1_state = FW1_STATE_UNKNOWN;
			pvr2_upload_firmware1(hdw);
		} else {
			pvr2_trace(
				PVR2_TRACE_ERROR_LEGS,
				"***WARNING*** pvrusb2 device hardware"
				" appears to be jammed"
				" and I can't clear it.");
			pvr2_trace(
				PVR2_TRACE_ERROR_LEGS,
				"You might need to power cycle"
				" the pvrusb2 device"
				" in order to recover.");
		}
	} while (0); LOCK_GIVE(hdw->big_lock);
	pvr2_trace(PVR2_TRACE_INIT,"pvr2_hdw_setup(hdw=%p) end",hdw);
	return hdw->flag_init_ok;
}


/* Create and return a structure for interacting with the underlying
   hardware */
struct pvr2_hdw *pvr2_hdw_create(struct usb_interface *intf,
				 const struct usb_device_id *devid)
{
	unsigned int idx,cnt1,cnt2;
	struct pvr2_hdw *hdw;
	unsigned int hdw_type;
	int valid_std_mask;
	struct pvr2_ctrl *cptr;
	__u8 ifnum;
	struct v4l2_queryctrl qctrl;
	struct pvr2_ctl_info *ciptr;

	hdw_type = devid - pvr2_device_table;
	if (hdw_type >=
	    sizeof(pvr2_device_names)/sizeof(pvr2_device_names[0])) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "Bogus device type of %u reported",hdw_type);
		return NULL;
	}

	hdw = kmalloc(sizeof(*hdw),GFP_KERNEL);
	pvr2_trace(PVR2_TRACE_INIT,"pvr2_hdw_create: hdw=%p, type \"%s\"",
		   hdw,pvr2_device_names[hdw_type]);
	if (!hdw) goto fail;
	memset(hdw,0,sizeof(*hdw));
	cx2341x_fill_defaults(&hdw->enc_ctl_state);

	hdw->control_cnt = CTRLDEF_COUNT;
	hdw->control_cnt += MPEGDEF_COUNT;
	hdw->controls = kmalloc(sizeof(struct pvr2_ctrl) * hdw->control_cnt,
				GFP_KERNEL);
	if (!hdw->controls) goto fail;
	memset(hdw->controls,0,sizeof(struct pvr2_ctrl) * hdw->control_cnt);
	hdw->hdw_type = hdw_type;
	for (idx = 0; idx < hdw->control_cnt; idx++) {
		cptr = hdw->controls + idx;
		cptr->hdw = hdw;
	}
	for (idx = 0; idx < 32; idx++) {
		hdw->std_mask_ptrs[idx] = hdw->std_mask_names[idx];
	}
	for (idx = 0; idx < CTRLDEF_COUNT; idx++) {
		cptr = hdw->controls + idx;
		cptr->info = control_defs+idx;
	}
	/* Define and configure additional controls from cx2341x module. */
	hdw->mpeg_ctrl_info = kmalloc(
		sizeof(*(hdw->mpeg_ctrl_info)) * MPEGDEF_COUNT, GFP_KERNEL);
	if (!hdw->mpeg_ctrl_info) goto fail;
	memset(hdw->mpeg_ctrl_info,0,
	       sizeof(*(hdw->mpeg_ctrl_info)) * MPEGDEF_COUNT);
	for (idx = 0; idx < MPEGDEF_COUNT; idx++) {
		cptr = hdw->controls + idx + CTRLDEF_COUNT;
		ciptr = &(hdw->mpeg_ctrl_info[idx].info);
		ciptr->desc = hdw->mpeg_ctrl_info[idx].desc;
		ciptr->name = mpeg_ids[idx].strid;
		ciptr->v4l_id = mpeg_ids[idx].id;
		ciptr->skip_init = !0;
		ciptr->get_value = ctrl_cx2341x_get;
		ciptr->get_v4lflags = ctrl_cx2341x_getv4lflags;
		ciptr->is_dirty = ctrl_cx2341x_is_dirty;
		if (!idx) ciptr->clear_dirty = ctrl_cx2341x_clear_dirty;
		qctrl.id = ciptr->v4l_id;
		cx2341x_ctrl_query(&hdw->enc_ctl_state,&qctrl);
		if (!(qctrl.flags & V4L2_CTRL_FLAG_READ_ONLY)) {
			ciptr->set_value = ctrl_cx2341x_set;
		}
		strncpy(hdw->mpeg_ctrl_info[idx].desc,qctrl.name,
			PVR2_CTLD_INFO_DESC_SIZE);
		hdw->mpeg_ctrl_info[idx].desc[PVR2_CTLD_INFO_DESC_SIZE-1] = 0;
		ciptr->default_value = qctrl.default_value;
		switch (qctrl.type) {
		default:
		case V4L2_CTRL_TYPE_INTEGER:
			ciptr->type = pvr2_ctl_int;
			ciptr->def.type_int.min_value = qctrl.minimum;
			ciptr->def.type_int.max_value = qctrl.maximum;
			break;
		case V4L2_CTRL_TYPE_BOOLEAN:
			ciptr->type = pvr2_ctl_bool;
			break;
		case V4L2_CTRL_TYPE_MENU:
			ciptr->type = pvr2_ctl_enum;
			ciptr->def.type_enum.value_names =
				cx2341x_ctrl_get_menu(ciptr->v4l_id);
			for (cnt1 = 0;
			     ciptr->def.type_enum.value_names[cnt1] != NULL;
			     cnt1++) { }
			ciptr->def.type_enum.count = cnt1;
			break;
		}
		cptr->info = ciptr;
	}

	// Initialize video standard enum dynamic control
	cptr = pvr2_hdw_get_ctrl_by_id(hdw,PVR2_CID_STDENUM);
	if (cptr) {
		memcpy(&hdw->std_info_enum,cptr->info,
		       sizeof(hdw->std_info_enum));
		cptr->info = &hdw->std_info_enum;

	}
	// Initialize control data regarding video standard masks
	valid_std_mask = pvr2_std_get_usable();
	for (idx = 0; idx < 32; idx++) {
		if (!(valid_std_mask & (1 << idx))) continue;
		cnt1 = pvr2_std_id_to_str(
			hdw->std_mask_names[idx],
			sizeof(hdw->std_mask_names[idx])-1,
			1 << idx);
		hdw->std_mask_names[idx][cnt1] = 0;
	}
	cptr = pvr2_hdw_get_ctrl_by_id(hdw,PVR2_CID_STDAVAIL);
	if (cptr) {
		memcpy(&hdw->std_info_avail,cptr->info,
		       sizeof(hdw->std_info_avail));
		cptr->info = &hdw->std_info_avail;
		hdw->std_info_avail.def.type_bitmask.bit_names =
			hdw->std_mask_ptrs;
		hdw->std_info_avail.def.type_bitmask.valid_bits =
			valid_std_mask;
	}
	cptr = pvr2_hdw_get_ctrl_by_id(hdw,PVR2_CID_STDCUR);
	if (cptr) {
		memcpy(&hdw->std_info_cur,cptr->info,
		       sizeof(hdw->std_info_cur));
		cptr->info = &hdw->std_info_cur;
		hdw->std_info_cur.def.type_bitmask.bit_names =
			hdw->std_mask_ptrs;
		hdw->std_info_avail.def.type_bitmask.valid_bits =
			valid_std_mask;
	}

	hdw->eeprom_addr = -1;
	hdw->unit_number = -1;
	hdw->v4l_minor_number = -1;
	hdw->ctl_write_buffer = kmalloc(PVR2_CTL_BUFFSIZE,GFP_KERNEL);
	if (!hdw->ctl_write_buffer) goto fail;
	hdw->ctl_read_buffer = kmalloc(PVR2_CTL_BUFFSIZE,GFP_KERNEL);
	if (!hdw->ctl_read_buffer) goto fail;
	hdw->ctl_write_urb = usb_alloc_urb(0,GFP_KERNEL);
	if (!hdw->ctl_write_urb) goto fail;
	hdw->ctl_read_urb = usb_alloc_urb(0,GFP_KERNEL);
	if (!hdw->ctl_read_urb) goto fail;

	down(&pvr2_unit_sem); do {
		for (idx = 0; idx < PVR_NUM; idx++) {
			if (unit_pointers[idx]) continue;
			hdw->unit_number = idx;
			unit_pointers[idx] = hdw;
			break;
		}
	} while (0); up(&pvr2_unit_sem);

	cnt1 = 0;
	cnt2 = scnprintf(hdw->name+cnt1,sizeof(hdw->name)-cnt1,"pvrusb2");
	cnt1 += cnt2;
	if (hdw->unit_number >= 0) {
		cnt2 = scnprintf(hdw->name+cnt1,sizeof(hdw->name)-cnt1,"_%c",
				 ('a' + hdw->unit_number));
		cnt1 += cnt2;
	}
	if (cnt1 >= sizeof(hdw->name)) cnt1 = sizeof(hdw->name)-1;
	hdw->name[cnt1] = 0;

	pvr2_trace(PVR2_TRACE_INIT,"Driver unit number is %d, name is %s",
		   hdw->unit_number,hdw->name);

	hdw->tuner_type = -1;
	hdw->flag_ok = !0;
	/* Initialize the mask of subsystems that we will shut down when we
	   stop streaming. */
	hdw->subsys_stream_mask = PVR2_SUBSYS_RUN_ALL;
	hdw->subsys_stream_mask |= (1<<PVR2_SUBSYS_B_ENC_CFG);

	pvr2_trace(PVR2_TRACE_INIT,"subsys_stream_mask: 0x%lx",
		   hdw->subsys_stream_mask);

	hdw->usb_intf = intf;
	hdw->usb_dev = interface_to_usbdev(intf);

	ifnum = hdw->usb_intf->cur_altsetting->desc.bInterfaceNumber;
	usb_set_interface(hdw->usb_dev,ifnum,0);

	mutex_init(&hdw->ctl_lock_mutex);
	mutex_init(&hdw->big_lock_mutex);

	return hdw;
 fail:
	if (hdw) {
		if (hdw->ctl_read_urb) usb_free_urb(hdw->ctl_read_urb);
		if (hdw->ctl_write_urb) usb_free_urb(hdw->ctl_write_urb);
		if (hdw->ctl_read_buffer) kfree(hdw->ctl_read_buffer);
		if (hdw->ctl_write_buffer) kfree(hdw->ctl_write_buffer);
		if (hdw->controls) kfree(hdw->controls);
		if (hdw->mpeg_ctrl_info) kfree(hdw->mpeg_ctrl_info);
		kfree(hdw);
	}
	return NULL;
}


/* Remove _all_ associations between this driver and the underlying USB
   layer. */
static void pvr2_hdw_remove_usb_stuff(struct pvr2_hdw *hdw)
{
	if (hdw->flag_disconnected) return;
	pvr2_trace(PVR2_TRACE_INIT,"pvr2_hdw_remove_usb_stuff: hdw=%p",hdw);
	if (hdw->ctl_read_urb) {
		usb_kill_urb(hdw->ctl_read_urb);
		usb_free_urb(hdw->ctl_read_urb);
		hdw->ctl_read_urb = NULL;
	}
	if (hdw->ctl_write_urb) {
		usb_kill_urb(hdw->ctl_write_urb);
		usb_free_urb(hdw->ctl_write_urb);
		hdw->ctl_write_urb = NULL;
	}
	if (hdw->ctl_read_buffer) {
		kfree(hdw->ctl_read_buffer);
		hdw->ctl_read_buffer = NULL;
	}
	if (hdw->ctl_write_buffer) {
		kfree(hdw->ctl_write_buffer);
		hdw->ctl_write_buffer = NULL;
	}
	pvr2_hdw_render_useless_unlocked(hdw);
	hdw->flag_disconnected = !0;
	hdw->usb_dev = NULL;
	hdw->usb_intf = NULL;
}


/* Destroy hardware interaction structure */
void pvr2_hdw_destroy(struct pvr2_hdw *hdw)
{
	pvr2_trace(PVR2_TRACE_INIT,"pvr2_hdw_destroy: hdw=%p",hdw);
	if (hdw->fw_buffer) {
		kfree(hdw->fw_buffer);
		hdw->fw_buffer = NULL;
	}
	if (hdw->vid_stream) {
		pvr2_stream_destroy(hdw->vid_stream);
		hdw->vid_stream = NULL;
	}
	if (hdw->audio_stat) {
		hdw->audio_stat->detach(hdw->audio_stat->ctxt);
	}
	if (hdw->decoder_ctrl) {
		hdw->decoder_ctrl->detach(hdw->decoder_ctrl->ctxt);
	}
	pvr2_i2c_core_done(hdw);
	pvr2_hdw_remove_usb_stuff(hdw);
	down(&pvr2_unit_sem); do {
		if ((hdw->unit_number >= 0) &&
		    (hdw->unit_number < PVR_NUM) &&
		    (unit_pointers[hdw->unit_number] == hdw)) {
			unit_pointers[hdw->unit_number] = NULL;
		}
	} while (0); up(&pvr2_unit_sem);
	if (hdw->controls) kfree(hdw->controls);
	if (hdw->mpeg_ctrl_info) kfree(hdw->mpeg_ctrl_info);
	if (hdw->std_defs) kfree(hdw->std_defs);
	if (hdw->std_enum_names) kfree(hdw->std_enum_names);
	kfree(hdw);
}


int pvr2_hdw_init_ok(struct pvr2_hdw *hdw)
{
	return hdw->flag_init_ok;
}


int pvr2_hdw_dev_ok(struct pvr2_hdw *hdw)
{
	return (hdw && hdw->flag_ok);
}


/* Called when hardware has been unplugged */
void pvr2_hdw_disconnect(struct pvr2_hdw *hdw)
{
	pvr2_trace(PVR2_TRACE_INIT,"pvr2_hdw_disconnect(hdw=%p)",hdw);
	LOCK_TAKE(hdw->big_lock);
	LOCK_TAKE(hdw->ctl_lock);
	pvr2_hdw_remove_usb_stuff(hdw);
	LOCK_GIVE(hdw->ctl_lock);
	LOCK_GIVE(hdw->big_lock);
}


// Attempt to autoselect an appropriate value for std_enum_cur given
// whatever is currently in std_mask_cur
static void pvr2_hdw_internal_find_stdenum(struct pvr2_hdw *hdw)
{
	unsigned int idx;
	for (idx = 1; idx < hdw->std_enum_cnt; idx++) {
		if (hdw->std_defs[idx-1].id == hdw->std_mask_cur) {
			hdw->std_enum_cur = idx;
			return;
		}
	}
	hdw->std_enum_cur = 0;
}


// Calculate correct set of enumerated standards based on currently known
// set of available standards bits.
static void pvr2_hdw_internal_set_std_avail(struct pvr2_hdw *hdw)
{
	struct v4l2_standard *newstd;
	unsigned int std_cnt;
	unsigned int idx;

	newstd = pvr2_std_create_enum(&std_cnt,hdw->std_mask_avail);

	if (hdw->std_defs) {
		kfree(hdw->std_defs);
		hdw->std_defs = NULL;
	}
	hdw->std_enum_cnt = 0;
	if (hdw->std_enum_names) {
		kfree(hdw->std_enum_names);
		hdw->std_enum_names = NULL;
	}

	if (!std_cnt) {
		pvr2_trace(
			PVR2_TRACE_ERROR_LEGS,
			"WARNING: Failed to identify any viable standards");
	}
	hdw->std_enum_names = kmalloc(sizeof(char *)*(std_cnt+1),GFP_KERNEL);
	hdw->std_enum_names[0] = "none";
	for (idx = 0; idx < std_cnt; idx++) {
		hdw->std_enum_names[idx+1] =
			newstd[idx].name;
	}
	// Set up the dynamic control for this standard
	hdw->std_info_enum.def.type_enum.value_names = hdw->std_enum_names;
	hdw->std_info_enum.def.type_enum.count = std_cnt+1;
	hdw->std_defs = newstd;
	hdw->std_enum_cnt = std_cnt+1;
	hdw->std_enum_cur = 0;
	hdw->std_info_cur.def.type_bitmask.valid_bits = hdw->std_mask_avail;
}


int pvr2_hdw_get_stdenum_value(struct pvr2_hdw *hdw,
			       struct v4l2_standard *std,
			       unsigned int idx)
{
	int ret = -EINVAL;
	if (!idx) return ret;
	LOCK_TAKE(hdw->big_lock); do {
		if (idx >= hdw->std_enum_cnt) break;
		idx--;
		memcpy(std,hdw->std_defs+idx,sizeof(*std));
		ret = 0;
	} while (0); LOCK_GIVE(hdw->big_lock);
	return ret;
}


/* Get the number of defined controls */
unsigned int pvr2_hdw_get_ctrl_count(struct pvr2_hdw *hdw)
{
	return hdw->control_cnt;
}


/* Retrieve a control handle given its index (0..count-1) */
struct pvr2_ctrl *pvr2_hdw_get_ctrl_by_index(struct pvr2_hdw *hdw,
					     unsigned int idx)
{
	if (idx >= hdw->control_cnt) return NULL;
	return hdw->controls + idx;
}


/* Retrieve a control handle given its index (0..count-1) */
struct pvr2_ctrl *pvr2_hdw_get_ctrl_by_id(struct pvr2_hdw *hdw,
					  unsigned int ctl_id)
{
	struct pvr2_ctrl *cptr;
	unsigned int idx;
	int i;

	/* This could be made a lot more efficient, but for now... */
	for (idx = 0; idx < hdw->control_cnt; idx++) {
		cptr = hdw->controls + idx;
		i = cptr->info->internal_id;
		if (i && (i == ctl_id)) return cptr;
	}
	return NULL;
}


/* Given a V4L ID, retrieve the control structure associated with it. */
struct pvr2_ctrl *pvr2_hdw_get_ctrl_v4l(struct pvr2_hdw *hdw,unsigned int ctl_id)
{
	struct pvr2_ctrl *cptr;
	unsigned int idx;
	int i;

	/* This could be made a lot more efficient, but for now... */
	for (idx = 0; idx < hdw->control_cnt; idx++) {
		cptr = hdw->controls + idx;
		i = cptr->info->v4l_id;
		if (i && (i == ctl_id)) return cptr;
	}
	return NULL;
}


/* Given a V4L ID for its immediate predecessor, retrieve the control
   structure associated with it. */
struct pvr2_ctrl *pvr2_hdw_get_ctrl_nextv4l(struct pvr2_hdw *hdw,
					    unsigned int ctl_id)
{
	struct pvr2_ctrl *cptr,*cp2;
	unsigned int idx;
	int i;

	/* This could be made a lot more efficient, but for now... */
	cp2 = NULL;
	for (idx = 0; idx < hdw->control_cnt; idx++) {
		cptr = hdw->controls + idx;
		i = cptr->info->v4l_id;
		if (!i) continue;
		if (i <= ctl_id) continue;
		if (cp2 && (cp2->info->v4l_id < i)) continue;
		cp2 = cptr;
	}
	return cp2;
	return NULL;
}


static const char *get_ctrl_typename(enum pvr2_ctl_type tp)
{
	switch (tp) {
	case pvr2_ctl_int: return "integer";
	case pvr2_ctl_enum: return "enum";
	case pvr2_ctl_bool: return "boolean";
	case pvr2_ctl_bitmask: return "bitmask";
	}
	return "";
}


/* Commit all control changes made up to this point.  Subsystems can be
   indirectly affected by these changes.  For a given set of things being
   committed, we'll clear the affected subsystem bits and then once we're
   done committing everything we'll make a request to restore the subsystem
   state(s) back to their previous value before this function was called.
   Thus we can automatically reconfigure affected pieces of the driver as
   controls are changed. */
static int pvr2_hdw_commit_ctl_internal(struct pvr2_hdw *hdw)
{
	unsigned long saved_subsys_mask = hdw->subsys_enabled_mask;
	unsigned long stale_subsys_mask = 0;
	unsigned int idx;
	struct pvr2_ctrl *cptr;
	int value;
	int commit_flag = 0;
	char buf[100];
	unsigned int bcnt,ccnt;

	for (idx = 0; idx < hdw->control_cnt; idx++) {
		cptr = hdw->controls + idx;
		if (cptr->info->is_dirty == 0) continue;
		if (!cptr->info->is_dirty(cptr)) continue;
		if (!commit_flag) {
			commit_flag = !0;
		}

		bcnt = scnprintf(buf,sizeof(buf),"\"%s\" <-- ",
				 cptr->info->name);
		value = 0;
		cptr->info->get_value(cptr,&value);
		pvr2_ctrl_value_to_sym_internal(cptr,~0,value,
						buf+bcnt,
						sizeof(buf)-bcnt,&ccnt);
		bcnt += ccnt;
		bcnt += scnprintf(buf+bcnt,sizeof(buf)-bcnt," <%s>",
				  get_ctrl_typename(cptr->info->type));
		pvr2_trace(PVR2_TRACE_CTL,
			   "/*--TRACE_COMMIT--*/ %.*s",
			   bcnt,buf);
	}

	if (!commit_flag) {
		/* Nothing has changed */
		return 0;
	}

	/* When video standard changes, reset the hres and vres values -
	   but if the user has pending changes there, then let the changes
	   take priority. */
	if (hdw->std_dirty) {
		/* Rewrite the vertical resolution to be appropriate to the
		   video standard that has been selected. */
		int nvres;
		if (hdw->std_mask_cur & V4L2_STD_525_60) {
			nvres = 480;
		} else {
			nvres = 576;
		}
		if (nvres != hdw->res_ver_val) {
			hdw->res_ver_val = nvres;
			hdw->res_ver_dirty = !0;
		}
	}

	if (hdw->std_dirty ||
	    hdw->enc_stale ||
	    hdw->srate_dirty ||
	    hdw->res_ver_dirty ||
	    hdw->res_hor_dirty ||
	    0) {
		/* If any of this changes, then the encoder needs to be
		   reconfigured, and we need to reset the stream. */
		stale_subsys_mask |= (1<<PVR2_SUBSYS_B_ENC_CFG);
	}

	if (hdw->srate_dirty) {
		/* Write new sample rate into control structure since
		 * the master copy is stale.  We must track srate
		 * separate from the mpeg control structure because
		 * other logic also uses this value. */
		struct v4l2_ext_controls cs;
		struct v4l2_ext_control c1;
		memset(&cs,0,sizeof(cs));
		memset(&c1,0,sizeof(c1));
		cs.controls = &c1;
		cs.count = 1;
		c1.id = V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ;
		c1.value = hdw->srate_val;
		cx2341x_ext_ctrls(&hdw->enc_ctl_state,&cs,VIDIOC_S_EXT_CTRLS);
	}

	/* Scan i2c core at this point - before we clear all the dirty
	   bits.  Various parts of the i2c core will notice dirty bits as
	   appropriate and arrange to broadcast or directly send updates to
	   the client drivers in order to keep everything in sync */
	pvr2_i2c_core_check_stale(hdw);

	for (idx = 0; idx < hdw->control_cnt; idx++) {
		cptr = hdw->controls + idx;
		if (!cptr->info->clear_dirty) continue;
		cptr->info->clear_dirty(cptr);
	}

	/* Now execute i2c core update */
	pvr2_i2c_core_sync(hdw);

	pvr2_hdw_subsys_bit_chg_no_lock(hdw,stale_subsys_mask,0);
	pvr2_hdw_subsys_bit_chg_no_lock(hdw,~0,saved_subsys_mask);

	return 0;
}


int pvr2_hdw_commit_ctl(struct pvr2_hdw *hdw)
{
	LOCK_TAKE(hdw->big_lock); do {
		pvr2_hdw_commit_ctl_internal(hdw);
	} while (0); LOCK_GIVE(hdw->big_lock);
	return 0;
}


void pvr2_hdw_poll(struct pvr2_hdw *hdw)
{
	LOCK_TAKE(hdw->big_lock); do {
		pvr2_i2c_core_sync(hdw);
	} while (0); LOCK_GIVE(hdw->big_lock);
}


void pvr2_hdw_setup_poll_trigger(struct pvr2_hdw *hdw,
				 void (*func)(void *),
				 void *data)
{
	LOCK_TAKE(hdw->big_lock); do {
		hdw->poll_trigger_func = func;
		hdw->poll_trigger_data = data;
	} while (0); LOCK_GIVE(hdw->big_lock);
}


void pvr2_hdw_poll_trigger_unlocked(struct pvr2_hdw *hdw)
{
	if (hdw->poll_trigger_func) {
		hdw->poll_trigger_func(hdw->poll_trigger_data);
	}
}

/* Return name for this driver instance */
const char *pvr2_hdw_get_driver_name(struct pvr2_hdw *hdw)
{
	return hdw->name;
}


/* Return bit mask indicating signal status */
static unsigned int pvr2_hdw_get_signal_status_internal(struct pvr2_hdw *hdw)
{
	unsigned int msk = 0;
	switch (hdw->input_val) {
	case PVR2_CVAL_INPUT_TV:
	case PVR2_CVAL_INPUT_RADIO:
		if (hdw->decoder_ctrl &&
		    hdw->decoder_ctrl->tuned(hdw->decoder_ctrl->ctxt)) {
			msk |= PVR2_SIGNAL_OK;
			if (hdw->audio_stat &&
			    hdw->audio_stat->status(hdw->audio_stat->ctxt)) {
				if (hdw->flag_stereo) {
					msk |= PVR2_SIGNAL_STEREO;
				}
				if (hdw->flag_bilingual) {
					msk |= PVR2_SIGNAL_SAP;
				}
			}
		}
		break;
	default:
		msk |= PVR2_SIGNAL_OK | PVR2_SIGNAL_STEREO;
	}
	return msk;
}


int pvr2_hdw_is_hsm(struct pvr2_hdw *hdw)
{
	int result;
	LOCK_TAKE(hdw->ctl_lock); do {
		hdw->cmd_buffer[0] = 0x0b;
		result = pvr2_send_request(hdw,
					   hdw->cmd_buffer,1,
					   hdw->cmd_buffer,1);
		if (result < 0) break;
		result = (hdw->cmd_buffer[0] != 0);
	} while(0); LOCK_GIVE(hdw->ctl_lock);
	return result;
}


/* Return bit mask indicating signal status */
unsigned int pvr2_hdw_get_signal_status(struct pvr2_hdw *hdw)
{
	unsigned int msk = 0;
	LOCK_TAKE(hdw->big_lock); do {
		msk = pvr2_hdw_get_signal_status_internal(hdw);
	} while (0); LOCK_GIVE(hdw->big_lock);
	return msk;
}


/* Get handle to video output stream */
struct pvr2_stream *pvr2_hdw_get_video_stream(struct pvr2_hdw *hp)
{
	return hp->vid_stream;
}


void pvr2_hdw_trigger_module_log(struct pvr2_hdw *hdw)
{
	int nr = pvr2_hdw_get_unit_number(hdw);
	LOCK_TAKE(hdw->big_lock); do {
		hdw->log_requested = !0;
		printk(KERN_INFO "pvrusb2: =================  START STATUS CARD #%d  =================\n", nr);
		pvr2_i2c_core_check_stale(hdw);
		hdw->log_requested = 0;
		pvr2_i2c_core_sync(hdw);
		pvr2_trace(PVR2_TRACE_INFO,"cx2341x config:");
		cx2341x_log_status(&hdw->enc_ctl_state, "pvrusb2");
		printk(KERN_INFO "pvrusb2: ==================  END STATUS CARD #%d  ==================\n", nr);
	} while (0); LOCK_GIVE(hdw->big_lock);
}

void pvr2_hdw_cpufw_set_enabled(struct pvr2_hdw *hdw, int enable_flag)
{
	int ret;
	u16 address;
	unsigned int pipe;
	LOCK_TAKE(hdw->big_lock); do {
		if ((hdw->fw_buffer == 0) == !enable_flag) break;

		if (!enable_flag) {
			pvr2_trace(PVR2_TRACE_FIRMWARE,
				   "Cleaning up after CPU firmware fetch");
			kfree(hdw->fw_buffer);
			hdw->fw_buffer = NULL;
			hdw->fw_size = 0;
			/* Now release the CPU.  It will disconnect and
			   reconnect later. */
			pvr2_hdw_cpureset_assert(hdw,0);
			break;
		}

		pvr2_trace(PVR2_TRACE_FIRMWARE,
			   "Preparing to suck out CPU firmware");
		hdw->fw_size = 0x2000;
		hdw->fw_buffer = kmalloc(hdw->fw_size,GFP_KERNEL);
		if (!hdw->fw_buffer) {
			hdw->fw_size = 0;
			break;
		}

		memset(hdw->fw_buffer,0,hdw->fw_size);

		/* We have to hold the CPU during firmware upload. */
		pvr2_hdw_cpureset_assert(hdw,1);

		/* download the firmware from address 0000-1fff in 2048
		   (=0x800) bytes chunk. */

		pvr2_trace(PVR2_TRACE_FIRMWARE,"Grabbing CPU firmware");
		pipe = usb_rcvctrlpipe(hdw->usb_dev, 0);
		for(address = 0; address < hdw->fw_size; address += 0x800) {
			ret = usb_control_msg(hdw->usb_dev,pipe,0xa0,0xc0,
					      address,0,
					      hdw->fw_buffer+address,0x800,HZ);
			if (ret < 0) break;
		}

		pvr2_trace(PVR2_TRACE_FIRMWARE,"Done grabbing CPU firmware");

	} while (0); LOCK_GIVE(hdw->big_lock);
}


/* Return true if we're in a mode for retrieval CPU firmware */
int pvr2_hdw_cpufw_get_enabled(struct pvr2_hdw *hdw)
{
	return hdw->fw_buffer != 0;
}


int pvr2_hdw_cpufw_get(struct pvr2_hdw *hdw,unsigned int offs,
		       char *buf,unsigned int cnt)
{
	int ret = -EINVAL;
	LOCK_TAKE(hdw->big_lock); do {
		if (!buf) break;
		if (!cnt) break;

		if (!hdw->fw_buffer) {
			ret = -EIO;
			break;
		}

		if (offs >= hdw->fw_size) {
			pvr2_trace(PVR2_TRACE_FIRMWARE,
				   "Read firmware data offs=%d EOF",
				   offs);
			ret = 0;
			break;
		}

		if (offs + cnt > hdw->fw_size) cnt = hdw->fw_size - offs;

		memcpy(buf,hdw->fw_buffer+offs,cnt);

		pvr2_trace(PVR2_TRACE_FIRMWARE,
			   "Read firmware data offs=%d cnt=%d",
			   offs,cnt);
		ret = cnt;
	} while (0); LOCK_GIVE(hdw->big_lock);

	return ret;
}


int pvr2_hdw_v4l_get_minor_number(struct pvr2_hdw *hdw)
{
	return hdw->v4l_minor_number;
}


/* Store the v4l minor device number */
void pvr2_hdw_v4l_store_minor_number(struct pvr2_hdw *hdw,int v)
{
	hdw->v4l_minor_number = v;
}


static void pvr2_ctl_write_complete(struct urb *urb)
{
	struct pvr2_hdw *hdw = urb->context;
	hdw->ctl_write_pend_flag = 0;
	if (hdw->ctl_read_pend_flag) return;
	complete(&hdw->ctl_done);
}


static void pvr2_ctl_read_complete(struct urb *urb)
{
	struct pvr2_hdw *hdw = urb->context;
	hdw->ctl_read_pend_flag = 0;
	if (hdw->ctl_write_pend_flag) return;
	complete(&hdw->ctl_done);
}


static void pvr2_ctl_timeout(unsigned long data)
{
	struct pvr2_hdw *hdw = (struct pvr2_hdw *)data;
	if (hdw->ctl_write_pend_flag || hdw->ctl_read_pend_flag) {
		hdw->ctl_timeout_flag = !0;
		if (hdw->ctl_write_pend_flag && hdw->ctl_write_urb) {
			usb_unlink_urb(hdw->ctl_write_urb);
		}
		if (hdw->ctl_read_pend_flag && hdw->ctl_read_urb) {
			usb_unlink_urb(hdw->ctl_read_urb);
		}
	}
}


/* Issue a command and get a response from the device.  This extended
   version includes a probe flag (which if set means that device errors
   should not be logged or treated as fatal) and a timeout in jiffies.
   This can be used to non-lethally probe the health of endpoint 1. */
static int pvr2_send_request_ex(struct pvr2_hdw *hdw,
				unsigned int timeout,int probe_fl,
				void *write_data,unsigned int write_len,
				void *read_data,unsigned int read_len)
{
	unsigned int idx;
	int status = 0;
	struct timer_list timer;
	if (!hdw->ctl_lock_held) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "Attempted to execute control transfer"
			   " without lock!!");
		return -EDEADLK;
	}
	if ((!hdw->flag_ok) && !probe_fl) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "Attempted to execute control transfer"
			   " when device not ok");
		return -EIO;
	}
	if (!(hdw->ctl_read_urb && hdw->ctl_write_urb)) {
		if (!probe_fl) {
			pvr2_trace(PVR2_TRACE_ERROR_LEGS,
				   "Attempted to execute control transfer"
				   " when USB is disconnected");
		}
		return -ENOTTY;
	}

	/* Ensure that we have sane parameters */
	if (!write_data) write_len = 0;
	if (!read_data) read_len = 0;
	if (write_len > PVR2_CTL_BUFFSIZE) {
		pvr2_trace(
			PVR2_TRACE_ERROR_LEGS,
			"Attempted to execute %d byte"
			" control-write transfer (limit=%d)",
			write_len,PVR2_CTL_BUFFSIZE);
		return -EINVAL;
	}
	if (read_len > PVR2_CTL_BUFFSIZE) {
		pvr2_trace(
			PVR2_TRACE_ERROR_LEGS,
			"Attempted to execute %d byte"
			" control-read transfer (limit=%d)",
			write_len,PVR2_CTL_BUFFSIZE);
		return -EINVAL;
	}
	if ((!write_len) && (!read_len)) {
		pvr2_trace(
			PVR2_TRACE_ERROR_LEGS,
			"Attempted to execute null control transfer?");
		return -EINVAL;
	}


	hdw->cmd_debug_state = 1;
	if (write_len) {
		hdw->cmd_debug_code = ((unsigned char *)write_data)[0];
	} else {
		hdw->cmd_debug_code = 0;
	}
	hdw->cmd_debug_write_len = write_len;
	hdw->cmd_debug_read_len = read_len;

	/* Initialize common stuff */
	init_completion(&hdw->ctl_done);
	hdw->ctl_timeout_flag = 0;
	hdw->ctl_write_pend_flag = 0;
	hdw->ctl_read_pend_flag = 0;
	init_timer(&timer);
	timer.expires = jiffies + timeout;
	timer.data = (unsigned long)hdw;
	timer.function = pvr2_ctl_timeout;

	if (write_len) {
		hdw->cmd_debug_state = 2;
		/* Transfer write data to internal buffer */
		for (idx = 0; idx < write_len; idx++) {
			hdw->ctl_write_buffer[idx] =
				((unsigned char *)write_data)[idx];
		}
		/* Initiate a write request */
		usb_fill_bulk_urb(hdw->ctl_write_urb,
				  hdw->usb_dev,
				  usb_sndbulkpipe(hdw->usb_dev,
						  PVR2_CTL_WRITE_ENDPOINT),
				  hdw->ctl_write_buffer,
				  write_len,
				  pvr2_ctl_write_complete,
				  hdw);
		hdw->ctl_write_urb->actual_length = 0;
		hdw->ctl_write_pend_flag = !0;
		status = usb_submit_urb(hdw->ctl_write_urb,GFP_KERNEL);
		if (status < 0) {
			pvr2_trace(PVR2_TRACE_ERROR_LEGS,
				   "Failed to submit write-control"
				   " URB status=%d",status);
			hdw->ctl_write_pend_flag = 0;
			goto done;
		}
	}

	if (read_len) {
		hdw->cmd_debug_state = 3;
		memset(hdw->ctl_read_buffer,0x43,read_len);
		/* Initiate a read request */
		usb_fill_bulk_urb(hdw->ctl_read_urb,
				  hdw->usb_dev,
				  usb_rcvbulkpipe(hdw->usb_dev,
						  PVR2_CTL_READ_ENDPOINT),
				  hdw->ctl_read_buffer,
				  read_len,
				  pvr2_ctl_read_complete,
				  hdw);
		hdw->ctl_read_urb->actual_length = 0;
		hdw->ctl_read_pend_flag = !0;
		status = usb_submit_urb(hdw->ctl_read_urb,GFP_KERNEL);
		if (status < 0) {
			pvr2_trace(PVR2_TRACE_ERROR_LEGS,
				   "Failed to submit read-control"
				   " URB status=%d",status);
			hdw->ctl_read_pend_flag = 0;
			goto done;
		}
	}

	/* Start timer */
	add_timer(&timer);

	/* Now wait for all I/O to complete */
	hdw->cmd_debug_state = 4;
	while (hdw->ctl_write_pend_flag || hdw->ctl_read_pend_flag) {
		wait_for_completion(&hdw->ctl_done);
	}
	hdw->cmd_debug_state = 5;

	/* Stop timer */
	del_timer_sync(&timer);

	hdw->cmd_debug_state = 6;
	status = 0;

	if (hdw->ctl_timeout_flag) {
		status = -ETIMEDOUT;
		if (!probe_fl) {
			pvr2_trace(PVR2_TRACE_ERROR_LEGS,
				   "Timed out control-write");
		}
		goto done;
	}

	if (write_len) {
		/* Validate results of write request */
		if ((hdw->ctl_write_urb->status != 0) &&
		    (hdw->ctl_write_urb->status != -ENOENT) &&
		    (hdw->ctl_write_urb->status != -ESHUTDOWN) &&
		    (hdw->ctl_write_urb->status != -ECONNRESET)) {
			/* USB subsystem is reporting some kind of failure
			   on the write */
			status = hdw->ctl_write_urb->status;
			if (!probe_fl) {
				pvr2_trace(PVR2_TRACE_ERROR_LEGS,
					   "control-write URB failure,"
					   " status=%d",
					   status);
			}
			goto done;
		}
		if (hdw->ctl_write_urb->actual_length < write_len) {
			/* Failed to write enough data */
			status = -EIO;
			if (!probe_fl) {
				pvr2_trace(PVR2_TRACE_ERROR_LEGS,
					   "control-write URB short,"
					   " expected=%d got=%d",
					   write_len,
					   hdw->ctl_write_urb->actual_length);
			}
			goto done;
		}
	}
	if (read_len) {
		/* Validate results of read request */
		if ((hdw->ctl_read_urb->status != 0) &&
		    (hdw->ctl_read_urb->status != -ENOENT) &&
		    (hdw->ctl_read_urb->status != -ESHUTDOWN) &&
		    (hdw->ctl_read_urb->status != -ECONNRESET)) {
			/* USB subsystem is reporting some kind of failure
			   on the read */
			status = hdw->ctl_read_urb->status;
			if (!probe_fl) {
				pvr2_trace(PVR2_TRACE_ERROR_LEGS,
					   "control-read URB failure,"
					   " status=%d",
					   status);
			}
			goto done;
		}
		if (hdw->ctl_read_urb->actual_length < read_len) {
			/* Failed to read enough data */
			status = -EIO;
			if (!probe_fl) {
				pvr2_trace(PVR2_TRACE_ERROR_LEGS,
					   "control-read URB short,"
					   " expected=%d got=%d",
					   read_len,
					   hdw->ctl_read_urb->actual_length);
			}
			goto done;
		}
		/* Transfer retrieved data out from internal buffer */
		for (idx = 0; idx < read_len; idx++) {
			((unsigned char *)read_data)[idx] =
				hdw->ctl_read_buffer[idx];
		}
	}

 done:

	hdw->cmd_debug_state = 0;
	if ((status < 0) && (!probe_fl)) {
		pvr2_hdw_render_useless_unlocked(hdw);
	}
	return status;
}


int pvr2_send_request(struct pvr2_hdw *hdw,
		      void *write_data,unsigned int write_len,
		      void *read_data,unsigned int read_len)
{
	return pvr2_send_request_ex(hdw,HZ*4,0,
				    write_data,write_len,
				    read_data,read_len);
}

int pvr2_write_register(struct pvr2_hdw *hdw, u16 reg, u32 data)
{
	int ret;

	LOCK_TAKE(hdw->ctl_lock);

	hdw->cmd_buffer[0] = 0x04;  /* write register prefix */
	PVR2_DECOMPOSE_LE(hdw->cmd_buffer,1,data);
	hdw->cmd_buffer[5] = 0;
	hdw->cmd_buffer[6] = (reg >> 8) & 0xff;
	hdw->cmd_buffer[7] = reg & 0xff;


	ret = pvr2_send_request(hdw, hdw->cmd_buffer, 8, hdw->cmd_buffer, 0);

	LOCK_GIVE(hdw->ctl_lock);

	return ret;
}


static int pvr2_read_register(struct pvr2_hdw *hdw, u16 reg, u32 *data)
{
	int ret = 0;

	LOCK_TAKE(hdw->ctl_lock);

	hdw->cmd_buffer[0] = 0x05;  /* read register prefix */
	hdw->cmd_buffer[1] = 0;
	hdw->cmd_buffer[2] = 0;
	hdw->cmd_buffer[3] = 0;
	hdw->cmd_buffer[4] = 0;
	hdw->cmd_buffer[5] = 0;
	hdw->cmd_buffer[6] = (reg >> 8) & 0xff;
	hdw->cmd_buffer[7] = reg & 0xff;

	ret |= pvr2_send_request(hdw, hdw->cmd_buffer, 8, hdw->cmd_buffer, 4);
	*data = PVR2_COMPOSE_LE(hdw->cmd_buffer,0);

	LOCK_GIVE(hdw->ctl_lock);

	return ret;
}


static int pvr2_write_u16(struct pvr2_hdw *hdw, u16 data, int res)
{
	int ret;

	LOCK_TAKE(hdw->ctl_lock);

	hdw->cmd_buffer[0] = (data >> 8) & 0xff;
	hdw->cmd_buffer[1] = data & 0xff;

	ret = pvr2_send_request(hdw, hdw->cmd_buffer, 2, hdw->cmd_buffer, res);

	LOCK_GIVE(hdw->ctl_lock);

	return ret;
}


static int pvr2_write_u8(struct pvr2_hdw *hdw, u8 data, int res)
{
	int ret;

	LOCK_TAKE(hdw->ctl_lock);

	hdw->cmd_buffer[0] = data;

	ret = pvr2_send_request(hdw, hdw->cmd_buffer, 1, hdw->cmd_buffer, res);

	LOCK_GIVE(hdw->ctl_lock);

	return ret;
}


static void pvr2_hdw_render_useless_unlocked(struct pvr2_hdw *hdw)
{
	if (!hdw->flag_ok) return;
	pvr2_trace(PVR2_TRACE_INIT,"render_useless");
	hdw->flag_ok = 0;
	if (hdw->vid_stream) {
		pvr2_stream_setup(hdw->vid_stream,NULL,0,0);
	}
	hdw->flag_streaming_enabled = 0;
	hdw->subsys_enabled_mask = 0;
}


void pvr2_hdw_render_useless(struct pvr2_hdw *hdw)
{
	LOCK_TAKE(hdw->ctl_lock);
	pvr2_hdw_render_useless_unlocked(hdw);
	LOCK_GIVE(hdw->ctl_lock);
}


void pvr2_hdw_device_reset(struct pvr2_hdw *hdw)
{
	int ret;
	pvr2_trace(PVR2_TRACE_INIT,"Performing a device reset...");
	ret = usb_lock_device_for_reset(hdw->usb_dev,NULL);
	if (ret == 1) {
		ret = usb_reset_device(hdw->usb_dev);
		usb_unlock_device(hdw->usb_dev);
	} else {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "Failed to lock USB device ret=%d",ret);
	}
	if (init_pause_msec) {
		pvr2_trace(PVR2_TRACE_INFO,
			   "Waiting %u msec for hardware to settle",
			   init_pause_msec);
		msleep(init_pause_msec);
	}

}


void pvr2_hdw_cpureset_assert(struct pvr2_hdw *hdw,int val)
{
	char da[1];
	unsigned int pipe;
	int ret;

	if (!hdw->usb_dev) return;

	pvr2_trace(PVR2_TRACE_INIT,"cpureset_assert(%d)",val);

	da[0] = val ? 0x01 : 0x00;

	/* Write the CPUCS register on the 8051.  The lsb of the register
	   is the reset bit; a 1 asserts reset while a 0 clears it. */
	pipe = usb_sndctrlpipe(hdw->usb_dev, 0);
	ret = usb_control_msg(hdw->usb_dev,pipe,0xa0,0x40,0xe600,0,da,1,HZ);
	if (ret < 0) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "cpureset_assert(%d) error=%d",val,ret);
		pvr2_hdw_render_useless(hdw);
	}
}


int pvr2_hdw_cmd_deep_reset(struct pvr2_hdw *hdw)
{
	int status;
	LOCK_TAKE(hdw->ctl_lock); do {
		pvr2_trace(PVR2_TRACE_INIT,"Requesting uproc hard reset");
		hdw->flag_ok = !0;
		hdw->cmd_buffer[0] = 0xdd;
		status = pvr2_send_request(hdw,hdw->cmd_buffer,1,NULL,0);
	} while (0); LOCK_GIVE(hdw->ctl_lock);
	return status;
}


int pvr2_hdw_cmd_powerup(struct pvr2_hdw *hdw)
{
	int status;
	LOCK_TAKE(hdw->ctl_lock); do {
		pvr2_trace(PVR2_TRACE_INIT,"Requesting powerup");
		hdw->cmd_buffer[0] = 0xde;
		status = pvr2_send_request(hdw,hdw->cmd_buffer,1,NULL,0);
	} while (0); LOCK_GIVE(hdw->ctl_lock);
	return status;
}


int pvr2_hdw_cmd_decoder_reset(struct pvr2_hdw *hdw)
{
	if (!hdw->decoder_ctrl) {
		pvr2_trace(PVR2_TRACE_INIT,
			   "Unable to reset decoder: nothing attached");
		return -ENOTTY;
	}

	if (!hdw->decoder_ctrl->force_reset) {
		pvr2_trace(PVR2_TRACE_INIT,
			   "Unable to reset decoder: not implemented");
		return -ENOTTY;
	}

	pvr2_trace(PVR2_TRACE_INIT,
		   "Requesting decoder reset");
	hdw->decoder_ctrl->force_reset(hdw->decoder_ctrl->ctxt);
	return 0;
}


/* Stop / start video stream transport */
static int pvr2_hdw_cmd_usbstream(struct pvr2_hdw *hdw,int runFl)
{
	int status;
	LOCK_TAKE(hdw->ctl_lock); do {
		hdw->cmd_buffer[0] = (runFl ? 0x36 : 0x37);
		status = pvr2_send_request(hdw,hdw->cmd_buffer,1,NULL,0);
	} while (0); LOCK_GIVE(hdw->ctl_lock);
	if (!status) {
		hdw->subsys_enabled_mask =
			((hdw->subsys_enabled_mask &
			  ~(1<<PVR2_SUBSYS_B_USBSTREAM_RUN)) |
			 (runFl ? (1<<PVR2_SUBSYS_B_USBSTREAM_RUN) : 0));
	}
	return status;
}


void pvr2_hdw_get_debug_info(const struct pvr2_hdw *hdw,
			     struct pvr2_hdw_debug_info *ptr)
{
	ptr->big_lock_held = hdw->big_lock_held;
	ptr->ctl_lock_held = hdw->ctl_lock_held;
	ptr->flag_ok = hdw->flag_ok;
	ptr->flag_disconnected = hdw->flag_disconnected;
	ptr->flag_init_ok = hdw->flag_init_ok;
	ptr->flag_streaming_enabled = hdw->flag_streaming_enabled;
	ptr->subsys_flags = hdw->subsys_enabled_mask;
	ptr->cmd_debug_state = hdw->cmd_debug_state;
	ptr->cmd_code = hdw->cmd_debug_code;
	ptr->cmd_debug_write_len = hdw->cmd_debug_write_len;
	ptr->cmd_debug_read_len = hdw->cmd_debug_read_len;
	ptr->cmd_debug_timeout = hdw->ctl_timeout_flag;
	ptr->cmd_debug_write_pend = hdw->ctl_write_pend_flag;
	ptr->cmd_debug_read_pend = hdw->ctl_read_pend_flag;
	ptr->cmd_debug_rstatus = hdw->ctl_read_urb->status;
	ptr->cmd_debug_wstatus = hdw->ctl_read_urb->status;
}


int pvr2_hdw_gpio_get_dir(struct pvr2_hdw *hdw,u32 *dp)
{
	return pvr2_read_register(hdw,PVR2_GPIO_DIR,dp);
}


int pvr2_hdw_gpio_get_out(struct pvr2_hdw *hdw,u32 *dp)
{
	return pvr2_read_register(hdw,PVR2_GPIO_OUT,dp);
}


int pvr2_hdw_gpio_get_in(struct pvr2_hdw *hdw,u32 *dp)
{
	return pvr2_read_register(hdw,PVR2_GPIO_IN,dp);
}


int pvr2_hdw_gpio_chg_dir(struct pvr2_hdw *hdw,u32 msk,u32 val)
{
	u32 cval,nval;
	int ret;
	if (~msk) {
		ret = pvr2_read_register(hdw,PVR2_GPIO_DIR,&cval);
		if (ret) return ret;
		nval = (cval & ~msk) | (val & msk);
		pvr2_trace(PVR2_TRACE_GPIO,
			   "GPIO direction changing 0x%x:0x%x"
			   " from 0x%x to 0x%x",
			   msk,val,cval,nval);
	} else {
		nval = val;
		pvr2_trace(PVR2_TRACE_GPIO,
			   "GPIO direction changing to 0x%x",nval);
	}
	return pvr2_write_register(hdw,PVR2_GPIO_DIR,nval);
}


int pvr2_hdw_gpio_chg_out(struct pvr2_hdw *hdw,u32 msk,u32 val)
{
	u32 cval,nval;
	int ret;
	if (~msk) {
		ret = pvr2_read_register(hdw,PVR2_GPIO_OUT,&cval);
		if (ret) return ret;
		nval = (cval & ~msk) | (val & msk);
		pvr2_trace(PVR2_TRACE_GPIO,
			   "GPIO output changing 0x%x:0x%x from 0x%x to 0x%x",
			   msk,val,cval,nval);
	} else {
		nval = val;
		pvr2_trace(PVR2_TRACE_GPIO,
			   "GPIO output changing to 0x%x",nval);
	}
	return pvr2_write_register(hdw,PVR2_GPIO_OUT,nval);
}


/* Find I2C address of eeprom */
static int pvr2_hdw_get_eeprom_addr(struct pvr2_hdw *hdw)
{
	int result;
	LOCK_TAKE(hdw->ctl_lock); do {
		hdw->cmd_buffer[0] = 0xeb;
		result = pvr2_send_request(hdw,
					   hdw->cmd_buffer,1,
					   hdw->cmd_buffer,1);
		if (result < 0) break;
		result = hdw->cmd_buffer[0];
	} while(0); LOCK_GIVE(hdw->ctl_lock);
	return result;
}


int pvr2_hdw_register_access(struct pvr2_hdw *hdw,
			     u32 chip_id,unsigned long reg_id,
			     int setFl,u32 *val_ptr)
{
#ifdef CONFIG_VIDEO_ADV_DEBUG
	struct list_head *item;
	struct pvr2_i2c_client *cp;
	struct v4l2_register req;
	int stat = 0;
	int okFl = 0;

	req.i2c_id = chip_id;
	req.reg = reg_id;
	if (setFl) req.val = *val_ptr;
	mutex_lock(&hdw->i2c_list_lock); do {
		list_for_each(item,&hdw->i2c_clients) {
			cp = list_entry(item,struct pvr2_i2c_client,list);
			if (cp->client->driver->id != chip_id) continue;
			stat = pvr2_i2c_client_cmd(
				cp,(setFl ? VIDIOC_INT_S_REGISTER :
				    VIDIOC_INT_G_REGISTER),&req);
			if (!setFl) *val_ptr = req.val;
			okFl = !0;
			break;
		}
	} while (0); mutex_unlock(&hdw->i2c_list_lock);
	if (okFl) {
		return stat;
	}
	return -EINVAL;
#else
	return -ENOSYS;
#endif
}


/*
  Stuff for Emacs to see, in order to encourage consistent editing style:
  *** Local Variables: ***
  *** mode: c ***
  *** fill-column: 75 ***
  *** tab-width: 8 ***
  *** c-basic-offset: 8 ***
  *** End: ***
  */
