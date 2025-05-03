// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2003-2014, 2018-2022, 2024-2025 Intel Corporation
 * Copyright (C) 2015-2016 Intel Deutschland GmbH
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/export.h>

#include "iwl-drv.h"
#include "iwl-io.h"
#include "iwl-csr.h"
#include "iwl-debug.h"
#include "iwl-prph.h"
#include "iwl-fh.h"

void iwl_write8(struct iwl_trans *trans, u32 ofs, u8 val)
{
	trace_iwlwifi_dev_iowrite8(trans->dev, ofs, val);
	iwl_trans_write8(trans, ofs, val);
}
IWL_EXPORT_SYMBOL(iwl_write8);

void iwl_write32(struct iwl_trans *trans, u32 ofs, u32 val)
{
	trace_iwlwifi_dev_iowrite32(trans->dev, ofs, val);
	iwl_trans_write32(trans, ofs, val);
}
IWL_EXPORT_SYMBOL(iwl_write32);

void iwl_write64(struct iwl_trans *trans, u64 ofs, u64 val)
{
	trace_iwlwifi_dev_iowrite64(trans->dev, ofs, val);
	iwl_trans_write32(trans, ofs, lower_32_bits(val));
	iwl_trans_write32(trans, ofs + 4, upper_32_bits(val));
}
IWL_EXPORT_SYMBOL(iwl_write64);

u32 iwl_read32(struct iwl_trans *trans, u32 ofs)
{
	u32 val = iwl_trans_read32(trans, ofs);

	trace_iwlwifi_dev_ioread32(trans->dev, ofs, val);
	return val;
}
IWL_EXPORT_SYMBOL(iwl_read32);

#define IWL_POLL_INTERVAL 10	/* microseconds */

int iwl_poll_bit(struct iwl_trans *trans, u32 addr,
		 u32 bits, u32 mask, int timeout)
{
	int t = 0;

	do {
		if ((iwl_read32(trans, addr) & mask) == (bits & mask))
			return t;
		udelay(IWL_POLL_INTERVAL);
		t += IWL_POLL_INTERVAL;
	} while (t < timeout);

	return -ETIMEDOUT;
}
IWL_EXPORT_SYMBOL(iwl_poll_bit);

u32 iwl_read_direct32(struct iwl_trans *trans, u32 reg)
{
	if (iwl_trans_grab_nic_access(trans)) {
		u32 value = iwl_read32(trans, reg);

		iwl_trans_release_nic_access(trans);
		return value;
	}

	/* return as if we have a HW timeout/failure */
	return 0x5a5a5a5a;
}
IWL_EXPORT_SYMBOL(iwl_read_direct32);

void iwl_write_direct32(struct iwl_trans *trans, u32 reg, u32 value)
{
	if (iwl_trans_grab_nic_access(trans)) {
		iwl_write32(trans, reg, value);
		iwl_trans_release_nic_access(trans);
	}
}
IWL_EXPORT_SYMBOL(iwl_write_direct32);

void iwl_write_direct64(struct iwl_trans *trans, u64 reg, u64 value)
{
	if (iwl_trans_grab_nic_access(trans)) {
		iwl_write64(trans, reg, value);
		iwl_trans_release_nic_access(trans);
	}
}
IWL_EXPORT_SYMBOL(iwl_write_direct64);

int iwl_poll_direct_bit(struct iwl_trans *trans, u32 addr, u32 mask,
			int timeout)
{
	int t = 0;

	do {
		if ((iwl_read_direct32(trans, addr) & mask) == mask)
			return t;
		udelay(IWL_POLL_INTERVAL);
		t += IWL_POLL_INTERVAL;
	} while (t < timeout);

	return -ETIMEDOUT;
}
IWL_EXPORT_SYMBOL(iwl_poll_direct_bit);

u32 iwl_read_prph_no_grab(struct iwl_trans *trans, u32 ofs)
{
	u32 val = iwl_trans_read_prph(trans, ofs);
	trace_iwlwifi_dev_ioread_prph32(trans->dev, ofs, val);
	return val;
}
IWL_EXPORT_SYMBOL(iwl_read_prph_no_grab);

