/*
 *  Support for audio capture
 *  PCI function #1 of the cx2388x.
 *
 *    (c) 2007 Trent Piepho <xyzzy@speakeasy.org>
 *    (c) 2005,2006 Ricardo Cerqueira <v4l@cerqueira.org>
 *    (c) 2005 Mauro Carvalho Chehab <mchehab@kernel.org>
 *    Based on a dummy cx88 module by Gerd Knorr <kraxel@bytesex.org>
 *    Based on dummy.c by Jaroslav Kysela <perex@perex.cz>
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
 */

#include "cx88.h"
#include "cx88-reg.h"

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/control.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <media/i2c/wm8775.h>

#define dprintk(level, fmt, arg...) do {				\
	if (debug + 1 > level)						\
		printk(KERN_DEBUG pr_fmt("%s: alsa: " fmt),		\
			chip->core->name, ##arg);			\
} while (0)

/*
 * Data type declarations - Can be moded to a header file later
 */

struct cx88_audio_buffer {
	unsigned int               bpl;
	struct cx88_riscmem        risc;
	void			*vaddr;
	struct scatterlist	*sglist;
	int                     sglen;
	int                     nr_pages;
};

struct cx88_audio_dev {
	struct cx88_core           *core;
	struct cx88_dmaqueue       q;

	/* pci i/o */
	struct pci_dev             *pci;

	/* audio controls */
	int                        irq;

	struct snd_card            *card;

	spinlock_t                 reg_lock;
	atomic_t		   count;

	unsigned int               dma_size;
	unsigned int               period_size;
	unsigned int               num_periods;

	struct cx88_audio_buffer   *buf;

	struct snd_pcm_substream   *substream;
};

/*
 * Module global static vars
 */

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static const char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;

module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable cx88x soundcard. default enabled.");

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for cx88x capture interface(s).");

/*
 * Module macros
 */

MODULE_DESCRIPTION("ALSA driver module for cx2388x based TV cards");
MODULE_AUTHOR("Ricardo Cerqueira");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@kernel.org>");
MODULE_LICENSE("GPL");
MODULE_VERSION(CX88_VERSION);

MODULE_SUPPORTED_DEVICE("{{Conexant,23881},{{Conexant,23882},{{Conexant,23883}");
static unsigned int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "enable debug messages");

/*
 * Module specific functions
 */

/*
 * BOARD Specific: Sets audio DMA
 */

static int _cx88_start_audio_dma(struct cx88_audio_dev *chip)
{
	struct cx88_audio_buffer *buf = chip->buf;
	struct cx88_core *core = chip->core;
	const struct sram_channel *audio_ch = &cx88_sram_channels[SRAM_CH25];

	/* Make sure RISC/FIFO are off before changing FIFO/RISC settings */
	cx_clear(MO_AUD_DMACNTRL, 0x11);

	/* setup fifo + format - out channel */
	cx88_sram_channel_setup(chip->core, audio_ch, buf->bpl, buf->risc.dma);

	/* sets bpl size */
	cx_write(MO_AUDD_LNGTH, buf->bpl);

	/* reset counter */
	cx_write(MO_AUDD_GPCNTRL, GP_COUNT_CONTROL_RESET);
	atomic_set(&chip->count, 0);

	dprintk(1,
		"Start audio DMA, %d B/line, %d lines/FIFO, %d periods, %d byte buffer\n",
		buf->bpl, cx_read(audio_ch->cmds_start + 8) >> 1,
		chip->num_periods, buf->bpl * chip->num_periods);

	/* Enables corresponding bits at AUD_INT_STAT */
	cx_write(MO_AUD_INTMSK, AUD_INT_OPC_ERR | AUD_INT_DN_SYNC |
				AUD_INT_DN_RISCI2 | AUD_INT_DN_RISCI1);

	/* Clean any pending interrupt bits already set */
	cx_write(MO_AUD_INTSTAT, ~0);

	/* enable audio irqs */
	cx_set(MO_PCI_INTMSK, chip->core->pci_irqmask | PCI_INT_AUDINT);

	/* start dma */

	/* Enables Risc Processor */
	cx_set(MO_DEV_CNTRL2, (1 << 5));
	/* audio downstream FIFO and RISC enable */
	cx_set(MO_AUD_DMACNTRL, 0x11);

	if (debug)
		cx88_sram_channel_dump(chip->core, audio_ch);

	return 0;
}

