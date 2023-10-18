/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Copyright (c) 2011-2017, 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __ARCH_ARM_MACH_MSM_SPM_DEVICES_H
#define __ARCH_ARM_MACH_MSM_SPM_DEVICES_H

#include <soc/qcom/spm.h>

enum {
	MSM_SPM_REG_SAW_CFG,
	MSM_SPM_REG_SAW_AVS_CTL,
	MSM_SPM_REG_SAW_AVS_HYSTERESIS,
	MSM_SPM_REG_SAW_SPM_CTL,
	MSM_SPM_REG_SAW_PMIC_DLY,
	MSM_SPM_REG_SAW_AVS_LIMIT,
	MSM_SPM_REG_SAW_AVS_DLY,
	MSM_SPM_REG_SAW_SPM_DLY,
	MSM_SPM_REG_SAW_PMIC_DATA_0,
	MSM_SPM_REG_SAW_PMIC_DATA_1,
	MSM_SPM_REG_SAW_PMIC_DATA_2,
	MSM_SPM_REG_SAW_PMIC_DATA_3,
	MSM_SPM_REG_SAW_PMIC_DATA_4,
	MSM_SPM_REG_SAW_PMIC_DATA_5,
	MSM_SPM_REG_SAW_PMIC_DATA_6,
	MSM_SPM_REG_SAW_PMIC_DATA_7,
	MSM_SPM_REG_SAW_RST,

	MSM_SPM_REG_NR_INITIALIZE = MSM_SPM_REG_SAW_RST,

	MSM_SPM_REG_SAW_ID,
	MSM_SPM_REG_SAW_SECURE,
	MSM_SPM_REG_SAW_STS0,
	MSM_SPM_REG_SAW_STS1,
	MSM_SPM_REG_SAW_STS2,
	MSM_SPM_REG_SAW_VCTL,
	MSM_SPM_REG_SAW_SEQ_ENTRY,
	MSM_SPM_REG_SAW_SPM_STS,
	MSM_SPM_REG_SAW_AVS_STS,
	MSM_SPM_REG_SAW_PMIC_STS,
	MSM_SPM_REG_SAW_VERSION,

	MSM_SPM_REG_NR,
};

struct msm_spm_seq_entry {
	uint32_t mode;
	uint8_t *cmd;
	uint32_t ctl;
};

struct msm_spm_platform_data {
	void __iomem *reg_base_addr;
	uint32_t reg_init_values[MSM_SPM_REG_NR_INITIALIZE];

	uint32_t ver_reg;
	uint32_t vctl_port;
	int vctl_port_ub;
	uint32_t phase_port;
	uint32_t pfm_port;

	uint8_t awake_vlevel;
	uint32_t vctl_timeout_us;
	uint32_t avs_timeout_us;

	uint32_t num_modes;
	struct msm_spm_seq_entry *modes;
};

enum msm_spm_pmic_port {
	MSM_SPM_PMIC_VCTL_PORT,
	MSM_SPM_PMIC_PHASE_PORT,
	MSM_SPM_PMIC_PFM_PORT,
};

struct msm_spm_driver_data {
	uint32_t major;
	uint32_t minor;
	uint32_t ver_reg;
	uint32_t vctl_port;
	int vctl_port_ub;
	uint32_t phase_port;
	uint32_t pfm_port;
	void __iomem *reg_base_addr;
	uint32_t vctl_timeout_us;
	uint32_t avs_timeout_us;
	uint32_t reg_shadow[MSM_SPM_REG_NR];
	uint32_t *reg_seq_entry_shadow;
	uint32_t *reg_offsets;
};

int msm_spm_drv_init(struct msm_spm_driver_data *dev,
		struct msm_spm_platform_data *data);
int msm_spm_drv_reg_init(struct msm_spm_driver_data *dev,
		struct msm_spm_platform_data *data);
void msm_spm_drv_reinit(struct msm_spm_driver_data *dev, bool seq);
int msm_spm_drv_set_low_power_mode(struct msm_spm_driver_data *dev,
		uint32_t ctl);
int msm_spm_drv_set_vdd(struct msm_spm_driver_data *dev,
		unsigned int vlevel);
void dump_regs(struct msm_spm_driver_data *dev, int cpu);
uint32_t msm_spm_drv_get_sts_curr_pmic_data(
		struct msm_spm_driver_data *dev);
int msm_spm_drv_write_seq_data(struct msm_spm_driver_data *dev,
		uint8_t *cmd, uint32_t *offset);
void msm_spm_drv_flush_seq_entry(struct msm_spm_driver_data *dev);
int msm_spm_drv_set_spm_enable(struct msm_spm_driver_data *dev,
		bool enable);
int msm_spm_drv_set_pmic_data(struct msm_spm_driver_data *dev,
		enum msm_spm_pmic_port port, unsigned int data);

int msm_spm_drv_set_avs_limit(struct msm_spm_driver_data *dev,
		 uint32_t min_lvl, uint32_t max_lvl);

int msm_spm_drv_set_avs_enable(struct msm_spm_driver_data *dev,
		 bool enable);
int msm_spm_drv_get_avs_enable(struct msm_spm_driver_data *dev);

int msm_spm_drv_set_avs_irq_enable(struct msm_spm_driver_data *dev,
		enum msm_spm_avs_irq irq, bool enable);
int msm_spm_drv_avs_clear_irq(struct msm_spm_driver_data *dev,
		enum msm_spm_avs_irq irq);

void msm_spm_reinit(void);
int msm_spm_init(struct msm_spm_platform_data *data, int nr_devs);
void msm_spm_drv_upd_reg_shadow(struct msm_spm_driver_data *dev, int id,
		int val);
uint32_t msm_spm_drv_get_vdd(struct msm_spm_driver_data *dev);
#endif
