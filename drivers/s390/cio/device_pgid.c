/*
 *  CCW device PGID and path verification I/O handling.
 *
 *    Copyright IBM Corp. 2002,2009
 *    Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 *		 Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/bitops.h>
#include <asm/ccwdev.h>
#include <asm/cio.h>

#include "cio.h"
#include "cio_debug.h"
#include "device.h"
#include "io_sch.h"

#define PGID_RETRIES	5
#define PGID_TIMEOUT	(10 * HZ)

/*
 * Process path verification data and report result.
 */
static void verify_done(struct ccw_device *cdev, int rc)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_dev_id *id = &cdev->private->dev_id;
	int mpath = !cdev->private->flags.pgid_single;
	int pgroup = cdev->private->options.pgroup;

	if (rc)
		goto out;
	/* Ensure consistent multipathing state at device and channel. */
	if (sch->config.mp != mpath) {
		sch->config.mp = mpath;
		rc = cio_commit_config(sch);
	}
out:
	CIO_MSG_EVENT(2, "vrfy: device 0.%x.%04x: rc=%d pgroup=%d mpath=%d "
			 "vpm=%02x\n", id->ssid, id->devno, rc, pgroup, mpath,
			 sch->vpm);
	ccw_device_verify_done(cdev, rc);
}

/*
 * Create channel program to perform a NOOP.
 */
static void nop_build_cp(struct ccw_device *cdev)
{
	struct ccw_request *req = &cdev->private->req;
	struct ccw1 *cp = cdev->private->iccws;

	cp->cmd_code	= CCW_CMD_NOOP;
	cp->cda		= 0;
	cp->count	= 0;
	cp->flags	= CCW_FLAG_SLI;
	req->cp		= cp;
}

/*
 * Perform NOOP on a single path.
 */
static void nop_do(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_request *req = &cdev->private->req;

	/* Adjust lpm. */
	req->lpm = lpm_adjust(req->lpm, sch->schib.pmcw.pam & sch->opm);
	if (!req->lpm)
		goto out_nopath;
	nop_build_cp(cdev);
	ccw_request_start(cdev);
	return;

out_nopath:
	verify_done(cdev, sch->vpm ? 0 : -EACCES);
}

/*
 * Adjust NOOP I/O status.
 */
static enum io_status nop_filter(struct ccw_device *cdev, void *data,
				 struct irb *irb, enum io_status status)
{
	/* Only subchannel status might indicate a path error. */
	if (status == IO_STATUS_ERROR && irb->scsw.cmd.cstat == 0)
		return IO_DONE;
	return status;
}

/*
 * Process NOOP request result for a single path.
 */
static void nop_callback(struct ccw_device *cdev, void *data, int rc)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_request *req = &cdev->private->req;

	if (rc == 0)
		sch->vpm |= req->lpm;
	else if (rc != -EACCES)
		goto err;
	req->lpm >>= 1;
	nop_do(cdev);
	return;

err:
	verify_done(cdev, rc);
}

/*
 * Create channel program to perform SET PGID on a single path.
 */
static void spid_build_cp(struct ccw_device *cdev, u8 fn)
{
	struct ccw_request *req = &cdev->private->req;
	struct ccw1 *cp = cdev->private->iccws;
	int i = 8 - ffs(req->lpm);
	struct pgid *pgid = &cdev->private->pgid[i];

	pgid->inf.fc	= fn;
	cp->cmd_code	= CCW_CMD_SET_PGID;
	cp->cda		= (u32) (addr_t) pgid;
	cp->count	= sizeof(*pgid);
	cp->flags	= CCW_FLAG_SLI;
	req->cp		= cp;
}

/*
 * Perform establish/resign SET PGID on a single path.
 */
static void spid_do(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_request *req = &cdev->private->req;
	u8 fn;

	/* Adjust lpm if paths are not set in pam. */
	req->lpm = lpm_adjust(req->lpm, sch->schib.pmcw.pam);
	if (!req->lpm)
		goto out_nopath;
	/* Channel program setup. */
	if (req->lpm & sch->opm)
		fn = SPID_FUNC_ESTABLISH;
	else
		fn = SPID_FUNC_RESIGN;
	if (!cdev->private->flags.pgid_single)
		fn |= SPID_FUNC_MULTI_PATH;
	spid_build_cp(cdev, fn);
	ccw_request_start(cdev);
	return;

out_nopath:
	verify_done(cdev, sch->vpm ? 0 : -EACCES);
}

static void verify_start(struct ccw_device *cdev);

/*
 * Process SET PGID request result for a single path.
 */
