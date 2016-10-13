/*
  This file is provided under a dual BSD/GPLv2 license.  When using or
  redistributing this file, you may do so under either license.

  GPL LICENSE SUMMARY
  Copyright(c) 2014 Intel Corporation.
  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  Contact Information:
  qat-linux@intel.com

  BSD LICENSE
  Copyright(c) 2014 Intel Corporation.
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

	* Redistributions of source code must retain the above copyright
	  notice, this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright
	  notice, this list of conditions and the following disclaimer in
	  the documentation and/or other materials provided with the
	  distribution.
	* Neither the name of Intel Corporation nor the names of its
	  contributors may be used to endorse or promote products derived
	  from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include <adf_accel_devices.h>
#include <adf_common_drv.h>
#include <adf_pf2vf_msg.h>
#include "adf_c3xxx_hw_data.h"

/* Worker thread to service arbiter mappings based on dev SKUs */
static const u32 thrd_to_arb_map_6_me_sku[] = {
	0x12222AAA, 0x11222AAA, 0x12222AAA,
	0x11222AAA, 0x12222AAA, 0x11222AAA
};

static struct adf_hw_device_class c3xxx_class = {
	.name = ADF_C3XXX_DEVICE_NAME,
	.type = DEV_C3XXX,
	.instances = 0
};

static u32 get_accel_mask(u32 fuse)
{
	return (~fuse) >> ADF_C3XXX_ACCELERATORS_REG_OFFSET &
		ADF_C3XXX_ACCELERATORS_MASK;
}

static u32 get_ae_mask(u32 fuse)
{
	return (~fuse) & ADF_C3XXX_ACCELENGINES_MASK;
}

