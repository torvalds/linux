/*
 * Copyright (c) 2015 Linaro Ltd.
 * Copyright (c) 2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include "hisi_sas.h"
#define DRV_NAME "hisi_sas"

#define DEV_IS_GONE(dev) \
	((!dev) || (dev->dev_type == SAS_PHY_UNUSED))

static int hisi_sas_debug_issue_ssp_tmf(struct domain_device *device,
				u8 *lun, struct hisi_sas_tmf_task *tmf);
static int
hisi_sas_internal_task_abort(struct hisi_hba *hisi_hba,
			     struct domain_device *device,
			     int abort_flag, int tag);
static int hisi_sas_softreset_ata_disk(struct domain_device *device);
static int hisi_sas_control_phy(struct asd_sas_phy *sas_phy, enum phy_func func,
				void *funcdata);

u8 hisi_sas_get_ata_protocol(struct host_to_dev_fis *fis, int direction)
{
	switch (fis->command) {
	case ATA_CMD_FPDMA_WRITE:
	case ATA_CMD_FPDMA_READ:
	case ATA_CMD_FPDMA_RECV:
	case ATA_CMD_FPDMA_SEND:
	case ATA_CMD_NCQ_NON_DATA:
		return HISI_SAS_SATA_PROTOCOL_FPDMA;

	case ATA_CMD_DOWNLOAD_MICRO:
	case ATA_CMD_ID_ATA:
	case ATA_CMD_PMP_READ:
	case ATA_CMD_READ_LOG_EXT:
	case ATA_CMD_PIO_READ:
	case ATA_CMD_PIO_READ_EXT:
	case ATA_CMD_PMP_WRITE:
	case ATA_CMD_WRITE_LOG_EXT:
	case ATA_CMD_PIO_WRITE:
	case ATA_CMD_PIO_WRITE_EXT:
		return HISI_SAS_SATA_PROTOCOL_PIO;

	case ATA_CMD_DSM:
	case ATA_CMD_DOWNLOAD_MICRO_DMA:
	case ATA_CMD_PMP_READ_DMA:
	case ATA_CMD_PMP_WRITE_DMA:
	case ATA_CMD_READ:
	case ATA_CMD_READ_EXT:
	case ATA_CMD_READ_LOG_DMA_EXT:
	case ATA_CMD_READ_STREAM_DMA_EXT:
	case ATA_CMD_TRUSTED_RCV_DMA:
	case ATA_CMD_TRUSTED_SND_DMA:
	case ATA_CMD_WRITE:
	case ATA_CMD_WRITE_EXT:
	case ATA_CMD_WRITE_FUA_EXT:
	case ATA_CMD_WRITE_QUEUED:
	case ATA_CMD_WRITE_LOG_DMA_EXT:
	case ATA_CMD_WRITE_STREAM_DMA_EXT:
	case ATA_CMD_ZAC_MGMT_IN:
		return HISI_SAS_SATA_PROTOCOL_DMA;

	case ATA_CMD_CHK_POWER:
	case ATA_CMD_DEV_RESET:
	case ATA_CMD_EDD:
	case ATA_CMD_FLUSH:
	case ATA_CMD_FLUSH_EXT:
	case ATA_CMD_VERIFY:
	case ATA_CMD_VERIFY_EXT:
	case ATA_CMD_SET_FEATURES:
	case ATA_CMD_STANDBY:
	case ATA_CMD_STANDBYNOW1:
	case ATA_CMD_ZAC_MGMT_OUT:
		return HISI_SAS_SATA_PROTOCOL_NONDATA;

	case ATA_CMD_SET_MAX:
		switch (fis->features) {
		case ATA_SET_MAX_PASSWD:
		case ATA_SET_MAX_LOCK:
			return HISI_SAS_SATA_PROTOCOL_PIO;

		case ATA_SET_MAX_PASSWD_DMA:
		case ATA_SET_MAX_UNLOCK_DMA:
			return HISI_SAS_SATA_PROTOCOL_DMA;

		default:
			return HISI_SAS_SATA_PROTOCOL_NONDATA;
		}

	default:
	{
		if (direction == DMA_NONE)
			return HISI_SAS_SATA_PROTOCOL_NONDATA;
		return HISI_SAS_SATA_PROTOCOL_PIO;
	}
	}
}
EXPORT_SYMBOL_GPL(hisi_sas_get_ata_protocol);

void hisi_sas_sata_done(struct sas_task *task,
			    struct hisi_sas_slot *slot)
{
	struct task_status_struct *ts = &task->task_status;
	struct ata_task_resp *resp = (struct ata_task_resp *)ts->buf;
	struct hisi_sas_status_buffer *status_buf =
			hisi_sas_status_buf_addr_mem(slot);
	u8 *iu = &status_buf->iu[0];
	struct dev_to_host_fis *d2h =  (struct dev_to_host_fis *)iu;

	resp->frame_len = sizeof(struct dev_to_host_fis);
	memcpy(&resp->ending_fis[0], d2h, sizeof(struct dev_to_host_fis));

	ts->buf_valid_size = sizeof(*resp);
}
EXPORT_SYMBOL_GPL(hisi_sas_sata_done);

int hisi_sas_get_ncq_tag(struct sas_task *task, u32 *tag)
{
	struct ata_queued_cmd *qc = task->uldd_task;

	if (qc) {
		if (qc->tf.command == ATA_CMD_FPDMA_WRITE ||
			qc->tf.command == ATA_CMD_FPDMA_READ) {
			*tag = qc->tag;
			return 1;
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(hisi_sas_get_ncq_tag);

static struct hisi_hba *dev_to_hisi_hba(struct domain_device *device)
{
	return device->port->ha->lldd_ha;
}

struct hisi_sas_port *to_hisi_sas_port(struct asd_sas_port *sas_port)
{
	return container_of(sas_port, struct hisi_sas_port, sas_port);
}
EXPORT_SYMBOL_GPL(to_hisi_sas_port);

void hisi_sas_stop_phys(struct hisi_hba *hisi_hba)
{
	int phy_no;

	for (phy_no = 0; phy_no < hisi_hba->n_phy; phy_no++)
		hisi_hba->hw->phy_disable(hisi_hba, phy_no);
}
EXPORT_SYMBOL_GPL(hisi_sas_stop_phys);

static void hisi_sas_slot_index_clear(struct hisi_hba *hisi_hba, int slot_idx)
{
	void *bitmap = hisi_hba->slot_index_tags;

	clear_bit(slot_idx, bitmap);
}

static void hisi_sas_slot_index_free(struct hisi_hba *hisi_hba, int slot_idx)
{
	hisi_sas_slot_index_clear(hisi_hba, slot_idx);
}

static void hisi_sas_slot_index_set(struct hisi_hba *hisi_hba, int slot_idx)
{
	void *bitmap = hisi_hba->slot_index_tags;

	set_bit(slot_idx, bitmap);
}

static int hisi_sas_slot_index_alloc(struct hisi_hba *hisi_hba, int *slot_idx)
{
	unsigned int index;
	void *bitmap = hisi_hba->slot_index_tags;

	index = find_first_zero_bit(bitmap, hisi_hba->slot_index_count);
	if (index >= hisi_hba->slot_index_count)
		return -SAS_QUEUE_FULL;
	hisi_sas_slot_index_set(hisi_hba, index);
	*slot_idx = index;
	return 0;
}

static void hisi_sas_slot_index_init(struct hisi_hba *hisi_hba)
{
	int i;

	for (i = 0; i < hisi_hba->slot_index_count; ++i)
		hisi_sas_slot_index_clear(hisi_hba, i);
}

void hisi_sas_slot_task_free(struct hisi_hba *hisi_hba, struct sas_task *task,
			     struct hisi_sas_slot *slot)
{

	if (task) {
		struct device *dev = hisi_hba->dev;

		if (!task->lldd_task)
			return;

		task->lldd_task = NULL;

		if (!sas_protocol_ata(task->task_proto))
			if (slot->n_elem)
				dma_unmap_sg(dev, task->scatter,
					     task->num_scatter,
					     task->data_dir);
	}

	if (slot->buf)
		dma_pool_free(hisi_hba->buffer_pool, slot->buf, slot->buf_dma);

	list_del_init(&slot->entry);
	slot->buf = NULL;
	slot->task = NULL;
	slot->port = NULL;
	hisi_sas_slot_index_free(hisi_hba, slot->idx);

	/* slot memory is fully zeroed when it is reused */
}
EXPORT_SYMBOL_GPL(hisi_sas_slot_task_free);

static int hisi_sas_task_prep_smp(struct hisi_hba *hisi_hba,
				  struct hisi_sas_slot *slot)
{
	return hisi_hba->hw->prep_smp(hisi_hba, slot);
}

static int hisi_sas_task_prep_ssp(struct hisi_hba *hisi_hba,
				  struct hisi_sas_slot *slot, int is_tmf,
				  struct hisi_sas_tmf_task *tmf)
{
	return hisi_hba->hw->prep_ssp(hisi_hba, slot, is_tmf, tmf);
}

static int hisi_sas_task_prep_ata(struct hisi_hba *hisi_hba,
				  struct hisi_sas_slot *slot)
{
	return hisi_hba->hw->prep_stp(hisi_hba, slot);
}

static int hisi_sas_task_prep_abort(struct hisi_hba *hisi_hba,
		struct hisi_sas_slot *slot,
		int device_id, int abort_flag, int tag_to_abort)
{
	return hisi_hba->hw->prep_abort(hisi_hba, slot,
			device_id, abort_flag, tag_to_abort);
}

/*
 * This function will issue an abort TMF regardless of whether the
 * task is in the sdev or not. Then it will do the task complete
 * cleanup and callbacks.
 */
