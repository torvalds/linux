/*
 * NVMe admin command implementation.
 * Copyright (c) 2015-2016 HGST, a Western Digital Company.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <generated/utsrelease.h>
#include "nvmet.h"

u32 nvmet_get_log_page_len(struct nvme_command *cmd)
{
	u32 len = le16_to_cpu(cmd->get_log_page.numdu);

	len <<= 16;
	len += le16_to_cpu(cmd->get_log_page.numdl);
	/* NUMD is a 0's based value */
	len += 1;
	len *= sizeof(u32);

	return len;
}

static void nvmet_execute_get_log_page(struct nvmet_req *req)
{
	size_t data_len = nvmet_get_log_page_len(req->cmd);
	void *buf;
	u16 status = 0;

	buf = kzalloc(data_len, GFP_KERNEL);
	if (!buf) {
		status = NVME_SC_INTERNAL;
		goto out;
	}

	switch (req->cmd->get_log_page.lid) {
	case 0x01:
		/*
		 * We currently never set the More bit in the status field,
		 * so all error log entries are invalid and can be zeroed out.
		 * This is called a minum viable implementation (TM) of this
		 * mandatory log page.
		 */
		break;
	case 0x02:
		/*
		 * XXX: fill out actual smart log
		 *
		 * We might have a hard time coming up with useful values for
		 * many of the fields, and even when we have useful data
		 * available (e.g. units or commands read/written) those aren't
		 * persistent over power loss.
		 */
		break;
	case 0x03:
		/*
		 * We only support a single firmware slot which always is
		 * active, so we can zero out the whole firmware slot log and
		 * still claim to fully implement this mandatory log page.
		 */
		break;
	default:
		BUG();
	}

	status = nvmet_copy_to_sgl(req, 0, buf, data_len);

	kfree(buf);
out:
	nvmet_req_complete(req, status);
}

static void nvmet_execute_identify_ctrl(struct nvmet_req *req)
{
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	struct nvme_id_ctrl *id;
	u16 status = 0;

	id = kzalloc(sizeof(*id), GFP_KERNEL);
	if (!id) {
		status = NVME_SC_INTERNAL;
		goto out;
	}

	/* XXX: figure out how to assign real vendors IDs. */
	id->vid = 0;
	id->ssvid = 0;

	memset(id->sn, ' ', sizeof(id->sn));
	snprintf(id->sn, sizeof(id->sn), "%llx", ctrl->serial);

	memset(id->mn, ' ', sizeof(id->mn));
	strncpy((char *)id->mn, "Linux", sizeof(id->mn));

	memset(id->fr, ' ', sizeof(id->fr));
	strncpy((char *)id->fr, UTS_RELEASE, sizeof(id->fr));

	id->rab = 6;

	/*
	 * XXX: figure out how we can assign a IEEE OUI, but until then
	 * the safest is to leave it as zeroes.
	 */

	/* we support multiple ports and multiples hosts: */
	id->mic = (1 << 0) | (1 << 1);

	/* no limit on data transfer sizes for now */
	id->mdts = 0;
	id->cntlid = cpu_to_le16(ctrl->cntlid);
	id->ver = cpu_to_le32(ctrl->subsys->ver);

	/* XXX: figure out what to do about RTD3R/RTD3 */
	id->oaes = cpu_to_le32(1 << 8);
	id->ctratt = cpu_to_le32(1 << 0);

	id->oacs = 0;

	/*
	 * We don't really have a practical limit on the number of abort
	 * comands.  But we don't do anything useful for abort either, so
	 * no point in allowing more abort commands than the spec requires.
	 */
	id->acl = 3;

	id->aerl = NVMET_ASYNC_EVENTS - 1;

	/* first slot is read-only, only one slot supported */
	id->frmw = (1 << 0) | (1 << 1);
	id->lpa = (1 << 0) | (1 << 2);
	id->elpe = NVMET_ERROR_LOG_SLOTS - 1;
	id->npss = 0;

	/* We support keep-alive timeout in granularity of seconds */
	id->kas = cpu_to_le16(NVMET_KAS);

	id->sqes = (0x6 << 4) | 0x6;
	id->cqes = (0x4 << 4) | 0x4;

	/* no enforcement soft-limit for maxcmd - pick arbitrary high value */
	id->maxcmd = cpu_to_le16(NVMET_MAX_CMD);

	id->nn = cpu_to_le32(ctrl->subsys->max_nsid);
	id->oncs = cpu_to_le16(NVME_CTRL_ONCS_DSM);

	/* XXX: don't report vwc if the underlying device is write through */
	id->vwc = NVME_CTRL_VWC_PRESENT;

	/*
	 * We can't support atomic writes bigger than a LBA without support
	 * from the backend device.
	 */
	id->awun = 0;
	id->awupf = 0;

	id->sgls = cpu_to_le32(1 << 0);	/* we always support SGLs */
	if (ctrl->ops->has_keyed_sgls)
		id->sgls |= cpu_to_le32(1 << 2);
	if (ctrl->ops->sqe_inline_size)
		id->sgls |= cpu_to_le32(1 << 20);

	strcpy(id->subnqn, ctrl->subsys->subsysnqn);

	/* Max command capsule size is sqe + single page of in-capsule data */
	id->ioccsz = cpu_to_le32((sizeof(struct nvme_command) +
				  ctrl->ops->sqe_inline_size) / 16);
	/* Max response capsule size is cqe */
	id->iorcsz = cpu_to_le32(sizeof(struct nvme_completion) / 16);

	id->msdbd = ctrl->ops->msdbd;

	/*
	 * Meh, we don't really support any power state.  Fake up the same
	 * values that qemu does.
	 */
	id->psd[0].max_power = cpu_to_le16(0x9c4);
	id->psd[0].entry_lat = cpu_to_le32(0x10);
	id->psd[0].exit_lat = cpu_to_le32(0x4);

	status = nvmet_copy_to_sgl(req, 0, id, sizeof(*id));

	kfree(id);
out:
	nvmet_req_complete(req, status);
}

