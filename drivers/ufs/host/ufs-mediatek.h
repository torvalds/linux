/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 MediaTek Inc.
 */

#ifndef _UFS_MEDIATEK_H
#define _UFS_MEDIATEK_H

#include <linux/bitops.h>

/*
 * MCQ define and struct
 */
#define UFSHCD_MAX_Q_NR 8
#define MTK_MCQ_INVALID_IRQ	0xFFFF

/* REG_UFS_MMIO_OPT_CTRL_0 160h */
#define EHS_EN                  BIT(0)
#define PFM_IMPV                BIT(1)
#define MCQ_MULTI_INTR_EN       BIT(2)
#define MCQ_CMB_INTR_EN         BIT(3)
#define MCQ_AH8                 BIT(4)

#define MCQ_INTR_EN_MSK         (MCQ_MULTI_INTR_EN | MCQ_CMB_INTR_EN)

/*
 * Vendor specific UFSHCI Registers
 */
#define REG_UFS_XOUFS_CTRL          0x140
#define REG_UFS_REFCLK_CTRL         0x144
#define REG_UFS_MMIO_OPT_CTRL_0     0x160
#define REG_UFS_EXTREG              0x2100
#define REG_UFS_MPHYCTRL            0x2200
#define REG_UFS_MTK_IP_VER          0x2240
#define REG_UFS_REJECT_MON          0x22AC
#define REG_UFS_DEBUG_SEL           0x22C0
#define REG_UFS_PROBE               0x22C8
#define REG_UFS_DEBUG_SEL_B0        0x22D0
#define REG_UFS_DEBUG_SEL_B1        0x22D4
#define REG_UFS_DEBUG_SEL_B2        0x22D8
#define REG_UFS_DEBUG_SEL_B3        0x22DC

#define REG_UFS_MTK_SQD             0x2800
#define REG_UFS_MTK_SQIS            0x2814
#define REG_UFS_MTK_CQD             0x281C
#define REG_UFS_MTK_CQIS            0x2824

#define REG_UFS_MCQ_STRIDE          0x30

/*
 * Ref-clk control
 *
 * Values for register REG_UFS_REFCLK_CTRL
 */
#define REFCLK_RELEASE              0x0
#define REFCLK_REQUEST              BIT(0)
#define REFCLK_ACK                  BIT(1)

#define REFCLK_REQ_TIMEOUT_US       3000
#define REFCLK_DEFAULT_WAIT_US      32

/*
 * Other attributes
 */
#define VS_DEBUGCLOCKENABLE         0xD0A1
#define VS_SAVEPOWERCONTROL         0xD0A6
#define VS_UNIPROPOWERDOWNCONTROL   0xD0A8

/*
 * Vendor specific link state
 */
enum {
	VS_LINK_DISABLED            = 0,
	VS_LINK_DOWN                = 1,
	VS_LINK_UP                  = 2,
	VS_LINK_HIBERN8             = 3,
	VS_LINK_LOST                = 4,
	VS_LINK_CFG                 = 5,
};

/*
 * Vendor specific host controller state
 */
enum {
	VS_HCE_RESET                = 0,
	VS_HCE_BASE                 = 1,
	VS_HCE_OOCPR_WAIT           = 2,
	VS_HCE_DME_RESET            = 3,
	VS_HCE_MIDDLE               = 4,
	VS_HCE_DME_ENABLE           = 5,
	VS_HCE_DEFAULTS             = 6,
	VS_HIB_IDLEEN               = 7,
	VS_HIB_ENTER                = 8,
	VS_HIB_ENTER_CONF           = 9,
	VS_HIB_MIDDLE               = 10,
	VS_HIB_WAITTIMER            = 11,
	VS_HIB_EXIT_CONF            = 12,
	VS_HIB_EXIT                 = 13,
};

/*
 * VS_DEBUGCLOCKENABLE
 */
enum {
	TX_SYMBOL_CLK_REQ_FORCE = 5,
};

/*
 * VS_SAVEPOWERCONTROL
 */
enum {
	RX_SYMBOL_CLK_GATE_EN   = 0,
	SYS_CLK_GATE_EN         = 2,
	TX_CLK_GATE_EN          = 3,
};

/*
 * Host capability
 */
