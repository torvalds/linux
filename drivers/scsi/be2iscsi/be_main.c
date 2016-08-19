/**
 * Copyright (C) 2005 - 2015 Emulex
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Written by: Jayamohan Kallickal (jayamohan.kallickal@avagotech.com)
 *
 * Contact Information:
 * linux-drivers@avagotech.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/semaphore.h>
#include <linux/iscsi_boot_sysfs.h>
#include <linux/module.h>
#include <linux/bsg-lib.h>
#include <linux/irq_poll.h>

#include <scsi/libiscsi.h>
#include <scsi/scsi_bsg_iscsi.h>
#include <scsi/scsi_netlink.h>
#include <scsi/scsi_transport_iscsi.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi.h>
#include "be_main.h"
#include "be_iscsi.h"
#include "be_mgmt.h"
#include "be_cmds.h"

static unsigned int be_iopoll_budget = 10;
static unsigned int be_max_phys_size = 64;
static unsigned int enable_msix = 1;

MODULE_DESCRIPTION(DRV_DESC " " BUILD_STR);
MODULE_VERSION(BUILD_STR);
MODULE_AUTHOR("Emulex Corporation");
MODULE_LICENSE("GPL");
module_param(be_iopoll_budget, int, 0);
module_param(enable_msix, int, 0);
module_param(be_max_phys_size, uint, S_IRUGO);
MODULE_PARM_DESC(be_max_phys_size,
		"Maximum Size (In Kilobytes) of physically contiguous "
		"memory that can be allocated. Range is 16 - 128");

#define beiscsi_disp_param(_name)\
ssize_t	\
beiscsi_##_name##_disp(struct device *dev,\
			struct device_attribute *attrib, char *buf)	\
{	\
	struct Scsi_Host *shost = class_to_shost(dev);\
	struct beiscsi_hba *phba = iscsi_host_priv(shost); \
	uint32_t param_val = 0;	\
	param_val = phba->attr_##_name;\
	return snprintf(buf, PAGE_SIZE, "%d\n",\
			phba->attr_##_name);\
}

#define beiscsi_change_param(_name, _minval, _maxval, _defaval)\
int \
beiscsi_##_name##_change(struct beiscsi_hba *phba, uint32_t val)\
{\
	if (val >= _minval && val <= _maxval) {\
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,\
			    "BA_%d : beiscsi_"#_name" updated "\
			    "from 0x%x ==> 0x%x\n",\
			    phba->attr_##_name, val); \
		phba->attr_##_name = val;\
		return 0;\
	} \
	beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT, \
		    "BA_%d beiscsi_"#_name" attribute "\
		    "cannot be updated to 0x%x, "\
		    "range allowed is ["#_minval" - "#_maxval"]\n", val);\
		return -EINVAL;\
}

#define beiscsi_store_param(_name)  \
ssize_t \
beiscsi_##_name##_store(struct device *dev,\
			 struct device_attribute *attr, const char *buf,\
			 size_t count) \
{ \
	struct Scsi_Host  *shost = class_to_shost(dev);\
	struct beiscsi_hba *phba = iscsi_host_priv(shost);\
	uint32_t param_val = 0;\
	if (!isdigit(buf[0]))\
		return -EINVAL;\
	if (sscanf(buf, "%i", &param_val) != 1)\
		return -EINVAL;\
	if (beiscsi_##_name##_change(phba, param_val) == 0) \
		return strlen(buf);\
	else \
		return -EINVAL;\
}

#define beiscsi_init_param(_name, _minval, _maxval, _defval) \
int \
beiscsi_##_name##_init(struct beiscsi_hba *phba, uint32_t val) \
{ \
	if (val >= _minval && val <= _maxval) {\
		phba->attr_##_name = val;\
		return 0;\
	} \
	beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,\
		    "BA_%d beiscsi_"#_name" attribute " \
		    "cannot be updated to 0x%x, "\
		    "range allowed is ["#_minval" - "#_maxval"]\n", val);\
	phba->attr_##_name = _defval;\
	return -EINVAL;\
}

#define BEISCSI_RW_ATTR(_name, _minval, _maxval, _defval, _descp) \
static uint beiscsi_##_name = _defval;\
module_param(beiscsi_##_name, uint, S_IRUGO);\
MODULE_PARM_DESC(beiscsi_##_name, _descp);\
beiscsi_disp_param(_name)\
beiscsi_change_param(_name, _minval, _maxval, _defval)\
beiscsi_store_param(_name)\
beiscsi_init_param(_name, _minval, _maxval, _defval)\
DEVICE_ATTR(beiscsi_##_name, S_IRUGO | S_IWUSR,\
	      beiscsi_##_name##_disp, beiscsi_##_name##_store)

/*
 * When new log level added update the
 * the MAX allowed value for log_enable
 */
BEISCSI_RW_ATTR(log_enable, 0x00,
		0xFF, 0x00, "Enable logging Bit Mask\n"
		"\t\t\t\tInitialization Events	: 0x01\n"
		"\t\t\t\tMailbox Events		: 0x02\n"
		"\t\t\t\tMiscellaneous Events	: 0x04\n"
		"\t\t\t\tError Handling		: 0x08\n"
		"\t\t\t\tIO Path Events		: 0x10\n"
		"\t\t\t\tConfiguration Path	: 0x20\n"
		"\t\t\t\tiSCSI Protocol		: 0x40\n");

DEVICE_ATTR(beiscsi_drvr_ver, S_IRUGO, beiscsi_drvr_ver_disp, NULL);
DEVICE_ATTR(beiscsi_adapter_family, S_IRUGO, beiscsi_adap_family_disp, NULL);
DEVICE_ATTR(beiscsi_fw_ver, S_IRUGO, beiscsi_fw_ver_disp, NULL);
DEVICE_ATTR(beiscsi_phys_port, S_IRUGO, beiscsi_phys_port_disp, NULL);
DEVICE_ATTR(beiscsi_active_session_count, S_IRUGO,
	     beiscsi_active_session_disp, NULL);
DEVICE_ATTR(beiscsi_free_session_count, S_IRUGO,
	     beiscsi_free_session_disp, NULL);
struct device_attribute *beiscsi_attrs[] = {
	&dev_attr_beiscsi_log_enable,
	&dev_attr_beiscsi_drvr_ver,
	&dev_attr_beiscsi_adapter_family,
	&dev_attr_beiscsi_fw_ver,
	&dev_attr_beiscsi_active_session_count,
	&dev_attr_beiscsi_free_session_count,
	&dev_attr_beiscsi_phys_port,
	NULL,
};

static char const *cqe_desc[] = {
	"RESERVED_DESC",
	"SOL_CMD_COMPLETE",
	"SOL_CMD_KILLED_DATA_DIGEST_ERR",
	"CXN_KILLED_PDU_SIZE_EXCEEDS_DSL",
	"CXN_KILLED_BURST_LEN_MISMATCH",
	"CXN_KILLED_AHS_RCVD",
	"CXN_KILLED_HDR_DIGEST_ERR",
	"CXN_KILLED_UNKNOWN_HDR",
	"CXN_KILLED_STALE_ITT_TTT_RCVD",
	"CXN_KILLED_INVALID_ITT_TTT_RCVD",
	"CXN_KILLED_RST_RCVD",
	"CXN_KILLED_TIMED_OUT",
	"CXN_KILLED_RST_SENT",
	"CXN_KILLED_FIN_RCVD",
	"CXN_KILLED_BAD_UNSOL_PDU_RCVD",
	"CXN_KILLED_BAD_WRB_INDEX_ERROR",
	"CXN_KILLED_OVER_RUN_RESIDUAL",
	"CXN_KILLED_UNDER_RUN_RESIDUAL",
	"CMD_KILLED_INVALID_STATSN_RCVD",
	"CMD_KILLED_INVALID_R2T_RCVD",
	"CMD_CXN_KILLED_LUN_INVALID",
	"CMD_CXN_KILLED_ICD_INVALID",
	"CMD_CXN_KILLED_ITT_INVALID",
	"CMD_CXN_KILLED_SEQ_OUTOFORDER",
	"CMD_CXN_KILLED_INVALID_DATASN_RCVD",
	"CXN_INVALIDATE_NOTIFY",
	"CXN_INVALIDATE_INDEX_NOTIFY",
	"CMD_INVALIDATED_NOTIFY",
	"UNSOL_HDR_NOTIFY",
	"UNSOL_DATA_NOTIFY",
	"UNSOL_DATA_DIGEST_ERROR_NOTIFY",
	"DRIVERMSG_NOTIFY",
	"CXN_KILLED_CMND_DATA_NOT_ON_SAME_CONN",
	"SOL_CMD_KILLED_DIF_ERR",
	"CXN_KILLED_SYN_RCVD",
	"CXN_KILLED_IMM_DATA_RCVD"
};

static int beiscsi_slave_configure(struct scsi_device *sdev)
{
	blk_queue_max_segment_size(sdev->request_queue, 65536);
	return 0;
}

static int beiscsi_eh_abort(struct scsi_cmnd *sc)
{
	struct iscsi_cls_session *cls_session;
	struct iscsi_task *aborted_task = (struct iscsi_task *)sc->SCp.ptr;
	struct beiscsi_io_task *aborted_io_task;
	struct iscsi_conn *conn;
	struct beiscsi_conn *beiscsi_conn;
	struct beiscsi_hba *phba;
	struct iscsi_session *session;
	struct invalidate_command_table *inv_tbl;
	struct be_dma_mem nonemb_cmd;
	unsigned int cid, tag, num_invalidate;
	int rc;

	cls_session = starget_to_session(scsi_target(sc->device));
	session = cls_session->dd_data;

	spin_lock_bh(&session->frwd_lock);
	if (!aborted_task || !aborted_task->sc) {
		/* we raced */
		spin_unlock_bh(&session->frwd_lock);
		return SUCCESS;
	}

	aborted_io_task = aborted_task->dd_data;
	if (!aborted_io_task->scsi_cmnd) {
		/* raced or invalid command */
		spin_unlock_bh(&session->frwd_lock);
		return SUCCESS;
	}
	spin_unlock_bh(&session->frwd_lock);
	/* Invalidate WRB Posted for this Task */
	AMAP_SET_BITS(struct amap_iscsi_wrb, invld,
		      aborted_io_task->pwrb_handle->pwrb,
		      1);

	conn = aborted_task->conn;
	beiscsi_conn = conn->dd_data;
	phba = beiscsi_conn->phba;

	/* invalidate iocb */
	cid = beiscsi_conn->beiscsi_conn_cid;
	inv_tbl = phba->inv_tbl;
	memset(inv_tbl, 0x0, sizeof(*inv_tbl));
	inv_tbl->cid = cid;
	inv_tbl->icd = aborted_io_task->psgl_handle->sgl_index;
	num_invalidate = 1;
	nonemb_cmd.va = pci_alloc_consistent(phba->ctrl.pdev,
				sizeof(struct invalidate_commands_params_in),
				&nonemb_cmd.dma);
	if (nonemb_cmd.va == NULL) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_EH,
			    "BM_%d : Failed to allocate memory for"
			    "mgmt_invalidate_icds\n");
		return FAILED;
	}
	nonemb_cmd.size = sizeof(struct invalidate_commands_params_in);

	tag = mgmt_invalidate_icds(phba, inv_tbl, num_invalidate,
				   cid, &nonemb_cmd);
	if (!tag) {
		beiscsi_log(phba, KERN_WARNING, BEISCSI_LOG_EH,
			    "BM_%d : mgmt_invalidate_icds could not be"
			    "submitted\n");
		pci_free_consistent(phba->ctrl.pdev, nonemb_cmd.size,
				    nonemb_cmd.va, nonemb_cmd.dma);

		return FAILED;
	}

	rc = beiscsi_mccq_compl_wait(phba, tag, NULL, &nonemb_cmd);
	if (rc != -EBUSY)
		pci_free_consistent(phba->ctrl.pdev, nonemb_cmd.size,
				    nonemb_cmd.va, nonemb_cmd.dma);

	return iscsi_eh_abort(sc);
}

static int beiscsi_eh_device_reset(struct scsi_cmnd *sc)
{
	struct iscsi_task *abrt_task;
	struct beiscsi_io_task *abrt_io_task;
	struct iscsi_conn *conn;
	struct beiscsi_conn *beiscsi_conn;
	struct beiscsi_hba *phba;
	struct iscsi_session *session;
	struct iscsi_cls_session *cls_session;
	struct invalidate_command_table *inv_tbl;
	struct be_dma_mem nonemb_cmd;
	unsigned int cid, tag, i, num_invalidate;
	int rc;

	/* invalidate iocbs */
	cls_session = starget_to_session(scsi_target(sc->device));
	session = cls_session->dd_data;
	spin_lock_bh(&session->frwd_lock);
	if (!session->leadconn || session->state != ISCSI_STATE_LOGGED_IN) {
		spin_unlock_bh(&session->frwd_lock);
		return FAILED;
	}
	conn = session->leadconn;
	beiscsi_conn = conn->dd_data;
	phba = beiscsi_conn->phba;
	cid = beiscsi_conn->beiscsi_conn_cid;
	inv_tbl = phba->inv_tbl;
	memset(inv_tbl, 0x0, sizeof(*inv_tbl) * BE2_CMDS_PER_CXN);
	num_invalidate = 0;
	for (i = 0; i < conn->session->cmds_max; i++) {
		abrt_task = conn->session->cmds[i];
		abrt_io_task = abrt_task->dd_data;
		if (!abrt_task->sc || abrt_task->state == ISCSI_TASK_FREE)
			continue;

		if (sc->device->lun != abrt_task->sc->device->lun)
			continue;

		/* Invalidate WRB Posted for this Task */
		AMAP_SET_BITS(struct amap_iscsi_wrb, invld,
			      abrt_io_task->pwrb_handle->pwrb,
			      1);

		inv_tbl->cid = cid;
		inv_tbl->icd = abrt_io_task->psgl_handle->sgl_index;
		num_invalidate++;
		inv_tbl++;
	}
	spin_unlock_bh(&session->frwd_lock);
	inv_tbl = phba->inv_tbl;

	nonemb_cmd.va = pci_alloc_consistent(phba->ctrl.pdev,
				sizeof(struct invalidate_commands_params_in),
				&nonemb_cmd.dma);
	if (nonemb_cmd.va == NULL) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_EH,
			    "BM_%d : Failed to allocate memory for"
			    "mgmt_invalidate_icds\n");
		return FAILED;
	}
	nonemb_cmd.size = sizeof(struct invalidate_commands_params_in);
	memset(nonemb_cmd.va, 0, nonemb_cmd.size);
	tag = mgmt_invalidate_icds(phba, inv_tbl, num_invalidate,
				   cid, &nonemb_cmd);
	if (!tag) {
		beiscsi_log(phba, KERN_WARNING, BEISCSI_LOG_EH,
			    "BM_%d : mgmt_invalidate_icds could not be"
			    " submitted\n");
		pci_free_consistent(phba->ctrl.pdev, nonemb_cmd.size,
				    nonemb_cmd.va, nonemb_cmd.dma);
		return FAILED;
	}

	rc = beiscsi_mccq_compl_wait(phba, tag, NULL, &nonemb_cmd);
	if (rc != -EBUSY)
		pci_free_consistent(phba->ctrl.pdev, nonemb_cmd.size,
				    nonemb_cmd.va, nonemb_cmd.dma);
	return iscsi_eh_device_reset(sc);
}

/*------------------- PCI Driver operations and data ----------------- */
static const struct pci_device_id beiscsi_pci_id_table[] = {
	{ PCI_DEVICE(BE_VENDOR_ID, BE_DEVICE_ID1) },
	{ PCI_DEVICE(BE_VENDOR_ID, BE_DEVICE_ID2) },
	{ PCI_DEVICE(BE_VENDOR_ID, OC_DEVICE_ID1) },
	{ PCI_DEVICE(BE_VENDOR_ID, OC_DEVICE_ID2) },
	{ PCI_DEVICE(BE_VENDOR_ID, OC_DEVICE_ID3) },
	{ PCI_DEVICE(ELX_VENDOR_ID, OC_SKH_ID1) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, beiscsi_pci_id_table);


static struct scsi_host_template beiscsi_sht = {
	.module = THIS_MODULE,
	.name = "Emulex 10Gbe open-iscsi Initiator Driver",
	.proc_name = DRV_NAME,
	.queuecommand = iscsi_queuecommand,
	.change_queue_depth = scsi_change_queue_depth,
	.slave_configure = beiscsi_slave_configure,
	.target_alloc = iscsi_target_alloc,
	.eh_abort_handler = beiscsi_eh_abort,
	.eh_device_reset_handler = beiscsi_eh_device_reset,
	.eh_target_reset_handler = iscsi_eh_session_reset,
	.shost_attrs = beiscsi_attrs,
	.sg_tablesize = BEISCSI_SGLIST_ELEMENTS,
	.can_queue = BE2_IO_DEPTH,
	.this_id = -1,
	.max_sectors = BEISCSI_MAX_SECTORS,
	.cmd_per_lun = BEISCSI_CMD_PER_LUN,
	.use_clustering = ENABLE_CLUSTERING,
	.vendor_id = SCSI_NL_VID_TYPE_PCI | BE_VENDOR_ID,
	.track_queue_depth = 1,
};

static struct scsi_transport_template *beiscsi_scsi_transport;

static struct beiscsi_hba *beiscsi_hba_alloc(struct pci_dev *pcidev)
{
	struct beiscsi_hba *phba;
	struct Scsi_Host *shost;

	shost = iscsi_host_alloc(&beiscsi_sht, sizeof(*phba), 0);
	if (!shost) {
		dev_err(&pcidev->dev,
			"beiscsi_hba_alloc - iscsi_host_alloc failed\n");
		return NULL;
	}
	shost->max_id = BE2_MAX_SESSIONS;
	shost->max_channel = 0;
	shost->max_cmd_len = BEISCSI_MAX_CMD_LEN;
	shost->max_lun = BEISCSI_NUM_MAX_LUN;
	shost->transportt = beiscsi_scsi_transport;
	phba = iscsi_host_priv(shost);
	memset(phba, 0, sizeof(*phba));
	phba->shost = shost;
	phba->pcidev = pci_dev_get(pcidev);
	pci_set_drvdata(pcidev, phba);
	phba->interface_handle = 0xFFFFFFFF;

	return phba;
}

static void beiscsi_unmap_pci_function(struct beiscsi_hba *phba)
{
	if (phba->csr_va) {
		iounmap(phba->csr_va);
		phba->csr_va = NULL;
	}
	if (phba->db_va) {
		iounmap(phba->db_va);
		phba->db_va = NULL;
	}
	if (phba->pci_va) {
		iounmap(phba->pci_va);
		phba->pci_va = NULL;
	}
}

static int beiscsi_map_pci_bars(struct beiscsi_hba *phba,
				struct pci_dev *pcidev)
{
	u8 __iomem *addr;
	int pcicfg_reg;

	addr = ioremap_nocache(pci_resource_start(pcidev, 2),
			       pci_resource_len(pcidev, 2));
	if (addr == NULL)
		return -ENOMEM;
	phba->ctrl.csr = addr;
	phba->csr_va = addr;
	phba->csr_pa.u.a64.address = pci_resource_start(pcidev, 2);

	addr = ioremap_nocache(pci_resource_start(pcidev, 4), 128 * 1024);
	if (addr == NULL)
		goto pci_map_err;
	phba->ctrl.db = addr;
	phba->db_va = addr;
	phba->db_pa.u.a64.address =  pci_resource_start(pcidev, 4);

	if (phba->generation == BE_GEN2)
		pcicfg_reg = 1;
	else
		pcicfg_reg = 0;

	addr = ioremap_nocache(pci_resource_start(pcidev, pcicfg_reg),
			       pci_resource_len(pcidev, pcicfg_reg));

	if (addr == NULL)
		goto pci_map_err;
	phba->ctrl.pcicfg = addr;
	phba->pci_va = addr;
	phba->pci_pa.u.a64.address = pci_resource_start(pcidev, pcicfg_reg);
	return 0;

pci_map_err:
	beiscsi_unmap_pci_function(phba);
	return -ENOMEM;
}

static int beiscsi_enable_pci(struct pci_dev *pcidev)
{
	int ret;

	ret = pci_enable_device(pcidev);
	if (ret) {
		dev_err(&pcidev->dev,
			"beiscsi_enable_pci - enable device failed\n");
		return ret;
	}

	ret = pci_request_regions(pcidev, DRV_NAME);
	if (ret) {
		dev_err(&pcidev->dev,
				"beiscsi_enable_pci - request region failed\n");
		goto pci_dev_disable;
	}

	pci_set_master(pcidev);
	ret = pci_set_dma_mask(pcidev, DMA_BIT_MASK(64));
	if (ret) {
		ret = pci_set_dma_mask(pcidev, DMA_BIT_MASK(32));
		if (ret) {
			dev_err(&pcidev->dev, "Could not set PCI DMA Mask\n");
			goto pci_region_release;
		} else {
			ret = pci_set_consistent_dma_mask(pcidev,
							  DMA_BIT_MASK(32));
		}
	} else {
		ret = pci_set_consistent_dma_mask(pcidev, DMA_BIT_MASK(64));
		if (ret) {
			dev_err(&pcidev->dev, "Could not set PCI DMA Mask\n");
			goto pci_region_release;
		}
	}
	return 0;

pci_region_release:
	pci_release_regions(pcidev);
pci_dev_disable:
	pci_disable_device(pcidev);

	return ret;
}

static int be_ctrl_init(struct beiscsi_hba *phba, struct pci_dev *pdev)
{
	struct be_ctrl_info *ctrl = &phba->ctrl;
	struct be_dma_mem *mbox_mem_alloc = &ctrl->mbox_mem_alloced;
	struct be_dma_mem *mbox_mem_align = &ctrl->mbox_mem;
	int status = 0;

	ctrl->pdev = pdev;
	status = beiscsi_map_pci_bars(phba, pdev);
	if (status)
		return status;
	mbox_mem_alloc->size = sizeof(struct be_mcc_mailbox) + 16;
	mbox_mem_alloc->va = pci_alloc_consistent(pdev,
						  mbox_mem_alloc->size,
						  &mbox_mem_alloc->dma);
	if (!mbox_mem_alloc->va) {
		beiscsi_unmap_pci_function(phba);
		return -ENOMEM;
	}

	mbox_mem_align->size = sizeof(struct be_mcc_mailbox);
	mbox_mem_align->va = PTR_ALIGN(mbox_mem_alloc->va, 16);
	mbox_mem_align->dma = PTR_ALIGN(mbox_mem_alloc->dma, 16);
	memset(mbox_mem_align->va, 0, sizeof(struct be_mcc_mailbox));
	mutex_init(&ctrl->mbox_lock);
	spin_lock_init(&phba->ctrl.mcc_lock);

	return status;
}

/**
 * beiscsi_get_params()- Set the config paramters
 * @phba: ptr  device priv structure
 **/
static void beiscsi_get_params(struct beiscsi_hba *phba)
{
	uint32_t total_cid_count = 0;
	uint32_t total_icd_count = 0;
	uint8_t ulp_num = 0;

	total_cid_count = BEISCSI_GET_CID_COUNT(phba, BEISCSI_ULP0) +
			  BEISCSI_GET_CID_COUNT(phba, BEISCSI_ULP1);

	for (ulp_num = 0; ulp_num < BEISCSI_ULP_COUNT; ulp_num++) {
		uint32_t align_mask = 0;
		uint32_t icd_post_per_page = 0;
		uint32_t icd_count_unavailable = 0;
		uint32_t icd_start = 0, icd_count = 0;
		uint32_t icd_start_align = 0, icd_count_align = 0;

		if (test_bit(ulp_num, &phba->fw_config.ulp_supported)) {
			icd_start = phba->fw_config.iscsi_icd_start[ulp_num];
			icd_count = phba->fw_config.iscsi_icd_count[ulp_num];

			/* Get ICD count that can be posted on each page */
			icd_post_per_page = (PAGE_SIZE / (BE2_SGE *
					     sizeof(struct iscsi_sge)));
			align_mask = (icd_post_per_page - 1);

			/* Check if icd_start is aligned ICD per page posting */
			if (icd_start % icd_post_per_page) {
				icd_start_align = ((icd_start +
						    icd_post_per_page) &
						    ~(align_mask));
				phba->fw_config.
					iscsi_icd_start[ulp_num] =
					icd_start_align;
			}

			icd_count_align = (icd_count & ~align_mask);

			/* ICD discarded in the process of alignment */
			if (icd_start_align)
				icd_count_unavailable = ((icd_start_align -
							  icd_start) +
							 (icd_count -
							  icd_count_align));

			/* Updated ICD count available */
			phba->fw_config.iscsi_icd_count[ulp_num] = (icd_count -
					icd_count_unavailable);

			beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
					"BM_%d : Aligned ICD values\n"
					"\t ICD Start : %d\n"
					"\t ICD Count : %d\n"
					"\t ICD Discarded : %d\n",
					phba->fw_config.
					iscsi_icd_start[ulp_num],
					phba->fw_config.
					iscsi_icd_count[ulp_num],
					icd_count_unavailable);
			break;
		}
	}

	total_icd_count = phba->fw_config.iscsi_icd_count[ulp_num];
	phba->params.ios_per_ctrl = (total_icd_count -
				    (total_cid_count +
				     BE2_TMFS + BE2_NOPOUT_REQ));
	phba->params.cxns_per_ctrl = total_cid_count;
	phba->params.asyncpdus_per_ctrl = total_cid_count;
	phba->params.icds_per_ctrl = total_icd_count;
	phba->params.num_sge_per_io = BE2_SGE;
	phba->params.defpdu_hdr_sz = BE2_DEFPDU_HDR_SZ;
	phba->params.defpdu_data_sz = BE2_DEFPDU_DATA_SZ;
	phba->params.eq_timer = 64;
	phba->params.num_eq_entries = 1024;
	phba->params.num_cq_entries = 1024;
	phba->params.wrbs_per_cxn = 256;
}

static void hwi_ring_eq_db(struct beiscsi_hba *phba,
			   unsigned int id, unsigned int clr_interrupt,
			   unsigned int num_processed,
			   unsigned char rearm, unsigned char event)
{
	u32 val = 0;

	if (rearm)
		val |= 1 << DB_EQ_REARM_SHIFT;
	if (clr_interrupt)
		val |= 1 << DB_EQ_CLR_SHIFT;
	if (event)
		val |= 1 << DB_EQ_EVNT_SHIFT;

	val |= num_processed << DB_EQ_NUM_POPPED_SHIFT;
	/* Setting lower order EQ_ID Bits */
	val |= (id & DB_EQ_RING_ID_LOW_MASK);

	/* Setting Higher order EQ_ID Bits */
	val |= (((id >> DB_EQ_HIGH_FEILD_SHIFT) &
		  DB_EQ_RING_ID_HIGH_MASK)
		  << DB_EQ_HIGH_SET_SHIFT);

	iowrite32(val, phba->db_va + DB_EQ_OFFSET);
}

/**
 * be_isr_mcc - The isr routine of the driver.
 * @irq: Not used
 * @dev_id: Pointer to host adapter structure
 */
static irqreturn_t be_isr_mcc(int irq, void *dev_id)
{
	struct beiscsi_hba *phba;
	struct be_eq_entry *eqe;
	struct be_queue_info *eq;
	struct be_queue_info *mcc;
	unsigned int mcc_events;
	struct be_eq_obj *pbe_eq;

	pbe_eq = dev_id;
	eq = &pbe_eq->q;
	phba =  pbe_eq->phba;
	mcc = &phba->ctrl.mcc_obj.cq;
	eqe = queue_tail_node(eq);

	mcc_events = 0;
	while (eqe->dw[offsetof(struct amap_eq_entry, valid) / 32]
				& EQE_VALID_MASK) {
		if (((eqe->dw[offsetof(struct amap_eq_entry,
		     resource_id) / 32] &
		     EQE_RESID_MASK) >> 16) == mcc->id) {
			mcc_events++;
		}
		AMAP_SET_BITS(struct amap_eq_entry, valid, eqe, 0);
		queue_tail_inc(eq);
		eqe = queue_tail_node(eq);
	}

	if (mcc_events) {
		queue_work(phba->wq, &pbe_eq->mcc_work);
		hwi_ring_eq_db(phba, eq->id, 1,	mcc_events, 1, 1);
	}
	return IRQ_HANDLED;
}

/**
 * be_isr_msix - The isr routine of the driver.
 * @irq: Not used
 * @dev_id: Pointer to host adapter structure
 */
static irqreturn_t be_isr_msix(int irq, void *dev_id)
{
	struct beiscsi_hba *phba;
	struct be_queue_info *eq;
	struct be_eq_obj *pbe_eq;

	pbe_eq = dev_id;
	eq = &pbe_eq->q;

	phba = pbe_eq->phba;
	/* disable interrupt till iopoll completes */
	hwi_ring_eq_db(phba, eq->id, 1,	0, 0, 1);
	irq_poll_sched(&pbe_eq->iopoll);

	return IRQ_HANDLED;
}