/*
 * BOARD Specific: Resets audio DMA
 */
static int _cx88_stop_audio_dma(struct cx88_audio_dev *chip)
{
	struct cx88_core *core = chip->core;

	dprintk(1, "Stopping audio DMA\n");

	/* stop dma */
	cx_clear(MO_AUD_DMACNTRL, 0x11);

	/* disable irqs */
	cx_clear(MO_PCI_INTMSK, PCI_INT_AUDINT);
	cx_clear(MO_AUD_INTMSK, AUD_INT_OPC_ERR | AUD_INT_DN_SYNC |
				AUD_INT_DN_RISCI2 | AUD_INT_DN_RISCI1);

	if (debug)
		cx88_sram_channel_dump(chip->core,
				       &cx88_sram_channels[SRAM_CH25]);

	return 0;
}

#define MAX_IRQ_LOOP 50

/*
 * BOARD Specific: IRQ dma bits
 */
static const char *cx88_aud_irqs[32] = {
	"dn_risci1", "up_risci1", "rds_dn_risc1", /* 0-2 */
	NULL,					  /* reserved */
	"dn_risci2", "up_risci2", "rds_dn_risc2", /* 4-6 */
	NULL,					  /* reserved */
	"dnf_of", "upf_uf", "rds_dnf_uf",	  /* 8-10 */
	NULL,					  /* reserved */
	"dn_sync", "up_sync", "rds_dn_sync",	  /* 12-14 */
	NULL,					  /* reserved */
	"opc_err", "par_err", "rip_err",	  /* 16-18 */
	"pci_abort", "ber_irq", "mchg_irq"	  /* 19-21 */
};

/*
 * BOARD Specific: Threats IRQ audio specific calls
 */
static void cx8801_aud_irq(struct cx88_audio_dev *chip)
{
	struct cx88_core *core = chip->core;
	u32 status, mask;

	status = cx_read(MO_AUD_INTSTAT);
	mask   = cx_read(MO_AUD_INTMSK);
	if (0 == (status & mask))
		return;
	cx_write(MO_AUD_INTSTAT, status);
	if (debug > 1  ||  (status & mask & ~0xff))
		cx88_print_irqbits("irq aud",
				   cx88_aud_irqs, ARRAY_SIZE(cx88_aud_irqs),
				   status, mask);
	/* risc op code error */
	if (status & AUD_INT_OPC_ERR) {
		pr_warn("Audio risc op code error\n");
		cx_clear(MO_AUD_DMACNTRL, 0x11);
		cx88_sram_channel_dump(core, &cx88_sram_channels[SRAM_CH25]);
	}
	if (status & AUD_INT_DN_SYNC) {
		dprintk(1, "Downstream sync error\n");
		cx_write(MO_AUDD_GPCNTRL, GP_COUNT_CONTROL_RESET);
		return;
	}
	/* risc1 downstream */
	if (status & AUD_INT_DN_RISCI1) {
		atomic_set(&chip->count, cx_read(MO_AUDD_GPCNT));
		snd_pcm_period_elapsed(chip->substream);
	}
	/* FIXME: Any other status should deserve a special handling? */
}

/*
 * BOARD Specific: Handles IRQ calls
 */
static irqreturn_t cx8801_irq(int irq, void *dev_id)
{
	struct cx88_audio_dev *chip = dev_id;
	struct cx88_core *core = chip->core;
	u32 status;
	int loop, handled = 0;

	for (loop = 0; loop < MAX_IRQ_LOOP; loop++) {
		status = cx_read(MO_PCI_INTSTAT) &
			(core->pci_irqmask | PCI_INT_AUDINT);
		if (status == 0)
			goto out;
		dprintk(3, "cx8801_irq loop %d/%d, status %x\n",
			loop, MAX_IRQ_LOOP, status);
		handled = 1;
		cx_write(MO_PCI_INTSTAT, status);

		if (status & core->pci_irqmask)
			cx88_core_irq(core, status);
		if (status & PCI_INT_AUDINT)
			cx8801_aud_irq(chip);
	}

	if (loop == MAX_IRQ_LOOP) {
		pr_err("IRQ loop detected, disabling interrupts\n");
		cx_clear(MO_PCI_INTMSK, PCI_INT_AUDINT);
	}

 out:
	return IRQ_RETVAL(handled);
}

