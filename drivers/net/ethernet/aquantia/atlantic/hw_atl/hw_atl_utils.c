// SPDX-License-Identifier: GPL-2.0-only
/* Atlantic Network Driver
 *
 * Copyright (C) 2014-2019 aQuantia Corporation
 * Copyright (C) 2019-2020 Marvell International Ltd.
 */

/* File hw_atl_utils.c: Definition of common functions for Atlantic hardware
 * abstraction layer.
 */

#include "../aq_nic.h"
#include "../aq_hw_utils.h"
#include "hw_atl_utils.h"
#include "hw_atl_llh.h"
#include "hw_atl_llh_internal.h"

#include <linux/random.h>

#define HW_ATL_UCP_0X370_REG    0x0370U

#define HW_ATL_MIF_CMD          0x0200U
#define HW_ATL_MIF_ADDR         0x0208U
#define HW_ATL_MIF_VAL          0x020CU

#define HW_ATL_MPI_RPC_ADDR     0x0334U
#define HW_ATL_RPC_CONTROL_ADR  0x0338U
#define HW_ATL_RPC_STATE_ADR    0x033CU

#define HW_ATL_MPI_FW_VERSION	0x18
#define HW_ATL_MPI_CONTROL_ADR  0x0368U
#define HW_ATL_MPI_STATE_ADR    0x036CU

#define HW_ATL_MPI_STATE_MSK      0x00FFU
#define HW_ATL_MPI_STATE_SHIFT    0U
#define HW_ATL_MPI_SPEED_MSK      0x00FF0000U
#define HW_ATL_MPI_SPEED_SHIFT    16U
#define HW_ATL_MPI_DIRTY_WAKE_MSK 0x02000000U

#define HW_ATL_MPI_DAISY_CHAIN_STATUS	0x704
#define HW_ATL_MPI_BOOT_EXIT_CODE	0x388

#define HW_ATL_MAC_PHY_CONTROL	0x4000
#define HW_ATL_MAC_PHY_MPI_RESET_BIT 0x1D

#define HW_ATL_FW_VER_1X 0x01050006U
#define HW_ATL_FW_VER_2X 0x02000000U
#define HW_ATL_FW_VER_3X 0x03000000U

#define FORCE_FLASHLESS 0

enum mcp_area {
	MCP_AREA_CONFIG = 0x80000000,
	MCP_AREA_SETTINGS = 0x20000000,
};

static int hw_atl_utils_mpi_set_state(struct aq_hw_s *self,
				      enum hal_atl_utils_fw_state_e state);
static u32 hw_atl_utils_get_mpi_mbox_tid(struct aq_hw_s *self);
static u32 hw_atl_utils_mpi_get_state(struct aq_hw_s *self);
static u32 hw_atl_utils_mif_cmd_get(struct aq_hw_s *self);
static u32 hw_atl_utils_mif_addr_get(struct aq_hw_s *self);
static u32 hw_atl_utils_rpc_state_get(struct aq_hw_s *self);
static u32 aq_fw1x_rpc_get(struct aq_hw_s *self);

int hw_atl_utils_initfw(struct aq_hw_s *self, const struct aq_fw_ops **fw_ops)
{
	int err = 0;

	hw_atl_utils_hw_chip_features_init(self,
					   &self->chip_features);

	self->fw_ver_actual = hw_atl_utils_get_fw_version(self);

	if (hw_atl_utils_ver_match(HW_ATL_FW_VER_1X,
				   self->fw_ver_actual) == 0) {
		*fw_ops = &aq_fw_1x_ops;
	} else if (hw_atl_utils_ver_match(HW_ATL_FW_VER_2X,
					  self->fw_ver_actual) == 0) {
		*fw_ops = &aq_fw_2x_ops;
	} else if (hw_atl_utils_ver_match(HW_ATL_FW_VER_3X,
					  self->fw_ver_actual) == 0) {
		*fw_ops = &aq_fw_2x_ops;
	} else {
		aq_pr_err("Bad FW version detected: %x\n",
			  self->fw_ver_actual);
		return -EOPNOTSUPP;
	}
	self->aq_fw_ops = *fw_ops;
	err = self->aq_fw_ops->init(self);

	return err;
}

