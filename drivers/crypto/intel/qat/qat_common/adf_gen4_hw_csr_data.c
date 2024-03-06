// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2024 Intel Corporation */
#include <linux/types.h>
#include "adf_gen4_hw_csr_data.h"

static u64 build_csr_ring_base_addr(dma_addr_t addr, u32 size)
{
	return BUILD_RING_BASE_ADDR(addr, size);
}

static u32 read_csr_ring_head(void __iomem *csr_base_addr, u32 bank, u32 ring)
{
	return READ_CSR_RING_HEAD(csr_base_addr, bank, ring);
}

static void write_csr_ring_head(void __iomem *csr_base_addr, u32 bank, u32 ring,
				u32 value)
{
	WRITE_CSR_RING_HEAD(csr_base_addr, bank, ring, value);
}

static u32 read_csr_ring_tail(void __iomem *csr_base_addr, u32 bank, u32 ring)
{
	return READ_CSR_RING_TAIL(csr_base_addr, bank, ring);
}

static void write_csr_ring_tail(void __iomem *csr_base_addr, u32 bank, u32 ring,
				u32 value)
{
	WRITE_CSR_RING_TAIL(csr_base_addr, bank, ring, value);
}

static u32 read_csr_stat(void __iomem *csr_base_addr, u32 bank)
{
	return READ_CSR_STAT(csr_base_addr, bank);
}

static u32 read_csr_uo_stat(void __iomem *csr_base_addr, u32 bank)
{
	return READ_CSR_UO_STAT(csr_base_addr, bank);
}

static u32 read_csr_e_stat(void __iomem *csr_base_addr, u32 bank)
{
	return READ_CSR_E_STAT(csr_base_addr, bank);
}

static u32 read_csr_ne_stat(void __iomem *csr_base_addr, u32 bank)
{
	return READ_CSR_NE_STAT(csr_base_addr, bank);
}

static u32 read_csr_nf_stat(void __iomem *csr_base_addr, u32 bank)
{
	return READ_CSR_NF_STAT(csr_base_addr, bank);
}

static u32 read_csr_f_stat(void __iomem *csr_base_addr, u32 bank)
{
	return READ_CSR_F_STAT(csr_base_addr, bank);
}

static u32 read_csr_c_stat(void __iomem *csr_base_addr, u32 bank)
{
	return READ_CSR_C_STAT(csr_base_addr, bank);
}

static u32 read_csr_exp_stat(void __iomem *csr_base_addr, u32 bank)
{
	return READ_CSR_EXP_STAT(csr_base_addr, bank);
}

static u32 read_csr_exp_int_en(void __iomem *csr_base_addr, u32 bank)
{
	return READ_CSR_EXP_INT_EN(csr_base_addr, bank);
}

static void write_csr_exp_int_en(void __iomem *csr_base_addr, u32 bank,
				 u32 value)
{
	WRITE_CSR_EXP_INT_EN(csr_base_addr, bank, value);
}

static u32 read_csr_ring_config(void __iomem *csr_base_addr, u32 bank,
				u32 ring)
{
	return READ_CSR_RING_CONFIG(csr_base_addr, bank, ring);
}

static void write_csr_ring_config(void __iomem *csr_base_addr, u32 bank, u32 ring,
				  u32 value)
{
	WRITE_CSR_RING_CONFIG(csr_base_addr, bank, ring, value);
}

static dma_addr_t read_csr_ring_base(void __iomem *csr_base_addr, u32 bank,
				     u32 ring)
{
	return READ_CSR_RING_BASE(csr_base_addr, bank, ring);
}

static void write_csr_ring_base(void __iomem *csr_base_addr, u32 bank, u32 ring,
				dma_addr_t addr)
{
	WRITE_CSR_RING_BASE(csr_base_addr, bank, ring, addr);
}

static u32 read_csr_int_en(void __iomem *csr_base_addr, u32 bank)
{
	return READ_CSR_INT_EN(csr_base_addr, bank);
}

static void write_csr_int_en(void __iomem *csr_base_addr, u32 bank, u32 value)
{
	WRITE_CSR_INT_EN(csr_base_addr, bank, value);
}

static u32 read_csr_int_flag(void __iomem *csr_base_addr, u32 bank)
{
	return READ_CSR_INT_FLAG(csr_base_addr, bank);
}

static void write_csr_int_flag(void __iomem *csr_base_addr, u32 bank,
			       u32 value)
{
	WRITE_CSR_INT_FLAG(csr_base_addr, bank, value);
}

static u32 read_csr_int_srcsel(void __iomem *csr_base_addr, u32 bank)
{
	return READ_CSR_INT_SRCSEL(csr_base_addr, bank);
}

static void write_csr_int_srcsel(void __iomem *csr_base_addr, u32 bank)
{
	WRITE_CSR_INT_SRCSEL(csr_base_addr, bank);
}

