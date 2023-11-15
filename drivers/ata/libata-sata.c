// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  SATA specific part of ATA helper library
 *
 *  Copyright 2003-2004 Red Hat, Inc.  All rights reserved.
 *  Copyright 2003-2004 Jeff Garzik
 *  Copyright 2006 Tejun Heo <htejun@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <linux/libata.h>
#include <asm/unaligned.h>

#include "libata.h"
#include "libata-transport.h"

/* debounce timing parameters in msecs { interval, duration, timeout } */
const unsigned int sata_deb_timing_normal[]		= {   5,  100, 2000 };
EXPORT_SYMBOL_GPL(sata_deb_timing_normal);
const unsigned int sata_deb_timing_hotplug[]		= {  25,  500, 2000 };
EXPORT_SYMBOL_GPL(sata_deb_timing_hotplug);
const unsigned int sata_deb_timing_long[]		= { 100, 2000, 5000 };
EXPORT_SYMBOL_GPL(sata_deb_timing_long);

/**
 *	sata_scr_valid - test whether SCRs are accessible
 *	@link: ATA link to test SCR accessibility for
 *
 *	Test whether SCRs are accessible for @link.
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	1 if SCRs are accessible, 0 otherwise.
 */
int sata_scr_valid(struct ata_link *link)
{
	struct ata_port *ap = link->ap;

	return (ap->flags & ATA_FLAG_SATA) && ap->ops->scr_read;
}
EXPORT_SYMBOL_GPL(sata_scr_valid);

/**
 *	sata_scr_read - read SCR register of the specified port
 *	@link: ATA link to read SCR for
 *	@reg: SCR to read
 *	@val: Place to store read value
 *
 *	Read SCR register @reg of @link into *@val.  This function is
 *	guaranteed to succeed if @link is ap->link, the cable type of
 *	the port is SATA and the port implements ->scr_read.
 *
 *	LOCKING:
 *	None if @link is ap->link.  Kernel thread context otherwise.
 *
 *	RETURNS:
 *	0 on success, negative errno on failure.
 */
int sata_scr_read(struct ata_link *link, int reg, u32 *val)
{
	if (ata_is_host_link(link)) {
		if (sata_scr_valid(link))
			return link->ap->ops->scr_read(link, reg, val);
		return -EOPNOTSUPP;
	}

	return sata_pmp_scr_read(link, reg, val);
}
EXPORT_SYMBOL_GPL(sata_scr_read);

/**
 *	sata_scr_write - write SCR register of the specified port
 *	@link: ATA link to write SCR for
 *	@reg: SCR to write
 *	@val: value to write
 *
 *	Write @val to SCR register @reg of @link.  This function is
 *	guaranteed to succeed if @link is ap->link, the cable type of
 *	the port is SATA and the port implements ->scr_read.
 *
 *	LOCKING:
 *	None if @link is ap->link.  Kernel thread context otherwise.
 *
 *	RETURNS:
 *	0 on success, negative errno on failure.
 */
int sata_scr_write(struct ata_link *link, int reg, u32 val)
{
	if (ata_is_host_link(link)) {
		if (sata_scr_valid(link))
			return link->ap->ops->scr_write(link, reg, val);
		return -EOPNOTSUPP;
	}

	return sata_pmp_scr_write(link, reg, val);
}
EXPORT_SYMBOL_GPL(sata_scr_write);

/**
 *	sata_scr_write_flush - write SCR register of the specified port and flush
 *	@link: ATA link to write SCR for
 *	@reg: SCR to write
 *	@val: value to write
 *
 *	This function is identical to sata_scr_write() except that this
 *	function performs flush after writing to the register.
 *
 *	LOCKING:
 *	None if @link is ap->link.  Kernel thread context otherwise.
 *
 *	RETURNS:
 *	0 on success, negative errno on failure.
 */
int sata_scr_write_flush(struct ata_link *link, int reg, u32 val)
{
	if (ata_is_host_link(link)) {
		int rc;

		if (sata_scr_valid(link)) {
			rc = link->ap->ops->scr_write(link, reg, val);
			if (rc == 0)
				rc = link->ap->ops->scr_read(link, reg, &val);
			return rc;
		}
		return -EOPNOTSUPP;
	}

	return sata_pmp_scr_write(link, reg, val);
}
EXPORT_SYMBOL_GPL(sata_scr_write_flush);

/**
 *	ata_tf_to_fis - Convert ATA taskfile to SATA FIS structure
 *	@tf: Taskfile to convert
 *	@pmp: Port multiplier port
 *	@is_cmd: This FIS is for command
 *	@fis: Buffer into which data will output
 *
 *	Converts a standard ATA taskfile to a Serial ATA
 *	FIS structure (Register - Host to Device).
 *
 *	LOCKING:
 *	Inherited from caller.
 */
void ata_tf_to_fis(const struct ata_taskfile *tf, u8 pmp, int is_cmd, u8 *fis)
{
	fis[0] = 0x27;			/* Register - Host to Device FIS */
	fis[1] = pmp & 0xf;		/* Port multiplier number*/
	if (is_cmd)
		fis[1] |= (1 << 7);	/* bit 7 indicates Command FIS */

	fis[2] = tf->command;
	fis[3] = tf->feature;

	fis[4] = tf->lbal;
	fis[5] = tf->lbam;
	fis[6] = tf->lbah;
	fis[7] = tf->device;

	fis[8] = tf->hob_lbal;
	fis[9] = tf->hob_lbam;
	fis[10] = tf->hob_lbah;
	fis[11] = tf->hob_feature;

	fis[12] = tf->nsect;
	fis[13] = tf->hob_nsect;
	fis[14] = 0;
	fis[15] = tf->ctl;

	fis[16] = tf->auxiliary & 0xff;
	fis[17] = (tf->auxiliary >> 8) & 0xff;
	fis[18] = (tf->auxiliary >> 16) & 0xff;
	fis[19] = (tf->auxiliary >> 24) & 0xff;
}
EXPORT_SYMBOL_GPL(ata_tf_to_fis);

