/*
 * DesignWare HDMI audio driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Written and tested against the Designware HDMI Tx found in iMX6.
 */
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_edid.h>

#include <sound/asoundef.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_drm_eld.h>
#include <sound/pcm_iec958.h>

#include "dw-hdmi-audio.h"

#define DRIVER_NAME "dw-hdmi-ahb-audio"

/* Provide some bits rather than bit offsets */
enum {
	HDMI_AHB_DMA_CONF0_SW_FIFO_RST = BIT(7),
	HDMI_AHB_DMA_CONF0_EN_HLOCK = BIT(3),
	HDMI_AHB_DMA_START_START = BIT(0),
	HDMI_AHB_DMA_STOP_STOP = BIT(0),
	HDMI_IH_MUTE_AHBDMAAUD_STAT0_ERROR = BIT(5),
	HDMI_IH_MUTE_AHBDMAAUD_STAT0_LOST = BIT(4),
	HDMI_IH_MUTE_AHBDMAAUD_STAT0_RETRY = BIT(3),
	HDMI_IH_MUTE_AHBDMAAUD_STAT0_DONE = BIT(2),
	HDMI_IH_MUTE_AHBDMAAUD_STAT0_BUFFFULL = BIT(1),
	HDMI_IH_MUTE_AHBDMAAUD_STAT0_BUFFEMPTY = BIT(0),
	HDMI_IH_MUTE_AHBDMAAUD_STAT0_ALL =
		HDMI_IH_MUTE_AHBDMAAUD_STAT0_ERROR |
		HDMI_IH_MUTE_AHBDMAAUD_STAT0_LOST |
		HDMI_IH_MUTE_AHBDMAAUD_STAT0_RETRY |
		HDMI_IH_MUTE_AHBDMAAUD_STAT0_DONE |
		HDMI_IH_MUTE_AHBDMAAUD_STAT0_BUFFFULL |
		HDMI_IH_MUTE_AHBDMAAUD_STAT0_BUFFEMPTY,
	HDMI_IH_AHBDMAAUD_STAT0_ERROR = BIT(5),
	HDMI_IH_AHBDMAAUD_STAT0_LOST = BIT(4),
	HDMI_IH_AHBDMAAUD_STAT0_RETRY = BIT(3),
	HDMI_IH_AHBDMAAUD_STAT0_DONE = BIT(2),
	HDMI_IH_AHBDMAAUD_STAT0_BUFFFULL = BIT(1),
	HDMI_IH_AHBDMAAUD_STAT0_BUFFEMPTY = BIT(0),
	HDMI_IH_AHBDMAAUD_STAT0_ALL =
		HDMI_IH_AHBDMAAUD_STAT0_ERROR |
		HDMI_IH_AHBDMAAUD_STAT0_LOST |
		HDMI_IH_AHBDMAAUD_STAT0_RETRY |
		HDMI_IH_AHBDMAAUD_STAT0_DONE |
		HDMI_IH_AHBDMAAUD_STAT0_BUFFFULL |
		HDMI_IH_AHBDMAAUD_STAT0_BUFFEMPTY,
	HDMI_AHB_DMA_CONF0_INCR16 = 2 << 1,
	HDMI_AHB_DMA_CONF0_INCR8 = 1 << 1,
	HDMI_AHB_DMA_CONF0_INCR4 = 0,
	HDMI_AHB_DMA_CONF0_BURST_MODE = BIT(0),
	HDMI_AHB_DMA_MASK_DONE = BIT(7),

