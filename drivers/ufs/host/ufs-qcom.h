/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2013-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef UFS_QCOM_H_
#define UFS_QCOM_H_

#include <linux/reset-controller.h>
#include <linux/reset.h>
#include <linux/phy/phy.h>
#include <linux/pm_qos.h>
#include <linux/notifier.h>
#include <ufs/ufshcd.h>
#include <ufs/unipro.h>

#define MAX_UFS_QCOM_HOSTS	2
#define MAX_U32                 (~(u32)0)
#define MPHY_TX_FSM_STATE       0x41
#define MPHY_RX_FSM_STATE       0xC1
#define TX_FSM_HIBERN8          0x1
#define HBRN8_POLL_TOUT_MS      100
#define DEFAULT_CLK_RATE_HZ     1000000
#define BUS_VECTOR_NAME_LEN     32
#define MAX_SUPP_MAC		64

#define UFS_HW_VER_MAJOR_MASK	GENMASK(31, 28)
#define UFS_HW_VER_MINOR_MASK	GENMASK(27, 16)
#define UFS_HW_VER_STEP_MASK	GENMASK(15, 0)

#define UFS_VENDOR_MICRON	0x12C

#define SLOW 1
#define FAST 2

enum ufs_qcom_phy_submode {
	UFS_QCOM_PHY_SUBMODE_NON_G4,
	UFS_QCOM_PHY_SUBMODE_G4,
	UFS_QCOM_PHY_SUBMODE_G5,
};

enum ufs_qcom_ber_mode {
	UFS_QCOM_BER_MODE_G1_G4,
	UFS_QCOM_BER_MODE_G5,
	UFS_QCOM_BER_MODE_MAX,
};

#define UFS_QCOM_LIMIT_NUM_LANES_RX	2
#define UFS_QCOM_LIMIT_NUM_LANES_TX	2
#define UFS_QCOM_LIMIT_HSGEAR_RX	UFS_HS_G4
#define UFS_QCOM_LIMIT_HSGEAR_TX	UFS_HS_G4
#define UFS_QCOM_LIMIT_PWMGEAR_RX	UFS_PWM_G4
#define UFS_QCOM_LIMIT_PWMGEAR_TX	UFS_PWM_G4
#define UFS_QCOM_LIMIT_RX_PWR_PWM	SLOW_MODE
#define UFS_QCOM_LIMIT_TX_PWR_PWM	SLOW_MODE
#define UFS_QCOM_LIMIT_RX_PWR_HS	FAST_MODE
#define UFS_QCOM_LIMIT_TX_PWR_HS	FAST_MODE
#define UFS_QCOM_LIMIT_HS_RATE		PA_HS_MODE_B
#define UFS_QCOM_LIMIT_DESIRED_MODE	FAST
#define UFS_QCOM_LIMIT_PHY_SUBMODE	UFS_QCOM_PHY_SUBMODE_G4
#define UFS_MEM_REG_PA_ERR_CODE	0xCC

/* default value of auto suspend is 3 seconds */
#define UFS_QCOM_AUTO_SUSPEND_DELAY	3000
#define UFS_QCOM_CLK_GATING_DELAY_MS_PWR_SAVE	10
#define UFS_QCOM_CLK_GATING_DELAY_MS_PERF	50

/* QCOM UFS host controller vendor specific registers */
enum {
	REG_UFS_SYS1CLK_1US                 = 0xC0,
	REG_UFS_TX_SYMBOL_CLK_NS_US         = 0xC4,
	REG_UFS_LOCAL_PORT_ID_REG           = 0xC8,
	REG_UFS_PA_ERR_CODE                 = 0xCC,
	/* On older UFS revisions, this register is called "RETRY_TIMER_REG" */
	REG_UFS_PARAM0                      = 0xD0,
	/* On older UFS revisions, this register is called "REG_UFS_PA_LINK_STARTUP_TIMER" */
	REG_UFS_CFG0                        = 0xD8,
	REG_UFS_CFG1                        = 0xDC,
	REG_UFS_CFG2                        = 0xE0,
	REG_UFS_HW_VERSION                  = 0xE4,

