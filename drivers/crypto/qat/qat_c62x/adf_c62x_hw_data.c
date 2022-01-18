// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2020 Intel Corporation */
#include <adf_accel_devices.h>
#include <adf_common_drv.h>
#include <adf_pf2vf_msg.h>
#include <adf_gen2_hw_data.h>
#include "adf_c62x_hw_data.h"
#include "icp_qat_hw.h"

/* Worker thread to service arbiter mappings */
static const u32 thrd_to_arb_map[ADF_C62X_MAX_ACCELENGINES] = {
	0x12222AAA, 0x11222AAA, 0x12222AAA, 0x11222AAA, 0x12222AAA,
	0x11222AAA, 0x12222AAA, 0x11222AAA, 0x12222AAA, 0x11222AAA
};

static struct adf_hw_device_class c62x_class = {
	.name = ADF_C62X_DEVICE_NAME,
	.type = DEV_C62X,
	.instances = 0
};

static u32 get_accel_mask(struct adf_hw_device_data *self)
{
	u32 straps = self->straps;
	u32 fuses = self->fuses;
	u32 accel;

	accel = ~(fuses | straps) >> ADF_C62X_ACCELERATORS_REG_OFFSET;
	accel &= ADF_C62X_ACCELERATORS_MASK;

	return accel;
}

static u32 get_ae_mask(struct adf_hw_device_data *self)
{
	u32 straps = self->straps;
	u32 fuses = self->fuses;
	unsigned long disabled;
	u32 ae_disable;
	int accel;

	/* If an accel is disabled, then disable the corresponding two AEs */
	disabled = ~get_accel_mask(self) & ADF_C62X_ACCELERATORS_MASK;
	ae_disable = BIT(1) | BIT(0);
	for_each_set_bit(accel, &disabled, ADF_C62X_MAX_ACCELERATORS)
		straps |= ae_disable << (accel << 1);

	return ~(fuses | straps) & ADF_C62X_ACCELENGINES_MASK;
}

static u32 get_misc_bar_id(struct adf_hw_device_data *self)
{
	return ADF_C62X_PMISC_BAR;
}

static u32 get_etr_bar_id(struct adf_hw_device_data *self)
{
	return ADF_C62X_ETR_BAR;
}

static u32 get_sram_bar_id(struct adf_hw_device_data *self)
{
	return ADF_C62X_SRAM_BAR;
}

static enum dev_sku_info get_sku(struct adf_hw_device_data *self)
{
	int aes = self->get_num_aes(self);

	if (aes == 8)
		return DEV_SKU_2;
	else if (aes == 10)
		return DEV_SKU_4;

	return DEV_SKU_UNKNOWN;
}

static const u32 *adf_get_arbiter_mapping(void)
{
	return thrd_to_arb_map;
}

static void adf_enable_ints(struct adf_accel_dev *accel_dev)
{
	void __iomem *addr;

	addr = (&GET_BARS(accel_dev)[ADF_C62X_PMISC_BAR])->virt_addr;

	/* Enable bundle and misc interrupts */
	ADF_CSR_WR(addr, ADF_C62X_SMIAPF0_MASK_OFFSET,
		   ADF_C62X_SMIA0_MASK);
	ADF_CSR_WR(addr, ADF_C62X_SMIAPF1_MASK_OFFSET,
		   ADF_C62X_SMIA1_MASK);
}

static void configure_iov_threads(struct adf_accel_dev *accel_dev, bool enable)
{
	adf_gen2_cfg_iov_thds(accel_dev, enable,
			      ADF_C62X_AE2FUNC_MAP_GRP_A_NUM_REGS,
			      ADF_C62X_AE2FUNC_MAP_GRP_B_NUM_REGS);
}

void adf_init_hw_data_c62x(struct adf_hw_device_data *hw_data)
{
	hw_data->dev_class = &c62x_class;
	hw_data->instance_id = c62x_class.instances++;
	hw_data->num_banks = ADF_C62X_ETR_MAX_BANKS;
	hw_data->num_rings_per_bank = ADF_ETR_MAX_RINGS_PER_BANK;
	hw_data->num_accel = ADF_C62X_MAX_ACCELERATORS;
	hw_data->num_logical_accel = 1;
	hw_data->num_engines = ADF_C62X_MAX_ACCELENGINES;
	hw_data->tx_rx_gap = ADF_GEN2_RX_RINGS_OFFSET;
	hw_data->tx_rings_mask = ADF_GEN2_TX_RINGS_MASK;
	hw_data->alloc_irq = adf_isr_resource_alloc;
	hw_data->free_irq = adf_isr_resource_free;
	hw_data->enable_error_correction = adf_gen2_enable_error_correction;
	hw_data->get_accel_mask = get_accel_mask;
	hw_data->get_ae_mask = get_ae_mask;
	hw_data->get_accel_cap = adf_gen2_get_accel_cap;
	hw_data->get_num_accels = adf_gen2_get_num_accels;
	hw_data->get_num_aes = adf_gen2_get_num_aes;
	hw_data->get_sram_bar_id = get_sram_bar_id;
	hw_data->get_etr_bar_id = get_etr_bar_id;
	hw_data->get_misc_bar_id = get_misc_bar_id;
	hw_data->get_admin_info = adf_gen2_get_admin_info;
	hw_data->get_arb_info = adf_gen2_get_arb_info;
	hw_data->get_sku = get_sku;
	hw_data->fw_name = ADF_C62X_FW;
	hw_data->fw_mmp_name = ADF_C62X_MMP;
	hw_data->init_admin_comms = adf_init_admin_comms;
	hw_data->exit_admin_comms = adf_exit_admin_comms;
	hw_data->configure_iov_threads = configure_iov_threads;
	hw_data->send_admin_init = adf_send_admin_init;
	hw_data->init_arb = adf_init_arb;
	hw_data->exit_arb = adf_exit_arb;
	hw_data->get_arb_mapping = adf_get_arbiter_mapping;
	hw_data->enable_ints = adf_enable_ints;
	hw_data->reset_device = adf_reset_flr;
	hw_data->set_ssm_wdtimer = adf_gen2_set_ssm_wdtimer;
	hw_data->get_pf2vf_offset = adf_gen2_get_pf2vf_offset;
	hw_data->get_vf2pf_sources = adf_gen2_get_vf2pf_sources;
	hw_data->enable_vf2pf_interrupts = adf_gen2_enable_vf2pf_interrupts;
	hw_data->disable_vf2pf_interrupts = adf_gen2_disable_vf2pf_interrupts;
	hw_data->enable_pfvf_comms = adf_enable_pf2vf_comms;
	hw_data->disable_iov = adf_disable_sriov;
	hw_data->min_iov_compat_ver = ADF_PFVF_COMPAT_THIS_VERSION;

	adf_gen2_init_hw_csr_ops(&hw_data->csr_ops);
}

void adf_clean_hw_data_c62x(struct adf_hw_device_data *hw_data)
{
	hw_data->dev_class->instances--;
}