static int hw_atl_utils_soft_reset_flb(struct aq_hw_s *self)
{
	u32 gsr, val;
	int k = 0;

	aq_hw_write_reg(self, 0x404, 0x40e1);
	AQ_HW_SLEEP(50);

	/* Cleanup SPI */
	val = aq_hw_read_reg(self, 0x53C);
	aq_hw_write_reg(self, 0x53C, val | 0x10);

	gsr = aq_hw_read_reg(self, HW_ATL_GLB_SOFT_RES_ADR);
	aq_hw_write_reg(self, HW_ATL_GLB_SOFT_RES_ADR, (gsr & 0xBFFF) | 0x8000);

	/* Kickstart MAC */
	aq_hw_write_reg(self, 0x404, 0x80e0);
	aq_hw_write_reg(self, 0x32a8, 0x0);
	aq_hw_write_reg(self, 0x520, 0x1);

	/* Reset SPI again because of possible interrupted SPI burst */
	val = aq_hw_read_reg(self, 0x53C);
	aq_hw_write_reg(self, 0x53C, val | 0x10);
	AQ_HW_SLEEP(10);
	/* Clear SPI reset state */
	aq_hw_write_reg(self, 0x53C, val & ~0x10);

	aq_hw_write_reg(self, 0x404, 0x180e0);

	for (k = 0; k < 1000; k++) {
		u32 flb_status = aq_hw_read_reg(self,
						HW_ATL_MPI_DAISY_CHAIN_STATUS);

		flb_status = flb_status & 0x10;
		if (flb_status)
			break;
		AQ_HW_SLEEP(10);
	}
	if (k == 1000) {
		aq_pr_err("MAC kickstart failed\n");
		return -EIO;
	}

	/* FW reset */
	aq_hw_write_reg(self, 0x404, 0x80e0);
	AQ_HW_SLEEP(50);
	aq_hw_write_reg(self, 0x3a0, 0x1);

	/* Kickstart PHY - skipped */

	/* Global software reset*/
	hw_atl_rx_rx_reg_res_dis_set(self, 0U);
	hw_atl_tx_tx_reg_res_dis_set(self, 0U);
	aq_hw_write_reg_bit(self, HW_ATL_MAC_PHY_CONTROL,
			    BIT(HW_ATL_MAC_PHY_MPI_RESET_BIT),
			    HW_ATL_MAC_PHY_MPI_RESET_BIT, 0x0);
	gsr = aq_hw_read_reg(self, HW_ATL_GLB_SOFT_RES_ADR);
	aq_hw_write_reg(self, HW_ATL_GLB_SOFT_RES_ADR, (gsr & 0xBFFF) | 0x8000);

	for (k = 0; k < 1000; k++) {
		u32 fw_state = aq_hw_read_reg(self, HW_ATL_MPI_FW_VERSION);

		if (fw_state)
			break;
		AQ_HW_SLEEP(10);
	}
	if (k == 1000) {
		aq_pr_err("FW kickstart failed\n");
		return -EIO;
	}
	/* Old FW requires fixed delay after init */
	AQ_HW_SLEEP(15);

	return 0;
}

static int hw_atl_utils_soft_reset_rbl(struct aq_hw_s *self)
{
	u32 gsr, val, rbl_status;
	int k;

	aq_hw_write_reg(self, 0x404, 0x40e1);
	aq_hw_write_reg(self, 0x3a0, 0x1);
	aq_hw_write_reg(self, 0x32a8, 0x0);

	/* Alter RBL status */
	aq_hw_write_reg(self, 0x388, 0xDEAD);

	/* Cleanup SPI */
	val = aq_hw_read_reg(self, 0x53C);
	aq_hw_write_reg(self, 0x53C, val | 0x10);

	/* Global software reset*/
	hw_atl_rx_rx_reg_res_dis_set(self, 0U);
	hw_atl_tx_tx_reg_res_dis_set(self, 0U);
	aq_hw_write_reg_bit(self, HW_ATL_MAC_PHY_CONTROL,
			    BIT(HW_ATL_MAC_PHY_MPI_RESET_BIT),
			    HW_ATL_MAC_PHY_MPI_RESET_BIT, 0x0);
	gsr = aq_hw_read_reg(self, HW_ATL_GLB_SOFT_RES_ADR);
	aq_hw_write_reg(self, HW_ATL_GLB_SOFT_RES_ADR,
			(gsr & 0xFFFFBFFF) | 0x8000);

	if (FORCE_FLASHLESS)
		aq_hw_write_reg(self, 0x534, 0x0);

	aq_hw_write_reg(self, 0x404, 0x40e0);

	/* Wait for RBL boot */
	for (k = 0; k < 1000; k++) {
		rbl_status = aq_hw_read_reg(self, 0x388) & 0xFFFF;
		if (rbl_status && rbl_status != 0xDEAD)
			break;
		AQ_HW_SLEEP(10);
	}
	if (!rbl_status || rbl_status == 0xDEAD) {
		aq_pr_err("RBL Restart failed");
		return -EIO;
	}

	/* Restore NVR */
	if (FORCE_FLASHLESS)
		aq_hw_write_reg(self, 0x534, 0xA0);

	if (rbl_status == 0xF1A7) {
		aq_pr_err("No FW detected. Dynamic FW load not implemented\n");
		return -ENOTSUPP;
	}

	for (k = 0; k < 1000; k++) {
		u32 fw_state = aq_hw_read_reg(self, HW_ATL_MPI_FW_VERSION);

		if (fw_state)
			break;
		AQ_HW_SLEEP(10);
	}
	if (k == 1000) {
		aq_pr_err("FW kickstart failed\n");
		return -EIO;
	}
	/* Old FW requires fixed delay after init */
	AQ_HW_SLEEP(15);

	return 0;
}

