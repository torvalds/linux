/**
 * Copyright (C) 2005 - 2013 Emulex
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

#include <linux/bsg-lib.h>
#include <scsi/scsi_transport_iscsi.h>
#include <scsi/scsi_bsg_iscsi.h>
#include "be_mgmt.h"
#include "be_iscsi.h"
#include "be_main.h"

/* UE Status Low CSR */
static const char * const desc_ue_status_low[] = {
	"CEV",
	"CTX",
	"DBUF",
	"ERX",
	"Host",
	"MPU",
	"NDMA",
	"PTC ",
	"RDMA ",
	"RXF ",
	"RXIPS ",
	"RXULP0 ",
	"RXULP1 ",
	"RXULP2 ",
	"TIM ",
	"TPOST ",
	"TPRE ",
	"TXIPS ",
	"TXULP0 ",
	"TXULP1 ",
	"UC ",
	"WDMA ",
	"TXULP2 ",
	"HOST1 ",
	"P0_OB_LINK ",
	"P1_OB_LINK ",
	"HOST_GPIO ",
	"MBOX ",
	"AXGMAC0",
	"AXGMAC1",
	"JTAG",
	"MPU_INTPEND"
};

/* UE Status High CSR */
static const char * const desc_ue_status_hi[] = {
	"LPCMEMHOST",
	"MGMT_MAC",
	"PCS0ONLINE",
	"MPU_IRAM",
	"PCS1ONLINE",
	"PCTL0",
	"PCTL1",
	"PMEM",
	"RR",
	"TXPB",
	"RXPP",
	"XAUI",
	"TXP",
	"ARM",
	"IPC",
	"HOST2",
	"HOST3",
	"HOST4",
	"HOST5",
	"HOST6",
	"HOST7",
	"HOST8",
	"HOST9",
	"NETC",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown"
};

/*
 * beiscsi_ue_detec()- Detect Unrecoverable Error on adapter
 * @phba: Driver priv structure
 *
 * Read registers linked to UE and check for the UE status
 **/
void beiscsi_ue_detect(struct beiscsi_hba *phba)
{
	uint32_t ue_hi = 0, ue_lo = 0;
	uint32_t ue_mask_hi = 0, ue_mask_lo = 0;
	uint8_t i = 0;

	if (phba->ue_detected)
		return;

	pci_read_config_dword(phba->pcidev,
			      PCICFG_UE_STATUS_LOW, &ue_lo);
	pci_read_config_dword(phba->pcidev,
			      PCICFG_UE_STATUS_MASK_LOW,
			      &ue_mask_lo);
	pci_read_config_dword(phba->pcidev,
			      PCICFG_UE_STATUS_HIGH,
			      &ue_hi);
	pci_read_config_dword(phba->pcidev,
			      PCICFG_UE_STATUS_MASK_HI,
			      &ue_mask_hi);

	ue_lo = (ue_lo & ~ue_mask_lo);
	ue_hi = (ue_hi & ~ue_mask_hi);


	if (ue_lo || ue_hi) {
		phba->ue_detected = true;
		beiscsi_log(phba, KERN_ERR,
			    BEISCSI_LOG_CONFIG | BEISCSI_LOG_MBOX,
			    "BG_%d : Error detected on the adapter\n");
	}

	if (ue_lo) {
		for (i = 0; ue_lo; ue_lo >>= 1, i++) {
			if (ue_lo & 1)
				beiscsi_log(phba, KERN_ERR,
					    BEISCSI_LOG_CONFIG,
					    "BG_%d : UE_LOW %s bit set\n",
					    desc_ue_status_low[i]);
		}
	}

	if (ue_hi) {
		for (i = 0; ue_hi; ue_hi >>= 1, i++) {
			if (ue_hi & 1)
				beiscsi_log(phba, KERN_ERR,
					    BEISCSI_LOG_CONFIG,
					    "BG_%d : UE_HIGH %s bit set\n",
					    desc_ue_status_hi[i]);
		}
	}
}

/**
 * mgmt_reopen_session()- Reopen a session based on reopen_type
 * @phba: Device priv structure instance
 * @reopen_type: Type of reopen_session FW should do.
 * @sess_handle: Session Handle of the session to be re-opened
 *
 * return
 *	the TAG used for MBOX Command
 *
 **/
unsigned int mgmt_reopen_session(struct beiscsi_hba *phba,
				  unsigned int reopen_type,
				  unsigned int sess_handle)
{
	struct be_ctrl_info *ctrl = &phba->ctrl;
	struct be_mcc_wrb *wrb;
	struct be_cmd_reopen_session_req *req;
	unsigned int tag = 0;

	beiscsi_log(phba, KERN_INFO,
		    BEISCSI_LOG_CONFIG | BEISCSI_LOG_MBOX,
		    "BG_%d : In bescsi_get_boot_target\n");

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
			   OPCODE_ISCSI_INI_DRIVER_REOPEN_ALL_SESSIONS,
			   sizeof(struct be_cmd_reopen_session_resp));

	/* set the reopen_type,sess_handle */
	req->reopen_type = reopen_type;
	req->session_handle = sess_handle;

	be_mcc_notify(phba);
	spin_unlock(&ctrl->mbox_lock);
	return tag;
}