/**
 *	ata_tf_from_fis - Convert SATA FIS to ATA taskfile
 *	@fis: Buffer from which data will be input
 *	@tf: Taskfile to output
 *
 *	Converts a serial ATA FIS structure to a standard ATA taskfile.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

void ata_tf_from_fis(const u8 *fis, struct ata_taskfile *tf)
{
	tf->status	= fis[2];
	tf->error	= fis[3];

	tf->lbal	= fis[4];
	tf->lbam	= fis[5];
	tf->lbah	= fis[6];
	tf->device	= fis[7];

	tf->hob_lbal	= fis[8];
	tf->hob_lbam	= fis[9];
	tf->hob_lbah	= fis[10];

	tf->nsect	= fis[12];
	tf->hob_nsect	= fis[13];
}
EXPORT_SYMBOL_GPL(ata_tf_from_fis);

/**
 *	sata_link_debounce - debounce SATA phy status
 *	@link: ATA link to debounce SATA phy status for
 *	@params: timing parameters { interval, duration, timeout } in msec
 *	@deadline: deadline jiffies for the operation
 *
 *	Make sure SStatus of @link reaches stable state, determined by
 *	holding the same value where DET is not 1 for @duration polled
 *	every @interval, before @timeout.  Timeout constraints the
 *	beginning of the stable state.  Because DET gets stuck at 1 on
 *	some controllers after hot unplugging, this functions waits
 *	until timeout then returns 0 if DET is stable at 1.
 *
 *	@timeout is further limited by @deadline.  The sooner of the
 *	two is used.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
int sata_link_debounce(struct ata_link *link, const unsigned int *params,
		       unsigned long deadline)
{
	unsigned int interval = params[0];
	unsigned int duration = params[1];
	unsigned long last_jiffies, t;
	u32 last, cur;
	int rc;

	t = ata_deadline(jiffies, params[2]);
	if (time_before(t, deadline))
		deadline = t;

	if ((rc = sata_scr_read(link, SCR_STATUS, &cur)))
		return rc;
	cur &= 0xf;

	last = cur;
	last_jiffies = jiffies;

	while (1) {
		ata_msleep(link->ap, interval);
		if ((rc = sata_scr_read(link, SCR_STATUS, &cur)))
			return rc;
		cur &= 0xf;

		/* DET stable? */
		if (cur == last) {
			if (cur == 1 && time_before(jiffies, deadline))
				continue;
			if (time_after(jiffies,
				       ata_deadline(last_jiffies, duration)))
				return 0;
			continue;
		}

		/* unstable, start over */
		last = cur;
		last_jiffies = jiffies;

		/* Check deadline.  If debouncing failed, return
		 * -EPIPE to tell upper layer to lower link speed.
		 */
		if (time_after(jiffies, deadline))
			return -EPIPE;
	}
}
EXPORT_SYMBOL_GPL(sata_link_debounce);

/**
 *	sata_link_resume - resume SATA link
 *	@link: ATA link to resume SATA
 *	@params: timing parameters { interval, duration, timeout } in msec
 *	@deadline: deadline jiffies for the operation
 *
 *	Resume SATA phy @link and debounce it.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
int sata_link_resume(struct ata_link *link, const unsigned int *params,
		     unsigned long deadline)
{
	int tries = ATA_LINK_RESUME_TRIES;
	u32 scontrol, serror;
	int rc;

	if ((rc = sata_scr_read(link, SCR_CONTROL, &scontrol)))
		return rc;

	/*
	 * Writes to SControl sometimes get ignored under certain
	 * controllers (ata_piix SIDPR).  Make sure DET actually is
	 * cleared.
	 */
	do {
		scontrol = (scontrol & 0x0f0) | 0x300;
		if ((rc = sata_scr_write(link, SCR_CONTROL, scontrol)))
			return rc;
		/*
		 * Some PHYs react badly if SStatus is pounded
		 * immediately after resuming.  Delay 200ms before
		 * debouncing.
		 */
		if (!(link->flags & ATA_LFLAG_NO_DEBOUNCE_DELAY))
			ata_msleep(link->ap, 200);

		/* is SControl restored correctly? */
		if ((rc = sata_scr_read(link, SCR_CONTROL, &scontrol)))
			return rc;
	} while ((scontrol & 0xf0f) != 0x300 && --tries);

	if ((scontrol & 0xf0f) != 0x300) {
		ata_link_warn(link, "failed to resume link (SControl %X)\n",
			     scontrol);
		return 0;
	}

	if (tries < ATA_LINK_RESUME_TRIES)
		ata_link_warn(link, "link resume succeeded after %d retries\n",
			      ATA_LINK_RESUME_TRIES - tries);

	if ((rc = sata_link_debounce(link, params, deadline)))
		return rc;

	/* clear SError, some PHYs require this even for SRST to work */
	if (!(rc = sata_scr_read(link, SCR_ERROR, &serror)))
		rc = sata_scr_write(link, SCR_ERROR, serror);

	return rc != -EINVAL ? rc : 0;
}
EXPORT_SYMBOL_GPL(sata_link_resume);

