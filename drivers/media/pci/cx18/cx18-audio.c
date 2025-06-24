// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  cx18 audio-related functions
 *
 *  Derived from ivtv-audio.c
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@kernel.org>
 */

#include "cx18-driver.h"
#include "cx18-io.h"
#include "cx18-cards.h"
#include "cx18-audio.h"

#define CX18_AUDIO_ENABLE    0xc72014
#define CX18_AI1_MUX_MASK    0x30
#define CX18_AI1_MUX_I2S1    0x00
#define CX18_AI1_MUX_I2S2    0x10
#define CX18_AI1_MUX_843_I2S 0x20

/* Selects the audio input and output according to the current
   settings. */
int cx18_audio_set_io(struct cx18 *cx)
{
	const struct cx18_card_audio_input *in;
	u32 u, v;
	int err;

	/* Determine which input to use */
	if (test_bit(CX18_F_I_RADIO_USER, &cx->i_flags))
		in = &cx->card->radio_input;
	else
		in = &cx->card->audio_inputs[cx->audio_input];

	/* handle muxer chips */
	v4l2_subdev_call(cx->sd_extmux, audio, s_routing,
			 (u32) in->muxer_input, 0, 0);

	err = cx18_call_hw_err(cx, cx->card->hw_audio_ctrl,
			       audio, s_routing, in->audio_input, 0, 0);
	if (err)
		return err;

	/* FIXME - this internal mux should be abstracted to a subdev */
	u = cx18_read_reg(cx, CX18_AUDIO_ENABLE);
	v = u & ~CX18_AI1_MUX_MASK;
	switch (in->audio_input) {
	case CX18_AV_AUDIO_SERIAL1:
		v |= CX18_AI1_MUX_I2S1;
		break;
	case CX18_AV_AUDIO_SERIAL2:
		v |= CX18_AI1_MUX_I2S2;
		break;
	default:
		v |= CX18_AI1_MUX_843_I2S;
		break;
	}
	if (v == u) {
		/* force a toggle of some AI1 MUX control bits */
		u &= ~CX18_AI1_MUX_MASK;
		switch (in->audio_input) {
		case CX18_AV_AUDIO_SERIAL1:
			u |= CX18_AI1_MUX_843_I2S;
			break;
		case CX18_AV_AUDIO_SERIAL2:
			u |= CX18_AI1_MUX_843_I2S;
			break;
		default:
			u |= CX18_AI1_MUX_I2S1;
			break;
		}
		cx18_write_reg_expect(cx, u | 0xb00, CX18_AUDIO_ENABLE,
				      u, CX18_AI1_MUX_MASK);
	}
	cx18_write_reg_expect(cx, v | 0xb00, CX18_AUDIO_ENABLE,
			      v, CX18_AI1_MUX_MASK);
	return 0;
}
