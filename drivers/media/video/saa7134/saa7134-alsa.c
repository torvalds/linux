/*
 *   SAA713x ALSA support for V4L
 *   Ricardo Cerqueira <v4l@cerqueira.org>
 *
 *
 *   Caveats:
 *        - Volume doesn't work (it's always at max)
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, version 2
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/moduleparam.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/rawmidi.h>
#include <sound/initval.h>

#include "saa7134.h"
#include "saa7134-reg.h"

static unsigned int alsa_debug  = 0;
module_param(alsa_debug, int, 0644);
MODULE_PARM_DESC(alsa_debug,"enable debug messages [alsa]");

/*
 * Configuration macros
 */

/* defaults */
#define MAX_BUFFER_SIZE		(256*1024)
#define USE_FORMATS 		SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE | SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_U16_LE | SNDRV_PCM_FMTBIT_U16_BE
#define USE_RATE		SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000
#define USE_RATE_MIN		32000
#define USE_RATE_MAX		48000
#define USE_CHANNELS_MIN 	1
#define USE_CHANNELS_MAX 	2
#ifndef USE_PERIODS_MIN
#define USE_PERIODS_MIN 	2
#endif
#ifndef USE_PERIODS_MAX
#define USE_PERIODS_MAX 	1024
#endif

#define MIXER_ADDR_TVTUNER	0
#define MIXER_ADDR_LINE1	1
#define MIXER_ADDR_LINE2	2
#define MIXER_ADDR_LAST		2

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = {1, [1 ... (SNDRV_CARDS - 1)] = 0};

