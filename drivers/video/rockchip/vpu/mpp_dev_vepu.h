/**
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd
 *	   Alpha Lin, alpha.lin@rock-chips.com
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ROCKCHIP_MPP_DEV_VEPU_H
#define __ROCKCHIP_MPP_DEV_VEPU_H

#define ROCKCHIP_VEPU_REG_LEN		184

struct regmap;

struct rockchip_vepu_dev {
	struct rockchip_mpp_dev idev;

	u32 irq_status;

	struct clk *aclk;
	struct clk *hclk;
	struct clk *cclk;

	struct reset_control *rst_a;
	struct reset_control *rst_h;

	u32 mode_bit;
	u32 mode_ctrl;
	struct regmap *grf;
};

struct vepu_ctx {
	struct mpp_ctx ictx;

	u32 reg[ROCKCHIP_VEPU_REG_LEN];
	struct extra_info_for_iommu ext_inf;
};

#endif
