/*
 * isph3a_af.c
 *
 * TI OMAP3 ISP - H3A AF module
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

/* Linux specific include files */
#include <linux/device.h>
#include <linux/slab.h>

#include "isp.h"
#include "isph3a.h"
#include "ispstat.h"

#define IS_OUT_OF_BOUNDS(value, min, max)		\
	(((value) < (min)) || ((value) > (max)))

static void h3a_af_setup_regs(struct ispstat *af, void *priv)
{
	struct omap3isp_h3a_af_config *conf = priv;
	u32 pcr;
	u32 pax1;
	u32 pax2;
	u32 paxstart;
	u32 coef;
	u32 base_coef_set0;
	u32 base_coef_set1;
	int index;

	if (af->state == ISPSTAT_DISABLED)
		return;

	isp_reg_writel(af->isp, af->active_buf->iommu_addr, OMAP3_ISP_IOMEM_H3A,
		       ISPH3A_AFBUFST);

	if (!af->update)
		return;

	/* Configure Hardware Registers */
	pax1 = ((conf->paxel.width >> 1) - 1) << AF_PAXW_SHIFT;
	/* Set height in AFPAX1 */
	pax1 |= (conf->paxel.height >> 1) - 1;
	isp_reg_writel(af->isp, pax1, OMAP3_ISP_IOMEM_H3A, ISPH3A_AFPAX1);

	/* Configure AFPAX2 Register */
	/* Set Line Increment in AFPAX2 Register */
	pax2 = ((conf->paxel.line_inc >> 1) - 1) << AF_LINE_INCR_SHIFT;
	/* Set Vertical Count */
	pax2 |= (conf->paxel.v_cnt - 1) << AF_VT_COUNT_SHIFT;
	/* Set Horizontal Count */
	pax2 |= (conf->paxel.h_cnt - 1);
	isp_reg_writel(af->isp, pax2, OMAP3_ISP_IOMEM_H3A, ISPH3A_AFPAX2);

	/* Configure PAXSTART Register */
	/*Configure Horizontal Start */
	paxstart = conf->paxel.h_start << AF_HZ_START_SHIFT;
	/* Configure Vertical Start */
	paxstart |= conf->paxel.v_start;
	isp_reg_writel(af->isp, paxstart, OMAP3_ISP_IOMEM_H3A,
		       ISPH3A_AFPAXSTART);

	/*SetIIRSH Register */
	isp_reg_writel(af->isp, conf->iir.h_start,
		       OMAP3_ISP_IOMEM_H3A, ISPH3A_AFIIRSH);

	base_coef_set0 = ISPH3A_AFCOEF010;
	base_coef_set1 = ISPH3A_AFCOEF110;
	for (index = 0; index <= 8; index += 2) {
		/*Set IIR Filter0 Coefficients */
		coef = 0;
		coef |= conf->iir.coeff_set0[index];
		coef |= conf->iir.coeff_set0[index + 1] <<
			AF_COEF_SHIFT;
		isp_reg_writel(af->isp, coef, OMAP3_ISP_IOMEM_H3A,
			       base_coef_set0);
		base_coef_set0 += AFCOEF_OFFSET;

		/*Set IIR Filter1 Coefficients */
		coef = 0;
		coef |= conf->iir.coeff_set1[index];
		coef |= conf->iir.coeff_set1[index + 1] <<
			AF_COEF_SHIFT;
		isp_reg_writel(af->isp, coef, OMAP3_ISP_IOMEM_H3A,
			       base_coef_set1);
		base_coef_set1 += AFCOEF_OFFSET;
	}
	/* set AFCOEF0010 Register */
	isp_reg_writel(af->isp, conf->iir.coeff_set0[10],
		       OMAP3_ISP_IOMEM_H3A, ISPH3A_AFCOEF0010);
	/* set AFCOEF1010 Register */
	isp_reg_writel(af->isp, conf->iir.coeff_set1[10],
		       OMAP3_ISP_IOMEM_H3A, ISPH3A_AFCOEF1010);

	/* PCR Register */
	/* Set RGB Position */
	pcr = conf->rgb_pos << AF_RGBPOS_SHIFT;
	/* Set Accumulator Mode */
	if (conf->fvmode == OMAP3ISP_AF_MODE_PEAK)
		pcr |= AF_FVMODE;
	/* Set A-law */
	if (conf->alaw_enable)
		pcr |= AF_ALAW_EN;
	/* HMF Configurations */
	if (conf->hmf.enable) {
		/* Enable HMF */
		pcr |= AF_MED_EN;
		/* Set Median Threshold */
		pcr |= conf->hmf.threshold << AF_MED_TH_SHIFT;
	}
	/* Set PCR Register */
	isp_reg_clr_set(af->isp, OMAP3_ISP_IOMEM_H3A, ISPH3A_PCR,
			AF_PCR_MASK, pcr);

	af->update = 0;
	af->config_counter += af->inc_config;
	af->inc_config = 0;
	af->buf_size = conf->buf_size;
}

