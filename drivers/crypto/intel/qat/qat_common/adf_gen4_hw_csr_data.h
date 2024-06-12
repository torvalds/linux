/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2024 Intel Corporation */
#ifndef ADF_GEN4_HW_CSR_DATA_H_
#define ADF_GEN4_HW_CSR_DATA_H_

#include <linux/bitops.h>
#include "adf_accel_devices.h"

#define ADF_BANK_INT_SRC_SEL_MASK	0x44UL
#define ADF_RING_CSR_RING_CONFIG	0x1000
#define ADF_RING_CSR_RING_LBASE		0x1040
#define ADF_RING_CSR_RING_UBASE		0x1080
#define ADF_RING_CSR_RING_HEAD		0x0C0
#define ADF_RING_CSR_RING_TAIL		0x100
#define ADF_RING_CSR_STAT		0x140
#define ADF_RING_CSR_UO_STAT		0x148
#define ADF_RING_CSR_E_STAT		0x14C
#define ADF_RING_CSR_NE_STAT		0x150
#define ADF_RING_CSR_NF_STAT		0x154
#define ADF_RING_CSR_F_STAT		0x158
#define ADF_RING_CSR_C_STAT		0x15C
#define ADF_RING_CSR_INT_FLAG_EN	0x16C
#define ADF_RING_CSR_INT_FLAG		0x170
#define ADF_RING_CSR_INT_SRCSEL		0x174
#define ADF_RING_CSR_INT_COL_EN		0x17C
#define ADF_RING_CSR_INT_COL_CTL	0x180
#define ADF_RING_CSR_INT_FLAG_AND_COL	0x184
#define ADF_RING_CSR_EXP_STAT		0x188
#define ADF_RING_CSR_EXP_INT_EN		0x18C
#define ADF_RING_CSR_INT_COL_CTL_ENABLE	0x80000000
#define ADF_RING_CSR_ADDR_OFFSET	0x100000
#define ADF_RING_BUNDLE_SIZE		0x2000
#define ADF_RING_CSR_RING_SRV_ARB_EN	0x19C

#define BUILD_RING_BASE_ADDR(addr, size) \
	((((addr) >> 6) & (GENMASK_ULL(63, 0) << (size))) << 6)
