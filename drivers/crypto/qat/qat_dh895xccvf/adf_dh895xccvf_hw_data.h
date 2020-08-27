/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2015 - 2020 Intel Corporation */
#ifndef ADF_DH895XVF_HW_DATA_H_
#define ADF_DH895XVF_HW_DATA_H_

#define ADF_DH895XCCIOV_PMISC_BAR 1
#define ADF_DH895XCCIOV_ACCELERATORS_MASK 0x1
#define ADF_DH895XCCIOV_ACCELENGINES_MASK 0x1
#define ADF_DH895XCCIOV_MAX_ACCELERATORS 1
#define ADF_DH895XCCIOV_MAX_ACCELENGINES 1
#define ADF_DH895XCCIOV_RX_RINGS_OFFSET 8
#define ADF_DH895XCCIOV_TX_RINGS_MASK 0xFF
#define ADF_DH895XCCIOV_ETR_BAR 0
#define ADF_DH895XCCIOV_ETR_MAX_BANKS 1
#define ADF_DH895XCCIOV_PF2VF_OFFSET	0x200
#define ADF_DH895XCCIOV_VINTMSK_OFFSET	0x208

void adf_init_hw_data_dh895xcciov(struct adf_hw_device_data *hw_data);
void adf_clean_hw_data_dh895xcciov(struct adf_hw_device_data *hw_data);
#endif
