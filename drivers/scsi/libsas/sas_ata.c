// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Support for SATA devices on Serial Attached SCSI (SAS) controllers
 *
 * Copyright (C) 2006 IBM Corporation
 *
 * Written by: Darrick J. Wong <djwong@us.ibm.com>, IBM Corporation
 */

#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/async.h>
#include <linux/export.h>

#include <scsi/sas_ata.h>
#include "sas_internal.h"
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_sas.h>
#include "scsi_sas_internal.h"
#include "scsi_transport_api.h"
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
		pr_warn("%s: Saw error %d.  What to do?\n",
			__func__, ts->stat);
		return AC_ERR_OTHER;
	case SAM_STAT_CHECK_CONDITION:
	case SAS_ABORTED_TASK:
		return AC_ERR_DEV;
	case SAS_PROTO_RESPONSE:
		/* This means the ending_fis has the error
		 * value; return 0 here to collect it
		 */
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

	if (stat->stat == SAS_PROTO_RESPONSE ||
	    stat->stat == SAS_SAM_STAT_GOOD ||
	    (stat->stat == SAS_SAM_STAT_CHECK_CONDITION &&
	      dev->sata_dev.class == ATA_DEV_ATAPI)) {
		memcpy(dev->sata_dev.fis, resp->ending_fis, ATA_RESP_FIS_SIZE);

		if (!link->sactive) {
			qc->err_mask |= ac_err_mask(dev->sata_dev.fis[2]);
		} else {
			link->eh_info.err_mask |= ac_err_mask(dev->sata_dev.fis[2]);
			if (unlikely(link->eh_info.err_mask))
				qc->flags |= ATA_QCFLAG_FAILED;
		}
	} else {
		ac = sas_to_ata_err(stat);
		if (ac) {
			pr_warn("%s: SAS error 0x%x\n", __func__, stat->stat);
			/* We saw a SAS error. Send a vague error. */
			if (!link->sactive) {
				qc->err_mask = ac;
			} else {
				link->eh_info.err_mask |= AC_ERR_DEV;
				qc->flags |= ATA_QCFLAG_FAILED;
			}

			dev->sata_dev.fis[3] = 0x04; /* status err */
			dev->sata_dev.fis[2] = ATA_ERR;
		}
	}

	qc->lldd_task = NULL;
	ata_qc_complete(qc);
	spin_unlock_irqrestore(ap->lock, flags);

qc_already_gone:
	sas_free_task(task);
}

static unsigned int sas_ata_qc_issue(struct ata_queued_cmd *qc)
	__must_hold(ap->lock)
{
	struct sas_task *task;
	struct scatterlist *sg;
	int ret = AC_ERR_SYSTEM;
	unsigned int si, xfer = 0;
	struct ata_port *ap = qc->ap;
	struct domain_device *dev = ap->private_data;
	struct sas_ha_struct *sas_ha = dev->port->ha;
	struct Scsi_Host *host = sas_ha->core.shost;
	struct sas_internal *i = to_sas_internal(host->transportt);

	/* TODO: we should try to remove that unlock */
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
	    qc->tf.command == ATA_CMD_FPDMA_READ ||
	    qc->tf.command == ATA_CMD_FPDMA_RECV ||
	    qc->tf.command == ATA_CMD_FPDMA_SEND ||
	    qc->tf.command == ATA_CMD_NCQ_NON_DATA) {
		/* Need to zero out the tag libata assigned us */
		qc->tf.nsect = 0;
	}

	ata_tf_to_fis(&qc->tf, qc->dev->link->pmp, 1, (u8 *)&task->ata_task.fis);
	task->uldd_task = qc;
	if (ata_is_atapi(qc->tf.protocol)) {
		memcpy(task->ata_task.atapi_packet, qc->cdb, qc->dev->cdb_len);
		task->total_xfer_len = qc->nbytes;
		task->num_scatter = qc->n_elem;
		task->data_dir = qc->dma_dir;
	} else if (qc->tf.protocol == ATA_PROT_NODATA) {
		task->data_dir = DMA_NONE;
	} else {
		for_each_sg(qc->sg, sg, qc->n_elem, si)
			xfer += sg_dma_len(sg);

		task->total_xfer_len = xfer;
		task->num_scatter = si;
		task->data_dir = qc->dma_dir;
	}
	task->scatter = qc->sg;
	task->ata_task.retry_count = 1;
	qc->lldd_task = task;

	task->ata_task.use_ncq = ata_is_ncq(qc->tf.protocol);
	task->ata_task.dma_xfer = ata_is_dma(qc->tf.protocol);

	if (qc->scsicmd)
		ASSIGN_SAS_TASK(qc->scsicmd, task);

	ret = i->dft->lldd_execute_task(task, GFP_ATOMIC);
	if (ret) {
		pr_debug("lldd_execute_task returned: %d\n", ret);

		if (qc->scsicmd)
			ASSIGN_SAS_TASK(qc->scsicmd, NULL);
		sas_free_task(task);
		qc->lldd_task = NULL;
		ret = AC_ERR_SYSTEM;
	}

 out:
	spin_lock(ap->lock);
	return ret;
}