static void spid_callback(struct ccw_device *cdev, void *data, int rc)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_request *req = &cdev->private->req;

	switch (rc) {
	case 0:
		sch->vpm |= req->lpm & sch->opm;
		break;
	case -EACCES:
		break;
	case -EOPNOTSUPP:
		if (!cdev->private->flags.pgid_single) {
			/* Try without multipathing. */
			cdev->private->flags.pgid_single = 1;
			goto out_restart;
		}
		/* Try without pathgrouping. */
		cdev->private->options.pgroup = 0;
		goto out_restart;
	default:
		goto err;
	}
	req->lpm >>= 1;
	spid_do(cdev);
	return;

out_restart:
	verify_start(cdev);
	return;
err:
	verify_done(cdev, rc);
}

static int pgid_cmp(struct pgid *p1, struct pgid *p2)
{
	return memcmp((char *) p1 + 1, (char *) p2 + 1,
		      sizeof(struct pgid) - 1);
}

/*
 * Determine pathgroup state from PGID data.
 */
static void pgid_analyze(struct ccw_device *cdev, struct pgid **p,
			 int *mismatch, int *reserved, int *reset)
{
	struct pgid *pgid = &cdev->private->pgid[0];
	struct pgid *first = NULL;
	int lpm;
	int i;

	*mismatch = 0;
	*reserved = 0;
	*reset = 0;
	for (i = 0, lpm = 0x80; i < 8; i++, pgid++, lpm >>= 1) {
		if ((cdev->private->pgid_valid_mask & lpm) == 0)
			continue;
		if (pgid->inf.ps.state2 == SNID_STATE2_RESVD_ELSE)
			*reserved = 1;
		if (pgid->inf.ps.state1 == SNID_STATE1_RESET) {
			/* A PGID was reset. */
			*reset = 1;
			continue;
		}
		if (!first) {
			first = pgid;
			continue;
		}
		if (pgid_cmp(pgid, first) != 0)
			*mismatch = 1;
	}
	if (!first)
		first = &channel_subsystems[0]->global_pgid;
	*p = first;
}

static void pgid_fill(struct ccw_device *cdev, struct pgid *pgid)
{
	int i;

	for (i = 0; i < 8; i++)
		memcpy(&cdev->private->pgid[i], pgid, sizeof(struct pgid));
}

/*
 * Process SENSE PGID data and report result.
 */
static void snid_done(struct ccw_device *cdev, int rc)
{
	struct ccw_dev_id *id = &cdev->private->dev_id;
	struct pgid *pgid;
	int mismatch = 0;
	int reserved = 0;
	int reset = 0;

	if (rc)
		goto out;
	pgid_analyze(cdev, &pgid, &mismatch, &reserved, &reset);
	if (!mismatch) {
		pgid_fill(cdev, pgid);
		cdev->private->flags.pgid_rdy = 1;
	}
	if (reserved)
		rc = -EUSERS;
out:
	CIO_MSG_EVENT(2, "snid: device 0.%x.%04x: rc=%d pvm=%02x mism=%d "
		      "rsvd=%d reset=%d\n", id->ssid, id->devno, rc,
		      cdev->private->pgid_valid_mask, mismatch, reserved,
		      reset);
	ccw_device_sense_pgid_done(cdev, rc);
}

/*
 * Create channel program to perform a SENSE PGID on a single path.
 */
static void snid_build_cp(struct ccw_device *cdev)
{
	struct ccw_request *req = &cdev->private->req;
	struct ccw1 *cp = cdev->private->iccws;
	int i = 8 - ffs(req->lpm);

	/* Channel program setup. */
	cp->cmd_code	= CCW_CMD_SENSE_PGID;
	cp->cda		= (u32) (addr_t) &cdev->private->pgid[i];
	cp->count	= sizeof(struct pgid);
	cp->flags	= CCW_FLAG_SLI;
	req->cp		= cp;
}

/*
 * Perform SENSE PGID on a single path.
 */
static void snid_do(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_request *req = &cdev->private->req;

	/* Adjust lpm if paths are not set in pam. */
	req->lpm = lpm_adjust(req->lpm, sch->schib.pmcw.pam);
	if (!req->lpm)
		goto out_nopath;
	snid_build_cp(cdev);
	ccw_request_start(cdev);
	return;

out_nopath:
	snid_done(cdev, cdev->private->pgid_valid_mask ? 0 : -EACCES);
}

/*
 * Process SENSE PGID request result for single path.
 */
