/******************************************************************************
*                                                                             *
*  easycap_sound.c                                                            *
*                                                                             *
*  Audio driver for EasyCAP USB2.0 Video Capture Device DC60                  *
*                                                                             *
*                                                                             *
******************************************************************************/
/*
 *
 *  Copyright (C) 2010 R.M. Thomas  <rmthomas@sciolus.org>
 *
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
*/
/*****************************************************************************/

#include "easycap.h"
#include "easycap_sound.h"

#if defined(EASYCAP_NEEDS_ALSA)
/*--------------------------------------------------------------------------*/
/*
 *  PARAMETERS USED WHEN REGISTERING THE AUDIO INTERFACE
 */
/*--------------------------------------------------------------------------*/
static const struct snd_pcm_hardware alsa_hardware = {
	.info = SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP           |
		SNDRV_PCM_INFO_INTERLEAVED    |
		SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000,
	.rate_min = 32000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = PAGE_SIZE * PAGES_PER_AUDIO_FRAGMENT * \
						AUDIO_FRAGMENT_MANY,
	.period_bytes_min = PAGE_SIZE * PAGES_PER_AUDIO_FRAGMENT,
	.period_bytes_max = PAGE_SIZE * PAGES_PER_AUDIO_FRAGMENT * 2,
	.periods_min = AUDIO_FRAGMENT_MANY,
	.periods_max = AUDIO_FRAGMENT_MANY * 2,
};

static struct snd_pcm_ops easycap_alsa_pcm_ops = {
	.open      = easycap_alsa_open,
	.close     = easycap_alsa_close,
	.ioctl     = snd_pcm_lib_ioctl,
	.hw_params = easycap_alsa_hw_params,
	.hw_free   = easycap_alsa_hw_free,
	.prepare   = easycap_alsa_prepare,
	.ack       = easycap_alsa_ack,
	.trigger   = easycap_alsa_trigger,
	.pointer   = easycap_alsa_pointer,
	.page      = easycap_alsa_page,
};

/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  THE FUNCTION snd_card_create() HAS  THIS_MODULE  AS AN ARGUMENT.  THIS
 *  MEANS MODULE easycap.  BEWARE.
*/
/*---------------------------------------------------------------------------*/
int
easycap_alsa_probe(struct easycap *peasycap)
{
int rc;
struct snd_card *psnd_card;
struct snd_pcm *psnd_pcm;

if (NULL == peasycap) {
	SAY("ERROR: peasycap is NULL\n");
	return -ENODEV;
}
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	SAY("ERROR: bad peasycap\n");
	return -EFAULT;
}
if (0 > peasycap->minor) {
	SAY("ERROR: no minor\n");
	return -ENODEV;
}

peasycap->alsa_hardware = alsa_hardware;
if (true == peasycap->microphone) {
	peasycap->alsa_hardware.rates = SNDRV_PCM_RATE_32000;
	peasycap->alsa_hardware.rate_min = 32000;
	peasycap->alsa_hardware.rate_max = 32000;
} else {
	peasycap->alsa_hardware.rates = SNDRV_PCM_RATE_48000;
	peasycap->alsa_hardware.rate_min = 48000;
	peasycap->alsa_hardware.rate_max = 48000;
}

#if defined(EASYCAP_NEEDS_CARD_CREATE)
	if (0 != snd_card_create(SNDRV_DEFAULT_IDX1, "easycap_alsa", \
					THIS_MODULE, 0, \
					&psnd_card)) {
		SAY("ERROR: Cannot do ALSA snd_card_create()\n");
		return -EFAULT;
	}
#else
	psnd_card = snd_card_new(SNDRV_DEFAULT_IDX1, "easycap_alsa", \
							THIS_MODULE, 0);
	if (NULL == psnd_card) {
		SAY("ERROR: Cannot do ALSA snd_card_new()\n");
		return -EFAULT;
	}
#endif /*EASYCAP_NEEDS_CARD_CREATE*/

	sprintf(&psnd_card->id[0], "EasyALSA%i", peasycap->minor);
	strcpy(&psnd_card->driver[0], EASYCAP_DRIVER_DESCRIPTION);
	strcpy(&psnd_card->shortname[0], "easycap_alsa");
	sprintf(&psnd_card->longname[0], "%s", &psnd_card->shortname[0]);

	psnd_card->dev = &peasycap->pusb_device->dev;
	psnd_card->private_data = peasycap;
	peasycap->psnd_card = psnd_card;

	rc = snd_pcm_new(psnd_card, "easycap_pcm", 0, 0, 1, &psnd_pcm);
	if (0 != rc) {
		SAM("ERROR: Cannot do ALSA snd_pcm_new()\n");
		snd_card_free(psnd_card);
		return -EFAULT;
	}

	snd_pcm_set_ops(psnd_pcm, SNDRV_PCM_STREAM_CAPTURE, \
							&easycap_alsa_pcm_ops);
	psnd_pcm->info_flags = 0;
	strcpy(&psnd_pcm->name[0], &psnd_card->id[0]);
	psnd_pcm->private_data = peasycap;
	peasycap->psnd_pcm = psnd_pcm;
	peasycap->psubstream = (struct snd_pcm_substream *)NULL;

	rc = snd_card_register(psnd_card);
	if (0 != rc) {
		SAM("ERROR: Cannot do ALSA snd_card_register()\n");
		snd_card_free(psnd_card);
		return -EFAULT;
	} else {
	;
	SAM("registered %s\n", &psnd_card->id[0]);
	}
return 0;
}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  ON COMPLETION OF AN AUDIO URB ITS DATA IS COPIED TO THE DAM BUFFER
 *  PROVIDED peasycap->audio_idle IS ZERO.  REGARDLESS OF THIS BEING TRUE,
 *  IT IS RESUBMITTED PROVIDED peasycap->audio_isoc_streaming IS NOT ZERO.
 */
/*---------------------------------------------------------------------------*/
void
easycap_alsa_complete(struct urb *purb)
{
struct easycap *peasycap;
struct snd_pcm_substream *pss;
struct snd_pcm_runtime *prt;
int dma_bytes, fragment_bytes;
int isfragment;
__u8 *p1, *p2;
__s16 s16;
int i, j, more, much, rc;
#if defined(UPSAMPLE)
int k;
__s16 oldaudio, newaudio, delta;
#endif /*UPSAMPLE*/

JOT(16, "\n");

if (NULL == purb) {
	SAY("ERROR: purb is NULL\n");
	return;
}
peasycap = purb->context;
if (NULL == peasycap) {
	SAY("ERROR: peasycap is NULL\n");
	return;
}
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	SAY("ERROR: bad peasycap\n");
	return;
}
much = 0;
if (peasycap->audio_idle) {
	JOM(16, "%i=audio_idle  %i=audio_isoc_streaming\n", \
			peasycap->audio_idle, peasycap->audio_isoc_streaming);
	if (peasycap->audio_isoc_streaming)
		goto resubmit;
}
/*---------------------------------------------------------------------------*/
pss = peasycap->psubstream;
if (NULL == pss)
	goto resubmit;
prt = pss->runtime;
if (NULL == prt)
	goto resubmit;
dma_bytes = (int)prt->dma_bytes;
if (0 == dma_bytes)
	goto resubmit;
fragment_bytes = 4 * ((int)prt->period_size);
if (0 == fragment_bytes)
	goto resubmit;