unsigned int mgmt_get_boot_target(struct beiscsi_hba *phba)
{
	struct be_ctrl_info *ctrl = &phba->ctrl;
	struct be_mcc_wrb *wrb;
	struct be_cmd_get_boot_target_req *req;
	unsigned int tag = 0;

	beiscsi_log(phba, KERN_INFO,
		    BEISCSI_LOG_CONFIG | BEISCSI_LOG_MBOX,
		    "BG_%d : In bescsi_get_boot_target\n");

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
			   sizeof(struct be_cmd_get_boot_target_resp));

	be_mcc_notify(phba);
	spin_unlock(&ctrl->mbox_lock);
	return tag;
}

unsigned int mgmt_get_session_info(struct beiscsi_hba *phba,
				   u32 boot_session_handle,
				   struct be_dma_mem *nonemb_cmd)
{
	struct be_ctrl_info *ctrl = &phba->ctrl;
	struct be_mcc_wrb *wrb;
	unsigned int tag = 0;
	struct  be_cmd_get_session_req *req;
	struct be_cmd_get_session_resp *resp;
	struct be_sge *sge;

	beiscsi_log(phba, KERN_INFO,
		    BEISCSI_LOG_CONFIG | BEISCSI_LOG_MBOX,
		    "BG_%d : In beiscsi_get_session_info\n");

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

/**
 * mgmt_get_fw_config()- Get the FW config for the function
 * @ctrl: ptr to Ctrl Info
 * @phba: ptr to the dev priv structure
 *
 * Get the FW config and resources available for the function.
 * The resources are created based on the count received here.
 *
 * return
 *	Success: 0
 *	Failure: Non-Zero Value
 **/
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
			   OPCODE_COMMON_QUERY_FIRMWARE_CONFIG,
			   EMBED_MBX_MAX_PAYLOAD_SIZE);
	status = be_mbox_notify(ctrl);
	if (!status) {
		uint8_t ulp_num = 0;
		struct be_fw_cfg *pfw_cfg;
		pfw_cfg = req;

		for (ulp_num = 0; ulp_num < BEISCSI_ULP_COUNT; ulp_num++)
			if (pfw_cfg->ulp[ulp_num].ulp_mode &
			    BEISCSI_ULP_ISCSI_INI_MODE)
				set_bit(ulp_num,
					&phba->fw_config.ulp_supported);

		phba->fw_config.phys_port = pfw_cfg->phys_port;
		for (ulp_num = 0; ulp_num < BEISCSI_ULP_COUNT; ulp_num++) {
			if (test_bit(ulp_num, &phba->fw_config.ulp_supported)) {

				phba->fw_config.iscsi_cid_start[ulp_num] =
					pfw_cfg->ulp[ulp_num].sq_base;
				phba->fw_config.iscsi_cid_count[ulp_num] =
					pfw_cfg->ulp[ulp_num].sq_count;

				phba->fw_config.iscsi_icd_start[ulp_num] =
					pfw_cfg->ulp[ulp_num].icd_base;
				phba->fw_config.iscsi_icd_count[ulp_num] =
					pfw_cfg->ulp[ulp_num].icd_count;

				phba->fw_config.iscsi_chain_start[ulp_num] =
					pfw_cfg->chain_icd[ulp_num].chain_base;
				phba->fw_config.iscsi_chain_count[ulp_num] =
					pfw_cfg->chain_icd[ulp_num].chain_count;

				beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
					    "BG_%d : Function loaded on ULP : %d\n"
					    "\tiscsi_cid_count : %d\n"
					    "\t iscsi_icd_count : %d\n",
					    ulp_num,
					    phba->fw_config.
					    iscsi_cid_count[ulp_num],
					    phba->fw_config.
					    iscsi_icd_count[ulp_num]);
			}
		}

		phba->fw_config.dual_ulp_aware = (pfw_cfg->function_mode &
						  BEISCSI_FUNC_DUA_MODE);

		beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
			    "BG_%d : DUA Mode : 0x%x\n",
			    phba->fw_config.dual_ulp_aware);

	} else {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BG_%d : Failed in mgmt_get_fw_config\n");
		status = -EINVAL;
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
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BG_%d : Failed to allocate memory for "
			    "mgmt_check_supported_fw\n");
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
		beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
			    "BG_%d : Firmware Version of CMD : %s\n"
			    "Firmware Version is : %s\n"
			    "Developer Build, not performing version check...\n",
			    resp->params.hba_attribs
			    .flashrom_version_string,
			    resp->params.hba_attribs.
			    firmware_version_string);

		phba->fw_config.iscsi_features =
				resp->params.hba_attribs.iscsi_features;
		beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
			    "BM_%d : phba->fw_config.iscsi_features = %d\n",
			    phba->fw_config.iscsi_features);
		memcpy(phba->fw_ver_str, resp->params.hba_attribs.
		       firmware_version_string, BEISCSI_VER_STRLEN);
	} else
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BG_%d :  Failed in mgmt_check_supported_fw\n");
	spin_unlock(&ctrl->mbox_lock);
	if (nonemb_cmd.va)
		pci_free_consistent(ctrl->pdev, nonemb_cmd.size,
				    nonemb_cmd.va, nonemb_cmd.dma);

	return status;
}

unsigned int mgmt_vendor_specific_fw_cmd(struct be_ctrl_info *ctrl,
					 struct beiscsi_hba *phba,
					 struct bsg_job *job,
					 struct be_dma_mem *nonemb_cmd)
{
	struct be_cmd_resp_hdr *resp;
	struct be_mcc_wrb *wrb = wrb_from_mccq(phba);
	struct be_sge *mcc_sge = nonembedded_sgl(wrb);
	unsigned int tag = 0;
	struct iscsi_bsg_request *bsg_req = job->request;
	struct be_bsg_vendor_cmd *req = nonemb_cmd->va;
	unsigned short region, sector_size, sector, offset;

