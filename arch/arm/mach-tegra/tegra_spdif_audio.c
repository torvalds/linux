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
#include <linux/wakelock.h>
#include <linux/delay.h>
#include <linux/tegra_audio.h>
#include <linux/pm.h>
#include <linux/workqueue.h>

#include <mach/dma.h>
#include <mach/iomap.h>
#include <mach/spdif.h>
#include <mach/audio.h>
#include <mach/irqs.h>

#include "clock.h"

#define PCM_BUFFER_MAX_SIZE_ORDER	(PAGE_SHIFT)

#define SPDIF_MAX_NUM_BUFS 4
/* Todo: Add IOCTL to configure the number of buffers. */
#define SPDIF_DEFAULT_TX_NUM_BUFS 2
#define SPDIF_DEFAULT_RX_NUM_BUFS 2
/* per stream (input/output) */
struct audio_stream {
	int opened;
	struct mutex lock;

	bool active; /* is DMA in progress? */
	int num_bufs;
	void *buffer[SPDIF_MAX_NUM_BUFS];
	dma_addr_t buf_phy[SPDIF_MAX_NUM_BUFS];
	struct completion comp[SPDIF_MAX_NUM_BUFS];
	struct tegra_dma_req dma_req[SPDIF_MAX_NUM_BUFS];
	int last_queued;

	int spdif_fifo_atn_level;

	struct tegra_dma_channel *dma_chan;
	bool stop;
	struct completion stop_completion;
	spinlock_t dma_req_lock;

	struct work_struct allow_suspend_work;
	struct wake_lock wake_lock;
	char wake_lock_name[100];
};

struct audio_driver_state {
	struct list_head next;

	struct platform_device *pdev;
	struct tegra_audio_platform_data *pdata;
	phys_addr_t spdif_phys;
	unsigned long spdif_base;

	unsigned long dma_req_sel;
	bool fifo_init;

	int irq;

	struct miscdevice misc_out;
	struct miscdevice misc_out_ctl;
	struct audio_stream out;
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

static inline struct audio_driver_state *ads_from_out(
			struct audio_stream *aos)
{
	return container_of(aos, struct audio_driver_state, out);
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
	val &= ~(SPDIF_CTRL_0_TX_EN | SPDIF_CTRL_0_TC_EN | SPDIF_CTRL_0_TU_EN);
	val |= on ? (SPDIF_CTRL_0_TX_EN) : 0;
	val |= on ? (SPDIF_CTRL_0_TC_EN) : 0;

	spdif_writel(base, val, SPDIF_CTRL_0);
}
#if 0
static bool spdif_is_fifo_enabled(unsigned long base)
{
	u32 val = spdif_readl(base, SPDIF_CTRL_0);
	return !!(val & SPDIF_CTRL_0_TX_EN);
}
#endif

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

#if 0
static void spdif_set_fifo_irq_on_err(unsigned long base, int on)
{
	u32 val = spdif_readl(base, SPDIF_CTRL_0);
	val &= ~SPDIF_CTRL_0_IE_TXE;
	val |= on ? SPDIF_CTRL_0_IE_TXE : 0;
	spdif_writel(base, val, SPDIF_CTRL_0);
}
#endif


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
	struct clk *spdif_clk;

	unsigned int ch_sta[] = {
		0x0, /* 44.1, default values */
		0x0,
		0x0,
		0x0,
		0x0,
		0x0,
	};