	HDMI_REVISION_ID = 0x0001,
	HDMI_IH_AHBDMAAUD_STAT0 = 0x0109,
	HDMI_IH_MUTE_AHBDMAAUD_STAT0 = 0x0189,
	HDMI_FC_AUDICONF2 = 0x1027,
	HDMI_FC_AUDSCONF = 0x1063,
	HDMI_FC_AUDSCONF_LAYOUT1 = 1 << 0,
	HDMI_FC_AUDSCONF_LAYOUT0 = 0 << 0,
	HDMI_AHB_DMA_CONF0 = 0x3600,
	HDMI_AHB_DMA_START = 0x3601,
	HDMI_AHB_DMA_STOP = 0x3602,
	HDMI_AHB_DMA_THRSLD = 0x3603,
	HDMI_AHB_DMA_STRADDR0 = 0x3604,
	HDMI_AHB_DMA_STPADDR0 = 0x3608,
	HDMI_AHB_DMA_MASK = 0x3614,
	HDMI_AHB_DMA_POL = 0x3615,
	HDMI_AHB_DMA_CONF1 = 0x3616,
	HDMI_AHB_DMA_BUFFPOL = 0x361a,
};

struct dw_hdmi_channel_conf {
	u8 conf1;
	u8 ca;
};

/*
 * The default mapping of ALSA channels to HDMI channels and speaker
 * allocation bits.  Note that we can't do channel remapping here -
 * channels must be in the same order.
 *
 * Mappings for alsa-lib pcm/surround*.conf files:
 *
 *		Front	Sur4.0	Sur4.1	Sur5.0	Sur5.1	Sur7.1
 * Channels	2	4	6	6	6	8
 *
 * Our mapping from ALSA channel to CEA686D speaker name and HDMI channel:
 *
 *				Number of ALSA channels
 * ALSA Channel	2	3	4	5	6	7	8
 * 0		FL:0	=	=	=	=	=	=
 * 1		FR:1	=	=	=	=	=	=
 * 2			FC:3	RL:4	LFE:2	=	=	=
 * 3				RR:5	RL:4	FC:3	=	=
 * 4					RR:5	RL:4	=	=
 * 5						RR:5	=	=
 * 6							RC:6	=
 * 7							RLC/FRC	RLC/FRC
 */
static struct dw_hdmi_channel_conf default_hdmi_channel_config[7] = {
	{ 0x03, 0x00 },	/* FL,FR */
	{ 0x0b, 0x02 },	/* FL,FR,FC */
	{ 0x33, 0x08 },	/* FL,FR,RL,RR */
	{ 0x37, 0x09 },	/* FL,FR,LFE,RL,RR */
	{ 0x3f, 0x0b },	/* FL,FR,LFE,FC,RL,RR */
	{ 0x7f, 0x0f },	/* FL,FR,LFE,FC,RL,RR,RC */
	{ 0xff, 0x13 },	/* FL,FR,LFE,FC,RL,RR,[FR]RC,[FR]LC */
};

struct snd_dw_hdmi {
	struct snd_card *card;
	struct snd_pcm *pcm;
	spinlock_t lock;
	struct dw_hdmi_audio_data data;
	struct snd_pcm_substream *substream;
	void (*reformat)(struct snd_dw_hdmi *, size_t, size_t);
	void *buf_src;
	void *buf_dst;
	dma_addr_t buf_addr;
	unsigned buf_offset;
	unsigned buf_period;
	unsigned buf_size;
	unsigned channels;
	u8 revision;
	u8 iec_offset;
	u8 cs[192][8];
};

static void dw_hdmi_writel(u32 val, void __iomem *ptr)
{
	writeb_relaxed(val, ptr);
	writeb_relaxed(val >> 8, ptr + 1);
	writeb_relaxed(val >> 16, ptr + 2);
	writeb_relaxed(val >> 24, ptr + 3);
}

/*
 * Convert to hardware format: The userspace buffer contains IEC958 samples,
 * with the PCUV bits in bits 31..28 and audio samples in bits 27..4.  We
 * need these to be in bits 27..24, with the IEC B bit in bit 28, and audio
 * samples in 23..0.
 *
 * Default preamble in bits 3..0: 8 = block start, 4 = even 2 = odd
 *
 * Ideally, we could do with having the data properly formatted in userspace.
 */
