/*
 * arch/arm/mach-tegra/tegra_i2s_audio.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *      Iliyan Malchev <malchev@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
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
#include <linux/io.h>

#include <mach/dma.h>
#include <mach/iomap.h>
#include <mach/i2s.h>
#include <mach/audio.h>
#include <mach/irqs.h>

#include "clock.h"

/* per stream (input/output) */
struct audio_out_stream {
	int opened;
	struct mutex lock;

	bool playback;
	spinlock_t pcm_out_lock;
	void *buffer;
	dma_addr_t buf_phys;
	struct kfifo fifo;
	struct completion fifo_completion;

	struct tegra_dma_channel *tx_dma;
	struct tegra_dma_req tx_dma_req;
};

struct i2s_pio_stats {
	u32 i2s_interrupt_count;
	u32 tx_fifo_errors;
	u32 rx_fifo_errors;
	u32 tx_fifo_written;
	u32 rx_fifo_read;
};

/* per i2s controller */
struct audio_driver_state {
	struct list_head next;

	struct platform_device *pdev;
	struct tegra_audio_platform_data *pdata;
	phys_addr_t i2s_phys;
	unsigned long i2s_base;

	bool using_dma;
	unsigned long dma_req_sel;

	int irq; /* for pio mode */
	struct i2s_pio_stats pio_stats;

	struct miscdevice misc_out;
	struct audio_out_stream out;

	struct miscdevice misc_in;
/*	struct audio_in_stream in; */
};

static inline struct audio_driver_state *ads_from_misc(struct file *file)
{
	struct miscdevice *m = file->private_data;
	return container_of(m, struct audio_driver_state, misc_out);
}

static inline struct audio_driver_state *ads_from_out(
			struct audio_out_stream *aos)
{
	return container_of(aos, struct audio_driver_state, out);
}

#define I2S_I2S_FIFO_TX_BUSY	I2S_I2S_STATUS_FIFO1_BSY
#define I2S_I2S_FIFO_TX_QS	I2S_I2S_STATUS_QS_FIFO1
#define I2S_I2S_FIFO_TX_ERR	I2S_I2S_STATUS_FIFO1_ERR

#define I2S_I2S_FIFO_RX_BUSY	I2S_I2S_STATUS_FIFO2_BSY
#define I2S_I2S_FIFO_RX_QS	I2S_I2S_STATUS_QS_FIFO2
#define I2S_I2S_FIFO_RX_ERR	I2S_I2S_STATUS_FIFO2_ERR

#define I2S_FIFO_ERR (I2S_I2S_STATUS_FIFO1_ERR | I2S_I2S_STATUS_FIFO2_ERR)

static inline void i2s_writel(unsigned long base, u32 val, u32 reg)
{
	writel(val, base + reg);
}

static inline u32 i2s_readl(unsigned long base, u32 reg)
{
	return readl(base + reg);
}

static inline void i2s_fifo_write(unsigned long base, int fifo, u32 data)
{
	i2s_writel(base, data, fifo ? I2S_I2S_FIFO2_0 : I2S_I2S_FIFO1_0);
}

static inline u32 i2s_fifo_read(unsigned long base, int fifo)
{
	return i2s_readl(base, fifo ? I2S_I2S_FIFO2_0 : I2S_I2S_FIFO1_0);
}

static int i2s_set_channel_bit_count(unsigned long base,
			int sampling, int bitclk)
{
	u32 val;
	int bitcnt = bitclk / (2 * sampling) - 1;

	if (bitcnt < 0 || bitcnt >= 1<<11) {
		pr_err("%s: bit count %d is out of bounds\n", __func__,
			bitcnt);
		return -EINVAL;
	}

	val = bitcnt;
	if (bitclk % (2 * sampling)) {
		pr_info("%s: enabling non-symmetric mode\n", __func__);
		val |= I2S_I2S_TIMING_NON_SYM_ENABLE;
	}

	pr_debug("%s: I2S_I2S_TIMING_0 = %08x\n", __func__, val);
	i2s_writel(base, val, I2S_I2S_TIMING_0);
	return 0;
}