void iwl_write_prph_no_grab(struct iwl_trans *trans, u32 ofs, u32 val)
{
	trace_iwlwifi_dev_iowrite_prph32(trans->dev, ofs, val);
	iwl_trans_write_prph(trans, ofs, val);
}
IWL_EXPORT_SYMBOL(iwl_write_prph_no_grab);

void iwl_write_prph64_no_grab(struct iwl_trans *trans, u64 ofs, u64 val)
{
	trace_iwlwifi_dev_iowrite_prph64(trans->dev, ofs, val);
	iwl_write_prph_no_grab(trans, ofs, val & 0xffffffff);
	iwl_write_prph_no_grab(trans, ofs + 4, val >> 32);
}
IWL_EXPORT_SYMBOL(iwl_write_prph64_no_grab);

u32 iwl_read_prph(struct iwl_trans *trans, u32 ofs)
{
	if (iwl_trans_grab_nic_access(trans)) {
		u32 val = iwl_read_prph_no_grab(trans, ofs);

		iwl_trans_release_nic_access(trans);

		return val;
	}

	/* return as if we have a HW timeout/failure */
	return 0x5a5a5a5a;
}
IWL_EXPORT_SYMBOL(iwl_read_prph);

void iwl_write_prph_delay(struct iwl_trans *trans, u32 ofs, u32 val, u32 delay_ms)
{
	if (iwl_trans_grab_nic_access(trans)) {
		mdelay(delay_ms);
		iwl_write_prph_no_grab(trans, ofs, val);
		iwl_trans_release_nic_access(trans);
	}
}
IWL_EXPORT_SYMBOL(iwl_write_prph_delay);

int iwl_poll_prph_bit(struct iwl_trans *trans, u32 addr,
		      u32 bits, u32 mask, int timeout)
{
	int t = 0;

	do {
		if ((iwl_read_prph(trans, addr) & mask) == (bits & mask))
			return t;
		udelay(IWL_POLL_INTERVAL);
		t += IWL_POLL_INTERVAL;
	} while (t < timeout);

	return -ETIMEDOUT;
}

void iwl_set_bits_prph(struct iwl_trans *trans, u32 ofs, u32 mask)
{
	if (iwl_trans_grab_nic_access(trans)) {
		iwl_write_prph_no_grab(trans, ofs,
				       iwl_read_prph_no_grab(trans, ofs) |
				       mask);
		iwl_trans_release_nic_access(trans);
	}
}
IWL_EXPORT_SYMBOL(iwl_set_bits_prph);

void iwl_set_bits_mask_prph(struct iwl_trans *trans, u32 ofs,
			    u32 bits, u32 mask)
{
	if (iwl_trans_grab_nic_access(trans)) {
		iwl_write_prph_no_grab(trans, ofs,
				       (iwl_read_prph_no_grab(trans, ofs) &
					mask) | bits);
		iwl_trans_release_nic_access(trans);
	}
}
IWL_EXPORT_SYMBOL(iwl_set_bits_mask_prph);

void iwl_clear_bits_prph(struct iwl_trans *trans, u32 ofs, u32 mask)
{
	u32 val;

	if (iwl_trans_grab_nic_access(trans)) {
		val = iwl_read_prph_no_grab(trans, ofs);
		iwl_write_prph_no_grab(trans, ofs, (val & ~mask));
		iwl_trans_release_nic_access(trans);
	}
}
IWL_EXPORT_SYMBOL(iwl_clear_bits_prph);

void iwl_force_nmi(struct iwl_trans *trans)
{
	if (trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_9000)
		iwl_write_prph_delay(trans, DEVICE_SET_NMI_REG,
				     DEVICE_SET_NMI_VAL_DRV, 1);
	else if (trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_AX210)
		iwl_write_umac_prph(trans, UREG_NIC_SET_NMI_DRIVER,
				UREG_NIC_SET_NMI_DRIVER_NMI_FROM_DRIVER);
	else if (trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_BZ)
		iwl_write_umac_prph(trans, UREG_DOORBELL_TO_ISR6,
				    UREG_DOORBELL_TO_ISR6_NMI_BIT);
	else
		iwl_write32(trans, CSR_DOORBELL_VECTOR,
			    UREG_DOORBELL_TO_ISR6_NMI_BIT);
}
IWL_EXPORT_SYMBOL(iwl_force_nmi);

