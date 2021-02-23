// SPDX-License-Identifier: GPL-2.0-only
/*
 * QLogic iSCSI HBA Driver
 * Copyright (c) 2011-2013 QLogic Corporation
 */

#include "ql4_def.h"
#include "ql4_glbl.h"
#include "ql4_bsg.h"

static int
qla4xxx_read_flash(struct bsg_job *bsg_job)
{
	struct Scsi_Host *host = iscsi_job_to_shost(bsg_job);
	struct scsi_qla_host *ha = to_qla_host(host);
	struct iscsi_bsg_reply *bsg_reply = bsg_job->reply;
	struct iscsi_bsg_request *bsg_req = bsg_job->request;
	uint32_t offset = 0;
	uint32_t length = 0;
	dma_addr_t flash_dma;
	uint8_t *flash = NULL;
	int rval = -EINVAL;

	bsg_reply->reply_payload_rcv_len = 0;

	if (unlikely(pci_channel_offline(ha->pdev)))
		goto leave;

	if (ql4xxx_reset_active(ha)) {
		ql4_printk(KERN_ERR, ha, "%s: reset active\n", __func__);
		rval = -EBUSY;
		goto leave;
	}

	if (ha->flash_state != QLFLASH_WAITING) {
		ql4_printk(KERN_ERR, ha, "%s: another flash operation "
			   "active\n", __func__);
		rval = -EBUSY;
		goto leave;
	}

	ha->flash_state = QLFLASH_READING;
	offset = bsg_req->rqst_data.h_vendor.vendor_cmd[1];
	length = bsg_job->reply_payload.payload_len;

	flash = dma_alloc_coherent(&ha->pdev->dev, length, &flash_dma,
				   GFP_KERNEL);
	if (!flash) {
		ql4_printk(KERN_ERR, ha, "%s: dma alloc failed for flash "
			   "data\n", __func__);
		rval = -ENOMEM;
		goto leave;
	}

	rval = qla4xxx_get_flash(ha, flash_dma, offset, length);
	if (rval) {
		ql4_printk(KERN_ERR, ha, "%s: get flash failed\n", __func__);
		bsg_reply->result = DID_ERROR << 16;
		rval = -EIO;
	} else {
		bsg_reply->reply_payload_rcv_len =
			sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
					    bsg_job->reply_payload.sg_cnt,
					    flash, length);
		bsg_reply->result = DID_OK << 16;
	}

	bsg_job_done(bsg_job, bsg_reply->result,
		     bsg_reply->reply_payload_rcv_len);
	dma_free_coherent(&ha->pdev->dev, length, flash, flash_dma);
leave:
	ha->flash_state = QLFLASH_WAITING;
	return rval;
}

static int
qla4xxx_update_flash(struct bsg_job *bsg_job)
{
	struct Scsi_Host *host = iscsi_job_to_shost(bsg_job);
	struct scsi_qla_host *ha = to_qla_host(host);
	struct iscsi_bsg_reply *bsg_reply = bsg_job->reply;
	struct iscsi_bsg_request *bsg_req = bsg_job->request;
	uint32_t length = 0;
	uint32_t offset = 0;
	uint32_t options = 0;
	dma_addr_t flash_dma;
	uint8_t *flash = NULL;
	int rval = -EINVAL;

	bsg_reply->reply_payload_rcv_len = 0;

	if (unlikely(pci_channel_offline(ha->pdev)))
		goto leave;

	if (ql4xxx_reset_active(ha)) {
		ql4_printk(KERN_ERR, ha, "%s: reset active\n", __func__);
		rval = -EBUSY;
		goto leave;
	}

	if (ha->flash_state != QLFLASH_WAITING) {
		ql4_printk(KERN_ERR, ha, "%s: another flash operation "
			   "active\n", __func__);
		rval = -EBUSY;
		goto leave;
	}

	ha->flash_state = QLFLASH_WRITING;
	length = bsg_job->request_payload.payload_len;
	offset = bsg_req->rqst_data.h_vendor.vendor_cmd[1];
	options = bsg_req->rqst_data.h_vendor.vendor_cmd[2];

	flash = dma_alloc_coherent(&ha->pdev->dev, length, &flash_dma,
				   GFP_KERNEL);
	if (!flash) {
		ql4_printk(KERN_ERR, ha, "%s: dma alloc failed for flash "
			   "data\n", __func__);
		rval = -ENOMEM;
		goto leave;
	}

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
			  bsg_job->request_payload.sg_cnt, flash, length);

	rval = qla4xxx_set_flash(ha, flash_dma, offset, length, options);
	if (rval) {
		ql4_printk(KERN_ERR, ha, "%s: set flash failed\n", __func__);
		bsg_reply->result = DID_ERROR << 16;
		rval = -EIO;
	} else
		bsg_reply->result = DID_OK << 16;

	bsg_job_done(bsg_job, bsg_reply->result,
		     bsg_reply->reply_payload_rcv_len);
	dma_free_coherent(&ha->pdev->dev, length, flash, flash_dma);