static void dw_hdmi_reformat_iec958(struct snd_dw_hdmi *dw,
	size_t offset, size_t bytes)
{
	u32 *src = dw->buf_src + offset;
	u32 *dst = dw->buf_dst + offset;
	u32 *end = dw->buf_src + offset + bytes;

	do {
		u32 b, sample = *src++;

		b = (sample & 8) << (28 - 3);

		sample >>= 4;

		*dst++ = sample | b;
	} while (src < end);
}

static u32 parity(u32 sample)
{
	sample ^= sample >> 16;
	sample ^= sample >> 8;
	sample ^= sample >> 4;
	sample ^= sample >> 2;
	sample ^= sample >> 1;
	return (sample & 1) << 27;
}

static void dw_hdmi_reformat_s24(struct snd_dw_hdmi *dw,
	size_t offset, size_t bytes)
{
	u32 *src = dw->buf_src + offset;
	u32 *dst = dw->buf_dst + offset;
	u32 *end = dw->buf_src + offset + bytes;

	do {
		unsigned i;
		u8 *cs;

		cs = dw->cs[dw->iec_offset++];
		if (dw->iec_offset >= 192)
			dw->iec_offset = 0;

		i = dw->channels;
		do {
			u32 sample = *src++;

			sample &= ~0xff000000;
			sample |= *cs++ << 24;
			sample |= parity(sample & ~0xf8000000);

			*dst++ = sample;
		} while (--i);
	} while (src < end);
}

static void dw_hdmi_create_cs(struct snd_dw_hdmi *dw,
	struct snd_pcm_runtime *runtime)
{
	u8 cs[4];
	unsigned ch, i, j;

	snd_pcm_create_iec958_consumer(runtime, cs, sizeof(cs));

	memset(dw->cs, 0, sizeof(dw->cs));

	for (ch = 0; ch < 8; ch++) {
		cs[2] &= ~IEC958_AES2_CON_CHANNEL;
		cs[2] |= (ch + 1) << 4;

		for (i = 0; i < ARRAY_SIZE(cs); i++) {
			unsigned c = cs[i];

			for (j = 0; j < 8; j++, c >>= 1)
				dw->cs[i * 8 + j][ch] = (c & 1) << 2;
		}
	}
	dw->cs[0][0] |= BIT(4);
}

static void dw_hdmi_start_dma(struct snd_dw_hdmi *dw)
{
	void __iomem *base = dw->data.base;
	unsigned offset = dw->buf_offset;
	unsigned period = dw->buf_period;
	u32 start, stop;

	dw->reformat(dw, offset, period);

	/* Clear all irqs before enabling irqs and starting DMA */
	writeb_relaxed(HDMI_IH_AHBDMAAUD_STAT0_ALL,
		       base + HDMI_IH_AHBDMAAUD_STAT0);

	start = dw->buf_addr + offset;
	stop = start + period - 1;

	/* Setup the hardware start/stop addresses */
	dw_hdmi_writel(start, base + HDMI_AHB_DMA_STRADDR0);
	dw_hdmi_writel(stop, base + HDMI_AHB_DMA_STPADDR0);

	writeb_relaxed((u8)~HDMI_AHB_DMA_MASK_DONE, base + HDMI_AHB_DMA_MASK);
	writeb(HDMI_AHB_DMA_START_START, base + HDMI_AHB_DMA_START);

	offset += period;
	if (offset >= dw->buf_size)
		offset = 0;
	dw->buf_offset = offset;
}

static void dw_hdmi_stop_dma(struct snd_dw_hdmi *dw)
{
	/* Disable interrupts before disabling DMA */
	writeb_relaxed(~0, dw->data.base + HDMI_AHB_DMA_MASK);
	writeb_relaxed(HDMI_AHB_DMA_STOP_STOP, dw->data.base + HDMI_AHB_DMA_STOP);
}