/**
 *	sata_link_scr_lpm - manipulate SControl IPM and SPM fields
 *	@link: ATA link to manipulate SControl for
 *	@policy: LPM policy to configure
 *	@spm_wakeup: initiate LPM transition to active state
 *
 *	Manipulate the IPM field of the SControl register of @link
 *	according to @policy.  If @policy is ATA_LPM_MAX_POWER and
 *	@spm_wakeup is %true, the SPM field is manipulated to wake up
 *	the link.  This function also clears PHYRDY_CHG before
 *	returning.
 *
 *	LOCKING:
 *	EH context.
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int sata_link_scr_lpm(struct ata_link *link, enum ata_lpm_policy policy,
		      bool spm_wakeup)
{
	struct ata_eh_context *ehc = &link->eh_context;
	bool woken_up = false;
	u32 scontrol;
	int rc;

	rc = sata_scr_read(link, SCR_CONTROL, &scontrol);
	if (rc)
		return rc;

	switch (policy) {
	case ATA_LPM_MAX_POWER:
		/* disable all LPM transitions */
		scontrol |= (0x7 << 8);
		/* initiate transition to active state */
		if (spm_wakeup) {
			scontrol |= (0x4 << 12);
			woken_up = true;
		}
		break;
	case ATA_LPM_MED_POWER:
		/* allow LPM to PARTIAL */
		scontrol &= ~(0x1 << 8);
		scontrol |= (0x6 << 8);
		break;
	case ATA_LPM_MED_POWER_WITH_DIPM:
	case ATA_LPM_MIN_POWER_WITH_PARTIAL:
	case ATA_LPM_MIN_POWER:
		if (ata_link_nr_enabled(link) > 0) {
			/* assume no restrictions on LPM transitions */
			scontrol &= ~(0x7 << 8);

			/*
			 * If the controller does not support partial, slumber,
			 * or devsleep, then disallow these transitions.
			 */
			if (link->ap->host->flags & ATA_HOST_NO_PART)
				scontrol |= (0x1 << 8);

			if (link->ap->host->flags & ATA_HOST_NO_SSC)
				scontrol |= (0x2 << 8);

			if (link->ap->host->flags & ATA_HOST_NO_DEVSLP)
				scontrol |= (0x4 << 8);
		} else {
			/* empty port, power off */
			scontrol &= ~0xf;
			scontrol |= (0x1 << 2);
		}
		break;
	default:
		WARN_ON(1);
	}

	rc = sata_scr_write(link, SCR_CONTROL, scontrol);
	if (rc)
		return rc;

	/* give the link time to transit out of LPM state */
	if (woken_up)
		msleep(10);

	/* clear PHYRDY_CHG from SError */
	ehc->i.serror &= ~SERR_PHYRDY_CHG;
	return sata_scr_write(link, SCR_ERROR, SERR_PHYRDY_CHG);
}
EXPORT_SYMBOL_GPL(sata_link_scr_lpm);

static int __sata_set_spd_needed(struct ata_link *link, u32 *scontrol)
{
	struct ata_link *host_link = &link->ap->link;
	u32 limit, target, spd;

	limit = link->sata_spd_limit;

	/* Don't configure downstream link faster than upstream link.
	 * It doesn't speed up anything and some PMPs choke on such
	 * configuration.
	 */
	if (!ata_is_host_link(link) && host_link->sata_spd)
		limit &= (1 << host_link->sata_spd) - 1;

	if (limit == UINT_MAX)
		target = 0;
	else
		target = fls(limit);

	spd = (*scontrol >> 4) & 0xf;
	*scontrol = (*scontrol & ~0xf0) | ((target & 0xf) << 4);

	return spd != target;
}

/**
 *	sata_set_spd_needed - is SATA spd configuration needed
 *	@link: Link in question
 *
 *	Test whether the spd limit in SControl matches
 *	@link->sata_spd_limit.  This function is used to determine
 *	whether hardreset is necessary to apply SATA spd
 *	configuration.
 *
 *	LOCKING:
 *	Inherited from caller.
 *
 *	RETURNS:
 *	1 if SATA spd configuration is needed, 0 otherwise.
 */
static int sata_set_spd_needed(struct ata_link *link)
{
	u32 scontrol;

	if (sata_scr_read(link, SCR_CONTROL, &scontrol))
		return 1;

	return __sata_set_spd_needed(link, &scontrol);
}

/**
 *	sata_set_spd - set SATA spd according to spd limit
 *	@link: Link to set SATA spd for
 *
 *	Set SATA spd of @link according to sata_spd_limit.
 *
 *	LOCKING:
 *	Inherited from caller.
 *
 *	RETURNS:
 *	0 if spd doesn't need to be changed, 1 if spd has been
 *	changed.  Negative errno if SCR registers are inaccessible.
 */
int sata_set_spd(struct ata_link *link)
{
	u32 scontrol;
	int rc;

	if ((rc = sata_scr_read(link, SCR_CONTROL, &scontrol)))
		return rc;

	if (!__sata_set_spd_needed(link, &scontrol))
		return 0;

	if ((rc = sata_scr_write(link, SCR_CONTROL, scontrol)))
		return rc;

	return 1;
}
EXPORT_SYMBOL_GPL(sata_set_spd);