enum ufs_mtk_host_caps {
	UFS_MTK_CAP_BOOST_CRYPT_ENGINE         = 1 << 0,
	UFS_MTK_CAP_VA09_PWR_CTRL              = 1 << 1,
	UFS_MTK_CAP_DISABLE_AH8                = 1 << 2,
	UFS_MTK_CAP_BROKEN_VCC                 = 1 << 3,

	/*
	 * Override UFS_MTK_CAP_BROKEN_VCC's behavior to
	 * allow vccqx upstream to enter LPM
	 */
	UFS_MTK_CAP_ALLOW_VCCQX_LPM            = 1 << 5,
	UFS_MTK_CAP_PMC_VIA_FASTAUTO           = 1 << 6,
	UFS_MTK_CAP_TX_SKEW_FIX                = 1 << 7,
	UFS_MTK_CAP_DISABLE_MCQ                = 1 << 8,
	/* Control MTCMOS with RTFF */
	UFS_MTK_CAP_RTFF_MTCMOS                = 1 << 9,

	UFS_MTK_CAP_MCQ_BROKEN_RTC             = 1 << 10,
};

struct ufs_mtk_crypt_cfg {
	struct regulator *reg_vcore;
	struct clk *clk_crypt_perf;
	struct clk *clk_crypt_mux;
	struct clk *clk_crypt_lp;
	int vcore_volt;
};

struct ufs_mtk_clk {
	struct ufs_clk_info *ufs_sel_clki; /* Mux */
	struct ufs_clk_info *ufs_sel_max_clki; /* Max src */
	struct ufs_clk_info *ufs_sel_min_clki; /* Min src */
	struct ufs_clk_info *ufs_fde_clki; /* Mux */
	struct ufs_clk_info *ufs_fde_max_clki; /* Max src */
	struct ufs_clk_info *ufs_fde_min_clki; /* Min src */
	struct regulator *reg_vcore;
	int vcore_volt;
};

struct ufs_mtk_hw_ver {
	u8 step;
	u8 minor;
	u8 major;
};

struct ufs_mtk_mcq_intr_info {
	struct ufs_hba *hba;
	u32 irq;
	u8 qid;
};

struct ufs_mtk_host {
	struct phy *mphy;
	struct regulator *reg_va09;
	struct reset_control *hci_reset;
	struct reset_control *unipro_reset;
	struct reset_control *crypto_reset;
	struct reset_control *mphy_reset;
	struct ufs_hba *hba;
	struct ufs_mtk_crypt_cfg *crypt;
	struct ufs_mtk_clk mclk;
	struct ufs_mtk_hw_ver hw_ver;
	enum ufs_mtk_host_caps caps;
	bool mphy_powered_on;
	bool unipro_lpm;
	bool ref_clk_enabled;
	bool clk_scale_up;
	u16 ref_clk_ungating_wait_us;
	u16 ref_clk_gating_wait_us;
	u32 ip_ver;
	bool legacy_ip_ver;

	bool mcq_set_intr;
	bool is_mcq_intr_enabled;
	int mcq_nr_intr;
	struct ufs_mtk_mcq_intr_info mcq_intr_info[UFSHCD_MAX_Q_NR];
	struct device *phy_dev;
};

/* MTK delay of autosuspend: 500 ms */
#define MTK_RPM_AUTOSUSPEND_DELAY_MS 500

/* MTK RTT support number */
#define MTK_MAX_NUM_RTT 2

/* UFSHCI MTK ip version value */
enum {
	/* UFSHCI 3.1 */
	IP_VER_MT6983    = 0x10360000,
	IP_VER_MT6878    = 0x10420200,

	/* UFSHCI 4.0 */
	IP_VER_MT6897    = 0x10440000,
	IP_VER_MT6989    = 0x10450000,
	IP_VER_MT6899    = 0x10450100,
	IP_VER_MT6991_A0 = 0x10460000,
	IP_VER_MT6991_B0 = 0x10470000,
	IP_VER_MT6993    = 0x10480000,

	IP_VER_NONE      = 0xFFFFFFFF
};

enum ip_ver_legacy {
	IP_LEGACY_VER_MT6781 = 0x10380000,
	IP_LEGACY_VER_MT6879 = 0x10360000,
	IP_LEGACY_VER_MT6893 = 0x20160706
};

#endif /* !_UFS_MEDIATEK_H */
