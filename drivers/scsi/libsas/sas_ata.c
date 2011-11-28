/*
 * Support for SATA devices on Serial Attached SCSI (SAS) controllers
 *
 * Copyright (C) 2006 IBM Corporation
 *
 * Written by: Darrick J. Wong <djwong@us.ibm.com>, IBM Corporation
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <linux/scatterlist.h>
#include <linux/slab.h>

#include <scsi/sas_ata.h>
#include "sas_internal.h"
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_sas.h>
#include "../scsi_sas_internal.h"
#include "../scsi_transport_api.h"
#include <scsi/scsi_eh.h>

static enum ata_completion_errors sas_to_ata_err(struct task_status_struct *ts)
{
	/* Cheesy attempt to translate SAS errors into ATA.  Hah! */

	/* transport error */
	if (ts->resp == SAS_TASK_UNDELIVERED)
		return AC_ERR_ATA_BUS;

	/* ts->resp == SAS_TASK_COMPLETE */
	/* task delivered, what happened afterwards? */
	switch (ts->stat) {
		case SAS_DEV_NO_RESPONSE:
			return AC_ERR_TIMEOUT;

		case SAS_INTERRUPTED:
		case SAS_PHY_DOWN:
		case SAS_NAK_R_ERR:
			return AC_ERR_ATA_BUS;


		case SAS_DATA_UNDERRUN:
			/*
			 * Some programs that use the taskfile interface
			 * (smartctl in particular) can cause underrun
			 * problems.  Ignore these errors, perhaps at our
			 * peril.
			 */
			return 0;

		case SAS_DATA_OVERRUN:
		case SAS_QUEUE_FULL:
		case SAS_DEVICE_UNKNOWN:
		case SAS_SG_ERR:
			return AC_ERR_INVALID;

		case SAS_OPEN_TO:
		case SAS_OPEN_REJECT:
			SAS_DPRINTK("%s: Saw error %d.  What to do?\n",
				    __func__, ts->stat);
			return AC_ERR_OTHER;

		case SAM_STAT_CHECK_CONDITION:
		case SAS_ABORTED_TASK:
			return AC_ERR_DEV;

		case SAS_PROTO_RESPONSE:
			/* This means the ending_fis has the error
			 * value; return 0 here to collect it */
			return 0;
		default:
			return 0;
	}
}

static void sas_ata_task_done(struct sas_task *task)
{
	struct ata_queued_cmd *qc = task->uldd_task;
	struct domain_device *dev = task->dev;
	struct task_status_struct *stat = &task->task_status;
	struct ata_task_resp *resp = (struct ata_task_resp *)stat->buf;
	struct sas_ha_struct *sas_ha = dev->port->ha;
	enum ata_completion_errors ac;
	unsigned long flags;
	struct ata_link *link;
	struct ata_port *ap;

	spin_lock_irqsave(&dev->done_lock, flags);
	if (test_bit(SAS_HA_FROZEN, &sas_ha->state))
		task = NULL;
	else if (qc && qc->scsicmd)
		ASSIGN_SAS_TASK(qc->scsicmd, NULL);
	spin_unlock_irqrestore(&dev->done_lock, flags);

	/* check if libsas-eh got to the task before us */
	if (unlikely(!task))
		return;

	if (!qc)
		goto qc_already_gone;

	ap = qc->ap;
	link = &ap->link;

	spin_lock_irqsave(ap->lock, flags);
	/* check if we lost the race with libata/sas_ata_post_internal() */
	if (unlikely(ap->pflags & ATA_PFLAG_FROZEN)) {
		spin_unlock_irqrestore(ap->lock, flags);
		if (qc->scsicmd)
			goto qc_already_gone;
		else {
			/* if eh is not involved and the port is frozen then the
			 * ata internal abort process has taken responsibility
			 * for this sas_task
			 */
			return;
		}
	}

	if (stat->stat == SAS_PROTO_RESPONSE || stat->stat == SAM_STAT_GOOD ||
	    ((stat->stat == SAM_STAT_CHECK_CONDITION &&
	      dev->sata_dev.command_set == ATAPI_COMMAND_SET))) {
		ata_tf_from_fis(resp->ending_fis, &dev->sata_dev.tf);

		if (!link->sactive) {
			qc->err_mask |= ac_err_mask(dev->sata_dev.tf.command);
		} else {
			link->eh_info.err_mask |= ac_err_mask(dev->sata_dev.tf.command);
			if (unlikely(link->eh_info.err_mask))
				qc->flags |= ATA_QCFLAG_FAILED;
		}
	} else {
		ac = sas_to_ata_err(stat);
		if (ac) {
			SAS_DPRINTK("%s: SAS error %x\n", __func__,
				    stat->stat);
			/* We saw a SAS error. Send a vague error. */
			if (!link->sactive) {
				qc->err_mask = ac;
			} else {
				link->eh_info.err_mask |= AC_ERR_DEV;
				qc->flags |= ATA_QCFLAG_FAILED;
			}

			dev->sata_dev.tf.feature = 0x04; /* status err */
			dev->sata_dev.tf.command = ATA_ERR;
		}
	}

	qc->lldd_task = NULL;
	ata_qc_complete(qc);
	spin_unlock_irqrestore(ap->lock, flags);

qc_already_gone:
	list_del_init(&task->list);
	sas_free_task(task);
}

