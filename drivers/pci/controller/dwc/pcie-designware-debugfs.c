// SPDX-License-Identifier: GPL-2.0
/*
 * Synopsys DesignWare PCIe controller debugfs driver
 *
 * Copyright (C) 2025 Samsung Electronics Co., Ltd.
 *		 http://www.samsung.com
 *
 * Author: Shradha Todi <shradha.t@samsung.com>
 */

#include <linux/debugfs.h>

#include "pcie-designware.h"

#define SD_STATUS_L1LANE_REG		0xb0
#define PIPE_RXVALID			BIT(18)
#define PIPE_DETECT_LANE		BIT(17)
#define LANE_SELECT			GENMASK(3, 0)

#define ERR_INJ0_OFF			0x34
#define EINJ_VAL_DIFF			GENMASK(28, 16)
#define EINJ_VC_NUM			GENMASK(14, 12)
#define EINJ_TYPE_SHIFT			8
#define EINJ0_TYPE			GENMASK(11, 8)
#define EINJ1_TYPE			BIT(8)
#define EINJ2_TYPE			GENMASK(9, 8)
#define EINJ3_TYPE			GENMASK(10, 8)
#define EINJ4_TYPE			GENMASK(10, 8)
#define EINJ5_TYPE			BIT(8)
#define EINJ_COUNT			GENMASK(7, 0)

#define ERR_INJ_ENABLE_REG		0x30

#define RAS_DES_EVENT_COUNTER_DATA_REG	0xc

#define RAS_DES_EVENT_COUNTER_CTRL_REG	0x8
#define EVENT_COUNTER_GROUP_SELECT	GENMASK(27, 24)
#define EVENT_COUNTER_EVENT_SELECT	GENMASK(23, 16)
#define EVENT_COUNTER_LANE_SELECT	GENMASK(11, 8)
#define EVENT_COUNTER_STATUS		BIT(7)
#define EVENT_COUNTER_ENABLE		GENMASK(4, 2)
#define PER_EVENT_ON			0x3
#define PER_EVENT_OFF			0x1

#define DWC_DEBUGFS_BUF_MAX		128

/**
 * struct dwc_pcie_rasdes_info - Stores controller common information
 * @ras_cap_offset: RAS DES vendor specific extended capability offset
 * @reg_event_lock: Mutex used for RAS DES shadow event registers
 *
 * Any parameter constant to all files of the debugfs hierarchy for a single
 * controller will be stored in this struct. It is allocated and assigned to
 * controller specific struct dw_pcie during initialization.
 */
struct dwc_pcie_rasdes_info {
	u32 ras_cap_offset;
	struct mutex reg_event_lock;
};

/**
 * struct dwc_pcie_rasdes_priv - Stores file specific private data information
 * @pci: Reference to the dw_pcie structure
 * @idx: Index of specific file related information in array of structs
 *
 * All debugfs files will have this struct as its private data.
 */
struct dwc_pcie_rasdes_priv {
	struct dw_pcie *pci;
	int idx;
};

/**
 * struct dwc_pcie_err_inj - Store details about each error injection
 *			     supported by DWC RAS DES
 * @name: Name of the error that can be injected
 * @err_inj_group: Group number to which the error belongs. The value
 *		   can range from 0 to 5
 * @err_inj_type: Each group can have multiple types of error
 */
struct dwc_pcie_err_inj {
	const char *name;
	u32 err_inj_group;
	u32 err_inj_type;
};

static const struct dwc_pcie_err_inj err_inj_list[] = {
	{"tx_lcrc", 0x0, 0x0},
	{"b16_crc_dllp", 0x0, 0x1},
	{"b16_crc_upd_fc", 0x0, 0x2},
	{"tx_ecrc", 0x0, 0x3},
	{"fcrc_tlp", 0x0, 0x4},
	{"parity_tsos", 0x0, 0x5},
	{"parity_skpos", 0x0, 0x6},
	{"rx_lcrc", 0x0, 0x8},
	{"rx_ecrc", 0x0, 0xb},
	{"tlp_err_seq", 0x1, 0x0},
	{"ack_nak_dllp_seq", 0x1, 0x1},
	{"ack_nak_dllp", 0x2, 0x0},
	{"upd_fc_dllp", 0x2, 0x1},
	{"nak_dllp", 0x2, 0x2},
	{"inv_sync_hdr_sym", 0x3, 0x0},
	{"com_pad_ts1", 0x3, 0x1},
	{"com_pad_ts2", 0x3, 0x2},
	{"com_fts", 0x3, 0x3},
	{"com_idl", 0x3, 0x4},
	{"end_edb", 0x3, 0x5},
	{"stp_sdp", 0x3, 0x6},
	{"com_skp", 0x3, 0x7},
	{"posted_tlp_hdr", 0x4, 0x0},
	{"non_post_tlp_hdr", 0x4, 0x1},
	{"cmpl_tlp_hdr", 0x4, 0x2},
	{"posted_tlp_data", 0x4, 0x4},
	{"non_post_tlp_data", 0x4, 0x5},
	{"cmpl_tlp_data", 0x4, 0x6},
	{"duplicate_tlp", 0x5, 0x0},
	{"nullified_tlp", 0x5, 0x1},
};