/**
 * be_isr - The isr routine of the driver.
 * @irq: Not used
 * @dev_id: Pointer to host adapter structure
 */
static irqreturn_t be_isr(int irq, void *dev_id)
{
	struct beiscsi_hba *phba;
	struct hwi_controller *phwi_ctrlr;
	struct hwi_context_memory *phwi_context;
	struct be_eq_entry *eqe;
	struct be_queue_info *eq;
	struct be_queue_info *mcc;
	unsigned int mcc_events, io_events;
	struct be_ctrl_info *ctrl;
	struct be_eq_obj *pbe_eq;
	int isr, rearm;

	phba = dev_id;
	ctrl = &phba->ctrl;
	isr = ioread32(ctrl->csr + CEV_ISR0_OFFSET +
		       (PCI_FUNC(ctrl->pdev->devfn) * CEV_ISR_SIZE));
	if (!isr)
		return IRQ_NONE;

	phwi_ctrlr = phba->phwi_ctrlr;
	phwi_context = phwi_ctrlr->phwi_ctxt;
	pbe_eq = &phwi_context->be_eq[0];

	eq = &phwi_context->be_eq[0].q;
	mcc = &phba->ctrl.mcc_obj.cq;
	eqe = queue_tail_node(eq);

	io_events = 0;
	mcc_events = 0;
	while (eqe->dw[offsetof(struct amap_eq_entry, valid) / 32]
				& EQE_VALID_MASK) {
		if (((eqe->dw[offsetof(struct amap_eq_entry,
		      resource_id) / 32] & EQE_RESID_MASK) >> 16) == mcc->id)
			mcc_events++;
		else
			io_events++;
		AMAP_SET_BITS(struct amap_eq_entry, valid, eqe, 0);
		queue_tail_inc(eq);
		eqe = queue_tail_node(eq);
	}
	if (!io_events && !mcc_events)
		return IRQ_NONE;

	/* no need to rearm if interrupt is only for IOs */
	rearm = 0;
	if (mcc_events) {
		queue_work(phba->wq, &pbe_eq->mcc_work);
		/* rearm for MCCQ */
		rearm = 1;
	}
	if (io_events)
		irq_poll_sched(&pbe_eq->iopoll);
	hwi_ring_eq_db(phba, eq->id, 0, (io_events + mcc_events), rearm, 1);
	return IRQ_HANDLED;
}


static int beiscsi_init_irqs(struct beiscsi_hba *phba)
{
	struct pci_dev *pcidev = phba->pcidev;
	struct hwi_controller *phwi_ctrlr;
	struct hwi_context_memory *phwi_context;
	int ret, msix_vec, i, j;

	phwi_ctrlr = phba->phwi_ctrlr;
	phwi_context = phwi_ctrlr->phwi_ctxt;

	if (phba->msix_enabled) {
		for (i = 0; i < phba->num_cpus; i++) {
			phba->msi_name[i] = kzalloc(BEISCSI_MSI_NAME,
						    GFP_KERNEL);
			if (!phba->msi_name[i]) {
				ret = -ENOMEM;
				goto free_msix_irqs;
			}

			sprintf(phba->msi_name[i], "beiscsi_%02x_%02x",
				phba->shost->host_no, i);
			msix_vec = phba->msix_entries[i].vector;
			ret = request_irq(msix_vec, be_isr_msix, 0,
					  phba->msi_name[i],
					  &phwi_context->be_eq[i]);
			if (ret) {
				beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
					    "BM_%d : beiscsi_init_irqs-Failed to"
					    "register msix for i = %d\n",
					    i);
				kfree(phba->msi_name[i]);
				goto free_msix_irqs;
			}
		}
		phba->msi_name[i] = kzalloc(BEISCSI_MSI_NAME, GFP_KERNEL);
		if (!phba->msi_name[i]) {
			ret = -ENOMEM;
			goto free_msix_irqs;
		}
		sprintf(phba->msi_name[i], "beiscsi_mcc_%02x",
			phba->shost->host_no);
		msix_vec = phba->msix_entries[i].vector;
		ret = request_irq(msix_vec, be_isr_mcc, 0, phba->msi_name[i],
				  &phwi_context->be_eq[i]);
		if (ret) {
			beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT ,
				    "BM_%d : beiscsi_init_irqs-"
				    "Failed to register beiscsi_msix_mcc\n");
			kfree(phba->msi_name[i]);
			goto free_msix_irqs;
		}

	} else {
		ret = request_irq(pcidev->irq, be_isr, IRQF_SHARED,
				  "beiscsi", phba);
		if (ret) {
			beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
				    "BM_%d : beiscsi_init_irqs-"
				    "Failed to register irq\\n");
			return ret;
		}
	}
	return 0;
free_msix_irqs:
	for (j = i - 1; j >= 0; j--) {
		kfree(phba->msi_name[j]);
		msix_vec = phba->msix_entries[j].vector;
		free_irq(msix_vec, &phwi_context->be_eq[j]);
	}
	return ret;
}

void hwi_ring_cq_db(struct beiscsi_hba *phba,
			   unsigned int id, unsigned int num_processed,
			   unsigned char rearm)
{
	u32 val = 0;

	if (rearm)
		val |= 1 << DB_CQ_REARM_SHIFT;

	val |= num_processed << DB_CQ_NUM_POPPED_SHIFT;

	/* Setting lower order CQ_ID Bits */
	val |= (id & DB_CQ_RING_ID_LOW_MASK);

	/* Setting Higher order CQ_ID Bits */
	val |= (((id >> DB_CQ_HIGH_FEILD_SHIFT) &
		  DB_CQ_RING_ID_HIGH_MASK)
		  << DB_CQ_HIGH_SET_SHIFT);

	iowrite32(val, phba->db_va + DB_CQ_OFFSET);
}

static struct sgl_handle *alloc_io_sgl_handle(struct beiscsi_hba *phba)
{
	struct sgl_handle *psgl_handle;

	spin_lock_bh(&phba->io_sgl_lock);
	if (phba->io_sgl_hndl_avbl) {
		beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_IO,
			    "BM_%d : In alloc_io_sgl_handle,"
			    " io_sgl_alloc_index=%d\n",
			    phba->io_sgl_alloc_index);

		psgl_handle = phba->io_sgl_hndl_base[phba->
						io_sgl_alloc_index];
		phba->io_sgl_hndl_base[phba->io_sgl_alloc_index] = NULL;
		phba->io_sgl_hndl_avbl--;
		if (phba->io_sgl_alloc_index == (phba->params.
						 ios_per_ctrl - 1))
			phba->io_sgl_alloc_index = 0;
		else
			phba->io_sgl_alloc_index++;
	} else
		psgl_handle = NULL;
	spin_unlock_bh(&phba->io_sgl_lock);
	return psgl_handle;
}

static void
free_io_sgl_handle(struct beiscsi_hba *phba, struct sgl_handle *psgl_handle)
{
	spin_lock_bh(&phba->io_sgl_lock);
	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_IO,
		    "BM_%d : In free_,io_sgl_free_index=%d\n",
		    phba->io_sgl_free_index);

	if (phba->io_sgl_hndl_base[phba->io_sgl_free_index]) {
		/*
		 * this can happen if clean_task is called on a task that
		 * failed in xmit_task or alloc_pdu.
		 */
		 beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_IO,
			     "BM_%d : Double Free in IO SGL io_sgl_free_index=%d,"
			     "value there=%p\n", phba->io_sgl_free_index,
			     phba->io_sgl_hndl_base
			     [phba->io_sgl_free_index]);
		 spin_unlock_bh(&phba->io_sgl_lock);
		return;
	}
	phba->io_sgl_hndl_base[phba->io_sgl_free_index] = psgl_handle;
	phba->io_sgl_hndl_avbl++;
	if (phba->io_sgl_free_index == (phba->params.ios_per_ctrl - 1))
		phba->io_sgl_free_index = 0;
	else
		phba->io_sgl_free_index++;
	spin_unlock_bh(&phba->io_sgl_lock);
}

static inline struct wrb_handle *
beiscsi_get_wrb_handle(struct hwi_wrb_context *pwrb_context,
		       unsigned int wrbs_per_cxn)
{
	struct wrb_handle *pwrb_handle;

	spin_lock_bh(&pwrb_context->wrb_lock);
	pwrb_handle = pwrb_context->pwrb_handle_base[pwrb_context->alloc_index];
	pwrb_context->wrb_handles_available--;
	if (pwrb_context->alloc_index == (wrbs_per_cxn - 1))
		pwrb_context->alloc_index = 0;
	else
		pwrb_context->alloc_index++;
	spin_unlock_bh(&pwrb_context->wrb_lock);
	memset(pwrb_handle->pwrb, 0, sizeof(*pwrb_handle->pwrb));

	return pwrb_handle;
}

/**
 * alloc_wrb_handle - To allocate a wrb handle
 * @phba: The hba pointer
 * @cid: The cid to use for allocation
 * @pwrb_context: ptr to ptr to wrb context
 *
 * This happens under session_lock until submission to chip
 */
struct wrb_handle *alloc_wrb_handle(struct beiscsi_hba *phba, unsigned int cid,
				    struct hwi_wrb_context **pcontext)
{
	struct hwi_wrb_context *pwrb_context;
	struct hwi_controller *phwi_ctrlr;
	uint16_t cri_index = BE_GET_CRI_FROM_CID(cid);

	phwi_ctrlr = phba->phwi_ctrlr;
	pwrb_context = &phwi_ctrlr->wrb_context[cri_index];
	/* return the context address */
	*pcontext = pwrb_context;
	return beiscsi_get_wrb_handle(pwrb_context, phba->params.wrbs_per_cxn);
}

static inline void
beiscsi_put_wrb_handle(struct hwi_wrb_context *pwrb_context,
		       struct wrb_handle *pwrb_handle,
		       unsigned int wrbs_per_cxn)
{
	spin_lock_bh(&pwrb_context->wrb_lock);
	pwrb_context->pwrb_handle_base[pwrb_context->free_index] = pwrb_handle;
	pwrb_context->wrb_handles_available++;
	if (pwrb_context->free_index == (wrbs_per_cxn - 1))
		pwrb_context->free_index = 0;
	else
		pwrb_context->free_index++;
	spin_unlock_bh(&pwrb_context->wrb_lock);
}

/**
 * free_wrb_handle - To free the wrb handle back to pool
 * @phba: The hba pointer
 * @pwrb_context: The context to free from
 * @pwrb_handle: The wrb_handle to free
 *
 * This happens under session_lock until submission to chip
 */
static void
free_wrb_handle(struct beiscsi_hba *phba, struct hwi_wrb_context *pwrb_context,
		struct wrb_handle *pwrb_handle)
{
	beiscsi_put_wrb_handle(pwrb_context,
			       pwrb_handle,
			       phba->params.wrbs_per_cxn);
	beiscsi_log(phba, KERN_INFO,
		    BEISCSI_LOG_IO | BEISCSI_LOG_CONFIG,
		    "BM_%d : FREE WRB: pwrb_handle=%p free_index=0x%x"
		    "wrb_handles_available=%d\n",
		    pwrb_handle, pwrb_context->free_index,
		    pwrb_context->wrb_handles_available);
}

static struct sgl_handle *alloc_mgmt_sgl_handle(struct beiscsi_hba *phba)
{
	struct sgl_handle *psgl_handle;

	spin_lock_bh(&phba->mgmt_sgl_lock);
	if (phba->eh_sgl_hndl_avbl) {
		psgl_handle = phba->eh_sgl_hndl_base[phba->eh_sgl_alloc_index];
		phba->eh_sgl_hndl_base[phba->eh_sgl_alloc_index] = NULL;
		beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_CONFIG,
			    "BM_%d : mgmt_sgl_alloc_index=%d=0x%x\n",
			    phba->eh_sgl_alloc_index,
			    phba->eh_sgl_alloc_index);

		phba->eh_sgl_hndl_avbl--;
		if (phba->eh_sgl_alloc_index ==
		    (phba->params.icds_per_ctrl - phba->params.ios_per_ctrl -
		     1))
			phba->eh_sgl_alloc_index = 0;
		else
			phba->eh_sgl_alloc_index++;
	} else
		psgl_handle = NULL;
	spin_unlock_bh(&phba->mgmt_sgl_lock);
	return psgl_handle;
}

void
free_mgmt_sgl_handle(struct beiscsi_hba *phba, struct sgl_handle *psgl_handle)
{
	spin_lock_bh(&phba->mgmt_sgl_lock);
	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_CONFIG,
		    "BM_%d : In  free_mgmt_sgl_handle,"
		    "eh_sgl_free_index=%d\n",
		    phba->eh_sgl_free_index);

	if (phba->eh_sgl_hndl_base[phba->eh_sgl_free_index]) {
		/*
		 * this can happen if clean_task is called on a task that
		 * failed in xmit_task or alloc_pdu.
		 */
		beiscsi_log(phba, KERN_WARNING, BEISCSI_LOG_CONFIG,
			    "BM_%d : Double Free in eh SGL ,"
			    "eh_sgl_free_index=%d\n",
			    phba->eh_sgl_free_index);
		spin_unlock_bh(&phba->mgmt_sgl_lock);
		return;
	}
	phba->eh_sgl_hndl_base[phba->eh_sgl_free_index] = psgl_handle;
	phba->eh_sgl_hndl_avbl++;
	if (phba->eh_sgl_free_index ==
	    (phba->params.icds_per_ctrl - phba->params.ios_per_ctrl - 1))
		phba->eh_sgl_free_index = 0;
	else
		phba->eh_sgl_free_index++;
	spin_unlock_bh(&phba->mgmt_sgl_lock);
}

static void
be_complete_io(struct beiscsi_conn *beiscsi_conn,
		struct iscsi_task *task,
		struct common_sol_cqe *csol_cqe)
{
	struct beiscsi_io_task *io_task = task->dd_data;
	struct be_status_bhs *sts_bhs =
				(struct be_status_bhs *)io_task->cmd_bhs;
	struct iscsi_conn *conn = beiscsi_conn->conn;
	unsigned char *sense;
	u32 resid = 0, exp_cmdsn, max_cmdsn;
	u8 rsp, status, flags;

	exp_cmdsn = csol_cqe->exp_cmdsn;
	max_cmdsn = (csol_cqe->exp_cmdsn +
		     csol_cqe->cmd_wnd - 1);
	rsp = csol_cqe->i_resp;
	status = csol_cqe->i_sts;
	flags = csol_cqe->i_flags;
	resid = csol_cqe->res_cnt;

	if (!task->sc) {
		if (io_task->scsi_cmnd) {
			scsi_dma_unmap(io_task->scsi_cmnd);
			io_task->scsi_cmnd = NULL;
		}

		return;
	}
	task->sc->result = (DID_OK << 16) | status;
	if (rsp != ISCSI_STATUS_CMD_COMPLETED) {
		task->sc->result = DID_ERROR << 16;
		goto unmap;
	}

	/* bidi not initially supported */
	if (flags & (ISCSI_FLAG_CMD_UNDERFLOW | ISCSI_FLAG_CMD_OVERFLOW)) {
		if (!status && (flags & ISCSI_FLAG_CMD_OVERFLOW))
			task->sc->result = DID_ERROR << 16;

		if (flags & ISCSI_FLAG_CMD_UNDERFLOW) {
			scsi_set_resid(task->sc, resid);
			if (!status && (scsi_bufflen(task->sc) - resid <
			    task->sc->underflow))
				task->sc->result = DID_ERROR << 16;
		}
	}

	if (status == SAM_STAT_CHECK_CONDITION) {
		u16 sense_len;
		unsigned short *slen = (unsigned short *)sts_bhs->sense_info;

		sense = sts_bhs->sense_info + sizeof(unsigned short);
		sense_len = be16_to_cpu(*slen);
		memcpy(task->sc->sense_buffer, sense,
		       min_t(u16, sense_len, SCSI_SENSE_BUFFERSIZE));
	}

	if (io_task->cmd_bhs->iscsi_hdr.flags & ISCSI_FLAG_CMD_READ)
		conn->rxdata_octets += resid;
unmap:
	if (io_task->scsi_cmnd) {
		scsi_dma_unmap(io_task->scsi_cmnd);
		io_task->scsi_cmnd = NULL;
	}
	iscsi_complete_scsi_task(task, exp_cmdsn, max_cmdsn);
}

static void
be_complete_logout(struct beiscsi_conn *beiscsi_conn,
		    struct iscsi_task *task,
		    struct common_sol_cqe *csol_cqe)
{
	struct iscsi_logout_rsp *hdr;
	struct beiscsi_io_task *io_task = task->dd_data;
	struct iscsi_conn *conn = beiscsi_conn->conn;

	hdr = (struct iscsi_logout_rsp *)task->hdr;
	hdr->opcode = ISCSI_OP_LOGOUT_RSP;
	hdr->t2wait = 5;
	hdr->t2retain = 0;
	hdr->flags = csol_cqe->i_flags;
	hdr->response = csol_cqe->i_resp;
	hdr->exp_cmdsn = cpu_to_be32(csol_cqe->exp_cmdsn);
	hdr->max_cmdsn = cpu_to_be32(csol_cqe->exp_cmdsn +
				     csol_cqe->cmd_wnd - 1);

	hdr->dlength[0] = 0;
	hdr->dlength[1] = 0;
	hdr->dlength[2] = 0;
	hdr->hlength = 0;
	hdr->itt = io_task->libiscsi_itt;
	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)hdr, NULL, 0);
}

static void
be_complete_tmf(struct beiscsi_conn *beiscsi_conn,
		 struct iscsi_task *task,
		 struct common_sol_cqe *csol_cqe)
{
	struct iscsi_tm_rsp *hdr;
	struct iscsi_conn *conn = beiscsi_conn->conn;
	struct beiscsi_io_task *io_task = task->dd_data;

	hdr = (struct iscsi_tm_rsp *)task->hdr;
	hdr->opcode = ISCSI_OP_SCSI_TMFUNC_RSP;
	hdr->flags = csol_cqe->i_flags;
	hdr->response = csol_cqe->i_resp;
	hdr->exp_cmdsn = cpu_to_be32(csol_cqe->exp_cmdsn);
	hdr->max_cmdsn = cpu_to_be32(csol_cqe->exp_cmdsn +
				     csol_cqe->cmd_wnd - 1);

	hdr->itt = io_task->libiscsi_itt;
	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)hdr, NULL, 0);
}

static void
hwi_complete_drvr_msgs(struct beiscsi_conn *beiscsi_conn,
		       struct beiscsi_hba *phba, struct sol_cqe *psol)
{
	struct hwi_wrb_context *pwrb_context;
	uint16_t wrb_index, cid, cri_index;
	struct hwi_controller *phwi_ctrlr;
	struct wrb_handle *pwrb_handle;
	struct iscsi_task *task;

	phwi_ctrlr = phba->phwi_ctrlr;
	if (is_chip_be2_be3r(phba)) {
		wrb_index = AMAP_GET_BITS(struct amap_it_dmsg_cqe,
					  wrb_idx, psol);
		cid = AMAP_GET_BITS(struct amap_it_dmsg_cqe,
				    cid, psol);
	} else {
		wrb_index = AMAP_GET_BITS(struct amap_it_dmsg_cqe_v2,
					  wrb_idx, psol);
		cid = AMAP_GET_BITS(struct amap_it_dmsg_cqe_v2,
				    cid, psol);
	}

	cri_index = BE_GET_CRI_FROM_CID(cid);
	pwrb_context = &phwi_ctrlr->wrb_context[cri_index];
	pwrb_handle = pwrb_context->pwrb_handle_basestd[wrb_index];
	task = pwrb_handle->pio_handle;
	iscsi_put_task(task);
}

static void
be_complete_nopin_resp(struct beiscsi_conn *beiscsi_conn,
			struct iscsi_task *task,
			struct common_sol_cqe *csol_cqe)
{
	struct iscsi_nopin *hdr;
	struct iscsi_conn *conn = beiscsi_conn->conn;
	struct beiscsi_io_task *io_task = task->dd_data;

	hdr = (struct iscsi_nopin *)task->hdr;
	hdr->flags = csol_cqe->i_flags;
	hdr->exp_cmdsn = cpu_to_be32(csol_cqe->exp_cmdsn);
	hdr->max_cmdsn = cpu_to_be32(csol_cqe->exp_cmdsn +
				     csol_cqe->cmd_wnd - 1);

	hdr->opcode = ISCSI_OP_NOOP_IN;
	hdr->itt = io_task->libiscsi_itt;
	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)hdr, NULL, 0);
}

static void adapter_get_sol_cqe(struct beiscsi_hba *phba,
		struct sol_cqe *psol,
		struct common_sol_cqe *csol_cqe)
{
	if (is_chip_be2_be3r(phba)) {
		csol_cqe->exp_cmdsn = AMAP_GET_BITS(struct amap_sol_cqe,
						    i_exp_cmd_sn, psol);
		csol_cqe->res_cnt = AMAP_GET_BITS(struct amap_sol_cqe,
						  i_res_cnt, psol);
		csol_cqe->cmd_wnd = AMAP_GET_BITS(struct amap_sol_cqe,
						  i_cmd_wnd, psol);
		csol_cqe->wrb_index = AMAP_GET_BITS(struct amap_sol_cqe,
						    wrb_index, psol);
		csol_cqe->cid = AMAP_GET_BITS(struct amap_sol_cqe,
					      cid, psol);
		csol_cqe->hw_sts = AMAP_GET_BITS(struct amap_sol_cqe,
						 hw_sts, psol);
		csol_cqe->i_resp = AMAP_GET_BITS(struct amap_sol_cqe,
						 i_resp, psol);
		csol_cqe->i_sts = AMAP_GET_BITS(struct amap_sol_cqe,
						i_sts, psol);
		csol_cqe->i_flags = AMAP_GET_BITS(struct amap_sol_cqe,
						  i_flags, psol);
	} else {
		csol_cqe->exp_cmdsn = AMAP_GET_BITS(struct amap_sol_cqe_v2,
						    i_exp_cmd_sn, psol);
		csol_cqe->res_cnt = AMAP_GET_BITS(struct amap_sol_cqe_v2,
						  i_res_cnt, psol);
		csol_cqe->wrb_index = AMAP_GET_BITS(struct amap_sol_cqe_v2,
						    wrb_index, psol);
		csol_cqe->cid = AMAP_GET_BITS(struct amap_sol_cqe_v2,
					      cid, psol);
		csol_cqe->hw_sts = AMAP_GET_BITS(struct amap_sol_cqe_v2,
						 hw_sts, psol);
		csol_cqe->cmd_wnd = AMAP_GET_BITS(struct amap_sol_cqe_v2,
						  i_cmd_wnd, psol);
		if (AMAP_GET_BITS(struct amap_sol_cqe_v2,
				  cmd_cmpl, psol))
			csol_cqe->i_sts = AMAP_GET_BITS(struct amap_sol_cqe_v2,
							i_sts, psol);
		else
			csol_cqe->i_resp = AMAP_GET_BITS(struct amap_sol_cqe_v2,
							 i_sts, psol);
		if (AMAP_GET_BITS(struct amap_sol_cqe_v2,
				  u, psol))
			csol_cqe->i_flags = ISCSI_FLAG_CMD_UNDERFLOW;

		if (AMAP_GET_BITS(struct amap_sol_cqe_v2,
				  o, psol))
			csol_cqe->i_flags |= ISCSI_FLAG_CMD_OVERFLOW;
	}
}


static void hwi_complete_cmd(struct beiscsi_conn *beiscsi_conn,
			     struct beiscsi_hba *phba, struct sol_cqe *psol)
{
	struct hwi_wrb_context *pwrb_context;
	struct wrb_handle *pwrb_handle;
	struct iscsi_wrb *pwrb = NULL;
	struct hwi_controller *phwi_ctrlr;
	struct iscsi_task *task;
	unsigned int type;
	struct iscsi_conn *conn = beiscsi_conn->conn;
	struct iscsi_session *session = conn->session;
	struct common_sol_cqe csol_cqe = {0};
	uint16_t cri_index = 0;

	phwi_ctrlr = phba->phwi_ctrlr;

	/* Copy the elements to a common structure */
	adapter_get_sol_cqe(phba, psol, &csol_cqe);

	cri_index = BE_GET_CRI_FROM_CID(csol_cqe.cid);
	pwrb_context = &phwi_ctrlr->wrb_context[cri_index];

	pwrb_handle = pwrb_context->pwrb_handle_basestd[
		      csol_cqe.wrb_index];

	task = pwrb_handle->pio_handle;
	pwrb = pwrb_handle->pwrb;
	type = ((struct beiscsi_io_task *)task->dd_data)->wrb_type;

	spin_lock_bh(&session->back_lock);
	switch (type) {
	case HWH_TYPE_IO:
	case HWH_TYPE_IO_RD:
		if ((task->hdr->opcode & ISCSI_OPCODE_MASK) ==
		     ISCSI_OP_NOOP_OUT)
			be_complete_nopin_resp(beiscsi_conn, task, &csol_cqe);
		else
			be_complete_io(beiscsi_conn, task, &csol_cqe);
		break;

	case HWH_TYPE_LOGOUT:
		if ((task->hdr->opcode & ISCSI_OPCODE_MASK) == ISCSI_OP_LOGOUT)
			be_complete_logout(beiscsi_conn, task, &csol_cqe);
		else
			be_complete_tmf(beiscsi_conn, task, &csol_cqe);
		break;

	case HWH_TYPE_LOGIN:
		beiscsi_log(phba, KERN_ERR,
			    BEISCSI_LOG_CONFIG | BEISCSI_LOG_IO,
			    "BM_%d :\t\t No HWH_TYPE_LOGIN Expected in"
			    " hwi_complete_cmd- Solicited path\n");
		break;

	case HWH_TYPE_NOP:
		be_complete_nopin_resp(beiscsi_conn, task, &csol_cqe);
		break;

	default:
		beiscsi_log(phba, KERN_WARNING,
			    BEISCSI_LOG_CONFIG | BEISCSI_LOG_IO,
			    "BM_%d : In hwi_complete_cmd, unknown type = %d"
			    "wrb_index 0x%x CID 0x%x\n", type,
			    csol_cqe.wrb_index,
			    csol_cqe.cid);
		break;
	}

	spin_unlock_bh(&session->back_lock);
}

/**
 * ASYNC PDUs include
 * a. Unsolicited NOP-In (target initiated NOP-In)
 * b. ASYNC Messages
 * c. Reject PDU
 * d. Login response
 * These headers arrive unprocessed by the EP firmware.
 * iSCSI layer processes them.
 */
static unsigned int
beiscsi_complete_pdu(struct beiscsi_conn *beiscsi_conn,
		struct pdu_base *phdr, void *pdata, unsigned int dlen)
{
	struct beiscsi_hba *phba = beiscsi_conn->phba;
	struct iscsi_conn *conn = beiscsi_conn->conn;
	struct beiscsi_io_task *io_task;
	struct iscsi_hdr *login_hdr;
	struct iscsi_task *task;
	u8 code;

	code = AMAP_GET_BITS(struct amap_pdu_base, opcode, phdr);
	switch (code) {
	case ISCSI_OP_NOOP_IN:
		pdata = NULL;
		dlen = 0;
		break;
	case ISCSI_OP_ASYNC_EVENT:
		break;
	case ISCSI_OP_REJECT:
		WARN_ON(!pdata);
		WARN_ON(!(dlen == 48));
		beiscsi_log(phba, KERN_ERR,
			    BEISCSI_LOG_CONFIG | BEISCSI_LOG_IO,
			    "BM_%d : In ISCSI_OP_REJECT\n");
		break;
	case ISCSI_OP_LOGIN_RSP:
	case ISCSI_OP_TEXT_RSP:
		task = conn->login_task;
		io_task = task->dd_data;
		login_hdr = (struct iscsi_hdr *)phdr;
		login_hdr->itt = io_task->libiscsi_itt;
		break;
	default:
		beiscsi_log(phba, KERN_WARNING,
			    BEISCSI_LOG_IO | BEISCSI_LOG_CONFIG,
			    "BM_%d : unrecognized async PDU opcode 0x%x\n",
			    code);
		return 1;
	}
	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)phdr, pdata, dlen);
	return 0;
}

static inline void
beiscsi_hdl_put_handle(struct hd_async_context *pasync_ctx,
			 struct hd_async_handle *pasync_handle)
{
	if (pasync_handle->is_header) {
		list_add_tail(&pasync_handle->link,
				&pasync_ctx->async_header.free_list);
		pasync_ctx->async_header.free_entries++;
	} else {
		list_add_tail(&pasync_handle->link,
				&pasync_ctx->async_data.free_list);
		pasync_ctx->async_data.free_entries++;
	}
}

static struct hd_async_handle *
beiscsi_hdl_get_handle(struct beiscsi_conn *beiscsi_conn,
		       struct hd_async_context *pasync_ctx,
		       struct i_t_dpdu_cqe *pdpdu_cqe)
{
	struct beiscsi_hba *phba = beiscsi_conn->phba;
	struct hd_async_handle *pasync_handle;
	struct be_bus_address phys_addr;
	u8 final, error = 0;
	u16 cid, code, ci;
	u32 dpl;

