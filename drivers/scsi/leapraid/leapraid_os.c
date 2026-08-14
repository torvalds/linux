// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 LeapIO Tech Inc.
 *
 * LeapRAID storage and RAID controller driver.
 */
#include <linux/module.h>

#include "leapraid_func.h"
#include "leapraid.h"

LIST_HEAD(leapraid_adapter_list);
DEFINE_SPINLOCK(leapraid_adapter_lock);

MODULE_AUTHOR(LEAPRAID_AUTHOR);
MODULE_DESCRIPTION(LEAPRAID_DESCRIPTION);
MODULE_LICENSE("GPL");
MODULE_VERSION(LEAPRAID_DRIVER_VERSION);

static atomic_t leapraid_ids = ATOMIC_INIT(0);

static int open_pcie_trace = 1;
module_param(open_pcie_trace, int, 0644);
MODULE_PARM_DESC(open_pcie_trace, "Default=1(open)/0(close)");

static int enable_mp = 1;
module_param(enable_mp, int, 0444);
MODULE_PARM_DESC(enable_mp,
		 "Enable multipath on target device. default=1(enable)");

static inline void leapraid_get_sense_data(char *sense,
					   struct sense_info *data)
{
	bool desc_format = (sense[0] & SCSI_SENSE_RESPONSE_CODE_MASK) >=
			    DESC_FORMAT_THRESHOLD;

	if (desc_format) {
		data->sense_key = sense[1] & SENSE_KEY_MASK;
		data->asc = sense[2];
		data->ascq = sense[3];
	} else {
		data->sense_key = sense[2] & SENSE_KEY_MASK;
		data->asc = sense[12];
		data->ascq = sense[13];
	}
}

static struct leapraid_adapter *pdev_to_adapter(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);

	if (!shost)
		return NULL;

	return shost_priv(shost);
}

void leapraid_set_tm_flg(struct leapraid_adapter *adapter, u16 hdl)
{
	struct leapraid_sdev_priv *sdev_priv;
	struct scsi_device *sdev;
	bool skip = false;

	/* Iterate over all devices. */
	shost_for_each_device(sdev, adapter->shost) {
		if (skip)
			continue;

		sdev_priv = sdev->hostdata;
		if (!sdev_priv)
			continue;

		if (sdev_priv->starget_priv->hdl == hdl) {
			sdev_priv->starget_priv->tm_busy = 1;
			skip = true;
		}
	}
}

void leapraid_clear_tm_flg(struct leapraid_adapter *adapter, u16 hdl)
{
	struct leapraid_sdev_priv *sdev_priv;
	struct scsi_device *sdev;
	bool skip = false;

	/* Iterate over all devices. */
	shost_for_each_device(sdev, adapter->shost) {
		if (skip)
			continue;

		sdev_priv = sdev->hostdata;
		if (!sdev_priv)
			continue;

		if (sdev_priv->starget_priv->hdl == hdl) {
			sdev_priv->starget_priv->tm_busy = 0;
			skip = true;
		}
	}
}

static int leapraid_tm_cmd_map_status(struct leapraid_adapter *adapter,
				      uint channel,
				      uint id,
				      uint lun,
				      u8 type,
				      u16 taskid_task)
{
	int rc = FAILED;

	if (taskid_task <= adapter->shost->can_queue) {
		switch (type) {
		case LEAPRAID_TM_TASKTYPE_ABRT_TASK_SET:
		case LEAPRAID_TM_TASKTYPE_LOGICAL_UNIT_RESET:
			if (!leapraid_scmd_find_by_lun(adapter, id, lun,
						       channel))
				rc = SUCCESS;
			break;
		case LEAPRAID_TM_TASKTYPE_TARGET_RESET:
			if (!leapraid_scmd_find_by_tgt(adapter, id, channel))
				rc = SUCCESS;
			break;
		default:
			rc = SUCCESS;
		}
	}

	if (taskid_task == adapter->driver_cmds.ctl_cmd.hp_taskid &&
	    (adapter->driver_cmds.ctl_cmd.status &
	     LEAPRAID_CMD_DONE ||
	     adapter->driver_cmds.ctl_cmd.status &
	     LEAPRAID_CMD_NOT_USED))
		rc = SUCCESS;

	return rc;
}

static int leapraid_tm_post_processing(struct leapraid_adapter *adapter,
				       u16 hdl, uint channel, uint id,
				       uint lun, u8 type, u16 taskid_task)
{
	int rc;

	rc = leapraid_tm_cmd_map_status(adapter, channel, id, lun,
					type, taskid_task);
	if (rc == SUCCESS)
		return rc;

	leapraid_mask_int(adapter);
	leapraid_sync_irqs(adapter, true);
	leapraid_unmask_int(adapter);

	return leapraid_tm_cmd_map_status(adapter, channel, id, lun, type,
					  taskid_task);
}

static void leapraid_build_tm_req(struct leapraid_scsi_tm_req *scsi_tm_req,
				  u16 hdl, uint lun, u8 type, u8 tr_method,
				  u16 target_taskid)
{
	memset(scsi_tm_req, 0, sizeof(*scsi_tm_req));
	scsi_tm_req->func = LEAPRAID_FUNC_SCSI_TMF;
	scsi_tm_req->dev_hdl = cpu_to_le16(hdl);
	scsi_tm_req->task_type = type;
	scsi_tm_req->msg_flg = tr_method;
	if (type == LEAPRAID_TM_TASKTYPE_ABORT_TASK ||
	    type == LEAPRAID_TM_TASKTYPE_QUERY_TASK)
		scsi_tm_req->task_mid = cpu_to_le16(target_taskid);
	int_to_scsilun(lun, (struct scsi_lun *)scsi_tm_req->lun);
}

int leapraid_issue_tm(struct leapraid_adapter *adapter, u16 hdl, uint channel,
		      uint id, uint lun, u8 type,
		      u16 target_taskid, u8 tr_method)
{
	struct leapraid_scsi_tm_req *scsi_tm_req;
	struct leapraid_scsiio_req *scsiio_req;
	struct leapraid_io_req_tracker *io_req_tracker = NULL;
	u16 msix_task;
	u16 taskid;
	bool issue_reset = false;
	u32 db;
	int rc;

	lockdep_assert_held(&adapter->driver_cmds.tm_cmd.mutex);

	if (adapter->access_ctrl.shost_recovering ||
	    adapter->access_ctrl.host_removing ||
	    adapter->access_ctrl.pcie_recovering) {
		dev_info(&adapter->pdev->dev,
			 "%s %s: Host is recovering, skip tm command!\n",
			 __func__, adapter->adapter_attr.name);
		return FAILED;
	}

	db = leapraid_readl(&adapter->iomem_base->db);
	if (db & LEAPRAID_DB_USED) {
		dev_info(&adapter->pdev->dev,
			 "%s Unexpected db status, issuing hard reset!\n",
			 adapter->adapter_attr.name);
		dev_info(&adapter->pdev->dev, "%s:%d: call hard_reset\n",
			 __func__, __LINE__);
		rc = leapraid_hard_reset_handler(adapter, FULL_RESET);
		return !rc ? SUCCESS : FAILED;
	}

	if ((db & LEAPRAID_DB_MASK) == LEAPRAID_DB_FAULT) {
		dev_info(&adapter->pdev->dev, "%s:%d: call hard_reset\n",
			 __func__, __LINE__);
		rc = leapraid_hard_reset_handler(adapter, FULL_RESET);
		return !rc ? SUCCESS : FAILED;
	}

	if (type == LEAPRAID_TM_TASKTYPE_ABORT_TASK)
		io_req_tracker =
			leapraid_get_io_tracker_from_taskid(adapter,
							    target_taskid);

	adapter->driver_cmds.tm_cmd.status = LEAPRAID_CMD_PENDING;
	scsi_tm_req =
		leapraid_get_task_desc(adapter,
				       adapter->driver_cmds.tm_cmd.hp_taskid);
	leapraid_build_tm_req(scsi_tm_req, hdl, lun, type, tr_method,
			      target_taskid);
	memset(&adapter->driver_cmds.tm_cmd.reply, 0,
	       sizeof(struct leapraid_scsi_tm_rep));
	leapraid_set_tm_flg(adapter, hdl);
	init_completion(&adapter->driver_cmds.tm_cmd.done);
	if (type == LEAPRAID_TM_TASKTYPE_ABORT_TASK &&
	    io_req_tracker &&
	    io_req_tracker->msix_io < adapter->adapter_attr.rq_cnt)
		msix_task = io_req_tracker->msix_io;
	else
		msix_task = 0;
	taskid = adapter->driver_cmds.tm_cmd.hp_taskid;
	leapraid_fire_hpr_task(adapter, taskid, msix_task);
	wait_for_completion_timeout(&adapter->driver_cmds.tm_cmd.done,
				    LEAPRAID_TM_CMD_TIMEOUT * HZ);
	if (!(adapter->driver_cmds.tm_cmd.status & LEAPRAID_CMD_DONE)) {
		dev_err(&adapter->pdev->dev,
			"%s: TM cmd timeout, status=0x%x\n",
			__func__, adapter->driver_cmds.tm_cmd.status);
		leapraid_log_req_context(adapter, taskid, scsi_tm_req);
		issue_reset =
			leapraid_check_reset(
				adapter->driver_cmds.tm_cmd.status);
		if (issue_reset) {
			dev_info(&adapter->pdev->dev,
				 "%s:%d: call hard_reset\n",
				 __func__, __LINE__);
			rc = leapraid_hard_reset_handler(adapter, FULL_RESET);
			rc = !rc ? SUCCESS : FAILED;
			goto out_cleanup;
		}
	}

	leapraid_sync_irqs(adapter, false);

	switch (type) {
	case LEAPRAID_TM_TASKTYPE_TARGET_RESET:
	case LEAPRAID_TM_TASKTYPE_ABRT_TASK_SET:
	case LEAPRAID_TM_TASKTYPE_LOGICAL_UNIT_RESET:
		rc = leapraid_tm_post_processing(adapter, hdl, channel, id,
						 lun, type, target_taskid);
		break;
	case LEAPRAID_TM_TASKTYPE_ABORT_TASK:
		rc = SUCCESS;
		scsiio_req = leapraid_get_task_desc(adapter, target_taskid);
		if (le16_to_cpu(scsiio_req->dev_hdl) != hdl)
			break;
		dev_err(&adapter->pdev->dev, "%s: Abort failed, hdl=0x%04x\n",
			adapter->adapter_attr.name, hdl);
		rc = FAILED;
		break;
	case LEAPRAID_TM_TASKTYPE_QUERY_TASK:
		rc = SUCCESS;
		break;
	default:
		rc = FAILED;
		break;
	}

out_cleanup:
	leapraid_clear_tm_flg(adapter, hdl);
	adapter->driver_cmds.tm_cmd.status = LEAPRAID_CMD_NOT_USED;
	return rc;
}

int leapraid_issue_locked_tm(struct leapraid_adapter *adapter, u16 hdl,
			     uint channel, uint id, uint lun, u8 type,
			     u16 target_taskid, u8 tr_method)
{
	int rc;

	mutex_lock(&adapter->driver_cmds.tm_cmd.mutex);
	rc = leapraid_issue_tm(adapter, hdl, channel, id, lun, type,
			       target_taskid, tr_method);
	mutex_unlock(&adapter->driver_cmds.tm_cmd.mutex);

	return rc;
}

