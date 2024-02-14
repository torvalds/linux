// SPDX-License-Identifier: GPL-2.0-only
/*
 *   SAA713x ALSA support for V4L
 */

#include "saa7134.h"
#include "saa7134-reg.h"

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>

/*
 * Configuration macros
 */

/* defaults */
#define MIXER_ADDR_UNSELECTED	-1
#define MIXER_ADDR_TVTUNER	0
#define MIXER_ADDR_LINE1	1
#define MIXER_ADDR_LINE2	2
#define MIXER_ADDR_LAST		2


static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = {1, [1 ... (SNDRV_CARDS - 1)] = 1};

module_param_array(index, int, NULL, 0444);
module_param_array(enable, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for SAA7134 capture interface(s).");
MODULE_PARM_DESC(enable, "Enable (or not) the SAA7134 capture interface(s).");

/*
 * Main chip structure
 */

typedef struct snd_card_saa7134 {
	struct snd_card *card;
	spinlock_t mixer_lock;
	int mixer_volume[MIXER_ADDR_LAST+1][2];
	int capture_source_addr;
	int capture_source[2];
	struct snd_kcontrol *capture_ctl[MIXER_ADDR_LAST+1];
	struct pci_dev *pci;
	struct saa7134_dev *dev;

	unsigned long iobase;
	s16 irq;
	u16 mute_was_on;

	spinlock_t lock;
} snd_card_saa7134_t;


/*
 * PCM structure
 */

typedef struct snd_card_saa7134_pcm {
	struct saa7134_dev *dev;

	spinlock_t lock;

	struct snd_pcm_substream *substream;
} snd_card_saa7134_pcm_t;

static struct snd_card *snd_saa7134_cards[SNDRV_CARDS];


/*
 * saa7134 DMA audio stop
 *
 *   Called when the capture device is released or the buffer overflows
 *
 *   - Copied verbatim from saa7134-oss's dsp_dma_stop.
 *
 */

static void saa7134_dma_stop(struct saa7134_dev *dev)
{
	dev->dmasound.dma_blk     = -1;
	dev->dmasound.dma_running = 0;
	saa7134_set_dmabits(dev);
}

/*
 * saa7134 DMA audio start
 *
 *   Called when preparing the capture device for use
 *
 *   - Copied verbatim from saa7134-oss's dsp_dma_start.
 *
 */

static void saa7134_dma_start(struct saa7134_dev *dev)
{
	dev->dmasound.dma_blk     = 0;
	dev->dmasound.dma_running = 1;
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

static void saa7134_irq_alsa_done(struct saa7134_dev *dev,
				  unsigned long status)
{
	int next_blk, reg = 0;

	spin_lock(&dev->slock);
	if (UNSET == dev->dmasound.dma_blk) {
		pr_debug("irq: recording stopped\n");
		goto done;
	}
	if (0 != (status & 0x0f000000))
		pr_debug("irq: lost %ld\n", (status >> 24) & 0x0f);
	if (0 == (status & 0x10000000)) {
		/* odd */
		if (0 == (dev->dmasound.dma_blk & 0x01))
			reg = SAA7134_RS_BA1(6);
	} else {
		/* even */
		if (1 == (dev->dmasound.dma_blk & 0x01))
			reg = SAA7134_RS_BA2(6);
	}
	if (0 == reg) {
		pr_debug("irq: field oops [%s]\n",
			(status & 0x10000000) ? "even" : "odd");
		goto done;
	}

	if (dev->dmasound.read_count >= dev->dmasound.blksize * (dev->dmasound.blocks-2)) {
		pr_debug("irq: overrun [full=%d/%d] - Blocks in %d\n",
			dev->dmasound.read_count,
			dev->dmasound.bufsize, dev->dmasound.blocks);
		spin_unlock(&dev->slock);
		snd_pcm_stop_xrun(dev->dmasound.substream);
		return;
	}

	/* next block addr */
	next_blk = (dev->dmasound.dma_blk + 2) % dev->dmasound.blocks;
	saa_writel(reg,next_blk * dev->dmasound.blksize);
	pr_debug("irq: ok, %s, next_blk=%d, addr=%x, blocks=%u, size=%u, read=%u\n",
		(status & 0x10000000) ? "even" : "odd ", next_blk,
		 next_blk * dev->dmasound.blksize, dev->dmasound.blocks,
		 dev->dmasound.blksize, dev->dmasound.read_count);

	/* update status & wake waiting readers */
	dev->dmasound.dma_blk = (dev->dmasound.dma_blk + 1) % dev->dmasound.blocks;
	dev->dmasound.read_count += dev->dmasound.blksize;

	dev->dmasound.recording_on = reg;

	if (dev->dmasound.read_count >= snd_pcm_lib_period_bytes(dev->dmasound.substream)) {
		spin_unlock(&dev->slock);
		snd_pcm_period_elapsed(dev->dmasound.substream);
		spin_lock(&dev->slock);
	}

 done:
	spin_unlock(&dev->slock);

}

/*
 * IRQ request handler
 *
 *   Runs along with saa7134's IRQ handler, discards anything that isn't
 *   DMA sound
 *
 */

static irqreturn_t saa7134_alsa_irq(int irq, void *dev_id)
{
	struct saa7134_dmasound *dmasound = dev_id;
	struct saa7134_dev *dev = dmasound->priv_data;

	unsigned long report, status;
	int loop, handled = 0;

	for (loop = 0; loop < 10; loop++) {
		report = saa_readl(SAA7134_IRQ_REPORT);
		status = saa_readl(SAA7134_IRQ_STATUS);

		if (report & SAA7134_IRQ_REPORT_DONE_RA3) {
			handled = 1;
			saa_writel(SAA7134_IRQ_REPORT,
				   SAA7134_IRQ_REPORT_DONE_RA3);
			saa7134_irq_alsa_done(dev, status);
		} else {
			goto out;
		}
	}

	if (loop == 10) {
		pr_debug("error! looping IRQ!");
	}

out:
	return IRQ_RETVAL(handled);
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

static int snd_card_saa7134_capture_trigger(struct snd_pcm_substream * substream,
					  int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_card_saa7134_pcm_t *pcm = runtime->private_data;
	struct saa7134_dev *dev=pcm->dev;
	int err = 0;

	spin_lock(&dev->slock);
	if (cmd == SNDRV_PCM_TRIGGER_START) {
		/* start dma */
		saa7134_dma_start(dev);
	} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		/* stop dma */
		saa7134_dma_stop(dev);
	} else {
		err = -EINVAL;
	}
	spin_unlock(&dev->slock);

	return err;
}

static int saa7134_alsa_dma_init(struct saa7134_dev *dev,
				 unsigned long nr_pages)
{
	struct saa7134_dmasound *dma = &dev->dmasound;
	struct page *pg;
	int i;

	dma->vaddr = vmalloc_32(nr_pages << PAGE_SHIFT);
	if (NULL == dma->vaddr) {
		pr_debug("vmalloc_32(%lu pages) failed\n", nr_pages);
		return -ENOMEM;
	}

	pr_debug("vmalloc is at addr %p, size=%lu\n",
		 dma->vaddr, nr_pages << PAGE_SHIFT);

	memset(dma->vaddr, 0, nr_pages << PAGE_SHIFT);
	dma->nr_pages = nr_pages;

	dma->sglist = vzalloc(array_size(sizeof(*dma->sglist), dma->nr_pages));
	if (NULL == dma->sglist)
		goto vzalloc_err;

	sg_init_table(dma->sglist, dma->nr_pages);
	for (i = 0; i < dma->nr_pages; i++) {
		pg = vmalloc_to_page(dma->vaddr + i * PAGE_SIZE);
		if (NULL == pg)
			goto vmalloc_to_page_err;
		sg_set_page(&dma->sglist[i], pg, PAGE_SIZE, 0);
	}
	return 0;

vmalloc_to_page_err:
	vfree(dma->sglist);
	dma->sglist = NULL;
vzalloc_err:
	vfree(dma->vaddr);
	dma->vaddr = NULL;
	return -ENOMEM;
}

static int saa7134_alsa_dma_map(struct saa7134_dev *dev)
{
	struct saa7134_dmasound *dma = &dev->dmasound;

	dma->sglen = dma_map_sg(&dev->pci->dev, dma->sglist,
			dma->nr_pages, DMA_FROM_DEVICE);

	if (0 == dma->sglen) {
		pr_warn("%s: saa7134_alsa_map_sg failed\n", __func__);
		return -ENOMEM;
	}
	return 0;
}

static int saa7134_alsa_dma_unmap(struct saa7134_dev *dev)
{
	struct saa7134_dmasound *dma = &dev->dmasound;

	if (!dma->sglen)
		return 0;

	dma_unmap_sg(&dev->pci->dev, dma->sglist, dma->nr_pages, DMA_FROM_DEVICE);
	dma->sglen = 0;
	return 0;
}

static int saa7134_alsa_dma_free(struct saa7134_dmasound *dma)
{
	vfree(dma->sglist);
	dma->sglist = NULL;
	vfree(dma->vaddr);
	dma->vaddr = NULL;
	return 0;
}

/*
 * DMA buffer initialization
 *
 *   Uses V4L functions to initialize the DMA. Shouldn't be necessary in
 *  ALSA, but I was unable to use ALSA's own DMA, and had to force the
 *  usage of V4L's
 *
 *   - Copied verbatim from saa7134-oss.
 *
 */

static int dsp_buffer_init(struct saa7134_dev *dev)
{
	int err;

	BUG_ON(!dev->dmasound.bufsize);

	err = saa7134_alsa_dma_init(dev,
			       (dev->dmasound.bufsize + PAGE_SIZE) >> PAGE_SHIFT);
	if (0 != err)
		return err;
	return 0;
}

/*
 * DMA buffer release
 *
 *   Called after closing the device, during snd_card_saa7134_capture_close
 *
 */

static int dsp_buffer_free(struct saa7134_dev *dev)
{
	BUG_ON(!dev->dmasound.blksize);

	saa7134_alsa_dma_free(&dev->dmasound);

	dev->dmasound.blocks  = 0;
	dev->dmasound.blksize = 0;
	dev->dmasound.bufsize = 0;

	return 0;
}

/*
 * Setting the capture source and updating the ALSA controls
 */
static int snd_saa7134_capsrc_set(struct snd_kcontrol *kcontrol,
				  int left, int right, bool force_notify)
{
	snd_card_saa7134_t *chip = snd_kcontrol_chip(kcontrol);
	int change = 0, addr = kcontrol->private_value;
	int active, old_addr;
	u32 anabar, xbarin;
	int analog_io, rate;
	struct saa7134_dev *dev;

	dev = chip->dev;

	spin_lock_irq(&chip->mixer_lock);

	active = left != 0 || right != 0;
	old_addr = chip->capture_source_addr;

	/* The active capture source cannot be deactivated */
	if (active) {
		change = old_addr != addr ||
			 chip->capture_source[0] != left ||
			 chip->capture_source[1] != right;

		chip->capture_source[0] = left;
		chip->capture_source[1] = right;
		chip->capture_source_addr = addr;
		dev->dmasound.input = addr;
	}
	spin_unlock_irq(&chip->mixer_lock);

	if (change) {
		switch (dev->pci->device) {

		case PCI_DEVICE_ID_PHILIPS_SAA7134:
			switch (addr) {
			case MIXER_ADDR_TVTUNER:
				saa_andorb(SAA7134_AUDIO_FORMAT_CTRL,
					   0xc0, 0xc0);
				saa_andorb(SAA7134_SIF_SAMPLE_FREQ,
					   0x03, 0x00);
				break;
			case MIXER_ADDR_LINE1:
			case MIXER_ADDR_LINE2:
				analog_io = (MIXER_ADDR_LINE1 == addr) ?
					     0x00 : 0x08;
				rate = (32000 == dev->dmasound.rate) ?
					0x01 : 0x03;
				saa_andorb(SAA7134_ANALOG_IO_SELECT,
					   0x08, analog_io);
				saa_andorb(SAA7134_AUDIO_FORMAT_CTRL,
					   0xc0, 0x80);
				saa_andorb(SAA7134_SIF_SAMPLE_FREQ,
					   0x03, rate);
				break;
			}

			break;
		case PCI_DEVICE_ID_PHILIPS_SAA7133:
		case PCI_DEVICE_ID_PHILIPS_SAA7135:
			xbarin = 0x03; /* adc */
			anabar = 0;
			switch (addr) {
			case MIXER_ADDR_TVTUNER:
				xbarin = 0; /* Demodulator */
				anabar = 2; /* DACs */
				break;
			case MIXER_ADDR_LINE1:
				anabar = 0;  /* aux1, aux1 */
				break;
			case MIXER_ADDR_LINE2:
				anabar = 9;  /* aux2, aux2 */
				break;
			}

			/* output xbar always main channel */
			saa_dsp_writel(dev, SAA7133_DIGITAL_OUTPUT_SEL1,
				       0xbbbb10);

			if (left || right) {
				/* We've got data, turn the input on */
				saa_dsp_writel(dev, SAA7133_DIGITAL_INPUT_XBAR1,
					       xbarin);
				saa_writel(SAA7133_ANALOG_IO_SELECT, anabar);
			} else {
				saa_dsp_writel(dev, SAA7133_DIGITAL_INPUT_XBAR1,
					       0);
				saa_writel(SAA7133_ANALOG_IO_SELECT, 0);
			}
			break;
		}
	}

	if (change) {
		if (force_notify)
			snd_ctl_notify(chip->card,
				       SNDRV_CTL_EVENT_MASK_VALUE,
				       &chip->capture_ctl[addr]->id);

		if (old_addr != MIXER_ADDR_UNSELECTED && old_addr != addr)
			snd_ctl_notify(chip->card,
				       SNDRV_CTL_EVENT_MASK_VALUE,
				       &chip->capture_ctl[old_addr]->id);
	}

	return change;
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

static int snd_card_saa7134_capture_prepare(struct snd_pcm_substream * substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int bswap, sign;
	u32 fmt, control;
	snd_card_saa7134_t *saa7134 = snd_pcm_substream_chip(substream);
	struct saa7134_dev *dev;
	snd_card_saa7134_pcm_t *pcm = runtime->private_data;

	pcm->dev->dmasound.substream = substream;

	dev = saa7134->dev;

	if (snd_pcm_format_width(runtime->format) == 8)
		fmt = 0x00;
	else
		fmt = 0x01;

	if (snd_pcm_format_signed(runtime->format))
		sign = 1;
	else
		sign = 0;

	if (snd_pcm_format_big_endian(runtime->format))
		bswap = 1;
	else
		bswap = 0;

	switch (dev->pci->device) {
	  case PCI_DEVICE_ID_PHILIPS_SAA7134:
		if (1 == runtime->channels)
			fmt |= (1 << 3);
		if (2 == runtime->channels)
			fmt |= (3 << 3);
		if (sign)
			fmt |= 0x04;

		fmt |= (MIXER_ADDR_TVTUNER == dev->dmasound.input) ? 0xc0 : 0x80;
		saa_writeb(SAA7134_NUM_SAMPLES0, ((dev->dmasound.blksize - 1) & 0x0000ff));
		saa_writeb(SAA7134_NUM_SAMPLES1, ((dev->dmasound.blksize - 1) & 0x00ff00) >>  8);
		saa_writeb(SAA7134_NUM_SAMPLES2, ((dev->dmasound.blksize - 1) & 0xff0000) >> 16);
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
		saa_writel(SAA7133_NUM_SAMPLES, dev->dmasound.blksize -1);
		saa_writel(SAA7133_AUDIO_CHANNEL, 0x543210 | (fmt << 24));
		break;
	}

	pr_debug("rec_start: afmt=%d ch=%d  =>  fmt=0x%x swap=%c\n",
		runtime->format, runtime->channels, fmt,
		bswap ? 'b' : '-');
	/* dma: setup channel 6 (= AUDIO) */
	control = SAA7134_RS_CONTROL_BURST_16 |
		SAA7134_RS_CONTROL_ME |
		(dev->dmasound.pt.dma >> 12);
	if (bswap)
		control |= SAA7134_RS_CONTROL_BSWAP;

	saa_writel(SAA7134_RS_BA1(6),0);
	saa_writel(SAA7134_RS_BA2(6),dev->dmasound.blksize);
	saa_writel(SAA7134_RS_PITCH(6),0);
	saa_writel(SAA7134_RS_CONTROL(6),control);

	dev->dmasound.rate = runtime->rate;

	/* Setup and update the card/ALSA controls */
	snd_saa7134_capsrc_set(saa7134->capture_ctl[dev->dmasound.input], 1, 1,
			       true);

	return 0;

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

static snd_pcm_uframes_t
snd_card_saa7134_capture_pointer(struct snd_pcm_substream * substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_card_saa7134_pcm_t *pcm = runtime->private_data;
	struct saa7134_dev *dev=pcm->dev;

	if (dev->dmasound.read_count) {
		dev->dmasound.read_count  -= snd_pcm_lib_period_bytes(substream);
		dev->dmasound.read_offset += snd_pcm_lib_period_bytes(substream);
		if (dev->dmasound.read_offset == dev->dmasound.bufsize)
			dev->dmasound.read_offset = 0;
	}

	return bytes_to_frames(runtime, dev->dmasound.read_offset);
}

/*
 * ALSA hardware capabilities definition
 *
 *  Report only 32kHz for ALSA:
 *
 *  - SAA7133/35 uses DDEP (DemDec Easy Programming mode), which works in 32kHz
 *    only
 *  - SAA7134 for TV mode uses DemDec mode (32kHz)
 *  - Radio works in 32kHz only
 *  - When recording 48kHz from Line1/Line2, switching of capture source to TV
 *    means
 *    switching to 32kHz without any frequency translation
 */

static const struct snd_pcm_hardware snd_card_saa7134_capture =
{
	.info =                 (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE | \
				SNDRV_PCM_FMTBIT_S16_BE | \
				SNDRV_PCM_FMTBIT_S8 | \
				SNDRV_PCM_FMTBIT_U8 | \
				SNDRV_PCM_FMTBIT_U16_LE | \
				SNDRV_PCM_FMTBIT_U16_BE,
	.rates =		SNDRV_PCM_RATE_32000,
	.rate_min =		32000,
	.rate_max =		32000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(256*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(256*1024),
	.periods_min =		4,
	.periods_max =		1024,
};

static void snd_card_saa7134_runtime_free(struct snd_pcm_runtime *runtime)
{
	snd_card_saa7134_pcm_t *pcm = runtime->private_data;

	kfree(pcm);
}


/*
 * ALSA hardware params
 *
 *   - One of the ALSA capture callbacks.
 *
 *   Called on initialization, right before the PCM preparation
 *
 */

static int snd_card_saa7134_hw_params(struct snd_pcm_substream * substream,
				      struct snd_pcm_hw_params * hw_params)
{
	snd_card_saa7134_t *saa7134 = snd_pcm_substream_chip(substream);
	struct saa7134_dev *dev;
	unsigned int period_size, periods;
	int err;

	period_size = params_period_bytes(hw_params);
	periods = params_periods(hw_params);

	if (period_size < 0x100 || period_size > 0x10000)
		return -EINVAL;
	if (periods < 4)
		return -EINVAL;
	if (period_size * periods > 1024 * 1024)
		return -EINVAL;

	dev = saa7134->dev;

	if (dev->dmasound.blocks == periods &&
	    dev->dmasound.blksize == period_size)
		return 0;

	/* release the old buffer */
	if (substream->runtime->dma_area) {
		saa7134_pgtable_free(dev->pci, &dev->dmasound.pt);
		saa7134_alsa_dma_unmap(dev);
		dsp_buffer_free(dev);
		substream->runtime->dma_area = NULL;
	}
	dev->dmasound.blocks  = periods;
	dev->dmasound.blksize = period_size;
	dev->dmasound.bufsize = period_size * periods;

	err = dsp_buffer_init(dev);
	if (0 != err) {
		dev->dmasound.blocks  = 0;
		dev->dmasound.blksize = 0;
		dev->dmasound.bufsize = 0;
		return err;
	}

	err = saa7134_alsa_dma_map(dev);
	if (err) {
		dsp_buffer_free(dev);
		return err;
	}
	err = saa7134_pgtable_alloc(dev->pci, &dev->dmasound.pt);
	if (err) {
		saa7134_alsa_dma_unmap(dev);
		dsp_buffer_free(dev);
		return err;
	}
	err = saa7134_pgtable_build(dev->pci, &dev->dmasound.pt,
				dev->dmasound.sglist, dev->dmasound.sglen, 0);
	if (err) {
		saa7134_pgtable_free(dev->pci, &dev->dmasound.pt);
		saa7134_alsa_dma_unmap(dev);
		dsp_buffer_free(dev);
		return err;
	}

	/* I should be able to use runtime->dma_addr in the control
	   byte, but it doesn't work. So I allocate the DMA using the
	   V4L functions, and force ALSA to use that as the DMA area */

	substream->runtime->dma_area = dev->dmasound.vaddr;
	substream->runtime->dma_bytes = dev->dmasound.bufsize;
	substream->runtime->dma_addr = 0;

	return 0;

}

/*
 * ALSA hardware release
 *
 *   - One of the ALSA capture callbacks.
 *
 *   Called after closing the device, but before snd_card_saa7134_capture_close
 *   It stops the DMA audio and releases the buffers.
 *
 */

static int snd_card_saa7134_hw_free(struct snd_pcm_substream * substream)
{
	snd_card_saa7134_t *saa7134 = snd_pcm_substream_chip(substream);
	struct saa7134_dev *dev;

	dev = saa7134->dev;

	if (substream->runtime->dma_area) {
		saa7134_pgtable_free(dev->pci, &dev->dmasound.pt);
		saa7134_alsa_dma_unmap(dev);
		dsp_buffer_free(dev);
		substream->runtime->dma_area = NULL;
	}

	return 0;
}

/*
 * ALSA capture finish
 *
 *   - One of the ALSA capture callbacks.
 *
 *   Called after closing the device.
 *
 */

static int snd_card_saa7134_capture_close(struct snd_pcm_substream * substream)
{
	snd_card_saa7134_t *saa7134 = snd_pcm_substream_chip(substream);
	struct saa7134_dev *dev = saa7134->dev;

	if (saa7134->mute_was_on) {
		dev->ctl_mute = 1;
		saa7134_tvaudio_setmute(dev);
	}
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

static int snd_card_saa7134_capture_open(struct snd_pcm_substream * substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	snd_card_saa7134_pcm_t *pcm;
	snd_card_saa7134_t *saa7134 = snd_pcm_substream_chip(substream);
	struct saa7134_dev *dev;
	int amux, err;

	if (!saa7134) {
		pr_err("BUG: saa7134 can't find device struct. Can't proceed with open\n");
		return -ENODEV;
	}
	dev = saa7134->dev;
	mutex_lock(&dev->dmasound.lock);

	dev->dmasound.read_count  = 0;
	dev->dmasound.read_offset = 0;

	amux = dev->input->amux;
	if ((amux < 1) || (amux > 3))
		amux = 1;
	dev->dmasound.input  =  amux - 1;

	mutex_unlock(&dev->dmasound.lock);

	pcm = kzalloc(sizeof(*pcm), GFP_KERNEL);
	if (pcm == NULL)
		return -ENOMEM;

	pcm->dev=saa7134->dev;

	spin_lock_init(&pcm->lock);

	pcm->substream = substream;
	runtime->private_data = pcm;
	runtime->private_free = snd_card_saa7134_runtime_free;
	runtime->hw = snd_card_saa7134_capture;

	if (dev->ctl_mute != 0) {
		saa7134->mute_was_on = 1;
		dev->ctl_mute = 0;
		saa7134_tvaudio_setmute(dev);
	}

	err = snd_pcm_hw_constraint_integer(runtime,
						SNDRV_PCM_HW_PARAM_PERIODS);
	if (err < 0)
		return err;

	err = snd_pcm_hw_constraint_step(runtime, 0,
						SNDRV_PCM_HW_PARAM_PERIODS, 2);
	if (err < 0)
		return err;

	return 0;
}

/*
 * page callback (needed for mmap)
 */

static struct page *snd_card_saa7134_page(struct snd_pcm_substream *substream,
					unsigned long offset)
{
	void *pageptr = substream->runtime->dma_area + offset;
	return vmalloc_to_page(pageptr);
}

/*
 * ALSA capture callbacks definition
 */

static const struct snd_pcm_ops snd_card_saa7134_capture_ops = {
	.open =			snd_card_saa7134_capture_open,
	.close =		snd_card_saa7134_capture_close,
	.hw_params =		snd_card_saa7134_hw_params,
	.hw_free =		snd_card_saa7134_hw_free,
	.prepare =		snd_card_saa7134_capture_prepare,
	.trigger =		snd_card_saa7134_capture_trigger,
	.pointer =		snd_card_saa7134_capture_pointer,
	.page =			snd_card_saa7134_page,
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
	struct snd_pcm *pcm;
	int err;

	if ((err = snd_pcm_new(saa7134->card, "SAA7134 PCM", device, 0, 1, &pcm)) < 0)
		return err;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_card_saa7134_capture_ops);
	pcm->private_data = saa7134;
	pcm->info_flags = 0;
	strscpy(pcm->name, "SAA7134 PCM", sizeof(pcm->name));
	return 0;
}

#define SAA713x_VOLUME(xname, xindex, addr) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_saa7134_volume_info, \
  .get = snd_saa7134_volume_get, .put = snd_saa7134_volume_put, \
  .private_value = addr }

static int snd_saa7134_volume_info(struct snd_kcontrol * kcontrol,
				   struct snd_ctl_elem_info * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 20;
	return 0;
}

static int snd_saa7134_volume_get(struct snd_kcontrol * kcontrol,
				  struct snd_ctl_elem_value * ucontrol)
{
	snd_card_saa7134_t *chip = snd_kcontrol_chip(kcontrol);
	int addr = kcontrol->private_value;

	ucontrol->value.integer.value[0] = chip->mixer_volume[addr][0];
	ucontrol->value.integer.value[1] = chip->mixer_volume[addr][1];
	return 0;
}

static int snd_saa7134_volume_put(struct snd_kcontrol * kcontrol,
				  struct snd_ctl_elem_value * ucontrol)
{
	snd_card_saa7134_t *chip = snd_kcontrol_chip(kcontrol);
	struct saa7134_dev *dev = chip->dev;

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
	change = 0;
	if (chip->mixer_volume[addr][0] != left) {
		change = 1;
		right = left;
	}
	if (chip->mixer_volume[addr][1] != right) {
		change = 1;
		left = right;
	}
	if (change) {
		switch (dev->pci->device) {
			case PCI_DEVICE_ID_PHILIPS_SAA7134:
				switch (addr) {
					case MIXER_ADDR_TVTUNER:
						left = 20;
						break;
					case MIXER_ADDR_LINE1:
						saa_andorb(SAA7134_ANALOG_IO_SELECT,  0x10,
							   (left > 10) ? 0x00 : 0x10);
						break;
					case MIXER_ADDR_LINE2:
						saa_andorb(SAA7134_ANALOG_IO_SELECT,  0x20,
							   (left > 10) ? 0x00 : 0x20);
						break;
				}
				break;
			case PCI_DEVICE_ID_PHILIPS_SAA7133:
			case PCI_DEVICE_ID_PHILIPS_SAA7135:
				switch (addr) {
					case MIXER_ADDR_TVTUNER:
						left = 20;
						break;
					case MIXER_ADDR_LINE1:
						saa_andorb(0x0594,  0x10,
							   (left > 10) ? 0x00 : 0x10);
						break;
					case MIXER_ADDR_LINE2:
						saa_andorb(0x0594,  0x20,
							   (left > 10) ? 0x00 : 0x20);
						break;
				}
				break;
		}
		chip->mixer_volume[addr][0] = left;
		chip->mixer_volume[addr][1] = right;
	}
	spin_unlock_irq(&chip->mixer_lock);
	return change;
}

#define SAA713x_CAPSRC(xname, xindex, addr) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_saa7134_capsrc_info, \
  .get = snd_saa7134_capsrc_get, .put = snd_saa7134_capsrc_put, \
  .private_value = addr }

static int snd_saa7134_capsrc_info(struct snd_kcontrol * kcontrol,
				   struct snd_ctl_elem_info * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_saa7134_capsrc_get(struct snd_kcontrol * kcontrol,
				  struct snd_ctl_elem_value * ucontrol)
{
	snd_card_saa7134_t *chip = snd_kcontrol_chip(kcontrol);
	int addr = kcontrol->private_value;

	spin_lock_irq(&chip->mixer_lock);
	if (chip->capture_source_addr == addr) {
		ucontrol->value.integer.value[0] = chip->capture_source[0];
		ucontrol->value.integer.value[1] = chip->capture_source[1];
	} else {
		ucontrol->value.integer.value[0] = 0;
		ucontrol->value.integer.value[1] = 0;
	}
	spin_unlock_irq(&chip->mixer_lock);

	return 0;
}

static int snd_saa7134_capsrc_put(struct snd_kcontrol * kcontrol,
				  struct snd_ctl_elem_value * ucontrol)
{
	int left, right;
	left = ucontrol->value.integer.value[0] & 1;
	right = ucontrol->value.integer.value[1] & 1;

	return snd_saa7134_capsrc_set(kcontrol, left, right, false);
}

static struct snd_kcontrol_new snd_saa7134_volume_controls[] = {
SAA713x_VOLUME("Video Volume", 0, MIXER_ADDR_TVTUNER),
SAA713x_VOLUME("Line Volume", 1, MIXER_ADDR_LINE1),
SAA713x_VOLUME("Line Volume", 2, MIXER_ADDR_LINE2),
};

static struct snd_kcontrol_new snd_saa7134_capture_controls[] = {
SAA713x_CAPSRC("Video Capture Switch", 0, MIXER_ADDR_TVTUNER),
SAA713x_CAPSRC("Line Capture Switch", 1, MIXER_ADDR_LINE1),
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
	struct snd_card *card = chip->card;
	struct snd_kcontrol *kcontrol;
	unsigned int idx;
	int err, addr;

	strscpy(card->mixername, "SAA7134 Mixer", sizeof(card->mixername));

	for (idx = 0; idx < ARRAY_SIZE(snd_saa7134_volume_controls); idx++) {
		kcontrol = snd_ctl_new1(&snd_saa7134_volume_controls[idx],
					chip);
		err = snd_ctl_add(card, kcontrol);
		if (err < 0)
			return err;
	}

	for (idx = 0; idx < ARRAY_SIZE(snd_saa7134_capture_controls); idx++) {
		kcontrol = snd_ctl_new1(&snd_saa7134_capture_controls[idx],
					chip);
		addr = snd_saa7134_capture_controls[idx].private_value;
		chip->capture_ctl[addr] = kcontrol;
		err = snd_ctl_add(card, kcontrol);
		if (err < 0)
			return err;
	}

	chip->capture_source_addr = MIXER_ADDR_UNSELECTED;
	return 0;
}

static void snd_saa7134_free(struct snd_card * card)
{
	snd_card_saa7134_t *chip = card->private_data;

	if (chip->dev->dmasound.priv_data == NULL)
		return;

	if (chip->irq >= 0)
		free_irq(chip->irq, &chip->dev->dmasound);

	chip->dev->dmasound.priv_data = NULL;

}

/*
 * ALSA initialization
 *
 *   Called by the init routine, once for each saa7134 device present,
 *  it creates the basic structures and registers the ALSA devices
 *
 */

static int alsa_card_saa7134_create(struct saa7134_dev *dev, int devnum)
{

	struct snd_card *card;
	snd_card_saa7134_t *chip;
	int err;


	if (devnum >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[devnum])
		return -ENODEV;

	err = snd_card_new(&dev->pci->dev, index[devnum], id[devnum],
			   THIS_MODULE, sizeof(snd_card_saa7134_t), &card);
	if (err < 0)
		return err;

	strscpy(card->driver, "SAA7134", sizeof(card->driver));

	/* Card "creation" */

	card->private_free = snd_saa7134_free;
	chip = card->private_data;

	spin_lock_init(&chip->lock);
	spin_lock_init(&chip->mixer_lock);

	chip->dev = dev;

	chip->card = card;

	chip->pci = dev->pci;
	chip->iobase = pci_resource_start(dev->pci, 0);


	err = request_irq(dev->pci->irq, saa7134_alsa_irq,
				IRQF_SHARED, dev->name,
				(void*) &dev->dmasound);

	if (err < 0) {
		pr_err("%s: can't get IRQ %d for ALSA\n",
			dev->name, dev->pci->irq);
		goto __nodev;
	}

	chip->irq = dev->pci->irq;

	mutex_init(&dev->dmasound.lock);

	if ((err = snd_card_saa7134_new_mixer(chip)) < 0)
		goto __nodev;

	if ((err = snd_card_saa7134_pcm(chip, 0)) < 0)
		goto __nodev;

	/* End of "creation" */

	strscpy(card->shortname, "SAA7134", sizeof(card->shortname));
	sprintf(card->longname, "%s at 0x%lx irq %d",
		chip->dev->name, chip->iobase, chip->irq);

	pr_info("%s/alsa: %s registered as card %d\n",
		dev->name, card->longname, index[devnum]);

	if ((err = snd_card_register(card)) == 0) {
		snd_saa7134_cards[devnum] = card;
		return 0;
	}

__nodev:
	snd_card_free(card);
	return err;
}


static int alsa_device_init(struct saa7134_dev *dev)
{
	dev->dmasound.priv_data = dev;
	alsa_card_saa7134_create(dev,dev->nr);
	return 1;
}

static int alsa_device_exit(struct saa7134_dev *dev)
{
	if (!snd_saa7134_cards[dev->nr])
		return 1;

	snd_card_free(snd_saa7134_cards[dev->nr]);
	snd_saa7134_cards[dev->nr] = NULL;
	return 1;
}

/*
 * Module initializer
 *
 * Loops through present saa7134 cards, and assigns an ALSA device
 * to each one
 *
 */

static int saa7134_alsa_init(void)
{
	struct saa7134_dev *dev;

	saa7134_dmasound_init = alsa_device_init;
	saa7134_dmasound_exit = alsa_device_exit;

	pr_info("saa7134 ALSA driver for DMA sound loaded\n");

	list_for_each_entry(dev, &saa7134_devlist, devlist) {
		if (dev->pci->device == PCI_DEVICE_ID_PHILIPS_SAA7130)
			pr_info("%s/alsa: %s doesn't support digital audio\n",
				dev->name, saa7134_boards[dev->board].name);
		else
			alsa_device_init(dev);
	}

	if (list_empty(&saa7134_devlist))
		pr_info("saa7134 ALSA: no saa7134 cards found\n");

	return 0;

}

/*
 * Module destructor
 */

static void saa7134_alsa_exit(void)
{
	int idx;

	for (idx = 0; idx < SNDRV_CARDS; idx++) {
		if (snd_saa7134_cards[idx])
			snd_card_free(snd_saa7134_cards[idx]);
	}

	saa7134_dmasound_init = NULL;
	saa7134_dmasound_exit = NULL;
	pr_info("saa7134 ALSA driver for DMA sound unloaded\n");

	return;
}

/* We initialize this late, to make sure the sound system is up and running */
late_initcall(saa7134_alsa_init);
module_exit(saa7134_alsa_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ricardo Cerqueira");
