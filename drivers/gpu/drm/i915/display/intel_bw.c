// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <drm/drm_atomic_state_helper.h>

#include "i915_drv.h"
#include "i915_reg.h"
#include "i915_utils.h"
#include "intel_atomic.h"
#include "intel_bw.h"
#include "intel_cdclk.h"
#include "intel_display_core.h"
#include "intel_display_types.h"
#include "skl_watermark.h"
#include "intel_mchbar_regs.h"
#include "intel_pcode.h"

/* Parameters for Qclk Geyserville (QGV) */
struct intel_qgv_point {
	u16 dclk, t_rp, t_rdpre, t_rc, t_ras, t_rcd;
};

#define DEPROGBWPCLIMIT		60

struct intel_psf_gv_point {
	u8 clk; /* clock in multiples of 16.6666 MHz */
};

struct intel_qgv_info {
	struct intel_qgv_point points[I915_NUM_QGV_POINTS];
	struct intel_psf_gv_point psf_points[I915_NUM_PSF_GV_POINTS];
	u8 num_points;
	u8 num_psf_points;
	u8 t_bl;
	u8 max_numchannels;
	u8 channel_width;
	u8 deinterleave;
};

static int dg1_mchbar_read_qgv_point_info(struct drm_i915_private *dev_priv,
					  struct intel_qgv_point *sp,
					  int point)
{
	u32 dclk_ratio, dclk_reference;
	u32 val;

	val = intel_uncore_read(&dev_priv->uncore, SA_PERF_STATUS_0_0_0_MCHBAR_PC);
	dclk_ratio = REG_FIELD_GET(DG1_QCLK_RATIO_MASK, val);
	if (val & DG1_QCLK_REFERENCE)
		dclk_reference = 6; /* 6 * 16.666 MHz = 100 MHz */
	else
		dclk_reference = 8; /* 8 * 16.666 MHz = 133 MHz */
	sp->dclk = DIV_ROUND_UP((16667 * dclk_ratio * dclk_reference) + 500, 1000);

	val = intel_uncore_read(&dev_priv->uncore, SKL_MC_BIOS_DATA_0_0_0_MCHBAR_PCU);
	if (val & DG1_GEAR_TYPE)
		sp->dclk *= 2;

	if (sp->dclk == 0)
		return -EINVAL;

	val = intel_uncore_read(&dev_priv->uncore, MCHBAR_CH0_CR_TC_PRE_0_0_0_MCHBAR);
	sp->t_rp = REG_FIELD_GET(DG1_DRAM_T_RP_MASK, val);
	sp->t_rdpre = REG_FIELD_GET(DG1_DRAM_T_RDPRE_MASK, val);

	val = intel_uncore_read(&dev_priv->uncore, MCHBAR_CH0_CR_TC_PRE_0_0_0_MCHBAR_HIGH);
	sp->t_rcd = REG_FIELD_GET(DG1_DRAM_T_RCD_MASK, val);
	sp->t_ras = REG_FIELD_GET(DG1_DRAM_T_RAS_MASK, val);

	sp->t_rc = sp->t_rp + sp->t_ras;

	return 0;
}

static int icl_pcode_read_qgv_point_info(struct drm_i915_private *dev_priv,
					 struct intel_qgv_point *sp,
					 int point)
{
	u32 val = 0, val2 = 0;
	u16 dclk;
	int ret;

	ret = snb_pcode_read(&dev_priv->uncore, ICL_PCODE_MEM_SUBSYSYSTEM_INFO |
			     ICL_PCODE_MEM_SS_READ_QGV_POINT_INFO(point),
			     &val, &val2);
	if (ret)
		return ret;

	dclk = val & 0xffff;
	sp->dclk = DIV_ROUND_UP((16667 * dclk) + (DISPLAY_VER(dev_priv) >= 12 ? 500 : 0),
				1000);
	sp->t_rp = (val & 0xff0000) >> 16;
	sp->t_rcd = (val & 0xff000000) >> 24;

	sp->t_rdpre = val2 & 0xff;
	sp->t_ras = (val2 & 0xff00) >> 8;

	sp->t_rc = sp->t_rp + sp->t_ras;

	return 0;
}

static int adls_pcode_read_psf_gv_point_info(struct drm_i915_private *dev_priv,
					    struct intel_psf_gv_point *points)
{
	u32 val = 0;
	int ret;
	int i;

	ret = snb_pcode_read(&dev_priv->uncore, ICL_PCODE_MEM_SUBSYSYSTEM_INFO |
			     ADL_PCODE_MEM_SS_READ_PSF_GV_INFO, &val, NULL);
	if (ret)
		return ret;

	for (i = 0; i < I915_NUM_PSF_GV_POINTS; i++) {
		points[i].clk = val & 0xff;
		val >>= 8;
	}

	return 0;
}

static u16 icl_qgv_points_mask(struct drm_i915_private *i915)
{
	unsigned int num_psf_gv_points = i915->display.bw.max[0].num_psf_gv_points;
	unsigned int num_qgv_points = i915->display.bw.max[0].num_qgv_points;
	u16 qgv_points = 0, psf_points = 0;

	/*
	 * We can _not_ use the whole ADLS_QGV_PT_MASK here, as PCode rejects
	 * it with failure if we try masking any unadvertised points.
	 * So need to operate only with those returned from PCode.
	 */
	if (num_qgv_points > 0)
		qgv_points = GENMASK(num_qgv_points - 1, 0);

	if (num_psf_gv_points > 0)
		psf_points = GENMASK(num_psf_gv_points - 1, 0);

	return ICL_PCODE_REQ_QGV_PT(qgv_points) | ADLS_PCODE_REQ_PSF_PT(psf_points);
}

static bool is_sagv_enabled(struct drm_i915_private *i915, u16 points_mask)
{
	return !is_power_of_2(~points_mask & icl_qgv_points_mask(i915) &
			      ICL_PCODE_REQ_QGV_PT_MASK);
}

int icl_pcode_restrict_qgv_points(struct drm_i915_private *dev_priv,
				  u32 points_mask)
{
	int ret;

	if (DISPLAY_VER(dev_priv) >= 14)
		return 0;

	/* bspec says to keep retrying for at least 1 ms */
	ret = skl_pcode_request(&dev_priv->uncore, ICL_PCODE_SAGV_DE_MEM_SS_CONFIG,
				points_mask,
				ICL_PCODE_REP_QGV_MASK | ADLS_PCODE_REP_PSF_MASK,
				ICL_PCODE_REP_QGV_SAFE | ADLS_PCODE_REP_PSF_SAFE,
				1);

	if (ret < 0) {
		drm_err(&dev_priv->drm,
			"Failed to disable qgv points (0x%x) points: 0x%x\n",
			ret, points_mask);
		return ret;
	}

	dev_priv->display.sagv.status = is_sagv_enabled(dev_priv, points_mask) ?
		I915_SAGV_ENABLED : I915_SAGV_DISABLED;

	return 0;
}

