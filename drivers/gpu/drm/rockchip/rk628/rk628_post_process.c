// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Wyon Bi <bivvy.bi@rock-chips.com>
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/mfd/rk628.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>

#include <drm/drm_of.h>

enum rk628_mode_sync_pol {
	MODE_FLAG_NSYNC,
	MODE_FLAG_PSYNC,
};

struct rk628_post_process {
	struct drm_bridge base;
	struct drm_bridge *bridge;
	struct drm_display_mode src_mode;
	struct drm_display_mode dst_mode;
	struct device *dev;
	struct regmap *grf;
	struct clk *sclk_vop;
	struct clk *clk_rx_read;
	struct reset_control *rstc_decoder;
	struct reset_control *rstc_clk_rx;
	struct reset_control *rstc_vop;
	struct rk628 *parent;
	int sync_pol;
};

static inline struct rk628_post_process *bridge_to_pp(struct drm_bridge *bridge)
{
	return container_of(bridge, struct rk628_post_process, base);
}

static void calc_dsp_frm_hst_vst(const struct videomode *src,
				 const struct videomode *dst,
				 u32 *dsp_frame_hst, u32 *dsp_frame_vst)
{
	u32 bp_in, bp_out;
	u32 v_scale_ratio;
	u64 t_frm_st;
	u64 t_bp_in, t_bp_out, t_delta, tin;
	u32 src_pixclock, dst_pixclock;
	u32 dsp_htotal, src_htotal, src_vtotal;

	src_pixclock = div_u64(1000000000000llu, src->pixelclock);
	dst_pixclock = div_u64(1000000000000llu, dst->pixelclock);

	src_htotal = src->hsync_len + src->hback_porch + src->hactive +
		     src->hfront_porch;
	src_vtotal = src->vsync_len + src->vback_porch + src->vactive +
		     src->vfront_porch;
	dsp_htotal = dst->hsync_len + dst->hback_porch + dst->hactive +
		     dst->hfront_porch;

	bp_in = (src->vback_porch + src->vsync_len) * src_htotal +
		src->hsync_len + src->hback_porch;
	bp_out = (dst->vback_porch + dst->vsync_len) * dsp_htotal +
		 dst->hsync_len + dst->hback_porch;

	t_bp_in = bp_in * src_pixclock;
	t_bp_out = bp_out * dst_pixclock;
	tin = src_vtotal * src_htotal * src_pixclock;

	v_scale_ratio = src->vactive / dst->vactive;
	if (v_scale_ratio <= 2)
		t_delta = 5 * src_htotal * src_pixclock;
	else
		t_delta = 12 * src_htotal * src_pixclock;

	if (t_bp_in + t_delta > t_bp_out)
		t_frm_st = (t_bp_in + t_delta - t_bp_out);
	else
		t_frm_st = tin - (t_bp_out - (t_bp_in + t_delta));

	do_div(t_frm_st, src_pixclock);
	*dsp_frame_hst = do_div(t_frm_st, src_htotal);
	*dsp_frame_vst = t_frm_st;
}