#define READ_CSR_RING_HEAD(csr_base_addr, bank, ring) \
	ADF_CSR_RD((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + \
		   ADF_RING_CSR_RING_HEAD + ((ring) << 2))
#define READ_CSR_RING_TAIL(csr_base_addr, bank, ring) \
	ADF_CSR_RD((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + \
		   ADF_RING_CSR_RING_TAIL + ((ring) << 2))
#define READ_CSR_STAT(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + ADF_RING_CSR_STAT)
#define READ_CSR_UO_STAT(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + ADF_RING_CSR_UO_STAT)
#define READ_CSR_E_STAT(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + ADF_RING_CSR_E_STAT)
#define READ_CSR_NE_STAT(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + ADF_RING_CSR_NE_STAT)
#define READ_CSR_NF_STAT(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + ADF_RING_CSR_NF_STAT)
#define READ_CSR_F_STAT(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + ADF_RING_CSR_F_STAT)
#define READ_CSR_C_STAT(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + ADF_RING_CSR_C_STAT)
#define READ_CSR_EXP_STAT(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + ADF_RING_CSR_EXP_STAT)
#define READ_CSR_EXP_INT_EN(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + ADF_RING_CSR_EXP_INT_EN)
#define WRITE_CSR_EXP_INT_EN(csr_base_addr, bank, value) \
	ADF_CSR_WR((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + \
		   ADF_RING_CSR_EXP_INT_EN, value)
#define READ_CSR_RING_CONFIG(csr_base_addr, bank, ring) \
	ADF_CSR_RD((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + \
		   ADF_RING_CSR_RING_CONFIG + ((ring) << 2))
#define WRITE_CSR_RING_CONFIG(csr_base_addr, bank, ring, value) \
	ADF_CSR_WR((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + \
		   ADF_RING_CSR_RING_CONFIG + ((ring) << 2), value)
#define WRITE_CSR_RING_BASE(csr_base_addr, bank, ring, value)	\
do { \
	void __iomem *_csr_base_addr = csr_base_addr; \
	u32 _bank = bank;						\
	u32 _ring = ring;						\
	dma_addr_t _value = value;					\
	u32 l_base = 0, u_base = 0;					\
	l_base = lower_32_bits(_value);					\
	u_base = upper_32_bits(_value);					\
	ADF_CSR_WR((_csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET,		\
		   ADF_RING_BUNDLE_SIZE * (_bank) +			\
		   ADF_RING_CSR_RING_LBASE + ((_ring) << 2), l_base);	\
	ADF_CSR_WR((_csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET,		\
		   ADF_RING_BUNDLE_SIZE * (_bank) +			\
		   ADF_RING_CSR_RING_UBASE + ((_ring) << 2), u_base);	\
} while (0)

static inline u64 read_base(void __iomem *csr_base_addr, u32 bank, u32 ring)
{
	u32 l_base, u_base;

	/*
	 * Use special IO wrapper for ring base as LBASE and UBASE are
	 * not physically contigious
	 */
	l_base = ADF_CSR_RD(csr_base_addr, (ADF_RING_BUNDLE_SIZE * bank) +
			    ADF_RING_CSR_RING_LBASE + (ring << 2));
	u_base = ADF_CSR_RD(csr_base_addr, (ADF_RING_BUNDLE_SIZE * bank) +
			    ADF_RING_CSR_RING_UBASE + (ring << 2));

	return (u64)u_base << 32 | (u64)l_base;
}

#define READ_CSR_RING_BASE(csr_base_addr, bank, ring) \
	read_base((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, (bank), (ring))

#define WRITE_CSR_RING_HEAD(csr_base_addr, bank, ring, value) \
	ADF_CSR_WR((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + \
		   ADF_RING_CSR_RING_HEAD + ((ring) << 2), value)
#define WRITE_CSR_RING_TAIL(csr_base_addr, bank, ring, value) \
	ADF_CSR_WR((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + \
		   ADF_RING_CSR_RING_TAIL + ((ring) << 2), value)
#define READ_CSR_INT_EN(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + ADF_RING_CSR_INT_FLAG_EN)
#define WRITE_CSR_INT_EN(csr_base_addr, bank, value) \
	ADF_CSR_WR((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + \
		   ADF_RING_CSR_INT_FLAG_EN, (value))
#define READ_CSR_INT_FLAG(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + ADF_RING_CSR_INT_FLAG)
#define WRITE_CSR_INT_FLAG(csr_base_addr, bank, value) \
	ADF_CSR_WR((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + \
		   ADF_RING_CSR_INT_FLAG, (value))
#define READ_CSR_INT_SRCSEL(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + ADF_RING_CSR_INT_SRCSEL)
#define WRITE_CSR_INT_SRCSEL(csr_base_addr, bank) \
	ADF_CSR_WR((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + \
		   ADF_RING_CSR_INT_SRCSEL, ADF_BANK_INT_SRC_SEL_MASK)
#define WRITE_CSR_INT_SRCSEL_W_VAL(csr_base_addr, bank, value) \
	ADF_CSR_WR((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + \
		   ADF_RING_CSR_INT_SRCSEL, (value))
#define READ_CSR_INT_COL_EN(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + ADF_RING_CSR_INT_COL_EN)
#define WRITE_CSR_INT_COL_EN(csr_base_addr, bank, value) \
	ADF_CSR_WR((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + \
		   ADF_RING_CSR_INT_COL_EN, (value))
#define READ_CSR_INT_COL_CTL(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + ADF_RING_CSR_INT_COL_CTL)
#define WRITE_CSR_INT_COL_CTL(csr_base_addr, bank, value) \
	ADF_CSR_WR((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + \
		   ADF_RING_CSR_INT_COL_CTL, \
		   ADF_RING_CSR_INT_COL_CTL_ENABLE | (value))
#define READ_CSR_INT_FLAG_AND_COL(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + \
		   ADF_RING_CSR_INT_FLAG_AND_COL)
#define WRITE_CSR_INT_FLAG_AND_COL(csr_base_addr, bank, value) \
	ADF_CSR_WR((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + \
		   ADF_RING_CSR_INT_FLAG_AND_COL, (value))

#define READ_CSR_RING_SRV_ARB_EN(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + \
		   ADF_RING_CSR_RING_SRV_ARB_EN)
#define WRITE_CSR_RING_SRV_ARB_EN(csr_base_addr, bank, value) \
	ADF_CSR_WR((csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET, \
		   ADF_RING_BUNDLE_SIZE * (bank) + \
		   ADF_RING_CSR_RING_SRV_ARB_EN, (value))

void adf_gen4_init_hw_csr_ops(struct adf_hw_csr_ops *csr_ops);

#endif
