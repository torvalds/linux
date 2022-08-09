// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017 Rockchip Electronics Co. Ltd.
 *
 * Author: Wyon Bi <bivvy.bi@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/mfd/rk618.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>

#include <drm/drm_of.h>
#include <drm/drm_encoder.h>
#include <drm/drm_print.h>
#include <video/videomode.h>

#define RK618_SCALER_REG0		0x0030
#define SCL_VER_DOWN_MODE(x)		HIWORD_UPDATE(x, 8, 8)
#define SCL_HOR_DOWN_MODE(x)		HIWORD_UPDATE(x, 7, 7)
#define SCL_BIC_COE_SEL(x)		HIWORD_UPDATE(x, 6, 5)
#define SCL_VER_MODE(x)			HIWORD_UPDATE(x, 4, 3)
#define SCL_HOR_MODE(x)			HIWORD_UPDATE(x, 2, 1)
#define SCL_ENABLE			HIWORD_UPDATE(1, 0, 0)
#define SCL_DISABLE			HIWORD_UPDATE(0, 0, 0)
#define RK618_SCALER_REG1		0x0034
#define SCL_V_FACTOR(x)			UPDATE(x, 31, 16)
#define SCL_H_FACTOR(x)			UPDATE(x, 15, 0)
#define RK618_SCALER_REG2		0x0038
#define DSP_FRAME_VST(x)		UPDATE(x, 27, 16)
#define DSP_FRAME_HST(x)		UPDATE(x, 11, 0)
#define RK618_SCALER_REG3		0x003c
#define DSP_HS_END(x)			UPDATE(x, 23, 16)
#define DSP_HTOTAL(x)			UPDATE(x, 11, 0)
#define RK618_SCALER_REG4		0x0040
#define DSP_HACT_END(x)			UPDATE(x, 27, 16)
#define DSP_HACT_ST(x)			UPDATE(x, 11, 0)
#define RK618_SCALER_REG5		0x0044
#define DSP_VS_END(x)			UPDATE(x, 23, 16)
#define DSP_VTOTAL(x)			UPDATE(x, 11, 0)
#define RK618_SCALER_REG6		0x0048
#define DSP_VACT_END(x)			UPDATE(x, 27, 16)
#define DSP_VACT_ST(x)			UPDATE(x, 11, 0)
#define RK618_SCALER_REG7		0x004c
#define DSP_HBOR_END(x)			UPDATE(x, 27, 16)
#define DSP_HBOR_ST(x)			UPDATE(x, 11, 0)
#define RK618_SCALER_REG8		0x0050
#define DSP_VBOR_END(x)			UPDATE(x, 27, 16)
#define DSP_VBOR_ST(x)			UPDATE(x, 11, 0)

struct rk618_scaler {
	struct drm_bridge base;
	struct drm_bridge *bridge;
	struct drm_display_mode src;
	struct drm_display_mode dst;
	struct device *dev;
	struct regmap *regmap;
	struct clk *vif_clk;
	struct clk *dither_clk;
	struct clk *scaler_clk;
};

static inline struct rk618_scaler *bridge_to_scaler(struct drm_bridge *bridge)
{
	return container_of(bridge, struct rk618_scaler, base);
}

static void rk618_scaler_enable(struct rk618_scaler *scl)
{
	regmap_write(scl->regmap, RK618_SCALER_REG0, SCL_ENABLE);
}