static void write_csr_int_srcsel_w_val(void __iomem *csr_base_addr, u32 bank,
				       u32 value)
{
	WRITE_CSR_INT_SRCSEL_W_VAL(csr_base_addr, bank, value);
}

static u32 read_csr_int_col_en(void __iomem *csr_base_addr, u32 bank)
{
	return READ_CSR_INT_COL_EN(csr_base_addr, bank);
}

static void write_csr_int_col_en(void __iomem *csr_base_addr, u32 bank, u32 value)
{
	WRITE_CSR_INT_COL_EN(csr_base_addr, bank, value);
}

static u32 read_csr_int_col_ctl(void __iomem *csr_base_addr, u32 bank)
{
	return READ_CSR_INT_COL_CTL(csr_base_addr, bank);
}

static void write_csr_int_col_ctl(void __iomem *csr_base_addr, u32 bank,
				  u32 value)
{
	WRITE_CSR_INT_COL_CTL(csr_base_addr, bank, value);
}

static u32 read_csr_int_flag_and_col(void __iomem *csr_base_addr, u32 bank)
{
	return READ_CSR_INT_FLAG_AND_COL(csr_base_addr, bank);
}

static void write_csr_int_flag_and_col(void __iomem *csr_base_addr, u32 bank,
				       u32 value)
{
	WRITE_CSR_INT_FLAG_AND_COL(csr_base_addr, bank, value);
}

static u32 read_csr_ring_srv_arb_en(void __iomem *csr_base_addr, u32 bank)
{
	return READ_CSR_RING_SRV_ARB_EN(csr_base_addr, bank);
}

static void write_csr_ring_srv_arb_en(void __iomem *csr_base_addr, u32 bank,
				      u32 value)
{
	WRITE_CSR_RING_SRV_ARB_EN(csr_base_addr, bank, value);
}

static u32 get_int_col_ctl_enable_mask(void)
{
	return ADF_RING_CSR_INT_COL_CTL_ENABLE;
}

void adf_gen4_init_hw_csr_ops(struct adf_hw_csr_ops *csr_ops)
{
	csr_ops->build_csr_ring_base_addr = build_csr_ring_base_addr;
	csr_ops->read_csr_ring_head = read_csr_ring_head;
	csr_ops->write_csr_ring_head = write_csr_ring_head;
	csr_ops->read_csr_ring_tail = read_csr_ring_tail;
	csr_ops->write_csr_ring_tail = write_csr_ring_tail;
	csr_ops->read_csr_stat = read_csr_stat;
	csr_ops->read_csr_uo_stat = read_csr_uo_stat;
	csr_ops->read_csr_e_stat = read_csr_e_stat;
	csr_ops->read_csr_ne_stat = read_csr_ne_stat;
	csr_ops->read_csr_nf_stat = read_csr_nf_stat;
	csr_ops->read_csr_f_stat = read_csr_f_stat;
	csr_ops->read_csr_c_stat = read_csr_c_stat;
	csr_ops->read_csr_exp_stat = read_csr_exp_stat;
	csr_ops->read_csr_exp_int_en = read_csr_exp_int_en;
	csr_ops->write_csr_exp_int_en = write_csr_exp_int_en;
	csr_ops->read_csr_ring_config = read_csr_ring_config;
	csr_ops->write_csr_ring_config = write_csr_ring_config;
	csr_ops->read_csr_ring_base = read_csr_ring_base;
	csr_ops->write_csr_ring_base = write_csr_ring_base;
	csr_ops->read_csr_int_en = read_csr_int_en;
	csr_ops->write_csr_int_en = write_csr_int_en;
	csr_ops->read_csr_int_flag = read_csr_int_flag;
	csr_ops->write_csr_int_flag = write_csr_int_flag;
	csr_ops->read_csr_int_srcsel = read_csr_int_srcsel;
	csr_ops->write_csr_int_srcsel = write_csr_int_srcsel;
	csr_ops->write_csr_int_srcsel_w_val = write_csr_int_srcsel_w_val;
	csr_ops->read_csr_int_col_en = read_csr_int_col_en;
	csr_ops->write_csr_int_col_en = write_csr_int_col_en;
	csr_ops->read_csr_int_col_ctl = read_csr_int_col_ctl;
	csr_ops->write_csr_int_col_ctl = write_csr_int_col_ctl;
	csr_ops->read_csr_int_flag_and_col = read_csr_int_flag_and_col;
	csr_ops->write_csr_int_flag_and_col = write_csr_int_flag_and_col;
	csr_ops->read_csr_ring_srv_arb_en = read_csr_ring_srv_arb_en;
	csr_ops->write_csr_ring_srv_arb_en = write_csr_ring_srv_arb_en;
	csr_ops->get_int_col_ctl_enable_mask = get_int_col_ctl_enable_mask;
}
EXPORT_SYMBOL_GPL(adf_gen4_init_hw_csr_ops);