static bool sas_ata_qc_fill_rtf(struct ata_queued_cmd *qc)
{
	struct domain_device *dev = qc->ap->private_data;

	ata_tf_from_fis(dev->sata_dev.fis, &qc->result_tf);
	return true;
}

static struct sas_internal *dev_to_sas_internal(struct domain_device *dev)
{
	return to_sas_internal(dev->port->ha->core.shost->transportt);
}

static int sas_get_ata_command_set(struct domain_device *dev);

int sas_get_ata_info(struct domain_device *dev, struct ex_phy *phy)
{
	if (phy->attached_tproto & SAS_PROTOCOL_STP)
		dev->tproto = phy->attached_tproto;
	if (phy->attached_sata_dev)
		dev->tproto |= SAS_SATA_DEV;

	if (phy->attached_dev_type == SAS_SATA_PENDING)
		dev->dev_type = SAS_SATA_PENDING;
	else {
		int res;

		dev->dev_type = SAS_SATA_DEV;
		res = sas_get_report_phy_sata(dev->parent, phy->phy_id,
					      &dev->sata_dev.rps_resp);
		if (res) {
			pr_debug("report phy sata to %016llx:%02d returned 0x%x\n",
				 SAS_ADDR(dev->parent->sas_addr),
				 phy->phy_id, res);
			return res;
		}
		memcpy(dev->frame_rcvd, &dev->sata_dev.rps_resp.rps.fis,
		       sizeof(struct dev_to_host_fis));
		dev->sata_dev.class = sas_get_ata_command_set(dev);
	}
	return 0;
}

static int sas_ata_clear_pending(struct domain_device *dev, struct ex_phy *phy)
{
	int res;

	/* we weren't pending, so successfully end the reset sequence now */
	if (dev->dev_type != SAS_SATA_PENDING)
		return 1;

	/* hmmm, if this succeeds do we need to repost the domain_device to the
	 * lldd so it can pick up new parameters?
	 */
	res = sas_get_ata_info(dev, phy);
	if (res)
		return 0; /* retry */
	else
		return 1;
}

static int smp_ata_check_ready(struct ata_link *link)
{
	int res;
	struct ata_port *ap = link->ap;
	struct domain_device *dev = ap->private_data;
	struct domain_device *ex_dev = dev->parent;
	struct sas_phy *phy = sas_get_local_phy(dev);
	struct ex_phy *ex_phy = &ex_dev->ex_dev.ex_phy[phy->number];

	res = sas_ex_phy_discover(ex_dev, phy->number);
	sas_put_local_phy(phy);

	/* break the wait early if the expander is unreachable,
	 * otherwise keep polling
	 */
	if (res == -ECOMM)
		return res;
	if (res != SMP_RESP_FUNC_ACC)
		return 0;

	switch (ex_phy->attached_dev_type) {
	case SAS_SATA_PENDING:
		return 0;
	case SAS_END_DEVICE:
		if (ex_phy->attached_sata_dev)
			return sas_ata_clear_pending(dev, ex_phy);
		fallthrough;
	default:
		return -ENODEV;
	}
}