	UFS_TEST_BUS				= 0xE8,
	UFS_TEST_BUS_CTRL_0			= 0xEC,
	UFS_TEST_BUS_CTRL_1			= 0xF0,
	UFS_TEST_BUS_CTRL_2			= 0xF4,
	UFS_UNIPRO_CFG				= 0xF8,

	/*
	 * QCOM UFS host controller vendor specific registers
	 * added in HW Version 3.0.0
	 */
	UFS_AH8_CFG				= 0xFC,
	UFS_RD_REG_MCQ			= 0xD00,
	UFS_MEM_ICE				= 0x2600,
	REG_UFS_DEBUG_SPARE_CFG			= 0x284C,

	REG_UFS_CFG3				= 0x271C,
};

/* QCOM UFS host controller vendor specific debug registers */
enum {
	UFS_DBG_RD_REG_UAWM			= 0x100,
	UFS_DBG_RD_REG_UARM			= 0x200,
	UFS_DBG_RD_REG_TXUC			= 0x300,
	UFS_DBG_RD_REG_RXUC			= 0x400,
	UFS_DBG_RD_REG_DFC			= 0x500,
	UFS_DBG_RD_REG_TRLUT			= 0x600,
	UFS_DBG_RD_REG_TMRLUT			= 0x700,
	UFS_UFS_DBG_RD_REG_OCSC			= 0x800,

	UFS_UFS_DBG_RAM_CTL			= 0x1000,
	UFS_UFS_DBG_RAM_RD_FATA_DWn		= 0x1024,
	UFS_UFS_DBG_RD_DESC_RAM			= 0x1500,
	UFS_UFS_DBG_RD_PRDT_RAM			= 0x1700,
	UFS_UFS_DBG_RD_RESP_RAM			= 0x1800,
	UFS_UFS_DBG_RD_EDTL_RAM			= 0x1900,
};

/* QCOM UFS host controller vendor specific H8 count registers */
enum {
	REG_UFS_HW_H8_ENTER_CNT				= 0x2700,
	REG_UFS_SW_H8_ENTER_CNT				= 0x2704,
	REG_UFS_SW_AFTER_HW_H8_ENTER_CNT	= 0x2708,
	REG_UFS_HW_H8_EXIT_CNT				= 0x270C,
	REG_UFS_SW_H8_EXIT_CNT				= 0x2710,
};

enum {
	UFS_MEM_CQIS_VS		= 0x8,
};

#define UFS_CNTLR_2_x_x_VEN_REGS_OFFSET(x)	(0x000 + x)
#define UFS_CNTLR_3_x_x_VEN_REGS_OFFSET(x)	(0x400 + x)

/* bit definitions for REG_UFS_CFG0 register */
#define QUNIPRO_G4_SEL		BIT(5)
#define HCI_UAWM_OOO_DIS	BIT(0)

/* bit definitions for REG_UFS_CFG1 register */
#define QUNIPRO_SEL		BIT(0)
#define UFS_PHY_SOFT_RESET	BIT(1)
#define UTP_DBG_RAMS_EN		BIT(17)
#define TEST_BUS_EN		BIT(18)
#define TEST_BUS_SEL		GENMASK(22, 19)
#define UFS_REG_TEST_BUS_EN	BIT(30)

#define UFS_PHY_RESET_ENABLE	1
#define UFS_PHY_RESET_DISABLE	0

/* bit definitions for REG_UFS_CFG2 register */
#define UAWM_HW_CGC_EN		BIT(0)
#define UARM_HW_CGC_EN		BIT(1)
#define TXUC_HW_CGC_EN		BIT(2)
#define RXUC_HW_CGC_EN		BIT(3)
#define DFC_HW_CGC_EN		BIT(4)
#define TRLUT_HW_CGC_EN		BIT(5)
#define TMRLUT_HW_CGC_EN	BIT(6)
#define OCSC_HW_CGC_EN		BIT(7)

/* bit definition for UFS_UFS_TEST_BUS_CTRL_n */
#define TEST_BUS_SUB_SEL_MASK	GENMASK(4, 0)  /* All XXX_SEL fields are 5 bits wide */

