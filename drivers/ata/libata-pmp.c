/*
 * libata-pmp.c - libata port multiplier support
 *
 * Copyright (c) 2007  SUSE Linux Products GmbH
 * Copyright (c) 2007  Tejun Heo <teheo@suse.de>
 *
 * This file is released under the GPLv2.
 */

#include <linux/kernel.h>
#include <linux/libata.h>
#include "libata.h"

/**
 *	sata_pmp_read - read PMP register
 *	@link: link to read PMP register for
 *	@reg: register to read
 *	@r_val: resulting value
 *
 *	Read PMP register.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, AC_ERR_* mask on failure.
 */
static unsigned int sata_pmp_read(struct ata_link *link, int reg, u32 *r_val)
{
	struct ata_port *ap = link->ap;
	struct ata_device *pmp_dev = ap->link.device;
	struct ata_taskfile tf;
	unsigned int err_mask;

	ata_tf_init(pmp_dev, &tf);
	tf.command = ATA_CMD_PMP_READ;
	tf.protocol = ATA_PROT_NODATA;
	tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	tf.feature = reg;
	tf.device = link->pmp;

	err_mask = ata_exec_internal(pmp_dev, &tf, NULL, DMA_NONE, NULL, 0,
				     SATA_PMP_SCR_TIMEOUT);
	if (err_mask)
		return err_mask;

	*r_val = tf.nsect | tf.lbal << 8 | tf.lbam << 16 | tf.lbah << 24;
	return 0;
}

/**
 *	sata_pmp_write - write PMP register
 *	@link: link to write PMP register for
 *	@reg: register to write
 *	@r_val: value to write
 *
 *	Write PMP register.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, AC_ERR_* mask on failure.
 */
static unsigned int sata_pmp_write(struct ata_link *link, int reg, u32 val)
{
	struct ata_port *ap = link->ap;
	struct ata_device *pmp_dev = ap->link.device;
	struct ata_taskfile tf;

	ata_tf_init(pmp_dev, &tf);
	tf.command = ATA_CMD_PMP_WRITE;
	tf.protocol = ATA_PROT_NODATA;
	tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	tf.feature = reg;
	tf.device = link->pmp;
	tf.nsect = val & 0xff;
	tf.lbal = (val >> 8) & 0xff;
	tf.lbam = (val >> 16) & 0xff;
	tf.lbah = (val >> 24) & 0xff;

	return ata_exec_internal(pmp_dev, &tf, NULL, DMA_NONE, NULL, 0,
				 SATA_PMP_SCR_TIMEOUT);
}

/**
 *	sata_pmp_qc_defer_cmd_switch - qc_defer for command switching PMP
 *	@qc: ATA command in question
 *
 *	A host which has command switching PMP support cannot issue
 *	commands to multiple links simultaneously.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	ATA_DEFER_* if deferring is needed, 0 otherwise.
 */
int sata_pmp_qc_defer_cmd_switch(struct ata_queued_cmd *qc)
{
	struct ata_link *link = qc->dev->link;
	struct ata_port *ap = link->ap;

	if (ap->excl_link == NULL || ap->excl_link == link) {
		if (ap->nr_active_links == 0 || ata_link_active(link)) {
			qc->flags |= ATA_QCFLAG_CLEAR_EXCL;
			return ata_std_qc_defer(qc);
		}

		ap->excl_link = link;
	}

	return ATA_DEFER_PORT;
}

