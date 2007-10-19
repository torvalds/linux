/*
 * Copyright (C) 2005 Mike Christie, All rights reserved.
 * Copyright (C) 2007 Red Hat, Inc. All rights reserved.
 * Authors: Mike Christie
 *          Dave Wysochanski
 *
 * This file is released under the GPL.
 *
 * This module implements the specific path activation code for
 * HP StorageWorks and FSC FibreCat Asymmetric (Active/Passive)
 * storage arrays.
 * These storage arrays have controller-based failover, not
 * LUN-based failover.  However, LUN-based failover is the design
 * of dm-multipath. Thus, this module is written for LUN-based failover.
 */
#include <linux/blkdev.h>
#include <linux/list.h>
#include <linux/types.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_dbg.h>

#include "dm.h"
#include "dm-hw-handler.h"

#define DM_MSG_PREFIX "multipath hp-sw"
#define DM_HP_HWH_NAME "hp-sw"
#define DM_HP_HWH_VER "1.0.0"

struct hp_sw_context {
	unsigned char sense[SCSI_SENSE_BUFFERSIZE];
};

/*
 * hp_sw_error_is_retryable - Is an HP-specific check condition retryable?
 * @req: path activation request
 *
 * Examine error codes of request and determine whether the error is retryable.
 * Some error codes are already retried by scsi-ml (see
 * scsi_decide_disposition), but some HP specific codes are not.
 * The intent of this routine is to supply the logic for the HP specific
 * check conditions.
 *
 * Returns:
 *  1 - command completed with retryable error
 *  0 - command completed with non-retryable error
 *
 * Possible optimizations
 * 1. More hardware-specific error codes
 */
static int hp_sw_error_is_retryable(struct request *req)
{
	/*
	 * NOT_READY is known to be retryable
	 * For now we just dump out the sense data and call it retryable
	 */
	if (status_byte(req->errors) == CHECK_CONDITION)
		__scsi_print_sense(DM_HP_HWH_NAME, req->sense, req->sense_len);

	/*
	 * At this point we don't have complete information about all the error
	 * codes from this hardware, so we are just conservative and retry
	 * when in doubt.
	 */
	return 1;
}

/*
 * hp_sw_end_io - Completion handler for HP path activation.
 * @req: path activation request
 * @error: scsi-ml error
 *
 *  Check sense data, free request structure, and notify dm that
 *  pg initialization has completed.
 *
 * Context: scsi-ml softirq
 *
 */
static void hp_sw_end_io(struct request *req, int error)
{
	struct dm_path *path = req->end_io_data;
	unsigned err_flags = 0;

	if (!error) {
		DMDEBUG("%s path activation command - success",
			path->dev->name);
		goto out;
	}

	if (hp_sw_error_is_retryable(req)) {
		DMDEBUG("%s path activation command - retry",
			path->dev->name);
		err_flags = MP_RETRY;
		goto out;
	}

	DMWARN("%s path activation fail - error=0x%x",
	       path->dev->name, error);
	err_flags = MP_FAIL_PATH;

out:
	req->end_io_data = NULL;
	__blk_put_request(req->q, req);
	dm_pg_init_complete(path, err_flags);
}

/*
 * hp_sw_get_request - Allocate an HP specific path activation request
 * @path: path on which request will be sent (needed for request queue)
 *
 * The START command is used for path activation request.
 * These arrays are controller-based failover, not LUN based.
 * One START command issued to a single path will fail over all
 * LUNs for the same controller.
 *
 * Possible optimizations
 * 1. Make timeout configurable
 * 2. Preallocate request
 */
static struct request *hp_sw_get_request(struct dm_path *path)
{
	struct request *req;
	struct block_device *bdev = path->dev->bdev;
	struct request_queue *q = bdev_get_queue(bdev);
	struct hp_sw_context *h = path->hwhcontext;

	req = blk_get_request(q, WRITE, GFP_NOIO);
	if (!req)
		goto out;

	req->timeout = 60 * HZ;

	req->errors = 0;
	req->cmd_type = REQ_TYPE_BLOCK_PC;
	req->cmd_flags |= REQ_FAILFAST | REQ_NOMERGE;
	req->end_io_data = path;
	req->sense = h->sense;
	memset(req->sense, 0, SCSI_SENSE_BUFFERSIZE);

	memset(&req->cmd, 0, BLK_MAX_CDB);
	req->cmd[0] = START_STOP;
	req->cmd[4] = 1;
	req->cmd_len = COMMAND_SIZE(req->cmd[0]);

out:
	return req;
}

/*
 * hp_sw_pg_init - HP path activation implementation.
 * @hwh: hardware handler specific data
 * @bypassed: unused; is the path group bypassed? (see dm-mpath.c)
 * @path: path to send initialization command
 *
 * Send an HP-specific path activation command on 'path'.
 * Do not try to optimize in any way, just send the activation command.
 * More than one path activation command may be sent to the same controller.
 * This seems to work fine for basic failover support.
 *
 * Possible optimizations
 * 1. Detect an in-progress activation request and avoid submitting another one
 * 2. Model the controller and only send a single activation request at a time
 * 3. Determine the state of a path before sending an activation request
 *
 * Context: kmpathd (see process_queued_ios() in dm-mpath.c)
 */
static void hp_sw_pg_init(struct hw_handler *hwh, unsigned bypassed,
			  struct dm_path *path)
{
	struct request *req;
	struct hp_sw_context *h;

	path->hwhcontext = hwh->context;
	h = hwh->context;

	req = hp_sw_get_request(path);
	if (!req) {
		DMERR("%s path activation command - allocation fail",
		      path->dev->name);
		goto retry;
	}

	DMDEBUG("%s path activation command - sent", path->dev->name);

	blk_execute_rq_nowait(req->q, NULL, req, 1, hp_sw_end_io);
	return;

retry:
	dm_pg_init_complete(path, MP_RETRY);
}

static int hp_sw_create(struct hw_handler *hwh, unsigned argc, char **argv)
{
	struct hp_sw_context *h;

	h = kmalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return -ENOMEM;

	hwh->context = h;

	return 0;
}

static void hp_sw_destroy(struct hw_handler *hwh)
{
	struct hp_sw_context *h = hwh->context;

	kfree(h);
}

static struct hw_handler_type hp_sw_hwh = {
	.name = DM_HP_HWH_NAME,
	.module = THIS_MODULE,
	.create = hp_sw_create,
	.destroy = hp_sw_destroy,
	.pg_init = hp_sw_pg_init,
};

static int __init hp_sw_init(void)
{
	int r;

	r = dm_register_hw_handler(&hp_sw_hwh);
	if (r < 0)
		DMERR("register failed %d", r);
	else
		DMINFO("version " DM_HP_HWH_VER " loaded");

	return r;
}

static void __exit hp_sw_exit(void)
{
	int r;

	r = dm_unregister_hw_handler(&hp_sw_hwh);
	if (r < 0)
		DMERR("unregister failed %d", r);
}

module_init(hp_sw_init);
module_exit(hp_sw_exit);

MODULE_DESCRIPTION("DM Multipath HP StorageWorks / FSC FibreCat (A/P) support");
MODULE_AUTHOR("Mike Christie, Dave Wysochanski <dm-devel@redhat.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION(DM_HP_HWH_VER);