leave:
	ha->flash_state = QLFLASH_WAITING;
	return rval;
}

static int
qla4xxx_get_acb_state(struct bsg_job *bsg_job)
{
	struct Scsi_Host *host = iscsi_job_to_shost(bsg_job);
	struct scsi_qla_host *ha = to_qla_host(host);
	struct iscsi_bsg_request *bsg_req = bsg_job->request;
	struct iscsi_bsg_reply *bsg_reply = bsg_job->reply;
	uint32_t status[MBOX_REG_COUNT];
	uint32_t acb_idx;
	uint32_t ip_idx;
	int rval = -EINVAL;

	bsg_reply->reply_payload_rcv_len = 0;

	if (unlikely(pci_channel_offline(ha->pdev)))
		goto leave;

	/* Only 4022 and above adapters are supported */
	if (is_qla4010(ha))
		goto leave;

	if (ql4xxx_reset_active(ha)) {
		ql4_printk(KERN_ERR, ha, "%s: reset active\n", __func__);
		rval = -EBUSY;
		goto leave;
	}

	if (bsg_job->reply_payload.payload_len < sizeof(status)) {
		ql4_printk(KERN_ERR, ha, "%s: invalid payload len %d\n",
			   __func__, bsg_job->reply_payload.payload_len);
		rval = -EINVAL;
		goto leave;
	}

	acb_idx = bsg_req->rqst_data.h_vendor.vendor_cmd[1];
	ip_idx = bsg_req->rqst_data.h_vendor.vendor_cmd[2];

	rval = qla4xxx_get_ip_state(ha, acb_idx, ip_idx, status);
	if (rval) {
		ql4_printk(KERN_ERR, ha, "%s: get ip state failed\n",
			   __func__);
		bsg_reply->result = DID_ERROR << 16;
		rval = -EIO;
	} else {
		bsg_reply->reply_payload_rcv_len =
			sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
					    bsg_job->reply_payload.sg_cnt,
					    status, sizeof(status));
		bsg_reply->result = DID_OK << 16;
	}

	bsg_job_done(bsg_job, bsg_reply->result,
		     bsg_reply->reply_payload_rcv_len);
leave:
	return rval;
}

