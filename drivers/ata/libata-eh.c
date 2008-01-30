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

#include <linux/kernel.h>
#include <linux/pci.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include "../scsi/scsi_transport_api.h"

#include <linux/libata.h>

#include "libata.h"

enum {
	/* speed down verdicts */
	ATA_EH_SPDN_NCQ_OFF		= (1 << 0),
	ATA_EH_SPDN_SPEED_DOWN		= (1 << 1),
	ATA_EH_SPDN_FALLBACK_TO_PIO	= (1 << 2),
	ATA_EH_SPDN_KEEP_ERRORS		= (1 << 3),

	/* error flags */
	ATA_EFLAG_IS_IO			= (1 << 0),
	ATA_EFLAG_DUBIOUS_XFER		= (1 << 1),

	/* error categories */
	ATA_ECAT_NONE			= 0,
	ATA_ECAT_ATA_BUS		= 1,
	ATA_ECAT_TOUT_HSM		= 2,
	ATA_ECAT_UNK_DEV		= 3,
	ATA_ECAT_DUBIOUS_NONE		= 4,
	ATA_ECAT_DUBIOUS_ATA_BUS	= 5,
	ATA_ECAT_DUBIOUS_TOUT_HSM	= 6,
	ATA_ECAT_DUBIOUS_UNK_DEV	= 7,
	ATA_ECAT_NR			= 8,
};

/* Waiting in ->prereset can never be reliable.  It's sometimes nice
 * to wait there but it can't be depended upon; otherwise, we wouldn't
 * be resetting.  Just give it enough time for most drives to spin up.
 */
enum {
	ATA_EH_PRERESET_TIMEOUT		= 10 * HZ,
	ATA_EH_FASTDRAIN_INTERVAL	= 3 * HZ,
};

/* The following table determines how we sequence resets.  Each entry
 * represents timeout for that try.  The first try can be soft or
 * hardreset.  All others are hardreset if available.  In most cases
 * the first reset w/ 10sec timeout should succeed.  Following entries
 * are mostly for error handling, hotplug and retarded devices.
 */
static const unsigned long ata_eh_reset_timeouts[] = {
	10 * HZ,	/* most drives spin up by 10sec */
	10 * HZ,	/* > 99% working drives spin up before 20sec */
	35 * HZ,	/* give > 30 secs of idleness for retarded devices */
	5 * HZ,		/* and sweet one last chance */
	/* > 1 min has elapsed, give up */
};

static void __ata_port_freeze(struct ata_port *ap);
#ifdef CONFIG_PM
static void ata_eh_handle_port_suspend(struct ata_port *ap);
static void ata_eh_handle_port_resume(struct ata_port *ap);
#else /* CONFIG_PM */
static void ata_eh_handle_port_suspend(struct ata_port *ap)
{ }

static void ata_eh_handle_port_resume(struct ata_port *ap)
{ }
#endif /* CONFIG_PM */

static void __ata_ehi_pushv_desc(struct ata_eh_info *ehi, const char *fmt,
				 va_list args)
{
	ehi->desc_len += vscnprintf(ehi->desc + ehi->desc_len,
				     ATA_EH_DESC_LEN - ehi->desc_len,
				     fmt, args);
}

/**
 *	__ata_ehi_push_desc - push error description without adding separator
 *	@ehi: target EHI
 *	@fmt: printf format string
 *
 *	Format string according to @fmt and append it to @ehi->desc.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
void __ata_ehi_push_desc(struct ata_eh_info *ehi, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	__ata_ehi_pushv_desc(ehi, fmt, args);
	va_end(args);
}

/**
 *	ata_ehi_push_desc - push error description with separator
 *	@ehi: target EHI
 *	@fmt: printf format string
 *
 *	Format string according to @fmt and append it to @ehi->desc.
 *	If @ehi->desc is not empty, ", " is added in-between.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
void ata_ehi_push_desc(struct ata_eh_info *ehi, const char *fmt, ...)
{
	va_list args;

	if (ehi->desc_len)
		__ata_ehi_push_desc(ehi, ", ");

	va_start(args, fmt);
	__ata_ehi_pushv_desc(ehi, fmt, args);
	va_end(args);
}

/**
 *	ata_ehi_clear_desc - clean error description
 *	@ehi: target EHI
 *
 *	Clear @ehi->desc.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
void ata_ehi_clear_desc(struct ata_eh_info *ehi)
{
	ehi->desc[0] = '\0';
	ehi->desc_len = 0;
}

/**
 *	ata_port_desc - append port description
 *	@ap: target ATA port
 *	@fmt: printf format string
 *
 *	Format string according to @fmt and append it to port
 *	description.  If port description is not empty, " " is added
 *	in-between.  This function is to be used while initializing
 *	ata_host.  The description is printed on host registration.
 *
 *	LOCKING:
 *	None.
 */
void ata_port_desc(struct ata_port *ap, const char *fmt, ...)
{
	va_list args;

	WARN_ON(!(ap->pflags & ATA_PFLAG_INITIALIZING));

	if (ap->link.eh_info.desc_len)
		__ata_ehi_push_desc(&ap->link.eh_info, " ");

	va_start(args, fmt);
	__ata_ehi_pushv_desc(&ap->link.eh_info, fmt, args);
	va_end(args);
}

#ifdef CONFIG_PCI

/**
 *	ata_port_pbar_desc - append PCI BAR description
 *	@ap: target ATA port
 *	@bar: target PCI BAR
 *	@offset: offset into PCI BAR
 *	@name: name of the area
 *
 *	If @offset is negative, this function formats a string which
 *	contains the name, address, size and type of the BAR and
 *	appends it to the port description.  If @offset is zero or
 *	positive, only name and offsetted address is appended.
 *
 *	LOCKING:
 *	None.
 */
void ata_port_pbar_desc(struct ata_port *ap, int bar, ssize_t offset,
			const char *name)
{
	struct pci_dev *pdev = to_pci_dev(ap->host->dev);
	char *type = "";
	unsigned long long start, len;

	if (pci_resource_flags(pdev, bar) & IORESOURCE_MEM)
		type = "m";
	else if (pci_resource_flags(pdev, bar) & IORESOURCE_IO)
		type = "i";

	start = (unsigned long long)pci_resource_start(pdev, bar);
	len = (unsigned long long)pci_resource_len(pdev, bar);

	if (offset < 0)
		ata_port_desc(ap, "%s %s%llu@0x%llx", name, type, len, start);
	else
		ata_port_desc(ap, "%s 0x%llx", name,
				start + (unsigned long long)offset);
}

#endif /* CONFIG_PCI */

static void ata_ering_record(struct ata_ering *ering, unsigned int eflags,
			     unsigned int err_mask)
{
	struct ata_ering_entry *ent;

	WARN_ON(!err_mask);

	ering->cursor++;
	ering->cursor %= ATA_ERING_SIZE;

	ent = &ering->ring[ering->cursor];
	ent->eflags = eflags;
	ent->err_mask = err_mask;
	ent->timestamp = get_jiffies_64();
}

static struct ata_ering_entry *ata_ering_top(struct ata_ering *ering)
{
	struct ata_ering_entry *ent = &ering->ring[ering->cursor];

	if (ent->err_mask)
		return ent;
	return NULL;
}