static void rk628_post_process_scaler_init(struct rk628_post_process *pp,
					   const struct drm_display_mode *s,
					   const struct drm_display_mode *d)
{
	struct videomode src, dst;
	u32 dsp_frame_hst, dsp_frame_vst;
	u32 scl_hor_mode, scl_ver_mode;
	u32 scl_v_factor, scl_h_factor;
	u32 dsp_htotal, dsp_hs_end, dsp_hact_st, dsp_hact_end;
	u32 dsp_vtotal, dsp_vs_end, dsp_vact_st, dsp_vact_end;
	u32 dsp_hbor_end, dsp_hbor_st, dsp_vbor_end, dsp_vbor_st;
	u16 bor_right = 0, bor_left = 0, bor_up = 0, bor_down = 0;
	u8 hor_down_mode = 0, ver_down_mode = 0;

	drm_display_mode_to_videomode(s, &src);
	drm_display_mode_to_videomode(d, &dst);

	dsp_htotal = dst.hsync_len + dst.hback_porch + dst.hactive +
		     dst.hfront_porch;
	dsp_vtotal = dst.vsync_len + dst.vback_porch + dst.vactive +
		     dst.vfront_porch;
	dsp_hs_end = dst.hsync_len;
	dsp_vs_end = dst.vsync_len;
	dsp_hbor_end = dst.hsync_len + dst.hback_porch + dst.hactive;
	dsp_hbor_st = dst.hsync_len + dst.hback_porch;
	dsp_vbor_end = dst.vsync_len + dst.vback_porch + dst.vactive;
	dsp_vbor_st = dst.vsync_len + dst.vback_porch;
	dsp_hact_st = dsp_hbor_st + bor_left;
	dsp_hact_end = dsp_hbor_end - bor_right;
	dsp_vact_st = dsp_vbor_st + bor_up;
	dsp_vact_end = dsp_vbor_end - bor_down;

	calc_dsp_frm_hst_vst(&src, &dst, &dsp_frame_hst, &dsp_frame_vst);
	dev_dbg(pp->dev, "dsp_frame_vst=%d, dsp_frame_hst=%d\n",
		dsp_frame_vst, dsp_frame_hst);

	if (src.hactive > dst.hactive) {
		scl_hor_mode = 2;

		if (hor_down_mode == 0) {
			if ((src.hactive - 1) / (dst.hactive - 1) > 2)
				scl_h_factor = ((src.hactive - 1) << 14) /
					       (dst.hactive - 1);
			else
				scl_h_factor = ((src.hactive - 2) << 14) /
					       (dst.hactive - 1);
		} else {
			scl_h_factor = (dst.hactive << 16) /
				       (src.hactive - 1);
		}

		dev_dbg(pp->dev, "horizontal scale down\n");
	} else if (src.hactive == dst.hactive) {
		scl_hor_mode = 0;
		scl_h_factor = 0;

		dev_dbg(pp->dev, "horizontal no scale\n");
	} else {
		scl_hor_mode = 1;
		scl_h_factor = ((src.hactive - 1) << 16) / (dst.hactive - 1);

		dev_dbg(pp->dev, "horizontal scale up\n");
	}

	if (src.vactive > dst.vactive) {
		scl_ver_mode = 2;

		if (ver_down_mode == 0) {
			if ((src.vactive - 1) / (dst.vactive - 1) > 2)
				scl_v_factor = ((src.vactive - 1) << 14) /
					       (dst.vactive - 1);
			else
				scl_v_factor = ((src.vactive - 2) << 14) /
					       (dst.vactive - 1);
		} else {
			scl_v_factor = (dst.vactive << 16) /
				       (src.vactive - 1);
		}

		dev_dbg(pp->dev, "vertical scale down\n");
	} else if (src.vactive == dst.vactive) {
		scl_ver_mode = 0;
		scl_v_factor = 0;

		dev_dbg(pp->dev, "vertical no scale\n");
	} else {
		scl_ver_mode = 1;
		scl_v_factor = ((src.vactive - 1) << 16) / (dst.vactive - 1);

		dev_dbg(pp->dev, "vertical scale up\n");
	}

	regmap_update_bits(pp->grf, GRF_RGB_DEC_CON0,
			   SW_HRES_MASK, SW_HRES(src.hactive));
	regmap_write(pp->grf, GRF_SCALER_CON0,
		     SCL_VER_DOWN_MODE(ver_down_mode) |
		     SCL_HOR_DOWN_MODE(hor_down_mode) |
		     SCL_VER_MODE(scl_ver_mode) | SCL_HOR_MODE(scl_hor_mode));
	regmap_write(pp->grf, GRF_SCALER_CON1,
		     SCL_V_FACTOR(scl_v_factor) | SCL_H_FACTOR(scl_h_factor));
	regmap_write(pp->grf, GRF_SCALER_CON2,
		     DSP_FRAME_VST(dsp_frame_vst) |
		     DSP_FRAME_HST(dsp_frame_hst));
	regmap_write(pp->grf, GRF_SCALER_CON3,
		     DSP_HS_END(dsp_hs_end) | DSP_HTOTAL(dsp_htotal));
	regmap_write(pp->grf, GRF_SCALER_CON4,
		     DSP_HACT_END(dsp_hact_end) | DSP_HACT_ST(dsp_hact_st));
	regmap_write(pp->grf, GRF_SCALER_CON5,
		     DSP_VS_END(dsp_vs_end) | DSP_VTOTAL(dsp_vtotal));
	regmap_write(pp->grf, GRF_SCALER_CON6,
		     DSP_VACT_END(dsp_vact_end) | DSP_VACT_ST(dsp_vact_st));
	regmap_write(pp->grf, GRF_SCALER_CON7,
		     DSP_HBOR_END(dsp_hbor_end) | DSP_HBOR_ST(dsp_hbor_st));
	regmap_write(pp->grf, GRF_SCALER_CON8,
		     DSP_VBOR_END(dsp_vbor_end) | DSP_VBOR_ST(dsp_vbor_st));
}

