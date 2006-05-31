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
#include "scsi_transport_api.h"

#include <linux/libata.h>

#include "libata.h"

static void __ata_port_freeze(struct ata_port *ap);

static void ata_ering_record(struct ata_ering *ering, int is_io,
			     unsigned int err_mask)
{
	struct ata_ering_entry *ent;

	WARN_ON(!err_mask);

	ering->cursor++;
	ering->cursor %= ATA_ERING_SIZE;

	ent = &ering->ring[ering->cursor];
	ent->is_io = is_io;
	ent->err_mask = err_mask;
	ent->timestamp = get_jiffies_64();
}

static struct ata_ering_entry * ata_ering_top(struct ata_ering *ering)
{
	struct ata_ering_entry *ent = &ering->ring[ering->cursor];
	if (!ent->err_mask)
		return NULL;
	return ent;
}

static int ata_ering_map(struct ata_ering *ering,
			 int (*map_fn)(struct ata_ering_entry *, void *),
			 void *arg)
{
	int idx, rc = 0;
	struct ata_ering_entry *ent;

	idx = ering->cursor;
	do {
		ent = &ering->ring[idx];
		if (!ent->err_mask)
			break;
		rc = map_fn(ent, arg);
		if (rc)
			break;
		idx = (idx - 1 + ATA_ERING_SIZE) % ATA_ERING_SIZE;
	} while (idx != ering->cursor);

	return rc;
}

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
 *	TODO: kill this function once old EH is gone.
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
	struct ata_port *ap = ata_shost_to_port(host);
	unsigned long flags;
	struct ata_queued_cmd *qc;
	enum scsi_eh_timer_return ret;

	DPRINTK("ENTER\n");

	if (ap->ops->error_handler) {
		ret = EH_NOT_HANDLED;
		goto out;
	}

	ret = EH_HANDLED;
	spin_lock_irqsave(&ap->host_set->lock, flags);
	qc = ata_qc_from_tag(ap, ap->active_tag);
	if (qc) {
		WARN_ON(qc->scsicmd != cmd);
		qc->flags |= ATA_QCFLAG_EH_SCHEDULED;
		qc->err_mask |= AC_ERR_TIMEOUT;
		ret = EH_NOT_HANDLED;
	}
	spin_unlock_irqrestore(&ap->host_set->lock, flags);

 out:
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
void ata_scsi_error(struct Scsi_Host *host)
{
	struct ata_port *ap = ata_shost_to_port(host);
	spinlock_t *hs_lock = &ap->host_set->lock;
	int i, repeat_cnt = ATA_EH_MAX_REPEAT;
	unsigned long flags;

	DPRINTK("ENTER\n");

	/* synchronize with port task */
	ata_port_flush_task(ap);

	/* synchronize with host_set lock and sort out timeouts */

	/* For new EH, all qcs are finished in one of three ways -
	 * normal completion, error completion, and SCSI timeout.
	 * Both cmpletions can race against SCSI timeout.  When normal
	 * completion wins, the qc never reaches EH.  When error
	 * completion wins, the qc has ATA_QCFLAG_FAILED set.
	 *
	 * When SCSI timeout wins, things are a bit more complex.
	 * Normal or error completion can occur after the timeout but
	 * before this point.  In such cases, both types of
	 * completions are honored.  A scmd is determined to have
	 * timed out iff its associated qc is active and not failed.
	 */
	if (ap->ops->error_handler) {
		struct scsi_cmnd *scmd, *tmp;
		int nr_timedout = 0;

		spin_lock_irqsave(hs_lock, flags);

		list_for_each_entry_safe(scmd, tmp, &host->eh_cmd_q, eh_entry) {
			struct ata_queued_cmd *qc;

			for (i = 0; i < ATA_MAX_QUEUE; i++) {
				qc = __ata_qc_from_tag(ap, i);
				if (qc->flags & ATA_QCFLAG_ACTIVE &&
				    qc->scsicmd == scmd)
					break;
			}

			if (i < ATA_MAX_QUEUE) {
				/* the scmd has an associated qc */
				if (!(qc->flags & ATA_QCFLAG_FAILED)) {
					/* which hasn't failed yet, timeout */
					qc->err_mask |= AC_ERR_TIMEOUT;
					qc->flags |= ATA_QCFLAG_FAILED;
					nr_timedout++;
				}
			} else {
				/* Normal completion occurred after
				 * SCSI timeout but before this point.
				 * Successfully complete it.
				 */
				scmd->retries = scmd->allowed;
				scsi_eh_finish_cmd(scmd, &ap->eh_done_q);
			}
		}

		/* If we have timed out qcs.  They belong to EH from
		 * this point but the state of the controller is
		 * unknown.  Freeze the port to make sure the IRQ
		 * handler doesn't diddle with those qcs.  This must
		 * be done atomically w.r.t. setting QCFLAG_FAILED.
		 */
		if (nr_timedout)
			__ata_port_freeze(ap);

		spin_unlock_irqrestore(hs_lock, flags);
	} else
		spin_unlock_wait(hs_lock);

 repeat:
	/* invoke error handler */
	if (ap->ops->error_handler) {
		/* fetch & clear EH info */
		spin_lock_irqsave(hs_lock, flags);

		memset(&ap->eh_context, 0, sizeof(ap->eh_context));
		ap->eh_context.i = ap->eh_info;
		memset(&ap->eh_info, 0, sizeof(ap->eh_info));

		ap->flags |= ATA_FLAG_EH_IN_PROGRESS;
		ap->flags &= ~ATA_FLAG_EH_PENDING;

		spin_unlock_irqrestore(hs_lock, flags);

		/* invoke EH */
		ap->ops->error_handler(ap);

		/* Exception might have happend after ->error_handler
		 * recovered the port but before this point.  Repeat
		 * EH in such case.
		 */
		spin_lock_irqsave(hs_lock, flags);

		if (ap->flags & ATA_FLAG_EH_PENDING) {
			if (--repeat_cnt) {
				ata_port_printk(ap, KERN_INFO,
					"EH pending after completion, "
					"repeating EH (cnt=%d)\n", repeat_cnt);
				spin_unlock_irqrestore(hs_lock, flags);
				goto repeat;
			}
			ata_port_printk(ap, KERN_ERR, "EH pending after %d "
					"tries, giving up\n", ATA_EH_MAX_REPEAT);
		}

		/* this run is complete, make sure EH info is clear */
		memset(&ap->eh_info, 0, sizeof(ap->eh_info));

		/* Clear host_eh_scheduled while holding hs_lock such
		 * that if exception occurs after this point but
		 * before EH completion, SCSI midlayer will
		 * re-initiate EH.
		 */
		host->host_eh_scheduled = 0;

		spin_unlock_irqrestore(hs_lock, flags);
	} else {
		WARN_ON(ata_qc_from_tag(ap, ap->active_tag) == NULL);
		ap->ops->eng_timeout(ap);
	}

	/* finish or retry handled scmd's and clean up */
	WARN_ON(host->host_failed || !list_empty(&host->eh_cmd_q));

	scsi_eh_flush_done_q(&ap->eh_done_q);

	/* clean up */
	spin_lock_irqsave(hs_lock, flags);

	if (ap->flags & ATA_FLAG_RECOVERED)
		ata_port_printk(ap, KERN_INFO, "EH complete\n");
	ap->flags &= ~ATA_FLAG_RECOVERED;

	/* tell wait_eh that we're done */
	ap->flags &= ~ATA_FLAG_EH_IN_PROGRESS;
	wake_up_all(&ap->eh_wait_q);

	spin_unlock_irqrestore(hs_lock, flags);

	DPRINTK("EXIT\n");
}