/**
 *	sata_link_hardreset - reset link via SATA phy reset
 *	@link: link to reset
 *	@timing: timing parameters { interval, duration, timeout } in msec
 *	@deadline: deadline jiffies for the operation
 *	@online: optional out parameter indicating link onlineness
 *	@check_ready: optional callback to check link readiness
 *
 *	SATA phy-reset @link using DET bits of SControl register.
 *	After hardreset, link readiness is waited upon using
 *	ata_wait_ready() if @check_ready is specified.  LLDs are
 *	allowed to not specify @check_ready and wait itself after this
 *	function returns.  Device classification is LLD's
 *	responsibility.
 *
 *	*@online is set to one iff reset succeeded and @link is online
 *	after reset.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int sata_link_hardreset(struct ata_link *link, const unsigned int *timing,
			unsigned long deadline,
			bool *online, int (*check_ready)(struct ata_link *))
{
	u32 scontrol;
	int rc;

	if (online)
		*online = false;

	if (sata_set_spd_needed(link)) {
		/* SATA spec says nothing about how to reconfigure
		 * spd.  To be on the safe side, turn off phy during
		 * reconfiguration.  This works for at least ICH7 AHCI
		 * and Sil3124.
		 */
		if ((rc = sata_scr_read(link, SCR_CONTROL, &scontrol)))
			goto out;

		scontrol = (scontrol & 0x0f0) | 0x304;

		if ((rc = sata_scr_write(link, SCR_CONTROL, scontrol)))
			goto out;

		sata_set_spd(link);
	}

	/* issue phy wake/reset */
	if ((rc = sata_scr_read(link, SCR_CONTROL, &scontrol)))
		goto out;

	scontrol = (scontrol & 0x0f0) | 0x301;

	if ((rc = sata_scr_write_flush(link, SCR_CONTROL, scontrol)))
		goto out;

	/* Couldn't find anything in SATA I/II specs, but AHCI-1.1
	 * 10.4.2 says at least 1 ms.
	 */
	ata_msleep(link->ap, 1);

	/* bring link back */
	rc = sata_link_resume(link, timing, deadline);
	if (rc)
		goto out;
	/* if link is offline nothing more to do */
	if (ata_phys_link_offline(link))
		goto out;

	/* Link is online.  From this point, -ENODEV too is an error. */
	if (online)
		*online = true;

	if (sata_pmp_supported(link->ap) && ata_is_host_link(link)) {
		/* If PMP is supported, we have to do follow-up SRST.
		 * Some PMPs don't send D2H Reg FIS after hardreset if
		 * the first port is empty.  Wait only for
		 * ATA_TMOUT_PMP_SRST_WAIT.
		 */
		if (check_ready) {
			unsigned long pmp_deadline;

			pmp_deadline = ata_deadline(jiffies,
						    ATA_TMOUT_PMP_SRST_WAIT);
			if (time_after(pmp_deadline, deadline))
				pmp_deadline = deadline;
			ata_wait_ready(link, pmp_deadline, check_ready);
		}
		rc = -EAGAIN;
		goto out;
	}

	rc = 0;
	if (check_ready)
		rc = ata_wait_ready(link, deadline, check_ready);
 out:
	if (rc && rc != -EAGAIN) {
		/* online is set iff link is online && reset succeeded */
		if (online)
			*online = false;
	}
	return rc;
}
EXPORT_SYMBOL_GPL(sata_link_hardreset);

/**
 *	ata_qc_complete_multiple - Complete multiple qcs successfully
 *	@ap: port in question
 *	@qc_active: new qc_active mask
 *
 *	Complete in-flight commands.  This functions is meant to be
 *	called from low-level driver's interrupt routine to complete
 *	requests normally.  ap->qc_active and @qc_active is compared
 *	and commands are completed accordingly.
 *
 *	Always use this function when completing multiple NCQ commands
 *	from IRQ handlers instead of calling ata_qc_complete()
 *	multiple times to keep IRQ expect status properly in sync.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	Number of completed commands on success, -errno otherwise.
 */
int ata_qc_complete_multiple(struct ata_port *ap, u64 qc_active)
{
	u64 done_mask, ap_qc_active = ap->qc_active;
	int nr_done = 0;

	/*
	 * If the internal tag is set on ap->qc_active, then we care about
	 * bit0 on the passed in qc_active mask. Move that bit up to match
	 * the internal tag.
	 */
	if (ap_qc_active & (1ULL << ATA_TAG_INTERNAL)) {
		qc_active |= (qc_active & 0x01) << ATA_TAG_INTERNAL;
		qc_active ^= qc_active & 0x01;
	}

	done_mask = ap_qc_active ^ qc_active;

	if (unlikely(done_mask & qc_active)) {
		ata_port_err(ap, "illegal qc_active transition (%08llx->%08llx)\n",
			     ap->qc_active, qc_active);
		return -EINVAL;
	}

	if (ap->ops->qc_ncq_fill_rtf)
		ap->ops->qc_ncq_fill_rtf(ap, done_mask);

	while (done_mask) {
		struct ata_queued_cmd *qc;
		unsigned int tag = __ffs64(done_mask);

		qc = ata_qc_from_tag(ap, tag);
		if (qc) {
			ata_qc_complete(qc);
			nr_done++;
		}
		done_mask &= ~(1ULL << tag);
	}

	return nr_done;
}
EXPORT_SYMBOL_GPL(ata_qc_complete_multiple);

