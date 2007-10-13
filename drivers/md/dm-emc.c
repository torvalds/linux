/*
 * Copyright (C) 2004 SUSE LINUX Products GmbH. All rights reserved.
 * Copyright (C) 2004 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 *
 * Multipath support for EMC CLARiiON AX/CX-series hardware.
 */

#include "dm.h"
#include "dm-hw-handler.h"
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>

#define DM_MSG_PREFIX "multipath emc"

struct emc_handler {
	spinlock_t lock;

	/* Whether we should send the short trespass command (FC-series)
	 * or the long version (default for AX/CX CLARiiON arrays). */
	unsigned short_trespass;
	/* Whether or not to honor SCSI reservations when initiating a
	 * switch-over. Default: Don't. */
	unsigned hr;

	unsigned char sense[SCSI_SENSE_BUFFERSIZE];
};

#define TRESPASS_PAGE 0x22
#define EMC_FAILOVER_TIMEOUT (60 * HZ)

/* Code borrowed from dm-lsi-rdac by Mike Christie */

static inline void free_bio(struct bio *bio)
{
	__free_page(bio->bi_io_vec[0].bv_page);
	bio_put(bio);
}

static void emc_endio(struct bio *bio, int error)
{
	struct dm_path *path = bio->bi_private;

	/* We also need to look at the sense keys here whether or not to
	 * switch to the next PG etc.
	 *
	 * For now simple logic: either it works or it doesn't.
	 */
	if (error)
		dm_pg_init_complete(path, MP_FAIL_PATH);
	else
		dm_pg_init_complete(path, 0);

	/* request is freed in block layer */
	free_bio(bio);

	return 0;
}

static struct bio *get_failover_bio(struct dm_path *path, unsigned data_size)
{
	struct bio *bio;
	struct page *page;

	bio = bio_alloc(GFP_ATOMIC, 1);
	if (!bio) {
		DMERR("get_failover_bio: bio_alloc() failed.");
		return NULL;
	}

	bio->bi_rw |= (1 << BIO_RW);
	bio->bi_bdev = path->dev->bdev;
	bio->bi_sector = 0;
	bio->bi_private = path;
	bio->bi_end_io = emc_endio;

	page = alloc_page(GFP_ATOMIC);
	if (!page) {
		DMERR("get_failover_bio: alloc_page() failed.");
		bio_put(bio);
		return NULL;
	}

	if (bio_add_page(bio, page, data_size, 0) != data_size) {
		DMERR("get_failover_bio: alloc_page() failed.");
		__free_page(page);
		bio_put(bio);
		return NULL;
	}

	return bio;
}

static struct request *get_failover_req(struct emc_handler *h,
					struct bio *bio, struct dm_path *path)
{
	struct request *rq;
	struct block_device *bdev = bio->bi_bdev;
	struct request_queue *q = bdev_get_queue(bdev);

	/* FIXME: Figure out why it fails with GFP_ATOMIC. */
	rq = blk_get_request(q, WRITE, __GFP_WAIT);
	if (!rq) {
		DMERR("get_failover_req: blk_get_request failed");
		return NULL;
	}

	blk_rq_append_bio(q, rq, bio);

	rq->sense = h->sense;
	memset(rq->sense, 0, SCSI_SENSE_BUFFERSIZE);
	rq->sense_len = 0;

	memset(&rq->cmd, 0, BLK_MAX_CDB);

	rq->timeout = EMC_FAILOVER_TIMEOUT;
	rq->cmd_type = REQ_TYPE_BLOCK_PC;
	rq->cmd_flags |= REQ_FAILFAST | REQ_NOMERGE;

	return rq;
}

static struct request *emc_trespass_get(struct emc_handler *h,
					struct dm_path *path)
{
	struct bio *bio;
	struct request *rq;
	unsigned char *page22;
	unsigned char long_trespass_pg[] = {
		0, 0, 0, 0,
		TRESPASS_PAGE,        /* Page code */
		0x09,                 /* Page length - 2 */
		h->hr ? 0x01 : 0x81,  /* Trespass code + Honor reservation bit */
		0xff, 0xff,           /* Trespass target */
		0, 0, 0, 0, 0, 0      /* Reserved bytes / unknown */
		};
	unsigned char short_trespass_pg[] = {
		0, 0, 0, 0,
		TRESPASS_PAGE,        /* Page code */
		0x02,                 /* Page length - 2 */
		h->hr ? 0x01 : 0x81,  /* Trespass code + Honor reservation bit */
		0xff,                 /* Trespass target */
		};
	unsigned data_size = h->short_trespass ? sizeof(short_trespass_pg) :
				sizeof(long_trespass_pg);

	/* get bio backing */
	if (data_size > PAGE_SIZE)
		/* this should never happen */
		return NULL;

	bio = get_failover_bio(path, data_size);
	if (!bio) {
		DMERR("emc_trespass_get: no bio");
		return NULL;
	}

	page22 = (unsigned char *)bio_data(bio);
	memset(page22, 0, data_size);

	memcpy(page22, h->short_trespass ?
		short_trespass_pg : long_trespass_pg, data_size);

	/* get request for block layer packet command */
	rq = get_failover_req(h, bio, path);
	if (!rq) {
		DMERR("emc_trespass_get: no rq");
		free_bio(bio);
		return NULL;
	}