	nonemb_cmd->size = job->request_payload.payload_len;
	memset(nonemb_cmd->va, 0, nonemb_cmd->size);
	resp = nonemb_cmd->va;
	region =  bsg_req->rqst_data.h_vendor.vendor_cmd[1];
	sector_size =  bsg_req->rqst_data.h_vendor.vendor_cmd[2];
	sector =  bsg_req->rqst_data.h_vendor.vendor_cmd[3];
	offset =  bsg_req->rqst_data.h_vendor.vendor_cmd[4];
	req->region = region;
	req->sector = sector;
	req->offset = offset;
	spin_lock(&ctrl->mbox_lock);
	memset(wrb, 0, sizeof(*wrb));

	switch (bsg_req->rqst_data.h_vendor.vendor_cmd[0]) {
	case BEISCSI_WRITE_FLASH:
		offset = sector * sector_size + offset;
		be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ISCSI,
				   OPCODE_COMMON_WRITE_FLASH, sizeof(*req));
		sg_copy_to_buffer(job->request_payload.sg_list,
				  job->request_payload.sg_cnt,
				  nonemb_cmd->va + offset, job->request_len);
		break;
	case BEISCSI_READ_FLASH:
		be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ISCSI,
			   OPCODE_COMMON_READ_FLASH, sizeof(*req));
		break;
	default:
		beiscsi_log(phba, KERN_WARNING, BEISCSI_LOG_CONFIG,
			    "BG_%d : Unsupported cmd = 0x%x\n\n",
			    bsg_req->rqst_data.h_vendor.vendor_cmd[0]);

		spin_unlock(&ctrl->mbox_lock);
		return -ENOSYS;
	}

	tag = alloc_mcc_tag(phba);
	if (!tag) {
		spin_unlock(&ctrl->mbox_lock);
		return tag;
	}

	be_wrb_hdr_prepare(wrb, nonemb_cmd->size, false,
			   job->request_payload.sg_cnt);
	mcc_sge->pa_hi = cpu_to_le32(upper_32_bits(nonemb_cmd->dma));
	mcc_sge->pa_lo = cpu_to_le32(nonemb_cmd->dma & 0xFFFFFFFF);
	mcc_sge->len = cpu_to_le32(nonemb_cmd->size);
	wrb->tag0 |= tag;

	be_mcc_notify(phba);

	spin_unlock(&ctrl->mbox_lock);
	return tag;
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
		beiscsi_log(phba, KERN_WARNING, BEISCSI_LOG_INIT,
			    "BG_%d : mgmt_epfw_cleanup , FAILED\n");
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
		req->ip_address.addr[0] = s_addr & 0x000000ff;
		req->ip_address.addr[1] = (s_addr & 0x0000ff00) >> 8;
		req->ip_address.addr[2] = (s_addr & 0x00ff0000) >> 16;
		req->ip_address.addr[3] = (s_addr & 0xff000000) >> 24;
		req->tcp_port = ntohs(daddr_in->sin_port);
		beiscsi_ep->dst_addr = daddr_in->sin_addr.s_addr;
		beiscsi_ep->dst_tcpport = ntohs(daddr_in->sin_port);
		beiscsi_ep->ip_type = BE2_IPV4;
	} else if (dst_addr->sa_family == PF_INET6) {
		req->ip_address.ip_type = BE2_IPV6;
		memcpy(&req->ip_address.addr,
		       &daddr_in6->sin6_addr.in6_u.u6_addr8, 16);
		req->tcp_port = ntohs(daddr_in6->sin6_port);
		beiscsi_ep->dst_tcpport = ntohs(daddr_in6->sin6_port);
		memcpy(&beiscsi_ep->dst6_addr,
		       &daddr_in6->sin6_addr.in6_u.u6_addr8, 16);
		beiscsi_ep->ip_type = BE2_IPV6;
	} else{
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_CONFIG,
			    "BG_%d : unknown addr family %d\n",
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
	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_CONFIG,
		    "BG_%d : i=%d cq_id=%d\n", i, req->cq_id);
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

unsigned int mgmt_get_all_if_id(struct beiscsi_hba *phba)
{
	struct be_ctrl_info *ctrl = &phba->ctrl;
	struct be_mcc_wrb *wrb = wrb_from_mbox(&ctrl->mbox_mem);
	struct be_cmd_get_all_if_id_req *req = embedded_payload(wrb);
	struct be_cmd_get_all_if_id_req *pbe_allid = req;
	int status = 0;

	memset(wrb, 0, sizeof(*wrb));

	spin_lock(&ctrl->mbox_lock);

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0);
	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ISCSI,
			   OPCODE_COMMON_ISCSI_NTWK_GET_ALL_IF_ID,
			   sizeof(*req));
	status = be_mbox_notify(ctrl);
	if (!status)
		phba->interface_handle = pbe_allid->if_hndl_list[0];
	else {
		beiscsi_log(phba, KERN_WARNING, BEISCSI_LOG_CONFIG,
			    "BG_%d : Failed in mgmt_get_all_if_id\n");
	}
	spin_unlock(&ctrl->mbox_lock);

	return status;
}

/*
 * mgmt_exec_nonemb_cmd()- Execute Non Embedded MBX Cmd
 * @phba: Driver priv structure
 * @nonemb_cmd: Address of the MBX command issued
 * @resp_buf: Buffer to copy the MBX cmd response
 * @resp_buf_len: respone lenght to be copied
 *
 **/
