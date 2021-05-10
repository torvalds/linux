// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  cx18 ADEC audio functions
 *
 *  Derived from cx25840-audio.c
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *  Copyright (C) 2008  Andy Walls <awalls@md.metrocast.net>
 */

#include "cx18-driver.h"

static int set_audclk_freq(struct cx18 *cx, u32 freq)
{
	struct cx18_av_state *state = &cx->av_state;

	if (freq != 32000 && freq != 44100 && freq != 48000)
		return -EINVAL;

	/*
	 * The PLL parameters are based on the external crystal frequency that
	 * would ideally be:
	 *
	 * NTSC Color subcarrier freq * 8 =
	 *	4.5 MHz/286 * 455/2 * 8 = 28.63636363... MHz
	 *
	 * The accidents of history and rationale that explain from where this
	 * combination of magic numbers originate can be found in:
	 *
	 * [1] Abrahams, I. C., "Choice of Chrominance Subcarrier Frequency in
	 * the NTSC Standards", Proceedings of the I-R-E, January 1954, pp 79-80
	 *
	 * [2] Abrahams, I. C., "The 'Frequency Interleaving' Principle in the
	 * NTSC Standards", Proceedings of the I-R-E, January 1954, pp 81-83
	 *
	 * As Mike Bradley has rightly pointed out, it's not the exact crystal
	 * frequency that matters, only that all parts of the driver and
	 * firmware are using the same value (close to the ideal value).
	 *
	 * Since I have a strong suspicion that, if the firmware ever assumes a
	 * crystal value at all, it will assume 28.636360 MHz, the crystal
	 * freq used in calculations in this driver will be:
	 *
	 *	xtal_freq = 28.636360 MHz
	 *
	 * an error of less than 0.13 ppm which is way, way better than any off
	 * the shelf crystal will have for accuracy anyway.
	 *
	 * Below I aim to run the PLLs' VCOs near 400 MHz to minimize error.
	 *
	 * Many thanks to Jeff Campbell and Mike Bradley for their extensive
	 * investigation, experimentation, testing, and suggested solutions of
	 * of audio/video sync problems with SVideo and CVBS captures.
	 */

	if (state->aud_input > CX18_AV_AUDIO_SERIAL2) {
		switch (freq) {
		case 32000:
			/*
			 * VID_PLL Integer = 0x0f, VID_PLL Post Divider = 0x04
			 * AUX_PLL Integer = 0x0d, AUX PLL Post Divider = 0x20
			 */
			cx18_av_write4(cx, 0x108, 0x200d040f);

			/* VID_PLL Fraction = 0x2be2fe */
			/* xtal * 0xf.15f17f0/4 = 108 MHz: 432 MHz pre-postdiv*/
			cx18_av_write4(cx, 0x10c, 0x002be2fe);

			/* AUX_PLL Fraction = 0x176740c */
			/* xtal * 0xd.bb3a060/0x20 = 32000 * 384: 393 MHz p-pd*/
			cx18_av_write4(cx, 0x110, 0x0176740c);

			/* src3/4/6_ctl */
			/* 0x1.f77f = (4 * xtal/8*2/455) / 32000 */
			cx18_av_write4(cx, 0x900, 0x0801f77f);
			cx18_av_write4(cx, 0x904, 0x0801f77f);
			cx18_av_write4(cx, 0x90c, 0x0801f77f);

			/* SA_MCLK_SEL=1, SA_MCLK_DIV=0x20 */
			cx18_av_write(cx, 0x127, 0x60);

			/* AUD_COUNT = 0x2fff = 8 samples * 4 * 384 - 1 */
			cx18_av_write4(cx, 0x12c, 0x11202fff);

			/*
			 * EN_AV_LOCK = 0
			 * VID_COUNT = 0x0d2ef8 = 107999.000 * 8 =
			 *  ((8 samples/32,000) * (13,500,000 * 8) * 4 - 1) * 8
			 */
			cx18_av_write4(cx, 0x128, 0xa00d2ef8);
			break;

		case 44100:
			/*
			 * VID_PLL Integer = 0x0f, VID_PLL Post Divider = 0x04
			 * AUX_PLL Integer = 0x0e, AUX PLL Post Divider = 0x18
			 */
			cx18_av_write4(cx, 0x108, 0x180e040f);

			/* VID_PLL Fraction = 0x2be2fe */
			/* xtal * 0xf.15f17f0/4 = 108 MHz: 432 MHz pre-postdiv*/
			cx18_av_write4(cx, 0x10c, 0x002be2fe);

			/* AUX_PLL Fraction = 0x062a1f2 */
			/* xtal * 0xe.3150f90/0x18 = 44100 * 384: 406 MHz p-pd*/
			cx18_av_write4(cx, 0x110, 0x0062a1f2);

			/* src3/4/6_ctl */
			/* 0x1.6d59 = (4 * xtal/8*2/455) / 44100 */
			cx18_av_write4(cx, 0x900, 0x08016d59);
			cx18_av_write4(cx, 0x904, 0x08016d59);
			cx18_av_write4(cx, 0x90c, 0x08016d59);

			/* SA_MCLK_SEL=1, SA_MCLK_DIV=0x18 */
			cx18_av_write(cx, 0x127, 0x58);

			/* AUD_COUNT = 0x92ff = 49 samples * 2 * 384 - 1 */
			cx18_av_write4(cx, 0x12c, 0x112092ff);

			/*
			 * EN_AV_LOCK = 0
			 * VID_COUNT = 0x1d4bf8 = 239999.000 * 8 =
			 *  ((49 samples/44,100) * (13,500,000 * 8) * 2 - 1) * 8
			 */
			cx18_av_write4(cx, 0x128, 0xa01d4bf8);
			break;

		case 48000:
			/*
			 * VID_PLL Integer = 0x0f, VID_PLL Post Divider = 0x04
			 * AUX_PLL Integer = 0x0e, AUX PLL Post Divider = 0x16
			 */
			cx18_av_write4(cx, 0x108, 0x160e040f);

			/* VID_PLL Fraction = 0x2be2fe */
			/* xtal * 0xf.15f17f0/4 = 108 MHz: 432 MHz pre-postdiv*/
			cx18_av_write4(cx, 0x10c, 0x002be2fe);

			/* AUX_PLL Fraction = 0x05227ad */
			/* xtal * 0xe.2913d68/0x16 = 48000 * 384: 406 MHz p-pd*/
			cx18_av_write4(cx, 0x110, 0x005227ad);

			/* src3/4/6_ctl */
			/* 0x1.4faa = (4 * xtal/8*2/455) / 48000 */
			cx18_av_write4(cx, 0x900, 0x08014faa);
			cx18_av_write4(cx, 0x904, 0x08014faa);
			cx18_av_write4(cx, 0x90c, 0x08014faa);

			/* SA_MCLK_SEL=1, SA_MCLK_DIV=0x16 */
			cx18_av_write(cx, 0x127, 0x56);

			/* AUD_COUNT = 0x5fff = 4 samples * 16 * 384 - 1 */
			cx18_av_write4(cx, 0x12c, 0x11205fff);

			/*
			 * EN_AV_LOCK = 0
			 * VID_COUNT = 0x1193f8 = 143999.000 * 8 =
			 *  ((4 samples/48,000) * (13,500,000 * 8) * 16 - 1) * 8
			 */
			cx18_av_write4(cx, 0x128, 0xa01193f8);
			break;
		}
	} else {
		switch (freq) {
		case 32000:
			/*
			 * VID_PLL Integer = 0x0f, VID_PLL Post Divider = 0x04
			 * AUX_PLL Integer = 0x0d, AUX PLL Post Divider = 0x30
			 */
			cx18_av_write4(cx, 0x108, 0x300d040f);

			/* VID_PLL Fraction = 0x2be2fe */
			/* xtal * 0xf.15f17f0/4 = 108 MHz: 432 MHz pre-postdiv*/
			cx18_av_write4(cx, 0x10c, 0x002be2fe);

			/* AUX_PLL Fraction = 0x176740c */
			/* xtal * 0xd.bb3a060/0x30 = 32000 * 256: 393 MHz p-pd*/
			cx18_av_write4(cx, 0x110, 0x0176740c);

			/* src1_ctl */
			/* 0x1.0000 = 32000/32000 */
			cx18_av_write4(cx, 0x8f8, 0x08010000);

			/* src3/4/6_ctl */
			/* 0x2.0000 = 2 * (32000/32000) */
			cx18_av_write4(cx, 0x900, 0x08020000);
			cx18_av_write4(cx, 0x904, 0x08020000);
			cx18_av_write4(cx, 0x90c, 0x08020000);

			/* SA_MCLK_SEL=1, SA_MCLK_DIV=0x30 */
			cx18_av_write(cx, 0x127, 0x70);

			/* AUD_COUNT = 0x1fff = 8 samples * 4 * 256 - 1 */
			cx18_av_write4(cx, 0x12c, 0x11201fff);

			/*
			 * EN_AV_LOCK = 0
			 * VID_COUNT = 0x0d2ef8 = 107999.000 * 8 =
			 *  ((8 samples/32,000) * (13,500,000 * 8) * 4 - 1) * 8
			 */
			cx18_av_write4(cx, 0x128, 0xa00d2ef8);
			break;

		case 44100:
			/*
			 * VID_PLL Integer = 0x0f, VID_PLL Post Divider = 0x04
			 * AUX_PLL Integer = 0x0e, AUX PLL Post Divider = 0x24
			 */
			cx18_av_write4(cx, 0x108, 0x240e040f);

			/* VID_PLL Fraction = 0x2be2fe */
			/* xtal * 0xf.15f17f0/4 = 108 MHz: 432 MHz pre-postdiv*/
			cx18_av_write4(cx, 0x10c, 0x002be2fe);

			/* AUX_PLL Fraction = 0x062a1f2 */
			/* xtal * 0xe.3150f90/0x24 = 44100 * 256: 406 MHz p-pd*/
			cx18_av_write4(cx, 0x110, 0x0062a1f2);

			/* src1_ctl */
			/* 0x1.60cd = 44100/32000 */
			cx18_av_write4(cx, 0x8f8, 0x080160cd);

			/* src3/4/6_ctl */
			/* 0x1.7385 = 2 * (32000/44100) */
			cx18_av_write4(cx, 0x900, 0x08017385);
			cx18_av_write4(cx, 0x904, 0x08017385);
			cx18_av_write4(cx, 0x90c, 0x08017385);

			/* SA_MCLK_SEL=1, SA_MCLK_DIV=0x24 */
			cx18_av_write(cx, 0x127, 0x64);

			/* AUD_COUNT = 0x61ff = 49 samples * 2 * 256 - 1 */
			cx18_av_write4(cx, 0x12c, 0x112061ff);

			/*
			 * EN_AV_LOCK = 0
			 * VID_COUNT = 0x1d4bf8 = 239999.000 * 8 =
			 *  ((49 samples/44,100) * (13,500,000 * 8) * 2 - 1) * 8
			 */
			cx18_av_write4(cx, 0x128, 0xa01d4bf8);
			break;

		case 48000:
			/*
			 * VID_PLL Integer = 0x0f, VID_PLL Post Divider = 0x04
			 * AUX_PLL Integer = 0x0d, AUX PLL Post Divider = 0x20
			 */
			cx18_av_write4(cx, 0x108, 0x200d040f);

			/* VID_PLL Fraction = 0x2be2fe */
			/* xtal * 0xf.15f17f0/4 = 108 MHz: 432 MHz pre-postdiv*/
			cx18_av_write4(cx, 0x10c, 0x002be2fe);

			/* AUX_PLL Fraction = 0x176740c */
			/* xtal * 0xd.bb3a060/0x20 = 48000 * 256: 393 MHz p-pd*/
			cx18_av_write4(cx, 0x110, 0x0176740c);

			/* src1_ctl */
			/* 0x1.8000 = 48000/32000 */
			cx18_av_write4(cx, 0x8f8, 0x08018000);

			/* src3/4/6_ctl */
			/* 0x1.5555 = 2 * (32000/48000) */
			cx18_av_write4(cx, 0x900, 0x08015555);
			cx18_av_write4(cx, 0x904, 0x08015555);
			cx18_av_write4(cx, 0x90c, 0x08015555);

			/* SA_MCLK_SEL=1, SA_MCLK_DIV=0x20 */
			cx18_av_write(cx, 0x127, 0x60);

			/* AUD_COUNT = 0x3fff = 4 samples * 16 * 256 - 1 */
			cx18_av_write4(cx, 0x12c, 0x11203fff);

			/*
			 * EN_AV_LOCK = 0
			 * VID_COUNT = 0x1193f8 = 143999.000 * 8 =
			 *  ((4 samples/48,000) * (13,500,000 * 8) * 16 - 1) * 8
			 */
			cx18_av_write4(cx, 0x128, 0xa01193f8);
			break;
		}
	}

	state->audclk_freq = freq;

	return 0;
}