static const u32 err_inj_type_mask[] = {
	EINJ0_TYPE,
	EINJ1_TYPE,
	EINJ2_TYPE,
	EINJ3_TYPE,
	EINJ4_TYPE,
	EINJ5_TYPE,
};

/**
 * struct dwc_pcie_event_counter - Store details about each event counter
 *				   supported in DWC RAS DES
 * @name: Name of the error counter
 * @group_no: Group number that the event belongs to. The value can range
 *	      from 0 to 4
 * @event_no: Event number of the particular event. The value ranges are:
 *		Group 0: 0 - 10
 *		Group 1: 5 - 13
 *		Group 2: 0 - 7
 *		Group 3: 0 - 5
 *		Group 4: 0 - 1
 */
struct dwc_pcie_event_counter {
	const char *name;
	u32 group_no;
	u32 event_no;
};

static const struct dwc_pcie_event_counter event_list[] = {
	{"ebuf_overflow", 0x0, 0x0},
	{"ebuf_underrun", 0x0, 0x1},
	{"decode_err", 0x0, 0x2},
	{"running_disparity_err", 0x0, 0x3},
	{"skp_os_parity_err", 0x0, 0x4},
	{"sync_header_err", 0x0, 0x5},
	{"rx_valid_deassertion", 0x0, 0x6},
	{"ctl_skp_os_parity_err", 0x0, 0x7},
	{"retimer_parity_err_1st", 0x0, 0x8},
	{"retimer_parity_err_2nd", 0x0, 0x9},
	{"margin_crc_parity_err", 0x0, 0xA},
	{"detect_ei_infer", 0x1, 0x5},
	{"receiver_err", 0x1, 0x6},
	{"rx_recovery_req", 0x1, 0x7},
	{"n_fts_timeout", 0x1, 0x8},
	{"framing_err", 0x1, 0x9},
	{"deskew_err", 0x1, 0xa},
	{"framing_err_in_l0", 0x1, 0xc},
	{"deskew_uncompleted_err", 0x1, 0xd},
	{"bad_tlp", 0x2, 0x0},
	{"lcrc_err", 0x2, 0x1},
	{"bad_dllp", 0x2, 0x2},
	{"replay_num_rollover", 0x2, 0x3},
	{"replay_timeout", 0x2, 0x4},
	{"rx_nak_dllp", 0x2, 0x5},
	{"tx_nak_dllp", 0x2, 0x6},
	{"retry_tlp", 0x2, 0x7},
	{"fc_timeout", 0x3, 0x0},
	{"poisoned_tlp", 0x3, 0x1},
	{"ecrc_error", 0x3, 0x2},
	{"unsupported_request", 0x3, 0x3},
	{"completer_abort", 0x3, 0x4},
	{"completion_timeout", 0x3, 0x5},
	{"ebuf_skp_add", 0x4, 0x0},
	{"ebuf_skp_del", 0x4, 0x1},
};

static ssize_t lane_detect_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	struct dw_pcie *pci = file->private_data;
	struct dwc_pcie_rasdes_info *rinfo = pci->debugfs->rasdes_info;
	char debugfs_buf[DWC_DEBUGFS_BUF_MAX];
	ssize_t pos;
	u32 val;

	val = dw_pcie_readl_dbi(pci, rinfo->ras_cap_offset + SD_STATUS_L1LANE_REG);
	val = FIELD_GET(PIPE_DETECT_LANE, val);
	if (val)
		pos = scnprintf(debugfs_buf, DWC_DEBUGFS_BUF_MAX, "Lane Detected\n");
	else
		pos = scnprintf(debugfs_buf, DWC_DEBUGFS_BUF_MAX, "Lane Undetected\n");

	return simple_read_from_buffer(buf, count, ppos, debugfs_buf, pos);
}