static int mgmt_exec_nonemb_cmd(struct beiscsi_hba *phba,
				struct be_dma_mem *nonemb_cmd, void *resp_buf,
				int resp_buf_len)
{
	struct be_ctrl_info *ctrl = &phba->ctrl;
	struct be_mcc_wrb *wrb = wrb_from_mccq(phba);
	struct be_sge *sge;
	unsigned int tag;
	int rc = 0;

	spin_lock(&ctrl->mbox_lock);
	tag = alloc_mcc_tag(phba);
	if (!tag) {
		spin_unlock(&ctrl->mbox_lock);
		rc = -ENOMEM;
		goto free_cmd;
	}
	memset(wrb, 0, sizeof(*wrb));
	wrb->tag0 |= tag;
	sge = nonembedded_sgl(wrb);

	be_wrb_hdr_prepare(wrb, nonemb_cmd->size, false, 1);
	sge->pa_hi = cpu_to_le32(upper_32_bits(nonemb_cmd->dma));
	sge->pa_lo = cpu_to_le32(lower_32_bits(nonemb_cmd->dma));
	sge->len = cpu_to_le32(nonemb_cmd->size);

	be_mcc_notify(phba);
	spin_unlock(&ctrl->mbox_lock);

	rc = beiscsi_mccq_compl(phba, tag, NULL, nonemb_cmd->va);
	if (rc) {
		beiscsi_log(phba, KERN_ERR,
			    BEISCSI_LOG_CONFIG | BEISCSI_LOG_MBOX,
			    "BG_%d : mgmt_exec_nonemb_cmd Failed status\n");

		rc = -EIO;
		goto free_cmd;
	}

	if (resp_buf)
		memcpy(resp_buf, nonemb_cmd->va, resp_buf_len);

free_cmd:
	pci_free_consistent(ctrl->pdev, nonemb_cmd->size,
			    nonemb_cmd->va, nonemb_cmd->dma);
	return rc;
}

static int mgmt_alloc_cmd_data(struct beiscsi_hba *phba, struct be_dma_mem *cmd,
			       int iscsi_cmd, int size)
{
	cmd->va = pci_alloc_consistent(phba->ctrl.pdev, size, &cmd->dma);
	if (!cmd->va) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_CONFIG,
			    "BG_%d : Failed to allocate memory for if info\n");
		return -ENOMEM;
	}
	memset(cmd->va, 0, size);
	cmd->size = size;
	be_cmd_hdr_prepare(cmd->va, CMD_SUBSYSTEM_ISCSI, iscsi_cmd, size);
	return 0;
}

static int
mgmt_static_ip_modify(struct beiscsi_hba *phba,
		      struct be_cmd_get_if_info_resp *if_info,
		      struct iscsi_iface_param_info *ip_param,
		      struct iscsi_iface_param_info *subnet_param,
		      uint32_t ip_action)
{
	struct be_cmd_set_ip_addr_req *req;
	struct be_dma_mem nonemb_cmd;
	uint32_t ip_type;
	int rc;

	rc = mgmt_alloc_cmd_data(phba, &nonemb_cmd,
				 OPCODE_COMMON_ISCSI_NTWK_MODIFY_IP_ADDR,
				 sizeof(*req));
	if (rc)
		return rc;

	ip_type = (ip_param->param == ISCSI_NET_PARAM_IPV6_ADDR) ?
		BE2_IPV6 : BE2_IPV4 ;

	req = nonemb_cmd.va;
	req->ip_params.record_entry_count = 1;
	req->ip_params.ip_record.action = ip_action;
	req->ip_params.ip_record.interface_hndl =
		phba->interface_handle;
	req->ip_params.ip_record.ip_addr.size_of_structure =
		sizeof(struct be_ip_addr_subnet_format);
	req->ip_params.ip_record.ip_addr.ip_type = ip_type;

	if (ip_action == IP_ACTION_ADD) {
		memcpy(req->ip_params.ip_record.ip_addr.addr, ip_param->value,
		       ip_param->len);

		if (subnet_param)
			memcpy(req->ip_params.ip_record.ip_addr.subnet_mask,
			       subnet_param->value, subnet_param->len);
	} else {
		memcpy(req->ip_params.ip_record.ip_addr.addr,
		       if_info->ip_addr.addr, ip_param->len);

		memcpy(req->ip_params.ip_record.ip_addr.subnet_mask,
		       if_info->ip_addr.subnet_mask, ip_param->len);
	}

	rc = mgmt_exec_nonemb_cmd(phba, &nonemb_cmd, NULL, 0);
	if (rc < 0)
		beiscsi_log(phba, KERN_WARNING, BEISCSI_LOG_CONFIG,
			    "BG_%d : Failed to Modify existing IP Address\n");
	return rc;
}

static int mgmt_modify_gateway(struct beiscsi_hba *phba, uint8_t *gt_addr,
			       uint32_t gtway_action, uint32_t param_len)
{
	struct be_cmd_set_def_gateway_req *req;
	struct be_dma_mem nonemb_cmd;
	int rt_val;


	rt_val = mgmt_alloc_cmd_data(phba, &nonemb_cmd,
				OPCODE_COMMON_ISCSI_NTWK_MODIFY_DEFAULT_GATEWAY,
				sizeof(*req));
	if (rt_val)
		return rt_val;

	req = nonemb_cmd.va;
	req->action = gtway_action;
	req->ip_addr.ip_type = BE2_IPV4;

	memcpy(req->ip_addr.addr, gt_addr, param_len);

	return mgmt_exec_nonemb_cmd(phba, &nonemb_cmd, NULL, 0);
}