static int cx88_alsa_dma_init(struct cx88_audio_dev *chip, int nr_pages)
{
	struct cx88_audio_buffer *buf = chip->buf;
	struct page *pg;
	int i;

	buf->vaddr = vmalloc_32(nr_pages << PAGE_SHIFT);
	if (!buf->vaddr) {
		dprintk(1, "vmalloc_32(%d pages) failed\n", nr_pages);
		return -ENOMEM;
	}

	dprintk(1, "vmalloc is at addr %p, size=%d\n",
		buf->vaddr, nr_pages << PAGE_SHIFT);

	memset(buf->vaddr, 0, nr_pages << PAGE_SHIFT);
	buf->nr_pages = nr_pages;

	buf->sglist = vzalloc(buf->nr_pages * sizeof(*buf->sglist));
	if (!buf->sglist)
		goto vzalloc_err;

	sg_init_table(buf->sglist, buf->nr_pages);
	for (i = 0; i < buf->nr_pages; i++) {
		pg = vmalloc_to_page(buf->vaddr + i * PAGE_SIZE);
		if (!pg)
			goto vmalloc_to_page_err;
		sg_set_page(&buf->sglist[i], pg, PAGE_SIZE, 0);
	}
	return 0;

vmalloc_to_page_err:
	vfree(buf->sglist);
	buf->sglist = NULL;
vzalloc_err:
	vfree(buf->vaddr);
	buf->vaddr = NULL;
	return -ENOMEM;
}

static int cx88_alsa_dma_map(struct cx88_audio_dev *dev)
{
	struct cx88_audio_buffer *buf = dev->buf;

	buf->sglen = dma_map_sg(&dev->pci->dev, buf->sglist,
			buf->nr_pages, PCI_DMA_FROMDEVICE);

	if (buf->sglen == 0) {
		pr_warn("%s: cx88_alsa_map_sg failed\n", __func__);
		return -ENOMEM;
	}
	return 0;
}

static int cx88_alsa_dma_unmap(struct cx88_audio_dev *dev)
{
	struct cx88_audio_buffer *buf = dev->buf;

	if (!buf->sglen)
		return 0;

	dma_unmap_sg(&dev->pci->dev, buf->sglist, buf->sglen,
		     PCI_DMA_FROMDEVICE);
	buf->sglen = 0;
	return 0;
}

static int cx88_alsa_dma_free(struct cx88_audio_buffer *buf)
{
	vfree(buf->sglist);
	buf->sglist = NULL;
	vfree(buf->vaddr);
	buf->vaddr = NULL;
	return 0;
}

static int dsp_buffer_free(struct cx88_audio_dev *chip)
{
	struct cx88_riscmem *risc = &chip->buf->risc;

	WARN_ON(!chip->dma_size);

	dprintk(2, "Freeing buffer\n");
	cx88_alsa_dma_unmap(chip);
	cx88_alsa_dma_free(chip->buf);
	if (risc->cpu)
		pci_free_consistent(chip->pci, risc->size,
				    risc->cpu, risc->dma);
	kfree(chip->buf);

	chip->buf = NULL;

	return 0;
}

/*
 * ALSA PCM Interface
 */

/*
 * Digital hardware definition
 */
#define DEFAULT_FIFO_SIZE	4096
static const struct snd_pcm_hardware snd_cx88_digital_hw = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,

	.rates =		SNDRV_PCM_RATE_48000,
	.rate_min =		48000,
	.rate_max =		48000,
	.channels_min = 2,
	.channels_max = 2,
	/*
	 * Analog audio output will be full of clicks and pops if there
	 * are not exactly four lines in the SRAM FIFO buffer.
	 */
	.period_bytes_min = DEFAULT_FIFO_SIZE / 4,
	.period_bytes_max = DEFAULT_FIFO_SIZE / 4,
	.periods_min = 1,
	.periods_max = 1024,
	.buffer_bytes_max = (1024 * 1024),
};

/*
 * audio pcm capture open callback
 */