static void ata_ering_clear(struct ata_ering *ering)
{
	memset(ering, 0, sizeof(*ering));
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

static unsigned int ata_eh_dev_action(struct ata_device *dev)
{
	struct ata_eh_context *ehc = &dev->link->eh_context;

	return ehc->i.action | ehc->i.dev_action[dev->devno];
}

static void ata_eh_clear_action(struct ata_link *link, struct ata_device *dev,
				struct ata_eh_info *ehi, unsigned int action)
{
	struct ata_device *tdev;

	if (!dev) {
		ehi->action &= ~action;
		ata_link_for_each_dev(tdev, link)
			ehi->dev_action[tdev->devno] &= ~action;
	} else {
		/* doesn't make sense for port-wide EH actions */
		WARN_ON(!(action & ATA_EH_PERDEV_MASK));

		/* break ehi->action into ehi->dev_action */
		if (ehi->action & action) {
			ata_link_for_each_dev(tdev, link)
				ehi->dev_action[tdev->devno] |=
					ehi->action & action;
			ehi->action &= ~action;
		}

		/* turn off the specified per-dev action */
		ehi->dev_action[dev->devno] &= ~action;
	}
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
	spin_lock_irqsave(ap->lock, flags);
	qc = ata_qc_from_tag(ap, ap->link.active_tag);
	if (qc) {
		WARN_ON(qc->scsicmd != cmd);
		qc->flags |= ATA_QCFLAG_EH_SCHEDULED;
		qc->err_mask |= AC_ERR_TIMEOUT;
		ret = EH_NOT_HANDLED;
	}
	spin_unlock_irqrestore(ap->lock, flags);

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
	int i;
	unsigned long flags;

	DPRINTK("ENTER\n");

	/* synchronize with port task */
	ata_port_flush_task(ap);

	/* synchronize with host lock and sort out timeouts */

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

		spin_lock_irqsave(ap->lock, flags);

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

		spin_unlock_irqrestore(ap->lock, flags);

		/* initialize eh_tries */
		ap->eh_tries = ATA_EH_MAX_TRIES;
	} else
		spin_unlock_wait(ap->lock);

 repeat:
	/* invoke error handler */
	if (ap->ops->error_handler) {
		struct ata_link *link;

		/* kill fast drain timer */
		del_timer_sync(&ap->fastdrain_timer);

		/* process port resume request */
		ata_eh_handle_port_resume(ap);

		/* fetch & clear EH info */
		spin_lock_irqsave(ap->lock, flags);

		__ata_port_for_each_link(link, ap) {
			struct ata_eh_context *ehc = &link->eh_context;
			struct ata_device *dev;

			memset(&link->eh_context, 0, sizeof(link->eh_context));
			link->eh_context.i = link->eh_info;
			memset(&link->eh_info, 0, sizeof(link->eh_info));

			ata_link_for_each_dev(dev, link) {
				int devno = dev->devno;

				ehc->saved_xfer_mode[devno] = dev->xfer_mode;
				if (ata_ncq_enabled(dev))
					ehc->saved_ncq_enabled |= 1 << devno;
			}
		}

		ap->pflags |= ATA_PFLAG_EH_IN_PROGRESS;
		ap->pflags &= ~ATA_PFLAG_EH_PENDING;
		ap->excl_link = NULL;	/* don't maintain exclusion over EH */

		spin_unlock_irqrestore(ap->lock, flags);

		/* invoke EH, skip if unloading or suspended */
		if (!(ap->pflags & (ATA_PFLAG_UNLOADING | ATA_PFLAG_SUSPENDED)))
			ap->ops->error_handler(ap);
		else
			ata_eh_finish(ap);

		/* process port suspend request */
		ata_eh_handle_port_suspend(ap);

		/* Exception might have happend after ->error_handler
		 * recovered the port but before this point.  Repeat
		 * EH in such case.
		 */
		spin_lock_irqsave(ap->lock, flags);

		if (ap->pflags & ATA_PFLAG_EH_PENDING) {
			if (--ap->eh_tries) {
				spin_unlock_irqrestore(ap->lock, flags);
				goto repeat;
			}
			ata_port_printk(ap, KERN_ERR, "EH pending after %d "
					"tries, giving up\n", ATA_EH_MAX_TRIES);
			ap->pflags &= ~ATA_PFLAG_EH_PENDING;
		}

		/* this run is complete, make sure EH info is clear */
		__ata_port_for_each_link(link, ap)
			memset(&link->eh_info, 0, sizeof(link->eh_info));

		/* Clear host_eh_scheduled while holding ap->lock such
		 * that if exception occurs after this point but
		 * before EH completion, SCSI midlayer will
		 * re-initiate EH.
		 */
		host->host_eh_scheduled = 0;

		spin_unlock_irqrestore(ap->lock, flags);
	} else {
		WARN_ON(ata_qc_from_tag(ap, ap->link.active_tag) == NULL);
		ap->ops->eng_timeout(ap);
	}

	/* finish or retry handled scmd's and clean up */
	WARN_ON(host->host_failed || !list_empty(&host->eh_cmd_q));

	scsi_eh_flush_done_q(&ap->eh_done_q);

	/* clean up */
	spin_lock_irqsave(ap->lock, flags);

	if (ap->pflags & ATA_PFLAG_LOADING)
		ap->pflags &= ~ATA_PFLAG_LOADING;
	else if (ap->pflags & ATA_PFLAG_SCSI_HOTPLUG)
		queue_delayed_work(ata_aux_wq, &ap->hotplug_task, 0);

	if (ap->pflags & ATA_PFLAG_RECOVERED)
		ata_port_printk(ap, KERN_INFO, "EH complete\n");

	ap->pflags &= ~(ATA_PFLAG_SCSI_HOTPLUG | ATA_PFLAG_RECOVERED);

	/* tell wait_eh that we're done */
	ap->pflags &= ~ATA_PFLAG_EH_IN_PROGRESS;
	wake_up_all(&ap->eh_wait_q);

	spin_unlock_irqrestore(ap->lock, flags);

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
	spin_lock_irqsave(ap->lock, flags);

	while (ap->pflags & (ATA_PFLAG_EH_PENDING | ATA_PFLAG_EH_IN_PROGRESS)) {
		prepare_to_wait(&ap->eh_wait_q, &wait, TASK_UNINTERRUPTIBLE);
		spin_unlock_irqrestore(ap->lock, flags);
		schedule();
		spin_lock_irqsave(ap->lock, flags);
	}
	finish_wait(&ap->eh_wait_q, &wait);

	spin_unlock_irqrestore(ap->lock, flags);

	/* make sure SCSI EH is complete */
	if (scsi_host_in_recovery(ap->scsi_host)) {
		msleep(10);
		goto retry;
	}
}

static int ata_eh_nr_in_flight(struct ata_port *ap)
{
	unsigned int tag;
	int nr = 0;

	/* count only non-internal commands */
	for (tag = 0; tag < ATA_MAX_QUEUE - 1; tag++)
		if (ata_qc_from_tag(ap, tag))
			nr++;

	return nr;
}

void ata_eh_fastdrain_timerfn(unsigned long arg)
{
	struct ata_port *ap = (void *)arg;
	unsigned long flags;
	int cnt;

	spin_lock_irqsave(ap->lock, flags);

	cnt = ata_eh_nr_in_flight(ap);

	/* are we done? */
	if (!cnt)
		goto out_unlock;

	if (cnt == ap->fastdrain_cnt) {
		unsigned int tag;

		/* No progress during the last interval, tag all
		 * in-flight qcs as timed out and freeze the port.
		 */
		for (tag = 0; tag < ATA_MAX_QUEUE - 1; tag++) {
			struct ata_queued_cmd *qc = ata_qc_from_tag(ap, tag);
			if (qc)
				qc->err_mask |= AC_ERR_TIMEOUT;
		}

		ata_port_freeze(ap);
	} else {
		/* some qcs have finished, give it another chance */
		ap->fastdrain_cnt = cnt;
		ap->fastdrain_timer.expires =
			jiffies + ATA_EH_FASTDRAIN_INTERVAL;
		add_timer(&ap->fastdrain_timer);
	}

 out_unlock:
	spin_unlock_irqrestore(ap->lock, flags);
}

/**
 *	ata_eh_set_pending - set ATA_PFLAG_EH_PENDING and activate fast drain
 *	@ap: target ATA port
 *	@fastdrain: activate fast drain
 *
 *	Set ATA_PFLAG_EH_PENDING and activate fast drain if @fastdrain
 *	is non-zero and EH wasn't pending before.  Fast drain ensures
 *	that EH kicks in in timely manner.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
static void ata_eh_set_pending(struct ata_port *ap, int fastdrain)
{
	int cnt;

	/* already scheduled? */
	if (ap->pflags & ATA_PFLAG_EH_PENDING)
		return;

	ap->pflags |= ATA_PFLAG_EH_PENDING;

	if (!fastdrain)
		return;

	/* do we have in-flight qcs? */
	cnt = ata_eh_nr_in_flight(ap);
	if (!cnt)
		return;

	/* activate fast drain */
	ap->fastdrain_cnt = cnt;
	ap->fastdrain_timer.expires = jiffies + ATA_EH_FASTDRAIN_INTERVAL;
	add_timer(&ap->fastdrain_timer);
}

/**
 *	ata_qc_schedule_eh - schedule qc for error handling
 *	@qc: command to schedule error handling for
 *
 *	Schedule error handling for @qc.  EH will kick in as soon as
 *	other commands are drained.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
void ata_qc_schedule_eh(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	WARN_ON(!ap->ops->error_handler);

	qc->flags |= ATA_QCFLAG_FAILED;
	ata_eh_set_pending(ap, 1);

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
 *	spin_lock_irqsave(host lock)
 */
void ata_port_schedule_eh(struct ata_port *ap)
{
	WARN_ON(!ap->ops->error_handler);

	if (ap->pflags & ATA_PFLAG_INITIALIZING)
		return;

	ata_eh_set_pending(ap, 1);
	scsi_schedule_eh(ap->scsi_host);

	DPRINTK("port EH scheduled\n");
}

