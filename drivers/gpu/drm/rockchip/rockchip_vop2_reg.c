// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
 * Author: Andy Yan <andy.yan@rock-chips.com>
 */

#include <linux/kernel.h>
#include <linux/component.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_plane.h>
#include <drm/drm_print.h>

#include "rockchip_drm_vop2.h"

static const uint32_t formats_win_full_10bit[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV16,
	DRM_FORMAT_NV24,
};

static const uint32_t formats_win_full_10bit_yuyv[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV16,
	DRM_FORMAT_NV24,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_VYUY,
};

static const uint32_t formats_win_lite[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
};

static const uint64_t format_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID,
};

static const uint64_t format_modifiers_afbc[] = {
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_SPARSE),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_CBR),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_SPARSE),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_CBR |
				AFBC_FORMAT_MOD_SPARSE),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_CBR),

	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_CBR |
				AFBC_FORMAT_MOD_SPARSE),

	/* SPLIT mandates SPARSE, RGB modes mandates YTR */
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
				AFBC_FORMAT_MOD_YTR |
				AFBC_FORMAT_MOD_SPARSE |
				AFBC_FORMAT_MOD_SPLIT),
	DRM_FORMAT_MOD_INVALID,
};

static const struct vop2_video_port_data rk3568_vop_video_ports[] = {
	{
		.id = 0,
		.feature = VOP_FEATURE_OUTPUT_10BIT,
		.gamma_lut_len = 1024,
		.cubic_lut_len = 9 * 9 * 9,
		.max_output = { 4096, 2304 },
		.pre_scan_max_dly = { 69, 53, 53, 42 },
		.offset = 0xc00,
	}, {
		.id = 1,
		.gamma_lut_len = 1024,
		.max_output = { 2048, 1536 },
		.pre_scan_max_dly = { 40, 40, 40, 40 },
		.offset = 0xd00,
	}, {
		.id = 2,
		.gamma_lut_len = 1024,
		.max_output = { 1920, 1080 },
		.pre_scan_max_dly = { 40, 40, 40, 40 },
		.offset = 0xe00,
	},
};

/*
 * rk3568 vop with 2 cluster, 2 esmart win, 2 smart win.
 * Every cluster can work as 4K win or split into two win.
 * All win in cluster support AFBCD.
 *
 * Every esmart win and smart win support 4 Multi-region.
 *
 * Scale filter mode:
 *
 * * Cluster:  bicubic for horizontal scale up, others use bilinear
 * * ESmart:
 *    * nearest-neighbor/bilinear/bicubic for scale up
 *    * nearest-neighbor/bilinear/average for scale down
 *
 *
 * @TODO describe the wind like cpu-map dt nodes;
 */
