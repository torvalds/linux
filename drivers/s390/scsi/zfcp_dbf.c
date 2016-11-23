/*
 * zfcp device driver
 *
 * Debug traces for zfcp.
 *
 * Copyright IBM Corp. 2002, 2016
 */

#define KMSG_COMPONENT "zfcp"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <asm/debug.h>
#include "zfcp_dbf.h"
#include "zfcp_ext.h"
#include "zfcp_fc.h"

static u32 dbfsize = 4;

module_param(dbfsize, uint, 0400);
MODULE_PARM_DESC(dbfsize,
		 "number of pages for each debug feature area (default 4)");

static u32 dbflevel = 3;

module_param(dbflevel, uint, 0400);
MODULE_PARM_DESC(dbflevel,
		 "log level for each debug feature area "
		 "(default 3, range 0..6)");

static inline unsigned int zfcp_dbf_plen(unsigned int offset)
{
	return sizeof(struct zfcp_dbf_pay) + offset - ZFCP_DBF_PAY_MAX_REC;
}

static inline
void zfcp_dbf_pl_write(struct zfcp_dbf *dbf, void *data, u16 length, char *area,
		       u64 req_id)
{
	struct zfcp_dbf_pay *pl = &dbf->pay_buf;
	u16 offset = 0, rec_length;

	spin_lock(&dbf->pay_lock);
	memset(pl, 0, sizeof(*pl));
	pl->fsf_req_id = req_id;
	memcpy(pl->area, area, ZFCP_DBF_TAG_LEN);

	while (offset < length) {
		rec_length = min((u16) ZFCP_DBF_PAY_MAX_REC,
				 (u16) (length - offset));
		memcpy(pl->data, data + offset, rec_length);
		debug_event(dbf->pay, 1, pl, zfcp_dbf_plen(rec_length));

		offset += rec_length;
		pl->counter++;
	}

	spin_unlock(&dbf->pay_lock);
}

/**
 * zfcp_dbf_hba_fsf_res - trace event for fsf responses
 * @tag: tag indicating which kind of unsolicited status has been received
 * @req: request for which a response was received
 */
void zfcp_dbf_hba_fsf_res(char *tag, int level, struct zfcp_fsf_req *req)
{
	struct zfcp_dbf *dbf = req->adapter->dbf;
	struct fsf_qtcb_prefix *q_pref = &req->qtcb->prefix;
	struct fsf_qtcb_header *q_head = &req->qtcb->header;
	struct zfcp_dbf_hba *rec = &dbf->hba_buf;
	unsigned long flags;

	spin_lock_irqsave(&dbf->hba_lock, flags);
	memset(rec, 0, sizeof(*rec));

	memcpy(rec->tag, tag, ZFCP_DBF_TAG_LEN);
	rec->id = ZFCP_DBF_HBA_RES;
	rec->fsf_req_id = req->req_id;
	rec->fsf_req_status = req->status;
	rec->fsf_cmd = req->fsf_command;
	rec->fsf_seq_no = req->seq_no;
	rec->u.res.req_issued = req->issued;
	rec->u.res.prot_status = q_pref->prot_status;
	rec->u.res.fsf_status = q_head->fsf_status;
	rec->u.res.port_handle = q_head->port_handle;
	rec->u.res.lun_handle = q_head->lun_handle;

	memcpy(rec->u.res.prot_status_qual, &q_pref->prot_status_qual,
	       FSF_PROT_STATUS_QUAL_SIZE);
	memcpy(rec->u.res.fsf_status_qual, &q_head->fsf_status_qual,
	       FSF_STATUS_QUALIFIER_SIZE);

	if (req->fsf_command != FSF_QTCB_FCP_CMND) {
		rec->pl_len = q_head->log_length;
		zfcp_dbf_pl_write(dbf, (char *)q_pref + q_head->log_start,
				  rec->pl_len, "fsf_res", req->req_id);
	}

	debug_event(dbf->hba, level, rec, sizeof(*rec));
	spin_unlock_irqrestore(&dbf->hba_lock, flags);
}

