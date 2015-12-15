/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>

#include <linux/kernel.h>
#include <linux/component.h>

#include "rockchip_drm_vop.h"
#include "rockchip_vop_reg.h"

#define VOP_REG(off, _mask, s) \
		{.offset = off, \
		 .mask = _mask, \
		 .shift = s,}

static const uint32_t formats_01[] = {
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

static const uint32_t formats_234[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
};

static const struct vop_scl_extension win_full_ext = {
	.cbcr_vsd_mode = VOP_REG(WIN0_CTRL1, 0x1, 31),
	.cbcr_vsu_mode = VOP_REG(WIN0_CTRL1, 0x1, 30),
	.cbcr_hsd_mode = VOP_REG(WIN0_CTRL1, 0x3, 28),
	.cbcr_ver_scl_mode = VOP_REG(WIN0_CTRL1, 0x3, 26),
	.cbcr_hor_scl_mode = VOP_REG(WIN0_CTRL1, 0x3, 24),
	.yrgb_vsd_mode = VOP_REG(WIN0_CTRL1, 0x1, 23),
	.yrgb_vsu_mode = VOP_REG(WIN0_CTRL1, 0x1, 22),
	.yrgb_hsd_mode = VOP_REG(WIN0_CTRL1, 0x3, 20),
	.yrgb_ver_scl_mode = VOP_REG(WIN0_CTRL1, 0x3, 18),
	.yrgb_hor_scl_mode = VOP_REG(WIN0_CTRL1, 0x3, 16),
	.line_load_mode = VOP_REG(WIN0_CTRL1, 0x1, 15),
	.cbcr_axi_gather_num = VOP_REG(WIN0_CTRL1, 0x7, 12),
	.yrgb_axi_gather_num = VOP_REG(WIN0_CTRL1, 0xf, 8),
	.vsd_cbcr_gt2 = VOP_REG(WIN0_CTRL1, 0x1, 7),
	.vsd_cbcr_gt4 = VOP_REG(WIN0_CTRL1, 0x1, 6),
	.vsd_yrgb_gt2 = VOP_REG(WIN0_CTRL1, 0x1, 5),
	.vsd_yrgb_gt4 = VOP_REG(WIN0_CTRL1, 0x1, 4),
	.bic_coe_sel = VOP_REG(WIN0_CTRL1, 0x3, 2),
	.cbcr_axi_gather_en = VOP_REG(WIN0_CTRL1, 0x1, 1),
	.yrgb_axi_gather_en = VOP_REG(WIN0_CTRL1, 0x1, 0),
	.lb_mode = VOP_REG(WIN0_CTRL0, 0x7, 5),
};

static const struct vop_scl_regs win_full_scl = {
	.scale_yrgb_x = VOP_REG(WIN0_SCL_FACTOR_YRGB, 0xffff, 0x0),
	.scale_yrgb_y = VOP_REG(WIN0_SCL_FACTOR_YRGB, 0xffff, 16),
	.scale_cbcr_x = VOP_REG(WIN0_SCL_FACTOR_CBR, 0xffff, 0x0),
	.scale_cbcr_y = VOP_REG(WIN0_SCL_FACTOR_CBR, 0xffff, 16),
};

static const struct vop_win_phy win01_data = {
	.scl = &win_full_scl,
	.data_formats = formats_01,
	.nformats = ARRAY_SIZE(formats_01),
	.enable = VOP_REG(WIN0_CTRL0, 0x1, 0),
	.format = VOP_REG(WIN0_CTRL0, 0x7, 1),
	.rb_swap = VOP_REG(WIN0_CTRL0, 0x1, 12),
	.act_info = VOP_REG(WIN0_ACT_INFO, 0x1fff1fff, 0),
	.dsp_info = VOP_REG(WIN0_DSP_INFO, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(WIN0_DSP_ST, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(WIN0_YRGB_MST, 0xffffffff, 0),
	.uv_mst = VOP_REG(WIN0_CBR_MST, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(WIN0_VIR, 0x3fff, 0),
	.uv_vir = VOP_REG(WIN0_VIR, 0x3fff, 16),
	.src_alpha_ctl = VOP_REG(WIN0_SRC_ALPHA_CTRL, 0xff, 0),
	.dst_alpha_ctl = VOP_REG(WIN0_DST_ALPHA_CTRL, 0xff, 0),
};

static const struct vop_win_phy win23_data = {
	.data_formats = formats_234,
	.nformats = ARRAY_SIZE(formats_234),
	.enable = VOP_REG(WIN2_CTRL0, 0x1, 0),
	.format = VOP_REG(WIN2_CTRL0, 0x7, 1),
	.rb_swap = VOP_REG(WIN2_CTRL0, 0x1, 12),
	.dsp_info = VOP_REG(WIN2_DSP_INFO0, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(WIN2_DSP_ST0, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(WIN2_MST0, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(WIN2_VIR0_1, 0x1fff, 0),
	.src_alpha_ctl = VOP_REG(WIN2_SRC_ALPHA_CTRL, 0xff, 0),
	.dst_alpha_ctl = VOP_REG(WIN2_DST_ALPHA_CTRL, 0xff, 0),
};

static const struct vop_ctrl ctrl_data = {
	.standby = VOP_REG(SYS_CTRL, 0x1, 22),
	.gate_en = VOP_REG(SYS_CTRL, 0x1, 23),
	.mmu_en = VOP_REG(SYS_CTRL, 0x1, 20),
	.rgb_en = VOP_REG(SYS_CTRL, 0x1, 12),
	.hdmi_en = VOP_REG(SYS_CTRL, 0x1, 13),
	.edp_en = VOP_REG(SYS_CTRL, 0x1, 14),
	.mipi_en = VOP_REG(SYS_CTRL, 0x1, 15),
	.dither_down = VOP_REG(DSP_CTRL1, 0xf, 1),
	.dither_up = VOP_REG(DSP_CTRL1, 0x1, 6),
	.data_blank = VOP_REG(DSP_CTRL0, 0x1, 19),
	.out_mode = VOP_REG(DSP_CTRL0, 0xf, 0),
	.pin_pol = VOP_REG(DSP_CTRL0, 0xf, 4),
	.htotal_pw = VOP_REG(DSP_HTOTAL_HS_END, 0x1fff1fff, 0),
	.hact_st_end = VOP_REG(DSP_HACT_ST_END, 0x1fff1fff, 0),
	.vtotal_pw = VOP_REG(DSP_VTOTAL_VS_END, 0x1fff1fff, 0),
	.vact_st_end = VOP_REG(DSP_VACT_ST_END, 0x1fff1fff, 0),
	.hpost_st_end = VOP_REG(POST_DSP_HACT_INFO, 0x1fff1fff, 0),
	.vpost_st_end = VOP_REG(POST_DSP_VACT_INFO, 0x1fff1fff, 0),
	.cfg_done = VOP_REG(REG_CFG_DONE, 0x1, 0),
};

static const struct vop_reg_data vop_init_reg_table[] = {
	{SYS_CTRL, 0x00c00000},
	{DSP_CTRL0, 0x00000000},
	{WIN0_CTRL0, 0x00000080},
	{WIN1_CTRL0, 0x00000080},
	/* TODO: Win2/3 support multiple area function, but we haven't found
	 * a suitable way to use it yet, so let's just use them as other windows
	 * with only area 0 enabled.
	 */
	{WIN2_CTRL0, 0x00000010},
	{WIN3_CTRL0, 0x00000010},
};

/*
 * Note: rk3288 has a dedicated 'cursor' window, however, that window requires
 * special support to get alpha blending working.  For now, just use overlay
 * window 3 for the drm cursor.
 *
 */
static const struct vop_win_data rk3288_vop_win_data[] = {
	{ .base = 0x00, .phy = &win01_data, .type = DRM_PLANE_TYPE_PRIMARY },
	{ .base = 0x40, .phy = &win01_data, .type = DRM_PLANE_TYPE_OVERLAY },
	{ .base = 0x00, .phy = &win23_data, .type = DRM_PLANE_TYPE_OVERLAY },
	{ .base = 0x50, .phy = &win23_data, .type = DRM_PLANE_TYPE_CURSOR },
};

static const int rk3288_vop_intrs[] = {
	DSP_HOLD_VALID_INTR,
	FS_INTR,
	LINE_FLAG_INTR,
	BUS_ERROR_INTR,
};

static const struct vop_intr rk3288_vop_intr = {
	.intrs = rk3288_vop_intrs,
	.nintrs = ARRAY_SIZE(rk3288_vop_intrs),
	.status = VOP_REG(INTR_CTRL0, 0xf, 0),
	.enable = VOP_REG(INTR_CTRL0, 0xf, 4),
	.clear = VOP_REG(INTR_CTRL0, 0xf, 8),
};

static const struct vop_data rk3288_vop = {
	.init_table = vop_init_reg_table,
	.intr = &rk3288_vop_intr,
	.table_size = ARRAY_SIZE(vop_init_reg_table),
	.ctrl = &ctrl_data,
	.win = rk3288_vop_win_data,
	.win_size = ARRAY_SIZE(rk3288_vop_win_data),
};

static const struct of_device_id vop_driver_dt_match[] = {
	{ .compatible = "rockchip,rk3288-vop",
	  .data = &rk3288_vop },
	{},
};
MODULE_DEVICE_TABLE(of, vop_driver_dt_match);

static int vop_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	if (!dev->of_node) {
		dev_err(dev, "can't find vop devices\n");
		return -ENODEV;
	}

	return component_add(dev, &vop_component_ops);
}

static int vop_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &vop_component_ops);

	return 0;
}

struct platform_driver vop_platform_driver = {
	.probe = vop_probe,
	.remove = vop_remove,
	.driver = {
		.name = "rockchip-vop",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(vop_driver_dt_match),
	},
};

module_platform_driver(vop_platform_driver);

MODULE_AUTHOR("Mark Yao <mark.yao@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP VOP Driver");
MODULE_LICENSE("GPL v2");