static const char *get_rfh_string(int cmd)
{
#define IWL_CMD(x) case x: return #x
#define IWL_CMD_MQ(arg, reg, q) { if (arg == reg(q)) return #reg; }

	int i;

	for (i = 0; i < IWL_MAX_RX_HW_QUEUES; i++) {
		IWL_CMD_MQ(cmd, RFH_Q_FRBDCB_BA_LSB, i);
		IWL_CMD_MQ(cmd, RFH_Q_FRBDCB_WIDX, i);
		IWL_CMD_MQ(cmd, RFH_Q_FRBDCB_RIDX, i);
		IWL_CMD_MQ(cmd, RFH_Q_URBD_STTS_WPTR_LSB, i);
	}

	switch (cmd) {
	IWL_CMD(RFH_RXF_DMA_CFG);
	IWL_CMD(RFH_GEN_CFG);
	IWL_CMD(RFH_GEN_STATUS);
	IWL_CMD(FH_TSSR_TX_STATUS_REG);
	IWL_CMD(FH_TSSR_TX_ERROR_REG);
	default:
		return "UNKNOWN";
	}
#undef IWL_CMD_MQ
}

struct reg {
	u32 addr;
	bool is64;
};

static int iwl_dump_rfh(struct iwl_trans *trans, char **buf)
{
	int i, q;
	int num_q = trans->info.num_rxqs;
	static const u32 rfh_tbl[] = {
		RFH_RXF_DMA_CFG,
		RFH_GEN_CFG,
		RFH_GEN_STATUS,
		FH_TSSR_TX_STATUS_REG,
		FH_TSSR_TX_ERROR_REG,
	};
	static const struct reg rfh_mq_tbl[] = {
		{ RFH_Q0_FRBDCB_BA_LSB, true },
		{ RFH_Q0_FRBDCB_WIDX, false },
		{ RFH_Q0_FRBDCB_RIDX, false },
		{ RFH_Q0_URBD_STTS_WPTR_LSB, true },
	};

#ifdef CONFIG_IWLWIFI_DEBUGFS
	if (buf) {
		int pos = 0;
		/*
		 * Register (up to 34 for name + 8 blank/q for MQ): 40 chars
		 * Colon + space: 2 characters
		 * 0X%08x: 10 characters
		 * New line: 1 character
		 * Total of 53 characters
		 */
		size_t bufsz = ARRAY_SIZE(rfh_tbl) * 53 +
			       ARRAY_SIZE(rfh_mq_tbl) * 53 * num_q + 40;

		*buf = kmalloc(bufsz, GFP_KERNEL);
		if (!*buf)
			return -ENOMEM;

		pos += scnprintf(*buf + pos, bufsz - pos,
				"RFH register values:\n");

		for (i = 0; i < ARRAY_SIZE(rfh_tbl); i++)
			pos += scnprintf(*buf + pos, bufsz - pos,
				"%40s: 0X%08x\n",
				get_rfh_string(rfh_tbl[i]),
				iwl_read_prph(trans, rfh_tbl[i]));

		for (i = 0; i < ARRAY_SIZE(rfh_mq_tbl); i++)
			for (q = 0; q < num_q; q++) {
				u32 addr = rfh_mq_tbl[i].addr;

				addr += q * (rfh_mq_tbl[i].is64 ? 8 : 4);
				pos += scnprintf(*buf + pos, bufsz - pos,
					"%34s(q %2d): 0X%08x\n",
					get_rfh_string(addr), q,
					iwl_read_prph(trans, addr));
			}

		return pos;
	}
#endif

	IWL_ERR(trans, "RFH register values:\n");
	for (i = 0; i < ARRAY_SIZE(rfh_tbl); i++)
		IWL_ERR(trans, "  %34s: 0X%08x\n",
			get_rfh_string(rfh_tbl[i]),
			iwl_read_prph(trans, rfh_tbl[i]));

	for (i = 0; i < ARRAY_SIZE(rfh_mq_tbl); i++)
		for (q = 0; q < num_q; q++) {
			u32 addr = rfh_mq_tbl[i].addr;

			addr += q * (rfh_mq_tbl[i].is64 ? 8 : 4);
			IWL_ERR(trans, "  %34s(q %d): 0X%08x\n",
				get_rfh_string(addr), q,
				iwl_read_prph(trans, addr));
		}

	return 0;
}

