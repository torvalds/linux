/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2020 Intel Corporation */
#ifndef ADF_C3XXX_HW_DATA_H_
#define ADF_C3XXX_HW_DATA_H_

/* PCIe configuration space */
#define ADF_C3XXX_PMISC_BAR 0
#define ADF_C3XXX_ETR_BAR 1
#define ADF_C3XXX_SRAM_BAR 0
#define ADF_C3XXX_MAX_ACCELERATORS 3
#define ADF_C3XXX_MAX_ACCELENGINES 6
#define ADF_C3XXX_ACCELERATORS_REG_OFFSET 16
#define ADF_C3XXX_ACCELERATORS_MASK 0x7
#define ADF_C3XXX_ACCELENGINES_MASK 0x3F
#define ADF_C3XXX_ETR_MAX_BANKS 16
#define ADF_C3XXX_SOFTSTRAP_CSR_OFFSET 0x2EC

/* AE to function mapping */
#define ADF_C3XXX_AE2FUNC_MAP_GRP_A_NUM_REGS 48
#define ADF_C3XXX_AE2FUNC_MAP_GRP_B_NUM_REGS 6

/* Firmware Binary */
#define ADF_C3XXX_FW "qat_c3xxx.bin"
#define ADF_C3XXX_MMP "qat_c3xxx_mmp.bin"

void adf_init_hw_data_c3xxx(struct adf_hw_device_data *hw_data);
void adf_clean_hw_data_c3xxx(struct adf_hw_device_data *hw_data);
#endif
