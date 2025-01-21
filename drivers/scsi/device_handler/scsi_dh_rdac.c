/*
 * LSI/Engenio/NetApp E-Series RDAC SCSI Device Handler
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
#include <scsi/scsi_eh.h>
#include <scsi/scsi_dh.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/module.h>

#define RDAC_NAME "rdac"
#define RDAC_RETRY_COUNT 5

/*
 * LSI mode page stuff
 *
 * These struct definitions and the forming of the
 * mode page were taken from the LSI RDAC 2.4 GPL'd
 * driver, and then converted to Linux conventions.
 */
#define RDAC_QUIESCENCE_TIME 20
/*
 * Page Codes
 */
#define RDAC_PAGE_CODE_REDUNDANT_CONTROLLER 0x2c

/*
 * Controller modes definitions
 */
#define RDAC_MODE_TRANSFER_SPECIFIED_LUNS	0x02

/*
 * RDAC Options field
 */
#define RDAC_FORCED_QUIESENCE 0x02

#define RDAC_TIMEOUT	(60 * HZ)
#define RDAC_RETRIES	3

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
#define ARRAY_LABEL_LEN	31

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

#define UNIQUE_ID_LEN 16
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
	u8	array_unique_id[UNIQUE_ID_LEN];
	u8	array_user_label_len;
	u8	array_user_label[60];
	u8	lun[8];
};

struct rdac_controller {
	u8			array_id[UNIQUE_ID_LEN];
	int			use_ms10;
	struct kref		kref;
	struct list_head	node; /* list of all controllers */
	union			{
		struct rdac_pg_legacy legacy;
		struct rdac_pg_expanded expanded;
	} mode_select;
	u8	index;
	u8	array_name[ARRAY_LABEL_LEN];
	struct Scsi_Host	*host;
	spinlock_t		ms_lock;
	int			ms_queued;
	struct work_struct	ms_work;
	struct scsi_device	*ms_sdev;
	struct list_head	ms_head;
	struct list_head	dh_list;
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

struct rdac_dh_data {
	struct list_head	node;
	struct rdac_controller	*ctlr;
	struct scsi_device	*sdev;
#define UNINITIALIZED_LUN	(1 << 8)
	unsigned		lun;

#define RDAC_MODE		0
#define RDAC_MODE_AVT		1
#define RDAC_MODE_IOSHIP	2
	unsigned char		mode;

#define RDAC_STATE_ACTIVE	0
#define RDAC_STATE_PASSIVE	1
	unsigned char		state;

#define RDAC_LUN_UNOWNED	0
#define RDAC_LUN_OWNED		1
	char			lun_state;

#define RDAC_PREFERRED		0
#define RDAC_NON_PREFERRED	1
	char			preferred;

