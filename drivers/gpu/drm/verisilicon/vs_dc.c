// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#include <linux/component.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/media-bus-format.h>

#include <drm/drm_framebuffer.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/vs_drm.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>

#include "vs_type.h"
#include "vs_dc_hw.h"
#include "vs_dc.h"
#include "vs_crtc.h"
#include "vs_drv.h"

#include <soc/starfive/vic7100.h>

#if KERNEL_VERSION(5, 5, 0) <= LINUX_VERSION_CODE
#include <drm/drm_fourcc.h>
#include <drm/drm_vblank.h>
#endif

//syscon panel
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
//syscon panel

static inline void update_format(u32 format, u64 mod, struct dc_hw_fb *fb)
{
	u8 f = FORMAT_A8R8G8B8;

	switch (format) {
	case DRM_FORMAT_XRGB4444:
	case DRM_FORMAT_RGBX4444:
	case DRM_FORMAT_XBGR4444:
	case DRM_FORMAT_BGRX4444:
		f = FORMAT_X4R4G4B4;
		break;
	case DRM_FORMAT_ARGB4444:
	case DRM_FORMAT_RGBA4444:
	case DRM_FORMAT_ABGR4444:
	case DRM_FORMAT_BGRA4444:
		f = FORMAT_A4R4G4B4;
		break;
	case DRM_FORMAT_XRGB1555:
	case DRM_FORMAT_RGBX5551:
	case DRM_FORMAT_XBGR1555:
	case DRM_FORMAT_BGRX5551:
		f = FORMAT_X1R5G5B5;
		break;
	case DRM_FORMAT_ARGB1555:
	case DRM_FORMAT_RGBA5551:
	case DRM_FORMAT_ABGR1555:
	case DRM_FORMAT_BGRA5551:
		f = FORMAT_A1R5G5B5;
		break;
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
		f = FORMAT_R5G6B5;
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_BGRX8888:
		f = FORMAT_X8R8G8B8;
		break;
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_BGRA8888:
		f = FORMAT_A8R8G8B8;
		break;
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
		f = FORMAT_YUY2;
		break;
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
		f = FORMAT_UYVY;
		break;
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
		f = FORMAT_YV12;
		break;
	case DRM_FORMAT_NV21:
		f = FORMAT_NV12;
		break;
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV61:
		f = FORMAT_NV16;
		break;
	case DRM_FORMAT_P010:
		f = FORMAT_P010;
		break;
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_RGBA1010102:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_BGRA1010102:
		f = FORMAT_A2R10G10B10;
		break;
	case DRM_FORMAT_NV12:
		if (fourcc_mod_vs_get_type(mod) ==
			DRM_FORMAT_MOD_VS_TYPE_CUSTOM_10BIT)
			f = FORMAT_NV12_10BIT;
		else
			f = FORMAT_NV12;
		break;
	case DRM_FORMAT_YUV444:
		if (fourcc_mod_vs_get_type(mod) ==
			DRM_FORMAT_MOD_VS_TYPE_CUSTOM_10BIT)
			f = FORMAT_YUV444_10BIT;
		else
			f = FORMAT_YUV444;
		break;
	default:
		break;
	}

	fb->format = f;
}

static inline void update_swizzle(u32 format, struct dc_hw_fb *fb)
{
	fb->swizzle = SWIZZLE_ARGB;
	fb->uv_swizzle = 0;

	switch (format) {
	case DRM_FORMAT_RGBX4444:
	case DRM_FORMAT_RGBA4444:
	case DRM_FORMAT_RGBX5551:
	case DRM_FORMAT_RGBA5551:
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_RGBA1010102:
		fb->swizzle = SWIZZLE_RGBA;
		break;
	case DRM_FORMAT_XBGR4444:
	case DRM_FORMAT_ABGR4444:
	case DRM_FORMAT_XBGR1555:
	case DRM_FORMAT_ABGR1555:
	case DRM_FORMAT_BGR565:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_ABGR2101010:
		fb->swizzle = SWIZZLE_ABGR;
		break;
	case DRM_FORMAT_BGRX4444:
	case DRM_FORMAT_BGRA4444:
	case DRM_FORMAT_BGRX5551:
	case DRM_FORMAT_BGRA5551:
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_BGRA1010102:
		fb->swizzle = SWIZZLE_BGRA;
		break;
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV61:
		fb->uv_swizzle = 1;
		break;
	default:
		break;
	}
}

static inline void update_watermark(struct drm_property_blob *watermark,
									struct dc_hw_fb *fb)
{
	struct drm_vs_watermark *data;

	fb->water_mark = 0;

	if (watermark) {
		data = watermark->data;
		fb->water_mark = data->watermark & 0xFFFFF;
	}
}

static inline u8 to_vs_rotation(unsigned int rotation)
{
	u8 rot;

	switch (rotation & DRM_MODE_REFLECT_MASK) {
	case DRM_MODE_REFLECT_X:
		rot = FLIP_X;
		return rot;
	case DRM_MODE_REFLECT_Y:
		rot = FLIP_Y;
		return rot;
	case DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y:
		rot = FLIP_XY;
		return rot;
	default:
		break;
	}

	switch (rotation & DRM_MODE_ROTATE_MASK) {
	case DRM_MODE_ROTATE_0:
		rot = ROT_0;
		break;
	case DRM_MODE_ROTATE_90:
		rot = ROT_90;
		break;
	case DRM_MODE_ROTATE_180:
		rot = ROT_180;
		break;
	case DRM_MODE_ROTATE_270:
		rot = ROT_270;
		break;
	default:
		rot = ROT_0;
		break;
	}

	return rot;
}

static inline u8 to_vs_yuv_color_space(u32 color_space)
{
	u8 cs;

	switch (color_space) {
	case DRM_COLOR_YCBCR_BT601:
		cs = COLOR_SPACE_601;
		break;
	case DRM_COLOR_YCBCR_BT709:
		cs = COLOR_SPACE_709;
		break;
	case DRM_COLOR_YCBCR_BT2020:
		cs = COLOR_SPACE_2020;
		break;
	default:
		cs = COLOR_SPACE_601;
		break;
	}

	return cs;
}

static inline u8 to_vs_tile_mode(u64 modifier)
{
	return (u8)(modifier & DRM_FORMAT_MOD_VS_NORM_MODE_MASK);
}

