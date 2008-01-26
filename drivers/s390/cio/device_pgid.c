/*
 * drivers/s390/cio/device_pgid.c
 *
 *    Copyright (C) 2002 IBM Deutschland Entwicklung GmbH,
 *			 IBM Corporation
 *    Author(s): Cornelia Huck (cornelia.huck@de.ibm.com)
 *		 Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 * Path Group ID functions.
 */

#include <linux/module.h>
#include <linux/init.h>

#include <asm/ccwdev.h>
#include <asm/cio.h>
#include <asm/delay.h>
#include <asm/lowcore.h>

#include "cio.h"
#include "cio_debug.h"
#include "css.h"
#include "device.h"
#include "ioasm.h"
#include "io_sch.h"

/*
 * Helper function called from interrupt context to decide whether an
 * operation should be tried again.
 */
static int __ccw_device_should_retry(struct scsw *scsw)
{
	/* CC is only valid if start function bit is set. */
	if ((scsw->fctl & SCSW_FCTL_START_FUNC) && scsw->cc == 1)
		return 1;
	/* No more activity. For sense and set PGID we stubbornly try again. */
	if (!scsw->actl)
		return 1;
	return 0;
}

/*
 * Start Sense Path Group ID helper function. Used in ccw_device_recog
 * and ccw_device_sense_pgid.
 */
static int
__ccw_device_sense_pgid_start(struct ccw_device *cdev)
{
	struct subchannel *sch;
	struct ccw1 *ccw;
	int ret;
	int i;

	sch = to_subchannel(cdev->dev.parent);
	/* Return if we already checked on all paths. */
	if (cdev->private->imask == 0)
		return (sch->lpm == 0) ? -ENODEV : -EACCES;
	i = 8 - ffs(cdev->private->imask);

	/* Setup sense path group id channel program. */
	ccw = cdev->private->iccws;
	ccw->cmd_code = CCW_CMD_SENSE_PGID;
	ccw->count = sizeof (struct pgid);
	ccw->flags = CCW_FLAG_SLI;

	/* Reset device status. */
	memset(&cdev->private->irb, 0, sizeof(struct irb));
	/* Try on every path. */
	ret = -ENODEV;
	while (cdev->private->imask != 0) {
		/* Try every path multiple times. */
		ccw->cda = (__u32) __pa (&cdev->private->pgid[i]);
		if (cdev->private->iretry > 0) {
			cdev->private->iretry--;
			/* Reset internal retry indication. */
			cdev->private->flags.intretry = 0;
			ret = cio_start (sch, cdev->private->iccws, 
					 cdev->private->imask);
			/* ret is 0, -EBUSY, -EACCES or -ENODEV */
			if (ret != -EACCES)
				return ret;
			CIO_MSG_EVENT(2, "SNID - Device %04x on Subchannel "
				      "0.%x.%04x, lpm %02X, became 'not "
				      "operational'\n",
				      cdev->private->dev_id.devno,
				      sch->schid.ssid,
				      sch->schid.sch_no, cdev->private->imask);

		}
		cdev->private->imask >>= 1;
		cdev->private->iretry = 5;
		i++;
	}

	return ret;
}

void
ccw_device_sense_pgid_start(struct ccw_device *cdev)
{
	int ret;

	/* Set a timeout of 60s */
	ccw_device_set_timeout(cdev, 60*HZ);

	cdev->private->state = DEV_STATE_SENSE_PGID;
	cdev->private->imask = 0x80;
	cdev->private->iretry = 5;
	memset (&cdev->private->pgid, 0, sizeof (cdev->private->pgid));
	ret = __ccw_device_sense_pgid_start(cdev);
	if (ret && ret != -EBUSY)
		ccw_device_sense_pgid_done(cdev, ret);
}

/*
 * Called from interrupt context to check if a valid answer
 * to Sense Path Group ID was received.
 */
