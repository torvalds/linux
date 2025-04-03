/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * UFS Host Controller driver for Exynos specific extensions
 *
 * Copyright (C) 2014-2015 Samsung Electronics Co., Ltd.
 *
 */

#ifndef _UFS_EXYNOS_H_
#define _UFS_EXYNOS_H_

/*
 * Component registers
 */

#define COMP_CLK_PERIOD	0x44

/*
 * UNIPRO registers
 */
#define UNIPRO_DBG_FORCE_DME_CTRL_STATE		0x150

/*
 * MIBs for PA debug registers
 */
#define PA_DBG_CLK_PERIOD	0x9514
#define PA_DBG_TXPHY_CFGUPDT	0x9518
#define PA_DBG_RXPHY_CFGUPDT	0x9519
#define PA_DBG_MODE		0x9529
#define PA_DBG_SKIP_RESET_PHY	0x9539
#define PA_DBG_AUTOMODE_THLD	0x9536
#define PA_DBG_OV_TM		0x9540
#define PA_DBG_SKIP_LINE_RESET	0x9541
#define PA_DBG_LINE_RESET_REQ	0x9543
#define PA_DBG_OPTION_SUITE	0x9564
#define PA_DBG_OPTION_SUITE_DYN	0x9565

/*
 * Note: GS101_DBG_OPTION offsets below differ from the TRM
 * but match the downstream driver. Following the TRM
 * results in non-functioning UFS.
 */
#define PA_GS101_DBG_OPTION_SUITE1 0x956a
#define PA_GS101_DBG_OPTION_SUITE2 0x956d

/*
 * MIBs for Transport Layer debug registers
 */
#define T_DBG_SKIP_INIT_HIBERN8_EXIT	0xc001

/*
 * Exynos MPHY attributes
 */
#define TX_LINERESET_N_VAL	0x0277
#define TX_LINERESET_N(v)	(((v) >> 10) & 0xFF)
#define TX_LINERESET_P_VAL	0x027D
#define TX_LINERESET_P(v)	(((v) >> 12) & 0xFF)
#define TX_OV_SLEEP_CNT_TIMER	0x028E
#define TX_OV_H8_ENTER_EN	(1 << 7)
#define TX_OV_SLEEP_CNT(v)	(((v) >> 5) & 0x7F)
#define TX_HIGH_Z_CNT_11_08	0x028C
#define TX_HIGH_Z_CNT_H(v)	(((v) >> 8) & 0xF)
#define TX_HIGH_Z_CNT_07_00	0x028D
#define TX_HIGH_Z_CNT_L(v)	((v) & 0xFF)
#define TX_BASE_NVAL_07_00	0x0293
#define TX_BASE_NVAL_L(v)	((v) & 0xFF)
#define TX_BASE_NVAL_15_08	0x0294
#define TX_BASE_NVAL_H(v)	(((v) >> 8) & 0xFF)
#define TX_GRAN_NVAL_07_00	0x0295
#define TX_GRAN_NVAL_L(v)	((v) & 0xFF)
#define TX_GRAN_NVAL_10_08	0x0296
#define TX_GRAN_NVAL_H(v)	(((v) >> 8) & 0x3)

#define VND_TX_CLK_PRD		0xAA
#define VND_TX_CLK_PRD_EN	0xA9
#define VND_TX_LINERESET_PVALUE0	0xAD
#define VND_TX_LINERESET_PVALUE1	0xAC
#define VND_TX_LINERESET_PVALUE2	0xAB

#define TX_LINE_RESET_TIME	3200

#define VND_RX_CLK_PRD		0x12
#define VND_RX_CLK_PRD_EN	0x11
#define VND_RX_LINERESET_VALUE0	0x1D
#define VND_RX_LINERESET_VALUE1	0x1C
#define VND_RX_LINERESET_VALUE2	0x1B

#define RX_LINE_RESET_TIME	1000