#define dprintk(fmt, arg...)    if (alsa_debug) \
        printk(KERN_DEBUG "%s/alsa: " fmt, dev->name , ## arg)

/*
 * Main chip structure
 */
typedef struct snd_card_saa7134 {
	snd_card_t *card;
	spinlock_t mixer_lock;
	int mixer_volume[MIXER_ADDR_LAST+1][2];
	int capture_source[MIXER_ADDR_LAST+1][2];
	struct pci_dev *pci;
	struct saa7134_dev *saadev;

	unsigned long iobase;
	int irq;

	spinlock_t lock;
} snd_card_saa7134_t;

/*
 * PCM structure
 */

typedef struct snd_card_saa7134_pcm {
	struct saa7134_dev *saadev;

	spinlock_t lock;
	unsigned int pcm_size;		/* buffer size */
	unsigned int pcm_count;		/* bytes per period */
	unsigned int pcm_bps;		/* bytes per second */
	snd_pcm_substream_t *substream;
} snd_card_saa7134_pcm_t;

static snd_card_t *snd_saa7134_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

/*
 * saa7134 DMA audio stop
 *
 *   Called when the capture device is released or the buffer overflows
 *
 *   - Copied verbatim from saa7134-oss's dsp_dma_stop. Can be dropped
 *     if we just share dsp_dma_stop and use it here
 *
 */

static void saa7134_dma_stop(struct saa7134_dev *dev)

{
	dev->oss.dma_blk     = -1;
	dev->oss.dma_running = 0;
	saa7134_set_dmabits(dev);
}

/*
 * saa7134 DMA audio start
 *
 *   Called when preparing the capture device for use
 *
 *   - Copied verbatim from saa7134-oss's dsp_dma_start. Can be dropped
 *     if we just share dsp_dma_start and use it here
 *
 */

static void saa7134_dma_start(struct saa7134_dev *dev)
{
	dev->oss.dma_blk     = 0;
	dev->oss.dma_running = 1;
	saa7134_set_dmabits(dev);
}

/*
 * saa7134 audio DMA IRQ handler
 *
 *   Called whenever we get an SAA7134_IRQ_REPORT_DONE_RA3 interrupt
 *   Handles shifting between the 2 buffers, manages the read counters,
 *  and notifies ALSA when periods elapse
 *
 *   - Mostly copied from saa7134-oss's saa7134_irq_oss_done.
 *
 */

void saa7134_irq_alsa_done(struct saa7134_dev *dev, unsigned long status)
{
	int next_blk, reg = 0;

	spin_lock(&dev->slock);
	if (UNSET == dev->oss.dma_blk) {
		dprintk("irq: recording stopped\n");
		goto done;
	}
	if (0 != (status & 0x0f000000))
		dprintk("irq: lost %ld\n", (status >> 24) & 0x0f);
	if (0 == (status & 0x10000000)) {
		/* odd */
		if (0 == (dev->oss.dma_blk & 0x01))
			reg = SAA7134_RS_BA1(6);
	} else {
		/* even */
		if (1 == (dev->oss.dma_blk & 0x01))
			reg = SAA7134_RS_BA2(6);
	}
	if (0 == reg) {
		dprintk("irq: field oops [%s]\n",
			(status & 0x10000000) ? "even" : "odd");
		goto done;
	}

	if (dev->oss.read_count >= dev->oss.blksize * (dev->oss.blocks-2)) {
		dprintk("irq: overrun [full=%d/%d] - Blocks in %d\n",dev->oss.read_count,
			dev->oss.bufsize, dev->oss.blocks);
		saa7134_dma_stop(dev);
		goto done;
	}

	/* next block addr */
	next_blk = (dev->oss.dma_blk + 2) % dev->oss.blocks;
	saa_writel(reg,next_blk * dev->oss.blksize);
	if (alsa_debug > 2)
		dprintk("irq: ok, %s, next_blk=%d, addr=%x, blocks=%u, size=%u, read=%u\n",
			(status & 0x10000000) ? "even" : "odd ", next_blk,
			next_blk * dev->oss.blksize, dev->oss.blocks, dev->oss.blksize, dev->oss.read_count);


	/* update status & wake waiting readers */
	dev->oss.dma_blk = (dev->oss.dma_blk + 1) % dev->oss.blocks;
	dev->oss.read_count += dev->oss.blksize;

	dev->oss.recording_on = reg;

	if (dev->oss.read_count >= snd_pcm_lib_period_bytes(dev->oss.substream)) {
		spin_unlock(&dev->slock);
		snd_pcm_period_elapsed(dev->oss.substream);
		spin_lock(&dev->slock);
	}
 done:
	spin_unlock(&dev->slock);

}

/*
 * ALSA capture trigger
 *
 *   - One of the ALSA capture callbacks.
 *
 *   Called whenever a capture is started or stopped. Must be defined,
 *   but there's nothing we want to do here
 *
 */

static int snd_card_saa7134_capture_trigger(snd_pcm_substream_t * substream,
					  int cmd)
{
	return 0;
}

/*
 * DMA buffer config
 *
 *   Sets the values that will later be used as the size of the buffer,
 *  size of the fragments, and total number of fragments.
 *   Must be called during the preparation stage, before memory is
 *  allocated
 *
 *   - Copied verbatim from saa7134-oss. Can be dropped
 *     if we just share dsp_buffer_conf from OSS.
 */

static int dsp_buffer_conf(struct saa7134_dev *dev, int blksize, int blocks)
{
	if (blksize < 0x100)
		blksize = 0x100;
	if (blksize > 0x10000)
		blksize = 0x10000;

	if (blocks < 2)
		blocks = 2;
	if ((blksize * blocks) > 1024*1024)
		blocks = 1024*1024 / blksize;

	dev->oss.blocks  = blocks;
	dev->oss.blksize = blksize;
	dev->oss.bufsize = blksize * blocks;

	dprintk("buffer config: %d blocks / %d bytes, %d kB total\n",
		blocks,blksize,blksize * blocks / 1024);
	return 0;
}

/*
 * DMA buffer initialization
 *
 *   Uses V4L functions to initialize the DMA. Shouldn't be necessary in
 *  ALSA, but I was unable to use ALSA's own DMA, and had to force the
 *  usage of V4L's
 *
 *   - Copied verbatim from saa7134-oss. Can be dropped
 *     if we just share dsp_buffer_init from OSS.
 */

static int dsp_buffer_init(struct saa7134_dev *dev)
{
	int err;

	if (!dev->oss.bufsize)
		BUG();
	videobuf_dma_init(&dev->oss.dma);
	err = videobuf_dma_init_kernel(&dev->oss.dma, PCI_DMA_FROMDEVICE,
			               (dev->oss.bufsize + PAGE_SIZE) >> PAGE_SHIFT);
	if (0 != err)
		return err;
	return 0;
}

/*
 * ALSA PCM preparation
 *
 *   - One of the ALSA capture callbacks.
 *
 *   Called right after the capture device is opened, this function configures
 *  the buffer using the previously defined functions, allocates the memory,
 *  sets up the hardware registers, and then starts the DMA. When this function
 *  returns, the audio should be flowing.
 *
 */

static int snd_card_saa7134_capture_prepare(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err, bswap, sign;
	u32 fmt, control;
	unsigned long flags;
	snd_card_saa7134_t *saa7134 = snd_pcm_substream_chip(substream);
	struct saa7134_dev *dev;
	snd_card_saa7134_pcm_t *saapcm = runtime->private_data;
	unsigned int bps;
	unsigned long size;
	unsigned count;

	size = snd_pcm_lib_buffer_bytes(substream);
	count = snd_pcm_lib_period_bytes(substream);

	saapcm->saadev->oss.substream = substream;
	bps = runtime->rate * runtime->channels;
	bps *= snd_pcm_format_width(runtime->format);
	bps /= 8;
	if (bps <= 0)
		return -EINVAL;
	saapcm->pcm_bps = bps;
	saapcm->pcm_size = snd_pcm_lib_buffer_bytes(substream);
	saapcm->pcm_count = snd_pcm_lib_period_bytes(substream);


	dev=saa7134->saadev;

	dsp_buffer_conf(dev,saapcm->pcm_count,(saapcm->pcm_size/saapcm->pcm_count));

	err = dsp_buffer_init(dev);
	if (0 != err)
		goto fail2;

	/* prepare buffer */
	if (0 != (err = videobuf_dma_pci_map(dev->pci,&dev->oss.dma)))
		return err;
	if (0 != (err = saa7134_pgtable_alloc(dev->pci,&dev->oss.pt)))
		goto fail1;
	if (0 != (err = saa7134_pgtable_build(dev->pci,&dev->oss.pt,
			                      dev->oss.dma.sglist,
			                      dev->oss.dma.sglen,
			                      0)))
		goto fail2;



	switch (runtime->format) {
	  case SNDRV_PCM_FORMAT_U8:
	  case SNDRV_PCM_FORMAT_S8:
		fmt = 0x00;
		break;
	  case SNDRV_PCM_FORMAT_U16_LE:
	  case SNDRV_PCM_FORMAT_U16_BE:
	  case SNDRV_PCM_FORMAT_S16_LE:
	  case SNDRV_PCM_FORMAT_S16_BE:
		fmt = 0x01;
		break;
	  default:
		err = -EINVAL;
		return 1;
	}

	switch (runtime->format) {
	  case SNDRV_PCM_FORMAT_S8:
	  case SNDRV_PCM_FORMAT_S16_LE:
	  case SNDRV_PCM_FORMAT_S16_BE:
		sign = 1;
		break;
	  default:
		sign = 0;
		break;
	}

	switch (runtime->format) {
	  case SNDRV_PCM_FORMAT_U16_BE:
	  case SNDRV_PCM_FORMAT_S16_BE:
		bswap = 1; break;
	  default:
		bswap = 0; break;
	}

	switch (dev->pci->device) {
	  case PCI_DEVICE_ID_PHILIPS_SAA7134:
		if (1 == runtime->channels)
			fmt |= (1 << 3);
		if (2 == runtime->channels)
			fmt |= (3 << 3);
		if (sign)
			fmt |= 0x04;

		fmt |= (MIXER_ADDR_TVTUNER == dev->oss.input) ? 0xc0 : 0x80;
		saa_writeb(SAA7134_NUM_SAMPLES0, ((dev->oss.blksize - 1) & 0x0000ff));
		saa_writeb(SAA7134_NUM_SAMPLES1, ((dev->oss.blksize - 1) & 0x00ff00) >>  8);
		saa_writeb(SAA7134_NUM_SAMPLES2, ((dev->oss.blksize - 1) & 0xff0000) >> 16);
		saa_writeb(SAA7134_AUDIO_FORMAT_CTRL, fmt);

		break;
	  case PCI_DEVICE_ID_PHILIPS_SAA7133:
	  case PCI_DEVICE_ID_PHILIPS_SAA7135:
		if (1 == runtime->channels)
			fmt |= (1 << 4);
		if (2 == runtime->channels)
			fmt |= (2 << 4);
		if (!sign)
			fmt |= 0x04;
		saa_writel(SAA7133_NUM_SAMPLES, dev->oss.blksize -1);
		saa_writel(SAA7133_AUDIO_CHANNEL, 0x543210 | (fmt << 24));
		//saa_writel(SAA7133_AUDIO_CHANNEL, 0x543210);
		break;
	}

	dprintk("rec_start: afmt=%d ch=%d  =>  fmt=0x%x swap=%c\n",
		runtime->format, runtime->channels, fmt,
		bswap ? 'b' : '-');
	/* dma: setup channel 6 (= AUDIO) */
	control = SAA7134_RS_CONTROL_BURST_16 |
		SAA7134_RS_CONTROL_ME |
		(dev->oss.pt.dma >> 12);
	if (bswap)
		control |= SAA7134_RS_CONTROL_BSWAP;

	/* I should be able to use runtime->dma_addr in the control
	   byte, but it doesn't work. So I allocate the DMA using the
	   V4L functions, and force ALSA to use that as the DMA area */

	runtime->dma_area = dev->oss.dma.vmalloc;

	saa_writel(SAA7134_RS_BA1(6),0);
	saa_writel(SAA7134_RS_BA2(6),dev->oss.blksize);
	saa_writel(SAA7134_RS_PITCH(6),0);
	saa_writel(SAA7134_RS_CONTROL(6),control);

	dev->oss.rate = runtime->rate;
	/* start dma */
	spin_lock_irqsave(&dev->slock,flags);
	saa7134_dma_start(dev);
	spin_unlock_irqrestore(&dev->slock,flags);

	return 0;
 fail2:
	saa7134_pgtable_free(dev->pci,&dev->oss.pt);
 fail1:
	videobuf_dma_pci_unmap(dev->pci,&dev->oss.dma);
	return err;


}

/*
 * ALSA pointer fetching
 *
 *   - One of the ALSA capture callbacks.
 *
 *   Called whenever a period elapses, it must return the current hardware
 *  position of the buffer.
 *   Also resets the read counter used to prevent overruns
 *
 */

static snd_pcm_uframes_t snd_card_saa7134_capture_pointer(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_card_saa7134_pcm_t *saapcm = runtime->private_data;
	struct saa7134_dev *dev=saapcm->saadev;



	if (dev->oss.read_count) {
		dev->oss.read_count  -= snd_pcm_lib_period_bytes(substream);
		dev->oss.read_offset += snd_pcm_lib_period_bytes(substream);
		if (dev->oss.read_offset == dev->oss.bufsize)
			dev->oss.read_offset = 0;
	}

	return bytes_to_frames(runtime, dev->oss.read_offset);
}

/*
 * ALSA hardware capabilities definition
 */

static snd_pcm_hardware_t snd_card_saa7134_capture =
{
	.info =                 (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
			         SNDRV_PCM_INFO_BLOCK_TRANSFER |
			         SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		USE_FORMATS,
	.rates =		USE_RATE,
	.rate_min =		USE_RATE_MIN,
	.rate_max =		USE_RATE_MAX,
	.channels_min =		USE_CHANNELS_MIN,
	.channels_max =		USE_CHANNELS_MAX,
	.buffer_bytes_max =	MAX_BUFFER_SIZE,
	.period_bytes_min =	64,
	.period_bytes_max =	MAX_BUFFER_SIZE,
	.periods_min =		USE_PERIODS_MIN,
	.periods_max =		USE_PERIODS_MAX,
	.fifo_size =		0x08070503,
};

static void snd_card_saa7134_runtime_free(snd_pcm_runtime_t *runtime)
{
	snd_card_saa7134_pcm_t *saapcm = runtime->private_data;

	kfree(saapcm);
}


/*
 * ALSA hardware params
 *
 *   - One of the ALSA capture callbacks.
 *
 *   Called on initialization, right before the PCM preparation
 *   Usually used in ALSA to allocate the DMA, but since we don't use the
 *  ALSA DMA I'm almost sure this isn't necessary.
 *
 */

static int snd_card_saa7134_hw_params(snd_pcm_substream_t * substream,
				    snd_pcm_hw_params_t * hw_params)
{

	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));


}