void leapraid_smart_fault_detect(struct leapraid_adapter *adapter, u16 hdl)
{
	struct leapraid_starget_priv *starget_priv;
	struct leapraid_sas_dev *sas_dev;
	struct scsi_target *starget;
	unsigned long flags;

	spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
	sas_dev = leapraid_hold_lock_get_sas_dev_by_hdl(adapter, hdl);
	if (!sas_dev) {
		spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);
		return;
	}

	starget = sas_dev->starget;
	starget_priv = starget ? starget->hostdata : NULL;
	if (!starget_priv) {
		spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);
		goto release_sdev;
	}

	if (starget_priv->flg & LEAPRAID_TGT_FLG_RAID_MEMBER ||
	    starget_priv->flg & LEAPRAID_TGT_FLG_VOLUME) {
		spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);
		goto release_sdev;
	}

	spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);
	leapraid_async_turn_on_led(adapter, hdl);
release_sdev:
	if (sas_dev)
		leapraid_sdev_put(sas_dev);
}

static void leapraid_process_sense_data(struct leapraid_adapter *adapter,
					struct leapraid_scsiio_rep *scsiio_rep,
					struct scsi_cmnd *scmd, u16 taskid)
{
	struct sense_info data;
	const void *sense_data;
	u32 sz;

	if (!(scsiio_rep->scsi_state & LEAPRAID_SCSI_STATE_AUTOSENSE_VALID))
		return;

	sense_data = leapraid_get_sense_buffer(adapter, taskid);
	sz = min_t(u32, SCSI_SENSE_BUFFERSIZE,
		   le32_to_cpu(scsiio_rep->sense_count));

	memcpy(scmd->sense_buffer, sense_data, sz);
	leapraid_get_sense_data(scmd->sense_buffer, &data);
	if (data.asc == ASC_FAILURE_PREDICTION_THRESHOLD_EXCEEDED)
		leapraid_smart_fault_detect(adapter,
					    le16_to_cpu(scsiio_rep->dev_hdl));
}

static void leapraid_handle_data_underrun(
		struct leapraid_scsiio_rep *scsiio_rep,
		struct scsi_cmnd *scmd, u32 xfer_cnt)
{
	u8 scsi_status = scsiio_rep->scsi_status;
	u8 scsi_state = scsiio_rep->scsi_state;

	scmd->result = (DID_OK << LEAPRAID_SCSI_HOST_SHIFT) | scsi_status;

	if (scsi_state & LEAPRAID_SCSI_STATE_AUTOSENSE_VALID)
		return;

	if (xfer_cnt < scmd->underflow) {
		if (scsi_status == SAM_STAT_BUSY)
			scmd->result = SAM_STAT_BUSY;
		else
			scmd->result = DID_SOFT_ERROR <<
				LEAPRAID_SCSI_HOST_SHIFT;
	} else if (scsi_state & (LEAPRAID_SCSI_STATE_AUTOSENSE_FAILED |
				 LEAPRAID_SCSI_STATE_NO_SCSI_STATUS)) {
		scmd->result = DID_SOFT_ERROR << LEAPRAID_SCSI_HOST_SHIFT;
	} else if (scsi_state & LEAPRAID_SCSI_STATE_TERMINATED) {
		scmd->result = DID_RESET << LEAPRAID_SCSI_HOST_SHIFT;
	} else if (!xfer_cnt && scmd->cmnd[0] == REPORT_LUNS) {
		scsiio_rep->scsi_state = LEAPRAID_SCSI_STATE_AUTOSENSE_VALID;
		scsiio_rep->scsi_status = SAM_STAT_CHECK_CONDITION;
		scsi_build_sense(scmd, 0, ILLEGAL_REQUEST,
				 LEAPRAID_SCSI_ASC_INVALID_CMD_CODE,
				 LEAPRAID_SCSI_ASCQ_DEFAULT);
	}
}

static void leapraid_handle_success_status(
		struct leapraid_scsiio_rep *scsiio_rep,
		struct scsi_cmnd *scmd,
		u32 response_code)
{
	u8 scsi_status = scsiio_rep->scsi_status;
	u8 scsi_state = scsiio_rep->scsi_state;

	scmd->result = (DID_OK << LEAPRAID_SCSI_HOST_SHIFT) | scsi_status;

	if (response_code == LEAPRAID_TM_RSP_INVALID_FRAME ||
	    (scsi_state & (LEAPRAID_SCSI_STATE_AUTOSENSE_FAILED |
			   LEAPRAID_SCSI_STATE_NO_SCSI_STATUS)))
		scmd->result = DID_SOFT_ERROR << LEAPRAID_SCSI_HOST_SHIFT;
	else if (scsi_state & LEAPRAID_SCSI_STATE_TERMINATED)
		scmd->result = DID_RESET << LEAPRAID_SCSI_HOST_SHIFT;
}

static void leapraid_scsiio_done_dispatch(
		struct leapraid_adapter *adapter,
		struct leapraid_scsiio_rep *scsiio_rep,
		struct leapraid_sdev_priv *sdev_priv,
		struct scsi_cmnd *scmd,
		u16 taskid, u32 response_code)
{
	u8 scsi_status = scsiio_rep->scsi_status;
	u8 scsi_state = scsiio_rep->scsi_state;
	u16 adapter_status;
	u32 xfer_cnt;
	u32 sz;

	adapter_status = le16_to_cpu(scsiio_rep->adapter_status) &
				     LEAPRAID_ADAPTER_STATUS_MASK;

	xfer_cnt = le32_to_cpu(scsiio_rep->transfer_count);
	scsi_set_resid(scmd, scsi_bufflen(scmd) - xfer_cnt);

	if (adapter_status == LEAPRAID_ADAPTER_STATUS_SCSI_DATA_UNDERRUN &&
	    xfer_cnt == 0 &&
	    (scsi_status == LEAPRAID_SCSI_STATUS_BUSY ||
	    scsi_status == LEAPRAID_SCSI_STATUS_RESERVATION_CONFLICT ||
	    scsi_status == LEAPRAID_SCSI_STATUS_TASK_SET_FULL))
		adapter_status = LEAPRAID_ADAPTER_STATUS_SUCCESS;

	switch (adapter_status) {
	case LEAPRAID_ADAPTER_STATUS_SCSI_DEVICE_NOT_THERE:
		scmd->result = DID_NO_CONNECT << LEAPRAID_SCSI_HOST_SHIFT;
		break;

	case LEAPRAID_ADAPTER_STATUS_BUSY:
	case LEAPRAID_ADAPTER_STATUS_INSUFFICIENT_RESOURCES:
		scmd->result = SAM_STAT_BUSY;
		break;

	case LEAPRAID_ADAPTER_STATUS_SCSI_RESIDUAL_MISMATCH:
		if (xfer_cnt == 0 || scmd->underflow > xfer_cnt)
			scmd->result = DID_SOFT_ERROR <<
				LEAPRAID_SCSI_HOST_SHIFT;
		else
			scmd->result = (DID_OK << LEAPRAID_SCSI_HOST_SHIFT) |
				scsi_status;
		break;

	case LEAPRAID_ADAPTER_STATUS_SCSI_ADAPTER_TERMINATED:
		if (sdev_priv->block) {
			scmd->result = DID_TRANSPORT_DISRUPTED <<
				LEAPRAID_SCSI_HOST_SHIFT;
			return;
		}

		if (scmd->device->channel == RAID_CHANNEL &&
		    scsi_state == (LEAPRAID_SCSI_STATE_TERMINATED |
				    LEAPRAID_SCSI_STATE_NO_SCSI_STATUS)) {
			scmd->result = DID_RESET << LEAPRAID_SCSI_HOST_SHIFT;
			break;
		}

		scmd->result = DID_SOFT_ERROR << LEAPRAID_SCSI_HOST_SHIFT;
		break;

	case LEAPRAID_ADAPTER_STATUS_SCSI_TASK_TERMINATED:
	case LEAPRAID_ADAPTER_STATUS_SCSI_EXT_TERMINATED:
		scmd->result = DID_RESET << LEAPRAID_SCSI_HOST_SHIFT;
		break;

	case LEAPRAID_ADAPTER_STATUS_SCSI_DATA_UNDERRUN:
		leapraid_handle_data_underrun(scsiio_rep, scmd, xfer_cnt);
		break;

	case LEAPRAID_ADAPTER_STATUS_SCSI_DATA_OVERRUN:
		scsi_set_resid(scmd, 0);
		leapraid_handle_success_status(scsiio_rep, scmd,
					       response_code);
		break;
	case LEAPRAID_ADAPTER_STATUS_SCSI_RECOVERED_ERROR:
	case LEAPRAID_ADAPTER_STATUS_SUCCESS:
		leapraid_handle_success_status(scsiio_rep, scmd,
					       response_code);
		break;

	case LEAPRAID_ADAPTER_STATUS_SCSI_PROTOCOL_ERROR:
	case LEAPRAID_ADAPTER_STATUS_INTERNAL_ERROR:
	case LEAPRAID_ADAPTER_STATUS_SCSI_IO_DATA_ERROR:
	case LEAPRAID_ADAPTER_STATUS_SCSI_TASK_MGMT_FAILED:
	default:
		scmd->result = DID_SOFT_ERROR << LEAPRAID_SCSI_HOST_SHIFT;
		break;
	}

	if (!scmd->result)
		return;

	scsi_print_command(scmd);
	dev_warn(&adapter->pdev->dev,
		 "SCSI I/O: hdl=0x%x, status: 0x%x, 0x%x, 0x%x\n",
		 le16_to_cpu(scsiio_rep->dev_hdl), adapter_status,
		 scsi_status, scsi_state);

	if (scsi_state & LEAPRAID_SCSI_STATE_AUTOSENSE_VALID) {
		struct scsi_sense_hdr sshdr;

		sz = min_t(u32, SCSI_SENSE_BUFFERSIZE,
			   le32_to_cpu(scsiio_rep->sense_count));
		if (scsi_normalize_sense(scmd->sense_buffer, sz,
					 &sshdr))
			dev_warn(&adapter->pdev->dev,
				 "Sense: key=0x%x asc=0x%x ascq=0x%x\n",
				 sshdr.sense_key, sshdr.asc,
				 sshdr.ascq);
		else
			dev_warn(&adapter->pdev->dev,
				 "Sense: Invalid sense data\n");
	}
}

bool leapraid_scsiio_done(struct leapraid_adapter *adapter, u16 taskid,
			  u8 msix_index, u32 rep)
{
	struct leapraid_scsiio_rep *scsiio_rep;
	struct leapraid_sdev_priv *sdev_priv;
	struct scsi_cmnd *scmd;
	u32 response_code = 0;

	scmd = leapraid_get_scmd_from_taskid(adapter, taskid);
	if (!scmd)
		return true;

	scsiio_rep = leapraid_get_reply_vaddr(adapter, rep);
	if (!scsiio_rep) {
		scmd->result = DID_OK << LEAPRAID_SCSI_HOST_SHIFT;
		goto out_scsiio_done;
	}

	sdev_priv = scmd->device->hostdata;
	if (!sdev_priv ||
	    !sdev_priv->starget_priv ||
	    sdev_priv->starget_priv->deleted) {
		scmd->result = DID_NO_CONNECT << LEAPRAID_SCSI_HOST_SHIFT;
		goto out_scsiio_done;
	}

	if (scsiio_rep->scsi_state & LEAPRAID_SCSI_STATE_RESPONSE_INFO_VALID)
		response_code = le32_to_cpu(scsiio_rep->resp_info) & 0xFF;

	leapraid_process_sense_data(adapter, scsiio_rep, scmd, taskid);
	leapraid_scsiio_done_dispatch(adapter, scsiio_rep, sdev_priv, scmd,
				      taskid, response_code);

out_scsiio_done:
	scsi_dma_unmap(scmd);
	leapraid_free_taskid(adapter, taskid);
	scsi_done(scmd);
	return false;
}

