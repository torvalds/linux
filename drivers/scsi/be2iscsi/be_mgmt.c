/**
 * Copyright (C) 2005 - 2011 Emulex
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Written by: Jayamohan Kallickal (jayamohan.kallickal@emulex.com)
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

#include "be_mgmt.h"
#include "be_iscsi.h"
#include <scsi/scsi_transport_iscsi.h>

unsigned int beiscsi_get_boot_target(struct beiscsi_hba *phba)
{
	struct be_ctrl_info *ctrl = &phba->ctrl;
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_mac_addr *req;
	unsigned int tag = 0;

	SE_DEBUG(DBG_LVL_8, "In bescsi_get_boot_target\n");
	spin_lock(&ctrl->mbox_lock);
	tag = alloc_mcc_tag(phba);
	if (!tag) {
		spin_unlock(&ctrl->mbox_lock);
		return tag;
	}

	wrb = wrb_from_mccq(phba);
	req = embedded_payload(wrb);
	wrb->tag0 |= tag;
	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0);
	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ISCSI_INI,
			   OPCODE_ISCSI_INI_BOOT_GET_BOOT_TARGET,
			   sizeof(*req));

	be_mcc_notify(phba);
	spin_unlock(&ctrl->mbox_lock);
	return tag;
}

unsigned int beiscsi_get_session_info(struct beiscsi_hba *phba,
				  u32 boot_session_handle,
				  struct be_dma_mem *nonemb_cmd)
{
	struct be_ctrl_info *ctrl = &phba->ctrl;
	struct be_mcc_wrb *wrb;
	unsigned int tag = 0;
	struct  be_cmd_req_get_session *req;
	struct be_cmd_resp_get_session *resp;
	struct be_sge *sge;

	SE_DEBUG(DBG_LVL_8, "In beiscsi_get_session_info\n");
	spin_lock(&ctrl->mbox_lock);
	tag = alloc_mcc_tag(phba);
	if (!tag) {
		spin_unlock(&ctrl->mbox_lock);
		return tag;
	}

	nonemb_cmd->size = sizeof(*resp);
	req = nonemb_cmd->va;
	memset(req, 0, sizeof(*req));
	wrb = wrb_from_mccq(phba);
	sge = nonembedded_sgl(wrb);
	wrb->tag0 |= tag;


	wrb->tag0 |= tag;
	be_wrb_hdr_prepare(wrb, sizeof(*req), false, 1);
	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ISCSI_INI,
			   OPCODE_ISCSI_INI_SESSION_GET_A_SESSION,
			   sizeof(*resp));
	req->session_handle = boot_session_handle;
	sge->pa_hi = cpu_to_le32(upper_32_bits(nonemb_cmd->dma));
	sge->pa_lo = cpu_to_le32(nonemb_cmd->dma & 0xFFFFFFFF);
	sge->len = cpu_to_le32(nonemb_cmd->size);

	be_mcc_notify(phba);
	spin_unlock(&ctrl->mbox_lock);
	return tag;
}

int mgmt_get_fw_config(struct be_ctrl_info *ctrl,
				struct beiscsi_hba *phba)
{
	struct be_mcc_wrb *wrb = wrb_from_mbox(&ctrl->mbox_mem);
	struct be_fw_cfg *req = embedded_payload(wrb);
	int status = 0;

	spin_lock(&ctrl->mbox_lock);
	memset(wrb, 0, sizeof(*wrb));

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			   OPCODE_COMMON_QUERY_FIRMWARE_CONFIG, sizeof(*req));
	status = be_mbox_notify(ctrl);
	if (!status) {
		struct be_fw_cfg *pfw_cfg;
		pfw_cfg = req;
		phba->fw_config.phys_port = pfw_cfg->phys_port;
		phba->fw_config.iscsi_icd_start =
					pfw_cfg->ulp[0].icd_base;
		phba->fw_config.iscsi_icd_count =
					pfw_cfg->ulp[0].icd_count;
		phba->fw_config.iscsi_cid_start =
					pfw_cfg->ulp[0].sq_base;
		phba->fw_config.iscsi_cid_count =
					pfw_cfg->ulp[0].sq_count;
		if (phba->fw_config.iscsi_cid_count > (BE2_MAX_SESSIONS / 2)) {
			SE_DEBUG(DBG_LVL_8,
				"FW reported MAX CXNS as %d\t"
				"Max Supported = %d.\n",
				phba->fw_config.iscsi_cid_count,
				BE2_MAX_SESSIONS);
			phba->fw_config.iscsi_cid_count = BE2_MAX_SESSIONS / 2;
		}
	} else {
		shost_printk(KERN_WARNING, phba->shost,
			     "Failed in mgmt_get_fw_config\n");
	}

	spin_unlock(&ctrl->mbox_lock);
	return status;
}

int mgmt_check_supported_fw(struct be_ctrl_info *ctrl,
				      struct beiscsi_hba *phba)
{
	struct be_dma_mem nonemb_cmd;
	struct be_mcc_wrb *wrb = wrb_from_mbox(&ctrl->mbox_mem);
	struct be_mgmt_controller_attributes *req;
	struct be_sge *sge = nonembedded_sgl(wrb);
	int status = 0;

	nonemb_cmd.va = pci_alloc_consistent(ctrl->pdev,
				sizeof(struct be_mgmt_controller_attributes),
				&nonemb_cmd.dma);
	if (nonemb_cmd.va == NULL) {
		SE_DEBUG(DBG_LVL_1,
			 "Failed to allocate memory for mgmt_check_supported_fw"
			 "\n");
		return -ENOMEM;
	}
	nonemb_cmd.size = sizeof(struct be_mgmt_controller_attributes);
	req = nonemb_cmd.va;
	memset(req, 0, sizeof(*req));
	spin_lock(&ctrl->mbox_lock);
	memset(wrb, 0, sizeof(*wrb));
	be_wrb_hdr_prepare(wrb, sizeof(*req), false, 1);
	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			   OPCODE_COMMON_GET_CNTL_ATTRIBUTES, sizeof(*req));
	sge->pa_hi = cpu_to_le32(upper_32_bits(nonemb_cmd.dma));
	sge->pa_lo = cpu_to_le32(nonemb_cmd.dma & 0xFFFFFFFF);
	sge->len = cpu_to_le32(nonemb_cmd.size);
	status = be_mbox_notify(ctrl);
	if (!status) {
		struct be_mgmt_controller_attributes_resp *resp = nonemb_cmd.va;
		SE_DEBUG(DBG_LVL_8, "Firmware version of CMD: %s\n",
			resp->params.hba_attribs.flashrom_version_string);
		SE_DEBUG(DBG_LVL_8, "Firmware version is : %s\n",
			resp->params.hba_attribs.firmware_version_string);
		SE_DEBUG(DBG_LVL_8,
			"Developer Build, not performing version check...\n");
		phba->fw_config.iscsi_features =
				resp->params.hba_attribs.iscsi_features;
		SE_DEBUG(DBG_LVL_8, " phba->fw_config.iscsi_features = %d\n",
				      phba->fw_config.iscsi_features);
	} else
		SE_DEBUG(DBG_LVL_1, " Failed in mgmt_check_supported_fw\n");
	spin_unlock(&ctrl->mbox_lock);
	if (nonemb_cmd.va)
		pci_free_consistent(ctrl->pdev, nonemb_cmd.size,
				    nonemb_cmd.va, nonemb_cmd.dma);

	return status;
}

int mgmt_epfw_cleanup(struct beiscsi_hba *phba, unsigned short chute)
{
	struct be_ctrl_info *ctrl = &phba->ctrl;
	struct be_mcc_wrb *wrb = wrb_from_mccq(phba);
	struct iscsi_cleanup_req *req = embedded_payload(wrb);
	int status = 0;

	spin_lock(&ctrl->mbox_lock);
	memset(wrb, 0, sizeof(*wrb));

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0);
	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ISCSI,
			   OPCODE_COMMON_ISCSI_CLEANUP, sizeof(*req));

	req->chute = chute;
	req->hdr_ring_id = cpu_to_le16(HWI_GET_DEF_HDRQ_ID(phba));
	req->data_ring_id = cpu_to_le16(HWI_GET_DEF_BUFQ_ID(phba));

	status =  be_mcc_notify_wait(phba);
	if (status)
		shost_printk(KERN_WARNING, phba->shost,
			     " mgmt_epfw_cleanup , FAILED\n");
	spin_unlock(&ctrl->mbox_lock);
	return status;
}

unsigned int  mgmt_invalidate_icds(struct beiscsi_hba *phba,
				struct invalidate_command_table *inv_tbl,
				unsigned int num_invalidate, unsigned int cid,
				struct be_dma_mem *nonemb_cmd)

{
	struct be_ctrl_info *ctrl = &phba->ctrl;
	struct be_mcc_wrb *wrb;
	struct be_sge *sge;
	struct invalidate_commands_params_in *req;
	unsigned int i, tag = 0;

	spin_lock(&ctrl->mbox_lock);
	tag = alloc_mcc_tag(phba);
	if (!tag) {
		spin_unlock(&ctrl->mbox_lock);
		return tag;
	}

	req = nonemb_cmd->va;
	memset(req, 0, sizeof(*req));
	wrb = wrb_from_mccq(phba);
	sge = nonembedded_sgl(wrb);
	wrb->tag0 |= tag;

	be_wrb_hdr_prepare(wrb, sizeof(*req), false, 1);
	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ISCSI,
			OPCODE_COMMON_ISCSI_ERROR_RECOVERY_INVALIDATE_COMMANDS,
			sizeof(*req));
	req->ref_handle = 0;
	req->cleanup_type = CMD_ISCSI_COMMAND_INVALIDATE;
	for (i = 0; i < num_invalidate; i++) {
		req->table[i].icd = inv_tbl->icd;
		req->table[i].cid = inv_tbl->cid;
		req->icd_count++;
		inv_tbl++;
	}
	sge->pa_hi = cpu_to_le32(upper_32_bits(nonemb_cmd->dma));
	sge->pa_lo = cpu_to_le32(nonemb_cmd->dma & 0xFFFFFFFF);
	sge->len = cpu_to_le32(nonemb_cmd->size);

	be_mcc_notify(phba);
	spin_unlock(&ctrl->mbox_lock);
	return tag;
}

unsigned int mgmt_invalidate_connection(struct beiscsi_hba *phba,
					 struct beiscsi_endpoint *beiscsi_ep,
					 unsigned short cid,
					 unsigned short issue_reset,
					 unsigned short savecfg_flag)
{
	struct be_ctrl_info *ctrl = &phba->ctrl;
	struct be_mcc_wrb *wrb;
	struct iscsi_invalidate_connection_params_in *req;
	unsigned int tag = 0;

	spin_lock(&ctrl->mbox_lock);
	tag = alloc_mcc_tag(phba);
	if (!tag) {
		spin_unlock(&ctrl->mbox_lock);
		return tag;
	}
	wrb = wrb_from_mccq(phba);
	wrb->tag0 |= tag;
	req = embedded_payload(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0);
	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ISCSI_INI,
			   OPCODE_ISCSI_INI_DRIVER_INVALIDATE_CONNECTION,
			   sizeof(*req));
	req->session_handle = beiscsi_ep->fw_handle;
	req->cid = cid;
	if (issue_reset)
		req->cleanup_type = CMD_ISCSI_CONNECTION_ISSUE_TCP_RST;
	else
		req->cleanup_type = CMD_ISCSI_CONNECTION_INVALIDATE;
	req->save_cfg = savecfg_flag;
	be_mcc_notify(phba);
	spin_unlock(&ctrl->mbox_lock);
	return tag;
}

unsigned int mgmt_upload_connection(struct beiscsi_hba *phba,
				unsigned short cid, unsigned int upload_flag)
{
	struct be_ctrl_info *ctrl = &phba->ctrl;
	struct be_mcc_wrb *wrb;
	struct tcp_upload_params_in *req;
	unsigned int tag = 0;

	spin_lock(&ctrl->mbox_lock);
	tag = alloc_mcc_tag(phba);
	if (!tag) {
		spin_unlock(&ctrl->mbox_lock);
		return tag;
	}
	wrb = wrb_from_mccq(phba);
	req = embedded_payload(wrb);
	wrb->tag0 |= tag;

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0);
	be_cmd_hdr_prepare(&req->hdr, CMD_COMMON_TCP_UPLOAD,
			   OPCODE_COMMON_TCP_UPLOAD, sizeof(*req));
	req->id = (unsigned short)cid;
	req->upload_type = (unsigned char)upload_flag;
	be_mcc_notify(phba);
	spin_unlock(&ctrl->mbox_lock);
	return tag;
}

int mgmt_open_connection(struct beiscsi_hba *phba,
			 struct sockaddr *dst_addr,
			 struct beiscsi_endpoint *beiscsi_ep,
			 struct be_dma_mem *nonemb_cmd)

{
	struct hwi_controller *phwi_ctrlr;
	struct hwi_context_memory *phwi_context;
	struct sockaddr_in *daddr_in = (struct sockaddr_in *)dst_addr;
	struct sockaddr_in6 *daddr_in6 = (struct sockaddr_in6 *)dst_addr;
	struct be_ctrl_info *ctrl = &phba->ctrl;
	struct be_mcc_wrb *wrb;
	struct tcp_connect_and_offload_in *req;
	unsigned short def_hdr_id;
	unsigned short def_data_id;
	struct phys_addr template_address = { 0, 0 };
	struct phys_addr *ptemplate_address;
	unsigned int tag = 0;
	unsigned int i;
	unsigned short cid = beiscsi_ep->ep_cid;
	struct be_sge *sge;

	phwi_ctrlr = phba->phwi_ctrlr;
	phwi_context = phwi_ctrlr->phwi_ctxt;
	def_hdr_id = (unsigned short)HWI_GET_DEF_HDRQ_ID(phba);
	def_data_id = (unsigned short)HWI_GET_DEF_BUFQ_ID(phba);

	ptemplate_address = &template_address;
	ISCSI_GET_PDU_TEMPLATE_ADDRESS(phba, ptemplate_address);
	spin_lock(&ctrl->mbox_lock);
	tag = alloc_mcc_tag(phba);
	if (!tag) {
		spin_unlock(&ctrl->mbox_lock);
		return tag;
	}
	wrb = wrb_from_mccq(phba);
	memset(wrb, 0, sizeof(*wrb));
	sge = nonembedded_sgl(wrb);

	req = nonemb_cmd->va;
	memset(req, 0, sizeof(*req));
	wrb->tag0 |= tag;

	be_wrb_hdr_prepare(wrb, sizeof(*req), false, 1);
	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ISCSI,
			   OPCODE_COMMON_ISCSI_TCP_CONNECT_AND_OFFLOAD,
			   sizeof(*req));
	if (dst_addr->sa_family == PF_INET) {
		__be32 s_addr = daddr_in->sin_addr.s_addr;
		req->ip_address.ip_type = BE2_IPV4;
		req->ip_address.ip_address[0] = s_addr & 0x000000ff;
		req->ip_address.ip_address[1] = (s_addr & 0x0000ff00) >> 8;
		req->ip_address.ip_address[2] = (s_addr & 0x00ff0000) >> 16;
		req->ip_address.ip_address[3] = (s_addr & 0xff000000) >> 24;
		req->tcp_port = ntohs(daddr_in->sin_port);
		beiscsi_ep->dst_addr = daddr_in->sin_addr.s_addr;
		beiscsi_ep->dst_tcpport = ntohs(daddr_in->sin_port);
		beiscsi_ep->ip_type = BE2_IPV4;
	} else if (dst_addr->sa_family == PF_INET6) {
		req->ip_address.ip_type = BE2_IPV6;
		memcpy(&req->ip_address.ip_address,
		       &daddr_in6->sin6_addr.in6_u.u6_addr8, 16);
		req->tcp_port = ntohs(daddr_in6->sin6_port);
		beiscsi_ep->dst_tcpport = ntohs(daddr_in6->sin6_port);
		memcpy(&beiscsi_ep->dst6_addr,
		       &daddr_in6->sin6_addr.in6_u.u6_addr8, 16);
		beiscsi_ep->ip_type = BE2_IPV6;
	} else{
		shost_printk(KERN_ERR, phba->shost, "unknown addr family %d\n",
			     dst_addr->sa_family);
		spin_unlock(&ctrl->mbox_lock);
		free_mcc_tag(&phba->ctrl, tag);
		return -EINVAL;

	}
	req->cid = cid;
	i = phba->nxt_cqid++;
	if (phba->nxt_cqid == phba->num_cpus)
		phba->nxt_cqid = 0;
	req->cq_id = phwi_context->be_cq[i].id;
	SE_DEBUG(DBG_LVL_8, "i=%d cq_id=%d\n", i, req->cq_id);
	req->defq_id = def_hdr_id;
	req->hdr_ring_id = def_hdr_id;
	req->data_ring_id = def_data_id;
	req->do_offload = 1;
	req->dataout_template_pa.lo = ptemplate_address->lo;
	req->dataout_template_pa.hi = ptemplate_address->hi;
	sge->pa_hi = cpu_to_le32(upper_32_bits(nonemb_cmd->dma));
	sge->pa_lo = cpu_to_le32(nonemb_cmd->dma & 0xFFFFFFFF);
	sge->len = cpu_to_le32(nonemb_cmd->size);
	be_mcc_notify(phba);
	spin_unlock(&ctrl->mbox_lock);
	return tag;
}

unsigned int be_cmd_get_mac_addr(struct beiscsi_hba *phba)
{
	struct be_ctrl_info *ctrl = &phba->ctrl;
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_mac_addr *req;
	unsigned int tag = 0;

	SE_DEBUG(DBG_LVL_8, "In be_cmd_get_mac_addr\n");
	spin_lock(&ctrl->mbox_lock);
	tag = alloc_mcc_tag(phba);
	if (!tag) {
		spin_unlock(&ctrl->mbox_lock);
		return tag;
	}

	wrb = wrb_from_mccq(phba);
	req = embedded_payload(wrb);
	wrb->tag0 |= tag;
	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0);
	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ISCSI,
			   OPCODE_COMMON_ISCSI_NTWK_GET_NIC_CONFIG,
			   sizeof(*req));

	be_mcc_notify(phba);
	spin_unlock(&ctrl->mbox_lock);
	return tag;
}

