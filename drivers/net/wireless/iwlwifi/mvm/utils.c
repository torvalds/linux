/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#include <net/mac80211.h>

#include "iwl-debug.h"
#include "iwl-io.h"
#include "iwl-prph.h"

#include "mvm.h"
#include "fw-api-rs.h"

/*
 * Will return 0 even if the cmd failed when RFKILL is asserted unless
 * CMD_WANT_SKB is set in cmd->flags.
 */
int iwl_mvm_send_cmd(struct iwl_mvm *mvm, struct iwl_host_cmd *cmd)
{
	int ret;

#if defined(CONFIG_IWLWIFI_DEBUGFS) && defined(CONFIG_PM_SLEEP)
	if (WARN_ON(mvm->d3_test_active))
		return -EIO;
#endif

	/*
	 * Synchronous commands from this op-mode must hold
	 * the mutex, this ensures we don't try to send two
	 * (or more) synchronous commands at a time.
	 */
	if (!(cmd->flags & CMD_ASYNC))
		lockdep_assert_held(&mvm->mutex);

	ret = iwl_trans_send_cmd(mvm->trans, cmd);

	/*
	 * If the caller wants the SKB, then don't hide any problems, the
	 * caller might access the response buffer which will be NULL if
	 * the command failed.
	 */
	if (cmd->flags & CMD_WANT_SKB)
		return ret;

	/* Silently ignore failures if RFKILL is asserted */
	if (!ret || ret == -ERFKILL)
		return 0;
	return ret;
}

int iwl_mvm_send_cmd_pdu(struct iwl_mvm *mvm, u32 id,
			 u32 flags, u16 len, const void *data)
{
	struct iwl_host_cmd cmd = {
		.id = id,
		.len = { len, },
		.data = { data, },
		.flags = flags,
	};

	return iwl_mvm_send_cmd(mvm, &cmd);
}

/*
 * We assume that the caller set the status to the success value
 */
int iwl_mvm_send_cmd_status(struct iwl_mvm *mvm, struct iwl_host_cmd *cmd,
			    u32 *status)
{
	struct iwl_rx_packet *pkt;
	struct iwl_cmd_response *resp;
	int ret, resp_len;

	lockdep_assert_held(&mvm->mutex);

#if defined(CONFIG_IWLWIFI_DEBUGFS) && defined(CONFIG_PM_SLEEP)
	if (WARN_ON(mvm->d3_test_active))
		return -EIO;
#endif

	/*
	 * Only synchronous commands can wait for status,
	 * we use WANT_SKB so the caller can't.
	 */
	if (WARN_ONCE(cmd->flags & (CMD_ASYNC | CMD_WANT_SKB),
		      "cmd flags %x", cmd->flags))
		return -EINVAL;

	cmd->flags |= CMD_WANT_SKB;

	ret = iwl_trans_send_cmd(mvm->trans, cmd);
	if (ret == -ERFKILL) {
		/*
		 * The command failed because of RFKILL, don't update
		 * the status, leave it as success and return 0.
		 */
		return 0;
	} else if (ret) {
		return ret;
	}

	pkt = cmd->resp_pkt;
	/* Can happen if RFKILL is asserted */
	if (!pkt) {
		ret = 0;
		goto out_free_resp;
	}

	resp_len = iwl_rx_packet_payload_len(pkt);
	if (WARN_ON_ONCE(resp_len != sizeof(*resp))) {
		ret = -EIO;
		goto out_free_resp;
	}

	resp = (void *)pkt->data;
	*status = le32_to_cpu(resp->status);
 out_free_resp:
	iwl_free_resp(cmd);
	return ret;
}

/*
 * We assume that the caller set the status to the sucess value
 */
int iwl_mvm_send_cmd_pdu_status(struct iwl_mvm *mvm, u32 id, u16 len,
				const void *data, u32 *status)
{
	struct iwl_host_cmd cmd = {
		.id = id,
		.len = { len, },
		.data = { data, },
	};

	return iwl_mvm_send_cmd_status(mvm, &cmd, status);
}