void cx18_av_audio_set_path(struct cx18 *cx)
{
	struct cx18_av_state *state = &cx->av_state;
	u8 v;

	/* stop microcontroller */
	v = cx18_av_read(cx, 0x803) & ~0x10;
	cx18_av_write_expect(cx, 0x803, v, v, 0x1f);

	/* assert soft reset */
	v = cx18_av_read(cx, 0x810) | 0x01;
	cx18_av_write_expect(cx, 0x810, v, v, 0x0f);

	/* Mute everything to prevent the PFFT! */
	cx18_av_write(cx, 0x8d3, 0x1f);

	if (state->aud_input <= CX18_AV_AUDIO_SERIAL2) {
		/* Set Path1 to Serial Audio Input */
		cx18_av_write4(cx, 0x8d0, 0x01011012);

		/* The microcontroller should not be started for the
		 * non-tuner inputs: autodetection is specific for
		 * TV audio. */
	} else {
		/* Set Path1 to Analog Demod Main Channel */
		cx18_av_write4(cx, 0x8d0, 0x1f063870);
	}

	set_audclk_freq(cx, state->audclk_freq);

	/* deassert soft reset */
	v = cx18_av_read(cx, 0x810) & ~0x01;
	cx18_av_write_expect(cx, 0x810, v, v, 0x0f);

	if (state->aud_input > CX18_AV_AUDIO_SERIAL2) {
		/* When the microcontroller detects the
		 * audio format, it will unmute the lines */
		v = cx18_av_read(cx, 0x803) | 0x10;
		cx18_av_write_expect(cx, 0x803, v, v, 0x1f);
	}
}