static int
__ccw_device_check_sense_pgid(struct ccw_device *cdev)
{
	struct subchannel *sch;
	struct irb *irb;
	int i;

	sch = to_subchannel(cdev->dev.parent);
	irb = &cdev->private->irb;
	if (irb->scsw.fctl & (SCSW_FCTL_HALT_FUNC | SCSW_FCTL_CLEAR_FUNC)) {
		/* Retry Sense PGID if requested. */
		if (cdev->private->flags.intretry) {
			cdev->private->flags.intretry = 0;
			return -EAGAIN;
		}
		return -ETIME;
	}
	if (irb->esw.esw0.erw.cons &&
	    (irb->ecw[0]&(SNS0_CMD_REJECT|SNS0_INTERVENTION_REQ))) {
		/*
		 * If the device doesn't support the Sense Path Group ID
		 *  command further retries wouldn't help ...
		 */
		return -EOPNOTSUPP;
	}
	if (irb->esw.esw0.erw.cons) {
		CIO_MSG_EVENT(2, "SNID - device 0.%x.%04x, unit check, "
			      "lpum %02X, cnt %02d, sns : "
			      "%02X%02X%02X%02X %02X%02X%02X%02X ...\n",
			      cdev->private->dev_id.ssid,
			      cdev->private->dev_id.devno,
			      irb->esw.esw0.sublog.lpum,
			      irb->esw.esw0.erw.scnt,
			      irb->ecw[0], irb->ecw[1],
			      irb->ecw[2], irb->ecw[3],
			      irb->ecw[4], irb->ecw[5],
			      irb->ecw[6], irb->ecw[7]);
		return -EAGAIN;
	}
	if (irb->scsw.cc == 3) {
		u8 lpm;

		lpm = to_io_private(sch)->orb.lpm;
		CIO_MSG_EVENT(2, "SNID - Device %04x on Subchannel 0.%x.%04x,"
			      " lpm %02X, became 'not operational'\n",
			      cdev->private->dev_id.devno, sch->schid.ssid,
			      sch->schid.sch_no, lpm);
		return -EACCES;
	}
	i = 8 - ffs(cdev->private->imask);
	if (cdev->private->pgid[i].inf.ps.state2 == SNID_STATE2_RESVD_ELSE) {
		CIO_MSG_EVENT(2, "SNID - Device %04x on Subchannel 0.%x.%04x "
			      "is reserved by someone else\n",
			      cdev->private->dev_id.devno, sch->schid.ssid,
			      sch->schid.sch_no);
		return -EUSERS;
	}
	return 0;
}

/*
 * Got interrupt for Sense Path Group ID.
 */
void
ccw_device_sense_pgid_irq(struct ccw_device *cdev, enum dev_event dev_event)
{
	struct subchannel *sch;
	struct irb *irb;
	int ret;

	irb = (struct irb *) __LC_IRB;

	if (irb->scsw.stctl ==
	    (SCSW_STCTL_STATUS_PEND | SCSW_STCTL_ALERT_STATUS)) {
		if (__ccw_device_should_retry(&irb->scsw)) {
			ret = __ccw_device_sense_pgid_start(cdev);
			if (ret && ret != -EBUSY)
				ccw_device_sense_pgid_done(cdev, ret);
		}
		return;
	}
	if (ccw_device_accumulate_and_sense(cdev, irb) != 0)
		return;
	sch = to_subchannel(cdev->dev.parent);
	ret = __ccw_device_check_sense_pgid(cdev);
	memset(&cdev->private->irb, 0, sizeof(struct irb));
	switch (ret) {
	/* 0, -ETIME, -EOPNOTSUPP, -EAGAIN, -EACCES or -EUSERS */
	case -EOPNOTSUPP:	/* Sense Path Group ID not supported */
		ccw_device_sense_pgid_done(cdev, -EOPNOTSUPP);
		break;
	case -ETIME:		/* Sense path group id stopped by timeout. */
		ccw_device_sense_pgid_done(cdev, -ETIME);
		break;
	case -EACCES:		/* channel is not operational. */
		sch->lpm &= ~cdev->private->imask;
		/* Fall through. */
	case 0:			/* Sense Path Group ID successful. */
		cdev->private->imask >>= 1;
		cdev->private->iretry = 5;
		/* Fall through. */
	case -EAGAIN:		/* Try again. */
		ret = __ccw_device_sense_pgid_start(cdev);
		if (ret != 0 && ret != -EBUSY)
			ccw_device_sense_pgid_done(cdev, ret);
		break;
	case -EUSERS:		/* device is reserved for someone else. */
		ccw_device_sense_pgid_done(cdev, -EUSERS);
		break;
	}
}

/*
 * Path Group ID helper function.
 */