static inline u8 to_vs_display_id(struct vs_dc *dc, struct drm_crtc *crtc)
{
	u8 panel_num = dc->hw.info->panel_num;
	u32 index = drm_crtc_index(crtc);
	int i;

	for (i = 0; i < panel_num; i++) {
		if (index == dc->crtc[i]->base.index)
			return i;
	}

	return 0;
}
#if 0
static int plda_clk_rst_init(struct device *dev)
{
	int ret;
	struct vs_dc *dc = dev_get_drvdata(dev);

	dc->num_clks = devm_clk_bulk_get_all(dev, &dc->clks);
	if (dc->num_clks < 0) {
		dev_err(dev, "failed to get vout clocks\n");
		ret = -ENODEV;
		goto exit;
	}
	ret = clk_bulk_prepare_enable(dc->num_clks, dc->clks);
	if (ret) {
		dev_err(dev, "failed to enable clocks\n");
		goto exit;
	}

	dc->resets = devm_reset_control_array_get_exclusive(dev);
	if (IS_ERR(dc->resets)) {
		ret = PTR_ERR(dc->resets);
		dev_err(dev, "failed to get pcie resets");
		goto err_clk_init;
	}
	ret = reset_control_deassert(dc->resets);
	goto exit;

err_clk_init:
	clk_bulk_disable_unprepare(dc->num_clks, dc->clks);
exit:
	return ret;
}

static void plda_clk_rst_deinit(struct device *dev)
{
	struct vs_dc *dc = dev_get_drvdata(dev);

	reset_control_assert(dc->resets);
	clk_bulk_disable_unprepare(dc->num_clks, dc->clks);
}
#endif

static int vs_dc_get_clock(struct device *dev, struct vs_dc *dc)
{
	dc->disp_axi = devm_clk_get(dev, "noc_disp");
	if (IS_ERR(dc->disp_axi)) {
		dev_err(dev, "---disp_axi get error\n");
		return PTR_ERR(dc->disp_axi);
	}

	return 0;
}

static int  vs_dc_clock_enable(struct device *dev, struct vs_dc *dc)
{
	int ret;
	ret = clk_prepare_enable(dc->disp_axi);
	if (ret) {
		dev_err(dev, "failed to prepare/enable disp_axi\n");
		return ret;
	}
	return 0;
}

static void  vs_dc_clock_disable(struct vs_dc *dc)
{
	clk_disable_unprepare(dc->disp_axi);
}

static int vs_dc_vouttop_get_clock(struct device *dev, struct vs_dc *dc)
{
	dc->vout_src = devm_clk_get(dev, "vout_src");
	if (IS_ERR(dc->vout_src)) {
		dev_err(dev, "failed to get vout_src\n");
		return PTR_ERR(dc->vout_src);
	}

	dc->vout_axi = devm_clk_get(dev, "top_vout_axi");
	if (IS_ERR(dc->vout_axi)) {
		dev_err(dev, "failed to get vout_axi\n");
		return PTR_ERR(dc->vout_axi);
	}
	dc->vout_ahb = devm_clk_get(dev, "top_vout_ahb");
	if (IS_ERR(dc->vout_ahb)) {
		dev_err(dev, "failed to get vout_ahb\n");
		return PTR_ERR(dc->vout_ahb);
	}
	return 0;
}

static int  vs_dc_vouttop_clock_enable(struct device *dev, struct vs_dc *dc)
{
	int ret;
	/*clk_prepare_enable(dc->sys_clk);*/
	ret = clk_prepare_enable(dc->vout_src);
	if (ret) {
		dev_err(dev, "failed to prepare/enable vout_src\n");
		return ret;
	}
	ret = clk_prepare_enable(dc->vout_axi);
	if (ret) {
		dev_err(dev, "failed to prepare/enable vout_axi\n");
		return ret;
	}
	return ret;
}

static void  vs_dc_vouttop_clock_disable(struct vs_dc *dc)
{
	clk_disable_unprepare(dc->vout_src);
	clk_disable_unprepare(dc->vout_axi);
}

static int vs_dc_dc8200_get_clock(struct device *dev, struct vs_dc *dc)
{
	dc->dc8200_clk_pix0 = devm_clk_get(dev, "pix_clk");
	if (IS_ERR(dc->dc8200_clk_pix0)) {
		dev_err(dev, "---dc8200_clk_pix0 get error\n");
		return PTR_ERR(dc->dc8200_clk_pix0);
	}

	dc->dc8200_clk_pix1 = devm_clk_get(dev, "vout_pix1");
	if (IS_ERR(dc->dc8200_clk_pix1)) {
		dev_err(dev, "---dc8200_clk_pix1 get error\n");
		return PTR_ERR(dc->dc8200_clk_pix1);
	}

	dc->dc8200_axi = devm_clk_get(dev, "axi_clk");
	if (IS_ERR(dc->dc8200_axi)) {
		dev_err(dev, "---dc8200_axi get error\n");
		return PTR_ERR(dc->dc8200_axi);
	}

	dc->dc8200_core = devm_clk_get(dev, "core_clk");
	if (IS_ERR(dc->dc8200_core)) {
		dev_err(dev, "---dc8200_core get error\n");
		return PTR_ERR(dc->dc8200_core);
	}

	dc->dc8200_ahb = devm_clk_get(dev, "vout_ahb");
	if (IS_ERR(dc->dc8200_ahb)) {
		dev_err(dev, "---dc8200_ahb get error\n");
		return PTR_ERR(dc->dc8200_ahb);
	}
	return 0;
}

static int  vs_dc_dc8200_clock_enable(struct device *dev, struct vs_dc *dc)
{
	int ret;
	/*clk_prepare_enable(dc->sys_clk);*/
	ret = clk_prepare_enable(dc->dc8200_clk_pix0);
	if (ret) {
		dev_err(dev, "failed to prepare/enable dc8200_clk_pix0\n");
		return ret;
	}
	ret = clk_prepare_enable(dc->dc8200_clk_pix1);
	if (ret) {
		dev_err(dev, "failed to prepare/enable dc8200_clk_pix1\n");
		return ret;
	}
	ret = clk_prepare_enable(dc->dc8200_axi);
	if (ret) {
		dev_err(dev, "failed to prepare/enable dc8200_axi\n");
		return ret;
	}
	ret = clk_prepare_enable(dc->dc8200_core);
	if (ret) {
		dev_err(dev, "failed to prepare/enable dc8200_core\n");
		return ret;
	}
	ret = clk_prepare_enable(dc->dc8200_ahb);
	if (ret) {
		dev_err(dev, "failed to prepare/enable dc8200_ahb\n");
		return ret;
	}

	return ret;
}

static void  vs_dc_dc8200_clock_disable(struct vs_dc *dc)
{
	clk_disable_unprepare(dc->dc8200_clk_pix0);
	clk_disable_unprepare(dc->dc8200_clk_pix1);
	clk_disable_unprepare(dc->dc8200_axi);
	clk_disable_unprepare(dc->dc8200_core);
	clk_disable_unprepare(dc->dc8200_ahb);
}