static void rk628_post_process_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct rk628_post_process *pp = bridge_to_pp(bridge);
	struct drm_display_mode *src = &pp->src_mode;
	struct drm_display_mode *dst = &pp->dst_mode;
	u64 dst_rate, src_rate;

	reset_control_assert(pp->rstc_decoder);
	udelay(10);
	reset_control_deassert(pp->rstc_decoder);
	udelay(10);

	clk_set_rate(pp->clk_rx_read, src->clock * 1000);
	clk_prepare_enable(pp->clk_rx_read);
	reset_control_assert(pp->rstc_clk_rx);
	udelay(10);
	reset_control_deassert(pp->rstc_clk_rx);
	udelay(10);

	src_rate = src->clock * 1000;
	dst_rate = src_rate * dst->vdisplay * dst->htotal;
	do_div(dst_rate, src->vdisplay * src->htotal);
	do_div(dst_rate, 1000);
	dst->clock = dst_rate;

	clk_set_rate(pp->sclk_vop, dst->clock * 1000);
	clk_prepare_enable(pp->sclk_vop);
	reset_control_assert(pp->rstc_vop);
	udelay(10);
	reset_control_deassert(pp->rstc_vop);
	udelay(10);

	regmap_update_bits(pp->grf, GRF_SYSTEM_CON0, SW_VSYNC_POL_MASK,
			   SW_VSYNC_POL(pp->sync_pol));
	regmap_update_bits(pp->grf, GRF_SYSTEM_CON0, SW_HSYNC_POL_MASK,
			   SW_HSYNC_POL(pp->sync_pol));

	rk628_post_process_scaler_init(pp, src, dst);
}

static void rk628_post_process_bridge_post_disable(struct drm_bridge *bridge)
{

}

static void rk628_post_process_bridge_enable(struct drm_bridge *bridge)
{
	struct rk628_post_process *pp = bridge_to_pp(bridge);

	regmap_write(pp->grf, GRF_SCALER_CON0, SCL_EN(1));
}

static void rk628_post_process_bridge_disable(struct drm_bridge *bridge)
{
	struct rk628_post_process *pp = bridge_to_pp(bridge);

	regmap_write(pp->grf, GRF_SCALER_CON0, SCL_EN(0));

	clk_disable_unprepare(pp->sclk_vop);
	clk_disable_unprepare(pp->clk_rx_read);
}

static void rk628_post_process_bridge_mode_set(struct drm_bridge *bridge,
					       const struct drm_display_mode *mode,
					       const struct drm_display_mode *adj)
{
	struct rk628_post_process *pp = bridge_to_pp(bridge);
	struct rk628 *rk628 = pp->parent;

	drm_mode_copy(&pp->src_mode, adj);

	if (rk628->dst_mode_valid)
		drm_mode_copy(&pp->dst_mode, &rk628->dst_mode);
	else
		drm_mode_copy(&pp->dst_mode, &pp->src_mode);

	/* hdmirx 4k-60Hz mode only support yuv420 */
	if (pp->src_mode.clock == 594000)
		regmap_write(pp->grf, GRF_CSC_CTRL_CON, SW_Y2R_EN(1));
}

static int rk628_post_process_bridge_attach(struct drm_bridge *bridge,
					    enum drm_bridge_attach_flags flags)
{
	struct rk628_post_process *pp = bridge_to_pp(bridge);
	struct device *dev = pp->dev;
	int ret;

	ret = drm_of_find_panel_or_bridge(dev->of_node, 1, -1,
					  NULL, &pp->bridge);
	if (ret)
		return ret;

	ret = drm_bridge_attach(bridge->encoder, pp->bridge, bridge, flags);
	if (ret) {
		dev_err(dev, "failed to attach bridge\n");
		return ret;
	}

	return 0;
}

static bool
rk628_post_process_bridge_mode_fixup(struct drm_bridge *bridge,
				     const struct drm_display_mode *mode,
				     struct drm_display_mode *adj)
{
	struct rk628_post_process *pp = bridge_to_pp(bridge);

	if (pp->sync_pol == MODE_FLAG_NSYNC) {
		adj->flags &= ~(DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC);
		adj->flags |= (DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC);
	} else {
		adj->flags &= ~(DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC);
		adj->flags |= (DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC);
	}

	return true;
}

static const struct drm_bridge_funcs rk628_post_process_bridge_funcs = {
	.pre_enable = rk628_post_process_bridge_pre_enable,
	.post_disable = rk628_post_process_bridge_post_disable,
	.enable = rk628_post_process_bridge_enable,
	.disable = rk628_post_process_bridge_disable,
	.mode_set = rk628_post_process_bridge_mode_set,
	.mode_fixup = rk628_post_process_bridge_mode_fixup,
	.attach = rk628_post_process_bridge_attach,
};


/**
 * rk628_scaler_add_src_mode - add source mode for scaler
 * @rk628: parent device
 * @connector: DRM connector
 * If need scale, call the function at last of get_modes.
 */
int rk628_scaler_add_src_mode(struct rk628 *rk628,
			      struct drm_connector *connector)
{
	struct drm_display_mode *pmode;
	struct drm_display_mode *dst;

	if (!rk628 || !connector)
		return 0;

	if (drm_mode_validate_driver(connector->dev, &rk628->src_mode) !=
	    MODE_OK)
		return 0;