/**
 * zfcp_dbf_hba_fsf_uss - trace event for an unsolicited status buffer
 * @tag: tag indicating which kind of unsolicited status has been received
 * @req: request providing the unsolicited status
 */
void zfcp_dbf_hba_fsf_uss(char *tag, struct zfcp_fsf_req *req)
{
	struct zfcp_dbf *dbf = req->adapter->dbf;
	struct fsf_status_read_buffer *srb = req->data;
	struct zfcp_dbf_hba *rec = &dbf->hba_buf;
	unsigned long flags;

	spin_lock_irqsave(&dbf->hba_lock, flags);
	memset(rec, 0, sizeof(*rec));

	memcpy(rec->tag, tag, ZFCP_DBF_TAG_LEN);
	rec->id = ZFCP_DBF_HBA_USS;
	rec->fsf_req_id = req->req_id;
	rec->fsf_req_status = req->status;
	rec->fsf_cmd = req->fsf_command;

	if (!srb)
		goto log;

	rec->u.uss.status_type = srb->status_type;
	rec->u.uss.status_subtype = srb->status_subtype;
	rec->u.uss.d_id = ntoh24(srb->d_id);
	rec->u.uss.lun = srb->fcp_lun;
	memcpy(&rec->u.uss.queue_designator, &srb->queue_designator,
	       sizeof(rec->u.uss.queue_designator));

	/* status read buffer payload length */
	rec->pl_len = (!srb->length) ? 0 : srb->length -
			offsetof(struct fsf_status_read_buffer, payload);

	if (rec->pl_len)
		zfcp_dbf_pl_write(dbf, srb->payload.data, rec->pl_len,
				  "fsf_uss", req->req_id);
log:
	debug_event(dbf->hba, 2, rec, sizeof(*rec));
	spin_unlock_irqrestore(&dbf->hba_lock, flags);
}

/**
 * zfcp_dbf_hba_bit_err - trace event for bit error conditions
 * @tag: tag indicating which kind of unsolicited status has been received
 * @req: request which caused the bit_error condition
 */
void zfcp_dbf_hba_bit_err(char *tag, struct zfcp_fsf_req *req)
{
	struct zfcp_dbf *dbf = req->adapter->dbf;
	struct zfcp_dbf_hba *rec = &dbf->hba_buf;
	struct fsf_status_read_buffer *sr_buf = req->data;
	unsigned long flags;

	spin_lock_irqsave(&dbf->hba_lock, flags);
	memset(rec, 0, sizeof(*rec));

	memcpy(rec->tag, tag, ZFCP_DBF_TAG_LEN);
	rec->id = ZFCP_DBF_HBA_BIT;
	rec->fsf_req_id = req->req_id;
	rec->fsf_req_status = req->status;
	rec->fsf_cmd = req->fsf_command;
	memcpy(&rec->u.be, &sr_buf->payload.bit_error,
	       sizeof(struct fsf_bit_error_payload));

	debug_event(dbf->hba, 1, rec, sizeof(*rec));
	spin_unlock_irqrestore(&dbf->hba_lock, flags);
}

/**
 * zfcp_dbf_hba_def_err - trace event for deferred error messages
 * @adapter: pointer to struct zfcp_adapter
 * @req_id: request id which caused the deferred error message
 * @scount: number of sbals incl. the signaling sbal
 * @pl: array of all involved sbals
 */
void zfcp_dbf_hba_def_err(struct zfcp_adapter *adapter, u64 req_id, u16 scount,
			  void **pl)
{
	struct zfcp_dbf *dbf = adapter->dbf;
	struct zfcp_dbf_pay *payload = &dbf->pay_buf;
	unsigned long flags;
	u16 length;

	if (!pl)
		return;

	spin_lock_irqsave(&dbf->pay_lock, flags);
	memset(payload, 0, sizeof(*payload));

	memcpy(payload->area, "def_err", 7);
	payload->fsf_req_id = req_id;
	payload->counter = 0;
	length = min((u16)sizeof(struct qdio_buffer),
		     (u16)ZFCP_DBF_PAY_MAX_REC);

	while (payload->counter < scount && (char *)pl[payload->counter]) {
		memcpy(payload->data, (char *)pl[payload->counter], length);
		debug_event(dbf->pay, 1, payload, zfcp_dbf_plen(length));
		payload->counter++;
	}

	spin_unlock_irqrestore(&dbf->pay_lock, flags);
}

