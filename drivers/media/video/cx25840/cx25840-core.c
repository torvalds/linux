/* cx25840 - Conexant CX25840 audio/video decoder driver
 *
 * Copyright (C) 2004 Ulf Eklund
 *
 * Based on the saa7115 driver and on the first verison of Chris Kennedy's
 * cx25840 driver.
 *
 * Changes by Tyler Trafford <tatrafford@comcast.net>
 *    - cleanup/rewrite for V4L2 API (2005)
 *
 * VBI support by Hans Verkuil <hverkuil@xs4all.nl>.
 *
 * NTSC sliced VBI support by Christopher Neufeld <television@cneufeld.ca>
 * with additional fixes by Hans Verkuil <hverkuil@xs4all.nl>.
 *
 * CX23885 support by Steven Toth <stoth@hauppauge.com>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-i2c-drv-legacy.h>
#include <media/cx25840.h>

#include "cx25840-core.h"

MODULE_DESCRIPTION("Conexant CX25840 audio/video decoder driver");
MODULE_AUTHOR("Ulf Eklund, Chris Kennedy, Hans Verkuil, Tyler Trafford");
MODULE_LICENSE("GPL");

static unsigned short normal_i2c[] = { 0x88 >> 1, I2C_CLIENT_END };


int cx25840_debug;

module_param_named(debug,cx25840_debug, int, 0644);

MODULE_PARM_DESC(debug, "Debugging messages [0=Off (default) 1=On]");

I2C_CLIENT_INSMOD;

/* ----------------------------------------------------------------------- */

int cx25840_write(struct i2c_client *client, u16 addr, u8 value)
{
	u8 buffer[3];
	buffer[0] = addr >> 8;
	buffer[1] = addr & 0xff;
	buffer[2] = value;
	return i2c_master_send(client, buffer, 3);
}

int cx25840_write4(struct i2c_client *client, u16 addr, u32 value)
{
	u8 buffer[6];
	buffer[0] = addr >> 8;
	buffer[1] = addr & 0xff;
	buffer[2] = value & 0xff;
	buffer[3] = (value >> 8) & 0xff;
	buffer[4] = (value >> 16) & 0xff;
	buffer[5] = value >> 24;
	return i2c_master_send(client, buffer, 6);
}

u8 cx25840_read(struct i2c_client * client, u16 addr)
{
	u8 buffer[2];
	buffer[0] = addr >> 8;
	buffer[1] = addr & 0xff;

	if (i2c_master_send(client, buffer, 2) < 2)
		return 0;

	if (i2c_master_recv(client, buffer, 1) < 1)
		return 0;

	return buffer[0];
}

u32 cx25840_read4(struct i2c_client * client, u16 addr)
{
	u8 buffer[4];
	buffer[0] = addr >> 8;
	buffer[1] = addr & 0xff;

	if (i2c_master_send(client, buffer, 2) < 2)
		return 0;

	if (i2c_master_recv(client, buffer, 4) < 4)
		return 0;

	return (buffer[3] << 24) | (buffer[2] << 16) |
	    (buffer[1] << 8) | buffer[0];
}

int cx25840_and_or(struct i2c_client *client, u16 addr, unsigned and_mask,
		   u8 or_value)
{
	return cx25840_write(client, addr,
			     (cx25840_read(client, addr) & and_mask) |
			     or_value);
}

/* ----------------------------------------------------------------------- */

static int set_input(struct i2c_client *client, enum cx25840_video_input vid_input,
						enum cx25840_audio_input aud_input);

/* ----------------------------------------------------------------------- */

static void init_dll1(struct i2c_client *client)
{
	/* This is the Hauppauge sequence used to
	 * initialize the Delay Lock Loop 1 (ADC DLL). */
	cx25840_write(client, 0x159, 0x23);
	cx25840_write(client, 0x15a, 0x87);
	cx25840_write(client, 0x15b, 0x06);
	udelay(10);
	cx25840_write(client, 0x159, 0xe1);
	udelay(10);
	cx25840_write(client, 0x15a, 0x86);
	cx25840_write(client, 0x159, 0xe0);
	cx25840_write(client, 0x159, 0xe1);
	cx25840_write(client, 0x15b, 0x10);
}

static void init_dll2(struct i2c_client *client)
{
	/* This is the Hauppauge sequence used to
	 * initialize the Delay Lock Loop 2 (ADC DLL). */
	cx25840_write(client, 0x15d, 0xe3);
	cx25840_write(client, 0x15e, 0x86);
	cx25840_write(client, 0x15f, 0x06);
	udelay(10);
	cx25840_write(client, 0x15d, 0xe1);
	cx25840_write(client, 0x15d, 0xe0);
	cx25840_write(client, 0x15d, 0xe1);
}

static void cx25836_initialize(struct i2c_client *client)
{
	/* reset configuration is described on page 3-77 of the CX25836 datasheet */
	/* 2. */
	cx25840_and_or(client, 0x000, ~0x01, 0x01);
	cx25840_and_or(client, 0x000, ~0x01, 0x00);
	/* 3a. */
	cx25840_and_or(client, 0x15a, ~0x70, 0x00);
	/* 3b. */
	cx25840_and_or(client, 0x15b, ~0x1e, 0x06);
	/* 3c. */
	cx25840_and_or(client, 0x159, ~0x02, 0x02);
	/* 3d. */
	udelay(10);
	/* 3e. */
	cx25840_and_or(client, 0x159, ~0x02, 0x00);
	/* 3f. */
	cx25840_and_or(client, 0x159, ~0xc0, 0xc0);
	/* 3g. */
	cx25840_and_or(client, 0x159, ~0x01, 0x00);
	cx25840_and_or(client, 0x159, ~0x01, 0x01);
	/* 3h. */
	cx25840_and_or(client, 0x15b, ~0x1e, 0x10);
}

