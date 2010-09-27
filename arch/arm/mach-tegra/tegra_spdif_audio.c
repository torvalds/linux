/*
 * arch/arm/mach-tegra/tegra_spdif_audio.c
 *
 * S/PDIF audio driver for NVIDIA Tegra SoCs
 *
 * Copyright (c) 2008-2009, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/err.h>
#include <linux/spi/spi.h>
#include <linux/kfifo.h>
#include <linux/debugfs.h>
#include <linux/completion.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/sysfs.h>

#include <linux/tegra_audio.h>

#include <mach/dma.h>
#include <mach/iomap.h>
#include <mach/spdif.h>
#include <mach/audio.h>
#include <mach/irqs.h>

#include "clock.h"


/* per stream (input/output) */
struct audio_stream {
	int opened;
	struct mutex lock;

	struct tegra_audio_buf_config buf_config;
	bool active; /* is DMA or PIO in progress? */
	void *buffer;
	dma_addr_t buf_phys;
	struct kfifo fifo;
	struct completion fifo_completion;

	unsigned errors;

	int spdif_fifo_atn_level;

	ktime_t last_dma_ts;
	struct tegra_dma_channel *dma_chan;
	struct completion stop_completion;
	spinlock_t dma_req_lock; /* guards dma_has_it */
	int dma_has_it;
	struct tegra_dma_req dma_req;
};

struct spdif_pio_stats {
	u32 spdif_interrupt_count;
	u32 tx_fifo_errors;
	u32 tx_fifo_written;
};


struct audio_driver_state {
	struct list_head next;

	struct platform_device *pdev;
	struct tegra_audio_platform_data *pdata;
	phys_addr_t spdif_phys;
	unsigned long spdif_base;

	bool using_dma;
	unsigned long dma_req_sel;

	int irq; /* for pio mode */
	struct spdif_pio_stats pio_stats;
	const int *in_divs;
	int in_divs_len;

	struct miscdevice misc_out;
	struct miscdevice misc_out_ctl;
	struct audio_stream out;
};

static inline int buf_size(struct audio_stream *s)
{
	return 1 << s->buf_config.size;
}

static inline int chunk_size(struct audio_stream *s)
{
	return 1 << s->buf_config.chunk;
}

static inline int threshold_size(struct audio_stream *s)
{
	return 1 << s->buf_config.threshold;
}

static inline struct audio_driver_state *ads_from_misc_out(struct file *file)
{
	struct miscdevice *m = file->private_data;
	struct audio_driver_state *ads =
			container_of(m, struct audio_driver_state, misc_out);
	BUG_ON(!ads);
	return ads;
}

static inline struct audio_driver_state *ads_from_misc_out_ctl(
		struct file *file)
{
	struct miscdevice *m = file->private_data;
	struct audio_driver_state *ads =
			container_of(m, struct audio_driver_state,
					misc_out_ctl);
	BUG_ON(!ads);
	return ads;
}

static inline struct audio_driver_state *ads_from_out(
			struct audio_stream *aos)
{
	return container_of(aos, struct audio_driver_state, out);
}

static inline void spdif_writel(unsigned long base, u32 val, u32 reg)
{
	writel(val, base + reg);
}

static inline u32 spdif_readl(unsigned long base, u32 reg)
{
	return readl(base + reg);
}

static inline void spdif_fifo_write(unsigned long base, u32 data)
{
	spdif_writel(base, data, SPDIF_DATA_OUT_0);
}

static int spdif_fifo_set_attention_level(unsigned long base,
			unsigned level)
{
	u32 val;

	if (level > SPDIF_FIFO_ATN_LVL_TWELVE_SLOTS) {
		pr_err("%s: invalid fifo level selector %d\n", __func__,
			level);
		return -EINVAL;
	}

	val = spdif_readl(base, SPDIF_DATA_FIFO_CSR_0);

	val &= ~SPDIF_DATA_FIFO_CSR_0_TX_ATN_LVL_MASK;
	val |= level << SPDIF_DATA_FIFO_CSR_0_TX_ATN_LVL_SHIFT;


	spdif_writel(base, val, SPDIF_DATA_FIFO_CSR_0);
	return 0;
}

static void spdif_fifo_enable(unsigned long base, int on)
{
	u32 val = spdif_readl(base, SPDIF_CTRL_0);
	val &= ~(SPDIF_CTRL_0_TX_EN | SPDIF_CTRL_0_TC_EN);
	val |= on ? (SPDIF_CTRL_0_TX_EN) : 0;
	val |= on ? (SPDIF_CTRL_0_TC_EN) : 0;

	spdif_writel(base, val, SPDIF_CTRL_0);
}

static bool spdif_is_fifo_enabled(unsigned long base)
{
	u32 val = spdif_readl(base, SPDIF_CTRL_0);
	return !!(val & SPDIF_CTRL_0_TX_EN);
}

static void spdif_fifo_clear(unsigned long base)
{
	u32 val = spdif_readl(base, SPDIF_DATA_FIFO_CSR_0);
	val &= ~(SPDIF_DATA_FIFO_CSR_0_TX_CLR | SPDIF_DATA_FIFO_CSR_0_TU_CLR);
	val |= SPDIF_DATA_FIFO_CSR_0_TX_CLR | SPDIF_DATA_FIFO_CSR_0_TU_CLR;
	spdif_writel(base, val, SPDIF_DATA_FIFO_CSR_0);
}


