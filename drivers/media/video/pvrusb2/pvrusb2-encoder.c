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

#include <linux/device.h>   // for linux/firmware.h
#include <linux/firmware.h>
#include "pvrusb2-util.h"
#include "pvrusb2-encoder.h"
#include "pvrusb2-hdw-internal.h"
#include "pvrusb2-debug.h"

static u32 pvr_tbl_emphasis [] = {
	[PVR2_CVAL_AUDIOEMPHASIS_NONE] = 0x0 << 12,
	[PVR2_CVAL_AUDIOEMPHASIS_50_15] = 0x1 << 12,
	[PVR2_CVAL_AUDIOEMPHASIS_CCITT] = 0x3 << 12,
};

static u32 pvr_tbl_srate[] = {
	[PVR2_CVAL_SRATE_48] =  0x01,
	[PVR2_CVAL_SRATE_44_1] = 0x00,
};

static u32 pvr_tbl_audiobitrate[] = {
	[PVR2_CVAL_AUDIOBITRATE_384] = 0xe << 4,
	[PVR2_CVAL_AUDIOBITRATE_320] = 0xd << 4,
	[PVR2_CVAL_AUDIOBITRATE_256] = 0xc << 4,
	[PVR2_CVAL_AUDIOBITRATE_224] = 0xb << 4,
	[PVR2_CVAL_AUDIOBITRATE_192] = 0xa << 4,
	[PVR2_CVAL_AUDIOBITRATE_160] = 0x9 << 4,
	[PVR2_CVAL_AUDIOBITRATE_128] = 0x8 << 4,
	[PVR2_CVAL_AUDIOBITRATE_112] = 0x7 << 4,
	[PVR2_CVAL_AUDIOBITRATE_96]  = 0x6 << 4,
	[PVR2_CVAL_AUDIOBITRATE_80]  = 0x5 << 4,
	[PVR2_CVAL_AUDIOBITRATE_64]  = 0x4 << 4,
	[PVR2_CVAL_AUDIOBITRATE_56]  = 0x3 << 4,
	[PVR2_CVAL_AUDIOBITRATE_48]  = 0x2 << 4,
	[PVR2_CVAL_AUDIOBITRATE_32]  = 0x1 << 4,
	[PVR2_CVAL_AUDIOBITRATE_VBR] = 0x0 << 4,
};


/* Firmware mailbox flags - definitions found from ivtv */
#define IVTV_MBOX_FIRMWARE_DONE 0x00000004
#define IVTV_MBOX_DRIVER_DONE 0x00000002
#define IVTV_MBOX_DRIVER_BUSY 0x00000001


static int pvr2_encoder_write_words(struct pvr2_hdw *hdw,
				    const u32 *data, unsigned int dlen)
{
	unsigned int idx;
	int ret;
	unsigned int offs = 0;
	unsigned int chunkCnt;

	/*

	Format: First byte must be 0x01.  Remaining 32 bit words are
	spread out into chunks of 7 bytes each, little-endian ordered,
	offset at zero within each 2 blank bytes following and a
	single byte that is 0x44 plus the offset of the word.  Repeat
	request for additional words, with offset adjusted
	accordingly.

	*/
	while (dlen) {
		chunkCnt = 8;
		if (chunkCnt > dlen) chunkCnt = dlen;
		memset(hdw->cmd_buffer,0,sizeof(hdw->cmd_buffer));
		hdw->cmd_buffer[0] = 0x01;
		for (idx = 0; idx < chunkCnt; idx++) {
			hdw->cmd_buffer[1+(idx*7)+6] = 0x44 + idx + offs;
			PVR2_DECOMPOSE_LE(hdw->cmd_buffer, 1+(idx*7),
					  data[idx]);
		}
		ret = pvr2_send_request(hdw,
					hdw->cmd_buffer,1+(chunkCnt*7),
					0,0);
		if (ret) return ret;
		data += chunkCnt;
		dlen -= chunkCnt;
		offs += chunkCnt;
	}

	return 0;
}


