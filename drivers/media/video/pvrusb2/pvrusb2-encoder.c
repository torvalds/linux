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
					NULL,0);
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
			(unsigned int)(sizeof(wrData)/sizeof(wrData[0])) - 4);
		return -EINVAL;
	}

	if (arg_cnt_recv > (sizeof(rdData)/sizeof(rdData[0]))-4) {
		pvr2_trace(
			PVR2_TRACE_ERROR_LEGS,
			"Failed to write cx23416 command"
			" - too many return arguments"
			" (was given %u limit %u)",
			arg_cnt_recv,
			(unsigned int)(sizeof(rdData)/sizeof(rdData[0])) - 4);
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
			args,(unsigned int)(sizeof(data)/sizeof(data[0])));
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
	int ret;
	pvr2_trace(PVR2_TRACE_ENCODER,"pvr2_encoder_configure"
		   " (cx2341x module)");
	hdw->enc_ctl_state.port = CX2341X_PORT_STREAMING;
	hdw->enc_ctl_state.width = hdw->res_hor_val;
	hdw->enc_ctl_state.height = hdw->res_ver_val;
	hdw->enc_ctl_state.is_50hz = ((hdw->std_mask_cur &
				       (V4L2_STD_NTSC|V4L2_STD_PAL_M)) ?
				      0 : 1);

	ret = 0;

	if (!ret) ret = pvr2_encoder_vcmd(
		hdw,CX2341X_ENC_SET_NUM_VSYNC_LINES, 2,
		0xf0, 0xf0);

	/* setup firmware to notify us about some events (don't know why...) */
	if (!ret) ret = pvr2_encoder_vcmd(
		hdw,CX2341X_ENC_SET_EVENT_NOTIFICATION, 4,
		0, 0, 0x10000000, 0xffffffff);

	if (!ret) ret = pvr2_encoder_vcmd(
		hdw,CX2341X_ENC_SET_VBI_LINE, 5,
		0xffffffff,0,0,0,0);

	if (ret) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "Failed to configure cx23416");
		return ret;
	}

	ret = cx2341x_update(hdw,pvr2_encoder_cmd,
			     (hdw->enc_cur_valid ? &hdw->enc_cur_state : NULL),
			     &hdw->enc_ctl_state);
	if (ret) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "Error from cx2341x module code=%d",ret);
		return ret;
	}

	ret = 0;

	if (!ret) ret = pvr2_encoder_vcmd(
		hdw, CX2341X_ENC_INITIALIZE_INPUT, 0);

	if (ret) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "Failed to initialize cx23416 video input");
		return ret;
	}

	hdw->subsys_enabled_mask |= (1<<PVR2_SUBSYS_B_ENC_CFG);
	memcpy(&hdw->enc_cur_state,&hdw->enc_ctl_state,
	       sizeof(struct cx2341x_mpeg_params));
	hdw->enc_cur_valid = !0;
	return 0;
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