/**
 *	ata_slave_link_init - initialize slave link
 *	@ap: port to initialize slave link for
 *
 *	Create and initialize slave link for @ap.  This enables slave
 *	link handling on the port.
 *
 *	In libata, a port contains links and a link contains devices.
 *	There is single host link but if a PMP is attached to it,
 *	there can be multiple fan-out links.  On SATA, there's usually
 *	a single device connected to a link but PATA and SATA
 *	controllers emulating TF based interface can have two - master
 *	and slave.
 *
 *	However, there are a few controllers which don't fit into this
 *	abstraction too well - SATA controllers which emulate TF
 *	interface with both master and slave devices but also have
 *	separate SCR register sets for each device.  These controllers
 *	need separate links for physical link handling
 *	(e.g. onlineness, link speed) but should be treated like a
 *	traditional M/S controller for everything else (e.g. command
 *	issue, softreset).
 *
 *	slave_link is libata's way of handling this class of
 *	controllers without impacting core layer too much.  For
 *	anything other than physical link handling, the default host
 *	link is used for both master and slave.  For physical link
 *	handling, separate @ap->slave_link is used.  All dirty details
 *	are implemented inside libata core layer.  From LLD's POV, the
 *	only difference is that prereset, hardreset and postreset are
 *	called once more for the slave link, so the reset sequence
 *	looks like the following.
 *
 *	prereset(M) -> prereset(S) -> hardreset(M) -> hardreset(S) ->
 *	softreset(M) -> postreset(M) -> postreset(S)
 *
 *	Note that softreset is called only for the master.  Softreset
 *	resets both M/S by definition, so SRST on master should handle
 *	both (the standard method will work just fine).
 *
 *	LOCKING:
 *	Should be called before host is registered.
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
int ata_slave_link_init(struct ata_port *ap)
{
	struct ata_link *link;

	WARN_ON(ap->slave_link);
	WARN_ON(ap->flags & ATA_FLAG_PMP);

	link = kzalloc(sizeof(*link), GFP_KERNEL);
	if (!link)
		return -ENOMEM;

	ata_link_init(ap, link, 1);
	ap->slave_link = link;
	return 0;
}
EXPORT_SYMBOL_GPL(ata_slave_link_init);

/**
 *	sata_lpm_ignore_phy_events - test if PHY event should be ignored
 *	@link: Link receiving the event
 *
 *	Test whether the received PHY event has to be ignored or not.
 *
 *	LOCKING:
 *	None:
 *
 *	RETURNS:
 *	True if the event has to be ignored.
 */
bool sata_lpm_ignore_phy_events(struct ata_link *link)
{
	unsigned long lpm_timeout = link->last_lpm_change +
				    msecs_to_jiffies(ATA_TMOUT_SPURIOUS_PHY);

	/* if LPM is enabled, PHYRDY doesn't mean anything */
	if (link->lpm_policy > ATA_LPM_MAX_POWER)
		return true;

	/* ignore the first PHY event after the LPM policy changed
	 * as it is might be spurious
	 */
	if ((link->flags & ATA_LFLAG_CHANGED) &&
	    time_before(jiffies, lpm_timeout))
		return true;

	return false;
}
EXPORT_SYMBOL_GPL(sata_lpm_ignore_phy_events);

static const char *ata_lpm_policy_names[] = {
	[ATA_LPM_UNKNOWN]		= "max_performance",
	[ATA_LPM_MAX_POWER]		= "max_performance",
	[ATA_LPM_MED_POWER]		= "medium_power",
	[ATA_LPM_MED_POWER_WITH_DIPM]	= "med_power_with_dipm",
	[ATA_LPM_MIN_POWER_WITH_PARTIAL] = "min_power_with_partial",
	[ATA_LPM_MIN_POWER]		= "min_power",
};

static ssize_t ata_scsi_lpm_store(struct device *device,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(device);
	struct ata_port *ap = ata_shost_to_port(shost);
	struct ata_link *link;
	struct ata_device *dev;
	enum ata_lpm_policy policy;
	unsigned long flags;

	/* UNKNOWN is internal state, iterate from MAX_POWER */
	for (policy = ATA_LPM_MAX_POWER;
	     policy < ARRAY_SIZE(ata_lpm_policy_names); policy++) {
		const char *name = ata_lpm_policy_names[policy];

		if (strncmp(name, buf, strlen(name)) == 0)
			break;
	}
	if (policy == ARRAY_SIZE(ata_lpm_policy_names))
		return -EINVAL;

	spin_lock_irqsave(ap->lock, flags);

	ata_for_each_link(link, ap, EDGE) {
		ata_for_each_dev(dev, &ap->link, ENABLED) {
			if (dev->horkage & ATA_HORKAGE_NOLPM) {
				count = -EOPNOTSUPP;
				goto out_unlock;
			}
		}
	}

	ap->target_lpm_policy = policy;
	ata_port_schedule_eh(ap);
out_unlock:
	spin_unlock_irqrestore(ap->lock, flags);
	return count;
}

static ssize_t ata_scsi_lpm_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ata_port *ap = ata_shost_to_port(shost);

	if (ap->target_lpm_policy >= ARRAY_SIZE(ata_lpm_policy_names))
		return -EINVAL;

	return sysfs_emit(buf, "%s\n",
			ata_lpm_policy_names[ap->target_lpm_policy]);
}
DEVICE_ATTR(link_power_management_policy, S_IRUGO | S_IWUSR,
	    ata_scsi_lpm_show, ata_scsi_lpm_store);
EXPORT_SYMBOL_GPL(dev_attr_link_power_management_policy);

static ssize_t ata_ncq_prio_supported_show(struct device *device,
					   struct device_attribute *attr,
					   char *buf)
{
	struct scsi_device *sdev = to_scsi_device(device);
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	struct ata_device *dev;
	bool ncq_prio_supported;
	int rc = 0;

	spin_lock_irq(ap->lock);
	dev = ata_scsi_find_dev(ap, sdev);
	if (!dev)
		rc = -ENODEV;
	else
		ncq_prio_supported = dev->flags & ATA_DFLAG_NCQ_PRIO;
	spin_unlock_irq(ap->lock);

	return rc ? rc : sysfs_emit(buf, "%u\n", ncq_prio_supported);
}

DEVICE_ATTR(ncq_prio_supported, S_IRUGO, ata_ncq_prio_supported_show, NULL);
EXPORT_SYMBOL_GPL(dev_attr_ncq_prio_supported);

