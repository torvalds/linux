/*
 *  libata-eh.c - libata error handling
 *
 *  Maintained by:  Tejun Heo <tj@kernel.org>
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
#include <linux/blkdev.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_dbg.h>
#include "../scsi/scsi_transport_api.h"

#include <linux/libata.h>

#include <trace/events/libata.h>
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
	ATA_EFLAG_OLD_ER                = (1 << 31),

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

	ATA_EH_CMD_DFL_TIMEOUT		=  5000,

	/* always put at least this amount of time between resets */
	ATA_EH_RESET_COOL_DOWN		=  5000,

	/* Waiting in ->prereset can never be reliable.  It's
	 * sometimes nice to wait there but it can't be depended upon;
	 * otherwise, we wouldn't be resetting.  Just give it enough
	 * time for most drives to spin up.
	 */
	ATA_EH_PRERESET_TIMEOUT		= 10000,
	ATA_EH_FASTDRAIN_INTERVAL	=  3000,

	ATA_EH_UA_TRIES			= 5,

	/* probe speed down parameters, see ata_eh_schedule_probe() */
	ATA_EH_PROBE_TRIAL_INTERVAL	= 60000,	/* 1 min */
	ATA_EH_PROBE_TRIALS		= 2,
};

/* The following table determines how we sequence resets.  Each entry
 * represents timeout for that try.  The first try can be soft or
 * hardreset.  All others are hardreset if available.  In most cases
 * the first reset w/ 10sec timeout should succeed.  Following entries
 * are mostly for error handling, hotplug and those outlier devices that
 * take an exceptionally long time to recover from reset.
 */
static const unsigned long ata_eh_reset_timeouts[] = {
	10000,	/* most drives spin up by 10sec */
	10000,	/* > 99% working drives spin up before 20sec */
	35000,	/* give > 30 secs of idleness for outlier devices */
	 5000,	/* and sweet one last chance */
	ULONG_MAX, /* > 1 min has elapsed, give up */
};

static const unsigned long ata_eh_identify_timeouts[] = {
	 5000,	/* covers > 99% of successes and not too boring on failures */
	10000,  /* combined time till here is enough even for media access */
	30000,	/* for true idiots */
	ULONG_MAX,
};

static const unsigned long ata_eh_flush_timeouts[] = {
	15000,	/* be generous with flush */
	15000,  /* ditto */
	30000,	/* and even more generous */
	ULONG_MAX,
};

static const unsigned long ata_eh_other_timeouts[] = {
	 5000,	/* same rationale as identify timeout */
	10000,	/* ditto */
	/* but no merciful 30sec for other commands, it just isn't worth it */
	ULONG_MAX,
};

struct ata_eh_cmd_timeout_ent {
	const u8		*commands;
	const unsigned long	*timeouts;
};

/* The following table determines timeouts to use for EH internal
 * commands.  Each table entry is a command class and matches the
 * commands the entry applies to and the timeout table to use.
 *
 * On the retry after a command timed out, the next timeout value from
 * the table is used.  If the table doesn't contain further entries,
 * the last value is used.
 *
 * ehc->cmd_timeout_idx keeps track of which timeout to use per
 * command class, so if SET_FEATURES times out on the first try, the
 * next try will use the second timeout value only for that class.
 */
#define CMDS(cmds...)	(const u8 []){ cmds, 0 }
static const struct ata_eh_cmd_timeout_ent
ata_eh_cmd_timeout_table[ATA_EH_CMD_TIMEOUT_TABLE_SIZE] = {
	{ .commands = CMDS(ATA_CMD_ID_ATA, ATA_CMD_ID_ATAPI),
	  .timeouts = ata_eh_identify_timeouts, },
	{ .commands = CMDS(ATA_CMD_READ_NATIVE_MAX, ATA_CMD_READ_NATIVE_MAX_EXT),
	  .timeouts = ata_eh_other_timeouts, },
	{ .commands = CMDS(ATA_CMD_SET_MAX, ATA_CMD_SET_MAX_EXT),
	  .timeouts = ata_eh_other_timeouts, },
	{ .commands = CMDS(ATA_CMD_SET_FEATURES),
	  .timeouts = ata_eh_other_timeouts, },
	{ .commands = CMDS(ATA_CMD_INIT_DEV_PARAMS),
	  .timeouts = ata_eh_other_timeouts, },
	{ .commands = CMDS(ATA_CMD_FLUSH, ATA_CMD_FLUSH_EXT),
	  .timeouts = ata_eh_flush_timeouts },
};
#undef CMDS

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

static int ata_lookup_timeout_table(u8 cmd)
{
	int i;

	for (i = 0; i < ATA_EH_CMD_TIMEOUT_TABLE_SIZE; i++) {
		const u8 *cur;

		for (cur = ata_eh_cmd_timeout_table[i].commands; *cur; cur++)
			if (*cur == cmd)
				return i;
	}

	return -1;
}

/**
 *	ata_internal_cmd_timeout - determine timeout for an internal command
 *	@dev: target device
 *	@cmd: internal command to be issued
 *
 *	Determine timeout for internal command @cmd for @dev.
 *
 *	LOCKING:
 *	EH context.
 *
 *	RETURNS:
 *	Determined timeout.
 */
unsigned long ata_internal_cmd_timeout(struct ata_device *dev, u8 cmd)
{
	struct ata_eh_context *ehc = &dev->link->eh_context;
	int ent = ata_lookup_timeout_table(cmd);
	int idx;

	if (ent < 0)
		return ATA_EH_CMD_DFL_TIMEOUT;

	idx = ehc->cmd_timeout_idx[dev->devno][ent];
	return ata_eh_cmd_timeout_table[ent].timeouts[idx];
}

/**
 *	ata_internal_cmd_timed_out - notification for internal command timeout
 *	@dev: target device
 *	@cmd: internal command which timed out
 *
 *	Notify EH that internal command @cmd for @dev timed out.  This
 *	function should be called only for commands whose timeouts are
 *	determined using ata_internal_cmd_timeout().
 *
 *	LOCKING:
 *	EH context.
 */
void ata_internal_cmd_timed_out(struct ata_device *dev, u8 cmd)
{
	struct ata_eh_context *ehc = &dev->link->eh_context;
	int ent = ata_lookup_timeout_table(cmd);
	int idx;

	if (ent < 0)
		return;

	idx = ehc->cmd_timeout_idx[dev->devno][ent];
	if (ata_eh_cmd_timeout_table[ent].timeouts[idx + 1] != ULONG_MAX)
		ehc->cmd_timeout_idx[dev->devno][ent]++;
}

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

int ata_ering_map(struct ata_ering *ering,
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

static int ata_ering_clear_cb(struct ata_ering_entry *ent, void *void_arg)
{
	ent->eflags |= ATA_EFLAG_OLD_ER;
	return 0;
}

static void ata_ering_clear(struct ata_ering *ering)
{
	ata_ering_map(ering, ata_ering_clear_cb, NULL);
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
		ata_for_each_dev(tdev, link, ALL)
			ehi->dev_action[tdev->devno] &= ~action;
	} else {
		/* doesn't make sense for port-wide EH actions */
		WARN_ON(!(action & ATA_EH_PERDEV_MASK));

		/* break ehi->action into ehi->dev_action */
		if (ehi->action & action) {
			ata_for_each_dev(tdev, link, ALL)
				ehi->dev_action[tdev->devno] |=
					ehi->action & action;
			ehi->action &= ~action;
		}

		/* turn off the specified per-dev action */
		ehi->dev_action[dev->devno] &= ~action;
	}
}

/**
 *	ata_eh_acquire - acquire EH ownership
 *	@ap: ATA port to acquire EH ownership for
 *
 *	Acquire EH ownership for @ap.  This is the basic exclusion
 *	mechanism for ports sharing a host.  Only one port hanging off
 *	the same host can claim the ownership of EH.
 *
 *	LOCKING:
 *	EH context.
 */
void ata_eh_acquire(struct ata_port *ap)
{
	mutex_lock(&ap->host->eh_mutex);
	WARN_ON_ONCE(ap->host->eh_owner);
	ap->host->eh_owner = current;
}

/**
 *	ata_eh_release - release EH ownership
 *	@ap: ATA port to release EH ownership for
 *
 *	Release EH ownership for @ap if the caller.  The caller must
 *	have acquired EH ownership using ata_eh_acquire() previously.
 *
 *	LOCKING:
 *	EH context.
 */
