// SPDX-License-Identifier: GPL-2.0
/*
 *  CCW device PGID and path verification I/O handling.
 *
 *    Copyright IBM Corp. 2002, 2009
 *    Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 *		 Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <asm/ccwdev.h>
#include <asm/cio.h>

#include "cio.h"
#include "cio_debug.h"
#include "device.h"
#include "io_sch.h"

#define PGID_RETRIES	256
#define PGID_TIMEOUT	(10 * HZ)

static void verify_start(struct ccw_device *cdev);

/*
 * Process path verification data and report result.
 */
static void verify_done(struct ccw_device *cdev, int rc)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_dev_id *id = &cdev->private->dev_id;
	int mpath = cdev->private->flags.mpath;
	int pgroup = cdev->private->flags.pgroup;

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
	struct ccw1 *cp = cdev->private->dma_area->iccws;

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

	req->lpm = lpm_adjust(req->lpm, sch->schib.pmcw.pam & sch->opm &
			      ~cdev->private->path_noirq_mask);
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

	switch (rc) {
	case 0:
		sch->vpm |= req->lpm;
		break;
	case -ETIME:
		cdev->private->path_noirq_mask |= req->lpm;
		break;
	case -EACCES:
		cdev->private->path_notoper_mask |= req->lpm;
		break;
	default:
		goto err;
	}
	/* Continue on the next path. */
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
	struct ccw1 *cp = cdev->private->dma_area->iccws;
	int i = pathmask_to_pos(req->lpm);
	struct pgid *pgid = &cdev->private->dma_area->pgid[i];

	pgid->inf.fc	= fn;
	cp->cmd_code	= CCW_CMD_SET_PGID;
	cp->cda		= virt_to_dma32(pgid);
	cp->count	= sizeof(*pgid);
	cp->flags	= CCW_FLAG_SLI;
	req->cp		= cp;
}

static void pgid_wipeout_callback(struct ccw_device *cdev, void *data, int rc)
{
	if (rc) {
		/* We don't know the path groups' state. Abort. */
		verify_done(cdev, rc);
		return;
	}
	/*
	 * Path groups have been reset. Restart path verification but
	 * leave paths in path_noirq_mask out.
	 */
	cdev->private->flags.pgid_unknown = 0;
	verify_start(cdev);
}

/*
 * Reset pathgroups and restart path verification, leave unusable paths out.
 */
static void pgid_wipeout_start(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_dev_id *id = &cdev->private->dev_id;
	struct ccw_request *req = &cdev->private->req;
	u8 fn;

	CIO_MSG_EVENT(2, "wipe: device 0.%x.%04x: pvm=%02x nim=%02x\n",
		      id->ssid, id->devno, cdev->private->pgid_valid_mask,
		      cdev->private->path_noirq_mask);

	/* Initialize request data. */
	memset(req, 0, sizeof(*req));
	req->timeout	= PGID_TIMEOUT;
	req->maxretries	= PGID_RETRIES;
	req->lpm	= sch->schib.pmcw.pam;
	req->callback	= pgid_wipeout_callback;
	fn = SPID_FUNC_DISBAND;
	if (cdev->private->flags.mpath)
		fn |= SPID_FUNC_MULTI_PATH;
	spid_build_cp(cdev, fn);
	ccw_request_start(cdev);
}

/*
 * Perform establish/resign SET PGID on a single path.
 */