int hw_atl_utils_soft_reset(struct aq_hw_s *self)
{
	u32 boot_exit_code = 0;
	u32 val;
	int k;

	for (k = 0; k < 1000; ++k) {
		u32 flb_status = aq_hw_read_reg(self,
						HW_ATL_MPI_DAISY_CHAIN_STATUS);
		boot_exit_code = aq_hw_read_reg(self,
						HW_ATL_MPI_BOOT_EXIT_CODE);
		if (flb_status != 0x06000000 || boot_exit_code != 0)
			break;
	}

	if (k == 1000) {
		aq_pr_err("Neither RBL nor FLB firmware started\n");
		return -EOPNOTSUPP;
	}

	self->rbl_enabled = (boot_exit_code != 0);

	/* FW 1.x may bootup in an invalid POWER state (WOL feature).
	 * We should work around this by forcing its state back to DEINIT
	 */
	if (!hw_atl_utils_ver_match(HW_ATL_FW_VER_1X,
				    aq_hw_read_reg(self,
						   HW_ATL_MPI_FW_VERSION))) {
		int err = 0;

		hw_atl_utils_mpi_set_state(self, MPI_DEINIT);
		err = readx_poll_timeout_atomic(hw_atl_utils_mpi_get_state,
						self, val,
						(val & HW_ATL_MPI_STATE_MSK) ==
						 MPI_DEINIT,
						10, 10000U);
		if (err)
			return err;
	}

	if (self->rbl_enabled)
		return hw_atl_utils_soft_reset_rbl(self);
	else
		return hw_atl_utils_soft_reset_flb(self);
}

int hw_atl_utils_fw_downld_dwords(struct aq_hw_s *self, u32 a,
				  u32 *p, u32 cnt)
{
	int err = 0;
	u32 val;

	err = readx_poll_timeout_atomic(hw_atl_sem_ram_get,
					self, val, val == 1U,
					1U, 10000U);

	if (err < 0) {
		bool is_locked;

		hw_atl_reg_glb_cpu_sem_set(self, 1U, HW_ATL_FW_SM_RAM);
		is_locked = hw_atl_sem_ram_get(self);
		if (!is_locked) {
			err = -ETIME;
			goto err_exit;
		}
	}

	aq_hw_write_reg(self, HW_ATL_MIF_ADDR, a);

	for (++cnt; --cnt && !err;) {
		aq_hw_write_reg(self, HW_ATL_MIF_CMD, 0x00008000U);

		if (ATL_HW_IS_CHIP_FEATURE(self, REVISION_B1))
			err = readx_poll_timeout_atomic(hw_atl_utils_mif_addr_get,
							self, val, val != a,
							1U, 1000U);
		else
			err = readx_poll_timeout_atomic(hw_atl_utils_mif_cmd_get,
							self, val,
							!(val & 0x100),
							1U, 1000U);

		*(p++) = aq_hw_read_reg(self, HW_ATL_MIF_VAL);
		a += 4;
	}

	hw_atl_reg_glb_cpu_sem_set(self, 1U, HW_ATL_FW_SM_RAM);

err_exit:
	return err;
}

static int hw_atl_utils_write_b1_mbox(struct aq_hw_s *self, u32 addr,
				      u32 *p, u32 cnt, enum mcp_area area)
{
	u32 data_offset = 0;
	u32 offset = addr;
	int err = 0;
	u32 val;

	switch (area) {
	case MCP_AREA_CONFIG:
		offset -= self->rpc_addr;
		break;

	case MCP_AREA_SETTINGS:
		offset -= self->settings_addr;
		break;
	}

	offset = offset / sizeof(u32);

	for (; data_offset < cnt; ++data_offset, ++offset) {
		aq_hw_write_reg(self, 0x328, p[data_offset]);
		aq_hw_write_reg(self, 0x32C,
				(area | (0xFFFF & (offset * 4))));
		hw_atl_mcp_up_force_intr_set(self, 1);
		/* 1000 times by 10us = 10ms */
		err = readx_poll_timeout_atomic(hw_atl_scrpad12_get,
						self, val,
						(val & 0xF0000000) !=
						area,
						10U, 10000U);

		if (err < 0)
			break;
	}

	return err;
}