static int
__ccw_device_do_pgid(struct ccw_device *cdev, __u8 func)
{
	struct subchannel *sch;
	struct ccw1 *ccw;
	int ret;

	sch = to_subchannel(cdev->dev.parent);

	/* Setup sense path group id channel program. */
	cdev->private->pgid[0].inf.fc = func;
	ccw = cdev->private->iccws;
	if (!cdev->private->flags.pgid_single) {
		cdev->private->pgid[0].inf.fc |= SPID_FUNC_MULTI_PATH;
		ccw->cmd_code = CCW_CMD_SUSPEND_RECONN;
		ccw->cda = 0;
		ccw->count = 0;
		ccw->flags = CCW_FLAG_SLI | CCW_FLAG_CC;
		ccw++;
	} else
		cdev->private->pgid[0].inf.fc |= SPID_FUNC_SINGLE_PATH;

	ccw->cmd_code = CCW_CMD_SET_PGID;
	ccw->cda = (__u32) __pa (&cdev->private->pgid[0]);
	ccw->count = sizeof (struct pgid);
	ccw->flags = CCW_FLAG_SLI;

	/* Reset device status. */
	memset(&cdev->private->irb, 0, sizeof(struct irb));

	/* Try multiple times. */
	ret = -EACCES;
	if (cdev->private->iretry > 0) {
		cdev->private->iretry--;
		/* Reset internal retry indication. */
		cdev->private->flags.intretry = 0;
		ret = cio_start (sch, cdev->private->iccws,
				 cdev->private->imask);
		/* We expect an interrupt in case of success or busy
		 * indication. */
		if ((ret == 0) || (ret == -EBUSY))
			return ret;
	}
	/* PGID command failed on this path. */
	CIO_MSG_EVENT(2, "SPID - Device %04x on Subchannel "
		      "0.%x.%04x, lpm %02X, became 'not operational'\n",
		      cdev->private->dev_id.devno, sch->schid.ssid,
		      sch->schid.sch_no, cdev->private->imask);
	return ret;
}

/*
 * Helper function to send a nop ccw down a path.
 */
static int __ccw_device_do_nop(struct ccw_device *cdev)
{
	struct subchannel *sch;
	struct ccw1 *ccw;
	int ret;

	sch = to_subchannel(cdev->dev.parent);

	/* Setup nop channel program. */
	ccw = cdev->private->iccws;
	ccw->cmd_code = CCW_CMD_NOOP;
	ccw->cda = 0;
	ccw->count = 0;
	ccw->flags = CCW_FLAG_SLI;

	/* Reset device status. */
	memset(&cdev->private->irb, 0, sizeof(struct irb));

	/* Try multiple times. */
	ret = -EACCES;
	if (cdev->private->iretry > 0) {
		cdev->private->iretry--;
		/* Reset internal retry indication. */
		cdev->private->flags.intretry = 0;
		ret = cio_start (sch, cdev->private->iccws,
				 cdev->private->imask);
		/* We expect an interrupt in case of success or busy
		 * indication. */
		if ((ret == 0) || (ret == -EBUSY))
			return ret;
	}
	/* nop command failed on this path. */
	CIO_MSG_EVENT(2, "NOP - Device %04x on Subchannel "
		      "0.%x.%04x, lpm %02X, became 'not operational'\n",
		      cdev->private->dev_id.devno, sch->schid.ssid,
		      sch->schid.sch_no, cdev->private->imask);
	return ret;
}


/*
 * Called from interrupt context to check if a valid answer
 * to Set Path Group ID was received.
 */