static void i2s_set_fifo_mode(unsigned long base, int fifo, int tx)
{
	u32 val = i2s_readl(base, I2S_I2S_CTRL_0);
	if (fifo == 0) {
		val &= ~I2S_I2S_CTRL_FIFO1_RX_ENABLE;
		val |= (!tx) ? I2S_I2S_CTRL_FIFO1_RX_ENABLE : 0;
	} else {
		val &= ~I2S_I2S_CTRL_FIFO2_TX_ENABLE;
		val |= tx ? I2S_I2S_CTRL_FIFO2_TX_ENABLE : 0;
	}
	i2s_writel(base, val, I2S_I2S_CTRL_0);
}

static int i2s_fifo_set_attention_level(unsigned long base,
			int fifo, unsigned level)
{
	u32 val;

	if (level > I2S_FIFO_ATN_LVL_TWELVE_SLOTS) {
		pr_err("%s: invalid fifo level selector %d\n", __func__,
			level);
		return -EINVAL;
	}

	val = i2s_readl(base, I2S_I2S_FIFO_SCR_0);

	if (!fifo) {
		val &= ~I2S_I2S_FIFO_SCR_FIFO1_ATN_LVL_MASK;
		val |= level << I2S_FIFO1_ATN_LVL_SHIFT;
	} else {
		val &= ~I2S_I2S_FIFO_SCR_FIFO2_ATN_LVL_MASK;
		val |= level << I2S_FIFO2_ATN_LVL_SHIFT;
	}

	i2s_writel(base, val, I2S_I2S_FIFO_SCR_0);
	return 0;
}

static void i2s_fifo_enable(unsigned long base, int fifo, int on)
{
	u32 val = i2s_readl(base, I2S_I2S_CTRL_0);
	if (!fifo) {
		val &= ~I2S_I2S_CTRL_FIFO1_ENABLE;
		val |= on ? I2S_I2S_CTRL_FIFO1_ENABLE : 0;
	} else {
		val &= ~I2S_I2S_CTRL_FIFO2_ENABLE;
		val |= on ? I2S_I2S_CTRL_FIFO2_ENABLE : 0;
	}

	i2s_writel(base, val, I2S_I2S_CTRL_0);
}

static void i2s_fifo_clear(unsigned long base, int fifo)
{
	u32 val = i2s_readl(base, I2S_I2S_FIFO_SCR_0);
	if (!fifo) {
		val &= ~I2S_I2S_FIFO_SCR_FIFO1_CLR;
		val |= I2S_I2S_FIFO_SCR_FIFO1_CLR;
#if 0
		/* Per Nvidia, reduces pop on the next run. */
		if (!(val & I2S_I2S_CTRL_FIFO1_RX_ENABLE)) {
			int cnt = 16;
			while (cnt--)
				i2s_writel(base, 0, I2S_I2S_FIFO1_0);
		}
#endif
	} else {
		val &= ~I2S_I2S_FIFO_SCR_FIFO2_CLR;
		val |= I2S_I2S_FIFO_SCR_FIFO2_CLR;
	}

	i2s_writel(base, val, I2S_I2S_FIFO_SCR_0);
}

static void i2s_set_master(unsigned long base, int master)
{
	u32 val = i2s_readl(base, I2S_I2S_CTRL_0);
	val &= ~I2S_I2S_CTRL_MASTER_ENABLE;
	val |= master ? I2S_I2S_CTRL_MASTER_ENABLE : 0;
	i2s_writel(base, val, I2S_I2S_CTRL_0);
}

static int i2s_set_bit_format(unsigned long base, unsigned fmt)
{
	u32 val;

	if (fmt > I2S_BIT_FORMAT_DSP) {
		pr_err("%s: invalid bit-format selector %d\n", __func__, fmt);
		return -EINVAL;
	}

	val = i2s_readl(base, I2S_I2S_CTRL_0);
	val &= ~I2S_I2S_CTRL_BIT_FORMAT_MASK;
	val |= fmt << I2S_BIT_FORMAT_SHIFT;

	i2s_writel(base, val, I2S_I2S_CTRL_0);
	return 0;
}

static int i2s_set_bit_size(unsigned long base, unsigned bit_size)
{
	u32 val = i2s_readl(base, I2S_I2S_CTRL_0);
	val &= ~I2S_I2S_CTRL_BIT_SIZE_MASK;

	if (bit_size > I2S_BIT_SIZE_32) {
		pr_err("%s: invalid bit_size selector %d\n", __func__,
			bit_size);
		return -EINVAL;
	}

	val |= bit_size << I2S_BIT_SIZE_SHIFT;

	i2s_writel(base, val, I2S_I2S_CTRL_0);
	return 0;
}

