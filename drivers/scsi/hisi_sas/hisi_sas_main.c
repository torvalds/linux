// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2015 Linaro Ltd.
 * Copyright (c) 2015 Hisilicon Limited.
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
static void hisi_sas_release_task(struct hisi_hba *hisi_hba,
				  struct domain_device *device);
static void hisi_sas_dev_gone(struct domain_device *device);

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

/*
 * This function assumes linkrate mask fits in 8 bits, which it
 * does for all HW versions supported.
 */
u8 hisi_sas_get_prog_phy_linkrate_mask(enum sas_linkrate max)
{
	u8 rate = 0;
	int i;

	max -= SAS_LINK_RATE_1_5_GBPS;
	for (i = 0; i <= max; i++)
		rate |= 1 << (i * 2);
	return rate;
}
EXPORT_SYMBOL_GPL(hisi_sas_get_prog_phy_linkrate_mask);

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
		hisi_sas_phy_enable(hisi_hba, phy_no, 0);
}
EXPORT_SYMBOL_GPL(hisi_sas_stop_phys);

static void hisi_sas_slot_index_clear(struct hisi_hba *hisi_hba, int slot_idx)
{
	void *bitmap = hisi_hba->slot_index_tags;

	clear_bit(slot_idx, bitmap);
}

static void hisi_sas_slot_index_free(struct hisi_hba *hisi_hba, int slot_idx)
{
	if (hisi_hba->hw->slot_index_alloc ||
	    slot_idx >= HISI_SAS_UNRESERVED_IPTT) {
		spin_lock(&hisi_hba->lock);
		hisi_sas_slot_index_clear(hisi_hba, slot_idx);
		spin_unlock(&hisi_hba->lock);
	}
}

static void hisi_sas_slot_index_set(struct hisi_hba *hisi_hba, int slot_idx)
{
	void *bitmap = hisi_hba->slot_index_tags;

	set_bit(slot_idx, bitmap);
}

static int hisi_sas_slot_index_alloc(struct hisi_hba *hisi_hba,
				     struct scsi_cmnd *scsi_cmnd)
{
	int index;
	void *bitmap = hisi_hba->slot_index_tags;

	if (scsi_cmnd)
		return scsi_cmnd->request->tag;

	spin_lock(&hisi_hba->lock);
	index = find_next_zero_bit(bitmap, hisi_hba->slot_index_count,
				   hisi_hba->last_slot_index + 1);
	if (index >= hisi_hba->slot_index_count) {
		index = find_next_zero_bit(bitmap,
				hisi_hba->slot_index_count,
				HISI_SAS_UNRESERVED_IPTT);
		if (index >= hisi_hba->slot_index_count) {
			spin_unlock(&hisi_hba->lock);
			return -SAS_QUEUE_FULL;
		}
	}
	hisi_sas_slot_index_set(hisi_hba, index);
	hisi_hba->last_slot_index = index;
	spin_unlock(&hisi_hba->lock);

	return index;
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
	int device_id = slot->device_id;
	struct hisi_sas_device *sas_dev = &hisi_hba->devices[device_id];

	if (task) {
		struct device *dev = hisi_hba->dev;

		if (!task->lldd_task)
			return;

		task->lldd_task = NULL;

		if (!sas_protocol_ata(task->task_proto)) {
			struct sas_ssp_task *ssp_task = &task->ssp_task;
			struct scsi_cmnd *scsi_cmnd = ssp_task->cmd;

			if (slot->n_elem)
				dma_unmap_sg(dev, task->scatter,
					     task->num_scatter,
					     task->data_dir);
			if (slot->n_elem_dif)
				dma_unmap_sg(dev, scsi_prot_sglist(scsi_cmnd),
					     scsi_prot_sg_count(scsi_cmnd),
					     task->data_dir);
		}
	}

	spin_lock(&sas_dev->lock);
	list_del_init(&slot->entry);
	spin_unlock(&sas_dev->lock);

	memset(slot, 0, offsetof(struct hisi_sas_slot, buf));

	hisi_sas_slot_index_free(hisi_hba, slot->idx);
}
EXPORT_SYMBOL_GPL(hisi_sas_slot_task_free);

static void hisi_sas_task_prep_smp(struct hisi_hba *hisi_hba,
				  struct hisi_sas_slot *slot)
{
	hisi_hba->hw->prep_smp(hisi_hba, slot);
}

static void hisi_sas_task_prep_ssp(struct hisi_hba *hisi_hba,
				  struct hisi_sas_slot *slot)
{
	hisi_hba->hw->prep_ssp(hisi_hba, slot);
}

static void hisi_sas_task_prep_ata(struct hisi_hba *hisi_hba,
				  struct hisi_sas_slot *slot)
{
	hisi_hba->hw->prep_stp(hisi_hba, slot);
}

static void hisi_sas_task_prep_abort(struct hisi_hba *hisi_hba,
		struct hisi_sas_slot *slot,
		int device_id, int abort_flag, int tag_to_abort)
{
	hisi_hba->hw->prep_abort(hisi_hba, slot,
			device_id, abort_flag, tag_to_abort);
}

static void hisi_sas_dma_unmap(struct hisi_hba *hisi_hba,
			       struct sas_task *task, int n_elem,
			       int n_elem_req)
{
	struct device *dev = hisi_hba->dev;

	if (!sas_protocol_ata(task->task_proto)) {
		if (task->num_scatter) {
			if (n_elem)
				dma_unmap_sg(dev, task->scatter,
					     task->num_scatter,
					     task->data_dir);
		} else if (task->task_proto & SAS_PROTOCOL_SMP) {
			if (n_elem_req)
				dma_unmap_sg(dev, &task->smp_task.smp_req,
					     1, DMA_TO_DEVICE);
		}
	}
}

static int hisi_sas_dma_map(struct hisi_hba *hisi_hba,
			    struct sas_task *task, int *n_elem,
			    int *n_elem_req)
{
	struct device *dev = hisi_hba->dev;
	int rc;

	if (sas_protocol_ata(task->task_proto)) {
		*n_elem = task->num_scatter;
	} else {
		unsigned int req_len;

		if (task->num_scatter) {
			*n_elem = dma_map_sg(dev, task->scatter,
					     task->num_scatter, task->data_dir);
			if (!*n_elem) {
				rc = -ENOMEM;
				goto prep_out;
			}
		} else if (task->task_proto & SAS_PROTOCOL_SMP) {
			*n_elem_req = dma_map_sg(dev, &task->smp_task.smp_req,
						 1, DMA_TO_DEVICE);
			if (!*n_elem_req) {
				rc = -ENOMEM;
				goto prep_out;
			}
			req_len = sg_dma_len(&task->smp_task.smp_req);
			if (req_len & 0x3) {
				rc = -EINVAL;
				goto err_out_dma_unmap;
			}
		}
	}

	if (*n_elem > HISI_SAS_SGE_PAGE_CNT) {
		dev_err(dev, "task prep: n_elem(%d) > HISI_SAS_SGE_PAGE_CNT",
			*n_elem);
		rc = -EINVAL;
		goto err_out_dma_unmap;
	}
	return 0;

err_out_dma_unmap:
	/* It would be better to call dma_unmap_sg() here, but it's messy */
	hisi_sas_dma_unmap(hisi_hba, task, *n_elem,
			   *n_elem_req);
prep_out:
	return rc;
}

static void hisi_sas_dif_dma_unmap(struct hisi_hba *hisi_hba,
				   struct sas_task *task, int n_elem_dif)
{
	struct device *dev = hisi_hba->dev;

	if (n_elem_dif) {
		struct sas_ssp_task *ssp_task = &task->ssp_task;
		struct scsi_cmnd *scsi_cmnd = ssp_task->cmd;

		dma_unmap_sg(dev, scsi_prot_sglist(scsi_cmnd),
			     scsi_prot_sg_count(scsi_cmnd),
			     task->data_dir);
	}
}

static int hisi_sas_dif_dma_map(struct hisi_hba *hisi_hba,
				int *n_elem_dif, struct sas_task *task)
{
	struct device *dev = hisi_hba->dev;
	struct sas_ssp_task *ssp_task;
	struct scsi_cmnd *scsi_cmnd;
	int rc;

	if (task->num_scatter) {
		ssp_task = &task->ssp_task;
		scsi_cmnd = ssp_task->cmd;

		if (scsi_prot_sg_count(scsi_cmnd)) {
			*n_elem_dif = dma_map_sg(dev,
						 scsi_prot_sglist(scsi_cmnd),
						 scsi_prot_sg_count(scsi_cmnd),
						 task->data_dir);

			if (!*n_elem_dif)
				return -ENOMEM;

			if (*n_elem_dif > HISI_SAS_SGE_DIF_PAGE_CNT) {
				dev_err(dev, "task prep: n_elem_dif(%d) too large\n",
					*n_elem_dif);
				rc = -EINVAL;
				goto err_out_dif_dma_unmap;
			}
		}
	}

	return 0;

err_out_dif_dma_unmap:
	dma_unmap_sg(dev, scsi_prot_sglist(scsi_cmnd),
		     scsi_prot_sg_count(scsi_cmnd), task->data_dir);
	return rc;
}

static int hisi_sas_task_prep(struct sas_task *task,
			      struct hisi_sas_dq **dq_pointer,
			      bool is_tmf, struct hisi_sas_tmf_task *tmf,
			      int *pass)
{
	struct domain_device *device = task->dev;
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(device);
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_sas_port *port;
	struct hisi_sas_slot *slot;
	struct hisi_sas_cmd_hdr	*cmd_hdr_base;
	struct asd_sas_port *sas_port = device->port;
	struct device *dev = hisi_hba->dev;
	int dlvry_queue_slot, dlvry_queue, rc, slot_idx;
	int n_elem = 0, n_elem_dif = 0, n_elem_req = 0;
	struct hisi_sas_dq *dq;
	unsigned long flags;
	int wr_q_index;

	if (DEV_IS_GONE(sas_dev)) {
		if (sas_dev)
			dev_info(dev, "task prep: device %d not ready\n",
				 sas_dev->device_id);
		else
			dev_info(dev, "task prep: device %016llx not ready\n",
				 SAS_ADDR(device->sas_addr));

		return -ECOMM;
	}

	if (hisi_hba->reply_map) {
		int cpu = raw_smp_processor_id();
		unsigned int dq_index = hisi_hba->reply_map[cpu];

		*dq_pointer = dq = &hisi_hba->dq[dq_index];
	} else {
		*dq_pointer = dq = sas_dev->dq;
	}

	port = to_hisi_sas_port(sas_port);
	if (port && !port->port_attached) {
		dev_info(dev, "task prep: %s port%d not attach device\n",
			 (dev_is_sata(device)) ?
			 "SATA/STP" : "SAS",
			 device->port->id);

		return -ECOMM;
	}

	rc = hisi_sas_dma_map(hisi_hba, task, &n_elem,
			      &n_elem_req);
	if (rc < 0)
		goto prep_out;

	if (!sas_protocol_ata(task->task_proto)) {
		rc = hisi_sas_dif_dma_map(hisi_hba, &n_elem_dif, task);
		if (rc < 0)
			goto err_out_dma_unmap;
	}

	if (hisi_hba->hw->slot_index_alloc)
		rc = hisi_hba->hw->slot_index_alloc(hisi_hba, device);
	else {
		struct scsi_cmnd *scsi_cmnd = NULL;

		if (task->uldd_task) {
			struct ata_queued_cmd *qc;

			if (dev_is_sata(device)) {
				qc = task->uldd_task;
				scsi_cmnd = qc->scsicmd;
			} else {
				scsi_cmnd = task->uldd_task;
			}
		}
		rc  = hisi_sas_slot_index_alloc(hisi_hba, scsi_cmnd);
	}
	if (rc < 0)
		goto err_out_dif_dma_unmap;

	slot_idx = rc;
	slot = &hisi_hba->slot_info[slot_idx];

	spin_lock(&dq->lock);
	wr_q_index = dq->wr_point;
	dq->wr_point = (dq->wr_point + 1) % HISI_SAS_QUEUE_SLOTS;
	list_add_tail(&slot->delivery, &dq->list);
	spin_unlock(&dq->lock);
	spin_lock(&sas_dev->lock);
	list_add_tail(&slot->entry, &sas_dev->list);
	spin_unlock(&sas_dev->lock);

	dlvry_queue = dq->id;
	dlvry_queue_slot = wr_q_index;

	slot->device_id = sas_dev->device_id;
	slot->n_elem = n_elem;
	slot->n_elem_dif = n_elem_dif;
	slot->dlvry_queue = dlvry_queue;
	slot->dlvry_queue_slot = dlvry_queue_slot;
	cmd_hdr_base = hisi_hba->cmd_hdr[dlvry_queue];
	slot->cmd_hdr = &cmd_hdr_base[dlvry_queue_slot];
	slot->task = task;
	slot->port = port;
	slot->tmf = tmf;
	slot->is_internal = is_tmf;
	task->lldd_task = slot;

	memset(slot->cmd_hdr, 0, sizeof(struct hisi_sas_cmd_hdr));
	memset(hisi_sas_cmd_hdr_addr_mem(slot), 0, HISI_SAS_COMMAND_TABLE_SZ);
	memset(hisi_sas_status_buf_addr_mem(slot), 0,
	       sizeof(struct hisi_sas_err_record));

	switch (task->task_proto) {
	case SAS_PROTOCOL_SMP:
		hisi_sas_task_prep_smp(hisi_hba, slot);
		break;
	case SAS_PROTOCOL_SSP:
		hisi_sas_task_prep_ssp(hisi_hba, slot);
		break;
	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SATA | SAS_PROTOCOL_STP:
		hisi_sas_task_prep_ata(hisi_hba, slot);
		break;
	default:
		dev_err(dev, "task prep: unknown/unsupported proto (0x%x)\n",
			task->task_proto);
		break;
	}

	spin_lock_irqsave(&task->task_state_lock, flags);
	task->task_state_flags |= SAS_TASK_AT_INITIATOR;
	spin_unlock_irqrestore(&task->task_state_lock, flags);

	++(*pass);
	WRITE_ONCE(slot->ready, 1);

	return 0;

err_out_dif_dma_unmap:
	if (!sas_protocol_ata(task->task_proto))
		hisi_sas_dif_dma_unmap(hisi_hba, task, n_elem_dif);
err_out_dma_unmap:
	hisi_sas_dma_unmap(hisi_hba, task, n_elem,
			   n_elem_req);
prep_out:
	dev_err(dev, "task prep: failed[%d]!\n", rc);
	return rc;
}