static void set_volume(struct cx18 *cx, int volume)
{
	/* First convert the volume to msp3400 values (0-127) */
	int vol = volume >> 9;
	/* now scale it up to cx18_av values
	 * -114dB to -96dB maps to 0
	 * this should be 19, but in my testing that was 4dB too loud */
	if (vol <= 23)
		vol = 0;
	else
		vol -= 23;

	/* PATH1_VOLUME */
	cx18_av_write(cx, 0x8d4, 228 - (vol * 2));
}

static void set_bass(struct cx18 *cx, int bass)
{
	/* PATH1_EQ_BASS_VOL */
	cx18_av_and_or(cx, 0x8d9, ~0x3f, 48 - (bass * 48 / 0xffff));
}

static void set_treble(struct cx18 *cx, int treble)
{
	/* PATH1_EQ_TREBLE_VOL */
	cx18_av_and_or(cx, 0x8db, ~0x3f, 48 - (treble * 48 / 0xffff));
}

static void set_balance(struct cx18 *cx, int balance)
{
	int bal = balance >> 8;
	if (bal > 0x80) {
		/* PATH1_BAL_LEFT */
		cx18_av_and_or(cx, 0x8d5, 0x7f, 0x80);
		/* PATH1_BAL_LEVEL */
		cx18_av_and_or(cx, 0x8d5, ~0x7f, bal & 0x7f);
	} else {
		/* PATH1_BAL_LEFT */
		cx18_av_and_or(cx, 0x8d5, 0x7f, 0x00);
		/* PATH1_BAL_LEVEL */
		cx18_av_and_or(cx, 0x8d5, ~0x7f, 0x80 - bal);
	}
}