static int spdif_set_bit_mode(unsigned long base, unsigned mode)
{
	u32 val = spdif_readl(base, SPDIF_CTRL_0);
	val &= ~SPDIF_CTRL_0_BIT_MODE_MASK;

	if (mode > SPDIF_BIT_MODE_MODERAW) {
		pr_err("%s: invalid bit_size selector %d\n", __func__,
			mode);
		return -EINVAL;
	}

	val |= mode << SPDIF_CTRL_0_BIT_MODE_SHIFT;

	spdif_writel(base, val, SPDIF_CTRL_0);
	return 0;
}

static int spdif_set_fifo_packed(unsigned long base, unsigned on)
{
	u32 val = spdif_readl(base, SPDIF_CTRL_0);
	val &= ~SPDIF_CTRL_0_PACK;
	val |= on ? SPDIF_CTRL_0_PACK : 0;
	spdif_writel(base, val, SPDIF_CTRL_0);
	return 0;
}


static void spdif_set_fifo_irq_on_err(unsigned long base, int on)
{
	u32 val = spdif_readl(base, SPDIF_CTRL_0);
	val &= ~SPDIF_CTRL_0_IE_TXE;
	val |= on ? SPDIF_CTRL_0_IE_TXE : 0;
	spdif_writel(base, val, SPDIF_CTRL_0);
}



static void spdif_enable_fifos(unsigned long base, int on)
{
	u32 val = spdif_readl(base, SPDIF_CTRL_0);
	if (on)
		val |= SPDIF_CTRL_0_TX_EN | SPDIF_CTRL_0_TC_EN |
		       SPDIF_CTRL_0_IE_TXE;
	else
		val &= ~(SPDIF_CTRL_0_TX_EN | SPDIF_CTRL_0_TC_EN |
			 SPDIF_CTRL_0_IE_TXE);

	spdif_writel(base, val, SPDIF_CTRL_0);
}

static inline u32 spdif_get_status(unsigned long base)
{
	return spdif_readl(base, SPDIF_STATUS_0);
}

static inline u32 spdif_get_control(unsigned long base)
{
	return spdif_readl(base, SPDIF_CTRL_0);
}

static inline void spdif_ack_status(unsigned long base)
{
	return spdif_writel(base, spdif_readl(base, SPDIF_STATUS_0),
				SPDIF_STATUS_0);
}

static inline u32 spdif_get_fifo_scr(unsigned long base)
{
	return spdif_readl(base, SPDIF_DATA_FIFO_CSR_0);
}

static inline phys_addr_t spdif_get_fifo_phy_base(unsigned long phy_base)
{
	return phy_base + SPDIF_DATA_OUT_0;
}

static inline u32 spdif_get_fifo_full_empty_count(unsigned long base)
{
	u32 val = spdif_readl(base, SPDIF_DATA_FIFO_CSR_0);
	val = val >> SPDIF_DATA_FIFO_CSR_0_TD_EMPTY_COUNT_SHIFT;
	return val & SPDIF_DATA_FIFO_CSR_0_TD_EMPTY_COUNT_MASK;
}


static int spdif_set_sample_rate(struct audio_driver_state *state,
				unsigned int sample_rate)
{
	unsigned int clock_freq = 0;
	unsigned int parent_clock_freq = 0;
	struct clk *spdif_clk;

	unsigned int ch_sta[] = {
		0x0, /* 44.1, default values */
		0xf << 4, /* bits 36-39, original sample freq -- 44.1 */
		0x0,
		0x0,
		0x0,
		0x0,
	};

	ch_sta[0] = 0x0;
	ch_sta[1] = 0xf << 4;

	switch (sample_rate) {
	case 32000:
		clock_freq = 4096000; /* 4.0960 MHz */
		parent_clock_freq = 12288000;
		ch_sta[0] = 0x3 << 24;
		ch_sta[1] = 0xC << 4;
		break;
	case 44100:
		clock_freq = 5644800; /* 5.6448 MHz */
		parent_clock_freq = 11289600;
		ch_sta[0] = 0x0;
		ch_sta[1] = 0xF << 4;
		break;
	case 48000:
		clock_freq = 6144000; /* 6.1440MHz */
		parent_clock_freq = 12288000;
		ch_sta[0] = 0x2 << 24;
		ch_sta[1] = 0xD << 4;
		break;
	case 88200:
		clock_freq = 11289600; /* 11.2896 MHz */
		parent_clock_freq = 11289600;
		break;
	case 96000:
		clock_freq = 12288000; /* 12.288 MHz */
		parent_clock_freq = 12288000;
		break;
	case 176400:
		clock_freq = 22579200; /* 22.5792 MHz */
		parent_clock_freq = 11289600;
		break;
	case 192000:
		clock_freq = 24576000; /* 24.5760 MHz */
		parent_clock_freq = 12288000;
		break;
	default:
		return -1;
	}

	spdif_clk = clk_get(&state->pdev->dev, NULL);
	if (!spdif_clk) {
		dev_err(&state->pdev->dev, "%s: could not get spdif clock\n",
			__func__);
		return -EIO;
	}

	clk_set_rate(spdif_clk, clock_freq);
	if (clk_enable(spdif_clk)) {
		dev_err(&state->pdev->dev, "%s: failed to enable spdif_clk"\
			" clock\n", __func__);
		return -EIO;
	}
	pr_info("%s: spdif_clk rate %ld\n", __func__, clk_get_rate(spdif_clk));

	spdif_writel(state->spdif_base, ch_sta[0], SPDIF_CH_STA_TX_A_0);
	spdif_writel(state->spdif_base, ch_sta[1], SPDIF_CH_STA_TX_B_0);
	spdif_writel(state->spdif_base, ch_sta[2], SPDIF_CH_STA_TX_C_0);
	spdif_writel(state->spdif_base, ch_sta[3], SPDIF_CH_STA_TX_D_0);
	spdif_writel(state->spdif_base, ch_sta[4], SPDIF_CH_STA_TX_E_0);
	spdif_writel(state->spdif_base, ch_sta[5], SPDIF_CH_STA_TX_F_0);

	return 0;
}


