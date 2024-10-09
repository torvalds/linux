// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Copyright 2018-2024 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include "efa_com.h"
#include "efa_com_cmd.h"

int efa_com_create_qp(struct efa_com_dev *edev,
		      struct efa_com_create_qp_params *params,
		      struct efa_com_create_qp_result *res)
{
	struct efa_admin_create_qp_cmd create_qp_cmd = {};
	struct efa_admin_create_qp_resp cmd_completion;
	struct efa_com_admin_queue *aq = &edev->aq;
	int err;

	create_qp_cmd.aq_common_desc.opcode = EFA_ADMIN_CREATE_QP;

	create_qp_cmd.pd = params->pd;
	create_qp_cmd.qp_type = params->qp_type;
	create_qp_cmd.rq_base_addr = params->rq_base_addr;
	create_qp_cmd.send_cq_idx = params->send_cq_idx;
	create_qp_cmd.recv_cq_idx = params->recv_cq_idx;
	create_qp_cmd.qp_alloc_size.send_queue_ring_size =
		params->sq_ring_size_in_bytes;
	create_qp_cmd.qp_alloc_size.send_queue_depth =
			params->sq_depth;
	create_qp_cmd.qp_alloc_size.recv_queue_ring_size =
			params->rq_ring_size_in_bytes;
	create_qp_cmd.qp_alloc_size.recv_queue_depth =
			params->rq_depth;
	create_qp_cmd.uar = params->uarn;

	if (params->unsolicited_write_recv)
		EFA_SET(&create_qp_cmd.flags, EFA_ADMIN_CREATE_QP_CMD_UNSOLICITED_WRITE_RECV, 1);

	err = efa_com_cmd_exec(aq,
			       (struct efa_admin_aq_entry *)&create_qp_cmd,
			       sizeof(create_qp_cmd),
			       (struct efa_admin_acq_entry *)&cmd_completion,
			       sizeof(cmd_completion));
	if (err) {
		ibdev_err_ratelimited(edev->efa_dev,
				      "Failed to create qp [%d]\n", err);
		return err;
	}

	res->qp_handle = cmd_completion.qp_handle;
	res->qp_num = cmd_completion.qp_num;
	res->sq_db_offset = cmd_completion.sq_db_offset;
	res->rq_db_offset = cmd_completion.rq_db_offset;
	res->llq_descriptors_offset = cmd_completion.llq_descriptors_offset;
	res->send_sub_cq_idx = cmd_completion.send_sub_cq_idx;
	res->recv_sub_cq_idx = cmd_completion.recv_sub_cq_idx;

	return 0;
}

int efa_com_modify_qp(struct efa_com_dev *edev,
		      struct efa_com_modify_qp_params *params)
{
	struct efa_com_admin_queue *aq = &edev->aq;
	struct efa_admin_modify_qp_cmd cmd = {};
	struct efa_admin_modify_qp_resp resp;
	int err;

	cmd.aq_common_desc.opcode = EFA_ADMIN_MODIFY_QP;
	cmd.modify_mask = params->modify_mask;
	cmd.qp_handle = params->qp_handle;
	cmd.qp_state = params->qp_state;
	cmd.cur_qp_state = params->cur_qp_state;
	cmd.qkey = params->qkey;
	cmd.sq_psn = params->sq_psn;
	cmd.sq_drained_async_notify = params->sq_drained_async_notify;
	cmd.rnr_retry = params->rnr_retry;

	err = efa_com_cmd_exec(aq,
			       (struct efa_admin_aq_entry *)&cmd,
			       sizeof(cmd),
			       (struct efa_admin_acq_entry *)&resp,
			       sizeof(resp));
	if (err) {
		ibdev_err_ratelimited(
			edev->efa_dev,
			"Failed to modify qp-%u modify_mask[%#x] [%d]\n",
			cmd.qp_handle, cmd.modify_mask, err);
		return err;
	}

	return 0;
}

int efa_com_query_qp(struct efa_com_dev *edev,
		     struct efa_com_query_qp_params *params,
		     struct efa_com_query_qp_result *result)
{
	struct efa_com_admin_queue *aq = &edev->aq;
	struct efa_admin_query_qp_cmd cmd = {};
	struct efa_admin_query_qp_resp resp;
	int err;

