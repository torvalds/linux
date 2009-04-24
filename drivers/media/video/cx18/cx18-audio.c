/*
 *  cx18 audio-related functions
 *
 *  Derived from ivtv-audio.c
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA
 */

#include "cx18-driver.h"
#include "cx18-io.h"
#include "cx18-cards.h"
#include "cx18-audio.h"

#define CX18_AUDIO_ENABLE 0xc72014

/* Selects the audio input and output according to the current
   settings. */
int cx18_audio_set_io(struct cx18 *cx)
{
	const struct cx18_card_audio_input *in;
	u32 val;
	int err;

	/* Determine which input to use */
	if (test_bit(CX18_F_I_RADIO_USER, &cx->i_flags))
		in = &cx->card->radio_input;
	else
		in = &cx->card->audio_inputs[cx->audio_input];

	/* handle muxer chips */
	v4l2_subdev_call(cx->sd_extmux, audio, s_routing,
			in->audio_input, 0, 0);

	err = cx18_call_hw_err(cx, cx->card->hw_audio_ctrl,
			       audio, s_routing, in->audio_input, 0, 0);
	if (err)
		return err;

	/* FIXME - this internal mux should be abstracted to a subdev */
	val = cx18_read_reg(cx, CX18_AUDIO_ENABLE) & ~0x30;
	val |= (in->audio_input > CX18_AV_AUDIO_SERIAL2) ? 0x20 :
					(in->audio_input << 4);
	cx18_write_reg_expect(cx, val | 0xb00, CX18_AUDIO_ENABLE, val, 0x30);
	return 0;
}