/**
 *	sata_pmp_scr_read - read PSCR
 *	@link: ATA link to read PSCR for
 *	@reg: PSCR to read
 *	@r_val: resulting value
 *
 *	Read PSCR @reg into @r_val for @link, to be called from
 *	ata_scr_read().
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
int sata_pmp_scr_read(struct ata_link *link, int reg, u32 *r_val)
{
	unsigned int err_mask;

	if (reg > SATA_PMP_PSCR_CONTROL)
		return -EINVAL;

	err_mask = sata_pmp_read(link, reg, r_val);
	if (err_mask) {
		ata_link_printk(link, KERN_WARNING, "failed to read SCR %d "
				"(Emask=0x%x)\n", reg, err_mask);
		return -EIO;
	}
	return 0;
}

/**
 *	sata_pmp_scr_write - write PSCR
 *	@link: ATA link to write PSCR for
 *	@reg: PSCR to write
 *	@val: value to be written
 *
 *	Write @val to PSCR @reg for @link, to be called from
 *	ata_scr_write() and ata_scr_write_flush().
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
int sata_pmp_scr_write(struct ata_link *link, int reg, u32 val)
{
	unsigned int err_mask;

	if (reg > SATA_PMP_PSCR_CONTROL)
		return -EINVAL;

	err_mask = sata_pmp_write(link, reg, val);
	if (err_mask) {
		ata_link_printk(link, KERN_WARNING, "failed to write SCR %d "
				"(Emask=0x%x)\n", reg, err_mask);
		return -EIO;
	}
	return 0;
}

/**
 *	sata_pmp_std_prereset - prepare PMP link for reset
 *	@link: link to be reset
 *	@deadline: deadline jiffies for the operation
 *
 *	@link is about to be reset.  Initialize it.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int sata_pmp_std_prereset(struct ata_link *link, unsigned long deadline)
{
	struct ata_eh_context *ehc = &link->eh_context;
	const unsigned long *timing = sata_ehc_deb_timing(ehc);
	int rc;

	/* force HRST? */
	if (link->flags & ATA_LFLAG_NO_SRST)
		ehc->i.action |= ATA_EH_HARDRESET;

	/* handle link resume */
	if ((ehc->i.flags & ATA_EHI_RESUME_LINK) &&
	    (link->flags & ATA_LFLAG_HRST_TO_RESUME))
		ehc->i.action |= ATA_EH_HARDRESET;

	/* if we're about to do hardreset, nothing more to do */
	if (ehc->i.action & ATA_EH_HARDRESET)
		return 0;

	/* resume link */
	rc = sata_link_resume(link, timing, deadline);
	if (rc) {
		/* phy resume failed */
		ata_link_printk(link, KERN_WARNING, "failed to resume link "
				"for reset (errno=%d)\n", rc);
		return rc;
	}

	/* clear SError bits including .X which blocks the port when set */
	rc = sata_scr_write(link, SCR_ERROR, 0xffffffff);
	if (rc) {
		ata_link_printk(link, KERN_ERR,
				"failed to clear SError (errno=%d)\n", rc);
		return rc;
	}

	return 0;
}

/**
 *	sata_pmp_std_hardreset - standard hardreset method for PMP link
 *	@link: link to be reset
 *	@class: resulting class of attached device
 *	@deadline: deadline jiffies for the operation
 *
 *	Hardreset PMP port @link.  Note that this function doesn't
 *	wait for BSY clearance.  There simply isn't a generic way to
 *	wait the event.  Instead, this function return -EAGAIN thus
 *	telling libata-EH to followup with softreset.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int sata_pmp_std_hardreset(struct ata_link *link, unsigned int *class,
			   unsigned long deadline)
{
	const unsigned long *timing = sata_ehc_deb_timing(&link->eh_context);
	u32 tmp;
	int rc;

	DPRINTK("ENTER\n");

	/* do hardreset */
	rc = sata_link_hardreset(link, timing, deadline);
	if (rc) {
		ata_link_printk(link, KERN_ERR,
				"COMRESET failed (errno=%d)\n", rc);
		goto out;
	}

	/* clear SError bits including .X which blocks the port when set */
	rc = sata_scr_write(link, SCR_ERROR, 0xffffffff);
	if (rc) {
		ata_link_printk(link, KERN_ERR, "failed to clear SError "
				"during hardreset (errno=%d)\n", rc);
		goto out;
	}

	/* if device is present, follow up with srst to wait for !BSY */
	if (ata_link_online(link))
		rc = -EAGAIN;
 out:
	/* if SCR isn't accessible, we need to reset the PMP */
	if (rc && rc != -EAGAIN && sata_scr_read(link, SCR_STATUS, &tmp))
		rc = -ERESTART;

	DPRINTK("EXIT, rc=%d\n", rc);
	return rc;
}

/**
 *	ata_std_postreset - standard postreset method for PMP link
 *	@link: the target ata_link
 *	@classes: classes of attached devices
 *
 *	This function is invoked after a successful reset.  Note that
 *	the device might have been reset more than once using
 *	different reset methods before postreset is invoked.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 */
void sata_pmp_std_postreset(struct ata_link *link, unsigned int *class)
{
	u32 serror;

	DPRINTK("ENTER\n");

	/* clear SError */
	if (sata_scr_read(link, SCR_ERROR, &serror) == 0)
		sata_scr_write(link, SCR_ERROR, serror);

	/* print link status */
	sata_print_link_status(link);

	DPRINTK("EXIT\n");
}

