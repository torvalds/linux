/*
 * ngene_snd.c: nGene PCIe bridge driver ALSA support
 *
 * Copyright (C) 2005-2007 Micronas
 *
 * Based on the initial ALSA support port by Thomas Eschbach.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/version.h>
#include <linux/module.h>

#include "ngene.h"
#include "ngene-ioctls.h"

static int sound_dev;

/* sound module parameters (see "Module Parameters") */
static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static int enable[SNDRV_CARDS] = {1, [1 ... (SNDRV_CARDS - 1)] = 1};

/****************************************************************************/
/* PCM Sound Funktions ******************************************************/
/****************************************************************************/

static struct snd_pcm_hardware snd_mychip_capture_hw = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER),
	.formats =          SNDRV_PCM_FMTBIT_S16_LE,
	.rates =            (SNDRV_PCM_RATE_11025 | SNDRV_PCM_RATE_16000
			    | SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000
			    | SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000),
	.rate_min =         11025,
	.rate_max =         48000,
	.channels_min =     2,
	.channels_max =     2,
	.buffer_bytes_max = 16384,
	.period_bytes_min = 8192,
	.period_bytes_max = 8192,
	.periods_min =      1,
	.periods_max =      2,
};

/* open callback */
static int snd_mychip_capture_open(struct snd_pcm_substream *substream)
{

	struct mychip *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	runtime->hw = snd_mychip_capture_hw;
	chip->substream = substream;
	return 0;
}

/* close callback */
static int snd_mychip_capture_close(struct snd_pcm_substream *substream)
{
	struct mychip *chip = snd_pcm_substream_chip(substream);
	chip->substream = NULL;
	return 0;

}

/* hw_params callback */
static int snd_mychip_pcm_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *hw_params)
{
	struct mychip *chip = snd_pcm_substream_chip(substream);
	struct ngene_channel *chan = chip->chan;
	if (chan->soundbuffisallocated == 0) {
		chan->soundbuffisallocated = 1;
		return snd_pcm_lib_malloc_pages(substream,
						params_buffer_bytes(hw_params));
	}
	return 0;
}

/* hw_free callback */
static int snd_mychip_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct mychip *chip = snd_pcm_substream_chip(substream);
	struct ngene_channel *chan = chip->chan;
	int retval = 0;
	if (chan->soundbuffisallocated == 1) {
		chan->soundbuffisallocated = 0;
		retval = snd_pcm_lib_free_pages(substream);
	}
	return retval;
}

/* prepare callback */
static int snd_mychip_pcm_prepare(struct snd_pcm_substream *substream)
{

	struct mychip *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct ngene_channel *chan = chip->chan;
	struct ngene_channel *ch = &chan->dev->channel[chan->number - 2];
	struct i2c_adapter *adap = &ch->i2c_adapter;

	if (ch->soundstreamon == 1)
		;/*ngene_command_stream_control_sound(chan->dev, chan->number,
						      0x00, 0x00);*/
	i2c_clients_command(adap, IOCTL_MIC_DEC_SRATE, &(runtime->rate));
	mdelay(80);
	if (ch->soundstreamon == 1)
		;/*ngene_command_stream_control_sound(chan->dev, chan->number,
						      0x80, 0x04);*/

	return 0;
}

/* trigger callback */
static int snd_mychip_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct mychip *chip = snd_pcm_substream_chip(substream);
	struct ngene_channel *chan = chip->chan;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		/* do something to start the PCM engine */
		chan->sndbuffflag = 0;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		/* do something to stop the PCM engine */
		chip->substream = NULL;
		chan->sndbuffflag = 0;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/* pointer callback */
static snd_pcm_uframes_t
snd_mychip_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct mychip *chip = snd_pcm_substream_chip(substream);
	struct ngene_channel *chan = chip->chan;
	unsigned int current_ptr;

	if (chan->sndbuffflag == 0) {
		current_ptr = (unsigned int)
			      bytes_to_frames(substream->runtime, 0);
	} else {
		current_ptr = (unsigned int)
			      bytes_to_frames(substream->runtime, 8192);
	}
	return current_ptr;
}

/*copy sound buffer to pcm middel layer*/
static int snd_capture_copy(struct snd_pcm_substream *substream, int channel,
			    snd_pcm_uframes_t pos, void *dst,
			    snd_pcm_uframes_t count)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct mychip *chip = snd_pcm_substream_chip(substream);
	struct ngene_channel *chan = chip->chan;

	memcpy(dst, chan->soundbuffer, frames_to_bytes(runtime, count));
	return 0;
}

static int snd_pcm_capture_silence(struct snd_pcm_substream *substream,
				   int channel,
				   snd_pcm_uframes_t pos,
				   snd_pcm_uframes_t count)
{
	/*
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct mychip *chip = snd_pcm_substream_chip(substream);
	struct ngene_channel *chan = chip->chan;
	*/
	return 0;
}

/* operators */
static struct snd_pcm_ops snd_mychip_capture_ops = {
	.open      = snd_mychip_capture_open,
	.close     = snd_mychip_capture_close,
	.ioctl     = snd_pcm_lib_ioctl,
	.hw_params = snd_mychip_pcm_hw_params,
	.hw_free   = snd_mychip_pcm_hw_free,
	.prepare   = snd_mychip_pcm_prepare,
	.trigger   = snd_mychip_pcm_trigger,
	.pointer   = snd_mychip_pcm_pointer,
	.copy      = snd_capture_copy,
	.silence   = snd_pcm_capture_silence,
};