#define PCM_IN_BUFFER_PADDING		(1<<6) /* bytes */
#define PCM_BUFFER_MAX_SIZE_ORDER	(PAGE_SHIFT + 2)
#define PCM_BUFFER_DMA_CHUNK_SIZE_ORDER	(PCM_BUFFER_MAX_SIZE_ORDER - 1)
#define PCM_BUFFER_THRESHOLD_ORDER	PCM_BUFFER_DMA_CHUNK_SIZE_ORDER
#define PCM_DMA_CHUNK_MIN_SIZE_ORDER	3

static int init_stream_buffer(struct audio_stream *,
		struct tegra_audio_buf_config *cfg, unsigned);

static int setup_dma(struct audio_driver_state *);
static void tear_down_dma(struct audio_driver_state *);
static int start_dma_playback(struct audio_stream *);
static void stop_dma_playback(struct audio_stream *);

static int setup_pio(struct audio_driver_state *);
static void tear_down_pio(struct audio_driver_state *);
static int start_pio_playback(struct audio_stream *);
static void stop_pio_playback(struct audio_stream *);


struct sound_ops {
	int (*setup)(struct audio_driver_state *);
	void (*tear_down)(struct audio_driver_state *);
	int (*start_playback)(struct audio_stream *);
	void (*stop_playback)(struct audio_stream *);
};

static const struct sound_ops dma_sound_ops = {
	.setup = setup_dma,
	.tear_down = tear_down_dma,
	.start_playback = start_dma_playback,
	.stop_playback = stop_dma_playback,
};

static const struct sound_ops pio_sound_ops = {
	.setup = setup_pio,
	.tear_down = tear_down_pio,
	.start_playback = start_pio_playback,
	.stop_playback = stop_pio_playback,
};

static const struct sound_ops *sound_ops = &dma_sound_ops;

static int start_playback(struct audio_stream *aos)
{
	int rc;
	unsigned long flags;
	spin_lock_irqsave(&aos->dma_req_lock, flags);
	pr_debug("%s: starting playback\n", __func__);
	rc = sound_ops->start_playback(aos);
	spin_unlock_irqrestore(&aos->dma_req_lock, flags);
	return rc;
}


static bool stop_playback_if_necessary(struct audio_stream *aos)
{
	unsigned long flags;
	spin_lock_irqsave(&aos->dma_req_lock, flags);
	if (kfifo_is_empty(&aos->fifo)) {
		sound_ops->stop_playback(aos);
		if (aos->active)
			aos->errors++;
		spin_unlock_irqrestore(&aos->dma_req_lock, flags);
		return true;
	}
	spin_unlock_irqrestore(&aos->dma_req_lock, flags);

	return false;
}


static void toggle_dma(struct audio_driver_state *ads)
{
	pr_info("%s: %s\n", __func__, ads->using_dma ? "pio" : "dma");
	sound_ops->tear_down(ads);
	sound_ops = ads->using_dma ? &pio_sound_ops : &dma_sound_ops;
	sound_ops->setup(ads);
	ads->using_dma = !ads->using_dma;
}

/* DMA */

static int resume_dma_playback(struct audio_stream *aos);

static void setup_dma_tx_request(struct tegra_dma_req *req,
		struct audio_stream *aos);

static int setup_dma(struct audio_driver_state *ads)
{
	int rc;
	pr_info("%s\n", __func__);

	/* setup audio playback */
	ads->out.buf_phys = dma_map_single(&ads->pdev->dev, ads->out.buffer,
				1 << PCM_BUFFER_MAX_SIZE_ORDER, DMA_TO_DEVICE);
	BUG_ON(!ads->out.buf_phys);
	setup_dma_tx_request(&ads->out.dma_req, &ads->out);
	ads->out.dma_chan = tegra_dma_allocate_channel(TEGRA_DMA_MODE_ONESHOT);
	if (!ads->out.dma_chan) {
		pr_err("%s: could not allocate output SPDIF DMA channel: %ld\n",
			__func__, PTR_ERR(ads->out.dma_chan));
		rc = -ENODEV;
		goto fail_tx;
	}
	return 0;


fail_tx:
	dma_unmap_single(&ads->pdev->dev, ads->out.buf_phys,
			1 << PCM_BUFFER_MAX_SIZE_ORDER, DMA_TO_DEVICE);
	tegra_dma_free_channel(ads->out.dma_chan);
	ads->out.dma_chan = 0;

	return rc;
}