static void snid_callback(struct ccw_device *cdev, void *data, int rc)
{
	struct ccw_request *req = &cdev->private->req;

	if (rc == 0)
		cdev->private->pgid_valid_mask |= req->lpm;
	else if (rc != -EACCES)
		goto err;
	req->lpm >>= 1;
	snid_do(cdev);
	return;

err:
	snid_done(cdev, rc);
}

/**
 * ccw_device_sense_pgid_start - perform SENSE PGID
 * @cdev: ccw device
 *
 * Execute a SENSE PGID channel program on each path to @cdev to update its
 * PGID information. When finished, call ccw_device_sense_id_done with a
 * return code specifying the result.
 */
void ccw_device_sense_pgid_start(struct ccw_device *cdev)
{
	struct ccw_request *req = &cdev->private->req;

	CIO_TRACE_EVENT(4, "snid");
	CIO_HEX_EVENT(4, &cdev->private->dev_id, sizeof(cdev->private->dev_id));
	/* Initialize PGID data. */
	memset(cdev->private->pgid, 0, sizeof(cdev->private->pgid));
	cdev->private->flags.pgid_rdy = 0;
	cdev->private->pgid_valid_mask = 0;
	/* Initialize request data. */
	memset(req, 0, sizeof(*req));
	req->timeout	= PGID_TIMEOUT;
	req->maxretries	= PGID_RETRIES;
	req->callback	= snid_callback;
	req->lpm	= 0x80;
	snid_do(cdev);
}

/*
 * Perform path verification.
 */
static void verify_start(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_request *req = &cdev->private->req;

	sch->vpm = 0;
	/* Initialize request data. */
	memset(req, 0, sizeof(*req));
	req->timeout	= PGID_TIMEOUT;
	req->maxretries	= PGID_RETRIES;
	req->lpm	= 0x80;
	if (cdev->private->options.pgroup) {
		req->callback	= spid_callback;
		spid_do(cdev);
	} else {
		req->filter	= nop_filter;
		req->callback	= nop_callback;
		nop_do(cdev);
	}
}

/**
 * ccw_device_verify_start - perform path verification
 * @cdev: ccw device
 *
 * Perform an I/O on each available channel path to @cdev to determine which
 * paths are operational. The resulting path mask is stored in sch->vpm.
 * If device options specify pathgrouping, establish a pathgroup for the
 * operational paths. When finished, call ccw_device_verify_done with a
 * return code specifying the result.
 */
void ccw_device_verify_start(struct ccw_device *cdev)
{
	CIO_TRACE_EVENT(4, "vrfy");
	CIO_HEX_EVENT(4, &cdev->private->dev_id, sizeof(cdev->private->dev_id));
	if (!cdev->private->flags.pgid_rdy) {
		/* No pathgrouping possible. */
		cdev->private->options.pgroup = 0;
		cdev->private->flags.pgid_single = 1;
	} else
		cdev->private->flags.pgid_single = 0;
	cdev->private->flags.doverify = 0;
	verify_start(cdev);
}

/*
 * Process disband SET PGID request result.
 */
static void disband_callback(struct ccw_device *cdev, void *data, int rc)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_dev_id *id = &cdev->private->dev_id;

	if (rc)
		goto out;
	/* Ensure consistent multipathing state at device and channel. */
	cdev->private->flags.pgid_single = 1;
	if (sch->config.mp) {
		sch->config.mp = 0;
		rc = cio_commit_config(sch);
	}
out:
	CIO_MSG_EVENT(0, "disb: device 0.%x.%04x: rc=%d\n", id->ssid, id->devno,
		      rc);
	ccw_device_disband_done(cdev, rc);
}

/**
 * ccw_device_disband_start - disband pathgroup
 * @cdev: ccw device
 *
 * Execute a SET PGID channel program on @cdev to disband a previously
 * established pathgroup. When finished, call ccw_device_disband_done with
 * a return code specifying the result.
 */
void ccw_device_disband_start(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_request *req = &cdev->private->req;
	u8 fn;

	CIO_TRACE_EVENT(4, "disb");
	CIO_HEX_EVENT(4, &cdev->private->dev_id, sizeof(cdev->private->dev_id));
	/* Request setup. */
	memset(req, 0, sizeof(*req));
	req->timeout	= PGID_TIMEOUT;
	req->maxretries	= PGID_RETRIES;
	req->lpm	= sch->schib.pmcw.pam & sch->opm;
	req->callback	= disband_callback;
	fn = SPID_FUNC_DISBAND;
	if (!cdev->private->flags.pgid_single)
		fn |= SPID_FUNC_MULTI_PATH;
	spid_build_cp(cdev, fn);
	ccw_request_start(cdev);
}
