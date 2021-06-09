// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2020 Intel Corporation */
#include "adf_gen2_hw_data.h"
#include "icp_qat_hw.h"
#include <linux/pci.h>

void adf_gen2_cfg_iov_thds(struct adf_accel_dev *accel_dev, bool enable,
			   int num_a_regs, int num_b_regs)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	void __iomem *pmisc_addr;
	struct adf_bar *pmisc;
	int pmisc_id, i;
	u32 reg;

	pmisc_id = hw_data->get_misc_bar_id(hw_data);
	pmisc = &GET_BARS(accel_dev)[pmisc_id];
	pmisc_addr = pmisc->virt_addr;

	/* Set/Unset Valid bit in AE Thread to PCIe Function Mapping Group A */
	for (i = 0; i < num_a_regs; i++) {
		reg = READ_CSR_AE2FUNCTION_MAP_A(pmisc_addr, i);
		if (enable)
			reg |= AE2FUNCTION_MAP_VALID;
		else
			reg &= ~AE2FUNCTION_MAP_VALID;
		WRITE_CSR_AE2FUNCTION_MAP_A(pmisc_addr, i, reg);
	}

	/* Set/Unset Valid bit in AE Thread to PCIe Function Mapping Group B */
	for (i = 0; i < num_b_regs; i++) {
		reg = READ_CSR_AE2FUNCTION_MAP_B(pmisc_addr, i);
		if (enable)
			reg |= AE2FUNCTION_MAP_VALID;
		else
			reg &= ~AE2FUNCTION_MAP_VALID;
		WRITE_CSR_AE2FUNCTION_MAP_B(pmisc_addr, i, reg);
	}
}
EXPORT_SYMBOL_GPL(adf_gen2_cfg_iov_thds);

void adf_gen2_get_admin_info(struct admin_info *admin_csrs_info)
{
	admin_csrs_info->mailbox_offset = ADF_MAILBOX_BASE_OFFSET;
	admin_csrs_info->admin_msg_ur = ADF_ADMINMSGUR_OFFSET;
	admin_csrs_info->admin_msg_lr = ADF_ADMINMSGLR_OFFSET;
}
EXPORT_SYMBOL_GPL(adf_gen2_get_admin_info);

void adf_gen2_get_arb_info(struct arb_info *arb_info)
{
	arb_info->arb_cfg = ADF_ARB_CONFIG;
	arb_info->arb_offset = ADF_ARB_OFFSET;
	arb_info->wt2sam_offset = ADF_ARB_WRK_2_SER_MAP_OFFSET;
}
EXPORT_SYMBOL_GPL(adf_gen2_get_arb_info);

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

static u32 read_csr_e_stat(void __iomem *csr_base_addr, u32 bank)
{
	return READ_CSR_E_STAT(csr_base_addr, bank);
}

static void write_csr_ring_config(void __iomem *csr_base_addr, u32 bank,
				  u32 ring, u32 value)
{
	WRITE_CSR_RING_CONFIG(csr_base_addr, bank, ring, value);
}

static void write_csr_ring_base(void __iomem *csr_base_addr, u32 bank, u32 ring,
				dma_addr_t addr)
{
	WRITE_CSR_RING_BASE(csr_base_addr, bank, ring, addr);
}

static void write_csr_int_flag(void __iomem *csr_base_addr, u32 bank, u32 value)
{
	WRITE_CSR_INT_FLAG(csr_base_addr, bank, value);
}

static void write_csr_int_srcsel(void __iomem *csr_base_addr, u32 bank)
{
	WRITE_CSR_INT_SRCSEL(csr_base_addr, bank);
}

static void write_csr_int_col_en(void __iomem *csr_base_addr, u32 bank,
				 u32 value)
{
	WRITE_CSR_INT_COL_EN(csr_base_addr, bank, value);
}

static void write_csr_int_col_ctl(void __iomem *csr_base_addr, u32 bank,
				  u32 value)
{
	WRITE_CSR_INT_COL_CTL(csr_base_addr, bank, value);
}

