// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2012-2014, 2018-2023 Intel Corporation
 * Copyright (C) 2013-2014 Intel Mobile Communications GmbH
 * Copyright (C) 2015-2017 Intel Deutschland GmbH
 */
#include <linux/devcoredump.h>
#include "iwl-drv.h"
#include "runtime.h"
#include "dbg.h"
#include "debugfs.h"
#include "iwl-io.h"
#include "iwl-prph.h"
#include "iwl-csr.h"
#include "pnvm.h"

#define FW_ASSERT_LMAC_FATAL			0x70
#define FW_ASSERT_LMAC2_FATAL			0x72
#define FW_ASSERT_UMAC_FATAL			0x71
#define UMAC_RT_NMI_LMAC2_FATAL			0x72
#define RT_NMI_INTERRUPT_OTHER_LMAC_FATAL	0x73
#define FW_ASSERT_NMI_UNKNOWN			0x84

/*
 * Note: This structure is read from the device with IO accesses,
 * and the reading already does the endian conversion. As it is
 * read with u32-sized accesses, any members with a different size
 * need to be ordered correctly though!
 */
struct iwl_error_event_table {
	u32 valid;		/* (nonzero) valid, (0) log is empty */
	u32 error_id;		/* type of error */
	u32 trm_hw_status0;	/* TRM HW status */
	u32 trm_hw_status1;	/* TRM HW status */
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
	u32 fw_rev_type;	/* firmware revision type */
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
	u32 last_cmd_id;	/* last HCMD id handled by the firmware */
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
} __packed /* LOG_ERROR_TABLE_API_S_VER_3 */;

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

static bool iwl_fwrt_if_errorid_other_cpu(u32 err_id)
{
	err_id &= 0xFF;

	if ((err_id >= FW_ASSERT_LMAC_FATAL &&
	     err_id <= RT_NMI_INTERRUPT_OTHER_LMAC_FATAL) ||
	    err_id == FW_ASSERT_NMI_UNKNOWN)
		return  true;
	return false;
}

static void iwl_fwrt_dump_umac_error_log(struct iwl_fw_runtime *fwrt)
{
	struct iwl_trans *trans = fwrt->trans;
	struct iwl_umac_error_event_table table = {};
	u32 base = fwrt->trans->dbg.umac_error_event_table;
	char pnvm_name[MAX_PNVM_NAME];

	if (!base &&
	    !(fwrt->trans->dbg.error_event_table_tlv_status &
	      IWL_ERROR_EVENT_TABLE_UMAC))
		return;

	iwl_trans_read_mem_bytes(trans, base, &table, sizeof(table));

	if (table.valid)
		fwrt->dump.umac_err_id = table.error_id;

	if (!iwl_fwrt_if_errorid_other_cpu(fwrt->dump.umac_err_id) &&
	    !fwrt->trans->dbg.dump_file_name_ext_valid) {
		fwrt->trans->dbg.dump_file_name_ext_valid = true;
		snprintf(fwrt->trans->dbg.dump_file_name_ext, IWL_FW_INI_MAX_NAME,
			 "0x%x", fwrt->dump.umac_err_id);
	}

	if (ERROR_START_OFFSET <= table.valid * ERROR_ELEM_SIZE) {
		IWL_ERR(trans, "Start IWL Error Log Dump:\n");
		IWL_ERR(trans, "Transport status: 0x%08lX, valid: %d\n",
			fwrt->trans->status, table.valid);
	}

	if ((table.error_id & ~FW_SYSASSERT_CPU_MASK) ==
	    FW_SYSASSERT_PNVM_MISSING) {
		iwl_pnvm_get_fs_name(trans, pnvm_name, sizeof(pnvm_name));
		IWL_ERR(fwrt, "PNVM data is missing, please install %s\n",
			pnvm_name);
	}

	IWL_ERR(fwrt, "0x%08X | %s\n", table.error_id,
		iwl_fw_lookup_assert_desc(table.error_id));
	IWL_ERR(fwrt, "0x%08X | umac branchlink1\n", table.blink1);
	IWL_ERR(fwrt, "0x%08X | umac branchlink2\n", table.blink2);
	IWL_ERR(fwrt, "0x%08X | umac interruptlink1\n", table.ilink1);
	IWL_ERR(fwrt, "0x%08X | umac interruptlink2\n", table.ilink2);
	IWL_ERR(fwrt, "0x%08X | umac data1\n", table.data1);
	IWL_ERR(fwrt, "0x%08X | umac data2\n", table.data2);
	IWL_ERR(fwrt, "0x%08X | umac data3\n", table.data3);
	IWL_ERR(fwrt, "0x%08X | umac major\n", table.umac_major);
	IWL_ERR(fwrt, "0x%08X | umac minor\n", table.umac_minor);
	IWL_ERR(fwrt, "0x%08X | frame pointer\n", table.frame_pointer);
	IWL_ERR(fwrt, "0x%08X | stack pointer\n", table.stack_pointer);
	IWL_ERR(fwrt, "0x%08X | last host cmd\n", table.cmd_header);
	IWL_ERR(fwrt, "0x%08X | isr status reg\n", table.nic_isr_pref);
}