static int snd_cx88_pcm_open(struct snd_pcm_substream *substream)
{
	struct cx88_audio_dev *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	if (!chip) {
		pr_err("BUG: cx88 can't find device struct. Can't proceed with open\n");
		return -ENODEV;
	}

	err = snd_pcm_hw_constraint_pow2(runtime, 0,
					 SNDRV_PCM_HW_PARAM_PERIODS);
	if (err < 0)
		goto _error;

	chip->substream = substream;

	runtime->hw = snd_cx88_digital_hw;

	if (cx88_sram_channels[SRAM_CH25].fifo_size != DEFAULT_FIFO_SIZE) {
		unsigned int bpl = cx88_sram_channels[SRAM_CH25].fifo_size / 4;

		bpl &= ~7; /* must be multiple of 8 */
		runtime->hw.period_bytes_min = bpl;
		runtime->hw.period_bytes_max = bpl;
	}

	return 0;
_error:
	dprintk(1, "Error opening PCM!\n");
	return err;
}

/*
 * audio close callback
 */
static int snd_cx88_close(struct snd_pcm_substream *substream)
{
	return 0;
}

/*
 * hw_params callback
 */
static int snd_cx88_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *hw_params)
{
	struct cx88_audio_dev *chip = snd_pcm_substream_chip(substream);

	struct cx88_audio_buffer *buf;
	int ret;

	if (substream->runtime->dma_area) {
		dsp_buffer_free(chip);
		substream->runtime->dma_area = NULL;
	}

	chip->period_size = params_period_bytes(hw_params);
	chip->num_periods = params_periods(hw_params);
	chip->dma_size = chip->period_size * params_periods(hw_params);

	WARN_ON(!chip->dma_size);
	WARN_ON(chip->num_periods & (chip->num_periods - 1));

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	chip->buf = buf;
	buf->bpl = chip->period_size;

	ret = cx88_alsa_dma_init(chip,
				 (PAGE_ALIGN(chip->dma_size) >> PAGE_SHIFT));
	if (ret < 0)
		goto error;

	ret = cx88_alsa_dma_map(chip);
	if (ret < 0)
		goto error;

	ret = cx88_risc_databuffer(chip->pci, &buf->risc, buf->sglist,
				   chip->period_size, chip->num_periods, 1);
	if (ret < 0)
		goto error;

	/* Loop back to start of program */
	buf->risc.jmp[0] = cpu_to_le32(RISC_JUMP | RISC_IRQ1 | RISC_CNT_INC);
	buf->risc.jmp[1] = cpu_to_le32(buf->risc.dma);

	substream->runtime->dma_area = chip->buf->vaddr;
	substream->runtime->dma_bytes = chip->dma_size;
	substream->runtime->dma_addr = 0;
	return 0;

error:
	kfree(buf);
	return ret;
}

/*
 * hw free callback
 */
static int snd_cx88_hw_free(struct snd_pcm_substream *substream)
{
	struct cx88_audio_dev *chip = snd_pcm_substream_chip(substream);

	if (substream->runtime->dma_area) {
		dsp_buffer_free(chip);
		substream->runtime->dma_area = NULL;
	}

	return 0;
}

/*
 * prepare callback
 */
static int snd_cx88_prepare(struct snd_pcm_substream *substream)
{
	return 0;
}

/*
 * trigger callback
 */
static int snd_cx88_card_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct cx88_audio_dev *chip = snd_pcm_substream_chip(substream);
	int err;

	/* Local interrupts are already disabled by ALSA */
	spin_lock(&chip->reg_lock);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		err = _cx88_start_audio_dma(chip);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		err = _cx88_stop_audio_dma(chip);
		break;
	default:
		err =  -EINVAL;
		break;
	}

	spin_unlock(&chip->reg_lock);

	return err;
}

/*
 * pointer callback
 */
static snd_pcm_uframes_t snd_cx88_pointer(struct snd_pcm_substream *substream)
{
	struct cx88_audio_dev *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	u16 count;

	count = atomic_read(&chip->count);

//	dprintk(2, "%s - count %d (+%u), period %d, frame %lu\n", __func__,
//		count, new, count & (runtime->periods-1),
//		runtime->period_size * (count & (runtime->periods-1)));
	return runtime->period_size * (count & (runtime->periods - 1));
}

