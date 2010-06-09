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

		case SAM_CHECK_COND:
		case SAS_OPEN_TO:
		case SAS_OPEN_REJECT:
			SAS_DPRINTK("%s: Saw error %d.  What to do?\n",
				    __func__, ts->stat);
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
			SAS_DPRINTK("%s: SAS error %x\n", __func__,
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
	unsigned int xfer = 0;
	unsigned int si;

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

static bool sas_ata_qc_fill_rtf(struct ata_queued_cmd *qc)
{
	struct domain_device *dev = qc->ap->private_data;

	memcpy(&qc->result_tf, &dev->sata_dev.tf, sizeof(qc->result_tf));
	return true;
}

static void sas_ata_phy_reset(struct ata_port *ap)
{
	struct domain_device *dev = ap->private_data;
	struct sas_internal *i =
		to_sas_internal(dev->port->ha->core.shost->transportt);
	int res = TMF_RESP_FUNC_FAILED;

	if (i->dft->lldd_I_T_nexus_reset)
		res = i->dft->lldd_I_T_nexus_reset(dev);

	if (res != TMF_RESP_FUNC_COMPLETE)
		SAS_DPRINTK("%s: Unable to reset I T nexus?\n", __func__);

	switch (dev->sata_dev.command_set) {
		case ATA_COMMAND_SET:
			SAS_DPRINTK("%s: Found ATA device.\n", __func__);
			ap->link.device[0].class = ATA_DEV_ATA;
			break;
		case ATAPI_COMMAND_SET:
			SAS_DPRINTK("%s: Found ATAPI device.\n", __func__);
			ap->link.device[0].class = ATA_DEV_ATAPI;
			break;
		default:
			SAS_DPRINTK("%s: Unknown SATA command set: %d.\n",
				    __func__,
				    dev->sata_dev.command_set);
			ap->link.device[0].class = ATA_DEV_UNKNOWN;
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

static int sas_ata_scr_write(struct ata_link *link, unsigned int sc_reg_in,
			      u32 val)
{
	struct domain_device *dev = link->ap->private_data;

	SAS_DPRINTK("STUB %s\n", __func__);
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
			dev->sata_dev.ap->link.sactive = val;
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

static int sas_ata_scr_read(struct ata_link *link, unsigned int sc_reg_in,
			    u32 *val)
{
	struct domain_device *dev = link->ap->private_data;

	SAS_DPRINTK("STUB %s\n", __func__);
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
			*val = dev->sata_dev.ap->link.sactive;
			return 0;
		default:
			return -EINVAL;
	}
}

static struct ata_port_operations sas_sata_ops = {
	.phy_reset		= sas_ata_phy_reset,
	.post_internal_cmd	= sas_ata_post_internal,
	.qc_prep		= ata_noop_qc_prep,
	.qc_issue		= sas_ata_qc_issue,
	.qc_fill_rtf		= sas_ata_qc_fill_rtf,
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
	struct request_queue *q = qc->scsicmd->device->request_queue;
	struct completion *waiting;
	unsigned long flags;

	/* Bounce SCSI-initiated commands to the SCSI EH */
	if (qc->scsicmd) {
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
 * @dma_dir: DMA direction.  DMA_xxx
 */
static int sas_execute_task(struct sas_task *task, void *buffer, int size,
			    enum dma_data_direction dma_dir)
{
	int res = 0;
	struct scatterlist *scatter = NULL;
	struct task_status_struct *ts = &task->task_status;
	int num_scatter = 0;
	int retries = 0;
	struct sas_internal *i =
		to_sas_internal(task->dev->port->ha->core.shost->transportt);

	if (dma_dir != DMA_NONE) {
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
	task->data_dir = dma_dir;
	task->task_done = sas_disc_task_done;
	if (dma_dir != DMA_NONE &&
	    sas_protocol_ata(task->task_proto)) {
		task->num_scatter = dma_map_sg(task->dev->port->ha->dev,
					       task->scatter,
					       task->num_scatter,
					       task->data_dir);
	}

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
		res = -ECOMM;
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
	if (dma_dir != DMA_NONE) {
		if (sas_protocol_ata(task->task_proto))
			dma_unmap_sg(task->dev->port->ha->dev,
				     task->scatter, task->num_scatter,
				     task->data_dir);
		kfree(scatter);
	}
out:
	return res;
}

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
 * @dma_dir: DMA direction.  DMA_xxx
 */
static int sas_issue_ata_cmd(struct domain_device *dev, u8 command,
			     u8 features, void *buffer, int size,
			     enum dma_data_direction dma_dir)
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

	task->ata_task.fis.fis_type = 0x27;
	task->ata_task.fis.command = command;
	task->ata_task.fis.features = features;
	task->ata_task.fis.device = d2h_fis->device;
	task->ata_task.retry_count = 1;

	res = sas_execute_task(task, buffer, size, dma_dir);

	sas_free_task(task);
out:
	return res;
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
				DMA_FROM_DEVICE);
	if (res)
		goto out_err;

	/* lives on the media? */
	if (le16_to_cpu(identify_x[0]) & 4) {
		/* incomplete response */
		SAS_DPRINTK("sending SET FEATURE/PUP_STBY_SPIN_UP to "
			    "dev %llx\n", SAS_ADDR(dev->sas_addr));
		if (!(identify_x[83] & cpu_to_le16(1<<6)))
			goto cont1;
		res = sas_issue_ata_cmd(dev, ATA_SET_FEATURES,
					ATA_FEATURE_PUP_STBY_SPIN_UP,
					NULL, 0, DMA_NONE);
		if (res)
			goto cont1;

		schedule_timeout_interruptible(5*HZ); /* More time? */
		res = sas_issue_ata_cmd(dev, command, 0, identify_x, 512,
					DMA_FROM_DEVICE);
		if (res)
			goto out_err;
	}
cont1:
	/* XXX Hint: register this SATA device with SATL.
	   When this returns, dev->sata_dev->lu is alive and
	   present.
	sas_satl_register_dev(dev);
	*/

	sas_fill_in_rphy(dev, dev->rphy);

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
		res = sas_rphy_add(dev->rphy);
	}

	return res;
}
