// SPDX-License-Identifier: GPL-2.0
/*
 *  Handling of internal CCW device requests.
 *
 *    Copyright IBM Corp. 2009, 2011
 *    Author(s): Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#define KMSG_COMPONENT "cio"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/types.h>
#include <linux/err.h>
#include <asm/ccwdev.h>
#include <asm/cio.h>

#include "io_sch.h"
#include "cio.h"
#include "device.h"
#include "cio_debug.h"

/**
 * lpm_adjust - adjust path mask
 * @lpm: path mask to adjust
 * @mask: mask of available paths
 *
 * Shift @lpm right until @lpm and @mask have at least one bit in common or
 * until @lpm is zero. Return the resulting lpm.
 */
int lpm_adjust(int lpm, int mask)
{
	while (lpm && ((lpm & mask) == 0))
		lpm >>= 1;
	return lpm;
}

/*
 * Adjust path mask to use next path and reset retry count. Return resulting
 * path mask.
 */
static u16 ccwreq_next_path(struct ccw_device *cdev)
{
	struct ccw_request *req = &cdev->private->req;

	if (!req->singlepath) {
		req->mask = 0;
		goto out;
	}
	req->retries	= req->maxretries;
	req->mask	= lpm_adjust(req->mask >> 1, req->lpm);
out:
	return req->mask;
}

/*
 * Clean up device state and report to callback.
 */
static void ccwreq_stop(struct ccw_device *cdev, int rc)
{
	struct ccw_request *req = &cdev->private->req;

	if (req->done)
		return;
	req->done = 1;
	ccw_device_set_timeout(cdev, 0);
	memset(&cdev->private->irb, 0, sizeof(struct irb));
	if (rc && rc != -ENODEV && req->drc)
		rc = req->drc;
	req->callback(cdev, req->data, rc);
}

/*
 * (Re-)Start the operation until retries and paths are exhausted.
 */
static void ccwreq_do(struct ccw_device *cdev)
{
	struct ccw_request *req = &cdev->private->req;
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw1 *cp = req->cp;
	int rc = -EACCES;

	while (req->mask) {
		if (req->retries-- == 0) {
			/* Retries exhausted, try next path. */
			ccwreq_next_path(cdev);
			continue;
		}
		/* Perform start function. */
		memset(&cdev->private->irb, 0, sizeof(struct irb));
		rc = cio_start(sch, cp, (u8) req->mask);
		if (rc == 0) {
			/* I/O started successfully. */
			ccw_device_set_timeout(cdev, req->timeout);
			return;
		}
		if (rc == -ENODEV) {
			/* Permanent device error. */
			break;
		}
		if (rc == -EACCES) {
			/* Permant path error. */
			ccwreq_next_path(cdev);
			continue;
		}
		/* Temporary improper status. */
		rc = cio_clear(sch);
		if (rc)
			break;
		return;
	}
	ccwreq_stop(cdev, rc);
}

/**
 * ccw_request_start - perform I/O request
 * @cdev: ccw device
 *
 * Perform the I/O request specified by cdev->req.
 */
void ccw_request_start(struct ccw_device *cdev)
{
	struct ccw_request *req = &cdev->private->req;

	if (req->singlepath) {
		/* Try all paths twice to counter link flapping. */
		req->mask = 0x8080;
	} else
		req->mask = req->lpm;

	req->retries	= req->maxretries;
	req->mask	= lpm_adjust(req->mask, req->lpm);
	req->drc	= 0;
	req->done	= 0;
	req->cancel	= 0;
	if (!req->mask)
		goto out_nopath;
	ccwreq_do(cdev);
	return;

out_nopath:
	ccwreq_stop(cdev, -EACCES);
}

/**
 * ccw_request_cancel - cancel running I/O request
 * @cdev: ccw device
 *
 * Cancel the I/O request specified by cdev->req. Return non-zero if request
 * has already finished, zero otherwise.
 */
int ccw_request_cancel(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_request *req = &cdev->private->req;
	int rc;

	if (req->done)
		return 1;
	req->cancel = 1;
	rc = cio_clear(sch);
	if (rc)
		ccwreq_stop(cdev, rc);
	return 0;
}

/*
 * Return the status of the internal I/O started on the specified ccw device.
 * Perform BASIC SENSE if required.
 */
static enum io_status ccwreq_status(struct ccw_device *cdev, struct irb *lcirb)
{
	struct irb *irb = &cdev->private->irb;
	struct cmd_scsw *scsw = &irb->scsw.cmd;
	enum uc_todo todo;