/**
 * zfcp_dbf_hba_basic - trace event for basic adapter events
 * @adapter: pointer to struct zfcp_adapter
 */
void zfcp_dbf_hba_basic(char *tag, struct zfcp_adapter *adapter)
{
	struct zfcp_dbf *dbf = adapter->dbf;
	struct zfcp_dbf_hba *rec = &dbf->hba_buf;
	unsigned long flags;

	spin_lock_irqsave(&dbf->hba_lock, flags);
	memset(rec, 0, sizeof(*rec));

	memcpy(rec->tag, tag, ZFCP_DBF_TAG_LEN);
	rec->id = ZFCP_DBF_HBA_BASIC;

	debug_event(dbf->hba, 1, rec, sizeof(*rec));
	spin_unlock_irqrestore(&dbf->hba_lock, flags);
}

static void zfcp_dbf_set_common(struct zfcp_dbf_rec *rec,
				struct zfcp_adapter *adapter,
				struct zfcp_port *port,
				struct scsi_device *sdev)
{
	rec->adapter_status = atomic_read(&adapter->status);
	if (port) {
		rec->port_status = atomic_read(&port->status);
		rec->wwpn = port->wwpn;
		rec->d_id = port->d_id;
	}
	if (sdev) {
		rec->lun_status = atomic_read(&sdev_to_zfcp(sdev)->status);
		rec->lun = zfcp_scsi_dev_lun(sdev);
	} else
		rec->lun = ZFCP_DBF_INVALID_LUN;
}

/**
 * zfcp_dbf_rec_trig - trace event related to triggered recovery
 * @tag: identifier for event
 * @adapter: adapter on which the erp_action should run
 * @port: remote port involved in the erp_action
 * @sdev: scsi device involved in the erp_action
 * @want: wanted erp_action
 * @need: required erp_action
 *
 * The adapter->erp_lock has to be held.
 */
void zfcp_dbf_rec_trig(char *tag, struct zfcp_adapter *adapter,
		       struct zfcp_port *port, struct scsi_device *sdev,
		       u8 want, u8 need)
{
	struct zfcp_dbf *dbf = adapter->dbf;
	struct zfcp_dbf_rec *rec = &dbf->rec_buf;
	struct list_head *entry;
	unsigned long flags;

	spin_lock_irqsave(&dbf->rec_lock, flags);
	memset(rec, 0, sizeof(*rec));

	rec->id = ZFCP_DBF_REC_TRIG;
	memcpy(rec->tag, tag, ZFCP_DBF_TAG_LEN);
	zfcp_dbf_set_common(rec, adapter, port, sdev);

	list_for_each(entry, &adapter->erp_ready_head)
		rec->u.trig.ready++;

	list_for_each(entry, &adapter->erp_running_head)
		rec->u.trig.running++;

	rec->u.trig.want = want;
	rec->u.trig.need = need;

	debug_event(dbf->rec, 1, rec, sizeof(*rec));
	spin_unlock_irqrestore(&dbf->rec_lock, flags);
}


/**
 * zfcp_dbf_rec_run - trace event related to running recovery
 * @tag: identifier for event
 * @erp: erp_action running
 */