	cmd.aq_common_desc.opcode = EFA_ADMIN_QUERY_QP;
	cmd.qp_handle = params->qp_handle;

	err = efa_com_cmd_exec(aq,
			       (struct efa_admin_aq_entry *)&cmd,
			       sizeof(cmd),
			       (struct efa_admin_acq_entry *)&resp,
			       sizeof(resp));
	if (err) {
		ibdev_err_ratelimited(edev->efa_dev,
				      "Failed to query qp-%u [%d]\n",
				      cmd.qp_handle, err);
		return err;
	}

	result->qp_state = resp.qp_state;
	result->qkey = resp.qkey;
	result->sq_draining = resp.sq_draining;
	result->sq_psn = resp.sq_psn;
	result->rnr_retry = resp.rnr_retry;

	return 0;
}

int efa_com_destroy_qp(struct efa_com_dev *edev,
		       struct efa_com_destroy_qp_params *params)
{
	struct efa_admin_destroy_qp_resp cmd_completion;
	struct efa_admin_destroy_qp_cmd qp_cmd = {};
	struct efa_com_admin_queue *aq = &edev->aq;
	int err;

	qp_cmd.aq_common_desc.opcode = EFA_ADMIN_DESTROY_QP;
	qp_cmd.qp_handle = params->qp_handle;

	err = efa_com_cmd_exec(aq,
			       (struct efa_admin_aq_entry *)&qp_cmd,
			       sizeof(qp_cmd),
			       (struct efa_admin_acq_entry *)&cmd_completion,
			       sizeof(cmd_completion));
	if (err) {
		ibdev_err_ratelimited(edev->efa_dev,
				      "Failed to destroy qp-%u [%d]\n",
				      qp_cmd.qp_handle, err);
		return err;
	}

	return 0;
}

int efa_com_create_cq(struct efa_com_dev *edev,
		      struct efa_com_create_cq_params *params,
		      struct efa_com_create_cq_result *result)
{
	struct efa_admin_create_cq_resp cmd_completion = {};
	struct efa_admin_create_cq_cmd create_cmd = {};
	struct efa_com_admin_queue *aq = &edev->aq;
	int err;

	create_cmd.aq_common_desc.opcode = EFA_ADMIN_CREATE_CQ;
	EFA_SET(&create_cmd.cq_caps_2,
		EFA_ADMIN_CREATE_CQ_CMD_CQ_ENTRY_SIZE_WORDS,
		params->entry_size_in_bytes / 4);
	create_cmd.cq_depth = params->cq_depth;
	create_cmd.num_sub_cqs = params->num_sub_cqs;
	create_cmd.uar = params->uarn;
	if (params->interrupt_mode_enabled) {
		EFA_SET(&create_cmd.cq_caps_1,
			EFA_ADMIN_CREATE_CQ_CMD_INTERRUPT_MODE_ENABLED, 1);
		create_cmd.eqn = params->eqn;
	}
	if (params->set_src_addr) {
		EFA_SET(&create_cmd.cq_caps_2,
			EFA_ADMIN_CREATE_CQ_CMD_SET_SRC_ADDR, 1);
	}
	efa_com_set_dma_addr(params->dma_addr,
			     &create_cmd.cq_ba.mem_addr_high,
			     &create_cmd.cq_ba.mem_addr_low);

	err = efa_com_cmd_exec(aq,
			       (struct efa_admin_aq_entry *)&create_cmd,
			       sizeof(create_cmd),
			       (struct efa_admin_acq_entry *)&cmd_completion,
			       sizeof(cmd_completion));
	if (err) {
		ibdev_err_ratelimited(edev->efa_dev,
				      "Failed to create cq[%d]\n", err);
		return err;
	}

	result->cq_idx = cmd_completion.cq_idx;
	result->actual_depth = params->cq_depth;
	result->db_off = cmd_completion.db_offset;
	result->db_valid = EFA_GET(&cmd_completion.flags,
				   EFA_ADMIN_CREATE_CQ_RESP_DB_VALID);

	return 0;
}