static void iwl_fwrt_dump_lmac_error_log(struct iwl_fw_runtime *fwrt, u8 lmac_num)
{
	struct iwl_trans *trans = fwrt->trans;
	struct iwl_error_event_table table = {};
	u32 val, base = fwrt->trans->dbg.lmac_error_event_table[lmac_num];

	if (fwrt->cur_fw_img == IWL_UCODE_INIT) {
		if (!base)
			base = fwrt->fw->init_errlog_ptr;
	} else {
		if (!base)
			base = fwrt->fw->inst_errlog_ptr;
	}

	if (!base) {
		IWL_ERR(fwrt,
			"Not valid error log pointer 0x%08X for %s uCode\n",
			base,
			(fwrt->cur_fw_img == IWL_UCODE_INIT)
			? "Init" : "RT");
		return;
	}

	/* check if there is a HW error */
	val = iwl_trans_read_mem32(trans, base);
	if (iwl_trans_is_hw_error_value(val)) {
		int err;

		IWL_ERR(trans, "HW error, resetting before reading\n");

		/* reset the device */
		err = iwl_trans_sw_reset(trans, true);
		if (err)
			return;

		err = iwl_finish_nic_init(trans);
		if (err)
			return;
	}

	iwl_trans_read_mem_bytes(trans, base, &table, sizeof(table));

	if (table.valid)
		fwrt->dump.lmac_err_id[lmac_num] = table.error_id;

	if (!iwl_fwrt_if_errorid_other_cpu(fwrt->dump.lmac_err_id[lmac_num]) &&
	    !fwrt->trans->dbg.dump_file_name_ext_valid) {
		fwrt->trans->dbg.dump_file_name_ext_valid = true;
		snprintf(fwrt->trans->dbg.dump_file_name_ext, IWL_FW_INI_MAX_NAME,
			 "0x%x", fwrt->dump.lmac_err_id[lmac_num]);
	}

	if (ERROR_START_OFFSET <= table.valid * ERROR_ELEM_SIZE) {
		IWL_ERR(trans, "Start IWL Error Log Dump:\n");
		IWL_ERR(trans, "Transport status: 0x%08lX, valid: %d\n",
			fwrt->trans->status, table.valid);
	}

	/* Do not change this output - scripts rely on it */

	IWL_ERR(fwrt, "Loaded firmware version: %s\n", fwrt->fw->fw_version);

	IWL_ERR(fwrt, "0x%08X | %-28s\n", table.error_id,
		iwl_fw_lookup_assert_desc(table.error_id));
	IWL_ERR(fwrt, "0x%08X | trm_hw_status0\n", table.trm_hw_status0);
	IWL_ERR(fwrt, "0x%08X | trm_hw_status1\n", table.trm_hw_status1);
	IWL_ERR(fwrt, "0x%08X | branchlink2\n", table.blink2);
	IWL_ERR(fwrt, "0x%08X | interruptlink1\n", table.ilink1);
	IWL_ERR(fwrt, "0x%08X | interruptlink2\n", table.ilink2);
	IWL_ERR(fwrt, "0x%08X | data1\n", table.data1);
	IWL_ERR(fwrt, "0x%08X | data2\n", table.data2);
	IWL_ERR(fwrt, "0x%08X | data3\n", table.data3);
	IWL_ERR(fwrt, "0x%08X | beacon time\n", table.bcon_time);
	IWL_ERR(fwrt, "0x%08X | tsf low\n", table.tsf_low);
	IWL_ERR(fwrt, "0x%08X | tsf hi\n", table.tsf_hi);
	IWL_ERR(fwrt, "0x%08X | time gp1\n", table.gp1);
	IWL_ERR(fwrt, "0x%08X | time gp2\n", table.gp2);
	IWL_ERR(fwrt, "0x%08X | uCode revision type\n", table.fw_rev_type);
	IWL_ERR(fwrt, "0x%08X | uCode version major\n", table.major);
	IWL_ERR(fwrt, "0x%08X | uCode version minor\n", table.minor);
	IWL_ERR(fwrt, "0x%08X | hw version\n", table.hw_ver);
	IWL_ERR(fwrt, "0x%08X | board version\n", table.brd_ver);
	IWL_ERR(fwrt, "0x%08X | hcmd\n", table.hcmd);
	IWL_ERR(fwrt, "0x%08X | isr0\n", table.isr0);
	IWL_ERR(fwrt, "0x%08X | isr1\n", table.isr1);
	IWL_ERR(fwrt, "0x%08X | isr2\n", table.isr2);
	IWL_ERR(fwrt, "0x%08X | isr3\n", table.isr3);
	IWL_ERR(fwrt, "0x%08X | isr4\n", table.isr4);
	IWL_ERR(fwrt, "0x%08X | last cmd Id\n", table.last_cmd_id);
	IWL_ERR(fwrt, "0x%08X | wait_event\n", table.wait_event);
	IWL_ERR(fwrt, "0x%08X | l2p_control\n", table.l2p_control);
	IWL_ERR(fwrt, "0x%08X | l2p_duration\n", table.l2p_duration);
	IWL_ERR(fwrt, "0x%08X | l2p_mhvalid\n", table.l2p_mhvalid);
	IWL_ERR(fwrt, "0x%08X | l2p_addr_match\n", table.l2p_addr_match);
	IWL_ERR(fwrt, "0x%08X | lmpm_pmg_sel\n", table.lmpm_pmg_sel);
	IWL_ERR(fwrt, "0x%08X | timestamp\n", table.u_timestamp);
	IWL_ERR(fwrt, "0x%08X | flow_handler\n", table.flow_handler);
}

