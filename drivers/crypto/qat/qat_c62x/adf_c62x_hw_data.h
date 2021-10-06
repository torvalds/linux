/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2020 Intel Corporation */
#ifndef ADF_C62X_HW_DATA_H_
#define ADF_C62X_HW_DATA_H_

/* PCIe configuration space */
#define ADF_C62X_SRAM_BAR 0
#define ADF_C62X_PMISC_BAR 1
#define ADF_C62X_ETR_BAR 2
#define ADF_C62X_RX_RINGS_OFFSET 8
#define ADF_C62X_TX_RINGS_MASK 0xFF
#define ADF_C62X_MAX_ACCELERATORS 5
#define ADF_C62X_MAX_ACCELENGINES 10
#define ADF_C62X_ACCELERATORS_REG_OFFSET 16
#define ADF_C62X_ACCELERATORS_MASK 0x1F
#define ADF_C62X_ACCELENGINES_MASK 0x3FF
#define ADF_C62X_ETR_MAX_BANKS 16
#define ADF_C62X_SMIAPF0_MASK_OFFSET (0x3A000 + 0x28)
#define ADF_C62X_SMIAPF1_MASK_OFFSET (0x3A000 + 0x30)
#define ADF_C62X_SMIA0_MASK 0xFFFF
#define ADF_C62X_SMIA1_MASK 0x1
#define ADF_C62X_SOFTSTRAP_CSR_OFFSET 0x2EC
/* Error detection and correction */
#define ADF_C62X_AE_CTX_ENABLES(i) (i * 0x1000 + 0x20818)
#define ADF_C62X_AE_MISC_CONTROL(i) (i * 0x1000 + 0x20960)
#define ADF_C62X_ENABLE_AE_ECC_ERR BIT(28)
#define ADF_C62X_ENABLE_AE_ECC_PARITY_CORR (BIT(24) | BIT(12))
#define ADF_C62X_UERRSSMSH(i) (i * 0x4000 + 0x18)
#define ADF_C62X_CERRSSMSH(i) (i * 0x4000 + 0x10)
#define ADF_C62X_ERRSSMSH_EN BIT(3)

#define ADF_C62X_PF2VF_OFFSET(i)	(0x3A000 + 0x280 + ((i) * 0x04))

/* AE to function mapping */
#define ADF_C62X_AE2FUNC_MAP_GRP_A_NUM_REGS 80
#define ADF_C62X_AE2FUNC_MAP_GRP_B_NUM_REGS 10

/* Firmware Binary */
#define ADF_C62X_FW "qat_c62x.bin"
#define ADF_C62X_MMP "qat_c62x_mmp.bin"

void adf_init_hw_data_c62x(struct adf_hw_device_data *hw_data);
void adf_clean_hw_data_c62x(struct adf_hw_device_data *hw_data);
#endif