static ssize_t ata_ncq_prio_enable_show(struct device *device,
					struct device_attribute *attr,
					char *buf)
{
	struct scsi_device *sdev = to_scsi_device(device);
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	struct ata_device *dev;
	bool ncq_prio_enable;
	int rc = 0;

	spin_lock_irq(ap->lock);
	dev = ata_scsi_find_dev(ap, sdev);
	if (!dev)
		rc = -ENODEV;
	else
		ncq_prio_enable = dev->flags & ATA_DFLAG_NCQ_PRIO_ENABLED;
	spin_unlock_irq(ap->lock);

	return rc ? rc : sysfs_emit(buf, "%u\n", ncq_prio_enable);
}

static ssize_t ata_ncq_prio_enable_store(struct device *device,
					 struct device_attribute *attr,
					 const char *buf, size_t len)
{
	struct scsi_device *sdev = to_scsi_device(device);
	struct ata_port *ap;
	struct ata_device *dev;
	long int input;
	int rc = 0;

	rc = kstrtol(buf, 10, &input);
	if (rc)
		return rc;
	if ((input < 0) || (input > 1))
		return -EINVAL;

	ap = ata_shost_to_port(sdev->host);
	dev = ata_scsi_find_dev(ap, sdev);
	if (unlikely(!dev))
		return  -ENODEV;

	spin_lock_irq(ap->lock);

	if (!(dev->flags & ATA_DFLAG_NCQ_PRIO)) {
		rc = -EINVAL;
		goto unlock;
	}

	if (input) {
		if (dev->flags & ATA_DFLAG_CDL_ENABLED) {
			ata_dev_err(dev,
				"CDL must be disabled to enable NCQ priority\n");
			rc = -EINVAL;
			goto unlock;
		}
		dev->flags |= ATA_DFLAG_NCQ_PRIO_ENABLED;
	} else {
		dev->flags &= ~ATA_DFLAG_NCQ_PRIO_ENABLED;
	}

unlock:
	spin_unlock_irq(ap->lock);

	return rc ? rc : len;
}

DEVICE_ATTR(ncq_prio_enable, S_IRUGO | S_IWUSR,
	    ata_ncq_prio_enable_show, ata_ncq_prio_enable_store);
EXPORT_SYMBOL_GPL(dev_attr_ncq_prio_enable);

static struct attribute *ata_ncq_sdev_attrs[] = {
	&dev_attr_unload_heads.attr,
	&dev_attr_ncq_prio_enable.attr,
	&dev_attr_ncq_prio_supported.attr,
	NULL
};

static const struct attribute_group ata_ncq_sdev_attr_group = {
	.attrs = ata_ncq_sdev_attrs
};

const struct attribute_group *ata_ncq_sdev_groups[] = {
	&ata_ncq_sdev_attr_group,
	NULL
};
EXPORT_SYMBOL_GPL(ata_ncq_sdev_groups);

static ssize_t
ata_scsi_em_message_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ata_port *ap = ata_shost_to_port(shost);
	if (ap->ops->em_store && (ap->flags & ATA_FLAG_EM))
		return ap->ops->em_store(ap, buf, count);
	return -EINVAL;
}

static ssize_t
ata_scsi_em_message_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ata_port *ap = ata_shost_to_port(shost);

	if (ap->ops->em_show && (ap->flags & ATA_FLAG_EM))
		return ap->ops->em_show(ap, buf);
	return -EINVAL;
}
DEVICE_ATTR(em_message, S_IRUGO | S_IWUSR,
		ata_scsi_em_message_show, ata_scsi_em_message_store);
EXPORT_SYMBOL_GPL(dev_attr_em_message);

static ssize_t
ata_scsi_em_message_type_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ata_port *ap = ata_shost_to_port(shost);

	return sysfs_emit(buf, "%d\n", ap->em_message_type);
}
DEVICE_ATTR(em_message_type, S_IRUGO,
		  ata_scsi_em_message_type_show, NULL);
EXPORT_SYMBOL_GPL(dev_attr_em_message_type);

static ssize_t
ata_scsi_activity_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	struct ata_device *atadev = ata_scsi_find_dev(ap, sdev);

	if (atadev && ap->ops->sw_activity_show &&
	    (ap->flags & ATA_FLAG_SW_ACTIVITY))
		return ap->ops->sw_activity_show(atadev, buf);
	return -EINVAL;
}

static ssize_t
ata_scsi_activity_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ata_port *ap = ata_shost_to_port(sdev->host);
	struct ata_device *atadev = ata_scsi_find_dev(ap, sdev);
	enum sw_activity val;
	int rc;

	if (atadev && ap->ops->sw_activity_store &&
	    (ap->flags & ATA_FLAG_SW_ACTIVITY)) {
		val = simple_strtoul(buf, NULL, 0);
		switch (val) {
		case OFF: case BLINK_ON: case BLINK_OFF:
			rc = ap->ops->sw_activity_store(atadev, val);
			if (!rc)
				return count;
			else
				return rc;
		}
	}
	return -EINVAL;
}
DEVICE_ATTR(sw_activity, S_IWUSR | S_IRUGO, ata_scsi_activity_show,
			ata_scsi_activity_store);
EXPORT_SYMBOL_GPL(dev_attr_sw_activity);

/**
 *	ata_change_queue_depth - Set a device maximum queue depth
 *	@ap: ATA port of the target device
 *	@sdev: SCSI device to configure queue depth for
 *	@queue_depth: new queue depth
 *
 *	Helper to set a device maximum queue depth, usable with both libsas
 *	and libata.
 *
 */
