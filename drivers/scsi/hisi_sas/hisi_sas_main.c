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

#define DEV_IS_EXPANDER(type) \
	((type == SAS_EDGE_EXPANDER_DEVICE) || \
	(type == SAS_FANOUT_EXPANDER_DEVICE))

#define DEV_IS_GONE(dev) \
	((!dev) || (dev->dev_type == SAS_PHY_UNUSED))

static struct hisi_hba *dev_to_hisi_hba(struct domain_device *device)
{
	return device->port->ha->lldd_ha;
}

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
	struct device *dev = &hisi_hba->pdev->dev;

	if (!slot->task)
		return;

	if (!sas_protocol_ata(task->task_proto))
		if (slot->n_elem)
			dma_unmap_sg(dev, task->scatter, slot->n_elem,
				     task->data_dir);

	if (slot->command_table)
		dma_pool_free(hisi_hba->command_table_pool,
			      slot->command_table, slot->command_table_dma);

	if (slot->status_buffer)
		dma_pool_free(hisi_hba->status_buffer_pool,
			      slot->status_buffer, slot->status_buffer_dma);

	if (slot->sge_page)
		dma_pool_free(hisi_hba->sge_page_pool, slot->sge_page,
			      slot->sge_page_dma);

	list_del_init(&slot->entry);
	task->lldd_task = NULL;
	slot->task = NULL;
	slot->port = NULL;
	hisi_sas_slot_index_free(hisi_hba, slot->idx);
	memset(slot, 0, sizeof(*slot));
}
EXPORT_SYMBOL_GPL(hisi_sas_slot_task_free);

static int hisi_sas_task_prep_ssp(struct hisi_hba *hisi_hba,
				  struct hisi_sas_slot *slot, int is_tmf,
				  struct hisi_sas_tmf_task *tmf)
{
	return hisi_hba->hw->prep_ssp(hisi_hba, slot, is_tmf, tmf);
}