void zfcp_dbf_rec_run(char *tag, struct zfcp_erp_action *erp)
{
	struct zfcp_dbf *dbf = erp->adapter->dbf;
	struct zfcp_dbf_rec *rec = &dbf->rec_buf;
	unsigned long flags;

	spin_lock_irqsave(&dbf->rec_lock, flags);
	memset(rec, 0, sizeof(*rec));

	rec->id = ZFCP_DBF_REC_RUN;
	memcpy(rec->tag, tag, ZFCP_DBF_TAG_LEN);
	zfcp_dbf_set_common(rec, erp->adapter, erp->port, erp->sdev);

	rec->u.run.fsf_req_id = erp->fsf_req_id;
	rec->u.run.rec_status = erp->status;
	rec->u.run.rec_step = erp->step;
	rec->u.run.rec_action = erp->action;

	if (erp->sdev)
		rec->u.run.rec_count =
			atomic_read(&sdev_to_zfcp(erp->sdev)->erp_counter);
	else if (erp->port)
		rec->u.run.rec_count = atomic_read(&erp->port->erp_counter);
	else
		rec->u.run.rec_count = atomic_read(&erp->adapter->erp_counter);

	debug_event(dbf->rec, 1, rec, sizeof(*rec));
	spin_unlock_irqrestore(&dbf->rec_lock, flags);
}

/**
 * zfcp_dbf_rec_run_wka - trace wka port event with info like running recovery
 * @tag: identifier for event
 * @wka_port: well known address port
 * @req_id: request ID to correlate with potential HBA trace record
 */
void zfcp_dbf_rec_run_wka(char *tag, struct zfcp_fc_wka_port *wka_port,
			  u64 req_id)
{
	struct zfcp_dbf *dbf = wka_port->adapter->dbf;
	struct zfcp_dbf_rec *rec = &dbf->rec_buf;
	unsigned long flags;

	spin_lock_irqsave(&dbf->rec_lock, flags);
	memset(rec, 0, sizeof(*rec));

	rec->id = ZFCP_DBF_REC_RUN;
	memcpy(rec->tag, tag, ZFCP_DBF_TAG_LEN);
	rec->port_status = wka_port->status;
	rec->d_id = wka_port->d_id;
	rec->lun = ZFCP_DBF_INVALID_LUN;

	rec->u.run.fsf_req_id = req_id;
	rec->u.run.rec_status = ~0;
	rec->u.run.rec_step = ~0;
	rec->u.run.rec_action = ~0;
	rec->u.run.rec_count = ~0;

	debug_event(dbf->rec, 1, rec, sizeof(*rec));
	spin_unlock_irqrestore(&dbf->rec_lock, flags);
}

static inline
void zfcp_dbf_san(char *tag, struct zfcp_dbf *dbf,
		  char *paytag, struct scatterlist *sg, u8 id, u16 len,
		  u64 req_id, u32 d_id, u16 cap_len)
{
	struct zfcp_dbf_san *rec = &dbf->san_buf;
	u16 rec_len;
	unsigned long flags;
	struct zfcp_dbf_pay *payload = &dbf->pay_buf;
	u16 pay_sum = 0;

	spin_lock_irqsave(&dbf->san_lock, flags);
	memset(rec, 0, sizeof(*rec));

	rec->id = id;
	rec->fsf_req_id = req_id;
	rec->d_id = d_id;
	memcpy(rec->tag, tag, ZFCP_DBF_TAG_LEN);
	rec->pl_len = len; /* full length even if we cap pay below */
	if (!sg)
		goto out;
	rec_len = min_t(unsigned int, sg->length, ZFCP_DBF_SAN_MAX_PAYLOAD);
	memcpy(rec->payload, sg_virt(sg), rec_len); /* part of 1st sg entry */
	if (len <= rec_len)
		goto out; /* skip pay record if full content in rec->payload */

	/* if (len > rec_len):
	 * dump data up to cap_len ignoring small duplicate in rec->payload
	 */
	spin_lock(&dbf->pay_lock);
	memset(payload, 0, sizeof(*payload));
	memcpy(payload->area, paytag, ZFCP_DBF_TAG_LEN);
	payload->fsf_req_id = req_id;
	payload->counter = 0;
	for (; sg && pay_sum < cap_len; sg = sg_next(sg)) {
		u16 pay_len, offset = 0;

		while (offset < sg->length && pay_sum < cap_len) {
			pay_len = min((u16)ZFCP_DBF_PAY_MAX_REC,
				      (u16)(sg->length - offset));
			/* cap_len <= pay_sum < cap_len+ZFCP_DBF_PAY_MAX_REC */
			memcpy(payload->data, sg_virt(sg) + offset, pay_len);
			debug_event(dbf->pay, 1, payload,
				    zfcp_dbf_plen(pay_len));
			payload->counter++;
			offset += pay_len;
			pay_sum += pay_len;
		}
	}
	spin_unlock(&dbf->pay_lock);

out:
	debug_event(dbf->san, 1, rec, sizeof(*rec));
	spin_unlock_irqrestore(&dbf->san_lock, flags);
}

