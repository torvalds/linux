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

/* TODO:
	-- replace make I2S_MAX_NUM_BUFS configurable through an ioctl
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
#include <linux/wakelock.h>
#include <linux/delay.h>
#include <linux/tegra_audio.h>
#include <linux/pm.h>
#include <linux/workqueue.h>

#include <mach/dma.h>
#include <mach/iomap.h>
#include <mach/i2s.h>
#include <mach/audio.h>
#include <mach/irqs.h>

#include "clock.h"

#define PCM_BUFFER_MAX_SIZE_ORDER	PAGE_SHIFT

#define TEGRA_AUDIO_DSP_NONE		0
#define TEGRA_AUDIO_DSP_PCM		1
#define TEGRA_AUDIO_DSP_NETWORK		2
#define TEGRA_AUDIO_DSP_TDM		3

#define I2S_MAX_NUM_BUFS 4
#define I2S_DEFAULT_TX_NUM_BUFS 2
#define I2S_DEFAULT_RX_NUM_BUFS 2

/* per stream (input/output) */
struct audio_stream {
	int opened;
	struct mutex lock;

	bool active; /* is DMA in progress? */
	int num_bufs;
	void *buffer[I2S_MAX_NUM_BUFS];
	dma_addr_t buf_phy[I2S_MAX_NUM_BUFS];
	struct completion comp[I2S_MAX_NUM_BUFS];
	struct tegra_dma_req dma_req[I2S_MAX_NUM_BUFS];
	int last_queued;

	int i2s_fifo_atn_level;

	struct tegra_dma_channel *dma_chan;
	bool stop;
	struct completion stop_completion;
	spinlock_t dma_req_lock;

	struct work_struct allow_suspend_work;
	struct wake_lock wake_lock;
	char wake_lock_name[100];
};

/* per i2s controller */
struct audio_driver_state {
	struct list_head next;

	struct platform_device *pdev;
	struct tegra_audio_platform_data *pdata;
	phys_addr_t i2s_phys;
	unsigned long i2s_base;

	unsigned long dma_req_sel;

	int irq;
	struct tegra_audio_in_config in_config;

	struct miscdevice misc_out;
	struct miscdevice misc_out_ctl;
	struct audio_stream out;

	struct miscdevice misc_in;
	struct miscdevice misc_in_ctl;
	struct audio_stream in;

	/* Control for whole I2S (Data format, etc.) */
	struct miscdevice misc_ctl;
	unsigned int bit_format;
};

static inline bool pending_buffer_requests(struct audio_stream *stream)
{
	int i;
	for (i = 0; i < stream->num_bufs; i++)
		if (!completion_done(&stream->comp[i]))
			return true;
	return false;
}