#define RX_FILLER_ENABLE	0x0316
#define RX_FILLER_EN		(1 << 1)
#define RX_LINERESET_VAL	0x0317
#define RX_LINERESET(v)	(((v) >> 12) & 0xFF)
#define RX_LCC_IGNORE		0x0318
#define RX_SYNC_MASK_LENGTH	0x0321
#define RX_HIBERN8_WAIT_VAL_BIT_20_16	0x0331
#define RX_HIBERN8_WAIT_VAL_BIT_15_08	0x0332
#define RX_HIBERN8_WAIT_VAL_BIT_07_00	0x0333
#define RX_OV_SLEEP_CNT_TIMER	0x0340
#define RX_OV_SLEEP_CNT(v)	(((v) >> 6) & 0x1F)
#define RX_OV_STALL_CNT_TIMER	0x0341
#define RX_OV_STALL_CNT(v)	(((v) >> 4) & 0xFF)
#define RX_BASE_NVAL_07_00	0x0355
#define RX_BASE_NVAL_L(v)	((v) & 0xFF)
#define RX_BASE_NVAL_15_08	0x0354
#define RX_BASE_NVAL_H(v)	(((v) >> 8) & 0xFF)
#define RX_GRAN_NVAL_07_00	0x0353
#define RX_GRAN_NVAL_L(v)	((v) & 0xFF)
#define RX_GRAN_NVAL_10_08	0x0352
#define RX_GRAN_NVAL_H(v)	(((v) >> 8) & 0x3)

#define CMN_PWM_CLK_CTRL	0x0402
#define PWM_CLK_CTRL_MASK	0x3

#define IATOVAL_NSEC		20000	/* unit: ns */
#define UNIPRO_PCLK_PERIOD(ufs) (NSEC_PER_SEC / ufs->pclk_rate)

struct exynos_ufs;

/* vendor specific pre-defined parameters */
#define SLOW 1
#define FAST 2

#define RX_ADV_FINE_GRAN_SUP_EN	0x1
#define RX_ADV_FINE_GRAN_STEP_VAL	0x3
#define RX_ADV_MIN_ACTV_TIME_CAP	0x9

#define PA_GRANULARITY_VAL	0x6
#define PA_TACTIVATE_VAL	0x3
#define PA_HIBERN8TIME_VAL	0x20

#define PCLK_AVAIL_MIN	70000000
#define PCLK_AVAIL_MAX	267000000

struct exynos_ufs_uic_attr {
	/* TX Attributes */
	unsigned int tx_trailingclks;
	unsigned int tx_dif_p_nsec;
	unsigned int tx_dif_n_nsec;
	unsigned int tx_high_z_cnt_nsec;
	unsigned int tx_base_unit_nsec;
	unsigned int tx_gran_unit_nsec;
	unsigned int tx_sleep_cnt;
	unsigned int tx_min_activatetime;
	/* RX Attributes */
	unsigned int rx_filler_enable;
	unsigned int rx_dif_p_nsec;
	unsigned int rx_hibern8_wait_nsec;
	unsigned int rx_base_unit_nsec;
	unsigned int rx_gran_unit_nsec;
	unsigned int rx_sleep_cnt;
	unsigned int rx_stall_cnt;
	unsigned int rx_hs_g1_sync_len_cap;
	unsigned int rx_hs_g2_sync_len_cap;
	unsigned int rx_hs_g3_sync_len_cap;
	unsigned int rx_hs_g1_prep_sync_len_cap;
	unsigned int rx_hs_g2_prep_sync_len_cap;
	unsigned int rx_hs_g3_prep_sync_len_cap;
	/* Common Attributes */
	unsigned int cmn_pwm_clk_ctrl;
	/* Internal Attributes */
	unsigned int pa_dbg_clk_period_off;
	unsigned int pa_dbg_opt_suite1_val;
	unsigned int pa_dbg_opt_suite1_off;
	unsigned int pa_dbg_opt_suite2_val;
	unsigned int pa_dbg_opt_suite2_off;
	/* Changeable Attributes */
	unsigned int rx_adv_fine_gran_sup_en;
	unsigned int rx_adv_fine_gran_step;
	unsigned int rx_min_actv_time_cap;
	unsigned int rx_hibern8_time_cap;
	unsigned int rx_adv_min_actv_time_cap;
	unsigned int rx_adv_hibern8_time_cap;
	unsigned int pa_granularity;
	unsigned int pa_tactivate;
	unsigned int pa_hibern8time;
};

struct exynos_ufs_drv_data {
	const struct ufs_hba_variant_ops *vops;
	struct exynos_ufs_uic_attr *uic_attr;
	unsigned int quirks;
	unsigned int opts;
	u32 iocc_mask;
	/* SoC's specific operations */
	int (*drv_init)(struct exynos_ufs *ufs);
	int (*pre_link)(struct exynos_ufs *ufs);
	int (*post_link)(struct exynos_ufs *ufs);
	int (*pre_pwr_change)(struct exynos_ufs *ufs,
				struct ufs_pa_layer_attr *pwr);
	int (*post_pwr_change)(struct exynos_ufs *ufs,
			       const struct ufs_pa_layer_attr *pwr);
	int (*pre_hce_enable)(struct exynos_ufs *ufs);
	int (*post_hce_enable)(struct exynos_ufs *ufs);
	int (*suspend)(struct exynos_ufs *ufs);
};