static irqreturn_t snd_dw_hdmi_irq(int irq, void *data)
{
	struct snd_dw_hdmi *dw = data;
	struct snd_pcm_substream *substream;
	unsigned stat;

	stat = readb_relaxed(dw->data.base + HDMI_IH_AHBDMAAUD_STAT0);
	if (!stat)
		return IRQ_NONE;

	writeb_relaxed(stat, dw->data.base + HDMI_IH_AHBDMAAUD_STAT0);

	substream = dw->substream;
	if (stat & HDMI_IH_AHBDMAAUD_STAT0_DONE && substream) {
		snd_pcm_period_elapsed(substream);

		spin_lock(&dw->lock);
		if (dw->substream)
			dw_hdmi_start_dma(dw);
		spin_unlock(&dw->lock);
	}

	return IRQ_HANDLED;
}

static struct snd_pcm_hardware dw_hdmi_hw = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE |
		   SNDRV_PCM_FMTBIT_S24_LE,
	.rates = SNDRV_PCM_RATE_32000 |
		 SNDRV_PCM_RATE_44100 |
		 SNDRV_PCM_RATE_48000 |
		 SNDRV_PCM_RATE_88200 |
		 SNDRV_PCM_RATE_96000 |
		 SNDRV_PCM_RATE_176400 |
		 SNDRV_PCM_RATE_192000,
	.channels_min = 2,
	.channels_max = 8,
	.buffer_bytes_max = 1024 * 1024,
	.period_bytes_min = 256,
	.period_bytes_max = 8192,	/* ERR004323: must limit to 8k */
	.periods_min = 2,
	.periods_max = 16,
	.fifo_size = 0,
};

static int dw_hdmi_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_dw_hdmi *dw = substream->private_data;
	void __iomem *base = dw->data.base;
	int ret;

	runtime->hw = dw_hdmi_hw;

	ret = snd_pcm_hw_constraint_eld(runtime, dw->data.eld);
	if (ret < 0)
		return ret;

	ret = snd_pcm_limit_hw_rates(runtime);
	if (ret < 0)
		return ret;

	ret = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		return ret;

	/* Limit the buffer size to the size of the preallocated buffer */
	ret = snd_pcm_hw_constraint_minmax(runtime,
					   SNDRV_PCM_HW_PARAM_BUFFER_SIZE,
					   0, substream->dma_buffer.bytes);
	if (ret < 0)
		return ret;

	/* Clear FIFO */
	writeb_relaxed(HDMI_AHB_DMA_CONF0_SW_FIFO_RST,
		       base + HDMI_AHB_DMA_CONF0);

	/* Configure interrupt polarities */
	writeb_relaxed(~0, base + HDMI_AHB_DMA_POL);
	writeb_relaxed(~0, base + HDMI_AHB_DMA_BUFFPOL);

	/* Keep interrupts masked, and clear any pending */
	writeb_relaxed(~0, base + HDMI_AHB_DMA_MASK);
	writeb_relaxed(~0, base + HDMI_IH_AHBDMAAUD_STAT0);

	ret = request_irq(dw->data.irq, snd_dw_hdmi_irq, IRQF_SHARED,
			  "dw-hdmi-audio", dw);
	if (ret)
		return ret;

	/* Un-mute done interrupt */
	writeb_relaxed(HDMI_IH_MUTE_AHBDMAAUD_STAT0_ALL &
		       ~HDMI_IH_MUTE_AHBDMAAUD_STAT0_DONE,
		       base + HDMI_IH_MUTE_AHBDMAAUD_STAT0);

	return 0;
}

static int dw_hdmi_close(struct snd_pcm_substream *substream)
{
	struct snd_dw_hdmi *dw = substream->private_data;

	/* Mute all interrupts */
	writeb_relaxed(HDMI_IH_MUTE_AHBDMAAUD_STAT0_ALL,
		       dw->data.base + HDMI_IH_MUTE_AHBDMAAUD_STAT0);

	free_irq(dw->data.irq, dw);

	return 0;
}

static int dw_hdmi_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int dw_hdmi_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	/* Allocate the PCM runtime buffer, which is exposed to userspace. */
	return snd_pcm_lib_alloc_vmalloc_buffer(substream,
						params_buffer_bytes(params));
}