/*
 * ALSA hardware release
 *
 *   - One of the ALSA capture callbacks.
 *
 *   Called after closing the device, but before snd_card_saa7134_capture_close
 *   Usually used in ALSA to free the DMA, but since we don't use the
 *  ALSA DMA I'm almost sure this isn't necessary.
 *
 */

static int snd_card_saa7134_hw_free(snd_pcm_substream_t * substream)
{
	return snd_pcm_lib_free_pages(substream);
}

/*
 * DMA buffer release
 *
 *   Called after closing the device, during snd_card_saa7134_capture_close
 *
 */

static int dsp_buffer_free(struct saa7134_dev *dev)
{
	if (!dev->oss.blksize)
		BUG();

	videobuf_dma_free(&dev->oss.dma);

	dev->oss.blocks  = 0;
	dev->oss.blksize = 0;
	dev->oss.bufsize = 0;

	return 0;
}

/*
 * ALSA capture finish
 *
 *   - One of the ALSA capture callbacks.
 *
 *   Called after closing the device. It stops the DMA audio and releases
 *  the buffers
 *
 */

static int snd_card_saa7134_capture_close(snd_pcm_substream_t * substream)
{
	snd_card_saa7134_t *chip = snd_pcm_substream_chip(substream);
	struct saa7134_dev *dev = chip->saadev;
	unsigned long flags;

	/* stop dma */
	spin_lock_irqsave(&dev->slock,flags);
	saa7134_dma_stop(dev);
	spin_unlock_irqrestore(&dev->slock,flags);

	/* unlock buffer */
	saa7134_pgtable_free(dev->pci,&dev->oss.pt);
	videobuf_dma_pci_unmap(dev->pci,&dev->oss.dma);

	dsp_buffer_free(dev);
	return 0;
}

