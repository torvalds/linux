/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2013-2014,2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DRIVERS_MMC_SDHCI_MSM_H
#define _DRIVERS_MMC_SDHCI_MSM_H

#include <linux/of_device.h>
#include <linux/delay.h>
#include <linux/mmc/mmc.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/interconnect.h>
#include <linux/iopoll.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_qos.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/reset.h>

#include "sdhci-pltfm.h"
#if IS_ENABLED(CONFIG_MMC_SDHCI_MSM_SCALING)
#include "sdhci-msm-scaling.h"
#endif
#include "cqhci.h"

#define MMC_CAP2_CLK_SCALE      (1 << 28)       /* Allow dynamic clk scaling */

enum dev_state {
	DEV_SUSPENDING = 1,
	DEV_SUSPENDED,
	DEV_RESUMED,
};

enum sdhci_msm_mmc_load {
	MMC_LOAD_HIGH,
	MMC_LOAD_LOW,
};

/**
 * struct mmc_devfeq_clk_scaling - main context for MMC clock scaling logic
 *
 * @lock: spinlock to protect statistics
 * @devfreq: struct that represent mmc-host as a client for devfreq
 * @devfreq_profile: MMC device profile, mostly polling interval and callbacks
 * @ondemand_gov_data: struct supplied to ondemmand governor (thresholds)
 * @state: load state, can be HIGH or LOW. used to notify mmc_host_ops callback
 * @start_busy: timestamped armed once a data request is started
 * @measure_interval_start: timestamped armed once a measure interval started
 * @devfreq_abort: flag to sync between different contexts relevant to devfreq
 * @skip_clk_scale_freq_update: flag that enable/disable frequency change
 * @freq_table_sz: table size of frequencies supplied to devfreq
 * @freq_table: frequencies table supplied to devfreq
 * @curr_freq: current frequency
 * @polling_delay_ms: polling interval for status collection used by devfreq
 * @upthreshold: up-threshold supplied to ondemand governor
 * @downthreshold: down-threshold supplied to ondemand governor
 * @need_freq_change: flag indicating if a frequency change is required
 * @is_busy_started: flag indicating if a request is handled by the HW
 * @enable: flag indicating if the clock scaling logic is enabled for this host
 * @is_suspended: to make devfreq request queued when mmc is suspened
 */
#if IS_ENABLED(CONFIG_MMC_SDHCI_MSM_SCALING)
struct sdhci_msm_mmc_devfeq_clk_scaling {
	spinlock_t	lock;
	struct		devfreq *devfreq;
	struct		devfreq_dev_profile devfreq_profile;
	struct		devfreq_simple_ondemand_data ondemand_gov_data;
	enum sdhci_msm_mmc_load	state;
	ktime_t		start_busy;
	ktime_t		measure_interval_start;
	atomic_t	devfreq_abort;
	bool		skip_clk_scale_freq_update;
	int		freq_table_sz;
	int		pltfm_freq_table_sz;
	u32		*freq_table;
	u32		*pltfm_freq_table;
	unsigned long	total_busy_time_us;
	unsigned long	target_freq;
	unsigned long	curr_freq;
	unsigned long	polling_delay_ms;
	unsigned int	upthreshold;
	unsigned int	downthreshold;
	unsigned int	lower_bus_speed_mode;
#define MMC_SCALING_LOWER_DDR52_MODE	1
	bool		need_freq_change;
	bool		is_busy_started;
	bool		enable;
	bool		is_suspended;
};
#endif
struct sdhci_msm_variant_ops {
	u32 (*msm_readl_relaxed)(struct sdhci_host *host, u32 offset);
	void (*msm_writel_relaxed)(u32 val, struct sdhci_host *host,
			u32 offset);
};

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
	u32 ext_fb_clk;
};

struct cqe_regs_restore {
	u32 cqe_vendor_cfg1;
};

struct sdhci_msm_host {
	struct platform_device *pdev;
	void __iomem *core_mem;	/* MSM SDCC mapped address */
#ifdef CONFIG_MMC_CRYPTO
	void __iomem *ice_mem;	/* MSM ICE mapped address (if available) */
#endif
#if (IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER) || IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER_V1))
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
	void *sdhci_msm_ipc_log_ctx;
	bool dbg_en;
	bool enable_ext_fb_clk;
#if IS_ENABLED(CONFIG_MMC_SDHCI_MSM_SCALING)
	struct sdhci_msm_mmc_devfeq_clk_scaling clk_scaling;
#endif
	unsigned long           clk_scaling_lowest;     /* lowest scaleable
							 * frequency.
							 */
	unsigned long           clk_scaling_highest;    /* highest scaleable
							 * frequency.
							 */
	atomic_t active_reqs;
	unsigned int	part_curr;
	int scale_caps;
	int clk_scale_init_done;
	int defer_clk_scaling_resume;
	int scaling_suspended;
	u8 raw_ext_csd_cmdq;
	u8 raw_ext_csd_cache_ctrl;
	u8 raw_ext_csd_bus_width;
	u8 raw_ext_csd_hs_timing;
	struct mmc_ios cached_ios;
	struct notifier_block sdhci_msm_pm_notifier;
};

struct mmc_pwrseq_ops {
	void (*pre_power_on)(struct mmc_host *host);
	void (*post_power_on)(struct mmc_host *host);
	void (*power_off)(struct mmc_host *host);
	void (*reset)(struct mmc_host *host);
};

struct mmc_pwrseq {
	struct mmc_pwrseq_ops *ops;
	struct device *dev;
	struct list_head pwrseq_node;
	struct module *owner;
};

#endif