	/* Perform BASIC SENSE if needed. */
	if (ccw_device_accumulate_and_sense(cdev, lcirb))
		return IO_RUNNING;
	/* Check for halt/clear interrupt. */
	if (scsw->fctl & (SCSW_FCTL_HALT_FUNC | SCSW_FCTL_CLEAR_FUNC))
		return IO_KILLED;
	/* Check for path error. */
	if (scsw->cc == 3 || scsw->pno)
		return IO_PATH_ERROR;
	/* Handle BASIC SENSE data. */
	if (irb->esw.esw0.erw.cons) {
		CIO_TRACE_EVENT(2, "sensedata");
		CIO_HEX_EVENT(2, &cdev->private->dev_id,
			      sizeof(struct ccw_dev_id));
		CIO_HEX_EVENT(2, &cdev->private->irb.ecw, SENSE_MAX_COUNT);
		/* Check for command reject. */
		if (irb->ecw[0] & SNS0_CMD_REJECT)
			return IO_REJECTED;
		/* Ask the driver what to do */
		if (cdev->drv && cdev->drv->uc_handler) {
			todo = cdev->drv->uc_handler(cdev, lcirb);
			CIO_TRACE_EVENT(2, "uc_response");
			CIO_HEX_EVENT(2, &todo, sizeof(todo));
			switch (todo) {
			case UC_TODO_RETRY:
				return IO_STATUS_ERROR;
			case UC_TODO_RETRY_ON_NEW_PATH:
				return IO_PATH_ERROR;
			case UC_TODO_STOP:
				return IO_REJECTED;
			default:
				return IO_STATUS_ERROR;
			}
		}
		/* Assume that unexpected SENSE data implies an error. */
		return IO_STATUS_ERROR;
	}
	/* Check for channel errors. */
	if (scsw->cstat != 0)
		return IO_STATUS_ERROR;
	/* Check for device errors. */
	if (scsw->dstat & ~(DEV_STAT_CHN_END | DEV_STAT_DEV_END))
		return IO_STATUS_ERROR;
	/* Check for final state. */
	if (!(scsw->dstat & DEV_STAT_DEV_END))
		return IO_RUNNING;
	/* Check for other improper status. */
	if (scsw->cc == 1 && (scsw->stctl & SCSW_STCTL_ALERT_STATUS))
		return IO_STATUS_ERROR;
	return IO_DONE;
}

/*
 * Log ccw request status.
 */
static void ccwreq_log_status(struct ccw_device *cdev, enum io_status status)
{
	struct ccw_request *req = &cdev->private->req;
	struct {
		struct ccw_dev_id dev_id;
		u16 retries;
		u8 lpm;
		u8 status;
	}  __attribute__ ((packed)) data;
	data.dev_id	= cdev->private->dev_id;
	data.retries	= req->retries;
	data.lpm	= (u8) req->mask;
	data.status	= (u8) status;
	CIO_TRACE_EVENT(2, "reqstat");
	CIO_HEX_EVENT(2, &data, sizeof(data));
}

/**
 * ccw_request_handler - interrupt handler for I/O request procedure.
 * @cdev: ccw device
 *
 * Handle interrupt during I/O request procedure.
 */
void ccw_request_handler(struct ccw_device *cdev)
{
	struct irb *irb = this_cpu_ptr(&cio_irb);
	struct ccw_request *req = &cdev->private->req;
	enum io_status status;
	int rc = -EOPNOTSUPP;

	/* Check status of I/O request. */
	status = ccwreq_status(cdev, irb);
	if (req->filter)
		status = req->filter(cdev, req->data, irb, status);
	if (status != IO_RUNNING)
		ccw_device_set_timeout(cdev, 0);
	if (status != IO_DONE && status != IO_RUNNING)
		ccwreq_log_status(cdev, status);
	switch (status) {
	case IO_DONE:
		break;
	case IO_RUNNING:
		return;
	case IO_REJECTED:
		goto err;
	case IO_PATH_ERROR:
		goto out_next_path;
	case IO_STATUS_ERROR:
		goto out_restart;
	case IO_KILLED:
		/* Check if request was cancelled on purpose. */
		if (req->cancel) {
			rc = -EIO;
			goto err;
		}
		goto out_restart;
	}
	/* Check back with request initiator. */
	if (!req->check)
		goto out;
	switch (req->check(cdev, req->data)) {
	case 0:
		break;
	case -EAGAIN:
		goto out_restart;
	case -EACCES:
		goto out_next_path;
	default:
		goto err;
	}
out:
	ccwreq_stop(cdev, 0);
	return;

out_next_path:
	/* Try next path and restart I/O. */
	if (!ccwreq_next_path(cdev)) {
		rc = -EACCES;
		goto err;
	}
out_restart:
	/* Restart. */
	ccwreq_do(cdev);
	return;
err:
	ccwreq_stop(cdev, rc);
}


/**
 * ccw_request_timeout - timeout handler for I/O request procedure
 * @cdev: ccw device
 *
 * Handle timeout during I/O request procedure.
 */
void ccw_request_timeout(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_request *req = &cdev->private->req;
	int rc = -ENODEV, chp;

	if (cio_update_schib(sch))
		goto err;

	for (chp = 0; chp < 8; chp++) {
		if ((0x80 >> chp) & sch->schib.pmcw.lpum)
			pr_warn("%s: No interrupt was received within %lus (CS=%02x, DS=%02x, CHPID=%x.%02x)\n",
				dev_name(&cdev->dev), req->timeout / HZ,
				scsw_cstat(&sch->schib.scsw),
				scsw_dstat(&sch->schib.scsw),
				sch->schid.cssid,
				sch->schib.pmcw.chpid[chp]);
	}

	if (!ccwreq_next_path(cdev)) {
		/* set the final return code for this request */
		req->drc = -ETIME;
	}
	rc = cio_clear(sch);
	if (rc)
		goto err;
	return;

err:
	ccwreq_stop(cdev, rc);
}

/**
 * ccw_request_notoper - notoper handler for I/O request procedure
 * @cdev: ccw device
 *
 * Handle notoper during I/O request procedure.
 */
void ccw_request_notoper(struct ccw_device *cdev)
{
	ccwreq_stop(cdev, -ENODEV);
}
