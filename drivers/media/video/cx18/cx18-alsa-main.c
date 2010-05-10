/*
 *  ALSA interface to cx18 PCM capture streams
 *
 *  Copyright (C) 2009  Andy Walls <awalls@radix.net>
 *  Copyright (C) 2009  Devin Heitmueller <dheitmueller@kernellabs.com>
 *
 *  Portions of this work were sponsored by ONELAN Limited.
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
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/spinlock.h>

#include <media/v4l2-device.h>

#include <sound/core.h>
#include <sound/initval.h>

#include "cx18-driver.h"
#include "cx18-version.h"
#include "cx18-alsa.h"
#include "cx18-alsa-mixer.h"
#include "cx18-alsa-pcm.h"

int cx18_alsa_debug;

#define CX18_DEBUG_ALSA_INFO(fmt, arg...) \
	do { \
		if (cx18_alsa_debug & 2) \
			printk(KERN_INFO "%s: " fmt, "cx18-alsa", ## arg); \
	} while (0);

module_param_named(debug, cx18_alsa_debug, int, 0644);
MODULE_PARM_DESC(debug,
		 "Debug level (bitmask). Default: 0\n"
		 "\t\t\t  1/0x0001: warning\n"
		 "\t\t\t  2/0x0002: info\n");

MODULE_AUTHOR("Andy Walls");
MODULE_DESCRIPTION("CX23418 ALSA Interface");
MODULE_SUPPORTED_DEVICE("CX23418 MPEG2 encoder");
MODULE_LICENSE("GPL");

MODULE_VERSION(CX18_VERSION);

static inline
struct snd_cx18_card *to_snd_cx18_card(struct v4l2_device *v4l2_dev)
{
	return to_cx18(v4l2_dev)->alsa;
}

static inline
struct snd_cx18_card *p_to_snd_cx18_card(struct v4l2_device **v4l2_dev)
{
	return container_of(v4l2_dev, struct snd_cx18_card, v4l2_dev);
}

static void snd_cx18_card_free(struct snd_cx18_card *cxsc)
{
	if (cxsc == NULL)
		return;

	if (cxsc->v4l2_dev != NULL)
		to_cx18(cxsc->v4l2_dev)->alsa = NULL;

	/* FIXME - take any other stopping actions needed */

	kfree(cxsc);
}

static void snd_cx18_card_private_free(struct snd_card *sc)
{
	if (sc == NULL)
		return;
	snd_cx18_card_free(sc->private_data);
	sc->private_data = NULL;
	sc->private_free = NULL;
}

static int snd_cx18_card_create(struct v4l2_device *v4l2_dev,
				       struct snd_card *sc,
				       struct snd_cx18_card **cxsc)
{
	*cxsc = kzalloc(sizeof(struct snd_cx18_card), GFP_KERNEL);
	if (*cxsc == NULL)
		return -ENOMEM;

	(*cxsc)->v4l2_dev = v4l2_dev;
	(*cxsc)->sc = sc;

	sc->private_data = *cxsc;
	sc->private_free = snd_cx18_card_private_free;

	return 0;
}

static int snd_cx18_card_set_names(struct snd_cx18_card *cxsc)
{
	struct cx18 *cx = to_cx18(cxsc->v4l2_dev);
	struct snd_card *sc = cxsc->sc;

	/* sc->driver is used by alsa-lib's configurator: simple, unique */
	strlcpy(sc->driver, "CX23418", sizeof(sc->driver));

	/* sc->shortname is a symlink in /proc/asound: CX18-M -> cardN */
	snprintf(sc->shortname,  sizeof(sc->shortname), "CX18-%d",
		 cx->instance);

	/* sc->longname is read from /proc/asound/cards */
	snprintf(sc->longname, sizeof(sc->longname),
		 "CX23418 #%d %s TV/FM Radio/Line-In Capture",
		 cx->instance, cx->card_name);

	return 0;
}