int efa_com_destroy_cq(struct efa_com_dev *edev,
		       struct efa_com_destroy_cq_params *params)
{
	struct efa_admin_destroy_cq_cmd destroy_cmd = {};
	struct efa_admin_destroy_cq_resp destroy_resp;
	struct efa_com_admin_queue *aq = &edev->aq;
	int err;

	destroy_cmd.cq_idx = params->cq_idx;
	destroy_cmd.aq_common_desc.opcode = EFA_ADMIN_DESTROY_CQ;

	err = efa_com_cmd_exec(aq,
			       (struct efa_admin_aq_entry *)&destroy_cmd,
			       sizeof(destroy_cmd),
			       (struct efa_admin_acq_entry *)&destroy_resp,
			       sizeof(destroy_resp));

	if (err) {
		ibdev_err_ratelimited(edev->efa_dev,
				      "Failed to destroy CQ-%u [%d]\n",
				      params->cq_idx, err);
		return err;
	}

	return 0;
}

int efa_com_register_mr(struct efa_com_dev *edev,
			struct efa_com_reg_mr_params *params,
			struct efa_com_reg_mr_result *result)
{
	struct efa_admin_reg_mr_resp cmd_completion;
	struct efa_com_admin_queue *aq = &edev->aq;
	struct efa_admin_reg_mr_cmd mr_cmd = {};
	int err;

	mr_cmd.aq_common_desc.opcode = EFA_ADMIN_REG_MR;
	mr_cmd.pd = params->pd;
	mr_cmd.mr_length = params->mr_length_in_bytes;
	EFA_SET(&mr_cmd.flags, EFA_ADMIN_REG_MR_CMD_PHYS_PAGE_SIZE_SHIFT,
		params->page_shift);
	mr_cmd.iova = params->iova;
	mr_cmd.permissions = params->permissions;

	if (params->inline_pbl) {
		memcpy(mr_cmd.pbl.inline_pbl_array,
		       params->pbl.inline_pbl_array,
		       sizeof(mr_cmd.pbl.inline_pbl_array));
	} else {
		mr_cmd.pbl.pbl.length = params->pbl.pbl.length;
		mr_cmd.pbl.pbl.address.mem_addr_low =
			params->pbl.pbl.address.mem_addr_low;
		mr_cmd.pbl.pbl.address.mem_addr_high =
			params->pbl.pbl.address.mem_addr_high;
		EFA_SET(&mr_cmd.aq_common_desc.flags,
			EFA_ADMIN_AQ_COMMON_DESC_CTRL_DATA, 1);
		if (params->indirect)
			EFA_SET(&mr_cmd.aq_common_desc.flags,
				EFA_ADMIN_AQ_COMMON_DESC_CTRL_DATA_INDIRECT, 1);
	}

	err = efa_com_cmd_exec(aq,
			       (struct efa_admin_aq_entry *)&mr_cmd,
			       sizeof(mr_cmd),
			       (struct efa_admin_acq_entry *)&cmd_completion,
			       sizeof(cmd_completion));
	if (err) {
		ibdev_err_ratelimited(edev->efa_dev,
				      "Failed to register mr [%d]\n", err);
		return err;
	}

	result->l_key = cmd_completion.l_key;
	result->r_key = cmd_completion.r_key;
	result->ic_info.recv_ic_id = cmd_completion.recv_ic_id;
	result->ic_info.rdma_read_ic_id = cmd_completion.rdma_read_ic_id;
	result->ic_info.rdma_recv_ic_id = cmd_completion.rdma_recv_ic_id;
	result->ic_info.recv_ic_id_valid = EFA_GET(&cmd_completion.validity,
						   EFA_ADMIN_REG_MR_RESP_RECV_IC_ID);
	result->ic_info.rdma_read_ic_id_valid = EFA_GET(&cmd_completion.validity,
							EFA_ADMIN_REG_MR_RESP_RDMA_READ_IC_ID);
	result->ic_info.rdma_recv_ic_id_valid = EFA_GET(&cmd_completion.validity,
							EFA_ADMIN_REG_MR_RESP_RDMA_RECV_IC_ID);

	return 0;
}