static void tear_down_dma(struct audio_driver_state *ads)
{
	pr_info("%s\n", __func__);

	tegra_dma_free_channel(ads->out.dma_chan);
	ads->out.dma_chan = NULL;
	dma_unmap_single(&ads->pdev->dev, ads->out.buf_phys,
				buf_size(&ads->out),
				DMA_TO_DEVICE);
	ads->out.buf_phys = 0;

}

static void dma_tx_complete_callback(struct tegra_dma_req *req)
{
	unsigned long flags;
	struct audio_stream *aos = req->dev;
	int count = req->bytes_transferred;
	u64 delta_us;
	u64 max_delay_us = count * 10000 / (4 * 441);

	pr_debug("%s bytes transferred %d\n", __func__, count);

	aos->dma_has_it = false;
	delta_us = ktime_to_us(ktime_sub(ktime_get_real(), aos->last_dma_ts));

	if (delta_us > max_delay_us) {
		pr_debug("%s: too late by %lld us\n", __func__,
			delta_us - max_delay_us);
		aos->errors++;
	}

	kfifo_skip(&aos->fifo, count);

	if (kfifo_avail(&aos->fifo) >= threshold_size(aos) &&
			!completion_done(&aos->fifo_completion)) {
		pr_debug("%s: complete (%d avail)\n", __func__,
				kfifo_avail(&aos->fifo));
		complete(&aos->fifo_completion);
	}

	if (stop_playback_if_necessary(aos))
		return;
	spin_lock_irqsave(&aos->dma_req_lock, flags);
	resume_dma_playback(aos);
	spin_unlock_irqrestore(&aos->dma_req_lock, flags);
}

static void setup_dma_tx_request(struct tegra_dma_req *req,
		struct audio_stream *aos)
{
	struct audio_driver_state *ads = ads_from_out(aos);

	memset(req, 0, sizeof(*req));

	req->complete = dma_tx_complete_callback;
	req->dev = aos;
	req->to_memory = false;
	req->dest_addr = spdif_get_fifo_phy_base(ads->spdif_phys);
	req->dest_wrap = 4;
	req->dest_bus_width = 16;
	req->source_bus_width = 32;
	req->source_wrap = 0;
	req->req_sel = ads->dma_req_sel;
}


/* Called with aos->dma_req_lock taken. */
static int resume_dma_playback(struct audio_stream *aos)
{
	int rc;
	struct audio_driver_state *ads = ads_from_out(aos);
	struct tegra_dma_req *req = &aos->dma_req;

	unsigned out, in;

	out = __kfifo_off(&aos->fifo, aos->fifo.out);
	in = __kfifo_off(&aos->fifo, aos->fifo.in);

	/* stop_playback_if_necessary() already checks to see if the fifo is
	 * empty.
	 */
	BUG_ON(!kfifo_len(&aos->fifo));

	if (aos->dma_has_it) {
		pr_debug("%s: playback already in progress\n", __func__);
		return 0;
	}

#if 0
	spdif_fifo_clear(ads->spdif_base);
#endif
	spdif_fifo_set_attention_level(ads->spdif_base,
			aos->spdif_fifo_atn_level);

	req->source_addr = aos->buf_phys + out;
	if (out < in)
		req->size = in - out;
	else
		req->size = kfifo_size(&aos->fifo) - out;

	dma_sync_single_for_device(NULL,
			aos->buf_phys + out, req->size, DMA_TO_DEVICE);

	/* Don't send all the data yet. */
	if (req->size > chunk_size(aos))
		req->size = chunk_size(aos);
	pr_debug("%s resume playback (%d in fifo, writing %d, in %d out %d)\n",
			__func__, kfifo_len(&aos->fifo), req->size, in, out);

	spdif_fifo_enable(ads->spdif_base, 1);

	aos->last_dma_ts = ktime_get_real();
	rc = tegra_dma_enqueue_req(aos->dma_chan, req);
	aos->dma_has_it = !rc;
	if (!aos->dma_has_it)
		pr_err("%s: could not enqueue TX DMA req\n", __func__);
	return rc;
}

/* Called with aos->dma_req_lock taken. */
static int start_dma_playback(struct audio_stream *aos)
{
	return resume_dma_playback(aos);
}

/* Called with aos->dma_req_lock taken. */
static void stop_dma_playback(struct audio_stream *aos)
{
	int spin = 0;
	struct audio_driver_state *ads = ads_from_out(aos);
	pr_debug("%s\n", __func__);
	spdif_fifo_enable(ads->spdif_base, 0);
	while ((spdif_get_status(ads->spdif_base) & SPDIF_STATUS_0_TX_BSY) &&
			spin < 100)
		if (spin++ > 50)
			pr_info("%s: spin %d\n", __func__, spin);
	if (spin == 100)
		pr_warn("%s: spinny\n", __func__);
}

/* PIO (non-DMA) */

