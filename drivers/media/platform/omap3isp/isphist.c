/*
 * isphist.c
 *
 * TI OMAP3 ISP - Histogram module
 *
 * Copyright (C) 2010 Nokia Corporation
 * Copyright (C) 2009 Texas Instruments, Inc.
 *
 * Contacts: David Cohen <dacohen@gmail.com>
 *	     Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/device.h>

#include "isp.h"
#include "ispreg.h"
#include "isphist.h"

#define HIST_CONFIG_DMA	1

#define HIST_USING_DMA(hist) ((hist)->dma_ch >= 0)

/*
 * hist_reset_mem - clear Histogram memory before start stats engine.
 */
static void hist_reset_mem(struct ispstat *hist)
{
	struct isp_device *isp = hist->isp;
	struct omap3isp_hist_config *conf = hist->priv;
	unsigned int i;

	isp_reg_writel(isp, 0, OMAP3_ISP_IOMEM_HIST, ISPHIST_ADDR);

	/*
	 * By setting it, the histogram internal buffer is being cleared at the
	 * same time it's being read. This bit must be cleared afterwards.
	 */
	isp_reg_set(isp, OMAP3_ISP_IOMEM_HIST, ISPHIST_CNT, ISPHIST_CNT_CLEAR);

	/*
	 * We'll clear 4 words at each iteration for optimization. It avoids
	 * 3/4 of the jumps. We also know HIST_MEM_SIZE is divisible by 4.
	 */
	for (i = OMAP3ISP_HIST_MEM_SIZE / 4; i > 0; i--) {
		isp_reg_readl(isp, OMAP3_ISP_IOMEM_HIST, ISPHIST_DATA);
		isp_reg_readl(isp, OMAP3_ISP_IOMEM_HIST, ISPHIST_DATA);
		isp_reg_readl(isp, OMAP3_ISP_IOMEM_HIST, ISPHIST_DATA);
		isp_reg_readl(isp, OMAP3_ISP_IOMEM_HIST, ISPHIST_DATA);
	}
	isp_reg_clr(isp, OMAP3_ISP_IOMEM_HIST, ISPHIST_CNT, ISPHIST_CNT_CLEAR);

	hist->wait_acc_frames = conf->num_acc_frames;
}

static void hist_dma_config(struct ispstat *hist)
{
	struct isp_device *isp = hist->isp;

	hist->dma_config.data_type = OMAP_DMA_DATA_TYPE_S32;
	hist->dma_config.sync_mode = OMAP_DMA_SYNC_ELEMENT;
	hist->dma_config.frame_count = 1;
	hist->dma_config.src_amode = OMAP_DMA_AMODE_CONSTANT;
	hist->dma_config.src_start = isp->mmio_base_phys[OMAP3_ISP_IOMEM_HIST]
				   + ISPHIST_DATA;
	hist->dma_config.dst_amode = OMAP_DMA_AMODE_POST_INC;
	hist->dma_config.src_or_dst_synch = OMAP_DMA_SRC_SYNC;
}

/*
 * hist_setup_regs - Helper function to update Histogram registers.
 */