int efa_com_dereg_mr(struct efa_com_dev *edev,
		     struct efa_com_dereg_mr_params *params)
{
	struct efa_admin_dereg_mr_resp cmd_completion;
	struct efa_com_admin_queue *aq = &edev->aq;
	struct efa_admin_dereg_mr_cmd mr_cmd = {};
	int err;

	mr_cmd.aq_common_desc.opcode = EFA_ADMIN_DEREG_MR;
	mr_cmd.l_key = params->l_key;

	err = efa_com_cmd_exec(aq,
			       (struct efa_admin_aq_entry *)&mr_cmd,
			       sizeof(mr_cmd),
			       (struct efa_admin_acq_entry *)&cmd_completion,
			       sizeof(cmd_completion));
	if (err) {
		ibdev_err_ratelimited(edev->efa_dev,
				      "Failed to de-register mr(lkey-%u) [%d]\n",
				      mr_cmd.l_key, err);
		return err;
	}

	return 0;
}

int efa_com_create_ah(struct efa_com_dev *edev,
		      struct efa_com_create_ah_params *params,
		      struct efa_com_create_ah_result *result)
{
	struct efa_admin_create_ah_resp cmd_completion;
	struct efa_com_admin_queue *aq = &edev->aq;
	struct efa_admin_create_ah_cmd ah_cmd = {};
	int err;

	ah_cmd.aq_common_desc.opcode = EFA_ADMIN_CREATE_AH;

	memcpy(ah_cmd.dest_addr, params->dest_addr, sizeof(ah_cmd.dest_addr));
	ah_cmd.pd = params->pdn;

	err = efa_com_cmd_exec(aq,
			       (struct efa_admin_aq_entry *)&ah_cmd,
			       sizeof(ah_cmd),
			       (struct efa_admin_acq_entry *)&cmd_completion,
			       sizeof(cmd_completion));
	if (err) {
		ibdev_err_ratelimited(edev->efa_dev,
				      "Failed to create ah for %pI6 [%d]\n",
				      ah_cmd.dest_addr, err);
		return err;
	}

	result->ah = cmd_completion.ah;

	return 0;
}

int efa_com_destroy_ah(struct efa_com_dev *edev,
		       struct efa_com_destroy_ah_params *params)
{
	struct efa_admin_destroy_ah_resp cmd_completion;
	struct efa_admin_destroy_ah_cmd ah_cmd = {};
	struct efa_com_admin_queue *aq = &edev->aq;
	int err;

	ah_cmd.aq_common_desc.opcode = EFA_ADMIN_DESTROY_AH;
	ah_cmd.ah = params->ah;
	ah_cmd.pd = params->pdn;

	err = efa_com_cmd_exec(aq,
			       (struct efa_admin_aq_entry *)&ah_cmd,
			       sizeof(ah_cmd),
			       (struct efa_admin_acq_entry *)&cmd_completion,
			       sizeof(cmd_completion));
	if (err) {
		ibdev_err_ratelimited(edev->efa_dev,
				      "Failed to destroy ah-%d pd-%d [%d]\n",
				      ah_cmd.ah, ah_cmd.pd, err);
		return err;
	}

	return 0;
}

bool
efa_com_check_supported_feature_id(struct efa_com_dev *edev,
				   enum efa_admin_aq_feature_id feature_id)
{
	u32 feature_mask = 1 << feature_id;

	/* Device attributes is always supported */
	if (feature_id != EFA_ADMIN_DEVICE_ATTR &&
	    !(edev->supported_features & feature_mask))
		return false;

	return true;
}