static int mtl_read_qgv_point_info(struct drm_i915_private *dev_priv,
				   struct intel_qgv_point *sp, int point)
{
	u32 val, val2;
	u16 dclk;

	val = intel_uncore_read(&dev_priv->uncore,
				MTL_MEM_SS_INFO_QGV_POINT_LOW(point));
	val2 = intel_uncore_read(&dev_priv->uncore,
				 MTL_MEM_SS_INFO_QGV_POINT_HIGH(point));
	dclk = REG_FIELD_GET(MTL_DCLK_MASK, val);
	sp->dclk = DIV_ROUND_CLOSEST(16667 * dclk, 1000);
	sp->t_rp = REG_FIELD_GET(MTL_TRP_MASK, val);
	sp->t_rcd = REG_FIELD_GET(MTL_TRCD_MASK, val);

	sp->t_rdpre = REG_FIELD_GET(MTL_TRDPRE_MASK, val2);
	sp->t_ras = REG_FIELD_GET(MTL_TRAS_MASK, val2);

	sp->t_rc = sp->t_rp + sp->t_ras;

	return 0;
}

static int
intel_read_qgv_point_info(struct drm_i915_private *dev_priv,
			  struct intel_qgv_point *sp,
			  int point)
{
	if (DISPLAY_VER(dev_priv) >= 14)
		return mtl_read_qgv_point_info(dev_priv, sp, point);
	else if (IS_DG1(dev_priv))
		return dg1_mchbar_read_qgv_point_info(dev_priv, sp, point);
	else
		return icl_pcode_read_qgv_point_info(dev_priv, sp, point);
}

static int icl_get_qgv_points(struct drm_i915_private *dev_priv,
			      struct intel_qgv_info *qi,
			      bool is_y_tile)
{
	const struct dram_info *dram_info = &dev_priv->dram_info;
	int i, ret;

	qi->num_points = dram_info->num_qgv_points;
	qi->num_psf_points = dram_info->num_psf_gv_points;

	if (DISPLAY_VER(dev_priv) >= 14) {
		switch (dram_info->type) {
		case INTEL_DRAM_DDR4:
			qi->t_bl = 4;
			qi->max_numchannels = 2;
			qi->channel_width = 64;
			qi->deinterleave = 2;
			break;
		case INTEL_DRAM_DDR5:
			qi->t_bl = 8;
			qi->max_numchannels = 4;
			qi->channel_width = 32;
			qi->deinterleave = 2;
			break;
		case INTEL_DRAM_LPDDR4:
		case INTEL_DRAM_LPDDR5:
			qi->t_bl = 16;
			qi->max_numchannels = 8;
			qi->channel_width = 16;
			qi->deinterleave = 4;
			break;
		case INTEL_DRAM_GDDR:
			qi->channel_width = 32;
			break;
		default:
			MISSING_CASE(dram_info->type);
			return -EINVAL;
		}
	} else if (DISPLAY_VER(dev_priv) >= 12) {
		switch (dram_info->type) {
		case INTEL_DRAM_DDR4:
			qi->t_bl = is_y_tile ? 8 : 4;
			qi->max_numchannels = 2;
			qi->channel_width = 64;
			qi->deinterleave = is_y_tile ? 1 : 2;
			break;
		case INTEL_DRAM_DDR5:
			qi->t_bl = is_y_tile ? 16 : 8;
			qi->max_numchannels = 4;
			qi->channel_width = 32;
			qi->deinterleave = is_y_tile ? 1 : 2;
			break;
		case INTEL_DRAM_LPDDR4:
			if (IS_ROCKETLAKE(dev_priv)) {
				qi->t_bl = 8;
				qi->max_numchannels = 4;
				qi->channel_width = 32;
				qi->deinterleave = 2;
				break;
			}
			fallthrough;
		case INTEL_DRAM_LPDDR5:
			qi->t_bl = 16;
			qi->max_numchannels = 8;
			qi->channel_width = 16;
			qi->deinterleave = is_y_tile ? 2 : 4;
			break;
		default:
			qi->t_bl = 16;
			qi->max_numchannels = 1;
			break;
		}
	} else if (DISPLAY_VER(dev_priv) == 11) {
		qi->t_bl = dev_priv->dram_info.type == INTEL_DRAM_DDR4 ? 4 : 8;
		qi->max_numchannels = 1;
	}

	if (drm_WARN_ON(&dev_priv->drm,
			qi->num_points > ARRAY_SIZE(qi->points)))
		qi->num_points = ARRAY_SIZE(qi->points);

	for (i = 0; i < qi->num_points; i++) {
		struct intel_qgv_point *sp = &qi->points[i];

		ret = intel_read_qgv_point_info(dev_priv, sp, i);
		if (ret) {
			drm_dbg_kms(&dev_priv->drm, "Could not read QGV %d info\n", i);
			return ret;
		}

		drm_dbg_kms(&dev_priv->drm,
			    "QGV %d: DCLK=%d tRP=%d tRDPRE=%d tRAS=%d tRCD=%d tRC=%d\n",
			    i, sp->dclk, sp->t_rp, sp->t_rdpre, sp->t_ras,
			    sp->t_rcd, sp->t_rc);
	}

	if (qi->num_psf_points > 0) {
		ret = adls_pcode_read_psf_gv_point_info(dev_priv, qi->psf_points);
		if (ret) {
			drm_err(&dev_priv->drm, "Failed to read PSF point data; PSF points will not be considered in bandwidth calculations.\n");
			qi->num_psf_points = 0;
		}

		for (i = 0; i < qi->num_psf_points; i++)
			drm_dbg_kms(&dev_priv->drm,
				    "PSF GV %d: CLK=%d \n",
				    i, qi->psf_points[i].clk);
	}

	return 0;
}

static int adl_calc_psf_bw(int clk)
{
	/*
	 * clk is multiples of 16.666MHz (100/6)
	 * According to BSpec PSF GV bandwidth is
	 * calculated as BW = 64 * clk * 16.666Mhz
	 */
	return DIV_ROUND_CLOSEST(64 * clk * 100, 6);
}