static int
qla4xxx_read_nvram(struct bsg_job *bsg_job)
{
	struct Scsi_Host *host = iscsi_job_to_shost(bsg_job);
	struct scsi_qla_host *ha = to_qla_host(host);
	struct iscsi_bsg_request *bsg_req = bsg_job->request;
	struct iscsi_bsg_reply *bsg_reply = bsg_job->reply;
	uint32_t offset = 0;
	uint32_t len = 0;
	uint32_t total_len = 0;
	dma_addr_t nvram_dma;
	uint8_t *nvram = NULL;
	int rval = -EINVAL;

	bsg_reply->reply_payload_rcv_len = 0;

	if (unlikely(pci_channel_offline(ha->pdev)))
		goto leave;

	/* Only 40xx adapters are supported */
	if (!(is_qla4010(ha) || is_qla4022(ha) || is_qla4032(ha)))
		goto leave;

	if (ql4xxx_reset_active(ha)) {
		ql4_printk(KERN_ERR, ha, "%s: reset active\n", __func__);
		rval = -EBUSY;
		goto leave;
	}

	offset = bsg_req->rqst_data.h_vendor.vendor_cmd[1];
	len = bsg_job->reply_payload.payload_len;
	total_len = offset + len;

	/* total len should not be greater than max NVRAM size */
	if ((is_qla4010(ha) && total_len > QL4010_NVRAM_SIZE) ||
	    ((is_qla4022(ha) || is_qla4032(ha)) &&
	     total_len > QL40X2_NVRAM_SIZE)) {
		ql4_printk(KERN_ERR, ha, "%s: offset+len greater than max"
			   " nvram size, offset=%d len=%d\n",
			   __func__, offset, len);
		goto leave;
	}

	nvram = dma_alloc_coherent(&ha->pdev->dev, len, &nvram_dma,
				   GFP_KERNEL);
	if (!nvram) {
		ql4_printk(KERN_ERR, ha, "%s: dma alloc failed for nvram "
			   "data\n", __func__);
		rval = -ENOMEM;
		goto leave;
	}

	rval = qla4xxx_get_nvram(ha, nvram_dma, offset, len);
	if (rval) {
		ql4_printk(KERN_ERR, ha, "%s: get nvram failed\n", __func__);
		bsg_reply->result = DID_ERROR << 16;
		rval = -EIO;
	} else {
		bsg_reply->reply_payload_rcv_len =
			sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
					    bsg_job->reply_payload.sg_cnt,
					    nvram, len);
		bsg_reply->result = DID_OK << 16;
	}

	bsg_job_done(bsg_job, bsg_reply->result,
		     bsg_reply->reply_payload_rcv_len);
	dma_free_coherent(&ha->pdev->dev, len, nvram, nvram_dma);
leave:
	return rval;
}

static int
qla4xxx_update_nvram(struct bsg_job *bsg_job)
{
	struct Scsi_Host *host = iscsi_job_to_shost(bsg_job);
	struct scsi_qla_host *ha = to_qla_host(host);
	struct iscsi_bsg_request *bsg_req = bsg_job->request;
	struct iscsi_bsg_reply *bsg_reply = bsg_job->reply;
	uint32_t offset = 0;
	uint32_t len = 0;
	uint32_t total_len = 0;
	dma_addr_t nvram_dma;
	uint8_t *nvram = NULL;
	int rval = -EINVAL;

	bsg_reply->reply_payload_rcv_len = 0;

	if (unlikely(pci_channel_offline(ha->pdev)))
		goto leave;

	if (!(is_qla4010(ha) || is_qla4022(ha) || is_qla4032(ha)))
		goto leave;

	if (ql4xxx_reset_active(ha)) {
		ql4_printk(KERN_ERR, ha, "%s: reset active\n", __func__);
		rval = -EBUSY;
		goto leave;
	}

	offset = bsg_req->rqst_data.h_vendor.vendor_cmd[1];
	len = bsg_job->request_payload.payload_len;
	total_len = offset + len;

	/* total len should not be greater than max NVRAM size */
	if ((is_qla4010(ha) && total_len > QL4010_NVRAM_SIZE) ||
	    ((is_qla4022(ha) || is_qla4032(ha)) &&
	     total_len > QL40X2_NVRAM_SIZE)) {
		ql4_printk(KERN_ERR, ha, "%s: offset+len greater than max"
			   " nvram size, offset=%d len=%d\n",
			   __func__, offset, len);
		goto leave;
	}

	nvram = dma_alloc_coherent(&ha->pdev->dev, len, &nvram_dma,
				   GFP_KERNEL);
	if (!nvram) {
		ql4_printk(KERN_ERR, ha, "%s: dma alloc failed for flash "
			   "data\n", __func__);
		rval = -ENOMEM;
		goto leave;
	}

	sg_copy_to_buffer(bsg_job->request_payload.sg_list,
			  bsg_job->request_payload.sg_cnt, nvram, len);

	rval = qla4xxx_set_nvram(ha, nvram_dma, offset, len);
	if (rval) {
		ql4_printk(KERN_ERR, ha, "%s: set nvram failed\n", __func__);
		bsg_reply->result = DID_ERROR << 16;
		rval = -EIO;
	} else
		bsg_reply->result = DID_OK << 16;

	bsg_job_done(bsg_job, bsg_reply->result,
		     bsg_reply->reply_payload_rcv_len);
	dma_free_coherent(&ha->pdev->dev, len, nvram, nvram_dma);
leave:
	return rval;
}

