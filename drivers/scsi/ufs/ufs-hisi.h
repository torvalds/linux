/*
 * Copyright (c) 2017, HiSilicon. All rights reserved.
 *
 * Released under the GPLv2 only.
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef UFS_HISI_H_
#define UFS_HISI_H_

#define HBRN8_POLL_TOUT_MS	1000

/*
 * ufs sysctrl specific define
 */
#define PSW_POWER_CTRL	(0x04)
#define PHY_ISO_EN	(0x08)
#define HC_LP_CTRL	(0x0C)
#define PHY_CLK_CTRL	(0x10)
#define PSW_CLK_CTRL	(0x14)
#define CLOCK_GATE_BYPASS	(0x18)
#define RESET_CTRL_EN	(0x1C)
#define UFS_SYSCTRL	(0x5C)
#define UFS_DEVICE_RESET_CTRL	(0x60)

#define BIT_UFS_PSW_ISO_CTRL		(1 << 16)
#define BIT_UFS_PSW_MTCMOS_EN		(1 << 0)
#define BIT_UFS_REFCLK_ISO_EN		(1 << 16)
#define BIT_UFS_PHY_ISO_CTRL		(1 << 0)
#define BIT_SYSCTRL_LP_ISOL_EN		(1 << 16)
#define BIT_SYSCTRL_PWR_READY		(1 << 8)
#define BIT_SYSCTRL_REF_CLOCK_EN	(1 << 24)
#define MASK_SYSCTRL_REF_CLOCK_SEL	(0x3 << 8)
#define MASK_SYSCTRL_CFG_CLOCK_FREQ	(0xFF)
#define UFS_FREQ_CFG_CLK                (0x39)
#define BIT_SYSCTRL_PSW_CLK_EN		(1 << 4)
#define MASK_UFS_CLK_GATE_BYPASS	(0x3F)
#define BIT_SYSCTRL_LP_RESET_N		(1 << 0)
#define BIT_UFS_REFCLK_SRC_SEl		(1 << 0)
#define MASK_UFS_SYSCRTL_BYPASS		(0x3F << 16)
#define MASK_UFS_DEVICE_RESET		(0x1 << 16)
#define BIT_UFS_DEVICE_RESET		(0x1)

/*
 * M-TX Configuration Attributes for Hixxxx
 */
#define MPHY_TX_FSM_STATE	0x41
#define TX_FSM_HIBERN8	0x1

/*
 * Hixxxx UFS HC specific Registers
 */
enum {
	UFS_REG_OCPTHRTL = 0xc0,
	UFS_REG_OOCPR    = 0xc4,

	UFS_REG_CDACFG   = 0xd0,
	UFS_REG_CDATX1   = 0xd4,
	UFS_REG_CDATX2   = 0xd8,
	UFS_REG_CDARX1   = 0xdc,
	UFS_REG_CDARX2   = 0xe0,
	UFS_REG_CDASTA   = 0xe4,

	UFS_REG_LBMCFG   = 0xf0,
	UFS_REG_LBMSTA   = 0xf4,
	UFS_REG_UFSMODE  = 0xf8,

	UFS_REG_HCLKDIV  = 0xfc,
};

/* AHIT - Auto-Hibernate Idle Timer */
#define UFS_AHIT_AH8ITV_MASK	0x3FF

/* REG UFS_REG_OCPTHRTL definition */
#define UFS_HCLKDIV_NORMAL_VALUE	0xE4

/* vendor specific pre-defined parameters */
#define SLOW	1
#define FAST	2

#define UFS_HISI_LIMIT_NUM_LANES_RX	2
#define UFS_HISI_LIMIT_NUM_LANES_TX	2
#define UFS_HISI_LIMIT_HSGEAR_RX	UFS_HS_G3
#define UFS_HISI_LIMIT_HSGEAR_TX	UFS_HS_G3
#define UFS_HISI_LIMIT_PWMGEAR_RX	UFS_PWM_G4
#define UFS_HISI_LIMIT_PWMGEAR_TX	UFS_PWM_G4
#define UFS_HISI_LIMIT_RX_PWR_PWM	SLOW_MODE
#define UFS_HISI_LIMIT_TX_PWR_PWM	SLOW_MODE
#define UFS_HISI_LIMIT_RX_PWR_HS	FAST_MODE
#define UFS_HISI_LIMIT_TX_PWR_HS	FAST_MODE
#define UFS_HISI_LIMIT_HS_RATE	PA_HS_MODE_B
#define UFS_HISI_LIMIT_DESIRED_MODE	FAST

struct ufs_hisi_host {
	struct ufs_hba *hba;
	void __iomem *ufs_sys_ctrl;

	struct reset_control	*rst;

	uint64_t caps;

	bool in_suspend;
};

#define ufs_sys_ctrl_writel(host, val, reg)                                    \
	writel((val), (host)->ufs_sys_ctrl + (reg))
#define ufs_sys_ctrl_readl(host, reg) readl((host)->ufs_sys_ctrl + (reg))
#define ufs_sys_ctrl_set_bits(host, mask, reg)                                 \
	ufs_sys_ctrl_writel(                                                   \
		(host), ((mask) | (ufs_sys_ctrl_readl((host), (reg)))), (reg))
#define ufs_sys_ctrl_clr_bits(host, mask, reg)                                 \
	ufs_sys_ctrl_writel((host),                                            \
			    ((~(mask)) & (ufs_sys_ctrl_readl((host), (reg)))), \
			    (reg))
#endif /* UFS_HISI_H_ */