static int efa_com_get_feature_ex(struct efa_com_dev *edev,
				  struct efa_admin_get_feature_resp *get_resp,
				  enum efa_admin_aq_feature_id feature_id,
				  dma_addr_t control_buf_dma_addr,
				  u32 control_buff_size)
{
	struct efa_admin_get_feature_cmd get_cmd = {};
	struct efa_com_admin_queue *aq;
	int err;

	if (!efa_com_check_supported_feature_id(edev, feature_id)) {
		ibdev_err_ratelimited(edev->efa_dev,
				      "Feature %d isn't supported\n",
				      feature_id);
		return -EOPNOTSUPP;
	}

	aq = &edev->aq;

	get_cmd.aq_common_descriptor.opcode = EFA_ADMIN_GET_FEATURE;

	if (control_buff_size)
		EFA_SET(&get_cmd.aq_common_descriptor.flags,
			EFA_ADMIN_AQ_COMMON_DESC_CTRL_DATA, 1);

	efa_com_set_dma_addr(control_buf_dma_addr,
			     &get_cmd.control_buffer.address.mem_addr_high,
			     &get_cmd.control_buffer.address.mem_addr_low);

	get_cmd.control_buffer.length = control_buff_size;
	get_cmd.feature_common.feature_id = feature_id;
	err = efa_com_cmd_exec(aq,
			       (struct efa_admin_aq_entry *)
			       &get_cmd,
			       sizeof(get_cmd),
			       (struct efa_admin_acq_entry *)
			       get_resp,
			       sizeof(*get_resp));

	if (err) {
		ibdev_err_ratelimited(
			edev->efa_dev,
			"Failed to submit get_feature command %d [%d]\n",
			feature_id, err);
		return err;
	}

	return 0;
}

static int efa_com_get_feature(struct efa_com_dev *edev,
			       struct efa_admin_get_feature_resp *get_resp,
			       enum efa_admin_aq_feature_id feature_id)
{
	return efa_com_get_feature_ex(edev, get_resp, feature_id, 0, 0);
}

int efa_com_get_device_attr(struct efa_com_dev *edev,
			    struct efa_com_get_device_attr_result *result)
{
	struct efa_admin_get_feature_resp resp;
	int err;

	err = efa_com_get_feature(edev, &resp, EFA_ADMIN_DEVICE_ATTR);
	if (err) {
		ibdev_err_ratelimited(edev->efa_dev,
				      "Failed to get device attributes %d\n",
				      err);
		return err;
	}

	result->page_size_cap = resp.u.device_attr.page_size_cap;
	result->fw_version = resp.u.device_attr.fw_version;
	result->admin_api_version = resp.u.device_attr.admin_api_version;
	result->device_version = resp.u.device_attr.device_version;
	result->supported_features = resp.u.device_attr.supported_features;
	result->phys_addr_width = resp.u.device_attr.phys_addr_width;
	result->virt_addr_width = resp.u.device_attr.virt_addr_width;
	result->db_bar = resp.u.device_attr.db_bar;
	result->max_rdma_size = resp.u.device_attr.max_rdma_size;
	result->device_caps = resp.u.device_attr.device_caps;
	result->guid = resp.u.device_attr.guid;

	if (result->admin_api_version < 1) {
		ibdev_err_ratelimited(
			edev->efa_dev,
			"Failed to get device attr api version [%u < 1]\n",
			result->admin_api_version);
		return -EINVAL;
	}

	edev->supported_features = resp.u.device_attr.supported_features;
	err = efa_com_get_feature(edev, &resp,
				  EFA_ADMIN_QUEUE_ATTR);
	if (err) {
		ibdev_err_ratelimited(edev->efa_dev,
				      "Failed to get queue attributes %d\n",
				      err);
		return err;
	}

	result->max_qp = resp.u.queue_attr.max_qp;
	result->max_sq_depth = resp.u.queue_attr.max_sq_depth;
	result->max_rq_depth = resp.u.queue_attr.max_rq_depth;
	result->max_cq = resp.u.queue_attr.max_cq;
	result->max_cq_depth = resp.u.queue_attr.max_cq_depth;
	result->inline_buf_size = resp.u.queue_attr.inline_buf_size;
	result->max_sq_sge = resp.u.queue_attr.max_wr_send_sges;
	result->max_rq_sge = resp.u.queue_attr.max_wr_recv_sges;
	result->max_mr = resp.u.queue_attr.max_mr;
	result->max_mr_pages = resp.u.queue_attr.max_mr_pages;
	result->max_pd = resp.u.queue_attr.max_pd;
	result->max_ah = resp.u.queue_attr.max_ah;
	result->max_llq_size = resp.u.queue_attr.max_llq_size;
	result->sub_cqs_per_cq = resp.u.queue_attr.sub_cqs_per_cq;
	result->max_wr_rdma_sge = resp.u.queue_attr.max_wr_rdma_sges;
	result->max_tx_batch = resp.u.queue_attr.max_tx_batch;
	result->min_sq_depth = resp.u.queue_attr.min_sq_depth;