static int
qla4xxx_restore_defaults(struct bsg_job *bsg_job)
{
	struct Scsi_Host *host = iscsi_job_to_shost(bsg_job);
	struct scsi_qla_host *ha = to_qla_host(host);
	struct iscsi_bsg_request *bsg_req = bsg_job->request;
	struct iscsi_bsg_reply *bsg_reply = bsg_job->reply;
	uint32_t region = 0;
	uint32_t field0 = 0;
	uint32_t field1 = 0;
	int rval = -EINVAL;

	bsg_reply->reply_payload_rcv_len = 0;

	if (unlikely(pci_channel_offline(ha->pdev)))
		goto leave;

	if (is_qla4010(ha))
		goto leave;

	if (ql4xxx_reset_active(ha)) {
		ql4_printk(KERN_ERR, ha, "%s: reset active\n", __func__);
		rval = -EBUSY;
		goto leave;
	}

	region = bsg_req->rqst_data.h_vendor.vendor_cmd[1];
	field0 = bsg_req->rqst_data.h_vendor.vendor_cmd[2];
	field1 = bsg_req->rqst_data.h_vendor.vendor_cmd[3];

	rval = qla4xxx_restore_factory_defaults(ha, region, field0, field1);
	if (rval) {
		ql4_printk(KERN_ERR, ha, "%s: set nvram failed\n", __func__);
		bsg_reply->result = DID_ERROR << 16;
		rval = -EIO;
	} else
		bsg_reply->result = DID_OK << 16;

	bsg_job_done(bsg_job, bsg_reply->result,
		     bsg_reply->reply_payload_rcv_len);
leave:
	return rval;
}

static int
qla4xxx_bsg_get_acb(struct bsg_job *bsg_job)
{
	struct Scsi_Host *host = iscsi_job_to_shost(bsg_job);
	struct scsi_qla_host *ha = to_qla_host(host);
	struct iscsi_bsg_request *bsg_req = bsg_job->request;
	struct iscsi_bsg_reply *bsg_reply = bsg_job->reply;
	uint32_t acb_type = 0;
	uint32_t len = 0;
	dma_addr_t acb_dma;
	uint8_t *acb = NULL;
	int rval = -EINVAL;

	bsg_reply->reply_payload_rcv_len = 0;

	if (unlikely(pci_channel_offline(ha->pdev)))
		goto leave;

	/* Only 4022 and above adapters are supported */
	if (is_qla4010(ha))
		goto leave;

	if (ql4xxx_reset_active(ha)) {
		ql4_printk(KERN_ERR, ha, "%s: reset active\n", __func__);
		rval = -EBUSY;
		goto leave;
	}

	acb_type = bsg_req->rqst_data.h_vendor.vendor_cmd[1];
	len = bsg_job->reply_payload.payload_len;
	if (len < sizeof(struct addr_ctrl_blk)) {
		ql4_printk(KERN_ERR, ha, "%s: invalid acb len %d\n",
			   __func__, len);
		rval = -EINVAL;
		goto leave;
	}

	acb = dma_alloc_coherent(&ha->pdev->dev, len, &acb_dma, GFP_KERNEL);
	if (!acb) {
		ql4_printk(KERN_ERR, ha, "%s: dma alloc failed for acb "
			   "data\n", __func__);
		rval = -ENOMEM;
		goto leave;
	}

	rval = qla4xxx_get_acb(ha, acb_dma, acb_type, len);
	if (rval) {
		ql4_printk(KERN_ERR, ha, "%s: get acb failed\n", __func__);
		bsg_reply->result = DID_ERROR << 16;
		rval = -EIO;
	} else {
		bsg_reply->reply_payload_rcv_len =
			sg_copy_from_buffer(bsg_job->reply_payload.sg_list,
					    bsg_job->reply_payload.sg_cnt,
					    acb, len);
		bsg_reply->result = DID_OK << 16;
	}

	bsg_job_done(bsg_job, bsg_reply->result,
		     bsg_reply->reply_payload_rcv_len);
	dma_free_coherent(&ha->pdev->dev, len, acb, acb_dma);
leave:
	return rval;
}