	list_for_each_entry(pmode, &connector->probed_modes, head) {
		if (pmode->type & DRM_MODE_TYPE_PREFERRED) {
			drm_mode_copy(&rk628->dst_mode, pmode);
			drm_mode_copy(pmode, &rk628->src_mode);
			pmode->type |= DRM_MODE_TYPE_PREFERRED;
			rk628->dst_mode_valid = true;
			break;
		}
	}
	if (rk628->dst_mode_valid) {
		dst = drm_mode_duplicate(connector->dev, &rk628->dst_mode);
		dst->type &= ~DRM_MODE_TYPE_PREFERRED;
		drm_mode_probed_add(connector, dst);
		return 1;
	}

	return 0;
}
EXPORT_SYMBOL(rk628_scaler_add_src_mode);

/**
 * rk628_mode_copy - rk628 mode copy
 * @rk628: parent device
 * @dst: dst mode
 * @src: src mode
 * Call the function at mode_set, replace drm_mode_copy.
 */
void rk628_mode_copy(struct rk628 *rk628, struct drm_display_mode *dst,
		     const struct drm_display_mode *src)
{
	if (rk628->dst_mode_valid)
		drm_mode_copy(dst, &rk628->dst_mode);
	else
		drm_mode_copy(dst, src);
}
EXPORT_SYMBOL(rk628_mode_copy);

static int rk628_post_process_probe(struct platform_device *pdev)
{
	struct rk628 *rk628 = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct rk628_post_process *pp;
	u32 bus_flags;
	u32 val;
	int ret;

	if (!of_device_is_available(dev->of_node))
		return -ENODEV;

	pp = devm_kzalloc(dev, sizeof(*pp), GFP_KERNEL);
	if (!pp)
		return -ENOMEM;

	pp->dev = dev;
	pp->grf = rk628->grf;
	platform_set_drvdata(pdev, pp);
	pp->parent = rk628;

	pp->sclk_vop = devm_clk_get(dev, "sclk_vop");
	if (IS_ERR(pp->sclk_vop)) {
		ret = PTR_ERR(pp->sclk_vop);
		dev_err(dev, "failed to get sclk: %d\n", ret);
		return ret;
	}

	pp->clk_rx_read = devm_clk_get(dev, "rx_read");
	if (IS_ERR(pp->clk_rx_read)) {
		ret = PTR_ERR(pp->clk_rx_read);
		dev_err(dev, "failed to get clk_rx_read: %d\n", ret);
		return ret;
	}

	pp->rstc_decoder = of_reset_control_get(dev->of_node, "decoder");
	if (IS_ERR(pp->rstc_decoder)) {
		ret = PTR_ERR(pp->rstc_decoder);
		dev_err(dev, "failed to get decoder reset: %d\n", ret);
		return ret;
	}

	pp->rstc_clk_rx = of_reset_control_get(dev->of_node, "clk_rx");
	if (IS_ERR(pp->rstc_clk_rx)) {
		ret = PTR_ERR(pp->rstc_clk_rx);
		dev_err(dev, "failed to get clk_rx reset: %d\n", ret);
		return ret;
	}

	pp->rstc_vop = of_reset_control_get(dev->of_node, "vop");
	if (IS_ERR(pp->rstc_vop)) {
		ret = PTR_ERR(pp->rstc_vop);
		dev_err(dev, "failed to get vop reset: %d\n", ret);
		return ret;
	}

	ret = of_property_read_u32(dev->of_node, "mode-sync-pol", &val);
	if (ret < 0)
		pp->sync_pol = MODE_FLAG_PSYNC;
	else
		pp->sync_pol = (!val ? MODE_FLAG_NSYNC : MODE_FLAG_PSYNC);

	pp->base.funcs = &rk628_post_process_bridge_funcs;
	pp->base.of_node = dev->of_node;
	drm_bridge_add(&pp->base);

	of_get_drm_display_mode(dev->of_node, &rk628->src_mode, &bus_flags,
				OF_USE_NATIVE_MODE);

	return 0;
}

static int rk628_post_process_remove(struct platform_device *pdev)
{
	struct rk628_post_process *pp = platform_get_drvdata(pdev);

	drm_bridge_remove(&pp->base);

	return 0;
}

static const struct of_device_id rk628_post_process_of_match[] = {
	{ .compatible = "rockchip,rk628-post-process", },
	{},
};
MODULE_DEVICE_TABLE(of, rk628_post_process_of_match);

static struct platform_driver rk628_post_process_driver = {
	.driver = {
		.name = "rk628-post-process",
		.of_match_table = of_match_ptr(rk628_post_process_of_match),
	},
	.probe = rk628_post_process_probe,
	.remove = rk628_post_process_remove,
};
module_platform_driver(rk628_post_process_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK628 Post Process driver");
MODULE_LICENSE("GPL v2");
