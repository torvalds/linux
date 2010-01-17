/*
 * drivers/s390/cio/device_status.c
 *
 *    Copyright (C) 2002 IBM Deutschland Entwicklung GmbH,
 *			 IBM Corporation
 *    Author(s): Cornelia Huck (cornelia.huck@de.ibm.com)
 *		 Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 * Status accumulation and basic sense functions.
 */

#include <linux/module.h>
#include <linux/init.h>

#include <asm/ccwdev.h>
#include <asm/cio.h>

#include "cio.h"
#include "cio_debug.h"
#include "css.h"
#include "device.h"
#include "ioasm.h"
#include "io_sch.h"

/*
 * Check for any kind of channel or interface control check but don't
 * issue the message for the console device
 */
static void
ccw_device_msg_control_check(struct ccw_device *cdev, struct irb *irb)
{
	char dbf_text[15];

	if (!scsw_is_valid_cstat(&irb->scsw) ||
	    !(scsw_cstat(&irb->scsw) & (SCHN_STAT_CHN_DATA_CHK |
	      SCHN_STAT_CHN_CTRL_CHK | SCHN_STAT_INTF_CTRL_CHK)))
		return;
	CIO_MSG_EVENT(0, "Channel-Check or Interface-Control-Check "
		      "received"
		      " ... device %04x on subchannel 0.%x.%04x, dev_stat "
		      ": %02X sch_stat : %02X\n",
		      cdev->private->dev_id.devno, cdev->private->schid.ssid,
		      cdev->private->schid.sch_no,
		      scsw_dstat(&irb->scsw), scsw_cstat(&irb->scsw));
	sprintf(dbf_text, "chk%x", cdev->private->schid.sch_no);
	CIO_TRACE_EVENT(0, dbf_text);
	CIO_HEX_EVENT(0, irb, sizeof(struct irb));
}

/*
 * Some paths became not operational (pno bit in scsw is set).
 */
static void
ccw_device_path_notoper(struct ccw_device *cdev)
{
	struct subchannel *sch;

	sch = to_subchannel(cdev->dev.parent);
	if (cio_update_schib(sch))
		goto doverify;

	CIO_MSG_EVENT(0, "%s(0.%x.%04x) - path(s) %02x are "
		      "not operational \n", __func__,
		      sch->schid.ssid, sch->schid.sch_no,
		      sch->schib.pmcw.pnom);

	sch->lpm &= ~sch->schib.pmcw.pnom;
doverify:
	cdev->private->flags.doverify = 1;
}

/*
 * Copy valid bits from the extended control word to device irb.
 */
static void
ccw_device_accumulate_ecw(struct ccw_device *cdev, struct irb *irb)
{
	/*
	 * Copy extended control bit if it is valid... yes there
	 * are condition that have to be met for the extended control
	 * bit to have meaning. Sick.
	 */
	cdev->private->irb.scsw.cmd.ectl = 0;
	if ((irb->scsw.cmd.stctl & SCSW_STCTL_ALERT_STATUS) &&
	    !(irb->scsw.cmd.stctl & SCSW_STCTL_INTER_STATUS))
		cdev->private->irb.scsw.cmd.ectl = irb->scsw.cmd.ectl;
	/* Check if extended control word is valid. */
	if (!cdev->private->irb.scsw.cmd.ectl)
		return;
	/* Copy concurrent sense / model dependent information. */
	memcpy (&cdev->private->irb.ecw, irb->ecw, sizeof (irb->ecw));
}

/*
 * Check if extended status word is valid.
 */
static int
ccw_device_accumulate_esw_valid(struct irb *irb)
{
	if (!irb->scsw.cmd.eswf &&
	    (irb->scsw.cmd.stctl == SCSW_STCTL_STATUS_PEND))
		return 0;
	if (irb->scsw.cmd.stctl ==
			(SCSW_STCTL_INTER_STATUS|SCSW_STCTL_STATUS_PEND) &&
	    !(irb->scsw.cmd.actl & SCSW_ACTL_SUSPENDED))
		return 0;
	return 1;
}

/*
 * Copy valid bits from the extended status word to device irb.
 */