static int hisi_sas_task_exec(struct sas_task *task, gfp_t gfp_flags,
			      bool is_tmf, struct hisi_sas_tmf_task *tmf)
{
	u32 rc;
	u32 pass = 0;
	struct hisi_hba *hisi_hba;
	struct device *dev;
	struct domain_device *device = task->dev;
	struct asd_sas_port *sas_port = device->port;
	struct hisi_sas_dq *dq = NULL;

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

	hisi_hba = dev_to_hisi_hba(device);
	dev = hisi_hba->dev;

	if (unlikely(test_bit(HISI_SAS_REJECT_CMD_BIT, &hisi_hba->flags))) {
		/*
		 * For IOs from upper layer, it may already disable preempt
		 * in the IO path, if disable preempt again in down(),
		 * function schedule() will report schedule_bug(), so check
		 * preemptible() before goto down().
		 */
		if (!preemptible())
			return -EINVAL;

		down(&hisi_hba->sem);
		up(&hisi_hba->sem);
	}

	/* protect task_prep and start_delivery sequence */
	rc = hisi_sas_task_prep(task, &dq, is_tmf, tmf, &pass);
	if (rc)
		dev_err(dev, "task exec: failed[%d]!\n", rc);

	if (likely(pass)) {
		spin_lock(&dq->lock);
		hisi_hba->hw->start_delivery(dq);
		spin_unlock(&dq->lock);
	}

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
		/* Nothing */
	}

	sas_phy->frame_rcvd_size = phy->frame_rcvd_size;
	sas_ha->notify_port_event(sas_phy, PORTE_BYTES_DMAED);
}

static struct hisi_sas_device *hisi_sas_alloc_dev(struct domain_device *device)
{
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(device);
	struct hisi_sas_device *sas_dev = NULL;
	int last = hisi_hba->last_dev_id;
	int first = (hisi_hba->last_dev_id + 1) % HISI_SAS_MAX_DEVICES;
	int i;

	spin_lock(&hisi_hba->lock);
	for (i = first; i != last; i %= HISI_SAS_MAX_DEVICES) {
		if (hisi_hba->devices[i].dev_type == SAS_PHY_UNUSED) {
			int queue = i % hisi_hba->queue_count;
			struct hisi_sas_dq *dq = &hisi_hba->dq[queue];

			hisi_hba->devices[i].device_id = i;
			sas_dev = &hisi_hba->devices[i];
			sas_dev->dev_status = HISI_SAS_DEV_INIT;
			sas_dev->dev_type = device->dev_type;
			sas_dev->hisi_hba = hisi_hba;
			sas_dev->sas_device = device;
			sas_dev->dq = dq;
			spin_lock_init(&sas_dev->lock);
			INIT_LIST_HEAD(&hisi_hba->devices[i].list);
			break;
		}
		i++;
	}
	hisi_hba->last_dev_id = i;
	spin_unlock(&hisi_hba->lock);

	return sas_dev;
}

#define HISI_SAS_DISK_RECOVER_CNT 3
static int hisi_sas_init_device(struct domain_device *device)
{
	int rc = TMF_RESP_FUNC_COMPLETE;
	struct scsi_lun lun;
	struct hisi_sas_tmf_task tmf_task;
	int retry = HISI_SAS_DISK_RECOVER_CNT;
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(device);
	struct device *dev = hisi_hba->dev;
	struct sas_phy *local_phy;

	switch (device->dev_type) {
	case SAS_END_DEVICE:
		int_to_scsilun(0, &lun);

		tmf_task.tmf = TMF_CLEAR_TASK_SET;
		while (retry-- > 0) {
			rc = hisi_sas_debug_issue_ssp_tmf(device, lun.scsi_lun,
							  &tmf_task);
			if (rc == TMF_RESP_FUNC_COMPLETE) {
				hisi_sas_release_task(hisi_hba, device);
				break;
			}
		}
		break;
	case SAS_SATA_DEV:
	case SAS_SATA_PM:
	case SAS_SATA_PM_PORT:
	case SAS_SATA_PENDING:
		/*
		 * send HARD RESET to clear previous affiliation of
		 * STP target port
		 */
		local_phy = sas_get_local_phy(device);
		if (!scsi_is_sas_phy_local(local_phy) &&
		    !test_bit(HISI_SAS_RESET_BIT, &hisi_hba->flags)) {
			unsigned long deadline = ata_deadline(jiffies, 20000);
			struct sata_device *sata_dev = &device->sata_dev;
			struct ata_host *ata_host = sata_dev->ata_host;
			struct ata_port_operations *ops = ata_host->ops;
			struct ata_port *ap = sata_dev->ap;
			struct ata_link *link;
			unsigned int classes;

			ata_for_each_link(link, ap, EDGE)
				rc = ops->hardreset(link, &classes,
						    deadline);
		}
		sas_put_local_phy(local_phy);
		if (rc) {
			dev_warn(dev, "SATA disk hardreset fail: %d\n", rc);
			return rc;
		}

		while (retry-- > 0) {
			rc = hisi_sas_softreset_ata_disk(device);
			if (!rc)
				break;
		}
		break;
	default:
		break;
	}

	return rc;
}

static int hisi_sas_dev_found(struct domain_device *device)
{
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(device);
	struct domain_device *parent_dev = device->parent;
	struct hisi_sas_device *sas_dev;
	struct device *dev = hisi_hba->dev;
	int rc;

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

	if (parent_dev && dev_is_expander(parent_dev->dev_type)) {
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
			rc = -EINVAL;
			goto err_out;
		}
	}

	dev_info(dev, "dev[%d:%x] found\n",
		sas_dev->device_id, sas_dev->dev_type);

	rc = hisi_sas_init_device(device);
	if (rc)
		goto err_out;
	sas_dev->dev_status = HISI_SAS_DEV_NORMAL;
	return 0;

err_out:
	hisi_sas_dev_gone(device);
	return rc;
}

int hisi_sas_slave_configure(struct scsi_device *sdev)
{
	struct domain_device *dev = sdev_to_domain_dev(sdev);
	int ret = sas_slave_configure(sdev);

	if (ret)
		return ret;
	if (!dev_is_sata(dev))
		sas_change_queue_depth(sdev, 64);

	return 0;
}
EXPORT_SYMBOL_GPL(hisi_sas_slave_configure);

void hisi_sas_scan_start(struct Scsi_Host *shost)
{
	struct hisi_hba *hisi_hba = shost_priv(shost);

	hisi_hba->hw->phys_init(hisi_hba);
}
EXPORT_SYMBOL_GPL(hisi_sas_scan_start);

int hisi_sas_scan_finished(struct Scsi_Host *shost, unsigned long time)
{
	struct hisi_hba *hisi_hba = shost_priv(shost);
	struct sas_ha_struct *sha = &hisi_hba->sha;

	/* Wait for PHY up interrupt to occur */
	if (time < HZ)
		return 0;

	sas_drain_work(sha);
	return 1;
}
EXPORT_SYMBOL_GPL(hisi_sas_scan_finished);

static void hisi_sas_phyup_work(struct work_struct *work)
{
	struct hisi_sas_phy *phy =
		container_of(work, typeof(*phy), works[HISI_PHYE_PHY_UP]);
	struct hisi_hba *hisi_hba = phy->hisi_hba;
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	int phy_no = sas_phy->id;

	if (phy->identify.target_port_protocols == SAS_PROTOCOL_SSP)
		hisi_hba->hw->sl_notify_ssp(hisi_hba, phy_no);
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

static void hisi_sas_wait_phyup_timedout(struct timer_list *t)
{
	struct hisi_sas_phy *phy = from_timer(phy, t, timer);
	struct hisi_hba *hisi_hba = phy->hisi_hba;
	struct device *dev = hisi_hba->dev;
	int phy_no = phy->sas_phy.id;

	dev_warn(dev, "phy%d wait phyup timeout, issuing link reset\n", phy_no);
	hisi_sas_notify_phy_event(phy, HISI_PHYE_LINK_RESET);
}

void hisi_sas_phy_oob_ready(struct hisi_hba *hisi_hba, int phy_no)
{
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct device *dev = hisi_hba->dev;

	if (!timer_pending(&phy->timer)) {
		dev_dbg(dev, "phy%d OOB ready\n", phy_no);
		phy->timer.expires = jiffies + HISI_SAS_WAIT_PHYUP_TIMEOUT * HZ;
		add_timer(&phy->timer);
	}
}
EXPORT_SYMBOL_GPL(hisi_sas_phy_oob_ready);

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

	spin_lock_init(&phy->lock);

	timer_setup(&phy->timer, hisi_sas_wait_phyup_timedout, 0);
}

/* Wrapper to ensure we track hisi_sas_phy.enable properly */
void hisi_sas_phy_enable(struct hisi_hba *hisi_hba, int phy_no, int enable)
{
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct asd_sas_phy *aphy = &phy->sas_phy;
	struct sas_phy *sphy = aphy->phy;
	unsigned long flags;

	spin_lock_irqsave(&phy->lock, flags);

	if (enable) {
		/* We may have been enabled already; if so, don't touch */
		if (!phy->enable)
			sphy->negotiated_linkrate = SAS_LINK_RATE_UNKNOWN;
		hisi_hba->hw->phy_start(hisi_hba, phy_no);
	} else {
		sphy->negotiated_linkrate = SAS_PHY_DISABLED;
		hisi_hba->hw->phy_disable(hisi_hba, phy_no);
	}
	phy->enable = enable;
	spin_unlock_irqrestore(&phy->lock, flags);
}
EXPORT_SYMBOL_GPL(hisi_sas_phy_enable);

static void hisi_sas_port_notify_formed(struct asd_sas_phy *sas_phy)
{
	struct sas_ha_struct *sas_ha = sas_phy->ha;
	struct hisi_hba *hisi_hba = sas_ha->lldd_ha;
	struct hisi_sas_phy *phy = sas_phy->lldd_phy;
	struct asd_sas_port *sas_port = sas_phy->port;
	struct hisi_sas_port *port;
	unsigned long flags;

	if (!sas_port)
		return;

	port = to_hisi_sas_port(sas_port);
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
		if (!slot->is_internal && task->task_proto != SAS_PROTOCOL_SMP)
			task->task_state_flags |= SAS_TASK_STATE_DONE;
		spin_unlock_irqrestore(&task->task_state_lock, flags);
	}

	hisi_sas_slot_task_free(hisi_hba, task, slot);
}

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
	int ret = 0;

	dev_info(dev, "dev[%d:%x] is gone\n",
		 sas_dev->device_id, sas_dev->dev_type);

	down(&hisi_hba->sem);
	if (!test_bit(HISI_SAS_RESET_BIT, &hisi_hba->flags)) {
		hisi_sas_internal_task_abort(hisi_hba, device,
					     HISI_SAS_INT_ABT_DEV, 0);

		hisi_sas_dereg_device(hisi_hba, device);

		ret = hisi_hba->hw->clear_itct(hisi_hba, sas_dev);
		device->lldd_dev = NULL;
	}

	if (hisi_hba->hw->free_device)
		hisi_hba->hw->free_device(sas_dev);

	/* Don't mark it as SAS_PHY_UNUSED if failed to clear ITCT */
	if (!ret)
		sas_dev->dev_type = SAS_PHY_UNUSED;
	sas_dev->sas_device = NULL;
	up(&hisi_hba->sem);
}

static int hisi_sas_queue_command(struct sas_task *task, gfp_t gfp_flags)
{
	return hisi_sas_task_exec(task, gfp_flags, 0, NULL);
}

static int hisi_sas_phy_set_linkrate(struct hisi_hba *hisi_hba, int phy_no,
			struct sas_phy_linkrates *r)
{
	struct sas_phy_linkrates _r;

	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	enum sas_linkrate min, max;

	if (r->minimum_linkrate > SAS_LINK_RATE_1_5_GBPS)
		return -EINVAL;

	if (r->maximum_linkrate == SAS_LINK_RATE_UNKNOWN) {
		max = sas_phy->phy->maximum_linkrate;
		min = r->minimum_linkrate;
	} else if (r->minimum_linkrate == SAS_LINK_RATE_UNKNOWN) {
		max = r->maximum_linkrate;
		min = sas_phy->phy->minimum_linkrate;
	} else
		return -EINVAL;

	_r.maximum_linkrate = max;
	_r.minimum_linkrate = min;

	sas_phy->phy->maximum_linkrate = max;
	sas_phy->phy->minimum_linkrate = min;