/**
 *	sata_pmp_read_gscr - read GSCR block of SATA PMP
 *	@dev: PMP device
 *	@gscr: buffer to read GSCR block into
 *
 *	Read selected PMP GSCRs from the PMP at @dev.  This will serve
 *	as configuration and identification info for the PMP.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
static int sata_pmp_read_gscr(struct ata_device *dev, u32 *gscr)
{
	static const int gscr_to_read[] = { 0, 1, 2, 32, 33, 64, 96 };
	int i;

	for (i = 0; i < ARRAY_SIZE(gscr_to_read); i++) {
		int reg = gscr_to_read[i];
		unsigned int err_mask;

		err_mask = sata_pmp_read(dev->link, reg, &gscr[reg]);
		if (err_mask) {
			ata_dev_printk(dev, KERN_ERR, "failed to read PMP "
				"GSCR[%d] (Emask=0x%x)\n", reg, err_mask);
			return -EIO;
		}
	}

	return 0;
}

static const char *sata_pmp_spec_rev_str(const u32 *gscr)
{
	u32 rev = gscr[SATA_PMP_GSCR_REV];

	if (rev & (1 << 2))
		return "1.1";
	if (rev & (1 << 1))
		return "1.0";
	return "<unknown>";
}

static int sata_pmp_configure(struct ata_device *dev, int print_info)
{
	struct ata_port *ap = dev->link->ap;
	u32 *gscr = dev->gscr;
	unsigned int err_mask = 0;
	const char *reason;
	int nr_ports, rc;

	nr_ports = sata_pmp_gscr_ports(gscr);

	if (nr_ports <= 0 || nr_ports > SATA_PMP_MAX_PORTS) {
		rc = -EINVAL;
		reason = "invalid nr_ports";
		goto fail;
	}

	if ((ap->flags & ATA_FLAG_AN) &&
	    (gscr[SATA_PMP_GSCR_FEAT] & SATA_PMP_FEAT_NOTIFY))
		dev->flags |= ATA_DFLAG_AN;

	/* monitor SERR_PHYRDY_CHG on fan-out ports */
	err_mask = sata_pmp_write(dev->link, SATA_PMP_GSCR_ERROR_EN,
				  SERR_PHYRDY_CHG);
	if (err_mask) {
		rc = -EIO;
		reason = "failed to write GSCR_ERROR_EN";
		goto fail;
	}

	/* turn off notification till fan-out ports are reset and configured */
	if (gscr[SATA_PMP_GSCR_FEAT_EN] & SATA_PMP_FEAT_NOTIFY) {
		gscr[SATA_PMP_GSCR_FEAT_EN] &= ~SATA_PMP_FEAT_NOTIFY;

		err_mask = sata_pmp_write(dev->link, SATA_PMP_GSCR_FEAT_EN,
					  gscr[SATA_PMP_GSCR_FEAT_EN]);
		if (err_mask) {
			rc = -EIO;
			reason = "failed to write GSCR_FEAT_EN";
			goto fail;
		}
	}

	if (print_info) {
		ata_dev_printk(dev, KERN_INFO, "Port Multiplier %s, "
			       "0x%04x:0x%04x r%d, %d ports, feat 0x%x/0x%x\n",
			       sata_pmp_spec_rev_str(gscr),
			       sata_pmp_gscr_vendor(gscr),
			       sata_pmp_gscr_devid(gscr),
			       sata_pmp_gscr_rev(gscr),
			       nr_ports, gscr[SATA_PMP_GSCR_FEAT_EN],
			       gscr[SATA_PMP_GSCR_FEAT]);

		if (!(dev->flags & ATA_DFLAG_AN))
			ata_dev_printk(dev, KERN_INFO,
				"Asynchronous notification not supported, "
				"hotplug won't\n         work on fan-out "
				"ports. Use warm-plug instead.\n");
	}

	return 0;

 fail:
	ata_dev_printk(dev, KERN_ERR,
		       "failed to configure Port Multiplier (%s, Emask=0x%x)\n",
		       reason, err_mask);
	return rc;
}

static int sata_pmp_init_links(struct ata_port *ap, int nr_ports)
{
	struct ata_link *pmp_link = ap->pmp_link;
	int i;

	if (!pmp_link) {
		pmp_link = kzalloc(sizeof(pmp_link[0]) * SATA_PMP_MAX_PORTS,
				   GFP_NOIO);
		if (!pmp_link)
			return -ENOMEM;

		for (i = 0; i < SATA_PMP_MAX_PORTS; i++)
			ata_link_init(ap, &pmp_link[i], i);

		ap->pmp_link = pmp_link;
	}

	for (i = 0; i < nr_ports; i++) {
		struct ata_link *link = &pmp_link[i];
		struct ata_eh_context *ehc = &link->eh_context;

		link->flags = 0;
		ehc->i.probe_mask |= 1;
		ehc->i.action |= ATA_EH_SOFTRESET;
		ehc->i.flags |= ATA_EHI_RESUME_LINK;
	}

	return 0;
}