static void leapraid_probe_raid(struct leapraid_adapter *adapter)
{
	struct leapraid_raid_volume *raid_volume, *next_raid_volume;
	unsigned long flags;
	LIST_HEAD(head);
	int rc;

	spin_lock_irqsave(&adapter->dev_topo.raid_volume_lock, flags);
	list_splice_init(&adapter->dev_topo.raid_volume_list, &head);
	spin_unlock_irqrestore(&adapter->dev_topo.raid_volume_lock, flags);

	list_for_each_entry_safe(raid_volume, next_raid_volume, &head, list) {
		spin_lock_irqsave(&adapter->dev_topo.raid_volume_lock, flags);
		list_move_tail(&raid_volume->list,
			       &adapter->dev_topo.raid_volume_list);
		if (raid_volume->starget) {
			spin_unlock_irqrestore(
				&adapter->dev_topo.raid_volume_lock, flags);
			continue;
		}

		leapraid_raid_volume_get(raid_volume);
		spin_unlock_irqrestore(
			&adapter->dev_topo.raid_volume_lock, flags);

		rc = scsi_add_device(adapter->shost, RAID_CHANNEL,
				     raid_volume->id, 0);
		if (rc)
			leapraid_raid_volume_remove(adapter, raid_volume);

		leapraid_raid_volume_put(raid_volume);
	}
}

static void leapraid_sas_dev_make_active(struct leapraid_adapter *adapter,
					 struct leapraid_sas_dev *sas_dev)
{
	unsigned long flags;

	spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
	if (!list_empty(&sas_dev->list)) {
		list_del_init(&sas_dev->list);
		leapraid_sdev_put(sas_dev);
	}

	leapraid_sdev_get(sas_dev);
	list_add_tail(&sas_dev->list, &adapter->dev_topo.sas_dev_list);
	spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);
}

static void leapraid_probe_sas(struct leapraid_adapter *adapter)
{
	struct leapraid_sas_dev *sas_dev;
	bool added;

	for (;;) {
		sas_dev = leapraid_get_next_sas_dev_from_init_list(adapter);
		if (!sas_dev)
			break;
		added = leapraid_transport_port_add(adapter,
						    sas_dev->hdl,
						    sas_dev->parent_sas_addr,
						    sas_dev->card_port);
		if (!added)
			goto remove_dev;

		if (!sas_dev->starget &&
		    !adapter->scan_dev_desc.driver_loading) {
			leapraid_transport_port_remove(adapter,
						       sas_dev->sas_addr,
						       sas_dev->parent_sas_addr,
						       sas_dev->card_port);
			goto remove_dev;
		}

		leapraid_sas_dev_make_active(adapter, sas_dev);
		leapraid_sdev_put(sas_dev);
		continue;

remove_dev:
		leapraid_sas_dev_remove(adapter, sas_dev);
		leapraid_sdev_put(sas_dev);
	}
}

static bool leapraid_get_boot_dev(struct leapraid_adapter *adapter,
				  struct leapraid_boot_dev *boot_dev,
				  void **pdev, u32 *pchnl)
{
	unsigned long flags;
	void *dev;
	u32 chnl;

	spin_lock_irqsave(&adapter->boot_devs.lock, flags);
	if (!boot_dev->dev) {
		spin_unlock_irqrestore(&adapter->boot_devs.lock, flags);
		return false;
	}

	dev = boot_dev->dev;
	chnl = boot_dev->chnl;
	leapraid_boot_dev_get(dev, chnl);
	spin_unlock_irqrestore(&adapter->boot_devs.lock, flags);

	*pdev = dev;
	*pchnl = chnl;
	return true;
}

static void leapraid_probe_boot_dev(struct leapraid_adapter *adapter)
{
	struct leapraid_raid_volume *raid_volume;
	struct leapraid_sas_dev *sas_dev;
	struct leapraid_sas_port *sport;
	unsigned long flags;
	void *dev = NULL;
	u32 chnl;

	if (leapraid_get_boot_dev(adapter,
				  &adapter->boot_devs.requested_boot_dev,
				  &dev, &chnl))
		goto boot_dev_found;

	if (leapraid_get_boot_dev(adapter,
				  &adapter->boot_devs.requested_alt_boot_dev,
				  &dev, &chnl))
		goto boot_dev_found;

	if (leapraid_get_boot_dev(adapter,
				  &adapter->boot_devs.current_boot_dev,
				  &dev, &chnl))
		goto boot_dev_found;

	return;

boot_dev_found:
	switch (chnl) {
	case RAID_CHANNEL:
		raid_volume = dev;

		if (raid_volume->starget)
			break;

		/* TODO eedp */

		if (scsi_add_device(adapter->shost, RAID_CHANNEL,
				    raid_volume->id, 0))
			leapraid_raid_volume_remove(adapter, raid_volume);
		break;
	default:
		sas_dev = dev;

		if (sas_dev->starget)
			break;

		spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
		list_move_tail(&sas_dev->list,
			       &adapter->dev_topo.sas_dev_list);
		spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);

		if (!sas_dev->card_port)
			break;

		sport = leapraid_transport_port_add(adapter, sas_dev->hdl,
						    sas_dev->parent_sas_addr,
						    sas_dev->card_port);
		if (!sport)
			leapraid_sas_dev_remove(adapter, sas_dev);
		break;
	}

	leapraid_boot_dev_put(dev, chnl);
}

static void leapraid_probe_devices(struct leapraid_adapter *adapter)
{
	leapraid_probe_boot_dev(adapter);

	if (adapter->adapter_attr.raid_support) {
		leapraid_probe_raid(adapter);
		leapraid_probe_sas(adapter);
	} else {
		leapraid_probe_sas(adapter);
	}
}

void leapraid_scan_dev_done(struct leapraid_adapter *adapter)
{
	if (adapter->scan_dev_desc.wait_scan_dev_done) {
		adapter->scan_dev_desc.wait_scan_dev_done = 0;
		leapraid_probe_devices(adapter);
	}

	adapter->scan_dev_desc.scan_start = 0;

	leapraid_check_scheduled_fault_start(adapter);
	leapraid_fw_log_start(adapter);
	adapter->scan_dev_desc.driver_loading = 0;
	wake_up(&adapter->scan_dev_desc.wait_driver_loading);
}

static const struct pci_device_id leapraid_pci_table[] = {
	{ PCI_DEVICE_SUB(LEAPRAID_VENDOR_ID, LEAPRAID_DEVID_HBA,
			 LEAPRAID_SUBVENDOR_ID,
			 LEAPRAID_SUBDEVID_HBA) },
	{ 0, }
};

static inline bool leapraid_is_scmd_permitted(struct leapraid_adapter *adapter,
					      struct scsi_cmnd *scmd)
{
	u8 opcode;

	if (adapter->access_ctrl.pcie_recovering ||
	    atomic_read(&adapter->overheat_desc.thermal_alert))
		return false;

	if (adapter->access_ctrl.host_removing) {
		if (leapraid_pci_removed(adapter))
			return false;

		opcode = scmd->cmnd[0];
		return opcode == SYNCHRONIZE_CACHE || opcode == START_STOP;
	}
	return true;
}

static bool leapraid_should_queuecommand(struct leapraid_adapter *adapter,
					 struct leapraid_sdev_priv *sdev_priv,
					 struct scsi_cmnd *scmd,
					 enum scsi_qc_status *rc)
{
	struct leapraid_starget_priv *starget_priv;

	if (!sdev_priv || !sdev_priv->starget_priv)
		goto no_connect;

	if (!leapraid_is_scmd_permitted(adapter, scmd))
		goto no_connect;

	starget_priv = sdev_priv->starget_priv;
	if (starget_priv->hdl == LEAPRAID_INVALID_DEV_HANDLE)
		goto no_connect;

	if (sdev_priv->block &&
	    scsi_get_host_state(scmd->device->host) == SHOST_RECOVERY &&
	    scmd->cmnd[0] == TEST_UNIT_READY) {
		scsi_build_sense(scmd, 0, UNIT_ATTENTION,
				 LEAPRAID_SCSI_ASC_POWER_ON_RESET,
				 LEAPRAID_SCSI_ASCQ_POWER_ON_RESET);
		goto scsiio_done;
	}

	if (adapter->access_ctrl.shost_recovering ||
	    adapter->reset_desc.adapter_link_resetting) {
		*rc = SCSI_MLQUEUE_HOST_BUSY;
		return false;
	}

	if (starget_priv->deleted || sdev_priv->deleted)
		goto no_connect;

	if (starget_priv->tm_busy || sdev_priv->block) {
		*rc = SCSI_MLQUEUE_DEVICE_BUSY;
		return false;
	}

	return true;

no_connect:
	scmd->result = DID_NO_CONNECT << LEAPRAID_SCSI_HOST_SHIFT;
scsiio_done:
	scsi_done(scmd);

	return false;
}

static u32 build_scsiio_req_control(struct scsi_cmnd *scmd,
				    struct leapraid_sdev_priv *sdev_priv)
{
	u32 control;

	switch (scmd->sc_data_direction) {
	case DMA_FROM_DEVICE:
		control = LEAPRAID_SCSIIO_CTRL_READ;
		break;
	case DMA_TO_DEVICE:
		control = LEAPRAID_SCSIIO_CTRL_WRITE;
		break;
	default:
		control = LEAPRAID_SCSIIO_CTRL_NODATATRANSFER;
		break;
	}

	control |= LEAPRAID_SCSIIO_CTRL_SIMPLEQ;

	if (sdev_priv->ncq_prio_enable &&
	    (IOPRIO_PRIO_CLASS(req_get_ioprio(scsi_cmd_to_rq(scmd))) ==
	     IOPRIO_CLASS_RT))
		control |= LEAPRAID_SCSIIO_CTRL_CMDPRI;
	if (scmd->cmd_len == 32)
		control |= LEAPRAID_SCSIIO_CTRL_CDB_32BYTE <<
				LEAPRAID_SCSIIO_CTRL_CDB_LEN_SHIFT;

	return control;
}

enum scsi_qc_status leapraid_queuecommand(struct Scsi_Host *shost,
					  struct scsi_cmnd *scmd)
{
	struct leapraid_adapter *adapter = shost_priv(scmd->device->host);
	struct leapraid_sdev_priv *sdev_priv = scmd->device->hostdata;
	struct leapraid_starget_priv *starget_priv;
	struct leapraid_scsiio_req *scsiio_req;
	u32 control;
	u16 taskid;
	u16 hdl;
	enum scsi_qc_status rc = 0;

	if (!leapraid_should_queuecommand(adapter, sdev_priv, scmd, &rc))
		return rc;

	starget_priv = sdev_priv->starget_priv;
	hdl = starget_priv->hdl;
	control = build_scsiio_req_control(scmd, sdev_priv);

	taskid = leapraid_alloc_scsiio_taskid(adapter, scmd);
	scsiio_req = leapraid_get_task_desc(adapter, taskid);

	scsiio_req->func = LEAPRAID_FUNC_SCSIIO;
	if (sdev_priv->starget_priv->flg & LEAPRAID_TGT_FLG_RAID_MEMBER)
		scsiio_req->func = LEAPRAID_FUNC_SCSIIO_RAID_PASSTHROUGH;
	else
		scsiio_req->func = LEAPRAID_FUNC_SCSIIO;

	scsiio_req->dev_hdl = cpu_to_le16(hdl);
	scsiio_req->data_len = cpu_to_le32(scsi_bufflen(scmd));
	scsiio_req->ctrl = cpu_to_le32(control);
	scsiio_req->io_flg = cpu_to_le16(scmd->cmd_len);
	scsiio_req->msg_flg = 0;
	scsiio_req->sense_buffer_len = SCSI_SENSE_BUFFERSIZE;
	scsiio_req->sense_buffer_low_add =
		leapraid_get_sense_buffer_dma(adapter, taskid);
	scsiio_req->sgl_offset0 =
		offsetof(struct leapraid_scsiio_req, sgl) /
		LEAPRAID_DWORDS_BYTE_SIZE;
	int_to_scsilun(sdev_priv->lun, (struct scsi_lun *)scsiio_req->lun);
	memcpy(scsiio_req->cdb.cdb32, scmd->cmnd, scmd->cmd_len);
	if (scsiio_req->data_len) {
		if (leapraid_build_scmd_ieee_sg(adapter, scmd, taskid)) {
			leapraid_free_taskid(adapter, taskid);
			return SCSI_MLQUEUE_HOST_BUSY;
		}
	} else {
		leapraid_build_ieee_nodata_sg(adapter, &scsiio_req->sgl);
	}