#define IWL_DECLARE_RATE_INFO(r) \
	[IWL_RATE_##r##M_INDEX] = IWL_RATE_##r##M_PLCP

/*
 * Translate from fw_rate_index (IWL_RATE_XXM_INDEX) to PLCP
 */
static const u8 fw_rate_idx_to_plcp[IWL_RATE_COUNT] = {
	IWL_DECLARE_RATE_INFO(1),
	IWL_DECLARE_RATE_INFO(2),
	IWL_DECLARE_RATE_INFO(5),
	IWL_DECLARE_RATE_INFO(11),
	IWL_DECLARE_RATE_INFO(6),
	IWL_DECLARE_RATE_INFO(9),
	IWL_DECLARE_RATE_INFO(12),
	IWL_DECLARE_RATE_INFO(18),
	IWL_DECLARE_RATE_INFO(24),
	IWL_DECLARE_RATE_INFO(36),
	IWL_DECLARE_RATE_INFO(48),
	IWL_DECLARE_RATE_INFO(54),
};

int iwl_mvm_legacy_rate_to_mac80211_idx(u32 rate_n_flags,
					enum ieee80211_band band)
{
	int rate = rate_n_flags & RATE_LEGACY_RATE_MSK;
	int idx;
	int band_offset = 0;

	/* Legacy rate format, search for match in table */
	if (band == IEEE80211_BAND_5GHZ)
		band_offset = IWL_FIRST_OFDM_RATE;
	for (idx = band_offset; idx < IWL_RATE_COUNT_LEGACY; idx++)
		if (fw_rate_idx_to_plcp[idx] == rate)
			return idx - band_offset;

	return -1;
}

u8 iwl_mvm_mac80211_idx_to_hwrate(int rate_idx)
{
	/* Get PLCP rate for tx_cmd->rate_n_flags */
	return fw_rate_idx_to_plcp[rate_idx];
}

void iwl_mvm_rx_fw_error(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_error_resp *err_resp = (void *)pkt->data;

	IWL_ERR(mvm, "FW Error notification: type 0x%08X cmd_id 0x%02X\n",
		le32_to_cpu(err_resp->error_type), err_resp->cmd_id);
	IWL_ERR(mvm, "FW Error notification: seq 0x%04X service 0x%08X\n",
		le16_to_cpu(err_resp->bad_cmd_seq_num),
		le32_to_cpu(err_resp->error_service));
	IWL_ERR(mvm, "FW Error notification: timestamp 0x%16llX\n",
		le64_to_cpu(err_resp->timestamp));
}

/*
 * Returns the first antenna as ANT_[ABC], as defined in iwl-config.h.
 * The parameter should also be a combination of ANT_[ABC].
 */
u8 first_antenna(u8 mask)
{
	BUILD_BUG_ON(ANT_A != BIT(0)); /* using ffs is wrong if not */
	if (WARN_ON_ONCE(!mask)) /* ffs will return 0 if mask is zeroed */
		return BIT(0);
	return BIT(ffs(mask) - 1);
}

/*
 * Toggles between TX antennas to send the probe request on.
 * Receives the bitmask of valid TX antennas and the *index* used
 * for the last TX, and returns the next valid *index* to use.
 * In order to set it in the tx_cmd, must do BIT(idx).
 */
u8 iwl_mvm_next_antenna(struct iwl_mvm *mvm, u8 valid, u8 last_idx)
{
	u8 ind = last_idx;
	int i;

	for (i = 0; i < RATE_MCS_ANT_NUM; i++) {
		ind = (ind + 1) % RATE_MCS_ANT_NUM;
		if (valid & BIT(ind))
			return ind;
	}

	WARN_ONCE(1, "Failed to toggle between antennas 0x%x", valid);
	return last_idx;
}

static const struct {
	const char *name;
	u8 num;
} advanced_lookup[] = {
	{ "NMI_INTERRUPT_WDG", 0x34 },
	{ "SYSASSERT", 0x35 },
	{ "UCODE_VERSION_MISMATCH", 0x37 },
	{ "BAD_COMMAND", 0x38 },
	{ "NMI_INTERRUPT_DATA_ACTION_PT", 0x3C },
	{ "FATAL_ERROR", 0x3D },
	{ "NMI_TRM_HW_ERR", 0x46 },
	{ "NMI_INTERRUPT_TRM", 0x4C },
	{ "NMI_INTERRUPT_BREAK_POINT", 0x54 },
	{ "NMI_INTERRUPT_WDG_RXF_FULL", 0x5C },
	{ "NMI_INTERRUPT_WDG_NO_RBD_RXF_FULL", 0x64 },
	{ "NMI_INTERRUPT_HOST", 0x66 },
	{ "NMI_INTERRUPT_ACTION_PT", 0x7C },
	{ "NMI_INTERRUPT_UNKNOWN", 0x84 },
	{ "NMI_INTERRUPT_INST_ACTION_PT", 0x86 },
	{ "ADVANCED_SYSASSERT", 0 },
};

static const char *desc_lookup(u32 num)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(advanced_lookup) - 1; i++)
		if (advanced_lookup[i].num == num)
			return advanced_lookup[i].name;

	/* No entry matches 'num', so it is the last: ADVANCED_SYSASSERT */
	return advanced_lookup[i].name;
}

/*
 * Note: This structure is read from the device with IO accesses,
 * and the reading already does the endian conversion. As it is
 * read with u32-sized accesses, any members with a different size
 * need to be ordered correctly though!
 */