static int snd_cx18_init(struct v4l2_device *v4l2_dev)
{
	struct cx18 *cx = to_cx18(v4l2_dev);
	struct snd_card *sc = NULL;
	struct snd_cx18_card *cxsc;
	int ret;

	/* Numbrs steps from "Writing an ALSA Driver" by Takashi Iwai */

	/* (1) Check and increment the device index */
	/* This is a no-op for us.  We'll use the cx->instance */

	/* (2) Create a card instance */
	ret = snd_card_create(SNDRV_DEFAULT_IDX1, /* use first available id */
			      SNDRV_DEFAULT_STR1, /* xid from end of shortname*/
			      THIS_MODULE, 0, &sc);
	if (ret) {
		CX18_ALSA_ERR("%s: snd_card_create() failed with err %d\n",
			      __func__, ret);
		goto err_exit;
	}

	/* (3) Create a main component */
	ret = snd_cx18_card_create(v4l2_dev, sc, &cxsc);
	if (ret) {
		CX18_ALSA_ERR("%s: snd_cx18_card_create() failed with err %d\n",
			      __func__, ret);
		goto err_exit_free;
	}

	/* (4) Set the driver ID and name strings */
	snd_cx18_card_set_names(cxsc);


	ret = snd_cx18_pcm_create(cxsc);
	if (ret) {
		CX18_ALSA_ERR("%s: snd_cx18_pcm_create() failed with err %d\n",
			      __func__, ret);
		goto err_exit_free;
	}
	/* FIXME - proc files */

	/* (7) Set the driver data and return 0 */
	/* We do this out of normal order for PCI drivers to avoid races */
	cx->alsa = cxsc;

	/* (6) Register the card instance */
	ret = snd_card_register(sc);
	if (ret) {
		cx->alsa = NULL;
		CX18_ALSA_ERR("%s: snd_card_register() failed with err %d\n",
			      __func__, ret);
		goto err_exit_free;
	}

	return 0;

err_exit_free:
	if (sc != NULL)
		snd_card_free(sc);
err_exit:
	return ret;
}

int cx18_alsa_load(struct cx18 *cx)
{
	struct v4l2_device *v4l2_dev = &cx->v4l2_dev;
	struct cx18_stream *s;

	if (v4l2_dev == NULL) {
		printk(KERN_ERR "cx18-alsa: %s: struct v4l2_device * is NULL\n",
		       __func__);
		return 0;
	}

	cx = to_cx18(v4l2_dev);
	if (cx == NULL) {
		printk(KERN_ERR "cx18-alsa cx is NULL\n");
		return 0;
	}

	s = &cx->streams[CX18_ENC_STREAM_TYPE_PCM];
	if (s->video_dev == NULL) {
		CX18_DEBUG_ALSA_INFO("%s: PCM stream for card is disabled - "
				     "skipping\n", __func__);
		return 0;
	}

	if (cx->alsa != NULL) {
		CX18_ALSA_ERR("%s: struct snd_cx18_card * already exists\n",
			      __func__);
		return 0;
	}

	if (snd_cx18_init(v4l2_dev)) {
		CX18_ALSA_ERR("%s: failed to create struct snd_cx18_card\n",
			      __func__);
	} else {
		CX18_DEBUG_ALSA_INFO("%s: created cx18 ALSA interface instance "
				     "\n", __func__);
	}
	return 0;
}

static int __init cx18_alsa_init(void)
{
	printk(KERN_INFO "cx18-alsa: module loading...\n");
	cx18_ext_init = &cx18_alsa_load;
	return 0;
}

static void __exit snd_cx18_exit(struct snd_cx18_card *cxsc)
{
	struct cx18 *cx = to_cx18(cxsc->v4l2_dev);

	/* FIXME - pointer checks & shutdown cxsc */

	snd_card_free(cxsc->sc);
	cx->alsa = NULL;
}

static int __exit cx18_alsa_exit_callback(struct device *dev, void *data)
{
	struct v4l2_device *v4l2_dev = dev_get_drvdata(dev);
	struct snd_cx18_card *cxsc;

	if (v4l2_dev == NULL) {
		printk(KERN_ERR "cx18-alsa: %s: struct v4l2_device * is NULL\n",
		       __func__);
		return 0;
	}

	cxsc = to_snd_cx18_card(v4l2_dev);
	if (cxsc == NULL) {
		CX18_ALSA_WARN("%s: struct snd_cx18_card * is NULL\n",
			       __func__);
		return 0;
	}

	snd_cx18_exit(cxsc);
	return 0;
}

static void __exit cx18_alsa_exit(void)
{
	struct device_driver *drv;
	int ret;

	printk(KERN_INFO "cx18-alsa: module unloading...\n");

	drv = driver_find("cx18", &pci_bus_type);
	ret = driver_for_each_device(drv, NULL, NULL, cx18_alsa_exit_callback);
	put_driver(drv);

	cx18_ext_init = NULL;
	printk(KERN_INFO "cx18-alsa: module unload complete\n");
}

module_init(cx18_alsa_init);
module_exit(cx18_alsa_exit);