static int setup_pio(struct audio_driver_state *ads)
{
	pr_info("%s\n", __func__);
	enable_irq(ads->irq);
	return 0;
}

static void tear_down_pio(struct audio_driver_state *ads)
{
	pr_info("%s\n", __func__);
	disable_irq(ads->irq);
}

static int start_pio_playback(struct audio_stream *aos)
{
	struct audio_driver_state *ads = ads_from_out(aos);

	if (spdif_is_fifo_enabled(ads->spdif_base)) {
		pr_debug("%s: playback is already in progress\n", __func__);
		return 0;
	}

	pr_debug("%s\n", __func__);

	spdif_fifo_set_attention_level(ads->spdif_base,
			aos->spdif_fifo_atn_level);
#if 0
	spdif_fifo_clear(ads->spdif_base);
#endif

	spdif_set_fifo_irq_on_err(ads->spdif_base, 1);
	spdif_fifo_enable(ads->spdif_base, 1);

	return 0;
}

static void stop_pio_playback(struct audio_stream *aos)
{
	struct audio_driver_state *ads = ads_from_out(aos);

	spdif_set_fifo_irq_on_err(ads->spdif_base, 0);
	spdif_fifo_enable(ads->spdif_base, 0);
	while (spdif_get_status(ads->spdif_base) & SPDIF_STATUS_0_TX_BSY)
		/* spin */;

	pr_info("%s: interrupts %d\n", __func__,
			ads->pio_stats.spdif_interrupt_count);
	pr_info("%s: sent       %d\n", __func__,
			ads->pio_stats.tx_fifo_written);
	pr_info("%s: tx errors  %d\n", __func__,
			ads->pio_stats.tx_fifo_errors);

	memset(&ads->pio_stats, 0, sizeof(ads->pio_stats));
}


static irqreturn_t spdif_interrupt(int irq, void *data)
{
	struct audio_driver_state *ads = data;
	u32 status = spdif_get_status(ads->spdif_base);

	pr_debug("%s: %08x\n", __func__, status);

	ads->pio_stats.spdif_interrupt_count++;

	if (status & SPDIF_CTRL_0_IE_TXE)
		ads->pio_stats.tx_fifo_errors++;

#if 0
	if (status & SPDIF_STATUS_0_TX_ERR)
#endif
		spdif_ack_status(ads->spdif_base);

	if (status & SPDIF_STATUS_0_QS_TX) {
		int written;
		int empty;
		int len;
		u16 fifo_buffer[32];

		struct audio_stream *out = &ads->out;

		if (!spdif_is_fifo_enabled(ads->spdif_base)) {
			pr_debug("%s: tx fifo not enabled, skipping\n",
				__func__);
			goto done;
		}

		pr_debug("%s tx fifo is ready\n", __func__);

		if (kfifo_avail(&out->fifo) > threshold_size(out) &&
				!completion_done(&out->fifo_completion)) {
			pr_debug("%s: tx complete (%d avail)\n", __func__,
					kfifo_avail(&out->fifo));
			complete(&out->fifo_completion);
		}

		if (stop_playback_if_necessary(out))
			goto done;

		empty = spdif_get_fifo_full_empty_count(ads->spdif_base);

		len = kfifo_out(&out->fifo, fifo_buffer,
				empty * sizeof(u16));
		len /= sizeof(u16);

		written = 0;
		while (empty-- && written < len) {
			ads->pio_stats.tx_fifo_written += written * sizeof(u16);
			spdif_fifo_write(ads->spdif_base,
					fifo_buffer[written++]);
		}

		/* TODO: Should we check to see if we wrote less than the
		 * FIFO threshold and adjust it if so?
		 */

		if (written) {
			/* start the transaction */
			pr_debug("%s: enabling fifo (%d samples written)\n",
					__func__, written);
			spdif_fifo_enable(ads->spdif_base, 1);
		}
	}

done:
	pr_debug("%s: done %08x\n", __func__,
			spdif_get_status(ads->spdif_base));
	return IRQ_HANDLED;
}

static ssize_t tegra_spdif_write(struct file *file,
		const char __user *buf, size_t size, loff_t *off)
{
	ssize_t rc, total = 0;
	unsigned nw = 0;

	struct audio_driver_state *ads = ads_from_misc_out(file);

	mutex_lock(&ads->out.lock);

	ads->out.active = true;

	if (!IS_ALIGNED(size, 4)) {
		pr_err("%s: user size request %d not aligned to 4\n",
			__func__, size);
		rc = -EINVAL;
		goto done;
	}

	pr_debug("%s: write %d bytes, %d available\n", __func__,
			size, kfifo_avail(&ads->out.fifo));

again:
	rc = kfifo_from_user(&ads->out.fifo, buf + total, size - total, &nw);
	if (rc < 0) {
		pr_err("%s: error copying from user\n", __func__);
		goto done;
	}

	rc = start_playback(&ads->out);
	if (rc < 0) {
		pr_err("%s: could not start playback: %d\n", __func__, rc);
		goto done;
	}

	total += nw;
	if (total < size) {
		pr_debug("%s: sleep (user %d total %d nw %d)\n", __func__,
				size, total, nw);
		mutex_unlock(&ads->out.lock);
		rc = wait_for_completion_interruptible(
				&ads->out.fifo_completion);
		mutex_lock(&ads->out.lock);
		if (rc == -ERESTARTSYS) {
			pr_warn("%s: interrupted\n", __func__);
			goto done;
		}
		pr_debug("%s: awake\n", __func__);
		goto again;
	}

	rc = total;
	*off += total;

done:
	ads->out.active = false;
	mutex_unlock(&ads->out.lock);
	return rc;
}