static void hisi_sas_slot_abort(struct work_struct *work)
{
	struct hisi_sas_slot *abort_slot =
		container_of(work, struct hisi_sas_slot, abort_slot);
	struct sas_task *task = abort_slot->task;
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(task->dev);
	struct scsi_cmnd *cmnd = task->uldd_task;
	struct hisi_sas_tmf_task tmf_task;
	struct scsi_lun lun;
	struct device *dev = hisi_hba->dev;
	int tag = abort_slot->idx;
	unsigned long flags;

	if (!(task->task_proto & SAS_PROTOCOL_SSP)) {
		dev_err(dev, "cannot abort slot for non-ssp task\n");
		goto out;
	}

	int_to_scsilun(cmnd->device->lun, &lun);
	tmf_task.tmf = TMF_ABORT_TASK;
	tmf_task.tag_of_task_to_be_managed = cpu_to_le16(tag);

	hisi_sas_debug_issue_ssp_tmf(task->dev, lun.scsi_lun, &tmf_task);
out:
	/* Do cleanup for this task */
	spin_lock_irqsave(&hisi_hba->lock, flags);
	hisi_sas_slot_task_free(hisi_hba, task, abort_slot);
	spin_unlock_irqrestore(&hisi_hba->lock, flags);
	if (task->task_done)
		task->task_done(task);
}

static int hisi_sas_task_prep(struct sas_task *task, struct hisi_sas_dq
		*dq, int is_tmf, struct hisi_sas_tmf_task *tmf,
		int *pass)
{
	struct hisi_hba *hisi_hba = dq->hisi_hba;
	struct domain_device *device = task->dev;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_sas_port *port;
	struct hisi_sas_slot *slot;
	struct hisi_sas_cmd_hdr	*cmd_hdr_base;
	struct asd_sas_port *sas_port = device->port;
	struct device *dev = hisi_hba->dev;
	int dlvry_queue_slot, dlvry_queue, n_elem = 0, rc, slot_idx;
	unsigned long flags;

	if (!sas_port) {
		struct task_status_struct *ts = &task->task_status;

		ts->resp = SAS_TASK_UNDELIVERED;
		ts->stat = SAS_PHY_DOWN;
		/*
		 * libsas will use dev->port, should
		 * not call task_done for sata
		 */
		if (device->dev_type != SAS_SATA_DEV)
			task->task_done(task);
		return -ECOMM;
	}

	if (DEV_IS_GONE(sas_dev)) {
		if (sas_dev)
			dev_info(dev, "task prep: device %d not ready\n",
				 sas_dev->device_id);
		else
			dev_info(dev, "task prep: device %016llx not ready\n",
				 SAS_ADDR(device->sas_addr));

		return -ECOMM;
	}

	port = to_hisi_sas_port(sas_port);
	if (port && !port->port_attached) {
		dev_info(dev, "task prep: %s port%d not attach device\n",
			 (dev_is_sata(device)) ?
			 "SATA/STP" : "SAS",
			 device->port->id);

		return -ECOMM;
	}

	if (!sas_protocol_ata(task->task_proto)) {
		if (task->num_scatter) {
			n_elem = dma_map_sg(dev, task->scatter,
					    task->num_scatter, task->data_dir);
			if (!n_elem) {
				rc = -ENOMEM;
				goto prep_out;
			}
		}
	} else
		n_elem = task->num_scatter;

	spin_lock_irqsave(&hisi_hba->lock, flags);
	if (hisi_hba->hw->slot_index_alloc)
		rc = hisi_hba->hw->slot_index_alloc(hisi_hba, &slot_idx,
						    device);
	else
		rc = hisi_sas_slot_index_alloc(hisi_hba, &slot_idx);
	if (rc) {
		spin_unlock_irqrestore(&hisi_hba->lock, flags);
		goto err_out;
	}
	spin_unlock_irqrestore(&hisi_hba->lock, flags);

	rc = hisi_hba->hw->get_free_slot(hisi_hba, dq);
	if (rc)
		goto err_out_tag;

	dlvry_queue = dq->id;
	dlvry_queue_slot = dq->wr_point;
	slot = &hisi_hba->slot_info[slot_idx];
	memset(slot, 0, sizeof(struct hisi_sas_slot));

	slot->idx = slot_idx;
	slot->n_elem = n_elem;
	slot->dlvry_queue = dlvry_queue;
	slot->dlvry_queue_slot = dlvry_queue_slot;
	cmd_hdr_base = hisi_hba->cmd_hdr[dlvry_queue];
	slot->cmd_hdr = &cmd_hdr_base[dlvry_queue_slot];
	slot->task = task;
	slot->port = port;
	if (is_tmf)
		slot->is_internal = true;
	task->lldd_task = slot;
	INIT_WORK(&slot->abort_slot, hisi_sas_slot_abort);

	slot->buf = dma_pool_alloc(hisi_hba->buffer_pool,
				   GFP_ATOMIC, &slot->buf_dma);
	if (!slot->buf) {
		rc = -ENOMEM;
		goto err_out_slot_buf;
	}
	memset(slot->cmd_hdr, 0, sizeof(struct hisi_sas_cmd_hdr));
	memset(hisi_sas_cmd_hdr_addr_mem(slot), 0, HISI_SAS_COMMAND_TABLE_SZ);
	memset(hisi_sas_status_buf_addr_mem(slot), 0, HISI_SAS_STATUS_BUF_SZ);

	switch (task->task_proto) {
	case SAS_PROTOCOL_SMP:
		rc = hisi_sas_task_prep_smp(hisi_hba, slot);
		break;
	case SAS_PROTOCOL_SSP:
		rc = hisi_sas_task_prep_ssp(hisi_hba, slot, is_tmf, tmf);
		break;
	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SATA | SAS_PROTOCOL_STP:
		rc = hisi_sas_task_prep_ata(hisi_hba, slot);
		break;
	default:
		dev_err(dev, "task prep: unknown/unsupported proto (0x%x)\n",
			task->task_proto);
		rc = -EINVAL;
		break;
	}

	if (rc) {
		dev_err(dev, "task prep: rc = 0x%x\n", rc);
		goto err_out_buf;
	}

	spin_lock_irqsave(&hisi_hba->lock, flags);
	list_add_tail(&slot->entry, &sas_dev->list);
	spin_unlock_irqrestore(&hisi_hba->lock, flags);
	spin_lock_irqsave(&task->task_state_lock, flags);
	task->task_state_flags |= SAS_TASK_AT_INITIATOR;
	spin_unlock_irqrestore(&task->task_state_lock, flags);

	dq->slot_prep = slot;
	++(*pass);

	return 0;

err_out_buf:
	dma_pool_free(hisi_hba->buffer_pool, slot->buf,
		slot->buf_dma);
err_out_slot_buf:
	/* Nothing to be done */
err_out_tag:
	spin_lock_irqsave(&hisi_hba->lock, flags);
	hisi_sas_slot_index_free(hisi_hba, slot_idx);
	spin_unlock_irqrestore(&hisi_hba->lock, flags);
err_out:
	dev_err(dev, "task prep: failed[%d]!\n", rc);
	if (!sas_protocol_ata(task->task_proto))
		if (n_elem)
			dma_unmap_sg(dev, task->scatter,
				     task->num_scatter,
				     task->data_dir);
prep_out:
	return rc;
}

static int hisi_sas_task_exec(struct sas_task *task, gfp_t gfp_flags,
			      int is_tmf, struct hisi_sas_tmf_task *tmf)
{
	u32 rc;
	u32 pass = 0;
	unsigned long flags;
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(task->dev);
	struct device *dev = hisi_hba->dev;
	struct domain_device *device = task->dev;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_sas_dq *dq = sas_dev->dq;

	if (unlikely(test_bit(HISI_SAS_REJECT_CMD_BIT, &hisi_hba->flags)))
		return -EINVAL;

	/* protect task_prep and start_delivery sequence */
	spin_lock_irqsave(&dq->lock, flags);
	rc = hisi_sas_task_prep(task, dq, is_tmf, tmf, &pass);
	if (rc)
		dev_err(dev, "task exec: failed[%d]!\n", rc);

	if (likely(pass))
		hisi_hba->hw->start_delivery(dq);
	spin_unlock_irqrestore(&dq->lock, flags);

	return rc;
}

static void hisi_sas_bytes_dmaed(struct hisi_hba *hisi_hba, int phy_no)
{
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	struct sas_ha_struct *sas_ha;

	if (!phy->phy_attached)
		return;

	sas_ha = &hisi_hba->sha;
	sas_ha->notify_phy_event(sas_phy, PHYE_OOB_DONE);

	if (sas_phy->phy) {
		struct sas_phy *sphy = sas_phy->phy;

		sphy->negotiated_linkrate = sas_phy->linkrate;
		sphy->minimum_linkrate_hw = SAS_LINK_RATE_1_5_GBPS;
		sphy->maximum_linkrate_hw =
			hisi_hba->hw->phy_get_max_linkrate();
		if (sphy->minimum_linkrate == SAS_LINK_RATE_UNKNOWN)
			sphy->minimum_linkrate = phy->minimum_linkrate;

		if (sphy->maximum_linkrate == SAS_LINK_RATE_UNKNOWN)
			sphy->maximum_linkrate = phy->maximum_linkrate;
	}

	if (phy->phy_type & PORT_TYPE_SAS) {
		struct sas_identify_frame *id;

		id = (struct sas_identify_frame *)phy->frame_rcvd;
		id->dev_type = phy->identify.device_type;
		id->initiator_bits = SAS_PROTOCOL_ALL;
		id->target_bits = phy->identify.target_port_protocols;
	} else if (phy->phy_type & PORT_TYPE_SATA) {
		/*Nothing*/
	}

	sas_phy->frame_rcvd_size = phy->frame_rcvd_size;
	sas_ha->notify_port_event(sas_phy, PORTE_BYTES_DMAED);
}

static struct hisi_sas_device *hisi_sas_alloc_dev(struct domain_device *device)
{
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(device);
	struct hisi_sas_device *sas_dev = NULL;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&hisi_hba->lock, flags);
	for (i = 0; i < HISI_SAS_MAX_DEVICES; i++) {
		if (hisi_hba->devices[i].dev_type == SAS_PHY_UNUSED) {
			int queue = i % hisi_hba->queue_count;
			struct hisi_sas_dq *dq = &hisi_hba->dq[queue];

			hisi_hba->devices[i].device_id = i;
			sas_dev = &hisi_hba->devices[i];
			sas_dev->dev_status = HISI_SAS_DEV_NORMAL;
			sas_dev->dev_type = device->dev_type;
			sas_dev->hisi_hba = hisi_hba;
			sas_dev->sas_device = device;
			sas_dev->dq = dq;
			INIT_LIST_HEAD(&hisi_hba->devices[i].list);
			break;
		}
	}
	spin_unlock_irqrestore(&hisi_hba->lock, flags);

	return sas_dev;
}

