// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/mmc/host/sdhci-msm.c - Qualcomm SDHCI Platform driver
 *
 * Copyright (c) 2013-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/delay.h>
#include <linux/mmc/mmc.h>
#include <linux/pm_runtime.h>
#include <linux/pm_opp.h>
#include <linux/slab.h>
#include <linux/interconnect.h>
#include <linux/iopoll.h>
#include <linux/qcom_scm.h>
#include <linux/regulator/consumer.h>
#include <linux/interconnect.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_qos.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/reset.h>
#include <linux/clk/qcom.h>
#include <linux/nvmem-consumer.h>

#include "sdhci-pltfm.h"
#include "cqhci.h"
#include "../core/core.h"
#include <linux/qtee_shmbridge.h>

#define CORE_MCI_VERSION		0x50
#define CORE_VERSION_MAJOR_SHIFT	28
#define CORE_VERSION_MAJOR_MASK		(0xf << CORE_VERSION_MAJOR_SHIFT)
#define CORE_VERSION_MINOR_MASK		0xff
#define CORE_VERSION_TARGET_MASK        0x000000FF
#define SDHCI_MSM_VER_420               0x49

#define CORE_MCI_GENERICS		0x70
#define SWITCHABLE_SIGNALING_VOLTAGE	BIT(29)

#define HC_MODE_EN		0x1
#define CORE_POWER		0x0
#define CORE_SW_RST		BIT(7)
#define FF_CLK_SW_RST_DIS	BIT(13)

#define CORE_PWRCTL_BUS_OFF	BIT(0)
#define CORE_PWRCTL_BUS_ON	BIT(1)
#define CORE_PWRCTL_IO_LOW	BIT(2)
#define CORE_PWRCTL_IO_HIGH	BIT(3)
#define CORE_PWRCTL_BUS_SUCCESS BIT(0)
#define CORE_PWRCTL_BUS_FAIL    BIT(1)
#define CORE_PWRCTL_IO_SUCCESS	BIT(2)
#define CORE_PWRCTL_IO_FAIL     BIT(3)
#define REQ_BUS_OFF		BIT(0)
#define REQ_BUS_ON		BIT(1)
#define REQ_IO_LOW		BIT(2)
#define REQ_IO_HIGH		BIT(3)
#define INT_MASK		0xf
#define MAX_PHASES		16
#define CORE_DLL_LOCK		BIT(7)
#define CORE_DDR_DLL_LOCK	BIT(11)
#define CORE_DLL_EN		BIT(16)
#define CORE_CDR_EN		BIT(17)
#define CORE_CK_OUT_EN		BIT(18)
#define CORE_CDR_EXT_EN		BIT(19)
#define CORE_DLL_PDN		BIT(29)
#define CORE_DLL_RST		BIT(30)
#define CORE_CMD_DAT_TRACK_SEL	BIT(0)

#define CORE_DDR_CAL_EN		BIT(0)
#define CORE_FLL_CYCLE_CNT	BIT(18)
#define CORE_LOW_FREQ_MODE	BIT(19)
#define CORE_DLL_CLOCK_DISABLE	BIT(21)

#define DLL_USR_CTL_POR_VAL	0x10800
#define ENABLE_DLL_LOCK_STATUS	BIT(26)
#define FINE_TUNE_MODE_EN	BIT(27)
#define BIAS_OK_SIGNAL		BIT(29)

#define DLL_CONFIG_3_LOW_FREQ_VAL	0x08
#define DLL_CONFIG_3_HIGH_FREQ_VAL	0x10

#define CORE_VENDOR_SPEC_POR_VAL 0xa9c
#define CORE_CLK_PWRSAVE	BIT(1)
#define CORE_VNDR_SPEC_ADMA_ERR_SIZE_EN	BIT(7)
#define CORE_HC_MCLK_SEL_DFLT	(2 << 8)
#define CORE_HC_MCLK_SEL_HS400	(3 << 8)
#define CORE_HC_MCLK_SEL_MASK	(3 << 8)
#define CORE_IO_PAD_PWR_SWITCH_EN	BIT(15)
#define CORE_IO_PAD_PWR_SWITCH	BIT(16)
#define CORE_HC_SELECT_IN_EN	BIT(18)
#define CORE_HC_SELECT_IN_HS400	(6 << 19)
#define CORE_HC_SELECT_IN_MASK	(7 << 19)
#define CORE_HC_SELECT_IN_SDR50	(4 << 19)

#define CORE_8_BIT_SUPPORT	BIT(18)
#define CORE_3_0V_SUPPORT	BIT(25)
#define CORE_1_8V_SUPPORT	BIT(26)
#define CORE_VOLT_SUPPORT	(CORE_3_0V_SUPPORT | CORE_1_8V_SUPPORT)

#define CORE_SYS_BUS_SUPPORT_64_BIT     BIT(28)

#define CORE_CSR_CDC_CTLR_CFG0		0x130
#define CORE_SW_TRIG_FULL_CALIB		BIT(16)
#define CORE_HW_AUTOCAL_ENA		BIT(17)

#define CORE_CSR_CDC_CTLR_CFG1		0x134
#define CORE_CSR_CDC_CAL_TIMER_CFG0	0x138
#define CORE_TIMER_ENA			BIT(16)

#define CORE_CSR_CDC_CAL_TIMER_CFG1	0x13C
#define CORE_CSR_CDC_REFCOUNT_CFG	0x140
#define CORE_CSR_CDC_COARSE_CAL_CFG	0x144
#define CORE_CDC_OFFSET_CFG		0x14C
#define CORE_CSR_CDC_DELAY_CFG		0x150
#define CORE_CDC_SLAVE_DDA_CFG		0x160
#define CORE_CSR_CDC_STATUS0		0x164
#define CORE_CALIBRATION_DONE		BIT(0)

#define CORE_CDC_ERROR_CODE_MASK	0x7000000

#define CQ_CMD_DBG_RAM                  0x110
#define CQ_CMD_DBG_RAM_WA               0x150
#define CQ_CMD_DBG_RAM_OL               0x154

#define CORE_CSR_CDC_GEN_CFG		0x178
#define CORE_CDC_SWITCH_BYPASS_OFF	BIT(0)
#define CORE_CDC_SWITCH_RC_EN		BIT(1)

#define CORE_CDC_T4_DLY_SEL		BIT(0)
#define CORE_CMDIN_RCLK_EN		BIT(1)
#define CORE_START_CDC_TRAFFIC		BIT(6)

#define CORE_PWRSAVE_DLL	BIT(3)
#define CORE_FIFO_ALT_EN	BIT(10)
#define DDR_CONFIG_POR_VAL		0x80040873
#define DLL_USR_CTL_POR_VAL		0x10800
#define ENABLE_DLL_LOCK_STATUS		BIT(26)
#define FINE_TUNE_MODE_EN		BIT(27)
#define BIAS_OK_SIGNAL			BIT(29)
#define DLL_CONFIG_3_POR_VAL		0x10


#define INVALID_TUNING_PHASE	-1
#define SDHCI_MSM_MIN_CLOCK	400000
#define CORE_FREQ_100MHZ	(100 * 1000 * 1000)
#define TCXO_FREQ		19200000

#define ROUND(x, y)		((x) / (y) + \
				((x) % (y) * 10 / (y) >= 5 ? 1 : 0))

#define CDR_SELEXT_SHIFT	20
#define CDR_SELEXT_MASK		(0xf << CDR_SELEXT_SHIFT)
#define CMUX_SHIFT_PHASE_SHIFT	24
#define CMUX_SHIFT_PHASE_MASK	(7 << CMUX_SHIFT_PHASE_SHIFT)

#define MSM_MMC_AUTOSUSPEND_DELAY_MS	10
#define MSM_CLK_GATING_DELAY_MS		200 /* msec */

/* Timeout value to avoid infinite waiting for pwr_irq */
#define MSM_PWR_IRQ_TIMEOUT_MS 5000

/* Max load for eMMC Vdd-io supply */
#define MMC_VQMMC_MAX_LOAD_UA	325000

/*
 * Due to level shifter insertion, HS mode frequency is reduced to 37.5MHz
 * but clk's driver supply 37MHz only and uses ceil ops. So vote for
 * 37MHz to avoid picking next ceil value.
 */
#define LEVEL_SHIFTER_HIGH_SPEED_FREQ	37000000

#define VS_CAPABILITIES_SDR_50_SUPPORT BIT(0)
#define VS_CAPABILITIES_SDR_104_SUPPORT BIT(1)
#define VS_CAPABILITIES_DDR_50_SUPPORT BIT(2)

#define msm_host_readl(msm_host, host, offset) \
	msm_host->var_ops->msm_readl_relaxed(host, offset)

#define msm_host_writel(msm_host, val, host, offset) \
	msm_host->var_ops->msm_writel_relaxed(val, host, offset)

/* CQHCI vendor specific registers */
#define CQHCI_VENDOR_CFG1	0xA00
#define CQHCI_VENDOR_DIS_RST_ON_CQ_EN	(0x3 << 13)

#define SDHCI_CMD_FLAGS_MASK	0xff
#define	SDHCI_BOOT_DEVICE	0x0

struct sdhci_msm_offset {
	u32 core_hc_mode;
	u32 core_mci_data_cnt;
	u32 core_mci_status;
	u32 core_mci_fifo_cnt;
	u32 core_mci_version;
	u32 core_generics;
	u32 core_testbus_config;
	u32 core_testbus_sel2_bit;
	u32 core_testbus_ena;
	u32 core_testbus_sel2;
	u32 core_pwrctl_status;
	u32 core_pwrctl_mask;
	u32 core_pwrctl_clear;
	u32 core_pwrctl_ctl;
	u32 core_sdcc_debug_reg;
	u32 core_dll_config;
	u32 core_dll_status;
	u32 core_vendor_spec;
	u32 core_vendor_spec_adma_err_addr0;
	u32 core_vendor_spec_adma_err_addr1;
	u32 core_vendor_spec_func2;
	u32 core_vendor_spec_capabilities0;
	u32 core_vendor_spec_capabilities1;
	u32 core_ddr_200_cfg;
	u32 core_vendor_spec3;
	u32 core_dll_config_2;
	u32 core_dll_config_3;
	u32 core_ddr_config_old; /* Applicable to sdcc minor ver < 0x49 */
	u32 core_ddr_config;
	u32 core_dll_usr_ctl; /* Present on SDCC5.1 onwards */
};

static const struct sdhci_msm_offset sdhci_msm_v5_offset = {
	.core_mci_data_cnt = 0x35c,
	.core_mci_status = 0x324,
	.core_mci_fifo_cnt = 0x308,
	.core_mci_version = 0x318,
	.core_generics = 0x320,
	.core_testbus_config = 0x32c,
	.core_testbus_sel2_bit = 3,
	.core_testbus_ena = (1 << 31),
	.core_testbus_sel2 = (1 << 3),
	.core_pwrctl_status = 0x240,
	.core_pwrctl_mask = 0x244,
	.core_pwrctl_clear = 0x248,
	.core_pwrctl_ctl = 0x24c,
	.core_sdcc_debug_reg = 0x358,
	.core_dll_config = 0x200,
	.core_dll_status = 0x208,
	.core_vendor_spec = 0x20c,
	.core_vendor_spec_adma_err_addr0 = 0x214,
	.core_vendor_spec_adma_err_addr1 = 0x218,
	.core_vendor_spec_func2 = 0x210,
	.core_vendor_spec_capabilities0 = 0x21c,
	.core_vendor_spec_capabilities1 = 0x220,
	.core_ddr_200_cfg = 0x224,
	.core_vendor_spec3 = 0x250,
	.core_dll_config_2 = 0x254,
	.core_dll_config_3 = 0x258,
	.core_ddr_config = 0x25c,
	.core_dll_usr_ctl = 0x388,
};

static const struct sdhci_msm_offset sdhci_msm_mci_offset = {
	.core_hc_mode = 0x78,
	.core_mci_data_cnt = 0x30,
	.core_mci_status = 0x34,
	.core_mci_fifo_cnt = 0x44,
	.core_mci_version = 0x050,
	.core_generics = 0x70,
	.core_testbus_config = 0x0cc,
	.core_testbus_sel2_bit = 4,
	.core_testbus_ena = (1 << 3),
	.core_testbus_sel2 = (1 << 4),
	.core_pwrctl_status = 0xdc,
	.core_pwrctl_mask = 0xe0,
	.core_pwrctl_clear = 0xe4,
	.core_pwrctl_ctl = 0xe8,
	.core_sdcc_debug_reg = 0x124,
	.core_dll_config = 0x100,
	.core_dll_status = 0x108,
	.core_vendor_spec = 0x10c,
	.core_vendor_spec_adma_err_addr0 = 0x114,
	.core_vendor_spec_adma_err_addr1 = 0x118,
	.core_vendor_spec_func2 = 0x110,
	.core_vendor_spec_capabilities0 = 0x11c,
	.core_ddr_200_cfg = 0x184,
	.core_vendor_spec3 = 0x1b0,
	.core_dll_config_2 = 0x1b4,
	.core_dll_config_3 = 0x1b8,
	.core_ddr_config_old = 0x1b8,
	.core_ddr_config = 0x1bc,
};

struct sdhci_msm_variant_ops {
	u32 (*msm_readl_relaxed)(struct sdhci_host *host, u32 offset);
	void (*msm_writel_relaxed)(u32 val, struct sdhci_host *host,
			u32 offset);
};

/*
 * From V5, register spaces have changed. Wrap this info in a structure
 * and choose the data_structure based on version info mentioned in DT.
 */
struct sdhci_msm_variant_info {
	bool mci_removed;
	bool restore_dll_config;
	const struct sdhci_msm_variant_ops *var_ops;
	const struct sdhci_msm_offset *offset;
};

struct msm_bus_vectors {
	u64 ab;
	u64 ib;
};

struct msm_bus_path {
	unsigned int num_paths;
	struct msm_bus_vectors *vec;
};

struct sdhci_msm_bus_vote_data {
	const char *name;
	unsigned int num_usecase;
	struct msm_bus_path *usecase;

	unsigned int *bw_vecs;
	unsigned int bw_vecs_size;

	struct icc_path *sdhc_ddr;
	struct icc_path *cpu_sdhc;

	u32 curr_vote;
};

/*
 * DLL registers which needs be programmed with HSR settings.
 * Add any new register only at the end and don't change the
 * sequence.
 */
struct sdhci_msm_dll_hsr {
	u32 dll_config;
	u32 dll_config_2;
	u32 dll_config_3;
	u32 dll_usr_ctl;
	u32 ddr_config;
};

struct cqe_regs_restore {
	u32 cqe_vendor_cfg1;
};

struct sdhci_msm_regs_restore {
	bool is_supported;
	bool is_valid;
	u32 vendor_pwrctl_mask;
	u32 vendor_pwrctl_ctl;
	u32 vendor_caps_0;
	u32 vendor_func;
	u32 vendor_func2;
	u32 vendor_func3;
	u32 hc_2c_2e;
	u32 hc_28_2a;
	u32 hc_34_36;
	u32 hc_38_3a;
	u32 hc_3c_3e;
	u32 hc_caps_1;
	u32 testbus_config;
	u32 dll_config;
	u32 dll_config2;
	u32 dll_config3;
	u32 dll_usr_ctl;
};

enum vdd_io_level {
	/* set vdd_io_data->low_vol_level */
	VDD_IO_LOW,
	/* set vdd_io_data->high_vol_level */
	VDD_IO_HIGH,
	/*
	 * set whatever there in voltage_level (third argument) of
	 * sdhci_msm_set_vdd_io_vol() function.
	 */
	VDD_IO_SET_LEVEL,
};

enum dll_init_context {
	DLL_INIT_NORMAL = 0,
	DLL_INIT_FROM_CX_COLLAPSE_EXIT,
};

/* This structure keeps information per regulator */
struct sdhci_msm_reg_data {
	/* voltage regulator handle */
	struct regulator *reg;
	/* regulator name */
	const char *name;
	/* voltage level to be set */
	u32 low_vol_level;
	u32 high_vol_level;
	/* Load values for low power and high power mode */
	u32 lpm_uA;
	u32 hpm_uA;

	/* is this regulator enabled? */
	bool is_enabled;
	/* is this regulator needs to be always on? */
	bool is_always_on;
	/* is low power mode setting required for this regulator? */
	bool lpm_sup;
	bool set_voltage_sup;
};

/*
 * This structure keeps information for all the
 * regulators required for a SDCC slot.
 */
struct sdhci_msm_vreg_data {
	/* keeps VDD/VCC regulator info */
	struct sdhci_msm_reg_data *vdd_data;
	 /* keeps VDD IO regulator info */
	struct sdhci_msm_reg_data *vdd_io_data;
};

/* Per cpu cluster qos group */
struct qos_cpu_group {
	cpumask_t mask;	/* CPU mask of cluster */
	unsigned int *votes;	/* Different votes for cluster */
	struct dev_pm_qos_request *qos_req;	/* Pointer to host qos request*/
	bool voted;
	struct sdhci_msm_host *host;
	bool initialized;
	bool curr_vote;
};

/* Per host qos request structure */
struct sdhci_msm_qos_req {
	struct qos_cpu_group *qcg;	/* CPU group per host */
	unsigned int num_groups;	/* Number of groups */
	unsigned int active_mask;	/* Active affine irq mask */
};

enum constraint {
	QOS_PERF,
	QOS_POWER,
	QOS_MAX,
};

struct sdhci_msm_host {
	struct platform_device *pdev;
	void __iomem *core_mem;	/* MSM SDCC mapped address */
#ifdef CONFIG_MMC_CRYPTO
	void __iomem *ice_mem;	/* MSM ICE mapped address (if available) */
#endif
#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER)
	void __iomem *ice_hwkm_mem;
#endif
	int pwr_irq;		/* power irq */
	struct clk *bus_clk;	/* SDHC bus voter clock */
	struct clk *xo_clk;	/* TCXO clk needed for FLL feature of cm_dll*/
	/* core, iface, ice, cal, sleep clocks */
	struct clk_bulk_data bulk_clks[5];
	unsigned long clk_rate;
	struct sdhci_msm_vreg_data *vreg_data;
	struct mmc_host *mmc;
	int opp_token;
	bool has_opp_table;
	struct cqhci_host *cq_host;
	bool use_14lpp_dll_reset;
	bool tuning_done;
	bool calibration_done;
	u8 saved_tuning_phase;
	bool use_cdclp533;
	u32 curr_pwr_state;
	u32 curr_io_level;
	wait_queue_head_t pwr_irq_wait;
	bool pwr_irq_flag;
	u32 caps_0;
	bool mci_removed;
	bool restore_dll_config;
	const struct sdhci_msm_variant_ops *var_ops;
	const struct sdhci_msm_offset *offset;
	bool use_cdr;
	u32 transfer_mode;
	bool updated_ddr_cfg;
	bool skip_bus_bw_voting;
	struct sdhci_msm_bus_vote_data *bus_vote_data;
	struct delayed_work bus_vote_work;
	struct delayed_work clk_gating_work;
	struct workqueue_struct *workq;	/* QoS work queue */
	struct sdhci_msm_qos_req *sdhci_qos;
	struct irq_affinity_notify affinity_notify;
	struct device_attribute clk_gating;
	struct device_attribute pm_qos;
	u32 clk_gating_delay;
	u32 pm_qos_delay;
	bool cqhci_offset_changed;
	bool reg_store;
	bool vbias_skip_wa;
	struct reset_control *core_reset;
	bool pltfm_init_done;
	bool fake_core_3_0v_support;
	bool use_7nm_dll;
	struct sdhci_msm_dll_hsr *dll_hsr;
	struct sdhci_msm_regs_restore regs_restore;
	struct cqe_regs_restore cqe_regs;
	u32 *sup_ice_clk_table;
	unsigned char sup_ice_clk_cnt;
	u32 ice_clk_max;
	u32 ice_clk_min;
	u32 ice_clk_rate;
	bool uses_tassadar_dll;
	bool uses_level_shifter;
	bool dll_lock_bist_fail_wa;
	u32 dll_config;
	u32 ddr_config;
	u16 last_cmd;
	bool vqmmc_enabled;
};

static struct sdhci_msm_host *sdhci_slot[2];

static int sdhci_msm_update_qos_constraints(struct qos_cpu_group *qcg,
					enum constraint type);
static void sdhci_msm_bus_voting(struct sdhci_host *host, bool enable);

static int sdhci_msm_dt_get_array(struct device *dev, const char *prop_name,
				u32 **bw_vecs, int *len, u32 size);

static unsigned int sdhci_msm_get_sup_clk_rate(struct sdhci_host *host,
				u32 req_clk);
static void sdhci_msm_dump_pwr_ctrl_regs(struct sdhci_host *host);
static void sdhci_msm_vbias_bypass_wa(struct sdhci_host *host);

static const struct sdhci_msm_offset *sdhci_priv_msm_offset(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);

	return msm_host->offset;
}

/*
 * APIs to read/write to vendor specific registers which were there in the
 * core_mem region before MCI was removed.
 */
static u32 sdhci_msm_mci_variant_readl_relaxed(struct sdhci_host *host,
		u32 offset)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);

	return readl_relaxed(msm_host->core_mem + offset);
}

static u32 sdhci_msm_v5_variant_readl_relaxed(struct sdhci_host *host,
		u32 offset)
{
	return readl_relaxed(host->ioaddr + offset);
}

static void sdhci_msm_mci_variant_writel_relaxed(u32 val,
		struct sdhci_host *host, u32 offset)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);

	writel_relaxed(val, msm_host->core_mem + offset);
}

static void sdhci_msm_v5_variant_writel_relaxed(u32 val,
		struct sdhci_host *host, u32 offset)
{
	writel_relaxed(val, host->ioaddr + offset);
}

static unsigned int msm_get_clock_rate_for_bus_mode(struct sdhci_host *host,
						    unsigned int clock)
{
	struct mmc_ios ios = host->mmc->ios;
	/*
	 * The SDHC requires internal clock frequency to be double the
	 * actual clock that will be set for DDR mode. The controller
	 * uses the faster clock(100/400MHz) for some of its parts and
	 * send the actual required clock (50/200MHz) to the card.
	 */
	if (ios.timing == MMC_TIMING_UHS_DDR50 ||
	    ios.timing == MMC_TIMING_MMC_DDR52 ||
	    ios.timing == MMC_TIMING_MMC_HS400 ||
	    host->flags & SDHCI_HS400_TUNING)
		clock *= 2;
	return clock;
}

static void msm_set_clock_rate_for_bus_mode(struct sdhci_host *host,
					    unsigned int clock)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	struct mmc_ios curr_ios = host->mmc->ios;
	struct clk *core_clk = msm_host->bulk_clks[0].clk;
	int rc;

	clock = msm_get_clock_rate_for_bus_mode(host, clock);

	if (curr_ios.timing == MMC_TIMING_SD_HS &&
			msm_host->uses_level_shifter)
		clock = LEVEL_SHIFTER_HIGH_SPEED_FREQ;

	rc = dev_pm_opp_set_rate(mmc_dev(host->mmc), clock);
	if (rc) {
		pr_err("%s: Failed to set clock at rate %u at timing %d\n",
		       mmc_hostname(host->mmc), clock,
		       curr_ios.timing);
		return;
	}
	msm_host->clk_rate = clock;
	pr_debug("%s: Setting clock at rate %lu at timing %d\n",
		 mmc_hostname(host->mmc), clk_get_rate(core_clk),
		 curr_ios.timing);
}

