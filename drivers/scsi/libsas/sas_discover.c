/*
 * Serial Attached SCSI (SAS) Discover process
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 *
 * This file is licensed under GPLv2.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <linux/pci.h>
#include <linux/scatterlist.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_eh.h>
#include "sas_internal.h"

#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_sas.h>
#include "../scsi_sas_internal.h"

/* ---------- Basic task processing for discovery purposes ---------- */

void sas_init_dev(struct domain_device *dev)
{
        INIT_LIST_HEAD(&dev->siblings);
        INIT_LIST_HEAD(&dev->dev_list_node);
        switch (dev->dev_type) {
        case SAS_END_DEV:
                break;
        case EDGE_DEV:
        case FANOUT_DEV:
                INIT_LIST_HEAD(&dev->ex_dev.children);
                break;
        case SATA_DEV:
        case SATA_PM:
        case SATA_PM_PORT:
                INIT_LIST_HEAD(&dev->sata_dev.children);
                break;
        default:
                break;
        }
}

static void sas_task_timedout(unsigned long _task)
{
	struct sas_task *task = (void *) _task;
	unsigned long flags;

	spin_lock_irqsave(&task->task_state_lock, flags);
	if (!(task->task_state_flags & SAS_TASK_STATE_DONE))
		task->task_state_flags |= SAS_TASK_STATE_ABORTED;
	spin_unlock_irqrestore(&task->task_state_lock, flags);

	complete(&task->completion);
}

static void sas_disc_task_done(struct sas_task *task)
{
	if (!del_timer(&task->timer))
		return;
	complete(&task->completion);
}

#define SAS_DEV_TIMEOUT 10

/**
 * sas_execute_task -- Basic task processing for discovery
 * @task: the task to be executed
 * @buffer: pointer to buffer to do I/O
 * @size: size of @buffer
 * @pci_dma_dir: PCI_DMA_...
 */
static int sas_execute_task(struct sas_task *task, void *buffer, int size,
			    int pci_dma_dir)
{
	int res = 0;
	struct scatterlist *scatter = NULL;
	struct task_status_struct *ts = &task->task_status;
	int num_scatter = 0;
	int retries = 0;
	struct sas_internal *i =
		to_sas_internal(task->dev->port->ha->core.shost->transportt);

	if (pci_dma_dir != PCI_DMA_NONE) {
		scatter = kzalloc(sizeof(*scatter), GFP_KERNEL);
		if (!scatter)
			goto out;

		sg_init_one(scatter, buffer, size);
		num_scatter = 1;
	}

	task->task_proto = task->dev->tproto;
	task->scatter = scatter;
	task->num_scatter = num_scatter;
	task->total_xfer_len = size;
	task->data_dir = pci_dma_dir;
	task->task_done = sas_disc_task_done;

	for (retries = 0; retries < 5; retries++) {
		task->task_state_flags = SAS_TASK_STATE_PENDING;
		init_completion(&task->completion);

		task->timer.data = (unsigned long) task;
		task->timer.function = sas_task_timedout;
		task->timer.expires = jiffies + SAS_DEV_TIMEOUT*HZ;
		add_timer(&task->timer);

		res = i->dft->lldd_execute_task(task, 1, GFP_KERNEL);
		if (res) {
			del_timer(&task->timer);
			SAS_DPRINTK("executing SAS discovery task failed:%d\n",
				    res);
			goto ex_err;
		}
		wait_for_completion(&task->completion);
		res = -ETASK;
		if (task->task_state_flags & SAS_TASK_STATE_ABORTED) {
			int res2;
			SAS_DPRINTK("task aborted, flags:0x%x\n",
				    task->task_state_flags);
			res2 = i->dft->lldd_abort_task(task);
			SAS_DPRINTK("came back from abort task\n");
			if (!(task->task_state_flags & SAS_TASK_STATE_DONE)) {
				if (res2 == TMF_RESP_FUNC_COMPLETE)
					continue; /* Retry the task */
				else
					goto ex_err;
			}
		}
		if (task->task_status.stat == SAM_BUSY ||
			   task->task_status.stat == SAM_TASK_SET_FULL ||
			   task->task_status.stat == SAS_QUEUE_FULL) {
			SAS_DPRINTK("task: q busy, sleeping...\n");
			schedule_timeout_interruptible(HZ);
		} else if (task->task_status.stat == SAM_CHECK_COND) {
			struct scsi_sense_hdr shdr;

			if (!scsi_normalize_sense(ts->buf, ts->buf_valid_size,
						  &shdr)) {
				SAS_DPRINTK("couldn't normalize sense\n");
				continue;
			}
			if ((shdr.sense_key == 6 && shdr.asc == 0x29) ||
			    (shdr.sense_key == 2 && shdr.asc == 4 &&
			     shdr.ascq == 1)) {
				SAS_DPRINTK("device %016llx LUN: %016llx "
					    "powering up or not ready yet, "
					    "sleeping...\n",
					    SAS_ADDR(task->dev->sas_addr),
					    SAS_ADDR(task->ssp_task.LUN));

				schedule_timeout_interruptible(5*HZ);
			} else if (shdr.sense_key == 1) {
				res = 0;
				break;
			} else if (shdr.sense_key == 5) {
				break;
			} else {
				SAS_DPRINTK("dev %016llx LUN: %016llx "
					    "sense key:0x%x ASC:0x%x ASCQ:0x%x"
					    "\n",
					    SAS_ADDR(task->dev->sas_addr),
					    SAS_ADDR(task->ssp_task.LUN),
					    shdr.sense_key,
					    shdr.asc, shdr.ascq);
			}
		} else if (task->task_status.resp != SAS_TASK_COMPLETE ||
			   task->task_status.stat != SAM_GOOD) {
			SAS_DPRINTK("task finished with resp:0x%x, "
				    "stat:0x%x\n",
				    task->task_status.resp,
				    task->task_status.stat);
			goto ex_err;
		} else {
			res = 0;
			break;
		}
	}
ex_err:
	if (pci_dma_dir != PCI_DMA_NONE)
		kfree(scatter);
out:
	return res;
}