struct iwl_error_event_table_v1 {
	u32 valid;		/* (nonzero) valid, (0) log is empty */
	u32 error_id;		/* type of error */
	u32 pc;			/* program counter */
	u32 blink1;		/* branch link */
	u32 blink2;		/* branch link */
	u32 ilink1;		/* interrupt link */
	u32 ilink2;		/* interrupt link */
	u32 data1;		/* error-specific data */
	u32 data2;		/* error-specific data */
	u32 data3;		/* error-specific data */
	u32 bcon_time;		/* beacon timer */
	u32 tsf_low;		/* network timestamp function timer */
	u32 tsf_hi;		/* network timestamp function timer */
	u32 gp1;		/* GP1 timer register */
	u32 gp2;		/* GP2 timer register */
	u32 gp3;		/* GP3 timer register */
	u32 ucode_ver;		/* uCode version */
	u32 hw_ver;		/* HW Silicon version */
	u32 brd_ver;		/* HW board version */
	u32 log_pc;		/* log program counter */
	u32 frame_ptr;		/* frame pointer */
	u32 stack_ptr;		/* stack pointer */
	u32 hcmd;		/* last host command header */
	u32 isr0;		/* isr status register LMPM_NIC_ISR0:
				 * rxtx_flag */
	u32 isr1;		/* isr status register LMPM_NIC_ISR1:
				 * host_flag */
	u32 isr2;		/* isr status register LMPM_NIC_ISR2:
				 * enc_flag */
	u32 isr3;		/* isr status register LMPM_NIC_ISR3:
				 * time_flag */
	u32 isr4;		/* isr status register LMPM_NIC_ISR4:
				 * wico interrupt */
	u32 isr_pref;		/* isr status register LMPM_NIC_PREF_STAT */
	u32 wait_event;		/* wait event() caller address */
	u32 l2p_control;	/* L2pControlField */
	u32 l2p_duration;	/* L2pDurationField */
	u32 l2p_mhvalid;	/* L2pMhValidBits */
	u32 l2p_addr_match;	/* L2pAddrMatchStat */
	u32 lmpm_pmg_sel;	/* indicate which clocks are turned on
				 * (LMPM_PMG_SEL) */
	u32 u_timestamp;	/* indicate when the date and time of the
				 * compilation */
	u32 flow_handler;	/* FH read/write pointers, RX credit */
} __packed /* LOG_ERROR_TABLE_API_S_VER_1 */;

struct iwl_error_event_table {
	u32 valid;		/* (nonzero) valid, (0) log is empty */
	u32 error_id;		/* type of error */
	u32 pc;			/* program counter */
	u32 blink1;		/* branch link */
	u32 blink2;		/* branch link */
	u32 ilink1;		/* interrupt link */
	u32 ilink2;		/* interrupt link */
	u32 data1;		/* error-specific data */
	u32 data2;		/* error-specific data */
	u32 data3;		/* error-specific data */
	u32 bcon_time;		/* beacon timer */
	u32 tsf_low;		/* network timestamp function timer */
	u32 tsf_hi;		/* network timestamp function timer */
	u32 gp1;		/* GP1 timer register */
	u32 gp2;		/* GP2 timer register */
	u32 gp3;		/* GP3 timer register */
	u32 major;		/* uCode version major */
	u32 minor;		/* uCode version minor */
	u32 hw_ver;		/* HW Silicon version */
	u32 brd_ver;		/* HW board version */
	u32 log_pc;		/* log program counter */
	u32 frame_ptr;		/* frame pointer */
	u32 stack_ptr;		/* stack pointer */
	u32 hcmd;		/* last host command header */
	u32 isr0;		/* isr status register LMPM_NIC_ISR0:
				 * rxtx_flag */
	u32 isr1;		/* isr status register LMPM_NIC_ISR1:
				 * host_flag */
	u32 isr2;		/* isr status register LMPM_NIC_ISR2:
				 * enc_flag */
	u32 isr3;		/* isr status register LMPM_NIC_ISR3:
				 * time_flag */
	u32 isr4;		/* isr status register LMPM_NIC_ISR4:
				 * wico interrupt */
	u32 isr_pref;		/* isr status register LMPM_NIC_PREF_STAT */
	u32 wait_event;		/* wait event() caller address */
	u32 l2p_control;	/* L2pControlField */
	u32 l2p_duration;	/* L2pDurationField */
	u32 l2p_mhvalid;	/* L2pMhValidBits */
	u32 l2p_addr_match;	/* L2pAddrMatchStat */
	u32 lmpm_pmg_sel;	/* indicate which clocks are turned on
				 * (LMPM_PMG_SEL) */
	u32 u_timestamp;	/* indicate when the date and time of the
				 * compilation */
	u32 flow_handler;	/* FH read/write pointers, RX credit */
} __packed /* LOG_ERROR_TABLE_API_S_VER_2 */;

/*
 * UMAC error struct - relevant starting from family 8000 chip.
 * Note: This structure is read from the device with IO accesses,
 * and the reading already does the endian conversion. As it is
 * read with u32-sized accesses, any members with a different size
 * need to be ordered correctly though!
 */