int mgmt_set_ip(struct beiscsi_hba *phba,
		struct iscsi_iface_param_info *ip_param,
		struct iscsi_iface_param_info *subnet_param,
		uint32_t boot_proto)
{
	struct be_cmd_get_def_gateway_resp gtway_addr_set;
	struct be_cmd_get_if_info_resp if_info;
	struct be_cmd_set_dhcp_req *dhcpreq;
	struct be_cmd_rel_dhcp_req *reldhcp;
	struct be_dma_mem nonemb_cmd;
	uint8_t *gtway_addr;
	uint32_t ip_type;
	int rc;

	if (mgmt_get_all_if_id(phba))
		return -EIO;

	memset(&if_info, 0, sizeof(if_info));
	ip_type = (ip_param->param == ISCSI_NET_PARAM_IPV6_ADDR) ?
		BE2_IPV6 : BE2_IPV4 ;

	rc = mgmt_get_if_info(phba, ip_type, &if_info);
	if (rc)
		return rc;

	if (boot_proto == ISCSI_BOOTPROTO_DHCP) {
		if (if_info.dhcp_state) {
			beiscsi_log(phba, KERN_WARNING, BEISCSI_LOG_CONFIG,
				    "BG_%d : DHCP Already Enabled\n");
			return 0;
		}
		/* The ip_param->len is 1 in DHCP case. Setting
		   proper IP len as this it is used while
		   freeing the Static IP.
		 */
		ip_param->len = (ip_param->param == ISCSI_NET_PARAM_IPV6_ADDR) ?
				IP_V6_LEN : IP_V4_LEN;

	} else {
		if (if_info.dhcp_state) {

			memset(&if_info, 0, sizeof(if_info));
			rc = mgmt_alloc_cmd_data(phba, &nonemb_cmd,
				OPCODE_COMMON_ISCSI_NTWK_REL_STATELESS_IP_ADDR,
				sizeof(*reldhcp));

			if (rc)
				return rc;

			reldhcp = nonemb_cmd.va;
			reldhcp->interface_hndl = phba->interface_handle;
			reldhcp->ip_type = ip_type;

			rc = mgmt_exec_nonemb_cmd(phba, &nonemb_cmd, NULL, 0);
			if (rc < 0) {
				beiscsi_log(phba, KERN_WARNING,
					    BEISCSI_LOG_CONFIG,
					    "BG_%d : Failed to Delete existing dhcp\n");
				return rc;
			}
		}
	}

	/* Delete the Static IP Set */
	if (if_info.ip_addr.addr[0]) {
		rc = mgmt_static_ip_modify(phba, &if_info, ip_param, NULL,
					   IP_ACTION_DEL);
		if (rc)
			return rc;
	}

	/* Delete the Gateway settings if mode change is to DHCP */
	if (boot_proto == ISCSI_BOOTPROTO_DHCP) {
		memset(&gtway_addr_set, 0, sizeof(gtway_addr_set));
		rc = mgmt_get_gateway(phba, BE2_IPV4, &gtway_addr_set);
		if (rc) {
			beiscsi_log(phba, KERN_WARNING, BEISCSI_LOG_CONFIG,
				    "BG_%d : Failed to Get Gateway Addr\n");
			return rc;
		}

		if (gtway_addr_set.ip_addr.addr[0]) {
			gtway_addr = (uint8_t *)&gtway_addr_set.ip_addr.addr;
			rc = mgmt_modify_gateway(phba, gtway_addr,
						 IP_ACTION_DEL, IP_V4_LEN);

			if (rc) {
				beiscsi_log(phba, KERN_WARNING,
					    BEISCSI_LOG_CONFIG,
					    "BG_%d : Failed to clear Gateway Addr Set\n");
				return rc;
			}
		}
	}

	/* Set Adapter to DHCP/Static Mode */
	if (boot_proto == ISCSI_BOOTPROTO_DHCP) {
		rc = mgmt_alloc_cmd_data(phba, &nonemb_cmd,
			OPCODE_COMMON_ISCSI_NTWK_CONFIG_STATELESS_IP_ADDR,
			sizeof(*dhcpreq));
		if (rc)
			return rc;

		dhcpreq = nonemb_cmd.va;
		dhcpreq->flags = BLOCKING;
		dhcpreq->retry_count = 1;
		dhcpreq->interface_hndl = phba->interface_handle;
		dhcpreq->ip_type = BE2_DHCP_V4;

		return mgmt_exec_nonemb_cmd(phba, &nonemb_cmd, NULL, 0);
	} else {
		return mgmt_static_ip_modify(phba, &if_info, ip_param,
					     subnet_param, IP_ACTION_ADD);
	}

	return rc;
}

int mgmt_set_gateway(struct beiscsi_hba *phba,
		     struct iscsi_iface_param_info *gateway_param)
{
	struct be_cmd_get_def_gateway_resp gtway_addr_set;
	uint8_t *gtway_addr;
	int rt_val;

	memset(&gtway_addr_set, 0, sizeof(gtway_addr_set));
	rt_val = mgmt_get_gateway(phba, BE2_IPV4, &gtway_addr_set);
	if (rt_val) {
		beiscsi_log(phba, KERN_WARNING, BEISCSI_LOG_CONFIG,
			    "BG_%d : Failed to Get Gateway Addr\n");
		return rt_val;
	}