/*
 * page callback (needed for mmap)
 */
static struct page *snd_cx88_page(struct snd_pcm_substream *substream,
				  unsigned long offset)
{
	void *pageptr = substream->runtime->dma_area + offset;

	return vmalloc_to_page(pageptr);
}

/*
 * operators
 */
static const struct snd_pcm_ops snd_cx88_pcm_ops = {
	.open = snd_cx88_pcm_open,
	.close = snd_cx88_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_cx88_hw_params,
	.hw_free = snd_cx88_hw_free,
	.prepare = snd_cx88_prepare,
	.trigger = snd_cx88_card_trigger,
	.pointer = snd_cx88_pointer,
	.page = snd_cx88_page,
};

/*
 * create a PCM device
 */
static int snd_cx88_pcm(struct cx88_audio_dev *chip, int device,
			const char *name)
{
	int err;
	struct snd_pcm *pcm;

	err = snd_pcm_new(chip->card, name, device, 0, 1, &pcm);
	if (err < 0)
		return err;
	pcm->private_data = chip;
	strcpy(pcm->name, name);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_cx88_pcm_ops);

	return 0;
}

/*
 * CONTROL INTERFACE
 */
static int snd_cx88_volume_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *info)
{
	info->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	info->count = 2;
	info->value.integer.min = 0;
	info->value.integer.max = 0x3f;

	return 0;
}

static int snd_cx88_volume_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *value)
{
	struct cx88_audio_dev *chip = snd_kcontrol_chip(kcontrol);
	struct cx88_core *core = chip->core;
	int vol = 0x3f - (cx_read(AUD_VOL_CTL) & 0x3f),
	    bal = cx_read(AUD_BAL_CTL);

	value->value.integer.value[(bal & 0x40) ? 0 : 1] = vol;
	vol -= (bal & 0x3f);
	value->value.integer.value[(bal & 0x40) ? 1 : 0] = vol < 0 ? 0 : vol;

	return 0;
}

static void snd_cx88_wm8775_volume_put(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *value)
{
	struct cx88_audio_dev *chip = snd_kcontrol_chip(kcontrol);
	struct cx88_core *core = chip->core;
	u16 left = value->value.integer.value[0];
	u16 right = value->value.integer.value[1];
	int v, b;

	/* Pass volume & balance onto any WM8775 */
	if (left >= right) {
		v = left << 10;
		b = left ? (0x8000 * right) / left : 0x8000;
	} else {
		v = right << 10;
		b = right ? 0xffff - (0x8000 * left) / right : 0x8000;
	}
	wm8775_s_ctrl(core, V4L2_CID_AUDIO_VOLUME, v);
	wm8775_s_ctrl(core, V4L2_CID_AUDIO_BALANCE, b);
}

/* OK - TODO: test it */
static int snd_cx88_volume_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *value)
{
	struct cx88_audio_dev *chip = snd_kcontrol_chip(kcontrol);
	struct cx88_core *core = chip->core;
	int left, right, v, b;
	int changed = 0;
	u32 old;

	if (core->sd_wm8775)
		snd_cx88_wm8775_volume_put(kcontrol, value);

	left = value->value.integer.value[0] & 0x3f;
	right = value->value.integer.value[1] & 0x3f;
	b = right - left;
	if (b < 0) {
		v = 0x3f - left;
		b = (-b) | 0x40;
	} else {
		v = 0x3f - right;
	}
	/* Do we really know this will always be called with IRQs on? */
	spin_lock_irq(&chip->reg_lock);
	old = cx_read(AUD_VOL_CTL);
	if (v != (old & 0x3f)) {
		cx_swrite(SHADOW_AUD_VOL_CTL, AUD_VOL_CTL, (old & ~0x3f) | v);
		changed = 1;
	}
	if ((cx_read(AUD_BAL_CTL) & 0x7f) != b) {
		cx_write(AUD_BAL_CTL, b);
		changed = 1;
	}
	spin_unlock_irq(&chip->reg_lock);

	return changed;
}

static const DECLARE_TLV_DB_SCALE(snd_cx88_db_scale, -6300, 100, 0);