static void sata_pmp_quirks(struct ata_port *ap)
{
	u32 *gscr = ap->link.device->gscr;
	u16 vendor = sata_pmp_gscr_vendor(gscr);
	u16 devid = sata_pmp_gscr_devid(gscr);
	struct ata_link *link;

	if (vendor == 0x1095 && devid == 0x3726) {
		/* sil3726 quirks */
		ata_port_for_each_link(link, ap) {
			/* SError.N need a kick in the ass to get working */
			link->flags |= ATA_LFLAG_HRST_TO_RESUME;

			/* class code report is unreliable */
			if (link->pmp < 5)
				link->flags |= ATA_LFLAG_ASSUME_ATA;

			/* port 5 is for SEMB device and it doesn't like SRST */
			if (link->pmp == 5)
				link->flags |= ATA_LFLAG_NO_SRST |
					       ATA_LFLAG_ASSUME_SEMB;
		}
	} else if (vendor == 0x1095 && devid == 0x4723) {
		/* sil4723 quirks */
		ata_port_for_each_link(link, ap) {
			/* SError.N need a kick in the ass to get working */
			link->flags |= ATA_LFLAG_HRST_TO_RESUME;

			/* class code report is unreliable */
			if (link->pmp < 2)
				link->flags |= ATA_LFLAG_ASSUME_ATA;

			/* the config device at port 2 locks up on SRST */
			if (link->pmp == 2)
				link->flags |= ATA_LFLAG_NO_SRST |
					       ATA_LFLAG_ASSUME_ATA;
		}
	} else if (vendor == 0x1095 && devid == 0x4726) {
		/* sil4726 quirks */
		ata_port_for_each_link(link, ap) {
			/* SError.N need a kick in the ass to get working */
			link->flags |= ATA_LFLAG_HRST_TO_RESUME;

			/* Class code report is unreliable and SRST
			 * times out under certain configurations.
			 * Config device can be at port 0 or 5 and
			 * locks up on SRST.
			 */
			if (link->pmp <= 5)
				link->flags |= ATA_LFLAG_NO_SRST |
					       ATA_LFLAG_ASSUME_ATA;

			/* Port 6 is for SEMB device which doesn't
			 * like SRST either.
			 */
			if (link->pmp == 6)
				link->flags |= ATA_LFLAG_NO_SRST |
					       ATA_LFLAG_ASSUME_SEMB;
		}
	} else if (vendor == 0x1095 && (devid == 0x5723 || devid == 0x5733 ||
					devid == 0x5734 || devid == 0x5744)) {
		/* sil5723/5744 quirks */

		/* sil5723/5744 has either two or three downstream
		 * ports depending on operation mode.  The last port
		 * is empty if any actual IO device is available or
		 * occupied by a pseudo configuration device
		 * otherwise.  Don't try hard to recover it.
		 */
		ap->pmp_link[ap->nr_pmp_links - 1].flags |= ATA_LFLAG_NO_RETRY;
	} else if (vendor == 0x11ab && devid == 0x4140) {
		/* Marvell 88SM4140 quirks.  Fan-out ports require PHY
		 * reset to work; other than that, it behaves very
		 * nicely.
		 */
		ata_port_for_each_link(link, ap)
			link->flags |= ATA_LFLAG_HRST_TO_RESUME;
	}
}

/**
 *	sata_pmp_attach - attach a SATA PMP device
 *	@dev: SATA PMP device to attach
 *
 *	Configure and attach SATA PMP device @dev.  This function is
 *	also responsible for allocating and initializing PMP links.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
int sata_pmp_attach(struct ata_device *dev)
{
	struct ata_link *link = dev->link;
	struct ata_port *ap = link->ap;
	unsigned long flags;
	struct ata_link *tlink;
	int rc;

	/* is it hanging off the right place? */
	if (!(ap->flags & ATA_FLAG_PMP)) {
		ata_dev_printk(dev, KERN_ERR,
			       "host does not support Port Multiplier\n");
		return -EINVAL;
	}

	if (!ata_is_host_link(link)) {
		ata_dev_printk(dev, KERN_ERR,
			       "Port Multipliers cannot be nested\n");
		return -EINVAL;
	}

	if (dev->devno) {
		ata_dev_printk(dev, KERN_ERR,
			       "Port Multiplier must be the first device\n");
		return -EINVAL;
	}

	WARN_ON(link->pmp != 0);
	link->pmp = SATA_PMP_CTRL_PORT;

	/* read GSCR block */
	rc = sata_pmp_read_gscr(dev, dev->gscr);
	if (rc)
		goto fail;

	/* config PMP */
	rc = sata_pmp_configure(dev, 1);
	if (rc)
		goto fail;

	rc = sata_pmp_init_links(ap, sata_pmp_gscr_ports(dev->gscr));
	if (rc) {
		ata_dev_printk(dev, KERN_INFO,
			       "failed to initialize PMP links\n");
		goto fail;
	}

	/* attach it */
	spin_lock_irqsave(ap->lock, flags);
	WARN_ON(ap->nr_pmp_links);
	ap->nr_pmp_links = sata_pmp_gscr_ports(dev->gscr);
	spin_unlock_irqrestore(ap->lock, flags);

	sata_pmp_quirks(ap);

	if (ap->ops->pmp_attach)
		ap->ops->pmp_attach(ap);

	ata_port_for_each_link(tlink, ap)
		sata_link_init_spd(tlink);

	ata_acpi_associate_sata_port(ap);

	return 0;

 fail:
	link->pmp = 0;
	return rc;
}