static inline int buf_size(struct audio_stream *s __attribute__((unused)))
{
	return 1 << PCM_BUFFER_MAX_SIZE_ORDER;
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

static inline struct audio_driver_state *ads_from_misc_in(struct file *file)
{
	struct miscdevice *m = file->private_data;
	struct audio_driver_state *ads =
			container_of(m, struct audio_driver_state, misc_in);
	BUG_ON(!ads);
	return ads;
}

static inline struct audio_driver_state *ads_from_misc_in_ctl(
		struct file *file)
{
	struct miscdevice *m = file->private_data;
	struct audio_driver_state *ads =
			container_of(m, struct audio_driver_state,
					misc_in_ctl);
	BUG_ON(!ads);
	return ads;
}

static inline struct audio_driver_state *ads_from_misc_ctl(
		struct file *file)
{
	struct miscdevice *m = file->private_data;
	struct audio_driver_state *ads =
			container_of(m, struct audio_driver_state,
					misc_ctl);
	BUG_ON(!ads);
	return ads;
}

static inline struct audio_driver_state *ads_from_out(
			struct audio_stream *aos)
{
	return container_of(aos, struct audio_driver_state, out);
}

static inline struct audio_driver_state *ads_from_in(
			struct audio_stream *ais)
{
	return container_of(ais, struct audio_driver_state, in);
}

static inline void prevent_suspend(struct audio_stream *as)
{
	pr_debug("%s\n", __func__);
	cancel_work_sync(&as->allow_suspend_work);
	wake_lock(&as->wake_lock);
}

static void allow_suspend_worker(struct work_struct *w)
{
	struct audio_stream *as = container_of(w,
			struct audio_stream, allow_suspend_work);
	pr_debug("%s\n", __func__);
	wake_unlock(&as->wake_lock);
}

static inline void allow_suspend(struct audio_stream *as)
{
	schedule_work(&as->allow_suspend_work);
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

#if 0
static bool i2s_is_fifo_enabled(unsigned long base, int fifo)
{
	u32 val = i2s_readl(base, I2S_I2S_CTRL_0);
	if (!fifo)
		return !!(val & I2S_I2S_CTRL_FIFO1_ENABLE);
	return !!(val & I2S_I2S_CTRL_FIFO2_ENABLE);
}
#endif

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

static int i2s_set_dsp_mode(unsigned long base, unsigned int mode)
{
	u32 val;
	if (mode > TEGRA_AUDIO_DSP_TDM) {
		pr_err("%s: invalid mode %d.\n", __func__, mode);
		return -EINVAL;
	}
	if (mode == TEGRA_AUDIO_DSP_TDM) {
		pr_err("TEGRA_AUDIO_DSP_TDM not implemented.\n");
		return -EINVAL;
	}

	/* Disable unused modes */
	if (mode != TEGRA_AUDIO_DSP_PCM) {
		/* Disable PCM mode */
		val = i2s_readl(base, I2S_I2S_PCM_CTRL_0);
		val &= ~(I2S_I2S_PCM_CTRL_TRM_MODE |
				I2S_I2S_PCM_CTRL_RCV_MODE);
		i2s_writel(base, val, I2S_I2S_PCM_CTRL_0);
	}
	if (mode != TEGRA_AUDIO_DSP_NETWORK) {
		/* Disable Network mode */
		val = i2s_readl(base, I2S_I2S_NW_CTRL_0);
		val &= ~(I2S_I2S_NW_CTRL_TRM_TLPHY_MODE |
				I2S_I2S_NW_CTRL_RCV_TLPHY_MODE);
		i2s_writel(base, val, I2S_I2S_NW_CTRL_0);
	}

	/* Enable the selected mode. */
	switch (mode) {
	case TEGRA_AUDIO_DSP_NETWORK:
		/* Set DSP Network (Telephony) Mode */
		val = i2s_readl(base, I2S_I2S_NW_CTRL_0);
		val |= I2S_I2S_NW_CTRL_TRM_TLPHY_MODE |
				I2S_I2S_NW_CTRL_RCV_TLPHY_MODE;
		i2s_writel(base, val, I2S_I2S_NW_CTRL_0);
		break;
	case TEGRA_AUDIO_DSP_PCM:
		/* Set DSP PCM Mode */
		val = i2s_readl(base, I2S_I2S_PCM_CTRL_0);
		val |= I2S_I2S_PCM_CTRL_TRM_MODE |
				I2S_I2S_PCM_CTRL_RCV_MODE;
		i2s_writel(base, val, I2S_I2S_PCM_CTRL_0);
		break;
	}

	return 0;
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
	/* For DSP format, select DSP PCM mode. */
	/* PCM mode and Network Mode slot 0 are effectively identical. */
	if (fmt == I2S_BIT_FORMAT_DSP)
		i2s_set_dsp_mode(base, TEGRA_AUDIO_DSP_PCM);
	else
		i2s_set_dsp_mode(base, TEGRA_AUDIO_DSP_NONE);

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

#if 0
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
#endif

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

static int i2s_configure(struct platform_device *pdev)
{
	struct tegra_audio_platform_data *pdata = pdev->dev.platform_data;
	struct audio_driver_state *state = pdata->driver_data;
	bool master;
	struct clk *i2s_clk;
	int master_clk;

	/* dev_info(&pdev->dev, "%s\n", __func__); */

	if (!state)
		return -ENOMEM;

	/* disable interrupts from I2S */
	i2s_enable_fifos(state->i2s_base, 0);
	i2s_fifo_clear(state->i2s_base, I2S_FIFO_TX);
	i2s_fifo_clear(state->i2s_base, I2S_FIFO_RX);
	i2s_set_left_right_control_polarity(state->i2s_base, 0); /* default */

	i2s_clk = clk_get(&pdev->dev, NULL);
	if (!i2s_clk) {
		dev_err(&pdev->dev, "%s: could not get i2s clock\n",
			__func__);
		return -EIO;
	}

	master = state->bit_format == TEGRA_AUDIO_BIT_FORMAT_DSP ?
			state->pdata->dsp_master : state->pdata->i2s_master;


	master_clk = state->bit_format == TEGRA_AUDIO_BIT_FORMAT_DSP ?
			state->pdata->dsp_master_clk :
			state->pdata->i2s_master_clk;
#define I2S_CLK_TO_BITCLK_RATIO 2  /* Todo, Bitclk based on 2X clock? */
	if (master)
		i2s_set_channel_bit_count(state->i2s_base, master_clk,
			clk_get_rate(i2s_clk)*I2S_CLK_TO_BITCLK_RATIO);
	i2s_set_master(state->i2s_base, master);

	i2s_set_fifo_mode(state->i2s_base, I2S_FIFO_TX, 1);
	i2s_set_fifo_mode(state->i2s_base, I2S_FIFO_RX, 0);

	if (state->bit_format == TEGRA_AUDIO_BIT_FORMAT_DSP)
		i2s_set_bit_format(state->i2s_base, I2S_BIT_FORMAT_DSP);
	else
		i2s_set_bit_format(state->i2s_base, state->pdata->mode);
	i2s_set_bit_size(state->i2s_base, state->pdata->bit_size);
	i2s_set_fifo_format(state->i2s_base, state->pdata->fifo_fmt);

	return 0;
}

static int init_stream_buffer(struct audio_stream *, int);

static int setup_dma(struct audio_driver_state *, int);
static void tear_down_dma(struct audio_driver_state *, int);
static void stop_dma_playback(struct audio_stream *);
static int start_dma_recording(struct audio_stream *, int);
static void stop_dma_recording(struct audio_stream *);

struct sound_ops {
	int (*setup)(struct audio_driver_state *, int);
	void (*tear_down)(struct audio_driver_state *, int);
	void (*stop_playback)(struct audio_stream *);
	int (*start_recording)(struct audio_stream *, int);
	void (*stop_recording)(struct audio_stream *);
};

static const struct sound_ops dma_sound_ops = {
	.setup = setup_dma,
	.tear_down = tear_down_dma,
	.stop_playback = stop_dma_playback,
	.start_recording = start_dma_recording,
	.stop_recording = stop_dma_recording,
};

static const struct sound_ops *sound_ops = &dma_sound_ops;

static int start_recording_if_necessary(struct audio_stream *ais, int size)
{
	int rc = 0;
	unsigned long flags;
	prevent_suspend(ais);
	spin_lock_irqsave(&ais->dma_req_lock, flags);
	if (!ais->stop && !pending_buffer_requests(ais)) {
		/* pr_debug("%s: starting recording\n", __func__); */
		rc = sound_ops->start_recording(ais, size);
		if (rc) {
			pr_err("%s start_recording() failed\n", __func__);
			allow_suspend(ais);
		}
	}
	spin_unlock_irqrestore(&ais->dma_req_lock, flags);
	return rc;
}

static bool stop_playback_if_necessary(struct audio_stream *aos)
{
	unsigned long flags;
	spin_lock_irqsave(&aos->dma_req_lock, flags);
	pr_debug("%s\n", __func__);
	if (!pending_buffer_requests(aos)) {
		pr_debug("%s: no more data to play back\n", __func__);
		sound_ops->stop_playback(aos);
		spin_unlock_irqrestore(&aos->dma_req_lock, flags);
		allow_suspend(aos);
		return true;
	}
	spin_unlock_irqrestore(&aos->dma_req_lock, flags);

	return false;
}

/* playback and recording */
static bool wait_till_stopped(struct audio_stream *as)
{
	int rc;
	pr_debug("%s: wait for completion\n", __func__);
	rc = wait_for_completion_timeout(
			&as->stop_completion, HZ);
	if (!rc)
		pr_err("%s: wait timed out", __func__);
	if (rc < 0)
		pr_err("%s: wait error %d\n", __func__, rc);
	allow_suspend(as);
	pr_debug("%s: done: %d\n", __func__, rc);
	return true;
}

/* Ask for playback and recording to stop.  The _nosync means that
 * as->lock has to be locked by the caller.
 */
static void request_stop_nosync(struct audio_stream *as)
{
	int i;
	pr_debug("%s\n", __func__);
	if (!as->stop) {
		as->stop = true;
		if (pending_buffer_requests(as))
			wait_till_stopped(as);
		for (i = 0; i < as->num_bufs; i++) {
			init_completion(&as->comp[i]);
			complete(&as->comp[i]);
		}
	}
	if (!tegra_dma_is_empty(as->dma_chan))
		pr_err("%s: DMA not empty!\n", __func__);
	/* Stop the DMA then dequeue anything that's in progress. */
	tegra_dma_cancel(as->dma_chan);
	as->active = false; /* applies to recording only */
	pr_debug("%s: done\n", __func__);
}

static void setup_dma_tx_request(struct tegra_dma_req *req,
		struct audio_stream *aos);

static void setup_dma_rx_request(struct tegra_dma_req *req,
		struct audio_stream *ais);

static int setup_dma(struct audio_driver_state *ads, int mask)
{
	int rc, i;
	pr_info("%s\n", __func__);

	if (mask & TEGRA_AUDIO_ENABLE_TX) {
		/* setup audio playback */
		for (i = 0; i < ads->out.num_bufs; i++) {
			ads->out.buf_phy[i] = dma_map_single(&ads->pdev->dev,
					ads->out.buffer[i],
					1 << PCM_BUFFER_MAX_SIZE_ORDER,
					DMA_TO_DEVICE);
			BUG_ON(!ads->out.buf_phy[i]);
			setup_dma_tx_request(&ads->out.dma_req[i], &ads->out);
			ads->out.dma_req[i].source_addr = ads->out.buf_phy[i];
		}
		ads->out.dma_chan = tegra_dma_allocate_channel(
				TEGRA_DMA_MODE_CONTINUOUS_SINGLE);
		if (!ads->out.dma_chan) {
			pr_err("%s: error alloc output DMA channel: %ld\n",
				__func__, PTR_ERR(ads->out.dma_chan));
			rc = -ENODEV;
			goto fail_tx;
		}
	}

	if (mask & TEGRA_AUDIO_ENABLE_RX) {
		/* setup audio recording */
		for (i = 0; i < ads->in.num_bufs; i++) {
			ads->in.buf_phy[i] = dma_map_single(&ads->pdev->dev,
					ads->in.buffer[i],
					1 << PCM_BUFFER_MAX_SIZE_ORDER,
					DMA_FROM_DEVICE);
			BUG_ON(!ads->in.buf_phy[i]);
			setup_dma_rx_request(&ads->in.dma_req[i], &ads->in);
			ads->in.dma_req[i].dest_addr = ads->in.buf_phy[i];
		}
		ads->in.dma_chan = tegra_dma_allocate_channel(
				TEGRA_DMA_MODE_CONTINUOUS_SINGLE);
		if (!ads->in.dma_chan) {
			pr_err("%s: error allocating input DMA channel: %ld\n",
				__func__, PTR_ERR(ads->in.dma_chan));
			rc = -ENODEV;
			goto fail_rx;
		}
	}

	return 0;

fail_rx:
	if (mask & TEGRA_AUDIO_ENABLE_RX) {
		for (i = 0; i < ads->in.num_bufs; i++) {
			dma_unmap_single(&ads->pdev->dev, ads->in.buf_phy[i],
					1 << PCM_BUFFER_MAX_SIZE_ORDER,
					DMA_FROM_DEVICE);
			ads->in.buf_phy[i] = 0;
		}
		tegra_dma_free_channel(ads->in.dma_chan);
		ads->in.dma_chan = 0;
	}
fail_tx:
	if (mask & TEGRA_AUDIO_ENABLE_TX) {
		for (i = 0; i < ads->out.num_bufs; i++) {
			dma_unmap_single(&ads->pdev->dev, ads->out.buf_phy[i],
					1 << PCM_BUFFER_MAX_SIZE_ORDER,
					DMA_TO_DEVICE);
			ads->out.buf_phy[i] = 0;
		}
		tegra_dma_free_channel(ads->out.dma_chan);
		ads->out.dma_chan = 0;
	}

	return rc;
}

static void tear_down_dma(struct audio_driver_state *ads, int mask)
{
	int i;
	pr_info("%s\n", __func__);

	if (mask & TEGRA_AUDIO_ENABLE_TX) {
		tegra_dma_free_channel(ads->out.dma_chan);
		for (i = 0; i < ads->out.num_bufs; i++) {
			dma_unmap_single(&ads->pdev->dev, ads->out.buf_phy[i],
					buf_size(&ads->out),
					DMA_TO_DEVICE);
			ads->out.buf_phy[i] = 0;
		}
	}
	ads->out.dma_chan = NULL;

	if (mask & TEGRA_AUDIO_ENABLE_RX) {
		tegra_dma_free_channel(ads->in.dma_chan);
		for (i = 0; i < ads->in.num_bufs; i++) {
			dma_unmap_single(&ads->pdev->dev, ads->in.buf_phy[i],
					buf_size(&ads->in),
					DMA_FROM_DEVICE);
			ads->in.buf_phy[i] = 0;
		}
	}
	ads->in.dma_chan = NULL;
}

static void dma_tx_complete_callback(struct tegra_dma_req *req)
{
	unsigned long flags;
	struct audio_stream *aos = req->dev;
	unsigned req_num;

	spin_lock_irqsave(&aos->dma_req_lock, flags);

	req_num = req - aos->dma_req;
	pr_debug("%s: completed buffer %d size %d\n", __func__,
			req_num, req->bytes_transferred);
	BUG_ON(req_num >= aos->num_bufs);

	complete(&aos->comp[req_num]);

	if (!pending_buffer_requests(aos)) {
		pr_debug("%s: Playback underflow\n", __func__);
		complete(&aos->stop_completion);
	}

	spin_unlock_irqrestore(&aos->dma_req_lock, flags);
}

static void dma_rx_complete_callback(struct tegra_dma_req *req)
{
	unsigned long flags;
	struct audio_stream *ais = req->dev;
	unsigned req_num;

	spin_lock_irqsave(&ais->dma_req_lock, flags);

	req_num = req - ais->dma_req;
	pr_debug("%s: completed buffer %d size %d\n", __func__,
			req_num, req->bytes_transferred);
	BUG_ON(req_num >= ais->num_bufs);

	complete(&ais->comp[req_num]);

	if (!pending_buffer_requests(ais))
		pr_debug("%s: Capture overflow\n", __func__);

	spin_unlock_irqrestore(&ais->dma_req_lock, flags);
}

static void setup_dma_tx_request(struct tegra_dma_req *req,
		struct audio_stream *aos)
{
	struct audio_driver_state *ads = ads_from_out(aos);

	memset(req, 0, sizeof(*req));

	req->complete = dma_tx_complete_callback;
	req->dev = aos;
	req->to_memory = false;
	req->dest_addr = i2s_get_fifo_phy_base(ads->i2s_phys, I2S_FIFO_TX);
	req->dest_wrap = 4;
	if (ads->bit_format == TEGRA_AUDIO_BIT_FORMAT_DSP)
		req->dest_bus_width = ads->pdata->dsp_bus_width;
	else
		req->dest_bus_width = ads->pdata->i2s_bus_width;
	req->source_bus_width = 32;
	req->source_wrap = 0;
	req->req_sel = ads->dma_req_sel;
}

static void setup_dma_rx_request(struct tegra_dma_req *req,
		struct audio_stream *ais)
{
	struct audio_driver_state *ads = ads_from_in(ais);

	memset(req, 0, sizeof(*req));

	req->complete = dma_rx_complete_callback;
	req->dev = ais;
	req->to_memory = true;
	req->source_addr = i2s_get_fifo_phy_base(ads->i2s_phys, I2S_FIFO_RX);
	req->source_wrap = 4;
	if (ads->bit_format == TEGRA_AUDIO_BIT_FORMAT_DSP)
		req->source_bus_width = ads->pdata->dsp_bus_width;
	else
		req->source_bus_width = ads->pdata->i2s_bus_width;
	req->dest_bus_width = 32;
	req->dest_wrap = 0;
	req->req_sel = ads->dma_req_sel;
}

static int start_playback(struct audio_stream *aos,
			struct tegra_dma_req *req)
{
	int rc;
	unsigned long flags;
	struct audio_driver_state *ads = ads_from_out(aos);

	pr_debug("%s: (writing %d)\n",
			__func__, req->size);

	spin_lock_irqsave(&aos->dma_req_lock, flags);
#if 0
	i2s_fifo_clear(ads->i2s_base, I2S_FIFO_TX);
#endif
	i2s_fifo_set_attention_level(ads->i2s_base,
			I2S_FIFO_TX, aos->i2s_fifo_atn_level);

	i2s_fifo_enable(ads->i2s_base, I2S_FIFO_TX, 1);

	rc = tegra_dma_enqueue_req(aos->dma_chan, req);
	spin_unlock_irqrestore(&aos->dma_req_lock, flags);

	if (rc)
		pr_err("%s: could not enqueue TX DMA req\n", __func__);
	return rc;
}

/* Called with aos->dma_req_lock taken. */
static void stop_dma_playback(struct audio_stream *aos)
{
	int spin = 0;
	struct audio_driver_state *ads = ads_from_out(aos);
	pr_debug("%s\n", __func__);
	i2s_fifo_enable(ads->i2s_base, I2S_FIFO_TX, 0);
	while ((i2s_get_status(ads->i2s_base) & I2S_I2S_FIFO_TX_BUSY) &&
			spin < 100) {
		udelay(10);
		if (spin++ > 50)
			pr_info("%s: spin %d\n", __func__, spin);
	}
	if (spin == 100)
		pr_warn("%s: spinny\n", __func__);
}

/* This function may be called from either interrupt or process context. */
/* Called with ais->dma_req_lock taken. */
static int start_dma_recording(struct audio_stream *ais, int size)
{
	int i;
	struct audio_driver_state *ads = ads_from_in(ais);

	pr_debug("%s\n", __func__);

	BUG_ON(pending_buffer_requests(ais));

	for (i = 0; i < ais->num_bufs; i++) {
		init_completion(&ais->comp[i]);
		ais->dma_req[i].dest_addr = ais->buf_phy[i];
		ais->dma_req[i].size = size;
		tegra_dma_enqueue_req(ais->dma_chan, &ais->dma_req[i]);
	}

	ais->last_queued = ais->num_bufs - 1;

#if 0
	i2s_fifo_clear(ads->i2s_base, I2S_FIFO_RX);
#endif
	i2s_fifo_set_attention_level(ads->i2s_base,
			I2S_FIFO_RX, ais->i2s_fifo_atn_level);
	i2s_fifo_enable(ads->i2s_base, I2S_FIFO_RX, 1);
	return 0;
}

static void stop_dma_recording(struct audio_stream *ais)
{
	int spin = 0;
	struct audio_driver_state *ads = ads_from_in(ais);
	pr_debug("%s\n", __func__);
	tegra_dma_cancel(ais->dma_chan);
	i2s_fifo_enable(ads->i2s_base, I2S_FIFO_RX, 0);
	i2s_fifo_clear(ads->i2s_base, I2S_FIFO_RX);
	while ((i2s_get_status(ads->i2s_base) & I2S_I2S_FIFO_RX_BUSY) &&
			spin < 100) {
		udelay(10);
		if (spin++ > 50)
			pr_info("%s: spin %d\n", __func__, spin);
	}
	if (spin == 100)
		pr_warn("%s: spinny\n", __func__);
}

static irqreturn_t i2s_interrupt(int irq, void *data)
{
	struct audio_driver_state *ads = data;
	u32 status = i2s_get_status(ads->i2s_base);

	pr_debug("%s: %08x\n", __func__, status);

	if (status & I2S_FIFO_ERR)
		i2s_ack_status(ads->i2s_base);

	pr_debug("%s: done %08x\n", __func__, i2s_get_status(ads->i2s_base));
	return IRQ_HANDLED;
}

static ssize_t tegra_audio_write(struct file *file,
		const char __user *buf, size_t size, loff_t *off)
{
	ssize_t rc = 0;
	int out_buf;
	struct tegra_dma_req *req;
	struct audio_driver_state *ads = ads_from_misc_out(file);

	mutex_lock(&ads->out.lock);

	if (!IS_ALIGNED(size, 4) || size < 4 || size > buf_size(&ads->out)) {
		pr_err("%s: invalid user size %d\n", __func__, size);
		rc = -EINVAL;
		goto done;
	}

	pr_debug("%s: write %d bytes\n", __func__, size);

	if (ads->out.stop) {
		pr_debug("%s: playback has been cancelled\n", __func__);
		goto done;
	}

	/* Decide which buf is next. */
	out_buf = (ads->out.last_queued + 1) % ads->out.num_bufs;
	req = &ads->out.dma_req[out_buf];

	/* Wait for the buffer to be emptied (complete).  The maximum timeout
	 * value could be calculated dynamically based on buf_size(&ads->out).
	 * For a buffer size of 16k, at 44.1kHz/stereo/16-bit PCM, you would
	 * have ~93ms.
	 */
	pr_debug("%s: waiting for buffer %d\n", __func__, out_buf);
	rc = wait_for_completion_interruptible_timeout(
				&ads->out.comp[out_buf], HZ);
	if (!rc) {
		pr_err("%s: timeout", __func__);
		rc = -ETIMEDOUT;
		goto done;
	} else if (rc < 0) {
		pr_err("%s: wait error %d", __func__, rc);
		goto done;
	}

	/* Fill the buffer and enqueue it. */
	pr_debug("%s: acquired buffer %d, copying data\n", __func__, out_buf);
	rc = copy_from_user(ads->out.buffer[out_buf], buf, size);
	if (rc) {
		rc = -EFAULT;
		goto done;
	}

	prevent_suspend(&ads->out);

	req->size = size;
	dma_sync_single_for_device(NULL,
			req->source_addr, req->size, DMA_TO_DEVICE);
	ads->out.last_queued = out_buf;
	init_completion(&ads->out.stop_completion);

	rc = start_playback(&ads->out, req);
	if (!rc)
		rc = size;
	else
		allow_suspend(&ads->out);

done:
	mutex_unlock(&ads->out.lock);
	return rc;
}

static long tegra_audio_out_ioctl(struct file *file,
			unsigned int cmd, unsigned long arg)
{
	int rc = 0;
	struct audio_driver_state *ads = ads_from_misc_out_ctl(file);
	struct audio_stream *aos = &ads->out;

	mutex_lock(&aos->lock);

	switch (cmd) {
	case TEGRA_AUDIO_OUT_FLUSH:
		if (pending_buffer_requests(aos)) {
			pr_debug("%s: flushing\n", __func__);
			request_stop_nosync(aos);
			pr_debug("%s: flushed\n", __func__);
		}
		if (stop_playback_if_necessary(aos))
			pr_debug("%s: done (stopped)\n", __func__);
		aos->stop = false;
		break;
	case TEGRA_AUDIO_OUT_SET_NUM_BUFS: {
		unsigned int num;
		if (copy_from_user(&num, (const void __user *)arg,
					sizeof(num))) {
			rc = -EFAULT;
			break;
		}
		if (!num || num > I2S_MAX_NUM_BUFS) {
			pr_err("%s: invalid buffer count %d\n", __func__, num);
			rc = -EINVAL;
			break;
		}
		if (pending_buffer_requests(aos)) {
			pr_err("%s: playback in progress\n", __func__);
			rc = -EBUSY;
			break;
		}
		rc = init_stream_buffer(aos, num);
		if (rc < 0)
			break;
		aos->num_bufs = num;
		sound_ops->tear_down(ads, TEGRA_AUDIO_ENABLE_TX);
		sound_ops->setup(ads, TEGRA_AUDIO_ENABLE_TX);
		pr_debug("%s: num buf set to %d\n", __func__, num);
	}
		break;
	case TEGRA_AUDIO_OUT_GET_NUM_BUFS:
		if (copy_to_user((void __user *)arg,
				&aos->num_bufs, sizeof(aos->num_bufs)))
			rc = -EFAULT;
		break;
	default:
		rc = -EINVAL;
	}

	mutex_unlock(&aos->lock);
	return rc;
}

static long tegra_audio_ioctl(struct file *file,
			unsigned int cmd, unsigned long arg)
{
	int rc = 0;
	struct audio_driver_state *ads = ads_from_misc_ctl(file);
	unsigned int mode;
	bool dma_restart = false;

	mutex_lock(&ads->out.lock);
	mutex_lock(&ads->in.lock);

	switch (cmd) {
	case TEGRA_AUDIO_SET_BIT_FORMAT:
		if (copy_from_user(&mode, (const void __user *)arg,
					sizeof(mode))) {
			rc = -EFAULT;
			goto done;
		}
		dma_restart = (mode != ads->bit_format);
		switch (mode) {
		case TEGRA_AUDIO_BIT_FORMAT_DEFAULT:
			i2s_set_bit_format(ads->i2s_base, ads->pdata->mode);
			ads->bit_format = mode;
			break;
		case TEGRA_AUDIO_BIT_FORMAT_DSP:
			i2s_set_bit_format(ads->i2s_base, I2S_BIT_FORMAT_DSP);
			ads->bit_format = mode;
			break;
		default:
			pr_err("%s: Invald PCM mode %d", __func__, mode);
			rc = -EINVAL;
			goto done;
		}
		break;
	case TEGRA_AUDIO_GET_BIT_FORMAT:
		if (copy_to_user((void __user *)arg, &ads->bit_format,
				sizeof(mode)))
			rc = -EFAULT;
		goto done;
	}

	if (dma_restart) {
		pr_debug("%s: Restarting DMA due to configuration change.\n",
			__func__);
		if (pending_buffer_requests(&ads->out) || ads->in.active) {
			pr_err("%s: dma busy, cannot restart.\n", __func__);
			rc = -EBUSY;
			goto done;
		}
		sound_ops->tear_down(ads, ads->pdata->mask);
		i2s_configure(ads->pdev);
		sound_ops->setup(ads, ads->pdata->mask);
	}

done:
	mutex_unlock(&ads->in.lock);
	mutex_unlock(&ads->out.lock);
	return rc;
}

static long tegra_audio_in_ioctl(struct file *file,
			unsigned int cmd, unsigned long arg)
{
	int rc = 0;
	struct audio_driver_state *ads = ads_from_misc_in_ctl(file);
	struct audio_stream *ais = &ads->in;

	mutex_lock(&ais->lock);

	switch (cmd) {
	case TEGRA_AUDIO_IN_START:
		pr_debug("%s: start recording\n", __func__);
		ais->stop = false;
		break;
	case TEGRA_AUDIO_IN_STOP:
		pr_debug("%s: stop recording\n", __func__);
		if (ais->active) {
			/* Clean up DMA/I2S, and complete the completion */
			sound_ops->stop_recording(ais);
			complete(&ais->stop_completion);
			/* Set stop flag and allow suspend. */
			request_stop_nosync(ais);
		}
		break;
	case TEGRA_AUDIO_IN_SET_CONFIG: {
		struct tegra_audio_in_config cfg;

		if (ais->active) {
			pr_err("%s: recording in progress\n", __func__);
			rc = -EBUSY;
			break;
		}
		if (copy_from_user(&cfg, (const void __user *)arg,
					sizeof(cfg))) {
			rc = -EFAULT;
			break;
		}

		if (cfg.stereo && !ads->pdata->stereo_capture) {
			pr_err("%s: not capable of stereo capture.",
				__func__);
			rc = -EINVAL;
		}
		if (!rc) {
			pr_info("%s: setting input sampling rate to %d, %s\n",
				__func__, cfg.rate,
				cfg.stereo ? "stereo" : "mono");
			ads->in_config = cfg;
			ads->in_config.stereo = !!ads->in_config.stereo;
		}
	}
		break;
	case TEGRA_AUDIO_IN_GET_CONFIG:
		if (copy_to_user((void __user *)arg, &ads->in_config,
				sizeof(ads->in_config)))
			rc = -EFAULT;
		break;
	case TEGRA_AUDIO_IN_SET_NUM_BUFS: {
		unsigned int num;
		if (copy_from_user(&num, (const void __user *)arg,
					sizeof(num))) {
			rc = -EFAULT;
			break;
		}
		if (!num || num > I2S_MAX_NUM_BUFS) {
			pr_err("%s: invalid buffer count %d\n", __func__,
				num);
			rc = -EINVAL;
			break;
		}
		if (ais->active || pending_buffer_requests(ais)) {
			pr_err("%s: recording in progress\n", __func__);
			rc = -EBUSY;
			break;
		}
		rc = init_stream_buffer(ais, num);
		if (rc < 0)
			break;
		ais->num_bufs = num;
		sound_ops->tear_down(ads, TEGRA_AUDIO_ENABLE_RX);
		sound_ops->setup(ads, TEGRA_AUDIO_ENABLE_RX);
	}
		break;
	case TEGRA_AUDIO_IN_GET_NUM_BUFS:
		if (copy_to_user((void __user *)arg,
				&ais->num_bufs, sizeof(ais->num_bufs)))
			rc = -EFAULT;
		break;
	default:
		rc = -EINVAL;
	}

	mutex_unlock(&ais->lock);
	return rc;
}

static ssize_t tegra_audio_read(struct file *file, char __user *buf,
			size_t size, loff_t *off)
{
	ssize_t rc;
	ssize_t nr = 0;
	int in_buf;
	struct tegra_dma_req *req;
	struct audio_driver_state *ads = ads_from_misc_in(file);

	mutex_lock(&ads->in.lock);

	if (!IS_ALIGNED(size, 4) || size < 4 || size > buf_size(&ads->in)) {
		pr_err("%s: invalid size %d.\n", __func__, size);
		rc = -EINVAL;
		goto done;
	}

	pr_debug("%s: size %d\n", __func__, size);

	/* If we want recording to stop immediately after it gets cancelled,
	 * then we do not want to wait for the fifo to get drained.
	 */
	if (ads->in.stop) {
		pr_debug("%s: recording has been cancelled\n", __func__);
		rc = 0;
		goto done;
	}

	/* This function calls prevent_suspend() internally */
	rc = start_recording_if_necessary(&ads->in, size);
	if (rc < 0 && rc != -EALREADY) {
		pr_err("%s: could not start recording\n", __func__);
		goto done;
	}

	ads->in.active = true;

	/* Note that when tegra_audio_read() is called for the first time (or
	 * when all the buffers are empty), then it queues up all
	 * ads->in.num_bufs buffers, and in_buf is set to zero below.
	 */
	in_buf = (ads->in.last_queued + 1) % ads->in.num_bufs;

	/* Wait for the buffer to be filled (complete).  The maximum timeout
	 * value could be calculated dynamically based on buf_size(&ads->in).
	 * For a buffer size of 16k, at 44.1kHz/stereo/16-bit PCM, you would
	 * have ~93ms.
	 */
	rc = wait_for_completion_interruptible_timeout(
				&ads->in.comp[in_buf], HZ);
	if (!rc) {
		pr_err("%s: timeout", __func__);
		rc = -ETIMEDOUT;
		goto done;
	} else if (rc < 0) {
		pr_err("%s: wait error %d", __func__, rc);
		goto done;
	}

	req = &ads->in.dma_req[in_buf];

	nr = size > req->size ? req->size : size;
	req->size = size;
	dma_sync_single_for_cpu(NULL, ads->in.dma_req[in_buf].dest_addr,
				ads->in.dma_req[in_buf].size, DMA_FROM_DEVICE);
	rc = copy_to_user(buf, ads->in.buffer[in_buf], nr);
	if (rc) {
		rc = -EFAULT;
		goto done;
	}

	init_completion(&ads->in.stop_completion);

	ads->in.last_queued = in_buf;
	rc = tegra_dma_enqueue_req(ads->in.dma_chan, req);
	/* We've successfully enqueued this request before. */
	BUG_ON(rc);

	rc = nr;
	*off += nr;
done:
	mutex_unlock(&ads->in.lock);
	pr_debug("%s: done %d\n", __func__, rc);
	return rc;
}

static int tegra_audio_out_open(struct inode *inode, struct file *file)
{
	int rc = 0;
	int i;
	struct audio_driver_state *ads = ads_from_misc_out(file);

	pr_debug("%s\n", __func__);

	mutex_lock(&ads->out.lock);

	if (ads->out.opened) {
		rc = -EBUSY;
		goto done;
	}

	ads->out.opened = 1;
	ads->out.stop = false;

	for (i = 0; i < I2S_MAX_NUM_BUFS; i++) {
		init_completion(&ads->out.comp[i]);
		/* TX buf rest state is unqueued, complete. */
		complete(&ads->out.comp[i]);
	}

done:
	mutex_unlock(&ads->out.lock);
	return rc;
}

static int tegra_audio_out_release(struct inode *inode, struct file *file)
{
	struct audio_driver_state *ads = ads_from_misc_out(file);

	pr_debug("%s\n", __func__);

	mutex_lock(&ads->out.lock);
	ads->out.opened = 0;
	request_stop_nosync(&ads->out);
	if (stop_playback_if_necessary(&ads->out))
		pr_debug("%s: done (stopped)\n", __func__);
	allow_suspend(&ads->out);
	mutex_unlock(&ads->out.lock);
	pr_debug("%s: done\n", __func__);
	return 0;
}

static int tegra_audio_in_open(struct inode *inode, struct file *file)
{
	int rc = 0;
	int i;
	struct audio_driver_state *ads = ads_from_misc_in(file);

	pr_debug("%s\n", __func__);

	mutex_lock(&ads->in.lock);
	if (ads->in.opened) {
		rc = -EBUSY;
		goto done;
	}

	ads->in.opened = 1;
	ads->in.stop = false;

	for (i = 0; i < I2S_MAX_NUM_BUFS; i++) {
		init_completion(&ads->in.comp[i]);
		/* RX buf rest state is unqueued, complete. */
		complete(&ads->in.comp[i]);
	}

done:
	mutex_unlock(&ads->in.lock);
	return rc;
}

static int tegra_audio_in_release(struct inode *inode, struct file *file)
{
	struct audio_driver_state *ads = ads_from_misc_in(file);

	pr_debug("%s\n", __func__);

	mutex_lock(&ads->in.lock);
	ads->in.opened = 0;
	if (ads->in.active) {
		sound_ops->stop_recording(&ads->in);
		complete(&ads->in.stop_completion);
		request_stop_nosync(&ads->in);
	}
	allow_suspend(&ads->in);
	mutex_unlock(&ads->in.lock);
	pr_debug("%s: done\n", __func__);
	return 0;
}

static const struct file_operations tegra_audio_out_fops = {
	.owner = THIS_MODULE,
	.open = tegra_audio_out_open,
	.release = tegra_audio_out_release,
	.write = tegra_audio_write,
};

static const struct file_operations tegra_audio_in_fops = {
	.owner = THIS_MODULE,
	.open = tegra_audio_in_open,
	.read = tegra_audio_read,
	.release = tegra_audio_in_release,
};

static int tegra_audio_ctl_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int tegra_audio_ctl_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations tegra_audio_out_ctl_fops = {
	.owner = THIS_MODULE,
	.open = tegra_audio_ctl_open,
	.release = tegra_audio_ctl_release,
	.unlocked_ioctl = tegra_audio_out_ioctl,
};

static const struct file_operations tegra_audio_in_ctl_fops = {
	.owner = THIS_MODULE,
	.open = tegra_audio_ctl_open,
	.release = tegra_audio_ctl_release,
	.unlocked_ioctl = tegra_audio_in_ioctl,
};

static const struct file_operations tegra_audio_ctl_fops = {
	.owner = THIS_MODULE,
	.open = tegra_audio_ctl_open,
	.release = tegra_audio_ctl_release,
	.unlocked_ioctl = tegra_audio_ioctl,
};

static int init_stream_buffer(struct audio_stream *s, int num)
{
	int i, j;
	pr_debug("%s (num %d)\n", __func__,  num);

	for (i = 0; i < num; i++) {
		kfree(s->buffer[i]);
		s->buffer[i] =
			kmalloc((1 << PCM_BUFFER_MAX_SIZE_ORDER),
					GFP_KERNEL | GFP_DMA);
		if (!s->buffer[i]) {
			pr_err("%s: could not allocate buffer\n", __func__);
			for (j = i - 1; j >= 0; j--) {
				kfree(s->buffer[j]);
				s->buffer[j] = 0;
			}
			return -ENOMEM;
		}
	}
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
	return sprintf(buf, "dma\n");
}

static ssize_t dma_toggle_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	pr_err("%s: Not implemented.", __func__);
	return 0;
}

static DEVICE_ATTR(dma_toggle, 0644, dma_toggle_show, dma_toggle_store);

static ssize_t __attr_fifo_atn_read(char *buf, int atn_lvl)
{
	switch (atn_lvl) {
	case I2S_FIFO_ATN_LVL_ONE_SLOT:
		strncpy(buf, "1\n", 2);
		return 2;
	case I2S_FIFO_ATN_LVL_FOUR_SLOTS:
		strncpy(buf, "4\n", 2);
		return 2;
	case I2S_FIFO_ATN_LVL_EIGHT_SLOTS:
		strncpy(buf, "8\n", 2);
		return 2;
	case I2S_FIFO_ATN_LVL_TWELVE_SLOTS:
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
		lvl = I2S_FIFO_ATN_LVL_ONE_SLOT;
		break;
	case 4:
		lvl = I2S_FIFO_ATN_LVL_FOUR_SLOTS;
		break;
	case 8:
		lvl = I2S_FIFO_ATN_LVL_EIGHT_SLOTS;
		break;
	case 12:
		lvl = I2S_FIFO_ATN_LVL_TWELVE_SLOTS;
		break;
	default:
		pr_err("%s: invalid attention level %d\n", __func__, lvl);
		return -EINVAL;
	}

	*fifo_lvl = lvl;
	pr_info("%s: fifo level %d\n", __func__, *fifo_lvl);

	return size;
}

static ssize_t tx_fifo_atn_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct tegra_audio_platform_data *pdata = dev->platform_data;
	struct audio_driver_state *ads = pdata->driver_data;
	return __attr_fifo_atn_read(buf, ads->out.i2s_fifo_atn_level);
}

static ssize_t tx_fifo_atn_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	ssize_t rc;
	struct tegra_audio_platform_data *pdata = dev->platform_data;
	struct audio_driver_state *ads = pdata->driver_data;
	mutex_lock(&ads->out.lock);
	if (pending_buffer_requests(&ads->out)) {
		pr_err("%s: playback in progress.\n", __func__);
		rc = -EBUSY;
		goto done;
	}
	rc = __attr_fifo_atn_write(ads, &ads->out,
			&ads->out.i2s_fifo_atn_level,
			buf, count);
done:
	mutex_unlock(&ads->out.lock);
	return rc;
}