	/* Prepare the command. */
	rq->cmd[0] = MODE_SELECT;
	rq->cmd[1] = 0x10;
	rq->cmd[4] = data_size;
	rq->cmd_len = COMMAND_SIZE(rq->cmd[0]);

	return rq;
}

static void emc_pg_init(struct hw_handler *hwh, unsigned bypassed,
			struct dm_path *path)
{
	struct request *rq;
	struct request_queue *q = bdev_get_queue(path->dev->bdev);

	/*
	 * We can either blindly init the pg (then look at the sense),
	 * or we can send some commands to get the state here (then
	 * possibly send the fo cmnd), or we can also have the
	 * initial state passed into us and then get an update here.
	 */
	if (!q) {
		DMINFO("emc_pg_init: no queue");
		goto fail_path;
	}

	/* FIXME: The request should be pre-allocated. */
	rq = emc_trespass_get(hwh->context, path);
	if (!rq) {
		DMERR("emc_pg_init: no rq");
		goto fail_path;
	}

	DMINFO("emc_pg_init: sending switch-over command");
	elv_add_request(q, rq, ELEVATOR_INSERT_FRONT, 1);
	return;

fail_path:
	dm_pg_init_complete(path, MP_FAIL_PATH);
}

static struct emc_handler *alloc_emc_handler(void)
{
	struct emc_handler *h = kmalloc(sizeof(*h), GFP_KERNEL);

	if (h) {
		memset(h, 0, sizeof(*h));
		spin_lock_init(&h->lock);
	}

	return h;
}

static int emc_create(struct hw_handler *hwh, unsigned argc, char **argv)
{
	struct emc_handler *h;
	unsigned hr, short_trespass;

	if (argc == 0) {
		/* No arguments: use defaults */
		hr = 0;
		short_trespass = 0;
	} else if (argc != 2) {
		DMWARN("incorrect number of arguments");
		return -EINVAL;
	} else {
		if ((sscanf(argv[0], "%u", &short_trespass) != 1)
			|| (short_trespass > 1)) {
			DMWARN("invalid trespass mode selected");
			return -EINVAL;
		}

		if ((sscanf(argv[1], "%u", &hr) != 1)
			|| (hr > 1)) {
			DMWARN("invalid honor reservation flag selected");
			return -EINVAL;
		}
	}

	h = alloc_emc_handler();
	if (!h)
		return -ENOMEM;

	hwh->context = h;

	if ((h->short_trespass = short_trespass))
		DMWARN("short trespass command will be send");
	else
		DMWARN("long trespass command will be send");

	if ((h->hr = hr))
		DMWARN("honor reservation bit will be set");
	else
		DMWARN("honor reservation bit will not be set (default)");

	return 0;
}

static void emc_destroy(struct hw_handler *hwh)
{
	struct emc_handler *h = (struct emc_handler *) hwh->context;

	kfree(h);
	hwh->context = NULL;
}

static unsigned emc_error(struct hw_handler *hwh, struct bio *bio)
{
	/* FIXME: Patch from axboe still missing */
#if 0
	int sense;

	if (bio->bi_error & BIO_SENSE) {
		sense = bio->bi_error & 0xffffff; /* sense key / asc / ascq */

		if (sense == 0x020403) {
			/* LUN Not Ready - Manual Intervention Required
			 * indicates this is a passive path.
			 *
			 * FIXME: However, if this is seen and EVPD C0
			 * indicates that this is due to a NDU in
			 * progress, we should set FAIL_PATH too.
			 * This indicates we might have to do a SCSI
			 * inquiry in the end_io path. Ugh. */
			return MP_BYPASS_PG | MP_RETRY_IO;
		} else if (sense == 0x052501) {
			/* An array based copy is in progress. Do not
			 * fail the path, do not bypass to another PG,
			 * do not retry. Fail the IO immediately.
			 * (Actually this is the same conclusion as in
			 * the default handler, but lets make sure.) */
			return 0;
		} else if (sense == 0x062900) {
			/* Unit Attention Code. This is the first IO
			 * to the new path, so just retry. */
			return MP_RETRY_IO;
		}
	}
#endif

	/* Try default handler */
	return dm_scsi_err_handler(hwh, bio);
}

static struct hw_handler_type emc_hwh = {
	.name = "emc",
	.module = THIS_MODULE,
	.create = emc_create,
	.destroy = emc_destroy,
	.pg_init = emc_pg_init,
	.error = emc_error,
};

static int __init dm_emc_init(void)
{
	int r = dm_register_hw_handler(&emc_hwh);

	if (r < 0)
		DMERR("register failed %d", r);

	DMINFO("version 0.0.3 loaded");

	return r;
}

static void __exit dm_emc_exit(void)
{
	int r = dm_unregister_hw_handler(&emc_hwh);

	if (r < 0)
		DMERR("unregister failed %d", r);
}

module_init(dm_emc_init);
module_exit(dm_emc_exit);

MODULE_DESCRIPTION(DM_NAME " EMC CX/AX/FC-family multipath");
MODULE_AUTHOR("Lars Marowsky-Bree <lmb@suse.de>");
MODULE_LICENSE("GPL");
