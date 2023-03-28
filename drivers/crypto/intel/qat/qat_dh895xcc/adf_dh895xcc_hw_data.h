/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2020 Intel Corporation */
#ifndef ADF_DH895x_HW_DATA_H_
#define ADF_DH895x_HW_DATA_H_

/* PCIe configuration space */
#define ADF_DH895XCC_SRAM_BAR 0
#define ADF_DH895XCC_PMISC_BAR 1
#define ADF_DH895XCC_ETR_BAR 2
#define ADF_DH895XCC_FUSECTL_SKU_MASK 0x300000
#define ADF_DH895XCC_FUSECTL_SKU_SHIFT 20
#define ADF_DH895XCC_FUSECTL_SKU_1 0x0
#define ADF_DH895XCC_FUSECTL_SKU_2 0x1
#define ADF_DH895XCC_FUSECTL_SKU_3 0x2
#define ADF_DH895XCC_FUSECTL_SKU_4 0x3
#define ADF_DH895XCC_MAX_ACCELERATORS 6
#define ADF_DH895XCC_MAX_ACCELENGINES 12
#define ADF_DH895XCC_ACCELERATORS_REG_OFFSET 13
#define ADF_DH895XCC_ACCELERATORS_MASK 0x3F
#define ADF_DH895XCC_ACCELENGINES_MASK 0xFFF
#define ADF_DH895XCC_ETR_MAX_BANKS 32

/* Masks for VF2PF interrupts */
#define ADF_DH895XCC_ERR_REG_VF2PF_L(vf_src)	(((vf_src) & 0x01FFFE00) >> 9)
#define ADF_DH895XCC_ERR_MSK_VF2PF_L(vf_mask)	(((vf_mask) & 0xFFFF) << 9)
#define ADF_DH895XCC_ERR_REG_VF2PF_U(vf_src)	(((vf_src) & 0x0000FFFF) << 16)
#define ADF_DH895XCC_ERR_MSK_VF2PF_U(vf_mask)	((vf_mask) >> 16)

/* AE to function mapping */
#define ADF_DH895XCC_AE2FUNC_MAP_GRP_A_NUM_REGS 96
#define ADF_DH895XCC_AE2FUNC_MAP_GRP_B_NUM_REGS 12

/* FW names */
#define ADF_DH895XCC_FW "qat_895xcc.bin"
#define ADF_DH895XCC_MMP "qat_895xcc_mmp.bin"

void adf_init_hw_data_dh895xcc(struct adf_hw_device_data *hw_data);
void adf_clean_hw_data_dh895xcc(struct adf_hw_device_data *hw_data);
#endif