static void ql4xxx_execute_diag_cmd(struct bsg_job *bsg_job)
{
	struct Scsi_Host *host = iscsi_job_to_shost(bsg_job);
	struct scsi_qla_host *ha = to_qla_host(host);
	struct iscsi_bsg_request *bsg_req = bsg_job->request;
	struct iscsi_bsg_reply *bsg_reply = bsg_job->reply;
	uint8_t *rsp_ptr = NULL;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	int status = QLA_ERROR;

	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: in\n", __func__));

	if (test_bit(DPC_RESET_HA, &ha->dpc_flags)) {
		ql4_printk(KERN_INFO, ha, "%s: Adapter reset in progress. Invalid Request\n",
			   __func__);
		bsg_reply->result = DID_ERROR << 16;
		goto exit_diag_mem_test;
	}

	bsg_reply->reply_payload_rcv_len = 0;
	memcpy(mbox_cmd, &bsg_req->rqst_data.h_vendor.vendor_cmd[1],
	       sizeof(uint32_t) * MBOX_REG_COUNT);

	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "%s: mbox_cmd: %08X %08X %08X %08X %08X %08X %08X %08X\n",
			  __func__, mbox_cmd[0], mbox_cmd[1], mbox_cmd[2],
			  mbox_cmd[3], mbox_cmd[4], mbox_cmd[5], mbox_cmd[6],
			  mbox_cmd[7]));

	status = qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 8, &mbox_cmd[0],
					 &mbox_sts[0]);

	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "%s: mbox_sts: %08X %08X %08X %08X %08X %08X %08X %08X\n",
			  __func__, mbox_sts[0], mbox_sts[1], mbox_sts[2],
			  mbox_sts[3], mbox_sts[4], mbox_sts[5], mbox_sts[6],
			  mbox_sts[7]));

	if (status == QLA_SUCCESS)
		bsg_reply->result = DID_OK << 16;
	else
		bsg_reply->result = DID_ERROR << 16;

	/* Send mbox_sts to application */
	bsg_job->reply_len = sizeof(struct iscsi_bsg_reply) + sizeof(mbox_sts);
	rsp_ptr = ((uint8_t *)bsg_reply) + sizeof(struct iscsi_bsg_reply);
	memcpy(rsp_ptr, mbox_sts, sizeof(mbox_sts));

exit_diag_mem_test:
	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "%s: bsg_reply->result = x%x, status = %s\n",
			  __func__, bsg_reply->result, STATUS(status)));

	bsg_job_done(bsg_job, bsg_reply->result,
		     bsg_reply->reply_payload_rcv_len);
}

static int qla4_83xx_wait_for_loopback_config_comp(struct scsi_qla_host *ha,
						   int wait_for_link)
{
	int status = QLA_SUCCESS;

	if (!wait_for_completion_timeout(&ha->idc_comp, (IDC_COMP_TOV * HZ))) {
		ql4_printk(KERN_INFO, ha, "%s: IDC Complete notification not received, Waiting for another %d timeout",
			   __func__, ha->idc_extend_tmo);
		if (ha->idc_extend_tmo) {
			if (!wait_for_completion_timeout(&ha->idc_comp,
						(ha->idc_extend_tmo * HZ))) {
				ha->notify_idc_comp = 0;
				ha->notify_link_up_comp = 0;
				ql4_printk(KERN_WARNING, ha, "%s: Aborting: IDC Complete notification not received",
					   __func__);
				status = QLA_ERROR;
				goto exit_wait;
			} else {
				DEBUG2(ql4_printk(KERN_INFO, ha,
						  "%s: IDC Complete notification received\n",
						  __func__));
			}
		}
	} else {
		DEBUG2(ql4_printk(KERN_INFO, ha,
				  "%s: IDC Complete notification received\n",
				  __func__));
	}
	ha->notify_idc_comp = 0;

	if (wait_for_link) {
		if (!wait_for_completion_timeout(&ha->link_up_comp,
						 (IDC_COMP_TOV * HZ))) {
			ha->notify_link_up_comp = 0;
			ql4_printk(KERN_WARNING, ha, "%s: Aborting: LINK UP notification not received",
				   __func__);
			status = QLA_ERROR;
			goto exit_wait;
		} else {
			DEBUG2(ql4_printk(KERN_INFO, ha,
					  "%s: LINK UP notification received\n",
					  __func__));
		}
		ha->notify_link_up_comp = 0;
	}

exit_wait:
	return status;
}