static void cx25840_work_handler(struct work_struct *work)
{
	struct cx25840_state *state = container_of(work, struct cx25840_state, fw_work);
	cx25840_loadfw(state->c);
	wake_up(&state->fw_wait);
}

static void cx25840_initialize(struct i2c_client *client)
{
	DEFINE_WAIT(wait);
	struct cx25840_state *state = i2c_get_clientdata(client);
	struct workqueue_struct *q;

	/* datasheet startup in numbered steps, refer to page 3-77 */
	/* 2. */
	cx25840_and_or(client, 0x803, ~0x10, 0x00);
	/* The default of this register should be 4, but I get 0 instead.
	 * Set this register to 4 manually. */
	cx25840_write(client, 0x000, 0x04);
	/* 3. */
	init_dll1(client);
	init_dll2(client);
	cx25840_write(client, 0x136, 0x0a);
	/* 4. */
	cx25840_write(client, 0x13c, 0x01);
	cx25840_write(client, 0x13c, 0x00);
	/* 5. */
	/* Do the firmware load in a work handler to prevent.
	   Otherwise the kernel is blocked waiting for the
	   bit-banging i2c interface to finish uploading the
	   firmware. */
	INIT_WORK(&state->fw_work, cx25840_work_handler);
	init_waitqueue_head(&state->fw_wait);
	q = create_singlethread_workqueue("cx25840_fw");
	prepare_to_wait(&state->fw_wait, &wait, TASK_UNINTERRUPTIBLE);
	queue_work(q, &state->fw_work);
	schedule();
	finish_wait(&state->fw_wait, &wait);
	destroy_workqueue(q);

	/* 6. */
	cx25840_write(client, 0x115, 0x8c);
	cx25840_write(client, 0x116, 0x07);
	cx25840_write(client, 0x118, 0x02);
	/* 7. */
	cx25840_write(client, 0x4a5, 0x80);
	cx25840_write(client, 0x4a5, 0x00);
	cx25840_write(client, 0x402, 0x00);
	/* 8. */
	cx25840_and_or(client, 0x401, ~0x18, 0);
	cx25840_and_or(client, 0x4a2, ~0x10, 0x10);
	/* steps 8c and 8d are done in change_input() */
	/* 10. */
	cx25840_write(client, 0x8d3, 0x1f);
	cx25840_write(client, 0x8e3, 0x03);

	cx25840_vbi_setup(client);

	/* trial and error says these are needed to get audio */
	cx25840_write(client, 0x914, 0xa0);
	cx25840_write(client, 0x918, 0xa0);
	cx25840_write(client, 0x919, 0x01);

	/* stereo prefered */
	cx25840_write(client, 0x809, 0x04);
	/* AC97 shift */
	cx25840_write(client, 0x8cf, 0x0f);

	/* (re)set input */
	set_input(client, state->vid_input, state->aud_input);

	/* start microcontroller */
	cx25840_and_or(client, 0x803, ~0x10, 0x10);
}

static void cx23885_initialize(struct i2c_client *client)
{
	DEFINE_WAIT(wait);
	struct cx25840_state *state = i2c_get_clientdata(client);
	struct workqueue_struct *q;

	/* Internal Reset */
	cx25840_and_or(client, 0x102, ~0x01, 0x01);
	cx25840_and_or(client, 0x102, ~0x01, 0x00);

	/* Stop microcontroller */
	cx25840_and_or(client, 0x803, ~0x10, 0x00);

	/* DIF in reset? */
	cx25840_write(client, 0x398, 0);

	/* Trust the default xtal, no division */
	/* This changes for the cx23888 products */
	cx25840_write(client, 0x2, 0x76);

	/* Bring down the regulator for AUX clk */
	cx25840_write(client, 0x1, 0x40);

	/* Sys PLL frac */
	cx25840_write4(client, 0x11c, 0x01d1744c);

	/* Sys PLL int */
	cx25840_write4(client, 0x118, 0x00000416);

	/* Disable DIF bypass */
	cx25840_write4(client, 0x33c, 0x00000001);

	/* DIF Src phase inc */
	cx25840_write4(client, 0x340, 0x0df7df83);

	/* Vid PLL frac */
	cx25840_write4(client, 0x10c, 0x01b6db7b);

	/* Vid PLL int */
	cx25840_write4(client, 0x108, 0x00000512);

	/* Luma */
	cx25840_write4(client, 0x414, 0x00107d12);

	/* Chroma */
	cx25840_write4(client, 0x420, 0x3d008282);

	/* Aux PLL frac */
	cx25840_write4(client, 0x114, 0x017dbf48);

	/* Aux PLL int */
	cx25840_write4(client, 0x110, 0x000a030e);

	/* ADC2 input select */
	cx25840_write(client, 0x102, 0x10);

	/* VIN1 & VIN5 */
	cx25840_write(client, 0x103, 0x11);

	/* Enable format auto detect */
	cx25840_write(client, 0x400, 0);
	/* Fast subchroma lock */
	/* White crush, Chroma AGC & Chroma Killer enabled */
	cx25840_write(client, 0x401, 0xe8);

	/* Select AFE clock pad output source */
	cx25840_write(client, 0x144, 0x05);

	/* Do the firmware load in a work handler to prevent.
	   Otherwise the kernel is blocked waiting for the
	   bit-banging i2c interface to finish uploading the
	   firmware. */
	INIT_WORK(&state->fw_work, cx25840_work_handler);
	init_waitqueue_head(&state->fw_wait);
	q = create_singlethread_workqueue("cx25840_fw");
	prepare_to_wait(&state->fw_wait, &wait, TASK_UNINTERRUPTIBLE);
	queue_work(q, &state->fw_work);
	schedule();
	finish_wait(&state->fw_wait, &wait);
	destroy_workqueue(q);

	cx25840_vbi_setup(client);

	/* (re)set input */
	set_input(client, state->vid_input, state->aud_input);

	/* start microcontroller */
	cx25840_and_or(client, 0x803, ~0x10, 0x10);
}