	cid = beiscsi_conn->beiscsi_conn_cid;
	/**
	 * This function is invoked to get the right async_handle structure
	 * from a given DEF PDU CQ entry.
	 *
	 * - index in CQ entry gives the vertical index
	 * - address in CQ entry is the offset where the DMA last ended
	 * - final - no more notifications for this PDU
	 */
	if (is_chip_be2_be3r(phba)) {
		dpl = AMAP_GET_BITS(struct amap_i_t_dpdu_cqe,
				    dpl, pdpdu_cqe);
		ci = AMAP_GET_BITS(struct amap_i_t_dpdu_cqe,
				      index, pdpdu_cqe);
		final = AMAP_GET_BITS(struct amap_i_t_dpdu_cqe,
				      final, pdpdu_cqe);
	} else {
		dpl = AMAP_GET_BITS(struct amap_i_t_dpdu_cqe_v2,
				    dpl, pdpdu_cqe);
		ci = AMAP_GET_BITS(struct amap_i_t_dpdu_cqe_v2,
				      index, pdpdu_cqe);
		final = AMAP_GET_BITS(struct amap_i_t_dpdu_cqe_v2,
				      final, pdpdu_cqe);
	}

	/**
	 * DB addr Hi/Lo is same for BE and SKH.
	 * Subtract the dataplacementlength to get to the base.
	 */
	phys_addr.u.a32.address_lo = AMAP_GET_BITS(struct amap_i_t_dpdu_cqe,
						   db_addr_lo, pdpdu_cqe);
	phys_addr.u.a32.address_lo -= dpl;
	phys_addr.u.a32.address_hi = AMAP_GET_BITS(struct amap_i_t_dpdu_cqe,
						   db_addr_hi, pdpdu_cqe);

	code = AMAP_GET_BITS(struct amap_i_t_dpdu_cqe, code, pdpdu_cqe);
	switch (code) {
	case UNSOL_HDR_NOTIFY:
		pasync_handle = pasync_ctx->async_entry[ci].header;
		break;
	case UNSOL_DATA_DIGEST_ERROR_NOTIFY:
		error = 1;
	case UNSOL_DATA_NOTIFY:
		pasync_handle = pasync_ctx->async_entry[ci].data;
		break;
	/* called only for above codes */
	default:
		pasync_handle = NULL;
		break;
	}

	if (!pasync_handle) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_ISCSI,
			    "BM_%d : cid %d async PDU handle not found - code %d ci %d addr %llx\n",
			    cid, code, ci, phys_addr.u.a64.address);
		return pasync_handle;
	}

	if (pasync_handle->pa.u.a64.address != phys_addr.u.a64.address ||
	    pasync_handle->index != ci) {
		/* driver bug - if ci does not match async handle index */
		error = 1;
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_ISCSI,
			    "BM_%d : cid %u async PDU handle mismatch - addr in %cQE %llx at %u:addr in CQE %llx ci %u\n",
			    cid, pasync_handle->is_header ? 'H' : 'D',
			    pasync_handle->pa.u.a64.address,
			    pasync_handle->index,
			    phys_addr.u.a64.address, ci);
		/* FW has stale address - attempt continuing by dropping */
	}

	/**
	 * Each CID is associated with unique CRI.
	 * ASYNC_CRI_FROM_CID mapping and CRI_FROM_CID are totaly different.
	 **/
	pasync_handle->cri = BE_GET_ASYNC_CRI_FROM_CID(cid);
	pasync_handle->is_final = final;
	pasync_handle->buffer_len = dpl;
	/* empty the slot */
	if (pasync_handle->is_header)
		pasync_ctx->async_entry[ci].header = NULL;
	else
		pasync_ctx->async_entry[ci].data = NULL;

	/**
	 * DEF PDU header and data buffers with errors should be simply
	 * dropped as there are no consumers for it.
	 */
	if (error) {
		beiscsi_hdl_put_handle(pasync_ctx, pasync_handle);
		pasync_handle = NULL;
	}
	return pasync_handle;
}

static void
beiscsi_hdl_purge_handles(struct beiscsi_hba *phba,
			  struct hd_async_context *pasync_ctx,
			  u16 cri)
{
	struct hd_async_handle *pasync_handle, *tmp_handle;
	struct list_head *plist;

	plist  = &pasync_ctx->async_entry[cri].wq.list;
	list_for_each_entry_safe(pasync_handle, tmp_handle, plist, link) {
		list_del(&pasync_handle->link);
		beiscsi_hdl_put_handle(pasync_ctx, pasync_handle);
	}

	INIT_LIST_HEAD(&pasync_ctx->async_entry[cri].wq.list);
	pasync_ctx->async_entry[cri].wq.hdr_len = 0;
	pasync_ctx->async_entry[cri].wq.bytes_received = 0;
	pasync_ctx->async_entry[cri].wq.bytes_needed = 0;
}

static unsigned int
beiscsi_hdl_fwd_pdu(struct beiscsi_conn *beiscsi_conn,
		    struct hd_async_context *pasync_ctx,
		    u16 cri)
{
	struct iscsi_session *session = beiscsi_conn->conn->session;
	struct hd_async_handle *pasync_handle, *plast_handle;
	struct beiscsi_hba *phba = beiscsi_conn->phba;
	void *phdr = NULL, *pdata = NULL;
	u32 dlen = 0, status = 0;
	struct list_head *plist;

	plist = &pasync_ctx->async_entry[cri].wq.list;
	plast_handle = NULL;
	list_for_each_entry(pasync_handle, plist, link) {
		plast_handle = pasync_handle;
		/* get the header, the first entry */
		if (!phdr) {
			phdr = pasync_handle->pbuffer;
			continue;
		}
		/* use first buffer to collect all the data */
		if (!pdata) {
			pdata = pasync_handle->pbuffer;
			dlen = pasync_handle->buffer_len;
			continue;
		}
		memcpy(pdata + dlen, pasync_handle->pbuffer,
		       pasync_handle->buffer_len);
		dlen += pasync_handle->buffer_len;
	}

	if (!plast_handle->is_final) {
		/* last handle should have final PDU notification from FW */
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_ISCSI,
			    "BM_%d : cid %u %p fwd async PDU with last handle missing - HL%u:DN%u:DR%u\n",
			    beiscsi_conn->beiscsi_conn_cid, plast_handle,
			    pasync_ctx->async_entry[cri].wq.hdr_len,
			    pasync_ctx->async_entry[cri].wq.bytes_needed,
			    pasync_ctx->async_entry[cri].wq.bytes_received);
	}
	spin_lock_bh(&session->back_lock);
	status = beiscsi_complete_pdu(beiscsi_conn, phdr, pdata, dlen);
	spin_unlock_bh(&session->back_lock);
	beiscsi_hdl_purge_handles(phba, pasync_ctx, cri);
	return status;
}

static unsigned int
beiscsi_hdl_gather_pdu(struct beiscsi_conn *beiscsi_conn,
		       struct hd_async_context *pasync_ctx,
		       struct hd_async_handle *pasync_handle)
{
	unsigned int bytes_needed = 0, status = 0;
	u16 cri = pasync_handle->cri;
	struct cri_wait_queue *wq;
	struct beiscsi_hba *phba;
	struct pdu_base *ppdu;
	char *err = "";

	phba = beiscsi_conn->phba;
	wq = &pasync_ctx->async_entry[cri].wq;
	if (pasync_handle->is_header) {
		/* check if PDU hdr is rcv'd when old hdr not completed */
		if (wq->hdr_len) {
			err = "incomplete";
			goto drop_pdu;
		}
		ppdu = pasync_handle->pbuffer;
		bytes_needed = AMAP_GET_BITS(struct amap_pdu_base,
					     data_len_hi, ppdu);
		bytes_needed <<= 16;
		bytes_needed |= be16_to_cpu(AMAP_GET_BITS(struct amap_pdu_base,
							  data_len_lo, ppdu));
		wq->hdr_len = pasync_handle->buffer_len;
		wq->bytes_received = 0;
		wq->bytes_needed = bytes_needed;
		list_add_tail(&pasync_handle->link, &wq->list);
		if (!bytes_needed)
			status = beiscsi_hdl_fwd_pdu(beiscsi_conn,
						     pasync_ctx, cri);
	} else {
		/* check if data received has header and is needed */
		if (!wq->hdr_len || !wq->bytes_needed) {
			err = "header less";
			goto drop_pdu;
		}
		wq->bytes_received += pasync_handle->buffer_len;
		/* Something got overwritten? Better catch it here. */
		if (wq->bytes_received > wq->bytes_needed) {
			err = "overflow";
			goto drop_pdu;
		}
		list_add_tail(&pasync_handle->link, &wq->list);
		if (wq->bytes_received == wq->bytes_needed)
			status = beiscsi_hdl_fwd_pdu(beiscsi_conn,
						     pasync_ctx, cri);
	}
	return status;

drop_pdu:
	beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_ISCSI,
		    "BM_%d : cid %u async PDU %s - def-%c:HL%u:DN%u:DR%u\n",
		    beiscsi_conn->beiscsi_conn_cid, err,
		    pasync_handle->is_header ? 'H' : 'D',
		    wq->hdr_len, wq->bytes_needed,
		    pasync_handle->buffer_len);
	/* discard this handle */
	beiscsi_hdl_put_handle(pasync_ctx, pasync_handle);
	/* free all the other handles in cri_wait_queue */
	beiscsi_hdl_purge_handles(phba, pasync_ctx, cri);
	/* try continuing */
	return status;
}

static void
beiscsi_hdq_post_handles(struct beiscsi_hba *phba,
			 u8 header, u8 ulp_num)
{
	struct hd_async_handle *pasync_handle, *tmp, **slot;
	struct hd_async_context *pasync_ctx;
	struct hwi_controller *phwi_ctrlr;
	struct list_head *hfree_list;
	struct phys_addr *pasync_sge;
	u32 ring_id, doorbell = 0;
	u16 index, num_entries;
	u32 doorbell_offset;
	u16 prod = 0, cons;

	phwi_ctrlr = phba->phwi_ctrlr;
	pasync_ctx = HWI_GET_ASYNC_PDU_CTX(phwi_ctrlr, ulp_num);
	num_entries = pasync_ctx->num_entries;
	if (header) {
		cons = pasync_ctx->async_header.free_entries;
		hfree_list = &pasync_ctx->async_header.free_list;
		ring_id = phwi_ctrlr->default_pdu_hdr[ulp_num].id;
		doorbell_offset = phwi_ctrlr->default_pdu_hdr[ulp_num].
					doorbell_offset;
	} else {
		cons = pasync_ctx->async_data.free_entries;
		hfree_list = &pasync_ctx->async_data.free_list;
		ring_id = phwi_ctrlr->default_pdu_data[ulp_num].id;
		doorbell_offset = phwi_ctrlr->default_pdu_data[ulp_num].
					doorbell_offset;
	}
	/* number of entries posted must be in multiples of 8 */
	if (cons % 8)
		return;

	list_for_each_entry_safe(pasync_handle, tmp, hfree_list, link) {
		list_del_init(&pasync_handle->link);
		pasync_handle->is_final = 0;
		pasync_handle->buffer_len = 0;

		/* handles can be consumed out of order, use index in handle */
		index = pasync_handle->index;
		WARN_ON(pasync_handle->is_header != header);
		if (header)
			slot = &pasync_ctx->async_entry[index].header;
		else
			slot = &pasync_ctx->async_entry[index].data;
		/**
		 * The slot just tracks handle's hold and release, so
		 * overwriting at the same index won't do any harm but
		 * needs to be caught.
		 */
		if (*slot != NULL) {
			beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_ISCSI,
				    "BM_%d : async PDU %s slot at %u not empty\n",
				    header ? "header" : "data", index);
		}
		/**
		 * We use same freed index as in completion to post so this
		 * operation is not required for refills. Its required only
		 * for ring creation.
		 */
		if (header)
			pasync_sge = pasync_ctx->async_header.ring_base;
		else
			pasync_sge = pasync_ctx->async_data.ring_base;
		pasync_sge += index;
		/* if its a refill then address is same; hi is lo */
		WARN_ON(pasync_sge->hi &&
			pasync_sge->hi != pasync_handle->pa.u.a32.address_lo);
		WARN_ON(pasync_sge->lo &&
			pasync_sge->lo != pasync_handle->pa.u.a32.address_hi);
		pasync_sge->hi = pasync_handle->pa.u.a32.address_lo;
		pasync_sge->lo = pasync_handle->pa.u.a32.address_hi;

		*slot = pasync_handle;
		if (++prod == cons)
			break;
	}
	if (header)
		pasync_ctx->async_header.free_entries -= prod;
	else
		pasync_ctx->async_data.free_entries -= prod;

	doorbell |= ring_id & DB_DEF_PDU_RING_ID_MASK;
	doorbell |= 1 << DB_DEF_PDU_REARM_SHIFT;
	doorbell |= 0 << DB_DEF_PDU_EVENT_SHIFT;
	doorbell |= (prod & DB_DEF_PDU_CQPROC_MASK) << DB_DEF_PDU_CQPROC_SHIFT;
	iowrite32(doorbell, phba->db_va + doorbell_offset);
}

static void
beiscsi_hdq_process_compl(struct beiscsi_conn *beiscsi_conn,
			  struct i_t_dpdu_cqe *pdpdu_cqe)
{
	struct beiscsi_hba *phba = beiscsi_conn->phba;
	struct hd_async_handle *pasync_handle = NULL;
	struct hd_async_context *pasync_ctx;
	struct hwi_controller *phwi_ctrlr;
	u16 cid_cri;
	u8 ulp_num;

	phwi_ctrlr = phba->phwi_ctrlr;
	cid_cri = BE_GET_CRI_FROM_CID(beiscsi_conn->beiscsi_conn_cid);
	ulp_num = BEISCSI_GET_ULP_FROM_CRI(phwi_ctrlr, cid_cri);
	pasync_ctx = HWI_GET_ASYNC_PDU_CTX(phwi_ctrlr, ulp_num);
	pasync_handle = beiscsi_hdl_get_handle(beiscsi_conn, pasync_ctx,
					       pdpdu_cqe);
	if (!pasync_handle)
		return;

	beiscsi_hdl_gather_pdu(beiscsi_conn, pasync_ctx, pasync_handle);
	beiscsi_hdq_post_handles(phba, pasync_handle->is_header, ulp_num);
}

void beiscsi_process_mcc_cq(struct beiscsi_hba *phba)
{
	struct be_queue_info *mcc_cq;
	struct  be_mcc_compl *mcc_compl;
	unsigned int num_processed = 0;

	mcc_cq = &phba->ctrl.mcc_obj.cq;
	mcc_compl = queue_tail_node(mcc_cq);
	mcc_compl->flags = le32_to_cpu(mcc_compl->flags);
	while (mcc_compl->flags & CQE_FLAGS_VALID_MASK) {
		if (beiscsi_hba_in_error(phba))
			return;

		if (num_processed >= 32) {
			hwi_ring_cq_db(phba, mcc_cq->id,
					num_processed, 0);
			num_processed = 0;
		}
		if (mcc_compl->flags & CQE_FLAGS_ASYNC_MASK) {
			beiscsi_process_async_event(phba, mcc_compl);
		} else if (mcc_compl->flags & CQE_FLAGS_COMPLETED_MASK) {
			beiscsi_process_mcc_compl(&phba->ctrl, mcc_compl);
		}

		mcc_compl->flags = 0;
		queue_tail_inc(mcc_cq);
		mcc_compl = queue_tail_node(mcc_cq);
		mcc_compl->flags = le32_to_cpu(mcc_compl->flags);
		num_processed++;
	}

	if (num_processed > 0)
		hwi_ring_cq_db(phba, mcc_cq->id, num_processed, 1);
}

static void beiscsi_mcc_work(struct work_struct *work)
{
	struct be_eq_obj *pbe_eq;
	struct beiscsi_hba *phba;

	pbe_eq = container_of(work, struct be_eq_obj, mcc_work);
	phba = pbe_eq->phba;
	beiscsi_process_mcc_cq(phba);
	/* rearm EQ for further interrupts */
	if (!beiscsi_hba_in_error(phba))
		hwi_ring_eq_db(phba, pbe_eq->q.id, 0, 0, 1, 1);
}

/**
 * beiscsi_process_cq()- Process the Completion Queue
 * @pbe_eq: Event Q on which the Completion has come
 * @budget: Max number of events to processed
 *
 * return
 *     Number of Completion Entries processed.
 **/
unsigned int beiscsi_process_cq(struct be_eq_obj *pbe_eq, int budget)
{
	struct be_queue_info *cq;
	struct sol_cqe *sol;
	struct dmsg_cqe *dmsg;
	unsigned int total = 0;
	unsigned int num_processed = 0;
	unsigned short code = 0, cid = 0;
	uint16_t cri_index = 0;
	struct beiscsi_conn *beiscsi_conn;
	struct beiscsi_endpoint *beiscsi_ep;
	struct iscsi_endpoint *ep;
	struct beiscsi_hba *phba;

	cq = pbe_eq->cq;
	sol = queue_tail_node(cq);
	phba = pbe_eq->phba;

	while (sol->dw[offsetof(struct amap_sol_cqe, valid) / 32] &
	       CQE_VALID_MASK) {
		if (beiscsi_hba_in_error(phba))
			return 0;

		be_dws_le_to_cpu(sol, sizeof(struct sol_cqe));

		 code = (sol->dw[offsetof(struct amap_sol_cqe, code) /
			 32] & CQE_CODE_MASK);

		 /* Get the CID */
		if (is_chip_be2_be3r(phba)) {
			cid = AMAP_GET_BITS(struct amap_sol_cqe, cid, sol);
		} else {
			if ((code == DRIVERMSG_NOTIFY) ||
			    (code == UNSOL_HDR_NOTIFY) ||
			    (code == UNSOL_DATA_NOTIFY))
				cid = AMAP_GET_BITS(
						    struct amap_i_t_dpdu_cqe_v2,
						    cid, sol);
			 else
				 cid = AMAP_GET_BITS(struct amap_sol_cqe_v2,
						     cid, sol);
		}

		cri_index = BE_GET_CRI_FROM_CID(cid);
		ep = phba->ep_array[cri_index];

		if (ep == NULL) {
			/* connection has already been freed
			 * just move on to next one
			 */
			beiscsi_log(phba, KERN_WARNING,
				    BEISCSI_LOG_INIT,
				    "BM_%d : proc cqe of disconn ep: cid %d\n",
				    cid);
			goto proc_next_cqe;
		}

		beiscsi_ep = ep->dd_data;
		beiscsi_conn = beiscsi_ep->conn;

		/* replenish cq */
		if (num_processed == 32) {
			hwi_ring_cq_db(phba, cq->id, 32, 0);
			num_processed = 0;
		}
		total++;

		switch (code) {
		case SOL_CMD_COMPLETE:
			hwi_complete_cmd(beiscsi_conn, phba, sol);
			break;
		case DRIVERMSG_NOTIFY:
			beiscsi_log(phba, KERN_INFO,
				    BEISCSI_LOG_IO | BEISCSI_LOG_CONFIG,
				    "BM_%d : Received %s[%d] on CID : %d\n",
				    cqe_desc[code], code, cid);

			dmsg = (struct dmsg_cqe *)sol;
			hwi_complete_drvr_msgs(beiscsi_conn, phba, sol);
			break;
		case UNSOL_HDR_NOTIFY:
			beiscsi_log(phba, KERN_INFO,
				    BEISCSI_LOG_IO | BEISCSI_LOG_CONFIG,
				    "BM_%d : Received %s[%d] on CID : %d\n",
				    cqe_desc[code], code, cid);

			spin_lock_bh(&phba->async_pdu_lock);
			beiscsi_hdq_process_compl(beiscsi_conn,
						  (struct i_t_dpdu_cqe *)sol);
			spin_unlock_bh(&phba->async_pdu_lock);
			break;
		case UNSOL_DATA_NOTIFY:
			beiscsi_log(phba, KERN_INFO,
				    BEISCSI_LOG_CONFIG | BEISCSI_LOG_IO,
				    "BM_%d : Received %s[%d] on CID : %d\n",
				    cqe_desc[code], code, cid);

			spin_lock_bh(&phba->async_pdu_lock);
			beiscsi_hdq_process_compl(beiscsi_conn,
						  (struct i_t_dpdu_cqe *)sol);
			spin_unlock_bh(&phba->async_pdu_lock);
			break;
		case CXN_INVALIDATE_INDEX_NOTIFY:
		case CMD_INVALIDATED_NOTIFY:
		case CXN_INVALIDATE_NOTIFY:
			beiscsi_log(phba, KERN_ERR,
				    BEISCSI_LOG_IO | BEISCSI_LOG_CONFIG,
				    "BM_%d : Ignoring %s[%d] on CID : %d\n",
				    cqe_desc[code], code, cid);
			break;
		case CXN_KILLED_HDR_DIGEST_ERR:
		case SOL_CMD_KILLED_DATA_DIGEST_ERR:
			beiscsi_log(phba, KERN_ERR,
				    BEISCSI_LOG_CONFIG | BEISCSI_LOG_IO,
				    "BM_%d : Cmd Notification %s[%d] on CID : %d\n",
				    cqe_desc[code], code,  cid);
			break;
		case CMD_KILLED_INVALID_STATSN_RCVD:
		case CMD_KILLED_INVALID_R2T_RCVD:
		case CMD_CXN_KILLED_LUN_INVALID:
		case CMD_CXN_KILLED_ICD_INVALID:
		case CMD_CXN_KILLED_ITT_INVALID:
		case CMD_CXN_KILLED_SEQ_OUTOFORDER:
		case CMD_CXN_KILLED_INVALID_DATASN_RCVD:
			beiscsi_log(phba, KERN_ERR,
				    BEISCSI_LOG_CONFIG | BEISCSI_LOG_IO,
				    "BM_%d : Cmd Notification %s[%d] on CID : %d\n",
				    cqe_desc[code], code,  cid);
			break;
		case UNSOL_DATA_DIGEST_ERROR_NOTIFY:
			beiscsi_log(phba, KERN_ERR,
				    BEISCSI_LOG_IO | BEISCSI_LOG_CONFIG,
				    "BM_%d :  Dropping %s[%d] on DPDU ring on CID : %d\n",
				    cqe_desc[code], code, cid);
			spin_lock_bh(&phba->async_pdu_lock);
			/* driver consumes the entry and drops the contents */
			beiscsi_hdq_process_compl(beiscsi_conn,
						  (struct i_t_dpdu_cqe *)sol);
			spin_unlock_bh(&phba->async_pdu_lock);
			break;
		case CXN_KILLED_PDU_SIZE_EXCEEDS_DSL:
		case CXN_KILLED_BURST_LEN_MISMATCH:
		case CXN_KILLED_AHS_RCVD:
		case CXN_KILLED_UNKNOWN_HDR:
		case CXN_KILLED_STALE_ITT_TTT_RCVD:
		case CXN_KILLED_INVALID_ITT_TTT_RCVD:
		case CXN_KILLED_TIMED_OUT:
		case CXN_KILLED_FIN_RCVD:
		case CXN_KILLED_RST_SENT:
		case CXN_KILLED_RST_RCVD:
		case CXN_KILLED_BAD_UNSOL_PDU_RCVD:
		case CXN_KILLED_BAD_WRB_INDEX_ERROR:
		case CXN_KILLED_OVER_RUN_RESIDUAL:
		case CXN_KILLED_UNDER_RUN_RESIDUAL:
		case CXN_KILLED_CMND_DATA_NOT_ON_SAME_CONN:
			beiscsi_log(phba, KERN_ERR,
				    BEISCSI_LOG_IO | BEISCSI_LOG_CONFIG,
				    "BM_%d : Event %s[%d] received on CID : %d\n",
				    cqe_desc[code], code, cid);
			if (beiscsi_conn)
				iscsi_conn_failure(beiscsi_conn->conn,
						   ISCSI_ERR_CONN_FAILED);
			break;
		default:
			beiscsi_log(phba, KERN_ERR,
				    BEISCSI_LOG_IO | BEISCSI_LOG_CONFIG,
				    "BM_%d : Invalid CQE Event Received Code : %d"
				    "CID 0x%x...\n",
				    code, cid);
			break;
		}

proc_next_cqe:
		AMAP_SET_BITS(struct amap_sol_cqe, valid, sol, 0);
		queue_tail_inc(cq);
		sol = queue_tail_node(cq);
		num_processed++;
		if (total == budget)
			break;
	}

	hwi_ring_cq_db(phba, cq->id, num_processed, 1);
	return total;
}

static int be_iopoll(struct irq_poll *iop, int budget)
{
	unsigned int ret, io_events;
	struct beiscsi_hba *phba;
	struct be_eq_obj *pbe_eq;
	struct be_eq_entry *eqe = NULL;
	struct be_queue_info *eq;

	pbe_eq = container_of(iop, struct be_eq_obj, iopoll);
	phba = pbe_eq->phba;
	if (beiscsi_hba_in_error(phba)) {
		irq_poll_complete(iop);
		return 0;
	}

	io_events = 0;
	eq = &pbe_eq->q;
	eqe = queue_tail_node(eq);
	while (eqe->dw[offsetof(struct amap_eq_entry, valid) / 32] &
			EQE_VALID_MASK) {
		AMAP_SET_BITS(struct amap_eq_entry, valid, eqe, 0);
		queue_tail_inc(eq);
		eqe = queue_tail_node(eq);
		io_events++;
	}
	hwi_ring_eq_db(phba, eq->id, 1, io_events, 0, 1);

	ret = beiscsi_process_cq(pbe_eq, budget);
	pbe_eq->cq_count += ret;
	if (ret < budget) {
		irq_poll_complete(iop);
		beiscsi_log(phba, KERN_INFO,
			    BEISCSI_LOG_CONFIG | BEISCSI_LOG_IO,
			    "BM_%d : rearm pbe_eq->q.id =%d ret %d\n",
			    pbe_eq->q.id, ret);
		if (!beiscsi_hba_in_error(phba))
			hwi_ring_eq_db(phba, pbe_eq->q.id, 0, 0, 1, 1);
	}
	return ret;
}

static void
hwi_write_sgl_v2(struct iscsi_wrb *pwrb, struct scatterlist *sg,
		  unsigned int num_sg, struct beiscsi_io_task *io_task)
{
	struct iscsi_sge *psgl;
	unsigned int sg_len, index;
	unsigned int sge_len = 0;
	unsigned long long addr;
	struct scatterlist *l_sg;
	unsigned int offset;

	AMAP_SET_BITS(struct amap_iscsi_wrb_v2, iscsi_bhs_addr_lo, pwrb,
		      io_task->bhs_pa.u.a32.address_lo);
	AMAP_SET_BITS(struct amap_iscsi_wrb_v2, iscsi_bhs_addr_hi, pwrb,
		      io_task->bhs_pa.u.a32.address_hi);

	l_sg = sg;
	for (index = 0; (index < num_sg) && (index < 2); index++,
			sg = sg_next(sg)) {
		if (index == 0) {
			sg_len = sg_dma_len(sg);
			addr = (u64) sg_dma_address(sg);
			AMAP_SET_BITS(struct amap_iscsi_wrb_v2,
				      sge0_addr_lo, pwrb,
				      lower_32_bits(addr));
			AMAP_SET_BITS(struct amap_iscsi_wrb_v2,
				      sge0_addr_hi, pwrb,
				      upper_32_bits(addr));
			AMAP_SET_BITS(struct amap_iscsi_wrb_v2,
				      sge0_len, pwrb,
				      sg_len);
			sge_len = sg_len;
		} else {
			AMAP_SET_BITS(struct amap_iscsi_wrb_v2, sge1_r2t_offset,
				      pwrb, sge_len);
			sg_len = sg_dma_len(sg);
			addr = (u64) sg_dma_address(sg);
			AMAP_SET_BITS(struct amap_iscsi_wrb_v2,
				      sge1_addr_lo, pwrb,
				      lower_32_bits(addr));
			AMAP_SET_BITS(struct amap_iscsi_wrb_v2,
				      sge1_addr_hi, pwrb,
				      upper_32_bits(addr));
			AMAP_SET_BITS(struct amap_iscsi_wrb_v2,
				      sge1_len, pwrb,
				      sg_len);
		}
	}
	psgl = (struct iscsi_sge *)io_task->psgl_handle->pfrag;
	memset(psgl, 0, sizeof(*psgl) * BE2_SGE);

	AMAP_SET_BITS(struct amap_iscsi_sge, len, psgl, io_task->bhs_len - 2);

	AMAP_SET_BITS(struct amap_iscsi_sge, addr_hi, psgl,
		      io_task->bhs_pa.u.a32.address_hi);
	AMAP_SET_BITS(struct amap_iscsi_sge, addr_lo, psgl,
		      io_task->bhs_pa.u.a32.address_lo);

	if (num_sg == 1) {
		AMAP_SET_BITS(struct amap_iscsi_wrb_v2, sge0_last, pwrb,
			      1);
		AMAP_SET_BITS(struct amap_iscsi_wrb_v2, sge1_last, pwrb,
			      0);
	} else if (num_sg == 2) {
		AMAP_SET_BITS(struct amap_iscsi_wrb_v2, sge0_last, pwrb,
			      0);
		AMAP_SET_BITS(struct amap_iscsi_wrb_v2, sge1_last, pwrb,
			      1);
	} else {
		AMAP_SET_BITS(struct amap_iscsi_wrb_v2, sge0_last, pwrb,
			      0);
		AMAP_SET_BITS(struct amap_iscsi_wrb_v2, sge1_last, pwrb,
			      0);
	}

	sg = l_sg;
	psgl++;
	psgl++;
	offset = 0;
	for (index = 0; index < num_sg; index++, sg = sg_next(sg), psgl++) {
		sg_len = sg_dma_len(sg);
		addr = (u64) sg_dma_address(sg);
		AMAP_SET_BITS(struct amap_iscsi_sge, addr_lo, psgl,
			      lower_32_bits(addr));
		AMAP_SET_BITS(struct amap_iscsi_sge, addr_hi, psgl,
			      upper_32_bits(addr));
		AMAP_SET_BITS(struct amap_iscsi_sge, len, psgl, sg_len);
		AMAP_SET_BITS(struct amap_iscsi_sge, sge_offset, psgl, offset);
		AMAP_SET_BITS(struct amap_iscsi_sge, last_sge, psgl, 0);
		offset += sg_len;
	}
	psgl--;
	AMAP_SET_BITS(struct amap_iscsi_sge, last_sge, psgl, 1);
}