static int qla4_83xx_pre_loopback_config(struct scsi_qla_host *ha,
					 uint32_t *mbox_cmd)
{
	uint32_t config = 0;
	int status = QLA_SUCCESS;

	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: in\n", __func__));

	status = qla4_83xx_get_port_config(ha, &config);
	if (status != QLA_SUCCESS)
		goto exit_pre_loopback_config;

	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: Default port config=%08X\n",
			  __func__, config));

	if ((config & ENABLE_INTERNAL_LOOPBACK) ||
	    (config & ENABLE_EXTERNAL_LOOPBACK)) {
		ql4_printk(KERN_INFO, ha, "%s: Loopback diagnostics already in progress. Invalid request\n",
			   __func__);
		goto exit_pre_loopback_config;
	}

	if (mbox_cmd[1] == QL_DIAG_CMD_TEST_INT_LOOPBACK)
		config |= ENABLE_INTERNAL_LOOPBACK;

	if (mbox_cmd[1] == QL_DIAG_CMD_TEST_EXT_LOOPBACK)
		config |= ENABLE_EXTERNAL_LOOPBACK;

	config &= ~ENABLE_DCBX;

	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: New port config=%08X\n",
			  __func__, config));

	ha->notify_idc_comp = 1;
	ha->notify_link_up_comp = 1;

	/* get the link state */
	qla4xxx_get_firmware_state(ha);

	status = qla4_83xx_set_port_config(ha, &config);
	if (status != QLA_SUCCESS) {
		ha->notify_idc_comp = 0;
		ha->notify_link_up_comp = 0;
		goto exit_pre_loopback_config;
	}
exit_pre_loopback_config:
	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: status = %s\n", __func__,
			  STATUS(status)));
	return status;
}

static int qla4_83xx_post_loopback_config(struct scsi_qla_host *ha,
					  uint32_t *mbox_cmd)
{
	int status = QLA_SUCCESS;
	uint32_t config = 0;

	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: in\n", __func__));

	status = qla4_83xx_get_port_config(ha, &config);
	if (status != QLA_SUCCESS)
		goto exit_post_loopback_config;

	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: port config=%08X\n", __func__,
			  config));

	if (mbox_cmd[1] == QL_DIAG_CMD_TEST_INT_LOOPBACK)
		config &= ~ENABLE_INTERNAL_LOOPBACK;
	else if (mbox_cmd[1] == QL_DIAG_CMD_TEST_EXT_LOOPBACK)
		config &= ~ENABLE_EXTERNAL_LOOPBACK;

	config |= ENABLE_DCBX;

	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "%s: Restore default port config=%08X\n", __func__,
			  config));

	ha->notify_idc_comp = 1;
	if (ha->addl_fw_state & FW_ADDSTATE_LINK_UP)
		ha->notify_link_up_comp = 1;

	status = qla4_83xx_set_port_config(ha, &config);
	if (status != QLA_SUCCESS) {
		ql4_printk(KERN_INFO, ha, "%s: Scheduling adapter reset\n",
			   __func__);
		set_bit(DPC_RESET_HA, &ha->dpc_flags);
		clear_bit(AF_LOOPBACK, &ha->flags);
		goto exit_post_loopback_config;
	}

exit_post_loopback_config:
	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: status = %s\n", __func__,
			  STATUS(status)));
	return status;
}

