/*
 * QLogic iSCSI HBA Driver
 * Copyright (c) 2011-2013 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
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

/**
 * qla4xxx_process_vendor_specific - handle vendor specific bsg request
 * @job: iscsi_bsg_job to handle
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
 * @job: iscsi_bsg_job to handle
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