	if (gtway_addr_set.ip_addr.addr[0]) {
		gtway_addr = (uint8_t *)&gtway_addr_set.ip_addr.addr;
		rt_val = mgmt_modify_gateway(phba, gtway_addr, IP_ACTION_DEL,
					     gateway_param->len);
		if (rt_val) {
			beiscsi_log(phba, KERN_WARNING, BEISCSI_LOG_CONFIG,
				    "BG_%d : Failed to clear Gateway Addr Set\n");
			return rt_val;
		}
	}

	gtway_addr = (uint8_t *)&gateway_param->value;
	rt_val = mgmt_modify_gateway(phba, gtway_addr, IP_ACTION_ADD,
				     gateway_param->len);

	if (rt_val)
		beiscsi_log(phba, KERN_WARNING, BEISCSI_LOG_CONFIG,
			    "BG_%d : Failed to Set Gateway Addr\n");

	return rt_val;
}

int mgmt_get_gateway(struct beiscsi_hba *phba, int ip_type,
		     struct be_cmd_get_def_gateway_resp *gateway)
{
	struct be_cmd_get_def_gateway_req *req;
	struct be_dma_mem nonemb_cmd;
	int rc;

	rc = mgmt_alloc_cmd_data(phba, &nonemb_cmd,
				 OPCODE_COMMON_ISCSI_NTWK_GET_DEFAULT_GATEWAY,
				 sizeof(*gateway));
	if (rc)
		return rc;

	req = nonemb_cmd.va;
	req->ip_type = ip_type;

	return mgmt_exec_nonemb_cmd(phba, &nonemb_cmd, gateway,
				    sizeof(*gateway));
}

int mgmt_get_if_info(struct beiscsi_hba *phba, int ip_type,
		     struct be_cmd_get_if_info_resp *if_info)
{
	struct be_cmd_get_if_info_req *req;
	struct be_dma_mem nonemb_cmd;
	int rc;

	if (mgmt_get_all_if_id(phba))
		return -EIO;

	rc = mgmt_alloc_cmd_data(phba, &nonemb_cmd,
				 OPCODE_COMMON_ISCSI_NTWK_GET_IF_INFO,
				 sizeof(*if_info));
	if (rc)
		return rc;

	req = nonemb_cmd.va;
	req->interface_hndl = phba->interface_handle;
	req->ip_type = ip_type;

	return mgmt_exec_nonemb_cmd(phba, &nonemb_cmd, if_info,
				    sizeof(*if_info));
}

int mgmt_get_nic_conf(struct beiscsi_hba *phba,
		      struct be_cmd_get_nic_conf_resp *nic)
{
	struct be_dma_mem nonemb_cmd;
	int rc;

	rc = mgmt_alloc_cmd_data(phba, &nonemb_cmd,
				 OPCODE_COMMON_ISCSI_NTWK_GET_NIC_CONFIG,
				 sizeof(*nic));
	if (rc)
		return rc;

	return mgmt_exec_nonemb_cmd(phba, &nonemb_cmd, nic, sizeof(*nic));
}



unsigned int be_cmd_get_initname(struct beiscsi_hba *phba)
{
	unsigned int tag = 0;
	struct be_mcc_wrb *wrb;
	struct be_cmd_hba_name *req;
	struct be_ctrl_info *ctrl = &phba->ctrl;

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
			OPCODE_ISCSI_INI_CFG_GET_HBA_NAME,
			sizeof(*req));

	be_mcc_notify(phba);
	spin_unlock(&ctrl->mbox_lock);
	return tag;
}

unsigned int be_cmd_get_port_speed(struct beiscsi_hba *phba)
{
	unsigned int tag = 0;
	struct be_mcc_wrb *wrb;
	struct be_cmd_ntwk_link_status_req *req;
	struct be_ctrl_info *ctrl = &phba->ctrl;

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
	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			OPCODE_COMMON_NTWK_LINK_STATUS_QUERY,
			sizeof(*req));

	be_mcc_notify(phba);
	spin_unlock(&ctrl->mbox_lock);
	return tag;
}

/**
 * be_mgmt_get_boot_shandle()- Get the session handle
 * @phba: device priv structure instance
 * @s_handle: session handle returned for boot session.
 *
 * Get the boot target session handle. In case of
 * crashdump mode driver has to issue and MBX Cmd
 * for FW to login to boot target
 *
 * return
 *	Success: 0
 *	Failure: Non-Zero value
 *
 **/
