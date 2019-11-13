// SPDX-License-Identifier: GPL-2.0
/*
 * Discovery service for the NVMe over Fabrics target.
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/slab.h>
#include <generated/utsrelease.h>
#include "nvmet.h"

struct nvmet_subsys *nvmet_disc_subsys;

static u64 nvmet_genctr;

static void __nvmet_disc_changed(struct nvmet_port *port,
				 struct nvmet_ctrl *ctrl)
{
	if (ctrl->port != port)
		return;

	if (nvmet_aen_bit_disabled(ctrl, NVME_AEN_BIT_DISC_CHANGE))
		return;

	nvmet_add_async_event(ctrl, NVME_AER_TYPE_NOTICE,
			      NVME_AER_NOTICE_DISC_CHANGED, NVME_LOG_DISC);
}

void nvmet_port_disc_changed(struct nvmet_port *port,
			     struct nvmet_subsys *subsys)
{
	struct nvmet_ctrl *ctrl;

	lockdep_assert_held(&nvmet_config_sem);
	nvmet_genctr++;

	mutex_lock(&nvmet_disc_subsys->lock);
	list_for_each_entry(ctrl, &nvmet_disc_subsys->ctrls, subsys_entry) {
		if (subsys && !nvmet_host_allowed(subsys, ctrl->hostnqn))
			continue;

		__nvmet_disc_changed(port, ctrl);
	}
	mutex_unlock(&nvmet_disc_subsys->lock);

	/* If transport can signal change, notify transport */
	if (port->tr_ops && port->tr_ops->discovery_chg)
		port->tr_ops->discovery_chg(port);
}

static void __nvmet_subsys_disc_changed(struct nvmet_port *port,
					struct nvmet_subsys *subsys,
					struct nvmet_host *host)
{
	struct nvmet_ctrl *ctrl;

	mutex_lock(&nvmet_disc_subsys->lock);
	list_for_each_entry(ctrl, &nvmet_disc_subsys->ctrls, subsys_entry) {
		if (host && strcmp(nvmet_host_name(host), ctrl->hostnqn))
			continue;

		__nvmet_disc_changed(port, ctrl);
	}
	mutex_unlock(&nvmet_disc_subsys->lock);
}

void nvmet_subsys_disc_changed(struct nvmet_subsys *subsys,
			       struct nvmet_host *host)
{
	struct nvmet_port *port;
	struct nvmet_subsys_link *s;

	nvmet_genctr++;

	list_for_each_entry(port, nvmet_ports, global_entry)
		list_for_each_entry(s, &port->subsystems, entry) {
			if (s->subsys != subsys)
				continue;
			__nvmet_subsys_disc_changed(port, subsys, host);
		}
}

void nvmet_referral_enable(struct nvmet_port *parent, struct nvmet_port *port)
{
	down_write(&nvmet_config_sem);
	if (list_empty(&port->entry)) {
		list_add_tail(&port->entry, &parent->referrals);
		port->enabled = true;
		nvmet_port_disc_changed(parent, NULL);
	}
	up_write(&nvmet_config_sem);
}

void nvmet_referral_disable(struct nvmet_port *parent, struct nvmet_port *port)
{
	down_write(&nvmet_config_sem);
	if (!list_empty(&port->entry)) {
		port->enabled = false;
		list_del_init(&port->entry);
		nvmet_port_disc_changed(parent, NULL);
	}
	up_write(&nvmet_config_sem);
}

static void nvmet_format_discovery_entry(struct nvmf_disc_rsp_page_hdr *hdr,
		struct nvmet_port *port, char *subsys_nqn, char *traddr,
		u8 type, u32 numrec)
{
	struct nvmf_disc_rsp_page_entry *e = &hdr->entries[numrec];

	e->trtype = port->disc_addr.trtype;
	e->adrfam = port->disc_addr.adrfam;
	e->treq = port->disc_addr.treq;
	e->portid = port->disc_addr.portid;
	/* we support only dynamic controllers */
	e->cntlid = cpu_to_le16(NVME_CNTLID_DYNAMIC);
	e->asqsz = cpu_to_le16(NVME_AQ_DEPTH);
	e->subtype = type;
	memcpy(e->trsvcid, port->disc_addr.trsvcid, NVMF_TRSVCID_SIZE);
	memcpy(e->traddr, traddr, NVMF_TRADDR_SIZE);
	memcpy(e->tsas.common, port->disc_addr.tsas.common, NVMF_TSAS_SIZE);
	strncpy(e->subnqn, subsys_nqn, NVMF_NQN_SIZE);
}

