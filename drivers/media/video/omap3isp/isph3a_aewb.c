/*
 * isph3a.c
 *
 * TI OMAP3 ISP - H3A module
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

#include <linux/slab.h>
#include <linux/uaccess.h>

#include "isp.h"
#include "isph3a.h"
#include "ispstat.h"

/*
 * h3a_aewb_update_regs - Helper function to update h3a registers.
 */
static void h3a_aewb_setup_regs(struct ispstat *aewb, void *priv)
{
	struct omap3isp_h3a_aewb_config *conf = priv;
	u32 pcr;
	u32 win1;
	u32 start;
	u32 blk;
	u32 subwin;

	if (aewb->state == ISPSTAT_DISABLED)
		return;

	isp_reg_writel(aewb->isp, aewb->active_buf->iommu_addr,
		       OMAP3_ISP_IOMEM_H3A, ISPH3A_AEWBUFST);

	if (!aewb->update)
		return;

	/* Converting config metadata into reg values */
	pcr = conf->saturation_limit << ISPH3A_PCR_AEW_AVE2LMT_SHIFT;
	pcr |= !!conf->alaw_enable << ISPH3A_PCR_AEW_ALAW_EN_SHIFT;

	win1 = ((conf->win_height >> 1) - 1) << ISPH3A_AEWWIN1_WINH_SHIFT;
	win1 |= ((conf->win_width >> 1) - 1) << ISPH3A_AEWWIN1_WINW_SHIFT;
	win1 |= (conf->ver_win_count - 1) << ISPH3A_AEWWIN1_WINVC_SHIFT;
	win1 |= (conf->hor_win_count - 1) << ISPH3A_AEWWIN1_WINHC_SHIFT;

	start = conf->hor_win_start << ISPH3A_AEWINSTART_WINSH_SHIFT;
	start |= conf->ver_win_start << ISPH3A_AEWINSTART_WINSV_SHIFT;

	blk = conf->blk_ver_win_start << ISPH3A_AEWINBLK_WINSV_SHIFT;
	blk |= ((conf->blk_win_height >> 1) - 1) << ISPH3A_AEWINBLK_WINH_SHIFT;

	subwin = ((conf->subsample_ver_inc >> 1) - 1) <<
		 ISPH3A_AEWSUBWIN_AEWINCV_SHIFT;
	subwin |= ((conf->subsample_hor_inc >> 1) - 1) <<
		  ISPH3A_AEWSUBWIN_AEWINCH_SHIFT;

	isp_reg_writel(aewb->isp, win1, OMAP3_ISP_IOMEM_H3A, ISPH3A_AEWWIN1);
	isp_reg_writel(aewb->isp, start, OMAP3_ISP_IOMEM_H3A,
		       ISPH3A_AEWINSTART);
	isp_reg_writel(aewb->isp, blk, OMAP3_ISP_IOMEM_H3A, ISPH3A_AEWINBLK);
	isp_reg_writel(aewb->isp, subwin, OMAP3_ISP_IOMEM_H3A,
		       ISPH3A_AEWSUBWIN);
	isp_reg_clr_set(aewb->isp, OMAP3_ISP_IOMEM_H3A, ISPH3A_PCR,
			ISPH3A_PCR_AEW_MASK, pcr);

	aewb->update = 0;
	aewb->config_counter += aewb->inc_config;
	aewb->inc_config = 0;
	aewb->buf_size = conf->buf_size;
}

static void h3a_aewb_enable(struct ispstat *aewb, int enable)
{
	if (enable) {
		isp_reg_set(aewb->isp, OMAP3_ISP_IOMEM_H3A, ISPH3A_PCR,
			    ISPH3A_PCR_AEW_EN);
		/* This bit is already set if AF is enabled */
		if (aewb->isp->isp_af.state != ISPSTAT_ENABLED)
			isp_reg_set(aewb->isp, OMAP3_ISP_IOMEM_MAIN, ISP_CTRL,
				    ISPCTRL_H3A_CLK_EN);
	} else {
		isp_reg_clr(aewb->isp, OMAP3_ISP_IOMEM_H3A, ISPH3A_PCR,
			    ISPH3A_PCR_AEW_EN);
		/* This bit can't be cleared if AF is enabled */
		if (aewb->isp->isp_af.state != ISPSTAT_ENABLED)
			isp_reg_clr(aewb->isp, OMAP3_ISP_IOMEM_MAIN, ISP_CTRL,
				    ISPCTRL_H3A_CLK_EN);
	}
}