int be_mgmt_get_boot_shandle(struct beiscsi_hba *phba,
			      unsigned int *s_handle)
{
	struct be_cmd_get_boot_target_resp *boot_resp;
	struct be_mcc_wrb *wrb;
	unsigned int tag;
	uint8_t boot_retry = 3;
	int rc;

	do {
		/* Get the Boot Target Session Handle and Count*/
		tag = mgmt_get_boot_target(phba);
		if (!tag) {
			beiscsi_log(phba, KERN_ERR,
				    BEISCSI_LOG_CONFIG | BEISCSI_LOG_INIT,
				    "BG_%d : Getting Boot Target Info Failed\n");
			return -EAGAIN;
		}

		rc = beiscsi_mccq_compl(phba, tag, &wrb, NULL);
		if (rc) {
			beiscsi_log(phba, KERN_ERR,
				    BEISCSI_LOG_INIT | BEISCSI_LOG_CONFIG,
				    "BG_%d : MBX CMD get_boot_target Failed\n");
			return -EBUSY;
		}

		boot_resp = embedded_payload(wrb);

		/* Check if the there are any Boot targets configured */
		if (!boot_resp->boot_session_count) {
			beiscsi_log(phba, KERN_INFO,
				    BEISCSI_LOG_INIT | BEISCSI_LOG_CONFIG,
				    "BG_%d  ;No boot targets configured\n");
			return -ENXIO;
		}

		/* FW returns the session handle of the boot session */
		if (boot_resp->boot_session_handle != INVALID_SESS_HANDLE) {
			*s_handle = boot_resp->boot_session_handle;
			return 0;
		}

		/* Issue MBX Cmd to FW to login to the boot target */
		tag = mgmt_reopen_session(phba, BE_REOPEN_BOOT_SESSIONS,
					  INVALID_SESS_HANDLE);
		if (!tag) {
			beiscsi_log(phba, KERN_ERR,
				    BEISCSI_LOG_INIT | BEISCSI_LOG_CONFIG,
				    "BG_%d : mgmt_reopen_session Failed\n");
			return -EAGAIN;
		}

		rc = beiscsi_mccq_compl(phba, tag, NULL, NULL);
		if (rc) {
			beiscsi_log(phba, KERN_ERR,
				    BEISCSI_LOG_INIT | BEISCSI_LOG_CONFIG,
				    "BG_%d : mgmt_reopen_session Failed");
			return rc;
		}
	} while (--boot_retry);

	/* Couldn't log into the boot target */
	beiscsi_log(phba, KERN_ERR,
		    BEISCSI_LOG_INIT | BEISCSI_LOG_CONFIG,
		    "BG_%d : Login to Boot Target Failed\n");
	return -ENXIO;
}

/**
 * mgmt_set_vlan()- Issue and wait for CMD completion
 * @phba: device private structure instance
 * @vlan_tag: VLAN tag
 *
 * Issue the MBX Cmd and wait for the completion of the
 * command.
 *
 * returns
 *	Success: 0
 *	Failure: Non-Xero Value
 **/
int mgmt_set_vlan(struct beiscsi_hba *phba,
		   uint16_t vlan_tag)
{
	int rc;
	unsigned int tag;
	struct be_mcc_wrb *wrb = NULL;

	tag = be_cmd_set_vlan(phba, vlan_tag);
	if (!tag) {
		beiscsi_log(phba, KERN_ERR,
			    (BEISCSI_LOG_CONFIG | BEISCSI_LOG_MBOX),
			    "BG_%d : VLAN Setting Failed\n");
		return -EBUSY;
	}

	rc = beiscsi_mccq_compl(phba, tag, &wrb, NULL);
	if (rc) {
		beiscsi_log(phba, KERN_ERR,
			    (BEISCSI_LOG_CONFIG | BEISCSI_LOG_MBOX),
			    "BS_%d : VLAN MBX Cmd Failed\n");
		return rc;
	}
	return rc;
}

/**
 * beiscsi_drvr_ver_disp()- Display the driver Name and Version
 * @dev: ptr to device not used.
 * @attr: device attribute, not used.
 * @buf: contains formatted text driver name and version
 *
 * return
 * size of the formatted string
 **/
ssize_t
beiscsi_drvr_ver_disp(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	return snprintf(buf, PAGE_SIZE, BE_NAME "\n");
}

/**
 * beiscsi_fw_ver_disp()- Display Firmware Version
 * @dev: ptr to device not used.
 * @attr: device attribute, not used.
 * @buf: contains formatted text Firmware version
 *
 * return
 * size of the formatted string
 **/
ssize_t
beiscsi_fw_ver_disp(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct beiscsi_hba *phba = iscsi_host_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%s\n", phba->fw_ver_str);
}

/**
 * beiscsi_active_cid_disp()- Display Sessions Active
 * @dev: ptr to device not used.
 * @attr: device attribute, not used.
 * @buf: contains formatted text Session Count
 *
 * return
 * size of the formatted string
 **/
ssize_t
beiscsi_active_cid_disp(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct beiscsi_hba *phba = iscsi_host_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		       (phba->params.cxns_per_ctrl - phba->avlbl_cids));
}

/**
 * beiscsi_adap_family_disp()- Display adapter family.
 * @dev: ptr to device to get priv structure
 * @attr: device attribute, not used.
 * @buf: contains formatted text driver name and version
 *
 * return
 * size of the formatted string
 **/
ssize_t
beiscsi_adap_family_disp(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	uint16_t dev_id = 0;
	struct Scsi_Host *shost = class_to_shost(dev);
	struct beiscsi_hba *phba = iscsi_host_priv(shost);

	dev_id = phba->pcidev->device;
	switch (dev_id) {
	case BE_DEVICE_ID1:
	case OC_DEVICE_ID1:
	case OC_DEVICE_ID2:
		return snprintf(buf, PAGE_SIZE, "BE2 Adapter Family\n");
		break;
	case BE_DEVICE_ID2:
	case OC_DEVICE_ID3:
		return snprintf(buf, PAGE_SIZE, "BE3-R Adapter Family\n");
		break;
	case OC_SKH_ID1:
		return snprintf(buf, PAGE_SIZE, "Skyhawk-R Adapter Family\n");
		break;
	default:
		return snprintf(buf, PAGE_SIZE,
				"Unknown Adapter Family: 0x%x\n", dev_id);
		break;
	}
}


void beiscsi_offload_cxn_v0(struct beiscsi_offload_params *params,
			     struct wrb_handle *pwrb_handle,
			     struct be_mem_descriptor *mem_descr)
{
	struct iscsi_wrb *pwrb = pwrb_handle->pwrb;

