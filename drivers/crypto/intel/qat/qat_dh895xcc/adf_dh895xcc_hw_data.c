// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */
#include <adf_accel_devices.h>
#include <adf_admin.h>
#include <adf_common_drv.h>
#include <adf_gen2_config.h>
#include <adf_gen2_dc.h>
#include <adf_gen2_hw_csr_data.h>
#include <adf_gen2_hw_data.h>
#include <adf_gen2_pfvf.h>
#include "adf_dh895xcc_hw_data.h"
#include "adf_heartbeat.h"
#include "icp_qat_hw.h"

#define ADF_DH895XCC_VF_MSK	0xFFFFFFFF

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
	u32 fuses = self->fuses[ADF_FUSECTL0];

	return ~fuses >> ADF_DH895XCC_ACCELERATORS_REG_OFFSET &
			 ADF_DH895XCC_ACCELERATORS_MASK;
}

static u32 get_ae_mask(struct adf_hw_device_data *self)
{
	u32 fuses = self->fuses[ADF_FUSECTL0];

	return ~fuses & ADF_DH895XCC_ACCELENGINES_MASK;
}

static u32 get_misc_bar_id(struct adf_hw_device_data *self)
{
	return ADF_DH895XCC_PMISC_BAR;
}

static u32 get_ts_clock(struct adf_hw_device_data *self)
{
	/*
	 * Timestamp update interval is 16 AE clock ticks for dh895xcc.
	 */
	return self->clock_frequency / 16;
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
		       ICP_ACCEL_CAPABILITIES_CIPHER |
		       ICP_ACCEL_CAPABILITIES_COMPRESSION;

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
	int sku = (self->fuses[ADF_FUSECTL0] & ADF_DH895XCC_FUSECTL_SKU_MASK)
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

static const u32 *adf_get_arbiter_mapping(struct adf_accel_dev *accel_dev)
{
	return thrd_to_arb_map;
}

static void enable_vf2pf_interrupts(void __iomem *pmisc_addr, u32 vf_mask)
{
	/* Enable VF2PF Messaging Ints - VFs 0 through 15 per vf_mask[15:0] */
	if (vf_mask & 0xFFFF) {
		u32 val = ADF_CSR_RD(pmisc_addr, ADF_GEN2_ERRMSK3)
			  & ~ADF_DH895XCC_ERR_MSK_VF2PF_L(vf_mask);
		ADF_CSR_WR(pmisc_addr, ADF_GEN2_ERRMSK3, val);
	}

	/* Enable VF2PF Messaging Ints - VFs 16 through 31 per vf_mask[31:16] */
	if (vf_mask >> 16) {
		u32 val = ADF_CSR_RD(pmisc_addr, ADF_GEN2_ERRMSK5)
			  & ~ADF_DH895XCC_ERR_MSK_VF2PF_U(vf_mask);
		ADF_CSR_WR(pmisc_addr, ADF_GEN2_ERRMSK5, val);
	}
}

static void disable_all_vf2pf_interrupts(void __iomem *pmisc_addr)
{
	u32 val;

	/* Disable VF2PF interrupts for VFs 0 through 15 per vf_mask[15:0] */
	val = ADF_CSR_RD(pmisc_addr, ADF_GEN2_ERRMSK3)
	      | ADF_DH895XCC_ERR_MSK_VF2PF_L(ADF_DH895XCC_VF_MSK);
	ADF_CSR_WR(pmisc_addr, ADF_GEN2_ERRMSK3, val);

	/* Disable VF2PF interrupts for VFs 16 through 31 per vf_mask[31:16] */
	val = ADF_CSR_RD(pmisc_addr, ADF_GEN2_ERRMSK5)
	      | ADF_DH895XCC_ERR_MSK_VF2PF_U(ADF_DH895XCC_VF_MSK);
	ADF_CSR_WR(pmisc_addr, ADF_GEN2_ERRMSK5, val);
}

static u32 disable_pending_vf2pf_interrupts(void __iomem *pmisc_addr)
{
	u32 sources, pending, disabled;
	u32 errsou3, errmsk3;
	u32 errsou5, errmsk5;

	/* Get the interrupt sources triggered by VFs */
	errsou3 = ADF_CSR_RD(pmisc_addr, ADF_GEN2_ERRSOU3);
	errsou5 = ADF_CSR_RD(pmisc_addr, ADF_GEN2_ERRSOU5);
	sources = ADF_DH895XCC_ERR_REG_VF2PF_L(errsou3)
		  | ADF_DH895XCC_ERR_REG_VF2PF_U(errsou5);

	if (!sources)
		return 0;

	/* Get the already disabled interrupts */
	errmsk3 = ADF_CSR_RD(pmisc_addr, ADF_GEN2_ERRMSK3);
	errmsk5 = ADF_CSR_RD(pmisc_addr, ADF_GEN2_ERRMSK5);
	disabled = ADF_DH895XCC_ERR_REG_VF2PF_L(errmsk3)
		   | ADF_DH895XCC_ERR_REG_VF2PF_U(errmsk5);

	pending = sources & ~disabled;
	if (!pending)
		return 0;

	/* Due to HW limitations, when disabling the interrupts, we can't
	 * just disable the requested sources, as this would lead to missed
	 * interrupts if sources changes just before writing to ERRMSK3 and
	 * ERRMSK5.
	 * To work around it, disable all and re-enable only the sources that
	 * are not in vf_mask and were not already disabled. Re-enabling will
	 * trigger a new interrupt for the sources that have changed in the
	 * meantime, if any.
	 */
	errmsk3 |= ADF_DH895XCC_ERR_MSK_VF2PF_L(ADF_DH895XCC_VF_MSK);
	errmsk5 |= ADF_DH895XCC_ERR_MSK_VF2PF_U(ADF_DH895XCC_VF_MSK);
	ADF_CSR_WR(pmisc_addr, ADF_GEN2_ERRMSK3, errmsk3);
	ADF_CSR_WR(pmisc_addr, ADF_GEN2_ERRMSK5, errmsk5);

	/* Update only section of errmsk3 and errmsk5 related to VF2PF */
	errmsk3 &= ~ADF_DH895XCC_ERR_MSK_VF2PF_L(ADF_DH895XCC_VF_MSK);
	errmsk5 &= ~ADF_DH895XCC_ERR_MSK_VF2PF_U(ADF_DH895XCC_VF_MSK);

	errmsk3 |= ADF_DH895XCC_ERR_MSK_VF2PF_L(sources | disabled);
	errmsk5 |= ADF_DH895XCC_ERR_MSK_VF2PF_U(sources | disabled);
	ADF_CSR_WR(pmisc_addr, ADF_GEN2_ERRMSK3, errmsk3);
	ADF_CSR_WR(pmisc_addr, ADF_GEN2_ERRMSK5, errmsk5);

	/* Return the sources of the (new) interrupt(s) */
	return pending;
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
	hw_data->tx_rx_gap = ADF_GEN2_RX_RINGS_OFFSET;
	hw_data->tx_rings_mask = ADF_GEN2_TX_RINGS_MASK;
	hw_data->ring_to_svc_map = ADF_GEN2_DEFAULT_RING_TO_SRV_MAP;
	hw_data->alloc_irq = adf_isr_resource_alloc;
	hw_data->free_irq = adf_isr_resource_free;
	hw_data->enable_error_correction = adf_gen2_enable_error_correction;
	hw_data->get_accel_mask = get_accel_mask;
	hw_data->get_ae_mask = get_ae_mask;
	hw_data->get_accel_cap = get_accel_cap;
	hw_data->get_num_accels = adf_gen2_get_num_accels;
	hw_data->get_num_aes = adf_gen2_get_num_aes;
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
	hw_data->enable_ints = adf_gen2_enable_ints;
	hw_data->reset_device = adf_reset_sbr;
	hw_data->disable_iov = adf_disable_sriov;
	hw_data->dev_config = adf_gen2_dev_config;
	hw_data->clock_frequency = ADF_DH895X_AE_FREQ;
	hw_data->get_hb_clock = get_ts_clock;
	hw_data->num_hb_ctrs = ADF_NUM_HB_CNT_PER_AE;
	hw_data->check_hb_ctrs = adf_heartbeat_check_ctrs;

	adf_gen2_init_pf_pfvf_ops(&hw_data->pfvf_ops);
	hw_data->pfvf_ops.enable_vf2pf_interrupts = enable_vf2pf_interrupts;
	hw_data->pfvf_ops.disable_all_vf2pf_interrupts = disable_all_vf2pf_interrupts;
	hw_data->pfvf_ops.disable_pending_vf2pf_interrupts = disable_pending_vf2pf_interrupts;
	adf_gen2_init_hw_csr_ops(&hw_data->csr_ops);
	adf_gen2_init_dc_ops(&hw_data->dc_ops);
}

void adf_clean_hw_data_dh895xcc(struct adf_hw_device_data *hw_data)
{
	hw_data->dev_class->instances--;
}