static void h3a_af_enable(struct ispstat *af, int enable)
{
	if (enable) {
		isp_reg_set(af->isp, OMAP3_ISP_IOMEM_H3A, ISPH3A_PCR,
			    ISPH3A_PCR_AF_EN);
		omap3isp_subclk_enable(af->isp, OMAP3_ISP_SUBCLK_AF);
	} else {
		isp_reg_clr(af->isp, OMAP3_ISP_IOMEM_H3A, ISPH3A_PCR,
			    ISPH3A_PCR_AF_EN);
		omap3isp_subclk_disable(af->isp, OMAP3_ISP_SUBCLK_AF);
	}
}

static int h3a_af_busy(struct ispstat *af)
{
	return isp_reg_readl(af->isp, OMAP3_ISP_IOMEM_H3A, ISPH3A_PCR)
						& ISPH3A_PCR_BUSYAF;
}

static u32 h3a_af_get_buf_size(struct omap3isp_h3a_af_config *conf)
{
	return conf->paxel.h_cnt * conf->paxel.v_cnt * OMAP3ISP_AF_PAXEL_SIZE;
}

/* Function to check paxel parameters */
static int h3a_af_validate_params(struct ispstat *af, void *new_conf)
{
	struct omap3isp_h3a_af_config *user_cfg = new_conf;
	struct omap3isp_h3a_af_paxel *paxel_cfg = &user_cfg->paxel;
	struct omap3isp_h3a_af_iir *iir_cfg = &user_cfg->iir;
	int index;
	u32 buf_size;

	/* Check horizontal Count */
	if (IS_OUT_OF_BOUNDS(paxel_cfg->h_cnt,
			     OMAP3ISP_AF_PAXEL_HORIZONTAL_COUNT_MIN,
			     OMAP3ISP_AF_PAXEL_HORIZONTAL_COUNT_MAX))
		return -EINVAL;

	/* Check Vertical Count */
	if (IS_OUT_OF_BOUNDS(paxel_cfg->v_cnt,
			     OMAP3ISP_AF_PAXEL_VERTICAL_COUNT_MIN,
			     OMAP3ISP_AF_PAXEL_VERTICAL_COUNT_MAX))
		return -EINVAL;

	if (IS_OUT_OF_BOUNDS(paxel_cfg->height, OMAP3ISP_AF_PAXEL_HEIGHT_MIN,
			     OMAP3ISP_AF_PAXEL_HEIGHT_MAX) ||
	    paxel_cfg->height % 2)
		return -EINVAL;

	/* Check width */
	if (IS_OUT_OF_BOUNDS(paxel_cfg->width, OMAP3ISP_AF_PAXEL_WIDTH_MIN,
			     OMAP3ISP_AF_PAXEL_WIDTH_MAX) ||
	    paxel_cfg->width % 2)
		return -EINVAL;

	/* Check Line Increment */
	if (IS_OUT_OF_BOUNDS(paxel_cfg->line_inc,
			     OMAP3ISP_AF_PAXEL_INCREMENT_MIN,
			     OMAP3ISP_AF_PAXEL_INCREMENT_MAX) ||
	    paxel_cfg->line_inc % 2)
		return -EINVAL;

	/* Check Horizontal Start */
	if ((paxel_cfg->h_start < iir_cfg->h_start) ||
	    IS_OUT_OF_BOUNDS(paxel_cfg->h_start,
			     OMAP3ISP_AF_PAXEL_HZSTART_MIN,
			     OMAP3ISP_AF_PAXEL_HZSTART_MAX))
		return -EINVAL;

	/* Check IIR */
	for (index = 0; index < OMAP3ISP_AF_NUM_COEF; index++) {
		if ((iir_cfg->coeff_set0[index]) > OMAP3ISP_AF_COEF_MAX)
			return -EINVAL;

		if ((iir_cfg->coeff_set1[index]) > OMAP3ISP_AF_COEF_MAX)
			return -EINVAL;
	}

	if (IS_OUT_OF_BOUNDS(iir_cfg->h_start, OMAP3ISP_AF_IIRSH_MIN,
			     OMAP3ISP_AF_IIRSH_MAX))
		return -EINVAL;

	/* Hack: If paxel size is 12, the 10th AF window may be corrupted */
	if ((paxel_cfg->h_cnt * paxel_cfg->v_cnt > 9) &&
	    (paxel_cfg->width * paxel_cfg->height == 12))
		return -EINVAL;

	buf_size = h3a_af_get_buf_size(user_cfg);
	if (buf_size > user_cfg->buf_size)
		/* User buf_size request wasn't enough */
		user_cfg->buf_size = buf_size;
	else if (user_cfg->buf_size > OMAP3ISP_AF_MAX_BUF_SIZE)
		user_cfg->buf_size = OMAP3ISP_AF_MAX_BUF_SIZE;

	return 0;
}