static void rk618_scaler_disable(struct rk618_scaler *scl)
{
	regmap_write(scl->regmap, RK618_SCALER_REG0, SCL_DISABLE);
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

static void rk618_scaler_init(struct rk618_scaler *scl,
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
	dev_dbg(scl->dev, "dsp_frame_vst=%d, dsp_frame_hst=%d\n",
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

		dev_dbg(scl->dev, "horizontal scale down\n");
	} else if (src.hactive == dst.hactive) {
		scl_hor_mode = 0;
		scl_h_factor = 0;

		dev_dbg(scl->dev, "horizontal no scale\n");
	} else {
		scl_hor_mode = 1;
		scl_h_factor = ((src.hactive - 1) << 16) / (dst.hactive - 1);

		dev_dbg(scl->dev, "horizontal scale up\n");
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

		dev_dbg(scl->dev, "vertical scale down\n");
	} else if (src.vactive == dst.vactive) {
		scl_ver_mode = 0;
		scl_v_factor = 0;

		dev_dbg(scl->dev, "vertical no scale\n");
	} else {
		scl_ver_mode = 1;
		scl_v_factor = ((src.vactive - 1) << 16) / (dst.vactive - 1);

		dev_dbg(scl->dev, "vertical scale up\n");
	}

	regmap_write(scl->regmap, RK618_SCALER_REG0,
		     SCL_VER_MODE(scl_ver_mode) | SCL_HOR_MODE(scl_hor_mode));
	regmap_write(scl->regmap, RK618_SCALER_REG1,
		     SCL_V_FACTOR(scl_v_factor) | SCL_H_FACTOR(scl_h_factor));
	regmap_write(scl->regmap, RK618_SCALER_REG2,
		     DSP_FRAME_VST(dsp_frame_vst) |
		     DSP_FRAME_HST(dsp_frame_hst));
	regmap_write(scl->regmap, RK618_SCALER_REG3,
		     DSP_HS_END(dsp_hs_end) | DSP_HTOTAL(dsp_htotal));
	regmap_write(scl->regmap, RK618_SCALER_REG4,
		     DSP_HACT_END(dsp_hact_end) | DSP_HACT_ST(dsp_hact_st));
	regmap_write(scl->regmap, RK618_SCALER_REG5,
		     DSP_VS_END(dsp_vs_end) | DSP_VTOTAL(dsp_vtotal));
	regmap_write(scl->regmap, RK618_SCALER_REG6,
		     DSP_VACT_END(dsp_vact_end) | DSP_VACT_ST(dsp_vact_st));
	regmap_write(scl->regmap, RK618_SCALER_REG7,
		     DSP_HBOR_END(dsp_hbor_end) | DSP_HBOR_ST(dsp_hbor_st));
	regmap_write(scl->regmap, RK618_SCALER_REG8,
		     DSP_VBOR_END(dsp_vbor_end) | DSP_VBOR_ST(dsp_vbor_st));
}

static void rk618_scaler_bridge_enable(struct drm_bridge *bridge)
{
	struct rk618_scaler *scl = bridge_to_scaler(bridge);
	struct drm_display_mode *src = &scl->src;
	struct drm_display_mode *dst = &scl->dst;
	long rate;

	clk_set_parent(scl->dither_clk, scl->scaler_clk);

	rate = clk_round_rate(scl->scaler_clk, dst->clock * 1000);
	clk_set_rate(scl->scaler_clk, rate);
	clk_prepare_enable(scl->scaler_clk);

	rk618_scaler_init(scl, src, dst);
	rk618_scaler_enable(scl);
}

static void rk618_scaler_bridge_disable(struct drm_bridge *bridge)
{
	struct rk618_scaler *scl = bridge_to_scaler(bridge);

	rk618_scaler_disable(scl);
	clk_disable_unprepare(scl->scaler_clk);
	clk_set_parent(scl->dither_clk, scl->vif_clk);
}

static void rk618_scaler_bridge_mode_set(struct drm_bridge *bridge,
					 const struct drm_display_mode *mode,
					 const struct drm_display_mode *adjusted)
{
	struct rk618_scaler *scl = bridge_to_scaler(bridge);
	struct drm_connector *connector;
	struct drm_display_mode *src = &scl->src;
	struct drm_display_mode *dst = &scl->dst;
	unsigned long dclk_rate;
	u64 sclk_rate;
	struct drm_connector_list_iter conn_iter;

	drm_mode_copy(&scl->src, adjusted);

	drm_connector_list_iter_begin(bridge->dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		const struct drm_display_mode *mode;

		if (connector->connector_type == DRM_MODE_CONNECTOR_HDMIA)
			continue;

		if (!drm_connector_has_possible_encoder(connector, bridge->encoder))
			continue;

		list_for_each_entry(mode, &connector->modes, head) {
			if (mode->type & DRM_MODE_TYPE_PREFERRED) {
				drm_mode_copy(&scl->dst, mode);
				break;
			}
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	dclk_rate = src->clock * 1000;
	sclk_rate = (u64)dclk_rate * dst->vdisplay * dst->htotal;
	do_div(sclk_rate, src->vdisplay * src->htotal);
	sclk_rate = div_u64(sclk_rate, 1000);
	dst->clock = sclk_rate;
	sclk_rate = sclk_rate * 1000;
	scl->bridge->driver_private = dst;

	DRM_DEV_INFO(scl->dev, "src=%s, dst=%s\n", src->name, dst->name);
	DRM_DEV_INFO(scl->dev, "dclk rate: %ld, sclk rate: %lld\n",
		     dclk_rate, sclk_rate);
}

static int rk618_scaler_bridge_attach(struct drm_bridge *bridge,
				      enum drm_bridge_attach_flags flags)
{
	struct rk618_scaler *scl = bridge_to_scaler(bridge);
	struct device *dev = scl->dev;
	struct device_node *endpoint;
	int ret;

	endpoint = of_graph_get_endpoint_by_regs(dev->of_node, 1, -1);
	if (endpoint && of_device_is_available(endpoint)) {
		struct device_node *remote;

		remote = of_graph_get_remote_port_parent(endpoint);
		of_node_put(endpoint);
		if (!remote || !of_device_is_available(remote))
			return -ENODEV;

		scl->bridge = of_drm_find_bridge(remote);
		of_node_put(remote);
		if (!scl->bridge)
			return -EPROBE_DEFER;

		ret = drm_bridge_attach(bridge->encoder, scl->bridge, bridge, 0);
		if (ret) {
			dev_err(dev, "failed to attach bridge\n");
			return ret;
		}
	}

	return 0;
}

static const struct drm_bridge_funcs rk618_scaler_bridge_funcs = {
	.enable = rk618_scaler_bridge_enable,
	.disable = rk618_scaler_bridge_disable,
	.mode_set = rk618_scaler_bridge_mode_set,
	.attach = rk618_scaler_bridge_attach,
};

static int rk618_scaler_probe(struct platform_device *pdev)
{
	struct rk618 *rk618 = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct rk618_scaler *scl;
	int ret;

	if (!of_device_is_available(dev->of_node))
		return -ENODEV;

	scl = devm_kzalloc(dev, sizeof(*scl), GFP_KERNEL);
	if (!scl)
		return -ENOMEM;

	scl->dev = dev;
	scl->regmap = rk618->regmap;
	platform_set_drvdata(pdev, scl);

	scl->vif_clk = devm_clk_get(dev, "vif");
	if (IS_ERR(scl->vif_clk)) {
		ret = PTR_ERR(scl->vif_clk);
		dev_err(dev, "failed to get vif clock: %d\n", ret);
		return ret;
	}

	scl->dither_clk = devm_clk_get(dev, "dither");
	if (IS_ERR(scl->dither_clk)) {
		ret = PTR_ERR(scl->dither_clk);
		dev_err(dev, "failed to get dither clock: %d\n", ret);
		return ret;
	}

	scl->scaler_clk = devm_clk_get(dev, "scaler");
	if (IS_ERR(scl->scaler_clk)) {
		ret = PTR_ERR(scl->scaler_clk);
		dev_err(dev, "failed to get scaler clock: %d\n", ret);
		return ret;
	}

	scl->base.funcs = &rk618_scaler_bridge_funcs;
	scl->base.of_node = dev->of_node;
	drm_bridge_add(&scl->base);

	return 0;
}

static int rk618_scaler_remove(struct platform_device *pdev)
{
	struct rk618_scaler *scl = platform_get_drvdata(pdev);

	drm_bridge_remove(&scl->base);

	return 0;
}

static const struct of_device_id rk618_scaler_of_match[] = {
	{ .compatible = "rockchip,rk618-scaler", },
	{},
};
MODULE_DEVICE_TABLE(of, rk618_scaler_of_match);

static struct platform_driver rk618_scaler_driver = {
	.driver = {
		.name = "rk618-scaler",
		.of_match_table = of_match_ptr(rk618_scaler_of_match),
	},
	.probe = rk618_scaler_probe,
	.remove = rk618_scaler_remove,
};
module_platform_driver(rk618_scaler_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK618 SCALER driver");
MODULE_LICENSE("GPL v2");
