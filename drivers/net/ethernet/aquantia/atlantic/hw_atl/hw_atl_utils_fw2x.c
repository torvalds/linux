/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

/* File hw_atl_utils_fw2x.c: Definition of firmware 2.x functions for
 * Atlantic hardware abstraction layer.
 */

#include "../aq_hw.h"
#include "../aq_hw_utils.h"
#include "../aq_pci_func.h"
#include "../aq_ring.h"
#include "../aq_vec.h"
#include "hw_atl_utils.h"
#include "hw_atl_llh.h"

#define HW_ATL_FW2X_MPI_EFUSE_ADDR	0x364
#define HW_ATL_FW2X_MPI_MBOX_ADDR	0x360
#define HW_ATL_FW2X_MPI_RPC_ADDR        0x334

#define HW_ATL_FW2X_MPI_CONTROL_ADDR	0x368
#define HW_ATL_FW2X_MPI_CONTROL2_ADDR	0x36C

#define HW_ATL_FW2X_MPI_STATE_ADDR	0x370
#define HW_ATL_FW2X_MPI_STATE2_ADDR	0x374

static int aq_fw2x_set_link_speed(struct aq_hw_s *self, u32 speed);
static int aq_fw2x_set_state(struct aq_hw_s *self,
			     enum hal_atl_utils_fw_state_e state);

static int aq_fw2x_init(struct aq_hw_s *self)
{
	int err = 0;

	/* check 10 times by 1ms */
	AQ_HW_WAIT_FOR(0U != (self->mbox_addr =
			aq_hw_read_reg(self, HW_ATL_FW2X_MPI_MBOX_ADDR)),
		       1000U, 10U);
	AQ_HW_WAIT_FOR(0U != (self->rpc_addr =
		       aq_hw_read_reg(self, HW_ATL_FW2X_MPI_RPC_ADDR)),
		       1000U, 100U);

	return err;
}

static int aq_fw2x_deinit(struct aq_hw_s *self)
{
	int err = aq_fw2x_set_link_speed(self, 0);

	if (!err)
		err = aq_fw2x_set_state(self, MPI_DEINIT);

	return err;
}

static enum hw_atl_fw2x_rate link_speed_mask_2fw2x_ratemask(u32 speed)
{
	enum hw_atl_fw2x_rate rate = 0;

	if (speed & AQ_NIC_RATE_10G)
		rate |= FW2X_RATE_10G;

	if (speed & AQ_NIC_RATE_5G)
		rate |= FW2X_RATE_5G;

	if (speed & AQ_NIC_RATE_5GSR)
		rate |= FW2X_RATE_5G;

	if (speed & AQ_NIC_RATE_2GS)
		rate |= FW2X_RATE_2G5;

	if (speed & AQ_NIC_RATE_1G)
		rate |= FW2X_RATE_1G;

	if (speed & AQ_NIC_RATE_100M)
		rate |= FW2X_RATE_100M;

	return rate;
}

static int aq_fw2x_set_link_speed(struct aq_hw_s *self, u32 speed)
{
	u32 val = link_speed_mask_2fw2x_ratemask(speed);

	aq_hw_write_reg(self, HW_ATL_FW2X_MPI_CONTROL_ADDR, val);

	return 0;
}

static void aq_fw2x_set_mpi_flow_control(struct aq_hw_s *self, u32 *mpi_state)
{
	if (self->aq_nic_cfg->flow_control & AQ_NIC_FC_RX)
		*mpi_state |= BIT(CAPS_HI_PAUSE);
	else
		*mpi_state &= ~BIT(CAPS_HI_PAUSE);

	if (self->aq_nic_cfg->flow_control & AQ_NIC_FC_TX)
		*mpi_state |= BIT(CAPS_HI_ASYMMETRIC_PAUSE);
	else
		*mpi_state &= ~BIT(CAPS_HI_ASYMMETRIC_PAUSE);
}

static int aq_fw2x_set_state(struct aq_hw_s *self,
			     enum hal_atl_utils_fw_state_e state)
{
	u32 mpi_state = aq_hw_read_reg(self, HW_ATL_FW2X_MPI_CONTROL2_ADDR);

	switch (state) {
	case MPI_INIT:
		mpi_state &= ~BIT(CAPS_HI_LINK_DROP);
		aq_fw2x_set_mpi_flow_control(self, &mpi_state);
		break;
	case MPI_DEINIT:
		mpi_state |= BIT(CAPS_HI_LINK_DROP);
		break;
	case MPI_RESET:
	case MPI_POWER:
		/* No actions */
		break;
	}
	aq_hw_write_reg(self, HW_ATL_FW2X_MPI_CONTROL2_ADDR, mpi_state);
	return 0;
}