/* ---------- Domain device discovery ---------- */

/**
 * sas_get_port_device -- Discover devices which caused port creation
 * @port: pointer to struct sas_port of interest
 *
 * Devices directly attached to a HA port, have no parent.  This is
 * how we know they are (domain) "root" devices.  All other devices
 * do, and should have their "parent" pointer set appropriately as
 * soon as a child device is discovered.
 */
static int sas_get_port_device(struct asd_sas_port *port)
{
	unsigned long flags;
	struct asd_sas_phy *phy;
	struct sas_rphy *rphy;
	struct domain_device *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	spin_lock_irqsave(&port->phy_list_lock, flags);
	if (list_empty(&port->phy_list)) {
		spin_unlock_irqrestore(&port->phy_list_lock, flags);
		kfree(dev);
		return -ENODEV;
	}
	phy = container_of(port->phy_list.next, struct asd_sas_phy, port_phy_el);
	spin_lock(&phy->frame_rcvd_lock);
	memcpy(dev->frame_rcvd, phy->frame_rcvd, min(sizeof(dev->frame_rcvd),
					     (size_t)phy->frame_rcvd_size));
	spin_unlock(&phy->frame_rcvd_lock);
	spin_unlock_irqrestore(&port->phy_list_lock, flags);

	if (dev->frame_rcvd[0] == 0x34 && port->oob_mode == SATA_OOB_MODE) {
		struct dev_to_host_fis *fis =
			(struct dev_to_host_fis *) dev->frame_rcvd;
		if (fis->interrupt_reason == 1 && fis->lbal == 1 &&
		    fis->byte_count_low==0x69 && fis->byte_count_high == 0x96
		    && (fis->device & ~0x10) == 0)
			dev->dev_type = SATA_PM;
		else
			dev->dev_type = SATA_DEV;
		dev->tproto = SATA_PROTO;
	} else {
		struct sas_identify_frame *id =
			(struct sas_identify_frame *) dev->frame_rcvd;
		dev->dev_type = id->dev_type;
		dev->iproto = id->initiator_bits;
		dev->tproto = id->target_bits;
	}

	sas_init_dev(dev);

	switch (dev->dev_type) {
	case SAS_END_DEV:
		rphy = sas_end_device_alloc(port->port);
		break;
	case EDGE_DEV:
		rphy = sas_expander_alloc(port->port,
					  SAS_EDGE_EXPANDER_DEVICE);
		break;
	case FANOUT_DEV:
		rphy = sas_expander_alloc(port->port,
					  SAS_FANOUT_EXPANDER_DEVICE);
		break;
	case SATA_DEV:
	default:
		printk("ERROR: Unidentified device type %d\n", dev->dev_type);
		rphy = NULL;
		break;
	}

	if (!rphy) {
		kfree(dev);
		return -ENODEV;
	}
	rphy->identify.phy_identifier = phy->phy->identify.phy_identifier;
	memcpy(dev->sas_addr, port->attached_sas_addr, SAS_ADDR_SIZE);
	sas_fill_in_rphy(dev, rphy);
	sas_hash_addr(dev->hashed_sas_addr, dev->sas_addr);
	port->port_dev = dev;
	dev->port = port;
	dev->linkrate = port->linkrate;
	dev->min_linkrate = port->linkrate;
	dev->max_linkrate = port->linkrate;
	dev->pathways = port->num_phys;
	memset(port->disc.fanout_sas_addr, 0, SAS_ADDR_SIZE);
	memset(port->disc.eeds_a, 0, SAS_ADDR_SIZE);
	memset(port->disc.eeds_b, 0, SAS_ADDR_SIZE);
	port->disc.max_level = 0;

	dev->rphy = rphy;
	spin_lock(&port->dev_list_lock);
	list_add_tail(&dev->dev_list_node, &port->dev_list);
	spin_unlock(&port->dev_list_lock);

	return 0;
}