static DEVICE_ATTR(tx_fifo_atn, 0644, tx_fifo_atn_show, tx_fifo_atn_store);

static ssize_t rx_fifo_atn_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct tegra_audio_platform_data *pdata = dev->platform_data;
	struct audio_driver_state *ads = pdata->driver_data;
	return __attr_fifo_atn_read(buf, ads->in.i2s_fifo_atn_level);
}

static ssize_t rx_fifo_atn_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	ssize_t rc;
	struct tegra_audio_platform_data *pdata = dev->platform_data;
	struct audio_driver_state *ads = pdata->driver_data;
	mutex_lock(&ads->in.lock);
	if (ads->in.active) {
		pr_err("%s: recording in progress.\n", __func__);
		rc = -EBUSY;
		goto done;
	}
	rc = __attr_fifo_atn_write(ads, &ads->in,
			&ads->in.i2s_fifo_atn_level,
			buf, count);
done:
	mutex_unlock(&ads->in.lock);
	return rc;
}

static DEVICE_ATTR(rx_fifo_atn, 0644, rx_fifo_atn_show, rx_fifo_atn_store);

static int tegra_audio_probe(struct platform_device *pdev)
{
	int rc, i;
	struct resource *res;
	struct clk *i2s_clk, *dap_mclk;
	struct audio_driver_state *state;

	pr_info("%s\n", __func__);

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->pdev = pdev;
	state->pdata = pdev->dev.platform_data;
	state->pdata->driver_data = state;
	BUG_ON(!state->pdata);

	if (!(state->pdata->mask &
			(TEGRA_AUDIO_ENABLE_TX | TEGRA_AUDIO_ENABLE_RX))) {
		dev_err(&pdev->dev, "neither tx nor rx is enabled!\n");
		return -EIO;
	}

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

	i2s_clk = clk_get(&pdev->dev, NULL);
	if (!i2s_clk) {
		dev_err(&pdev->dev, "%s: could not get i2s clock\n",
			__func__);
		return -EIO;
	}

	clk_set_rate(i2s_clk, state->pdata->i2s_clk_rate);
	if (clk_enable(i2s_clk)) {
		dev_err(&pdev->dev, "%s: failed to enable i2s clock\n",
			__func__);
		return -EIO;
	}
	pr_info("%s: i2s_clk rate %ld\n", __func__, clk_get_rate(i2s_clk));

	dap_mclk = tegra_get_clock_by_name(state->pdata->dap_clk);
	if (!dap_mclk) {
		dev_err(&pdev->dev, "%s: could not get DAP clock\n",
			__func__);
		return -EIO;
	}
	clk_enable(dap_mclk);

	rc = i2s_configure(pdev);
	if (rc < 0)
		return rc;

	if ((state->pdata->mask & TEGRA_AUDIO_ENABLE_TX)) {
		state->out.opened = 0;
		state->out.active = false;
		mutex_init(&state->out.lock);
		init_completion(&state->out.stop_completion);
		spin_lock_init(&state->out.dma_req_lock);
		state->out.dma_chan = NULL;
		state->out.i2s_fifo_atn_level = I2S_FIFO_ATN_LVL_FOUR_SLOTS;
		state->out.num_bufs = I2S_DEFAULT_TX_NUM_BUFS;
		for (i = 0; i < I2S_MAX_NUM_BUFS; i++) {
			init_completion(&state->out.comp[i]);
			/* TX buf rest state is unqueued, complete. */
			complete(&state->out.comp[i]);
			state->out.buffer[i] = 0;
			state->out.buf_phy[i] = 0;
		}
		state->out.last_queued = 0;
		rc = init_stream_buffer(&state->out, state->out.num_bufs);
		if (rc < 0)
			return rc;

		INIT_WORK(&state->out.allow_suspend_work, allow_suspend_worker);

		snprintf(state->out.wake_lock_name,
			sizeof(state->out.wake_lock_name),
			"i2s.%d-audio-out", state->pdev->id);
		wake_lock_init(&state->out.wake_lock, WAKE_LOCK_SUSPEND,
			state->out.wake_lock_name);

		rc = setup_misc_device(&state->misc_out,
			&tegra_audio_out_fops,
			"audio%d_out", state->pdev->id);
		if (rc < 0)
			return rc;

		rc = setup_misc_device(&state->misc_out_ctl,
				&tegra_audio_out_ctl_fops,
				"audio%d_out_ctl", state->pdev->id);
		if (rc < 0)
			return rc;
	}

	if ((state->pdata->mask & TEGRA_AUDIO_ENABLE_RX)) {
		state->in.opened = 0;
		state->in.active = false;
		mutex_init(&state->in.lock);
		init_completion(&state->in.stop_completion);
		spin_lock_init(&state->in.dma_req_lock);
		state->in.dma_chan = NULL;
		state->in.i2s_fifo_atn_level = I2S_FIFO_ATN_LVL_FOUR_SLOTS;
		state->in.num_bufs = I2S_DEFAULT_RX_NUM_BUFS;
		for (i = 0; i < I2S_MAX_NUM_BUFS; i++) {
			init_completion(&state->in.comp[i]);
			/* RX buf rest state is unqueued, complete. */
			complete(&state->in.comp[i]);
			state->in.buffer[i] = 0;
			state->in.buf_phy[i] = 0;
		}
		state->in.last_queued = 0;
		rc = init_stream_buffer(&state->in, state->in.num_bufs);
		if (rc < 0)
			return rc;

		INIT_WORK(&state->in.allow_suspend_work, allow_suspend_worker);

		snprintf(state->in.wake_lock_name,
			sizeof(state->in.wake_lock_name),
			"i2s.%d-audio-in", state->pdev->id);
		wake_lock_init(&state->in.wake_lock, WAKE_LOCK_SUSPEND,
			state->in.wake_lock_name);

		rc = setup_misc_device(&state->misc_in,
			&tegra_audio_in_fops,
			"audio%d_in", state->pdev->id);
		if (rc < 0)
			return rc;

		rc = setup_misc_device(&state->misc_in_ctl,
			&tegra_audio_in_ctl_fops,
			"audio%d_in_ctl", state->pdev->id);
		if (rc < 0)
			return rc;
	}

	if (request_irq(state->irq, i2s_interrupt,
			IRQF_DISABLED, state->pdev->name, state) < 0) {
		dev_err(&pdev->dev,
			"%s: could not register handler for irq %d\n",
			__func__, state->irq);
		return -EIO;
	}

	rc = setup_misc_device(&state->misc_ctl,
			&tegra_audio_ctl_fops,
			"audio%d_ctl", state->pdev->id);
	if (rc < 0)
		return rc;

	sound_ops->setup(state, state->pdata->mask);

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

	rc = device_create_file(&pdev->dev, &dev_attr_rx_fifo_atn);
	if (rc < 0) {
		dev_err(&pdev->dev, "%s: could not create sysfs entry %s: %d\n",
			__func__, dev_attr_rx_fifo_atn.attr.name, rc);
		return rc;
	}

	state->in_config.rate = 11025;
	state->in_config.stereo = false;

	return 0;
}

#ifdef CONFIG_PM
static int tegra_audio_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	/* dev_info(&pdev->dev, "%s\n", __func__); */
	return 0;
}

static int tegra_audio_resume(struct platform_device *pdev)
{
	return i2s_configure(pdev);
}
#endif /* CONFIG_PM */

static struct platform_driver tegra_audio_driver = {
	.driver = {
		.name = "i2s",
		.owner = THIS_MODULE,
	},
	.probe = tegra_audio_probe,
#ifdef CONFIG_PM
	.suspend = tegra_audio_suspend,
	.resume = tegra_audio_resume,
#endif
};

static int __init tegra_audio_init(void)
{
	return platform_driver_register(&tegra_audio_driver);
}

module_init(tegra_audio_init);
MODULE_LICENSE("GPL");
