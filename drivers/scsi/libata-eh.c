/*
 *  libata-eh.c - libata error handling
 *
 *  Maintained by:  Jeff Garzik <jgarzik@pobox.com>
 *    		    Please ALWAYS copy linux-ide@vger.kernel.org
 *		    on emails.
 *
 *  Copyright 2006 Tejun Heo <htejun@gmail.com>
 *
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139,
 *  USA.
 *
 *
 *  libata documentation is available via 'make {ps|pdf}docs',
 *  as Documentation/DocBook/libata.*
 *
 *  Hardware documentation available from http://www.t13.org/ and
 *  http://www.sata-io.org/
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>

#include <linux/libata.h>

#include "libata.h"

/**
 *	ata_scsi_timed_out - SCSI layer time out callback
 *	@cmd: timed out SCSI command
 *
 *	Handles SCSI layer timeout.  We race with normal completion of
 *	the qc for @cmd.  If the qc is already gone, we lose and let
 *	the scsi command finish (EH_HANDLED).  Otherwise, the qc has
 *	timed out and EH should be invoked.  Prevent ata_qc_complete()
 *	from finishing it by setting EH_SCHEDULED and return
 *	EH_NOT_HANDLED.
 *
 *	LOCKING:
 *	Called from timer context
 *
 *	RETURNS:
 *	EH_HANDLED or EH_NOT_HANDLED
 */
enum scsi_eh_timer_return ata_scsi_timed_out(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *host = cmd->device->host;
	struct ata_port *ap = (struct ata_port *) &host->hostdata[0];
	unsigned long flags;
	struct ata_queued_cmd *qc;
	enum scsi_eh_timer_return ret = EH_HANDLED;

	DPRINTK("ENTER\n");

	spin_lock_irqsave(&ap->host_set->lock, flags);
	qc = ata_qc_from_tag(ap, ap->active_tag);
	if (qc) {
		WARN_ON(qc->scsicmd != cmd);
		qc->flags |= ATA_QCFLAG_EH_SCHEDULED;
		qc->err_mask |= AC_ERR_TIMEOUT;
		ret = EH_NOT_HANDLED;
	}
	spin_unlock_irqrestore(&ap->host_set->lock, flags);

	DPRINTK("EXIT, ret=%d\n", ret);
	return ret;
}

/**
 *	ata_scsi_error - SCSI layer error handler callback
 *	@host: SCSI host on which error occurred
 *
 *	Handles SCSI-layer-thrown error events.
 *
 *	LOCKING:
 *	Inherited from SCSI layer (none, can sleep)
 *
 *	RETURNS:
 *	Zero.
 */
int ata_scsi_error(struct Scsi_Host *host)
{
	struct ata_port *ap = (struct ata_port *)&host->hostdata[0];

	DPRINTK("ENTER\n");

	/* synchronize with IRQ handler and port task */
	spin_unlock_wait(&ap->host_set->lock);
	ata_port_flush_task(ap);

	WARN_ON(ata_qc_from_tag(ap, ap->active_tag) == NULL);

	ap->ops->eng_timeout(ap);

	WARN_ON(host->host_failed || !list_empty(&host->eh_cmd_q));

	scsi_eh_flush_done_q(&ap->eh_done_q);

	DPRINTK("EXIT\n");
	return 0;
}

/**
 *	ata_qc_timeout - Handle timeout of queued command
 *	@qc: Command that timed out
 *
 *	Some part of the kernel (currently, only the SCSI layer)
 *	has noticed that the active command on port @ap has not
 *	completed after a specified length of time.  Handle this
 *	condition by disabling DMA (if necessary) and completing
 *	transactions, with error if necessary.
 *
 *	This also handles the case of the "lost interrupt", where
 *	for some reason (possibly hardware bug, possibly driver bug)
 *	an interrupt was not delivered to the driver, even though the
 *	transaction completed successfully.
 *
 *	LOCKING:
 *	Inherited from SCSI layer (none, can sleep)
 */
