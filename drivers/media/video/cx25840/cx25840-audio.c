/* cx25840 audio functions
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


#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <media/v4l2-common.h>
#include <media/cx25840.h>

#include "cx25840-core.h"

/*
 * Note: The PLL and SRC parameters are based on a reference frequency that
 * would ideally be:
 *
 * NTSC Color subcarrier freq * 8 = 4.5 MHz/286 * 455/2 * 8 = 28.63636363... MHz
 *
 * However, it's not the exact reference frequency that matters, only that the
 * firmware and modules that comprise the driver for a particular board all
 * use the same value (close to the ideal value).
 *
 * Comments below will note which reference frequency is assumed for various
 * parameters.  They will usually be one of
 *
 *	ref_freq = 28.636360 MHz
 *		or
 *	ref_freq = 28.636363 MHz
 */

static int cx25840_set_audclk_freq(struct i2c_client *client, u32 freq)
{
	struct cx25840_state *state = to_state(i2c_get_clientdata(client));

	if (state->aud_input != CX25840_AUDIO_SERIAL) {
		switch (freq) {
		case 32000:
			/*
			 * VID_PLL Integer = 0x0f, VID_PLL Post Divider = 0x04
			 * AUX_PLL Integer = 0x06, AUX PLL Post Divider = 0x10
			 */
			cx25840_write4(client, 0x108, 0x1006040f);

			/*
			 * VID_PLL Fraction (register 0x10c) = 0x2be2fe
			 * 28636360 * 0xf.15f17f0/4 = 108 MHz
			 * 432 MHz pre-postdivide
			 */

			/*
			 * AUX_PLL Fraction = 0x1bb39ee
			 * 28636363 * 0x6.dd9cf70/0x10 = 32000 * 384
			 * 196.6 MHz pre-postdivide
			 * FIXME < 200 MHz is out of specified valid range
			 * FIXME 28636363 ref_freq doesn't match VID PLL ref
			 */
			cx25840_write4(client, 0x110, 0x01bb39ee);

			/*
			 * SA_MCLK_SEL = 1
			 * SA_MCLK_DIV = 0x10 = 384/384 * AUX_PLL post dvivider
			 */
			cx25840_write(client, 0x127, 0x50);

			if (is_cx2583x(state))
				break;

			/* src3/4/6_ctl */
			/* 0x1.f77f = (4 * 28636360/8 * 2/455) / 32000 */
			cx25840_write4(client, 0x900, 0x0801f77f);
			cx25840_write4(client, 0x904, 0x0801f77f);
			cx25840_write4(client, 0x90c, 0x0801f77f);
			break;

		case 44100:
			/*
			 * VID_PLL Integer = 0x0f, VID_PLL Post Divider = 0x04
			 * AUX_PLL Integer = 0x09, AUX PLL Post Divider = 0x10
			 */
			cx25840_write4(client, 0x108, 0x1009040f);

			/*
			 * VID_PLL Fraction (register 0x10c) = 0x2be2fe
			 * 28636360 * 0xf.15f17f0/4 = 108 MHz
			 * 432 MHz pre-postdivide
			 */

			/*
			 * AUX_PLL Fraction = 0x0ec6bd6
			 * 28636363 * 0x9.7635eb0/0x10 = 44100 * 384
			 * 271 MHz pre-postdivide
			 * FIXME 28636363 ref_freq doesn't match VID PLL ref
			 */
			cx25840_write4(client, 0x110, 0x00ec6bd6);

			/*
			 * SA_MCLK_SEL = 1
			 * SA_MCLK_DIV = 0x10 = 384/384 * AUX_PLL post dvivider
			 */
			cx25840_write(client, 0x127, 0x50);

			if (is_cx2583x(state))
				break;

			/* src3/4/6_ctl */
			/* 0x1.6d59 = (4 * 28636360/8 * 2/455) / 44100 */
			cx25840_write4(client, 0x900, 0x08016d59);
			cx25840_write4(client, 0x904, 0x08016d59);
			cx25840_write4(client, 0x90c, 0x08016d59);
			break;

		case 48000:
			/*
			 * VID_PLL Integer = 0x0f, VID_PLL Post Divider = 0x04
			 * AUX_PLL Integer = 0x0a, AUX PLL Post Divider = 0x10
			 */
			cx25840_write4(client, 0x108, 0x100a040f);

			/*
			 * VID_PLL Fraction (register 0x10c) = 0x2be2fe
			 * 28636360 * 0xf.15f17f0/4 = 108 MHz
			 * 432 MHz pre-postdivide
			 */

			/*
			 * AUX_PLL Fraction = 0x098d6e5
			 * 28636363 * 0xa.4c6b728/0x10 = 48000 * 384
			 * 295 MHz pre-postdivide
			 * FIXME 28636363 ref_freq doesn't match VID PLL ref
			 */
			cx25840_write4(client, 0x110, 0x0098d6e5);

			/*
			 * SA_MCLK_SEL = 1
			 * SA_MCLK_DIV = 0x10 = 384/384 * AUX_PLL post dvivider
			 */
			cx25840_write(client, 0x127, 0x50);

			if (is_cx2583x(state))
				break;

			/* src3/4/6_ctl */
			/* 0x1.4faa = (4 * 28636360/8 * 2/455) / 48000 */
			cx25840_write4(client, 0x900, 0x08014faa);
			cx25840_write4(client, 0x904, 0x08014faa);
			cx25840_write4(client, 0x90c, 0x08014faa);
			break;
		}
	} else {
		switch (freq) {
		case 32000:
			/*
			 * VID_PLL Integer = 0x0f, VID_PLL Post Divider = 0x04
			 * AUX_PLL Integer = 0x08, AUX PLL Post Divider = 0x1e
			 */
			cx25840_write4(client, 0x108, 0x1e08040f);

			/*
			 * VID_PLL Fraction (register 0x10c) = 0x2be2fe
			 * 28636360 * 0xf.15f17f0/4 = 108 MHz
			 * 432 MHz pre-postdivide
			 */

			/*
			 * AUX_PLL Fraction = 0x12a0869
			 * 28636363 * 0x8.9504348/0x1e = 32000 * 256
			 * 246 MHz pre-postdivide
			 * FIXME 28636363 ref_freq doesn't match VID PLL ref
			 */
			cx25840_write4(client, 0x110, 0x012a0869);

			/*
			 * SA_MCLK_SEL = 1
			 * SA_MCLK_DIV = 0x14 = 256/384 * AUX_PLL post dvivider
			 */
			cx25840_write(client, 0x127, 0x54);

			if (is_cx2583x(state))
				break;

			/* src1_ctl */
			/* 0x1.0000 = 32000/32000 */
			cx25840_write4(client, 0x8f8, 0x08010000);

			/* src3/4/6_ctl */
			/* 0x2.0000 = 2 * (32000/32000) */
			cx25840_write4(client, 0x900, 0x08020000);
			cx25840_write4(client, 0x904, 0x08020000);
			cx25840_write4(client, 0x90c, 0x08020000);
			break;

		case 44100:
			/*
			 * VID_PLL Integer = 0x0f, VID_PLL Post Divider = 0x04
			 * AUX_PLL Integer = 0x09, AUX PLL Post Divider = 0x18
			 */
			cx25840_write4(client, 0x108, 0x1809040f);

			/*
			 * VID_PLL Fraction (register 0x10c) = 0x2be2fe
			 * 28636360 * 0xf.15f17f0/4 = 108 MHz
			 * 432 MHz pre-postdivide
			 */

			/*
			 * AUX_PLL Fraction = 0x0ec6bd6
			 * 28636363 * 0x9.7635eb0/0x18 = 44100 * 256
			 * 271 MHz pre-postdivide
			 * FIXME 28636363 ref_freq doesn't match VID PLL ref
			 */
			cx25840_write4(client, 0x110, 0x00ec6bd6);

			/*
			 * SA_MCLK_SEL = 1
			 * SA_MCLK_DIV = 0x10 = 256/384 * AUX_PLL post dvivider
			 */
			cx25840_write(client, 0x127, 0x50);

			if (is_cx2583x(state))
				break;

			/* src1_ctl */
			/* 0x1.60cd = 44100/32000 */
			cx25840_write4(client, 0x8f8, 0x080160cd);

			/* src3/4/6_ctl */
			/* 0x1.7385 = 2 * (32000/44100) */
			cx25840_write4(client, 0x900, 0x08017385);
			cx25840_write4(client, 0x904, 0x08017385);
			cx25840_write4(client, 0x90c, 0x08017385);
			break;

		case 48000:
			/*
			 * VID_PLL Integer = 0x0f, VID_PLL Post Divider = 0x04
			 * AUX_PLL Integer = 0x0a, AUX PLL Post Divider = 0x18
			 */
			cx25840_write4(client, 0x108, 0x180a040f);

			/*
			 * VID_PLL Fraction (register 0x10c) = 0x2be2fe
			 * 28636360 * 0xf.15f17f0/4 = 108 MHz
			 * 432 MHz pre-postdivide
			 */

			/*
			 * AUX_PLL Fraction = 0x098d6e5
			 * 28636363 * 0xa.4c6b728/0x18 = 48000 * 256
			 * 295 MHz pre-postdivide
			 * FIXME 28636363 ref_freq doesn't match VID PLL ref
			 */
			cx25840_write4(client, 0x110, 0x0098d6e5);

			/*
			 * SA_MCLK_SEL = 1
			 * SA_MCLK_DIV = 0x10 = 256/384 * AUX_PLL post dvivider
			 */
			cx25840_write(client, 0x127, 0x50);

			if (is_cx2583x(state))
				break;

			/* src1_ctl */
			/* 0x1.8000 = 48000/32000 */
			cx25840_write4(client, 0x8f8, 0x08018000);

			/* src3/4/6_ctl */
			/* 0x1.5555 = 2 * (32000/48000) */
			cx25840_write4(client, 0x900, 0x08015555);
			cx25840_write4(client, 0x904, 0x08015555);
			cx25840_write4(client, 0x90c, 0x08015555);
			break;
		}
	}

	state->audclk_freq = freq;

	return 0;
}