/**
 *	sata_pmp_detach - detach a SATA PMP device
 *	@dev: SATA PMP device to detach
 *
 *	Detach SATA PMP device @dev.  This function is also
 *	responsible for deconfiguring PMP links.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 */
static void sata_pmp_detach(struct ata_device *dev)
{
	struct ata_link *link = dev->link;
	struct ata_port *ap = link->ap;
	struct ata_link *tlink;
	unsigned long flags;

	ata_dev_printk(dev, KERN_INFO, "Port Multiplier detaching\n");

	WARN_ON(!ata_is_host_link(link) || dev->devno ||
		link->pmp != SATA_PMP_CTRL_PORT);

	if (ap->ops->pmp_detach)
		ap->ops->pmp_detach(ap);

	ata_port_for_each_link(tlink, ap)
		ata_eh_detach_dev(tlink->device);

	spin_lock_irqsave(ap->lock, flags);
	ap->nr_pmp_links = 0;
	link->pmp = 0;
	spin_unlock_irqrestore(ap->lock, flags);

	ata_acpi_associate_sata_port(ap);
}

/**
 *	sata_pmp_same_pmp - does new GSCR matches the configured PMP?
 *	@dev: PMP device to compare against
 *	@new_gscr: GSCR block of the new device
 *
 *	Compare @new_gscr against @dev and determine whether @dev is
 *	the PMP described by @new_gscr.
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	1 if @dev matches @new_gscr, 0 otherwise.
 */
static int sata_pmp_same_pmp(struct ata_device *dev, const u32 *new_gscr)
{
	const u32 *old_gscr = dev->gscr;
	u16 old_vendor, new_vendor, old_devid, new_devid;
	int old_nr_ports, new_nr_ports;

	old_vendor = sata_pmp_gscr_vendor(old_gscr);
	new_vendor = sata_pmp_gscr_vendor(new_gscr);
	old_devid = sata_pmp_gscr_devid(old_gscr);
	new_devid = sata_pmp_gscr_devid(new_gscr);
	old_nr_ports = sata_pmp_gscr_ports(old_gscr);
	new_nr_ports = sata_pmp_gscr_ports(new_gscr);

	if (old_vendor != new_vendor) {
		ata_dev_printk(dev, KERN_INFO, "Port Multiplier "
			       "vendor mismatch '0x%x' != '0x%x'\n",
			       old_vendor, new_vendor);
		return 0;
	}

	if (old_devid != new_devid) {
		ata_dev_printk(dev, KERN_INFO, "Port Multiplier "
			       "device ID mismatch '0x%x' != '0x%x'\n",
			       old_devid, new_devid);
		return 0;
	}

	if (old_nr_ports != new_nr_ports) {
		ata_dev_printk(dev, KERN_INFO, "Port Multiplier "
			       "nr_ports mismatch '0x%x' != '0x%x'\n",
			       old_nr_ports, new_nr_ports);
		return 0;
	}

	return 1;
}

/**
 *	sata_pmp_revalidate - revalidate SATA PMP
 *	@dev: PMP device to revalidate
 *	@new_class: new class code
 *
 *	Re-read GSCR block and make sure @dev is still attached to the
 *	port and properly configured.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
static int sata_pmp_revalidate(struct ata_device *dev, unsigned int new_class)
{
	struct ata_link *link = dev->link;
	struct ata_port *ap = link->ap;
	u32 *gscr = (void *)ap->sector_buf;
	int rc;

	DPRINTK("ENTER\n");

	ata_eh_about_to_do(link, NULL, ATA_EH_REVALIDATE);

	if (!ata_dev_enabled(dev)) {
		rc = -ENODEV;
		goto fail;
	}

	/* wrong class? */
	if (ata_class_enabled(new_class) && new_class != ATA_DEV_PMP) {
		rc = -ENODEV;
		goto fail;
	}

	/* read GSCR */
	rc = sata_pmp_read_gscr(dev, gscr);
	if (rc)
		goto fail;

	/* is the pmp still there? */
	if (!sata_pmp_same_pmp(dev, gscr)) {
		rc = -ENODEV;
		goto fail;
	}

	memcpy(dev->gscr, gscr, sizeof(gscr[0]) * SATA_PMP_GSCR_DWORDS);

	rc = sata_pmp_configure(dev, 0);
	if (rc)
		goto fail;

	ata_eh_done(link, NULL, ATA_EH_REVALIDATE);

	DPRINTK("EXIT, rc=0\n");
	return 0;

 fail:
	ata_dev_printk(dev, KERN_ERR,
		       "PMP revalidation failed (errno=%d)\n", rc);
	DPRINTK("EXIT, rc=%d\n", rc);
	return rc;
}