static void qla4xxx_execute_diag_loopback_cmd(struct bsg_job *bsg_job)
{
	struct Scsi_Host *host = iscsi_job_to_shost(bsg_job);
	struct scsi_qla_host *ha = to_qla_host(host);
	struct iscsi_bsg_request *bsg_req = bsg_job->request;
	struct iscsi_bsg_reply *bsg_reply = bsg_job->reply;
	uint8_t *rsp_ptr = NULL;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	int wait_for_link = 1;
	int status = QLA_ERROR;

	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: in\n", __func__));

	bsg_reply->reply_payload_rcv_len = 0;

	if (test_bit(AF_LOOPBACK, &ha->flags)) {
		ql4_printk(KERN_INFO, ha, "%s: Loopback Diagnostics already in progress. Invalid Request\n",
			   __func__);
		bsg_reply->result = DID_ERROR << 16;
		goto exit_loopback_cmd;
	}

	if (test_bit(DPC_RESET_HA, &ha->dpc_flags)) {
		ql4_printk(KERN_INFO, ha, "%s: Adapter reset in progress. Invalid Request\n",
			   __func__);
		bsg_reply->result = DID_ERROR << 16;
		goto exit_loopback_cmd;
	}

	memcpy(mbox_cmd, &bsg_req->rqst_data.h_vendor.vendor_cmd[1],
	       sizeof(uint32_t) * MBOX_REG_COUNT);

	if (is_qla8032(ha) || is_qla8042(ha)) {
		status = qla4_83xx_pre_loopback_config(ha, mbox_cmd);
		if (status != QLA_SUCCESS) {
			bsg_reply->result = DID_ERROR << 16;
			goto exit_loopback_cmd;
		}

		status = qla4_83xx_wait_for_loopback_config_comp(ha,
								 wait_for_link);
		if (status != QLA_SUCCESS) {
			bsg_reply->result = DID_TIME_OUT << 16;
			goto restore;
		}
	}

	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "%s: mbox_cmd: %08X %08X %08X %08X %08X %08X %08X %08X\n",
			  __func__, mbox_cmd[0], mbox_cmd[1], mbox_cmd[2],
			  mbox_cmd[3], mbox_cmd[4], mbox_cmd[5], mbox_cmd[6],
			  mbox_cmd[7]));

	status = qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 8, &mbox_cmd[0],
				&mbox_sts[0]);

	if (status == QLA_SUCCESS)
		bsg_reply->result = DID_OK << 16;
	else
		bsg_reply->result = DID_ERROR << 16;

	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "%s: mbox_sts: %08X %08X %08X %08X %08X %08X %08X %08X\n",
			  __func__, mbox_sts[0], mbox_sts[1], mbox_sts[2],
			  mbox_sts[3], mbox_sts[4], mbox_sts[5], mbox_sts[6],
			  mbox_sts[7]));

	/* Send mbox_sts to application */
	bsg_job->reply_len = sizeof(struct iscsi_bsg_reply) + sizeof(mbox_sts);
	rsp_ptr = ((uint8_t *)bsg_reply) + sizeof(struct iscsi_bsg_reply);
	memcpy(rsp_ptr, mbox_sts, sizeof(mbox_sts));
restore:
	if (is_qla8032(ha) || is_qla8042(ha)) {
		status = qla4_83xx_post_loopback_config(ha, mbox_cmd);
		if (status != QLA_SUCCESS) {
			bsg_reply->result = DID_ERROR << 16;
			goto exit_loopback_cmd;
		}

		/* for pre_loopback_config() wait for LINK UP only
		 * if PHY LINK is UP */
		if (!(ha->addl_fw_state & FW_ADDSTATE_LINK_UP))
			wait_for_link = 0;

		status = qla4_83xx_wait_for_loopback_config_comp(ha,
								 wait_for_link);
		if (status != QLA_SUCCESS) {
			bsg_reply->result = DID_TIME_OUT << 16;
			goto exit_loopback_cmd;
		}
	}
exit_loopback_cmd:
	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "%s: bsg_reply->result = x%x, status = %s\n",
			  __func__, bsg_reply->result, STATUS(status)));
	bsg_job_done(bsg_job, bsg_reply->result,
		     bsg_reply->reply_payload_rcv_len);
}