static int i2s_set_fifo_format(unsigned long base, unsigned fmt)
{
	u32 val = i2s_readl(base, I2S_I2S_CTRL_0);
	val &= ~I2S_I2S_CTRL_FIFO_FORMAT_MASK;

	if (fmt > I2S_FIFO_32 && fmt != I2S_FIFO_PACKED) {
		pr_err("%s: invalid fmt selector %d\n", __func__, fmt);
		return -EINVAL;
	}

	val |= fmt << I2S_FIFO_SHIFT;

	i2s_writel(base, val, I2S_I2S_CTRL_0);
	return 0;
}

static void i2s_set_left_right_control_polarity(unsigned long base,
		int high_low)
{
	u32 val = i2s_readl(base, I2S_I2S_CTRL_0);
	val &= ~I2S_I2S_CTRL_L_R_CTRL;
	val |= high_low ? I2S_I2S_CTRL_L_R_CTRL : 0;
	i2s_writel(base, val, I2S_I2S_CTRL_0);
}

static void i2s_set_fifo_irq_on_err(unsigned long base, int fifo, int on)
{
	u32 val = i2s_readl(base, I2S_I2S_CTRL_0);
	if (!fifo) {
		val &= ~I2S_I2S_IE_FIFO1_ERR;
		val |= on ? I2S_I2S_IE_FIFO1_ERR : 0;
	} else {
		val &= ~I2S_I2S_IE_FIFO2_ERR;
		val |= on ? I2S_I2S_IE_FIFO2_ERR : 0;
	}
	i2s_writel(base, val, I2S_I2S_CTRL_0);
}

static void i2s_set_fifo_irq_on_qe(unsigned long base, int fifo, int on)
{
	u32 val = i2s_readl(base, I2S_I2S_CTRL_0);
	if (!fifo) {
		val &= ~I2S_I2S_QE_FIFO1;
		val |= on ? I2S_I2S_QE_FIFO1 : 0;
	} else {
		val &= ~I2S_I2S_QE_FIFO2;
		val |= on ? I2S_I2S_QE_FIFO2 : 0;
	}
	i2s_writel(base, val, I2S_I2S_CTRL_0);
}

static void i2s_enable_fifos(unsigned long base, int on)
{
	u32 val = i2s_readl(base, I2S_I2S_CTRL_0);
	if (on)
		val |= I2S_I2S_QE_FIFO1 | I2S_I2S_QE_FIFO2 |
		       I2S_I2S_IE_FIFO1_ERR | I2S_I2S_IE_FIFO2_ERR;
	else
		val &= ~(I2S_I2S_QE_FIFO1 | I2S_I2S_QE_FIFO2 |
			 I2S_I2S_IE_FIFO1_ERR | I2S_I2S_IE_FIFO2_ERR);

	i2s_writel(base, val, I2S_I2S_CTRL_0);
}

static inline u32 i2s_get_status(unsigned long base)
{
	return i2s_readl(base, I2S_I2S_STATUS_0);
}

static inline u32 i2s_get_control(unsigned long base)
{
	return i2s_readl(base, I2S_I2S_CTRL_0);
}

static inline void i2s_ack_status(unsigned long base)
{
	return i2s_writel(base, i2s_readl(base, I2S_I2S_STATUS_0),
				I2S_I2S_STATUS_0);
}

static inline u32 i2s_get_fifo_scr(unsigned long base)
{
	return i2s_readl(base, I2S_I2S_FIFO_SCR_0);
}

static inline phys_addr_t i2s_get_fifo_phy_base(unsigned long phy_base,
		int fifo)
{
	return phy_base + (fifo ? I2S_I2S_FIFO2_0 : I2S_I2S_FIFO1_0);
}

static inline u32 i2s_get_fifo_full_empty_count(unsigned long base, int fifo)
{
	u32 val = i2s_readl(base, I2S_I2S_FIFO_SCR_0);

	if (!fifo)
		val = val >> I2S_I2S_FIFO_SCR_FIFO1_FULL_EMPTY_COUNT_SHIFT;
	else
		val = val >> I2S_I2S_FIFO_SCR_FIFO2_FULL_EMPTY_COUNT_SHIFT;

	return val & I2S_I2S_FIFO_SCR_FIFO_FULL_EMPTY_COUNT_MASK;
}

/* FIXME: add an ioctl to report the buffer sizes */