static void spid_do(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_request *req = &cdev->private->req;
	u8 fn;

	/* Use next available path that is not already in correct state. */
	req->lpm = lpm_adjust(req->lpm, cdev->private->pgid_todo_mask);
	if (!req->lpm)
		goto out_nopath;
	/* Channel program setup. */
	if (req->lpm & sch->opm)
		fn = SPID_FUNC_ESTABLISH;
	else
		fn = SPID_FUNC_RESIGN;
	if (cdev->private->flags.mpath)
		fn |= SPID_FUNC_MULTI_PATH;
	spid_build_cp(cdev, fn);
	ccw_request_start(cdev);
	return;

out_nopath:
	if (cdev->private->flags.pgid_unknown) {
		/* At least one SPID could be partially done. */
		pgid_wipeout_start(cdev);
		return;
	}
	verify_done(cdev, sch->vpm ? 0 : -EACCES);
}

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
	case -ETIME:
		cdev->private->flags.pgid_unknown = 1;
		cdev->private->path_noirq_mask |= req->lpm;
		break;
	case -EACCES:
		cdev->private->path_notoper_mask |= req->lpm;
		break;
	case -EOPNOTSUPP:
		if (cdev->private->flags.mpath) {
			/* Try without multipathing. */
			cdev->private->flags.mpath = 0;
			goto out_restart;
		}
		/* Try without pathgrouping. */
		cdev->private->flags.pgroup = 0;
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

static void spid_start(struct ccw_device *cdev)
{
	struct ccw_request *req = &cdev->private->req;

	/* Initialize request data. */
	memset(req, 0, sizeof(*req));
	req->timeout	= PGID_TIMEOUT;
	req->maxretries	= PGID_RETRIES;
	req->lpm	= 0x80;
	req->singlepath	= 1;
	req->callback	= spid_callback;
	spid_do(cdev);
}