/*
 * nvmet_set_disc_traddr - set a correct discovery log entry traddr
 *
 * IP based transports (e.g RDMA) can listen on "any" ipv4/ipv6 addresses
 * (INADDR_ANY or IN6ADDR_ANY_INIT). The discovery log page traddr reply
 * must not contain that "any" IP address. If the transport implements
 * .disc_traddr, use it. this callback will set the discovery traddr
 * from the req->port address in case the port in question listens
 * "any" IP address.
 */
static void nvmet_set_disc_traddr(struct nvmet_req *req, struct nvmet_port *port,
		char *traddr)
{
	if (req->ops->disc_traddr)
		req->ops->disc_traddr(req, port, traddr);
	else
		memcpy(traddr, port->disc_addr.traddr, NVMF_TRADDR_SIZE);
}

static size_t discovery_log_entries(struct nvmet_req *req)
{
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	struct nvmet_subsys_link *p;
	struct nvmet_port *r;
	size_t entries = 0;

	list_for_each_entry(p, &req->port->subsystems, entry) {
		if (!nvmet_host_allowed(p->subsys, ctrl->hostnqn))
			continue;
		entries++;
	}
	list_for_each_entry(r, &req->port->referrals, entry)
		entries++;
	return entries;
}

static void nvmet_execute_disc_get_log_page(struct nvmet_req *req)
{
	const int entry_size = sizeof(struct nvmf_disc_rsp_page_entry);
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	struct nvmf_disc_rsp_page_hdr *hdr;
	u64 offset = nvmet_get_log_page_offset(req->cmd);
	size_t data_len = nvmet_get_log_page_len(req->cmd);
	size_t alloc_len;
	struct nvmet_subsys_link *p;
	struct nvmet_port *r;
	u32 numrec = 0;
	u16 status = 0;
	void *buffer;

	if (!nvmet_check_data_len(req, data_len))
		return;

	if (req->cmd->get_log_page.lid != NVME_LOG_DISC) {
		req->error_loc =
			offsetof(struct nvme_get_log_page_command, lid);
		status = NVME_SC_INVALID_OPCODE | NVME_SC_DNR;
		goto out;
	}

	/* Spec requires dword aligned offsets */
	if (offset & 0x3) {
		status = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		goto out;
	}

	/*
	 * Make sure we're passing at least a buffer of response header size.
	 * If host provided data len is less than the header size, only the
	 * number of bytes requested by host will be sent to host.
	 */
	down_read(&nvmet_config_sem);
	alloc_len = sizeof(*hdr) + entry_size * discovery_log_entries(req);
	buffer = kzalloc(alloc_len, GFP_KERNEL);
	if (!buffer) {
		up_read(&nvmet_config_sem);
		status = NVME_SC_INTERNAL;
		goto out;
	}

	hdr = buffer;
	list_for_each_entry(p, &req->port->subsystems, entry) {
		char traddr[NVMF_TRADDR_SIZE];

		if (!nvmet_host_allowed(p->subsys, ctrl->hostnqn))
			continue;

		nvmet_set_disc_traddr(req, req->port, traddr);
		nvmet_format_discovery_entry(hdr, req->port,
				p->subsys->subsysnqn, traddr,
				NVME_NQN_NVME, numrec);
		numrec++;
	}

	list_for_each_entry(r, &req->port->referrals, entry) {
		nvmet_format_discovery_entry(hdr, r,
				NVME_DISC_SUBSYS_NAME,
				r->disc_addr.traddr,
				NVME_NQN_DISC, numrec);
		numrec++;
	}

	hdr->genctr = cpu_to_le64(nvmet_genctr);
	hdr->numrec = cpu_to_le64(numrec);
	hdr->recfmt = cpu_to_le16(0);

	nvmet_clear_aen_bit(req, NVME_AEN_BIT_DISC_CHANGE);

	up_read(&nvmet_config_sem);

	status = nvmet_copy_to_sgl(req, 0, buffer + offset, data_len);
	kfree(buffer);
out:
	nvmet_req_complete(req, status);
}