static u32 get_num_accels(struct adf_hw_device_data *self)
{
	u32 i, ctr = 0;

	if (!self || !self->accel_mask)
		return 0;

	for (i = 0; i < ADF_C3XXX_MAX_ACCELERATORS; i++) {
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

	for (i = 0; i < ADF_C3XXX_MAX_ACCELENGINES; i++) {
		if (self->ae_mask & (1 << i))
			ctr++;
	}
	return ctr;
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
	return 0;
}

static enum dev_sku_info get_sku(struct adf_hw_device_data *self)
{
	int aes = get_num_aes(self);

	if (aes == 6)
		return DEV_SKU_4;

	return DEV_SKU_UNKNOWN;
}

static void adf_get_arbiter_mapping(struct adf_accel_dev *accel_dev,
				    u32 const **arb_map_config)
{
	switch (accel_dev->accel_pci_dev.sku) {
	case DEV_SKU_4:
		*arb_map_config = thrd_to_arb_map_6_me_sku;
		break;
	default:
		dev_err(&GET_DEV(accel_dev),
			"The configuration doesn't match any SKU");
		*arb_map_config = NULL;
	}
}

static u32 get_pf2vf_offset(u32 i)
{
	return ADF_C3XXX_PF2VF_OFFSET(i);
}

static u32 get_vintmsk_offset(u32 i)
{
	return ADF_C3XXX_VINTMSK_OFFSET(i);
}

static void adf_enable_error_correction(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_device = accel_dev->hw_device;
	struct adf_bar *misc_bar = &GET_BARS(accel_dev)[ADF_C3XXX_PMISC_BAR];
	void __iomem *csr = misc_bar->virt_addr;
	unsigned int val, i;

	/* Enable Accel Engine error detection & correction */
	for (i = 0; i < hw_device->get_num_aes(hw_device); i++) {
		val = ADF_CSR_RD(csr, ADF_C3XXX_AE_CTX_ENABLES(i));
		val |= ADF_C3XXX_ENABLE_AE_ECC_ERR;
		ADF_CSR_WR(csr, ADF_C3XXX_AE_CTX_ENABLES(i), val);
		val = ADF_CSR_RD(csr, ADF_C3XXX_AE_MISC_CONTROL(i));
		val |= ADF_C3XXX_ENABLE_AE_ECC_PARITY_CORR;
		ADF_CSR_WR(csr, ADF_C3XXX_AE_MISC_CONTROL(i), val);
	}

	/* Enable shared memory error detection & correction */
	for (i = 0; i < hw_device->get_num_accels(hw_device); i++) {
		val = ADF_CSR_RD(csr, ADF_C3XXX_UERRSSMSH(i));
		val |= ADF_C3XXX_ERRSSMSH_EN;
		ADF_CSR_WR(csr, ADF_C3XXX_UERRSSMSH(i), val);
		val = ADF_CSR_RD(csr, ADF_C3XXX_CERRSSMSH(i));
		val |= ADF_C3XXX_ERRSSMSH_EN;
		ADF_CSR_WR(csr, ADF_C3XXX_CERRSSMSH(i), val);
	}
}

static void adf_enable_ints(struct adf_accel_dev *accel_dev)
{
	void __iomem *addr;

	addr = (&GET_BARS(accel_dev)[ADF_C3XXX_PMISC_BAR])->virt_addr;

	/* Enable bundle and misc interrupts */
	ADF_CSR_WR(addr, ADF_C3XXX_SMIAPF0_MASK_OFFSET,
		   ADF_C3XXX_SMIA0_MASK);
	ADF_CSR_WR(addr, ADF_C3XXX_SMIAPF1_MASK_OFFSET,
		   ADF_C3XXX_SMIA1_MASK);
}

static int adf_pf_enable_vf2pf_comms(struct adf_accel_dev *accel_dev)
{
	return 0;
}

void adf_init_hw_data_c3xxx(struct adf_hw_device_data *hw_data)
{
	hw_data->dev_class = &c3xxx_class;
	hw_data->instance_id = c3xxx_class.instances++;
	hw_data->num_banks = ADF_C3XXX_ETR_MAX_BANKS;
	hw_data->num_accel = ADF_C3XXX_MAX_ACCELERATORS;
	hw_data->num_logical_accel = 1;
	hw_data->num_engines = ADF_C3XXX_MAX_ACCELENGINES;
	hw_data->tx_rx_gap = ADF_C3XXX_RX_RINGS_OFFSET;
	hw_data->tx_rings_mask = ADF_C3XXX_TX_RINGS_MASK;
	hw_data->alloc_irq = adf_isr_resource_alloc;
	hw_data->free_irq = adf_isr_resource_free;
	hw_data->enable_error_correction = adf_enable_error_correction;
	hw_data->get_accel_mask = get_accel_mask;
	hw_data->get_ae_mask = get_ae_mask;
	hw_data->get_num_accels = get_num_accels;
	hw_data->get_num_aes = get_num_aes;
	hw_data->get_sram_bar_id = get_sram_bar_id;
	hw_data->get_etr_bar_id = get_etr_bar_id;
	hw_data->get_misc_bar_id = get_misc_bar_id;
	hw_data->get_pf2vf_offset = get_pf2vf_offset;
	hw_data->get_vintmsk_offset = get_vintmsk_offset;
	hw_data->get_sku = get_sku;
	hw_data->fw_name = ADF_C3XXX_FW;
	hw_data->fw_mmp_name = ADF_C3XXX_MMP;
	hw_data->init_admin_comms = adf_init_admin_comms;
	hw_data->exit_admin_comms = adf_exit_admin_comms;
	hw_data->disable_iov = adf_disable_sriov;
	hw_data->send_admin_init = adf_send_admin_init;
	hw_data->init_arb = adf_init_arb;
	hw_data->exit_arb = adf_exit_arb;
	hw_data->get_arb_mapping = adf_get_arbiter_mapping;
	hw_data->enable_ints = adf_enable_ints;
	hw_data->enable_vf2pf_comms = adf_pf_enable_vf2pf_comms;
	hw_data->reset_device = adf_reset_flr;
	hw_data->min_iov_compat_ver = ADF_PFVF_COMPATIBILITY_VERSION;
}

void adf_clean_hw_data_c3xxx(struct adf_hw_device_data *hw_data)
{
	hw_data->dev_class->instances--;
}