static int pgid_is_reset(struct pgid *p)
{
	char *c;

	for (c = (char *)p + 1; c < (char *)(p + 1); c++) {
		if (*c != 0)
			return 0;
	}
	return 1;
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
			 int *mismatch, u8 *reserved, u8 *reset)
{
	struct pgid *pgid = &cdev->private->dma_area->pgid[0];
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
			*reserved |= lpm;
		if (pgid_is_reset(pgid)) {
			*reset |= lpm;
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

static u8 pgid_to_donepm(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct pgid *pgid;
	int i;
	int lpm;
	u8 donepm = 0;

	/* Set bits for paths which are already in the target state. */
	for (i = 0; i < 8; i++) {
		lpm = 0x80 >> i;
		if ((cdev->private->pgid_valid_mask & lpm) == 0)
			continue;
		pgid = &cdev->private->dma_area->pgid[i];
		if (sch->opm & lpm) {
			if (pgid->inf.ps.state1 != SNID_STATE1_GROUPED)
				continue;
		} else {
			if (pgid->inf.ps.state1 != SNID_STATE1_UNGROUPED)
				continue;
		}
		if (cdev->private->flags.mpath) {
			if (pgid->inf.ps.state3 != SNID_STATE3_MULTI_PATH)
				continue;
		} else {
			if (pgid->inf.ps.state3 != SNID_STATE3_SINGLE_PATH)
				continue;
		}
		donepm |= lpm;
	}

	return donepm;
}

static void pgid_fill(struct ccw_device *cdev, struct pgid *pgid)
{
	int i;

	for (i = 0; i < 8; i++)
		memcpy(&cdev->private->dma_area->pgid[i], pgid,
		       sizeof(struct pgid));
}

/*
 * Process SENSE PGID data and report result.
 */
static void snid_done(struct ccw_device *cdev, int rc)
{
	struct ccw_dev_id *id = &cdev->private->dev_id;
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct pgid *pgid;
	int mismatch = 0;
	u8 reserved = 0;
	u8 reset = 0;
	u8 donepm;

	if (rc)
		goto out;
	pgid_analyze(cdev, &pgid, &mismatch, &reserved, &reset);
	if (reserved == cdev->private->pgid_valid_mask)
		rc = -EUSERS;
	else if (mismatch)
		rc = -EOPNOTSUPP;
	else {
		donepm = pgid_to_donepm(cdev);
		sch->vpm = donepm & sch->opm;
		cdev->private->pgid_reset_mask |= reset;
		cdev->private->pgid_todo_mask &=
			~(donepm | cdev->private->path_noirq_mask);
		pgid_fill(cdev, pgid);
	}
out:
	CIO_MSG_EVENT(2, "snid: device 0.%x.%04x: rc=%d pvm=%02x vpm=%02x "
		      "todo=%02x mism=%d rsvd=%02x reset=%02x\n", id->ssid,
		      id->devno, rc, cdev->private->pgid_valid_mask, sch->vpm,
		      cdev->private->pgid_todo_mask, mismatch, reserved, reset);
	switch (rc) {
	case 0:
		if (cdev->private->flags.pgid_unknown) {
			pgid_wipeout_start(cdev);
			return;
		}
		/* Anything left to do? */
		if (cdev->private->pgid_todo_mask == 0) {
			verify_done(cdev, sch->vpm == 0 ? -EACCES : 0);
			return;
		}
		/* Perform path-grouping. */
		spid_start(cdev);
		break;
	case -EOPNOTSUPP:
		/* Path-grouping not supported. */
		cdev->private->flags.pgroup = 0;
		cdev->private->flags.mpath = 0;
		verify_start(cdev);
		break;
	default:
		verify_done(cdev, rc);
	}
}

/*
 * Create channel program to perform a SENSE PGID on a single path.
 */
static void snid_build_cp(struct ccw_device *cdev)
{
	struct ccw_request *req = &cdev->private->req;
	struct ccw1 *cp = cdev->private->dma_area->iccws;
	int i = pathmask_to_pos(req->lpm);

	/* Channel program setup. */
	cp->cmd_code	= CCW_CMD_SENSE_PGID;
	cp->cda		= virt_to_dma32(&cdev->private->dma_area->pgid[i]);
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
	int ret;

	req->lpm = lpm_adjust(req->lpm, sch->schib.pmcw.pam &
			      ~cdev->private->path_noirq_mask);
	if (!req->lpm)
		goto out_nopath;
	snid_build_cp(cdev);
	ccw_request_start(cdev);
	return;

out_nopath:
	if (cdev->private->pgid_valid_mask)
		ret = 0;
	else if (cdev->private->path_noirq_mask)
		ret = -ETIME;
	else
		ret = -EACCES;
	snid_done(cdev, ret);
}

/*
 * Process SENSE PGID request result for single path.
 */
static void snid_callback(struct ccw_device *cdev, void *data, int rc)
{
	struct ccw_request *req = &cdev->private->req;

	switch (rc) {
	case 0:
		cdev->private->pgid_valid_mask |= req->lpm;
		break;
	case -ETIME:
		cdev->private->flags.pgid_unknown = 1;
		cdev->private->path_noirq_mask |= req->lpm;
		break;
	case -EACCES:
		cdev->private->path_notoper_mask |= req->lpm;
		break;
	default:
		goto err;
	}
	/* Continue on the next path. */
	req->lpm >>= 1;
	snid_do(cdev);
	return;

err:
	snid_done(cdev, rc);
}

/*
 * Perform path verification.
 */
static void verify_start(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_request *req = &cdev->private->req;
	struct ccw_dev_id *devid = &cdev->private->dev_id;

	sch->vpm = 0;
	sch->lpm = sch->schib.pmcw.pam;

	/* Initialize PGID data. */
	memset(cdev->private->dma_area->pgid, 0,
	       sizeof(cdev->private->dma_area->pgid));
	cdev->private->pgid_valid_mask = 0;
	cdev->private->pgid_todo_mask = sch->schib.pmcw.pam;
	cdev->private->path_notoper_mask = 0;

	/* Initialize request data. */
	memset(req, 0, sizeof(*req));
	req->timeout	= PGID_TIMEOUT;
	req->maxretries	= PGID_RETRIES;
	req->lpm	= 0x80;
	req->singlepath	= 1;
	if (cdev->private->flags.pgroup) {
		CIO_TRACE_EVENT(4, "snid");
		CIO_HEX_EVENT(4, devid, sizeof(*devid));
		req->callback	= snid_callback;
		snid_do(cdev);
	} else {
		CIO_TRACE_EVENT(4, "nop");
		CIO_HEX_EVENT(4, devid, sizeof(*devid));
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
	/*
	 * Initialize pathgroup and multipath state with target values.
	 * They may change in the course of path verification.
	 */
	cdev->private->flags.pgroup = cdev->private->options.pgroup;
	cdev->private->flags.mpath = cdev->private->options.mpath;
	cdev->private->flags.doverify = 0;
	cdev->private->path_noirq_mask = 0;
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
	cdev->private->flags.mpath = 0;
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
	req->singlepath	= 1;
	req->callback	= disband_callback;
	fn = SPID_FUNC_DISBAND;
	if (cdev->private->flags.mpath)
		fn |= SPID_FUNC_MULTI_PATH;
	spid_build_cp(cdev, fn);
	ccw_request_start(cdev);
}

struct stlck_data {
	struct completion done;
	int rc;
};

static void stlck_build_cp(struct ccw_device *cdev, void *buf1, void *buf2)
{
	struct ccw_request *req = &cdev->private->req;
	struct ccw1 *cp = cdev->private->dma_area->iccws;

	cp[0].cmd_code = CCW_CMD_STLCK;
	cp[0].cda = virt_to_dma32(buf1);
	cp[0].count = 32;
	cp[0].flags = CCW_FLAG_CC;
	cp[1].cmd_code = CCW_CMD_RELEASE;
	cp[1].cda = virt_to_dma32(buf2);
	cp[1].count = 32;
	cp[1].flags = 0;
	req->cp = cp;
}

static void stlck_callback(struct ccw_device *cdev, void *data, int rc)
{
	struct stlck_data *sdata = data;

	sdata->rc = rc;
	complete(&sdata->done);
}

/**
 * ccw_device_stlck_start - perform unconditional release
 * @cdev: ccw device
 * @data: data pointer to be passed to ccw_device_stlck_done
 * @buf1: data pointer used in channel program
 * @buf2: data pointer used in channel program
 *
 * Execute a channel program on @cdev to release an existing PGID reservation.
 */
static void ccw_device_stlck_start(struct ccw_device *cdev, void *data,
				   void *buf1, void *buf2)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct ccw_request *req = &cdev->private->req;

	CIO_TRACE_EVENT(4, "stlck");
	CIO_HEX_EVENT(4, &cdev->private->dev_id, sizeof(cdev->private->dev_id));
	/* Request setup. */
	memset(req, 0, sizeof(*req));
	req->timeout	= PGID_TIMEOUT;
	req->maxretries	= PGID_RETRIES;
	req->lpm	= sch->schib.pmcw.pam & sch->opm;
	req->data	= data;
	req->callback	= stlck_callback;
	stlck_build_cp(cdev, buf1, buf2);
	ccw_request_start(cdev);
}

/*
 * Perform unconditional reserve + release.
 */
int ccw_device_stlck(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct stlck_data data;
	u8 *buffer;
	int rc;

	/* Check if steal lock operation is valid for this device. */
	if (cdev->drv) {
		if (!cdev->private->options.force)
			return -EINVAL;
	}
	buffer = kzalloc(64, GFP_DMA | GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;
	init_completion(&data.done);
	data.rc = -EIO;
	spin_lock_irq(&sch->lock);
	rc = cio_enable_subchannel(sch, (u32)virt_to_phys(sch));
	if (rc)
		goto out_unlock;
	/* Perform operation. */
	cdev->private->state = DEV_STATE_STEAL_LOCK;
	ccw_device_stlck_start(cdev, &data, &buffer[0], &buffer[32]);
	spin_unlock_irq(&sch->lock);
	/* Wait for operation to finish. */
	if (wait_for_completion_interruptible(&data.done)) {
		/* Got a signal. */
		spin_lock_irq(&sch->lock);
		ccw_request_cancel(cdev);
		spin_unlock_irq(&sch->lock);
		wait_for_completion(&data.done);
	}
	rc = data.rc;
	/* Check results. */
	spin_lock_irq(&sch->lock);
	cio_disable_subchannel(sch);
	cdev->private->state = DEV_STATE_BOXED;
out_unlock:
	spin_unlock_irq(&sch->lock);
	kfree(buffer);

	return rc;
}
