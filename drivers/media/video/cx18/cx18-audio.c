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
#include "cx18-i2c.h"
#include "cx18-cards.h"
#include "cx18-audio.h"

/* Selects the audio input and output according to the current
   settings. */
int cx18_audio_set_io(struct cx18 *cx)
{
	struct v4l2_routing route;
	u32 audio_input;
	int mux_input;

	/* Determine which input to use */
	if (test_bit(CX18_F_I_RADIO_USER, &cx->i_flags)) {
		audio_input = cx->card->radio_input.audio_input;
		mux_input = cx->card->radio_input.muxer_input;
	} else {
		audio_input =
			cx->card->audio_inputs[cx->audio_input].audio_input;
		mux_input =
			cx->card->audio_inputs[cx->audio_input].muxer_input;
	}

	/* handle muxer chips */
	route.input = mux_input;
	route.output = 0;
	cx18_i2c_hw(cx, cx->card->hw_muxer, VIDIOC_INT_S_AUDIO_ROUTING, &route);

	route.input = audio_input;
	return cx18_i2c_hw(cx, cx->card->hw_audio_ctrl,
			VIDIOC_INT_S_AUDIO_ROUTING, &route);
}

void cx18_audio_set_route(struct cx18 *cx, struct v4l2_routing *route)
{
	cx18_i2c_hw(cx, cx->card->hw_audio_ctrl,
			VIDIOC_INT_S_AUDIO_ROUTING, route);
}

void cx18_audio_set_audio_clock_freq(struct cx18 *cx, u8 freq)
{
	static u32 freqs[3] = { 44100, 48000, 32000 };

	/* The audio clock of the digitizer must match the codec sample
	   rate otherwise you get some very strange effects. */
	if (freq > 2)
		return;
	cx18_call_i2c_clients(cx, VIDIOC_INT_AUDIO_CLOCK_FREQ, &freqs[freq]);
}
