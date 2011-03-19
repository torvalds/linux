/*
 *
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
#include <media/tuner.h>
#include "pvrusb2.h"
#include "pvrusb2-std.h"
#include "pvrusb2-util.h"
#include "pvrusb2-hdw.h"
#include "pvrusb2-i2c-core.h"
#include "pvrusb2-eeprom.h"
#include "pvrusb2-hdw-internal.h"
#include "pvrusb2-encoder.h"
#include "pvrusb2-debug.h"
#include "pvrusb2-fx2-cmd.h"
#include "pvrusb2-wm8775.h"
#include "pvrusb2-video-v4l.h"
#include "pvrusb2-cx2584x-v4l.h"
#include "pvrusb2-cs53l32a.h"
#include "pvrusb2-audio.h"

#define TV_MIN_FREQ     55250000L
#define TV_MAX_FREQ    850000000L

/* This defines a minimum interval that the decoder must remain quiet
   before we are allowed to start it running. */
#define TIME_MSEC_DECODER_WAIT 50

/* This defines a minimum interval that the decoder must be allowed to run
   before we can safely begin using its streaming output. */
#define TIME_MSEC_DECODER_STABILIZATION_WAIT 300

/* This defines a minimum interval that the encoder must remain quiet
   before we are allowed to configure it. */
#define TIME_MSEC_ENCODER_WAIT 50

/* This defines the minimum interval that the encoder must successfully run
   before we consider that the encoder has run at least once since its
   firmware has been loaded.  This measurement is in important for cases
   where we can't do something until we know that the encoder has been run
   at least once. */
#define TIME_MSEC_ENCODER_OK 250

static struct pvr2_hdw *unit_pointers[PVR_NUM] = {[ 0 ... PVR_NUM-1 ] = NULL};
static DEFINE_MUTEX(pvr2_unit_mtx);

static int ctlchg;
static int procreload;
static int tuner[PVR_NUM] = { [0 ... PVR_NUM-1] = -1 };
static int tolerance[PVR_NUM] = { [0 ... PVR_NUM-1] = 0 };
static int video_std[PVR_NUM] = { [0 ... PVR_NUM-1] = 0 };
static int init_pause_msec;