/* Platform specific tuning */
static inline int msm_dll_poll_ck_out_en(struct sdhci_host *host, u8 poll)
{
	u32 wait_cnt = 50;
	u8 ck_out_en;
	struct mmc_host *mmc = host->mmc;
	const struct sdhci_msm_offset *msm_offset =
					sdhci_priv_msm_offset(host);

	/* Poll for CK_OUT_EN bit.  max. poll time = 50us */
	ck_out_en = !!(readl_relaxed(host->ioaddr +
			msm_offset->core_dll_config) & CORE_CK_OUT_EN);

	while (ck_out_en != poll) {
		if (--wait_cnt == 0) {
			dev_err(mmc_dev(mmc), "%s: CK_OUT_EN bit is not %d\n",
			       mmc_hostname(mmc), poll);
			return -ETIMEDOUT;
		}
		udelay(1);

		ck_out_en = !!(readl_relaxed(host->ioaddr +
			msm_offset->core_dll_config) & CORE_CK_OUT_EN);
	}

	return 0;
}

static int msm_config_cm_dll_phase(struct sdhci_host *host, u8 phase)
{
	int rc;
	static const u8 grey_coded_phase_table[] = {
		0x0, 0x1, 0x3, 0x2, 0x6, 0x7, 0x5, 0x4,
		0xc, 0xd, 0xf, 0xe, 0xa, 0xb, 0x9, 0x8
	};
	unsigned long flags;
	u32 config;
	struct mmc_host *mmc = host->mmc;
	const struct sdhci_msm_offset *msm_offset =
					sdhci_priv_msm_offset(host);

	if (phase > 0xf)
		return -EINVAL;

	spin_lock_irqsave(&host->lock, flags);

	config = readl_relaxed(host->ioaddr + msm_offset->core_dll_config);
	config &= ~(CORE_CDR_EN | CORE_CK_OUT_EN);
	config |= (CORE_CDR_EXT_EN | CORE_DLL_EN);
	writel_relaxed(config, host->ioaddr + msm_offset->core_dll_config);

	/* Wait until CK_OUT_EN bit of DLL_CONFIG register becomes '0' */
	rc = msm_dll_poll_ck_out_en(host, 0);
	if (rc)
		goto err_out;

	/*
	 * Write the selected DLL clock output phase (0 ... 15)
	 * to CDR_SELEXT bit field of DLL_CONFIG register.
	 */
	config = readl_relaxed(host->ioaddr + msm_offset->core_dll_config);
	config &= ~CDR_SELEXT_MASK;
	config |= grey_coded_phase_table[phase] << CDR_SELEXT_SHIFT;
	writel_relaxed(config, host->ioaddr + msm_offset->core_dll_config);

	config = readl_relaxed(host->ioaddr + msm_offset->core_dll_config);
	config |= CORE_CK_OUT_EN;
	writel_relaxed(config, host->ioaddr + msm_offset->core_dll_config);

	/* Wait until CK_OUT_EN bit of DLL_CONFIG register becomes '1' */
	rc = msm_dll_poll_ck_out_en(host, 1);
	if (rc)
		goto err_out;

	config = readl_relaxed(host->ioaddr + msm_offset->core_dll_config);
	config |= CORE_CDR_EN;
	config &= ~CORE_CDR_EXT_EN;
	writel_relaxed(config, host->ioaddr + msm_offset->core_dll_config);
	goto out;

err_out:
	dev_err(mmc_dev(mmc), "%s: Failed to set DLL phase: %d\n",
	       mmc_hostname(mmc), phase);
out:
	spin_unlock_irqrestore(&host->lock, flags);
	return rc;
}

/*
 * Find out the greatest range of consecuitive selected
 * DLL clock output phases that can be used as sampling
 * setting for SD3.0 UHS-I card read operation (in SDR104
 * timing mode) or for eMMC4.5 card read operation (in
 * HS400/HS200 timing mode).
 * Select the 3/4 of the range and configure the DLL with the
 * selected DLL clock output phase.
 */

static int msm_find_most_appropriate_phase(struct sdhci_host *host,
					   u8 *phase_table, u8 total_phases)
{
	int ret;
	u8 ranges[MAX_PHASES][MAX_PHASES] = { {0}, {0} };
	u8 phases_per_row[MAX_PHASES] = { 0 };
	int row_index = 0, col_index = 0, selected_row_index = 0, curr_max = 0;
	int i, cnt, phase_0_raw_index = 0, phase_15_raw_index = 0;
	bool phase_0_found = false, phase_15_found = false;
	struct mmc_host *mmc = host->mmc;

	if (!total_phases || (total_phases > MAX_PHASES)) {
		dev_err(mmc_dev(mmc), "%s: Invalid argument: total_phases=%d\n",
		       mmc_hostname(mmc), total_phases);
		return -EINVAL;
	}

	for (cnt = 0; cnt < total_phases; cnt++) {
		ranges[row_index][col_index] = phase_table[cnt];
		phases_per_row[row_index] += 1;
		col_index++;

		if ((cnt + 1) == total_phases) {
			continue;
		/* check if next phase in phase_table is consecutive or not */
		} else if ((phase_table[cnt] + 1) != phase_table[cnt + 1]) {
			row_index++;
			col_index = 0;
		}
	}

	if (row_index >= MAX_PHASES)
		return -EINVAL;

	/* Check if phase-0 is present in first valid window? */
	if (!ranges[0][0]) {
		phase_0_found = true;
		phase_0_raw_index = 0;
		/* Check if cycle exist between 2 valid windows */
		for (cnt = 1; cnt <= row_index; cnt++) {
			if (phases_per_row[cnt]) {
				for (i = 0; i < phases_per_row[cnt]; i++) {
					if (ranges[cnt][i] == 15) {
						phase_15_found = true;
						phase_15_raw_index = cnt;
						break;
					}
				}
			}
		}
	}

	/* If 2 valid windows form cycle then merge them as single window */
	if (phase_0_found && phase_15_found) {
		/* number of phases in raw where phase 0 is present */
		u8 phases_0 = phases_per_row[phase_0_raw_index];
		/* number of phases in raw where phase 15 is present */
		u8 phases_15 = phases_per_row[phase_15_raw_index];

		if (phases_0 + phases_15 >= MAX_PHASES)
			/*
			 * If there are more than 1 phase windows then total
			 * number of phases in both the windows should not be
			 * more than or equal to MAX_PHASES.
			 */
			return -EINVAL;

		/* Merge 2 cyclic windows */
		i = phases_15;
		for (cnt = 0; cnt < phases_0; cnt++) {
			ranges[phase_15_raw_index][i] =
			    ranges[phase_0_raw_index][cnt];
			if (++i >= MAX_PHASES)
				break;
		}

		phases_per_row[phase_0_raw_index] = 0;
		phases_per_row[phase_15_raw_index] = phases_15 + phases_0;
	}

	for (cnt = 0; cnt <= row_index; cnt++) {
		if (phases_per_row[cnt] > curr_max) {
			curr_max = phases_per_row[cnt];
			selected_row_index = cnt;
		}
	}

	i = (curr_max * 3) / 4;
	if (i)
		i--;

	ret = ranges[selected_row_index][i];

	if (ret >= MAX_PHASES) {
		ret = -EINVAL;
		dev_err(mmc_dev(mmc), "%s: Invalid phase selected=%d\n",
		       mmc_hostname(mmc), ret);
	}

	return ret;
}

static inline void msm_cm_dll_set_freq(struct sdhci_host *host)
{
	u32 mclk_freq = 0, config;
	const struct sdhci_msm_offset *msm_offset =
					sdhci_priv_msm_offset(host);

	/* Program the MCLK value to MCLK_FREQ bit field */
	if (host->clock <= 112000000)
		mclk_freq = 0;
	else if (host->clock <= 125000000)
		mclk_freq = 1;
	else if (host->clock <= 137000000)
		mclk_freq = 2;
	else if (host->clock <= 150000000)
		mclk_freq = 3;
	else if (host->clock <= 162000000)
		mclk_freq = 4;
	else if (host->clock <= 175000000)
		mclk_freq = 5;
	else if (host->clock <= 187000000)
		mclk_freq = 6;
	else if (host->clock <= 208000000)
		mclk_freq = 7;

	config = readl_relaxed(host->ioaddr + msm_offset->core_dll_config);
	config &= ~CMUX_SHIFT_PHASE_MASK;
	config |= mclk_freq << CMUX_SHIFT_PHASE_SHIFT;
	writel_relaxed(config, host->ioaddr + msm_offset->core_dll_config);
}

/* Initialize the DLL (Programmable Delay Line) */
static int msm_init_cm_dll(struct sdhci_host *host,
				enum dll_init_context init_context)
{
	struct mmc_host *mmc = host->mmc;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	struct mmc_ios curr_ios = mmc->ios;
	int wait_cnt = 50;
	int rc = 0;
	unsigned long flags, dll_clock = 0;
	u32 ddr_cfg_offset, core_vendor_spec;
	const struct sdhci_msm_offset *msm_offset =
					msm_host->offset;

	dll_clock = sdhci_msm_get_sup_clk_rate(host, host->clock);
	spin_lock_irqsave(&host->lock, flags);

	core_vendor_spec = readl_relaxed(host->ioaddr +
			msm_offset->core_vendor_spec);

	/*
	 * Step 1 - Always disable PWRSAVE during the DLL power
	 * up regardless of its current setting.
	 */

	writel_relaxed((core_vendor_spec & ~CORE_CLK_PWRSAVE),
			host->ioaddr +
			msm_offset->core_vendor_spec);

	if (msm_host->use_14lpp_dll_reset) {
		/* Step 2 - Disable CK_OUT */
		writel_relaxed((readl_relaxed(host->ioaddr +
			msm_offset->core_dll_config)
			& ~CORE_CK_OUT_EN), host->ioaddr +
			msm_offset->core_dll_config);

		/* Step 3 - Disable the DLL clock */
		writel_relaxed((readl_relaxed(host->ioaddr +
			msm_offset->core_dll_config_2)
			| CORE_DLL_CLOCK_DISABLE), host->ioaddr +
			msm_offset->core_dll_config_2);
	}

	/*
	 * Step 4 - Write 1 to DLL_RST bit of DLL_CONFIG register
	 * and Write 1 to DLL_PDN bit of DLL_CONFIG register.
	 */
	writel_relaxed((readl_relaxed(host->ioaddr +
		msm_offset->core_dll_config) | CORE_DLL_RST),
		host->ioaddr + msm_offset->core_dll_config);

	writel_relaxed((readl_relaxed(host->ioaddr +
		msm_offset->core_dll_config) | CORE_DLL_PDN),
		host->ioaddr + msm_offset->core_dll_config);

	/*
	 * Step 5 and Step 6 - Configure Tassadar DLL and USER_CTRL
	 * (Only applicable for 7FF projects).
	 */
	if (msm_host->use_7nm_dll) {
		if (msm_host->dll_hsr) {
			writel_relaxed(msm_host->dll_hsr->dll_config_3,
					host->ioaddr +
					msm_offset->core_dll_config_3);
			writel_relaxed(msm_host->dll_hsr->dll_usr_ctl,
					host->ioaddr +
					msm_offset->core_dll_usr_ctl);
		} else {
			writel_relaxed(DLL_CONFIG_3_POR_VAL, host->ioaddr +
				msm_offset->core_dll_config_3);
			writel_relaxed(DLL_USR_CTL_POR_VAL | FINE_TUNE_MODE_EN |
					ENABLE_DLL_LOCK_STATUS | BIAS_OK_SIGNAL,
					host->ioaddr +
					msm_offset->core_dll_usr_ctl);
		}
	}

	/*
	 * Step 8 - Set DDR_CONFIG since step 7 is setting TEST_CTRL
	 * that can be skipped.
	 */
	if (msm_host->updated_ddr_cfg)
		ddr_cfg_offset = msm_offset->core_ddr_config;
	else
		ddr_cfg_offset = msm_offset->core_ddr_config_old;

	if (msm_host->dll_hsr && msm_host->dll_hsr->ddr_config)
		writel_relaxed(msm_host->dll_hsr->ddr_config, host->ioaddr +
			ddr_cfg_offset);
	else
		writel_relaxed(DDR_CONFIG_POR_VAL, host->ioaddr +
			ddr_cfg_offset);

	/* Step 9 - Set DLL_CONFIG_2 */
	if (msm_host->use_14lpp_dll_reset) {
		u32 mclk_freq = 0;
		int cycle_cnt = 0;
		/*
		 * Only configure the mclk_freq in normal DLL init
		 * context. If the DLL init is coming from
		 * CX Collapse Exit context, the host->clock may be zero.
		 * The DLL_CONFIG_2 register has already been restored to
		 * proper value prior to getting here.
		 */
		if (init_context == DLL_INIT_NORMAL) {
			cycle_cnt = readl_relaxed(host->ioaddr +
					msm_offset->core_dll_config_2)
					& CORE_FLL_CYCLE_CNT ? 8 : 4;

			mclk_freq = ROUND(dll_clock * cycle_cnt, TCXO_FREQ);
			if (dll_clock < 100000000)
				pr_err("%s: %s: Non standard clk freq =%u\n",
				mmc_hostname(mmc), __func__, dll_clock);
			writel_relaxed(((readl_relaxed(host->ioaddr +
				msm_offset->core_dll_config_2)
				& ~(0xFF << 10)) | (mclk_freq << 10)),
				host->ioaddr + msm_offset->core_dll_config_2);
		}

		if (msm_host->dll_lock_bist_fail_wa &&
			(curr_ios.timing == MMC_TIMING_UHS_SDR104 ||
				!mmc_card_is_removable(mmc))) {
			writel_relaxed((readl_relaxed(host->ioaddr +
				msm_offset->core_dll_config_2)
				| CORE_LOW_FREQ_MODE), host->ioaddr +
				msm_offset->core_dll_config_2);
		}
		/* wait for 5us before enabling DLL clock */
		udelay(5);
	}

	/*
	 * Step 10 - Update the lower two bytes of DLL_CONFIG only with
	 * HSR values. Since these are the static settings.
	 */
	if (msm_host->dll_hsr) {
		writel_relaxed((readl_relaxed(host->ioaddr +
			msm_offset->core_dll_config) |
			(msm_host->dll_hsr->dll_config & 0xffff)),
			host->ioaddr + msm_offset->core_dll_config);
	}

	/*
	 * Configure DLL user control register to enable DLL status.
	 * This setting is applicable to SDCC v5.1 onwards only.
	 */
	if (msm_host->uses_tassadar_dll) {
		u32 config;
		config = DLL_USR_CTL_POR_VAL | FINE_TUNE_MODE_EN |
			ENABLE_DLL_LOCK_STATUS | BIAS_OK_SIGNAL;
		writel_relaxed(config, host->ioaddr +
				msm_offset->core_dll_usr_ctl);

		config = readl_relaxed(host->ioaddr +
				msm_offset->core_dll_config_3);
		config &= ~0xFF;
		if (msm_host->clk_rate < 150000000)
			config |= DLL_CONFIG_3_LOW_FREQ_VAL;
		else
			config |= DLL_CONFIG_3_HIGH_FREQ_VAL;
		writel_relaxed(config, host->ioaddr +
			msm_offset->core_dll_config_3);
	}

	/* Step 11 - Wait for 52us */
	spin_unlock_irqrestore(&host->lock, flags);
	usleep_range(55, 60);
	spin_lock_irqsave(&host->lock, flags);

	/*
	 * Step12 - Write 0 to DLL_RST bit of DLL_CONFIG register
	 * and Write 0 to DLL_PDN bit of DLL_CONFIG register.
	 */
	writel_relaxed((readl_relaxed(host->ioaddr +
		msm_offset->core_dll_config) & ~CORE_DLL_RST),
		host->ioaddr + msm_offset->core_dll_config);

	writel_relaxed((readl_relaxed(host->ioaddr +
		msm_offset->core_dll_config) & ~CORE_DLL_PDN),
		host->ioaddr + msm_offset->core_dll_config);

	/* Step 13 - Write 1 to DLL_RST bit of DLL_CONFIG register */
	writel_relaxed((readl_relaxed(host->ioaddr +
		msm_offset->core_dll_config) | CORE_DLL_RST),
		host->ioaddr + msm_offset->core_dll_config);

	/* Step 14 - Write 0 to DLL_RST bit of DLL_CONFIG register */
	writel_relaxed((readl_relaxed(host->ioaddr +
		msm_offset->core_dll_config) & ~CORE_DLL_RST),
		host->ioaddr + msm_offset->core_dll_config);

	/* Step 15 - Set CORE_DLL_CLOCK_DISABLE to 0 */
	if (msm_host->use_14lpp_dll_reset) {
		writel_relaxed((readl_relaxed(host->ioaddr +
				msm_offset->core_dll_config_2)
				& ~CORE_DLL_CLOCK_DISABLE), host->ioaddr +
				msm_offset->core_dll_config_2);
	}

	/* Step 16 - Set DLL_EN bit to 1. */
	writel_relaxed((readl_relaxed(host->ioaddr +
			msm_offset->core_dll_config) | CORE_DLL_EN),
			host->ioaddr + msm_offset->core_dll_config);

	/*
	 * Step 17 - Wait for 8000 input clock. Here we calculate the
	 * delay from fixed clock freq 192MHz, which turns out 42us.
	 */
	spin_unlock_irqrestore(&host->lock, flags);
	usleep_range(45, 50);
	spin_lock_irqsave(&host->lock, flags);

	/* Step 18 - Set CK_OUT_EN bit to 1. */
	writel_relaxed((readl_relaxed(host->ioaddr +
			msm_offset->core_dll_config)
			| CORE_CK_OUT_EN), host->ioaddr +
			msm_offset->core_dll_config);

	/*
	 * Step 19 - Wait until DLL_LOCK bit of DLL_STATUS register
	 * becomes '1'.
	 */
	while (!(readl_relaxed(host->ioaddr + msm_offset->core_dll_status) &
		 CORE_DLL_LOCK)) {
		/* max. wait for 50us sec for LOCK bit to be set */
		if (--wait_cnt == 0) {
			dev_err(mmc_dev(mmc), "%s: DLL failed to LOCK\n",
			       mmc_hostname(mmc));
			rc = -ETIMEDOUT;
			goto out;
		}
		/* wait for 1us before polling again */
		udelay(1);
	}

out:
	if (core_vendor_spec & CORE_CLK_PWRSAVE) {
		/* Step 20 - Reenable PWRSAVE as needed */
		writel_relaxed((readl_relaxed(host->ioaddr +
			msm_offset->core_vendor_spec)
			| CORE_CLK_PWRSAVE), host->ioaddr +
			msm_offset->core_vendor_spec);
	}

	spin_unlock_irqrestore(&host->lock, flags);
	return rc;
}

static void msm_hc_select_default(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	u32 config;
	const struct sdhci_msm_offset *msm_offset =
					msm_host->offset;

	if (!msm_host->use_cdclp533) {
		config = readl_relaxed(host->ioaddr +
				msm_offset->core_vendor_spec3);
		config &= ~CORE_PWRSAVE_DLL;
		writel_relaxed(config, host->ioaddr +
				msm_offset->core_vendor_spec3);
	}

	config = readl_relaxed(host->ioaddr + msm_offset->core_vendor_spec);
	config &= ~CORE_HC_MCLK_SEL_MASK;
	config |= CORE_HC_MCLK_SEL_DFLT;
	writel_relaxed(config, host->ioaddr + msm_offset->core_vendor_spec);

	/*
	 * Disable HC_SELECT_IN to be able to use the UHS mode select
	 * configuration from Host Control2 register for all other
	 * modes.
	 * Write 0 to HC_SELECT_IN and HC_SELECT_IN_EN field
	 * in VENDOR_SPEC_FUNC
	 */
	config = readl_relaxed(host->ioaddr + msm_offset->core_vendor_spec);
	config &= ~CORE_HC_SELECT_IN_EN;
	config &= ~CORE_HC_SELECT_IN_MASK;
	writel_relaxed(config, host->ioaddr + msm_offset->core_vendor_spec);

	/*
	 * Make sure above writes impacting free running MCLK are completed
	 * before changing the clk_rate at GCC.
	 */
	wmb();
}

static void msm_hc_select_hs400(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	struct mmc_ios ios = host->mmc->ios;
	u32 config, dll_lock;
	int rc;
	const struct sdhci_msm_offset *msm_offset =
					msm_host->offset;

	/* Select the divided clock (free running MCLK/2) */
	config = readl_relaxed(host->ioaddr + msm_offset->core_vendor_spec);
	config &= ~CORE_HC_MCLK_SEL_MASK;
	config |= CORE_HC_MCLK_SEL_HS400;

	writel_relaxed(config, host->ioaddr + msm_offset->core_vendor_spec);
	/*
	 * Select HS400 mode using the HC_SELECT_IN from VENDOR SPEC
	 * register
	 */
	if ((msm_host->tuning_done || ios.enhanced_strobe) &&
	    !msm_host->calibration_done) {
		config = readl_relaxed(host->ioaddr +
				msm_offset->core_vendor_spec);
		config |= CORE_HC_SELECT_IN_HS400;
		config |= CORE_HC_SELECT_IN_EN;
		writel_relaxed(config, host->ioaddr +
				msm_offset->core_vendor_spec);
	}
	if (!msm_host->clk_rate && !msm_host->use_cdclp533) {
		/*
		 * Poll on DLL_LOCK or DDR_DLL_LOCK bits in
		 * core_dll_status to be set. This should get set
		 * within 15 us at 200 MHz.
		 */
		rc = readl_relaxed_poll_timeout(host->ioaddr +
						msm_offset->core_dll_status,
						dll_lock,
						(dll_lock &
						(CORE_DLL_LOCK |
						CORE_DDR_DLL_LOCK)), 10,
						1000);
		if (rc == -ETIMEDOUT)
			pr_err("%s: Unable to get DLL_LOCK/DDR_DLL_LOCK, dll_status: 0x%08x\n",
			       mmc_hostname(host->mmc), dll_lock);
	}
	/*
	 * Make sure above writes impacting free running MCLK are completed
	 * before changing the clk_rate at GCC.
	 */
	wmb();
}

/*
 * sdhci_msm_hc_select_mode :- In general all timing modes are
 * controlled via UHS mode select in Host Control2 register.
 * eMMC specific HS200/HS400 doesn't have their respective modes
 * defined here, hence we use these values.
 *
 * HS200 - SDR104 (Since they both are equivalent in functionality)
 * HS400 - This involves multiple configurations
 *		Initially SDR104 - when tuning is required as HS200
 *		Then when switching to DDR @ 400MHz (HS400) we use
 *		the vendor specific HC_SELECT_IN to control the mode.
 *
 * In addition to controlling the modes we also need to select the
 * correct input clock for DLL depending on the mode.
 *
 * HS400 - divided clock (free running MCLK/2)
 * All other modes - default (free running MCLK)
 */
static void sdhci_msm_hc_select_mode(struct sdhci_host *host)
{
	struct mmc_ios ios = host->mmc->ios;

	if (ios.timing == MMC_TIMING_MMC_HS400 ||
	    host->flags & SDHCI_HS400_TUNING)
		msm_hc_select_hs400(host);
	else
		msm_hc_select_default(host);
}

