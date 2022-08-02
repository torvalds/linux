/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AMD Platform Management Framework Driver
 *
 * Copyright (c) 2022, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 */

#ifndef PMF_H
#define PMF_H

#include <linux/acpi.h>
#include <linux/platform_profile.h>

/* APMF Functions */
#define APMF_FUNC_VERIFY_INTERFACE			0
#define APMF_FUNC_GET_SYS_PARAMS			1
#define APMF_FUNC_STATIC_SLIDER_GRANULAR       9

/* Message Definitions */
#define SET_SPL				0x03 /* SPL: Sustained Power Limit */
#define SET_SPPT			0x05 /* SPPT: Slow Package Power Tracking */
#define SET_FPPT			0x07 /* FPPT: Fast Package Power Tracking */
#define GET_SPL				0x0B
#define GET_SPPT			0x0D
#define GET_FPPT			0x0F
#define SET_DRAM_ADDR_HIGH	0x14
#define SET_DRAM_ADDR_LOW	0x15
#define SET_TRANSFER_TABLE	0x16
#define SET_STT_MIN_LIMIT	0x18 /* STT: Skin Temperature Tracking */
#define SET_STT_LIMIT_APU	0x19
#define SET_STT_LIMIT_HS2	0x1A
#define SET_SPPT_APU_ONLY	0x1D
#define GET_SPPT_APU_ONLY	0x1E
#define GET_STT_MIN_LIMIT	0x1F
#define GET_STT_LIMIT_APU	0x20
#define GET_STT_LIMIT_HS2	0x21

#define ARG_NONE 0

/* AMD PMF BIOS interfaces */
struct apmf_verify_interface {
	u16 size;
	u16 version;
	u32 notification_mask;
	u32 supported_functions;
} __packed;

struct apmf_system_params {
	u16 size;
	u32 valid_mask;
	u32 flags;
	u8 command_code;
} __packed;

enum amd_stt_skin_temp {
	STT_TEMP_APU,
	STT_TEMP_HS2,
	STT_TEMP_COUNT,
};

enum amd_slider_op {
	SLIDER_OP_GET,
	SLIDER_OP_SET,
};

enum power_source {
	POWER_SOURCE_AC,
	POWER_SOURCE_DC,
	POWER_SOURCE_MAX,
};

enum power_modes {
	POWER_MODE_PERFORMANCE,
	POWER_MODE_BALANCED_POWER,
	POWER_MODE_POWER_SAVER,
	POWER_MODE_MAX,
};

struct amd_pmf_dev {
	void __iomem *regbase;
	void __iomem *smu_virt_addr;
	void *buf;
	u32 base_addr;
	u32 cpu_id;
	struct device *dev;
	struct mutex lock; /* protects the PMF interface */
	u32 supported_func;
	enum platform_profile_option current_profile;
	struct platform_profile_handler pprof;
};

struct apmf_sps_prop_granular {
	u32 fppt;
	u32 sppt;
	u32 sppt_apu_only;
	u32 spl;
	u32 stt_min;
	u8 stt_skin_temp[STT_TEMP_COUNT];
	u32 fan_id;
} __packed;

/* Static Slider */
struct apmf_static_slider_granular_output {
	u16 size;
	struct apmf_sps_prop_granular prop[POWER_SOURCE_MAX * POWER_MODE_MAX];
} __packed;

struct amd_pmf_static_slider_granular {
	u16 size;
	struct apmf_sps_prop_granular prop[POWER_SOURCE_MAX][POWER_MODE_MAX];
};

/* Core Layer */
int apmf_acpi_init(struct amd_pmf_dev *pmf_dev);
int is_apmf_func_supported(struct amd_pmf_dev *pdev, unsigned long index);
int amd_pmf_send_cmd(struct amd_pmf_dev *dev, u8 message, bool get, u32 arg, u32 *data);
int amd_pmf_get_power_source(void);

/* SPS Layer */
u8 amd_pmf_get_pprof_modes(struct amd_pmf_dev *pmf);
void amd_pmf_update_slider(struct amd_pmf_dev *dev, bool op, int idx,
			   struct amd_pmf_static_slider_granular *table);
int amd_pmf_init_sps(struct amd_pmf_dev *dev);
void amd_pmf_deinit_sps(struct amd_pmf_dev *dev);
int apmf_get_static_slider_granular(struct amd_pmf_dev *pdev,
				    struct apmf_static_slider_granular_output *output);


#endif /* PMF_H */