/* -------------------------------------------------------------------------*/
if (purb->status) {
	if ((-ESHUTDOWN == purb->status) || (-ENOENT == purb->status)) {
		JOM(16, "urb status -ESHUTDOWN or -ENOENT\n");
		return;
	}
	SAM("ERROR: non-zero urb status:\n");
	switch (purb->status) {
	case -EINPROGRESS: {
		SAM("-EINPROGRESS\n");
		break;
	}
	case -ENOSR: {
		SAM("-ENOSR\n");
		break;
	}
	case -EPIPE: {
		SAM("-EPIPE\n");
		break;
	}
	case -EOVERFLOW: {
		SAM("-EOVERFLOW\n");
		break;
	}
	case -EPROTO: {
		SAM("-EPROTO\n");
		break;
	}
	case -EILSEQ: {
		SAM("-EILSEQ\n");
		break;
	}
	case -ETIMEDOUT: {
		SAM("-ETIMEDOUT\n");
		break;
	}
	case -EMSGSIZE: {
		SAM("-EMSGSIZE\n");
		break;
	}
	case -EOPNOTSUPP: {
		SAM("-EOPNOTSUPP\n");
		break;
	}
	case -EPFNOSUPPORT: {
		SAM("-EPFNOSUPPORT\n");
		break;
	}
	case -EAFNOSUPPORT: {
		SAM("-EAFNOSUPPORT\n");
		break;
	}
	case -EADDRINUSE: {
		SAM("-EADDRINUSE\n");
		break;
	}
	case -EADDRNOTAVAIL: {
		SAM("-EADDRNOTAVAIL\n");
		break;
	}
	case -ENOBUFS: {
		SAM("-ENOBUFS\n");
		break;
	}
	case -EISCONN: {
		SAM("-EISCONN\n");
		break;
	}
	case -ENOTCONN: {
		SAM("-ENOTCONN\n");
		break;
	}
	case -ESHUTDOWN: {
		SAM("-ESHUTDOWN\n");
		break;
	}
	case -ENOENT: {
		SAM("-ENOENT\n");
		break;
	}
	case -ECONNRESET: {
		SAM("-ECONNRESET\n");
		break;
	}
	case -ENOSPC: {
		SAM("ENOSPC\n");
		break;
	}
	default: {
		SAM("unknown error: %i\n", purb->status);
		break;
	}
	}
	goto resubmit;
}
/*---------------------------------------------------------------------------*/
/*
 *  PROCEED HERE WHEN NO ERROR
 */
/*---------------------------------------------------------------------------*/

#if defined(UPSAMPLE)
oldaudio = peasycap->oldaudio;
#endif /*UPSAMPLE*/

for (i = 0;  i < purb->number_of_packets; i++) {
	switch (purb->iso_frame_desc[i].status) {
	case  0: {
		break;
	}
	case -ENOENT: {
		SAM("-ENOENT\n");
		break;
	}
	case -EINPROGRESS: {
		SAM("-EINPROGRESS\n");
		break;
	}
	case -EPROTO: {
		SAM("-EPROTO\n");
		break;
	}
	case -EILSEQ: {
		SAM("-EILSEQ\n");
		break;
	}
	case -ETIME: {
		SAM("-ETIME\n");
		break;
	}
	case -ETIMEDOUT: {
		SAM("-ETIMEDOUT\n");
		break;
	}
	case -EPIPE: {
		SAM("-EPIPE\n");
		break;
	}
	case -ECOMM: {
		SAM("-ECOMM\n");
		break;
	}
	case -ENOSR: {
		SAM("-ENOSR\n");
		break;
	}
	case -EOVERFLOW: {
		SAM("-EOVERFLOW\n");
		break;
	}
	case -EREMOTEIO: {
		SAM("-EREMOTEIO\n");
		break;
	}
	case -ENODEV: {
		SAM("-ENODEV\n");
		break;
	}
	case -EXDEV: {
		SAM("-EXDEV\n");
		break;
	}
	case -EINVAL: {
		SAM("-EINVAL\n");
		break;
	}
	case -ECONNRESET: {
		SAM("-ECONNRESET\n");
		break;
	}
	case -ENOSPC: {
		SAM("-ENOSPC\n");
		break;
	}
	case -ESHUTDOWN: {
		SAM("-ESHUTDOWN\n");
		break;
	}
	case -EPERM: {
		SAM("-EPERM\n");
		break;
	}
	default: {
		SAM("unknown error: %i\n", purb->iso_frame_desc[i].status);
		break;
	}
	}
	if (!purb->iso_frame_desc[i].status) {
		more = purb->iso_frame_desc[i].actual_length;
		if (!more)
			peasycap->audio_mt++;
		else {
			if (peasycap->audio_mt) {
				JOM(12, "%4i empty audio urb frames\n", \
							peasycap->audio_mt);
				peasycap->audio_mt = 0;
			}

			p1 = (__u8 *)(purb->transfer_buffer + \
					purb->iso_frame_desc[i].offset);

/*---------------------------------------------------------------------------*/
/*
 *  COPY more BYTES FROM ISOC BUFFER TO THE DMA BUFFER,
 *  CONVERTING 8-BIT MONO TO 16-BIT SIGNED LITTLE-ENDIAN SAMPLES IF NECESSARY
 */
/*---------------------------------------------------------------------------*/
			while (more) {
				if (0 > more) {
					SAM("MISTAKE: more is negative\n");
					return;
				}
				much = dma_bytes - peasycap->dma_fill;
				if (0 > much) {
					SAM("MISTAKE: much is negative\n");
					return;
				}
				if (0 == much) {
					peasycap->dma_fill = 0;
					peasycap->dma_next = fragment_bytes;
					JOM(8, "wrapped dma buffer\n");
				}
				if (false == peasycap->microphone) {
					if (much > more)
						much = more;
					memcpy(prt->dma_area + \
						peasycap->dma_fill, \
								p1, much);
					p1 += much;
					more -= much;
				} else {
#if defined(UPSAMPLE)
					if (much % 16)
						JOM(8, "MISTAKE? much" \
						" is not divisible by 16\n");
					if (much > (16 * \
							more))
						much = 16 * \
							more;
					p2 = (__u8 *)(prt->dma_area + \
						peasycap->dma_fill);

					for (j = 0;  j < (much/16);  j++) {
						newaudio =  ((int) *p1) - 128;
						newaudio = 128 * \
								newaudio;

						delta = (newaudio - oldaudio) \
									/ 4;
						s16 = oldaudio + delta;

						for (k = 0;  k < 4;  k++) {
							*p2 = (0x00FF & s16);
							*(p2 + 1) = (0xFF00 & \
								s16) >> 8;
							p2 += 2;
							*p2 = (0x00FF & s16);
							*(p2 + 1) = (0xFF00 & \
								s16) >> 8;
							p2 += 2;
							s16 += delta;
						}
						p1++;
						more--;
						oldaudio = s16;
					}
#else /*!UPSAMPLE*/
					if (much > (2 * more))
						much = 2 * more;
					p2 = (__u8 *)(prt->dma_area + \
						peasycap->dma_fill);

					for (j = 0;  j < (much / 2);  j++) {
						s16 =  ((int) *p1) - 128;
						s16 = 128 * \
								s16;
						*p2 = (0x00FF & s16);
						*(p2 + 1) = (0xFF00 & s16) >> \
									8;
						p1++;  p2 += 2;
						more--;
					}
#endif /*UPSAMPLE*/
				}
				peasycap->dma_fill += much;
				if (peasycap->dma_fill >= peasycap->dma_next) {
					isfragment = peasycap->dma_fill / \
						fragment_bytes;
					if (0 > isfragment) {
						SAM("MISTAKE: isfragment is " \
							"negative\n");
						return;
					}
					peasycap->dma_read = (isfragment \
						- 1) * fragment_bytes;
					peasycap->dma_next = (isfragment \
						+ 1) * fragment_bytes;
					if (dma_bytes < peasycap->dma_next) {
						peasycap->dma_next = \
								fragment_bytes;
					}
					if (0 <= peasycap->dma_read) {
						JOM(8, "snd_pcm_period_elap" \
							"sed(), %i=" \
							"isfragment\n", \
							isfragment);
						snd_pcm_period_elapsed(pss);
					}
				}
			}
		}
	} else {
		JOM(12, "discarding audio samples because " \
			"%i=purb->iso_frame_desc[i].status\n", \
				purb->iso_frame_desc[i].status);
	}

#if defined(UPSAMPLE)
peasycap->oldaudio = oldaudio;
#endif /*UPSAMPLE*/

}
/*---------------------------------------------------------------------------*/
/*
 *  RESUBMIT THIS URB
 */
