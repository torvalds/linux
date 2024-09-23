/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#ifndef __DRV_CLK_MTK_RESET_H
#define __DRV_CLK_MTK_RESET_H

#include <linux/reset-controller.h>
#include <linux/types.h>

#define RST_NR_PER_BANK 32

/* Infra global controller reset set register */
#define INFRA_RST0_SET_OFFSET 0x120
#define INFRA_RST1_SET_OFFSET 0x130
#define INFRA_RST2_SET_OFFSET 0x140
#define INFRA_RST3_SET_OFFSET 0x150
#define INFRA_RST4_SET_OFFSET 0x730

/**
 * enum mtk_reset_version - Version of MediaTek clock reset controller.
 * @MTK_RST_SIMPLE: Use the same registers for bit set and clear.
 * @MTK_RST_SET_CLR: Use separate registers for bit set and clear.
 * @MTK_RST_MAX: Total quantity of version for MediaTek clock reset controller.
 */
enum mtk_reset_version {
	MTK_RST_SIMPLE = 0,
	MTK_RST_SET_CLR,
	MTK_RST_MAX,
};

/**
 * struct mtk_clk_rst_desc - Description of MediaTek clock reset.
 * @version: Reset version which is defined in enum mtk_reset_version.
 * @rst_bank_ofs: Pointer to an array containing base offsets of the reset register.
 * @rst_bank_nr: Quantity of reset bank.
 * @rst_idx_map:Pointer to an array containing ids if input argument is index.
 *		This array is not necessary if our input argument does not mean index.
 * @rst_idx_map_nr: Quantity of reset index map.
 */
struct mtk_clk_rst_desc {
	enum mtk_reset_version version;
	u16 *rst_bank_ofs;
	u32 rst_bank_nr;
	u16 *rst_idx_map;
	u32 rst_idx_map_nr;
};

/**
 * struct mtk_clk_rst_data - Data of MediaTek clock reset controller.
 * @regmap: Pointer to base address of reset register address.
 * @rcdev: Reset controller device.
 * @desc: Pointer to description of the reset controller.
 */
struct mtk_clk_rst_data {
	struct regmap *regmap;
	struct reset_controller_dev rcdev;
	const struct mtk_clk_rst_desc *desc;
};

/**
 * mtk_register_reset_controller - Register mediatek clock reset controller with device
 * @np: Pointer to device.
 * @desc: Constant pointer to description of clock reset.
 *
 * Return: 0 on success and errorno otherwise.
 */
int mtk_register_reset_controller_with_dev(struct device *dev,
					   const struct mtk_clk_rst_desc *desc);

#endif /* __DRV_CLK_MTK_RESET_H */