static int h3a_aewb_busy(struct ispstat *aewb)
{
	return isp_reg_readl(aewb->isp, OMAP3_ISP_IOMEM_H3A, ISPH3A_PCR)
						& ISPH3A_PCR_BUSYAEAWB;
}

static u32 h3a_aewb_get_buf_size(struct omap3isp_h3a_aewb_config *conf)
{
	/* Number of configured windows + extra row for black data */
	u32 win_count = (conf->ver_win_count + 1) * conf->hor_win_count;

	/*
	 * Unsaturated block counts for each 8 windows.
	 * 1 extra for the last (win_count % 8) windows if win_count is not
	 * divisible by 8.
	 */
	win_count += (win_count + 7) / 8;

	return win_count * AEWB_PACKET_SIZE;
}

static int h3a_aewb_validate_params(struct ispstat *aewb, void *new_conf)
{
	struct omap3isp_h3a_aewb_config *user_cfg = new_conf;
	u32 buf_size;

	if (unlikely(user_cfg->saturation_limit >
		     OMAP3ISP_AEWB_MAX_SATURATION_LIM))
		return -EINVAL;

	if (unlikely(user_cfg->win_height < OMAP3ISP_AEWB_MIN_WIN_H ||
		     user_cfg->win_height > OMAP3ISP_AEWB_MAX_WIN_H ||
		     user_cfg->win_height & 0x01))
		return -EINVAL;

	if (unlikely(user_cfg->win_width < OMAP3ISP_AEWB_MIN_WIN_W ||
		     user_cfg->win_width > OMAP3ISP_AEWB_MAX_WIN_W ||
		     user_cfg->win_width & 0x01))
		return -EINVAL;

	if (unlikely(user_cfg->ver_win_count < OMAP3ISP_AEWB_MIN_WINVC ||
		     user_cfg->ver_win_count > OMAP3ISP_AEWB_MAX_WINVC))
		return -EINVAL;

	if (unlikely(user_cfg->hor_win_count < OMAP3ISP_AEWB_MIN_WINHC ||
		     user_cfg->hor_win_count > OMAP3ISP_AEWB_MAX_WINHC))
		return -EINVAL;

	if (unlikely(user_cfg->ver_win_start > OMAP3ISP_AEWB_MAX_WINSTART))
		return -EINVAL;

	if (unlikely(user_cfg->hor_win_start > OMAP3ISP_AEWB_MAX_WINSTART))
		return -EINVAL;

	if (unlikely(user_cfg->blk_ver_win_start > OMAP3ISP_AEWB_MAX_WINSTART))
		return -EINVAL;

	if (unlikely(user_cfg->blk_win_height < OMAP3ISP_AEWB_MIN_WIN_H ||
		     user_cfg->blk_win_height > OMAP3ISP_AEWB_MAX_WIN_H ||
		     user_cfg->blk_win_height & 0x01))
		return -EINVAL;

	if (unlikely(user_cfg->subsample_ver_inc < OMAP3ISP_AEWB_MIN_SUB_INC ||
		     user_cfg->subsample_ver_inc > OMAP3ISP_AEWB_MAX_SUB_INC ||
		     user_cfg->subsample_ver_inc & 0x01))
		return -EINVAL;

	if (unlikely(user_cfg->subsample_hor_inc < OMAP3ISP_AEWB_MIN_SUB_INC ||
		     user_cfg->subsample_hor_inc > OMAP3ISP_AEWB_MAX_SUB_INC ||
		     user_cfg->subsample_hor_inc & 0x01))
		return -EINVAL;

	buf_size = h3a_aewb_get_buf_size(user_cfg);
	if (buf_size > user_cfg->buf_size)
		user_cfg->buf_size = buf_size;
	else if (user_cfg->buf_size > OMAP3ISP_AEWB_MAX_BUF_SIZE)
		user_cfg->buf_size = OMAP3ISP_AEWB_MAX_BUF_SIZE;

	return 0;
}