	if (likely(scsiio_req->func == LEAPRAID_FUNC_SCSIIO))
		leapraid_fire_scsi_io(adapter, taskid,
				      le16_to_cpu(scsiio_req->dev_hdl));
	else
		leapraid_fire_task(adapter, taskid);

	dev_dbg(&adapter->pdev->dev,
		"LEAPRAID_SCSIIO: Send Descriptor taskid %d, req type 0x%x\n",
		taskid, scsiio_req->func);
	return rc;
}

static int leapraid_init_cmd_priv(struct Scsi_Host *shost,
				  struct scsi_cmnd *scmd)
{
	struct leapraid_adapter *adapter = shost_priv(shost);
	struct leapraid_io_req_tracker *io_tracker;

	io_tracker = scsi_cmd_priv(scmd);
	leapraid_internal_init_cmd_priv(adapter, io_tracker);

	return 0;
}

static int leapraid_exit_cmd_priv(struct Scsi_Host *shost,
				  struct scsi_cmnd *scmd)
{
	struct leapraid_adapter *adapter = shost_priv(shost);
	struct leapraid_io_req_tracker *io_tracker;

	io_tracker = scsi_cmd_priv(scmd);
	leapraid_internal_exit_cmd_priv(adapter, io_tracker);

	return 0;
}

static int leapraid_error_handler(struct scsi_cmnd *scmd,
				  const char *str, u8 type)
{
	struct leapraid_adapter *adapter = shost_priv(scmd->device->host);
	struct scsi_target *starget = scmd->device->sdev_target;
	struct leapraid_starget_priv *starget_priv = starget->hostdata;
	struct leapraid_io_req_tracker *io_req_tracker = NULL;
	struct leapraid_sdev_priv *sdev_priv;
	struct leapraid_sas_dev *sas_dev = NULL;
	u16 hdl;
	int rc;

	dev_info(&adapter->pdev->dev,
		 "EH enter: type=%s, scmd=0x%p, req tag=%d\n", str, scmd,
		 scsi_cmd_to_rq(scmd)->tag);
	scsi_print_command(scmd);

	if (type == LEAPRAID_TM_TASKTYPE_ABORT_TASK) {
		io_req_tracker = scsi_cmd_priv(scmd);
		dev_info(&adapter->pdev->dev,
			 "EH ABORT: scmd=0x%p, pend=%ums, tout=%ums, tag=%d\n",
			 scmd,
			 jiffies_to_msecs(jiffies - scmd->jiffies_at_alloc),
			 (scsi_cmd_to_rq(scmd)->timeout / HZ) * 1000,
			 scsi_cmd_to_rq(scmd)->tag);
	}

	if (leapraid_pci_removed(adapter) ||
	    adapter->access_ctrl.host_removing) {
		dev_err(&adapter->pdev->dev,
			"EH %s failed: %s scmd=0x%p\n", str,
			(adapter->access_ctrl.host_removing ?
			"shost removing!" : "pci_dev removed!"), scmd);
		if (type == LEAPRAID_TM_TASKTYPE_ABORT_TASK &&
		    io_req_tracker && io_req_tracker->taskid)
			leapraid_free_taskid(adapter, io_req_tracker->taskid);
		scmd->result = DID_NO_CONNECT << LEAPRAID_SCSI_HOST_SHIFT;
#ifdef FAST_IO_FAIL
		rc = FAST_IO_FAIL;
#else
		rc = FAILED;
#endif
		goto out_eh_done;
	}

	sdev_priv = scmd->device->hostdata;
	if (!sdev_priv || !sdev_priv->starget_priv) {
		dev_warn(&adapter->pdev->dev,
			 "EH %s: SAS dev or starget gone, scmd=0x%p\n",
			 str, scmd);
		scmd->result = DID_NO_CONNECT << LEAPRAID_SCSI_HOST_SHIFT;
		scsi_done(scmd);
		rc = SUCCESS;
		goto out_eh_done;
	}

	if (type == LEAPRAID_TM_TASKTYPE_ABORT_TASK) {
		if (!io_req_tracker) {
			dev_warn(&adapter->pdev->dev,
				 "EH ABORT: No I/O tracker, scmd 0x%p\n", scmd);
			scmd->result = DID_RESET << LEAPRAID_SCSI_HOST_SHIFT;
			rc = SUCCESS;
			goto out_eh_done;
		}

		if (sdev_priv->starget_priv->flg &
			LEAPRAID_TGT_FLG_RAID_MEMBER ||
		    sdev_priv->starget_priv->flg & LEAPRAID_TGT_FLG_VOLUME) {
			dev_err(&adapter->pdev->dev,
				"EH ABORT: Skip RAID/VOLUME, scmd=0x%p\n",
				scmd);
			scmd->result = DID_RESET << LEAPRAID_SCSI_HOST_SHIFT;
			rc = FAILED;
			goto out_eh_done;
		}

		hdl = sdev_priv->starget_priv->hdl;
	} else {
		hdl = 0;
		if (sdev_priv->starget_priv->flg &
		    LEAPRAID_TGT_FLG_RAID_MEMBER) {
			sas_dev = leapraid_get_sas_dev_from_tgt(adapter,
								starget_priv);
			if (sas_dev)
				hdl = sas_dev->volume_hdl;
		} else {
			hdl = sdev_priv->starget_priv->hdl;
		}

		if (!hdl) {
			dev_err(&adapter->pdev->dev,
				"EH %s failed: Target handle 0, scmd=0x%p\n",
				str, scmd);
			scmd->result = DID_RESET << LEAPRAID_SCSI_HOST_SHIFT;
			rc = FAILED;
			goto out_eh_done;
		}
	}

	dev_info(&adapter->pdev->dev,
		 "EH issue TM: type=%s, scmd=0x%p, hdl=0x%x\n",
		 str, scmd, hdl);

	rc = leapraid_issue_locked_tm(
			adapter,
			hdl,
			scmd->device->channel,
			scmd->device->id,
			(type == LEAPRAID_TM_TASKTYPE_TARGET_RESET ?
				0 : scmd->device->lun),
			type,
			(type == LEAPRAID_TM_TASKTYPE_ABORT_TASK ?
				io_req_tracker->taskid : 0),
			LEAPRAID_TM_MSGFLAGS_LINK_RESET);

out_eh_done:
	if (type == LEAPRAID_TM_TASKTYPE_ABORT_TASK) {
		if (rc != SUCCESS)
			dev_err(&adapter->pdev->dev,
				"EH ABORT result: failed, scmd=0x%p\n",
				scmd);
	} else {
		if (rc != SUCCESS)
			dev_err(&adapter->pdev->dev,
				"EH %s result: failed, scmd=0x%p\n",
				str, scmd);
		if (sas_dev)
			leapraid_sdev_put(sas_dev);
	}
	return rc;
}

static int leapraid_eh_abort_handler(struct scsi_cmnd *scmd)
{
	return leapraid_error_handler(scmd, "ABORT TASK",
				      LEAPRAID_TM_TASKTYPE_ABORT_TASK);
}

static int leapraid_eh_device_reset_handler(struct scsi_cmnd *scmd)
{
	return leapraid_error_handler(scmd, "UNIT RESET",
				      LEAPRAID_TM_TASKTYPE_LOGICAL_UNIT_RESET);
}

static int leapraid_eh_target_reset_handler(struct scsi_cmnd *scmd)
{
	return leapraid_error_handler(scmd, "TARGET RESET",
				      LEAPRAID_TM_TASKTYPE_TARGET_RESET);
}

static int leapraid_eh_host_reset_handler(struct scsi_cmnd *scmd)
{
	struct leapraid_adapter *adapter = shost_priv(scmd->device->host);
	int rc;

	dev_info(&adapter->pdev->dev,
		 "EH HOST RESET enter: scmd=%p, req tag=%d\n",
		 scmd,
		 scsi_cmd_to_rq(scmd)->tag);
	scsi_print_command(scmd);

	if (adapter->scan_dev_desc.driver_loading ||
	    adapter->access_ctrl.host_removing) {
		dev_err(&adapter->pdev->dev,
			"EH HOST RESET failed: %s scmd=0x%p\n",
			(adapter->access_ctrl.host_removing ?
			"shost removing!" : "driver loading!"), scmd);
		rc = FAILED;
		goto out_host_reset_done;
	}

	dev_info(&adapter->pdev->dev, "%s:%d: Issuing hard reset\n",
		 __func__, __LINE__);
	if (leapraid_hard_reset_handler(adapter, FULL_RESET) < 0)
		rc = FAILED;
	else
		rc = SUCCESS;

out_host_reset_done:
	if (rc != SUCCESS)
		dev_err(&adapter->pdev->dev,
			"EH HOST RESET result: failed, scmd=0x%p\n",
			scmd);
	return rc;
}

static int leapraid_sdev_init(struct scsi_device *sdev)
{
	struct leapraid_raid_volume *raid_volume;
	struct leapraid_starget_priv *stgt_priv;
	struct leapraid_sdev_priv *sdev_priv;
	struct leapraid_adapter *adapter;
	struct leapraid_sas_dev *sas_dev;
	struct scsi_target *tgt;
	struct Scsi_Host *shost;
	unsigned long flags;

	sdev_priv = kzalloc_obj(*sdev_priv);
	if (!sdev_priv)
		return -ENOMEM;

	sdev_priv->lun = sdev->lun;
	sdev_priv->flg = LEAPRAID_DEVICE_FLG_INIT;
	tgt = scsi_target(sdev);
	stgt_priv = tgt->hostdata;
	stgt_priv->num_luns++;
	sdev_priv->starget_priv = stgt_priv;
	sdev->hostdata = sdev_priv;
	if (stgt_priv->flg & LEAPRAID_TGT_FLG_RAID_MEMBER)
		sdev->no_uld_attach = LEAPRAID_NO_ULD_ATTACH;

	shost = dev_to_shost(&tgt->dev);
	adapter = shost_priv(shost);
	if (tgt->channel == RAID_CHANNEL) {
		raid_volume = leapraid_raid_volume_find_by_id(adapter,
							      tgt->id,
							      tgt->channel);
		if (raid_volume) {
			spin_lock_irqsave(&adapter->dev_topo.raid_volume_lock,
					  flags);
			raid_volume->sdev = sdev;
			spin_unlock_irqrestore(
				&adapter->dev_topo.raid_volume_lock, flags);
			leapraid_raid_volume_put(raid_volume);
		}
	}

	if (!(stgt_priv->flg & LEAPRAID_TGT_FLG_VOLUME)) {
		spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
		sas_dev = leapraid_hold_lock_get_sas_dev_by_addr(
				adapter,
				stgt_priv->sas_address,
				stgt_priv->card_port);
		if (sas_dev && !sas_dev->starget) {
			sdev_printk(KERN_INFO, sdev,
				    "%s: Assign starget to sas_dev\n",
				    __func__);
			sas_dev->starget = tgt;
		}

		if (sas_dev)
			leapraid_sdev_put(sas_dev);
		spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);
	}
	return 0;
}