static void nvmet_execute_identify_ns(struct nvmet_req *req)
{
	struct nvmet_ns *ns;
	struct nvme_id_ns *id;
	u16 status = 0;

	ns = nvmet_find_namespace(req->sq->ctrl, req->cmd->identify.nsid);
	if (!ns) {
		status = NVME_SC_INVALID_NS | NVME_SC_DNR;
		goto out;
	}

	id = kzalloc(sizeof(*id), GFP_KERNEL);
	if (!id) {
		status = NVME_SC_INTERNAL;
		goto out_put_ns;
	}

	/*
	 * nuse = ncap = nsze isn't aways true, but we have no way to find
	 * that out from the underlying device.
	 */
	id->ncap = id->nuse = id->nsze =
		cpu_to_le64(ns->size >> ns->blksize_shift);

	/*
	 * We just provide a single LBA format that matches what the
	 * underlying device reports.
	 */
	id->nlbaf = 0;
	id->flbas = 0;

	/*
	 * Our namespace might always be shared.  Not just with other
	 * controllers, but also with any other user of the block device.
	 */
	id->nmic = (1 << 0);

	memcpy(&id->nguid, &ns->nguid, sizeof(uuid_le));

	id->lbaf[0].ds = ns->blksize_shift;

	status = nvmet_copy_to_sgl(req, 0, id, sizeof(*id));

	kfree(id);
out_put_ns:
	nvmet_put_namespace(ns);
out:
	nvmet_req_complete(req, status);
}

static void nvmet_execute_identify_nslist(struct nvmet_req *req)
{
	static const int buf_size = 4096;
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	struct nvmet_ns *ns;
	u32 min_nsid = le32_to_cpu(req->cmd->identify.nsid);
	__le32 *list;
	u16 status = 0;
	int i = 0;

	list = kzalloc(buf_size, GFP_KERNEL);
	if (!list) {
		status = NVME_SC_INTERNAL;
		goto out;
	}

	rcu_read_lock();
	list_for_each_entry_rcu(ns, &ctrl->subsys->namespaces, dev_link) {
		if (ns->nsid <= min_nsid)
			continue;
		list[i++] = cpu_to_le32(ns->nsid);
		if (i == buf_size / sizeof(__le32))
			break;
	}
	rcu_read_unlock();

	status = nvmet_copy_to_sgl(req, 0, list, buf_size);

	kfree(list);
out:
	nvmet_req_complete(req, status);
}

/*
 * A "mimimum viable" abort implementation: the command is mandatory in the
 * spec, but we are not required to do any useful work.  We couldn't really
 * do a useful abort, so don't bother even with waiting for the command
 * to be exectuted and return immediately telling the command to abort
 * wasn't found.
 */
static void nvmet_execute_abort(struct nvmet_req *req)
{
	nvmet_set_result(req, 1);
	nvmet_req_complete(req, 0);
}

static void nvmet_execute_set_features(struct nvmet_req *req)
{
	struct nvmet_subsys *subsys = req->sq->ctrl->subsys;
	u32 cdw10 = le32_to_cpu(req->cmd->common.cdw10[0]);
	u64 val;
	u32 val32;
	u16 status = 0;

	switch (cdw10 & 0xf) {
	case NVME_FEAT_NUM_QUEUES:
		nvmet_set_result(req,
			(subsys->max_qid - 1) | ((subsys->max_qid - 1) << 16));
		break;
	case NVME_FEAT_KATO:
		val = le64_to_cpu(req->cmd->prop_set.value);
		val32 = val & 0xffff;
		req->sq->ctrl->kato = DIV_ROUND_UP(val32, 1000);
		nvmet_set_result(req, req->sq->ctrl->kato);
		break;
	default:
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		break;
	}

	nvmet_req_complete(req, status);
}