static int dw_hdmi_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_dw_hdmi *dw = substream->private_data;
	u8 threshold, conf0, conf1, layout, ca;

	/* Setup as per 3.0.5 FSL 4.1.0 BSP */
	switch (dw->revision) {
	case 0x0a:
		conf0 = HDMI_AHB_DMA_CONF0_BURST_MODE |
			HDMI_AHB_DMA_CONF0_INCR4;
		if (runtime->channels == 2)
			threshold = 126;
		else
			threshold = 124;
		break;
	case 0x1a:
		conf0 = HDMI_AHB_DMA_CONF0_BURST_MODE |
			HDMI_AHB_DMA_CONF0_INCR8;
		threshold = 128;
		break;
	default:
		/* NOTREACHED */
		return -EINVAL;
	}

	dw_hdmi_set_sample_rate(dw->data.hdmi, runtime->rate);

	/* Minimum number of bytes in the fifo. */
	runtime->hw.fifo_size = threshold * 32;

	conf0 |= HDMI_AHB_DMA_CONF0_EN_HLOCK;
	conf1 = default_hdmi_channel_config[runtime->channels - 2].conf1;
	ca = default_hdmi_channel_config[runtime->channels - 2].ca;

	/*
	 * For >2 channel PCM audio, we need to select layout 1
	 * and set an appropriate channel map.
	 */
	if (runtime->channels > 2)
		layout = HDMI_FC_AUDSCONF_LAYOUT1;
	else
		layout = HDMI_FC_AUDSCONF_LAYOUT0;

	writeb_relaxed(threshold, dw->data.base + HDMI_AHB_DMA_THRSLD);
	writeb_relaxed(conf0, dw->data.base + HDMI_AHB_DMA_CONF0);
	writeb_relaxed(conf1, dw->data.base + HDMI_AHB_DMA_CONF1);
	writeb_relaxed(layout, dw->data.base + HDMI_FC_AUDSCONF);
	writeb_relaxed(ca, dw->data.base + HDMI_FC_AUDICONF2);

	switch (runtime->format) {
	case SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE:
		dw->reformat = dw_hdmi_reformat_iec958;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		dw_hdmi_create_cs(dw, runtime);
		dw->reformat = dw_hdmi_reformat_s24;
		break;
	}
	dw->iec_offset = 0;
	dw->channels = runtime->channels;
	dw->buf_src  = runtime->dma_area;
	dw->buf_dst  = substream->dma_buffer.area;
	dw->buf_addr = substream->dma_buffer.addr;
	dw->buf_period = snd_pcm_lib_period_bytes(substream);
	dw->buf_size = snd_pcm_lib_buffer_bytes(substream);

	return 0;
}

static int dw_hdmi_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_dw_hdmi *dw = substream->private_data;
	unsigned long flags;
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		spin_lock_irqsave(&dw->lock, flags);
		dw->buf_offset = 0;
		dw->substream = substream;
		dw_hdmi_start_dma(dw);
		dw_hdmi_audio_enable(dw->data.hdmi);
		spin_unlock_irqrestore(&dw->lock, flags);
		substream->runtime->delay = substream->runtime->period_size;
		break;

	case SNDRV_PCM_TRIGGER_STOP:
		spin_lock_irqsave(&dw->lock, flags);
		dw->substream = NULL;
		dw_hdmi_stop_dma(dw);
		dw_hdmi_audio_disable(dw->data.hdmi);
		spin_unlock_irqrestore(&dw->lock, flags);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static snd_pcm_uframes_t dw_hdmi_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_dw_hdmi *dw = substream->private_data;

	/*
	 * We are unable to report the exact hardware position as
	 * reading the 32-bit DMA position using 8-bit reads is racy.
	 */
	return bytes_to_frames(runtime, dw->buf_offset);
}

static struct snd_pcm_ops snd_dw_hdmi_ops = {
	.open = dw_hdmi_open,
	.close = dw_hdmi_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = dw_hdmi_hw_params,
	.hw_free = dw_hdmi_hw_free,
	.prepare = dw_hdmi_prepare,
	.trigger = dw_hdmi_trigger,
	.pointer = dw_hdmi_pointer,
	.page = snd_pcm_lib_get_vmalloc_page,
};