static long tegra_spdif_out_ioctl(struct file *file,
			unsigned int cmd, unsigned long arg)
{
	int rc = 0;
	struct audio_driver_state *ads = ads_from_misc_out_ctl(file);
	struct audio_stream *aos = &ads->out;

	mutex_lock(&aos->lock);

	switch (cmd) {
	case TEGRA_AUDIO_OUT_SET_BUF_CONFIG: {
		struct tegra_audio_buf_config cfg;
		if (copy_from_user(&cfg, (void __user *)arg, sizeof(cfg))) {
			rc = -EFAULT;
			break;
		}
		if (aos->active) {
			pr_err("%s: playback in progress\n", __func__);
			rc = -EBUSY;
			break;
		}
		rc = init_stream_buffer(aos, &cfg, 0);
		if (rc < 0)
			break;
		aos->buf_config = cfg;
	}
		break;
	case TEGRA_AUDIO_OUT_GET_BUF_CONFIG:
		if (copy_to_user((void __user *)arg, &aos->buf_config,
				sizeof(aos->buf_config)))
			rc = -EFAULT;
		break;
	case TEGRA_AUDIO_OUT_GET_ERROR_COUNT:
		if (copy_to_user((void __user *)arg, &aos->errors,
				sizeof(aos->errors)))
			rc = -EFAULT;
		if (!rc)
			aos->errors = 0;
		break;
	case TEGRA_AUDIO_OUT_PRELOAD_FIFO: {
		struct tegra_audio_out_preload preload;
		if (copy_from_user(&preload, (void __user *)arg,
				sizeof(preload))) {
			rc = -EFAULT;
			break;
		}
		rc = kfifo_from_user(&ads->out.fifo,
				(void __user *)preload.data, preload.len,
				&preload.len_written);
		if (rc < 0) {
			pr_err("%s: error copying from user\n", __func__);
			break;
		}
		if (copy_to_user((void __user *)arg, &preload, sizeof(preload)))
			rc = -EFAULT;
		pr_info("%s: preloaded output fifo with %d bytes\n", __func__,
			preload.len_written);
	}
		break;
	default:
		rc = -EINVAL;
	}

	mutex_unlock(&aos->lock);
	return rc;
}



static int tegra_spdif_out_open(struct inode *inode, struct file *file)
{
	int rc = 0;
	struct audio_driver_state *ads = ads_from_misc_out(file);

	pr_info("%s\n", __func__);

	mutex_lock(&ads->out.lock);
	if (!ads->out.opened++) {
		pr_info("%s: resetting fifo and error count\n", __func__);
		ads->out.errors = 0;
		kfifo_reset(&ads->out.fifo);
	}
	rc = spdif_set_sample_rate(ads, 44100);

	mutex_unlock(&ads->out.lock);

	return rc;
}

static int tegra_spdif_out_release(struct inode *inode, struct file *file)
{
	struct audio_driver_state *ads = ads_from_misc_out(file);
	struct clk *spdif_clk;

	pr_info("%s\n", __func__);

	mutex_lock(&ads->out.lock);
	if (ads->out.opened)
		ads->out.opened--;
	if (!ads->out.opened)
		stop_playback_if_necessary(&ads->out);

	spdif_clk = clk_get(&ads->pdev->dev, NULL);
	if (!spdif_clk) {
		dev_err(&ads->pdev->dev, "%s: could not get spdif clock\n",
			__func__);
		return -EIO;
	}
	clk_disable(spdif_clk);
	mutex_unlock(&ads->out.lock);

	return 0;
}

static const struct file_operations tegra_spdif_out_fops = {
	.owner = THIS_MODULE,
	.open = tegra_spdif_out_open,
	.release = tegra_spdif_out_release,
	.write = tegra_spdif_write,
};

static int tegra_spdif_ctl_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int tegra_spdif_ctl_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations tegra_spdif_out_ctl_fops = {
	.owner = THIS_MODULE,
	.open = tegra_spdif_ctl_open,
	.release = tegra_spdif_ctl_release,
	.unlocked_ioctl = tegra_spdif_out_ioctl,
};