static void hist_setup_regs(struct ispstat *hist, void *priv)
{
	struct isp_device *isp = hist->isp;
	struct omap3isp_hist_config *conf = priv;
	int c;
	u32 cnt;
	u32 wb_gain;
	u32 reg_hor[OMAP3ISP_HIST_MAX_REGIONS];
	u32 reg_ver[OMAP3ISP_HIST_MAX_REGIONS];

	if (!hist->update || hist->state == ISPSTAT_DISABLED ||
	    hist->state == ISPSTAT_DISABLING)
		return;

	cnt = conf->cfa << ISPHIST_CNT_CFA_SHIFT;

	wb_gain = conf->wg[0] << ISPHIST_WB_GAIN_WG00_SHIFT;
	wb_gain |= conf->wg[1] << ISPHIST_WB_GAIN_WG01_SHIFT;
	wb_gain |= conf->wg[2] << ISPHIST_WB_GAIN_WG02_SHIFT;
	if (conf->cfa == OMAP3ISP_HIST_CFA_BAYER)
		wb_gain |= conf->wg[3] << ISPHIST_WB_GAIN_WG03_SHIFT;

	/* Regions size and position */
	for (c = 0; c < OMAP3ISP_HIST_MAX_REGIONS; c++) {
		if (c < conf->num_regions) {
			reg_hor[c] = conf->region[c].h_start <<
				     ISPHIST_REG_START_SHIFT;
			reg_hor[c] = conf->region[c].h_end <<
				     ISPHIST_REG_END_SHIFT;
			reg_ver[c] = conf->region[c].v_start <<
				     ISPHIST_REG_START_SHIFT;
			reg_ver[c] = conf->region[c].v_end <<
				     ISPHIST_REG_END_SHIFT;
		} else {
			reg_hor[c] = 0;
			reg_ver[c] = 0;
		}
	}

	cnt |= conf->hist_bins << ISPHIST_CNT_BINS_SHIFT;
	switch (conf->hist_bins) {
	case OMAP3ISP_HIST_BINS_256:
		cnt |= (ISPHIST_IN_BIT_WIDTH_CCDC - 8) <<
			ISPHIST_CNT_SHIFT_SHIFT;
		break;
	case OMAP3ISP_HIST_BINS_128:
		cnt |= (ISPHIST_IN_BIT_WIDTH_CCDC - 7) <<
			ISPHIST_CNT_SHIFT_SHIFT;
		break;
	case OMAP3ISP_HIST_BINS_64:
		cnt |= (ISPHIST_IN_BIT_WIDTH_CCDC - 6) <<
			ISPHIST_CNT_SHIFT_SHIFT;
		break;
	default: /* OMAP3ISP_HIST_BINS_32 */
		cnt |= (ISPHIST_IN_BIT_WIDTH_CCDC - 5) <<
			ISPHIST_CNT_SHIFT_SHIFT;
		break;
	}

	hist_reset_mem(hist);

	isp_reg_writel(isp, cnt, OMAP3_ISP_IOMEM_HIST, ISPHIST_CNT);
	isp_reg_writel(isp, wb_gain,  OMAP3_ISP_IOMEM_HIST, ISPHIST_WB_GAIN);
	isp_reg_writel(isp, reg_hor[0], OMAP3_ISP_IOMEM_HIST, ISPHIST_R0_HORZ);
	isp_reg_writel(isp, reg_ver[0], OMAP3_ISP_IOMEM_HIST, ISPHIST_R0_VERT);
	isp_reg_writel(isp, reg_hor[1], OMAP3_ISP_IOMEM_HIST, ISPHIST_R1_HORZ);
	isp_reg_writel(isp, reg_ver[1], OMAP3_ISP_IOMEM_HIST, ISPHIST_R1_VERT);
	isp_reg_writel(isp, reg_hor[2], OMAP3_ISP_IOMEM_HIST, ISPHIST_R2_HORZ);
	isp_reg_writel(isp, reg_ver[2], OMAP3_ISP_IOMEM_HIST, ISPHIST_R2_VERT);
	isp_reg_writel(isp, reg_hor[3], OMAP3_ISP_IOMEM_HIST, ISPHIST_R3_HORZ);
	isp_reg_writel(isp, reg_ver[3], OMAP3_ISP_IOMEM_HIST, ISPHIST_R3_VERT);

	hist->update = 0;
	hist->config_counter += hist->inc_config;
	hist->inc_config = 0;
	hist->buf_size = conf->buf_size;
}

static void hist_enable(struct ispstat *hist, int enable)
{
	if (enable) {
		isp_reg_set(hist->isp, OMAP3_ISP_IOMEM_HIST, ISPHIST_PCR,
			    ISPHIST_PCR_ENABLE);
		omap3isp_subclk_enable(hist->isp, OMAP3_ISP_SUBCLK_HIST);
	} else {
		isp_reg_clr(hist->isp, OMAP3_ISP_IOMEM_HIST, ISPHIST_PCR,
			    ISPHIST_PCR_ENABLE);
		omap3isp_subclk_disable(hist->isp, OMAP3_ISP_SUBCLK_HIST);
	}
}

static int hist_busy(struct ispstat *hist)
{
	return isp_reg_readl(hist->isp, OMAP3_ISP_IOMEM_HIST, ISPHIST_PCR)
						& ISPHIST_PCR_BUSY;
}