static int snd_dw_hdmi_probe(struct platform_device *pdev)
{
	const struct dw_hdmi_audio_data *data = pdev->dev.platform_data;
	struct device *dev = pdev->dev.parent;
	struct snd_dw_hdmi *dw;
	struct snd_card *card;
	struct snd_pcm *pcm;
	unsigned revision;
	int ret;

	writeb_relaxed(HDMI_IH_MUTE_AHBDMAAUD_STAT0_ALL,
		       data->base + HDMI_IH_MUTE_AHBDMAAUD_STAT0);
	revision = readb_relaxed(data->base + HDMI_REVISION_ID);
	if (revision != 0x0a && revision != 0x1a) {
		dev_err(dev, "dw-hdmi-audio: unknown revision 0x%02x\n",
			revision);
		return -ENXIO;
	}

	ret = snd_card_new(dev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			      THIS_MODULE, sizeof(struct snd_dw_hdmi), &card);
	if (ret < 0)
		return ret;

	strlcpy(card->driver, DRIVER_NAME, sizeof(card->driver));
	strlcpy(card->shortname, "DW-HDMI", sizeof(card->shortname));
	snprintf(card->longname, sizeof(card->longname),
		 "%s rev 0x%02x, irq %d", card->shortname, revision,
		 data->irq);

	dw = card->private_data;
	dw->card = card;
	dw->data = *data;
	dw->revision = revision;

	spin_lock_init(&dw->lock);

	ret = snd_pcm_new(card, "DW HDMI", 0, 1, 0, &pcm);
	if (ret < 0)
		goto err;

	dw->pcm = pcm;
	pcm->private_data = dw;
	strlcpy(pcm->name, DRIVER_NAME, sizeof(pcm->name));
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_dw_hdmi_ops);

	/*
	 * To support 8-channel 96kHz audio reliably, we need 512k
	 * to satisfy alsa with our restricted period (ERR004323).
	 */
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
			dev, 128 * 1024, 1024 * 1024);

	ret = snd_card_register(card);
	if (ret < 0)
		goto err;

	platform_set_drvdata(pdev, dw);

	return 0;

err:
	snd_card_free(card);
	return ret;
}

static int snd_dw_hdmi_remove(struct platform_device *pdev)
{
	struct snd_dw_hdmi *dw = platform_get_drvdata(pdev);

	snd_card_free(dw->card);

	return 0;
}

#if defined(CONFIG_PM_SLEEP) && defined(IS_NOT_BROKEN)
/*
 * This code is fine, but requires implementation in the dw_hdmi_trigger()
 * method which is currently missing as I have no way to test this.
 */
static int snd_dw_hdmi_suspend(struct device *dev)
{
	struct snd_dw_hdmi *dw = dev_get_drvdata(dev);

	snd_power_change_state(dw->card, SNDRV_CTL_POWER_D3cold);
	snd_pcm_suspend_all(dw->pcm);

	return 0;
}

static int snd_dw_hdmi_resume(struct device *dev)
{
	struct snd_dw_hdmi *dw = dev_get_drvdata(dev);

	snd_power_change_state(dw->card, SNDRV_CTL_POWER_D0);

	return 0;
}

static SIMPLE_DEV_PM_OPS(snd_dw_hdmi_pm, snd_dw_hdmi_suspend,
			 snd_dw_hdmi_resume);
#define PM_OPS &snd_dw_hdmi_pm
#else
#define PM_OPS NULL
#endif

static struct platform_driver snd_dw_hdmi_driver = {
	.probe	= snd_dw_hdmi_probe,
	.remove	= snd_dw_hdmi_remove,
	.driver	= {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.pm = PM_OPS,
	},
};

module_platform_driver(snd_dw_hdmi_driver);

MODULE_AUTHOR("Russell King <rmk+kernel@arm.linux.org.uk>");
MODULE_DESCRIPTION("Synopsis Designware HDMI AHB ALSA interface");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