static bool leapraid_slave_cfg_volume(struct scsi_device *sdev,
				      struct queue_limits *lim)
{
	struct Scsi_Host *shost = sdev->host;
	struct leapraid_adapter *adapter = shost_priv(shost);
	struct leapraid_raid_volume *raid_volume;
	struct leapraid_starget_priv *starget_priv;
	struct leapraid_sdev_priv *sdev_priv;
	int qd;
	u16 hdl;

	sdev_priv = sdev->hostdata;
	starget_priv = sdev_priv->starget_priv;
	hdl = starget_priv->hdl;

	raid_volume = leapraid_raid_volume_find_by_hdl(adapter, hdl);
	if (!raid_volume) {
		sdev_printk(KERN_WARNING, sdev,
			    "%s: RAID volume not found, hdl=0x%x\n",
			    __func__, hdl);
		return 1;
	}

	if (leapraid_get_volume_cap(adapter, raid_volume)) {
		sdev_printk(KERN_ERR, sdev,
			    "%s: Failed to get volume cap, hdl=0x%x\n",
			    __func__, hdl);
		leapraid_raid_volume_put(raid_volume);
		return 1;
	}

	qd = (raid_volume->dev_info & LEAPRAID_DEVTYP_SSP_TGT) ?
		adapter->adapter_attr.narrowport_max_queue_depth :
		adapter->adapter_attr.sata_max_queue_depth;
	if (raid_volume->vol_type != LEAPRAID_VOL_TYPE_RAID0)
		qd = adapter->adapter_attr.raid_volume_max_queue_depth;

	sdev_printk(KERN_INFO, sdev,
		    "RAID volume: hdl=0x%04x, wwid=0x%016llx\n",
		    raid_volume->hdl, (unsigned long long)raid_volume->wwid);

	if (shost->max_sectors > LEAPRAID_MAX_SECTORS)
		lim->max_hw_sectors = LEAPRAID_MAX_SECTORS;

	leapraid_change_queue_depth(sdev, qd);
	leapraid_raid_volume_put(raid_volume);
	return 0;
}

static bool leapraid_slave_configure_extra(struct scsi_device *sdev,
					   struct leapraid_sas_dev **psas_dev,
					   u16 vol_hdl, u64 volume_wwid,
					   bool *is_target_ssp, int *qd)
{
	struct leapraid_sas_dev *sas_dev;
	struct leapraid_sdev_priv *sdev_priv;
	struct Scsi_Host *shost = sdev->host;
	struct leapraid_adapter *adapter = shost_priv(shost);
	unsigned long flags;

	sdev_priv = sdev->hostdata;
	spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
	*is_target_ssp = false;
	sas_dev = leapraid_hold_lock_get_sas_dev_by_addr(
			adapter,
			sdev_priv->starget_priv->sas_address,
			sdev_priv->starget_priv->card_port);
	if (!sas_dev) {
		spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);
		sdev_printk(KERN_WARNING, sdev,
			    "%s: SAS dev not found, sas=0x%llx\n",
			    __func__, sdev_priv->starget_priv->sas_address);
		return 1;
	}

	*psas_dev = sas_dev;
	sas_dev->volume_hdl = vol_hdl;
	sas_dev->volume_wwid = volume_wwid;
	if (sas_dev->dev_info & LEAPRAID_DEVTYP_SSP_TGT) {
		*qd = (sas_dev->port_connection > 1) ?
			adapter->adapter_attr.wideport_max_queue_depth :
			adapter->adapter_attr.narrowport_max_queue_depth;
		*is_target_ssp = true;
		if (sas_dev->dev_info & LEAPRAID_DEVTYP_SEP)
			sdev_priv->sep = 1;
	} else {
		*qd = adapter->adapter_attr.sata_max_queue_depth;
	}

	sdev_printk(KERN_INFO, sdev,
		    "device name=0x%016llx, SAS addr=0x%016llx\n",
		    (unsigned long long)sas_dev->dev_name,
		    (unsigned long long)sas_dev->sas_addr);
	leapraid_sdev_put(sas_dev);
	spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);
	return 0;
}

static int leapraid_sdev_configure(struct scsi_device *sdev,
				   struct queue_limits *lim)
{
	struct leapraid_sas_dev *sas_dev;
	struct leapraid_sdev_priv *sdev_priv;
	struct Scsi_Host *shost = sdev->host;
	struct leapraid_starget_priv *starget_priv;
	struct leapraid_adapter *adapter;
	u16 hdl, vol_hdl = 0;
	bool is_target_ssp = false;
	u64 volume_wwid = 0;
	int qd = 1;

	adapter = shost_priv(shost);
	sdev_priv = sdev->hostdata;
	sdev_priv->flg &= ~LEAPRAID_DEVICE_FLG_INIT;
	starget_priv = sdev_priv->starget_priv;
	hdl = starget_priv->hdl;
	if (starget_priv->flg & LEAPRAID_TGT_FLG_VOLUME)
		return leapraid_slave_cfg_volume(sdev, lim);

	if (starget_priv->flg & LEAPRAID_TGT_FLG_RAID_MEMBER) {
		if (leapraid_cfg_get_volume_hdl(adapter, hdl, &vol_hdl)) {
			sdev_printk(KERN_WARNING, sdev,
				    "%s: Get volume hdl failed, hdl=0x%x\n",
				    __func__, hdl);
			return 1;
		}

		if (vol_hdl && leapraid_cfg_get_volume_wwid(adapter, vol_hdl,
							    &volume_wwid)) {
			sdev_printk(KERN_WARNING, sdev,
				    "%s: Get wwid failed, volume_hdl=0x%x\n",
				    __func__, vol_hdl);
			return 1;
		}
	}

	if (leapraid_slave_configure_extra(sdev, &sas_dev, vol_hdl,
					   volume_wwid, &is_target_ssp, &qd)) {
		sdev_printk(KERN_WARNING, sdev,
			    "%s: slave_configure_extra failed\n", __func__);
		return 1;
	}

	leapraid_change_queue_depth(sdev, qd);
	if (is_target_ssp)
		sas_read_port_mode_page(sdev);

	return 0;
}

static void leapraid_sdev_destroy(struct scsi_device *sdev)
{
	struct leapraid_adapter *adapter;
	struct Scsi_Host *shost;
	struct leapraid_sas_dev *sas_dev;
	struct leapraid_starget_priv *starget_priv;
	struct scsi_target *stgt;
	unsigned long flags;

	if (!sdev->hostdata)
		return;

	stgt = scsi_target(sdev);
	starget_priv = stgt->hostdata;
	starget_priv->num_luns--;
	shost = dev_to_shost(&stgt->dev);
	adapter = shost_priv(shost);
	if (!(starget_priv->flg & LEAPRAID_TGT_FLG_VOLUME)) {
		spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
		sas_dev = leapraid_hold_lock_get_sas_dev_from_tgt(adapter,
								  starget_priv);
		if (sas_dev && !starget_priv->num_luns)
			sas_dev->starget = NULL;
		if (sas_dev)
			leapraid_sdev_put(sas_dev);
		spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);
	}

	kfree(sdev->hostdata);
	sdev->hostdata = NULL;
}

static int leapraid_target_alloc_raid(struct scsi_target *tgt)
{
	struct leapraid_starget_priv *starget_priv;
	struct leapraid_raid_volume *raid_volume;
	struct Scsi_Host *shost = dev_to_shost(&tgt->dev);
	struct leapraid_adapter *adapter = shost_priv(shost);
	unsigned long flags;

	starget_priv = tgt->hostdata;
	raid_volume = leapraid_raid_volume_find_by_id(adapter, tgt->id,
						      tgt->channel);
	if (raid_volume) {
		spin_lock_irqsave(&adapter->dev_topo.raid_volume_lock, flags);
		starget_priv->hdl = raid_volume->hdl;
		starget_priv->sas_address = raid_volume->wwid;
		starget_priv->flg |= LEAPRAID_TGT_FLG_VOLUME;
		raid_volume->starget = tgt;
		spin_unlock_irqrestore(&adapter->dev_topo.raid_volume_lock,
				       flags);
		leapraid_raid_volume_put(raid_volume);
	}
	return 0;
}

static int leapraid_target_alloc_sas(struct scsi_target *tgt)
{
	struct sas_rphy *rphy;
	struct Scsi_Host *shost;
	struct leapraid_sas_dev *sas_dev;
	struct leapraid_adapter *adapter;
	struct leapraid_starget_priv *starget_priv;
	unsigned long flags;

	shost = dev_to_shost(&tgt->dev);
	adapter = shost_priv(shost);
	starget_priv = tgt->hostdata;
	spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
	rphy = dev_to_rphy(tgt->dev.parent);
	sas_dev = leapraid_hold_lock_get_sas_dev_by_addr_and_rphy(
			adapter,
			rphy->identify.sas_address,
			rphy);
	if (sas_dev) {
		starget_priv->sas_dev = sas_dev;
		starget_priv->card_port = sas_dev->card_port;
		starget_priv->sas_address = sas_dev->sas_addr;
		starget_priv->hdl = sas_dev->hdl;
		sas_dev->channel = tgt->channel;
		sas_dev->id = tgt->id;
		sas_dev->starget = tgt;
		if (sas_dev->hdl &&
		    sas_dev->hdl <=
		    adapter->adapter_attr.features.max_dev_handle &&
		    test_bit(sas_dev->hdl, adapter->dev_topo.pd_hdls))
			starget_priv->flg |= LEAPRAID_TGT_FLG_RAID_MEMBER;
	}
	spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);

	return 0;
}

static int leapraid_target_alloc(struct scsi_target *tgt)
{
	struct leapraid_starget_priv *starget_priv;

	starget_priv = kzalloc_obj(*starget_priv);
	if (!starget_priv)
		return -ENOMEM;

	tgt->hostdata = starget_priv;
	starget_priv->starget = tgt;
	starget_priv->hdl = LEAPRAID_INVALID_DEV_HANDLE;
	if (tgt->channel == RAID_CHANNEL)
		return leapraid_target_alloc_raid(tgt);

	return leapraid_target_alloc_sas(tgt);
}

static void leapraid_target_destroy_raid(struct scsi_target *tgt)
{
	struct leapraid_raid_volume *raid_volume;
	struct Scsi_Host *shost = dev_to_shost(&tgt->dev);
	struct leapraid_adapter *adapter = shost_priv(shost);
	unsigned long flags;

	raid_volume = leapraid_raid_volume_find_by_id(adapter, tgt->id,
						      tgt->channel);
	if (raid_volume) {
		spin_lock_irqsave(&adapter->dev_topo.raid_volume_lock, flags);
		raid_volume->starget = NULL;
		raid_volume->sdev = NULL;
		spin_unlock_irqrestore(&adapter->dev_topo.raid_volume_lock,
				       flags);
		leapraid_raid_volume_put(raid_volume);
	}
}

static void leapraid_target_destroy_sas(struct scsi_target *tgt)
{
	struct leapraid_adapter *adapter;
	struct leapraid_sas_dev *sas_dev;
	struct leapraid_starget_priv *starget_priv;
	struct Scsi_Host *shost;
	unsigned long flags;

	shost = dev_to_shost(&tgt->dev);
	adapter = shost_priv(shost);
	starget_priv = tgt->hostdata;

	spin_lock_irqsave(&adapter->dev_topo.sas_dev_lock, flags);
	sas_dev = leapraid_hold_lock_get_sas_dev_from_tgt(adapter,
							  starget_priv);
	if (sas_dev &&
	    sas_dev->starget == tgt &&
	    sas_dev->id == tgt->id &&
	    sas_dev->channel == tgt->channel)
		sas_dev->starget = NULL;

	if (sas_dev) {
		starget_priv->sas_dev = NULL;
		leapraid_sdev_put(sas_dev);
		leapraid_sdev_put(sas_dev);
	}
	spin_unlock_irqrestore(&adapter->dev_topo.sas_dev_lock, flags);
}