static void vs_vout_reset_get(struct device *dev, struct vs_dc *dc)
{
	//dc->rst_vout_src=reset_control_get_shared(dev, "rst_vout_src");
	dc->rst_vout_src = reset_control_get_shared(dev, "rst_vout_src");
	if (IS_ERR(dc->rst_vout_src))
		dev_err(dev, "failed to get rst_vout_src\n");
	dc->noc_disp = reset_control_get_shared(dev, "rst_noc_disp");
	if (IS_ERR(dc->noc_disp))
		dev_err(dev, "failed to get rst_noc_disp\n");
}

static void vs_vout_reset_deassert(struct vs_dc *dc)
{
	reset_control_deassert(dc->rst_vout_src);//no!
	reset_control_deassert(dc->noc_disp);//ok
}

/*
static void vs_vout_reset_assert(struct vs_dc *dc)
{
	reset_control_assert(dc->rst_vout_src);//no!
	reset_control_assert(dc->noc_disp);//ok
}
*/

static void vs_dc8200_reset_get(struct device *dev, struct vs_dc *dc)
{
	dc->dc8200_rst_axi = reset_control_get_shared(dev, "rst_axi");
	if (IS_ERR(dc->dc8200_rst_axi))
		dev_err(dev, "failed to get dc8200_rst_axi\n");
	dc->dc8200_rst_core = reset_control_get_shared(dev, "rst_ahb");
	if (IS_ERR(dc->dc8200_rst_core))
		dev_err(dev, "failed to get dc8200_rst_core\n");
	dc->dc8200_rst_ahb = reset_control_get_shared(dev, "rst_core");
	if (IS_ERR(dc->dc8200_rst_core))
		dev_err(dev, "failed to get dc8200_rst_core\n");
}

static void vs_dc8200_reset_deassert(struct vs_dc *dc)
{
	reset_control_deassert(dc->dc8200_rst_axi);
	reset_control_deassert(dc->dc8200_rst_core);//ok
	reset_control_deassert(dc->dc8200_rst_ahb);
}

static void vs_dc8200_reset_assert(struct vs_dc *dc)
{
	reset_control_assert(dc->dc8200_rst_axi);
	reset_control_assert(dc->dc8200_rst_core);//ok
	reset_control_assert(dc->dc8200_rst_ahb);
}

static int dc_vout_clk_get(struct device *dev, struct vs_dc *dc)
{
	int ret;
	ret = vs_dc_get_clock(dev, dc);
	if (ret) {
		dev_err(dev, "failed to get clock\n");
		return ret;
	}
	ret = vs_dc_vouttop_get_clock(dev, dc);
	if (ret) {
		dev_err(dev, "failed to get clock\n");
		return ret;
	}
	ret = vs_dc_dc8200_get_clock(dev, dc);
	if (ret) {
		dev_err(dev, "failed to get clock\n");
		return ret;
	}

	return 0;
}

static int dc_vout_clk_enable(struct device *dev, struct vs_dc *dc)
{
	int ret;

	ret = vs_dc_clock_enable(dev, dc);
	if (ret) {
		dev_err(dev, "failed to enable clock\n");
		return ret;
	}

	ret = vs_dc_vouttop_clock_enable(dev, dc);
	if (ret) {
		dev_err(dev, "failed to enable clock\n");
		return ret;
	}

	ret = vs_dc_dc8200_clock_enable(dev, dc);
	if (ret) {
		dev_err(dev, "failed to enable clock\n");
		return ret;
	}

	return 0;
}

static int syscon_panel_parse_dt(struct device *dev)
{
	struct vs_dc *dc = dev_get_drvdata(dev);
	int ret = 0;

	dc->dss_regmap = syscon_regmap_lookup_by_phandle(dev->of_node,
						"verisilicon,dss-syscon");

	if (IS_ERR(dc->dss_regmap)) {
		if (PTR_ERR(dc->dss_regmap) != -ENODEV) {
			dev_err(dev, "failed to get dss-syscon\n");
			ret = PTR_ERR(dc->dss_regmap);
			goto err;
		}
		dc->dss_regmap = NULL;
		goto err;
	}

err:
	return ret;
}

int sys_dispctrl_clk_standard(struct vs_dc *dc, struct device *dev)
{
	dc->dc8200_clk_pix1 = devm_clk_get(dev, "vout_pix1");
	if (IS_ERR(dc->dc8200_clk_pix1)) {
		dev_err(dev, "---dc8200_clk_pix1 get error\n");
		return PTR_ERR(dc->dc8200_clk_pix1);
	}

	dc->hdmitx0_pixelclk = devm_clk_get(dev, "hdmitx0_pixelclk");
	if (IS_ERR(dc->hdmitx0_pixelclk)) {
		dev_err(dev, "---hdmitx0_pixelclk get error\n");
		return PTR_ERR(dc->hdmitx0_pixelclk);
	}

	dc->dc8200_clk_pix0 = devm_clk_get(dev, "pix_clk");
	if (IS_ERR(dc->dc8200_clk_pix0)) {
		dev_err(dev, "---dc8200_clk_pix0 get error\n");
		return PTR_ERR(dc->dc8200_clk_pix0);
	}

	dc->dc8200_clk_pix0 = devm_clk_get(dev, "pix_clk");	//dc8200_clk_pix0
	if (IS_ERR(dc->dc8200_clk_pix0)) {
		dev_err(dev, "---dc8200_clk_pix0 get error\n");
		return PTR_ERR(dc->dc8200_clk_pix0);
	}

	dc->hdmitx0_pixelclk = devm_clk_get(dev, "hdmitx0_pixelclk");//hdmitx0_pixelclk
	if (IS_ERR(dc->hdmitx0_pixelclk)) {
		dev_err(dev, "---hdmitx0_pixelclk get error\n");
		return PTR_ERR(dc->hdmitx0_pixelclk);
	}

	dc->vout_src = devm_clk_get(dev, "vout_src");
	if (IS_ERR(dc->vout_src)){
		dev_err(dev,"failed to get dc->vout_src\n");
		return PTR_ERR(dc->vout_src);
	}

	dc->vout_top_lcd = devm_clk_get(dev, "vout_top_lcd");
	if (IS_ERR(dc->vout_top_lcd)){
		dev_err(dev,"failed to get dc->vout_top_lcd\n");
		return PTR_ERR(dc->vout_top_lcd);
	}

	dc->dc8200_pix0 = devm_clk_get(dev, "dc8200_pix0");	//dc8200_pix0
	if (IS_ERR(dc->dc8200_pix0)) {
		dev_err(dev, "---dc8200_pix0 get error\n");
		return PTR_ERR(dc->dc8200_pix0);
	}

    return 0;
}

static void dc_deinit(struct device *dev)
{
	struct vs_dc *dc = dev_get_drvdata(dev);
	int ret;
	dc_hw_enable_interrupt(&dc->hw, 0);
	dc_hw_deinit(&dc->hw);
	vs_dc_dc8200_clock_disable(dc);
	vs_dc_vouttop_clock_disable(dc);
	vs_dc_clock_disable(dc);
	ret = reset_control_assert(dc->vout_resets);
	if (ret)
		dev_err(dev, "assert vout resets error.\n");
}