	hisi_sas_phy_enable(hisi_hba, phy_no, 0);
	msleep(100);
	hisi_hba->hw->phy_set_linkrate(hisi_hba, phy_no, &_r);
	hisi_sas_phy_enable(hisi_hba, phy_no, 1);

	return 0;
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
		hisi_sas_phy_enable(hisi_hba, phy_no, 0);
		msleep(100);
		hisi_sas_phy_enable(hisi_hba, phy_no, 1);
		break;

	case PHY_FUNC_DISABLE:
		hisi_sas_phy_enable(hisi_hba, phy_no, 0);
		break;

	case PHY_FUNC_SET_LINK_RATE:
		return hisi_sas_phy_set_linkrate(hisi_hba, phy_no, funcdata);
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
	del_timer(&task->slow_task->timer);
	complete(&task->slow_task->completion);
}

static void hisi_sas_tmf_timedout(struct timer_list *t)
{
	struct sas_task_slow *slow = from_timer(slow, t, timer);
	struct sas_task *task = slow->task;
	unsigned long flags;
	bool is_completed = true;

	spin_lock_irqsave(&task->task_state_lock, flags);
	if (!(task->task_state_flags & SAS_TASK_STATE_DONE)) {
		task->task_state_flags |= SAS_TASK_STATE_ABORTED;
		is_completed = false;
	}
	spin_unlock_irqrestore(&task->task_state_lock, flags);

	if (!is_completed)
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
		task->slow_task->timer.expires = jiffies + TASK_TIMEOUT * HZ;
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
				if (slot) {
					struct hisi_sas_cq *cq =
					       &hisi_hba->cq[slot->dlvry_queue];
					/*
					 * sync irq to avoid free'ing task
					 * before using task in IO completion
					 */
					synchronize_irq(cq->irq_no);
					slot->task = NULL;
				}

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
			dev_warn(dev, "abort tmf: task to dev %016llx resp: 0x%x sts 0x%x underrun\n",
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

		if (task->task_status.resp == SAS_TASK_COMPLETE &&
		    task->task_status.stat == SAS_OPEN_REJECT) {
			dev_warn(dev, "abort tmf: open reject failed\n");
			res = -EIO;
		} else {
			dev_warn(dev, "abort tmf: task to dev %016llx resp: 0x%x status 0x%x\n",
				 SAS_ADDR(device->sas_addr),
				 task->task_status.resp,
				 task->task_status.stat);
		}
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

	if (rc == TMF_RESP_FUNC_COMPLETE)
		hisi_sas_release_task(hisi_hba, device);

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

static void hisi_sas_rescan_topology(struct hisi_hba *hisi_hba, u32 state)
{
	struct sas_ha_struct *sas_ha = &hisi_hba->sha;
	struct asd_sas_port *_sas_port = NULL;
	int phy_no;

	for (phy_no = 0; phy_no < hisi_hba->n_phy; phy_no++) {
		struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
		struct asd_sas_phy *sas_phy = &phy->sas_phy;
		struct asd_sas_port *sas_port = sas_phy->port;
		bool do_port_check = _sas_port != sas_port;

		if (!sas_phy->phy->enabled)
			continue;

		/* Report PHY state change to libsas */
		if (state & BIT(phy_no)) {
			if (do_port_check && sas_port && sas_port->port_dev) {
				struct domain_device *dev = sas_port->port_dev;

				_sas_port = sas_port;

				if (dev_is_expander(dev->dev_type))
					sas_ha->notify_port_event(sas_phy,
							PORTE_BROADCAST_RCVD);
			}
		} else {
			hisi_sas_phy_down(hisi_hba, phy_no, 0);
		}

	}
}

static void hisi_sas_reset_init_all_devices(struct hisi_hba *hisi_hba)
{
	struct hisi_sas_device *sas_dev;
	struct domain_device *device;
	int i;

	for (i = 0; i < HISI_SAS_MAX_DEVICES; i++) {
		sas_dev = &hisi_hba->devices[i];
		device = sas_dev->sas_device;

		if ((sas_dev->dev_type == SAS_PHY_UNUSED) || !device)
			continue;

		hisi_sas_init_device(device);
	}
}

static void hisi_sas_send_ata_reset_each_phy(struct hisi_hba *hisi_hba,
					     struct asd_sas_port *sas_port,
					     struct domain_device *device)
{
	struct hisi_sas_tmf_task tmf_task = { .force_phy = 1 };
	struct ata_port *ap = device->sata_dev.ap;
	struct device *dev = hisi_hba->dev;
	int s = sizeof(struct host_to_dev_fis);
	int rc = TMF_RESP_FUNC_FAILED;
	struct asd_sas_phy *sas_phy;
	struct ata_link *link;
	u8 fis[20] = {0};
	u32 state;

	state = hisi_hba->hw->get_phys_state(hisi_hba);
	list_for_each_entry(sas_phy, &sas_port->phy_list, port_phy_el) {
		if (!(state & BIT(sas_phy->id)))
			continue;

		ata_for_each_link(link, ap, EDGE) {
			int pmp = sata_srst_pmp(link);

			tmf_task.phy_id = sas_phy->id;
			hisi_sas_fill_ata_reset_cmd(link->device, 1, pmp, fis);
			rc = hisi_sas_exec_internal_tmf_task(device, fis, s,
							     &tmf_task);
			if (rc != TMF_RESP_FUNC_COMPLETE) {
				dev_err(dev, "phy%d ata reset failed rc=%d\n",
					sas_phy->id, rc);
				break;
			}
		}
	}
}

static void hisi_sas_terminate_stp_reject(struct hisi_hba *hisi_hba)
{
	struct device *dev = hisi_hba->dev;
	int port_no, rc, i;

	for (i = 0; i < HISI_SAS_MAX_DEVICES; i++) {
		struct hisi_sas_device *sas_dev = &hisi_hba->devices[i];
		struct domain_device *device = sas_dev->sas_device;

		if ((sas_dev->dev_type == SAS_PHY_UNUSED) || !device)
			continue;

		rc = hisi_sas_internal_task_abort(hisi_hba, device,
						  HISI_SAS_INT_ABT_DEV, 0);
		if (rc < 0)
			dev_err(dev, "STP reject: abort dev failed %d\n", rc);
	}

	for (port_no = 0; port_no < hisi_hba->n_phy; port_no++) {
		struct hisi_sas_port *port = &hisi_hba->port[port_no];
		struct asd_sas_port *sas_port = &port->sas_port;
		struct domain_device *port_dev = sas_port->port_dev;
		struct domain_device *device;

		if (!port_dev || !dev_is_expander(port_dev->dev_type))
			continue;

		/* Try to find a SATA device */
		list_for_each_entry(device, &sas_port->dev_list,
				    dev_list_node) {
			if (dev_is_sata(device)) {
				hisi_sas_send_ata_reset_each_phy(hisi_hba,
								 sas_port,
								 device);
				break;
			}
		}
	}
}

void hisi_sas_controller_reset_prepare(struct hisi_hba *hisi_hba)
{
	struct Scsi_Host *shost = hisi_hba->shost;

	down(&hisi_hba->sem);
	hisi_hba->phy_state = hisi_hba->hw->get_phys_state(hisi_hba);

	scsi_block_requests(shost);
	hisi_hba->hw->wait_cmds_complete_timeout(hisi_hba, 100, 5000);

	if (timer_pending(&hisi_hba->timer))
		del_timer_sync(&hisi_hba->timer);

	set_bit(HISI_SAS_REJECT_CMD_BIT, &hisi_hba->flags);
}
EXPORT_SYMBOL_GPL(hisi_sas_controller_reset_prepare);

void hisi_sas_controller_reset_done(struct hisi_hba *hisi_hba)
{
	struct Scsi_Host *shost = hisi_hba->shost;
	u32 state;

	/* Init and wait for PHYs to come up and all libsas event finished. */
	hisi_hba->hw->phys_init(hisi_hba);
	msleep(1000);
	hisi_sas_refresh_port_id(hisi_hba);
	clear_bit(HISI_SAS_REJECT_CMD_BIT, &hisi_hba->flags);

	if (hisi_hba->reject_stp_links_msk)
		hisi_sas_terminate_stp_reject(hisi_hba);
	hisi_sas_reset_init_all_devices(hisi_hba);
	up(&hisi_hba->sem);
	scsi_unblock_requests(shost);
	clear_bit(HISI_SAS_RESET_BIT, &hisi_hba->flags);

	state = hisi_hba->hw->get_phys_state(hisi_hba);
	hisi_sas_rescan_topology(hisi_hba, state);
}
EXPORT_SYMBOL_GPL(hisi_sas_controller_reset_done);

static int hisi_sas_controller_reset(struct hisi_hba *hisi_hba)
{
	struct device *dev = hisi_hba->dev;
	struct Scsi_Host *shost = hisi_hba->shost;
	int rc;

	if (hisi_sas_debugfs_enable && hisi_hba->debugfs_itct[0].itct)
		queue_work(hisi_hba->wq, &hisi_hba->debugfs_work);

	if (!hisi_hba->hw->soft_reset)
		return -1;

	if (test_and_set_bit(HISI_SAS_RESET_BIT, &hisi_hba->flags))
		return -1;

	dev_info(dev, "controller resetting...\n");
	hisi_sas_controller_reset_prepare(hisi_hba);

	rc = hisi_hba->hw->soft_reset(hisi_hba);
	if (rc) {
		dev_warn(dev, "controller reset failed (%d)\n", rc);
		clear_bit(HISI_SAS_REJECT_CMD_BIT, &hisi_hba->flags);
		up(&hisi_hba->sem);
		scsi_unblock_requests(shost);
		clear_bit(HISI_SAS_RESET_BIT, &hisi_hba->flags);
		return rc;
	}

	hisi_sas_controller_reset_done(hisi_hba);
	dev_info(dev, "controller reset complete\n");

	return 0;
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
		struct hisi_sas_slot *slot = task->lldd_task;
		struct hisi_sas_cq *cq;

		if (slot) {
			/*
			 * sync irq to avoid free'ing task
			 * before using task in IO completion
			 */
			cq = &hisi_hba->cq[slot->dlvry_queue];
			synchronize_irq(cq->irq_no);
		}
		spin_unlock_irqrestore(&task->task_state_lock, flags);
		rc = TMF_RESP_FUNC_COMPLETE;
		goto out;
	}
	task->task_state_flags |= SAS_TASK_STATE_ABORTED;
	spin_unlock_irqrestore(&task->task_state_lock, flags);

	if (task->lldd_task && task->task_proto & SAS_PROTOCOL_SSP) {
		struct scsi_cmnd *cmnd = task->uldd_task;
		struct hisi_sas_slot *slot = task->lldd_task;
		u16 tag = slot->idx;
		int rc2;

		int_to_scsilun(cmnd->device->lun, &lun);
		tmf_task.tmf = TMF_ABORT_TASK;
		tmf_task.tag_of_task_to_be_managed = tag;

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
			if (task->lldd_task)
				hisi_sas_do_release_task(hisi_hba, task, slot);
		}
	} else if (task->task_proto & SAS_PROTOCOL_SATA ||
		task->task_proto & SAS_PROTOCOL_STP) {
		if (task->dev->dev_type == SAS_SATA_DEV) {
			rc = hisi_sas_internal_task_abort(hisi_hba, device,
							  HISI_SAS_INT_ABT_DEV,
							  0);
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
		struct hisi_sas_cq *cq = &hisi_hba->cq[slot->dlvry_queue];

		rc = hisi_sas_internal_task_abort(hisi_hba, device,
						  HISI_SAS_INT_ABT_CMD, tag);
		if (((rc < 0) || (rc == TMF_RESP_FUNC_FAILED)) &&
					task->lldd_task) {
			/*
			 * sync irq to avoid free'ing task
			 * before using task in IO completion
			 */
			synchronize_irq(cq->irq_no);
			slot->task = NULL;
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
	int rc;

	rc = hisi_sas_internal_task_abort(hisi_hba, device,
					  HISI_SAS_INT_ABT_DEV, 0);
	if (rc < 0) {
		dev_err(dev, "abort task set: internal abort rc=%d\n", rc);
		return TMF_RESP_FUNC_FAILED;
	}
	hisi_sas_dereg_device(hisi_hba, device);

	tmf_task.tmf = TMF_ABORT_TASK_SET;
	rc = hisi_sas_debug_issue_ssp_tmf(device, lun, &tmf_task);

	if (rc == TMF_RESP_FUNC_COMPLETE)
		hisi_sas_release_task(hisi_hba, device);

	return rc;
}

static int hisi_sas_clear_aca(struct domain_device *device, u8 *lun)
{
	struct hisi_sas_tmf_task tmf_task;
	int rc;

	tmf_task.tmf = TMF_CLEAR_ACA;
	rc = hisi_sas_debug_issue_ssp_tmf(device, lun, &tmf_task);

	return rc;
}

static int hisi_sas_debug_I_T_nexus_reset(struct domain_device *device)
{
	struct sas_phy *local_phy = sas_get_local_phy(device);
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(device);
	struct sas_ha_struct *sas_ha = &hisi_hba->sha;
	DECLARE_COMPLETION_ONSTACK(phyreset);
	int rc, reset_type;

	if (!local_phy->enabled) {
		sas_put_local_phy(local_phy);
		return -ENODEV;
	}

	if (scsi_is_sas_phy_local(local_phy)) {
		struct asd_sas_phy *sas_phy =
			sas_ha->sas_phy[local_phy->number];
		struct hisi_sas_phy *phy =
			container_of(sas_phy, struct hisi_sas_phy, sas_phy);
		phy->in_reset = 1;
		phy->reset_completion = &phyreset;
	}

	reset_type = (sas_dev->dev_status == HISI_SAS_DEV_INIT ||
		      !dev_is_sata(device)) ? true : false;

	rc = sas_phy_reset(local_phy, reset_type);
	sas_put_local_phy(local_phy);

	if (scsi_is_sas_phy_local(local_phy)) {
		struct asd_sas_phy *sas_phy =
			sas_ha->sas_phy[local_phy->number];
		struct hisi_sas_phy *phy =
			container_of(sas_phy, struct hisi_sas_phy, sas_phy);
		int ret = wait_for_completion_timeout(&phyreset, 2 * HZ);
		unsigned long flags;

		spin_lock_irqsave(&phy->lock, flags);
		phy->reset_completion = NULL;
		phy->in_reset = 0;
		spin_unlock_irqrestore(&phy->lock, flags);

		/* report PHY down if timed out */
		if (!ret)
			hisi_sas_phy_down(hisi_hba, sas_phy->id, 0);
	} else if (sas_dev->dev_status != HISI_SAS_DEV_INIT) {
		/*
		 * If in init state, we rely on caller to wait for link to be
		 * ready; otherwise, except phy reset is fail, delay.
		 */
		if (!rc)
			msleep(2000);
	}

	return rc;
}

static int hisi_sas_I_T_nexus_reset(struct domain_device *device)
{
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(device);
	struct device *dev = hisi_hba->dev;
	int rc;

	rc = hisi_sas_internal_task_abort(hisi_hba, device,
					  HISI_SAS_INT_ABT_DEV, 0);
	if (rc < 0) {
		dev_err(dev, "I_T nexus reset: internal abort (%d)\n", rc);
		return TMF_RESP_FUNC_FAILED;
	}
	hisi_sas_dereg_device(hisi_hba, device);

	if (dev_is_sata(device)) {
		rc = hisi_sas_softreset_ata_disk(device);
		if (rc == TMF_RESP_FUNC_FAILED)
			return TMF_RESP_FUNC_FAILED;
	}

	rc = hisi_sas_debug_I_T_nexus_reset(device);

	if ((rc == TMF_RESP_FUNC_COMPLETE) || (rc == -ENODEV))
		hisi_sas_release_task(hisi_hba, device);

	return rc;
}

static int hisi_sas_lu_reset(struct domain_device *device, u8 *lun)
{
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(device);
	struct device *dev = hisi_hba->dev;
	int rc = TMF_RESP_FUNC_FAILED;

	/* Clear internal IO and then lu reset */
	rc = hisi_sas_internal_task_abort(hisi_hba, device,
					  HISI_SAS_INT_ABT_DEV, 0);
	if (rc < 0) {
		dev_err(dev, "lu_reset: internal abort failed\n");
		goto out;
	}
	hisi_sas_dereg_device(hisi_hba, device);

	if (dev_is_sata(device)) {
		struct sas_phy *phy;

		phy = sas_get_local_phy(device);

		rc = sas_phy_reset(phy, true);

		if (rc == 0)
			hisi_sas_release_task(hisi_hba, device);
		sas_put_local_phy(phy);
	} else {
		struct hisi_sas_tmf_task tmf_task = { .tmf =  TMF_LU_RESET };

		rc = hisi_sas_debug_issue_ssp_tmf(device, lun, &tmf_task);
		if (rc == TMF_RESP_FUNC_COMPLETE)
			hisi_sas_release_task(hisi_hba, device);
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
	struct device *dev = hisi_hba->dev;
	HISI_SAS_DECLARE_RST_WORK_ON_STACK(r);
	int rc, i;

	queue_work(hisi_hba->wq, &r.work);
	wait_for_completion(r.completion);
	if (!r.done)
		return TMF_RESP_FUNC_FAILED;

	for (i = 0; i < HISI_SAS_MAX_DEVICES; i++) {
		struct hisi_sas_device *sas_dev = &hisi_hba->devices[i];
		struct domain_device *device = sas_dev->sas_device;

		if ((sas_dev->dev_type == SAS_PHY_UNUSED) || !device ||
		    dev_is_expander(device->dev_type))
			continue;

		rc = hisi_sas_debug_I_T_nexus_reset(device);
		if (rc != TMF_RESP_FUNC_COMPLETE)
			dev_info(dev, "clear nexus ha: for device[%d] rc=%d\n",
				 sas_dev->device_id, rc);
	}

	hisi_sas_release_tasks(hisi_hba);

	return TMF_RESP_FUNC_COMPLETE;
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
		tmf_task.tag_of_task_to_be_managed = tag;

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
				  int task_tag, struct hisi_sas_dq *dq)
{
	struct domain_device *device = task->dev;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct device *dev = hisi_hba->dev;
	struct hisi_sas_port *port;
	struct hisi_sas_slot *slot;
	struct asd_sas_port *sas_port = device->port;
	struct hisi_sas_cmd_hdr *cmd_hdr_base;
	int dlvry_queue_slot, dlvry_queue, n_elem = 0, rc, slot_idx;
	unsigned long flags;
	int wr_q_index;

	if (unlikely(test_bit(HISI_SAS_REJECT_CMD_BIT, &hisi_hba->flags)))
		return -EINVAL;

	if (!device->port)
		return -1;

	port = to_hisi_sas_port(sas_port);

	/* simply get a slot and send abort command */
	rc = hisi_sas_slot_index_alloc(hisi_hba, NULL);
	if (rc < 0)
		goto err_out;

	slot_idx = rc;
	slot = &hisi_hba->slot_info[slot_idx];

	spin_lock(&dq->lock);
	wr_q_index = dq->wr_point;
	dq->wr_point = (dq->wr_point + 1) % HISI_SAS_QUEUE_SLOTS;
	list_add_tail(&slot->delivery, &dq->list);
	spin_unlock(&dq->lock);
	spin_lock(&sas_dev->lock);
	list_add_tail(&slot->entry, &sas_dev->list);
	spin_unlock(&sas_dev->lock);

	dlvry_queue = dq->id;
	dlvry_queue_slot = wr_q_index;

	slot->device_id = sas_dev->device_id;
	slot->n_elem = n_elem;
	slot->dlvry_queue = dlvry_queue;
	slot->dlvry_queue_slot = dlvry_queue_slot;
	cmd_hdr_base = hisi_hba->cmd_hdr[dlvry_queue];
	slot->cmd_hdr = &cmd_hdr_base[dlvry_queue_slot];
	slot->task = task;
	slot->port = port;
	slot->is_internal = true;
	task->lldd_task = slot;

	memset(slot->cmd_hdr, 0, sizeof(struct hisi_sas_cmd_hdr));
	memset(hisi_sas_cmd_hdr_addr_mem(slot), 0, HISI_SAS_COMMAND_TABLE_SZ);
	memset(hisi_sas_status_buf_addr_mem(slot), 0,
	       sizeof(struct hisi_sas_err_record));

	hisi_sas_task_prep_abort(hisi_hba, slot, device_id,
				      abort_flag, task_tag);

	spin_lock_irqsave(&task->task_state_lock, flags);
	task->task_state_flags |= SAS_TASK_AT_INITIATOR;
	spin_unlock_irqrestore(&task->task_state_lock, flags);
	WRITE_ONCE(slot->ready, 1);
	/* send abort command to the chip */
	spin_lock(&dq->lock);
	hisi_hba->hw->start_delivery(dq);
	spin_unlock(&dq->lock);

	return 0;

err_out:
	dev_err(dev, "internal abort task prep: failed[%d]!\n", rc);

	return rc;
}

/**
 * _hisi_sas_internal_task_abort -- execute an internal
 * abort command for single IO command or a device
 * @hisi_hba: host controller struct
 * @device: domain device
 * @abort_flag: mode of operation, device or single IO
 * @tag: tag of IO to be aborted (only relevant to single
 *       IO mode)
 * @dq: delivery queue for this internal abort command
 */
static int
_hisi_sas_internal_task_abort(struct hisi_hba *hisi_hba,
			      struct domain_device *device, int abort_flag,
			      int tag, struct hisi_sas_dq *dq)
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
	task->slow_task->timer.expires = jiffies + INTERNAL_ABORT_TIMEOUT * HZ;
	add_timer(&task->slow_task->timer);

	res = hisi_sas_internal_abort_task_exec(hisi_hba, sas_dev->device_id,
						task, abort_flag, tag, dq);
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
		if (hisi_sas_debugfs_enable && hisi_hba->debugfs_itct[0].itct)
			queue_work(hisi_hba->wq, &hisi_hba->debugfs_work);

		if (!(task->task_state_flags & SAS_TASK_STATE_DONE)) {
			struct hisi_sas_slot *slot = task->lldd_task;

			if (slot) {
				struct hisi_sas_cq *cq =
					&hisi_hba->cq[slot->dlvry_queue];
				/*
				 * sync irq to avoid free'ing task
				 * before using task in IO completion
				 */
				synchronize_irq(cq->irq_no);
				slot->task = NULL;
			}
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
	dev_dbg(dev, "internal task abort: task to dev %016llx task=%pK resp: 0x%x sts 0x%x\n",
		SAS_ADDR(device->sas_addr), task,
		task->task_status.resp, /* 0 is complete, -1 is undelivered */
		task->task_status.stat);
	sas_free_task(task);

	return res;
}

static int
hisi_sas_internal_task_abort(struct hisi_hba *hisi_hba,
			     struct domain_device *device,
			     int abort_flag, int tag)
{
	struct hisi_sas_slot *slot;
	struct device *dev = hisi_hba->dev;
	struct hisi_sas_dq *dq;
	int i, rc;

	switch (abort_flag) {
	case HISI_SAS_INT_ABT_CMD:
		slot = &hisi_hba->slot_info[tag];
		dq = &hisi_hba->dq[slot->dlvry_queue];
		return _hisi_sas_internal_task_abort(hisi_hba, device,
						     abort_flag, tag, dq);
	case HISI_SAS_INT_ABT_DEV:
		for (i = 0; i < hisi_hba->cq_nvecs; i++) {
			struct hisi_sas_cq *cq = &hisi_hba->cq[i];
			const struct cpumask *mask = cq->irq_mask;

			if (mask && !cpumask_intersects(cpu_online_mask, mask))
				continue;
			dq = &hisi_hba->dq[i];
			rc = _hisi_sas_internal_task_abort(hisi_hba, device,
							   abort_flag, tag,
							   dq);
			if (rc)
				return rc;
		}
		break;
	default:
		dev_err(dev, "Unrecognised internal abort flag (%d)\n",
			abort_flag);
		return -EINVAL;
	}

	return 0;
}

static void hisi_sas_port_formed(struct asd_sas_phy *sas_phy)
{
	hisi_sas_port_notify_formed(sas_phy);
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
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	struct sas_phy *sphy = sas_phy->phy;
	unsigned long flags;

	phy->phy_attached = 0;
	phy->phy_type = 0;
	phy->port = NULL;

	spin_lock_irqsave(&phy->lock, flags);
	if (phy->enable)
		sphy->negotiated_linkrate = SAS_LINK_RATE_UNKNOWN;
	else
		sphy->negotiated_linkrate = SAS_PHY_DISABLED;
	spin_unlock_irqrestore(&phy->lock, flags);
}

void hisi_sas_phy_down(struct hisi_hba *hisi_hba, int phy_no, int rdy)
{
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	struct sas_ha_struct *sas_ha = &hisi_hba->sha;
	struct device *dev = hisi_hba->dev;

	if (rdy) {
		/* Phy down but ready */
		hisi_sas_bytes_dmaed(hisi_hba, phy_no);
		hisi_sas_port_notify_formed(sas_phy);
	} else {
		struct hisi_sas_port *port  = phy->port;

		if (test_bit(HISI_SAS_RESET_BIT, &hisi_hba->flags) ||
		    phy->in_reset) {
			dev_info(dev, "ignore flutter phy%d down\n", phy_no);
			return;
		}
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

void hisi_sas_sync_irqs(struct hisi_hba *hisi_hba)
{
	int i;

	for (i = 0; i < hisi_hba->cq_nvecs; i++) {
		struct hisi_sas_cq *cq = &hisi_hba->cq[i];

		synchronize_irq(cq->irq_no);
	}
}
EXPORT_SYMBOL_GPL(hisi_sas_sync_irqs);

int hisi_sas_host_reset(struct Scsi_Host *shost, int reset_type)
{
	struct hisi_hba *hisi_hba = shost_priv(shost);

	if (reset_type != SCSI_ADAPTER_RESET)
		return -EOPNOTSUPP;

	queue_work(hisi_hba->wq, &hisi_hba->rst_work);

	return 0;
}
EXPORT_SYMBOL_GPL(hisi_sas_host_reset);

struct scsi_transport_template *hisi_sas_stt;
EXPORT_SYMBOL_GPL(hisi_sas_stt);

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
	.lldd_clear_nexus_ha	= hisi_sas_clear_nexus_ha,
	.lldd_port_formed	= hisi_sas_port_formed,
	.lldd_write_gpio	= hisi_sas_write_gpio,
};

void hisi_sas_init_mem(struct hisi_hba *hisi_hba)
{
	int i, s, j, max_command_entries = HISI_SAS_MAX_COMMANDS;
	struct hisi_sas_breakpoint *sata_breakpoint = hisi_hba->sata_breakpoint;

	for (i = 0; i < hisi_hba->queue_count; i++) {
		struct hisi_sas_cq *cq = &hisi_hba->cq[i];
		struct hisi_sas_dq *dq = &hisi_hba->dq[i];
		struct hisi_sas_cmd_hdr *cmd_hdr = hisi_hba->cmd_hdr[i];

		s = sizeof(struct hisi_sas_cmd_hdr);
		for (j = 0; j < HISI_SAS_QUEUE_SLOTS; j++)
			memset(&cmd_hdr[j], 0, s);

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

	s = sizeof(struct hisi_sas_sata_breakpoint);
	for (j = 0; j < HISI_SAS_MAX_ITCT_ENTRIES; j++)
		memset(&sata_breakpoint[j], 0, s);
}
EXPORT_SYMBOL_GPL(hisi_sas_init_mem);

int hisi_sas_alloc(struct hisi_hba *hisi_hba)
{
	struct device *dev = hisi_hba->dev;
	int i, j, s, max_command_entries = HISI_SAS_MAX_COMMANDS;
	int max_command_entries_ru, sz_slot_buf_ru;
	int blk_cnt, slots_per_blk;

	sema_init(&hisi_hba->sem, 1);
	spin_lock_init(&hisi_hba->lock);
	for (i = 0; i < hisi_hba->n_phy; i++) {
		hisi_sas_phy_init(hisi_hba, i);
		hisi_hba->port[i].port_attached = 0;
		hisi_hba->port[i].id = -1;
	}

	for (i = 0; i < HISI_SAS_MAX_DEVICES; i++) {
		hisi_hba->devices[i].dev_type = SAS_PHY_UNUSED;
		hisi_hba->devices[i].device_id = i;
		hisi_hba->devices[i].dev_status = HISI_SAS_DEV_INIT;
	}

	for (i = 0; i < hisi_hba->queue_count; i++) {
		struct hisi_sas_cq *cq = &hisi_hba->cq[i];
		struct hisi_sas_dq *dq = &hisi_hba->dq[i];

		/* Completion queue structure */
		cq->id = i;
		cq->hisi_hba = hisi_hba;

		/* Delivery queue structure */
		spin_lock_init(&dq->lock);
		INIT_LIST_HEAD(&dq->list);
		dq->id = i;
		dq->hisi_hba = hisi_hba;

		/* Delivery queue */
		s = sizeof(struct hisi_sas_cmd_hdr) * HISI_SAS_QUEUE_SLOTS;
		hisi_hba->cmd_hdr[i] = dmam_alloc_coherent(dev, s,
						&hisi_hba->cmd_hdr_dma[i],
						GFP_KERNEL);
		if (!hisi_hba->cmd_hdr[i])
			goto err_out;

		/* Completion queue */
		s = hisi_hba->hw->complete_hdr_size * HISI_SAS_QUEUE_SLOTS;
		hisi_hba->complete_hdr[i] = dmam_alloc_coherent(dev, s,
						&hisi_hba->complete_hdr_dma[i],
						GFP_KERNEL);
		if (!hisi_hba->complete_hdr[i])
			goto err_out;
	}

	s = HISI_SAS_MAX_ITCT_ENTRIES * sizeof(struct hisi_sas_itct);
	hisi_hba->itct = dmam_alloc_coherent(dev, s, &hisi_hba->itct_dma,
					     GFP_KERNEL);
	if (!hisi_hba->itct)
		goto err_out;

	hisi_hba->slot_info = devm_kcalloc(dev, max_command_entries,
					   sizeof(struct hisi_sas_slot),
					   GFP_KERNEL);
	if (!hisi_hba->slot_info)
		goto err_out;

	/* roundup to avoid overly large block size */
	max_command_entries_ru = roundup(max_command_entries, 64);
	if (hisi_hba->prot_mask & HISI_SAS_DIX_PROT_MASK)
		sz_slot_buf_ru = sizeof(struct hisi_sas_slot_dif_buf_table);
	else
		sz_slot_buf_ru = sizeof(struct hisi_sas_slot_buf_table);
	sz_slot_buf_ru = roundup(sz_slot_buf_ru, 64);
	s = max(lcm(max_command_entries_ru, sz_slot_buf_ru), PAGE_SIZE);
	blk_cnt = (max_command_entries_ru * sz_slot_buf_ru) / s;
	slots_per_blk = s / sz_slot_buf_ru;

	for (i = 0; i < blk_cnt; i++) {
		int slot_index = i * slots_per_blk;
		dma_addr_t buf_dma;
		void *buf;

		buf = dmam_alloc_coherent(dev, s, &buf_dma,
					  GFP_KERNEL);
		if (!buf)
			goto err_out;

		for (j = 0; j < slots_per_blk; j++, slot_index++) {
			struct hisi_sas_slot *slot;

			slot = &hisi_hba->slot_info[slot_index];
			slot->buf = buf;
			slot->buf_dma = buf_dma;
			slot->idx = slot_index;

			buf += sz_slot_buf_ru;
			buf_dma += sz_slot_buf_ru;
		}
	}

	s = max_command_entries * sizeof(struct hisi_sas_iost);
	hisi_hba->iost = dmam_alloc_coherent(dev, s, &hisi_hba->iost_dma,
					     GFP_KERNEL);
	if (!hisi_hba->iost)
		goto err_out;

	s = max_command_entries * sizeof(struct hisi_sas_breakpoint);
	hisi_hba->breakpoint = dmam_alloc_coherent(dev, s,
						   &hisi_hba->breakpoint_dma,
						   GFP_KERNEL);
	if (!hisi_hba->breakpoint)
		goto err_out;

	hisi_hba->slot_index_count = max_command_entries;
	s = hisi_hba->slot_index_count / BITS_PER_BYTE;
	hisi_hba->slot_index_tags = devm_kzalloc(dev, s, GFP_KERNEL);
	if (!hisi_hba->slot_index_tags)
		goto err_out;

	s = sizeof(struct hisi_sas_initial_fis) * HISI_SAS_MAX_PHYS;
	hisi_hba->initial_fis = dmam_alloc_coherent(dev, s,
						    &hisi_hba->initial_fis_dma,
						    GFP_KERNEL);
	if (!hisi_hba->initial_fis)
		goto err_out;

	s = HISI_SAS_MAX_ITCT_ENTRIES * sizeof(struct hisi_sas_sata_breakpoint);
	hisi_hba->sata_breakpoint = dmam_alloc_coherent(dev, s,
					&hisi_hba->sata_breakpoint_dma,
					GFP_KERNEL);
	if (!hisi_hba->sata_breakpoint)
		goto err_out;

	hisi_sas_slot_index_init(hisi_hba);
	hisi_hba->last_slot_index = HISI_SAS_UNRESERVED_IPTT;

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
	int i;

	for (i = 0; i < hisi_hba->n_phy; i++) {
		struct hisi_sas_phy *phy = &hisi_hba->phy[i];

		del_timer_sync(&phy->timer);
	}

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
			dev_err(dev, "could not get property ctrl-reset-reg\n");
			return -ENOENT;
		}

		if (device_property_read_u32(dev, "ctrl-reset-sts-reg",
					     &hisi_hba->ctrl_reset_sts_reg)) {
			dev_err(dev, "could not get property ctrl-reset-sts-reg\n");
			return -ENOENT;
		}

		if (device_property_read_u32(dev, "ctrl-clock-ena-reg",
					     &hisi_hba->ctrl_clock_ena_reg)) {
			dev_err(dev, "could not get property ctrl-clock-ena-reg\n");
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
	int error;

	shost = scsi_host_alloc(hw->sht, sizeof(*hisi_hba));
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

	error = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (error)
		error = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));

	if (error) {
		dev_err(dev, "No usable DMA addressing method\n");
		goto err_out;
	}

	hisi_hba->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hisi_hba->regs))
		goto err_out;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res) {
		hisi_hba->sgpio_regs = devm_ioremap_resource(dev, res);
		if (IS_ERR(hisi_hba->sgpio_regs))
			goto err_out;
	}

	if (hisi_sas_alloc(hisi_hba)) {
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
	if (hisi_hba->hw->slot_index_alloc) {
		shost->can_queue = HISI_SAS_MAX_COMMANDS;
		shost->cmd_per_lun = HISI_SAS_MAX_COMMANDS;
	} else {
		shost->can_queue = HISI_SAS_UNRESERVED_IPTT;
		shost->cmd_per_lun = HISI_SAS_UNRESERVED_IPTT;
	}

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
	hisi_sas_debugfs_exit(hisi_hba);
	hisi_sas_free(hisi_hba);
	scsi_host_put(shost);
	return rc;
}
EXPORT_SYMBOL_GPL(hisi_sas_probe);

struct dentry *hisi_sas_debugfs_dir;

static void hisi_sas_debugfs_snapshot_cq_reg(struct hisi_hba *hisi_hba)
{
	int queue_entry_size = hisi_hba->hw->complete_hdr_size;
	int dump_index = hisi_hba->debugfs_dump_index;
	int i;

	for (i = 0; i < hisi_hba->queue_count; i++)
		memcpy(hisi_hba->debugfs_cq[dump_index][i].complete_hdr,
		       hisi_hba->complete_hdr[i],
		       HISI_SAS_QUEUE_SLOTS * queue_entry_size);
}

static void hisi_sas_debugfs_snapshot_dq_reg(struct hisi_hba *hisi_hba)
{
	int queue_entry_size = sizeof(struct hisi_sas_cmd_hdr);
	int dump_index = hisi_hba->debugfs_dump_index;
	int i;

	for (i = 0; i < hisi_hba->queue_count; i++) {
		struct hisi_sas_cmd_hdr *debugfs_cmd_hdr, *cmd_hdr;
		int j;

		debugfs_cmd_hdr = hisi_hba->debugfs_dq[dump_index][i].hdr;
		cmd_hdr = hisi_hba->cmd_hdr[i];

		for (j = 0; j < HISI_SAS_QUEUE_SLOTS; j++)
			memcpy(&debugfs_cmd_hdr[j], &cmd_hdr[j],
			       queue_entry_size);
	}
}

static void hisi_sas_debugfs_snapshot_port_reg(struct hisi_hba *hisi_hba)
{
	int dump_index = hisi_hba->debugfs_dump_index;
	const struct hisi_sas_debugfs_reg *port =
		hisi_hba->hw->debugfs_reg_port;
	int i, phy_cnt;
	u32 offset;
	u32 *databuf;

	for (phy_cnt = 0; phy_cnt < hisi_hba->n_phy; phy_cnt++) {
		databuf = hisi_hba->debugfs_port_reg[dump_index][phy_cnt].data;
		for (i = 0; i < port->count; i++, databuf++) {
			offset = port->base_off + 4 * i;
			*databuf = port->read_port_reg(hisi_hba, phy_cnt,
						       offset);
		}
	}
}

static void hisi_sas_debugfs_snapshot_global_reg(struct hisi_hba *hisi_hba)
{
	int dump_index = hisi_hba->debugfs_dump_index;
	u32 *databuf = hisi_hba->debugfs_regs[dump_index][DEBUGFS_GLOBAL].data;
	const struct hisi_sas_hw *hw = hisi_hba->hw;
	const struct hisi_sas_debugfs_reg *global =
			hw->debugfs_reg_array[DEBUGFS_GLOBAL];
	int i;

	for (i = 0; i < global->count; i++, databuf++)
		*databuf = global->read_global_reg(hisi_hba, 4 * i);
}

static void hisi_sas_debugfs_snapshot_axi_reg(struct hisi_hba *hisi_hba)
{
	int dump_index = hisi_hba->debugfs_dump_index;
	u32 *databuf = hisi_hba->debugfs_regs[dump_index][DEBUGFS_AXI].data;
	const struct hisi_sas_hw *hw = hisi_hba->hw;
	const struct hisi_sas_debugfs_reg *axi =
			hw->debugfs_reg_array[DEBUGFS_AXI];
	int i;

	for (i = 0; i < axi->count; i++, databuf++)
		*databuf = axi->read_global_reg(hisi_hba,
						4 * i + axi->base_off);
}

static void hisi_sas_debugfs_snapshot_ras_reg(struct hisi_hba *hisi_hba)
{
	int dump_index = hisi_hba->debugfs_dump_index;
	u32 *databuf = hisi_hba->debugfs_regs[dump_index][DEBUGFS_RAS].data;
	const struct hisi_sas_hw *hw = hisi_hba->hw;
	const struct hisi_sas_debugfs_reg *ras =
			hw->debugfs_reg_array[DEBUGFS_RAS];
	int i;

	for (i = 0; i < ras->count; i++, databuf++)
		*databuf = ras->read_global_reg(hisi_hba,
						4 * i + ras->base_off);
}

static void hisi_sas_debugfs_snapshot_itct_reg(struct hisi_hba *hisi_hba)
{
	int dump_index = hisi_hba->debugfs_dump_index;
	void *cachebuf = hisi_hba->debugfs_itct_cache[dump_index].cache;
	void *databuf = hisi_hba->debugfs_itct[dump_index].itct;
	struct hisi_sas_itct *itct;
	int i;

	hisi_hba->hw->read_iost_itct_cache(hisi_hba, HISI_SAS_ITCT_CACHE,
					   cachebuf);

	itct = hisi_hba->itct;

	for (i = 0; i < HISI_SAS_MAX_ITCT_ENTRIES; i++, itct++) {
		memcpy(databuf, itct, sizeof(struct hisi_sas_itct));
		databuf += sizeof(struct hisi_sas_itct);
	}
}

static void hisi_sas_debugfs_snapshot_iost_reg(struct hisi_hba *hisi_hba)
{
	int dump_index = hisi_hba->debugfs_dump_index;
	int max_command_entries = HISI_SAS_MAX_COMMANDS;
	void *cachebuf = hisi_hba->debugfs_iost_cache[dump_index].cache;
	void *databuf = hisi_hba->debugfs_iost[dump_index].iost;
	struct hisi_sas_iost *iost;
	int i;

	hisi_hba->hw->read_iost_itct_cache(hisi_hba, HISI_SAS_IOST_CACHE,
					   cachebuf);

	iost = hisi_hba->iost;

	for (i = 0; i < max_command_entries; i++, iost++) {
		memcpy(databuf, iost, sizeof(struct hisi_sas_iost));
		databuf += sizeof(struct hisi_sas_iost);
	}
}

static const char *
hisi_sas_debugfs_to_reg_name(int off, int base_off,
			     const struct hisi_sas_debugfs_reg_lu *lu)
{
	for (; lu->name; lu++) {
		if (off == lu->off - base_off)
			return lu->name;
	}

	return NULL;
}

static void hisi_sas_debugfs_print_reg(u32 *regs_val, const void *ptr,
				       struct seq_file *s)
{
	const struct hisi_sas_debugfs_reg *reg = ptr;
	int i;

	for (i = 0; i < reg->count; i++) {
		int off = i * 4;
		const char *name;

		name = hisi_sas_debugfs_to_reg_name(off, reg->base_off,
						    reg->lu);

		if (name)
			seq_printf(s, "0x%08x 0x%08x %s\n", off,
				   regs_val[i], name);
		else
			seq_printf(s, "0x%08x 0x%08x\n", off,
				   regs_val[i]);
	}
}

static int hisi_sas_debugfs_global_show(struct seq_file *s, void *p)
{
	struct hisi_sas_debugfs_regs *global = s->private;
	struct hisi_hba *hisi_hba = global->hisi_hba;
	const struct hisi_sas_hw *hw = hisi_hba->hw;
	const void *reg_global = hw->debugfs_reg_array[DEBUGFS_GLOBAL];

	hisi_sas_debugfs_print_reg(global->data,
				   reg_global, s);

	return 0;
}

static int hisi_sas_debugfs_global_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, hisi_sas_debugfs_global_show,
			   inode->i_private);
}

