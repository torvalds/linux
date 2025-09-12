// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/delay.h>

#include "hinic3_cmdq.h"
#include "hinic3_hw_comm.h"
#include "hinic3_hwdev.h"
#include "hinic3_hwif.h"
#include "hinic3_mbox.h"

int hinic3_set_interrupt_cfg_direct(struct hinic3_hwdev *hwdev,
				    const struct hinic3_interrupt_info *info)
{
	struct comm_cmd_cfg_msix_ctrl_reg msix_cfg = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	msix_cfg.func_id = hinic3_global_func_id(hwdev);
	msix_cfg.msix_index = info->msix_index;
	msix_cfg.opcode = MGMT_MSG_CMD_OP_SET;

	msix_cfg.lli_credit_cnt = info->lli_credit_limit;
	msix_cfg.lli_timer_cnt = info->lli_timer_cfg;
	msix_cfg.pending_cnt = info->pending_limit;
	msix_cfg.coalesce_timer_cnt = info->coalesc_timer_cfg;
	msix_cfg.resend_timer_cnt = info->resend_timer_cfg;

	mgmt_msg_params_init_default(&msg_params, &msix_cfg, sizeof(msix_cfg));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_COMM,
				       COMM_CMD_CFG_MSIX_CTRL_REG, &msg_params);
	if (err || msix_cfg.head.status) {
		dev_err(hwdev->dev,
			"Failed to set interrupt config, err: %d, status: 0x%x\n",
			err, msix_cfg.head.status);
		return -EINVAL;
	}

	return 0;
}

int hinic3_func_reset(struct hinic3_hwdev *hwdev, u16 func_id, u64 reset_flag)
{
	struct comm_cmd_func_reset func_reset = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	func_reset.func_id = func_id;
	func_reset.reset_flag = reset_flag;

	mgmt_msg_params_init_default(&msg_params, &func_reset,
				     sizeof(func_reset));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_COMM,
				       COMM_CMD_FUNC_RESET, &msg_params);
	if (err || func_reset.head.status) {
		dev_err(hwdev->dev, "Failed to reset func resources, reset_flag 0x%llx, err: %d, status: 0x%x\n",
			reset_flag, err, func_reset.head.status);
		return -EIO;
	}

	return 0;
}

static int hinic3_comm_features_nego(struct hinic3_hwdev *hwdev, u8 opcode,
				     u64 *s_feature, u16 size)
{
	struct comm_cmd_feature_nego feature_nego = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	feature_nego.func_id = hinic3_global_func_id(hwdev);
	feature_nego.opcode = opcode;
	if (opcode == MGMT_MSG_CMD_OP_SET)
		memcpy(feature_nego.s_feature, s_feature,
		       array_size(size, sizeof(u64)));

	mgmt_msg_params_init_default(&msg_params, &feature_nego,
				     sizeof(feature_nego));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_COMM,
				       COMM_CMD_FEATURE_NEGO, &msg_params);
	if (err || feature_nego.head.status) {
		dev_err(hwdev->dev, "Failed to negotiate feature, err: %d, status: 0x%x\n",
			err, feature_nego.head.status);
		return -EINVAL;
	}

	if (opcode == MGMT_MSG_CMD_OP_GET)
		memcpy(s_feature, feature_nego.s_feature,
		       array_size(size, sizeof(u64)));

	return 0;
}

int hinic3_get_comm_features(struct hinic3_hwdev *hwdev, u64 *s_feature,
			     u16 size)
{
	return hinic3_comm_features_nego(hwdev, MGMT_MSG_CMD_OP_GET, s_feature,
					 size);
}

int hinic3_set_comm_features(struct hinic3_hwdev *hwdev, u64 *s_feature,
			     u16 size)
{
	return hinic3_comm_features_nego(hwdev, MGMT_MSG_CMD_OP_SET, s_feature,
					 size);
}

