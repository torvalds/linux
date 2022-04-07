// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2020 Intel Corporation */
#include <adf_accel_devices.h>
#include <adf_pf2vf_msg.h>
#include <adf_common_drv.h>
#include <adf_gen2_hw_data.h>
#include "adf_dh895xcc_hw_data.h"
#include "icp_qat_hw.h"

/* Worker thread to service arbiter mappings */
static const u32 thrd_to_arb_map[ADF_DH895XCC_MAX_ACCELENGINES] = {
	0x12222AAA, 0x11666666, 0x12222AAA, 0x11666666,
	0x12222AAA, 0x11222222, 0x12222AAA, 0x11222222,
	0x12222AAA, 0x11222222, 0x12222AAA, 0x11222222
};

static struct adf_hw_device_class dh895xcc_class = {
	.name = ADF_DH895XCC_DEVICE_NAME,
	.type = DEV_DH895XCC,
	.instances = 0
};

static u32 get_accel_mask(struct adf_hw_device_data *self)
{
	u32 fuses = self->fuses;

	return ~fuses >> ADF_DH895XCC_ACCELERATORS_REG_OFFSET &
			 ADF_DH895XCC_ACCELERATORS_MASK;
}

static u32 get_ae_mask(struct adf_hw_device_data *self)
{
	u32 fuses = self->fuses;

	return ~fuses & ADF_DH895XCC_ACCELENGINES_MASK;
}

static u32 get_num_accels(struct adf_hw_device_data *self)
{
	u32 i, ctr = 0;

	if (!self || !self->accel_mask)
		return 0;

	for (i = 0; i < ADF_DH895XCC_MAX_ACCELERATORS; i++) {
		if (self->accel_mask & (1 << i))
			ctr++;
	}
	return ctr;
}

static u32 get_num_aes(struct adf_hw_device_data *self)
{
	u32 i, ctr = 0;

	if (!self || !self->ae_mask)
		return 0;

	for (i = 0; i < ADF_DH895XCC_MAX_ACCELENGINES; i++) {
		if (self->ae_mask & (1 << i))
			ctr++;
	}
	return ctr;
}

static u32 get_misc_bar_id(struct adf_hw_device_data *self)
{
	return ADF_DH895XCC_PMISC_BAR;
}

static u32 get_etr_bar_id(struct adf_hw_device_data *self)
{
	return ADF_DH895XCC_ETR_BAR;
}

static u32 get_sram_bar_id(struct adf_hw_device_data *self)
{
	return ADF_DH895XCC_SRAM_BAR;
}

static u32 get_accel_cap(struct adf_accel_dev *accel_dev)
{
	struct pci_dev *pdev = accel_dev->accel_pci_dev.pci_dev;
	u32 capabilities;
	u32 legfuses;

	capabilities = ICP_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC |
		       ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC |
		       ICP_ACCEL_CAPABILITIES_AUTHENTICATION |
		       ICP_ACCEL_CAPABILITIES_CIPHER;

	/* Read accelerator capabilities mask */
	pci_read_config_dword(pdev, ADF_DEVICE_LEGFUSE_OFFSET, &legfuses);

	/* A set bit in legfuses means the feature is OFF in this SKU */
	if (legfuses & ICP_ACCEL_MASK_CIPHER_SLICE) {
		capabilities &= ~ICP_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC;
		capabilities &= ~ICP_ACCEL_CAPABILITIES_CIPHER;
	}
	if (legfuses & ICP_ACCEL_MASK_PKE_SLICE)
		capabilities &= ~ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC;
	if (legfuses & ICP_ACCEL_MASK_AUTH_SLICE) {
		capabilities &= ~ICP_ACCEL_CAPABILITIES_AUTHENTICATION;
		capabilities &= ~ICP_ACCEL_CAPABILITIES_CIPHER;
	}
	if (legfuses & ICP_ACCEL_MASK_COMPRESS_SLICE)
		capabilities &= ~ICP_ACCEL_CAPABILITIES_COMPRESSION;

	return capabilities;
}

static enum dev_sku_info get_sku(struct adf_hw_device_data *self)
{
	int sku = (self->fuses & ADF_DH895XCC_FUSECTL_SKU_MASK)
	    >> ADF_DH895XCC_FUSECTL_SKU_SHIFT;

	switch (sku) {
	case ADF_DH895XCC_FUSECTL_SKU_1:
		return DEV_SKU_1;
	case ADF_DH895XCC_FUSECTL_SKU_2:
		return DEV_SKU_2;
	case ADF_DH895XCC_FUSECTL_SKU_3:
		return DEV_SKU_3;
	case ADF_DH895XCC_FUSECTL_SKU_4:
		return DEV_SKU_4;
	default:
		return DEV_SKU_UNKNOWN;
	}
	return DEV_SKU_UNKNOWN;
}

static const u32 *adf_get_arbiter_mapping(void)
{
	return thrd_to_arb_map;
}

static u32 get_pf2vf_offset(u32 i)
{
	return ADF_DH895XCC_PF2VF_OFFSET(i);
}