/**
 *	sata_pmp_revalidate_quick - revalidate SATA PMP quickly
 *	@dev: PMP device to revalidate
 *
 *	Make sure the attached PMP is accessible.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
static int sata_pmp_revalidate_quick(struct ata_device *dev)
{
	unsigned int err_mask;
	u32 prod_id;

	err_mask = sata_pmp_read(dev->link, SATA_PMP_GSCR_PROD_ID, &prod_id);
	if (err_mask) {
		ata_dev_printk(dev, KERN_ERR, "failed to read PMP product ID "
			       "(Emask=0x%x)\n", err_mask);
		return -EIO;
	}

	if (prod_id != dev->gscr[SATA_PMP_GSCR_PROD_ID]) {
		ata_dev_printk(dev, KERN_ERR, "PMP product ID mismatch\n");
		/* something weird is going on, request full PMP recovery */
		return -EIO;
	}

	return 0;
}

/**
 *	sata_pmp_eh_recover_pmp - recover PMP
 *	@ap: ATA port PMP is attached to
 *	@prereset: prereset method (can be NULL)
 *	@softreset: softreset method
 *	@hardreset: hardreset method
 *	@postreset: postreset method (can be NULL)
 *
 *	Recover PMP attached to @ap.  Recovery procedure is somewhat
 *	similar to that of ata_eh_recover() except that reset should
 *	always be performed in hard->soft sequence and recovery
 *	failure results in PMP detachment.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
static int sata_pmp_eh_recover_pmp(struct ata_port *ap,
		ata_prereset_fn_t prereset, ata_reset_fn_t softreset,
		ata_reset_fn_t hardreset, ata_postreset_fn_t postreset)
{
	struct ata_link *link = &ap->link;
	struct ata_eh_context *ehc = &link->eh_context;
	struct ata_device *dev = link->device;
	int tries = ATA_EH_PMP_TRIES;
	int detach = 0, rc = 0;
	int reval_failed = 0;

	DPRINTK("ENTER\n");

	if (dev->flags & ATA_DFLAG_DETACH) {
		detach = 1;
		goto fail;
	}

 retry:
	ehc->classes[0] = ATA_DEV_UNKNOWN;

	if (ehc->i.action & ATA_EH_RESET_MASK) {
		struct ata_link *tlink;

		ata_eh_freeze_port(ap);

		/* reset */
		ehc->i.action = ATA_EH_HARDRESET;
		rc = ata_eh_reset(link, 0, prereset, softreset, hardreset,
				  postreset);
		if (rc) {
			ata_link_printk(link, KERN_ERR,
					"failed to reset PMP, giving up\n");
			goto fail;
		}

		ata_eh_thaw_port(ap);

		/* PMP is reset, SErrors cannot be trusted, scan all */
		ata_port_for_each_link(tlink, ap)
			ata_ehi_schedule_probe(&tlink->eh_context.i);
	}

	/* If revalidation is requested, revalidate and reconfigure;
	 * otherwise, do quick revalidation.
	 */
	if (ehc->i.action & ATA_EH_REVALIDATE)
		rc = sata_pmp_revalidate(dev, ehc->classes[0]);
	else
		rc = sata_pmp_revalidate_quick(dev);

	if (rc) {
		tries--;

		if (rc == -ENODEV) {
			ehc->i.probe_mask |= 1;
			detach = 1;
			/* give it just two more chances */
			tries = min(tries, 2);
		}

		if (tries) {
			int sleep = ehc->i.flags & ATA_EHI_DID_RESET;

			/* consecutive revalidation failures? speed down */
			if (reval_failed)
				sata_down_spd_limit(link);
			else
				reval_failed = 1;

			ata_dev_printk(dev, KERN_WARNING,
				       "retrying hardreset%s\n",
				       sleep ? " in 5 secs" : "");
			if (sleep)
				ssleep(5);
			ehc->i.action |= ATA_EH_HARDRESET;
			goto retry;
		} else {
			ata_dev_printk(dev, KERN_ERR, "failed to recover PMP "
				       "after %d tries, giving up\n",
				       ATA_EH_PMP_TRIES);
			goto fail;
		}
	}

	/* okay, PMP resurrected */
	ehc->i.flags = 0;

	DPRINTK("EXIT, rc=0\n");
	return 0;

 fail:
	sata_pmp_detach(dev);
	if (detach)
		ata_eh_detach_dev(dev);
	else
		ata_dev_disable(dev);

	DPRINTK("EXIT, rc=%d\n", rc);
	return rc;
}