/* ----------------------------------------------------------------------- */

static void input_change(struct i2c_client *client)
{
	struct cx25840_state *state = i2c_get_clientdata(client);
	v4l2_std_id std = state->std;

	/* Follow step 8c and 8d of section 3.16 in the cx25840 datasheet */
	if (std & V4L2_STD_SECAM) {
		cx25840_write(client, 0x402, 0);
	}
	else {
		cx25840_write(client, 0x402, 0x04);
		cx25840_write(client, 0x49f, (std & V4L2_STD_NTSC) ? 0x14 : 0x11);
	}
	cx25840_and_or(client, 0x401, ~0x60, 0);
	cx25840_and_or(client, 0x401, ~0x60, 0x60);
	cx25840_and_or(client, 0x810, ~0x01, 1);

	if (state->radio) {
		cx25840_write(client, 0x808, 0xf9);
		cx25840_write(client, 0x80b, 0x00);
	}
	else if (std & V4L2_STD_525_60) {
		/* Certain Hauppauge PVR150 models have a hardware bug
		   that causes audio to drop out. For these models the
		   audio standard must be set explicitly.
		   To be precise: it affects cards with tuner models
		   85, 99 and 112 (model numbers from tveeprom). */
		int hw_fix = state->pvr150_workaround;

		if (std == V4L2_STD_NTSC_M_JP) {
			/* Japan uses EIAJ audio standard */
			cx25840_write(client, 0x808, hw_fix ? 0x2f : 0xf7);
		} else if (std == V4L2_STD_NTSC_M_KR) {
			/* South Korea uses A2 audio standard */
			cx25840_write(client, 0x808, hw_fix ? 0x3f : 0xf8);
		} else {
			/* Others use the BTSC audio standard */
			cx25840_write(client, 0x808, hw_fix ? 0x1f : 0xf6);
		}
		cx25840_write(client, 0x80b, 0x00);
	} else if (std & V4L2_STD_PAL) {
		/* Follow tuner change procedure for PAL */
		cx25840_write(client, 0x808, 0xff);
		cx25840_write(client, 0x80b, 0x10);
	} else if (std & V4L2_STD_SECAM) {
		/* Select autodetect for SECAM */
		cx25840_write(client, 0x808, 0xff);
		cx25840_write(client, 0x80b, 0x10);
	}

	cx25840_and_or(client, 0x810, ~0x01, 0);
}

static int set_input(struct i2c_client *client, enum cx25840_video_input vid_input,
						enum cx25840_audio_input aud_input)
{
	struct cx25840_state *state = i2c_get_clientdata(client);
	u8 is_composite = (vid_input >= CX25840_COMPOSITE1 &&
			   vid_input <= CX25840_COMPOSITE8);
	u8 reg;

	v4l_dbg(1, cx25840_debug, client,
		"decoder set video input %d, audio input %d\n",
		vid_input, aud_input);

	if (vid_input >= CX25840_VIN1_CH1) {
		v4l_dbg(1, cx25840_debug, client, "vid_input 0x%x\n",
			vid_input);
		reg = vid_input & 0xff;
		if ((vid_input & CX25840_SVIDEO_ON) == CX25840_SVIDEO_ON)
			is_composite = 0;
		else
			is_composite = 1;

		v4l_dbg(1, cx25840_debug, client, "mux cfg 0x%x comp=%d\n",
			reg, is_composite);
	} else
	if (is_composite) {
		reg = 0xf0 + (vid_input - CX25840_COMPOSITE1);
	} else {
		int luma = vid_input & 0xf0;
		int chroma = vid_input & 0xf00;

		if ((vid_input & ~0xff0) ||
		    luma < CX25840_SVIDEO_LUMA1 || luma > CX25840_SVIDEO_LUMA4 ||
		    chroma < CX25840_SVIDEO_CHROMA4 || chroma > CX25840_SVIDEO_CHROMA8) {
			v4l_err(client, "0x%04x is not a valid video input!\n",
				vid_input);
			return -EINVAL;
		}
		reg = 0xf0 + ((luma - CX25840_SVIDEO_LUMA1) >> 4);
		if (chroma >= CX25840_SVIDEO_CHROMA7) {
			reg &= 0x3f;
			reg |= (chroma - CX25840_SVIDEO_CHROMA7) >> 2;
		} else {
			reg &= 0xcf;
			reg |= (chroma - CX25840_SVIDEO_CHROMA4) >> 4;
		}
	}

	/* The caller has previously prepared the correct routing
	 * configuration in reg (for the cx23885) so we have no
	 * need to attempt to flip bits for earlier av decoders.
	 */
	if (!state->is_cx23885) {
		switch (aud_input) {
		case CX25840_AUDIO_SERIAL:
			/* do nothing, use serial audio input */
			break;
		case CX25840_AUDIO4: reg &= ~0x30; break;
		case CX25840_AUDIO5: reg &= ~0x30; reg |= 0x10; break;
		case CX25840_AUDIO6: reg &= ~0x30; reg |= 0x20; break;
		case CX25840_AUDIO7: reg &= ~0xc0; break;
		case CX25840_AUDIO8: reg &= ~0xc0; reg |= 0x40; break;

		default:
			v4l_err(client, "0x%04x is not a valid audio input!\n",
				aud_input);
			return -EINVAL;
		}
	}

	cx25840_write(client, 0x103, reg);

	/* Set INPUT_MODE to Composite (0) or S-Video (1) */
	cx25840_and_or(client, 0x401, ~0x6, is_composite ? 0 : 0x02);