/*---------------------------------------------------------------------------*/
resubmit:
if (peasycap->audio_isoc_streaming) {
	rc = usb_submit_urb(purb, GFP_ATOMIC);
	if (0 != rc) {
		if ((-ENODEV != rc) && (-ENOENT != rc)) {
			SAM("ERROR: while %i=audio_idle, " \
				"usb_submit_urb() failed " \
				"with rc:\n", peasycap->audio_idle);
		}
		switch (rc) {
		case -ENODEV:
		case -ENOENT:
			break;
		case -ENOMEM: {
			SAM("-ENOMEM\n");
			break;
		}
		case -ENXIO: {
			SAM("-ENXIO\n");
			break;
		}
		case -EINVAL: {
			SAM("-EINVAL\n");
			break;
		}
		case -EAGAIN: {
			SAM("-EAGAIN\n");
			break;
		}
		case -EFBIG: {
			SAM("-EFBIG\n");
			break;
		}
		case -EPIPE: {
			SAM("-EPIPE\n");
			break;
		}
		case -EMSGSIZE: {
			SAM("-EMSGSIZE\n");
			break;
		}
		case -ENOSPC: {
			SAM("-ENOSPC\n");
			break;
		}
		case -EPERM: {
			SAM("-EPERM\n");
			break;
		}
		default: {
			SAM("unknown error: %i\n", rc);
			break;
		}
		}
		if (0 < peasycap->audio_isoc_streaming)
			(peasycap->audio_isoc_streaming)--;
	}
}
return;
}
/*****************************************************************************/
int
easycap_alsa_open(struct snd_pcm_substream *pss)
{
struct snd_pcm *psnd_pcm;
struct snd_card *psnd_card;
struct easycap *peasycap;

JOT(4, "\n");
if (NULL == pss) {
	SAY("ERROR:  pss is NULL\n");
	return -EFAULT;
}
psnd_pcm = pss->pcm;
if (NULL == psnd_pcm) {
	SAY("ERROR:  psnd_pcm is NULL\n");
	return -EFAULT;
}
psnd_card = psnd_pcm->card;
if (NULL == psnd_card) {
	SAY("ERROR:  psnd_card is NULL\n");
	return -EFAULT;
}

peasycap = psnd_card->private_data;
if (NULL == peasycap) {
	SAY("ERROR:  peasycap is NULL\n");
	return -EFAULT;
}
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	SAY("ERROR: bad peasycap\n");
	return -EFAULT;
}
if (peasycap->psnd_card != psnd_card) {
	SAM("ERROR: bad peasycap->psnd_card\n");
	return -EFAULT;
}
if (NULL != peasycap->psubstream) {
	SAM("ERROR: bad peasycap->psubstream\n");
	return -EFAULT;
}
pss->private_data = peasycap;
peasycap->psubstream = pss;
pss->runtime->hw = peasycap->alsa_hardware;
pss->runtime->private_data = peasycap;
pss->private_data = peasycap;

if (0 != easycap_sound_setup(peasycap)) {
	JOM(4, "ending unsuccessfully\n");
	return -EFAULT;
}
JOM(4, "ending successfully\n");
return 0;
}
/*****************************************************************************/
int
easycap_alsa_close(struct snd_pcm_substream *pss)
{
struct easycap *peasycap;

JOT(4, "\n");
if (NULL == pss) {
	SAY("ERROR:  pss is NULL\n");
	return -EFAULT;
}
peasycap = snd_pcm_substream_chip(pss);
if (NULL == peasycap) {
	SAY("ERROR:  peasycap is NULL\n");
	return -EFAULT;
}
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	SAY("ERROR: bad peasycap\n");
	return -EFAULT;
}
pss->private_data = NULL;
peasycap->psubstream = (struct snd_pcm_substream *)NULL;
JOT(4, "ending successfully\n");
return 0;
}
/*****************************************************************************/
int
easycap_alsa_hw_params(struct snd_pcm_substream *pss, \
						struct snd_pcm_hw_params *phw)
{
int rc;

JOT(4, "%i\n", (params_buffer_bytes(phw)));
if (NULL == pss) {
	SAY("ERROR:  pss is NULL\n");
	return -EFAULT;
}
rc = easycap_alsa_vmalloc(pss, params_buffer_bytes(phw));
if (0 != rc)
	return rc;
return 0;
}
/*****************************************************************************/
int
easycap_alsa_vmalloc(struct snd_pcm_substream *pss, size_t sz)
{
struct snd_pcm_runtime *prt;
JOT(4, "\n");

if (NULL == pss) {
	SAY("ERROR:  pss is NULL\n");
	return -EFAULT;
}
prt = pss->runtime;
if (NULL == prt) {
	SAY("ERROR: substream.runtime is NULL\n");
	return -EFAULT;
}
if (prt->dma_area) {
	if (prt->dma_bytes > sz)
		return 0;
	vfree(prt->dma_area);
}
prt->dma_area = vmalloc(sz);
if (NULL == prt->dma_area)
	return -ENOMEM;
prt->dma_bytes = sz;
return 0;
}
/*****************************************************************************/
int
easycap_alsa_hw_free(struct snd_pcm_substream *pss)
{
struct snd_pcm_runtime *prt;
JOT(4, "\n");

if (NULL == pss) {
	SAY("ERROR:  pss is NULL\n");
	return -EFAULT;
}
prt = pss->runtime;
if (NULL == prt) {
	SAY("ERROR: substream.runtime is NULL\n");
	return -EFAULT;
}
if (NULL != prt->dma_area) {
	JOT(8, "0x%08lX=prt->dma_area\n", (unsigned long int)prt->dma_area);
	vfree(prt->dma_area);
	prt->dma_area = NULL;
} else
	JOT(8, "dma_area already freed\n");
return 0;
}
/*****************************************************************************/
int
easycap_alsa_prepare(struct snd_pcm_substream *pss)
{
struct easycap *peasycap;
struct snd_pcm_runtime *prt;

JOT(4, "\n");
if (NULL == pss) {
	SAY("ERROR:  pss is NULL\n");
	return -EFAULT;
}
prt = pss->runtime;
peasycap = snd_pcm_substream_chip(pss);
if (NULL == peasycap) {
	SAY("ERROR:  peasycap is NULL\n");
	return -EFAULT;
}
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	SAY("ERROR: bad peasycap\n");
	return -EFAULT;
}

JOM(16, "ALSA decides %8i Hz=rate\n", (int)pss->runtime->rate);
JOM(16, "ALSA decides %8i   =period_size\n", (int)pss->runtime->period_size);
JOM(16, "ALSA decides %8i   =periods\n", (int)pss->runtime->periods);
JOM(16, "ALSA decides %8i   =buffer_size\n", (int)pss->runtime->buffer_size);
JOM(16, "ALSA decides %8i   =dma_bytes\n", (int)pss->runtime->dma_bytes);
JOM(16, "ALSA decides %8i   =boundary\n", (int)pss->runtime->boundary);
JOM(16, "ALSA decides %8i   =period_step\n", (int)pss->runtime->period_step);
JOM(16, "ALSA decides %8i   =sample_bits\n", (int)pss->runtime->sample_bits);
JOM(16, "ALSA decides %8i   =frame_bits\n", (int)pss->runtime->frame_bits);
JOM(16, "ALSA decides %8i   =min_align\n", (int)pss->runtime->min_align);
JOM(12, "ALSA decides %8i   =hw_ptr_base\n", (int)pss->runtime->hw_ptr_base);
JOM(12, "ALSA decides %8i   =hw_ptr_interrupt\n", \
					(int)pss->runtime->hw_ptr_interrupt);
if (prt->dma_bytes != 4 * ((int)prt->period_size) * ((int)prt->periods)) {
	SAY("MISTAKE:  unexpected ALSA parameters\n");
	return -ENOENT;
}
return 0;
}
/*****************************************************************************/
int
easycap_alsa_ack(struct snd_pcm_substream *pss)
{
return 0;
}
/*****************************************************************************/
int
easycap_alsa_trigger(struct snd_pcm_substream *pss, int cmd)
{
struct easycap *peasycap;
int retval;

JOT(4, "%i=cmd cf %i=START %i=STOP\n", cmd, SNDRV_PCM_TRIGGER_START, \
						SNDRV_PCM_TRIGGER_STOP);
if (NULL == pss) {
	SAY("ERROR:  pss is NULL\n");
	return -EFAULT;
}
peasycap = snd_pcm_substream_chip(pss);
if (NULL == peasycap) {
	SAY("ERROR:  peasycap is NULL\n");
	return -EFAULT;
}
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	SAY("ERROR: bad peasycap\n");
	return -EFAULT;
}