static int icl_sagv_max_dclk(const struct intel_qgv_info *qi)
{
	u16 dclk = 0;
	int i;

	for (i = 0; i < qi->num_points; i++)
		dclk = max(dclk, qi->points[i].dclk);

	return dclk;
}

struct intel_sa_info {
	u16 displayrtids;
	u8 deburst, deprogbwlimit, derating;
};

static const struct intel_sa_info icl_sa_info = {
	.deburst = 8,
	.deprogbwlimit = 25, /* GB/s */
	.displayrtids = 128,
	.derating = 10,
};

static const struct intel_sa_info tgl_sa_info = {
	.deburst = 16,
	.deprogbwlimit = 34, /* GB/s */
	.displayrtids = 256,
	.derating = 10,
};

static const struct intel_sa_info rkl_sa_info = {
	.deburst = 8,
	.deprogbwlimit = 20, /* GB/s */
	.displayrtids = 128,
	.derating = 10,
};

static const struct intel_sa_info adls_sa_info = {
	.deburst = 16,
	.deprogbwlimit = 38, /* GB/s */
	.displayrtids = 256,
	.derating = 10,
};

static const struct intel_sa_info adlp_sa_info = {
	.deburst = 16,
	.deprogbwlimit = 38, /* GB/s */
	.displayrtids = 256,
	.derating = 20,
};

static const struct intel_sa_info mtl_sa_info = {
	.deburst = 32,
	.deprogbwlimit = 38, /* GB/s */
	.displayrtids = 256,
	.derating = 10,
};

static const struct intel_sa_info xe2_hpd_sa_info = {
	.derating = 30,
	.deprogbwlimit = 53,
	/* Other values not used by simplified algorithm */
};

static int icl_get_bw_info(struct drm_i915_private *dev_priv, const struct intel_sa_info *sa)
{
	struct intel_qgv_info qi = {};
	bool is_y_tile = true; /* assume y tile may be used */
	int num_channels = max_t(u8, 1, dev_priv->dram_info.num_channels);
	int ipqdepth, ipqdepthpch = 16;
	int dclk_max;
	int maxdebw;
	int num_groups = ARRAY_SIZE(dev_priv->display.bw.max);
	int i, ret;

	ret = icl_get_qgv_points(dev_priv, &qi, is_y_tile);
	if (ret) {
		drm_dbg_kms(&dev_priv->drm,
			    "Failed to get memory subsystem information, ignoring bandwidth limits");
		return ret;
	}

	dclk_max = icl_sagv_max_dclk(&qi);
	maxdebw = min(sa->deprogbwlimit * 1000, dclk_max * 16 * 6 / 10);
	ipqdepth = min(ipqdepthpch, sa->displayrtids / num_channels);
	qi.deinterleave = DIV_ROUND_UP(num_channels, is_y_tile ? 4 : 2);

	for (i = 0; i < num_groups; i++) {
		struct intel_bw_info *bi = &dev_priv->display.bw.max[i];
		int clpchgroup;
		int j;

		clpchgroup = (sa->deburst * qi.deinterleave / num_channels) << i;
		bi->num_planes = (ipqdepth - clpchgroup) / clpchgroup + 1;

		bi->num_qgv_points = qi.num_points;
		bi->num_psf_gv_points = qi.num_psf_points;

		for (j = 0; j < qi.num_points; j++) {
			const struct intel_qgv_point *sp = &qi.points[j];
			int ct, bw;

			/*
			 * Max row cycle time
			 *
			 * FIXME what is the logic behind the
			 * assumed burst length?
			 */
			ct = max_t(int, sp->t_rc, sp->t_rp + sp->t_rcd +
				   (clpchgroup - 1) * qi.t_bl + sp->t_rdpre);
			bw = DIV_ROUND_UP(sp->dclk * clpchgroup * 32 * num_channels, ct);

			bi->deratedbw[j] = min(maxdebw,
					       bw * (100 - sa->derating) / 100);

			drm_dbg_kms(&dev_priv->drm,
				    "BW%d / QGV %d: num_planes=%d deratedbw=%u\n",
				    i, j, bi->num_planes, bi->deratedbw[j]);
		}
	}
	/*
	 * In case if SAGV is disabled in BIOS, we always get 1
	 * SAGV point, but we can't send PCode commands to restrict it
	 * as it will fail and pointless anyway.
	 */
	if (qi.num_points == 1)
		dev_priv->display.sagv.status = I915_SAGV_NOT_CONTROLLED;
	else
		dev_priv->display.sagv.status = I915_SAGV_ENABLED;

	return 0;
}