static inline int cx25836_set_audclk_freq(struct i2c_client *client, u32 freq)
{
	return cx25840_set_audclk_freq(client, freq);
}

static int cx23885_set_audclk_freq(struct i2c_client *client, u32 freq)
{
	struct cx25840_state *state = to_state(i2c_get_clientdata(client));

	if (state->aud_input != CX25840_AUDIO_SERIAL) {
		switch (freq) {
		case 32000:
		case 44100:
		case 48000:
			/* We don't have register values
			 * so avoid destroying registers. */
			/* FIXME return -EINVAL; */
			break;
		}
	} else {
		switch (freq) {
		case 32000:
		case 44100:
			/* We don't have register values
			 * so avoid destroying registers. */
			/* FIXME return -EINVAL; */
			break;

		case 48000:
			/* src1_ctl */
			/* 0x1.867c = 48000 / (2 * 28636360/8 * 2/455) */
			cx25840_write4(client, 0x8f8, 0x0801867c);

			/* src3/4/6_ctl */
			/* 0x1.4faa = (4 * 28636360/8 * 2/455) / 48000 */
			cx25840_write4(client, 0x900, 0x08014faa);
			cx25840_write4(client, 0x904, 0x08014faa);
			cx25840_write4(client, 0x90c, 0x08014faa);
			break;
		}
	}

	state->audclk_freq = freq;

	return 0;
}