static unsigned int sas_ata_qc_issue(struct ata_queued_cmd *qc)
{
	unsigned long flags;
	struct sas_task *task;
	struct scatterlist *sg;
	int ret = AC_ERR_SYSTEM;
	unsigned int si, xfer = 0;
	struct ata_port *ap = qc->ap;
	struct domain_device *dev = ap->private_data;
	struct sas_ha_struct *sas_ha = dev->port->ha;
	struct Scsi_Host *host = sas_ha->core.shost;
	struct sas_internal *i = to_sas_internal(host->transportt);

	/* TODO: audit callers to ensure they are ready for qc_issue to
	 * unconditionally re-enable interrupts
	 */
	local_irq_save(flags);
	spin_unlock(ap->lock);

	/* If the device fell off, no sense in issuing commands */
	if (test_bit(SAS_DEV_GONE, &dev->state))
		goto out;

	task = sas_alloc_task(GFP_ATOMIC);
	if (!task)
		goto out;
	task->dev = dev;
	task->task_proto = SAS_PROTOCOL_STP;
	task->task_done = sas_ata_task_done;

	if (qc->tf.command == ATA_CMD_FPDMA_WRITE ||
	    qc->tf.command == ATA_CMD_FPDMA_READ) {
		/* Need to zero out the tag libata assigned us */
		qc->tf.nsect = 0;
	}

	ata_tf_to_fis(&qc->tf, 1, 0, (u8*)&task->ata_task.fis);
	task->uldd_task = qc;
	if (ata_is_atapi(qc->tf.protocol)) {
		memcpy(task->ata_task.atapi_packet, qc->cdb, qc->dev->cdb_len);
		task->total_xfer_len = qc->nbytes;
		task->num_scatter = qc->n_elem;
	} else {
		for_each_sg(qc->sg, sg, qc->n_elem, si)
			xfer += sg->length;

		task->total_xfer_len = xfer;
		task->num_scatter = si;
	}

	task->data_dir = qc->dma_dir;
	task->scatter = qc->sg;
	task->ata_task.retry_count = 1;
	task->task_state_flags = SAS_TASK_STATE_PENDING;
	qc->lldd_task = task;

	switch (qc->tf.protocol) {
	case ATA_PROT_NCQ:
		task->ata_task.use_ncq = 1;
		/* fall through */
	case ATAPI_PROT_DMA:
	case ATA_PROT_DMA:
		task->ata_task.dma_xfer = 1;
		break;
	}

	if (qc->scsicmd)
		ASSIGN_SAS_TASK(qc->scsicmd, task);

	if (sas_ha->lldd_max_execute_num < 2)
		ret = i->dft->lldd_execute_task(task, 1, GFP_ATOMIC);
	else
		ret = sas_queue_up(task);

	/* Examine */
	if (ret) {
		SAS_DPRINTK("lldd_execute_task returned: %d\n", ret);

		if (qc->scsicmd)
			ASSIGN_SAS_TASK(qc->scsicmd, NULL);
		sas_free_task(task);
		ret = AC_ERR_SYSTEM;
	}

 out:
	spin_lock(ap->lock);
	local_irq_restore(flags);
	return ret;
}

static bool sas_ata_qc_fill_rtf(struct ata_queued_cmd *qc)
{
	struct domain_device *dev = qc->ap->private_data;

	memcpy(&qc->result_tf, &dev->sata_dev.tf, sizeof(qc->result_tf));
	return true;
}