static ssize_t lane_detect_write(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct dw_pcie *pci = file->private_data;
	struct dwc_pcie_rasdes_info *rinfo = pci->debugfs->rasdes_info;
	u32 lane, val;

	val = kstrtou32_from_user(buf, count, 0, &lane);
	if (val)
		return val;

	val = dw_pcie_readl_dbi(pci, rinfo->ras_cap_offset + SD_STATUS_L1LANE_REG);
	val &= ~(LANE_SELECT);
	val |= FIELD_PREP(LANE_SELECT, lane);
	dw_pcie_writel_dbi(pci, rinfo->ras_cap_offset + SD_STATUS_L1LANE_REG, val);

	return count;
}

static ssize_t rx_valid_read(struct file *file, char __user *buf,
			     size_t count, loff_t *ppos)
{
	struct dw_pcie *pci = file->private_data;
	struct dwc_pcie_rasdes_info *rinfo = pci->debugfs->rasdes_info;
	char debugfs_buf[DWC_DEBUGFS_BUF_MAX];
	ssize_t pos;
	u32 val;

	val = dw_pcie_readl_dbi(pci, rinfo->ras_cap_offset + SD_STATUS_L1LANE_REG);
	val = FIELD_GET(PIPE_RXVALID, val);
	if (val)
		pos = scnprintf(debugfs_buf, DWC_DEBUGFS_BUF_MAX, "RX Valid\n");
	else
		pos = scnprintf(debugfs_buf, DWC_DEBUGFS_BUF_MAX, "RX Invalid\n");

	return simple_read_from_buffer(buf, count, ppos, debugfs_buf, pos);
}

static ssize_t rx_valid_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	return lane_detect_write(file, buf, count, ppos);
}

static ssize_t err_inj_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *ppos)
{
	struct dwc_pcie_rasdes_priv *pdata = file->private_data;
	struct dw_pcie *pci = pdata->pci;
	struct dwc_pcie_rasdes_info *rinfo = pci->debugfs->rasdes_info;
	u32 val, counter, vc_num, err_group, type_mask;
	int val_diff = 0;
	char *kern_buf;

	err_group = err_inj_list[pdata->idx].err_inj_group;
	type_mask = err_inj_type_mask[err_group];

	kern_buf = memdup_user_nul(buf, count);
	if (IS_ERR(kern_buf))
		return PTR_ERR(kern_buf);

	if (err_group == 4) {
		val = sscanf(kern_buf, "%u %d %u", &counter, &val_diff, &vc_num);
		if ((val != 3) || (val_diff < -4095 || val_diff > 4095)) {
			kfree(kern_buf);
			return -EINVAL;
		}
	} else if (err_group == 1) {
		val = sscanf(kern_buf, "%u %d", &counter, &val_diff);
		if ((val != 2) || (val_diff < -4095 || val_diff > 4095)) {
			kfree(kern_buf);
			return -EINVAL;
		}
	} else {
		val = kstrtou32(kern_buf, 0, &counter);
		if (val) {
			kfree(kern_buf);
			return val;
		}
	}

	val = dw_pcie_readl_dbi(pci, rinfo->ras_cap_offset + ERR_INJ0_OFF + (0x4 * err_group));
	val &= ~(type_mask | EINJ_COUNT);
	val |= ((err_inj_list[pdata->idx].err_inj_type << EINJ_TYPE_SHIFT) & type_mask);
	val |= FIELD_PREP(EINJ_COUNT, counter);

	if (err_group == 1 || err_group == 4) {
		val &= ~(EINJ_VAL_DIFF);
		val |= FIELD_PREP(EINJ_VAL_DIFF, val_diff);
	}
	if (err_group == 4) {
		val &= ~(EINJ_VC_NUM);
		val |= FIELD_PREP(EINJ_VC_NUM, vc_num);
	}

	dw_pcie_writel_dbi(pci, rinfo->ras_cap_offset + ERR_INJ0_OFF + (0x4 * err_group), val);
	dw_pcie_writel_dbi(pci, rinfo->ras_cap_offset + ERR_INJ_ENABLE_REG, (0x1 << err_group));

	kfree(kern_buf);
	return count;
}

static void set_event_number(struct dwc_pcie_rasdes_priv *pdata,
			     struct dw_pcie *pci, struct dwc_pcie_rasdes_info *rinfo)
{
	u32 val;

	val = dw_pcie_readl_dbi(pci, rinfo->ras_cap_offset + RAS_DES_EVENT_COUNTER_CTRL_REG);
	val &= ~EVENT_COUNTER_ENABLE;
	val &= ~(EVENT_COUNTER_GROUP_SELECT | EVENT_COUNTER_EVENT_SELECT);
	val |= FIELD_PREP(EVENT_COUNTER_GROUP_SELECT, event_list[pdata->idx].group_no);
	val |= FIELD_PREP(EVENT_COUNTER_EVENT_SELECT, event_list[pdata->idx].event_no);
	dw_pcie_writel_dbi(pci, rinfo->ras_cap_offset + RAS_DES_EVENT_COUNTER_CTRL_REG, val);
}