/**
 * zfcp_dbf_san_req - trace event for issued SAN request
 * @tag: identifier for event
 * @fsf_req: request containing issued CT data
 * d_id: destination ID
 */
void zfcp_dbf_san_req(char *tag, struct zfcp_fsf_req *fsf, u32 d_id)
{
	struct zfcp_dbf *dbf = fsf->adapter->dbf;
	struct zfcp_fsf_ct_els *ct_els = fsf->data;
	u16 length;

	length = (u16)zfcp_qdio_real_bytes(ct_els->req);
	zfcp_dbf_san(tag, dbf, "san_req", ct_els->req, ZFCP_DBF_SAN_REQ,
		     length, fsf->req_id, d_id, length);
}

static u16 zfcp_dbf_san_res_cap_len_if_gpn_ft(char *tag,
					      struct zfcp_fsf_req *fsf,
					      u16 len)
{
	struct zfcp_fsf_ct_els *ct_els = fsf->data;
	struct fc_ct_hdr *reqh = sg_virt(ct_els->req);
	struct fc_ns_gid_ft *reqn = (struct fc_ns_gid_ft *)(reqh + 1);
	struct scatterlist *resp_entry = ct_els->resp;
	struct fc_gpn_ft_resp *acc;
	int max_entries, x, last = 0;

	if (!(memcmp(tag, "fsscth2", 7) == 0
	      && ct_els->d_id == FC_FID_DIR_SERV
	      && reqh->ct_rev == FC_CT_REV
	      && reqh->ct_in_id[0] == 0
	      && reqh->ct_in_id[1] == 0
	      && reqh->ct_in_id[2] == 0
	      && reqh->ct_fs_type == FC_FST_DIR
	      && reqh->ct_fs_subtype == FC_NS_SUBTYPE
	      && reqh->ct_options == 0
	      && reqh->_ct_resvd1 == 0
	      && reqh->ct_cmd == FC_NS_GPN_FT
	      /* reqh->ct_mr_size can vary so do not match but read below */
	      && reqh->_ct_resvd2 == 0
	      && reqh->ct_reason == 0
	      && reqh->ct_explan == 0
	      && reqh->ct_vendor == 0
	      && reqn->fn_resvd == 0
	      && reqn->fn_domain_id_scope == 0
	      && reqn->fn_area_id_scope == 0
	      && reqn->fn_fc4_type == FC_TYPE_FCP))
		return len; /* not GPN_FT response so do not cap */

	acc = sg_virt(resp_entry);
	max_entries = (reqh->ct_mr_size * 4 / sizeof(struct fc_gpn_ft_resp))
		+ 1 /* zfcp_fc_scan_ports: bytes correct, entries off-by-one
		     * to account for header as 1st pseudo "entry" */;

	/* the basic CT_IU preamble is the same size as one entry in the GPN_FT
	 * response, allowing us to skip special handling for it - just skip it
	 */
	for (x = 1; x < max_entries && !last; x++) {
		if (x % (ZFCP_FC_GPN_FT_ENT_PAGE + 1))
			acc++;
		else
			acc = sg_virt(++resp_entry);

		last = acc->fp_flags & FC_NS_FID_LAST;
	}
	len = min(len, (u16)(x * sizeof(struct fc_gpn_ft_resp)));
	return len; /* cap after last entry */
}