/* ---------- Discover and Revalidate ---------- */

/* ---------- SATA ---------- */

static void sas_get_ata_command_set(struct domain_device *dev)
{
	struct dev_to_host_fis *fis =
		(struct dev_to_host_fis *) dev->frame_rcvd;

	if ((fis->sector_count == 1 && /* ATA */
	     fis->lbal         == 1 &&
	     fis->lbam         == 0 &&
	     fis->lbah         == 0 &&
	     fis->device       == 0)
	    ||
	    (fis->sector_count == 0 && /* CE-ATA (mATA) */
	     fis->lbal         == 0 &&
	     fis->lbam         == 0xCE &&
	     fis->lbah         == 0xAA &&
	     (fis->device & ~0x10) == 0))

		dev->sata_dev.command_set = ATA_COMMAND_SET;

	else if ((fis->interrupt_reason == 1 &&	/* ATAPI */
		  fis->lbal             == 1 &&
		  fis->byte_count_low   == 0x14 &&
		  fis->byte_count_high  == 0xEB &&
		  (fis->device & ~0x10) == 0))

		dev->sata_dev.command_set = ATAPI_COMMAND_SET;

	else if ((fis->sector_count == 1 && /* SEMB */
		  fis->lbal         == 1 &&
		  fis->lbam         == 0x3C &&
		  fis->lbah         == 0xC3 &&
		  fis->device       == 0)
		||
		 (fis->interrupt_reason == 1 &&	/* SATA PM */
		  fis->lbal             == 1 &&
		  fis->byte_count_low   == 0x69 &&
		  fis->byte_count_high  == 0x96 &&
		  (fis->device & ~0x10) == 0))

		/* Treat it as a superset? */
		dev->sata_dev.command_set = ATAPI_COMMAND_SET;
}

/**
 * sas_issue_ata_cmd -- Basic SATA command processing for discovery
 * @dev: the device to send the command to
 * @command: the command register
 * @features: the features register
 * @buffer: pointer to buffer to do I/O
 * @size: size of @buffer
 * @pci_dma_dir: PCI_DMA_...
 */
static int sas_issue_ata_cmd(struct domain_device *dev, u8 command,
			     u8 features, void *buffer, int size,
			     int pci_dma_dir)
{
	int res = 0;
	struct sas_task *task;
	struct dev_to_host_fis *d2h_fis = (struct dev_to_host_fis *)
		&dev->frame_rcvd[0];

	res = -ENOMEM;
	task = sas_alloc_task(GFP_KERNEL);
	if (!task)
		goto out;

	task->dev = dev;

	task->ata_task.fis.command = command;
	task->ata_task.fis.features = features;
	task->ata_task.fis.device = d2h_fis->device;
	task->ata_task.retry_count = 1;

	res = sas_execute_task(task, buffer, size, pci_dma_dir);

	sas_free_task(task);
out:
	return res;
}

