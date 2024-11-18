// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023-2024, Advanced Micro Devices, Inc.
 */

#include <drm/amdxdna_accel.h>
#include <drm/drm_device.h>
#include <linux/sizes.h>

#include "aie2_pci.h"
#include "amdxdna_pci_drv.h"

/* NPU Public Registers on MpNPUAxiXbar (refer to Diag npu_registers.h) */
#define MPNPU_PUB_SEC_INTR             0x3010060
#define MPNPU_PUB_PWRMGMT_INTR         0x3010064
#define MPNPU_PUB_SCRATCH0             0x301006C
#define MPNPU_PUB_SCRATCH1             0x3010070
#define MPNPU_PUB_SCRATCH2             0x3010074
#define MPNPU_PUB_SCRATCH3             0x3010078
#define MPNPU_PUB_SCRATCH4             0x301007C
#define MPNPU_PUB_SCRATCH5             0x3010080
#define MPNPU_PUB_SCRATCH6             0x3010084
#define MPNPU_PUB_SCRATCH7             0x3010088
#define MPNPU_PUB_SCRATCH8             0x301008C
#define MPNPU_PUB_SCRATCH9             0x3010090
#define MPNPU_PUB_SCRATCH10            0x3010094
#define MPNPU_PUB_SCRATCH11            0x3010098
#define MPNPU_PUB_SCRATCH12            0x301009C
#define MPNPU_PUB_SCRATCH13            0x30100A0
#define MPNPU_PUB_SCRATCH14            0x30100A4
#define MPNPU_PUB_SCRATCH15            0x30100A8
#define MP0_C2PMSG_73                  0x3810A24
#define MP0_C2PMSG_123                 0x3810AEC

#define MP1_C2PMSG_0                   0x3B10900
#define MP1_C2PMSG_60                  0x3B109F0
#define MP1_C2PMSG_61                  0x3B109F4

#define MPNPU_SRAM_X2I_MAILBOX_0       0x3600000
#define MPNPU_SRAM_X2I_MAILBOX_15      0x361E000
#define MPNPU_SRAM_X2I_MAILBOX_31      0x363E000
#define MPNPU_SRAM_I2X_MAILBOX_31      0x363F000

#define MMNPU_APERTURE0_BASE           0x3000000
#define MMNPU_APERTURE1_BASE           0x3600000
#define MMNPU_APERTURE3_BASE           0x3810000
#define MMNPU_APERTURE4_BASE           0x3B10000

/* PCIe BAR Index for NPU4 */
#define NPU4_REG_BAR_INDEX	0
#define NPU4_MBOX_BAR_INDEX	0
#define NPU4_PSP_BAR_INDEX	4
#define NPU4_SMU_BAR_INDEX	5
#define NPU4_SRAM_BAR_INDEX	2
/* Associated BARs and Apertures */
#define NPU4_REG_BAR_BASE	MMNPU_APERTURE0_BASE
#define NPU4_MBOX_BAR_BASE	MMNPU_APERTURE0_BASE
#define NPU4_PSP_BAR_BASE	MMNPU_APERTURE3_BASE
#define NPU4_SMU_BAR_BASE	MMNPU_APERTURE4_BASE
#define NPU4_SRAM_BAR_BASE	MMNPU_APERTURE1_BASE

#define NPU4_RT_CFG_TYPE_PDI_LOAD 5
#define NPU4_RT_CFG_VAL_PDI_LOAD_MGMT 0
#define NPU4_RT_CFG_VAL_PDI_LOAD_APP 1

#define NPU4_MPNPUCLK_FREQ_MAX  1267
#define NPU4_HCLK_FREQ_MAX      1800

const struct amdxdna_dev_priv npu4_dev_priv = {
	.fw_path        = "amdnpu/17f0_10/npu.sbin",
	.protocol_major = 0x6,
	.protocol_minor = 0x1,
	.rt_config	= {NPU4_RT_CFG_TYPE_PDI_LOAD, NPU4_RT_CFG_VAL_PDI_LOAD_APP},
	.col_align	= COL_ALIGN_NATURE,
	.mbox_dev_addr  = NPU4_MBOX_BAR_BASE,
	.mbox_size      = 0, /* Use BAR size */
	.sram_dev_addr  = NPU4_SRAM_BAR_BASE,
	.sram_offs      = {
		DEFINE_BAR_OFFSET(MBOX_CHANN_OFF, NPU4_SRAM, MPNPU_SRAM_X2I_MAILBOX_0),
		DEFINE_BAR_OFFSET(FW_ALIVE_OFF,   NPU4_SRAM, MPNPU_SRAM_X2I_MAILBOX_15),
	},
	.psp_regs_off   = {
		DEFINE_BAR_OFFSET(PSP_CMD_REG,    NPU4_PSP, MP0_C2PMSG_123),
		DEFINE_BAR_OFFSET(PSP_ARG0_REG,   NPU4_REG, MPNPU_PUB_SCRATCH3),
		DEFINE_BAR_OFFSET(PSP_ARG1_REG,   NPU4_REG, MPNPU_PUB_SCRATCH4),
		DEFINE_BAR_OFFSET(PSP_ARG2_REG,   NPU4_REG, MPNPU_PUB_SCRATCH9),
		DEFINE_BAR_OFFSET(PSP_INTR_REG,   NPU4_PSP, MP0_C2PMSG_73),
		DEFINE_BAR_OFFSET(PSP_STATUS_REG, NPU4_PSP, MP0_C2PMSG_123),
		DEFINE_BAR_OFFSET(PSP_RESP_REG,   NPU4_REG, MPNPU_PUB_SCRATCH3),
	},
	.smu_regs_off   = {
		DEFINE_BAR_OFFSET(SMU_CMD_REG,  NPU4_SMU, MP1_C2PMSG_0),
		DEFINE_BAR_OFFSET(SMU_ARG_REG,  NPU4_SMU, MP1_C2PMSG_60),
		DEFINE_BAR_OFFSET(SMU_INTR_REG, NPU4_SMU, MMNPU_APERTURE4_BASE),
		DEFINE_BAR_OFFSET(SMU_RESP_REG, NPU4_SMU, MP1_C2PMSG_61),
		DEFINE_BAR_OFFSET(SMU_OUT_REG,  NPU4_SMU, MP1_C2PMSG_60),
	},
	.smu_mpnpuclk_freq_max = NPU4_MPNPUCLK_FREQ_MAX,
	.smu_hclk_freq_max     = NPU4_HCLK_FREQ_MAX,
};

const struct amdxdna_dev_info dev_npu4_info = {
	.reg_bar           = NPU4_REG_BAR_INDEX,
	.mbox_bar          = NPU4_MBOX_BAR_INDEX,
	.sram_bar          = NPU4_SRAM_BAR_INDEX,
	.psp_bar           = NPU4_PSP_BAR_INDEX,
	.smu_bar           = NPU4_SMU_BAR_INDEX,
	.first_col         = 0,
	.dev_mem_buf_shift = 15, /* 32 KiB aligned */
	.dev_mem_base      = AIE2_DEVM_BASE,
	.dev_mem_size      = AIE2_DEVM_SIZE,
	.vbnv              = "RyzenAI-npu4",
	.device_type       = AMDXDNA_DEV_TYPE_KMQ,
	.dev_priv          = &npu4_dev_priv,
	.ops               = &aie2_ops, /* NPU4 can share NPU1's callback */
};