/*
 * ALSA capture start
 *
 *   - One of the ALSA capture callbacks.
 *
 *   Called when opening the device. It creates and populates the PCM
 *  structure
 *
 */

static int snd_card_saa7134_capture_open(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_card_saa7134_pcm_t *saapcm;
	snd_card_saa7134_t *saa7134 = snd_pcm_substream_chip(substream);
	struct saa7134_dev *dev = saa7134->saadev;
	int err;

	down(&dev->oss.lock);

	dev->oss.afmt        = SNDRV_PCM_FORMAT_U8;
	dev->oss.channels    = 2;
	dev->oss.read_count  = 0;
	dev->oss.read_offset = 0;

	up(&dev->oss.lock);

	saapcm = kcalloc(1, sizeof(*saapcm), GFP_KERNEL);
	if (saapcm == NULL)
		return -ENOMEM;
	saapcm->saadev=saa7134->saadev;

	spin_lock_init(&saapcm->lock);

	saapcm->substream = substream;
	runtime->private_data = saapcm;
	runtime->private_free = snd_card_saa7134_runtime_free;
	runtime->hw = snd_card_saa7134_capture;

	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;

	return 0;
}

/*
 * ALSA capture callbacks definition
 */

static snd_pcm_ops_t snd_card_saa7134_capture_ops = {
	.open =			snd_card_saa7134_capture_open,
	.close =		snd_card_saa7134_capture_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_card_saa7134_hw_params,
	.hw_free =		snd_card_saa7134_hw_free,
	.prepare =		snd_card_saa7134_capture_prepare,
	.trigger =		snd_card_saa7134_capture_trigger,
	.pointer =		snd_card_saa7134_capture_pointer,
};