module_param(ctlchg, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ctlchg, "0=optimize ctl change 1=always accept new ctl value");
module_param(init_pause_msec, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(init_pause_msec, "hardware initialization settling delay");
module_param(procreload, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(procreload,
		 "Attempt init failure recovery with firmware reload");
module_param_array(tuner,    int, NULL, 0444);
MODULE_PARM_DESC(tuner,"specify installed tuner type");
module_param_array(video_std,    int, NULL, 0444);
MODULE_PARM_DESC(video_std,"specify initial video standard");
module_param_array(tolerance,    int, NULL, 0444);
MODULE_PARM_DESC(tolerance,"specify stream error tolerance");

/* US Broadcast channel 3 (61.25 MHz), to help with testing */
static int default_tv_freq    = 61250000L;
/* 104.3 MHz, a usable FM station for my area */
static int default_radio_freq = 104300000L;

module_param_named(tv_freq, default_tv_freq, int, 0444);
MODULE_PARM_DESC(tv_freq, "specify initial television frequency");
module_param_named(radio_freq, default_radio_freq, int, 0444);
MODULE_PARM_DESC(radio_freq, "specify initial radio frequency");

#define PVR2_CTL_WRITE_ENDPOINT  0x01
#define PVR2_CTL_READ_ENDPOINT   0x81

#define PVR2_GPIO_IN 0x9008
#define PVR2_GPIO_OUT 0x900c
#define PVR2_GPIO_DIR 0x9020

#define trace_firmware(...) pvr2_trace(PVR2_TRACE_FIRMWARE,__VA_ARGS__)

#define PVR2_FIRMWARE_ENDPOINT   0x02

/* size of a firmware chunk */
#define FIRMWARE_CHUNK_SIZE 0x2000

typedef void (*pvr2_subdev_update_func)(struct pvr2_hdw *,
					struct v4l2_subdev *);

static const pvr2_subdev_update_func pvr2_module_update_functions[] = {
	[PVR2_CLIENT_ID_WM8775] = pvr2_wm8775_subdev_update,
	[PVR2_CLIENT_ID_SAA7115] = pvr2_saa7115_subdev_update,
	[PVR2_CLIENT_ID_MSP3400] = pvr2_msp3400_subdev_update,
	[PVR2_CLIENT_ID_CX25840] = pvr2_cx25840_subdev_update,
	[PVR2_CLIENT_ID_CS53L32A] = pvr2_cs53l32a_subdev_update,
};

static const char *module_names[] = {
	[PVR2_CLIENT_ID_MSP3400] = "msp3400",
	[PVR2_CLIENT_ID_CX25840] = "cx25840",
	[PVR2_CLIENT_ID_SAA7115] = "saa7115",
	[PVR2_CLIENT_ID_TUNER] = "tuner",
	[PVR2_CLIENT_ID_DEMOD] = "tuner",
	[PVR2_CLIENT_ID_CS53L32A] = "cs53l32a",
	[PVR2_CLIENT_ID_WM8775] = "wm8775",
};


static const unsigned char *module_i2c_addresses[] = {
	[PVR2_CLIENT_ID_TUNER] = "\x60\x61\x62\x63",
	[PVR2_CLIENT_ID_DEMOD] = "\x43",
	[PVR2_CLIENT_ID_MSP3400] = "\x40",
	[PVR2_CLIENT_ID_SAA7115] = "\x21",
	[PVR2_CLIENT_ID_WM8775] = "\x1b",
	[PVR2_CLIENT_ID_CX25840] = "\x44",
	[PVR2_CLIENT_ID_CS53L32A] = "\x11",
};


static const char *ir_scheme_names[] = {
	[PVR2_IR_SCHEME_NONE] = "none",
	[PVR2_IR_SCHEME_29XXX] = "29xxx",
	[PVR2_IR_SCHEME_24XXX] = "24xxx (29xxx emulation)",
	[PVR2_IR_SCHEME_24XXX_MCE] = "24xxx (MCE device)",
	[PVR2_IR_SCHEME_ZILOG] = "Zilog",
};


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
#define MPEGDEF_COUNT ARRAY_SIZE(mpeg_ids)


static const char *control_values_srate[] = {
	[V4L2_MPEG_AUDIO_SAMPLING_FREQ_44100]   = "44.1 kHz",
	[V4L2_MPEG_AUDIO_SAMPLING_FREQ_48000]   = "48 kHz",
	[V4L2_MPEG_AUDIO_SAMPLING_FREQ_32000]   = "32 kHz",
};



static const char *control_values_input[] = {
	[PVR2_CVAL_INPUT_TV]        = "television",  /*xawtv needs this name*/
	[PVR2_CVAL_INPUT_DTV]       = "dtv",
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


static const char *pvr2_state_names[] = {
	[PVR2_STATE_NONE] =    "none",
	[PVR2_STATE_DEAD] =    "dead",
	[PVR2_STATE_COLD] =    "cold",
	[PVR2_STATE_WARM] =    "warm",
	[PVR2_STATE_ERROR] =   "error",
	[PVR2_STATE_READY] =   "ready",
	[PVR2_STATE_RUN] =     "run",
};


struct pvr2_fx2cmd_descdef {
	unsigned char id;
	unsigned char *desc;
};

static const struct pvr2_fx2cmd_descdef pvr2_fx2cmd_desc[] = {
	{FX2CMD_MEM_WRITE_DWORD, "write encoder dword"},
	{FX2CMD_MEM_READ_DWORD, "read encoder dword"},
	{FX2CMD_HCW_ZILOG_RESET, "zilog IR reset control"},
	{FX2CMD_MEM_READ_64BYTES, "read encoder 64bytes"},
	{FX2CMD_REG_WRITE, "write encoder register"},
	{FX2CMD_REG_READ, "read encoder register"},
	{FX2CMD_MEMSEL, "encoder memsel"},
	{FX2CMD_I2C_WRITE, "i2c write"},
	{FX2CMD_I2C_READ, "i2c read"},
	{FX2CMD_GET_USB_SPEED, "get USB speed"},
	{FX2CMD_STREAMING_ON, "stream on"},
	{FX2CMD_STREAMING_OFF, "stream off"},
	{FX2CMD_FWPOST1, "fwpost1"},
	{FX2CMD_POWER_OFF, "power off"},
	{FX2CMD_POWER_ON, "power on"},
	{FX2CMD_DEEP_RESET, "deep reset"},
	{FX2CMD_GET_EEPROM_ADDR, "get rom addr"},
	{FX2CMD_GET_IR_CODE, "get IR code"},
	{FX2CMD_HCW_DEMOD_RESETIN, "hcw demod resetin"},
	{FX2CMD_HCW_DTV_STREAMING_ON, "hcw dtv stream on"},
	{FX2CMD_HCW_DTV_STREAMING_OFF, "hcw dtv stream off"},
	{FX2CMD_ONAIR_DTV_STREAMING_ON, "onair dtv stream on"},
	{FX2CMD_ONAIR_DTV_STREAMING_OFF, "onair dtv stream off"},
	{FX2CMD_ONAIR_DTV_POWER_ON, "onair dtv power on"},
	{FX2CMD_ONAIR_DTV_POWER_OFF, "onair dtv power off"},
};


static int pvr2_hdw_set_input(struct pvr2_hdw *hdw,int v);
static void pvr2_hdw_state_sched(struct pvr2_hdw *);
static int pvr2_hdw_state_eval(struct pvr2_hdw *);
static void pvr2_hdw_set_cur_freq(struct pvr2_hdw *,unsigned long);
static void pvr2_hdw_worker_poll(struct work_struct *work);
static int pvr2_hdw_wait(struct pvr2_hdw *,int state);
static int pvr2_hdw_untrip_unlocked(struct pvr2_hdw *);
static void pvr2_hdw_state_log_state(struct pvr2_hdw *);
static int pvr2_hdw_cmd_usbstream(struct pvr2_hdw *hdw,int runFl);
static int pvr2_hdw_commit_setup(struct pvr2_hdw *hdw);
static int pvr2_hdw_get_eeprom_addr(struct pvr2_hdw *hdw);
static void pvr2_hdw_internal_find_stdenum(struct pvr2_hdw *hdw);
static void pvr2_hdw_internal_set_std_avail(struct pvr2_hdw *hdw);
static void pvr2_hdw_quiescent_timeout(unsigned long);
static void pvr2_hdw_decoder_stabilization_timeout(unsigned long);
static void pvr2_hdw_encoder_wait_timeout(unsigned long);
static void pvr2_hdw_encoder_run_timeout(unsigned long);
static int pvr2_issue_simple_cmd(struct pvr2_hdw *,u32);
static int pvr2_send_request_ex(struct pvr2_hdw *hdw,
				unsigned int timeout,int probe_fl,
				void *write_data,unsigned int write_len,
				void *read_data,unsigned int read_len);
static int pvr2_hdw_check_cropcap(struct pvr2_hdw *hdw);


static void trace_stbit(const char *name,int val)
{
	pvr2_trace(PVR2_TRACE_STBITS,
		   "State bit %s <-- %s",
		   name,(val ? "true" : "false"));
}

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
	unsigned int slotId = hdw->freqProgSlot;
	if ((slotId > 0) && (slotId <= FREQTABLE_SIZE)) {
		hdw->freqTable[slotId-1] = v;
		/* Handle side effects correctly - if we're tuned to this
		   slot, then forgot the slot id relation since the stored
		   frequency has been changed. */
		if (hdw->freqSelector) {
			if (hdw->freqSlotRadio == slotId) {
				hdw->freqSlotRadio = 0;
			}
		} else {
			if (hdw->freqSlotTelevision == slotId) {
				hdw->freqSlotTelevision = 0;
			}
		}
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
	struct pvr2_hdw *hdw = cptr->hdw;
	*vp = hdw->freqSelector ? hdw->freqSlotRadio : hdw->freqSlotTelevision;
	return 0;
}

static int ctrl_channel_set(struct pvr2_ctrl *cptr,int m,int slotId)
{
	unsigned freq = 0;
	struct pvr2_hdw *hdw = cptr->hdw;
	if ((slotId < 0) || (slotId > FREQTABLE_SIZE)) return 0;
	if (slotId > 0) {
		freq = hdw->freqTable[slotId-1];
		if (!freq) return 0;
		pvr2_hdw_set_cur_freq(hdw,freq);
	}
	if (hdw->freqSelector) {
		hdw->freqSlotRadio = slotId;
	} else {
		hdw->freqSlotTelevision = slotId;
	}
	return 0;
}

static int ctrl_freq_get(struct pvr2_ctrl *cptr,int *vp)
{
	*vp = pvr2_hdw_get_cur_freq(cptr->hdw);
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
	pvr2_hdw_set_cur_freq(cptr->hdw,v);
	return 0;
}

static int ctrl_cropl_min_get(struct pvr2_ctrl *cptr, int *left)
{
	struct v4l2_cropcap *cap = &cptr->hdw->cropcap_info;
	int stat = pvr2_hdw_check_cropcap(cptr->hdw);
	if (stat != 0) {
		return stat;
	}
	*left = cap->bounds.left;
	return 0;
}

static int ctrl_cropl_max_get(struct pvr2_ctrl *cptr, int *left)
{
	struct v4l2_cropcap *cap = &cptr->hdw->cropcap_info;
	int stat = pvr2_hdw_check_cropcap(cptr->hdw);
	if (stat != 0) {
		return stat;
	}
	*left = cap->bounds.left;
	if (cap->bounds.width > cptr->hdw->cropw_val) {
		*left += cap->bounds.width - cptr->hdw->cropw_val;
	}
	return 0;
}

static int ctrl_cropt_min_get(struct pvr2_ctrl *cptr, int *top)
{
	struct v4l2_cropcap *cap = &cptr->hdw->cropcap_info;
	int stat = pvr2_hdw_check_cropcap(cptr->hdw);
	if (stat != 0) {
		return stat;
	}
	*top = cap->bounds.top;
	return 0;
}

static int ctrl_cropt_max_get(struct pvr2_ctrl *cptr, int *top)
{
	struct v4l2_cropcap *cap = &cptr->hdw->cropcap_info;
	int stat = pvr2_hdw_check_cropcap(cptr->hdw);
	if (stat != 0) {
		return stat;
	}
	*top = cap->bounds.top;
	if (cap->bounds.height > cptr->hdw->croph_val) {
		*top += cap->bounds.height - cptr->hdw->croph_val;
	}
	return 0;
}

static int ctrl_cropw_max_get(struct pvr2_ctrl *cptr, int *val)
{
	struct v4l2_cropcap *cap = &cptr->hdw->cropcap_info;
	int stat = pvr2_hdw_check_cropcap(cptr->hdw);
	if (stat != 0) {
		return stat;
	}
	*val = 0;
	if (cap->bounds.width > cptr->hdw->cropl_val) {
		*val = cap->bounds.width - cptr->hdw->cropl_val;
	}
	return 0;
}

static int ctrl_croph_max_get(struct pvr2_ctrl *cptr, int *val)
{
	struct v4l2_cropcap *cap = &cptr->hdw->cropcap_info;
	int stat = pvr2_hdw_check_cropcap(cptr->hdw);
	if (stat != 0) {
		return stat;
	}
	*val = 0;
	if (cap->bounds.height > cptr->hdw->cropt_val) {
		*val = cap->bounds.height - cptr->hdw->cropt_val;
	}
	return 0;
}

static int ctrl_get_cropcapbl(struct pvr2_ctrl *cptr, int *val)
{
	struct v4l2_cropcap *cap = &cptr->hdw->cropcap_info;
	int stat = pvr2_hdw_check_cropcap(cptr->hdw);
	if (stat != 0) {
		return stat;
	}
	*val = cap->bounds.left;
	return 0;
}

static int ctrl_get_cropcapbt(struct pvr2_ctrl *cptr, int *val)
{
	struct v4l2_cropcap *cap = &cptr->hdw->cropcap_info;
	int stat = pvr2_hdw_check_cropcap(cptr->hdw);
	if (stat != 0) {
		return stat;
	}
	*val = cap->bounds.top;
	return 0;
}

static int ctrl_get_cropcapbw(struct pvr2_ctrl *cptr, int *val)
{
	struct v4l2_cropcap *cap = &cptr->hdw->cropcap_info;
	int stat = pvr2_hdw_check_cropcap(cptr->hdw);
	if (stat != 0) {
		return stat;
	}
	*val = cap->bounds.width;
	return 0;
}

static int ctrl_get_cropcapbh(struct pvr2_ctrl *cptr, int *val)
{
	struct v4l2_cropcap *cap = &cptr->hdw->cropcap_info;
	int stat = pvr2_hdw_check_cropcap(cptr->hdw);
	if (stat != 0) {
		return stat;
	}
	*val = cap->bounds.height;
	return 0;
}

static int ctrl_get_cropcapdl(struct pvr2_ctrl *cptr, int *val)
{
	struct v4l2_cropcap *cap = &cptr->hdw->cropcap_info;
	int stat = pvr2_hdw_check_cropcap(cptr->hdw);
	if (stat != 0) {
		return stat;
	}
	*val = cap->defrect.left;
	return 0;
}

static int ctrl_get_cropcapdt(struct pvr2_ctrl *cptr, int *val)
{
	struct v4l2_cropcap *cap = &cptr->hdw->cropcap_info;
	int stat = pvr2_hdw_check_cropcap(cptr->hdw);
	if (stat != 0) {
		return stat;
	}
	*val = cap->defrect.top;
	return 0;
}

static int ctrl_get_cropcapdw(struct pvr2_ctrl *cptr, int *val)
{
	struct v4l2_cropcap *cap = &cptr->hdw->cropcap_info;
	int stat = pvr2_hdw_check_cropcap(cptr->hdw);
	if (stat != 0) {
		return stat;
	}
	*val = cap->defrect.width;
	return 0;
}

static int ctrl_get_cropcapdh(struct pvr2_ctrl *cptr, int *val)
{
	struct v4l2_cropcap *cap = &cptr->hdw->cropcap_info;
	int stat = pvr2_hdw_check_cropcap(cptr->hdw);
	if (stat != 0) {
		return stat;
	}
	*val = cap->defrect.height;
	return 0;
}

static int ctrl_get_cropcappan(struct pvr2_ctrl *cptr, int *val)
{
	struct v4l2_cropcap *cap = &cptr->hdw->cropcap_info;
	int stat = pvr2_hdw_check_cropcap(cptr->hdw);
	if (stat != 0) {
		return stat;
	}
	*val = cap->pixelaspect.numerator;
	return 0;
}

static int ctrl_get_cropcappad(struct pvr2_ctrl *cptr, int *val)
{
	struct v4l2_cropcap *cap = &cptr->hdw->cropcap_info;
	int stat = pvr2_hdw_check_cropcap(cptr->hdw);
	if (stat != 0) {
		return stat;
	}
	*val = cap->pixelaspect.denominator;
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
	/* Actual minimum depends on device digitizer type. */
	if (cptr->hdw->hdw_desc->flag_has_cx25840) {
		*vp = 75;
	} else {
		*vp = 17;
	}
	return 0;
}

static int ctrl_get_input(struct pvr2_ctrl *cptr,int *vp)
{
	*vp = cptr->hdw->input_val;
	return 0;
}

static int ctrl_check_input(struct pvr2_ctrl *cptr,int v)
{
	return ((1 << v) & cptr->hdw->input_allowed_mask) != 0;
}

static int ctrl_set_input(struct pvr2_ctrl *cptr,int m,int v)
{
	return pvr2_hdw_set_input(cptr->hdw,v);
}

static int ctrl_isdirty_input(struct pvr2_ctrl *cptr)
{
	return cptr->hdw->input_dirty != 0;
}

static void ctrl_cleardirty_input(struct pvr2_ctrl *cptr)
{
	cptr->hdw->input_dirty = 0;
}


static int ctrl_freq_max_get(struct pvr2_ctrl *cptr, int *vp)
{
	unsigned long fv;
	struct pvr2_hdw *hdw = cptr->hdw;
	if (hdw->tuner_signal_stale) {
		pvr2_hdw_status_poll(hdw);
	}
	fv = hdw->tuner_signal_info.rangehigh;
	if (!fv) {
		/* Safety fallback */
		*vp = TV_MAX_FREQ;
		return 0;
	}
	if (hdw->tuner_signal_info.capability & V4L2_TUNER_CAP_LOW) {
		fv = (fv * 125) / 2;
	} else {
		fv = fv * 62500;
	}
	*vp = fv;
	return 0;
}

static int ctrl_freq_min_get(struct pvr2_ctrl *cptr, int *vp)
{
	unsigned long fv;
	struct pvr2_hdw *hdw = cptr->hdw;
	if (hdw->tuner_signal_stale) {
		pvr2_hdw_status_poll(hdw);
	}
	fv = hdw->tuner_signal_info.rangelow;
	if (!fv) {
		/* Safety fallback */
		*vp = TV_MIN_FREQ;
		return 0;
	}
	if (hdw->tuner_signal_info.capability & V4L2_TUNER_CAP_LOW) {
		fv = (fv * 125) / 2;
	} else {
		fv = fv * 62500;
	}
	*vp = fv;
	return 0;
}

static int ctrl_cx2341x_is_dirty(struct pvr2_ctrl *cptr)
{
	return cptr->hdw->enc_stale != 0;
}

static void ctrl_cx2341x_clear_dirty(struct pvr2_ctrl *cptr)
{
	cptr->hdw->enc_stale = 0;
	cptr->hdw->enc_unsafe_stale = 0;
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
	ret = cx2341x_ext_ctrls(&cptr->hdw->enc_ctl_state, 0, &cs,
				VIDIOC_G_EXT_CTRLS);
	if (ret) return ret;
	*vp = c1.value;
	return 0;
}

static int ctrl_cx2341x_set(struct pvr2_ctrl *cptr,int m,int v)
{
	int ret;
	struct pvr2_hdw *hdw = cptr->hdw;
	struct v4l2_ext_controls cs;
	struct v4l2_ext_control c1;
	memset(&cs,0,sizeof(cs));
	memset(&c1,0,sizeof(c1));
	cs.controls = &c1;
	cs.count = 1;
	c1.id = cptr->info->v4l_id;
	c1.value = v;
	ret = cx2341x_ext_ctrls(&hdw->enc_ctl_state,
				hdw->state_encoder_run, &cs,
				VIDIOC_S_EXT_CTRLS);
	if (ret == -EBUSY) {
		/* Oops.  cx2341x is telling us it's not safe to change
		   this control while we're capturing.  Make a note of this
		   fact so that the pipeline will be stopped the next time
		   controls are committed.  Then go on ahead and store this
		   change anyway. */
		ret = cx2341x_ext_ctrls(&hdw->enc_ctl_state,
					0, &cs,
					VIDIOC_S_EXT_CTRLS);
		if (!ret) hdw->enc_unsafe_stale = !0;
	}
	if (ret) return ret;
	hdw->enc_stale = !0;
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
	*vp = cptr->hdw->state_pipeline_req;
	return 0;
}

static int ctrl_masterstate_get(struct pvr2_ctrl *cptr,int *vp)
{
	*vp = cptr->hdw->master_state;
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
	struct pvr2_hdw *hdw = cptr->hdw;
	pvr2_hdw_status_poll(hdw);
	*vp = hdw->tuner_signal_info.signal;
	return 0;
}

static int ctrl_audio_modes_present_get(struct pvr2_ctrl *cptr,int *vp)
{
	int val = 0;
	unsigned int subchan;
	struct pvr2_hdw *hdw = cptr->hdw;
	pvr2_hdw_status_poll(hdw);
	subchan = hdw->tuner_signal_info.rxsubchans;
	if (subchan & V4L2_TUNER_SUB_MONO) {
		val |= (1 << V4L2_TUNER_MODE_MONO);
	}
	if (subchan & V4L2_TUNER_SUB_STEREO) {
		val |= (1 << V4L2_TUNER_MODE_STEREO);
	}
	if (subchan & V4L2_TUNER_SUB_LANG1) {
		val |= (1 << V4L2_TUNER_MODE_LANG1);
	}
	if (subchan & V4L2_TUNER_SUB_LANG2) {
		val |= (1 << V4L2_TUNER_MODE_LANG2);
	}
	*vp = val;
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
	.def.type_enum.count = ARRAY_SIZE(tab), \
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
VCREATE_FUNCS(cropl)
VCREATE_FUNCS(cropt)
VCREATE_FUNCS(cropw)
VCREATE_FUNCS(croph)
VCREATE_FUNCS(audiomode)
VCREATE_FUNCS(res_hor)
VCREATE_FUNCS(res_ver)
VCREATE_FUNCS(srate)

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
		.default_value = 62000,
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
	}, {
		.desc = "Capture crop left margin",
		.name = "crop_left",
		.internal_id = PVR2_CID_CROPL,
		.default_value = 0,
		DEFREF(cropl),
		DEFINT(-129, 340),
		.get_min_value = ctrl_cropl_min_get,
		.get_max_value = ctrl_cropl_max_get,
		.get_def_value = ctrl_get_cropcapdl,
	}, {
		.desc = "Capture crop top margin",
		.name = "crop_top",
		.internal_id = PVR2_CID_CROPT,
		.default_value = 0,
		DEFREF(cropt),
		DEFINT(-35, 544),
		.get_min_value = ctrl_cropt_min_get,
		.get_max_value = ctrl_cropt_max_get,
		.get_def_value = ctrl_get_cropcapdt,
	}, {
		.desc = "Capture crop width",
		.name = "crop_width",
		.internal_id = PVR2_CID_CROPW,
		.default_value = 720,
		DEFREF(cropw),
		.get_max_value = ctrl_cropw_max_get,
		.get_def_value = ctrl_get_cropcapdw,
	}, {
		.desc = "Capture crop height",
		.name = "crop_height",
		.internal_id = PVR2_CID_CROPH,
		.default_value = 480,
		DEFREF(croph),
		.get_max_value = ctrl_croph_max_get,
		.get_def_value = ctrl_get_cropcapdh,
	}, {
		.desc = "Capture capability pixel aspect numerator",
		.name = "cropcap_pixel_numerator",
		.internal_id = PVR2_CID_CROPCAPPAN,
		.get_value = ctrl_get_cropcappan,
	}, {
		.desc = "Capture capability pixel aspect denominator",
		.name = "cropcap_pixel_denominator",
		.internal_id = PVR2_CID_CROPCAPPAD,
		.get_value = ctrl_get_cropcappad,
	}, {
		.desc = "Capture capability bounds top",
		.name = "cropcap_bounds_top",
		.internal_id = PVR2_CID_CROPCAPBT,
		.get_value = ctrl_get_cropcapbt,
	}, {
		.desc = "Capture capability bounds left",
		.name = "cropcap_bounds_left",
		.internal_id = PVR2_CID_CROPCAPBL,
		.get_value = ctrl_get_cropcapbl,
	}, {
		.desc = "Capture capability bounds width",
		.name = "cropcap_bounds_width",
		.internal_id = PVR2_CID_CROPCAPBW,
		.get_value = ctrl_get_cropcapbw,
	}, {
		.desc = "Capture capability bounds height",
		.name = "cropcap_bounds_height",
		.internal_id = PVR2_CID_CROPCAPBH,
		.get_value = ctrl_get_cropcapbh,
	},{
		.desc = "Video Source",
		.name = "input",
		.internal_id = PVR2_CID_INPUT,
		.default_value = PVR2_CVAL_INPUT_TV,
		.check_value = ctrl_check_input,
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
		.default_value = 0,
		.set_value = ctrl_freq_set,
		.get_value = ctrl_freq_get,
		.is_dirty = ctrl_freq_is_dirty,
		.clear_dirty = ctrl_freq_clear_dirty,
		DEFINT(0,0),
		/* Hook in check for input value (tv/radio) and adjust
		   max/min values accordingly */
		.get_max_value = ctrl_freq_max_get,
		.get_min_value = ctrl_freq_min_get,
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
		DEFINT(0,0),
		/* Hook in check for input value (tv/radio) and adjust
		   max/min values accordingly */
		.get_max_value = ctrl_freq_max_get,
		.get_min_value = ctrl_freq_min_get,
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
		.desc = "Master State",
		.name = "master_state",
		.get_value = ctrl_masterstate_get,
		DEFENUM(pvr2_state_names),
	},{
		.desc = "Signal Present",
		.name = "signal_present",
		.get_value = ctrl_signal_get,
		DEFINT(0,65535),
	},{
		.desc = "Audio Modes Present",
		.name = "audio_modes_present",
		.get_value = ctrl_audio_modes_present_get,
		/* For this type we "borrow" the V4L2_TUNER_MODE enum from
		   v4l.  Nothing outside of this module cares about this,
		   but I reuse it in order to also reuse the
		   control_values_audiomode string table. */
		DEFMASK(((1 << V4L2_TUNER_MODE_MONO)|
			 (1 << V4L2_TUNER_MODE_STEREO)|
			 (1 << V4L2_TUNER_MODE_LANG1)|
			 (1 << V4L2_TUNER_MODE_LANG2)),
			control_values_audiomode),
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

#define CTRLDEF_COUNT ARRAY_SIZE(control_defs)


const char *pvr2_config_get_name(enum pvr2_config cfg)
{
	switch (cfg) {
	case pvr2_config_empty: return "empty";
	case pvr2_config_mpeg: return "mpeg";
	case pvr2_config_vbi: return "vbi";
	case pvr2_config_pcm: return "pcm";
	case pvr2_config_rawvideo: return "raw video";
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


const char *pvr2_hdw_get_bus_info(struct pvr2_hdw *hdw)
{
	return hdw->bus_info;
}


const char *pvr2_hdw_get_device_identifier(struct pvr2_hdw *hdw)
{
	return hdw->identifier;
}


unsigned long pvr2_hdw_get_cur_freq(struct pvr2_hdw *hdw)
{
	return hdw->freqSelector ? hdw->freqValTelevision : hdw->freqValRadio;
}

/* Set the currently tuned frequency and account for all possible
   driver-core side effects of this action. */
static void pvr2_hdw_set_cur_freq(struct pvr2_hdw *hdw,unsigned long val)
{
	if (hdw->input_val == PVR2_CVAL_INPUT_RADIO) {
		if (hdw->freqSelector) {
			/* Swing over to radio frequency selection */
			hdw->freqSelector = 0;
			hdw->freqDirty = !0;
		}
		if (hdw->freqValRadio != val) {
			hdw->freqValRadio = val;
			hdw->freqSlotRadio = 0;
			hdw->freqDirty = !0;
		}
	} else {
		if (!(hdw->freqSelector)) {
			/* Swing over to television frequency selection */
			hdw->freqSelector = 1;
			hdw->freqDirty = !0;
		}
		if (hdw->freqValTelevision != val) {
			hdw->freqValTelevision = val;
			hdw->freqSlotTelevision = 0;
			hdw->freqDirty = !0;
		}
	}
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
	unsigned int fwsize;
	int ret;
	u16 address;

	if (!hdw->hdw_desc->fx2_firmware.cnt) {
		hdw->fw1_state = FW1_STATE_OK;
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "Connected device type defines"
			   " no firmware to upload; ignoring firmware");
		return -ENOTTY;
	}

	hdw->fw1_state = FW1_STATE_FAILED; // default result

	trace_firmware("pvr2_upload_firmware1");

	ret = pvr2_locate_firmware(hdw,&fw_entry,"fx2 controller",
				   hdw->hdw_desc->fx2_firmware.cnt,
				   hdw->hdw_desc->fx2_firmware.lst);
	if (ret < 0) {
		if (ret == -ENOENT) hdw->fw1_state = FW1_STATE_MISSING;
		return ret;
	}

	usb_clear_halt(hdw->usb_dev, usb_sndbulkpipe(hdw->usb_dev, 0 & 0x7f));

	pipe = usb_sndctrlpipe(hdw->usb_dev, 0);
	fwsize = fw_entry->size;

	if ((fwsize != 0x2000) &&
	    (!(hdw->hdw_desc->flag_fx2_16kb && (fwsize == 0x4000)))) {
		if (hdw->hdw_desc->flag_fx2_16kb) {
			pvr2_trace(PVR2_TRACE_ERROR_LEGS,
				   "Wrong fx2 firmware size"
				   " (expected 8192 or 16384, got %u)",
				   fwsize);
		} else {
			pvr2_trace(PVR2_TRACE_ERROR_LEGS,
				   "Wrong fx2 firmware size"
				   " (expected 8192, got %u)",
				   fwsize);
		}
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
	for (address = 0; address < fwsize; address += 0x800) {
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

	/* We should have written fwsize bytes */
	if (ret == fwsize) {
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
	unsigned int pipe, fw_len, fw_done, bcnt, icnt;
	int actual_length;
	int ret = 0;
	int fwidx;
	static const char *fw_files[] = {
		CX2341X_FIRM_ENC_FILENAME,
	};

	if (hdw->hdw_desc->flag_skip_cx23416_firmware) {
		return 0;
	}

	trace_firmware("pvr2_upload_firmware2");

	ret = pvr2_locate_firmware(hdw,&fw_entry,"encoder",
				   ARRAY_SIZE(fw_files), fw_files);
	if (ret < 0) return ret;
	fwidx = ret;
	ret = 0;
	/* Since we're about to completely reinitialize the encoder,
	   invalidate our cached copy of its configuration state.  Next
	   time we configure the encoder, then we'll fully configure it. */
	hdw->enc_cur_valid = 0;

	/* Encoder is about to be reset so note that as far as we're
	   concerned now, the encoder has never been run. */
	del_timer_sync(&hdw->encoder_run_timer);
	if (hdw->state_encoder_runok) {
		hdw->state_encoder_runok = 0;
		trace_stbit("state_encoder_runok",hdw->state_encoder_runok);
	}

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
	ret |= pvr2_issue_simple_cmd(hdw,FX2CMD_FWPOST1);
	ret |= pvr2_issue_simple_cmd(hdw,FX2CMD_MEMSEL | (1 << 8) | (0 << 16));

	if (ret) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "firmware2 upload prep failed, ret=%d",ret);
		release_firmware(fw_entry);
		goto done;
	}

	/* Now send firmware */

	fw_len = fw_entry->size;

	if (fw_len % sizeof(u32)) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "size of %s firmware"
			   " must be a multiple of %zu bytes",
			   fw_files[fwidx],sizeof(u32));
		release_firmware(fw_entry);
		ret = -EINVAL;
		goto done;
	}

	fw_ptr = kmalloc(FIRMWARE_CHUNK_SIZE, GFP_KERNEL);
	if (fw_ptr == NULL){
		release_firmware(fw_entry);
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "failed to allocate memory for firmware2 upload");
		ret = -ENOMEM;
		goto done;
	}

	pipe = usb_sndbulkpipe(hdw->usb_dev, PVR2_FIRMWARE_ENDPOINT);

	fw_done = 0;
	for (fw_done = 0; fw_done < fw_len;) {
		bcnt = fw_len - fw_done;
		if (bcnt > FIRMWARE_CHUNK_SIZE) bcnt = FIRMWARE_CHUNK_SIZE;
		memcpy(fw_ptr, fw_entry->data + fw_done, bcnt);
		/* Usbsnoop log shows that we must swap bytes... */
		/* Some background info: The data being swapped here is a
		   firmware image destined for the mpeg encoder chip that
		   lives at the other end of a USB endpoint.  The encoder
		   chip always talks in 32 bit chunks and its storage is
		   organized into 32 bit words.  However from the file
		   system to the encoder chip everything is purely a byte
		   stream.  The firmware file's contents are always 32 bit
		   swapped from what the encoder expects.  Thus the need
		   always exists to swap the bytes regardless of the endian
		   type of the host processor and therefore swab32() makes
		   the most sense. */
		for (icnt = 0; icnt < bcnt/4 ; icnt++)
			((u32 *)fw_ptr)[icnt] = swab32(((u32 *)fw_ptr)[icnt]);

		ret |= usb_bulk_msg(hdw->usb_dev, pipe, fw_ptr,bcnt,
				    &actual_length, HZ);
		ret |= (actual_length != bcnt);
		if (ret) break;
		fw_done += bcnt;
	}

	trace_firmware("upload of %s : %i / %i ",
		       fw_files[fwidx],fw_done,fw_len);

	kfree(fw_ptr);
	release_firmware(fw_entry);

	if (ret) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "firmware2 upload transfer failure");
		goto done;
	}

	/* Finish upload */

	ret |= pvr2_write_register(hdw, 0x9054, 0xffffffff); /*reset hw blocks*/
	ret |= pvr2_write_register(hdw, 0x9058, 0xffffffe8); /*VPU ctrl*/
	ret |= pvr2_issue_simple_cmd(hdw,FX2CMD_MEMSEL | (1 << 8) | (0 << 16));

	if (ret) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "firmware2 upload post-proc failure");
	}

 done:
	if (hdw->hdw_desc->signal_routing_scheme ==
	    PVR2_ROUTING_SCHEME_GOTVIEW) {
		/* Ensure that GPIO 11 is set to output for GOTVIEW
		   hardware. */
		pvr2_hdw_gpio_chg_dir(hdw,(1 << 11),~0);
	}
	return ret;
}


