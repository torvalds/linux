/*
 *  ALSA interface to cobalt PCM capture streams
 *
 *  Copyright 2014-2015 Cisco Systems, Inc. and/or its affiliates.
 *  All rights reserved.
 *
 *  This program is free software; you may redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 *  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 *  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

struct snd_card;

struct snd_cobalt_card {
	struct cobalt_stream *s;
	struct snd_card *sc;
	unsigned int capture_transfer_done;
	unsigned int hwptr_done_capture;
	unsigned alsa_record_cnt;
	struct snd_pcm_substream *capture_pcm_substream;

	unsigned int pb_size;
	unsigned int pb_count;
	unsigned int pb_pos;
	unsigned pb_filled;
	bool alsa_pb_channel;
	unsigned alsa_playback_cnt;
	struct snd_pcm_substream *playback_pcm_substream;
};

int cobalt_alsa_init(struct cobalt_stream *s);
void cobalt_alsa_exit(struct cobalt_stream *s);
