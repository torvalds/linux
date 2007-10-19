/*
 * Engenio/LSI RDAC DM HW handler
 *
 * Copyright (C) 2005 Mike Christie. All rights reserved.
 * Copyright (C) Chandra Seetharaman, IBM Corp. 2007
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_eh.h>

#define DM_MSG_PREFIX "multipath rdac"

#include "dm.h"
#include "dm-hw-handler.h"

#define RDAC_DM_HWH_NAME "rdac"
#define RDAC_DM_HWH_VER "0.4"

/*
 * LSI mode page stuff
 *
 * These struct definitions and the forming of the
 * mode page were taken from the LSI RDAC 2.4 GPL'd
 * driver, and then converted to Linux conventions.
 */
#define RDAC_QUIESCENCE_TIME 20;
/*
 * Page Codes
 */
#define RDAC_PAGE_CODE_REDUNDANT_CONTROLLER 0x2c

/*
 * Controller modes definitions
 */
#define RDAC_MODE_TRANSFER_ALL_LUNS		0x01
#define RDAC_MODE_TRANSFER_SPECIFIED_LUNS	0x02

/*
 * RDAC Options field
 */
#define RDAC_FORCED_QUIESENCE 0x02

#define RDAC_FAILOVER_TIMEOUT (60 * HZ)

struct rdac_mode_6_hdr {
	u8	data_len;
	u8	medium_type;
	u8	device_params;
	u8	block_desc_len;
};

struct rdac_mode_10_hdr {
	u16	data_len;
	u8	medium_type;
	u8	device_params;
	u16	reserved;
	u16	block_desc_len;
};

struct rdac_mode_common {
	u8	controller_serial[16];
	u8	alt_controller_serial[16];
	u8	rdac_mode[2];
	u8	alt_rdac_mode[2];
	u8	quiescence_timeout;
	u8	rdac_options;
};

struct rdac_pg_legacy {
	struct rdac_mode_6_hdr hdr;
	u8	page_code;
	u8	page_len;
	struct rdac_mode_common common;
#define MODE6_MAX_LUN	32
	u8	lun_table[MODE6_MAX_LUN];
	u8	reserved2[32];
	u8	reserved3;
	u8	reserved4;
};

struct rdac_pg_expanded {
	struct rdac_mode_10_hdr hdr;
	u8	page_code;
	u8	subpage_code;
	u8	page_len[2];
	struct rdac_mode_common common;
	u8	lun_table[256];
	u8	reserved3;
	u8	reserved4;
};

struct c9_inquiry {
	u8	peripheral_info;
	u8	page_code;	/* 0xC9 */
	u8	reserved1;
	u8	page_len;
	u8	page_id[4];	/* "vace" */
	u8	avte_cvp;
	u8	path_prio;
	u8	reserved2[38];
};

#define SUBSYS_ID_LEN	16
#define SLOT_ID_LEN	2

struct c4_inquiry {
	u8	peripheral_info;
	u8	page_code;	/* 0xC4 */
	u8	reserved1;
	u8	page_len;
	u8	page_id[4];	/* "subs" */
	u8	subsys_id[SUBSYS_ID_LEN];
	u8	revision[4];
	u8	slot_id[SLOT_ID_LEN];
	u8	reserved[2];
};

struct rdac_controller {
	u8			subsys_id[SUBSYS_ID_LEN];
	u8			slot_id[SLOT_ID_LEN];
	int			use_10_ms;
	struct kref		kref;
	struct list_head	node; /* list of all controllers */
	spinlock_t		lock;
	int			submitted;
	struct list_head	cmd_list; /* list of commands to be submitted */
	union			{
		struct rdac_pg_legacy legacy;
		struct rdac_pg_expanded expanded;
	} mode_select;
};
struct c8_inquiry {
	u8	peripheral_info;
	u8	page_code; /* 0xC8 */
	u8	reserved1;
	u8	page_len;
	u8	page_id[4]; /* "edid" */
	u8	reserved2[3];
	u8	vol_uniq_id_len;
	u8	vol_uniq_id[16];
	u8	vol_user_label_len;
	u8	vol_user_label[60];
	u8	array_uniq_id_len;
	u8	array_unique_id[16];
	u8	array_user_label_len;
	u8	array_user_label[60];
	u8	lun[8];
};