static int tgl_get_bw_info(struct drm_i915_private *dev_priv, const struct intel_sa_info *sa)
{
	struct intel_qgv_info qi = {};
	const struct dram_info *dram_info = &dev_priv->dram_info;
	bool is_y_tile = true; /* assume y tile may be used */
	int num_channels = max_t(u8, 1, dev_priv->dram_info.num_channels);
	int ipqdepth, ipqdepthpch = 16;
	int dclk_max;
	int maxdebw, peakbw;
	int clperchgroup;
	int num_groups = ARRAY_SIZE(dev_priv->display.bw.max);
	int i, ret;

	ret = icl_get_qgv_points(dev_priv, &qi, is_y_tile);
	if (ret) {
		drm_dbg_kms(&dev_priv->drm,
			    "Failed to get memory subsystem information, ignoring bandwidth limits");
		return ret;
	}

	if (DISPLAY_VER(dev_priv) < 14 &&
	    (dram_info->type == INTEL_DRAM_LPDDR4 || dram_info->type == INTEL_DRAM_LPDDR5))
		num_channels *= 2;

	qi.deinterleave = qi.deinterleave ? : DIV_ROUND_UP(num_channels, is_y_tile ? 4 : 2);

	if (num_channels < qi.max_numchannels && DISPLAY_VER(dev_priv) >= 12)
		qi.deinterleave = max(DIV_ROUND_UP(qi.deinterleave, 2), 1);

	if (DISPLAY_VER(dev_priv) >= 12 && num_channels > qi.max_numchannels)
		drm_warn(&dev_priv->drm, "Number of channels exceeds max number of channels.");
	if (qi.max_numchannels != 0)
		num_channels = min_t(u8, num_channels, qi.max_numchannels);

	dclk_max = icl_sagv_max_dclk(&qi);

	peakbw = num_channels * DIV_ROUND_UP(qi.channel_width, 8) * dclk_max;
	maxdebw = min(sa->deprogbwlimit * 1000, peakbw * DEPROGBWPCLIMIT / 100);

	ipqdepth = min(ipqdepthpch, sa->displayrtids / num_channels);
	/*
	 * clperchgroup = 4kpagespermempage * clperchperblock,
	 * clperchperblock = 8 / num_channels * interleave
	 */
	clperchgroup = 4 * DIV_ROUND_UP(8, num_channels) * qi.deinterleave;

	for (i = 0; i < num_groups; i++) {
		struct intel_bw_info *bi = &dev_priv->display.bw.max[i];
		struct intel_bw_info *bi_next;
		int clpchgroup;
		int j;

		clpchgroup = (sa->deburst * qi.deinterleave / num_channels) << i;

		if (i < num_groups - 1) {
			bi_next = &dev_priv->display.bw.max[i + 1];

			if (clpchgroup < clperchgroup)
				bi_next->num_planes = (ipqdepth - clpchgroup) /
						       clpchgroup + 1;
			else
				bi_next->num_planes = 0;
		}

		bi->num_qgv_points = qi.num_points;
		bi->num_psf_gv_points = qi.num_psf_points;

		for (j = 0; j < qi.num_points; j++) {
			const struct intel_qgv_point *sp = &qi.points[j];
			int ct, bw;

			/*
			 * Max row cycle time
			 *
			 * FIXME what is the logic behind the
			 * assumed burst length?
			 */
			ct = max_t(int, sp->t_rc, sp->t_rp + sp->t_rcd +
				   (clpchgroup - 1) * qi.t_bl + sp->t_rdpre);
			bw = DIV_ROUND_UP(sp->dclk * clpchgroup * 32 * num_channels, ct);

			bi->deratedbw[j] = min(maxdebw,
					       bw * (100 - sa->derating) / 100);
			bi->peakbw[j] = DIV_ROUND_CLOSEST(sp->dclk *
							  num_channels *
							  qi.channel_width, 8);

			drm_dbg_kms(&dev_priv->drm,
				    "BW%d / QGV %d: num_planes=%d deratedbw=%u peakbw: %u\n",
				    i, j, bi->num_planes, bi->deratedbw[j],
				    bi->peakbw[j]);
		}

		for (j = 0; j < qi.num_psf_points; j++) {
			const struct intel_psf_gv_point *sp = &qi.psf_points[j];

			bi->psf_bw[j] = adl_calc_psf_bw(sp->clk);

			drm_dbg_kms(&dev_priv->drm,
				    "BW%d / PSF GV %d: num_planes=%d bw=%u\n",
				    i, j, bi->num_planes, bi->psf_bw[j]);
		}
	}

	/*
	 * In case if SAGV is disabled in BIOS, we always get 1
	 * SAGV point, but we can't send PCode commands to restrict it
	 * as it will fail and pointless anyway.
	 */
	if (qi.num_points == 1)
		dev_priv->display.sagv.status = I915_SAGV_NOT_CONTROLLED;
	else
		dev_priv->display.sagv.status = I915_SAGV_ENABLED;

	return 0;
}

static void dg2_get_bw_info(struct drm_i915_private *i915)
{
	unsigned int deratedbw = IS_DG2_G11(i915) ? 38000 : 50000;
	int num_groups = ARRAY_SIZE(i915->display.bw.max);
	int i;

	/*
	 * DG2 doesn't have SAGV or QGV points, just a constant max bandwidth
	 * that doesn't depend on the number of planes enabled. So fill all the
	 * plane group with constant bw information for uniformity with other
	 * platforms. DG2-G10 platforms have a constant 50 GB/s bandwidth,
	 * whereas DG2-G11 platforms have 38 GB/s.
	 */
	for (i = 0; i < num_groups; i++) {
		struct intel_bw_info *bi = &i915->display.bw.max[i];

		bi->num_planes = 1;
		/* Need only one dummy QGV point per group */
		bi->num_qgv_points = 1;
		bi->deratedbw[0] = deratedbw;
	}

	i915->display.sagv.status = I915_SAGV_NOT_CONTROLLED;
}

static int xe2_hpd_get_bw_info(struct drm_i915_private *i915,
			       const struct intel_sa_info *sa)
{
	struct intel_qgv_info qi = {};
	int num_channels = i915->dram_info.num_channels;
	int peakbw, maxdebw;
	int ret, i;

	ret = icl_get_qgv_points(i915, &qi, true);
	if (ret) {
		drm_dbg_kms(&i915->drm,
			    "Failed to get memory subsystem information, ignoring bandwidth limits");
		return ret;
	}

	peakbw = num_channels * qi.channel_width / 8 * icl_sagv_max_dclk(&qi);
	maxdebw = min(sa->deprogbwlimit * 1000, peakbw * DEPROGBWPCLIMIT / 10);

	for (i = 0; i < qi.num_points; i++) {
		const struct intel_qgv_point *point = &qi.points[i];
		int bw = num_channels * (qi.channel_width / 8) * point->dclk;

		i915->display.bw.max[0].deratedbw[i] =
			min(maxdebw, (100 - sa->derating) * bw / 100);
		i915->display.bw.max[0].peakbw[i] = bw;

		drm_dbg_kms(&i915->drm, "QGV %d: deratedbw=%u peakbw: %u\n",
			    i, i915->display.bw.max[0].deratedbw[i],
			    i915->display.bw.max[0].peakbw[i]);
	}

	/* Bandwidth does not depend on # of planes; set all groups the same */
	i915->display.bw.max[0].num_planes = 1;
	i915->display.bw.max[0].num_qgv_points = qi.num_points;
	for (i = 1; i < ARRAY_SIZE(i915->display.bw.max); i++)
		memcpy(&i915->display.bw.max[i], &i915->display.bw.max[0],
		       sizeof(i915->display.bw.max[0]));

	/*
	 * Xe2_HPD should always have exactly two QGV points representing
	 * battery and plugged-in operation.
	 */
	drm_WARN_ON(&i915->drm, qi.num_points != 2);
	i915->display.sagv.status = I915_SAGV_ENABLED;

	return 0;
}

static unsigned int icl_max_bw_index(struct drm_i915_private *dev_priv,
				     int num_planes, int qgv_point)
{
	int i;

	/*
	 * Let's return max bw for 0 planes
	 */
	num_planes = max(1, num_planes);

	for (i = 0; i < ARRAY_SIZE(dev_priv->display.bw.max); i++) {
		const struct intel_bw_info *bi =
			&dev_priv->display.bw.max[i];

		/*
		 * Pcode will not expose all QGV points when
		 * SAGV is forced to off/min/med/max.
		 */
		if (qgv_point >= bi->num_qgv_points)
			return UINT_MAX;

		if (num_planes >= bi->num_planes)
			return i;
	}

	return UINT_MAX;
}