static int hisi_sas_task_prep(struct sas_task *task, struct hisi_hba *hisi_hba,
			      int is_tmf, struct hisi_sas_tmf_task *tmf,
			      int *pass)
{
	struct domain_device *device = task->dev;
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_sas_port *port;
	struct hisi_sas_slot *slot;
	struct hisi_sas_cmd_hdr	*cmd_hdr_base;
	struct device *dev = &hisi_hba->pdev->dev;
	int dlvry_queue_slot, dlvry_queue, n_elem = 0, rc, slot_idx;

	if (!device->port) {
		struct task_status_struct *ts = &task->task_status;

		ts->resp = SAS_TASK_UNDELIVERED;
		ts->stat = SAS_PHY_DOWN;
		/*
		 * libsas will use dev->port, should
		 * not call task_done for sata
		 */
		if (device->dev_type != SAS_SATA_DEV)
			task->task_done(task);
		return 0;
	}

	if (DEV_IS_GONE(sas_dev)) {
		if (sas_dev)
			dev_info(dev, "task prep: device %llu not ready\n",
				 sas_dev->device_id);
		else
			dev_info(dev, "task prep: device %016llx not ready\n",
				 SAS_ADDR(device->sas_addr));

		rc = SAS_PHY_DOWN;
		return rc;
	}
	port = device->port->lldd_port;
	if (port && !port->port_attached && !tmf) {
		if (sas_protocol_ata(task->task_proto)) {
			struct task_status_struct *ts = &task->task_status;

			dev_info(dev,
				 "task prep: SATA/STP port%d not attach device\n",
				 device->port->id);
			ts->resp = SAS_TASK_COMPLETE;
			ts->stat = SAS_PHY_DOWN;
			task->task_done(task);
		} else {
			struct task_status_struct *ts = &task->task_status;

			dev_info(dev,
				 "task prep: SAS port%d does not attach device\n",
				 device->port->id);
			ts->resp = SAS_TASK_UNDELIVERED;
			ts->stat = SAS_PHY_DOWN;
			task->task_done(task);
		}
		return 0;
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

	rc = hisi_sas_slot_index_alloc(hisi_hba, &slot_idx);
	if (rc)
		goto err_out;
	rc = hisi_hba->hw->get_free_slot(hisi_hba, &dlvry_queue,
					 &dlvry_queue_slot);
	if (rc)
		goto err_out_tag;

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
	task->lldd_task = slot;

	slot->status_buffer = dma_pool_alloc(hisi_hba->status_buffer_pool,
					     GFP_ATOMIC,
					     &slot->status_buffer_dma);
	if (!slot->status_buffer)
		goto err_out_slot_buf;
	memset(slot->status_buffer, 0, HISI_SAS_STATUS_BUF_SZ);

	slot->command_table = dma_pool_alloc(hisi_hba->command_table_pool,
					     GFP_ATOMIC,
					     &slot->command_table_dma);
	if (!slot->command_table)
		goto err_out_status_buf;
	memset(slot->command_table, 0, HISI_SAS_COMMAND_TABLE_SZ);
	memset(slot->cmd_hdr, 0, sizeof(struct hisi_sas_cmd_hdr));

	switch (task->task_proto) {
	case SAS_PROTOCOL_SSP:
		rc = hisi_sas_task_prep_ssp(hisi_hba, slot, is_tmf, tmf);
		break;
	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SATA | SAS_PROTOCOL_STP:
	default:
		dev_err(dev, "task prep: unknown/unsupported proto (0x%x)\n",
			task->task_proto);
		rc = -EINVAL;
		break;
	}

	if (rc) {
		dev_err(dev, "task prep: rc = 0x%x\n", rc);
		if (slot->sge_page)
			goto err_out_sge;
		goto err_out_command_table;
	}

	list_add_tail(&slot->entry, &port->list);
	spin_lock(&task->task_state_lock);
	task->task_state_flags |= SAS_TASK_AT_INITIATOR;
	spin_unlock(&task->task_state_lock);

	hisi_hba->slot_prep = slot;

	sas_dev->running_req++;
	++(*pass);

	return rc;

err_out_sge:
	dma_pool_free(hisi_hba->sge_page_pool, slot->sge_page,
		slot->sge_page_dma);
err_out_command_table:
	dma_pool_free(hisi_hba->command_table_pool, slot->command_table,
		slot->command_table_dma);
err_out_status_buf:
	dma_pool_free(hisi_hba->status_buffer_pool, slot->status_buffer,
		slot->status_buffer_dma);
err_out_slot_buf:
	/* Nothing to be done */
err_out_tag:
	hisi_sas_slot_index_free(hisi_hba, slot_idx);
err_out:
	dev_err(dev, "task prep: failed[%d]!\n", rc);
	if (!sas_protocol_ata(task->task_proto))
		if (n_elem)
			dma_unmap_sg(dev, task->scatter, n_elem,
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
	struct device *dev = &hisi_hba->pdev->dev;

	/* protect task_prep and start_delivery sequence */
	spin_lock_irqsave(&hisi_hba->lock, flags);
	rc = hisi_sas_task_prep(task, hisi_hba, is_tmf, tmf, &pass);
	if (rc)
		dev_err(dev, "task exec: failed[%d]!\n", rc);

	if (likely(pass))
		hisi_hba->hw->start_delivery(hisi_hba);
	spin_unlock_irqrestore(&hisi_hba->lock, flags);

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
		sphy->minimum_linkrate = phy->minimum_linkrate;
		sphy->minimum_linkrate_hw = SAS_LINK_RATE_1_5_GBPS;
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
	int i;

	spin_lock(&hisi_hba->lock);
	for (i = 0; i < HISI_SAS_MAX_DEVICES; i++) {
		if (hisi_hba->devices[i].dev_type == SAS_PHY_UNUSED) {
			hisi_hba->devices[i].device_id = i;
			sas_dev = &hisi_hba->devices[i];
			sas_dev->dev_status = HISI_SAS_DEV_NORMAL;
			sas_dev->dev_type = device->dev_type;
			sas_dev->hisi_hba = hisi_hba;
			sas_dev->sas_device = device;
			break;
		}
	}
	spin_unlock(&hisi_hba->lock);

	return sas_dev;
}

static int hisi_sas_dev_found(struct domain_device *device)
{
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(device);
	struct domain_device *parent_dev = device->parent;
	struct hisi_sas_device *sas_dev;
	struct device *dev = &hisi_hba->pdev->dev;

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
				SAS_ADDR(device->sas_addr)) {
				sas_dev->attached_phy = phy_no;
				break;
			}
		}

		if (phy_no == phy_num) {
			dev_info(dev, "dev found: no attached "
				 "dev:%016llx at ex:%016llx\n",
				 SAS_ADDR(device->sas_addr),
				 SAS_ADDR(parent_dev->sas_addr));
			return -EINVAL;
		}
	}

	return 0;
}