static int hisi_sas_dev_found(struct domain_device *device)
{
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(device);
	struct domain_device *parent_dev = device->parent;
	struct hisi_sas_device *sas_dev;
	struct device *dev = hisi_hba->dev;

	if (hisi_hba->hw->alloc_dev)
		sas_dev = hisi_hba->hw->alloc_dev(device);
	else
		sas_dev = hisi_sas_alloc_dev(device);
	if (!sas_dev) {
		dev_err(dev, "fail alloc dev: max support %d devices\n",
			HISI_SAS_MAX_DEVICES);
		return -EINVAL;
	}

	device->lldd_dev = sas_dev;
	hisi_hba->hw->setup_itct(hisi_hba, sas_dev);

	if (parent_dev && DEV_IS_EXPANDER(parent_dev->dev_type)) {
		int phy_no;
		u8 phy_num = parent_dev->ex_dev.num_phys;
		struct ex_phy *phy;

		for (phy_no = 0; phy_no < phy_num; phy_no++) {
			phy = &parent_dev->ex_dev.ex_phy[phy_no];
			if (SAS_ADDR(phy->attached_sas_addr) ==
				SAS_ADDR(device->sas_addr))
				break;
		}

		if (phy_no == phy_num) {
			dev_info(dev, "dev found: no attached "
				 "dev:%016llx at ex:%016llx\n",
				 SAS_ADDR(device->sas_addr),
				 SAS_ADDR(parent_dev->sas_addr));
			return -EINVAL;
		}
	}

	dev_info(dev, "dev[%d:%x] found\n",
		sas_dev->device_id, sas_dev->dev_type);

	return 0;
}

static int hisi_sas_slave_configure(struct scsi_device *sdev)
{
	struct domain_device *dev = sdev_to_domain_dev(sdev);
	int ret = sas_slave_configure(sdev);

	if (ret)
		return ret;
	if (!dev_is_sata(dev))
		sas_change_queue_depth(sdev, 64);

	return 0;
}

static void hisi_sas_scan_start(struct Scsi_Host *shost)
{
	struct hisi_hba *hisi_hba = shost_priv(shost);

	hisi_hba->hw->phys_init(hisi_hba);
}

static int hisi_sas_scan_finished(struct Scsi_Host *shost, unsigned long time)
{
	struct hisi_hba *hisi_hba = shost_priv(shost);
	struct sas_ha_struct *sha = &hisi_hba->sha;

	/* Wait for PHY up interrupt to occur */
	if (time < HZ)
		return 0;

	sas_drain_work(sha);
	return 1;
}

static void hisi_sas_phyup_work(struct work_struct *work)
{
	struct hisi_sas_phy *phy =
		container_of(work, typeof(*phy), works[HISI_PHYE_PHY_UP]);
	struct hisi_hba *hisi_hba = phy->hisi_hba;
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	int phy_no = sas_phy->id;

	hisi_hba->hw->sl_notify(hisi_hba, phy_no); /* This requires a sleep */
	hisi_sas_bytes_dmaed(hisi_hba, phy_no);
}

static void hisi_sas_linkreset_work(struct work_struct *work)
{
	struct hisi_sas_phy *phy =
		container_of(work, typeof(*phy), works[HISI_PHYE_LINK_RESET]);
	struct asd_sas_phy *sas_phy = &phy->sas_phy;

	hisi_sas_control_phy(sas_phy, PHY_FUNC_LINK_RESET, NULL);
}

static const work_func_t hisi_sas_phye_fns[HISI_PHYES_NUM] = {
	[HISI_PHYE_PHY_UP] = hisi_sas_phyup_work,
	[HISI_PHYE_LINK_RESET] = hisi_sas_linkreset_work,
};

bool hisi_sas_notify_phy_event(struct hisi_sas_phy *phy,
				enum hisi_sas_phy_event event)
{
	struct hisi_hba *hisi_hba = phy->hisi_hba;

	if (WARN_ON(event >= HISI_PHYES_NUM))
		return false;

	return queue_work(hisi_hba->wq, &phy->works[event]);
}
EXPORT_SYMBOL_GPL(hisi_sas_notify_phy_event);

static void hisi_sas_phy_init(struct hisi_hba *hisi_hba, int phy_no)
{
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	int i;

	phy->hisi_hba = hisi_hba;
	phy->port = NULL;
	phy->minimum_linkrate = SAS_LINK_RATE_1_5_GBPS;
	phy->maximum_linkrate = hisi_hba->hw->phy_get_max_linkrate();
	sas_phy->enabled = (phy_no < hisi_hba->n_phy) ? 1 : 0;
	sas_phy->class = SAS;
	sas_phy->iproto = SAS_PROTOCOL_ALL;
	sas_phy->tproto = 0;
	sas_phy->type = PHY_TYPE_PHYSICAL;
	sas_phy->role = PHY_ROLE_INITIATOR;
	sas_phy->oob_mode = OOB_NOT_CONNECTED;
	sas_phy->linkrate = SAS_LINK_RATE_UNKNOWN;
	sas_phy->id = phy_no;
	sas_phy->sas_addr = &hisi_hba->sas_addr[0];
	sas_phy->frame_rcvd = &phy->frame_rcvd[0];
	sas_phy->ha = (struct sas_ha_struct *)hisi_hba->shost->hostdata;
	sas_phy->lldd_phy = phy;

	for (i = 0; i < HISI_PHYES_NUM; i++)
		INIT_WORK(&phy->works[i], hisi_sas_phye_fns[i]);
}

static void hisi_sas_port_notify_formed(struct asd_sas_phy *sas_phy)
{
	struct sas_ha_struct *sas_ha = sas_phy->ha;
	struct hisi_hba *hisi_hba = sas_ha->lldd_ha;
	struct hisi_sas_phy *phy = sas_phy->lldd_phy;
	struct asd_sas_port *sas_port = sas_phy->port;
	struct hisi_sas_port *port = to_hisi_sas_port(sas_port);
	unsigned long flags;

	if (!sas_port)
		return;

	spin_lock_irqsave(&hisi_hba->lock, flags);
	port->port_attached = 1;
	port->id = phy->port_id;
	phy->port = port;
	sas_port->lldd_port = port;
	spin_unlock_irqrestore(&hisi_hba->lock, flags);
}

static void hisi_sas_do_release_task(struct hisi_hba *hisi_hba, struct sas_task *task,
				     struct hisi_sas_slot *slot)
{
	if (task) {
		unsigned long flags;
		struct task_status_struct *ts;

		ts = &task->task_status;

		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_ABORTED_TASK;
		spin_lock_irqsave(&task->task_state_lock, flags);
		task->task_state_flags &=
			~(SAS_TASK_STATE_PENDING | SAS_TASK_AT_INITIATOR);
		task->task_state_flags |= SAS_TASK_STATE_DONE;
		spin_unlock_irqrestore(&task->task_state_lock, flags);
	}

	hisi_sas_slot_task_free(hisi_hba, task, slot);
}

/* hisi_hba.lock should be locked */
static void hisi_sas_release_task(struct hisi_hba *hisi_hba,
			struct domain_device *device)
{
	struct hisi_sas_slot *slot, *slot2;
	struct hisi_sas_device *sas_dev = device->lldd_dev;

	list_for_each_entry_safe(slot, slot2, &sas_dev->list, entry)
		hisi_sas_do_release_task(hisi_hba, slot->task, slot);
}

void hisi_sas_release_tasks(struct hisi_hba *hisi_hba)
{
	struct hisi_sas_device *sas_dev;
	struct domain_device *device;
	int i;

	for (i = 0; i < HISI_SAS_MAX_DEVICES; i++) {
		sas_dev = &hisi_hba->devices[i];
		device = sas_dev->sas_device;

		if ((sas_dev->dev_type == SAS_PHY_UNUSED) ||
		    !device)
			continue;

		hisi_sas_release_task(hisi_hba, device);
	}
}
EXPORT_SYMBOL_GPL(hisi_sas_release_tasks);

static void hisi_sas_dereg_device(struct hisi_hba *hisi_hba,
				struct domain_device *device)
{
	if (hisi_hba->hw->dereg_device)
		hisi_hba->hw->dereg_device(hisi_hba, device);
}

static void hisi_sas_dev_gone(struct domain_device *device)
{
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(device);
	struct device *dev = hisi_hba->dev;

	dev_info(dev, "dev[%d:%x] is gone\n",
		 sas_dev->device_id, sas_dev->dev_type);

	if (!test_bit(HISI_SAS_RESET_BIT, &hisi_hba->flags)) {
		hisi_sas_internal_task_abort(hisi_hba, device,
				     HISI_SAS_INT_ABT_DEV, 0);

		hisi_sas_dereg_device(hisi_hba, device);

		hisi_hba->hw->clear_itct(hisi_hba, sas_dev);
		device->lldd_dev = NULL;
	}

	if (hisi_hba->hw->free_device)
		hisi_hba->hw->free_device(sas_dev);
	sas_dev->dev_type = SAS_PHY_UNUSED;
}

static int hisi_sas_queue_command(struct sas_task *task, gfp_t gfp_flags)
{
	return hisi_sas_task_exec(task, gfp_flags, 0, NULL);
}