static int sas_ata_hard_reset(struct ata_link *link, unsigned int *class,
			       unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	struct domain_device *dev = ap->private_data;
	struct sas_internal *i =
		to_sas_internal(dev->port->ha->core.shost->transportt);
	int res = TMF_RESP_FUNC_FAILED;
	int ret = 0;

	if (i->dft->lldd_I_T_nexus_reset)
		res = i->dft->lldd_I_T_nexus_reset(dev);

	if (res != TMF_RESP_FUNC_COMPLETE) {
		SAS_DPRINTK("%s: Unable to reset I T nexus?\n", __func__);
		ret = -EAGAIN;
	}

	switch (dev->sata_dev.command_set) {
		case ATA_COMMAND_SET:
			SAS_DPRINTK("%s: Found ATA device.\n", __func__);
			*class = ATA_DEV_ATA;
			break;
		case ATAPI_COMMAND_SET:
			SAS_DPRINTK("%s: Found ATAPI device.\n", __func__);
			*class = ATA_DEV_ATAPI;
			break;
		default:
			SAS_DPRINTK("%s: Unknown SATA command set: %d.\n",
				    __func__,
				    dev->sata_dev.command_set);
			*class = ATA_DEV_UNKNOWN;
			break;
	}

	ap->cbl = ATA_CBL_SATA;
	return ret;
}

static int sas_ata_soft_reset(struct ata_link *link, unsigned int *class,
			       unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	struct domain_device *dev = ap->private_data;
	struct sas_internal *i =
		to_sas_internal(dev->port->ha->core.shost->transportt);
	int res = TMF_RESP_FUNC_FAILED;
	int ret = 0;

	if (i->dft->lldd_ata_soft_reset)
		res = i->dft->lldd_ata_soft_reset(dev);

	if (res != TMF_RESP_FUNC_COMPLETE) {
		SAS_DPRINTK("%s: Unable to soft reset\n", __func__);
		ret = -EAGAIN;
	}

	switch (dev->sata_dev.command_set) {
	case ATA_COMMAND_SET:
		SAS_DPRINTK("%s: Found ATA device.\n", __func__);
		*class = ATA_DEV_ATA;
		break;
	case ATAPI_COMMAND_SET:
		SAS_DPRINTK("%s: Found ATAPI device.\n", __func__);
		*class = ATA_DEV_ATAPI;
		break;
	default:
		SAS_DPRINTK("%s: Unknown SATA command set: %d.\n",
			    __func__, dev->sata_dev.command_set);
		*class = ATA_DEV_UNKNOWN;
		break;
	}

	ap->cbl = ATA_CBL_SATA;
	return ret;
}

/*
 * notify the lldd to forget the sas_task for this internal ata command
 * that bypasses scsi-eh
 */
static void sas_ata_internal_abort(struct sas_task *task)
{
	struct sas_internal *si =
		to_sas_internal(task->dev->port->ha->core.shost->transportt);
	unsigned long flags;
	int res;

	spin_lock_irqsave(&task->task_state_lock, flags);
	if (task->task_state_flags & SAS_TASK_STATE_ABORTED ||
	    task->task_state_flags & SAS_TASK_STATE_DONE) {
		spin_unlock_irqrestore(&task->task_state_lock, flags);
		SAS_DPRINTK("%s: Task %p already finished.\n", __func__,
			    task);
		goto out;
	}
	task->task_state_flags |= SAS_TASK_STATE_ABORTED;
	spin_unlock_irqrestore(&task->task_state_lock, flags);

	res = si->dft->lldd_abort_task(task);

	spin_lock_irqsave(&task->task_state_lock, flags);
	if (task->task_state_flags & SAS_TASK_STATE_DONE ||
	    res == TMF_RESP_FUNC_COMPLETE) {
		spin_unlock_irqrestore(&task->task_state_lock, flags);
		goto out;
	}

	/* XXX we are not prepared to deal with ->lldd_abort_task()
	 * failures.  TODO: lldds need to unconditionally forget about
	 * aborted ata tasks, otherwise we (likely) leak the sas task
	 * here
	 */
	SAS_DPRINTK("%s: Task %p leaked.\n", __func__, task);

	if (!(task->task_state_flags & SAS_TASK_STATE_DONE))
		task->task_state_flags &= ~SAS_TASK_STATE_ABORTED;
	spin_unlock_irqrestore(&task->task_state_lock, flags);

	return;
 out:
	list_del_init(&task->list);
	sas_free_task(task);
}