static ssize_t counter_enable_read(struct file *file, char __user *buf,
				   size_t count, loff_t *ppos)
{
	struct dwc_pcie_rasdes_priv *pdata = file->private_data;
	struct dw_pcie *pci = pdata->pci;
	struct dwc_pcie_rasdes_info *rinfo = pci->debugfs->rasdes_info;
	char debugfs_buf[DWC_DEBUGFS_BUF_MAX];
	ssize_t pos;
	u32 val;

	mutex_lock(&rinfo->reg_event_lock);
	set_event_number(pdata, pci, rinfo);
	val = dw_pcie_readl_dbi(pci, rinfo->ras_cap_offset + RAS_DES_EVENT_COUNTER_CTRL_REG);
	mutex_unlock(&rinfo->reg_event_lock);
	val = FIELD_GET(EVENT_COUNTER_STATUS, val);
	if (val)
		pos = scnprintf(debugfs_buf, DWC_DEBUGFS_BUF_MAX, "Counter Enabled\n");
	else
		pos = scnprintf(debugfs_buf, DWC_DEBUGFS_BUF_MAX, "Counter Disabled\n");

	return simple_read_from_buffer(buf, count, ppos, debugfs_buf, pos);
}

static ssize_t counter_enable_write(struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	struct dwc_pcie_rasdes_priv *pdata = file->private_data;
	struct dw_pcie *pci = pdata->pci;
	struct dwc_pcie_rasdes_info *rinfo = pci->debugfs->rasdes_info;
	u32 val, enable;

	val = kstrtou32_from_user(buf, count, 0, &enable);
	if (val)
		return val;

	mutex_lock(&rinfo->reg_event_lock);
	set_event_number(pdata, pci, rinfo);
	val = dw_pcie_readl_dbi(pci, rinfo->ras_cap_offset + RAS_DES_EVENT_COUNTER_CTRL_REG);
	if (enable)
		val |= FIELD_PREP(EVENT_COUNTER_ENABLE, PER_EVENT_ON);
	else
		val |= FIELD_PREP(EVENT_COUNTER_ENABLE, PER_EVENT_OFF);

	dw_pcie_writel_dbi(pci, rinfo->ras_cap_offset + RAS_DES_EVENT_COUNTER_CTRL_REG, val);

	/*
	 * While enabling the counter, always read the status back to check if
	 * it is enabled or not. Return error if it is not enabled to let the
	 * users know that the counter is not supported on the platform.
	 */
	if (enable) {
		val = dw_pcie_readl_dbi(pci, rinfo->ras_cap_offset +
					RAS_DES_EVENT_COUNTER_CTRL_REG);
		if (!FIELD_GET(EVENT_COUNTER_STATUS, val)) {
			mutex_unlock(&rinfo->reg_event_lock);
			return -EOPNOTSUPP;
		}
	}

	mutex_unlock(&rinfo->reg_event_lock);

	return count;
}

static ssize_t counter_lane_read(struct file *file, char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct dwc_pcie_rasdes_priv *pdata = file->private_data;
	struct dw_pcie *pci = pdata->pci;
	struct dwc_pcie_rasdes_info *rinfo = pci->debugfs->rasdes_info;
	char debugfs_buf[DWC_DEBUGFS_BUF_MAX];
	ssize_t pos;
	u32 val;

	mutex_lock(&rinfo->reg_event_lock);
	set_event_number(pdata, pci, rinfo);
	val = dw_pcie_readl_dbi(pci, rinfo->ras_cap_offset + RAS_DES_EVENT_COUNTER_CTRL_REG);
	mutex_unlock(&rinfo->reg_event_lock);
	val = FIELD_GET(EVENT_COUNTER_LANE_SELECT, val);
	pos = scnprintf(debugfs_buf, DWC_DEBUGFS_BUF_MAX, "Lane: %d\n", val);

	return simple_read_from_buffer(buf, count, ppos, debugfs_buf, pos);
}

