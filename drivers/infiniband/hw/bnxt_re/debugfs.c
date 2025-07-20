// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Copyright (c) 2024, Broadcom. All rights reserved.  The term
 * Broadcom refers to Broadcom Limited and/or its subsidiaries.
 *
 * Description: Debugfs component of the bnxt_re driver
 */

#include <linux/debugfs.h>
#include <linux/pci.h>
#include <rdma/ib_addr.h>

#include "bnxt_ulp.h"
#include "roce_hsi.h"
#include "qplib_res.h"
#include "qplib_sp.h"
#include "qplib_fp.h"
#include "qplib_rcfw.h"
#include "bnxt_re.h"
#include "ib_verbs.h"
#include "debugfs.h"

static struct dentry *bnxt_re_debugfs_root;

static const char * const bnxt_re_cc_gen0_name[] = {
	"enable_cc",
	"run_avg_weight_g",
	"num_phase_per_state",
	"init_cr",
	"init_tr",
	"tos_ecn",
	"tos_dscp",
	"alt_vlan_pcp",
	"alt_vlan_dscp",
	"rtt",
	"cc_mode",
	"tcp_cp",
	"tx_queue",
	"inactivity_cp",
};

static inline const char *bnxt_re_qp_state_str(u8 state)
{
	switch (state) {
	case CMDQ_MODIFY_QP_NEW_STATE_RESET:
		return "RST";
	case CMDQ_MODIFY_QP_NEW_STATE_INIT:
		return "INIT";
	case CMDQ_MODIFY_QP_NEW_STATE_RTR:
		return "RTR";
	case CMDQ_MODIFY_QP_NEW_STATE_RTS:
		return "RTS";
	case CMDQ_MODIFY_QP_NEW_STATE_SQE:
		return "SQER";
	case CMDQ_MODIFY_QP_NEW_STATE_SQD:
		return "SQD";
	case CMDQ_MODIFY_QP_NEW_STATE_ERR:
		return "ERR";
	default:
		return "Invalid QP state";
	}
}

static inline const char *bnxt_re_qp_type_str(u8 type)
{
	switch (type) {
	case CMDQ_CREATE_QP1_TYPE_GSI: return "QP1";
	case CMDQ_CREATE_QP_TYPE_GSI: return "QP1";
	case CMDQ_CREATE_QP_TYPE_RC: return "RC";
	case CMDQ_CREATE_QP_TYPE_UD: return "UD";
	case CMDQ_CREATE_QP_TYPE_RAW_ETHERTYPE: return "RAW_ETHERTYPE";
	default: return "Invalid transport type";
	}
}

static ssize_t qp_info_read(struct file *filep,
			    char __user *buffer,
			    size_t count, loff_t *ppos)
{
	struct bnxt_re_qp *qp = filep->private_data;
	char *buf;
	int len;

	if (*ppos)
		return 0;

	buf = kasprintf(GFP_KERNEL,
			"QPN\t\t: %d\n"
			"transport\t: %s\n"
			"state\t\t: %s\n"
			"mtu\t\t: %d\n"
			"timeout\t\t: %d\n"
			"remote QPN\t: %d\n",
			qp->qplib_qp.id,
			bnxt_re_qp_type_str(qp->qplib_qp.type),
			bnxt_re_qp_state_str(qp->qplib_qp.state),
			qp->qplib_qp.mtu,
			qp->qplib_qp.timeout,
			qp->qplib_qp.dest_qpn);
	if (!buf)
		return -ENOMEM;
	if (count < strlen(buf)) {
		kfree(buf);
		return -ENOSPC;
	}
	len = simple_read_from_buffer(buffer, count, ppos, buf, strlen(buf));
	kfree(buf);
	return len;
}

static const struct file_operations debugfs_qp_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = qp_info_read,
};

void bnxt_re_debug_add_qpinfo(struct bnxt_re_dev *rdev, struct bnxt_re_qp *qp)
{
	char resn[32];

	sprintf(resn, "0x%x", qp->qplib_qp.id);
	qp->dentry = debugfs_create_file(resn, 0400, rdev->qp_debugfs, qp, &debugfs_qp_fops);
}

void bnxt_re_debug_rem_qpinfo(struct bnxt_re_dev *rdev, struct bnxt_re_qp *qp)
{
	debugfs_remove(qp->dentry);
}