static int cx231xx_set_audclk_freq(struct i2c_client *client, u32 freq)
{
	struct cx25840_state *state = to_state(i2c_get_clientdata(client));

	if (state->aud_input != CX25840_AUDIO_SERIAL) {
		switch (freq) {
		case 32000:
			/* src3/4/6_ctl */
			/* 0x1.f77f = (4 * 28636360/8 * 2/455) / 32000 */
			cx25840_write4(client, 0x900, 0x0801f77f);
			cx25840_write4(client, 0x904, 0x0801f77f);
			cx25840_write4(client, 0x90c, 0x0801f77f);
			break;

		case 44100:
			/* src3/4/6_ctl */
			/* 0x1.6d59 = (4 * 28636360/8 * 2/455) / 44100 */
			cx25840_write4(client, 0x900, 0x08016d59);
			cx25840_write4(client, 0x904, 0x08016d59);
			cx25840_write4(client, 0x90c, 0x08016d59);
			break;

		case 48000:
			/* src3/4/6_ctl */
			/* 0x1.4faa = (4 * 28636360/8 * 2/455) / 48000 */
			cx25840_write4(client, 0x900, 0x08014faa);
			cx25840_write4(client, 0x904, 0x08014faa);
			cx25840_write4(client, 0x90c, 0x08014faa);
			break;
		}
	} else {
		switch (freq) {
		/* FIXME These cases make different assumptions about audclk */
		case 32000:
			/* src1_ctl */
			/* 0x1.0000 = 32000/32000 */
			cx25840_write4(client, 0x8f8, 0x08010000);

			/* src3/4/6_ctl */
			/* 0x2.0000 = 2 * (32000/32000) */
			cx25840_write4(client, 0x900, 0x08020000);
			cx25840_write4(client, 0x904, 0x08020000);
			cx25840_write4(client, 0x90c, 0x08020000);
			break;

		case 44100:
			/* src1_ctl */
			/* 0x1.60cd = 44100/32000 */
			cx25840_write4(client, 0x8f8, 0x080160cd);

			/* src3/4/6_ctl */
			/* 0x1.7385 = 2 * (32000/44100) */
			cx25840_write4(client, 0x900, 0x08017385);
			cx25840_write4(client, 0x904, 0x08017385);
			cx25840_write4(client, 0x90c, 0x08017385);
			break;

		case 48000:
			/* src1_ctl */
			/* 0x1.867c = 48000 / (2 * 28636360/8 * 2/455) */
			cx25840_write4(client, 0x8f8, 0x0801867c);

			/* src3/4/6_ctl */
			/* 0x1.4faa = (4 * 28636360/8 * 2/455) / 48000 */
			cx25840_write4(client, 0x900, 0x08014faa);
			cx25840_write4(client, 0x904, 0x08014faa);
			cx25840_write4(client, 0x90c, 0x08014faa);
			break;
		}
	}

	state->audclk_freq = freq;

	return 0;
}