static int hisi_sas_control_phy(struct asd_sas_phy *sas_phy, enum phy_func func,
				void *funcdata)
{
	struct sas_ha_struct *sas_ha = sas_phy->ha;
	struct hisi_hba *hisi_hba = sas_ha->lldd_ha;
	int phy_no = sas_phy->id;

	switch (func) {
	case PHY_FUNC_HARD_RESET:
		hisi_hba->hw->phy_hard_reset(hisi_hba, phy_no);
		break;

	case PHY_FUNC_LINK_RESET:
		hisi_hba->hw->phy_disable(hisi_hba, phy_no);
		msleep(100);
		hisi_hba->hw->phy_start(hisi_hba, phy_no);
		break;

	case PHY_FUNC_DISABLE:
		hisi_hba->hw->phy_disable(hisi_hba, phy_no);
		break;

	case PHY_FUNC_SET_LINK_RATE:
		hisi_hba->hw->phy_set_linkrate(hisi_hba, phy_no, funcdata);
		break;
	case PHY_FUNC_GET_EVENTS:
		if (hisi_hba->hw->get_events) {
			hisi_hba->hw->get_events(hisi_hba, phy_no);
			break;
		}
		/* fallthru */
	case PHY_FUNC_RELEASE_SPINUP_HOLD:
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static void hisi_sas_task_done(struct sas_task *task)
{
	if (!del_timer(&task->slow_task->timer))
		return;
	complete(&task->slow_task->completion);
}

static void hisi_sas_tmf_timedout(struct timer_list *t)
{
	struct sas_task_slow *slow = from_timer(slow, t, timer);
	struct sas_task *task = slow->task;
	unsigned long flags;

	spin_lock_irqsave(&task->task_state_lock, flags);
	if (!(task->task_state_flags & SAS_TASK_STATE_DONE))
		task->task_state_flags |= SAS_TASK_STATE_ABORTED;
	spin_unlock_irqrestore(&task->task_state_lock, flags);

	complete(&task->slow_task->completion);
}

#define TASK_TIMEOUT 20
#define TASK_RETRY 3
#define INTERNAL_ABORT_TIMEOUT 6
static int hisi_sas_exec_internal_tmf_task(struct domain_device *device,
					   void *parameter, u32 para_len,
					   struct hisi_sas_tmf_task *tmf)
{
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_hba *hisi_hba = sas_dev->hisi_hba;
	struct device *dev = hisi_hba->dev;
	struct sas_task *task;
	int res, retry;

	for (retry = 0; retry < TASK_RETRY; retry++) {
		task = sas_alloc_slow_task(GFP_KERNEL);
		if (!task)
			return -ENOMEM;

		task->dev = device;
		task->task_proto = device->tproto;

		if (dev_is_sata(device)) {
			task->ata_task.device_control_reg_update = 1;
			memcpy(&task->ata_task.fis, parameter, para_len);
		} else {
			memcpy(&task->ssp_task, parameter, para_len);
		}
		task->task_done = hisi_sas_task_done;

		task->slow_task->timer.function = hisi_sas_tmf_timedout;
		task->slow_task->timer.expires = jiffies + TASK_TIMEOUT*HZ;
		add_timer(&task->slow_task->timer);

		res = hisi_sas_task_exec(task, GFP_KERNEL, 1, tmf);

		if (res) {
			del_timer(&task->slow_task->timer);
			dev_err(dev, "abort tmf: executing internal task failed: %d\n",
				res);
			goto ex_err;
		}

		wait_for_completion(&task->slow_task->completion);
		res = TMF_RESP_FUNC_FAILED;
		/* Even TMF timed out, return direct. */
		if ((task->task_state_flags & SAS_TASK_STATE_ABORTED)) {
			if (!(task->task_state_flags & SAS_TASK_STATE_DONE)) {
				struct hisi_sas_slot *slot = task->lldd_task;

				dev_err(dev, "abort tmf: TMF task timeout and not done\n");
				if (slot)
					slot->task = NULL;

				goto ex_err;
			} else
				dev_err(dev, "abort tmf: TMF task timeout\n");
		}

		if (task->task_status.resp == SAS_TASK_COMPLETE &&
		     task->task_status.stat == TMF_RESP_FUNC_COMPLETE) {
			res = TMF_RESP_FUNC_COMPLETE;
			break;
		}

		if (task->task_status.resp == SAS_TASK_COMPLETE &&
			task->task_status.stat == TMF_RESP_FUNC_SUCC) {
			res = TMF_RESP_FUNC_SUCC;
			break;
		}

		if (task->task_status.resp == SAS_TASK_COMPLETE &&
		      task->task_status.stat == SAS_DATA_UNDERRUN) {
			/* no error, but return the number of bytes of
			 * underrun
			 */
			dev_warn(dev, "abort tmf: task to dev %016llx "
				 "resp: 0x%x sts 0x%x underrun\n",
				 SAS_ADDR(device->sas_addr),
				 task->task_status.resp,
				 task->task_status.stat);
			res = task->task_status.residual;
			break;
		}

		if (task->task_status.resp == SAS_TASK_COMPLETE &&
			task->task_status.stat == SAS_DATA_OVERRUN) {
			dev_warn(dev, "abort tmf: blocked task error\n");
			res = -EMSGSIZE;
			break;
		}

		dev_warn(dev, "abort tmf: task to dev "
			 "%016llx resp: 0x%x status 0x%x\n",
			 SAS_ADDR(device->sas_addr), task->task_status.resp,
			 task->task_status.stat);
		sas_free_task(task);
		task = NULL;
	}
ex_err:
	if (retry == TASK_RETRY)
		dev_warn(dev, "abort tmf: executing internal task failed!\n");
	sas_free_task(task);
	return res;
}

static void hisi_sas_fill_ata_reset_cmd(struct ata_device *dev,
		bool reset, int pmp, u8 *fis)
{
	struct ata_taskfile tf;

	ata_tf_init(dev, &tf);
	if (reset)
		tf.ctl |= ATA_SRST;
	else
		tf.ctl &= ~ATA_SRST;
	tf.command = ATA_CMD_DEV_RESET;
	ata_tf_to_fis(&tf, pmp, 0, fis);
}

static int hisi_sas_softreset_ata_disk(struct domain_device *device)
{
	u8 fis[20] = {0};
	struct ata_port *ap = device->sata_dev.ap;
	struct ata_link *link;
	int rc = TMF_RESP_FUNC_FAILED;
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(device);
	struct device *dev = hisi_hba->dev;
	int s = sizeof(struct host_to_dev_fis);
	unsigned long flags;

	ata_for_each_link(link, ap, EDGE) {
		int pmp = sata_srst_pmp(link);

		hisi_sas_fill_ata_reset_cmd(link->device, 1, pmp, fis);
		rc = hisi_sas_exec_internal_tmf_task(device, fis, s, NULL);
		if (rc != TMF_RESP_FUNC_COMPLETE)
			break;
	}

	if (rc == TMF_RESP_FUNC_COMPLETE) {
		ata_for_each_link(link, ap, EDGE) {
			int pmp = sata_srst_pmp(link);

			hisi_sas_fill_ata_reset_cmd(link->device, 0, pmp, fis);
			rc = hisi_sas_exec_internal_tmf_task(device, fis,
							     s, NULL);
			if (rc != TMF_RESP_FUNC_COMPLETE)
				dev_err(dev, "ata disk de-reset failed\n");
		}
	} else {
		dev_err(dev, "ata disk reset failed\n");
	}

	if (rc == TMF_RESP_FUNC_COMPLETE) {
		spin_lock_irqsave(&hisi_hba->lock, flags);
		hisi_sas_release_task(hisi_hba, device);
		spin_unlock_irqrestore(&hisi_hba->lock, flags);
	}

	return rc;
}

static int hisi_sas_debug_issue_ssp_tmf(struct domain_device *device,
				u8 *lun, struct hisi_sas_tmf_task *tmf)
{
	struct sas_ssp_task ssp_task;

	if (!(device->tproto & SAS_PROTOCOL_SSP))
		return TMF_RESP_FUNC_ESUPP;

	memcpy(ssp_task.LUN, lun, 8);

	return hisi_sas_exec_internal_tmf_task(device, &ssp_task,
				sizeof(ssp_task), tmf);
}

static void hisi_sas_refresh_port_id(struct hisi_hba *hisi_hba)
{
	u32 state = hisi_hba->hw->get_phys_state(hisi_hba);
	int i;

	for (i = 0; i < HISI_SAS_MAX_DEVICES; i++) {
		struct hisi_sas_device *sas_dev = &hisi_hba->devices[i];
		struct domain_device *device = sas_dev->sas_device;
		struct asd_sas_port *sas_port;
		struct hisi_sas_port *port;
		struct hisi_sas_phy *phy = NULL;
		struct asd_sas_phy *sas_phy;

		if ((sas_dev->dev_type == SAS_PHY_UNUSED)
				|| !device || !device->port)
			continue;

		sas_port = device->port;
		port = to_hisi_sas_port(sas_port);

		list_for_each_entry(sas_phy, &sas_port->phy_list, port_phy_el)
			if (state & BIT(sas_phy->id)) {
				phy = sas_phy->lldd_phy;
				break;
			}

		if (phy) {
			port->id = phy->port_id;

			/* Update linkrate of directly attached device. */
			if (!device->parent)
				device->linkrate = phy->sas_phy.linkrate;

			hisi_hba->hw->setup_itct(hisi_hba, sas_dev);
		} else
			port->id = 0xff;
	}
}

static void hisi_sas_rescan_topology(struct hisi_hba *hisi_hba, u32 old_state,
			      u32 state)
{
	struct sas_ha_struct *sas_ha = &hisi_hba->sha;
	struct asd_sas_port *_sas_port = NULL;
	int phy_no;

	for (phy_no = 0; phy_no < hisi_hba->n_phy; phy_no++) {
		struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
		struct asd_sas_phy *sas_phy = &phy->sas_phy;
		struct asd_sas_port *sas_port = sas_phy->port;
		bool do_port_check = !!(_sas_port != sas_port);

		if (!sas_phy->phy->enabled)
			continue;

		/* Report PHY state change to libsas */
		if (state & BIT(phy_no)) {
			if (do_port_check && sas_port && sas_port->port_dev) {
				struct domain_device *dev = sas_port->port_dev;

				_sas_port = sas_port;

				if (DEV_IS_EXPANDER(dev->dev_type))
					sas_ha->notify_port_event(sas_phy,
							PORTE_BROADCAST_RCVD);
			}
		} else if (old_state & (1 << phy_no))
			/* PHY down but was up before */
			hisi_sas_phy_down(hisi_hba, phy_no, 0);

	}
}

static int hisi_sas_controller_reset(struct hisi_hba *hisi_hba)
{
	struct device *dev = hisi_hba->dev;
	struct Scsi_Host *shost = hisi_hba->shost;
	u32 old_state, state;
	unsigned long flags;
	int rc;

	if (!hisi_hba->hw->soft_reset)
		return -1;

	if (test_and_set_bit(HISI_SAS_RESET_BIT, &hisi_hba->flags))
		return -1;

	dev_info(dev, "controller resetting...\n");
	old_state = hisi_hba->hw->get_phys_state(hisi_hba);

	scsi_block_requests(shost);
	set_bit(HISI_SAS_REJECT_CMD_BIT, &hisi_hba->flags);
	rc = hisi_hba->hw->soft_reset(hisi_hba);
	if (rc) {
		dev_warn(dev, "controller reset failed (%d)\n", rc);
		clear_bit(HISI_SAS_REJECT_CMD_BIT, &hisi_hba->flags);
		scsi_unblock_requests(shost);
		goto out;
	}
	spin_lock_irqsave(&hisi_hba->lock, flags);
	hisi_sas_release_tasks(hisi_hba);
	spin_unlock_irqrestore(&hisi_hba->lock, flags);

	clear_bit(HISI_SAS_REJECT_CMD_BIT, &hisi_hba->flags);

	/* Init and wait for PHYs to come up and all libsas event finished. */
	hisi_hba->hw->phys_init(hisi_hba);
	msleep(1000);
	hisi_sas_refresh_port_id(hisi_hba);
	scsi_unblock_requests(shost);

	state = hisi_hba->hw->get_phys_state(hisi_hba);
	hisi_sas_rescan_topology(hisi_hba, old_state, state);
	dev_info(dev, "controller reset complete\n");

out:
	clear_bit(HISI_SAS_RESET_BIT, &hisi_hba->flags);

	return rc;
}

static int hisi_sas_abort_task(struct sas_task *task)
{
	struct scsi_lun lun;
	struct hisi_sas_tmf_task tmf_task;
	struct domain_device *device = task->dev;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_hba *hisi_hba;
	struct device *dev;
	int rc = TMF_RESP_FUNC_FAILED;
	unsigned long flags;

	if (!sas_dev)
		return TMF_RESP_FUNC_FAILED;

	hisi_hba = dev_to_hisi_hba(task->dev);
	dev = hisi_hba->dev;

	spin_lock_irqsave(&task->task_state_lock, flags);
	if (task->task_state_flags & SAS_TASK_STATE_DONE) {
		spin_unlock_irqrestore(&task->task_state_lock, flags);
		rc = TMF_RESP_FUNC_COMPLETE;
		goto out;
	}
	task->task_state_flags |= SAS_TASK_STATE_ABORTED;
	spin_unlock_irqrestore(&task->task_state_lock, flags);

	sas_dev->dev_status = HISI_SAS_DEV_EH;
	if (task->lldd_task && task->task_proto & SAS_PROTOCOL_SSP) {
		struct scsi_cmnd *cmnd = task->uldd_task;
		struct hisi_sas_slot *slot = task->lldd_task;
		u32 tag = slot->idx;
		int rc2;

		int_to_scsilun(cmnd->device->lun, &lun);
		tmf_task.tmf = TMF_ABORT_TASK;
		tmf_task.tag_of_task_to_be_managed = cpu_to_le16(tag);

		rc = hisi_sas_debug_issue_ssp_tmf(task->dev, lun.scsi_lun,
						  &tmf_task);

		rc2 = hisi_sas_internal_task_abort(hisi_hba, device,
						   HISI_SAS_INT_ABT_CMD, tag);
		if (rc2 < 0) {
			dev_err(dev, "abort task: internal abort (%d)\n", rc2);
			return TMF_RESP_FUNC_FAILED;
		}

		/*
		 * If the TMF finds that the IO is not in the device and also
		 * the internal abort does not succeed, then it is safe to
		 * free the slot.
		 * Note: if the internal abort succeeds then the slot
		 * will have already been completed
		 */
		if (rc == TMF_RESP_FUNC_COMPLETE && rc2 != TMF_RESP_FUNC_SUCC) {
			if (task->lldd_task) {
				spin_lock_irqsave(&hisi_hba->lock, flags);
				hisi_sas_do_release_task(hisi_hba, task, slot);
				spin_unlock_irqrestore(&hisi_hba->lock, flags);
			}
		}
	} else if (task->task_proto & SAS_PROTOCOL_SATA ||
		task->task_proto & SAS_PROTOCOL_STP) {
		if (task->dev->dev_type == SAS_SATA_DEV) {
			rc = hisi_sas_internal_task_abort(hisi_hba, device,
						HISI_SAS_INT_ABT_DEV, 0);
			if (rc < 0) {
				dev_err(dev, "abort task: internal abort failed\n");
				goto out;
			}
			hisi_sas_dereg_device(hisi_hba, device);
			rc = hisi_sas_softreset_ata_disk(device);
		}
	} else if (task->lldd_task && task->task_proto & SAS_PROTOCOL_SMP) {
		/* SMP */
		struct hisi_sas_slot *slot = task->lldd_task;
		u32 tag = slot->idx;

		rc = hisi_sas_internal_task_abort(hisi_hba, device,
			     HISI_SAS_INT_ABT_CMD, tag);
		if (((rc < 0) || (rc == TMF_RESP_FUNC_FAILED)) &&
					task->lldd_task) {
			spin_lock_irqsave(&hisi_hba->lock, flags);
			hisi_sas_do_release_task(hisi_hba, task, slot);
			spin_unlock_irqrestore(&hisi_hba->lock, flags);
		}
	}

out:
	if (rc != TMF_RESP_FUNC_COMPLETE)
		dev_notice(dev, "abort task: rc=%d\n", rc);
	return rc;
}

static int hisi_sas_abort_task_set(struct domain_device *device, u8 *lun)
{
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(device);
	struct device *dev = hisi_hba->dev;
	struct hisi_sas_tmf_task tmf_task;
	int rc = TMF_RESP_FUNC_FAILED;
	unsigned long flags;

	rc = hisi_sas_internal_task_abort(hisi_hba, device,
					HISI_SAS_INT_ABT_DEV, 0);
	if (rc < 0) {
		dev_err(dev, "abort task set: internal abort rc=%d\n", rc);
		return TMF_RESP_FUNC_FAILED;
	}
	hisi_sas_dereg_device(hisi_hba, device);

	tmf_task.tmf = TMF_ABORT_TASK_SET;
	rc = hisi_sas_debug_issue_ssp_tmf(device, lun, &tmf_task);

	if (rc == TMF_RESP_FUNC_COMPLETE) {
		spin_lock_irqsave(&hisi_hba->lock, flags);
		hisi_sas_release_task(hisi_hba, device);
		spin_unlock_irqrestore(&hisi_hba->lock, flags);
	}

	return rc;
}

static int hisi_sas_clear_aca(struct domain_device *device, u8 *lun)
{
	int rc = TMF_RESP_FUNC_FAILED;
	struct hisi_sas_tmf_task tmf_task;

	tmf_task.tmf = TMF_CLEAR_ACA;
	rc = hisi_sas_debug_issue_ssp_tmf(device, lun, &tmf_task);

	return rc;
}

static int hisi_sas_debug_I_T_nexus_reset(struct domain_device *device)
{
	struct sas_phy *phy = sas_get_local_phy(device);
	int rc, reset_type = (device->dev_type == SAS_SATA_DEV ||
			(device->tproto & SAS_PROTOCOL_STP)) ? 0 : 1;
	rc = sas_phy_reset(phy, reset_type);
	sas_put_local_phy(phy);
	msleep(2000);
	return rc;
}

static int hisi_sas_I_T_nexus_reset(struct domain_device *device)
{
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(device);
	struct device *dev = hisi_hba->dev;
	int rc = TMF_RESP_FUNC_FAILED;
	unsigned long flags;

	if (sas_dev->dev_status != HISI_SAS_DEV_EH)
		return TMF_RESP_FUNC_FAILED;
	sas_dev->dev_status = HISI_SAS_DEV_NORMAL;

	rc = hisi_sas_internal_task_abort(hisi_hba, device,
					HISI_SAS_INT_ABT_DEV, 0);
	if (rc < 0) {
		dev_err(dev, "I_T nexus reset: internal abort (%d)\n", rc);
		return TMF_RESP_FUNC_FAILED;
	}
	hisi_sas_dereg_device(hisi_hba, device);

	rc = hisi_sas_debug_I_T_nexus_reset(device);

	if ((rc == TMF_RESP_FUNC_COMPLETE) || (rc == -ENODEV)) {
		spin_lock_irqsave(&hisi_hba->lock, flags);
		hisi_sas_release_task(hisi_hba, device);
		spin_unlock_irqrestore(&hisi_hba->lock, flags);
	}
	return rc;
}

static int hisi_sas_lu_reset(struct domain_device *device, u8 *lun)
{
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(device);
	struct device *dev = hisi_hba->dev;
	unsigned long flags;
	int rc = TMF_RESP_FUNC_FAILED;

	sas_dev->dev_status = HISI_SAS_DEV_EH;
	if (dev_is_sata(device)) {
		struct sas_phy *phy;

		/* Clear internal IO and then hardreset */
		rc = hisi_sas_internal_task_abort(hisi_hba, device,
						  HISI_SAS_INT_ABT_DEV, 0);
		if (rc < 0) {
			dev_err(dev, "lu_reset: internal abort failed\n");
			goto out;
		}
		hisi_sas_dereg_device(hisi_hba, device);

		phy = sas_get_local_phy(device);

		rc = sas_phy_reset(phy, 1);

		if (rc == 0) {
			spin_lock_irqsave(&hisi_hba->lock, flags);
			hisi_sas_release_task(hisi_hba, device);
			spin_unlock_irqrestore(&hisi_hba->lock, flags);
		}
		sas_put_local_phy(phy);
	} else {
		struct hisi_sas_tmf_task tmf_task = { .tmf =  TMF_LU_RESET };

		rc = hisi_sas_internal_task_abort(hisi_hba, device,
						HISI_SAS_INT_ABT_DEV, 0);
		if (rc < 0) {
			dev_err(dev, "lu_reset: internal abort failed\n");
			goto out;
		}
		hisi_sas_dereg_device(hisi_hba, device);

		rc = hisi_sas_debug_issue_ssp_tmf(device, lun, &tmf_task);
		if (rc == TMF_RESP_FUNC_COMPLETE) {
			spin_lock_irqsave(&hisi_hba->lock, flags);
			hisi_sas_release_task(hisi_hba, device);
			spin_unlock_irqrestore(&hisi_hba->lock, flags);
		}
	}
out:
	if (rc != TMF_RESP_FUNC_COMPLETE)
		dev_err(dev, "lu_reset: for device[%d]:rc= %d\n",
			     sas_dev->device_id, rc);
	return rc;
}

static int hisi_sas_clear_nexus_ha(struct sas_ha_struct *sas_ha)
{
	struct hisi_hba *hisi_hba = sas_ha->lldd_ha;
	HISI_SAS_DECLARE_RST_WORK_ON_STACK(r);

	queue_work(hisi_hba->wq, &r.work);
	wait_for_completion(r.completion);
	if (r.done)
		return TMF_RESP_FUNC_COMPLETE;

	return TMF_RESP_FUNC_FAILED;
}

static int hisi_sas_query_task(struct sas_task *task)
{
	struct scsi_lun lun;
	struct hisi_sas_tmf_task tmf_task;
	int rc = TMF_RESP_FUNC_FAILED;

	if (task->lldd_task && task->task_proto & SAS_PROTOCOL_SSP) {
		struct scsi_cmnd *cmnd = task->uldd_task;
		struct domain_device *device = task->dev;
		struct hisi_sas_slot *slot = task->lldd_task;
		u32 tag = slot->idx;

		int_to_scsilun(cmnd->device->lun, &lun);
		tmf_task.tmf = TMF_QUERY_TASK;
		tmf_task.tag_of_task_to_be_managed = cpu_to_le16(tag);

		rc = hisi_sas_debug_issue_ssp_tmf(device,
						  lun.scsi_lun,
						  &tmf_task);
		switch (rc) {
		/* The task is still in Lun, release it then */
		case TMF_RESP_FUNC_SUCC:
		/* The task is not in Lun or failed, reset the phy */
		case TMF_RESP_FUNC_FAILED:
		case TMF_RESP_FUNC_COMPLETE:
			break;
		default:
			rc = TMF_RESP_FUNC_FAILED;
			break;
		}
	}
	return rc;
}

static int
hisi_sas_internal_abort_task_exec(struct hisi_hba *hisi_hba, int device_id,
				  struct sas_task *task, int abort_flag,
				  int task_tag)
{
	struct domain_device *device = task->dev;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct device *dev = hisi_hba->dev;
	struct hisi_sas_port *port;
	struct hisi_sas_slot *slot;
	struct asd_sas_port *sas_port = device->port;
	struct hisi_sas_cmd_hdr *cmd_hdr_base;
	struct hisi_sas_dq *dq = sas_dev->dq;
	int dlvry_queue_slot, dlvry_queue, n_elem = 0, rc, slot_idx;
	unsigned long flags, flags_dq;

	if (unlikely(test_bit(HISI_SAS_REJECT_CMD_BIT, &hisi_hba->flags)))
		return -EINVAL;

	if (!device->port)
		return -1;

	port = to_hisi_sas_port(sas_port);

	/* simply get a slot and send abort command */
	spin_lock_irqsave(&hisi_hba->lock, flags);
	rc = hisi_sas_slot_index_alloc(hisi_hba, &slot_idx);
	if (rc) {
		spin_unlock_irqrestore(&hisi_hba->lock, flags);
		goto err_out;
	}
	spin_unlock_irqrestore(&hisi_hba->lock, flags);

	spin_lock_irqsave(&dq->lock, flags_dq);
	rc = hisi_hba->hw->get_free_slot(hisi_hba, dq);
	if (rc)
		goto err_out_tag;

	dlvry_queue = dq->id;
	dlvry_queue_slot = dq->wr_point;

	slot = &hisi_hba->slot_info[slot_idx];
	memset(slot, 0, sizeof(struct hisi_sas_slot));

	slot->idx = slot_idx;
	slot->n_elem = n_elem;
	slot->dlvry_queue = dlvry_queue;
	slot->dlvry_queue_slot = dlvry_queue_slot;
	cmd_hdr_base = hisi_hba->cmd_hdr[dlvry_queue];
	slot->cmd_hdr = &cmd_hdr_base[dlvry_queue_slot];
	slot->task = task;
	slot->port = port;
	slot->is_internal = true;
	task->lldd_task = slot;

	slot->buf = dma_pool_alloc(hisi_hba->buffer_pool,
			GFP_ATOMIC, &slot->buf_dma);
	if (!slot->buf) {
		rc = -ENOMEM;
		goto err_out_tag;
	}

	memset(slot->cmd_hdr, 0, sizeof(struct hisi_sas_cmd_hdr));
	memset(hisi_sas_cmd_hdr_addr_mem(slot), 0, HISI_SAS_COMMAND_TABLE_SZ);
	memset(hisi_sas_status_buf_addr_mem(slot), 0, HISI_SAS_STATUS_BUF_SZ);

	rc = hisi_sas_task_prep_abort(hisi_hba, slot, device_id,
				      abort_flag, task_tag);
	if (rc)
		goto err_out_buf;

	spin_lock_irqsave(&hisi_hba->lock, flags);
	list_add_tail(&slot->entry, &sas_dev->list);
	spin_unlock_irqrestore(&hisi_hba->lock, flags);
	spin_lock_irqsave(&task->task_state_lock, flags);
	task->task_state_flags |= SAS_TASK_AT_INITIATOR;
	spin_unlock_irqrestore(&task->task_state_lock, flags);

	dq->slot_prep = slot;

	/* send abort command to the chip */
	hisi_hba->hw->start_delivery(dq);
	spin_unlock_irqrestore(&dq->lock, flags_dq);

	return 0;

err_out_buf:
	dma_pool_free(hisi_hba->buffer_pool, slot->buf,
		slot->buf_dma);
err_out_tag:
	spin_lock_irqsave(&hisi_hba->lock, flags);
	hisi_sas_slot_index_free(hisi_hba, slot_idx);
	spin_unlock_irqrestore(&hisi_hba->lock, flags);
	spin_unlock_irqrestore(&dq->lock, flags_dq);
err_out:
	dev_err(dev, "internal abort task prep: failed[%d]!\n", rc);

	return rc;
}

/**
 * hisi_sas_internal_task_abort -- execute an internal
 * abort command for single IO command or a device
 * @hisi_hba: host controller struct
 * @device: domain device
 * @abort_flag: mode of operation, device or single IO
 * @tag: tag of IO to be aborted (only relevant to single
 *       IO mode)
 */
static int
hisi_sas_internal_task_abort(struct hisi_hba *hisi_hba,
			     struct domain_device *device,
			     int abort_flag, int tag)
{
	struct sas_task *task;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct device *dev = hisi_hba->dev;
	int res;

	/*
	 * The interface is not realized means this HW don't support internal
	 * abort, or don't need to do internal abort. Then here, we return
	 * TMF_RESP_FUNC_FAILED and let other steps go on, which depends that
	 * the internal abort has been executed and returned CQ.
	 */
	if (!hisi_hba->hw->prep_abort)
		return TMF_RESP_FUNC_FAILED;

	task = sas_alloc_slow_task(GFP_KERNEL);
	if (!task)
		return -ENOMEM;

	task->dev = device;
	task->task_proto = device->tproto;
	task->task_done = hisi_sas_task_done;
	task->slow_task->timer.function = hisi_sas_tmf_timedout;
	task->slow_task->timer.expires = jiffies + INTERNAL_ABORT_TIMEOUT*HZ;
	add_timer(&task->slow_task->timer);

	res = hisi_sas_internal_abort_task_exec(hisi_hba, sas_dev->device_id,
						task, abort_flag, tag);
	if (res) {
		del_timer(&task->slow_task->timer);
		dev_err(dev, "internal task abort: executing internal task failed: %d\n",
			res);
		goto exit;
	}
	wait_for_completion(&task->slow_task->completion);
	res = TMF_RESP_FUNC_FAILED;

	/* Internal abort timed out */
	if ((task->task_state_flags & SAS_TASK_STATE_ABORTED)) {
		if (!(task->task_state_flags & SAS_TASK_STATE_DONE)) {
			struct hisi_sas_slot *slot = task->lldd_task;

			if (slot)
				slot->task = NULL;
			dev_err(dev, "internal task abort: timeout and not done.\n");
			res = -EIO;
			goto exit;
		} else
			dev_err(dev, "internal task abort: timeout.\n");
	}

	if (task->task_status.resp == SAS_TASK_COMPLETE &&
		task->task_status.stat == TMF_RESP_FUNC_COMPLETE) {
		res = TMF_RESP_FUNC_COMPLETE;
		goto exit;
	}

	if (task->task_status.resp == SAS_TASK_COMPLETE &&
		task->task_status.stat == TMF_RESP_FUNC_SUCC) {
		res = TMF_RESP_FUNC_SUCC;
		goto exit;
	}

exit:
	dev_dbg(dev, "internal task abort: task to dev %016llx task=%p "
		"resp: 0x%x sts 0x%x\n",
		SAS_ADDR(device->sas_addr),
		task,
		task->task_status.resp, /* 0 is complete, -1 is undelivered */
		task->task_status.stat);
	sas_free_task(task);

	return res;
}

static void hisi_sas_port_formed(struct asd_sas_phy *sas_phy)
{
	hisi_sas_port_notify_formed(sas_phy);
}

static void hisi_sas_port_deformed(struct asd_sas_phy *sas_phy)
{
}

static int hisi_sas_write_gpio(struct sas_ha_struct *sha, u8 reg_type,
			u8 reg_index, u8 reg_count, u8 *write_data)
{
	struct hisi_hba *hisi_hba = sha->lldd_ha;

	if (!hisi_hba->hw->write_gpio)
		return -EOPNOTSUPP;

	return hisi_hba->hw->write_gpio(hisi_hba, reg_type,
				reg_index, reg_count, write_data);
}

static void hisi_sas_phy_disconnected(struct hisi_sas_phy *phy)
{
	phy->phy_attached = 0;
	phy->phy_type = 0;
	phy->port = NULL;
}

void hisi_sas_phy_down(struct hisi_hba *hisi_hba, int phy_no, int rdy)
{
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	struct sas_ha_struct *sas_ha = &hisi_hba->sha;

	if (rdy) {
		/* Phy down but ready */
		hisi_sas_bytes_dmaed(hisi_hba, phy_no);
		hisi_sas_port_notify_formed(sas_phy);
	} else {
		struct hisi_sas_port *port  = phy->port;

		/* Phy down and not ready */
		sas_ha->notify_phy_event(sas_phy, PHYE_LOSS_OF_SIGNAL);
		sas_phy_disconnected(sas_phy);

		if (port) {
			if (phy->phy_type & PORT_TYPE_SAS) {
				int port_id = port->id;

				if (!hisi_hba->hw->get_wideport_bitmap(hisi_hba,
								       port_id))
					port->port_attached = 0;
			} else if (phy->phy_type & PORT_TYPE_SATA)
				port->port_attached = 0;
		}
		hisi_sas_phy_disconnected(phy);
	}
}
EXPORT_SYMBOL_GPL(hisi_sas_phy_down);

void hisi_sas_kill_tasklets(struct hisi_hba *hisi_hba)
{
	int i;

	for (i = 0; i < hisi_hba->queue_count; i++) {
		struct hisi_sas_cq *cq = &hisi_hba->cq[i];

		tasklet_kill(&cq->tasklet);
	}
}
EXPORT_SYMBOL_GPL(hisi_sas_kill_tasklets);

struct scsi_transport_template *hisi_sas_stt;
EXPORT_SYMBOL_GPL(hisi_sas_stt);

static struct device_attribute *host_attrs[] = {
	&dev_attr_phy_event_threshold,
	NULL,
};

static struct scsi_host_template _hisi_sas_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.queuecommand		= sas_queuecommand,
	.target_alloc		= sas_target_alloc,
	.slave_configure	= hisi_sas_slave_configure,
	.scan_finished		= hisi_sas_scan_finished,
	.scan_start		= hisi_sas_scan_start,
	.change_queue_depth	= sas_change_queue_depth,
	.bios_param		= sas_bios_param,
	.can_queue		= 1,
	.this_id		= -1,
	.sg_tablesize		= SG_ALL,
	.max_sectors		= SCSI_DEFAULT_MAX_SECTORS,
	.use_clustering		= ENABLE_CLUSTERING,
	.eh_device_reset_handler = sas_eh_device_reset_handler,
	.eh_target_reset_handler = sas_eh_target_reset_handler,
	.target_destroy		= sas_target_destroy,
	.ioctl			= sas_ioctl,
	.shost_attrs		= host_attrs,
};
struct scsi_host_template *hisi_sas_sht = &_hisi_sas_sht;
EXPORT_SYMBOL_GPL(hisi_sas_sht);

static struct sas_domain_function_template hisi_sas_transport_ops = {
	.lldd_dev_found		= hisi_sas_dev_found,
	.lldd_dev_gone		= hisi_sas_dev_gone,
	.lldd_execute_task	= hisi_sas_queue_command,
	.lldd_control_phy	= hisi_sas_control_phy,
	.lldd_abort_task	= hisi_sas_abort_task,
	.lldd_abort_task_set	= hisi_sas_abort_task_set,
	.lldd_clear_aca		= hisi_sas_clear_aca,
	.lldd_I_T_nexus_reset	= hisi_sas_I_T_nexus_reset,
	.lldd_lu_reset		= hisi_sas_lu_reset,
	.lldd_query_task	= hisi_sas_query_task,
	.lldd_clear_nexus_ha = hisi_sas_clear_nexus_ha,
	.lldd_port_formed	= hisi_sas_port_formed,
	.lldd_port_deformed = hisi_sas_port_deformed,
	.lldd_write_gpio = hisi_sas_write_gpio,
};

void hisi_sas_init_mem(struct hisi_hba *hisi_hba)
{
	int i, s, max_command_entries = hisi_hba->hw->max_command_entries;

	for (i = 0; i < hisi_hba->queue_count; i++) {
		struct hisi_sas_cq *cq = &hisi_hba->cq[i];
		struct hisi_sas_dq *dq = &hisi_hba->dq[i];

		s = sizeof(struct hisi_sas_cmd_hdr) * HISI_SAS_QUEUE_SLOTS;
		memset(hisi_hba->cmd_hdr[i], 0, s);
		dq->wr_point = 0;

		s = hisi_hba->hw->complete_hdr_size * HISI_SAS_QUEUE_SLOTS;
		memset(hisi_hba->complete_hdr[i], 0, s);
		cq->rd_point = 0;
	}

	s = sizeof(struct hisi_sas_initial_fis) * hisi_hba->n_phy;
	memset(hisi_hba->initial_fis, 0, s);

	s = max_command_entries * sizeof(struct hisi_sas_iost);
	memset(hisi_hba->iost, 0, s);

	s = max_command_entries * sizeof(struct hisi_sas_breakpoint);
	memset(hisi_hba->breakpoint, 0, s);

	s = HISI_SAS_MAX_ITCT_ENTRIES * sizeof(struct hisi_sas_sata_breakpoint);
	memset(hisi_hba->sata_breakpoint, 0, s);
}
EXPORT_SYMBOL_GPL(hisi_sas_init_mem);

int hisi_sas_alloc(struct hisi_hba *hisi_hba, struct Scsi_Host *shost)
{
	struct device *dev = hisi_hba->dev;
	int i, s, max_command_entries = hisi_hba->hw->max_command_entries;

	spin_lock_init(&hisi_hba->lock);
	for (i = 0; i < hisi_hba->n_phy; i++) {
		hisi_sas_phy_init(hisi_hba, i);
		hisi_hba->port[i].port_attached = 0;
		hisi_hba->port[i].id = -1;
	}

	for (i = 0; i < HISI_SAS_MAX_DEVICES; i++) {
		hisi_hba->devices[i].dev_type = SAS_PHY_UNUSED;
		hisi_hba->devices[i].device_id = i;
		hisi_hba->devices[i].dev_status = HISI_SAS_DEV_NORMAL;
	}

	for (i = 0; i < hisi_hba->queue_count; i++) {
		struct hisi_sas_cq *cq = &hisi_hba->cq[i];
		struct hisi_sas_dq *dq = &hisi_hba->dq[i];

		/* Completion queue structure */
		cq->id = i;
		cq->hisi_hba = hisi_hba;

		/* Delivery queue structure */
		spin_lock_init(&dq->lock);
		dq->id = i;
		dq->hisi_hba = hisi_hba;

		/* Delivery queue */
		s = sizeof(struct hisi_sas_cmd_hdr) * HISI_SAS_QUEUE_SLOTS;
		hisi_hba->cmd_hdr[i] = dma_alloc_coherent(dev, s,
					&hisi_hba->cmd_hdr_dma[i], GFP_KERNEL);
		if (!hisi_hba->cmd_hdr[i])
			goto err_out;

		/* Completion queue */
		s = hisi_hba->hw->complete_hdr_size * HISI_SAS_QUEUE_SLOTS;
		hisi_hba->complete_hdr[i] = dma_alloc_coherent(dev, s,
				&hisi_hba->complete_hdr_dma[i], GFP_KERNEL);
		if (!hisi_hba->complete_hdr[i])
			goto err_out;
	}

	s = sizeof(struct hisi_sas_slot_buf_table);
	hisi_hba->buffer_pool = dma_pool_create("dma_buffer", dev, s, 16, 0);
	if (!hisi_hba->buffer_pool)
		goto err_out;

	s = HISI_SAS_MAX_ITCT_ENTRIES * sizeof(struct hisi_sas_itct);
	hisi_hba->itct = dma_zalloc_coherent(dev, s, &hisi_hba->itct_dma,
					    GFP_KERNEL);
	if (!hisi_hba->itct)
		goto err_out;

	hisi_hba->slot_info = devm_kcalloc(dev, max_command_entries,
					   sizeof(struct hisi_sas_slot),
					   GFP_KERNEL);
	if (!hisi_hba->slot_info)
		goto err_out;

	s = max_command_entries * sizeof(struct hisi_sas_iost);
	hisi_hba->iost = dma_alloc_coherent(dev, s, &hisi_hba->iost_dma,
					    GFP_KERNEL);
	if (!hisi_hba->iost)
		goto err_out;

	s = max_command_entries * sizeof(struct hisi_sas_breakpoint);
	hisi_hba->breakpoint = dma_alloc_coherent(dev, s,
				&hisi_hba->breakpoint_dma, GFP_KERNEL);
	if (!hisi_hba->breakpoint)
		goto err_out;

	hisi_hba->slot_index_count = max_command_entries;
	s = hisi_hba->slot_index_count / BITS_PER_BYTE;
	hisi_hba->slot_index_tags = devm_kzalloc(dev, s, GFP_KERNEL);
	if (!hisi_hba->slot_index_tags)
		goto err_out;

	s = sizeof(struct hisi_sas_initial_fis) * HISI_SAS_MAX_PHYS;
	hisi_hba->initial_fis = dma_alloc_coherent(dev, s,
				&hisi_hba->initial_fis_dma, GFP_KERNEL);
	if (!hisi_hba->initial_fis)
		goto err_out;

	s = HISI_SAS_MAX_ITCT_ENTRIES * sizeof(struct hisi_sas_sata_breakpoint);
	hisi_hba->sata_breakpoint = dma_alloc_coherent(dev, s,
				&hisi_hba->sata_breakpoint_dma, GFP_KERNEL);
	if (!hisi_hba->sata_breakpoint)
		goto err_out;
	hisi_sas_init_mem(hisi_hba);

	hisi_sas_slot_index_init(hisi_hba);

	hisi_hba->wq = create_singlethread_workqueue(dev_name(dev));
	if (!hisi_hba->wq) {
		dev_err(dev, "sas_alloc: failed to create workqueue\n");
		goto err_out;
	}

	return 0;
err_out:
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(hisi_sas_alloc);

void hisi_sas_free(struct hisi_hba *hisi_hba)
{
	struct device *dev = hisi_hba->dev;
	int i, s, max_command_entries = hisi_hba->hw->max_command_entries;

	for (i = 0; i < hisi_hba->queue_count; i++) {
		s = sizeof(struct hisi_sas_cmd_hdr) * HISI_SAS_QUEUE_SLOTS;
		if (hisi_hba->cmd_hdr[i])
			dma_free_coherent(dev, s,
					  hisi_hba->cmd_hdr[i],
					  hisi_hba->cmd_hdr_dma[i]);

		s = hisi_hba->hw->complete_hdr_size * HISI_SAS_QUEUE_SLOTS;
		if (hisi_hba->complete_hdr[i])
			dma_free_coherent(dev, s,
					  hisi_hba->complete_hdr[i],
					  hisi_hba->complete_hdr_dma[i]);
	}

	dma_pool_destroy(hisi_hba->buffer_pool);

	s = HISI_SAS_MAX_ITCT_ENTRIES * sizeof(struct hisi_sas_itct);
	if (hisi_hba->itct)
		dma_free_coherent(dev, s,
				  hisi_hba->itct, hisi_hba->itct_dma);

	s = max_command_entries * sizeof(struct hisi_sas_iost);
	if (hisi_hba->iost)
		dma_free_coherent(dev, s,
				  hisi_hba->iost, hisi_hba->iost_dma);

	s = max_command_entries * sizeof(struct hisi_sas_breakpoint);
	if (hisi_hba->breakpoint)
		dma_free_coherent(dev, s,
				  hisi_hba->breakpoint,
				  hisi_hba->breakpoint_dma);


	s = sizeof(struct hisi_sas_initial_fis) * HISI_SAS_MAX_PHYS;
	if (hisi_hba->initial_fis)
		dma_free_coherent(dev, s,
				  hisi_hba->initial_fis,
				  hisi_hba->initial_fis_dma);

	s = HISI_SAS_MAX_ITCT_ENTRIES * sizeof(struct hisi_sas_sata_breakpoint);
	if (hisi_hba->sata_breakpoint)
		dma_free_coherent(dev, s,
				  hisi_hba->sata_breakpoint,
				  hisi_hba->sata_breakpoint_dma);

	if (hisi_hba->wq)
		destroy_workqueue(hisi_hba->wq);
}
EXPORT_SYMBOL_GPL(hisi_sas_free);

void hisi_sas_rst_work_handler(struct work_struct *work)
{
	struct hisi_hba *hisi_hba =
		container_of(work, struct hisi_hba, rst_work);

	hisi_sas_controller_reset(hisi_hba);
}
EXPORT_SYMBOL_GPL(hisi_sas_rst_work_handler);

void hisi_sas_sync_rst_work_handler(struct work_struct *work)
{
	struct hisi_sas_rst *rst =
		container_of(work, struct hisi_sas_rst, work);

	if (!hisi_sas_controller_reset(rst->hisi_hba))
		rst->done = true;
	complete(rst->completion);
}
EXPORT_SYMBOL_GPL(hisi_sas_sync_rst_work_handler);

int hisi_sas_get_fw_info(struct hisi_hba *hisi_hba)
{
	struct device *dev = hisi_hba->dev;
	struct platform_device *pdev = hisi_hba->platform_dev;
	struct device_node *np = pdev ? pdev->dev.of_node : NULL;
	struct clk *refclk;

	if (device_property_read_u8_array(dev, "sas-addr", hisi_hba->sas_addr,
					  SAS_ADDR_SIZE)) {
		dev_err(dev, "could not get property sas-addr\n");
		return -ENOENT;
	}

	if (np) {
		/*
		 * These properties are only required for platform device-based
		 * controller with DT firmware.
		 */
		hisi_hba->ctrl = syscon_regmap_lookup_by_phandle(np,
					"hisilicon,sas-syscon");
		if (IS_ERR(hisi_hba->ctrl)) {
			dev_err(dev, "could not get syscon\n");
			return -ENOENT;
		}

		if (device_property_read_u32(dev, "ctrl-reset-reg",
					     &hisi_hba->ctrl_reset_reg)) {
			dev_err(dev,
				"could not get property ctrl-reset-reg\n");
			return -ENOENT;
		}

		if (device_property_read_u32(dev, "ctrl-reset-sts-reg",
					     &hisi_hba->ctrl_reset_sts_reg)) {
			dev_err(dev,
				"could not get property ctrl-reset-sts-reg\n");
			return -ENOENT;
		}

		if (device_property_read_u32(dev, "ctrl-clock-ena-reg",
					     &hisi_hba->ctrl_clock_ena_reg)) {
			dev_err(dev,
				"could not get property ctrl-clock-ena-reg\n");
			return -ENOENT;
		}
	}

	refclk = devm_clk_get(dev, NULL);
	if (IS_ERR(refclk))
		dev_dbg(dev, "no ref clk property\n");
	else
		hisi_hba->refclk_frequency_mhz = clk_get_rate(refclk) / 1000000;

	if (device_property_read_u32(dev, "phy-count", &hisi_hba->n_phy)) {
		dev_err(dev, "could not get property phy-count\n");
		return -ENOENT;
	}

	if (device_property_read_u32(dev, "queue-count",
				     &hisi_hba->queue_count)) {
		dev_err(dev, "could not get property queue-count\n");
		return -ENOENT;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(hisi_sas_get_fw_info);

static struct Scsi_Host *hisi_sas_shost_alloc(struct platform_device *pdev,
					      const struct hisi_sas_hw *hw)
{
	struct resource *res;
	struct Scsi_Host *shost;
	struct hisi_hba *hisi_hba;
	struct device *dev = &pdev->dev;

	shost = scsi_host_alloc(hisi_sas_sht, sizeof(*hisi_hba));
	if (!shost) {
		dev_err(dev, "scsi host alloc failed\n");
		return NULL;
	}
	hisi_hba = shost_priv(shost);

	INIT_WORK(&hisi_hba->rst_work, hisi_sas_rst_work_handler);
	hisi_hba->hw = hw;
	hisi_hba->dev = dev;
	hisi_hba->platform_dev = pdev;
	hisi_hba->shost = shost;
	SHOST_TO_SAS_HA(shost) = &hisi_hba->sha;

	timer_setup(&hisi_hba->timer, NULL, 0);

	if (hisi_sas_get_fw_info(hisi_hba) < 0)
		goto err_out;

	if (dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64)) &&
	    dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32))) {
		dev_err(dev, "No usable DMA addressing method\n");
		goto err_out;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hisi_hba->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(hisi_hba->regs))
		goto err_out;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res) {
		hisi_hba->sgpio_regs = devm_ioremap_resource(dev, res);
		if (IS_ERR(hisi_hba->sgpio_regs))
			goto err_out;
	}

	if (hisi_sas_alloc(hisi_hba, shost)) {
		hisi_sas_free(hisi_hba);
		goto err_out;
	}

	return shost;