static int sdhci_msm_cdclp533_calibration(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	u32 config, calib_done;
	int ret;
	const struct sdhci_msm_offset *msm_offset =
					msm_host->offset;

	pr_debug("%s: %s: Enter\n", mmc_hostname(host->mmc), __func__);

	/*
	 * Retuning in HS400 (DDR mode) will fail, just reset the
	 * tuning block and restore the saved tuning phase.
	 */
	ret = msm_init_cm_dll(host, DLL_INIT_NORMAL);
	if (ret)
		goto out;

	/* Set the selected phase in delay line hw block */
	ret = msm_config_cm_dll_phase(host, msm_host->saved_tuning_phase);
	if (ret)
		goto out;

	config = readl_relaxed(host->ioaddr + msm_offset->core_dll_config);
	config |= CORE_CMD_DAT_TRACK_SEL;
	writel_relaxed(config, host->ioaddr + msm_offset->core_dll_config);

	config = readl_relaxed(host->ioaddr + msm_offset->core_ddr_200_cfg);
	config &= ~CORE_CDC_T4_DLY_SEL;
	writel_relaxed(config, host->ioaddr + msm_offset->core_ddr_200_cfg);

	config = readl_relaxed(host->ioaddr + CORE_CSR_CDC_GEN_CFG);
	config &= ~CORE_CDC_SWITCH_BYPASS_OFF;
	writel_relaxed(config, host->ioaddr + CORE_CSR_CDC_GEN_CFG);

	config = readl_relaxed(host->ioaddr + CORE_CSR_CDC_GEN_CFG);
	config |= CORE_CDC_SWITCH_RC_EN;
	writel_relaxed(config, host->ioaddr + CORE_CSR_CDC_GEN_CFG);

	config = readl_relaxed(host->ioaddr + msm_offset->core_ddr_200_cfg);
	config &= ~CORE_START_CDC_TRAFFIC;
	writel_relaxed(config, host->ioaddr + msm_offset->core_ddr_200_cfg);

	/* Perform CDC Register Initialization Sequence */

	writel_relaxed(0x11800EC, host->ioaddr + CORE_CSR_CDC_CTLR_CFG0);
	writel_relaxed(0x3011111, host->ioaddr + CORE_CSR_CDC_CTLR_CFG1);
	writel_relaxed(0x1201000, host->ioaddr + CORE_CSR_CDC_CAL_TIMER_CFG0);
	writel_relaxed(0x4, host->ioaddr + CORE_CSR_CDC_CAL_TIMER_CFG1);
	writel_relaxed(0xCB732020, host->ioaddr + CORE_CSR_CDC_REFCOUNT_CFG);
	writel_relaxed(0xB19, host->ioaddr + CORE_CSR_CDC_COARSE_CAL_CFG);
	writel_relaxed(0x4E2, host->ioaddr + CORE_CSR_CDC_DELAY_CFG);
	writel_relaxed(0x0, host->ioaddr + CORE_CDC_OFFSET_CFG);
	writel_relaxed(0x16334, host->ioaddr + CORE_CDC_SLAVE_DDA_CFG);

	/* CDC HW Calibration */

	config = readl_relaxed(host->ioaddr + CORE_CSR_CDC_CTLR_CFG0);
	config |= CORE_SW_TRIG_FULL_CALIB;
	writel_relaxed(config, host->ioaddr + CORE_CSR_CDC_CTLR_CFG0);

	config = readl_relaxed(host->ioaddr + CORE_CSR_CDC_CTLR_CFG0);
	config &= ~CORE_SW_TRIG_FULL_CALIB;
	writel_relaxed(config, host->ioaddr + CORE_CSR_CDC_CTLR_CFG0);

	config = readl_relaxed(host->ioaddr + CORE_CSR_CDC_CTLR_CFG0);
	config |= CORE_HW_AUTOCAL_ENA;
	writel_relaxed(config, host->ioaddr + CORE_CSR_CDC_CTLR_CFG0);

	config = readl_relaxed(host->ioaddr + CORE_CSR_CDC_CAL_TIMER_CFG0);
	config |= CORE_TIMER_ENA;
	writel_relaxed(config, host->ioaddr + CORE_CSR_CDC_CAL_TIMER_CFG0);

	ret = readl_relaxed_poll_timeout(host->ioaddr + CORE_CSR_CDC_STATUS0,
					 calib_done,
					 (calib_done & CORE_CALIBRATION_DONE),
					 1, 50);

	if (ret == -ETIMEDOUT) {
		pr_err("%s: %s: CDC calibration was not completed\n",
		       mmc_hostname(host->mmc), __func__);
		goto out;
	}

	ret = readl_relaxed(host->ioaddr + CORE_CSR_CDC_STATUS0)
			& CORE_CDC_ERROR_CODE_MASK;
	if (ret) {
		pr_err("%s: %s: CDC error code %d\n",
		       mmc_hostname(host->mmc), __func__, ret);
		ret = -EINVAL;
		goto out;
	}

	config = readl_relaxed(host->ioaddr + msm_offset->core_ddr_200_cfg);
	config |= CORE_START_CDC_TRAFFIC;
	writel_relaxed(config, host->ioaddr + msm_offset->core_ddr_200_cfg);
out:
	pr_debug("%s: %s: Exit, ret %d\n", mmc_hostname(host->mmc),
		 __func__, ret);
	return ret;
}

static int sdhci_msm_cm_dll_sdc4_calibration(struct sdhci_host *host)
{
	struct mmc_host *mmc = host->mmc;
	u32 dll_status, config, ddr_cfg_offset;
	int ret;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_msm_offset *msm_offset =
					sdhci_priv_msm_offset(host);

	pr_debug("%s: %s: Enter\n", mmc_hostname(host->mmc), __func__);

	/*
	 * Currently the core_ddr_config register defaults to desired
	 * configuration on reset. Currently reprogramming the power on
	 * reset (POR) value in case it might have been modified by
	 * bootloaders. In the future, if this changes, then the desired
	 * values will need to be programmed appropriately.
	 */
	if (msm_host->updated_ddr_cfg)
		ddr_cfg_offset = msm_offset->core_ddr_config;
	else
		ddr_cfg_offset = msm_offset->core_ddr_config_old;

	if (msm_host->dll_hsr->ddr_config)
		config = msm_host->dll_hsr->ddr_config;
	else
		config = DDR_CONFIG_POR_VAL;

	writel_relaxed(config, host->ioaddr + ddr_cfg_offset);

	if (mmc->ios.enhanced_strobe) {
		config = readl_relaxed(host->ioaddr +
				msm_offset->core_ddr_200_cfg);
		config |= CORE_CMDIN_RCLK_EN;
		writel_relaxed(config, host->ioaddr +
				msm_offset->core_ddr_200_cfg);
	}

	config = readl_relaxed(host->ioaddr + msm_offset->core_dll_config_2);
	config |= CORE_DDR_CAL_EN;
	writel_relaxed(config, host->ioaddr + msm_offset->core_dll_config_2);

	ret = readl_relaxed_poll_timeout(host->ioaddr +
					msm_offset->core_dll_status,
					dll_status,
					(dll_status & CORE_DDR_DLL_LOCK),
					10, 1000);

	if (ret == -ETIMEDOUT) {
		pr_err("%s: %s: CM_DLL_SDC4 calibration was not completed\n",
		       mmc_hostname(host->mmc), __func__);
		goto out;
	}

	/*
	 * Set CORE_PWRSAVE_DLL bit in CORE_VENDOR_SPEC3.
	 * When MCLK is gated OFF, it is not gated for less than 0.5us
	 * and MCLK must be switched on for at-least 1us before DATA
	 * starts coming. Controllers with 14lpp and later tech DLL cannot
	 * guarantee above requirement. So PWRSAVE_DLL should not be
	 * turned on for host controllers using this DLL.
	 */
	if (!msm_host->use_14lpp_dll_reset) {
		config = readl_relaxed(host->ioaddr +
				msm_offset->core_vendor_spec3);
		config |= CORE_PWRSAVE_DLL;
		writel_relaxed(config, host->ioaddr +
				msm_offset->core_vendor_spec3);
	}

	/*
	 * Drain writebuffer to ensure above DLL calibration
	 * and PWRSAVE DLL is enabled.
	 */
	wmb();
out:
	pr_debug("%s: %s: Exit, ret %d\n", mmc_hostname(host->mmc),
		 __func__, ret);
	return ret;
}

static int sdhci_msm_hs400_dll_calibration(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	int ret;
	struct mmc_host *mmc = host->mmc;
	u32 config;
	const struct sdhci_msm_offset *msm_offset =
					msm_host->offset;

	pr_debug("%s: %s: Enter\n", mmc_hostname(host->mmc), __func__);

	/*
	 * Retuning in HS400 (DDR mode) will fail, just reset the
	 * tuning block and restore the saved tuning phase.
	 */
	ret = msm_init_cm_dll(host, DLL_INIT_NORMAL);
	if (ret)
		goto out;

	if (!mmc->ios.enhanced_strobe) {
		/* set the selected phase in delay line hw block */
		ret = msm_config_cm_dll_phase(host,
				      msm_host->saved_tuning_phase);
		if (ret)
			goto out;
		config = readl_relaxed(host->ioaddr +
				msm_offset->core_dll_config);
		config |= CORE_CMD_DAT_TRACK_SEL;
		writel_relaxed(config, host->ioaddr +
				msm_offset->core_dll_config);
	}

	if (msm_host->use_cdclp533)
		ret = sdhci_msm_cdclp533_calibration(host);
	else
		ret = sdhci_msm_cm_dll_sdc4_calibration(host);
out:
	pr_debug("%s: %s: Exit, ret %d\n", mmc_hostname(host->mmc),
		 __func__, ret);
	return ret;
}

static bool sdhci_msm_is_tuning_needed(struct sdhci_host *host)
{
	struct mmc_ios *ios = &host->mmc->ios;

	if (ios->timing == MMC_TIMING_UHS_SDR50 &&
			host->flags & SDHCI_SDR50_NEEDS_TUNING)
		return true;

	/*
	 * Tuning is required for SDR104, HS200 and HS400 cards and
	 * if clock frequency is greater than 100MHz in these modes.
	 */
	if (host->clock <= CORE_FREQ_100MHZ ||
	    !(ios->timing == MMC_TIMING_MMC_HS400 ||
	    ios->timing == MMC_TIMING_MMC_HS200 ||
	    ios->timing == MMC_TIMING_UHS_SDR104) ||
	    ios->enhanced_strobe)
		return false;

	return true;
}

static int sdhci_msm_restore_sdr_dll_config(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	int ret;

	/*
	 * SDR DLL comes into picture only for timing modes which needs
	 * tuning.
	 */
	if (!sdhci_msm_is_tuning_needed(host))
		return 0;

	/* Reset the tuning block */
	ret = msm_init_cm_dll(host, DLL_INIT_NORMAL);
	if (ret)
		return ret;

	/* Restore the tuning block */
	ret = msm_config_cm_dll_phase(host, msm_host->saved_tuning_phase);

	return ret;
}

static void sdhci_msm_set_cdr(struct sdhci_host *host, bool enable)
{
	const struct sdhci_msm_offset *msm_offset = sdhci_priv_msm_offset(host);
	u32 config, oldconfig = readl_relaxed(host->ioaddr +
					      msm_offset->core_dll_config);

	config = oldconfig;
	if (enable) {
		config |= CORE_CDR_EN;
		config &= ~CORE_CDR_EXT_EN;
	} else {
		config &= ~CORE_CDR_EN;
		config |= CORE_CDR_EXT_EN;
	}

	if (config != oldconfig) {
		writel_relaxed(config, host->ioaddr +
			       msm_offset->core_dll_config);
	}
}

static int sdhci_msm_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct sdhci_host *host = mmc_priv(mmc);
	int tuning_seq_cnt = 10;
	u8 phase, tuned_phases[16], tuned_phase_cnt = 0;
	int rc = 0;
	u32 config;
	struct mmc_ios ios = host->mmc->ios;
	u32 core_vendor_spec;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_msm_offset *msm_offset =
					sdhci_priv_msm_offset(host);

	if (!sdhci_msm_is_tuning_needed(host)) {
		msm_host->use_cdr = false;
		sdhci_msm_set_cdr(host, false);
		return 0;
	}

	/* Clock-Data-Recovery used to dynamically adjust RX sampling point */
	msm_host->use_cdr = true;

	/*
	 * Clear tuning_done flag before tuning to ensure proper
	 * HS400 settings.
	 */
	msm_host->tuning_done = 0;

	if (ios.timing == MMC_TIMING_UHS_SDR50 &&
			host->flags & SDHCI_SDR50_NEEDS_TUNING) {
		/*
		 * Bit1 SDHCI_CTRL_UHS_SDR50 of the Host Control 2 register is
		 * already set by the sdhci_set_ios -> sdhci_msm_set_uhs_signaling().
		 * It is not necessary to set it again here.
		 */

		config = readl_relaxed(host->ioaddr + msm_offset->core_vendor_spec);
		config |= CORE_HC_SELECT_IN_EN;
		config &= ~CORE_HC_SELECT_IN_MASK;
		config |= CORE_HC_SELECT_IN_SDR50;
		writel_relaxed(config, host->ioaddr + msm_offset->core_vendor_spec);
	}

	/*
	 * For HS400 tuning in HS200 timing requires:
	 * - select MCLK/2 in VENDOR_SPEC
	 * - program MCLK to 400MHz (or nearest supported) in GCC
	 */
	if (host->flags & SDHCI_HS400_TUNING) {
		sdhci_msm_hc_select_mode(host);
		msm_set_clock_rate_for_bus_mode(host, ios.clock);
		host->flags &= ~SDHCI_HS400_TUNING;
	}

	core_vendor_spec = readl_relaxed(host->ioaddr +
			msm_offset->core_vendor_spec);

	/* Make sure that PWRSAVE bit is set to '0' during tuning */
	writel_relaxed((core_vendor_spec & ~CORE_CLK_PWRSAVE),
			host->ioaddr +
			msm_offset->core_vendor_spec);

retry:
	/* First of all reset the tuning block */
	rc = msm_init_cm_dll(host, DLL_INIT_NORMAL);
	if (rc)
		goto out;

	phase = 0;
	do {
		/* Set the phase in delay line hw block */
		rc = msm_config_cm_dll_phase(host, phase);
		if (rc)
			goto out;

		rc = mmc_send_tuning(mmc, opcode, NULL);
		if (!rc) {
			/* Tuning is successful at this tuning point */
			tuned_phases[tuned_phase_cnt++] = phase;
			dev_dbg(mmc_dev(mmc), "%s: Found good phase = %d\n",
				 mmc_hostname(mmc), phase);
		}
	} while (++phase < ARRAY_SIZE(tuned_phases));

	if (tuned_phase_cnt) {
		if (tuned_phase_cnt == ARRAY_SIZE(tuned_phases)) {
			/*
			 * All phases valid is _almost_ as bad as no phases
			 * valid.  Probably all phases are not really reliable
			 * but we didn't detect where the unreliable place is.
			 * That means we'll essentially be guessing and hoping
			 * we get a good phase.  Better to try a few times.
			 */
			dev_dbg(mmc_dev(mmc), "%s: All phases valid; try again\n",
				mmc_hostname(mmc));
			if (--tuning_seq_cnt) {
				tuned_phase_cnt = 0;
				goto retry;
			}
		}

		rc = msm_find_most_appropriate_phase(host, tuned_phases,
						     tuned_phase_cnt);
		if (rc < 0)
			goto out;
		else
			phase = rc;

		/*
		 * Finally set the selected phase in delay
		 * line hw block.
		 */
		rc = msm_config_cm_dll_phase(host, phase);
		if (rc)
			goto out;
		msm_host->saved_tuning_phase = phase;
		dev_dbg(mmc_dev(mmc), "%s: Setting the tuning phase to %d\n",
			 mmc_hostname(mmc), phase);
	} else {
		if (--tuning_seq_cnt)
			goto retry;
		/* Tuning failed */
		dev_dbg(mmc_dev(mmc), "%s: No tuning point found\n",
		       mmc_hostname(mmc));
		rc = -EIO;
	}

	if (!rc)
		msm_host->tuning_done = true;
out:

	/* Set PWRSAVE bit to '1' after completion of tuning as needed */
	if (core_vendor_spec & CORE_CLK_PWRSAVE) {
		writel_relaxed((readl_relaxed(host->ioaddr +
			msm_offset->core_vendor_spec)
			| CORE_CLK_PWRSAVE), host->ioaddr +
			msm_offset->core_vendor_spec);
	}

	return rc;
}

/*
 * sdhci_msm_hs400 - Calibrate the DLL for HS400 bus speed mode operation.
 * This needs to be done for both tuning and enhanced_strobe mode.
 * DLL operation is only needed for clock > 100MHz. For clock <= 100MHz
 * fixed feedback clock is used.
 */
static void sdhci_msm_hs400(struct sdhci_host *host, struct mmc_ios *ios)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	int ret;

	if (host->clock > CORE_FREQ_100MHZ &&
	    (msm_host->tuning_done || ios->enhanced_strobe) &&
	    !msm_host->calibration_done) {
		ret = sdhci_msm_hs400_dll_calibration(host);
		if (!ret)
			msm_host->calibration_done = true;
		else
			pr_err("%s: Failed to calibrate DLL for hs400 mode (%d)\n",
			       mmc_hostname(host->mmc), ret);
	}
}

static void sdhci_msm_set_uhs_signaling(struct sdhci_host *host,
					unsigned int uhs)
{
	struct mmc_host *mmc = host->mmc;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	u16 ctrl_2;
	u32 config;
	const struct sdhci_msm_offset *msm_offset =
					msm_host->offset;

	ctrl_2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	/* Select Bus Speed Mode for host */
	ctrl_2 &= ~SDHCI_CTRL_UHS_MASK;
	switch (uhs) {
	case MMC_TIMING_UHS_SDR12:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR12;
		break;
	case MMC_TIMING_UHS_SDR25:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR25;
		break;
	case MMC_TIMING_UHS_SDR50:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR50;
		break;
	case MMC_TIMING_MMC_HS400:
	case MMC_TIMING_MMC_HS200:
	case MMC_TIMING_UHS_SDR104:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR104;
		break;
	case MMC_TIMING_UHS_DDR50:
	case MMC_TIMING_MMC_DDR52:
		ctrl_2 |= SDHCI_CTRL_UHS_DDR50;
		break;
	}

	/*
	 * When clock frequency is less than 100MHz, the feedback clock must be
	 * provided and DLL must not be used so that tuning can be skipped. To
	 * provide feedback clock, the mode selection can be any value less
	 * than 3'b011 in bits [2:0] of HOST CONTROL2 register.
	 */
	if (host->clock <= CORE_FREQ_100MHZ) {
		if (uhs == MMC_TIMING_MMC_HS400 ||
		    uhs == MMC_TIMING_MMC_HS200 ||
		    uhs == MMC_TIMING_UHS_SDR104)
			ctrl_2 &= ~SDHCI_CTRL_UHS_MASK;
		/*
		 * DLL is not required for clock <= 100MHz
		 * Thus, make sure DLL it is disabled when not required
		 */
		config = readl_relaxed(host->ioaddr +
				msm_offset->core_dll_config);
		config |= CORE_DLL_RST;
		writel_relaxed(config, host->ioaddr +
				msm_offset->core_dll_config);

		config = readl_relaxed(host->ioaddr +
				msm_offset->core_dll_config);
		config |= CORE_DLL_PDN;
		writel_relaxed(config, host->ioaddr +
				msm_offset->core_dll_config);

		/*
		 * The DLL needs to be restored and CDCLP533 recalibrated
		 * when the clock frequency is set back to 400MHz.
		 */
		msm_host->calibration_done = false;
	}

	dev_dbg(mmc_dev(mmc), "%s: clock=%u uhs=%u ctrl_2=0x%x\n",
		mmc_hostname(host->mmc), host->clock, uhs, ctrl_2);
	sdhci_writew(host, ctrl_2, SDHCI_HOST_CONTROL2);

	if (mmc->ios.timing == MMC_TIMING_MMC_HS400)
		sdhci_msm_hs400(host, &mmc->ios);
}

/*
 * Ensure larger discard size by always setting max_busy_timeout to zero.
 * This will always return max_busy_timeout as zero to the sdhci layer.
 */

static unsigned int sdhci_msm_get_max_timeout_count(struct sdhci_host *host)
{
	return 0;
}

#define MAX_PROP_SIZE 32
static int sdhci_msm_dt_parse_vreg_info(struct device *dev,
		struct sdhci_msm_reg_data **vreg_data, const char *vreg_name)
{
	int len, ret = 0;
	const __be32 *prop;
	char prop_name[MAX_PROP_SIZE];
	struct sdhci_msm_reg_data *vreg;
	struct device_node *np = dev->of_node;

	snprintf(prop_name, MAX_PROP_SIZE, "%s-supply", vreg_name);
	if (!of_parse_phandle(np, prop_name, 0)) {
		dev_info(dev, "No vreg data found for %s\n", vreg_name);
		return ret;
	}

	vreg = devm_kzalloc(dev, sizeof(*vreg), GFP_KERNEL);
	if (!vreg) {
		ret = -ENOMEM;
		return ret;
	}

	vreg->name = vreg_name;

	snprintf(prop_name, MAX_PROP_SIZE,
			"qcom,%s-always-on", vreg_name);
	if (of_get_property(np, prop_name, NULL))
		vreg->is_always_on = true;

	snprintf(prop_name, MAX_PROP_SIZE,
			"qcom,%s-lpm-sup", vreg_name);
	if (of_get_property(np, prop_name, NULL))
		vreg->lpm_sup = true;

	snprintf(prop_name, MAX_PROP_SIZE,
			"qcom,%s-voltage-level", vreg_name);
	prop = of_get_property(np, prop_name, &len);
	if (!prop || (len != (2 * sizeof(__be32)))) {
		dev_warn(dev, "%s %s property\n",
			prop ? "invalid format" : "no", prop_name);
	} else {
		vreg->low_vol_level = be32_to_cpup(&prop[0]);
		vreg->high_vol_level = be32_to_cpup(&prop[1]);
	}

	snprintf(prop_name, MAX_PROP_SIZE,
			"qcom,%s-current-level", vreg_name);
	prop = of_get_property(np, prop_name, &len);
	if (!prop || (len != (2 * sizeof(__be32)))) {
		dev_warn(dev, "%s %s property\n",
			prop ? "invalid format" : "no", prop_name);
	} else {
		vreg->lpm_uA = be32_to_cpup(&prop[0]);
		vreg->hpm_uA = be32_to_cpup(&prop[1]);
	}

	*vreg_data = vreg;
	dev_dbg(dev, "%s: %s %s vol=[%d %d]uV, curr=[%d %d]uA\n",
		vreg->name, vreg->is_always_on ? "always_on," : "",
		vreg->lpm_sup ? "lpm_sup," : "", vreg->low_vol_level,
		vreg->high_vol_level, vreg->lpm_uA, vreg->hpm_uA);

	return ret;
}

static int sdhci_msm_set_pincfg(struct sdhci_msm_host *msm_host, bool level)
{
	struct platform_device *pdev = msm_host->pdev;
	int ret;

	if (level)
		ret = pinctrl_pm_select_default_state(&pdev->dev);
	else
		ret = pinctrl_pm_select_sleep_state(&pdev->dev);

	return ret;
}

static int sdhci_msm_dt_parse_hsr_info(struct device *dev,
		struct sdhci_msm_host *msm_host)

{
	u32 *dll_hsr_table = NULL;
	int dll_hsr_table_len, dll_hsr_reg_count;
	int ret = 0;

	if (sdhci_msm_dt_get_array(dev, "qcom,dll-hsr-list",
			&dll_hsr_table, &dll_hsr_table_len, 0))
		goto skip_hsr;

	dll_hsr_reg_count = sizeof(struct sdhci_msm_dll_hsr) / sizeof(u32);
	if (dll_hsr_table_len != dll_hsr_reg_count) {
		dev_err(dev, "Number of HSR entries are not matching\n");
		ret = -EINVAL;
	} else {
		msm_host->dll_hsr = (struct sdhci_msm_dll_hsr *)dll_hsr_table;
	}

skip_hsr:
	if (!msm_host->dll_hsr)
		dev_info(dev, "Failed to get dll hsr settings from dt\n");
	return ret;
}