static void leapraid_target_destroy(struct scsi_target *tgt)
{
	struct leapraid_starget_priv *starget_priv;

	starget_priv = tgt->hostdata;
	if (!starget_priv)
		return;

	if (tgt->channel == RAID_CHANNEL) {
		leapraid_target_destroy_raid(tgt);
		goto out_free;
	}

	leapraid_target_destroy_sas(tgt);

out_free:
	kfree(starget_priv);
	tgt->hostdata = NULL;
}

static bool leapraid_scan_check_status(struct leapraid_adapter *adapter,
				       bool *need_hard_reset)
{
	u32 adapter_state;

	if (adapter->scan_dev_desc.scan_start) {
		adapter_state = leapraid_get_adapter_state(adapter);
		if (adapter_state == LEAPRAID_DB_FAULT) {
			*need_hard_reset = true;
			return true;
		}
		return false;
	}

	if (adapter->driver_cmds.scan_dev_cmd.status & LEAPRAID_CMD_RESET) {
		dev_err(&adapter->pdev->dev,
			"Device scan: Aborted due to reset\n");
		adapter->driver_cmds.scan_dev_cmd.status =
			LEAPRAID_CMD_NOT_USED;
		adapter->scan_dev_desc.driver_loading = 0;
		wake_up(&adapter->scan_dev_desc.wait_driver_loading);
		return true;
	}

	if (adapter->scan_dev_desc.scan_start_failed) {
		dev_err(&adapter->pdev->dev,
			"Device scan: Failed with adapter_status=0x%08x\n",
			adapter->scan_dev_desc.scan_start_failed);
		adapter->scan_dev_desc.driver_loading = 0;
		wake_up(&adapter->scan_dev_desc.wait_driver_loading);
		adapter->scan_dev_desc.wait_scan_dev_done = 0;
		adapter->access_ctrl.host_removing = 1;
		return true;
	}

	adapter->driver_cmds.scan_dev_cmd.status = LEAPRAID_CMD_NOT_USED;
	leapraid_scan_dev_done(adapter);
	return true;
}

static int leapraid_scan_finished(struct Scsi_Host *shost, unsigned long time)
{
	struct leapraid_adapter *adapter = shost_priv(shost);
	bool need_hard_reset = false;

	if (time >= (LEAPRAID_SCAN_DEV_CMD_TIMEOUT * HZ)) {
		adapter->driver_cmds.scan_dev_cmd.status =
			LEAPRAID_CMD_NOT_USED;
		dev_err(&adapter->pdev->dev,
			"Device scan: Failed with timeout 300s\n");
		adapter->scan_dev_desc.driver_loading = 0;
		wake_up(&adapter->scan_dev_desc.wait_driver_loading);
		return 1;
	}

	if (!leapraid_scan_check_status(adapter, &need_hard_reset))
		return 0;

	if (need_hard_reset) {
		adapter->driver_cmds.scan_dev_cmd.status =
			LEAPRAID_CMD_NOT_USED;
		dev_info(&adapter->pdev->dev, "%s:%d: call hard_reset\n",
			 __func__, __LINE__);
		if (leapraid_hard_reset_handler(adapter, PART_RESET)) {
			adapter->scan_dev_desc.driver_loading = 0;
			wake_up(&adapter->scan_dev_desc.wait_driver_loading);
		}
	}

	return 1;
}

static void leapraid_scan_start(struct Scsi_Host *shost)
{
	struct leapraid_adapter *adapter = shost_priv(shost);

	adapter->scan_dev_desc.scan_start = 1;
	leapraid_scan_dev(adapter, true);
}

static u32 leapraid_get_raid_qd(struct leapraid_adapter *adapter,
				const struct leapraid_raid_volume *raid_volume,
				bool default_qd)
{
	const struct leapraid_adapter_attr *attr = &adapter->adapter_attr;

	if (raid_volume->vol_type != LEAPRAID_VOL_TYPE_RAID0)
		return default_qd ? LEAPRAID_RAID_QUEUE_DEPTH :
			attr->raid_volume_max_queue_depth;

	if (raid_volume->dev_info & LEAPRAID_DEVTYP_SSP_TGT)
		return default_qd ? LEAPRAID_SAS_QUEUE_DEPTH :
			attr->narrowport_max_queue_depth;

	return default_qd ? LEAPRAID_SATA_QUEUE_DEPTH :
		attr->sata_max_queue_depth;
}

static int leapraid_calc_max_queue_depth(struct scsi_device *sdev, int qdepth)
{
	struct Scsi_Host *shost;
	struct leapraid_adapter *adapter;
	struct leapraid_starget_priv *starget_priv;
	struct leapraid_sdev_priv *sdev_priv;
	struct leapraid_raid_volume *raid_volume;
	struct leapraid_sas_dev *sas_dev;
	int max_depth;
	u32 default_qdepth = 0;
	u32 fw_qdepth = 0;

	shost = sdev->host;
	adapter = shost_priv(shost);
	max_depth = shost->can_queue;

	sdev_priv = sdev->hostdata;
	if (!sdev_priv)
		goto out_tag_check;

	starget_priv = sdev_priv->starget_priv;
	if (!starget_priv)
		goto out_tag_check;

	if (starget_priv->flg & LEAPRAID_TGT_FLG_VOLUME) {
		raid_volume = leapraid_raid_volume_find_by_hdl(
					adapter, starget_priv->hdl);
		if (raid_volume) {
			default_qdepth = leapraid_get_raid_qd(
					adapter, raid_volume, true);
			fw_qdepth = leapraid_get_raid_qd(
					adapter, raid_volume, false);
			leapraid_raid_volume_put(raid_volume);
		}
		goto out_limit_check;
	}

	sas_dev = leapraid_get_sas_dev_from_tgt(adapter, starget_priv);
	if (sas_dev) {
		if (sas_dev->dev_info & LEAPRAID_DEVTYP_SSP_TGT) {
			default_qdepth = LEAPRAID_SAS_QUEUE_DEPTH;
			fw_qdepth = (sas_dev->port_connection > 1) ?
			      adapter->adapter_attr.wideport_max_queue_depth :
			      adapter->adapter_attr.narrowport_max_queue_depth;
		}
		if (sas_dev->dev_info & LEAPRAID_DEVTYP_SATA_DEV) {
			default_qdepth = LEAPRAID_SATA_QUEUE_DEPTH;
			fw_qdepth = adapter->adapter_attr.sata_max_queue_depth;
		}
		leapraid_sdev_put(sas_dev);
	}

out_limit_check:
	if (fw_qdepth > shost->can_queue && default_qdepth)
		fw_qdepth = default_qdepth;

	if (fw_qdepth)
		max_depth = min_t(int, max_depth, fw_qdepth);

out_tag_check:
	if (!sdev->tagged_supported)
		max_depth = 1;

	if (qdepth > max_depth)
		qdepth = max_depth;

	return qdepth;
}

int leapraid_change_queue_depth(struct scsi_device *sdev, int qdepth)
{
	qdepth = leapraid_calc_max_queue_depth(sdev, qdepth);
	scsi_change_queue_depth(sdev, qdepth);
	return sdev->queue_depth;
}

static void leapraid_map_queues(struct Scsi_Host *shost)
{
	struct leapraid_adapter *adapter;
	struct blk_mq_queue_map *queue_map;
	int msix_queue_count;
	int poll_queue_count;
	int queue_offset;
	int map_index;

	adapter = (struct leapraid_adapter *)shost->hostdata;
	if (shost->nr_hw_queues == 1)
		return;

	msix_queue_count = adapter->notification_desc.iopoll_qdex;
	poll_queue_count = adapter->adapter_attr.rq_cnt - msix_queue_count;

	queue_offset = 0;
	for (map_index = 0; map_index < shost->nr_maps; map_index++) {
		queue_map = &shost->tag_set.map[map_index];
		queue_map->nr_queues = 0;

		switch (map_index) {
		case HCTX_TYPE_DEFAULT:
			queue_map->nr_queues = msix_queue_count;
			queue_map->queue_offset = queue_offset;
			WARN_ON_ONCE(!queue_map->nr_queues);
			blk_mq_map_hw_queues(queue_map,
					     &adapter->pdev->dev, 0);
			break;
		case HCTX_TYPE_POLL:
			queue_map->nr_queues = poll_queue_count;
			queue_map->queue_offset = queue_offset;
			blk_mq_map_queues(queue_map);
			break;
		default:
			queue_map->queue_offset = queue_offset;
			blk_mq_map_hw_queues(queue_map,
					     &adapter->pdev->dev, 0);
			break;
		}
		queue_offset += queue_map->nr_queues;
	}
}

int leapraid_blk_mq_poll(struct Scsi_Host *shost, unsigned int queue_num)
{
	struct leapraid_adapter *adapter =
		(struct leapraid_adapter *)shost->hostdata;
	struct leapraid_blk_mq_poll_rq *blk_mq_poll_rq;
	int num_entries;
	int qid = queue_num - adapter->notification_desc.iopoll_qdex;

	blk_mq_poll_rq = &adapter->notification_desc.blk_mq_poll_rqs[qid];
	if (atomic_read(&blk_mq_poll_rq->pause) ||
	    !atomic_add_unless(&blk_mq_poll_rq->busy, 1, 1))
		return 0;

	num_entries = leapraid_rep_queue_handler(&blk_mq_poll_rq->rq);
	atomic_dec(&blk_mq_poll_rq->busy);
	return num_entries;
}

static int leapraid_bios_param(struct scsi_device *sdev,
			       struct gendisk *disk, sector_t capacity,
			       int geom[])
{
	int heads;
	int sectors;
	sector_t cylinders;

	if (scsi_partsize(disk, capacity, geom))
		return 0;

	if ((ulong)capacity >= LEAPRAID_LARGE_DISK_THRESHOLD) {
		heads = LEAPRAID_LARGE_DISK_HEADS;
		sectors = LEAPRAID_LARGE_DISK_SECTORS;
	} else {
		heads = LEAPRAID_SMALL_DISK_HEADS;
		sectors = LEAPRAID_SMALL_DISK_SECTORS;
	}

	cylinders = capacity;
	sector_div(cylinders, heads * sectors);

	geom[0] = heads;
	geom[1] = sectors;
	geom[2] = cylinders;
	return 0;
}

static ssize_t fw_queue_depth_show(struct device *cdev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct leapraid_adapter *adapter = shost_priv(shost);

	return sysfs_emit(buf, "%02d\n",
			  adapter->adapter_attr.features.req_slot);
}

static ssize_t host_sas_address_show(struct device *cdev,
				     struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct leapraid_adapter *adapter = shost_priv(shost);

	return sysfs_emit(buf, "0x%016llx\n",
		(unsigned long long)adapter->dev_topo.card.sas_address);
}

static ssize_t board_name_show(struct device *cdev,
			       struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct leapraid_adapter *adapter = shost_priv(shost);

	return sysfs_emit(buf, "%s\n", adapter->adapter_attr.board_name);
}

static DEVICE_ATTR_RO(fw_queue_depth);
static DEVICE_ATTR_RO(host_sas_address);
static DEVICE_ATTR_RO(board_name);

static struct attribute *leapraid_shost_attrs[] = {
	&dev_attr_fw_queue_depth.attr,
	&dev_attr_host_sas_address.attr,
	&dev_attr_board_name.attr,
	NULL,
};

ATTRIBUTE_GROUPS(leapraid_shost);

static ssize_t sas_device_handle_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct leapraid_sdev_priv *sas_device_priv_data = sdev->hostdata;

	if (!sas_device_priv_data || !sas_device_priv_data->starget_priv) {
		dev_err(&sdev->sdev_gendev,
			"%s: Invalid sdev_priv or starget_priv\n", __func__);
		return -EINVAL;
	}

	return sysfs_emit(buf, "0x%04x\n",
			  sas_device_priv_data->starget_priv->hdl);
}