static int set_audclk_freq(struct i2c_client *client, u32 freq)
{
	struct cx25840_state *state = to_state(i2c_get_clientdata(client));

	if (freq != 32000 && freq != 44100 && freq != 48000)
		return -EINVAL;

	if (is_cx231xx(state))
		return cx231xx_set_audclk_freq(client, freq);

	if (is_cx2388x(state))
		return cx23885_set_audclk_freq(client, freq);

	if (is_cx2583x(state))
		return cx25836_set_audclk_freq(client, freq);

	return cx25840_set_audclk_freq(client, freq);
}

void cx25840_audio_set_path(struct i2c_client *client)
{
	struct cx25840_state *state = to_state(i2c_get_clientdata(client));

	/* assert soft reset */
	cx25840_and_or(client, 0x810, ~0x1, 0x01);

	/* stop microcontroller */
	cx25840_and_or(client, 0x803, ~0x10, 0);

	/* Mute everything to prevent the PFFT! */
	cx25840_write(client, 0x8d3, 0x1f);

	if (state->aud_input == CX25840_AUDIO_SERIAL) {
		/* Set Path1 to Serial Audio Input */
		cx25840_write4(client, 0x8d0, 0x01011012);

		/* The microcontroller should not be started for the
		 * non-tuner inputs: autodetection is specific for
		 * TV audio. */
	} else {
		/* Set Path1 to Analog Demod Main Channel */
		cx25840_write4(client, 0x8d0, 0x1f063870);
	}

	set_audclk_freq(client, state->audclk_freq);

	if (state->aud_input != CX25840_AUDIO_SERIAL) {
		/* When the microcontroller detects the
		 * audio format, it will unmute the lines */
		cx25840_and_or(client, 0x803, ~0x10, 0x10);
	}

	/* deassert soft reset */
	cx25840_and_or(client, 0x810, ~0x1, 0x00);

	/* Ensure the controller is running when we exit */
	if (is_cx2388x(state) || is_cx231xx(state))
		cx25840_and_or(client, 0x803, ~0x10, 0x10);
}

static int get_volume(struct i2c_client *client)
{
	struct cx25840_state *state = to_state(i2c_get_clientdata(client));
	int vol;

	if (state->unmute_volume >= 0)
		return state->unmute_volume;

	/* Volume runs +18dB to -96dB in 1/2dB steps
	 * change to fit the msp3400 -114dB to +12dB range */

	/* check PATH1_VOLUME */
	vol = 228 - cx25840_read(client, 0x8d4);
	vol = (vol / 2) + 23;
	return vol << 9;
}

static void set_volume(struct i2c_client *client, int volume)
{
	struct cx25840_state *state = to_state(i2c_get_clientdata(client));
	int vol;

	if (state->unmute_volume >= 0) {
		state->unmute_volume = volume;
		return;
	}

	/* Convert the volume to msp3400 values (0-127) */
	vol = volume >> 9;

	/* now scale it up to cx25840 values
	 * -114dB to -96dB maps to 0
	 * this should be 19, but in my testing that was 4dB too loud */
	if (vol <= 23) {
		vol = 0;
	} else {
		vol -= 23;
	}

	/* PATH1_VOLUME */
	cx25840_write(client, 0x8d4, 228 - (vol * 2));
}

static int get_bass(struct i2c_client *client)
{
	/* bass is 49 steps +12dB to -12dB */

	/* check PATH1_EQ_BASS_VOL */
	int bass = cx25840_read(client, 0x8d9) & 0x3f;
	bass = (((48 - bass) * 0xffff) + 47) / 48;
	return bass;
}

