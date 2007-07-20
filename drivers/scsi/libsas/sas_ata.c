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

		case SAM_CHECK_COND:
		case SAS_OPEN_TO:
		case SAS_OPEN_REJECT:
			SAS_DPRINTK("%s: Saw error %d.  What to do?\n",
				    __FUNCTION__, ts->stat);
			return AC_ERR_OTHER;

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
	struct domain_device *dev;
	struct task_status_struct *stat = &task->task_status;
	struct ata_task_resp *resp = (struct ata_task_resp *)stat->buf;
	struct sas_ha_struct *sas_ha;
	enum ata_completion_errors ac;
	unsigned long flags;

	if (!qc)
		goto qc_already_gone;

	dev = qc->ap->private_data;
	sas_ha = dev->port->ha;

	spin_lock_irqsave(dev->sata_dev.ap->lock, flags);
	if (stat->stat == SAS_PROTO_RESPONSE || stat->stat == SAM_GOOD) {
		ata_tf_from_fis(resp->ending_fis, &dev->sata_dev.tf);
		qc->err_mask |= ac_err_mask(dev->sata_dev.tf.command);
		dev->sata_dev.sstatus = resp->sstatus;
		dev->sata_dev.serror = resp->serror;
		dev->sata_dev.scontrol = resp->scontrol;
	} else if (stat->stat != SAM_STAT_GOOD) {
		ac = sas_to_ata_err(stat);
		if (ac) {
			SAS_DPRINTK("%s: SAS error %x\n", __FUNCTION__,
				    stat->stat);
			/* We saw a SAS error. Send a vague error. */
			qc->err_mask = ac;
			dev->sata_dev.tf.feature = 0x04; /* status err */
			dev->sata_dev.tf.command = ATA_ERR;
		}
	}

	qc->lldd_task = NULL;
	if (qc->scsicmd)
		ASSIGN_SAS_TASK(qc->scsicmd, NULL);
	ata_qc_complete(qc);
	spin_unlock_irqrestore(dev->sata_dev.ap->lock, flags);

	/*
	 * If the sas_task has an ata qc, a scsi_cmnd and the aborted
	 * flag is set, then we must have come in via the libsas EH
	 * functions.  When we exit this function, we need to put the
	 * scsi_cmnd on the list of finished errors.  The ata_qc_complete
	 * call cleans up the libata side of things but we're protected
	 * from the scsi_cmnd going away because the scsi_cmnd is owned
	 * by the EH, making libata's call to scsi_done a NOP.
	 */
	spin_lock_irqsave(&task->task_state_lock, flags);
	if (qc->scsicmd && task->task_state_flags & SAS_TASK_STATE_ABORTED)
		scsi_eh_finish_cmd(qc->scsicmd, &sas_ha->eh_done_q);
	spin_unlock_irqrestore(&task->task_state_lock, flags);

qc_already_gone:
	list_del_init(&task->list);
	sas_free_task(task);
}

static unsigned int sas_ata_qc_issue(struct ata_queued_cmd *qc)
{
	int res;
	struct sas_task *task;
	struct domain_device *dev = qc->ap->private_data;
	struct sas_ha_struct *sas_ha = dev->port->ha;
	struct Scsi_Host *host = sas_ha->core.shost;
	struct sas_internal *i = to_sas_internal(host->transportt);
	struct scatterlist *sg;
	unsigned int num = 0;
	unsigned int xfer = 0;

	task = sas_alloc_task(GFP_ATOMIC);
	if (!task)
		return AC_ERR_SYSTEM;
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
	if (is_atapi_taskfile(&qc->tf)) {
		memcpy(task->ata_task.atapi_packet, qc->cdb, qc->dev->cdb_len);
		task->total_xfer_len = qc->nbytes + qc->pad_len;
		task->num_scatter = qc->pad_len ? qc->n_elem + 1 : qc->n_elem;
	} else {
		ata_for_each_sg(sg, qc) {
			num++;
			xfer += sg->length;
		}

		task->total_xfer_len = xfer;
		task->num_scatter = num;
	}

	task->data_dir = qc->dma_dir;
	task->scatter = qc->__sg;
	task->ata_task.retry_count = 1;
	task->task_state_flags = SAS_TASK_STATE_PENDING;
	qc->lldd_task = task;

	switch (qc->tf.protocol) {
	case ATA_PROT_NCQ:
		task->ata_task.use_ncq = 1;
		/* fall through */
	case ATA_PROT_ATAPI_DMA:
	case ATA_PROT_DMA:
		task->ata_task.dma_xfer = 1;
		break;
	}

	if (qc->scsicmd)
		ASSIGN_SAS_TASK(qc->scsicmd, task);

	if (sas_ha->lldd_max_execute_num < 2)
		res = i->dft->lldd_execute_task(task, 1, GFP_ATOMIC);
	else
		res = sas_queue_up(task);

	/* Examine */
	if (res) {
		SAS_DPRINTK("lldd_execute_task returned: %d\n", res);

		if (qc->scsicmd)
			ASSIGN_SAS_TASK(qc->scsicmd, NULL);
		sas_free_task(task);
		return AC_ERR_SYSTEM;
	}

	return 0;
}