static const struct snd_kcontrol_new snd_cx88_volume = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
		  SNDRV_CTL_ELEM_ACCESS_TLV_READ,
	.name = "Analog-TV Volume",
	.info = snd_cx88_volume_info,
	.get = snd_cx88_volume_get,
	.put = snd_cx88_volume_put,
	.tlv.p = snd_cx88_db_scale,
};

static int snd_cx88_switch_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *value)
{
	struct cx88_audio_dev *chip = snd_kcontrol_chip(kcontrol);
	struct cx88_core *core = chip->core;
	u32 bit = kcontrol->private_value;

	value->value.integer.value[0] = !(cx_read(AUD_VOL_CTL) & bit);
	return 0;
}

static int snd_cx88_switch_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *value)
{
	struct cx88_audio_dev *chip = snd_kcontrol_chip(kcontrol);
	struct cx88_core *core = chip->core;
	u32 bit = kcontrol->private_value;
	int ret = 0;
	u32 vol;

	spin_lock_irq(&chip->reg_lock);
	vol = cx_read(AUD_VOL_CTL);
	if (value->value.integer.value[0] != !(vol & bit)) {
		vol ^= bit;
		cx_swrite(SHADOW_AUD_VOL_CTL, AUD_VOL_CTL, vol);
		/* Pass mute onto any WM8775 */
		if (core->sd_wm8775 && ((1 << 6) == bit))
			wm8775_s_ctrl(core,
				      V4L2_CID_AUDIO_MUTE, 0 != (vol & bit));
		ret = 1;
	}
	spin_unlock_irq(&chip->reg_lock);
	return ret;
}

static const struct snd_kcontrol_new snd_cx88_dac_switch = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Audio-Out Switch",
	.info = snd_ctl_boolean_mono_info,
	.get = snd_cx88_switch_get,
	.put = snd_cx88_switch_put,
	.private_value = (1 << 8),
};

static const struct snd_kcontrol_new snd_cx88_source_switch = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Analog-TV Switch",
	.info = snd_ctl_boolean_mono_info,
	.get = snd_cx88_switch_get,
	.put = snd_cx88_switch_put,
	.private_value = (1 << 6),
};

static int snd_cx88_alc_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *value)
{
	struct cx88_audio_dev *chip = snd_kcontrol_chip(kcontrol);
	struct cx88_core *core = chip->core;
	s32 val;

	val = wm8775_g_ctrl(core, V4L2_CID_AUDIO_LOUDNESS);
	value->value.integer.value[0] = val ? 1 : 0;
	return 0;
}

static int snd_cx88_alc_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *value)
{
	struct cx88_audio_dev *chip = snd_kcontrol_chip(kcontrol);
	struct cx88_core *core = chip->core;

	wm8775_s_ctrl(core, V4L2_CID_AUDIO_LOUDNESS,
		      value->value.integer.value[0] != 0);
	return 0;
}

static const struct snd_kcontrol_new snd_cx88_alc_switch = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Line-In ALC Switch",
	.info = snd_ctl_boolean_mono_info,
	.get = snd_cx88_alc_get,
	.put = snd_cx88_alc_put,
};

/*
 * Basic Flow for Sound Devices
 */

/*
 * PCI ID Table - 14f1:8801 and 14f1:8811 means function 1: Audio
 * Only boards with eeprom and byte 1 at eeprom=1 have it
 */

static const struct pci_device_id cx88_audio_pci_tbl[] = {
	{0x14f1, 0x8801, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0x14f1, 0x8811, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0, }
};
MODULE_DEVICE_TABLE(pci, cx88_audio_pci_tbl);

/*
 * Chip-specific destructor
 */

static int snd_cx88_free(struct cx88_audio_dev *chip)
{
	if (chip->irq >= 0)
		free_irq(chip->irq, chip);

	cx88_core_put(chip->core, chip->pci);

	pci_disable_device(chip->pci);
	return 0;
}

/*
 * Component Destructor
 */
static void snd_cx88_dev_free(struct snd_card *card)
{
	struct cx88_audio_dev *chip = card->private_data;

	snd_cx88_free(chip);
}

/*
 * Alsa Constructor - Component probe
 */

static int devno;
static int snd_cx88_create(struct snd_card *card, struct pci_dev *pci,
			   struct cx88_audio_dev **rchip,
			   struct cx88_core **core_ptr)
{
	struct cx88_audio_dev	*chip;
	struct cx88_core	*core;
	int			err;
	unsigned char		pci_lat;