	union			{
		struct c2_inquiry c2;
		struct c4_inquiry c4;
		struct c8_inquiry c8;
		struct c9_inquiry c9;
	} inq;
};

static const char *mode[] = {
	"RDAC",
	"AVT",
	"IOSHIP",
};
static const char *lun_state[] =
{
	"unowned",
	"owned",
};

struct rdac_queue_data {
	struct list_head	entry;
	struct rdac_dh_data	*h;
	activate_complete	callback_fn;
	void			*callback_data;
};

static LIST_HEAD(ctlr_list);
static DEFINE_SPINLOCK(list_lock);
static struct workqueue_struct *kmpath_rdacd;
static void send_mode_select(struct work_struct *work);

/*
 * module parameter to enable rdac debug logging.
 * 2 bits for each type of logging, only two types defined for now
 * Can be enhanced if required at later point
 */
static int rdac_logging = 1;
module_param(rdac_logging, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(rdac_logging, "A bit mask of rdac logging levels, "
		"Default is 1 - failover logging enabled, "
		"set it to 0xF to enable all the logs");

#define RDAC_LOG_FAILOVER	0
#define RDAC_LOG_SENSE		2

#define RDAC_LOG_BITS		2

#define RDAC_LOG_LEVEL(SHIFT)  \
	((rdac_logging >> (SHIFT)) & ((1 << (RDAC_LOG_BITS)) - 1))

#define RDAC_LOG(SHIFT, sdev, f, arg...) \
do { \
	if (unlikely(RDAC_LOG_LEVEL(SHIFT))) \
		sdev_printk(KERN_INFO, sdev, RDAC_NAME ": " f "\n", ## arg); \
} while (0);

static unsigned int rdac_failover_get(struct rdac_controller *ctlr,
				      struct list_head *list,
				      unsigned char *cdb)
{
	struct rdac_mode_common *common;
	unsigned data_size;
	struct rdac_queue_data *qdata;
	u8 *lun_table;

	if (ctlr->use_ms10) {
		struct rdac_pg_expanded *rdac_pg;

		data_size = sizeof(struct rdac_pg_expanded);
		rdac_pg = &ctlr->mode_select.expanded;
		memset(rdac_pg, 0, data_size);
		common = &rdac_pg->common;
		rdac_pg->page_code = RDAC_PAGE_CODE_REDUNDANT_CONTROLLER + 0x40;
		rdac_pg->subpage_code = 0x1;
		rdac_pg->page_len[0] = 0x01;
		rdac_pg->page_len[1] = 0x28;
		lun_table = rdac_pg->lun_table;
	} else {
		struct rdac_pg_legacy *rdac_pg;

		data_size = sizeof(struct rdac_pg_legacy);
		rdac_pg = &ctlr->mode_select.legacy;
		memset(rdac_pg, 0, data_size);
		common = &rdac_pg->common;
		rdac_pg->page_code = RDAC_PAGE_CODE_REDUNDANT_CONTROLLER;
		rdac_pg->page_len = 0x68;
		lun_table = rdac_pg->lun_table;
	}
	common->rdac_mode[1] = RDAC_MODE_TRANSFER_SPECIFIED_LUNS;
	common->quiescence_timeout = RDAC_QUIESCENCE_TIME;
	common->rdac_options = RDAC_FORCED_QUIESENCE;

	list_for_each_entry(qdata, list, entry) {
		lun_table[qdata->h->lun] = 0x81;
	}

	/* Prepare the command. */
	if (ctlr->use_ms10) {
		cdb[0] = MODE_SELECT_10;
		cdb[7] = data_size >> 8;
		cdb[8] = data_size & 0xff;
	} else {
		cdb[0] = MODE_SELECT;
		cdb[4] = data_size;
	}

	return data_size;
}

static void release_controller(struct kref *kref)
{
	struct rdac_controller *ctlr;
	ctlr = container_of(kref, struct rdac_controller, kref);

	list_del(&ctlr->node);
	kfree(ctlr);
}

static struct rdac_controller *get_controller(int index, char *array_name,
			u8 *array_id, struct scsi_device *sdev)
{
	struct rdac_controller *ctlr, *tmp;

	list_for_each_entry(tmp, &ctlr_list, node) {
		if ((memcmp(tmp->array_id, array_id, UNIQUE_ID_LEN) == 0) &&
			  (tmp->index == index) &&
			  (tmp->host == sdev->host)) {
			kref_get(&tmp->kref);
			return tmp;
		}
	}
	ctlr = kmalloc(sizeof(*ctlr), GFP_ATOMIC);
	if (!ctlr)
		return NULL;

	/* initialize fields of controller */
	memcpy(ctlr->array_id, array_id, UNIQUE_ID_LEN);
	ctlr->index = index;
	ctlr->host = sdev->host;
	memcpy(ctlr->array_name, array_name, ARRAY_LABEL_LEN);

	kref_init(&ctlr->kref);
	ctlr->use_ms10 = -1;
	ctlr->ms_queued = 0;
	ctlr->ms_sdev = NULL;
	spin_lock_init(&ctlr->ms_lock);
	INIT_WORK(&ctlr->ms_work, send_mode_select);
	INIT_LIST_HEAD(&ctlr->ms_head);
	list_add(&ctlr->node, &ctlr_list);
	INIT_LIST_HEAD(&ctlr->dh_list);

	return ctlr;
}

static int get_lun_info(struct scsi_device *sdev, struct rdac_dh_data *h,
			char *array_name, u8 *array_id)
{
	int err = SCSI_DH_IO, i;
	struct c8_inquiry *inqp = &h->inq.c8;

	if (!scsi_get_vpd_page(sdev, 0xC8, (unsigned char *)inqp,
			       sizeof(struct c8_inquiry))) {
		if (inqp->page_code != 0xc8)
			return SCSI_DH_NOSYS;
		if (inqp->page_id[0] != 'e' || inqp->page_id[1] != 'd' ||
		    inqp->page_id[2] != 'i' || inqp->page_id[3] != 'd')
			return SCSI_DH_NOSYS;
		h->lun = inqp->lun[7]; /* Uses only the last byte */

		for(i=0; i<ARRAY_LABEL_LEN-1; ++i)
			*(array_name+i) = inqp->array_user_label[(2*i)+1];

		*(array_name+ARRAY_LABEL_LEN-1) = '\0';
		memset(array_id, 0, UNIQUE_ID_LEN);
		memcpy(array_id, inqp->array_unique_id, inqp->array_uniq_id_len);
		err = SCSI_DH_OK;
	}
	return err;
}

static int check_ownership(struct scsi_device *sdev, struct rdac_dh_data *h)
{
	int err = SCSI_DH_IO, access_state;
	struct rdac_dh_data *tmp;
	struct c9_inquiry *inqp = &h->inq.c9;

	h->state = RDAC_STATE_ACTIVE;
	if (!scsi_get_vpd_page(sdev, 0xC9, (unsigned char *)inqp,
			       sizeof(struct c9_inquiry))) {
		/* detect the operating mode */
		if ((inqp->avte_cvp >> 5) & 0x1)
			h->mode = RDAC_MODE_IOSHIP; /* LUN in IOSHIP mode */
		else if (inqp->avte_cvp >> 7)
			h->mode = RDAC_MODE_AVT; /* LUN in AVT mode */
		else
			h->mode = RDAC_MODE; /* LUN in RDAC mode */

		/* Update ownership */
		if (inqp->avte_cvp & 0x1) {
			h->lun_state = RDAC_LUN_OWNED;
			access_state = SCSI_ACCESS_STATE_OPTIMAL;
		} else {
			h->lun_state = RDAC_LUN_UNOWNED;
			if (h->mode == RDAC_MODE) {
				h->state = RDAC_STATE_PASSIVE;
				access_state = SCSI_ACCESS_STATE_STANDBY;
			} else
				access_state = SCSI_ACCESS_STATE_ACTIVE;
		}

		/* Update path prio*/
		if (inqp->path_prio & 0x1) {
			h->preferred = RDAC_PREFERRED;
			access_state |= SCSI_ACCESS_STATE_PREFERRED;
		} else
			h->preferred = RDAC_NON_PREFERRED;
		rcu_read_lock();
		list_for_each_entry_rcu(tmp, &h->ctlr->dh_list, node) {
			/* h->sdev should always be valid */
			BUG_ON(!tmp->sdev);
			tmp->sdev->access_state = access_state;
		}
		rcu_read_unlock();
		err = SCSI_DH_OK;
	}

	return err;
}

static int initialize_controller(struct scsi_device *sdev,
		struct rdac_dh_data *h, char *array_name, u8 *array_id)
{
	int err = SCSI_DH_IO, index;
	struct c4_inquiry *inqp = &h->inq.c4;

	if (!scsi_get_vpd_page(sdev, 0xC4, (unsigned char *)inqp,
			       sizeof(struct c4_inquiry))) {
		/* get the controller index */
		if (inqp->slot_id[1] == 0x31)
			index = 0;
		else
			index = 1;

		spin_lock(&list_lock);
		h->ctlr = get_controller(index, array_name, array_id, sdev);
		if (!h->ctlr)
			err = SCSI_DH_RES_TEMP_UNAVAIL;
		else {
			h->sdev = sdev;
			list_add_rcu(&h->node, &h->ctlr->dh_list);
		}
		spin_unlock(&list_lock);
		err = SCSI_DH_OK;
	}
	return err;
}

static int set_mode_select(struct scsi_device *sdev, struct rdac_dh_data *h)
{
	int err = SCSI_DH_IO;
	struct c2_inquiry *inqp = &h->inq.c2;

	if (!scsi_get_vpd_page(sdev, 0xC2, (unsigned char *)inqp,
			       sizeof(struct c2_inquiry))) {
		/*
		 * If more than MODE6_MAX_LUN luns are supported, use
		 * mode select 10
		 */
		if (inqp->max_lun_supported >= MODE6_MAX_LUN)
			h->ctlr->use_ms10 = 1;
		else
			h->ctlr->use_ms10 = 0;
		err = SCSI_DH_OK;
	}
	return err;
}

static int mode_select_handle_sense(struct scsi_device *sdev,
				    struct scsi_sense_hdr *sense_hdr)
{
	struct rdac_dh_data *h = sdev->handler_data;

	if (!scsi_sense_valid(sense_hdr))
		return SCSI_DH_IO;

	RDAC_LOG(RDAC_LOG_FAILOVER, sdev, "array %s, ctlr %d, "
		"MODE_SELECT returned with sense %02x/%02x/%02x",
		(char *) h->ctlr->array_name, h->ctlr->index,
		sense_hdr->sense_key, sense_hdr->asc, sense_hdr->ascq);

	return SCSI_DH_IO;
}

static void send_mode_select(struct work_struct *work)
{
	struct rdac_controller *ctlr =
		container_of(work, struct rdac_controller, ms_work);
	struct scsi_device *sdev = ctlr->ms_sdev;
	struct rdac_dh_data *h = sdev->handler_data;
	int rc, err;
	struct rdac_queue_data *tmp, *qdata;
	LIST_HEAD(list);
	unsigned char cdb[MAX_COMMAND_SIZE];
	struct scsi_sense_hdr sshdr;
	unsigned int data_size;
	blk_opf_t opf = REQ_OP_DRV_OUT | REQ_FAILFAST_DEV |
				REQ_FAILFAST_TRANSPORT | REQ_FAILFAST_DRIVER;
	struct scsi_failure failure_defs[] = {
		{
			.sense = NO_SENSE,
			.asc = SCMD_FAILURE_ASC_ANY,
			.ascq = SCMD_FAILURE_ASCQ_ANY,
			.result = SAM_STAT_CHECK_CONDITION,
		},
		{
			.sense = ABORTED_COMMAND,
			.asc = SCMD_FAILURE_ASC_ANY,
			.ascq = SCMD_FAILURE_ASCQ_ANY,
			.result = SAM_STAT_CHECK_CONDITION,
		},
		{
			.sense = UNIT_ATTENTION,
			.asc = SCMD_FAILURE_ASC_ANY,
			.ascq = SCMD_FAILURE_ASCQ_ANY,
			.result = SAM_STAT_CHECK_CONDITION,
		},
		/* LUN Not Ready and is in the Process of Becoming Ready */
		{
			.sense = NOT_READY,
			.asc = 0x04,
			.ascq = 0x01,
			.result = SAM_STAT_CHECK_CONDITION,
		},
		/* Command Lock contention */
		{
			.sense = ILLEGAL_REQUEST,
			.asc = 0x91,
			.ascq = 0x36,
			.allowed = SCMD_FAILURE_NO_LIMIT,
			.result = SAM_STAT_CHECK_CONDITION,
		},
		{}
	};
	struct scsi_failures failures = {
		.total_allowed = RDAC_RETRY_COUNT,
		.failure_definitions = failure_defs,
	};
	const struct scsi_exec_args exec_args = {
		.sshdr = &sshdr,
		.failures = &failures,
	};

	spin_lock(&ctlr->ms_lock);
	list_splice_init(&ctlr->ms_head, &list);
	ctlr->ms_queued = 0;
	ctlr->ms_sdev = NULL;
	spin_unlock(&ctlr->ms_lock);

	memset(cdb, 0, sizeof(cdb));

	data_size = rdac_failover_get(ctlr, &list, cdb);

	RDAC_LOG(RDAC_LOG_FAILOVER, sdev, "array %s, ctlr %d, queueing MODE_SELECT command",
		(char *)h->ctlr->array_name, h->ctlr->index);

	rc = scsi_execute_cmd(sdev, cdb, opf, &h->ctlr->mode_select, data_size,
			      RDAC_TIMEOUT * HZ, RDAC_RETRIES, &exec_args);
	if (!rc) {
		h->state = RDAC_STATE_ACTIVE;
		RDAC_LOG(RDAC_LOG_FAILOVER, sdev, "array %s, ctlr %d, "
				"MODE_SELECT completed",
				(char *) h->ctlr->array_name, h->ctlr->index);
		err = SCSI_DH_OK;
	} else if (rc < 0) {
		err = SCSI_DH_IO;
	} else {
		err = mode_select_handle_sense(sdev, &sshdr);
	}

	list_for_each_entry_safe(qdata, tmp, &list, entry) {
		list_del(&qdata->entry);
		if (err == SCSI_DH_OK)
			qdata->h->state = RDAC_STATE_ACTIVE;
		if (qdata->callback_fn)
			qdata->callback_fn(qdata->callback_data, err);
		kfree(qdata);
	}
	return;
}

static int queue_mode_select(struct scsi_device *sdev,
				activate_complete fn, void *data)
{
	struct rdac_queue_data *qdata;
	struct rdac_controller *ctlr;

	qdata = kzalloc(sizeof(*qdata), GFP_KERNEL);
	if (!qdata)
		return SCSI_DH_RETRY;

	qdata->h = sdev->handler_data;
	qdata->callback_fn = fn;
	qdata->callback_data = data;

	ctlr = qdata->h->ctlr;
	spin_lock(&ctlr->ms_lock);
	list_add_tail(&qdata->entry, &ctlr->ms_head);
	if (!ctlr->ms_queued) {
		ctlr->ms_queued = 1;
		ctlr->ms_sdev = sdev;
		queue_work(kmpath_rdacd, &ctlr->ms_work);
	}
	spin_unlock(&ctlr->ms_lock);
	return SCSI_DH_OK;
}

static int rdac_activate(struct scsi_device *sdev,
			activate_complete fn, void *data)
{
	struct rdac_dh_data *h = sdev->handler_data;
	int err = SCSI_DH_OK;
	int act = 0;

	err = check_ownership(sdev, h);
	if (err != SCSI_DH_OK)
		goto done;

	switch (h->mode) {
	case RDAC_MODE:
		if (h->lun_state == RDAC_LUN_UNOWNED)
			act = 1;
		break;
	case RDAC_MODE_IOSHIP:
		if ((h->lun_state == RDAC_LUN_UNOWNED) &&
		    (h->preferred == RDAC_PREFERRED))
			act = 1;
		break;
	default:
		break;
	}

	if (act) {
		err = queue_mode_select(sdev, fn, data);
		if (err == SCSI_DH_OK)
			return 0;
	}
done:
	if (fn)
		fn(data, err);
	return 0;
}

static blk_status_t rdac_prep_fn(struct scsi_device *sdev, struct request *req)
{
	struct rdac_dh_data *h = sdev->handler_data;

	if (h->state != RDAC_STATE_ACTIVE) {
		req->rq_flags |= RQF_QUIET;
		return BLK_STS_IOERR;
	}

	return BLK_STS_OK;
}

static enum scsi_disposition rdac_check_sense(struct scsi_device *sdev,
					      struct scsi_sense_hdr *sense_hdr)
{
	struct rdac_dh_data *h = sdev->handler_data;

	RDAC_LOG(RDAC_LOG_SENSE, sdev, "array %s, ctlr %d, "
			"I/O returned with sense %02x/%02x/%02x",
			(char *) h->ctlr->array_name, h->ctlr->index,
			sense_hdr->sense_key, sense_hdr->asc, sense_hdr->ascq);

	switch (sense_hdr->sense_key) {
	case NOT_READY:
		if (sense_hdr->asc == 0x04 && sense_hdr->ascq == 0x01)
			/* LUN Not Ready - Logical Unit Not Ready and is in
			* the process of becoming ready
			* Just retry.
			*/
			return ADD_TO_MLQUEUE;
		if (sense_hdr->asc == 0x04 && sense_hdr->ascq == 0x81)
			/* LUN Not Ready - Storage firmware incompatible
			 * Manual code synchonisation required.
			 *
			 * Nothing we can do here. Try to bypass the path.
			 */
			return SUCCESS;
		if (sense_hdr->asc == 0x04 && sense_hdr->ascq == 0xA1)
			/* LUN Not Ready - Quiescense in progress
			 *
			 * Just retry and wait.
			 */
			return ADD_TO_MLQUEUE;
		if (sense_hdr->asc == 0xA1  && sense_hdr->ascq == 0x02)
			/* LUN Not Ready - Quiescense in progress
			 * or has been achieved
			 * Just retry.
			 */
			return ADD_TO_MLQUEUE;
		break;
	case ILLEGAL_REQUEST:
		if (sense_hdr->asc == 0x94 && sense_hdr->ascq == 0x01) {
			/* Invalid Request - Current Logical Unit Ownership.
			 * Controller is not the current owner of the LUN,
			 * Fail the path, so that the other path be used.
			 */
			h->state = RDAC_STATE_PASSIVE;
			return SUCCESS;
		}
		break;
	case UNIT_ATTENTION:
		if (sense_hdr->asc == 0x29 && sense_hdr->ascq == 0x00)
			/*
			 * Power On, Reset, or Bus Device Reset, just retry.
			 */
			return ADD_TO_MLQUEUE;
		if (sense_hdr->asc == 0x8b && sense_hdr->ascq == 0x02)
			/*
			 * Quiescence in progress , just retry.
			 */
			return ADD_TO_MLQUEUE;
		break;
	}
	/* success just means we do not care what scsi-ml does */
	return SCSI_RETURN_NOT_HANDLED;
}

static int rdac_bus_attach(struct scsi_device *sdev)
{
	struct rdac_dh_data *h;
	int err;
	char array_name[ARRAY_LABEL_LEN];
	char array_id[UNIQUE_ID_LEN];

	h = kzalloc(sizeof(*h) , GFP_KERNEL);
	if (!h)
		return SCSI_DH_NOMEM;
	h->lun = UNINITIALIZED_LUN;
	h->state = RDAC_STATE_ACTIVE;

	err = get_lun_info(sdev, h, array_name, array_id);
	if (err != SCSI_DH_OK)
		goto failed;

	err = initialize_controller(sdev, h, array_name, array_id);
	if (err != SCSI_DH_OK)
		goto failed;

	err = check_ownership(sdev, h);
	if (err != SCSI_DH_OK)
		goto clean_ctlr;

	err = set_mode_select(sdev, h);
	if (err != SCSI_DH_OK)
		goto clean_ctlr;

	sdev_printk(KERN_NOTICE, sdev,
		    "%s: LUN %d (%s) (%s)\n",
		    RDAC_NAME, h->lun, mode[(int)h->mode],
		    lun_state[(int)h->lun_state]);

	sdev->handler_data = h;
	return SCSI_DH_OK;

clean_ctlr:
	spin_lock(&list_lock);
	kref_put(&h->ctlr->kref, release_controller);
	spin_unlock(&list_lock);

failed:
	kfree(h);
	return err;
}

static void rdac_bus_detach( struct scsi_device *sdev )
{
	struct rdac_dh_data *h = sdev->handler_data;

	if (h->ctlr && h->ctlr->ms_queued)
		flush_workqueue(kmpath_rdacd);

	spin_lock(&list_lock);
	if (h->ctlr) {
		list_del_rcu(&h->node);
		kref_put(&h->ctlr->kref, release_controller);
	}
	spin_unlock(&list_lock);
	sdev->handler_data = NULL;
	synchronize_rcu();
	kfree(h);
}

static struct scsi_device_handler rdac_dh = {
	.name = RDAC_NAME,
	.module = THIS_MODULE,
	.prep_fn = rdac_prep_fn,
	.check_sense = rdac_check_sense,
	.attach = rdac_bus_attach,
	.detach = rdac_bus_detach,
	.activate = rdac_activate,
};

static int __init rdac_init(void)
{
	int r;

	r = scsi_register_device_handler(&rdac_dh);
	if (r != 0) {
		printk(KERN_ERR "Failed to register scsi device handler.");
		goto done;
	}

	/*
	 * Create workqueue to handle mode selects for rdac
	 */
	kmpath_rdacd =
		alloc_ordered_workqueue("%s", WQ_MEM_RECLAIM, "kmpath_rdacd");
	if (!kmpath_rdacd) {
		scsi_unregister_device_handler(&rdac_dh);
		printk(KERN_ERR "kmpath_rdacd creation failed.\n");

		r = -EINVAL;
	}
done:
	return r;
}

static void __exit rdac_exit(void)
{
	destroy_workqueue(kmpath_rdacd);
	scsi_unregister_device_handler(&rdac_dh);
}

module_init(rdac_init);
module_exit(rdac_exit);

MODULE_DESCRIPTION("Multipath LSI/Engenio/NetApp E-Series RDAC driver");
MODULE_AUTHOR("Mike Christie, Chandra Seetharaman");
MODULE_VERSION("01.00.0000.0000");
MODULE_LICENSE("GPL");
