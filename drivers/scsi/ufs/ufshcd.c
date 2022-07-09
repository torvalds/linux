// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Universal Flash Storage Host controller driver Core
 * Copyright (C) 2011-2013 Samsung India Software Operations
 * Copyright (c) 2013-2016, The Linux Foundation. All rights reserved.
 *
 * Authors:
 *	Santosh Yaraganavi <santosh.sy@samsung.com>
 *	Vinayak Holikatti <h.vinayak@samsung.com>
 */

#include <linux/async.h>
#include <linux/devfreq.h>
#include <linux/nls.h>
#include <linux/of.h>
#include <linux/bitfield.h>
#include <linux/blk-pm.h>
#include <linux/blkdev.h>
#include <scsi/scsi_driver.h>
#include "ufshcd.h"
#include "ufs_quirks.h"
#include "unipro.h"
#include "ufs-sysfs.h"
#include "ufs-debugfs.h"
#include "ufs-fault-injection.h"
#include "ufs_bsg.h"
#include "ufshcd-crypto.h"
#include "ufshpb.h"
#include <asm/unaligned.h>

#define CREATE_TRACE_POINTS
#include <trace/events/ufs.h>

#define UFSHCD_ENABLE_INTRS	(UTP_TRANSFER_REQ_COMPL |\
				 UTP_TASK_REQ_COMPL |\
				 UFSHCD_ERROR_MASK)
/* UIC command timeout, unit: ms */
#define UIC_CMD_TIMEOUT	500

/* NOP OUT retries waiting for NOP IN response */
#define NOP_OUT_RETRIES    10
/* Timeout after 50 msecs if NOP OUT hangs without response */
#define NOP_OUT_TIMEOUT    50 /* msecs */

/* Query request retries */
#define QUERY_REQ_RETRIES 3
/* Query request timeout */
#define QUERY_REQ_TIMEOUT 1500 /* 1.5 seconds */

/* Task management command timeout */
#define TM_CMD_TIMEOUT	100 /* msecs */

/* maximum number of retries for a general UIC command  */
#define UFS_UIC_COMMAND_RETRIES 3

/* maximum number of link-startup retries */
#define DME_LINKSTARTUP_RETRIES 3

/* Maximum retries for Hibern8 enter */
#define UIC_HIBERN8_ENTER_RETRIES 3

/* maximum number of reset retries before giving up */
#define MAX_HOST_RESET_RETRIES 5

/* Expose the flag value from utp_upiu_query.value */
#define MASK_QUERY_UPIU_FLAG_LOC 0xFF

/* Interrupt aggregation default timeout, unit: 40us */
#define INT_AGGR_DEF_TO	0x02

/* default delay of autosuspend: 2000 ms */
#define RPM_AUTOSUSPEND_DELAY_MS 2000

/* Default delay of RPM device flush delayed work */
#define RPM_DEV_FLUSH_RECHECK_WORK_DELAY_MS 5000

/* Default value of wait time before gating device ref clock */
#define UFSHCD_REF_CLK_GATING_WAIT_US 0xFF /* microsecs */

/* Polling time to wait for fDeviceInit */
#define FDEVICEINIT_COMPL_TIMEOUT 1500 /* millisecs */

#define wlun_dev_to_hba(dv) shost_priv(to_scsi_device(dv)->host)

#define ufshcd_toggle_vreg(_dev, _vreg, _on)				\
	({                                                              \
		int _ret;                                               \
		if (_on)                                                \
			_ret = ufshcd_enable_vreg(_dev, _vreg);         \
		else                                                    \
			_ret = ufshcd_disable_vreg(_dev, _vreg);        \
		_ret;                                                   \
	})

#define ufshcd_hex_dump(prefix_str, buf, len) do {                       \
	size_t __len = (len);                                            \
	print_hex_dump(KERN_ERR, prefix_str,                             \
		       __len > 4 ? DUMP_PREFIX_OFFSET : DUMP_PREFIX_NONE,\
		       16, 4, buf, __len, false);                        \
} while (0)

int ufshcd_dump_regs(struct ufs_hba *hba, size_t offset, size_t len,
		     const char *prefix)
{
	u32 *regs;
	size_t pos;

	if (offset % 4 != 0 || len % 4 != 0) /* keep readl happy */
		return -EINVAL;

	regs = kzalloc(len, GFP_ATOMIC);
	if (!regs)
		return -ENOMEM;

	for (pos = 0; pos < len; pos += 4) {
		if (offset == 0 &&
		    pos >= REG_UIC_ERROR_CODE_PHY_ADAPTER_LAYER &&
		    pos <= REG_UIC_ERROR_CODE_DME)
			continue;
		regs[pos / 4] = ufshcd_readl(hba, offset + pos);
	}

	ufshcd_hex_dump(prefix, regs, len);
	kfree(regs);

	return 0;
}
EXPORT_SYMBOL_GPL(ufshcd_dump_regs);

enum {
	UFSHCD_MAX_CHANNEL	= 0,
	UFSHCD_MAX_ID		= 1,
	UFSHCD_NUM_RESERVED	= 1,
	UFSHCD_CMD_PER_LUN	= 32 - UFSHCD_NUM_RESERVED,
	UFSHCD_CAN_QUEUE	= 32 - UFSHCD_NUM_RESERVED,
};

/* UFSHCD error handling flags */
enum {
	UFSHCD_EH_IN_PROGRESS = (1 << 0),
};

/* UFSHCD UIC layer error flags */
enum {
	UFSHCD_UIC_DL_PA_INIT_ERROR = (1 << 0), /* Data link layer error */
	UFSHCD_UIC_DL_NAC_RECEIVED_ERROR = (1 << 1), /* Data link layer error */
	UFSHCD_UIC_DL_TCx_REPLAY_ERROR = (1 << 2), /* Data link layer error */
	UFSHCD_UIC_NL_ERROR = (1 << 3), /* Network layer error */
	UFSHCD_UIC_TL_ERROR = (1 << 4), /* Transport Layer error */
	UFSHCD_UIC_DME_ERROR = (1 << 5), /* DME error */
	UFSHCD_UIC_PA_GENERIC_ERROR = (1 << 6), /* Generic PA error */
};

#define ufshcd_set_eh_in_progress(h) \
	((h)->eh_flags |= UFSHCD_EH_IN_PROGRESS)
#define ufshcd_eh_in_progress(h) \
	((h)->eh_flags & UFSHCD_EH_IN_PROGRESS)
#define ufshcd_clear_eh_in_progress(h) \
	((h)->eh_flags &= ~UFSHCD_EH_IN_PROGRESS)

struct ufs_pm_lvl_states ufs_pm_lvl_states[] = {
	[UFS_PM_LVL_0] = {UFS_ACTIVE_PWR_MODE, UIC_LINK_ACTIVE_STATE},
	[UFS_PM_LVL_1] = {UFS_ACTIVE_PWR_MODE, UIC_LINK_HIBERN8_STATE},
	[UFS_PM_LVL_2] = {UFS_SLEEP_PWR_MODE, UIC_LINK_ACTIVE_STATE},
	[UFS_PM_LVL_3] = {UFS_SLEEP_PWR_MODE, UIC_LINK_HIBERN8_STATE},
	[UFS_PM_LVL_4] = {UFS_POWERDOWN_PWR_MODE, UIC_LINK_HIBERN8_STATE},
	[UFS_PM_LVL_5] = {UFS_POWERDOWN_PWR_MODE, UIC_LINK_OFF_STATE},
	/*
	 * For DeepSleep, the link is first put in hibern8 and then off.
	 * Leaving the link in hibern8 is not supported.
	 */
	[UFS_PM_LVL_6] = {UFS_DEEPSLEEP_PWR_MODE, UIC_LINK_OFF_STATE},
};

static inline enum ufs_dev_pwr_mode
ufs_get_pm_lvl_to_dev_pwr_mode(enum ufs_pm_level lvl)
{
	return ufs_pm_lvl_states[lvl].dev_state;
}

static inline enum uic_link_state
ufs_get_pm_lvl_to_link_pwr_state(enum ufs_pm_level lvl)
{
	return ufs_pm_lvl_states[lvl].link_state;
}

static inline enum ufs_pm_level
ufs_get_desired_pm_lvl_for_dev_link_state(enum ufs_dev_pwr_mode dev_state,
					enum uic_link_state link_state)
{
	enum ufs_pm_level lvl;

	for (lvl = UFS_PM_LVL_0; lvl < UFS_PM_LVL_MAX; lvl++) {
		if ((ufs_pm_lvl_states[lvl].dev_state == dev_state) &&
			(ufs_pm_lvl_states[lvl].link_state == link_state))
			return lvl;
	}

	/* if no match found, return the level 0 */
	return UFS_PM_LVL_0;
}

static struct ufs_dev_fix ufs_fixups[] = {
	/* UFS cards deviations table */
	UFS_FIX(UFS_VENDOR_MICRON, UFS_ANY_MODEL,
		UFS_DEVICE_QUIRK_DELAY_BEFORE_LPM |
		UFS_DEVICE_QUIRK_SWAP_L2P_ENTRY_FOR_HPB_READ),
	UFS_FIX(UFS_VENDOR_SAMSUNG, UFS_ANY_MODEL,
		UFS_DEVICE_QUIRK_DELAY_BEFORE_LPM |
		UFS_DEVICE_QUIRK_HOST_PA_TACTIVATE |
		UFS_DEVICE_QUIRK_RECOVERY_FROM_DL_NAC_ERRORS),
	UFS_FIX(UFS_VENDOR_SKHYNIX, UFS_ANY_MODEL,
		UFS_DEVICE_QUIRK_HOST_PA_SAVECONFIGTIME),
	UFS_FIX(UFS_VENDOR_SKHYNIX, "hB8aL1" /*H28U62301AMR*/,
		UFS_DEVICE_QUIRK_HOST_VS_DEBUGSAVECONFIGTIME),
	UFS_FIX(UFS_VENDOR_TOSHIBA, UFS_ANY_MODEL,
		UFS_DEVICE_QUIRK_DELAY_BEFORE_LPM),
	UFS_FIX(UFS_VENDOR_TOSHIBA, "THGLF2G9C8KBADG",
		UFS_DEVICE_QUIRK_PA_TACTIVATE),
	UFS_FIX(UFS_VENDOR_TOSHIBA, "THGLF2G9D8KBADG",
		UFS_DEVICE_QUIRK_PA_TACTIVATE),
	END_FIX
};

static irqreturn_t ufshcd_tmc_handler(struct ufs_hba *hba);
static void ufshcd_async_scan(void *data, async_cookie_t cookie);
static int ufshcd_reset_and_restore(struct ufs_hba *hba);
static int ufshcd_eh_host_reset_handler(struct scsi_cmnd *cmd);
static int ufshcd_clear_tm_cmd(struct ufs_hba *hba, int tag);
static void ufshcd_hba_exit(struct ufs_hba *hba);
static int ufshcd_probe_hba(struct ufs_hba *hba, bool init_dev_params);
static int ufshcd_setup_clocks(struct ufs_hba *hba, bool on);
static int ufshcd_uic_hibern8_enter(struct ufs_hba *hba);
static inline void ufshcd_add_delay_before_dme_cmd(struct ufs_hba *hba);
static int ufshcd_host_reset_and_restore(struct ufs_hba *hba);
static void ufshcd_resume_clkscaling(struct ufs_hba *hba);
static void ufshcd_suspend_clkscaling(struct ufs_hba *hba);
static void __ufshcd_suspend_clkscaling(struct ufs_hba *hba);
static int ufshcd_scale_clks(struct ufs_hba *hba, bool scale_up);
static irqreturn_t ufshcd_intr(int irq, void *__hba);
static int ufshcd_change_power_mode(struct ufs_hba *hba,
			     struct ufs_pa_layer_attr *pwr_mode);
static void ufshcd_schedule_eh_work(struct ufs_hba *hba);
static int ufshcd_setup_hba_vreg(struct ufs_hba *hba, bool on);
static int ufshcd_setup_vreg(struct ufs_hba *hba, bool on);
static inline int ufshcd_config_vreg_hpm(struct ufs_hba *hba,
					 struct ufs_vreg *vreg);
static int ufshcd_try_to_abort_task(struct ufs_hba *hba, int tag);
static void ufshcd_wb_toggle_flush_during_h8(struct ufs_hba *hba, bool set);
static inline void ufshcd_wb_toggle_flush(struct ufs_hba *hba, bool enable);
static void ufshcd_hba_vreg_set_lpm(struct ufs_hba *hba);
static void ufshcd_hba_vreg_set_hpm(struct ufs_hba *hba);

static inline void ufshcd_enable_irq(struct ufs_hba *hba)
{
	if (!hba->is_irq_enabled) {
		enable_irq(hba->irq);
		hba->is_irq_enabled = true;
	}
}

static inline void ufshcd_disable_irq(struct ufs_hba *hba)
{
	if (hba->is_irq_enabled) {
		disable_irq(hba->irq);
		hba->is_irq_enabled = false;
	}
}

static inline void ufshcd_wb_config(struct ufs_hba *hba)
{
	if (!ufshcd_is_wb_allowed(hba))
		return;

	ufshcd_wb_toggle(hba, true);

	ufshcd_wb_toggle_flush_during_h8(hba, true);
	if (!(hba->quirks & UFSHCI_QUIRK_SKIP_MANUAL_WB_FLUSH_CTRL))
		ufshcd_wb_toggle_flush(hba, true);
}

static void ufshcd_scsi_unblock_requests(struct ufs_hba *hba)
{
	if (atomic_dec_and_test(&hba->scsi_block_reqs_cnt))
		scsi_unblock_requests(hba->host);
}

static void ufshcd_scsi_block_requests(struct ufs_hba *hba)
{
	if (atomic_inc_return(&hba->scsi_block_reqs_cnt) == 1)
		scsi_block_requests(hba->host);
}

static void ufshcd_add_cmd_upiu_trace(struct ufs_hba *hba, unsigned int tag,
				      enum ufs_trace_str_t str_t)
{
	struct utp_upiu_req *rq = hba->lrb[tag].ucd_req_ptr;
	struct utp_upiu_header *header;

	if (!trace_ufshcd_upiu_enabled())
		return;

	if (str_t == UFS_CMD_SEND)
		header = &rq->header;
	else
		header = &hba->lrb[tag].ucd_rsp_ptr->header;

	trace_ufshcd_upiu(dev_name(hba->dev), str_t, header, &rq->sc.cdb,
			  UFS_TSF_CDB);
}

static void ufshcd_add_query_upiu_trace(struct ufs_hba *hba,
					enum ufs_trace_str_t str_t,
					struct utp_upiu_req *rq_rsp)
{
	if (!trace_ufshcd_upiu_enabled())
		return;

	trace_ufshcd_upiu(dev_name(hba->dev), str_t, &rq_rsp->header,
			  &rq_rsp->qr, UFS_TSF_OSF);
}

static void ufshcd_add_tm_upiu_trace(struct ufs_hba *hba, unsigned int tag,
				     enum ufs_trace_str_t str_t)
{
	struct utp_task_req_desc *descp = &hba->utmrdl_base_addr[tag];

	if (!trace_ufshcd_upiu_enabled())
		return;

	if (str_t == UFS_TM_SEND)
		trace_ufshcd_upiu(dev_name(hba->dev), str_t,
				  &descp->upiu_req.req_header,
				  &descp->upiu_req.input_param1,
				  UFS_TSF_TM_INPUT);
	else
		trace_ufshcd_upiu(dev_name(hba->dev), str_t,
				  &descp->upiu_rsp.rsp_header,
				  &descp->upiu_rsp.output_param1,
				  UFS_TSF_TM_OUTPUT);
}

static void ufshcd_add_uic_command_trace(struct ufs_hba *hba,
					 struct uic_command *ucmd,
					 enum ufs_trace_str_t str_t)
{
	u32 cmd;

	if (!trace_ufshcd_uic_command_enabled())
		return;

	if (str_t == UFS_CMD_SEND)
		cmd = ucmd->command;
	else
		cmd = ufshcd_readl(hba, REG_UIC_COMMAND);

	trace_ufshcd_uic_command(dev_name(hba->dev), str_t, cmd,
				 ufshcd_readl(hba, REG_UIC_COMMAND_ARG_1),
				 ufshcd_readl(hba, REG_UIC_COMMAND_ARG_2),
				 ufshcd_readl(hba, REG_UIC_COMMAND_ARG_3));
}

static void ufshcd_add_command_trace(struct ufs_hba *hba, unsigned int tag,
				     enum ufs_trace_str_t str_t)
{
	u64 lba = 0;
	u8 opcode = 0, group_id = 0;
	u32 intr, doorbell;
	struct ufshcd_lrb *lrbp = &hba->lrb[tag];
	struct scsi_cmnd *cmd = lrbp->cmd;
	struct request *rq = scsi_cmd_to_rq(cmd);
	int transfer_len = -1;

	if (!cmd)
		return;

	/* trace UPIU also */
	ufshcd_add_cmd_upiu_trace(hba, tag, str_t);
	if (!trace_ufshcd_command_enabled())
		return;

	opcode = cmd->cmnd[0];

	if (opcode == READ_10 || opcode == WRITE_10) {
		/*
		 * Currently we only fully trace read(10) and write(10) commands
		 */
		transfer_len =
		       be32_to_cpu(lrbp->ucd_req_ptr->sc.exp_data_transfer_len);
		lba = scsi_get_lba(cmd);
		if (opcode == WRITE_10)
			group_id = lrbp->cmd->cmnd[6];
	} else if (opcode == UNMAP) {
		/*
		 * The number of Bytes to be unmapped beginning with the lba.
		 */
		transfer_len = blk_rq_bytes(rq);
		lba = scsi_get_lba(cmd);
	}

	intr = ufshcd_readl(hba, REG_INTERRUPT_STATUS);
	doorbell = ufshcd_readl(hba, REG_UTP_TRANSFER_REQ_DOOR_BELL);
	trace_ufshcd_command(dev_name(hba->dev), str_t, tag,
			doorbell, transfer_len, intr, lba, opcode, group_id);
}

static void ufshcd_print_clk_freqs(struct ufs_hba *hba)
{
	struct ufs_clk_info *clki;
	struct list_head *head = &hba->clk_list_head;

	if (list_empty(head))
		return;

	list_for_each_entry(clki, head, list) {
		if (!IS_ERR_OR_NULL(clki->clk) && clki->min_freq &&
				clki->max_freq)
			dev_err(hba->dev, "clk: %s, rate: %u\n",
					clki->name, clki->curr_freq);
	}
}

static void ufshcd_print_evt(struct ufs_hba *hba, u32 id,
			     char *err_name)
{
	int i;
	bool found = false;
	struct ufs_event_hist *e;

	if (id >= UFS_EVT_CNT)
		return;

	e = &hba->ufs_stats.event[id];

	for (i = 0; i < UFS_EVENT_HIST_LENGTH; i++) {
		int p = (i + e->pos) % UFS_EVENT_HIST_LENGTH;

		if (e->tstamp[p] == 0)
			continue;
		dev_err(hba->dev, "%s[%d] = 0x%x at %lld us\n", err_name, p,
			e->val[p], ktime_to_us(e->tstamp[p]));
		found = true;
	}

	if (!found)
		dev_err(hba->dev, "No record of %s\n", err_name);
	else
		dev_err(hba->dev, "%s: total cnt=%llu\n", err_name, e->cnt);
}

static void ufshcd_print_evt_hist(struct ufs_hba *hba)
{
	ufshcd_dump_regs(hba, 0, UFSHCI_REG_SPACE_SIZE, "host_regs: ");

	ufshcd_print_evt(hba, UFS_EVT_PA_ERR, "pa_err");
	ufshcd_print_evt(hba, UFS_EVT_DL_ERR, "dl_err");
	ufshcd_print_evt(hba, UFS_EVT_NL_ERR, "nl_err");
	ufshcd_print_evt(hba, UFS_EVT_TL_ERR, "tl_err");
	ufshcd_print_evt(hba, UFS_EVT_DME_ERR, "dme_err");
	ufshcd_print_evt(hba, UFS_EVT_AUTO_HIBERN8_ERR,
			 "auto_hibern8_err");
	ufshcd_print_evt(hba, UFS_EVT_FATAL_ERR, "fatal_err");
	ufshcd_print_evt(hba, UFS_EVT_LINK_STARTUP_FAIL,
			 "link_startup_fail");
	ufshcd_print_evt(hba, UFS_EVT_RESUME_ERR, "resume_fail");
	ufshcd_print_evt(hba, UFS_EVT_SUSPEND_ERR,
			 "suspend_fail");
	ufshcd_print_evt(hba, UFS_EVT_DEV_RESET, "dev_reset");
	ufshcd_print_evt(hba, UFS_EVT_HOST_RESET, "host_reset");
	ufshcd_print_evt(hba, UFS_EVT_ABORT, "task_abort");

	ufshcd_vops_dbg_register_dump(hba);
}

static
void ufshcd_print_trs(struct ufs_hba *hba, unsigned long bitmap, bool pr_prdt)
{
	struct ufshcd_lrb *lrbp;
	int prdt_length;
	int tag;

	for_each_set_bit(tag, &bitmap, hba->nutrs) {
		lrbp = &hba->lrb[tag];

		dev_err(hba->dev, "UPIU[%d] - issue time %lld us\n",
				tag, ktime_to_us(lrbp->issue_time_stamp));
		dev_err(hba->dev, "UPIU[%d] - complete time %lld us\n",
				tag, ktime_to_us(lrbp->compl_time_stamp));
		dev_err(hba->dev,
			"UPIU[%d] - Transfer Request Descriptor phys@0x%llx\n",
			tag, (u64)lrbp->utrd_dma_addr);

		ufshcd_hex_dump("UPIU TRD: ", lrbp->utr_descriptor_ptr,
				sizeof(struct utp_transfer_req_desc));
		dev_err(hba->dev, "UPIU[%d] - Request UPIU phys@0x%llx\n", tag,
			(u64)lrbp->ucd_req_dma_addr);
		ufshcd_hex_dump("UPIU REQ: ", lrbp->ucd_req_ptr,
				sizeof(struct utp_upiu_req));
		dev_err(hba->dev, "UPIU[%d] - Response UPIU phys@0x%llx\n", tag,
			(u64)lrbp->ucd_rsp_dma_addr);
		ufshcd_hex_dump("UPIU RSP: ", lrbp->ucd_rsp_ptr,
				sizeof(struct utp_upiu_rsp));

		prdt_length = le16_to_cpu(
			lrbp->utr_descriptor_ptr->prd_table_length);
		if (hba->quirks & UFSHCD_QUIRK_PRDT_BYTE_GRAN)
			prdt_length /= sizeof(struct ufshcd_sg_entry);

		dev_err(hba->dev,
			"UPIU[%d] - PRDT - %d entries  phys@0x%llx\n",
			tag, prdt_length,
			(u64)lrbp->ucd_prdt_dma_addr);

		if (pr_prdt)
			ufshcd_hex_dump("UPIU PRDT: ", lrbp->ucd_prdt_ptr,
				sizeof(struct ufshcd_sg_entry) * prdt_length);
	}
}

static void ufshcd_print_tmrs(struct ufs_hba *hba, unsigned long bitmap)
{
	int tag;

	for_each_set_bit(tag, &bitmap, hba->nutmrs) {
		struct utp_task_req_desc *tmrdp = &hba->utmrdl_base_addr[tag];

		dev_err(hba->dev, "TM[%d] - Task Management Header\n", tag);
		ufshcd_hex_dump("", tmrdp, sizeof(*tmrdp));
	}
}

static void ufshcd_print_host_state(struct ufs_hba *hba)
{
	struct scsi_device *sdev_ufs = hba->sdev_ufs_device;

	dev_err(hba->dev, "UFS Host state=%d\n", hba->ufshcd_state);
	dev_err(hba->dev, "outstanding reqs=0x%lx tasks=0x%lx\n",
		hba->outstanding_reqs, hba->outstanding_tasks);
	dev_err(hba->dev, "saved_err=0x%x, saved_uic_err=0x%x\n",
		hba->saved_err, hba->saved_uic_err);
	dev_err(hba->dev, "Device power mode=%d, UIC link state=%d\n",
		hba->curr_dev_pwr_mode, hba->uic_link_state);
	dev_err(hba->dev, "PM in progress=%d, sys. suspended=%d\n",
		hba->pm_op_in_progress, hba->is_sys_suspended);
	dev_err(hba->dev, "Auto BKOPS=%d, Host self-block=%d\n",
		hba->auto_bkops_enabled, hba->host->host_self_blocked);
	dev_err(hba->dev, "Clk gate=%d\n", hba->clk_gating.state);
	dev_err(hba->dev,
		"last_hibern8_exit_tstamp at %lld us, hibern8_exit_cnt=%d\n",
		ktime_to_us(hba->ufs_stats.last_hibern8_exit_tstamp),
		hba->ufs_stats.hibern8_exit_cnt);
	dev_err(hba->dev, "last intr at %lld us, last intr status=0x%x\n",
		ktime_to_us(hba->ufs_stats.last_intr_ts),
		hba->ufs_stats.last_intr_status);
	dev_err(hba->dev, "error handling flags=0x%x, req. abort count=%d\n",
		hba->eh_flags, hba->req_abort_count);
	dev_err(hba->dev, "hba->ufs_version=0x%x, Host capabilities=0x%x, caps=0x%x\n",
		hba->ufs_version, hba->capabilities, hba->caps);
	dev_err(hba->dev, "quirks=0x%x, dev. quirks=0x%x\n", hba->quirks,
		hba->dev_quirks);
	if (sdev_ufs)
		dev_err(hba->dev, "UFS dev info: %.8s %.16s rev %.4s\n",
			sdev_ufs->vendor, sdev_ufs->model, sdev_ufs->rev);

	ufshcd_print_clk_freqs(hba);
}

/**
 * ufshcd_print_pwr_info - print power params as saved in hba
 * power info
 * @hba: per-adapter instance
 */
static void ufshcd_print_pwr_info(struct ufs_hba *hba)
{
	static const char * const names[] = {
		"INVALID MODE",
		"FAST MODE",
		"SLOW_MODE",
		"INVALID MODE",
		"FASTAUTO_MODE",
		"SLOWAUTO_MODE",
		"INVALID MODE",
	};

	/*
	 * Using dev_dbg to avoid messages during runtime PM to avoid
	 * never-ending cycles of messages written back to storage by user space
	 * causing runtime resume, causing more messages and so on.
	 */
	dev_dbg(hba->dev, "%s:[RX, TX]: gear=[%d, %d], lane[%d, %d], pwr[%s, %s], rate = %d\n",
		 __func__,
		 hba->pwr_info.gear_rx, hba->pwr_info.gear_tx,
		 hba->pwr_info.lane_rx, hba->pwr_info.lane_tx,
		 names[hba->pwr_info.pwr_rx],
		 names[hba->pwr_info.pwr_tx],
		 hba->pwr_info.hs_rate);
}

static void ufshcd_device_reset(struct ufs_hba *hba)
{
	int err;

	err = ufshcd_vops_device_reset(hba);

	if (!err) {
		ufshcd_set_ufs_dev_active(hba);
		if (ufshcd_is_wb_allowed(hba)) {
			hba->dev_info.wb_enabled = false;
			hba->dev_info.wb_buf_flush_enabled = false;
		}
	}
	if (err != -EOPNOTSUPP)
		ufshcd_update_evt_hist(hba, UFS_EVT_DEV_RESET, err);
}

void ufshcd_delay_us(unsigned long us, unsigned long tolerance)
{
	if (!us)
		return;

	if (us < 10)
		udelay(us);
	else
		usleep_range(us, us + tolerance);
}
EXPORT_SYMBOL_GPL(ufshcd_delay_us);

/**
 * ufshcd_wait_for_register - wait for register value to change
 * @hba: per-adapter interface
 * @reg: mmio register offset
 * @mask: mask to apply to the read register value
 * @val: value to wait for
 * @interval_us: polling interval in microseconds
 * @timeout_ms: timeout in milliseconds
 *
 * Return:
 * -ETIMEDOUT on error, zero on success.
 */
int ufshcd_wait_for_register(struct ufs_hba *hba, u32 reg, u32 mask,
				u32 val, unsigned long interval_us,
				unsigned long timeout_ms)
{
	int err = 0;
	unsigned long timeout = jiffies + msecs_to_jiffies(timeout_ms);

	/* ignore bits that we don't intend to wait on */
	val = val & mask;

	while ((ufshcd_readl(hba, reg) & mask) != val) {
		usleep_range(interval_us, interval_us + 50);
		if (time_after(jiffies, timeout)) {
			if ((ufshcd_readl(hba, reg) & mask) != val)
				err = -ETIMEDOUT;
			break;
		}
	}

	return err;
}

/**
 * ufshcd_get_intr_mask - Get the interrupt bit mask
 * @hba: Pointer to adapter instance
 *
 * Returns interrupt bit mask per version
 */
static inline u32 ufshcd_get_intr_mask(struct ufs_hba *hba)
{
	if (hba->ufs_version == ufshci_version(1, 0))
		return INTERRUPT_MASK_ALL_VER_10;
	if (hba->ufs_version <= ufshci_version(2, 0))
		return INTERRUPT_MASK_ALL_VER_11;

	return INTERRUPT_MASK_ALL_VER_21;
}

/**
 * ufshcd_get_ufs_version - Get the UFS version supported by the HBA
 * @hba: Pointer to adapter instance
 *
 * Returns UFSHCI version supported by the controller
 */
static inline u32 ufshcd_get_ufs_version(struct ufs_hba *hba)
{
	u32 ufshci_ver;

	if (hba->quirks & UFSHCD_QUIRK_BROKEN_UFS_HCI_VERSION)
		ufshci_ver = ufshcd_vops_get_ufs_hci_version(hba);
	else
		ufshci_ver = ufshcd_readl(hba, REG_UFS_VERSION);

	/*
	 * UFSHCI v1.x uses a different version scheme, in order
	 * to allow the use of comparisons with the ufshci_version
	 * function, we convert it to the same scheme as ufs 2.0+.
	 */
	if (ufshci_ver & 0x00010000)
		return ufshci_version(1, ufshci_ver & 0x00000100);

	return ufshci_ver;
}

/**
 * ufshcd_is_device_present - Check if any device connected to
 *			      the host controller
 * @hba: pointer to adapter instance
 *
 * Returns true if device present, false if no device detected
 */
static inline bool ufshcd_is_device_present(struct ufs_hba *hba)
{
	return (ufshcd_readl(hba, REG_CONTROLLER_STATUS) &
						DEVICE_PRESENT) ? true : false;
}

/**
 * ufshcd_get_tr_ocs - Get the UTRD Overall Command Status
 * @lrbp: pointer to local command reference block
 *
 * This function is used to get the OCS field from UTRD
 * Returns the OCS field in the UTRD
 */
static inline int ufshcd_get_tr_ocs(struct ufshcd_lrb *lrbp)
{
	return le32_to_cpu(lrbp->utr_descriptor_ptr->header.dword_2) & MASK_OCS;
}

/**
 * ufshcd_utrl_clear - Clear a bit in UTRLCLR register
 * @hba: per adapter instance
 * @pos: position of the bit to be cleared
 */
static inline void ufshcd_utrl_clear(struct ufs_hba *hba, u32 pos)
{
	if (hba->quirks & UFSHCI_QUIRK_BROKEN_REQ_LIST_CLR)
		ufshcd_writel(hba, (1 << pos), REG_UTP_TRANSFER_REQ_LIST_CLEAR);
	else
		ufshcd_writel(hba, ~(1 << pos),
				REG_UTP_TRANSFER_REQ_LIST_CLEAR);
}

/**
 * ufshcd_utmrl_clear - Clear a bit in UTRMLCLR register
 * @hba: per adapter instance
 * @pos: position of the bit to be cleared
 */
static inline void ufshcd_utmrl_clear(struct ufs_hba *hba, u32 pos)
{
	if (hba->quirks & UFSHCI_QUIRK_BROKEN_REQ_LIST_CLR)
		ufshcd_writel(hba, (1 << pos), REG_UTP_TASK_REQ_LIST_CLEAR);
	else
		ufshcd_writel(hba, ~(1 << pos), REG_UTP_TASK_REQ_LIST_CLEAR);
}

/**
 * ufshcd_get_lists_status - Check UCRDY, UTRLRDY and UTMRLRDY
 * @reg: Register value of host controller status
 *
 * Returns integer, 0 on Success and positive value if failed
 */
static inline int ufshcd_get_lists_status(u32 reg)
{
	return !((reg & UFSHCD_STATUS_READY) == UFSHCD_STATUS_READY);
}

/**
 * ufshcd_get_uic_cmd_result - Get the UIC command result
 * @hba: Pointer to adapter instance
 *
 * This function gets the result of UIC command completion
 * Returns 0 on success, non zero value on error
 */
static inline int ufshcd_get_uic_cmd_result(struct ufs_hba *hba)
{
	return ufshcd_readl(hba, REG_UIC_COMMAND_ARG_2) &
	       MASK_UIC_COMMAND_RESULT;
}

/**
 * ufshcd_get_dme_attr_val - Get the value of attribute returned by UIC command
 * @hba: Pointer to adapter instance
 *
 * This function gets UIC command argument3
 * Returns 0 on success, non zero value on error
 */
static inline u32 ufshcd_get_dme_attr_val(struct ufs_hba *hba)
{
	return ufshcd_readl(hba, REG_UIC_COMMAND_ARG_3);
}

/**
 * ufshcd_get_req_rsp - returns the TR response transaction type
 * @ucd_rsp_ptr: pointer to response UPIU
 */
static inline int
ufshcd_get_req_rsp(struct utp_upiu_rsp *ucd_rsp_ptr)
{
	return be32_to_cpu(ucd_rsp_ptr->header.dword_0) >> 24;
}

/**
 * ufshcd_get_rsp_upiu_result - Get the result from response UPIU
 * @ucd_rsp_ptr: pointer to response UPIU
 *
 * This function gets the response status and scsi_status from response UPIU
 * Returns the response result code.
 */
static inline int
ufshcd_get_rsp_upiu_result(struct utp_upiu_rsp *ucd_rsp_ptr)
{
	return be32_to_cpu(ucd_rsp_ptr->header.dword_1) & MASK_RSP_UPIU_RESULT;
}

/*
 * ufshcd_get_rsp_upiu_data_seg_len - Get the data segment length
 *				from response UPIU
 * @ucd_rsp_ptr: pointer to response UPIU
 *
 * Return the data segment length.
 */
static inline unsigned int
ufshcd_get_rsp_upiu_data_seg_len(struct utp_upiu_rsp *ucd_rsp_ptr)
{
	return be32_to_cpu(ucd_rsp_ptr->header.dword_2) &
		MASK_RSP_UPIU_DATA_SEG_LEN;
}

/**
 * ufshcd_is_exception_event - Check if the device raised an exception event
 * @ucd_rsp_ptr: pointer to response UPIU
 *
 * The function checks if the device raised an exception event indicated in
 * the Device Information field of response UPIU.
 *
 * Returns true if exception is raised, false otherwise.
 */
static inline bool ufshcd_is_exception_event(struct utp_upiu_rsp *ucd_rsp_ptr)
{
	return be32_to_cpu(ucd_rsp_ptr->header.dword_2) &
			MASK_RSP_EXCEPTION_EVENT ? true : false;
}

/**
 * ufshcd_reset_intr_aggr - Reset interrupt aggregation values.
 * @hba: per adapter instance
 */
static inline void
ufshcd_reset_intr_aggr(struct ufs_hba *hba)
{
	ufshcd_writel(hba, INT_AGGR_ENABLE |
		      INT_AGGR_COUNTER_AND_TIMER_RESET,
		      REG_UTP_TRANSFER_REQ_INT_AGG_CONTROL);
}

/**
 * ufshcd_config_intr_aggr - Configure interrupt aggregation values.
 * @hba: per adapter instance
 * @cnt: Interrupt aggregation counter threshold
 * @tmout: Interrupt aggregation timeout value
 */
static inline void
ufshcd_config_intr_aggr(struct ufs_hba *hba, u8 cnt, u8 tmout)
{
	ufshcd_writel(hba, INT_AGGR_ENABLE | INT_AGGR_PARAM_WRITE |
		      INT_AGGR_COUNTER_THLD_VAL(cnt) |
		      INT_AGGR_TIMEOUT_VAL(tmout),
		      REG_UTP_TRANSFER_REQ_INT_AGG_CONTROL);
}

/**
 * ufshcd_disable_intr_aggr - Disables interrupt aggregation.
 * @hba: per adapter instance
 */
static inline void ufshcd_disable_intr_aggr(struct ufs_hba *hba)
{
	ufshcd_writel(hba, 0, REG_UTP_TRANSFER_REQ_INT_AGG_CONTROL);
}

/**
 * ufshcd_enable_run_stop_reg - Enable run-stop registers,
 *			When run-stop registers are set to 1, it indicates the
 *			host controller that it can process the requests
 * @hba: per adapter instance
 */
static void ufshcd_enable_run_stop_reg(struct ufs_hba *hba)
{
	ufshcd_writel(hba, UTP_TASK_REQ_LIST_RUN_STOP_BIT,
		      REG_UTP_TASK_REQ_LIST_RUN_STOP);
	ufshcd_writel(hba, UTP_TRANSFER_REQ_LIST_RUN_STOP_BIT,
		      REG_UTP_TRANSFER_REQ_LIST_RUN_STOP);
}

/**
 * ufshcd_hba_start - Start controller initialization sequence
 * @hba: per adapter instance
 */
static inline void ufshcd_hba_start(struct ufs_hba *hba)
{
	u32 val = CONTROLLER_ENABLE;

	if (ufshcd_crypto_enable(hba))
		val |= CRYPTO_GENERAL_ENABLE;

	ufshcd_writel(hba, val, REG_CONTROLLER_ENABLE);
}

/**
 * ufshcd_is_hba_active - Get controller state
 * @hba: per adapter instance
 *
 * Returns false if controller is active, true otherwise
 */
static inline bool ufshcd_is_hba_active(struct ufs_hba *hba)
{
	return (ufshcd_readl(hba, REG_CONTROLLER_ENABLE) & CONTROLLER_ENABLE)
		? false : true;
}

u32 ufshcd_get_local_unipro_ver(struct ufs_hba *hba)
{
	/* HCI version 1.0 and 1.1 supports UniPro 1.41 */
	if (hba->ufs_version <= ufshci_version(1, 1))
		return UFS_UNIPRO_VER_1_41;
	else
		return UFS_UNIPRO_VER_1_6;
}
EXPORT_SYMBOL(ufshcd_get_local_unipro_ver);

static bool ufshcd_is_unipro_pa_params_tuning_req(struct ufs_hba *hba)
{
	/*
	 * If both host and device support UniPro ver1.6 or later, PA layer
	 * parameters tuning happens during link startup itself.
	 *
	 * We can manually tune PA layer parameters if either host or device
	 * doesn't support UniPro ver 1.6 or later. But to keep manual tuning
	 * logic simple, we will only do manual tuning if local unipro version
	 * doesn't support ver1.6 or later.
	 */
	if (ufshcd_get_local_unipro_ver(hba) < UFS_UNIPRO_VER_1_6)
		return true;
	else
		return false;
}

/**
 * ufshcd_set_clk_freq - set UFS controller clock frequencies
 * @hba: per adapter instance
 * @scale_up: If True, set max possible frequency othewise set low frequency
 *
 * Returns 0 if successful
 * Returns < 0 for any other errors
 */
static int ufshcd_set_clk_freq(struct ufs_hba *hba, bool scale_up)
{
	int ret = 0;
	struct ufs_clk_info *clki;
	struct list_head *head = &hba->clk_list_head;

	if (list_empty(head))
		goto out;

	list_for_each_entry(clki, head, list) {
		if (!IS_ERR_OR_NULL(clki->clk)) {
			if (scale_up && clki->max_freq) {
				if (clki->curr_freq == clki->max_freq)
					continue;

				ret = clk_set_rate(clki->clk, clki->max_freq);
				if (ret) {
					dev_err(hba->dev, "%s: %s clk set rate(%dHz) failed, %d\n",
						__func__, clki->name,
						clki->max_freq, ret);
					break;
				}
				trace_ufshcd_clk_scaling(dev_name(hba->dev),
						"scaled up", clki->name,
						clki->curr_freq,
						clki->max_freq);

				clki->curr_freq = clki->max_freq;

			} else if (!scale_up && clki->min_freq) {
				if (clki->curr_freq == clki->min_freq)
					continue;

				ret = clk_set_rate(clki->clk, clki->min_freq);
				if (ret) {
					dev_err(hba->dev, "%s: %s clk set rate(%dHz) failed, %d\n",
						__func__, clki->name,
						clki->min_freq, ret);
					break;
				}
				trace_ufshcd_clk_scaling(dev_name(hba->dev),
						"scaled down", clki->name,
						clki->curr_freq,
						clki->min_freq);
				clki->curr_freq = clki->min_freq;
			}
		}
		dev_dbg(hba->dev, "%s: clk: %s, rate: %lu\n", __func__,
				clki->name, clk_get_rate(clki->clk));
	}

out:
	return ret;
}

/**
 * ufshcd_scale_clks - scale up or scale down UFS controller clocks
 * @hba: per adapter instance
 * @scale_up: True if scaling up and false if scaling down
 *
 * Returns 0 if successful
 * Returns < 0 for any other errors
 */
static int ufshcd_scale_clks(struct ufs_hba *hba, bool scale_up)
{
	int ret = 0;
	ktime_t start = ktime_get();

	ret = ufshcd_vops_clk_scale_notify(hba, scale_up, PRE_CHANGE);
	if (ret)
		goto out;

	ret = ufshcd_set_clk_freq(hba, scale_up);
	if (ret)
		goto out;

	ret = ufshcd_vops_clk_scale_notify(hba, scale_up, POST_CHANGE);
	if (ret)
		ufshcd_set_clk_freq(hba, !scale_up);

out:
	trace_ufshcd_profile_clk_scaling(dev_name(hba->dev),
			(scale_up ? "up" : "down"),
			ktime_to_us(ktime_sub(ktime_get(), start)), ret);
	return ret;
}

/**
 * ufshcd_is_devfreq_scaling_required - check if scaling is required or not
 * @hba: per adapter instance
 * @scale_up: True if scaling up and false if scaling down
 *
 * Returns true if scaling is required, false otherwise.
 */
static bool ufshcd_is_devfreq_scaling_required(struct ufs_hba *hba,
					       bool scale_up)
{
	struct ufs_clk_info *clki;
	struct list_head *head = &hba->clk_list_head;

	if (list_empty(head))
		return false;

	list_for_each_entry(clki, head, list) {
		if (!IS_ERR_OR_NULL(clki->clk)) {
			if (scale_up && clki->max_freq) {
				if (clki->curr_freq == clki->max_freq)
					continue;
				return true;
			} else if (!scale_up && clki->min_freq) {
				if (clki->curr_freq == clki->min_freq)
					continue;
				return true;
			}
		}
	}

	return false;
}

static int ufshcd_wait_for_doorbell_clr(struct ufs_hba *hba,
					u64 wait_timeout_us)
{
	unsigned long flags;
	int ret = 0;
	u32 tm_doorbell;
	u32 tr_doorbell;
	bool timeout = false, do_last_check = false;
	ktime_t start;

	ufshcd_hold(hba, false);
	spin_lock_irqsave(hba->host->host_lock, flags);
	/*
	 * Wait for all the outstanding tasks/transfer requests.
	 * Verify by checking the doorbell registers are clear.
	 */
	start = ktime_get();
	do {
		if (hba->ufshcd_state != UFSHCD_STATE_OPERATIONAL) {
			ret = -EBUSY;
			goto out;
		}

		tm_doorbell = ufshcd_readl(hba, REG_UTP_TASK_REQ_DOOR_BELL);
		tr_doorbell = ufshcd_readl(hba, REG_UTP_TRANSFER_REQ_DOOR_BELL);
		if (!tm_doorbell && !tr_doorbell) {
			timeout = false;
			break;
		} else if (do_last_check) {
			break;
		}

		spin_unlock_irqrestore(hba->host->host_lock, flags);
		schedule();
		if (ktime_to_us(ktime_sub(ktime_get(), start)) >
		    wait_timeout_us) {
			timeout = true;
			/*
			 * We might have scheduled out for long time so make
			 * sure to check if doorbells are cleared by this time
			 * or not.
			 */
			do_last_check = true;
		}
		spin_lock_irqsave(hba->host->host_lock, flags);
	} while (tm_doorbell || tr_doorbell);

	if (timeout) {
		dev_err(hba->dev,
			"%s: timedout waiting for doorbell to clear (tm=0x%x, tr=0x%x)\n",
			__func__, tm_doorbell, tr_doorbell);
		ret = -EBUSY;
	}
out:
	spin_unlock_irqrestore(hba->host->host_lock, flags);
	ufshcd_release(hba);
	return ret;
}

/**
 * ufshcd_scale_gear - scale up/down UFS gear
 * @hba: per adapter instance
 * @scale_up: True for scaling up gear and false for scaling down
 *
 * Returns 0 for success,
 * Returns -EBUSY if scaling can't happen at this time
 * Returns non-zero for any other errors
 */
static int ufshcd_scale_gear(struct ufs_hba *hba, bool scale_up)
{
	int ret = 0;
	struct ufs_pa_layer_attr new_pwr_info;

	if (scale_up) {
		memcpy(&new_pwr_info, &hba->clk_scaling.saved_pwr_info.info,
		       sizeof(struct ufs_pa_layer_attr));
	} else {
		memcpy(&new_pwr_info, &hba->pwr_info,
		       sizeof(struct ufs_pa_layer_attr));

		if (hba->pwr_info.gear_tx > hba->clk_scaling.min_gear ||
		    hba->pwr_info.gear_rx > hba->clk_scaling.min_gear) {
			/* save the current power mode */
			memcpy(&hba->clk_scaling.saved_pwr_info.info,
				&hba->pwr_info,
				sizeof(struct ufs_pa_layer_attr));

			/* scale down gear */
			new_pwr_info.gear_tx = hba->clk_scaling.min_gear;
			new_pwr_info.gear_rx = hba->clk_scaling.min_gear;
		}
	}

	/* check if the power mode needs to be changed or not? */
	ret = ufshcd_config_pwr_mode(hba, &new_pwr_info);
	if (ret)
		dev_err(hba->dev, "%s: failed err %d, old gear: (tx %d rx %d), new gear: (tx %d rx %d)",
			__func__, ret,
			hba->pwr_info.gear_tx, hba->pwr_info.gear_rx,
			new_pwr_info.gear_tx, new_pwr_info.gear_rx);

	return ret;
}

static int ufshcd_clock_scaling_prepare(struct ufs_hba *hba)
{
	#define DOORBELL_CLR_TOUT_US		(1000 * 1000) /* 1 sec */
	int ret = 0;
	/*
	 * make sure that there are no outstanding requests when
	 * clock scaling is in progress
	 */
	ufshcd_scsi_block_requests(hba);
	down_write(&hba->clk_scaling_lock);

	if (!hba->clk_scaling.is_allowed ||
	    ufshcd_wait_for_doorbell_clr(hba, DOORBELL_CLR_TOUT_US)) {
		ret = -EBUSY;
		up_write(&hba->clk_scaling_lock);
		ufshcd_scsi_unblock_requests(hba);
		goto out;
	}

	/* let's not get into low power until clock scaling is completed */
	ufshcd_hold(hba, false);

out:
	return ret;
}

static void ufshcd_clock_scaling_unprepare(struct ufs_hba *hba, bool writelock)
{
	if (writelock)
		up_write(&hba->clk_scaling_lock);
	else
		up_read(&hba->clk_scaling_lock);
	ufshcd_scsi_unblock_requests(hba);
	ufshcd_release(hba);
}

/**
 * ufshcd_devfreq_scale - scale up/down UFS clocks and gear
 * @hba: per adapter instance
 * @scale_up: True for scaling up and false for scalin down
 *
 * Returns 0 for success,
 * Returns -EBUSY if scaling can't happen at this time
 * Returns non-zero for any other errors
 */
static int ufshcd_devfreq_scale(struct ufs_hba *hba, bool scale_up)
{
	int ret = 0;
	bool is_writelock = true;

	ret = ufshcd_clock_scaling_prepare(hba);
	if (ret)
		return ret;

	/* scale down the gear before scaling down clocks */
	if (!scale_up) {
		ret = ufshcd_scale_gear(hba, false);
		if (ret)
			goto out_unprepare;
	}

	ret = ufshcd_scale_clks(hba, scale_up);
	if (ret) {
		if (!scale_up)
			ufshcd_scale_gear(hba, true);
		goto out_unprepare;
	}

	/* scale up the gear after scaling up clocks */
	if (scale_up) {
		ret = ufshcd_scale_gear(hba, true);
		if (ret) {
			ufshcd_scale_clks(hba, false);
			goto out_unprepare;
		}
	}

	/* Enable Write Booster if we have scaled up else disable it */
	downgrade_write(&hba->clk_scaling_lock);
	is_writelock = false;
	ufshcd_wb_toggle(hba, scale_up);

out_unprepare:
	ufshcd_clock_scaling_unprepare(hba, is_writelock);
	return ret;
}

static void ufshcd_clk_scaling_suspend_work(struct work_struct *work)
{
	struct ufs_hba *hba = container_of(work, struct ufs_hba,
					   clk_scaling.suspend_work);
	unsigned long irq_flags;

	spin_lock_irqsave(hba->host->host_lock, irq_flags);
	if (hba->clk_scaling.active_reqs || hba->clk_scaling.is_suspended) {
		spin_unlock_irqrestore(hba->host->host_lock, irq_flags);
		return;
	}
	hba->clk_scaling.is_suspended = true;
	spin_unlock_irqrestore(hba->host->host_lock, irq_flags);

	__ufshcd_suspend_clkscaling(hba);
}

static void ufshcd_clk_scaling_resume_work(struct work_struct *work)
{
	struct ufs_hba *hba = container_of(work, struct ufs_hba,
					   clk_scaling.resume_work);
	unsigned long irq_flags;

	spin_lock_irqsave(hba->host->host_lock, irq_flags);
	if (!hba->clk_scaling.is_suspended) {
		spin_unlock_irqrestore(hba->host->host_lock, irq_flags);
		return;
	}
	hba->clk_scaling.is_suspended = false;
	spin_unlock_irqrestore(hba->host->host_lock, irq_flags);

	devfreq_resume_device(hba->devfreq);
}

static int ufshcd_devfreq_target(struct device *dev,
				unsigned long *freq, u32 flags)
{
	int ret = 0;
	struct ufs_hba *hba = dev_get_drvdata(dev);
	ktime_t start;
	bool scale_up, sched_clk_scaling_suspend_work = false;
	struct list_head *clk_list = &hba->clk_list_head;
	struct ufs_clk_info *clki;
	unsigned long irq_flags;

	if (!ufshcd_is_clkscaling_supported(hba))
		return -EINVAL;

	clki = list_first_entry(&hba->clk_list_head, struct ufs_clk_info, list);
	/* Override with the closest supported frequency */
	*freq = (unsigned long) clk_round_rate(clki->clk, *freq);
	spin_lock_irqsave(hba->host->host_lock, irq_flags);
	if (ufshcd_eh_in_progress(hba)) {
		spin_unlock_irqrestore(hba->host->host_lock, irq_flags);
		return 0;
	}

	if (!hba->clk_scaling.active_reqs)
		sched_clk_scaling_suspend_work = true;

	if (list_empty(clk_list)) {
		spin_unlock_irqrestore(hba->host->host_lock, irq_flags);
		goto out;
	}

	/* Decide based on the rounded-off frequency and update */
	scale_up = (*freq == clki->max_freq) ? true : false;
	if (!scale_up)
		*freq = clki->min_freq;
	/* Update the frequency */
	if (!ufshcd_is_devfreq_scaling_required(hba, scale_up)) {
		spin_unlock_irqrestore(hba->host->host_lock, irq_flags);
		ret = 0;
		goto out; /* no state change required */
	}
	spin_unlock_irqrestore(hba->host->host_lock, irq_flags);

	start = ktime_get();
	ret = ufshcd_devfreq_scale(hba, scale_up);

	trace_ufshcd_profile_clk_scaling(dev_name(hba->dev),
		(scale_up ? "up" : "down"),
		ktime_to_us(ktime_sub(ktime_get(), start)), ret);

out:
	if (sched_clk_scaling_suspend_work)
		queue_work(hba->clk_scaling.workq,
			   &hba->clk_scaling.suspend_work);

	return ret;
}

static bool ufshcd_is_busy(struct request *req, void *priv, bool reserved)
{
	int *busy = priv;

	WARN_ON_ONCE(reserved);
	(*busy)++;
	return false;
}

/* Whether or not any tag is in use by a request that is in progress. */
static bool ufshcd_any_tag_in_use(struct ufs_hba *hba)
{
	struct request_queue *q = hba->cmd_queue;
	int busy = 0;

	blk_mq_tagset_busy_iter(q->tag_set, ufshcd_is_busy, &busy);
	return busy;
}

static int ufshcd_devfreq_get_dev_status(struct device *dev,
		struct devfreq_dev_status *stat)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	struct ufs_clk_scaling *scaling = &hba->clk_scaling;
	unsigned long flags;
	struct list_head *clk_list = &hba->clk_list_head;
	struct ufs_clk_info *clki;
	ktime_t curr_t;

	if (!ufshcd_is_clkscaling_supported(hba))
		return -EINVAL;

	memset(stat, 0, sizeof(*stat));

	spin_lock_irqsave(hba->host->host_lock, flags);
	curr_t = ktime_get();
	if (!scaling->window_start_t)
		goto start_window;

	clki = list_first_entry(clk_list, struct ufs_clk_info, list);
	/*
	 * If current frequency is 0, then the ondemand governor considers
	 * there's no initial frequency set. And it always requests to set
	 * to max. frequency.
	 */
	stat->current_frequency = clki->curr_freq;
	if (scaling->is_busy_started)
		scaling->tot_busy_t += ktime_us_delta(curr_t,
				scaling->busy_start_t);

	stat->total_time = ktime_us_delta(curr_t, scaling->window_start_t);
	stat->busy_time = scaling->tot_busy_t;
start_window:
	scaling->window_start_t = curr_t;
	scaling->tot_busy_t = 0;

	if (hba->outstanding_reqs) {
		scaling->busy_start_t = curr_t;
		scaling->is_busy_started = true;
	} else {
		scaling->busy_start_t = 0;
		scaling->is_busy_started = false;
	}
	spin_unlock_irqrestore(hba->host->host_lock, flags);
	return 0;
}

static int ufshcd_devfreq_init(struct ufs_hba *hba)
{
	struct list_head *clk_list = &hba->clk_list_head;
	struct ufs_clk_info *clki;
	struct devfreq *devfreq;
	int ret;

	/* Skip devfreq if we don't have any clocks in the list */
	if (list_empty(clk_list))
		return 0;

	clki = list_first_entry(clk_list, struct ufs_clk_info, list);
	dev_pm_opp_add(hba->dev, clki->min_freq, 0);
	dev_pm_opp_add(hba->dev, clki->max_freq, 0);

	ufshcd_vops_config_scaling_param(hba, &hba->vps->devfreq_profile,
					 &hba->vps->ondemand_data);
	devfreq = devfreq_add_device(hba->dev,
			&hba->vps->devfreq_profile,
			DEVFREQ_GOV_SIMPLE_ONDEMAND,
			&hba->vps->ondemand_data);
	if (IS_ERR(devfreq)) {
		ret = PTR_ERR(devfreq);
		dev_err(hba->dev, "Unable to register with devfreq %d\n", ret);

		dev_pm_opp_remove(hba->dev, clki->min_freq);
		dev_pm_opp_remove(hba->dev, clki->max_freq);
		return ret;
	}

	hba->devfreq = devfreq;

	return 0;
}

static void ufshcd_devfreq_remove(struct ufs_hba *hba)
{
	struct list_head *clk_list = &hba->clk_list_head;
	struct ufs_clk_info *clki;

	if (!hba->devfreq)
		return;

	devfreq_remove_device(hba->devfreq);
	hba->devfreq = NULL;

	clki = list_first_entry(clk_list, struct ufs_clk_info, list);
	dev_pm_opp_remove(hba->dev, clki->min_freq);
	dev_pm_opp_remove(hba->dev, clki->max_freq);
}

static void __ufshcd_suspend_clkscaling(struct ufs_hba *hba)
{
	unsigned long flags;

	devfreq_suspend_device(hba->devfreq);
	spin_lock_irqsave(hba->host->host_lock, flags);
	hba->clk_scaling.window_start_t = 0;
	spin_unlock_irqrestore(hba->host->host_lock, flags);
}

static void ufshcd_suspend_clkscaling(struct ufs_hba *hba)
{
	unsigned long flags;
	bool suspend = false;

	cancel_work_sync(&hba->clk_scaling.suspend_work);
	cancel_work_sync(&hba->clk_scaling.resume_work);

	spin_lock_irqsave(hba->host->host_lock, flags);
	if (!hba->clk_scaling.is_suspended) {
		suspend = true;
		hba->clk_scaling.is_suspended = true;
	}
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	if (suspend)
		__ufshcd_suspend_clkscaling(hba);
}

static void ufshcd_resume_clkscaling(struct ufs_hba *hba)
{
	unsigned long flags;
	bool resume = false;

	spin_lock_irqsave(hba->host->host_lock, flags);
	if (hba->clk_scaling.is_suspended) {
		resume = true;
		hba->clk_scaling.is_suspended = false;
	}
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	if (resume)
		devfreq_resume_device(hba->devfreq);
}

static ssize_t ufshcd_clkscale_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", hba->clk_scaling.is_enabled);
}

static ssize_t ufshcd_clkscale_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	u32 value;
	int err = 0;

	if (kstrtou32(buf, 0, &value))
		return -EINVAL;

	down(&hba->host_sem);
	if (!ufshcd_is_user_access_allowed(hba)) {
		err = -EBUSY;
		goto out;
	}

	value = !!value;
	if (value == hba->clk_scaling.is_enabled)
		goto out;

	ufshcd_rpm_get_sync(hba);
	ufshcd_hold(hba, false);

	hba->clk_scaling.is_enabled = value;

	if (value) {
		ufshcd_resume_clkscaling(hba);
	} else {
		ufshcd_suspend_clkscaling(hba);
		err = ufshcd_devfreq_scale(hba, true);
		if (err)
			dev_err(hba->dev, "%s: failed to scale clocks up %d\n",
					__func__, err);
	}

	ufshcd_release(hba);
	ufshcd_rpm_put_sync(hba);
out:
	up(&hba->host_sem);
	return err ? err : count;
}

static void ufshcd_init_clk_scaling_sysfs(struct ufs_hba *hba)
{
	hba->clk_scaling.enable_attr.show = ufshcd_clkscale_enable_show;
	hba->clk_scaling.enable_attr.store = ufshcd_clkscale_enable_store;
	sysfs_attr_init(&hba->clk_scaling.enable_attr.attr);
	hba->clk_scaling.enable_attr.attr.name = "clkscale_enable";
	hba->clk_scaling.enable_attr.attr.mode = 0644;
	if (device_create_file(hba->dev, &hba->clk_scaling.enable_attr))
		dev_err(hba->dev, "Failed to create sysfs for clkscale_enable\n");
}

static void ufshcd_remove_clk_scaling_sysfs(struct ufs_hba *hba)
{
	if (hba->clk_scaling.enable_attr.attr.name)
		device_remove_file(hba->dev, &hba->clk_scaling.enable_attr);
}

static void ufshcd_init_clk_scaling(struct ufs_hba *hba)
{
	char wq_name[sizeof("ufs_clkscaling_00")];

	if (!ufshcd_is_clkscaling_supported(hba))
		return;

	if (!hba->clk_scaling.min_gear)
		hba->clk_scaling.min_gear = UFS_HS_G1;

	INIT_WORK(&hba->clk_scaling.suspend_work,
		  ufshcd_clk_scaling_suspend_work);
	INIT_WORK(&hba->clk_scaling.resume_work,
		  ufshcd_clk_scaling_resume_work);

	snprintf(wq_name, sizeof(wq_name), "ufs_clkscaling_%d",
		 hba->host->host_no);
	hba->clk_scaling.workq = create_singlethread_workqueue(wq_name);

	hba->clk_scaling.is_initialized = true;
}

static void ufshcd_exit_clk_scaling(struct ufs_hba *hba)
{
	if (!hba->clk_scaling.is_initialized)
		return;

	ufshcd_remove_clk_scaling_sysfs(hba);
	destroy_workqueue(hba->clk_scaling.workq);
	ufshcd_devfreq_remove(hba);
	hba->clk_scaling.is_initialized = false;
}

static void ufshcd_ungate_work(struct work_struct *work)
{
	int ret;
	unsigned long flags;
	struct ufs_hba *hba = container_of(work, struct ufs_hba,
			clk_gating.ungate_work);

	cancel_delayed_work_sync(&hba->clk_gating.gate_work);

	spin_lock_irqsave(hba->host->host_lock, flags);
	if (hba->clk_gating.state == CLKS_ON) {
		spin_unlock_irqrestore(hba->host->host_lock, flags);
		goto unblock_reqs;
	}

	spin_unlock_irqrestore(hba->host->host_lock, flags);
	ufshcd_hba_vreg_set_hpm(hba);
	ufshcd_setup_clocks(hba, true);

	ufshcd_enable_irq(hba);

	/* Exit from hibern8 */
	if (ufshcd_can_hibern8_during_gating(hba)) {
		/* Prevent gating in this path */
		hba->clk_gating.is_suspended = true;
		if (ufshcd_is_link_hibern8(hba)) {
			ret = ufshcd_uic_hibern8_exit(hba);
			if (ret)
				dev_err(hba->dev, "%s: hibern8 exit failed %d\n",
					__func__, ret);
			else
				ufshcd_set_link_active(hba);
		}
		hba->clk_gating.is_suspended = false;
	}
unblock_reqs:
	ufshcd_scsi_unblock_requests(hba);
}

/**
 * ufshcd_hold - Enable clocks that were gated earlier due to ufshcd_release.
 * Also, exit from hibern8 mode and set the link as active.
 * @hba: per adapter instance
 * @async: This indicates whether caller should ungate clocks asynchronously.
 */
int ufshcd_hold(struct ufs_hba *hba, bool async)
{
	int rc = 0;
	bool flush_result;
	unsigned long flags;

	if (!ufshcd_is_clkgating_allowed(hba) ||
	    !hba->clk_gating.is_initialized)
		goto out;
	spin_lock_irqsave(hba->host->host_lock, flags);
	hba->clk_gating.active_reqs++;

start:
	switch (hba->clk_gating.state) {
	case CLKS_ON:
		/*
		 * Wait for the ungate work to complete if in progress.
		 * Though the clocks may be in ON state, the link could
		 * still be in hibner8 state if hibern8 is allowed
		 * during clock gating.
		 * Make sure we exit hibern8 state also in addition to
		 * clocks being ON.
		 */
		if (ufshcd_can_hibern8_during_gating(hba) &&
		    ufshcd_is_link_hibern8(hba)) {
			if (async) {
				rc = -EAGAIN;
				hba->clk_gating.active_reqs--;
				break;
			}
			spin_unlock_irqrestore(hba->host->host_lock, flags);
			flush_result = flush_work(&hba->clk_gating.ungate_work);
			if (hba->clk_gating.is_suspended && !flush_result)
				goto out;
			spin_lock_irqsave(hba->host->host_lock, flags);
			goto start;
		}
		break;
	case REQ_CLKS_OFF:
		if (cancel_delayed_work(&hba->clk_gating.gate_work)) {
			hba->clk_gating.state = CLKS_ON;
			trace_ufshcd_clk_gating(dev_name(hba->dev),
						hba->clk_gating.state);
			break;
		}
		/*
		 * If we are here, it means gating work is either done or
		 * currently running. Hence, fall through to cancel gating
		 * work and to enable clocks.
		 */
		fallthrough;
	case CLKS_OFF:
		hba->clk_gating.state = REQ_CLKS_ON;
		trace_ufshcd_clk_gating(dev_name(hba->dev),
					hba->clk_gating.state);
		if (queue_work(hba->clk_gating.clk_gating_workq,
			       &hba->clk_gating.ungate_work))
			ufshcd_scsi_block_requests(hba);
		/*
		 * fall through to check if we should wait for this
		 * work to be done or not.
		 */
		fallthrough;
	case REQ_CLKS_ON:
		if (async) {
			rc = -EAGAIN;
			hba->clk_gating.active_reqs--;
			break;
		}

		spin_unlock_irqrestore(hba->host->host_lock, flags);
		flush_work(&hba->clk_gating.ungate_work);
		/* Make sure state is CLKS_ON before returning */
		spin_lock_irqsave(hba->host->host_lock, flags);
		goto start;
	default:
		dev_err(hba->dev, "%s: clk gating is in invalid state %d\n",
				__func__, hba->clk_gating.state);
		break;
	}
	spin_unlock_irqrestore(hba->host->host_lock, flags);
out:
	return rc;
}
EXPORT_SYMBOL_GPL(ufshcd_hold);

static void ufshcd_gate_work(struct work_struct *work)
{
	struct ufs_hba *hba = container_of(work, struct ufs_hba,
			clk_gating.gate_work.work);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(hba->host->host_lock, flags);
	/*
	 * In case you are here to cancel this work the gating state
	 * would be marked as REQ_CLKS_ON. In this case save time by
	 * skipping the gating work and exit after changing the clock
	 * state to CLKS_ON.
	 */
	if (hba->clk_gating.is_suspended ||
		(hba->clk_gating.state != REQ_CLKS_OFF)) {
		hba->clk_gating.state = CLKS_ON;
		trace_ufshcd_clk_gating(dev_name(hba->dev),
					hba->clk_gating.state);
		goto rel_lock;
	}

	if (hba->clk_gating.active_reqs
		|| hba->ufshcd_state != UFSHCD_STATE_OPERATIONAL
		|| ufshcd_any_tag_in_use(hba) || hba->outstanding_tasks
		|| hba->active_uic_cmd || hba->uic_async_done)
		goto rel_lock;

	spin_unlock_irqrestore(hba->host->host_lock, flags);

	/* put the link into hibern8 mode before turning off clocks */
	if (ufshcd_can_hibern8_during_gating(hba)) {
		ret = ufshcd_uic_hibern8_enter(hba);
		if (ret) {
			hba->clk_gating.state = CLKS_ON;
			dev_err(hba->dev, "%s: hibern8 enter failed %d\n",
					__func__, ret);
			trace_ufshcd_clk_gating(dev_name(hba->dev),
						hba->clk_gating.state);
			goto out;
		}
		ufshcd_set_link_hibern8(hba);
	}

	ufshcd_disable_irq(hba);

	ufshcd_setup_clocks(hba, false);

	/* Put the host controller in low power mode if possible */
	ufshcd_hba_vreg_set_lpm(hba);
	/*
	 * In case you are here to cancel this work the gating state
	 * would be marked as REQ_CLKS_ON. In this case keep the state
	 * as REQ_CLKS_ON which would anyway imply that clocks are off
	 * and a request to turn them on is pending. By doing this way,
	 * we keep the state machine in tact and this would ultimately
	 * prevent from doing cancel work multiple times when there are
	 * new requests arriving before the current cancel work is done.
	 */
	spin_lock_irqsave(hba->host->host_lock, flags);
	if (hba->clk_gating.state == REQ_CLKS_OFF) {
		hba->clk_gating.state = CLKS_OFF;
		trace_ufshcd_clk_gating(dev_name(hba->dev),
					hba->clk_gating.state);
	}
rel_lock:
	spin_unlock_irqrestore(hba->host->host_lock, flags);
out:
	return;
}

/* host lock must be held before calling this variant */
static void __ufshcd_release(struct ufs_hba *hba)
{
	if (!ufshcd_is_clkgating_allowed(hba))
		return;

	hba->clk_gating.active_reqs--;

	if (hba->clk_gating.active_reqs || hba->clk_gating.is_suspended ||
	    hba->ufshcd_state != UFSHCD_STATE_OPERATIONAL ||
	    hba->outstanding_tasks || !hba->clk_gating.is_initialized ||
	    hba->active_uic_cmd || hba->uic_async_done ||
	    hba->clk_gating.state == CLKS_OFF)
		return;

	hba->clk_gating.state = REQ_CLKS_OFF;
	trace_ufshcd_clk_gating(dev_name(hba->dev), hba->clk_gating.state);
	queue_delayed_work(hba->clk_gating.clk_gating_workq,
			   &hba->clk_gating.gate_work,
			   msecs_to_jiffies(hba->clk_gating.delay_ms));
}

void ufshcd_release(struct ufs_hba *hba)
{
	unsigned long flags;

	spin_lock_irqsave(hba->host->host_lock, flags);
	__ufshcd_release(hba);
	spin_unlock_irqrestore(hba->host->host_lock, flags);
}
EXPORT_SYMBOL_GPL(ufshcd_release);

static ssize_t ufshcd_clkgate_delay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lu\n", hba->clk_gating.delay_ms);
}

static ssize_t ufshcd_clkgate_delay_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	unsigned long flags, value;

	if (kstrtoul(buf, 0, &value))
		return -EINVAL;

	spin_lock_irqsave(hba->host->host_lock, flags);
	hba->clk_gating.delay_ms = value;
	spin_unlock_irqrestore(hba->host->host_lock, flags);
	return count;
}

static ssize_t ufshcd_clkgate_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", hba->clk_gating.is_enabled);
}

static ssize_t ufshcd_clkgate_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	unsigned long flags;
	u32 value;

	if (kstrtou32(buf, 0, &value))
		return -EINVAL;

	value = !!value;

	spin_lock_irqsave(hba->host->host_lock, flags);
	if (value == hba->clk_gating.is_enabled)
		goto out;

	if (value)
		__ufshcd_release(hba);
	else
		hba->clk_gating.active_reqs++;

	hba->clk_gating.is_enabled = value;
out:
	spin_unlock_irqrestore(hba->host->host_lock, flags);
	return count;
}

static void ufshcd_init_clk_gating_sysfs(struct ufs_hba *hba)
{
	hba->clk_gating.delay_attr.show = ufshcd_clkgate_delay_show;
	hba->clk_gating.delay_attr.store = ufshcd_clkgate_delay_store;
	sysfs_attr_init(&hba->clk_gating.delay_attr.attr);
	hba->clk_gating.delay_attr.attr.name = "clkgate_delay_ms";
	hba->clk_gating.delay_attr.attr.mode = 0644;
	if (device_create_file(hba->dev, &hba->clk_gating.delay_attr))
		dev_err(hba->dev, "Failed to create sysfs for clkgate_delay\n");

	hba->clk_gating.enable_attr.show = ufshcd_clkgate_enable_show;
	hba->clk_gating.enable_attr.store = ufshcd_clkgate_enable_store;
	sysfs_attr_init(&hba->clk_gating.enable_attr.attr);
	hba->clk_gating.enable_attr.attr.name = "clkgate_enable";
	hba->clk_gating.enable_attr.attr.mode = 0644;
	if (device_create_file(hba->dev, &hba->clk_gating.enable_attr))
		dev_err(hba->dev, "Failed to create sysfs for clkgate_enable\n");
}

static void ufshcd_remove_clk_gating_sysfs(struct ufs_hba *hba)
{
	if (hba->clk_gating.delay_attr.attr.name)
		device_remove_file(hba->dev, &hba->clk_gating.delay_attr);
	if (hba->clk_gating.enable_attr.attr.name)
		device_remove_file(hba->dev, &hba->clk_gating.enable_attr);
}

static void ufshcd_init_clk_gating(struct ufs_hba *hba)
{
	char wq_name[sizeof("ufs_clk_gating_00")];

	if (!ufshcd_is_clkgating_allowed(hba))
		return;

	hba->clk_gating.state = CLKS_ON;

	hba->clk_gating.delay_ms = 150;
	INIT_DELAYED_WORK(&hba->clk_gating.gate_work, ufshcd_gate_work);
	INIT_WORK(&hba->clk_gating.ungate_work, ufshcd_ungate_work);

	snprintf(wq_name, ARRAY_SIZE(wq_name), "ufs_clk_gating_%d",
		 hba->host->host_no);
	hba->clk_gating.clk_gating_workq = alloc_ordered_workqueue(wq_name,
					WQ_MEM_RECLAIM | WQ_HIGHPRI);

	ufshcd_init_clk_gating_sysfs(hba);

	hba->clk_gating.is_enabled = true;
	hba->clk_gating.is_initialized = true;
}

static void ufshcd_exit_clk_gating(struct ufs_hba *hba)
{
	if (!hba->clk_gating.is_initialized)
		return;

	ufshcd_remove_clk_gating_sysfs(hba);

	/* Ungate the clock if necessary. */
	ufshcd_hold(hba, false);
	hba->clk_gating.is_initialized = false;
	ufshcd_release(hba);

	destroy_workqueue(hba->clk_gating.clk_gating_workq);
}

/* Must be called with host lock acquired */
static void ufshcd_clk_scaling_start_busy(struct ufs_hba *hba)
{
	bool queue_resume_work = false;
	ktime_t curr_t = ktime_get();
	unsigned long flags;

	if (!ufshcd_is_clkscaling_supported(hba))
		return;

	spin_lock_irqsave(hba->host->host_lock, flags);
	if (!hba->clk_scaling.active_reqs++)
		queue_resume_work = true;

	if (!hba->clk_scaling.is_enabled || hba->pm_op_in_progress) {
		spin_unlock_irqrestore(hba->host->host_lock, flags);
		return;
	}

	if (queue_resume_work)
		queue_work(hba->clk_scaling.workq,
			   &hba->clk_scaling.resume_work);

	if (!hba->clk_scaling.window_start_t) {
		hba->clk_scaling.window_start_t = curr_t;
		hba->clk_scaling.tot_busy_t = 0;
		hba->clk_scaling.is_busy_started = false;
	}

	if (!hba->clk_scaling.is_busy_started) {
		hba->clk_scaling.busy_start_t = curr_t;
		hba->clk_scaling.is_busy_started = true;
	}
	spin_unlock_irqrestore(hba->host->host_lock, flags);
}

static void ufshcd_clk_scaling_update_busy(struct ufs_hba *hba)
{
	struct ufs_clk_scaling *scaling = &hba->clk_scaling;
	unsigned long flags;

	if (!ufshcd_is_clkscaling_supported(hba))
		return;

	spin_lock_irqsave(hba->host->host_lock, flags);
	hba->clk_scaling.active_reqs--;
	if (!hba->outstanding_reqs && scaling->is_busy_started) {
		scaling->tot_busy_t += ktime_to_us(ktime_sub(ktime_get(),
					scaling->busy_start_t));
		scaling->busy_start_t = 0;
		scaling->is_busy_started = false;
	}
	spin_unlock_irqrestore(hba->host->host_lock, flags);
}

static inline int ufshcd_monitor_opcode2dir(u8 opcode)
{
	if (opcode == READ_6 || opcode == READ_10 || opcode == READ_16)
		return READ;
	else if (opcode == WRITE_6 || opcode == WRITE_10 || opcode == WRITE_16)
		return WRITE;
	else
		return -EINVAL;
}

static inline bool ufshcd_should_inform_monitor(struct ufs_hba *hba,
						struct ufshcd_lrb *lrbp)
{
	struct ufs_hba_monitor *m = &hba->monitor;

	return (m->enabled && lrbp && lrbp->cmd &&
		(!m->chunk_size || m->chunk_size == lrbp->cmd->sdb.length) &&
		ktime_before(hba->monitor.enabled_ts, lrbp->issue_time_stamp));
}

static void ufshcd_start_monitor(struct ufs_hba *hba, struct ufshcd_lrb *lrbp)
{
	int dir = ufshcd_monitor_opcode2dir(*lrbp->cmd->cmnd);
	unsigned long flags;

	spin_lock_irqsave(hba->host->host_lock, flags);
	if (dir >= 0 && hba->monitor.nr_queued[dir]++ == 0)
		hba->monitor.busy_start_ts[dir] = ktime_get();
	spin_unlock_irqrestore(hba->host->host_lock, flags);
}

static void ufshcd_update_monitor(struct ufs_hba *hba, struct ufshcd_lrb *lrbp)
{
	int dir = ufshcd_monitor_opcode2dir(*lrbp->cmd->cmnd);
	unsigned long flags;

	spin_lock_irqsave(hba->host->host_lock, flags);
	if (dir >= 0 && hba->monitor.nr_queued[dir] > 0) {
		struct request *req = scsi_cmd_to_rq(lrbp->cmd);
		struct ufs_hba_monitor *m = &hba->monitor;
		ktime_t now, inc, lat;

		now = lrbp->compl_time_stamp;
		inc = ktime_sub(now, m->busy_start_ts[dir]);
		m->total_busy[dir] = ktime_add(m->total_busy[dir], inc);
		m->nr_sec_rw[dir] += blk_rq_sectors(req);

		/* Update latencies */
		m->nr_req[dir]++;
		lat = ktime_sub(now, lrbp->issue_time_stamp);
		m->lat_sum[dir] += lat;
		if (m->lat_max[dir] < lat || !m->lat_max[dir])
			m->lat_max[dir] = lat;
		if (m->lat_min[dir] > lat || !m->lat_min[dir])
			m->lat_min[dir] = lat;

		m->nr_queued[dir]--;
		/* Push forward the busy start of monitor */
		m->busy_start_ts[dir] = now;
	}
	spin_unlock_irqrestore(hba->host->host_lock, flags);
}

/**
 * ufshcd_send_command - Send SCSI or device management commands
 * @hba: per adapter instance
 * @task_tag: Task tag of the command
 */
static inline
void ufshcd_send_command(struct ufs_hba *hba, unsigned int task_tag)
{
	struct ufshcd_lrb *lrbp = &hba->lrb[task_tag];
	unsigned long flags;

	lrbp->issue_time_stamp = ktime_get();
	lrbp->compl_time_stamp = ktime_set(0, 0);
	ufshcd_add_command_trace(hba, task_tag, UFS_CMD_SEND);
	ufshcd_clk_scaling_start_busy(hba);
	if (unlikely(ufshcd_should_inform_monitor(hba, lrbp)))
		ufshcd_start_monitor(hba, lrbp);

	spin_lock_irqsave(&hba->outstanding_lock, flags);
	if (hba->vops && hba->vops->setup_xfer_req)
		hba->vops->setup_xfer_req(hba, task_tag, !!lrbp->cmd);
	__set_bit(task_tag, &hba->outstanding_reqs);
	ufshcd_writel(hba, 1 << task_tag, REG_UTP_TRANSFER_REQ_DOOR_BELL);
	spin_unlock_irqrestore(&hba->outstanding_lock, flags);

	/* Make sure that doorbell is committed immediately */
	wmb();
}

/**
 * ufshcd_copy_sense_data - Copy sense data in case of check condition
 * @lrbp: pointer to local reference block
 */
static inline void ufshcd_copy_sense_data(struct ufshcd_lrb *lrbp)
{
	int len;
	if (lrbp->sense_buffer &&
	    ufshcd_get_rsp_upiu_data_seg_len(lrbp->ucd_rsp_ptr)) {
		int len_to_copy;

		len = be16_to_cpu(lrbp->ucd_rsp_ptr->sr.sense_data_len);
		len_to_copy = min_t(int, UFS_SENSE_SIZE, len);

		memcpy(lrbp->sense_buffer, lrbp->ucd_rsp_ptr->sr.sense_data,
		       len_to_copy);
	}
}

/**
 * ufshcd_copy_query_response() - Copy the Query Response and the data
 * descriptor
 * @hba: per adapter instance
 * @lrbp: pointer to local reference block
 */
static
int ufshcd_copy_query_response(struct ufs_hba *hba, struct ufshcd_lrb *lrbp)
{
	struct ufs_query_res *query_res = &hba->dev_cmd.query.response;

	memcpy(&query_res->upiu_res, &lrbp->ucd_rsp_ptr->qr, QUERY_OSF_SIZE);

	/* Get the descriptor */
	if (hba->dev_cmd.query.descriptor &&
	    lrbp->ucd_rsp_ptr->qr.opcode == UPIU_QUERY_OPCODE_READ_DESC) {
		u8 *descp = (u8 *)lrbp->ucd_rsp_ptr +
				GENERAL_UPIU_REQUEST_SIZE;
		u16 resp_len;
		u16 buf_len;

		/* data segment length */
		resp_len = be32_to_cpu(lrbp->ucd_rsp_ptr->header.dword_2) &
						MASK_QUERY_DATA_SEG_LEN;
		buf_len = be16_to_cpu(
				hba->dev_cmd.query.request.upiu_req.length);
		if (likely(buf_len >= resp_len)) {
			memcpy(hba->dev_cmd.query.descriptor, descp, resp_len);
		} else {
			dev_warn(hba->dev,
				 "%s: rsp size %d is bigger than buffer size %d",
				 __func__, resp_len, buf_len);
			return -EINVAL;
		}
	}

	return 0;
}

/**
 * ufshcd_hba_capabilities - Read controller capabilities
 * @hba: per adapter instance
 *
 * Return: 0 on success, negative on error.
 */
static inline int ufshcd_hba_capabilities(struct ufs_hba *hba)
{
	int err;

	hba->capabilities = ufshcd_readl(hba, REG_CONTROLLER_CAPABILITIES);

	/* nutrs and nutmrs are 0 based values */
	hba->nutrs = (hba->capabilities & MASK_TRANSFER_REQUESTS_SLOTS) + 1;
	hba->nutmrs =
	((hba->capabilities & MASK_TASK_MANAGEMENT_REQUEST_SLOTS) >> 16) + 1;
	hba->reserved_slot = hba->nutrs - 1;

	/* Read crypto capabilities */
	err = ufshcd_hba_init_crypto_capabilities(hba);
	if (err)
		dev_err(hba->dev, "crypto setup failed\n");

	return err;
}

/**
 * ufshcd_ready_for_uic_cmd - Check if controller is ready
 *                            to accept UIC commands
 * @hba: per adapter instance
 * Return true on success, else false
 */
static inline bool ufshcd_ready_for_uic_cmd(struct ufs_hba *hba)
{
	if (ufshcd_readl(hba, REG_CONTROLLER_STATUS) & UIC_COMMAND_READY)
		return true;
	else
		return false;
}

/**
 * ufshcd_get_upmcrs - Get the power mode change request status
 * @hba: Pointer to adapter instance
 *
 * This function gets the UPMCRS field of HCS register
 * Returns value of UPMCRS field
 */
static inline u8 ufshcd_get_upmcrs(struct ufs_hba *hba)
{
	return (ufshcd_readl(hba, REG_CONTROLLER_STATUS) >> 8) & 0x7;
}

/**
 * ufshcd_dispatch_uic_cmd - Dispatch an UIC command to the Unipro layer
 * @hba: per adapter instance
 * @uic_cmd: UIC command
 */
static inline void
ufshcd_dispatch_uic_cmd(struct ufs_hba *hba, struct uic_command *uic_cmd)
{
	lockdep_assert_held(&hba->uic_cmd_mutex);

	WARN_ON(hba->active_uic_cmd);

	hba->active_uic_cmd = uic_cmd;

	/* Write Args */
	ufshcd_writel(hba, uic_cmd->argument1, REG_UIC_COMMAND_ARG_1);
	ufshcd_writel(hba, uic_cmd->argument2, REG_UIC_COMMAND_ARG_2);
	ufshcd_writel(hba, uic_cmd->argument3, REG_UIC_COMMAND_ARG_3);

	ufshcd_add_uic_command_trace(hba, uic_cmd, UFS_CMD_SEND);

	/* Write UIC Cmd */
	ufshcd_writel(hba, uic_cmd->command & COMMAND_OPCODE_MASK,
		      REG_UIC_COMMAND);
}

/**
 * ufshcd_wait_for_uic_cmd - Wait for completion of an UIC command
 * @hba: per adapter instance
 * @uic_cmd: UIC command
 *
 * Returns 0 only if success.
 */
static int
ufshcd_wait_for_uic_cmd(struct ufs_hba *hba, struct uic_command *uic_cmd)
{
	int ret;
	unsigned long flags;

	lockdep_assert_held(&hba->uic_cmd_mutex);

	if (wait_for_completion_timeout(&uic_cmd->done,
					msecs_to_jiffies(UIC_CMD_TIMEOUT))) {
		ret = uic_cmd->argument2 & MASK_UIC_COMMAND_RESULT;
	} else {
		ret = -ETIMEDOUT;
		dev_err(hba->dev,
			"uic cmd 0x%x with arg3 0x%x completion timeout\n",
			uic_cmd->command, uic_cmd->argument3);

		if (!uic_cmd->cmd_active) {
			dev_err(hba->dev, "%s: UIC cmd has been completed, return the result\n",
				__func__);
			ret = uic_cmd->argument2 & MASK_UIC_COMMAND_RESULT;
		}
	}

	spin_lock_irqsave(hba->host->host_lock, flags);
	hba->active_uic_cmd = NULL;
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	return ret;
}

/**
 * __ufshcd_send_uic_cmd - Send UIC commands and retrieve the result
 * @hba: per adapter instance
 * @uic_cmd: UIC command
 * @completion: initialize the completion only if this is set to true
 *
 * Returns 0 only if success.
 */
static int
__ufshcd_send_uic_cmd(struct ufs_hba *hba, struct uic_command *uic_cmd,
		      bool completion)
{
	lockdep_assert_held(&hba->uic_cmd_mutex);
	lockdep_assert_held(hba->host->host_lock);

	if (!ufshcd_ready_for_uic_cmd(hba)) {
		dev_err(hba->dev,
			"Controller not ready to accept UIC commands\n");
		return -EIO;
	}

	if (completion)
		init_completion(&uic_cmd->done);

	uic_cmd->cmd_active = 1;
	ufshcd_dispatch_uic_cmd(hba, uic_cmd);

	return 0;
}

/**
 * ufshcd_send_uic_cmd - Send UIC commands and retrieve the result
 * @hba: per adapter instance
 * @uic_cmd: UIC command
 *
 * Returns 0 only if success.
 */
int ufshcd_send_uic_cmd(struct ufs_hba *hba, struct uic_command *uic_cmd)
{
	int ret;
	unsigned long flags;

	ufshcd_hold(hba, false);
	mutex_lock(&hba->uic_cmd_mutex);
	ufshcd_add_delay_before_dme_cmd(hba);

	spin_lock_irqsave(hba->host->host_lock, flags);
	ret = __ufshcd_send_uic_cmd(hba, uic_cmd, true);
	spin_unlock_irqrestore(hba->host->host_lock, flags);
	if (!ret)
		ret = ufshcd_wait_for_uic_cmd(hba, uic_cmd);

	mutex_unlock(&hba->uic_cmd_mutex);

	ufshcd_release(hba);
	return ret;
}

/**
 * ufshcd_map_sg - Map scatter-gather list to prdt
 * @hba: per adapter instance
 * @lrbp: pointer to local reference block
 *
 * Returns 0 in case of success, non-zero value in case of failure
 */
static int ufshcd_map_sg(struct ufs_hba *hba, struct ufshcd_lrb *lrbp)
{
	struct ufshcd_sg_entry *prd_table;
	struct scatterlist *sg;
	struct scsi_cmnd *cmd;
	int sg_segments;
	int i;

	cmd = lrbp->cmd;
	sg_segments = scsi_dma_map(cmd);
	if (sg_segments < 0)
		return sg_segments;

	if (sg_segments) {

		if (hba->quirks & UFSHCD_QUIRK_PRDT_BYTE_GRAN)
			lrbp->utr_descriptor_ptr->prd_table_length =
				cpu_to_le16((sg_segments *
					sizeof(struct ufshcd_sg_entry)));
		else
			lrbp->utr_descriptor_ptr->prd_table_length =
				cpu_to_le16((u16) (sg_segments));

		prd_table = (struct ufshcd_sg_entry *)lrbp->ucd_prdt_ptr;

		scsi_for_each_sg(cmd, sg, sg_segments, i) {
			prd_table[i].size  =
				cpu_to_le32(((u32) sg_dma_len(sg))-1);
			prd_table[i].base_addr =
				cpu_to_le32(lower_32_bits(sg->dma_address));
			prd_table[i].upper_addr =
				cpu_to_le32(upper_32_bits(sg->dma_address));
			prd_table[i].reserved = 0;
		}
	} else {
		lrbp->utr_descriptor_ptr->prd_table_length = 0;
	}

	return 0;
}

/**
 * ufshcd_enable_intr - enable interrupts
 * @hba: per adapter instance
 * @intrs: interrupt bits
 */
static void ufshcd_enable_intr(struct ufs_hba *hba, u32 intrs)
{
	u32 set = ufshcd_readl(hba, REG_INTERRUPT_ENABLE);

	if (hba->ufs_version == ufshci_version(1, 0)) {
		u32 rw;
		rw = set & INTERRUPT_MASK_RW_VER_10;
		set = rw | ((set ^ intrs) & intrs);
	} else {
		set |= intrs;
	}

	ufshcd_writel(hba, set, REG_INTERRUPT_ENABLE);
}

/**
 * ufshcd_disable_intr - disable interrupts
 * @hba: per adapter instance
 * @intrs: interrupt bits
 */
static void ufshcd_disable_intr(struct ufs_hba *hba, u32 intrs)
{
	u32 set = ufshcd_readl(hba, REG_INTERRUPT_ENABLE);

	if (hba->ufs_version == ufshci_version(1, 0)) {
		u32 rw;
		rw = (set & INTERRUPT_MASK_RW_VER_10) &
			~(intrs & INTERRUPT_MASK_RW_VER_10);
		set = rw | ((set & intrs) & ~INTERRUPT_MASK_RW_VER_10);

	} else {
		set &= ~intrs;
	}

	ufshcd_writel(hba, set, REG_INTERRUPT_ENABLE);
}

/**
 * ufshcd_prepare_req_desc_hdr() - Fills the requests header
 * descriptor according to request
 * @lrbp: pointer to local reference block
 * @upiu_flags: flags required in the header
 * @cmd_dir: requests data direction
 */
static void ufshcd_prepare_req_desc_hdr(struct ufshcd_lrb *lrbp,
			u8 *upiu_flags, enum dma_data_direction cmd_dir)
{
	struct utp_transfer_req_desc *req_desc = lrbp->utr_descriptor_ptr;
	u32 data_direction;
	u32 dword_0;
	u32 dword_1 = 0;
	u32 dword_3 = 0;

	if (cmd_dir == DMA_FROM_DEVICE) {
		data_direction = UTP_DEVICE_TO_HOST;
		*upiu_flags = UPIU_CMD_FLAGS_READ;
	} else if (cmd_dir == DMA_TO_DEVICE) {
		data_direction = UTP_HOST_TO_DEVICE;
		*upiu_flags = UPIU_CMD_FLAGS_WRITE;
	} else {
		data_direction = UTP_NO_DATA_TRANSFER;
		*upiu_flags = UPIU_CMD_FLAGS_NONE;
	}

	dword_0 = data_direction | (lrbp->command_type
				<< UPIU_COMMAND_TYPE_OFFSET);
	if (lrbp->intr_cmd)
		dword_0 |= UTP_REQ_DESC_INT_CMD;

	/* Prepare crypto related dwords */
	ufshcd_prepare_req_desc_hdr_crypto(lrbp, &dword_0, &dword_1, &dword_3);

	/* Transfer request descriptor header fields */
	req_desc->header.dword_0 = cpu_to_le32(dword_0);
	req_desc->header.dword_1 = cpu_to_le32(dword_1);
	/*
	 * assigning invalid value for command status. Controller
	 * updates OCS on command completion, with the command
	 * status
	 */
	req_desc->header.dword_2 =
		cpu_to_le32(OCS_INVALID_COMMAND_STATUS);
	req_desc->header.dword_3 = cpu_to_le32(dword_3);

	req_desc->prd_table_length = 0;
}

/**
 * ufshcd_prepare_utp_scsi_cmd_upiu() - fills the utp_transfer_req_desc,
 * for scsi commands
 * @lrbp: local reference block pointer
 * @upiu_flags: flags
 */
static
void ufshcd_prepare_utp_scsi_cmd_upiu(struct ufshcd_lrb *lrbp, u8 upiu_flags)
{
	struct scsi_cmnd *cmd = lrbp->cmd;
	struct utp_upiu_req *ucd_req_ptr = lrbp->ucd_req_ptr;
	unsigned short cdb_len;

	/* command descriptor fields */
	ucd_req_ptr->header.dword_0 = UPIU_HEADER_DWORD(
				UPIU_TRANSACTION_COMMAND, upiu_flags,
				lrbp->lun, lrbp->task_tag);
	ucd_req_ptr->header.dword_1 = UPIU_HEADER_DWORD(
				UPIU_COMMAND_SET_TYPE_SCSI, 0, 0, 0);

	/* Total EHS length and Data segment length will be zero */
	ucd_req_ptr->header.dword_2 = 0;

	ucd_req_ptr->sc.exp_data_transfer_len = cpu_to_be32(cmd->sdb.length);

	cdb_len = min_t(unsigned short, cmd->cmd_len, UFS_CDB_SIZE);
	memset(ucd_req_ptr->sc.cdb, 0, UFS_CDB_SIZE);
	memcpy(ucd_req_ptr->sc.cdb, cmd->cmnd, cdb_len);

	memset(lrbp->ucd_rsp_ptr, 0, sizeof(struct utp_upiu_rsp));
}

/**
 * ufshcd_prepare_utp_query_req_upiu() - fills the utp_transfer_req_desc,
 * for query requsts
 * @hba: UFS hba
 * @lrbp: local reference block pointer
 * @upiu_flags: flags
 */
static void ufshcd_prepare_utp_query_req_upiu(struct ufs_hba *hba,
				struct ufshcd_lrb *lrbp, u8 upiu_flags)
{
	struct utp_upiu_req *ucd_req_ptr = lrbp->ucd_req_ptr;
	struct ufs_query *query = &hba->dev_cmd.query;
	u16 len = be16_to_cpu(query->request.upiu_req.length);

	/* Query request header */
	ucd_req_ptr->header.dword_0 = UPIU_HEADER_DWORD(
			UPIU_TRANSACTION_QUERY_REQ, upiu_flags,
			lrbp->lun, lrbp->task_tag);
	ucd_req_ptr->header.dword_1 = UPIU_HEADER_DWORD(
			0, query->request.query_func, 0, 0);

	/* Data segment length only need for WRITE_DESC */
	if (query->request.upiu_req.opcode == UPIU_QUERY_OPCODE_WRITE_DESC)
		ucd_req_ptr->header.dword_2 =
			UPIU_HEADER_DWORD(0, 0, (len >> 8), (u8)len);
	else
		ucd_req_ptr->header.dword_2 = 0;

	/* Copy the Query Request buffer as is */
	memcpy(&ucd_req_ptr->qr, &query->request.upiu_req,
			QUERY_OSF_SIZE);

	/* Copy the Descriptor */
	if (query->request.upiu_req.opcode == UPIU_QUERY_OPCODE_WRITE_DESC)
		memcpy(ucd_req_ptr + 1, query->descriptor, len);

	memset(lrbp->ucd_rsp_ptr, 0, sizeof(struct utp_upiu_rsp));
}

static inline void ufshcd_prepare_utp_nop_upiu(struct ufshcd_lrb *lrbp)
{
	struct utp_upiu_req *ucd_req_ptr = lrbp->ucd_req_ptr;

	memset(ucd_req_ptr, 0, sizeof(struct utp_upiu_req));

	/* command descriptor fields */
	ucd_req_ptr->header.dword_0 =
		UPIU_HEADER_DWORD(
			UPIU_TRANSACTION_NOP_OUT, 0, 0, lrbp->task_tag);
	/* clear rest of the fields of basic header */
	ucd_req_ptr->header.dword_1 = 0;
	ucd_req_ptr->header.dword_2 = 0;

	memset(lrbp->ucd_rsp_ptr, 0, sizeof(struct utp_upiu_rsp));
}

/**
 * ufshcd_compose_devman_upiu - UFS Protocol Information Unit(UPIU)
 *			     for Device Management Purposes
 * @hba: per adapter instance
 * @lrbp: pointer to local reference block
 */
static int ufshcd_compose_devman_upiu(struct ufs_hba *hba,
				      struct ufshcd_lrb *lrbp)
{
	u8 upiu_flags;
	int ret = 0;

	if (hba->ufs_version <= ufshci_version(1, 1))
		lrbp->command_type = UTP_CMD_TYPE_DEV_MANAGE;
	else
		lrbp->command_type = UTP_CMD_TYPE_UFS_STORAGE;

	ufshcd_prepare_req_desc_hdr(lrbp, &upiu_flags, DMA_NONE);
	if (hba->dev_cmd.type == DEV_CMD_TYPE_QUERY)
		ufshcd_prepare_utp_query_req_upiu(hba, lrbp, upiu_flags);
	else if (hba->dev_cmd.type == DEV_CMD_TYPE_NOP)
		ufshcd_prepare_utp_nop_upiu(lrbp);
	else
		ret = -EINVAL;

	return ret;
}

/**
 * ufshcd_comp_scsi_upiu - UFS Protocol Information Unit(UPIU)
 *			   for SCSI Purposes
 * @hba: per adapter instance
 * @lrbp: pointer to local reference block
 */
static int ufshcd_comp_scsi_upiu(struct ufs_hba *hba, struct ufshcd_lrb *lrbp)
{
	u8 upiu_flags;
	int ret = 0;

	if (hba->ufs_version <= ufshci_version(1, 1))
		lrbp->command_type = UTP_CMD_TYPE_SCSI;
	else
		lrbp->command_type = UTP_CMD_TYPE_UFS_STORAGE;

	if (likely(lrbp->cmd)) {
		ufshcd_prepare_req_desc_hdr(lrbp, &upiu_flags,
						lrbp->cmd->sc_data_direction);
		ufshcd_prepare_utp_scsi_cmd_upiu(lrbp, upiu_flags);
	} else {
		ret = -EINVAL;
	}

	return ret;
}

/**
 * ufshcd_upiu_wlun_to_scsi_wlun - maps UPIU W-LUN id to SCSI W-LUN ID
 * @upiu_wlun_id: UPIU W-LUN id
 *
 * Returns SCSI W-LUN id
 */
static inline u16 ufshcd_upiu_wlun_to_scsi_wlun(u8 upiu_wlun_id)
{
	return (upiu_wlun_id & ~UFS_UPIU_WLUN_ID) | SCSI_W_LUN_BASE;
}

static inline bool is_rpmb_wlun(struct scsi_device *sdev)
{
	return sdev->lun == ufshcd_upiu_wlun_to_scsi_wlun(UFS_UPIU_RPMB_WLUN);
}

static inline bool is_device_wlun(struct scsi_device *sdev)
{
	return sdev->lun ==
		ufshcd_upiu_wlun_to_scsi_wlun(UFS_UPIU_UFS_DEVICE_WLUN);
}

static void ufshcd_init_lrb(struct ufs_hba *hba, struct ufshcd_lrb *lrb, int i)
{
	struct utp_transfer_cmd_desc *cmd_descp = hba->ucdl_base_addr;
	struct utp_transfer_req_desc *utrdlp = hba->utrdl_base_addr;
	dma_addr_t cmd_desc_element_addr = hba->ucdl_dma_addr +
		i * sizeof(struct utp_transfer_cmd_desc);
	u16 response_offset = offsetof(struct utp_transfer_cmd_desc,
				       response_upiu);
	u16 prdt_offset = offsetof(struct utp_transfer_cmd_desc, prd_table);

	lrb->utr_descriptor_ptr = utrdlp + i;
	lrb->utrd_dma_addr = hba->utrdl_dma_addr +
		i * sizeof(struct utp_transfer_req_desc);
	lrb->ucd_req_ptr = (struct utp_upiu_req *)(cmd_descp + i);
	lrb->ucd_req_dma_addr = cmd_desc_element_addr;
	lrb->ucd_rsp_ptr = (struct utp_upiu_rsp *)cmd_descp[i].response_upiu;
	lrb->ucd_rsp_dma_addr = cmd_desc_element_addr + response_offset;
	lrb->ucd_prdt_ptr = (struct ufshcd_sg_entry *)cmd_descp[i].prd_table;
	lrb->ucd_prdt_dma_addr = cmd_desc_element_addr + prdt_offset;
}

/**
 * ufshcd_queuecommand - main entry point for SCSI requests
 * @host: SCSI host pointer
 * @cmd: command from SCSI Midlayer
 *
 * Returns 0 for success, non-zero in case of failure
 */
static int ufshcd_queuecommand(struct Scsi_Host *host, struct scsi_cmnd *cmd)
{
	struct ufs_hba *hba = shost_priv(host);
	int tag = scsi_cmd_to_rq(cmd)->tag;
	struct ufshcd_lrb *lrbp;
	int err = 0;

	WARN_ONCE(tag < 0, "Invalid tag %d\n", tag);

	if (!down_read_trylock(&hba->clk_scaling_lock))
		return SCSI_MLQUEUE_HOST_BUSY;

	switch (hba->ufshcd_state) {
	case UFSHCD_STATE_OPERATIONAL:
	case UFSHCD_STATE_EH_SCHEDULED_NON_FATAL:
		break;
	case UFSHCD_STATE_EH_SCHEDULED_FATAL:
		/*
		 * pm_runtime_get_sync() is used at error handling preparation
		 * stage. If a scsi cmd, e.g. the SSU cmd, is sent from hba's
		 * PM ops, it can never be finished if we let SCSI layer keep
		 * retrying it, which gets err handler stuck forever. Neither
		 * can we let the scsi cmd pass through, because UFS is in bad
		 * state, the scsi cmd may eventually time out, which will get
		 * err handler blocked for too long. So, just fail the scsi cmd
		 * sent from PM ops, err handler can recover PM error anyways.
		 */
		if (hba->pm_op_in_progress) {
			hba->force_reset = true;
			set_host_byte(cmd, DID_BAD_TARGET);
			cmd->scsi_done(cmd);
			goto out;
		}
		fallthrough;
	case UFSHCD_STATE_RESET:
		err = SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	case UFSHCD_STATE_ERROR:
		set_host_byte(cmd, DID_ERROR);
		cmd->scsi_done(cmd);
		goto out;
	}

	hba->req_abort_count = 0;

	err = ufshcd_hold(hba, true);
	if (err) {
		err = SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	}
	WARN_ON(ufshcd_is_clkgating_allowed(hba) &&
		(hba->clk_gating.state != CLKS_ON));

	lrbp = &hba->lrb[tag];
	WARN_ON(lrbp->cmd);
	lrbp->cmd = cmd;
	lrbp->sense_bufflen = UFS_SENSE_SIZE;
	lrbp->sense_buffer = cmd->sense_buffer;
	lrbp->task_tag = tag;
	lrbp->lun = ufshcd_scsi_to_upiu_lun(cmd->device->lun);
	lrbp->intr_cmd = !ufshcd_is_intr_aggr_allowed(hba) ? true : false;

	ufshcd_prepare_lrbp_crypto(scsi_cmd_to_rq(cmd), lrbp);

	lrbp->req_abort_skip = false;

	ufshpb_prep(hba, lrbp);

	ufshcd_comp_scsi_upiu(hba, lrbp);

	err = ufshcd_map_sg(hba, lrbp);
	if (err) {
		lrbp->cmd = NULL;
		ufshcd_release(hba);
		goto out;
	}

	ufshcd_send_command(hba, tag);
out:
	up_read(&hba->clk_scaling_lock);

	if (ufs_trigger_eh()) {
		unsigned long flags;

		spin_lock_irqsave(hba->host->host_lock, flags);
		ufshcd_schedule_eh_work(hba);
		spin_unlock_irqrestore(hba->host->host_lock, flags);
	}

	return err;
}

static int ufshcd_compose_dev_cmd(struct ufs_hba *hba,
		struct ufshcd_lrb *lrbp, enum dev_cmd_type cmd_type, int tag)
{
	lrbp->cmd = NULL;
	lrbp->sense_bufflen = 0;
	lrbp->sense_buffer = NULL;
	lrbp->task_tag = tag;
	lrbp->lun = 0; /* device management cmd is not specific to any LUN */
	lrbp->intr_cmd = true; /* No interrupt aggregation */
	ufshcd_prepare_lrbp_crypto(NULL, lrbp);
	hba->dev_cmd.type = cmd_type;

	return ufshcd_compose_devman_upiu(hba, lrbp);
}

static int
ufshcd_clear_cmd(struct ufs_hba *hba, int tag)
{
	int err = 0;
	unsigned long flags;
	u32 mask = 1 << tag;

	/* clear outstanding transaction before retry */
	spin_lock_irqsave(hba->host->host_lock, flags);
	ufshcd_utrl_clear(hba, tag);
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	/*
	 * wait for h/w to clear corresponding bit in door-bell.
	 * max. wait is 1 sec.
	 */
	err = ufshcd_wait_for_register(hba,
			REG_UTP_TRANSFER_REQ_DOOR_BELL,
			mask, ~mask, 1000, 1000);

	return err;
}

static int
ufshcd_check_query_response(struct ufs_hba *hba, struct ufshcd_lrb *lrbp)
{
	struct ufs_query_res *query_res = &hba->dev_cmd.query.response;

	/* Get the UPIU response */
	query_res->response = ufshcd_get_rsp_upiu_result(lrbp->ucd_rsp_ptr) >>
				UPIU_RSP_CODE_OFFSET;
	return query_res->response;
}

/**
 * ufshcd_dev_cmd_completion() - handles device management command responses
 * @hba: per adapter instance
 * @lrbp: pointer to local reference block
 */
static int
ufshcd_dev_cmd_completion(struct ufs_hba *hba, struct ufshcd_lrb *lrbp)
{
	int resp;
	int err = 0;

	hba->ufs_stats.last_hibern8_exit_tstamp = ktime_set(0, 0);
	resp = ufshcd_get_req_rsp(lrbp->ucd_rsp_ptr);

	switch (resp) {
	case UPIU_TRANSACTION_NOP_IN:
		if (hba->dev_cmd.type != DEV_CMD_TYPE_NOP) {
			err = -EINVAL;
			dev_err(hba->dev, "%s: unexpected response %x\n",
					__func__, resp);
		}
		break;
	case UPIU_TRANSACTION_QUERY_RSP:
		err = ufshcd_check_query_response(hba, lrbp);
		if (!err)
			err = ufshcd_copy_query_response(hba, lrbp);
		break;
	case UPIU_TRANSACTION_REJECT_UPIU:
		/* TODO: handle Reject UPIU Response */
		err = -EPERM;
		dev_err(hba->dev, "%s: Reject UPIU not fully implemented\n",
				__func__);
		break;
	default:
		err = -EINVAL;
		dev_err(hba->dev, "%s: Invalid device management cmd response: %x\n",
				__func__, resp);
		break;
	}

	return err;
}

static int ufshcd_wait_for_dev_cmd(struct ufs_hba *hba,
		struct ufshcd_lrb *lrbp, int max_timeout)
{
	int err = 0;
	unsigned long time_left;
	unsigned long flags;

	time_left = wait_for_completion_timeout(hba->dev_cmd.complete,
			msecs_to_jiffies(max_timeout));

	spin_lock_irqsave(hba->host->host_lock, flags);
	hba->dev_cmd.complete = NULL;
	if (likely(time_left)) {
		err = ufshcd_get_tr_ocs(lrbp);
		if (!err)
			err = ufshcd_dev_cmd_completion(hba, lrbp);
	}
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	if (!time_left) {
		err = -ETIMEDOUT;
		dev_dbg(hba->dev, "%s: dev_cmd request timedout, tag %d\n",
			__func__, lrbp->task_tag);
		if (!ufshcd_clear_cmd(hba, lrbp->task_tag))
			/* successfully cleared the command, retry if needed */
			err = -EAGAIN;
		/*
		 * in case of an error, after clearing the doorbell,
		 * we also need to clear the outstanding_request
		 * field in hba
		 */
		spin_lock_irqsave(&hba->outstanding_lock, flags);
		__clear_bit(lrbp->task_tag, &hba->outstanding_reqs);
		spin_unlock_irqrestore(&hba->outstanding_lock, flags);
	}

	return err;
}

/**
 * ufshcd_exec_dev_cmd - API for sending device management requests
 * @hba: UFS hba
 * @cmd_type: specifies the type (NOP, Query...)
 * @timeout: timeout in milliseconds
 *
 * NOTE: Since there is only one available tag for device management commands,
 * it is expected you hold the hba->dev_cmd.lock mutex.
 */
static int ufshcd_exec_dev_cmd(struct ufs_hba *hba,
		enum dev_cmd_type cmd_type, int timeout)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	const u32 tag = hba->reserved_slot;
	struct ufshcd_lrb *lrbp;
	int err;

	/* Protects use of hba->reserved_slot. */
	lockdep_assert_held(&hba->dev_cmd.lock);

	down_read(&hba->clk_scaling_lock);

	lrbp = &hba->lrb[tag];
	WARN_ON(lrbp->cmd);
	err = ufshcd_compose_dev_cmd(hba, lrbp, cmd_type, tag);
	if (unlikely(err))
		goto out;

	hba->dev_cmd.complete = &wait;

	ufshcd_add_query_upiu_trace(hba, UFS_QUERY_SEND, lrbp->ucd_req_ptr);

	ufshcd_send_command(hba, tag);
	err = ufshcd_wait_for_dev_cmd(hba, lrbp, timeout);
	ufshcd_add_query_upiu_trace(hba, err ? UFS_QUERY_ERR : UFS_QUERY_COMP,
				    (struct utp_upiu_req *)lrbp->ucd_rsp_ptr);

out:
	up_read(&hba->clk_scaling_lock);
	return err;
}

/**
 * ufshcd_init_query() - init the query response and request parameters
 * @hba: per-adapter instance
 * @request: address of the request pointer to be initialized
 * @response: address of the response pointer to be initialized
 * @opcode: operation to perform
 * @idn: flag idn to access
 * @index: LU number to access
 * @selector: query/flag/descriptor further identification
 */
static inline void ufshcd_init_query(struct ufs_hba *hba,
		struct ufs_query_req **request, struct ufs_query_res **response,
		enum query_opcode opcode, u8 idn, u8 index, u8 selector)
{
	*request = &hba->dev_cmd.query.request;
	*response = &hba->dev_cmd.query.response;
	memset(*request, 0, sizeof(struct ufs_query_req));
	memset(*response, 0, sizeof(struct ufs_query_res));
	(*request)->upiu_req.opcode = opcode;
	(*request)->upiu_req.idn = idn;
	(*request)->upiu_req.index = index;
	(*request)->upiu_req.selector = selector;
}

static int ufshcd_query_flag_retry(struct ufs_hba *hba,
	enum query_opcode opcode, enum flag_idn idn, u8 index, bool *flag_res)
{
	int ret;
	int retries;

	for (retries = 0; retries < QUERY_REQ_RETRIES; retries++) {
		ret = ufshcd_query_flag(hba, opcode, idn, index, flag_res);
		if (ret)
			dev_dbg(hba->dev,
				"%s: failed with error %d, retries %d\n",
				__func__, ret, retries);
		else
			break;
	}

	if (ret)
		dev_err(hba->dev,
			"%s: query attribute, opcode %d, idn %d, failed with error %d after %d retires\n",
			__func__, opcode, idn, ret, retries);
	return ret;
}

/**
 * ufshcd_query_flag() - API function for sending flag query requests
 * @hba: per-adapter instance
 * @opcode: flag query to perform
 * @idn: flag idn to access
 * @index: flag index to access
 * @flag_res: the flag value after the query request completes
 *
 * Returns 0 for success, non-zero in case of failure
 */
int ufshcd_query_flag(struct ufs_hba *hba, enum query_opcode opcode,
			enum flag_idn idn, u8 index, bool *flag_res)
{
	struct ufs_query_req *request = NULL;
	struct ufs_query_res *response = NULL;
	int err, selector = 0;
	int timeout = QUERY_REQ_TIMEOUT;

	BUG_ON(!hba);

	ufshcd_hold(hba, false);
	mutex_lock(&hba->dev_cmd.lock);
	ufshcd_init_query(hba, &request, &response, opcode, idn, index,
			selector);

	switch (opcode) {
	case UPIU_QUERY_OPCODE_SET_FLAG:
	case UPIU_QUERY_OPCODE_CLEAR_FLAG:
	case UPIU_QUERY_OPCODE_TOGGLE_FLAG:
		request->query_func = UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST;
		break;
	case UPIU_QUERY_OPCODE_READ_FLAG:
		request->query_func = UPIU_QUERY_FUNC_STANDARD_READ_REQUEST;
		if (!flag_res) {
			/* No dummy reads */
			dev_err(hba->dev, "%s: Invalid argument for read request\n",
					__func__);
			err = -EINVAL;
			goto out_unlock;
		}
		break;
	default:
		dev_err(hba->dev,
			"%s: Expected query flag opcode but got = %d\n",
			__func__, opcode);
		err = -EINVAL;
		goto out_unlock;
	}

	err = ufshcd_exec_dev_cmd(hba, DEV_CMD_TYPE_QUERY, timeout);

	if (err) {
		dev_err(hba->dev,
			"%s: Sending flag query for idn %d failed, err = %d\n",
			__func__, idn, err);
		goto out_unlock;
	}

	if (flag_res)
		*flag_res = (be32_to_cpu(response->upiu_res.value) &
				MASK_QUERY_UPIU_FLAG_LOC) & 0x1;

out_unlock:
	mutex_unlock(&hba->dev_cmd.lock);
	ufshcd_release(hba);
	return err;
}

/**
 * ufshcd_query_attr - API function for sending attribute requests
 * @hba: per-adapter instance
 * @opcode: attribute opcode
 * @idn: attribute idn to access
 * @index: index field
 * @selector: selector field
 * @attr_val: the attribute value after the query request completes
 *
 * Returns 0 for success, non-zero in case of failure
*/
int ufshcd_query_attr(struct ufs_hba *hba, enum query_opcode opcode,
		      enum attr_idn idn, u8 index, u8 selector, u32 *attr_val)
{
	struct ufs_query_req *request = NULL;
	struct ufs_query_res *response = NULL;
	int err;

	BUG_ON(!hba);

	if (!attr_val) {
		dev_err(hba->dev, "%s: attribute value required for opcode 0x%x\n",
				__func__, opcode);
		return -EINVAL;
	}

	ufshcd_hold(hba, false);

	mutex_lock(&hba->dev_cmd.lock);
	ufshcd_init_query(hba, &request, &response, opcode, idn, index,
			selector);

	switch (opcode) {
	case UPIU_QUERY_OPCODE_WRITE_ATTR:
		request->query_func = UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST;
		request->upiu_req.value = cpu_to_be32(*attr_val);
		break;
	case UPIU_QUERY_OPCODE_READ_ATTR:
		request->query_func = UPIU_QUERY_FUNC_STANDARD_READ_REQUEST;
		break;
	default:
		dev_err(hba->dev, "%s: Expected query attr opcode but got = 0x%.2x\n",
				__func__, opcode);
		err = -EINVAL;
		goto out_unlock;
	}

	err = ufshcd_exec_dev_cmd(hba, DEV_CMD_TYPE_QUERY, QUERY_REQ_TIMEOUT);

	if (err) {
		dev_err(hba->dev, "%s: opcode 0x%.2x for idn %d failed, index %d, err = %d\n",
				__func__, opcode, idn, index, err);
		goto out_unlock;
	}

	*attr_val = be32_to_cpu(response->upiu_res.value);

out_unlock:
	mutex_unlock(&hba->dev_cmd.lock);
	ufshcd_release(hba);
	return err;
}

/**
 * ufshcd_query_attr_retry() - API function for sending query
 * attribute with retries
 * @hba: per-adapter instance
 * @opcode: attribute opcode
 * @idn: attribute idn to access
 * @index: index field
 * @selector: selector field
 * @attr_val: the attribute value after the query request
 * completes
 *
 * Returns 0 for success, non-zero in case of failure
*/
int ufshcd_query_attr_retry(struct ufs_hba *hba,
	enum query_opcode opcode, enum attr_idn idn, u8 index, u8 selector,
	u32 *attr_val)
{
	int ret = 0;
	u32 retries;

	for (retries = QUERY_REQ_RETRIES; retries > 0; retries--) {
		ret = ufshcd_query_attr(hba, opcode, idn, index,
						selector, attr_val);
		if (ret)
			dev_dbg(hba->dev, "%s: failed with error %d, retries %d\n",
				__func__, ret, retries);
		else
			break;
	}

	if (ret)
		dev_err(hba->dev,
			"%s: query attribute, idn %d, failed with error %d after %d retires\n",
			__func__, idn, ret, QUERY_REQ_RETRIES);
	return ret;
}

static int __ufshcd_query_descriptor(struct ufs_hba *hba,
			enum query_opcode opcode, enum desc_idn idn, u8 index,
			u8 selector, u8 *desc_buf, int *buf_len)
{
	struct ufs_query_req *request = NULL;
	struct ufs_query_res *response = NULL;
	int err;

	BUG_ON(!hba);

	if (!desc_buf) {
		dev_err(hba->dev, "%s: descriptor buffer required for opcode 0x%x\n",
				__func__, opcode);
		return -EINVAL;
	}

	if (*buf_len < QUERY_DESC_MIN_SIZE || *buf_len > QUERY_DESC_MAX_SIZE) {
		dev_err(hba->dev, "%s: descriptor buffer size (%d) is out of range\n",
				__func__, *buf_len);
		return -EINVAL;
	}

	ufshcd_hold(hba, false);

	mutex_lock(&hba->dev_cmd.lock);
	ufshcd_init_query(hba, &request, &response, opcode, idn, index,
			selector);
	hba->dev_cmd.query.descriptor = desc_buf;
	request->upiu_req.length = cpu_to_be16(*buf_len);

	switch (opcode) {
	case UPIU_QUERY_OPCODE_WRITE_DESC:
		request->query_func = UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST;
		break;
	case UPIU_QUERY_OPCODE_READ_DESC:
		request->query_func = UPIU_QUERY_FUNC_STANDARD_READ_REQUEST;
		break;
	default:
		dev_err(hba->dev,
				"%s: Expected query descriptor opcode but got = 0x%.2x\n",
				__func__, opcode);
		err = -EINVAL;
		goto out_unlock;
	}

	err = ufshcd_exec_dev_cmd(hba, DEV_CMD_TYPE_QUERY, QUERY_REQ_TIMEOUT);

	if (err) {
		dev_err(hba->dev, "%s: opcode 0x%.2x for idn %d failed, index %d, err = %d\n",
				__func__, opcode, idn, index, err);
		goto out_unlock;
	}

	*buf_len = be16_to_cpu(response->upiu_res.length);

out_unlock:
	hba->dev_cmd.query.descriptor = NULL;
	mutex_unlock(&hba->dev_cmd.lock);
	ufshcd_release(hba);
	return err;
}

/**
 * ufshcd_query_descriptor_retry - API function for sending descriptor requests
 * @hba: per-adapter instance
 * @opcode: attribute opcode
 * @idn: attribute idn to access
 * @index: index field
 * @selector: selector field
 * @desc_buf: the buffer that contains the descriptor
 * @buf_len: length parameter passed to the device
 *
 * Returns 0 for success, non-zero in case of failure.
 * The buf_len parameter will contain, on return, the length parameter
 * received on the response.
 */
int ufshcd_query_descriptor_retry(struct ufs_hba *hba,
				  enum query_opcode opcode,
				  enum desc_idn idn, u8 index,
				  u8 selector,
				  u8 *desc_buf, int *buf_len)
{
	int err;
	int retries;

	for (retries = QUERY_REQ_RETRIES; retries > 0; retries--) {
		err = __ufshcd_query_descriptor(hba, opcode, idn, index,
						selector, desc_buf, buf_len);
		if (!err || err == -EINVAL)
			break;
	}

	return err;
}

/**
 * ufshcd_map_desc_id_to_length - map descriptor IDN to its length
 * @hba: Pointer to adapter instance
 * @desc_id: descriptor idn value
 * @desc_len: mapped desc length (out)
 */
void ufshcd_map_desc_id_to_length(struct ufs_hba *hba, enum desc_idn desc_id,
				  int *desc_len)
{
	if (desc_id >= QUERY_DESC_IDN_MAX || desc_id == QUERY_DESC_IDN_RFU_0 ||
	    desc_id == QUERY_DESC_IDN_RFU_1)
		*desc_len = 0;
	else
		*desc_len = hba->desc_size[desc_id];
}
EXPORT_SYMBOL(ufshcd_map_desc_id_to_length);

static void ufshcd_update_desc_length(struct ufs_hba *hba,
				      enum desc_idn desc_id, int desc_index,
				      unsigned char desc_len)
{
	if (hba->desc_size[desc_id] == QUERY_DESC_MAX_SIZE &&
	    desc_id != QUERY_DESC_IDN_STRING && desc_index != UFS_RPMB_UNIT)
		/* For UFS 3.1, the normal unit descriptor is 10 bytes larger
		 * than the RPMB unit, however, both descriptors share the same
		 * desc_idn, to cover both unit descriptors with one length, we
		 * choose the normal unit descriptor length by desc_index.
		 */
		hba->desc_size[desc_id] = desc_len;
}

/**
 * ufshcd_read_desc_param - read the specified descriptor parameter
 * @hba: Pointer to adapter instance
 * @desc_id: descriptor idn value
 * @desc_index: descriptor index
 * @param_offset: offset of the parameter to read
 * @param_read_buf: pointer to buffer where parameter would be read
 * @param_size: sizeof(param_read_buf)
 *
 * Return 0 in case of success, non-zero otherwise
 */
int ufshcd_read_desc_param(struct ufs_hba *hba,
			   enum desc_idn desc_id,
			   int desc_index,
			   u8 param_offset,
			   u8 *param_read_buf,
			   u8 param_size)
{
	int ret;
	u8 *desc_buf;
	int buff_len;
	bool is_kmalloc = true;

	/* Safety check */
	if (desc_id >= QUERY_DESC_IDN_MAX || !param_size)
		return -EINVAL;

	/* Get the length of descriptor */
	ufshcd_map_desc_id_to_length(hba, desc_id, &buff_len);
	if (!buff_len) {
		dev_err(hba->dev, "%s: Failed to get desc length\n", __func__);
		return -EINVAL;
	}

	if (param_offset >= buff_len) {
		dev_err(hba->dev, "%s: Invalid offset 0x%x in descriptor IDN 0x%x, length 0x%x\n",
			__func__, param_offset, desc_id, buff_len);
		return -EINVAL;
	}

	/* Check whether we need temp memory */
	if (param_offset != 0 || param_size < buff_len) {
		desc_buf = kzalloc(buff_len, GFP_KERNEL);
		if (!desc_buf)
			return -ENOMEM;
	} else {
		desc_buf = param_read_buf;
		is_kmalloc = false;
	}

	/* Request for full descriptor */
	ret = ufshcd_query_descriptor_retry(hba, UPIU_QUERY_OPCODE_READ_DESC,
					desc_id, desc_index, 0,
					desc_buf, &buff_len);

	if (ret) {
		dev_err(hba->dev, "%s: Failed reading descriptor. desc_id %d, desc_index %d, param_offset %d, ret %d\n",
			__func__, desc_id, desc_index, param_offset, ret);
		goto out;
	}

	/* Sanity check */
	if (desc_buf[QUERY_DESC_DESC_TYPE_OFFSET] != desc_id) {
		dev_err(hba->dev, "%s: invalid desc_id %d in descriptor header\n",
			__func__, desc_buf[QUERY_DESC_DESC_TYPE_OFFSET]);
		ret = -EINVAL;
		goto out;
	}

	/* Update descriptor length */
	buff_len = desc_buf[QUERY_DESC_LENGTH_OFFSET];
	ufshcd_update_desc_length(hba, desc_id, desc_index, buff_len);

	if (is_kmalloc) {
		/* Make sure we don't copy more data than available */
		if (param_offset >= buff_len)
			ret = -EINVAL;
		else
			memcpy(param_read_buf, &desc_buf[param_offset],
			       min_t(u32, param_size, buff_len - param_offset));
	}
out:
	if (is_kmalloc)
		kfree(desc_buf);
	return ret;
}

/**
 * struct uc_string_id - unicode string
 *
 * @len: size of this descriptor inclusive
 * @type: descriptor type
 * @uc: unicode string character
 */
struct uc_string_id {
	u8 len;
	u8 type;
	wchar_t uc[];
} __packed;

/* replace non-printable or non-ASCII characters with spaces */
static inline char ufshcd_remove_non_printable(u8 ch)
{
	return (ch >= 0x20 && ch <= 0x7e) ? ch : ' ';
}

/**
 * ufshcd_read_string_desc - read string descriptor
 * @hba: pointer to adapter instance
 * @desc_index: descriptor index
 * @buf: pointer to buffer where descriptor would be read,
 *       the caller should free the memory.
 * @ascii: if true convert from unicode to ascii characters
 *         null terminated string.
 *
 * Return:
 * *      string size on success.
 * *      -ENOMEM: on allocation failure
 * *      -EINVAL: on a wrong parameter
 */
int ufshcd_read_string_desc(struct ufs_hba *hba, u8 desc_index,
			    u8 **buf, bool ascii)
{
	struct uc_string_id *uc_str;
	u8 *str;
	int ret;

	if (!buf)
		return -EINVAL;

	uc_str = kzalloc(QUERY_DESC_MAX_SIZE, GFP_KERNEL);
	if (!uc_str)
		return -ENOMEM;

	ret = ufshcd_read_desc_param(hba, QUERY_DESC_IDN_STRING, desc_index, 0,
				     (u8 *)uc_str, QUERY_DESC_MAX_SIZE);
	if (ret < 0) {
		dev_err(hba->dev, "Reading String Desc failed after %d retries. err = %d\n",
			QUERY_REQ_RETRIES, ret);
		str = NULL;
		goto out;
	}

	if (uc_str->len <= QUERY_DESC_HDR_SIZE) {
		dev_dbg(hba->dev, "String Desc is of zero length\n");
		str = NULL;
		ret = 0;
		goto out;
	}

	if (ascii) {
		ssize_t ascii_len;
		int i;
		/* remove header and divide by 2 to move from UTF16 to UTF8 */
		ascii_len = (uc_str->len - QUERY_DESC_HDR_SIZE) / 2 + 1;
		str = kzalloc(ascii_len, GFP_KERNEL);
		if (!str) {
			ret = -ENOMEM;
			goto out;
		}

		/*
		 * the descriptor contains string in UTF16 format
		 * we need to convert to utf-8 so it can be displayed
		 */
		ret = utf16s_to_utf8s(uc_str->uc,
				      uc_str->len - QUERY_DESC_HDR_SIZE,
				      UTF16_BIG_ENDIAN, str, ascii_len);

		/* replace non-printable or non-ASCII characters with spaces */
		for (i = 0; i < ret; i++)
			str[i] = ufshcd_remove_non_printable(str[i]);

		str[ret++] = '\0';

	} else {
		str = kmemdup(uc_str, uc_str->len, GFP_KERNEL);
		if (!str) {
			ret = -ENOMEM;
			goto out;
		}
		ret = uc_str->len;
	}
out:
	*buf = str;
	kfree(uc_str);
	return ret;
}

/**
 * ufshcd_read_unit_desc_param - read the specified unit descriptor parameter
 * @hba: Pointer to adapter instance
 * @lun: lun id
 * @param_offset: offset of the parameter to read
 * @param_read_buf: pointer to buffer where parameter would be read
 * @param_size: sizeof(param_read_buf)
 *
 * Return 0 in case of success, non-zero otherwise
 */
static inline int ufshcd_read_unit_desc_param(struct ufs_hba *hba,
					      int lun,
					      enum unit_desc_param param_offset,
					      u8 *param_read_buf,
					      u32 param_size)
{
	/*
	 * Unit descriptors are only available for general purpose LUs (LUN id
	 * from 0 to 7) and RPMB Well known LU.
	 */
	if (!ufs_is_valid_unit_desc_lun(&hba->dev_info, lun, param_offset))
		return -EOPNOTSUPP;

	return ufshcd_read_desc_param(hba, QUERY_DESC_IDN_UNIT, lun,
				      param_offset, param_read_buf, param_size);
}

static int ufshcd_get_ref_clk_gating_wait(struct ufs_hba *hba)
{
	int err = 0;
	u32 gating_wait = UFSHCD_REF_CLK_GATING_WAIT_US;

	if (hba->dev_info.wspecversion >= 0x300) {
		err = ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_READ_ATTR,
				QUERY_ATTR_IDN_REF_CLK_GATING_WAIT_TIME, 0, 0,
				&gating_wait);
		if (err)
			dev_err(hba->dev, "Failed reading bRefClkGatingWait. err = %d, use default %uus\n",
					 err, gating_wait);

		if (gating_wait == 0) {
			gating_wait = UFSHCD_REF_CLK_GATING_WAIT_US;
			dev_err(hba->dev, "Undefined ref clk gating wait time, use default %uus\n",
					 gating_wait);
		}

		hba->dev_info.clk_gating_wait_us = gating_wait;
	}

	return err;
}

/**
 * ufshcd_memory_alloc - allocate memory for host memory space data structures
 * @hba: per adapter instance
 *
 * 1. Allocate DMA memory for Command Descriptor array
 *	Each command descriptor consist of Command UPIU, Response UPIU and PRDT
 * 2. Allocate DMA memory for UTP Transfer Request Descriptor List (UTRDL).
 * 3. Allocate DMA memory for UTP Task Management Request Descriptor List
 *	(UTMRDL)
 * 4. Allocate memory for local reference block(lrb).
 *
 * Returns 0 for success, non-zero in case of failure
 */
static int ufshcd_memory_alloc(struct ufs_hba *hba)
{
	size_t utmrdl_size, utrdl_size, ucdl_size;

	/* Allocate memory for UTP command descriptors */
	ucdl_size = (sizeof(struct utp_transfer_cmd_desc) * hba->nutrs);
	hba->ucdl_base_addr = dmam_alloc_coherent(hba->dev,
						  ucdl_size,
						  &hba->ucdl_dma_addr,
						  GFP_KERNEL);

	/*
	 * UFSHCI requires UTP command descriptor to be 128 byte aligned.
	 * make sure hba->ucdl_dma_addr is aligned to PAGE_SIZE
	 * if hba->ucdl_dma_addr is aligned to PAGE_SIZE, then it will
	 * be aligned to 128 bytes as well
	 */
	if (!hba->ucdl_base_addr ||
	    WARN_ON(hba->ucdl_dma_addr & (PAGE_SIZE - 1))) {
		dev_err(hba->dev,
			"Command Descriptor Memory allocation failed\n");
		goto out;
	}

	/*
	 * Allocate memory for UTP Transfer descriptors
	 * UFSHCI requires 1024 byte alignment of UTRD
	 */
	utrdl_size = (sizeof(struct utp_transfer_req_desc) * hba->nutrs);
	hba->utrdl_base_addr = dmam_alloc_coherent(hba->dev,
						   utrdl_size,
						   &hba->utrdl_dma_addr,
						   GFP_KERNEL);
	if (!hba->utrdl_base_addr ||
	    WARN_ON(hba->utrdl_dma_addr & (PAGE_SIZE - 1))) {
		dev_err(hba->dev,
			"Transfer Descriptor Memory allocation failed\n");
		goto out;
	}

	/*
	 * Allocate memory for UTP Task Management descriptors
	 * UFSHCI requires 1024 byte alignment of UTMRD
	 */
	utmrdl_size = sizeof(struct utp_task_req_desc) * hba->nutmrs;
	hba->utmrdl_base_addr = dmam_alloc_coherent(hba->dev,
						    utmrdl_size,
						    &hba->utmrdl_dma_addr,
						    GFP_KERNEL);
	if (!hba->utmrdl_base_addr ||
	    WARN_ON(hba->utmrdl_dma_addr & (PAGE_SIZE - 1))) {
		dev_err(hba->dev,
		"Task Management Descriptor Memory allocation failed\n");
		goto out;
	}

	/* Allocate memory for local reference block */
	hba->lrb = devm_kcalloc(hba->dev,
				hba->nutrs, sizeof(struct ufshcd_lrb),
				GFP_KERNEL);
	if (!hba->lrb) {
		dev_err(hba->dev, "LRB Memory allocation failed\n");
		goto out;
	}
	return 0;
out:
	return -ENOMEM;
}

/**
 * ufshcd_host_memory_configure - configure local reference block with
 *				memory offsets
 * @hba: per adapter instance
 *
 * Configure Host memory space
 * 1. Update Corresponding UTRD.UCDBA and UTRD.UCDBAU with UCD DMA
 * address.
 * 2. Update each UTRD with Response UPIU offset, Response UPIU length
 * and PRDT offset.
 * 3. Save the corresponding addresses of UTRD, UCD.CMD, UCD.RSP and UCD.PRDT
 * into local reference block.
 */
static void ufshcd_host_memory_configure(struct ufs_hba *hba)
{
	struct utp_transfer_req_desc *utrdlp;
	dma_addr_t cmd_desc_dma_addr;
	dma_addr_t cmd_desc_element_addr;
	u16 response_offset;
	u16 prdt_offset;
	int cmd_desc_size;
	int i;

	utrdlp = hba->utrdl_base_addr;

	response_offset =
		offsetof(struct utp_transfer_cmd_desc, response_upiu);
	prdt_offset =
		offsetof(struct utp_transfer_cmd_desc, prd_table);

	cmd_desc_size = sizeof(struct utp_transfer_cmd_desc);
	cmd_desc_dma_addr = hba->ucdl_dma_addr;

	for (i = 0; i < hba->nutrs; i++) {
		/* Configure UTRD with command descriptor base address */
		cmd_desc_element_addr =
				(cmd_desc_dma_addr + (cmd_desc_size * i));
		utrdlp[i].command_desc_base_addr_lo =
				cpu_to_le32(lower_32_bits(cmd_desc_element_addr));
		utrdlp[i].command_desc_base_addr_hi =
				cpu_to_le32(upper_32_bits(cmd_desc_element_addr));

		/* Response upiu and prdt offset should be in double words */
		if (hba->quirks & UFSHCD_QUIRK_PRDT_BYTE_GRAN) {
			utrdlp[i].response_upiu_offset =
				cpu_to_le16(response_offset);
			utrdlp[i].prd_table_offset =
				cpu_to_le16(prdt_offset);
			utrdlp[i].response_upiu_length =
				cpu_to_le16(ALIGNED_UPIU_SIZE);
		} else {
			utrdlp[i].response_upiu_offset =
				cpu_to_le16(response_offset >> 2);
			utrdlp[i].prd_table_offset =
				cpu_to_le16(prdt_offset >> 2);
			utrdlp[i].response_upiu_length =
				cpu_to_le16(ALIGNED_UPIU_SIZE >> 2);
		}

		ufshcd_init_lrb(hba, &hba->lrb[i], i);
	}
}

/**
 * ufshcd_dme_link_startup - Notify Unipro to perform link startup
 * @hba: per adapter instance
 *
 * UIC_CMD_DME_LINK_STARTUP command must be issued to Unipro layer,
 * in order to initialize the Unipro link startup procedure.
 * Once the Unipro links are up, the device connected to the controller
 * is detected.
 *
 * Returns 0 on success, non-zero value on failure
 */
static int ufshcd_dme_link_startup(struct ufs_hba *hba)
{
	struct uic_command uic_cmd = {0};
	int ret;

	uic_cmd.command = UIC_CMD_DME_LINK_STARTUP;

	ret = ufshcd_send_uic_cmd(hba, &uic_cmd);
	if (ret)
		dev_dbg(hba->dev,
			"dme-link-startup: error code %d\n", ret);
	return ret;
}
/**
 * ufshcd_dme_reset - UIC command for DME_RESET
 * @hba: per adapter instance
 *
 * DME_RESET command is issued in order to reset UniPro stack.
 * This function now deals with cold reset.
 *
 * Returns 0 on success, non-zero value on failure
 */
static int ufshcd_dme_reset(struct ufs_hba *hba)
{
	struct uic_command uic_cmd = {0};
	int ret;

	uic_cmd.command = UIC_CMD_DME_RESET;

	ret = ufshcd_send_uic_cmd(hba, &uic_cmd);
	if (ret)
		dev_err(hba->dev,
			"dme-reset: error code %d\n", ret);

	return ret;
}

int ufshcd_dme_configure_adapt(struct ufs_hba *hba,
			       int agreed_gear,
			       int adapt_val)
{
	int ret;

	if (agreed_gear != UFS_HS_G4)
		adapt_val = PA_NO_ADAPT;

	ret = ufshcd_dme_set(hba,
			     UIC_ARG_MIB(PA_TXHSADAPTTYPE),
			     adapt_val);
	return ret;
}
EXPORT_SYMBOL_GPL(ufshcd_dme_configure_adapt);

/**
 * ufshcd_dme_enable - UIC command for DME_ENABLE
 * @hba: per adapter instance
 *
 * DME_ENABLE command is issued in order to enable UniPro stack.
 *
 * Returns 0 on success, non-zero value on failure
 */
static int ufshcd_dme_enable(struct ufs_hba *hba)
{
	struct uic_command uic_cmd = {0};
	int ret;

	uic_cmd.command = UIC_CMD_DME_ENABLE;

	ret = ufshcd_send_uic_cmd(hba, &uic_cmd);
	if (ret)
		dev_err(hba->dev,
			"dme-enable: error code %d\n", ret);

	return ret;
}

static inline void ufshcd_add_delay_before_dme_cmd(struct ufs_hba *hba)
{
	#define MIN_DELAY_BEFORE_DME_CMDS_US	1000
	unsigned long min_sleep_time_us;

	if (!(hba->quirks & UFSHCD_QUIRK_DELAY_BEFORE_DME_CMDS))
		return;

	/*
	 * last_dme_cmd_tstamp will be 0 only for 1st call to
	 * this function
	 */
	if (unlikely(!ktime_to_us(hba->last_dme_cmd_tstamp))) {
		min_sleep_time_us = MIN_DELAY_BEFORE_DME_CMDS_US;
	} else {
		unsigned long delta =
			(unsigned long) ktime_to_us(
				ktime_sub(ktime_get(),
				hba->last_dme_cmd_tstamp));

		if (delta < MIN_DELAY_BEFORE_DME_CMDS_US)
			min_sleep_time_us =
				MIN_DELAY_BEFORE_DME_CMDS_US - delta;
		else
			return; /* no more delay required */
	}

	/* allow sleep for extra 50us if needed */
	usleep_range(min_sleep_time_us, min_sleep_time_us + 50);
}

/**
 * ufshcd_dme_set_attr - UIC command for DME_SET, DME_PEER_SET
 * @hba: per adapter instance
 * @attr_sel: uic command argument1
 * @attr_set: attribute set type as uic command argument2
 * @mib_val: setting value as uic command argument3
 * @peer: indicate whether peer or local
 *
 * Returns 0 on success, non-zero value on failure
 */
int ufshcd_dme_set_attr(struct ufs_hba *hba, u32 attr_sel,
			u8 attr_set, u32 mib_val, u8 peer)
{
	struct uic_command uic_cmd = {0};
	static const char *const action[] = {
		"dme-set",
		"dme-peer-set"
	};
	const char *set = action[!!peer];
	int ret;
	int retries = UFS_UIC_COMMAND_RETRIES;

	uic_cmd.command = peer ?
		UIC_CMD_DME_PEER_SET : UIC_CMD_DME_SET;
	uic_cmd.argument1 = attr_sel;
	uic_cmd.argument2 = UIC_ARG_ATTR_TYPE(attr_set);
	uic_cmd.argument3 = mib_val;

	do {
		/* for peer attributes we retry upon failure */
		ret = ufshcd_send_uic_cmd(hba, &uic_cmd);
		if (ret)
			dev_dbg(hba->dev, "%s: attr-id 0x%x val 0x%x error code %d\n",
				set, UIC_GET_ATTR_ID(attr_sel), mib_val, ret);
	} while (ret && peer && --retries);

	if (ret)
		dev_err(hba->dev, "%s: attr-id 0x%x val 0x%x failed %d retries\n",
			set, UIC_GET_ATTR_ID(attr_sel), mib_val,
			UFS_UIC_COMMAND_RETRIES - retries);

	return ret;
}
EXPORT_SYMBOL_GPL(ufshcd_dme_set_attr);

/**
 * ufshcd_dme_get_attr - UIC command for DME_GET, DME_PEER_GET
 * @hba: per adapter instance
 * @attr_sel: uic command argument1
 * @mib_val: the value of the attribute as returned by the UIC command
 * @peer: indicate whether peer or local
 *
 * Returns 0 on success, non-zero value on failure
 */
int ufshcd_dme_get_attr(struct ufs_hba *hba, u32 attr_sel,
			u32 *mib_val, u8 peer)
{
	struct uic_command uic_cmd = {0};
	static const char *const action[] = {
		"dme-get",
		"dme-peer-get"
	};
	const char *get = action[!!peer];
	int ret;
	int retries = UFS_UIC_COMMAND_RETRIES;
	struct ufs_pa_layer_attr orig_pwr_info;
	struct ufs_pa_layer_attr temp_pwr_info;
	bool pwr_mode_change = false;

	if (peer && (hba->quirks & UFSHCD_QUIRK_DME_PEER_ACCESS_AUTO_MODE)) {
		orig_pwr_info = hba->pwr_info;
		temp_pwr_info = orig_pwr_info;

		if (orig_pwr_info.pwr_tx == FAST_MODE ||
		    orig_pwr_info.pwr_rx == FAST_MODE) {
			temp_pwr_info.pwr_tx = FASTAUTO_MODE;
			temp_pwr_info.pwr_rx = FASTAUTO_MODE;
			pwr_mode_change = true;
		} else if (orig_pwr_info.pwr_tx == SLOW_MODE ||
		    orig_pwr_info.pwr_rx == SLOW_MODE) {
			temp_pwr_info.pwr_tx = SLOWAUTO_MODE;
			temp_pwr_info.pwr_rx = SLOWAUTO_MODE;
			pwr_mode_change = true;
		}
		if (pwr_mode_change) {
			ret = ufshcd_change_power_mode(hba, &temp_pwr_info);
			if (ret)
				goto out;
		}
	}

	uic_cmd.command = peer ?
		UIC_CMD_DME_PEER_GET : UIC_CMD_DME_GET;
	uic_cmd.argument1 = attr_sel;

	do {
		/* for peer attributes we retry upon failure */
		ret = ufshcd_send_uic_cmd(hba, &uic_cmd);
		if (ret)
			dev_dbg(hba->dev, "%s: attr-id 0x%x error code %d\n",
				get, UIC_GET_ATTR_ID(attr_sel), ret);
	} while (ret && peer && --retries);

	if (ret)
		dev_err(hba->dev, "%s: attr-id 0x%x failed %d retries\n",
			get, UIC_GET_ATTR_ID(attr_sel),
			UFS_UIC_COMMAND_RETRIES - retries);

	if (mib_val && !ret)
		*mib_val = uic_cmd.argument3;

	if (peer && (hba->quirks & UFSHCD_QUIRK_DME_PEER_ACCESS_AUTO_MODE)
	    && pwr_mode_change)
		ufshcd_change_power_mode(hba, &orig_pwr_info);
out:
	return ret;
}
EXPORT_SYMBOL_GPL(ufshcd_dme_get_attr);

/**
 * ufshcd_uic_pwr_ctrl - executes UIC commands (which affects the link power
 * state) and waits for it to take effect.
 *
 * @hba: per adapter instance
 * @cmd: UIC command to execute
 *
 * DME operations like DME_SET(PA_PWRMODE), DME_HIBERNATE_ENTER &
 * DME_HIBERNATE_EXIT commands take some time to take its effect on both host
 * and device UniPro link and hence it's final completion would be indicated by
 * dedicated status bits in Interrupt Status register (UPMS, UHES, UHXS) in
 * addition to normal UIC command completion Status (UCCS). This function only
 * returns after the relevant status bits indicate the completion.
 *
 * Returns 0 on success, non-zero value on failure
 */
static int ufshcd_uic_pwr_ctrl(struct ufs_hba *hba, struct uic_command *cmd)
{
	DECLARE_COMPLETION_ONSTACK(uic_async_done);
	unsigned long flags;
	u8 status;
	int ret;
	bool reenable_intr = false;

	mutex_lock(&hba->uic_cmd_mutex);
	ufshcd_add_delay_before_dme_cmd(hba);

	spin_lock_irqsave(hba->host->host_lock, flags);
	if (ufshcd_is_link_broken(hba)) {
		ret = -ENOLINK;
		goto out_unlock;
	}
	hba->uic_async_done = &uic_async_done;
	if (ufshcd_readl(hba, REG_INTERRUPT_ENABLE) & UIC_COMMAND_COMPL) {
		ufshcd_disable_intr(hba, UIC_COMMAND_COMPL);
		/*
		 * Make sure UIC command completion interrupt is disabled before
		 * issuing UIC command.
		 */
		wmb();
		reenable_intr = true;
	}
	ret = __ufshcd_send_uic_cmd(hba, cmd, false);
	spin_unlock_irqrestore(hba->host->host_lock, flags);
	if (ret) {
		dev_err(hba->dev,
			"pwr ctrl cmd 0x%x with mode 0x%x uic error %d\n",
			cmd->command, cmd->argument3, ret);
		goto out;
	}

	if (!wait_for_completion_timeout(hba->uic_async_done,
					 msecs_to_jiffies(UIC_CMD_TIMEOUT))) {
		dev_err(hba->dev,
			"pwr ctrl cmd 0x%x with mode 0x%x completion timeout\n",
			cmd->command, cmd->argument3);

		if (!cmd->cmd_active) {
			dev_err(hba->dev, "%s: Power Mode Change operation has been completed, go check UPMCRS\n",
				__func__);
			goto check_upmcrs;
		}

		ret = -ETIMEDOUT;
		goto out;
	}

check_upmcrs:
	status = ufshcd_get_upmcrs(hba);
	if (status != PWR_LOCAL) {
		dev_err(hba->dev,
			"pwr ctrl cmd 0x%x failed, host upmcrs:0x%x\n",
			cmd->command, status);
		ret = (status != PWR_OK) ? status : -1;
	}
out:
	if (ret) {
		ufshcd_print_host_state(hba);
		ufshcd_print_pwr_info(hba);
		ufshcd_print_evt_hist(hba);
	}

	spin_lock_irqsave(hba->host->host_lock, flags);
	hba->active_uic_cmd = NULL;
	hba->uic_async_done = NULL;
	if (reenable_intr)
		ufshcd_enable_intr(hba, UIC_COMMAND_COMPL);
	if (ret) {
		ufshcd_set_link_broken(hba);
		ufshcd_schedule_eh_work(hba);
	}
out_unlock:
	spin_unlock_irqrestore(hba->host->host_lock, flags);
	mutex_unlock(&hba->uic_cmd_mutex);

	return ret;
}

/**
 * ufshcd_uic_change_pwr_mode - Perform the UIC power mode chage
 *				using DME_SET primitives.
 * @hba: per adapter instance
 * @mode: powr mode value
 *
 * Returns 0 on success, non-zero value on failure
 */
static int ufshcd_uic_change_pwr_mode(struct ufs_hba *hba, u8 mode)
{
	struct uic_command uic_cmd = {0};
	int ret;

	if (hba->quirks & UFSHCD_QUIRK_BROKEN_PA_RXHSUNTERMCAP) {
		ret = ufshcd_dme_set(hba,
				UIC_ARG_MIB_SEL(PA_RXHSUNTERMCAP, 0), 1);
		if (ret) {
			dev_err(hba->dev, "%s: failed to enable PA_RXHSUNTERMCAP ret %d\n",
						__func__, ret);
			goto out;
		}
	}

	uic_cmd.command = UIC_CMD_DME_SET;
	uic_cmd.argument1 = UIC_ARG_MIB(PA_PWRMODE);
	uic_cmd.argument3 = mode;
	ufshcd_hold(hba, false);
	ret = ufshcd_uic_pwr_ctrl(hba, &uic_cmd);
	ufshcd_release(hba);

out:
	return ret;
}

int ufshcd_link_recovery(struct ufs_hba *hba)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(hba->host->host_lock, flags);
	hba->ufshcd_state = UFSHCD_STATE_RESET;
	ufshcd_set_eh_in_progress(hba);
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	/* Reset the attached device */
	ufshcd_device_reset(hba);

	ret = ufshcd_host_reset_and_restore(hba);

	spin_lock_irqsave(hba->host->host_lock, flags);
	if (ret)
		hba->ufshcd_state = UFSHCD_STATE_ERROR;
	ufshcd_clear_eh_in_progress(hba);
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	if (ret)
		dev_err(hba->dev, "%s: link recovery failed, err %d",
			__func__, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(ufshcd_link_recovery);

static int ufshcd_uic_hibern8_enter(struct ufs_hba *hba)
{
	int ret;
	struct uic_command uic_cmd = {0};
	ktime_t start = ktime_get();

	ufshcd_vops_hibern8_notify(hba, UIC_CMD_DME_HIBER_ENTER, PRE_CHANGE);

	uic_cmd.command = UIC_CMD_DME_HIBER_ENTER;
	ret = ufshcd_uic_pwr_ctrl(hba, &uic_cmd);
	trace_ufshcd_profile_hibern8(dev_name(hba->dev), "enter",
			     ktime_to_us(ktime_sub(ktime_get(), start)), ret);

	if (ret)
		dev_err(hba->dev, "%s: hibern8 enter failed. ret = %d\n",
			__func__, ret);
	else
		ufshcd_vops_hibern8_notify(hba, UIC_CMD_DME_HIBER_ENTER,
								POST_CHANGE);

	return ret;
}

int ufshcd_uic_hibern8_exit(struct ufs_hba *hba)
{
	struct uic_command uic_cmd = {0};
	int ret;
	ktime_t start = ktime_get();

	ufshcd_vops_hibern8_notify(hba, UIC_CMD_DME_HIBER_EXIT, PRE_CHANGE);

	uic_cmd.command = UIC_CMD_DME_HIBER_EXIT;
	ret = ufshcd_uic_pwr_ctrl(hba, &uic_cmd);
	trace_ufshcd_profile_hibern8(dev_name(hba->dev), "exit",
			     ktime_to_us(ktime_sub(ktime_get(), start)), ret);

	if (ret) {
		dev_err(hba->dev, "%s: hibern8 exit failed. ret = %d\n",
			__func__, ret);
	} else {
		ufshcd_vops_hibern8_notify(hba, UIC_CMD_DME_HIBER_EXIT,
								POST_CHANGE);
		hba->ufs_stats.last_hibern8_exit_tstamp = ktime_get();
		hba->ufs_stats.hibern8_exit_cnt++;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(ufshcd_uic_hibern8_exit);

void ufshcd_auto_hibern8_update(struct ufs_hba *hba, u32 ahit)
{
	unsigned long flags;
	bool update = false;

	if (!ufshcd_is_auto_hibern8_supported(hba))
		return;

	spin_lock_irqsave(hba->host->host_lock, flags);
	if (hba->ahit != ahit) {
		hba->ahit = ahit;
		update = true;
	}
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	if (update &&
	    !pm_runtime_suspended(&hba->sdev_ufs_device->sdev_gendev)) {
		ufshcd_rpm_get_sync(hba);
		ufshcd_hold(hba, false);
		ufshcd_auto_hibern8_enable(hba);
		ufshcd_release(hba);
		ufshcd_rpm_put_sync(hba);
	}
}
EXPORT_SYMBOL_GPL(ufshcd_auto_hibern8_update);

void ufshcd_auto_hibern8_enable(struct ufs_hba *hba)
{
	unsigned long flags;

	if (!ufshcd_is_auto_hibern8_supported(hba))
		return;

	spin_lock_irqsave(hba->host->host_lock, flags);
	ufshcd_writel(hba, hba->ahit, REG_AUTO_HIBERNATE_IDLE_TIMER);
	spin_unlock_irqrestore(hba->host->host_lock, flags);
}

 /**
 * ufshcd_init_pwr_info - setting the POR (power on reset)
 * values in hba power info
 * @hba: per-adapter instance
 */
static void ufshcd_init_pwr_info(struct ufs_hba *hba)
{
	hba->pwr_info.gear_rx = UFS_PWM_G1;
	hba->pwr_info.gear_tx = UFS_PWM_G1;
	hba->pwr_info.lane_rx = 1;
	hba->pwr_info.lane_tx = 1;
	hba->pwr_info.pwr_rx = SLOWAUTO_MODE;
	hba->pwr_info.pwr_tx = SLOWAUTO_MODE;
	hba->pwr_info.hs_rate = 0;
}

/**
 * ufshcd_get_max_pwr_mode - reads the max power mode negotiated with device
 * @hba: per-adapter instance
 */
static int ufshcd_get_max_pwr_mode(struct ufs_hba *hba)
{
	struct ufs_pa_layer_attr *pwr_info = &hba->max_pwr_info.info;

	if (hba->max_pwr_info.is_valid)
		return 0;

	pwr_info->pwr_tx = FAST_MODE;
	pwr_info->pwr_rx = FAST_MODE;
	pwr_info->hs_rate = PA_HS_MODE_B;

	/* Get the connected lane count */
	ufshcd_dme_get(hba, UIC_ARG_MIB(PA_CONNECTEDRXDATALANES),
			&pwr_info->lane_rx);
	ufshcd_dme_get(hba, UIC_ARG_MIB(PA_CONNECTEDTXDATALANES),
			&pwr_info->lane_tx);

	if (!pwr_info->lane_rx || !pwr_info->lane_tx) {
		dev_err(hba->dev, "%s: invalid connected lanes value. rx=%d, tx=%d\n",
				__func__,
				pwr_info->lane_rx,
				pwr_info->lane_tx);
		return -EINVAL;
	}

	/*
	 * First, get the maximum gears of HS speed.
	 * If a zero value, it means there is no HSGEAR capability.
	 * Then, get the maximum gears of PWM speed.
	 */
	ufshcd_dme_get(hba, UIC_ARG_MIB(PA_MAXRXHSGEAR), &pwr_info->gear_rx);
	if (!pwr_info->gear_rx) {
		ufshcd_dme_get(hba, UIC_ARG_MIB(PA_MAXRXPWMGEAR),
				&pwr_info->gear_rx);
		if (!pwr_info->gear_rx) {
			dev_err(hba->dev, "%s: invalid max pwm rx gear read = %d\n",
				__func__, pwr_info->gear_rx);
			return -EINVAL;
		}
		pwr_info->pwr_rx = SLOW_MODE;
	}

	ufshcd_dme_peer_get(hba, UIC_ARG_MIB(PA_MAXRXHSGEAR),
			&pwr_info->gear_tx);
	if (!pwr_info->gear_tx) {
		ufshcd_dme_peer_get(hba, UIC_ARG_MIB(PA_MAXRXPWMGEAR),
				&pwr_info->gear_tx);
		if (!pwr_info->gear_tx) {
			dev_err(hba->dev, "%s: invalid max pwm tx gear read = %d\n",
				__func__, pwr_info->gear_tx);
			return -EINVAL;
		}
		pwr_info->pwr_tx = SLOW_MODE;
	}

	hba->max_pwr_info.is_valid = true;
	return 0;
}

static int ufshcd_change_power_mode(struct ufs_hba *hba,
			     struct ufs_pa_layer_attr *pwr_mode)
{
	int ret;

	/* if already configured to the requested pwr_mode */
	if (!hba->force_pmc &&
	    pwr_mode->gear_rx == hba->pwr_info.gear_rx &&
	    pwr_mode->gear_tx == hba->pwr_info.gear_tx &&
	    pwr_mode->lane_rx == hba->pwr_info.lane_rx &&
	    pwr_mode->lane_tx == hba->pwr_info.lane_tx &&
	    pwr_mode->pwr_rx == hba->pwr_info.pwr_rx &&
	    pwr_mode->pwr_tx == hba->pwr_info.pwr_tx &&
	    pwr_mode->hs_rate == hba->pwr_info.hs_rate) {
		dev_dbg(hba->dev, "%s: power already configured\n", __func__);
		return 0;
	}

	/*
	 * Configure attributes for power mode change with below.
	 * - PA_RXGEAR, PA_ACTIVERXDATALANES, PA_RXTERMINATION,
	 * - PA_TXGEAR, PA_ACTIVETXDATALANES, PA_TXTERMINATION,
	 * - PA_HSSERIES
	 */
	ufshcd_dme_set(hba, UIC_ARG_MIB(PA_RXGEAR), pwr_mode->gear_rx);
	ufshcd_dme_set(hba, UIC_ARG_MIB(PA_ACTIVERXDATALANES),
			pwr_mode->lane_rx);
	if (pwr_mode->pwr_rx == FASTAUTO_MODE ||
			pwr_mode->pwr_rx == FAST_MODE)
		ufshcd_dme_set(hba, UIC_ARG_MIB(PA_RXTERMINATION), TRUE);
	else
		ufshcd_dme_set(hba, UIC_ARG_MIB(PA_RXTERMINATION), FALSE);

	ufshcd_dme_set(hba, UIC_ARG_MIB(PA_TXGEAR), pwr_mode->gear_tx);
	ufshcd_dme_set(hba, UIC_ARG_MIB(PA_ACTIVETXDATALANES),
			pwr_mode->lane_tx);
	if (pwr_mode->pwr_tx == FASTAUTO_MODE ||
			pwr_mode->pwr_tx == FAST_MODE)
		ufshcd_dme_set(hba, UIC_ARG_MIB(PA_TXTERMINATION), TRUE);
	else
		ufshcd_dme_set(hba, UIC_ARG_MIB(PA_TXTERMINATION), FALSE);

	if (pwr_mode->pwr_rx == FASTAUTO_MODE ||
	    pwr_mode->pwr_tx == FASTAUTO_MODE ||
	    pwr_mode->pwr_rx == FAST_MODE ||
	    pwr_mode->pwr_tx == FAST_MODE)
		ufshcd_dme_set(hba, UIC_ARG_MIB(PA_HSSERIES),
						pwr_mode->hs_rate);

	if (!(hba->quirks & UFSHCD_QUIRK_SKIP_DEF_UNIPRO_TIMEOUT_SETTING)) {
		ufshcd_dme_set(hba, UIC_ARG_MIB(PA_PWRMODEUSERDATA0),
				DL_FC0ProtectionTimeOutVal_Default);
		ufshcd_dme_set(hba, UIC_ARG_MIB(PA_PWRMODEUSERDATA1),
				DL_TC0ReplayTimeOutVal_Default);
		ufshcd_dme_set(hba, UIC_ARG_MIB(PA_PWRMODEUSERDATA2),
				DL_AFC0ReqTimeOutVal_Default);
		ufshcd_dme_set(hba, UIC_ARG_MIB(PA_PWRMODEUSERDATA3),
				DL_FC1ProtectionTimeOutVal_Default);
		ufshcd_dme_set(hba, UIC_ARG_MIB(PA_PWRMODEUSERDATA4),
				DL_TC1ReplayTimeOutVal_Default);
		ufshcd_dme_set(hba, UIC_ARG_MIB(PA_PWRMODEUSERDATA5),
				DL_AFC1ReqTimeOutVal_Default);

		ufshcd_dme_set(hba, UIC_ARG_MIB(DME_LocalFC0ProtectionTimeOutVal),
				DL_FC0ProtectionTimeOutVal_Default);
		ufshcd_dme_set(hba, UIC_ARG_MIB(DME_LocalTC0ReplayTimeOutVal),
				DL_TC0ReplayTimeOutVal_Default);
		ufshcd_dme_set(hba, UIC_ARG_MIB(DME_LocalAFC0ReqTimeOutVal),
				DL_AFC0ReqTimeOutVal_Default);
	}

	ret = ufshcd_uic_change_pwr_mode(hba, pwr_mode->pwr_rx << 4
			| pwr_mode->pwr_tx);

	if (ret) {
		dev_err(hba->dev,
			"%s: power mode change failed %d\n", __func__, ret);
	} else {
		ufshcd_vops_pwr_change_notify(hba, POST_CHANGE, NULL,
								pwr_mode);

		memcpy(&hba->pwr_info, pwr_mode,
			sizeof(struct ufs_pa_layer_attr));
	}

	return ret;
}

/**
 * ufshcd_config_pwr_mode - configure a new power mode
 * @hba: per-adapter instance
 * @desired_pwr_mode: desired power configuration
 */
int ufshcd_config_pwr_mode(struct ufs_hba *hba,
		struct ufs_pa_layer_attr *desired_pwr_mode)
{
	struct ufs_pa_layer_attr final_params = { 0 };
	int ret;

	ret = ufshcd_vops_pwr_change_notify(hba, PRE_CHANGE,
					desired_pwr_mode, &final_params);

	if (ret)
		memcpy(&final_params, desired_pwr_mode, sizeof(final_params));

	ret = ufshcd_change_power_mode(hba, &final_params);

	return ret;
}
EXPORT_SYMBOL_GPL(ufshcd_config_pwr_mode);

/**
 * ufshcd_complete_dev_init() - checks device readiness
 * @hba: per-adapter instance
 *
 * Set fDeviceInit flag and poll until device toggles it.
 */
static int ufshcd_complete_dev_init(struct ufs_hba *hba)
{
	int err;
	bool flag_res = true;
	ktime_t timeout;

	err = ufshcd_query_flag_retry(hba, UPIU_QUERY_OPCODE_SET_FLAG,
		QUERY_FLAG_IDN_FDEVICEINIT, 0, NULL);
	if (err) {
		dev_err(hba->dev,
			"%s setting fDeviceInit flag failed with error %d\n",
			__func__, err);
		goto out;
	}

	/* Poll fDeviceInit flag to be cleared */
	timeout = ktime_add_ms(ktime_get(), FDEVICEINIT_COMPL_TIMEOUT);
	do {
		err = ufshcd_query_flag(hba, UPIU_QUERY_OPCODE_READ_FLAG,
					QUERY_FLAG_IDN_FDEVICEINIT, 0, &flag_res);
		if (!flag_res)
			break;
		usleep_range(5000, 10000);
	} while (ktime_before(ktime_get(), timeout));

	if (err) {
		dev_err(hba->dev,
				"%s reading fDeviceInit flag failed with error %d\n",
				__func__, err);
	} else if (flag_res) {
		dev_err(hba->dev,
				"%s fDeviceInit was not cleared by the device\n",
				__func__);
		err = -EBUSY;
	}
out:
	return err;
}

/**
 * ufshcd_make_hba_operational - Make UFS controller operational
 * @hba: per adapter instance
 *
 * To bring UFS host controller to operational state,
 * 1. Enable required interrupts
 * 2. Configure interrupt aggregation
 * 3. Program UTRL and UTMRL base address
 * 4. Configure run-stop-registers
 *
 * Returns 0 on success, non-zero value on failure
 */
int ufshcd_make_hba_operational(struct ufs_hba *hba)
{
	int err = 0;
	u32 reg;

	/* Enable required interrupts */
	ufshcd_enable_intr(hba, UFSHCD_ENABLE_INTRS);

	/* Configure interrupt aggregation */
	if (ufshcd_is_intr_aggr_allowed(hba))
		ufshcd_config_intr_aggr(hba, hba->nutrs - 1, INT_AGGR_DEF_TO);
	else
		ufshcd_disable_intr_aggr(hba);

	/* Configure UTRL and UTMRL base address registers */
	ufshcd_writel(hba, lower_32_bits(hba->utrdl_dma_addr),
			REG_UTP_TRANSFER_REQ_LIST_BASE_L);
	ufshcd_writel(hba, upper_32_bits(hba->utrdl_dma_addr),
			REG_UTP_TRANSFER_REQ_LIST_BASE_H);
	ufshcd_writel(hba, lower_32_bits(hba->utmrdl_dma_addr),
			REG_UTP_TASK_REQ_LIST_BASE_L);
	ufshcd_writel(hba, upper_32_bits(hba->utmrdl_dma_addr),
			REG_UTP_TASK_REQ_LIST_BASE_H);

	/*
	 * Make sure base address and interrupt setup are updated before
	 * enabling the run/stop registers below.
	 */
	wmb();

	/*
	 * UCRDY, UTMRLDY and UTRLRDY bits must be 1
	 */
	reg = ufshcd_readl(hba, REG_CONTROLLER_STATUS);
	if (!(ufshcd_get_lists_status(reg))) {
		ufshcd_enable_run_stop_reg(hba);
	} else {
		dev_err(hba->dev,
			"Host controller not ready to process requests");
		err = -EIO;
	}

	return err;
}
EXPORT_SYMBOL_GPL(ufshcd_make_hba_operational);

/**
 * ufshcd_hba_stop - Send controller to reset state
 * @hba: per adapter instance
 */
void ufshcd_hba_stop(struct ufs_hba *hba)
{
	unsigned long flags;
	int err;

	/*
	 * Obtain the host lock to prevent that the controller is disabled
	 * while the UFS interrupt handler is active on another CPU.
	 */
	spin_lock_irqsave(hba->host->host_lock, flags);
	ufshcd_writel(hba, CONTROLLER_DISABLE,  REG_CONTROLLER_ENABLE);
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	err = ufshcd_wait_for_register(hba, REG_CONTROLLER_ENABLE,
					CONTROLLER_ENABLE, CONTROLLER_DISABLE,
					10, 1);
	if (err)
		dev_err(hba->dev, "%s: Controller disable failed\n", __func__);
}
EXPORT_SYMBOL_GPL(ufshcd_hba_stop);

/**
 * ufshcd_hba_execute_hce - initialize the controller
 * @hba: per adapter instance
 *
 * The controller resets itself and controller firmware initialization
 * sequence kicks off. When controller is ready it will set
 * the Host Controller Enable bit to 1.
 *
 * Returns 0 on success, non-zero value on failure
 */
static int ufshcd_hba_execute_hce(struct ufs_hba *hba)
{
	int retry_outer = 3;
	int retry_inner;

start:
	if (!ufshcd_is_hba_active(hba))
		/* change controller state to "reset state" */
		ufshcd_hba_stop(hba);

	/* UniPro link is disabled at this point */
	ufshcd_set_link_off(hba);

	ufshcd_vops_hce_enable_notify(hba, PRE_CHANGE);

	/* start controller initialization sequence */
	ufshcd_hba_start(hba);

	/*
	 * To initialize a UFS host controller HCE bit must be set to 1.
	 * During initialization the HCE bit value changes from 1->0->1.
	 * When the host controller completes initialization sequence
	 * it sets the value of HCE bit to 1. The same HCE bit is read back
	 * to check if the controller has completed initialization sequence.
	 * So without this delay the value HCE = 1, set in the previous
	 * instruction might be read back.
	 * This delay can be changed based on the controller.
	 */
	ufshcd_delay_us(hba->vps->hba_enable_delay_us, 100);

	/* wait for the host controller to complete initialization */
	retry_inner = 50;
	while (ufshcd_is_hba_active(hba)) {
		if (retry_inner) {
			retry_inner--;
		} else {
			dev_err(hba->dev,
				"Controller enable failed\n");
			if (retry_outer) {
				retry_outer--;
				goto start;
			}
			return -EIO;
		}
		usleep_range(1000, 1100);
	}

	/* enable UIC related interrupts */
	ufshcd_enable_intr(hba, UFSHCD_UIC_MASK);

	ufshcd_vops_hce_enable_notify(hba, POST_CHANGE);

	return 0;
}

int ufshcd_hba_enable(struct ufs_hba *hba)
{
	int ret;

	if (hba->quirks & UFSHCI_QUIRK_BROKEN_HCE) {
		ufshcd_set_link_off(hba);
		ufshcd_vops_hce_enable_notify(hba, PRE_CHANGE);

		/* enable UIC related interrupts */
		ufshcd_enable_intr(hba, UFSHCD_UIC_MASK);
		ret = ufshcd_dme_reset(hba);
		if (!ret) {
			ret = ufshcd_dme_enable(hba);
			if (!ret)
				ufshcd_vops_hce_enable_notify(hba, POST_CHANGE);
			if (ret)
				dev_err(hba->dev,
					"Host controller enable failed with non-hce\n");
		}
	} else {
		ret = ufshcd_hba_execute_hce(hba);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(ufshcd_hba_enable);

static int ufshcd_disable_tx_lcc(struct ufs_hba *hba, bool peer)
{
	int tx_lanes = 0, i, err = 0;

	if (!peer)
		ufshcd_dme_get(hba, UIC_ARG_MIB(PA_CONNECTEDTXDATALANES),
			       &tx_lanes);
	else
		ufshcd_dme_peer_get(hba, UIC_ARG_MIB(PA_CONNECTEDTXDATALANES),
				    &tx_lanes);
	for (i = 0; i < tx_lanes; i++) {
		if (!peer)
			err = ufshcd_dme_set(hba,
				UIC_ARG_MIB_SEL(TX_LCC_ENABLE,
					UIC_ARG_MPHY_TX_GEN_SEL_INDEX(i)),
					0);
		else
			err = ufshcd_dme_peer_set(hba,
				UIC_ARG_MIB_SEL(TX_LCC_ENABLE,
					UIC_ARG_MPHY_TX_GEN_SEL_INDEX(i)),
					0);
		if (err) {
			dev_err(hba->dev, "%s: TX LCC Disable failed, peer = %d, lane = %d, err = %d",
				__func__, peer, i, err);
			break;
		}
	}

	return err;
}

static inline int ufshcd_disable_device_tx_lcc(struct ufs_hba *hba)
{
	return ufshcd_disable_tx_lcc(hba, true);
}

void ufshcd_update_evt_hist(struct ufs_hba *hba, u32 id, u32 val)
{
	struct ufs_event_hist *e;

	if (id >= UFS_EVT_CNT)
		return;

	e = &hba->ufs_stats.event[id];
	e->val[e->pos] = val;
	e->tstamp[e->pos] = ktime_get();
	e->cnt += 1;
	e->pos = (e->pos + 1) % UFS_EVENT_HIST_LENGTH;

	ufshcd_vops_event_notify(hba, id, &val);
}
EXPORT_SYMBOL_GPL(ufshcd_update_evt_hist);

/**
 * ufshcd_link_startup - Initialize unipro link startup
 * @hba: per adapter instance
 *
 * Returns 0 for success, non-zero in case of failure
 */
static int ufshcd_link_startup(struct ufs_hba *hba)
{
	int ret;
	int retries = DME_LINKSTARTUP_RETRIES;
	bool link_startup_again = false;

	/*
	 * If UFS device isn't active then we will have to issue link startup
	 * 2 times to make sure the device state move to active.
	 */
	if (!ufshcd_is_ufs_dev_active(hba))
		link_startup_again = true;

link_startup:
	do {
		ufshcd_vops_link_startup_notify(hba, PRE_CHANGE);

		ret = ufshcd_dme_link_startup(hba);

		/* check if device is detected by inter-connect layer */
		if (!ret && !ufshcd_is_device_present(hba)) {
			ufshcd_update_evt_hist(hba,
					       UFS_EVT_LINK_STARTUP_FAIL,
					       0);
			dev_err(hba->dev, "%s: Device not present\n", __func__);
			ret = -ENXIO;
			goto out;
		}

		/*
		 * DME link lost indication is only received when link is up,
		 * but we can't be sure if the link is up until link startup
		 * succeeds. So reset the local Uni-Pro and try again.
		 */
		if (ret && ufshcd_hba_enable(hba)) {
			ufshcd_update_evt_hist(hba,
					       UFS_EVT_LINK_STARTUP_FAIL,
					       (u32)ret);
			goto out;
		}
	} while (ret && retries--);

	if (ret) {
		/* failed to get the link up... retire */
		ufshcd_update_evt_hist(hba,
				       UFS_EVT_LINK_STARTUP_FAIL,
				       (u32)ret);
		goto out;
	}

	if (link_startup_again) {
		link_startup_again = false;
		retries = DME_LINKSTARTUP_RETRIES;
		goto link_startup;
	}

	/* Mark that link is up in PWM-G1, 1-lane, SLOW-AUTO mode */
	ufshcd_init_pwr_info(hba);
	ufshcd_print_pwr_info(hba);

	if (hba->quirks & UFSHCD_QUIRK_BROKEN_LCC) {
		ret = ufshcd_disable_device_tx_lcc(hba);
		if (ret)
			goto out;
	}

	/* Include any host controller configuration via UIC commands */
	ret = ufshcd_vops_link_startup_notify(hba, POST_CHANGE);
	if (ret)
		goto out;

	/* Clear UECPA once due to LINERESET has happened during LINK_STARTUP */
	ufshcd_readl(hba, REG_UIC_ERROR_CODE_PHY_ADAPTER_LAYER);
	ret = ufshcd_make_hba_operational(hba);
out:
	if (ret) {
		dev_err(hba->dev, "link startup failed %d\n", ret);
		ufshcd_print_host_state(hba);
		ufshcd_print_pwr_info(hba);
		ufshcd_print_evt_hist(hba);
	}
	return ret;
}

/**
 * ufshcd_verify_dev_init() - Verify device initialization
 * @hba: per-adapter instance
 *
 * Send NOP OUT UPIU and wait for NOP IN response to check whether the
 * device Transport Protocol (UTP) layer is ready after a reset.
 * If the UTP layer at the device side is not initialized, it may
 * not respond with NOP IN UPIU within timeout of %NOP_OUT_TIMEOUT
 * and we retry sending NOP OUT for %NOP_OUT_RETRIES iterations.
 */
static int ufshcd_verify_dev_init(struct ufs_hba *hba)
{
	int err = 0;
	int retries;

	ufshcd_hold(hba, false);
	mutex_lock(&hba->dev_cmd.lock);
	for (retries = NOP_OUT_RETRIES; retries > 0; retries--) {
		err = ufshcd_exec_dev_cmd(hba, DEV_CMD_TYPE_NOP,
					  hba->nop_out_timeout);

		if (!err || err == -ETIMEDOUT)
			break;

		dev_dbg(hba->dev, "%s: error %d retrying\n", __func__, err);
	}
	mutex_unlock(&hba->dev_cmd.lock);
	ufshcd_release(hba);

	if (err)
		dev_err(hba->dev, "%s: NOP OUT failed %d\n", __func__, err);
	return err;
}

/**
 * ufshcd_set_queue_depth - set lun queue depth
 * @sdev: pointer to SCSI device
 *
 * Read bLUQueueDepth value and activate scsi tagged command
 * queueing. For WLUN, queue depth is set to 1. For best-effort
 * cases (bLUQueueDepth = 0) the queue depth is set to a maximum
 * value that host can queue.
 */
static void ufshcd_set_queue_depth(struct scsi_device *sdev)
{
	int ret = 0;
	u8 lun_qdepth;
	struct ufs_hba *hba;

	hba = shost_priv(sdev->host);

	lun_qdepth = hba->nutrs;
	ret = ufshcd_read_unit_desc_param(hba,
					  ufshcd_scsi_to_upiu_lun(sdev->lun),
					  UNIT_DESC_PARAM_LU_Q_DEPTH,
					  &lun_qdepth,
					  sizeof(lun_qdepth));

	/* Some WLUN doesn't support unit descriptor */
	if (ret == -EOPNOTSUPP)
		lun_qdepth = 1;
	else if (!lun_qdepth)
		/* eventually, we can figure out the real queue depth */
		lun_qdepth = hba->nutrs;
	else
		lun_qdepth = min_t(int, lun_qdepth, hba->nutrs);

	dev_dbg(hba->dev, "%s: activate tcq with queue depth %d\n",
			__func__, lun_qdepth);
	scsi_change_queue_depth(sdev, lun_qdepth);
}

/*
 * ufshcd_get_lu_wp - returns the "b_lu_write_protect" from UNIT DESCRIPTOR
 * @hba: per-adapter instance
 * @lun: UFS device lun id
 * @b_lu_write_protect: pointer to buffer to hold the LU's write protect info
 *
 * Returns 0 in case of success and b_lu_write_protect status would be returned
 * @b_lu_write_protect parameter.
 * Returns -ENOTSUPP if reading b_lu_write_protect is not supported.
 * Returns -EINVAL in case of invalid parameters passed to this function.
 */
static int ufshcd_get_lu_wp(struct ufs_hba *hba,
			    u8 lun,
			    u8 *b_lu_write_protect)
{
	int ret;

	if (!b_lu_write_protect)
		ret = -EINVAL;
	/*
	 * According to UFS device spec, RPMB LU can't be write
	 * protected so skip reading bLUWriteProtect parameter for
	 * it. For other W-LUs, UNIT DESCRIPTOR is not available.
	 */
	else if (lun >= hba->dev_info.max_lu_supported)
		ret = -ENOTSUPP;
	else
		ret = ufshcd_read_unit_desc_param(hba,
					  lun,
					  UNIT_DESC_PARAM_LU_WR_PROTECT,
					  b_lu_write_protect,
					  sizeof(*b_lu_write_protect));
	return ret;
}

/**
 * ufshcd_get_lu_power_on_wp_status - get LU's power on write protect
 * status
 * @hba: per-adapter instance
 * @sdev: pointer to SCSI device
 *
 */
static inline void ufshcd_get_lu_power_on_wp_status(struct ufs_hba *hba,
						    struct scsi_device *sdev)
{
	if (hba->dev_info.f_power_on_wp_en &&
	    !hba->dev_info.is_lu_power_on_wp) {
		u8 b_lu_write_protect;

		if (!ufshcd_get_lu_wp(hba, ufshcd_scsi_to_upiu_lun(sdev->lun),
				      &b_lu_write_protect) &&
		    (b_lu_write_protect == UFS_LU_POWER_ON_WP))
			hba->dev_info.is_lu_power_on_wp = true;
	}
}

/**
 * ufshcd_setup_links - associate link b/w device wlun and other luns
 * @sdev: pointer to SCSI device
 * @hba: pointer to ufs hba
 */
static void ufshcd_setup_links(struct ufs_hba *hba, struct scsi_device *sdev)
{
	struct device_link *link;

	/*
	 * Device wlun is the supplier & rest of the luns are consumers.
	 * This ensures that device wlun suspends after all other luns.
	 */
	if (hba->sdev_ufs_device) {
		link = device_link_add(&sdev->sdev_gendev,
				       &hba->sdev_ufs_device->sdev_gendev,
				       DL_FLAG_PM_RUNTIME | DL_FLAG_RPM_ACTIVE);
		if (!link) {
			dev_err(&sdev->sdev_gendev, "Failed establishing link - %s\n",
				dev_name(&hba->sdev_ufs_device->sdev_gendev));
			return;
		}
		hba->luns_avail--;
		/* Ignore REPORT_LUN wlun probing */
		if (hba->luns_avail == 1) {
			ufshcd_rpm_put(hba);
			return;
		}
	} else {
		/*
		 * Device wlun is probed. The assumption is that WLUNs are
		 * scanned before other LUNs.
		 */
		hba->luns_avail--;
	}
}

/**
 * ufshcd_slave_alloc - handle initial SCSI device configurations
 * @sdev: pointer to SCSI device
 *
 * Returns success
 */
static int ufshcd_slave_alloc(struct scsi_device *sdev)
{
	struct ufs_hba *hba;

	hba = shost_priv(sdev->host);

	/* Mode sense(6) is not supported by UFS, so use Mode sense(10) */
	sdev->use_10_for_ms = 1;

	/* DBD field should be set to 1 in mode sense(10) */
	sdev->set_dbd_for_ms = 1;

	/* allow SCSI layer to restart the device in case of errors */
	sdev->allow_restart = 1;

	/* REPORT SUPPORTED OPERATION CODES is not supported */
	sdev->no_report_opcodes = 1;

	/* WRITE_SAME command is not supported */
	sdev->no_write_same = 1;

	ufshcd_set_queue_depth(sdev);

	ufshcd_get_lu_power_on_wp_status(hba, sdev);

	ufshcd_setup_links(hba, sdev);

	return 0;
}

/**
 * ufshcd_change_queue_depth - change queue depth
 * @sdev: pointer to SCSI device
 * @depth: required depth to set
 *
 * Change queue depth and make sure the max. limits are not crossed.
 */
static int ufshcd_change_queue_depth(struct scsi_device *sdev, int depth)
{
	struct ufs_hba *hba = shost_priv(sdev->host);

	if (depth > hba->nutrs)
		depth = hba->nutrs;
	return scsi_change_queue_depth(sdev, depth);
}

static void ufshcd_hpb_destroy(struct ufs_hba *hba, struct scsi_device *sdev)
{
	/* skip well-known LU */
	if ((sdev->lun >= UFS_UPIU_MAX_UNIT_NUM_ID) ||
	    !(hba->dev_info.hpb_enabled) || !ufshpb_is_allowed(hba))
		return;

	ufshpb_destroy_lu(hba, sdev);
}

static void ufshcd_hpb_configure(struct ufs_hba *hba, struct scsi_device *sdev)
{
	/* skip well-known LU */
	if ((sdev->lun >= UFS_UPIU_MAX_UNIT_NUM_ID) ||
	    !(hba->dev_info.hpb_enabled) || !ufshpb_is_allowed(hba))
		return;

	ufshpb_init_hpb_lu(hba, sdev);
}

/**
 * ufshcd_slave_configure - adjust SCSI device configurations
 * @sdev: pointer to SCSI device
 */
static int ufshcd_slave_configure(struct scsi_device *sdev)
{
	struct ufs_hba *hba = shost_priv(sdev->host);
	struct request_queue *q = sdev->request_queue;

	ufshcd_hpb_configure(hba, sdev);

	blk_queue_update_dma_pad(q, PRDT_DATA_BYTE_COUNT_PAD - 1);
	if (hba->quirks & UFSHCD_QUIRK_ALIGN_SG_WITH_PAGE_SIZE)
		blk_queue_update_dma_alignment(q, PAGE_SIZE - 1);
	/*
	 * Block runtime-pm until all consumers are added.
	 * Refer ufshcd_setup_links().
	 */
	if (is_device_wlun(sdev))
		pm_runtime_get_noresume(&sdev->sdev_gendev);
	else if (ufshcd_is_rpm_autosuspend_allowed(hba))
		sdev->rpm_autosuspend = 1;
	/*
	 * Do not print messages during runtime PM to avoid never-ending cycles
	 * of messages written back to storage by user space causing runtime
	 * resume, causing more messages and so on.
	 */
	sdev->silence_suspend = 1;

	ufshcd_crypto_setup_rq_keyslot_manager(hba, q);

	return 0;
}

/**
 * ufshcd_slave_destroy - remove SCSI device configurations
 * @sdev: pointer to SCSI device
 */
static void ufshcd_slave_destroy(struct scsi_device *sdev)
{
	struct ufs_hba *hba;
	unsigned long flags;

	hba = shost_priv(sdev->host);

	ufshcd_hpb_destroy(hba, sdev);

	/* Drop the reference as it won't be needed anymore */
	if (ufshcd_scsi_to_upiu_lun(sdev->lun) == UFS_UPIU_UFS_DEVICE_WLUN) {
		spin_lock_irqsave(hba->host->host_lock, flags);
		hba->sdev_ufs_device = NULL;
		spin_unlock_irqrestore(hba->host->host_lock, flags);
	} else if (hba->sdev_ufs_device) {
		struct device *supplier = NULL;

		/* Ensure UFS Device WLUN exists and does not disappear */
		spin_lock_irqsave(hba->host->host_lock, flags);
		if (hba->sdev_ufs_device) {
			supplier = &hba->sdev_ufs_device->sdev_gendev;
			get_device(supplier);
		}
		spin_unlock_irqrestore(hba->host->host_lock, flags);

		if (supplier) {
			/*
			 * If a LUN fails to probe (e.g. absent BOOT WLUN), the
			 * device will not have been registered but can still
			 * have a device link holding a reference to the device.
			 */
			device_link_remove(&sdev->sdev_gendev, supplier);
			put_device(supplier);
		}
	}
}

/**
 * ufshcd_scsi_cmd_status - Update SCSI command result based on SCSI status
 * @lrbp: pointer to local reference block of completed command
 * @scsi_status: SCSI command status
 *
 * Returns value base on SCSI command status
 */
static inline int
ufshcd_scsi_cmd_status(struct ufshcd_lrb *lrbp, int scsi_status)
{
	int result = 0;

	switch (scsi_status) {
	case SAM_STAT_CHECK_CONDITION:
		ufshcd_copy_sense_data(lrbp);
		fallthrough;
	case SAM_STAT_GOOD:
		result |= DID_OK << 16 | scsi_status;
		break;
	case SAM_STAT_TASK_SET_FULL:
	case SAM_STAT_BUSY:
	case SAM_STAT_TASK_ABORTED:
		ufshcd_copy_sense_data(lrbp);
		result |= scsi_status;
		break;
	default:
		result |= DID_ERROR << 16;
		break;
	} /* end of switch */

	return result;
}

/**
 * ufshcd_transfer_rsp_status - Get overall status of the response
 * @hba: per adapter instance
 * @lrbp: pointer to local reference block of completed command
 *
 * Returns result of the command to notify SCSI midlayer
 */
static inline int
ufshcd_transfer_rsp_status(struct ufs_hba *hba, struct ufshcd_lrb *lrbp)
{
	int result = 0;
	int scsi_status;
	int ocs;

	/* overall command status of utrd */
	ocs = ufshcd_get_tr_ocs(lrbp);

	if (hba->quirks & UFSHCD_QUIRK_BROKEN_OCS_FATAL_ERROR) {
		if (be32_to_cpu(lrbp->ucd_rsp_ptr->header.dword_1) &
					MASK_RSP_UPIU_RESULT)
			ocs = OCS_SUCCESS;
	}

	switch (ocs) {
	case OCS_SUCCESS:
		result = ufshcd_get_req_rsp(lrbp->ucd_rsp_ptr);
		hba->ufs_stats.last_hibern8_exit_tstamp = ktime_set(0, 0);
		switch (result) {
		case UPIU_TRANSACTION_RESPONSE:
			/*
			 * get the response UPIU result to extract
			 * the SCSI command status
			 */
			result = ufshcd_get_rsp_upiu_result(lrbp->ucd_rsp_ptr);

			/*
			 * get the result based on SCSI status response
			 * to notify the SCSI midlayer of the command status
			 */
			scsi_status = result & MASK_SCSI_STATUS;
			result = ufshcd_scsi_cmd_status(lrbp, scsi_status);

			/*
			 * Currently we are only supporting BKOPs exception
			 * events hence we can ignore BKOPs exception event
			 * during power management callbacks. BKOPs exception
			 * event is not expected to be raised in runtime suspend
			 * callback as it allows the urgent bkops.
			 * During system suspend, we are anyway forcefully
			 * disabling the bkops and if urgent bkops is needed
			 * it will be enabled on system resume. Long term
			 * solution could be to abort the system suspend if
			 * UFS device needs urgent BKOPs.
			 */
			if (!hba->pm_op_in_progress &&
			    !ufshcd_eh_in_progress(hba) &&
			    ufshcd_is_exception_event(lrbp->ucd_rsp_ptr))
				/* Flushed in suspend */
				schedule_work(&hba->eeh_work);

			if (scsi_status == SAM_STAT_GOOD)
				ufshpb_rsp_upiu(hba, lrbp);
			break;
		case UPIU_TRANSACTION_REJECT_UPIU:
			/* TODO: handle Reject UPIU Response */
			result = DID_ERROR << 16;
			dev_err(hba->dev,
				"Reject UPIU not fully implemented\n");
			break;
		default:
			dev_err(hba->dev,
				"Unexpected request response code = %x\n",
				result);
			result = DID_ERROR << 16;
			break;
		}
		break;
	case OCS_ABORTED:
		result |= DID_ABORT << 16;
		break;
	case OCS_INVALID_COMMAND_STATUS:
		result |= DID_REQUEUE << 16;
		break;
	case OCS_INVALID_CMD_TABLE_ATTR:
	case OCS_INVALID_PRDT_ATTR:
	case OCS_MISMATCH_DATA_BUF_SIZE:
	case OCS_MISMATCH_RESP_UPIU_SIZE:
	case OCS_PEER_COMM_FAILURE:
	case OCS_FATAL_ERROR:
	case OCS_DEVICE_FATAL_ERROR:
	case OCS_INVALID_CRYPTO_CONFIG:
	case OCS_GENERAL_CRYPTO_ERROR:
	default:
		result |= DID_ERROR << 16;
		dev_err(hba->dev,
				"OCS error from controller = %x for tag %d\n",
				ocs, lrbp->task_tag);
		ufshcd_print_evt_hist(hba);
		ufshcd_print_host_state(hba);
		break;
	} /* end of switch */

	if ((host_byte(result) != DID_OK) &&
	    (host_byte(result) != DID_REQUEUE) && !hba->silence_err_logs)
		ufshcd_print_trs(hba, 1 << lrbp->task_tag, true);
	return result;
}

static bool ufshcd_is_auto_hibern8_error(struct ufs_hba *hba,
					 u32 intr_mask)
{
	if (!ufshcd_is_auto_hibern8_supported(hba) ||
	    !ufshcd_is_auto_hibern8_enabled(hba))
		return false;

	if (!(intr_mask & UFSHCD_UIC_HIBERN8_MASK))
		return false;

	if (hba->active_uic_cmd &&
	    (hba->active_uic_cmd->command == UIC_CMD_DME_HIBER_ENTER ||
	    hba->active_uic_cmd->command == UIC_CMD_DME_HIBER_EXIT))
		return false;

	return true;
}

/**
 * ufshcd_uic_cmd_compl - handle completion of uic command
 * @hba: per adapter instance
 * @intr_status: interrupt status generated by the controller
 *
 * Returns
 *  IRQ_HANDLED - If interrupt is valid
 *  IRQ_NONE    - If invalid interrupt
 */
static irqreturn_t ufshcd_uic_cmd_compl(struct ufs_hba *hba, u32 intr_status)
{
	irqreturn_t retval = IRQ_NONE;

	spin_lock(hba->host->host_lock);
	if (ufshcd_is_auto_hibern8_error(hba, intr_status))
		hba->errors |= (UFSHCD_UIC_HIBERN8_MASK & intr_status);

	if ((intr_status & UIC_COMMAND_COMPL) && hba->active_uic_cmd) {
		hba->active_uic_cmd->argument2 |=
			ufshcd_get_uic_cmd_result(hba);
		hba->active_uic_cmd->argument3 =
			ufshcd_get_dme_attr_val(hba);
		if (!hba->uic_async_done)
			hba->active_uic_cmd->cmd_active = 0;
		complete(&hba->active_uic_cmd->done);
		retval = IRQ_HANDLED;
	}

	if ((intr_status & UFSHCD_UIC_PWR_MASK) && hba->uic_async_done) {
		hba->active_uic_cmd->cmd_active = 0;
		complete(hba->uic_async_done);
		retval = IRQ_HANDLED;
	}

	if (retval == IRQ_HANDLED)
		ufshcd_add_uic_command_trace(hba, hba->active_uic_cmd,
					     UFS_CMD_COMP);
	spin_unlock(hba->host->host_lock);
	return retval;
}

/**
 * __ufshcd_transfer_req_compl - handle SCSI and query command completion
 * @hba: per adapter instance
 * @completed_reqs: bitmask that indicates which requests to complete
 * @retry_requests: whether to ask the SCSI core to retry completed requests
 */
static void __ufshcd_transfer_req_compl(struct ufs_hba *hba,
					unsigned long completed_reqs,
					bool retry_requests)
{
	struct ufshcd_lrb *lrbp;
	struct scsi_cmnd *cmd;
	int result;
	int index;
	bool update_scaling = false;

	for_each_set_bit(index, &completed_reqs, hba->nutrs) {
		lrbp = &hba->lrb[index];
		lrbp->compl_time_stamp = ktime_get();
		cmd = lrbp->cmd;
		if (cmd) {
			if (unlikely(ufshcd_should_inform_monitor(hba, lrbp)))
				ufshcd_update_monitor(hba, lrbp);
			ufshcd_add_command_trace(hba, index, UFS_CMD_COMP);
			result = retry_requests ? DID_BUS_BUSY << 16 :
				ufshcd_transfer_rsp_status(hba, lrbp);
			scsi_dma_unmap(cmd);
			cmd->result = result;
			/* Mark completed command as NULL in LRB */
			lrbp->cmd = NULL;
			/* Do not touch lrbp after scsi done */
			cmd->scsi_done(cmd);
			ufshcd_release(hba);
			update_scaling = true;
		} else if (lrbp->command_type == UTP_CMD_TYPE_DEV_MANAGE ||
			lrbp->command_type == UTP_CMD_TYPE_UFS_STORAGE) {
			if (hba->dev_cmd.complete) {
				ufshcd_add_command_trace(hba, index,
							 UFS_DEV_COMP);
				complete(hba->dev_cmd.complete);
				update_scaling = true;
			}
		}
		if (update_scaling)
			ufshcd_clk_scaling_update_busy(hba);
	}
}

/**
 * ufshcd_transfer_req_compl - handle SCSI and query command completion
 * @hba: per adapter instance
 * @retry_requests: whether or not to ask to retry requests
 *
 * Returns
 *  IRQ_HANDLED - If interrupt is valid
 *  IRQ_NONE    - If invalid interrupt
 */
static irqreturn_t ufshcd_transfer_req_compl(struct ufs_hba *hba,
					     bool retry_requests)
{
	unsigned long completed_reqs, flags;
	u32 tr_doorbell;

	/* Resetting interrupt aggregation counters first and reading the
	 * DOOR_BELL afterward allows us to handle all the completed requests.
	 * In order to prevent other interrupts starvation the DB is read once
	 * after reset. The down side of this solution is the possibility of
	 * false interrupt if device completes another request after resetting
	 * aggregation and before reading the DB.
	 */
	if (ufshcd_is_intr_aggr_allowed(hba) &&
	    !(hba->quirks & UFSHCI_QUIRK_SKIP_RESET_INTR_AGGR))
		ufshcd_reset_intr_aggr(hba);

	if (ufs_fail_completion())
		return IRQ_HANDLED;

	spin_lock_irqsave(&hba->outstanding_lock, flags);
	tr_doorbell = ufshcd_readl(hba, REG_UTP_TRANSFER_REQ_DOOR_BELL);
	completed_reqs = ~tr_doorbell & hba->outstanding_reqs;
	WARN_ONCE(completed_reqs & ~hba->outstanding_reqs,
		  "completed: %#lx; outstanding: %#lx\n", completed_reqs,
		  hba->outstanding_reqs);
	hba->outstanding_reqs &= ~completed_reqs;
	spin_unlock_irqrestore(&hba->outstanding_lock, flags);

	if (completed_reqs) {
		__ufshcd_transfer_req_compl(hba, completed_reqs,
					    retry_requests);
		return IRQ_HANDLED;
	} else {
		return IRQ_NONE;
	}
}

int __ufshcd_write_ee_control(struct ufs_hba *hba, u32 ee_ctrl_mask)
{
	return ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_WRITE_ATTR,
				       QUERY_ATTR_IDN_EE_CONTROL, 0, 0,
				       &ee_ctrl_mask);
}

int ufshcd_write_ee_control(struct ufs_hba *hba)
{
	int err;

	mutex_lock(&hba->ee_ctrl_mutex);
	err = __ufshcd_write_ee_control(hba, hba->ee_ctrl_mask);
	mutex_unlock(&hba->ee_ctrl_mutex);
	if (err)
		dev_err(hba->dev, "%s: failed to write ee control %d\n",
			__func__, err);
	return err;
}

int ufshcd_update_ee_control(struct ufs_hba *hba, u16 *mask, u16 *other_mask,
			     u16 set, u16 clr)
{
	u16 new_mask, ee_ctrl_mask;
	int err = 0;

	mutex_lock(&hba->ee_ctrl_mutex);
	new_mask = (*mask & ~clr) | set;
	ee_ctrl_mask = new_mask | *other_mask;
	if (ee_ctrl_mask != hba->ee_ctrl_mask)
		err = __ufshcd_write_ee_control(hba, ee_ctrl_mask);
	/* Still need to update 'mask' even if 'ee_ctrl_mask' was unchanged */
	if (!err) {
		hba->ee_ctrl_mask = ee_ctrl_mask;
		*mask = new_mask;
	}
	mutex_unlock(&hba->ee_ctrl_mutex);
	return err;
}

/**
 * ufshcd_disable_ee - disable exception event
 * @hba: per-adapter instance
 * @mask: exception event to disable
 *
 * Disables exception event in the device so that the EVENT_ALERT
 * bit is not set.
 *
 * Returns zero on success, non-zero error value on failure.
 */
static inline int ufshcd_disable_ee(struct ufs_hba *hba, u16 mask)
{
	return ufshcd_update_ee_drv_mask(hba, 0, mask);
}

/**
 * ufshcd_enable_ee - enable exception event
 * @hba: per-adapter instance
 * @mask: exception event to enable
 *
 * Enable corresponding exception event in the device to allow
 * device to alert host in critical scenarios.
 *
 * Returns zero on success, non-zero error value on failure.
 */
static inline int ufshcd_enable_ee(struct ufs_hba *hba, u16 mask)
{
	return ufshcd_update_ee_drv_mask(hba, mask, 0);
}

/**
 * ufshcd_enable_auto_bkops - Allow device managed BKOPS
 * @hba: per-adapter instance
 *
 * Allow device to manage background operations on its own. Enabling
 * this might lead to inconsistent latencies during normal data transfers
 * as the device is allowed to manage its own way of handling background
 * operations.
 *
 * Returns zero on success, non-zero on failure.
 */
static int ufshcd_enable_auto_bkops(struct ufs_hba *hba)
{
	int err = 0;

	if (hba->auto_bkops_enabled)
		goto out;

	err = ufshcd_query_flag_retry(hba, UPIU_QUERY_OPCODE_SET_FLAG,
			QUERY_FLAG_IDN_BKOPS_EN, 0, NULL);
	if (err) {
		dev_err(hba->dev, "%s: failed to enable bkops %d\n",
				__func__, err);
		goto out;
	}

	hba->auto_bkops_enabled = true;
	trace_ufshcd_auto_bkops_state(dev_name(hba->dev), "Enabled");

	/* No need of URGENT_BKOPS exception from the device */
	err = ufshcd_disable_ee(hba, MASK_EE_URGENT_BKOPS);
	if (err)
		dev_err(hba->dev, "%s: failed to disable exception event %d\n",
				__func__, err);
out:
	return err;
}

/**
 * ufshcd_disable_auto_bkops - block device in doing background operations
 * @hba: per-adapter instance
 *
 * Disabling background operations improves command response latency but
 * has drawback of device moving into critical state where the device is
 * not-operable. Make sure to call ufshcd_enable_auto_bkops() whenever the
 * host is idle so that BKOPS are managed effectively without any negative
 * impacts.
 *
 * Returns zero on success, non-zero on failure.
 */
static int ufshcd_disable_auto_bkops(struct ufs_hba *hba)
{
	int err = 0;

	if (!hba->auto_bkops_enabled)
		goto out;

	/*
	 * If host assisted BKOPs is to be enabled, make sure
	 * urgent bkops exception is allowed.
	 */
	err = ufshcd_enable_ee(hba, MASK_EE_URGENT_BKOPS);
	if (err) {
		dev_err(hba->dev, "%s: failed to enable exception event %d\n",
				__func__, err);
		goto out;
	}

	err = ufshcd_query_flag_retry(hba, UPIU_QUERY_OPCODE_CLEAR_FLAG,
			QUERY_FLAG_IDN_BKOPS_EN, 0, NULL);
	if (err) {
		dev_err(hba->dev, "%s: failed to disable bkops %d\n",
				__func__, err);
		ufshcd_disable_ee(hba, MASK_EE_URGENT_BKOPS);
		goto out;
	}

	hba->auto_bkops_enabled = false;
	trace_ufshcd_auto_bkops_state(dev_name(hba->dev), "Disabled");
	hba->is_urgent_bkops_lvl_checked = false;
out:
	return err;
}

/**
 * ufshcd_force_reset_auto_bkops - force reset auto bkops state
 * @hba: per adapter instance
 *
 * After a device reset the device may toggle the BKOPS_EN flag
 * to default value. The s/w tracking variables should be updated
 * as well. This function would change the auto-bkops state based on
 * UFSHCD_CAP_KEEP_AUTO_BKOPS_ENABLED_EXCEPT_SUSPEND.
 */
static void ufshcd_force_reset_auto_bkops(struct ufs_hba *hba)
{
	if (ufshcd_keep_autobkops_enabled_except_suspend(hba)) {
		hba->auto_bkops_enabled = false;
		hba->ee_ctrl_mask |= MASK_EE_URGENT_BKOPS;
		ufshcd_enable_auto_bkops(hba);
	} else {
		hba->auto_bkops_enabled = true;
		hba->ee_ctrl_mask &= ~MASK_EE_URGENT_BKOPS;
		ufshcd_disable_auto_bkops(hba);
	}
	hba->urgent_bkops_lvl = BKOPS_STATUS_PERF_IMPACT;
	hba->is_urgent_bkops_lvl_checked = false;
}

static inline int ufshcd_get_bkops_status(struct ufs_hba *hba, u32 *status)
{
	return ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_READ_ATTR,
			QUERY_ATTR_IDN_BKOPS_STATUS, 0, 0, status);
}

/**
 * ufshcd_bkops_ctrl - control the auto bkops based on current bkops status
 * @hba: per-adapter instance
 * @status: bkops_status value
 *
 * Read the bkops_status from the UFS device and Enable fBackgroundOpsEn
 * flag in the device to permit background operations if the device
 * bkops_status is greater than or equal to "status" argument passed to
 * this function, disable otherwise.
 *
 * Returns 0 for success, non-zero in case of failure.
 *
 * NOTE: Caller of this function can check the "hba->auto_bkops_enabled" flag
 * to know whether auto bkops is enabled or disabled after this function
 * returns control to it.
 */
static int ufshcd_bkops_ctrl(struct ufs_hba *hba,
			     enum bkops_status status)
{
	int err;
	u32 curr_status = 0;

	err = ufshcd_get_bkops_status(hba, &curr_status);
	if (err) {
		dev_err(hba->dev, "%s: failed to get BKOPS status %d\n",
				__func__, err);
		goto out;
	} else if (curr_status > BKOPS_STATUS_MAX) {
		dev_err(hba->dev, "%s: invalid BKOPS status %d\n",
				__func__, curr_status);
		err = -EINVAL;
		goto out;
	}

	if (curr_status >= status)
		err = ufshcd_enable_auto_bkops(hba);
	else
		err = ufshcd_disable_auto_bkops(hba);
out:
	return err;
}

/**
 * ufshcd_urgent_bkops - handle urgent bkops exception event
 * @hba: per-adapter instance
 *
 * Enable fBackgroundOpsEn flag in the device to permit background
 * operations.
 *
 * If BKOPs is enabled, this function returns 0, 1 if the bkops in not enabled
 * and negative error value for any other failure.
 */
static int ufshcd_urgent_bkops(struct ufs_hba *hba)
{
	return ufshcd_bkops_ctrl(hba, hba->urgent_bkops_lvl);
}

static inline int ufshcd_get_ee_status(struct ufs_hba *hba, u32 *status)
{
	return ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_READ_ATTR,
			QUERY_ATTR_IDN_EE_STATUS, 0, 0, status);
}

static void ufshcd_bkops_exception_event_handler(struct ufs_hba *hba)
{
	int err;
	u32 curr_status = 0;

	if (hba->is_urgent_bkops_lvl_checked)
		goto enable_auto_bkops;

	err = ufshcd_get_bkops_status(hba, &curr_status);
	if (err) {
		dev_err(hba->dev, "%s: failed to get BKOPS status %d\n",
				__func__, err);
		goto out;
	}

	/*
	 * We are seeing that some devices are raising the urgent bkops
	 * exception events even when BKOPS status doesn't indicate performace
	 * impacted or critical. Handle these device by determining their urgent
	 * bkops status at runtime.
	 */
	if (curr_status < BKOPS_STATUS_PERF_IMPACT) {
		dev_err(hba->dev, "%s: device raised urgent BKOPS exception for bkops status %d\n",
				__func__, curr_status);
		/* update the current status as the urgent bkops level */
		hba->urgent_bkops_lvl = curr_status;
		hba->is_urgent_bkops_lvl_checked = true;
	}

enable_auto_bkops:
	err = ufshcd_enable_auto_bkops(hba);
out:
	if (err < 0)
		dev_err(hba->dev, "%s: failed to handle urgent bkops %d\n",
				__func__, err);
}

static int __ufshcd_wb_toggle(struct ufs_hba *hba, bool set, enum flag_idn idn)
{
	u8 index;
	enum query_opcode opcode = set ? UPIU_QUERY_OPCODE_SET_FLAG :
				   UPIU_QUERY_OPCODE_CLEAR_FLAG;

	index = ufshcd_wb_get_query_index(hba);
	return ufshcd_query_flag_retry(hba, opcode, idn, index, NULL);
}

int ufshcd_wb_toggle(struct ufs_hba *hba, bool enable)
{
	int ret;

	if (!ufshcd_is_wb_allowed(hba))
		return 0;

	if (!(enable ^ hba->dev_info.wb_enabled))
		return 0;

	ret = __ufshcd_wb_toggle(hba, enable, QUERY_FLAG_IDN_WB_EN);
	if (ret) {
		dev_err(hba->dev, "%s Write Booster %s failed %d\n",
			__func__, enable ? "enable" : "disable", ret);
		return ret;
	}

	hba->dev_info.wb_enabled = enable;
	dev_dbg(hba->dev, "%s Write Booster %s\n",
			__func__, enable ? "enabled" : "disabled");

	return ret;
}

static void ufshcd_wb_toggle_flush_during_h8(struct ufs_hba *hba, bool set)
{
	int ret;

	ret = __ufshcd_wb_toggle(hba, set,
			QUERY_FLAG_IDN_WB_BUFF_FLUSH_DURING_HIBERN8);
	if (ret) {
		dev_err(hba->dev, "%s: WB-Buf Flush during H8 %s failed: %d\n",
			__func__, set ? "enable" : "disable", ret);
		return;
	}
	dev_dbg(hba->dev, "%s WB-Buf Flush during H8 %s\n",
			__func__, set ? "enabled" : "disabled");
}

static inline void ufshcd_wb_toggle_flush(struct ufs_hba *hba, bool enable)
{
	int ret;

	if (!ufshcd_is_wb_allowed(hba) ||
	    hba->dev_info.wb_buf_flush_enabled == enable)
		return;

	ret = __ufshcd_wb_toggle(hba, enable, QUERY_FLAG_IDN_WB_BUFF_FLUSH_EN);
	if (ret) {
		dev_err(hba->dev, "%s WB-Buf Flush %s failed %d\n", __func__,
			enable ? "enable" : "disable", ret);
		return;
	}

	hba->dev_info.wb_buf_flush_enabled = enable;

	dev_dbg(hba->dev, "%s WB-Buf Flush %s\n",
			__func__, enable ? "enabled" : "disabled");
}

static bool ufshcd_wb_presrv_usrspc_keep_vcc_on(struct ufs_hba *hba,
						u32 avail_buf)
{
	u32 cur_buf;
	int ret;
	u8 index;

	index = ufshcd_wb_get_query_index(hba);
	ret = ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_READ_ATTR,
					      QUERY_ATTR_IDN_CURR_WB_BUFF_SIZE,
					      index, 0, &cur_buf);
	if (ret) {
		dev_err(hba->dev, "%s dCurWriteBoosterBufferSize read failed %d\n",
			__func__, ret);
		return false;
	}

	if (!cur_buf) {
		dev_info(hba->dev, "dCurWBBuf: %d WB disabled until free-space is available\n",
			 cur_buf);
		return false;
	}
	/* Let it continue to flush when available buffer exceeds threshold */
	if (avail_buf < hba->vps->wb_flush_threshold)
		return true;

	return false;
}

static bool ufshcd_wb_need_flush(struct ufs_hba *hba)
{
	int ret;
	u32 avail_buf;
	u8 index;

	if (!ufshcd_is_wb_allowed(hba))
		return false;
	/*
	 * The ufs device needs the vcc to be ON to flush.
	 * With user-space reduction enabled, it's enough to enable flush
	 * by checking only the available buffer. The threshold
	 * defined here is > 90% full.
	 * With user-space preserved enabled, the current-buffer
	 * should be checked too because the wb buffer size can reduce
	 * when disk tends to be full. This info is provided by current
	 * buffer (dCurrentWriteBoosterBufferSize). There's no point in
	 * keeping vcc on when current buffer is empty.
	 */
	index = ufshcd_wb_get_query_index(hba);
	ret = ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_READ_ATTR,
				      QUERY_ATTR_IDN_AVAIL_WB_BUFF_SIZE,
				      index, 0, &avail_buf);
	if (ret) {
		dev_warn(hba->dev, "%s dAvailableWriteBoosterBufferSize read failed %d\n",
			 __func__, ret);
		return false;
	}

	if (!hba->dev_info.b_presrv_uspc_en) {
		if (avail_buf <= UFS_WB_BUF_REMAIN_PERCENT(10))
			return true;
		return false;
	}

	return ufshcd_wb_presrv_usrspc_keep_vcc_on(hba, avail_buf);
}

static void ufshcd_rpm_dev_flush_recheck_work(struct work_struct *work)
{
	struct ufs_hba *hba = container_of(to_delayed_work(work),
					   struct ufs_hba,
					   rpm_dev_flush_recheck_work);
	/*
	 * To prevent unnecessary VCC power drain after device finishes
	 * WriteBooster buffer flush or Auto BKOPs, force runtime resume
	 * after a certain delay to recheck the threshold by next runtime
	 * suspend.
	 */
	ufshcd_rpm_get_sync(hba);
	ufshcd_rpm_put_sync(hba);
}

/**
 * ufshcd_exception_event_handler - handle exceptions raised by device
 * @work: pointer to work data
 *
 * Read bExceptionEventStatus attribute from the device and handle the
 * exception event accordingly.
 */
static void ufshcd_exception_event_handler(struct work_struct *work)
{
	struct ufs_hba *hba;
	int err;
	u32 status = 0;
	hba = container_of(work, struct ufs_hba, eeh_work);

	ufshcd_scsi_block_requests(hba);
	err = ufshcd_get_ee_status(hba, &status);
	if (err) {
		dev_err(hba->dev, "%s: failed to get exception status %d\n",
				__func__, err);
		goto out;
	}

	trace_ufshcd_exception_event(dev_name(hba->dev), status);

	if (status & hba->ee_drv_mask & MASK_EE_URGENT_BKOPS)
		ufshcd_bkops_exception_event_handler(hba);

	ufs_debugfs_exception_event(hba, status);
out:
	ufshcd_scsi_unblock_requests(hba);
	return;
}

/* Complete requests that have door-bell cleared */
static void ufshcd_complete_requests(struct ufs_hba *hba)
{
	ufshcd_transfer_req_compl(hba, /*retry_requests=*/false);
	ufshcd_tmc_handler(hba);
}

static void ufshcd_retry_aborted_requests(struct ufs_hba *hba)
{
	ufshcd_transfer_req_compl(hba, /*retry_requests=*/true);
	ufshcd_tmc_handler(hba);
}

/**
 * ufshcd_quirk_dl_nac_errors - This function checks if error handling is
 *				to recover from the DL NAC errors or not.
 * @hba: per-adapter instance
 *
 * Returns true if error handling is required, false otherwise
 */
static bool ufshcd_quirk_dl_nac_errors(struct ufs_hba *hba)
{
	unsigned long flags;
	bool err_handling = true;

	spin_lock_irqsave(hba->host->host_lock, flags);
	/*
	 * UFS_DEVICE_QUIRK_RECOVERY_FROM_DL_NAC_ERRORS only workaround the
	 * device fatal error and/or DL NAC & REPLAY timeout errors.
	 */
	if (hba->saved_err & (CONTROLLER_FATAL_ERROR | SYSTEM_BUS_FATAL_ERROR))
		goto out;

	if ((hba->saved_err & DEVICE_FATAL_ERROR) ||
	    ((hba->saved_err & UIC_ERROR) &&
	     (hba->saved_uic_err & UFSHCD_UIC_DL_TCx_REPLAY_ERROR)))
		goto out;

	if ((hba->saved_err & UIC_ERROR) &&
	    (hba->saved_uic_err & UFSHCD_UIC_DL_NAC_RECEIVED_ERROR)) {
		int err;
		/*
		 * wait for 50ms to see if we can get any other errors or not.
		 */
		spin_unlock_irqrestore(hba->host->host_lock, flags);
		msleep(50);
		spin_lock_irqsave(hba->host->host_lock, flags);

		/*
		 * now check if we have got any other severe errors other than
		 * DL NAC error?
		 */
		if ((hba->saved_err & INT_FATAL_ERRORS) ||
		    ((hba->saved_err & UIC_ERROR) &&
		    (hba->saved_uic_err & ~UFSHCD_UIC_DL_NAC_RECEIVED_ERROR)))
			goto out;

		/*
		 * As DL NAC is the only error received so far, send out NOP
		 * command to confirm if link is still active or not.
		 *   - If we don't get any response then do error recovery.
		 *   - If we get response then clear the DL NAC error bit.
		 */

		spin_unlock_irqrestore(hba->host->host_lock, flags);
		err = ufshcd_verify_dev_init(hba);
		spin_lock_irqsave(hba->host->host_lock, flags);

		if (err)
			goto out;

		/* Link seems to be alive hence ignore the DL NAC errors */
		if (hba->saved_uic_err == UFSHCD_UIC_DL_NAC_RECEIVED_ERROR)
			hba->saved_err &= ~UIC_ERROR;
		/* clear NAC error */
		hba->saved_uic_err &= ~UFSHCD_UIC_DL_NAC_RECEIVED_ERROR;
		if (!hba->saved_uic_err)
			err_handling = false;
	}
out:
	spin_unlock_irqrestore(hba->host->host_lock, flags);
	return err_handling;
}

/* host lock must be held before calling this func */
static inline bool ufshcd_is_saved_err_fatal(struct ufs_hba *hba)
{
	return (hba->saved_uic_err & UFSHCD_UIC_DL_PA_INIT_ERROR) ||
	       (hba->saved_err & (INT_FATAL_ERRORS | UFSHCD_UIC_HIBERN8_MASK));
}

/* host lock must be held before calling this func */
static inline void ufshcd_schedule_eh_work(struct ufs_hba *hba)
{
	/* handle fatal errors only when link is not in error state */
	if (hba->ufshcd_state != UFSHCD_STATE_ERROR) {
		if (hba->force_reset || ufshcd_is_link_broken(hba) ||
		    ufshcd_is_saved_err_fatal(hba))
			hba->ufshcd_state = UFSHCD_STATE_EH_SCHEDULED_FATAL;
		else
			hba->ufshcd_state = UFSHCD_STATE_EH_SCHEDULED_NON_FATAL;
		queue_work(hba->eh_wq, &hba->eh_work);
	}
}

static void ufshcd_clk_scaling_allow(struct ufs_hba *hba, bool allow)
{
	down_write(&hba->clk_scaling_lock);
	hba->clk_scaling.is_allowed = allow;
	up_write(&hba->clk_scaling_lock);
}

static void ufshcd_clk_scaling_suspend(struct ufs_hba *hba, bool suspend)
{
	if (suspend) {
		if (hba->clk_scaling.is_enabled)
			ufshcd_suspend_clkscaling(hba);
		ufshcd_clk_scaling_allow(hba, false);
	} else {
		ufshcd_clk_scaling_allow(hba, true);
		if (hba->clk_scaling.is_enabled)
			ufshcd_resume_clkscaling(hba);
	}
}

static void ufshcd_err_handling_prepare(struct ufs_hba *hba)
{
	ufshcd_rpm_get_sync(hba);
	if (pm_runtime_status_suspended(&hba->sdev_ufs_device->sdev_gendev) ||
	    hba->is_sys_suspended) {
		enum ufs_pm_op pm_op;

		/*
		 * Don't assume anything of resume, if
		 * resume fails, irq and clocks can be OFF, and powers
		 * can be OFF or in LPM.
		 */
		ufshcd_setup_hba_vreg(hba, true);
		ufshcd_enable_irq(hba);
		ufshcd_setup_vreg(hba, true);
		ufshcd_config_vreg_hpm(hba, hba->vreg_info.vccq);
		ufshcd_config_vreg_hpm(hba, hba->vreg_info.vccq2);
		ufshcd_hold(hba, false);
		if (!ufshcd_is_clkgating_allowed(hba))
			ufshcd_setup_clocks(hba, true);
		ufshcd_release(hba);
		pm_op = hba->is_sys_suspended ? UFS_SYSTEM_PM : UFS_RUNTIME_PM;
		ufshcd_vops_resume(hba, pm_op);
	} else {
		ufshcd_hold(hba, false);
		if (ufshcd_is_clkscaling_supported(hba) &&
		    hba->clk_scaling.is_enabled)
			ufshcd_suspend_clkscaling(hba);
		ufshcd_clk_scaling_allow(hba, false);
	}
	ufshcd_scsi_block_requests(hba);
	/* Drain ufshcd_queuecommand() */
	down_write(&hba->clk_scaling_lock);
	up_write(&hba->clk_scaling_lock);
	cancel_work_sync(&hba->eeh_work);
}

static void ufshcd_err_handling_unprepare(struct ufs_hba *hba)
{
	ufshcd_scsi_unblock_requests(hba);
	ufshcd_release(hba);
	if (ufshcd_is_clkscaling_supported(hba))
		ufshcd_clk_scaling_suspend(hba, false);
	ufshcd_rpm_put(hba);
}

static inline bool ufshcd_err_handling_should_stop(struct ufs_hba *hba)
{
	return (!hba->is_powered || hba->shutting_down ||
		!hba->sdev_ufs_device ||
		hba->ufshcd_state == UFSHCD_STATE_ERROR ||
		(!(hba->saved_err || hba->saved_uic_err || hba->force_reset ||
		   ufshcd_is_link_broken(hba))));
}

#ifdef CONFIG_PM
static void ufshcd_recover_pm_error(struct ufs_hba *hba)
{
	struct Scsi_Host *shost = hba->host;
	struct scsi_device *sdev;
	struct request_queue *q;
	int ret;

	hba->is_sys_suspended = false;
	/*
	 * Set RPM status of wlun device to RPM_ACTIVE,
	 * this also clears its runtime error.
	 */
	ret = pm_runtime_set_active(&hba->sdev_ufs_device->sdev_gendev);

	/* hba device might have a runtime error otherwise */
	if (ret)
		ret = pm_runtime_set_active(hba->dev);
	/*
	 * If wlun device had runtime error, we also need to resume those
	 * consumer scsi devices in case any of them has failed to be
	 * resumed due to supplier runtime resume failure. This is to unblock
	 * blk_queue_enter in case there are bios waiting inside it.
	 */
	if (!ret) {
		shost_for_each_device(sdev, shost) {
			q = sdev->request_queue;
			if (q->dev && (q->rpm_status == RPM_SUSPENDED ||
				       q->rpm_status == RPM_SUSPENDING))
				pm_request_resume(q->dev);
		}
	}
}
#else
static inline void ufshcd_recover_pm_error(struct ufs_hba *hba)
{
}
#endif

static bool ufshcd_is_pwr_mode_restore_needed(struct ufs_hba *hba)
{
	struct ufs_pa_layer_attr *pwr_info = &hba->pwr_info;
	u32 mode;

	ufshcd_dme_get(hba, UIC_ARG_MIB(PA_PWRMODE), &mode);

	if (pwr_info->pwr_rx != ((mode >> PWRMODE_RX_OFFSET) & PWRMODE_MASK))
		return true;

	if (pwr_info->pwr_tx != (mode & PWRMODE_MASK))
		return true;

	return false;
}

/**
 * ufshcd_err_handler - handle UFS errors that require s/w attention
 * @work: pointer to work structure
 */
static void ufshcd_err_handler(struct work_struct *work)
{
	struct ufs_hba *hba;
	unsigned long flags;
	bool err_xfer = false;
	bool err_tm = false;
	int err = 0, pmc_err;
	int tag;
	bool needs_reset = false, needs_restore = false;

	hba = container_of(work, struct ufs_hba, eh_work);

	down(&hba->host_sem);
	spin_lock_irqsave(hba->host->host_lock, flags);
	if (ufshcd_err_handling_should_stop(hba)) {
		if (hba->ufshcd_state != UFSHCD_STATE_ERROR)
			hba->ufshcd_state = UFSHCD_STATE_OPERATIONAL;
		spin_unlock_irqrestore(hba->host->host_lock, flags);
		up(&hba->host_sem);
		return;
	}
	ufshcd_set_eh_in_progress(hba);
	spin_unlock_irqrestore(hba->host->host_lock, flags);
	ufshcd_err_handling_prepare(hba);
	/* Complete requests that have door-bell cleared by h/w */
	ufshcd_complete_requests(hba);
	spin_lock_irqsave(hba->host->host_lock, flags);
	if (hba->ufshcd_state != UFSHCD_STATE_ERROR)
		hba->ufshcd_state = UFSHCD_STATE_RESET;
	/*
	 * A full reset and restore might have happened after preparation
	 * is finished, double check whether we should stop.
	 */
	if (ufshcd_err_handling_should_stop(hba))
		goto skip_err_handling;

	if (hba->dev_quirks & UFS_DEVICE_QUIRK_RECOVERY_FROM_DL_NAC_ERRORS) {
		bool ret;

		spin_unlock_irqrestore(hba->host->host_lock, flags);
		/* release the lock as ufshcd_quirk_dl_nac_errors() may sleep */
		ret = ufshcd_quirk_dl_nac_errors(hba);
		spin_lock_irqsave(hba->host->host_lock, flags);
		if (!ret && ufshcd_err_handling_should_stop(hba))
			goto skip_err_handling;
	}

	if ((hba->saved_err & (INT_FATAL_ERRORS | UFSHCD_UIC_HIBERN8_MASK)) ||
	    (hba->saved_uic_err &&
	     (hba->saved_uic_err != UFSHCD_UIC_PA_GENERIC_ERROR))) {
		bool pr_prdt = !!(hba->saved_err & SYSTEM_BUS_FATAL_ERROR);

		spin_unlock_irqrestore(hba->host->host_lock, flags);
		ufshcd_print_host_state(hba);
		ufshcd_print_pwr_info(hba);
		ufshcd_print_evt_hist(hba);
		ufshcd_print_tmrs(hba, hba->outstanding_tasks);
		ufshcd_print_trs(hba, hba->outstanding_reqs, pr_prdt);
		spin_lock_irqsave(hba->host->host_lock, flags);
	}

	/*
	 * if host reset is required then skip clearing the pending
	 * transfers forcefully because they will get cleared during
	 * host reset and restore
	 */
	if (hba->force_reset || ufshcd_is_link_broken(hba) ||
	    ufshcd_is_saved_err_fatal(hba) ||
	    ((hba->saved_err & UIC_ERROR) &&
	     (hba->saved_uic_err & (UFSHCD_UIC_DL_NAC_RECEIVED_ERROR |
				    UFSHCD_UIC_DL_TCx_REPLAY_ERROR)))) {
		needs_reset = true;
		goto do_reset;
	}

	/*
	 * If LINERESET was caught, UFS might have been put to PWM mode,
	 * check if power mode restore is needed.
	 */
	if (hba->saved_uic_err & UFSHCD_UIC_PA_GENERIC_ERROR) {
		hba->saved_uic_err &= ~UFSHCD_UIC_PA_GENERIC_ERROR;
		if (!hba->saved_uic_err)
			hba->saved_err &= ~UIC_ERROR;
		spin_unlock_irqrestore(hba->host->host_lock, flags);
		if (ufshcd_is_pwr_mode_restore_needed(hba))
			needs_restore = true;
		spin_lock_irqsave(hba->host->host_lock, flags);
		if (!hba->saved_err && !needs_restore)
			goto skip_err_handling;
	}

	hba->silence_err_logs = true;
	/* release lock as clear command might sleep */
	spin_unlock_irqrestore(hba->host->host_lock, flags);
	/* Clear pending transfer requests */
	for_each_set_bit(tag, &hba->outstanding_reqs, hba->nutrs) {
		if (ufshcd_try_to_abort_task(hba, tag)) {
			err_xfer = true;
			goto lock_skip_pending_xfer_clear;
		}
	}

	/* Clear pending task management requests */
	for_each_set_bit(tag, &hba->outstanding_tasks, hba->nutmrs) {
		if (ufshcd_clear_tm_cmd(hba, tag)) {
			err_tm = true;
			goto lock_skip_pending_xfer_clear;
		}
	}

lock_skip_pending_xfer_clear:
	ufshcd_retry_aborted_requests(hba);

	spin_lock_irqsave(hba->host->host_lock, flags);
	hba->silence_err_logs = false;
	if (err_xfer || err_tm) {
		needs_reset = true;
		goto do_reset;
	}

	/*
	 * After all reqs and tasks are cleared from doorbell,
	 * now it is safe to retore power mode.
	 */
	if (needs_restore) {
		spin_unlock_irqrestore(hba->host->host_lock, flags);
		/*
		 * Hold the scaling lock just in case dev cmds
		 * are sent via bsg and/or sysfs.
		 */
		down_write(&hba->clk_scaling_lock);
		hba->force_pmc = true;
		pmc_err = ufshcd_config_pwr_mode(hba, &(hba->pwr_info));
		if (pmc_err) {
			needs_reset = true;
			dev_err(hba->dev, "%s: Failed to restore power mode, err = %d\n",
					__func__, pmc_err);
		}
		hba->force_pmc = false;
		ufshcd_print_pwr_info(hba);
		up_write(&hba->clk_scaling_lock);
		spin_lock_irqsave(hba->host->host_lock, flags);
	}

do_reset:
	/* Fatal errors need reset */
	if (needs_reset) {
		hba->force_reset = false;
		spin_unlock_irqrestore(hba->host->host_lock, flags);
		err = ufshcd_reset_and_restore(hba);
		if (err)
			dev_err(hba->dev, "%s: reset and restore failed with err %d\n",
					__func__, err);
		else
			ufshcd_recover_pm_error(hba);
		spin_lock_irqsave(hba->host->host_lock, flags);
	}

skip_err_handling:
	if (!needs_reset) {
		if (hba->ufshcd_state == UFSHCD_STATE_RESET)
			hba->ufshcd_state = UFSHCD_STATE_OPERATIONAL;
		if (hba->saved_err || hba->saved_uic_err)
			dev_err_ratelimited(hba->dev, "%s: exit: saved_err 0x%x saved_uic_err 0x%x",
			    __func__, hba->saved_err, hba->saved_uic_err);
	}
	ufshcd_clear_eh_in_progress(hba);
	spin_unlock_irqrestore(hba->host->host_lock, flags);
	ufshcd_err_handling_unprepare(hba);
	up(&hba->host_sem);
}

/**
 * ufshcd_update_uic_error - check and set fatal UIC error flags.
 * @hba: per-adapter instance
 *
 * Returns
 *  IRQ_HANDLED - If interrupt is valid
 *  IRQ_NONE    - If invalid interrupt
 */
static irqreturn_t ufshcd_update_uic_error(struct ufs_hba *hba)
{
	u32 reg;
	irqreturn_t retval = IRQ_NONE;

	/* PHY layer error */
	reg = ufshcd_readl(hba, REG_UIC_ERROR_CODE_PHY_ADAPTER_LAYER);
	if ((reg & UIC_PHY_ADAPTER_LAYER_ERROR) &&
	    (reg & UIC_PHY_ADAPTER_LAYER_ERROR_CODE_MASK)) {
		ufshcd_update_evt_hist(hba, UFS_EVT_PA_ERR, reg);
		/*
		 * To know whether this error is fatal or not, DB timeout
		 * must be checked but this error is handled separately.
		 */
		if (reg & UIC_PHY_ADAPTER_LAYER_LANE_ERR_MASK)
			dev_dbg(hba->dev, "%s: UIC Lane error reported\n",
					__func__);

		/* Got a LINERESET indication. */
		if (reg & UIC_PHY_ADAPTER_LAYER_GENERIC_ERROR) {
			struct uic_command *cmd = NULL;

			hba->uic_error |= UFSHCD_UIC_PA_GENERIC_ERROR;
			if (hba->uic_async_done && hba->active_uic_cmd)
				cmd = hba->active_uic_cmd;
			/*
			 * Ignore the LINERESET during power mode change
			 * operation via DME_SET command.
			 */
			if (cmd && (cmd->command == UIC_CMD_DME_SET))
				hba->uic_error &= ~UFSHCD_UIC_PA_GENERIC_ERROR;
		}
		retval |= IRQ_HANDLED;
	}

	/* PA_INIT_ERROR is fatal and needs UIC reset */
	reg = ufshcd_readl(hba, REG_UIC_ERROR_CODE_DATA_LINK_LAYER);
	if ((reg & UIC_DATA_LINK_LAYER_ERROR) &&
	    (reg & UIC_DATA_LINK_LAYER_ERROR_CODE_MASK)) {
		ufshcd_update_evt_hist(hba, UFS_EVT_DL_ERR, reg);

		if (reg & UIC_DATA_LINK_LAYER_ERROR_PA_INIT)
			hba->uic_error |= UFSHCD_UIC_DL_PA_INIT_ERROR;
		else if (hba->dev_quirks &
				UFS_DEVICE_QUIRK_RECOVERY_FROM_DL_NAC_ERRORS) {
			if (reg & UIC_DATA_LINK_LAYER_ERROR_NAC_RECEIVED)
				hba->uic_error |=
					UFSHCD_UIC_DL_NAC_RECEIVED_ERROR;
			else if (reg & UIC_DATA_LINK_LAYER_ERROR_TCx_REPLAY_TIMEOUT)
				hba->uic_error |= UFSHCD_UIC_DL_TCx_REPLAY_ERROR;
		}
		retval |= IRQ_HANDLED;
	}

	/* UIC NL/TL/DME errors needs software retry */
	reg = ufshcd_readl(hba, REG_UIC_ERROR_CODE_NETWORK_LAYER);
	if ((reg & UIC_NETWORK_LAYER_ERROR) &&
	    (reg & UIC_NETWORK_LAYER_ERROR_CODE_MASK)) {
		ufshcd_update_evt_hist(hba, UFS_EVT_NL_ERR, reg);
		hba->uic_error |= UFSHCD_UIC_NL_ERROR;
		retval |= IRQ_HANDLED;
	}

	reg = ufshcd_readl(hba, REG_UIC_ERROR_CODE_TRANSPORT_LAYER);
	if ((reg & UIC_TRANSPORT_LAYER_ERROR) &&
	    (reg & UIC_TRANSPORT_LAYER_ERROR_CODE_MASK)) {
		ufshcd_update_evt_hist(hba, UFS_EVT_TL_ERR, reg);
		hba->uic_error |= UFSHCD_UIC_TL_ERROR;
		retval |= IRQ_HANDLED;
	}

	reg = ufshcd_readl(hba, REG_UIC_ERROR_CODE_DME);
	if ((reg & UIC_DME_ERROR) &&
	    (reg & UIC_DME_ERROR_CODE_MASK)) {
		ufshcd_update_evt_hist(hba, UFS_EVT_DME_ERR, reg);
		hba->uic_error |= UFSHCD_UIC_DME_ERROR;
		retval |= IRQ_HANDLED;
	}

	dev_dbg(hba->dev, "%s: UIC error flags = 0x%08x\n",
			__func__, hba->uic_error);
	return retval;
}

/**
 * ufshcd_check_errors - Check for errors that need s/w attention
 * @hba: per-adapter instance
 * @intr_status: interrupt status generated by the controller
 *
 * Returns
 *  IRQ_HANDLED - If interrupt is valid
 *  IRQ_NONE    - If invalid interrupt
 */
static irqreturn_t ufshcd_check_errors(struct ufs_hba *hba, u32 intr_status)
{
	bool queue_eh_work = false;
	irqreturn_t retval = IRQ_NONE;

	spin_lock(hba->host->host_lock);
	hba->errors |= UFSHCD_ERROR_MASK & intr_status;

	if (hba->errors & INT_FATAL_ERRORS) {
		ufshcd_update_evt_hist(hba, UFS_EVT_FATAL_ERR,
				       hba->errors);
		queue_eh_work = true;
	}

	if (hba->errors & UIC_ERROR) {
		hba->uic_error = 0;
		retval = ufshcd_update_uic_error(hba);
		if (hba->uic_error)
			queue_eh_work = true;
	}

	if (hba->errors & UFSHCD_UIC_HIBERN8_MASK) {
		dev_err(hba->dev,
			"%s: Auto Hibern8 %s failed - status: 0x%08x, upmcrs: 0x%08x\n",
			__func__, (hba->errors & UIC_HIBERNATE_ENTER) ?
			"Enter" : "Exit",
			hba->errors, ufshcd_get_upmcrs(hba));
		ufshcd_update_evt_hist(hba, UFS_EVT_AUTO_HIBERN8_ERR,
				       hba->errors);
		ufshcd_set_link_broken(hba);
		queue_eh_work = true;
	}

	if (queue_eh_work) {
		/*
		 * update the transfer error masks to sticky bits, let's do this
		 * irrespective of current ufshcd_state.
		 */
		hba->saved_err |= hba->errors;
		hba->saved_uic_err |= hba->uic_error;

		/* dump controller state before resetting */
		if ((hba->saved_err &
		     (INT_FATAL_ERRORS | UFSHCD_UIC_HIBERN8_MASK)) ||
		    (hba->saved_uic_err &&
		     (hba->saved_uic_err != UFSHCD_UIC_PA_GENERIC_ERROR))) {
			dev_err(hba->dev, "%s: saved_err 0x%x saved_uic_err 0x%x\n",
					__func__, hba->saved_err,
					hba->saved_uic_err);
			ufshcd_dump_regs(hba, 0, UFSHCI_REG_SPACE_SIZE,
					 "host_regs: ");
			ufshcd_print_pwr_info(hba);
		}
		ufshcd_schedule_eh_work(hba);
		retval |= IRQ_HANDLED;
	}
	/*
	 * if (!queue_eh_work) -
	 * Other errors are either non-fatal where host recovers
	 * itself without s/w intervention or errors that will be
	 * handled by the SCSI core layer.
	 */
	hba->errors = 0;
	hba->uic_error = 0;
	spin_unlock(hba->host->host_lock);
	return retval;
}

/**
 * ufshcd_tmc_handler - handle task management function completion
 * @hba: per adapter instance
 *
 * Returns
 *  IRQ_HANDLED - If interrupt is valid
 *  IRQ_NONE    - If invalid interrupt
 */
static irqreturn_t ufshcd_tmc_handler(struct ufs_hba *hba)
{
	unsigned long flags, pending, issued;
	irqreturn_t ret = IRQ_NONE;
	int tag;

	spin_lock_irqsave(hba->host->host_lock, flags);
	pending = ufshcd_readl(hba, REG_UTP_TASK_REQ_DOOR_BELL);
	issued = hba->outstanding_tasks & ~pending;
	for_each_set_bit(tag, &issued, hba->nutmrs) {
		struct request *req = hba->tmf_rqs[tag];
		struct completion *c = req->end_io_data;

		complete(c);
		ret = IRQ_HANDLED;
	}
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	return ret;
}

/**
 * ufshcd_sl_intr - Interrupt service routine
 * @hba: per adapter instance
 * @intr_status: contains interrupts generated by the controller
 *
 * Returns
 *  IRQ_HANDLED - If interrupt is valid
 *  IRQ_NONE    - If invalid interrupt
 */
static irqreturn_t ufshcd_sl_intr(struct ufs_hba *hba, u32 intr_status)
{
	irqreturn_t retval = IRQ_NONE;

	if (intr_status & UFSHCD_UIC_MASK)
		retval |= ufshcd_uic_cmd_compl(hba, intr_status);

	if (intr_status & UFSHCD_ERROR_MASK || hba->errors)
		retval |= ufshcd_check_errors(hba, intr_status);

	if (intr_status & UTP_TASK_REQ_COMPL)
		retval |= ufshcd_tmc_handler(hba);

	if (intr_status & UTP_TRANSFER_REQ_COMPL)
		retval |= ufshcd_transfer_req_compl(hba, /*retry_requests=*/false);

	return retval;
}

/**
 * ufshcd_intr - Main interrupt service routine
 * @irq: irq number
 * @__hba: pointer to adapter instance
 *
 * Returns
 *  IRQ_HANDLED - If interrupt is valid
 *  IRQ_NONE    - If invalid interrupt
 */
static irqreturn_t ufshcd_intr(int irq, void *__hba)
{
	u32 intr_status, enabled_intr_status = 0;
	irqreturn_t retval = IRQ_NONE;
	struct ufs_hba *hba = __hba;
	int retries = hba->nutrs;

	intr_status = ufshcd_readl(hba, REG_INTERRUPT_STATUS);
	hba->ufs_stats.last_intr_status = intr_status;
	hba->ufs_stats.last_intr_ts = ktime_get();

	/*
	 * There could be max of hba->nutrs reqs in flight and in worst case
	 * if the reqs get finished 1 by 1 after the interrupt status is
	 * read, make sure we handle them by checking the interrupt status
	 * again in a loop until we process all of the reqs before returning.
	 */
	while (intr_status && retries--) {
		enabled_intr_status =
			intr_status & ufshcd_readl(hba, REG_INTERRUPT_ENABLE);
		ufshcd_writel(hba, intr_status, REG_INTERRUPT_STATUS);
		if (enabled_intr_status)
			retval |= ufshcd_sl_intr(hba, enabled_intr_status);

		intr_status = ufshcd_readl(hba, REG_INTERRUPT_STATUS);
	}

	if (enabled_intr_status && retval == IRQ_NONE &&
	    (!(enabled_intr_status & UTP_TRANSFER_REQ_COMPL) ||
	     hba->outstanding_reqs) && !ufshcd_eh_in_progress(hba)) {
		dev_err(hba->dev, "%s: Unhandled interrupt 0x%08x (0x%08x, 0x%08x)\n",
					__func__,
					intr_status,
					hba->ufs_stats.last_intr_status,
					enabled_intr_status);
		ufshcd_dump_regs(hba, 0, UFSHCI_REG_SPACE_SIZE, "host_regs: ");
	}

	return retval;
}

static int ufshcd_clear_tm_cmd(struct ufs_hba *hba, int tag)
{
	int err = 0;
	u32 mask = 1 << tag;
	unsigned long flags;

	if (!test_bit(tag, &hba->outstanding_tasks))
		goto out;

	spin_lock_irqsave(hba->host->host_lock, flags);
	ufshcd_utmrl_clear(hba, tag);
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	/* poll for max. 1 sec to clear door bell register by h/w */
	err = ufshcd_wait_for_register(hba,
			REG_UTP_TASK_REQ_DOOR_BELL,
			mask, 0, 1000, 1000);
out:
	return err;
}

static int __ufshcd_issue_tm_cmd(struct ufs_hba *hba,
		struct utp_task_req_desc *treq, u8 tm_function)
{
	struct request_queue *q = hba->tmf_queue;
	struct Scsi_Host *host = hba->host;
	DECLARE_COMPLETION_ONSTACK(wait);
	struct request *req;
	unsigned long flags;
	int task_tag, err;

	/*
	 * blk_get_request() is used here only to get a free tag.
	 */
	req = blk_get_request(q, REQ_OP_DRV_OUT, 0);
	if (IS_ERR(req))
		return PTR_ERR(req);

	req->end_io_data = &wait;
	ufshcd_hold(hba, false);

	spin_lock_irqsave(host->host_lock, flags);

	task_tag = req->tag;
	hba->tmf_rqs[req->tag] = req;
	treq->upiu_req.req_header.dword_0 |= cpu_to_be32(task_tag);

	memcpy(hba->utmrdl_base_addr + task_tag, treq, sizeof(*treq));
	ufshcd_vops_setup_task_mgmt(hba, task_tag, tm_function);

	/* send command to the controller */
	__set_bit(task_tag, &hba->outstanding_tasks);

	ufshcd_writel(hba, 1 << task_tag, REG_UTP_TASK_REQ_DOOR_BELL);
	/* Make sure that doorbell is committed immediately */
	wmb();

	spin_unlock_irqrestore(host->host_lock, flags);

	ufshcd_add_tm_upiu_trace(hba, task_tag, UFS_TM_SEND);

	/* wait until the task management command is completed */
	err = wait_for_completion_io_timeout(&wait,
			msecs_to_jiffies(TM_CMD_TIMEOUT));
	if (!err) {
		ufshcd_add_tm_upiu_trace(hba, task_tag, UFS_TM_ERR);
		dev_err(hba->dev, "%s: task management cmd 0x%.2x timed-out\n",
				__func__, tm_function);
		if (ufshcd_clear_tm_cmd(hba, task_tag))
			dev_WARN(hba->dev, "%s: unable to clear tm cmd (slot %d) after timeout\n",
					__func__, task_tag);
		err = -ETIMEDOUT;
	} else {
		err = 0;
		memcpy(treq, hba->utmrdl_base_addr + task_tag, sizeof(*treq));

		ufshcd_add_tm_upiu_trace(hba, task_tag, UFS_TM_COMP);
	}

	spin_lock_irqsave(hba->host->host_lock, flags);
	hba->tmf_rqs[req->tag] = NULL;
	__clear_bit(task_tag, &hba->outstanding_tasks);
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	ufshcd_release(hba);
	blk_put_request(req);

	return err;
}

/**
 * ufshcd_issue_tm_cmd - issues task management commands to controller
 * @hba: per adapter instance
 * @lun_id: LUN ID to which TM command is sent
 * @task_id: task ID to which the TM command is applicable
 * @tm_function: task management function opcode
 * @tm_response: task management service response return value
 *
 * Returns non-zero value on error, zero on success.
 */
static int ufshcd_issue_tm_cmd(struct ufs_hba *hba, int lun_id, int task_id,
		u8 tm_function, u8 *tm_response)
{
	struct utp_task_req_desc treq = { { 0 }, };
	int ocs_value, err;

	/* Configure task request descriptor */
	treq.header.dword_0 = cpu_to_le32(UTP_REQ_DESC_INT_CMD);
	treq.header.dword_2 = cpu_to_le32(OCS_INVALID_COMMAND_STATUS);

	/* Configure task request UPIU */
	treq.upiu_req.req_header.dword_0 = cpu_to_be32(lun_id << 8) |
				  cpu_to_be32(UPIU_TRANSACTION_TASK_REQ << 24);
	treq.upiu_req.req_header.dword_1 = cpu_to_be32(tm_function << 16);

	/*
	 * The host shall provide the same value for LUN field in the basic
	 * header and for Input Parameter.
	 */
	treq.upiu_req.input_param1 = cpu_to_be32(lun_id);
	treq.upiu_req.input_param2 = cpu_to_be32(task_id);

	err = __ufshcd_issue_tm_cmd(hba, &treq, tm_function);
	if (err == -ETIMEDOUT)
		return err;

	ocs_value = le32_to_cpu(treq.header.dword_2) & MASK_OCS;
	if (ocs_value != OCS_SUCCESS)
		dev_err(hba->dev, "%s: failed, ocs = 0x%x\n",
				__func__, ocs_value);
	else if (tm_response)
		*tm_response = be32_to_cpu(treq.upiu_rsp.output_param1) &
				MASK_TM_SERVICE_RESP;
	return err;
}

/**
 * ufshcd_issue_devman_upiu_cmd - API for sending "utrd" type requests
 * @hba:	per-adapter instance
 * @req_upiu:	upiu request
 * @rsp_upiu:	upiu reply
 * @desc_buff:	pointer to descriptor buffer, NULL if NA
 * @buff_len:	descriptor size, 0 if NA
 * @cmd_type:	specifies the type (NOP, Query...)
 * @desc_op:	descriptor operation
 *
 * Those type of requests uses UTP Transfer Request Descriptor - utrd.
 * Therefore, it "rides" the device management infrastructure: uses its tag and
 * tasks work queues.
 *
 * Since there is only one available tag for device management commands,
 * the caller is expected to hold the hba->dev_cmd.lock mutex.
 */
static int ufshcd_issue_devman_upiu_cmd(struct ufs_hba *hba,
					struct utp_upiu_req *req_upiu,
					struct utp_upiu_req *rsp_upiu,
					u8 *desc_buff, int *buff_len,
					enum dev_cmd_type cmd_type,
					enum query_opcode desc_op)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	const u32 tag = hba->reserved_slot;
	struct ufshcd_lrb *lrbp;
	int err = 0;
	u8 upiu_flags;

	/* Protects use of hba->reserved_slot. */
	lockdep_assert_held(&hba->dev_cmd.lock);

	down_read(&hba->clk_scaling_lock);

	lrbp = &hba->lrb[tag];
	WARN_ON(lrbp->cmd);
	lrbp->cmd = NULL;
	lrbp->sense_bufflen = 0;
	lrbp->sense_buffer = NULL;
	lrbp->task_tag = tag;
	lrbp->lun = 0;
	lrbp->intr_cmd = true;
	ufshcd_prepare_lrbp_crypto(NULL, lrbp);
	hba->dev_cmd.type = cmd_type;

	if (hba->ufs_version <= ufshci_version(1, 1))
		lrbp->command_type = UTP_CMD_TYPE_DEV_MANAGE;
	else
		lrbp->command_type = UTP_CMD_TYPE_UFS_STORAGE;

	/* update the task tag in the request upiu */
	req_upiu->header.dword_0 |= cpu_to_be32(tag);

	ufshcd_prepare_req_desc_hdr(lrbp, &upiu_flags, DMA_NONE);

	/* just copy the upiu request as it is */
	memcpy(lrbp->ucd_req_ptr, req_upiu, sizeof(*lrbp->ucd_req_ptr));
	if (desc_buff && desc_op == UPIU_QUERY_OPCODE_WRITE_DESC) {
		/* The Data Segment Area is optional depending upon the query
		 * function value. for WRITE DESCRIPTOR, the data segment
		 * follows right after the tsf.
		 */
		memcpy(lrbp->ucd_req_ptr + 1, desc_buff, *buff_len);
		*buff_len = 0;
	}

	memset(lrbp->ucd_rsp_ptr, 0, sizeof(struct utp_upiu_rsp));

	hba->dev_cmd.complete = &wait;

	ufshcd_add_query_upiu_trace(hba, UFS_QUERY_SEND, lrbp->ucd_req_ptr);

	ufshcd_send_command(hba, tag);
	/*
	 * ignore the returning value here - ufshcd_check_query_response is
	 * bound to fail since dev_cmd.query and dev_cmd.type were left empty.
	 * read the response directly ignoring all errors.
	 */
	ufshcd_wait_for_dev_cmd(hba, lrbp, QUERY_REQ_TIMEOUT);

	/* just copy the upiu response as it is */
	memcpy(rsp_upiu, lrbp->ucd_rsp_ptr, sizeof(*rsp_upiu));
	if (desc_buff && desc_op == UPIU_QUERY_OPCODE_READ_DESC) {
		u8 *descp = (u8 *)lrbp->ucd_rsp_ptr + sizeof(*rsp_upiu);
		u16 resp_len = be32_to_cpu(lrbp->ucd_rsp_ptr->header.dword_2) &
			       MASK_QUERY_DATA_SEG_LEN;

		if (*buff_len >= resp_len) {
			memcpy(desc_buff, descp, resp_len);
			*buff_len = resp_len;
		} else {
			dev_warn(hba->dev,
				 "%s: rsp size %d is bigger than buffer size %d",
				 __func__, resp_len, *buff_len);
			*buff_len = 0;
			err = -EINVAL;
		}
	}
	ufshcd_add_query_upiu_trace(hba, err ? UFS_QUERY_ERR : UFS_QUERY_COMP,
				    (struct utp_upiu_req *)lrbp->ucd_rsp_ptr);

	up_read(&hba->clk_scaling_lock);
	return err;
}

/**
 * ufshcd_exec_raw_upiu_cmd - API function for sending raw upiu commands
 * @hba:	per-adapter instance
 * @req_upiu:	upiu request
 * @rsp_upiu:	upiu reply - only 8 DW as we do not support scsi commands
 * @msgcode:	message code, one of UPIU Transaction Codes Initiator to Target
 * @desc_buff:	pointer to descriptor buffer, NULL if NA
 * @buff_len:	descriptor size, 0 if NA
 * @desc_op:	descriptor operation
 *
 * Supports UTP Transfer requests (nop and query), and UTP Task
 * Management requests.
 * It is up to the caller to fill the upiu conent properly, as it will
 * be copied without any further input validations.
 */
int ufshcd_exec_raw_upiu_cmd(struct ufs_hba *hba,
			     struct utp_upiu_req *req_upiu,
			     struct utp_upiu_req *rsp_upiu,
			     int msgcode,
			     u8 *desc_buff, int *buff_len,
			     enum query_opcode desc_op)
{
	int err;
	enum dev_cmd_type cmd_type = DEV_CMD_TYPE_QUERY;
	struct utp_task_req_desc treq = { { 0 }, };
	int ocs_value;
	u8 tm_f = be32_to_cpu(req_upiu->header.dword_1) >> 16 & MASK_TM_FUNC;

	switch (msgcode) {
	case UPIU_TRANSACTION_NOP_OUT:
		cmd_type = DEV_CMD_TYPE_NOP;
		fallthrough;
	case UPIU_TRANSACTION_QUERY_REQ:
		ufshcd_hold(hba, false);
		mutex_lock(&hba->dev_cmd.lock);
		err = ufshcd_issue_devman_upiu_cmd(hba, req_upiu, rsp_upiu,
						   desc_buff, buff_len,
						   cmd_type, desc_op);
		mutex_unlock(&hba->dev_cmd.lock);
		ufshcd_release(hba);

		break;
	case UPIU_TRANSACTION_TASK_REQ:
		treq.header.dword_0 = cpu_to_le32(UTP_REQ_DESC_INT_CMD);
		treq.header.dword_2 = cpu_to_le32(OCS_INVALID_COMMAND_STATUS);

		memcpy(&treq.upiu_req, req_upiu, sizeof(*req_upiu));

		err = __ufshcd_issue_tm_cmd(hba, &treq, tm_f);
		if (err == -ETIMEDOUT)
			break;

		ocs_value = le32_to_cpu(treq.header.dword_2) & MASK_OCS;
		if (ocs_value != OCS_SUCCESS) {
			dev_err(hba->dev, "%s: failed, ocs = 0x%x\n", __func__,
				ocs_value);
			break;
		}

		memcpy(rsp_upiu, &treq.upiu_rsp, sizeof(*rsp_upiu));

		break;
	default:
		err = -EINVAL;

		break;
	}

	return err;
}

/**
 * ufshcd_eh_device_reset_handler - device reset handler registered to
 *                                    scsi layer.
 * @cmd: SCSI command pointer
 *
 * Returns SUCCESS/FAILED
 */
static int ufshcd_eh_device_reset_handler(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *host;
	struct ufs_hba *hba;
	u32 pos;
	int err;
	u8 resp = 0xF, lun;

	host = cmd->device->host;
	hba = shost_priv(host);

	lun = ufshcd_scsi_to_upiu_lun(cmd->device->lun);
	err = ufshcd_issue_tm_cmd(hba, lun, 0, UFS_LOGICAL_RESET, &resp);
	if (err || resp != UPIU_TASK_MANAGEMENT_FUNC_COMPL) {
		if (!err)
			err = resp;
		goto out;
	}

	/* clear the commands that were pending for corresponding LUN */
	for_each_set_bit(pos, &hba->outstanding_reqs, hba->nutrs) {
		if (hba->lrb[pos].lun == lun) {
			err = ufshcd_clear_cmd(hba, pos);
			if (err)
				break;
			__ufshcd_transfer_req_compl(hba, 1U << pos, false);
		}
	}

out:
	hba->req_abort_count = 0;
	ufshcd_update_evt_hist(hba, UFS_EVT_DEV_RESET, (u32)err);
	if (!err) {
		err = SUCCESS;
	} else {
		dev_err(hba->dev, "%s: failed with err %d\n", __func__, err);
		err = FAILED;
	}
	return err;
}

static void ufshcd_set_req_abort_skip(struct ufs_hba *hba, unsigned long bitmap)
{
	struct ufshcd_lrb *lrbp;
	int tag;

	for_each_set_bit(tag, &bitmap, hba->nutrs) {
		lrbp = &hba->lrb[tag];
		lrbp->req_abort_skip = true;
	}
}

/**
 * ufshcd_try_to_abort_task - abort a specific task
 * @hba: Pointer to adapter instance
 * @tag: Task tag/index to be aborted
 *
 * Abort the pending command in device by sending UFS_ABORT_TASK task management
 * command, and in host controller by clearing the door-bell register. There can
 * be race between controller sending the command to the device while abort is
 * issued. To avoid that, first issue UFS_QUERY_TASK to check if the command is
 * really issued and then try to abort it.
 *
 * Returns zero on success, non-zero on failure
 */
static int ufshcd_try_to_abort_task(struct ufs_hba *hba, int tag)
{
	struct ufshcd_lrb *lrbp = &hba->lrb[tag];
	int err = 0;
	int poll_cnt;
	u8 resp = 0xF;
	u32 reg;

	for (poll_cnt = 100; poll_cnt; poll_cnt--) {
		err = ufshcd_issue_tm_cmd(hba, lrbp->lun, lrbp->task_tag,
				UFS_QUERY_TASK, &resp);
		if (!err && resp == UPIU_TASK_MANAGEMENT_FUNC_SUCCEEDED) {
			/* cmd pending in the device */
			dev_err(hba->dev, "%s: cmd pending in the device. tag = %d\n",
				__func__, tag);
			break;
		} else if (!err && resp == UPIU_TASK_MANAGEMENT_FUNC_COMPL) {
			/*
			 * cmd not pending in the device, check if it is
			 * in transition.
			 */
			dev_err(hba->dev, "%s: cmd at tag %d not pending in the device.\n",
				__func__, tag);
			reg = ufshcd_readl(hba, REG_UTP_TRANSFER_REQ_DOOR_BELL);
			if (reg & (1 << tag)) {
				/* sleep for max. 200us to stabilize */
				usleep_range(100, 200);
				continue;
			}
			/* command completed already */
			dev_err(hba->dev, "%s: cmd at tag %d successfully cleared from DB.\n",
				__func__, tag);
			goto out;
		} else {
			dev_err(hba->dev,
				"%s: no response from device. tag = %d, err %d\n",
				__func__, tag, err);
			if (!err)
				err = resp; /* service response error */
			goto out;
		}
	}

	if (!poll_cnt) {
		err = -EBUSY;
		goto out;
	}

	err = ufshcd_issue_tm_cmd(hba, lrbp->lun, lrbp->task_tag,
			UFS_ABORT_TASK, &resp);
	if (err || resp != UPIU_TASK_MANAGEMENT_FUNC_COMPL) {
		if (!err) {
			err = resp; /* service response error */
			dev_err(hba->dev, "%s: issued. tag = %d, err %d\n",
				__func__, tag, err);
		}
		goto out;
	}

	err = ufshcd_clear_cmd(hba, tag);
	if (err)
		dev_err(hba->dev, "%s: Failed clearing cmd at tag %d, err %d\n",
			__func__, tag, err);

out:
	return err;
}

/**
 * ufshcd_abort - scsi host template eh_abort_handler callback
 * @cmd: SCSI command pointer
 *
 * Returns SUCCESS/FAILED
 */
static int ufshcd_abort(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *host = cmd->device->host;
	struct ufs_hba *hba = shost_priv(host);
	int tag = scsi_cmd_to_rq(cmd)->tag;
	struct ufshcd_lrb *lrbp = &hba->lrb[tag];
	unsigned long flags;
	int err = FAILED;
	u32 reg;

	WARN_ONCE(tag < 0, "Invalid tag %d\n", tag);

	ufshcd_hold(hba, false);
	reg = ufshcd_readl(hba, REG_UTP_TRANSFER_REQ_DOOR_BELL);
	/* If command is already aborted/completed, return FAILED. */
	if (!(test_bit(tag, &hba->outstanding_reqs))) {
		dev_err(hba->dev,
			"%s: cmd at tag %d already completed, outstanding=0x%lx, doorbell=0x%x\n",
			__func__, tag, hba->outstanding_reqs, reg);
		goto release;
	}

	/* Print Transfer Request of aborted task */
	dev_info(hba->dev, "%s: Device abort task at tag %d\n", __func__, tag);

	/*
	 * Print detailed info about aborted request.
	 * As more than one request might get aborted at the same time,
	 * print full information only for the first aborted request in order
	 * to reduce repeated printouts. For other aborted requests only print
	 * basic details.
	 */
	scsi_print_command(cmd);
	if (!hba->req_abort_count) {
		ufshcd_update_evt_hist(hba, UFS_EVT_ABORT, tag);
		ufshcd_print_evt_hist(hba);
		ufshcd_print_host_state(hba);
		ufshcd_print_pwr_info(hba);
		ufshcd_print_trs(hba, 1 << tag, true);
	} else {
		ufshcd_print_trs(hba, 1 << tag, false);
	}
	hba->req_abort_count++;

	if (!(reg & (1 << tag))) {
		dev_err(hba->dev,
		"%s: cmd was completed, but without a notifying intr, tag = %d",
		__func__, tag);
		__ufshcd_transfer_req_compl(hba, 1UL << tag, /*retry_requests=*/false);
		goto release;
	}

	/*
	 * Task abort to the device W-LUN is illegal. When this command
	 * will fail, due to spec violation, scsi err handling next step
	 * will be to send LU reset which, again, is a spec violation.
	 * To avoid these unnecessary/illegal steps, first we clean up
	 * the lrb taken by this cmd and re-set it in outstanding_reqs,
	 * then queue the eh_work and bail.
	 */
	if (lrbp->lun == UFS_UPIU_UFS_DEVICE_WLUN) {
		ufshcd_update_evt_hist(hba, UFS_EVT_ABORT, lrbp->lun);

		spin_lock_irqsave(host->host_lock, flags);
		hba->force_reset = true;
		ufshcd_schedule_eh_work(hba);
		spin_unlock_irqrestore(host->host_lock, flags);
		goto release;
	}

	/* Skip task abort in case previous aborts failed and report failure */
	if (lrbp->req_abort_skip) {
		dev_err(hba->dev, "%s: skipping abort\n", __func__);
		ufshcd_set_req_abort_skip(hba, hba->outstanding_reqs);
		goto release;
	}

	err = ufshcd_try_to_abort_task(hba, tag);
	if (err) {
		dev_err(hba->dev, "%s: failed with err %d\n", __func__, err);
		ufshcd_set_req_abort_skip(hba, hba->outstanding_reqs);
		err = FAILED;
		goto release;
	}

	lrbp->cmd = NULL;
	err = SUCCESS;

release:
	/* Matches the ufshcd_hold() call at the start of this function. */
	ufshcd_release(hba);
	return err;
}

/**
 * ufshcd_host_reset_and_restore - reset and restore host controller
 * @hba: per-adapter instance
 *
 * Note that host controller reset may issue DME_RESET to
 * local and remote (device) Uni-Pro stack and the attributes
 * are reset to default state.
 *
 * Returns zero on success, non-zero on failure
 */
static int ufshcd_host_reset_and_restore(struct ufs_hba *hba)
{
	int err;

	/*
	 * Stop the host controller and complete the requests
	 * cleared by h/w
	 */
	ufshpb_reset_host(hba);
	ufshcd_hba_stop(hba);
	hba->silence_err_logs = true;
	ufshcd_retry_aborted_requests(hba);
	hba->silence_err_logs = false;

	/* scale up clocks to max frequency before full reinitialization */
	ufshcd_set_clk_freq(hba, true);

	err = ufshcd_hba_enable(hba);

	/* Establish the link again and restore the device */
	if (!err)
		err = ufshcd_probe_hba(hba, false);

	if (err)
		dev_err(hba->dev, "%s: Host init failed %d\n", __func__, err);
	ufshcd_update_evt_hist(hba, UFS_EVT_HOST_RESET, (u32)err);
	return err;
}

/**
 * ufshcd_reset_and_restore - reset and re-initialize host/device
 * @hba: per-adapter instance
 *
 * Reset and recover device, host and re-establish link. This
 * is helpful to recover the communication in fatal error conditions.
 *
 * Returns zero on success, non-zero on failure
 */
static int ufshcd_reset_and_restore(struct ufs_hba *hba)
{
	u32 saved_err;
	u32 saved_uic_err;
	int err = 0;
	unsigned long flags;
	int retries = MAX_HOST_RESET_RETRIES;

	/*
	 * This is a fresh start, cache and clear saved error first,
	 * in case new error generated during reset and restore.
	 */
	spin_lock_irqsave(hba->host->host_lock, flags);
	saved_err = hba->saved_err;
	saved_uic_err = hba->saved_uic_err;
	hba->saved_err = 0;
	hba->saved_uic_err = 0;
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	do {
		/* Reset the attached device */
		ufshcd_device_reset(hba);

		err = ufshcd_host_reset_and_restore(hba);
	} while (err && --retries);

	spin_lock_irqsave(hba->host->host_lock, flags);
	/*
	 * Inform scsi mid-layer that we did reset and allow to handle
	 * Unit Attention properly.
	 */
	scsi_report_bus_reset(hba->host, 0);
	if (err) {
		hba->ufshcd_state = UFSHCD_STATE_ERROR;
		hba->saved_err |= saved_err;
		hba->saved_uic_err |= saved_uic_err;
	}
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	return err;
}

/**
 * ufshcd_eh_host_reset_handler - host reset handler registered to scsi layer
 * @cmd: SCSI command pointer
 *
 * Returns SUCCESS/FAILED
 */
static int ufshcd_eh_host_reset_handler(struct scsi_cmnd *cmd)
{
	int err = SUCCESS;
	unsigned long flags;
	struct ufs_hba *hba;

	hba = shost_priv(cmd->device->host);

	spin_lock_irqsave(hba->host->host_lock, flags);
	hba->force_reset = true;
	ufshcd_schedule_eh_work(hba);
	dev_err(hba->dev, "%s: reset in progress - 1\n", __func__);
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	flush_work(&hba->eh_work);

	spin_lock_irqsave(hba->host->host_lock, flags);
	if (hba->ufshcd_state == UFSHCD_STATE_ERROR)
		err = FAILED;
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	return err;
}

/**
 * ufshcd_get_max_icc_level - calculate the ICC level
 * @sup_curr_uA: max. current supported by the regulator
 * @start_scan: row at the desc table to start scan from
 * @buff: power descriptor buffer
 *
 * Returns calculated max ICC level for specific regulator
 */
static u32 ufshcd_get_max_icc_level(int sup_curr_uA, u32 start_scan, char *buff)
{
	int i;
	int curr_uA;
	u16 data;
	u16 unit;

	for (i = start_scan; i >= 0; i--) {
		data = be16_to_cpup((__be16 *)&buff[2 * i]);
		unit = (data & ATTR_ICC_LVL_UNIT_MASK) >>
						ATTR_ICC_LVL_UNIT_OFFSET;
		curr_uA = data & ATTR_ICC_LVL_VALUE_MASK;
		switch (unit) {
		case UFSHCD_NANO_AMP:
			curr_uA = curr_uA / 1000;
			break;
		case UFSHCD_MILI_AMP:
			curr_uA = curr_uA * 1000;
			break;
		case UFSHCD_AMP:
			curr_uA = curr_uA * 1000 * 1000;
			break;
		case UFSHCD_MICRO_AMP:
		default:
			break;
		}
		if (sup_curr_uA >= curr_uA)
			break;
	}
	if (i < 0) {
		i = 0;
		pr_err("%s: Couldn't find valid icc_level = %d", __func__, i);
	}

	return (u32)i;
}

/**
 * ufshcd_find_max_sup_active_icc_level - calculate the max ICC level
 * In case regulators are not initialized we'll return 0
 * @hba: per-adapter instance
 * @desc_buf: power descriptor buffer to extract ICC levels from.
 * @len: length of desc_buff
 *
 * Returns calculated ICC level
 */
static u32 ufshcd_find_max_sup_active_icc_level(struct ufs_hba *hba,
							u8 *desc_buf, int len)
{
	u32 icc_level = 0;

	if (!hba->vreg_info.vcc || !hba->vreg_info.vccq ||
						!hba->vreg_info.vccq2) {
		/*
		 * Using dev_dbg to avoid messages during runtime PM to avoid
		 * never-ending cycles of messages written back to storage by
		 * user space causing runtime resume, causing more messages and
		 * so on.
		 */
		dev_dbg(hba->dev,
			"%s: Regulator capability was not set, actvIccLevel=%d",
							__func__, icc_level);
		goto out;
	}

	if (hba->vreg_info.vcc->max_uA)
		icc_level = ufshcd_get_max_icc_level(
				hba->vreg_info.vcc->max_uA,
				POWER_DESC_MAX_ACTV_ICC_LVLS - 1,
				&desc_buf[PWR_DESC_ACTIVE_LVLS_VCC_0]);

	if (hba->vreg_info.vccq->max_uA)
		icc_level = ufshcd_get_max_icc_level(
				hba->vreg_info.vccq->max_uA,
				icc_level,
				&desc_buf[PWR_DESC_ACTIVE_LVLS_VCCQ_0]);

	if (hba->vreg_info.vccq2->max_uA)
		icc_level = ufshcd_get_max_icc_level(
				hba->vreg_info.vccq2->max_uA,
				icc_level,
				&desc_buf[PWR_DESC_ACTIVE_LVLS_VCCQ2_0]);
out:
	return icc_level;
}

static void ufshcd_set_active_icc_lvl(struct ufs_hba *hba)
{
	int ret;
	int buff_len = hba->desc_size[QUERY_DESC_IDN_POWER];
	u8 *desc_buf;
	u32 icc_level;

	desc_buf = kmalloc(buff_len, GFP_KERNEL);
	if (!desc_buf)
		return;

	ret = ufshcd_read_desc_param(hba, QUERY_DESC_IDN_POWER, 0, 0,
				     desc_buf, buff_len);
	if (ret) {
		dev_err(hba->dev,
			"%s: Failed reading power descriptor.len = %d ret = %d",
			__func__, buff_len, ret);
		goto out;
	}

	icc_level = ufshcd_find_max_sup_active_icc_level(hba, desc_buf,
							 buff_len);
	dev_dbg(hba->dev, "%s: setting icc_level 0x%x", __func__, icc_level);

	ret = ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_WRITE_ATTR,
		QUERY_ATTR_IDN_ACTIVE_ICC_LVL, 0, 0, &icc_level);

	if (ret)
		dev_err(hba->dev,
			"%s: Failed configuring bActiveICCLevel = %d ret = %d",
			__func__, icc_level, ret);

out:
	kfree(desc_buf);
}

static inline void ufshcd_blk_pm_runtime_init(struct scsi_device *sdev)
{
	scsi_autopm_get_device(sdev);
	blk_pm_runtime_init(sdev->request_queue, &sdev->sdev_gendev);
	if (sdev->rpm_autosuspend)
		pm_runtime_set_autosuspend_delay(&sdev->sdev_gendev,
						 RPM_AUTOSUSPEND_DELAY_MS);
	scsi_autopm_put_device(sdev);
}

/**
 * ufshcd_scsi_add_wlus - Adds required W-LUs
 * @hba: per-adapter instance
 *
 * UFS device specification requires the UFS devices to support 4 well known
 * logical units:
 *	"REPORT_LUNS" (address: 01h)
 *	"UFS Device" (address: 50h)
 *	"RPMB" (address: 44h)
 *	"BOOT" (address: 30h)
 * UFS device's power management needs to be controlled by "POWER CONDITION"
 * field of SSU (START STOP UNIT) command. But this "power condition" field
 * will take effect only when its sent to "UFS device" well known logical unit
 * hence we require the scsi_device instance to represent this logical unit in
 * order for the UFS host driver to send the SSU command for power management.
 *
 * We also require the scsi_device instance for "RPMB" (Replay Protected Memory
 * Block) LU so user space process can control this LU. User space may also
 * want to have access to BOOT LU.
 *
 * This function adds scsi device instances for each of all well known LUs
 * (except "REPORT LUNS" LU).
 *
 * Returns zero on success (all required W-LUs are added successfully),
 * non-zero error value on failure (if failed to add any of the required W-LU).
 */
static int ufshcd_scsi_add_wlus(struct ufs_hba *hba)
{
	int ret = 0;
	struct scsi_device *sdev_boot;

	hba->sdev_ufs_device = __scsi_add_device(hba->host, 0, 0,
		ufshcd_upiu_wlun_to_scsi_wlun(UFS_UPIU_UFS_DEVICE_WLUN), NULL);
	if (IS_ERR(hba->sdev_ufs_device)) {
		ret = PTR_ERR(hba->sdev_ufs_device);
		hba->sdev_ufs_device = NULL;
		goto out;
	}
	scsi_device_put(hba->sdev_ufs_device);

	hba->sdev_rpmb = __scsi_add_device(hba->host, 0, 0,
		ufshcd_upiu_wlun_to_scsi_wlun(UFS_UPIU_RPMB_WLUN), NULL);
	if (IS_ERR(hba->sdev_rpmb)) {
		ret = PTR_ERR(hba->sdev_rpmb);
		goto remove_sdev_ufs_device;
	}
	ufshcd_blk_pm_runtime_init(hba->sdev_rpmb);
	scsi_device_put(hba->sdev_rpmb);

	sdev_boot = __scsi_add_device(hba->host, 0, 0,
		ufshcd_upiu_wlun_to_scsi_wlun(UFS_UPIU_BOOT_WLUN), NULL);
	if (IS_ERR(sdev_boot)) {
		dev_err(hba->dev, "%s: BOOT WLUN not found\n", __func__);
	} else {
		ufshcd_blk_pm_runtime_init(sdev_boot);
		scsi_device_put(sdev_boot);
	}
	goto out;

remove_sdev_ufs_device:
	scsi_remove_device(hba->sdev_ufs_device);
out:
	return ret;
}

static void ufshcd_wb_probe(struct ufs_hba *hba, u8 *desc_buf)
{
	struct ufs_dev_info *dev_info = &hba->dev_info;
	u8 lun;
	u32 d_lu_wb_buf_alloc;
	u32 ext_ufs_feature;

	if (!ufshcd_is_wb_allowed(hba))
		return;
	/*
	 * Probe WB only for UFS-2.2 and UFS-3.1 (and later) devices or
	 * UFS devices with quirk UFS_DEVICE_QUIRK_SUPPORT_EXTENDED_FEATURES
	 * enabled
	 */
	if (!(dev_info->wspecversion >= 0x310 ||
	      dev_info->wspecversion == 0x220 ||
	     (hba->dev_quirks & UFS_DEVICE_QUIRK_SUPPORT_EXTENDED_FEATURES)))
		goto wb_disabled;

	if (hba->desc_size[QUERY_DESC_IDN_DEVICE] <
	    DEVICE_DESC_PARAM_EXT_UFS_FEATURE_SUP + 4)
		goto wb_disabled;

	ext_ufs_feature = get_unaligned_be32(desc_buf +
					DEVICE_DESC_PARAM_EXT_UFS_FEATURE_SUP);

	if (!(ext_ufs_feature & UFS_DEV_WRITE_BOOSTER_SUP))
		goto wb_disabled;

	/*
	 * WB may be supported but not configured while provisioning. The spec
	 * says, in dedicated wb buffer mode, a max of 1 lun would have wb
	 * buffer configured.
	 */
	dev_info->wb_buffer_type = desc_buf[DEVICE_DESC_PARAM_WB_TYPE];

	dev_info->b_presrv_uspc_en =
		desc_buf[DEVICE_DESC_PARAM_WB_PRESRV_USRSPC_EN];

	if (dev_info->wb_buffer_type == WB_BUF_MODE_SHARED) {
		if (!get_unaligned_be32(desc_buf +
				   DEVICE_DESC_PARAM_WB_SHARED_ALLOC_UNITS))
			goto wb_disabled;
	} else {
		for (lun = 0; lun < UFS_UPIU_MAX_WB_LUN_ID; lun++) {
			d_lu_wb_buf_alloc = 0;
			ufshcd_read_unit_desc_param(hba,
					lun,
					UNIT_DESC_PARAM_WB_BUF_ALLOC_UNITS,
					(u8 *)&d_lu_wb_buf_alloc,
					sizeof(d_lu_wb_buf_alloc));
			if (d_lu_wb_buf_alloc) {
				dev_info->wb_dedicated_lu = lun;
				break;
			}
		}

		if (!d_lu_wb_buf_alloc)
			goto wb_disabled;
	}
	return;

wb_disabled:
	hba->caps &= ~UFSHCD_CAP_WB_EN;
}

void ufshcd_fixup_dev_quirks(struct ufs_hba *hba, struct ufs_dev_fix *fixups)
{
	struct ufs_dev_fix *f;
	struct ufs_dev_info *dev_info = &hba->dev_info;

	if (!fixups)
		return;

	for (f = fixups; f->quirk; f++) {
		if ((f->wmanufacturerid == dev_info->wmanufacturerid ||
		     f->wmanufacturerid == UFS_ANY_VENDOR) &&
		     ((dev_info->model &&
		       STR_PRFX_EQUAL(f->model, dev_info->model)) ||
		      !strcmp(f->model, UFS_ANY_MODEL)))
			hba->dev_quirks |= f->quirk;
	}
}
EXPORT_SYMBOL_GPL(ufshcd_fixup_dev_quirks);

static void ufs_fixup_device_setup(struct ufs_hba *hba)
{
	/* fix by general quirk table */
	ufshcd_fixup_dev_quirks(hba, ufs_fixups);

	/* allow vendors to fix quirks */
	ufshcd_vops_fixup_dev_quirks(hba);
}

static int ufs_get_device_desc(struct ufs_hba *hba)
{
	int err;
	u8 model_index;
	u8 b_ufs_feature_sup;
	u8 *desc_buf;
	struct ufs_dev_info *dev_info = &hba->dev_info;

	desc_buf = kmalloc(QUERY_DESC_MAX_SIZE, GFP_KERNEL);
	if (!desc_buf) {
		err = -ENOMEM;
		goto out;
	}

	err = ufshcd_read_desc_param(hba, QUERY_DESC_IDN_DEVICE, 0, 0, desc_buf,
				     hba->desc_size[QUERY_DESC_IDN_DEVICE]);
	if (err) {
		dev_err(hba->dev, "%s: Failed reading Device Desc. err = %d\n",
			__func__, err);
		goto out;
	}

	/*
	 * getting vendor (manufacturerID) and Bank Index in big endian
	 * format
	 */
	dev_info->wmanufacturerid = desc_buf[DEVICE_DESC_PARAM_MANF_ID] << 8 |
				     desc_buf[DEVICE_DESC_PARAM_MANF_ID + 1];

	/* getting Specification Version in big endian format */
	dev_info->wspecversion = desc_buf[DEVICE_DESC_PARAM_SPEC_VER] << 8 |
				      desc_buf[DEVICE_DESC_PARAM_SPEC_VER + 1];
	b_ufs_feature_sup = desc_buf[DEVICE_DESC_PARAM_UFS_FEAT];

	model_index = desc_buf[DEVICE_DESC_PARAM_PRDCT_NAME];

	if (dev_info->wspecversion >= UFS_DEV_HPB_SUPPORT_VERSION &&
	    (b_ufs_feature_sup & UFS_DEV_HPB_SUPPORT)) {
		bool hpb_en = false;

		ufshpb_get_dev_info(hba, desc_buf);

		if (!ufshpb_is_legacy(hba))
			err = ufshcd_query_flag_retry(hba,
						      UPIU_QUERY_OPCODE_READ_FLAG,
						      QUERY_FLAG_IDN_HPB_EN, 0,
						      &hpb_en);

		if (ufshpb_is_legacy(hba) || (!err && hpb_en))
			dev_info->hpb_enabled = true;
	}

	err = ufshcd_read_string_desc(hba, model_index,
				      &dev_info->model, SD_ASCII_STD);
	if (err < 0) {
		dev_err(hba->dev, "%s: Failed reading Product Name. err = %d\n",
			__func__, err);
		goto out;
	}

	hba->luns_avail = desc_buf[DEVICE_DESC_PARAM_NUM_LU] +
		desc_buf[DEVICE_DESC_PARAM_NUM_WLU];

	ufs_fixup_device_setup(hba);

	ufshcd_wb_probe(hba, desc_buf);

	/*
	 * ufshcd_read_string_desc returns size of the string
	 * reset the error value
	 */
	err = 0;

out:
	kfree(desc_buf);
	return err;
}

static void ufs_put_device_desc(struct ufs_hba *hba)
{
	struct ufs_dev_info *dev_info = &hba->dev_info;

	kfree(dev_info->model);
	dev_info->model = NULL;
}

/**
 * ufshcd_tune_pa_tactivate - Tunes PA_TActivate of local UniPro
 * @hba: per-adapter instance
 *
 * PA_TActivate parameter can be tuned manually if UniPro version is less than
 * 1.61. PA_TActivate needs to be greater than or equal to peerM-PHY's
 * RX_MIN_ACTIVATETIME_CAPABILITY attribute. This optimal value can help reduce
 * the hibern8 exit latency.
 *
 * Returns zero on success, non-zero error value on failure.
 */
static int ufshcd_tune_pa_tactivate(struct ufs_hba *hba)
{
	int ret = 0;
	u32 peer_rx_min_activatetime = 0, tuned_pa_tactivate;

	ret = ufshcd_dme_peer_get(hba,
				  UIC_ARG_MIB_SEL(
					RX_MIN_ACTIVATETIME_CAPABILITY,
					UIC_ARG_MPHY_RX_GEN_SEL_INDEX(0)),
				  &peer_rx_min_activatetime);
	if (ret)
		goto out;

	/* make sure proper unit conversion is applied */
	tuned_pa_tactivate =
		((peer_rx_min_activatetime * RX_MIN_ACTIVATETIME_UNIT_US)
		 / PA_TACTIVATE_TIME_UNIT_US);
	ret = ufshcd_dme_set(hba, UIC_ARG_MIB(PA_TACTIVATE),
			     tuned_pa_tactivate);

out:
	return ret;
}

/**
 * ufshcd_tune_pa_hibern8time - Tunes PA_Hibern8Time of local UniPro
 * @hba: per-adapter instance
 *
 * PA_Hibern8Time parameter can be tuned manually if UniPro version is less than
 * 1.61. PA_Hibern8Time needs to be maximum of local M-PHY's
 * TX_HIBERN8TIME_CAPABILITY & peer M-PHY's RX_HIBERN8TIME_CAPABILITY.
 * This optimal value can help reduce the hibern8 exit latency.
 *
 * Returns zero on success, non-zero error value on failure.
 */
static int ufshcd_tune_pa_hibern8time(struct ufs_hba *hba)
{
	int ret = 0;
	u32 local_tx_hibern8_time_cap = 0, peer_rx_hibern8_time_cap = 0;
	u32 max_hibern8_time, tuned_pa_hibern8time;

	ret = ufshcd_dme_get(hba,
			     UIC_ARG_MIB_SEL(TX_HIBERN8TIME_CAPABILITY,
					UIC_ARG_MPHY_TX_GEN_SEL_INDEX(0)),
				  &local_tx_hibern8_time_cap);
	if (ret)
		goto out;

	ret = ufshcd_dme_peer_get(hba,
				  UIC_ARG_MIB_SEL(RX_HIBERN8TIME_CAPABILITY,
					UIC_ARG_MPHY_RX_GEN_SEL_INDEX(0)),
				  &peer_rx_hibern8_time_cap);
	if (ret)
		goto out;

	max_hibern8_time = max(local_tx_hibern8_time_cap,
			       peer_rx_hibern8_time_cap);
	/* make sure proper unit conversion is applied */
	tuned_pa_hibern8time = ((max_hibern8_time * HIBERN8TIME_UNIT_US)
				/ PA_HIBERN8_TIME_UNIT_US);
	ret = ufshcd_dme_set(hba, UIC_ARG_MIB(PA_HIBERN8TIME),
			     tuned_pa_hibern8time);
out:
	return ret;
}

/**
 * ufshcd_quirk_tune_host_pa_tactivate - Ensures that host PA_TACTIVATE is
 * less than device PA_TACTIVATE time.
 * @hba: per-adapter instance
 *
 * Some UFS devices require host PA_TACTIVATE to be lower than device
 * PA_TACTIVATE, we need to enable UFS_DEVICE_QUIRK_HOST_PA_TACTIVATE quirk
 * for such devices.
 *
 * Returns zero on success, non-zero error value on failure.
 */
static int ufshcd_quirk_tune_host_pa_tactivate(struct ufs_hba *hba)
{
	int ret = 0;
	u32 granularity, peer_granularity;
	u32 pa_tactivate, peer_pa_tactivate;
	u32 pa_tactivate_us, peer_pa_tactivate_us;
	u8 gran_to_us_table[] = {1, 4, 8, 16, 32, 100};

	ret = ufshcd_dme_get(hba, UIC_ARG_MIB(PA_GRANULARITY),
				  &granularity);
	if (ret)
		goto out;

	ret = ufshcd_dme_peer_get(hba, UIC_ARG_MIB(PA_GRANULARITY),
				  &peer_granularity);
	if (ret)
		goto out;

	if ((granularity < PA_GRANULARITY_MIN_VAL) ||
	    (granularity > PA_GRANULARITY_MAX_VAL)) {
		dev_err(hba->dev, "%s: invalid host PA_GRANULARITY %d",
			__func__, granularity);
		return -EINVAL;
	}

	if ((peer_granularity < PA_GRANULARITY_MIN_VAL) ||
	    (peer_granularity > PA_GRANULARITY_MAX_VAL)) {
		dev_err(hba->dev, "%s: invalid device PA_GRANULARITY %d",
			__func__, peer_granularity);
		return -EINVAL;
	}

	ret = ufshcd_dme_get(hba, UIC_ARG_MIB(PA_TACTIVATE), &pa_tactivate);
	if (ret)
		goto out;

	ret = ufshcd_dme_peer_get(hba, UIC_ARG_MIB(PA_TACTIVATE),
				  &peer_pa_tactivate);
	if (ret)
		goto out;

	pa_tactivate_us = pa_tactivate * gran_to_us_table[granularity - 1];
	peer_pa_tactivate_us = peer_pa_tactivate *
			     gran_to_us_table[peer_granularity - 1];

	if (pa_tactivate_us > peer_pa_tactivate_us) {
		u32 new_peer_pa_tactivate;

		new_peer_pa_tactivate = pa_tactivate_us /
				      gran_to_us_table[peer_granularity - 1];
		new_peer_pa_tactivate++;
		ret = ufshcd_dme_peer_set(hba, UIC_ARG_MIB(PA_TACTIVATE),
					  new_peer_pa_tactivate);
	}

out:
	return ret;
}

static void ufshcd_tune_unipro_params(struct ufs_hba *hba)
{
	if (ufshcd_is_unipro_pa_params_tuning_req(hba)) {
		ufshcd_tune_pa_tactivate(hba);
		ufshcd_tune_pa_hibern8time(hba);
	}

	ufshcd_vops_apply_dev_quirks(hba);

	if (hba->dev_quirks & UFS_DEVICE_QUIRK_PA_TACTIVATE)
		/* set 1ms timeout for PA_TACTIVATE */
		ufshcd_dme_set(hba, UIC_ARG_MIB(PA_TACTIVATE), 10);

	if (hba->dev_quirks & UFS_DEVICE_QUIRK_HOST_PA_TACTIVATE)
		ufshcd_quirk_tune_host_pa_tactivate(hba);
}

static void ufshcd_clear_dbg_ufs_stats(struct ufs_hba *hba)
{
	hba->ufs_stats.hibern8_exit_cnt = 0;
	hba->ufs_stats.last_hibern8_exit_tstamp = ktime_set(0, 0);
	hba->req_abort_count = 0;
}

static int ufshcd_device_geo_params_init(struct ufs_hba *hba)
{
	int err;
	size_t buff_len;
	u8 *desc_buf;

	buff_len = hba->desc_size[QUERY_DESC_IDN_GEOMETRY];
	desc_buf = kmalloc(buff_len, GFP_KERNEL);
	if (!desc_buf) {
		err = -ENOMEM;
		goto out;
	}

	err = ufshcd_read_desc_param(hba, QUERY_DESC_IDN_GEOMETRY, 0, 0,
				     desc_buf, buff_len);
	if (err) {
		dev_err(hba->dev, "%s: Failed reading Geometry Desc. err = %d\n",
				__func__, err);
		goto out;
	}

	if (desc_buf[GEOMETRY_DESC_PARAM_MAX_NUM_LUN] == 1)
		hba->dev_info.max_lu_supported = 32;
	else if (desc_buf[GEOMETRY_DESC_PARAM_MAX_NUM_LUN] == 0)
		hba->dev_info.max_lu_supported = 8;

	if (hba->desc_size[QUERY_DESC_IDN_GEOMETRY] >=
		GEOMETRY_DESC_PARAM_HPB_MAX_ACTIVE_REGS)
		ufshpb_get_geo_info(hba, desc_buf);

out:
	kfree(desc_buf);
	return err;
}

static struct ufs_ref_clk ufs_ref_clk_freqs[] = {
	{19200000, REF_CLK_FREQ_19_2_MHZ},
	{26000000, REF_CLK_FREQ_26_MHZ},
	{38400000, REF_CLK_FREQ_38_4_MHZ},
	{52000000, REF_CLK_FREQ_52_MHZ},
	{0, REF_CLK_FREQ_INVAL},
};

static enum ufs_ref_clk_freq
ufs_get_bref_clk_from_hz(unsigned long freq)
{
	int i;

	for (i = 0; ufs_ref_clk_freqs[i].freq_hz; i++)
		if (ufs_ref_clk_freqs[i].freq_hz == freq)
			return ufs_ref_clk_freqs[i].val;

	return REF_CLK_FREQ_INVAL;
}

void ufshcd_parse_dev_ref_clk_freq(struct ufs_hba *hba, struct clk *refclk)
{
	unsigned long freq;

	freq = clk_get_rate(refclk);

	hba->dev_ref_clk_freq =
		ufs_get_bref_clk_from_hz(freq);

	if (hba->dev_ref_clk_freq == REF_CLK_FREQ_INVAL)
		dev_err(hba->dev,
		"invalid ref_clk setting = %ld\n", freq);
}

static int ufshcd_set_dev_ref_clk(struct ufs_hba *hba)
{
	int err;
	u32 ref_clk;
	u32 freq = hba->dev_ref_clk_freq;

	err = ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_READ_ATTR,
			QUERY_ATTR_IDN_REF_CLK_FREQ, 0, 0, &ref_clk);

	if (err) {
		dev_err(hba->dev, "failed reading bRefClkFreq. err = %d\n",
			err);
		goto out;
	}

	if (ref_clk == freq)
		goto out; /* nothing to update */

	err = ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_WRITE_ATTR,
			QUERY_ATTR_IDN_REF_CLK_FREQ, 0, 0, &freq);

	if (err) {
		dev_err(hba->dev, "bRefClkFreq setting to %lu Hz failed\n",
			ufs_ref_clk_freqs[freq].freq_hz);
		goto out;
	}

	dev_dbg(hba->dev, "bRefClkFreq setting to %lu Hz succeeded\n",
			ufs_ref_clk_freqs[freq].freq_hz);

out:
	return err;
}

static int ufshcd_device_params_init(struct ufs_hba *hba)
{
	bool flag;
	int ret, i;

	 /* Init device descriptor sizes */
	for (i = 0; i < QUERY_DESC_IDN_MAX; i++)
		hba->desc_size[i] = QUERY_DESC_MAX_SIZE;

	/* Init UFS geometry descriptor related parameters */
	ret = ufshcd_device_geo_params_init(hba);
	if (ret)
		goto out;

	/* Check and apply UFS device quirks */
	ret = ufs_get_device_desc(hba);
	if (ret) {
		dev_err(hba->dev, "%s: Failed getting device info. err = %d\n",
			__func__, ret);
		goto out;
	}

	ufshcd_get_ref_clk_gating_wait(hba);

	if (!ufshcd_query_flag_retry(hba, UPIU_QUERY_OPCODE_READ_FLAG,
			QUERY_FLAG_IDN_PWR_ON_WPE, 0, &flag))
		hba->dev_info.f_power_on_wp_en = flag;

	/* Probe maximum power mode co-supported by both UFS host and device */
	if (ufshcd_get_max_pwr_mode(hba))
		dev_err(hba->dev,
			"%s: Failed getting max supported power mode\n",
			__func__);
out:
	return ret;
}

/**
 * ufshcd_add_lus - probe and add UFS logical units
 * @hba: per-adapter instance
 */
static int ufshcd_add_lus(struct ufs_hba *hba)
{
	int ret;

	/* Add required well known logical units to scsi mid layer */
	ret = ufshcd_scsi_add_wlus(hba);
	if (ret)
		goto out;

	/* Initialize devfreq after UFS device is detected */
	if (ufshcd_is_clkscaling_supported(hba)) {
		memcpy(&hba->clk_scaling.saved_pwr_info.info,
			&hba->pwr_info,
			sizeof(struct ufs_pa_layer_attr));
		hba->clk_scaling.saved_pwr_info.is_valid = true;
		hba->clk_scaling.is_allowed = true;

		ret = ufshcd_devfreq_init(hba);
		if (ret)
			goto out;

		hba->clk_scaling.is_enabled = true;
		ufshcd_init_clk_scaling_sysfs(hba);
	}

	ufs_bsg_probe(hba);
	ufshpb_init(hba);
	scsi_scan_host(hba->host);
	pm_runtime_put_sync(hba->dev);

out:
	return ret;
}

/**
 * ufshcd_probe_hba - probe hba to detect device and initialize it
 * @hba: per-adapter instance
 * @init_dev_params: whether or not to call ufshcd_device_params_init().
 *
 * Execute link-startup and verify device initialization
 */
static int ufshcd_probe_hba(struct ufs_hba *hba, bool init_dev_params)
{
	int ret;
	unsigned long flags;
	ktime_t start = ktime_get();

	hba->ufshcd_state = UFSHCD_STATE_RESET;

	ret = ufshcd_link_startup(hba);
	if (ret)
		goto out;

	/* Debug counters initialization */
	ufshcd_clear_dbg_ufs_stats(hba);

	/* UniPro link is active now */
	ufshcd_set_link_active(hba);

	/* Verify device initialization by sending NOP OUT UPIU */
	ret = ufshcd_verify_dev_init(hba);
	if (ret)
		goto out;

	/* Initiate UFS initialization, and waiting until completion */
	ret = ufshcd_complete_dev_init(hba);
	if (ret)
		goto out;

	/*
	 * Initialize UFS device parameters used by driver, these
	 * parameters are associated with UFS descriptors.
	 */
	if (init_dev_params) {
		ret = ufshcd_device_params_init(hba);
		if (ret)
			goto out;
	}

	ufshcd_tune_unipro_params(hba);

	/* UFS device is also active now */
	ufshcd_set_ufs_dev_active(hba);
	ufshcd_force_reset_auto_bkops(hba);

	/* Gear up to HS gear if supported */
	if (hba->max_pwr_info.is_valid) {
		/*
		 * Set the right value to bRefClkFreq before attempting to
		 * switch to HS gears.
		 */
		if (hba->dev_ref_clk_freq != REF_CLK_FREQ_INVAL)
			ufshcd_set_dev_ref_clk(hba);
		ret = ufshcd_config_pwr_mode(hba, &hba->max_pwr_info.info);
		if (ret) {
			dev_err(hba->dev, "%s: Failed setting power mode, err = %d\n",
					__func__, ret);
			goto out;
		}
		ufshcd_print_pwr_info(hba);
	}

	/*
	 * bActiveICCLevel is volatile for UFS device (as per latest v2.1 spec)
	 * and for removable UFS card as well, hence always set the parameter.
	 * Note: Error handler may issue the device reset hence resetting
	 * bActiveICCLevel as well so it is always safe to set this here.
	 */
	ufshcd_set_active_icc_lvl(hba);

	ufshcd_wb_config(hba);
	if (hba->ee_usr_mask)
		ufshcd_write_ee_control(hba);
	/* Enable Auto-Hibernate if configured */
	ufshcd_auto_hibern8_enable(hba);

	ufshpb_reset(hba);
out:
	spin_lock_irqsave(hba->host->host_lock, flags);
	if (ret)
		hba->ufshcd_state = UFSHCD_STATE_ERROR;
	else if (hba->ufshcd_state == UFSHCD_STATE_RESET)
		hba->ufshcd_state = UFSHCD_STATE_OPERATIONAL;
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	trace_ufshcd_init(dev_name(hba->dev), ret,
		ktime_to_us(ktime_sub(ktime_get(), start)),
		hba->curr_dev_pwr_mode, hba->uic_link_state);
	return ret;
}

/**
 * ufshcd_async_scan - asynchronous execution for probing hba
 * @data: data pointer to pass to this function
 * @cookie: cookie data
 */
static void ufshcd_async_scan(void *data, async_cookie_t cookie)
{
	struct ufs_hba *hba = (struct ufs_hba *)data;
	int ret;

	down(&hba->host_sem);
	/* Initialize hba, detect and initialize UFS device */
	ret = ufshcd_probe_hba(hba, true);
	up(&hba->host_sem);
	if (ret)
		goto out;

	/* Probe and add UFS logical units  */
	ret = ufshcd_add_lus(hba);
out:
	/*
	 * If we failed to initialize the device or the device is not
	 * present, turn off the power/clocks etc.
	 */
	if (ret) {
		pm_runtime_put_sync(hba->dev);
		ufshcd_hba_exit(hba);
	}
}

static const struct attribute_group *ufshcd_driver_groups[] = {
	&ufs_sysfs_unit_descriptor_group,
	&ufs_sysfs_lun_attributes_group,
#ifdef CONFIG_SCSI_UFS_HPB
	&ufs_sysfs_hpb_stat_group,
	&ufs_sysfs_hpb_param_group,
#endif
	NULL,
};

static struct ufs_hba_variant_params ufs_hba_vps = {
	.hba_enable_delay_us		= 1000,
	.wb_flush_threshold		= UFS_WB_BUF_REMAIN_PERCENT(40),
	.devfreq_profile.polling_ms	= 100,
	.devfreq_profile.target		= ufshcd_devfreq_target,
	.devfreq_profile.get_dev_status	= ufshcd_devfreq_get_dev_status,
	.ondemand_data.upthreshold	= 70,
	.ondemand_data.downdifferential	= 5,
};

static struct scsi_host_template ufshcd_driver_template = {
	.module			= THIS_MODULE,
	.name			= UFSHCD,
	.proc_name		= UFSHCD,
	.queuecommand		= ufshcd_queuecommand,
	.slave_alloc		= ufshcd_slave_alloc,
	.slave_configure	= ufshcd_slave_configure,
	.slave_destroy		= ufshcd_slave_destroy,
	.change_queue_depth	= ufshcd_change_queue_depth,
	.eh_abort_handler	= ufshcd_abort,
	.eh_device_reset_handler = ufshcd_eh_device_reset_handler,
	.eh_host_reset_handler   = ufshcd_eh_host_reset_handler,
	.this_id		= -1,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= UFSHCD_CMD_PER_LUN,
	.can_queue		= UFSHCD_CAN_QUEUE,
	.max_segment_size	= PRDT_DATA_BYTE_COUNT_MAX,
	.max_host_blocked	= 1,
	.track_queue_depth	= 1,
	.sdev_groups		= ufshcd_driver_groups,
	.dma_boundary		= PAGE_SIZE - 1,
	.rpm_autosuspend_delay	= RPM_AUTOSUSPEND_DELAY_MS,
};

static int ufshcd_config_vreg_load(struct device *dev, struct ufs_vreg *vreg,
				   int ua)
{
	int ret;

	if (!vreg)
		return 0;

	/*
	 * "set_load" operation shall be required on those regulators
	 * which specifically configured current limitation. Otherwise
	 * zero max_uA may cause unexpected behavior when regulator is
	 * enabled or set as high power mode.
	 */
	if (!vreg->max_uA)
		return 0;

	ret = regulator_set_load(vreg->reg, ua);
	if (ret < 0) {
		dev_err(dev, "%s: %s set load (ua=%d) failed, err=%d\n",
				__func__, vreg->name, ua, ret);
	}

	return ret;
}

static inline int ufshcd_config_vreg_lpm(struct ufs_hba *hba,
					 struct ufs_vreg *vreg)
{
	return ufshcd_config_vreg_load(hba->dev, vreg, UFS_VREG_LPM_LOAD_UA);
}

static inline int ufshcd_config_vreg_hpm(struct ufs_hba *hba,
					 struct ufs_vreg *vreg)
{
	if (!vreg)
		return 0;

	return ufshcd_config_vreg_load(hba->dev, vreg, vreg->max_uA);
}

static int ufshcd_config_vreg(struct device *dev,
		struct ufs_vreg *vreg, bool on)
{
	int ret = 0;
	struct regulator *reg;
	const char *name;
	int min_uV, uA_load;

	BUG_ON(!vreg);

	reg = vreg->reg;
	name = vreg->name;

	if (regulator_count_voltages(reg) > 0) {
		uA_load = on ? vreg->max_uA : 0;
		ret = ufshcd_config_vreg_load(dev, vreg, uA_load);
		if (ret)
			goto out;

		if (vreg->min_uV && vreg->max_uV) {
			min_uV = on ? vreg->min_uV : 0;
			ret = regulator_set_voltage(reg, min_uV, vreg->max_uV);
			if (ret)
				dev_err(dev,
					"%s: %s set voltage failed, err=%d\n",
					__func__, name, ret);
		}
	}
out:
	return ret;
}

static int ufshcd_enable_vreg(struct device *dev, struct ufs_vreg *vreg)
{
	int ret = 0;

	if (!vreg || vreg->enabled)
		goto out;

	ret = ufshcd_config_vreg(dev, vreg, true);
	if (!ret)
		ret = regulator_enable(vreg->reg);

	if (!ret)
		vreg->enabled = true;
	else
		dev_err(dev, "%s: %s enable failed, err=%d\n",
				__func__, vreg->name, ret);
out:
	return ret;
}

static int ufshcd_disable_vreg(struct device *dev, struct ufs_vreg *vreg)
{
	int ret = 0;

	if (!vreg || !vreg->enabled || vreg->always_on)
		goto out;

	ret = regulator_disable(vreg->reg);

	if (!ret) {
		/* ignore errors on applying disable config */
		ufshcd_config_vreg(dev, vreg, false);
		vreg->enabled = false;
	} else {
		dev_err(dev, "%s: %s disable failed, err=%d\n",
				__func__, vreg->name, ret);
	}
out:
	return ret;
}

static int ufshcd_setup_vreg(struct ufs_hba *hba, bool on)
{
	int ret = 0;
	struct device *dev = hba->dev;
	struct ufs_vreg_info *info = &hba->vreg_info;

	ret = ufshcd_toggle_vreg(dev, info->vcc, on);
	if (ret)
		goto out;

	ret = ufshcd_toggle_vreg(dev, info->vccq, on);
	if (ret)
		goto out;

	ret = ufshcd_toggle_vreg(dev, info->vccq2, on);

out:
	if (ret) {
		ufshcd_toggle_vreg(dev, info->vccq2, false);
		ufshcd_toggle_vreg(dev, info->vccq, false);
		ufshcd_toggle_vreg(dev, info->vcc, false);
	}
	return ret;
}

static int ufshcd_setup_hba_vreg(struct ufs_hba *hba, bool on)
{
	struct ufs_vreg_info *info = &hba->vreg_info;

	return ufshcd_toggle_vreg(hba->dev, info->vdd_hba, on);
}

static int ufshcd_get_vreg(struct device *dev, struct ufs_vreg *vreg)
{
	int ret = 0;

	if (!vreg)
		goto out;

	vreg->reg = devm_regulator_get(dev, vreg->name);
	if (IS_ERR(vreg->reg)) {
		ret = PTR_ERR(vreg->reg);
		dev_err(dev, "%s: %s get failed, err=%d\n",
				__func__, vreg->name, ret);
	}
out:
	return ret;
}

static int ufshcd_init_vreg(struct ufs_hba *hba)
{
	int ret = 0;
	struct device *dev = hba->dev;
	struct ufs_vreg_info *info = &hba->vreg_info;

	ret = ufshcd_get_vreg(dev, info->vcc);
	if (ret)
		goto out;

	ret = ufshcd_get_vreg(dev, info->vccq);
	if (!ret)
		ret = ufshcd_get_vreg(dev, info->vccq2);
out:
	return ret;
}

static int ufshcd_init_hba_vreg(struct ufs_hba *hba)
{
	struct ufs_vreg_info *info = &hba->vreg_info;

	if (info)
		return ufshcd_get_vreg(hba->dev, info->vdd_hba);

	return 0;
}

static int ufshcd_setup_clocks(struct ufs_hba *hba, bool on)
{
	int ret = 0;
	struct ufs_clk_info *clki;
	struct list_head *head = &hba->clk_list_head;
	unsigned long flags;
	ktime_t start = ktime_get();
	bool clk_state_changed = false;

	if (list_empty(head))
		goto out;

	ret = ufshcd_vops_setup_clocks(hba, on, PRE_CHANGE);
	if (ret)
		return ret;

	list_for_each_entry(clki, head, list) {
		if (!IS_ERR_OR_NULL(clki->clk)) {
			/*
			 * Don't disable clocks which are needed
			 * to keep the link active.
			 */
			if (ufshcd_is_link_active(hba) &&
			    clki->keep_link_active)
				continue;

			clk_state_changed = on ^ clki->enabled;
			if (on && !clki->enabled) {
				ret = clk_prepare_enable(clki->clk);
				if (ret) {
					dev_err(hba->dev, "%s: %s prepare enable failed, %d\n",
						__func__, clki->name, ret);
					goto out;
				}
			} else if (!on && clki->enabled) {
				clk_disable_unprepare(clki->clk);
			}
			clki->enabled = on;
			dev_dbg(hba->dev, "%s: clk: %s %sabled\n", __func__,
					clki->name, on ? "en" : "dis");
		}
	}

	ret = ufshcd_vops_setup_clocks(hba, on, POST_CHANGE);
	if (ret)
		return ret;

out:
	if (ret) {
		list_for_each_entry(clki, head, list) {
			if (!IS_ERR_OR_NULL(clki->clk) && clki->enabled)
				clk_disable_unprepare(clki->clk);
		}
	} else if (!ret && on) {
		spin_lock_irqsave(hba->host->host_lock, flags);
		hba->clk_gating.state = CLKS_ON;
		trace_ufshcd_clk_gating(dev_name(hba->dev),
					hba->clk_gating.state);
		spin_unlock_irqrestore(hba->host->host_lock, flags);
	}

	if (clk_state_changed)
		trace_ufshcd_profile_clk_gating(dev_name(hba->dev),
			(on ? "on" : "off"),
			ktime_to_us(ktime_sub(ktime_get(), start)), ret);
	return ret;
}

static int ufshcd_init_clocks(struct ufs_hba *hba)
{
	int ret = 0;
	struct ufs_clk_info *clki;
	struct device *dev = hba->dev;
	struct list_head *head = &hba->clk_list_head;

	if (list_empty(head))
		goto out;

	list_for_each_entry(clki, head, list) {
		if (!clki->name)
			continue;

		clki->clk = devm_clk_get(dev, clki->name);
		if (IS_ERR(clki->clk)) {
			ret = PTR_ERR(clki->clk);
			dev_err(dev, "%s: %s clk get failed, %d\n",
					__func__, clki->name, ret);
			goto out;
		}

		/*
		 * Parse device ref clk freq as per device tree "ref_clk".
		 * Default dev_ref_clk_freq is set to REF_CLK_FREQ_INVAL
		 * in ufshcd_alloc_host().
		 */
		if (!strcmp(clki->name, "ref_clk"))
			ufshcd_parse_dev_ref_clk_freq(hba, clki->clk);

		if (clki->max_freq) {
			ret = clk_set_rate(clki->clk, clki->max_freq);
			if (ret) {
				dev_err(hba->dev, "%s: %s clk set rate(%dHz) failed, %d\n",
					__func__, clki->name,
					clki->max_freq, ret);
				goto out;
			}
			clki->curr_freq = clki->max_freq;
		}
		dev_dbg(dev, "%s: clk: %s, rate: %lu\n", __func__,
				clki->name, clk_get_rate(clki->clk));
	}
out:
	return ret;
}

static int ufshcd_variant_hba_init(struct ufs_hba *hba)
{
	int err = 0;

	if (!hba->vops)
		goto out;

	err = ufshcd_vops_init(hba);
	if (err)
		dev_err(hba->dev, "%s: variant %s init failed err %d\n",
			__func__, ufshcd_get_var_name(hba), err);
out:
	return err;
}

static void ufshcd_variant_hba_exit(struct ufs_hba *hba)
{
	if (!hba->vops)
		return;

	ufshcd_vops_exit(hba);
}

static int ufshcd_hba_init(struct ufs_hba *hba)
{
	int err;

	/*
	 * Handle host controller power separately from the UFS device power
	 * rails as it will help controlling the UFS host controller power
	 * collapse easily which is different than UFS device power collapse.
	 * Also, enable the host controller power before we go ahead with rest
	 * of the initialization here.
	 */
	err = ufshcd_init_hba_vreg(hba);
	if (err)
		goto out;

	err = ufshcd_setup_hba_vreg(hba, true);
	if (err)
		goto out;

	err = ufshcd_init_clocks(hba);
	if (err)
		goto out_disable_hba_vreg;

	err = ufshcd_setup_clocks(hba, true);
	if (err)
		goto out_disable_hba_vreg;

	err = ufshcd_init_vreg(hba);
	if (err)
		goto out_disable_clks;

	err = ufshcd_setup_vreg(hba, true);
	if (err)
		goto out_disable_clks;

	err = ufshcd_variant_hba_init(hba);
	if (err)
		goto out_disable_vreg;

	ufs_debugfs_hba_init(hba);

	hba->is_powered = true;
	goto out;

out_disable_vreg:
	ufshcd_setup_vreg(hba, false);
out_disable_clks:
	ufshcd_setup_clocks(hba, false);
out_disable_hba_vreg:
	ufshcd_setup_hba_vreg(hba, false);
out:
	return err;
}

static void ufshcd_hba_exit(struct ufs_hba *hba)
{
	if (hba->is_powered) {
		ufshcd_exit_clk_scaling(hba);
		ufshcd_exit_clk_gating(hba);
		if (hba->eh_wq)
			destroy_workqueue(hba->eh_wq);
		ufs_debugfs_hba_exit(hba);
		ufshcd_variant_hba_exit(hba);
		ufshcd_setup_vreg(hba, false);
		ufshcd_setup_clocks(hba, false);
		ufshcd_setup_hba_vreg(hba, false);
		hba->is_powered = false;
		ufs_put_device_desc(hba);
	}
}

/**
 * ufshcd_set_dev_pwr_mode - sends START STOP UNIT command to set device
 *			     power mode
 * @hba: per adapter instance
 * @pwr_mode: device power mode to set
 *
 * Returns 0 if requested power mode is set successfully
 * Returns < 0 if failed to set the requested power mode
 */
static int ufshcd_set_dev_pwr_mode(struct ufs_hba *hba,
				     enum ufs_dev_pwr_mode pwr_mode)
{
	unsigned char cmd[6] = { START_STOP };
	struct scsi_sense_hdr sshdr;
	struct scsi_device *sdp;
	unsigned long flags;
	int ret, retries;

	spin_lock_irqsave(hba->host->host_lock, flags);
	sdp = hba->sdev_ufs_device;
	if (sdp) {
		ret = scsi_device_get(sdp);
		if (!ret && !scsi_device_online(sdp)) {
			ret = -ENODEV;
			scsi_device_put(sdp);
		}
	} else {
		ret = -ENODEV;
	}
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	if (ret)
		return ret;

	/*
	 * If scsi commands fail, the scsi mid-layer schedules scsi error-
	 * handling, which would wait for host to be resumed. Since we know
	 * we are functional while we are here, skip host resume in error
	 * handling context.
	 */
	hba->host->eh_noresume = 1;

	cmd[4] = pwr_mode << 4;

	/*
	 * Current function would be generally called from the power management
	 * callbacks hence set the RQF_PM flag so that it doesn't resume the
	 * already suspended childs.
	 */
	for (retries = 3; retries > 0; --retries) {
		ret = scsi_execute(sdp, cmd, DMA_NONE, NULL, 0, NULL, &sshdr,
				START_STOP_TIMEOUT, 0, 0, RQF_PM, NULL);
		if (!scsi_status_is_check_condition(ret) ||
				!scsi_sense_valid(&sshdr) ||
				sshdr.sense_key != UNIT_ATTENTION)
			break;
	}
	if (ret) {
		sdev_printk(KERN_WARNING, sdp,
			    "START_STOP failed for power mode: %d, result %x\n",
			    pwr_mode, ret);
		if (ret > 0) {
			if (scsi_sense_valid(&sshdr))
				scsi_print_sense_hdr(sdp, NULL, &sshdr);
			ret = -EIO;
		}
	}

	if (!ret)
		hba->curr_dev_pwr_mode = pwr_mode;

	scsi_device_put(sdp);
	hba->host->eh_noresume = 0;
	return ret;
}

static int ufshcd_link_state_transition(struct ufs_hba *hba,
					enum uic_link_state req_link_state,
					int check_for_bkops)
{
	int ret = 0;

	if (req_link_state == hba->uic_link_state)
		return 0;

	if (req_link_state == UIC_LINK_HIBERN8_STATE) {
		ret = ufshcd_uic_hibern8_enter(hba);
		if (!ret) {
			ufshcd_set_link_hibern8(hba);
		} else {
			dev_err(hba->dev, "%s: hibern8 enter failed %d\n",
					__func__, ret);
			goto out;
		}
	}
	/*
	 * If autobkops is enabled, link can't be turned off because
	 * turning off the link would also turn off the device, except in the
	 * case of DeepSleep where the device is expected to remain powered.
	 */
	else if ((req_link_state == UIC_LINK_OFF_STATE) &&
		 (!check_for_bkops || !hba->auto_bkops_enabled)) {
		/*
		 * Let's make sure that link is in low power mode, we are doing
		 * this currently by putting the link in Hibern8. Otherway to
		 * put the link in low power mode is to send the DME end point
		 * to device and then send the DME reset command to local
		 * unipro. But putting the link in hibern8 is much faster.
		 *
		 * Note also that putting the link in Hibern8 is a requirement
		 * for entering DeepSleep.
		 */
		ret = ufshcd_uic_hibern8_enter(hba);
		if (ret) {
			dev_err(hba->dev, "%s: hibern8 enter failed %d\n",
					__func__, ret);
			goto out;
		}
		/*
		 * Change controller state to "reset state" which
		 * should also put the link in off/reset state
		 */
		ufshcd_hba_stop(hba);
		/*
		 * TODO: Check if we need any delay to make sure that
		 * controller is reset
		 */
		ufshcd_set_link_off(hba);
	}

out:
	return ret;
}

static void ufshcd_vreg_set_lpm(struct ufs_hba *hba)
{
	bool vcc_off = false;

	/*
	 * It seems some UFS devices may keep drawing more than sleep current
	 * (atleast for 500us) from UFS rails (especially from VCCQ rail).
	 * To avoid this situation, add 2ms delay before putting these UFS
	 * rails in LPM mode.
	 */
	if (!ufshcd_is_link_active(hba) &&
	    hba->dev_quirks & UFS_DEVICE_QUIRK_DELAY_BEFORE_LPM)
		usleep_range(2000, 2100);

	/*
	 * If UFS device is either in UFS_Sleep turn off VCC rail to save some
	 * power.
	 *
	 * If UFS device and link is in OFF state, all power supplies (VCC,
	 * VCCQ, VCCQ2) can be turned off if power on write protect is not
	 * required. If UFS link is inactive (Hibern8 or OFF state) and device
	 * is in sleep state, put VCCQ & VCCQ2 rails in LPM mode.
	 *
	 * Ignore the error returned by ufshcd_toggle_vreg() as device is anyway
	 * in low power state which would save some power.
	 *
	 * If Write Booster is enabled and the device needs to flush the WB
	 * buffer OR if bkops status is urgent for WB, keep Vcc on.
	 */
	if (ufshcd_is_ufs_dev_poweroff(hba) && ufshcd_is_link_off(hba) &&
	    !hba->dev_info.is_lu_power_on_wp) {
		ufshcd_setup_vreg(hba, false);
		vcc_off = true;
	} else if (!ufshcd_is_ufs_dev_active(hba)) {
		ufshcd_toggle_vreg(hba->dev, hba->vreg_info.vcc, false);
		vcc_off = true;
		if (ufshcd_is_link_hibern8(hba) || ufshcd_is_link_off(hba)) {
			ufshcd_config_vreg_lpm(hba, hba->vreg_info.vccq);
			ufshcd_config_vreg_lpm(hba, hba->vreg_info.vccq2);
		}
	}

	/*
	 * Some UFS devices require delay after VCC power rail is turned-off.
	 */
	if (vcc_off && hba->vreg_info.vcc &&
		hba->dev_quirks & UFS_DEVICE_QUIRK_DELAY_AFTER_LPM)
		usleep_range(5000, 5100);
}

#ifdef CONFIG_PM
static int ufshcd_vreg_set_hpm(struct ufs_hba *hba)
{
	int ret = 0;

	if (ufshcd_is_ufs_dev_poweroff(hba) && ufshcd_is_link_off(hba) &&
	    !hba->dev_info.is_lu_power_on_wp) {
		ret = ufshcd_setup_vreg(hba, true);
	} else if (!ufshcd_is_ufs_dev_active(hba)) {
		if (!ufshcd_is_link_active(hba)) {
			ret = ufshcd_config_vreg_hpm(hba, hba->vreg_info.vccq);
			if (ret)
				goto vcc_disable;
			ret = ufshcd_config_vreg_hpm(hba, hba->vreg_info.vccq2);
			if (ret)
				goto vccq_lpm;
		}
		ret = ufshcd_toggle_vreg(hba->dev, hba->vreg_info.vcc, true);
	}
	goto out;

vccq_lpm:
	ufshcd_config_vreg_lpm(hba, hba->vreg_info.vccq);
vcc_disable:
	ufshcd_toggle_vreg(hba->dev, hba->vreg_info.vcc, false);
out:
	return ret;
}
#endif /* CONFIG_PM */

static void ufshcd_hba_vreg_set_lpm(struct ufs_hba *hba)
{
	if (ufshcd_is_link_off(hba) || ufshcd_can_aggressive_pc(hba))
		ufshcd_setup_hba_vreg(hba, false);
}

static void ufshcd_hba_vreg_set_hpm(struct ufs_hba *hba)
{
	if (ufshcd_is_link_off(hba) || ufshcd_can_aggressive_pc(hba))
		ufshcd_setup_hba_vreg(hba, true);
}

static int __ufshcd_wl_suspend(struct ufs_hba *hba, enum ufs_pm_op pm_op)
{
	int ret = 0;
	int check_for_bkops;
	enum ufs_pm_level pm_lvl;
	enum ufs_dev_pwr_mode req_dev_pwr_mode;
	enum uic_link_state req_link_state;

	hba->pm_op_in_progress = true;
	if (pm_op != UFS_SHUTDOWN_PM) {
		pm_lvl = pm_op == UFS_RUNTIME_PM ?
			 hba->rpm_lvl : hba->spm_lvl;
		req_dev_pwr_mode = ufs_get_pm_lvl_to_dev_pwr_mode(pm_lvl);
		req_link_state = ufs_get_pm_lvl_to_link_pwr_state(pm_lvl);
	} else {
		req_dev_pwr_mode = UFS_POWERDOWN_PWR_MODE;
		req_link_state = UIC_LINK_OFF_STATE;
	}

	ufshpb_suspend(hba);

	/*
	 * If we can't transition into any of the low power modes
	 * just gate the clocks.
	 */
	ufshcd_hold(hba, false);
	hba->clk_gating.is_suspended = true;

	if (ufshcd_is_clkscaling_supported(hba))
		ufshcd_clk_scaling_suspend(hba, true);

	if (req_dev_pwr_mode == UFS_ACTIVE_PWR_MODE &&
			req_link_state == UIC_LINK_ACTIVE_STATE) {
		goto vops_suspend;
	}

	if ((req_dev_pwr_mode == hba->curr_dev_pwr_mode) &&
	    (req_link_state == hba->uic_link_state))
		goto enable_scaling;

	/* UFS device & link must be active before we enter in this function */
	if (!ufshcd_is_ufs_dev_active(hba) || !ufshcd_is_link_active(hba)) {
		ret = -EINVAL;
		goto enable_scaling;
	}

	if (pm_op == UFS_RUNTIME_PM) {
		if (ufshcd_can_autobkops_during_suspend(hba)) {
			/*
			 * The device is idle with no requests in the queue,
			 * allow background operations if bkops status shows
			 * that performance might be impacted.
			 */
			ret = ufshcd_urgent_bkops(hba);
			if (ret)
				goto enable_scaling;
		} else {
			/* make sure that auto bkops is disabled */
			ufshcd_disable_auto_bkops(hba);
		}
		/*
		 * If device needs to do BKOP or WB buffer flush during
		 * Hibern8, keep device power mode as "active power mode"
		 * and VCC supply.
		 */
		hba->dev_info.b_rpm_dev_flush_capable =
			hba->auto_bkops_enabled ||
			(((req_link_state == UIC_LINK_HIBERN8_STATE) ||
			((req_link_state == UIC_LINK_ACTIVE_STATE) &&
			ufshcd_is_auto_hibern8_enabled(hba))) &&
			ufshcd_wb_need_flush(hba));
	}

	flush_work(&hba->eeh_work);

	if (req_dev_pwr_mode != hba->curr_dev_pwr_mode) {
		if (pm_op != UFS_RUNTIME_PM)
			/* ensure that bkops is disabled */
			ufshcd_disable_auto_bkops(hba);

		if (!hba->dev_info.b_rpm_dev_flush_capable) {
			ret = ufshcd_set_dev_pwr_mode(hba, req_dev_pwr_mode);
			if (ret)
				goto enable_scaling;
		}
	}

	/*
	 * In the case of DeepSleep, the device is expected to remain powered
	 * with the link off, so do not check for bkops.
	 */
	check_for_bkops = !ufshcd_is_ufs_dev_deepsleep(hba);
	ret = ufshcd_link_state_transition(hba, req_link_state, check_for_bkops);
	if (ret)
		goto set_dev_active;

vops_suspend:
	/*
	 * Call vendor specific suspend callback. As these callbacks may access
	 * vendor specific host controller register space call them before the
	 * host clocks are ON.
	 */
	ret = ufshcd_vops_suspend(hba, pm_op);
	if (ret)
		goto set_link_active;
	goto out;

set_link_active:
	/*
	 * Device hardware reset is required to exit DeepSleep. Also, for
	 * DeepSleep, the link is off so host reset and restore will be done
	 * further below.
	 */
	if (ufshcd_is_ufs_dev_deepsleep(hba)) {
		ufshcd_device_reset(hba);
		WARN_ON(!ufshcd_is_link_off(hba));
	}
	if (ufshcd_is_link_hibern8(hba) && !ufshcd_uic_hibern8_exit(hba))
		ufshcd_set_link_active(hba);
	else if (ufshcd_is_link_off(hba))
		ufshcd_host_reset_and_restore(hba);
set_dev_active:
	/* Can also get here needing to exit DeepSleep */
	if (ufshcd_is_ufs_dev_deepsleep(hba)) {
		ufshcd_device_reset(hba);
		ufshcd_host_reset_and_restore(hba);
	}
	if (!ufshcd_set_dev_pwr_mode(hba, UFS_ACTIVE_PWR_MODE))
		ufshcd_disable_auto_bkops(hba);
enable_scaling:
	if (ufshcd_is_clkscaling_supported(hba))
		ufshcd_clk_scaling_suspend(hba, false);

	hba->dev_info.b_rpm_dev_flush_capable = false;
out:
	if (hba->dev_info.b_rpm_dev_flush_capable) {
		schedule_delayed_work(&hba->rpm_dev_flush_recheck_work,
			msecs_to_jiffies(RPM_DEV_FLUSH_RECHECK_WORK_DELAY_MS));
	}

	if (ret) {
		ufshcd_update_evt_hist(hba, UFS_EVT_WL_SUSP_ERR, (u32)ret);
		hba->clk_gating.is_suspended = false;
		ufshcd_release(hba);
		ufshpb_resume(hba);
	}
	hba->pm_op_in_progress = false;
	return ret;
}

#ifdef CONFIG_PM
static int __ufshcd_wl_resume(struct ufs_hba *hba, enum ufs_pm_op pm_op)
{
	int ret;
	enum uic_link_state old_link_state = hba->uic_link_state;

	hba->pm_op_in_progress = true;

	/*
	 * Call vendor specific resume callback. As these callbacks may access
	 * vendor specific host controller register space call them when the
	 * host clocks are ON.
	 */
	ret = ufshcd_vops_resume(hba, pm_op);
	if (ret)
		goto out;

	/* For DeepSleep, the only supported option is to have the link off */
	WARN_ON(ufshcd_is_ufs_dev_deepsleep(hba) && !ufshcd_is_link_off(hba));

	if (ufshcd_is_link_hibern8(hba)) {
		ret = ufshcd_uic_hibern8_exit(hba);
		if (!ret) {
			ufshcd_set_link_active(hba);
		} else {
			dev_err(hba->dev, "%s: hibern8 exit failed %d\n",
					__func__, ret);
			goto vendor_suspend;
		}
	} else if (ufshcd_is_link_off(hba)) {
		/*
		 * A full initialization of the host and the device is
		 * required since the link was put to off during suspend.
		 * Note, in the case of DeepSleep, the device will exit
		 * DeepSleep due to device reset.
		 */
		ret = ufshcd_reset_and_restore(hba);
		/*
		 * ufshcd_reset_and_restore() should have already
		 * set the link state as active
		 */
		if (ret || !ufshcd_is_link_active(hba))
			goto vendor_suspend;
	}

	if (!ufshcd_is_ufs_dev_active(hba)) {
		ret = ufshcd_set_dev_pwr_mode(hba, UFS_ACTIVE_PWR_MODE);
		if (ret)
			goto set_old_link_state;
	}

	if (ufshcd_keep_autobkops_enabled_except_suspend(hba))
		ufshcd_enable_auto_bkops(hba);
	else
		/*
		 * If BKOPs operations are urgently needed at this moment then
		 * keep auto-bkops enabled or else disable it.
		 */
		ufshcd_urgent_bkops(hba);

	if (hba->ee_usr_mask)
		ufshcd_write_ee_control(hba);

	if (ufshcd_is_clkscaling_supported(hba))
		ufshcd_clk_scaling_suspend(hba, false);

	if (hba->dev_info.b_rpm_dev_flush_capable) {
		hba->dev_info.b_rpm_dev_flush_capable = false;
		cancel_delayed_work(&hba->rpm_dev_flush_recheck_work);
	}

	/* Enable Auto-Hibernate if configured */
	ufshcd_auto_hibern8_enable(hba);

	ufshpb_resume(hba);
	goto out;

set_old_link_state:
	ufshcd_link_state_transition(hba, old_link_state, 0);
vendor_suspend:
	ufshcd_vops_suspend(hba, pm_op);
out:
	if (ret)
		ufshcd_update_evt_hist(hba, UFS_EVT_WL_RES_ERR, (u32)ret);
	hba->clk_gating.is_suspended = false;
	ufshcd_release(hba);
	hba->pm_op_in_progress = false;
	return ret;
}

static int ufshcd_wl_runtime_suspend(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ufs_hba *hba;
	int ret;
	ktime_t start = ktime_get();

	hba = shost_priv(sdev->host);

	ret = __ufshcd_wl_suspend(hba, UFS_RUNTIME_PM);
	if (ret)
		dev_err(&sdev->sdev_gendev, "%s failed: %d\n", __func__, ret);

	trace_ufshcd_wl_runtime_suspend(dev_name(dev), ret,
		ktime_to_us(ktime_sub(ktime_get(), start)),
		hba->curr_dev_pwr_mode, hba->uic_link_state);

	return ret;
}

static int ufshcd_wl_runtime_resume(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ufs_hba *hba;
	int ret = 0;
	ktime_t start = ktime_get();

	hba = shost_priv(sdev->host);

	ret = __ufshcd_wl_resume(hba, UFS_RUNTIME_PM);
	if (ret)
		dev_err(&sdev->sdev_gendev, "%s failed: %d\n", __func__, ret);

	trace_ufshcd_wl_runtime_resume(dev_name(dev), ret,
		ktime_to_us(ktime_sub(ktime_get(), start)),
		hba->curr_dev_pwr_mode, hba->uic_link_state);

	return ret;
}
#endif

#ifdef CONFIG_PM_SLEEP
static int ufshcd_wl_suspend(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ufs_hba *hba;
	int ret = 0;
	ktime_t start = ktime_get();

	hba = shost_priv(sdev->host);
	down(&hba->host_sem);

	if (pm_runtime_suspended(dev))
		goto out;

	ret = __ufshcd_wl_suspend(hba, UFS_SYSTEM_PM);
	if (ret) {
		dev_err(&sdev->sdev_gendev, "%s failed: %d\n", __func__,  ret);
		up(&hba->host_sem);
	}

out:
	if (!ret)
		hba->is_sys_suspended = true;
	trace_ufshcd_wl_suspend(dev_name(dev), ret,
		ktime_to_us(ktime_sub(ktime_get(), start)),
		hba->curr_dev_pwr_mode, hba->uic_link_state);

	return ret;
}

static int ufshcd_wl_resume(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ufs_hba *hba;
	int ret = 0;
	ktime_t start = ktime_get();

	hba = shost_priv(sdev->host);

	if (pm_runtime_suspended(dev))
		goto out;

	ret = __ufshcd_wl_resume(hba, UFS_SYSTEM_PM);
	if (ret)
		dev_err(&sdev->sdev_gendev, "%s failed: %d\n", __func__, ret);
out:
	trace_ufshcd_wl_resume(dev_name(dev), ret,
		ktime_to_us(ktime_sub(ktime_get(), start)),
		hba->curr_dev_pwr_mode, hba->uic_link_state);
	if (!ret)
		hba->is_sys_suspended = false;
	up(&hba->host_sem);
	return ret;
}
#endif

static void ufshcd_wl_shutdown(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ufs_hba *hba;

	hba = shost_priv(sdev->host);

	down(&hba->host_sem);
	hba->shutting_down = true;
	up(&hba->host_sem);

	/* Turn on everything while shutting down */
	ufshcd_rpm_get_sync(hba);
	scsi_device_quiesce(sdev);
	shost_for_each_device(sdev, hba->host) {
		if (sdev == hba->sdev_ufs_device)
			continue;
		scsi_device_quiesce(sdev);
	}
	__ufshcd_wl_suspend(hba, UFS_SHUTDOWN_PM);
}

/**
 * ufshcd_suspend - helper function for suspend operations
 * @hba: per adapter instance
 *
 * This function will put disable irqs, turn off clocks
 * and set vreg and hba-vreg in lpm mode.
 */
static int ufshcd_suspend(struct ufs_hba *hba)
{
	int ret;

	if (!hba->is_powered)
		return 0;
	/*
	 * Disable the host irq as host controller as there won't be any
	 * host controller transaction expected till resume.
	 */
	ufshcd_disable_irq(hba);
	ret = ufshcd_setup_clocks(hba, false);
	if (ret) {
		ufshcd_enable_irq(hba);
		return ret;
	}
	if (ufshcd_is_clkgating_allowed(hba)) {
		hba->clk_gating.state = CLKS_OFF;
		trace_ufshcd_clk_gating(dev_name(hba->dev),
					hba->clk_gating.state);
	}

	ufshcd_vreg_set_lpm(hba);
	/* Put the host controller in low power mode if possible */
	ufshcd_hba_vreg_set_lpm(hba);
	return ret;
}

#ifdef CONFIG_PM
/**
 * ufshcd_resume - helper function for resume operations
 * @hba: per adapter instance
 *
 * This function basically turns on the regulators, clocks and
 * irqs of the hba.
 *
 * Returns 0 for success and non-zero for failure
 */
static int ufshcd_resume(struct ufs_hba *hba)
{
	int ret;

	if (!hba->is_powered)
		return 0;

	ufshcd_hba_vreg_set_hpm(hba);
	ret = ufshcd_vreg_set_hpm(hba);
	if (ret)
		goto out;

	/* Make sure clocks are enabled before accessing controller */
	ret = ufshcd_setup_clocks(hba, true);
	if (ret)
		goto disable_vreg;

	/* enable the host irq as host controller would be active soon */
	ufshcd_enable_irq(hba);
	goto out;

disable_vreg:
	ufshcd_vreg_set_lpm(hba);
out:
	if (ret)
		ufshcd_update_evt_hist(hba, UFS_EVT_RESUME_ERR, (u32)ret);
	return ret;
}
#endif /* CONFIG_PM */

#ifdef CONFIG_PM_SLEEP
/**
 * ufshcd_system_suspend - system suspend callback
 * @dev: Device associated with the UFS controller.
 *
 * Executed before putting the system into a sleep state in which the contents
 * of main memory are preserved.
 *
 * Returns 0 for success and non-zero for failure
 */
int ufshcd_system_suspend(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	int ret = 0;
	ktime_t start = ktime_get();

	if (pm_runtime_suspended(hba->dev))
		goto out;

	ret = ufshcd_suspend(hba);
out:
	trace_ufshcd_system_suspend(dev_name(hba->dev), ret,
		ktime_to_us(ktime_sub(ktime_get(), start)),
		hba->curr_dev_pwr_mode, hba->uic_link_state);
	return ret;
}
EXPORT_SYMBOL(ufshcd_system_suspend);

/**
 * ufshcd_system_resume - system resume callback
 * @dev: Device associated with the UFS controller.
 *
 * Executed after waking the system up from a sleep state in which the contents
 * of main memory were preserved.
 *
 * Returns 0 for success and non-zero for failure
 */
int ufshcd_system_resume(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	ktime_t start = ktime_get();
	int ret = 0;

	if (pm_runtime_suspended(hba->dev))
		goto out;

	ret = ufshcd_resume(hba);

out:
	trace_ufshcd_system_resume(dev_name(hba->dev), ret,
		ktime_to_us(ktime_sub(ktime_get(), start)),
		hba->curr_dev_pwr_mode, hba->uic_link_state);

	return ret;
}
EXPORT_SYMBOL(ufshcd_system_resume);
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM
/**
 * ufshcd_runtime_suspend - runtime suspend callback
 * @dev: Device associated with the UFS controller.
 *
 * Check the description of ufshcd_suspend() function for more details.
 *
 * Returns 0 for success and non-zero for failure
 */
int ufshcd_runtime_suspend(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	int ret;
	ktime_t start = ktime_get();

	ret = ufshcd_suspend(hba);

	trace_ufshcd_runtime_suspend(dev_name(hba->dev), ret,
		ktime_to_us(ktime_sub(ktime_get(), start)),
		hba->curr_dev_pwr_mode, hba->uic_link_state);
	return ret;
}
EXPORT_SYMBOL(ufshcd_runtime_suspend);

/**
 * ufshcd_runtime_resume - runtime resume routine
 * @dev: Device associated with the UFS controller.
 *
 * This function basically brings controller
 * to active state. Following operations are done in this function:
 *
 * 1. Turn on all the controller related clocks
 * 2. Turn ON VCC rail
 */
int ufshcd_runtime_resume(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	int ret;
	ktime_t start = ktime_get();

	ret = ufshcd_resume(hba);

	trace_ufshcd_runtime_resume(dev_name(hba->dev), ret,
		ktime_to_us(ktime_sub(ktime_get(), start)),
		hba->curr_dev_pwr_mode, hba->uic_link_state);
	return ret;
}
EXPORT_SYMBOL(ufshcd_runtime_resume);
#endif /* CONFIG_PM */

/**
 * ufshcd_shutdown - shutdown routine
 * @hba: per adapter instance
 *
 * This function would turn off both UFS device and UFS hba
 * regulators. It would also disable clocks.
 *
 * Returns 0 always to allow force shutdown even in case of errors.
 */
int ufshcd_shutdown(struct ufs_hba *hba)
{
	if (ufshcd_is_ufs_dev_poweroff(hba) && ufshcd_is_link_off(hba))
		goto out;

	pm_runtime_get_sync(hba->dev);

	ufshcd_suspend(hba);
out:
	hba->is_powered = false;
	/* allow force shutdown even in case of errors */
	return 0;
}
EXPORT_SYMBOL(ufshcd_shutdown);

/**
 * ufshcd_remove - de-allocate SCSI host and host memory space
 *		data structure memory
 * @hba: per adapter instance
 */
void ufshcd_remove(struct ufs_hba *hba)
{
	if (hba->sdev_ufs_device)
		ufshcd_rpm_get_sync(hba);
	ufs_bsg_remove(hba);
	ufshpb_remove(hba);
	ufs_sysfs_remove_nodes(hba->dev);
	blk_cleanup_queue(hba->tmf_queue);
	blk_mq_free_tag_set(&hba->tmf_tag_set);
	blk_cleanup_queue(hba->cmd_queue);
	scsi_remove_host(hba->host);
	/* disable interrupts */
	ufshcd_disable_intr(hba, hba->intr_mask);
	ufshcd_hba_stop(hba);
	ufshcd_hba_exit(hba);
}
EXPORT_SYMBOL_GPL(ufshcd_remove);

/**
 * ufshcd_dealloc_host - deallocate Host Bus Adapter (HBA)
 * @hba: pointer to Host Bus Adapter (HBA)
 */
void ufshcd_dealloc_host(struct ufs_hba *hba)
{
	scsi_host_put(hba->host);
}
EXPORT_SYMBOL_GPL(ufshcd_dealloc_host);

/**
 * ufshcd_set_dma_mask - Set dma mask based on the controller
 *			 addressing capability
 * @hba: per adapter instance
 *
 * Returns 0 for success, non-zero for failure
 */
static int ufshcd_set_dma_mask(struct ufs_hba *hba)
{
	if (hba->capabilities & MASK_64_ADDRESSING_SUPPORT) {
		if (!dma_set_mask_and_coherent(hba->dev, DMA_BIT_MASK(64)))
			return 0;
	}
	return dma_set_mask_and_coherent(hba->dev, DMA_BIT_MASK(32));
}

/**
 * ufshcd_alloc_host - allocate Host Bus Adapter (HBA)
 * @dev: pointer to device handle
 * @hba_handle: driver private handle
 * Returns 0 on success, non-zero value on failure
 */
int ufshcd_alloc_host(struct device *dev, struct ufs_hba **hba_handle)
{
	struct Scsi_Host *host;
	struct ufs_hba *hba;
	int err = 0;

	if (!dev) {
		dev_err(dev,
		"Invalid memory reference for dev is NULL\n");
		err = -ENODEV;
		goto out_error;
	}

	host = scsi_host_alloc(&ufshcd_driver_template,
				sizeof(struct ufs_hba));
	if (!host) {
		dev_err(dev, "scsi_host_alloc failed\n");
		err = -ENOMEM;
		goto out_error;
	}
	hba = shost_priv(host);
	hba->host = host;
	hba->dev = dev;
	hba->dev_ref_clk_freq = REF_CLK_FREQ_INVAL;
	hba->nop_out_timeout = NOP_OUT_TIMEOUT;
	INIT_LIST_HEAD(&hba->clk_list_head);
	spin_lock_init(&hba->outstanding_lock);

	*hba_handle = hba;

out_error:
	return err;
}
EXPORT_SYMBOL(ufshcd_alloc_host);

/* This function exists because blk_mq_alloc_tag_set() requires this. */
static blk_status_t ufshcd_queue_tmf(struct blk_mq_hw_ctx *hctx,
				     const struct blk_mq_queue_data *qd)
{
	WARN_ON_ONCE(true);
	return BLK_STS_NOTSUPP;
}

static const struct blk_mq_ops ufshcd_tmf_ops = {
	.queue_rq = ufshcd_queue_tmf,
};

/**
 * ufshcd_init - Driver initialization routine
 * @hba: per-adapter instance
 * @mmio_base: base register address
 * @irq: Interrupt line of device
 * Returns 0 on success, non-zero value on failure
 */
int ufshcd_init(struct ufs_hba *hba, void __iomem *mmio_base, unsigned int irq)
{
	int err;
	struct Scsi_Host *host = hba->host;
	struct device *dev = hba->dev;
	char eh_wq_name[sizeof("ufs_eh_wq_00")];

	/*
	 * dev_set_drvdata() must be called before any callbacks are registered
	 * that use dev_get_drvdata() (frequency scaling, clock scaling, hwmon,
	 * sysfs).
	 */
	dev_set_drvdata(dev, hba);

	if (!mmio_base) {
		dev_err(hba->dev,
		"Invalid memory reference for mmio_base is NULL\n");
		err = -ENODEV;
		goto out_error;
	}

	hba->mmio_base = mmio_base;
	hba->irq = irq;
	hba->vps = &ufs_hba_vps;

	err = ufshcd_hba_init(hba);
	if (err)
		goto out_error;

	/* Read capabilities registers */
	err = ufshcd_hba_capabilities(hba);
	if (err)
		goto out_disable;

	/* Get UFS version supported by the controller */
	hba->ufs_version = ufshcd_get_ufs_version(hba);

	/* Get Interrupt bit mask per version */
	hba->intr_mask = ufshcd_get_intr_mask(hba);

	err = ufshcd_set_dma_mask(hba);
	if (err) {
		dev_err(hba->dev, "set dma mask failed\n");
		goto out_disable;
	}

	/* Allocate memory for host memory space */
	err = ufshcd_memory_alloc(hba);
	if (err) {
		dev_err(hba->dev, "Memory allocation failed\n");
		goto out_disable;
	}

	/* Configure LRB */
	ufshcd_host_memory_configure(hba);

	host->can_queue = hba->nutrs - UFSHCD_NUM_RESERVED;
	host->cmd_per_lun = hba->nutrs - UFSHCD_NUM_RESERVED;
	host->max_id = UFSHCD_MAX_ID;
	host->max_lun = UFS_MAX_LUNS;
	host->max_channel = UFSHCD_MAX_CHANNEL;
	host->unique_id = host->host_no;
	host->max_cmd_len = UFS_CDB_SIZE;

	hba->max_pwr_info.is_valid = false;

	/* Initialize work queues */
	snprintf(eh_wq_name, sizeof(eh_wq_name), "ufs_eh_wq_%d",
		 hba->host->host_no);
	hba->eh_wq = create_singlethread_workqueue(eh_wq_name);
	if (!hba->eh_wq) {
		dev_err(hba->dev, "%s: failed to create eh workqueue\n",
			__func__);
		err = -ENOMEM;
		goto out_disable;
	}
	INIT_WORK(&hba->eh_work, ufshcd_err_handler);
	INIT_WORK(&hba->eeh_work, ufshcd_exception_event_handler);

	sema_init(&hba->host_sem, 1);

	/* Initialize UIC command mutex */
	mutex_init(&hba->uic_cmd_mutex);

	/* Initialize mutex for device management commands */
	mutex_init(&hba->dev_cmd.lock);

	/* Initialize mutex for exception event control */
	mutex_init(&hba->ee_ctrl_mutex);

	init_rwsem(&hba->clk_scaling_lock);

	ufshcd_init_clk_gating(hba);

	ufshcd_init_clk_scaling(hba);

	/*
	 * In order to avoid any spurious interrupt immediately after
	 * registering UFS controller interrupt handler, clear any pending UFS
	 * interrupt status and disable all the UFS interrupts.
	 */
	ufshcd_writel(hba, ufshcd_readl(hba, REG_INTERRUPT_STATUS),
		      REG_INTERRUPT_STATUS);
	ufshcd_writel(hba, 0, REG_INTERRUPT_ENABLE);
	/*
	 * Make sure that UFS interrupts are disabled and any pending interrupt
	 * status is cleared before registering UFS interrupt handler.
	 */
	mb();

	/* IRQ registration */
	err = devm_request_irq(dev, irq, ufshcd_intr, IRQF_SHARED, UFSHCD, hba);
	if (err) {
		dev_err(hba->dev, "request irq failed\n");
		goto out_disable;
	} else {
		hba->is_irq_enabled = true;
	}

	err = scsi_add_host(host, hba->dev);
	if (err) {
		dev_err(hba->dev, "scsi_add_host failed\n");
		goto out_disable;
	}

	hba->cmd_queue = blk_mq_init_queue(&hba->host->tag_set);
	if (IS_ERR(hba->cmd_queue)) {
		err = PTR_ERR(hba->cmd_queue);
		goto out_remove_scsi_host;
	}

	hba->tmf_tag_set = (struct blk_mq_tag_set) {
		.nr_hw_queues	= 1,
		.queue_depth	= hba->nutmrs,
		.ops		= &ufshcd_tmf_ops,
		.flags		= BLK_MQ_F_NO_SCHED,
	};
	err = blk_mq_alloc_tag_set(&hba->tmf_tag_set);
	if (err < 0)
		goto free_cmd_queue;
	hba->tmf_queue = blk_mq_init_queue(&hba->tmf_tag_set);
	if (IS_ERR(hba->tmf_queue)) {
		err = PTR_ERR(hba->tmf_queue);
		goto free_tmf_tag_set;
	}
	hba->tmf_rqs = devm_kcalloc(hba->dev, hba->nutmrs,
				    sizeof(*hba->tmf_rqs), GFP_KERNEL);
	if (!hba->tmf_rqs) {
		err = -ENOMEM;
		goto free_tmf_queue;
	}

	/* Reset the attached device */
	ufshcd_device_reset(hba);

	ufshcd_init_crypto(hba);

	/* Host controller enable */
	err = ufshcd_hba_enable(hba);
	if (err) {
		dev_err(hba->dev, "Host controller enable failed\n");
		ufshcd_print_evt_hist(hba);
		ufshcd_print_host_state(hba);
		goto free_tmf_queue;
	}

	/*
	 * Set the default power management level for runtime and system PM.
	 * Default power saving mode is to keep UFS link in Hibern8 state
	 * and UFS device in sleep state.
	 */
	hba->rpm_lvl = ufs_get_desired_pm_lvl_for_dev_link_state(
						UFS_SLEEP_PWR_MODE,
						UIC_LINK_HIBERN8_STATE);
	hba->spm_lvl = ufs_get_desired_pm_lvl_for_dev_link_state(
						UFS_SLEEP_PWR_MODE,
						UIC_LINK_HIBERN8_STATE);

	INIT_DELAYED_WORK(&hba->rpm_dev_flush_recheck_work,
			  ufshcd_rpm_dev_flush_recheck_work);

	/* Set the default auto-hiberate idle timer value to 150 ms */
	if (ufshcd_is_auto_hibern8_supported(hba) && !hba->ahit) {
		hba->ahit = FIELD_PREP(UFSHCI_AHIBERN8_TIMER_MASK, 150) |
			    FIELD_PREP(UFSHCI_AHIBERN8_SCALE_MASK, 3);
	}

	/* Hold auto suspend until async scan completes */
	pm_runtime_get_sync(dev);
	atomic_set(&hba->scsi_block_reqs_cnt, 0);
	/*
	 * We are assuming that device wasn't put in sleep/power-down
	 * state exclusively during the boot stage before kernel.
	 * This assumption helps avoid doing link startup twice during
	 * ufshcd_probe_hba().
	 */
	ufshcd_set_ufs_dev_active(hba);

	async_schedule(ufshcd_async_scan, hba);
	ufs_sysfs_add_nodes(hba->dev);

	device_enable_async_suspend(dev);
	return 0;

free_tmf_queue:
	blk_cleanup_queue(hba->tmf_queue);
free_tmf_tag_set:
	blk_mq_free_tag_set(&hba->tmf_tag_set);
free_cmd_queue:
	blk_cleanup_queue(hba->cmd_queue);
out_remove_scsi_host:
	scsi_remove_host(hba->host);
out_disable:
	hba->is_irq_enabled = false;
	ufshcd_hba_exit(hba);
out_error:
	return err;
}
EXPORT_SYMBOL_GPL(ufshcd_init);

void ufshcd_resume_complete(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);

	if (hba->complete_put) {
		ufshcd_rpm_put(hba);
		hba->complete_put = false;
	}
}
EXPORT_SYMBOL_GPL(ufshcd_resume_complete);

int ufshcd_suspend_prepare(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	int ret;

	/*
	 * SCSI assumes that runtime-pm and system-pm for scsi drivers
	 * are same. And it doesn't wake up the device for system-suspend
	 * if it's runtime suspended. But ufs doesn't follow that.
	 * Refer ufshcd_resume_complete()
	 */
	if (hba->sdev_ufs_device) {
		ret = ufshcd_rpm_get_sync(hba);
		if (ret < 0 && ret != -EACCES) {
			ufshcd_rpm_put(hba);
			return ret;
		}
		hba->complete_put = true;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(ufshcd_suspend_prepare);

#ifdef CONFIG_PM_SLEEP
static int ufshcd_wl_poweroff(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ufs_hba *hba = shost_priv(sdev->host);

	__ufshcd_wl_suspend(hba, UFS_SHUTDOWN_PM);
	return 0;
}
#endif

static int ufshcd_wl_probe(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);

	if (!is_device_wlun(sdev))
		return -ENODEV;

	blk_pm_runtime_init(sdev->request_queue, dev);
	pm_runtime_set_autosuspend_delay(dev, 0);
	pm_runtime_allow(dev);

	return  0;
}

static int ufshcd_wl_remove(struct device *dev)
{
	pm_runtime_forbid(dev);
	return 0;
}

static const struct dev_pm_ops ufshcd_wl_pm_ops = {
#ifdef CONFIG_PM_SLEEP
	.suspend = ufshcd_wl_suspend,
	.resume = ufshcd_wl_resume,
	.freeze = ufshcd_wl_suspend,
	.thaw = ufshcd_wl_resume,
	.poweroff = ufshcd_wl_poweroff,
	.restore = ufshcd_wl_resume,
#endif
	SET_RUNTIME_PM_OPS(ufshcd_wl_runtime_suspend, ufshcd_wl_runtime_resume, NULL)
};

/*
 * ufs_dev_wlun_template - describes ufs device wlun
 * ufs-device wlun - used to send pm commands
 * All luns are consumers of ufs-device wlun.
 *
 * Currently, no sd driver is present for wluns.
 * Hence the no specific pm operations are performed.
 * With ufs design, SSU should be sent to ufs-device wlun.
 * Hence register a scsi driver for ufs wluns only.
 */
static struct scsi_driver ufs_dev_wlun_template = {
	.gendrv = {
		.name = "ufs_device_wlun",
		.owner = THIS_MODULE,
		.probe = ufshcd_wl_probe,
		.remove = ufshcd_wl_remove,
		.pm = &ufshcd_wl_pm_ops,
		.shutdown = ufshcd_wl_shutdown,
	},
};

static int __init ufshcd_core_init(void)
{
	int ret;

	ufs_debugfs_init();

	ret = scsi_register_driver(&ufs_dev_wlun_template.gendrv);
	if (ret)
		ufs_debugfs_exit();
	return ret;
}

static void __exit ufshcd_core_exit(void)
{
	ufs_debugfs_exit();
	scsi_unregister_driver(&ufs_dev_wlun_template.gendrv);
}

module_init(ufshcd_core_init);
module_exit(ufshcd_core_exit);

MODULE_AUTHOR("Santosh Yaragnavi <santosh.sy@samsung.com>");
MODULE_AUTHOR("Vinayak Holikatti <h.vinayak@samsung.com>");
MODULE_DESCRIPTION("Generic UFS host controller driver Core");
MODULE_LICENSE("GPL");
MODULE_VERSION(UFSHCD_DRIVER_VERSION);