struct c2_inquiry {
	u8	peripheral_info;
	u8	page_code;	/* 0xC2 */
	u8	reserved1;
	u8	page_len;
	u8	page_id[4];	/* "swr4" */
	u8	sw_version[3];
	u8	sw_date[3];
	u8	features_enabled;
	u8	max_lun_supported;
	u8	partitions[239]; /* Total allocation length should be 0xFF */
};

struct rdac_handler {
	struct list_head	entry; /* list waiting to submit MODE SELECT */
	unsigned		timeout;
	struct rdac_controller	*ctlr;
#define UNINITIALIZED_LUN	(1 << 8)
	unsigned		lun;
	unsigned char		sense[SCSI_SENSE_BUFFERSIZE];
	struct dm_path		*path;
	struct work_struct	work;
#define	SEND_C2_INQUIRY		1
#define	SEND_C4_INQUIRY		2
#define	SEND_C8_INQUIRY		3
#define	SEND_C9_INQUIRY		4
#define	SEND_MODE_SELECT	5
	int			cmd_to_send;
	union			{
		struct c2_inquiry c2;
		struct c4_inquiry c4;
		struct c8_inquiry c8;
		struct c9_inquiry c9;
	} inq;
};

static LIST_HEAD(ctlr_list);
static DEFINE_SPINLOCK(list_lock);
static struct workqueue_struct *rdac_wkqd;

static inline int had_failures(struct request *req, int error)
{
	return (error || host_byte(req->errors) != DID_OK ||
			msg_byte(req->errors) != COMMAND_COMPLETE);
}

static void rdac_resubmit_all(struct rdac_handler *h)
{
	struct rdac_controller *ctlr = h->ctlr;
	struct rdac_handler *tmp, *h1;

	spin_lock(&ctlr->lock);
	list_for_each_entry_safe(h1, tmp, &ctlr->cmd_list, entry) {
		h1->cmd_to_send = SEND_C9_INQUIRY;
		queue_work(rdac_wkqd, &h1->work);
		list_del(&h1->entry);
	}
	ctlr->submitted = 0;
	spin_unlock(&ctlr->lock);
}

static void mode_select_endio(struct request *req, int error)
{
	struct rdac_handler *h = req->end_io_data;
	struct scsi_sense_hdr sense_hdr;
	int sense = 0, fail = 0;

	if (had_failures(req, error)) {
		fail = 1;
		goto failed;
	}

	if (status_byte(req->errors) == CHECK_CONDITION) {
		scsi_normalize_sense(req->sense, SCSI_SENSE_BUFFERSIZE,
				&sense_hdr);
		sense = (sense_hdr.sense_key << 16) | (sense_hdr.asc << 8) |
				sense_hdr.ascq;
		/* If it is retryable failure, submit the c9 inquiry again */
		if (sense == 0x59136 || sense == 0x68b02 || sense == 0xb8b02 ||
		    sense == 0x62900) {
			/* 0x59136    - Command lock contention
			 * 0x[6b]8b02 - Quiesense in progress or achieved
			 * 0x62900    - Power On, Reset, or Bus Device Reset
			 */
			h->cmd_to_send = SEND_C9_INQUIRY;
			queue_work(rdac_wkqd, &h->work);
			goto done;
		}
		if (sense)
			DMINFO("MODE_SELECT failed on %s with sense 0x%x",
						h->path->dev->name, sense);
 	}
failed:
	if (fail || sense)
		dm_pg_init_complete(h->path, MP_FAIL_PATH);
	else
		dm_pg_init_complete(h->path, 0);

done:
	rdac_resubmit_all(h);
	__blk_put_request(req->q, req);
}

static struct request *get_rdac_req(struct rdac_handler *h,
			void *buffer, unsigned buflen, int rw)
{
	struct request *rq;
	struct request_queue *q = bdev_get_queue(h->path->dev->bdev);

	rq = blk_get_request(q, rw, GFP_KERNEL);

	if (!rq) {
		DMINFO("get_rdac_req: blk_get_request failed");
		return NULL;
	}

	if (buflen && blk_rq_map_kern(q, rq, buffer, buflen, GFP_KERNEL)) {
		blk_put_request(rq);
		DMINFO("get_rdac_req: blk_rq_map_kern failed");
		return NULL;
	}

 	memset(&rq->cmd, 0, BLK_MAX_CDB);
	rq->sense = h->sense;
	memset(rq->sense, 0, SCSI_SENSE_BUFFERSIZE);
	rq->sense_len = 0;

	rq->end_io_data = h;
	rq->timeout = h->timeout;
	rq->cmd_type = REQ_TYPE_BLOCK_PC;
	rq->cmd_flags |= REQ_FAILFAST | REQ_NOMERGE;
	return rq;
}

