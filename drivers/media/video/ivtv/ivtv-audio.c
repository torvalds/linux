/*
    Audio-related ivtv functions.
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@xs4all.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ivtv-driver.h"
#include "ivtv-mailbox.h"
#include "ivtv-i2c.h"
#include "ivtv-gpio.h"
#include "ivtv-cards.h"
#include "ivtv-audio.h"
#include <media/msp3400.h>
#include <linux/videodev.h>

/* Selects the audio input and output according to the current
   settings. */
int ivtv_audio_set_io(struct ivtv *itv)
{
	struct v4l2_routing route;
	u32 audio_input;
	int mux_input;

	/* Determine which input to use */
	if (test_bit(IVTV_F_I_RADIO_USER, &itv->i_flags)) {
		audio_input = itv->card->radio_input.audio_input;
		mux_input = itv->card->radio_input.muxer_input;
	} else {
		audio_input = itv->card->audio_inputs[itv->audio_input].audio_input;
		mux_input = itv->card->audio_inputs[itv->audio_input].muxer_input;
	}

	/* handle muxer chips */
	route.input = mux_input;
	route.output = 0;
	ivtv_i2c_hw(itv, itv->card->hw_muxer, VIDIOC_INT_S_AUDIO_ROUTING, &route);

	route.input = audio_input;
	if (itv->card->hw_audio & IVTV_HW_MSP34XX) {
		route.output = MSP_OUTPUT(MSP_SC_IN_DSP_SCART1);
	}
	return ivtv_i2c_hw(itv, itv->card->hw_audio, VIDIOC_INT_S_AUDIO_ROUTING, &route);
}

void ivtv_audio_set_route(struct ivtv *itv, struct v4l2_routing *route)
{
	ivtv_i2c_hw(itv, itv->card->hw_audio, VIDIOC_INT_S_AUDIO_ROUTING, route);
}

void ivtv_audio_set_audio_clock_freq(struct ivtv *itv, u8 freq)
{
	static u32 freqs[3] = { 44100, 48000, 32000 };

	/* The audio clock of the digitizer must match the codec sample
	   rate otherwise you get some very strange effects. */
	if (freq > 2)
		return;
	ivtv_call_i2c_clients(itv, VIDIOC_INT_AUDIO_CLOCK_FREQ, &freqs[freq]);
}