static int sata_pmp_eh_handle_disabled_links(struct ata_port *ap)
{
	struct ata_link *link;
	unsigned long flags;
	int rc;

	spin_lock_irqsave(ap->lock, flags);

	ata_port_for_each_link(link, ap) {
		if (!(link->flags & ATA_LFLAG_DISABLED))
			continue;

		spin_unlock_irqrestore(ap->lock, flags);

		/* Some PMPs require hardreset sequence to get
		 * SError.N working.
		 */
		if ((link->flags & ATA_LFLAG_HRST_TO_RESUME) &&
		    (link->eh_context.i.flags & ATA_EHI_RESUME_LINK))
			sata_link_hardreset(link, sata_deb_timing_normal,
					    jiffies + ATA_TMOUT_INTERNAL_QUICK);

		/* unconditionally clear SError.N */
		rc = sata_scr_write(link, SCR_ERROR, SERR_PHYRDY_CHG);
		if (rc) {
			ata_link_printk(link, KERN_ERR, "failed to clear "
					"SError.N (errno=%d)\n", rc);
			return rc;
		}

		spin_lock_irqsave(ap->lock, flags);
	}

	spin_unlock_irqrestore(ap->lock, flags);

	return 0;
}

static int sata_pmp_handle_link_fail(struct ata_link *link, int *link_tries)
{
	struct ata_port *ap = link->ap;
	unsigned long flags;

	if (link_tries[link->pmp] && --link_tries[link->pmp])
		return 1;

	/* disable this link */
	if (!(link->flags & ATA_LFLAG_DISABLED)) {
		ata_link_printk(link, KERN_WARNING,
			"failed to recover link after %d tries, disabling\n",
			ATA_EH_PMP_LINK_TRIES);

		spin_lock_irqsave(ap->lock, flags);
		link->flags |= ATA_LFLAG_DISABLED;
		spin_unlock_irqrestore(ap->lock, flags);
	}

	ata_dev_disable(link->device);
	link->eh_context.i.action = 0;

	return 0;
}