/*
 * TCM error struct.
 * Note: This structure is read from the device with IO accesses,
 * and the reading already does the endian conversion. As it is
 * read with u32-sized accesses, any members with a different size
 * need to be ordered correctly though!
 */
struct iwl_tcm_error_event_table {
	u32 valid;
	u32 error_id;
	u32 blink2;
	u32 ilink1;
	u32 ilink2;
	u32 data1, data2, data3;
	u32 logpc;
	u32 frame_pointer;
	u32 stack_pointer;
	u32 msgid;
	u32 isr;
	u32 hw_status[5];
	u32 sw_status[1];
	u32 reserved[4];
} __packed; /* TCM_LOG_ERROR_TABLE_API_S_VER_1 */

static void iwl_fwrt_dump_tcm_error_log(struct iwl_fw_runtime *fwrt, int idx)
{
	struct iwl_trans *trans = fwrt->trans;
	struct iwl_tcm_error_event_table table = {};
	u32 base = fwrt->trans->dbg.tcm_error_event_table[idx];
	int i;
	u32 flag = idx ? IWL_ERROR_EVENT_TABLE_TCM2 :
			 IWL_ERROR_EVENT_TABLE_TCM1;

	if (!base || !(fwrt->trans->dbg.error_event_table_tlv_status & flag))
		return;

	iwl_trans_read_mem_bytes(trans, base, &table, sizeof(table));

	if (table.valid)
		fwrt->dump.tcm_err_id[idx] = table.error_id;

	if (!iwl_fwrt_if_errorid_other_cpu(fwrt->dump.tcm_err_id[idx]) &&
	    !fwrt->trans->dbg.dump_file_name_ext_valid) {
		fwrt->trans->dbg.dump_file_name_ext_valid = true;
		snprintf(fwrt->trans->dbg.dump_file_name_ext, IWL_FW_INI_MAX_NAME,
			 "0x%x", fwrt->dump.tcm_err_id[idx]);
	}

	IWL_ERR(fwrt, "TCM%d status:\n", idx + 1);
	IWL_ERR(fwrt, "0x%08X | error ID\n", table.error_id);
	IWL_ERR(fwrt, "0x%08X | tcm branchlink2\n", table.blink2);
	IWL_ERR(fwrt, "0x%08X | tcm interruptlink1\n", table.ilink1);
	IWL_ERR(fwrt, "0x%08X | tcm interruptlink2\n", table.ilink2);
	IWL_ERR(fwrt, "0x%08X | tcm data1\n", table.data1);
	IWL_ERR(fwrt, "0x%08X | tcm data2\n", table.data2);
	IWL_ERR(fwrt, "0x%08X | tcm data3\n", table.data3);
	IWL_ERR(fwrt, "0x%08X | tcm log PC\n", table.logpc);
	IWL_ERR(fwrt, "0x%08X | tcm frame pointer\n", table.frame_pointer);
	IWL_ERR(fwrt, "0x%08X | tcm stack pointer\n", table.stack_pointer);
	IWL_ERR(fwrt, "0x%08X | tcm msg ID\n", table.msgid);
	IWL_ERR(fwrt, "0x%08X | tcm ISR status\n", table.isr);
	for (i = 0; i < ARRAY_SIZE(table.hw_status); i++)
		IWL_ERR(fwrt, "0x%08X | tcm HW status[%d]\n",
			table.hw_status[i], i);
	for (i = 0; i < ARRAY_SIZE(table.sw_status); i++)
		IWL_ERR(fwrt, "0x%08X | tcm SW status[%d]\n",
			table.sw_status[i], i);
}

