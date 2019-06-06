// SPDX-License-Identifier: GPL-2.0
/*
 * NVMe Fabrics command implementation.
 * Copyright (c) 2015-2016 HGST, a Western Digital Company.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/blkdev.h>
#include "nvmet.h"

static void nvmet_execute_prop_set(struct nvmet_req *req)
{
	u64 val = le64_to_cpu(req->cmd->prop_set.value);
	u16 status = 0;

	if (req->cmd->prop_set.attrib & 1) {
		req->error_loc =
			offsetof(struct nvmf_property_set_command, attrib);
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		goto out;
	}

	switch (le32_to_cpu(req->cmd->prop_set.offset)) {
	case NVME_REG_CC:
		nvmet_update_cc(req->sq->ctrl, val);
		break;
	default:
		req->error_loc =
			offsetof(struct nvmf_property_set_command, offset);
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
	}
out:
	nvmet_req_complete(req, status);
}

static void nvmet_execute_prop_get(struct nvmet_req *req)
{
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	u16 status = 0;
	u64 val = 0;

	if (req->cmd->prop_get.attrib & 1) {
		switch (le32_to_cpu(req->cmd->prop_get.offset)) {
		case NVME_REG_CAP:
			val = ctrl->cap;
			break;
		default:
			status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
			break;
		}
	} else {
		switch (le32_to_cpu(req->cmd->prop_get.offset)) {
		case NVME_REG_VS:
			val = ctrl->subsys->ver;
			break;
		case NVME_REG_CC:
			val = ctrl->cc;
			break;
		case NVME_REG_CSTS:
			val = ctrl->csts;
			break;
		default:
			status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
			break;
		}
	}

	if (status && req->cmd->prop_get.attrib & 1) {
		req->error_loc =
			offsetof(struct nvmf_property_get_command, offset);
	} else {
		req->error_loc =
			offsetof(struct nvmf_property_get_command, attrib);
	}

	req->cqe->result.u64 = cpu_to_le64(val);
	nvmet_req_complete(req, status);
}

u16 nvmet_parse_fabrics_cmd(struct nvmet_req *req)
{
	struct nvme_command *cmd = req->cmd;

	switch (cmd->fabrics.fctype) {
	case nvme_fabrics_type_property_set:
		req->data_len = 0;
		req->execute = nvmet_execute_prop_set;
		break;
	case nvme_fabrics_type_property_get:
		req->data_len = 0;
		req->execute = nvmet_execute_prop_get;
		break;
	default:
		pr_err("received unknown capsule type 0x%x\n",
			cmd->fabrics.fctype);
		req->error_loc = offsetof(struct nvmf_common_command, fctype);
		return NVME_SC_INVALID_OPCODE | NVME_SC_DNR;
	}

	return 0;
}

static u16 nvmet_install_queue(struct nvmet_ctrl *ctrl, struct nvmet_req *req)
{
	struct nvmf_connect_command *c = &req->cmd->connect;
	u16 qid = le16_to_cpu(c->qid);
	u16 sqsize = le16_to_cpu(c->sqsize);
	struct nvmet_ctrl *old;

	old = cmpxchg(&req->sq->ctrl, NULL, ctrl);
	if (old) {
		pr_warn("queue already connected!\n");
		req->error_loc = offsetof(struct nvmf_connect_command, opcode);
		return NVME_SC_CONNECT_CTRL_BUSY | NVME_SC_DNR;
	}
	if (!sqsize) {
		pr_warn("queue size zero!\n");
		req->error_loc = offsetof(struct nvmf_connect_command, sqsize);
		return NVME_SC_CONNECT_INVALID_PARAM | NVME_SC_DNR;
	}

	/* note: convert queue size from 0's-based value to 1's-based value */
	nvmet_cq_setup(ctrl, req->cq, qid, sqsize + 1);
	nvmet_sq_setup(ctrl, req->sq, qid, sqsize + 1);

	if (c->cattr & NVME_CONNECT_DISABLE_SQFLOW) {
		req->sq->sqhd_disabled = true;
		req->cqe->sq_head = cpu_to_le16(0xffff);
	}

	if (ctrl->ops->install_queue) {
		u16 ret = ctrl->ops->install_queue(req->sq);

		if (ret) {
			pr_err("failed to install queue %d cntlid %d ret %x\n",
				qid, ret, ctrl->cntlid);
			return ret;
		}
	}

	return 0;
}