static void sas_sata_propagate_sas_addr(struct domain_device *dev)
{
	unsigned long flags;
	struct asd_sas_port *port = dev->port;
	struct asd_sas_phy  *phy;

	BUG_ON(dev->parent);

	memcpy(port->attached_sas_addr, dev->sas_addr, SAS_ADDR_SIZE);
	spin_lock_irqsave(&port->phy_list_lock, flags);
	list_for_each_entry(phy, &port->phy_list, port_phy_el)
		memcpy(phy->attached_sas_addr, dev->sas_addr, SAS_ADDR_SIZE);
	spin_unlock_irqrestore(&port->phy_list_lock, flags);
}

#define ATA_IDENTIFY_DEV         0xEC
#define ATA_IDENTIFY_PACKET_DEV  0xA1
#define ATA_SET_FEATURES         0xEF
#define ATA_FEATURE_PUP_STBY_SPIN_UP 0x07

/**
 * sas_discover_sata_dev -- discover a STP/SATA device (SATA_DEV)
 * @dev: STP/SATA device of interest (ATA/ATAPI)
 *
 * The LLDD has already been notified of this device, so that we can
 * send FISes to it.  Here we try to get IDENTIFY DEVICE or IDENTIFY
 * PACKET DEVICE, if ATAPI device, so that the LLDD can fine-tune its
 * performance for this device.
 */
static int sas_discover_sata_dev(struct domain_device *dev)
{
	int     res;
	__le16  *identify_x;
	u8      command;

	identify_x = kzalloc(512, GFP_KERNEL);
	if (!identify_x)
		return -ENOMEM;

	if (dev->sata_dev.command_set == ATA_COMMAND_SET) {
		dev->sata_dev.identify_device = identify_x;
		command = ATA_IDENTIFY_DEV;
	} else {
		dev->sata_dev.identify_packet_device = identify_x;
		command = ATA_IDENTIFY_PACKET_DEV;
	}

	res = sas_issue_ata_cmd(dev, command, 0, identify_x, 512,
				PCI_DMA_FROMDEVICE);
	if (res)
		goto out_err;

	/* lives on the media? */
	if (le16_to_cpu(identify_x[0]) & 4) {
		/* incomplete response */
		SAS_DPRINTK("sending SET FEATURE/PUP_STBY_SPIN_UP to "
			    "dev %llx\n", SAS_ADDR(dev->sas_addr));
		if (!le16_to_cpu(identify_x[83] & (1<<6)))
			goto cont1;
		res = sas_issue_ata_cmd(dev, ATA_SET_FEATURES,
					ATA_FEATURE_PUP_STBY_SPIN_UP,
					NULL, 0, PCI_DMA_NONE);
		if (res)
			goto cont1;

		schedule_timeout_interruptible(5*HZ); /* More time? */
		res = sas_issue_ata_cmd(dev, command, 0, identify_x, 512,
					PCI_DMA_FROMDEVICE);
		if (res)
			goto out_err;
	}
cont1:
	/* Get WWN */
	if (dev->port->oob_mode != SATA_OOB_MODE) {
		memcpy(dev->sas_addr, dev->sata_dev.rps_resp.rps.stp_sas_addr,
		       SAS_ADDR_SIZE);
	} else if (dev->sata_dev.command_set == ATA_COMMAND_SET &&
		   (le16_to_cpu(dev->sata_dev.identify_device[108]) & 0xF000)
		   == 0x5000) {
		int i;

		for (i = 0; i < 4; i++) {
			dev->sas_addr[2*i] =
	     (le16_to_cpu(dev->sata_dev.identify_device[108+i]) & 0xFF00) >> 8;
			dev->sas_addr[2*i+1] =
	      le16_to_cpu(dev->sata_dev.identify_device[108+i]) & 0x00FF;
		}
	}
	sas_hash_addr(dev->hashed_sas_addr, dev->sas_addr);
	if (!dev->parent)
		sas_sata_propagate_sas_addr(dev);

	/* XXX Hint: register this SATA device with SATL.
	   When this returns, dev->sata_dev->lu is alive and
	   present.
	sas_satl_register_dev(dev);
	*/
	return 0;
out_err:
	dev->sata_dev.identify_packet_device = NULL;
	dev->sata_dev.identify_device = NULL;
	kfree(identify_x);
	return res;
}