	err = efa_com_get_feature(edev, &resp, EFA_ADMIN_NETWORK_ATTR);
	if (err) {
		ibdev_err_ratelimited(edev->efa_dev,
				      "Failed to get network attributes %d\n",
				      err);
		return err;
	}

	memcpy(result->addr, resp.u.network_attr.addr,
	       sizeof(resp.u.network_attr.addr));
	result->mtu = resp.u.network_attr.mtu;

	if (efa_com_check_supported_feature_id(edev,
					       EFA_ADMIN_EVENT_QUEUE_ATTR)) {
		err = efa_com_get_feature(edev, &resp,
					  EFA_ADMIN_EVENT_QUEUE_ATTR);
		if (err) {
			ibdev_err_ratelimited(
				edev->efa_dev,
				"Failed to get event queue attributes %d\n",
				err);
			return err;
		}

		result->max_eq = resp.u.event_queue_attr.max_eq;
		result->max_eq_depth = resp.u.event_queue_attr.max_eq_depth;
		result->event_bitmask = resp.u.event_queue_attr.event_bitmask;
	}

	return 0;
}

int efa_com_get_hw_hints(struct efa_com_dev *edev,
			 struct efa_com_get_hw_hints_result *result)
{
	struct efa_admin_get_feature_resp resp;
	int err;

	err = efa_com_get_feature(edev, &resp, EFA_ADMIN_HW_HINTS);
	if (err) {
		ibdev_err_ratelimited(edev->efa_dev,
				      "Failed to get hw hints %d\n", err);
		return err;
	}

	result->admin_completion_timeout = resp.u.hw_hints.admin_completion_timeout;
	result->driver_watchdog_timeout = resp.u.hw_hints.driver_watchdog_timeout;
	result->mmio_read_timeout = resp.u.hw_hints.mmio_read_timeout;
	result->poll_interval = resp.u.hw_hints.poll_interval;

	return 0;
}

int efa_com_set_feature_ex(struct efa_com_dev *edev,
			   struct efa_admin_set_feature_resp *set_resp,
			   struct efa_admin_set_feature_cmd *set_cmd,
			   enum efa_admin_aq_feature_id feature_id,
			   dma_addr_t control_buf_dma_addr,
			   u32 control_buff_size)
{
	struct efa_com_admin_queue *aq;
	int err;

	if (!efa_com_check_supported_feature_id(edev, feature_id)) {
		ibdev_err_ratelimited(edev->efa_dev,
				      "Feature %d isn't supported\n",
				      feature_id);
		return -EOPNOTSUPP;
	}

	aq = &edev->aq;

	set_cmd->aq_common_descriptor.opcode = EFA_ADMIN_SET_FEATURE;
	if (control_buff_size) {
		set_cmd->aq_common_descriptor.flags = 0;
		EFA_SET(&set_cmd->aq_common_descriptor.flags,
			EFA_ADMIN_AQ_COMMON_DESC_CTRL_DATA, 1);
		efa_com_set_dma_addr(control_buf_dma_addr,
				     &set_cmd->control_buffer.address.mem_addr_high,
				     &set_cmd->control_buffer.address.mem_addr_low);
	}

	set_cmd->control_buffer.length = control_buff_size;
	set_cmd->feature_common.feature_id = feature_id;
	err = efa_com_cmd_exec(aq,
			       (struct efa_admin_aq_entry *)set_cmd,
			       sizeof(*set_cmd),
			       (struct efa_admin_acq_entry *)set_resp,
			       sizeof(*set_resp));

	if (err) {
		ibdev_err_ratelimited(
			edev->efa_dev,
			"Failed to submit set_feature command %d error: %d\n",
			feature_id, err);
		return err;
	}

	return 0;
}

static int efa_com_set_feature(struct efa_com_dev *edev,
			       struct efa_admin_set_feature_resp *set_resp,
			       struct efa_admin_set_feature_cmd *set_cmd,
			       enum efa_admin_aq_feature_id feature_id)
{
	return efa_com_set_feature_ex(edev, set_resp, set_cmd, feature_id,
				      0, 0);
}