struct iwl_umac_error_event_table {
	u32 valid;		/* (nonzero) valid, (0) log is empty */
	u32 error_id;		/* type of error */
	u32 blink1;		/* branch link */
	u32 blink2;		/* branch link */
	u32 ilink1;		/* interrupt link */
	u32 ilink2;		/* interrupt link */
	u32 data1;		/* error-specific data */
	u32 data2;		/* error-specific data */
	u32 data3;		/* error-specific data */
	u32 umac_major;
	u32 umac_minor;
	u32 frame_pointer;	/* core register 27*/
	u32 stack_pointer;	/* core register 28 */
	u32 cmd_header;		/* latest host cmd sent to UMAC */
	u32 nic_isr_pref;	/* ISR status register */
} __packed;

#define ERROR_START_OFFSET  (1 * sizeof(u32))
#define ERROR_ELEM_SIZE     (7 * sizeof(u32))

static void iwl_mvm_dump_umac_error_log(struct iwl_mvm *mvm)
{
	struct iwl_trans *trans = mvm->trans;
	struct iwl_umac_error_event_table table;
	u32 base;

	base = mvm->umac_error_event_table;

	if (base < 0x800000) {
		IWL_ERR(mvm,
			"Not valid error log pointer 0x%08X for %s uCode\n",
			base,
			(mvm->cur_ucode == IWL_UCODE_INIT)
					? "Init" : "RT");
		return;
	}

	iwl_trans_read_mem_bytes(trans, base, &table, sizeof(table));

	if (ERROR_START_OFFSET <= table.valid * ERROR_ELEM_SIZE) {
		IWL_ERR(trans, "Start IWL Error Log Dump:\n");
		IWL_ERR(trans, "Status: 0x%08lX, count: %d\n",
			mvm->status, table.valid);
	}

	IWL_ERR(mvm, "0x%08X | %s\n", table.error_id,
		desc_lookup(table.error_id));
	IWL_ERR(mvm, "0x%08X | umac branchlink1\n", table.blink1);
	IWL_ERR(mvm, "0x%08X | umac branchlink2\n", table.blink2);
	IWL_ERR(mvm, "0x%08X | umac interruptlink1\n", table.ilink1);
	IWL_ERR(mvm, "0x%08X | umac interruptlink2\n", table.ilink2);
	IWL_ERR(mvm, "0x%08X | umac data1\n", table.data1);
	IWL_ERR(mvm, "0x%08X | umac data2\n", table.data2);
	IWL_ERR(mvm, "0x%08X | umac data3\n", table.data3);
	IWL_ERR(mvm, "0x%08X | umac major\n", table.umac_major);
	IWL_ERR(mvm, "0x%08X | umac minor\n", table.umac_minor);
	IWL_ERR(mvm, "0x%08X | frame pointer\n", table.frame_pointer);
	IWL_ERR(mvm, "0x%08X | stack pointer\n", table.stack_pointer);
	IWL_ERR(mvm, "0x%08X | last host cmd\n", table.cmd_header);
	IWL_ERR(mvm, "0x%08X | isr status reg\n", table.nic_isr_pref);
}

