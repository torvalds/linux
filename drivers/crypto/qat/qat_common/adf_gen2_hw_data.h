/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2020 Intel Corporation */
#ifndef ADF_GEN2_HW_DATA_H_
#define ADF_GEN2_HW_DATA_H_

#include "adf_accel_devices.h"

/* AE to function map */
#define AE2FUNCTION_MAP_A_OFFSET	(0x3A400 + 0x190)
#define AE2FUNCTION_MAP_B_OFFSET	(0x3A400 + 0x310)
#define AE2FUNCTION_MAP_REG_SIZE	4
#define AE2FUNCTION_MAP_VALID		BIT(7)

#define READ_CSR_AE2FUNCTION_MAP_A(pmisc_bar_addr, index) \
	ADF_CSR_RD(pmisc_bar_addr, AE2FUNCTION_MAP_A_OFFSET + \
		   AE2FUNCTION_MAP_REG_SIZE * (index))
#define WRITE_CSR_AE2FUNCTION_MAP_A(pmisc_bar_addr, index, value) \
	ADF_CSR_WR(pmisc_bar_addr, AE2FUNCTION_MAP_A_OFFSET + \
		   AE2FUNCTION_MAP_REG_SIZE * (index), value)
#define READ_CSR_AE2FUNCTION_MAP_B(pmisc_bar_addr, index) \
	ADF_CSR_RD(pmisc_bar_addr, AE2FUNCTION_MAP_B_OFFSET + \
		   AE2FUNCTION_MAP_REG_SIZE * (index))
#define WRITE_CSR_AE2FUNCTION_MAP_B(pmisc_bar_addr, index, value) \
	ADF_CSR_WR(pmisc_bar_addr, AE2FUNCTION_MAP_B_OFFSET + \
		   AE2FUNCTION_MAP_REG_SIZE * (index), value)

void adf_gen2_cfg_iov_thds(struct adf_accel_dev *accel_dev, bool enable,
			   int num_a_regs, int num_b_regs);
void adf_gen2_init_hw_csr_ops(struct adf_hw_csr_ops *csr_ops);

#endif