static struct request *rdac_failover_get(struct rdac_handler *h)
{
	struct request *rq;
	struct rdac_mode_common *common;
	unsigned data_size;

	if (h->ctlr->use_10_ms) {
		struct rdac_pg_expanded *rdac_pg;

		data_size = sizeof(struct rdac_pg_expanded);
		rdac_pg = &h->ctlr->mode_select.expanded;
		memset(rdac_pg, 0, data_size);
		common = &rdac_pg->common;
		rdac_pg->page_code = RDAC_PAGE_CODE_REDUNDANT_CONTROLLER + 0x40;
		rdac_pg->subpage_code = 0x1;
		rdac_pg->page_len[0] = 0x01;
		rdac_pg->page_len[1] = 0x28;
		rdac_pg->lun_table[h->lun] = 0x81;
	} else {
		struct rdac_pg_legacy *rdac_pg;

		data_size = sizeof(struct rdac_pg_legacy);
		rdac_pg = &h->ctlr->mode_select.legacy;
		memset(rdac_pg, 0, data_size);
		common = &rdac_pg->common;
		rdac_pg->page_code = RDAC_PAGE_CODE_REDUNDANT_CONTROLLER;
		rdac_pg->page_len = 0x68;
		rdac_pg->lun_table[h->lun] = 0x81;
	}
	common->rdac_mode[1] = RDAC_MODE_TRANSFER_SPECIFIED_LUNS;
	common->quiescence_timeout = RDAC_QUIESCENCE_TIME;
	common->rdac_options = RDAC_FORCED_QUIESENCE;

	/* get request for block layer packet command */
	rq = get_rdac_req(h, &h->ctlr->mode_select, data_size, WRITE);
	if (!rq) {
		DMERR("rdac_failover_get: no rq");
		return NULL;
	}

	/* Prepare the command. */
	if (h->ctlr->use_10_ms) {
		rq->cmd[0] = MODE_SELECT_10;
		rq->cmd[7] = data_size >> 8;
		rq->cmd[8] = data_size & 0xff;
	} else {
		rq->cmd[0] = MODE_SELECT;
		rq->cmd[4] = data_size;
	}
	rq->cmd_len = COMMAND_SIZE(rq->cmd[0]);

	return rq;
}

/* Acquires h->ctlr->lock */
static void submit_mode_select(struct rdac_handler *h)
{
	struct request *rq;
	struct request_queue *q = bdev_get_queue(h->path->dev->bdev);

	spin_lock(&h->ctlr->lock);
	if (h->ctlr->submitted) {
		list_add(&h->entry, &h->ctlr->cmd_list);
		goto drop_lock;
	}

	if (!q) {
		DMINFO("submit_mode_select: no queue");
		goto fail_path;
	}

	rq = rdac_failover_get(h);
	if (!rq) {
		DMERR("submit_mode_select: no rq");
		goto fail_path;
	}

	DMINFO("queueing MODE_SELECT command on %s", h->path->dev->name);

	blk_execute_rq_nowait(q, NULL, rq, 1, mode_select_endio);
	h->ctlr->submitted = 1;
	goto drop_lock;
fail_path:
	dm_pg_init_complete(h->path, MP_FAIL_PATH);
drop_lock:
	spin_unlock(&h->ctlr->lock);
}

static void release_ctlr(struct kref *kref)
{
	struct rdac_controller *ctlr;
	ctlr = container_of(kref, struct rdac_controller, kref);

	spin_lock(&list_lock);
	list_del(&ctlr->node);
	spin_unlock(&list_lock);
	kfree(ctlr);
}

static struct rdac_controller *get_controller(u8 *subsys_id, u8 *slot_id)
{
	struct rdac_controller *ctlr, *tmp;

	spin_lock(&list_lock);

	list_for_each_entry(tmp, &ctlr_list, node) {
		if ((memcmp(tmp->subsys_id, subsys_id, SUBSYS_ID_LEN) == 0) &&
			  (memcmp(tmp->slot_id, slot_id, SLOT_ID_LEN) == 0)) {
			kref_get(&tmp->kref);
			spin_unlock(&list_lock);
			return tmp;
		}
	}
	ctlr = kmalloc(sizeof(*ctlr), GFP_ATOMIC);
	if (!ctlr)
		goto done;