switch (cmd) {
case SNDRV_PCM_TRIGGER_START: {
	peasycap->audio_idle = 0;
	break;
}
case SNDRV_PCM_TRIGGER_STOP: {
	peasycap->audio_idle = 1;
	break;
}
default:
	retval = -EINVAL;
}
return 0;
}
/*****************************************************************************/
snd_pcm_uframes_t
easycap_alsa_pointer(struct snd_pcm_substream *pss)
{
struct easycap *peasycap;
snd_pcm_uframes_t offset;

JOT(16, "\n");
if (NULL == pss) {
	SAY("ERROR:  pss is NULL\n");
	return -EFAULT;
}
peasycap = snd_pcm_substream_chip(pss);
if (NULL == peasycap) {
	SAY("ERROR:  peasycap is NULL\n");
	return -EFAULT;
}
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	SAY("ERROR: bad peasycap\n");
	return -EFAULT;
}
if ((0 != peasycap->audio_eof) || (0 != peasycap->audio_idle)) {
	JOM(8, "returning -EIO because  " \
			"%i=audio_idle  %i=audio_eof\n", \
			peasycap->audio_idle, peasycap->audio_eof);
	return -EIO;
}
/*---------------------------------------------------------------------------*/
if (0 > peasycap->dma_read) {
	JOM(8, "returning -EBUSY\n");
	return -EBUSY;
}
offset = ((snd_pcm_uframes_t)peasycap->dma_read)/4;
JOM(8, "ALSA decides %8i   =hw_ptr_base\n", (int)pss->runtime->hw_ptr_base);
JOM(8, "ALSA decides %8i   =hw_ptr_interrupt\n", \
					(int)pss->runtime->hw_ptr_interrupt);
JOM(8, "%7i=offset %7i=dma_read %7i=dma_next\n", \
			(int)offset, peasycap->dma_read, peasycap->dma_next);
return offset;
}
/*****************************************************************************/
struct page *
easycap_alsa_page(struct snd_pcm_substream *pss, unsigned long offset)
{
return vmalloc_to_page(pss->runtime->dma_area + offset);
}
/*****************************************************************************/

#else /*!EASYCAP_NEEDS_ALSA*/

/*****************************************************************************/
/****************************                       **************************/
/****************************   Open Sound System   **************************/
/****************************                       **************************/
/*****************************************************************************/
/*--------------------------------------------------------------------------*/
/*
 *  PARAMETERS USED WHEN REGISTERING THE AUDIO INTERFACE
 */
/*--------------------------------------------------------------------------*/
const struct file_operations easyoss_fops = {
	.owner		= THIS_MODULE,
	.open		= easyoss_open,
	.release	= easyoss_release,
#if defined(EASYCAP_NEEDS_UNLOCKED_IOCTL)
	.unlocked_ioctl	= easyoss_ioctl_noinode,
#else
	.ioctl		= easyoss_ioctl,
#endif /*EASYCAP_NEEDS_UNLOCKED_IOCTL*/
	.read		= easyoss_read,
	.llseek		= no_llseek,
};
struct usb_class_driver easyoss_class = {
.name = "usb/easyoss%d",
.fops = &easyoss_fops,
.minor_base = USB_SKEL_MINOR_BASE,
};
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  ON COMPLETION OF AN AUDIO URB ITS DATA IS COPIED TO THE AUDIO BUFFERS
 *  PROVIDED peasycap->audio_idle IS ZERO.  REGARDLESS OF THIS BEING TRUE,
 *  IT IS RESUBMITTED PROVIDED peasycap->audio_isoc_streaming IS NOT ZERO.
 */
/*---------------------------------------------------------------------------*/
void
easyoss_complete(struct urb *purb)
{
struct easycap *peasycap;
struct data_buffer *paudio_buffer;
__u8 *p1, *p2;
__s16 s16;
int i, j, more, much, leap, rc;
#if defined(UPSAMPLE)
int k;
__s16 oldaudio, newaudio, delta;
#endif /*UPSAMPLE*/

JOT(16, "\n");

if (NULL == purb) {
	SAY("ERROR: purb is NULL\n");
	return;
}
peasycap = purb->context;
if (NULL == peasycap) {
	SAY("ERROR: peasycap is NULL\n");
	return;
}
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	SAY("ERROR: bad peasycap\n");
	return;
}
much = 0;
if (peasycap->audio_idle) {
	JOM(16, "%i=audio_idle  %i=audio_isoc_streaming\n", \
			peasycap->audio_idle, peasycap->audio_isoc_streaming);
	if (peasycap->audio_isoc_streaming) {
		rc = usb_submit_urb(purb, GFP_ATOMIC);
		if (0 != rc) {
			if (-ENODEV != rc && -ENOENT != rc) {
				SAM("ERROR: while %i=audio_idle, " \
					"usb_submit_urb() failed with rc:\n", \
							peasycap->audio_idle);
			}
			switch (rc) {
			case -ENODEV:
			case -ENOENT:
				break;
			case -ENOMEM: {
				SAM("-ENOMEM\n");
				break;
			}
			case -ENXIO: {
				SAM("-ENXIO\n");
				break;
			}
			case -EINVAL: {
				SAM("-EINVAL\n");
				break;
			}
			case -EAGAIN: {
				SAM("-EAGAIN\n");
				break;
			}
			case -EFBIG: {
				SAM("-EFBIG\n");
				break;
			}
			case -EPIPE: {
				SAM("-EPIPE\n");
				break;
			}
			case -EMSGSIZE: {
				SAM("-EMSGSIZE\n");
				break;
			}
			case -ENOSPC: {
				SAM("-ENOSPC\n");
				break;
			}
			case -EPERM: {
				SAM("-EPERM\n");
				break;
			}
			default: {
				SAM("unknown error: %i\n", rc);
				break;
			}
			}
		}
	}
return;
}
/*---------------------------------------------------------------------------*/
if (purb->status) {
	if ((-ESHUTDOWN == purb->status) || (-ENOENT == purb->status)) {
		JOM(16, "urb status -ESHUTDOWN or -ENOENT\n");
		return;
	}
	SAM("ERROR: non-zero urb status:\n");
	switch (purb->status) {
	case -EINPROGRESS: {
		SAM("-EINPROGRESS\n");
		break;
	}
	case -ENOSR: {
		SAM("-ENOSR\n");
		break;
	}
	case -EPIPE: {
		SAM("-EPIPE\n");
		break;
	}
	case -EOVERFLOW: {
		SAM("-EOVERFLOW\n");
		break;
	}
	case -EPROTO: {
		SAM("-EPROTO\n");
		break;
	}
	case -EILSEQ: {
		SAM("-EILSEQ\n");
		break;
	}
	case -ETIMEDOUT: {
		SAM("-ETIMEDOUT\n");
		break;
	}
	case -EMSGSIZE: {
		SAM("-EMSGSIZE\n");
		break;
	}
	case -EOPNOTSUPP: {
		SAM("-EOPNOTSUPP\n");
		break;
	}
	case -EPFNOSUPPORT: {
		SAM("-EPFNOSUPPORT\n");
		break;
	}
	case -EAFNOSUPPORT: {
		SAM("-EAFNOSUPPORT\n");
		break;
	}
	case -EADDRINUSE: {
		SAM("-EADDRINUSE\n");
		break;
	}
	case -EADDRNOTAVAIL: {
		SAM("-EADDRNOTAVAIL\n");
		break;
	}
	case -ENOBUFS: {
		SAM("-ENOBUFS\n");
		break;
	}
	case -EISCONN: {
		SAM("-EISCONN\n");
		break;
	}
	case -ENOTCONN: {
		SAM("-ENOTCONN\n");
		break;
	}
	case -ESHUTDOWN: {
		SAM("-ESHUTDOWN\n");
		break;
	}
	case -ENOENT: {
		SAM("-ENOENT\n");
		break;
	}
	case -ECONNRESET: {
		SAM("-ECONNRESET\n");
		break;
	}
	case -ENOSPC: {
		SAM("ENOSPC\n");
		break;
	}
	case -EPERM: {
		SAM("-EPERM\n");
		break;
	}
	default: {
		SAM("unknown error: %i\n", purb->status);
		break;
	}
	}
	goto resubmit;
}
/*---------------------------------------------------------------------------*/
/*
 *  PROCEED HERE WHEN NO ERROR
 */