static void set_mute(struct cx18 *cx, int mute)
{
	struct cx18_av_state *state = &cx->av_state;
	u8 v;

	if (state->aud_input > CX18_AV_AUDIO_SERIAL2) {
		/* Must turn off microcontroller in order to mute sound.
		 * Not sure if this is the best method, but it does work.
		 * If the microcontroller is running, then it will undo any
		 * changes to the mute register. */
		v = cx18_av_read(cx, 0x803);
		if (mute) {
			/* disable microcontroller */
			v &= ~0x10;
			cx18_av_write_expect(cx, 0x803, v, v, 0x1f);
			cx18_av_write(cx, 0x8d3, 0x1f);
		} else {
			/* enable microcontroller */
			v |= 0x10;
			cx18_av_write_expect(cx, 0x803, v, v, 0x1f);
		}
	} else {
		/* SRC1_MUTE_EN */
		cx18_av_and_or(cx, 0x8d3, ~0x2, mute ? 0x02 : 0x00);
	}
}

int cx18_av_s_clock_freq(struct v4l2_subdev *sd, u32 freq)
{
	struct cx18 *cx = v4l2_get_subdevdata(sd);
	struct cx18_av_state *state = &cx->av_state;
	int retval;
	u8 v;

	if (state->aud_input > CX18_AV_AUDIO_SERIAL2) {
		v = cx18_av_read(cx, 0x803) & ~0x10;
		cx18_av_write_expect(cx, 0x803, v, v, 0x1f);
		cx18_av_write(cx, 0x8d3, 0x1f);
	}
	v = cx18_av_read(cx, 0x810) | 0x1;
	cx18_av_write_expect(cx, 0x810, v, v, 0x0f);

	retval = set_audclk_freq(cx, freq);

	v = cx18_av_read(cx, 0x810) & ~0x1;
	cx18_av_write_expect(cx, 0x810, v, v, 0x0f);
	if (state->aud_input > CX18_AV_AUDIO_SERIAL2) {
		v = cx18_av_read(cx, 0x803) | 0x10;
		cx18_av_write_expect(cx, 0x803, v, v, 0x1f);
	}
	return retval;
}

static int cx18_av_audio_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct cx18 *cx = v4l2_get_subdevdata(sd);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_VOLUME:
		set_volume(cx, ctrl->val);
		break;
	case V4L2_CID_AUDIO_BASS:
		set_bass(cx, ctrl->val);
		break;
	case V4L2_CID_AUDIO_TREBLE:
		set_treble(cx, ctrl->val);
		break;
	case V4L2_CID_AUDIO_BALANCE:
		set_balance(cx, ctrl->val);
		break;
	case V4L2_CID_AUDIO_MUTE:
		set_mute(cx, ctrl->val);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

const struct v4l2_ctrl_ops cx18_av_audio_ctrl_ops = {
	.s_ctrl = cx18_av_audio_s_ctrl,
};