static int dc_init(struct device *dev)
{
	struct vs_dc *dc = dev_get_drvdata(dev);
	int ret;

	dc->first_frame = true;

	ret = syscon_panel_parse_dt(dev);
	if (ret){
		dev_err(dev,"syscon_panel_parse_dt failed\n");
		return ret;
	}

	ret = dc_vout_clk_get(dev, dc);
	if (ret) {
		dev_err(dev, "failed to get clock\n");
		return ret;
	}
	vs_vout_reset_get(dev, dc);
	vs_dc8200_reset_get(dev, dc);

	ret = sys_dispctrl_clk_standard(dc, dev);

	ret = dc_vout_clk_enable(dev, dc);
	if (ret) {
		dev_err(dev, "failed to enable clock\n");
		//return ret;
	}
	//vs_vout_reset_deassert(dc);
	vs_dc8200_reset_deassert(dc);
	ret = clk_prepare_enable(dc->vout_top_lcd);
	if (ret)
		dev_err(dev, "failed to prepare/enable vout_top_lcd\n");

	vs_vout_reset_deassert(dc);
#ifdef CONFIG_DRM_I2C_NXP_TDA998X//tda998x-rgb2hdmi
	regmap_update_bits(dc->dss_regmap, 0x4, BIT(20), 1<<20);
#endif

#ifdef CONFIG_STARFIVE_DSI
	regmap_update_bits(dc->dss_regmap, 0x8, BIT(3), 1<<3);
#endif

	dc->dc8200_clk_pix0_out = devm_clk_get(dev, "dc8200_pix0_out");
	if (IS_ERR(dc->dc8200_clk_pix0_out)){
		dev_err(dev,"failed to get dc->dc8200_clk_pix0_out\n");
		return PTR_ERR(dc->dc8200_clk_pix0_out);
	}

	dc->dc8200_clk_pix1_out = devm_clk_get(dev, "dc8200_pix1_out");
	if (IS_ERR(dc->dc8200_clk_pix0_out)){
		dev_err(dev,"failed to get dc->dc8200_clk_pix0_out\n");
		return PTR_ERR(dc->dc8200_clk_pix0_out);
	}

	dc->vout_top_lcd = devm_clk_get(dev, "vout_top_lcd");
	if (IS_ERR(dc->vout_top_lcd)){
		dev_err(dev,"failed to get dc->vout_top_lcd\n");
		return PTR_ERR(dc->vout_top_lcd);
	}

	dc->init_count = 0;

	ret = dc_hw_init(&dc->hw);
	if (ret) {
		dev_err(dev, "failed to init DC HW\n");
		return ret;
	}

	/*after uboot show logo , it will set the pixclock and parent same value,
	  so need to reset a another value to avoid clock framework fail
	  to set value*/
	clk_set_rate(dc->dc8200_pix0, 1000);
	clk_set_parent(dc->dc8200_clk_pix1, dc->hdmitx0_pixelclk);
	clk_set_parent(dc->vout_top_lcd, dc->dc8200_clk_pix0_out);
	clk_set_parent(dc->dc8200_clk_pix0, dc->dc8200_pix0);

	return 0;

}

static void vs_dc_dump_enable(struct device *dev, dma_addr_t addr,
				   unsigned int pitch)
{
	struct vs_dc *dc = dev_get_drvdata(dev);

	dc_hw_enable_dump(&dc->hw, addr, pitch);
}

static void vs_dc_dump_disable(struct device *dev)
{
	struct vs_dc *dc = dev_get_drvdata(dev);

	dc_hw_disable_dump(&dc->hw);
}

static void vs_dc_enable(struct device *dev, struct drm_crtc *crtc)
{
	struct vs_dc *dc = dev_get_drvdata(dev);
	struct vs_crtc_state *crtc_state = to_vs_crtc_state(crtc->state);
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	struct dc_hw_display display;
	int ret;

	if (dc->init_count == 0) {
		ret = dc_vout_clk_enable(dev, dc);
		if (ret)
			dev_err(dev, "failed to enable clock\n");

		vs_dc8200_reset_deassert(dc);
		ret = clk_prepare_enable(dc->vout_top_lcd);
		if (ret)
			dev_err(dev, "failed to prepare/enable vout_top_lcd\n");

		regmap_update_bits(dc->dss_regmap, 0x4, BIT(20), 1<<20);

		regmap_update_bits(dc->dss_regmap, 0x8, BIT(3), 1<<3);

		ret = dc_hw_init(&dc->hw);
		if (ret)
			dev_err(dev, "failed to init DC HW\n");

	}
	dc->init_count++;

	display.bus_format = crtc_state->output_fmt;
	display.h_active = mode->hdisplay;
	display.h_total = mode->htotal;
	display.h_sync_start = mode->hsync_start;
	display.h_sync_end = mode->hsync_end;
	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		display.h_sync_polarity = true;
	else
		display.h_sync_polarity = false;

	display.v_active = mode->vdisplay;
	display.v_total = mode->vtotal;

	if (crtc_state->encoder_type == DRM_MODE_ENCODER_DSI){
		display.v_sync_start = mode->vsync_start + 1;
		display.v_sync_end = mode->vsync_end - 1;
	}else{
		display.v_sync_start = mode->vsync_start;
		display.v_sync_end = mode->vsync_end;
	}

	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		display.v_sync_polarity = true;
	else
		display.v_sync_polarity = false;

	display.sync_mode = crtc_state->sync_mode;
	display.bg_color = crtc_state->bg_color;

	display.id = to_vs_display_id(dc, crtc);
	display.sync_enable = crtc_state->sync_enable;
	display.dither_enable = crtc_state->dither_enable;

	display.enable = true;

	if(display.id == 1)
	{
		clk_set_rate(dc->dc8200_pix0, mode->clock * 1000);
		clk_set_parent(dc->dc8200_clk_pix1, dc->dc8200_pix0);
		clk_set_parent(dc->vout_top_lcd, dc->dc8200_clk_pix1_out);
	}else{
		clk_set_parent(dc->dc8200_clk_pix0, dc->hdmitx0_pixelclk);
	}

	if (crtc_state->encoder_type == DRM_MODE_ENCODER_DSI){
		dc_hw_set_out(&dc->hw, OUT_DPI, display.id);
	} else {
		dc_hw_set_out(&dc->hw, OUT_DP, display.id);
	}

#ifdef CONFIG_VERISILICON_MMU
	if (crtc_state->mmu_prefetch == VS_MMU_PREFETCH_ENABLE)
		dc_hw_enable_mmu_prefetch(&dc->hw, true);
	else
		dc_hw_enable_mmu_prefetch(&dc->hw, false);
#endif

	dc_hw_setup_display(&dc->hw, &display);
}

