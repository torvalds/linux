/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Rockchip UFS Host Controller driver
 *
 * Copyright (C) 2025 Rockchip Electronics Co., Ltd.
 */

#ifndef _UFS_ROCKCHIP_H_
#define _UFS_ROCKCHIP_H_

#define SEL_TX_LANE0 0x0
#define SEL_TX_LANE1 0x1
#define SEL_TX_LANE2 0x2
#define SEL_TX_LANE3 0x3
#define SEL_RX_LANE0 0x4
#define SEL_RX_LANE1 0x5
#define SEL_RX_LANE2 0x6
#define SEL_RX_LANE3 0x7

#define VND_TX_CLK_PRD                  0xAA
#define VND_TX_CLK_PRD_EN               0xA9
#define VND_TX_LINERESET_PVALUE2        0xAB
#define VND_TX_LINERESET_PVALUE1        0xAC
#define VND_TX_LINERESET_VALUE          0xAD
#define VND_TX_BASE_NVALUE              0x93
#define VND_TX_TASE_VALUE               0x94
#define VND_TX_POWER_SAVING_CTRL        0x7F
#define VND_RX_CLK_PRD                  0x12
#define VND_RX_CLK_PRD_EN               0x11
#define VND_RX_LINERESET_PVALUE2        0x1B
#define VND_RX_LINERESET_PVALUE1        0x1C
#define VND_RX_LINERESET_VALUE          0x1D
#define VND_RX_LINERESET_OPTION         0x25
#define VND_RX_POWER_SAVING_CTRL        0x2F
#define VND_RX_SAVE_DET_CTRL            0x1E

#define CMN_REG23                       0x8C
#define CMN_REG25                       0x94
#define TRSV0_REG08                     0xE0
#define TRSV1_REG08                     0x220
#define TRSV0_REG14                     0x110
#define TRSV1_REG14                     0x250
#define TRSV0_REG15                     0x134
#define TRSV1_REG15                     0x274
#define TRSV0_REG16                     0x128
#define TRSV1_REG16                     0x268
#define TRSV0_REG17                     0x12C
#define TRSV1_REG17                     0x26c
#define TRSV0_REG18                     0x120
#define TRSV1_REG18                     0x260
#define TRSV0_REG29                     0x164
#define TRSV1_REG29                     0x2A4
#define TRSV0_REG2E                     0x178
#define TRSV1_REG2E                     0x2B8
#define TRSV0_REG3C                     0x1B0
#define TRSV1_REG3C                     0x2F0
#define TRSV0_REG3D                     0x1B4
#define TRSV1_REG3D                     0x2F4

#define MPHY_CFG                        0x200
#define MPHY_CFG_ENABLE                 0x40
#define MPHY_CFG_DISABLE                0x0

#define MIB_T_DBG_CPORT_TX_ENDIAN       0xc022
#define MIB_T_DBG_CPORT_RX_ENDIAN       0xc023

struct ufs_rockchip_host {
	struct ufs_hba *hba;
	void __iomem *ufs_phy_ctrl;
	void __iomem *ufs_sys_ctrl;
	void __iomem *mphy_base;
	struct gpio_desc *rst_gpio;
	struct reset_control *rst;
	struct clk *ref_out_clk;
	struct clk_bulk_data *clks;
	uint64_t caps;
};

#define ufs_sys_writel(base, val, reg)                                    \
	writel((val), (base) + (reg))
#define ufs_sys_readl(base, reg) readl((base) + (reg))
#define ufs_sys_set_bits(base, mask, reg)                                 \
	ufs_sys_writel(                                                   \
		(base), ((mask) | (ufs_sys_readl((base), (reg)))), (reg))
#define ufs_sys_ctrl_clr_bits(base, mask, reg)                                 \
	ufs_sys_writel((base),                                            \
			    ((~(mask)) & (ufs_sys_readl((base), (reg)))), \
			    (reg))

#endif /* _UFS_ROCKCHIP_H_ */
