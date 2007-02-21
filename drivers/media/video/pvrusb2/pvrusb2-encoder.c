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
#include "pvrusb2-fx2-cmd.h"



/* Firmware mailbox flags - definitions found from ivtv */
#define IVTV_MBOX_FIRMWARE_DONE 0x00000004
#define IVTV_MBOX_DRIVER_DONE 0x00000002
#define IVTV_MBOX_DRIVER_BUSY 0x00000001

#define MBOX_BASE 0x44


static int pvr2_encoder_write_words(struct pvr2_hdw *hdw,
				    unsigned int offs,
				    const u32 *data, unsigned int dlen)
{
	unsigned int idx,addr;
	unsigned int bAddr;
	int ret;
	unsigned int chunkCnt;

	/*

	Format: First byte must be 0x01.  Remaining 32 bit words are
	spread out into chunks of 7 bytes each, with the first 4 bytes
	being the data word (little endian), and the next 3 bytes
	being the address where that data word is to be written (big
	endian).  Repeat request for additional words, with offset
	adjusted accordingly.

	*/
	while (dlen) {
		chunkCnt = 8;
		if (chunkCnt > dlen) chunkCnt = dlen;
		memset(hdw->cmd_buffer,0,sizeof(hdw->cmd_buffer));
		bAddr = 0;
		hdw->cmd_buffer[bAddr++] = FX2CMD_MEM_WRITE_DWORD;
		for (idx = 0; idx < chunkCnt; idx++) {
			addr = idx + offs;
			hdw->cmd_buffer[bAddr+6] = (addr & 0xffu);
			hdw->cmd_buffer[bAddr+5] = ((addr>>8) & 0xffu);
			hdw->cmd_buffer[bAddr+4] = ((addr>>16) & 0xffu);
			PVR2_DECOMPOSE_LE(hdw->cmd_buffer, bAddr,data[idx]);
			bAddr += 7;
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


static int pvr2_encoder_read_words(struct pvr2_hdw *hdw,
				   unsigned int offs,
				   u32 *data, unsigned int dlen)
{
	unsigned int idx;
	int ret;
	unsigned int chunkCnt;

	/*

	Format: First byte must be 0x02 (status check) or 0x28 (read
	back block of 32 bit words).  Next 6 bytes must be zero,
	followed by a single byte of MBOX_BASE+offset for portion to
	be read.  Returned data is packed set of 32 bits words that
	were read.

	*/

	while (dlen) {
		chunkCnt = 16;
		if (chunkCnt > dlen) chunkCnt = dlen;
		if (chunkCnt < 16) chunkCnt = 1;
		hdw->cmd_buffer[0] =
			((chunkCnt == 1) ?
			 FX2CMD_MEM_READ_DWORD : FX2CMD_MEM_READ_64BYTES);
		hdw->cmd_buffer[1] = 0;
		hdw->cmd_buffer[2] = 0;
		hdw->cmd_buffer[3] = 0;
		hdw->cmd_buffer[4] = 0;
		hdw->cmd_buffer[5] = ((offs>>16) & 0xffu);
		hdw->cmd_buffer[6] = ((offs>>8) & 0xffu);
		hdw->cmd_buffer[7] = (offs & 0xffu);
		ret = pvr2_send_request(hdw,
					hdw->cmd_buffer,8,
					hdw->cmd_buffer,
					(chunkCnt == 1 ? 4 : 16 * 4));
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
	unsigned int try_count = 0;
	int retry_flag;
	int ret = 0;
	unsigned int idx;
	/* These sizes look to be limited by the FX2 firmware implementation */
	u32 wrData[16];
	u32 rdData[16];
	struct pvr2_hdw *hdw = (struct pvr2_hdw *)ctxt;


	/*

	The encoder seems to speak entirely using blocks 32 bit words.
	In ivtv driver terms, this is a mailbox at MBOX_BASE which we
	populate with data and watch what the hardware does with it.
	The first word is a set of flags used to control the
	transaction, the second word is the command to execute, the
	third byte is zero (ivtv driver suggests that this is some
	kind of return value), and the fourth byte is a specified
	timeout (windows driver always uses 0x00060000 except for one
	case when it is zero).  All successive words are the argument
	words for the command.

	First, write out the entire set of words, with the first word
	being zero.

	Next, write out just the first word again, but set it to
	IVTV_MBOX_DRIVER_DONE | IVTV_DRIVER_BUSY this time (which
	probably means "go").

	Next, read back the return count words.  Check the first word,
	which should have IVTV_MBOX_FIRMWARE_DONE set.  If however
	that bit is not set, then the command isn't done so repeat the
	read until it is set.

	Finally, write out just the first word again, but set it to
	0x0 this time (which probably means "idle").

	*/

	if (arg_cnt_send > (ARRAY_SIZE(wrData) - 4)) {
		pvr2_trace(
			PVR2_TRACE_ERROR_LEGS,
			"Failed to write cx23416 command"
			" - too many input arguments"
			" (was given %u limit %lu)",
			arg_cnt_send, (long unsigned) ARRAY_SIZE(wrData) - 4);
		return -EINVAL;
	}

	if (arg_cnt_recv > (ARRAY_SIZE(rdData) - 4)) {
		pvr2_trace(
			PVR2_TRACE_ERROR_LEGS,
			"Failed to write cx23416 command"
			" - too many return arguments"
			" (was given %u limit %lu)",
			arg_cnt_recv, (long unsigned) ARRAY_SIZE(rdData) - 4);
		return -EINVAL;
	}


	LOCK_TAKE(hdw->ctl_lock); do {

		retry_flag = 0;
		try_count++;
		ret = 0;
		wrData[0] = 0;
		wrData[1] = cmd;
		wrData[2] = 0;
		wrData[3] = 0x00060000;
		for (idx = 0; idx < arg_cnt_send; idx++) {
			wrData[idx+4] = argp[idx];
		}
		for (; idx < ARRAY_SIZE(wrData) - 4; idx++) {
			wrData[idx+4] = 0;
		}

		ret = pvr2_encoder_write_words(hdw,MBOX_BASE,wrData,idx);
		if (ret) break;
		wrData[0] = IVTV_MBOX_DRIVER_DONE|IVTV_MBOX_DRIVER_BUSY;
		ret = pvr2_encoder_write_words(hdw,MBOX_BASE,wrData,1);
		if (ret) break;
		poll_count = 0;
		while (1) {
			poll_count++;
			ret = pvr2_encoder_read_words(hdw,MBOX_BASE,rdData,
						      arg_cnt_recv+4);
			if (ret) {
				break;
			}
			if (rdData[0] & IVTV_MBOX_FIRMWARE_DONE) {
				break;
			}
			if (rdData[0] && (poll_count < 1000)) continue;
			if (!rdData[0]) {
				retry_flag = !0;
				pvr2_trace(
					PVR2_TRACE_ERROR_LEGS,
					"Encoder timed out waiting for us"
					"; arranging to retry");
			} else {
				pvr2_trace(
					PVR2_TRACE_ERROR_LEGS,
					"***WARNING*** device's encoder"
					" appears to be stuck"
					" (status=0x%08x)",rdData[0]);
			}
			pvr2_trace(
				PVR2_TRACE_ERROR_LEGS,
				"Encoder command: 0x%02x",cmd);
			for (idx = 4; idx < arg_cnt_send; idx++) {
				pvr2_trace(
					PVR2_TRACE_ERROR_LEGS,
					"Encoder arg%d: 0x%08x",
					idx-3,wrData[idx]);
			}
			ret = -EBUSY;
			break;
		}
		if (retry_flag) {
			if (try_count < 20) continue;
			pvr2_trace(
				PVR2_TRACE_ERROR_LEGS,
				"Too many retries...");
			ret = -EBUSY;
		}
		if (ret) {
			pvr2_trace(
				PVR2_TRACE_ERROR_LEGS,
				"Giving up on command."
				"  It is likely that"
				" this is a bad idea...");
			break;
		}
		wrData[0] = 0x7;
		for (idx = 0; idx < arg_cnt_recv; idx++) {
			argp[idx] = rdData[idx+4];
		}

		wrData[0] = 0x0;
		ret = pvr2_encoder_write_words(hdw,MBOX_BASE,wrData,1);
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

	if (args > ARRAY_SIZE(data)) {
		pvr2_trace(
			PVR2_TRACE_ERROR_LEGS,
			"Failed to write cx23416 command"
			" - too many arguments"
			" (was given %u limit %lu)",
			args, (long unsigned) ARRAY_SIZE(data));
		return -EINVAL;
	}

	va_start(vl, args);
	for (idx = 0; idx < args; idx++) {
		data[idx] = va_arg(vl, u32);
	}
	va_end(vl);

	return pvr2_encoder_cmd(hdw,cmd,args,0,data);
}


/* This implements some extra setup for the encoder that seems to be
   specific to the PVR USB2 hardware. */
int pvr2_encoder_prep_config(struct pvr2_hdw *hdw)
{
	int ret = 0;
	int encMisc3Arg = 0;

#if 0
	/* This inexplicable bit happens in the Hauppage windows
	   driver (for both 24xxx and 29xxx devices).  However I
	   currently see no difference in behavior with or without
	   this stuff.  Leave this here as a note of its existence,
	   but don't use it. */
	LOCK_TAKE(hdw->ctl_lock); do {
		u32 dat[1];
		dat[0] = 0x80000640;
		pvr2_encoder_write_words(hdw,0x01fe,dat,1);
		pvr2_encoder_write_words(hdw,0x023e,dat,1);
	} while(0); LOCK_GIVE(hdw->ctl_lock);
#endif

	/* Mike Isely <isely@pobox.com> 26-Jan-2006 The windows driver
	   sends the following list of ENC_MISC commands (for both
	   24xxx and 29xxx devices).  Meanings are not entirely clear,
	   however without the ENC_MISC(3,1) command then we risk
	   random perpetual video corruption whenever the video input
	   breaks up for a moment (like when switching channels). */


#if 0
	/* This ENC_MISC(5,0) command seems to hurt 29xxx sync
	   performance on channel changes, but is not a problem on
	   24xxx devices. */
	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_MISC,4, 5,0,0,0);
#endif

	/* This ENC_MISC(3,encMisc3Arg) command is critical - without
	   it there will eventually be video corruption.  Also, the
	   29xxx case is strange - the Windows driver is passing 1
	   regardless of device type but if we have 1 for 29xxx device
	   the video turns sluggish.  */
	switch (hdw->hdw_type) {
	case PVR2_HDW_TYPE_24XXX: encMisc3Arg = 1; break;
	case PVR2_HDW_TYPE_29XXX: encMisc3Arg = 0; break;
	default: break;
	}
	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_MISC,4, 3,
				 encMisc3Arg,0,0);

	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_MISC,4, 8,0,0,0);

#if 0
	/* This ENC_MISC(4,1) command is poisonous, so it is commented
	   out.  But I'm leaving it here anyway to document its
	   existence in the Windows driver.  The effect of this
	   command is that apps displaying the stream become sluggish
	   with stuttering video. */
	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_MISC,4, 4,1,0,0);
#endif

	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_MISC,4, 0,3,0,0);
	ret |= pvr2_encoder_vcmd(hdw, CX2341X_ENC_MISC,4,15,0,0,0);

	return ret;
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

	ret |= pvr2_encoder_prep_config(hdw);

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

	pvr2_encoder_vcmd(hdw,CX2341X_ENC_MUTE_VIDEO,1,
			  hdw->input_val == PVR2_CVAL_INPUT_RADIO ? 1 : 0);

	switch (hdw->config) {
	case pvr2_config_vbi:
		status = pvr2_encoder_vcmd(hdw,CX2341X_ENC_START_CAPTURE,2,
					   0x01,0x14);
		break;
	case pvr2_config_mpeg:
		status = pvr2_encoder_vcmd(hdw,CX2341X_ENC_START_CAPTURE,2,
					   0,0x13);
		break;
	default: /* Unhandled cases for now */
		status = pvr2_encoder_vcmd(hdw,CX2341X_ENC_START_CAPTURE,2,
					   0,0x13);
		break;
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

	switch (hdw->config) {
	case pvr2_config_vbi:
		status = pvr2_encoder_vcmd(hdw,CX2341X_ENC_STOP_CAPTURE,3,
					   0x01,0x01,0x14);
		break;
	case pvr2_config_mpeg:
		status = pvr2_encoder_vcmd(hdw,CX2341X_ENC_STOP_CAPTURE,3,
					   0x01,0,0x13);
		break;
	default: /* Unhandled cases for now */
		status = pvr2_encoder_vcmd(hdw,CX2341X_ENC_STOP_CAPTURE,3,
					   0x01,0,0x13);
		break;
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