#define REG_UFS_CFG2_CGC_EN_ALL (UAWM_HW_CGC_EN | UARM_HW_CGC_EN |\
				 TXUC_HW_CGC_EN | RXUC_HW_CGC_EN |\
				 DFC_HW_CGC_EN | TRLUT_HW_CGC_EN |\
				 TMRLUT_HW_CGC_EN | OCSC_HW_CGC_EN)

/* bit definitions for UFS_AH8_CFG register */
#define CC_UFS_HCLK_REQ_EN		BIT(1)
#define CC_UFS_SYS_CLK_REQ_EN		BIT(2)
#define CC_UFS_ICE_CORE_CLK_REQ_EN	BIT(3)
#define CC_UFS_UNIPRO_CORE_CLK_REQ_EN	BIT(4)
#define CC_UFS_AUXCLK_REQ_EN		BIT(5)

#define UNUSED_UNIPRO_CORE_CGC_EN	BIT(11)
#define UNUSED_UNIPRO_SYMB_CGC_EN	BIT(12)
#define UNUSED_UNIPRO_CLK_GATED	(UNUSED_UNIPRO_CORE_CGC_EN |\
					UNUSED_UNIPRO_SYMB_CGC_EN)

#define UFS_HW_CLK_CTRL_EN	(CC_UFS_SYS_CLK_REQ_EN |\
				 CC_UFS_ICE_CORE_CLK_REQ_EN |\
				 CC_UFS_UNIPRO_CORE_CLK_REQ_EN |\
				 CC_UFS_AUXCLK_REQ_EN)

/* UFS_MEM_PARAM0 register */
#define UFS_MAX_HS_GEAR_SHIFT	(4)
#define UFS_MAX_HS_GEAR_MASK	(0x7 << UFS_MAX_HS_GEAR_SHIFT)
#define UFS_QCOM_MAX_HS_GEAR(x) (((x) & UFS_MAX_HS_GEAR_MASK) >>\
				 UFS_MAX_HS_GEAR_SHIFT)

/* bit offset */
#define OFFSET_CLK_NS_REG		0xa

/* bit masks */
enum {
	MASK_UFS_PHY_SOFT_RESET             = 0x2,
};

enum ufs_qcom_phy_init_type {
	UFS_PHY_INIT_FULL,
	UFS_PHY_INIT_CFG_RESTORE,
};

/* QCOM UFS debug print bit mask */
#define UFS_QCOM_DBG_PRINT_REGS_EN	BIT(0)

#define MASK_TX_SYMBOL_CLK_1US_REG	GENMASK(9, 0)
#define MASK_CLK_NS_REG			GENMASK(23, 10)


/* QUniPro Vendor specific attributes */
#define PA_VS_CONFIG_REG1	0x9000
#define BIT_TX_EOB_COND         BIT(23)
#define PA_VS_CONFIG_REG2       0x9005
#define H8_ENTER_COND_OFFSET 0x6
#define H8_ENTER_COND_MASK GENMASK(7, 6)
#define BIT_RX_EOB_COND		BIT(5)
#define BIT_LINKCFG_WAIT_LL1_RX_CFG_RDY BIT(26)
#define SAVECONFIGTIME_MODE_MASK        0x6000
#define DME_VS_CORE_CLK_CTRL    0xD002


/* bit and mask definitions for DME_VS_CORE_CLK_CTRL attribute */
#define DME_VS_CORE_CLK_CTRL_CORE_CLK_DIV_EN_BIT		BIT(8)
#define DME_VS_CORE_CLK_CTRL_MAX_CORE_CLK_1US_CYCLES_MASK	0xFF

#define PA_VS_CLK_CFG_REG	0x9004
#define PA_VS_CLK_CFG_REG_MASK	0x1FF

#define PA_VS_CORE_CLK_40NS_CYCLES	0x9007
#define PA_VS_CORE_CLK_40NS_CYCLES_MASK	0x3F

#define DL_VS_CLK_CFG		0xA00B
#define DL_VS_CLK_CFG_MASK	0x3FF