static const struct vop2_win_data rk3568_vop_win_data[] = {
	{
		.name = "Smart0-win0",
		.phys_id = ROCKCHIP_VOP2_SMART0,
		.base = 0x1c00,
		.formats = formats_win_lite,
		.nformats = ARRAY_SIZE(formats_win_lite),
		.format_modifiers = format_modifiers,
		.layer_sel_id = 3,
		.supported_rotations = DRM_MODE_REFLECT_Y,
		.type = DRM_PLANE_TYPE_PRIMARY,
		.max_upscale_factor = 8,
		.max_downscale_factor = 8,
		.dly = { 20, 47, 41 },
	}, {
		.name = "Smart1-win0",
		.phys_id = ROCKCHIP_VOP2_SMART1,
		.formats = formats_win_lite,
		.nformats = ARRAY_SIZE(formats_win_lite),
		.format_modifiers = format_modifiers,
		.base = 0x1e00,
		.layer_sel_id = 7,
		.supported_rotations = DRM_MODE_REFLECT_Y,
		.type = DRM_PLANE_TYPE_PRIMARY,
		.max_upscale_factor = 8,
		.max_downscale_factor = 8,
		.dly = { 20, 47, 41 },
	}, {
		.name = "Esmart1-win0",
		.phys_id = ROCKCHIP_VOP2_ESMART1,
		.formats = formats_win_full_10bit_yuyv,
		.nformats = ARRAY_SIZE(formats_win_full_10bit_yuyv),
		.format_modifiers = format_modifiers,
		.base = 0x1a00,
		.layer_sel_id = 6,
		.supported_rotations = DRM_MODE_REFLECT_Y,
		.type = DRM_PLANE_TYPE_PRIMARY,
		.max_upscale_factor = 8,
		.max_downscale_factor = 8,
		.dly = { 20, 47, 41 },
	}, {
		.name = "Esmart0-win0",
		.phys_id = ROCKCHIP_VOP2_ESMART0,
		.formats = formats_win_full_10bit_yuyv,
		.nformats = ARRAY_SIZE(formats_win_full_10bit_yuyv),
		.format_modifiers = format_modifiers,
		.base = 0x1800,
		.layer_sel_id = 2,
		.supported_rotations = DRM_MODE_REFLECT_Y,
		.type = DRM_PLANE_TYPE_OVERLAY,
		.max_upscale_factor = 8,
		.max_downscale_factor = 8,
		.dly = { 20, 47, 41 },
	}, {
		.name = "Cluster0-win0",
		.phys_id = ROCKCHIP_VOP2_CLUSTER0,
		.base = 0x1000,
		.formats = formats_win_full_10bit,
		.nformats = ARRAY_SIZE(formats_win_full_10bit),
		.format_modifiers = format_modifiers_afbc,
		.layer_sel_id = 0,
		.supported_rotations = DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_270 |
					DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y,
		.max_upscale_factor = 4,
		.max_downscale_factor = 4,
		.dly = { 0, 27, 21 },
		.type = DRM_PLANE_TYPE_OVERLAY,
		.feature = WIN_FEATURE_AFBDC | WIN_FEATURE_CLUSTER,
	}, {
		.name = "Cluster1-win0",
		.phys_id = ROCKCHIP_VOP2_CLUSTER1,
		.base = 0x1200,
		.formats = formats_win_full_10bit,
		.nformats = ARRAY_SIZE(formats_win_full_10bit),
		.format_modifiers = format_modifiers_afbc,
		.layer_sel_id = 1,
		.supported_rotations = DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_270 |
					DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y,
		.type = DRM_PLANE_TYPE_OVERLAY,
		.max_upscale_factor = 4,
		.max_downscale_factor = 4,
		.dly = { 0, 27, 21 },
		.feature = WIN_FEATURE_AFBDC | WIN_FEATURE_CLUSTER,
	},
};

static const struct vop2_data rk3566_vop = {
	.nr_vps = 3,
	.max_input = { 4096, 2304 },
	.max_output = { 4096, 2304 },
	.vp = rk3568_vop_video_ports,
	.win = rk3568_vop_win_data,
	.win_size = ARRAY_SIZE(rk3568_vop_win_data),
	.soc_id = 3566,
};

static const struct vop2_data rk3568_vop = {
	.nr_vps = 3,
	.max_input = { 4096, 2304 },
	.max_output = { 4096, 2304 },
	.vp = rk3568_vop_video_ports,
	.win = rk3568_vop_win_data,
	.win_size = ARRAY_SIZE(rk3568_vop_win_data),
	.soc_id = 3568,
};

static const struct of_device_id vop2_dt_match[] = {
	{
		.compatible = "rockchip,rk3566-vop",
		.data = &rk3566_vop,
	}, {
		.compatible = "rockchip,rk3568-vop",
		.data = &rk3568_vop,
	}, {
	},
};
MODULE_DEVICE_TABLE(of, vop2_dt_match);

static int vop2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	return component_add(dev, &vop2_component_ops);
}

static int vop2_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &vop2_component_ops);

	return 0;
}

struct platform_driver vop2_platform_driver = {
	.probe = vop2_probe,
	.remove = vop2_remove,
	.driver = {
		.name = "rockchip-vop2",
		.of_match_table = of_match_ptr(vop2_dt_match),
	},
};