static void hist_dma_cb(int lch, u16 ch_status, void *data)
{
	struct ispstat *hist = data;

	if (ch_status & ~OMAP_DMA_BLOCK_IRQ) {
		dev_dbg(hist->isp->dev, "hist: DMA error. status = 0x%04x\n",
			ch_status);
		omap_stop_dma(lch);
		hist_reset_mem(hist);
		atomic_set(&hist->buf_err, 1);
	}
	isp_reg_clr(hist->isp, OMAP3_ISP_IOMEM_HIST, ISPHIST_CNT,
		    ISPHIST_CNT_CLEAR);

	omap3isp_stat_dma_isr(hist);
	if (hist->state != ISPSTAT_DISABLED)
		omap3isp_hist_dma_done(hist->isp);
}

static int hist_buf_dma(struct ispstat *hist)
{
	dma_addr_t dma_addr = hist->active_buf->dma_addr;

	if (unlikely(!dma_addr)) {
		dev_dbg(hist->isp->dev, "hist: invalid DMA buffer address\n");
		hist_reset_mem(hist);
		return STAT_NO_BUF;
	}

	isp_reg_writel(hist->isp, 0, OMAP3_ISP_IOMEM_HIST, ISPHIST_ADDR);
	isp_reg_set(hist->isp, OMAP3_ISP_IOMEM_HIST, ISPHIST_CNT,
		    ISPHIST_CNT_CLEAR);
	omap3isp_flush(hist->isp);
	hist->dma_config.dst_start = dma_addr;
	hist->dma_config.elem_count = hist->buf_size / sizeof(u32);
	omap_set_dma_params(hist->dma_ch, &hist->dma_config);

	omap_start_dma(hist->dma_ch);

	return STAT_BUF_WAITING_DMA;
}

static int hist_buf_pio(struct ispstat *hist)
{
	struct isp_device *isp = hist->isp;
	u32 *buf = hist->active_buf->virt_addr;
	unsigned int i;

	if (!buf) {
		dev_dbg(isp->dev, "hist: invalid PIO buffer address\n");
		hist_reset_mem(hist);
		return STAT_NO_BUF;
	}

	isp_reg_writel(isp, 0, OMAP3_ISP_IOMEM_HIST, ISPHIST_ADDR);

	/*
	 * By setting it, the histogram internal buffer is being cleared at the
	 * same time it's being read. This bit must be cleared just after all
	 * data is acquired.
	 */
	isp_reg_set(isp, OMAP3_ISP_IOMEM_HIST, ISPHIST_CNT, ISPHIST_CNT_CLEAR);

	/*
	 * We'll read 4 times a 4-bytes-word at each iteration for
	 * optimization. It avoids 3/4 of the jumps. We also know buf_size is
	 * divisible by 16.
	 */
	for (i = hist->buf_size / 16; i > 0; i--) {
		*buf++ = isp_reg_readl(isp, OMAP3_ISP_IOMEM_HIST, ISPHIST_DATA);
		*buf++ = isp_reg_readl(isp, OMAP3_ISP_IOMEM_HIST, ISPHIST_DATA);
		*buf++ = isp_reg_readl(isp, OMAP3_ISP_IOMEM_HIST, ISPHIST_DATA);
		*buf++ = isp_reg_readl(isp, OMAP3_ISP_IOMEM_HIST, ISPHIST_DATA);
	}
	isp_reg_clr(hist->isp, OMAP3_ISP_IOMEM_HIST, ISPHIST_CNT,
		    ISPHIST_CNT_CLEAR);

	return STAT_BUF_DONE;
}

/*
 * hist_buf_process - Callback from ISP driver for HIST interrupt.
 */
static int hist_buf_process(struct ispstat *hist)
{
	struct omap3isp_hist_config *user_cfg = hist->priv;
	int ret;

	if (atomic_read(&hist->buf_err) || hist->state != ISPSTAT_ENABLED) {
		hist_reset_mem(hist);
		return STAT_NO_BUF;
	}

	if (--(hist->wait_acc_frames))
		return STAT_NO_BUF;

	if (HIST_USING_DMA(hist))
		ret = hist_buf_dma(hist);
	else
		ret = hist_buf_pio(hist);

	hist->wait_acc_frames = user_cfg->num_acc_frames;

	return ret;
}