static const char *get_fh_string(int cmd)
{
	switch (cmd) {
	IWL_CMD(FH_RSCSR_CHNL0_STTS_WPTR_REG);
	IWL_CMD(FH_RSCSR_CHNL0_RBDCB_BASE_REG);
	IWL_CMD(FH_RSCSR_CHNL0_WPTR);
	IWL_CMD(FH_MEM_RCSR_CHNL0_CONFIG_REG);
	IWL_CMD(FH_MEM_RSSR_SHARED_CTRL_REG);
	IWL_CMD(FH_MEM_RSSR_RX_STATUS_REG);
	IWL_CMD(FH_MEM_RSSR_RX_ENABLE_ERR_IRQ2DRV);
	IWL_CMD(FH_TSSR_TX_STATUS_REG);
	IWL_CMD(FH_TSSR_TX_ERROR_REG);
	default:
		return "UNKNOWN";
	}
#undef IWL_CMD
}

int iwl_dump_fh(struct iwl_trans *trans, char **buf)
{
	int i;
	static const u32 fh_tbl[] = {
		FH_RSCSR_CHNL0_STTS_WPTR_REG,
		FH_RSCSR_CHNL0_RBDCB_BASE_REG,
		FH_RSCSR_CHNL0_WPTR,
		FH_MEM_RCSR_CHNL0_CONFIG_REG,
		FH_MEM_RSSR_SHARED_CTRL_REG,
		FH_MEM_RSSR_RX_STATUS_REG,
		FH_MEM_RSSR_RX_ENABLE_ERR_IRQ2DRV,
		FH_TSSR_TX_STATUS_REG,
		FH_TSSR_TX_ERROR_REG
	};

	if (trans->trans_cfg->mq_rx_supported)
		return iwl_dump_rfh(trans, buf);

#ifdef CONFIG_IWLWIFI_DEBUGFS
	if (buf) {
		int pos = 0;
		size_t bufsz = ARRAY_SIZE(fh_tbl) * 48 + 40;

		*buf = kmalloc(bufsz, GFP_KERNEL);
		if (!*buf)
			return -ENOMEM;

		pos += scnprintf(*buf + pos, bufsz - pos,
				"FH register values:\n");

		for (i = 0; i < ARRAY_SIZE(fh_tbl); i++)
			pos += scnprintf(*buf + pos, bufsz - pos,
				"  %34s: 0X%08x\n",
				get_fh_string(fh_tbl[i]),
				iwl_read_direct32(trans, fh_tbl[i]));

		return pos;
	}
#endif

	IWL_ERR(trans, "FH register values:\n");
	for (i = 0; i <  ARRAY_SIZE(fh_tbl); i++)
		IWL_ERR(trans, "  %34s: 0X%08x\n",
			get_fh_string(fh_tbl[i]),
			iwl_read_direct32(trans, fh_tbl[i]));

	return 0;
}

#define IWL_HOST_MON_BLOCK_PEMON	0x00
#define IWL_HOST_MON_BLOCK_HIPM		0x22

#define IWL_HOST_MON_BLOCK_PEMON_VEC0	0x00
#define IWL_HOST_MON_BLOCK_PEMON_VEC1	0x01
#define IWL_HOST_MON_BLOCK_PEMON_WFPM	0x06

static void iwl_dump_host_monitor_block(struct iwl_trans *trans,
					u32 block, u32 vec, u32 iter)
{
	int i;

	IWL_ERR(trans, "Host monitor block 0x%x vector 0x%x\n", block, vec);
	iwl_write32(trans, CSR_MONITOR_CFG_REG, (block << 8) | vec);
	for (i = 0; i < iter; i++)
		IWL_ERR(trans, "    value [iter %d]: 0x%08x\n",
			i, iwl_read32(trans, CSR_MONITOR_STATUS_REG));
}