	switch (sample_rate) {
	case 32000:
		clock_freq = 4096000; /* 4.0960 MHz */
		ch_sta[0] = 0x3 << 24;
		ch_sta[1] = 0xC << 4;
		break;
	case 44100:
		clock_freq = 5644800; /* 5.6448 MHz */
		ch_sta[0] = 0x0;
		ch_sta[1] = 0xF << 4;
		break;
	case 48000:
		clock_freq = 6144000; /* 6.1440MHz */
		ch_sta[0] = 0x2 << 24;
		ch_sta[1] = 0xD << 4;
		break;
	case 88200:
		clock_freq = 11289600; /* 11.2896 MHz */
		break;
	case 96000:
		clock_freq = 12288000; /* 12.288 MHz */
		break;
	case 176400:
		clock_freq = 22579200; /* 22.5792 MHz */
		break;
	case 192000:
		clock_freq = 24576000; /* 24.5760 MHz */
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
		dev_err(&state->pdev->dev,
			"%s: failed to enable spdif_clk clock\n", __func__);
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

static int init_stream_buffer(struct audio_stream *, int);

static int setup_dma(struct audio_driver_state *);
static void tear_down_dma(struct audio_driver_state *);
static void stop_dma_playback(struct audio_stream *);


struct sound_ops {
	int (*setup)(struct audio_driver_state *);
	void (*tear_down)(struct audio_driver_state *);
	void (*stop_playback)(struct audio_stream *);
};

static const struct sound_ops dma_sound_ops = {
	.setup = setup_dma,
	.tear_down = tear_down_dma,
	.stop_playback = stop_dma_playback,
};

static const struct sound_ops *sound_ops = &dma_sound_ops;



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

/* playback */
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

/* Ask for playback to stop.  The _nosync means that
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

static int setup_dma(struct audio_driver_state *ads)
{
	int rc, i;
	pr_info("%s\n", __func__);

	/* setup audio playback */
	for (i = 0; i < ads->out.num_bufs; i++) {
		ads->out.buf_phy[i] = dma_map_single(&ads->pdev->dev,
				ads->out.buffer[i],
				buf_size(&ads->out),
				DMA_TO_DEVICE);
		BUG_ON(!ads->out.buf_phy[i]);
		setup_dma_tx_request(&ads->out.dma_req[i], &ads->out);
		ads->out.dma_req[i].source_addr = ads->out.buf_phy[i];
	}
	ads->out.dma_chan =
		 tegra_dma_allocate_channel(TEGRA_DMA_MODE_CONTINUOUS_SINGLE);
	if (!ads->out.dma_chan) {
		pr_err("%s: error alloc output DMA channel: %ld\n",
			__func__, PTR_ERR(ads->out.dma_chan));
		rc = -ENODEV;
		goto fail_tx;
	}
	return 0;


fail_tx:

	for (i = 0; i < ads->out.num_bufs; i++) {
		dma_unmap_single(&ads->pdev->dev, ads->out.buf_phy[i],
				buf_size(&ads->out),
				DMA_TO_DEVICE);
		ads->out.buf_phy[i] = 0;
	}
	tegra_dma_free_channel(ads->out.dma_chan);
	ads->out.dma_chan = 0;


	return rc;
}

static void tear_down_dma(struct audio_driver_state *ads)
{
	int i;
	pr_info("%s\n", __func__);


	tegra_dma_free_channel(ads->out.dma_chan);
	for (i = 0; i < ads->out.num_bufs; i++) {
		dma_unmap_single(&ads->pdev->dev, ads->out.buf_phy[i],
				buf_size(&ads->out),
				DMA_TO_DEVICE);
		ads->out.buf_phy[i] = 0;
	}

	ads->out.dma_chan = NULL;
}

static void dma_tx_complete_callback(struct tegra_dma_req *req)
{
	struct audio_stream *aos = req->dev;
	unsigned req_num;

	req_num = req - aos->dma_req;
	pr_debug("%s: completed buffer %d size %d\n", __func__,
			req_num, req->bytes_transferred);
	BUG_ON(req_num >= aos->num_bufs);

	complete(&aos->comp[req_num]);

	if (!pending_buffer_requests(aos)) {
		pr_debug("%s: Playback underflow", __func__);
		complete(&aos->stop_completion);
	}
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
	req->dest_bus_width = 32;
	req->dest_wrap = 4;
	req->source_wrap = 0;
	req->source_bus_width = 32;
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
	spdif_fifo_clear(ads->spdif_base);
#endif

	spdif_fifo_set_attention_level(ads->spdif_base,
		ads->out.spdif_fifo_atn_level);

	if (ads->fifo_init) {
		spdif_set_bit_mode(ads->spdif_base, SPDIF_BIT_MODE_MODE16BIT);
		spdif_set_fifo_packed(ads->spdif_base, 1);
		ads->fifo_init = false;
	}

	spdif_fifo_enable(ads->spdif_base, 1);

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
	spdif_fifo_enable(ads->spdif_base, 0);
	while ((spdif_get_status(ads->spdif_base) & SPDIF_STATUS_0_TX_BSY) &&
			spin < 100) {
		udelay(10);
		if (spin++ > 50)
			pr_info("%s: spin %d\n", __func__, spin);
	}
	if (spin == 100)
		pr_warn("%s: spinny\n", __func__);
	ads->fifo_init = true;
}



static irqreturn_t spdif_interrupt(int irq, void *data)
{
	struct audio_driver_state *ads = data;
	u32 status = spdif_get_status(ads->spdif_base);

	pr_debug("%s: %08x\n", __func__, status);

/*	if (status & SPDIF_STATUS_0_TX_ERR) */
		spdif_ack_status(ads->spdif_base);

	pr_debug("%s: done %08x\n", __func__,
			spdif_get_status(ads->spdif_base));
	return IRQ_HANDLED;
}

static ssize_t tegra_spdif_write(struct file *file,
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

static long tegra_spdif_out_ioctl(struct file *file,
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
		if (!num || num > SPDIF_MAX_NUM_BUFS) {
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
		sound_ops->setup(ads);
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


static int tegra_spdif_out_open(struct inode *inode, struct file *file)
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

	for (i = 0; i < SPDIF_MAX_NUM_BUFS; i++) {
		init_completion(&ads->out.comp[i]);
		/* TX buf rest state is unqueued, complete. */
		complete(&ads->out.comp[i]);
	}

done:
	mutex_unlock(&ads->out.lock);
	return rc;
}

static int tegra_spdif_out_release(struct inode *inode, struct file *file)
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

static int init_stream_buffer(struct audio_stream *s, int num)
{
	int i, j;
	pr_debug("%s (num %d)\n", __func__,  num);

	for (i = 0; i < num; i++) {
		kfree(s->buffer[i]);
		s->buffer[i] =
			kmalloc(buf_size(s), GFP_KERNEL | GFP_DMA);
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
	return __attr_fifo_atn_read(buf, ads->out.spdif_fifo_atn_level);
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
			&ads->out.spdif_fifo_atn_level,
			buf, count);
done:
	mutex_unlock(&ads->out.lock);
	return rc;
}

static DEVICE_ATTR(tx_fifo_atn, 0644, tx_fifo_atn_show, tx_fifo_atn_store);


static int spdif_configure(struct platform_device *pdev)
{
	struct tegra_audio_platform_data *pdata = pdev->dev.platform_data;
	struct audio_driver_state *state = pdata->driver_data;

	if (!state)
		return -ENOMEM;

	/* disable interrupts from SPDIF */
	spdif_writel(state->spdif_base, 0x0, SPDIF_CTRL_0);
	spdif_fifo_clear(state->spdif_base);
	spdif_enable_fifos(state->spdif_base, 0);

	spdif_set_bit_mode(state->spdif_base, SPDIF_BIT_MODE_MODE16BIT);
	spdif_set_fifo_packed(state->spdif_base, 1);

	spdif_fifo_set_attention_level(state->spdif_base,
		state->out.spdif_fifo_atn_level);

	spdif_set_sample_rate(state, 44100);

	state->fifo_init = true;
	return 0;
}

static int tegra_spdif_probe(struct platform_device *pdev)
{
	int rc, i;
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

	rc = spdif_configure(pdev);
	if (rc < 0)
		return rc;

	state->out.opened = 0;
	state->out.active = false;
	mutex_init(&state->out.lock);
	init_completion(&state->out.stop_completion);
	spin_lock_init(&state->out.dma_req_lock);
	state->out.dma_chan = NULL;
	state->out.num_bufs = SPDIF_DEFAULT_TX_NUM_BUFS;
	for (i = 0; i < SPDIF_MAX_NUM_BUFS; i++) {
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
	snprintf(state->out.wake_lock_name, sizeof(state->out.wake_lock_name),
		"tegra-audio-spdif");
	wake_lock_init(&state->out.wake_lock, WAKE_LOCK_SUSPEND,
			state->out.wake_lock_name);

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

#ifdef CONFIG_PM
static int tegra_spdif_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	/* dev_info(&pdev->dev, "%s\n", __func__); */
	return 0;
}

static int tegra_spdif_resume(struct platform_device *pdev)
{
	return spdif_configure(pdev);
}
#endif /* CONFIG_PM */

static struct platform_driver tegra_spdif_driver = {
	.driver = {
		.name = "spdif_out",
		.owner = THIS_MODULE,
	},
	.probe = tegra_spdif_probe,
#ifdef CONFIG_PM
	.suspend = tegra_spdif_suspend,
	.resume = tegra_spdif_resume,
#endif
};

static int __init tegra_spdif_init(void)
{
	return platform_driver_register(&tegra_spdif_driver);
}

module_init(tegra_spdif_init);
MODULE_LICENSE("GPL");