	/* initialize fields of controller */
	memcpy(ctlr->subsys_id, subsys_id, SUBSYS_ID_LEN);
	memcpy(ctlr->slot_id, slot_id, SLOT_ID_LEN);
	kref_init(&ctlr->kref);
	spin_lock_init(&ctlr->lock);
	ctlr->submitted = 0;
	ctlr->use_10_ms = -1;
	INIT_LIST_HEAD(&ctlr->cmd_list);
	list_add(&ctlr->node, &ctlr_list);
done:
	spin_unlock(&list_lock);
	return ctlr;
}

static void c4_endio(struct request *req, int error)
{
	struct rdac_handler *h = req->end_io_data;
	struct c4_inquiry *sp;

	if (had_failures(req, error)) {
		dm_pg_init_complete(h->path, MP_FAIL_PATH);
		goto done;
	}

	sp = &h->inq.c4;

	h->ctlr = get_controller(sp->subsys_id, sp->slot_id);

	if (h->ctlr) {
		h->cmd_to_send = SEND_C9_INQUIRY;
		queue_work(rdac_wkqd, &h->work);
	} else
		dm_pg_init_complete(h->path, MP_FAIL_PATH);
done:
	__blk_put_request(req->q, req);
}

static void c2_endio(struct request *req, int error)
{
	struct rdac_handler *h = req->end_io_data;
	struct c2_inquiry *sp;

	if (had_failures(req, error)) {
		dm_pg_init_complete(h->path, MP_FAIL_PATH);
		goto done;
	}

	sp = &h->inq.c2;

	/* If more than MODE6_MAX_LUN luns are supported, use mode select 10 */
	if (sp->max_lun_supported >= MODE6_MAX_LUN)
		h->ctlr->use_10_ms = 1;
	else
		h->ctlr->use_10_ms = 0;

	h->cmd_to_send = SEND_MODE_SELECT;
	queue_work(rdac_wkqd, &h->work);
done:
	__blk_put_request(req->q, req);
}

static void c9_endio(struct request *req, int error)
{
	struct rdac_handler *h = req->end_io_data;
	struct c9_inquiry *sp;

	if (had_failures(req, error)) {
		dm_pg_init_complete(h->path, MP_FAIL_PATH);
		goto done;
	}

	/* We need to look at the sense keys here to take clear action.
	 * For now simple logic: If the host is in AVT mode or if controller
	 * owns the lun, return dm_pg_init_complete(), otherwise submit
	 * MODE SELECT.
	 */
	sp = &h->inq.c9;

	/* If in AVT mode, return success */
	if ((sp->avte_cvp >> 7) == 0x1) {
		dm_pg_init_complete(h->path, 0);
		goto done;
	}

	/* If the controller on this path owns the LUN, return success */
	if (sp->avte_cvp & 0x1) {
		dm_pg_init_complete(h->path, 0);
		goto done;
	}

	if (h->ctlr) {
		if (h->ctlr->use_10_ms == -1)
			h->cmd_to_send = SEND_C2_INQUIRY;
		else
			h->cmd_to_send = SEND_MODE_SELECT;
	} else
		h->cmd_to_send = SEND_C4_INQUIRY;
	queue_work(rdac_wkqd, &h->work);
done:
	__blk_put_request(req->q, req);
}

static void c8_endio(struct request *req, int error)
{
	struct rdac_handler *h = req->end_io_data;
	struct c8_inquiry *sp;

	if (had_failures(req, error)) {
		dm_pg_init_complete(h->path, MP_FAIL_PATH);
		goto done;
	}

	/* We need to look at the sense keys here to take clear action.
	 * For now simple logic: Get the lun from the inquiry page.
	 */
	sp = &h->inq.c8;
	h->lun = sp->lun[7]; /* currently it uses only one byte */
	h->cmd_to_send = SEND_C9_INQUIRY;
	queue_work(rdac_wkqd, &h->work);
done:
	__blk_put_request(req->q, req);
}

static void submit_inquiry(struct rdac_handler *h, int page_code,
		unsigned int len, rq_end_io_fn endio)
{
	struct request *rq;
	struct request_queue *q = bdev_get_queue(h->path->dev->bdev);

	if (!q)
		goto fail_path;

	rq = get_rdac_req(h, &h->inq, len, READ);
	if (!rq)
		goto fail_path;

	/* Prepare the command. */
	rq->cmd[0] = INQUIRY;
	rq->cmd[1] = 1;
	rq->cmd[2] = page_code;
	rq->cmd[4] = len;
	rq->cmd_len = COMMAND_SIZE(INQUIRY);
	blk_execute_rq_nowait(q, NULL, rq, 1, endio);
	return;

fail_path:
	dm_pg_init_complete(h->path, MP_FAIL_PATH);
}