static int map_cc_config_offset_gen0_ext0(u32 offset, struct bnxt_qplib_cc_param *ccparam, u32 *val)
{
	u64 map_offset;

	map_offset = BIT(offset);

	switch (map_offset) {
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_ENABLE_CC:
		*val = ccparam->enable;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_G:
		*val = ccparam->g;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_NUMPHASEPERSTATE:
		*val = ccparam->nph_per_state;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_INIT_CR:
		*val = ccparam->init_cr;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_INIT_TR:
		*val = ccparam->init_tr;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_TOS_ECN:
		*val = ccparam->tos_ecn;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_TOS_DSCP:
		*val =  ccparam->tos_dscp;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_ALT_VLAN_PCP:
		*val = ccparam->alt_vlan_pcp;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_ALT_TOS_DSCP:
		*val = ccparam->alt_tos_dscp;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_RTT:
	       *val = ccparam->rtt;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_CC_MODE:
		*val = ccparam->cc_mode;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_TCP_CP:
		*val =  ccparam->tcp_cp;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_INACTIVITY_CP:
		*val = ccparam->inact_th;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static ssize_t bnxt_re_cc_config_get(struct file *filp, char __user *buffer,
				     size_t usr_buf_len, loff_t *ppos)
{
	struct bnxt_re_cc_param *dbg_cc_param = filp->private_data;
	struct bnxt_re_dev *rdev = dbg_cc_param->rdev;
	struct bnxt_qplib_cc_param ccparam = {};
	u32 offset = dbg_cc_param->offset;
	char buf[16];
	u32 val;
	int rc;

	rc = bnxt_qplib_query_cc_param(&rdev->qplib_res, &ccparam);
	if (rc)
		return rc;

	rc = map_cc_config_offset_gen0_ext0(offset, &ccparam, &val);
	if (rc)
		return rc;

	rc = snprintf(buf, sizeof(buf), "%d\n", val);
	if (rc < 0)
		return rc;

	return simple_read_from_buffer(buffer, usr_buf_len, ppos, (u8 *)(buf), rc);
}

static int bnxt_re_fill_gen0_ext0(struct bnxt_qplib_cc_param *ccparam, u32 offset, u32 val)
{
	u32 modify_mask;

	modify_mask = BIT(offset);

	switch (modify_mask) {
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_ENABLE_CC:
		ccparam->enable = val;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_G:
		ccparam->g = val;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_NUMPHASEPERSTATE:
		ccparam->nph_per_state = val;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_INIT_CR:
		ccparam->init_cr = val;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_INIT_TR:
		ccparam->init_tr = val;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_TOS_ECN:
		ccparam->tos_ecn = val;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_TOS_DSCP:
		ccparam->tos_dscp = val;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_ALT_VLAN_PCP:
		ccparam->alt_vlan_pcp = val;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_ALT_TOS_DSCP:
		ccparam->alt_tos_dscp = val;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_RTT:
		ccparam->rtt = val;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_CC_MODE:
		ccparam->cc_mode = val;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_TCP_CP:
		ccparam->tcp_cp = val;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_TX_QUEUE:
		return -EOPNOTSUPP;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_INACTIVITY_CP:
		ccparam->inact_th = val;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_TIME_PER_PHASE:
		ccparam->time_pph = val;
		break;
	case CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_PKTS_PER_PHASE:
		ccparam->pkts_pph = val;
		break;
	}

	ccparam->mask = modify_mask;
	return 0;
}

static int bnxt_re_configure_cc(struct bnxt_re_dev *rdev, u32 gen_ext, u32 offset, u32 val)
{
	struct bnxt_qplib_cc_param ccparam = { };
	int rc;

	if (gen_ext != CC_CONFIG_GEN0_EXT0)
		return -EOPNOTSUPP;

	rc = bnxt_re_fill_gen0_ext0(&ccparam, offset, val);
	if (rc)
		return rc;

	bnxt_qplib_modify_cc(&rdev->qplib_res, &ccparam);
	return 0;
}

static ssize_t bnxt_re_cc_config_set(struct file *filp, const char __user *buffer,
				     size_t count, loff_t *ppos)
{
	struct bnxt_re_cc_param *dbg_cc_param = filp->private_data;
	struct bnxt_re_dev *rdev = dbg_cc_param->rdev;
	u32 offset = dbg_cc_param->offset;
	u8 cc_gen = dbg_cc_param->cc_gen;
	char buf[16];
	u32 val;
	int rc;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, buffer, count))
		return -EFAULT;

	buf[count] = '\0';
	if (kstrtou32(buf, 0, &val))
		return -EINVAL;

	rc = bnxt_re_configure_cc(rdev, cc_gen, offset, val);
	return rc ? rc : count;
}

static const struct file_operations bnxt_re_cc_config_ops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = bnxt_re_cc_config_get,
	.write = bnxt_re_cc_config_set,
};

void bnxt_re_debugfs_add_pdev(struct bnxt_re_dev *rdev)
{
	struct pci_dev *pdev = rdev->en_dev->pdev;
	struct bnxt_re_dbg_cc_config_params *cc_params;
	int i;

	rdev->dbg_root = debugfs_create_dir(dev_name(&pdev->dev), bnxt_re_debugfs_root);

	rdev->qp_debugfs = debugfs_create_dir("QPs", rdev->dbg_root);
	rdev->cc_config = debugfs_create_dir("cc_config", rdev->dbg_root);

	rdev->cc_config_params = kzalloc(sizeof(*cc_params), GFP_KERNEL);

	for (i = 0; i < BNXT_RE_CC_PARAM_GEN0; i++) {
		struct bnxt_re_cc_param *tmp_params = &rdev->cc_config_params->gen0_parms[i];

		tmp_params->rdev = rdev;
		tmp_params->offset = i;
		tmp_params->cc_gen = CC_CONFIG_GEN0_EXT0;
		tmp_params->dentry = debugfs_create_file(bnxt_re_cc_gen0_name[i], 0400,
							 rdev->cc_config, tmp_params,
							 &bnxt_re_cc_config_ops);
	}
}

void bnxt_re_debugfs_rem_pdev(struct bnxt_re_dev *rdev)
{
	debugfs_remove_recursive(rdev->qp_debugfs);
	debugfs_remove_recursive(rdev->cc_config);
	kfree(rdev->cc_config_params);
	debugfs_remove_recursive(rdev->dbg_root);
	rdev->dbg_root = NULL;
}

void bnxt_re_register_debugfs(void)
{
	bnxt_re_debugfs_root = debugfs_create_dir("bnxt_re", NULL);
}

void bnxt_re_unregister_debugfs(void)
{
	debugfs_remove(bnxt_re_debugfs_root);
}