static int init_stream_buffer(struct audio_stream *s,
				struct tegra_audio_buf_config *cfg,
				unsigned padding)
{
	pr_info("%s (size %d threshold %d chunk %d)\n", __func__,
		cfg->size, cfg->threshold, cfg->chunk);

	if (cfg->chunk < PCM_DMA_CHUNK_MIN_SIZE_ORDER) {
		pr_err("%s: chunk %d too small (%d min)\n", __func__,
			cfg->chunk, PCM_DMA_CHUNK_MIN_SIZE_ORDER);
		return -EINVAL;
	}

	if (cfg->chunk > cfg->size) {
		pr_err("%s: chunk %d > size %d\n", __func__,
				cfg->chunk, cfg->size);
		return -EINVAL;
	}

	if (cfg->threshold > cfg->size) {
		pr_err("%s: threshold %d > size %d\n", __func__,
				cfg->threshold, cfg->size);
		return -EINVAL;
	}

	if ((1 << cfg->size) < padding) {
		pr_err("%s: size %d < buffer padding %d (bytes)\n", __func__,
			cfg->size, padding);
		return -EINVAL;
	}

	if (cfg->size > PCM_BUFFER_MAX_SIZE_ORDER) {
		pr_err("%s: size %d exceeds max %d\n", __func__,
			cfg->size, PCM_BUFFER_MAX_SIZE_ORDER);
		return -EINVAL;
	}

	if (!s->buffer) {
		pr_debug("%s: allocating buffer (size %d, padding %d)\n",
				__func__, 1 << cfg->size, padding);
		s->buffer = kmalloc((1 << cfg->size) + padding,
				GFP_KERNEL | GFP_DMA);
	}
	if (!s->buffer) {
		pr_err("%s: could not allocate output buffer\n", __func__);
		return -ENOMEM;
	}

	kfifo_init(&s->fifo, s->buffer, 1 << cfg->size);
	return 0;
}


static int setup_misc_device(struct miscdevice *misc,
			const struct file_operations  *fops,
			const char *fmt, ...)
{
	int rc = 0;
	va_list args;
	const int sz = 64;

	va_start(args, fmt);

	memset(misc, 0, sizeof(*misc));
	misc->minor = MISC_DYNAMIC_MINOR;
	misc->name  = kmalloc(sz, GFP_KERNEL);
	if (!misc->name) {
		rc = -ENOMEM;
		goto done;
	}

	vsnprintf((char *)misc->name, sz, fmt, args);
	misc->fops = fops;
	if (misc_register(misc)) {
		pr_err("%s: could not register %s\n", __func__, misc->name);
		kfree(misc->name);
		rc = -EIO;
		goto done;
	}

done:
	va_end(args);
	return rc;
}

static ssize_t dma_toggle_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct tegra_audio_platform_data *pdata = dev->platform_data;
	struct audio_driver_state *ads = pdata->driver_data;
	return sprintf(buf, "%s\n", ads->using_dma ? "dma" : "pio");
}

static ssize_t dma_toggle_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	int use_dma;
	struct tegra_audio_platform_data *pdata = dev->platform_data;
	struct audio_driver_state *ads = pdata->driver_data;

	if (count < 4)
		return -EINVAL;

	use_dma = 0;
	if (!strncmp(buf, "dma", 3))
		use_dma = 1;
	else if (strncmp(buf, "pio", 3)) {
		dev_err(dev, "%s: invalid string [%s]\n", __func__, buf);
		return -EINVAL;
	}

	mutex_lock(&ads->out.lock);
	if (ads->out.active) {
		dev_err(dev, "%s: playback or recording in progress.\n",
			__func__);
		mutex_unlock(&ads->out.lock);
		return -EBUSY;
	}
	if (!!use_dma ^ !!ads->using_dma)
		toggle_dma(ads);
	else
		dev_info(dev, "%s: no change\n", __func__);
	mutex_unlock(&ads->out.lock);

	return count;
}

static DEVICE_ATTR(dma_toggle, 0644, dma_toggle_show, dma_toggle_store);

static ssize_t __attr_fifo_atn_read(char *buf, int atn_lvl)
{
	switch (atn_lvl) {
	case SPDIF_FIFO_ATN_LVL_ONE_SLOT:
		strncpy(buf, "1\n", 2);
		return 2;
	case SPDIF_FIFO_ATN_LVL_FOUR_SLOTS:
		strncpy(buf, "4\n", 2);
		return 2;
	case SPDIF_FIFO_ATN_LVL_EIGHT_SLOTS:
		strncpy(buf, "8\n", 2);
		return 2;
	case SPDIF_FIFO_ATN_LVL_TWELVE_SLOTS:
		strncpy(buf, "12\n", 3);
		return 3;
	default:
		BUG_ON(1);
		return -EIO;
	}
}

static ssize_t __attr_fifo_atn_write(struct audio_driver_state *ads,
		struct audio_stream *as,
		int *fifo_lvl,
		const char *buf, size_t size)
{
	int lvl;

	if (size > 3) {
		pr_err("%s: buffer size %d too big\n", __func__, size);
		return -EINVAL;
	}

	if (sscanf(buf, "%d", &lvl) != 1) {
		pr_err("%s: invalid input string [%s]\n", __func__, buf);
		return -EINVAL;
	}

	switch (lvl) {
	case 1:
		lvl = SPDIF_FIFO_ATN_LVL_ONE_SLOT;
		break;
	case 4:
		lvl = SPDIF_FIFO_ATN_LVL_FOUR_SLOTS;
		break;
	case 8:
		lvl = SPDIF_FIFO_ATN_LVL_EIGHT_SLOTS;
		break;
	case 12:
		lvl = SPDIF_FIFO_ATN_LVL_TWELVE_SLOTS;
		break;
	default:
		pr_err("%s: invalid attention level %d\n", __func__, lvl);
		return -EINVAL;
	}

	mutex_lock(&as->lock);
	if (as->active) {
		pr_err("%s: in progress.\n", __func__);
		mutex_unlock(&as->lock);
		return -EBUSY;
	}
	*fifo_lvl = lvl;
	pr_info("%s: fifo level %d\n", __func__, *fifo_lvl);
	mutex_unlock(&as->lock);

	return size;
}