static void sas_ata_post_internal(struct ata_queued_cmd *qc)
{
	if (qc->flags & ATA_QCFLAG_FAILED)
		qc->err_mask |= AC_ERR_OTHER;

	if (qc->err_mask) {
		/*
		 * Find the sas_task and kill it.  By this point, libata
		 * has decided to kill the qc and has frozen the port.
		 * In this state sas_ata_task_done() will no longer free
		 * the sas_task, so we need to notify the lldd (via
		 * ->lldd_abort_task) that the task is dead and free it
		 *  ourselves.
		 */
		struct sas_task *task = qc->lldd_task;
		unsigned long flags;

		qc->lldd_task = NULL;
		if (task) {
			/* Should this be a AT(API) device reset? */
			spin_lock_irqsave(&task->task_state_lock, flags);
			task->task_state_flags |= SAS_TASK_NEED_DEV_RESET;
			spin_unlock_irqrestore(&task->task_state_lock, flags);

			task->uldd_task = NULL;
			sas_ata_internal_abort(task);
		}
	}
}


static void sas_ata_set_dmamode(struct ata_port *ap, struct ata_device *ata_dev)
{
	struct domain_device *dev = ap->private_data;
	struct sas_internal *i =
		to_sas_internal(dev->port->ha->core.shost->transportt);

	if (i->dft->lldd_ata_set_dmamode)
		i->dft->lldd_ata_set_dmamode(dev);
}

static struct ata_port_operations sas_sata_ops = {
	.prereset		= ata_std_prereset,
	.softreset		= sas_ata_soft_reset,
	.hardreset		= sas_ata_hard_reset,
	.postreset		= ata_std_postreset,
	.error_handler		= ata_std_error_handler,
	.post_internal_cmd	= sas_ata_post_internal,
	.qc_defer               = ata_std_qc_defer,
	.qc_prep		= ata_noop_qc_prep,
	.qc_issue		= sas_ata_qc_issue,
	.qc_fill_rtf		= sas_ata_qc_fill_rtf,
	.port_start		= ata_sas_port_start,
	.port_stop		= ata_sas_port_stop,
	.set_dmamode		= sas_ata_set_dmamode,
};

static struct ata_port_info sata_port_info = {
	.flags = ATA_FLAG_SATA | ATA_FLAG_PIO_DMA | ATA_FLAG_NCQ,
	.pio_mask = ATA_PIO4,
	.mwdma_mask = ATA_MWDMA2,
	.udma_mask = ATA_UDMA6,
	.port_ops = &sas_sata_ops
};

int sas_ata_init_host_and_port(struct domain_device *found_dev,
			       struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(&starget->dev);
	struct sas_ha_struct *ha = SHOST_TO_SAS_HA(shost);
	struct ata_port *ap;

	ata_host_init(&found_dev->sata_dev.ata_host,
		      ha->dev,
		      sata_port_info.flags,
		      &sas_sata_ops);
	ap = ata_sas_port_alloc(&found_dev->sata_dev.ata_host,
				&sata_port_info,
				shost);
	if (!ap) {
		SAS_DPRINTK("ata_sas_port_alloc failed.\n");
		return -ENODEV;
	}

	ap->private_data = found_dev;
	ap->cbl = ATA_CBL_SATA;
	ap->scsi_host = shost;
	found_dev->sata_dev.ap = ap;

	return 0;
}

void sas_ata_task_abort(struct sas_task *task)
{
	struct ata_queued_cmd *qc = task->uldd_task;
	struct completion *waiting;

	/* Bounce SCSI-initiated commands to the SCSI EH */
	if (qc->scsicmd) {
		struct request_queue *q = qc->scsicmd->device->request_queue;
		unsigned long flags;

		spin_lock_irqsave(q->queue_lock, flags);
		blk_abort_request(qc->scsicmd->request);
		spin_unlock_irqrestore(q->queue_lock, flags);
		scsi_schedule_eh(qc->scsicmd->device->host);
		return;
	}

	/* Internal command, fake a timeout and complete. */
	qc->flags &= ~ATA_QCFLAG_ACTIVE;
	qc->flags |= ATA_QCFLAG_FAILED;
	qc->err_mask |= AC_ERR_TIMEOUT;
	waiting = qc->private_data;
	complete(waiting);
}

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