static int local_ata_check_ready(struct ata_link *link)
{
	struct ata_port *ap = link->ap;
	struct domain_device *dev = ap->private_data;
	struct sas_internal *i = dev_to_sas_internal(dev);

	if (i->dft->lldd_ata_check_ready)
		return i->dft->lldd_ata_check_ready(dev);
	else {
		/* lldd's that don't implement 'ready' checking get the
		 * old default behavior of not coordinating reset
		 * recovery with libata
		 */
		return 1;
	}
}

static int sas_ata_printk(const char *level, const struct domain_device *ddev,
			  const char *fmt, ...)
{
	struct ata_port *ap = ddev->sata_dev.ap;
	struct device *dev = &ddev->rphy->dev;
	struct va_format vaf;
	va_list args;
	int r;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	r = printk("%s" SAS_FMT "ata%u: %s: %pV",
		   level, ap->print_id, dev_name(dev), &vaf);

	va_end(args);

	return r;
}

static int sas_ata_hard_reset(struct ata_link *link, unsigned int *class,
			      unsigned long deadline)
{
	int ret = 0, res;
	struct sas_phy *phy;
	struct ata_port *ap = link->ap;
	int (*check_ready)(struct ata_link *link);
	struct domain_device *dev = ap->private_data;
	struct sas_internal *i = dev_to_sas_internal(dev);

	res = i->dft->lldd_I_T_nexus_reset(dev);
	if (res == -ENODEV)
		return res;

	if (res != TMF_RESP_FUNC_COMPLETE)
		sas_ata_printk(KERN_DEBUG, dev, "Unable to reset ata device?\n");

	phy = sas_get_local_phy(dev);
	if (scsi_is_sas_phy_local(phy))
		check_ready = local_ata_check_ready;
	else
		check_ready = smp_ata_check_ready;
	sas_put_local_phy(phy);

	ret = ata_wait_after_reset(link, deadline, check_ready);
	if (ret && ret != -EAGAIN)
		sas_ata_printk(KERN_ERR, dev, "reset failed (errno=%d)\n", ret);

	*class = dev->sata_dev.class;

	ap->cbl = ATA_CBL_SATA;
	return ret;
}

/*
 * notify the lldd to forget the sas_task for this internal ata command
 * that bypasses scsi-eh
 */
static void sas_ata_internal_abort(struct sas_task *task)
{
	struct sas_internal *si = dev_to_sas_internal(task->dev);
	unsigned long flags;
	int res;

	spin_lock_irqsave(&task->task_state_lock, flags);
	if (task->task_state_flags & SAS_TASK_STATE_ABORTED ||
	    task->task_state_flags & SAS_TASK_STATE_DONE) {
		spin_unlock_irqrestore(&task->task_state_lock, flags);
		pr_debug("%s: Task %p already finished.\n", __func__, task);
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
	pr_warn("%s: Task %p leaked.\n", __func__, task);

	if (!(task->task_state_flags & SAS_TASK_STATE_DONE))
		task->task_state_flags &= ~SAS_TASK_STATE_ABORTED;
	spin_unlock_irqrestore(&task->task_state_lock, flags);

	return;
 out:
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

		qc->lldd_task = NULL;
		if (!task)
			return;
		task->uldd_task = NULL;
		sas_ata_internal_abort(task);
	}
}


static void sas_ata_set_dmamode(struct ata_port *ap, struct ata_device *ata_dev)
{
	struct domain_device *dev = ap->private_data;
	struct sas_internal *i = dev_to_sas_internal(dev);

	if (i->dft->lldd_ata_set_dmamode)
		i->dft->lldd_ata_set_dmamode(dev);
}