static int aq_fw2x_update_link_status(struct aq_hw_s *self)
{
	u32 mpi_state = aq_hw_read_reg(self, HW_ATL_FW2X_MPI_STATE_ADDR);
	u32 speed = mpi_state & (FW2X_RATE_100M | FW2X_RATE_1G |
				FW2X_RATE_2G5 | FW2X_RATE_5G | FW2X_RATE_10G);
	struct aq_hw_link_status_s *link_status = &self->aq_link_status;

	if (speed) {
		if (speed & FW2X_RATE_10G)
			link_status->mbps = 10000;
		else if (speed & FW2X_RATE_5G)
			link_status->mbps = 5000;
		else if (speed & FW2X_RATE_2G5)
			link_status->mbps = 2500;
		else if (speed & FW2X_RATE_1G)
			link_status->mbps = 1000;
		else if (speed & FW2X_RATE_100M)
			link_status->mbps = 100;
		else
			link_status->mbps = 10000;
	} else {
		link_status->mbps = 0;
	}

	return 0;
}

static int aq_fw2x_get_mac_permanent(struct aq_hw_s *self, u8 *mac)
{
	int err = 0;
	u32 h = 0U;
	u32 l = 0U;
	u32 mac_addr[2] = { 0 };
	u32 efuse_addr = aq_hw_read_reg(self, HW_ATL_FW2X_MPI_EFUSE_ADDR);

	if (efuse_addr != 0) {
		err = hw_atl_utils_fw_downld_dwords(self,
						    efuse_addr + (40U * 4U),
						    mac_addr,
						    ARRAY_SIZE(mac_addr));
		if (err)
			return err;
		mac_addr[0] = __swab32(mac_addr[0]);
		mac_addr[1] = __swab32(mac_addr[1]);
	}

	ether_addr_copy(mac, (u8 *)mac_addr);

	if ((mac[0] & 0x01U) || ((mac[0] | mac[1] | mac[2]) == 0x00U)) {
		unsigned int rnd = 0;

		get_random_bytes(&rnd, sizeof(unsigned int));

		l = 0xE3000000U
			| (0xFFFFU & rnd)
			| (0x00 << 16);
		h = 0x8001300EU;

		mac[5] = (u8)(0xFFU & l);
		l >>= 8;
		mac[4] = (u8)(0xFFU & l);
		l >>= 8;
		mac[3] = (u8)(0xFFU & l);
		l >>= 8;
		mac[2] = (u8)(0xFFU & l);
		mac[1] = (u8)(0xFFU & h);
		h >>= 8;
		mac[0] = (u8)(0xFFU & h);
	}
	return err;
}

static int aq_fw2x_update_stats(struct aq_hw_s *self)
{
	int err = 0;
	u32 mpi_opts = aq_hw_read_reg(self, HW_ATL_FW2X_MPI_CONTROL2_ADDR);
	u32 orig_stats_val = mpi_opts & BIT(CAPS_HI_STATISTICS);

	/* Toggle statistics bit for FW to update */
	mpi_opts = mpi_opts ^ BIT(CAPS_HI_STATISTICS);
	aq_hw_write_reg(self, HW_ATL_FW2X_MPI_CONTROL2_ADDR, mpi_opts);

	/* Wait FW to report back */
	AQ_HW_WAIT_FOR(orig_stats_val !=
		       (aq_hw_read_reg(self, HW_ATL_FW2X_MPI_STATE2_ADDR) &
				       BIT(CAPS_HI_STATISTICS)),
		       1U, 10000U);
	if (err)
		return err;

	return hw_atl_utils_update_stats(self);
}

static int aq_fw2x_renegotiate(struct aq_hw_s *self)
{
	u32 mpi_opts = aq_hw_read_reg(self, HW_ATL_FW2X_MPI_CONTROL2_ADDR);

	mpi_opts |= BIT(CTRL_FORCE_RECONNECT);

	aq_hw_write_reg(self, HW_ATL_FW2X_MPI_CONTROL2_ADDR, mpi_opts);

	return 0;
}

static int aq_fw2x_set_flow_control(struct aq_hw_s *self)
{
	u32 mpi_state = aq_hw_read_reg(self, HW_ATL_FW2X_MPI_CONTROL2_ADDR);

	aq_fw2x_set_mpi_flow_control(self, &mpi_state);

	aq_hw_write_reg(self, HW_ATL_FW2X_MPI_CONTROL2_ADDR, mpi_state);

	return 0;
}

const struct aq_fw_ops aq_fw_2x_ops = {
	.init = aq_fw2x_init,
	.deinit = aq_fw2x_deinit,
	.reset = NULL,
	.renegotiate = aq_fw2x_renegotiate,
	.get_mac_permanent = aq_fw2x_get_mac_permanent,
	.set_link_speed = aq_fw2x_set_link_speed,
	.set_state = aq_fw2x_set_state,
	.update_link_status = aq_fw2x_update_link_status,
	.update_stats = aq_fw2x_update_stats,
	.set_flow_control   = aq_fw2x_set_flow_control,
};
