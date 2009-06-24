/*
 *  ALSA PCM device for the
 *  ALSA interface to cx18 PCM capture streams
 *
 *  Copyright (C) 2009  Andy Walls <awalls@radix.net>
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

#include <linux/init.h>
#include <linux/kernel.h>

#include <media/v4l2-device.h>

#include <sound/core.h>
#include <sound/pcm.h>

#include "cx18-driver.h"
#include "cx18-alsa.h"

static int snd_cx18_pcm_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_cx18_card *cxsc = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct v4l2_device *v4l2_dev = cxsc->v4l2_dev;
	struct snd_card *sc = cxsc->sc;
	struct cx18 *cx = to_cx18(v4l2_dev);
	return 0;
}

static int snd_cx18_pcm_capture_close(struct snd_pcm_substream *substream)
{
	return 0;
}

static int snd_cx18_pcm_ioctl(struct snd_pcm_substream *substream,
		     unsigned int cmd, void *arg)
{
	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static int snd_cx18_pcm_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params)
{
	return 0;
}

static int snd_cx18_pcm_hw_free(struct snd_pcm_substream *substream)
{
	return 0;
}

static int snd_cx18_pcm_prepare(struct snd_pcm_substream *substream)
{
	return 0;
}

static int snd_cx18_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	return 0;
}

static
snd_pcm_uframes_t snd_cx18_pcm_pointer(struct snd_pcm_substream *substream)
{
	return 0;
}

static struct snd_pcm_ops snd_cx18_pcm_capture_ops = {
	.open		= snd_cx18_pcm_capture_open,
	.close		= snd_cx18_pcm_capture_close,
	.ioctl		= snd_cx18_pcm_ioctl,
	.hw_params	= snd_cx18_pcm_hw_params,
	.hw_free	= snd_cx18_pcm_hw_free,
	.prepare	= snd_cx18_pcm_prepare,
	.trigger	= snd_cx18_pcm_trigger,
	.pointer	= snd_cx18_pcm_pointer,
};

int __init snd_cx18_pcm_create(struct snd_cx18_card *cxsc)
{
	struct snd_pcm *sp;
	struct snd_card *sc = cxsc->sc;
	struct v4l2_device *v4l2_dev = cxsc->v4l2_dev;
	struct cx18 *cx = to_cx18(v4l2_dev);
	int ret;

	ret = snd_pcm_new(sc, "CX23418 PCM",
			  0, /* PCM device 0, the only one for this card */
			  0, /* 0 playback substreams */
			  1, /* 1 capture substream */
			  &sp);
	if (ret) {
		CX18_ALSA_ERR("%s: snd_cx18_pcm_create() failed with err %d\n",
			      __func__, ret);
		goto err_exit;
	}
	return 0;

err_exit:
	return ret;
}
