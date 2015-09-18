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

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/spinlock.h>

#include <media/v4l2-device.h>

#include <sound/core.h>
#include <sound/initval.h>

#include "cobalt-driver.h"
#include "cobalt-alsa.h"
#include "cobalt-alsa-pcm.h"

static void snd_cobalt_card_free(struct snd_cobalt_card *cobsc)
{
	if (cobsc == NULL)
		return;

	cobsc->s->alsa = NULL;

	kfree(cobsc);
}

static void snd_cobalt_card_private_free(struct snd_card *sc)
{
	if (sc == NULL)
		return;
	snd_cobalt_card_free(sc->private_data);
	sc->private_data = NULL;
	sc->private_free = NULL;
}

static int snd_cobalt_card_create(struct cobalt_stream *s,
				       struct snd_card *sc,
				       struct snd_cobalt_card **cobsc)
{
	*cobsc = kzalloc(sizeof(struct snd_cobalt_card), GFP_KERNEL);
	if (*cobsc == NULL)
		return -ENOMEM;

	(*cobsc)->s = s;
	(*cobsc)->sc = sc;

	sc->private_data = *cobsc;
	sc->private_free = snd_cobalt_card_private_free;

	return 0;
}

static int snd_cobalt_card_set_names(struct snd_cobalt_card *cobsc)
{
	struct cobalt_stream *s = cobsc->s;
	struct cobalt *cobalt = s->cobalt;
	struct snd_card *sc = cobsc->sc;

	/* sc->driver is used by alsa-lib's configurator: simple, unique */
	strlcpy(sc->driver, "cobalt", sizeof(sc->driver));

	/* sc->shortname is a symlink in /proc/asound: COBALT-M -> cardN */
	snprintf(sc->shortname,  sizeof(sc->shortname), "cobalt-%d-%d",
		 cobalt->instance, s->video_channel);

	/* sc->longname is read from /proc/asound/cards */
	snprintf(sc->longname, sizeof(sc->longname),
		 "Cobalt %d HDMI %d",
		 cobalt->instance, s->video_channel);

	return 0;
}

int cobalt_alsa_init(struct cobalt_stream *s)
{
	struct cobalt *cobalt = s->cobalt;
	struct snd_card *sc = NULL;
	struct snd_cobalt_card *cobsc;
	int ret;

	/* Numbrs steps from "Writing an ALSA Driver" by Takashi Iwai */

	/* (1) Check and increment the device index */
	/* This is a no-op for us.  We'll use the cobalt->instance */

	/* (2) Create a card instance */
	ret = snd_card_new(&cobalt->pci_dev->dev, SNDRV_DEFAULT_IDX1,
			   SNDRV_DEFAULT_STR1, THIS_MODULE, 0, &sc);
	if (ret) {
		cobalt_err("snd_card_new() failed with err %d\n", ret);
		goto err_exit;
	}

	/* (3) Create a main component */
	ret = snd_cobalt_card_create(s, sc, &cobsc);
	if (ret) {
		cobalt_err("snd_cobalt_card_create() failed with err %d\n",
			   ret);
		goto err_exit_free;
	}

	/* (4) Set the driver ID and name strings */
	snd_cobalt_card_set_names(cobsc);

	ret = snd_cobalt_pcm_create(cobsc);
	if (ret) {
		cobalt_err("snd_cobalt_pcm_create() failed with err %d\n",
			   ret);
		goto err_exit_free;
	}
	/* FIXME - proc files */

	/* (7) Set the driver data and return 0 */
	/* We do this out of normal order for PCI drivers to avoid races */
	s->alsa = cobsc;

	/* (6) Register the card instance */
	ret = snd_card_register(sc);
	if (ret) {
		s->alsa = NULL;
		cobalt_err("snd_card_register() failed with err %d\n", ret);
		goto err_exit_free;
	}

	return 0;

err_exit_free:
	if (sc != NULL)
		snd_card_free(sc);
	kfree(cobsc);
err_exit:
	return ret;
}

void cobalt_alsa_exit(struct cobalt_stream *s)
{
	struct snd_cobalt_card *cobsc = s->alsa;

	if (cobsc)
		snd_card_free(cobsc->sc);
	s->alsa = NULL;
}