static const struct file_operations hisi_sas_debugfs_global_fops = {
	.open = hisi_sas_debugfs_global_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int hisi_sas_debugfs_axi_show(struct seq_file *s, void *p)
{
	struct hisi_sas_debugfs_regs *axi = s->private;
	struct hisi_hba *hisi_hba = axi->hisi_hba;
	const struct hisi_sas_hw *hw = hisi_hba->hw;
	const void *reg_axi = hw->debugfs_reg_array[DEBUGFS_AXI];

	hisi_sas_debugfs_print_reg(axi->data,
				   reg_axi, s);

	return 0;
}

static int hisi_sas_debugfs_axi_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, hisi_sas_debugfs_axi_show,
			   inode->i_private);
}

static const struct file_operations hisi_sas_debugfs_axi_fops = {
	.open = hisi_sas_debugfs_axi_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int hisi_sas_debugfs_ras_show(struct seq_file *s, void *p)
{
	struct hisi_sas_debugfs_regs *ras = s->private;
	struct hisi_hba *hisi_hba = ras->hisi_hba;
	const struct hisi_sas_hw *hw = hisi_hba->hw;
	const void *reg_ras = hw->debugfs_reg_array[DEBUGFS_RAS];

	hisi_sas_debugfs_print_reg(ras->data,
				   reg_ras, s);

	return 0;
}

static int hisi_sas_debugfs_ras_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, hisi_sas_debugfs_ras_show,
			   inode->i_private);
}