static int
__ccw_device_check_pgid(struct ccw_device *cdev)
{
	struct subchannel *sch;
	struct irb *irb;

	sch = to_subchannel(cdev->dev.parent);
	irb = &cdev->private->irb;
	if (irb->scsw.fctl & (SCSW_FCTL_HALT_FUNC | SCSW_FCTL_CLEAR_FUNC)) {
		/* Retry Set PGID if requested. */
		if (cdev->private->flags.intretry) {
			cdev->private->flags.intretry = 0;
			return -EAGAIN;
		}
		return -ETIME;
	}
	if (irb->esw.esw0.erw.cons) {
		if (irb->ecw[0] & SNS0_CMD_REJECT)
			return -EOPNOTSUPP;
		/* Hmm, whatever happened, try again. */
		CIO_MSG_EVENT(2, "SPID - device 0.%x.%04x, unit check, "
			      "cnt %02d, "
			      "sns : %02X%02X%02X%02X %02X%02X%02X%02X ...\n",
			      cdev->private->dev_id.ssid,
			      cdev->private->dev_id.devno,
			      irb->esw.esw0.erw.scnt,
			      irb->ecw[0], irb->ecw[1],
			      irb->ecw[2], irb->ecw[3],
			      irb->ecw[4], irb->ecw[5],
			      irb->ecw[6], irb->ecw[7]);
		return -EAGAIN;
	}
	if (irb->scsw.cc == 3) {
		CIO_MSG_EVENT(2, "SPID - Device %04x on Subchannel 0.%x.%04x,"
			      " lpm %02X, became 'not operational'\n",
			      cdev->private->dev_id.devno, sch->schid.ssid,
			      sch->schid.sch_no, cdev->private->imask);
		return -EACCES;
	}
	return 0;
}

/*
 * Called from interrupt context to check the path status after a nop has
 * been send.
 */
static int __ccw_device_check_nop(struct ccw_device *cdev)
{
	struct subchannel *sch;
	struct irb *irb;

	sch = to_subchannel(cdev->dev.parent);
	irb = &cdev->private->irb;
	if (irb->scsw.fctl & (SCSW_FCTL_HALT_FUNC | SCSW_FCTL_CLEAR_FUNC)) {
		/* Retry NOP if requested. */
		if (cdev->private->flags.intretry) {
			cdev->private->flags.intretry = 0;
			return -EAGAIN;
		}
		return -ETIME;
	}
	if (irb->scsw.cc == 3) {
		CIO_MSG_EVENT(2, "NOP - Device %04x on Subchannel 0.%x.%04x,"
			      " lpm %02X, became 'not operational'\n",
			      cdev->private->dev_id.devno, sch->schid.ssid,
			      sch->schid.sch_no, cdev->private->imask);
		return -EACCES;
	}
	return 0;
}

static void
__ccw_device_verify_start(struct ccw_device *cdev)
{
	struct subchannel *sch;
	__u8 func;
	int ret;

	sch = to_subchannel(cdev->dev.parent);
	/* Repeat for all paths. */
	for (; cdev->private->imask; cdev->private->imask >>= 1,
				     cdev->private->iretry = 5) {
		if ((cdev->private->imask & sch->schib.pmcw.pam) == 0)
			/* Path not available, try next. */
			continue;
		if (cdev->private->options.pgroup) {
			if (sch->opm & cdev->private->imask)
				func = SPID_FUNC_ESTABLISH;
			else
				func = SPID_FUNC_RESIGN;
			ret = __ccw_device_do_pgid(cdev, func);
		} else
			ret = __ccw_device_do_nop(cdev);
		/* We expect an interrupt in case of success or busy
		 * indication. */
		if (ret == 0 || ret == -EBUSY)
			return;
		/* Permanent path failure, try next. */
	}
	/* Done with all paths. */
	ccw_device_verify_done(cdev, (sch->vpm != 0) ? 0 : -ENODEV);
}
		
/*
 * Got interrupt for Set Path Group ID.
 */
void
ccw_device_verify_irq(struct ccw_device *cdev, enum dev_event dev_event)
{
	struct subchannel *sch;
	struct irb *irb;
	int ret;

	irb = (struct irb *) __LC_IRB;

	if (irb->scsw.stctl ==
	    (SCSW_STCTL_STATUS_PEND | SCSW_STCTL_ALERT_STATUS)) {
		if (__ccw_device_should_retry(&irb->scsw))
			__ccw_device_verify_start(cdev);
		return;
	}
	if (ccw_device_accumulate_and_sense(cdev, irb) != 0)
		return;
	sch = to_subchannel(cdev->dev.parent);
	if (cdev->private->options.pgroup)
		ret = __ccw_device_check_pgid(cdev);
	else
		ret = __ccw_device_check_nop(cdev);
	memset(&cdev->private->irb, 0, sizeof(struct irb));

	switch (ret) {
	/* 0, -ETIME, -EAGAIN, -EOPNOTSUPP or -EACCES */
	case 0:
		/* Path verification ccw finished successfully, update lpm. */
		sch->vpm |= sch->opm & cdev->private->imask;
		/* Go on with next path. */
		cdev->private->imask >>= 1;
		cdev->private->iretry = 5;
		__ccw_device_verify_start(cdev);
		break;
	case -EOPNOTSUPP:
		/*
		 * One of those strange devices which claim to be able
		 * to do multipathing but not for Set Path Group ID.
		 */
		if (cdev->private->flags.pgid_single)
			cdev->private->options.pgroup = 0;
		else
			cdev->private->flags.pgid_single = 1;
		/* Retry */
		sch->vpm = 0;
		cdev->private->imask = 0x80;
		cdev->private->iretry = 5;
		/* fall through. */
	case -EAGAIN:		/* Try again. */
		__ccw_device_verify_start(cdev);
		break;
	case -ETIME:		/* Set path group id stopped by timeout. */
		ccw_device_verify_done(cdev, -ETIME);
		break;
	case -EACCES:		/* channel is not operational. */
		cdev->private->imask >>= 1;
		cdev->private->iretry = 5;
		__ccw_device_verify_start(cdev);
		break;
	}
}