static int pvr2_encoder_read_words(struct pvr2_hdw *hdw,int statusFl,
				   u32 *data, unsigned int dlen)
{
	unsigned int idx;
	int ret;
	unsigned int offs = 0;
	unsigned int chunkCnt;

	/*

	Format: First byte must be 0x02 (status check) or 0x28 (read
	back block of 32 bit words).  Next 6 bytes must be zero,
	followed by a single byte of 0x44+offset for portion to be
	read.  Returned data is packed set of 32 bits words that were
	read.

	*/

	while (dlen) {
		chunkCnt = 16;
		if (chunkCnt > dlen) chunkCnt = dlen;
		memset(hdw->cmd_buffer,0,sizeof(hdw->cmd_buffer));
		hdw->cmd_buffer[0] = statusFl ? 0x02 : 0x28;
		hdw->cmd_buffer[7] = 0x44 + offs;
		ret = pvr2_send_request(hdw,
					hdw->cmd_buffer,8,
					hdw->cmd_buffer,chunkCnt * 4);
		if (ret) return ret;

		for (idx = 0; idx < chunkCnt; idx++) {
			data[idx] = PVR2_COMPOSE_LE(hdw->cmd_buffer,idx*4);
		}
		data += chunkCnt;
		dlen -= chunkCnt;
		offs += chunkCnt;
	}

	return 0;
}


/* This prototype is set up to be compatible with the
   cx2341x_mbox_func prototype in cx2341x.h, which should be in
   kernels 2.6.18 or later.  We do this so that we can enable
   cx2341x.ko to write to our encoder (by handing it a pointer to this
   function).  For earlier kernels this doesn't really matter. */
static int pvr2_encoder_cmd(void *ctxt,
			    int cmd,
			    int arg_cnt_send,
			    int arg_cnt_recv,
			    u32 *argp)
{
	unsigned int poll_count;
	int ret = 0;
	unsigned int idx;
	/* These sizes look to be limited by the FX2 firmware implementation */
	u32 wrData[16];
	u32 rdData[16];
	struct pvr2_hdw *hdw = (struct pvr2_hdw *)ctxt;


	/*

	The encoder seems to speak entirely using blocks 32 bit words.
	In ivtv driver terms, this is a mailbox which we populate with
	data and watch what the hardware does with it.  The first word
	is a set of flags used to control the transaction, the second
	word is the command to execute, the third byte is zero (ivtv
	driver suggests that this is some kind of return value), and
	the fourth byte is a specified timeout (windows driver always
	uses 0x00060000 except for one case when it is zero).  All
	successive words are the argument words for the command.

	First, write out the entire set of words, with the first word
	being zero.

	Next, write out just the first word again, but set it to
	IVTV_MBOX_DRIVER_DONE | IVTV_DRIVER_BUSY this time (which
	probably means "go").

	Next, read back 16 words as status.  Check the first word,
	which should have IVTV_MBOX_FIRMWARE_DONE set.  If however
	that bit is not set, then the command isn't done so repeat the
	read.

	Next, read back 32 words and compare with the original
	arugments.  Hopefully they will match.

	Finally, write out just the first word again, but set it to
	0x0 this time (which probably means "idle").

	*/

	if (arg_cnt_send > (sizeof(wrData)/sizeof(wrData[0]))-4) {
		pvr2_trace(
			PVR2_TRACE_ERROR_LEGS,
			"Failed to write cx23416 command"
			" - too many input arguments"
			" (was given %u limit %u)",
			arg_cnt_send,
			(sizeof(wrData)/sizeof(wrData[0])) - 4);
		return -EINVAL;
	}

	if (arg_cnt_recv > (sizeof(rdData)/sizeof(rdData[0]))-4) {
		pvr2_trace(
			PVR2_TRACE_ERROR_LEGS,
			"Failed to write cx23416 command"
			" - too many return arguments"
			" (was given %u limit %u)",
			arg_cnt_recv,
			(sizeof(rdData)/sizeof(rdData[0])) - 4);
		return -EINVAL;
	}


	LOCK_TAKE(hdw->ctl_lock); do {

		wrData[0] = 0;
		wrData[1] = cmd;
		wrData[2] = 0;
		wrData[3] = 0x00060000;
		for (idx = 0; idx < arg_cnt_send; idx++) {
			wrData[idx+4] = argp[idx];
		}
		for (; idx < (sizeof(wrData)/sizeof(wrData[0]))-4; idx++) {
			wrData[idx+4] = 0;
		}

		ret = pvr2_encoder_write_words(hdw,wrData,idx);
		if (ret) break;
		wrData[0] = IVTV_MBOX_DRIVER_DONE|IVTV_MBOX_DRIVER_BUSY;
		ret = pvr2_encoder_write_words(hdw,wrData,1);
		if (ret) break;
		poll_count = 0;
		while (1) {
			if (poll_count < 10000000) poll_count++;
			ret = pvr2_encoder_read_words(hdw,!0,rdData,1);
			if (ret) break;
			if (rdData[0] & IVTV_MBOX_FIRMWARE_DONE) {
				break;
			}
			if (poll_count == 100) {
				pvr2_trace(
					PVR2_TRACE_ERROR_LEGS,
					"***WARNING*** device's encoder"
					" appears to be stuck"
					" (status=0%08x)",rdData[0]);
				pvr2_trace(
					PVR2_TRACE_ERROR_LEGS,
					"Encoder command: 0x%02x",cmd);
				for (idx = 4; idx < arg_cnt_send; idx++) {
					pvr2_trace(
						PVR2_TRACE_ERROR_LEGS,
						"Encoder arg%d: 0x%08x",
						idx-3,wrData[idx]);
				}
				pvr2_trace(
					PVR2_TRACE_ERROR_LEGS,
					"Giving up waiting."
					"  It is likely that"
					" this is a bad idea...");
				ret = -EBUSY;
				break;
			}
		}
		if (ret) break;
		wrData[0] = 0x7;
		ret = pvr2_encoder_read_words(
			hdw,0,rdData,
			sizeof(rdData)/sizeof(rdData[0]));
		if (ret) break;
		for (idx = 0; idx < arg_cnt_recv; idx++) {
			argp[idx] = rdData[idx+4];
		}

		wrData[0] = 0x0;
		ret = pvr2_encoder_write_words(hdw,wrData,1);
		if (ret) break;

	} while(0); LOCK_GIVE(hdw->ctl_lock);

	return ret;
}


