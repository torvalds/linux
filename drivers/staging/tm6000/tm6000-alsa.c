/*
 *
 *  Support for audio capture for tm5600/6000/6010
 *    (c) 2007-2008 Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 *  Based on cx88-alsa.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/usb.h>
#include <linux/slab.h>

#include <asm/delay.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/control.h>
#include <sound/initval.h>


#include "tm6000.h"
#include "tm6000-regs.h"

#undef dprintk

#define dprintk(level, fmt, arg...) do {				   \
	if (debug >= level)						   \
		printk(KERN_INFO "%s/1: " fmt, chip->core->name , ## arg); \
	} while (0)

/****************************************************************************
	Data type declarations - Can be moded to a header file later
 ****************************************************************************/

struct snd_tm6000_card {
	struct snd_card            *card;

	spinlock_t                 reg_lock;

	atomic_t		   count;

	unsigned int               period_size;
	unsigned int               num_periods;

	struct tm6000_core         *core;
	struct tm6000_buffer       *buf;

	int			   bufsize;

	struct snd_pcm_substream *substream;
};


/****************************************************************************
			Module global static vars
 ****************************************************************************/

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;       /* ID for this card */
static int enable[SNDRV_CARDS] = {1, [1 ... (SNDRV_CARDS - 1)] = 1};

module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable tm6000x soundcard. default enabled.");

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for tm6000x capture interface(s).");


/****************************************************************************
				Module macros
 ****************************************************************************/

MODULE_DESCRIPTION("ALSA driver module for tm5600/tm6000/tm6010 based TV cards");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Trident,tm5600},"
			"{{Trident,tm6000},"
			"{{Trident,tm6010}");
static unsigned int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "enable debug messages");

/****************************************************************************
			Module specific funtions
 ****************************************************************************/

/*
 * BOARD Specific: Sets audio DMA
 */

static int _tm6000_start_audio_dma(struct snd_tm6000_card *chip)
{
	struct tm6000_core *core = chip->core;
	int val;

	/* Enables audio */
	val = tm6000_get_reg(core, TM6010_REQ07_RCC_ACTIVE_VIDEO_IF, 0x0);
	val |= 0x20;
	tm6000_set_reg(core, TM6010_REQ07_RCC_ACTIVE_VIDEO_IF, val);

	tm6000_set_reg(core, TM6010_REQ08_R01_A_INIT, 0x80);

	return 0;
}

/*
 * BOARD Specific: Resets audio DMA
 */
static int _tm6000_stop_audio_dma(struct snd_tm6000_card *chip)
{
	struct tm6000_core *core = chip->core;
	int val;
	dprintk(1, "Stopping audio DMA\n");

	/* Enables audio */
	val = tm6000_get_reg(core, TM6010_REQ07_RCC_ACTIVE_VIDEO_IF, 0x0);
	val &= ~0x20;
	tm6000_set_reg(core, TM6010_REQ07_RCC_ACTIVE_VIDEO_IF, val);

	tm6000_set_reg(core, TM6010_REQ08_R01_A_INIT, 0);

	return 0;
}

static int dsp_buffer_free(struct snd_tm6000_card *chip)
{
	BUG_ON(!chip->bufsize);

	dprintk(2, "Freeing buffer\n");

	/* FIXME: Frees buffer */

	chip->bufsize = 0;

       return 0;
}

/****************************************************************************
				ALSA PCM Interface
 ****************************************************************************/

/*
 * Digital hardware definition
 */
#define DEFAULT_FIFO_SIZE	4096

static struct snd_pcm_hardware snd_tm6000_digital_hw = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,

	.rates =		SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000,
	.rate_min =		44100,
	.rate_max =		48000,
	.channels_min = 2,
	.channels_max = 2,
	.period_bytes_min = DEFAULT_FIFO_SIZE/4,
	.period_bytes_max = DEFAULT_FIFO_SIZE/4,
	.periods_min = 1,
	.periods_max = 1024,
	.buffer_bytes_max = (1024*1024),
};

/*
 * audio pcm capture open callback
 */
static int snd_tm6000_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_tm6000_card *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	err = snd_pcm_hw_constraint_pow2(runtime, 0,
					 SNDRV_PCM_HW_PARAM_PERIODS);
	if (err < 0)
		goto _error;

	chip->substream = substream;

	runtime->hw = snd_tm6000_digital_hw;

	return 0;
_error:
	dprintk(1, "Error opening PCM!\n");
	return err;
}

/*
 * audio close callback
 */
static int snd_tm6000_close(struct snd_pcm_substream *substream)
{
	return 0;
}

/*
 * hw_params callback
 */
static int snd_tm6000_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *hw_params)
{
	struct snd_tm6000_card *chip = snd_pcm_substream_chip(substream);

	if (substream->runtime->dma_area) {
		dsp_buffer_free(chip);
		substream->runtime->dma_area = NULL;
	}

	chip->period_size = params_period_bytes(hw_params);
	chip->num_periods = params_periods(hw_params);
	chip->bufsize = chip->period_size * params_periods(hw_params);

	BUG_ON(!chip->bufsize);

	dprintk(1, "Setting buffer\n");

	/* FIXME: Allocate buffer for audio */


	return 0;
}