static unsigned int tgl_max_bw_index(struct drm_i915_private *dev_priv,
				     int num_planes, int qgv_point)
{
	int i;

	/*
	 * Let's return max bw for 0 planes
	 */
	num_planes = max(1, num_planes);

	for (i = ARRAY_SIZE(dev_priv->display.bw.max) - 1; i >= 0; i--) {
		const struct intel_bw_info *bi =
			&dev_priv->display.bw.max[i];

		/*
		 * Pcode will not expose all QGV points when
		 * SAGV is forced to off/min/med/max.
		 */
		if (qgv_point >= bi->num_qgv_points)
			return UINT_MAX;

		if (num_planes <= bi->num_planes)
			return i;
	}

	return 0;
}

static unsigned int adl_psf_bw(struct drm_i915_private *dev_priv,
			       int psf_gv_point)
{
	const struct intel_bw_info *bi =
			&dev_priv->display.bw.max[0];

	return bi->psf_bw[psf_gv_point];
}

static unsigned int icl_qgv_bw(struct drm_i915_private *i915,
			       int num_active_planes, int qgv_point)
{
	unsigned int idx;

	if (DISPLAY_VER(i915) >= 12)
		idx = tgl_max_bw_index(i915, num_active_planes, qgv_point);
	else
		idx = icl_max_bw_index(i915, num_active_planes, qgv_point);

	if (idx >= ARRAY_SIZE(i915->display.bw.max))
		return 0;

	return i915->display.bw.max[idx].deratedbw[qgv_point];
}

void intel_bw_init_hw(struct drm_i915_private *dev_priv)
{
	if (!HAS_DISPLAY(dev_priv))
		return;

	if (DISPLAY_VERx100(dev_priv) >= 1401 && IS_DGFX(dev_priv))
		xe2_hpd_get_bw_info(dev_priv, &xe2_hpd_sa_info);
	else if (DISPLAY_VER(dev_priv) >= 14)
		tgl_get_bw_info(dev_priv, &mtl_sa_info);
	else if (IS_DG2(dev_priv))
		dg2_get_bw_info(dev_priv);
	else if (IS_ALDERLAKE_P(dev_priv))
		tgl_get_bw_info(dev_priv, &adlp_sa_info);
	else if (IS_ALDERLAKE_S(dev_priv))
		tgl_get_bw_info(dev_priv, &adls_sa_info);
	else if (IS_ROCKETLAKE(dev_priv))
		tgl_get_bw_info(dev_priv, &rkl_sa_info);
	else if (DISPLAY_VER(dev_priv) == 12)
		tgl_get_bw_info(dev_priv, &tgl_sa_info);
	else if (DISPLAY_VER(dev_priv) == 11)
		icl_get_bw_info(dev_priv, &icl_sa_info);
}

static unsigned int intel_bw_crtc_num_active_planes(const struct intel_crtc_state *crtc_state)
{
	/*
	 * We assume cursors are small enough
	 * to not not cause bandwidth problems.
	 */
	return hweight8(crtc_state->active_planes & ~BIT(PLANE_CURSOR));
}

static unsigned int intel_bw_crtc_data_rate(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	unsigned int data_rate = 0;
	enum plane_id plane_id;

	for_each_plane_id_on_crtc(crtc, plane_id) {
		/*
		 * We assume cursors are small enough
		 * to not not cause bandwidth problems.
		 */
		if (plane_id == PLANE_CURSOR)
			continue;

		data_rate += crtc_state->data_rate[plane_id];

		if (DISPLAY_VER(i915) < 11)
			data_rate += crtc_state->data_rate_y[plane_id];
	}

	return data_rate;
}

/* "Maximum Pipe Read Bandwidth" */
static int intel_bw_crtc_min_cdclk(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	if (DISPLAY_VER(i915) < 12)
		return 0;

	return DIV_ROUND_UP_ULL(mul_u32_u32(intel_bw_crtc_data_rate(crtc_state), 10), 512);
}

void intel_bw_crtc_update(struct intel_bw_state *bw_state,
			  const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	bw_state->data_rate[crtc->pipe] =
		intel_bw_crtc_data_rate(crtc_state);
	bw_state->num_active_planes[crtc->pipe] =
		intel_bw_crtc_num_active_planes(crtc_state);
	bw_state->force_check_qgv = true;

	drm_dbg_kms(&i915->drm, "pipe %c data rate %u num active planes %u\n",
		    pipe_name(crtc->pipe),
		    bw_state->data_rate[crtc->pipe],
		    bw_state->num_active_planes[crtc->pipe]);
}

static unsigned int intel_bw_num_active_planes(struct drm_i915_private *dev_priv,
					       const struct intel_bw_state *bw_state)
{
	unsigned int num_active_planes = 0;
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe)
		num_active_planes += bw_state->num_active_planes[pipe];

	return num_active_planes;
}

static unsigned int intel_bw_data_rate(struct drm_i915_private *dev_priv,
				       const struct intel_bw_state *bw_state)
{
	unsigned int data_rate = 0;
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe)
		data_rate += bw_state->data_rate[pipe];

	if (DISPLAY_VER(dev_priv) >= 13 && i915_vtd_active(dev_priv))
		data_rate = DIV_ROUND_UP(data_rate * 105, 100);

	return data_rate;
}

struct intel_bw_state *
intel_atomic_get_old_bw_state(struct intel_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_global_state *bw_state;

	bw_state = intel_atomic_get_old_global_obj_state(state, &dev_priv->display.bw.obj);

	return to_intel_bw_state(bw_state);
}

struct intel_bw_state *
intel_atomic_get_new_bw_state(struct intel_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_global_state *bw_state;

	bw_state = intel_atomic_get_new_global_obj_state(state, &dev_priv->display.bw.obj);

	return to_intel_bw_state(bw_state);
}

struct intel_bw_state *
intel_atomic_get_bw_state(struct intel_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_global_state *bw_state;

	bw_state = intel_atomic_get_global_obj_state(state, &dev_priv->display.bw.obj);
	if (IS_ERR(bw_state))
		return ERR_CAST(bw_state);

	return to_intel_bw_state(bw_state);
}