static int hw_atl_utils_write_b0_mbox(struct aq_hw_s *self, u32 addr,
				      u32 *p, u32 cnt)
{
	u32 offset = 0;
	int err = 0;
	u32 val;

	aq_hw_write_reg(self, 0x208, addr);

	for (; offset < cnt; ++offset) {
		aq_hw_write_reg(self, 0x20C, p[offset]);
		aq_hw_write_reg(self, 0x200, 0xC000);

		err = readx_poll_timeout_atomic(hw_atl_utils_mif_cmd_get,
						self, val,
						(val & 0x100) == 0U,
						10U, 10000U);

		if (err < 0)
			break;
	}

	return err;
}

static int hw_atl_utils_fw_upload_dwords(struct aq_hw_s *self, u32 addr, u32 *p,
					 u32 cnt, enum mcp_area area)
{
	int err = 0;
	u32 val;

	err = readx_poll_timeout_atomic(hw_atl_sem_ram_get, self,
					val, val == 1U,
					10U, 100000U);
	if (err < 0)
		goto err_exit;

	if (ATL_HW_IS_CHIP_FEATURE(self, REVISION_B1))
		err = hw_atl_utils_write_b1_mbox(self, addr, p, cnt, area);
	else
		err = hw_atl_utils_write_b0_mbox(self, addr, p, cnt);

	hw_atl_reg_glb_cpu_sem_set(self, 1U, HW_ATL_FW_SM_RAM);

	if (err < 0)
		goto err_exit;

	err = aq_hw_err_from_flags(self);

err_exit:
	return err;
}

int hw_atl_write_fwcfg_dwords(struct aq_hw_s *self, u32 *p, u32 cnt)
{
	return hw_atl_utils_fw_upload_dwords(self, self->rpc_addr, p,
					     cnt, MCP_AREA_CONFIG);
}

int hw_atl_write_fwsettings_dwords(struct aq_hw_s *self, u32 offset, u32 *p,
				   u32 cnt)
{
	return hw_atl_utils_fw_upload_dwords(self, self->settings_addr + offset,
					     p, cnt, MCP_AREA_SETTINGS);
}

int hw_atl_utils_ver_match(u32 ver_expected, u32 ver_actual)
{
	const u32 dw_major_mask = 0xff000000U;
	const u32 dw_minor_mask = 0x00ffffffU;
	int err = 0;

	err = (dw_major_mask & (ver_expected ^ ver_actual)) ? -EOPNOTSUPP : 0;
	if (err < 0)
		goto err_exit;
	err = ((dw_minor_mask & ver_expected) > (dw_minor_mask & ver_actual)) ?
		-EOPNOTSUPP : 0;

err_exit:
	return err;
}

static int hw_atl_utils_init_ucp(struct aq_hw_s *self,
				 const struct aq_hw_caps_s *aq_hw_caps)
{
	int err = 0;

	if (!aq_hw_read_reg(self, 0x370U)) {
		unsigned int rnd = 0U;
		unsigned int ucp_0x370 = 0U;

		get_random_bytes(&rnd, sizeof(unsigned int));

		ucp_0x370 = 0x02020202U | (0xFEFEFEFEU & rnd);
		aq_hw_write_reg(self, HW_ATL_UCP_0X370_REG, ucp_0x370);
	}

	hw_atl_reg_glb_cpu_scratch_scp_set(self, 0x00000000U, 25U);

	/* check 10 times by 1ms */
	err = readx_poll_timeout_atomic(hw_atl_scrpad25_get,
					self, self->mbox_addr,
					self->mbox_addr != 0U,
					1000U, 10000U);
	err = readx_poll_timeout_atomic(aq_fw1x_rpc_get, self,
					self->rpc_addr,
					self->rpc_addr != 0U,
					1000U, 100000U);

	return err;
}

struct aq_hw_atl_utils_fw_rpc_tid_s {
	union {
		u32 val;
		struct {
			u16 tid;
			u16 len;
		};
	};
};

#define hw_atl_utils_fw_rpc_init(_H_) hw_atl_utils_fw_rpc_wait(_H_, NULL)

int hw_atl_utils_fw_rpc_call(struct aq_hw_s *self, unsigned int rpc_size)
{
	struct aq_hw_atl_utils_fw_rpc_tid_s sw;
	int err = 0;

	if (!ATL_HW_IS_CHIP_FEATURE(self, MIPS)) {
		err = -1;
		goto err_exit;
	}
	err = hw_atl_write_fwcfg_dwords(self, (u32 *)(void *)&self->rpc,
					(rpc_size + sizeof(u32) -
					 sizeof(u8)) / sizeof(u32));
	if (err < 0)
		goto err_exit;

	sw.tid = 0xFFFFU & (++self->rpc_tid);
	sw.len = (u16)rpc_size;
	aq_hw_write_reg(self, HW_ATL_RPC_CONTROL_ADR, sw.val);

err_exit:
	return err;
}