int efa_com_set_aenq_config(struct efa_com_dev *edev, u32 groups)
{
	struct efa_admin_get_feature_resp get_resp;
	struct efa_admin_set_feature_resp set_resp;
	struct efa_admin_set_feature_cmd cmd = {};
	int err;

	ibdev_dbg(edev->efa_dev, "Configuring aenq with groups[%#x]\n", groups);

	err = efa_com_get_feature(edev, &get_resp, EFA_ADMIN_AENQ_CONFIG);
	if (err) {
		ibdev_err_ratelimited(edev->efa_dev,
				      "Failed to get aenq attributes: %d\n",
				      err);
		return err;
	}

	ibdev_dbg(edev->efa_dev,
		  "Get aenq groups: supported[%#x] enabled[%#x]\n",
		  get_resp.u.aenq.supported_groups,
		  get_resp.u.aenq.enabled_groups);

	if ((get_resp.u.aenq.supported_groups & groups) != groups) {
		ibdev_err_ratelimited(
			edev->efa_dev,
			"Trying to set unsupported aenq groups[%#x] supported[%#x]\n",
			groups, get_resp.u.aenq.supported_groups);
		return -EOPNOTSUPP;
	}

	cmd.u.aenq.enabled_groups = groups;
	err = efa_com_set_feature(edev, &set_resp, &cmd,
				  EFA_ADMIN_AENQ_CONFIG);
	if (err) {
		ibdev_err_ratelimited(edev->efa_dev,
				      "Failed to set aenq attributes: %d\n",
				      err);
		return err;
	}

	return 0;
}

int efa_com_alloc_pd(struct efa_com_dev *edev,
		     struct efa_com_alloc_pd_result *result)
{
	struct efa_com_admin_queue *aq = &edev->aq;
	struct efa_admin_alloc_pd_cmd cmd = {};
	struct efa_admin_alloc_pd_resp resp;
	int err;

	cmd.aq_common_descriptor.opcode = EFA_ADMIN_ALLOC_PD;

	err = efa_com_cmd_exec(aq,
			       (struct efa_admin_aq_entry *)&cmd,
			       sizeof(cmd),
			       (struct efa_admin_acq_entry *)&resp,
			       sizeof(resp));
	if (err) {
		ibdev_err_ratelimited(edev->efa_dev,
				      "Failed to allocate pd[%d]\n", err);
		return err;
	}

	result->pdn = resp.pd;

	return 0;
}

int efa_com_dealloc_pd(struct efa_com_dev *edev,
		       struct efa_com_dealloc_pd_params *params)
{
	struct efa_com_admin_queue *aq = &edev->aq;
	struct efa_admin_dealloc_pd_cmd cmd = {};
	struct efa_admin_dealloc_pd_resp resp;
	int err;

	cmd.aq_common_descriptor.opcode = EFA_ADMIN_DEALLOC_PD;
	cmd.pd = params->pdn;

	err = efa_com_cmd_exec(aq,
			       (struct efa_admin_aq_entry *)&cmd,
			       sizeof(cmd),
			       (struct efa_admin_acq_entry *)&resp,
			       sizeof(resp));
	if (err) {
		ibdev_err_ratelimited(edev->efa_dev,
				      "Failed to deallocate pd-%u [%d]\n",
				      cmd.pd, err);
		return err;
	}

	return 0;
}

int efa_com_alloc_uar(struct efa_com_dev *edev,
		      struct efa_com_alloc_uar_result *result)
{
	struct efa_com_admin_queue *aq = &edev->aq;
	struct efa_admin_alloc_uar_cmd cmd = {};
	struct efa_admin_alloc_uar_resp resp;
	int err;

	cmd.aq_common_descriptor.opcode = EFA_ADMIN_ALLOC_UAR;

	err = efa_com_cmd_exec(aq,
			       (struct efa_admin_aq_entry *)&cmd,
			       sizeof(cmd),
			       (struct efa_admin_acq_entry *)&resp,
			       sizeof(resp));
	if (err) {
		ibdev_err_ratelimited(edev->efa_dev,
				      "Failed to allocate uar[%d]\n", err);
		return err;
	}