static unsigned int icl_max_bw_qgv_point_mask(struct drm_i915_private *i915,
					      int num_active_planes)
{
	unsigned int num_qgv_points = i915->display.bw.max[0].num_qgv_points;
	unsigned int max_bw_point = 0;
	unsigned int max_bw = 0;
	int i;

	for (i = 0; i < num_qgv_points; i++) {
		unsigned int max_data_rate =
			icl_qgv_bw(i915, num_active_planes, i);

		/*
		 * We need to know which qgv point gives us
		 * maximum bandwidth in order to disable SAGV
		 * if we find that we exceed SAGV block time
		 * with watermarks. By that moment we already
		 * have those, as it is calculated earlier in
		 * intel_atomic_check,
		 */
		if (max_data_rate > max_bw) {
			max_bw_point = BIT(i);
			max_bw = max_data_rate;
		}
	}

	return max_bw_point;
}

static u16 icl_prepare_qgv_points_mask(struct drm_i915_private *i915,
				       unsigned int qgv_points,
				       unsigned int psf_points)
{
	return ~(ICL_PCODE_REQ_QGV_PT(qgv_points) |
		 ADLS_PCODE_REQ_PSF_PT(psf_points)) & icl_qgv_points_mask(i915);
}

static unsigned int icl_max_bw_psf_gv_point_mask(struct drm_i915_private *i915)
{
	unsigned int num_psf_gv_points = i915->display.bw.max[0].num_psf_gv_points;
	unsigned int max_bw_point_mask = 0;
	unsigned int max_bw = 0;
	int i;

	for (i = 0; i < num_psf_gv_points; i++) {
		unsigned int max_data_rate = adl_psf_bw(i915, i);

		if (max_data_rate > max_bw) {
			max_bw_point_mask = BIT(i);
			max_bw = max_data_rate;
		} else if (max_data_rate == max_bw) {
			max_bw_point_mask |= BIT(i);
		}
	}

	return max_bw_point_mask;
}

static void icl_force_disable_sagv(struct drm_i915_private *i915,
				   struct intel_bw_state *bw_state)
{
	unsigned int qgv_points = icl_max_bw_qgv_point_mask(i915, 0);
	unsigned int psf_points = icl_max_bw_psf_gv_point_mask(i915);

	bw_state->qgv_points_mask = icl_prepare_qgv_points_mask(i915,
								qgv_points,
								psf_points);

	drm_dbg_kms(&i915->drm, "Forcing SAGV disable: mask 0x%x\n",
		    bw_state->qgv_points_mask);

	icl_pcode_restrict_qgv_points(i915, bw_state->qgv_points_mask);
}

static int mtl_find_qgv_points(struct drm_i915_private *i915,
			       unsigned int data_rate,
			       unsigned int num_active_planes,
			       struct intel_bw_state *new_bw_state)
{
	unsigned int best_rate = UINT_MAX;
	unsigned int num_qgv_points = i915->display.bw.max[0].num_qgv_points;
	unsigned int qgv_peak_bw  = 0;
	int i;
	int ret;

	ret = intel_atomic_lock_global_state(&new_bw_state->base);
	if (ret)
		return ret;

	/*
	 * If SAGV cannot be enabled, disable the pcode SAGV by passing all 1's
	 * for qgv peak bw in PM Demand request. So assign UINT_MAX if SAGV is
	 * not enabled. PM Demand code will clamp the value for the register
	 */
	if (!intel_can_enable_sagv(i915, new_bw_state)) {
		new_bw_state->qgv_point_peakbw = U16_MAX;
		drm_dbg_kms(&i915->drm, "No SAGV, use UINT_MAX as peak bw.");
		return 0;
	}

	/*
	 * Find the best QGV point by comparing the data_rate with max data rate
	 * offered per plane group
	 */
	for (i = 0; i < num_qgv_points; i++) {
		unsigned int bw_index =
			tgl_max_bw_index(i915, num_active_planes, i);
		unsigned int max_data_rate;

		if (bw_index >= ARRAY_SIZE(i915->display.bw.max))
			continue;

		max_data_rate = i915->display.bw.max[bw_index].deratedbw[i];

		if (max_data_rate < data_rate)
			continue;

		if (max_data_rate - data_rate < best_rate) {
			best_rate = max_data_rate - data_rate;
			qgv_peak_bw = i915->display.bw.max[bw_index].peakbw[i];
		}

		drm_dbg_kms(&i915->drm, "QGV point %d: max bw %d required %d qgv_peak_bw: %d\n",
			    i, max_data_rate, data_rate, qgv_peak_bw);
	}

	drm_dbg_kms(&i915->drm, "Matching peaks QGV bw: %d for required data rate: %d\n",
		    qgv_peak_bw, data_rate);

	/*
	 * The display configuration cannot be supported if no QGV point
	 * satisfying the required data rate is found
	 */
	if (qgv_peak_bw == 0) {
		drm_dbg_kms(&i915->drm, "No QGV points for bw %d for display configuration(%d active planes).\n",
			    data_rate, num_active_planes);
		return -EINVAL;
	}

	/* MTL PM DEMAND expects QGV BW parameter in multiples of 100 mbps */
	new_bw_state->qgv_point_peakbw = DIV_ROUND_CLOSEST(qgv_peak_bw, 100);

	return 0;
}

static int icl_find_qgv_points(struct drm_i915_private *i915,
			       unsigned int data_rate,
			       unsigned int num_active_planes,
			       const struct intel_bw_state *old_bw_state,
			       struct intel_bw_state *new_bw_state)
{
	unsigned int num_psf_gv_points = i915->display.bw.max[0].num_psf_gv_points;
	unsigned int num_qgv_points = i915->display.bw.max[0].num_qgv_points;
	u16 psf_points = 0;
	u16 qgv_points = 0;
	int i;
	int ret;

	ret = intel_atomic_lock_global_state(&new_bw_state->base);
	if (ret)
		return ret;

	for (i = 0; i < num_qgv_points; i++) {
		unsigned int max_data_rate = icl_qgv_bw(i915,
							num_active_planes, i);
		if (max_data_rate >= data_rate)
			qgv_points |= BIT(i);

		drm_dbg_kms(&i915->drm, "QGV point %d: max bw %d required %d\n",
			    i, max_data_rate, data_rate);
	}

	for (i = 0; i < num_psf_gv_points; i++) {
		unsigned int max_data_rate = adl_psf_bw(i915, i);

		if (max_data_rate >= data_rate)
			psf_points |= BIT(i);

		drm_dbg_kms(&i915->drm, "PSF GV point %d: max bw %d"
			    " required %d\n",
			    i, max_data_rate, data_rate);
	}