static void sas_ata_sched_eh(struct ata_port *ap)
{
	struct domain_device *dev = ap->private_data;
	struct sas_ha_struct *ha = dev->port->ha;
	unsigned long flags;

	spin_lock_irqsave(&ha->lock, flags);
	if (!test_and_set_bit(SAS_DEV_EH_PENDING, &dev->state))
		ha->eh_active++;
	ata_std_sched_eh(ap);
	spin_unlock_irqrestore(&ha->lock, flags);
}

void sas_ata_end_eh(struct ata_port *ap)
{
	struct domain_device *dev = ap->private_data;
	struct sas_ha_struct *ha = dev->port->ha;
	unsigned long flags;

	spin_lock_irqsave(&ha->lock, flags);
	if (test_and_clear_bit(SAS_DEV_EH_PENDING, &dev->state))
		ha->eh_active--;
	spin_unlock_irqrestore(&ha->lock, flags);
}

static int sas_ata_prereset(struct ata_link *link, unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	struct domain_device *dev = ap->private_data;
	struct sas_phy *local_phy = sas_get_local_phy(dev);
	int res = 0;

	if (!local_phy->enabled || test_bit(SAS_DEV_GONE, &dev->state))
		res = -ENOENT;
	sas_put_local_phy(local_phy);

	return res;
}

static struct ata_port_operations sas_sata_ops = {
	.prereset		= sas_ata_prereset,
	.hardreset		= sas_ata_hard_reset,
	.error_handler		= ata_std_error_handler,
	.post_internal_cmd	= sas_ata_post_internal,
	.qc_defer               = ata_std_qc_defer,
	.qc_prep		= ata_noop_qc_prep,
	.qc_issue		= sas_ata_qc_issue,
	.qc_fill_rtf		= sas_ata_qc_fill_rtf,
	.port_start		= ata_sas_port_start,
	.port_stop		= ata_sas_port_stop,
	.set_dmamode		= sas_ata_set_dmamode,
	.sched_eh		= sas_ata_sched_eh,
	.end_eh			= sas_ata_end_eh,
};

static struct ata_port_info sata_port_info = {
	.flags = ATA_FLAG_SATA | ATA_FLAG_PIO_DMA | ATA_FLAG_NCQ |
		 ATA_FLAG_SAS_HOST | ATA_FLAG_FPDMA_AUX,
	.pio_mask = ATA_PIO4,
	.mwdma_mask = ATA_MWDMA2,
	.udma_mask = ATA_UDMA6,
	.port_ops = &sas_sata_ops
};

int sas_ata_init(struct domain_device *found_dev)
{
	struct sas_ha_struct *ha = found_dev->port->ha;
	struct Scsi_Host *shost = ha->core.shost;
	struct ata_host *ata_host;
	struct ata_port *ap;
	int rc;

	ata_host = kzalloc(sizeof(*ata_host), GFP_KERNEL);
	if (!ata_host)	{
		pr_err("ata host alloc failed.\n");
		return -ENOMEM;
	}

	ata_host_init(ata_host, ha->dev, &sas_sata_ops);

	ap = ata_sas_port_alloc(ata_host, &sata_port_info, shost);
	if (!ap) {
		pr_err("ata_sas_port_alloc failed.\n");
		rc = -ENODEV;
		goto free_host;
	}

	ap->private_data = found_dev;
	ap->cbl = ATA_CBL_SATA;
	ap->scsi_host = shost;
	rc = ata_sas_port_init(ap);
	if (rc)
		goto destroy_port;

	rc = ata_sas_tport_add(ata_host->dev, ap);
	if (rc)
		goto destroy_port;

	found_dev->sata_dev.ata_host = ata_host;
	found_dev->sata_dev.ap = ap;

	return 0;

destroy_port:
	ata_sas_port_destroy(ap);
free_host:
	ata_host_put(ata_host);
	return rc;
}