static void iwl_mvm_dump_nic_error_log_old(struct iwl_mvm *mvm)
{
	struct iwl_trans *trans = mvm->trans;
	struct iwl_error_event_table_v1 table;
	u32 base;

	base = mvm->error_event_table;
	if (mvm->cur_ucode == IWL_UCODE_INIT) {
		if (!base)
			base = mvm->fw->init_errlog_ptr;
	} else {
		if (!base)
			base = mvm->fw->inst_errlog_ptr;
	}

	if (base < 0x800000) {
		IWL_ERR(mvm,
			"Not valid error log pointer 0x%08X for %s uCode\n",
			base,
			(mvm->cur_ucode == IWL_UCODE_INIT)
					? "Init" : "RT");
		return;
	}

	iwl_trans_read_mem_bytes(trans, base, &table, sizeof(table));

	if (ERROR_START_OFFSET <= table.valid * ERROR_ELEM_SIZE) {
		IWL_ERR(trans, "Start IWL Error Log Dump:\n");
		IWL_ERR(trans, "Status: 0x%08lX, count: %d\n",
			mvm->status, table.valid);
	}

	/* Do not change this output - scripts rely on it */

	IWL_ERR(mvm, "Loaded firmware version: %s\n", mvm->fw->fw_version);

	trace_iwlwifi_dev_ucode_error(trans->dev, table.error_id, table.tsf_low,
				      table.data1, table.data2, table.data3,
				      table.blink1, table.blink2, table.ilink1,
				      table.ilink2, table.bcon_time, table.gp1,
				      table.gp2, table.gp3, table.ucode_ver, 0,
				      table.hw_ver, table.brd_ver);
	IWL_ERR(mvm, "0x%08X | %-28s\n", table.error_id,
		desc_lookup(table.error_id));
	IWL_ERR(mvm, "0x%08X | uPc\n", table.pc);
	IWL_ERR(mvm, "0x%08X | branchlink1\n", table.blink1);
	IWL_ERR(mvm, "0x%08X | branchlink2\n", table.blink2);
	IWL_ERR(mvm, "0x%08X | interruptlink1\n", table.ilink1);
	IWL_ERR(mvm, "0x%08X | interruptlink2\n", table.ilink2);
	IWL_ERR(mvm, "0x%08X | data1\n", table.data1);
	IWL_ERR(mvm, "0x%08X | data2\n", table.data2);
	IWL_ERR(mvm, "0x%08X | data3\n", table.data3);
	IWL_ERR(mvm, "0x%08X | beacon time\n", table.bcon_time);
	IWL_ERR(mvm, "0x%08X | tsf low\n", table.tsf_low);
	IWL_ERR(mvm, "0x%08X | tsf hi\n", table.tsf_hi);
	IWL_ERR(mvm, "0x%08X | time gp1\n", table.gp1);
	IWL_ERR(mvm, "0x%08X | time gp2\n", table.gp2);
	IWL_ERR(mvm, "0x%08X | time gp3\n", table.gp3);
	IWL_ERR(mvm, "0x%08X | uCode version\n", table.ucode_ver);
	IWL_ERR(mvm, "0x%08X | hw version\n", table.hw_ver);
	IWL_ERR(mvm, "0x%08X | board version\n", table.brd_ver);
	IWL_ERR(mvm, "0x%08X | hcmd\n", table.hcmd);
	IWL_ERR(mvm, "0x%08X | isr0\n", table.isr0);
	IWL_ERR(mvm, "0x%08X | isr1\n", table.isr1);
	IWL_ERR(mvm, "0x%08X | isr2\n", table.isr2);
	IWL_ERR(mvm, "0x%08X | isr3\n", table.isr3);
	IWL_ERR(mvm, "0x%08X | isr4\n", table.isr4);
	IWL_ERR(mvm, "0x%08X | isr_pref\n", table.isr_pref);
	IWL_ERR(mvm, "0x%08X | wait_event\n", table.wait_event);
	IWL_ERR(mvm, "0x%08X | l2p_control\n", table.l2p_control);
	IWL_ERR(mvm, "0x%08X | l2p_duration\n", table.l2p_duration);
	IWL_ERR(mvm, "0x%08X | l2p_mhvalid\n", table.l2p_mhvalid);
	IWL_ERR(mvm, "0x%08X | l2p_addr_match\n", table.l2p_addr_match);
	IWL_ERR(mvm, "0x%08X | lmpm_pmg_sel\n", table.lmpm_pmg_sel);
	IWL_ERR(mvm, "0x%08X | timestamp\n", table.u_timestamp);
	IWL_ERR(mvm, "0x%08X | flow_handler\n", table.flow_handler);

	if (mvm->support_umac_log)
		iwl_mvm_dump_umac_error_log(mvm);
}