	*rchip = NULL;

	err = pci_enable_device(pci);
	if (err < 0)
		return err;

	pci_set_master(pci);

	chip = card->private_data;

	core = cx88_core_get(pci);
	if (!core) {
		err = -EINVAL;
		return err;
	}

	err = pci_set_dma_mask(pci, DMA_BIT_MASK(32));
	if (err) {
		dprintk(0, "%s/1: Oops: no 32bit PCI DMA ???\n", core->name);
		cx88_core_put(core, pci);
		return err;
	}

	/* pci init */
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;
	spin_lock_init(&chip->reg_lock);

	chip->core = core;

	/* get irq */
	err = request_irq(chip->pci->irq, cx8801_irq,
			  IRQF_SHARED, chip->core->name, chip);
	if (err < 0) {
		dprintk(0, "%s: can't get IRQ %d\n",
			chip->core->name, chip->pci->irq);
		return err;
	}

	/* print pci info */
	pci_read_config_byte(pci, PCI_LATENCY_TIMER, &pci_lat);

	dprintk(1,
		"ALSA %s/%i: found at %s, rev: %d, irq: %d, latency: %d, mmio: 0x%llx\n",
		core->name, devno,
		pci_name(pci), pci->revision, pci->irq,
		pci_lat, (unsigned long long)pci_resource_start(pci, 0));

	chip->irq = pci->irq;
	synchronize_irq(chip->irq);

	*rchip = chip;
	*core_ptr = core;

	return 0;
}

static int cx88_audio_initdev(struct pci_dev *pci,
			      const struct pci_device_id *pci_id)
{
	struct snd_card		*card;
	struct cx88_audio_dev	*chip;
	struct cx88_core	*core = NULL;
	int			err;

	if (devno >= SNDRV_CARDS)
		return (-ENODEV);

	if (!enable[devno]) {
		++devno;
		return (-ENOENT);
	}

	err = snd_card_new(&pci->dev, index[devno], id[devno], THIS_MODULE,
			   sizeof(struct cx88_audio_dev), &card);
	if (err < 0)
		return err;

	card->private_free = snd_cx88_dev_free;

	err = snd_cx88_create(card, pci, &chip, &core);
	if (err < 0)
		goto error;

	err = snd_cx88_pcm(chip, 0, "CX88 Digital");
	if (err < 0)
		goto error;

	err = snd_ctl_add(card, snd_ctl_new1(&snd_cx88_volume, chip));
	if (err < 0)
		goto error;
	err = snd_ctl_add(card, snd_ctl_new1(&snd_cx88_dac_switch, chip));
	if (err < 0)
		goto error;
	err = snd_ctl_add(card, snd_ctl_new1(&snd_cx88_source_switch, chip));
	if (err < 0)
		goto error;

	/* If there's a wm8775 then add a Line-In ALC switch */
	if (core->sd_wm8775)
		snd_ctl_add(card, snd_ctl_new1(&snd_cx88_alc_switch, chip));

	strcpy(card->driver, "CX88x");
	sprintf(card->shortname, "Conexant CX%x", pci->device);
	sprintf(card->longname, "%s at %#llx",
		card->shortname,
		(unsigned long long)pci_resource_start(pci, 0));
	strcpy(card->mixername, "CX88");

	dprintk(0, "%s/%i: ALSA support for cx2388x boards\n",
		card->driver, devno);

	err = snd_card_register(card);
	if (err < 0)
		goto error;
	pci_set_drvdata(pci, card);

	devno++;
	return 0;

error:
	snd_card_free(card);
	return err;
}

/*
 * ALSA destructor
 */
static void cx88_audio_finidev(struct pci_dev *pci)
{
	struct snd_card *card = pci_get_drvdata(pci);

	snd_card_free(card);

	devno--;
}

/*
 * PCI driver definition
 */

static struct pci_driver cx88_audio_pci_driver = {
	.name     = "cx88_audio",
	.id_table = cx88_audio_pci_tbl,
	.probe    = cx88_audio_initdev,
	.remove   = cx88_audio_finidev,
};

module_pci_driver(cx88_audio_pci_driver);
