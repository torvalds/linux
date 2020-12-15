/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2020 Intel Corporation */
#ifndef ADF_GEN2_HW_DATA_H_
#define ADF_GEN2_HW_DATA_H_

#include "adf_accel_devices.h"

/* Transport access */
#define ADF_BANK_INT_SRC_SEL_MASK_0	0x4444444CUL
#define ADF_BANK_INT_SRC_SEL_MASK_X	0x44444444UL
#define ADF_RING_CSR_RING_CONFIG	0x000
#define ADF_RING_CSR_RING_LBASE		0x040
#define ADF_RING_CSR_RING_UBASE		0x080
#define ADF_RING_CSR_RING_HEAD		0x0C0
#define ADF_RING_CSR_RING_TAIL		0x100
#define ADF_RING_CSR_E_STAT		0x14C
#define ADF_RING_CSR_INT_FLAG		0x170
#define ADF_RING_CSR_INT_SRCSEL		0x174
#define ADF_RING_CSR_INT_SRCSEL_2	0x178
#define ADF_RING_CSR_INT_COL_EN		0x17C
#define ADF_RING_CSR_INT_COL_CTL	0x180
#define ADF_RING_CSR_INT_FLAG_AND_COL	0x184
#define ADF_RING_CSR_INT_COL_CTL_ENABLE	0x80000000
#define ADF_RING_BUNDLE_SIZE		0x1000

#define BUILD_RING_BASE_ADDR(addr, size) \
	(((addr) >> 6) & (GENMASK_ULL(63, 0) << (size)))
#define READ_CSR_RING_HEAD(csr_base_addr, bank, ring) \
	ADF_CSR_RD(csr_base_addr, (ADF_RING_BUNDLE_SIZE * (bank)) + \
		   ADF_RING_CSR_RING_HEAD + ((ring) << 2))
#define READ_CSR_RING_TAIL(csr_base_addr, bank, ring) \
	ADF_CSR_RD(csr_base_addr, (ADF_RING_BUNDLE_SIZE * (bank)) + \
		   ADF_RING_CSR_RING_TAIL + ((ring) << 2))
#define READ_CSR_E_STAT(csr_base_addr, bank) \
	ADF_CSR_RD(csr_base_addr, (ADF_RING_BUNDLE_SIZE * (bank)) + \
		   ADF_RING_CSR_E_STAT)
#define WRITE_CSR_RING_CONFIG(csr_base_addr, bank, ring, value) \
	ADF_CSR_WR(csr_base_addr, (ADF_RING_BUNDLE_SIZE * (bank)) + \
		   ADF_RING_CSR_RING_CONFIG + ((ring) << 2), value)
#define WRITE_CSR_RING_BASE(csr_base_addr, bank, ring, value) \
do { \
	u32 l_base = 0, u_base = 0; \
	l_base = (u32)((value) & 0xFFFFFFFF); \
	u_base = (u32)(((value) & 0xFFFFFFFF00000000ULL) >> 32); \
	ADF_CSR_WR(csr_base_addr, (ADF_RING_BUNDLE_SIZE * (bank)) + \
		   ADF_RING_CSR_RING_LBASE + ((ring) << 2), l_base); \
	ADF_CSR_WR(csr_base_addr, (ADF_RING_BUNDLE_SIZE * (bank)) + \
		   ADF_RING_CSR_RING_UBASE + ((ring) << 2), u_base); \
} while (0)

#define WRITE_CSR_RING_HEAD(csr_base_addr, bank, ring, value) \
	ADF_CSR_WR(csr_base_addr, (ADF_RING_BUNDLE_SIZE * (bank)) + \
		   ADF_RING_CSR_RING_HEAD + ((ring) << 2), value)
#define WRITE_CSR_RING_TAIL(csr_base_addr, bank, ring, value) \
	ADF_CSR_WR(csr_base_addr, (ADF_RING_BUNDLE_SIZE * (bank)) + \
		   ADF_RING_CSR_RING_TAIL + ((ring) << 2), value)
#define WRITE_CSR_INT_FLAG(csr_base_addr, bank, value) \
	ADF_CSR_WR(csr_base_addr, (ADF_RING_BUNDLE_SIZE * (bank)) + \
		   ADF_RING_CSR_INT_FLAG, value)
#define WRITE_CSR_INT_SRCSEL(csr_base_addr, bank) \
do { \
	ADF_CSR_WR(csr_base_addr, (ADF_RING_BUNDLE_SIZE * (bank)) + \
	ADF_RING_CSR_INT_SRCSEL, ADF_BANK_INT_SRC_SEL_MASK_0); \
	ADF_CSR_WR(csr_base_addr, (ADF_RING_BUNDLE_SIZE * (bank)) + \
	ADF_RING_CSR_INT_SRCSEL_2, ADF_BANK_INT_SRC_SEL_MASK_X); \
} while (0)
#define WRITE_CSR_INT_COL_EN(csr_base_addr, bank, value) \
	ADF_CSR_WR(csr_base_addr, (ADF_RING_BUNDLE_SIZE * (bank)) + \
		   ADF_RING_CSR_INT_COL_EN, value)
#define WRITE_CSR_INT_COL_CTL(csr_base_addr, bank, value) \
	ADF_CSR_WR(csr_base_addr, (ADF_RING_BUNDLE_SIZE * (bank)) + \
		   ADF_RING_CSR_INT_COL_CTL, \
		   ADF_RING_CSR_INT_COL_CTL_ENABLE | (value))
#define WRITE_CSR_INT_FLAG_AND_COL(csr_base_addr, bank, value) \
	ADF_CSR_WR(csr_base_addr, (ADF_RING_BUNDLE_SIZE * (bank)) + \
		   ADF_RING_CSR_INT_FLAG_AND_COL, value)

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

/* Admin Interface Offsets */
#define ADF_ADMINMSGUR_OFFSET	(0x3A000 + 0x574)
#define ADF_ADMINMSGLR_OFFSET	(0x3A000 + 0x578)
#define ADF_MAILBOX_BASE_OFFSET	0x20970

/* Arbiter configuration */
#define ADF_ARB_OFFSET			0x30000
#define ADF_ARB_WRK_2_SER_MAP_OFFSET	0x180
#define ADF_ARB_CONFIG			(BIT(31) | BIT(6) | BIT(0))
#define ADF_ARB_REG_SLOT		0x1000
#define ADF_ARB_RINGSRVARBEN_OFFSET	0x19C

#define WRITE_CSR_RING_SRV_ARB_EN(csr_addr, index, value) \
	ADF_CSR_WR(csr_addr, ADF_ARB_RINGSRVARBEN_OFFSET + \
	(ADF_ARB_REG_SLOT * (index)), value)

/* Power gating */
#define ADF_POWERGATE_PKE		BIT(24)

void adf_gen2_cfg_iov_thds(struct adf_accel_dev *accel_dev, bool enable,
			   int num_a_regs, int num_b_regs);
void adf_gen2_init_hw_csr_ops(struct adf_hw_csr_ops *csr_ops);
void adf_gen2_get_admin_info(struct admin_info *admin_csrs_info);
void adf_gen2_get_arb_info(struct arb_info *arb_info);
u32 adf_gen2_get_accel_cap(struct adf_accel_dev *accel_dev);

#endif