/*---------------------------------------------------------------------------*/
#if defined(UPSAMPLE)
oldaudio = peasycap->oldaudio;
#endif /*UPSAMPLE*/

for (i = 0;  i < purb->number_of_packets; i++) {
	switch (purb->iso_frame_desc[i].status) {
	case  0: {
		break;
	}
	case -ENODEV: {
		SAM("-ENODEV\n");
		break;
	}
	case -ENOENT: {
		SAM("-ENOENT\n");
		break;
	}
	case -EINPROGRESS: {
		SAM("-EINPROGRESS\n");
		break;
	}
	case -EPROTO: {
		SAM("-EPROTO\n");
		break;
	}
	case -EILSEQ: {
		SAM("-EILSEQ\n");
		break;
	}
	case -ETIME: {
		SAM("-ETIME\n");
		break;
	}
	case -ETIMEDOUT: {
		SAM("-ETIMEDOUT\n");
		break;
	}
	case -EPIPE: {
		SAM("-EPIPE\n");
		break;
	}
	case -ECOMM: {
		SAM("-ECOMM\n");
		break;
	}
	case -ENOSR: {
		SAM("-ENOSR\n");
		break;
	}
	case -EOVERFLOW: {
		SAM("-EOVERFLOW\n");
		break;
	}
	case -EREMOTEIO: {
		SAM("-EREMOTEIO\n");
		break;
	}
	case -EXDEV: {
		SAM("-EXDEV\n");
		break;
	}
	case -EINVAL: {
		SAM("-EINVAL\n");
		break;
	}
	case -ECONNRESET: {
		SAM("-ECONNRESET\n");
		break;
	}
	case -ENOSPC: {
		SAM("-ENOSPC\n");
		break;
	}
	case -ESHUTDOWN: {
		SAM("-ESHUTDOWN\n");
		break;
	}
	case -EPERM: {
		SAM("-EPERM\n");
		break;
	}
	default: {
		SAM("unknown error: %i\n", purb->iso_frame_desc[i].status);
		break;
	}
	}
	if (!purb->iso_frame_desc[i].status) {
		more = purb->iso_frame_desc[i].actual_length;

#if defined(TESTTONE)
		if (!more)
			more = purb->iso_frame_desc[i].length;
#endif

		if (!more)
			peasycap->audio_mt++;
		else {
			if (peasycap->audio_mt) {
				JOM(12, "%4i empty audio urb frames\n", \
							peasycap->audio_mt);
				peasycap->audio_mt = 0;
			}

			p1 = (__u8 *)(purb->transfer_buffer + \
					purb->iso_frame_desc[i].offset);

			leap = 0;
			p1 += leap;
			more -= leap;
/*---------------------------------------------------------------------------*/
/*
 *  COPY more BYTES FROM ISOC BUFFER TO AUDIO BUFFER,
 *  CONVERTING 8-BIT MONO TO 16-BIT SIGNED LITTLE-ENDIAN SAMPLES IF NECESSARY
 */
/*---------------------------------------------------------------------------*/
			while (more) {
				if (0 > more) {
					SAM("MISTAKE: more is negative\n");
					return;
				}
				if (peasycap->audio_buffer_page_many <= \
							peasycap->audio_fill) {
					SAM("ERROR: bad " \
						"peasycap->audio_fill\n");
					return;
				}

				paudio_buffer = &peasycap->audio_buffer\
							[peasycap->audio_fill];
				if (PAGE_SIZE < (paudio_buffer->pto - \
						paudio_buffer->pgo)) {
					SAM("ERROR: bad paudio_buffer->pto\n");
					return;
				}
				if (PAGE_SIZE == (paudio_buffer->pto - \
							paudio_buffer->pgo)) {

#if defined(TESTTONE)
					easyoss_testtone(peasycap, \
							peasycap->audio_fill);
#endif /*TESTTONE*/

					paudio_buffer->pto = \
							paudio_buffer->pgo;
					(peasycap->audio_fill)++;
					if (peasycap->\
						audio_buffer_page_many <= \
							peasycap->audio_fill)
						peasycap->audio_fill = 0;

					JOM(8, "bumped peasycap->" \
							"audio_fill to %i\n", \
							peasycap->audio_fill);

					paudio_buffer = &peasycap->\
							audio_buffer\
							[peasycap->audio_fill];
					paudio_buffer->pto = \
							paudio_buffer->pgo;

					if (!(peasycap->audio_fill % \
						peasycap->\
						audio_pages_per_fragment)) {
						JOM(12, "wakeup call on wq_" \
						"audio, %i=frag reading  %i" \
						"=fragment fill\n", \
						(peasycap->audio_read / \
						peasycap->\
						audio_pages_per_fragment), \
						(peasycap->audio_fill / \
						peasycap->\
						audio_pages_per_fragment));
						wake_up_interruptible\
						(&(peasycap->wq_audio));
					}
				}

				much = PAGE_SIZE - (int)(paudio_buffer->pto -\
							 paudio_buffer->pgo);

				if (false == peasycap->microphone) {
					if (much > more)
						much = more;

					memcpy(paudio_buffer->pto, p1, much);
					p1 += much;
					more -= much;
				} else {
#if defined(UPSAMPLE)
					if (much % 16)
						JOM(8, "MISTAKE? much" \
						" is not divisible by 16\n");
					if (much > (16 * \
							more))
						much = 16 * \
							more;
					p2 = (__u8 *)paudio_buffer->pto;

					for (j = 0;  j < (much/16);  j++) {
						newaudio =  ((int) *p1) - 128;
						newaudio = 128 * \
								newaudio;

						delta = (newaudio - oldaudio) \
									/ 4;
						s16 = oldaudio + delta;

						for (k = 0;  k < 4;  k++) {
							*p2 = (0x00FF & s16);
							*(p2 + 1) = (0xFF00 & \
								s16) >> 8;
							p2 += 2;
							*p2 = (0x00FF & s16);
							*(p2 + 1) = (0xFF00 & \
								s16) >> 8;
							p2 += 2;

							s16 += delta;
						}
						p1++;
						more--;
						oldaudio = s16;
					}
#else /*!UPSAMPLE*/
					if (much > (2 * more))
						much = 2 * more;
					p2 = (__u8 *)paudio_buffer->pto;

					for (j = 0;  j < (much / 2);  j++) {
						s16 =  ((int) *p1) - 128;
						s16 = 128 * \
								s16;
						*p2 = (0x00FF & s16);
						*(p2 + 1) = (0xFF00 & s16) >> \
									8;
						p1++;  p2 += 2;
						more--;
					}
#endif /*UPSAMPLE*/
				}
				(paudio_buffer->pto) += much;
			}
		}
	} else {
		JOM(12, "discarding audio samples because " \
			"%i=purb->iso_frame_desc[i].status\n", \
				purb->iso_frame_desc[i].status);
	}

#if defined(UPSAMPLE)
peasycap->oldaudio = oldaudio;
#endif /*UPSAMPLE*/

}
/*---------------------------------------------------------------------------*/
/*
 *  RESUBMIT THIS URB
 */