static void write_csr_int_flag_and_col(void __iomem *csr_base_addr, u32 bank,
				       u32 value)
{
	WRITE_CSR_INT_FLAG_AND_COL(csr_base_addr, bank, value);
}

static void write_csr_ring_srv_arb_en(void __iomem *csr_base_addr, u32 bank,
				      u32 value)
{
	WRITE_CSR_RING_SRV_ARB_EN(csr_base_addr, bank, value);
}

void adf_gen2_init_hw_csr_ops(struct adf_hw_csr_ops *csr_ops)
{
	csr_ops->build_csr_ring_base_addr = build_csr_ring_base_addr;
	csr_ops->read_csr_ring_head = read_csr_ring_head;
	csr_ops->write_csr_ring_head = write_csr_ring_head;
	csr_ops->read_csr_ring_tail = read_csr_ring_tail;
	csr_ops->write_csr_ring_tail = write_csr_ring_tail;
	csr_ops->read_csr_e_stat = read_csr_e_stat;
	csr_ops->write_csr_ring_config = write_csr_ring_config;
	csr_ops->write_csr_ring_base = write_csr_ring_base;
	csr_ops->write_csr_int_flag = write_csr_int_flag;
	csr_ops->write_csr_int_srcsel = write_csr_int_srcsel;
	csr_ops->write_csr_int_col_en = write_csr_int_col_en;
	csr_ops->write_csr_int_col_ctl = write_csr_int_col_ctl;
	csr_ops->write_csr_int_flag_and_col = write_csr_int_flag_and_col;
	csr_ops->write_csr_ring_srv_arb_en = write_csr_ring_srv_arb_en;
}
EXPORT_SYMBOL_GPL(adf_gen2_init_hw_csr_ops);

u32 adf_gen2_get_accel_cap(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct pci_dev *pdev = accel_dev->accel_pci_dev.pci_dev;
	u32 straps = hw_data->straps;
	u32 fuses = hw_data->fuses;
	u32 legfuses;
	u32 capabilities = ICP_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC |
			   ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC |
			   ICP_ACCEL_CAPABILITIES_AUTHENTICATION;

	/* Read accelerator capabilities mask */
	pci_read_config_dword(pdev, ADF_DEVICE_LEGFUSE_OFFSET, &legfuses);

	if (legfuses & ICP_ACCEL_MASK_CIPHER_SLICE)
		capabilities &= ~ICP_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC;
	if (legfuses & ICP_ACCEL_MASK_PKE_SLICE)
		capabilities &= ~ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC;
	if (legfuses & ICP_ACCEL_MASK_AUTH_SLICE)
		capabilities &= ~ICP_ACCEL_CAPABILITIES_AUTHENTICATION;

	if ((straps | fuses) & ADF_POWERGATE_PKE)
		capabilities &= ~ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC;

	return capabilities;
}
EXPORT_SYMBOL_GPL(adf_gen2_get_accel_cap);

void adf_gen2_set_ssm_wdtimer(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 timer_val_pke = ADF_SSM_WDT_PKE_DEFAULT_VALUE;
	u32 timer_val = ADF_SSM_WDT_DEFAULT_VALUE;
	unsigned long accel_mask = hw_data->accel_mask;
	void __iomem *pmisc_addr;
	struct adf_bar *pmisc;
	int pmisc_id;
	u32 i = 0;

	pmisc_id = hw_data->get_misc_bar_id(hw_data);
	pmisc = &GET_BARS(accel_dev)[pmisc_id];
	pmisc_addr = pmisc->virt_addr;

	/* Configures WDT timers */
	for_each_set_bit(i, &accel_mask, hw_data->num_accel) {
		/* Enable WDT for sym and dc */
		ADF_CSR_WR(pmisc_addr, ADF_SSMWDT(i), timer_val);
		/* Enable WDT for pke */
		ADF_CSR_WR(pmisc_addr, ADF_SSMWDTPKE(i), timer_val_pke);
	}
}
EXPORT_SYMBOL_GPL(adf_gen2_set_ssm_wdtimer);