#define DME_VS_CORE_CLK_CTRL	0xD002
/* bit and mask definitions for DME_VS_CORE_CLK_CTRL attribute */
#define DME_VS_CORE_CLK_CTRL_MAX_CORE_CLK_1US_CYCLES_MASK_V4	0xFFF
#define DME_VS_CORE_CLK_CTRL_MAX_CORE_CLK_1US_CYCLES_OFFSET_V4	0x10
#define DME_VS_CORE_CLK_CTRL_MAX_CORE_CLK_1US_CYCLES_MASK	0xFF
#define DME_VS_CORE_CLK_CTRL_CORE_CLK_DIV_EN_BIT		BIT(8)
#define DME_VS_CORE_CLK_CTRL_DME_HW_CGC_EN			BIT(9)

/* Device Quirks */
/*
 * Some ufs devices may need more time to be in hibern8 before exiting.
 * Enable this quirk to give it an additional 100us.
 */
#define UFS_DEVICE_QUIRK_PA_HIBER8TIME          (1 << 15)

/*
 * Some ufs device vendors need a different TSync length.
 * Enable this quirk to give an additional TX_HS_SYNC_LENGTH.
 */
#define UFS_DEVICE_QUIRK_PA_TX_HSG1_SYNC_LENGTH (1 << 16)

static inline void
ufs_qcom_get_controller_revision(struct ufs_hba *hba,
				 u8 *major, u16 *minor, u16 *step)
{
	u32 ver = ufshcd_readl(hba, REG_UFS_HW_VERSION);

	*major = FIELD_GET(UFS_HW_VER_MAJOR_MASK, ver);
	*minor = FIELD_GET(UFS_HW_VER_MINOR_MASK, ver);
	*step = FIELD_GET(UFS_HW_VER_STEP_MASK, ver);
};

static inline void ufs_qcom_assert_reset(struct ufs_hba *hba)
{
	ufshcd_rmwl(hba, UFS_PHY_SOFT_RESET, FIELD_PREP(UFS_PHY_SOFT_RESET, UFS_PHY_RESET_ENABLE),
		    REG_UFS_CFG1);

	/*
	 * Make sure assertion of ufs phy reset is written to
	 * register before returning
	 */
	mb();
}

static inline void ufs_qcom_deassert_reset(struct ufs_hba *hba)
{
	ufshcd_rmwl(hba, UFS_PHY_SOFT_RESET, FIELD_PREP(UFS_PHY_SOFT_RESET, UFS_PHY_RESET_DISABLE),
		    REG_UFS_CFG1);

	/*
	 * Make sure de-assertion of ufs phy reset is written to
	 * register before returning
	 */
	mb();
}

struct ufs_qcom_bus_vote {
	uint32_t client_handle;
	uint32_t curr_vote;
	int min_bw_vote;
	int max_bw_vote;
	int saved_vote;
	bool is_max_bw_needed;
	struct device_attribute max_bus_bw;
};

/* Host controller hardware version: major.minor.step */
struct ufs_hw_version {
	u16 step;
	u16 minor;
	u8 major;
};

struct ufs_qcom_testbus {
	u8 select_major;
	u8 select_minor;
};

struct gpio_desc;

struct qcom_bus_vectors {
	uint32_t ab;
	uint32_t ib;
};

struct qcom_bus_path {
	unsigned int num_paths;
	struct qcom_bus_vectors *vec;
};

struct qcom_bus_scale_data {
	struct qcom_bus_path *usecase;
	unsigned int num_usecase;
	struct icc_path *ufs_ddr;
	struct icc_path *cpu_ufs;

	const char *name;
};

struct qos_cpu_group {
	cpumask_t mask;
	unsigned int *votes;
	struct dev_pm_qos_request *qos_req;
	bool voted;
	struct work_struct vwork;
	struct ufs_qcom_host *host;
	unsigned int curr_vote;
	bool perf_core;
};

struct ufs_qcom_qos_req {
	struct qos_cpu_group *qcg;
	unsigned int num_groups;
	struct workqueue_struct *workq;
};

/* Check for QOS_POWER when added to DT */
enum constraint {
	QOS_PERF,
	QOS_POWER,
	QOS_MAX,
};

enum ufs_qcom_therm_lvl {
	UFS_QCOM_LVL_NO_THERM, /* No thermal mitigation */
	UFS_QCOM_LVL_AGGR_THERM, /* Aggressive thermal mitigation */
	UFS_QCOM_LVL_MAX_THERM, /* Max thermal mitigation */
};