static int sdhci_msm_parse_reset_data(struct device *dev,
			struct sdhci_msm_host *msm_host)
{
	int ret = 0;

	msm_host->core_reset = devm_reset_control_get(dev,
					"core_reset");
	if (IS_ERR(msm_host->core_reset)) {
		ret = PTR_ERR(msm_host->core_reset);
		dev_err(dev, "core_reset unavailable,err = %d\n",
				ret);
		msm_host->core_reset = NULL;
	}

	return ret;
}

/* Parse platform data */
static bool sdhci_msm_populate_pdata(struct device *dev,
						struct sdhci_msm_host *msm_host)
{
	struct device_node *np = dev->of_node;
	int ice_clk_table_len;
	u32 *ice_clk_table = NULL;

	msm_host->vreg_data = devm_kzalloc(dev, sizeof(struct
						    sdhci_msm_vreg_data),
					GFP_KERNEL);
	if (!msm_host->vreg_data) {
		dev_err(dev, "failed to allocate memory for vreg data\n");
		goto out;
	}

	if (sdhci_msm_dt_parse_vreg_info(dev, &msm_host->vreg_data->vdd_data,
					 "vdd")) {
		dev_err(dev, "failed parsing vdd data\n");
		goto out;
	}
	if (sdhci_msm_dt_parse_vreg_info(dev,
					 &msm_host->vreg_data->vdd_io_data,
					 "vdd-io")) {
		dev_err(dev, "failed parsing vdd-io data\n");
		goto out;
	}

	if (of_get_property(np, "qcom,core_3_0v_support", NULL))
		msm_host->fake_core_3_0v_support = true;

	msm_host->regs_restore.is_supported =
		of_property_read_bool(np, "qcom,restore-after-cx-collapse");

	msm_host->uses_level_shifter =
		of_property_read_bool(np, "qcom,uses_level_shifter");

	msm_host->dll_lock_bist_fail_wa =
		of_property_read_bool(np, "qcom,dll_lock_bist_fail_wa");

	if (sdhci_msm_dt_parse_hsr_info(dev, msm_host))
		goto out;

	if (!sdhci_msm_dt_get_array(dev, "qcom,ice-clk-rates",
			&ice_clk_table, &ice_clk_table_len, 0)) {
		if (ice_clk_table && ice_clk_table_len) {
			if (ice_clk_table_len != 2) {
				dev_err(dev, "Need max and min frequencies\n");
				goto out;
			}
			msm_host->sup_ice_clk_table = ice_clk_table;
			msm_host->sup_ice_clk_cnt = ice_clk_table_len;
			msm_host->ice_clk_max = msm_host->sup_ice_clk_table[0];
			msm_host->ice_clk_min = msm_host->sup_ice_clk_table[1];
			dev_dbg(dev, "ICE clock rates (Hz): max: %u min: %u\n",
				msm_host->ice_clk_max, msm_host->ice_clk_min);
		}
	}

	msm_host->vbias_skip_wa =
		of_property_read_bool(np, "qcom,vbias-skip-wa");

	sdhci_msm_parse_reset_data(dev, msm_host);

	return false;
out:
	return true;
}

static int sdhci_msm_set_vmmc(struct mmc_host *mmc)
{
	if (IS_ERR(mmc->supply.vmmc))
		return 0;

	return mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, mmc->ios.vdd);
}

static int msm_toggle_vqmmc(struct sdhci_msm_host *msm_host,
			      struct mmc_host *mmc, bool level)
{
	int ret;
	struct mmc_ios ios;

	if (msm_host->vqmmc_enabled == level)
		return 0;

	if (level) {
		/* Set the IO voltage regulator to default voltage level */
		if (msm_host->caps_0 & CORE_3_0V_SUPPORT)
			ios.signal_voltage = MMC_SIGNAL_VOLTAGE_330;
		else if (msm_host->caps_0 & CORE_1_8V_SUPPORT)
			ios.signal_voltage = MMC_SIGNAL_VOLTAGE_180;

		if (msm_host->caps_0 & CORE_VOLT_SUPPORT) {
			ret = mmc_regulator_set_vqmmc(mmc, &ios);
			if (ret < 0) {
				dev_err(mmc_dev(mmc), "%s: vqmmc set volgate failed: %d\n",
					mmc_hostname(mmc), ret);
				goto out;
			}
		}
		ret = regulator_enable(mmc->supply.vqmmc);
	} else {
		ret = regulator_disable(mmc->supply.vqmmc);
	}

	if (ret)
		dev_err(mmc_dev(mmc), "%s: vqmm %sable failed: %d\n",
			mmc_hostname(mmc), level ? "en":"dis", ret);
	else
		msm_host->vqmmc_enabled = level;
out:
	return ret;
}

static int msm_config_vqmmc_mode(struct sdhci_msm_host *msm_host,
			      struct mmc_host *mmc, bool hpm)
{
	int load, ret;

	load = hpm ? MMC_VQMMC_MAX_LOAD_UA : 0;
	ret = regulator_set_load(mmc->supply.vqmmc, load);
	if (ret)
		dev_err(mmc_dev(mmc), "%s: vqmmc set load failed: %d\n",
			mmc_hostname(mmc), ret);
	return ret;
}

static int sdhci_msm_set_vqmmc(struct sdhci_msm_host *msm_host,
			      struct mmc_host *mmc, bool level)
{
	int ret;
	bool always_on;

	if (IS_ERR(mmc->supply.vqmmc) ||
			(mmc->ios.power_mode == MMC_POWER_UNDEFINED))
		return 0;
	/*
	 * For eMMC don't turn off Vqmmc, Instead just configure it in LPM
	 * and HPM modes by setting the corresponding load.
	 *
	 * Till eMMC is initialized (i.e. always_on == 0), just turn on/off
	 * Vqmmc. Vqmmc gets turned off only if init fails and mmc_power_off
	 * gets invoked. Once eMMC is initialized (i.e. always_on == 1),
	 * Vqmmc should remain ON, So just set the load instead of turning it
	 * off/on.
	 */
	always_on = !mmc_card_is_removable(mmc) &&
			mmc->card && mmc_card_mmc(mmc->card);

	if (always_on)
		ret = msm_config_vqmmc_mode(msm_host, mmc, level);
	else
		ret = msm_toggle_vqmmc(msm_host, mmc, level);

	return ret;
}

static inline void sdhci_msm_init_pwr_irq_wait(struct sdhci_msm_host *msm_host)
{
	init_waitqueue_head(&msm_host->pwr_irq_wait);
}

static inline void sdhci_msm_complete_pwr_irq_wait(
		struct sdhci_msm_host *msm_host)
{
	wake_up(&msm_host->pwr_irq_wait);
}

/*
 * sdhci_msm_check_power_status API should be called when registers writes
 * which can toggle sdhci IO bus ON/OFF or change IO lines HIGH/LOW happens.
 * To what state the register writes will change the IO lines should be passed
 * as the argument req_type. This API will check whether the IO line's state
 * is already the expected state and will wait for power irq only if
 * power irq is expected to be triggered based on the current IO line state
 * and expected IO line state.
 */
static void sdhci_msm_check_power_status(struct sdhci_host *host, u32 req_type)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	bool done = false;
	u32 val = SWITCHABLE_SIGNALING_VOLTAGE;
	const struct sdhci_msm_offset *msm_offset =
					msm_host->offset;
	struct mmc_host *mmc = host->mmc;

	pr_debug("%s: %s: request %d curr_pwr_state %x curr_io_level %x\n",
			mmc_hostname(host->mmc), __func__, req_type,
			msm_host->curr_pwr_state, msm_host->curr_io_level);

	/*
	 * The power interrupt will not be generated for signal voltage
	 * switches if SWITCHABLE_SIGNALING_VOLTAGE in MCI_GENERICS is not set.
	 * Since sdhci-msm-v5, this bit has been removed and SW must consider
	 * it as always set.
	 */
	if (!msm_host->mci_removed)
		val = msm_host_readl(msm_host, host,
				msm_offset->core_generics);
	if ((req_type & REQ_IO_HIGH || req_type & REQ_IO_LOW) &&
	    !(val & SWITCHABLE_SIGNALING_VOLTAGE)) {
		return;
	}

	/*
	 * The IRQ for request type IO High/LOW will be generated when -
	 * there is a state change in 1.8V enable bit (bit 3) of
	 * SDHCI_HOST_CONTROL2 register. The reset state of that bit is 0
	 * which indicates 3.3V IO voltage. So, when MMC core layer tries
	 * to set it to 3.3V before card detection happens, the
	 * IRQ doesn't get triggered as there is no state change in this bit.
	 * The driver already handles this case by changing the IO voltage
	 * level to high as part of controller power up sequence. Hence, check
	 * for host->pwr to handle a case where IO voltage high request is
	 * issued even before controller power up.
	 */
	if ((req_type & REQ_IO_HIGH) && !host->pwr) {
		pr_debug("%s: do not wait for power IRQ that never comes, req_type: %d\n",
				mmc_hostname(host->mmc), req_type);
		return;
	}

	if ((req_type & msm_host->curr_pwr_state) ||
			(req_type & msm_host->curr_io_level))
		done = true;
	/*
	 * This is needed here to handle cases where register writes will
	 * not change the current bus state or io level of the controller.
	 * In this case, no power irq will be triggerred and we should
	 * not wait.
	 */
	if (!done) {
		if (!wait_event_timeout(msm_host->pwr_irq_wait,
				msm_host->pwr_irq_flag,
				msecs_to_jiffies(MSM_PWR_IRQ_TIMEOUT_MS))) {
			dev_warn(&msm_host->pdev->dev,
				 "%s: pwr_irq for req: (%d) timed out\n",
				 mmc_hostname(host->mmc), req_type);
			sdhci_msm_dump_pwr_ctrl_regs(host);
		}

	}

	if (mmc->card && mmc->ops->get_cd && !mmc->ops->get_cd(mmc) &&
			(req_type & REQ_BUS_ON)) {
		host->pwr = 0;
		sdhci_writeb(host, 0, SDHCI_POWER_CONTROL);
	}

	pr_debug("%s: %s: request %d done\n", mmc_hostname(host->mmc),
			__func__, req_type);
}

static void sdhci_msm_dump_pwr_ctrl_regs(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_msm_offset *msm_offset =
					msm_host->offset;

	pr_err("%s: PWRCTL_STATUS: 0x%08x | PWRCTL_MASK: 0x%08x | PWRCTL_CTL: 0x%08x\n",
		mmc_hostname(host->mmc),
		msm_host_readl(msm_host, host, msm_offset->core_pwrctl_status),
		msm_host_readl(msm_host, host, msm_offset->core_pwrctl_mask),
		msm_host_readl(msm_host, host, msm_offset->core_pwrctl_ctl));
}

static int sdhci_msm_clear_pwrctl_status(struct sdhci_host *host, u32 value)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_msm_offset *msm_offset = msm_host->offset;
	int ret = 0, retry = 10;

	/*
	 * There is a rare HW scenario where the first clear pulse could be
	 * lost when actual reset and clear/read of status register is
	 * happening at a time. Hence, retry for at least 10 times to make
	 * sure status register is cleared. Otherwise, this will result in
	 * a spurious power IRQ resulting in system instability.
	 */
	do {
		if (retry == 0) {
			pr_err("%s: Timedout clearing (0x%x) pwrctl status register\n",
				mmc_hostname(host->mmc), value);
			sdhci_msm_dump_pwr_ctrl_regs(host);
			WARN_ON(1);
			ret = -EBUSY;
			break;
		}

		/*
		 * Clear the PWRCTL_STATUS interrupt bits by writing to the
		 * corresponding bits in the PWRCTL_CLEAR register.
		 */
		msm_host_writel(msm_host, value, host,
				msm_offset->core_pwrctl_clear);
		/*
		 * SDHC has core_mem and hc_mem device memory and these memory
		 * addresses do not fall within 1KB region. Hence, any update to
		 * core_mem address space would require an mb() to ensure this
		 * gets completed before its next update to registers within
		 * hc_mem.
		 */
		mb();
		retry--;
		udelay(10);
	} while (value & msm_host_readl(msm_host, host,
				msm_offset->core_pwrctl_status));

	return ret;
}

/* Regulator utility functions */
static int sdhci_msm_vreg_init_reg(struct device *dev,
					struct sdhci_msm_reg_data *vreg)
{
	int ret = 0;

	/* check if regulator is already initialized? */
	if (vreg->reg)
		goto out;

	/* Get the regulator handle */
	vreg->reg = devm_regulator_get(dev, vreg->name);
	if (IS_ERR(vreg->reg)) {
		ret = PTR_ERR(vreg->reg);
		pr_err("%s: devm_regulator_get(%s) failed. ret=%d\n",
			__func__, vreg->name, ret);
		goto out;
	}

	if (regulator_count_voltages(vreg->reg) > 0) {
		vreg->set_voltage_sup = true;
		/* sanity check */
		if (!vreg->high_vol_level || !vreg->hpm_uA) {
			pr_err("%s: %s invalid constraints specified\n",
			       __func__, vreg->name);
			ret = -EINVAL;
		}
	}

out:
	return ret;
}

static int sdhci_msm_vreg_set_optimum_mode(struct sdhci_msm_reg_data
						  *vreg, int uA_load)
{
	int ret = 0;

	/*
	 * regulators that do not support regulator_set_voltage also
	 * do not support regulator_set_optimum_mode
	 */
	if (vreg->set_voltage_sup) {
		ret = regulator_set_load(vreg->reg, uA_load);
		if (ret < 0)
			pr_err("%s: regulator_set_load(reg=%s,uA_load=%d) failed. ret=%d\n",
			       __func__, vreg->name, uA_load, ret);
		else
			/*
			 * regulator_set_load() can return non zero
			 * value even for success case.
			 */
			ret = 0;
	}
	return ret;
}

static int sdhci_msm_vreg_set_voltage(struct sdhci_msm_reg_data *vreg,
					int min_uV, int max_uV)
{
	int ret = 0;

	if (vreg->set_voltage_sup) {
		ret = regulator_set_voltage(vreg->reg, min_uV, max_uV);
		if (ret) {
			pr_err("%s: regulator_set_voltage(%s)failed. min_uV=%d,max_uV=%d,ret=%d\n",
			       __func__, vreg->name, min_uV, max_uV, ret);
		}
	}

	return ret;
}

static int sdhci_msm_vreg_enable(struct sdhci_msm_reg_data *vreg)
{
	int ret = 0;

	/* Put regulator in HPM (high power mode) */
	ret = sdhci_msm_vreg_set_optimum_mode(vreg, vreg->hpm_uA);
	if (ret < 0)
		return ret;

	if (!vreg->is_enabled) {
		/* Set voltage level */
		ret = sdhci_msm_vreg_set_voltage(vreg, vreg->high_vol_level,
						vreg->high_vol_level);
		if (ret)
			return ret;
	}
	ret = regulator_enable(vreg->reg);
	if (ret) {
		pr_err("%s: regulator_enable(%s) failed. ret=%d\n",
				__func__, vreg->name, ret);
		return ret;
	}
	vreg->is_enabled = true;
	return ret;
}

static int sdhci_msm_vreg_disable(struct sdhci_msm_reg_data *vreg)
{
	int ret = 0;

	/* Never disable regulator marked as always_on */
	if (vreg->is_enabled && !vreg->is_always_on) {
		ret = regulator_disable(vreg->reg);
		if (ret) {
			pr_err("%s: regulator_disable(%s) failed. ret=%d\n",
				__func__, vreg->name, ret);
			goto out;
		}
		vreg->is_enabled = false;

		ret = sdhci_msm_vreg_set_optimum_mode(vreg, 0);
		if (ret < 0)
			goto out;

		/* Set min. voltage level to 0 */
		ret = sdhci_msm_vreg_set_voltage(vreg, 0, vreg->high_vol_level);
		if (ret)
			goto out;
	} else if (vreg->is_enabled && vreg->is_always_on) {
		if (vreg->lpm_sup) {
			/* Put always_on regulator in LPM (low power mode) */
			ret = sdhci_msm_vreg_set_optimum_mode(vreg,
							      vreg->lpm_uA);
			if (ret < 0)
				goto out;
		}
	}
out:
	return ret;
}

static int sdhci_msm_setup_vreg(struct sdhci_msm_host *msm_host,
			bool enable, bool is_init)
{
	int ret = 0, i;
	struct sdhci_msm_vreg_data *curr_slot;
	struct sdhci_msm_reg_data *vreg_table[2];
	struct mmc_host *mmc = msm_host->mmc;

	curr_slot = msm_host->vreg_data;
	if (!curr_slot) {
		pr_debug("%s: vreg info unavailable,assuming the slot is powered by always on domain\n",
			 __func__);
		goto out;
	}

	vreg_table[0] = curr_slot->vdd_data;
	vreg_table[1] = curr_slot->vdd_io_data;

	/* When eMMC absent disable regulator marked as always_on */
	if (!enable && vreg_table[1]->is_always_on && !mmc->card)
		vreg_table[1]->is_always_on = false;

	for (i = 0; i < ARRAY_SIZE(vreg_table); i++) {
		if (vreg_table[i]) {
			if (enable)
				ret = sdhci_msm_vreg_enable(vreg_table[i]);
			else
				ret = sdhci_msm_vreg_disable(vreg_table[i]);
			if (ret)
				goto out;
		}
	}
out:
	return ret;
}

/* This init function should be called only once for each SDHC slot */
static int sdhci_msm_vreg_init(struct device *dev,
				struct sdhci_msm_host *msm_host,
				bool is_init)
{
	int ret = 0;
	struct sdhci_msm_vreg_data *curr_slot;
	struct sdhci_msm_reg_data *curr_vdd_reg, *curr_vdd_io_reg;

	curr_slot = msm_host->vreg_data;
	if (!curr_slot)
		goto out;

	curr_vdd_reg = curr_slot->vdd_data;
	curr_vdd_io_reg = curr_slot->vdd_io_data;

	if (!is_init)
		/* Deregister all regulators from regulator framework */
		goto out;

	/*
	 * Get the regulator handle from voltage regulator framework
	 * and then try to set the voltage level for the regulator
	 */
	if (curr_vdd_reg) {
		ret = sdhci_msm_vreg_init_reg(dev, curr_vdd_reg);
		if (ret)
			goto out;
	}
	if (curr_vdd_io_reg)
		ret = sdhci_msm_vreg_init_reg(dev, curr_vdd_io_reg);
out:
	if (ret)
		dev_err(dev, "vreg reset failed (%d)\n", ret);

	return ret;
}

static int sdhci_msm_set_vdd_io_vol(struct sdhci_msm_host *msm_host,
			enum vdd_io_level level,
			unsigned int voltage_level)
{
	int ret = 0;
	int set_level;
	struct sdhci_msm_reg_data *vdd_io_reg;

	if (!msm_host->vreg_data)
		return ret;

	vdd_io_reg = msm_host->vreg_data->vdd_io_data;
	if (vdd_io_reg && vdd_io_reg->is_enabled) {
		switch (level) {
		case VDD_IO_LOW:
			set_level = vdd_io_reg->low_vol_level;
			break;
		case VDD_IO_HIGH:
			set_level = vdd_io_reg->high_vol_level;
			break;
		case VDD_IO_SET_LEVEL:
			set_level = voltage_level;
			break;
		default:
			pr_err("%s: invalid argument level = %d\n",
					__func__, level);
			ret = -EINVAL;
			return ret;
		}
		ret = sdhci_msm_vreg_set_voltage(vdd_io_reg, set_level,
				set_level);
	}
	return ret;
}

static void sdhci_msm_vbias_bypass_wa(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_msm_offset *msm_host_offset =
					msm_host->offset;
	struct mmc_host *mmc = host->mmc;
	u32 config;
	int card_detect = 0;

	if (mmc->ops->get_cd)
		card_detect = mmc->ops->get_cd(mmc);

	config = readl_relaxed(host->ioaddr +
			msm_host_offset->core_vendor_spec);

	/*
	 * Following cases are covered.
	 * 1. Card Probe
	 * 2. Card suspend
	 * 3. Card Resume
	 * 4. Card remove
	 */
	if ((mmc->card == NULL) && card_detect &&
		(mmc->ios.power_mode == MMC_POWER_UP))
		config &= ~CORE_IO_PAD_PWR_SWITCH;
	else if (mmc->card && card_detect &&
			(mmc->ios.power_mode == MMC_POWER_OFF))
		config |= CORE_IO_PAD_PWR_SWITCH;
	else if (mmc->card && card_detect &&
			(mmc->ios.power_mode == MMC_POWER_UP))
		config &= ~CORE_IO_PAD_PWR_SWITCH;
	else if (mmc->card == NULL && !card_detect &&
			(mmc->ios.power_mode == MMC_POWER_OFF))
		config |= CORE_IO_PAD_PWR_SWITCH;

	writel_relaxed(config, host->ioaddr +
				msm_host_offset->core_vendor_spec);
}