static void
hwi_write_sgl(struct iscsi_wrb *pwrb, struct scatterlist *sg,
	      unsigned int num_sg, struct beiscsi_io_task *io_task)
{
	struct iscsi_sge *psgl;
	unsigned int sg_len, index;
	unsigned int sge_len = 0;
	unsigned long long addr;
	struct scatterlist *l_sg;
	unsigned int offset;

	AMAP_SET_BITS(struct amap_iscsi_wrb, iscsi_bhs_addr_lo, pwrb,
				      io_task->bhs_pa.u.a32.address_lo);
	AMAP_SET_BITS(struct amap_iscsi_wrb, iscsi_bhs_addr_hi, pwrb,
				      io_task->bhs_pa.u.a32.address_hi);

	l_sg = sg;
	for (index = 0; (index < num_sg) && (index < 2); index++,
							 sg = sg_next(sg)) {
		if (index == 0) {
			sg_len = sg_dma_len(sg);
			addr = (u64) sg_dma_address(sg);
			AMAP_SET_BITS(struct amap_iscsi_wrb, sge0_addr_lo, pwrb,
						((u32)(addr & 0xFFFFFFFF)));
			AMAP_SET_BITS(struct amap_iscsi_wrb, sge0_addr_hi, pwrb,
							((u32)(addr >> 32)));
			AMAP_SET_BITS(struct amap_iscsi_wrb, sge0_len, pwrb,
							sg_len);
			sge_len = sg_len;
		} else {
			AMAP_SET_BITS(struct amap_iscsi_wrb, sge1_r2t_offset,
							pwrb, sge_len);
			sg_len = sg_dma_len(sg);
			addr = (u64) sg_dma_address(sg);
			AMAP_SET_BITS(struct amap_iscsi_wrb, sge1_addr_lo, pwrb,
						((u32)(addr & 0xFFFFFFFF)));
			AMAP_SET_BITS(struct amap_iscsi_wrb, sge1_addr_hi, pwrb,
							((u32)(addr >> 32)));
			AMAP_SET_BITS(struct amap_iscsi_wrb, sge1_len, pwrb,
							sg_len);
		}
	}
	psgl = (struct iscsi_sge *)io_task->psgl_handle->pfrag;
	memset(psgl, 0, sizeof(*psgl) * BE2_SGE);

	AMAP_SET_BITS(struct amap_iscsi_sge, len, psgl, io_task->bhs_len - 2);

	AMAP_SET_BITS(struct amap_iscsi_sge, addr_hi, psgl,
			io_task->bhs_pa.u.a32.address_hi);
	AMAP_SET_BITS(struct amap_iscsi_sge, addr_lo, psgl,
			io_task->bhs_pa.u.a32.address_lo);

	if (num_sg == 1) {
		AMAP_SET_BITS(struct amap_iscsi_wrb, sge0_last, pwrb,
								1);
		AMAP_SET_BITS(struct amap_iscsi_wrb, sge1_last, pwrb,
								0);
	} else if (num_sg == 2) {
		AMAP_SET_BITS(struct amap_iscsi_wrb, sge0_last, pwrb,
								0);
		AMAP_SET_BITS(struct amap_iscsi_wrb, sge1_last, pwrb,
								1);
	} else {
		AMAP_SET_BITS(struct amap_iscsi_wrb, sge0_last, pwrb,
								0);
		AMAP_SET_BITS(struct amap_iscsi_wrb, sge1_last, pwrb,
								0);
	}
	sg = l_sg;
	psgl++;
	psgl++;
	offset = 0;
	for (index = 0; index < num_sg; index++, sg = sg_next(sg), psgl++) {
		sg_len = sg_dma_len(sg);
		addr = (u64) sg_dma_address(sg);
		AMAP_SET_BITS(struct amap_iscsi_sge, addr_lo, psgl,
						(addr & 0xFFFFFFFF));
		AMAP_SET_BITS(struct amap_iscsi_sge, addr_hi, psgl,
						(addr >> 32));
		AMAP_SET_BITS(struct amap_iscsi_sge, len, psgl, sg_len);
		AMAP_SET_BITS(struct amap_iscsi_sge, sge_offset, psgl, offset);
		AMAP_SET_BITS(struct amap_iscsi_sge, last_sge, psgl, 0);
		offset += sg_len;
	}
	psgl--;
	AMAP_SET_BITS(struct amap_iscsi_sge, last_sge, psgl, 1);
}

/**
 * hwi_write_buffer()- Populate the WRB with task info
 * @pwrb: ptr to the WRB entry
 * @task: iscsi task which is to be executed
 **/
static int hwi_write_buffer(struct iscsi_wrb *pwrb, struct iscsi_task *task)
{
	struct iscsi_sge *psgl;
	struct beiscsi_io_task *io_task = task->dd_data;
	struct beiscsi_conn *beiscsi_conn = io_task->conn;
	struct beiscsi_hba *phba = beiscsi_conn->phba;
	uint8_t dsp_value = 0;

	io_task->bhs_len = sizeof(struct be_nonio_bhs) - 2;
	AMAP_SET_BITS(struct amap_iscsi_wrb, iscsi_bhs_addr_lo, pwrb,
				io_task->bhs_pa.u.a32.address_lo);
	AMAP_SET_BITS(struct amap_iscsi_wrb, iscsi_bhs_addr_hi, pwrb,
				io_task->bhs_pa.u.a32.address_hi);

	if (task->data) {

		/* Check for the data_count */
		dsp_value = (task->data_count) ? 1 : 0;

		if (is_chip_be2_be3r(phba))
			AMAP_SET_BITS(struct amap_iscsi_wrb, dsp,
				      pwrb, dsp_value);
		else
			AMAP_SET_BITS(struct amap_iscsi_wrb_v2, dsp,
				      pwrb, dsp_value);

		/* Map addr only if there is data_count */
		if (dsp_value) {
			io_task->mtask_addr = pci_map_single(phba->pcidev,
							     task->data,
							     task->data_count,
							     PCI_DMA_TODEVICE);
			if (pci_dma_mapping_error(phba->pcidev,
						  io_task->mtask_addr))
				return -ENOMEM;
			io_task->mtask_data_count = task->data_count;
		} else
			io_task->mtask_addr = 0;

		AMAP_SET_BITS(struct amap_iscsi_wrb, sge0_addr_lo, pwrb,
			      lower_32_bits(io_task->mtask_addr));
		AMAP_SET_BITS(struct amap_iscsi_wrb, sge0_addr_hi, pwrb,
			      upper_32_bits(io_task->mtask_addr));
		AMAP_SET_BITS(struct amap_iscsi_wrb, sge0_len, pwrb,
						task->data_count);

		AMAP_SET_BITS(struct amap_iscsi_wrb, sge0_last, pwrb, 1);
	} else {
		AMAP_SET_BITS(struct amap_iscsi_wrb, dsp, pwrb, 0);
		io_task->mtask_addr = 0;
	}

	psgl = (struct iscsi_sge *)io_task->psgl_handle->pfrag;

	AMAP_SET_BITS(struct amap_iscsi_sge, len, psgl, io_task->bhs_len);

	AMAP_SET_BITS(struct amap_iscsi_sge, addr_hi, psgl,
		      io_task->bhs_pa.u.a32.address_hi);
	AMAP_SET_BITS(struct amap_iscsi_sge, addr_lo, psgl,
		      io_task->bhs_pa.u.a32.address_lo);
	if (task->data) {
		psgl++;
		AMAP_SET_BITS(struct amap_iscsi_sge, addr_hi, psgl, 0);
		AMAP_SET_BITS(struct amap_iscsi_sge, addr_lo, psgl, 0);
		AMAP_SET_BITS(struct amap_iscsi_sge, len, psgl, 0);
		AMAP_SET_BITS(struct amap_iscsi_sge, sge_offset, psgl, 0);
		AMAP_SET_BITS(struct amap_iscsi_sge, rsvd0, psgl, 0);
		AMAP_SET_BITS(struct amap_iscsi_sge, last_sge, psgl, 0);

		psgl++;
		if (task->data) {
			AMAP_SET_BITS(struct amap_iscsi_sge, addr_lo, psgl,
				      lower_32_bits(io_task->mtask_addr));
			AMAP_SET_BITS(struct amap_iscsi_sge, addr_hi, psgl,
				      upper_32_bits(io_task->mtask_addr));
		}
		AMAP_SET_BITS(struct amap_iscsi_sge, len, psgl, 0x106);
	}
	AMAP_SET_BITS(struct amap_iscsi_sge, last_sge, psgl, 1);
	return 0;
}

/**
 * beiscsi_find_mem_req()- Find mem needed
 * @phba: ptr to HBA struct
 **/
static void beiscsi_find_mem_req(struct beiscsi_hba *phba)
{
	uint8_t mem_descr_index, ulp_num;
	unsigned int num_cq_pages, num_async_pdu_buf_pages;
	unsigned int num_async_pdu_data_pages, wrb_sz_per_cxn;
	unsigned int num_async_pdu_buf_sgl_pages, num_async_pdu_data_sgl_pages;

	num_cq_pages = PAGES_REQUIRED(phba->params.num_cq_entries * \
				      sizeof(struct sol_cqe));

	phba->params.hwi_ws_sz = sizeof(struct hwi_controller);

	phba->mem_req[ISCSI_MEM_GLOBAL_HEADER] = 2 *
						 BE_ISCSI_PDU_HEADER_SIZE;
	phba->mem_req[HWI_MEM_ADDN_CONTEXT] =
					    sizeof(struct hwi_context_memory);


	phba->mem_req[HWI_MEM_WRB] = sizeof(struct iscsi_wrb)
	    * (phba->params.wrbs_per_cxn)
	    * phba->params.cxns_per_ctrl;
	wrb_sz_per_cxn =  sizeof(struct wrb_handle) *
				 (phba->params.wrbs_per_cxn);
	phba->mem_req[HWI_MEM_WRBH] = roundup_pow_of_two((wrb_sz_per_cxn) *
				phba->params.cxns_per_ctrl);

	phba->mem_req[HWI_MEM_SGLH] = sizeof(struct sgl_handle) *
		phba->params.icds_per_ctrl;
	phba->mem_req[HWI_MEM_SGE] = sizeof(struct iscsi_sge) *
		phba->params.num_sge_per_io * phba->params.icds_per_ctrl;
	for (ulp_num = 0; ulp_num < BEISCSI_ULP_COUNT; ulp_num++) {
		if (test_bit(ulp_num, &phba->fw_config.ulp_supported)) {

			num_async_pdu_buf_sgl_pages =
				PAGES_REQUIRED(BEISCSI_GET_CID_COUNT(
					       phba, ulp_num) *
					       sizeof(struct phys_addr));

			num_async_pdu_buf_pages =
				PAGES_REQUIRED(BEISCSI_GET_CID_COUNT(
					       phba, ulp_num) *
					       phba->params.defpdu_hdr_sz);

			num_async_pdu_data_pages =
				PAGES_REQUIRED(BEISCSI_GET_CID_COUNT(
					       phba, ulp_num) *
					       phba->params.defpdu_data_sz);

			num_async_pdu_data_sgl_pages =
				PAGES_REQUIRED(BEISCSI_GET_CID_COUNT(
					       phba, ulp_num) *
					       sizeof(struct phys_addr));

			mem_descr_index = (HWI_MEM_TEMPLATE_HDR_ULP0 +
					  (ulp_num * MEM_DESCR_OFFSET));
			phba->mem_req[mem_descr_index] =
					BEISCSI_GET_CID_COUNT(phba, ulp_num) *
					BEISCSI_TEMPLATE_HDR_PER_CXN_SIZE;

			mem_descr_index = (HWI_MEM_ASYNC_HEADER_BUF_ULP0 +
					  (ulp_num * MEM_DESCR_OFFSET));
			phba->mem_req[mem_descr_index] =
					  num_async_pdu_buf_pages *
					  PAGE_SIZE;

			mem_descr_index = (HWI_MEM_ASYNC_DATA_BUF_ULP0 +
					  (ulp_num * MEM_DESCR_OFFSET));
			phba->mem_req[mem_descr_index] =
					  num_async_pdu_data_pages *
					  PAGE_SIZE;

			mem_descr_index = (HWI_MEM_ASYNC_HEADER_RING_ULP0 +
					  (ulp_num * MEM_DESCR_OFFSET));
			phba->mem_req[mem_descr_index] =
					  num_async_pdu_buf_sgl_pages *
					  PAGE_SIZE;

			mem_descr_index = (HWI_MEM_ASYNC_DATA_RING_ULP0 +
					  (ulp_num * MEM_DESCR_OFFSET));
			phba->mem_req[mem_descr_index] =
					  num_async_pdu_data_sgl_pages *
					  PAGE_SIZE;

			mem_descr_index = (HWI_MEM_ASYNC_HEADER_HANDLE_ULP0 +
					  (ulp_num * MEM_DESCR_OFFSET));
			phba->mem_req[mem_descr_index] =
					  BEISCSI_GET_CID_COUNT(phba, ulp_num) *
					  sizeof(struct hd_async_handle);

			mem_descr_index = (HWI_MEM_ASYNC_DATA_HANDLE_ULP0 +
					  (ulp_num * MEM_DESCR_OFFSET));
			phba->mem_req[mem_descr_index] =
					  BEISCSI_GET_CID_COUNT(phba, ulp_num) *
					  sizeof(struct hd_async_handle);

			mem_descr_index = (HWI_MEM_ASYNC_PDU_CONTEXT_ULP0 +
					  (ulp_num * MEM_DESCR_OFFSET));
			phba->mem_req[mem_descr_index] =
					  sizeof(struct hd_async_context) +
					 (BEISCSI_GET_CID_COUNT(phba, ulp_num) *
					  sizeof(struct hd_async_entry));
		}
	}
}

static int beiscsi_alloc_mem(struct beiscsi_hba *phba)
{
	dma_addr_t bus_add;
	struct hwi_controller *phwi_ctrlr;
	struct be_mem_descriptor *mem_descr;
	struct mem_array *mem_arr, *mem_arr_orig;
	unsigned int i, j, alloc_size, curr_alloc_size;

	phba->phwi_ctrlr = kzalloc(phba->params.hwi_ws_sz, GFP_KERNEL);
	if (!phba->phwi_ctrlr)
		return -ENOMEM;

	/* Allocate memory for wrb_context */
	phwi_ctrlr = phba->phwi_ctrlr;
	phwi_ctrlr->wrb_context = kzalloc(sizeof(struct hwi_wrb_context) *
					  phba->params.cxns_per_ctrl,
					  GFP_KERNEL);
	if (!phwi_ctrlr->wrb_context) {
		kfree(phba->phwi_ctrlr);
		return -ENOMEM;
	}

	phba->init_mem = kcalloc(SE_MEM_MAX, sizeof(*mem_descr),
				 GFP_KERNEL);
	if (!phba->init_mem) {
		kfree(phwi_ctrlr->wrb_context);
		kfree(phba->phwi_ctrlr);
		return -ENOMEM;
	}

	mem_arr_orig = kmalloc(sizeof(*mem_arr_orig) * BEISCSI_MAX_FRAGS_INIT,
			       GFP_KERNEL);
	if (!mem_arr_orig) {
		kfree(phba->init_mem);
		kfree(phwi_ctrlr->wrb_context);
		kfree(phba->phwi_ctrlr);
		return -ENOMEM;
	}

	mem_descr = phba->init_mem;
	for (i = 0; i < SE_MEM_MAX; i++) {
		if (!phba->mem_req[i]) {
			mem_descr->mem_array = NULL;
			mem_descr++;
			continue;
		}

		j = 0;
		mem_arr = mem_arr_orig;
		alloc_size = phba->mem_req[i];
		memset(mem_arr, 0, sizeof(struct mem_array) *
		       BEISCSI_MAX_FRAGS_INIT);
		curr_alloc_size = min(be_max_phys_size * 1024, alloc_size);
		do {
			mem_arr->virtual_address = pci_alloc_consistent(
							phba->pcidev,
							curr_alloc_size,
							&bus_add);
			if (!mem_arr->virtual_address) {
				if (curr_alloc_size <= BE_MIN_MEM_SIZE)
					goto free_mem;
				if (curr_alloc_size -
					rounddown_pow_of_two(curr_alloc_size))
					curr_alloc_size = rounddown_pow_of_two
							     (curr_alloc_size);
				else
					curr_alloc_size = curr_alloc_size / 2;
			} else {
				mem_arr->bus_address.u.
				    a64.address = (__u64) bus_add;
				mem_arr->size = curr_alloc_size;
				alloc_size -= curr_alloc_size;
				curr_alloc_size = min(be_max_phys_size *
						      1024, alloc_size);
				j++;
				mem_arr++;
			}
		} while (alloc_size);
		mem_descr->num_elements = j;
		mem_descr->size_in_bytes = phba->mem_req[i];
		mem_descr->mem_array = kmalloc(sizeof(*mem_arr) * j,
					       GFP_KERNEL);
		if (!mem_descr->mem_array)
			goto free_mem;

		memcpy(mem_descr->mem_array, mem_arr_orig,
		       sizeof(struct mem_array) * j);
		mem_descr++;
	}
	kfree(mem_arr_orig);
	return 0;
free_mem:
	mem_descr->num_elements = j;
	while ((i) || (j)) {
		for (j = mem_descr->num_elements; j > 0; j--) {
			pci_free_consistent(phba->pcidev,
					    mem_descr->mem_array[j - 1].size,
					    mem_descr->mem_array[j - 1].
					    virtual_address,
					    (unsigned long)mem_descr->
					    mem_array[j - 1].
					    bus_address.u.a64.address);
		}
		if (i) {
			i--;
			kfree(mem_descr->mem_array);
			mem_descr--;
		}
	}
	kfree(mem_arr_orig);
	kfree(phba->init_mem);
	kfree(phba->phwi_ctrlr->wrb_context);
	kfree(phba->phwi_ctrlr);
	return -ENOMEM;
}

static int beiscsi_get_memory(struct beiscsi_hba *phba)
{
	beiscsi_find_mem_req(phba);
	return beiscsi_alloc_mem(phba);
}

static void iscsi_init_global_templates(struct beiscsi_hba *phba)
{
	struct pdu_data_out *pdata_out;
	struct pdu_nop_out *pnop_out;
	struct be_mem_descriptor *mem_descr;

	mem_descr = phba->init_mem;
	mem_descr += ISCSI_MEM_GLOBAL_HEADER;
	pdata_out =
	    (struct pdu_data_out *)mem_descr->mem_array[0].virtual_address;
	memset(pdata_out, 0, BE_ISCSI_PDU_HEADER_SIZE);

	AMAP_SET_BITS(struct amap_pdu_data_out, opcode, pdata_out,
		      IIOC_SCSI_DATA);

	pnop_out =
	    (struct pdu_nop_out *)((unsigned char *)mem_descr->mem_array[0].
				   virtual_address + BE_ISCSI_PDU_HEADER_SIZE);

	memset(pnop_out, 0, BE_ISCSI_PDU_HEADER_SIZE);
	AMAP_SET_BITS(struct amap_pdu_nop_out, ttt, pnop_out, 0xFFFFFFFF);
	AMAP_SET_BITS(struct amap_pdu_nop_out, f_bit, pnop_out, 1);
	AMAP_SET_BITS(struct amap_pdu_nop_out, i_bit, pnop_out, 0);
}

static int beiscsi_init_wrb_handle(struct beiscsi_hba *phba)
{
	struct be_mem_descriptor *mem_descr_wrbh, *mem_descr_wrb;
	struct hwi_context_memory *phwi_ctxt;
	struct wrb_handle *pwrb_handle = NULL;
	struct hwi_controller *phwi_ctrlr;
	struct hwi_wrb_context *pwrb_context;
	struct iscsi_wrb *pwrb = NULL;
	unsigned int num_cxn_wrbh = 0;
	unsigned int num_cxn_wrb = 0, j, idx = 0, index;

	mem_descr_wrbh = phba->init_mem;
	mem_descr_wrbh += HWI_MEM_WRBH;

	mem_descr_wrb = phba->init_mem;
	mem_descr_wrb += HWI_MEM_WRB;
	phwi_ctrlr = phba->phwi_ctrlr;

	/* Allocate memory for WRBQ */
	phwi_ctxt = phwi_ctrlr->phwi_ctxt;
	phwi_ctxt->be_wrbq = kzalloc(sizeof(struct be_queue_info) *
				     phba->params.cxns_per_ctrl,
				     GFP_KERNEL);
	if (!phwi_ctxt->be_wrbq) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : WRBQ Mem Alloc Failed\n");
		return -ENOMEM;
	}

	for (index = 0; index < phba->params.cxns_per_ctrl; index++) {
		pwrb_context = &phwi_ctrlr->wrb_context[index];
		pwrb_context->pwrb_handle_base =
				kzalloc(sizeof(struct wrb_handle *) *
					phba->params.wrbs_per_cxn, GFP_KERNEL);
		if (!pwrb_context->pwrb_handle_base) {
			beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
				    "BM_%d : Mem Alloc Failed. Failing to load\n");
			goto init_wrb_hndl_failed;
		}
		pwrb_context->pwrb_handle_basestd =
				kzalloc(sizeof(struct wrb_handle *) *
					phba->params.wrbs_per_cxn, GFP_KERNEL);
		if (!pwrb_context->pwrb_handle_basestd) {
			beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
				    "BM_%d : Mem Alloc Failed. Failing to load\n");
			goto init_wrb_hndl_failed;
		}
		if (!num_cxn_wrbh) {
			pwrb_handle =
				mem_descr_wrbh->mem_array[idx].virtual_address;
			num_cxn_wrbh = ((mem_descr_wrbh->mem_array[idx].size) /
					((sizeof(struct wrb_handle)) *
					 phba->params.wrbs_per_cxn));
			idx++;
		}
		pwrb_context->alloc_index = 0;
		pwrb_context->wrb_handles_available = 0;
		pwrb_context->free_index = 0;

		if (num_cxn_wrbh) {
			for (j = 0; j < phba->params.wrbs_per_cxn; j++) {
				pwrb_context->pwrb_handle_base[j] = pwrb_handle;
				pwrb_context->pwrb_handle_basestd[j] =
								pwrb_handle;
				pwrb_context->wrb_handles_available++;
				pwrb_handle->wrb_index = j;
				pwrb_handle++;
			}
			num_cxn_wrbh--;
		}
		spin_lock_init(&pwrb_context->wrb_lock);
	}
	idx = 0;
	for (index = 0; index < phba->params.cxns_per_ctrl; index++) {
		pwrb_context = &phwi_ctrlr->wrb_context[index];
		if (!num_cxn_wrb) {
			pwrb = mem_descr_wrb->mem_array[idx].virtual_address;
			num_cxn_wrb = (mem_descr_wrb->mem_array[idx].size) /
				((sizeof(struct iscsi_wrb) *
				  phba->params.wrbs_per_cxn));
			idx++;
		}

		if (num_cxn_wrb) {
			for (j = 0; j < phba->params.wrbs_per_cxn; j++) {
				pwrb_handle = pwrb_context->pwrb_handle_base[j];
				pwrb_handle->pwrb = pwrb;
				pwrb++;
			}
			num_cxn_wrb--;
		}
	}
	return 0;
init_wrb_hndl_failed:
	for (j = index; j > 0; j--) {
		pwrb_context = &phwi_ctrlr->wrb_context[j];
		kfree(pwrb_context->pwrb_handle_base);
		kfree(pwrb_context->pwrb_handle_basestd);
	}
	return -ENOMEM;
}