static ssize_t counter_lane_write(struct file *file, const char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct dwc_pcie_rasdes_priv *pdata = file->private_data;
	struct dw_pcie *pci = pdata->pci;
	struct dwc_pcie_rasdes_info *rinfo = pci->debugfs->rasdes_info;
	u32 val, lane;

	val = kstrtou32_from_user(buf, count, 0, &lane);
	if (val)
		return val;

	mutex_lock(&rinfo->reg_event_lock);
	set_event_number(pdata, pci, rinfo);
	val = dw_pcie_readl_dbi(pci, rinfo->ras_cap_offset + RAS_DES_EVENT_COUNTER_CTRL_REG);
	val &= ~(EVENT_COUNTER_LANE_SELECT);
	val |= FIELD_PREP(EVENT_COUNTER_LANE_SELECT, lane);
	dw_pcie_writel_dbi(pci, rinfo->ras_cap_offset + RAS_DES_EVENT_COUNTER_CTRL_REG, val);
	mutex_unlock(&rinfo->reg_event_lock);

	return count;
}

static ssize_t counter_value_read(struct file *file, char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct dwc_pcie_rasdes_priv *pdata = file->private_data;
	struct dw_pcie *pci = pdata->pci;
	struct dwc_pcie_rasdes_info *rinfo = pci->debugfs->rasdes_info;
	char debugfs_buf[DWC_DEBUGFS_BUF_MAX];
	ssize_t pos;
	u32 val;

	mutex_lock(&rinfo->reg_event_lock);
	set_event_number(pdata, pci, rinfo);
	val = dw_pcie_readl_dbi(pci, rinfo->ras_cap_offset + RAS_DES_EVENT_COUNTER_DATA_REG);
	mutex_unlock(&rinfo->reg_event_lock);
	pos = scnprintf(debugfs_buf, DWC_DEBUGFS_BUF_MAX, "Counter value: %d\n", val);

	return simple_read_from_buffer(buf, count, ppos, debugfs_buf, pos);
}

static const char *ltssm_status_string(enum dw_pcie_ltssm ltssm)
{
	const char *str;

	switch (ltssm) {
#define DW_PCIE_LTSSM_NAME(n) case n: str = #n; break
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_DETECT_QUIET);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_DETECT_ACT);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_POLL_ACTIVE);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_POLL_COMPLIANCE);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_POLL_CONFIG);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_PRE_DETECT_QUIET);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_DETECT_WAIT);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_CFG_LINKWD_START);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_CFG_LINKWD_ACEPT);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_CFG_LANENUM_WAI);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_CFG_LANENUM_ACEPT);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_CFG_COMPLETE);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_CFG_IDLE);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_RCVRY_LOCK);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_RCVRY_SPEED);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_RCVRY_RCVRCFG);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_RCVRY_IDLE);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_L0);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_L0S);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_L123_SEND_EIDLE);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_L1_IDLE);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_L2_IDLE);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_L2_WAKE);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_DISABLED_ENTRY);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_DISABLED_IDLE);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_DISABLED);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_LPBK_ENTRY);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_LPBK_ACTIVE);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_LPBK_EXIT);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_LPBK_EXIT_TIMEOUT);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_HOT_RESET_ENTRY);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_HOT_RESET);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_RCVRY_EQ0);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_RCVRY_EQ1);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_RCVRY_EQ2);
	DW_PCIE_LTSSM_NAME(DW_PCIE_LTSSM_RCVRY_EQ3);
	default:
		str = "DW_PCIE_LTSSM_UNKNOWN";
		break;
	}

	return str + strlen("DW_PCIE_LTSSM_");
}

static int ltssm_status_show(struct seq_file *s, void *v)
{
	struct dw_pcie *pci = s->private;
	enum dw_pcie_ltssm val;

	val = dw_pcie_get_ltssm(pci);
	seq_printf(s, "%s (0x%02x)\n", ltssm_status_string(val), val);

	return 0;
}

static int ltssm_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, ltssm_status_show, inode->i_private);
}

#define dwc_debugfs_create(name)			\
debugfs_create_file(#name, 0644, rasdes_debug, pci,	\
			&dbg_ ## name ## _fops)

#define DWC_DEBUGFS_FOPS(name)					\
static const struct file_operations dbg_ ## name ## _fops = {	\
	.open = simple_open,				\
	.read = name ## _read,				\
	.write = name ## _write				\
}

DWC_DEBUGFS_FOPS(lane_detect);
DWC_DEBUGFS_FOPS(rx_valid);

static const struct file_operations dwc_pcie_err_inj_ops = {
	.open = simple_open,
	.write = err_inj_write,
};

static const struct file_operations dwc_pcie_counter_enable_ops = {
	.open = simple_open,
	.read = counter_enable_read,
	.write = counter_enable_write,
};

static const struct file_operations dwc_pcie_counter_lane_ops = {
	.open = simple_open,
	.read = counter_lane_read,
	.write = counter_lane_write,
};

static const struct file_operations dwc_pcie_counter_value_ops = {
	.open = simple_open,
	.read = counter_value_read,
};

