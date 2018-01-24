/*
 * Discovery service for the NVMe over Fabrics target.
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/slab.h>
#include <generated/utsrelease.h>
#include "nvmet.h"

struct nvmet_subsys *nvmet_disc_subsys;

u64 nvmet_genctr;

void nvmet_referral_enable(struct nvmet_port *parent, struct nvmet_port *port)
{
	down_write(&nvmet_config_sem);
	if (list_empty(&port->entry)) {
		list_add_tail(&port->entry, &parent->referrals);
		port->enabled = true;
		nvmet_genctr++;
	}
	up_write(&nvmet_config_sem);
}

void nvmet_referral_disable(struct nvmet_port *port)
{
	down_write(&nvmet_config_sem);
	if (!list_empty(&port->entry)) {
		port->enabled = false;
		list_del_init(&port->entry);
		nvmet_genctr++;
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
	memcpy(e->subnqn, subsys_nqn, NVMF_NQN_SIZE);
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

static void nvmet_execute_get_disc_log_page(struct nvmet_req *req)
{
	const int entry_size = sizeof(struct nvmf_disc_rsp_page_entry);
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	struct nvmf_disc_rsp_page_hdr *hdr;
	size_t data_len = nvmet_get_log_page_len(req->cmd);
	size_t alloc_len = max(data_len, sizeof(*hdr));
	int residual_len = data_len - sizeof(*hdr);
	struct nvmet_subsys_link *p;
	struct nvmet_port *r;
	u32 numrec = 0;
	u16 status = 0;

	/*
	 * Make sure we're passing at least a buffer of response header size.
	 * If host provided data len is less than the header size, only the
	 * number of bytes requested by host will be sent to host.
	 */
	hdr = kzalloc(alloc_len, GFP_KERNEL);
	if (!hdr) {
		status = NVME_SC_INTERNAL;
		goto out;
	}

	down_read(&nvmet_config_sem);
	list_for_each_entry(p, &req->port->subsystems, entry) {
		if (!nvmet_host_allowed(req, p->subsys, ctrl->hostnqn))
			continue;
		if (residual_len >= entry_size) {
			char traddr[NVMF_TRADDR_SIZE];

			nvmet_set_disc_traddr(req, req->port, traddr);
			nvmet_format_discovery_entry(hdr, req->port,
					p->subsys->subsysnqn, traddr,
					NVME_NQN_NVME, numrec);
			residual_len -= entry_size;
		}
		numrec++;
	}

	list_for_each_entry(r, &req->port->referrals, entry) {
		if (residual_len >= entry_size) {
			nvmet_format_discovery_entry(hdr, r,
					NVME_DISC_SUBSYS_NAME,
					r->disc_addr.traddr,
					NVME_NQN_DISC, numrec);
			residual_len -= entry_size;
		}
		numrec++;
	}

	hdr->genctr = cpu_to_le64(nvmet_genctr);
	hdr->numrec = cpu_to_le64(numrec);
	hdr->recfmt = cpu_to_le16(0);

	up_read(&nvmet_config_sem);

	status = nvmet_copy_to_sgl(req, 0, hdr, data_len);
	kfree(hdr);
out:
	nvmet_req_complete(req, status);
}

static void nvmet_execute_identify_disc_ctrl(struct nvmet_req *req)
{
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	struct nvme_id_ctrl *id;
	u16 status = 0;

	id = kzalloc(sizeof(*id), GFP_KERNEL);
	if (!id) {
		status = NVME_SC_INTERNAL;
		goto out;
	}

	memset(id->fr, ' ', sizeof(id->fr));
	strncpy((char *)id->fr, UTS_RELEASE, sizeof(id->fr));

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
	if (ctrl->ops->sqe_inline_size)
		id->sgls |= cpu_to_le32(1 << 20);

	strcpy(id->subnqn, ctrl->subsys->subsysnqn);

	status = nvmet_copy_to_sgl(req, 0, id, sizeof(*id));

	kfree(id);
out:
	nvmet_req_complete(req, status);
}

u16 nvmet_parse_discovery_cmd(struct nvmet_req *req)
{
	struct nvme_command *cmd = req->cmd;

	req->ns = NULL;

	if (unlikely(!(req->sq->ctrl->csts & NVME_CSTS_RDY))) {
		pr_err("got cmd %d while not ready\n",
		       cmd->common.opcode);
		return NVME_SC_INVALID_OPCODE | NVME_SC_DNR;
	}

	switch (cmd->common.opcode) {
	case nvme_admin_get_log_page:
		req->data_len = nvmet_get_log_page_len(cmd);

		switch (cmd->get_log_page.lid) {
		case NVME_LOG_DISC:
			req->execute = nvmet_execute_get_disc_log_page;
			return 0;
		default:
			pr_err("unsupported get_log_page lid %d\n",
			       cmd->get_log_page.lid);
		return NVME_SC_INVALID_OPCODE | NVME_SC_DNR;
		}
	case nvme_admin_identify:
		req->data_len = NVME_IDENTIFY_DATA_SIZE;
		switch (cmd->identify.cns) {
		case NVME_ID_CNS_CTRL:
			req->execute =
				nvmet_execute_identify_disc_ctrl;
			return 0;
		default:
			pr_err("unsupported identify cns %d\n",
			       cmd->identify.cns);
			return NVME_SC_INVALID_OPCODE | NVME_SC_DNR;
		}
	default:
		pr_err("unsupported cmd %d\n", cmd->common.opcode);
		return NVME_SC_INVALID_OPCODE | NVME_SC_DNR;
	}

	pr_err("unhandled cmd %d\n", cmd->common.opcode);
	return NVME_SC_INVALID_OPCODE | NVME_SC_DNR;
}

int __init nvmet_init_discovery(void)
{
	nvmet_disc_subsys =
		nvmet_subsys_alloc(NVME_DISC_SUBSYS_NAME, NVME_NQN_DISC);
	if (!nvmet_disc_subsys)
		return -ENOMEM;
	return 0;
}

void nvmet_exit_discovery(void)
{
	nvmet_subsys_put(nvmet_disc_subsys);
}