static int hwi_init_async_pdu_ctx(struct beiscsi_hba *phba)
{
	uint8_t ulp_num;
	struct hwi_controller *phwi_ctrlr;
	struct hba_parameters *p = &phba->params;
	struct hd_async_context *pasync_ctx;
	struct hd_async_handle *pasync_header_h, *pasync_data_h;
	unsigned int index, idx, num_per_mem, num_async_data;
	struct be_mem_descriptor *mem_descr;

	for (ulp_num = 0; ulp_num < BEISCSI_ULP_COUNT; ulp_num++) {
		if (test_bit(ulp_num, &phba->fw_config.ulp_supported)) {
			 /* get async_ctx for each ULP */
			mem_descr = (struct be_mem_descriptor *)phba->init_mem;
			mem_descr += (HWI_MEM_ASYNC_PDU_CONTEXT_ULP0 +
				     (ulp_num * MEM_DESCR_OFFSET));

			phwi_ctrlr = phba->phwi_ctrlr;
			phwi_ctrlr->phwi_ctxt->pasync_ctx[ulp_num] =
				(struct hd_async_context *)
				 mem_descr->mem_array[0].virtual_address;

			pasync_ctx = phwi_ctrlr->phwi_ctxt->pasync_ctx[ulp_num];
			memset(pasync_ctx, 0, sizeof(*pasync_ctx));

			pasync_ctx->async_entry =
					(struct hd_async_entry *)
					((long unsigned int)pasync_ctx +
					sizeof(struct hd_async_context));

			pasync_ctx->num_entries = BEISCSI_GET_CID_COUNT(phba,
						  ulp_num);
			/* setup header buffers */
			mem_descr = (struct be_mem_descriptor *)phba->init_mem;
			mem_descr += HWI_MEM_ASYNC_HEADER_BUF_ULP0 +
				(ulp_num * MEM_DESCR_OFFSET);
			if (mem_descr->mem_array[0].virtual_address) {
				beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
					    "BM_%d : hwi_init_async_pdu_ctx"
					    " HWI_MEM_ASYNC_HEADER_BUF_ULP%d va=%p\n",
					    ulp_num,
					    mem_descr->mem_array[0].
					    virtual_address);
			} else
				beiscsi_log(phba, KERN_WARNING,
					    BEISCSI_LOG_INIT,
					    "BM_%d : No Virtual address for ULP : %d\n",
					    ulp_num);

			pasync_ctx->async_header.buffer_size = p->defpdu_hdr_sz;
			pasync_ctx->async_header.va_base =
				mem_descr->mem_array[0].virtual_address;

			pasync_ctx->async_header.pa_base.u.a64.address =
				mem_descr->mem_array[0].
				bus_address.u.a64.address;

			/* setup header buffer sgls */
			mem_descr = (struct be_mem_descriptor *)phba->init_mem;
			mem_descr += HWI_MEM_ASYNC_HEADER_RING_ULP0 +
				     (ulp_num * MEM_DESCR_OFFSET);
			if (mem_descr->mem_array[0].virtual_address) {
				beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
					    "BM_%d : hwi_init_async_pdu_ctx"
					    " HWI_MEM_ASYNC_HEADER_RING_ULP%d va=%p\n",
					    ulp_num,
					    mem_descr->mem_array[0].
					    virtual_address);
			} else
				beiscsi_log(phba, KERN_WARNING,
					    BEISCSI_LOG_INIT,
					    "BM_%d : No Virtual address for ULP : %d\n",
					    ulp_num);

			pasync_ctx->async_header.ring_base =
				mem_descr->mem_array[0].virtual_address;

			/* setup header buffer handles */
			mem_descr = (struct be_mem_descriptor *)phba->init_mem;
			mem_descr += HWI_MEM_ASYNC_HEADER_HANDLE_ULP0 +
				     (ulp_num * MEM_DESCR_OFFSET);
			if (mem_descr->mem_array[0].virtual_address) {
				beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
					    "BM_%d : hwi_init_async_pdu_ctx"
					    " HWI_MEM_ASYNC_HEADER_HANDLE_ULP%d va=%p\n",
					    ulp_num,
					    mem_descr->mem_array[0].
					    virtual_address);
			} else
				beiscsi_log(phba, KERN_WARNING,
					    BEISCSI_LOG_INIT,
					    "BM_%d : No Virtual address for ULP : %d\n",
					    ulp_num);

			pasync_ctx->async_header.handle_base =
				mem_descr->mem_array[0].virtual_address;
			INIT_LIST_HEAD(&pasync_ctx->async_header.free_list);

			/* setup data buffer sgls */
			mem_descr = (struct be_mem_descriptor *)phba->init_mem;
			mem_descr += HWI_MEM_ASYNC_DATA_RING_ULP0 +
				     (ulp_num * MEM_DESCR_OFFSET);
			if (mem_descr->mem_array[0].virtual_address) {
				beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
					    "BM_%d : hwi_init_async_pdu_ctx"
					    " HWI_MEM_ASYNC_DATA_RING_ULP%d va=%p\n",
					    ulp_num,
					    mem_descr->mem_array[0].
					    virtual_address);
			} else
				beiscsi_log(phba, KERN_WARNING,
					    BEISCSI_LOG_INIT,
					    "BM_%d : No Virtual address for ULP : %d\n",
					    ulp_num);

			pasync_ctx->async_data.ring_base =
				mem_descr->mem_array[0].virtual_address;

			/* setup data buffer handles */
			mem_descr = (struct be_mem_descriptor *)phba->init_mem;
			mem_descr += HWI_MEM_ASYNC_DATA_HANDLE_ULP0 +
				     (ulp_num * MEM_DESCR_OFFSET);
			if (!mem_descr->mem_array[0].virtual_address)
				beiscsi_log(phba, KERN_WARNING,
					    BEISCSI_LOG_INIT,
					    "BM_%d : No Virtual address for ULP : %d\n",
					    ulp_num);

			pasync_ctx->async_data.handle_base =
				mem_descr->mem_array[0].virtual_address;
			INIT_LIST_HEAD(&pasync_ctx->async_data.free_list);

			pasync_header_h =
				(struct hd_async_handle *)
				pasync_ctx->async_header.handle_base;
			pasync_data_h =
				(struct hd_async_handle *)
				pasync_ctx->async_data.handle_base;

			/* setup data buffers */
			mem_descr = (struct be_mem_descriptor *)phba->init_mem;
			mem_descr += HWI_MEM_ASYNC_DATA_BUF_ULP0 +
				     (ulp_num * MEM_DESCR_OFFSET);
			if (mem_descr->mem_array[0].virtual_address) {
				beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
					    "BM_%d : hwi_init_async_pdu_ctx"
					    " HWI_MEM_ASYNC_DATA_BUF_ULP%d va=%p\n",
					    ulp_num,
					    mem_descr->mem_array[0].
					    virtual_address);
			} else
				beiscsi_log(phba, KERN_WARNING,
					    BEISCSI_LOG_INIT,
					    "BM_%d : No Virtual address for ULP : %d\n",
					    ulp_num);

			idx = 0;
			pasync_ctx->async_data.buffer_size = p->defpdu_data_sz;
			pasync_ctx->async_data.va_base =
				mem_descr->mem_array[idx].virtual_address;
			pasync_ctx->async_data.pa_base.u.a64.address =
				mem_descr->mem_array[idx].
				bus_address.u.a64.address;

			num_async_data = ((mem_descr->mem_array[idx].size) /
					phba->params.defpdu_data_sz);
			num_per_mem = 0;

			for (index = 0;	index < BEISCSI_GET_CID_COUNT
					(phba, ulp_num); index++) {
				pasync_header_h->cri = -1;
				pasync_header_h->is_header = 1;
				pasync_header_h->index = index;
				INIT_LIST_HEAD(&pasync_header_h->link);
				pasync_header_h->pbuffer =
					(void *)((unsigned long)
						 (pasync_ctx->
						  async_header.va_base) +
						 (p->defpdu_hdr_sz * index));

				pasync_header_h->pa.u.a64.address =
					pasync_ctx->async_header.pa_base.u.a64.
					address + (p->defpdu_hdr_sz * index);

				list_add_tail(&pasync_header_h->link,
					      &pasync_ctx->async_header.
					      free_list);
				pasync_header_h++;
				pasync_ctx->async_header.free_entries++;
				INIT_LIST_HEAD(&pasync_ctx->async_entry[index].
						wq.list);
				pasync_ctx->async_entry[index].header = NULL;

				pasync_data_h->cri = -1;
				pasync_data_h->is_header = 0;
				pasync_data_h->index = index;
				INIT_LIST_HEAD(&pasync_data_h->link);

				if (!num_async_data) {
					num_per_mem = 0;
					idx++;
					pasync_ctx->async_data.va_base =
						mem_descr->mem_array[idx].
						virtual_address;
					pasync_ctx->async_data.pa_base.u.
						a64.address =
						mem_descr->mem_array[idx].
						bus_address.u.a64.address;
					num_async_data =
						((mem_descr->mem_array[idx].
						  size) /
						 phba->params.defpdu_data_sz);
				}
				pasync_data_h->pbuffer =
					(void *)((unsigned long)
					(pasync_ctx->async_data.va_base) +
					(p->defpdu_data_sz * num_per_mem));

				pasync_data_h->pa.u.a64.address =
					pasync_ctx->async_data.pa_base.u.a64.
					address + (p->defpdu_data_sz *
					num_per_mem);
				num_per_mem++;
				num_async_data--;

				list_add_tail(&pasync_data_h->link,
					      &pasync_ctx->async_data.
					      free_list);
				pasync_data_h++;
				pasync_ctx->async_data.free_entries++;
				pasync_ctx->async_entry[index].data = NULL;
			}
		}
	}

	return 0;
}

static int
be_sgl_create_contiguous(void *virtual_address,
			 u64 physical_address, u32 length,
			 struct be_dma_mem *sgl)
{
	WARN_ON(!virtual_address);
	WARN_ON(!physical_address);
	WARN_ON(!length);
	WARN_ON(!sgl);

	sgl->va = virtual_address;
	sgl->dma = (unsigned long)physical_address;
	sgl->size = length;

	return 0;
}

static void be_sgl_destroy_contiguous(struct be_dma_mem *sgl)
{
	memset(sgl, 0, sizeof(*sgl));
}

static void
hwi_build_be_sgl_arr(struct beiscsi_hba *phba,
		     struct mem_array *pmem, struct be_dma_mem *sgl)
{
	if (sgl->va)
		be_sgl_destroy_contiguous(sgl);

	be_sgl_create_contiguous(pmem->virtual_address,
				 pmem->bus_address.u.a64.address,
				 pmem->size, sgl);
}

static void
hwi_build_be_sgl_by_offset(struct beiscsi_hba *phba,
			   struct mem_array *pmem, struct be_dma_mem *sgl)
{
	if (sgl->va)
		be_sgl_destroy_contiguous(sgl);

	be_sgl_create_contiguous((unsigned char *)pmem->virtual_address,
				 pmem->bus_address.u.a64.address,
				 pmem->size, sgl);
}

static int be_fill_queue(struct be_queue_info *q,
		u16 len, u16 entry_size, void *vaddress)
{
	struct be_dma_mem *mem = &q->dma_mem;

	memset(q, 0, sizeof(*q));
	q->len = len;
	q->entry_size = entry_size;
	mem->size = len * entry_size;
	mem->va = vaddress;
	if (!mem->va)
		return -ENOMEM;
	memset(mem->va, 0, mem->size);
	return 0;
}

static int beiscsi_create_eqs(struct beiscsi_hba *phba,
			     struct hwi_context_memory *phwi_context)
{
	unsigned int i, num_eq_pages;
	int ret = 0, eq_for_mcc;
	struct be_queue_info *eq;
	struct be_dma_mem *mem;
	void *eq_vaddress;
	dma_addr_t paddr;

	num_eq_pages = PAGES_REQUIRED(phba->params.num_eq_entries * \
				      sizeof(struct be_eq_entry));

	if (phba->msix_enabled)
		eq_for_mcc = 1;
	else
		eq_for_mcc = 0;
	for (i = 0; i < (phba->num_cpus + eq_for_mcc); i++) {
		eq = &phwi_context->be_eq[i].q;
		mem = &eq->dma_mem;
		phwi_context->be_eq[i].phba = phba;
		eq_vaddress = pci_alloc_consistent(phba->pcidev,
						     num_eq_pages * PAGE_SIZE,
						     &paddr);
		if (!eq_vaddress)
			goto create_eq_error;

		mem->va = eq_vaddress;
		ret = be_fill_queue(eq, phba->params.num_eq_entries,
				    sizeof(struct be_eq_entry), eq_vaddress);
		if (ret) {
			beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
				    "BM_%d : be_fill_queue Failed for EQ\n");
			goto create_eq_error;
		}

		mem->dma = paddr;
		ret = beiscsi_cmd_eq_create(&phba->ctrl, eq,
					    phwi_context->cur_eqd);
		if (ret) {
			beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
				    "BM_%d : beiscsi_cmd_eq_create"
				    "Failed for EQ\n");
			goto create_eq_error;
		}

		beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
			    "BM_%d : eqid = %d\n",
			    phwi_context->be_eq[i].q.id);
	}
	return 0;
create_eq_error:
	for (i = 0; i < (phba->num_cpus + eq_for_mcc); i++) {
		eq = &phwi_context->be_eq[i].q;
		mem = &eq->dma_mem;
		if (mem->va)
			pci_free_consistent(phba->pcidev, num_eq_pages
					    * PAGE_SIZE,
					    mem->va, mem->dma);
	}
	return ret;
}

static int beiscsi_create_cqs(struct beiscsi_hba *phba,
			     struct hwi_context_memory *phwi_context)
{
	unsigned int i, num_cq_pages;
	int ret = 0;
	struct be_queue_info *cq, *eq;
	struct be_dma_mem *mem;
	struct be_eq_obj *pbe_eq;
	void *cq_vaddress;
	dma_addr_t paddr;

	num_cq_pages = PAGES_REQUIRED(phba->params.num_cq_entries * \
				      sizeof(struct sol_cqe));

	for (i = 0; i < phba->num_cpus; i++) {
		cq = &phwi_context->be_cq[i];
		eq = &phwi_context->be_eq[i].q;
		pbe_eq = &phwi_context->be_eq[i];
		pbe_eq->cq = cq;
		pbe_eq->phba = phba;
		mem = &cq->dma_mem;
		cq_vaddress = pci_alloc_consistent(phba->pcidev,
						     num_cq_pages * PAGE_SIZE,
						     &paddr);
		if (!cq_vaddress)
			goto create_cq_error;
		ret = be_fill_queue(cq, phba->params.num_cq_entries,
				    sizeof(struct sol_cqe), cq_vaddress);
		if (ret) {
			beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
				    "BM_%d : be_fill_queue Failed "
				    "for ISCSI CQ\n");
			goto create_cq_error;
		}

		mem->dma = paddr;
		ret = beiscsi_cmd_cq_create(&phba->ctrl, cq, eq, false,
					    false, 0);
		if (ret) {
			beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
				    "BM_%d : beiscsi_cmd_eq_create"
				    "Failed for ISCSI CQ\n");
			goto create_cq_error;
		}
		beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
			    "BM_%d : iscsi cq_id is %d for eq_id %d\n"
			    "iSCSI CQ CREATED\n", cq->id, eq->id);
	}
	return 0;

create_cq_error:
	for (i = 0; i < phba->num_cpus; i++) {
		cq = &phwi_context->be_cq[i];
		mem = &cq->dma_mem;
		if (mem->va)
			pci_free_consistent(phba->pcidev, num_cq_pages
					    * PAGE_SIZE,
					    mem->va, mem->dma);
	}
	return ret;

}

static int
beiscsi_create_def_hdr(struct beiscsi_hba *phba,
		       struct hwi_context_memory *phwi_context,
		       struct hwi_controller *phwi_ctrlr,
		       unsigned int def_pdu_ring_sz, uint8_t ulp_num)
{
	unsigned int idx;
	int ret;
	struct be_queue_info *dq, *cq;
	struct be_dma_mem *mem;
	struct be_mem_descriptor *mem_descr;
	void *dq_vaddress;

	idx = 0;
	dq = &phwi_context->be_def_hdrq[ulp_num];
	cq = &phwi_context->be_cq[0];
	mem = &dq->dma_mem;
	mem_descr = phba->init_mem;
	mem_descr += HWI_MEM_ASYNC_HEADER_RING_ULP0 +
		    (ulp_num * MEM_DESCR_OFFSET);
	dq_vaddress = mem_descr->mem_array[idx].virtual_address;
	ret = be_fill_queue(dq, mem_descr->mem_array[0].size /
			    sizeof(struct phys_addr),
			    sizeof(struct phys_addr), dq_vaddress);
	if (ret) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : be_fill_queue Failed for DEF PDU HDR on ULP : %d\n",
			    ulp_num);

		return ret;
	}
	mem->dma = (unsigned long)mem_descr->mem_array[idx].
				  bus_address.u.a64.address;
	ret = be_cmd_create_default_pdu_queue(&phba->ctrl, cq, dq,
					      def_pdu_ring_sz,
					      phba->params.defpdu_hdr_sz,
					      BEISCSI_DEFQ_HDR, ulp_num);
	if (ret) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : be_cmd_create_default_pdu_queue Failed DEFHDR on ULP : %d\n",
			    ulp_num);

		return ret;
	}

	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
		    "BM_%d : iscsi hdr def pdu id for ULP : %d is %d\n",
		    ulp_num,
		    phwi_context->be_def_hdrq[ulp_num].id);
	return 0;
}

static int
beiscsi_create_def_data(struct beiscsi_hba *phba,
			struct hwi_context_memory *phwi_context,
			struct hwi_controller *phwi_ctrlr,
			unsigned int def_pdu_ring_sz, uint8_t ulp_num)
{
	unsigned int idx;
	int ret;
	struct be_queue_info *dataq, *cq;
	struct be_dma_mem *mem;
	struct be_mem_descriptor *mem_descr;
	void *dq_vaddress;

	idx = 0;
	dataq = &phwi_context->be_def_dataq[ulp_num];
	cq = &phwi_context->be_cq[0];
	mem = &dataq->dma_mem;
	mem_descr = phba->init_mem;
	mem_descr += HWI_MEM_ASYNC_DATA_RING_ULP0 +
		    (ulp_num * MEM_DESCR_OFFSET);
	dq_vaddress = mem_descr->mem_array[idx].virtual_address;
	ret = be_fill_queue(dataq, mem_descr->mem_array[0].size /
			    sizeof(struct phys_addr),
			    sizeof(struct phys_addr), dq_vaddress);
	if (ret) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : be_fill_queue Failed for DEF PDU "
			    "DATA on ULP : %d\n",
			    ulp_num);

		return ret;
	}
	mem->dma = (unsigned long)mem_descr->mem_array[idx].
				  bus_address.u.a64.address;
	ret = be_cmd_create_default_pdu_queue(&phba->ctrl, cq, dataq,
					      def_pdu_ring_sz,
					      phba->params.defpdu_data_sz,
					      BEISCSI_DEFQ_DATA, ulp_num);
	if (ret) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d be_cmd_create_default_pdu_queue"
			    " Failed for DEF PDU DATA on ULP : %d\n",
			    ulp_num);
		return ret;
	}

	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
		    "BM_%d : iscsi def data id on ULP : %d is  %d\n",
		    ulp_num,
		    phwi_context->be_def_dataq[ulp_num].id);

	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
		    "BM_%d : DEFAULT PDU DATA RING CREATED"
		    "on ULP : %d\n", ulp_num);
	return 0;
}


static int
beiscsi_post_template_hdr(struct beiscsi_hba *phba)
{
	struct be_mem_descriptor *mem_descr;
	struct mem_array *pm_arr;
	struct be_dma_mem sgl;
	int status, ulp_num;

	for (ulp_num = 0; ulp_num < BEISCSI_ULP_COUNT; ulp_num++) {
		if (test_bit(ulp_num, &phba->fw_config.ulp_supported)) {
			mem_descr = (struct be_mem_descriptor *)phba->init_mem;
			mem_descr += HWI_MEM_TEMPLATE_HDR_ULP0 +
				    (ulp_num * MEM_DESCR_OFFSET);
			pm_arr = mem_descr->mem_array;

			hwi_build_be_sgl_arr(phba, pm_arr, &sgl);
			status = be_cmd_iscsi_post_template_hdr(
				 &phba->ctrl, &sgl);

			if (status != 0) {
				beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
					    "BM_%d : Post Template HDR Failed for"
					    "ULP_%d\n", ulp_num);
				return status;
			}

			beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
				    "BM_%d : Template HDR Pages Posted for"
				    "ULP_%d\n", ulp_num);
		}
	}
	return 0;
}

static int
beiscsi_post_pages(struct beiscsi_hba *phba)
{
	struct be_mem_descriptor *mem_descr;
	struct mem_array *pm_arr;
	unsigned int page_offset, i;
	struct be_dma_mem sgl;
	int status, ulp_num = 0;

	mem_descr = phba->init_mem;
	mem_descr += HWI_MEM_SGE;
	pm_arr = mem_descr->mem_array;

	for (ulp_num = 0; ulp_num < BEISCSI_ULP_COUNT; ulp_num++)
		if (test_bit(ulp_num, &phba->fw_config.ulp_supported))
			break;

	page_offset = (sizeof(struct iscsi_sge) * phba->params.num_sge_per_io *
			phba->fw_config.iscsi_icd_start[ulp_num]) / PAGE_SIZE;
	for (i = 0; i < mem_descr->num_elements; i++) {
		hwi_build_be_sgl_arr(phba, pm_arr, &sgl);
		status = be_cmd_iscsi_post_sgl_pages(&phba->ctrl, &sgl,
						page_offset,
						(pm_arr->size / PAGE_SIZE));
		page_offset += pm_arr->size / PAGE_SIZE;
		if (status != 0) {
			beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
				    "BM_%d : post sgl failed.\n");
			return status;
		}
		pm_arr++;
	}
	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
		    "BM_%d : POSTED PAGES\n");
	return 0;
}

static void be_queue_free(struct beiscsi_hba *phba, struct be_queue_info *q)
{
	struct be_dma_mem *mem = &q->dma_mem;
	if (mem->va) {
		pci_free_consistent(phba->pcidev, mem->size,
			mem->va, mem->dma);
		mem->va = NULL;
	}
}

static int be_queue_alloc(struct beiscsi_hba *phba, struct be_queue_info *q,
		u16 len, u16 entry_size)
{
	struct be_dma_mem *mem = &q->dma_mem;

	memset(q, 0, sizeof(*q));
	q->len = len;
	q->entry_size = entry_size;
	mem->size = len * entry_size;
	mem->va = pci_zalloc_consistent(phba->pcidev, mem->size, &mem->dma);
	if (!mem->va)
		return -ENOMEM;
	return 0;
}

static int
beiscsi_create_wrb_rings(struct beiscsi_hba *phba,
			 struct hwi_context_memory *phwi_context,
			 struct hwi_controller *phwi_ctrlr)
{
	unsigned int wrb_mem_index, offset, size, num_wrb_rings;
	u64 pa_addr_lo;
	unsigned int idx, num, i, ulp_num;
	struct mem_array *pwrb_arr;
	void *wrb_vaddr;
	struct be_dma_mem sgl;
	struct be_mem_descriptor *mem_descr;
	struct hwi_wrb_context *pwrb_context;
	int status;
	uint8_t ulp_count = 0, ulp_base_num = 0;
	uint16_t cid_count_ulp[BEISCSI_ULP_COUNT] = { 0 };

	idx = 0;
	mem_descr = phba->init_mem;
	mem_descr += HWI_MEM_WRB;
	pwrb_arr = kmalloc(sizeof(*pwrb_arr) * phba->params.cxns_per_ctrl,
			   GFP_KERNEL);
	if (!pwrb_arr) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : Memory alloc failed in create wrb ring.\n");
		return -ENOMEM;
	}
	wrb_vaddr = mem_descr->mem_array[idx].virtual_address;
	pa_addr_lo = mem_descr->mem_array[idx].bus_address.u.a64.address;
	num_wrb_rings = mem_descr->mem_array[idx].size /
		(phba->params.wrbs_per_cxn * sizeof(struct iscsi_wrb));

	for (num = 0; num < phba->params.cxns_per_ctrl; num++) {
		if (num_wrb_rings) {
			pwrb_arr[num].virtual_address = wrb_vaddr;
			pwrb_arr[num].bus_address.u.a64.address	= pa_addr_lo;
			pwrb_arr[num].size = phba->params.wrbs_per_cxn *
					    sizeof(struct iscsi_wrb);
			wrb_vaddr += pwrb_arr[num].size;
			pa_addr_lo += pwrb_arr[num].size;
			num_wrb_rings--;
		} else {
			idx++;
			wrb_vaddr = mem_descr->mem_array[idx].virtual_address;
			pa_addr_lo = mem_descr->mem_array[idx].\
					bus_address.u.a64.address;
			num_wrb_rings = mem_descr->mem_array[idx].size /
					(phba->params.wrbs_per_cxn *
					sizeof(struct iscsi_wrb));
			pwrb_arr[num].virtual_address = wrb_vaddr;
			pwrb_arr[num].bus_address.u.a64.address\
						= pa_addr_lo;
			pwrb_arr[num].size = phba->params.wrbs_per_cxn *
						 sizeof(struct iscsi_wrb);
			wrb_vaddr += pwrb_arr[num].size;
			pa_addr_lo   += pwrb_arr[num].size;
			num_wrb_rings--;
		}
	}

	/* Get the ULP Count */
	for (ulp_num = 0; ulp_num < BEISCSI_ULP_COUNT; ulp_num++)
		if (test_bit(ulp_num, &phba->fw_config.ulp_supported)) {
			ulp_count++;
			ulp_base_num = ulp_num;
			cid_count_ulp[ulp_num] =
				BEISCSI_GET_CID_COUNT(phba, ulp_num);
		}

	for (i = 0; i < phba->params.cxns_per_ctrl; i++) {
		wrb_mem_index = 0;
		offset = 0;
		size = 0;

		if (ulp_count > 1) {
			ulp_base_num = (ulp_base_num + 1) % BEISCSI_ULP_COUNT;

			if (!cid_count_ulp[ulp_base_num])
				ulp_base_num = (ulp_base_num + 1) %
						BEISCSI_ULP_COUNT;

			cid_count_ulp[ulp_base_num]--;
		}


		hwi_build_be_sgl_by_offset(phba, &pwrb_arr[i], &sgl);
		status = be_cmd_wrbq_create(&phba->ctrl, &sgl,
					    &phwi_context->be_wrbq[i],
					    &phwi_ctrlr->wrb_context[i],
					    ulp_base_num);
		if (status != 0) {
			beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
				    "BM_%d : wrbq create failed.");
			kfree(pwrb_arr);
			return status;
		}
		pwrb_context = &phwi_ctrlr->wrb_context[i];
		BE_SET_CID_TO_CRI(i, pwrb_context->cid);
	}
	kfree(pwrb_arr);
	return 0;
}

static void free_wrb_handles(struct beiscsi_hba *phba)
{
	unsigned int index;
	struct hwi_controller *phwi_ctrlr;
	struct hwi_wrb_context *pwrb_context;

	phwi_ctrlr = phba->phwi_ctrlr;
	for (index = 0; index < phba->params.cxns_per_ctrl; index++) {
		pwrb_context = &phwi_ctrlr->wrb_context[index];
		kfree(pwrb_context->pwrb_handle_base);
		kfree(pwrb_context->pwrb_handle_basestd);
	}
}

static void be_mcc_queues_destroy(struct beiscsi_hba *phba)
{
	struct be_ctrl_info *ctrl = &phba->ctrl;
	struct be_dma_mem *ptag_mem;
	struct be_queue_info *q;
	int i, tag;

	q = &phba->ctrl.mcc_obj.q;
	for (i = 0; i < MAX_MCC_CMD; i++) {
		tag = i + 1;
		if (!test_bit(MCC_TAG_STATE_RUNNING,
			      &ctrl->ptag_state[tag].tag_state))
			continue;

		if (test_bit(MCC_TAG_STATE_TIMEOUT,
			     &ctrl->ptag_state[tag].tag_state)) {
			ptag_mem = &ctrl->ptag_state[tag].tag_mem_state;
			if (ptag_mem->size) {
				pci_free_consistent(ctrl->pdev,
						    ptag_mem->size,
						    ptag_mem->va,
						    ptag_mem->dma);
				ptag_mem->size = 0;
			}
			continue;
		}
		/**
		 * If MCC is still active and waiting then wake up the process.
		 * We are here only because port is going offline. The process
		 * sees that (BEISCSI_HBA_ONLINE is cleared) and EIO error is
		 * returned for the operation and allocated memory cleaned up.
		 */
		if (waitqueue_active(&ctrl->mcc_wait[tag])) {
			ctrl->mcc_tag_status[tag] = MCC_STATUS_FAILED;
			ctrl->mcc_tag_status[tag] |= CQE_VALID_MASK;
			wake_up_interruptible(&ctrl->mcc_wait[tag]);
			/*
			 * Control tag info gets reinitialized in enable
			 * so wait for the process to clear running state.
			 */
			while (test_bit(MCC_TAG_STATE_RUNNING,
					&ctrl->ptag_state[tag].tag_state))
				schedule_timeout_uninterruptible(HZ);
		}
		/**
		 * For MCC with tag_states MCC_TAG_STATE_ASYNC and
		 * MCC_TAG_STATE_IGNORE nothing needs to done.
		 */
	}
	if (q->created) {
		beiscsi_cmd_q_destroy(ctrl, q, QTYPE_MCCQ);
		be_queue_free(phba, q);
	}

	q = &phba->ctrl.mcc_obj.cq;
	if (q->created) {
		beiscsi_cmd_q_destroy(ctrl, q, QTYPE_CQ);
		be_queue_free(phba, q);
	}
}

static int be_mcc_queues_create(struct beiscsi_hba *phba,
				struct hwi_context_memory *phwi_context)
{
	struct be_queue_info *q, *cq;
	struct be_ctrl_info *ctrl = &phba->ctrl;

	/* Alloc MCC compl queue */
	cq = &phba->ctrl.mcc_obj.cq;
	if (be_queue_alloc(phba, cq, MCC_CQ_LEN,
			sizeof(struct be_mcc_compl)))
		goto err;
	/* Ask BE to create MCC compl queue; */
	if (phba->msix_enabled) {
		if (beiscsi_cmd_cq_create(ctrl, cq, &phwi_context->be_eq
					 [phba->num_cpus].q, false, true, 0))
		goto mcc_cq_free;
	} else {
		if (beiscsi_cmd_cq_create(ctrl, cq, &phwi_context->be_eq[0].q,
					  false, true, 0))
		goto mcc_cq_free;
	}

	/* Alloc MCC queue */
	q = &phba->ctrl.mcc_obj.q;
	if (be_queue_alloc(phba, q, MCC_Q_LEN, sizeof(struct be_mcc_wrb)))
		goto mcc_cq_destroy;

	/* Ask BE to create MCC queue */
	if (beiscsi_cmd_mccq_create(phba, q, cq))
		goto mcc_q_free;

	return 0;

mcc_q_free:
	be_queue_free(phba, q);
mcc_cq_destroy:
	beiscsi_cmd_q_destroy(ctrl, cq, QTYPE_CQ);
mcc_cq_free:
	be_queue_free(phba, cq);
err:
	return -ENOMEM;
}

/**
 * find_num_cpus()- Get the CPU online count
 * @phba: ptr to priv structure
 *
 * CPU count is used for creating EQ.
 **/
static void find_num_cpus(struct beiscsi_hba *phba)
{
	int  num_cpus = 0;

	num_cpus = num_online_cpus();

	switch (phba->generation) {
	case BE_GEN2:
	case BE_GEN3:
		phba->num_cpus = (num_cpus > BEISCSI_MAX_NUM_CPUS) ?
				  BEISCSI_MAX_NUM_CPUS : num_cpus;
		break;
	case BE_GEN4:
		/*
		 * If eqid_count == 1 fall back to
		 * INTX mechanism
		 **/
		if (phba->fw_config.eqid_count == 1) {
			enable_msix = 0;
			phba->num_cpus = 1;
			return;
		}

		phba->num_cpus =
			(num_cpus > (phba->fw_config.eqid_count - 1)) ?
			(phba->fw_config.eqid_count - 1) : num_cpus;
		break;
	default:
		phba->num_cpus = 1;
	}
}

static void hwi_purge_eq(struct beiscsi_hba *phba)
{
	struct hwi_controller *phwi_ctrlr;
	struct hwi_context_memory *phwi_context;
	struct be_queue_info *eq;
	struct be_eq_entry *eqe = NULL;
	int i, eq_msix;
	unsigned int num_processed;

	if (beiscsi_hba_in_error(phba))
		return;

	phwi_ctrlr = phba->phwi_ctrlr;
	phwi_context = phwi_ctrlr->phwi_ctxt;
	if (phba->msix_enabled)
		eq_msix = 1;
	else
		eq_msix = 0;

	for (i = 0; i < (phba->num_cpus + eq_msix); i++) {
		eq = &phwi_context->be_eq[i].q;
		eqe = queue_tail_node(eq);
		num_processed = 0;
		while (eqe->dw[offsetof(struct amap_eq_entry, valid) / 32]
					& EQE_VALID_MASK) {
			AMAP_SET_BITS(struct amap_eq_entry, valid, eqe, 0);
			queue_tail_inc(eq);
			eqe = queue_tail_node(eq);
			num_processed++;
		}

		if (num_processed)
			hwi_ring_eq_db(phba, eq->id, 1,	num_processed, 1, 1);
	}
}

