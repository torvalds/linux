// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  SATA specific part of ATA helper library
 *
 *  Copyright 2003-2004 Red Hat, Inc.  All rights reserved.
 *  Copyright 2003-2004 Jeff Garzik
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/libata.h>

#include "libata.h"

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
	tf->command	= fis[2];	/* status */
	tf->feature	= fis[3];	/* error */

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
int sata_link_debounce(struct ata_link *link, const unsigned long *params,
		       unsigned long deadline)
{
	unsigned long interval = params[0];
	unsigned long duration = params[1];
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
int sata_link_resume(struct ata_link *link, const unsigned long *params,
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
		if (!(link->flags & ATA_LFLAG_NO_DB_DELAY))
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
		if (ata_link_nr_enabled(link) > 0)
			/* no restrictions on LPM transitions */
			scontrol &= ~(0x7 << 8);
		else {
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
int sata_set_spd_needed(struct ata_link *link)
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