void iwl_mvm_dump_nic_error_log(struct iwl_mvm *mvm)
{
	struct iwl_trans *trans = mvm->trans;
	struct iwl_error_event_table table;
	u32 base;

	if (!fw_has_api(&mvm->fw->ucode_capa, IWL_UCODE_TLV_API_NEW_VERSION)) {
		iwl_mvm_dump_nic_error_log_old(mvm);
		return;
	}

	base = mvm->error_event_table;
	if (mvm->cur_ucode == IWL_UCODE_INIT) {
		if (!base)
			base = mvm->fw->init_errlog_ptr;
	} else {
		if (!base)
			base = mvm->fw->inst_errlog_ptr;
	}

	if (base < 0x800000) {
		IWL_ERR(mvm,
			"Not valid error log pointer 0x%08X for %s uCode\n",
			base,
			(mvm->cur_ucode == IWL_UCODE_INIT)
					? "Init" : "RT");
		return;
	}

	iwl_trans_read_mem_bytes(trans, base, &table, sizeof(table));

	if (ERROR_START_OFFSET <= table.valid * ERROR_ELEM_SIZE) {
		IWL_ERR(trans, "Start IWL Error Log Dump:\n");
		IWL_ERR(trans, "Status: 0x%08lX, count: %d\n",
			mvm->status, table.valid);
	}

	/* Do not change this output - scripts rely on it */

	IWL_ERR(mvm, "Loaded firmware version: %s\n", mvm->fw->fw_version);

	trace_iwlwifi_dev_ucode_error(trans->dev, table.error_id, table.tsf_low,
				      table.data1, table.data2, table.data3,
				      table.blink1, table.blink2, table.ilink1,
				      table.ilink2, table.bcon_time, table.gp1,
				      table.gp2, table.gp3, table.major,
				      table.minor, table.hw_ver, table.brd_ver);
	IWL_ERR(mvm, "0x%08X | %-28s\n", table.error_id,
		desc_lookup(table.error_id));
	IWL_ERR(mvm, "0x%08X | uPc\n", table.pc);
	IWL_ERR(mvm, "0x%08X | branchlink1\n", table.blink1);
	IWL_ERR(mvm, "0x%08X | branchlink2\n", table.blink2);
	IWL_ERR(mvm, "0x%08X | interruptlink1\n", table.ilink1);
	IWL_ERR(mvm, "0x%08X | interruptlink2\n", table.ilink2);
	IWL_ERR(mvm, "0x%08X | data1\n", table.data1);
	IWL_ERR(mvm, "0x%08X | data2\n", table.data2);
	IWL_ERR(mvm, "0x%08X | data3\n", table.data3);
	IWL_ERR(mvm, "0x%08X | beacon time\n", table.bcon_time);
	IWL_ERR(mvm, "0x%08X | tsf low\n", table.tsf_low);
	IWL_ERR(mvm, "0x%08X | tsf hi\n", table.tsf_hi);
	IWL_ERR(mvm, "0x%08X | time gp1\n", table.gp1);
	IWL_ERR(mvm, "0x%08X | time gp2\n", table.gp2);
	IWL_ERR(mvm, "0x%08X | time gp3\n", table.gp3);
	IWL_ERR(mvm, "0x%08X | uCode version major\n", table.major);
	IWL_ERR(mvm, "0x%08X | uCode version minor\n", table.minor);
	IWL_ERR(mvm, "0x%08X | hw version\n", table.hw_ver);
	IWL_ERR(mvm, "0x%08X | board version\n", table.brd_ver);
	IWL_ERR(mvm, "0x%08X | hcmd\n", table.hcmd);
	IWL_ERR(mvm, "0x%08X | isr0\n", table.isr0);
	IWL_ERR(mvm, "0x%08X | isr1\n", table.isr1);
	IWL_ERR(mvm, "0x%08X | isr2\n", table.isr2);
	IWL_ERR(mvm, "0x%08X | isr3\n", table.isr3);
	IWL_ERR(mvm, "0x%08X | isr4\n", table.isr4);
	IWL_ERR(mvm, "0x%08X | isr_pref\n", table.isr_pref);
	IWL_ERR(mvm, "0x%08X | wait_event\n", table.wait_event);
	IWL_ERR(mvm, "0x%08X | l2p_control\n", table.l2p_control);
	IWL_ERR(mvm, "0x%08X | l2p_duration\n", table.l2p_duration);
	IWL_ERR(mvm, "0x%08X | l2p_mhvalid\n", table.l2p_mhvalid);
	IWL_ERR(mvm, "0x%08X | l2p_addr_match\n", table.l2p_addr_match);
	IWL_ERR(mvm, "0x%08X | lmpm_pmg_sel\n", table.lmpm_pmg_sel);
	IWL_ERR(mvm, "0x%08X | timestamp\n", table.u_timestamp);
	IWL_ERR(mvm, "0x%08X | flow_handler\n", table.flow_handler);

	if (mvm->support_umac_log)
		iwl_mvm_dump_umac_error_log(mvm);
}
void iwl_mvm_enable_txq(struct iwl_mvm *mvm, int queue, u16 ssn,
			const struct iwl_trans_txq_scd_cfg *cfg,
			unsigned int wdg_timeout)
{
	struct iwl_scd_txq_cfg_cmd cmd = {
		.scd_queue = queue,
		.enable = 1,
		.window = cfg->frame_limit,
		.sta_id = cfg->sta_id,
		.ssn = cpu_to_le16(ssn),
		.tx_fifo = cfg->fifo,
		.aggregate = cfg->aggregate,
		.tid = cfg->tid,
	};

	if (!iwl_mvm_is_scd_cfg_supported(mvm)) {
		iwl_trans_txq_enable_cfg(mvm->trans, queue, ssn, cfg,
					 wdg_timeout);
		return;
	}

	iwl_trans_txq_enable_cfg(mvm->trans, queue, ssn, NULL, wdg_timeout);
	WARN(iwl_mvm_send_cmd_pdu(mvm, SCD_QUEUE_CFG, 0, sizeof(cmd), &cmd),
	     "Failed to configure queue %d on FIFO %d\n", queue, cfg->fifo);
}

void iwl_mvm_disable_txq(struct iwl_mvm *mvm, int queue, u8 flags)
{
	struct iwl_scd_txq_cfg_cmd cmd = {
		.scd_queue = queue,
		.enable = 0,
	};
	int ret;

	if (!iwl_mvm_is_scd_cfg_supported(mvm)) {
		iwl_trans_txq_disable(mvm->trans, queue, true);
		return;
	}

	iwl_trans_txq_disable(mvm->trans, queue, false);
	ret = iwl_mvm_send_cmd_pdu(mvm, SCD_QUEUE_CFG, flags,
				   sizeof(cmd), &cmd);
	if (ret)
		IWL_ERR(mvm, "Failed to disable queue %d (ret=%d)\n",
			queue, ret);
}