/*
 * ALSA PCM setup
 *
 *   Called when initializing the board. Sets up the name and hooks up
 *  the callbacks
 *
 */

static int snd_card_saa7134_pcm(snd_card_saa7134_t *saa7134, int device)
{
	snd_pcm_t *pcm;
	int err;

	if ((err = snd_pcm_new(saa7134->card, "SAA7134 PCM", device, 0, 1, &pcm)) < 0)
		return err;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_card_saa7134_capture_ops);
	pcm->private_data = saa7134;
	pcm->info_flags = 0;
	strcpy(pcm->name, "SAA7134 PCM");
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(saa7134->pci),
					      128*1024, 256*1024);
	return 0;
}

#define SAA713x_VOLUME(xname, xindex, addr) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_saa7134_volume_info, \
  .get = snd_saa7134_volume_get, .put = snd_saa7134_volume_put, \
  .private_value = addr }

static int snd_saa7134_volume_info(snd_kcontrol_t * kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 20;
	return 0;
}

static int snd_saa7134_volume_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	snd_card_saa7134_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int addr = kcontrol->private_value;

	spin_lock_irqsave(&chip->mixer_lock, flags);
	ucontrol->value.integer.value[0] = chip->mixer_volume[addr][0];
	ucontrol->value.integer.value[1] = chip->mixer_volume[addr][1];
	spin_unlock_irqrestore(&chip->mixer_lock, flags);
	return 0;
}