static int pvr2_encoder_vcmd(struct pvr2_hdw *hdw, int cmd,
			     int args, ...)
{
	va_list vl;
	unsigned int idx;
	u32 data[12];

	if (args > sizeof(data)/sizeof(data[0])) {
		pvr2_trace(
			PVR2_TRACE_ERROR_LEGS,
			"Failed to write cx23416 command"
			" - too many arguments"
			" (was given %u limit %u)",
			args,sizeof(data)/sizeof(data[0]));
		return -EINVAL;
	}

	va_start(vl, args);
	for (idx = 0; idx < args; idx++) {
		data[idx] = va_arg(vl, u32);
	}
	va_end(vl);

	return pvr2_encoder_cmd(hdw,cmd,args,0,data);
}


int pvr2_encoder_configure(struct pvr2_hdw *hdw)
{
	int ret = 0, audio, i;
	v4l2_std_id vd_std = hdw->std_mask_cur;
	int height = hdw->res_ver_val;
	int width = hdw->res_hor_val;
	int height_full = !hdw->interlace_val;

	int is_30fps, is_ntsc;

	if (vd_std & V4L2_STD_NTSC) {
		is_ntsc=1;
		is_30fps=1;
	} else if (vd_std & V4L2_STD_PAL_M) {
		is_ntsc=0;
		is_30fps=1;
	} else {
		is_ntsc=0;
		is_30fps=0;
	}

	pvr2_trace(PVR2_TRACE_ENCODER,"pvr2_encoder_configure (native)");

	/* set stream output port.  Some notes here: The ivtv-derived
	   encoder documentation says that this command only gets a
	   single argument.  However the Windows driver for the model
	   29xxx series hardware has been sending 0x01 as a second
	   argument, while the Windows driver for the model 24xxx
	   series hardware has been sending 0x02 as a second argument.
	   Confusing matters further are the observations that 0x01
	   for that second argument simply won't work on the 24xxx
	   hardware, while 0x02 will work on the 29xxx - except that
	   when we use 0x02 then xawtv breaks due to a loss of
	   synchronization with the mpeg packet headers.  While xawtv
	   should be fixed to let it resync better (I did try to
	   contact Gerd about this but he has not answered), it has
	   also been determined that sending 0x00 as this mystery
	   second argument seems to work on both hardware models AND
	   xawtv works again.  So we're going to send 0x00. */
	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_SET_OUTPUT_PORT, 2,
				 0x01, 0x00);

	/* set the Program Index Information. We want I,P,B frames (max 400) */
	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_SET_PGM_INDEX_INFO, 2,
				 0x07, 0x0190);

	/* NOTE : windows driver sends these */
	/* Mike Isely <isely@pobox.com> 7-Mar-2006 The windows driver
	   sends the following commands but if we do the same then
	   many apps are no longer able to read the video stream.
	   Leaving these out seems to do no harm at all, so they're
	   commented out for that reason. */