void
ccw_device_verify_start(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);

	cdev->private->flags.pgid_single = 0;
	cdev->private->imask = 0x80;
	cdev->private->iretry = 5;

	/* Start with empty vpm. */
	sch->vpm = 0;

	/* Get current pam. */
	if (stsch(sch->schid, &sch->schib)) {
		ccw_device_verify_done(cdev, -ENODEV);
		return;
	}
	/* After 60s path verification is considered to have failed. */
	ccw_device_set_timeout(cdev, 60*HZ);
	__ccw_device_verify_start(cdev);
}

static void
__ccw_device_disband_start(struct ccw_device *cdev)
{
	struct subchannel *sch;
	int ret;

	sch = to_subchannel(cdev->dev.parent);
	while (cdev->private->imask != 0) {
		if (sch->lpm & cdev->private->imask) {
			ret = __ccw_device_do_pgid(cdev, SPID_FUNC_DISBAND);
			if (ret == 0)
				return;
		}
		cdev->private->iretry = 5;
		cdev->private->imask >>= 1;
	}
	ccw_device_disband_done(cdev, (sch->lpm != 0) ? 0 : -ENODEV);
}

/*
 * Got interrupt for Unset Path Group ID.
 */
void
ccw_device_disband_irq(struct ccw_device *cdev, enum dev_event dev_event)
{
	struct subchannel *sch;
	struct irb *irb;
	int ret;

	irb = (struct irb *) __LC_IRB;

	if (irb->scsw.stctl ==
	    (SCSW_STCTL_STATUS_PEND | SCSW_STCTL_ALERT_STATUS)) {
		if (__ccw_device_should_retry(&irb->scsw))
			__ccw_device_disband_start(cdev);
		return;
	}
	if (ccw_device_accumulate_and_sense(cdev, irb) != 0)
		return;
	sch = to_subchannel(cdev->dev.parent);
	ret = __ccw_device_check_pgid(cdev);
	memset(&cdev->private->irb, 0, sizeof(struct irb));
	switch (ret) {
	/* 0, -ETIME, -EAGAIN, -EOPNOTSUPP or -EACCES */
	case 0:			/* disband successful. */
		ccw_device_disband_done(cdev, ret);
		break;
	case -EOPNOTSUPP:
		/*
		 * One of those strange devices which claim to be able
		 * to do multipathing but not for Unset Path Group ID.
		 */
		cdev->private->flags.pgid_single = 1;
		/* fall through. */
	case -EAGAIN:		/* Try again. */
		__ccw_device_disband_start(cdev);
		break;
	case -ETIME:		/* Set path group id stopped by timeout. */
		ccw_device_disband_done(cdev, -ETIME);
		break;
	case -EACCES:		/* channel is not operational. */
		cdev->private->imask >>= 1;
		cdev->private->iretry = 5;
		__ccw_device_disband_start(cdev);
		break;
	}
}

void
ccw_device_disband_start(struct ccw_device *cdev)
{
	/* After 60s disbanding is considered to have failed. */
	ccw_device_set_timeout(cdev, 60*HZ);

	cdev->private->flags.pgid_single = 0;
	cdev->private->iretry = 5;
	cdev->private->imask = 0x80;
	__ccw_device_disband_start(cdev);
}