static void ata_qc_timeout(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_host_set *host_set = ap->host_set;
	u8 host_stat = 0, drv_stat;
	unsigned long flags;

	DPRINTK("ENTER\n");

	ap->hsm_task_state = HSM_ST_IDLE;

	spin_lock_irqsave(&host_set->lock, flags);

	switch (qc->tf.protocol) {

	case ATA_PROT_DMA:
	case ATA_PROT_ATAPI_DMA:
		host_stat = ap->ops->bmdma_status(ap);

		/* before we do anything else, clear DMA-Start bit */
		ap->ops->bmdma_stop(qc);

		/* fall through */

	default:
		ata_altstatus(ap);
		drv_stat = ata_chk_status(ap);

		/* ack bmdma irq events */
		ap->ops->irq_clear(ap);

		printk(KERN_ERR "ata%u: command 0x%x timeout, stat 0x%x host_stat 0x%x\n",
		       ap->id, qc->tf.command, drv_stat, host_stat);

		/* complete taskfile transaction */
		qc->err_mask |= AC_ERR_TIMEOUT;
		break;
	}

	spin_unlock_irqrestore(&host_set->lock, flags);

	ata_eh_qc_complete(qc);

	DPRINTK("EXIT\n");
}

/**
 *	ata_eng_timeout - Handle timeout of queued command
 *	@ap: Port on which timed-out command is active
 *
 *	Some part of the kernel (currently, only the SCSI layer)
 *	has noticed that the active command on port @ap has not
 *	completed after a specified length of time.  Handle this
 *	condition by disabling DMA (if necessary) and completing
 *	transactions, with error if necessary.
 *
 *	This also handles the case of the "lost interrupt", where
 *	for some reason (possibly hardware bug, possibly driver bug)
 *	an interrupt was not delivered to the driver, even though the
 *	transaction completed successfully.
 *
 *	LOCKING:
 *	Inherited from SCSI layer (none, can sleep)
 */
void ata_eng_timeout(struct ata_port *ap)
{
	DPRINTK("ENTER\n");

	ata_qc_timeout(ata_qc_from_tag(ap, ap->active_tag));

	DPRINTK("EXIT\n");
}

static void ata_eh_scsidone(struct scsi_cmnd *scmd)
{
	/* nada */
}

static void __ata_eh_qc_complete(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct scsi_cmnd *scmd = qc->scsicmd;
	unsigned long flags;

	spin_lock_irqsave(&ap->host_set->lock, flags);
	qc->scsidone = ata_eh_scsidone;
	__ata_qc_complete(qc);
	WARN_ON(ata_tag_valid(qc->tag));
	spin_unlock_irqrestore(&ap->host_set->lock, flags);

	scsi_eh_finish_cmd(scmd, &ap->eh_done_q);
}

/**
 *	ata_eh_qc_complete - Complete an active ATA command from EH
 *	@qc: Command to complete
 *
 *	Indicate to the mid and upper layers that an ATA command has
 *	completed.  To be used from EH.
 */
void ata_eh_qc_complete(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *scmd = qc->scsicmd;
	scmd->retries = scmd->allowed;
	__ata_eh_qc_complete(qc);
}

/**
 *	ata_eh_qc_retry - Tell midlayer to retry an ATA command after EH
 *	@qc: Command to retry
 *
 *	Indicate to the mid and upper layers that an ATA command
 *	should be retried.  To be used from EH.
 *
 *	SCSI midlayer limits the number of retries to scmd->allowed.
 *	scmd->retries is decremented for commands which get retried
 *	due to unrelated failures (qc->err_mask is zero).
 */
void ata_eh_qc_retry(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *scmd = qc->scsicmd;
	if (!qc->err_mask && scmd->retries)
		scmd->retries--;
	__ata_eh_qc_complete(qc);
}
