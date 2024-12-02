/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  ALSA interface to ivtv PCM capture streams
 *
 *  Copyright (C) 2009,2012  Andy Walls <awalls@md.metrocast.net>
 *  Copyright (C) 2009  Devin Heitmueller <dheitmueller@kernellabs.com>
 */

struct snd_card;

struct snd_ivtv_card {
	struct v4l2_device *v4l2_dev;
	struct snd_card *sc;
	unsigned int capture_transfer_done;
	unsigned int hwptr_done_capture;
	struct snd_pcm_substream *capture_pcm_substream;
	spinlock_t slock;
};

extern int ivtv_alsa_debug;

/*
 * File operations that manipulate the encoder or video or audio subdevices
 * need to be serialized.  Use the same lock we use for v4l2 file ops.
 */
static inline void snd_ivtv_lock(struct snd_ivtv_card *itvsc)
{
	struct ivtv *itv = to_ivtv(itvsc->v4l2_dev);
	mutex_lock(&itv->serialize_lock);
}

static inline void snd_ivtv_unlock(struct snd_ivtv_card *itvsc)
{
	struct ivtv *itv = to_ivtv(itvsc->v4l2_dev);
	mutex_unlock(&itv->serialize_lock);
}

#define IVTV_ALSA_DBGFLG_WARN  (1 << 0)
#define IVTV_ALSA_DBGFLG_INFO  (1 << 1)

#define IVTV_ALSA_DEBUG(x, type, fmt, args...) \
	do { \
		if ((x) & ivtv_alsa_debug) \
			pr_info("%s-alsa: " type ": " fmt, \
				v4l2_dev->name , ## args); \
	} while (0)

#define IVTV_ALSA_DEBUG_WARN(fmt, args...) \
	IVTV_ALSA_DEBUG(IVTV_ALSA_DBGFLG_WARN, "warning", fmt , ## args)

#define IVTV_ALSA_DEBUG_INFO(fmt, args...) \
	IVTV_ALSA_DEBUG(IVTV_ALSA_DBGFLG_INFO, "info", fmt , ## args)

#define IVTV_ALSA_ERR(fmt, args...) \
	pr_err("%s-alsa: " fmt, v4l2_dev->name , ## args)

#define IVTV_ALSA_WARN(fmt, args...) \
	pr_warn("%s-alsa: " fmt, v4l2_dev->name , ## args)

#define IVTV_ALSA_INFO(fmt, args...) \
	pr_info("%s-alsa: " fmt, v4l2_dev->name , ## args)