int hinic3_get_global_attr(struct hinic3_hwdev *hwdev,
			   struct comm_global_attr *attr)
{
	struct comm_cmd_get_glb_attr get_attr = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	mgmt_msg_params_init_default(&msg_params, &get_attr, sizeof(get_attr));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_COMM,
				       COMM_CMD_GET_GLOBAL_ATTR, &msg_params);
	if (err || get_attr.head.status) {
		dev_err(hwdev->dev,
			"Failed to get global attribute, err: %d, status: 0x%x\n",
			err, get_attr.head.status);
		return -EIO;
	}

	memcpy(attr, &get_attr.attr, sizeof(*attr));

	return 0;
}

int hinic3_set_func_svc_used_state(struct hinic3_hwdev *hwdev, u16 svc_type,
				   u8 state)
{
	struct comm_cmd_set_func_svc_used_state used_state = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	used_state.func_id = hinic3_global_func_id(hwdev);
	used_state.svc_type = svc_type;
	used_state.used_state = state;

	mgmt_msg_params_init_default(&msg_params, &used_state,
				     sizeof(used_state));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_COMM,
				       COMM_CMD_SET_FUNC_SVC_USED_STATE,
				       &msg_params);
	if (err || used_state.head.status) {
		dev_err(hwdev->dev,
			"Failed to set func service used state, err: %d, status: 0x%x\n",
			err, used_state.head.status);
		return -EIO;
	}

	return 0;
}

int hinic3_set_dma_attr_tbl(struct hinic3_hwdev *hwdev, u8 entry_idx, u8 st,
			    u8 at, u8 ph, u8 no_snooping, u8 tph_en)
{
	struct comm_cmd_set_dma_attr dma_attr = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	dma_attr.func_id = hinic3_global_func_id(hwdev);
	dma_attr.entry_idx = entry_idx;
	dma_attr.st = st;
	dma_attr.at = at;
	dma_attr.ph = ph;
	dma_attr.no_snooping = no_snooping;
	dma_attr.tph_en = tph_en;

	mgmt_msg_params_init_default(&msg_params, &dma_attr, sizeof(dma_attr));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_COMM,
				       COMM_CMD_SET_DMA_ATTR, &msg_params);
	if (err || dma_attr.head.status) {
		dev_err(hwdev->dev, "Failed to set dma attr, err: %d, status: 0x%x\n",
			err, dma_attr.head.status);
		return -EIO;
	}

	return 0;
}

int hinic3_set_wq_page_size(struct hinic3_hwdev *hwdev, u16 func_idx,
			    u32 page_size)
{
	struct comm_cmd_cfg_wq_page_size page_size_info = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	page_size_info.func_id = func_idx;
	page_size_info.page_size = ilog2(page_size / HINIC3_MIN_PAGE_SIZE);
	page_size_info.opcode = MGMT_MSG_CMD_OP_SET;

	mgmt_msg_params_init_default(&msg_params, &page_size_info,
				     sizeof(page_size_info));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_COMM,
				       COMM_CMD_CFG_PAGESIZE, &msg_params);
	if (err || page_size_info.head.status) {
		dev_err(hwdev->dev,
			"Failed to set wq page size, err: %d, status: 0x%x\n",
			err, page_size_info.head.status);
		return -EFAULT;
	}

	return 0;
}

int hinic3_set_cmdq_depth(struct hinic3_hwdev *hwdev, u16 cmdq_depth)
{
	struct comm_cmd_set_root_ctxt root_ctxt = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	root_ctxt.func_id = hinic3_global_func_id(hwdev);

	root_ctxt.set_cmdq_depth = 1;
	root_ctxt.cmdq_depth = ilog2(cmdq_depth);

	mgmt_msg_params_init_default(&msg_params, &root_ctxt,
				     sizeof(root_ctxt));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_COMM,
				       COMM_CMD_SET_VAT, &msg_params);
	if (err || root_ctxt.head.status) {
		dev_err(hwdev->dev,
			"Failed to set cmdq depth, err: %d, status: 0x%x\n",
			err, root_ctxt.head.status);
		return -EFAULT;
	}

	return 0;
}