static void nvmet_execute_disc_identify(struct nvmet_req *req)
{
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	struct nvme_id_ctrl *id;
	const char model[] = "Linux";
	u16 status = 0;

	if (!nvmet_check_data_len(req, NVME_IDENTIFY_DATA_SIZE))
		return;

	if (req->cmd->identify.cns != NVME_ID_CNS_CTRL) {
		req->error_loc = offsetof(struct nvme_identify, cns);
		status = NVME_SC_INVALID_OPCODE | NVME_SC_DNR;
		goto out;
	}

	id = kzalloc(sizeof(*id), GFP_KERNEL);
	if (!id) {
		status = NVME_SC_INTERNAL;
		goto out;
	}

	memset(id->sn, ' ', sizeof(id->sn));
	bin2hex(id->sn, &ctrl->subsys->serial,
		min(sizeof(ctrl->subsys->serial), sizeof(id->sn) / 2));
	memset(id->fr, ' ', sizeof(id->fr));
	memcpy_and_pad(id->mn, sizeof(id->mn), model, sizeof(model) - 1, ' ');
	memcpy_and_pad(id->fr, sizeof(id->fr),
		       UTS_RELEASE, strlen(UTS_RELEASE), ' ');

	/* no limit on data transfer sizes for now */
	id->mdts = 0;
	id->cntlid = cpu_to_le16(ctrl->cntlid);
	id->ver = cpu_to_le32(ctrl->subsys->ver);
	id->lpa = (1 << 2);

	/* no enforcement soft-limit for maxcmd - pick arbitrary high value */
	id->maxcmd = cpu_to_le16(NVMET_MAX_CMD);

	id->sgls = cpu_to_le32(1 << 0);	/* we always support SGLs */
	if (ctrl->ops->has_keyed_sgls)
		id->sgls |= cpu_to_le32(1 << 2);
	if (req->port->inline_data_size)
		id->sgls |= cpu_to_le32(1 << 20);

	id->oaes = cpu_to_le32(NVMET_DISC_AEN_CFG_OPTIONAL);

	strlcpy(id->subnqn, ctrl->subsys->subsysnqn, sizeof(id->subnqn));

	status = nvmet_copy_to_sgl(req, 0, id, sizeof(*id));

	kfree(id);
out:
	nvmet_req_complete(req, status);
}

static void nvmet_execute_disc_set_features(struct nvmet_req *req)
{
	u32 cdw10 = le32_to_cpu(req->cmd->common.cdw10);
	u16 stat;

	if (!nvmet_check_data_len(req, 0))
		return;

	switch (cdw10 & 0xff) {
	case NVME_FEAT_KATO:
		stat = nvmet_set_feat_kato(req);
		break;
	case NVME_FEAT_ASYNC_EVENT:
		stat = nvmet_set_feat_async_event(req,
						  NVMET_DISC_AEN_CFG_OPTIONAL);
		break;
	default:
		req->error_loc =
			offsetof(struct nvme_common_command, cdw10);
		stat = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		break;
	}

	nvmet_req_complete(req, stat);
}

static void nvmet_execute_disc_get_features(struct nvmet_req *req)
{
	u32 cdw10 = le32_to_cpu(req->cmd->common.cdw10);
	u16 stat = 0;

	if (!nvmet_check_data_len(req, 0))
		return;

	switch (cdw10 & 0xff) {
	case NVME_FEAT_KATO:
		nvmet_get_feat_kato(req);
		break;
	case NVME_FEAT_ASYNC_EVENT:
		nvmet_get_feat_async_event(req);
		break;
	default:
		req->error_loc =
			offsetof(struct nvme_common_command, cdw10);
		stat = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		break;
	}

	nvmet_req_complete(req, stat);
}

u16 nvmet_parse_discovery_cmd(struct nvmet_req *req)
{
	struct nvme_command *cmd = req->cmd;

	if (unlikely(!(req->sq->ctrl->csts & NVME_CSTS_RDY))) {
		pr_err("got cmd %d while not ready\n",
		       cmd->common.opcode);
		req->error_loc =
			offsetof(struct nvme_common_command, opcode);
		return NVME_SC_INVALID_OPCODE | NVME_SC_DNR;
	}

	switch (cmd->common.opcode) {
	case nvme_admin_set_features:
		req->execute = nvmet_execute_disc_set_features;
		return 0;
	case nvme_admin_get_features:
		req->execute = nvmet_execute_disc_get_features;
		return 0;
	case nvme_admin_async_event:
		req->execute = nvmet_execute_async_event;
		return 0;
	case nvme_admin_keep_alive:
		req->execute = nvmet_execute_keep_alive;
		return 0;
	case nvme_admin_get_log_page:
		req->execute = nvmet_execute_disc_get_log_page;
		return 0;
	case nvme_admin_identify:
		req->execute = nvmet_execute_disc_identify;
		return 0;
	default:
		pr_err("unhandled cmd %d\n", cmd->common.opcode);
		req->error_loc = offsetof(struct nvme_common_command, opcode);
		return NVME_SC_INVALID_OPCODE | NVME_SC_DNR;
	}

}

int __init nvmet_init_discovery(void)
{
	nvmet_disc_subsys =
		nvmet_subsys_alloc(NVME_DISC_SUBSYS_NAME, NVME_NQN_DISC);
	return PTR_ERR_OR_ZERO(nvmet_disc_subsys);
}

void nvmet_exit_discovery(void)
{
	nvmet_subsys_put(nvmet_disc_subsys);
}