static void service_wkq(struct work_struct *work)
{
	struct rdac_handler *h = container_of(work, struct rdac_handler, work);

	switch (h->cmd_to_send) {
	case SEND_C2_INQUIRY:
		submit_inquiry(h, 0xC2, sizeof(struct c2_inquiry), c2_endio);
		break;
	case SEND_C4_INQUIRY:
		submit_inquiry(h, 0xC4, sizeof(struct c4_inquiry), c4_endio);
		break;
	case SEND_C8_INQUIRY:
		submit_inquiry(h, 0xC8, sizeof(struct c8_inquiry), c8_endio);
		break;
	case SEND_C9_INQUIRY:
		submit_inquiry(h, 0xC9, sizeof(struct c9_inquiry), c9_endio);
		break;
	case SEND_MODE_SELECT:
		submit_mode_select(h);
		break;
	default:
		BUG();
	}
}
/*
 * only support subpage2c until we confirm that this is just a matter of
 * of updating firmware or not, and RDAC (basic AVT works already) for now
 * but we can add these in in when we get time and testers
 */
static int rdac_create(struct hw_handler *hwh, unsigned argc, char **argv)
{
	struct rdac_handler *h;
	unsigned timeout;

	if (argc == 0) {
		/* No arguments: use defaults */
		timeout = RDAC_FAILOVER_TIMEOUT;
	} else if (argc != 1) {
		DMWARN("incorrect number of arguments");
		return -EINVAL;
	} else {
		if (sscanf(argv[1], "%u", &timeout) != 1) {
			DMWARN("invalid timeout value");
			return -EINVAL;
		}
	}

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return -ENOMEM;

	hwh->context = h;
	h->timeout = timeout;
	h->lun = UNINITIALIZED_LUN;
	INIT_WORK(&h->work, service_wkq);
	DMWARN("using RDAC command with timeout %u", h->timeout);

	return 0;
}

static void rdac_destroy(struct hw_handler *hwh)
{
	struct rdac_handler *h = hwh->context;

	if (h->ctlr)
		kref_put(&h->ctlr->kref, release_ctlr);
	kfree(h);
	hwh->context = NULL;
}

static unsigned rdac_error(struct hw_handler *hwh, struct bio *bio)
{
	/* Try default handler */
	return dm_scsi_err_handler(hwh, bio);
}

static void rdac_pg_init(struct hw_handler *hwh, unsigned bypassed,
			struct dm_path *path)
{
	struct rdac_handler *h = hwh->context;

	h->path = path;
	switch (h->lun) {
	case UNINITIALIZED_LUN:
		submit_inquiry(h, 0xC8, sizeof(struct c8_inquiry), c8_endio);
		break;
	default:
		submit_inquiry(h, 0xC9, sizeof(struct c9_inquiry), c9_endio);
	}
}

static struct hw_handler_type rdac_handler = {
	.name = RDAC_DM_HWH_NAME,
	.module = THIS_MODULE,
	.create = rdac_create,
	.destroy = rdac_destroy,
	.pg_init = rdac_pg_init,
	.error = rdac_error,
};

static int __init rdac_init(void)
{
	int r;

	rdac_wkqd = create_singlethread_workqueue("rdac_wkqd");
	if (!rdac_wkqd) {
		DMERR("Failed to create workqueue rdac_wkqd.");
		return -ENOMEM;
	}

	r = dm_register_hw_handler(&rdac_handler);
	if (r < 0) {
		DMERR("%s: register failed %d", RDAC_DM_HWH_NAME, r);
		destroy_workqueue(rdac_wkqd);
		return r;
	}

	DMINFO("%s: version %s loaded", RDAC_DM_HWH_NAME, RDAC_DM_HWH_VER);
	return 0;
}

static void __exit rdac_exit(void)
{
	int r = dm_unregister_hw_handler(&rdac_handler);

	destroy_workqueue(rdac_wkqd);
	if (r < 0)
		DMERR("%s: unregister failed %d", RDAC_DM_HWH_NAME, r);
}

module_init(rdac_init);
module_exit(rdac_exit);

MODULE_DESCRIPTION("DM Multipath LSI/Engenio RDAC support");
MODULE_AUTHOR("Mike Christie, Chandra Seetharaman");
MODULE_LICENSE("GPL");
MODULE_VERSION(RDAC_DM_HWH_VER);