static void hisi_sas_phyup_work(struct work_struct *work)
{
	struct hisi_sas_phy *phy =
		container_of(work, struct hisi_sas_phy, phyup_ws);
	struct hisi_hba *hisi_hba = phy->hisi_hba;
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	int phy_no = sas_phy->id;

	hisi_hba->hw->sl_notify(hisi_hba, phy_no); /* This requires a sleep */
	hisi_sas_bytes_dmaed(hisi_hba, phy_no);
}

static void hisi_sas_phy_init(struct hisi_hba *hisi_hba, int phy_no)
{
	struct hisi_sas_phy *phy = &hisi_hba->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;

	phy->hisi_hba = hisi_hba;
	phy->port = NULL;
	init_timer(&phy->timer);
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

	INIT_WORK(&phy->phyup_ws, hisi_sas_phyup_work);
}

static void hisi_sas_port_notify_formed(struct asd_sas_phy *sas_phy)
{
	struct sas_ha_struct *sas_ha = sas_phy->ha;
	struct hisi_hba *hisi_hba = sas_ha->lldd_ha;
	struct hisi_sas_phy *phy = sas_phy->lldd_phy;
	struct asd_sas_port *sas_port = sas_phy->port;
	struct hisi_sas_port *port = &hisi_hba->port[sas_phy->id];
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

static void hisi_sas_do_release_task(struct hisi_hba *hisi_hba, int phy_no,
				     struct domain_device *device)
{
	struct hisi_sas_phy *phy;
	struct hisi_sas_port *port;
	struct hisi_sas_slot *slot, *slot2;
	struct device *dev = &hisi_hba->pdev->dev;

	phy = &hisi_hba->phy[phy_no];
	port = phy->port;
	if (!port)
		return;

	list_for_each_entry_safe(slot, slot2, &port->list, entry) {
		struct sas_task *task;

		task = slot->task;
		if (device && task->dev != device)
			continue;

		dev_info(dev, "Release slot [%d:%d], task [%p]:\n",
			 slot->dlvry_queue, slot->dlvry_queue_slot, task);
		hisi_hba->hw->slot_complete(hisi_hba, slot, 1);
	}
}

static void hisi_sas_port_notify_deformed(struct asd_sas_phy *sas_phy)
{
	struct domain_device *device;
	struct hisi_sas_phy *phy = sas_phy->lldd_phy;
	struct asd_sas_port *sas_port = sas_phy->port;

	list_for_each_entry(device, &sas_port->dev_list, dev_list_node)
		hisi_sas_do_release_task(phy->hisi_hba, sas_phy->id, device);
}

static void hisi_sas_release_task(struct hisi_hba *hisi_hba,
			struct domain_device *device)
{
	struct asd_sas_port *port = device->port;
	struct asd_sas_phy *sas_phy;

	list_for_each_entry(sas_phy, &port->phy_list, port_phy_el)
		hisi_sas_do_release_task(hisi_hba, sas_phy->id, device);
}

static void hisi_sas_dev_gone(struct domain_device *device)
{
	struct hisi_sas_device *sas_dev = device->lldd_dev;
	struct hisi_hba *hisi_hba = dev_to_hisi_hba(device);
	struct device *dev = &hisi_hba->pdev->dev;
	u64 dev_id = sas_dev->device_id;

	dev_info(dev, "found dev[%lld:%x] is gone\n",
		 sas_dev->device_id, sas_dev->dev_type);

	hisi_hba->hw->free_device(hisi_hba, sas_dev);
	device->lldd_dev = NULL;
	memset(sas_dev, 0, sizeof(*sas_dev));
	sas_dev->device_id = dev_id;
	sas_dev->dev_type = SAS_PHY_UNUSED;
	sas_dev->dev_status = HISI_SAS_DEV_NORMAL;
}

static int hisi_sas_queue_command(struct sas_task *task, gfp_t gfp_flags)
{
	return hisi_sas_task_exec(task, gfp_flags, 0, NULL);
}


static void hisi_sas_port_formed(struct asd_sas_phy *sas_phy)
{
	hisi_sas_port_notify_formed(sas_phy);
}

static void hisi_sas_port_deformed(struct asd_sas_phy *sas_phy)
{
	hisi_sas_port_notify_deformed(sas_phy);
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

static struct scsi_transport_template *hisi_sas_stt;

static struct scsi_host_template hisi_sas_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.queuecommand		= sas_queuecommand,
	.target_alloc		= sas_target_alloc,
	.slave_configure	= sas_slave_configure,
	.change_queue_depth	= sas_change_queue_depth,
	.bios_param		= sas_bios_param,
	.can_queue		= 1,
	.this_id		= -1,
	.sg_tablesize		= SG_ALL,
	.max_sectors		= SCSI_DEFAULT_MAX_SECTORS,
	.use_clustering		= ENABLE_CLUSTERING,
	.eh_device_reset_handler = sas_eh_device_reset_handler,
	.eh_bus_reset_handler	= sas_eh_bus_reset_handler,
	.target_destroy		= sas_target_destroy,
	.ioctl			= sas_ioctl,
};

static struct sas_domain_function_template hisi_sas_transport_ops = {
	.lldd_dev_found		= hisi_sas_dev_found,
	.lldd_dev_gone		= hisi_sas_dev_gone,
	.lldd_execute_task	= hisi_sas_queue_command,
	.lldd_port_formed	= hisi_sas_port_formed,
	.lldd_port_deformed	= hisi_sas_port_deformed,
};

static int hisi_sas_alloc(struct hisi_hba *hisi_hba, struct Scsi_Host *shost)
{
	int i, s;
	struct platform_device *pdev = hisi_hba->pdev;
	struct device *dev = &pdev->dev;

	spin_lock_init(&hisi_hba->lock);
	for (i = 0; i < hisi_hba->n_phy; i++) {
		hisi_sas_phy_init(hisi_hba, i);
		hisi_hba->port[i].port_attached = 0;
		hisi_hba->port[i].id = -1;
		INIT_LIST_HEAD(&hisi_hba->port[i].list);
	}

	for (i = 0; i < HISI_SAS_MAX_DEVICES; i++) {
		hisi_hba->devices[i].dev_type = SAS_PHY_UNUSED;
		hisi_hba->devices[i].device_id = i;
		hisi_hba->devices[i].dev_status = HISI_SAS_DEV_NORMAL;
	}

	for (i = 0; i < hisi_hba->queue_count; i++) {
		struct hisi_sas_cq *cq = &hisi_hba->cq[i];

		/* Completion queue structure */
		cq->id = i;
		cq->hisi_hba = hisi_hba;

		/* Delivery queue */
		s = sizeof(struct hisi_sas_cmd_hdr) * HISI_SAS_QUEUE_SLOTS;
		hisi_hba->cmd_hdr[i] = dma_alloc_coherent(dev, s,
					&hisi_hba->cmd_hdr_dma[i], GFP_KERNEL);
		if (!hisi_hba->cmd_hdr[i])
			goto err_out;
		memset(hisi_hba->cmd_hdr[i], 0, s);

		/* Completion queue */
		s = hisi_hba->hw->complete_hdr_size * HISI_SAS_QUEUE_SLOTS;
		hisi_hba->complete_hdr[i] = dma_alloc_coherent(dev, s,
				&hisi_hba->complete_hdr_dma[i], GFP_KERNEL);
		if (!hisi_hba->complete_hdr[i])
			goto err_out;
		memset(hisi_hba->complete_hdr[i], 0, s);
	}

	s = HISI_SAS_STATUS_BUF_SZ;
	hisi_hba->status_buffer_pool = dma_pool_create("status_buffer",
						       dev, s, 16, 0);
	if (!hisi_hba->status_buffer_pool)
		goto err_out;

	s = HISI_SAS_COMMAND_TABLE_SZ;
	hisi_hba->command_table_pool = dma_pool_create("command_table",
						       dev, s, 16, 0);
	if (!hisi_hba->command_table_pool)
		goto err_out;

	s = HISI_SAS_MAX_ITCT_ENTRIES * sizeof(struct hisi_sas_itct);
	hisi_hba->itct = dma_alloc_coherent(dev, s, &hisi_hba->itct_dma,
					    GFP_KERNEL);
	if (!hisi_hba->itct)
		goto err_out;

	memset(hisi_hba->itct, 0, s);

	hisi_hba->slot_info = devm_kcalloc(dev, HISI_SAS_COMMAND_ENTRIES,
					   sizeof(struct hisi_sas_slot),
					   GFP_KERNEL);
	if (!hisi_hba->slot_info)
		goto err_out;

	s = HISI_SAS_COMMAND_ENTRIES * sizeof(struct hisi_sas_iost);
	hisi_hba->iost = dma_alloc_coherent(dev, s, &hisi_hba->iost_dma,
					    GFP_KERNEL);
	if (!hisi_hba->iost)
		goto err_out;

	memset(hisi_hba->iost, 0, s);

	s = HISI_SAS_COMMAND_ENTRIES * sizeof(struct hisi_sas_breakpoint);
	hisi_hba->breakpoint = dma_alloc_coherent(dev, s,
				&hisi_hba->breakpoint_dma, GFP_KERNEL);
	if (!hisi_hba->breakpoint)
		goto err_out;

	memset(hisi_hba->breakpoint, 0, s);

	hisi_hba->slot_index_count = HISI_SAS_COMMAND_ENTRIES;
	s = hisi_hba->slot_index_count / sizeof(unsigned long);
	hisi_hba->slot_index_tags = devm_kzalloc(dev, s, GFP_KERNEL);
	if (!hisi_hba->slot_index_tags)
		goto err_out;

	hisi_hba->sge_page_pool = dma_pool_create("status_sge", dev,
				sizeof(struct hisi_sas_sge_page), 16, 0);
	if (!hisi_hba->sge_page_pool)
		goto err_out;

	s = sizeof(struct hisi_sas_initial_fis) * HISI_SAS_MAX_PHYS;
	hisi_hba->initial_fis = dma_alloc_coherent(dev, s,
				&hisi_hba->initial_fis_dma, GFP_KERNEL);
	if (!hisi_hba->initial_fis)
		goto err_out;
	memset(hisi_hba->initial_fis, 0, s);

	s = HISI_SAS_COMMAND_ENTRIES * sizeof(struct hisi_sas_breakpoint) * 2;
	hisi_hba->sata_breakpoint = dma_alloc_coherent(dev, s,
				&hisi_hba->sata_breakpoint_dma, GFP_KERNEL);
	if (!hisi_hba->sata_breakpoint)
		goto err_out;
	memset(hisi_hba->sata_breakpoint, 0, s);

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

static void hisi_sas_free(struct hisi_hba *hisi_hba)
{
	struct device *dev = &hisi_hba->pdev->dev;
	int i, s;

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

	dma_pool_destroy(hisi_hba->status_buffer_pool);
	dma_pool_destroy(hisi_hba->command_table_pool);
	dma_pool_destroy(hisi_hba->sge_page_pool);

	s = HISI_SAS_MAX_ITCT_ENTRIES * sizeof(struct hisi_sas_itct);
	if (hisi_hba->itct)
		dma_free_coherent(dev, s,
				  hisi_hba->itct, hisi_hba->itct_dma);

	s = HISI_SAS_COMMAND_ENTRIES * sizeof(struct hisi_sas_iost);
	if (hisi_hba->iost)
		dma_free_coherent(dev, s,
				  hisi_hba->iost, hisi_hba->iost_dma);

	s = HISI_SAS_COMMAND_ENTRIES * sizeof(struct hisi_sas_breakpoint);
	if (hisi_hba->breakpoint)
		dma_free_coherent(dev, s,
				  hisi_hba->breakpoint,
				  hisi_hba->breakpoint_dma);


	s = sizeof(struct hisi_sas_initial_fis) * HISI_SAS_MAX_PHYS;
	if (hisi_hba->initial_fis)
		dma_free_coherent(dev, s,
				  hisi_hba->initial_fis,
				  hisi_hba->initial_fis_dma);

	s = HISI_SAS_COMMAND_ENTRIES * sizeof(struct hisi_sas_breakpoint) * 2;
	if (hisi_hba->sata_breakpoint)
		dma_free_coherent(dev, s,
				  hisi_hba->sata_breakpoint,
				  hisi_hba->sata_breakpoint_dma);

	if (hisi_hba->wq)
		destroy_workqueue(hisi_hba->wq);
}

static struct Scsi_Host *hisi_sas_shost_alloc(struct platform_device *pdev,
					      const struct hisi_sas_hw *hw)
{
	struct resource *res;
	struct Scsi_Host *shost;
	struct hisi_hba *hisi_hba;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct property *sas_addr_prop;
	int num;

	shost = scsi_host_alloc(&hisi_sas_sht, sizeof(*hisi_hba));
	if (!shost)
		goto err_out;
	hisi_hba = shost_priv(shost);

	hisi_hba->hw = hw;
	hisi_hba->pdev = pdev;
	hisi_hba->shost = shost;
	SHOST_TO_SAS_HA(shost) = &hisi_hba->sha;

	init_timer(&hisi_hba->timer);

	sas_addr_prop = of_find_property(np, "sas-addr", NULL);
	if (!sas_addr_prop || (sas_addr_prop->length != SAS_ADDR_SIZE))
		goto err_out;
	memcpy(hisi_hba->sas_addr, sas_addr_prop->value, SAS_ADDR_SIZE);

	if (of_property_read_u32(np, "ctrl-reset-reg",
				 &hisi_hba->ctrl_reset_reg))
		goto err_out;

	if (of_property_read_u32(np, "ctrl-reset-sts-reg",
				 &hisi_hba->ctrl_reset_sts_reg))
		goto err_out;

	if (of_property_read_u32(np, "ctrl-clock-ena-reg",
				 &hisi_hba->ctrl_clock_ena_reg))
		goto err_out;

	if (of_property_read_u32(np, "phy-count", &hisi_hba->n_phy))
		goto err_out;

	if (of_property_read_u32(np, "queue-count", &hisi_hba->queue_count))
		goto err_out;

	num = of_irq_count(np);
	hisi_hba->int_names = devm_kcalloc(dev, num,
					   HISI_SAS_NAME_LEN,
					   GFP_KERNEL);
	if (!hisi_hba->int_names)
		goto err_out;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hisi_hba->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(hisi_hba->regs))
		goto err_out;

	hisi_hba->ctrl = syscon_regmap_lookup_by_phandle(
				np, "hisilicon,sas-syscon");
	if (IS_ERR(hisi_hba->ctrl))
		goto err_out;

	if (hisi_sas_alloc(hisi_hba, shost)) {
		hisi_sas_free(hisi_hba);
		goto err_out;
	}

	return shost;
err_out:
	dev_err(dev, "shost alloc failed\n");
	return NULL;
}

static void hisi_sas_init_add(struct hisi_hba *hisi_hba)
{
	int i;

	for (i = 0; i < hisi_hba->n_phy; i++)
		memcpy(&hisi_hba->phy[i].dev_sas_addr,
		       hisi_hba->sas_addr,
		       SAS_ADDR_SIZE);
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
	if (!shost) {
		rc = -ENOMEM;
		goto err_out_ha;
	}

	sha = SHOST_TO_SAS_HA(shost);
	hisi_hba = shost_priv(shost);
	platform_set_drvdata(pdev, sha);

	if (dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64)) &&
	    dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32))) {
		dev_err(dev, "No usable DMA addressing method\n");
		rc = -EIO;
		goto err_out_ha;
	}

	phy_nr = port_nr = hisi_hba->n_phy;

	arr_phy = devm_kcalloc(dev, phy_nr, sizeof(void *), GFP_KERNEL);
	arr_port = devm_kcalloc(dev, port_nr, sizeof(void *), GFP_KERNEL);
	if (!arr_phy || !arr_port)
		return -ENOMEM;

	sha->sas_phy = arr_phy;
	sha->sas_port = arr_port;
	sha->core.shost = shost;
	sha->lldd_ha = hisi_hba;

	shost->transportt = hisi_sas_stt;
	shost->max_id = HISI_SAS_MAX_DEVICES;
	shost->max_lun = ~0;
	shost->max_channel = 1;
	shost->max_cmd_len = 16;
	shost->sg_tablesize = min_t(u16, SG_ALL, HISI_SAS_SGE_PAGE_CNT);
	shost->can_queue = HISI_SAS_COMMAND_ENTRIES;
	shost->cmd_per_lun = HISI_SAS_COMMAND_ENTRIES;

	sha->sas_ha_name = DRV_NAME;
	sha->dev = &hisi_hba->pdev->dev;
	sha->lldd_module = THIS_MODULE;
	sha->sas_addr = &hisi_hba->sas_addr[0];
	sha->num_phys = hisi_hba->n_phy;
	sha->core.shost = hisi_hba->shost;

	for (i = 0; i < hisi_hba->n_phy; i++) {
		sha->sas_phy[i] = &hisi_hba->phy[i].sas_phy;
		sha->sas_port[i] = &hisi_hba->port[i].sas_port;
	}

	hisi_sas_init_add(hisi_hba);

	rc = hisi_hba->hw->hw_init(hisi_hba);
	if (rc)
		goto err_out_ha;

	rc = scsi_add_host(shost, &pdev->dev);
	if (rc)
		goto err_out_ha;

	rc = sas_register_ha(sha);
	if (rc)
		goto err_out_register_ha;

	scsi_scan_host(shost);

	return 0;

err_out_register_ha:
	scsi_remove_host(shost);
err_out_ha:
	kfree(shost);
	return rc;
}
EXPORT_SYMBOL_GPL(hisi_sas_probe);

int hisi_sas_remove(struct platform_device *pdev)
{
	struct sas_ha_struct *sha = platform_get_drvdata(pdev);
	struct hisi_hba *hisi_hba = sha->lldd_ha;

	scsi_remove_host(sha->core.shost);
	sas_unregister_ha(sha);
	sas_remove_host(sha->core.shost);

	hisi_sas_free(hisi_hba);
	return 0;
}
EXPORT_SYMBOL_GPL(hisi_sas_remove);

static __init int hisi_sas_init(void)
{
	pr_info("hisi_sas: driver version %s\n", DRV_VERSION);

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

MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Garry <john.garry@huawei.com>");
MODULE_DESCRIPTION("HISILICON SAS controller driver");
MODULE_ALIAS("platform:" DRV_NAME);