static void vs_dc_disable(struct device *dev, struct drm_crtc *crtc)
{
	struct vs_dc *dc = dev_get_drvdata(dev);
	struct dc_hw_display display;

	display.id = to_vs_display_id(dc, crtc);
	display.enable = false;

	dc->init_count--;
	dc_hw_setup_display(&dc->hw, &display);

	if (dc->init_count == 0) {

		clk_disable_unprepare(dc->vout_top_lcd);

		vs_dc8200_reset_assert(dc);

		/*dc8200 clk disable*/
		vs_dc_dc8200_clock_disable(dc);

		/*vouttop clk disable*/
		vs_dc_vouttop_clock_disable(dc);

		/*vout clk disable*/
		vs_dc_clock_disable(dc);

		/*297000000 reset the pixclk channel*/
		clk_set_rate(dc->dc8200_pix0, 1000);
		/*reset the parent pixclk  channel*/
		clk_set_parent(dc->dc8200_clk_pix1, dc->hdmitx0_pixelclk);
		clk_set_parent(dc->vout_top_lcd, dc->dc8200_clk_pix0_out);
		clk_set_parent(dc->dc8200_clk_pix0, dc->dc8200_pix0);

	}
}

static bool vs_dc_mode_fixup(struct device *dev,
				  const struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{

#if 1
	;//printk("====> %s, %d--pix_clk.\n", __func__, __LINE__);
#else
	struct vs_dc *dc = dev_get_drvdata(dev);

	long clk_rate;
	if (dc->pix_clk) {
		clk_rate = clk_round_rate(dc->pix_clk,
					  adjusted_mode->clock * 1000);
		adjusted_mode->clock = DIV_ROUND_UP(clk_rate, 1000);
	}
#endif

	return true;
}

static void vs_dc_set_gamma(struct device *dev, struct drm_crtc *crtc,
				 struct drm_color_lut *lut, unsigned int size)
{
	struct vs_dc *dc = dev_get_drvdata(dev);
	u16 i, r, g, b;
	u8 bits, id;

	if (size != dc->hw.info->gamma_size) {
		dev_err(dev, "gamma size does not match!\n");
		return;
	}

	id = to_vs_display_id(dc, crtc);

	bits = dc->hw.info->gamma_bits;
	for (i = 0; i < size; i++) {
		r = drm_color_lut_extract(lut[i].red, bits);
		g = drm_color_lut_extract(lut[i].green, bits);
		b = drm_color_lut_extract(lut[i].blue, bits);
		dc_hw_update_gamma(&dc->hw, id, i, r, g, b);
	}
}

static void vs_dc_enable_gamma(struct device *dev, struct drm_crtc *crtc,
				 bool enable)
{
	struct vs_dc *dc = dev_get_drvdata(dev);
	u8 id;

	id = to_vs_display_id(dc, crtc);
	dc_hw_enable_gamma(&dc->hw, id, enable);
}

static void vs_dc_enable_vblank(struct device *dev, bool enable)
{
	struct vs_dc *dc = dev_get_drvdata(dev);

	dc_hw_enable_interrupt(&dc->hw, enable);
}

static u32 calc_factor(u32 src, u32 dest)
{
	u32 factor = 1 << 16;

	if ((src > 1) && (dest > 1))
		factor = ((src - 1) << 16) / (dest - 1);

	return factor;
}

static void update_scale(struct drm_plane_state *state, struct dc_hw_roi *roi,
						 struct dc_hw_scale *scale)
{
	int dst_w = drm_rect_width(&state->dst);
	int dst_h = drm_rect_height(&state->dst);
	int src_w, src_h, temp;

	scale->enable = false;

	if (roi->enable) {
		src_w = roi->width;
		src_h = roi->height;
	} else {
		src_w = drm_rect_width(&state->src) >> 16;
		src_h = drm_rect_height(&state->src) >> 16;
	}

	if (drm_rotation_90_or_270(state->rotation)) {
		temp = src_w;
		src_w = src_h;
		src_h = temp;
	}

	if (src_w != dst_w) {
		scale->scale_factor_x = calc_factor(src_w, dst_w);
		scale->enable = true;
	} else {
		scale->scale_factor_x = 1 << 16;
	}
	if (src_h != dst_h) {
		scale->scale_factor_y = calc_factor(src_h, dst_h);
		scale->enable = true;
	} else {
		scale->scale_factor_y = 1 << 16;
	}
}

static void update_fb(struct vs_plane *plane, u8 display_id,
					  struct dc_hw_fb *fb, struct drm_plane_state *state)
{
	//struct drm_plane_state *state = plane->base.state;
	struct vs_plane_state *plane_state = to_vs_plane_state(state);
	struct drm_framebuffer *drm_fb = state->fb;
	struct drm_rect *src = &state->src;

	fb->display_id = display_id;
	fb->y_address = plane->dma_addr[0];
	fb->y_stride = drm_fb->pitches[0];
	if (drm_fb->format->format == DRM_FORMAT_YVU420) {
		fb->u_address = plane->dma_addr[2];
		fb->v_address = plane->dma_addr[1];
		fb->u_stride = drm_fb->pitches[2];
		fb->v_stride = drm_fb->pitches[1];
	} else {
		fb->u_address = plane->dma_addr[1];
		fb->v_address = plane->dma_addr[2];
		fb->u_stride = drm_fb->pitches[1];
		fb->v_stride = drm_fb->pitches[2];
	}
	fb->width = drm_rect_width(src) >> 16;
	fb->height = drm_rect_height(src) >> 16;
	fb->tile_mode = to_vs_tile_mode(drm_fb->modifier);
	//fb->tile_mode = 0x04;
	fb->rotation = to_vs_rotation(state->rotation);
	fb->yuv_color_space = to_vs_yuv_color_space(state->color_encoding);
	fb->zpos = state->zpos;
	fb->enable = state->visible;
	update_format(drm_fb->format->format, drm_fb->modifier, fb);
	update_swizzle(drm_fb->format->format, fb);
	update_watermark(plane_state->watermark, fb);

	starfive_flush_dcache(fb->y_address, fb->height * fb->y_stride);
	if (fb->u_address)
		starfive_flush_dcache(fb->u_address, fb->height * fb->u_stride);
	if (fb->v_address)
		starfive_flush_dcache(fb->v_address, fb->height * fb->v_stride);

	plane_state->status.tile_mode = fb->tile_mode;
}

#ifdef CONFIG_VERISILICON_DEC
static u8 get_stream_base(u8 id)
{
	u8 stream_base = 0;

	switch (id) {
	case OVERLAY_PLANE_0:
		stream_base = 3;
		break;
	case OVERLAY_PLANE_1:
		stream_base = 6;
		break;
	case PRIMARY_PLANE_1:
		stream_base = 16;
		break;
	case OVERLAY_PLANE_2:
		stream_base = 19;
		break;
	case OVERLAY_PLANE_3:
		stream_base = 22;
		break;
	default:
		break;
	}

	return stream_base;
}

static void update_fbc(struct vs_dc *dc, struct vs_plane *plane, bool *enable,
						struct drm_plane_state *state)
{
	struct dc_dec_fb dec_fb;
	//struct drm_plane_state *state = plane->base.state;
	struct drm_framebuffer *drm_fb = state->fb;
	struct vs_dc_plane *dc_plane = &dc->planes[plane->id];
	u8 i, stream_id;

	if (!dc->hw.info->cap_dec) {
		*enable = false;
		return;
	}

	stream_id = get_stream_base(dc_plane->id);
	memset(&dec_fb, 0, sizeof(struct dc_dec_fb));
	dec_fb.fb = drm_fb;

	if (fourcc_mod_vs_get_type(drm_fb->modifier) !=
					DRM_FORMAT_MOD_VS_TYPE_COMPRESSED) {
		*enable = false;
	} else {
		*enable = true;

		for (i = 0; i < DEC_PLANE_MAX; i++) {
			dec_fb.addr[i] = plane->dma_addr[i];
			dec_fb.stride[i] = drm_fb->pitches[i];
		}
	}

	dc_dec_config(&dc->dec400l, &dec_fb, stream_id);
}

static void disable_fbc(struct vs_dc *dc, struct vs_plane *plane)
{
	struct vs_dc_plane *dc_plane = &dc->planes[plane->id];
	u8 stream_id;

	if (!dc->hw.info->cap_dec)
		return;

	stream_id = get_stream_base(dc_plane->id);
	dc_dec_config(&dc->dec400l, NULL, stream_id);
}
#endif

static void update_degamma(struct vs_dc *dc, struct vs_plane *plane,
			   struct vs_plane_state *plane_state)
{
	dc_hw_update_degamma(&dc->hw, plane->id, plane_state->degamma);
	plane_state->degamma_changed = false;
}

static void update_roi(struct vs_dc *dc, u8 id,
					   struct vs_plane_state *plane_state,
					   struct dc_hw_roi *roi,
					   struct drm_plane_state *state)
{
	struct drm_vs_roi *data;
	//struct drm_rect *src = &plane_state->base.src;
	struct drm_rect *src = &state->src;
	u16 src_w = drm_rect_width(src) >> 16;
	u16 src_h = drm_rect_height(src) >> 16;

	if (plane_state->roi) {
		data = plane_state->roi->data;

		if (data->enable) {
			roi->x = data->roi_x;
			roi->y = data->roi_y;
			roi->width = (data->roi_x + data->roi_w > src_w) ?
						 (src_w - data->roi_x) : data->roi_w;
			roi->height = (data->roi_y + data->roi_h > src_h) ?
						  (src_h - data->roi_y) : data->roi_h;
			roi->enable = true;
		} else {
			roi->enable = false;
		}

		dc_hw_update_roi(&dc->hw, id, roi);
	} else {
		roi->enable = false;
	}
}

static void update_color_mgmt(struct vs_dc *dc, u8 id,
							struct dc_hw_fb *fb,
							struct vs_plane_state *plane_state)
{
	struct drm_vs_color_mgmt *data;
	struct dc_hw_colorkey colorkey;

	if (plane_state->color_mgmt) {
		data = plane_state->color_mgmt->data;

		fb->clear_enable = data->clear_enable;
		fb->clear_value = data->clear_value;

		if (data->colorkey > data->colorkey_high)
			data->colorkey = data->colorkey_high;

		colorkey.colorkey = data->colorkey;
		colorkey.colorkey_high = data->colorkey_high;
		colorkey.transparency = (data->transparency) ?
				DC_TRANSPARENCY_KEY : DC_TRANSPARENCY_OPAQUE;
		dc_hw_update_colorkey(&dc->hw, id, &colorkey);
	}
}

static void update_plane(struct vs_dc *dc, struct vs_plane *plane, struct drm_plane *drm_plane,
						struct drm_atomic_state *drm_state)
{
	struct dc_hw_fb fb = {0};
	struct dc_hw_scale scale;
	struct dc_hw_position pos;
	struct dc_hw_blend blend;
	struct dc_hw_roi roi;
	//struct drm_plane_state *state = plane->base.state;
	struct drm_plane_state *state = drm_atomic_get_new_plane_state(drm_state,
									   drm_plane);
	struct vs_plane_state *plane_state = to_vs_plane_state(state);
	struct drm_rect *dest = &state->dst;
	bool dec_enable = false;
	u8 display_id = 0;

#ifdef CONFIG_VERISILICON_DEC
	update_fbc(dc, plane, &dec_enable, state);
#endif

	display_id = to_vs_display_id(dc, state->crtc);
	update_fb(plane, display_id, &fb, state);
	fb.dec_enable = dec_enable;


	update_roi(dc, plane->id, plane_state, &roi, state);

	update_scale(state, &roi, &scale);

	if (plane_state->degamma_changed)
		update_degamma(dc, plane, plane_state);

	pos.start_x = dest->x1;
	pos.start_y = dest->y1;
	pos.end_x = dest->x2;
	pos.end_y = dest->y2;

	blend.alpha = (u8)(state->alpha >> 8);
	blend.blend_mode = (u8)(state->pixel_blend_mode);

	update_color_mgmt(dc, plane->id, &fb, plane_state);

	dc_hw_update_plane(&dc->hw, plane->id, &fb, &scale, &pos, &blend);
}

static void update_qos(struct vs_dc *dc, struct vs_plane *plane, struct drm_plane *drm_plane,
						struct drm_atomic_state *drm_state)
{
	//struct drm_plane_state *state = plane->base.state;
	struct drm_plane_state *state = drm_atomic_get_new_plane_state(drm_state,
									   drm_plane);
	struct vs_plane_state *plane_state = to_vs_plane_state(state);
	struct drm_vs_watermark *data;
	struct dc_hw_qos qos;

	if (plane_state->watermark) {
		data = plane_state->watermark->data;

		if (data->qos_high) {
			if (data->qos_low > data->qos_high)
				data->qos_low = data->qos_high;

			qos.low_value = data->qos_low & 0x0F;
			qos.high_value = data->qos_high & 0x0F;
			dc_hw_update_qos(&dc->hw, &qos);
		}
	}
}

static void update_cursor_size(struct drm_plane_state *state, struct dc_hw_cursor *cursor)
{
	u8 size_type;

	switch (state->crtc_w) {
	case 32:
		size_type = CURSOR_SIZE_32X32;
		break;
	case 64:
		size_type = CURSOR_SIZE_64X64;
		break;
	default:
		size_type = CURSOR_SIZE_32X32;
		break;
	}

	cursor->size = size_type;
}

static void update_cursor_plane(struct vs_dc *dc, struct vs_plane *plane, struct drm_plane *drm_plane,
									struct drm_atomic_state *drm_state)
{
	//struct drm_plane_state *state = plane->base.state;
	struct drm_plane_state *state = drm_atomic_get_new_plane_state(drm_state,
									   drm_plane);
	struct drm_framebuffer *drm_fb = state->fb;
	struct dc_hw_cursor cursor;

	cursor.address = plane->dma_addr[0];
	cursor.x = state->crtc_x;
	cursor.y = state->crtc_y;
	cursor.hot_x = drm_fb->hot_x;
	cursor.hot_y = drm_fb->hot_y;
	cursor.display_id = to_vs_display_id(dc, state->crtc);
	update_cursor_size(state, &cursor);
	cursor.enable = true;

	dc_hw_update_cursor(&dc->hw, cursor.display_id, &cursor);
}

static void vs_dc_update_plane(struct device *dev, struct vs_plane *plane, struct drm_plane *drm_plane,
								struct drm_atomic_state *drm_state)
{
	struct vs_dc *dc = dev_get_drvdata(dev);
	enum drm_plane_type type = plane->base.type;

	switch (type) {
	case DRM_PLANE_TYPE_PRIMARY:
	case DRM_PLANE_TYPE_OVERLAY:
		update_plane(dc, plane, drm_plane, drm_state);
		update_qos(dc, plane, drm_plane, drm_state);
		break;
	case DRM_PLANE_TYPE_CURSOR:
		update_cursor_plane(dc, plane, drm_plane, drm_state);
		break;
	default:
		break;
	}
}

static void vs_dc_disable_plane(struct device *dev, struct vs_plane *plane,
								struct drm_plane_state *old_state)
{
	struct vs_dc *dc = dev_get_drvdata(dev);
	enum drm_plane_type type = plane->base.type;
	struct dc_hw_fb fb = {0};
	struct dc_hw_cursor cursor = {0};

	switch (type) {
	case DRM_PLANE_TYPE_PRIMARY:
	case DRM_PLANE_TYPE_OVERLAY:
		fb.enable = false;
		dc_hw_update_plane(&dc->hw, plane->id, &fb, NULL, NULL, NULL);
#ifdef CONFIG_VERISILICON_DEC
		disable_fbc(dc, plane);
#endif
		break;
	case DRM_PLANE_TYPE_CURSOR:
		cursor.enable = false;
		cursor.display_id = to_vs_display_id(dc, old_state->crtc);
		dc_hw_update_cursor(&dc->hw, cursor.display_id, &cursor);
		break;
	default:
		break;
	}
}

static bool vs_dc_mod_supported(const struct vs_plane_info *plane_info,
								u64 modifier)
{
	const u64 *mods;

	if (plane_info->modifiers == NULL)
		return false;

	for (mods = plane_info->modifiers; *mods != DRM_FORMAT_MOD_INVALID; mods++) {
		if (*mods == modifier)
			return true;
	}

	return false;
}

static int vs_dc_check_plane(struct device *dev, struct drm_plane *plane,
				  struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
											 plane);
	struct vs_dc *dc = dev_get_drvdata(dev);
	struct drm_framebuffer *fb = new_plane_state->fb;
	const struct vs_plane_info *plane_info;
	struct drm_crtc *crtc = new_plane_state->crtc;
	struct drm_crtc_state *crtc_state;
	struct vs_plane *vs_plane = to_vs_plane(plane);

	plane_info = &dc->hw.info->planes[vs_plane->id];
	if (plane_info == NULL)
		return -EINVAL;

	if (fb->width < plane_info->min_width ||
		fb->width > plane_info->max_width ||
		fb->height < plane_info->min_height ||
		fb->height > plane_info->max_height)
		dev_err_once(dev, "buffer size may not support on plane%d.\n",
				 vs_plane->id);

	if ((vs_plane->base.type != DRM_PLANE_TYPE_CURSOR) &&
		(!vs_dc_mod_supported(plane_info, fb->modifier))) {
		dev_err(dev, "unsupported modifier on plane%d.\n", vs_plane->id);
		return -EINVAL;
	}

	crtc_state = drm_atomic_get_existing_crtc_state(state, crtc);
	if (IS_ERR(crtc_state))
		return -EINVAL;

	return drm_atomic_helper_check_plane_state(new_plane_state, crtc_state,
						  plane_info->min_scale,
						  plane_info->max_scale,
						  true, true);
}

