/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _NPU_COMMON_H
#define _NPU_COMMON_H

/* -------------------------------------------------------------------------
 * Includes
 * -------------------------------------------------------------------------
 */
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mailbox_controller.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/msm_npu.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/mailbox/qmp.h>

#include "npu_mgr.h"

/* -------------------------------------------------------------------------
 * Defines
 * -------------------------------------------------------------------------
 */
#define NPU_MAX_MBOX_NUM	    2
#define NPU_MBOX_LOW_PRI	    0
#define NPU_MBOX_HIGH_PRI	    1

#define DEFAULT_REG_DUMP_NUM	64
#define ROW_BYTES 16
#define GROUP_BYTES 4

#define NUM_MAX_CLK_NUM			24
#define NPU_MAX_REGULATOR_NUM	2
#define NPU_MAX_DT_NAME_LEN	    21
#define NPU_MAX_PWRLEVELS		8
#define NPU_MAX_STATS_BUF_SIZE 16384
#define NPU_MAX_PATCH_NUM		160

#define PERF_MODE_DEFAULT 0

enum npu_power_level {
	NPU_PWRLEVEL_MINSVS = 0,
	NPU_PWRLEVEL_LOWSVS,
	NPU_PWRLEVEL_SVS,
	NPU_PWRLEVEL_SVS_L1,
	NPU_PWRLEVEL_NOM,
	NPU_PWRLEVEL_NOM_L1,
	NPU_PWRLEVEL_TURBO,
	NPU_PWRLEVEL_TURBO_L1,
	NPU_PWRLEVEL_OFF = 0xFFFFFFFF,
};

/* -------------------------------------------------------------------------
 * Data Structures
 * -------------------------------------------------------------------------
 */
struct npu_smmu_ctx {
	int domain;
	struct dma_iommu_mapping *mmu_mapping;
	struct reg_bus_client *reg_bus_clt;
	int32_t attach_cnt;
};

struct npu_ion_buf {
	int fd;
	struct dma_buf *dma_buf;
	struct dma_buf_attachment *attachment;
	struct sg_table *table;
	dma_addr_t iova;
	uint32_t size;
	void *phys_addr;
	void *buf;
	struct list_head list;
};

struct npu_clk {
	struct clk *clk;
	char clk_name[NPU_MAX_DT_NAME_LEN];
};

struct npu_regulator {
	struct regulator *regulator;
	char regulator_name[NPU_MAX_DT_NAME_LEN];
};

struct npu_debugfs_ctx {
	struct dentry *root;
	uint32_t reg_off;
	uint32_t reg_cnt;
};

struct npu_debugfs_reg_ctx {
	char *buf;
	size_t buf_len;
	struct npu_device *npu_dev;
};

struct npu_mbox {
	struct mbox_client client;
	struct mbox_chan *chan;
	struct npu_device *npu_dev;
	uint32_t id;
};

/**
 * struct npu_pwrlevel - Struct holding different pwrlevel info obtained
 * from dtsi file
 * @pwr_level:           NPU power level
 * @freq[]:              NPU frequency vote in Hz
 */
struct npu_pwrlevel {
	uint32_t pwr_level;
	long clk_freq[NUM_MAX_CLK_NUM];
};

/*
 * struct npu_reg - Struct holding npu register information
 * @ off - register offset
 * @ val - register value
 * @ valid - if register value is valid
 */
struct npu_reg {
	uint32_t off;
	uint32_t val;
	bool valid;
};

/**
 * struct npu_pwrctrl - Power control settings for a NPU device
 * @pwr_vote_num - voting information for power enable
 * @pwrlevels - List of supported power levels
 * @active_pwrlevel - The currently active power level
 * @default_pwrlevel - device wake up power level
 * @max_pwrlevel - maximum allowable powerlevel per the user
 * @min_pwrlevel - minimum allowable powerlevel per the user
 * @num_pwrlevels - number of available power levels
 * @cdsprm_pwrlevel - maximum power level from cdsprm
 * @fmax_pwrlevel - maximum power level from qfprom fmax setting
 * @uc_pwrlevel - power level from user driver setting
 * @perf_mode_override - perf mode from sysfs to override perf mode
 *                       settings from user driver
 * @dcvs_mode - dcvs mode from sysfs to turn on dcvs mode
 *              settings from user driver
 * @devbw - bw device
 */