static void sdhci_msm_handle_pwr_irq(struct sdhci_host *host, int irq)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	struct mmc_host *mmc = host->mmc;
	u32 irq_status, irq_ack = 0;
	int retry = 10, ret = 0;
	u32 pwr_state = 0, io_level = 0;
	u32 config;
	const struct sdhci_msm_offset *msm_offset = msm_host->offset;

	irq_status = msm_host_readl(msm_host, host,
			msm_offset->core_pwrctl_status);
	irq_status &= INT_MASK;

	msm_host_writel(msm_host, irq_status, host,
			msm_offset->core_pwrctl_clear);

	/*
	 * There is a rare HW scenario where the first clear pulse could be
	 * lost when actual reset and clear/read of status register is
	 * happening at a time. Hence, retry for at least 10 times to make
	 * sure status register is cleared. Otherwise, this will result in
	 * a spurious power IRQ resulting in system instability.
	 */
	while (irq_status & msm_host_readl(msm_host, host,
				msm_offset->core_pwrctl_status)) {
		if (retry == 0) {
			pr_err("%s: Timedout clearing (0x%x) pwrctl status register\n",
					mmc_hostname(host->mmc), irq_status);
			sdhci_msm_dump_pwr_ctrl_regs(host);
			WARN_ON(1);
			break;
		}
		msm_host_writel(msm_host, irq_status, host,
			msm_offset->core_pwrctl_clear);
		retry--;
		udelay(10);
	}

	if (mmc->card && mmc->ops->get_cd && !mmc->ops->get_cd(mmc) &&
		irq_status & CORE_PWRCTL_BUS_ON) {
		irq_ack = CORE_PWRCTL_BUS_FAIL;
		msm_host_writel(msm_host, irq_ack, host,
				msm_offset->core_pwrctl_ctl);
		return;
	}
	/* Handle BUS ON/OFF*/
	if (irq_status & CORE_PWRCTL_BUS_ON) {
		ret = sdhci_msm_setup_vreg(msm_host, true, false);
		if (!ret)
			ret = sdhci_msm_set_vdd_io_vol(msm_host,
					VDD_IO_HIGH, 0);
		if (ret)
			irq_ack |= CORE_PWRCTL_BUS_FAIL;
		else
			irq_ack |= CORE_PWRCTL_BUS_SUCCESS;

		pwr_state = REQ_BUS_ON;
		io_level = REQ_IO_HIGH;
	}
	if (irq_status & CORE_PWRCTL_BUS_OFF) {
		if (!(host->mmc->caps & MMC_CAP_NONREMOVABLE) ||
		    msm_host->pltfm_init_done)
			ret = sdhci_msm_setup_vreg(msm_host,
					false, false);
		if (!ret)
			ret = sdhci_msm_set_vdd_io_vol(msm_host,
					VDD_IO_LOW, 0);
		if (ret)
			irq_ack |= CORE_PWRCTL_BUS_FAIL;
		else
			irq_ack |= CORE_PWRCTL_BUS_SUCCESS;

		pwr_state = REQ_BUS_OFF;
		io_level = REQ_IO_LOW;
	}

	if (pwr_state) {
		ret = sdhci_msm_set_vmmc(mmc);
		if (!ret)
			ret = sdhci_msm_set_vqmmc(msm_host, mmc,
					pwr_state & REQ_BUS_ON);
		if (!ret)
			ret = sdhci_msm_set_pincfg(msm_host,
					pwr_state & REQ_BUS_ON);
		if (!ret)
			irq_ack |= CORE_PWRCTL_BUS_SUCCESS;
		else
			irq_ack |= CORE_PWRCTL_BUS_FAIL;
	}

	/* Handle IO LOW/HIGH */
	if (irq_status & CORE_PWRCTL_IO_LOW) {
		io_level = REQ_IO_LOW;
		/* Switch voltage low */
		ret = sdhci_msm_set_vdd_io_vol(msm_host, VDD_IO_LOW, 0);
		if (ret)
			irq_ack |= CORE_PWRCTL_IO_FAIL;
		else
			irq_ack |= CORE_PWRCTL_IO_SUCCESS;
	}

	if (irq_status & CORE_PWRCTL_IO_HIGH) {
		io_level = REQ_IO_HIGH;
		/* Switch voltage high */
		ret = sdhci_msm_set_vdd_io_vol(msm_host, VDD_IO_HIGH, 0);
		if (ret)
			irq_ack |= CORE_PWRCTL_IO_FAIL;
		else
			irq_ack |= CORE_PWRCTL_IO_SUCCESS;
	}

	if (io_level && !IS_ERR(mmc->supply.vqmmc) && !pwr_state) {
		ret = mmc_regulator_set_vqmmc(mmc, &mmc->ios);
		if (ret < 0) {
			dev_err(mmc_dev(mmc), "%s: IO_level setting failed(%d). signal_voltage: %d, vdd: %d irq_status: 0x%08x\n",
					mmc_hostname(mmc), ret,
					mmc->ios.signal_voltage, mmc->ios.vdd,
					irq_status);
			irq_ack |= CORE_PWRCTL_IO_FAIL;
		}
	}

	/*
	 * The driver has to acknowledge the interrupt, switch voltages and
	 * report back if it succeded or not to this register. The voltage
	 * switches are handled by the sdhci core, so just report success.
	 */
	msm_host_writel(msm_host, irq_ack, host,
			msm_offset->core_pwrctl_ctl);

	/*
	 * If we don't have info regarding the voltage levels supported by
	 * regulators, don't change the IO PAD PWR SWITCH.
	 */
	if (msm_host->caps_0 & CORE_VOLT_SUPPORT) {
		u32 new_config;
		/*
		 * We should unset IO PAD PWR switch only if the register write
		 * can set IO lines high and the regulator also switches to 3 V.
		 * Else, we should keep the IO PAD PWR switch set.
		 * This is applicable to certain targets where eMMC vccq supply
		 * is only 1.8V. In such targets, even during REQ_IO_HIGH, the
		 * IO PAD PWR switch must be kept set to reflect actual
		 * regulator voltage. This way, during initialization of
		 * controllers with only 1.8V, we will set the IO PAD bit
		 * without waiting for a REQ_IO_LOW.
		 */
		config = readl_relaxed(host->ioaddr +
				msm_offset->core_vendor_spec);
		new_config = config;

		if ((io_level & REQ_IO_HIGH) &&
				(msm_host->caps_0 & CORE_3_0V_SUPPORT) &&
				!msm_host->fake_core_3_0v_support) {
			if (msm_host->vbias_skip_wa)
				sdhci_msm_vbias_bypass_wa(host);
			else
				new_config &= ~CORE_IO_PAD_PWR_SWITCH;
		} else if ((io_level & REQ_IO_LOW) ||
				(msm_host->caps_0 & CORE_1_8V_SUPPORT)) {
			new_config |= CORE_IO_PAD_PWR_SWITCH;
		}

		if (config ^ new_config)
			writel_relaxed(new_config, host->ioaddr +
					msm_offset->core_vendor_spec);
	}

	if (pwr_state)
		msm_host->curr_pwr_state = pwr_state;
	if (io_level)
		msm_host->curr_io_level = io_level;

	dev_dbg(mmc_dev(mmc), "%s: %s: Handled IRQ(%d), irq_status=0x%x, ack=0x%x\n",
		mmc_hostname(msm_host->mmc), __func__, irq, irq_status,
		irq_ack);
}

static irqreturn_t sdhci_msm_pwr_irq(int irq, void *data)
{
	struct sdhci_host *host = (struct sdhci_host *)data;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);

	sdhci_msm_handle_pwr_irq(host, irq);
	msm_host->pwr_irq_flag = 1;
	sdhci_msm_complete_pwr_irq_wait(msm_host);

	return IRQ_HANDLED;
}

static unsigned int sdhci_msm_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	struct clk *core_clk = msm_host->bulk_clks[0].clk;

	return clk_round_rate(core_clk, ULONG_MAX);
}

static unsigned int sdhci_msm_get_min_clock(struct sdhci_host *host)
{
	return SDHCI_MSM_MIN_CLOCK;
}

/*
 * __sdhci_msm_set_clock - sdhci_msm clock control.
 *
 * Description:
 * MSM controller does not use internal divider and
 * instead directly control the GCC clock as per
 * HW recommendation.
 **/
static void __sdhci_msm_set_clock(struct sdhci_host *host, unsigned int clock)
{
	u16 clk;
	/*
	 * Keep actual_clock as zero -
	 * - since there is no divider used so no need of having actual_clock.
	 * - MSM controller uses SDCLK for data timeout calculation. If
	 *   actual_clock is zero, host->clock is taken for calculation.
	 */
	host->mmc->actual_clock = 0;

	sdhci_writew(host, 0, SDHCI_CLOCK_CONTROL);

	if (clock == 0)
		return;

	/*
	 * MSM controller do not use clock divider.
	 * Thus read SDHCI_CLOCK_CONTROL and only enable
	 * clock with no divider value programmed.
	 */
	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	sdhci_enable_clk(host, clk);
}

/* sdhci_msm_set_clock - Called with (host->lock) spinlock held. */
static void sdhci_msm_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);

	if (!clock) {
		msm_host->clk_rate = clock;
		goto out;
	}

	sdhci_msm_hc_select_mode(host);

	msm_set_clock_rate_for_bus_mode(host, clock);
out:
	/* Vote on bus only with clock frequency or when changing clock
	 * frequency. No need to vote when setting clock frequency as 0
	 * because after setting clock at 0, we release host, which will
	 * eventually call host runtime suspend and unvoting would be
	 * taken care in runtime suspend call.
	 */
	if (!msm_host->skip_bus_bw_voting && clock)
		sdhci_msm_bus_voting(host, true);
	__sdhci_msm_set_clock(host, clock);
}

/*****************************************************************************\
 *                                                                           *
 * Inline Crypto Engine (ICE) support                                        *
 *                                                                           *
\*****************************************************************************/

#ifdef CONFIG_MMC_CRYPTO

#define AES_256_XTS_KEY_SIZE			64

/* QCOM ICE registers */

#define QCOM_ICE_REG_VERSION			0x0008

#define QCOM_ICE_REG_FUSE_SETTING		0x0010
#define QCOM_ICE_FUSE_SETTING_MASK		0x1
#define QCOM_ICE_FORCE_HW_KEY0_SETTING_MASK	0x2
#define QCOM_ICE_FORCE_HW_KEY1_SETTING_MASK	0x4

#define QCOM_ICE_REG_BIST_STATUS		0x0070
#define QCOM_ICE_BIST_STATUS_MASK		0xF0000000

#define QCOM_ICE_REG_ADVANCED_CONTROL		0x1000

#define sdhci_msm_ice_writel(host, val, reg)	\
	writel((val), (host)->ice_mem + (reg))
#define sdhci_msm_ice_readl(host, reg)	\
	readl((host)->ice_mem + (reg))

static bool sdhci_msm_ice_supported(struct sdhci_msm_host *msm_host)
{
	struct device *dev = mmc_dev(msm_host->mmc);
	u32 regval = sdhci_msm_ice_readl(msm_host, QCOM_ICE_REG_VERSION);
	int major = regval >> 24;
	int minor = (regval >> 16) & 0xFF;
	int step = regval & 0xFFFF;

	/* For now this driver only supports ICE version 3. */
	if (major != 3) {
		dev_warn(dev, "Unsupported ICE version: v%d.%d.%d\n",
			 major, minor, step);
		return false;
	}

	dev_info(dev, "Found QC Inline Crypto Engine (ICE) v%d.%d.%d\n",
		 major, minor, step);

	/* If fuses are blown, ICE might not work in the standard way. */
	regval = sdhci_msm_ice_readl(msm_host, QCOM_ICE_REG_FUSE_SETTING);
	if (regval & (QCOM_ICE_FUSE_SETTING_MASK |
		      QCOM_ICE_FORCE_HW_KEY0_SETTING_MASK |
		      QCOM_ICE_FORCE_HW_KEY1_SETTING_MASK)) {
		dev_warn(dev, "Fuses are blown; ICE is unusable!\n");
		return false;
	}
	return true;
}

static inline struct clk *sdhci_msm_ice_get_clk(struct device *dev)
{
	return devm_clk_get(dev, "ice");
}

static int sdhci_msm_ice_init(struct sdhci_msm_host *msm_host,
			      struct cqhci_host *cq_host)
{
	struct mmc_host *mmc = msm_host->mmc;
	struct device *dev = mmc_dev(mmc);
	struct resource *ice_base_res;
#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER)
	struct resource *ice_hwkm_res;
#endif
	int err;

	if (!(cqhci_readl(cq_host, CQHCI_CAP) & CQHCI_CAP_CS))
		return 0;

	ice_base_res = platform_get_resource_byname(msm_host->pdev, IORESOURCE_MEM,
					   "cqhci_ice");
	if (!ice_base_res) {
		dev_warn(dev, "ICE registers not found\n");
		goto disable;
	}

	if (!qcom_scm_ice_available()) {
		dev_warn(dev, "ICE SCM interface not found\n");
		goto disable;
	}

	msm_host->ice_mem = devm_ioremap_resource(dev, ice_base_res);
	if (IS_ERR(msm_host->ice_mem)) {
		err = PTR_ERR(msm_host->ice_mem);
		dev_err(dev, "Failed to map ICE registers; err=%d\n", err);
		return err;
	}

#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER)
	ice_hwkm_res = platform_get_resource_byname(msm_host->pdev,
						    IORESOURCE_MEM,
						    "cqhci_ice_hwkm");
	if (!ice_hwkm_res) {
		dev_warn(dev, "ICE HWKM registers not found\n");
		goto disable;
	}
	msm_host->ice_hwkm_mem = devm_ioremap_resource(dev, ice_hwkm_res);
	if (IS_ERR(msm_host->ice_hwkm_mem)) {
		err = PTR_ERR(msm_host->ice_hwkm_mem);
		dev_err(dev, "Failed to map ICE HWKM registers; err=%d\n", err);
		return err;
	}
#endif

	if (!sdhci_msm_ice_supported(msm_host))
		goto disable;

	mmc->caps2 |= MMC_CAP2_CRYPTO;
	return 0;

disable:
	dev_warn(dev, "Disabling inline encryption support\n");
	return 0;
}

static void sdhci_msm_ice_low_power_mode_enable(struct sdhci_msm_host *msm_host)
{
	u32 regval;

	regval = sdhci_msm_ice_readl(msm_host, QCOM_ICE_REG_ADVANCED_CONTROL);
	/*
	 * Enable low power mode sequence
	 * [0]-0, [1]-0, [2]-0, [3]-E, [4]-0, [5]-0, [6]-0, [7]-0
	 */
	regval |= 0x7000;
	sdhci_msm_ice_writel(msm_host, regval, QCOM_ICE_REG_ADVANCED_CONTROL);
}

static void sdhci_msm_ice_optimization_enable(struct sdhci_msm_host *msm_host)
{
	u32 regval;

	/* ICE Optimizations Enable Sequence */
	regval = sdhci_msm_ice_readl(msm_host, QCOM_ICE_REG_ADVANCED_CONTROL);
	regval |= 0xD807100;
	/* ICE HPG requires delay before writing */
	udelay(5);
	sdhci_msm_ice_writel(msm_host, regval, QCOM_ICE_REG_ADVANCED_CONTROL);
	udelay(5);
}

/*
 * Wait until the ICE BIST (built-in self-test) has completed.
 *
 * This may be necessary before ICE can be used.
 *
 * Note that we don't really care whether the BIST passed or failed; we really
 * just want to make sure that it isn't still running.  This is because (a) the
 * BIST is a FIPS compliance thing that never fails in practice, (b) ICE is
 * documented to reject crypto requests if the BIST fails, so we needn't do it
 * in software too, and (c) properly testing storage encryption requires testing
 * the full storage stack anyway, and not relying on hardware-level self-tests.
 */
static int sdhci_msm_ice_wait_bist_status(struct sdhci_msm_host *msm_host)
{
	u32 regval;
	int err;

	err = readl_poll_timeout(msm_host->ice_mem + QCOM_ICE_REG_BIST_STATUS,
				 regval, !(regval & QCOM_ICE_BIST_STATUS_MASK),
				 50, 5000);
	if (err)
		dev_err(mmc_dev(msm_host->mmc),
			"Timed out waiting for ICE self-test to complete\n");
	return err;
}

static void sdhci_msm_ice_enable(struct sdhci_msm_host *msm_host)
{
	if (!(msm_host->mmc->caps2 & MMC_CAP2_CRYPTO))
		return;
	sdhci_msm_ice_low_power_mode_enable(msm_host);
	sdhci_msm_ice_optimization_enable(msm_host);
	sdhci_msm_ice_wait_bist_status(msm_host);
}

static int __maybe_unused sdhci_msm_ice_resume(struct sdhci_msm_host *msm_host)
{
	if (!(msm_host->mmc->caps2 & MMC_CAP2_CRYPTO))
		return 0;
	return sdhci_msm_ice_wait_bist_status(msm_host);
}

/*
 * Program a key into a QC ICE keyslot, or evict a keyslot.  QC ICE requires
 * vendor-specific SCM calls for this; it doesn't support the standard way.
 */
static int sdhci_msm_program_key(struct cqhci_host *cq_host,
				 const union cqhci_crypto_cfg_entry *cfg,
				 int slot)
{
	struct device *dev = mmc_dev(cq_host->mmc);
	union cqhci_crypto_cap_entry cap;
	union {
		u8 bytes[AES_256_XTS_KEY_SIZE];
		u32 words[AES_256_XTS_KEY_SIZE / sizeof(u32)];
	} key;
	int i;
	int err;

	if (!(cfg->config_enable & CQHCI_CRYPTO_CONFIGURATION_ENABLE))
		return qcom_scm_ice_invalidate_key(slot);

	/* Only AES-256-XTS has been tested so far. */
	cap = cq_host->crypto_cap_array[cfg->crypto_cap_idx];
	if (cap.algorithm_id != CQHCI_CRYPTO_ALG_AES_XTS ||
	    cap.key_size != CQHCI_CRYPTO_KEY_SIZE_256) {
		dev_err_ratelimited(dev,
				    "Unhandled crypto capability; algorithm_id=%d, key_size=%d\n",
				    cap.algorithm_id, cap.key_size);
		return -EINVAL;
	}

	memcpy(key.bytes, cfg->crypto_key, AES_256_XTS_KEY_SIZE);

	/*
	 * The SCM call byte-swaps the 32-bit words of the key.  So we have to
	 * do the same, in order for the final key be correct.
	 */
	for (i = 0; i < ARRAY_SIZE(key.words); i++)
		__cpu_to_be32s(&key.words[i]);

	err = qcom_scm_ice_set_key(slot, key.bytes, AES_256_XTS_KEY_SIZE,
				   QCOM_SCM_ICE_CIPHER_AES_256_XTS,
				   cfg->data_unit_size);
	memzero_explicit(&key, sizeof(key));
	return err;
}

void sdhci_msm_ice_disable(struct sdhci_msm_host *msm_host)
{
	if (!(msm_host->mmc->caps2 & MMC_CAP2_CRYPTO))
		return;
#if IS_ENABLED(CONFIG_MMC_CRYPTO_QTI)
	crypto_qti_disable();
#endif
}
#else /* CONFIG_MMC_CRYPTO */
static inline struct clk *sdhci_msm_ice_get_clk(struct device *dev)
{
	return NULL;
}

static inline int sdhci_msm_ice_init(struct sdhci_msm_host *msm_host,
				     struct cqhci_host *cq_host)
{
	return 0;
}

static inline void sdhci_msm_ice_enable(struct sdhci_msm_host *msm_host)
{
}

static inline int __maybe_unused
sdhci_msm_ice_resume(struct sdhci_msm_host *msm_host)
{
	return 0;
}

void sdhci_msm_ice_disable(struct sdhci_msm_host *msm_host)
{
}
#endif /* !CONFIG_MMC_CRYPTO */

/*****************************************************************************\
 *                                                                           *
 * MSM Command Queue Engine (CQE)                                            *
 *                                                                           *
\*****************************************************************************/

static u32 sdhci_msm_cqe_irq(struct sdhci_host *host, u32 intmask)
{
	int cmd_error = 0;
	int data_error = 0;

	if (!sdhci_cqe_irq(host, intmask, &cmd_error, &data_error))
		return intmask;

	cqhci_irq(host->mmc, intmask, cmd_error, data_error);
	return 0;
}

static void sdhci_msm_cqe_enable(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);

	sdhci_cqe_enable(mmc);
	sdhci_msm_ice_enable(msm_host);
}

static void sdhci_msm_cqe_disable(struct mmc_host *mmc, bool recovery)
{
	struct sdhci_host *host = mmc_priv(mmc);
	unsigned long flags;
	u32 ctrl;

	/*
	 * When CQE is halted, the legacy SDHCI path operates only
	 * on 16-byte descriptors in 64bit mode.
	 */
	if (host->flags & SDHCI_USE_64_BIT_DMA)
		host->desc_sz = 16;

	spin_lock_irqsave(&host->lock, flags);

	/*
	 * During CQE command transfers, command complete bit gets latched.
	 * So s/w should clear command complete interrupt status when CQE is
	 * either halted or disabled. Otherwise unexpected SDCHI legacy
	 * interrupt gets triggered when CQE is halted/disabled.
	 */
	ctrl = sdhci_readl(host, SDHCI_INT_ENABLE);
	ctrl |= SDHCI_INT_RESPONSE;
	sdhci_writel(host,  ctrl, SDHCI_INT_ENABLE);
	sdhci_writel(host, SDHCI_INT_RESPONSE, SDHCI_INT_STATUS);

	spin_unlock_irqrestore(&host->lock, flags);

	sdhci_cqe_disable(mmc, recovery);
}

static void sdhci_msm_set_timeout(struct sdhci_host *host, struct mmc_command *cmd)
{
	u32 count, start = 15;

	/*
	 * Qcom SoC hardware data timeout value was calculated
	 * using 4 * MCLK * 2^(count + 13). where MCLK = 1 / host->clock.
	 */

	host->timeout_clk = host->mmc->actual_clock ?
				host->mmc->actual_clock / 1000 :
				host->clock / 1000;
	host->timeout_clk /= 4;

	__sdhci_set_timeout(host, cmd);
	count = sdhci_readb(host, SDHCI_TIMEOUT_CONTROL);

	/*
	 * Update software timeout value if its value is less than hardware data
	 * timeout value.
	 */
	if (cmd && cmd->data && host->clock > 400000 &&
	    host->clock <= 50000000 &&
	    ((1 << (count + start)) > (10 * host->clock)))
		host->data_timeout = 22LL * NSEC_PER_SEC;
}

static const struct cqhci_host_ops sdhci_msm_cqhci_ops = {
	.enable		= sdhci_msm_cqe_enable,
	.disable	= sdhci_msm_cqe_disable,
#ifdef CONFIG_MMC_CRYPTO
	.program_key	= sdhci_msm_program_key,
#endif
};

static int sdhci_msm_cqe_add_host(struct sdhci_host *host,
				struct platform_device *pdev)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	struct cqhci_host *cq_host;
	bool dma64;
	u32 cqcfg;
	int ret;

	/*
	 * When CQE is halted, SDHC operates only on 16byte ADMA descriptors.
	 * So ensure ADMA table is allocated for 16byte descriptors.
	 */
	if (host->caps & SDHCI_CAN_64BIT)
		host->alloc_desc_sz = 16;

	ret = sdhci_setup_host(host);
	if (ret)
		return ret;

	cq_host = cqhci_pltfm_init(pdev);
	if (IS_ERR(cq_host)) {
		ret = PTR_ERR(cq_host);
		dev_err(&pdev->dev, "cqhci-pltfm init: failed: %d\n", ret);
		goto cleanup;
	}

	msm_host->mmc->caps2 |= MMC_CAP2_CQE | MMC_CAP2_CQE_DCMD;
	cq_host->ops = &sdhci_msm_cqhci_ops;
	msm_host->cq_host = cq_host;
	/* TODO: Needs Upstreaming */
	/* cq_host->offset_changed = msm_host->cqhci_offset_changed; */

	dma64 = host->flags & SDHCI_USE_64_BIT_DMA;

	ret = sdhci_msm_ice_init(msm_host, cq_host);
	if (ret)
		goto cleanup;

	ret = cqhci_init(cq_host, host->mmc, dma64);
	if (ret) {
		dev_err(&pdev->dev, "%s: CQE init: failed (%d)\n",
				mmc_hostname(host->mmc), ret);
		goto cleanup;
	}

	/* Disable cqe reset due to cqe enable signal */
	cqcfg = cqhci_readl(cq_host, CQHCI_VENDOR_CFG1);
	cqcfg |= CQHCI_VENDOR_DIS_RST_ON_CQ_EN;
	cqhci_writel(cq_host, cqcfg, CQHCI_VENDOR_CFG1);

	/*
	 * SDHC expects 12byte ADMA descriptors till CQE is enabled.
	 * So limit desc_sz to 12 so that the data commands that are sent
	 * during card initialization (before CQE gets enabled) would
	 * get executed without any issues.
	 */
	if (host->flags & SDHCI_USE_64_BIT_DMA)
		host->desc_sz = 12;

	ret = __sdhci_add_host(host);
	if (ret)
		goto cleanup;

	dev_info(&pdev->dev, "%s: CQE init: success\n",
			mmc_hostname(host->mmc));
	return ret;

cleanup:
	sdhci_cleanup_host(host);
	return ret;
}