static irqreturn_t dc_isr(int irq, void *data)
{
	struct vs_dc *dc = data;
	struct vs_dc_info *dc_info = dc->hw.info;
	u32 i, ret;

	if(!dc_info)
	  return IRQ_HANDLED;

	ret = dc_hw_get_interrupt(&dc->hw);

	for (i = 0; i < dc_info->panel_num; i++)
	  vs_crtc_handle_vblank(&dc->crtc[i]->base, dc_hw_check_underflow(&dc->hw));

	return IRQ_HANDLED;
}

static void vs_dc_commit(struct device *dev)
{
	struct vs_dc *dc = dev_get_drvdata(dev);

#ifdef CONFIG_VERISILICON_DEC
	if (dc->hw.info->cap_dec)
		dc_dec_commit(&dc->dec400l, &dc->hw);
#endif

	dc_hw_enable_shadow_register(&dc->hw, false);

	dc_hw_commit(&dc->hw);

	if (dc->first_frame)
		dc->first_frame = false;

	dc_hw_enable_shadow_register(&dc->hw, true);
}

static const struct vs_crtc_funcs dc_crtc_funcs = {
	.enable			= vs_dc_enable,
	.disable		= vs_dc_disable,
	.mode_fixup		= vs_dc_mode_fixup,
	.set_gamma		= vs_dc_set_gamma,
	.enable_gamma	= vs_dc_enable_gamma,
	.enable_vblank	= vs_dc_enable_vblank,
	.commit			= vs_dc_commit,
};