struct npu_pwrctrl {
	int32_t pwr_vote_num;

	struct npu_pwrlevel pwrlevels[NPU_MAX_PWRLEVELS];
	uint32_t active_pwrlevel;
	uint32_t default_pwrlevel;
	uint32_t max_pwrlevel;
	uint32_t min_pwrlevel;
	uint32_t num_pwrlevels;

	struct device *devbw;
	uint32_t bwmon_enabled;
	uint32_t uc_pwrlevel;
	uint32_t cdsprm_pwrlevel;
	uint32_t fmax_pwrlevel;
	uint32_t perf_mode_override;
	uint32_t dcvs_mode;
	uint32_t cur_dcvs_activity;
};

/**
 * struct npu_thermalctrl - Thermal control settings for a NPU device
 * @max_state - maximum thermal mitigation state
 * @current_state - current thermal mitigation state
 * @pwr_level -power level that thermal control requested
 */
struct npu_thermalctrl {
	unsigned long max_state;
	unsigned long current_state;
	uint32_t pwr_level;
};

#define NPU_MAX_IRQ		3

struct npu_irq {
	char *name;
	int irq;
	int irq_type;
};

struct npu_io_data {
	size_t size;
	void __iomem *base;
};

struct npu_fw_io_data {
	phys_addr_t mem_phys;
	phys_addr_t mem_reloc;
	void *mem_region;
	size_t mem_size;
};

struct npu_device {
	struct mutex dev_lock;

	struct platform_device *pdev;

	dev_t dev_num;
	struct cdev cdev;
	struct class *class;
	struct device *device;

	struct npu_io_data core_io;
	struct npu_io_data tcm_io;
	struct npu_io_data bwmon_io;
	struct npu_io_data qfprom_io;
	struct npu_fw_io_data fw_io;

	uint32_t core_clk_num;
	struct npu_clk core_clks[NUM_MAX_CLK_NUM];

	uint32_t regulator_num;
	struct npu_regulator regulators[NPU_MAX_DT_NAME_LEN];

	struct npu_irq irq[NPU_MAX_IRQ];

	struct device *cb_device;

	struct npu_host_ctx host_ctx;
	struct npu_smmu_ctx smmu_ctx;
	struct npu_debugfs_ctx debugfs_ctx;

	struct npu_mbox mbox_aop;

	struct thermal_cooling_device *tcdev;
	struct npu_pwrctrl pwrctrl;
	struct npu_thermalctrl thermalctrl;

	struct llcc_slice_desc *sys_cache;
	uint32_t execute_v2_flag;
	bool cxlimit_registered;
	struct icc_path *icc_npu_cdspmem;
	struct icc_path *icc_cpu_imemcfg;
	uint32_t hw_version;
};

struct npu_client {
	struct npu_device *npu_dev;
	struct mutex list_lock;
	struct list_head mapped_buffer_list;
};

/* -------------------------------------------------------------------------
 * Function Prototypes
 * -------------------------------------------------------------------------
 */
int npu_debugfs_init(struct npu_device *npu_dev);
void npu_debugfs_deinit(struct npu_device *npu_dev);

int npu_enable_core_power(struct npu_device *npu_dev);
void npu_disable_core_power(struct npu_device *npu_dev);
int npu_enable_post_pil_clocks(struct npu_device *npu_dev);
void npu_disable_post_pil_clocks(struct npu_device *npu_dev);

irqreturn_t npu_intr_hdler(int irq, void *ptr);

int npu_set_uc_power_level(struct npu_device *npu_dev,
	uint32_t pwr_level);

int fw_init(struct npu_device *npu_dev);
void fw_deinit(struct npu_device *npu_dev, bool ssr, bool fw_alive);
int npu_notify_cdsprm_cxlimit_activity(struct npu_device *npu_dev, bool enable);

#endif /* _NPU_COMMON_H */