static const struct file_operations dwc_pcie_ltssm_status_ops = {
	.open = ltssm_status_open,
	.read = seq_read,
};

static void dwc_pcie_rasdes_debugfs_deinit(struct dw_pcie *pci)
{
	struct dwc_pcie_rasdes_info *rinfo = pci->debugfs->rasdes_info;

	mutex_destroy(&rinfo->reg_event_lock);
}

static int dwc_pcie_rasdes_debugfs_init(struct dw_pcie *pci, struct dentry *dir)
{
	struct dentry *rasdes_debug, *rasdes_err_inj;
	struct dentry *rasdes_event_counter, *rasdes_events;
	struct dwc_pcie_rasdes_info *rasdes_info;
	struct dwc_pcie_rasdes_priv *priv_tmp;
	struct device *dev = pci->dev;
	int ras_cap, i, ret;

	/*
	 * If a given SoC has no RAS DES capability, the following call is
	 * bound to return an error, breaking some existing platforms. So,
	 * return 0 here, as this is not necessarily an error.
	 */
	ras_cap = dw_pcie_find_rasdes_capability(pci);
	if (!ras_cap) {
		dev_dbg(dev, "no RAS DES capability available\n");
		return 0;
	}

	rasdes_info = devm_kzalloc(dev, sizeof(*rasdes_info), GFP_KERNEL);
	if (!rasdes_info)
		return -ENOMEM;

	/* Create subdirectories for Debug, Error Injection, Statistics. */
	rasdes_debug = debugfs_create_dir("rasdes_debug", dir);
	rasdes_err_inj = debugfs_create_dir("rasdes_err_inj", dir);
	rasdes_event_counter = debugfs_create_dir("rasdes_event_counter", dir);

	mutex_init(&rasdes_info->reg_event_lock);
	rasdes_info->ras_cap_offset = ras_cap;
	pci->debugfs->rasdes_info = rasdes_info;

	/* Create debugfs files for Debug subdirectory. */
	dwc_debugfs_create(lane_detect);
	dwc_debugfs_create(rx_valid);

	/* Create debugfs files for Error Injection subdirectory. */
	for (i = 0; i < ARRAY_SIZE(err_inj_list); i++) {
		priv_tmp = devm_kzalloc(dev, sizeof(*priv_tmp), GFP_KERNEL);
		if (!priv_tmp) {
			ret = -ENOMEM;
			goto err_deinit;
		}

		priv_tmp->idx = i;
		priv_tmp->pci = pci;
		debugfs_create_file(err_inj_list[i].name, 0200, rasdes_err_inj, priv_tmp,
				    &dwc_pcie_err_inj_ops);
	}

	/* Create debugfs files for Statistical Counter subdirectory. */
	for (i = 0; i < ARRAY_SIZE(event_list); i++) {
		priv_tmp = devm_kzalloc(dev, sizeof(*priv_tmp), GFP_KERNEL);
		if (!priv_tmp) {
			ret = -ENOMEM;
			goto err_deinit;
		}

		priv_tmp->idx = i;
		priv_tmp->pci = pci;
		rasdes_events = debugfs_create_dir(event_list[i].name, rasdes_event_counter);
		if (event_list[i].group_no == 0 || event_list[i].group_no == 4) {
			debugfs_create_file("lane_select", 0644, rasdes_events,
					    priv_tmp, &dwc_pcie_counter_lane_ops);
		}
		debugfs_create_file("counter_value", 0444, rasdes_events, priv_tmp,
				    &dwc_pcie_counter_value_ops);
		debugfs_create_file("counter_enable", 0644, rasdes_events, priv_tmp,
				    &dwc_pcie_counter_enable_ops);
	}

	return 0;

err_deinit:
	dwc_pcie_rasdes_debugfs_deinit(pci);
	return ret;
}

static void dwc_pcie_ltssm_debugfs_init(struct dw_pcie *pci, struct dentry *dir)
{
	debugfs_create_file("ltssm_status", 0444, dir, pci,
			    &dwc_pcie_ltssm_status_ops);
}

static int dw_pcie_ptm_check_capability(void *drvdata)
{
	struct dw_pcie *pci = drvdata;

	pci->ptm_vsec_offset = dw_pcie_find_ptm_capability(pci);

	return pci->ptm_vsec_offset;
}