static const struct vs_plane_funcs dc_plane_funcs = {
	.update			= vs_dc_update_plane,
	.disable		= vs_dc_disable_plane,
	.check			= vs_dc_check_plane,
};

static const struct vs_dc_funcs dc_funcs = {
	.dump_enable		= vs_dc_dump_enable,
	.dump_disable		= vs_dc_dump_disable,
};

static int dc_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm_dev = data;
#ifdef CONFIG_VERISILICON_MMU
	struct vs_drm_private *priv = drm_dev->dev_private;
#endif
	struct vs_dc *dc = dev_get_drvdata(dev);
	struct device_node *port;
	struct vs_crtc *crtc;
	struct drm_crtc *drm_crtc;
	struct vs_dc_info *dc_info;
	struct vs_plane *plane;
	struct drm_plane *drm_plane, *tmp;
	struct vs_plane_info *plane_info;
	int i, ret;
	u32 ctrc_mask = 0;

	if (!drm_dev || !dc) {
		dev_err(dev, "devices are not created.\n");
		return -ENODEV;
	}

	ret = dc_init(dev);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize DC hardware.\n");
		return ret;
	}

#ifdef CONFIG_VERISILICON_MMU
	ret = dc_mmu_construct(priv->dma_dev, &priv->mmu);
	if (ret) {
		dev_err(dev, "failed to construct DC MMU\n");
		goto err_clean_dc;
	}

	ret = dc_hw_mmu_init(&dc->hw, priv->mmu);
	if (ret) {
		dev_err(dev, "failed to init DC MMU\n");
		goto err_clean_dc;
	}