static const char *pvr2_get_state_name(unsigned int st)
{
	if (st < ARRAY_SIZE(pvr2_state_names)) {
		return pvr2_state_names[st];
	}
	return "???";
}

static int pvr2_decoder_enable(struct pvr2_hdw *hdw,int enablefl)
{
	/* Even though we really only care about the video decoder chip at
	   this point, we'll broadcast stream on/off to all sub-devices
	   anyway, just in case somebody else wants to hear the
	   command... */
	pvr2_trace(PVR2_TRACE_CHIPS, "subdev v4l2 stream=%s",
		   (enablefl ? "on" : "off"));
	v4l2_device_call_all(&hdw->v4l2_dev, 0, video, s_stream, enablefl);
	v4l2_device_call_all(&hdw->v4l2_dev, 0, audio, s_stream, enablefl);
	if (hdw->decoder_client_id) {
		/* We get here if the encoder has been noticed.  Otherwise
		   we'll issue a warning to the user (which should
		   normally never happen). */
		return 0;
	}
	if (!hdw->flag_decoder_missed) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "WARNING: No decoder present");
		hdw->flag_decoder_missed = !0;
		trace_stbit("flag_decoder_missed",
			    hdw->flag_decoder_missed);
	}
	return -EIO;
}


int pvr2_hdw_get_state(struct pvr2_hdw *hdw)
{
	return hdw->master_state;
}


static int pvr2_hdw_untrip_unlocked(struct pvr2_hdw *hdw)
{
	if (!hdw->flag_tripped) return 0;
	hdw->flag_tripped = 0;
	pvr2_trace(PVR2_TRACE_ERROR_LEGS,
		   "Clearing driver error statuss");
	return !0;
}


int pvr2_hdw_untrip(struct pvr2_hdw *hdw)
{
	int fl;
	LOCK_TAKE(hdw->big_lock); do {
		fl = pvr2_hdw_untrip_unlocked(hdw);
	} while (0); LOCK_GIVE(hdw->big_lock);
	if (fl) pvr2_hdw_state_sched(hdw);
	return 0;
}




int pvr2_hdw_get_streaming(struct pvr2_hdw *hdw)
{
	return hdw->state_pipeline_req != 0;
}


int pvr2_hdw_set_streaming(struct pvr2_hdw *hdw,int enable_flag)
{
	int ret,st;
	LOCK_TAKE(hdw->big_lock); do {
		pvr2_hdw_untrip_unlocked(hdw);
		if ((!enable_flag) != !(hdw->state_pipeline_req)) {
			hdw->state_pipeline_req = enable_flag != 0;
			pvr2_trace(PVR2_TRACE_START_STOP,
				   "/*--TRACE_STREAM--*/ %s",
				   enable_flag ? "enable" : "disable");
		}
		pvr2_hdw_state_sched(hdw);
	} while (0); LOCK_GIVE(hdw->big_lock);
	if ((ret = pvr2_hdw_wait(hdw,0)) < 0) return ret;
	if (enable_flag) {
		while ((st = hdw->master_state) != PVR2_STATE_RUN) {
			if (st != PVR2_STATE_READY) return -EIO;
			if ((ret = pvr2_hdw_wait(hdw,st)) < 0) return ret;
		}
	}
	return 0;
}


int pvr2_hdw_set_stream_type(struct pvr2_hdw *hdw,enum pvr2_config config)
{
	int fl;
	LOCK_TAKE(hdw->big_lock);
	if ((fl = (hdw->desired_stream_type != config)) != 0) {
		hdw->desired_stream_type = config;
		hdw->state_pipeline_config = 0;
		trace_stbit("state_pipeline_config",
			    hdw->state_pipeline_config);
		pvr2_hdw_state_sched(hdw);
	}
	LOCK_GIVE(hdw->big_lock);
	if (fl) return 0;
	return pvr2_hdw_wait(hdw,0);
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
	hdw->tuner_updated = !0;
	return 0;
}


static v4l2_std_id get_default_standard(struct pvr2_hdw *hdw)
{
	int unit_number = hdw->unit_number;
	int tp = 0;
	if ((unit_number >= 0) && (unit_number < PVR_NUM)) {
		tp = video_std[unit_number];
		if (tp) return tp;
	}
	return 0;
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
		hdw->cmd_buffer[0] = FX2CMD_GET_EEPROM_ADDR;
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

struct pvr2_std_hack {
	v4l2_std_id pat;  /* Pattern to match */
	v4l2_std_id msk;  /* Which bits we care about */
	v4l2_std_id std;  /* What additional standards or default to set */
};

/* This data structure labels specific combinations of standards from
   tveeprom that we'll try to recognize.  If we recognize one, then assume
   a specified default standard to use.  This is here because tveeprom only
   tells us about available standards not the intended default standard (if
   any) for the device in question.  We guess the default based on what has
   been reported as available.  Note that this is only for guessing a
   default - which can always be overridden explicitly - and if the user
   has otherwise named a default then that default will always be used in
   place of this table. */
static const struct pvr2_std_hack std_eeprom_maps[] = {
	{	/* PAL(B/G) */
		.pat = V4L2_STD_B|V4L2_STD_GH,
		.std = V4L2_STD_PAL_B|V4L2_STD_PAL_B1|V4L2_STD_PAL_G,
	},
	{	/* NTSC(M) */
		.pat = V4L2_STD_MN,
		.std = V4L2_STD_NTSC_M,
	},
	{	/* PAL(I) */
		.pat = V4L2_STD_PAL_I,
		.std = V4L2_STD_PAL_I,
	},
	{	/* SECAM(L/L') */
		.pat = V4L2_STD_SECAM_L|V4L2_STD_SECAM_LC,
		.std = V4L2_STD_SECAM_L|V4L2_STD_SECAM_LC,
	},
	{	/* PAL(D/D1/K) */
		.pat = V4L2_STD_DK,
		.std = V4L2_STD_PAL_D|V4L2_STD_PAL_D1|V4L2_STD_PAL_K,
	},
};

static void pvr2_hdw_setup_std(struct pvr2_hdw *hdw)
{
	char buf[40];
	unsigned int bcnt;
	v4l2_std_id std1,std2,std3;

	std1 = get_default_standard(hdw);
	std3 = std1 ? 0 : hdw->hdw_desc->default_std_mask;

	bcnt = pvr2_std_id_to_str(buf,sizeof(buf),hdw->std_mask_eeprom);
	pvr2_trace(PVR2_TRACE_STD,
		   "Supported video standard(s) reported available"
		   " in hardware: %.*s",
		   bcnt,buf);

	hdw->std_mask_avail = hdw->std_mask_eeprom;

	std2 = (std1|std3) & ~hdw->std_mask_avail;
	if (std2) {
		bcnt = pvr2_std_id_to_str(buf,sizeof(buf),std2);
		pvr2_trace(PVR2_TRACE_STD,
			   "Expanding supported video standards"
			   " to include: %.*s",
			   bcnt,buf);
		hdw->std_mask_avail |= std2;
	}

	pvr2_hdw_internal_set_std_avail(hdw);

	if (std1) {
		bcnt = pvr2_std_id_to_str(buf,sizeof(buf),std1);
		pvr2_trace(PVR2_TRACE_STD,
			   "Initial video standard forced to %.*s",
			   bcnt,buf);
		hdw->std_mask_cur = std1;
		hdw->std_dirty = !0;
		pvr2_hdw_internal_find_stdenum(hdw);
		return;
	}
	if (std3) {
		bcnt = pvr2_std_id_to_str(buf,sizeof(buf),std3);
		pvr2_trace(PVR2_TRACE_STD,
			   "Initial video standard"
			   " (determined by device type): %.*s",bcnt,buf);
		hdw->std_mask_cur = std3;
		hdw->std_dirty = !0;
		pvr2_hdw_internal_find_stdenum(hdw);
		return;
	}

	{
		unsigned int idx;
		for (idx = 0; idx < ARRAY_SIZE(std_eeprom_maps); idx++) {
			if (std_eeprom_maps[idx].msk ?
			    ((std_eeprom_maps[idx].pat ^
			     hdw->std_mask_eeprom) &
			     std_eeprom_maps[idx].msk) :
			    (std_eeprom_maps[idx].pat !=
			     hdw->std_mask_eeprom)) continue;
			bcnt = pvr2_std_id_to_str(buf,sizeof(buf),
						  std_eeprom_maps[idx].std);
			pvr2_trace(PVR2_TRACE_STD,
				   "Initial video standard guessed as %.*s",
				   bcnt,buf);
			hdw->std_mask_cur = std_eeprom_maps[idx].std;
			hdw->std_dirty = !0;
			pvr2_hdw_internal_find_stdenum(hdw);
			return;
		}
	}

	if (hdw->std_enum_cnt > 1) {
		// Autoselect the first listed standard
		hdw->std_enum_cur = 1;
		hdw->std_mask_cur = hdw->std_defs[hdw->std_enum_cur-1].id;
		hdw->std_dirty = !0;
		pvr2_trace(PVR2_TRACE_STD,
			   "Initial video standard auto-selected to %s",
			   hdw->std_defs[hdw->std_enum_cur-1].name);
		return;
	}

	pvr2_trace(PVR2_TRACE_ERROR_LEGS,
		   "Unable to select a viable initial video standard");
}


static unsigned int pvr2_copy_i2c_addr_list(
	unsigned short *dst, const unsigned char *src,
	unsigned int dst_max)
{
	unsigned int cnt = 0;
	if (!src) return 0;
	while (src[cnt] && (cnt + 1) < dst_max) {
		dst[cnt] = src[cnt];
		cnt++;
	}
	dst[cnt] = I2C_CLIENT_END;
	return cnt;
}


static void pvr2_hdw_cx25840_vbi_hack(struct pvr2_hdw *hdw)
{
	/*
	  Mike Isely <isely@pobox.com> 19-Nov-2006 - This bit of nuttiness
	  for cx25840 causes that module to correctly set up its video
	  scaling.  This is really a problem in the cx25840 module itself,
	  but we work around it here.  The problem has not been seen in
	  ivtv because there VBI is supported and set up.  We don't do VBI
	  here (at least not yet) and thus we never attempted to even set
	  it up.
	*/
	struct v4l2_format fmt;
	if (hdw->decoder_client_id != PVR2_CLIENT_ID_CX25840) {
		/* We're not using a cx25840 so don't enable the hack */
		return;
	}

	pvr2_trace(PVR2_TRACE_INIT,
		   "Module ID %u:"
		   " Executing cx25840 VBI hack",
		   hdw->decoder_client_id);
	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_SLICED_VBI_CAPTURE;
	v4l2_device_call_all(&hdw->v4l2_dev, hdw->decoder_client_id,
			     vbi, s_sliced_fmt, &fmt.fmt.sliced);
}


static int pvr2_hdw_load_subdev(struct pvr2_hdw *hdw,
				const struct pvr2_device_client_desc *cd)
{
	const char *fname;
	unsigned char mid;
	struct v4l2_subdev *sd;
	unsigned int i2ccnt;
	const unsigned char *p;
	/* Arbitrary count - max # i2c addresses we will probe */
	unsigned short i2caddr[25];

	mid = cd->module_id;
	fname = (mid < ARRAY_SIZE(module_names)) ? module_names[mid] : NULL;
	if (!fname) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "Module ID %u for device %s has no name?"
			   "  The driver might have a configuration problem.",
			   mid,
			   hdw->hdw_desc->description);
		return -EINVAL;
	}
	pvr2_trace(PVR2_TRACE_INIT,
		   "Module ID %u (%s) for device %s being loaded...",
		   mid, fname,
		   hdw->hdw_desc->description);

	i2ccnt = pvr2_copy_i2c_addr_list(i2caddr, cd->i2c_address_list,
					 ARRAY_SIZE(i2caddr));
	if (!i2ccnt && ((p = (mid < ARRAY_SIZE(module_i2c_addresses)) ?
			 module_i2c_addresses[mid] : NULL) != NULL)) {
		/* Second chance: Try default i2c address list */
		i2ccnt = pvr2_copy_i2c_addr_list(i2caddr, p,
						 ARRAY_SIZE(i2caddr));
		if (i2ccnt) {
			pvr2_trace(PVR2_TRACE_INIT,
				   "Module ID %u:"
				   " Using default i2c address list",
				   mid);
		}
	}

	if (!i2ccnt) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "Module ID %u (%s) for device %s:"
			   " No i2c addresses."
			   "  The driver might have a configuration problem.",
			   mid, fname, hdw->hdw_desc->description);
		return -EINVAL;
	}

	if (i2ccnt == 1) {
		pvr2_trace(PVR2_TRACE_INIT,
			   "Module ID %u:"
			   " Setting up with specified i2c address 0x%x",
			   mid, i2caddr[0]);
		sd = v4l2_i2c_new_subdev(&hdw->v4l2_dev, &hdw->i2c_adap,
					 fname, i2caddr[0], NULL);
	} else {
		pvr2_trace(PVR2_TRACE_INIT,
			   "Module ID %u:"
			   " Setting up with address probe list",
			   mid);
		sd = v4l2_i2c_new_subdev(&hdw->v4l2_dev, &hdw->i2c_adap,
					 fname, 0, i2caddr);
	}

	if (!sd) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "Module ID %u (%s) for device %s failed to load."
			   "  Possible missing sub-device kernel module or"
			   " initialization failure within module.",
			   mid, fname, hdw->hdw_desc->description);
		return -EIO;
	}

	/* Tag this sub-device instance with the module ID we know about.
	   In other places we'll use that tag to determine if the instance
	   requires special handling. */
	sd->grp_id = mid;

	pvr2_trace(PVR2_TRACE_INFO, "Attached sub-driver %s", fname);


	/* client-specific setup... */
	switch (mid) {
	case PVR2_CLIENT_ID_CX25840:
	case PVR2_CLIENT_ID_SAA7115:
		hdw->decoder_client_id = mid;
		break;
	default: break;
	}

	return 0;
}