struct ufs_qcom_thermal {
	struct thermal_cooling_device *tcd;
	unsigned long curr_state;
};

/* Algorithm Selection */
#define STATIC_ALLOC_ALG1 0x0
#define FLOOR_BASED_ALG2 BIT(0)
#define INSTANTANEOUS_ALG3 BIT(1)

enum {
	REG_UFS_MEM_ICE_NUM_AES_CORES = 0x2608,
	REG_UFS_MEM_SHARED_ICE_CONFIG = 0x260C,
	REG_UFS_MEM_SHARED_ICE_ALG1_NUM_CORE = 0x2610,
	REG_UFS_MEM_SHARED_ICE_ALG2_NUM_CORE_0 = 0x2614,
	REG_UFS_MEM_SHARED_ICE_ALG2_NUM_TASK_0 = 0x2618,
	REG_UFS_MEM_SHARED_ICE_ALG2_NUM_CORE_1 = 0x261C,
	REG_UFS_MEM_SHARED_ICE_ALG2_NUM_TASK_1 = 0x2620,
	REG_UFS_MEM_SHARED_ICE_ALG2_NUM_CORE_2 = 0x2624,
	REG_UFS_MEM_SHARED_ICE_ALG2_NUM_TASK_2 = 0x2628,
	REG_UFS_MEM_SHARED_ICE_ALG2_NUM_CORE_3 = 0x262C,
	REG_UFS_MEM_SHARED_ICE_ALG2_NUM_TASK_3 = 0x2630,
	REG_UFS_MEM_SHARED_ICE_ALG2_NUM_CORE_4 = 0x2634,
	REG_UFS_MEM_SHARED_ICE_ALG2_NUM_TASK_4 = 0x2638,
	REG_UFS_MEM_SHARED_ICE_ALG2_NUM_CORE_5 = 0x263C,
	REG_UFS_MEM_SHARED_ICE_ALG2_NUM_TASK_5 = 0x2640,
	REG_UFS_MEM_SHARED_ICE_ALG2_NUM_CORE_6 = 0x2644,
	REG_UFS_MEM_SHARED_ICE_ALG2_NUM_TASK_6 = 0x2648,
	REG_UFS_MEM_SHARED_ICE_ALG2_NUM_CORE_7 = 0x264C,
	REG_UFS_MEM_SHARED_ICE_ALG2_NUM_TASK_7 = 0x2650,
	REG_UFS_MEM_SHARED_ICE_ALG2_NUM_CORE_8 = 0x2654,
	REG_UFS_MEM_SHARED_ICE_ALG2_NUM_TASK_8 = 0x2658,
	REG_UFS_MEM_SHARED_ICE_ALG2_NUM_CORE_9 = 0x265C,
	REG_UFS_MEM_SHARED_ICE_ALG2_NUM_TASK_9 = 0x2660,
	REG_UFS_MEM_SHARED_ICE_ALG3_NUM_CORE = 0x2664,
};

struct shared_ice_alg2_config {
	/* group names */
	char name[3];
	/*
	 * num_core_tx_stream, num_core_rx_stream, num_wr_task_max,
	 * num_wr_task_min, num_rd_task_max, num_rd_task_min
	 */
	unsigned int val[6];
};

/*
 * Default overrides:
 * There're 10 sets of settings for floor-based algorithm
 */
static struct shared_ice_alg2_config alg2_config[] = {
	{"G0", {5, 12, 0, 0, 32, 0}},
	{"G1", {12, 5, 32, 0, 0, 0}},
	{"G2", {6, 11, 4, 1, 32, 1}},
	{"G3", {6, 11, 7, 1, 32, 1}},
	{"G4", {7, 10, 11, 1, 32, 1}},
	{"G5", {7, 10, 14, 1, 32, 1}},
	{"G6", {8, 9, 18, 1, 32, 1}},
	{"G7", {9, 8, 21, 1, 32, 1}},
	{"G8", {10, 7, 24, 1, 32, 1}},
	{"G9", {10, 7, 32, 1, 32, 1}},
};

/**
 * Refer struct shared_ice_alg2_config
 */