/**
 * zfcp_dbf_san_res - trace event for received SAN request
 * @tag: identifier for event
 * @fsf_req: request containing issued CT data
 */
void zfcp_dbf_san_res(char *tag, struct zfcp_fsf_req *fsf)
{
	struct zfcp_dbf *dbf = fsf->adapter->dbf;
	struct zfcp_fsf_ct_els *ct_els = fsf->data;
	u16 length;

	length = (u16)zfcp_qdio_real_bytes(ct_els->resp);
	zfcp_dbf_san(tag, dbf, "san_res", ct_els->resp, ZFCP_DBF_SAN_RES,
		     length, fsf->req_id, ct_els->d_id,
		     zfcp_dbf_san_res_cap_len_if_gpn_ft(tag, fsf, length));
}

/**
 * zfcp_dbf_san_in_els - trace event for incoming ELS
 * @tag: identifier for event
 * @fsf_req: request containing issued CT data
 */
void zfcp_dbf_san_in_els(char *tag, struct zfcp_fsf_req *fsf)
{
	struct zfcp_dbf *dbf = fsf->adapter->dbf;
	struct fsf_status_read_buffer *srb =
		(struct fsf_status_read_buffer *) fsf->data;
	u16 length;
	struct scatterlist sg;

	length = (u16)(srb->length -
			offsetof(struct fsf_status_read_buffer, payload));
	sg_init_one(&sg, srb->payload.data, length);
	zfcp_dbf_san(tag, dbf, "san_els", &sg, ZFCP_DBF_SAN_ELS, length,
		     fsf->req_id, ntoh24(srb->d_id), length);
}

/**
 * zfcp_dbf_scsi - trace event for scsi commands
 * @tag: identifier for event
 * @sc: pointer to struct scsi_cmnd
 * @fsf: pointer to struct zfcp_fsf_req
 */
void zfcp_dbf_scsi(char *tag, int level, struct scsi_cmnd *sc,
		   struct zfcp_fsf_req *fsf)
{
	struct zfcp_adapter *adapter =
		(struct zfcp_adapter *) sc->device->host->hostdata[0];
	struct zfcp_dbf *dbf = adapter->dbf;
	struct zfcp_dbf_scsi *rec = &dbf->scsi_buf;
	struct fcp_resp_with_ext *fcp_rsp;
	struct fcp_resp_rsp_info *fcp_rsp_info;
	unsigned long flags;

	spin_lock_irqsave(&dbf->scsi_lock, flags);
	memset(rec, 0, sizeof(*rec));

	memcpy(rec->tag, tag, ZFCP_DBF_TAG_LEN);
	rec->id = ZFCP_DBF_SCSI_CMND;
	rec->scsi_result = sc->result;
	rec->scsi_retries = sc->retries;
	rec->scsi_allowed = sc->allowed;
	rec->scsi_id = sc->device->id;
	/* struct zfcp_dbf_scsi needs to be updated to handle 64bit LUNs */
	rec->scsi_lun = (u32)sc->device->lun;
	rec->host_scribble = (unsigned long)sc->host_scribble;

	memcpy(rec->scsi_opcode, sc->cmnd,
	       min((int)sc->cmd_len, ZFCP_DBF_SCSI_OPCODE));

	if (fsf) {
		rec->fsf_req_id = fsf->req_id;
		fcp_rsp = (struct fcp_resp_with_ext *)
				&(fsf->qtcb->bottom.io.fcp_rsp);
		memcpy(&rec->fcp_rsp, fcp_rsp, FCP_RESP_WITH_EXT);
		if (fcp_rsp->resp.fr_flags & FCP_RSP_LEN_VAL) {
			fcp_rsp_info = (struct fcp_resp_rsp_info *) &fcp_rsp[1];
			rec->fcp_rsp_info = fcp_rsp_info->rsp_code;
		}
		if (fcp_rsp->resp.fr_flags & FCP_SNS_LEN_VAL) {
			rec->pl_len = min((u16)SCSI_SENSE_BUFFERSIZE,
					  (u16)ZFCP_DBF_PAY_MAX_REC);
			zfcp_dbf_pl_write(dbf, sc->sense_buffer, rec->pl_len,
					  "fcp_sns", fsf->req_id);
		}
	}