static u32 hist_get_buf_size(struct omap3isp_hist_config *conf)
{
	return OMAP3ISP_HIST_MEM_SIZE_BINS(conf->hist_bins) * conf->num_regions;
}

/*
 * hist_validate_params - Helper function to check user given params.
 * @user_cfg: Pointer to user configuration structure.
 *
 * Returns 0 on success configuration.
 */
static int hist_validate_params(struct ispstat *hist, void *new_conf)
{
	struct omap3isp_hist_config *user_cfg = new_conf;
	int c;
	u32 buf_size;

	if (user_cfg->cfa > OMAP3ISP_HIST_CFA_FOVEONX3)
		return -EINVAL;

	/* Regions size and position */

	if ((user_cfg->num_regions < OMAP3ISP_HIST_MIN_REGIONS) ||
	    (user_cfg->num_regions > OMAP3ISP_HIST_MAX_REGIONS))
		return -EINVAL;

	/* Regions */
	for (c = 0; c < user_cfg->num_regions; c++) {
		if (user_cfg->region[c].h_start & ~ISPHIST_REG_START_END_MASK)
			return -EINVAL;
		if (user_cfg->region[c].h_end & ~ISPHIST_REG_START_END_MASK)
			return -EINVAL;
		if (user_cfg->region[c].v_start & ~ISPHIST_REG_START_END_MASK)
			return -EINVAL;
		if (user_cfg->region[c].v_end & ~ISPHIST_REG_START_END_MASK)
			return -EINVAL;
		if (user_cfg->region[c].h_start > user_cfg->region[c].h_end)
			return -EINVAL;
		if (user_cfg->region[c].v_start > user_cfg->region[c].v_end)
			return -EINVAL;
	}

	switch (user_cfg->num_regions) {
	case 1:
		if (user_cfg->hist_bins > OMAP3ISP_HIST_BINS_256)
			return -EINVAL;
		break;
	case 2:
		if (user_cfg->hist_bins > OMAP3ISP_HIST_BINS_128)
			return -EINVAL;
		break;
	default: /* 3 or 4 */
		if (user_cfg->hist_bins > OMAP3ISP_HIST_BINS_64)
			return -EINVAL;
		break;
	}

	buf_size = hist_get_buf_size(user_cfg);
	if (buf_size > user_cfg->buf_size)
		/* User's buf_size request wasn't enoght */
		user_cfg->buf_size = buf_size;
	else if (user_cfg->buf_size > OMAP3ISP_HIST_MAX_BUF_SIZE)
		user_cfg->buf_size = OMAP3ISP_HIST_MAX_BUF_SIZE;

	return 0;
}

static int hist_comp_params(struct ispstat *hist,
			    struct omap3isp_hist_config *user_cfg)
{
	struct omap3isp_hist_config *cur_cfg = hist->priv;
	int c;

	if (cur_cfg->cfa != user_cfg->cfa)
		return 1;

	if (cur_cfg->num_acc_frames != user_cfg->num_acc_frames)
		return 1;

	if (cur_cfg->hist_bins != user_cfg->hist_bins)
		return 1;

	for (c = 0; c < OMAP3ISP_HIST_MAX_WG; c++) {
		if (c == 3 && user_cfg->cfa == OMAP3ISP_HIST_CFA_FOVEONX3)
			break;
		else if (cur_cfg->wg[c] != user_cfg->wg[c])
			return 1;
	}

	if (cur_cfg->num_regions != user_cfg->num_regions)
		return 1;

	/* Regions */
	for (c = 0; c < user_cfg->num_regions; c++) {
		if (cur_cfg->region[c].h_start != user_cfg->region[c].h_start)
			return 1;
		if (cur_cfg->region[c].h_end != user_cfg->region[c].h_end)
			return 1;
		if (cur_cfg->region[c].v_start != user_cfg->region[c].v_start)
			return 1;
		if (cur_cfg->region[c].v_end != user_cfg->region[c].v_end)
			return 1;
	}

	return 0;
}

/*
 * hist_update_params - Helper function to check and store user given params.
 * @new_conf: Pointer to user configuration structure.
 */