static int snd_saa7134_volume_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	snd_card_saa7134_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
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
	spin_lock_irqsave(&chip->mixer_lock, flags);
	change = chip->mixer_volume[addr][0] != left ||
		 chip->mixer_volume[addr][1] != right;
	chip->mixer_volume[addr][0] = left;
	chip->mixer_volume[addr][1] = right;
	spin_unlock_irqrestore(&chip->mixer_lock, flags);
	return change;
}

#define SAA713x_CAPSRC(xname, xindex, addr) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_saa7134_capsrc_info, \
  .get = snd_saa7134_capsrc_get, .put = snd_saa7134_capsrc_put, \
  .private_value = addr }

static int snd_saa7134_capsrc_info(snd_kcontrol_t * kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_saa7134_capsrc_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	snd_card_saa7134_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int addr = kcontrol->private_value;

	spin_lock_irqsave(&chip->mixer_lock, flags);
	ucontrol->value.integer.value[0] = chip->capture_source[addr][0];
	ucontrol->value.integer.value[1] = chip->capture_source[addr][1];
	spin_unlock_irqrestore(&chip->mixer_lock, flags);
	return 0;
}

static int snd_saa7134_capsrc_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	snd_card_saa7134_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change, addr = kcontrol->private_value;
	int left, right;
	u32 anabar, xbarin;
	int analog_io, rate;
	struct saa7134_dev *dev;

	dev = chip->saadev;

	left = ucontrol->value.integer.value[0] & 1;
	right = ucontrol->value.integer.value[1] & 1;
	spin_lock_irqsave(&chip->mixer_lock, flags);

	change = chip->capture_source[addr][0] != left ||
		 chip->capture_source[addr][1] != right;
	chip->capture_source[addr][0] = left;
	chip->capture_source[addr][1] = right;
	dev->oss.input=addr;
	spin_unlock_irqrestore(&chip->mixer_lock, flags);


	if (change) {
	  switch (dev->pci->device) {

	   case PCI_DEVICE_ID_PHILIPS_SAA7134:
		switch (addr) {
			case MIXER_ADDR_TVTUNER:
				saa_andorb(SAA7134_AUDIO_FORMAT_CTRL, 0xc0, 0xc0);
				saa_andorb(SAA7134_SIF_SAMPLE_FREQ,   0x03, 0x00);
				break;
			case MIXER_ADDR_LINE1:
			case MIXER_ADDR_LINE2:
				analog_io = (MIXER_ADDR_LINE1 == addr) ? 0x00 : 0x08;
				rate = (32000 == dev->oss.rate) ? 0x01 : 0x03;
				saa_andorb(SAA7134_ANALOG_IO_SELECT,  0x08, analog_io);
				saa_andorb(SAA7134_AUDIO_FORMAT_CTRL, 0xc0, 0x80);
				saa_andorb(SAA7134_SIF_SAMPLE_FREQ,   0x03, rate);
				break;
		}

		break;
	   case PCI_DEVICE_ID_PHILIPS_SAA7133:
	   case PCI_DEVICE_ID_PHILIPS_SAA7135:
		xbarin = 0x03; // adc
		anabar = 0;
		switch (addr) {
			case MIXER_ADDR_TVTUNER:
				xbarin = 0; // Demodulator
				anabar = 2; // DACs
				break;
			case MIXER_ADDR_LINE1:
				anabar = 0;  // aux1, aux1
				break;
			case MIXER_ADDR_LINE2:
				anabar = 9;  // aux2, aux2
				break;
		}

	    	/* output xbar always main channel */
		saa_dsp_writel(dev, SAA7133_DIGITAL_OUTPUT_SEL1, 0xbbbb10);

		if (left || right) { // We've got data, turn the input on
		  //saa_dsp_writel(dev, SAA7133_DIGITAL_OUTPUT_SEL2, 0x101010);
		  saa_dsp_writel(dev, SAA7133_DIGITAL_INPUT_XBAR1, xbarin);
		  saa_writel(SAA7133_ANALOG_IO_SELECT, anabar);
		} else {
		  //saa_dsp_writel(dev, SAA7133_DIGITAL_OUTPUT_SEL2, 0x101010);
		  saa_dsp_writel(dev, SAA7133_DIGITAL_INPUT_XBAR1, 0);
		  saa_writel(SAA7133_ANALOG_IO_SELECT, 0);
		}
		break;
	  }
	}

	return change;
}