static void sdhci_msm_registers_save(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_msm_offset *msm_offset = msm_host->offset;
	struct cqhci_host *cq_host = host->mmc->cqe_private;

	if (!msm_host->regs_restore.is_supported &&
			!msm_host->reg_store)
		return;

	msm_host->regs_restore.vendor_func = readl_relaxed(host->ioaddr +
		msm_offset->core_vendor_spec);
	msm_host->regs_restore.vendor_pwrctl_mask =
		readl_relaxed(host->ioaddr +
		msm_offset->core_pwrctl_mask);
	msm_host->regs_restore.vendor_func2 =
		readl_relaxed(host->ioaddr +
		msm_offset->core_vendor_spec_func2);
	msm_host->regs_restore.vendor_func3 =
		readl_relaxed(host->ioaddr +
		msm_offset->core_vendor_spec3);
	msm_host->regs_restore.hc_2c_2e =
		sdhci_readl(host, SDHCI_CLOCK_CONTROL);
	msm_host->regs_restore.hc_3c_3e =
		sdhci_readl(host, SDHCI_AUTO_CMD_STATUS);
	msm_host->regs_restore.vendor_pwrctl_ctl =
		readl_relaxed(host->ioaddr +
		msm_offset->core_pwrctl_ctl);
	msm_host->regs_restore.hc_38_3a =
		sdhci_readl(host, SDHCI_SIGNAL_ENABLE);
	msm_host->regs_restore.hc_34_36 =
		sdhci_readl(host, SDHCI_INT_ENABLE);
	msm_host->regs_restore.hc_28_2a =
		sdhci_readl(host, SDHCI_HOST_CONTROL);
	msm_host->regs_restore.vendor_caps_0 =
		readl_relaxed(host->ioaddr +
		msm_offset->core_vendor_spec_capabilities0);
	msm_host->regs_restore.hc_caps_1 =
		sdhci_readl(host, SDHCI_CAPABILITIES_1);
	msm_host->regs_restore.testbus_config = readl_relaxed(host->ioaddr +
		msm_offset->core_testbus_config);
	msm_host->regs_restore.dll_config = readl_relaxed(host->ioaddr +
		msm_offset->core_dll_config);
	msm_host->regs_restore.dll_config2 = readl_relaxed(host->ioaddr +
		msm_offset->core_dll_config_2);
	msm_host->regs_restore.dll_config = readl_relaxed(host->ioaddr +
		msm_offset->core_dll_config);
	msm_host->regs_restore.dll_config3 = readl_relaxed(host->ioaddr +
		msm_offset->core_dll_config_3);
	msm_host->regs_restore.dll_usr_ctl = readl_relaxed(host->ioaddr +
		msm_offset->core_dll_usr_ctl);
	if (cq_host)
		msm_host->cqe_regs.cqe_vendor_cfg1 =
			cqhci_readl(cq_host, CQHCI_VENDOR_CFG1);

	msm_host->regs_restore.is_valid = true;

	pr_debug("%s: %s: registers saved. PWRCTL_MASK = 0x%x\n",
		mmc_hostname(host->mmc), __func__,
		readl_relaxed(host->ioaddr +
			msm_offset->core_pwrctl_mask));
}

static void sdhci_msm_registers_restore(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_msm_offset *msm_offset = msm_host->offset;
	u32 irq_status;
	struct mmc_ios ios = host->mmc->ios;
	struct cqhci_host *cq_host = host->mmc->cqe_private;

	if ((!msm_host->regs_restore.is_supported ||
		!msm_host->regs_restore.is_valid) &&
		!msm_host->reg_store)
		return;

	writel_relaxed(0, host->ioaddr + msm_offset->core_pwrctl_mask);
	writel_relaxed(msm_host->regs_restore.vendor_func, host->ioaddr +
			msm_offset->core_vendor_spec);
	writel_relaxed(msm_host->regs_restore.vendor_func2,
			host->ioaddr +
			msm_offset->core_vendor_spec_func2);
	writel_relaxed(msm_host->regs_restore.vendor_func3,
			host->ioaddr +
			msm_offset->core_vendor_spec3);
	sdhci_writel(host, msm_host->regs_restore.hc_2c_2e,
			SDHCI_CLOCK_CONTROL);
	sdhci_writel(host, msm_host->regs_restore.hc_3c_3e,
			SDHCI_AUTO_CMD_STATUS);
	sdhci_writel(host, msm_host->regs_restore.hc_38_3a,
			SDHCI_SIGNAL_ENABLE);
	sdhci_writel(host, msm_host->regs_restore.hc_34_36,
			SDHCI_INT_ENABLE);
	sdhci_writel(host, msm_host->regs_restore.hc_28_2a,
			SDHCI_HOST_CONTROL);
	writel_relaxed(msm_host->regs_restore.vendor_caps_0,
			host->ioaddr +
			msm_offset->core_vendor_spec_capabilities0);
	sdhci_writel(host, msm_host->regs_restore.hc_caps_1,
			SDHCI_CAPABILITIES_1);
	writel_relaxed(msm_host->regs_restore.testbus_config, host->ioaddr +
			msm_offset->core_testbus_config);
	msm_host->regs_restore.is_valid = false;

	/*
	 * Clear the PWRCTL_STATUS register.
	 * There is a rare HW scenario where the first clear pulse could be
	 * lost when actual reset and clear/read of status register is
	 * happening at a time. Hence, retry for at least 10 times to make
	 * sure status register is cleared. Otherwise, this will result in
	 * a spurious power IRQ resulting in system instability.
	 */
	irq_status = msm_host_readl(msm_host, host,
			msm_offset->core_pwrctl_status);

	irq_status &= INT_MASK;
	sdhci_msm_clear_pwrctl_status(host, irq_status);

	writel_relaxed(msm_host->regs_restore.vendor_pwrctl_ctl,
			host->ioaddr + msm_offset->core_pwrctl_ctl);
	writel_relaxed(msm_host->regs_restore.vendor_pwrctl_mask,
			host->ioaddr + msm_offset->core_pwrctl_mask);

	if (cq_host)
		cqhci_writel(cq_host, msm_host->cqe_regs.cqe_vendor_cfg1,
				CQHCI_VENDOR_CFG1);

	if ((ios.timing == MMC_TIMING_UHS_SDR50 &&
		host->flags & SDHCI_SDR50_NEEDS_TUNING) ||
		(((ios.timing == MMC_TIMING_MMC_HS400) ||
			(ios.timing == MMC_TIMING_MMC_HS200) ||
			(ios.timing == MMC_TIMING_UHS_SDR104))
			&& (ios.clock > CORE_FREQ_100MHZ))) {
		writel_relaxed(msm_host->regs_restore.dll_config2,
			host->ioaddr + msm_offset->core_dll_config_2);
		writel_relaxed(msm_host->regs_restore.dll_config3,
			host->ioaddr + msm_offset->core_dll_config_3);
		writel_relaxed(msm_host->regs_restore.dll_usr_ctl,
			host->ioaddr + msm_offset->core_dll_usr_ctl);
		writel_relaxed(msm_host->regs_restore.dll_config &
			~(CORE_DLL_RST | CORE_DLL_PDN),
			host->ioaddr + msm_offset->core_dll_config);

		msm_init_cm_dll(host, DLL_INIT_FROM_CX_COLLAPSE_EXIT);
		msm_config_cm_dll_phase(host, msm_host->saved_tuning_phase);
	}

	pr_debug("%s: %s: registers restored. PWRCTL_MASK = 0x%x\n",
		mmc_hostname(host->mmc), __func__,
		readl_relaxed(host->ioaddr +
			msm_offset->core_pwrctl_mask));
}

/*
 * Platform specific register write functions. This is so that, if any
 * register write needs to be followed up by platform specific actions,
 * they can be added here. These functions can go to sleep when writes
 * to certain registers are done.
 * These functions are relying on sdhci_set_ios not using spinlock.
 */
static int __sdhci_msm_check_write(struct sdhci_host *host, u16 val, int reg)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	u32 req_type = 0;

	switch (reg) {
	case SDHCI_HOST_CONTROL2:
		req_type = (val & SDHCI_CTRL_VDD_180) ? REQ_IO_LOW :
			REQ_IO_HIGH;
		break;
	case SDHCI_SOFTWARE_RESET:
		if (host->pwr && (val & SDHCI_RESET_ALL))
			req_type = REQ_BUS_OFF;
		break;
	case SDHCI_POWER_CONTROL:
		req_type = !val ? REQ_BUS_OFF : REQ_BUS_ON;
		break;
	case SDHCI_TRANSFER_MODE:
		msm_host->transfer_mode = val;
		break;
	case SDHCI_COMMAND:
		/*
		 * If a command does not have CRC field in its response, as per
		 * design, host controller shall clear the CRC error flag for
		 * it, which takes 3 MCLK's, i.e. 7.5us in the case of 400KHz.
		 * The next command, which requires CRC check to its response,
		 * should be sent after the CRC error flag of previous command
		 * gets cleaned.
		 */
		if ((val & SDHCI_CMD_CRC) &&
		    (msm_host->last_cmd & SDHCI_CMD_FLAGS_MASK) &&
		    !(msm_host->last_cmd & SDHCI_CMD_CRC))
			udelay(8);
		msm_host->last_cmd = val;
		if (!msm_host->use_cdr)
			break;
		if ((msm_host->transfer_mode & SDHCI_TRNS_READ) &&
		    SDHCI_GET_CMD(val) != MMC_SEND_TUNING_BLOCK_HS200 &&
		    SDHCI_GET_CMD(val) != MMC_SEND_TUNING_BLOCK)
			sdhci_msm_set_cdr(host, true);
		else
			sdhci_msm_set_cdr(host, false);
		break;
	}

	if (req_type) {
		msm_host->pwr_irq_flag = 0;
		/*
		 * Since this register write may trigger a power irq, ensure
		 * all previous register writes are complete by this point.
		 */
		mb();
	}
	return req_type;
}

/* This function may sleep*/
static void sdhci_msm_writew(struct sdhci_host *host, u16 val, int reg)
{
	u32 req_type = 0;

	req_type = __sdhci_msm_check_write(host, val, reg);
	writew_relaxed(val, host->ioaddr + reg);

	if (req_type)
		sdhci_msm_check_power_status(host, req_type);
}

/* This function may sleep*/
static void sdhci_msm_writeb(struct sdhci_host *host, u8 val, int reg)
{
	u32 req_type = 0;

	req_type = __sdhci_msm_check_write(host, val, reg);

	writeb_relaxed(val, host->ioaddr + reg);

	if (req_type)
		sdhci_msm_check_power_status(host, req_type);
}

static void sdhci_msm_set_regulator_caps(struct sdhci_msm_host *msm_host)
{
	struct mmc_host *mmc = msm_host->mmc;
	struct regulator *supply = mmc->supply.vqmmc;
	u32 caps = 0, config;
	struct sdhci_host *host = mmc_priv(mmc);
	const struct sdhci_msm_offset *msm_offset = msm_host->offset;

	if (!IS_ERR(mmc->supply.vqmmc)) {
		if (regulator_is_supported_voltage(supply, 1700000, 1950000))
			caps |= CORE_1_8V_SUPPORT;
		if (regulator_is_supported_voltage(supply, 2700000, 3600000))
			caps |= CORE_3_0V_SUPPORT;

		if (!caps)
			pr_warn("%s: 1.8/3V not supported for vqmmc\n",
					mmc_hostname(mmc));
	}

	if (caps) {
		/*
		 * Set the PAD_PWR_SWITCH_EN bit so that the PAD_PWR_SWITCH
		 * bit can be used as required later on.
		 */
		u32 io_level = msm_host->curr_io_level;

		config = readl_relaxed(host->ioaddr +
				msm_offset->core_vendor_spec);
		config |= CORE_IO_PAD_PWR_SWITCH_EN;

		if ((io_level & REQ_IO_HIGH) && (caps &	CORE_3_0V_SUPPORT))
			config &= ~CORE_IO_PAD_PWR_SWITCH;
		else if ((io_level & REQ_IO_LOW) || (caps & CORE_1_8V_SUPPORT))
			config |= CORE_IO_PAD_PWR_SWITCH;

		writel_relaxed(config,
				host->ioaddr + msm_offset->core_vendor_spec);
	}
	msm_host->caps_0 |= caps;
	pr_debug("%s: supported caps: 0x%08x\n", mmc_hostname(mmc), caps);
}

static int sdhci_msm_dt_get_array(struct device *dev, const char *prop_name,
				 u32 **bw_vecs, int *len, u32 size)
{
	int ret = 0;
	struct device_node *np = dev->of_node;
	size_t sz;
	u32 *arr = NULL;

	if (!of_get_property(np, prop_name, len)) {
		ret = -EINVAL;
		goto out;
	}
	sz = *len = *len / sizeof(*arr);
	if (sz <= 0 || (size > 0 && (sz > size))) {
		dev_err(dev, "%s invalid size\n", prop_name);
		ret = -EINVAL;
		goto out;
	}

	arr = devm_kzalloc(dev, sz * sizeof(*arr), GFP_KERNEL);
	if (!arr) {
		ret = -ENOMEM;
		goto out;
	}

	ret = of_property_read_u32_array(np, prop_name, arr, sz);
	if (ret < 0) {
		dev_err(dev, "%s failed reading array %d\n", prop_name, ret);
		goto out;
	}
	*bw_vecs = arr;
out:
	if (ret)
		*len = 0;
	return ret;
}

/* Returns required bandwidth in Bytes per Sec */
static unsigned long sdhci_get_bw_required(struct sdhci_host *host,
					struct mmc_ios *ios)
{
	unsigned long bw;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);

	bw = msm_host->clk_rate;

	if (ios->bus_width == MMC_BUS_WIDTH_4)
		bw /= 2;
	else if (ios->bus_width == MMC_BUS_WIDTH_1)
		bw /= 8;

	return bw;
}

static int sdhci_msm_bus_get_vote_for_bw(struct sdhci_msm_host *host,
					   unsigned int bw)
{
	struct sdhci_msm_bus_vote_data *bvd = host->bus_vote_data;

	const unsigned int *table = bvd->bw_vecs;
	unsigned int size = bvd->bw_vecs_size;
	int i;

	for (i = 0; i < size; i++) {
		if (bw <= table[i])
			return i;
	}

	return i - 1;
}

/*
 * Caller of this function should ensure that msm bus client
 * handle is not null.
 */
static inline int sdhci_msm_bus_set_vote(struct sdhci_msm_host *msm_host,
					     int vote)
{
	struct sdhci_host *host =  platform_get_drvdata(msm_host->pdev);
	struct sdhci_msm_bus_vote_data *bvd = msm_host->bus_vote_data;
	struct msm_bus_path *usecase = bvd->usecase;
	struct msm_bus_vectors *vec = usecase[vote].vec;
	int ddr_rc, cpu_rc;

	if (vote == bvd->curr_vote)
		return 0;

	pr_debug("%s: vote:%d sdhc_ddr ab:%llu ib:%llu cpu_sdhc ab:%llu ib:%llu\n",
			mmc_hostname(host->mmc), vote, vec[0].ab,
			vec[0].ib, vec[1].ab, vec[1].ib);
	ddr_rc = icc_set_bw(bvd->sdhc_ddr, vec[0].ab, vec[0].ib);
	cpu_rc = icc_set_bw(bvd->cpu_sdhc, vec[1].ab, vec[1].ib);
	if (ddr_rc || cpu_rc) {
		pr_err("%s: icc_set() failed\n",
			mmc_hostname(host->mmc));
		goto out;
	}
	bvd->curr_vote = vote;
out:
	return cpu_rc;
}

/*
 * This function cancels any scheduled delayed work and sets the bus
 * vote based on bw (bandwidth) argument.
 */
static void sdhci_msm_bus_get_and_set_vote(struct sdhci_host *host,
						unsigned int bw)
{
	int vote;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);

	if (!msm_host->bus_vote_data ||
		!msm_host->bus_vote_data->sdhc_ddr ||
		!msm_host->bus_vote_data->cpu_sdhc)
		return;
	vote = sdhci_msm_bus_get_vote_for_bw(msm_host, bw);
	sdhci_msm_bus_set_vote(msm_host, vote);
}

static struct sdhci_msm_bus_vote_data *sdhci_msm_get_bus_vote_data(struct device
				       *dev, struct sdhci_msm_host *host)

{
	struct platform_device *pdev = to_platform_device(dev);
	struct device_node *of_node = dev->of_node;
	struct sdhci_msm_bus_vote_data *bvd = NULL;
	struct msm_bus_path *usecase = NULL;
	int ret = 0, i = 0, j, num_paths, len;
	const u32 *vec_arr = NULL;

	if (!pdev) {
		dev_err(dev, "Null platform device!\n");
		return NULL;
	}

	bvd = devm_kzalloc(dev, sizeof(*bvd), GFP_KERNEL);
	if (!bvd)
		return bvd;

	ret = sdhci_msm_dt_get_array(dev, "qcom,bus-bw-vectors-bps",
				&bvd->bw_vecs, &bvd->bw_vecs_size, 0);
	if (ret) {
		if (ret == -EINVAL) {
			dev_dbg(dev, "No dt property of bus bw. voting defined!\n");
			dev_dbg(dev, "Skipping Bus BW voting now!!\n");
			host->skip_bus_bw_voting = true;
		}
		goto out;
	}

	ret = of_property_read_string(of_node, "qcom,msm-bus,name",
					&bvd->name);
	if (ret) {
		dev_err(dev, "Bus name missing err:(%d)\n", ret);
		goto out;
	}

	ret = of_property_read_u32(of_node, "qcom,msm-bus,num-cases",
		&bvd->num_usecase);
	if (ret) {
		dev_err(dev, "num-usecases not found err:(%d)\n", ret);
		goto out;
	}

	usecase = devm_kzalloc(dev, (sizeof(struct msm_bus_path) *
				   bvd->num_usecase), GFP_KERNEL);
	if (!usecase)
		goto out;

	ret = of_property_read_u32(of_node, "qcom,msm-bus,num-paths",
				   &num_paths);
	if (ret) {
		dev_err(dev, "num_paths not found err:(%d)\n", ret);
		goto out;
	}

	vec_arr = of_get_property(of_node, "qcom,msm-bus,vectors-KBps", &len);
	if (!vec_arr) {
		dev_err(dev, "Vector array not found\n");
		goto out;
	}

	for (i = 0; i < bvd->num_usecase; i++) {
		usecase[i].num_paths = num_paths;
		usecase[i].vec = devm_kcalloc(dev, num_paths,
					      sizeof(struct msm_bus_vectors),
					      GFP_KERNEL);
		if (!usecase[i].vec)
			goto out;
		for (j = 0; j < num_paths; j++) {
			int idx = ((i * num_paths) + j) * 2;

			usecase[i].vec[j].ab = (u64)
				be32_to_cpu(vec_arr[idx]);
			usecase[i].vec[j].ib = (u64)
				be32_to_cpu(vec_arr[idx + 1]);
		}
	}

	bvd->usecase = usecase;
	return bvd;
out:
	bvd = NULL;
	return bvd;
}

static int sdhci_msm_bus_register(struct sdhci_msm_host *host,
				struct platform_device *pdev)
{
	struct sdhci_msm_bus_vote_data *bsd;
	struct device *dev = &pdev->dev;
	int ret = 0;

	bsd = sdhci_msm_get_bus_vote_data(dev, host);
	if (!bsd) {
		dev_err(&pdev->dev, "Failed to get bus_scale data\n");
		return -EINVAL;
	}
	host->bus_vote_data = bsd;

	bsd->sdhc_ddr = of_icc_get(&pdev->dev, "sdhc-ddr");
	if (IS_ERR_OR_NULL(bsd->sdhc_ddr)) {
		dev_err(&pdev->dev, "(%ld): failed getting %s path\n",
			PTR_ERR(bsd->sdhc_ddr), "sdhc-ddr");
		ret = PTR_ERR(bsd->sdhc_ddr);
		bsd->sdhc_ddr = NULL;
		return ret;
	}

	bsd->cpu_sdhc = of_icc_get(&pdev->dev, "cpu-sdhc");
	if (IS_ERR_OR_NULL(bsd->cpu_sdhc)) {
		dev_err(&pdev->dev, "(%ld): failed getting %s path\n",
			PTR_ERR(bsd->cpu_sdhc), "cpu-sdhc");
		ret = PTR_ERR(bsd->cpu_sdhc);
		bsd->cpu_sdhc = NULL;
		return ret;
	}

	return ret;
}

static void sdhci_msm_bus_unregister(struct device *dev,
				struct sdhci_msm_host *host)
{
	struct sdhci_msm_bus_vote_data *bsd = host->bus_vote_data;

	icc_put(bsd->sdhc_ddr);
	icc_put(bsd->cpu_sdhc);
}

static void sdhci_msm_bus_voting(struct sdhci_host *host, bool enable)
{
	struct mmc_ios *ios = &host->mmc->ios;
	unsigned int bw;

	if (enable) {
		bw = sdhci_get_bw_required(host, ios);
		sdhci_msm_bus_get_and_set_vote(host, bw);
	} else
		sdhci_msm_bus_get_and_set_vote(host, 0);
}

static void sdhci_msm_reset(struct sdhci_host *host, u8 mask)
{
	if ((host->mmc->caps2 & MMC_CAP2_CQE) && (mask & SDHCI_RESET_ALL))
		cqhci_deactivate(host->mmc);
	sdhci_reset(host, mask);
}

static int sdhci_msm_register_vreg(struct sdhci_msm_host *msm_host)
{
	int ret;

	ret = mmc_regulator_get_supply(msm_host->mmc);
	if (ret)
		return ret;

	sdhci_msm_set_regulator_caps(msm_host);

	return 0;
}

static int sdhci_msm_start_signal_voltage_switch(struct mmc_host *mmc,
				      struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);
	u16 ctrl, status;

	/*
	 * Signal Voltage Switching is only applicable for Host Controllers
	 * v3.00 and above.
	 */
	if (host->version < SDHCI_SPEC_300)
		return 0;

	ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);

	switch (ios->signal_voltage) {
	case MMC_SIGNAL_VOLTAGE_330:
		if (!(host->flags & SDHCI_SIGNALING_330))
			return -EINVAL;

		/* Set 1.8V Signal Enable in the Host Control2 register to 0 */
		ctrl &= ~SDHCI_CTRL_VDD_180;
		break;
	case MMC_SIGNAL_VOLTAGE_180:
		if (!(host->flags & SDHCI_SIGNALING_180))
			return -EINVAL;

		/* Enable 1.8V Signal Enable in the Host Control2 register */
		ctrl |= SDHCI_CTRL_VDD_180;
		break;

	default:
		return -EINVAL;
	}

	sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);

	/* Wait for 5ms */
	usleep_range(5000, 5500);

	/* regulator output should be stable within 5 ms */
	status = ctrl & SDHCI_CTRL_VDD_180;
	ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	if ((ctrl & SDHCI_CTRL_VDD_180) == status)
		return 0;

	dev_warn(mmc_dev(mmc), "%s: Regulator output did not became stable\n",
		mmc_hostname(mmc));

	return -EAGAIN;
}