static inline void __get_alg2_grp_params(unsigned int *val, int *c, int *t)
{
	*c = ((val[0] << 8) | val[1] | (1 << 31));
	*t = ((val[2] << 24) | (val[3] << 16) | (val[4] << 8) | val[5]);
}

static inline void get_alg2_grp_params(unsigned int group, int *core, int *task)
{
	struct shared_ice_alg2_config *p = &alg2_config[group];

	 __get_alg2_grp_params(p->val, core, task);
}

/**
 * struct ufs_qcom_ber_hist - record the detail of each BER event.
 * @pos: index of event.
 * @uec_pa: PA error type.
 * @err_code: error code, only needed for PA error.
 * @gear: the gear info when PHY PA occurs.
 * @tstamp: record timestamp.
 * @run_time: valid running time since last event.
 * @full_time: total time since last event.
 * @cnt: total error count.
 * @name: mode name.
 */
struct ufs_qcom_ber_hist {
	#define UFS_QCOM_EVT_LEN    32
	int pos;
	u32 uec_pa[UFS_QCOM_EVT_LEN];
	u32 err_code[UFS_QCOM_EVT_LEN];
	u32 gear[UFS_QCOM_EVT_LEN];
	ktime_t tstamp[UFS_QCOM_EVT_LEN];
	s64 run_time[UFS_QCOM_EVT_LEN];
	s64 full_time[UFS_QCOM_EVT_LEN];
	u32 cnt;
	char *name;
};

struct ufs_qcom_ber_table {
	enum ufs_qcom_ber_mode mode;
	u32 ber_threshold;
};

struct ufs_qcom_regs {
	struct list_head list;
	const char *prefix;
	u32 *ptr;
	size_t len;
};

/**
 * struct cpu_freq_info - keep CPUs frequency info
 * @cpu: the cpu to bump up when requests on perf core exceeds the threshold
 * @min_cpu_scale_freq: the minimal frequency of the cpu
 * @max_cpu_scale_freq: the maximal frequency of the cpu
 */
struct cpu_freq_info {
	u32 cpu;
	unsigned int min_cpu_scale_freq;
	unsigned int max_cpu_scale_freq;
};

struct ufs_qcom_host {
	/*
	 * Set this capability if host controller supports the QUniPro mode
	 * and if driver wants the Host controller to operate in QUniPro mode.
	 * Note: By default this capability will be kept enabled if host
	 * controller supports the QUniPro mode.
	 */
	#define UFS_QCOM_CAP_QUNIPRO	0x1

	/*
	 * Set this capability if host controller can retain the secure
	 * configuration even after UFS controller core power collapse.
	 */
	#define UFS_QCOM_CAP_RETAIN_SEC_CFG_AFTER_PWR_COLLAPSE	0x2

	/*
	 * Set this capability if host controller supports Qunipro internal
	 * clock gating.
	 */
	#define UFS_QCOM_CAP_QUNIPRO_CLK_GATING		0x4

	/*
	 * Set this capability if host controller supports SVS2 frequencies.
	 */
	#define UFS_QCOM_CAP_SVS2	0x8

	/*
	 * Set this capability if host controller supports shared ICE.
	 */
	#define UFS_QCOM_CAP_SHARED_ICE BIT(4)
	u32 caps;

	struct phy *generic_phy;
	struct ufs_hba *hba;
	struct ufs_qcom_bus_vote bus_vote;
	struct ufs_pa_layer_attr dev_req_params;
	struct clk *rx_l0_sync_clk;
	struct clk *tx_l0_sync_clk;
	struct clk *rx_l1_sync_clk;
	struct clk *tx_l1_sync_clk;
	bool is_lane_clks_enabled;

	void __iomem *dev_ref_clk_ctrl_mmio;
	bool is_dev_ref_clk_enabled;
	struct ufs_hw_version hw_ver;
#ifdef CONFIG_SCSI_UFS_CRYPTO
	void __iomem *ice_mmio;
#endif
#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER)
	void __iomem *ice_hwkm_mmio;
#endif

	bool reset_in_progress;
	u32 dev_ref_clk_en_mask;

	/* Bitmask for enabling debug prints */
	u32 dbg_print_en;
	struct ufs_qcom_testbus testbus;

	/* Reset control of HCI */
	struct reset_control *core_reset;
	struct reset_controller_dev rcdev;