static int dw_pcie_ptm_context_update_write(void *drvdata, u8 mode)
{
	struct dw_pcie *pci = drvdata;
	u32 val;

	if (mode == PCIE_PTM_CONTEXT_UPDATE_AUTO) {
		val = dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_RES_REQ_CTRL);
		val |= PTM_REQ_AUTO_UPDATE_ENABLED;
		dw_pcie_writel_dbi(pci, pci->ptm_vsec_offset + PTM_RES_REQ_CTRL, val);
	} else if (mode == PCIE_PTM_CONTEXT_UPDATE_MANUAL) {
		val = dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_RES_REQ_CTRL);
		val &= ~PTM_REQ_AUTO_UPDATE_ENABLED;
		val |= PTM_REQ_START_UPDATE;
		dw_pcie_writel_dbi(pci, pci->ptm_vsec_offset + PTM_RES_REQ_CTRL, val);
	} else {
		return -EINVAL;
	}

	return 0;
}

static int dw_pcie_ptm_context_update_read(void *drvdata, u8 *mode)
{
	struct dw_pcie *pci = drvdata;
	u32 val;

	val = dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_RES_REQ_CTRL);
	if (FIELD_GET(PTM_REQ_AUTO_UPDATE_ENABLED, val))
		*mode = PCIE_PTM_CONTEXT_UPDATE_AUTO;
	else
		/*
		 * PTM_REQ_START_UPDATE is a self clearing register bit. So if
		 * PTM_REQ_AUTO_UPDATE_ENABLED is not set, then it implies that
		 * manual update is used.
		 */
		*mode = PCIE_PTM_CONTEXT_UPDATE_MANUAL;

	return 0;
}

static int dw_pcie_ptm_context_valid_write(void *drvdata, bool valid)
{
	struct dw_pcie *pci = drvdata;
	u32 val;

	if (valid) {
		val = dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_RES_REQ_CTRL);
		val |= PTM_RES_CCONTEXT_VALID;
		dw_pcie_writel_dbi(pci, pci->ptm_vsec_offset + PTM_RES_REQ_CTRL, val);
	} else {
		val = dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_RES_REQ_CTRL);
		val &= ~PTM_RES_CCONTEXT_VALID;
		dw_pcie_writel_dbi(pci, pci->ptm_vsec_offset + PTM_RES_REQ_CTRL, val);
	}

	return 0;
}

static int dw_pcie_ptm_context_valid_read(void *drvdata, bool *valid)
{
	struct dw_pcie *pci = drvdata;
	u32 val;

	val = dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_RES_REQ_CTRL);
	*valid = !!FIELD_GET(PTM_RES_CCONTEXT_VALID, val);

	return 0;
}

static int dw_pcie_ptm_local_clock_read(void *drvdata, u64 *clock)
{
	struct dw_pcie *pci = drvdata;
	u32 msb, lsb;

	do {
		msb = dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_LOCAL_MSB);
		lsb = dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_LOCAL_LSB);
	} while (msb != dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_LOCAL_MSB));

	*clock = ((u64) msb) << 32 | lsb;

	return 0;
}

static int dw_pcie_ptm_master_clock_read(void *drvdata, u64 *clock)
{
	struct dw_pcie *pci = drvdata;
	u32 msb, lsb;

	do {
		msb = dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_MASTER_MSB);
		lsb = dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_MASTER_LSB);
	} while (msb != dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_MASTER_MSB));

	*clock = ((u64) msb) << 32 | lsb;

	return 0;
}

static int dw_pcie_ptm_t1_read(void *drvdata, u64 *clock)
{
	struct dw_pcie *pci = drvdata;
	u32 msb, lsb;

	do {
		msb = dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_T1_T2_MSB);
		lsb = dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_T1_T2_LSB);
	} while (msb != dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_T1_T2_MSB));

	*clock = ((u64) msb) << 32 | lsb;

	return 0;
}

static int dw_pcie_ptm_t2_read(void *drvdata, u64 *clock)
{
	struct dw_pcie *pci = drvdata;
	u32 msb, lsb;

	do {
		msb = dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_T1_T2_MSB);
		lsb = dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_T1_T2_LSB);
	} while (msb != dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_T1_T2_MSB));

	*clock = ((u64) msb) << 32 | lsb;

	return 0;
}

static int dw_pcie_ptm_t3_read(void *drvdata, u64 *clock)
{
	struct dw_pcie *pci = drvdata;
	u32 msb, lsb;

	do {
		msb = dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_T3_T4_MSB);
		lsb = dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_T3_T4_LSB);
	} while (msb != dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_T3_T4_MSB));

	*clock = ((u64) msb) << 32 | lsb;

	return 0;
}