int ata_change_queue_depth(struct ata_port *ap, struct scsi_device *sdev,
			   int queue_depth)
{
	struct ata_device *dev;
	unsigned long flags;
	int max_queue_depth;

	spin_lock_irqsave(ap->lock, flags);

	dev = ata_scsi_find_dev(ap, sdev);
	if (!dev || queue_depth < 1 || queue_depth == sdev->queue_depth) {
		spin_unlock_irqrestore(ap->lock, flags);
		return sdev->queue_depth;
	}

	/*
	 * Make sure that the queue depth requested does not exceed the device
	 * capabilities.
	 */
	max_queue_depth = min(ATA_MAX_QUEUE, sdev->host->can_queue);
	max_queue_depth = min(max_queue_depth, ata_id_queue_depth(dev->id));
	if (queue_depth > max_queue_depth) {
		spin_unlock_irqrestore(ap->lock, flags);
		return -EINVAL;
	}

	/*
	 * If NCQ is not supported by the device or if the target queue depth
	 * is 1 (to disable drive side command queueing), turn off NCQ.
	 */
	if (queue_depth == 1 || !ata_ncq_supported(dev)) {
		dev->flags |= ATA_DFLAG_NCQ_OFF;
		queue_depth = 1;
	} else {
		dev->flags &= ~ATA_DFLAG_NCQ_OFF;
	}

	spin_unlock_irqrestore(ap->lock, flags);

	if (queue_depth == sdev->queue_depth)
		return sdev->queue_depth;

	return scsi_change_queue_depth(sdev, queue_depth);
}
EXPORT_SYMBOL_GPL(ata_change_queue_depth);

/**
 *	ata_scsi_change_queue_depth - SCSI callback for queue depth config
 *	@sdev: SCSI device to configure queue depth for
 *	@queue_depth: new queue depth
 *
 *	This is libata standard hostt->change_queue_depth callback.
 *	SCSI will call into this callback when user tries to set queue
 *	depth via sysfs.
 *
 *	LOCKING:
 *	SCSI layer (we don't care)
 *
 *	RETURNS:
 *	Newly configured queue depth.
 */
int ata_scsi_change_queue_depth(struct scsi_device *sdev, int queue_depth)
{
	struct ata_port *ap = ata_shost_to_port(sdev->host);

	return ata_change_queue_depth(ap, sdev, queue_depth);
}
EXPORT_SYMBOL_GPL(ata_scsi_change_queue_depth);

/**
 *	ata_sas_port_alloc - Allocate port for a SAS attached SATA device
 *	@host: ATA host container for all SAS ports
 *	@port_info: Information from low-level host driver
 *	@shost: SCSI host that the scsi device is attached to
 *
 *	LOCKING:
 *	PCI/etc. bus probe sem.
 *
 *	RETURNS:
 *	ata_port pointer on success / NULL on failure.
 */

struct ata_port *ata_sas_port_alloc(struct ata_host *host,
				    struct ata_port_info *port_info,
				    struct Scsi_Host *shost)
{
	struct ata_port *ap;

	ap = ata_port_alloc(host);
	if (!ap)
		return NULL;

	ap->port_no = 0;
	ap->lock = &host->lock;
	ap->pio_mask = port_info->pio_mask;
	ap->mwdma_mask = port_info->mwdma_mask;
	ap->udma_mask = port_info->udma_mask;
	ap->flags |= port_info->flags;
	ap->ops = port_info->port_ops;
	ap->cbl = ATA_CBL_SATA;
	ap->print_id = atomic_inc_return(&ata_print_id);

	return ap;
}
EXPORT_SYMBOL_GPL(ata_sas_port_alloc);

int ata_sas_tport_add(struct device *parent, struct ata_port *ap)
{
	return ata_tport_add(parent, ap);
}
EXPORT_SYMBOL_GPL(ata_sas_tport_add);

void ata_sas_tport_delete(struct ata_port *ap)
{
	ata_tport_delete(ap);
}
EXPORT_SYMBOL_GPL(ata_sas_tport_delete);

/**
 *	ata_sas_slave_configure - Default slave_config routine for libata devices
 *	@sdev: SCSI device to configure
 *	@ap: ATA port to which SCSI device is attached
 *
 *	RETURNS:
 *	Zero.
 */

int ata_sas_slave_configure(struct scsi_device *sdev, struct ata_port *ap)
{
	ata_scsi_sdev_config(sdev);

	return ata_scsi_dev_config(sdev, ap->link.device);
}
EXPORT_SYMBOL_GPL(ata_sas_slave_configure);

/**
 *	ata_sas_queuecmd - Issue SCSI cdb to libata-managed device
 *	@cmd: SCSI command to be sent
 *	@ap:	ATA port to which the command is being sent
 *
 *	RETURNS:
 *	Return value from __ata_scsi_queuecmd() if @cmd can be queued,
 *	0 otherwise.
 */

int ata_sas_queuecmd(struct scsi_cmnd *cmd, struct ata_port *ap)
{
	int rc = 0;

	if (likely(ata_dev_enabled(ap->link.device)))
		rc = __ata_scsi_queuecmd(cmd, ap->link.device);
	else {
		cmd->result = (DID_BAD_TARGET << 16);
		scsi_done(cmd);
	}
	return rc;
}
EXPORT_SYMBOL_GPL(ata_sas_queuecmd);

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
EXPORT_SYMBOL_GPL(sata_async_notification);

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

	tf->status = buf[2];
	tf->error = buf[3];
	tf->lbal = buf[4];
	tf->lbam = buf[5];
	tf->lbah = buf[6];
	tf->device = buf[7];
	tf->hob_lbal = buf[8];
	tf->hob_lbam = buf[9];
	tf->hob_lbah = buf[10];
	tf->nsect = buf[12];
	tf->hob_nsect = buf[13];
	if (ata_id_has_ncq_autosense(dev->id) && (tf->status & ATA_SENSE))
		tf->auxiliary = buf[14] << 16 | buf[15] << 8 | buf[16];

	return 0;
}