static void
ccw_device_accumulate_esw(struct ccw_device *cdev, struct irb *irb)
{
	struct irb *cdev_irb;
	struct sublog *cdev_sublog, *sublog;

	if (!ccw_device_accumulate_esw_valid(irb))
		return;

	cdev_irb = &cdev->private->irb;

	/* Copy last path used mask. */
	cdev_irb->esw.esw1.lpum = irb->esw.esw1.lpum;

	/* Copy subchannel logout information if esw is of format 0. */
	if (irb->scsw.cmd.eswf) {
		cdev_sublog = &cdev_irb->esw.esw0.sublog;
		sublog = &irb->esw.esw0.sublog;
		/* Copy extended status flags. */
		cdev_sublog->esf = sublog->esf;
		/*
		 * Copy fields that have a meaning for channel data check
		 * channel control check and interface control check.
		 */
		if (irb->scsw.cmd.cstat & (SCHN_STAT_CHN_DATA_CHK |
				       SCHN_STAT_CHN_CTRL_CHK |
				       SCHN_STAT_INTF_CTRL_CHK)) {
			/* Copy ancillary report bit. */
			cdev_sublog->arep = sublog->arep;
			/* Copy field-validity-flags. */
			cdev_sublog->fvf = sublog->fvf;
			/* Copy storage access code. */
			cdev_sublog->sacc = sublog->sacc;
			/* Copy termination code. */
			cdev_sublog->termc = sublog->termc;
			/* Copy sequence code. */
			cdev_sublog->seqc = sublog->seqc;
		}
		/* Copy device status check. */
		cdev_sublog->devsc = sublog->devsc;
		/* Copy secondary error. */
		cdev_sublog->serr = sublog->serr;
		/* Copy i/o-error alert. */
		cdev_sublog->ioerr = sublog->ioerr;
		/* Copy channel path timeout bit. */
		if (irb->scsw.cmd.cstat & SCHN_STAT_INTF_CTRL_CHK)
			cdev_irb->esw.esw0.erw.cpt = irb->esw.esw0.erw.cpt;
		/* Copy failing storage address validity flag. */
		cdev_irb->esw.esw0.erw.fsavf = irb->esw.esw0.erw.fsavf;
		if (cdev_irb->esw.esw0.erw.fsavf) {
			/* ... and copy the failing storage address. */
			memcpy(cdev_irb->esw.esw0.faddr, irb->esw.esw0.faddr,
			       sizeof (irb->esw.esw0.faddr));
			/* ... and copy the failing storage address format. */
			cdev_irb->esw.esw0.erw.fsaf = irb->esw.esw0.erw.fsaf;
		}
		/* Copy secondary ccw address validity bit. */
		cdev_irb->esw.esw0.erw.scavf = irb->esw.esw0.erw.scavf;
		if (irb->esw.esw0.erw.scavf)
			/* ... and copy the secondary ccw address. */
			cdev_irb->esw.esw0.saddr = irb->esw.esw0.saddr;
		
	}
	/* FIXME: DCTI for format 2? */

	/* Copy authorization bit. */
	cdev_irb->esw.esw0.erw.auth = irb->esw.esw0.erw.auth;
	/* Copy path verification required flag. */
	cdev_irb->esw.esw0.erw.pvrf = irb->esw.esw0.erw.pvrf;
	if (irb->esw.esw0.erw.pvrf)
		cdev->private->flags.doverify = 1;
	/* Copy concurrent sense bit. */
	cdev_irb->esw.esw0.erw.cons = irb->esw.esw0.erw.cons;
	if (irb->esw.esw0.erw.cons)
		cdev_irb->esw.esw0.erw.scnt = irb->esw.esw0.erw.scnt;
}

/*
 * Accumulate status from irb to devstat.
 */