static const struct file_operations hisi_sas_debugfs_ras_fops = {
	.open = hisi_sas_debugfs_ras_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int hisi_sas_debugfs_port_show(struct seq_file *s, void *p)
{
	struct hisi_sas_debugfs_port *port = s->private;
	struct hisi_sas_phy *phy = port->phy;
	struct hisi_hba *hisi_hba = phy->hisi_hba;
	const struct hisi_sas_hw *hw = hisi_hba->hw;
	const struct hisi_sas_debugfs_reg *reg_port = hw->debugfs_reg_port;

	hisi_sas_debugfs_print_reg(port->data, reg_port, s);

	return 0;
}

static int hisi_sas_debugfs_port_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, hisi_sas_debugfs_port_show, inode->i_private);
}

static const struct file_operations hisi_sas_debugfs_port_fops = {
	.open = hisi_sas_debugfs_port_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static void hisi_sas_show_row_64(struct seq_file *s, int index,
				 int sz, __le64 *ptr)
{
	int i;

	/* completion header size not fixed per HW version */
	seq_printf(s, "index %04d:\n\t", index);
	for (i = 1; i <= sz / 8; i++, ptr++) {
		seq_printf(s, " 0x%016llx", le64_to_cpu(*ptr));
		if (!(i % 2))
			seq_puts(s, "\n\t");
	}

	seq_puts(s, "\n");
}

static void hisi_sas_show_row_32(struct seq_file *s, int index,
				 int sz, __le32 *ptr)
{
	int i;

	/* completion header size not fixed per HW version */
	seq_printf(s, "index %04d:\n\t", index);
	for (i = 1; i <= sz / 4; i++, ptr++) {
		seq_printf(s, " 0x%08x", le32_to_cpu(*ptr));
		if (!(i % 4))
			seq_puts(s, "\n\t");
	}
	seq_puts(s, "\n");
}

static void hisi_sas_cq_show_slot(struct seq_file *s, int slot,
				  struct hisi_sas_debugfs_cq *debugfs_cq)
{
	struct hisi_sas_cq *cq = debugfs_cq->cq;
	struct hisi_hba *hisi_hba = cq->hisi_hba;
	__le32 *complete_hdr = debugfs_cq->complete_hdr +
			       (hisi_hba->hw->complete_hdr_size * slot);

	hisi_sas_show_row_32(s, slot,
			     hisi_hba->hw->complete_hdr_size,
			     complete_hdr);
}

static int hisi_sas_debugfs_cq_show(struct seq_file *s, void *p)
{
	struct hisi_sas_debugfs_cq *debugfs_cq = s->private;
	int slot;

	for (slot = 0; slot < HISI_SAS_QUEUE_SLOTS; slot++) {
		hisi_sas_cq_show_slot(s, slot, debugfs_cq);
	}
	return 0;
}

static int hisi_sas_debugfs_cq_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, hisi_sas_debugfs_cq_show, inode->i_private);
}