static void mychip_pcm_free(struct snd_pcm *pcm)
{
	pcm->private_data = NULL;
}

/* create a pcm device */
static int snd_mychip_new_pcm(struct mychip *chip, struct ngene_channel *chan)
{
	struct snd_pcm *pcm;
	int err;
	char gro[10];
	sprintf(gro, "PCM%d", chan->number);

	err = snd_pcm_new(chip->card, gro, 0, 0, 1, &pcm);
	if (err < 0)
		return err;

	pcm->private_data = chip;
	pcm->private_free = mychip_pcm_free;

	sprintf(pcm->name, "MyPCM_%d", chan->number);

	chip->pcm = pcm;
	/* set operators */
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_mychip_capture_ops);
	/* pre-allocation of buffers */

	err = snd_pcm_lib_preallocate_pages_for_all(pcm,
						    SNDRV_DMA_TYPE_CONTINUOUS,
						    snd_dma_continuous_data(
							GFP_KERNEL),
						    0, 16 * 1024);

	return 0;
}

#define ngene_VOLUME(xname, xindex, addr) \
  { .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_volume_info, \
  .get = snd_volume_get, .put = snd_volume_put, \
  .private_value = addr }

static int snd_volume_info(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 20;
	return 0;
}

static int snd_volume_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct mychip *chip = snd_kcontrol_chip(kcontrol);
	int addr = kcontrol->private_value;

	ucontrol->value.integer.value[0] = chip->mixer_volume[addr][0];
	ucontrol->value.integer.value[1] = chip->mixer_volume[addr][1];
	return 0;
}

static int snd_volume_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct mychip *chip = snd_kcontrol_chip(kcontrol);
	int change, addr = kcontrol->private_value;
	int left, right;

	left = ucontrol->value.integer.value[0];
	if (left < 0)
		left = 0;
	if (left > 20)
		left = 20;
	right = ucontrol->value.integer.value[1];
	if (right < 0)
		right = 0;
	if (right > 20)
		right = 20;
	spin_lock_irq(&chip->mixer_lock);
	change = chip->mixer_volume[addr][0] != left ||
		 chip->mixer_volume[addr][1] != right;
	chip->mixer_volume[addr][0] = left;
	chip->mixer_volume[addr][1] = right;
	spin_unlock_irq(&chip->mixer_lock);
	return change;
}

#define ngene_CAPSRC(xname, xindex, addr) \
  { .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_capsrc_info, \
  .get = snd_capsrc_get, .put = snd_capsrc_put, \
  .private_value = addr }

static int snd_capsrc_info(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_capsrc_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct mychip *chip = snd_kcontrol_chip(kcontrol);
	int addr = kcontrol->private_value;

	spin_lock_irq(&chip->mixer_lock);
	ucontrol->value.integer.value[0] = chip->capture_source[addr][0];
	ucontrol->value.integer.value[1] = chip->capture_source[addr][1];
	spin_unlock_irq(&chip->mixer_lock);

	return 0;
}

static int snd_capsrc_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct mychip *chip = snd_kcontrol_chip(kcontrol);
	int change, addr = kcontrol->private_value;
	int left, right;

	left = ucontrol->value.integer.value[0] & 1;
	right = ucontrol->value.integer.value[1] & 1;
	spin_lock_irq(&chip->mixer_lock);

	change = chip->capture_source[addr][0] != left ||
		 chip->capture_source[addr][1] != right;
	chip->capture_source[addr][0] = left;
	chip->capture_source[addr][1] = right;

	spin_unlock_irq(&chip->mixer_lock);

	if (change)
		printk(KERN_INFO "snd_capsrc_put change\n");
	return 0;
}

static struct snd_kcontrol_new snd_controls[] = {
	ngene_VOLUME("Video Volume", 0, MIXER_ADDR_TVTUNER),
	ngene_CAPSRC("Video Capture Switch", 0, MIXER_ADDR_TVTUNER),
};

static int snd_card_new_mixer(struct mychip *chip)
{
	struct snd_card *card = chip->card;
	unsigned int idx;
	int err;

	strcpy(card->mixername, "NgeneMixer");

	for (idx = 0; idx < ARRAY_SIZE(snd_controls); idx++) {
		err = snd_ctl_add(card, snd_ctl_new1(&snd_controls[idx], chip));
		if (err < 0)
			return err;
	}
	return 0;
}

int ngene_snd_init(struct ngene_channel *chan)
{
	struct snd_card *card;
	struct mychip *chip;
	int err;

	if (sound_dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[sound_dev]) {
		sound_dev++;
		return -ENOENT;
	}
	card = snd_card_new(index[sound_dev], id[sound_dev],
			    THIS_MODULE, sizeof(struct mychip));
	if (card == NULL)
		return -ENOMEM;

	chip = card->private_data;
	chip->card = card;
	chip->irq = -1;

	sprintf(card->shortname, "MyChip%d%d", chan->dev->nr, chan->number);
	sprintf(card->shortname, "Myown%d%d", chan->dev->nr, chan->number);
	sprintf(card->longname, "My first Own Chip on Card Nr.%d  is %d",
		chan->dev->nr, chan->number);

	spin_lock_init(&chip->lock);
	spin_lock_init(&chip->mixer_lock);

	snd_card_new_mixer(chip);

	snd_mychip_new_pcm(chip, chan);
	err = snd_card_register(card);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}
	chan->soundcard = card;
	chan->mychip = chip;
	chip->chan = chan;
	sound_dev++;
	return 0;
}

int ngene_snd_exit(struct ngene_channel *chan)
{
	snd_card_free(chan->soundcard);
	return 0;
}