void
ccw_device_accumulate_irb(struct ccw_device *cdev, struct irb *irb)
{
	struct irb *cdev_irb;

	/*
	 * Check if the status pending bit is set in stctl.
	 * If not, the remaining bit have no meaning and we must ignore them.
	 * The esw is not meaningful as well...
	 */
	if (!(scsw_stctl(&irb->scsw) & SCSW_STCTL_STATUS_PEND))
		return;

	/* Check for channel checks and interface control checks. */
	ccw_device_msg_control_check(cdev, irb);

	/* Check for path not operational. */
	if (scsw_is_valid_pno(&irb->scsw) && scsw_pno(&irb->scsw))
		ccw_device_path_notoper(cdev);
	/* No irb accumulation for transport mode irbs. */
	if (scsw_is_tm(&irb->scsw)) {
		memcpy(&cdev->private->irb, irb, sizeof(struct irb));
		return;
	}
	/*
	 * Don't accumulate unsolicited interrupts.
	 */
	if (!scsw_is_solicited(&irb->scsw))
		return;

	cdev_irb = &cdev->private->irb;

	/*
	 * If the clear function had been performed, all formerly pending
	 * status at the subchannel has been cleared and we must not pass
	 * intermediate accumulated status to the device driver.
	 */
	if (irb->scsw.cmd.fctl & SCSW_FCTL_CLEAR_FUNC)
		memset(&cdev->private->irb, 0, sizeof(struct irb));

	/* Copy bits which are valid only for the start function. */
	if (irb->scsw.cmd.fctl & SCSW_FCTL_START_FUNC) {
		/* Copy key. */
		cdev_irb->scsw.cmd.key = irb->scsw.cmd.key;
		/* Copy suspend control bit. */
		cdev_irb->scsw.cmd.sctl = irb->scsw.cmd.sctl;
		/* Accumulate deferred condition code. */
		cdev_irb->scsw.cmd.cc |= irb->scsw.cmd.cc;
		/* Copy ccw format bit. */
		cdev_irb->scsw.cmd.fmt = irb->scsw.cmd.fmt;
		/* Copy prefetch bit. */
		cdev_irb->scsw.cmd.pfch = irb->scsw.cmd.pfch;
		/* Copy initial-status-interruption-control. */
		cdev_irb->scsw.cmd.isic = irb->scsw.cmd.isic;
		/* Copy address limit checking control. */
		cdev_irb->scsw.cmd.alcc = irb->scsw.cmd.alcc;
		/* Copy suppress suspend bit. */
		cdev_irb->scsw.cmd.ssi = irb->scsw.cmd.ssi;
	}

	/* Take care of the extended control bit and extended control word. */
	ccw_device_accumulate_ecw(cdev, irb);
	    
	/* Accumulate function control. */
	cdev_irb->scsw.cmd.fctl |= irb->scsw.cmd.fctl;
	/* Copy activity control. */
	cdev_irb->scsw.cmd.actl = irb->scsw.cmd.actl;
	/* Accumulate status control. */
	cdev_irb->scsw.cmd.stctl |= irb->scsw.cmd.stctl;
	/*
	 * Copy ccw address if it is valid. This is a bit simplified
	 * but should be close enough for all practical purposes.
	 */
	if ((irb->scsw.cmd.stctl & SCSW_STCTL_PRIM_STATUS) ||
	    ((irb->scsw.cmd.stctl ==
	      (SCSW_STCTL_INTER_STATUS|SCSW_STCTL_STATUS_PEND)) &&
	     (irb->scsw.cmd.actl & SCSW_ACTL_DEVACT) &&
	     (irb->scsw.cmd.actl & SCSW_ACTL_SCHACT)) ||
	    (irb->scsw.cmd.actl & SCSW_ACTL_SUSPENDED))
		cdev_irb->scsw.cmd.cpa = irb->scsw.cmd.cpa;
	/* Accumulate device status, but not the device busy flag. */
	cdev_irb->scsw.cmd.dstat &= ~DEV_STAT_BUSY;
	/* dstat is not always valid. */
	if (irb->scsw.cmd.stctl &
	    (SCSW_STCTL_PRIM_STATUS | SCSW_STCTL_SEC_STATUS
	     | SCSW_STCTL_INTER_STATUS | SCSW_STCTL_ALERT_STATUS))
		cdev_irb->scsw.cmd.dstat |= irb->scsw.cmd.dstat;
	/* Accumulate subchannel status. */
	cdev_irb->scsw.cmd.cstat |= irb->scsw.cmd.cstat;
	/* Copy residual count if it is valid. */
	if ((irb->scsw.cmd.stctl & SCSW_STCTL_PRIM_STATUS) &&
	    (irb->scsw.cmd.cstat & ~(SCHN_STAT_PCI | SCHN_STAT_INCORR_LEN))
	     == 0)
		cdev_irb->scsw.cmd.count = irb->scsw.cmd.count;

	/* Take care of bits in the extended status word. */
	ccw_device_accumulate_esw(cdev, irb);

	/*
	 * Check whether we must issue a SENSE CCW ourselves if there is no
	 * concurrent sense facility installed for the subchannel.
	 * No sense is required if no delayed sense is pending
	 * and we did not get a unit check without sense information.
	 *
	 * Note: We should check for ioinfo[irq]->flags.consns but VM
	 *	 violates the ESA/390 architecture and doesn't present an
	 *	 operand exception for virtual devices without concurrent
	 *	 sense facility available/supported when enabling the
	 *	 concurrent sense facility.
	 */
	if ((cdev_irb->scsw.cmd.dstat & DEV_STAT_UNIT_CHECK) &&
	    !(cdev_irb->esw.esw0.erw.cons))
		cdev->private->flags.dosense = 1;
}