/*---------------------------------------------------------------------------*/
resubmit:
if (peasycap->audio_isoc_streaming) {
	rc = usb_submit_urb(purb, GFP_ATOMIC);
	if (0 != rc) {
		if (-ENODEV != rc && -ENOENT != rc) {
			SAM("ERROR: while %i=audio_idle, " \
				"usb_submit_urb() failed " \
				"with rc:\n", peasycap->audio_idle);
		}
		switch (rc) {
		case -ENODEV:
		case -ENOENT:
			break;
		case -ENOMEM: {
			SAM("-ENOMEM\n");
			break;
		}
		case -ENXIO: {
			SAM("-ENXIO\n");
			break;
		}
		case -EINVAL: {
			SAM("-EINVAL\n");
			break;
		}
		case -EAGAIN: {
			SAM("-EAGAIN\n");
			break;
		}
		case -EFBIG: {
			SAM("-EFBIG\n");
			break;
		}
		case -EPIPE: {
			SAM("-EPIPE\n");
			break;
		}
		case -EMSGSIZE: {
			SAM("-EMSGSIZE\n");
			break;
		}
		case -ENOSPC: {
			SAM("-ENOSPC\n");
			break;
		}
		case -EPERM: {
			SAM("-EPERM\n");
			break;
		}
		default: {
			SAM("unknown error: %i\n", rc);
			break;
		}
		}
	}
}
return;
}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  THE AUDIO URBS ARE SUBMITTED AT THIS EARLY STAGE SO THAT IT IS POSSIBLE TO
 *  STREAM FROM /dev/easyoss1 WITH SIMPLE PROGRAMS SUCH AS cat WHICH DO NOT
 *  HAVE AN IOCTL INTERFACE.
 */
/*---------------------------------------------------------------------------*/
int
easyoss_open(struct inode *inode, struct file *file)
{
struct usb_interface *pusb_interface;
struct easycap *peasycap;
int subminor;
/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
#if defined(EASYCAP_IS_VIDEODEV_CLIENT)
#if defined(EASYCAP_NEEDS_V4L2_DEVICE_H)
struct v4l2_device *pv4l2_device;
#endif /*EASYCAP_NEEDS_V4L2_DEVICE_H*/
#endif /*EASYCAP_IS_VIDEODEV_CLIENT*/
/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/

JOT(4, "begins\n");

subminor = iminor(inode);

pusb_interface = usb_find_interface(&easycap_usb_driver, subminor);
if (NULL == pusb_interface) {
	SAY("ERROR: pusb_interface is NULL\n");
	SAY("ending unsuccessfully\n");
	return -1;
}
peasycap = usb_get_intfdata(pusb_interface);
if (NULL == peasycap) {
	SAY("ERROR: peasycap is NULL\n");
	SAY("ending unsuccessfully\n");
	return -1;
}
/*---------------------------------------------------------------------------*/
#if (!defined(EASYCAP_IS_VIDEODEV_CLIENT))
#
/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
#else
#if defined(EASYCAP_NEEDS_V4L2_DEVICE_H)
/*---------------------------------------------------------------------------*/
/*
 *  SOME VERSIONS OF THE videodev MODULE OVERWRITE THE DATA WHICH HAS
 *  BEEN WRITTEN BY THE CALL TO usb_set_intfdata() IN easycap_usb_probe(),
 *  REPLACING IT WITH A POINTER TO THE EMBEDDED v4l2_device STRUCTURE.
 *  TO DETECT THIS, THE STRING IN THE easycap.telltale[] BUFFER IS CHECKED.
*/
/*---------------------------------------------------------------------------*/
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	pv4l2_device = usb_get_intfdata(pusb_interface);
	if ((struct v4l2_device *)NULL == pv4l2_device) {
		SAY("ERROR: pv4l2_device is NULL\n");
		return -EFAULT;
	}
	peasycap = (struct easycap *) \
		container_of(pv4l2_device, struct easycap, v4l2_device);
}
#endif /*EASYCAP_NEEDS_V4L2_DEVICE_H*/
#
#endif /*EASYCAP_IS_VIDEODEV_CLIENT*/
/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
/*---------------------------------------------------------------------------*/
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	SAY("ERROR: bad peasycap: 0x%08lX\n", (unsigned long int) peasycap);
	return -EFAULT;
}
/*---------------------------------------------------------------------------*/

file->private_data = peasycap;

if (0 != easycap_sound_setup(peasycap)) {
	;
	;
}
return 0;
}
/*****************************************************************************/
int
easyoss_release(struct inode *inode, struct file *file)
{
struct easycap *peasycap;

JOT(4, "begins\n");

peasycap = file->private_data;
if (NULL == peasycap) {
	SAY("ERROR:  peasycap is NULL.\n");
	return -EFAULT;
}
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	SAY("ERROR: bad peasycap: 0x%08lX\n", (unsigned long int) peasycap);
	return -EFAULT;
}
if (0 != kill_audio_urbs(peasycap)) {
	SAM("ERROR: kill_audio_urbs() failed\n");
	return -EFAULT;
}
JOM(4, "ending successfully\n");
return 0;
}
/*****************************************************************************/
ssize_t
easyoss_read(struct file *file, char __user *puserspacebuffer, \
						size_t kount, loff_t *poff)
{
struct timeval timeval;
long long int above, below, mean;
struct signed_div_result sdr;
unsigned char *p0;
long int kount1, more, rc, l0, lm;
int fragment, kd;
struct easycap *peasycap;
struct data_buffer *pdata_buffer;
size_t szret;

/*---------------------------------------------------------------------------*/
/*
 *  DO A BLOCKING READ TO TRANSFER DATA TO USER SPACE.
 *
 ******************************************************************************
 *****  N.B.  IF THIS FUNCTION RETURNS 0, NOTHING IS SEEN IN USER SPACE. ******
 *****        THIS CONDITION SIGNIFIES END-OF-FILE.                      ******
 ******************************************************************************
 */
/*---------------------------------------------------------------------------*/

JOT(8, "%5i=kount  %5i=*poff\n", (int)kount, (int)(*poff));

if (NULL == file) {
	SAY("ERROR:  file is NULL\n");
	return -ERESTARTSYS;
}
peasycap = file->private_data;
if (NULL == peasycap) {
	SAY("ERROR in easyoss_read(): peasycap is NULL\n");
	return -EFAULT;
}
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	SAY("ERROR: bad peasycap: 0x%08lX\n", (unsigned long int) peasycap);
	return -EFAULT;
}
if (NULL == peasycap->pusb_device) {
	SAY("ERROR: peasycap->pusb_device is NULL\n");
	return -EFAULT;
}
kd = isdongle(peasycap);
if (0 <= kd && DONGLE_MANY > kd) {
	if (mutex_lock_interruptible(&(easycapdc60_dongle[kd].mutex_audio))) {
		SAY("ERROR: "
		"cannot lock easycapdc60_dongle[%i].mutex_audio\n", kd);
		return -ERESTARTSYS;
	}
	JOM(4, "locked easycapdc60_dongle[%i].mutex_audio\n", kd);
/*---------------------------------------------------------------------------*/
/*
 *  MEANWHILE, easycap_usb_disconnect() MAY HAVE FREED POINTER peasycap,
 *  IN WHICH CASE A REPEAT CALL TO isdongle() WILL FAIL.
 *  IF NECESSARY, BAIL OUT.
*/
/*---------------------------------------------------------------------------*/
	if (kd != isdongle(peasycap))
		return -ERESTARTSYS;
	if (NULL == file) {
		SAY("ERROR:  file is NULL\n");
		mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
		return -ERESTARTSYS;
	}
	peasycap = file->private_data;
	if (NULL == peasycap) {
		SAY("ERROR:  peasycap is NULL\n");
		mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
		return -ERESTARTSYS;
	}
	if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
		SAY("ERROR: bad peasycap: 0x%08lX\n", \
						(unsigned long int) peasycap);
		mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
		return -ERESTARTSYS;
	}
	if (NULL == peasycap->pusb_device) {
		SAM("ERROR: peasycap->pusb_device is NULL\n");
		mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
		return -ERESTARTSYS;
	}
} else {
/*---------------------------------------------------------------------------*/
/*
 *  IF easycap_usb_disconnect() HAS ALREADY FREED POINTER peasycap BEFORE THE
 *  ATTEMPT TO ACQUIRE THE SEMAPHORE, isdongle() WILL HAVE FAILED.  BAIL OUT.
*/
/*---------------------------------------------------------------------------*/
	return -ERESTARTSYS;
}
/*---------------------------------------------------------------------------*/
if (file->f_flags & O_NONBLOCK)
	JOT(16, "NONBLOCK  kount=%i, *poff=%i\n", (int)kount, (int)(*poff));