#define PCM_OUT_BUFFER_SIZE	(PAGE_SIZE*4)
#define PCM_OUT_DMA_CHUNK	(PAGE_SIZE)
#define PCM_OUT_THRESHOLD	(PAGE_SIZE*2)

#define PLAYBACK_STARTED true
#define PLAYBACK_STOPPED false

static int setup_dma(struct audio_driver_state *);
static void tear_down_dma(struct audio_driver_state *);
static void start_dma_playback(struct audio_out_stream *);
static void stop_dma_playback(struct audio_out_stream *);

static int setup_pio(struct audio_driver_state *);
static void tear_down_pio(struct audio_driver_state *);
static void start_pio_playback(struct audio_out_stream *);
static void stop_pio_playback(struct audio_out_stream *);

struct sound_ops {
	int (*setup)(struct audio_driver_state *);
	void (*tear_down)(struct audio_driver_state *);
	void (*start_playback)(struct audio_out_stream *);
	void (*stop_playback)(struct audio_out_stream *);
};

static const struct sound_ops dma_sound_ops = {
	.setup = setup_dma,
	.tear_down = tear_down_dma,
	.start_playback = start_dma_playback,
	.stop_playback = stop_dma_playback
};

static const struct sound_ops pio_sound_ops = {
	.setup = setup_pio,
	.tear_down = tear_down_pio,
	.start_playback = start_pio_playback,
	.stop_playback = stop_pio_playback
};

static const struct sound_ops *sound_ops = &dma_sound_ops;

static void start_playback(struct audio_out_stream *aos)
{
	sound_ops->start_playback(aos);
	aos->playback = PLAYBACK_STARTED;
}

static void start_playback_if_necessary(struct audio_out_stream *aos)
{
	unsigned long flags;
	spin_lock_irqsave(&aos->pcm_out_lock, flags);
	if (aos->playback == PLAYBACK_STOPPED) {
		pr_debug("%s: starting playback\n", __func__);
		start_playback(aos);
	} else
		pr_debug("%s: playback already started\n", __func__);
	spin_unlock_irqrestore(&aos->pcm_out_lock, flags);
}

static void stop_playback(struct audio_out_stream *aos)
{
	aos->playback = PLAYBACK_STOPPED;
	sound_ops->stop_playback(aos);
}