#define MAX_TEST_BUS 60
#define DRIVER_NAME "sdhci_msm"
#define SDHCI_MSM_DUMP(f, x...) \
	pr_err("%s: " DRIVER_NAME ": " f, mmc_hostname(host->mmc), ## x)
#define DRV_NAME "cqhci-host"

static void sdhci_msm_cqe_dump_debug_ram(struct sdhci_host *host)
{
	int i = 0;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_msm_offset *msm_offset = msm_host->offset;

	struct cqhci_host *cq_host;
	u32 version;
	u16 minor;
	int offset;

	if (msm_host->cq_host)
		cq_host = msm_host->cq_host;
	else
		return;

	version =  msm_host_readl(msm_host, host,
				 msm_offset->core_mci_version);
	minor = version & CORE_VERSION_TARGET_MASK;

	/* registers offset changed starting from 4.2.0 */
	offset = minor >= SDHCI_MSM_VER_420 ? 0 : 0x48;
	/*
	 * TODO: Needs upstreaming?
	 * if (cq_host->offset_changed)
	 *	offset += CQE_V5_VENDOR_CFG;
	 */
	pr_err("---- Debug RAM dump ----\n");
	pr_err(DRV_NAME ": Debug RAM wrap-around: 0x%08x | Debug RAM overlap: 0x%08x\n",
	       cqhci_readl(cq_host, CQ_CMD_DBG_RAM_WA + offset),
	       cqhci_readl(cq_host, CQ_CMD_DBG_RAM_OL + offset));

	while (i < 16) {
		pr_err(DRV_NAME ": Debug RAM dump [%d]: 0x%08x\n", i,
		       cqhci_readl(cq_host, CQ_CMD_DBG_RAM + offset + (4 * i)));
		i++;
	}
	pr_err("-------------------------\n");
}


static void sdhci_msm_dump_vendor_regs(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_msm_offset *msm_offset = msm_host->offset;

	int tbsel, tbsel2;
	int i, index = 0;
	u32 test_bus_val = 0;
	u32 debug_reg[MAX_TEST_BUS] = {0};

	SDHCI_MSM_DUMP("----------- VENDOR REGISTER DUMP -----------\n");

	if (msm_host->cq_host)
		sdhci_msm_cqe_dump_debug_ram(host);

	SDHCI_MSM_DUMP(
			"Data cnt: 0x%08x | Fifo cnt: 0x%08x | Int sts: 0x%08x\n",
		readl_relaxed(host->ioaddr + msm_offset->core_mci_data_cnt),
		readl_relaxed(host->ioaddr + msm_offset->core_mci_fifo_cnt),
		readl_relaxed(host->ioaddr + msm_offset->core_mci_status));
	SDHCI_MSM_DUMP(
			"DLL sts: 0x%08x | DLL cfg:  0x%08x | DLL cfg2: 0x%08x\n",
		readl_relaxed(host->ioaddr + msm_offset->core_dll_status),
		readl_relaxed(host->ioaddr + msm_offset->core_dll_config),
		readl_relaxed(host->ioaddr + msm_offset->core_dll_config_2));
	SDHCI_MSM_DUMP(
			"DLL cfg3: 0x%08x | DLL usr ctl:  0x%08x | DDR cfg: 0x%08x\n",
		readl_relaxed(host->ioaddr + msm_offset->core_dll_config_3),
		readl_relaxed(host->ioaddr + msm_offset->core_dll_usr_ctl),
		readl_relaxed(host->ioaddr + msm_offset->core_ddr_config));
	SDHCI_MSM_DUMP(
			"SDCC ver: 0x%08x | Vndr adma err : addr0: 0x%08x addr1: 0x%08x\n",
		readl_relaxed(host->ioaddr + msm_offset->core_mci_version),
		readl_relaxed(host->ioaddr +
				msm_offset->core_vendor_spec_adma_err_addr0),
		readl_relaxed(host->ioaddr +
				msm_offset->core_vendor_spec_adma_err_addr1));
	SDHCI_MSM_DUMP(
			"Vndr func: 0x%08x | Vndr func2 : 0x%08x Vndr func3: 0x%08x\n",
		readl_relaxed(host->ioaddr + msm_offset->core_vendor_spec),
		readl_relaxed(host->ioaddr +
			msm_offset->core_vendor_spec_func2),
		readl_relaxed(host->ioaddr + msm_offset->core_vendor_spec3));
	sdhci_msm_dump_pwr_ctrl_regs(host);

	/*
	 * tbsel indicates [2:0] bits and tbsel2 indicates [7:4] bits
	 * of core_testbus_config register.
	 *
	 * To select test bus 0 to 7 use tbsel and to select any test bus
	 * above 7 use (tbsel2 | tbsel) to get the test bus number. For eg,
	 * to select test bus 14, write 0x1E to core_testbus_config register
	 * i.e., tbsel2[7:4] = 0001, tbsel[2:0] = 110.
	 */
	for (tbsel2 = 0; tbsel2 < 7; tbsel2++) {
		for (tbsel = 0; tbsel < 8; tbsel++) {
			if (index >= MAX_TEST_BUS)
				break;
			test_bus_val =
			(tbsel2 << msm_offset->core_testbus_sel2_bit) |
				tbsel | msm_offset->core_testbus_ena;
			writel_relaxed(test_bus_val, host->ioaddr +
				msm_offset->core_testbus_config);
			debug_reg[index++] = readl_relaxed(host->ioaddr +
					msm_offset->core_sdcc_debug_reg);
		}
	}
	for (i = 0; i < MAX_TEST_BUS; i = i + 4)
		SDHCI_MSM_DUMP(
				" Test bus[%d to %d]: 0x%08x 0x%08x 0x%08x 0x%08x\n",
				i, i + 3, debug_reg[i], debug_reg[i+1],
				debug_reg[i+2], debug_reg[i+3]);
}

static const struct sdhci_msm_variant_ops mci_var_ops = {
	.msm_readl_relaxed = sdhci_msm_mci_variant_readl_relaxed,
	.msm_writel_relaxed = sdhci_msm_mci_variant_writel_relaxed,
};

static const struct sdhci_msm_variant_ops v5_var_ops = {
	.msm_readl_relaxed = sdhci_msm_v5_variant_readl_relaxed,
	.msm_writel_relaxed = sdhci_msm_v5_variant_writel_relaxed,
};

static const struct sdhci_msm_variant_info sdhci_msm_mci_var = {
	.var_ops = &mci_var_ops,
	.offset = &sdhci_msm_mci_offset,
};

static const struct sdhci_msm_variant_info sdhci_msm_v5_var = {
	.mci_removed = true,
	.var_ops = &v5_var_ops,
	.offset = &sdhci_msm_v5_offset,
};

static const struct sdhci_msm_variant_info sdm845_sdhci_var = {
	.mci_removed = true,
	.restore_dll_config = true,
	.var_ops = &v5_var_ops,
	.offset = &sdhci_msm_v5_offset,
};

static const struct of_device_id sdhci_msm_dt_match[] = {
	/*
	 * Do not add new variants to the driver which are compatible with
	 * generic ones, unless they need customization.
	 */
	{.compatible = "qcom,sdhci-msm-v4", .data = &sdhci_msm_mci_var},
	{.compatible = "qcom,sdhci-msm-v5", .data = &sdhci_msm_v5_var},
	{.compatible = "qcom,sdm670-sdhci", .data = &sdm845_sdhci_var},
	{.compatible = "qcom,sdm845-sdhci", .data = &sdm845_sdhci_var},
	{},
};

MODULE_DEVICE_TABLE(of, sdhci_msm_dt_match);

static int sdhci_msm_gcc_reset(struct device *dev, struct sdhci_host *host)
{

	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	struct reset_control *reset = msm_host->core_reset;
	int ret = -EOPNOTSUPP;

	if (!reset)
		return dev_err_probe(dev, ret, "unable to acquire core_reset\n");

	ret = reset_control_assert(reset);
	if (ret)
		return dev_err_probe(dev, ret, "core_reset assert failed\n");

	/*
	 * The hardware requirement for delay between assert/deassert
	 * is at least 3-4 sleep clock (32.7KHz) cycles, which comes to
	 * ~125us (4/32768). To be on the safe side add 200us delay.
	 */
	usleep_range(200, 210);

	ret = reset_control_deassert(reset);
	if (ret)
		return dev_err_probe(dev, ret, "core_reset deassert failed\n");

	usleep_range(200, 210);

	return ret;
}

static void sdhci_msm_hw_reset(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	struct platform_device *pdev = msm_host->pdev;
	int ret = -EOPNOTSUPP;

	if (!msm_host->core_reset) {
		dev_err(&pdev->dev, "%s: failed, err = %d\n", __func__,
				ret);
		return;
	}

	msm_host->reg_store = true;
	sdhci_msm_registers_save(host);
	if (host->mmc->caps2 & MMC_CAP2_CQE) {
		host->mmc->cqe_ops->cqe_disable(host->mmc);
		host->mmc->cqe_enabled = false;
	}


	sdhci_msm_gcc_reset(&pdev->dev, host);
	sdhci_msm_registers_restore(host);
	msm_host->reg_store = false;

#if defined(CONFIG_SDC_QTI)
	if (host->mmc->card)
		mmc_power_cycle(host->mmc, host->mmc->card->ocr);
#endif
}

static const struct sdhci_ops sdhci_msm_ops = {
	.reset = sdhci_msm_reset,
	.set_clock = sdhci_msm_set_clock,
	.get_min_clock = sdhci_msm_get_min_clock,
	.get_max_clock = sdhci_msm_get_max_clock,
	.set_bus_width = sdhci_set_bus_width,
	.set_uhs_signaling = sdhci_msm_set_uhs_signaling,
	.get_max_timeout_count = sdhci_msm_get_max_timeout_count,
	.write_w = sdhci_msm_writew,
	.write_b = sdhci_msm_writeb,
	.irq	= sdhci_msm_cqe_irq,
	.dump_vendor_regs = sdhci_msm_dump_vendor_regs,
	.set_power = sdhci_set_power_noreg,
	.hw_reset = sdhci_msm_hw_reset,
	.set_timeout = sdhci_msm_set_timeout,
};

static const struct sdhci_pltfm_data sdhci_msm_pdata = {
	.quirks = SDHCI_QUIRK_BROKEN_CARD_DETECTION |
		  SDHCI_QUIRK_SINGLE_POWER_WRITE |
		  SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		  SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12 |
		  SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC |
		  SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK,

	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
	.ops = &sdhci_msm_ops,
};

static void sdhci_set_default_hw_caps(struct sdhci_msm_host *msm_host,
		struct sdhci_host *host)
{
	u32 version, caps = 0;
	u16 minor;
	u8 major;
	const struct sdhci_msm_offset *msm_offset =
					sdhci_priv_msm_offset(host);

	version =  msm_host_readl(msm_host, host,
				 msm_offset->core_mci_version);

	major = (version & CORE_VERSION_MAJOR_MASK) >>
			CORE_VERSION_MAJOR_SHIFT;
	minor = version & CORE_VERSION_TARGET_MASK;

	caps = readl_relaxed(host->ioaddr + SDHCI_CAPABILITIES);

	/*
	 * SDCC 5 controller with major version 1, minor version 0x34 and later
	 * with HS 400 mode support will use CM DLL instead of CDC LP 533 DLL.
	 */
	if ((major == 1) && (minor < 0x34))
		msm_host->use_cdclp533 = true;


	if (major == 1 && minor >= 0x42)
		msm_host->use_14lpp_dll_reset = true;

	/* Fake 3.0V support for SDIO devices which requires such voltage */
	if (msm_host->fake_core_3_0v_support) {
		caps |= CORE_3_0V_SUPPORT;
			writel_relaxed((readl_relaxed(host->ioaddr +
			SDHCI_CAPABILITIES) | caps), host->ioaddr +
			msm_offset->core_vendor_spec_capabilities0);
	}

	writel_relaxed(caps, host->ioaddr +
		msm_offset->core_vendor_spec_capabilities0);

	/* keep track of the value in SDHCI_CAPABILITIES */
	msm_host->caps_0 = caps;

	/* 7FF projects with 7nm DLL */
	if ((major == 1) && ((minor == 0x6e) || (minor == 0x71) ||
				(minor == 0x72)))
		msm_host->use_7nm_dll = true;
}

static inline void sdhci_msm_get_of_property(struct platform_device *pdev,
		struct sdhci_host *host)
{
	struct device_node *node = pdev->dev.of_node;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);

	if (of_property_read_u32(node, "qcom,ddr-config",
				&msm_host->ddr_config))
		msm_host->ddr_config = DDR_CONFIG_POR_VAL;

	of_property_read_u32(node, "qcom,dll-config", &msm_host->dll_config);
}

static void sdhci_msm_clkgate_bus_delayed_work(struct work_struct *work)
{
	struct sdhci_msm_host *msm_host = container_of(work,
			struct sdhci_msm_host, clk_gating_work.work);
	struct sdhci_host *host = mmc_priv(msm_host->mmc);

	sdhci_msm_registers_save(host);
	dev_pm_opp_set_rate(&msm_host->pdev->dev, 0);
	clk_bulk_disable_unprepare(ARRAY_SIZE(msm_host->bulk_clks),
					msm_host->bulk_clks);
	sdhci_msm_bus_voting(host, false);
}

/* Find cpu group qos from a given cpu */
static struct qos_cpu_group *cpu_to_group(struct sdhci_msm_qos_req *r, int cpu)
{
	int i;
	struct qos_cpu_group *g = r->qcg;

	if (cpu < 0 || cpu > num_possible_cpus())
		return NULL;

	for (i = 0; i < r->num_groups; i++, g++) {
		if (cpumask_test_cpu(cpu, &g->mask))
			return &r->qcg[i];
	}

	return NULL;
}

/*
 * Function to put qos vote. This takes qos cpu group of
 * host and type of vote as input
 */
static int sdhci_msm_update_qos_constraints(struct qos_cpu_group *qcg,
							enum constraint type)
{
	unsigned int vote;
	int cpu, err;
	struct dev_pm_qos_request *qos_req = qcg->qos_req;

	if (type == QOS_MAX)
		vote = S32_MAX;
	else
		vote = qcg->votes[type];

	if (qcg->curr_vote == vote)
		return 0;

	for_each_cpu(cpu, &qcg->mask) {
		err = dev_pm_qos_update_request(qos_req, vote);
		if (err < 0)
			return err;
		++qos_req;
	}

	if (type == QOS_MAX)
		qcg->voted = false;
	else
		qcg->voted = true;

	qcg->curr_vote = vote;

	return 0;
}

/* Unregister pm qos requests */
static int remove_group_qos(struct qos_cpu_group *qcg)
{
	int err, cpu;
	struct dev_pm_qos_request *qos_req = qcg->qos_req;

	for_each_cpu(cpu, &qcg->mask) {
		if (!dev_pm_qos_request_active(qos_req)) {
			++qos_req;
			continue;
		}
		err = dev_pm_qos_remove_request(qos_req);
		if (err < 0)
			return err;
		qos_req++;
	}

	return 0;
}

/* Register pm qos request */
static int add_group_qos(struct qos_cpu_group *qcg, enum constraint type)
{
	int cpu, err;
	struct dev_pm_qos_request *qos_req = qcg->qos_req;

	for_each_cpu(cpu, &qcg->mask) {
		memset(qos_req, 0,
				sizeof(struct dev_pm_qos_request));
		err = dev_pm_qos_add_request(get_cpu_device(cpu),
				qos_req,
				DEV_PM_QOS_RESUME_LATENCY,
				type);
		if (err < 0)
			return err;
		qos_req++;
	}
	return 0;
}

/* Function to remove pm qos vote */
static void sdhci_msm_unvote_qos_all(struct sdhci_msm_host *msm_host)
{
	struct sdhci_msm_qos_req *qos_req = msm_host->sdhci_qos;
	struct qos_cpu_group *qcg;
	int i, err;

	if (!qos_req)
		return;
	qcg = qos_req->qcg;
	for (i = 0; ((i < qos_req->num_groups) && qcg->initialized); i++,
								qcg++) {
		err = sdhci_msm_update_qos_constraints(qcg, QOS_MAX);
		if (err)
			dev_err(&msm_host->pdev->dev,
				"Failed (%d) removing qos vote(%d)\n", err, i);
	}
}

/* Function to vote pmqos from sdcc. */
static void sdhci_msm_vote_pmqos(struct mmc_host *mmc, int cpu)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	struct qos_cpu_group *qcg;

	qcg = cpu_to_group(msm_host->sdhci_qos, cpu);
	if (!qcg) {
		dev_dbg(&msm_host->pdev->dev, "QoS group is undefined\n");
		return;
	}

	if (qcg->voted)
		return;

	if (sdhci_msm_update_qos_constraints(qcg, QOS_PERF))
		dev_err(&qcg->host->pdev->dev, "%s: update qos - failed\n",
				__func__);
	dev_dbg(&msm_host->pdev->dev, "Voted pmqos - cpu: %d\n", cpu);
}

static unsigned int sdhci_msm_get_sup_clk_rate(struct sdhci_host *host,
						u32 req_clk)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	struct clk *core_clk = msm_host->bulk_clks[0].clk;
	unsigned int sup_clk = -1;

	if (req_clk < sdhci_msm_get_min_clock(host)) {
		sup_clk = sdhci_msm_get_min_clock(host);
		return sup_clk;
	}

	sup_clk = clk_round_rate(core_clk, clk_get_rate(core_clk));

	if (host->clock != msm_host->clk_rate)
		sup_clk = sup_clk / 2;

	return sup_clk;
}

/**
 * sdhci_msm_irq_affinity_notify - Callback for affinity changes
 * @notify: context as to what irq was changed
 * @mask: the new affinity mask
 *
 * This is a callback function used by the irq_set_affinity_notifier function
 * so that we may register to receive changes to the irq affinity masks.
 */
static void
sdhci_msm_irq_affinity_notify(struct irq_affinity_notify *notify,
		const cpumask_t *mask)
{
	struct sdhci_msm_host *msm_host =
		container_of(notify, struct sdhci_msm_host, affinity_notify);
	struct platform_device *pdev = msm_host->pdev;
	struct sdhci_msm_qos_req *qos_req = msm_host->sdhci_qos;
	struct qos_cpu_group *qcg;
	int i, err;

	if (!qos_req)
		return;
	/*
	 * If device is in suspend mode, just update the active mask,
	 * vote would be taken care when device resumes.
	 */
	msm_host->sdhci_qos->active_mask = cpumask_first(mask);
	if (pm_runtime_status_suspended(&pdev->dev))
		return;

	/* Cancel previous scheduled work and unvote votes */
	qcg = qos_req->qcg;
	for (i = 0; i < qos_req->num_groups; i++, qcg++) {
		err = sdhci_msm_update_qos_constraints(qcg, QOS_MAX);
		if (err)
			pr_err("%s: Failed (%d) removing qos vote of grp(%d)\n",
					mmc_hostname(msm_host->mmc), err, i);
	}

	sdhci_msm_vote_pmqos(msm_host->mmc,
			msm_host->sdhci_qos->active_mask);
}

/**
 * sdhci_msm_irq_affinity_release - Callback for affinity notifier release
 * @ref: internal core kernel usage
 *
 * This is a callback function used by the irq_set_affinity_notifier function
 * to inform the current notification subscriber that they will no longer
 * receive notifications.
 */
static void
inline sdhci_msm_irq_affinity_release(struct kref __always_unused *ref)
{ }

/* Function for settig up qos based on parsed dt entries */
static int sdhci_msm_setup_qos(struct sdhci_msm_host *msm_host)
{
	struct platform_device *pdev = msm_host->pdev;
	struct sdhci_msm_qos_req *qr = msm_host->sdhci_qos;
	struct qos_cpu_group *qcg = qr->qcg;
	struct mmc_host *mmc = msm_host->mmc;
	struct sdhci_host *host = mmc_priv(mmc);
	int i, err;

	if (!msm_host->sdhci_qos)
		return 0;

	/* Affine irq to first set of mask */
	WARN_ON(irq_set_affinity_hint(host->irq, &qcg->mask));

	/* Setup notifier for case of affinity change/migration */
	msm_host->affinity_notify.notify = sdhci_msm_irq_affinity_notify;
	msm_host->affinity_notify.release = sdhci_msm_irq_affinity_release;
	irq_set_affinity_notifier(host->irq, &msm_host->affinity_notify);

	for (i = 0; i < qr->num_groups; i++, qcg++) {
		qcg->qos_req = kcalloc(cpumask_weight(&qcg->mask),
				sizeof(struct dev_pm_qos_request),
				GFP_KERNEL);
		if (!qcg->qos_req) {
			dev_err(&pdev->dev, "Memory allocation failed\n");
			if (!i)
				return -ENOMEM;
			goto free_mem;
		}
		err = add_group_qos(qcg, S32_MAX);
		if (err < 0) {
			dev_err(&pdev->dev, "Fail (%d) add qos-req: grp-%d\n",
					err, i);
			if (!i) {
				kfree(qcg->qos_req);
				return err;
			}
			goto free_mem;
		}
		qcg->initialized = true;
		dev_dbg(&pdev->dev, "%s: qcg: 0x%08x | mask: 0x%08x\n",
				 __func__, qcg, qcg->mask);
	}

	/* Vote pmqos during setup for first set of mask*/
	sdhci_msm_update_qos_constraints(qr->qcg, QOS_PERF);
	qr->active_mask = cpumask_first(&qr->qcg->mask);
	return 0;

free_mem:
	while (i--) {
		kfree(qcg->qos_req);
		qcg--;
	}

	return err;
}

/*
 * QoS init function. It parses dt entries and intializes data
 * structures.
 */
static void sdhci_msm_qos_init(struct sdhci_msm_host *msm_host)
{
	struct platform_device *pdev = msm_host->pdev;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *group_node;
	struct sdhci_msm_qos_req *qr;
	struct qos_cpu_group *qcg;
	int i, err, mask = 0;

	qr = kzalloc(sizeof(*qr), GFP_KERNEL);
	if (!qr)
		return;

	msm_host->sdhci_qos = qr;

	/* find numbers of qos child node present */
	qr->num_groups = of_get_available_child_count(np);
	dev_dbg(&pdev->dev, "num-groups: %d\n", qr->num_groups);
	if (!qr->num_groups) {
		dev_err(&pdev->dev, "QoS groups undefined\n");
		kfree(qr);
		msm_host->sdhci_qos = NULL;
		return;
	}
	qcg = kzalloc(sizeof(*qcg) * qr->num_groups, GFP_KERNEL);
	if (!qcg) {
		msm_host->sdhci_qos = NULL;
		kfree(qr);
		return;
	}

	/*
	 * Assign qos cpu group/cluster to host qos request and
	 * read child entries of qos node
	 */
	qr->qcg = qcg;
	for_each_available_child_of_node(np, group_node) {
		err = of_property_read_u32(group_node, "mask", &mask);
		if (err) {
			dev_dbg(&pdev->dev, "Error reading group mask: %d\n",
					err);
			continue;
		}
		qcg->mask.bits[0] = mask;
		if (!cpumask_subset(&qcg->mask, cpu_possible_mask)) {
			dev_err(&pdev->dev, "Invalid group mask\n");
			goto out_vote_err;
		}

		err = of_property_count_u32_elems(group_node, "vote");
		if (err <= 0) {
			dev_err(&pdev->dev, "1 vote is needed, bailing out\n");
			goto out_vote_err;
		}
		qcg->votes = kmalloc(sizeof(*qcg->votes) * err, GFP_KERNEL);
		if (!qcg->votes)
			goto out_vote_err;
		for (i = 0; i < err; i++) {
			if (of_property_read_u32_index(group_node, "vote", i,
						&qcg->votes[i]))
				goto out_vote_err;
		}
		qcg->host = msm_host;
		++qcg;
	}
	err = sdhci_msm_setup_qos(msm_host);
	if (!err)
		return;
	dev_err(&pdev->dev, "Failed to setup PM QoS.\n");

out_vote_err:
	for (i = 0, qcg = qr->qcg; i < qr->num_groups; i++, qcg++)
		kfree(qcg->votes);
	kfree(qr->qcg);
	kfree(qr);
	msm_host->sdhci_qos = NULL;
}

static ssize_t show_sdhci_msm_clk_gating(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);

	return scnprintf(buf, PAGE_SIZE, "%u\n", msm_host->clk_gating_delay);
}

static ssize_t store_sdhci_msm_clk_gating(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	uint32_t value;

	if (!kstrtou32(buf, 0, &value)) {
		msm_host->clk_gating_delay = value;
		dev_info(dev, "set clk scaling work delay (%u)\n", value);
	}

	return count;
}

static ssize_t show_sdhci_msm_pm_qos(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);

	return scnprintf(buf, PAGE_SIZE, "%u\n", msm_host->pm_qos_delay);
}

static ssize_t store_sdhci_msm_pm_qos(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	uint32_t value;

	if (!kstrtou32(buf, 0, &value)) {
		msm_host->pm_qos_delay = value;
		dev_info(dev, "set pm qos work delay (%u)\n", value);
	}

	return count;
}