else
	JOT(8, "BLOCKING  kount=%i, *poff=%i\n", (int)kount, (int)(*poff));

if ((0 > peasycap->audio_read) || \
		(peasycap->audio_buffer_page_many <= peasycap->audio_read)) {
	SAM("ERROR: peasycap->audio_read out of range\n");
	mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
	return -EFAULT;
}
pdata_buffer = &peasycap->audio_buffer[peasycap->audio_read];
if ((struct data_buffer *)NULL == pdata_buffer) {
	SAM("ERROR: pdata_buffer is NULL\n");
	mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
	return -EFAULT;
}
JOM(12, "before wait, %i=frag read  %i=frag fill\n", \
		(peasycap->audio_read / peasycap->audio_pages_per_fragment), \
		(peasycap->audio_fill / peasycap->audio_pages_per_fragment));
fragment = (peasycap->audio_read / peasycap->audio_pages_per_fragment);
while ((fragment == (peasycap->audio_fill / \
				peasycap->audio_pages_per_fragment)) || \
		(0 == (PAGE_SIZE - (pdata_buffer->pto - pdata_buffer->pgo)))) {
	if (file->f_flags & O_NONBLOCK) {
		JOM(16, "returning -EAGAIN as instructed\n");
		mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
		return -EAGAIN;
	}
	rc = wait_event_interruptible(peasycap->wq_audio, \
		(peasycap->audio_idle  || peasycap->audio_eof   || \
		((fragment != (peasycap->audio_fill / \
				peasycap->audio_pages_per_fragment)) && \
		(0 < (PAGE_SIZE - (pdata_buffer->pto - pdata_buffer->pgo))))));
	if (0 != rc) {
		SAM("aborted by signal\n");
		mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
		return -ERESTARTSYS;
	}
	if (peasycap->audio_eof) {
		JOM(8, "returning 0 because  %i=audio_eof\n", \
							peasycap->audio_eof);
		kill_audio_urbs(peasycap);
		mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
		return 0;
	}
	if (peasycap->audio_idle) {
		JOM(16, "returning 0 because  %i=audio_idle\n", \
							peasycap->audio_idle);
		mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
		return 0;
	}
	if (!peasycap->audio_isoc_streaming) {
		JOM(16, "returning 0 because audio urbs not streaming\n");
		mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
		return 0;
	}
}
JOM(12, "after  wait, %i=frag read  %i=frag fill\n", \
		(peasycap->audio_read / peasycap->audio_pages_per_fragment), \
		(peasycap->audio_fill / peasycap->audio_pages_per_fragment));