/*
 * h3a_aewb_set_params - Helper function to check & store user given params.
 * @new_conf: Pointer to AE and AWB parameters struct.
 *
 * As most of them are busy-lock registers, need to wait until AEW_BUSY = 0 to
 * program them during ISR.
 */
static void h3a_aewb_set_params(struct ispstat *aewb, void *new_conf)
{
	struct omap3isp_h3a_aewb_config *user_cfg = new_conf;
	struct omap3isp_h3a_aewb_config *cur_cfg = aewb->priv;
	int update = 0;

	if (cur_cfg->saturation_limit != user_cfg->saturation_limit) {
		cur_cfg->saturation_limit = user_cfg->saturation_limit;
		update = 1;
	}
	if (cur_cfg->alaw_enable != user_cfg->alaw_enable) {
		cur_cfg->alaw_enable = user_cfg->alaw_enable;
		update = 1;
	}
	if (cur_cfg->win_height != user_cfg->win_height) {
		cur_cfg->win_height = user_cfg->win_height;
		update = 1;
	}
	if (cur_cfg->win_width != user_cfg->win_width) {
		cur_cfg->win_width = user_cfg->win_width;
		update = 1;
	}
	if (cur_cfg->ver_win_count != user_cfg->ver_win_count) {
		cur_cfg->ver_win_count = user_cfg->ver_win_count;
		update = 1;
	}
	if (cur_cfg->hor_win_count != user_cfg->hor_win_count) {
		cur_cfg->hor_win_count = user_cfg->hor_win_count;
		update = 1;
	}
	if (cur_cfg->ver_win_start != user_cfg->ver_win_start) {
		cur_cfg->ver_win_start = user_cfg->ver_win_start;
		update = 1;
	}
	if (cur_cfg->hor_win_start != user_cfg->hor_win_start) {
		cur_cfg->hor_win_start = user_cfg->hor_win_start;
		update = 1;
	}
	if (cur_cfg->blk_ver_win_start != user_cfg->blk_ver_win_start) {
		cur_cfg->blk_ver_win_start = user_cfg->blk_ver_win_start;
		update = 1;
	}
	if (cur_cfg->blk_win_height != user_cfg->blk_win_height) {
		cur_cfg->blk_win_height = user_cfg->blk_win_height;
		update = 1;
	}
	if (cur_cfg->subsample_ver_inc != user_cfg->subsample_ver_inc) {
		cur_cfg->subsample_ver_inc = user_cfg->subsample_ver_inc;
		update = 1;
	}
	if (cur_cfg->subsample_hor_inc != user_cfg->subsample_hor_inc) {
		cur_cfg->subsample_hor_inc = user_cfg->subsample_hor_inc;
		update = 1;
	}

	if (update || !aewb->configured) {
		aewb->inc_config++;
		aewb->update = 1;
		cur_cfg->buf_size = h3a_aewb_get_buf_size(cur_cfg);
	}
}

static long h3a_aewb_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct ispstat *stat = v4l2_get_subdevdata(sd);

	switch (cmd) {
	case VIDIOC_OMAP3ISP_AEWB_CFG:
		return omap3isp_stat_config(stat, arg);
	case VIDIOC_OMAP3ISP_STAT_REQ:
		return omap3isp_stat_request_statistics(stat, arg);
	case VIDIOC_OMAP3ISP_STAT_EN: {
		unsigned long *en = arg;
		return omap3isp_stat_enable(stat, !!*en);
	}
	}

	return -ENOIOCTLCMD;
}

static const struct ispstat_ops h3a_aewb_ops = {
	.validate_params	= h3a_aewb_validate_params,
	.set_params		= h3a_aewb_set_params,
	.setup_regs		= h3a_aewb_setup_regs,
	.enable			= h3a_aewb_enable,
	.busy			= h3a_aewb_busy,
};