/**
 * iwl_mvm_send_lq_cmd() - Send link quality command
 * @init: This command is sent as part of station initialization right
 *        after station has been added.
 *
 * The link quality command is sent as the last step of station creation.
 * This is the special case in which init is set and we call a callback in
 * this case to clear the state indicating that station creation is in
 * progress.
 */
int iwl_mvm_send_lq_cmd(struct iwl_mvm *mvm, struct iwl_lq_cmd *lq, bool init)
{
	struct iwl_host_cmd cmd = {
		.id = LQ_CMD,
		.len = { sizeof(struct iwl_lq_cmd), },
		.flags = init ? 0 : CMD_ASYNC,
		.data = { lq, },
	};

	if (WARN_ON(lq->sta_id == IWL_MVM_STATION_COUNT))
		return -EINVAL;

	return iwl_mvm_send_cmd(mvm, &cmd);
}

/**
 * iwl_mvm_update_smps - Get a request to change the SMPS mode
 * @req_type: The part of the driver who call for a change.
 * @smps_requests: The request to change the SMPS mode.
 *
 * Get a requst to change the SMPS mode,
 * and change it according to all other requests in the driver.
 */
void iwl_mvm_update_smps(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			 enum iwl_mvm_smps_type_request req_type,
			 enum ieee80211_smps_mode smps_request)
{
	struct iwl_mvm_vif *mvmvif;
	enum ieee80211_smps_mode smps_mode;
	int i;

	lockdep_assert_held(&mvm->mutex);

	/* SMPS is irrelevant for NICs that don't have at least 2 RX antenna */
	if (num_of_ant(iwl_mvm_get_valid_rx_ant(mvm)) == 1)
		return;

	if (vif->type == NL80211_IFTYPE_AP)
		smps_mode = IEEE80211_SMPS_OFF;
	else
		smps_mode = IEEE80211_SMPS_AUTOMATIC;

	mvmvif = iwl_mvm_vif_from_mac80211(vif);
	mvmvif->smps_requests[req_type] = smps_request;
	for (i = 0; i < NUM_IWL_MVM_SMPS_REQ; i++) {
		if (mvmvif->smps_requests[i] == IEEE80211_SMPS_STATIC) {
			smps_mode = IEEE80211_SMPS_STATIC;
			break;
		}
		if (mvmvif->smps_requests[i] == IEEE80211_SMPS_DYNAMIC)
			smps_mode = IEEE80211_SMPS_DYNAMIC;
	}

	ieee80211_request_smps(vif, smps_mode);
}

int iwl_mvm_request_statistics(struct iwl_mvm *mvm, bool clear)
{
	struct iwl_statistics_cmd scmd = {
		.flags = clear ? cpu_to_le32(IWL_STATISTICS_FLG_CLEAR) : 0,
	};
	struct iwl_host_cmd cmd = {
		.id = STATISTICS_CMD,
		.len[0] = sizeof(scmd),
		.data[0] = &scmd,
		.flags = CMD_WANT_SKB,
	};
	int ret;

	ret = iwl_mvm_send_cmd(mvm, &cmd);
	if (ret)
		return ret;

	iwl_mvm_handle_rx_statistics(mvm, cmd.resp_pkt);
	iwl_free_resp(&cmd);

	if (clear)
		iwl_mvm_accu_radio_stats(mvm);

	return 0;
}

void iwl_mvm_accu_radio_stats(struct iwl_mvm *mvm)
{
	mvm->accu_radio_stats.rx_time += mvm->radio_stats.rx_time;
	mvm->accu_radio_stats.tx_time += mvm->radio_stats.tx_time;
	mvm->accu_radio_stats.on_time_rf += mvm->radio_stats.on_time_rf;
	mvm->accu_radio_stats.on_time_scan += mvm->radio_stats.on_time_scan;
}

static void iwl_mvm_diversity_iter(void *_data, u8 *mac,
				   struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	bool *result = _data;
	int i;

	for (i = 0; i < NUM_IWL_MVM_SMPS_REQ; i++) {
		if (mvmvif->smps_requests[i] == IEEE80211_SMPS_STATIC ||
		    mvmvif->smps_requests[i] == IEEE80211_SMPS_DYNAMIC)
			*result = false;
	}
}

bool iwl_mvm_rx_diversity_allowed(struct iwl_mvm *mvm)
{
	bool result = true;

	lockdep_assert_held(&mvm->mutex);

	if (num_of_ant(iwl_mvm_get_valid_rx_ant(mvm)) == 1)
		return false;

	if (mvm->cfg->rx_with_siso_diversity)
		return false;

	ieee80211_iterate_active_interfaces_atomic(
			mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
			iwl_mvm_diversity_iter, &result);

	return result;
}