static int ata_do_link_abort(struct ata_port *ap, struct ata_link *link)
{
	int tag, nr_aborted = 0;

	WARN_ON(!ap->ops->error_handler);

	/* we're gonna abort all commands, no need for fast drain */
	ata_eh_set_pending(ap, 0);

	for (tag = 0; tag < ATA_MAX_QUEUE; tag++) {
		struct ata_queued_cmd *qc = ata_qc_from_tag(ap, tag);

		if (qc && (!link || qc->dev->link == link)) {
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
 *	ata_link_abort - abort all qc's on the link
 *	@link: ATA link to abort qc's for
 *
 *	Abort all active qc's active on @link and schedule EH.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	Number of aborted qc's.
 */
int ata_link_abort(struct ata_link *link)
{
	return ata_do_link_abort(link->ap, link);
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
	return ata_do_link_abort(ap, NULL);
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
 *	spin_lock_irqsave(host lock)
 */
static void __ata_port_freeze(struct ata_port *ap)
{
	WARN_ON(!ap->ops->error_handler);

	if (ap->ops->freeze)
		ap->ops->freeze(ap);

	ap->pflags |= ATA_PFLAG_FROZEN;

	DPRINTK("ata%u port frozen\n", ap->print_id);
}

/**
 *	ata_port_freeze - abort & freeze port
 *	@ap: ATA port to freeze
 *
 *	Abort and freeze @ap.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
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
 *	sata_async_notification - SATA async notification handler
 *	@ap: ATA port where async notification is received
 *
 *	Handler to be called when async notification via SDB FIS is
 *	received.  This function schedules EH if necessary.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	1 if EH is scheduled, 0 otherwise.
 */
int sata_async_notification(struct ata_port *ap)
{
	u32 sntf;
	int rc;

	if (!(ap->flags & ATA_FLAG_AN))
		return 0;

	rc = sata_scr_read(&ap->link, SCR_NOTIFICATION, &sntf);
	if (rc == 0)
		sata_scr_write(&ap->link, SCR_NOTIFICATION, sntf);

	if (!sata_pmp_attached(ap) || rc) {
		/* PMP is not attached or SNTF is not available */
		if (!sata_pmp_attached(ap)) {
			/* PMP is not attached.  Check whether ATAPI
			 * AN is configured.  If so, notify media
			 * change.
			 */
			struct ata_device *dev = ap->link.device;

			if ((dev->class == ATA_DEV_ATAPI) &&
			    (dev->flags & ATA_DFLAG_AN))
				ata_scsi_media_change_notify(dev);
			return 0;
		} else {
			/* PMP is attached but SNTF is not available.
			 * ATAPI async media change notification is
			 * not used.  The PMP must be reporting PHY
			 * status change, schedule EH.
			 */
			ata_port_schedule_eh(ap);
			return 1;
		}
	} else {
		/* PMP is attached and SNTF is available */
		struct ata_link *link;

		/* check and notify ATAPI AN */
		ata_port_for_each_link(link, ap) {
			if (!(sntf & (1 << link->pmp)))
				continue;

			if ((link->device->class == ATA_DEV_ATAPI) &&
			    (link->device->flags & ATA_DFLAG_AN))
				ata_scsi_media_change_notify(link->device);
		}

		/* If PMP is reporting that PHY status of some
		 * downstream ports has changed, schedule EH.
		 */
		if (sntf & (1 << SATA_PMP_CTRL_PORT)) {
			ata_port_schedule_eh(ap);
			return 1;
		}

		return 0;
	}
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

	spin_lock_irqsave(ap->lock, flags);
	__ata_port_freeze(ap);
	spin_unlock_irqrestore(ap->lock, flags);
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

	spin_lock_irqsave(ap->lock, flags);

	ap->pflags &= ~ATA_PFLAG_FROZEN;

	if (ap->ops->thaw)
		ap->ops->thaw(ap);

	spin_unlock_irqrestore(ap->lock, flags);

	DPRINTK("ata%u port thawed\n", ap->print_id);
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

	spin_lock_irqsave(ap->lock, flags);
	qc->scsidone = ata_eh_scsidone;
	__ata_qc_complete(qc);
	WARN_ON(ata_tag_valid(qc->tag));
	spin_unlock_irqrestore(ap->lock, flags);

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
 *	ata_eh_detach_dev - detach ATA device
 *	@dev: ATA device to detach
 *
 *	Detach @dev.
 *
 *	LOCKING:
 *	None.
 */
void ata_eh_detach_dev(struct ata_device *dev)
{
	struct ata_link *link = dev->link;
	struct ata_port *ap = link->ap;
	unsigned long flags;

	ata_dev_disable(dev);

	spin_lock_irqsave(ap->lock, flags);

	dev->flags &= ~ATA_DFLAG_DETACH;

	if (ata_scsi_offline_dev(dev)) {
		dev->flags |= ATA_DFLAG_DETACHED;
		ap->pflags |= ATA_PFLAG_SCSI_HOTPLUG;
	}

	/* clear per-dev EH actions */
	ata_eh_clear_action(link, dev, &link->eh_info, ATA_EH_PERDEV_MASK);
	ata_eh_clear_action(link, dev, &link->eh_context.i, ATA_EH_PERDEV_MASK);

	spin_unlock_irqrestore(ap->lock, flags);
}

/**
 *	ata_eh_about_to_do - about to perform eh_action
 *	@link: target ATA link
 *	@dev: target ATA dev for per-dev action (can be NULL)
 *	@action: action about to be performed
 *
 *	Called just before performing EH actions to clear related bits
 *	in @link->eh_info such that eh actions are not unnecessarily
 *	repeated.
 *
 *	LOCKING:
 *	None.
 */
void ata_eh_about_to_do(struct ata_link *link, struct ata_device *dev,
			unsigned int action)
{
	struct ata_port *ap = link->ap;
	struct ata_eh_info *ehi = &link->eh_info;
	struct ata_eh_context *ehc = &link->eh_context;
	unsigned long flags;

	spin_lock_irqsave(ap->lock, flags);

	ata_eh_clear_action(link, dev, ehi, action);

	if (!(ehc->i.flags & ATA_EHI_QUIET))
		ap->pflags |= ATA_PFLAG_RECOVERED;

	spin_unlock_irqrestore(ap->lock, flags);
}

/**
 *	ata_eh_done - EH action complete
*	@ap: target ATA port
 *	@dev: target ATA dev for per-dev action (can be NULL)
 *	@action: action just completed
 *
 *	Called right after performing EH actions to clear related bits
 *	in @link->eh_context.
 *
 *	LOCKING:
 *	None.
 */
void ata_eh_done(struct ata_link *link, struct ata_device *dev,
		 unsigned int action)
{
	struct ata_eh_context *ehc = &link->eh_context;

	ata_eh_clear_action(link, dev, &ehc->i, action);
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
static const char *ata_err_string(unsigned int err_mask)
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
				     buf, sectors * ATA_SECT_SIZE, 0);

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
	u8 *buf = dev->link->ap->sector_buf;
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
static unsigned int atapi_eh_request_sense(struct ata_queued_cmd *qc)
{
	struct ata_device *dev = qc->dev;
	unsigned char *sense_buf = qc->scsicmd->sense_buffer;
	struct ata_port *ap = dev->link->ap;
	struct ata_taskfile tf;
	u8 cdb[ATAPI_CDB_LEN];

	DPRINTK("ATAPI request sense\n");

	/* FIXME: is this needed? */
	memset(sense_buf, 0, SCSI_SENSE_BUFFERSIZE);

	/* initialize sense_buf with the error register,
	 * for the case where they are -not- overwritten
	 */
	sense_buf[0] = 0x70;
	sense_buf[2] = qc->result_tf.feature >> 4;

	/* some devices time out if garbage left in tf */
	ata_tf_init(dev, &tf);

	memset(cdb, 0, ATAPI_CDB_LEN);
	cdb[0] = REQUEST_SENSE;
	cdb[4] = SCSI_SENSE_BUFFERSIZE;

	tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	tf.command = ATA_CMD_PACKET;

	/* is it pointless to prefer PIO for "safety reasons"? */
	if (ap->flags & ATA_FLAG_PIO_DMA) {
		tf.protocol = ATAPI_PROT_DMA;
		tf.feature |= ATAPI_PKT_DMA;
	} else {
		tf.protocol = ATAPI_PROT_PIO;
		tf.lbam = SCSI_SENSE_BUFFERSIZE;
		tf.lbah = 0;
	}

	return ata_exec_internal(dev, &tf, cdb, DMA_FROM_DEVICE,
				 sense_buf, SCSI_SENSE_BUFFERSIZE, 0);
}

/**
 *	ata_eh_analyze_serror - analyze SError for a failed port
 *	@link: ATA link to analyze SError for
 *
 *	Analyze SError if available and further determine cause of
 *	failure.
 *
 *	LOCKING:
 *	None.
 */
static void ata_eh_analyze_serror(struct ata_link *link)
{
	struct ata_eh_context *ehc = &link->eh_context;
	u32 serror = ehc->i.serror;
	unsigned int err_mask = 0, action = 0;
	u32 hotplug_mask;

	if (serror & SERR_PERSISTENT) {
		err_mask |= AC_ERR_ATA_BUS;
		action |= ATA_EH_RESET;
	}
	if (serror &
	    (SERR_DATA_RECOVERED | SERR_COMM_RECOVERED | SERR_DATA)) {
		err_mask |= AC_ERR_ATA_BUS;
		action |= ATA_EH_RESET;
	}
	if (serror & SERR_PROTOCOL) {
		err_mask |= AC_ERR_HSM;
		action |= ATA_EH_RESET;
	}
	if (serror & SERR_INTERNAL) {
		err_mask |= AC_ERR_SYSTEM;
		action |= ATA_EH_RESET;
	}

	/* Determine whether a hotplug event has occurred.  Both
	 * SError.N/X are considered hotplug events for enabled or
	 * host links.  For disabled PMP links, only N bit is
	 * considered as X bit is left at 1 for link plugging.
	 */
	hotplug_mask = 0;

	if (!(link->flags & ATA_LFLAG_DISABLED) || ata_is_host_link(link))
		hotplug_mask = SERR_PHYRDY_CHG | SERR_DEV_XCHG;
	else
		hotplug_mask = SERR_PHYRDY_CHG;

	if (serror & hotplug_mask)
		ata_ehi_hotplugged(&ehc->i);

	ehc->i.err_mask |= err_mask;
	ehc->i.action |= action;
}

/**
 *	ata_eh_analyze_ncq_error - analyze NCQ error
 *	@link: ATA link to analyze NCQ error for
 *
 *	Read log page 10h, determine the offending qc and acquire
 *	error status TF.  For NCQ device errors, all LLDDs have to do
 *	is setting AC_ERR_DEV in ehi->err_mask.  This function takes
 *	care of the rest.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 */
static void ata_eh_analyze_ncq_error(struct ata_link *link)
{
	struct ata_port *ap = link->ap;
	struct ata_eh_context *ehc = &link->eh_context;
	struct ata_device *dev = link->device;
	struct ata_queued_cmd *qc;
	struct ata_taskfile tf;
	int tag, rc;

	/* if frozen, we can't do much */
	if (ap->pflags & ATA_PFLAG_FROZEN)
		return;

	/* is it NCQ device error? */
	if (!link->sactive || !(ehc->i.err_mask & AC_ERR_DEV))
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
		ata_link_printk(link, KERN_ERR, "failed to read log page 10h "
				"(errno=%d)\n", rc);
		return;
	}

	if (!(link->sactive & (1 << tag))) {
		ata_link_printk(link, KERN_ERR, "log page 10h reported "
				"inactive tag %d\n", tag);
		return;
	}

	/* we've got the perpetrator, condemn it */
	qc = __ata_qc_from_tag(ap, tag);
	memcpy(&qc->result_tf, &tf, sizeof(tf));
	qc->err_mask |= AC_ERR_DEV | AC_ERR_NCQ;
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
		return ATA_EH_RESET;
	}

	if (stat & (ATA_ERR | ATA_DF))
		qc->err_mask |= AC_ERR_DEV;
	else
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
		if (!(qc->ap->pflags & ATA_PFLAG_FROZEN)) {
			tmp = atapi_eh_request_sense(qc);
			if (!tmp) {
				/* ATA_QCFLAG_SENSE_VALID is used to
				 * tell atapi_qc_complete() that sense
				 * data is already valid.
				 *
				 * TODO: interpret sense data and set
				 * appropriate err_mask.
				 */
				qc->flags |= ATA_QCFLAG_SENSE_VALID;
			} else
				qc->err_mask |= tmp;
		}
	}

	if (qc->err_mask & (AC_ERR_HSM | AC_ERR_TIMEOUT | AC_ERR_ATA_BUS))
		action |= ATA_EH_RESET;

	return action;
}

static int ata_eh_categorize_error(unsigned int eflags, unsigned int err_mask,
				   int *xfer_ok)
{
	int base = 0;

	if (!(eflags & ATA_EFLAG_DUBIOUS_XFER))
		*xfer_ok = 1;

	if (!*xfer_ok)
		base = ATA_ECAT_DUBIOUS_NONE;

	if (err_mask & AC_ERR_ATA_BUS)
		return base + ATA_ECAT_ATA_BUS;

	if (err_mask & AC_ERR_TIMEOUT)
		return base + ATA_ECAT_TOUT_HSM;

	if (eflags & ATA_EFLAG_IS_IO) {
		if (err_mask & AC_ERR_HSM)
			return base + ATA_ECAT_TOUT_HSM;
		if ((err_mask &
		     (AC_ERR_DEV|AC_ERR_MEDIA|AC_ERR_INVALID)) == AC_ERR_DEV)
			return base + ATA_ECAT_UNK_DEV;
	}

	return 0;
}

struct speed_down_verdict_arg {
	u64 since;
	int xfer_ok;
	int nr_errors[ATA_ECAT_NR];
};

static int speed_down_verdict_cb(struct ata_ering_entry *ent, void *void_arg)
{
	struct speed_down_verdict_arg *arg = void_arg;
	int cat;

	if (ent->timestamp < arg->since)
		return -1;

	cat = ata_eh_categorize_error(ent->eflags, ent->err_mask,
				      &arg->xfer_ok);
	arg->nr_errors[cat]++;

	return 0;
}

/**
 *	ata_eh_speed_down_verdict - Determine speed down verdict
 *	@dev: Device of interest
 *
 *	This function examines error ring of @dev and determines
 *	whether NCQ needs to be turned off, transfer speed should be
 *	stepped down, or falling back to PIO is necessary.
 *
 *	ECAT_ATA_BUS	: ATA_BUS error for any command
 *
 *	ECAT_TOUT_HSM	: TIMEOUT for any command or HSM violation for
 *			  IO commands
 *
 *	ECAT_UNK_DEV	: Unknown DEV error for IO commands
 *
 *	ECAT_DUBIOUS_*	: Identical to above three but occurred while
 *			  data transfer hasn't been verified.
 *
 *	Verdicts are
 *
 *	NCQ_OFF		: Turn off NCQ.
 *
 *	SPEED_DOWN	: Speed down transfer speed but don't fall back
 *			  to PIO.
 *
 *	FALLBACK_TO_PIO	: Fall back to PIO.
 *
 *	Even if multiple verdicts are returned, only one action is
 *	taken per error.  An action triggered by non-DUBIOUS errors
 *	clears ering, while one triggered by DUBIOUS_* errors doesn't.
 *	This is to expedite speed down decisions right after device is
 *	initially configured.
 *
 *	The followings are speed down rules.  #1 and #2 deal with
 *	DUBIOUS errors.
 *
 *	1. If more than one DUBIOUS_ATA_BUS or DUBIOUS_TOUT_HSM errors
 *	   occurred during last 5 mins, SPEED_DOWN and FALLBACK_TO_PIO.
 *
 *	2. If more than one DUBIOUS_TOUT_HSM or DUBIOUS_UNK_DEV errors
 *	   occurred during last 5 mins, NCQ_OFF.
 *
 *	3. If more than 8 ATA_BUS, TOUT_HSM or UNK_DEV errors
 *	   ocurred during last 5 mins, FALLBACK_TO_PIO
 *
 *	4. If more than 3 TOUT_HSM or UNK_DEV errors occurred
 *	   during last 10 mins, NCQ_OFF.
 *
 *	5. If more than 3 ATA_BUS or TOUT_HSM errors, or more than 6
 *	   UNK_DEV errors occurred during last 10 mins, SPEED_DOWN.
 *
 *	LOCKING:
 *	Inherited from caller.
 *
 *	RETURNS:
 *	OR of ATA_EH_SPDN_* flags.
 */
static unsigned int ata_eh_speed_down_verdict(struct ata_device *dev)
{
	const u64 j5mins = 5LLU * 60 * HZ, j10mins = 10LLU * 60 * HZ;
	u64 j64 = get_jiffies_64();
	struct speed_down_verdict_arg arg;
	unsigned int verdict = 0;

	/* scan past 5 mins of error history */
	memset(&arg, 0, sizeof(arg));
	arg.since = j64 - min(j64, j5mins);
	ata_ering_map(&dev->ering, speed_down_verdict_cb, &arg);

	if (arg.nr_errors[ATA_ECAT_DUBIOUS_ATA_BUS] +
	    arg.nr_errors[ATA_ECAT_DUBIOUS_TOUT_HSM] > 1)
		verdict |= ATA_EH_SPDN_SPEED_DOWN |
			ATA_EH_SPDN_FALLBACK_TO_PIO | ATA_EH_SPDN_KEEP_ERRORS;

	if (arg.nr_errors[ATA_ECAT_DUBIOUS_TOUT_HSM] +
	    arg.nr_errors[ATA_ECAT_DUBIOUS_UNK_DEV] > 1)
		verdict |= ATA_EH_SPDN_NCQ_OFF | ATA_EH_SPDN_KEEP_ERRORS;

	if (arg.nr_errors[ATA_ECAT_ATA_BUS] +
	    arg.nr_errors[ATA_ECAT_TOUT_HSM] +
	    arg.nr_errors[ATA_ECAT_UNK_DEV] > 6)
		verdict |= ATA_EH_SPDN_FALLBACK_TO_PIO;

	/* scan past 10 mins of error history */
	memset(&arg, 0, sizeof(arg));
	arg.since = j64 - min(j64, j10mins);
	ata_ering_map(&dev->ering, speed_down_verdict_cb, &arg);

	if (arg.nr_errors[ATA_ECAT_TOUT_HSM] +
	    arg.nr_errors[ATA_ECAT_UNK_DEV] > 3)
		verdict |= ATA_EH_SPDN_NCQ_OFF;

	if (arg.nr_errors[ATA_ECAT_ATA_BUS] +
	    arg.nr_errors[ATA_ECAT_TOUT_HSM] > 3 ||
	    arg.nr_errors[ATA_ECAT_UNK_DEV] > 6)
		verdict |= ATA_EH_SPDN_SPEED_DOWN;

	return verdict;
}

/**
 *	ata_eh_speed_down - record error and speed down if necessary
 *	@dev: Failed device
 *	@eflags: mask of ATA_EFLAG_* flags
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
 *	Determined recovery action.
 */
static unsigned int ata_eh_speed_down(struct ata_device *dev,
				unsigned int eflags, unsigned int err_mask)
{
	struct ata_link *link = dev->link;
	int xfer_ok = 0;
	unsigned int verdict;
	unsigned int action = 0;

	/* don't bother if Cat-0 error */
	if (ata_eh_categorize_error(eflags, err_mask, &xfer_ok) == 0)
		return 0;

	/* record error and determine whether speed down is necessary */
	ata_ering_record(&dev->ering, eflags, err_mask);
	verdict = ata_eh_speed_down_verdict(dev);

	/* turn off NCQ? */
	if ((verdict & ATA_EH_SPDN_NCQ_OFF) &&
	    (dev->flags & (ATA_DFLAG_PIO | ATA_DFLAG_NCQ |
			   ATA_DFLAG_NCQ_OFF)) == ATA_DFLAG_NCQ) {
		dev->flags |= ATA_DFLAG_NCQ_OFF;
		ata_dev_printk(dev, KERN_WARNING,
			       "NCQ disabled due to excessive errors\n");
		goto done;
	}

	/* speed down? */
	if (verdict & ATA_EH_SPDN_SPEED_DOWN) {
		/* speed down SATA link speed if possible */
		if (sata_down_spd_limit(link) == 0) {
			action |= ATA_EH_RESET;
			goto done;
		}

		/* lower transfer mode */
		if (dev->spdn_cnt < 2) {
			static const int dma_dnxfer_sel[] =
				{ ATA_DNXFER_DMA, ATA_DNXFER_40C };
			static const int pio_dnxfer_sel[] =
				{ ATA_DNXFER_PIO, ATA_DNXFER_FORCE_PIO0 };
			int sel;

			if (dev->xfer_shift != ATA_SHIFT_PIO)
				sel = dma_dnxfer_sel[dev->spdn_cnt];
			else
				sel = pio_dnxfer_sel[dev->spdn_cnt];

			dev->spdn_cnt++;

			if (ata_down_xfermask_limit(dev, sel) == 0) {
				action |= ATA_EH_RESET;
				goto done;
			}
		}
	}

	/* Fall back to PIO?  Slowing down to PIO is meaningless for
	 * SATA ATA devices.  Consider it only for PATA and SATAPI.
	 */
	if ((verdict & ATA_EH_SPDN_FALLBACK_TO_PIO) && (dev->spdn_cnt >= 2) &&
	    (link->ap->cbl != ATA_CBL_SATA || dev->class == ATA_DEV_ATAPI) &&
	    (dev->xfer_shift != ATA_SHIFT_PIO)) {
		if (ata_down_xfermask_limit(dev, ATA_DNXFER_FORCE_PIO) == 0) {
			dev->spdn_cnt = 0;
			action |= ATA_EH_RESET;
			goto done;
		}
	}

	return 0;
 done:
	/* device has been slowed down, blow error history */
	if (!(verdict & ATA_EH_SPDN_KEEP_ERRORS))
		ata_ering_clear(&dev->ering);
	return action;
}

/**
 *	ata_eh_link_autopsy - analyze error and determine recovery action
 *	@link: host link to perform autopsy on
 *
 *	Analyze why @link failed and determine which recovery actions
 *	are needed.  This function also sets more detailed AC_ERR_*
 *	values and fills sense data for ATAPI CHECK SENSE.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 */
static void ata_eh_link_autopsy(struct ata_link *link)
{
	struct ata_port *ap = link->ap;
	struct ata_eh_context *ehc = &link->eh_context;
	struct ata_device *dev;
	unsigned int all_err_mask = 0, eflags = 0;
	int tag;
	u32 serror;
	int rc;

	DPRINTK("ENTER\n");

	if (ehc->i.flags & ATA_EHI_NO_AUTOPSY)
		return;

	/* obtain and analyze SError */
	rc = sata_scr_read(link, SCR_ERROR, &serror);
	if (rc == 0) {
		ehc->i.serror |= serror;
		ata_eh_analyze_serror(link);
	} else if (rc != -EOPNOTSUPP) {
		/* SError read failed, force reset and probing */
		ehc->i.probe_mask |= ATA_ALL_DEVICES;
		ehc->i.action |= ATA_EH_RESET;
		ehc->i.err_mask |= AC_ERR_OTHER;
	}

	/* analyze NCQ failure */
	ata_eh_analyze_ncq_error(link);

	/* any real error trumps AC_ERR_OTHER */
	if (ehc->i.err_mask & ~AC_ERR_OTHER)
		ehc->i.err_mask &= ~AC_ERR_OTHER;

	all_err_mask |= ehc->i.err_mask;

	for (tag = 0; tag < ATA_MAX_QUEUE; tag++) {
		struct ata_queued_cmd *qc = __ata_qc_from_tag(ap, tag);

		if (!(qc->flags & ATA_QCFLAG_FAILED) || qc->dev->link != link)
			continue;

		/* inherit upper level err_mask */
		qc->err_mask |= ehc->i.err_mask;

		/* analyze TF */
		ehc->i.action |= ata_eh_analyze_tf(qc, &qc->result_tf);

		/* DEV errors are probably spurious in case of ATA_BUS error */
		if (qc->err_mask & AC_ERR_ATA_BUS)
			qc->err_mask &= ~(AC_ERR_DEV | AC_ERR_MEDIA |
					  AC_ERR_INVALID);

		/* any real error trumps unknown error */
		if (qc->err_mask & ~AC_ERR_OTHER)
			qc->err_mask &= ~AC_ERR_OTHER;

		/* SENSE_VALID trumps dev/unknown error and revalidation */
		if (qc->flags & ATA_QCFLAG_SENSE_VALID)
			qc->err_mask &= ~(AC_ERR_DEV | AC_ERR_OTHER);

		/* determine whether the command is worth retrying */
		if (!(qc->err_mask & AC_ERR_INVALID) &&
		    ((qc->flags & ATA_QCFLAG_IO) || qc->err_mask != AC_ERR_DEV))
			qc->flags |= ATA_QCFLAG_RETRY;

		/* accumulate error info */
		ehc->i.dev = qc->dev;
		all_err_mask |= qc->err_mask;
		if (qc->flags & ATA_QCFLAG_IO)
			eflags |= ATA_EFLAG_IS_IO;
	}

	/* enforce default EH actions */
	if (ap->pflags & ATA_PFLAG_FROZEN ||
	    all_err_mask & (AC_ERR_HSM | AC_ERR_TIMEOUT))
		ehc->i.action |= ATA_EH_RESET;
	else if (((eflags & ATA_EFLAG_IS_IO) && all_err_mask) ||
		 (!(eflags & ATA_EFLAG_IS_IO) && (all_err_mask & ~AC_ERR_DEV)))
		ehc->i.action |= ATA_EH_REVALIDATE;

	/* If we have offending qcs and the associated failed device,
	 * perform per-dev EH action only on the offending device.
	 */
	if (ehc->i.dev) {
		ehc->i.dev_action[ehc->i.dev->devno] |=
			ehc->i.action & ATA_EH_PERDEV_MASK;
		ehc->i.action &= ~ATA_EH_PERDEV_MASK;
	}

	/* propagate timeout to host link */
	if ((all_err_mask & AC_ERR_TIMEOUT) && !ata_is_host_link(link))
		ap->link.eh_context.i.err_mask |= AC_ERR_TIMEOUT;

	/* record error and consider speeding down */
	dev = ehc->i.dev;
	if (!dev && ((ata_link_max_devices(link) == 1 &&
		      ata_dev_enabled(link->device))))
	    dev = link->device;

	if (dev) {
		if (dev->flags & ATA_DFLAG_DUBIOUS_XFER)
			eflags |= ATA_EFLAG_DUBIOUS_XFER;
		ehc->i.action |= ata_eh_speed_down(dev, eflags, all_err_mask);
	}

	DPRINTK("EXIT\n");
}

/**
 *	ata_eh_autopsy - analyze error and determine recovery action
 *	@ap: host port to perform autopsy on
 *
 *	Analyze all links of @ap and determine why they failed and
 *	which recovery actions are needed.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 */
void ata_eh_autopsy(struct ata_port *ap)
{
	struct ata_link *link;

	ata_port_for_each_link(link, ap)
		ata_eh_link_autopsy(link);

	/* Autopsy of fanout ports can affect host link autopsy.
	 * Perform host link autopsy last.
	 */
	if (sata_pmp_attached(ap))
		ata_eh_link_autopsy(&ap->link);
}

/**
 *	ata_eh_link_report - report error handling to user
 *	@link: ATA link EH is going on
 *
 *	Report EH to user.
 *
 *	LOCKING:
 *	None.
 */
static void ata_eh_link_report(struct ata_link *link)
{
	struct ata_port *ap = link->ap;
	struct ata_eh_context *ehc = &link->eh_context;
	const char *frozen, *desc;
	char tries_buf[6];
	int tag, nr_failed = 0;

	if (ehc->i.flags & ATA_EHI_QUIET)
		return;

	desc = NULL;
	if (ehc->i.desc[0] != '\0')
		desc = ehc->i.desc;

	for (tag = 0; tag < ATA_MAX_QUEUE; tag++) {
		struct ata_queued_cmd *qc = __ata_qc_from_tag(ap, tag);

		if (!(qc->flags & ATA_QCFLAG_FAILED) || qc->dev->link != link ||
		    ((qc->flags & ATA_QCFLAG_QUIET) &&
		     qc->err_mask == AC_ERR_DEV))
			continue;
		if (qc->flags & ATA_QCFLAG_SENSE_VALID && !qc->err_mask)
			continue;

		nr_failed++;
	}

	if (!nr_failed && !ehc->i.err_mask)
		return;

	frozen = "";
	if (ap->pflags & ATA_PFLAG_FROZEN)
		frozen = " frozen";

	memset(tries_buf, 0, sizeof(tries_buf));
	if (ap->eh_tries < ATA_EH_MAX_TRIES)
		snprintf(tries_buf, sizeof(tries_buf) - 1, " t%d",
			 ap->eh_tries);

	if (ehc->i.dev) {
		ata_dev_printk(ehc->i.dev, KERN_ERR, "exception Emask 0x%x "
			       "SAct 0x%x SErr 0x%x action 0x%x%s%s\n",
			       ehc->i.err_mask, link->sactive, ehc->i.serror,
			       ehc->i.action, frozen, tries_buf);
		if (desc)
			ata_dev_printk(ehc->i.dev, KERN_ERR, "%s\n", desc);
	} else {
		ata_link_printk(link, KERN_ERR, "exception Emask 0x%x "
				"SAct 0x%x SErr 0x%x action 0x%x%s%s\n",
				ehc->i.err_mask, link->sactive, ehc->i.serror,
				ehc->i.action, frozen, tries_buf);
		if (desc)
			ata_link_printk(link, KERN_ERR, "%s\n", desc);
	}

	if (ehc->i.serror)
		ata_port_printk(ap, KERN_ERR,
		  "SError: { %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s}\n",
		  ehc->i.serror & SERR_DATA_RECOVERED ? "RecovData " : "",
		  ehc->i.serror & SERR_COMM_RECOVERED ? "RecovComm " : "",
		  ehc->i.serror & SERR_DATA ? "UnrecovData " : "",
		  ehc->i.serror & SERR_PERSISTENT ? "Persist " : "",
		  ehc->i.serror & SERR_PROTOCOL ? "Proto " : "",
		  ehc->i.serror & SERR_INTERNAL ? "HostInt " : "",
		  ehc->i.serror & SERR_PHYRDY_CHG ? "PHYRdyChg " : "",
		  ehc->i.serror & SERR_PHY_INT_ERR ? "PHYInt " : "",
		  ehc->i.serror & SERR_COMM_WAKE ? "CommWake " : "",
		  ehc->i.serror & SERR_10B_8B_ERR ? "10B8B " : "",
		  ehc->i.serror & SERR_DISPARITY ? "Dispar " : "",
		  ehc->i.serror & SERR_CRC ? "BadCRC " : "",
		  ehc->i.serror & SERR_HANDSHAKE ? "Handshk " : "",
		  ehc->i.serror & SERR_LINK_SEQ_ERR ? "LinkSeq " : "",
		  ehc->i.serror & SERR_TRANS_ST_ERROR ? "TrStaTrns " : "",
		  ehc->i.serror & SERR_UNRECOG_FIS ? "UnrecFIS " : "",
		  ehc->i.serror & SERR_DEV_XCHG ? "DevExch " : "");

	for (tag = 0; tag < ATA_MAX_QUEUE; tag++) {
		struct ata_queued_cmd *qc = __ata_qc_from_tag(ap, tag);
		struct ata_taskfile *cmd = &qc->tf, *res = &qc->result_tf;
		const u8 *cdb = qc->cdb;
		char data_buf[20] = "";
		char cdb_buf[70] = "";

		if (!(qc->flags & ATA_QCFLAG_FAILED) ||
		    qc->dev->link != link || !qc->err_mask)
			continue;

		if (qc->dma_dir != DMA_NONE) {
			static const char *dma_str[] = {
				[DMA_BIDIRECTIONAL]	= "bidi",
				[DMA_TO_DEVICE]		= "out",
				[DMA_FROM_DEVICE]	= "in",
			};
			static const char *prot_str[] = {
				[ATA_PROT_PIO]		= "pio",
				[ATA_PROT_DMA]		= "dma",
				[ATA_PROT_NCQ]		= "ncq",
				[ATAPI_PROT_PIO]	= "pio",
				[ATAPI_PROT_DMA]	= "dma",
			};

			snprintf(data_buf, sizeof(data_buf), " %s %u %s",
				 prot_str[qc->tf.protocol], qc->nbytes,
				 dma_str[qc->dma_dir]);
		}

		if (ata_is_atapi(qc->tf.protocol))
			snprintf(cdb_buf, sizeof(cdb_buf),
				 "cdb %02x %02x %02x %02x %02x %02x %02x %02x  "
				 "%02x %02x %02x %02x %02x %02x %02x %02x\n         ",
				 cdb[0], cdb[1], cdb[2], cdb[3],
				 cdb[4], cdb[5], cdb[6], cdb[7],
				 cdb[8], cdb[9], cdb[10], cdb[11],
				 cdb[12], cdb[13], cdb[14], cdb[15]);

		ata_dev_printk(qc->dev, KERN_ERR,
			"cmd %02x/%02x:%02x:%02x:%02x:%02x/%02x:%02x:%02x:%02x:%02x/%02x "
			"tag %d%s\n         %s"
			"res %02x/%02x:%02x:%02x:%02x:%02x/%02x:%02x:%02x:%02x:%02x/%02x "
			"Emask 0x%x (%s)%s\n",
			cmd->command, cmd->feature, cmd->nsect,
			cmd->lbal, cmd->lbam, cmd->lbah,
			cmd->hob_feature, cmd->hob_nsect,
			cmd->hob_lbal, cmd->hob_lbam, cmd->hob_lbah,
			cmd->device, qc->tag, data_buf, cdb_buf,
			res->command, res->feature, res->nsect,
			res->lbal, res->lbam, res->lbah,
			res->hob_feature, res->hob_nsect,
			res->hob_lbal, res->hob_lbam, res->hob_lbah,
			res->device, qc->err_mask, ata_err_string(qc->err_mask),
			qc->err_mask & AC_ERR_NCQ ? " <F>" : "");

		if (res->command & (ATA_BUSY | ATA_DRDY | ATA_DF | ATA_DRQ |
				    ATA_ERR)) {
			if (res->command & ATA_BUSY)
				ata_dev_printk(qc->dev, KERN_ERR,
				  "status: { Busy }\n");
			else
				ata_dev_printk(qc->dev, KERN_ERR,
				  "status: { %s%s%s%s}\n",
				  res->command & ATA_DRDY ? "DRDY " : "",
				  res->command & ATA_DF ? "DF " : "",
				  res->command & ATA_DRQ ? "DRQ " : "",
				  res->command & ATA_ERR ? "ERR " : "");
		}

		if (cmd->command != ATA_CMD_PACKET &&
		    (res->feature & (ATA_ICRC | ATA_UNC | ATA_IDNF |
				     ATA_ABORTED)))
			ata_dev_printk(qc->dev, KERN_ERR,
			  "error: { %s%s%s%s}\n",
			  res->feature & ATA_ICRC ? "ICRC " : "",
			  res->feature & ATA_UNC ? "UNC " : "",
			  res->feature & ATA_IDNF ? "IDNF " : "",
			  res->feature & ATA_ABORTED ? "ABRT " : "");
	}
}

/**
 *	ata_eh_report - report error handling to user
 *	@ap: ATA port to report EH about
 *
 *	Report EH to user.
 *
 *	LOCKING:
 *	None.
 */
void ata_eh_report(struct ata_port *ap)
{
	struct ata_link *link;

	__ata_port_for_each_link(link, ap)
		ata_eh_link_report(link);
}

static int ata_do_reset(struct ata_link *link, ata_reset_fn_t reset,
			unsigned int *classes, unsigned long deadline)
{
	struct ata_device *dev;
	int rc;

	ata_link_for_each_dev(dev, link)
		classes[dev->devno] = ATA_DEV_UNKNOWN;

	rc = reset(link, classes, deadline);

	/* convert all ATA_DEV_UNKNOWN to ATA_DEV_NONE */
	ata_link_for_each_dev(dev, link)
		if (classes[dev->devno] == ATA_DEV_UNKNOWN)
			classes[dev->devno] = ATA_DEV_NONE;

	return rc;
}

static int ata_eh_followup_srst_needed(struct ata_link *link,
				       int rc, int classify,
				       const unsigned int *classes)
{
	if ((link->flags & ATA_LFLAG_NO_SRST) || ata_link_offline(link))
		return 0;
	if (rc == -EAGAIN) {
		if (classify)
			return 1;
		rc = 0;
	}
	if (rc != 0)
		return 0;
	if (sata_pmp_supported(link->ap) && ata_is_host_link(link))
		return 1;
	return 0;
}

int ata_eh_reset(struct ata_link *link, int classify,
		 ata_prereset_fn_t prereset, ata_reset_fn_t softreset,
		 ata_reset_fn_t hardreset, ata_postreset_fn_t postreset)
{
	const int max_tries = ARRAY_SIZE(ata_eh_reset_timeouts);
	struct ata_port *ap = link->ap;
	struct ata_eh_context *ehc = &link->eh_context;
	unsigned int *classes = ehc->classes;
	unsigned int lflags = link->flags;
	int verbose = !(ehc->i.flags & ATA_EHI_QUIET);
	int try = 0;
	struct ata_device *dev;
	unsigned long deadline, now;
	ata_reset_fn_t reset;
	unsigned long flags;
	u32 sstatus;
	int rc;

	/* about to reset */
	spin_lock_irqsave(ap->lock, flags);
	ap->pflags |= ATA_PFLAG_RESETTING;
	spin_unlock_irqrestore(ap->lock, flags);

	ata_eh_about_to_do(link, NULL, ATA_EH_RESET);

	ata_link_for_each_dev(dev, link) {
		/* If we issue an SRST then an ATA drive (not ATAPI)
		 * may change configuration and be in PIO0 timing. If
		 * we do a hard reset (or are coming from power on)
		 * this is true for ATA or ATAPI. Until we've set a
		 * suitable controller mode we should not touch the
		 * bus as we may be talking too fast.
		 */
		dev->pio_mode = XFER_PIO_0;

		/* If the controller has a pio mode setup function
		 * then use it to set the chipset to rights. Don't
		 * touch the DMA setup as that will be dealt with when
		 * configuring devices.
		 */
		if (ap->ops->set_piomode)
			ap->ops->set_piomode(ap, dev);
	}

	if (!softreset && !hardreset) {
		if (verbose)
			ata_link_printk(link, KERN_INFO, "no reset method "
					"available, skipping reset\n");
		if (!(lflags & ATA_LFLAG_ASSUME_CLASS))
			lflags |= ATA_LFLAG_ASSUME_ATA;
		goto done;
	}

	/* prefer hardreset */
	ehc->i.action &= ~ATA_EH_RESET;
	if (hardreset) {
		reset = hardreset;
		ehc->i.action = ATA_EH_HARDRESET;
	} else if (softreset) {
		reset = softreset;
		ehc->i.action = ATA_EH_SOFTRESET;
	} else {
		ata_link_printk(link, KERN_ERR, "BUG: no reset method, "
				"please report to linux-ide@vger.kernel.org\n");
		dump_stack();
		return -EINVAL;
	}

	if (prereset) {
		rc = prereset(link, jiffies + ATA_EH_PRERESET_TIMEOUT);
		if (rc) {
			if (rc == -ENOENT) {
				ata_link_printk(link, KERN_DEBUG,
						"port disabled. ignoring.\n");
				ehc->i.action &= ~ATA_EH_RESET;

				ata_link_for_each_dev(dev, link)
					classes[dev->devno] = ATA_DEV_NONE;

				rc = 0;
			} else
				ata_link_printk(link, KERN_ERR,
					"prereset failed (errno=%d)\n", rc);
			goto out;
		}
	}

	/* prereset() might have cleared ATA_EH_RESET */
	if (!(ehc->i.action & ATA_EH_RESET)) {
		/* prereset told us not to reset, bang classes and return */
		ata_link_for_each_dev(dev, link)
			classes[dev->devno] = ATA_DEV_NONE;
		rc = 0;
		goto out;
	}

 retry:
	deadline = jiffies + ata_eh_reset_timeouts[try++];

	/* shut up during boot probing */
	if (verbose)
		ata_link_printk(link, KERN_INFO, "%s resetting link\n",
				reset == softreset ? "soft" : "hard");

	/* mark that this EH session started with reset */
	if (reset == hardreset)
		ehc->i.flags |= ATA_EHI_DID_HARDRESET;
	else
		ehc->i.flags |= ATA_EHI_DID_SOFTRESET;

	rc = ata_do_reset(link, reset, classes, deadline);

	if (reset == hardreset &&
	    ata_eh_followup_srst_needed(link, rc, classify, classes)) {
		/* okay, let's do follow-up softreset */
		reset = softreset;

		if (!reset) {
			ata_link_printk(link, KERN_ERR,
					"follow-up softreset required "
					"but no softreset avaliable\n");
			rc = -EINVAL;
			goto fail;
		}

		ata_eh_about_to_do(link, NULL, ATA_EH_RESET);
		rc = ata_do_reset(link, reset, classes, deadline);
	}

	/* -EAGAIN can happen if we skipped followup SRST */
	if (rc && rc != -EAGAIN)
		goto fail;

 done:
	ata_link_for_each_dev(dev, link) {
		/* After the reset, the device state is PIO 0 and the
		 * controller state is undefined.  Reset also wakes up
		 * drives from sleeping mode.
		 */
		dev->pio_mode = XFER_PIO_0;
		dev->flags &= ~ATA_DFLAG_SLEEPING;

		if (ata_link_offline(link))
			continue;

		/* apply class override */
		if (lflags & ATA_LFLAG_ASSUME_ATA)
			classes[dev->devno] = ATA_DEV_ATA;
		else if (lflags & ATA_LFLAG_ASSUME_SEMB)
			classes[dev->devno] = ATA_DEV_SEMB_UNSUP; /* not yet */
	}

	/* record current link speed */
	if (sata_scr_read(link, SCR_STATUS, &sstatus) == 0)
		link->sata_spd = (sstatus >> 4) & 0xf;

	if (postreset)
		postreset(link, classes);

	/* reset successful, schedule revalidation */
	ata_eh_done(link, NULL, ATA_EH_RESET);
	ehc->i.action |= ATA_EH_REVALIDATE;

	rc = 0;
 out:
	/* clear hotplug flag */
	ehc->i.flags &= ~ATA_EHI_HOTPLUGGED;

	spin_lock_irqsave(ap->lock, flags);
	ap->pflags &= ~ATA_PFLAG_RESETTING;
	spin_unlock_irqrestore(ap->lock, flags);

	return rc;

 fail:
	/* if SCR isn't accessible on a fan-out port, PMP needs to be reset */
	if (!ata_is_host_link(link) &&
	    sata_scr_read(link, SCR_STATUS, &sstatus))
		rc = -ERESTART;

	if (rc == -ERESTART || try >= max_tries)
		goto out;

	now = jiffies;
	if (time_before(now, deadline)) {
		unsigned long delta = deadline - now;

		ata_link_printk(link, KERN_WARNING, "reset failed "
				"(errno=%d), retrying in %u secs\n",
				rc, (jiffies_to_msecs(delta) + 999) / 1000);

		while (delta)
			delta = schedule_timeout_uninterruptible(delta);
	}

	if (rc == -EPIPE || try == max_tries - 1)
		sata_down_spd_limit(link);
	if (hardreset)
		reset = hardreset;
	goto retry;
}

static int ata_eh_revalidate_and_attach(struct ata_link *link,
					struct ata_device **r_failed_dev)
{
	struct ata_port *ap = link->ap;
	struct ata_eh_context *ehc = &link->eh_context;
	struct ata_device *dev;
	unsigned int new_mask = 0;
	unsigned long flags;
	int rc = 0;

	DPRINTK("ENTER\n");

	/* For PATA drive side cable detection to work, IDENTIFY must
	 * be done backwards such that PDIAG- is released by the slave
	 * device before the master device is identified.
	 */
	ata_link_for_each_dev_reverse(dev, link) {
		unsigned int action = ata_eh_dev_action(dev);
		unsigned int readid_flags = 0;

		if (ehc->i.flags & ATA_EHI_DID_RESET)
			readid_flags |= ATA_READID_POSTRESET;

		if ((action & ATA_EH_REVALIDATE) && ata_dev_enabled(dev)) {
			WARN_ON(dev->class == ATA_DEV_PMP);

			if (ata_link_offline(link)) {
				rc = -EIO;
				goto err;
			}

			ata_eh_about_to_do(link, dev, ATA_EH_REVALIDATE);
			rc = ata_dev_revalidate(dev, ehc->classes[dev->devno],
						readid_flags);
			if (rc)
				goto err;

			ata_eh_done(link, dev, ATA_EH_REVALIDATE);

			/* Configuration may have changed, reconfigure
			 * transfer mode.
			 */
			ehc->i.flags |= ATA_EHI_SETMODE;

			/* schedule the scsi_rescan_device() here */
			queue_work(ata_aux_wq, &(ap->scsi_rescan_task));
		} else if (dev->class == ATA_DEV_UNKNOWN &&
			   ehc->tries[dev->devno] &&
			   ata_class_enabled(ehc->classes[dev->devno])) {
			dev->class = ehc->classes[dev->devno];

			if (dev->class == ATA_DEV_PMP)
				rc = sata_pmp_attach(dev);
			else
				rc = ata_dev_read_id(dev, &dev->class,
						     readid_flags, dev->id);
			switch (rc) {
			case 0:
				new_mask |= 1 << dev->devno;
				break;
			case -ENOENT:
				/* IDENTIFY was issued to non-existent
				 * device.  No need to reset.  Just
				 * thaw and kill the device.
				 */
				ata_eh_thaw_port(ap);
				dev->class = ATA_DEV_UNKNOWN;
				break;
			default:
				dev->class = ATA_DEV_UNKNOWN;
				goto err;
			}
		}
	}

	/* PDIAG- should have been released, ask cable type if post-reset */
	if ((ehc->i.flags & ATA_EHI_DID_RESET) && ata_is_host_link(link)) {
		if (ap->ops->cable_detect)
			ap->cbl = ap->ops->cable_detect(ap);
		ata_force_cbl(ap);
	}

	/* Configure new devices forward such that user doesn't see
	 * device detection messages backwards.
	 */
	ata_link_for_each_dev(dev, link) {
		if (!(new_mask & (1 << dev->devno)) ||
		    dev->class == ATA_DEV_PMP)
			continue;

		ehc->i.flags |= ATA_EHI_PRINTINFO;
		rc = ata_dev_configure(dev);
		ehc->i.flags &= ~ATA_EHI_PRINTINFO;
		if (rc)
			goto err;

		spin_lock_irqsave(ap->lock, flags);
		ap->pflags |= ATA_PFLAG_SCSI_HOTPLUG;
		spin_unlock_irqrestore(ap->lock, flags);

		/* new device discovered, configure xfermode */
		ehc->i.flags |= ATA_EHI_SETMODE;
	}

	return 0;

 err:
	*r_failed_dev = dev;
	DPRINTK("EXIT rc=%d\n", rc);
	return rc;
}

/**
 *	ata_set_mode - Program timings and issue SET FEATURES - XFER
 *	@link: link on which timings will be programmed
 *	@r_failed_dev: out paramter for failed device
 *
 *	Set ATA device disk transfer mode (PIO3, UDMA6, etc.).  If
 *	ata_set_mode() fails, pointer to the failing device is
 *	returned in @r_failed_dev.
 *
 *	LOCKING:
 *	PCI/etc. bus probe sem.
 *
 *	RETURNS:
 *	0 on success, negative errno otherwise
 */
int ata_set_mode(struct ata_link *link, struct ata_device **r_failed_dev)
{
	struct ata_port *ap = link->ap;
	struct ata_device *dev;
	int rc;

	/* if data transfer is verified, clear DUBIOUS_XFER on ering top */
	ata_link_for_each_dev(dev, link) {
		if (!(dev->flags & ATA_DFLAG_DUBIOUS_XFER)) {
			struct ata_ering_entry *ent;

			ent = ata_ering_top(&dev->ering);
			if (ent)
				ent->eflags &= ~ATA_EFLAG_DUBIOUS_XFER;
		}
	}

	/* has private set_mode? */
	if (ap->ops->set_mode)
		rc = ap->ops->set_mode(link, r_failed_dev);
	else
		rc = ata_do_set_mode(link, r_failed_dev);

	/* if transfer mode has changed, set DUBIOUS_XFER on device */
	ata_link_for_each_dev(dev, link) {
		struct ata_eh_context *ehc = &link->eh_context;
		u8 saved_xfer_mode = ehc->saved_xfer_mode[dev->devno];
		u8 saved_ncq = !!(ehc->saved_ncq_enabled & (1 << dev->devno));

		if (dev->xfer_mode != saved_xfer_mode ||
		    ata_ncq_enabled(dev) != saved_ncq)
			dev->flags |= ATA_DFLAG_DUBIOUS_XFER;
	}

	return rc;
}

static int ata_link_nr_enabled(struct ata_link *link)
{
	struct ata_device *dev;
	int cnt = 0;

	ata_link_for_each_dev(dev, link)
		if (ata_dev_enabled(dev))
			cnt++;
	return cnt;
}

static int ata_link_nr_vacant(struct ata_link *link)
{
	struct ata_device *dev;
	int cnt = 0;

	ata_link_for_each_dev(dev, link)
		if (dev->class == ATA_DEV_UNKNOWN)
			cnt++;
	return cnt;
}

static int ata_eh_skip_recovery(struct ata_link *link)
{
	struct ata_port *ap = link->ap;
	struct ata_eh_context *ehc = &link->eh_context;
	struct ata_device *dev;

	/* skip disabled links */
	if (link->flags & ATA_LFLAG_DISABLED)
		return 1;

	/* thaw frozen port and recover failed devices */
	if ((ap->pflags & ATA_PFLAG_FROZEN) || ata_link_nr_enabled(link))
		return 0;

	/* reset at least once if reset is requested */
	if ((ehc->i.action & ATA_EH_RESET) &&
	    !(ehc->i.flags & ATA_EHI_DID_RESET))
		return 0;

	/* skip if class codes for all vacant slots are ATA_DEV_NONE */
	ata_link_for_each_dev(dev, link) {
		if (dev->class == ATA_DEV_UNKNOWN &&
		    ehc->classes[dev->devno] != ATA_DEV_NONE)
			return 0;
	}

	return 1;
}

static int ata_eh_schedule_probe(struct ata_device *dev)
{
	struct ata_eh_context *ehc = &dev->link->eh_context;

	if (!(ehc->i.probe_mask & (1 << dev->devno)) ||
	    (ehc->did_probe_mask & (1 << dev->devno)))
		return 0;

	ata_eh_detach_dev(dev);
	ata_dev_init(dev);
	ehc->did_probe_mask |= (1 << dev->devno);
	ehc->i.action |= ATA_EH_RESET;
	ehc->saved_xfer_mode[dev->devno] = 0;
	ehc->saved_ncq_enabled &= ~(1 << dev->devno);

	return 1;
}

static int ata_eh_handle_dev_fail(struct ata_device *dev, int err)
{
	struct ata_eh_context *ehc = &dev->link->eh_context;

	ehc->tries[dev->devno]--;

	switch (err) {
	case -ENODEV:
		/* device missing or wrong IDENTIFY data, schedule probing */
		ehc->i.probe_mask |= (1 << dev->devno);
	case -EINVAL:
		/* give it just one more chance */
		ehc->tries[dev->devno] = min(ehc->tries[dev->devno], 1);
	case -EIO:
		if (ehc->tries[dev->devno] == 1 && dev->pio_mode > XFER_PIO_0) {
			/* This is the last chance, better to slow
			 * down than lose it.
			 */
			sata_down_spd_limit(dev->link);
			ata_down_xfermask_limit(dev, ATA_DNXFER_PIO);
		}
	}

	if (ata_dev_enabled(dev) && !ehc->tries[dev->devno]) {
		/* disable device if it has used up all its chances */
		ata_dev_disable(dev);

		/* detach if offline */
		if (ata_link_offline(dev->link))
			ata_eh_detach_dev(dev);

		/* schedule probe if necessary */
		if (ata_eh_schedule_probe(dev))
			ehc->tries[dev->devno] = ATA_EH_DEV_TRIES;

		return 1;
	} else {
		ehc->i.action |= ATA_EH_RESET;
		return 0;
	}
}

/**
 *	ata_eh_recover - recover host port after error
 *	@ap: host port to recover
 *	@prereset: prereset method (can be NULL)
 *	@softreset: softreset method (can be NULL)
 *	@hardreset: hardreset method (can be NULL)
 *	@postreset: postreset method (can be NULL)
 *	@r_failed_link: out parameter for failed link
 *
 *	This is the alpha and omega, eum and yang, heart and soul of
 *	libata exception handling.  On entry, actions required to
 *	recover each link and hotplug requests are recorded in the
 *	link's eh_context.  This function executes all the operations
 *	with appropriate retrials and fallbacks to resurrect failed
 *	devices, detach goners and greet newcomers.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
int ata_eh_recover(struct ata_port *ap, ata_prereset_fn_t prereset,
		   ata_reset_fn_t softreset, ata_reset_fn_t hardreset,
		   ata_postreset_fn_t postreset,
		   struct ata_link **r_failed_link)
{
	struct ata_link *link;
	struct ata_device *dev;
	int nr_failed_devs, nr_disabled_devs;
	int reset, rc;
	unsigned long flags;

	DPRINTK("ENTER\n");

	/* prep for recovery */
	ata_port_for_each_link(link, ap) {
		struct ata_eh_context *ehc = &link->eh_context;

		/* re-enable link? */
		if (ehc->i.action & ATA_EH_ENABLE_LINK) {
			ata_eh_about_to_do(link, NULL, ATA_EH_ENABLE_LINK);
			spin_lock_irqsave(ap->lock, flags);
			link->flags &= ~ATA_LFLAG_DISABLED;
			spin_unlock_irqrestore(ap->lock, flags);
			ata_eh_done(link, NULL, ATA_EH_ENABLE_LINK);
		}

		ata_link_for_each_dev(dev, link) {
			if (link->flags & ATA_LFLAG_NO_RETRY)
				ehc->tries[dev->devno] = 1;
			else
				ehc->tries[dev->devno] = ATA_EH_DEV_TRIES;

			/* collect port action mask recorded in dev actions */
			ehc->i.action |= ehc->i.dev_action[dev->devno] &
					 ~ATA_EH_PERDEV_MASK;
			ehc->i.dev_action[dev->devno] &= ATA_EH_PERDEV_MASK;

			/* process hotplug request */
			if (dev->flags & ATA_DFLAG_DETACH)
				ata_eh_detach_dev(dev);

			/* schedule probe if necessary */
			if (!ata_dev_enabled(dev))
				ata_eh_schedule_probe(dev);
		}
	}

 retry:
	rc = 0;
	nr_failed_devs = 0;
	nr_disabled_devs = 0;
	reset = 0;

	/* if UNLOADING, finish immediately */
	if (ap->pflags & ATA_PFLAG_UNLOADING)
		goto out;

	/* prep for EH */
	ata_port_for_each_link(link, ap) {
		struct ata_eh_context *ehc = &link->eh_context;

		/* skip EH if possible. */
		if (ata_eh_skip_recovery(link))
			ehc->i.action = 0;

		/* do we need to reset? */
		if (ehc->i.action & ATA_EH_RESET)
			reset = 1;

		ata_link_for_each_dev(dev, link)
			ehc->classes[dev->devno] = ATA_DEV_UNKNOWN;
	}

	/* reset */
	if (reset) {
		/* if PMP is attached, this function only deals with
		 * downstream links, port should stay thawed.
		 */
		if (!sata_pmp_attached(ap))
			ata_eh_freeze_port(ap);

		ata_port_for_each_link(link, ap) {
			struct ata_eh_context *ehc = &link->eh_context;

			if (!(ehc->i.action & ATA_EH_RESET))
				continue;

			rc = ata_eh_reset(link, ata_link_nr_vacant(link),
					  prereset, softreset, hardreset,
					  postreset);
			if (rc) {
				ata_link_printk(link, KERN_ERR,
						"reset failed, giving up\n");
				goto out;
			}
		}

		if (!sata_pmp_attached(ap))
			ata_eh_thaw_port(ap);
	}

	/* the rest */
	ata_port_for_each_link(link, ap) {
		struct ata_eh_context *ehc = &link->eh_context;

		/* revalidate existing devices and attach new ones */
		rc = ata_eh_revalidate_and_attach(link, &dev);
		if (rc)
			goto dev_fail;

		/* if PMP got attached, return, pmp EH will take care of it */
		if (link->device->class == ATA_DEV_PMP) {
			ehc->i.action = 0;
			return 0;
		}

		/* configure transfer mode if necessary */
		if (ehc->i.flags & ATA_EHI_SETMODE) {
			rc = ata_set_mode(link, &dev);
			if (rc)
				goto dev_fail;
			ehc->i.flags &= ~ATA_EHI_SETMODE;
		}

		if (ehc->i.action & ATA_EH_LPM)
			ata_link_for_each_dev(dev, link)
				ata_dev_enable_pm(dev, ap->pm_policy);

		/* this link is okay now */
		ehc->i.flags = 0;
		continue;

dev_fail:
		nr_failed_devs++;
		if (ata_eh_handle_dev_fail(dev, rc))
			nr_disabled_devs++;

		if (ap->pflags & ATA_PFLAG_FROZEN) {
			/* PMP reset requires working host port.
			 * Can't retry if it's frozen.
			 */
			if (sata_pmp_attached(ap))
				goto out;
			break;
		}
	}

	if (nr_failed_devs) {
		if (nr_failed_devs != nr_disabled_devs) {
			ata_port_printk(ap, KERN_WARNING, "failed to recover "
					"some devices, retrying in 5 secs\n");
			ssleep(5);
		} else {
			/* no device left to recover, repeat fast */
			msleep(500);
		}

		goto retry;
	}

 out:
	if (rc && r_failed_link)
		*r_failed_link = link;

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
void ata_eh_finish(struct ata_port *ap)
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
			if (qc->flags & ATA_QCFLAG_RETRY)
				ata_eh_qc_retry(qc);
			else
				ata_eh_qc_complete(qc);
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

	/* make sure nr_active_links is zero after EH */
	WARN_ON(ap->nr_active_links);
	ap->nr_active_links = 0;
}

/**
 *	ata_do_eh - do standard error handling
 *	@ap: host port to handle error for
 *
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
	struct ata_device *dev;
	int rc;

	ata_eh_autopsy(ap);
	ata_eh_report(ap);

	rc = ata_eh_recover(ap, prereset, softreset, hardreset, postreset,
			    NULL);
	if (rc) {
		ata_link_for_each_dev(dev, &ap->link)
			ata_dev_disable(dev);
	}

	ata_eh_finish(ap);
}

/**
 *	ata_std_error_handler - standard error handler
 *	@ap: host port to handle error for
 *
 *	Standard error handler
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 */
void ata_std_error_handler(struct ata_port *ap)
{
	struct ata_port_operations *ops = ap->ops;
	ata_reset_fn_t hardreset = ops->hardreset;

	/* ignore built-in hardreset if SCR access is not available */
	if (ata_is_builtin_hardreset(hardreset) && !sata_scr_valid(&ap->link))
		hardreset = NULL;

	ata_do_eh(ap, ops->prereset, ops->softreset, hardreset, ops->postreset);
}

#ifdef CONFIG_PM
/**
 *	ata_eh_handle_port_suspend - perform port suspend operation
 *	@ap: port to suspend
 *
 *	Suspend @ap.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 */
static void ata_eh_handle_port_suspend(struct ata_port *ap)
{
	unsigned long flags;
	int rc = 0;

	/* are we suspending? */
	spin_lock_irqsave(ap->lock, flags);
	if (!(ap->pflags & ATA_PFLAG_PM_PENDING) ||
	    ap->pm_mesg.event == PM_EVENT_ON) {
		spin_unlock_irqrestore(ap->lock, flags);
		return;
	}
	spin_unlock_irqrestore(ap->lock, flags);

	WARN_ON(ap->pflags & ATA_PFLAG_SUSPENDED);

	/* tell ACPI we're suspending */
	rc = ata_acpi_on_suspend(ap);
	if (rc)
		goto out;

	/* suspend */
	ata_eh_freeze_port(ap);

	if (ap->ops->port_suspend)
		rc = ap->ops->port_suspend(ap, ap->pm_mesg);

	ata_acpi_set_state(ap, PMSG_SUSPEND);
 out:
	/* report result */
	spin_lock_irqsave(ap->lock, flags);

	ap->pflags &= ~ATA_PFLAG_PM_PENDING;
	if (rc == 0)
		ap->pflags |= ATA_PFLAG_SUSPENDED;
	else if (ap->pflags & ATA_PFLAG_FROZEN)
		ata_port_schedule_eh(ap);

	if (ap->pm_result) {
		*ap->pm_result = rc;
		ap->pm_result = NULL;
	}

	spin_unlock_irqrestore(ap->lock, flags);

	return;
}

/**
 *	ata_eh_handle_port_resume - perform port resume operation
 *	@ap: port to resume
 *
 *	Resume @ap.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 */
static void ata_eh_handle_port_resume(struct ata_port *ap)
{
	unsigned long flags;
	int rc = 0;

	/* are we resuming? */
	spin_lock_irqsave(ap->lock, flags);
	if (!(ap->pflags & ATA_PFLAG_PM_PENDING) ||
	    ap->pm_mesg.event != PM_EVENT_ON) {
		spin_unlock_irqrestore(ap->lock, flags);
		return;
	}
	spin_unlock_irqrestore(ap->lock, flags);

	WARN_ON(!(ap->pflags & ATA_PFLAG_SUSPENDED));

	ata_acpi_set_state(ap, PMSG_ON);

	if (ap->ops->port_resume)
		rc = ap->ops->port_resume(ap);

	/* tell ACPI that we're resuming */
	ata_acpi_on_resume(ap);

	/* report result */
	spin_lock_irqsave(ap->lock, flags);
	ap->pflags &= ~(ATA_PFLAG_PM_PENDING | ATA_PFLAG_SUSPENDED);
	if (ap->pm_result) {
		*ap->pm_result = rc;
		ap->pm_result = NULL;
	}
	spin_unlock_irqrestore(ap->lock, flags);
}
#endif /* CONFIG_PM */