	if (!state->is_cx23885) {
		/* Set CH_SEL_ADC2 to 1 if input comes from CH3 */
		cx25840_and_or(client, 0x102, ~0x2, (reg & 0x80) == 0 ? 2 : 0);
		/* Set DUAL_MODE_ADC2 to 1 if input comes from both CH2&CH3 */
		if ((reg & 0xc0) != 0xc0 && (reg & 0x30) != 0x30)
			cx25840_and_or(client, 0x102, ~0x4, 4);
		else
			cx25840_and_or(client, 0x102, ~0x4, 0);
	} else {
		if (is_composite)
			/* ADC2 input select channel 2 */
			cx25840_and_or(client, 0x102, ~0x2, 0);
		else
			/* ADC2 input select channel 3 */
			cx25840_and_or(client, 0x102, ~0x2, 2);
	}

	state->vid_input = vid_input;
	state->aud_input = aud_input;
	if (!state->is_cx25836) {
		cx25840_audio_set_path(client);
		input_change(client);
	}

	if (state->is_cx23885) {
		/* Audio channel 1 src : Parallel 1 */
		cx25840_write(client, 0x124, 0x03);

		/* Select AFE clock pad output source */
		cx25840_write(client, 0x144, 0x05);

		/* I2S_IN_CTL: I2S_IN_SONY_MODE, LEFT SAMPLE on WS=1 */
		cx25840_write(client, 0x914, 0xa0);

		/* I2S_OUT_CTL:
		 * I2S_IN_SONY_MODE, LEFT SAMPLE on WS=1
		 * I2S_OUT_MASTER_MODE = Master
		 */
		cx25840_write(client, 0x918, 0xa0);
		cx25840_write(client, 0x919, 0x01);
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

static int set_v4lstd(struct i2c_client *client)
{
	struct cx25840_state *state = i2c_get_clientdata(client);
	u8 fmt = 0; 	/* zero is autodetect */
	u8 pal_m = 0;

	/* First tests should be against specific std */
	if (state->std == V4L2_STD_NTSC_M_JP) {
		fmt = 0x2;
	} else if (state->std == V4L2_STD_NTSC_443) {
		fmt = 0x3;
	} else if (state->std == V4L2_STD_PAL_M) {
		pal_m = 1;
		fmt = 0x5;
	} else if (state->std == V4L2_STD_PAL_N) {
		fmt = 0x6;
	} else if (state->std == V4L2_STD_PAL_Nc) {
		fmt = 0x7;
	} else if (state->std == V4L2_STD_PAL_60) {
		fmt = 0x8;
	} else {
		/* Then, test against generic ones */
		if (state->std & V4L2_STD_NTSC)
			fmt = 0x1;
		else if (state->std & V4L2_STD_PAL)
			fmt = 0x4;
		else if (state->std & V4L2_STD_SECAM)
			fmt = 0xc;
	}

	v4l_dbg(1, cx25840_debug, client, "changing video std to fmt %i\n",fmt);

	/* Follow step 9 of section 3.16 in the cx25840 datasheet.
	   Without this PAL may display a vertical ghosting effect.
	   This happens for example with the Yuan MPC622. */
	if (fmt >= 4 && fmt < 8) {
		/* Set format to NTSC-M */
		cx25840_and_or(client, 0x400, ~0xf, 1);
		/* Turn off LCOMB */
		cx25840_and_or(client, 0x47b, ~6, 0);
	}
	cx25840_and_or(client, 0x400, ~0xf, fmt);
	cx25840_and_or(client, 0x403, ~0x3, pal_m);
	cx25840_vbi_setup(client);
	if (!state->is_cx25836)
		input_change(client);
	return 0;
}

/* ----------------------------------------------------------------------- */

static int set_v4lctrl(struct i2c_client *client, struct v4l2_control *ctrl)
{
	struct cx25840_state *state = i2c_get_clientdata(client);

	switch (ctrl->id) {
	case CX25840_CID_ENABLE_PVR150_WORKAROUND:
		state->pvr150_workaround = ctrl->value;
		set_input(client, state->vid_input, state->aud_input);
		break;

	case V4L2_CID_BRIGHTNESS:
		if (ctrl->value < 0 || ctrl->value > 255) {
			v4l_err(client, "invalid brightness setting %d\n",
				    ctrl->value);
			return -ERANGE;
		}

		cx25840_write(client, 0x414, ctrl->value - 128);
		break;

	case V4L2_CID_CONTRAST:
		if (ctrl->value < 0 || ctrl->value > 127) {
			v4l_err(client, "invalid contrast setting %d\n",
				    ctrl->value);
			return -ERANGE;
		}

		cx25840_write(client, 0x415, ctrl->value << 1);
		break;

	case V4L2_CID_SATURATION:
		if (ctrl->value < 0 || ctrl->value > 127) {
			v4l_err(client, "invalid saturation setting %d\n",
				    ctrl->value);
			return -ERANGE;
		}

		cx25840_write(client, 0x420, ctrl->value << 1);
		cx25840_write(client, 0x421, ctrl->value << 1);
		break;

	case V4L2_CID_HUE:
		if (ctrl->value < -127 || ctrl->value > 127) {
			v4l_err(client, "invalid hue setting %d\n", ctrl->value);
			return -ERANGE;
		}

		cx25840_write(client, 0x422, ctrl->value);
		break;

	case V4L2_CID_AUDIO_VOLUME:
	case V4L2_CID_AUDIO_BASS:
	case V4L2_CID_AUDIO_TREBLE:
	case V4L2_CID_AUDIO_BALANCE:
	case V4L2_CID_AUDIO_MUTE:
		if (state->is_cx25836)
			return -EINVAL;
		return cx25840_audio(client, VIDIOC_S_CTRL, ctrl);

	default:
		return -EINVAL;
	}

	return 0;
}

static int get_v4lctrl(struct i2c_client *client, struct v4l2_control *ctrl)
{
	struct cx25840_state *state = i2c_get_clientdata(client);

	switch (ctrl->id) {
	case CX25840_CID_ENABLE_PVR150_WORKAROUND:
		ctrl->value = state->pvr150_workaround;
		break;
	case V4L2_CID_BRIGHTNESS:
		ctrl->value = (s8)cx25840_read(client, 0x414) + 128;
		break;
	case V4L2_CID_CONTRAST:
		ctrl->value = cx25840_read(client, 0x415) >> 1;
		break;
	case V4L2_CID_SATURATION:
		ctrl->value = cx25840_read(client, 0x420) >> 1;
		break;
	case V4L2_CID_HUE:
		ctrl->value = (s8)cx25840_read(client, 0x422);
		break;
	case V4L2_CID_AUDIO_VOLUME:
	case V4L2_CID_AUDIO_BASS:
	case V4L2_CID_AUDIO_TREBLE:
	case V4L2_CID_AUDIO_BALANCE:
	case V4L2_CID_AUDIO_MUTE:
		if (state->is_cx25836)
			return -EINVAL;
		return cx25840_audio(client, VIDIOC_G_CTRL, ctrl);
	default:
		return -EINVAL;
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

static int get_v4lfmt(struct i2c_client *client, struct v4l2_format *fmt)
{
	switch (fmt->type) {
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
		return cx25840_vbi(client, VIDIOC_G_FMT, fmt);
	default:
		return -EINVAL;
	}

	return 0;
}

static int set_v4lfmt(struct i2c_client *client, struct v4l2_format *fmt)
{
	struct cx25840_state *state = i2c_get_clientdata(client);
	struct v4l2_pix_format *pix;
	int HSC, VSC, Vsrc, Hsrc, filter, Vlines;
	int is_50Hz = !(state->std & V4L2_STD_525_60);

	switch (fmt->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		pix = &(fmt->fmt.pix);

		Vsrc = (cx25840_read(client, 0x476) & 0x3f) << 4;
		Vsrc |= (cx25840_read(client, 0x475) & 0xf0) >> 4;

		Hsrc = (cx25840_read(client, 0x472) & 0x3f) << 4;
		Hsrc |= (cx25840_read(client, 0x471) & 0xf0) >> 4;

		Vlines = pix->height + (is_50Hz ? 4 : 7);

		if ((pix->width * 16 < Hsrc) || (Hsrc < pix->width) ||
		    (Vlines * 8 < Vsrc) || (Vsrc < Vlines)) {
			v4l_err(client, "%dx%d is not a valid size!\n",
				    pix->width, pix->height);
			return -ERANGE;
		}

		HSC = (Hsrc * (1 << 20)) / pix->width - (1 << 20);
		VSC = (1 << 16) - (Vsrc * (1 << 9) / Vlines - (1 << 9));
		VSC &= 0x1fff;

		if (pix->width >= 385)
			filter = 0;
		else if (pix->width > 192)
			filter = 1;
		else if (pix->width > 96)
			filter = 2;
		else
			filter = 3;

		v4l_dbg(1, cx25840_debug, client, "decoder set size %dx%d -> scale  %ux%u\n",
			    pix->width, pix->height, HSC, VSC);

		/* HSCALE=HSC */
		cx25840_write(client, 0x418, HSC & 0xff);
		cx25840_write(client, 0x419, (HSC >> 8) & 0xff);
		cx25840_write(client, 0x41a, HSC >> 16);
		/* VSCALE=VSC */
		cx25840_write(client, 0x41c, VSC & 0xff);
		cx25840_write(client, 0x41d, VSC >> 8);
		/* VS_INTRLACE=1 VFILT=filter */
		cx25840_write(client, 0x41e, 0x8 | filter);
		break;

	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
		return cx25840_vbi(client, VIDIOC_S_FMT, fmt);

	case V4L2_BUF_TYPE_VBI_CAPTURE:
		return cx25840_vbi(client, VIDIOC_S_FMT, fmt);

	default:
		return -EINVAL;
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

static void log_video_status(struct i2c_client *client)
{
	static const char *const fmt_strs[] = {
		"0x0",
		"NTSC-M", "NTSC-J", "NTSC-4.43",
		"PAL-BDGHI", "PAL-M", "PAL-N", "PAL-Nc", "PAL-60",
		"0x9", "0xA", "0xB",
		"SECAM",
		"0xD", "0xE", "0xF"
	};

	struct cx25840_state *state = i2c_get_clientdata(client);
	u8 vidfmt_sel = cx25840_read(client, 0x400) & 0xf;
	u8 gen_stat1 = cx25840_read(client, 0x40d);
	u8 gen_stat2 = cx25840_read(client, 0x40e);
	int vid_input = state->vid_input;

	v4l_info(client, "Video signal:              %spresent\n",
		    (gen_stat2 & 0x20) ? "" : "not ");
	v4l_info(client, "Detected format:           %s\n",
		    fmt_strs[gen_stat1 & 0xf]);

	v4l_info(client, "Specified standard:        %s\n",
		    vidfmt_sel ? fmt_strs[vidfmt_sel] : "automatic detection");

	if (vid_input >= CX25840_COMPOSITE1 &&
	    vid_input <= CX25840_COMPOSITE8) {
		v4l_info(client, "Specified video input:     Composite %d\n",
			vid_input - CX25840_COMPOSITE1 + 1);
	} else {
		v4l_info(client, "Specified video input:     S-Video (Luma In%d, Chroma In%d)\n",
			(vid_input & 0xf0) >> 4, (vid_input & 0xf00) >> 8);
	}

	v4l_info(client, "Specified audioclock freq: %d Hz\n", state->audclk_freq);
}

/* ----------------------------------------------------------------------- */

static void log_audio_status(struct i2c_client *client)
{
	struct cx25840_state *state = i2c_get_clientdata(client);
	u8 download_ctl = cx25840_read(client, 0x803);
	u8 mod_det_stat0 = cx25840_read(client, 0x804);
	u8 mod_det_stat1 = cx25840_read(client, 0x805);
	u8 audio_config = cx25840_read(client, 0x808);
	u8 pref_mode = cx25840_read(client, 0x809);
	u8 afc0 = cx25840_read(client, 0x80b);
	u8 mute_ctl = cx25840_read(client, 0x8d3);
	int aud_input = state->aud_input;
	char *p;

	switch (mod_det_stat0) {
	case 0x00: p = "mono"; break;
	case 0x01: p = "stereo"; break;
	case 0x02: p = "dual"; break;
	case 0x04: p = "tri"; break;
	case 0x10: p = "mono with SAP"; break;
	case 0x11: p = "stereo with SAP"; break;
	case 0x12: p = "dual with SAP"; break;
	case 0x14: p = "tri with SAP"; break;
	case 0xfe: p = "forced mode"; break;
	default: p = "not defined";
	}
	v4l_info(client, "Detected audio mode:       %s\n", p);

	switch (mod_det_stat1) {
	case 0x00: p = "not defined"; break;
	case 0x01: p = "EIAJ"; break;
	case 0x02: p = "A2-M"; break;
	case 0x03: p = "A2-BG"; break;
	case 0x04: p = "A2-DK1"; break;
	case 0x05: p = "A2-DK2"; break;
	case 0x06: p = "A2-DK3"; break;
	case 0x07: p = "A1 (6.0 MHz FM Mono)"; break;
	case 0x08: p = "AM-L"; break;
	case 0x09: p = "NICAM-BG"; break;
	case 0x0a: p = "NICAM-DK"; break;
	case 0x0b: p = "NICAM-I"; break;
	case 0x0c: p = "NICAM-L"; break;
	case 0x0d: p = "BTSC/EIAJ/A2-M Mono (4.5 MHz FMMono)"; break;
	case 0x0e: p = "IF FM Radio"; break;
	case 0x0f: p = "BTSC"; break;
	case 0x10: p = "high-deviation FM"; break;
	case 0x11: p = "very high-deviation FM"; break;
	case 0xfd: p = "unknown audio standard"; break;
	case 0xfe: p = "forced audio standard"; break;
	case 0xff: p = "no detected audio standard"; break;
	default: p = "not defined";
	}
	v4l_info(client, "Detected audio standard:   %s\n", p);
	v4l_info(client, "Audio muted:               %s\n",
		    (state->unmute_volume >= 0) ? "yes" : "no");
	v4l_info(client, "Audio microcontroller:     %s\n",
		    (download_ctl & 0x10) ?
				((mute_ctl & 0x2) ? "detecting" : "running") : "stopped");

	switch (audio_config >> 4) {
	case 0x00: p = "undefined"; break;
	case 0x01: p = "BTSC"; break;
	case 0x02: p = "EIAJ"; break;
	case 0x03: p = "A2-M"; break;
	case 0x04: p = "A2-BG"; break;
	case 0x05: p = "A2-DK1"; break;
	case 0x06: p = "A2-DK2"; break;
	case 0x07: p = "A2-DK3"; break;
	case 0x08: p = "A1 (6.0 MHz FM Mono)"; break;
	case 0x09: p = "AM-L"; break;
	case 0x0a: p = "NICAM-BG"; break;
	case 0x0b: p = "NICAM-DK"; break;
	case 0x0c: p = "NICAM-I"; break;
	case 0x0d: p = "NICAM-L"; break;
	case 0x0e: p = "FM radio"; break;
	case 0x0f: p = "automatic detection"; break;
	default: p = "undefined";
	}
	v4l_info(client, "Configured audio standard: %s\n", p);

	if ((audio_config >> 4) < 0xF) {
		switch (audio_config & 0xF) {
		case 0x00: p = "MONO1 (LANGUAGE A/Mono L+R channel for BTSC, EIAJ, A2)"; break;
		case 0x01: p = "MONO2 (LANGUAGE B)"; break;
		case 0x02: p = "MONO3 (STEREO forced MONO)"; break;
		case 0x03: p = "MONO4 (NICAM ANALOG-Language C/Analog Fallback)"; break;
		case 0x04: p = "STEREO"; break;
		case 0x05: p = "DUAL1 (AB)"; break;
		case 0x06: p = "DUAL2 (AC) (FM)"; break;
		case 0x07: p = "DUAL3 (BC) (FM)"; break;
		case 0x08: p = "DUAL4 (AC) (AM)"; break;
		case 0x09: p = "DUAL5 (BC) (AM)"; break;
		case 0x0a: p = "SAP"; break;
		default: p = "undefined";
		}
		v4l_info(client, "Configured audio mode:     %s\n", p);
	} else {
		switch (audio_config & 0xF) {
		case 0x00: p = "BG"; break;
		case 0x01: p = "DK1"; break;
		case 0x02: p = "DK2"; break;
		case 0x03: p = "DK3"; break;
		case 0x04: p = "I"; break;
		case 0x05: p = "L"; break;
		case 0x06: p = "BTSC"; break;
		case 0x07: p = "EIAJ"; break;
		case 0x08: p = "A2-M"; break;
		case 0x09: p = "FM Radio"; break;
		case 0x0f: p = "automatic standard and mode detection"; break;
		default: p = "undefined";
		}
		v4l_info(client, "Configured audio system:   %s\n", p);
	}

	if (aud_input) {
		v4l_info(client, "Specified audio input:     Tuner (In%d)\n", aud_input);
	} else {
		v4l_info(client, "Specified audio input:     External\n");
	}

	switch (pref_mode & 0xf) {
	case 0: p = "mono/language A"; break;
	case 1: p = "language B"; break;
	case 2: p = "language C"; break;
	case 3: p = "analog fallback"; break;
	case 4: p = "stereo"; break;
	case 5: p = "language AC"; break;
	case 6: p = "language BC"; break;
	case 7: p = "language AB"; break;
	default: p = "undefined";
	}
	v4l_info(client, "Preferred audio mode:      %s\n", p);

	if ((audio_config & 0xf) == 0xf) {
		switch ((afc0 >> 3) & 0x3) {
		case 0: p = "system DK"; break;
		case 1: p = "system L"; break;
		case 2: p = "autodetect"; break;
		default: p = "undefined";
		}
		v4l_info(client, "Selected 65 MHz format:    %s\n", p);

		switch (afc0 & 0x7) {
		case 0: p = "chroma"; break;
		case 1: p = "BTSC"; break;
		case 2: p = "EIAJ"; break;
		case 3: p = "A2-M"; break;
		case 4: p = "autodetect"; break;
		default: p = "undefined";
		}
		v4l_info(client, "Selected 45 MHz format:    %s\n", p);
	}
}

/* ----------------------------------------------------------------------- */

static int cx25840_command(struct i2c_client *client, unsigned int cmd,
			   void *arg)
{
	struct cx25840_state *state = i2c_get_clientdata(client);
	struct v4l2_tuner *vt = arg;
	struct v4l2_routing *route = arg;

	/* ignore these commands */
	switch (cmd) {
		case TUNER_SET_TYPE_ADDR:
			return 0;
	}

	if (!state->is_initialized) {
		v4l_dbg(1, cx25840_debug, client, "cmd %08x triggered fw load\n", cmd);
		/* initialize on first use */
		state->is_initialized = 1;
		if (state->is_cx25836)
			cx25836_initialize(client);
		else if (state->is_cx23885)
			cx23885_initialize(client);
		else
			cx25840_initialize(client);
	}

	switch (cmd) {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	/* ioctls to allow direct access to the
	 * cx25840 registers for testing */
	case VIDIOC_DBG_G_REGISTER:
	case VIDIOC_DBG_S_REGISTER:
	{
		struct v4l2_register *reg = arg;

		if (!v4l2_chip_match_i2c_client(client, reg->match_type, reg->match_chip))
			return -EINVAL;
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (cmd == VIDIOC_DBG_G_REGISTER)
			reg->val = cx25840_read(client, reg->reg & 0x0fff);
		else
			cx25840_write(client, reg->reg & 0x0fff, reg->val & 0xff);
		break;
	}
#endif

	case VIDIOC_INT_DECODE_VBI_LINE:
		return cx25840_vbi(client, cmd, arg);

	case VIDIOC_INT_AUDIO_CLOCK_FREQ:
		return cx25840_audio(client, cmd, arg);

	case VIDIOC_STREAMON:
		v4l_dbg(1, cx25840_debug, client, "enable output\n");
		if (state->is_cx23885) {
			u8 v = (cx25840_read(client, 0x421) | 0x0b);
			cx25840_write(client, 0x421, v);
		} else {
			cx25840_write(client, 0x115,
				state->is_cx25836 ? 0x0c : 0x8c);
			cx25840_write(client, 0x116,
				state->is_cx25836 ? 0x04 : 0x07);
		}
		break;

	case VIDIOC_STREAMOFF:
		v4l_dbg(1, cx25840_debug, client, "disable output\n");
		if (state->is_cx23885) {
			u8 v = cx25840_read(client, 0x421) & ~(0x0b);
			cx25840_write(client, 0x421, v);
		} else {
			cx25840_write(client, 0x115, 0x00);
			cx25840_write(client, 0x116, 0x00);
		}
		break;

	case VIDIOC_LOG_STATUS:
		log_video_status(client);
		if (!state->is_cx25836)
			log_audio_status(client);
		break;

	case VIDIOC_G_CTRL:
		return get_v4lctrl(client, (struct v4l2_control *)arg);

	case VIDIOC_S_CTRL:
		return set_v4lctrl(client, (struct v4l2_control *)arg);

	case VIDIOC_QUERYCTRL:
	{
		struct v4l2_queryctrl *qc = arg;

		switch (qc->id) {
			case V4L2_CID_BRIGHTNESS:
			case V4L2_CID_CONTRAST:
			case V4L2_CID_SATURATION:
			case V4L2_CID_HUE:
				return v4l2_ctrl_query_fill_std(qc);
			default:
				break;
		}
		if (state->is_cx25836)
			return -EINVAL;

		switch (qc->id) {
			case V4L2_CID_AUDIO_VOLUME:
			case V4L2_CID_AUDIO_MUTE:
			case V4L2_CID_AUDIO_BALANCE:
			case V4L2_CID_AUDIO_BASS:
			case V4L2_CID_AUDIO_TREBLE:
				return v4l2_ctrl_query_fill_std(qc);
			default:
				return -EINVAL;
		}
		return -EINVAL;
	}

	case VIDIOC_G_STD:
		*(v4l2_std_id *)arg = state->std;
		break;

	case VIDIOC_S_STD:
		if (state->radio == 0 && state->std == *(v4l2_std_id *)arg)
			return 0;
		state->radio = 0;
		state->std = *(v4l2_std_id *)arg;
		return set_v4lstd(client);

	case AUDC_SET_RADIO:
		state->radio = 1;
		break;

	case VIDIOC_INT_G_VIDEO_ROUTING:
		route->input = state->vid_input;
		route->output = 0;
		break;

	case VIDIOC_INT_S_VIDEO_ROUTING:
		return set_input(client, route->input, state->aud_input);

	case VIDIOC_INT_G_AUDIO_ROUTING:
		if (state->is_cx25836)
			return -EINVAL;
		route->input = state->aud_input;
		route->output = 0;
		break;

	case VIDIOC_INT_S_AUDIO_ROUTING:
		if (state->is_cx25836)
			return -EINVAL;
		return set_input(client, state->vid_input, route->input);

	case VIDIOC_S_FREQUENCY:
		if (!state->is_cx25836) {
			input_change(client);
		}
		break;

	case VIDIOC_G_TUNER:
	{
		u8 vpres = cx25840_read(client, 0x40e) & 0x20;
		u8 mode;
		int val = 0;

		if (state->radio)
			break;

		vt->signal = vpres ? 0xffff : 0x0;
		if (state->is_cx25836)
			break;

		vt->capability |=
		    V4L2_TUNER_CAP_STEREO | V4L2_TUNER_CAP_LANG1 |
		    V4L2_TUNER_CAP_LANG2 | V4L2_TUNER_CAP_SAP;

		mode = cx25840_read(client, 0x804);

		/* get rxsubchans and audmode */
		if ((mode & 0xf) == 1)
			val |= V4L2_TUNER_SUB_STEREO;
		else
			val |= V4L2_TUNER_SUB_MONO;

		if (mode == 2 || mode == 4)
			val = V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_LANG2;

		if (mode & 0x10)
			val |= V4L2_TUNER_SUB_SAP;

		vt->rxsubchans = val;
		vt->audmode = state->audmode;
		break;
	}

	case VIDIOC_S_TUNER:
		if (state->radio || state->is_cx25836)
			break;

		switch (vt->audmode) {
		case V4L2_TUNER_MODE_MONO:
			/* mono      -> mono
			   stereo    -> mono
			   bilingual -> lang1 */
			cx25840_and_or(client, 0x809, ~0xf, 0x00);
			break;
		case V4L2_TUNER_MODE_STEREO:
		case V4L2_TUNER_MODE_LANG1:
			/* mono      -> mono
			   stereo    -> stereo
			   bilingual -> lang1 */
			cx25840_and_or(client, 0x809, ~0xf, 0x04);
			break;
		case V4L2_TUNER_MODE_LANG1_LANG2:
			/* mono      -> mono
			   stereo    -> stereo
			   bilingual -> lang1/lang2 */
			cx25840_and_or(client, 0x809, ~0xf, 0x07);
			break;
		case V4L2_TUNER_MODE_LANG2:
			/* mono      -> mono
			   stereo    -> stereo
			   bilingual -> lang2 */
			cx25840_and_or(client, 0x809, ~0xf, 0x01);
			break;
		default:
			return -EINVAL;
		}
		state->audmode = vt->audmode;
		break;

	case VIDIOC_G_FMT:
		return get_v4lfmt(client, (struct v4l2_format *)arg);

	case VIDIOC_S_FMT:
		return set_v4lfmt(client, (struct v4l2_format *)arg);

	case VIDIOC_INT_RESET:
		if (state->is_cx25836)
			cx25836_initialize(client);
		else if (state->is_cx23885)
			cx23885_initialize(client);
		else
			cx25840_initialize(client);
		break;

	case VIDIOC_G_CHIP_IDENT:
		return v4l2_chip_ident_i2c_client(client, arg, state->id, state->rev);

	default:
		return -EINVAL;
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

static int cx25840_probe(struct i2c_client *client)
{
	struct cx25840_state *state;
	u32 id;
	u16 device_id;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	v4l_dbg(1, cx25840_debug, client, "detecting cx25840 client on address 0x%x\n", client->addr << 1);

	device_id = cx25840_read(client, 0x101) << 8;
	device_id |= cx25840_read(client, 0x100);
	v4l_dbg(1, cx25840_debug, client, "device_id = 0x%04x\n", device_id);

	/* The high byte of the device ID should be
	 * 0x83 for the cx2583x and 0x84 for the cx2584x */
	if ((device_id & 0xff00) == 0x8300) {
		id = V4L2_IDENT_CX25836 + ((device_id >> 4) & 0xf) - 6;
	}
	else if ((device_id & 0xff00) == 0x8400) {
		id = V4L2_IDENT_CX25840 + ((device_id >> 4) & 0xf);
	} else if (device_id == 0x0000) {
		id = V4L2_IDENT_CX25836 + ((device_id >> 4) & 0xf) - 6;
	} else if (device_id == 0x1313) {
		id = V4L2_IDENT_CX25836 + ((device_id >> 4) & 0xf) - 6;
	}
	else {
		v4l_dbg(1, cx25840_debug, client, "cx25840 not found\n");
		return -ENODEV;
	}

	state = kzalloc(sizeof(struct cx25840_state), GFP_KERNEL);
	if (state == NULL) {
		return -ENOMEM;
	}

	/* Note: revision '(device_id & 0x0f) == 2' was never built. The
	   marking skips from 0x1 == 22 to 0x3 == 23. */
	v4l_info(client, "cx25%3x-2%x found @ 0x%x (%s)\n",
		    (device_id & 0xfff0) >> 4,
		    (device_id & 0x0f) < 3 ? (device_id & 0x0f) + 1 : (device_id & 0x0f),
		    client->addr << 1, client->adapter->name);

	i2c_set_clientdata(client, state);
	state->c = client;
	state->is_cx25836 = ((device_id & 0xff00) == 0x8300);
	state->is_cx23885 = (device_id == 0x0000) || (device_id == 0x1313);
	state->vid_input = CX25840_COMPOSITE7;
	state->aud_input = CX25840_AUDIO8;
	state->audclk_freq = 48000;
	state->pvr150_workaround = 0;
	state->audmode = V4L2_TUNER_MODE_LANG1;
	state->unmute_volume = -1;
	state->vbi_line_offset = 8;
	state->id = id;
	state->rev = device_id;

	if (state->is_cx23885) {
		/* Drive GPIO2 direction and values */
		cx25840_write(client, 0x160, 0x1d);
		cx25840_write(client, 0x164, 0x00);
	}

	return 0;
}

static int cx25840_remove(struct i2c_client *client)
{
	kfree(i2c_get_clientdata(client));
	return 0;
}

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "cx25840",
	.driverid = I2C_DRIVERID_CX25840,
	.command = cx25840_command,
	.probe = cx25840_probe,
	.remove = cx25840_remove,
};