	struct gpio_desc *device_reset;
	int max_hs_gear;
	int limit_tx_hs_gear;
	int limit_rx_hs_gear;
	int limit_tx_pwm_gear;
	int limit_rx_pwm_gear;
	int limit_rate;
	int limit_phy_submode;

	bool disable_lpm;
	struct qcom_bus_scale_data *qbsd;

	bool vdd_hba_pc;
	struct notifier_block vdd_hba_reg_nb;

	struct ufs_vreg *vddp_ref_clk;
	struct ufs_vreg *vccq_parent;
	bool work_pending;
	bool bypass_g4_cfgready;
	bool is_dt_pm_level_read;
	bool is_phy_pwr_on;
	/* Protect the usage of is_phy_pwr_on against racing */
	struct mutex phy_mutex;
	struct ufs_qcom_qos_req *ufs_qos;
	struct ufs_qcom_thermal uqt;
	/* FlashPVL entries */
	bool err_occurred;
	bool crash_on_err;
	atomic_t scale_up;
	atomic_t clks_on;
	unsigned long load_delay_ms;
#define NUM_REQS_HIGH_THRESH 64
#define NUM_REQS_LOW_THRESH 32
	atomic_t num_reqs_threshold;
	bool cur_freq_vote;
	struct delayed_work fwork;
	bool cpufreq_dis;
	struct cpu_freq_info *cpu_info;
	/* number of CPUs to bump up */
	int num_cpus;
	void *ufs_ipc_log_ctx;
	bool dbg_en;
	struct device_node *np;
	int chosen_algo;
	struct ufs_clk_info *ref_clki;
	struct ufs_clk_info *core_unipro_clki;
	atomic_t hi_pri_en;
	atomic_t therm_mitigation;
	cpumask_t perf_mask;
	cpumask_t def_mask;
	cpumask_t esi_affinity_mask;
	bool disable_wb_support;
	struct ufs_qcom_ber_hist ber_hist[UFS_QCOM_BER_MODE_MAX];
	struct list_head regs_list_head;
	bool ber_th_exceeded;

	bool esi_enabled;

	bool bypass_pbl_rst_wa;
	atomic_t cqhp_update_pending;
};

static inline u32
ufs_qcom_get_debug_reg_offset(struct ufs_qcom_host *host, u32 reg)
{
	if (host->hw_ver.major <= 0x02)
		return UFS_CNTLR_2_x_x_VEN_REGS_OFFSET(reg);

	return UFS_CNTLR_3_x_x_VEN_REGS_OFFSET(reg);
};

#define ufs_qcom_is_link_off(hba) ufshcd_is_link_off(hba)
#define ufs_qcom_is_link_active(hba) ufshcd_is_link_active(hba)
#define ufs_qcom_is_link_hibern8(hba) ufshcd_is_link_hibern8(hba)

int ufs_qcom_testbus_config(struct ufs_qcom_host *host);
void ufs_qcom_print_hw_debug_reg_all(struct ufs_hba *hba, void *priv,
		void (*print_fn)(struct ufs_hba *hba, int offset, int num_regs,
				const char *str, void *priv));

static inline bool ufs_qcom_cap_qunipro(struct ufs_qcom_host *host)
{
	return host->caps & UFS_QCOM_CAP_QUNIPRO;
}

static inline bool ufs_qcom_cap_qunipro_clk_gating(struct ufs_qcom_host *host)
{
	return !!(host->caps & UFS_QCOM_CAP_QUNIPRO_CLK_GATING);
}

static inline bool ufs_qcom_cap_svs2(struct ufs_qcom_host *host)
{
	return !!(host->caps & UFS_QCOM_CAP_SVS2);
}

static inline bool is_shared_ice_supported(struct ufs_qcom_host *host)
{
	return !!(host->caps & UFS_QCOM_CAP_SHARED_ICE);
}

/**
 * ufshcd_dme_rmw - get modify set a dme attribute
 * @hba - per adapter instance
 * @mask - mask to apply on read value
 * @val - actual value to write
 * @attr - dme attribute
 */