	debug_event(dbf->scsi, level, rec, sizeof(*rec));
	spin_unlock_irqrestore(&dbf->scsi_lock, flags);
}

static debug_info_t *zfcp_dbf_reg(const char *name, int size, int rec_size)
{
	struct debug_info *d;

	d = debug_register(name, size, 1, rec_size);
	if (!d)
		return NULL;

	debug_register_view(d, &debug_hex_ascii_view);
	debug_set_level(d, dbflevel);

	return d;
}

static void zfcp_dbf_unregister(struct zfcp_dbf *dbf)
{
	if (!dbf)
		return;

	debug_unregister(dbf->scsi);
	debug_unregister(dbf->san);
	debug_unregister(dbf->hba);
	debug_unregister(dbf->pay);
	debug_unregister(dbf->rec);
	kfree(dbf);
}

/**
 * zfcp_adapter_debug_register - registers debug feature for an adapter
 * @adapter: pointer to adapter for which debug features should be registered
 * return: -ENOMEM on error, 0 otherwise
 */
int zfcp_dbf_adapter_register(struct zfcp_adapter *adapter)
{
	char name[DEBUG_MAX_NAME_LEN];
	struct zfcp_dbf *dbf;

	dbf = kzalloc(sizeof(struct zfcp_dbf), GFP_KERNEL);
	if (!dbf)
		return -ENOMEM;

	spin_lock_init(&dbf->pay_lock);
	spin_lock_init(&dbf->hba_lock);
	spin_lock_init(&dbf->san_lock);
	spin_lock_init(&dbf->scsi_lock);
	spin_lock_init(&dbf->rec_lock);

	/* debug feature area which records recovery activity */
	sprintf(name, "zfcp_%s_rec", dev_name(&adapter->ccw_device->dev));
	dbf->rec = zfcp_dbf_reg(name, dbfsize, sizeof(struct zfcp_dbf_rec));
	if (!dbf->rec)
		goto err_out;

	/* debug feature area which records HBA (FSF and QDIO) conditions */
	sprintf(name, "zfcp_%s_hba", dev_name(&adapter->ccw_device->dev));
	dbf->hba = zfcp_dbf_reg(name, dbfsize, sizeof(struct zfcp_dbf_hba));
	if (!dbf->hba)
		goto err_out;

	/* debug feature area which records payload info */
	sprintf(name, "zfcp_%s_pay", dev_name(&adapter->ccw_device->dev));
	dbf->pay = zfcp_dbf_reg(name, dbfsize * 2, sizeof(struct zfcp_dbf_pay));
	if (!dbf->pay)
		goto err_out;

	/* debug feature area which records SAN command failures and recovery */
	sprintf(name, "zfcp_%s_san", dev_name(&adapter->ccw_device->dev));
	dbf->san = zfcp_dbf_reg(name, dbfsize, sizeof(struct zfcp_dbf_san));
	if (!dbf->san)
		goto err_out;

	/* debug feature area which records SCSI command failures and recovery */
	sprintf(name, "zfcp_%s_scsi", dev_name(&adapter->ccw_device->dev));
	dbf->scsi = zfcp_dbf_reg(name, dbfsize, sizeof(struct zfcp_dbf_scsi));
	if (!dbf->scsi)
		goto err_out;

	adapter->dbf = dbf;

	return 0;
err_out:
	zfcp_dbf_unregister(dbf);
	return -ENOMEM;
}

/**
 * zfcp_adapter_debug_unregister - unregisters debug feature for an adapter
 * @adapter: pointer to adapter for which debug features should be unregistered
 */
void zfcp_dbf_adapter_unregister(struct zfcp_adapter *adapter)
{
	struct zfcp_dbf *dbf = adapter->dbf;

	adapter->dbf = NULL;
	zfcp_dbf_unregister(dbf);
}