/*
 * hw free callback
 */
static int snd_tm6000_hw_free(struct snd_pcm_substream *substream)
{

	struct snd_tm6000_card *chip = snd_pcm_substream_chip(substream);

	if (substream->runtime->dma_area) {
		dsp_buffer_free(chip);
		substream->runtime->dma_area = NULL;
	}

	return 0;
}

/*
 * prepare callback
 */
static int snd_tm6000_prepare(struct snd_pcm_substream *substream)
{
	return 0;
}


/*
 * trigger callback
 */
static int snd_tm6000_card_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_tm6000_card *chip = snd_pcm_substream_chip(substream);
	int err;

	spin_lock(&chip->reg_lock);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		err = _tm6000_start_audio_dma(chip);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		err = _tm6000_stop_audio_dma(chip);
		break;
	default:
		err = -EINVAL;
		break;
	}

	spin_unlock(&chip->reg_lock);

	return err;
}

/*
 * pointer callback
 */
static snd_pcm_uframes_t snd_tm6000_pointer(struct snd_pcm_substream *substream)
{
	struct snd_tm6000_card *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	u16 count;

	count = atomic_read(&chip->count);

	return runtime->period_size * (count & (runtime->periods-1));
}

/*
 * operators
 */
static struct snd_pcm_ops snd_tm6000_pcm_ops = {
	.open = snd_tm6000_pcm_open,
	.close = snd_tm6000_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_tm6000_hw_params,
	.hw_free = snd_tm6000_hw_free,
	.prepare = snd_tm6000_prepare,
	.trigger = snd_tm6000_card_trigger,
	.pointer = snd_tm6000_pointer,
};

/*
 * create a PCM device
 */
static int __devinit snd_tm6000_pcm(struct snd_tm6000_card *chip,
				    int device, char *name)
{
	int err;
	struct snd_pcm *pcm;

	err = snd_pcm_new(chip->card, name, device, 0, 1, &pcm);
	if (err < 0)
		return err;
	pcm->private_data = chip;
	strcpy(pcm->name, name);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_tm6000_pcm_ops);

	return 0;
}

/* FIXME: Control interface - How to control volume/mute? */

/****************************************************************************
			Basic Flow for Sound Devices
 ****************************************************************************/

/*
 * Alsa Constructor - Component probe
 */

int tm6000_audio_init(struct tm6000_core *dev, int idx)
{
	struct snd_card         *card;
	struct snd_tm6000_card  *chip;
	int                     rc, len;
	char                    component[14];

	if (idx >= SNDRV_CARDS)
		return -ENODEV;

	if (!enable[idx])
		return -ENOENT;

	rc = snd_card_create(index[idx], id[idx], THIS_MODULE, 0, &card);
	if (rc < 0) {
		snd_printk(KERN_ERR "cannot create card instance %d\n", idx);
		return rc;
	}

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		rc = -ENOMEM;
		goto error;
	}

	chip->core = dev;
	chip->card = card;

	strcpy(card->driver, "tm6000-alsa");
	sprintf(component, "USB%04x:%04x",
		le16_to_cpu(dev->udev->descriptor.idVendor),
		le16_to_cpu(dev->udev->descriptor.idProduct));
	snd_component_add(card, component);

	if (dev->udev->descriptor.iManufacturer)
		len = usb_string(dev->udev,
				 dev->udev->descriptor.iManufacturer,
				 card->longname, sizeof(card->longname));
	else
		len = 0;

	if (len > 0)
		strlcat(card->longname, " ", sizeof(card->longname));

	strlcat(card->longname, card->shortname, sizeof(card->longname));

	len = strlcat(card->longname, " at ", sizeof(card->longname));

	if (len < sizeof(card->longname))
		usb_make_path(dev->udev, card->longname + len,
			      sizeof(card->longname) - len);

	strlcat(card->longname,
		dev->udev->speed == USB_SPEED_LOW ? ", low speed" :
		dev->udev->speed == USB_SPEED_FULL ? ", full speed" :
							   ", high speed",
		sizeof(card->longname));

	rc = snd_tm6000_pcm(chip, 0, "tm6000 Digital");
	if (rc < 0)
		goto error;

	rc = snd_card_register(card);
	if (rc < 0)
		goto error;


	return 0;

error:
	snd_card_free(card);
	return rc;
}

static int tm6000_audio_fini(struct tm6000_core *dev)
{
	return 0;
}

struct tm6000_ops audio_ops = {
	.id	= TM6000_AUDIO,
	.name	= "TM6000 Audio Extension",
	.init	= tm6000_audio_init,
	.fini	= tm6000_audio_fini,
};

static int __init tm6000_alsa_register(void)
{
	return tm6000_register_extension(&audio_ops);
}

static void __exit tm6000_alsa_unregister(void)
{
	tm6000_unregister_extension(&audio_ops);
}

module_init(tm6000_alsa_register);
module_exit(tm6000_alsa_unregister);