	result->uarn = resp.uar;

	return 0;
}

int efa_com_dealloc_uar(struct efa_com_dev *edev,
			struct efa_com_dealloc_uar_params *params)
{
	struct efa_com_admin_queue *aq = &edev->aq;
	struct efa_admin_dealloc_uar_cmd cmd = {};
	struct efa_admin_dealloc_uar_resp resp;
	int err;

	cmd.aq_common_descriptor.opcode = EFA_ADMIN_DEALLOC_UAR;
	cmd.uar = params->uarn;

	err = efa_com_cmd_exec(aq,
			       (struct efa_admin_aq_entry *)&cmd,
			       sizeof(cmd),
			       (struct efa_admin_acq_entry *)&resp,
			       sizeof(resp));
	if (err) {
		ibdev_err_ratelimited(edev->efa_dev,
				      "Failed to deallocate uar-%u [%d]\n",
				      cmd.uar, err);
		return err;
	}

	return 0;
}

int efa_com_get_stats(struct efa_com_dev *edev,
		      struct efa_com_get_stats_params *params,
		      union efa_com_get_stats_result *result)
{
	struct efa_com_admin_queue *aq = &edev->aq;
	struct efa_admin_aq_get_stats_cmd cmd = {};
	struct efa_admin_acq_get_stats_resp resp;
	int err;

	cmd.aq_common_descriptor.opcode = EFA_ADMIN_GET_STATS;
	cmd.type = params->type;
	cmd.scope = params->scope;
	cmd.scope_modifier = params->scope_modifier;

	err = efa_com_cmd_exec(aq,
			       (struct efa_admin_aq_entry *)&cmd,
			       sizeof(cmd),
			       (struct efa_admin_acq_entry *)&resp,
			       sizeof(resp));
	if (err) {
		ibdev_err_ratelimited(
			edev->efa_dev,
			"Failed to get stats type-%u scope-%u.%u [%d]\n",
			cmd.type, cmd.scope, cmd.scope_modifier, err);
		return err;
	}

	switch (cmd.type) {
	case EFA_ADMIN_GET_STATS_TYPE_BASIC:
		result->basic_stats.tx_bytes = resp.u.basic_stats.tx_bytes;
		result->basic_stats.tx_pkts = resp.u.basic_stats.tx_pkts;
		result->basic_stats.rx_bytes = resp.u.basic_stats.rx_bytes;
		result->basic_stats.rx_pkts = resp.u.basic_stats.rx_pkts;
		result->basic_stats.rx_drops = resp.u.basic_stats.rx_drops;
		break;
	case EFA_ADMIN_GET_STATS_TYPE_MESSAGES:
		result->messages_stats.send_bytes = resp.u.messages_stats.send_bytes;
		result->messages_stats.send_wrs = resp.u.messages_stats.send_wrs;
		result->messages_stats.recv_bytes = resp.u.messages_stats.recv_bytes;
		result->messages_stats.recv_wrs = resp.u.messages_stats.recv_wrs;
		break;
	case EFA_ADMIN_GET_STATS_TYPE_RDMA_READ:
		result->rdma_read_stats.read_wrs = resp.u.rdma_read_stats.read_wrs;
		result->rdma_read_stats.read_bytes = resp.u.rdma_read_stats.read_bytes;
		result->rdma_read_stats.read_wr_err = resp.u.rdma_read_stats.read_wr_err;
		result->rdma_read_stats.read_resp_bytes = resp.u.rdma_read_stats.read_resp_bytes;
		break;
	case EFA_ADMIN_GET_STATS_TYPE_RDMA_WRITE:
		result->rdma_write_stats.write_wrs = resp.u.rdma_write_stats.write_wrs;
		result->rdma_write_stats.write_bytes = resp.u.rdma_write_stats.write_bytes;
		result->rdma_write_stats.write_wr_err = resp.u.rdma_write_stats.write_wr_err;
		result->rdma_write_stats.write_recv_bytes = resp.u.rdma_write_stats.write_recv_bytes;
		break;
	}

	return 0;
}