	/*
	 * BSpec states that we always should have at least one allowed point
	 * left, so if we couldn't - simply reject the configuration for obvious
	 * reasons.
	 */
	if (qgv_points == 0) {
		drm_dbg_kms(&i915->drm, "No QGV points provide sufficient memory"
			    " bandwidth %d for display configuration(%d active planes).\n",
			    data_rate, num_active_planes);
		return -EINVAL;
	}

	if (num_psf_gv_points > 0 && psf_points == 0) {
		drm_dbg_kms(&i915->drm, "No PSF GV points provide sufficient memory"
			    " bandwidth %d for display configuration(%d active planes).\n",
			    data_rate, num_active_planes);
		return -EINVAL;
	}

	/*
	 * Leave only single point with highest bandwidth, if
	 * we can't enable SAGV due to the increased memory latency it may
	 * cause.
	 */
	if (!intel_can_enable_sagv(i915, new_bw_state)) {
		qgv_points = icl_max_bw_qgv_point_mask(i915, num_active_planes);
		drm_dbg_kms(&i915->drm, "No SAGV, using single QGV point mask 0x%x\n",
			    qgv_points);
	}

	/*
	 * We store the ones which need to be masked as that is what PCode
	 * actually accepts as a parameter.
	 */
	new_bw_state->qgv_points_mask = icl_prepare_qgv_points_mask(i915,
								    qgv_points,
								    psf_points);
	/*
	 * If the actual mask had changed we need to make sure that
	 * the commits are serialized(in case this is a nomodeset, nonblocking)
	 */
	if (new_bw_state->qgv_points_mask != old_bw_state->qgv_points_mask) {
		ret = intel_atomic_serialize_global_state(&new_bw_state->base);
		if (ret)
			return ret;
	}

	return 0;
}

static int intel_bw_check_qgv_points(struct drm_i915_private *i915,
				     const struct intel_bw_state *old_bw_state,
				     struct intel_bw_state *new_bw_state)
{
	unsigned int data_rate = intel_bw_data_rate(i915, new_bw_state);
	unsigned int num_active_planes =
			intel_bw_num_active_planes(i915, new_bw_state);

	data_rate = DIV_ROUND_UP(data_rate, 1000);

	if (DISPLAY_VER(i915) >= 14)
		return mtl_find_qgv_points(i915, data_rate, num_active_planes,
					   new_bw_state);
	else
		return icl_find_qgv_points(i915, data_rate, num_active_planes,
					   old_bw_state, new_bw_state);
}

static bool intel_bw_state_changed(struct drm_i915_private *i915,
				   const struct intel_bw_state *old_bw_state,
				   const struct intel_bw_state *new_bw_state)
{
	enum pipe pipe;

	for_each_pipe(i915, pipe) {
		const struct intel_dbuf_bw *old_crtc_bw =
			&old_bw_state->dbuf_bw[pipe];
		const struct intel_dbuf_bw *new_crtc_bw =
			&new_bw_state->dbuf_bw[pipe];
		enum dbuf_slice slice;

		for_each_dbuf_slice(i915, slice) {
			if (old_crtc_bw->max_bw[slice] != new_crtc_bw->max_bw[slice] ||
			    old_crtc_bw->active_planes[slice] != new_crtc_bw->active_planes[slice])
				return true;
		}

		if (old_bw_state->min_cdclk[pipe] != new_bw_state->min_cdclk[pipe])
			return true;
	}

	return false;
}

static void skl_plane_calc_dbuf_bw(struct intel_bw_state *bw_state,
				   struct intel_crtc *crtc,
				   enum plane_id plane_id,
				   const struct skl_ddb_entry *ddb,
				   unsigned int data_rate)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	struct intel_dbuf_bw *crtc_bw = &bw_state->dbuf_bw[crtc->pipe];
	unsigned int dbuf_mask = skl_ddb_dbuf_slice_mask(i915, ddb);
	enum dbuf_slice slice;

	/*
	 * The arbiter can only really guarantee an
	 * equal share of the total bw to each plane.
	 */
	for_each_dbuf_slice_in_mask(i915, slice, dbuf_mask) {
		crtc_bw->max_bw[slice] = max(crtc_bw->max_bw[slice], data_rate);
		crtc_bw->active_planes[slice] |= BIT(plane_id);
	}
}

static void skl_crtc_calc_dbuf_bw(struct intel_bw_state *bw_state,
				  const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	struct intel_dbuf_bw *crtc_bw = &bw_state->dbuf_bw[crtc->pipe];
	enum plane_id plane_id;

	memset(crtc_bw, 0, sizeof(*crtc_bw));

	if (!crtc_state->hw.active)
		return;

	for_each_plane_id_on_crtc(crtc, plane_id) {
		/*
		 * We assume cursors are small enough
		 * to not cause bandwidth problems.
		 */
		if (plane_id == PLANE_CURSOR)
			continue;

		skl_plane_calc_dbuf_bw(bw_state, crtc, plane_id,
				       &crtc_state->wm.skl.plane_ddb[plane_id],
				       crtc_state->data_rate[plane_id]);

		if (DISPLAY_VER(i915) < 11)
			skl_plane_calc_dbuf_bw(bw_state, crtc, plane_id,
					       &crtc_state->wm.skl.plane_ddb_y[plane_id],
					       crtc_state->data_rate[plane_id]);
	}
}

/* "Maximum Data Buffer Bandwidth" */
static int
intel_bw_dbuf_min_cdclk(struct drm_i915_private *i915,
			const struct intel_bw_state *bw_state)
{
	unsigned int total_max_bw = 0;
	enum dbuf_slice slice;

	for_each_dbuf_slice(i915, slice) {
		int num_active_planes = 0;
		unsigned int max_bw = 0;
		enum pipe pipe;

		/*
		 * The arbiter can only really guarantee an
		 * equal share of the total bw to each plane.
		 */
		for_each_pipe(i915, pipe) {
			const struct intel_dbuf_bw *crtc_bw = &bw_state->dbuf_bw[pipe];

			max_bw = max(crtc_bw->max_bw[slice], max_bw);
			num_active_planes += hweight8(crtc_bw->active_planes[slice]);
		}
		max_bw *= num_active_planes;

		total_max_bw = max(total_max_bw, max_bw);
	}

	return DIV_ROUND_UP(total_max_bw, 64);
}

int intel_bw_min_cdclk(struct drm_i915_private *i915,
		       const struct intel_bw_state *bw_state)
{
	enum pipe pipe;
	int min_cdclk;

	min_cdclk = intel_bw_dbuf_min_cdclk(i915, bw_state);

	for_each_pipe(i915, pipe)
		min_cdclk = max(min_cdclk, bw_state->min_cdclk[pipe]);

	return min_cdclk;
}