static const struct file_operations hisi_sas_debugfs_cq_fops = {
	.open = hisi_sas_debugfs_cq_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static void hisi_sas_dq_show_slot(struct seq_file *s, int slot, void *dq_ptr)
{
	struct hisi_sas_debugfs_dq *debugfs_dq = dq_ptr;
	void *cmd_queue = debugfs_dq->hdr;
	__le32 *cmd_hdr = cmd_queue +
		sizeof(struct hisi_sas_cmd_hdr) * slot;

	hisi_sas_show_row_32(s, slot, sizeof(struct hisi_sas_cmd_hdr), cmd_hdr);
}

static int hisi_sas_debugfs_dq_show(struct seq_file *s, void *p)
{
	int slot;

	for (slot = 0; slot < HISI_SAS_QUEUE_SLOTS; slot++) {
		hisi_sas_dq_show_slot(s, slot, s->private);
	}
	return 0;
}

static int hisi_sas_debugfs_dq_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, hisi_sas_debugfs_dq_show, inode->i_private);
}

static const struct file_operations hisi_sas_debugfs_dq_fops = {
	.open = hisi_sas_debugfs_dq_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int hisi_sas_debugfs_iost_show(struct seq_file *s, void *p)
{
	struct hisi_sas_debugfs_iost *debugfs_iost = s->private;
	struct hisi_sas_iost *iost = debugfs_iost->iost;
	int i, max_command_entries = HISI_SAS_MAX_COMMANDS;

	for (i = 0; i < max_command_entries; i++, iost++) {
		__le64 *data = &iost->qw0;

		hisi_sas_show_row_64(s, i, sizeof(*iost), data);
	}

	return 0;
}

static int hisi_sas_debugfs_iost_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, hisi_sas_debugfs_iost_show, inode->i_private);
}

static const struct file_operations hisi_sas_debugfs_iost_fops = {
	.open = hisi_sas_debugfs_iost_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int hisi_sas_debugfs_iost_cache_show(struct seq_file *s, void *p)
{
	struct hisi_sas_debugfs_iost_cache *debugfs_iost_cache = s->private;
	struct hisi_sas_iost_itct_cache *iost_cache = debugfs_iost_cache->cache;
	u32 cache_size = HISI_SAS_IOST_ITCT_CACHE_DW_SZ * 4;
	int i, tab_idx;
	__le64 *iost;

	for (i = 0; i < HISI_SAS_IOST_ITCT_CACHE_NUM; i++, iost_cache++) {
		/*
		 * Data struct of IOST cache:
		 * Data[1]: BIT0~15: Table index
		 *	    Bit16:   Valid mask
		 * Data[2]~[9]: IOST table
		 */
		tab_idx = (iost_cache->data[1] & 0xffff);
		iost = (__le64 *)iost_cache;

		hisi_sas_show_row_64(s, tab_idx, cache_size, iost);
	}

	return 0;
}

static int hisi_sas_debugfs_iost_cache_open(struct inode *inode,
					    struct file *filp)
{
	return single_open(filp, hisi_sas_debugfs_iost_cache_show,
			   inode->i_private);
}

static const struct file_operations hisi_sas_debugfs_iost_cache_fops = {
	.open = hisi_sas_debugfs_iost_cache_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int hisi_sas_debugfs_itct_show(struct seq_file *s, void *p)
{
	int i;
	struct hisi_sas_debugfs_itct *debugfs_itct = s->private;
	struct hisi_sas_itct *itct = debugfs_itct->itct;

	for (i = 0; i < HISI_SAS_MAX_ITCT_ENTRIES; i++, itct++) {
		__le64 *data = &itct->qw0;

		hisi_sas_show_row_64(s, i, sizeof(*itct), data);
	}

	return 0;
}

static int hisi_sas_debugfs_itct_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, hisi_sas_debugfs_itct_show, inode->i_private);
}