/**
 *	ata_port_wait_eh - Wait for the currently pending EH to complete
 *	@ap: Port to wait EH for
 *
 *	Wait until the currently pending EH is complete.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 */
void ata_port_wait_eh(struct ata_port *ap)
{
	unsigned long flags;
	DEFINE_WAIT(wait);

 retry:
	spin_lock_irqsave(&ap->host_set->lock, flags);

	while (ap->flags & (ATA_FLAG_EH_PENDING | ATA_FLAG_EH_IN_PROGRESS)) {
		prepare_to_wait(&ap->eh_wait_q, &wait, TASK_UNINTERRUPTIBLE);
		spin_unlock_irqrestore(&ap->host_set->lock, flags);
		schedule();
		spin_lock_irqsave(&ap->host_set->lock, flags);
	}

	spin_unlock_irqrestore(&ap->host_set->lock, flags);

	/* make sure SCSI EH is complete */
	if (scsi_host_in_recovery(ap->host)) {
		msleep(10);
		goto retry;
	}
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
 *	TODO: kill this function once old EH is gone.
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

		ata_dev_printk(qc->dev, KERN_ERR, "command 0x%x timeout, "
			       "stat 0x%x host_stat 0x%x\n",
			       qc->tf.command, drv_stat, host_stat);

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
 *	TODO: kill this function once old EH is gone.
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

/**
 *	ata_qc_schedule_eh - schedule qc for error handling
 *	@qc: command to schedule error handling for
 *
 *	Schedule error handling for @qc.  EH will kick in as soon as
 *	other commands are drained.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */
void ata_qc_schedule_eh(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	WARN_ON(!ap->ops->error_handler);

	qc->flags |= ATA_QCFLAG_FAILED;
	qc->ap->flags |= ATA_FLAG_EH_PENDING;

	/* The following will fail if timeout has already expired.
	 * ata_scsi_error() takes care of such scmds on EH entry.
	 * Note that ATA_QCFLAG_FAILED is unconditionally set after
	 * this function completes.
	 */
	scsi_req_abort_cmd(qc->scsicmd);
}

/**
 *	ata_port_schedule_eh - schedule error handling without a qc
 *	@ap: ATA port to schedule EH for
 *
 *	Schedule error handling for @ap.  EH will kick in as soon as
 *	all commands are drained.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */
void ata_port_schedule_eh(struct ata_port *ap)
{
	WARN_ON(!ap->ops->error_handler);

	ap->flags |= ATA_FLAG_EH_PENDING;
	scsi_schedule_eh(ap->host);

	DPRINTK("port EH scheduled\n");
}

/**
 *	ata_port_abort - abort all qc's on the port
 *	@ap: ATA port to abort qc's for
 *
 *	Abort all active qc's of @ap and schedule EH.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *	Number of aborted qc's.
 */
int ata_port_abort(struct ata_port *ap)
{
	int tag, nr_aborted = 0;

	WARN_ON(!ap->ops->error_handler);

	for (tag = 0; tag < ATA_MAX_QUEUE; tag++) {
		struct ata_queued_cmd *qc = ata_qc_from_tag(ap, tag);

		if (qc) {
			qc->flags |= ATA_QCFLAG_FAILED;
			ata_qc_complete(qc);
			nr_aborted++;
		}
	}

	if (!nr_aborted)
		ata_port_schedule_eh(ap);

	return nr_aborted;
}

/**
 *	__ata_port_freeze - freeze port
 *	@ap: ATA port to freeze
 *
 *	This function is called when HSM violation or some other
 *	condition disrupts normal operation of the port.  Frozen port
 *	is not allowed to perform any operation until the port is
 *	thawed, which usually follows a successful reset.
 *
 *	ap->ops->freeze() callback can be used for freezing the port
 *	hardware-wise (e.g. mask interrupt and stop DMA engine).  If a
 *	port cannot be frozen hardware-wise, the interrupt handler
 *	must ack and clear interrupts unconditionally while the port
 *	is frozen.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */
static void __ata_port_freeze(struct ata_port *ap)
{
	WARN_ON(!ap->ops->error_handler);

	if (ap->ops->freeze)
		ap->ops->freeze(ap);

	ap->flags |= ATA_FLAG_FROZEN;

	DPRINTK("ata%u port frozen\n", ap->id);
}

/**
 *	ata_port_freeze - abort & freeze port
 *	@ap: ATA port to freeze
 *
 *	Abort and freeze @ap.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *	Number of aborted commands.
 */
int ata_port_freeze(struct ata_port *ap)
{
	int nr_aborted;

	WARN_ON(!ap->ops->error_handler);

	nr_aborted = ata_port_abort(ap);
	__ata_port_freeze(ap);

	return nr_aborted;
}

/**
 *	ata_eh_freeze_port - EH helper to freeze port
 *	@ap: ATA port to freeze
 *
 *	Freeze @ap.
 *
 *	LOCKING:
 *	None.
 */
void ata_eh_freeze_port(struct ata_port *ap)
{
	unsigned long flags;

	if (!ap->ops->error_handler)
		return;

	spin_lock_irqsave(&ap->host_set->lock, flags);
	__ata_port_freeze(ap);
	spin_unlock_irqrestore(&ap->host_set->lock, flags);
}

/**
 *	ata_port_thaw_port - EH helper to thaw port
 *	@ap: ATA port to thaw
 *
 *	Thaw frozen port @ap.
 *
 *	LOCKING:
 *	None.
 */
void ata_eh_thaw_port(struct ata_port *ap)
{
	unsigned long flags;

	if (!ap->ops->error_handler)
		return;

	spin_lock_irqsave(&ap->host_set->lock, flags);

	ap->flags &= ~ATA_FLAG_FROZEN;

	if (ap->ops->thaw)
		ap->ops->thaw(ap);

	spin_unlock_irqrestore(&ap->host_set->lock, flags);

	DPRINTK("ata%u port thawed\n", ap->id);
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

/**
 *	ata_eh_about_to_do - about to perform eh_action
 *	@ap: target ATA port
 *	@action: action about to be performed
 *
 *	Called just before performing EH actions to clear related bits
 *	in @ap->eh_info such that eh actions are not unnecessarily
 *	repeated.
 *
 *	LOCKING:
 *	None.
 */
static void ata_eh_about_to_do(struct ata_port *ap, unsigned int action)
{
	unsigned long flags;

	spin_lock_irqsave(&ap->host_set->lock, flags);
	ap->eh_info.action &= ~action;
	ap->flags |= ATA_FLAG_RECOVERED;
	spin_unlock_irqrestore(&ap->host_set->lock, flags);
}

/**
 *	ata_err_string - convert err_mask to descriptive string
 *	@err_mask: error mask to convert to string
 *
 *	Convert @err_mask to descriptive string.  Errors are
 *	prioritized according to severity and only the most severe
 *	error is reported.
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	Descriptive string for @err_mask
 */
static const char * ata_err_string(unsigned int err_mask)
{
	if (err_mask & AC_ERR_HOST_BUS)
		return "host bus error";
	if (err_mask & AC_ERR_ATA_BUS)
		return "ATA bus error";
	if (err_mask & AC_ERR_TIMEOUT)
		return "timeout";
	if (err_mask & AC_ERR_HSM)
		return "HSM violation";
	if (err_mask & AC_ERR_SYSTEM)
		return "internal error";
	if (err_mask & AC_ERR_MEDIA)
		return "media error";
	if (err_mask & AC_ERR_INVALID)
		return "invalid argument";
	if (err_mask & AC_ERR_DEV)
		return "device error";
	return "unknown error";
}

/**
 *	ata_read_log_page - read a specific log page
 *	@dev: target device
 *	@page: page to read
 *	@buf: buffer to store read page
 *	@sectors: number of sectors to read
 *
 *	Read log page using READ_LOG_EXT command.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, AC_ERR_* mask otherwise.
 */
static unsigned int ata_read_log_page(struct ata_device *dev,
				      u8 page, void *buf, unsigned int sectors)
{
	struct ata_taskfile tf;
	unsigned int err_mask;

	DPRINTK("read log page - page %d\n", page);

	ata_tf_init(dev, &tf);
	tf.command = ATA_CMD_READ_LOG_EXT;
	tf.lbal = page;
	tf.nsect = sectors;
	tf.hob_nsect = sectors >> 8;
	tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_LBA48 | ATA_TFLAG_DEVICE;
	tf.protocol = ATA_PROT_PIO;

	err_mask = ata_exec_internal(dev, &tf, NULL, DMA_FROM_DEVICE,
				     buf, sectors * ATA_SECT_SIZE);

	DPRINTK("EXIT, err_mask=%x\n", err_mask);
	return err_mask;
}

/**
 *	ata_eh_read_log_10h - Read log page 10h for NCQ error details
 *	@dev: Device to read log page 10h from
 *	@tag: Resulting tag of the failed command
 *	@tf: Resulting taskfile registers of the failed command
 *
 *	Read log page 10h to obtain NCQ error details and clear error
 *	condition.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
static int ata_eh_read_log_10h(struct ata_device *dev,
			       int *tag, struct ata_taskfile *tf)
{
	u8 *buf = dev->ap->sector_buf;
	unsigned int err_mask;
	u8 csum;
	int i;

	err_mask = ata_read_log_page(dev, ATA_LOG_SATA_NCQ, buf, 1);
	if (err_mask)
		return -EIO;

	csum = 0;
	for (i = 0; i < ATA_SECT_SIZE; i++)
		csum += buf[i];
	if (csum)
		ata_dev_printk(dev, KERN_WARNING,
			       "invalid checksum 0x%x on log page 10h\n", csum);

	if (buf[0] & 0x80)
		return -ENOENT;

	*tag = buf[0] & 0x1f;

	tf->command = buf[2];
	tf->feature = buf[3];
	tf->lbal = buf[4];
	tf->lbam = buf[5];
	tf->lbah = buf[6];
	tf->device = buf[7];
	tf->hob_lbal = buf[8];
	tf->hob_lbam = buf[9];
	tf->hob_lbah = buf[10];
	tf->nsect = buf[12];
	tf->hob_nsect = buf[13];

	return 0;
}

/**
 *	atapi_eh_request_sense - perform ATAPI REQUEST_SENSE
 *	@dev: device to perform REQUEST_SENSE to
 *	@sense_buf: result sense data buffer (SCSI_SENSE_BUFFERSIZE bytes long)
 *
 *	Perform ATAPI REQUEST_SENSE after the device reported CHECK
 *	SENSE.  This function is EH helper.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, AC_ERR_* mask on failure
 */
static unsigned int atapi_eh_request_sense(struct ata_device *dev,
					   unsigned char *sense_buf)
{
	struct ata_port *ap = dev->ap;
	struct ata_taskfile tf;
	u8 cdb[ATAPI_CDB_LEN];

	DPRINTK("ATAPI request sense\n");

	ata_tf_init(dev, &tf);

	/* FIXME: is this needed? */
	memset(sense_buf, 0, SCSI_SENSE_BUFFERSIZE);

	/* XXX: why tf_read here? */
	ap->ops->tf_read(ap, &tf);

	/* fill these in, for the case where they are -not- overwritten */
	sense_buf[0] = 0x70;
	sense_buf[2] = tf.feature >> 4;

	memset(cdb, 0, ATAPI_CDB_LEN);
	cdb[0] = REQUEST_SENSE;
	cdb[4] = SCSI_SENSE_BUFFERSIZE;

	tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	tf.command = ATA_CMD_PACKET;

	/* is it pointless to prefer PIO for "safety reasons"? */
	if (ap->flags & ATA_FLAG_PIO_DMA) {
		tf.protocol = ATA_PROT_ATAPI_DMA;
		tf.feature |= ATAPI_PKT_DMA;
	} else {
		tf.protocol = ATA_PROT_ATAPI;
		tf.lbam = (8 * 1024) & 0xff;
		tf.lbah = (8 * 1024) >> 8;
	}

	return ata_exec_internal(dev, &tf, cdb, DMA_FROM_DEVICE,
				 sense_buf, SCSI_SENSE_BUFFERSIZE);
}

/**
 *	ata_eh_analyze_serror - analyze SError for a failed port
 *	@ap: ATA port to analyze SError for
 *
 *	Analyze SError if available and further determine cause of
 *	failure.
 *
 *	LOCKING:
 *	None.
 */
static void ata_eh_analyze_serror(struct ata_port *ap)
{
	struct ata_eh_context *ehc = &ap->eh_context;
	u32 serror = ehc->i.serror;
	unsigned int err_mask = 0, action = 0;

	if (serror & SERR_PERSISTENT) {
		err_mask |= AC_ERR_ATA_BUS;
		action |= ATA_EH_HARDRESET;
	}
	if (serror &
	    (SERR_DATA_RECOVERED | SERR_COMM_RECOVERED | SERR_DATA)) {
		err_mask |= AC_ERR_ATA_BUS;
		action |= ATA_EH_SOFTRESET;
	}
	if (serror & SERR_PROTOCOL) {
		err_mask |= AC_ERR_HSM;
		action |= ATA_EH_SOFTRESET;
	}
	if (serror & SERR_INTERNAL) {
		err_mask |= AC_ERR_SYSTEM;
		action |= ATA_EH_SOFTRESET;
	}
	if (serror & (SERR_PHYRDY_CHG | SERR_DEV_XCHG)) {
		err_mask |= AC_ERR_ATA_BUS;
		action |= ATA_EH_HARDRESET;
	}

	ehc->i.err_mask |= err_mask;
	ehc->i.action |= action;
}

/**
 *	ata_eh_analyze_ncq_error - analyze NCQ error
 *	@ap: ATA port to analyze NCQ error for
 *
 *	Read log page 10h, determine the offending qc and acquire
 *	error status TF.  For NCQ device errors, all LLDDs have to do
 *	is setting AC_ERR_DEV in ehi->err_mask.  This function takes
 *	care of the rest.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 */
static void ata_eh_analyze_ncq_error(struct ata_port *ap)
{
	struct ata_eh_context *ehc = &ap->eh_context;
	struct ata_device *dev = ap->device;
	struct ata_queued_cmd *qc;
	struct ata_taskfile tf;
	int tag, rc;

	/* if frozen, we can't do much */
	if (ap->flags & ATA_FLAG_FROZEN)
		return;

	/* is it NCQ device error? */
	if (!ap->sactive || !(ehc->i.err_mask & AC_ERR_DEV))
		return;

	/* has LLDD analyzed already? */
	for (tag = 0; tag < ATA_MAX_QUEUE; tag++) {
		qc = __ata_qc_from_tag(ap, tag);

		if (!(qc->flags & ATA_QCFLAG_FAILED))
			continue;

		if (qc->err_mask)
			return;
	}

	/* okay, this error is ours */
	rc = ata_eh_read_log_10h(dev, &tag, &tf);
	if (rc) {
		ata_port_printk(ap, KERN_ERR, "failed to read log page 10h "
				"(errno=%d)\n", rc);
		return;
	}

	if (!(ap->sactive & (1 << tag))) {
		ata_port_printk(ap, KERN_ERR, "log page 10h reported "
				"inactive tag %d\n", tag);
		return;
	}

	/* we've got the perpetrator, condemn it */
	qc = __ata_qc_from_tag(ap, tag);
	memcpy(&qc->result_tf, &tf, sizeof(tf));
	qc->err_mask |= AC_ERR_DEV;
	ehc->i.err_mask &= ~AC_ERR_DEV;
}

/**
 *	ata_eh_analyze_tf - analyze taskfile of a failed qc
 *	@qc: qc to analyze
 *	@tf: Taskfile registers to analyze
 *
 *	Analyze taskfile of @qc and further determine cause of
 *	failure.  This function also requests ATAPI sense data if
 *	avaliable.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	Determined recovery action
 */
static unsigned int ata_eh_analyze_tf(struct ata_queued_cmd *qc,
				      const struct ata_taskfile *tf)
{
	unsigned int tmp, action = 0;
	u8 stat = tf->command, err = tf->feature;

	if ((stat & (ATA_BUSY | ATA_DRQ | ATA_DRDY)) != ATA_DRDY) {
		qc->err_mask |= AC_ERR_HSM;
		return ATA_EH_SOFTRESET;
	}

	if (!(qc->err_mask & AC_ERR_DEV))
		return 0;

	switch (qc->dev->class) {
	case ATA_DEV_ATA:
		if (err & ATA_ICRC)
			qc->err_mask |= AC_ERR_ATA_BUS;
		if (err & ATA_UNC)
			qc->err_mask |= AC_ERR_MEDIA;
		if (err & ATA_IDNF)
			qc->err_mask |= AC_ERR_INVALID;
		break;

	case ATA_DEV_ATAPI:
		tmp = atapi_eh_request_sense(qc->dev,
					     qc->scsicmd->sense_buffer);
		if (!tmp) {
			/* ATA_QCFLAG_SENSE_VALID is used to tell
			 * atapi_qc_complete() that sense data is
			 * already valid.
			 *
			 * TODO: interpret sense data and set
			 * appropriate err_mask.
			 */
			qc->flags |= ATA_QCFLAG_SENSE_VALID;
		} else
			qc->err_mask |= tmp;
	}

	if (qc->err_mask & (AC_ERR_HSM | AC_ERR_TIMEOUT | AC_ERR_ATA_BUS))
		action |= ATA_EH_SOFTRESET;

	return action;
}

static int ata_eh_categorize_ering_entry(struct ata_ering_entry *ent)
{
	if (ent->err_mask & (AC_ERR_ATA_BUS | AC_ERR_TIMEOUT))
		return 1;

	if (ent->is_io) {
		if (ent->err_mask & AC_ERR_HSM)
			return 1;
		if ((ent->err_mask &
		     (AC_ERR_DEV|AC_ERR_MEDIA|AC_ERR_INVALID)) == AC_ERR_DEV)
			return 2;
	}

	return 0;
}

struct speed_down_needed_arg {
	u64 since;
	int nr_errors[3];
};

static int speed_down_needed_cb(struct ata_ering_entry *ent, void *void_arg)
{
	struct speed_down_needed_arg *arg = void_arg;

	if (ent->timestamp < arg->since)
		return -1;

	arg->nr_errors[ata_eh_categorize_ering_entry(ent)]++;
	return 0;
}

/**
 *	ata_eh_speed_down_needed - Determine wheter speed down is necessary
 *	@dev: Device of interest
 *
 *	This function examines error ring of @dev and determines
 *	whether speed down is necessary.  Speed down is necessary if
 *	there have been more than 3 of Cat-1 errors or 10 of Cat-2
 *	errors during last 15 minutes.
 *
 *	Cat-1 errors are ATA_BUS, TIMEOUT for any command and HSM
 *	violation for known supported commands.
 *
 *	Cat-2 errors are unclassified DEV error for known supported
 *	command.
 *
 *	LOCKING:
 *	Inherited from caller.
 *
 *	RETURNS:
 *	1 if speed down is necessary, 0 otherwise
 */
static int ata_eh_speed_down_needed(struct ata_device *dev)
{
	const u64 interval = 15LLU * 60 * HZ;
	static const int err_limits[3] = { -1, 3, 10 };
	struct speed_down_needed_arg arg;
	struct ata_ering_entry *ent;
	int err_cat;
	u64 j64;

	ent = ata_ering_top(&dev->ering);
	if (!ent)
		return 0;

	err_cat = ata_eh_categorize_ering_entry(ent);
	if (err_cat == 0)
		return 0;

	memset(&arg, 0, sizeof(arg));

	j64 = get_jiffies_64();
	if (j64 >= interval)
		arg.since = j64 - interval;
	else
		arg.since = 0;

	ata_ering_map(&dev->ering, speed_down_needed_cb, &arg);

	return arg.nr_errors[err_cat] > err_limits[err_cat];
}

/**
 *	ata_eh_speed_down - record error and speed down if necessary
 *	@dev: Failed device
 *	@is_io: Did the device fail during normal IO?
 *	@err_mask: err_mask of the error
 *
 *	Record error and examine error history to determine whether
 *	adjusting transmission speed is necessary.  It also sets
 *	transmission limits appropriately if such adjustment is
 *	necessary.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno otherwise
 */
static int ata_eh_speed_down(struct ata_device *dev, int is_io,
			     unsigned int err_mask)
{
	if (!err_mask)
		return 0;

	/* record error and determine whether speed down is necessary */
	ata_ering_record(&dev->ering, is_io, err_mask);

	if (!ata_eh_speed_down_needed(dev))
		return 0;

	/* speed down SATA link speed if possible */
	if (sata_down_spd_limit(dev->ap) == 0)
		return ATA_EH_HARDRESET;

	/* lower transfer mode */
	if (ata_down_xfermask_limit(dev, 0) == 0)
		return ATA_EH_SOFTRESET;

	ata_dev_printk(dev, KERN_ERR,
		       "speed down requested but no transfer mode left\n");
	return 0;
}

/**
 *	ata_eh_autopsy - analyze error and determine recovery action
 *	@ap: ATA port to perform autopsy on
 *
 *	Analyze why @ap failed and determine which recovery action is
 *	needed.  This function also sets more detailed AC_ERR_* values
 *	and fills sense data for ATAPI CHECK SENSE.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 */
static void ata_eh_autopsy(struct ata_port *ap)
{
	struct ata_eh_context *ehc = &ap->eh_context;
	unsigned int action = ehc->i.action;
	struct ata_device *failed_dev = NULL;
	unsigned int all_err_mask = 0;
	int tag, is_io = 0;
	u32 serror;
	int rc;

	DPRINTK("ENTER\n");

	/* obtain and analyze SError */
	rc = sata_scr_read(ap, SCR_ERROR, &serror);
	if (rc == 0) {
		ehc->i.serror |= serror;
		ata_eh_analyze_serror(ap);
	} else if (rc != -EOPNOTSUPP)
		action |= ATA_EH_HARDRESET;

	/* analyze NCQ failure */
	ata_eh_analyze_ncq_error(ap);

	/* any real error trumps AC_ERR_OTHER */
	if (ehc->i.err_mask & ~AC_ERR_OTHER)
		ehc->i.err_mask &= ~AC_ERR_OTHER;

	all_err_mask |= ehc->i.err_mask;

	for (tag = 0; tag < ATA_MAX_QUEUE; tag++) {
		struct ata_queued_cmd *qc = __ata_qc_from_tag(ap, tag);

		if (!(qc->flags & ATA_QCFLAG_FAILED))
			continue;

		/* inherit upper level err_mask */
		qc->err_mask |= ehc->i.err_mask;

		/* analyze TF */
		action |= ata_eh_analyze_tf(qc, &qc->result_tf);

		/* DEV errors are probably spurious in case of ATA_BUS error */
		if (qc->err_mask & AC_ERR_ATA_BUS)
			qc->err_mask &= ~(AC_ERR_DEV | AC_ERR_MEDIA |
					  AC_ERR_INVALID);

		/* any real error trumps unknown error */
		if (qc->err_mask & ~AC_ERR_OTHER)
			qc->err_mask &= ~AC_ERR_OTHER;

		/* SENSE_VALID trumps dev/unknown error and revalidation */
		if (qc->flags & ATA_QCFLAG_SENSE_VALID) {
			qc->err_mask &= ~(AC_ERR_DEV | AC_ERR_OTHER);
			action &= ~ATA_EH_REVALIDATE;
		}

		/* accumulate error info */
		failed_dev = qc->dev;
		all_err_mask |= qc->err_mask;
		if (qc->flags & ATA_QCFLAG_IO)
			is_io = 1;
	}

	/* speed down iff command was in progress */
	if (failed_dev)
		action |= ata_eh_speed_down(failed_dev, is_io, all_err_mask);

	/* enforce default EH actions */
	if (ap->flags & ATA_FLAG_FROZEN ||
	    all_err_mask & (AC_ERR_HSM | AC_ERR_TIMEOUT))
		action |= ATA_EH_SOFTRESET;
	else if (all_err_mask)
		action |= ATA_EH_REVALIDATE;

	/* record autopsy result */
	ehc->i.dev = failed_dev;
	ehc->i.action = action;

	DPRINTK("EXIT\n");
}

/**
 *	ata_eh_report - report error handling to user
 *	@ap: ATA port EH is going on
 *
 *	Report EH to user.
 *
 *	LOCKING:
 *	None.
 */
static void ata_eh_report(struct ata_port *ap)
{
	struct ata_eh_context *ehc = &ap->eh_context;
	const char *frozen, *desc;
	int tag, nr_failed = 0;

	desc = NULL;
	if (ehc->i.desc[0] != '\0')
		desc = ehc->i.desc;

	for (tag = 0; tag < ATA_MAX_QUEUE; tag++) {
		struct ata_queued_cmd *qc = __ata_qc_from_tag(ap, tag);

		if (!(qc->flags & ATA_QCFLAG_FAILED))
			continue;
		if (qc->flags & ATA_QCFLAG_SENSE_VALID && !qc->err_mask)
			continue;

		nr_failed++;
	}

	if (!nr_failed && !ehc->i.err_mask)
		return;

	frozen = "";
	if (ap->flags & ATA_FLAG_FROZEN)
		frozen = " frozen";

	if (ehc->i.dev) {
		ata_dev_printk(ehc->i.dev, KERN_ERR, "exception Emask 0x%x "
			       "SAct 0x%x SErr 0x%x action 0x%x%s\n",
			       ehc->i.err_mask, ap->sactive, ehc->i.serror,
			       ehc->i.action, frozen);
		if (desc)
			ata_dev_printk(ehc->i.dev, KERN_ERR, "(%s)\n", desc);
	} else {
		ata_port_printk(ap, KERN_ERR, "exception Emask 0x%x "
				"SAct 0x%x SErr 0x%x action 0x%x%s\n",
				ehc->i.err_mask, ap->sactive, ehc->i.serror,
				ehc->i.action, frozen);
		if (desc)
			ata_port_printk(ap, KERN_ERR, "(%s)\n", desc);
	}

	for (tag = 0; tag < ATA_MAX_QUEUE; tag++) {
		struct ata_queued_cmd *qc = __ata_qc_from_tag(ap, tag);

		if (!(qc->flags & ATA_QCFLAG_FAILED) || !qc->err_mask)
			continue;

		ata_dev_printk(qc->dev, KERN_ERR, "tag %d cmd 0x%x "
			       "Emask 0x%x stat 0x%x err 0x%x (%s)\n",
			       qc->tag, qc->tf.command, qc->err_mask,
			       qc->result_tf.command, qc->result_tf.feature,
			       ata_err_string(qc->err_mask));
	}
}

static int ata_eh_reset(struct ata_port *ap,
			ata_prereset_fn_t prereset, ata_reset_fn_t softreset,
			ata_reset_fn_t hardreset, ata_postreset_fn_t postreset)
{
	struct ata_eh_context *ehc = &ap->eh_context;
	unsigned int classes[ATA_MAX_DEVICES];
	int tries = ATA_EH_RESET_TRIES;
	unsigned int action;
	ata_reset_fn_t reset;
	int i, rc;

	/* Determine which reset to use and record in ehc->i.action.
	 * prereset() may examine and modify it.
	 */
	action = ehc->i.action;
	ehc->i.action &= ~ATA_EH_RESET_MASK;
	if (softreset && (!hardreset || (!sata_set_spd_needed(ap) &&
					 !(action & ATA_EH_HARDRESET))))
		ehc->i.action |= ATA_EH_SOFTRESET;
	else
		ehc->i.action |= ATA_EH_HARDRESET;

	if (prereset) {
		rc = prereset(ap);
		if (rc) {
			ata_port_printk(ap, KERN_ERR,
					"prereset failed (errno=%d)\n", rc);
			return rc;
		}
	}

	/* prereset() might have modified ehc->i.action */
	if (ehc->i.action & ATA_EH_HARDRESET)
		reset = hardreset;
	else if (ehc->i.action & ATA_EH_SOFTRESET)
		reset = softreset;
	else {
		/* prereset told us not to reset, bang classes and return */
		for (i = 0; i < ATA_MAX_DEVICES; i++)
			classes[i] = ATA_DEV_NONE;
		return 0;
	}

	/* did prereset() screw up?  if so, fix up to avoid oopsing */
	if (!reset) {
		ata_port_printk(ap, KERN_ERR, "BUG: prereset() requested "
				"invalid reset type\n");
		if (softreset)
			reset = softreset;
		else
			reset = hardreset;
	}

 retry:
	ata_port_printk(ap, KERN_INFO, "%s resetting port\n",
			reset == softreset ? "soft" : "hard");

	/* reset */
	ata_eh_about_to_do(ap, ATA_EH_RESET_MASK);
	ehc->i.flags |= ATA_EHI_DID_RESET;

	rc = ata_do_reset(ap, reset, classes);

	if (rc && --tries) {
		ata_port_printk(ap, KERN_WARNING,
				"%sreset failed, retrying in 5 secs\n",
				reset == softreset ? "soft" : "hard");
		ssleep(5);

		if (reset == hardreset)
			sata_down_spd_limit(ap);
		if (hardreset)
			reset = hardreset;
		goto retry;
	}

	if (rc == 0) {
		/* After the reset, the device state is PIO 0 and the
		 * controller state is undefined.  Record the mode.
		 */
		for (i = 0; i < ATA_MAX_DEVICES; i++)
			ap->device[i].pio_mode = XFER_PIO_0;

		if (postreset)
			postreset(ap, classes);

		/* reset successful, schedule revalidation */
		ehc->i.dev = NULL;
		ehc->i.action &= ~ATA_EH_RESET_MASK;
		ehc->i.action |= ATA_EH_REVALIDATE;
	}

	return rc;
}

static int ata_eh_revalidate(struct ata_port *ap,
			     struct ata_device **r_failed_dev)
{
	struct ata_eh_context *ehc = &ap->eh_context;
	struct ata_device *dev;
	int i, rc = 0;

	DPRINTK("ENTER\n");

	for (i = 0; i < ATA_MAX_DEVICES; i++) {
		dev = &ap->device[i];

		if (ehc->i.action & ATA_EH_REVALIDATE && ata_dev_enabled(dev) &&
		    (!ehc->i.dev || ehc->i.dev == dev)) {
			if (ata_port_offline(ap)) {
				rc = -EIO;
				break;
			}

			ata_eh_about_to_do(ap, ATA_EH_REVALIDATE);
			rc = ata_dev_revalidate(dev,
					ehc->i.flags & ATA_EHI_DID_RESET);
			if (rc)
				break;

			ehc->i.action &= ~ATA_EH_REVALIDATE;
		}
	}

	if (rc)
		*r_failed_dev = dev;

	DPRINTK("EXIT\n");
	return rc;
}

static int ata_port_nr_enabled(struct ata_port *ap)
{
	int i, cnt = 0;

	for (i = 0; i < ATA_MAX_DEVICES; i++)
		if (ata_dev_enabled(&ap->device[i]))
			cnt++;
	return cnt;
}

/**
 *	ata_eh_recover - recover host port after error
 *	@ap: host port to recover
 *	@prereset: prereset method (can be NULL)
 *	@softreset: softreset method (can be NULL)
 *	@hardreset: hardreset method (can be NULL)
 *	@postreset: postreset method (can be NULL)
 *
 *	This is the alpha and omega, eum and yang, heart and soul of
 *	libata exception handling.  On entry, actions required to
 *	recover each devices are recorded in eh_context.  This
 *	function executes all the operations with appropriate retrials
 *	and fallbacks to resurrect failed devices.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
static int ata_eh_recover(struct ata_port *ap, ata_prereset_fn_t prereset,
			  ata_reset_fn_t softreset, ata_reset_fn_t hardreset,
			  ata_postreset_fn_t postreset)
{
	struct ata_eh_context *ehc = &ap->eh_context;
	struct ata_device *dev;
	int down_xfermask, i, rc;

	DPRINTK("ENTER\n");

	/* prep for recovery */
	for (i = 0; i < ATA_MAX_DEVICES; i++) {
		dev = &ap->device[i];

		ehc->tries[dev->devno] = ATA_EH_DEV_TRIES;
	}

 retry:
	down_xfermask = 0;
	rc = 0;

	/* skip EH if possible. */
	if (!ata_port_nr_enabled(ap) && !(ap->flags & ATA_FLAG_FROZEN))
		ehc->i.action = 0;

	/* reset */
	if (ehc->i.action & ATA_EH_RESET_MASK) {
		ata_eh_freeze_port(ap);

		rc = ata_eh_reset(ap, prereset, softreset, hardreset,
				  postreset);
		if (rc) {
			ata_port_printk(ap, KERN_ERR,
					"reset failed, giving up\n");
			goto out;
		}

		ata_eh_thaw_port(ap);
	}

	/* revalidate existing devices */
	rc = ata_eh_revalidate(ap, &dev);
	if (rc)
		goto dev_fail;

	/* configure transfer mode if the port has been reset */
	if (ehc->i.flags & ATA_EHI_DID_RESET) {
		rc = ata_set_mode(ap, &dev);
		if (rc) {
			down_xfermask = 1;
			goto dev_fail;
		}
	}

	goto out;

 dev_fail:
	switch (rc) {
	case -ENODEV:
	case -EINVAL:
		ehc->tries[dev->devno] = 0;
		break;
	case -EIO:
		sata_down_spd_limit(ap);
	default:
		ehc->tries[dev->devno]--;
		if (down_xfermask &&
		    ata_down_xfermask_limit(dev, ehc->tries[dev->devno] == 1))
			ehc->tries[dev->devno] = 0;
	}

	/* disable device if it has used up all its chances */
	if (ata_dev_enabled(dev) && !ehc->tries[dev->devno])
		ata_dev_disable(dev);

	/* soft didn't work?  be haaaaard */
	if (ehc->i.flags & ATA_EHI_DID_RESET)
		ehc->i.action |= ATA_EH_HARDRESET;
	else
		ehc->i.action |= ATA_EH_SOFTRESET;

	if (ata_port_nr_enabled(ap)) {
		ata_port_printk(ap, KERN_WARNING, "failed to recover some "
				"devices, retrying in 5 secs\n");
		ssleep(5);
	} else {
		/* no device left, repeat fast */
		msleep(500);
	}

	goto retry;

 out:
	if (rc) {
		for (i = 0; i < ATA_MAX_DEVICES; i++)
			ata_dev_disable(&ap->device[i]);
	}

	DPRINTK("EXIT, rc=%d\n", rc);
	return rc;
}

/**
 *	ata_eh_finish - finish up EH
 *	@ap: host port to finish EH for
 *
 *	Recovery is complete.  Clean up EH states and retry or finish
 *	failed qcs.
 *
 *	LOCKING:
 *	None.
 */
static void ata_eh_finish(struct ata_port *ap)
{
	int tag;

	/* retry or finish qcs */
	for (tag = 0; tag < ATA_MAX_QUEUE; tag++) {
		struct ata_queued_cmd *qc = __ata_qc_from_tag(ap, tag);

		if (!(qc->flags & ATA_QCFLAG_FAILED))
			continue;

		if (qc->err_mask) {
			/* FIXME: Once EH migration is complete,
			 * generate sense data in this function,
			 * considering both err_mask and tf.
			 */
			if (qc->err_mask & AC_ERR_INVALID)
				ata_eh_qc_complete(qc);
			else
				ata_eh_qc_retry(qc);
		} else {
			if (qc->flags & ATA_QCFLAG_SENSE_VALID) {
				ata_eh_qc_complete(qc);
			} else {
				/* feed zero TF to sense generation */
				memset(&qc->result_tf, 0, sizeof(qc->result_tf));
				ata_eh_qc_retry(qc);
			}
		}
	}
}

/**
 *	ata_do_eh - do standard error handling
 *	@ap: host port to handle error for
 *	@prereset: prereset method (can be NULL)
 *	@softreset: softreset method (can be NULL)
 *	@hardreset: hardreset method (can be NULL)
 *	@postreset: postreset method (can be NULL)
 *
 *	Perform standard error handling sequence.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 */
void ata_do_eh(struct ata_port *ap, ata_prereset_fn_t prereset,
	       ata_reset_fn_t softreset, ata_reset_fn_t hardreset,
	       ata_postreset_fn_t postreset)
{
	ata_eh_autopsy(ap);
	ata_eh_report(ap);
	ata_eh_recover(ap, prereset, softreset, hardreset, postreset);
	ata_eh_finish(ap);
}