	memset(pwrb, 0, sizeof(*pwrb));
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb,
		      max_send_data_segment_length, pwrb,
		      params->dw[offsetof(struct amap_beiscsi_offload_params,
		      max_send_data_segment_length) / 32]);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, type, pwrb,
		      BE_TGT_CTX_UPDT_CMD);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb,
		      first_burst_length,
		      pwrb,
		      params->dw[offsetof(struct amap_beiscsi_offload_params,
		      first_burst_length) / 32]);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, erl, pwrb,
		      (params->dw[offsetof(struct amap_beiscsi_offload_params,
		      erl) / 32] & OFFLD_PARAMS_ERL));
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, dde, pwrb,
		      (params->dw[offsetof(struct amap_beiscsi_offload_params,
		       dde) / 32] & OFFLD_PARAMS_DDE) >> 2);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, hde, pwrb,
		      (params->dw[offsetof(struct amap_beiscsi_offload_params,
		      hde) / 32] & OFFLD_PARAMS_HDE) >> 3);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, ir2t, pwrb,
		      (params->dw[offsetof(struct amap_beiscsi_offload_params,
		      ir2t) / 32] & OFFLD_PARAMS_IR2T) >> 4);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, imd, pwrb,
		      (params->dw[offsetof(struct amap_beiscsi_offload_params,
		      imd) / 32] & OFFLD_PARAMS_IMD) >> 5);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, stat_sn,
		      pwrb,
		      (params->dw[offsetof(struct amap_beiscsi_offload_params,
		      exp_statsn) / 32] + 1));
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, wrb_idx,
		      pwrb, pwrb_handle->wrb_index);

	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb,
		      max_burst_length, pwrb, params->dw[offsetof
		      (struct amap_beiscsi_offload_params,
		      max_burst_length) / 32]);

	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, ptr2nextwrb,
		      pwrb, pwrb_handle->nxt_wrb_index);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb,
		      session_state, pwrb, 0);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, compltonack,
		      pwrb, 1);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, notpredblq,
		      pwrb, 0);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, mode, pwrb,
		      0);

	mem_descr += ISCSI_MEM_GLOBAL_HEADER;
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb,
		      pad_buffer_addr_hi, pwrb,
		      mem_descr->mem_array[0].bus_address.u.a32.address_hi);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb,
		      pad_buffer_addr_lo, pwrb,
		      mem_descr->mem_array[0].bus_address.u.a32.address_lo);
}

void beiscsi_offload_cxn_v2(struct beiscsi_offload_params *params,
			     struct wrb_handle *pwrb_handle)
{
	struct iscsi_wrb *pwrb = pwrb_handle->pwrb;

	memset(pwrb, 0, sizeof(*pwrb));

	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb_v2,
		      max_burst_length, pwrb, params->dw[offsetof
		      (struct amap_beiscsi_offload_params,
		      max_burst_length) / 32]);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb_v2,
		      type, pwrb,
		      BE_TGT_CTX_UPDT_CMD);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb_v2,
		      ptr2nextwrb,
		      pwrb, pwrb_handle->nxt_wrb_index);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb_v2, wrb_idx,
		      pwrb, pwrb_handle->wrb_index);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb_v2,
		      max_send_data_segment_length, pwrb,
		      params->dw[offsetof(struct amap_beiscsi_offload_params,
		      max_send_data_segment_length) / 32]);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb_v2,
		      first_burst_length, pwrb,
		      params->dw[offsetof(struct amap_beiscsi_offload_params,
		      first_burst_length) / 32]);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb_v2,
		      max_recv_dataseg_len, pwrb,
		      params->dw[offsetof(struct amap_beiscsi_offload_params,
		      max_recv_data_segment_length) / 32]);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb_v2,
		      max_cxns, pwrb, BEISCSI_MAX_CXNS);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb_v2, erl, pwrb,
		      (params->dw[offsetof(struct amap_beiscsi_offload_params,
		      erl) / 32] & OFFLD_PARAMS_ERL));
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb_v2, dde, pwrb,
		      (params->dw[offsetof(struct amap_beiscsi_offload_params,
		      dde) / 32] & OFFLD_PARAMS_DDE) >> 2);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb_v2, hde, pwrb,
		      (params->dw[offsetof(struct amap_beiscsi_offload_params,
		      hde) / 32] & OFFLD_PARAMS_HDE) >> 3);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb_v2,
		      ir2t, pwrb,
		      (params->dw[offsetof(struct amap_beiscsi_offload_params,
		      ir2t) / 32] & OFFLD_PARAMS_IR2T) >> 4);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb_v2, imd, pwrb,
		      (params->dw[offsetof(struct amap_beiscsi_offload_params,
		      imd) / 32] & OFFLD_PARAMS_IMD) >> 5);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb_v2,
		      data_seq_inorder,
		      pwrb,
		      (params->dw[offsetof(struct amap_beiscsi_offload_params,
		      data_seq_inorder) / 32] &
		      OFFLD_PARAMS_DATA_SEQ_INORDER) >> 6);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb_v2,
		      pdu_seq_inorder,
		      pwrb,
		      (params->dw[offsetof(struct amap_beiscsi_offload_params,
		      pdu_seq_inorder) / 32] &
		      OFFLD_PARAMS_PDU_SEQ_INORDER) >> 7);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb_v2, max_r2t,
		      pwrb,
		      (params->dw[offsetof(struct amap_beiscsi_offload_params,
		      max_r2t) / 32] &
		      OFFLD_PARAMS_MAX_R2T) >> 8);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb_v2, stat_sn,
		      pwrb,
		     (params->dw[offsetof(struct amap_beiscsi_offload_params,
		      exp_statsn) / 32] + 1));
}