static ssize_t sas_ncq_prio_supported_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);

	return sysfs_emit(buf, "%d\n", sas_ata_ncq_prio_supported(sdev));
}

static ssize_t sas_ncq_prio_enable_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct leapraid_sdev_priv *sas_device_priv_data = sdev->hostdata;

	if (!sas_device_priv_data) {
		dev_err(&sdev->sdev_gendev,
			"%s: Invalid sdev_priv\n", __func__);
		return -EINVAL;
	}

	return sysfs_emit(buf, "%d\n", sas_device_priv_data->ncq_prio_enable);
}

static ssize_t sas_ncq_prio_enable_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct leapraid_sdev_priv *sas_device_priv_data = sdev->hostdata;
	bool enable;

	if (!sas_device_priv_data) {
		dev_err(&sdev->sdev_gendev,
			"%s: Invalid sdev_priv\n", __func__);
		return -EINVAL;
	}

	if (kstrtobool(buf, &enable))
		return -EINVAL;

	if (!sas_ata_ncq_prio_supported(sdev))
		return -EINVAL;

	sas_device_priv_data->ncq_prio_enable = enable;
	return count;
}

static DEVICE_ATTR_RO(sas_device_handle);
static DEVICE_ATTR_RO(sas_ncq_prio_supported);
static DEVICE_ATTR_RW(sas_ncq_prio_enable);

static bool leapraid_sdev_is_sata(struct scsi_device *sdev)
{
	struct scsi_target *starget = sdev->sdev_target;
	struct leapraid_starget_priv *starget_priv = starget->hostdata;
	struct leapraid_sas_dev *sas_dev;

	if (!starget_priv)
		return false;

	sas_dev = starget_priv->sas_dev;
	return sas_dev && (sas_dev->dev_info & LEAPRAID_DEVTYP_SATA_DEV);
}

static struct attribute *leapraid_sdev_attrs[] = {
	&dev_attr_sas_device_handle.attr,
	&dev_attr_sas_ncq_prio_supported.attr,
	&dev_attr_sas_ncq_prio_enable.attr,
	NULL,
};

static umode_t leapraid_sdev_attr_is_visible(struct kobject *kobj,
					     struct attribute *attr, int i)
{
	struct device *dev = kobj_to_dev(kobj);
	struct scsi_device *sdev = to_scsi_device(dev);

	if (attr == &dev_attr_sas_ncq_prio_supported.attr ||
	    attr == &dev_attr_sas_ncq_prio_enable.attr)
		if (!leapraid_sdev_is_sata(sdev))
			return 0;

	return attr->mode;
}

static const struct attribute_group leapraid_sdev_attr_group = {
	.attrs = leapraid_sdev_attrs,
	.is_visible = leapraid_sdev_attr_is_visible,
};

static const struct attribute_group *leapraid_sdev_groups[] = {
	&leapraid_sdev_attr_group,
	NULL,
};

static struct scsi_host_template leapraid_driver_template = {
	.module = THIS_MODULE,
	.name = "LEAPIO RAID Host",
	.proc_name = LEAPRAID_DRIVER_NAME,
	.queuecommand = leapraid_queuecommand,
	.cmd_size = sizeof(struct leapraid_io_req_tracker),
	.init_cmd_priv = leapraid_init_cmd_priv,
	.exit_cmd_priv = leapraid_exit_cmd_priv,
	.eh_abort_handler = leapraid_eh_abort_handler,
	.eh_device_reset_handler = leapraid_eh_device_reset_handler,
	.eh_target_reset_handler = leapraid_eh_target_reset_handler,
	.eh_host_reset_handler = leapraid_eh_host_reset_handler,
	.sdev_init = leapraid_sdev_init,
	.sdev_destroy = leapraid_sdev_destroy,
	.sdev_configure = leapraid_sdev_configure,
	.target_alloc = leapraid_target_alloc,
	.target_destroy = leapraid_target_destroy,
	.scan_finished = leapraid_scan_finished,
	.scan_start = leapraid_scan_start,
	.change_queue_depth = leapraid_change_queue_depth,
	.map_queues = leapraid_map_queues,
	.mq_poll = leapraid_blk_mq_poll,
	.bios_param = leapraid_bios_param,
	.can_queue = LEAPRAID_CAN_QUEUE_MIN,
	.this_id = LEAPRAID_THIS_ID_NONE,
	.sg_tablesize = LEAPRAID_SG_DEPTH,
	.max_sectors = LEAPRAID_MAX_SECTORS,
	.max_segment_size = LEAPRAID_MAX_SEGMENT_SIZE,
	.cmd_per_lun = LEAPRAID_CMD_PER_LUN,
	.shost_groups = leapraid_shost_groups,
	.sdev_groups = leapraid_sdev_groups,
	.track_queue_depth = 1,
};

static void leapraid_lock_init(struct leapraid_adapter *adapter)
{
	mutex_init(&adapter->reset_desc.adapter_reset_mutex);
	mutex_init(&adapter->reset_desc.host_diag_mutex);
	mutex_init(&adapter->access_ctrl.pci_access_lock);

	spin_lock_init(&adapter->reset_desc.adapter_reset_lock);
	spin_lock_init(&adapter->dynamic_task_desc.task_lock);
	spin_lock_init(&adapter->dev_topo.sas_dev_lock);
	spin_lock_init(&adapter->dev_topo.topo_node_lock);
	spin_lock_init(&adapter->fw_evt_s.fw_evt_lock);
	spin_lock_init(&adapter->dev_topo.raid_volume_lock);
	spin_lock_init(&adapter->dev_topo.enc_lock);
	spin_lock_init(&adapter->boot_devs.lock);
}

static void leapraid_list_init(struct leapraid_adapter *adapter)
{
	INIT_LIST_HEAD(&adapter->dev_topo.sas_dev_list);
	INIT_LIST_HEAD(&adapter->dev_topo.card_port_list);
	INIT_LIST_HEAD(&adapter->dev_topo.sas_dev_init_list);
	INIT_LIST_HEAD(&adapter->dev_topo.exp_list);
	INIT_LIST_HEAD(&adapter->dev_topo.enc_list);
	INIT_LIST_HEAD(&adapter->fw_evt_s.fw_evt_list);
	INIT_LIST_HEAD(&adapter->dev_topo.raid_volume_list);
	INIT_LIST_HEAD(&adapter->dev_topo.card.sas_port_list);
}

static int leapraid_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct leapraid_adapter *adapter;
	struct Scsi_Host *shost;
	int iopoll_q_count;
	int rc;

	shost = scsi_host_alloc(&leapraid_driver_template,
				sizeof(struct leapraid_adapter));
	if (!shost) {
		dev_err(&pdev->dev,
			"%s: SCSI host alloc failed\n", __func__);
		return -ENODEV;
	}

	adapter = shost_priv(shost);
	memset(adapter, 0, sizeof(struct leapraid_adapter));

	init_waitqueue_head(&adapter->access_ctrl.recovery_waitq);
	adapter->adapter_attr.id = atomic_inc_return(&leapraid_ids) - 1;

	adapter->adapter_attr.enable_mp = enable_mp;

	adapter = shost_priv(shost);
	INIT_LIST_HEAD(&adapter->list);

	adapter->shost = shost;
	adapter->pdev = pdev;
	adapter->fw_log_desc.open_pcie_trace = open_pcie_trace;
	atomic_set(&adapter->fw_log_desc.mmap_refcnt, 0);
	init_waitqueue_head(&adapter->fw_log_desc.mmap_waitq);
	leapraid_lock_init(adapter);
	leapraid_list_init(adapter);
	snprintf(adapter->adapter_attr.name, LEAPRAID_NAME_LENGTH, "%s%d",
		 LEAPRAID_DRIVER_NAME, adapter->adapter_attr.id);

	shost->max_cmd_len = LEAPRAID_MAX_CDB_LEN;
	shost->max_lun = LEAPRAID_MAX_LUNS;
	shost->transportt = leapraid_transport_template;
	shost->unique_id = adapter->adapter_attr.id;

	snprintf(adapter->fw_evt_s.fw_evt_name,
		 sizeof(adapter->fw_evt_s.fw_evt_name),
		 "fw_event_%s%d", LEAPRAID_DRIVER_NAME,
		 adapter->adapter_attr.id);
	adapter->fw_evt_s.fw_evt_thread =
		alloc_ordered_workqueue(adapter->fw_evt_s.fw_evt_name, 0);
	if (!adapter->fw_evt_s.fw_evt_thread) {
		dev_err(&adapter->pdev->dev,
			"%s: Failed to create fw event workqueue\n", __func__);
		rc = -ENODEV;
		goto evt_wq_fail;
	}

	shost->host_tagset = 1;
	init_waitqueue_head(&adapter->scan_dev_desc.wait_driver_loading);
	adapter->scan_dev_desc.driver_loading = 1;
	if (leapraid_ctrl_init(adapter)) {
		dev_err(&adapter->pdev->dev,
			"%s: Adapter init failed\n", __func__);
		rc = -ENODEV;
		goto ctrl_init_fail;
	}

	shost->nr_hw_queues = 1;
	if (shost->host_tagset) {
		shost->nr_hw_queues = adapter->adapter_attr.rq_cnt;
		iopoll_q_count = adapter->adapter_attr.rq_cnt -
				 adapter->notification_desc.iopoll_qdex;
		shost->nr_maps = iopoll_q_count ? 3 : 1;
		dev_info(&adapter->pdev->dev,
			 "Max scsi I/O cmds %d shared with nr_hw_queues=%d\n",
			 shost->can_queue, shost->nr_hw_queues);
	}

	rc = scsi_add_host(shost, &pdev->dev);
	if (rc) {
		dev_err(&pdev->dev,
			"%s: SCSI host add failed\n", __func__);
		goto scsi_add_shost_fail;
	}

	spin_lock(&leapraid_adapter_lock);
	list_add_tail(&adapter->list, &leapraid_adapter_list);
	spin_unlock(&leapraid_adapter_lock);

	scsi_scan_host(shost);
	return 0;

scsi_add_shost_fail:
	leapraid_remove_ctrl(adapter);
ctrl_init_fail:
	leapraid_overheat_cleanup(adapter);
	destroy_workqueue(adapter->fw_evt_s.fw_evt_thread);
evt_wq_fail:
	scsi_host_put(shost);
	return rc;
}

void leapraid_cleanup_lists(struct leapraid_adapter *adapter)
{
	struct leapraid_raid_volume *raid_volume, *next_raid_volume;
	struct leapraid_starget_priv *starget_priv_data;
	struct leapraid_sas_port *leapraid_port, *next_port;
	struct leapraid_card_port *port, *port_next;
	struct leapraid_vphy *vphy, *vphy_next;

	list_for_each_entry_safe(raid_volume, next_raid_volume,
				 &adapter->dev_topo.raid_volume_list, list) {
		if (raid_volume->starget) {
			starget_priv_data = raid_volume->starget->hostdata;
			if (starget_priv_data)
				starget_priv_data->deleted = 1;
			scsi_remove_target(&raid_volume->starget->dev);
		}
		dev_info(&adapter->pdev->dev,
			 "removing hdl=0x%04x, wwid=0x%016llx\n",
			 raid_volume->hdl,
			 (unsigned long long)raid_volume->wwid);
		leapraid_raid_volume_remove(adapter, raid_volume);
	}

	list_for_each_entry_safe(leapraid_port, next_port,
				 &adapter->dev_topo.card.sas_port_list,
				 port_list) {
		if (leapraid_port->remote_identify.device_type ==
		    SAS_END_DEVICE)
			leapraid_sas_dev_remove_by_sas_address(
				adapter,
				leapraid_port->remote_identify.sas_address,
				leapraid_port->card_port);
		else if (leapraid_port->remote_identify.device_type ==
				SAS_EDGE_EXPANDER_DEVICE ||
			 leapraid_port->remote_identify.device_type ==
				SAS_FANOUT_EXPANDER_DEVICE)
			leapraid_exp_rm(
				adapter,
				leapraid_port->remote_identify.sas_address,
				leapraid_port->card_port);
	}

	list_for_each_entry_safe(port, port_next,
				 &adapter->dev_topo.card_port_list, list) {
		if (port->vphys_mask)
			list_for_each_entry_safe(vphy, vphy_next,
						 &port->vphys_list, list) {
				list_del(&vphy->list);
				kfree(vphy);
			}
		list_del(&port->list);
		kfree(port);
	}

	if (adapter->dev_topo.card.phys_num) {
		kfree(adapter->dev_topo.card.card_phy);
		adapter->dev_topo.card.card_phy = NULL;
		adapter->dev_topo.card.phys_num = 0;
	}
}