struct ufs_phy_time_cfg {
	u32 tx_linereset_p;
	u32 tx_linereset_n;
	u32 tx_high_z_cnt;
	u32 tx_base_n_val;
	u32 tx_gran_n_val;
	u32 tx_sleep_cnt;
	u32 rx_linereset;
	u32 rx_hibern8_wait;
	u32 rx_base_n_val;
	u32 rx_gran_n_val;
	u32 rx_sleep_cnt;
	u32 rx_stall_cnt;
};

struct exynos_ufs {
	struct ufs_hba *hba;
	struct phy *phy;
	void __iomem *reg_hci;
	void __iomem *reg_unipro;
	void __iomem *reg_ufsp;
	struct clk *clk_hci_core;
	struct clk *clk_unipro_main;
	struct clk *clk_apb;
	u32 pclk_rate;
	u32 pclk_div;
	u32 pclk_avail_min;
	u32 pclk_avail_max;
	unsigned long mclk_rate;
	int avail_ln_rx;
	int avail_ln_tx;
	int rx_sel_idx;
	struct ufs_pa_layer_attr dev_req_params;
	struct ufs_phy_time_cfg t_cfg;
	ktime_t entry_hibern8_t;
	const struct exynos_ufs_drv_data *drv_data;
	struct regmap *sysreg;
	u32 iocc_offset;
	u32 iocc_mask;
	u32 iocc_val;

	u32 opts;
#define EXYNOS_UFS_OPT_HAS_APB_CLK_CTRL		BIT(0)
#define EXYNOS_UFS_OPT_SKIP_CONNECTION_ESTAB	BIT(1)
#define EXYNOS_UFS_OPT_BROKEN_AUTO_CLK_CTRL	BIT(2)
#define EXYNOS_UFS_OPT_BROKEN_RX_SEL_IDX	BIT(3)
#define EXYNOS_UFS_OPT_USE_SW_HIBERN8_TIMER	BIT(4)
#define EXYNOS_UFS_OPT_SKIP_CONFIG_PHY_ATTR	BIT(5)
#define EXYNOS_UFS_OPT_UFSPR_SECURE		BIT(6)
#define EXYNOS_UFS_OPT_TIMER_TICK_SELECT	BIT(7)
};

#define for_each_ufs_rx_lane(ufs, i) \
	for (i = (ufs)->rx_sel_idx; \
		i < (ufs)->rx_sel_idx + (ufs)->avail_ln_rx; i++)
#define for_each_ufs_tx_lane(ufs, i) \
	for (i = 0; i < (ufs)->avail_ln_tx; i++)

#define EXYNOS_UFS_MMIO_FUNC(name)					  \
static inline void name##_writel(struct exynos_ufs *ufs, u32 val, u32 reg)\
{									  \
	writel(val, ufs->reg_##name + reg);				  \
}									  \
									  \
static inline u32 name##_readl(struct exynos_ufs *ufs, u32 reg)		  \
{									  \
	return readl(ufs->reg_##name + reg);				  \
}

EXYNOS_UFS_MMIO_FUNC(hci);
EXYNOS_UFS_MMIO_FUNC(unipro);
EXYNOS_UFS_MMIO_FUNC(ufsp);
#undef EXYNOS_UFS_MMIO_FUNC

long exynos_ufs_calc_time_cntr(struct exynos_ufs *, long);

static inline void exynos_ufs_enable_ov_tm(struct ufs_hba *hba)
{
	ufshcd_dme_set(hba, UIC_ARG_MIB(PA_DBG_OV_TM), true);
}

static inline void exynos_ufs_disable_ov_tm(struct ufs_hba *hba)
{
	ufshcd_dme_set(hba, UIC_ARG_MIB(PA_DBG_OV_TM), false);
}

static inline void exynos_ufs_enable_dbg_mode(struct ufs_hba *hba)
{
	ufshcd_dme_set(hba, UIC_ARG_MIB(PA_DBG_MODE), true);
}

static inline void exynos_ufs_disable_dbg_mode(struct ufs_hba *hba)
{
	ufshcd_dme_set(hba, UIC_ARG_MIB(PA_DBG_MODE), false);
}

#endif /* _UFS_EXYNOS_H_ */