static void sdhci_msm_init_sysfs_gating_qos(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	int ret;

	msm_host->clk_gating.show = show_sdhci_msm_clk_gating;
	msm_host->clk_gating.store = store_sdhci_msm_clk_gating;
	sysfs_attr_init(&msm_host->clk_gating.attr);
	msm_host->clk_gating.attr.name = "clk_gating";
	msm_host->clk_gating.attr.mode = 0644;
	ret = device_create_file(dev, &msm_host->clk_gating);
	if (ret) {
		pr_err("%s: %s: failed creating clk gating attr: %d\n",
				mmc_hostname(host->mmc), __func__, ret);
	}

	msm_host->pm_qos.show = show_sdhci_msm_pm_qos;
	msm_host->pm_qos.store = store_sdhci_msm_pm_qos;
	sysfs_attr_init(&msm_host->pm_qos.attr);
	msm_host->pm_qos.attr.name = "pm_qos";
	msm_host->pm_qos.attr.mode = 0644;
	ret = device_create_file(dev, &msm_host->pm_qos);
	if (ret) {
		pr_err("%s: %s: failed creating pm qos attr: %d\n",
				mmc_hostname(host->mmc), __func__, ret);
	}
}

static void sdhci_msm_setup_pm(struct platform_device *pdev,
			struct sdhci_msm_host *msm_host)
{
	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	if (!(msm_host->mmc->caps & MMC_CAP_SYNC_RUNTIME_PM)) {
		pm_runtime_set_autosuspend_delay(&pdev->dev,
					 MSM_MMC_AUTOSUSPEND_DELAY_MS);
		pm_runtime_use_autosuspend(&pdev->dev);
	}
}

static int sdhci_msm_setup_ice_clk(struct sdhci_msm_host *msm_host,
						struct platform_device *pdev)
{
	int ret = 0;
	struct clk *clk;

	msm_host->bulk_clks[2].clk = NULL;

	/* Setup SDC ICE clock */
	clk = devm_clk_get(&pdev->dev, "ice_core");
	if (!IS_ERR(clk)) {
		msm_host->bulk_clks[2].clk = clk;

		/* Set maximum clock rate for ice clk */
		ret = clk_set_rate(clk, msm_host->ice_clk_max);
		if (ret) {
			dev_err(&pdev->dev, "ICE_CLK rate set failed (%d) for %u\n",
				ret, msm_host->ice_clk_max);
			return ret;
		}

		msm_host->ice_clk_rate = msm_host->ice_clk_max;
	}

	return ret;
}

static void sdhci_msm_set_caps(struct sdhci_msm_host *msm_host)
{
	msm_host->mmc->caps |= MMC_CAP_AGGRESSIVE_PM;
	msm_host->mmc->caps |= MMC_CAP_WAIT_WHILE_BUSY | MMC_CAP_NEED_RSP_BUSY;
}
/* RUMI W/A for SD card */
static void sdhci_msm_set_rumi_bus_mode(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_msm_offset *msm_offset = msm_host->offset;
	u32 config = readl_relaxed(host->ioaddr +
			msm_offset->core_vendor_spec_capabilities1);

	if (!(host->mmc->caps & MMC_CAP_NONREMOVABLE)) {
		config &= ~(VS_CAPABILITIES_SDR_104_SUPPORT |
				VS_CAPABILITIES_DDR_50_SUPPORT |
				VS_CAPABILITIES_SDR_50_SUPPORT);
		writel_relaxed(config, host->ioaddr +
				msm_offset->core_vendor_spec_capabilities1);
	}
}

static u32 is_bootdevice_sdhci = SDHCI_BOOT_DEVICE;

static int sdhci_qcom_read_boot_config(struct platform_device *pdev)
{
	u32 *buf;
	size_t len;
	struct nvmem_cell *cell;

	cell = nvmem_cell_get(&pdev->dev, "boot_conf");
	if (IS_ERR(cell)) {
		dev_warn(&pdev->dev, "nvmem cell get failed\n");
		return 0;
	}

	buf = nvmem_cell_read(cell, &len);
	if (IS_ERR(buf)) {
		dev_warn(&pdev->dev, "nvmem cell read failed\n");
		nvmem_cell_put(cell);
		return 0;
	}

	is_bootdevice_sdhci = (*buf) >> 1 & 0x1f;
	dev_info(&pdev->dev, "boot_config val = %x is_bootdevice_sdhci = %x\n",
			*buf, is_bootdevice_sdhci);
	kfree(buf);
	nvmem_cell_put(cell);

	return is_bootdevice_sdhci;
}

static int sdhci_msm_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_msm_host *msm_host;
	struct clk *clk;
	int ret;
	u16 host_version, core_minor;
	u32 core_version, config;
	u8 core_major;
	const struct sdhci_msm_offset *msm_offset;
	const struct sdhci_msm_variant_info *var_info;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;

	if (of_property_read_bool(node, "non-removable") && sdhci_qcom_read_boot_config(pdev)) {
		dev_err(dev, "SDHCI is not boot dev.\n");
		return 0;
	}

	host = sdhci_pltfm_init(pdev, &sdhci_msm_pdata, sizeof(*msm_host));
	if (IS_ERR(host))
		return PTR_ERR(host);

	host->sdma_boundary = 0;
	pltfm_host = sdhci_priv(host);
	msm_host = sdhci_pltfm_priv(pltfm_host);
	msm_host->mmc = host->mmc;
	msm_host->pdev = pdev;

	/**
	 * System resume triggers a card detect interrupt even when there's no
	 * card inserted. Core layer acquires the registered wakeup source for
	 * 5s thus preventing system suspend for 5s at least.
	 * Disable this wakesource until this is sorted out.
	 */
	if ((host->mmc->ws) && !(host->mmc->caps & MMC_CAP_NONREMOVABLE)) {
		wakeup_source_unregister(host->mmc->ws);
		host->mmc->ws = NULL;
	}

	ret = mmc_of_parse(host->mmc);
	if (ret)
		goto pltfm_free;

	if (pdev->dev.of_node) {
		ret = of_alias_get_id(pdev->dev.of_node, "sdhc");
		if (ret <= 0)
			dev_err(&pdev->dev, "get slot index failed %d\n", ret);
		else if (ret <= 2)
			sdhci_slot[ret-1] = msm_host;
	}

	/*
	 * Based on the compatible string, load the required msm host info from
	 * the data associated with the version info.
	 */
	var_info = of_device_get_match_data(&pdev->dev);

	if (!var_info) {
		dev_err(&pdev->dev, "Compatible string not found\n");
		goto pltfm_free;
	}

	msm_host->mci_removed = var_info->mci_removed;
	msm_host->restore_dll_config = var_info->restore_dll_config;
	msm_host->var_ops = var_info->var_ops;
	msm_host->offset = var_info->offset;

	msm_offset = msm_host->offset;

	sdhci_get_of_property(pdev);
	sdhci_msm_get_of_property(pdev, host);

	msm_host->saved_tuning_phase = INVALID_TUNING_PHASE;

	ret = sdhci_msm_populate_pdata(dev, msm_host);
	if (ret) {
		dev_err(&pdev->dev, "DT parsing error\n");
		goto pltfm_free;
	}

	sdhci_msm_gcc_reset(&pdev->dev, host);
	msm_host->regs_restore.is_supported =
		of_property_read_bool(dev->of_node,
			"qcom,restore-after-cx-collapse");

	/* Setup SDCC bus voter clock. */
	msm_host->bus_clk = devm_clk_get(&pdev->dev, "bus");
	if (!IS_ERR(msm_host->bus_clk)) {
		/* Vote for max. clk rate for max. performance */
		ret = clk_set_rate(msm_host->bus_clk, INT_MAX);
		if (ret)
			goto pltfm_free;
		ret = clk_prepare_enable(msm_host->bus_clk);
		if (ret)
			goto pltfm_free;
	}

	/* Setup main peripheral bus clock */
	clk = devm_clk_get(&pdev->dev, "iface");
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		dev_err(&pdev->dev, "Peripheral clk setup failed (%d)\n", ret);
		goto bus_clk_disable;
	}
	msm_host->bulk_clks[1].clk = clk;

	/* Setup SDC MMC clock */
	clk = devm_clk_get(&pdev->dev, "core");
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		dev_err(&pdev->dev, "SDC MMC clk setup failed (%d)\n", ret);
		goto bus_clk_disable;
	}
	msm_host->bulk_clks[0].clk = clk;

	 /* Check for optional interconnect paths */
	ret = dev_pm_opp_of_find_icc_paths(&pdev->dev, NULL);
	if (ret)
		goto bus_clk_disable;

	msm_host->opp_token = dev_pm_opp_set_clkname(&pdev->dev, "core");
	if (msm_host->opp_token <= 0) {
		ret = msm_host->opp_token;
		goto bus_clk_disable;
	}

	/* OPP table is optional */
	ret = dev_pm_opp_of_add_table(&pdev->dev);
	if (!ret) {
		msm_host->has_opp_table = true;
	} else if (ret != -ENODEV) {
		dev_err(&pdev->dev, "Invalid OPP table in Device tree\n");
		goto opp_cleanup;
	}

	/* Vote for maximum clock rate for maximum performance */
	ret = dev_pm_opp_set_rate(&pdev->dev, INT_MAX);
	if (ret)
		dev_warn(&pdev->dev, "core clock boost failed\n");

	ret = sdhci_msm_setup_ice_clk(msm_host, pdev);
	if (ret)
		goto bus_clk_disable;

	clk = devm_clk_get(&pdev->dev, "cal");
	if (IS_ERR(clk))
		clk = NULL;
	msm_host->bulk_clks[3].clk = clk;

	clk = devm_clk_get(&pdev->dev, "sleep");
	if (IS_ERR(clk))
		clk = NULL;
	msm_host->bulk_clks[4].clk = clk;

	ret = clk_bulk_prepare_enable(ARRAY_SIZE(msm_host->bulk_clks),
				      msm_host->bulk_clks);
	if (ret)
		goto opp_cleanup;
	ret = qcom_clk_set_flags(msm_host->bulk_clks[0].clk,
			CLKFLAG_NORETAIN_MEM);
	if (ret)
		dev_err(&pdev->dev, "Failed to set core clk NORETAIN_MEM: %d\n",
				ret);
	ret = qcom_clk_set_flags(msm_host->bulk_clks[2].clk,
			CLKFLAG_RETAIN_MEM);
	if (ret)
		dev_err(&pdev->dev, "Failed to set ice clk RETAIN_MEM: %d\n",
				ret);
	/*
	 * xo clock is needed for FLL feature of cm_dll.
	 * In case if xo clock is not mentioned in DT, warn and proceed.
	 */
	msm_host->xo_clk = devm_clk_get(&pdev->dev, "xo");
	if (IS_ERR(msm_host->xo_clk)) {
		ret = PTR_ERR(msm_host->xo_clk);
		dev_warn(&pdev->dev, "TCXO clk not present (%d)\n", ret);
	}

	INIT_DELAYED_WORK(&msm_host->clk_gating_work,
			sdhci_msm_clkgate_bus_delayed_work);

	ret = sdhci_msm_bus_register(msm_host, pdev);
	if (ret && !msm_host->skip_bus_bw_voting) {
		dev_err(&pdev->dev, "Bus registration failed (%d)\n", ret);
		goto clk_disable;
	}

	if (!msm_host->skip_bus_bw_voting)
		sdhci_msm_bus_voting(host, true);

	/* Setup regulators */
	ret = sdhci_msm_vreg_init(&pdev->dev, msm_host, true);
	if (ret) {
		dev_err(&pdev->dev, "Regulator setup failed (%d)\n", ret);
		goto bus_unregister;
	}

	if (!msm_host->mci_removed) {
		msm_host->core_mem = devm_platform_ioremap_resource(pdev, 1);
		if (IS_ERR(msm_host->core_mem)) {
			ret = PTR_ERR(msm_host->core_mem);
			goto vreg_deinit;
		}
	}

	/* Reset the vendor spec register to power on reset state */
	writel_relaxed(CORE_VENDOR_SPEC_POR_VAL,
			host->ioaddr + msm_offset->core_vendor_spec);

	/* Ensure SDHCI FIFO is enabled by disabling alternative FIFO */
	config = readl_relaxed(host->ioaddr + msm_offset->core_vendor_spec3);
	config &= ~CORE_FIFO_ALT_EN;
	writel_relaxed(config, host->ioaddr + msm_offset->core_vendor_spec3);

	if (!msm_host->mci_removed) {
		/* Set HC_MODE_EN bit in HC_MODE register */
		msm_host_writel(msm_host, HC_MODE_EN, host,
				msm_offset->core_hc_mode);
		config = msm_host_readl(msm_host, host,
				msm_offset->core_hc_mode);
		config |= FF_CLK_SW_RST_DIS;
		msm_host_writel(msm_host, config, host,
				msm_offset->core_hc_mode);
	}

	if (of_property_read_bool(node, "is_rumi"))
		sdhci_msm_set_rumi_bus_mode(host);

	host_version = readw_relaxed((host->ioaddr + SDHCI_HOST_VERSION));
	dev_dbg(&pdev->dev, "Host Version: 0x%x Vendor Version 0x%x\n",
		host_version, ((host_version & SDHCI_VENDOR_VER_MASK) >>
			       SDHCI_VENDOR_VER_SHIFT));

	core_version = msm_host_readl(msm_host, host,
			msm_offset->core_mci_version);
	core_major = (core_version & CORE_VERSION_MAJOR_MASK) >>
		      CORE_VERSION_MAJOR_SHIFT;
	core_minor = core_version & CORE_VERSION_MINOR_MASK;
	dev_dbg(&pdev->dev, "MCI Version: 0x%08x, major: 0x%04x, minor: 0x%02x\n",
		core_version, core_major, core_minor);

	sdhci_set_default_hw_caps(msm_host, host);

	/*
	 * Support for some capabilities is not advertised by newer
	 * controller versions and must be explicitly enabled.
	 */
	if (core_major >= 1 && core_minor != 0x11 && core_minor != 0x12) {
		config = readl_relaxed(host->ioaddr + SDHCI_CAPABILITIES);
		config |= SDHCI_CAN_VDD_300 | SDHCI_CAN_DO_8BIT;
		writel_relaxed(config, host->ioaddr +
				msm_offset->core_vendor_spec_capabilities0);
	}

	if (core_major == 1 && core_minor >= 0x49)
		msm_host->updated_ddr_cfg = true;

	if (core_major == 1 && core_minor >= 0x71)
		msm_host->uses_tassadar_dll = true;

	ret = sdhci_msm_register_vreg(msm_host);
	if (ret)
		goto clk_disable;

	/*
	 * Power on reset state may trigger power irq if previous status of
	 * PWRCTL was either BUS_ON or IO_HIGH_V. So before enabling pwr irq
	 * interrupt in GIC, any pending power irq interrupt should be
	 * acknowledged. Otherwise power irq interrupt handler would be
	 * fired prematurely.
	 */
	sdhci_msm_handle_pwr_irq(host, 0);

	/*
	 * Ensure that above writes are propogated before interrupt enablement
	 * in GIC.
	 */
	mb();

	/* Setup IRQ for handling power/voltage tasks with PMIC */
	msm_host->pwr_irq = platform_get_irq_byname(pdev, "pwr_irq");
	if (msm_host->pwr_irq < 0) {
		ret = msm_host->pwr_irq;
		goto vreg_deinit;
	}

	sdhci_msm_init_pwr_irq_wait(msm_host);
	/* Enable pwr irq interrupts */
	msm_host_writel(msm_host, INT_MASK, host,
		msm_offset->core_pwrctl_mask);

	ret = devm_request_threaded_irq(&pdev->dev, msm_host->pwr_irq, NULL,
					sdhci_msm_pwr_irq, IRQF_ONESHOT,
					dev_name(&pdev->dev), host);
	if (ret) {
		dev_err(&pdev->dev, "Request IRQ failed (%d)\n", ret);
		goto vreg_deinit;
	}

	sdhci_msm_set_caps(msm_host);

	sdhci_msm_setup_pm(pdev, msm_host);

	host->mmc_host_ops.start_signal_voltage_switch =
		sdhci_msm_start_signal_voltage_switch;
	host->mmc_host_ops.execute_tuning = sdhci_msm_execute_tuning;

	msm_host->workq = create_workqueue("sdhci_msm_generic_swq");
	if (!msm_host->workq)
		dev_err(&pdev->dev, "Generic swq creation failed\n");

	msm_host->clk_gating_delay = MSM_CLK_GATING_DELAY_MS;
	msm_host->pm_qos_delay = MSM_MMC_AUTOSUSPEND_DELAY_MS;
	/* Initialize pmqos */
	sdhci_msm_qos_init(msm_host);
	/* Initialize sysfs entries */
	sdhci_msm_init_sysfs_gating_qos(dev);

	if (of_property_read_bool(node, "supports-cqe"))
		ret = sdhci_msm_cqe_add_host(host, pdev);
	else
		ret = sdhci_add_host(host);
	if (ret)
		goto pm_runtime_disable;

	/* For SDHC v5.0.0 onwards, ICE 3.0 specific registers are added
	 * in CQ register space, due to which few CQ registers are
	 * shifted. Set cqhci_offset_changed boolean to use updated address.
	 */
	if (core_major == 1 && core_minor >= 0x6B)
		msm_host->cqhci_offset_changed = true;

	/*
	 * Set platfm_init_done only after sdhci_add_host().
	 * So that we don't turn off vqmmc while we reset sdhc as
	 * part of sdhci_add_host().
	 */
	msm_host->pltfm_init_done = true;

	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);

	return 0;

pm_runtime_disable:
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);
vreg_deinit:
	sdhci_msm_vreg_init(&pdev->dev, msm_host, false);
bus_unregister:
	if (!msm_host->skip_bus_bw_voting) {
		sdhci_msm_bus_get_and_set_vote(host, 0);
		sdhci_msm_bus_unregister(&pdev->dev, msm_host);
	}
clk_disable:
	clk_bulk_disable_unprepare(ARRAY_SIZE(msm_host->bulk_clks),
				   msm_host->bulk_clks);
opp_cleanup:
	if (msm_host->has_opp_table)
		dev_pm_opp_of_remove_table(&pdev->dev);
	dev_pm_opp_put_clkname(msm_host->opp_token);
bus_clk_disable:
	if (!IS_ERR(msm_host->bus_clk))
		clk_disable_unprepare(msm_host->bus_clk);
pltfm_free:
	sdhci_pltfm_free(pdev);
	return ret;
}

static int sdhci_msm_remove(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_msm_host *msm_host;
	struct sdhci_msm_qos_req *r;
	struct qos_cpu_group *qcg;
	struct device_node *np = pdev->dev.of_node;
	int i;
	int dead;

	if (of_property_read_bool(np, "non-removable") && is_bootdevice_sdhci) {
		dev_err(&pdev->dev, "SDHCI is not boot dev.\n");
		return 0;
	}

	host = platform_get_drvdata(pdev);
	pltfm_host = sdhci_priv(host);
	msm_host = sdhci_pltfm_priv(pltfm_host);
	r = msm_host->sdhci_qos;

	dead = (readl_relaxed(host->ioaddr + SDHCI_INT_STATUS) ==
		    0xffffffff);

	sdhci_remove_host(host, dead);

	sdhci_msm_vreg_init(&pdev->dev, msm_host, false);

	if (msm_host->has_opp_table)
		dev_pm_opp_of_remove_table(&pdev->dev);
	dev_pm_opp_put_clkname(msm_host->opp_token);
	pm_runtime_get_sync(&pdev->dev);

	/* Add delay to complete resume where qos vote is scheduled */
	if (!r)
		goto skip_removing_qos;
	qcg = r->qcg;
	msleep(50);
	for (i = 0; i < r->num_groups; i++, qcg++) {
		sdhci_msm_update_qos_constraints(qcg, QOS_MAX);
		remove_group_qos(qcg);
	}
	destroy_workqueue(msm_host->workq);

skip_removing_qos:
	pm_runtime_disable(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	clk_bulk_disable_unprepare(ARRAY_SIZE(msm_host->bulk_clks),
				   msm_host->bulk_clks);
	if (!IS_ERR(msm_host->bus_clk))
		clk_disable_unprepare(msm_host->bus_clk);
	if (!msm_host->skip_bus_bw_voting) {
		sdhci_msm_bus_get_and_set_vote(host, 0);
		sdhci_msm_bus_unregister(&pdev->dev, msm_host);
	}
	sdhci_pltfm_free(pdev);
	return 0;
}

static __maybe_unused int sdhci_msm_runtime_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	struct sdhci_msm_qos_req *qos_req = msm_host->sdhci_qos;

	if (!qos_req)
		goto skip_qos;
	sdhci_msm_unvote_qos_all(msm_host);

skip_qos:
	queue_delayed_work(msm_host->workq,
			&msm_host->clk_gating_work,
			msecs_to_jiffies(msm_host->clk_gating_delay));
	return 0;
}

static __maybe_unused int sdhci_msm_runtime_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);
	struct sdhci_msm_qos_req *qos_req = msm_host->sdhci_qos;
	int ret;

	ret = cancel_delayed_work_sync(&msm_host->clk_gating_work);
	if (!ret) {
		sdhci_msm_bus_voting(host, true);
		dev_pm_opp_set_rate(dev, msm_host->clk_rate);
		ret = clk_bulk_prepare_enable(ARRAY_SIZE(msm_host->bulk_clks),
					       msm_host->bulk_clks);
		if (ret) {
			dev_err(dev, "Failed to enable clocks %d\n", ret);
			sdhci_msm_bus_voting(host, false);
			return ret;
		}

		sdhci_msm_registers_restore(host);
		/*
		 * Whenever core-clock is gated dynamically, it's needed to
		 * restore the SDR DLL settings when the clock is ungated.
		 */
		if (msm_host->restore_dll_config && msm_host->clk_rate)
			sdhci_msm_restore_sdr_dll_config(host);
	}

	if (!qos_req)
		return 0;

	sdhci_msm_vote_pmqos(msm_host->mmc,
			msm_host->sdhci_qos->active_mask);

	return sdhci_msm_ice_resume(msm_host);
}

static int sdhci_msm_suspend_late(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_host *msm_host = sdhci_pltfm_priv(pltfm_host);

	if (flush_delayed_work(&msm_host->clk_gating_work))
		dev_dbg(dev, "%s Waited for clk_gating_work to finish\n",
			 __func__);
	return 0;
}

static int sdhci_msm_wrapper_suspend_late(struct device *dev)
{
	struct device_node *np = dev->of_node;

	if (of_property_read_bool(np, "non-removable") && is_bootdevice_sdhci) {
		dev_info(dev, "SDHCI is not boot dev.\n");
		return 0;
	}

	return sdhci_msm_suspend_late(dev);
}

static const struct dev_pm_ops sdhci_msm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_LATE_SYSTEM_SLEEP_PM_OPS(sdhci_msm_wrapper_suspend_late, NULL)
	SET_RUNTIME_PM_OPS(sdhci_msm_runtime_suspend,
			   sdhci_msm_runtime_resume,
			   NULL)
};

static struct platform_driver sdhci_msm_driver = {
	.probe = sdhci_msm_probe,
	.remove = sdhci_msm_remove,
	.driver = {
		   .name = "sdhci_msm",
		   .probe_type = PROBE_PREFER_ASYNCHRONOUS,
		   .of_match_table = sdhci_msm_dt_match,
		   .pm = &sdhci_msm_pm_ops,
	},
};

module_platform_driver(sdhci_msm_driver);

MODULE_DESCRIPTION("Qualcomm Secure Digital Host Controller Interface driver");
MODULE_LICENSE("GPL v2");