int intel_bw_calc_min_cdclk(struct intel_atomic_state *state,
			    bool *need_cdclk_calc)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_bw_state *new_bw_state = NULL;
	const struct intel_bw_state *old_bw_state = NULL;
	const struct intel_cdclk_state *cdclk_state;
	const struct intel_crtc_state *crtc_state;
	int old_min_cdclk, new_min_cdclk;
	struct intel_crtc *crtc;
	int i;

	if (DISPLAY_VER(dev_priv) < 9)
		return 0;

	for_each_new_intel_crtc_in_state(state, crtc, crtc_state, i) {
		new_bw_state = intel_atomic_get_bw_state(state);
		if (IS_ERR(new_bw_state))
			return PTR_ERR(new_bw_state);

		old_bw_state = intel_atomic_get_old_bw_state(state);

		skl_crtc_calc_dbuf_bw(new_bw_state, crtc_state);

		new_bw_state->min_cdclk[crtc->pipe] =
			intel_bw_crtc_min_cdclk(crtc_state);
	}

	if (!old_bw_state)
		return 0;

	if (intel_bw_state_changed(dev_priv, old_bw_state, new_bw_state)) {
		int ret = intel_atomic_lock_global_state(&new_bw_state->base);
		if (ret)
			return ret;
	}

	old_min_cdclk = intel_bw_min_cdclk(dev_priv, old_bw_state);
	new_min_cdclk = intel_bw_min_cdclk(dev_priv, new_bw_state);

	/*
	 * No need to check against the cdclk state if
	 * the min cdclk doesn't increase.
	 *
	 * Ie. we only ever increase the cdclk due to bandwidth
	 * requirements. This can reduce back and forth
	 * display blinking due to constant cdclk changes.
	 */
	if (new_min_cdclk <= old_min_cdclk)
		return 0;

	cdclk_state = intel_atomic_get_cdclk_state(state);
	if (IS_ERR(cdclk_state))
		return PTR_ERR(cdclk_state);

	/*
	 * No need to recalculate the cdclk state if
	 * the min cdclk doesn't increase.
	 *
	 * Ie. we only ever increase the cdclk due to bandwidth
	 * requirements. This can reduce back and forth
	 * display blinking due to constant cdclk changes.
	 */
	if (new_min_cdclk <= cdclk_state->bw_min_cdclk)
		return 0;

	drm_dbg_kms(&dev_priv->drm,
		    "new bandwidth min cdclk (%d kHz) > old min cdclk (%d kHz)\n",
		    new_min_cdclk, cdclk_state->bw_min_cdclk);
	*need_cdclk_calc = true;

	return 0;
}

static int intel_bw_check_data_rate(struct intel_atomic_state *state, bool *changed)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	const struct intel_crtc_state *new_crtc_state, *old_crtc_state;
	struct intel_crtc *crtc;
	int i;

	for_each_oldnew_intel_crtc_in_state(state, crtc, old_crtc_state,
					    new_crtc_state, i) {
		unsigned int old_data_rate =
			intel_bw_crtc_data_rate(old_crtc_state);
		unsigned int new_data_rate =
			intel_bw_crtc_data_rate(new_crtc_state);
		unsigned int old_active_planes =
			intel_bw_crtc_num_active_planes(old_crtc_state);
		unsigned int new_active_planes =
			intel_bw_crtc_num_active_planes(new_crtc_state);
		struct intel_bw_state *new_bw_state;

		/*
		 * Avoid locking the bw state when
		 * nothing significant has changed.
		 */
		if (old_data_rate == new_data_rate &&
		    old_active_planes == new_active_planes)
			continue;

		new_bw_state = intel_atomic_get_bw_state(state);
		if (IS_ERR(new_bw_state))
			return PTR_ERR(new_bw_state);

		new_bw_state->data_rate[crtc->pipe] = new_data_rate;
		new_bw_state->num_active_planes[crtc->pipe] = new_active_planes;

		*changed = true;

		drm_dbg_kms(&i915->drm,
			    "[CRTC:%d:%s] data rate %u num active planes %u\n",
			    crtc->base.base.id, crtc->base.name,
			    new_bw_state->data_rate[crtc->pipe],
			    new_bw_state->num_active_planes[crtc->pipe]);
	}

	return 0;
}

int intel_bw_atomic_check(struct intel_atomic_state *state)
{
	bool changed = false;
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct intel_bw_state *new_bw_state;
	const struct intel_bw_state *old_bw_state;
	int ret;

	/* FIXME earlier gens need some checks too */
	if (DISPLAY_VER(i915) < 11)
		return 0;

	ret = intel_bw_check_data_rate(state, &changed);
	if (ret)
		return ret;

	old_bw_state = intel_atomic_get_old_bw_state(state);
	new_bw_state = intel_atomic_get_new_bw_state(state);

	if (new_bw_state &&
	    (intel_can_enable_sagv(i915, old_bw_state) !=
	     intel_can_enable_sagv(i915, new_bw_state) ||
	     new_bw_state->force_check_qgv))
		changed = true;

	/*
	 * If none of our inputs (data rates, number of active
	 * planes, SAGV yes/no) changed then nothing to do here.
	 */
	if (!changed)
		return 0;

	ret = intel_bw_check_qgv_points(i915, old_bw_state, new_bw_state);
	if (ret)
		return ret;

	new_bw_state->force_check_qgv = false;

	return 0;
}

static struct intel_global_state *
intel_bw_duplicate_state(struct intel_global_obj *obj)
{
	struct intel_bw_state *state;

	state = kmemdup(obj->state, sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	return &state->base;
}

static void intel_bw_destroy_state(struct intel_global_obj *obj,
				   struct intel_global_state *state)
{
	kfree(state);
}

static const struct intel_global_state_funcs intel_bw_funcs = {
	.atomic_duplicate_state = intel_bw_duplicate_state,
	.atomic_destroy_state = intel_bw_destroy_state,
};

int intel_bw_init(struct drm_i915_private *i915)
{
	struct intel_display *display = &i915->display;
	struct intel_bw_state *state;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	intel_atomic_global_obj_init(display, &display->bw.obj,
				     &state->base, &intel_bw_funcs);

	/*
	 * Limit this only if we have SAGV. And for Display version 14 onwards
	 * sagv is handled though pmdemand requests
	 */
	if (intel_has_sagv(i915) && IS_DISPLAY_VER(i915, 11, 13))
		icl_force_disable_sagv(i915, state);

	return 0;
}