int iwl_mvm_update_low_latency(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			       bool value)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int res;

	lockdep_assert_held(&mvm->mutex);

	if (mvmvif->low_latency == value)
		return 0;

	mvmvif->low_latency = value;

	res = iwl_mvm_update_quotas(mvm, false, NULL);
	if (res)
		return res;

	iwl_mvm_bt_coex_vif_change(mvm);

	return iwl_mvm_power_update_mac(mvm);
}

static void iwl_mvm_ll_iter(void *_data, u8 *mac, struct ieee80211_vif *vif)
{
	bool *result = _data;

	if (iwl_mvm_vif_low_latency(iwl_mvm_vif_from_mac80211(vif)))
		*result = true;
}

bool iwl_mvm_low_latency(struct iwl_mvm *mvm)
{
	bool result = false;

	ieee80211_iterate_active_interfaces_atomic(
			mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
			iwl_mvm_ll_iter, &result);

	return result;
}

struct iwl_bss_iter_data {
	struct ieee80211_vif *vif;
	bool error;
};

static void iwl_mvm_bss_iface_iterator(void *_data, u8 *mac,
				       struct ieee80211_vif *vif)
{
	struct iwl_bss_iter_data *data = _data;

	if (vif->type != NL80211_IFTYPE_STATION || vif->p2p)
		return;

	if (data->vif) {
		data->error = true;
		return;
	}

	data->vif = vif;
}

struct ieee80211_vif *iwl_mvm_get_bss_vif(struct iwl_mvm *mvm)
{
	struct iwl_bss_iter_data bss_iter_data = {};

	ieee80211_iterate_active_interfaces_atomic(
		mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
		iwl_mvm_bss_iface_iterator, &bss_iter_data);

	if (bss_iter_data.error) {
		IWL_ERR(mvm, "More than one managed interface active!\n");
		return ERR_PTR(-EINVAL);
	}

	return bss_iter_data.vif;
}

unsigned int iwl_mvm_get_wd_timeout(struct iwl_mvm *mvm,
				    struct ieee80211_vif *vif,
				    bool tdls, bool cmd_q)
{
	struct iwl_fw_dbg_trigger_tlv *trigger;
	struct iwl_fw_dbg_trigger_txq_timer *txq_timer;
	unsigned int default_timeout =
		cmd_q ? IWL_DEF_WD_TIMEOUT : mvm->cfg->base_params->wd_timeout;

	if (!iwl_fw_dbg_trigger_enabled(mvm->fw, FW_DBG_TRIGGER_TXQ_TIMERS))
		return iwlmvm_mod_params.tfd_q_hang_detect ?
			default_timeout : IWL_WATCHDOG_DISABLED;

	trigger = iwl_fw_dbg_get_trigger(mvm->fw, FW_DBG_TRIGGER_TXQ_TIMERS);
	txq_timer = (void *)trigger->data;

	if (tdls)
		return le32_to_cpu(txq_timer->tdls);

	if (cmd_q)
		return le32_to_cpu(txq_timer->command_queue);

	if (WARN_ON(!vif))
		return default_timeout;

	switch (ieee80211_vif_type_p2p(vif)) {
	case NL80211_IFTYPE_ADHOC:
		return le32_to_cpu(txq_timer->ibss);
	case NL80211_IFTYPE_STATION:
		return le32_to_cpu(txq_timer->bss);
	case NL80211_IFTYPE_AP:
		return le32_to_cpu(txq_timer->softap);
	case NL80211_IFTYPE_P2P_CLIENT:
		return le32_to_cpu(txq_timer->p2p_client);
	case NL80211_IFTYPE_P2P_GO:
		return le32_to_cpu(txq_timer->p2p_go);
	case NL80211_IFTYPE_P2P_DEVICE:
		return le32_to_cpu(txq_timer->p2p_device);
	default:
		WARN_ON(1);
		return mvm->cfg->base_params->wd_timeout;
	}
}

void iwl_mvm_connection_loss(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			     const char *errmsg)
{
	struct iwl_fw_dbg_trigger_tlv *trig;
	struct iwl_fw_dbg_trigger_mlme *trig_mlme;

	if (!iwl_fw_dbg_trigger_enabled(mvm->fw, FW_DBG_TRIGGER_MLME))
		goto out;

	trig = iwl_fw_dbg_get_trigger(mvm->fw, FW_DBG_TRIGGER_MLME);
	trig_mlme = (void *)trig->data;
	if (!iwl_fw_dbg_trigger_check_stop(mvm, vif, trig))
		goto out;

	if (trig_mlme->stop_connection_loss &&
	    --trig_mlme->stop_connection_loss)
		goto out;

	iwl_mvm_fw_dbg_collect_trig(mvm, trig, "%s", errmsg);

out:
	ieee80211_connection_loss(vif);
}