static int sas_discover_sata_pm(struct domain_device *dev)
{
	return -ENODEV;
}

int sas_notify_lldd_dev_found(struct domain_device *dev)
{
	int res = 0;
	struct sas_ha_struct *sas_ha = dev->port->ha;
	struct Scsi_Host *shost = sas_ha->core.shost;
	struct sas_internal *i = to_sas_internal(shost->transportt);

	if (i->dft->lldd_dev_found) {
		res = i->dft->lldd_dev_found(dev);
		if (res) {
			printk("sas: driver on pcidev %s cannot handle "
			       "device %llx, error:%d\n",
			       pci_name(sas_ha->pcidev),
			       SAS_ADDR(dev->sas_addr), res);
		}
	}
	return res;
}


void sas_notify_lldd_dev_gone(struct domain_device *dev)
{
	struct sas_ha_struct *sas_ha = dev->port->ha;
	struct Scsi_Host *shost = sas_ha->core.shost;
	struct sas_internal *i = to_sas_internal(shost->transportt);

	if (i->dft->lldd_dev_gone)
		i->dft->lldd_dev_gone(dev);
}

/* ---------- Common/dispatchers ---------- */

/**
 * sas_discover_sata -- discover an STP/SATA domain device
 * @dev: pointer to struct domain_device of interest
 *
 * First we notify the LLDD of this device, so we can send frames to
 * it.  Then depending on the type of device we call the appropriate
 * discover functions.  Once device discover is done, we notify the
 * LLDD so that it can fine-tune its parameters for the device, by
 * removing it and then adding it.  That is, the second time around,
 * the driver would have certain fields, that it is looking at, set.
 * Finally we initialize the kobj so that the device can be added to
 * the system at registration time.  Devices directly attached to a HA
 * port, have no parents.  All other devices do, and should have their
 * "parent" pointer set appropriately before calling this function.
 */
int sas_discover_sata(struct domain_device *dev)
{
	int res;

	sas_get_ata_command_set(dev);

	res = sas_notify_lldd_dev_found(dev);
	if (res)
		return res;

	switch (dev->dev_type) {
	case SATA_DEV:
		res = sas_discover_sata_dev(dev);
		break;
	case SATA_PM:
		res = sas_discover_sata_pm(dev);
		break;
	default:
		break;
	}

	sas_notify_lldd_dev_gone(dev);
	if (!res) {
		sas_notify_lldd_dev_found(dev);
	}
	return res;
}

/**
 * sas_discover_end_dev -- discover an end device (SSP, etc)
 * @end: pointer to domain device of interest
 *
 * See comment in sas_discover_sata().
 */
int sas_discover_end_dev(struct domain_device *dev)
{
	int res;

	res = sas_notify_lldd_dev_found(dev);
	if (res)
		return res;

	res = sas_rphy_add(dev->rphy);
	if (res)
		goto out_err;

	/* do this to get the end device port attributes which will have
	 * been scanned in sas_rphy_add */
	sas_notify_lldd_dev_gone(dev);
	sas_notify_lldd_dev_found(dev);

	return 0;

out_err:
	sas_notify_lldd_dev_gone(dev);
	return res;
}

/* ---------- Device registration and unregistration ---------- */

static inline void sas_unregister_common_dev(struct domain_device *dev)
{
	sas_notify_lldd_dev_gone(dev);
	if (!dev->parent)
		dev->port->port_dev = NULL;
	else
		list_del_init(&dev->siblings);
	list_del_init(&dev->dev_list_node);
}

void sas_unregister_dev(struct domain_device *dev)
{
	if (dev->rphy) {
		sas_remove_children(&dev->rphy->dev);
		sas_rphy_delete(dev->rphy);
		dev->rphy = NULL;
	}
	if (dev->dev_type == EDGE_DEV || dev->dev_type == FANOUT_DEV) {
		/* remove the phys and ports, everything else should be gone */
		kfree(dev->ex_dev.ex_phy);
		dev->ex_dev.ex_phy = NULL;
	}
	sas_unregister_common_dev(dev);
}