void sas_ata_task_abort(struct sas_task *task)
{
	struct ata_queued_cmd *qc = task->uldd_task;
	struct completion *waiting;

	/* Bounce SCSI-initiated commands to the SCSI EH */
	if (qc->scsicmd) {
		blk_abort_request(scsi_cmd_to_rq(qc->scsicmd));
		return;
	}

	/* Internal command, fake a timeout and complete. */
	qc->flags &= ~ATA_QCFLAG_ACTIVE;
	qc->flags |= ATA_QCFLAG_FAILED;
	qc->err_mask |= AC_ERR_TIMEOUT;
	waiting = qc->private_data;
	complete(waiting);
}

static int sas_get_ata_command_set(struct domain_device *dev)
{
	struct dev_to_host_fis *fis =
		(struct dev_to_host_fis *) dev->frame_rcvd;
	struct ata_taskfile tf;

	if (dev->dev_type == SAS_SATA_PENDING)
		return ATA_DEV_UNKNOWN;

	ata_tf_from_fis((const u8 *)fis, &tf);

	return ata_dev_classify(&tf);
}

void sas_probe_sata(struct asd_sas_port *port)
{
	struct domain_device *dev, *n;

	mutex_lock(&port->ha->disco_mutex);
	list_for_each_entry(dev, &port->disco_list, disco_list_node) {
		if (!dev_is_sata(dev))
			continue;

		ata_sas_async_probe(dev->sata_dev.ap);
	}
	mutex_unlock(&port->ha->disco_mutex);

	list_for_each_entry_safe(dev, n, &port->disco_list, disco_list_node) {
		if (!dev_is_sata(dev))
			continue;

		sas_ata_wait_eh(dev);

		/* if libata could not bring the link up, don't surface
		 * the device
		 */
		if (!ata_dev_enabled(sas_to_ata_dev(dev)))
			sas_fail_probe(dev, __func__, -ENODEV);
	}

}

static void sas_ata_flush_pm_eh(struct asd_sas_port *port, const char *func)
{
	struct domain_device *dev, *n;

	list_for_each_entry_safe(dev, n, &port->dev_list, dev_list_node) {
		if (!dev_is_sata(dev))
			continue;

		sas_ata_wait_eh(dev);

		/* if libata failed to power manage the device, tear it down */
		if (ata_dev_disabled(sas_to_ata_dev(dev)))
			sas_fail_probe(dev, func, -ENODEV);
	}
}

void sas_suspend_sata(struct asd_sas_port *port)
{
	struct domain_device *dev;

	mutex_lock(&port->ha->disco_mutex);
	list_for_each_entry(dev, &port->dev_list, dev_list_node) {
		struct sata_device *sata;

		if (!dev_is_sata(dev))
			continue;

		sata = &dev->sata_dev;
		if (sata->ap->pm_mesg.event == PM_EVENT_SUSPEND)
			continue;

		ata_sas_port_suspend(sata->ap);
	}
	mutex_unlock(&port->ha->disco_mutex);

	sas_ata_flush_pm_eh(port, __func__);
}

void sas_resume_sata(struct asd_sas_port *port)
{
	struct domain_device *dev;

	mutex_lock(&port->ha->disco_mutex);
	list_for_each_entry(dev, &port->dev_list, dev_list_node) {
		struct sata_device *sata;

		if (!dev_is_sata(dev))
			continue;

		sata = &dev->sata_dev;
		if (sata->ap->pm_mesg.event == PM_EVENT_ON)
			continue;

		ata_sas_port_resume(sata->ap);
	}
	mutex_unlock(&port->ha->disco_mutex);

	sas_ata_flush_pm_eh(port, __func__);
}

/**
 * sas_discover_sata - discover an STP/SATA domain device
 * @dev: pointer to struct domain_device of interest
 *
 * Devices directly attached to a HA port, have no parents.  All other
 * devices do, and should have their "parent" pointer set appropriately
 * before calling this function.
 */