/* Update local parameters */
static void h3a_af_set_params(struct ispstat *af, void *new_conf)
{
	struct omap3isp_h3a_af_config *user_cfg = new_conf;
	struct omap3isp_h3a_af_config *cur_cfg = af->priv;
	int update = 0;
	int index;

	/* alaw */
	if (cur_cfg->alaw_enable != user_cfg->alaw_enable) {
		update = 1;
		goto out;
	}

	/* hmf */
	if (cur_cfg->hmf.enable != user_cfg->hmf.enable) {
		update = 1;
		goto out;
	}
	if (cur_cfg->hmf.threshold != user_cfg->hmf.threshold) {
		update = 1;
		goto out;
	}

	/* rgbpos */
	if (cur_cfg->rgb_pos != user_cfg->rgb_pos) {
		update = 1;
		goto out;
	}

	/* iir */
	if (cur_cfg->iir.h_start != user_cfg->iir.h_start) {
		update = 1;
		goto out;
	}
	for (index = 0; index < OMAP3ISP_AF_NUM_COEF; index++) {
		if (cur_cfg->iir.coeff_set0[index] !=
				user_cfg->iir.coeff_set0[index]) {
			update = 1;
			goto out;
		}
		if (cur_cfg->iir.coeff_set1[index] !=
				user_cfg->iir.coeff_set1[index]) {
			update = 1;
			goto out;
		}
	}

	/* paxel */
	if ((cur_cfg->paxel.width != user_cfg->paxel.width) ||
	    (cur_cfg->paxel.height != user_cfg->paxel.height) ||
	    (cur_cfg->paxel.h_start != user_cfg->paxel.h_start) ||
	    (cur_cfg->paxel.v_start != user_cfg->paxel.v_start) ||
	    (cur_cfg->paxel.h_cnt != user_cfg->paxel.h_cnt) ||
	    (cur_cfg->paxel.v_cnt != user_cfg->paxel.v_cnt) ||
	    (cur_cfg->paxel.line_inc != user_cfg->paxel.line_inc)) {
		update = 1;
		goto out;
	}

	/* af_mode */
	if (cur_cfg->fvmode != user_cfg->fvmode)
		update = 1;

out:
	if (update || !af->configured) {
		memcpy(cur_cfg, user_cfg, sizeof(*cur_cfg));
		af->inc_config++;
		af->update = 1;
		/*
		 * User might be asked for a bigger buffer than necessary for
		 * this configuration. In order to return the right amount of
		 * data during buffer request, let's calculate the size here
		 * instead of stick with user_cfg->buf_size.
		 */
		cur_cfg->buf_size = h3a_af_get_buf_size(cur_cfg);
	}
}