err_out:
	scsi_host_put(shost);
	dev_err(dev, "shost alloc failed\n");
	return NULL;
}

int hisi_sas_probe(struct platform_device *pdev,
			 const struct hisi_sas_hw *hw)
{
	struct Scsi_Host *shost;
	struct hisi_hba *hisi_hba;
	struct device *dev = &pdev->dev;
	struct asd_sas_phy **arr_phy;
	struct asd_sas_port **arr_port;
	struct sas_ha_struct *sha;
	int rc, phy_nr, port_nr, i;

	shost = hisi_sas_shost_alloc(pdev, hw);
	if (!shost)
		return -ENOMEM;

	sha = SHOST_TO_SAS_HA(shost);
	hisi_hba = shost_priv(shost);
	platform_set_drvdata(pdev, sha);

	phy_nr = port_nr = hisi_hba->n_phy;

	arr_phy = devm_kcalloc(dev, phy_nr, sizeof(void *), GFP_KERNEL);
	arr_port = devm_kcalloc(dev, port_nr, sizeof(void *), GFP_KERNEL);
	if (!arr_phy || !arr_port) {
		rc = -ENOMEM;
		goto err_out_ha;
	}

	sha->sas_phy = arr_phy;
	sha->sas_port = arr_port;
	sha->lldd_ha = hisi_hba;

	shost->transportt = hisi_sas_stt;
	shost->max_id = HISI_SAS_MAX_DEVICES;
	shost->max_lun = ~0;
	shost->max_channel = 1;
	shost->max_cmd_len = 16;
	shost->sg_tablesize = min_t(u16, SG_ALL, HISI_SAS_SGE_PAGE_CNT);
	shost->can_queue = hisi_hba->hw->max_command_entries;
	shost->cmd_per_lun = hisi_hba->hw->max_command_entries;

	sha->sas_ha_name = DRV_NAME;
	sha->dev = hisi_hba->dev;
	sha->lldd_module = THIS_MODULE;
	sha->sas_addr = &hisi_hba->sas_addr[0];
	sha->num_phys = hisi_hba->n_phy;
	sha->core.shost = hisi_hba->shost;

	for (i = 0; i < hisi_hba->n_phy; i++) {
		sha->sas_phy[i] = &hisi_hba->phy[i].sas_phy;
		sha->sas_port[i] = &hisi_hba->port[i].sas_port;
	}

	rc = scsi_add_host(shost, &pdev->dev);
	if (rc)
		goto err_out_ha;

	rc = sas_register_ha(sha);
	if (rc)
		goto err_out_register_ha;

	rc = hisi_hba->hw->hw_init(hisi_hba);
	if (rc)
		goto err_out_register_ha;

	scsi_scan_host(shost);

	return 0;

err_out_register_ha:
	scsi_remove_host(shost);
err_out_ha:
	hisi_sas_free(hisi_hba);
	scsi_host_put(shost);
	return rc;
}
EXPORT_SYMBOL_GPL(hisi_sas_probe);

int hisi_sas_remove(struct platform_device *pdev)
{
	struct sas_ha_struct *sha = platform_get_drvdata(pdev);
	struct hisi_hba *hisi_hba = sha->lldd_ha;
	struct Scsi_Host *shost = sha->core.shost;

	if (timer_pending(&hisi_hba->timer))
		del_timer(&hisi_hba->timer);

	sas_unregister_ha(sha);
	sas_remove_host(sha->core.shost);

	hisi_sas_free(hisi_hba);
	scsi_host_put(shost);
	return 0;
}
EXPORT_SYMBOL_GPL(hisi_sas_remove);

static __init int hisi_sas_init(void)
{
	hisi_sas_stt = sas_domain_attach_transport(&hisi_sas_transport_ops);
	if (!hisi_sas_stt)
		return -ENOMEM;

	return 0;
}

static __exit void hisi_sas_exit(void)
{
	sas_release_transport(hisi_sas_stt);
}

module_init(hisi_sas_init);
module_exit(hisi_sas_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Garry <john.garry@huawei.com>");
MODULE_DESCRIPTION("HISILICON SAS controller driver");
MODULE_ALIAS("platform:" DRV_NAME);