/**
 *	sata_pmp_eh_recover - recover PMP-enabled port
 *	@ap: ATA port to recover
 *	@prereset: prereset method (can be NULL)
 *	@softreset: softreset method
 *	@hardreset: hardreset method
 *	@postreset: postreset method (can be NULL)
 *	@pmp_prereset: PMP prereset method (can be NULL)
 *	@pmp_softreset: PMP softreset method (can be NULL)
 *	@pmp_hardreset: PMP hardreset method (can be NULL)
 *	@pmp_postreset: PMP postreset method (can be NULL)
 *
 *	Drive EH recovery operation for PMP enabled port @ap.  This
 *	function recovers host and PMP ports with proper retrials and
 *	fallbacks.  Actual recovery operations are performed using
 *	ata_eh_recover() and sata_pmp_eh_recover_pmp().
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
static int sata_pmp_eh_recover(struct ata_port *ap,
		ata_prereset_fn_t prereset, ata_reset_fn_t softreset,
		ata_reset_fn_t hardreset, ata_postreset_fn_t postreset,
		ata_prereset_fn_t pmp_prereset, ata_reset_fn_t pmp_softreset,
		ata_reset_fn_t pmp_hardreset, ata_postreset_fn_t pmp_postreset)
{
	int pmp_tries, link_tries[SATA_PMP_MAX_PORTS];
	struct ata_link *pmp_link = &ap->link;
	struct ata_device *pmp_dev = pmp_link->device;
	struct ata_eh_context *pmp_ehc = &pmp_link->eh_context;
	struct ata_link *link;
	struct ata_device *dev;
	unsigned int err_mask;
	u32 gscr_error, sntf;
	int cnt, rc;

	pmp_tries = ATA_EH_PMP_TRIES;
	ata_port_for_each_link(link, ap)
		link_tries[link->pmp] = ATA_EH_PMP_LINK_TRIES;

 retry:
	/* PMP attached? */
	if (!ap->nr_pmp_links) {
		rc = ata_eh_recover(ap, prereset, softreset, hardreset,
				    postreset, NULL);
		if (rc) {
			ata_link_for_each_dev(dev, &ap->link)
				ata_dev_disable(dev);
			return rc;
		}

		if (pmp_dev->class != ATA_DEV_PMP)
			return 0;

		/* new PMP online */
		ata_port_for_each_link(link, ap)
			link_tries[link->pmp] = ATA_EH_PMP_LINK_TRIES;

		/* fall through */
	}

	/* recover pmp */
	rc = sata_pmp_eh_recover_pmp(ap, prereset, softreset, hardreset,
				     postreset);
	if (rc)
		goto pmp_fail;

	/* handle disabled links */
	rc = sata_pmp_eh_handle_disabled_links(ap);
	if (rc)
		goto pmp_fail;

	/* recover links */
	rc = ata_eh_recover(ap, pmp_prereset, pmp_softreset, pmp_hardreset,
			    pmp_postreset, &link);
	if (rc)
		goto link_fail;

	/* Connection status might have changed while resetting other
	 * links, check SATA_PMP_GSCR_ERROR before returning.
	 */

	/* clear SNotification */
	rc = sata_scr_read(&ap->link, SCR_NOTIFICATION, &sntf);
	if (rc == 0)
		sata_scr_write(&ap->link, SCR_NOTIFICATION, sntf);

	/* enable notification */
	if (pmp_dev->flags & ATA_DFLAG_AN) {
		pmp_dev->gscr[SATA_PMP_GSCR_FEAT_EN] |= SATA_PMP_FEAT_NOTIFY;

		err_mask = sata_pmp_write(pmp_dev->link, SATA_PMP_GSCR_FEAT_EN,
					  pmp_dev->gscr[SATA_PMP_GSCR_FEAT_EN]);
		if (err_mask) {
			ata_dev_printk(pmp_dev, KERN_ERR, "failed to write "
				       "PMP_FEAT_EN (Emask=0x%x)\n", err_mask);
			rc = -EIO;
			goto pmp_fail;
		}
	}

	/* check GSCR_ERROR */
	err_mask = sata_pmp_read(pmp_link, SATA_PMP_GSCR_ERROR, &gscr_error);
	if (err_mask) {
		ata_dev_printk(pmp_dev, KERN_ERR, "failed to read "
			       "PMP_GSCR_ERROR (Emask=0x%x)\n", err_mask);
		rc = -EIO;
		goto pmp_fail;
	}

	cnt = 0;
	ata_port_for_each_link(link, ap) {
		if (!(gscr_error & (1 << link->pmp)))
			continue;

		if (sata_pmp_handle_link_fail(link, link_tries)) {
			ata_ehi_hotplugged(&link->eh_context.i);
			cnt++;
		} else {
			ata_link_printk(link, KERN_WARNING,
				"PHY status changed but maxed out on retries, "
				"giving up\n");
			ata_link_printk(link, KERN_WARNING,
				"Manully issue scan to resume this link\n");
		}
	}

	if (cnt) {
		ata_port_printk(ap, KERN_INFO, "PMP SError.N set for some "
				"ports, repeating recovery\n");
		goto retry;
	}

	return 0;

 link_fail:
	if (sata_pmp_handle_link_fail(link, link_tries)) {
		pmp_ehc->i.action |= ATA_EH_HARDRESET;
		goto retry;
	}

	/* fall through */
 pmp_fail:
	/* Control always ends up here after detaching PMP.  Shut up
	 * and return if we're unloading.
	 */
	if (ap->pflags & ATA_PFLAG_UNLOADING)
		return rc;

	if (!ap->nr_pmp_links)
		goto retry;

	if (--pmp_tries) {
		ata_port_printk(ap, KERN_WARNING,
				"failed to recover PMP, retrying in 5 secs\n");
		pmp_ehc->i.action |= ATA_EH_HARDRESET;
		ssleep(5);
		goto retry;
	}

	ata_port_printk(ap, KERN_ERR,
			"failed to recover PMP after %d tries, giving up\n",
			ATA_EH_PMP_TRIES);
	sata_pmp_detach(pmp_dev);
	ata_dev_disable(pmp_dev);

	return rc;
}

/**
 *	sata_pmp_do_eh - do standard error handling for PMP-enabled host
 *	@ap: host port to handle error for
 *	@prereset: prereset method (can be NULL)
 *	@softreset: softreset method
 *	@hardreset: hardreset method
 *	@postreset: postreset method (can be NULL)
 *	@pmp_prereset: PMP prereset method (can be NULL)
 *	@pmp_softreset: PMP softreset method (can be NULL)
 *	@pmp_hardreset: PMP hardreset method (can be NULL)
 *	@pmp_postreset: PMP postreset method (can be NULL)
 *
 *	Perform standard error handling sequence for PMP-enabled host
 *	@ap.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 */
void sata_pmp_do_eh(struct ata_port *ap,
		ata_prereset_fn_t prereset, ata_reset_fn_t softreset,
		ata_reset_fn_t hardreset, ata_postreset_fn_t postreset,
		ata_prereset_fn_t pmp_prereset, ata_reset_fn_t pmp_softreset,
		ata_reset_fn_t pmp_hardreset, ata_postreset_fn_t pmp_postreset)
{
	ata_eh_autopsy(ap);
	ata_eh_report(ap);
	sata_pmp_eh_recover(ap, prereset, softreset, hardreset, postreset,
			    pmp_prereset, pmp_softreset, pmp_hardreset,
			    pmp_postreset);
	ata_eh_finish(ap);
}