#ifdef notdef
	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_MISC,4, 5,0,0,0);
	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_MISC,4, 3,1,0,0);
	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_MISC,4, 8,0,0,0);
	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_MISC,4, 4,1,0,0);
	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_MISC,4, 0,3,0,0);
	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_MISC,4,15,0,0,0);
#endif

	/* Strange compared to ivtv data. */
	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_SET_NUM_VSYNC_LINES, 2,
				 0xf0, 0xf0);

	/* setup firmware to notify us about some events (don't know why...) */
	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_SET_EVENT_NOTIFICATION, 4,
				 0, 0, 0x10000000, 0xffffffff);

	/* set fps to 25 or 30 (1 or 0)*/
	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_SET_FRAME_RATE, 1,
				 is_30fps ? 0 : 1);

	/* set encoding resolution */
	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_SET_FRAME_SIZE, 2,
				 (height_full ? height : (height / 2)),
				 width);
	/* set encoding aspect ratio to 4:3 */
	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_SET_ASPECT_RATIO, 1,
				 0x02);

	/* VBI */

	if (hdw->config == pvr2_config_vbi) {
		int lines = 2 * (is_30fps ? 12 : 18);
		int size = (4*((lines*1443+3)/4)) / lines;
		ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_SET_VBI_CONFIG, 7,
					 0xbd05, 1, 4,
					 0x25256262, 0x387f7f7f,
					 lines , size);
//                                     0x25256262, 0x13135454, lines , size);
		/* select vbi lines */