static void hwi_cleanup_port(struct beiscsi_hba *phba)
{
	struct be_queue_info *q;
	struct be_ctrl_info *ctrl = &phba->ctrl;
	struct hwi_controller *phwi_ctrlr;
	struct hwi_context_memory *phwi_context;
	struct hd_async_context *pasync_ctx;
	int i, eq_for_mcc, ulp_num;

	for (ulp_num = 0; ulp_num < BEISCSI_ULP_COUNT; ulp_num++)
		if (test_bit(ulp_num, &phba->fw_config.ulp_supported))
			beiscsi_cmd_iscsi_cleanup(phba, ulp_num);

	/**
	 * Purge all EQ entries that may have been left out. This is to
	 * workaround a problem we've seen occasionally where driver gets an
	 * interrupt with EQ entry bit set after stopping the controller.
	 */
	hwi_purge_eq(phba);

	phwi_ctrlr = phba->phwi_ctrlr;
	phwi_context = phwi_ctrlr->phwi_ctxt;

	be_cmd_iscsi_remove_template_hdr(ctrl);

	for (i = 0; i < phba->params.cxns_per_ctrl; i++) {
		q = &phwi_context->be_wrbq[i];
		if (q->created)
			beiscsi_cmd_q_destroy(ctrl, q, QTYPE_WRBQ);
	}
	kfree(phwi_context->be_wrbq);
	free_wrb_handles(phba);

	for (ulp_num = 0; ulp_num < BEISCSI_ULP_COUNT; ulp_num++) {
		if (test_bit(ulp_num, &phba->fw_config.ulp_supported)) {

			q = &phwi_context->be_def_hdrq[ulp_num];
			if (q->created)
				beiscsi_cmd_q_destroy(ctrl, q, QTYPE_DPDUQ);

			q = &phwi_context->be_def_dataq[ulp_num];
			if (q->created)
				beiscsi_cmd_q_destroy(ctrl, q, QTYPE_DPDUQ);

			pasync_ctx = phwi_ctrlr->phwi_ctxt->pasync_ctx[ulp_num];
		}
	}

	beiscsi_cmd_q_destroy(ctrl, NULL, QTYPE_SGL);

	for (i = 0; i < (phba->num_cpus); i++) {
		q = &phwi_context->be_cq[i];
		if (q->created) {
			be_queue_free(phba, q);
			beiscsi_cmd_q_destroy(ctrl, q, QTYPE_CQ);
		}
	}

	be_mcc_queues_destroy(phba);
	if (phba->msix_enabled)
		eq_for_mcc = 1;
	else
		eq_for_mcc = 0;
	for (i = 0; i < (phba->num_cpus + eq_for_mcc); i++) {
		q = &phwi_context->be_eq[i].q;
		if (q->created) {
			be_queue_free(phba, q);
			beiscsi_cmd_q_destroy(ctrl, q, QTYPE_EQ);
		}
	}
	/* this ensures complete FW cleanup */
	beiscsi_cmd_function_reset(phba);
	/* last communication, indicate driver is unloading */
	beiscsi_cmd_special_wrb(&phba->ctrl, 0);
}

static int hwi_init_port(struct beiscsi_hba *phba)
{
	struct hwi_controller *phwi_ctrlr;
	struct hwi_context_memory *phwi_context;
	unsigned int def_pdu_ring_sz;
	struct be_ctrl_info *ctrl = &phba->ctrl;
	int status, ulp_num;

	phwi_ctrlr = phba->phwi_ctrlr;
	phwi_context = phwi_ctrlr->phwi_ctxt;
	phwi_context->max_eqd = 128;
	phwi_context->min_eqd = 0;
	phwi_context->cur_eqd = 0;
	/* set port optic state to unknown */
	phba->optic_state = 0xff;

	status = beiscsi_create_eqs(phba, phwi_context);
	if (status != 0) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : EQ not created\n");
		goto error;
	}

	status = be_mcc_queues_create(phba, phwi_context);
	if (status != 0)
		goto error;

	status = beiscsi_check_supported_fw(ctrl, phba);
	if (status != 0) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : Unsupported fw version\n");
		goto error;
	}

	status = beiscsi_create_cqs(phba, phwi_context);
	if (status != 0) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : CQ not created\n");
		goto error;
	}

	for (ulp_num = 0; ulp_num < BEISCSI_ULP_COUNT; ulp_num++) {
		if (test_bit(ulp_num, &phba->fw_config.ulp_supported)) {
			def_pdu_ring_sz =
				BEISCSI_GET_CID_COUNT(phba, ulp_num) *
				sizeof(struct phys_addr);

			status = beiscsi_create_def_hdr(phba, phwi_context,
							phwi_ctrlr,
							def_pdu_ring_sz,
							ulp_num);
			if (status != 0) {
				beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
					    "BM_%d : Default Header not created for ULP : %d\n",
					    ulp_num);
				goto error;
			}

			status = beiscsi_create_def_data(phba, phwi_context,
							 phwi_ctrlr,
							 def_pdu_ring_sz,
							 ulp_num);
			if (status != 0) {
				beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
					    "BM_%d : Default Data not created for ULP : %d\n",
					    ulp_num);
				goto error;
			}
			/**
			 * Now that the default PDU rings have been created,
			 * let EP know about it.
			 * Call beiscsi_cmd_iscsi_cleanup before posting?
			 */
			beiscsi_hdq_post_handles(phba, BEISCSI_DEFQ_HDR,
						 ulp_num);
			beiscsi_hdq_post_handles(phba, BEISCSI_DEFQ_DATA,
						 ulp_num);
		}
	}

	status = beiscsi_post_pages(phba);
	if (status != 0) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : Post SGL Pages Failed\n");
		goto error;
	}

	status = beiscsi_post_template_hdr(phba);
	if (status != 0) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : Template HDR Posting for CXN Failed\n");
	}

	status = beiscsi_create_wrb_rings(phba,	phwi_context, phwi_ctrlr);
	if (status != 0) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : WRB Rings not created\n");
		goto error;
	}

	for (ulp_num = 0; ulp_num < BEISCSI_ULP_COUNT; ulp_num++) {
		uint16_t async_arr_idx = 0;

		if (test_bit(ulp_num, &phba->fw_config.ulp_supported)) {
			uint16_t cri = 0;
			struct hd_async_context *pasync_ctx;

			pasync_ctx = HWI_GET_ASYNC_PDU_CTX(
				     phwi_ctrlr, ulp_num);
			for (cri = 0; cri <
			     phba->params.cxns_per_ctrl; cri++) {
				if (ulp_num == BEISCSI_GET_ULP_FROM_CRI
					       (phwi_ctrlr, cri))
					pasync_ctx->cid_to_async_cri_map[
					phwi_ctrlr->wrb_context[cri].cid] =
					async_arr_idx++;
			}
			/**
			 * Now that the default PDU rings have been created,
			 * let EP know about it.
			 */
			beiscsi_hdq_post_handles(phba, BEISCSI_DEFQ_HDR,
						 ulp_num);
			beiscsi_hdq_post_handles(phba, BEISCSI_DEFQ_DATA,
						 ulp_num);
		}
	}

	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
		    "BM_%d : hwi_init_port success\n");
	return 0;

error:
	beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
		    "BM_%d : hwi_init_port failed");
	hwi_cleanup_port(phba);
	return status;
}

static int hwi_init_controller(struct beiscsi_hba *phba)
{
	struct hwi_controller *phwi_ctrlr;

	phwi_ctrlr = phba->phwi_ctrlr;
	if (1 == phba->init_mem[HWI_MEM_ADDN_CONTEXT].num_elements) {
		phwi_ctrlr->phwi_ctxt = (struct hwi_context_memory *)phba->
		    init_mem[HWI_MEM_ADDN_CONTEXT].mem_array[0].virtual_address;
		beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
			    "BM_%d :  phwi_ctrlr->phwi_ctxt=%p\n",
			    phwi_ctrlr->phwi_ctxt);
	} else {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : HWI_MEM_ADDN_CONTEXT is more "
			    "than one element.Failing to load\n");
		return -ENOMEM;
	}

	iscsi_init_global_templates(phba);
	if (beiscsi_init_wrb_handle(phba))
		return -ENOMEM;

	if (hwi_init_async_pdu_ctx(phba)) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : hwi_init_async_pdu_ctx failed\n");
		return -ENOMEM;
	}

	if (hwi_init_port(phba) != 0) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : hwi_init_controller failed\n");

		return -ENOMEM;
	}
	return 0;
}

static void beiscsi_free_mem(struct beiscsi_hba *phba)
{
	struct be_mem_descriptor *mem_descr;
	int i, j;

	mem_descr = phba->init_mem;
	i = 0;
	j = 0;
	for (i = 0; i < SE_MEM_MAX; i++) {
		for (j = mem_descr->num_elements; j > 0; j--) {
			pci_free_consistent(phba->pcidev,
			  mem_descr->mem_array[j - 1].size,
			  mem_descr->mem_array[j - 1].virtual_address,
			  (unsigned long)mem_descr->mem_array[j - 1].
			  bus_address.u.a64.address);
		}

		kfree(mem_descr->mem_array);
		mem_descr++;
	}
	kfree(phba->init_mem);
	kfree(phba->phwi_ctrlr->wrb_context);
	kfree(phba->phwi_ctrlr);
}

static int beiscsi_init_controller(struct beiscsi_hba *phba)
{
	int ret = -ENOMEM;

	ret = beiscsi_get_memory(phba);
	if (ret < 0) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : beiscsi_dev_probe -"
			    "Failed in beiscsi_alloc_memory\n");
		return ret;
	}

	ret = hwi_init_controller(phba);
	if (ret)
		goto free_init;
	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
		    "BM_%d : Return success from beiscsi_init_controller");

	return 0;

free_init:
	beiscsi_free_mem(phba);
	return ret;
}

static int beiscsi_init_sgl_handle(struct beiscsi_hba *phba)
{
	struct be_mem_descriptor *mem_descr_sglh, *mem_descr_sg;
	struct sgl_handle *psgl_handle;
	struct iscsi_sge *pfrag;
	unsigned int arr_index, i, idx;
	unsigned int ulp_icd_start, ulp_num = 0;

	phba->io_sgl_hndl_avbl = 0;
	phba->eh_sgl_hndl_avbl = 0;

	mem_descr_sglh = phba->init_mem;
	mem_descr_sglh += HWI_MEM_SGLH;
	if (1 == mem_descr_sglh->num_elements) {
		phba->io_sgl_hndl_base = kzalloc(sizeof(struct sgl_handle *) *
						 phba->params.ios_per_ctrl,
						 GFP_KERNEL);
		if (!phba->io_sgl_hndl_base) {
			beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
				    "BM_%d : Mem Alloc Failed. Failing to load\n");
			return -ENOMEM;
		}
		phba->eh_sgl_hndl_base = kzalloc(sizeof(struct sgl_handle *) *
						 (phba->params.icds_per_ctrl -
						 phba->params.ios_per_ctrl),
						 GFP_KERNEL);
		if (!phba->eh_sgl_hndl_base) {
			kfree(phba->io_sgl_hndl_base);
			beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
				    "BM_%d : Mem Alloc Failed. Failing to load\n");
			return -ENOMEM;
		}
	} else {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : HWI_MEM_SGLH is more than one element."
			    "Failing to load\n");
		return -ENOMEM;
	}

	arr_index = 0;
	idx = 0;
	while (idx < mem_descr_sglh->num_elements) {
		psgl_handle = mem_descr_sglh->mem_array[idx].virtual_address;

		for (i = 0; i < (mem_descr_sglh->mem_array[idx].size /
		      sizeof(struct sgl_handle)); i++) {
			if (arr_index < phba->params.ios_per_ctrl) {
				phba->io_sgl_hndl_base[arr_index] = psgl_handle;
				phba->io_sgl_hndl_avbl++;
				arr_index++;
			} else {
				phba->eh_sgl_hndl_base[arr_index -
					phba->params.ios_per_ctrl] =
								psgl_handle;
				arr_index++;
				phba->eh_sgl_hndl_avbl++;
			}
			psgl_handle++;
		}
		idx++;
	}
	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
		    "BM_%d : phba->io_sgl_hndl_avbl=%d"
		    "phba->eh_sgl_hndl_avbl=%d\n",
		    phba->io_sgl_hndl_avbl,
		    phba->eh_sgl_hndl_avbl);

	mem_descr_sg = phba->init_mem;
	mem_descr_sg += HWI_MEM_SGE;
	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
		    "\n BM_%d : mem_descr_sg->num_elements=%d\n",
		    mem_descr_sg->num_elements);

	for (ulp_num = 0; ulp_num < BEISCSI_ULP_COUNT; ulp_num++)
		if (test_bit(ulp_num, &phba->fw_config.ulp_supported))
			break;

	ulp_icd_start = phba->fw_config.iscsi_icd_start[ulp_num];

	arr_index = 0;
	idx = 0;
	while (idx < mem_descr_sg->num_elements) {
		pfrag = mem_descr_sg->mem_array[idx].virtual_address;

		for (i = 0;
		     i < (mem_descr_sg->mem_array[idx].size) /
		     (sizeof(struct iscsi_sge) * phba->params.num_sge_per_io);
		     i++) {
			if (arr_index < phba->params.ios_per_ctrl)
				psgl_handle = phba->io_sgl_hndl_base[arr_index];
			else
				psgl_handle = phba->eh_sgl_hndl_base[arr_index -
						phba->params.ios_per_ctrl];
			psgl_handle->pfrag = pfrag;
			AMAP_SET_BITS(struct amap_iscsi_sge, addr_hi, pfrag, 0);
			AMAP_SET_BITS(struct amap_iscsi_sge, addr_lo, pfrag, 0);
			pfrag += phba->params.num_sge_per_io;
			psgl_handle->sgl_index = ulp_icd_start + arr_index++;
		}
		idx++;
	}
	phba->io_sgl_free_index = 0;
	phba->io_sgl_alloc_index = 0;
	phba->eh_sgl_free_index = 0;
	phba->eh_sgl_alloc_index = 0;
	return 0;
}

static int hba_setup_cid_tbls(struct beiscsi_hba *phba)
{
	int ret;
	uint16_t i, ulp_num;
	struct ulp_cid_info *ptr_cid_info = NULL;

	for (ulp_num = 0; ulp_num < BEISCSI_ULP_COUNT; ulp_num++) {
		if (test_bit(ulp_num, (void *)&phba->fw_config.ulp_supported)) {
			ptr_cid_info = kzalloc(sizeof(struct ulp_cid_info),
					       GFP_KERNEL);

			if (!ptr_cid_info) {
				beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
					    "BM_%d : Failed to allocate memory"
					    "for ULP_CID_INFO for ULP : %d\n",
					    ulp_num);
				ret = -ENOMEM;
				goto free_memory;

			}

			/* Allocate memory for CID array */
			ptr_cid_info->cid_array = kzalloc(sizeof(void *) *
						  BEISCSI_GET_CID_COUNT(phba,
						  ulp_num), GFP_KERNEL);
			if (!ptr_cid_info->cid_array) {
				beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
					    "BM_%d : Failed to allocate memory"
					    "for CID_ARRAY for ULP : %d\n",
					    ulp_num);
				kfree(ptr_cid_info);
				ptr_cid_info = NULL;
				ret = -ENOMEM;

				goto free_memory;
			}
			ptr_cid_info->avlbl_cids = BEISCSI_GET_CID_COUNT(
						   phba, ulp_num);

			/* Save the cid_info_array ptr */
			phba->cid_array_info[ulp_num] = ptr_cid_info;
		}
	}
	phba->ep_array = kzalloc(sizeof(struct iscsi_endpoint *) *
				 phba->params.cxns_per_ctrl, GFP_KERNEL);
	if (!phba->ep_array) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : Failed to allocate memory in "
			    "hba_setup_cid_tbls\n");
		ret = -ENOMEM;

		goto free_memory;
	}

	phba->conn_table = kzalloc(sizeof(struct beiscsi_conn *) *
				   phba->params.cxns_per_ctrl, GFP_KERNEL);
	if (!phba->conn_table) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : Failed to allocate memory in"
			    "hba_setup_cid_tbls\n");

		kfree(phba->ep_array);
		phba->ep_array = NULL;
		ret = -ENOMEM;

		goto free_memory;
	}

	for (i = 0; i < phba->params.cxns_per_ctrl; i++) {
		ulp_num = phba->phwi_ctrlr->wrb_context[i].ulp_num;

		ptr_cid_info = phba->cid_array_info[ulp_num];
		ptr_cid_info->cid_array[ptr_cid_info->cid_alloc++] =
			phba->phwi_ctrlr->wrb_context[i].cid;

	}

	for (ulp_num = 0; ulp_num < BEISCSI_ULP_COUNT; ulp_num++) {
		if (test_bit(ulp_num, (void *)&phba->fw_config.ulp_supported)) {
			ptr_cid_info = phba->cid_array_info[ulp_num];

			ptr_cid_info->cid_alloc = 0;
			ptr_cid_info->cid_free = 0;
		}
	}
	return 0;

free_memory:
	for (ulp_num = 0; ulp_num < BEISCSI_ULP_COUNT; ulp_num++) {
		if (test_bit(ulp_num, (void *)&phba->fw_config.ulp_supported)) {
			ptr_cid_info = phba->cid_array_info[ulp_num];

			if (ptr_cid_info) {
				kfree(ptr_cid_info->cid_array);
				kfree(ptr_cid_info);
				phba->cid_array_info[ulp_num] = NULL;
			}
		}
	}

	return ret;
}

static void hwi_enable_intr(struct beiscsi_hba *phba)
{
	struct be_ctrl_info *ctrl = &phba->ctrl;
	struct hwi_controller *phwi_ctrlr;
	struct hwi_context_memory *phwi_context;
	struct be_queue_info *eq;
	u8 __iomem *addr;
	u32 reg, i;
	u32 enabled;

	phwi_ctrlr = phba->phwi_ctrlr;
	phwi_context = phwi_ctrlr->phwi_ctxt;

	addr = (u8 __iomem *) ((u8 __iomem *) ctrl->pcicfg +
			PCICFG_MEMBAR_CTRL_INT_CTRL_OFFSET);
	reg = ioread32(addr);

	enabled = reg & MEMBAR_CTRL_INT_CTRL_HOSTINTR_MASK;
	if (!enabled) {
		reg |= MEMBAR_CTRL_INT_CTRL_HOSTINTR_MASK;
		beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
			    "BM_%d : reg =x%08x addr=%p\n", reg, addr);
		iowrite32(reg, addr);
	}

	if (!phba->msix_enabled) {
		eq = &phwi_context->be_eq[0].q;
		beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
			    "BM_%d : eq->id=%d\n", eq->id);

		hwi_ring_eq_db(phba, eq->id, 0, 0, 1, 1);
	} else {
		for (i = 0; i <= phba->num_cpus; i++) {
			eq = &phwi_context->be_eq[i].q;
			beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
				    "BM_%d : eq->id=%d\n", eq->id);
			hwi_ring_eq_db(phba, eq->id, 0, 0, 1, 1);
		}
	}
}

static void hwi_disable_intr(struct beiscsi_hba *phba)
{
	struct be_ctrl_info *ctrl = &phba->ctrl;

	u8 __iomem *addr = ctrl->pcicfg + PCICFG_MEMBAR_CTRL_INT_CTRL_OFFSET;
	u32 reg = ioread32(addr);

	u32 enabled = reg & MEMBAR_CTRL_INT_CTRL_HOSTINTR_MASK;
	if (enabled) {
		reg &= ~MEMBAR_CTRL_INT_CTRL_HOSTINTR_MASK;
		iowrite32(reg, addr);
	} else
		beiscsi_log(phba, KERN_WARNING, BEISCSI_LOG_INIT,
			    "BM_%d : In hwi_disable_intr, Already Disabled\n");
}

static int beiscsi_init_port(struct beiscsi_hba *phba)
{
	int ret;

	ret = beiscsi_init_controller(phba);
	if (ret < 0) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : beiscsi_dev_probe - Failed in"
			    "beiscsi_init_controller\n");
		return ret;
	}
	ret = beiscsi_init_sgl_handle(phba);
	if (ret < 0) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : beiscsi_dev_probe - Failed in"
			    "beiscsi_init_sgl_handle\n");
		goto do_cleanup_ctrlr;
	}

	if (hba_setup_cid_tbls(phba)) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : Failed in hba_setup_cid_tbls\n");
		kfree(phba->io_sgl_hndl_base);
		kfree(phba->eh_sgl_hndl_base);
		goto do_cleanup_ctrlr;
	}

	return ret;

do_cleanup_ctrlr:
	hwi_cleanup_port(phba);
	return ret;
}

static void beiscsi_cleanup_port(struct beiscsi_hba *phba)
{
	struct ulp_cid_info *ptr_cid_info = NULL;
	int ulp_num;

	kfree(phba->io_sgl_hndl_base);
	kfree(phba->eh_sgl_hndl_base);
	kfree(phba->ep_array);
	kfree(phba->conn_table);

	for (ulp_num = 0; ulp_num < BEISCSI_ULP_COUNT; ulp_num++) {
		if (test_bit(ulp_num, (void *)&phba->fw_config.ulp_supported)) {
			ptr_cid_info = phba->cid_array_info[ulp_num];

			if (ptr_cid_info) {
				kfree(ptr_cid_info->cid_array);
				kfree(ptr_cid_info);
				phba->cid_array_info[ulp_num] = NULL;
			}
		}
	}
}

/**
 * beiscsi_free_mgmt_task_handles()- Free driver CXN resources
 * @beiscsi_conn: ptr to the conn to be cleaned up
 * @task: ptr to iscsi_task resource to be freed.
 *
 * Free driver mgmt resources binded to CXN.
 **/
void
beiscsi_free_mgmt_task_handles(struct beiscsi_conn *beiscsi_conn,
				struct iscsi_task *task)
{
	struct beiscsi_io_task *io_task;
	struct beiscsi_hba *phba = beiscsi_conn->phba;
	struct hwi_wrb_context *pwrb_context;
	struct hwi_controller *phwi_ctrlr;
	uint16_t cri_index = BE_GET_CRI_FROM_CID(
				beiscsi_conn->beiscsi_conn_cid);

	phwi_ctrlr = phba->phwi_ctrlr;
	pwrb_context = &phwi_ctrlr->wrb_context[cri_index];

	io_task = task->dd_data;

	if (io_task->pwrb_handle) {
		free_wrb_handle(phba, pwrb_context, io_task->pwrb_handle);
		io_task->pwrb_handle = NULL;
	}

	if (io_task->psgl_handle) {
		free_mgmt_sgl_handle(phba, io_task->psgl_handle);
		io_task->psgl_handle = NULL;
	}

	if (io_task->mtask_addr) {
		pci_unmap_single(phba->pcidev,
				 io_task->mtask_addr,
				 io_task->mtask_data_count,
				 PCI_DMA_TODEVICE);
		io_task->mtask_addr = 0;
	}
}

/**
 * beiscsi_cleanup_task()- Free driver resources of the task
 * @task: ptr to the iscsi task
 *
 **/
static void beiscsi_cleanup_task(struct iscsi_task *task)
{
	struct beiscsi_io_task *io_task = task->dd_data;
	struct iscsi_conn *conn = task->conn;
	struct beiscsi_conn *beiscsi_conn = conn->dd_data;
	struct beiscsi_hba *phba = beiscsi_conn->phba;
	struct beiscsi_session *beiscsi_sess = beiscsi_conn->beiscsi_sess;
	struct hwi_wrb_context *pwrb_context;
	struct hwi_controller *phwi_ctrlr;
	uint16_t cri_index = BE_GET_CRI_FROM_CID(
			     beiscsi_conn->beiscsi_conn_cid);

	phwi_ctrlr = phba->phwi_ctrlr;
	pwrb_context = &phwi_ctrlr->wrb_context[cri_index];

	if (io_task->cmd_bhs) {
		pci_pool_free(beiscsi_sess->bhs_pool, io_task->cmd_bhs,
			      io_task->bhs_pa.u.a64.address);
		io_task->cmd_bhs = NULL;
		task->hdr = NULL;
	}

	if (task->sc) {
		if (io_task->pwrb_handle) {
			free_wrb_handle(phba, pwrb_context,
					io_task->pwrb_handle);
			io_task->pwrb_handle = NULL;
		}

		if (io_task->psgl_handle) {
			free_io_sgl_handle(phba, io_task->psgl_handle);
			io_task->psgl_handle = NULL;
		}

		if (io_task->scsi_cmnd) {
			if (io_task->num_sg)
				scsi_dma_unmap(io_task->scsi_cmnd);
			io_task->scsi_cmnd = NULL;
		}
	} else {
		if (!beiscsi_conn->login_in_progress)
			beiscsi_free_mgmt_task_handles(beiscsi_conn, task);
	}
}

void
beiscsi_offload_connection(struct beiscsi_conn *beiscsi_conn,
			   struct beiscsi_offload_params *params)
{
	struct wrb_handle *pwrb_handle;
	struct hwi_wrb_context *pwrb_context = NULL;
	struct beiscsi_hba *phba = beiscsi_conn->phba;
	struct iscsi_task *task = beiscsi_conn->task;
	struct iscsi_session *session = task->conn->session;
	u32 doorbell = 0;

	/*
	 * We can always use 0 here because it is reserved by libiscsi for
	 * login/startup related tasks.
	 */
	beiscsi_conn->login_in_progress = 0;
	spin_lock_bh(&session->back_lock);
	beiscsi_cleanup_task(task);
	spin_unlock_bh(&session->back_lock);

	pwrb_handle = alloc_wrb_handle(phba, beiscsi_conn->beiscsi_conn_cid,
				       &pwrb_context);

	/* Check for the adapter family */
	if (is_chip_be2_be3r(phba))
		beiscsi_offload_cxn_v0(params, pwrb_handle,
				       phba->init_mem,
				       pwrb_context);
	else
		beiscsi_offload_cxn_v2(params, pwrb_handle,
				       pwrb_context);

	be_dws_le_to_cpu(pwrb_handle->pwrb,
			 sizeof(struct iscsi_target_context_update_wrb));

	doorbell |= beiscsi_conn->beiscsi_conn_cid & DB_WRB_POST_CID_MASK;
	doorbell |= (pwrb_handle->wrb_index & DB_DEF_PDU_WRB_INDEX_MASK)
			     << DB_DEF_PDU_WRB_INDEX_SHIFT;
	doorbell |= 1 << DB_DEF_PDU_NUM_POSTED_SHIFT;
	iowrite32(doorbell, phba->db_va +
		  beiscsi_conn->doorbell_offset);

	/*
	 * There is no completion for CONTEXT_UPDATE. The completion of next
	 * WRB posted guarantees FW's processing and DMA'ing of it.
	 * Use beiscsi_put_wrb_handle to put it back in the pool which makes
	 * sure zero'ing or reuse of the WRB only after wrbs_per_cxn.
	 */
	beiscsi_put_wrb_handle(pwrb_context, pwrb_handle,
			       phba->params.wrbs_per_cxn);
	beiscsi_log(phba, KERN_INFO,
		    BEISCSI_LOG_IO | BEISCSI_LOG_CONFIG,
		    "BM_%d : put CONTEXT_UPDATE pwrb_handle=%p free_index=0x%x wrb_handles_available=%d\n",
		    pwrb_handle, pwrb_context->free_index,
		    pwrb_context->wrb_handles_available);
}

static void beiscsi_parse_pdu(struct iscsi_conn *conn, itt_t itt,
			      int *index, int *age)
{
	*index = (int)itt;
	if (age)
		*age = conn->session->age;
}

/**
 * beiscsi_alloc_pdu - allocates pdu and related resources
 * @task: libiscsi task
 * @opcode: opcode of pdu for task
 *
 * This is called with the session lock held. It will allocate
 * the wrb and sgl if needed for the command. And it will prep
 * the pdu's itt. beiscsi_parse_pdu will later translate
 * the pdu itt to the libiscsi task itt.
 */