static void pvr2_hdw_load_modules(struct pvr2_hdw *hdw)
{
	unsigned int idx;
	const struct pvr2_string_table *cm;
	const struct pvr2_device_client_table *ct;
	int okFl = !0;

	cm = &hdw->hdw_desc->client_modules;
	for (idx = 0; idx < cm->cnt; idx++) {
		request_module(cm->lst[idx]);
	}

	ct = &hdw->hdw_desc->client_table;
	for (idx = 0; idx < ct->cnt; idx++) {
		if (pvr2_hdw_load_subdev(hdw, &ct->lst[idx]) < 0) okFl = 0;
	}
	if (!okFl) {
		hdw->flag_modulefail = !0;
		pvr2_hdw_render_useless(hdw);
	}
}


static void pvr2_hdw_setup_low(struct pvr2_hdw *hdw)
{
	int ret;
	unsigned int idx;
	struct pvr2_ctrl *cptr;
	int reloadFl = 0;
	if (hdw->hdw_desc->fx2_firmware.cnt) {
		if (!reloadFl) {
			reloadFl =
				(hdw->usb_intf->cur_altsetting->desc.bNumEndpoints
				 == 0);
			if (reloadFl) {
				pvr2_trace(PVR2_TRACE_INIT,
					   "USB endpoint config looks strange"
					   "; possibly firmware needs to be"
					   " loaded");
			}
		}
		if (!reloadFl) {
			reloadFl = !pvr2_hdw_check_firmware(hdw);
			if (reloadFl) {
				pvr2_trace(PVR2_TRACE_INIT,
					   "Check for FX2 firmware failed"
					   "; possibly firmware needs to be"
					   " loaded");
			}
		}
		if (reloadFl) {
			if (pvr2_upload_firmware1(hdw) != 0) {
				pvr2_trace(PVR2_TRACE_ERROR_LEGS,
					   "Failure uploading firmware1");
			}
			return;
		}
	}
	hdw->fw1_state = FW1_STATE_OK;

	if (!pvr2_hdw_dev_ok(hdw)) return;

	hdw->force_dirty = !0;

	if (!hdw->hdw_desc->flag_no_powerup) {
		pvr2_hdw_cmd_powerup(hdw);
		if (!pvr2_hdw_dev_ok(hdw)) return;
	}

	/* Take the IR chip out of reset, if appropriate */
	if (hdw->ir_scheme_active == PVR2_IR_SCHEME_ZILOG) {
		pvr2_issue_simple_cmd(hdw,
				      FX2CMD_HCW_ZILOG_RESET |
				      (1 << 8) |
				      ((0) << 16));
	}

	// This step MUST happen after the earlier powerup step.
	pvr2_i2c_core_init(hdw);
	if (!pvr2_hdw_dev_ok(hdw)) return;

	pvr2_hdw_load_modules(hdw);
	if (!pvr2_hdw_dev_ok(hdw)) return;

	v4l2_device_call_all(&hdw->v4l2_dev, 0, core, load_fw);

	for (idx = 0; idx < CTRLDEF_COUNT; idx++) {
		cptr = hdw->controls + idx;
		if (cptr->info->skip_init) continue;
		if (!cptr->info->set_value) continue;
		cptr->info->set_value(cptr,~0,cptr->info->default_value);
	}

	pvr2_hdw_cx25840_vbi_hack(hdw);

	/* Set up special default values for the television and radio
	   frequencies here.  It's not really important what these defaults
	   are, but I set them to something usable in the Chicago area just
	   to make driver testing a little easier. */

	hdw->freqValTelevision = default_tv_freq;
	hdw->freqValRadio = default_radio_freq;

	// Do not use pvr2_reset_ctl_endpoints() here.  It is not
	// thread-safe against the normal pvr2_send_request() mechanism.
	// (We should make it thread safe).

	if (hdw->hdw_desc->flag_has_hauppauge_rom) {
		ret = pvr2_hdw_get_eeprom_addr(hdw);
		if (!pvr2_hdw_dev_ok(hdw)) return;
		if (ret < 0) {
			pvr2_trace(PVR2_TRACE_ERROR_LEGS,
				   "Unable to determine location of eeprom,"
				   " skipping");
		} else {
			hdw->eeprom_addr = ret;
			pvr2_eeprom_analyze(hdw);
			if (!pvr2_hdw_dev_ok(hdw)) return;
		}
	} else {
		hdw->tuner_type = hdw->hdw_desc->default_tuner_type;
		hdw->tuner_updated = !0;
		hdw->std_mask_eeprom = V4L2_STD_ALL;
	}

	if (hdw->serial_number) {
		idx = scnprintf(hdw->identifier, sizeof(hdw->identifier) - 1,
				"sn-%lu", hdw->serial_number);
	} else if (hdw->unit_number >= 0) {
		idx = scnprintf(hdw->identifier, sizeof(hdw->identifier) - 1,
				"unit-%c",
				hdw->unit_number + 'a');
	} else {
		idx = scnprintf(hdw->identifier, sizeof(hdw->identifier) - 1,
				"unit-??");
	}
	hdw->identifier[idx] = 0;

	pvr2_hdw_setup_std(hdw);

	if (!get_default_tuner_type(hdw)) {
		pvr2_trace(PVR2_TRACE_INIT,
			   "pvr2_hdw_setup: Tuner type overridden to %d",
			   hdw->tuner_type);
	}


	if (!pvr2_hdw_dev_ok(hdw)) return;

	if (hdw->hdw_desc->signal_routing_scheme ==
	    PVR2_ROUTING_SCHEME_GOTVIEW) {
		/* Ensure that GPIO 11 is set to output for GOTVIEW
		   hardware. */
		pvr2_hdw_gpio_chg_dir(hdw,(1 << 11),~0);
	}

	pvr2_hdw_commit_setup(hdw);

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

	hdw->flag_init_ok = !0;

	pvr2_hdw_state_sched(hdw);
}


/* Set up the structure and attempt to put the device into a usable state.
   This can be a time-consuming operation, which is why it is not done
   internally as part of the create() step. */
