// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */
#include <adf_accel_devices.h>
#include <adf_admin.h>
#include <adf_clock.h>
#include <adf_common_drv.h>
#include <adf_gen2_config.h>
#include <adf_gen2_hw_csr_data.h>
#include <adf_gen2_hw_data.h>
#include <adf_gen2_pfvf.h>
#include "adf_c3xxx_hw_data.h"
#include "adf_heartbeat.h"
#include "icp_qat_hw.h"

/* Worker thread to service arbiter mappings */
static const u32 thrd_to_arb_map[ADF_C3XXX_MAX_ACCELENGINES] = {
	0x12222AAA, 0x11222AAA, 0x12222AAA,
	0x11222AAA, 0x12222AAA, 0x11222AAA
};

static struct adf_hw_device_class c3xxx_class = {
	.name = ADF_C3XXX_DEVICE_NAME,
	.type = DEV_C3XXX,
};

static u32 get_accel_mask(struct adf_hw_device_data *self)
{
	u32 fuses = self->fuses[ADF_FUSECTL0];
	u32 straps = self->straps;
	u32 accel;

	accel = ~(fuses | straps) >> ADF_C3XXX_ACCELERATORS_REG_OFFSET;
	accel &= ADF_C3XXX_ACCELERATORS_MASK;

	return accel;
}

static u32 get_ae_mask(struct adf_hw_device_data *self)
{
	u32 fuses = self->fuses[ADF_FUSECTL0];
	u32 straps = self->straps;
	unsigned long disabled;
	u32 ae_disable;
	int accel;

	/* If an accel is disabled, then disable the corresponding two AEs */
	disabled = ~get_accel_mask(self) & ADF_C3XXX_ACCELERATORS_MASK;
	ae_disable = BIT(1) | BIT(0);
	for_each_set_bit(accel, &disabled, ADF_C3XXX_MAX_ACCELERATORS)
		straps |= ae_disable << (accel << 1);

	return ~(fuses | straps) & ADF_C3XXX_ACCELENGINES_MASK;
}

static u32 get_ts_clock(struct adf_hw_device_data *self)
{
	/*
	 * Timestamp update interval is 16 AE clock ticks for c3xxx.
	 */
	return self->clock_frequency / 16;
}

static int measure_clock(struct adf_accel_dev *accel_dev)
{
	u32 frequency;
	int ret;

	ret = adf_dev_measure_clock(accel_dev, &frequency, ADF_C3XXX_MIN_AE_FREQ,
				    ADF_C3XXX_MAX_AE_FREQ);
	if (ret)
		return ret;

	accel_dev->hw_device->clock_frequency = frequency;
	return 0;
}

static u32 get_misc_bar_id(struct adf_hw_device_data *self)
{
	return ADF_C3XXX_PMISC_BAR;
}

static u32 get_etr_bar_id(struct adf_hw_device_data *self)
{
	return ADF_C3XXX_ETR_BAR;
}

static u32 get_sram_bar_id(struct adf_hw_device_data *self)
{
	return ADF_C3XXX_SRAM_BAR;
}

static enum dev_sku_info get_sku(struct adf_hw_device_data *self)
{
	int aes = self->get_num_aes(self);

	if (aes == 6)
		return DEV_SKU_4;

	return DEV_SKU_UNKNOWN;
}

static const u32 *adf_get_arbiter_mapping(struct adf_accel_dev *accel_dev)
{
	return thrd_to_arb_map;
}

static void configure_iov_threads(struct adf_accel_dev *accel_dev, bool enable)
{
	adf_gen2_cfg_iov_thds(accel_dev, enable,
			      ADF_C3XXX_AE2FUNC_MAP_GRP_A_NUM_REGS,
			      ADF_C3XXX_AE2FUNC_MAP_GRP_B_NUM_REGS);
}

void adf_init_hw_data_c3xxx(struct adf_hw_device_data *hw_data)
{
	hw_data->dev_class = &c3xxx_class;
	hw_data->instance_id = c3xxx_class.instances++;
	hw_data->num_banks = ADF_C3XXX_ETR_MAX_BANKS;
	hw_data->num_rings_per_bank = ADF_ETR_MAX_RINGS_PER_BANK;
	hw_data->num_accel = ADF_C3XXX_MAX_ACCELERATORS;
	hw_data->num_logical_accel = 1;
	hw_data->num_engines = ADF_C3XXX_MAX_ACCELENGINES;
	hw_data->tx_rx_gap = ADF_GEN2_RX_RINGS_OFFSET;
	hw_data->tx_rings_mask = ADF_GEN2_TX_RINGS_MASK;
	hw_data->ring_to_svc_map = ADF_GEN2_DEFAULT_RING_TO_SRV_MAP;
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
	hw_data->fw_name = ADF_C3XXX_FW;
	hw_data->fw_mmp_name = ADF_C3XXX_MMP;
	hw_data->init_admin_comms = adf_init_admin_comms;
	hw_data->exit_admin_comms = adf_exit_admin_comms;
	hw_data->configure_iov_threads = configure_iov_threads;
	hw_data->send_admin_init = adf_send_admin_init;
	hw_data->init_arb = adf_init_arb;
	hw_data->exit_arb = adf_exit_arb;
	hw_data->get_arb_mapping = adf_get_arbiter_mapping;
	hw_data->enable_ints = adf_gen2_enable_ints;
	hw_data->reset_device = adf_reset_flr;
	hw_data->set_ssm_wdtimer = adf_gen2_set_ssm_wdtimer;
	hw_data->disable_iov = adf_disable_sriov;
	hw_data->dev_config = adf_gen2_dev_config;
	hw_data->measure_clock = measure_clock;
	hw_data->get_hb_clock = get_ts_clock;
	hw_data->num_hb_ctrs = ADF_NUM_HB_CNT_PER_AE;
	hw_data->check_hb_ctrs = adf_heartbeat_check_ctrs;

	adf_gen2_init_pf_pfvf_ops(&hw_data->pfvf_ops);
	adf_gen2_init_hw_csr_ops(&hw_data->csr_ops);
	adf_gen2_init_dc_ops(&hw_data->dc_ops);
}

void adf_clean_hw_data_c3xxx(struct adf_hw_device_data *hw_data)
{
	hw_data->dev_class->instances--;
}