static int beiscsi_alloc_pdu(struct iscsi_task *task, uint8_t opcode)
{
	struct beiscsi_io_task *io_task = task->dd_data;
	struct iscsi_conn *conn = task->conn;
	struct beiscsi_conn *beiscsi_conn = conn->dd_data;
	struct beiscsi_hba *phba = beiscsi_conn->phba;
	struct hwi_wrb_context *pwrb_context;
	struct hwi_controller *phwi_ctrlr;
	itt_t itt;
	uint16_t cri_index = 0;
	struct beiscsi_session *beiscsi_sess = beiscsi_conn->beiscsi_sess;
	dma_addr_t paddr;

	io_task->cmd_bhs = pci_pool_alloc(beiscsi_sess->bhs_pool,
					  GFP_ATOMIC, &paddr);
	if (!io_task->cmd_bhs)
		return -ENOMEM;
	io_task->bhs_pa.u.a64.address = paddr;
	io_task->libiscsi_itt = (itt_t)task->itt;
	io_task->conn = beiscsi_conn;

	task->hdr = (struct iscsi_hdr *)&io_task->cmd_bhs->iscsi_hdr;
	task->hdr_max = sizeof(struct be_cmd_bhs);
	io_task->psgl_handle = NULL;
	io_task->pwrb_handle = NULL;

	if (task->sc) {
		io_task->psgl_handle = alloc_io_sgl_handle(phba);
		if (!io_task->psgl_handle) {
			beiscsi_log(phba, KERN_ERR,
				    BEISCSI_LOG_IO | BEISCSI_LOG_CONFIG,
				    "BM_%d : Alloc of IO_SGL_ICD Failed"
				    "for the CID : %d\n",
				    beiscsi_conn->beiscsi_conn_cid);
			goto free_hndls;
		}
		io_task->pwrb_handle = alloc_wrb_handle(phba,
					beiscsi_conn->beiscsi_conn_cid,
					&io_task->pwrb_context);
		if (!io_task->pwrb_handle) {
			beiscsi_log(phba, KERN_ERR,
				    BEISCSI_LOG_IO | BEISCSI_LOG_CONFIG,
				    "BM_%d : Alloc of WRB_HANDLE Failed"
				    "for the CID : %d\n",
				    beiscsi_conn->beiscsi_conn_cid);
			goto free_io_hndls;
		}
	} else {
		io_task->scsi_cmnd = NULL;
		if ((opcode & ISCSI_OPCODE_MASK) == ISCSI_OP_LOGIN) {
			beiscsi_conn->task = task;
			if (!beiscsi_conn->login_in_progress) {
				io_task->psgl_handle = (struct sgl_handle *)
						alloc_mgmt_sgl_handle(phba);
				if (!io_task->psgl_handle) {
					beiscsi_log(phba, KERN_ERR,
						    BEISCSI_LOG_IO |
						    BEISCSI_LOG_CONFIG,
						    "BM_%d : Alloc of MGMT_SGL_ICD Failed"
						    "for the CID : %d\n",
						    beiscsi_conn->
						    beiscsi_conn_cid);
					goto free_hndls;
				}

				beiscsi_conn->login_in_progress = 1;
				beiscsi_conn->plogin_sgl_handle =
							io_task->psgl_handle;
				io_task->pwrb_handle =
					alloc_wrb_handle(phba,
					beiscsi_conn->beiscsi_conn_cid,
					&io_task->pwrb_context);
				if (!io_task->pwrb_handle) {
					beiscsi_log(phba, KERN_ERR,
						    BEISCSI_LOG_IO |
						    BEISCSI_LOG_CONFIG,
						    "BM_%d : Alloc of WRB_HANDLE Failed"
						    "for the CID : %d\n",
						    beiscsi_conn->
						    beiscsi_conn_cid);
					goto free_mgmt_hndls;
				}
				beiscsi_conn->plogin_wrb_handle =
							io_task->pwrb_handle;

			} else {
				io_task->psgl_handle =
						beiscsi_conn->plogin_sgl_handle;
				io_task->pwrb_handle =
						beiscsi_conn->plogin_wrb_handle;
			}
		} else {
			io_task->psgl_handle = alloc_mgmt_sgl_handle(phba);
			if (!io_task->psgl_handle) {
				beiscsi_log(phba, KERN_ERR,
					    BEISCSI_LOG_IO |
					    BEISCSI_LOG_CONFIG,
					    "BM_%d : Alloc of MGMT_SGL_ICD Failed"
					    "for the CID : %d\n",
					    beiscsi_conn->
					    beiscsi_conn_cid);
				goto free_hndls;
			}
			io_task->pwrb_handle =
					alloc_wrb_handle(phba,
					beiscsi_conn->beiscsi_conn_cid,
					&io_task->pwrb_context);
			if (!io_task->pwrb_handle) {
				beiscsi_log(phba, KERN_ERR,
					    BEISCSI_LOG_IO | BEISCSI_LOG_CONFIG,
					    "BM_%d : Alloc of WRB_HANDLE Failed"
					    "for the CID : %d\n",
					    beiscsi_conn->beiscsi_conn_cid);
				goto free_mgmt_hndls;
			}

		}
	}
	itt = (itt_t) cpu_to_be32(((unsigned int)io_task->pwrb_handle->
				 wrb_index << 16) | (unsigned int)
				(io_task->psgl_handle->sgl_index));
	io_task->pwrb_handle->pio_handle = task;

	io_task->cmd_bhs->iscsi_hdr.itt = itt;
	return 0;

free_io_hndls:
	free_io_sgl_handle(phba, io_task->psgl_handle);
	goto free_hndls;
free_mgmt_hndls:
	free_mgmt_sgl_handle(phba, io_task->psgl_handle);
	io_task->psgl_handle = NULL;
free_hndls:
	phwi_ctrlr = phba->phwi_ctrlr;
	cri_index = BE_GET_CRI_FROM_CID(
	beiscsi_conn->beiscsi_conn_cid);
	pwrb_context = &phwi_ctrlr->wrb_context[cri_index];
	if (io_task->pwrb_handle)
		free_wrb_handle(phba, pwrb_context, io_task->pwrb_handle);
	io_task->pwrb_handle = NULL;
	pci_pool_free(beiscsi_sess->bhs_pool, io_task->cmd_bhs,
		      io_task->bhs_pa.u.a64.address);
	io_task->cmd_bhs = NULL;
	return -ENOMEM;
}
int beiscsi_iotask_v2(struct iscsi_task *task, struct scatterlist *sg,
		       unsigned int num_sg, unsigned int xferlen,
		       unsigned int writedir)
{

	struct beiscsi_io_task *io_task = task->dd_data;
	struct iscsi_conn *conn = task->conn;
	struct beiscsi_conn *beiscsi_conn = conn->dd_data;
	struct beiscsi_hba *phba = beiscsi_conn->phba;
	struct iscsi_wrb *pwrb = NULL;
	unsigned int doorbell = 0;

	pwrb = io_task->pwrb_handle->pwrb;

	io_task->bhs_len = sizeof(struct be_cmd_bhs);

	if (writedir) {
		AMAP_SET_BITS(struct amap_iscsi_wrb_v2, type, pwrb,
			      INI_WR_CMD);
		AMAP_SET_BITS(struct amap_iscsi_wrb_v2, dsp, pwrb, 1);
	} else {
		AMAP_SET_BITS(struct amap_iscsi_wrb_v2, type, pwrb,
			      INI_RD_CMD);
		AMAP_SET_BITS(struct amap_iscsi_wrb_v2, dsp, pwrb, 0);
	}

	io_task->wrb_type = AMAP_GET_BITS(struct amap_iscsi_wrb_v2,
					  type, pwrb);

	AMAP_SET_BITS(struct amap_iscsi_wrb_v2, lun, pwrb,
		      cpu_to_be16(*(unsigned short *)
		      &io_task->cmd_bhs->iscsi_hdr.lun));
	AMAP_SET_BITS(struct amap_iscsi_wrb_v2, r2t_exp_dtl, pwrb, xferlen);
	AMAP_SET_BITS(struct amap_iscsi_wrb_v2, wrb_idx, pwrb,
		      io_task->pwrb_handle->wrb_index);
	AMAP_SET_BITS(struct amap_iscsi_wrb_v2, cmdsn_itt, pwrb,
		      be32_to_cpu(task->cmdsn));
	AMAP_SET_BITS(struct amap_iscsi_wrb_v2, sgl_idx, pwrb,
		      io_task->psgl_handle->sgl_index);

	hwi_write_sgl_v2(pwrb, sg, num_sg, io_task);
	AMAP_SET_BITS(struct amap_iscsi_wrb_v2, ptr2nextwrb, pwrb,
		      io_task->pwrb_handle->wrb_index);
	if (io_task->pwrb_context->plast_wrb)
		AMAP_SET_BITS(struct amap_iscsi_wrb_v2, ptr2nextwrb,
			      io_task->pwrb_context->plast_wrb,
			      io_task->pwrb_handle->wrb_index);
	io_task->pwrb_context->plast_wrb = pwrb;

	be_dws_le_to_cpu(pwrb, sizeof(struct iscsi_wrb));

	doorbell |= beiscsi_conn->beiscsi_conn_cid & DB_WRB_POST_CID_MASK;
	doorbell |= (io_task->pwrb_handle->wrb_index &
		     DB_DEF_PDU_WRB_INDEX_MASK) <<
		     DB_DEF_PDU_WRB_INDEX_SHIFT;
	doorbell |= 1 << DB_DEF_PDU_NUM_POSTED_SHIFT;
	iowrite32(doorbell, phba->db_va +
		  beiscsi_conn->doorbell_offset);
	return 0;
}

static int beiscsi_iotask(struct iscsi_task *task, struct scatterlist *sg,
			  unsigned int num_sg, unsigned int xferlen,
			  unsigned int writedir)
{

	struct beiscsi_io_task *io_task = task->dd_data;
	struct iscsi_conn *conn = task->conn;
	struct beiscsi_conn *beiscsi_conn = conn->dd_data;
	struct beiscsi_hba *phba = beiscsi_conn->phba;
	struct iscsi_wrb *pwrb = NULL;
	unsigned int doorbell = 0;

	pwrb = io_task->pwrb_handle->pwrb;
	io_task->bhs_len = sizeof(struct be_cmd_bhs);

	if (writedir) {
		AMAP_SET_BITS(struct amap_iscsi_wrb, type, pwrb,
			      INI_WR_CMD);
		AMAP_SET_BITS(struct amap_iscsi_wrb, dsp, pwrb, 1);
	} else {
		AMAP_SET_BITS(struct amap_iscsi_wrb, type, pwrb,
			      INI_RD_CMD);
		AMAP_SET_BITS(struct amap_iscsi_wrb, dsp, pwrb, 0);
	}

	io_task->wrb_type = AMAP_GET_BITS(struct amap_iscsi_wrb,
					  type, pwrb);

	AMAP_SET_BITS(struct amap_iscsi_wrb, lun, pwrb,
		      cpu_to_be16(*(unsigned short *)
				  &io_task->cmd_bhs->iscsi_hdr.lun));
	AMAP_SET_BITS(struct amap_iscsi_wrb, r2t_exp_dtl, pwrb, xferlen);
	AMAP_SET_BITS(struct amap_iscsi_wrb, wrb_idx, pwrb,
		      io_task->pwrb_handle->wrb_index);
	AMAP_SET_BITS(struct amap_iscsi_wrb, cmdsn_itt, pwrb,
		      be32_to_cpu(task->cmdsn));
	AMAP_SET_BITS(struct amap_iscsi_wrb, sgl_icd_idx, pwrb,
		      io_task->psgl_handle->sgl_index);

	hwi_write_sgl(pwrb, sg, num_sg, io_task);

	AMAP_SET_BITS(struct amap_iscsi_wrb, ptr2nextwrb, pwrb,
		      io_task->pwrb_handle->wrb_index);
	if (io_task->pwrb_context->plast_wrb)
		AMAP_SET_BITS(struct amap_iscsi_wrb, ptr2nextwrb,
			      io_task->pwrb_context->plast_wrb,
			      io_task->pwrb_handle->wrb_index);
	io_task->pwrb_context->plast_wrb = pwrb;

	be_dws_le_to_cpu(pwrb, sizeof(struct iscsi_wrb));

	doorbell |= beiscsi_conn->beiscsi_conn_cid & DB_WRB_POST_CID_MASK;
	doorbell |= (io_task->pwrb_handle->wrb_index &
		     DB_DEF_PDU_WRB_INDEX_MASK) << DB_DEF_PDU_WRB_INDEX_SHIFT;
	doorbell |= 1 << DB_DEF_PDU_NUM_POSTED_SHIFT;

	iowrite32(doorbell, phba->db_va +
		  beiscsi_conn->doorbell_offset);
	return 0;
}

static int beiscsi_mtask(struct iscsi_task *task)
{
	struct beiscsi_io_task *io_task = task->dd_data;
	struct iscsi_conn *conn = task->conn;
	struct beiscsi_conn *beiscsi_conn = conn->dd_data;
	struct beiscsi_hba *phba = beiscsi_conn->phba;
	struct iscsi_wrb *pwrb = NULL;
	unsigned int doorbell = 0;
	unsigned int cid;
	unsigned int pwrb_typeoffset = 0;
	int ret = 0;

	cid = beiscsi_conn->beiscsi_conn_cid;
	pwrb = io_task->pwrb_handle->pwrb;
	memset(pwrb, 0, sizeof(*pwrb));

	if (is_chip_be2_be3r(phba)) {
		AMAP_SET_BITS(struct amap_iscsi_wrb, cmdsn_itt, pwrb,
			      be32_to_cpu(task->cmdsn));
		AMAP_SET_BITS(struct amap_iscsi_wrb, wrb_idx, pwrb,
			      io_task->pwrb_handle->wrb_index);
		AMAP_SET_BITS(struct amap_iscsi_wrb, sgl_icd_idx, pwrb,
			      io_task->psgl_handle->sgl_index);
		AMAP_SET_BITS(struct amap_iscsi_wrb, r2t_exp_dtl, pwrb,
			      task->data_count);
		AMAP_SET_BITS(struct amap_iscsi_wrb, ptr2nextwrb, pwrb,
			      io_task->pwrb_handle->wrb_index);
		if (io_task->pwrb_context->plast_wrb)
			AMAP_SET_BITS(struct amap_iscsi_wrb, ptr2nextwrb,
				      io_task->pwrb_context->plast_wrb,
				      io_task->pwrb_handle->wrb_index);
		io_task->pwrb_context->plast_wrb = pwrb;

		pwrb_typeoffset = BE_WRB_TYPE_OFFSET;
	} else {
		AMAP_SET_BITS(struct amap_iscsi_wrb_v2, cmdsn_itt, pwrb,
			      be32_to_cpu(task->cmdsn));
		AMAP_SET_BITS(struct amap_iscsi_wrb_v2, wrb_idx, pwrb,
			      io_task->pwrb_handle->wrb_index);
		AMAP_SET_BITS(struct amap_iscsi_wrb_v2, sgl_idx, pwrb,
			      io_task->psgl_handle->sgl_index);
		AMAP_SET_BITS(struct amap_iscsi_wrb_v2, r2t_exp_dtl, pwrb,
			      task->data_count);
		AMAP_SET_BITS(struct amap_iscsi_wrb_v2, ptr2nextwrb, pwrb,
			      io_task->pwrb_handle->wrb_index);
		if (io_task->pwrb_context->plast_wrb)
			AMAP_SET_BITS(struct amap_iscsi_wrb_v2, ptr2nextwrb,
				      io_task->pwrb_context->plast_wrb,
				      io_task->pwrb_handle->wrb_index);
		io_task->pwrb_context->plast_wrb = pwrb;

		pwrb_typeoffset = SKH_WRB_TYPE_OFFSET;
	}


	switch (task->hdr->opcode & ISCSI_OPCODE_MASK) {
	case ISCSI_OP_LOGIN:
		AMAP_SET_BITS(struct amap_iscsi_wrb, cmdsn_itt, pwrb, 1);
		ADAPTER_SET_WRB_TYPE(pwrb, TGT_DM_CMD, pwrb_typeoffset);
		ret = hwi_write_buffer(pwrb, task);
		break;
	case ISCSI_OP_NOOP_OUT:
		if (task->hdr->ttt != ISCSI_RESERVED_TAG) {
			ADAPTER_SET_WRB_TYPE(pwrb, TGT_DM_CMD, pwrb_typeoffset);
			if (is_chip_be2_be3r(phba))
				AMAP_SET_BITS(struct amap_iscsi_wrb,
					      dmsg, pwrb, 1);
			else
				AMAP_SET_BITS(struct amap_iscsi_wrb_v2,
					      dmsg, pwrb, 1);
		} else {
			ADAPTER_SET_WRB_TYPE(pwrb, INI_RD_CMD, pwrb_typeoffset);
			if (is_chip_be2_be3r(phba))
				AMAP_SET_BITS(struct amap_iscsi_wrb,
					      dmsg, pwrb, 0);
			else
				AMAP_SET_BITS(struct amap_iscsi_wrb_v2,
					      dmsg, pwrb, 0);
		}
		ret = hwi_write_buffer(pwrb, task);
		break;
	case ISCSI_OP_TEXT:
		ADAPTER_SET_WRB_TYPE(pwrb, TGT_DM_CMD, pwrb_typeoffset);
		ret = hwi_write_buffer(pwrb, task);
		break;
	case ISCSI_OP_SCSI_TMFUNC:
		ADAPTER_SET_WRB_TYPE(pwrb, INI_TMF_CMD, pwrb_typeoffset);
		ret = hwi_write_buffer(pwrb, task);
		break;
	case ISCSI_OP_LOGOUT:
		ADAPTER_SET_WRB_TYPE(pwrb, HWH_TYPE_LOGOUT, pwrb_typeoffset);
		ret = hwi_write_buffer(pwrb, task);
		break;

	default:
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_CONFIG,
			    "BM_%d : opcode =%d Not supported\n",
			    task->hdr->opcode & ISCSI_OPCODE_MASK);

		return -EINVAL;
	}

	if (ret)
		return ret;

	/* Set the task type */
	io_task->wrb_type = (is_chip_be2_be3r(phba)) ?
		AMAP_GET_BITS(struct amap_iscsi_wrb, type, pwrb) :
		AMAP_GET_BITS(struct amap_iscsi_wrb_v2, type, pwrb);

	doorbell |= cid & DB_WRB_POST_CID_MASK;
	doorbell |= (io_task->pwrb_handle->wrb_index &
		     DB_DEF_PDU_WRB_INDEX_MASK) << DB_DEF_PDU_WRB_INDEX_SHIFT;
	doorbell |= 1 << DB_DEF_PDU_NUM_POSTED_SHIFT;
	iowrite32(doorbell, phba->db_va +
		  beiscsi_conn->doorbell_offset);
	return 0;
}

static int beiscsi_task_xmit(struct iscsi_task *task)
{
	struct beiscsi_io_task *io_task = task->dd_data;
	struct scsi_cmnd *sc = task->sc;
	struct beiscsi_hba *phba;
	struct scatterlist *sg;
	int num_sg;
	unsigned int  writedir = 0, xferlen = 0;

	phba = io_task->conn->phba;
	/**
	 * HBA in error includes BEISCSI_HBA_FW_TIMEOUT. IO path might be
	 * operational if FW still gets heartbeat from EP FW. Is management
	 * path really needed to continue further?
	 */
	if (!beiscsi_hba_is_online(phba))
		return -EIO;

	if (!io_task->conn->login_in_progress)
		task->hdr->exp_statsn = 0;

	if (!sc)
		return beiscsi_mtask(task);

	io_task->scsi_cmnd = sc;
	io_task->num_sg = 0;
	num_sg = scsi_dma_map(sc);
	if (num_sg < 0) {
		beiscsi_log(phba, KERN_ERR,
			    BEISCSI_LOG_IO | BEISCSI_LOG_ISCSI,
			    "BM_%d : scsi_dma_map Failed "
			    "Driver_ITT : 0x%x ITT : 0x%x Xferlen : 0x%x\n",
			    be32_to_cpu(io_task->cmd_bhs->iscsi_hdr.itt),
			    io_task->libiscsi_itt, scsi_bufflen(sc));

		return num_sg;
	}
	/**
	 * For scsi cmd task, check num_sg before unmapping in cleanup_task.
	 * For management task, cleanup_task checks mtask_addr before unmapping.
	 */
	io_task->num_sg = num_sg;
	xferlen = scsi_bufflen(sc);
	sg = scsi_sglist(sc);
	if (sc->sc_data_direction == DMA_TO_DEVICE)
		writedir = 1;
	 else
		writedir = 0;

	 return phba->iotask_fn(task, sg, num_sg, xferlen, writedir);
}

/**
 * beiscsi_bsg_request - handle bsg request from ISCSI transport
 * @job: job to handle
 */
static int beiscsi_bsg_request(struct bsg_job *job)
{
	struct Scsi_Host *shost;
	struct beiscsi_hba *phba;
	struct iscsi_bsg_request *bsg_req = job->request;
	int rc = -EINVAL;
	unsigned int tag;
	struct be_dma_mem nonemb_cmd;
	struct be_cmd_resp_hdr *resp;
	struct iscsi_bsg_reply *bsg_reply = job->reply;
	unsigned short status, extd_status;

	shost = iscsi_job_to_shost(job);
	phba = iscsi_host_priv(shost);

	if (!beiscsi_hba_is_online(phba)) {
		beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_CONFIG,
			    "BM_%d : HBA in error 0x%lx\n", phba->state);
		return -ENXIO;
	}

	switch (bsg_req->msgcode) {
	case ISCSI_BSG_HST_VENDOR:
		nonemb_cmd.va = pci_alloc_consistent(phba->ctrl.pdev,
					job->request_payload.payload_len,
					&nonemb_cmd.dma);
		if (nonemb_cmd.va == NULL) {
			beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_CONFIG,
				    "BM_%d : Failed to allocate memory for "
				    "beiscsi_bsg_request\n");
			return -ENOMEM;
		}
		tag = mgmt_vendor_specific_fw_cmd(&phba->ctrl, phba, job,
						  &nonemb_cmd);
		if (!tag) {
			beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_CONFIG,
				    "BM_%d : MBX Tag Allocation Failed\n");

			pci_free_consistent(phba->ctrl.pdev, nonemb_cmd.size,
					    nonemb_cmd.va, nonemb_cmd.dma);
			return -EAGAIN;
		}

		rc = wait_event_interruptible_timeout(
					phba->ctrl.mcc_wait[tag],
					phba->ctrl.mcc_tag_status[tag],
					msecs_to_jiffies(
					BEISCSI_HOST_MBX_TIMEOUT));

		if (!test_bit(BEISCSI_HBA_ONLINE, &phba->state)) {
			clear_bit(MCC_TAG_STATE_RUNNING,
				  &phba->ctrl.ptag_state[tag].tag_state);
			pci_free_consistent(phba->ctrl.pdev, nonemb_cmd.size,
					    nonemb_cmd.va, nonemb_cmd.dma);
			return -EIO;
		}
		extd_status = (phba->ctrl.mcc_tag_status[tag] &
			       CQE_STATUS_ADDL_MASK) >> CQE_STATUS_ADDL_SHIFT;
		status = phba->ctrl.mcc_tag_status[tag] & CQE_STATUS_MASK;
		free_mcc_wrb(&phba->ctrl, tag);
		resp = (struct be_cmd_resp_hdr *)nonemb_cmd.va;
		sg_copy_from_buffer(job->reply_payload.sg_list,
				    job->reply_payload.sg_cnt,
				    nonemb_cmd.va, (resp->response_length
				    + sizeof(*resp)));
		bsg_reply->reply_payload_rcv_len = resp->response_length;
		bsg_reply->result = status;
		bsg_job_done(job, bsg_reply->result,
			     bsg_reply->reply_payload_rcv_len);
		pci_free_consistent(phba->ctrl.pdev, nonemb_cmd.size,
				    nonemb_cmd.va, nonemb_cmd.dma);
		if (status || extd_status) {
			beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_CONFIG,
				    "BM_%d : MBX Cmd Failed"
				    " status = %d extd_status = %d\n",
				    status, extd_status);

			return -EIO;
		} else {
			rc = 0;
		}
		break;

	default:
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_CONFIG,
				"BM_%d : Unsupported bsg command: 0x%x\n",
				bsg_req->msgcode);
		break;
	}

	return rc;
}

void beiscsi_hba_attrs_init(struct beiscsi_hba *phba)
{
	/* Set the logging parameter */
	beiscsi_log_enable_init(phba, beiscsi_log_enable);
}

void beiscsi_start_boot_work(struct beiscsi_hba *phba, unsigned int s_handle)
{
	if (phba->boot_struct.boot_kset)
		return;

	/* skip if boot work is already in progress */
	if (test_and_set_bit(BEISCSI_HBA_BOOT_WORK, &phba->state))
		return;

	phba->boot_struct.retry = 3;
	phba->boot_struct.tag = 0;
	phba->boot_struct.s_handle = s_handle;
	phba->boot_struct.action = BEISCSI_BOOT_GET_SHANDLE;
	schedule_work(&phba->boot_work);
}

static ssize_t beiscsi_show_boot_tgt_info(void *data, int type, char *buf)
{
	struct beiscsi_hba *phba = data;
	struct mgmt_session_info *boot_sess = &phba->boot_struct.boot_sess;
	struct mgmt_conn_info *boot_conn = &boot_sess->conn_list[0];
	char *str = buf;
	int rc = -EPERM;

	switch (type) {
	case ISCSI_BOOT_TGT_NAME:
		rc = sprintf(buf, "%.*s\n",
			    (int)strlen(boot_sess->target_name),
			    (char *)&boot_sess->target_name);
		break;
	case ISCSI_BOOT_TGT_IP_ADDR:
		if (boot_conn->dest_ipaddr.ip_type == BEISCSI_IP_TYPE_V4)
			rc = sprintf(buf, "%pI4\n",
				(char *)&boot_conn->dest_ipaddr.addr);
		else
			rc = sprintf(str, "%pI6\n",
				(char *)&boot_conn->dest_ipaddr.addr);
		break;
	case ISCSI_BOOT_TGT_PORT:
		rc = sprintf(str, "%d\n", boot_conn->dest_port);
		break;

	case ISCSI_BOOT_TGT_CHAP_NAME:
		rc = sprintf(str,  "%.*s\n",
			     boot_conn->negotiated_login_options.auth_data.chap.
			     target_chap_name_length,
			     (char *)&boot_conn->negotiated_login_options.
			     auth_data.chap.target_chap_name);
		break;
	case ISCSI_BOOT_TGT_CHAP_SECRET:
		rc = sprintf(str,  "%.*s\n",
			     boot_conn->negotiated_login_options.auth_data.chap.
			     target_secret_length,
			     (char *)&boot_conn->negotiated_login_options.
			     auth_data.chap.target_secret);
		break;
	case ISCSI_BOOT_TGT_REV_CHAP_NAME:
		rc = sprintf(str,  "%.*s\n",
			     boot_conn->negotiated_login_options.auth_data.chap.
			     intr_chap_name_length,
			     (char *)&boot_conn->negotiated_login_options.
			     auth_data.chap.intr_chap_name);
		break;
	case ISCSI_BOOT_TGT_REV_CHAP_SECRET:
		rc = sprintf(str,  "%.*s\n",
			     boot_conn->negotiated_login_options.auth_data.chap.
			     intr_secret_length,
			     (char *)&boot_conn->negotiated_login_options.
			     auth_data.chap.intr_secret);
		break;
	case ISCSI_BOOT_TGT_FLAGS:
		rc = sprintf(str, "2\n");
		break;
	case ISCSI_BOOT_TGT_NIC_ASSOC:
		rc = sprintf(str, "0\n");
		break;
	}
	return rc;
}

static ssize_t beiscsi_show_boot_ini_info(void *data, int type, char *buf)
{
	struct beiscsi_hba *phba = data;
	char *str = buf;
	int rc = -EPERM;

	switch (type) {
	case ISCSI_BOOT_INI_INITIATOR_NAME:
		rc = sprintf(str, "%s\n",
			     phba->boot_struct.boot_sess.initiator_iscsiname);
		break;
	}
	return rc;
}

static ssize_t beiscsi_show_boot_eth_info(void *data, int type, char *buf)
{
	struct beiscsi_hba *phba = data;
	char *str = buf;
	int rc = -EPERM;

	switch (type) {
	case ISCSI_BOOT_ETH_FLAGS:
		rc = sprintf(str, "2\n");
		break;
	case ISCSI_BOOT_ETH_INDEX:
		rc = sprintf(str, "0\n");
		break;
	case ISCSI_BOOT_ETH_MAC:
		rc  = beiscsi_get_macaddr(str, phba);
		break;
	}
	return rc;
}

static umode_t beiscsi_tgt_get_attr_visibility(void *data, int type)
{
	umode_t rc = 0;

	switch (type) {
	case ISCSI_BOOT_TGT_NAME:
	case ISCSI_BOOT_TGT_IP_ADDR:
	case ISCSI_BOOT_TGT_PORT:
	case ISCSI_BOOT_TGT_CHAP_NAME:
	case ISCSI_BOOT_TGT_CHAP_SECRET:
	case ISCSI_BOOT_TGT_REV_CHAP_NAME:
	case ISCSI_BOOT_TGT_REV_CHAP_SECRET:
	case ISCSI_BOOT_TGT_NIC_ASSOC:
	case ISCSI_BOOT_TGT_FLAGS:
		rc = S_IRUGO;
		break;
	}
	return rc;
}

static umode_t beiscsi_ini_get_attr_visibility(void *data, int type)
{
	umode_t rc = 0;

	switch (type) {
	case ISCSI_BOOT_INI_INITIATOR_NAME:
		rc = S_IRUGO;
		break;
	}
	return rc;
}