static const struct v4l2_subdev_core_ops h3a_aewb_subdev_core_ops = {
	.ioctl = h3a_aewb_ioctl,
	.subscribe_event = omap3isp_stat_subscribe_event,
	.unsubscribe_event = omap3isp_stat_unsubscribe_event,
};

static const struct v4l2_subdev_video_ops h3a_aewb_subdev_video_ops = {
	.s_stream = omap3isp_stat_s_stream,
};

static const struct v4l2_subdev_ops h3a_aewb_subdev_ops = {
	.core = &h3a_aewb_subdev_core_ops,
	.video = &h3a_aewb_subdev_video_ops,
};

/*
 * omap3isp_h3a_aewb_init - Module Initialisation.
 */
int omap3isp_h3a_aewb_init(struct isp_device *isp)
{
	struct ispstat *aewb = &isp->isp_aewb;
	struct omap3isp_h3a_aewb_config *aewb_cfg;
	struct omap3isp_h3a_aewb_config *aewb_recover_cfg;
	int ret;

	aewb_cfg = kzalloc(sizeof(*aewb_cfg), GFP_KERNEL);
	if (!aewb_cfg)
		return -ENOMEM;

	memset(aewb, 0, sizeof(*aewb));
	aewb->ops = &h3a_aewb_ops;
	aewb->priv = aewb_cfg;
	aewb->dma_ch = -1;
	aewb->event_type = V4L2_EVENT_OMAP3ISP_AEWB;
	aewb->isp = isp;

	/* Set recover state configuration */
	aewb_recover_cfg = kzalloc(sizeof(*aewb_recover_cfg), GFP_KERNEL);
	if (!aewb_recover_cfg) {
		dev_err(aewb->isp->dev, "AEWB: cannot allocate memory for "
					"recover configuration.\n");
		ret = -ENOMEM;
		goto err_recover_alloc;
	}

	aewb_recover_cfg->saturation_limit = OMAP3ISP_AEWB_MAX_SATURATION_LIM;
	aewb_recover_cfg->win_height = OMAP3ISP_AEWB_MIN_WIN_H;
	aewb_recover_cfg->win_width = OMAP3ISP_AEWB_MIN_WIN_W;
	aewb_recover_cfg->ver_win_count = OMAP3ISP_AEWB_MIN_WINVC;
	aewb_recover_cfg->hor_win_count = OMAP3ISP_AEWB_MIN_WINHC;
	aewb_recover_cfg->blk_ver_win_start = aewb_recover_cfg->ver_win_start +
		aewb_recover_cfg->win_height * aewb_recover_cfg->ver_win_count;
	aewb_recover_cfg->blk_win_height = OMAP3ISP_AEWB_MIN_WIN_H;
	aewb_recover_cfg->subsample_ver_inc = OMAP3ISP_AEWB_MIN_SUB_INC;
	aewb_recover_cfg->subsample_hor_inc = OMAP3ISP_AEWB_MIN_SUB_INC;

	if (h3a_aewb_validate_params(aewb, aewb_recover_cfg)) {
		dev_err(aewb->isp->dev, "AEWB: recover configuration is "
					"invalid.\n");
		ret = -EINVAL;
		goto err_conf;
	}

	aewb_recover_cfg->buf_size = h3a_aewb_get_buf_size(aewb_recover_cfg);
	aewb->recover_priv = aewb_recover_cfg;

	ret = omap3isp_stat_init(aewb, "AEWB", &h3a_aewb_subdev_ops);
	if (ret)
		goto err_conf;

	return 0;

err_conf:
	kfree(aewb_recover_cfg);
err_recover_alloc:
	kfree(aewb_cfg);

	return ret;
}

/*
 * omap3isp_h3a_aewb_cleanup - Module exit.
 */
void omap3isp_h3a_aewb_cleanup(struct isp_device *isp)
{
	kfree(isp->isp_aewb.priv);
	kfree(isp->isp_aewb.recover_priv);
	omap3isp_stat_free(&isp->isp_aewb);
}