static long h3a_af_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct ispstat *stat = v4l2_get_subdevdata(sd);

	switch (cmd) {
	case VIDIOC_OMAP3ISP_AF_CFG:
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

static const struct ispstat_ops h3a_af_ops = {
	.validate_params	= h3a_af_validate_params,
	.set_params		= h3a_af_set_params,
	.setup_regs		= h3a_af_setup_regs,
	.enable			= h3a_af_enable,
	.busy			= h3a_af_busy,
};

static const struct v4l2_subdev_core_ops h3a_af_subdev_core_ops = {
	.ioctl = h3a_af_ioctl,
	.subscribe_event = omap3isp_stat_subscribe_event,
	.unsubscribe_event = omap3isp_stat_unsubscribe_event,
};

static const struct v4l2_subdev_video_ops h3a_af_subdev_video_ops = {
	.s_stream = omap3isp_stat_s_stream,
};

static const struct v4l2_subdev_ops h3a_af_subdev_ops = {
	.core = &h3a_af_subdev_core_ops,
	.video = &h3a_af_subdev_video_ops,
};

/* Function to register the AF character device driver. */
int omap3isp_h3a_af_init(struct isp_device *isp)
{
	struct ispstat *af = &isp->isp_af;
	struct omap3isp_h3a_af_config *af_cfg;
	struct omap3isp_h3a_af_config *af_recover_cfg;
	int ret;

	af_cfg = kzalloc(sizeof(*af_cfg), GFP_KERNEL);
	if (af_cfg == NULL)
		return -ENOMEM;

	memset(af, 0, sizeof(*af));
	af->ops = &h3a_af_ops;
	af->priv = af_cfg;
	af->dma_ch = -1;
	af->event_type = V4L2_EVENT_OMAP3ISP_AF;
	af->isp = isp;

	/* Set recover state configuration */
	af_recover_cfg = kzalloc(sizeof(*af_recover_cfg), GFP_KERNEL);
	if (!af_recover_cfg) {
		dev_err(af->isp->dev, "AF: cannot allocate memory for recover "
				      "configuration.\n");
		ret = -ENOMEM;
		goto err_recover_alloc;
	}

	af_recover_cfg->paxel.h_start = OMAP3ISP_AF_PAXEL_HZSTART_MIN;
	af_recover_cfg->paxel.width = OMAP3ISP_AF_PAXEL_WIDTH_MIN;
	af_recover_cfg->paxel.height = OMAP3ISP_AF_PAXEL_HEIGHT_MIN;
	af_recover_cfg->paxel.h_cnt = OMAP3ISP_AF_PAXEL_HORIZONTAL_COUNT_MIN;
	af_recover_cfg->paxel.v_cnt = OMAP3ISP_AF_PAXEL_VERTICAL_COUNT_MIN;
	af_recover_cfg->paxel.line_inc = OMAP3ISP_AF_PAXEL_INCREMENT_MIN;
	if (h3a_af_validate_params(af, af_recover_cfg)) {
		dev_err(af->isp->dev, "AF: recover configuration is "
				      "invalid.\n");
		ret = -EINVAL;
		goto err_conf;
	}

	af_recover_cfg->buf_size = h3a_af_get_buf_size(af_recover_cfg);
	af->recover_priv = af_recover_cfg;

	ret = omap3isp_stat_init(af, "AF", &h3a_af_subdev_ops);
	if (ret)
		goto err_conf;

	return 0;

err_conf:
	kfree(af_recover_cfg);
err_recover_alloc:
	kfree(af_cfg);

	return ret;
}

void omap3isp_h3a_af_cleanup(struct isp_device *isp)
{
	kfree(isp->isp_af.priv);
	kfree(isp->isp_af.recover_priv);
	omap3isp_stat_cleanup(&isp->isp_af);
}