static int qla4xxx_execute_diag_test(struct bsg_job *bsg_job)
{
	struct Scsi_Host *host = iscsi_job_to_shost(bsg_job);
	struct scsi_qla_host *ha = to_qla_host(host);
	struct iscsi_bsg_request *bsg_req = bsg_job->request;
	uint32_t diag_cmd;
	int rval = -EINVAL;

	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: in\n", __func__));

	diag_cmd = bsg_req->rqst_data.h_vendor.vendor_cmd[1];
	if (diag_cmd == MBOX_CMD_DIAG_TEST) {
		switch (bsg_req->rqst_data.h_vendor.vendor_cmd[2]) {
		case QL_DIAG_CMD_TEST_DDR_SIZE:
		case QL_DIAG_CMD_TEST_DDR_RW:
		case QL_DIAG_CMD_TEST_ONCHIP_MEM_RW:
		case QL_DIAG_CMD_TEST_NVRAM:
		case QL_DIAG_CMD_TEST_FLASH_ROM:
		case QL_DIAG_CMD_TEST_DMA_XFER:
		case QL_DIAG_CMD_SELF_DDR_RW:
		case QL_DIAG_CMD_SELF_ONCHIP_MEM_RW:
			/* Execute diag test for adapter RAM/FLASH */
			ql4xxx_execute_diag_cmd(bsg_job);
			/* Always return success as we want to sent bsg_reply
			 * to Application */
			rval = QLA_SUCCESS;
			break;

		case QL_DIAG_CMD_TEST_INT_LOOPBACK:
		case QL_DIAG_CMD_TEST_EXT_LOOPBACK:
			/* Execute diag test for Network */
			qla4xxx_execute_diag_loopback_cmd(bsg_job);
			/* Always return success as we want to sent bsg_reply
			 * to Application */
			rval = QLA_SUCCESS;
			break;
		default:
			ql4_printk(KERN_ERR, ha, "%s: Invalid diag test: 0x%x\n",
				   __func__,
				   bsg_req->rqst_data.h_vendor.vendor_cmd[2]);
		}
	} else if ((diag_cmd == MBOX_CMD_SET_LED_CONFIG) ||
		   (diag_cmd == MBOX_CMD_GET_LED_CONFIG)) {
		ql4xxx_execute_diag_cmd(bsg_job);
		rval = QLA_SUCCESS;
	} else {
		ql4_printk(KERN_ERR, ha, "%s: Invalid diag cmd: 0x%x\n",
			   __func__, diag_cmd);
	}

	return rval;
}

/**
 * qla4xxx_process_vendor_specific - handle vendor specific bsg request
 * @bsg_job: iscsi_bsg_job to handle
 **/
int qla4xxx_process_vendor_specific(struct bsg_job *bsg_job)
{
	struct iscsi_bsg_reply *bsg_reply = bsg_job->reply;
	struct iscsi_bsg_request *bsg_req = bsg_job->request;
	struct Scsi_Host *host = iscsi_job_to_shost(bsg_job);
	struct scsi_qla_host *ha = to_qla_host(host);

	switch (bsg_req->rqst_data.h_vendor.vendor_cmd[0]) {
	case QLISCSI_VND_READ_FLASH:
		return qla4xxx_read_flash(bsg_job);

	case QLISCSI_VND_UPDATE_FLASH:
		return qla4xxx_update_flash(bsg_job);

	case QLISCSI_VND_GET_ACB_STATE:
		return qla4xxx_get_acb_state(bsg_job);

	case QLISCSI_VND_READ_NVRAM:
		return qla4xxx_read_nvram(bsg_job);

	case QLISCSI_VND_UPDATE_NVRAM:
		return qla4xxx_update_nvram(bsg_job);

	case QLISCSI_VND_RESTORE_DEFAULTS:
		return qla4xxx_restore_defaults(bsg_job);

	case QLISCSI_VND_GET_ACB:
		return qla4xxx_bsg_get_acb(bsg_job);

	case QLISCSI_VND_DIAG_TEST:
		return qla4xxx_execute_diag_test(bsg_job);

	default:
		ql4_printk(KERN_ERR, ha, "%s: invalid BSG vendor command: "
			   "0x%x\n", __func__, bsg_req->msgcode);
		bsg_reply->result = (DID_ERROR << 16);
		bsg_reply->reply_payload_rcv_len = 0;
		bsg_job_done(bsg_job, bsg_reply->result,
			     bsg_reply->reply_payload_rcv_len);
		return -ENOSYS;
	}
}

/**
 * qla4xxx_bsg_request - handle bsg request from ISCSI transport
 * @bsg_job: iscsi_bsg_job to handle
 */
int qla4xxx_bsg_request(struct bsg_job *bsg_job)
{
	struct iscsi_bsg_request *bsg_req = bsg_job->request;
	struct Scsi_Host *host = iscsi_job_to_shost(bsg_job);
	struct scsi_qla_host *ha = to_qla_host(host);

	switch (bsg_req->msgcode) {
	case ISCSI_BSG_HST_VENDOR:
		return qla4xxx_process_vendor_specific(bsg_job);

	default:
		ql4_printk(KERN_ERR, ha, "%s: invalid BSG command: 0x%x\n",
			   __func__, bsg_req->msgcode);
	}

	return -ENOSYS;
}