static umode_t beiscsi_eth_get_attr_visibility(void *data, int type)
{
	umode_t rc = 0;

	switch (type) {
	case ISCSI_BOOT_ETH_FLAGS:
	case ISCSI_BOOT_ETH_MAC:
	case ISCSI_BOOT_ETH_INDEX:
		rc = S_IRUGO;
		break;
	}
	return rc;
}

static void beiscsi_boot_kobj_release(void *data)
{
	struct beiscsi_hba *phba = data;

	scsi_host_put(phba->shost);
}

static int beiscsi_boot_create_kset(struct beiscsi_hba *phba)
{
	struct boot_struct *bs = &phba->boot_struct;
	struct iscsi_boot_kobj *boot_kobj;

	if (bs->boot_kset) {
		__beiscsi_log(phba, KERN_ERR,
			      "BM_%d: boot_kset already created\n");
		return 0;
	}

	bs->boot_kset = iscsi_boot_create_host_kset(phba->shost->host_no);
	if (!bs->boot_kset) {
		__beiscsi_log(phba, KERN_ERR,
			      "BM_%d: boot_kset alloc failed\n");
		return -ENOMEM;
	}

	/* get shost ref because the show function will refer phba */
	if (!scsi_host_get(phba->shost))
		goto free_kset;

	boot_kobj = iscsi_boot_create_target(bs->boot_kset, 0, phba,
					     beiscsi_show_boot_tgt_info,
					     beiscsi_tgt_get_attr_visibility,
					     beiscsi_boot_kobj_release);
	if (!boot_kobj)
		goto put_shost;

	if (!scsi_host_get(phba->shost))
		goto free_kset;

	boot_kobj = iscsi_boot_create_initiator(bs->boot_kset, 0, phba,
						beiscsi_show_boot_ini_info,
						beiscsi_ini_get_attr_visibility,
						beiscsi_boot_kobj_release);
	if (!boot_kobj)
		goto put_shost;

	if (!scsi_host_get(phba->shost))
		goto free_kset;

	boot_kobj = iscsi_boot_create_ethernet(bs->boot_kset, 0, phba,
					       beiscsi_show_boot_eth_info,
					       beiscsi_eth_get_attr_visibility,
					       beiscsi_boot_kobj_release);
	if (!boot_kobj)
		goto put_shost;

	return 0;

put_shost:
	scsi_host_put(phba->shost);
free_kset:
	iscsi_boot_destroy_kset(bs->boot_kset);
	bs->boot_kset = NULL;
	return -ENOMEM;
}

static void beiscsi_boot_work(struct work_struct *work)
{
	struct beiscsi_hba *phba =
		container_of(work, struct beiscsi_hba, boot_work);
	struct boot_struct *bs = &phba->boot_struct;
	unsigned int tag = 0;

	if (!beiscsi_hba_is_online(phba))
		return;

	beiscsi_log(phba, KERN_INFO,
		    BEISCSI_LOG_CONFIG | BEISCSI_LOG_MBOX,
		    "BM_%d : %s action %d\n",
		    __func__, phba->boot_struct.action);

	switch (phba->boot_struct.action) {
	case BEISCSI_BOOT_REOPEN_SESS:
		tag = beiscsi_boot_reopen_sess(phba);
		break;
	case BEISCSI_BOOT_GET_SHANDLE:
		tag = __beiscsi_boot_get_shandle(phba, 1);
		break;
	case BEISCSI_BOOT_GET_SINFO:
		tag = beiscsi_boot_get_sinfo(phba);
		break;
	case BEISCSI_BOOT_LOGOUT_SESS:
		tag = beiscsi_boot_logout_sess(phba);
		break;
	case BEISCSI_BOOT_CREATE_KSET:
		beiscsi_boot_create_kset(phba);
		/**
		 * updated boot_kset is made visible to all before
		 * ending the boot work.
		 */
		mb();
		clear_bit(BEISCSI_HBA_BOOT_WORK, &phba->state);
		return;
	}
	if (!tag) {
		if (bs->retry--)
			schedule_work(&phba->boot_work);
		else
			clear_bit(BEISCSI_HBA_BOOT_WORK, &phba->state);
	}
}

static void beiscsi_eqd_update_work(struct work_struct *work)
{
	struct hwi_context_memory *phwi_context;
	struct be_set_eqd set_eqd[MAX_CPUS];
	struct hwi_controller *phwi_ctrlr;
	struct be_eq_obj *pbe_eq;
	struct beiscsi_hba *phba;
	unsigned int pps, delta;
	struct be_aic_obj *aic;
	int eqd, i, num = 0;
	unsigned long now;

	phba = container_of(work, struct beiscsi_hba, eqd_update.work);
	if (!beiscsi_hba_is_online(phba))
		return;

	phwi_ctrlr = phba->phwi_ctrlr;
	phwi_context = phwi_ctrlr->phwi_ctxt;

	for (i = 0; i <= phba->num_cpus; i++) {
		aic = &phba->aic_obj[i];
		pbe_eq = &phwi_context->be_eq[i];
		now = jiffies;
		if (!aic->jiffies || time_before(now, aic->jiffies) ||
		    pbe_eq->cq_count < aic->eq_prev) {
			aic->jiffies = now;
			aic->eq_prev = pbe_eq->cq_count;
			continue;
		}
		delta = jiffies_to_msecs(now - aic->jiffies);
		pps = (((u32)(pbe_eq->cq_count - aic->eq_prev) * 1000) / delta);
		eqd = (pps / 1500) << 2;

		if (eqd < 8)
			eqd = 0;
		eqd = min_t(u32, eqd, phwi_context->max_eqd);
		eqd = max_t(u32, eqd, phwi_context->min_eqd);

		aic->jiffies = now;
		aic->eq_prev = pbe_eq->cq_count;

		if (eqd != aic->prev_eqd) {
			set_eqd[num].delay_multiplier = (eqd * 65)/100;
			set_eqd[num].eq_id = pbe_eq->q.id;
			aic->prev_eqd = eqd;
			num++;
		}
	}
	if (num)
		/* completion of this is ignored */
		beiscsi_modify_eq_delay(phba, set_eqd, num);

	schedule_delayed_work(&phba->eqd_update,
			      msecs_to_jiffies(BEISCSI_EQD_UPDATE_INTERVAL));
}

static void beiscsi_msix_enable(struct beiscsi_hba *phba)
{
	int i, status;

	for (i = 0; i <= phba->num_cpus; i++)
		phba->msix_entries[i].entry = i;

	status = pci_enable_msix_range(phba->pcidev, phba->msix_entries,
				       phba->num_cpus + 1, phba->num_cpus + 1);
	if (status > 0)
		phba->msix_enabled = true;
}

static void beiscsi_hw_tpe_check(unsigned long ptr)
{
	struct beiscsi_hba *phba;
	u32 wait;

	phba = (struct beiscsi_hba *)ptr;
	/* if not TPE, do nothing */
	if (!beiscsi_detect_tpe(phba))
		return;

	/* wait default 4000ms before recovering */
	wait = 4000;
	if (phba->ue2rp > BEISCSI_UE_DETECT_INTERVAL)
		wait = phba->ue2rp - BEISCSI_UE_DETECT_INTERVAL;
	queue_delayed_work(phba->wq, &phba->recover_port,
			   msecs_to_jiffies(wait));
}

static void beiscsi_hw_health_check(unsigned long ptr)
{
	struct beiscsi_hba *phba;

	phba = (struct beiscsi_hba *)ptr;
	beiscsi_detect_ue(phba);
	if (beiscsi_detect_ue(phba)) {
		__beiscsi_log(phba, KERN_ERR,
			      "BM_%d : port in error: %lx\n", phba->state);
		/* sessions are no longer valid, so first fail the sessions */
		queue_work(phba->wq, &phba->sess_work);

		/* detect UER supported */
		if (!test_bit(BEISCSI_HBA_UER_SUPP, &phba->state))
			return;
		/* modify this timer to check TPE */
		phba->hw_check.function = beiscsi_hw_tpe_check;
	}

	mod_timer(&phba->hw_check,
		  jiffies + msecs_to_jiffies(BEISCSI_UE_DETECT_INTERVAL));
}

/*
 * beiscsi_enable_port()- Enables the disabled port.
 * Only port resources freed in disable function are reallocated.
 * This is called in HBA error handling path.
 *
 * @phba: Instance of driver private structure
 *
 **/
static int beiscsi_enable_port(struct beiscsi_hba *phba)
{
	struct hwi_context_memory *phwi_context;
	struct hwi_controller *phwi_ctrlr;
	struct be_eq_obj *pbe_eq;
	int ret, i;

	if (test_bit(BEISCSI_HBA_ONLINE, &phba->state)) {
		__beiscsi_log(phba, KERN_ERR,
			      "BM_%d : %s : port is online %lx\n",
			      __func__, phba->state);
		return 0;
	}

	ret = beiscsi_init_sliport(phba);
	if (ret)
		return ret;

	if (enable_msix)
		find_num_cpus(phba);
	else
		phba->num_cpus = 1;
	if (enable_msix) {
		beiscsi_msix_enable(phba);
		if (!phba->msix_enabled)
			phba->num_cpus = 1;
	}

	beiscsi_get_params(phba);
	/* Re-enable UER. If different TPE occurs then it is recoverable. */
	beiscsi_set_uer_feature(phba);

	phba->shost->max_id = phba->params.cxns_per_ctrl;
	phba->shost->can_queue = phba->params.ios_per_ctrl;
	ret = hwi_init_controller(phba);
	if (ret) {
		__beiscsi_log(phba, KERN_ERR,
			      "BM_%d : init controller failed %d\n", ret);
		goto disable_msix;
	}

	for (i = 0; i < MAX_MCC_CMD; i++) {
		init_waitqueue_head(&phba->ctrl.mcc_wait[i + 1]);
		phba->ctrl.mcc_tag[i] = i + 1;
		phba->ctrl.mcc_tag_status[i + 1] = 0;
		phba->ctrl.mcc_tag_available++;
	}

	phwi_ctrlr = phba->phwi_ctrlr;
	phwi_context = phwi_ctrlr->phwi_ctxt;
	for (i = 0; i < phba->num_cpus; i++) {
		pbe_eq = &phwi_context->be_eq[i];
		irq_poll_init(&pbe_eq->iopoll, be_iopoll_budget, be_iopoll);
	}

	i = (phba->msix_enabled) ? i : 0;
	/* Work item for MCC handling */
	pbe_eq = &phwi_context->be_eq[i];
	INIT_WORK(&pbe_eq->mcc_work, beiscsi_mcc_work);

	ret = beiscsi_init_irqs(phba);
	if (ret < 0) {
		__beiscsi_log(phba, KERN_ERR,
			      "BM_%d : setup IRQs failed %d\n", ret);
		goto cleanup_port;
	}
	hwi_enable_intr(phba);
	/* port operational: clear all error bits */
	set_bit(BEISCSI_HBA_ONLINE, &phba->state);
	__beiscsi_log(phba, KERN_INFO,
		      "BM_%d : port online: 0x%lx\n", phba->state);

	/* start hw_check timer and eqd_update work */
	schedule_delayed_work(&phba->eqd_update,
			      msecs_to_jiffies(BEISCSI_EQD_UPDATE_INTERVAL));

	/**
	 * Timer function gets modified for TPE detection.
	 * Always reinit to do health check first.
	 */
	phba->hw_check.function = beiscsi_hw_health_check;
	mod_timer(&phba->hw_check,
		  jiffies + msecs_to_jiffies(BEISCSI_UE_DETECT_INTERVAL));
	return 0;

cleanup_port:
	for (i = 0; i < phba->num_cpus; i++) {
		pbe_eq = &phwi_context->be_eq[i];
		irq_poll_disable(&pbe_eq->iopoll);
	}
	hwi_cleanup_port(phba);

disable_msix:
	if (phba->msix_enabled)
		pci_disable_msix(phba->pcidev);

	return ret;
}

/*
 * beiscsi_disable_port()- Disable port and cleanup driver resources.
 * This is called in HBA error handling and driver removal.
 * @phba: Instance Priv structure
 * @unload: indicate driver is unloading
 *
 * Free the OS and HW resources held by the driver
 **/
static void beiscsi_disable_port(struct beiscsi_hba *phba, int unload)
{
	struct hwi_context_memory *phwi_context;
	struct hwi_controller *phwi_ctrlr;
	struct be_eq_obj *pbe_eq;
	unsigned int i, msix_vec;

	if (!test_and_clear_bit(BEISCSI_HBA_ONLINE, &phba->state))
		return;

	phwi_ctrlr = phba->phwi_ctrlr;
	phwi_context = phwi_ctrlr->phwi_ctxt;
	hwi_disable_intr(phba);
	if (phba->msix_enabled) {
		for (i = 0; i <= phba->num_cpus; i++) {
			msix_vec = phba->msix_entries[i].vector;
			free_irq(msix_vec, &phwi_context->be_eq[i]);
			kfree(phba->msi_name[i]);
		}
	} else
		if (phba->pcidev->irq)
			free_irq(phba->pcidev->irq, phba);
	pci_disable_msix(phba->pcidev);

	for (i = 0; i < phba->num_cpus; i++) {
		pbe_eq = &phwi_context->be_eq[i];
		irq_poll_disable(&pbe_eq->iopoll);
	}
	cancel_delayed_work_sync(&phba->eqd_update);
	cancel_work_sync(&phba->boot_work);
	/* WQ might be running cancel queued mcc_work if we are not exiting */
	if (!unload && beiscsi_hba_in_error(phba)) {
		pbe_eq = &phwi_context->be_eq[i];
		cancel_work_sync(&pbe_eq->mcc_work);
	}
	hwi_cleanup_port(phba);
}

static void beiscsi_sess_work(struct work_struct *work)
{
	struct beiscsi_hba *phba;

	phba = container_of(work, struct beiscsi_hba, sess_work);
	/*
	 * This work gets scheduled only in case of HBA error.
	 * Old sessions are gone so need to be re-established.
	 * iscsi_session_failure needs process context hence this work.
	 */
	iscsi_host_for_each_session(phba->shost, beiscsi_session_fail);
}

static void beiscsi_recover_port(struct work_struct *work)
{
	struct beiscsi_hba *phba;

	phba = container_of(work, struct beiscsi_hba, recover_port.work);
	beiscsi_disable_port(phba, 0);
	beiscsi_enable_port(phba);
}

static pci_ers_result_t beiscsi_eeh_err_detected(struct pci_dev *pdev,
		pci_channel_state_t state)
{
	struct beiscsi_hba *phba = NULL;

	phba = (struct beiscsi_hba *)pci_get_drvdata(pdev);
	set_bit(BEISCSI_HBA_PCI_ERR, &phba->state);

	beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
		    "BM_%d : EEH error detected\n");

	/* first stop UE detection when PCI error detected */
	del_timer_sync(&phba->hw_check);
	cancel_delayed_work_sync(&phba->recover_port);

	/* sessions are no longer valid, so first fail the sessions */
	iscsi_host_for_each_session(phba->shost, beiscsi_session_fail);
	beiscsi_disable_port(phba, 0);

	if (state == pci_channel_io_perm_failure) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : EEH : State PERM Failure");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	pci_disable_device(pdev);

	/* The error could cause the FW to trigger a flash debug dump.
	 * Resetting the card while flash dump is in progress
	 * can cause it not to recover; wait for it to finish.
	 * Wait only for first function as it is needed only once per
	 * adapter.
	 **/
	if (pdev->devfn == 0)
		ssleep(30);

	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t beiscsi_eeh_reset(struct pci_dev *pdev)
{
	struct beiscsi_hba *phba = NULL;
	int status = 0;

	phba = (struct beiscsi_hba *)pci_get_drvdata(pdev);

	beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
		    "BM_%d : EEH Reset\n");

	status = pci_enable_device(pdev);
	if (status)
		return PCI_ERS_RESULT_DISCONNECT;

	pci_set_master(pdev);
	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	status = beiscsi_check_fw_rdy(phba);
	if (status) {
		beiscsi_log(phba, KERN_WARNING, BEISCSI_LOG_INIT,
			    "BM_%d : EEH Reset Completed\n");
	} else {
		beiscsi_log(phba, KERN_WARNING, BEISCSI_LOG_INIT,
			    "BM_%d : EEH Reset Completion Failure\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	pci_cleanup_aer_uncorrect_error_status(pdev);
	return PCI_ERS_RESULT_RECOVERED;
}

static void beiscsi_eeh_resume(struct pci_dev *pdev)
{
	struct beiscsi_hba *phba;
	int ret;

	phba = (struct beiscsi_hba *)pci_get_drvdata(pdev);
	pci_save_state(pdev);

	ret = beiscsi_enable_port(phba);
	if (ret)
		__beiscsi_log(phba, KERN_ERR,
			      "BM_%d : AER EEH resume failed\n");
}

static int beiscsi_dev_probe(struct pci_dev *pcidev,
			     const struct pci_device_id *id)
{
	struct beiscsi_hba *phba = NULL;
	struct hwi_controller *phwi_ctrlr;
	struct hwi_context_memory *phwi_context;
	struct be_eq_obj *pbe_eq;
	unsigned int s_handle;
	int ret = 0, i;

	ret = beiscsi_enable_pci(pcidev);
	if (ret < 0) {
		dev_err(&pcidev->dev,
			"beiscsi_dev_probe - Failed to enable pci device\n");
		return ret;
	}

	phba = beiscsi_hba_alloc(pcidev);
	if (!phba) {
		dev_err(&pcidev->dev,
			"beiscsi_dev_probe - Failed in beiscsi_hba_alloc\n");
		goto disable_pci;
	}

	/* Enable EEH reporting */
	ret = pci_enable_pcie_error_reporting(pcidev);
	if (ret)
		beiscsi_log(phba, KERN_WARNING, BEISCSI_LOG_INIT,
			    "BM_%d : PCIe Error Reporting "
			    "Enabling Failed\n");

	pci_save_state(pcidev);

	/* Initialize Driver configuration Paramters */
	beiscsi_hba_attrs_init(phba);

	phba->mac_addr_set = false;

	switch (pcidev->device) {
	case BE_DEVICE_ID1:
	case OC_DEVICE_ID1:
	case OC_DEVICE_ID2:
		phba->generation = BE_GEN2;
		phba->iotask_fn = beiscsi_iotask;
		break;
	case BE_DEVICE_ID2:
	case OC_DEVICE_ID3:
		phba->generation = BE_GEN3;
		phba->iotask_fn = beiscsi_iotask;
		break;
	case OC_SKH_ID1:
		phba->generation = BE_GEN4;
		phba->iotask_fn = beiscsi_iotask_v2;
		break;
	default:
		phba->generation = 0;
	}

	ret = be_ctrl_init(phba, pcidev);
	if (ret) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : be_ctrl_init failed\n");
		goto hba_free;
	}

	ret = beiscsi_init_sliport(phba);
	if (ret)
		goto hba_free;

	spin_lock_init(&phba->io_sgl_lock);
	spin_lock_init(&phba->mgmt_sgl_lock);
	spin_lock_init(&phba->async_pdu_lock);
	ret = beiscsi_get_fw_config(&phba->ctrl, phba);
	if (ret != 0) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : Error getting fw config\n");
		goto free_port;
	}
	beiscsi_get_port_name(&phba->ctrl, phba);
	beiscsi_get_params(phba);
	beiscsi_set_uer_feature(phba);

	if (enable_msix)
		find_num_cpus(phba);
	else
		phba->num_cpus = 1;

	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
		    "BM_%d : num_cpus = %d\n",
		    phba->num_cpus);

	if (enable_msix) {
		beiscsi_msix_enable(phba);
		if (!phba->msix_enabled)
			phba->num_cpus = 1;
	}

	phba->shost->max_id = phba->params.cxns_per_ctrl;
	phba->shost->can_queue = phba->params.ios_per_ctrl;
	ret = beiscsi_init_port(phba);
	if (ret < 0) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : beiscsi_dev_probe-"
			    "Failed in beiscsi_init_port\n");
		goto free_port;
	}

	for (i = 0; i < MAX_MCC_CMD; i++) {
		init_waitqueue_head(&phba->ctrl.mcc_wait[i + 1]);
		phba->ctrl.mcc_tag[i] = i + 1;
		phba->ctrl.mcc_tag_status[i + 1] = 0;
		phba->ctrl.mcc_tag_available++;
		memset(&phba->ctrl.ptag_state[i].tag_mem_state, 0,
		       sizeof(struct be_dma_mem));
	}

	phba->ctrl.mcc_alloc_index = phba->ctrl.mcc_free_index = 0;

	snprintf(phba->wq_name, sizeof(phba->wq_name), "beiscsi_%02x_wq",
		 phba->shost->host_no);
	phba->wq = alloc_workqueue("%s", WQ_MEM_RECLAIM, 1, phba->wq_name);
	if (!phba->wq) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : beiscsi_dev_probe-"
			    "Failed to allocate work queue\n");
		goto free_twq;
	}

	INIT_DELAYED_WORK(&phba->eqd_update, beiscsi_eqd_update_work);

	phwi_ctrlr = phba->phwi_ctrlr;
	phwi_context = phwi_ctrlr->phwi_ctxt;

	for (i = 0; i < phba->num_cpus; i++) {
		pbe_eq = &phwi_context->be_eq[i];
		irq_poll_init(&pbe_eq->iopoll, be_iopoll_budget, be_iopoll);
	}

	i = (phba->msix_enabled) ? i : 0;
	/* Work item for MCC handling */
	pbe_eq = &phwi_context->be_eq[i];
	INIT_WORK(&pbe_eq->mcc_work, beiscsi_mcc_work);

	ret = beiscsi_init_irqs(phba);
	if (ret < 0) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BM_%d : beiscsi_dev_probe-"
			    "Failed to beiscsi_init_irqs\n");
		goto free_blkenbld;
	}
	hwi_enable_intr(phba);

	ret = iscsi_host_add(phba->shost, &phba->pcidev->dev);
	if (ret)
		goto free_blkenbld;

	/* set online bit after port is operational */
	set_bit(BEISCSI_HBA_ONLINE, &phba->state);
	__beiscsi_log(phba, KERN_INFO,
		      "BM_%d : port online: 0x%lx\n", phba->state);

	INIT_WORK(&phba->boot_work, beiscsi_boot_work);
	ret = beiscsi_boot_get_shandle(phba, &s_handle);
	if (ret > 0) {
		beiscsi_start_boot_work(phba, s_handle);
		/**
		 * Set this bit after starting the work to let
		 * probe handle it first.
		 * ASYNC event can too schedule this work.
		 */
		set_bit(BEISCSI_HBA_BOOT_FOUND, &phba->state);
	}

	beiscsi_iface_create_default(phba);
	schedule_delayed_work(&phba->eqd_update,
			      msecs_to_jiffies(BEISCSI_EQD_UPDATE_INTERVAL));

	INIT_WORK(&phba->sess_work, beiscsi_sess_work);
	INIT_DELAYED_WORK(&phba->recover_port, beiscsi_recover_port);
	/**
	 * Start UE detection here. UE before this will cause stall in probe
	 * and eventually fail the probe.
	 */
	init_timer(&phba->hw_check);
	phba->hw_check.function = beiscsi_hw_health_check;
	phba->hw_check.data = (unsigned long)phba;
	mod_timer(&phba->hw_check,
		  jiffies + msecs_to_jiffies(BEISCSI_UE_DETECT_INTERVAL));
	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
		    "\n\n\n BM_%d : SUCCESS - DRIVER LOADED\n\n\n");
	return 0;

free_blkenbld:
	destroy_workqueue(phba->wq);
	for (i = 0; i < phba->num_cpus; i++) {
		pbe_eq = &phwi_context->be_eq[i];
		irq_poll_disable(&pbe_eq->iopoll);
	}
free_twq:
	hwi_cleanup_port(phba);
	beiscsi_cleanup_port(phba);
	beiscsi_free_mem(phba);
free_port:
	pci_free_consistent(phba->pcidev,
			    phba->ctrl.mbox_mem_alloced.size,
			    phba->ctrl.mbox_mem_alloced.va,
			   phba->ctrl.mbox_mem_alloced.dma);
	beiscsi_unmap_pci_function(phba);
hba_free:
	if (phba->msix_enabled)
		pci_disable_msix(phba->pcidev);
	pci_dev_put(phba->pcidev);
	iscsi_host_free(phba->shost);
	pci_set_drvdata(pcidev, NULL);
disable_pci:
	pci_release_regions(pcidev);
	pci_disable_device(pcidev);
	return ret;
}

static void beiscsi_remove(struct pci_dev *pcidev)
{
	struct beiscsi_hba *phba = NULL;

	phba = pci_get_drvdata(pcidev);
	if (!phba) {
		dev_err(&pcidev->dev, "beiscsi_remove called with no phba\n");
		return;
	}

	/* first stop UE detection before unloading */
	del_timer_sync(&phba->hw_check);
	cancel_delayed_work_sync(&phba->recover_port);
	cancel_work_sync(&phba->sess_work);

	beiscsi_iface_destroy_default(phba);
	iscsi_host_remove(phba->shost);
	beiscsi_disable_port(phba, 1);

	/* after cancelling boot_work */
	iscsi_boot_destroy_kset(phba->boot_struct.boot_kset);

	/* free all resources */
	destroy_workqueue(phba->wq);
	beiscsi_cleanup_port(phba);
	beiscsi_free_mem(phba);

	/* ctrl uninit */
	beiscsi_unmap_pci_function(phba);
	pci_free_consistent(phba->pcidev,
			    phba->ctrl.mbox_mem_alloced.size,
			    phba->ctrl.mbox_mem_alloced.va,
			    phba->ctrl.mbox_mem_alloced.dma);

	pci_dev_put(phba->pcidev);
	iscsi_host_free(phba->shost);
	pci_disable_pcie_error_reporting(pcidev);
	pci_set_drvdata(pcidev, NULL);
	pci_release_regions(pcidev);
	pci_disable_device(pcidev);
}


static struct pci_error_handlers beiscsi_eeh_handlers = {
	.error_detected = beiscsi_eeh_err_detected,
	.slot_reset = beiscsi_eeh_reset,
	.resume = beiscsi_eeh_resume,
};

struct iscsi_transport beiscsi_iscsi_transport = {
	.owner = THIS_MODULE,
	.name = DRV_NAME,
	.caps = CAP_RECOVERY_L0 | CAP_HDRDGST | CAP_TEXT_NEGO |
		CAP_MULTI_R2T | CAP_DATADGST | CAP_DATA_PATH_OFFLOAD,
	.create_session = beiscsi_session_create,
	.destroy_session = beiscsi_session_destroy,
	.create_conn = beiscsi_conn_create,
	.bind_conn = beiscsi_conn_bind,
	.destroy_conn = iscsi_conn_teardown,
	.attr_is_visible = beiscsi_attr_is_visible,
	.set_iface_param = beiscsi_iface_set_param,
	.get_iface_param = beiscsi_iface_get_param,
	.set_param = beiscsi_set_param,
	.get_conn_param = iscsi_conn_get_param,
	.get_session_param = iscsi_session_get_param,
	.get_host_param = beiscsi_get_host_param,
	.start_conn = beiscsi_conn_start,
	.stop_conn = iscsi_conn_stop,
	.send_pdu = iscsi_conn_send_pdu,
	.xmit_task = beiscsi_task_xmit,
	.cleanup_task = beiscsi_cleanup_task,
	.alloc_pdu = beiscsi_alloc_pdu,
	.parse_pdu_itt = beiscsi_parse_pdu,
	.get_stats = beiscsi_conn_get_stats,
	.get_ep_param = beiscsi_ep_get_param,
	.ep_connect = beiscsi_ep_connect,
	.ep_poll = beiscsi_ep_poll,
	.ep_disconnect = beiscsi_ep_disconnect,
	.session_recovery_timedout = iscsi_session_recovery_timedout,
	.bsg_request = beiscsi_bsg_request,
};

static struct pci_driver beiscsi_pci_driver = {
	.name = DRV_NAME,
	.probe = beiscsi_dev_probe,
	.remove = beiscsi_remove,
	.id_table = beiscsi_pci_id_table,
	.err_handler = &beiscsi_eeh_handlers
};

static int __init beiscsi_module_init(void)
{
	int ret;

	beiscsi_scsi_transport =
			iscsi_register_transport(&beiscsi_iscsi_transport);
	if (!beiscsi_scsi_transport) {
		printk(KERN_ERR
		       "beiscsi_module_init - Unable to  register beiscsi transport.\n");
		return -ENOMEM;
	}
	printk(KERN_INFO "In beiscsi_module_init, tt=%p\n",
	       &beiscsi_iscsi_transport);

	ret = pci_register_driver(&beiscsi_pci_driver);
	if (ret) {
		printk(KERN_ERR
		       "beiscsi_module_init - Unable to  register beiscsi pci driver.\n");
		goto unregister_iscsi_transport;
	}
	return 0;

unregister_iscsi_transport:
	iscsi_unregister_transport(&beiscsi_iscsi_transport);
	return ret;
}

static void __exit beiscsi_module_exit(void)
{
	pci_unregister_driver(&beiscsi_pci_driver);
	iscsi_unregister_transport(&beiscsi_iscsi_transport);
}

module_init(beiscsi_module_init);
module_exit(beiscsi_module_exit);