void ata_eh_release(struct ata_port *ap)
{
	WARN_ON_ONCE(ap->host->eh_owner != current);
	ap->host->eh_owner = NULL;
	mutex_unlock(&ap->host->eh_mutex);
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
enum blk_eh_timer_return ata_scsi_timed_out(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *host = cmd->device->host;
	struct ata_port *ap = ata_shost_to_port(host);
	unsigned long flags;
	struct ata_queued_cmd *qc;
	enum blk_eh_timer_return ret;

	DPRINTK("ENTER\n");

	if (ap->ops->error_handler) {
		ret = BLK_EH_NOT_HANDLED;
		goto out;
	}

	ret = BLK_EH_HANDLED;
	spin_lock_irqsave(ap->lock, flags);
	qc = ata_qc_from_tag(ap, ap->link.active_tag);
	if (qc) {
		WARN_ON(qc->scsicmd != cmd);
		qc->flags |= ATA_QCFLAG_EH_SCHEDULED;
		qc->err_mask |= AC_ERR_TIMEOUT;
		ret = BLK_EH_NOT_HANDLED;
	}
	spin_unlock_irqrestore(ap->lock, flags);

 out:
	DPRINTK("EXIT, ret=%d\n", ret);
	return ret;
}

static void ata_eh_unload(struct ata_port *ap)
{
	struct ata_link *link;
	struct ata_device *dev;
	unsigned long flags;

	/* Restore SControl IPM and SPD for the next driver and
	 * disable attached devices.
	 */
	ata_for_each_link(link, ap, PMP_FIRST) {
		sata_scr_write(link, SCR_CONTROL, link->saved_scontrol & 0xff0);
		ata_for_each_dev(dev, link, ALL)
			ata_dev_disable(dev);
	}

	/* freeze and set UNLOADED */
	spin_lock_irqsave(ap->lock, flags);

	ata_port_freeze(ap);			/* won't be thawed */
	ap->pflags &= ~ATA_PFLAG_EH_PENDING;	/* clear pending from freeze */
	ap->pflags |= ATA_PFLAG_UNLOADED;

	spin_unlock_irqrestore(ap->lock, flags);
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
	unsigned long flags;
	LIST_HEAD(eh_work_q);

	DPRINTK("ENTER\n");

	spin_lock_irqsave(host->host_lock, flags);
	list_splice_init(&host->eh_cmd_q, &eh_work_q);
	spin_unlock_irqrestore(host->host_lock, flags);

	ata_scsi_cmd_error_handler(host, ap, &eh_work_q);

	/* If we timed raced normal completion and there is nothing to
	   recover nr_timedout == 0 why exactly are we doing error recovery ? */
	ata_scsi_port_error_handler(host, ap);

	/* finish or retry handled scmd's and clean up */
	WARN_ON(!list_empty(&eh_work_q));

	DPRINTK("EXIT\n");
}

/**
 * ata_scsi_cmd_error_handler - error callback for a list of commands
 * @host:	scsi host containing the port
 * @ap:		ATA port within the host
 * @eh_work_q:	list of commands to process
 *
 * process the given list of commands and return those finished to the
 * ap->eh_done_q.  This function is the first part of the libata error
 * handler which processes a given list of failed commands.
 */
void ata_scsi_cmd_error_handler(struct Scsi_Host *host, struct ata_port *ap,
				struct list_head *eh_work_q)
{
	int i;
	unsigned long flags;

	/* make sure sff pio task is not running */
	ata_sff_flush_pio_task(ap);

	/* synchronize with host lock and sort out timeouts */

	/* For new EH, all qcs are finished in one of three ways -
	 * normal completion, error completion, and SCSI timeout.
	 * Both completions can race against SCSI timeout.  When normal
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

		/* This must occur under the ap->lock as we don't want
		   a polled recovery to race the real interrupt handler

		   The lost_interrupt handler checks for any completed but
		   non-notified command and completes much like an IRQ handler.

		   We then fall into the error recovery code which will treat
		   this as if normal completion won the race */

		if (ap->ops->lost_interrupt)
			ap->ops->lost_interrupt(ap);

		list_for_each_entry_safe(scmd, tmp, eh_work_q, eh_entry) {
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

}
EXPORT_SYMBOL(ata_scsi_cmd_error_handler);

/**
 * ata_scsi_port_error_handler - recover the port after the commands
 * @host:	SCSI host containing the port
 * @ap:		the ATA port
 *
 * Handle the recovery of the port @ap after all the commands
 * have been recovered.
 */
void ata_scsi_port_error_handler(struct Scsi_Host *host, struct ata_port *ap)
{
	unsigned long flags;

	/* invoke error handler */
	if (ap->ops->error_handler) {
		struct ata_link *link;

		/* acquire EH ownership */
		ata_eh_acquire(ap);
 repeat:
		/* kill fast drain timer */
		del_timer_sync(&ap->fastdrain_timer);

		/* process port resume request */
		ata_eh_handle_port_resume(ap);

		/* fetch & clear EH info */
		spin_lock_irqsave(ap->lock, flags);

		ata_for_each_link(link, ap, HOST_FIRST) {
			struct ata_eh_context *ehc = &link->eh_context;
			struct ata_device *dev;

			memset(&link->eh_context, 0, sizeof(link->eh_context));
			link->eh_context.i = link->eh_info;
			memset(&link->eh_info, 0, sizeof(link->eh_info));

			ata_for_each_dev(dev, link, ENABLED) {
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
		else {
			/* if unloading, commence suicide */
			if ((ap->pflags & ATA_PFLAG_UNLOADING) &&
			    !(ap->pflags & ATA_PFLAG_UNLOADED))
				ata_eh_unload(ap);
			ata_eh_finish(ap);
		}

		/* process port suspend request */
		ata_eh_handle_port_suspend(ap);

		/* Exception might have happened after ->error_handler
		 * recovered the port but before this point.  Repeat
		 * EH in such case.
		 */
		spin_lock_irqsave(ap->lock, flags);

		if (ap->pflags & ATA_PFLAG_EH_PENDING) {
			if (--ap->eh_tries) {
				spin_unlock_irqrestore(ap->lock, flags);
				goto repeat;
			}
			ata_port_err(ap,
				     "EH pending after %d tries, giving up\n",
				     ATA_EH_MAX_TRIES);
			ap->pflags &= ~ATA_PFLAG_EH_PENDING;
		}

		/* this run is complete, make sure EH info is clear */
		ata_for_each_link(link, ap, HOST_FIRST)
			memset(&link->eh_info, 0, sizeof(link->eh_info));

		/* end eh (clear host_eh_scheduled) while holding
		 * ap->lock such that if exception occurs after this
		 * point but before EH completion, SCSI midlayer will
		 * re-initiate EH.
		 */
		ap->ops->end_eh(ap);

		spin_unlock_irqrestore(ap->lock, flags);
		ata_eh_release(ap);
	} else {
		WARN_ON(ata_qc_from_tag(ap, ap->link.active_tag) == NULL);
		ap->ops->eng_timeout(ap);
	}

	scsi_eh_flush_done_q(&ap->eh_done_q);

	/* clean up */
	spin_lock_irqsave(ap->lock, flags);

	if (ap->pflags & ATA_PFLAG_LOADING)
		ap->pflags &= ~ATA_PFLAG_LOADING;
	else if (ap->pflags & ATA_PFLAG_SCSI_HOTPLUG)
		schedule_delayed_work(&ap->hotplug_task, 0);

	if (ap->pflags & ATA_PFLAG_RECOVERED)
		ata_port_info(ap, "EH complete\n");

	ap->pflags &= ~(ATA_PFLAG_SCSI_HOTPLUG | ATA_PFLAG_RECOVERED);

	/* tell wait_eh that we're done */
	ap->pflags &= ~ATA_PFLAG_EH_IN_PROGRESS;
	wake_up_all(&ap->eh_wait_q);

	spin_unlock_irqrestore(ap->lock, flags);
}
EXPORT_SYMBOL_GPL(ata_scsi_port_error_handler);

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
		ata_msleep(ap, 10);
		goto retry;
	}
}
EXPORT_SYMBOL_GPL(ata_port_wait_eh);

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
			ata_deadline(jiffies, ATA_EH_FASTDRAIN_INTERVAL);
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
	ap->fastdrain_timer.expires =
		ata_deadline(jiffies, ATA_EH_FASTDRAIN_INTERVAL);
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
	struct request_queue *q = qc->scsicmd->device->request_queue;
	unsigned long flags;

	WARN_ON(!ap->ops->error_handler);

	qc->flags |= ATA_QCFLAG_FAILED;
	ata_eh_set_pending(ap, 1);

	/* The following will fail if timeout has already expired.
	 * ata_scsi_error() takes care of such scmds on EH entry.
	 * Note that ATA_QCFLAG_FAILED is unconditionally set after
	 * this function completes.
	 */
	spin_lock_irqsave(q->queue_lock, flags);
	blk_abort_request(qc->scsicmd->request);
	spin_unlock_irqrestore(q->queue_lock, flags);
}

/**
 * ata_std_sched_eh - non-libsas ata_ports issue eh with this common routine
 * @ap: ATA port to schedule EH for
 *
 *	LOCKING: inherited from ata_port_schedule_eh
 *	spin_lock_irqsave(host lock)
 */
void ata_std_sched_eh(struct ata_port *ap)
{
	WARN_ON(!ap->ops->error_handler);

	if (ap->pflags & ATA_PFLAG_INITIALIZING)
		return;

	ata_eh_set_pending(ap, 1);
	scsi_schedule_eh(ap->scsi_host);

	DPRINTK("port EH scheduled\n");
}
EXPORT_SYMBOL_GPL(ata_std_sched_eh);

/**
 * ata_std_end_eh - non-libsas ata_ports complete eh with this common routine
 * @ap: ATA port to end EH for
 *
 * In the libata object model there is a 1:1 mapping of ata_port to
 * shost, so host fields can be directly manipulated under ap->lock, in
 * the libsas case we need to hold a lock at the ha->level to coordinate
 * these events.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
void ata_std_end_eh(struct ata_port *ap)
{
	struct Scsi_Host *host = ap->scsi_host;

	host->host_eh_scheduled = 0;
}
EXPORT_SYMBOL(ata_std_end_eh);


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
	/* see: ata_std_sched_eh, unless you know better */
	ap->ops->sched_eh(ap);
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
 *	Abort and freeze @ap.  The freeze operation must be called
 *	first, because some hardware requires special operations
 *	before the taskfile registers are accessible.
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

	__ata_port_freeze(ap);
	nr_aborted = ata_port_abort(ap);

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
		ata_for_each_link(link, ap, EDGE) {
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
 *	scmd->allowed is incremented for commands which get retried
 *	due to unrelated failures (qc->err_mask is zero).
 */
void ata_eh_qc_retry(struct ata_queued_cmd *qc)
{
	struct scsi_cmnd *scmd = qc->scsicmd;
	if (!qc->err_mask)
		scmd->allowed++;
	__ata_eh_qc_complete(qc);
}

/**
 *	ata_dev_disable - disable ATA device
 *	@dev: ATA device to disable
 *
 *	Disable @dev.
 *
 *	Locking:
 *	EH context.
 */
void ata_dev_disable(struct ata_device *dev)
{
	if (!ata_dev_enabled(dev))
		return;

	if (ata_msg_drv(dev->link->ap))
		ata_dev_warn(dev, "disabled\n");
	ata_acpi_on_disable(dev);
	ata_down_xfermask_limit(dev, ATA_DNXFER_FORCE_PIO0 | ATA_DNXFER_QUIET);
	dev->class++;

	/* From now till the next successful probe, ering is used to
	 * track probe failures.  Clear accumulated device error info.
	 */
	ata_ering_clear(&dev->ering);
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
	struct ata_eh_context *ehc = &link->eh_context;
	unsigned long flags;

	ata_dev_disable(dev);

	spin_lock_irqsave(ap->lock, flags);

	dev->flags &= ~ATA_DFLAG_DETACH;

	if (ata_scsi_offline_dev(dev)) {
		dev->flags |= ATA_DFLAG_DETACHED;
		ap->pflags |= ATA_PFLAG_SCSI_HOTPLUG;
	}

	/* clear per-dev EH info */
	ata_eh_clear_action(link, dev, &link->eh_info, ATA_EH_PERDEV_MASK);
	ata_eh_clear_action(link, dev, &link->eh_context.i, ATA_EH_PERDEV_MASK);
	ehc->saved_xfer_mode[dev->devno] = 0;
	ehc->saved_ncq_enabled &= ~(1 << dev->devno);

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

	/* About to take EH action, set RECOVERED.  Ignore actions on
	 * slave links as master will do them again.
	 */
	if (!(ehc->i.flags & ATA_EHI_QUIET) && link != ap->slave_link)
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
 *	@log: log to read
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
unsigned int ata_read_log_page(struct ata_device *dev, u8 log,
			       u8 page, void *buf, unsigned int sectors)
{
	unsigned long ap_flags = dev->link->ap->flags;
	struct ata_taskfile tf;
	unsigned int err_mask;
	bool dma = false;

	DPRINTK("read log page - log 0x%x, page 0x%x\n", log, page);

	/*
	 * Return error without actually issuing the command on controllers
	 * which e.g. lockup on a read log page.
	 */
	if (ap_flags & ATA_FLAG_NO_LOG_PAGE)
		return AC_ERR_DEV;

retry:
	ata_tf_init(dev, &tf);
	if (dev->dma_mode && ata_id_has_read_log_dma_ext(dev->id) &&
	    !(dev->horkage & ATA_HORKAGE_NO_NCQ_LOG)) {
		tf.command = ATA_CMD_READ_LOG_DMA_EXT;
		tf.protocol = ATA_PROT_DMA;
		dma = true;
	} else {
		tf.command = ATA_CMD_READ_LOG_EXT;
		tf.protocol = ATA_PROT_PIO;
		dma = false;
	}
	tf.lbal = log;
	tf.lbam = page;
	tf.nsect = sectors;
	tf.hob_nsect = sectors >> 8;
	tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_LBA48 | ATA_TFLAG_DEVICE;

	err_mask = ata_exec_internal(dev, &tf, NULL, DMA_FROM_DEVICE,
				     buf, sectors * ATA_SECT_SIZE, 0);

	if (err_mask && dma) {
		dev->horkage |= ATA_HORKAGE_NO_NCQ_LOG;
		ata_dev_warn(dev, "READ LOG DMA EXT failed, trying unqueued\n");
		goto retry;
	}

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

	err_mask = ata_read_log_page(dev, ATA_LOG_SATA_NCQ, 0, buf, 1);
	if (err_mask)
		return -EIO;

	csum = 0;
	for (i = 0; i < ATA_SECT_SIZE; i++)
		csum += buf[i];
	if (csum)
		ata_dev_warn(dev, "invalid checksum 0x%x on log page 10h\n",
			     csum);

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
	if (ata_id_has_ncq_autosense(dev->id))
		tf->auxiliary = buf[14] << 16 | buf[15] << 8 | buf[16];

	return 0;
}

/**
 *	atapi_eh_tur - perform ATAPI TEST_UNIT_READY
 *	@dev: target ATAPI device
 *	@r_sense_key: out parameter for sense_key
 *
 *	Perform ATAPI TEST_UNIT_READY.
 *
 *	LOCKING:
 *	EH context (may sleep).
 *
 *	RETURNS:
 *	0 on success, AC_ERR_* mask on failure.
 */
unsigned int atapi_eh_tur(struct ata_device *dev, u8 *r_sense_key)
{
	u8 cdb[ATAPI_CDB_LEN] = { TEST_UNIT_READY, 0, 0, 0, 0, 0 };
	struct ata_taskfile tf;
	unsigned int err_mask;

	ata_tf_init(dev, &tf);

	tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	tf.command = ATA_CMD_PACKET;
	tf.protocol = ATAPI_PROT_NODATA;

	err_mask = ata_exec_internal(dev, &tf, cdb, DMA_NONE, NULL, 0, 0);
	if (err_mask == AC_ERR_DEV)
		*r_sense_key = tf.feature >> 4;
	return err_mask;
}

/**
 *	ata_eh_request_sense - perform REQUEST_SENSE_DATA_EXT
 *	@dev: device to perform REQUEST_SENSE_SENSE_DATA_EXT to
 *	@cmd: scsi command for which the sense code should be set
 *
 *	Perform REQUEST_SENSE_DATA_EXT after the device reported CHECK
 *	SENSE.  This function is an EH helper.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 */
static void ata_eh_request_sense(struct ata_queued_cmd *qc,
				 struct scsi_cmnd *cmd)
{
	struct ata_device *dev = qc->dev;
	struct ata_taskfile tf;
	unsigned int err_mask;

	if (qc->ap->pflags & ATA_PFLAG_FROZEN) {
		ata_dev_warn(dev, "sense data available but port frozen\n");
		return;
	}

	if (!cmd || qc->flags & ATA_QCFLAG_SENSE_VALID)
		return;

	if (!ata_id_sense_reporting_enabled(dev->id)) {
		ata_dev_warn(qc->dev, "sense data reporting disabled\n");
		return;
	}

	DPRINTK("ATA request sense\n");

	ata_tf_init(dev, &tf);
	tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	tf.flags |= ATA_TFLAG_LBA | ATA_TFLAG_LBA48;
	tf.command = ATA_CMD_REQ_SENSE_DATA;
	tf.protocol = ATA_PROT_NODATA;

	err_mask = ata_exec_internal(dev, &tf, NULL, DMA_NONE, NULL, 0, 0);
	/* Ignore err_mask; ATA_ERR might be set */
	if (tf.command & ATA_SENSE) {
		ata_scsi_set_sense(dev, cmd, tf.lbah, tf.lbam, tf.lbal);
		qc->flags |= ATA_QCFLAG_SENSE_VALID;
	} else {
		ata_dev_warn(dev, "request sense failed stat %02x emask %x\n",
			     tf.command, err_mask);
	}
}

/**
 *	atapi_eh_request_sense - perform ATAPI REQUEST_SENSE
 *	@dev: device to perform REQUEST_SENSE to
 *	@sense_buf: result sense data buffer (SCSI_SENSE_BUFFERSIZE bytes long)
 *	@dfl_sense_key: default sense key to use
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
unsigned int atapi_eh_request_sense(struct ata_device *dev,
					   u8 *sense_buf, u8 dfl_sense_key)
{
	u8 cdb[ATAPI_CDB_LEN] =
		{ REQUEST_SENSE, 0, 0, 0, SCSI_SENSE_BUFFERSIZE, 0 };
	struct ata_port *ap = dev->link->ap;
	struct ata_taskfile tf;

	DPRINTK("ATAPI request sense\n");

	memset(sense_buf, 0, SCSI_SENSE_BUFFERSIZE);

	/* initialize sense_buf with the error register,
	 * for the case where they are -not- overwritten
	 */
	sense_buf[0] = 0x70;
	sense_buf[2] = dfl_sense_key;

	/* some devices time out if garbage left in tf */
	ata_tf_init(dev, &tf);

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

	if (serror & (SERR_PERSISTENT | SERR_DATA)) {
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
	if (link->lpm_policy > ATA_LPM_MAX_POWER)
		hotplug_mask = 0;	/* hotplug doesn't work w/ LPM */
	else if (!(link->flags & ATA_LFLAG_DISABLED) || ata_is_host_link(link))
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
void ata_eh_analyze_ncq_error(struct ata_link *link)
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
	memset(&tf, 0, sizeof(tf));
	rc = ata_eh_read_log_10h(dev, &tag, &tf);
	if (rc) {
		ata_link_err(link, "failed to read log page 10h (errno=%d)\n",
			     rc);
		return;
	}

	if (!(link->sactive & (1 << tag))) {
		ata_link_err(link, "log page 10h reported inactive tag %d\n",
			     tag);
		return;
	}

	/* we've got the perpetrator, condemn it */
	qc = __ata_qc_from_tag(ap, tag);
	memcpy(&qc->result_tf, &tf, sizeof(tf));
	qc->result_tf.flags = ATA_TFLAG_ISADDR | ATA_TFLAG_LBA | ATA_TFLAG_LBA48;
	qc->err_mask |= AC_ERR_DEV | AC_ERR_NCQ;
	if ((qc->result_tf.command & ATA_SENSE) || qc->result_tf.auxiliary) {
		char sense_key, asc, ascq;

		sense_key = (qc->result_tf.auxiliary >> 16) & 0xff;
		asc = (qc->result_tf.auxiliary >> 8) & 0xff;
		ascq = qc->result_tf.auxiliary & 0xff;
		ata_scsi_set_sense(dev, qc->scsicmd, sense_key, asc, ascq);
		ata_scsi_set_sense_information(dev, qc->scsicmd,
					       &qc->result_tf);
		qc->flags |= ATA_QCFLAG_SENSE_VALID;
	}

	ehc->i.err_mask &= ~AC_ERR_DEV;
}

/**
 *	ata_eh_analyze_tf - analyze taskfile of a failed qc
 *	@qc: qc to analyze
 *	@tf: Taskfile registers to analyze
 *
 *	Analyze taskfile of @qc and further determine cause of
 *	failure.  This function also requests ATAPI sense data if
 *	available.
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

	if (stat & (ATA_ERR | ATA_DF)) {
		qc->err_mask |= AC_ERR_DEV;
		/*
		 * Sense data reporting does not work if the
		 * device fault bit is set.
		 */
		if (stat & ATA_DF)
			stat &= ~ATA_SENSE;
	} else {
		return 0;
	}

	switch (qc->dev->class) {
	case ATA_DEV_ATA:
	case ATA_DEV_ZAC:
		if (stat & ATA_SENSE)
			ata_eh_request_sense(qc, qc->scsicmd);
		if (err & ATA_ICRC)
			qc->err_mask |= AC_ERR_ATA_BUS;
		if (err & (ATA_UNC | ATA_AMNF))
			qc->err_mask |= AC_ERR_MEDIA;
		if (err & ATA_IDNF)
			qc->err_mask |= AC_ERR_INVALID;
		break;

	case ATA_DEV_ATAPI:
		if (!(qc->ap->pflags & ATA_PFLAG_FROZEN)) {
			tmp = atapi_eh_request_sense(qc->dev,
						qc->scsicmd->sense_buffer,
						qc->result_tf.feature >> 4);
			if (!tmp)
				qc->flags |= ATA_QCFLAG_SENSE_VALID;
			else
				qc->err_mask |= tmp;
		}
	}

	if (qc->flags & ATA_QCFLAG_SENSE_VALID) {
		int ret = scsi_check_sense(qc->scsicmd);
		/*
		 * SUCCESS here means that the sense code could
		 * evaluated and should be passed to the upper layers
		 * for correct evaluation.
		 * FAILED means the sense code could not interpreted
		 * and the device would need to be reset.
		 * NEEDS_RETRY and ADD_TO_MLQUEUE means that the
		 * command would need to be retried.
		 */
		if (ret == NEEDS_RETRY || ret == ADD_TO_MLQUEUE) {
			qc->flags |= ATA_QCFLAG_RETRY;
			qc->err_mask |= AC_ERR_OTHER;
		} else if (ret != SUCCESS) {
			qc->err_mask |= AC_ERR_HSM;
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

	if ((ent->eflags & ATA_EFLAG_OLD_ER) || (ent->timestamp < arg->since))
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
 *	   occurred during last 5 mins, FALLBACK_TO_PIO
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
	struct ata_link *link = ata_dev_phys_link(dev);
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
		ata_dev_warn(dev, "NCQ disabled due to excessive errors\n");
		goto done;
	}

	/* speed down? */
	if (verdict & ATA_EH_SPDN_SPEED_DOWN) {
		/* speed down SATA link speed if possible */
		if (sata_down_spd_limit(link, 0) == 0) {
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
 *	ata_eh_worth_retry - analyze error and decide whether to retry
 *	@qc: qc to possibly retry
 *
 *	Look at the cause of the error and decide if a retry
 * 	might be useful or not.  We don't want to retry media errors
 *	because the drive itself has probably already taken 10-30 seconds
 *	doing its own internal retries before reporting the failure.
 */
static inline int ata_eh_worth_retry(struct ata_queued_cmd *qc)
{
	if (qc->err_mask & AC_ERR_MEDIA)
		return 0;	/* don't retry media errors */
	if (qc->flags & ATA_QCFLAG_IO)
		return 1;	/* otherwise retry anything from fs stack */
	if (qc->err_mask & AC_ERR_INVALID)
		return 0;	/* don't retry these */
	return qc->err_mask != AC_ERR_DEV;  /* retry if not dev error */
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

		if (!(qc->flags & ATA_QCFLAG_FAILED) ||
		    ata_dev_phys_link(qc->dev) != link)
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
		if (ata_eh_worth_retry(qc))
			qc->flags |= ATA_QCFLAG_RETRY;

		/* accumulate error info */
		ehc->i.dev = qc->dev;
		all_err_mask |= qc->err_mask;
		if (qc->flags & ATA_QCFLAG_IO)
			eflags |= ATA_EFLAG_IS_IO;
		trace_ata_eh_link_autopsy_qc(qc);
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
	trace_ata_eh_link_autopsy(dev, ehc->i.action, all_err_mask);
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

	ata_for_each_link(link, ap, EDGE)
		ata_eh_link_autopsy(link);

	/* Handle the frigging slave link.  Autopsy is done similarly
	 * but actions and flags are transferred over to the master
	 * link and handled from there.
	 */
	if (ap->slave_link) {
		struct ata_eh_context *mehc = &ap->link.eh_context;
		struct ata_eh_context *sehc = &ap->slave_link->eh_context;

		/* transfer control flags from master to slave */
		sehc->i.flags |= mehc->i.flags & ATA_EHI_TO_SLAVE_MASK;

		/* perform autopsy on the slave link */
		ata_eh_link_autopsy(ap->slave_link);

		/* transfer actions from slave to master and clear slave */
		ata_eh_about_to_do(ap->slave_link, NULL, ATA_EH_ALL_ACTIONS);
		mehc->i.action		|= sehc->i.action;
		mehc->i.dev_action[1]	|= sehc->i.dev_action[1];
		mehc->i.flags		|= sehc->i.flags;
		ata_eh_done(ap->slave_link, NULL, ATA_EH_ALL_ACTIONS);
	}

	/* Autopsy of fanout ports can affect host link autopsy.
	 * Perform host link autopsy last.
	 */
	if (sata_pmp_attached(ap))
		ata_eh_link_autopsy(&ap->link);
}

/**
 *	ata_get_cmd_descript - get description for ATA command
 *	@command: ATA command code to get description for
 *
 *	Return a textual description of the given command, or NULL if the
 *	command is not known.
 *
 *	LOCKING:
 *	None
 */
const char *ata_get_cmd_descript(u8 command)
{
#ifdef CONFIG_ATA_VERBOSE_ERROR
	static const struct
	{
		u8 command;
		const char *text;
	} cmd_descr[] = {
		{ ATA_CMD_DEV_RESET,		"DEVICE RESET" },
		{ ATA_CMD_CHK_POWER,		"CHECK POWER MODE" },
		{ ATA_CMD_STANDBY,		"STANDBY" },
		{ ATA_CMD_IDLE,			"IDLE" },
		{ ATA_CMD_EDD,			"EXECUTE DEVICE DIAGNOSTIC" },
		{ ATA_CMD_DOWNLOAD_MICRO,	"DOWNLOAD MICROCODE" },
		{ ATA_CMD_DOWNLOAD_MICRO_DMA,	"DOWNLOAD MICROCODE DMA" },
		{ ATA_CMD_NOP,			"NOP" },
		{ ATA_CMD_FLUSH,		"FLUSH CACHE" },
		{ ATA_CMD_FLUSH_EXT,		"FLUSH CACHE EXT" },
		{ ATA_CMD_ID_ATA,		"IDENTIFY DEVICE" },
		{ ATA_CMD_ID_ATAPI,		"IDENTIFY PACKET DEVICE" },
		{ ATA_CMD_SERVICE,		"SERVICE" },
		{ ATA_CMD_READ,			"READ DMA" },
		{ ATA_CMD_READ_EXT,		"READ DMA EXT" },
		{ ATA_CMD_READ_QUEUED,		"READ DMA QUEUED" },
		{ ATA_CMD_READ_STREAM_EXT,	"READ STREAM EXT" },
		{ ATA_CMD_READ_STREAM_DMA_EXT,  "READ STREAM DMA EXT" },
		{ ATA_CMD_WRITE,		"WRITE DMA" },
		{ ATA_CMD_WRITE_EXT,		"WRITE DMA EXT" },
		{ ATA_CMD_WRITE_QUEUED,		"WRITE DMA QUEUED EXT" },
		{ ATA_CMD_WRITE_STREAM_EXT,	"WRITE STREAM EXT" },
		{ ATA_CMD_WRITE_STREAM_DMA_EXT, "WRITE STREAM DMA EXT" },
		{ ATA_CMD_WRITE_FUA_EXT,	"WRITE DMA FUA EXT" },
		{ ATA_CMD_WRITE_QUEUED_FUA_EXT, "WRITE DMA QUEUED FUA EXT" },
		{ ATA_CMD_FPDMA_READ,		"READ FPDMA QUEUED" },
		{ ATA_CMD_FPDMA_WRITE,		"WRITE FPDMA QUEUED" },
		{ ATA_CMD_FPDMA_SEND,		"SEND FPDMA QUEUED" },
		{ ATA_CMD_FPDMA_RECV,		"RECEIVE FPDMA QUEUED" },
		{ ATA_CMD_PIO_READ,		"READ SECTOR(S)" },
		{ ATA_CMD_PIO_READ_EXT,		"READ SECTOR(S) EXT" },
		{ ATA_CMD_PIO_WRITE,		"WRITE SECTOR(S)" },
		{ ATA_CMD_PIO_WRITE_EXT,	"WRITE SECTOR(S) EXT" },
		{ ATA_CMD_READ_MULTI,		"READ MULTIPLE" },
		{ ATA_CMD_READ_MULTI_EXT,	"READ MULTIPLE EXT" },
		{ ATA_CMD_WRITE_MULTI,		"WRITE MULTIPLE" },
		{ ATA_CMD_WRITE_MULTI_EXT,	"WRITE MULTIPLE EXT" },
		{ ATA_CMD_WRITE_MULTI_FUA_EXT,	"WRITE MULTIPLE FUA EXT" },
		{ ATA_CMD_SET_FEATURES,		"SET FEATURES" },
		{ ATA_CMD_SET_MULTI,		"SET MULTIPLE MODE" },
		{ ATA_CMD_VERIFY,		"READ VERIFY SECTOR(S)" },
		{ ATA_CMD_VERIFY_EXT,		"READ VERIFY SECTOR(S) EXT" },
		{ ATA_CMD_WRITE_UNCORR_EXT,	"WRITE UNCORRECTABLE EXT" },
		{ ATA_CMD_STANDBYNOW1,		"STANDBY IMMEDIATE" },
		{ ATA_CMD_IDLEIMMEDIATE,	"IDLE IMMEDIATE" },
		{ ATA_CMD_SLEEP,		"SLEEP" },
		{ ATA_CMD_INIT_DEV_PARAMS,	"INITIALIZE DEVICE PARAMETERS" },
		{ ATA_CMD_READ_NATIVE_MAX,	"READ NATIVE MAX ADDRESS" },
		{ ATA_CMD_READ_NATIVE_MAX_EXT,	"READ NATIVE MAX ADDRESS EXT" },
		{ ATA_CMD_SET_MAX,		"SET MAX ADDRESS" },
		{ ATA_CMD_SET_MAX_EXT,		"SET MAX ADDRESS EXT" },
		{ ATA_CMD_READ_LOG_EXT,		"READ LOG EXT" },
		{ ATA_CMD_WRITE_LOG_EXT,	"WRITE LOG EXT" },
		{ ATA_CMD_READ_LOG_DMA_EXT,	"READ LOG DMA EXT" },
		{ ATA_CMD_WRITE_LOG_DMA_EXT,	"WRITE LOG DMA EXT" },
		{ ATA_CMD_TRUSTED_NONDATA,	"TRUSTED NON-DATA" },
		{ ATA_CMD_TRUSTED_RCV,		"TRUSTED RECEIVE" },
		{ ATA_CMD_TRUSTED_RCV_DMA,	"TRUSTED RECEIVE DMA" },
		{ ATA_CMD_TRUSTED_SND,		"TRUSTED SEND" },
		{ ATA_CMD_TRUSTED_SND_DMA,	"TRUSTED SEND DMA" },
		{ ATA_CMD_PMP_READ,		"READ BUFFER" },
		{ ATA_CMD_PMP_READ_DMA,		"READ BUFFER DMA" },
		{ ATA_CMD_PMP_WRITE,		"WRITE BUFFER" },
		{ ATA_CMD_PMP_WRITE_DMA,	"WRITE BUFFER DMA" },
		{ ATA_CMD_CONF_OVERLAY,		"DEVICE CONFIGURATION OVERLAY" },
		{ ATA_CMD_SEC_SET_PASS,		"SECURITY SET PASSWORD" },
		{ ATA_CMD_SEC_UNLOCK,		"SECURITY UNLOCK" },
		{ ATA_CMD_SEC_ERASE_PREP,	"SECURITY ERASE PREPARE" },
		{ ATA_CMD_SEC_ERASE_UNIT,	"SECURITY ERASE UNIT" },
		{ ATA_CMD_SEC_FREEZE_LOCK,	"SECURITY FREEZE LOCK" },
		{ ATA_CMD_SEC_DISABLE_PASS,	"SECURITY DISABLE PASSWORD" },
		{ ATA_CMD_CONFIG_STREAM,	"CONFIGURE STREAM" },
		{ ATA_CMD_SMART,		"SMART" },
		{ ATA_CMD_MEDIA_LOCK,		"DOOR LOCK" },
		{ ATA_CMD_MEDIA_UNLOCK,		"DOOR UNLOCK" },
		{ ATA_CMD_DSM,			"DATA SET MANAGEMENT" },
		{ ATA_CMD_CHK_MED_CRD_TYP,	"CHECK MEDIA CARD TYPE" },
		{ ATA_CMD_CFA_REQ_EXT_ERR,	"CFA REQUEST EXTENDED ERROR" },
		{ ATA_CMD_CFA_WRITE_NE,		"CFA WRITE SECTORS WITHOUT ERASE" },
		{ ATA_CMD_CFA_TRANS_SECT,	"CFA TRANSLATE SECTOR" },
		{ ATA_CMD_CFA_ERASE,		"CFA ERASE SECTORS" },
		{ ATA_CMD_CFA_WRITE_MULT_NE,	"CFA WRITE MULTIPLE WITHOUT ERASE" },
		{ ATA_CMD_REQ_SENSE_DATA,	"REQUEST SENSE DATA EXT" },
		{ ATA_CMD_SANITIZE_DEVICE,	"SANITIZE DEVICE" },
		{ ATA_CMD_ZAC_MGMT_IN,		"ZAC MANAGEMENT IN" },
		{ ATA_CMD_ZAC_MGMT_OUT,		"ZAC MANAGEMENT OUT" },
		{ ATA_CMD_READ_LONG,		"READ LONG (with retries)" },
		{ ATA_CMD_READ_LONG_ONCE,	"READ LONG (without retries)" },
		{ ATA_CMD_WRITE_LONG,		"WRITE LONG (with retries)" },
		{ ATA_CMD_WRITE_LONG_ONCE,	"WRITE LONG (without retries)" },
		{ ATA_CMD_RESTORE,		"RECALIBRATE" },
		{ 0,				NULL } /* terminate list */
	};

	unsigned int i;
	for (i = 0; cmd_descr[i].text; i++)
		if (cmd_descr[i].command == command)
			return cmd_descr[i].text;
#endif

	return NULL;
}
EXPORT_SYMBOL_GPL(ata_get_cmd_descript);

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
	char tries_buf[6] = "";
	int tag, nr_failed = 0;

	if (ehc->i.flags & ATA_EHI_QUIET)
		return;

	desc = NULL;
	if (ehc->i.desc[0] != '\0')
		desc = ehc->i.desc;

	for (tag = 0; tag < ATA_MAX_QUEUE; tag++) {
		struct ata_queued_cmd *qc = __ata_qc_from_tag(ap, tag);

		if (!(qc->flags & ATA_QCFLAG_FAILED) ||
		    ata_dev_phys_link(qc->dev) != link ||
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

	if (ap->eh_tries < ATA_EH_MAX_TRIES)
		snprintf(tries_buf, sizeof(tries_buf), " t%d",
			 ap->eh_tries);

	if (ehc->i.dev) {
		ata_dev_err(ehc->i.dev, "exception Emask 0x%x "
			    "SAct 0x%x SErr 0x%x action 0x%x%s%s\n",
			    ehc->i.err_mask, link->sactive, ehc->i.serror,
			    ehc->i.action, frozen, tries_buf);
		if (desc)
			ata_dev_err(ehc->i.dev, "%s\n", desc);
	} else {
		ata_link_err(link, "exception Emask 0x%x "
			     "SAct 0x%x SErr 0x%x action 0x%x%s%s\n",
			     ehc->i.err_mask, link->sactive, ehc->i.serror,
			     ehc->i.action, frozen, tries_buf);
		if (desc)
			ata_link_err(link, "%s\n", desc);
	}

#ifdef CONFIG_ATA_VERBOSE_ERROR
	if (ehc->i.serror)
		ata_link_err(link,
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
#endif

	for (tag = 0; tag < ATA_MAX_QUEUE; tag++) {
		struct ata_queued_cmd *qc = __ata_qc_from_tag(ap, tag);
		struct ata_taskfile *cmd = &qc->tf, *res = &qc->result_tf;
		char data_buf[20] = "";
		char cdb_buf[70] = "";

		if (!(qc->flags & ATA_QCFLAG_FAILED) ||
		    ata_dev_phys_link(qc->dev) != link || !qc->err_mask)
			continue;

		if (qc->dma_dir != DMA_NONE) {
			static const char *dma_str[] = {
				[DMA_BIDIRECTIONAL]	= "bidi",
				[DMA_TO_DEVICE]		= "out",
				[DMA_FROM_DEVICE]	= "in",
			};
			static const char *prot_str[] = {
				[ATA_PROT_UNKNOWN]	= "unknown",
				[ATA_PROT_NODATA]	= "nodata",
				[ATA_PROT_PIO]		= "pio",
				[ATA_PROT_DMA]		= "dma",
				[ATA_PROT_NCQ]		= "ncq dma",
				[ATA_PROT_NCQ_NODATA]	= "ncq nodata",
				[ATAPI_PROT_NODATA]	= "nodata",
				[ATAPI_PROT_PIO]	= "pio",
				[ATAPI_PROT_DMA]	= "dma",
			};

			snprintf(data_buf, sizeof(data_buf), " %s %u %s",
				 prot_str[qc->tf.protocol], qc->nbytes,
				 dma_str[qc->dma_dir]);
		}

		if (ata_is_atapi(qc->tf.protocol)) {
			const u8 *cdb = qc->cdb;
			size_t cdb_len = qc->dev->cdb_len;

			if (qc->scsicmd) {
				cdb = qc->scsicmd->cmnd;
				cdb_len = qc->scsicmd->cmd_len;
			}
			__scsi_format_command(cdb_buf, sizeof(cdb_buf),
					      cdb, cdb_len);
		} else {
			const char *descr = ata_get_cmd_descript(cmd->command);
			if (descr)
				ata_dev_err(qc->dev, "failed command: %s\n",
					    descr);
		}

		ata_dev_err(qc->dev,
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

#ifdef CONFIG_ATA_VERBOSE_ERROR
		if (res->command & (ATA_BUSY | ATA_DRDY | ATA_DF | ATA_DRQ |
				    ATA_SENSE | ATA_ERR)) {
			if (res->command & ATA_BUSY)
				ata_dev_err(qc->dev, "status: { Busy }\n");
			else
				ata_dev_err(qc->dev, "status: { %s%s%s%s%s}\n",
				  res->command & ATA_DRDY ? "DRDY " : "",
				  res->command & ATA_DF ? "DF " : "",
				  res->command & ATA_DRQ ? "DRQ " : "",
				  res->command & ATA_SENSE ? "SENSE " : "",
				  res->command & ATA_ERR ? "ERR " : "");
		}

		if (cmd->command != ATA_CMD_PACKET &&
		    (res->feature & (ATA_ICRC | ATA_UNC | ATA_AMNF |
				     ATA_IDNF | ATA_ABORTED)))
			ata_dev_err(qc->dev, "error: { %s%s%s%s%s}\n",
			  res->feature & ATA_ICRC ? "ICRC " : "",
			  res->feature & ATA_UNC ? "UNC " : "",
			  res->feature & ATA_AMNF ? "AMNF " : "",
			  res->feature & ATA_IDNF ? "IDNF " : "",
			  res->feature & ATA_ABORTED ? "ABRT " : "");
#endif
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

	ata_for_each_link(link, ap, HOST_FIRST)
		ata_eh_link_report(link);
}

static int ata_do_reset(struct ata_link *link, ata_reset_fn_t reset,
			unsigned int *classes, unsigned long deadline,
			bool clear_classes)
{
	struct ata_device *dev;

	if (clear_classes)
		ata_for_each_dev(dev, link, ALL)
			classes[dev->devno] = ATA_DEV_UNKNOWN;

	return reset(link, classes, deadline);
}

static int ata_eh_followup_srst_needed(struct ata_link *link, int rc)
{
	if ((link->flags & ATA_LFLAG_NO_SRST) || ata_link_offline(link))
		return 0;
	if (rc == -EAGAIN)
		return 1;
	if (sata_pmp_supported(link->ap) && ata_is_host_link(link))
		return 1;
	return 0;
}

int ata_eh_reset(struct ata_link *link, int classify,
		 ata_prereset_fn_t prereset, ata_reset_fn_t softreset,
		 ata_reset_fn_t hardreset, ata_postreset_fn_t postreset)
{
	struct ata_port *ap = link->ap;
	struct ata_link *slave = ap->slave_link;
	struct ata_eh_context *ehc = &link->eh_context;
	struct ata_eh_context *sehc = slave ? &slave->eh_context : NULL;
	unsigned int *classes = ehc->classes;
	unsigned int lflags = link->flags;
	int verbose = !(ehc->i.flags & ATA_EHI_QUIET);
	int max_tries = 0, try = 0;
	struct ata_link *failed_link;
	struct ata_device *dev;
	unsigned long deadline, now;
	ata_reset_fn_t reset;
	unsigned long flags;
	u32 sstatus;
	int nr_unknown, rc;

	/*
	 * Prepare to reset
	 */
	while (ata_eh_reset_timeouts[max_tries] != ULONG_MAX)
		max_tries++;
	if (link->flags & ATA_LFLAG_RST_ONCE)
		max_tries = 1;
	if (link->flags & ATA_LFLAG_NO_HRST)
		hardreset = NULL;
	if (link->flags & ATA_LFLAG_NO_SRST)
		softreset = NULL;

	/* make sure each reset attempt is at least COOL_DOWN apart */
	if (ehc->i.flags & ATA_EHI_DID_RESET) {
		now = jiffies;
		WARN_ON(time_after(ehc->last_reset, now));
		deadline = ata_deadline(ehc->last_reset,
					ATA_EH_RESET_COOL_DOWN);
		if (time_before(now, deadline))
			schedule_timeout_uninterruptible(deadline - now);
	}

	spin_lock_irqsave(ap->lock, flags);
	ap->pflags |= ATA_PFLAG_RESETTING;
	spin_unlock_irqrestore(ap->lock, flags);

	ata_eh_about_to_do(link, NULL, ATA_EH_RESET);

	ata_for_each_dev(dev, link, ALL) {
		/* If we issue an SRST then an ATA drive (not ATAPI)
		 * may change configuration and be in PIO0 timing. If
		 * we do a hard reset (or are coming from power on)
		 * this is true for ATA or ATAPI. Until we've set a
		 * suitable controller mode we should not touch the
		 * bus as we may be talking too fast.
		 */
		dev->pio_mode = XFER_PIO_0;
		dev->dma_mode = 0xff;

		/* If the controller has a pio mode setup function
		 * then use it to set the chipset to rights. Don't
		 * touch the DMA setup as that will be dealt with when
		 * configuring devices.
		 */
		if (ap->ops->set_piomode)
			ap->ops->set_piomode(ap, dev);
	}

	/* prefer hardreset */
	reset = NULL;
	ehc->i.action &= ~ATA_EH_RESET;
	if (hardreset) {
		reset = hardreset;
		ehc->i.action |= ATA_EH_HARDRESET;
	} else if (softreset) {
		reset = softreset;
		ehc->i.action |= ATA_EH_SOFTRESET;
	}

	if (prereset) {
		unsigned long deadline = ata_deadline(jiffies,
						      ATA_EH_PRERESET_TIMEOUT);

		if (slave) {
			sehc->i.action &= ~ATA_EH_RESET;
			sehc->i.action |= ehc->i.action;
		}

		rc = prereset(link, deadline);

		/* If present, do prereset on slave link too.  Reset
		 * is skipped iff both master and slave links report
		 * -ENOENT or clear ATA_EH_RESET.
		 */
		if (slave && (rc == 0 || rc == -ENOENT)) {
			int tmp;

			tmp = prereset(slave, deadline);
			if (tmp != -ENOENT)
				rc = tmp;

			ehc->i.action |= sehc->i.action;
		}

		if (rc) {
			if (rc == -ENOENT) {
				ata_link_dbg(link, "port disabled--ignoring\n");
				ehc->i.action &= ~ATA_EH_RESET;

				ata_for_each_dev(dev, link, ALL)
					classes[dev->devno] = ATA_DEV_NONE;

				rc = 0;
			} else
				ata_link_err(link,
					     "prereset failed (errno=%d)\n",
					     rc);
			goto out;
		}

		/* prereset() might have cleared ATA_EH_RESET.  If so,
		 * bang classes, thaw and return.
		 */
		if (reset && !(ehc->i.action & ATA_EH_RESET)) {
			ata_for_each_dev(dev, link, ALL)
				classes[dev->devno] = ATA_DEV_NONE;
			if ((ap->pflags & ATA_PFLAG_FROZEN) &&
			    ata_is_host_link(link))
				ata_eh_thaw_port(ap);
			rc = 0;
			goto out;
		}
	}

 retry:
	/*
	 * Perform reset
	 */
	if (ata_is_host_link(link))
		ata_eh_freeze_port(ap);

	deadline = ata_deadline(jiffies, ata_eh_reset_timeouts[try++]);

	if (reset) {
		if (verbose)
			ata_link_info(link, "%s resetting link\n",
				      reset == softreset ? "soft" : "hard");

		/* mark that this EH session started with reset */
		ehc->last_reset = jiffies;
		if (reset == hardreset)
			ehc->i.flags |= ATA_EHI_DID_HARDRESET;
		else
			ehc->i.flags |= ATA_EHI_DID_SOFTRESET;

		rc = ata_do_reset(link, reset, classes, deadline, true);
		if (rc && rc != -EAGAIN) {
			failed_link = link;
			goto fail;
		}

		/* hardreset slave link if existent */
		if (slave && reset == hardreset) {
			int tmp;

			if (verbose)
				ata_link_info(slave, "hard resetting link\n");

			ata_eh_about_to_do(slave, NULL, ATA_EH_RESET);
			tmp = ata_do_reset(slave, reset, classes, deadline,
					   false);
			switch (tmp) {
			case -EAGAIN:
				rc = -EAGAIN;
			case 0:
				break;
			default:
				failed_link = slave;
				rc = tmp;
				goto fail;
			}
		}

		/* perform follow-up SRST if necessary */
		if (reset == hardreset &&
		    ata_eh_followup_srst_needed(link, rc)) {
			reset = softreset;

			if (!reset) {
				ata_link_err(link,
	     "follow-up softreset required but no softreset available\n");
				failed_link = link;
				rc = -EINVAL;
				goto fail;
			}

			ata_eh_about_to_do(link, NULL, ATA_EH_RESET);
			rc = ata_do_reset(link, reset, classes, deadline, true);
			if (rc) {
				failed_link = link;
				goto fail;
			}
		}
	} else {
		if (verbose)
			ata_link_info(link,
	"no reset method available, skipping reset\n");
		if (!(lflags & ATA_LFLAG_ASSUME_CLASS))
			lflags |= ATA_LFLAG_ASSUME_ATA;
	}

	/*
	 * Post-reset processing
	 */
	ata_for_each_dev(dev, link, ALL) {
		/* After the reset, the device state is PIO 0 and the
		 * controller state is undefined.  Reset also wakes up
		 * drives from sleeping mode.
		 */
		dev->pio_mode = XFER_PIO_0;
		dev->flags &= ~ATA_DFLAG_SLEEPING;

		if (ata_phys_link_offline(ata_dev_phys_link(dev)))
			continue;

		/* apply class override */
		if (lflags & ATA_LFLAG_ASSUME_ATA)
			classes[dev->devno] = ATA_DEV_ATA;
		else if (lflags & ATA_LFLAG_ASSUME_SEMB)
			classes[dev->devno] = ATA_DEV_SEMB_UNSUP;
	}

	/* record current link speed */
	if (sata_scr_read(link, SCR_STATUS, &sstatus) == 0)
		link->sata_spd = (sstatus >> 4) & 0xf;
	if (slave && sata_scr_read(slave, SCR_STATUS, &sstatus) == 0)
		slave->sata_spd = (sstatus >> 4) & 0xf;

	/* thaw the port */
	if (ata_is_host_link(link))
		ata_eh_thaw_port(ap);

	/* postreset() should clear hardware SError.  Although SError
	 * is cleared during link resume, clearing SError here is
	 * necessary as some PHYs raise hotplug events after SRST.
	 * This introduces race condition where hotplug occurs between
	 * reset and here.  This race is mediated by cross checking
	 * link onlineness and classification result later.
	 */
	if (postreset) {
		postreset(link, classes);
		if (slave)
			postreset(slave, classes);
	}

	/*
	 * Some controllers can't be frozen very well and may set spurious
	 * error conditions during reset.  Clear accumulated error
	 * information and re-thaw the port if frozen.  As reset is the
	 * final recovery action and we cross check link onlineness against
	 * device classification later, no hotplug event is lost by this.
	 */
	spin_lock_irqsave(link->ap->lock, flags);
	memset(&link->eh_info, 0, sizeof(link->eh_info));
	if (slave)
		memset(&slave->eh_info, 0, sizeof(link->eh_info));
	ap->pflags &= ~ATA_PFLAG_EH_PENDING;
	spin_unlock_irqrestore(link->ap->lock, flags);

	if (ap->pflags & ATA_PFLAG_FROZEN)
		ata_eh_thaw_port(ap);

	/*
	 * Make sure onlineness and classification result correspond.
	 * Hotplug could have happened during reset and some
	 * controllers fail to wait while a drive is spinning up after
	 * being hotplugged causing misdetection.  By cross checking
	 * link on/offlineness and classification result, those
	 * conditions can be reliably detected and retried.
	 */
	nr_unknown = 0;
	ata_for_each_dev(dev, link, ALL) {
		if (ata_phys_link_online(ata_dev_phys_link(dev))) {
			if (classes[dev->devno] == ATA_DEV_UNKNOWN) {
				ata_dev_dbg(dev, "link online but device misclassified\n");
				classes[dev->devno] = ATA_DEV_NONE;
				nr_unknown++;
			}
		} else if (ata_phys_link_offline(ata_dev_phys_link(dev))) {
			if (ata_class_enabled(classes[dev->devno]))
				ata_dev_dbg(dev,
					    "link offline, clearing class %d to NONE\n",
					    classes[dev->devno]);
			classes[dev->devno] = ATA_DEV_NONE;
		} else if (classes[dev->devno] == ATA_DEV_UNKNOWN) {
			ata_dev_dbg(dev,
				    "link status unknown, clearing UNKNOWN to NONE\n");
			classes[dev->devno] = ATA_DEV_NONE;
		}
	}

	if (classify && nr_unknown) {
		if (try < max_tries) {
			ata_link_warn(link,
				      "link online but %d devices misclassified, retrying\n",
				      nr_unknown);
			failed_link = link;
			rc = -EAGAIN;
			goto fail;
		}
		ata_link_warn(link,
			      "link online but %d devices misclassified, "
			      "device detection might fail\n", nr_unknown);
	}

	/* reset successful, schedule revalidation */
	ata_eh_done(link, NULL, ATA_EH_RESET);
	if (slave)
		ata_eh_done(slave, NULL, ATA_EH_RESET);
	ehc->last_reset = jiffies;		/* update to completion time */
	ehc->i.action |= ATA_EH_REVALIDATE;
	link->lpm_policy = ATA_LPM_UNKNOWN;	/* reset LPM state */

	rc = 0;
 out:
	/* clear hotplug flag */
	ehc->i.flags &= ~ATA_EHI_HOTPLUGGED;
	if (slave)
		sehc->i.flags &= ~ATA_EHI_HOTPLUGGED;

	spin_lock_irqsave(ap->lock, flags);
	ap->pflags &= ~ATA_PFLAG_RESETTING;
	spin_unlock_irqrestore(ap->lock, flags);

	return rc;

 fail:
	/* if SCR isn't accessible on a fan-out port, PMP needs to be reset */
	if (!ata_is_host_link(link) &&
	    sata_scr_read(link, SCR_STATUS, &sstatus))
		rc = -ERESTART;

	if (try >= max_tries) {
		/*
		 * Thaw host port even if reset failed, so that the port
		 * can be retried on the next phy event.  This risks
		 * repeated EH runs but seems to be a better tradeoff than
		 * shutting down a port after a botched hotplug attempt.
		 */
		if (ata_is_host_link(link))
			ata_eh_thaw_port(ap);
		goto out;
	}

	now = jiffies;
	if (time_before(now, deadline)) {
		unsigned long delta = deadline - now;

		ata_link_warn(failed_link,
			"reset failed (errno=%d), retrying in %u secs\n",
			rc, DIV_ROUND_UP(jiffies_to_msecs(delta), 1000));

		ata_eh_release(ap);
		while (delta)
			delta = schedule_timeout_uninterruptible(delta);
		ata_eh_acquire(ap);
	}

	/*
	 * While disks spinup behind PMP, some controllers fail sending SRST.
	 * They need to be reset - as well as the PMP - before retrying.
	 */
	if (rc == -ERESTART) {
		if (ata_is_host_link(link))
			ata_eh_thaw_port(ap);
		goto out;
	}

	if (try == max_tries - 1) {
		sata_down_spd_limit(link, 0);
		if (slave)
			sata_down_spd_limit(slave, 0);
	} else if (rc == -EPIPE)
		sata_down_spd_limit(failed_link, 0);

	if (hardreset)
		reset = hardreset;
	goto retry;
}

static inline void ata_eh_pull_park_action(struct ata_port *ap)
{
	struct ata_link *link;
	struct ata_device *dev;
	unsigned long flags;

	/*
	 * This function can be thought of as an extended version of
	 * ata_eh_about_to_do() specially crafted to accommodate the
	 * requirements of ATA_EH_PARK handling. Since the EH thread
	 * does not leave the do {} while () loop in ata_eh_recover as
	 * long as the timeout for a park request to *one* device on
	 * the port has not expired, and since we still want to pick
	 * up park requests to other devices on the same port or
	 * timeout updates for the same device, we have to pull
	 * ATA_EH_PARK actions from eh_info into eh_context.i
	 * ourselves at the beginning of each pass over the loop.
	 *
	 * Additionally, all write accesses to &ap->park_req_pending
	 * through reinit_completion() (see below) or complete_all()
	 * (see ata_scsi_park_store()) are protected by the host lock.
	 * As a result we have that park_req_pending.done is zero on
	 * exit from this function, i.e. when ATA_EH_PARK actions for
	 * *all* devices on port ap have been pulled into the
	 * respective eh_context structs. If, and only if,
	 * park_req_pending.done is non-zero by the time we reach
	 * wait_for_completion_timeout(), another ATA_EH_PARK action
	 * has been scheduled for at least one of the devices on port
	 * ap and we have to cycle over the do {} while () loop in
	 * ata_eh_recover() again.
	 */

	spin_lock_irqsave(ap->lock, flags);
	reinit_completion(&ap->park_req_pending);
	ata_for_each_link(link, ap, EDGE) {
		ata_for_each_dev(dev, link, ALL) {
			struct ata_eh_info *ehi = &link->eh_info;

			link->eh_context.i.dev_action[dev->devno] |=
				ehi->dev_action[dev->devno] & ATA_EH_PARK;
			ata_eh_clear_action(link, dev, ehi, ATA_EH_PARK);
		}
	}
	spin_unlock_irqrestore(ap->lock, flags);
}

static void ata_eh_park_issue_cmd(struct ata_device *dev, int park)
{
	struct ata_eh_context *ehc = &dev->link->eh_context;
	struct ata_taskfile tf;
	unsigned int err_mask;

	ata_tf_init(dev, &tf);
	if (park) {
		ehc->unloaded_mask |= 1 << dev->devno;
		tf.command = ATA_CMD_IDLEIMMEDIATE;
		tf.feature = 0x44;
		tf.lbal = 0x4c;
		tf.lbam = 0x4e;
		tf.lbah = 0x55;
	} else {
		ehc->unloaded_mask &= ~(1 << dev->devno);
		tf.command = ATA_CMD_CHK_POWER;
	}

	tf.flags |= ATA_TFLAG_DEVICE | ATA_TFLAG_ISADDR;
	tf.protocol = ATA_PROT_NODATA;
	err_mask = ata_exec_internal(dev, &tf, NULL, DMA_NONE, NULL, 0, 0);
	if (park && (err_mask || tf.lbal != 0xc4)) {
		ata_dev_err(dev, "head unload failed!\n");
		ehc->unloaded_mask &= ~(1 << dev->devno);
	}
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
	ata_for_each_dev(dev, link, ALL_REVERSE) {
		unsigned int action = ata_eh_dev_action(dev);
		unsigned int readid_flags = 0;

		if (ehc->i.flags & ATA_EHI_DID_RESET)
			readid_flags |= ATA_READID_POSTRESET;

		if ((action & ATA_EH_REVALIDATE) && ata_dev_enabled(dev)) {
			WARN_ON(dev->class == ATA_DEV_PMP);

			if (ata_phys_link_offline(ata_dev_phys_link(dev))) {
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
			schedule_work(&(ap->scsi_rescan_task));
		} else if (dev->class == ATA_DEV_UNKNOWN &&
			   ehc->tries[dev->devno] &&
			   ata_class_enabled(ehc->classes[dev->devno])) {
			/* Temporarily set dev->class, it will be
			 * permanently set once all configurations are
			 * complete.  This is necessary because new
			 * device configuration is done in two
			 * separate loops.
			 */
			dev->class = ehc->classes[dev->devno];

			if (dev->class == ATA_DEV_PMP)
				rc = sata_pmp_attach(dev);
			else
				rc = ata_dev_read_id(dev, &dev->class,
						     readid_flags, dev->id);

			/* read_id might have changed class, store and reset */
			ehc->classes[dev->devno] = dev->class;
			dev->class = ATA_DEV_UNKNOWN;

			switch (rc) {
			case 0:
				/* clear error info accumulated during probe */
				ata_ering_clear(&dev->ering);
				new_mask |= 1 << dev->devno;
				break;
			case -ENOENT:
				/* IDENTIFY was issued to non-existent
				 * device.  No need to reset.  Just
				 * thaw and ignore the device.
				 */
				ata_eh_thaw_port(ap);
				break;
			default:
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
	ata_for_each_dev(dev, link, ALL) {
		if (!(new_mask & (1 << dev->devno)))
			continue;

		dev->class = ehc->classes[dev->devno];

		if (dev->class == ATA_DEV_PMP)
			continue;

		ehc->i.flags |= ATA_EHI_PRINTINFO;
		rc = ata_dev_configure(dev);
		ehc->i.flags &= ~ATA_EHI_PRINTINFO;
		if (rc) {
			dev->class = ATA_DEV_UNKNOWN;
			goto err;
		}

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
 *	@r_failed_dev: out parameter for failed device
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
	ata_for_each_dev(dev, link, ENABLED) {
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
	ata_for_each_dev(dev, link, ENABLED) {
		struct ata_eh_context *ehc = &link->eh_context;
		u8 saved_xfer_mode = ehc->saved_xfer_mode[dev->devno];
		u8 saved_ncq = !!(ehc->saved_ncq_enabled & (1 << dev->devno));

		if (dev->xfer_mode != saved_xfer_mode ||
		    ata_ncq_enabled(dev) != saved_ncq)
			dev->flags |= ATA_DFLAG_DUBIOUS_XFER;
	}

	return rc;
}

/**
 *	atapi_eh_clear_ua - Clear ATAPI UNIT ATTENTION after reset
 *	@dev: ATAPI device to clear UA for
 *
 *	Resets and other operations can make an ATAPI device raise
 *	UNIT ATTENTION which causes the next operation to fail.  This
 *	function clears UA.
 *
 *	LOCKING:
 *	EH context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
static int atapi_eh_clear_ua(struct ata_device *dev)
{
	int i;

	for (i = 0; i < ATA_EH_UA_TRIES; i++) {
		u8 *sense_buffer = dev->link->ap->sector_buf;
		u8 sense_key = 0;
		unsigned int err_mask;

		err_mask = atapi_eh_tur(dev, &sense_key);
		if (err_mask != 0 && err_mask != AC_ERR_DEV) {
			ata_dev_warn(dev,
				     "TEST_UNIT_READY failed (err_mask=0x%x)\n",
				     err_mask);
			return -EIO;
		}

		if (!err_mask || sense_key != UNIT_ATTENTION)
			return 0;

		err_mask = atapi_eh_request_sense(dev, sense_buffer, sense_key);
		if (err_mask) {
			ata_dev_warn(dev, "failed to clear "
				"UNIT ATTENTION (err_mask=0x%x)\n", err_mask);
			return -EIO;
		}
	}

	ata_dev_warn(dev, "UNIT ATTENTION persists after %d tries\n",
		     ATA_EH_UA_TRIES);

	return 0;
}

/**
 *	ata_eh_maybe_retry_flush - Retry FLUSH if necessary
 *	@dev: ATA device which may need FLUSH retry
 *
 *	If @dev failed FLUSH, it needs to be reported upper layer
 *	immediately as it means that @dev failed to remap and already
 *	lost at least a sector and further FLUSH retrials won't make
 *	any difference to the lost sector.  However, if FLUSH failed
 *	for other reasons, for example transmission error, FLUSH needs
 *	to be retried.
 *
 *	This function determines whether FLUSH failure retry is
 *	necessary and performs it if so.
 *
 *	RETURNS:
 *	0 if EH can continue, -errno if EH needs to be repeated.
 */
static int ata_eh_maybe_retry_flush(struct ata_device *dev)
{
	struct ata_link *link = dev->link;
	struct ata_port *ap = link->ap;
	struct ata_queued_cmd *qc;
	struct ata_taskfile tf;
	unsigned int err_mask;
	int rc = 0;

	/* did flush fail for this device? */
	if (!ata_tag_valid(link->active_tag))
		return 0;

	qc = __ata_qc_from_tag(ap, link->active_tag);
	if (qc->dev != dev || (qc->tf.command != ATA_CMD_FLUSH_EXT &&
			       qc->tf.command != ATA_CMD_FLUSH))
		return 0;

	/* if the device failed it, it should be reported to upper layers */
	if (qc->err_mask & AC_ERR_DEV)
		return 0;

	/* flush failed for some other reason, give it another shot */
	ata_tf_init(dev, &tf);

	tf.command = qc->tf.command;
	tf.flags |= ATA_TFLAG_DEVICE;
	tf.protocol = ATA_PROT_NODATA;

	ata_dev_warn(dev, "retrying FLUSH 0x%x Emask 0x%x\n",
		       tf.command, qc->err_mask);

	err_mask = ata_exec_internal(dev, &tf, NULL, DMA_NONE, NULL, 0, 0);
	if (!err_mask) {
		/*
		 * FLUSH is complete but there's no way to
		 * successfully complete a failed command from EH.
		 * Making sure retry is allowed at least once and
		 * retrying it should do the trick - whatever was in
		 * the cache is already on the platter and this won't
		 * cause infinite loop.
		 */
		qc->scsicmd->allowed = max(qc->scsicmd->allowed, 1);
	} else {
		ata_dev_warn(dev, "FLUSH failed Emask 0x%x\n",
			       err_mask);
		rc = -EIO;

		/* if device failed it, report it to upper layers */
		if (err_mask & AC_ERR_DEV) {
			qc->err_mask |= AC_ERR_DEV;
			qc->result_tf = tf;
			if (!(ap->pflags & ATA_PFLAG_FROZEN))
				rc = 0;
		}
	}
	return rc;
}

/**
 *	ata_eh_set_lpm - configure SATA interface power management
 *	@link: link to configure power management
 *	@policy: the link power management policy
 *	@r_failed_dev: out parameter for failed device
 *
 *	Enable SATA Interface power management.  This will enable
 *	Device Interface Power Management (DIPM) for min_power
 * 	policy, and then call driver specific callbacks for
 *	enabling Host Initiated Power management.
 *
 *	LOCKING:
 *	EH context.
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
static int ata_eh_set_lpm(struct ata_link *link, enum ata_lpm_policy policy,
			  struct ata_device **r_failed_dev)
{
	struct ata_port *ap = ata_is_host_link(link) ? link->ap : NULL;
	struct ata_eh_context *ehc = &link->eh_context;
	struct ata_device *dev, *link_dev = NULL, *lpm_dev = NULL;
	enum ata_lpm_policy old_policy = link->lpm_policy;
	bool no_dipm = link->ap->flags & ATA_FLAG_NO_DIPM;
	unsigned int hints = ATA_LPM_EMPTY | ATA_LPM_HIPM;
	unsigned int err_mask;
	int rc;

	/* if the link or host doesn't do LPM, noop */
	if ((link->flags & ATA_LFLAG_NO_LPM) || (ap && !ap->ops->set_lpm))
		return 0;

	/*
	 * DIPM is enabled only for MIN_POWER as some devices
	 * misbehave when the host NACKs transition to SLUMBER.  Order
	 * device and link configurations such that the host always
	 * allows DIPM requests.
	 */
	ata_for_each_dev(dev, link, ENABLED) {
		bool hipm = ata_id_has_hipm(dev->id);
		bool dipm = ata_id_has_dipm(dev->id) && !no_dipm;

		/* find the first enabled and LPM enabled devices */
		if (!link_dev)
			link_dev = dev;

		if (!lpm_dev && (hipm || dipm))
			lpm_dev = dev;

		hints &= ~ATA_LPM_EMPTY;
		if (!hipm)
			hints &= ~ATA_LPM_HIPM;

		/* disable DIPM before changing link config */
		if (policy != ATA_LPM_MIN_POWER && dipm) {
			err_mask = ata_dev_set_feature(dev,
					SETFEATURES_SATA_DISABLE, SATA_DIPM);
			if (err_mask && err_mask != AC_ERR_DEV) {
				ata_dev_warn(dev,
					     "failed to disable DIPM, Emask 0x%x\n",
					     err_mask);
				rc = -EIO;
				goto fail;
			}
		}
	}

	if (ap) {
		rc = ap->ops->set_lpm(link, policy, hints);
		if (!rc && ap->slave_link)
			rc = ap->ops->set_lpm(ap->slave_link, policy, hints);
	} else
		rc = sata_pmp_set_lpm(link, policy, hints);

	/*
	 * Attribute link config failure to the first (LPM) enabled
	 * device on the link.
	 */
	if (rc) {
		if (rc == -EOPNOTSUPP) {
			link->flags |= ATA_LFLAG_NO_LPM;
			return 0;
		}
		dev = lpm_dev ? lpm_dev : link_dev;
		goto fail;
	}

	/*
	 * Low level driver acked the transition.  Issue DIPM command
	 * with the new policy set.
	 */
	link->lpm_policy = policy;
	if (ap && ap->slave_link)
		ap->slave_link->lpm_policy = policy;

	/* host config updated, enable DIPM if transitioning to MIN_POWER */
	ata_for_each_dev(dev, link, ENABLED) {
		if (policy == ATA_LPM_MIN_POWER && !no_dipm &&
		    ata_id_has_dipm(dev->id)) {
			err_mask = ata_dev_set_feature(dev,
					SETFEATURES_SATA_ENABLE, SATA_DIPM);
			if (err_mask && err_mask != AC_ERR_DEV) {
				ata_dev_warn(dev,
					"failed to enable DIPM, Emask 0x%x\n",
					err_mask);
				rc = -EIO;
				goto fail;
			}
		}
	}

	link->last_lpm_change = jiffies;
	link->flags |= ATA_LFLAG_CHANGED;

	return 0;

fail:
	/* restore the old policy */
	link->lpm_policy = old_policy;
	if (ap && ap->slave_link)
		ap->slave_link->lpm_policy = old_policy;

	/* if no device or only one more chance is left, disable LPM */
	if (!dev || ehc->tries[dev->devno] <= 2) {
		ata_link_warn(link, "disabling LPM on the link\n");
		link->flags |= ATA_LFLAG_NO_LPM;
	}
	if (r_failed_dev)
		*r_failed_dev = dev;
	return rc;
}

int ata_link_nr_enabled(struct ata_link *link)
{
	struct ata_device *dev;
	int cnt = 0;

	ata_for_each_dev(dev, link, ENABLED)
		cnt++;
	return cnt;
}

static int ata_link_nr_vacant(struct ata_link *link)
{
	struct ata_device *dev;
	int cnt = 0;

	ata_for_each_dev(dev, link, ALL)
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

	/* skip if explicitly requested */
	if (ehc->i.flags & ATA_EHI_NO_RECOVERY)
		return 1;

	/* thaw frozen port and recover failed devices */
	if ((ap->pflags & ATA_PFLAG_FROZEN) || ata_link_nr_enabled(link))
		return 0;

	/* reset at least once if reset is requested */
	if ((ehc->i.action & ATA_EH_RESET) &&
	    !(ehc->i.flags & ATA_EHI_DID_RESET))
		return 0;

	/* skip if class codes for all vacant slots are ATA_DEV_NONE */
	ata_for_each_dev(dev, link, ALL) {
		if (dev->class == ATA_DEV_UNKNOWN &&
		    ehc->classes[dev->devno] != ATA_DEV_NONE)
			return 0;
	}

	return 1;
}

static int ata_count_probe_trials_cb(struct ata_ering_entry *ent, void *void_arg)
{
	u64 interval = msecs_to_jiffies(ATA_EH_PROBE_TRIAL_INTERVAL);
	u64 now = get_jiffies_64();
	int *trials = void_arg;

	if ((ent->eflags & ATA_EFLAG_OLD_ER) ||
	    (ent->timestamp < now - min(now, interval)))
		return -1;

	(*trials)++;
	return 0;
}

static int ata_eh_schedule_probe(struct ata_device *dev)
{
	struct ata_eh_context *ehc = &dev->link->eh_context;
	struct ata_link *link = ata_dev_phys_link(dev);
	int trials = 0;

	if (!(ehc->i.probe_mask & (1 << dev->devno)) ||
	    (ehc->did_probe_mask & (1 << dev->devno)))
		return 0;

	ata_eh_detach_dev(dev);
	ata_dev_init(dev);
	ehc->did_probe_mask |= (1 << dev->devno);
	ehc->i.action |= ATA_EH_RESET;
	ehc->saved_xfer_mode[dev->devno] = 0;
	ehc->saved_ncq_enabled &= ~(1 << dev->devno);

	/* the link maybe in a deep sleep, wake it up */
	if (link->lpm_policy > ATA_LPM_MAX_POWER) {
		if (ata_is_host_link(link))
			link->ap->ops->set_lpm(link, ATA_LPM_MAX_POWER,
					       ATA_LPM_EMPTY);
		else
			sata_pmp_set_lpm(link, ATA_LPM_MAX_POWER,
					 ATA_LPM_EMPTY);
	}

	/* Record and count probe trials on the ering.  The specific
	 * error mask used is irrelevant.  Because a successful device
	 * detection clears the ering, this count accumulates only if
	 * there are consecutive failed probes.
	 *
	 * If the count is equal to or higher than ATA_EH_PROBE_TRIALS
	 * in the last ATA_EH_PROBE_TRIAL_INTERVAL, link speed is
	 * forced to 1.5Gbps.
	 *
	 * This is to work around cases where failed link speed
	 * negotiation results in device misdetection leading to
	 * infinite DEVXCHG or PHRDY CHG events.
	 */
	ata_ering_record(&dev->ering, 0, AC_ERR_OTHER);
	ata_ering_map(&dev->ering, ata_count_probe_trials_cb, &trials);

	if (trials > ATA_EH_PROBE_TRIALS)
		sata_down_spd_limit(link, 1);

	return 1;
}

static int ata_eh_handle_dev_fail(struct ata_device *dev, int err)
{
	struct ata_eh_context *ehc = &dev->link->eh_context;

	/* -EAGAIN from EH routine indicates retry without prejudice.
	 * The requester is responsible for ensuring forward progress.
	 */
	if (err != -EAGAIN)
		ehc->tries[dev->devno]--;

	switch (err) {
	case -ENODEV:
		/* device missing or wrong IDENTIFY data, schedule probing */
		ehc->i.probe_mask |= (1 << dev->devno);
	case -EINVAL:
		/* give it just one more chance */
		ehc->tries[dev->devno] = min(ehc->tries[dev->devno], 1);
	case -EIO:
		if (ehc->tries[dev->devno] == 1) {
			/* This is the last chance, better to slow
			 * down than lose it.
			 */
			sata_down_spd_limit(ata_dev_phys_link(dev), 0);
			if (dev->pio_mode > XFER_PIO_0)
				ata_down_xfermask_limit(dev, ATA_DNXFER_PIO);
		}
	}

	if (ata_dev_enabled(dev) && !ehc->tries[dev->devno]) {
		/* disable device if it has used up all its chances */
		ata_dev_disable(dev);

		/* detach if offline */
		if (ata_phys_link_offline(ata_dev_phys_link(dev)))
			ata_eh_detach_dev(dev);

		/* schedule probe if necessary */
		if (ata_eh_schedule_probe(dev)) {
			ehc->tries[dev->devno] = ATA_EH_DEV_TRIES;
			memset(ehc->cmd_timeout_idx[dev->devno], 0,
			       sizeof(ehc->cmd_timeout_idx[dev->devno]));
		}

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
	int rc, nr_fails;
	unsigned long flags, deadline;

	DPRINTK("ENTER\n");

	/* prep for recovery */
	ata_for_each_link(link, ap, EDGE) {
		struct ata_eh_context *ehc = &link->eh_context;

		/* re-enable link? */
		if (ehc->i.action & ATA_EH_ENABLE_LINK) {
			ata_eh_about_to_do(link, NULL, ATA_EH_ENABLE_LINK);
			spin_lock_irqsave(ap->lock, flags);
			link->flags &= ~ATA_LFLAG_DISABLED;
			spin_unlock_irqrestore(ap->lock, flags);
			ata_eh_done(link, NULL, ATA_EH_ENABLE_LINK);
		}

		ata_for_each_dev(dev, link, ALL) {
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

	/* if UNLOADING, finish immediately */
	if (ap->pflags & ATA_PFLAG_UNLOADING)
		goto out;

	/* prep for EH */
	ata_for_each_link(link, ap, EDGE) {
		struct ata_eh_context *ehc = &link->eh_context;

		/* skip EH if possible. */
		if (ata_eh_skip_recovery(link))
			ehc->i.action = 0;

		ata_for_each_dev(dev, link, ALL)
			ehc->classes[dev->devno] = ATA_DEV_UNKNOWN;
	}

	/* reset */
	ata_for_each_link(link, ap, EDGE) {
		struct ata_eh_context *ehc = &link->eh_context;

		if (!(ehc->i.action & ATA_EH_RESET))
			continue;

		rc = ata_eh_reset(link, ata_link_nr_vacant(link),
				  prereset, softreset, hardreset, postreset);
		if (rc) {
			ata_link_err(link, "reset failed, giving up\n");
			goto out;
		}
	}

	do {
		unsigned long now;

		/*
		 * clears ATA_EH_PARK in eh_info and resets
		 * ap->park_req_pending
		 */
		ata_eh_pull_park_action(ap);

		deadline = jiffies;
		ata_for_each_link(link, ap, EDGE) {
			ata_for_each_dev(dev, link, ALL) {
				struct ata_eh_context *ehc = &link->eh_context;
				unsigned long tmp;

				if (dev->class != ATA_DEV_ATA &&
				    dev->class != ATA_DEV_ZAC)
					continue;
				if (!(ehc->i.dev_action[dev->devno] &
				      ATA_EH_PARK))
					continue;
				tmp = dev->unpark_deadline;
				if (time_before(deadline, tmp))
					deadline = tmp;
				else if (time_before_eq(tmp, jiffies))
					continue;
				if (ehc->unloaded_mask & (1 << dev->devno))
					continue;

				ata_eh_park_issue_cmd(dev, 1);
			}
		}

		now = jiffies;
		if (time_before_eq(deadline, now))
			break;

		ata_eh_release(ap);
		deadline = wait_for_completion_timeout(&ap->park_req_pending,
						       deadline - now);
		ata_eh_acquire(ap);
	} while (deadline);
	ata_for_each_link(link, ap, EDGE) {
		ata_for_each_dev(dev, link, ALL) {
			if (!(link->eh_context.unloaded_mask &
			      (1 << dev->devno)))
				continue;

			ata_eh_park_issue_cmd(dev, 0);
			ata_eh_done(link, dev, ATA_EH_PARK);
		}
	}

	/* the rest */
	nr_fails = 0;
	ata_for_each_link(link, ap, PMP_FIRST) {
		struct ata_eh_context *ehc = &link->eh_context;

		if (sata_pmp_attached(ap) && ata_is_host_link(link))
			goto config_lpm;

		/* revalidate existing devices and attach new ones */
		rc = ata_eh_revalidate_and_attach(link, &dev);
		if (rc)
			goto rest_fail;

		/* if PMP got attached, return, pmp EH will take care of it */
		if (link->device->class == ATA_DEV_PMP) {
			ehc->i.action = 0;
			return 0;
		}

		/* configure transfer mode if necessary */
		if (ehc->i.flags & ATA_EHI_SETMODE) {
			rc = ata_set_mode(link, &dev);
			if (rc)
				goto rest_fail;
			ehc->i.flags &= ~ATA_EHI_SETMODE;
		}

		/* If reset has been issued, clear UA to avoid
		 * disrupting the current users of the device.
		 */
		if (ehc->i.flags & ATA_EHI_DID_RESET) {
			ata_for_each_dev(dev, link, ALL) {
				if (dev->class != ATA_DEV_ATAPI)
					continue;
				rc = atapi_eh_clear_ua(dev);
				if (rc)
					goto rest_fail;
				if (zpodd_dev_enabled(dev))
					zpodd_post_poweron(dev);
			}
		}

		/* retry flush if necessary */
		ata_for_each_dev(dev, link, ALL) {
			if (dev->class != ATA_DEV_ATA &&
			    dev->class != ATA_DEV_ZAC)
				continue;
			rc = ata_eh_maybe_retry_flush(dev);
			if (rc)
				goto rest_fail;
		}

	config_lpm:
		/* configure link power saving */
		if (link->lpm_policy != ap->target_lpm_policy) {
			rc = ata_eh_set_lpm(link, ap->target_lpm_policy, &dev);
			if (rc)
				goto rest_fail;
		}

		/* this link is okay now */
		ehc->i.flags = 0;
		continue;

	rest_fail:
		nr_fails++;
		if (dev)
			ata_eh_handle_dev_fail(dev, rc);

		if (ap->pflags & ATA_PFLAG_FROZEN) {
			/* PMP reset requires working host port.
			 * Can't retry if it's frozen.
			 */
			if (sata_pmp_attached(ap))
				goto out;
			break;
		}
	}

	if (nr_fails)
		goto retry;

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
		ata_for_each_dev(dev, &ap->link, ALL)
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
	if (hardreset == sata_std_hardreset && !sata_scr_valid(&ap->link))
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
	struct ata_device *dev;

	/* are we suspending? */
	spin_lock_irqsave(ap->lock, flags);
	if (!(ap->pflags & ATA_PFLAG_PM_PENDING) ||
	    ap->pm_mesg.event & PM_EVENT_RESUME) {
		spin_unlock_irqrestore(ap->lock, flags);
		return;
	}
	spin_unlock_irqrestore(ap->lock, flags);

	WARN_ON(ap->pflags & ATA_PFLAG_SUSPENDED);

	/*
	 * If we have a ZPODD attached, check its zero
	 * power ready status before the port is frozen.
	 * Only needed for runtime suspend.
	 */
	if (PMSG_IS_AUTO(ap->pm_mesg)) {
		ata_for_each_dev(dev, &ap->link, ENABLED) {
			if (zpodd_dev_enabled(dev))
				zpodd_on_suspend(dev);
		}
	}

	/* tell ACPI we're suspending */
	rc = ata_acpi_on_suspend(ap);
	if (rc)
		goto out;

	/* suspend */
	ata_eh_freeze_port(ap);

	if (ap->ops->port_suspend)
		rc = ap->ops->port_suspend(ap, ap->pm_mesg);

	ata_acpi_set_state(ap, ap->pm_mesg);
 out:
	/* update the flags */
	spin_lock_irqsave(ap->lock, flags);

	ap->pflags &= ~ATA_PFLAG_PM_PENDING;
	if (rc == 0)
		ap->pflags |= ATA_PFLAG_SUSPENDED;
	else if (ap->pflags & ATA_PFLAG_FROZEN)
		ata_port_schedule_eh(ap);

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
	struct ata_link *link;
	struct ata_device *dev;
	unsigned long flags;
	int rc = 0;

	/* are we resuming? */
	spin_lock_irqsave(ap->lock, flags);
	if (!(ap->pflags & ATA_PFLAG_PM_PENDING) ||
	    !(ap->pm_mesg.event & PM_EVENT_RESUME)) {
		spin_unlock_irqrestore(ap->lock, flags);
		return;
	}
	spin_unlock_irqrestore(ap->lock, flags);

	WARN_ON(!(ap->pflags & ATA_PFLAG_SUSPENDED));

	/*
	 * Error timestamps are in jiffies which doesn't run while
	 * suspended and PHY events during resume isn't too uncommon.
	 * When the two are combined, it can lead to unnecessary speed
	 * downs if the machine is suspended and resumed repeatedly.
	 * Clear error history.
	 */
	ata_for_each_link(link, ap, HOST_FIRST)
		ata_for_each_dev(dev, link, ALL)
			ata_ering_clear(&dev->ering);

	ata_acpi_set_state(ap, ap->pm_mesg);

	if (ap->ops->port_resume)
		rc = ap->ops->port_resume(ap);

	/* tell ACPI that we're resuming */
	ata_acpi_on_resume(ap);

	/* update the flags */
	spin_lock_irqsave(ap->lock, flags);
	ap->pflags &= ~(ATA_PFLAG_PM_PENDING | ATA_PFLAG_SUSPENDED);
	spin_unlock_irqrestore(ap->lock, flags);
}
#endif /* CONFIG_PM */