static void nvmet_execute_get_features(struct nvmet_req *req)
{
	struct nvmet_subsys *subsys = req->sq->ctrl->subsys;
	u32 cdw10 = le32_to_cpu(req->cmd->common.cdw10[0]);
	u16 status = 0;

	switch (cdw10 & 0xf) {
	/*
	 * These features are mandatory in the spec, but we don't
	 * have a useful way to implement them.  We'll eventually
	 * need to come up with some fake values for these.
	 */
#if 0
	case NVME_FEAT_ARBITRATION:
		break;
	case NVME_FEAT_POWER_MGMT:
		break;
	case NVME_FEAT_TEMP_THRESH:
		break;
	case NVME_FEAT_ERR_RECOVERY:
		break;
	case NVME_FEAT_IRQ_COALESCE:
		break;
	case NVME_FEAT_IRQ_CONFIG:
		break;
	case NVME_FEAT_WRITE_ATOMIC:
		break;
	case NVME_FEAT_ASYNC_EVENT:
		break;
#endif
	case NVME_FEAT_VOLATILE_WC:
		nvmet_set_result(req, 1);
		break;
	case NVME_FEAT_NUM_QUEUES:
		nvmet_set_result(req,
			(subsys->max_qid-1) | ((subsys->max_qid-1) << 16));
		break;
	case NVME_FEAT_KATO:
		nvmet_set_result(req, req->sq->ctrl->kato * 1000);
		break;
	default:
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		break;
	}

	nvmet_req_complete(req, status);
}

static void nvmet_execute_async_event(struct nvmet_req *req)
{
	struct nvmet_ctrl *ctrl = req->sq->ctrl;

	mutex_lock(&ctrl->lock);
	if (ctrl->nr_async_event_cmds >= NVMET_ASYNC_EVENTS) {
		mutex_unlock(&ctrl->lock);
		nvmet_req_complete(req, NVME_SC_ASYNC_LIMIT | NVME_SC_DNR);
		return;
	}
	ctrl->async_event_cmds[ctrl->nr_async_event_cmds++] = req;
	mutex_unlock(&ctrl->lock);

	schedule_work(&ctrl->async_event_work);
}

static void nvmet_execute_keep_alive(struct nvmet_req *req)
{
	struct nvmet_ctrl *ctrl = req->sq->ctrl;

	pr_debug("ctrl %d update keep-alive timer for %d secs\n",
		ctrl->cntlid, ctrl->kato);

	mod_delayed_work(system_wq, &ctrl->ka_work, ctrl->kato * HZ);
	nvmet_req_complete(req, 0);
}

int nvmet_parse_admin_cmd(struct nvmet_req *req)
{
	struct nvme_command *cmd = req->cmd;

	req->ns = NULL;

	if (unlikely(!(req->sq->ctrl->cc & NVME_CC_ENABLE))) {
		pr_err("nvmet: got admin cmd %d while CC.EN == 0\n",
				cmd->common.opcode);
		return NVME_SC_CMD_SEQ_ERROR | NVME_SC_DNR;
	}
	if (unlikely(!(req->sq->ctrl->csts & NVME_CSTS_RDY))) {
		pr_err("nvmet: got admin cmd %d while CSTS.RDY == 0\n",
				cmd->common.opcode);
		return NVME_SC_CMD_SEQ_ERROR | NVME_SC_DNR;
	}

	switch (cmd->common.opcode) {
	case nvme_admin_get_log_page:
		req->data_len = nvmet_get_log_page_len(cmd);

		switch (cmd->get_log_page.lid) {
		case 0x01:
		case 0x02:
		case 0x03:
			req->execute = nvmet_execute_get_log_page;
			return 0;
		}
		break;
	case nvme_admin_identify:
		req->data_len = 4096;
		switch (le32_to_cpu(cmd->identify.cns)) {
		case 0x00:
			req->execute = nvmet_execute_identify_ns;
			return 0;
		case 0x01:
			req->execute = nvmet_execute_identify_ctrl;
			return 0;
		case 0x02:
			req->execute = nvmet_execute_identify_nslist;
			return 0;
		}
		break;
	case nvme_admin_abort_cmd:
		req->execute = nvmet_execute_abort;
		req->data_len = 0;
		return 0;
	case nvme_admin_set_features:
		req->execute = nvmet_execute_set_features;
		req->data_len = 0;
		return 0;
	case nvme_admin_get_features:
		req->execute = nvmet_execute_get_features;
		req->data_len = 0;
		return 0;
	case nvme_admin_async_event:
		req->execute = nvmet_execute_async_event;
		req->data_len = 0;
		return 0;
	case nvme_admin_keep_alive:
		req->execute = nvmet_execute_keep_alive;
		req->data_len = 0;
		return 0;
	}

	pr_err("nvmet: unhandled cmd %d\n", cmd->common.opcode);
	return NVME_SC_INVALID_OPCODE | NVME_SC_DNR;
}