#endif

	ret = vs_drm_iommu_attach_device(drm_dev, dev);
	if (ret < 0) {
		dev_err(dev, "Failed to attached iommu device.\n");
		goto err_clean_dc;
	}

	port = of_get_child_by_name(dev->of_node, "port");
	if (!port) {
		dev_err(dev, "no port node found\n");
		goto err_detach_dev;
	}
	of_node_put(port);

	dc_info = dc->hw.info;

	for (i = 0; i < dc_info->panel_num; i++) {
		crtc = vs_crtc_create(drm_dev, dc_info);
		if (!crtc) {
			dev_err(dev, "Failed to create CRTC.\n");
			ret = -ENOMEM;
			goto err_detach_dev;
		}

		crtc->base.port = port;
		crtc->dev = dev;
		crtc->funcs = &dc_crtc_funcs;
		dc->crtc[i] = crtc;
		ctrc_mask |= drm_crtc_mask(&crtc->base);
	}

	for (i = 0; i < dc_info->plane_num; i++) {
		plane_info = (struct vs_plane_info *)&dc_info->planes[i];

		if (!strcmp(plane_info->name, "Primary") || !strcmp(plane_info->name, "Cursor")) {
			plane = vs_plane_create(drm_dev, plane_info, dc_info->layer_num,
					drm_crtc_mask(&dc->crtc[0]->base));
		} else if (!strcmp(plane_info->name, "Primary_1") ||
				 !strcmp(plane_info->name, "Cursor_1")) {
			plane = vs_plane_create(drm_dev, plane_info, dc_info->layer_num,
					drm_crtc_mask(&dc->crtc[1]->base));
		} else {
			plane = vs_plane_create(drm_dev, plane_info,
					dc_info->layer_num, ctrc_mask);
		}

		if (!plane)
			goto err_cleanup_planes;

		plane->id = i;
		dc->planes[i].id = plane_info->id;

		plane->funcs = &dc_plane_funcs;

		if (plane_info->type == DRM_PLANE_TYPE_PRIMARY) {
			if (!strcmp(plane_info->name, "Primary"))
				dc->crtc[0]->base.primary = &plane->base;
			else
				dc->crtc[1]->base.primary = &plane->base;
			drm_dev->mode_config.min_width = plane_info->min_width;
			drm_dev->mode_config.min_height =
							plane_info->min_height;
			drm_dev->mode_config.max_width = plane_info->max_width;
			drm_dev->mode_config.max_height =
							plane_info->max_height;
		}

		if (plane_info->type == DRM_PLANE_TYPE_CURSOR) {
			if (!strcmp(plane_info->name, "Cursor"))
				dc->crtc[0]->base.cursor = &plane->base;
			else
				dc->crtc[1]->base.cursor = &plane->base;
			drm_dev->mode_config.cursor_width =
							plane_info->max_width;
			drm_dev->mode_config.cursor_height =
							plane_info->max_height;
		}
	}

	dc->funcs = &dc_funcs;

	vs_drm_update_pitch_alignment(drm_dev, dc_info->pitch_alignment);

	clk_disable_unprepare(dc->vout_top_lcd);
/*dc8200 asrt*/
	vs_dc8200_reset_assert(dc);

/*dc8200 clk disable*/
	vs_dc_dc8200_clock_disable(dc);

/*vouttop clk disable*/
	vs_dc_vouttop_clock_disable(dc);

/*vout clk disable*/
	vs_dc_clock_disable(dc);

	return 0;

err_cleanup_planes:
	list_for_each_entry_safe(drm_plane, tmp,
				 &drm_dev->mode_config.plane_list, head)
		if (drm_plane->possible_crtcs & ctrc_mask)
			vs_plane_destory(drm_plane);

	drm_for_each_crtc(drm_crtc, drm_dev)
		vs_crtc_destroy(drm_crtc);
err_detach_dev:
	vs_drm_iommu_detach_device(drm_dev, dev);
err_clean_dc:
	dc_deinit(dev);
	return ret;
}

static void dc_unbind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm_dev = data;

	dc_deinit(dev);

	vs_drm_iommu_detach_device(drm_dev, dev);
}

const struct component_ops dc_component_ops = {
	.bind = dc_bind,
	.unbind = dc_unbind,
};

static const struct of_device_id dc_driver_dt_match[] = {
	{ .compatible = "verisilicon,dc8200", },
	{},
};
MODULE_DEVICE_TABLE(of, dc_driver_dt_match);

static int dc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vs_dc *dc;
	int irq, ret;

	dc = devm_kzalloc(dev, sizeof(*dc), GFP_KERNEL);
	if (!dc)
		return -ENOMEM;


	dc->hw.hi_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dc->hw.hi_base))
		return PTR_ERR(dc->hw.hi_base);

	dc->hw.reg_base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(dc->hw.reg_base))
		return PTR_ERR(dc->hw.reg_base);

#ifdef CONFIG_VERISILICON_MMU
	dc->hw.mmu_base = devm_platform_ioremap_resource(pdev, 2);
	if (IS_ERR(dc->hw.mmu_base))
		return PTR_ERR(dc->hw.mmu_base);
#endif
	irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(dev, irq, dc_isr, 0, dev_name(dev), dc);
	if (ret < 0) {
		dev_err(dev, "Failed to install irq:%u.\n", irq);
		return ret;
	}

	dev_set_drvdata(dev, dc);

	return component_add(dev, &dc_component_ops);
}

static int dc_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	component_del(dev, &dc_component_ops);

	dev_set_drvdata(dev, NULL);

	return 0;
}

struct platform_driver dc_platform_driver = {
	.probe = dc_probe,
	.remove = dc_remove,
	.driver = {
		.name = "vs-dc",
		.of_match_table = of_match_ptr(dc_driver_dt_match),
	},
};

MODULE_DESCRIPTION("VeriSilicon DC Driver");
MODULE_LICENSE("GPL v2");