static const struct file_operations hisi_sas_debugfs_itct_fops = {
	.open = hisi_sas_debugfs_itct_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int hisi_sas_debugfs_itct_cache_show(struct seq_file *s, void *p)
{
	struct hisi_sas_debugfs_itct_cache *debugfs_itct_cache = s->private;
	struct hisi_sas_iost_itct_cache *itct_cache = debugfs_itct_cache->cache;
	u32 cache_size = HISI_SAS_IOST_ITCT_CACHE_DW_SZ * 4;
	int i, tab_idx;
	__le64 *itct;

	for (i = 0; i < HISI_SAS_IOST_ITCT_CACHE_NUM; i++, itct_cache++) {
		/*
		 * Data struct of ITCT cache:
		 * Data[1]: BIT0~15: Table index
		 *	    Bit16:   Valid mask
		 * Data[2]~[9]: ITCT table
		 */
		tab_idx = itct_cache->data[1] & 0xffff;
		itct = (__le64 *)itct_cache;

		hisi_sas_show_row_64(s, tab_idx, cache_size, itct);
	}

	return 0;
}

static int hisi_sas_debugfs_itct_cache_open(struct inode *inode,
					    struct file *filp)
{
	return single_open(filp, hisi_sas_debugfs_itct_cache_show,
			   inode->i_private);
}

static const struct file_operations hisi_sas_debugfs_itct_cache_fops = {
	.open = hisi_sas_debugfs_itct_cache_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static void hisi_sas_debugfs_create_files(struct hisi_hba *hisi_hba)
{
	u64 *debugfs_timestamp;
	int dump_index = hisi_hba->debugfs_dump_index;
	struct dentry *dump_dentry;
	struct dentry *dentry;
	char name[256];
	int p;
	int c;
	int d;

	snprintf(name, 256, "%d", dump_index);

	dump_dentry = debugfs_create_dir(name, hisi_hba->debugfs_dump_dentry);

	debugfs_timestamp = &hisi_hba->debugfs_timestamp[dump_index];

	debugfs_create_u64("timestamp", 0400, dump_dentry,
			   debugfs_timestamp);

	debugfs_create_file("global", 0400, dump_dentry,
			   &hisi_hba->debugfs_regs[dump_index][DEBUGFS_GLOBAL],
			   &hisi_sas_debugfs_global_fops);

	/* Create port dir and files */
	dentry = debugfs_create_dir("port", dump_dentry);
	for (p = 0; p < hisi_hba->n_phy; p++) {
		snprintf(name, 256, "%d", p);

		debugfs_create_file(name, 0400, dentry,
				    &hisi_hba->debugfs_port_reg[dump_index][p],
				    &hisi_sas_debugfs_port_fops);
	}

	/* Create CQ dir and files */
	dentry = debugfs_create_dir("cq", dump_dentry);
	for (c = 0; c < hisi_hba->queue_count; c++) {
		snprintf(name, 256, "%d", c);

		debugfs_create_file(name, 0400, dentry,
				    &hisi_hba->debugfs_cq[dump_index][c],
				    &hisi_sas_debugfs_cq_fops);
	}

	/* Create DQ dir and files */
	dentry = debugfs_create_dir("dq", dump_dentry);
	for (d = 0; d < hisi_hba->queue_count; d++) {
		snprintf(name, 256, "%d", d);

		debugfs_create_file(name, 0400, dentry,
				    &hisi_hba->debugfs_dq[dump_index][d],
				    &hisi_sas_debugfs_dq_fops);
	}

	debugfs_create_file("iost", 0400, dump_dentry,
			    &hisi_hba->debugfs_iost[dump_index],
			    &hisi_sas_debugfs_iost_fops);

	debugfs_create_file("iost_cache", 0400, dump_dentry,
			    &hisi_hba->debugfs_iost_cache[dump_index],
			    &hisi_sas_debugfs_iost_cache_fops);

	debugfs_create_file("itct", 0400, dump_dentry,
			    &hisi_hba->debugfs_itct[dump_index],
			    &hisi_sas_debugfs_itct_fops);

	debugfs_create_file("itct_cache", 0400, dump_dentry,
			    &hisi_hba->debugfs_itct_cache[dump_index],
			    &hisi_sas_debugfs_itct_cache_fops);

	debugfs_create_file("axi", 0400, dump_dentry,
			    &hisi_hba->debugfs_regs[dump_index][DEBUGFS_AXI],
			    &hisi_sas_debugfs_axi_fops);

	debugfs_create_file("ras", 0400, dump_dentry,
			    &hisi_hba->debugfs_regs[dump_index][DEBUGFS_RAS],
			    &hisi_sas_debugfs_ras_fops);

	return;
}

static void hisi_sas_debugfs_snapshot_regs(struct hisi_hba *hisi_hba)
{
	hisi_hba->hw->snapshot_prepare(hisi_hba);

	hisi_sas_debugfs_snapshot_global_reg(hisi_hba);
	hisi_sas_debugfs_snapshot_port_reg(hisi_hba);
	hisi_sas_debugfs_snapshot_axi_reg(hisi_hba);
	hisi_sas_debugfs_snapshot_ras_reg(hisi_hba);
	hisi_sas_debugfs_snapshot_cq_reg(hisi_hba);
	hisi_sas_debugfs_snapshot_dq_reg(hisi_hba);
	hisi_sas_debugfs_snapshot_itct_reg(hisi_hba);
	hisi_sas_debugfs_snapshot_iost_reg(hisi_hba);

	hisi_sas_debugfs_create_files(hisi_hba);

	hisi_hba->hw->snapshot_restore(hisi_hba);
}

static ssize_t hisi_sas_debugfs_trigger_dump_write(struct file *file,
						   const char __user *user_buf,
						   size_t count, loff_t *ppos)
{
	struct hisi_hba *hisi_hba = file->f_inode->i_private;
	char buf[8];

	if (hisi_hba->debugfs_dump_index >= hisi_sas_debugfs_dump_count)
		return -EFAULT;

	if (count > 8)
		return -EFAULT;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (buf[0] != '1')
		return -EFAULT;

	queue_work(hisi_hba->wq, &hisi_hba->debugfs_work);

	return count;
}

static const struct file_operations hisi_sas_debugfs_trigger_dump_fops = {
	.write = &hisi_sas_debugfs_trigger_dump_write,
	.owner = THIS_MODULE,
};

enum {
	HISI_SAS_BIST_LOOPBACK_MODE_DIGITAL = 0,
	HISI_SAS_BIST_LOOPBACK_MODE_SERDES,
	HISI_SAS_BIST_LOOPBACK_MODE_REMOTE,
};

enum {
	HISI_SAS_BIST_CODE_MODE_PRBS7 = 0,
	HISI_SAS_BIST_CODE_MODE_PRBS23,
	HISI_SAS_BIST_CODE_MODE_PRBS31,
	HISI_SAS_BIST_CODE_MODE_JTPAT,
	HISI_SAS_BIST_CODE_MODE_CJTPAT,
	HISI_SAS_BIST_CODE_MODE_SCRAMBED_0,
	HISI_SAS_BIST_CODE_MODE_TRAIN,
	HISI_SAS_BIST_CODE_MODE_TRAIN_DONE,
	HISI_SAS_BIST_CODE_MODE_HFTP,
	HISI_SAS_BIST_CODE_MODE_MFTP,
	HISI_SAS_BIST_CODE_MODE_LFTP,
	HISI_SAS_BIST_CODE_MODE_FIXED_DATA,
};

static const struct {
	int		value;
	char		*name;
} hisi_sas_debugfs_loop_linkrate[] = {
	{ SAS_LINK_RATE_1_5_GBPS, "1.5 Gbit" },
	{ SAS_LINK_RATE_3_0_GBPS, "3.0 Gbit" },
	{ SAS_LINK_RATE_6_0_GBPS, "6.0 Gbit" },
	{ SAS_LINK_RATE_12_0_GBPS, "12.0 Gbit" },
};

static int hisi_sas_debugfs_bist_linkrate_show(struct seq_file *s, void *p)
{
	struct hisi_hba *hisi_hba = s->private;
	int i;

	for (i = 0; i < ARRAY_SIZE(hisi_sas_debugfs_loop_linkrate); i++) {
		int match = (hisi_hba->debugfs_bist_linkrate ==
			     hisi_sas_debugfs_loop_linkrate[i].value);

		seq_printf(s, "%s%s%s ", match ? "[" : "",
			   hisi_sas_debugfs_loop_linkrate[i].name,
			   match ? "]" : "");
	}
	seq_puts(s, "\n");

	return 0;
}

static ssize_t hisi_sas_debugfs_bist_linkrate_write(struct file *filp,
						    const char __user *buf,
						    size_t count, loff_t *ppos)
{
	struct seq_file *m = filp->private_data;
	struct hisi_hba *hisi_hba = m->private;
	char kbuf[16] = {}, *pkbuf;
	bool found = false;
	int i;

	if (hisi_hba->debugfs_bist_enable)
		return -EPERM;

	if (count >= sizeof(kbuf))
		return -EOVERFLOW;

	if (copy_from_user(kbuf, buf, count))
		return -EINVAL;

	pkbuf = strstrip(kbuf);

	for (i = 0; i < ARRAY_SIZE(hisi_sas_debugfs_loop_linkrate); i++) {
		if (!strncmp(hisi_sas_debugfs_loop_linkrate[i].name,
			     pkbuf, 16)) {
			hisi_hba->debugfs_bist_linkrate =
				hisi_sas_debugfs_loop_linkrate[i].value;
			found = true;
			break;
		}
	}

	if (!found)
		return -EINVAL;

	return count;
}

static int hisi_sas_debugfs_bist_linkrate_open(struct inode *inode,
					       struct file *filp)
{
	return single_open(filp, hisi_sas_debugfs_bist_linkrate_show,
			   inode->i_private);
}

static const struct file_operations hisi_sas_debugfs_bist_linkrate_ops = {
	.open = hisi_sas_debugfs_bist_linkrate_open,
	.read = seq_read,
	.write = hisi_sas_debugfs_bist_linkrate_write,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct {
	int		value;
	char		*name;
} hisi_sas_debugfs_loop_code_mode[] = {
	{ HISI_SAS_BIST_CODE_MODE_PRBS7, "PRBS7" },
	{ HISI_SAS_BIST_CODE_MODE_PRBS23, "PRBS23" },
	{ HISI_SAS_BIST_CODE_MODE_PRBS31, "PRBS31" },
	{ HISI_SAS_BIST_CODE_MODE_JTPAT, "JTPAT" },
	{ HISI_SAS_BIST_CODE_MODE_CJTPAT, "CJTPAT" },
	{ HISI_SAS_BIST_CODE_MODE_SCRAMBED_0, "SCRAMBED_0" },
	{ HISI_SAS_BIST_CODE_MODE_TRAIN, "TRAIN" },
	{ HISI_SAS_BIST_CODE_MODE_TRAIN_DONE, "TRAIN_DONE" },
	{ HISI_SAS_BIST_CODE_MODE_HFTP, "HFTP" },
	{ HISI_SAS_BIST_CODE_MODE_MFTP, "MFTP" },
	{ HISI_SAS_BIST_CODE_MODE_LFTP, "LFTP" },
	{ HISI_SAS_BIST_CODE_MODE_FIXED_DATA, "FIXED_DATA" },
};

static int hisi_sas_debugfs_bist_code_mode_show(struct seq_file *s, void *p)
{
	struct hisi_hba *hisi_hba = s->private;
	int i;

	for (i = 0; i < ARRAY_SIZE(hisi_sas_debugfs_loop_code_mode); i++) {
		int match = (hisi_hba->debugfs_bist_code_mode ==
			     hisi_sas_debugfs_loop_code_mode[i].value);

		seq_printf(s, "%s%s%s ", match ? "[" : "",
			   hisi_sas_debugfs_loop_code_mode[i].name,
			   match ? "]" : "");
	}
	seq_puts(s, "\n");

	return 0;
}

static ssize_t hisi_sas_debugfs_bist_code_mode_write(struct file *filp,
						     const char __user *buf,
						     size_t count,
						     loff_t *ppos)
{
	struct seq_file *m = filp->private_data;
	struct hisi_hba *hisi_hba = m->private;
	char kbuf[16] = {}, *pkbuf;
	bool found = false;
	int i;

	if (hisi_hba->debugfs_bist_enable)
		return -EPERM;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, buf, count))
		return -EOVERFLOW;

	pkbuf = strstrip(kbuf);

	for (i = 0; i < ARRAY_SIZE(hisi_sas_debugfs_loop_code_mode); i++) {
		if (!strncmp(hisi_sas_debugfs_loop_code_mode[i].name,
			     pkbuf, 16)) {
			hisi_hba->debugfs_bist_code_mode =
				hisi_sas_debugfs_loop_code_mode[i].value;
			found = true;
			break;
		}
	}

	if (!found)
		return -EINVAL;

	return count;
}

static int hisi_sas_debugfs_bist_code_mode_open(struct inode *inode,
						struct file *filp)
{
	return single_open(filp, hisi_sas_debugfs_bist_code_mode_show,
			   inode->i_private);
}

static const struct file_operations hisi_sas_debugfs_bist_code_mode_ops = {
	.open = hisi_sas_debugfs_bist_code_mode_open,
	.read = seq_read,
	.write = hisi_sas_debugfs_bist_code_mode_write,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static ssize_t hisi_sas_debugfs_bist_phy_write(struct file *filp,
					       const char __user *buf,
					       size_t count, loff_t *ppos)
{
	struct seq_file *m = filp->private_data;
	struct hisi_hba *hisi_hba = m->private;
	unsigned int phy_no;
	int val;

	if (hisi_hba->debugfs_bist_enable)
		return -EPERM;

	val = kstrtouint_from_user(buf, count, 0, &phy_no);
	if (val)
		return val;

	if (phy_no >= hisi_hba->n_phy)
		return -EINVAL;

	hisi_hba->debugfs_bist_phy_no = phy_no;

	return count;
}

static int hisi_sas_debugfs_bist_phy_show(struct seq_file *s, void *p)
{
	struct hisi_hba *hisi_hba = s->private;

	seq_printf(s, "%d\n", hisi_hba->debugfs_bist_phy_no);

	return 0;
}

static int hisi_sas_debugfs_bist_phy_open(struct inode *inode,
					  struct file *filp)
{
	return single_open(filp, hisi_sas_debugfs_bist_phy_show,
			   inode->i_private);
}

static const struct file_operations hisi_sas_debugfs_bist_phy_ops = {
	.open = hisi_sas_debugfs_bist_phy_open,
	.read = seq_read,
	.write = hisi_sas_debugfs_bist_phy_write,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static const struct {
	int		value;
	char		*name;
} hisi_sas_debugfs_loop_modes[] = {
	{ HISI_SAS_BIST_LOOPBACK_MODE_DIGITAL, "digital" },
	{ HISI_SAS_BIST_LOOPBACK_MODE_SERDES, "serdes" },
	{ HISI_SAS_BIST_LOOPBACK_MODE_REMOTE, "remote" },
};

static int hisi_sas_debugfs_bist_mode_show(struct seq_file *s, void *p)
{
	struct hisi_hba *hisi_hba = s->private;
	int i;

	for (i = 0; i < ARRAY_SIZE(hisi_sas_debugfs_loop_modes); i++) {
		int match = (hisi_hba->debugfs_bist_mode ==
			     hisi_sas_debugfs_loop_modes[i].value);

		seq_printf(s, "%s%s%s ", match ? "[" : "",
			   hisi_sas_debugfs_loop_modes[i].name,
			   match ? "]" : "");
	}
	seq_puts(s, "\n");

	return 0;
}

static ssize_t hisi_sas_debugfs_bist_mode_write(struct file *filp,
						const char __user *buf,
						size_t count, loff_t *ppos)
{
	struct seq_file *m = filp->private_data;
	struct hisi_hba *hisi_hba = m->private;
	char kbuf[16] = {}, *pkbuf;
	bool found = false;
	int i;

	if (hisi_hba->debugfs_bist_enable)
		return -EPERM;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, buf, count))
		return -EOVERFLOW;

	pkbuf = strstrip(kbuf);

	for (i = 0; i < ARRAY_SIZE(hisi_sas_debugfs_loop_modes); i++) {
		if (!strncmp(hisi_sas_debugfs_loop_modes[i].name, pkbuf, 16)) {
			hisi_hba->debugfs_bist_mode =
				hisi_sas_debugfs_loop_modes[i].value;
			found = true;
			break;
		}
	}

	if (!found)
		return -EINVAL;

	return count;
}

static int hisi_sas_debugfs_bist_mode_open(struct inode *inode,
					   struct file *filp)
{
	return single_open(filp, hisi_sas_debugfs_bist_mode_show,
			   inode->i_private);
}

static const struct file_operations hisi_sas_debugfs_bist_mode_ops = {
	.open = hisi_sas_debugfs_bist_mode_open,
	.read = seq_read,
	.write = hisi_sas_debugfs_bist_mode_write,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static ssize_t hisi_sas_debugfs_bist_enable_write(struct file *filp,
						  const char __user *buf,
						  size_t count, loff_t *ppos)
{
	struct seq_file *m = filp->private_data;
	struct hisi_hba *hisi_hba = m->private;
	unsigned int enable;
	int val;

	val = kstrtouint_from_user(buf, count, 0, &enable);
	if (val)
		return val;

	if (enable > 1)
		return -EINVAL;

	if (enable == hisi_hba->debugfs_bist_enable)
		return count;

	if (!hisi_hba->hw->set_bist)
		return -EPERM;

	val = hisi_hba->hw->set_bist(hisi_hba, enable);
	if (val < 0)
		return val;

	hisi_hba->debugfs_bist_enable = enable;

	return count;
}

static int hisi_sas_debugfs_bist_enable_show(struct seq_file *s, void *p)
{
	struct hisi_hba *hisi_hba = s->private;

	seq_printf(s, "%d\n", hisi_hba->debugfs_bist_enable);

	return 0;
}

static int hisi_sas_debugfs_bist_enable_open(struct inode *inode,
					     struct file *filp)
{
	return single_open(filp, hisi_sas_debugfs_bist_enable_show,
			   inode->i_private);
}

static const struct file_operations hisi_sas_debugfs_bist_enable_ops = {
	.open = hisi_sas_debugfs_bist_enable_open,
	.read = seq_read,
	.write = hisi_sas_debugfs_bist_enable_write,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static ssize_t hisi_sas_debugfs_phy_down_cnt_write(struct file *filp,
						   const char __user *buf,
						   size_t count, loff_t *ppos)
{
	struct seq_file *s = filp->private_data;
	struct hisi_sas_phy *phy = s->private;
	unsigned int set_val;
	int res;

	res = kstrtouint_from_user(buf, count, 0, &set_val);
	if (res)
		return res;

	if (set_val > 0)
		return -EINVAL;

	atomic_set(&phy->down_cnt, 0);

	return count;
}

static int hisi_sas_debugfs_phy_down_cnt_show(struct seq_file *s, void *p)
{
	struct hisi_sas_phy *phy = s->private;

	seq_printf(s, "%d\n", atomic_read(&phy->down_cnt));

	return 0;
}

static int hisi_sas_debugfs_phy_down_cnt_open(struct inode *inode,
					      struct file *filp)
{
	return single_open(filp, hisi_sas_debugfs_phy_down_cnt_show,
			   inode->i_private);
}

static const struct file_operations hisi_sas_debugfs_phy_down_cnt_ops = {
	.open = hisi_sas_debugfs_phy_down_cnt_open,
	.read = seq_read,
	.write = hisi_sas_debugfs_phy_down_cnt_write,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

void hisi_sas_debugfs_work_handler(struct work_struct *work)
{
	struct hisi_hba *hisi_hba =
		container_of(work, struct hisi_hba, debugfs_work);
	int debugfs_dump_index = hisi_hba->debugfs_dump_index;
	struct device *dev = hisi_hba->dev;
	u64 timestamp = local_clock();

	if (debugfs_dump_index >= hisi_sas_debugfs_dump_count) {
		dev_warn(dev, "dump count exceeded!\n");
		return;
	}

	do_div(timestamp, NSEC_PER_MSEC);
	hisi_hba->debugfs_timestamp[debugfs_dump_index] = timestamp;

	hisi_sas_debugfs_snapshot_regs(hisi_hba);
	hisi_hba->debugfs_dump_index++;
}
EXPORT_SYMBOL_GPL(hisi_sas_debugfs_work_handler);

static void hisi_sas_debugfs_release(struct hisi_hba *hisi_hba, int dump_index)
{
	struct device *dev = hisi_hba->dev;
	int i;

	devm_kfree(dev, hisi_hba->debugfs_iost_cache[dump_index].cache);
	devm_kfree(dev, hisi_hba->debugfs_itct_cache[dump_index].cache);
	devm_kfree(dev, hisi_hba->debugfs_iost[dump_index].iost);
	devm_kfree(dev, hisi_hba->debugfs_itct[dump_index].itct);

	for (i = 0; i < hisi_hba->queue_count; i++)
		devm_kfree(dev, hisi_hba->debugfs_dq[dump_index][i].hdr);

	for (i = 0; i < hisi_hba->queue_count; i++)
		devm_kfree(dev,
			   hisi_hba->debugfs_cq[dump_index][i].complete_hdr);

	for (i = 0; i < DEBUGFS_REGS_NUM; i++)
		devm_kfree(dev, hisi_hba->debugfs_regs[dump_index][i].data);

	for (i = 0; i < hisi_hba->n_phy; i++)
		devm_kfree(dev, hisi_hba->debugfs_port_reg[dump_index][i].data);
}

static int hisi_sas_debugfs_alloc(struct hisi_hba *hisi_hba, int dump_index)
{
	const struct hisi_sas_hw *hw = hisi_hba->hw;
	struct device *dev = hisi_hba->dev;
	int p, c, d, r, i;
	size_t sz;

	for (r = 0; r < DEBUGFS_REGS_NUM; r++) {
		struct hisi_sas_debugfs_regs *regs =
				&hisi_hba->debugfs_regs[dump_index][r];

		sz = hw->debugfs_reg_array[r]->count * 4;
		regs->data = devm_kmalloc(dev, sz, GFP_KERNEL);
		if (!regs->data)
			goto fail;
		regs->hisi_hba = hisi_hba;
	}

	sz = hw->debugfs_reg_port->count * 4;
	for (p = 0; p < hisi_hba->n_phy; p++) {
		struct hisi_sas_debugfs_port *port =
				&hisi_hba->debugfs_port_reg[dump_index][p];

		port->data = devm_kmalloc(dev, sz, GFP_KERNEL);
		if (!port->data)
			goto fail;
		port->phy = &hisi_hba->phy[p];
	}

	sz = hw->complete_hdr_size * HISI_SAS_QUEUE_SLOTS;
	for (c = 0; c < hisi_hba->queue_count; c++) {
		struct hisi_sas_debugfs_cq *cq =
				&hisi_hba->debugfs_cq[dump_index][c];

		cq->complete_hdr = devm_kmalloc(dev, sz, GFP_KERNEL);
		if (!cq->complete_hdr)
			goto fail;
		cq->cq = &hisi_hba->cq[c];
	}

	sz = sizeof(struct hisi_sas_cmd_hdr) * HISI_SAS_QUEUE_SLOTS;
	for (d = 0; d < hisi_hba->queue_count; d++) {
		struct hisi_sas_debugfs_dq *dq =
				&hisi_hba->debugfs_dq[dump_index][d];

		dq->hdr = devm_kmalloc(dev, sz, GFP_KERNEL);
		if (!dq->hdr)
			goto fail;
		dq->dq = &hisi_hba->dq[d];
	}

	sz = HISI_SAS_MAX_COMMANDS * sizeof(struct hisi_sas_iost);

	hisi_hba->debugfs_iost[dump_index].iost =
				devm_kmalloc(dev, sz, GFP_KERNEL);
	if (!hisi_hba->debugfs_iost[dump_index].iost)
		goto fail;

	sz = HISI_SAS_IOST_ITCT_CACHE_NUM *
	     sizeof(struct hisi_sas_iost_itct_cache);

	hisi_hba->debugfs_iost_cache[dump_index].cache =
				devm_kmalloc(dev, sz, GFP_KERNEL);
	if (!hisi_hba->debugfs_iost_cache[dump_index].cache)
		goto fail;

	sz = HISI_SAS_IOST_ITCT_CACHE_NUM *
	     sizeof(struct hisi_sas_iost_itct_cache);

	hisi_hba->debugfs_itct_cache[dump_index].cache =
				devm_kmalloc(dev, sz, GFP_KERNEL);
	if (!hisi_hba->debugfs_itct_cache[dump_index].cache)
		goto fail;

	/* New memory allocation must be locate before itct */
	sz = HISI_SAS_MAX_ITCT_ENTRIES * sizeof(struct hisi_sas_itct);

	hisi_hba->debugfs_itct[dump_index].itct =
				devm_kmalloc(dev, sz, GFP_KERNEL);
	if (!hisi_hba->debugfs_itct[dump_index].itct)
		goto fail;

	return 0;
fail:
	for (i = 0; i < hisi_sas_debugfs_dump_count; i++)
		hisi_sas_debugfs_release(hisi_hba, i);
	return -ENOMEM;
}

static void hisi_sas_debugfs_phy_down_cnt_init(struct hisi_hba *hisi_hba)
{
	struct dentry *dir = debugfs_create_dir("phy_down_cnt",
						hisi_hba->debugfs_dir);
	char name[16];
	int phy_no;

	for (phy_no = 0; phy_no < hisi_hba->n_phy; phy_no++) {
		snprintf(name, 16, "%d", phy_no);
		debugfs_create_file(name, 0600, dir,
				    &hisi_hba->phy[phy_no],
				    &hisi_sas_debugfs_phy_down_cnt_ops);
	}
}

static void hisi_sas_debugfs_bist_init(struct hisi_hba *hisi_hba)
{
	hisi_hba->debugfs_bist_dentry =
			debugfs_create_dir("bist", hisi_hba->debugfs_dir);
	debugfs_create_file("link_rate", 0600,
			    hisi_hba->debugfs_bist_dentry, hisi_hba,
			    &hisi_sas_debugfs_bist_linkrate_ops);

	debugfs_create_file("code_mode", 0600,
			    hisi_hba->debugfs_bist_dentry, hisi_hba,
			    &hisi_sas_debugfs_bist_code_mode_ops);

	debugfs_create_file("phy_id", 0600, hisi_hba->debugfs_bist_dentry,
			    hisi_hba, &hisi_sas_debugfs_bist_phy_ops);

	debugfs_create_u32("cnt", 0600, hisi_hba->debugfs_bist_dentry,
			   &hisi_hba->debugfs_bist_cnt);

	debugfs_create_file("loopback_mode", 0600,
			    hisi_hba->debugfs_bist_dentry,
			    hisi_hba, &hisi_sas_debugfs_bist_mode_ops);

	debugfs_create_file("enable", 0600, hisi_hba->debugfs_bist_dentry,
			    hisi_hba, &hisi_sas_debugfs_bist_enable_ops);

	hisi_hba->debugfs_bist_linkrate = SAS_LINK_RATE_1_5_GBPS;
}

void hisi_sas_debugfs_init(struct hisi_hba *hisi_hba)
{
	struct device *dev = hisi_hba->dev;
	int i;

	hisi_hba->debugfs_dir = debugfs_create_dir(dev_name(dev),
						   hisi_sas_debugfs_dir);
	debugfs_create_file("trigger_dump", 0200,
			    hisi_hba->debugfs_dir,
			    hisi_hba,
			    &hisi_sas_debugfs_trigger_dump_fops);

	/* create bist structures */
	hisi_sas_debugfs_bist_init(hisi_hba);

	hisi_hba->debugfs_dump_dentry =
			debugfs_create_dir("dump", hisi_hba->debugfs_dir);

	hisi_sas_debugfs_phy_down_cnt_init(hisi_hba);

	for (i = 0; i < hisi_sas_debugfs_dump_count; i++) {
		if (hisi_sas_debugfs_alloc(hisi_hba, i)) {
			debugfs_remove_recursive(hisi_hba->debugfs_dir);
			dev_dbg(dev, "failed to init debugfs!\n");
			break;
		}
	}
}
EXPORT_SYMBOL_GPL(hisi_sas_debugfs_init);

void hisi_sas_debugfs_exit(struct hisi_hba *hisi_hba)
{
	debugfs_remove_recursive(hisi_hba->debugfs_dir);
}
EXPORT_SYMBOL_GPL(hisi_sas_debugfs_exit);

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

bool hisi_sas_debugfs_enable;
EXPORT_SYMBOL_GPL(hisi_sas_debugfs_enable);
module_param_named(debugfs_enable, hisi_sas_debugfs_enable, bool, 0444);
MODULE_PARM_DESC(hisi_sas_debugfs_enable, "Enable driver debugfs (default disabled)");

u32 hisi_sas_debugfs_dump_count = 1;
EXPORT_SYMBOL_GPL(hisi_sas_debugfs_dump_count);
module_param_named(debugfs_dump_count, hisi_sas_debugfs_dump_count, uint, 0444);
MODULE_PARM_DESC(hisi_sas_debugfs_dump_count, "Number of debugfs dumps to allow");

static __init int hisi_sas_init(void)
{
	hisi_sas_stt = sas_domain_attach_transport(&hisi_sas_transport_ops);
	if (!hisi_sas_stt)
		return -ENOMEM;

	if (hisi_sas_debugfs_enable) {
		hisi_sas_debugfs_dir = debugfs_create_dir("hisi_sas", NULL);
		if (hisi_sas_debugfs_dump_count > HISI_SAS_MAX_DEBUGFS_DUMP) {
			pr_info("hisi_sas: Limiting debugfs dump count\n");
			hisi_sas_debugfs_dump_count = HISI_SAS_MAX_DEBUGFS_DUMP;
		}
	}

	return 0;
}

static __exit void hisi_sas_exit(void)
{
	sas_release_transport(hisi_sas_stt);

	debugfs_remove(hisi_sas_debugfs_dir);
}

module_init(hisi_sas_init);
module_exit(hisi_sas_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Garry <john.garry@huawei.com>");
MODULE_DESCRIPTION("HISILICON SAS controller driver");
MODULE_ALIAS("platform:" DRV_NAME);