static void nvmet_execute_admin_connect(struct nvmet_req *req)
{
	struct nvmf_connect_command *c = &req->cmd->connect;
	struct nvmf_connect_data *d;
	struct nvmet_ctrl *ctrl = NULL;
	u16 status = 0;

	d = kmalloc(sizeof(*d), GFP_KERNEL);
	if (!d) {
		status = NVME_SC_INTERNAL;
		goto complete;
	}

	status = nvmet_copy_from_sgl(req, 0, d, sizeof(*d));
	if (status)
		goto out;

	/* zero out initial completion result, assign values as needed */
	req->cqe->result.u32 = 0;

	if (c->recfmt != 0) {
		pr_warn("invalid connect version (%d).\n",
			le16_to_cpu(c->recfmt));
		req->error_loc = offsetof(struct nvmf_connect_command, recfmt);
		status = NVME_SC_CONNECT_FORMAT | NVME_SC_DNR;
		goto out;
	}

	if (unlikely(d->cntlid != cpu_to_le16(0xffff))) {
		pr_warn("connect attempt for invalid controller ID %#x\n",
			d->cntlid);
		status = NVME_SC_CONNECT_INVALID_PARAM | NVME_SC_DNR;
		req->cqe->result.u32 = IPO_IATTR_CONNECT_DATA(cntlid);
		goto out;
	}

	status = nvmet_alloc_ctrl(d->subsysnqn, d->hostnqn, req,
				  le32_to_cpu(c->kato), &ctrl);
	if (status) {
		if (status == (NVME_SC_INVALID_FIELD | NVME_SC_DNR))
			req->error_loc =
				offsetof(struct nvme_common_command, opcode);
		goto out;
	}

	uuid_copy(&ctrl->hostid, &d->hostid);

	status = nvmet_install_queue(ctrl, req);
	if (status) {
		nvmet_ctrl_put(ctrl);
		goto out;
	}

	pr_info("creating controller %d for subsystem %s for NQN %s.\n",
		ctrl->cntlid, ctrl->subsys->subsysnqn, ctrl->hostnqn);
	req->cqe->result.u16 = cpu_to_le16(ctrl->cntlid);

out:
	kfree(d);
complete:
	nvmet_req_complete(req, status);
}

static void nvmet_execute_io_connect(struct nvmet_req *req)
{
	struct nvmf_connect_command *c = &req->cmd->connect;
	struct nvmf_connect_data *d;
	struct nvmet_ctrl *ctrl = NULL;
	u16 qid = le16_to_cpu(c->qid);
	u16 status = 0;

	d = kmalloc(sizeof(*d), GFP_KERNEL);
	if (!d) {
		status = NVME_SC_INTERNAL;
		goto complete;
	}

	status = nvmet_copy_from_sgl(req, 0, d, sizeof(*d));
	if (status)
		goto out;

	/* zero out initial completion result, assign values as needed */
	req->cqe->result.u32 = 0;

	if (c->recfmt != 0) {
		pr_warn("invalid connect version (%d).\n",
			le16_to_cpu(c->recfmt));
		status = NVME_SC_CONNECT_FORMAT | NVME_SC_DNR;
		goto out;
	}

	status = nvmet_ctrl_find_get(d->subsysnqn, d->hostnqn,
				     le16_to_cpu(d->cntlid),
				     req, &ctrl);
	if (status)
		goto out;

	if (unlikely(qid > ctrl->subsys->max_qid)) {
		pr_warn("invalid queue id (%d)\n", qid);
		status = NVME_SC_CONNECT_INVALID_PARAM | NVME_SC_DNR;
		req->cqe->result.u32 = IPO_IATTR_CONNECT_SQE(qid);
		goto out_ctrl_put;
	}

	status = nvmet_install_queue(ctrl, req);
	if (status) {
		/* pass back cntlid that had the issue of installing queue */
		req->cqe->result.u16 = cpu_to_le16(ctrl->cntlid);
		goto out_ctrl_put;
	}

	pr_debug("adding queue %d to ctrl %d.\n", qid, ctrl->cntlid);

out:
	kfree(d);
complete:
	nvmet_req_complete(req, status);
	return;

out_ctrl_put:
	nvmet_ctrl_put(ctrl);
	goto out;
}

u16 nvmet_parse_connect_cmd(struct nvmet_req *req)
{
	struct nvme_command *cmd = req->cmd;

	if (!nvme_is_fabrics(cmd)) {
		pr_err("invalid command 0x%x on unconnected queue.\n",
			cmd->fabrics.opcode);
		req->error_loc = offsetof(struct nvme_common_command, opcode);
		return NVME_SC_INVALID_OPCODE | NVME_SC_DNR;
	}
	if (cmd->fabrics.fctype != nvme_fabrics_type_connect) {
		pr_err("invalid capsule type 0x%x on unconnected queue.\n",
			cmd->fabrics.fctype);
		req->error_loc = offsetof(struct nvmf_common_command, fctype);
		return NVME_SC_INVALID_OPCODE | NVME_SC_DNR;
	}

	req->data_len = sizeof(struct nvmf_connect_data);
	if (cmd->connect.qid == 0)
		req->execute = nvmet_execute_admin_connect;
	else
		req->execute = nvmet_execute_io_connect;
	return 0;
}