static void leapraid_remove(struct pci_dev *pdev)
{
	struct leapraid_adapter *adapter = pdev_to_adapter(pdev);
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct workqueue_struct *wq;
	unsigned long flags;

	if (!shost || !adapter) {
		dev_err(&pdev->dev, "Unable to remove!\n");
		return;
	}

	wait_event(adapter->scan_dev_desc.wait_driver_loading,
		   !adapter->scan_dev_desc.driver_loading);
	wait_event(adapter->scan_dev_desc.wait_driver_loading,
		   !atomic_read(&adapter->overheat_desc.thermal_alert));

	WRITE_ONCE(adapter->access_ctrl.host_removing, 1);
	spin_lock(&leapraid_adapter_lock);
	list_del(&adapter->list);
	spin_unlock(&leapraid_adapter_lock);

	leapraid_wait_cmds_done(adapter);

	if (leapraid_pci_removed(adapter)) {
		leapraid_mq_polling_pause(adapter);
		leapraid_clean_active_scsi_cmds(adapter);
	}
	leapraid_clean_active_fw_evt(adapter);

	spin_lock_irqsave(&adapter->fw_evt_s.fw_evt_lock, flags);
	wq = adapter->fw_evt_s.fw_evt_thread;
	adapter->fw_evt_s.fw_evt_thread = NULL;
	spin_unlock_irqrestore(&adapter->fw_evt_s.fw_evt_lock, flags);
	if (wq)
		destroy_workqueue(wq);

	sas_remove_host(shost);
	leapraid_cleanup_lists(adapter);
	leapraid_remove_ctrl(adapter);
	scsi_host_put(shost);
}

static void leapraid_shutdown(struct pci_dev *pdev)
{
	struct leapraid_adapter *adapter = pdev_to_adapter(pdev);
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct workqueue_struct *wq;
	unsigned long flags;

	if (!shost || !adapter) {
		dev_err(&pdev->dev, "Unable to shutdown!\n");
		return;
	}

	adapter->access_ctrl.host_removing = 1;
	leapraid_wait_cmds_done(adapter);
	leapraid_clean_active_fw_evt(adapter);
	leapraid_overheat_cleanup(adapter);
	leapraid_fw_log_stop(adapter);
	spin_lock_irqsave(&adapter->fw_evt_s.fw_evt_lock, flags);
	wq = adapter->fw_evt_s.fw_evt_thread;
	adapter->fw_evt_s.fw_evt_thread = NULL;
	spin_unlock_irqrestore(&adapter->fw_evt_s.fw_evt_lock, flags);
	if (wq)
		destroy_workqueue(wq);

	leapraid_disable_controller(adapter);
}

static pci_ers_result_t leapraid_pci_error_detected(struct pci_dev *pdev,
						    pci_channel_state_t state)
{
	struct leapraid_adapter *adapter = pdev_to_adapter(pdev);
	struct Scsi_Host *shost = pci_get_drvdata(pdev);

	if (!shost || !adapter) {
		dev_err(&pdev->dev, "Failed to error detected for device\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	dev_err(&pdev->dev, "%s: PCI error detected, state=%d\n",
		adapter->adapter_attr.name, state);

	switch (state) {
	case pci_channel_io_normal:
		return PCI_ERS_RESULT_CAN_RECOVER;
	case pci_channel_io_frozen:
		adapter->access_ctrl.pcie_recovering = 1;
		scsi_block_requests(adapter->shost);
		leapraid_overheat_cleanup(adapter);
		leapraid_check_scheduled_fault_stop(adapter);
		leapraid_fw_log_stop(adapter);
		leapraid_disable_controller(adapter);
		return PCI_ERS_RESULT_NEED_RESET;
	case pci_channel_io_perm_failure:
		adapter->access_ctrl.pcie_recovering = 1;
		leapraid_overheat_cleanup(adapter);
		leapraid_check_scheduled_fault_stop(adapter);
		leapraid_fw_log_stop(adapter);
		leapraid_mq_polling_pause(adapter);
		leapraid_clean_active_scsi_cmds(adapter);
		return PCI_ERS_RESULT_DISCONNECT;
	}

	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t leapraid_pci_mmio_enabled(struct pci_dev *pdev)
{
	struct leapraid_adapter *adapter = pdev_to_adapter(pdev);
	struct Scsi_Host *shost = pci_get_drvdata(pdev);

	if (!shost || !adapter) {
		dev_err(&pdev->dev,
			"Failed to enable mmio for device\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	dev_info(&pdev->dev, "%s: PCI error mmio enabled\n",
		 adapter->adapter_attr.name);

	return PCI_ERS_RESULT_RECOVERED;
}

static pci_ers_result_t leapraid_pci_slot_reset(struct pci_dev *pdev)
{
	struct leapraid_adapter *adapter = pdev_to_adapter(pdev);
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	int rc;

	if (!shost || !adapter) {
		dev_err(&pdev->dev,
			"Failed to slot reset for device\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	dev_err(&pdev->dev, "%s PCI error slot reset\n",
		adapter->adapter_attr.name);

	adapter->pdev = pdev;
	pci_restore_state(pdev);
	if (leapraid_set_pcie_and_notification(adapter)) {
		dev_err(&pdev->dev,
			"%s: Failed to set PCIe state and notification\n",
			__func__);
		return PCI_ERS_RESULT_DISCONNECT;
	}

	adapter->access_ctrl.pcie_recovering = 0;
	dev_info(&pdev->dev, "%s: Hard reset triggered by PCI slot reset\n",
		 adapter->adapter_attr.name);
	dev_info(&adapter->pdev->dev, "%s: %d: call hard_reset\n",
		 __func__, __LINE__);
	rc = leapraid_hard_reset_handler(adapter, FULL_RESET);
	if (rc)
		dev_err(&pdev->dev, "%s hard reset: failed\n",
			adapter->adapter_attr.name);

	return rc == 0 ? PCI_ERS_RESULT_RECOVERED :
		 PCI_ERS_RESULT_DISCONNECT;
}

static void leapraid_pci_resume(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct leapraid_adapter *adapter = pdev_to_adapter(pdev);

	if (!shost || !adapter) {
		dev_err(&pdev->dev, "Failed to resume\n");
		return;
	}

	dev_err(&pdev->dev, "PCI error resume!\n");

	pci_aer_clear_nonfatal_status(pdev);
	leapraid_check_scheduled_fault_start(adapter);
	leapraid_fw_log_start(adapter);
	scsi_unblock_requests(adapter->shost);
}

MODULE_DEVICE_TABLE(pci, leapraid_pci_table);
static struct pci_error_handlers leapraid_err_handler = {
	.error_detected = leapraid_pci_error_detected,
	.mmio_enabled = leapraid_pci_mmio_enabled,
	.slot_reset = leapraid_pci_slot_reset,
	.resume = leapraid_pci_resume,
};

#ifdef CONFIG_PM
static int leapraid_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct leapraid_adapter *adapter = pdev_to_adapter(pdev);
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	pci_power_t device_state;

	if (!shost || !adapter) {
		dev_err(&pdev->dev,
			"Suspend failed, invalid host or adapter\n");
		return -ENXIO;
	}

	leapraid_overheat_cleanup(adapter);
	leapraid_check_scheduled_fault_stop(adapter);
	leapraid_fw_log_stop(adapter);
	scsi_block_requests(shost);
	device_state = pci_choose_state(pdev, state);

	dev_info(&pdev->dev, "Entering PCI power state D%d, (slot=%s)\n",
		 device_state, pci_name(pdev));

	pci_save_state(pdev);
	leapraid_disable_controller(adapter);
	pci_set_power_state(pdev, device_state);
	return 0;
}

static int leapraid_resume(struct pci_dev *pdev)
{
	struct leapraid_adapter *adapter = pdev_to_adapter(pdev);
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	pci_power_t device_state = pdev->current_state;
	int rc;

	if (!shost || !adapter) {
		dev_err(&pdev->dev,
			"Resume failed, invalid host or adapter\n");
		return -ENXIO;
	}

	dev_info(&pdev->dev,
		 "Resuming device %s, previous state D%d\n",
		 pci_name(pdev), device_state);

	pci_set_power_state(pdev, PCI_D0);
	pci_enable_wake(pdev, PCI_D0, 0);
	pci_restore_state(pdev);
	adapter->pdev = pdev;
	rc = leapraid_set_pcie_and_notification(adapter);
	if (rc) {
		dev_err(&pdev->dev,
			"%s: Failed to set PCIe state and notification\n",
			__func__);
		return rc;
	}

	dev_info(&adapter->pdev->dev, "%s:%d: call hard_reset\n",
		 __func__, __LINE__);
	rc = leapraid_hard_reset_handler(adapter, PART_RESET);
	if (rc) {
		dev_err(&adapter->pdev->dev,
			"%s: Hard reset failed during resume, rc=%d\n",
			__func__, rc);
		return rc;
	}
	scsi_unblock_requests(shost);
	leapraid_check_scheduled_fault_start(adapter);
	leapraid_fw_log_start(adapter);
	return 0;
}
#endif /* CONFIG_PM */

static struct pci_driver leapraid_driver = {
	.name = LEAPRAID_DRIVER_NAME,
	.id_table = leapraid_pci_table,
	.probe = leapraid_probe,
	.remove = leapraid_remove,
	.shutdown = leapraid_shutdown,
	.err_handler = &leapraid_err_handler,
#ifdef CONFIG_PM
	.suspend = leapraid_suspend,
	.resume = leapraid_resume,
#endif /* CONFIG_PM */
};

static int __init leapraid_init(void)
{
	int error;

	pr_info("%s initializing\n", LEAPRAID_DRIVER_NAME);

	leapraid_transport_template =
		sas_attach_transport(&leapraid_transport_functions);
	if (!leapraid_transport_template) {
		pr_err("%s: Failed to attach SAS transport\n",
		       LEAPRAID_DRIVER_NAME);
		return -ENODEV;
	}

	error = pci_register_driver(&leapraid_driver);
	if (error) {
		pr_err("%s: PCI driver registration failed: %d\n",
		       LEAPRAID_DRIVER_NAME, error);
		sas_release_transport(leapraid_transport_template);
		return error;
	}

	error = leapraid_ctl_init();
	if (error) {
		pci_unregister_driver(&leapraid_driver);
		sas_release_transport(leapraid_transport_template);
		return error;
	}

	return 0;
}

static void __exit leapraid_exit(void)
{
	pr_info("%s exiting\n", LEAPRAID_DRIVER_NAME);

	leapraid_ctl_exit();
	pci_unregister_driver(&leapraid_driver);
	sas_release_transport(leapraid_transport_template);
}

module_init(leapraid_init);
module_exit(leapraid_exit);