static u8 sas_ata_check_status(struct ata_port *ap)
{
	struct domain_device *dev = ap->private_data;
	return dev->sata_dev.tf.command;
}

static void sas_ata_phy_reset(struct ata_port *ap)
{
	struct domain_device *dev = ap->private_data;
	struct sas_internal *i =
		to_sas_internal(dev->port->ha->core.shost->transportt);
	int res = 0;

	if (i->dft->lldd_I_T_nexus_reset)
		res = i->dft->lldd_I_T_nexus_reset(dev);

	if (res)
		SAS_DPRINTK("%s: Unable to reset I T nexus?\n", __FUNCTION__);

	switch (dev->sata_dev.command_set) {
		case ATA_COMMAND_SET:
			SAS_DPRINTK("%s: Found ATA device.\n", __FUNCTION__);
			ap->device[0].class = ATA_DEV_ATA;
			break;
		case ATAPI_COMMAND_SET:
			SAS_DPRINTK("%s: Found ATAPI device.\n", __FUNCTION__);
			ap->device[0].class = ATA_DEV_ATAPI;
			break;
		default:
			SAS_DPRINTK("%s: Unknown SATA command set: %d.\n",
				    __FUNCTION__,
				    dev->sata_dev.command_set);
			ap->device[0].class = ATA_DEV_UNKNOWN;
			break;
	}

	ap->cbl = ATA_CBL_SATA;
}

static void sas_ata_post_internal(struct ata_queued_cmd *qc)
{
	if (qc->flags & ATA_QCFLAG_FAILED)
		qc->err_mask |= AC_ERR_OTHER;

	if (qc->err_mask) {
		/*
		 * Find the sas_task and kill it.  By this point,
		 * libata has decided to kill the qc, so we needn't
		 * bother with sas_ata_task_done.  But we still
		 * ought to abort the task.
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
			__sas_task_abort(task);
		}
	}
}

static void sas_ata_tf_read(struct ata_port *ap, struct ata_taskfile *tf)
{
	struct domain_device *dev = ap->private_data;
	memcpy(tf, &dev->sata_dev.tf, sizeof (*tf));
}

static int sas_ata_scr_write(struct ata_port *ap, unsigned int sc_reg_in,
			      u32 val)
{
	struct domain_device *dev = ap->private_data;

	SAS_DPRINTK("STUB %s\n", __FUNCTION__);
	switch (sc_reg_in) {
		case SCR_STATUS:
			dev->sata_dev.sstatus = val;
			break;
		case SCR_CONTROL:
			dev->sata_dev.scontrol = val;
			break;
		case SCR_ERROR:
			dev->sata_dev.serror = val;
			break;
		case SCR_ACTIVE:
			dev->sata_dev.ap->sactive = val;
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

static int sas_ata_scr_read(struct ata_port *ap, unsigned int sc_reg_in,
			    u32 *val)
{
	struct domain_device *dev = ap->private_data;

	SAS_DPRINTK("STUB %s\n", __FUNCTION__);
	switch (sc_reg_in) {
		case SCR_STATUS:
			*val = dev->sata_dev.sstatus;
			return 0;
		case SCR_CONTROL:
			*val = dev->sata_dev.scontrol;
			return 0;
		case SCR_ERROR:
			*val = dev->sata_dev.serror;
			return 0;
		case SCR_ACTIVE:
			*val = dev->sata_dev.ap->sactive;
			return 0;
		default:
			return -EINVAL;
	}
}

static struct ata_port_operations sas_sata_ops = {
	.port_disable		= ata_port_disable,
	.check_status		= sas_ata_check_status,
	.check_altstatus	= sas_ata_check_status,
	.dev_select		= ata_noop_dev_select,
	.phy_reset		= sas_ata_phy_reset,
	.post_internal_cmd	= sas_ata_post_internal,
	.tf_read		= sas_ata_tf_read,
	.qc_prep		= ata_noop_qc_prep,
	.qc_issue		= sas_ata_qc_issue,
	.port_start		= ata_sas_port_start,
	.port_stop		= ata_sas_port_stop,
	.scr_read		= sas_ata_scr_read,
	.scr_write		= sas_ata_scr_write
};

static struct ata_port_info sata_port_info = {
	.flags = ATA_FLAG_SATA | ATA_FLAG_NO_LEGACY | ATA_FLAG_SATA_RESET |
		ATA_FLAG_MMIO | ATA_FLAG_PIO_DMA | ATA_FLAG_NCQ,
	.pio_mask = 0x1f, /* PIO0-4 */
	.mwdma_mask = 0x07, /* MWDMA0-2 */
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
		      &ha->pcidev->dev,
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
		scsi_req_abort_cmd(qc->scsicmd);
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