/*
 * Do a basic sense.
 */
int
ccw_device_do_sense(struct ccw_device *cdev, struct irb *irb)
{
	struct subchannel *sch;
	struct ccw1 *sense_ccw;
	int rc;

	sch = to_subchannel(cdev->dev.parent);

	/* A sense is required, can we do it now ? */
	if (scsw_actl(&irb->scsw) & (SCSW_ACTL_DEVACT | SCSW_ACTL_SCHACT))
		/*
		 * we received an Unit Check but we have no final
		 *  status yet, therefore we must delay the SENSE
		 *  processing. We must not report this intermediate
		 *  status to the device interrupt handler.
		 */
		return -EBUSY;

	/*
	 * We have ending status but no sense information. Do a basic sense.
	 */
	sense_ccw = &to_io_private(sch)->sense_ccw;
	sense_ccw->cmd_code = CCW_CMD_BASIC_SENSE;
	sense_ccw->cda = (__u32) __pa(cdev->private->irb.ecw);
	sense_ccw->count = SENSE_MAX_COUNT;
	sense_ccw->flags = CCW_FLAG_SLI;

	rc = cio_start(sch, sense_ccw, 0xff);
	if (rc == -ENODEV || rc == -EACCES)
		dev_fsm_event(cdev, DEV_EVENT_VERIFY);
	return rc;
}

/*
 * Add information from basic sense to devstat.
 */
void
ccw_device_accumulate_basic_sense(struct ccw_device *cdev, struct irb *irb)
{
	/*
	 * Check if the status pending bit is set in stctl.
	 * If not, the remaining bit have no meaning and we must ignore them.
	 * The esw is not meaningful as well...
	 */
	if (!(scsw_stctl(&irb->scsw) & SCSW_STCTL_STATUS_PEND))
		return;

	/* Check for channel checks and interface control checks. */
	ccw_device_msg_control_check(cdev, irb);

	/* Check for path not operational. */
	if (scsw_is_valid_pno(&irb->scsw) && scsw_pno(&irb->scsw))
		ccw_device_path_notoper(cdev);

	if (!(irb->scsw.cmd.dstat & DEV_STAT_UNIT_CHECK) &&
	    (irb->scsw.cmd.dstat & DEV_STAT_CHN_END)) {
		cdev->private->irb.esw.esw0.erw.cons = 1;
		cdev->private->flags.dosense = 0;
	}
	/* Check if path verification is required. */
	if (ccw_device_accumulate_esw_valid(irb) &&
	    irb->esw.esw0.erw.pvrf)
		cdev->private->flags.doverify = 1;
}

/*
 * This function accumulates the status into the private devstat and
 * starts a basic sense if one is needed.
 */
int
ccw_device_accumulate_and_sense(struct ccw_device *cdev, struct irb *irb)
{
	ccw_device_accumulate_irb(cdev, irb);
	if ((irb->scsw.cmd.actl  & (SCSW_ACTL_DEVACT | SCSW_ACTL_SCHACT)) != 0)
		return -EBUSY;
	/* Check for basic sense. */
	if (cdev->private->flags.dosense &&
	    !(irb->scsw.cmd.dstat & DEV_STAT_UNIT_CHECK)) {
		cdev->private->irb.esw.esw0.erw.cons = 1;
		cdev->private->flags.dosense = 0;
		return 0;
	}
	if (cdev->private->flags.dosense) {
		ccw_device_do_sense(cdev, irb);
		return -EBUSY;
	}
	return 0;
}