void sas_probe_sata(struct work_struct *work)
{
	struct domain_device *dev, *n;
	struct sas_discovery_event *ev =
		container_of(work, struct sas_discovery_event, work);
	struct asd_sas_port *port = ev->port;

	clear_bit(DISCE_PROBE, &port->disc.pending);

	list_for_each_entry_safe(dev, n, &port->disco_list, disco_list_node) {
		int err;

		spin_lock_irq(&port->dev_list_lock);
		list_add_tail(&dev->dev_list_node, &port->dev_list);
		spin_unlock_irq(&port->dev_list_lock);

		err = sas_rphy_add(dev->rphy);

		if (err) {
			SAS_DPRINTK("%s: for %s device %16llx returned %d\n",
				    __func__, dev->parent ? "exp-attached" :
							    "direct-attached",
				    SAS_ADDR(dev->sas_addr), err);
			sas_unregister_dev(port, dev);
		} else
			list_del_init(&dev->disco_list_node);
	}
}

/**
 * sas_discover_sata -- discover an STP/SATA domain device
 * @dev: pointer to struct domain_device of interest
 *
 * Devices directly attached to a HA port, have no parents.  All other
 * devices do, and should have their "parent" pointer set appropriately
 * before calling this function.
 */
int sas_discover_sata(struct domain_device *dev)
{
	int res;

	if (dev->dev_type == SATA_PM)
		return -ENODEV;

	sas_get_ata_command_set(dev);
	sas_fill_in_rphy(dev, dev->rphy);

	res = sas_notify_lldd_dev_found(dev);
	if (res)
		return res;

	sas_discover_event(dev->port, DISCE_PROBE);
	return 0;
}

void sas_ata_strategy_handler(struct Scsi_Host *shost)
{
	struct scsi_device *sdev;
	struct sas_ha_struct *sas_ha = SHOST_TO_SAS_HA(shost);

	/* it's ok to defer revalidation events during ata eh, these
	 * disks are in one of three states:
	 * 1/ present for initial domain discovery, and these
	 *    resets will cause bcn flutters
	 * 2/ hot removed, we'll discover that after eh fails
	 * 3/ hot added after initial discovery, lost the race, and need
	 *    to catch the next train.
	 */
	sas_disable_revalidation(sas_ha);

	shost_for_each_device(sdev, shost) {
		struct domain_device *ddev = sdev_to_domain_dev(sdev);
		struct ata_port *ap = ddev->sata_dev.ap;

		if (!dev_is_sata(ddev))
			continue;

		ata_port_printk(ap, KERN_DEBUG, "sas eh calling libata port error handler");
		ata_scsi_port_error_handler(shost, ap);
	}

	sas_enable_revalidation(sas_ha);
}

int sas_ata_eh(struct Scsi_Host *shost, struct list_head *work_q,
	       struct list_head *done_q)
{
	int rtn = 0;
	struct scsi_cmnd *cmd, *n;
	struct ata_port *ap;

	do {
		LIST_HEAD(sata_q);

		ap = NULL;

		list_for_each_entry_safe(cmd, n, work_q, eh_entry) {
			struct domain_device *ddev = cmd_to_domain_dev(cmd);

			if (!dev_is_sata(ddev) || TO_SAS_TASK(cmd))
				continue;
			if (ap && ap != ddev->sata_dev.ap)
				continue;
			ap = ddev->sata_dev.ap;
			rtn = 1;
			list_move(&cmd->eh_entry, &sata_q);
		}

		if (!list_empty(&sata_q)) {
			ata_port_printk(ap, KERN_DEBUG, "sas eh calling libata cmd error handler\n");
			ata_scsi_cmd_error_handler(shost, ap, &sata_q);
			/*
			 * ata's error handler may leave the cmd on the list
			 * so make sure they don't remain on a stack list
			 * about to go out of scope.
			 *
			 * This looks strange, since the commands are
			 * now part of no list, but the next error
			 * action will be ata_port_error_handler()
			 * which takes no list and sweeps them up
			 * anyway from the ata tag array.
			 */
			while (!list_empty(&sata_q))
				list_del_init(sata_q.next);
		}
	} while (ap);

	return rtn;
}