static void iwl_dump_host_monitor(struct iwl_trans *trans)
{
	switch (trans->trans_cfg->device_family) {
	case IWL_DEVICE_FAMILY_22000:
	case IWL_DEVICE_FAMILY_AX210:
		IWL_ERR(trans, "CSR_RESET = 0x%x\n",
			iwl_read32(trans, CSR_RESET));
		iwl_dump_host_monitor_block(trans, IWL_HOST_MON_BLOCK_PEMON,
					    IWL_HOST_MON_BLOCK_PEMON_VEC0, 15);
		iwl_dump_host_monitor_block(trans, IWL_HOST_MON_BLOCK_PEMON,
					    IWL_HOST_MON_BLOCK_PEMON_VEC1, 15);
		iwl_dump_host_monitor_block(trans, IWL_HOST_MON_BLOCK_PEMON,
					    IWL_HOST_MON_BLOCK_PEMON_WFPM, 15);
		iwl_dump_host_monitor_block(trans, IWL_HOST_MON_BLOCK_HIPM,
					    IWL_HOST_MON_BLOCK_PEMON_VEC0, 1);
		break;
	default:
		/* not supported yet */
		return;
	}
}

int iwl_finish_nic_init(struct iwl_trans *trans)
{
	const struct iwl_cfg_trans_params *cfg_trans = trans->trans_cfg;
	u32 poll_ready;
	int err;

	if (cfg_trans->bisr_workaround) {
		/* ensure the TOP FSM isn't still in previous reset */
		mdelay(2);
	}

	/*
	 * Set "initialization complete" bit to move adapter from
	 * D0U* --> D0A* (powered-up active) state.
	 */
	if (cfg_trans->device_family >= IWL_DEVICE_FAMILY_BZ) {
		iwl_set_bit(trans, CSR_GP_CNTRL,
			    CSR_GP_CNTRL_REG_FLAG_BZ_MAC_ACCESS_REQ |
			    CSR_GP_CNTRL_REG_FLAG_MAC_INIT);
		poll_ready = CSR_GP_CNTRL_REG_FLAG_MAC_STATUS;
	} else {
		iwl_set_bit(trans, CSR_GP_CNTRL,
			    CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
		poll_ready = CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY;
	}

	if (cfg_trans->device_family == IWL_DEVICE_FAMILY_8000)
		udelay(2);

	/*
	 * Wait for clock stabilization; once stabilized, access to
	 * device-internal resources is supported, e.g. iwl_write_prph()
	 * and accesses to uCode SRAM.
	 */
	err = iwl_poll_bit(trans, CSR_GP_CNTRL, poll_ready, poll_ready, 25000);
	if (err < 0) {
		IWL_DEBUG_INFO(trans, "Failed to wake NIC\n");

		iwl_dump_host_monitor(trans);
	}

	if (cfg_trans->bisr_workaround) {
		/* ensure BISR shift has finished */
		udelay(200);
	}

	return err < 0 ? err : 0;
}
IWL_EXPORT_SYMBOL(iwl_finish_nic_init);

void iwl_trans_sync_nmi_with_addr(struct iwl_trans *trans, u32 inta_addr,
				  u32 sw_err_bit)
{
	unsigned long timeout = jiffies + IWL_TRANS_NMI_TIMEOUT;
	bool interrupts_enabled = test_bit(STATUS_INT_ENABLED, &trans->status);

	/* if the interrupts were already disabled, there is no point in
	 * calling iwl_disable_interrupts
	 */
	if (interrupts_enabled)
		iwl_trans_interrupts(trans, false);

	iwl_force_nmi(trans);
	while (time_after(timeout, jiffies)) {
		u32 inta_hw = iwl_read32(trans, inta_addr);

		/* Error detected by uCode */
		if (inta_hw & sw_err_bit) {
			/* Clear causes register */
			iwl_write32(trans, inta_addr, inta_hw & sw_err_bit);
			break;
		}

		mdelay(1);
	}

	/* enable interrupts only if there were already enabled before this
	 * function to avoid a case were the driver enable interrupts before
	 * proper configurations were made
	 */
	if (interrupts_enabled)
		iwl_trans_interrupts(trans, true);

	iwl_trans_fw_error(trans, IWL_ERR_TYPE_NMI_FORCED);
}