/**
 *	ata_eh_read_sense_success_ncq_log - Read the sense data for successful
 *					    NCQ commands log
 *	@link: ATA link to get sense data for
 *
 *	Read the sense data for successful NCQ commands log page to obtain
 *	sense data for all NCQ commands that completed successfully with
 *	the sense data available bit set.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int ata_eh_read_sense_success_ncq_log(struct ata_link *link)
{
	struct ata_device *dev = link->device;
	struct ata_port *ap = dev->link->ap;
	u8 *buf = ap->ncq_sense_buf;
	struct ata_queued_cmd *qc;
	unsigned int err_mask, tag;
	u8 *sense, sk = 0, asc = 0, ascq = 0;
	u64 sense_valid, val;
	int ret = 0;

	err_mask = ata_read_log_page(dev, ATA_LOG_SENSE_NCQ, 0, buf, 2);
	if (err_mask) {
		ata_dev_err(dev,
			"Failed to read Sense Data for Successful NCQ Commands log\n");
		return -EIO;
	}

	/* Check the log header */
	val = get_unaligned_le64(&buf[0]);
	if ((val & 0xffff) != 1 || ((val >> 16) & 0xff) != 0x0f) {
		ata_dev_err(dev,
			"Invalid Sense Data for Successful NCQ Commands log\n");
		return -EIO;
	}

	sense_valid = (u64)buf[8] | ((u64)buf[9] << 8) |
		((u64)buf[10] << 16) | ((u64)buf[11] << 24);

	ata_qc_for_each_raw(ap, qc, tag) {
		if (!(qc->flags & ATA_QCFLAG_EH) ||
		    !(qc->flags & ATA_QCFLAG_EH_SUCCESS_CMD) ||
		    qc->err_mask ||
		    ata_dev_phys_link(qc->dev) != link)
			continue;

		/*
		 * If the command does not have any sense data, clear ATA_SENSE.
		 * Keep ATA_QCFLAG_EH_SUCCESS_CMD so that command is finished.
		 */
		if (!(sense_valid & (1ULL << tag))) {
			qc->result_tf.status &= ~ATA_SENSE;
			continue;
		}

		sense = &buf[32 + 24 * tag];
		sk = sense[0];
		asc = sense[1];
		ascq = sense[2];

		if (!ata_scsi_sense_is_valid(sk, asc, ascq)) {
			ret = -EIO;
			continue;
		}

		/* Set sense without also setting scsicmd->result */
		scsi_build_sense_buffer(dev->flags & ATA_DFLAG_D_SENSE,
					qc->scsicmd->sense_buffer, sk,
					asc, ascq);
		qc->flags |= ATA_QCFLAG_SENSE_VALID;

		/*
		 * If we have sense data, call scsi_check_sense() in order to
		 * set the correct SCSI ML byte (if any). No point in checking
		 * the return value, since the command has already completed
		 * successfully.
		 */
		scsi_check_sense(qc->scsicmd);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(ata_eh_read_sense_success_ncq_log);

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
	if (ata_port_is_frozen(ap))
		return;

	/* is it NCQ device error? */
	if (!link->sactive || !(ehc->i.err_mask & AC_ERR_DEV))
		return;

	/* has LLDD analyzed already? */
	ata_qc_for_each_raw(ap, qc, tag) {
		if (!(qc->flags & ATA_QCFLAG_EH))
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

	/*
	 * If the device supports NCQ autosense, ata_eh_read_log_10h() will have
	 * stored the sense data in qc->result_tf.auxiliary.
	 */
	if (qc->result_tf.auxiliary) {
		char sense_key, asc, ascq;

		sense_key = (qc->result_tf.auxiliary >> 16) & 0xff;
		asc = (qc->result_tf.auxiliary >> 8) & 0xff;
		ascq = qc->result_tf.auxiliary & 0xff;
		if (ata_scsi_sense_is_valid(sense_key, asc, ascq)) {
			ata_scsi_set_sense(dev, qc->scsicmd, sense_key, asc,
					   ascq);
			ata_scsi_set_sense_information(dev, qc->scsicmd,
						       &qc->result_tf);
			qc->flags |= ATA_QCFLAG_SENSE_VALID;
		}
	}

	ata_qc_for_each_raw(ap, qc, tag) {
		if (!(qc->flags & ATA_QCFLAG_EH) ||
		    qc->flags & ATA_QCFLAG_EH_SUCCESS_CMD ||
		    ata_dev_phys_link(qc->dev) != link)
			continue;

		/* Skip the single QC which caused the NCQ error. */
		if (qc->err_mask)
			continue;

		/*
		 * For SATA, the STATUS and ERROR fields are shared for all NCQ
		 * commands that were completed with the same SDB FIS.
		 * Therefore, we have to clear the ATA_ERR bit for all QCs
		 * except the one that caused the NCQ error.
		 */
		qc->result_tf.status &= ~ATA_ERR;
		qc->result_tf.error = 0;

		/*
		 * If we get a NCQ error, that means that a single command was
		 * aborted. All other failed commands for our link should be
		 * retried and has no business of going though further scrutiny
		 * by ata_eh_link_autopsy().
		 */
		qc->flags |= ATA_QCFLAG_RETRY;
	}

	ehc->i.err_mask &= ~AC_ERR_DEV;
}
EXPORT_SYMBOL_GPL(ata_eh_analyze_ncq_error);
