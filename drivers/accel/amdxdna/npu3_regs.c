// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */

#include <drm/amdxdna_accel.h>
#include <drm/drm_device.h>

#include "aie4_pci.h"
#include "amdxdna_pci_drv.h"

#define NPU3_MBOX_BAR		0

#define NPU3_MBOX_BUFFER_BAR	2
#define NPU3_MBOX_INFO_OFF	0x0

#define NPU3_DOORBELL_BAR       2
#define NPU3_DOORBELL_OFF       0x0

/* PCIe BAR Index for NPU3 */
#define NPU3_REG_BAR_INDEX	0
#define NPU3_PSP_BAR_INDEX      4
#define NPU3_SMU_BAR_INDEX      5

#define MMNPU_APERTURE3_BASE    0x3810000
#define MMNPU_APERTURE4_BASE    0x3B10000

#define NPU3_PSP_BAR_BASE       MMNPU_APERTURE3_BASE
#define NPU3_SMU_BAR_BASE       MMNPU_APERTURE4_BASE

#define MPASP_C2PMSG_123_ALT_1  0x3810AEC
#define MPASP_C2PMSG_156_ALT_1  0x3810B70
#define MPASP_C2PMSG_157_ALT_1  0x3810B74
#define MPASP_C2PMSG_73_ALT_1   0x3810A24

#define MP1_C2PMSG_59_ALT_1     0x3B109EC
#define MP1_C2PMSG_61_ALT_1     0x3B109F4
#define MP1_C2PMSG_60_ALT_1     0x3B109F0

static const struct amdxdna_fw_feature_tbl npu3_fw_feature_table[] = {
	{ .major = 5, .min_minor = 10 },
	{ 0 }
};

static const struct amdxdna_dev_priv npu3_dev_priv = {
	.npufw_path             = "npu.dev.sbin",
	.certfw_path            = "cert.dev.sbin",
	.mbox_bar		= NPU3_MBOX_BAR,
	.mbox_rbuf_bar		= NPU3_MBOX_BUFFER_BAR,
	.mbox_info_off		= NPU3_MBOX_INFO_OFF,
	.doorbell_off		= NPU3_DOORBELL_OFF,
	.psp_regs_off   = {
		DEFINE_BAR_OFFSET(PSP_CMD_REG,    NPU3_PSP, MPASP_C2PMSG_123_ALT_1),
		DEFINE_BAR_OFFSET(PSP_ARG0_REG,   NPU3_PSP, MPASP_C2PMSG_156_ALT_1),
		DEFINE_BAR_OFFSET(PSP_ARG1_REG,   NPU3_PSP, MPASP_C2PMSG_157_ALT_1),
		DEFINE_BAR_OFFSET(PSP_ARG2_REG,   NPU3_PSP, MPASP_C2PMSG_123_ALT_1),
		DEFINE_BAR_OFFSET(PSP_INTR_REG,   NPU3_PSP, MPASP_C2PMSG_73_ALT_1),
		DEFINE_BAR_OFFSET(PSP_STATUS_REG, NPU3_PSP, MPASP_C2PMSG_123_ALT_1),
		DEFINE_BAR_OFFSET(PSP_RESP_REG,   NPU3_PSP, MPASP_C2PMSG_156_ALT_1),
		/* npu3 doesn't use 8th pwaitmode register */
	},
	.smu_regs_off   = {
		DEFINE_BAR_OFFSET(SMU_CMD_REG,  NPU3_SMU, MP1_C2PMSG_59_ALT_1),
		DEFINE_BAR_OFFSET(SMU_ARG_REG,  NPU3_SMU, MP1_C2PMSG_61_ALT_1),
		DEFINE_BAR_OFFSET(SMU_INTR_REG, NPU3_SMU, MMNPU_APERTURE4_BASE),
		DEFINE_BAR_OFFSET(SMU_RESP_REG, NPU3_SMU, MP1_C2PMSG_60_ALT_1),
		DEFINE_BAR_OFFSET(SMU_OUT_REG,  NPU3_SMU, MP1_C2PMSG_61_ALT_1),
	},
};

static const struct amdxdna_dev_priv npu3_dev_vf_priv = {
	/* vf device does not load firmware */
	.mbox_bar		= NPU3_MBOX_BAR,
	.mbox_rbuf_bar		= NPU3_MBOX_BUFFER_BAR,
	.mbox_info_off		= NPU3_MBOX_INFO_OFF,
	/* vf device does not have smu and psp */
};

const struct amdxdna_dev_info dev_npu3_pf_info = {
	.mbox_bar		= NPU3_MBOX_BAR,
	.sram_bar		= NPU3_MBOX_BUFFER_BAR,
	.psp_bar                = NPU3_PSP_BAR_INDEX,
	.smu_bar		= NPU3_SMU_BAR_INDEX,
	.default_vbnv		= "RyzenAI-npu3-pf",
	.device_type		= AMDXDNA_DEV_TYPE_PF,
	.dev_priv		= &npu3_dev_priv,
	.fw_feature_tbl		= npu3_fw_feature_table,
	.ops			= &aie4_pf_ops,
};

const struct amdxdna_dev_info dev_npu3_vf_info = {
	.mbox_bar		= NPU3_MBOX_BAR,
	.sram_bar		= NPU3_MBOX_BUFFER_BAR,
	.doorbell_bar		= NPU3_DOORBELL_BAR,
	.default_vbnv		= "RyzenAI-npu3-vf",
	.device_type		= AMDXDNA_DEV_TYPE_UMQ,
	.dev_priv		= &npu3_dev_vf_priv,
	.fw_feature_tbl		= npu3_fw_feature_table,
	.ops			= &aie4_vf_ops,
};