#define line_used(l)  (is_30fps ? (l >= 10 && l <= 21) : (l >= 6 && l <= 23))
		for (i = 2 ; i <= 24 ; i++){
			ret |= pvr2_encoder_vcmd(
				hdw,CX2341X_ENC_SET_VBI_LINE, 5,
				i-1,line_used(i), 0, 0, 0);
			ret |= pvr2_encoder_vcmd(
				hdw,CX2341X_ENC_SET_VBI_LINE, 5,
				(i-1) | (1 << 31),
				line_used(i), 0, 0, 0);
		}
	} else {
		ret |= pvr2_encoder_vcmd(
			hdw,CX2341X_ENC_SET_VBI_LINE, 5,
			0xffffffff,0,0,0,0);
	}

	/* set stream type, depending on resolution. */
	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_SET_STREAM_TYPE, 1,
				 height_full ? 0x0a : 0x0b);
	/* set video bitrate */
	ret |= pvr2_encoder_vcmd(
		hdw, CX2341X_ENC_SET_BIT_RATE, 3,
		(hdw->vbr_val ? 1 : 0),
		hdw->videobitrate_val,
		hdw->videopeak_val / 400);
	/* setup GOP structure (GOP size = 0f or 0c, 3-1 = 2 B-frames) */
	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_SET_GOP_PROPERTIES, 2,
				 is_30fps ?  0x0f : 0x0c, 0x03);

	/* enable 3:2 pulldown */
	ret |= pvr2_encoder_vcmd(hdw,CX2341X_ENC_SET_3_2_PULLDOWN,1,0);

	/* set GOP open/close property (open) */
	ret |= pvr2_encoder_vcmd(hdw,CX2341X_ENC_SET_GOP_CLOSURE,1,0);

	/* set audio stream properties 0x40b9? 0100 0000 1011 1001 */
	audio = (pvr_tbl_audiobitrate[hdw->audiobitrate_val] |
		 pvr_tbl_srate[hdw->srate_val] |
		 hdw->audiolayer_val << 2 |
		 (hdw->audiocrc_val ? 1 << 14 : 0) |
		 pvr_tbl_emphasis[hdw->audioemphasis_val]);

	ret |= pvr2_encoder_vcmd(hdw,CX2341X_ENC_SET_AUDIO_PROPERTIES,1,
				 audio);

	/* set dynamic noise reduction filter to manual, Horiz/Vert */
	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_SET_DNR_FILTER_MODE, 2,
				 0, 0x03);

	/* dynamic noise reduction filter param */
	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_SET_DNR_FILTER_PROPS, 2
				 , 0, 0);

	/* dynamic noise reduction median filter */
	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_SET_CORING_LEVELS, 4,
				 0, 0xff, 0, 0xff);

	/* spacial prefiler parameter */
	ret |= pvr2_encoder_vcmd(hdw,
				 CX2341X_ENC_SET_SPATIAL_FILTER_TYPE, 2,
				 0x01, 0x01);

	/* initialize video input */
	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_INITIALIZE_INPUT, 0);

	if (!ret) {
		hdw->subsys_enabled_mask |= (1<<PVR2_SUBSYS_B_ENC_CFG);
	}

	return ret;
}

int pvr2_encoder_start(struct pvr2_hdw *hdw)
{
	int status;

	/* unmask some interrupts */
	pvr2_write_register(hdw, 0x0048, 0xbfffffff);

	/* change some GPIO data */
	pvr2_hdw_gpio_chg_dir(hdw,0xffffffff,0x00000481);
	pvr2_hdw_gpio_chg_out(hdw,0xffffffff,0x00000000);

	if (hdw->config == pvr2_config_vbi) {
		status = pvr2_encoder_vcmd(hdw,CX2341X_ENC_START_CAPTURE,2,
					   0x01,0x14);
	} else if (hdw->config == pvr2_config_mpeg) {
		status = pvr2_encoder_vcmd(hdw,CX2341X_ENC_START_CAPTURE,2,
					   0,0x13);
	} else {
		status = pvr2_encoder_vcmd(hdw,CX2341X_ENC_START_CAPTURE,2,
					   0,0x13);
	}
	if (!status) {
		hdw->subsys_enabled_mask |= (1<<PVR2_SUBSYS_B_ENC_RUN);
	}
	return status;
}

int pvr2_encoder_stop(struct pvr2_hdw *hdw)
{
	int status;

	/* mask all interrupts */
	pvr2_write_register(hdw, 0x0048, 0xffffffff);

	if (hdw->config == pvr2_config_vbi) {
		status = pvr2_encoder_vcmd(hdw,CX2341X_ENC_STOP_CAPTURE,3,
					   0x01,0x01,0x14);
	} else if (hdw->config == pvr2_config_mpeg) {
		status = pvr2_encoder_vcmd(hdw,CX2341X_ENC_STOP_CAPTURE,3,
					   0x01,0,0x13);
	} else {
		status = pvr2_encoder_vcmd(hdw,CX2341X_ENC_STOP_CAPTURE,3,
					   0x01,0,0x13);
	}

	/* change some GPIO data */
	/* Note: Bit d7 of dir appears to control the LED.  So we shut it
	   off here. */
	pvr2_hdw_gpio_chg_dir(hdw,0xffffffff,0x00000401);
	pvr2_hdw_gpio_chg_out(hdw,0xffffffff,0x00000000);

	if (!status) {
		hdw->subsys_enabled_mask &= ~(1<<PVR2_SUBSYS_B_ENC_RUN);
	}
	return status;
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