static inline int ufshcd_dme_rmw(struct ufs_hba *hba, u32 mask,
				 u32 val, u32 attr)
{
	u32 cfg = 0;
	int err = 0;

	err = ufshcd_dme_get(hba, UIC_ARG_MIB(attr), &cfg);
	if (err)
		goto out;

	cfg &= ~mask;
	cfg |= (val & mask);

	err = ufshcd_dme_set(hba, UIC_ARG_MIB(attr), cfg);

out:
	return err;
}

/*
 *  IOCTL opcode for ufs queries has the following opcode after
 *  SCSI_IOCTL_GET_PCI
 */
#define UFS_IOCTL_QUERY			0x5388

/**
 * struct ufs_ioctl_query_data - used to transfer data to and from user via
 * ioctl
 * @opcode: type of data to query (descriptor/attribute/flag)
 * @idn: id of the data structure
 * @buf_size: number of allocated bytes/data size on return
 * @buffer: data location
 *
 * Received: buffer and buf_size (available space for transferred data)
 * Submitted: opcode, idn, length, buf_size
 */
struct ufs_ioctl_query_data {
	/*
	 * User should select one of the opcode defined in "enum query_opcode".
	 * Please check include/uapi/scsi/ufs/ufs.h for the definition of it.
	 * Note that only UPIU_QUERY_OPCODE_READ_DESC,
	 * UPIU_QUERY_OPCODE_READ_ATTR & UPIU_QUERY_OPCODE_READ_FLAG are
	 * supported as of now. All other query_opcode would be considered
	 * invalid.
	 * As of now only read query operations are supported.
	 */
	__u32 opcode;
	/*
	 * User should select one of the idn from "enum flag_idn" or "enum
	 * attr_idn" or "enum desc_idn" based on whether opcode above is
	 * attribute, flag or descriptor.
	 * Please check include/uapi/scsi/ufs/ufs.h for the definition of it.
	 */
	__u8 idn;
	/*
	 * User should specify the size of the buffer (buffer[0] below) where
	 * it wants to read the query data (attribute/flag/descriptor).
	 * As we might end up reading less data then what is specified in
	 * buf_size. So we are updating buf_size to what exactly we have read.
	 */
	__u16 buf_size;
	/*
	 * placeholder for the start of the data buffer where kernel will copy
	 * the query data (attribute/flag/descriptor) read from the UFS device
	 * Note:
	 * For Read/Write Attribute you will have to allocate 4 bytes
	 * For Read/Write Flag you will have to allocate 1 byte
	 */
	__u8 buffer[0];
};

/* ufs-qcom-ice.c */

#ifdef CONFIG_SCSI_UFS_CRYPTO
int ufs_qcom_ice_init(struct ufs_qcom_host *host);
int ufs_qcom_ice_enable(struct ufs_qcom_host *host);
int ufs_qcom_ice_resume(struct ufs_qcom_host *host);
int ufs_qcom_ice_program_key(struct ufs_hba *hba,
			     const union ufs_crypto_cfg_entry *cfg, int slot);
void ufs_qcom_ice_disable(struct ufs_qcom_host *host);
void ufs_qcom_ice_debug(struct ufs_qcom_host *host);
#else
static inline int ufs_qcom_ice_init(struct ufs_qcom_host *host)
{
	return 0;
}
static inline int ufs_qcom_ice_enable(struct ufs_qcom_host *host)
{
	return 0;
}
static inline int ufs_qcom_ice_resume(struct ufs_qcom_host *host)
{
	return 0;
}
static inline void ufs_qcom_ice_disable(struct ufs_qcom_host *host)
{
	return;
}
static inline void ufs_qcom_ice_debug(struct ufs_qcom_host *host)
{
	return;
}
#define ufs_qcom_ice_program_key NULL
#endif /* !CONFIG_SCSI_UFS_CRYPTO */

static inline void ufs_qcom_msi_lock_descs(struct ufs_hba *hba)
{
	mutex_lock(&hba->dev->msi.data->mutex);
}

static inline void ufs_qcom_msi_unlock_descs(struct ufs_hba *hba)
{
	hba->dev->msi.data->__iter_idx = MSI_MAX_INDEX;
	mutex_unlock(&hba->dev->msi.data->mutex);
}

#endif /* UFS_QCOM_H_ */