/*
 * RCM error struct.
 * Note: This structure is read from the device with IO accesses,
 * and the reading already does the endian conversion. As it is
 * read with u32-sized accesses, any members with a different size
 * need to be ordered correctly though!
 */
struct iwl_rcm_error_event_table {
	u32 valid;
	u32 error_id;
	u32 blink2;
	u32 ilink1;
	u32 ilink2;
	u32 data1, data2, data3;
	u32 logpc;
	u32 frame_pointer;
	u32 stack_pointer;
	u32 msgid;
	u32 isr;
	u32 frame_hw_status;
	u32 mbx_lmac_to_rcm_req;
	u32 mbx_rcm_to_lmac_req;
	u32 mh_ctl;
	u32 mh_addr1_lo;
	u32 mh_info;
	u32 mh_err;
	u32 reserved[3];
} __packed; /* RCM_LOG_ERROR_TABLE_API_S_VER_1 */

static void iwl_fwrt_dump_rcm_error_log(struct iwl_fw_runtime *fwrt, int idx)
{
	struct iwl_trans *trans = fwrt->trans;
	struct iwl_rcm_error_event_table table = {};
	u32 base = fwrt->trans->dbg.rcm_error_event_table[idx];
	u32 flag = idx ? IWL_ERROR_EVENT_TABLE_RCM2 :
			 IWL_ERROR_EVENT_TABLE_RCM1;

	if (!base || !(fwrt->trans->dbg.error_event_table_tlv_status & flag))
		return;

	iwl_trans_read_mem_bytes(trans, base, &table, sizeof(table));

	if (table.valid)
		fwrt->dump.rcm_err_id[idx] = table.error_id;

	if (!iwl_fwrt_if_errorid_other_cpu(fwrt->dump.rcm_err_id[idx]) &&
	    !fwrt->trans->dbg.dump_file_name_ext_valid) {
		fwrt->trans->dbg.dump_file_name_ext_valid = true;
		snprintf(fwrt->trans->dbg.dump_file_name_ext, IWL_FW_INI_MAX_NAME,
			 "0x%x", fwrt->dump.rcm_err_id[idx]);
	}

	IWL_ERR(fwrt, "RCM%d status:\n", idx + 1);
	IWL_ERR(fwrt, "0x%08X | error ID\n", table.error_id);
	IWL_ERR(fwrt, "0x%08X | rcm branchlink2\n", table.blink2);
	IWL_ERR(fwrt, "0x%08X | rcm interruptlink1\n", table.ilink1);
	IWL_ERR(fwrt, "0x%08X | rcm interruptlink2\n", table.ilink2);
	IWL_ERR(fwrt, "0x%08X | rcm data1\n", table.data1);
	IWL_ERR(fwrt, "0x%08X | rcm data2\n", table.data2);
	IWL_ERR(fwrt, "0x%08X | rcm data3\n", table.data3);
	IWL_ERR(fwrt, "0x%08X | rcm log PC\n", table.logpc);
	IWL_ERR(fwrt, "0x%08X | rcm frame pointer\n", table.frame_pointer);
	IWL_ERR(fwrt, "0x%08X | rcm stack pointer\n", table.stack_pointer);
	IWL_ERR(fwrt, "0x%08X | rcm msg ID\n", table.msgid);
	IWL_ERR(fwrt, "0x%08X | rcm ISR status\n", table.isr);
	IWL_ERR(fwrt, "0x%08X | frame HW status\n", table.frame_hw_status);
	IWL_ERR(fwrt, "0x%08X | LMAC-to-RCM request mbox\n",
		table.mbx_lmac_to_rcm_req);
	IWL_ERR(fwrt, "0x%08X | RCM-to-LMAC request mbox\n",
		table.mbx_rcm_to_lmac_req);
	IWL_ERR(fwrt, "0x%08X | MAC header control\n", table.mh_ctl);
	IWL_ERR(fwrt, "0x%08X | MAC header addr1 low\n", table.mh_addr1_lo);
	IWL_ERR(fwrt, "0x%08X | MAC header info\n", table.mh_info);
	IWL_ERR(fwrt, "0x%08X | MAC header error\n", table.mh_err);
}