void sas_unregister_domain_devices(struct asd_sas_port *port)
{
	struct domain_device *dev, *n;

	list_for_each_entry_safe_reverse(dev,n,&port->dev_list,dev_list_node)
		sas_unregister_dev(dev);

	port->port->rphy = NULL;

}

/* ---------- Discovery and Revalidation ---------- */

/**
 * sas_discover_domain -- discover the domain
 * @port: port to the domain of interest
 *
 * NOTE: this process _must_ quit (return) as soon as any connection
 * errors are encountered.  Connection recovery is done elsewhere.
 * Discover process only interrogates devices in order to discover the
 * domain.
 */
static void sas_discover_domain(struct work_struct *work)
{
	int error = 0;
	struct sas_discovery_event *ev =
		container_of(work, struct sas_discovery_event, work);
	struct asd_sas_port *port = ev->port;

	sas_begin_event(DISCE_DISCOVER_DOMAIN, &port->disc.disc_event_lock,
			&port->disc.pending);

	if (port->port_dev)
		return ;
	else {
		error = sas_get_port_device(port);
		if (error)
			return;
	}

	SAS_DPRINTK("DOING DISCOVERY on port %d, pid:%d\n", port->id,
		    current->pid);

	switch (port->port_dev->dev_type) {
	case SAS_END_DEV:
		error = sas_discover_end_dev(port->port_dev);
		break;
	case EDGE_DEV:
	case FANOUT_DEV:
		error = sas_discover_root_expander(port->port_dev);
		break;
	case SATA_DEV:
	case SATA_PM:
		error = sas_discover_sata(port->port_dev);
		break;
	default:
		SAS_DPRINTK("unhandled device %d\n", port->port_dev->dev_type);
		break;
	}

	if (error) {
		kfree(port->port_dev); /* not kobject_register-ed yet */
		port->port_dev = NULL;
	}

	SAS_DPRINTK("DONE DISCOVERY on port %d, pid:%d, result:%d\n", port->id,
		    current->pid, error);
}

static void sas_revalidate_domain(struct work_struct *work)
{
	int res = 0;
	struct sas_discovery_event *ev =
		container_of(work, struct sas_discovery_event, work);
	struct asd_sas_port *port = ev->port;

	sas_begin_event(DISCE_REVALIDATE_DOMAIN, &port->disc.disc_event_lock,
			&port->disc.pending);

	SAS_DPRINTK("REVALIDATING DOMAIN on port %d, pid:%d\n", port->id,
		    current->pid);
	if (port->port_dev)
		res = sas_ex_revalidate_domain(port->port_dev);

	SAS_DPRINTK("done REVALIDATING DOMAIN on port %d, pid:%d, res 0x%x\n",
		    port->id, current->pid, res);
}

/* ---------- Events ---------- */

int sas_discover_event(struct asd_sas_port *port, enum discover_event ev)
{
	struct sas_discovery *disc;

	if (!port)
		return 0;
	disc = &port->disc;

	BUG_ON(ev >= DISC_NUM_EVENTS);

	sas_queue_event(ev, &disc->disc_event_lock, &disc->pending,
			&disc->disc_work[ev].work, port->ha->core.shost);

	return 0;
}

/**
 * sas_init_disc -- initialize the discovery struct in the port
 * @port: pointer to struct port
 *
 * Called when the ports are being initialized.
 */
void sas_init_disc(struct sas_discovery *disc, struct asd_sas_port *port)
{
	int i;

	static const work_func_t sas_event_fns[DISC_NUM_EVENTS] = {
		[DISCE_DISCOVER_DOMAIN] = sas_discover_domain,
		[DISCE_REVALIDATE_DOMAIN] = sas_revalidate_domain,
	};

	spin_lock_init(&disc->disc_event_lock);
	disc->pending = 0;
	for (i = 0; i < DISC_NUM_EVENTS; i++) {
		INIT_WORK(&disc->disc_work[i].work, sas_event_fns[i]);
		disc->disc_work[i].port = port;
	}
}