int hw_atl_utils_fw_rpc_wait(struct aq_hw_s *self,
			     struct hw_atl_utils_fw_rpc **rpc)
{
	struct aq_hw_atl_utils_fw_rpc_tid_s sw;
	struct aq_hw_atl_utils_fw_rpc_tid_s fw;
	int err = 0;

	do {
		sw.val = aq_hw_read_reg(self, HW_ATL_RPC_CONTROL_ADR);

		self->rpc_tid = sw.tid;

		err = readx_poll_timeout_atomic(hw_atl_utils_rpc_state_get,
						self, fw.val,
						sw.tid == fw.tid,
						1000U, 100000U);
		if (err < 0)
			goto err_exit;

		err = aq_hw_err_from_flags(self);
		if (err < 0)
			goto err_exit;

		if (fw.len == 0xFFFFU) {
			err = hw_atl_utils_fw_rpc_call(self, sw.len);
			if (err < 0)
				goto err_exit;
		}
	} while (sw.tid != fw.tid || 0xFFFFU == fw.len);

	if (rpc) {
		if (fw.len) {
			err =
			hw_atl_utils_fw_downld_dwords(self,
						      self->rpc_addr,
						      (u32 *)(void *)
						      &self->rpc,
						      (fw.len + sizeof(u32) -
						       sizeof(u8)) /
						      sizeof(u32));
			if (err < 0)
				goto err_exit;
		}

		*rpc = &self->rpc;
	}

err_exit:
	return err;
}

static int hw_atl_utils_mpi_create(struct aq_hw_s *self)
{
	int err = 0;

	err = hw_atl_utils_init_ucp(self, self->aq_nic_cfg->aq_hw_caps);
	if (err < 0)
		goto err_exit;

	err = hw_atl_utils_fw_rpc_init(self);
	if (err < 0)
		goto err_exit;

err_exit:
	return err;
}

int hw_atl_utils_mpi_read_mbox(struct aq_hw_s *self,
			       struct hw_atl_utils_mbox_header *pmbox)
{
	return hw_atl_utils_fw_downld_dwords(self,
					     self->mbox_addr,
					     (u32 *)(void *)pmbox,
					     sizeof(*pmbox) / sizeof(u32));
}

void hw_atl_utils_mpi_read_stats(struct aq_hw_s *self,
				 struct hw_atl_utils_mbox *pmbox)
{
	int err = 0;

	err = hw_atl_utils_fw_downld_dwords(self,
					    self->mbox_addr,
					    (u32 *)(void *)pmbox,
					    sizeof(*pmbox) / sizeof(u32));
	if (err < 0)
		goto err_exit;

	if (ATL_HW_IS_CHIP_FEATURE(self, REVISION_A0)) {
		unsigned int mtu = self->aq_nic_cfg ?
					self->aq_nic_cfg->mtu : 1514U;
		pmbox->stats.ubrc = pmbox->stats.uprc * mtu;
		pmbox->stats.ubtc = pmbox->stats.uptc * mtu;
		pmbox->stats.dpc = atomic_read(&self->dpc);
	} else {
		pmbox->stats.dpc = hw_atl_rpb_rx_dma_drop_pkt_cnt_get(self);
	}

err_exit:;
}

static int hw_atl_utils_mpi_set_speed(struct aq_hw_s *self, u32 speed)
{
	u32 val = aq_hw_read_reg(self, HW_ATL_MPI_CONTROL_ADR);

	val = val & ~HW_ATL_MPI_SPEED_MSK;
	val |= speed << HW_ATL_MPI_SPEED_SHIFT;
	aq_hw_write_reg(self, HW_ATL_MPI_CONTROL_ADR, val);

	return 0;
}

static int hw_atl_utils_mpi_set_state(struct aq_hw_s *self,
				      enum hal_atl_utils_fw_state_e state)
{
	u32 val = aq_hw_read_reg(self, HW_ATL_MPI_CONTROL_ADR);
	struct hw_atl_utils_mbox_header mbox;
	u32 transaction_id = 0;
	int err = 0;

	if (state == MPI_RESET) {
		hw_atl_utils_mpi_read_mbox(self, &mbox);

		transaction_id = mbox.transaction_id;

		err = readx_poll_timeout_atomic(hw_atl_utils_get_mpi_mbox_tid,
						self, mbox.transaction_id,
						transaction_id !=
						mbox.transaction_id,
						1000U, 100000U);
		if (err < 0)
			goto err_exit;
	}
	/* On interface DEINIT we disable DW (raise bit)
	 * Otherwise enable DW (clear bit)
	 */
	if (state == MPI_DEINIT || state == MPI_POWER)
		val |= HW_ATL_MPI_DIRTY_WAKE_MSK;
	else
		val &= ~HW_ATL_MPI_DIRTY_WAKE_MSK;