static int dw_pcie_ptm_t4_read(void *drvdata, u64 *clock)
{
	struct dw_pcie *pci = drvdata;
	u32 msb, lsb;

	do {
		msb = dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_T3_T4_MSB);
		lsb = dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_T3_T4_LSB);
	} while (msb != dw_pcie_readl_dbi(pci, pci->ptm_vsec_offset + PTM_T3_T4_MSB));

	*clock = ((u64) msb) << 32 | lsb;

	return 0;
}

static bool dw_pcie_ptm_context_update_visible(void *drvdata)
{
	struct dw_pcie *pci = drvdata;

	return pci->mode == DW_PCIE_EP_TYPE;
}

static bool dw_pcie_ptm_context_valid_visible(void *drvdata)
{
	struct dw_pcie *pci = drvdata;

	return pci->mode == DW_PCIE_RC_TYPE;
}

static bool dw_pcie_ptm_local_clock_visible(void *drvdata)
{
	/* PTM local clock is always visible */
	return true;
}

static bool dw_pcie_ptm_master_clock_visible(void *drvdata)
{
	struct dw_pcie *pci = drvdata;

	return pci->mode == DW_PCIE_EP_TYPE;
}

static bool dw_pcie_ptm_t1_visible(void *drvdata)
{
	struct dw_pcie *pci = drvdata;

	return pci->mode == DW_PCIE_EP_TYPE;
}

static bool dw_pcie_ptm_t2_visible(void *drvdata)
{
	struct dw_pcie *pci = drvdata;

	return pci->mode == DW_PCIE_RC_TYPE;
}

static bool dw_pcie_ptm_t3_visible(void *drvdata)
{
	struct dw_pcie *pci = drvdata;

	return pci->mode == DW_PCIE_RC_TYPE;
}

static bool dw_pcie_ptm_t4_visible(void *drvdata)
{
	struct dw_pcie *pci = drvdata;

	return pci->mode == DW_PCIE_EP_TYPE;
}

static const struct pcie_ptm_ops dw_pcie_ptm_ops = {
	.check_capability = dw_pcie_ptm_check_capability,
	.context_update_write = dw_pcie_ptm_context_update_write,
	.context_update_read = dw_pcie_ptm_context_update_read,
	.context_valid_write = dw_pcie_ptm_context_valid_write,
	.context_valid_read = dw_pcie_ptm_context_valid_read,
	.local_clock_read = dw_pcie_ptm_local_clock_read,
	.master_clock_read = dw_pcie_ptm_master_clock_read,
	.t1_read = dw_pcie_ptm_t1_read,
	.t2_read = dw_pcie_ptm_t2_read,
	.t3_read = dw_pcie_ptm_t3_read,
	.t4_read = dw_pcie_ptm_t4_read,
	.context_update_visible = dw_pcie_ptm_context_update_visible,
	.context_valid_visible = dw_pcie_ptm_context_valid_visible,
	.local_clock_visible = dw_pcie_ptm_local_clock_visible,
	.master_clock_visible = dw_pcie_ptm_master_clock_visible,
	.t1_visible = dw_pcie_ptm_t1_visible,
	.t2_visible = dw_pcie_ptm_t2_visible,
	.t3_visible = dw_pcie_ptm_t3_visible,
	.t4_visible = dw_pcie_ptm_t4_visible,
};

void dwc_pcie_debugfs_deinit(struct dw_pcie *pci)
{
	if (!pci->debugfs)
		return;

	pcie_ptm_destroy_debugfs(pci->ptm_debugfs);
	dwc_pcie_rasdes_debugfs_deinit(pci);
	debugfs_remove_recursive(pci->debugfs->debug_dir);
}

void dwc_pcie_debugfs_init(struct dw_pcie *pci, enum dw_pcie_device_mode mode)
{
	char dirname[DWC_DEBUGFS_BUF_MAX];
	struct device *dev = pci->dev;
	struct debugfs_info *debugfs;
	struct dentry *dir;
	int err;

	/* Create main directory for each platform driver. */
	snprintf(dirname, DWC_DEBUGFS_BUF_MAX, "dwc_pcie_%s", dev_name(dev));
	dir = debugfs_create_dir(dirname, NULL);
	debugfs = devm_kzalloc(dev, sizeof(*debugfs), GFP_KERNEL);
	if (!debugfs)
		return;

	debugfs->debug_dir = dir;
	pci->debugfs = debugfs;
	err = dwc_pcie_rasdes_debugfs_init(pci, dir);
	if (err)
		dev_err(dev, "failed to initialize RAS DES debugfs, err=%d\n",
			err);

	dwc_pcie_ltssm_debugfs_init(pci, dir);

	pci->mode = mode;
	pci->ptm_debugfs = pcie_ptm_create_debugfs(pci->dev, pci,
						   &dw_pcie_ptm_ops);
}