static void hist_set_params(struct ispstat *hist, void *new_conf)
{
	struct omap3isp_hist_config *user_cfg = new_conf;
	struct omap3isp_hist_config *cur_cfg = hist->priv;

	if (!hist->configured || hist_comp_params(hist, user_cfg)) {
		memcpy(cur_cfg, user_cfg, sizeof(*user_cfg));
		if (user_cfg->num_acc_frames == 0)
			user_cfg->num_acc_frames = 1;
		hist->inc_config++;
		hist->update = 1;
		/*
		 * User might be asked for a bigger buffer than necessary for
		 * this configuration. In order to return the right amount of
		 * data during buffer request, let's calculate the size here
		 * instead of stick with user_cfg->buf_size.
		 */
		cur_cfg->buf_size = hist_get_buf_size(cur_cfg);

	}
}

static long hist_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct ispstat *stat = v4l2_get_subdevdata(sd);

	switch (cmd) {
	case VIDIOC_OMAP3ISP_HIST_CFG:
		return omap3isp_stat_config(stat, arg);
	case VIDIOC_OMAP3ISP_STAT_REQ:
		return omap3isp_stat_request_statistics(stat, arg);
	case VIDIOC_OMAP3ISP_STAT_EN: {
		int *en = arg;
		return omap3isp_stat_enable(stat, !!*en);
	}
	}

	return -ENOIOCTLCMD;

}

static const struct ispstat_ops hist_ops = {
	.validate_params	= hist_validate_params,
	.set_params		= hist_set_params,
	.setup_regs		= hist_setup_regs,
	.enable			= hist_enable,
	.busy			= hist_busy,
	.buf_process		= hist_buf_process,
};

static const struct v4l2_subdev_core_ops hist_subdev_core_ops = {
	.ioctl = hist_ioctl,
	.subscribe_event = omap3isp_stat_subscribe_event,
	.unsubscribe_event = omap3isp_stat_unsubscribe_event,
};

static const struct v4l2_subdev_video_ops hist_subdev_video_ops = {
	.s_stream = omap3isp_stat_s_stream,
};

static const struct v4l2_subdev_ops hist_subdev_ops = {
	.core = &hist_subdev_core_ops,
	.video = &hist_subdev_video_ops,
};

/*
 * omap3isp_hist_init - Module Initialization.
 */
int omap3isp_hist_init(struct isp_device *isp)
{
	struct ispstat *hist = &isp->isp_hist;
	struct omap3isp_hist_config *hist_cfg;
	int ret = -1;

	hist_cfg = kzalloc(sizeof(*hist_cfg), GFP_KERNEL);
	if (hist_cfg == NULL)
		return -ENOMEM;

	memset(hist, 0, sizeof(*hist));
	hist->isp = isp;

	if (HIST_CONFIG_DMA)
		ret = omap_request_dma(OMAP24XX_DMA_NO_DEVICE, "DMA_ISP_HIST",
				       hist_dma_cb, hist, &hist->dma_ch);
	if (ret) {
		if (HIST_CONFIG_DMA)
			dev_warn(isp->dev, "hist: DMA request channel failed. "
					   "Using PIO only.\n");
		hist->dma_ch = -1;
	} else {
		dev_dbg(isp->dev, "hist: DMA channel = %d\n", hist->dma_ch);
		hist_dma_config(hist);
		omap_enable_dma_irq(hist->dma_ch, OMAP_DMA_BLOCK_IRQ);
	}

	hist->ops = &hist_ops;
	hist->priv = hist_cfg;
	hist->event_type = V4L2_EVENT_OMAP3ISP_HIST;

	ret = omap3isp_stat_init(hist, "histogram", &hist_subdev_ops);
	if (ret) {
		kfree(hist_cfg);
		if (HIST_USING_DMA(hist))
			omap_free_dma(hist->dma_ch);
	}

	return ret;
}

/*
 * omap3isp_hist_cleanup - Module cleanup.
 */
void omap3isp_hist_cleanup(struct isp_device *isp)
{
	if (HIST_USING_DMA(&isp->isp_hist))
		omap_free_dma(isp->isp_hist.dma_ch);
	kfree(isp->isp_hist.priv);
	omap3isp_stat_cleanup(&isp->isp_hist);
}