static void pvr2_hdw_setup(struct pvr2_hdw *hdw)
{
	pvr2_trace(PVR2_TRACE_INIT,"pvr2_hdw_setup(hdw=%p) begin",hdw);
	do {
		pvr2_hdw_setup_low(hdw);
		pvr2_trace(PVR2_TRACE_INIT,
			   "pvr2_hdw_setup(hdw=%p) done, ok=%d init_ok=%d",
			   hdw,pvr2_hdw_dev_ok(hdw),hdw->flag_init_ok);
		if (pvr2_hdw_dev_ok(hdw)) {
			if (hdw->flag_init_ok) {
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
		if (hdw->flag_modulefail) {
			pvr2_trace(
				PVR2_TRACE_ERROR_LEGS,
				"***WARNING*** pvrusb2 driver initialization"
				" failed due to the failure of one or more"
				" sub-device kernel modules.");
			pvr2_trace(
				PVR2_TRACE_ERROR_LEGS,
				"You need to resolve the failing condition"
				" before this driver can function.  There"
				" should be some earlier messages giving more"
				" information about the problem.");
			break;
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
	} while (0);
	pvr2_trace(PVR2_TRACE_INIT,"pvr2_hdw_setup(hdw=%p) end",hdw);
}


/* Perform second stage initialization.  Set callback pointer first so that
   we can avoid a possible initialization race (if the kernel thread runs
   before the callback has been set). */
int pvr2_hdw_initialize(struct pvr2_hdw *hdw,
			void (*callback_func)(void *),
			void *callback_data)
{
	LOCK_TAKE(hdw->big_lock); do {
		if (hdw->flag_disconnected) {
			/* Handle a race here: If we're already
			   disconnected by this point, then give up.  If we
			   get past this then we'll remain connected for
			   the duration of initialization since the entire
			   initialization sequence is now protected by the
			   big_lock. */
			break;
		}
		hdw->state_data = callback_data;
		hdw->state_func = callback_func;
		pvr2_hdw_setup(hdw);
	} while (0); LOCK_GIVE(hdw->big_lock);
	return hdw->flag_init_ok;
}


/* Create, set up, and return a structure for interacting with the
   underlying hardware.  */
struct pvr2_hdw *pvr2_hdw_create(struct usb_interface *intf,
				 const struct usb_device_id *devid)
{
	unsigned int idx,cnt1,cnt2,m;
	struct pvr2_hdw *hdw = NULL;
	int valid_std_mask;
	struct pvr2_ctrl *cptr;
	struct usb_device *usb_dev;
	const struct pvr2_device_desc *hdw_desc;
	__u8 ifnum;
	struct v4l2_queryctrl qctrl;
	struct pvr2_ctl_info *ciptr;

	usb_dev = interface_to_usbdev(intf);

	hdw_desc = (const struct pvr2_device_desc *)(devid->driver_info);

	if (hdw_desc == NULL) {
		pvr2_trace(PVR2_TRACE_INIT, "pvr2_hdw_create:"
			   " No device description pointer,"
			   " unable to continue.");
		pvr2_trace(PVR2_TRACE_INIT, "If you have a new device type,"
			   " please contact Mike Isely <isely@pobox.com>"
			   " to get it included in the driver\n");
		goto fail;
	}

	hdw = kzalloc(sizeof(*hdw),GFP_KERNEL);
	pvr2_trace(PVR2_TRACE_INIT,"pvr2_hdw_create: hdw=%p, type \"%s\"",
		   hdw,hdw_desc->description);
	pvr2_trace(PVR2_TRACE_INFO, "Hardware description: %s",
		hdw_desc->description);
	if (hdw_desc->flag_is_experimental) {
		pvr2_trace(PVR2_TRACE_INFO, "**********");
		pvr2_trace(PVR2_TRACE_INFO,
			   "WARNING: Support for this device (%s) is"
			   " experimental.", hdw_desc->description);
		pvr2_trace(PVR2_TRACE_INFO,
			   "Important functionality might not be"
			   " entirely working.");
		pvr2_trace(PVR2_TRACE_INFO,
			   "Please consider contacting the driver author to"
			   " help with further stabilization of the driver.");
		pvr2_trace(PVR2_TRACE_INFO, "**********");
	}
	if (!hdw) goto fail;

	init_timer(&hdw->quiescent_timer);
	hdw->quiescent_timer.data = (unsigned long)hdw;
	hdw->quiescent_timer.function = pvr2_hdw_quiescent_timeout;

	init_timer(&hdw->decoder_stabilization_timer);
	hdw->decoder_stabilization_timer.data = (unsigned long)hdw;
	hdw->decoder_stabilization_timer.function =
		pvr2_hdw_decoder_stabilization_timeout;

	init_timer(&hdw->encoder_wait_timer);
	hdw->encoder_wait_timer.data = (unsigned long)hdw;
	hdw->encoder_wait_timer.function = pvr2_hdw_encoder_wait_timeout;

	init_timer(&hdw->encoder_run_timer);
	hdw->encoder_run_timer.data = (unsigned long)hdw;
	hdw->encoder_run_timer.function = pvr2_hdw_encoder_run_timeout;

	hdw->master_state = PVR2_STATE_DEAD;

	init_waitqueue_head(&hdw->state_wait_data);

	hdw->tuner_signal_stale = !0;
	cx2341x_fill_defaults(&hdw->enc_ctl_state);

	/* Calculate which inputs are OK */
	m = 0;
	if (hdw_desc->flag_has_analogtuner) m |= 1 << PVR2_CVAL_INPUT_TV;
	if (hdw_desc->digital_control_scheme != PVR2_DIGITAL_SCHEME_NONE) {
		m |= 1 << PVR2_CVAL_INPUT_DTV;
	}
	if (hdw_desc->flag_has_svideo) m |= 1 << PVR2_CVAL_INPUT_SVIDEO;
	if (hdw_desc->flag_has_composite) m |= 1 << PVR2_CVAL_INPUT_COMPOSITE;
	if (hdw_desc->flag_has_fmradio) m |= 1 << PVR2_CVAL_INPUT_RADIO;
	hdw->input_avail_mask = m;
	hdw->input_allowed_mask = hdw->input_avail_mask;

	/* If not a hybrid device, pathway_state never changes.  So
	   initialize it here to what it should forever be. */
	if (!(hdw->input_avail_mask & (1 << PVR2_CVAL_INPUT_DTV))) {
		hdw->pathway_state = PVR2_PATHWAY_ANALOG;
	} else if (!(hdw->input_avail_mask & (1 << PVR2_CVAL_INPUT_TV))) {
		hdw->pathway_state = PVR2_PATHWAY_DIGITAL;
	}

	hdw->control_cnt = CTRLDEF_COUNT;
	hdw->control_cnt += MPEGDEF_COUNT;
	hdw->controls = kzalloc(sizeof(struct pvr2_ctrl) * hdw->control_cnt,
				GFP_KERNEL);
	if (!hdw->controls) goto fail;
	hdw->hdw_desc = hdw_desc;
	hdw->ir_scheme_active = hdw->hdw_desc->ir_scheme;
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

	/* Ensure that default input choice is a valid one. */
	m = hdw->input_avail_mask;
	if (m) for (idx = 0; idx < (sizeof(m) << 3); idx++) {
		if (!((1 << idx) & m)) continue;
		hdw->input_val = idx;
		break;
	}

	/* Define and configure additional controls from cx2341x module. */
	hdw->mpeg_ctrl_info = kzalloc(
		sizeof(*(hdw->mpeg_ctrl_info)) * MPEGDEF_COUNT, GFP_KERNEL);
	if (!hdw->mpeg_ctrl_info) goto fail;
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
				cx2341x_ctrl_get_menu(&hdw->enc_ctl_state,
								ciptr->v4l_id);
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

	hdw->cropcap_stale = !0;
	hdw->eeprom_addr = -1;
	hdw->unit_number = -1;
	hdw->v4l_minor_number_video = -1;
	hdw->v4l_minor_number_vbi = -1;
	hdw->v4l_minor_number_radio = -1;
	hdw->ctl_write_buffer = kmalloc(PVR2_CTL_BUFFSIZE,GFP_KERNEL);
	if (!hdw->ctl_write_buffer) goto fail;
	hdw->ctl_read_buffer = kmalloc(PVR2_CTL_BUFFSIZE,GFP_KERNEL);
	if (!hdw->ctl_read_buffer) goto fail;
	hdw->ctl_write_urb = usb_alloc_urb(0,GFP_KERNEL);
	if (!hdw->ctl_write_urb) goto fail;
	hdw->ctl_read_urb = usb_alloc_urb(0,GFP_KERNEL);
	if (!hdw->ctl_read_urb) goto fail;

	if (v4l2_device_register(&intf->dev, &hdw->v4l2_dev) != 0) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "Error registering with v4l core, giving up");
		goto fail;
	}
	mutex_lock(&pvr2_unit_mtx); do {
		for (idx = 0; idx < PVR_NUM; idx++) {
			if (unit_pointers[idx]) continue;
			hdw->unit_number = idx;
			unit_pointers[idx] = hdw;
			break;
		}
	} while (0); mutex_unlock(&pvr2_unit_mtx);

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

	hdw->workqueue = create_singlethread_workqueue(hdw->name);
	INIT_WORK(&hdw->workpoll,pvr2_hdw_worker_poll);

	pvr2_trace(PVR2_TRACE_INIT,"Driver unit number is %d, name is %s",
		   hdw->unit_number,hdw->name);

	hdw->tuner_type = -1;
	hdw->flag_ok = !0;

	hdw->usb_intf = intf;
	hdw->usb_dev = usb_dev;

	usb_make_path(hdw->usb_dev, hdw->bus_info, sizeof(hdw->bus_info));

	ifnum = hdw->usb_intf->cur_altsetting->desc.bInterfaceNumber;
	usb_set_interface(hdw->usb_dev,ifnum,0);

	mutex_init(&hdw->ctl_lock_mutex);
	mutex_init(&hdw->big_lock_mutex);

	return hdw;
 fail:
	if (hdw) {
		del_timer_sync(&hdw->quiescent_timer);
		del_timer_sync(&hdw->decoder_stabilization_timer);
		del_timer_sync(&hdw->encoder_run_timer);
		del_timer_sync(&hdw->encoder_wait_timer);
		if (hdw->workqueue) {
			flush_workqueue(hdw->workqueue);
			destroy_workqueue(hdw->workqueue);
			hdw->workqueue = NULL;
		}
		usb_free_urb(hdw->ctl_read_urb);
		usb_free_urb(hdw->ctl_write_urb);
		kfree(hdw->ctl_read_buffer);
		kfree(hdw->ctl_write_buffer);
		kfree(hdw->controls);
		kfree(hdw->mpeg_ctrl_info);
		kfree(hdw->std_defs);
		kfree(hdw->std_enum_names);
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
	hdw->flag_disconnected = !0;
	/* If we don't do this, then there will be a dangling struct device
	   reference to our disappearing device persisting inside the V4L
	   core... */
	v4l2_device_disconnect(&hdw->v4l2_dev);
	hdw->usb_dev = NULL;
	hdw->usb_intf = NULL;
	pvr2_hdw_render_useless(hdw);
}


/* Destroy hardware interaction structure */
void pvr2_hdw_destroy(struct pvr2_hdw *hdw)
{
	if (!hdw) return;
	pvr2_trace(PVR2_TRACE_INIT,"pvr2_hdw_destroy: hdw=%p",hdw);
	if (hdw->workqueue) {
		flush_workqueue(hdw->workqueue);
		destroy_workqueue(hdw->workqueue);
		hdw->workqueue = NULL;
	}
	del_timer_sync(&hdw->quiescent_timer);
	del_timer_sync(&hdw->decoder_stabilization_timer);
	del_timer_sync(&hdw->encoder_run_timer);
	del_timer_sync(&hdw->encoder_wait_timer);
	if (hdw->fw_buffer) {
		kfree(hdw->fw_buffer);
		hdw->fw_buffer = NULL;
	}
	if (hdw->vid_stream) {
		pvr2_stream_destroy(hdw->vid_stream);
		hdw->vid_stream = NULL;
	}
	pvr2_i2c_core_done(hdw);
	v4l2_device_unregister(&hdw->v4l2_dev);
	pvr2_hdw_remove_usb_stuff(hdw);
	mutex_lock(&pvr2_unit_mtx); do {
		if ((hdw->unit_number >= 0) &&
		    (hdw->unit_number < PVR_NUM) &&
		    (unit_pointers[hdw->unit_number] == hdw)) {
			unit_pointers[hdw->unit_number] = NULL;
		}
	} while (0); mutex_unlock(&pvr2_unit_mtx);
	kfree(hdw->controls);
	kfree(hdw->mpeg_ctrl_info);
	kfree(hdw->std_defs);
	kfree(hdw->std_enum_names);
	kfree(hdw);
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


static void pvr2_subdev_set_control(struct pvr2_hdw *hdw, int id,
				    const char *name, int val)
{
	struct v4l2_control ctrl;
	pvr2_trace(PVR2_TRACE_CHIPS, "subdev v4l2 %s=%d", name, val);
	memset(&ctrl, 0, sizeof(ctrl));
	ctrl.id = id;
	ctrl.value = val;
	v4l2_device_call_all(&hdw->v4l2_dev, 0, core, s_ctrl, &ctrl);
}

#define PVR2_SUBDEV_SET_CONTROL(hdw, id, lab) \
	if ((hdw)->lab##_dirty || (hdw)->force_dirty) {		\
		pvr2_subdev_set_control(hdw, id, #lab, (hdw)->lab##_val); \
	}

/* Execute whatever commands are required to update the state of all the
   sub-devices so that they match our current control values. */
static void pvr2_subdev_update(struct pvr2_hdw *hdw)
{
	struct v4l2_subdev *sd;
	unsigned int id;
	pvr2_subdev_update_func fp;

	pvr2_trace(PVR2_TRACE_CHIPS, "subdev update...");

	if (hdw->tuner_updated || hdw->force_dirty) {
		struct tuner_setup setup;
		pvr2_trace(PVR2_TRACE_CHIPS, "subdev tuner set_type(%d)",
			   hdw->tuner_type);
		if (((int)(hdw->tuner_type)) >= 0) {
			memset(&setup, 0, sizeof(setup));
			setup.addr = ADDR_UNSET;
			setup.type = hdw->tuner_type;
			setup.mode_mask = T_RADIO | T_ANALOG_TV;
			v4l2_device_call_all(&hdw->v4l2_dev, 0,
					     tuner, s_type_addr, &setup);
		}
	}

	if (hdw->input_dirty || hdw->std_dirty || hdw->force_dirty) {
		pvr2_trace(PVR2_TRACE_CHIPS, "subdev v4l2 set_standard");
		if (hdw->input_val == PVR2_CVAL_INPUT_RADIO) {
			v4l2_device_call_all(&hdw->v4l2_dev, 0,
					     tuner, s_radio);
		} else {
			v4l2_std_id vs;
			vs = hdw->std_mask_cur;
			v4l2_device_call_all(&hdw->v4l2_dev, 0,
					     core, s_std, vs);
			pvr2_hdw_cx25840_vbi_hack(hdw);
		}
		hdw->tuner_signal_stale = !0;
		hdw->cropcap_stale = !0;
	}

	PVR2_SUBDEV_SET_CONTROL(hdw, V4L2_CID_BRIGHTNESS, brightness);
	PVR2_SUBDEV_SET_CONTROL(hdw, V4L2_CID_CONTRAST, contrast);
	PVR2_SUBDEV_SET_CONTROL(hdw, V4L2_CID_SATURATION, saturation);
	PVR2_SUBDEV_SET_CONTROL(hdw, V4L2_CID_HUE, hue);
	PVR2_SUBDEV_SET_CONTROL(hdw, V4L2_CID_AUDIO_MUTE, mute);
	PVR2_SUBDEV_SET_CONTROL(hdw, V4L2_CID_AUDIO_VOLUME, volume);
	PVR2_SUBDEV_SET_CONTROL(hdw, V4L2_CID_AUDIO_BALANCE, balance);
	PVR2_SUBDEV_SET_CONTROL(hdw, V4L2_CID_AUDIO_BASS, bass);
	PVR2_SUBDEV_SET_CONTROL(hdw, V4L2_CID_AUDIO_TREBLE, treble);

	if (hdw->input_dirty || hdw->audiomode_dirty || hdw->force_dirty) {
		struct v4l2_tuner vt;
		memset(&vt, 0, sizeof(vt));
		vt.audmode = hdw->audiomode_val;
		v4l2_device_call_all(&hdw->v4l2_dev, 0, tuner, s_tuner, &vt);
	}

	if (hdw->freqDirty || hdw->force_dirty) {
		unsigned long fv;
		struct v4l2_frequency freq;
		fv = pvr2_hdw_get_cur_freq(hdw);
		pvr2_trace(PVR2_TRACE_CHIPS, "subdev v4l2 set_freq(%lu)", fv);
		if (hdw->tuner_signal_stale) pvr2_hdw_status_poll(hdw);
		memset(&freq, 0, sizeof(freq));
		if (hdw->tuner_signal_info.capability & V4L2_TUNER_CAP_LOW) {
			/* ((fv * 1000) / 62500) */
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
		v4l2_device_call_all(&hdw->v4l2_dev, 0, tuner,
				     s_frequency, &freq);
	}

	if (hdw->res_hor_dirty || hdw->res_ver_dirty || hdw->force_dirty) {
		struct v4l2_mbus_framefmt fmt;
		memset(&fmt, 0, sizeof(fmt));
		fmt.width = hdw->res_hor_val;
		fmt.height = hdw->res_ver_val;
		fmt.code = V4L2_MBUS_FMT_FIXED;
		pvr2_trace(PVR2_TRACE_CHIPS, "subdev v4l2 set_size(%dx%d)",
			   fmt.width, fmt.height);
		v4l2_device_call_all(&hdw->v4l2_dev, 0, video, s_mbus_fmt, &fmt);
	}

	if (hdw->srate_dirty || hdw->force_dirty) {
		u32 val;
		pvr2_trace(PVR2_TRACE_CHIPS, "subdev v4l2 set_audio %d",
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
		v4l2_device_call_all(&hdw->v4l2_dev, 0,
				     audio, s_clock_freq, val);
	}

	/* Unable to set crop parameters; there is apparently no equivalent
	   for VIDIOC_S_CROP */

	v4l2_device_for_each_subdev(sd, &hdw->v4l2_dev) {
		id = sd->grp_id;
		if (id >= ARRAY_SIZE(pvr2_module_update_functions)) continue;
		fp = pvr2_module_update_functions[id];
		if (!fp) continue;
		(*fp)(hdw, sd);
	}

	if (hdw->tuner_signal_stale || hdw->cropcap_stale) {
		pvr2_hdw_status_poll(hdw);
	}
}


/* Figure out if we need to commit control changes.  If so, mark internal
   state flags to indicate this fact and return true.  Otherwise do nothing
   else and return false. */
static int pvr2_hdw_commit_setup(struct pvr2_hdw *hdw)
{
	unsigned int idx;
	struct pvr2_ctrl *cptr;
	int value;
	int commit_flag = hdw->force_dirty;
	char buf[100];
	unsigned int bcnt,ccnt;

	for (idx = 0; idx < hdw->control_cnt; idx++) {
		cptr = hdw->controls + idx;
		if (!cptr->info->is_dirty) continue;
		if (!cptr->info->is_dirty(cptr)) continue;
		commit_flag = !0;

		if (!(pvrusb2_debug & PVR2_TRACE_CTL)) continue;
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

	hdw->state_pipeline_config = 0;
	trace_stbit("state_pipeline_config",hdw->state_pipeline_config);
	pvr2_hdw_state_sched(hdw);

	return !0;
}


/* Perform all operations needed to commit all control changes.  This must
   be performed in synchronization with the pipeline state and is thus
   expected to be called as part of the driver's worker thread.  Return
   true if commit successful, otherwise return false to indicate that
   commit isn't possible at this time. */
static int pvr2_hdw_commit_execute(struct pvr2_hdw *hdw)
{
	unsigned int idx;
	struct pvr2_ctrl *cptr;
	int disruptive_change;

	/* Handle some required side effects when the video standard is
	   changed.... */
	if (hdw->std_dirty) {
		int nvres;
		int gop_size;
		if (hdw->std_mask_cur & V4L2_STD_525_60) {
			nvres = 480;
			gop_size = 15;
		} else {
			nvres = 576;
			gop_size = 12;
		}
		/* Rewrite the vertical resolution to be appropriate to the
		   video standard that has been selected. */
		if (nvres != hdw->res_ver_val) {
			hdw->res_ver_val = nvres;
			hdw->res_ver_dirty = !0;
		}
		/* Rewrite the GOP size to be appropriate to the video
		   standard that has been selected. */
		if (gop_size != hdw->enc_ctl_state.video_gop_size) {
			struct v4l2_ext_controls cs;
			struct v4l2_ext_control c1;
			memset(&cs, 0, sizeof(cs));
			memset(&c1, 0, sizeof(c1));
			cs.controls = &c1;
			cs.count = 1;
			c1.id = V4L2_CID_MPEG_VIDEO_GOP_SIZE;
			c1.value = gop_size;
			cx2341x_ext_ctrls(&hdw->enc_ctl_state, 0, &cs,
					  VIDIOC_S_EXT_CTRLS);
		}
	}

	if (hdw->input_dirty && hdw->state_pathway_ok &&
	    (((hdw->input_val == PVR2_CVAL_INPUT_DTV) ?
	      PVR2_PATHWAY_DIGITAL : PVR2_PATHWAY_ANALOG) !=
	     hdw->pathway_state)) {
		/* Change of mode being asked for... */
		hdw->state_pathway_ok = 0;
		trace_stbit("state_pathway_ok",hdw->state_pathway_ok);
	}
	if (!hdw->state_pathway_ok) {
		/* Can't commit anything until pathway is ok. */
		return 0;
	}
	/* The broadcast decoder can only scale down, so if
	 * res_*_dirty && crop window < output format ==> enlarge crop.
	 *
	 * The mpeg encoder receives fields of res_hor_val dots and
	 * res_ver_val halflines.  Limits: hor<=720, ver<=576.
	 */
	if (hdw->res_hor_dirty && hdw->cropw_val < hdw->res_hor_val) {
		hdw->cropw_val = hdw->res_hor_val;
		hdw->cropw_dirty = !0;
	} else if (hdw->cropw_dirty) {
		hdw->res_hor_dirty = !0;           /* must rescale */
		hdw->res_hor_val = min(720, hdw->cropw_val);
	}
	if (hdw->res_ver_dirty && hdw->croph_val < hdw->res_ver_val) {
		hdw->croph_val = hdw->res_ver_val;
		hdw->croph_dirty = !0;
	} else if (hdw->croph_dirty) {
		int nvres = hdw->std_mask_cur & V4L2_STD_525_60 ? 480 : 576;
		hdw->res_ver_dirty = !0;
		hdw->res_ver_val = min(nvres, hdw->croph_val);
	}

	/* If any of the below has changed, then we can't do the update
	   while the pipeline is running.  Pipeline must be paused first
	   and decoder -> encoder connection be made quiescent before we
	   can proceed. */
	disruptive_change =
		(hdw->std_dirty ||
		 hdw->enc_unsafe_stale ||
		 hdw->srate_dirty ||
		 hdw->res_ver_dirty ||
		 hdw->res_hor_dirty ||
		 hdw->cropw_dirty ||
		 hdw->croph_dirty ||
		 hdw->input_dirty ||
		 (hdw->active_stream_type != hdw->desired_stream_type));
	if (disruptive_change && !hdw->state_pipeline_idle) {
		/* Pipeline is not idle; we can't proceed.  Arrange to
		   cause pipeline to stop so that we can try this again
		   later.... */
		hdw->state_pipeline_pause = !0;
		return 0;
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
		cx2341x_ext_ctrls(&hdw->enc_ctl_state, 0, &cs,VIDIOC_S_EXT_CTRLS);
	}

	if (hdw->active_stream_type != hdw->desired_stream_type) {
		/* Handle any side effects of stream config here */
		hdw->active_stream_type = hdw->desired_stream_type;
	}

	if (hdw->hdw_desc->signal_routing_scheme ==
	    PVR2_ROUTING_SCHEME_GOTVIEW) {
		u32 b;
		/* Handle GOTVIEW audio switching */
		pvr2_hdw_gpio_get_out(hdw,&b);
		if (hdw->input_val == PVR2_CVAL_INPUT_RADIO) {
			/* Set GPIO 11 */
			pvr2_hdw_gpio_chg_out(hdw,(1 << 11),~0);
		} else {
			/* Clear GPIO 11 */
			pvr2_hdw_gpio_chg_out(hdw,(1 << 11),0);
		}
	}

	/* Check and update state for all sub-devices. */
	pvr2_subdev_update(hdw);

	hdw->tuner_updated = 0;
	hdw->force_dirty = 0;
	for (idx = 0; idx < hdw->control_cnt; idx++) {
		cptr = hdw->controls + idx;
		if (!cptr->info->clear_dirty) continue;
		cptr->info->clear_dirty(cptr);
	}

	if ((hdw->pathway_state == PVR2_PATHWAY_ANALOG) &&
	    hdw->state_encoder_run) {
		/* If encoder isn't running or it can't be touched, then
		   this will get worked out later when we start the
		   encoder. */
		if (pvr2_encoder_adjust(hdw) < 0) return !0;
	}

	hdw->state_pipeline_config = !0;
	/* Hardware state may have changed in a way to cause the cropping
	   capabilities to have changed.  So mark it stale, which will
	   cause a later re-fetch. */
	trace_stbit("state_pipeline_config",hdw->state_pipeline_config);
	return !0;
}


int pvr2_hdw_commit_ctl(struct pvr2_hdw *hdw)
{
	int fl;
	LOCK_TAKE(hdw->big_lock);
	fl = pvr2_hdw_commit_setup(hdw);
	LOCK_GIVE(hdw->big_lock);
	if (!fl) return 0;
	return pvr2_hdw_wait(hdw,0);
}


static void pvr2_hdw_worker_poll(struct work_struct *work)
{
	int fl = 0;
	struct pvr2_hdw *hdw = container_of(work,struct pvr2_hdw,workpoll);
	LOCK_TAKE(hdw->big_lock); do {
		fl = pvr2_hdw_state_eval(hdw);
	} while (0); LOCK_GIVE(hdw->big_lock);
	if (fl && hdw->state_func) {
		hdw->state_func(hdw->state_data);
	}
}


static int pvr2_hdw_wait(struct pvr2_hdw *hdw,int state)
{
	return wait_event_interruptible(
		hdw->state_wait_data,
		(hdw->state_stale == 0) &&
		(!state || (hdw->master_state != state)));
}


/* Return name for this driver instance */
const char *pvr2_hdw_get_driver_name(struct pvr2_hdw *hdw)
{
	return hdw->name;
}


const char *pvr2_hdw_get_desc(struct pvr2_hdw *hdw)
{
	return hdw->hdw_desc->description;
}


const char *pvr2_hdw_get_type(struct pvr2_hdw *hdw)
{
	return hdw->hdw_desc->shortname;
}


int pvr2_hdw_is_hsm(struct pvr2_hdw *hdw)
{
	int result;
	LOCK_TAKE(hdw->ctl_lock); do {
		hdw->cmd_buffer[0] = FX2CMD_GET_USB_SPEED;
		result = pvr2_send_request(hdw,
					   hdw->cmd_buffer,1,
					   hdw->cmd_buffer,1);
		if (result < 0) break;
		result = (hdw->cmd_buffer[0] != 0);
	} while(0); LOCK_GIVE(hdw->ctl_lock);
	return result;
}


/* Execute poll of tuner status */
void pvr2_hdw_execute_tuner_poll(struct pvr2_hdw *hdw)
{
	LOCK_TAKE(hdw->big_lock); do {
		pvr2_hdw_status_poll(hdw);
	} while (0); LOCK_GIVE(hdw->big_lock);
}


static int pvr2_hdw_check_cropcap(struct pvr2_hdw *hdw)
{
	if (!hdw->cropcap_stale) {
		return 0;
	}
	pvr2_hdw_status_poll(hdw);
	if (hdw->cropcap_stale) {
		return -EIO;
	}
	return 0;
}


/* Return information about cropping capabilities */
int pvr2_hdw_get_cropcap(struct pvr2_hdw *hdw, struct v4l2_cropcap *pp)
{
	int stat = 0;
	LOCK_TAKE(hdw->big_lock);
	stat = pvr2_hdw_check_cropcap(hdw);
	if (!stat) {
		memcpy(pp, &hdw->cropcap_info, sizeof(hdw->cropcap_info));
	}
	LOCK_GIVE(hdw->big_lock);
	return stat;
}


/* Return information about the tuner */
int pvr2_hdw_get_tuner_status(struct pvr2_hdw *hdw,struct v4l2_tuner *vtp)
{
	LOCK_TAKE(hdw->big_lock); do {
		if (hdw->tuner_signal_stale) {
			pvr2_hdw_status_poll(hdw);
		}
		memcpy(vtp,&hdw->tuner_signal_info,sizeof(struct v4l2_tuner));
	} while (0); LOCK_GIVE(hdw->big_lock);
	return 0;
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
		printk(KERN_INFO "pvrusb2: =================  START STATUS CARD #%d  =================\n", nr);
		v4l2_device_call_all(&hdw->v4l2_dev, 0, core, log_status);
		pvr2_trace(PVR2_TRACE_INFO,"cx2341x config:");
		cx2341x_log_status(&hdw->enc_ctl_state, "pvrusb2");
		pvr2_hdw_state_log_state(hdw);
		printk(KERN_INFO "pvrusb2: ==================  END STATUS CARD #%d  ==================\n", nr);
	} while (0); LOCK_GIVE(hdw->big_lock);
}


/* Grab EEPROM contents, needed for direct method. */
#define EEPROM_SIZE 8192
#define trace_eeprom(...) pvr2_trace(PVR2_TRACE_EEPROM,__VA_ARGS__)
static u8 *pvr2_full_eeprom_fetch(struct pvr2_hdw *hdw)
{
	struct i2c_msg msg[2];
	u8 *eeprom;
	u8 iadd[2];
	u8 addr;
	u16 eepromSize;
	unsigned int offs;
	int ret;
	int mode16 = 0;
	unsigned pcnt,tcnt;
	eeprom = kmalloc(EEPROM_SIZE,GFP_KERNEL);
	if (!eeprom) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "Failed to allocate memory"
			   " required to read eeprom");
		return NULL;
	}

	trace_eeprom("Value for eeprom addr from controller was 0x%x",
		     hdw->eeprom_addr);
	addr = hdw->eeprom_addr;
	/* Seems that if the high bit is set, then the *real* eeprom
	   address is shifted right now bit position (noticed this in
	   newer PVR USB2 hardware) */
	if (addr & 0x80) addr >>= 1;

	/* FX2 documentation states that a 16bit-addressed eeprom is
	   expected if the I2C address is an odd number (yeah, this is
	   strange but it's what they do) */
	mode16 = (addr & 1);
	eepromSize = (mode16 ? EEPROM_SIZE : 256);
	trace_eeprom("Examining %d byte eeprom at location 0x%x"
		     " using %d bit addressing",eepromSize,addr,
		     mode16 ? 16 : 8);

	msg[0].addr = addr;
	msg[0].flags = 0;
	msg[0].len = mode16 ? 2 : 1;
	msg[0].buf = iadd;
	msg[1].addr = addr;
	msg[1].flags = I2C_M_RD;

	/* We have to do the actual eeprom data fetch ourselves, because
	   (1) we're only fetching part of the eeprom, and (2) if we were
	   getting the whole thing our I2C driver can't grab it in one
	   pass - which is what tveeprom is otherwise going to attempt */
	memset(eeprom,0,EEPROM_SIZE);
	for (tcnt = 0; tcnt < EEPROM_SIZE; tcnt += pcnt) {
		pcnt = 16;
		if (pcnt + tcnt > EEPROM_SIZE) pcnt = EEPROM_SIZE-tcnt;
		offs = tcnt + (eepromSize - EEPROM_SIZE);
		if (mode16) {
			iadd[0] = offs >> 8;
			iadd[1] = offs;
		} else {
			iadd[0] = offs;
		}
		msg[1].len = pcnt;
		msg[1].buf = eeprom+tcnt;
		if ((ret = i2c_transfer(&hdw->i2c_adap,
					msg,ARRAY_SIZE(msg))) != 2) {
			pvr2_trace(PVR2_TRACE_ERROR_LEGS,
				   "eeprom fetch set offs err=%d",ret);
			kfree(eeprom);
			return NULL;
		}
	}
	return eeprom;
}


void pvr2_hdw_cpufw_set_enabled(struct pvr2_hdw *hdw,
				int mode,
				int enable_flag)
{
	int ret;
	u16 address;
	unsigned int pipe;
	LOCK_TAKE(hdw->big_lock); do {
		if ((hdw->fw_buffer == NULL) == !enable_flag) break;

		if (!enable_flag) {
			pvr2_trace(PVR2_TRACE_FIRMWARE,
				   "Cleaning up after CPU firmware fetch");
			kfree(hdw->fw_buffer);
			hdw->fw_buffer = NULL;
			hdw->fw_size = 0;
			if (hdw->fw_cpu_flag) {
				/* Now release the CPU.  It will disconnect
				   and reconnect later. */
				pvr2_hdw_cpureset_assert(hdw,0);
			}
			break;
		}

		hdw->fw_cpu_flag = (mode != 2);
		if (hdw->fw_cpu_flag) {
			hdw->fw_size = (mode == 1) ? 0x4000 : 0x2000;
			pvr2_trace(PVR2_TRACE_FIRMWARE,
				   "Preparing to suck out CPU firmware"
				   " (size=%u)", hdw->fw_size);
			hdw->fw_buffer = kzalloc(hdw->fw_size,GFP_KERNEL);
			if (!hdw->fw_buffer) {
				hdw->fw_size = 0;
				break;
			}

			/* We have to hold the CPU during firmware upload. */
			pvr2_hdw_cpureset_assert(hdw,1);

			/* download the firmware from address 0000-1fff in 2048
			   (=0x800) bytes chunk. */

			pvr2_trace(PVR2_TRACE_FIRMWARE,
				   "Grabbing CPU firmware");
			pipe = usb_rcvctrlpipe(hdw->usb_dev, 0);
			for(address = 0; address < hdw->fw_size;
			    address += 0x800) {
				ret = usb_control_msg(hdw->usb_dev,pipe,
						      0xa0,0xc0,
						      address,0,
						      hdw->fw_buffer+address,
						      0x800,HZ);
				if (ret < 0) break;
			}

			pvr2_trace(PVR2_TRACE_FIRMWARE,
				   "Done grabbing CPU firmware");
		} else {
			pvr2_trace(PVR2_TRACE_FIRMWARE,
				   "Sucking down EEPROM contents");
			hdw->fw_buffer = pvr2_full_eeprom_fetch(hdw);
			if (!hdw->fw_buffer) {
				pvr2_trace(PVR2_TRACE_FIRMWARE,
					   "EEPROM content suck failed.");
				break;
			}
			hdw->fw_size = EEPROM_SIZE;
			pvr2_trace(PVR2_TRACE_FIRMWARE,
				   "Done sucking down EEPROM contents");
		}

	} while (0); LOCK_GIVE(hdw->big_lock);
}


/* Return true if we're in a mode for retrieval CPU firmware */
int pvr2_hdw_cpufw_get_enabled(struct pvr2_hdw *hdw)
{
	return hdw->fw_buffer != NULL;
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


int pvr2_hdw_v4l_get_minor_number(struct pvr2_hdw *hdw,
				  enum pvr2_v4l_type index)
{
	switch (index) {
	case pvr2_v4l_type_video: return hdw->v4l_minor_number_video;
	case pvr2_v4l_type_vbi: return hdw->v4l_minor_number_vbi;
	case pvr2_v4l_type_radio: return hdw->v4l_minor_number_radio;
	default: return -1;
	}
}


/* Store a v4l minor device number */
void pvr2_hdw_v4l_store_minor_number(struct pvr2_hdw *hdw,
				     enum pvr2_v4l_type index,int v)
{
	switch (index) {
	case pvr2_v4l_type_video: hdw->v4l_minor_number_video = v;
	case pvr2_v4l_type_vbi: hdw->v4l_minor_number_vbi = v;
	case pvr2_v4l_type_radio: hdw->v4l_minor_number_radio = v;
	default: break;
	}
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
		if (hdw->ctl_write_pend_flag)
			usb_unlink_urb(hdw->ctl_write_urb);
		if (hdw->ctl_read_pend_flag)
			usb_unlink_urb(hdw->ctl_read_urb);
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
	if (!hdw->flag_ok && !probe_fl) {
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
		pvr2_hdw_render_useless(hdw);
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


static int pvr2_issue_simple_cmd(struct pvr2_hdw *hdw,u32 cmdcode)
{
	int ret;
	unsigned int cnt = 1;
	unsigned int args = 0;
	LOCK_TAKE(hdw->ctl_lock);
	hdw->cmd_buffer[0] = cmdcode & 0xffu;
	args = (cmdcode >> 8) & 0xffu;
	args = (args > 2) ? 2 : args;
	if (args) {
		cnt += args;
		hdw->cmd_buffer[1] = (cmdcode >> 16) & 0xffu;
		if (args > 1) {
			hdw->cmd_buffer[2] = (cmdcode >> 24) & 0xffu;
		}
	}
	if (pvrusb2_debug & PVR2_TRACE_INIT) {
		unsigned int idx;
		unsigned int ccnt,bcnt;
		char tbuf[50];
		cmdcode &= 0xffu;
		bcnt = 0;
		ccnt = scnprintf(tbuf+bcnt,
				 sizeof(tbuf)-bcnt,
				 "Sending FX2 command 0x%x",cmdcode);
		bcnt += ccnt;
		for (idx = 0; idx < ARRAY_SIZE(pvr2_fx2cmd_desc); idx++) {
			if (pvr2_fx2cmd_desc[idx].id == cmdcode) {
				ccnt = scnprintf(tbuf+bcnt,
						 sizeof(tbuf)-bcnt,
						 " \"%s\"",
						 pvr2_fx2cmd_desc[idx].desc);
				bcnt += ccnt;
				break;
			}
		}
		if (args) {
			ccnt = scnprintf(tbuf+bcnt,
					 sizeof(tbuf)-bcnt,
					 " (%u",hdw->cmd_buffer[1]);
			bcnt += ccnt;
			if (args > 1) {
				ccnt = scnprintf(tbuf+bcnt,
						 sizeof(tbuf)-bcnt,
						 ",%u",hdw->cmd_buffer[2]);
				bcnt += ccnt;
			}
			ccnt = scnprintf(tbuf+bcnt,
					 sizeof(tbuf)-bcnt,
					 ")");
			bcnt += ccnt;
		}
		pvr2_trace(PVR2_TRACE_INIT,"%.*s",bcnt,tbuf);
	}
	ret = pvr2_send_request(hdw,hdw->cmd_buffer,cnt,NULL,0);
	LOCK_GIVE(hdw->ctl_lock);
	return ret;
}


int pvr2_write_register(struct pvr2_hdw *hdw, u16 reg, u32 data)
{
	int ret;

	LOCK_TAKE(hdw->ctl_lock);

	hdw->cmd_buffer[0] = FX2CMD_REG_WRITE;  /* write register prefix */
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

	hdw->cmd_buffer[0] = FX2CMD_REG_READ;  /* read register prefix */
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


void pvr2_hdw_render_useless(struct pvr2_hdw *hdw)
{
	if (!hdw->flag_ok) return;
	pvr2_trace(PVR2_TRACE_ERROR_LEGS,
		   "Device being rendered inoperable");
	if (hdw->vid_stream) {
		pvr2_stream_setup(hdw->vid_stream,NULL,0,0);
	}
	hdw->flag_ok = 0;
	trace_stbit("flag_ok",hdw->flag_ok);
	pvr2_hdw_state_sched(hdw);
}


void pvr2_hdw_device_reset(struct pvr2_hdw *hdw)
{
	int ret;
	pvr2_trace(PVR2_TRACE_INIT,"Performing a device reset...");
	ret = usb_lock_device_for_reset(hdw->usb_dev,NULL);
	if (ret == 0) {
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
	char *da;
	unsigned int pipe;
	int ret;

	if (!hdw->usb_dev) return;

	da = kmalloc(16, GFP_KERNEL);

	if (da == NULL) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "Unable to allocate memory to control CPU reset");
		return;
	}

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

	kfree(da);
}


int pvr2_hdw_cmd_deep_reset(struct pvr2_hdw *hdw)
{
	return pvr2_issue_simple_cmd(hdw,FX2CMD_DEEP_RESET);
}


int pvr2_hdw_cmd_powerup(struct pvr2_hdw *hdw)
{
	return pvr2_issue_simple_cmd(hdw,FX2CMD_POWER_ON);
}


int pvr2_hdw_cmd_powerdown(struct pvr2_hdw *hdw)
{
	return pvr2_issue_simple_cmd(hdw,FX2CMD_POWER_OFF);
}


int pvr2_hdw_cmd_decoder_reset(struct pvr2_hdw *hdw)
{
	pvr2_trace(PVR2_TRACE_INIT,
		   "Requesting decoder reset");
	if (hdw->decoder_client_id) {
		v4l2_device_call_all(&hdw->v4l2_dev, hdw->decoder_client_id,
				     core, reset, 0);
		pvr2_hdw_cx25840_vbi_hack(hdw);
		return 0;
	}
	pvr2_trace(PVR2_TRACE_INIT,
		   "Unable to reset decoder: nothing attached");
	return -ENOTTY;
}


static int pvr2_hdw_cmd_hcw_demod_reset(struct pvr2_hdw *hdw, int onoff)
{
	hdw->flag_ok = !0;
	return pvr2_issue_simple_cmd(hdw,
				     FX2CMD_HCW_DEMOD_RESETIN |
				     (1 << 8) |
				     ((onoff ? 1 : 0) << 16));
}


static int pvr2_hdw_cmd_onair_fe_power_ctrl(struct pvr2_hdw *hdw, int onoff)
{
	hdw->flag_ok = !0;
	return pvr2_issue_simple_cmd(hdw,(onoff ?
					  FX2CMD_ONAIR_DTV_POWER_ON :
					  FX2CMD_ONAIR_DTV_POWER_OFF));
}


static int pvr2_hdw_cmd_onair_digital_path_ctrl(struct pvr2_hdw *hdw,
						int onoff)
{
	return pvr2_issue_simple_cmd(hdw,(onoff ?
					  FX2CMD_ONAIR_DTV_STREAMING_ON :
					  FX2CMD_ONAIR_DTV_STREAMING_OFF));
}


static void pvr2_hdw_cmd_modeswitch(struct pvr2_hdw *hdw,int digitalFl)
{
	int cmode;
	/* Compare digital/analog desired setting with current setting.  If
	   they don't match, fix it... */
	cmode = (digitalFl ? PVR2_PATHWAY_DIGITAL : PVR2_PATHWAY_ANALOG);
	if (cmode == hdw->pathway_state) {
		/* They match; nothing to do */
		return;
	}

	switch (hdw->hdw_desc->digital_control_scheme) {
	case PVR2_DIGITAL_SCHEME_HAUPPAUGE:
		pvr2_hdw_cmd_hcw_demod_reset(hdw,digitalFl);
		if (cmode == PVR2_PATHWAY_ANALOG) {
			/* If moving to analog mode, also force the decoder
			   to reset.  If no decoder is attached, then it's
			   ok to ignore this because if/when the decoder
			   attaches, it will reset itself at that time. */
			pvr2_hdw_cmd_decoder_reset(hdw);
		}
		break;
	case PVR2_DIGITAL_SCHEME_ONAIR:
		/* Supposedly we should always have the power on whether in
		   digital or analog mode.  But for now do what appears to
		   work... */
		pvr2_hdw_cmd_onair_fe_power_ctrl(hdw,digitalFl);
		break;
	default: break;
	}

	pvr2_hdw_untrip_unlocked(hdw);
	hdw->pathway_state = cmode;
}


static void pvr2_led_ctrl_hauppauge(struct pvr2_hdw *hdw, int onoff)
{
	/* change some GPIO data
	 *
	 * note: bit d7 of dir appears to control the LED,
	 * so we shut it off here.
	 *
	 */
	if (onoff) {
		pvr2_hdw_gpio_chg_dir(hdw, 0xffffffff, 0x00000481);
	} else {
		pvr2_hdw_gpio_chg_dir(hdw, 0xffffffff, 0x00000401);
	}
	pvr2_hdw_gpio_chg_out(hdw, 0xffffffff, 0x00000000);
}


typedef void (*led_method_func)(struct pvr2_hdw *,int);

static led_method_func led_methods[] = {
	[PVR2_LED_SCHEME_HAUPPAUGE] = pvr2_led_ctrl_hauppauge,
};


/* Toggle LED */
static void pvr2_led_ctrl(struct pvr2_hdw *hdw,int onoff)
{
	unsigned int scheme_id;
	led_method_func fp;

	if ((!onoff) == (!hdw->led_on)) return;

	hdw->led_on = onoff != 0;

	scheme_id = hdw->hdw_desc->led_scheme;
	if (scheme_id < ARRAY_SIZE(led_methods)) {
		fp = led_methods[scheme_id];
	} else {
		fp = NULL;
	}

	if (fp) (*fp)(hdw,onoff);
}


/* Stop / start video stream transport */
static int pvr2_hdw_cmd_usbstream(struct pvr2_hdw *hdw,int runFl)
{
	int ret;

	/* If we're in analog mode, then just issue the usual analog
	   command. */
	if (hdw->pathway_state == PVR2_PATHWAY_ANALOG) {
		return pvr2_issue_simple_cmd(hdw,
					     (runFl ?
					      FX2CMD_STREAMING_ON :
					      FX2CMD_STREAMING_OFF));
		/*Note: Not reached */
	}

	if (hdw->pathway_state != PVR2_PATHWAY_DIGITAL) {
		/* Whoops, we don't know what mode we're in... */
		return -EINVAL;
	}

	/* To get here we have to be in digital mode.  The mechanism here
	   is unfortunately different for different vendors.  So we switch
	   on the device's digital scheme attribute in order to figure out
	   what to do. */
	switch (hdw->hdw_desc->digital_control_scheme) {
	case PVR2_DIGITAL_SCHEME_HAUPPAUGE:
		return pvr2_issue_simple_cmd(hdw,
					     (runFl ?
					      FX2CMD_HCW_DTV_STREAMING_ON :
					      FX2CMD_HCW_DTV_STREAMING_OFF));
	case PVR2_DIGITAL_SCHEME_ONAIR:
		ret = pvr2_issue_simple_cmd(hdw,
					    (runFl ?
					     FX2CMD_STREAMING_ON :
					     FX2CMD_STREAMING_OFF));
		if (ret) return ret;
		return pvr2_hdw_cmd_onair_digital_path_ctrl(hdw,runFl);
	default:
		return -EINVAL;
	}
}


/* Evaluate whether or not state_pathway_ok can change */
static int state_eval_pathway_ok(struct pvr2_hdw *hdw)
{
	if (hdw->state_pathway_ok) {
		/* Nothing to do if pathway is already ok */
		return 0;
	}
	if (!hdw->state_pipeline_idle) {
		/* Not allowed to change anything if pipeline is not idle */
		return 0;
	}
	pvr2_hdw_cmd_modeswitch(hdw,hdw->input_val == PVR2_CVAL_INPUT_DTV);
	hdw->state_pathway_ok = !0;
	trace_stbit("state_pathway_ok",hdw->state_pathway_ok);
	return !0;
}


/* Evaluate whether or not state_encoder_ok can change */
static int state_eval_encoder_ok(struct pvr2_hdw *hdw)
{
	if (hdw->state_encoder_ok) return 0;
	if (hdw->flag_tripped) return 0;
	if (hdw->state_encoder_run) return 0;
	if (hdw->state_encoder_config) return 0;
	if (hdw->state_decoder_run) return 0;
	if (hdw->state_usbstream_run) return 0;
	if (hdw->pathway_state == PVR2_PATHWAY_DIGITAL) {
		if (!hdw->hdw_desc->flag_digital_requires_cx23416) return 0;
	} else if (hdw->pathway_state != PVR2_PATHWAY_ANALOG) {
		return 0;
	}

	if (pvr2_upload_firmware2(hdw) < 0) {
		hdw->flag_tripped = !0;
		trace_stbit("flag_tripped",hdw->flag_tripped);
		return !0;
	}
	hdw->state_encoder_ok = !0;
	trace_stbit("state_encoder_ok",hdw->state_encoder_ok);
	return !0;
}


/* Evaluate whether or not state_encoder_config can change */
static int state_eval_encoder_config(struct pvr2_hdw *hdw)
{
	if (hdw->state_encoder_config) {
		if (hdw->state_encoder_ok) {
			if (hdw->state_pipeline_req &&
			    !hdw->state_pipeline_pause) return 0;
		}
		hdw->state_encoder_config = 0;
		hdw->state_encoder_waitok = 0;
		trace_stbit("state_encoder_waitok",hdw->state_encoder_waitok);
		/* paranoia - solve race if timer just completed */
		del_timer_sync(&hdw->encoder_wait_timer);
	} else {
		if (!hdw->state_pathway_ok ||
		    (hdw->pathway_state != PVR2_PATHWAY_ANALOG) ||
		    !hdw->state_encoder_ok ||
		    !hdw->state_pipeline_idle ||
		    hdw->state_pipeline_pause ||
		    !hdw->state_pipeline_req ||
		    !hdw->state_pipeline_config) {
			/* We must reset the enforced wait interval if
			   anything has happened that might have disturbed
			   the encoder.  This should be a rare case. */
			if (timer_pending(&hdw->encoder_wait_timer)) {
				del_timer_sync(&hdw->encoder_wait_timer);
			}
			if (hdw->state_encoder_waitok) {
				/* Must clear the state - therefore we did
				   something to a state bit and must also
				   return true. */
				hdw->state_encoder_waitok = 0;
				trace_stbit("state_encoder_waitok",
					    hdw->state_encoder_waitok);
				return !0;
			}
			return 0;
		}
		if (!hdw->state_encoder_waitok) {
			if (!timer_pending(&hdw->encoder_wait_timer)) {
				/* waitok flag wasn't set and timer isn't
				   running.  Check flag once more to avoid
				   a race then start the timer.  This is
				   the point when we measure out a minimal
				   quiet interval before doing something to
				   the encoder. */
				if (!hdw->state_encoder_waitok) {
					hdw->encoder_wait_timer.expires =
						jiffies +
						(HZ * TIME_MSEC_ENCODER_WAIT
						 / 1000);
					add_timer(&hdw->encoder_wait_timer);
				}
			}
			/* We can't continue until we know we have been
			   quiet for the interval measured by this
			   timer. */
			return 0;
		}
		pvr2_encoder_configure(hdw);
		if (hdw->state_encoder_ok) hdw->state_encoder_config = !0;
	}
	trace_stbit("state_encoder_config",hdw->state_encoder_config);
	return !0;
}


/* Return true if the encoder should not be running. */
static int state_check_disable_encoder_run(struct pvr2_hdw *hdw)
{
	if (!hdw->state_encoder_ok) {
		/* Encoder isn't healthy at the moment, so stop it. */
		return !0;
	}
	if (!hdw->state_pathway_ok) {
		/* Mode is not understood at the moment (i.e. it wants to
		   change), so encoder must be stopped. */
		return !0;
	}

	switch (hdw->pathway_state) {
	case PVR2_PATHWAY_ANALOG:
		if (!hdw->state_decoder_run) {
			/* We're in analog mode and the decoder is not
			   running; thus the encoder should be stopped as
			   well. */
			return !0;
		}
		break;
	case PVR2_PATHWAY_DIGITAL:
		if (hdw->state_encoder_runok) {
			/* This is a funny case.  We're in digital mode so
			   really the encoder should be stopped.  However
			   if it really is running, only kill it after
			   runok has been set.  This gives a chance for the
			   onair quirk to function (encoder must run
			   briefly first, at least once, before onair
			   digital streaming can work). */
			return !0;
		}
		break;
	default:
		/* Unknown mode; so encoder should be stopped. */
		return !0;
	}

	/* If we get here, we haven't found a reason to stop the
	   encoder. */
	return 0;
}


/* Return true if the encoder should be running. */
static int state_check_enable_encoder_run(struct pvr2_hdw *hdw)
{
	if (!hdw->state_encoder_ok) {
		/* Don't run the encoder if it isn't healthy... */
		return 0;
	}
	if (!hdw->state_pathway_ok) {
		/* Don't run the encoder if we don't (yet) know what mode
		   we need to be in... */
		return 0;
	}

	switch (hdw->pathway_state) {
	case PVR2_PATHWAY_ANALOG:
		if (hdw->state_decoder_run && hdw->state_decoder_ready) {
			/* In analog mode, if the decoder is running, then
			   run the encoder. */
			return !0;
		}
		break;
	case PVR2_PATHWAY_DIGITAL:
		if ((hdw->hdw_desc->digital_control_scheme ==
		     PVR2_DIGITAL_SCHEME_ONAIR) &&
		    !hdw->state_encoder_runok) {
			/* This is a quirk.  OnAir hardware won't stream
			   digital until the encoder has been run at least
			   once, for a minimal period of time (empiricially
			   measured to be 1/4 second).  So if we're on
			   OnAir hardware and the encoder has never been
			   run at all, then start the encoder.  Normal
			   state machine logic in the driver will
			   automatically handle the remaining bits. */
			return !0;
		}
		break;
	default:
		/* For completeness (unknown mode; encoder won't run ever) */
		break;
	}
	/* If we get here, then we haven't found any reason to run the
	   encoder, so don't run it. */
	return 0;
}


/* Evaluate whether or not state_encoder_run can change */
static int state_eval_encoder_run(struct pvr2_hdw *hdw)
{
	if (hdw->state_encoder_run) {
		if (!state_check_disable_encoder_run(hdw)) return 0;
		if (hdw->state_encoder_ok) {
			del_timer_sync(&hdw->encoder_run_timer);
			if (pvr2_encoder_stop(hdw) < 0) return !0;
		}
		hdw->state_encoder_run = 0;
	} else {
		if (!state_check_enable_encoder_run(hdw)) return 0;
		if (pvr2_encoder_start(hdw) < 0) return !0;
		hdw->state_encoder_run = !0;
		if (!hdw->state_encoder_runok) {
			hdw->encoder_run_timer.expires =
				jiffies + (HZ * TIME_MSEC_ENCODER_OK / 1000);
			add_timer(&hdw->encoder_run_timer);
		}
	}
	trace_stbit("state_encoder_run",hdw->state_encoder_run);
	return !0;
}


/* Timeout function for quiescent timer. */
static void pvr2_hdw_quiescent_timeout(unsigned long data)
{
	struct pvr2_hdw *hdw = (struct pvr2_hdw *)data;
	hdw->state_decoder_quiescent = !0;
	trace_stbit("state_decoder_quiescent",hdw->state_decoder_quiescent);
	hdw->state_stale = !0;
	queue_work(hdw->workqueue,&hdw->workpoll);
}


/* Timeout function for decoder stabilization timer. */
static void pvr2_hdw_decoder_stabilization_timeout(unsigned long data)
{
	struct pvr2_hdw *hdw = (struct pvr2_hdw *)data;
	hdw->state_decoder_ready = !0;
	trace_stbit("state_decoder_ready", hdw->state_decoder_ready);
	hdw->state_stale = !0;
	queue_work(hdw->workqueue, &hdw->workpoll);
}


/* Timeout function for encoder wait timer. */
static void pvr2_hdw_encoder_wait_timeout(unsigned long data)
{
	struct pvr2_hdw *hdw = (struct pvr2_hdw *)data;
	hdw->state_encoder_waitok = !0;
	trace_stbit("state_encoder_waitok",hdw->state_encoder_waitok);
	hdw->state_stale = !0;
	queue_work(hdw->workqueue,&hdw->workpoll);
}


/* Timeout function for encoder run timer. */
static void pvr2_hdw_encoder_run_timeout(unsigned long data)
{
	struct pvr2_hdw *hdw = (struct pvr2_hdw *)data;
	if (!hdw->state_encoder_runok) {
		hdw->state_encoder_runok = !0;
		trace_stbit("state_encoder_runok",hdw->state_encoder_runok);
		hdw->state_stale = !0;
		queue_work(hdw->workqueue,&hdw->workpoll);
	}
}


/* Evaluate whether or not state_decoder_run can change */
static int state_eval_decoder_run(struct pvr2_hdw *hdw)
{
	if (hdw->state_decoder_run) {
		if (hdw->state_encoder_ok) {
			if (hdw->state_pipeline_req &&
			    !hdw->state_pipeline_pause &&
			    hdw->state_pathway_ok) return 0;
		}
		if (!hdw->flag_decoder_missed) {
			pvr2_decoder_enable(hdw,0);
		}
		hdw->state_decoder_quiescent = 0;
		hdw->state_decoder_run = 0;
		/* paranoia - solve race if timer(s) just completed */
		del_timer_sync(&hdw->quiescent_timer);
		/* Kill the stabilization timer, in case we're killing the
		   encoder before the previous stabilization interval has
		   been properly timed. */
		del_timer_sync(&hdw->decoder_stabilization_timer);
		hdw->state_decoder_ready = 0;
	} else {
		if (!hdw->state_decoder_quiescent) {
			if (!timer_pending(&hdw->quiescent_timer)) {
				/* We don't do something about the
				   quiescent timer until right here because
				   we also want to catch cases where the
				   decoder was already not running (like
				   after initialization) as opposed to
				   knowing that we had just stopped it.
				   The second flag check is here to cover a
				   race - the timer could have run and set
				   this flag just after the previous check
				   but before we did the pending check. */
				if (!hdw->state_decoder_quiescent) {
					hdw->quiescent_timer.expires =
						jiffies +
						(HZ * TIME_MSEC_DECODER_WAIT
						 / 1000);
					add_timer(&hdw->quiescent_timer);
				}
			}
			/* Don't allow decoder to start again until it has
			   been quiesced first.  This little detail should
			   hopefully further stabilize the encoder. */
			return 0;
		}
		if (!hdw->state_pathway_ok ||
		    (hdw->pathway_state != PVR2_PATHWAY_ANALOG) ||
		    !hdw->state_pipeline_req ||
		    hdw->state_pipeline_pause ||
		    !hdw->state_pipeline_config ||
		    !hdw->state_encoder_config ||
		    !hdw->state_encoder_ok) return 0;
		del_timer_sync(&hdw->quiescent_timer);
		if (hdw->flag_decoder_missed) return 0;
		if (pvr2_decoder_enable(hdw,!0) < 0) return 0;
		hdw->state_decoder_quiescent = 0;
		hdw->state_decoder_ready = 0;
		hdw->state_decoder_run = !0;
		if (hdw->decoder_client_id == PVR2_CLIENT_ID_SAA7115) {
			hdw->decoder_stabilization_timer.expires =
				jiffies +
				(HZ * TIME_MSEC_DECODER_STABILIZATION_WAIT /
				 1000);
			add_timer(&hdw->decoder_stabilization_timer);
		} else {
			hdw->state_decoder_ready = !0;
		}
	}
	trace_stbit("state_decoder_quiescent",hdw->state_decoder_quiescent);
	trace_stbit("state_decoder_run",hdw->state_decoder_run);
	trace_stbit("state_decoder_ready", hdw->state_decoder_ready);
	return !0;
}


/* Evaluate whether or not state_usbstream_run can change */
static int state_eval_usbstream_run(struct pvr2_hdw *hdw)
{
	if (hdw->state_usbstream_run) {
		int fl = !0;
		if (hdw->pathway_state == PVR2_PATHWAY_ANALOG) {
			fl = (hdw->state_encoder_ok &&
			      hdw->state_encoder_run);
		} else if ((hdw->pathway_state == PVR2_PATHWAY_DIGITAL) &&
			   (hdw->hdw_desc->flag_digital_requires_cx23416)) {
			fl = hdw->state_encoder_ok;
		}
		if (fl &&
		    hdw->state_pipeline_req &&
		    !hdw->state_pipeline_pause &&
		    hdw->state_pathway_ok) {
			return 0;
		}
		pvr2_hdw_cmd_usbstream(hdw,0);
		hdw->state_usbstream_run = 0;
	} else {
		if (!hdw->state_pipeline_req ||
		    hdw->state_pipeline_pause ||
		    !hdw->state_pathway_ok) return 0;
		if (hdw->pathway_state == PVR2_PATHWAY_ANALOG) {
			if (!hdw->state_encoder_ok ||
			    !hdw->state_encoder_run) return 0;
		} else if ((hdw->pathway_state == PVR2_PATHWAY_DIGITAL) &&
			   (hdw->hdw_desc->flag_digital_requires_cx23416)) {
			if (!hdw->state_encoder_ok) return 0;
			if (hdw->state_encoder_run) return 0;
			if (hdw->hdw_desc->digital_control_scheme ==
			    PVR2_DIGITAL_SCHEME_ONAIR) {
				/* OnAir digital receivers won't stream
				   unless the analog encoder has run first.
				   Why?  I have no idea.  But don't even
				   try until we know the analog side is
				   known to have run. */
				if (!hdw->state_encoder_runok) return 0;
			}
		}
		if (pvr2_hdw_cmd_usbstream(hdw,!0) < 0) return 0;
		hdw->state_usbstream_run = !0;
	}
	trace_stbit("state_usbstream_run",hdw->state_usbstream_run);
	return !0;
}


/* Attempt to configure pipeline, if needed */
static int state_eval_pipeline_config(struct pvr2_hdw *hdw)
{
	if (hdw->state_pipeline_config ||
	    hdw->state_pipeline_pause) return 0;
	pvr2_hdw_commit_execute(hdw);
	return !0;
}


/* Update pipeline idle and pipeline pause tracking states based on other
   inputs.  This must be called whenever the other relevant inputs have
   changed. */
static int state_update_pipeline_state(struct pvr2_hdw *hdw)
{
	unsigned int st;
	int updatedFl = 0;
	/* Update pipeline state */
	st = !(hdw->state_encoder_run ||
	       hdw->state_decoder_run ||
	       hdw->state_usbstream_run ||
	       (!hdw->state_decoder_quiescent));
	if (!st != !hdw->state_pipeline_idle) {
		hdw->state_pipeline_idle = st;
		updatedFl = !0;
	}
	if (hdw->state_pipeline_idle && hdw->state_pipeline_pause) {
		hdw->state_pipeline_pause = 0;
		updatedFl = !0;
	}
	return updatedFl;
}


typedef int (*state_eval_func)(struct pvr2_hdw *);

/* Set of functions to be run to evaluate various states in the driver. */
static const state_eval_func eval_funcs[] = {
	state_eval_pathway_ok,
	state_eval_pipeline_config,
	state_eval_encoder_ok,
	state_eval_encoder_config,
	state_eval_decoder_run,
	state_eval_encoder_run,
	state_eval_usbstream_run,
};


/* Process various states and return true if we did anything interesting. */
static int pvr2_hdw_state_update(struct pvr2_hdw *hdw)
{
	unsigned int i;
	int state_updated = 0;
	int check_flag;

	if (!hdw->state_stale) return 0;
	if ((hdw->fw1_state != FW1_STATE_OK) ||
	    !hdw->flag_ok) {
		hdw->state_stale = 0;
		return !0;
	}
	/* This loop is the heart of the entire driver.  It keeps trying to
	   evaluate various bits of driver state until nothing changes for
	   one full iteration.  Each "bit of state" tracks some global
	   aspect of the driver, e.g. whether decoder should run, if
	   pipeline is configured, usb streaming is on, etc.  We separately
	   evaluate each of those questions based on other driver state to
	   arrive at the correct running configuration. */
	do {
		check_flag = 0;
		state_update_pipeline_state(hdw);
		/* Iterate over each bit of state */
		for (i = 0; (i<ARRAY_SIZE(eval_funcs)) && hdw->flag_ok; i++) {
			if ((*eval_funcs[i])(hdw)) {
				check_flag = !0;
				state_updated = !0;
				state_update_pipeline_state(hdw);
			}
		}
	} while (check_flag && hdw->flag_ok);
	hdw->state_stale = 0;
	trace_stbit("state_stale",hdw->state_stale);
	return state_updated;
}


static unsigned int print_input_mask(unsigned int msk,
				     char *buf,unsigned int acnt)
{
	unsigned int idx,ccnt;
	unsigned int tcnt = 0;
	for (idx = 0; idx < ARRAY_SIZE(control_values_input); idx++) {
		if (!((1 << idx) & msk)) continue;
		ccnt = scnprintf(buf+tcnt,
				 acnt-tcnt,
				 "%s%s",
				 (tcnt ? ", " : ""),
				 control_values_input[idx]);
		tcnt += ccnt;
	}
	return tcnt;
}


static const char *pvr2_pathway_state_name(int id)
{
	switch (id) {
	case PVR2_PATHWAY_ANALOG: return "analog";
	case PVR2_PATHWAY_DIGITAL: return "digital";
	default: return "unknown";
	}
}


static unsigned int pvr2_hdw_report_unlocked(struct pvr2_hdw *hdw,int which,
					     char *buf,unsigned int acnt)
{
	switch (which) {
	case 0:
		return scnprintf(
			buf,acnt,
			"driver:%s%s%s%s%s <mode=%s>",
			(hdw->flag_ok ? " <ok>" : " <fail>"),
			(hdw->flag_init_ok ? " <init>" : " <uninitialized>"),
			(hdw->flag_disconnected ? " <disconnected>" :
			 " <connected>"),
			(hdw->flag_tripped ? " <tripped>" : ""),
			(hdw->flag_decoder_missed ? " <no decoder>" : ""),
			pvr2_pathway_state_name(hdw->pathway_state));

	case 1:
		return scnprintf(
			buf,acnt,
			"pipeline:%s%s%s%s",
			(hdw->state_pipeline_idle ? " <idle>" : ""),
			(hdw->state_pipeline_config ?
			 " <configok>" : " <stale>"),
			(hdw->state_pipeline_req ? " <req>" : ""),
			(hdw->state_pipeline_pause ? " <pause>" : ""));
	case 2:
		return scnprintf(
			buf,acnt,
			"worker:%s%s%s%s%s%s%s",
			(hdw->state_decoder_run ?
			 (hdw->state_decoder_ready ?
			  "<decode:run>" : " <decode:start>") :
			 (hdw->state_decoder_quiescent ?
			  "" : " <decode:stop>")),
			(hdw->state_decoder_quiescent ?
			 " <decode:quiescent>" : ""),
			(hdw->state_encoder_ok ?
			 "" : " <encode:init>"),
			(hdw->state_encoder_run ?
			 (hdw->state_encoder_runok ?
			  " <encode:run>" :
			  " <encode:firstrun>") :
			 (hdw->state_encoder_runok ?
			  " <encode:stop>" :
			  " <encode:virgin>")),
			(hdw->state_encoder_config ?
			 " <encode:configok>" :
			 (hdw->state_encoder_waitok ?
			  "" : " <encode:waitok>")),
			(hdw->state_usbstream_run ?
			 " <usb:run>" : " <usb:stop>"),
			(hdw->state_pathway_ok ?
			 " <pathway:ok>" : ""));
	case 3:
		return scnprintf(
			buf,acnt,
			"state: %s",
			pvr2_get_state_name(hdw->master_state));
	case 4: {
		unsigned int tcnt = 0;
		unsigned int ccnt;

		ccnt = scnprintf(buf,
				 acnt,
				 "Hardware supported inputs: ");
		tcnt += ccnt;
		tcnt += print_input_mask(hdw->input_avail_mask,
					 buf+tcnt,
					 acnt-tcnt);
		if (hdw->input_avail_mask != hdw->input_allowed_mask) {
			ccnt = scnprintf(buf+tcnt,
					 acnt-tcnt,
					 "; allowed inputs: ");
			tcnt += ccnt;
			tcnt += print_input_mask(hdw->input_allowed_mask,
						 buf+tcnt,
						 acnt-tcnt);
		}
		return tcnt;
	}
	case 5: {
		struct pvr2_stream_stats stats;
		if (!hdw->vid_stream) break;
		pvr2_stream_get_stats(hdw->vid_stream,
				      &stats,
				      0);
		return scnprintf(
			buf,acnt,
			"Bytes streamed=%u"
			" URBs: queued=%u idle=%u ready=%u"
			" processed=%u failed=%u",
			stats.bytes_processed,
			stats.buffers_in_queue,
			stats.buffers_in_idle,
			stats.buffers_in_ready,
			stats.buffers_processed,
			stats.buffers_failed);
	}
	case 6: {
		unsigned int id = hdw->ir_scheme_active;
		return scnprintf(buf, acnt, "ir scheme: id=%d %s", id,
				 (id >= ARRAY_SIZE(ir_scheme_names) ?
				  "?" : ir_scheme_names[id]));
	}
	default: break;
	}
	return 0;
}


/* Generate report containing info about attached sub-devices and attached
   i2c clients, including an indication of which attached i2c clients are
   actually sub-devices. */
static unsigned int pvr2_hdw_report_clients(struct pvr2_hdw *hdw,
					    char *buf, unsigned int acnt)
{
	struct v4l2_subdev *sd;
	unsigned int tcnt = 0;
	unsigned int ccnt;
	struct i2c_client *client;
	const char *p;
	unsigned int id;

	ccnt = scnprintf(buf, acnt, "Associated v4l2-subdev drivers and I2C clients:\n");
	tcnt += ccnt;
	v4l2_device_for_each_subdev(sd, &hdw->v4l2_dev) {
		id = sd->grp_id;
		p = NULL;
		if (id < ARRAY_SIZE(module_names)) p = module_names[id];
		if (p) {
			ccnt = scnprintf(buf + tcnt, acnt - tcnt, "  %s:", p);
			tcnt += ccnt;
		} else {
			ccnt = scnprintf(buf + tcnt, acnt - tcnt,
					 "  (unknown id=%u):", id);
			tcnt += ccnt;
		}
		client = v4l2_get_subdevdata(sd);
		if (client) {
			ccnt = scnprintf(buf + tcnt, acnt - tcnt,
					 " %s @ %02x\n", client->name,
					 client->addr);
			tcnt += ccnt;
		} else {
			ccnt = scnprintf(buf + tcnt, acnt - tcnt,
					 " no i2c client\n");
			tcnt += ccnt;
		}
	}
	return tcnt;
}


unsigned int pvr2_hdw_state_report(struct pvr2_hdw *hdw,
				   char *buf,unsigned int acnt)
{
	unsigned int bcnt,ccnt,idx;
	bcnt = 0;
	LOCK_TAKE(hdw->big_lock);
	for (idx = 0; ; idx++) {
		ccnt = pvr2_hdw_report_unlocked(hdw,idx,buf,acnt);
		if (!ccnt) break;
		bcnt += ccnt; acnt -= ccnt; buf += ccnt;
		if (!acnt) break;
		buf[0] = '\n'; ccnt = 1;
		bcnt += ccnt; acnt -= ccnt; buf += ccnt;
	}
	ccnt = pvr2_hdw_report_clients(hdw, buf, acnt);
	bcnt += ccnt; acnt -= ccnt; buf += ccnt;
	LOCK_GIVE(hdw->big_lock);
	return bcnt;
}


static void pvr2_hdw_state_log_state(struct pvr2_hdw *hdw)
{
	char buf[256];
	unsigned int idx, ccnt;
	unsigned int lcnt, ucnt;

	for (idx = 0; ; idx++) {
		ccnt = pvr2_hdw_report_unlocked(hdw,idx,buf,sizeof(buf));
		if (!ccnt) break;
		printk(KERN_INFO "%s %.*s\n",hdw->name,ccnt,buf);
	}
	ccnt = pvr2_hdw_report_clients(hdw, buf, sizeof(buf));
	ucnt = 0;
	while (ucnt < ccnt) {
		lcnt = 0;
		while ((lcnt + ucnt < ccnt) && (buf[lcnt + ucnt] != '\n')) {
			lcnt++;
		}
		printk(KERN_INFO "%s %.*s\n", hdw->name, lcnt, buf + ucnt);
		ucnt += lcnt + 1;
	}
}


/* Evaluate and update the driver's current state, taking various actions
   as appropriate for the update. */
static int pvr2_hdw_state_eval(struct pvr2_hdw *hdw)
{
	unsigned int st;
	int state_updated = 0;
	int callback_flag = 0;
	int analog_mode;

	pvr2_trace(PVR2_TRACE_STBITS,
		   "Drive state check START");
	if (pvrusb2_debug & PVR2_TRACE_STBITS) {
		pvr2_hdw_state_log_state(hdw);
	}

	/* Process all state and get back over disposition */
	state_updated = pvr2_hdw_state_update(hdw);

	analog_mode = (hdw->pathway_state != PVR2_PATHWAY_DIGITAL);

	/* Update master state based upon all other states. */
	if (!hdw->flag_ok) {
		st = PVR2_STATE_DEAD;
	} else if (hdw->fw1_state != FW1_STATE_OK) {
		st = PVR2_STATE_COLD;
	} else if ((analog_mode ||
		    hdw->hdw_desc->flag_digital_requires_cx23416) &&
		   !hdw->state_encoder_ok) {
		st = PVR2_STATE_WARM;
	} else if (hdw->flag_tripped ||
		   (analog_mode && hdw->flag_decoder_missed)) {
		st = PVR2_STATE_ERROR;
	} else if (hdw->state_usbstream_run &&
		   (!analog_mode ||
		    (hdw->state_encoder_run && hdw->state_decoder_run))) {
		st = PVR2_STATE_RUN;
	} else {
		st = PVR2_STATE_READY;
	}
	if (hdw->master_state != st) {
		pvr2_trace(PVR2_TRACE_STATE,
			   "Device state change from %s to %s",
			   pvr2_get_state_name(hdw->master_state),
			   pvr2_get_state_name(st));
		pvr2_led_ctrl(hdw,st == PVR2_STATE_RUN);
		hdw->master_state = st;
		state_updated = !0;
		callback_flag = !0;
	}
	if (state_updated) {
		/* Trigger anyone waiting on any state changes here. */
		wake_up(&hdw->state_wait_data);
	}

	if (pvrusb2_debug & PVR2_TRACE_STBITS) {
		pvr2_hdw_state_log_state(hdw);
	}
	pvr2_trace(PVR2_TRACE_STBITS,
		   "Drive state check DONE callback=%d",callback_flag);

	return callback_flag;
}


/* Cause kernel thread to check / update driver state */
static void pvr2_hdw_state_sched(struct pvr2_hdw *hdw)
{
	if (hdw->state_stale) return;
	hdw->state_stale = !0;
	trace_stbit("state_stale",hdw->state_stale);
	queue_work(hdw->workqueue,&hdw->workpoll);
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


void pvr2_hdw_status_poll(struct pvr2_hdw *hdw)
{
	struct v4l2_tuner *vtp = &hdw->tuner_signal_info;
	memset(vtp, 0, sizeof(*vtp));
	hdw->tuner_signal_stale = 0;
	/* Note: There apparently is no replacement for VIDIOC_CROPCAP
	   using v4l2-subdev - therefore we can't support that AT ALL right
	   now.  (Of course, no sub-drivers seem to implement it either.
	   But now it's a a chicken and egg problem...) */
	v4l2_device_call_all(&hdw->v4l2_dev, 0, tuner, g_tuner,
			     &hdw->tuner_signal_info);
	pvr2_trace(PVR2_TRACE_CHIPS, "subdev status poll"
		   " type=%u strength=%u audio=0x%x cap=0x%x"
		   " low=%u hi=%u",
		   vtp->type,
		   vtp->signal, vtp->rxsubchans, vtp->capability,
		   vtp->rangelow, vtp->rangehigh);

	/* We have to do this to avoid getting into constant polling if
	   there's nobody to answer a poll of cropcap info. */
	hdw->cropcap_stale = 0;
}


unsigned int pvr2_hdw_get_input_available(struct pvr2_hdw *hdw)
{
	return hdw->input_avail_mask;
}


unsigned int pvr2_hdw_get_input_allowed(struct pvr2_hdw *hdw)
{
	return hdw->input_allowed_mask;
}


static int pvr2_hdw_set_input(struct pvr2_hdw *hdw,int v)
{
	if (hdw->input_val != v) {
		hdw->input_val = v;
		hdw->input_dirty = !0;
	}

	/* Handle side effects - if we switch to a mode that needs the RF
	   tuner, then select the right frequency choice as well and mark
	   it dirty. */
	if (hdw->input_val == PVR2_CVAL_INPUT_RADIO) {
		hdw->freqSelector = 0;
		hdw->freqDirty = !0;
	} else if ((hdw->input_val == PVR2_CVAL_INPUT_TV) ||
		   (hdw->input_val == PVR2_CVAL_INPUT_DTV)) {
		hdw->freqSelector = 1;
		hdw->freqDirty = !0;
	}
	return 0;
}


int pvr2_hdw_set_input_allowed(struct pvr2_hdw *hdw,
			       unsigned int change_mask,
			       unsigned int change_val)
{
	int ret = 0;
	unsigned int nv,m,idx;
	LOCK_TAKE(hdw->big_lock);
	do {
		nv = hdw->input_allowed_mask & ~change_mask;
		nv |= (change_val & change_mask);
		nv &= hdw->input_avail_mask;
		if (!nv) {
			/* No legal modes left; return error instead. */
			ret = -EPERM;
			break;
		}
		hdw->input_allowed_mask = nv;
		if ((1 << hdw->input_val) & hdw->input_allowed_mask) {
			/* Current mode is still in the allowed mask, so
			   we're done. */
			break;
		}
		/* Select and switch to a mode that is still in the allowed
		   mask */
		if (!hdw->input_allowed_mask) {
			/* Nothing legal; give up */
			break;
		}
		m = hdw->input_allowed_mask;
		for (idx = 0; idx < (sizeof(m) << 3); idx++) {
			if (!((1 << idx) & m)) continue;
			pvr2_hdw_set_input(hdw,idx);
			break;
		}
	} while (0);
	LOCK_GIVE(hdw->big_lock);
	return ret;
}


/* Find I2C address of eeprom */
static int pvr2_hdw_get_eeprom_addr(struct pvr2_hdw *hdw)
{
	int result;
	LOCK_TAKE(hdw->ctl_lock); do {
		hdw->cmd_buffer[0] = FX2CMD_GET_EEPROM_ADDR;
		result = pvr2_send_request(hdw,
					   hdw->cmd_buffer,1,
					   hdw->cmd_buffer,1);
		if (result < 0) break;
		result = hdw->cmd_buffer[0];
	} while(0); LOCK_GIVE(hdw->ctl_lock);
	return result;
}


int pvr2_hdw_register_access(struct pvr2_hdw *hdw,
			     struct v4l2_dbg_match *match, u64 reg_id,
			     int setFl, u64 *val_ptr)
{
#ifdef CONFIG_VIDEO_ADV_DEBUG
	struct v4l2_dbg_register req;
	int stat = 0;
	int okFl = 0;

	if (!capable(CAP_SYS_ADMIN)) return -EPERM;

	req.match = *match;
	req.reg = reg_id;
	if (setFl) req.val = *val_ptr;
	/* It would be nice to know if a sub-device answered the request */
	v4l2_device_call_all(&hdw->v4l2_dev, 0, core, g_register, &req);
	if (!setFl) *val_ptr = req.val;
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