static void adf_enable_error_correction(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_device = accel_dev->hw_device;
	struct adf_bar *misc_bar = &GET_BARS(accel_dev)[ADF_DH895XCC_PMISC_BAR];
	unsigned long accel_mask = hw_device->accel_mask;
	unsigned long ae_mask = hw_device->ae_mask;
	void __iomem *csr = misc_bar->virt_addr;
	unsigned int val, i;

	/* Enable Accel Engine error detection & correction */
	for_each_set_bit(i, &ae_mask, GET_MAX_ACCELENGINES(accel_dev)) {
		val = ADF_CSR_RD(csr, ADF_DH895XCC_AE_CTX_ENABLES(i));
		val |= ADF_DH895XCC_ENABLE_AE_ECC_ERR;
		ADF_CSR_WR(csr, ADF_DH895XCC_AE_CTX_ENABLES(i), val);
		val = ADF_CSR_RD(csr, ADF_DH895XCC_AE_MISC_CONTROL(i));
		val |= ADF_DH895XCC_ENABLE_AE_ECC_PARITY_CORR;
		ADF_CSR_WR(csr, ADF_DH895XCC_AE_MISC_CONTROL(i), val);
	}

	/* Enable shared memory error detection & correction */
	for_each_set_bit(i, &accel_mask, ADF_DH895XCC_MAX_ACCELERATORS) {
		val = ADF_CSR_RD(csr, ADF_DH895XCC_UERRSSMSH(i));
		val |= ADF_DH895XCC_ERRSSMSH_EN;
		ADF_CSR_WR(csr, ADF_DH895XCC_UERRSSMSH(i), val);
		val = ADF_CSR_RD(csr, ADF_DH895XCC_CERRSSMSH(i));
		val |= ADF_DH895XCC_ERRSSMSH_EN;
		ADF_CSR_WR(csr, ADF_DH895XCC_CERRSSMSH(i), val);
	}
}

static void adf_enable_ints(struct adf_accel_dev *accel_dev)
{
	void __iomem *addr;

	addr = (&GET_BARS(accel_dev)[ADF_DH895XCC_PMISC_BAR])->virt_addr;

	/* Enable bundle and misc interrupts */
	ADF_CSR_WR(addr, ADF_DH895XCC_SMIAPF0_MASK_OFFSET,
		   accel_dev->pf.vf_info ? 0 :
			BIT_ULL(GET_MAX_BANKS(accel_dev)) - 1);
	ADF_CSR_WR(addr, ADF_DH895XCC_SMIAPF1_MASK_OFFSET,
		   ADF_DH895XCC_SMIA1_MASK);
}

static int adf_enable_pf2vf_comms(struct adf_accel_dev *accel_dev)
{
	spin_lock_init(&accel_dev->pf.vf2pf_ints_lock);

	return 0;
}

static void configure_iov_threads(struct adf_accel_dev *accel_dev, bool enable)
{
	adf_gen2_cfg_iov_thds(accel_dev, enable,
			      ADF_DH895XCC_AE2FUNC_MAP_GRP_A_NUM_REGS,
			      ADF_DH895XCC_AE2FUNC_MAP_GRP_B_NUM_REGS);
}

void adf_init_hw_data_dh895xcc(struct adf_hw_device_data *hw_data)
{
	hw_data->dev_class = &dh895xcc_class;
	hw_data->instance_id = dh895xcc_class.instances++;
	hw_data->num_banks = ADF_DH895XCC_ETR_MAX_BANKS;
	hw_data->num_rings_per_bank = ADF_ETR_MAX_RINGS_PER_BANK;
	hw_data->num_accel = ADF_DH895XCC_MAX_ACCELERATORS;
	hw_data->num_logical_accel = 1;
	hw_data->num_engines = ADF_DH895XCC_MAX_ACCELENGINES;
	hw_data->tx_rx_gap = ADF_DH895XCC_RX_RINGS_OFFSET;
	hw_data->tx_rings_mask = ADF_DH895XCC_TX_RINGS_MASK;
	hw_data->alloc_irq = adf_isr_resource_alloc;
	hw_data->free_irq = adf_isr_resource_free;
	hw_data->enable_error_correction = adf_enable_error_correction;
	hw_data->get_accel_mask = get_accel_mask;
	hw_data->get_ae_mask = get_ae_mask;
	hw_data->get_accel_cap = get_accel_cap;
	hw_data->get_num_accels = get_num_accels;
	hw_data->get_num_aes = get_num_aes;
	hw_data->get_etr_bar_id = get_etr_bar_id;
	hw_data->get_misc_bar_id = get_misc_bar_id;
	hw_data->get_admin_info = adf_gen2_get_admin_info;
	hw_data->get_arb_info = adf_gen2_get_arb_info;
	hw_data->get_sram_bar_id = get_sram_bar_id;
	hw_data->get_sku = get_sku;
	hw_data->fw_name = ADF_DH895XCC_FW;
	hw_data->fw_mmp_name = ADF_DH895XCC_MMP;
	hw_data->init_admin_comms = adf_init_admin_comms;
	hw_data->exit_admin_comms = adf_exit_admin_comms;
	hw_data->configure_iov_threads = configure_iov_threads;
	hw_data->send_admin_init = adf_send_admin_init;
	hw_data->init_arb = adf_init_arb;
	hw_data->exit_arb = adf_exit_arb;
	hw_data->get_arb_mapping = adf_get_arbiter_mapping;
	hw_data->enable_ints = adf_enable_ints;
	hw_data->reset_device = adf_reset_sbr;
	hw_data->get_pf2vf_offset = get_pf2vf_offset;
	hw_data->enable_pfvf_comms = adf_enable_pf2vf_comms;
	hw_data->disable_iov = adf_disable_sriov;
	hw_data->min_iov_compat_ver = ADF_PFVF_COMPAT_THIS_VERSION;

	adf_gen2_init_hw_csr_ops(&hw_data->csr_ops);
}

void adf_clean_hw_data_dh895xcc(struct adf_hw_device_data *hw_data)
{
	hw_data->dev_class->instances--;
}