int sas_discover_sata(struct domain_device *dev)
{
	if (dev->dev_type == SAS_SATA_PM)
		return -ENODEV;

	dev->sata_dev.class = sas_get_ata_command_set(dev);
	sas_fill_in_rphy(dev, dev->rphy);

	return sas_notify_lldd_dev_found(dev);
}

static void async_sas_ata_eh(void *data, async_cookie_t cookie)
{
	struct domain_device *dev = data;
	struct ata_port *ap = dev->sata_dev.ap;
	struct sas_ha_struct *ha = dev->port->ha;

	sas_ata_printk(KERN_DEBUG, dev, "dev error handler\n");
	ata_scsi_port_error_handler(ha->core.shost, ap);
	sas_put_device(dev);
}

void sas_ata_strategy_handler(struct Scsi_Host *shost)
{
	struct sas_ha_struct *sas_ha = SHOST_TO_SAS_HA(shost);
	ASYNC_DOMAIN_EXCLUSIVE(async);
	int i;

	/* it's ok to defer revalidation events during ata eh, these
	 * disks are in one of three states:
	 * 1/ present for initial domain discovery, and these
	 *    resets will cause bcn flutters
	 * 2/ hot removed, we'll discover that after eh fails
	 * 3/ hot added after initial discovery, lost the race, and need
	 *    to catch the next train.
	 */
	sas_disable_revalidation(sas_ha);

	spin_lock_irq(&sas_ha->phy_port_lock);
	for (i = 0; i < sas_ha->num_phys; i++) {
		struct asd_sas_port *port = sas_ha->sas_port[i];
		struct domain_device *dev;

		spin_lock(&port->dev_list_lock);
		list_for_each_entry(dev, &port->dev_list, dev_list_node) {
			if (!dev_is_sata(dev))
				continue;

			/* hold a reference over eh since we may be
			 * racing with final remove once all commands
			 * are completed
			 */
			kref_get(&dev->kref);

			async_schedule_domain(async_sas_ata_eh, dev, &async);
		}
		spin_unlock(&port->dev_list_lock);
	}
	spin_unlock_irq(&sas_ha->phy_port_lock);

	async_synchronize_full_domain(&async);

	sas_enable_revalidation(sas_ha);
}

void sas_ata_eh(struct Scsi_Host *shost, struct list_head *work_q)
{
	struct scsi_cmnd *cmd, *n;
	struct domain_device *eh_dev;

	do {
		LIST_HEAD(sata_q);
		eh_dev = NULL;

		list_for_each_entry_safe(cmd, n, work_q, eh_entry) {
			struct domain_device *ddev = cmd_to_domain_dev(cmd);

			if (!dev_is_sata(ddev) || TO_SAS_TASK(cmd))
				continue;
			if (eh_dev && eh_dev != ddev)
				continue;
			eh_dev = ddev;
			list_move(&cmd->eh_entry, &sata_q);
		}

		if (!list_empty(&sata_q)) {
			struct ata_port *ap = eh_dev->sata_dev.ap;

			sas_ata_printk(KERN_DEBUG, eh_dev, "cmd error handler\n");
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
	} while (eh_dev);
}

void sas_ata_schedule_reset(struct domain_device *dev)
{
	struct ata_eh_info *ehi;
	struct ata_port *ap;
	unsigned long flags;

	if (!dev_is_sata(dev))
		return;

	ap = dev->sata_dev.ap;
	ehi = &ap->link.eh_info;

	spin_lock_irqsave(ap->lock, flags);
	ehi->err_mask |= AC_ERR_TIMEOUT;
	ehi->action |= ATA_EH_RESET;
	ata_port_schedule_eh(ap);
	spin_unlock_irqrestore(ap->lock, flags);
}
EXPORT_SYMBOL_GPL(sas_ata_schedule_reset);

void sas_ata_wait_eh(struct domain_device *dev)
{
	struct ata_port *ap;

	if (!dev_is_sata(dev))
		return;

	ap = dev->sata_dev.ap;
	ata_port_wait_eh(ap);
}