static void set_bass(struct i2c_client *client, int bass)
{
	/* PATH1_EQ_BASS_VOL */
	cx25840_and_or(client, 0x8d9, ~0x3f, 48 - (bass * 48 / 0xffff));
}

static int get_treble(struct i2c_client *client)
{
	/* treble is 49 steps +12dB to -12dB */

	/* check PATH1_EQ_TREBLE_VOL */
	int treble = cx25840_read(client, 0x8db) & 0x3f;
	treble = (((48 - treble) * 0xffff) + 47) / 48;
	return treble;
}

static void set_treble(struct i2c_client *client, int treble)
{
	/* PATH1_EQ_TREBLE_VOL */
	cx25840_and_or(client, 0x8db, ~0x3f, 48 - (treble * 48 / 0xffff));
}

static int get_balance(struct i2c_client *client)
{
	/* balance is 7 bit, 0 to -96dB */

	/* check PATH1_BAL_LEVEL */
	int balance = cx25840_read(client, 0x8d5) & 0x7f;
	/* check PATH1_BAL_LEFT */
	if ((cx25840_read(client, 0x8d5) & 0x80) == 0)
		balance = 0x80 - balance;
	else
		balance = 0x80 + balance;
	return balance << 8;
}

static void set_balance(struct i2c_client *client, int balance)
{
	int bal = balance >> 8;
	if (bal > 0x80) {
		/* PATH1_BAL_LEFT */
		cx25840_and_or(client, 0x8d5, 0x7f, 0x80);
		/* PATH1_BAL_LEVEL */
		cx25840_and_or(client, 0x8d5, ~0x7f, bal & 0x7f);
	} else {
		/* PATH1_BAL_LEFT */
		cx25840_and_or(client, 0x8d5, 0x7f, 0x00);
		/* PATH1_BAL_LEVEL */
		cx25840_and_or(client, 0x8d5, ~0x7f, 0x80 - bal);
	}
}

static int get_mute(struct i2c_client *client)
{
	struct cx25840_state *state = to_state(i2c_get_clientdata(client));

	return state->unmute_volume >= 0;
}

static void set_mute(struct i2c_client *client, int mute)
{
	struct cx25840_state *state = to_state(i2c_get_clientdata(client));

	if (mute && state->unmute_volume == -1) {
		int vol = get_volume(client);

		set_volume(client, 0);
		state->unmute_volume = vol;
	}
	else if (!mute && state->unmute_volume != -1) {
		int vol = state->unmute_volume;

		state->unmute_volume = -1;
		set_volume(client, vol);
	}
}

int cx25840_s_clock_freq(struct v4l2_subdev *sd, u32 freq)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct cx25840_state *state = to_state(sd);
	int retval;

	if (!is_cx2583x(state))
		cx25840_and_or(client, 0x810, ~0x1, 1);
	if (state->aud_input != CX25840_AUDIO_SERIAL) {
		cx25840_and_or(client, 0x803, ~0x10, 0);
		cx25840_write(client, 0x8d3, 0x1f);
	}
	retval = set_audclk_freq(client, freq);
	if (state->aud_input != CX25840_AUDIO_SERIAL)
		cx25840_and_or(client, 0x803, ~0x10, 0x10);
	if (!is_cx2583x(state))
		cx25840_and_or(client, 0x810, ~0x1, 0);
	return retval;
}

int cx25840_audio_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_VOLUME:
		ctrl->value = get_volume(client);
		break;
	case V4L2_CID_AUDIO_BASS:
		ctrl->value = get_bass(client);
		break;
	case V4L2_CID_AUDIO_TREBLE:
		ctrl->value = get_treble(client);
		break;
	case V4L2_CID_AUDIO_BALANCE:
		ctrl->value = get_balance(client);
		break;
	case V4L2_CID_AUDIO_MUTE:
		ctrl->value = get_mute(client);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

int cx25840_audio_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_VOLUME:
		set_volume(client, ctrl->value);
		break;
	case V4L2_CID_AUDIO_BASS:
		set_bass(client, ctrl->value);
		break;
	case V4L2_CID_AUDIO_TREBLE:
		set_treble(client, ctrl->value);
		break;
	case V4L2_CID_AUDIO_BALANCE:
		set_balance(client, ctrl->value);
		break;
	case V4L2_CID_AUDIO_MUTE:
		set_mute(client, ctrl->value);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