static void iwl_fwrt_dump_iml_error_log(struct iwl_fw_runtime *fwrt)
{
	struct iwl_trans *trans = fwrt->trans;
	u32 error, data1;

	if (fwrt->trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_22000) {
		error = UMAG_SB_CPU_2_STATUS;
		data1 = UMAG_SB_CPU_1_STATUS;
	} else if (fwrt->trans->trans_cfg->device_family >=
		   IWL_DEVICE_FAMILY_8000) {
		error = SB_CPU_2_STATUS;
		data1 = SB_CPU_1_STATUS;
	} else {
		return;
	}

	error = iwl_read_umac_prph(trans, error);

	IWL_ERR(trans, "IML/ROM dump:\n");

	if (error & 0xFFFF0000)
		IWL_ERR(trans, "0x%04X | IML/ROM SYSASSERT\n", error >> 16);

	IWL_ERR(fwrt, "0x%08X | IML/ROM error/state\n", error);
	IWL_ERR(fwrt, "0x%08X | IML/ROM data1\n",
		iwl_read_umac_prph(trans, data1));

	if (fwrt->trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_22000)
		IWL_ERR(fwrt, "0x%08X | IML/ROM WFPM_AUTH_KEY_0\n",
			iwl_read_umac_prph(trans, SB_MODIFY_CFG_FLAG));
}

#define FSEQ_REG(x) { .addr = (x), .str = #x, }