static ssize_t tx_fifo_atn_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct tegra_audio_platform_data *pdata = dev->platform_data;
	struct audio_driver_state *ads = pdata->driver_data;
	return __attr_fifo_atn_read(buf, ads->out.spdif_fifo_atn_level);
}

static ssize_t tx_fifo_atn_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct tegra_audio_platform_data *pdata = dev->platform_data;
	struct audio_driver_state *ads = pdata->driver_data;
	return __attr_fifo_atn_write(ads, &ads->out,
			&ads->out.spdif_fifo_atn_level, buf, count);
}

static DEVICE_ATTR(tx_fifo_atn, 0644, tx_fifo_atn_show, tx_fifo_atn_store);


static int tegra_spdif_probe(struct platform_device *pdev)
{
	int rc;
	struct resource *res;
	struct audio_driver_state *state;

	pr_info("%s\n", __func__);

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->pdev = pdev;
	state->pdata = pdev->dev.platform_data;
	state->pdata->driver_data = state;
	BUG_ON(!state->pdata);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no mem resource!\n");
		return -ENODEV;
	}

	if (!request_mem_region(res->start, resource_size(res), pdev->name)) {
		dev_err(&pdev->dev, "memory region already claimed!\n");
		return -ENOMEM;
	}

	state->spdif_phys = res->start;
	state->spdif_base = (unsigned long)ioremap(res->start,
			res->end - res->start + 1);
	if (!state->spdif_base) {
		dev_err(&pdev->dev, "cannot remap iomem!\n");
		return -EIO;
	}

	state->out.spdif_fifo_atn_level = SPDIF_FIFO_ATN_LVL_FOUR_SLOTS;

	res = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (!res) {
		dev_err(&pdev->dev, "no dma resource!\n");
		return -ENODEV;
	}
	state->dma_req_sel = res->start;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "no irq resource!\n");
		return -ENODEV;
	}
	state->irq = res->start;

	memset(&state->pio_stats, 0, sizeof(state->pio_stats));

	/* disable interrupts from SPDIF */
	spdif_fifo_clear(state->spdif_base);
	spdif_enable_fifos(state->spdif_base, 0);

	spdif_set_bit_mode(state->spdif_base, state->pdata->mode);
	spdif_set_fifo_packed(state->spdif_base, state->pdata->fifo_fmt);

	state->out.opened = 0;
	state->out.active = false;
	mutex_init(&state->out.lock);
	init_completion(&state->out.fifo_completion);
	init_completion(&state->out.stop_completion);
	spin_lock_init(&state->out.dma_req_lock);
	state->out.buf_phys = 0;
	state->out.dma_chan = NULL;
	state->out.dma_has_it = false;

	state->out.buffer = 0;
	state->out.buf_config.size = PCM_BUFFER_MAX_SIZE_ORDER;
	state->out.buf_config.threshold = PCM_BUFFER_THRESHOLD_ORDER;
	state->out.buf_config.chunk = PCM_BUFFER_DMA_CHUNK_SIZE_ORDER;
	rc = init_stream_buffer(&state->out, &state->out.buf_config, 0);
	if (rc < 0)
		return rc;

	if (request_irq(state->irq, spdif_interrupt,
			IRQF_DISABLED, state->pdev->name, state) < 0) {
		dev_err(&pdev->dev,
			"%s: could not register handler for irq %d\n",
			__func__, state->irq);
		return -EIO;
	}

	rc = setup_misc_device(&state->misc_out,
			&tegra_spdif_out_fops,
			"spdif_out");
	if (rc < 0)
		return rc;

	rc = setup_misc_device(&state->misc_out_ctl,
			&tegra_spdif_out_ctl_fops,
			"spdif_out_ctl");
	if (rc < 0)
		return rc;


	state->using_dma = state->pdata->dma_on;
	if (!state->using_dma)
		sound_ops = &pio_sound_ops;
	sound_ops->setup(state);

	rc = device_create_file(&pdev->dev, &dev_attr_dma_toggle);
	if (rc < 0) {
		dev_err(&pdev->dev, "%s: could not create sysfs entry %s: %d\n",
			__func__, dev_attr_dma_toggle.attr.name, rc);
		return rc;
	}

	rc = device_create_file(&pdev->dev, &dev_attr_tx_fifo_atn);
	if (rc < 0) {
		dev_err(&pdev->dev, "%s: could not create sysfs entry %s: %d\n",
			__func__, dev_attr_tx_fifo_atn.attr.name, rc);
		return rc;
	}

	return 0;
}

static struct platform_driver tegra_spdif_driver = {
	.driver = {
		.name = "spdif_out",
		.owner = THIS_MODULE,
	},
	.probe = tegra_spdif_probe,
};

static int __init tegra_spdif_init(void)
{
	return platform_driver_register(&tegra_spdif_driver);
}

module_init(tegra_spdif_init);
MODULE_LICENSE("GPL");