static snd_kcontrol_new_t snd_saa7134_controls[] = {
SAA713x_VOLUME("Video Volume", 0, MIXER_ADDR_TVTUNER),
SAA713x_CAPSRC("Video Capture Switch", 0, MIXER_ADDR_TVTUNER),
SAA713x_VOLUME("Line Volume", 1, MIXER_ADDR_LINE1),
SAA713x_CAPSRC("Line Capture Switch", 1, MIXER_ADDR_LINE1),
SAA713x_VOLUME("Line Volume", 2, MIXER_ADDR_LINE2),
SAA713x_CAPSRC("Line Capture Switch", 2, MIXER_ADDR_LINE2),
};

/*
 * ALSA mixer setup
 *
 *   Called when initializing the board. Sets up the name and hooks up
 *  the callbacks
 *
 */

static int snd_card_saa7134_new_mixer(snd_card_saa7134_t * chip)
{
	snd_card_t *card = chip->card;
	unsigned int idx;
	int err;

	snd_assert(chip != NULL, return -EINVAL);
	strcpy(card->mixername, "SAA7134 Mixer");

	for (idx = 0; idx < ARRAY_SIZE(snd_saa7134_controls); idx++) {
		if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_saa7134_controls[idx], chip))) < 0)
			return err;
	}
	return 0;
}

static int snd_saa7134_free(snd_card_saa7134_t *chip)
{
	return 0;
}

static int snd_saa7134_dev_free(snd_device_t *device)
{
	snd_card_saa7134_t *chip = device->device_data;
	return snd_saa7134_free(chip);
}

/*
 * ALSA initialization
 *
 *   Called by saa7134-core, it creates the basic structures and registers
 *  the ALSA devices
 *
 */

int alsa_card_saa7134_create(struct saa7134_dev *saadev, unsigned int devicenum)
{
	static int dev;
	snd_card_t *card;
	snd_card_saa7134_t *chip;
	int err;
	static snd_device_ops_t ops = {
		.dev_free =     snd_saa7134_dev_free,
	};

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev])
		return -ENODEV;

	if (devicenum) {
		card = snd_card_new(devicenum, id[dev], THIS_MODULE, 0);
	} else {
		card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	}
	if (card == NULL)
		return -ENOMEM;

	strcpy(card->driver, "SAA7134");

	/* Card "creation" */

	chip = kcalloc(1, sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
		return -ENOMEM;
	}

	spin_lock_init(&chip->lock);

	chip->saadev = saadev;

	chip->card = card;

	chip->pci = saadev->pci;
	chip->irq = saadev->pci->irq;
	chip->iobase = pci_resource_start(saadev->pci, 0);

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_saa7134_free(chip);
		return err;
	}

	if ((err = snd_card_saa7134_new_mixer(chip)) < 0)
		goto __nodev;

	if ((err = snd_card_saa7134_pcm(chip, 0)) < 0)
		goto __nodev;

	spin_lock_init(&chip->mixer_lock);

	snd_card_set_dev(card, &chip->pci->dev);

	/* End of "creation" */

	strcpy(card->shortname, "SAA7134");
	sprintf(card->longname, "%s at 0x%lx irq %d",
		chip->saadev->name, chip->iobase, chip->irq);

	if ((err = snd_card_register(card)) == 0) {
		snd_saa7134_cards[dev] = card;
		return 0;
	}

__nodev:
	snd_card_free(card);
	kfree(card);
	return err;
}

void alsa_card_saa7134_exit(void)
{
	int idx;
	 for (idx = 0; idx < SNDRV_CARDS; idx++) {
		snd_card_free(snd_saa7134_cards[idx]);
	}
}