	/* Set new state bits */
	val = val & ~HW_ATL_MPI_STATE_MSK;
	val |= state & HW_ATL_MPI_STATE_MSK;

	aq_hw_write_reg(self, HW_ATL_MPI_CONTROL_ADR, val);

err_exit:
	return err;
}

int hw_atl_utils_mpi_get_link_status(struct aq_hw_s *self)
{
	struct aq_hw_link_status_s *link_status = &self->aq_link_status;
	u32 mpi_state;
	u32 speed;

	mpi_state = hw_atl_utils_mpi_get_state(self);
	speed = mpi_state >> HW_ATL_MPI_SPEED_SHIFT;

	if (!speed) {
		link_status->mbps = 0U;
	} else {
		switch (speed) {
		case HAL_ATLANTIC_RATE_10G:
			link_status->mbps = 10000U;
			break;

		case HAL_ATLANTIC_RATE_5G:
		case HAL_ATLANTIC_RATE_5GSR:
			link_status->mbps = 5000U;
			break;

		case HAL_ATLANTIC_RATE_2G5:
			link_status->mbps = 2500U;
			break;

		case HAL_ATLANTIC_RATE_1G:
			link_status->mbps = 1000U;
			break;

		case HAL_ATLANTIC_RATE_100M:
			link_status->mbps = 100U;
			break;

		default:
			return -EBUSY;
		}
	}

	return 0;
}