szret = (size_t)0;
fragment = (peasycap->audio_read / peasycap->audio_pages_per_fragment);
while (fragment == (peasycap->audio_read / \
				peasycap->audio_pages_per_fragment)) {
	if (NULL == pdata_buffer->pgo) {
		SAM("ERROR: pdata_buffer->pgo is NULL\n");
		mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
		return -EFAULT;
	}
	if (NULL == pdata_buffer->pto) {
		SAM("ERROR: pdata_buffer->pto is NULL\n");
		mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
		return -EFAULT;
	}
	kount1 = PAGE_SIZE - (pdata_buffer->pto - pdata_buffer->pgo);
	if (0 > kount1) {
		SAM("MISTAKE: kount1 is negative\n");
		mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
		return -ERESTARTSYS;
	}
	if (!kount1) {
		(peasycap->audio_read)++;
		if (peasycap->audio_buffer_page_many <= peasycap->audio_read)
			peasycap->audio_read = 0;
		JOM(12, "bumped peasycap->audio_read to %i\n", \
						peasycap->audio_read);

		if (fragment != (peasycap->audio_read / \
					peasycap->audio_pages_per_fragment))
			break;

		if ((0 > peasycap->audio_read) || \
			(peasycap->audio_buffer_page_many <= \
					peasycap->audio_read)) {
			SAM("ERROR: peasycap->audio_read out of range\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		pdata_buffer = &peasycap->audio_buffer[peasycap->audio_read];
		if ((struct data_buffer *)NULL == pdata_buffer) {
			SAM("ERROR: pdata_buffer is NULL\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		if (NULL == pdata_buffer->pgo) {
			SAM("ERROR: pdata_buffer->pgo is NULL\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		if (NULL == pdata_buffer->pto) {
			SAM("ERROR: pdata_buffer->pto is NULL\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		kount1 = PAGE_SIZE - (pdata_buffer->pto - pdata_buffer->pgo);
	}
	JOM(12, "ready  to send %li bytes\n", (long int) kount1);
	JOM(12, "still  to send %li bytes\n", (long int) kount);
	more = kount1;
	if (more > kount)
		more = kount;
	JOM(12, "agreed to send %li bytes from page %i\n", \
						more, peasycap->audio_read);
	if (!more)
		break;

/*---------------------------------------------------------------------------*/
/*
 *  ACCUMULATE DYNAMIC-RANGE INFORMATION
 */
/*---------------------------------------------------------------------------*/
	p0 = (unsigned char *)pdata_buffer->pgo;  l0 = 0;  lm = more/2;
	while (l0 < lm) {
		SUMMER(p0, &peasycap->audio_sample, &peasycap->audio_niveau, \
				&peasycap->audio_square);  l0++;  p0 += 2;
	}
/*---------------------------------------------------------------------------*/
	rc = copy_to_user(puserspacebuffer, pdata_buffer->pto, more);
	if (0 != rc) {
		SAM("ERROR: copy_to_user() returned %li\n", rc);
		mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
		return -EFAULT;
	}
	*poff += (loff_t)more;
	szret += (size_t)more;
	pdata_buffer->pto += more;
	puserspacebuffer += more;
	kount -= (size_t)more;
}
JOM(12, "after  read, %i=frag read  %i=frag fill\n", \
		(peasycap->audio_read / peasycap->audio_pages_per_fragment), \
		(peasycap->audio_fill / peasycap->audio_pages_per_fragment));
if (kount < 0) {
	SAM("MISTAKE:  %li=kount  %li=szret\n", \
					(long int)kount, (long int)szret);
}
/*---------------------------------------------------------------------------*/
/*
 *  CALCULATE DYNAMIC RANGE FOR (VAPOURWARE) AUTOMATIC VOLUME CONTROL
 */
/*---------------------------------------------------------------------------*/
if (peasycap->audio_sample) {
	below = peasycap->audio_sample;
	above = peasycap->audio_square;
	sdr = signed_div(above, below);
	above = sdr.quotient;
	mean = peasycap->audio_niveau;
	sdr = signed_div(mean, peasycap->audio_sample);

	JOM(8, "%8lli=mean  %8lli=meansquare after %lli samples, =>\n", \
				sdr.quotient, above, peasycap->audio_sample);

	sdr = signed_div(above, 32768);
	JOM(8, "audio dynamic range is roughly %lli\n", sdr.quotient);
}
/*---------------------------------------------------------------------------*/
/*
 *  UPDATE THE AUDIO CLOCK
 */
/*---------------------------------------------------------------------------*/
do_gettimeofday(&timeval);
if (!peasycap->timeval1.tv_sec) {
	peasycap->audio_bytes = 0;
	peasycap->timeval3 = timeval;
	peasycap->timeval1 = peasycap->timeval3;
	sdr.quotient = 192000;
} else {
	peasycap->audio_bytes += (long long int) szret;
	below = ((long long int)(1000000)) * \
		((long long int)(timeval.tv_sec  - \
						peasycap->timeval3.tv_sec)) + \
		(long long int)(timeval.tv_usec - peasycap->timeval3.tv_usec);
	above = 1000000 * ((long long int) peasycap->audio_bytes);

	if (below)
		sdr = signed_div(above, below);
	else
		sdr.quotient = 192000;
}
JOM(8, "audio streaming at %lli bytes/second\n", sdr.quotient);
peasycap->dnbydt = sdr.quotient;

mutex_unlock(&easycapdc60_dongle[kd].mutex_audio);
JOM(4, "unlocked easycapdc60_dongle[%i].mutex_audio\n", kd);
JOM(8, "returning %li\n", (long int)szret);
return szret;
}
/*****************************************************************************/

#endif /*!EASYCAP_NEEDS_ALSA*/

/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  COMMON AUDIO INITIALIZATION
 */
/*---------------------------------------------------------------------------*/
int
easycap_sound_setup(struct easycap *peasycap)
{
int rc;

JOM(4, "starting initialization\n");

if (NULL == peasycap) {
	SAY("ERROR:  peasycap is NULL.\n");
	return -EFAULT;
}
if ((struct usb_device *)NULL == peasycap->pusb_device) {
	SAM("ERROR: peasycap->pusb_device is NULL\n");
	return -ENODEV;
}
JOM(16, "0x%08lX=peasycap->pusb_device\n", (long int)peasycap->pusb_device);

rc = audio_setup(peasycap);
JOM(8, "audio_setup() returned %i\n", rc);

if ((struct usb_device *)NULL == peasycap->pusb_device) {
	SAM("ERROR: peasycap->pusb_device has become NULL\n");
	return -ENODEV;
}
/*---------------------------------------------------------------------------*/
if ((struct usb_device *)NULL == peasycap->pusb_device) {
	SAM("ERROR: peasycap->pusb_device has become NULL\n");
	return -ENODEV;
}
rc = usb_set_interface(peasycap->pusb_device, peasycap->audio_interface, \
					peasycap->audio_altsetting_on);
JOM(8, "usb_set_interface(.,%i,%i) returned %i\n", peasycap->audio_interface, \
					peasycap->audio_altsetting_on, rc);

rc = wakeup_device(peasycap->pusb_device);
JOM(8, "wakeup_device() returned %i\n", rc);

peasycap->audio_eof = 0;
peasycap->audio_idle = 0;

peasycap->timeval1.tv_sec  = 0;
peasycap->timeval1.tv_usec = 0;

submit_audio_urbs(peasycap);

JOM(4, "finished initialization\n");
return 0;
}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  SUBMIT ALL AUDIO URBS.
 */
/*---------------------------------------------------------------------------*/
int
submit_audio_urbs(struct easycap *peasycap)
{
struct data_urb *pdata_urb;
struct urb *purb;
struct list_head *plist_head;
int j, isbad, nospc, m, rc;
int isbuf;

if (NULL == peasycap) {
	SAY("ERROR: peasycap is NULL\n");
	return -EFAULT;
}
if ((struct list_head *)NULL == peasycap->purb_audio_head) {
	SAM("ERROR: peasycap->urb_audio_head uninitialized\n");
	return -EFAULT;
}
if ((struct usb_device *)NULL == peasycap->pusb_device) {
	SAM("ERROR: peasycap->pusb_device is NULL\n");
	return -EFAULT;
}
if (!peasycap->audio_isoc_streaming) {
	JOM(4, "initial submission of all audio urbs\n");
	rc = usb_set_interface(peasycap->pusb_device,
					peasycap->audio_interface, \
					peasycap->audio_altsetting_on);
	JOM(8, "usb_set_interface(.,%i,%i) returned %i\n", \
					peasycap->audio_interface, \
					peasycap->audio_altsetting_on, rc);

	isbad = 0;  nospc = 0;  m = 0;
	list_for_each(plist_head, (peasycap->purb_audio_head)) {
		pdata_urb = list_entry(plist_head, struct data_urb, list_head);
		if (NULL != pdata_urb) {
			purb = pdata_urb->purb;
			if (NULL != purb) {
				isbuf = pdata_urb->isbuf;

				purb->interval = 1;
				purb->dev = peasycap->pusb_device;
				purb->pipe = \
					usb_rcvisocpipe(peasycap->pusb_device,\
					peasycap->audio_endpointnumber);
				purb->transfer_flags = URB_ISO_ASAP;
				purb->transfer_buffer = \
					peasycap->audio_isoc_buffer[isbuf].pgo;
				purb->transfer_buffer_length = \
					peasycap->audio_isoc_buffer_size;
#if defined(EASYCAP_NEEDS_ALSA)
				purb->complete = easycap_alsa_complete;
#else
				purb->complete = easyoss_complete;
#endif /*EASYCAP_NEEDS_ALSA*/
				purb->context = peasycap;
				purb->start_frame = 0;
				purb->number_of_packets = \
					peasycap->audio_isoc_framesperdesc;
				for (j = 0;  j < peasycap->\
						audio_isoc_framesperdesc; \
									j++) {
					purb->iso_frame_desc[j].offset = j * \
						peasycap->\
						audio_isoc_maxframesize;
					purb->iso_frame_desc[j].length = \
						peasycap->\
						audio_isoc_maxframesize;
				}

				rc = usb_submit_urb(purb, GFP_KERNEL);
				if (0 != rc) {
					isbad++;
					SAM("ERROR: usb_submit_urb() failed" \
							" for urb with rc:\n");
					switch (rc) {
					case -ENODEV: {
						SAM("-ENODEV\n");
						break;
					}
					case -ENOENT: {
						SAM("-ENOENT\n");
						break;
					}
					case -ENOMEM: {
						SAM("-ENOMEM\n");
						break;
					}
					case -ENXIO: {
						SAM("-ENXIO\n");
						break;
					}
					case -EINVAL: {
						SAM("-EINVAL\n");
						break;
					}
					case -EAGAIN: {
						SAM("-EAGAIN\n");
						break;
					}
					case -EFBIG: {
						SAM("-EFBIG\n");
						break;
					}
					case -EPIPE: {
						SAM("-EPIPE\n");
						break;
					}
					case -EMSGSIZE: {
						SAM("-EMSGSIZE\n");
						break;
					}
					case -ENOSPC: {
						nospc++;
						break;
					}
					case -EPERM: {
						SAM("-EPERM\n");
						break;
					}
					default: {
						SAM("unknown error: %i\n", rc);
						break;
					}
					}
				} else {
					 m++;
				}
			} else {
				isbad++;
			}
		} else {
			isbad++;
		}
	}
	if (nospc) {
		SAM("-ENOSPC=usb_submit_urb() for %i urbs\n", nospc);
		SAM(".....  possibly inadequate USB bandwidth\n");
		peasycap->audio_eof = 1;
	}
	if (isbad) {
		JOM(4, "attempting cleanup instead of submitting\n");
		list_for_each(plist_head, (peasycap->purb_audio_head)) {
			pdata_urb = list_entry(plist_head, struct data_urb, \
								list_head);
			if (NULL != pdata_urb) {
				purb = pdata_urb->purb;
				if (NULL != purb)
					usb_kill_urb(purb);
			}
		}
		peasycap->audio_isoc_streaming = 0;
	} else {
		peasycap->audio_isoc_streaming = m;
		JOM(4, "submitted %i audio urbs\n", m);
	}
} else
	JOM(4, "already streaming audio urbs\n");

return 0;
}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  KILL ALL AUDIO URBS.
 */
/*---------------------------------------------------------------------------*/
int
kill_audio_urbs(struct easycap *peasycap)
{
int m;
struct list_head *plist_head;
struct data_urb *pdata_urb;

if (NULL == peasycap) {
	SAY("ERROR: peasycap is NULL\n");
	return -EFAULT;
}
if (peasycap->audio_isoc_streaming) {
	if ((struct list_head *)NULL != peasycap->purb_audio_head) {
		peasycap->audio_isoc_streaming = 0;
		JOM(4, "killing audio urbs\n");
		m = 0;
		list_for_each(plist_head, (peasycap->purb_audio_head)) {
			pdata_urb = list_entry(plist_head, struct data_urb,
								list_head);
			if ((struct data_urb *)NULL != pdata_urb) {
				if ((struct urb *)NULL != pdata_urb->purb) {
					usb_kill_urb(pdata_urb->purb);
					m++;
				}
			}
		}
		JOM(4, "%i audio urbs killed\n", m);
	} else {
		SAM("ERROR: peasycap->purb_audio_head is NULL\n");
		return -EFAULT;
	}
} else {
	JOM(8, "%i=audio_isoc_streaming, no audio urbs killed\n", \
					peasycap->audio_isoc_streaming);
}
return 0;
}
/*****************************************************************************/