static void iwl_fwrt_dump_fseq_regs(struct iwl_fw_runtime *fwrt)
{
	struct iwl_trans *trans = fwrt->trans;
	int i;
	struct {
		u32 addr;
		const char *str;
	} fseq_regs[] = {
		FSEQ_REG(FSEQ_ERROR_CODE),
		FSEQ_REG(FSEQ_TOP_INIT_VERSION),
		FSEQ_REG(FSEQ_CNVIO_INIT_VERSION),
		FSEQ_REG(FSEQ_OTP_VERSION),
		FSEQ_REG(FSEQ_TOP_CONTENT_VERSION),
		FSEQ_REG(FSEQ_ALIVE_TOKEN),
		FSEQ_REG(FSEQ_CNVI_ID),
		FSEQ_REG(FSEQ_CNVR_ID),
		FSEQ_REG(CNVI_AUX_MISC_CHIP),
		FSEQ_REG(CNVR_AUX_MISC_CHIP),
		FSEQ_REG(CNVR_SCU_SD_REGS_SD_REG_DIG_DCDC_VTRIM),
		FSEQ_REG(CNVR_SCU_SD_REGS_SD_REG_ACTIVE_VDIG_MIRROR),
		FSEQ_REG(FSEQ_PREV_CNVIO_INIT_VERSION),
		FSEQ_REG(FSEQ_WIFI_FSEQ_VERSION),
		FSEQ_REG(FSEQ_BT_FSEQ_VERSION),
		FSEQ_REG(FSEQ_CLASS_TP_VERSION),
	};

	if (!iwl_trans_grab_nic_access(trans))
		return;

	IWL_ERR(fwrt, "Fseq Registers:\n");

	for (i = 0; i < ARRAY_SIZE(fseq_regs); i++)
		IWL_ERR(fwrt, "0x%08X | %s\n",
			iwl_read_prph_no_grab(trans, fseq_regs[i].addr),
			fseq_regs[i].str);

	iwl_trans_release_nic_access(trans);
}

void iwl_fwrt_dump_error_logs(struct iwl_fw_runtime *fwrt)
{
	struct iwl_pc_data *pc_data;
	u8 count;

	if (!test_bit(STATUS_DEVICE_ENABLED, &fwrt->trans->status)) {
		IWL_ERR(fwrt,
			"DEVICE_ENABLED bit is not set. Aborting dump.\n");
		return;
	}

	iwl_fwrt_dump_lmac_error_log(fwrt, 0);
	if (fwrt->trans->dbg.lmac_error_event_table[1])
		iwl_fwrt_dump_lmac_error_log(fwrt, 1);
	iwl_fwrt_dump_umac_error_log(fwrt);
	iwl_fwrt_dump_tcm_error_log(fwrt, 0);
	iwl_fwrt_dump_rcm_error_log(fwrt, 0);
	if (fwrt->trans->dbg.tcm_error_event_table[1])
		iwl_fwrt_dump_tcm_error_log(fwrt, 1);
	if (fwrt->trans->dbg.rcm_error_event_table[1])
		iwl_fwrt_dump_rcm_error_log(fwrt, 1);
	iwl_fwrt_dump_iml_error_log(fwrt);
	iwl_fwrt_dump_fseq_regs(fwrt);
	if (fwrt->trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_22000) {
		pc_data = fwrt->trans->dbg.pc_data;

		if (!iwl_trans_grab_nic_access(fwrt->trans))
			return;
		for (count = 0; count < fwrt->trans->dbg.num_pc;
		     count++, pc_data++)
			IWL_ERR(fwrt, "%s: 0x%x\n",
				pc_data->pc_name,
				iwl_read_prph_no_grab(fwrt->trans,
						      pc_data->pc_address));
		iwl_trans_release_nic_access(fwrt->trans);
	}

	if (fwrt->trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_BZ) {
		u32 scratch = iwl_read32(fwrt->trans, CSR_FUNC_SCRATCH);

		IWL_ERR(fwrt, "Function Scratch status:\n");
		IWL_ERR(fwrt, "0x%08X | Func Scratch\n", scratch);
	}
}
IWL_EXPORT_SYMBOL(iwl_fwrt_dump_error_logs);