int hw_atl_utils_get_mac_permanent(struct aq_hw_s *self,
				   u8 *mac)
{
	u32 mac_addr[2];
	u32 efuse_addr;
	int err = 0;
	u32 h = 0U;
	u32 l = 0U;

	if (!aq_hw_read_reg(self, HW_ATL_UCP_0X370_REG)) {
		unsigned int ucp_0x370 = 0;
		unsigned int rnd = 0;

		get_random_bytes(&rnd, sizeof(unsigned int));

		ucp_0x370 = 0x02020202 | (0xFEFEFEFE & rnd);
		aq_hw_write_reg(self, HW_ATL_UCP_0X370_REG, ucp_0x370);
	}

	efuse_addr = aq_hw_read_reg(self, 0x00000374U);

	err = hw_atl_utils_fw_downld_dwords(self, efuse_addr + (40U * 4U),
					    mac_addr, ARRAY_SIZE(mac_addr));
	if (err < 0) {
		mac_addr[0] = 0U;
		mac_addr[1] = 0U;
		err = 0;
	} else {
		mac_addr[0] = __swab32(mac_addr[0]);
		mac_addr[1] = __swab32(mac_addr[1]);
	}

	ether_addr_copy(mac, (u8 *)mac_addr);

	if ((mac[0] & 0x01U) || ((mac[0] | mac[1] | mac[2]) == 0x00U)) {
		/* chip revision */
		l = 0xE3000000U |
		    (0xFFFFU & aq_hw_read_reg(self, HW_ATL_UCP_0X370_REG)) |
		    (0x00 << 16);
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

unsigned int hw_atl_utils_mbps_2_speed_index(unsigned int mbps)
{
	unsigned int ret = 0U;

	switch (mbps) {
	case 100U:
		ret = 5U;
		break;

	case 1000U:
		ret = 4U;
		break;

	case 2500U:
		ret = 3U;
		break;

	case 5000U:
		ret = 1U;
		break;

	case 10000U:
		ret = 0U;
		break;

	default:
		break;
	}

	return ret;
}

void hw_atl_utils_hw_chip_features_init(struct aq_hw_s *self, u32 *p)
{
	u32 val = hw_atl_reg_glb_mif_id_get(self);
	u32 mif_rev = val & 0xFFU;
	u32 chip_features = 0U;

	chip_features |= ATL_HW_CHIP_ATLANTIC;

	if ((0xFU & mif_rev) == 1U) {
		chip_features |= ATL_HW_CHIP_REVISION_A0 |
			ATL_HW_CHIP_MPI_AQ |
			ATL_HW_CHIP_MIPS;
	} else if ((0xFU & mif_rev) == 2U) {
		chip_features |= ATL_HW_CHIP_REVISION_B0 |
			ATL_HW_CHIP_MPI_AQ |
			ATL_HW_CHIP_MIPS |
			ATL_HW_CHIP_TPO2 |
			ATL_HW_CHIP_RPF2;
	} else if ((0xFU & mif_rev) == 0xAU) {
		chip_features |= ATL_HW_CHIP_REVISION_B1 |
			ATL_HW_CHIP_MPI_AQ |
			ATL_HW_CHIP_MIPS |
			ATL_HW_CHIP_TPO2 |
			ATL_HW_CHIP_RPF2;
	}

	*p = chip_features;
}

static int hw_atl_fw1x_deinit(struct aq_hw_s *self)
{
	hw_atl_utils_mpi_set_speed(self, 0);
	hw_atl_utils_mpi_set_state(self, MPI_DEINIT);

	return 0;
}

int hw_atl_utils_update_stats(struct aq_hw_s *self)
{
	struct aq_stats_s *cs = &self->curr_stats;
	struct hw_atl_utils_mbox mbox;

	hw_atl_utils_mpi_read_stats(self, &mbox);

#define AQ_SDELTA(_N_) (self->curr_stats._N_ += \
			mbox.stats._N_ - self->last_stats._N_)

	if (self->aq_link_status.mbps) {
		AQ_SDELTA(uprc);
		AQ_SDELTA(mprc);
		AQ_SDELTA(bprc);
		AQ_SDELTA(erpt);

		AQ_SDELTA(uptc);
		AQ_SDELTA(mptc);
		AQ_SDELTA(bptc);
		AQ_SDELTA(erpr);

		AQ_SDELTA(ubrc);
		AQ_SDELTA(ubtc);
		AQ_SDELTA(mbrc);
		AQ_SDELTA(mbtc);
		AQ_SDELTA(bbrc);
		AQ_SDELTA(bbtc);
		AQ_SDELTA(dpc);
	}
#undef AQ_SDELTA

	cs->dma_pkt_rc = hw_atl_stats_rx_dma_good_pkt_counter_get(self);
	cs->dma_pkt_tc = hw_atl_stats_tx_dma_good_pkt_counter_get(self);
	cs->dma_oct_rc = hw_atl_stats_rx_dma_good_octet_counter_get(self);
	cs->dma_oct_tc = hw_atl_stats_tx_dma_good_octet_counter_get(self);

	memcpy(&self->last_stats, &mbox.stats, sizeof(mbox.stats));

	return 0;
}

struct aq_stats_s *hw_atl_utils_get_hw_stats(struct aq_hw_s *self)
{
	return &self->curr_stats;
}

static const u32 hw_atl_utils_hw_mac_regs[] = {
	0x00005580U, 0x00005590U, 0x000055B0U, 0x000055B4U,
	0x000055C0U, 0x00005B00U, 0x00005B04U, 0x00005B08U,
	0x00005B0CU, 0x00005B10U, 0x00005B14U, 0x00005B18U,
	0x00005B1CU, 0x00005B20U, 0x00005B24U, 0x00005B28U,
	0x00005B2CU, 0x00005B30U, 0x00005B34U, 0x00005B38U,
	0x00005B3CU, 0x00005B40U, 0x00005B44U, 0x00005B48U,
	0x00005B4CU, 0x00005B50U, 0x00005B54U, 0x00005B58U,
	0x00005B5CU, 0x00005B60U, 0x00005B64U, 0x00005B68U,
	0x00005B6CU, 0x00005B70U, 0x00005B74U, 0x00005B78U,
	0x00005B7CU, 0x00007C00U, 0x00007C04U, 0x00007C08U,
	0x00007C0CU, 0x00007C10U, 0x00007C14U, 0x00007C18U,
	0x00007C1CU, 0x00007C20U, 0x00007C40U, 0x00007C44U,
	0x00007C48U, 0x00007C4CU, 0x00007C50U, 0x00007C54U,
	0x00007C58U, 0x00007C5CU, 0x00007C60U, 0x00007C80U,
	0x00007C84U, 0x00007C88U, 0x00007C8CU, 0x00007C90U,
	0x00007C94U, 0x00007C98U, 0x00007C9CU, 0x00007CA0U,
	0x00007CC0U, 0x00007CC4U, 0x00007CC8U, 0x00007CCCU,
	0x00007CD0U, 0x00007CD4U, 0x00007CD8U, 0x00007CDCU,
	0x00007CE0U, 0x00000300U, 0x00000304U, 0x00000308U,
	0x0000030cU, 0x00000310U, 0x00000314U, 0x00000318U,
	0x0000031cU, 0x00000360U, 0x00000364U, 0x00000368U,
	0x0000036cU, 0x00000370U, 0x00000374U, 0x00006900U,
};

int hw_atl_utils_hw_get_regs(struct aq_hw_s *self,
			     const struct aq_hw_caps_s *aq_hw_caps,
			     u32 *regs_buff)
{
	unsigned int i = 0U;

	for (i = 0; i < aq_hw_caps->mac_regs_count; i++)
		regs_buff[i] = aq_hw_read_reg(self,
					      hw_atl_utils_hw_mac_regs[i]);

	return 0;
}

u32 hw_atl_utils_get_fw_version(struct aq_hw_s *self)
{
	return aq_hw_read_reg(self, HW_ATL_MPI_FW_VERSION);
}

static int aq_fw1x_set_wake_magic(struct aq_hw_s *self, bool wol_enabled,
				  u8 *mac)
{
	struct hw_atl_utils_fw_rpc *prpc = NULL;
	unsigned int rpc_size = 0U;
	int err = 0;

	err = hw_atl_utils_fw_rpc_wait(self, &prpc);
	if (err < 0)
		goto err_exit;

	memset(prpc, 0, sizeof(*prpc));

	if (wol_enabled) {
		rpc_size = offsetof(struct hw_atl_utils_fw_rpc, msg_wol_add) +
			   sizeof(prpc->msg_wol_add);


		prpc->msg_id = HAL_ATLANTIC_UTILS_FW_MSG_WOL_ADD;
		prpc->msg_wol_add.priority =
				HAL_ATLANTIC_UTILS_FW_MSG_WOL_PRIOR;
		prpc->msg_wol_add.pattern_id =
				HAL_ATLANTIC_UTILS_FW_MSG_WOL_PATTERN;
		prpc->msg_wol_add.packet_type =
				HAL_ATLANTIC_UTILS_FW_MSG_WOL_MAG_PKT;

		ether_addr_copy((u8 *)&prpc->msg_wol_add.magic_packet_pattern,
				mac);
	} else {
		rpc_size = sizeof(prpc->msg_wol_remove) +
			   offsetof(struct hw_atl_utils_fw_rpc, msg_wol_remove);

		prpc->msg_id = HAL_ATLANTIC_UTILS_FW_MSG_WOL_DEL;
		prpc->msg_wol_add.pattern_id =
				HAL_ATLANTIC_UTILS_FW_MSG_WOL_PATTERN;
	}

	err = hw_atl_utils_fw_rpc_call(self, rpc_size);

err_exit:
	return err;
}

static int aq_fw1x_set_power(struct aq_hw_s *self, unsigned int power_state,
			     u8 *mac)
{
	struct hw_atl_utils_fw_rpc *prpc = NULL;
	unsigned int rpc_size = 0U;
	int err = 0;

	if (self->aq_nic_cfg->wol & WAKE_MAGIC) {
		err = aq_fw1x_set_wake_magic(self, 1, mac);

		if (err < 0)
			goto err_exit;

		rpc_size = sizeof(prpc->msg_id) +
			   sizeof(prpc->msg_enable_wakeup);

		err = hw_atl_utils_fw_rpc_wait(self, &prpc);

		if (err < 0)
			goto err_exit;

		memset(prpc, 0, rpc_size);

		prpc->msg_id = HAL_ATLANTIC_UTILS_FW_MSG_ENABLE_WAKEUP;
		prpc->msg_enable_wakeup.pattern_mask = 0x00000002;

		err = hw_atl_utils_fw_rpc_call(self, rpc_size);
		if (err < 0)
			goto err_exit;
	}
	hw_atl_utils_mpi_set_speed(self, 0);
	hw_atl_utils_mpi_set_state(self, MPI_POWER);

err_exit:
	return err;
}

static u32 hw_atl_utils_get_mpi_mbox_tid(struct aq_hw_s *self)
{
	struct hw_atl_utils_mbox_header mbox;

	hw_atl_utils_mpi_read_mbox(self, &mbox);

	return mbox.transaction_id;
}

static u32 hw_atl_utils_mpi_get_state(struct aq_hw_s *self)
{
	return aq_hw_read_reg(self, HW_ATL_MPI_STATE_ADR);
}

static u32 hw_atl_utils_mif_cmd_get(struct aq_hw_s *self)
{
	return aq_hw_read_reg(self, HW_ATL_MIF_CMD);
}

static u32 hw_atl_utils_mif_addr_get(struct aq_hw_s *self)
{
	return aq_hw_read_reg(self, HW_ATL_MIF_ADDR);
}

static u32 hw_atl_utils_rpc_state_get(struct aq_hw_s *self)
{
	return aq_hw_read_reg(self, HW_ATL_RPC_STATE_ADR);
}

static u32 aq_fw1x_rpc_get(struct aq_hw_s *self)
{
	return aq_hw_read_reg(self, HW_ATL_MPI_RPC_ADDR);
}

const struct aq_fw_ops aq_fw_1x_ops = {
	.init = hw_atl_utils_mpi_create,
	.deinit = hw_atl_fw1x_deinit,
	.reset = NULL,
	.get_mac_permanent = hw_atl_utils_get_mac_permanent,
	.set_link_speed = hw_atl_utils_mpi_set_speed,
	.set_state = hw_atl_utils_mpi_set_state,
	.update_link_status = hw_atl_utils_mpi_get_link_status,
	.update_stats = hw_atl_utils_update_stats,
	.get_phy_temp = NULL,
	.set_power = aq_fw1x_set_power,
	.set_eee_rate = NULL,
	.get_eee_rate = NULL,
	.set_flow_control = NULL,
	.send_fw_request = NULL,
	.enable_ptp = NULL,
	.led_control = NULL,
};