static bool stop_playback_if_necessary(struct audio_out_stream *aos)
{
	unsigned long flags;
	spin_lock_irqsave(&aos->pcm_out_lock, flags);
	if (kfifo_is_empty(&aos->fifo)) {
		stop_playback(aos);
		spin_unlock_irqrestore(&aos->pcm_out_lock, flags);
		return true;
	}
	spin_unlock_irqrestore(&aos->pcm_out_lock, flags);

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

static void resume_dma_playback(struct audio_out_stream *aos);

static void setup_dma_tx_request(struct tegra_dma_req *req,
		struct audio_out_stream *aos);

static int setup_dma(struct audio_driver_state *ads)
{
	pr_info("%s\n", __func__);

	ads->out.buf_phys = dma_map_single(NULL, ads->out.buffer,
				PCM_OUT_BUFFER_SIZE, DMA_TO_DEVICE);
	BUG_ON(!ads->out.buf_phys);

	/* setup audio playback */
	setup_dma_tx_request(&ads->out.tx_dma_req, &ads->out);
	ads->out.tx_dma = tegra_dma_allocate_channel(TEGRA_DMA_MODE_ONESHOT);
	if (IS_ERR(ads->out.tx_dma)) {
		pr_err("%s: could not allocate DMA channel for I2S: %ld\n",
			__func__, PTR_ERR(ads->out.tx_dma));
		return PTR_ERR(ads->out.tx_dma);
	}

	/* TODO: setup recording */

	return 0;
}

static void tear_down_dma(struct audio_driver_state *ads)
{
	pr_info("%s\n", __func__);
	tegra_dma_free_channel(ads->out.tx_dma);
	ads->out.tx_dma = NULL;
	dma_unmap_single(&ads->pdev->dev, ads->out.buf_phys,
				PCM_OUT_BUFFER_SIZE,
				DMA_TO_DEVICE);
	ads->out.buf_phys = 0;
}

static void dma_tx_complete_callback(struct tegra_dma_req *req)
{
	struct audio_out_stream *aos = req->dev;
	int count = req->bytes_transferred;

	pr_debug("%s bytes transferred %d\n", __func__, count);

	kfifo_skip(&aos->fifo, count);

	if (kfifo_avail(&aos->fifo) > PCM_OUT_THRESHOLD &&
			!completion_done(&aos->fifo_completion)) {
		pr_debug("%s: complete (%d avail)\n", __func__,
				kfifo_avail(&aos->fifo));
		complete(&aos->fifo_completion);
	}

	if (stop_playback_if_necessary(aos))
		return;
	resume_dma_playback(aos);
}

static void setup_dma_tx_request(struct tegra_dma_req *req,
		struct audio_out_stream *aos)
{
	struct audio_driver_state *ads = ads_from_out(aos);

	memset(req, 0, sizeof(*req));

	/* FIXME:  need to set instance? */
	req->complete = dma_tx_complete_callback;
	req->dev = aos;
	req->to_memory = false;
	req->dest_addr = i2s_get_fifo_phy_base(ads->i2s_phys, I2S_FIFO_TX);
	req->dest_wrap = 4;
	req->dest_bus_width = 16;
	req->source_bus_width = 32;
	req->source_wrap = 0;
	req->req_sel = ads->dma_req_sel;
}

static void resume_dma_playback(struct audio_out_stream *aos)
{
	struct audio_driver_state *ads = ads_from_out(aos);
	struct tegra_dma_req *req = &aos->tx_dma_req;

	unsigned out = __kfifo_off(&aos->fifo, aos->fifo.out);
	unsigned in = __kfifo_off(&aos->fifo, aos->fifo.in);

	BUG_ON(!kfifo_len(&aos->fifo));

	req->source_addr = aos->buf_phys + out;
	if (out < in)
		req->size = in - out;
	else
		req->size = kfifo_size(&aos->fifo) - out;

	dma_sync_single_for_device(NULL,
			aos->buf_phys + out, req->size, DMA_TO_DEVICE);

	/* Don't send all the data yet. */
	if (req->size > PCM_OUT_DMA_CHUNK)
		req->size = PCM_OUT_DMA_CHUNK;
	pr_debug("%s resume playback (%d in fifo, writing %d, in %d out %d)\n",
			__func__, kfifo_len(&aos->fifo), req->size, in, out);

	i2s_fifo_set_attention_level(ads->i2s_base,
			I2S_FIFO_TX, I2S_FIFO_ATN_LVL_FOUR_SLOTS);
	i2s_fifo_enable(ads->i2s_base, I2S_FIFO_TX, 1);
	tegra_dma_enqueue_req(aos->tx_dma, req);
}

static void start_dma_playback(struct audio_out_stream *aos)
{
	resume_dma_playback(aos);
}

static void stop_dma_playback(struct audio_out_stream *aos)
{
	struct audio_driver_state *ads = ads_from_out(aos);
	pr_debug("%s\n", __func__);
	i2s_fifo_enable(ads->i2s_base, I2S_FIFO_TX, 0);
	while (i2s_get_status(ads->i2s_base) & I2S_I2S_FIFO_TX_BUSY)
		/* spin */;
}

/* PIO (non-DMA) */

static irqreturn_t i2s_interrupt(int irq, void *data);

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

static void start_pio_playback(struct audio_out_stream *aos)
{
	struct audio_driver_state *ads = ads_from_out(aos);

	pr_debug("%s\n", __func__);

	i2s_fifo_set_attention_level(ads->i2s_base,
			I2S_FIFO_TX, I2S_FIFO_ATN_LVL_ONE_SLOT);
	i2s_fifo_clear(ads->i2s_base, I2S_FIFO_TX);

	i2s_set_fifo_irq_on_err(ads->i2s_base, I2S_FIFO_TX, 1);
	i2s_set_fifo_irq_on_qe(ads->i2s_base, I2S_FIFO_TX, 1);
}

static void stop_pio_playback(struct audio_out_stream *aos)
{
	struct audio_driver_state *ads = ads_from_out(aos);

	i2s_set_fifo_irq_on_err(ads->i2s_base, I2S_FIFO_TX, 0);
	i2s_set_fifo_irq_on_qe(ads->i2s_base, I2S_FIFO_TX, 0);
	i2s_fifo_enable(ads->i2s_base, I2S_FIFO_TX, 0);
	while (i2s_get_status(ads->i2s_base) & I2S_I2S_FIFO_TX_BUSY)
		/* spin */;

	pr_info("%s: interrupts %d\n", __func__,
			ads->pio_stats.i2s_interrupt_count);
	pr_info("%s: sent       %d\n", __func__,
			ads->pio_stats.tx_fifo_written);
	pr_info("%s: received   %d\n", __func__,
			ads->pio_stats.rx_fifo_read);
	pr_info("%s: tx errors  %d\n", __func__,
			ads->pio_stats.tx_fifo_errors);
	pr_info("%s: rx errors  %d\n", __func__,
			ads->pio_stats.rx_fifo_errors);

	memset(&ads->pio_stats, 0, sizeof(ads->pio_stats));
}

static irqreturn_t i2s_interrupt(int irq, void *data)
{
	struct audio_driver_state *ads = data;
	u32 status = i2s_get_status(ads->i2s_base);

	pr_debug("%s: %08x\n", __func__, status);

	ads->pio_stats.i2s_interrupt_count++;

	if (status & I2S_I2S_FIFO_TX_ERR)
		ads->pio_stats.tx_fifo_errors++;

	if (status & I2S_I2S_FIFO_RX_ERR)
		ads->pio_stats.rx_fifo_errors++;

	if (status & I2S_FIFO_ERR)
		i2s_ack_status(ads->i2s_base);

	if (status & I2S_I2S_FIFO_TX_QS) {
		int written;
		int empty;
		int len;
		u16 fifo_buffer[32];

		struct audio_out_stream *out = &ads->out;

		pr_debug("%s fifo is ready\n", __func__);

		if (kfifo_avail(&out->fifo) > PCM_OUT_THRESHOLD &&
				!completion_done(&out->fifo_completion)) {
			pr_debug("%s: complete (%d avail)\n", __func__,
					kfifo_avail(&out->fifo));
			complete(&out->fifo_completion);
		}

		if (stop_playback_if_necessary(out))
			goto done;

		empty = i2s_get_fifo_full_empty_count(ads->i2s_base,
				I2S_FIFO_TX);

		len = kfifo_out(&out->fifo, fifo_buffer,
				empty * sizeof(u16));
		len /= sizeof(u16);

		written = 0;
		while (empty-- && written < len) {
			ads->pio_stats.tx_fifo_written += written * sizeof(u16);
			i2s_fifo_write(ads->i2s_base,
					I2S_FIFO_TX, fifo_buffer[written++]);
		}

		/* TODO: Should we check to see if we wrote less than the
		 * FIFO threshold and adjust it if so?
		 */

		if (written) {
			/* start the transaction */
			pr_debug("%s: enabling fifo (%d samples written)\n",
					__func__, written);
			i2s_fifo_enable(ads->i2s_base, I2S_FIFO_TX, 1);
		}
	}

	if (status & I2S_I2S_FIFO_RX_QS) {
		int read = 0;
		while (i2s_get_fifo_full_empty_count(ads->i2s_base,
				I2S_FIFO_RX)) {
			i2s_fifo_read(ads->i2s_base, I2S_FIFO_RX);
			read++;
		}
		ads->pio_stats.rx_fifo_read += read;
		pr_debug("%s: rx fifo empty (%d read)\n", __func__, read);
	}

done:
	pr_debug("%s: done %08x\n", __func__, i2s_get_status(ads->i2s_base));
	return IRQ_HANDLED;
}

static ssize_t tegra_audio_write(struct file *file,
		const char __user *buf, size_t size, loff_t *off)
{
	ssize_t rc, total = 0;
	unsigned nw = 0;

	struct audio_driver_state *ads = ads_from_misc(file);

	mutex_lock(&ads->out.lock);

	if (!IS_ALIGNED(size, 4)) {
		pr_err("%s: user size request %d not aligned to 4\n",
			__func__, size);
		rc = -EINVAL;
	}

	pr_debug("%s: write %d bytes, %d available\n", __func__,
			size, kfifo_avail(&ads->out.fifo));

again:
	rc = kfifo_from_user(&ads->out.fifo, buf + total, size - total, &nw);
	if (rc < 0) {
		pr_err("%s: error copying from user\n", __func__);
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

	start_playback_if_necessary(&ads->out);
done:
	mutex_unlock(&ads->out.lock);
	return rc;
}

static int tegra_audio_out_open(struct inode *inode, struct file *file)
{
	struct audio_driver_state *ads = ads_from_misc(file);

	mutex_lock(&ads->out.lock);
	if (!ads->out.opened++)
		kfifo_reset(&ads->out.fifo);
	mutex_unlock(&ads->out.lock);

	return 0;
}

static int tegra_audio_out_release(struct inode *inode, struct file *file)
{
	struct audio_driver_state *ads = ads_from_misc(file);

	mutex_lock(&ads->out.lock);
	if (ads->out.opened)
		ads->out.opened--;
	mutex_unlock(&ads->out.lock);

	return 0;
}

static int tegra_audio_in_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int tegra_audio_in_release(struct inode *inode, struct file *file)
{
	return 0;
}

#ifdef CONFIG_DEBUG_FS
static int debugfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static DEFINE_MUTEX(debugfs_lock);

ssize_t debugfs_read(struct file *file, char __user *buf,
		size_t size, loff_t *off)
{
	int rc = 0;
	struct audio_driver_state *ads = file->private_data;
	static bool r;

	mutex_lock(&debugfs_lock);

	if (r) {
		r = false;
		goto done;
	}

	if (size < 5) {
		rc = -ETOOSMALL;
		goto done;
	}

	if (copy_to_user(buf, ads->using_dma ? "dma\n" : "pio\n", 5)) {
		rc = -EFAULT;
		goto done;
	}

	r = true;
	rc = *off = 5;
done:
	mutex_unlock(&debugfs_lock);
	return rc;
}

ssize_t debugfs_write(struct file *file,
		const char __user *buf, size_t size, loff_t *off)
{
	char cmd[5];
	int use_dma;

	struct audio_driver_state *ads = file->private_data;

	if (size < 4) {
		pr_err("%s: buffer size %d too small\n", __func__, size);
		return -ETOOSMALL;
	}

	if (copy_from_user(cmd, buf, 4)) {
		pr_err("%s: could not copy from user\n", __func__);
		return -EFAULT;
	}
	cmd[3] = 0;

	use_dma = 0;
	if (!strcmp(cmd, "dma"))
		use_dma = 1;
	else if (strcmp(cmd, "pio")) {
		pr_err("%s: invalid string [%s]\n", __func__, cmd);
		return -EINVAL;
	}

	/* FIXME: lock the input stream here as well. */
	mutex_lock(&ads->out.lock);
	if (ads->out.playback == PLAYBACK_STARTED) {
		pr_err("%s: playback in progress, try again.\n", __func__);
		mutex_unlock(&ads->out.lock);
		return -EBUSY;
	}
	if (!!use_dma ^ !!ads->using_dma)
		toggle_dma(ads);
	else
		pr_info("%s: no change\n", __func__);
	mutex_unlock(&ads->out.lock);

	return 5;
}

static const struct file_operations debugfs_ops = {
	.read = debugfs_read,
	.write = debugfs_write,
	.open = debugfs_open,
};

static void setup_tegra_audio_debugfs(struct audio_driver_state *ads)
{
	struct dentry *dent;

	dent = debugfs_create_dir("tegra_audio", 0);
	if (IS_ERR(dent)) {
		pr_err("%s: could not create dentry\n", __func__);
		return;
	}

	debugfs_create_file("dma", 0666, dent, ads, &debugfs_ops);
}
#else
static inline void setup_tegra_audio_debugfs(struct audio_driver_state *ads) {}
#endif

static const struct file_operations tegra_audio_out_fops = {
	.owner = THIS_MODULE,
	.open = tegra_audio_out_open,
	.release = tegra_audio_out_release,
	.write = tegra_audio_write,
};

static const struct file_operations tegra_audio_in_fops = {
	.owner = THIS_MODULE,
	.open = tegra_audio_in_open,
	.release = tegra_audio_in_release,
};

static int tegra_audio_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct clk *i2s_clk, *audio_sync_clk, *dap_mclk;
	struct audio_driver_state *state;

	pr_info("%s\n", __func__);

	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->pdev = pdev;
	state->pdata = pdev->dev.platform_data;
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

	state->i2s_phys = res->start;
	state->i2s_base = (unsigned long)ioremap(res->start,
			res->end - res->start + 1);
	if (!state->i2s_base) {
		dev_err(&pdev->dev, "cannot remap iomem!\n");
		return -EIO;
	}

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

	i2s_clk = clk_get(&pdev->dev, NULL);
	if (!i2s_clk) {
		pr_err("%s: could not get i2s1 clock\n", __func__);
		return -EIO;
	}

	clk_set_rate(i2s_clk, state->pdata->i2s_clk_rate);
	if (clk_enable(i2s_clk)) {
		pr_err("%s: failed to enable i2s1 clock\n", __func__);
		return -EIO;
	}
	pr_info("%s: i2s_clk rate %ld\n", __func__, clk_get_rate(i2s_clk));

	dap_mclk = tegra_get_clock_by_name(state->pdata->dap_clk);
	if (!dap_mclk) {
		pr_err("%s: could not get DAP clock\n", __func__);
		return -EIO;
	}
	clk_enable(dap_mclk);

	audio_sync_clk = tegra_get_clock_by_name(state->pdata->audio_sync_clk);
	if (!audio_sync_clk) {
		pr_err("%s: could not get audio_2x clock\n", __func__);
		return -EIO;
	}
	clk_enable(audio_sync_clk);

	/* disable interrupts from I2S */
	i2s_enable_fifos(state->i2s_base, 0);

	i2s_set_left_right_control_polarity(state->i2s_base, 0); /* default */

	if (state->pdata->master)
		i2s_set_channel_bit_count(state->i2s_base, 44100,
				clk_get_rate(i2s_clk));
	i2s_set_master(state->i2s_base, state->pdata->master);

	i2s_set_fifo_mode(state->i2s_base, FIFO1, 1); /* FIFO1 is TX */
	i2s_set_fifo_mode(state->i2s_base, FIFO2, 0); /* FIFO2 is RX */

	i2s_set_bit_format(state->i2s_base, state->pdata->mode);
	i2s_set_bit_size(state->i2s_base, state->pdata->bit_size);
	i2s_set_fifo_format(state->i2s_base, state->pdata->fifo_fmt);

	state->out.opened = 0;
	state->out.playback = PLAYBACK_STOPPED;
	mutex_init(&state->out.lock);
	init_completion(&state->out.fifo_completion);
	spin_lock_init(&state->out.pcm_out_lock);
	state->out.buf_phys = 0;
	state->out.tx_dma = NULL;

	state->out.buffer = kmalloc(PCM_OUT_BUFFER_SIZE,
			GFP_KERNEL | GFP_DMA);
	if (!state->out.buffer) {
		pr_err("%s: could not allocate buffer\n", __func__);
		return -ENOMEM;
	}

	if (request_irq(state->irq, i2s_interrupt,
			IRQF_DISABLED, state->pdev->name, state) < 0) {
		pr_err("%s: could not register handler for irq %d\n",
			__func__, state->irq);
		return -EIO;
	}

	kfifo_init(&state->out.fifo, state->out.buffer,
			PCM_OUT_BUFFER_SIZE);

	state->using_dma = state->pdata->dma_on;
	sound_ops->setup(state);

	memset(&state->misc_out, 0, sizeof(state->misc_out));
	state->misc_out.minor = MISC_DYNAMIC_MINOR;
	state->misc_out.name  = kmalloc(sizeof("audio_out") + 1, GFP_KERNEL);
	if (!state->misc_out.name)
		return -ENOMEM;
	snprintf((char *)state->misc_out.name, sizeof("audio_out") + 1,
			"audio%d_out", state->pdev->id);
	state->misc_out.fops = &tegra_audio_out_fops;
	if (misc_register(&state->misc_out)) {
		pr_err("%s: could not register audio_out\n", __func__);
		return -EIO;
	}

	memset(&state->misc_in, 0, sizeof(state->misc_in));
	state->misc_in.minor = MISC_DYNAMIC_MINOR;
	state->misc_in.name  = kmalloc(sizeof("audio_in") + 1, GFP_KERNEL);
	if (!state->misc_in.name)
		return -ENOMEM;
	snprintf((char *)state->misc_in.name, sizeof("audio_in") + 1,
			"audio%d_in", state->pdev->id);
	state->misc_in.fops = &tegra_audio_in_fops;
	if (misc_register(&state->misc_in)) {
		pr_err("%s: could not register audio_in\n", __func__);
		return -EIO;
	}

	setup_tegra_audio_debugfs(state);

	return 0;
}

static struct platform_driver tegra_audio_driver = {
	.driver = {
		.name = "i2s",
		.owner = THIS_MODULE,
	},
	.probe = tegra_audio_probe,
};

static int __init tegra_audio_init(void)
{
	return platform_driver_register(&tegra_audio_driver);
}

module_init(tegra_audio_init);
MODULE_LICENSE("GPL");
