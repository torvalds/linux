/*
 * zfcp device driver
 *
 * Debug traces for zfcp.
 *
 * Copyright IBM Corporation 2002, 2009
 */

#define KMSG_COMPONENT "zfcp"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

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

static void zfcp_dbf_tag(char **p, const char *label, const char *tag)
{
	int i;

	*p += sprintf(*p, "%-24s", label);
	for (i = 0; i < ZFCP_DBF_TAG_SIZE; i++)
		*p += sprintf(*p, "%c", tag[i]);
	*p += sprintf(*p, "\n");
}

static void zfcp_dbf_out(char **buf, const char *s, const char *format, ...)
{
	va_list arg;

	*buf += sprintf(*buf, "%-24s", s);
	va_start(arg, format);
	*buf += vsprintf(*buf, format, arg);
	va_end(arg);
	*buf += sprintf(*buf, "\n");
}

static void zfcp_dbf_outd(char **p, const char *label, char *buffer,
			  int buflen, int offset, int total_size)
{
	if (!offset)
		*p += sprintf(*p, "%-24s  ", label);
	while (buflen--) {
		if (offset > 0) {
			if ((offset % 32) == 0)
				*p += sprintf(*p, "\n%-24c  ", ' ');
			else if ((offset % 4) == 0)
				*p += sprintf(*p, " ");
		}
		*p += sprintf(*p, "%02x", *buffer++);
		if (++offset == total_size) {
			*p += sprintf(*p, "\n");
			break;
		}
	}
	if (!total_size)
		*p += sprintf(*p, "\n");
}

static int zfcp_dbf_view_header(debug_info_t *id, struct debug_view *view,
				int area, debug_entry_t *entry, char *out_buf)
{
	struct zfcp_dbf_dump *dump = (struct zfcp_dbf_dump *)DEBUG_DATA(entry);
	struct timespec t;
	char *p = out_buf;

	if (strncmp(dump->tag, "dump", ZFCP_DBF_TAG_SIZE) != 0) {
		stck_to_timespec(entry->id.stck, &t);
		zfcp_dbf_out(&p, "timestamp", "%011lu:%06lu",
			     t.tv_sec, t.tv_nsec);
		zfcp_dbf_out(&p, "cpu", "%02i", entry->id.fields.cpuid);
	} else	{
		zfcp_dbf_outd(&p, "", dump->data, dump->size, dump->offset,
			      dump->total_size);
		if ((dump->offset + dump->size) == dump->total_size)
			p += sprintf(p, "\n");
	}
	return p - out_buf;
}

/**
 * zfcp_dbf_hba_fsf_res - trace event for fsf responses
 * @tag: tag indicating which kind of unsolicited status has been received
 * @req: request for which a response was received
 */
void zfcp_dbf_hba_fsf_res(char *tag, struct zfcp_fsf_req *req)
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

	memcpy(rec->u.res.prot_status_qual, &q_pref->prot_status_qual,
	       FSF_PROT_STATUS_QUAL_SIZE);
	memcpy(rec->u.res.fsf_status_qual, &q_head->fsf_status_qual,
	       FSF_STATUS_QUALIFIER_SIZE);

	if (req->fsf_command != FSF_QTCB_FCP_CMND) {
		rec->pl_len = q_head->log_length;
		zfcp_dbf_pl_write(dbf, (char *)q_pref + q_head->log_start,
				  rec->pl_len, "fsf_res", req->req_id);
	}

	debug_event(dbf->hba, 1, rec, sizeof(*rec));
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
	}
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

static inline
void zfcp_dbf_san(char *tag, struct zfcp_dbf *dbf, void *data, u8 id, u16 len,
		  u64 req_id, u32 d_id)
{
	struct zfcp_dbf_san *rec = &dbf->san_buf;
	u16 rec_len;
	unsigned long flags;

	spin_lock_irqsave(&dbf->san_lock, flags);
	memset(rec, 0, sizeof(*rec));

	rec->id = id;
	rec->fsf_req_id = req_id;
	rec->d_id = d_id;
	rec_len = min(len, (u16)ZFCP_DBF_SAN_MAX_PAYLOAD);
	memcpy(rec->payload, data, rec_len);
	memcpy(rec->tag, tag, ZFCP_DBF_TAG_LEN);

	debug_event(dbf->san, 1, rec, sizeof(*rec));
	spin_unlock_irqrestore(&dbf->san_lock, flags);
}

/**
 * zfcp_dbf_san_req - trace event for issued SAN request
 * @tag: indentifier for event
 * @fsf_req: request containing issued CT data
 * d_id: destination ID
 */
void zfcp_dbf_san_req(char *tag, struct zfcp_fsf_req *fsf, u32 d_id)
{
	struct zfcp_dbf *dbf = fsf->adapter->dbf;
	struct zfcp_fsf_ct_els *ct_els = fsf->data;
	u16 length;

	length = (u16)(ct_els->req->length + FC_CT_HDR_LEN);
	zfcp_dbf_san(tag, dbf, sg_virt(ct_els->req), ZFCP_DBF_SAN_REQ, length,
		     fsf->req_id, d_id);
}

/**
 * zfcp_dbf_san_res - trace event for received SAN request
 * @tag: indentifier for event
 * @fsf_req: request containing issued CT data
 */
void zfcp_dbf_san_res(char *tag, struct zfcp_fsf_req *fsf)
{
	struct zfcp_dbf *dbf = fsf->adapter->dbf;
	struct zfcp_fsf_ct_els *ct_els = fsf->data;
	u16 length;

	length = (u16)(ct_els->resp->length + FC_CT_HDR_LEN);
	zfcp_dbf_san(tag, dbf, sg_virt(ct_els->resp), ZFCP_DBF_SAN_RES, length,
		     fsf->req_id, 0);
}

/**
 * zfcp_dbf_san_in_els - trace event for incoming ELS
 * @tag: indentifier for event
 * @fsf_req: request containing issued CT data
 */
void zfcp_dbf_san_in_els(char *tag, struct zfcp_fsf_req *fsf)
{
	struct zfcp_dbf *dbf = fsf->adapter->dbf;
	struct fsf_status_read_buffer *srb =
		(struct fsf_status_read_buffer *) fsf->data;
	u16 length;

	length = (u16)(srb->length -
			offsetof(struct fsf_status_read_buffer, payload));
	zfcp_dbf_san(tag, dbf, srb->payload.data, ZFCP_DBF_SAN_ELS, length,
		     fsf->req_id, ntoh24(srb->d_id));
}

void _zfcp_dbf_scsi(const char *tag, const char *tag2, int level,
		    struct zfcp_dbf *dbf, struct scsi_cmnd *scsi_cmnd,
		    struct zfcp_fsf_req *fsf_req, unsigned long old_req_id)
{
	struct zfcp_dbf_scsi_record *rec = &dbf->scsi_buf;
	struct zfcp_dbf_dump *dump = (struct zfcp_dbf_dump *)rec;
	unsigned long flags;
	struct fcp_resp_with_ext *fcp_rsp;
	struct fcp_resp_rsp_info *fcp_rsp_info = NULL;
	char *fcp_sns_info = NULL;
	int offset = 0, buflen = 0;

	spin_lock_irqsave(&dbf->scsi_lock, flags);
	do {
		memset(rec, 0, sizeof(*rec));
		if (offset == 0) {
			strncpy(rec->tag, tag, ZFCP_DBF_TAG_SIZE);
			strncpy(rec->tag2, tag2, ZFCP_DBF_TAG_SIZE);
			if (scsi_cmnd != NULL) {
				if (scsi_cmnd->device) {
					rec->scsi_id = scsi_cmnd->device->id;
					rec->scsi_lun = scsi_cmnd->device->lun;
				}
				rec->scsi_result = scsi_cmnd->result;
				rec->scsi_cmnd = (unsigned long)scsi_cmnd;
				memcpy(rec->scsi_opcode, scsi_cmnd->cmnd,
					min((int)scsi_cmnd->cmd_len,
						ZFCP_DBF_SCSI_OPCODE));
				rec->scsi_retries = scsi_cmnd->retries;
				rec->scsi_allowed = scsi_cmnd->allowed;
			}
			if (fsf_req != NULL) {
				fcp_rsp = (struct fcp_resp_with_ext *)
					&(fsf_req->qtcb->bottom.io.fcp_rsp);
				fcp_rsp_info = (struct fcp_resp_rsp_info *)
					&fcp_rsp[1];
				fcp_sns_info = (char *) &fcp_rsp[1];
				if (fcp_rsp->resp.fr_flags & FCP_RSP_LEN_VAL)
					fcp_sns_info += fcp_rsp->ext.fr_sns_len;

				rec->rsp_validity = fcp_rsp->resp.fr_flags;
				rec->rsp_scsi_status = fcp_rsp->resp.fr_status;
				rec->rsp_resid = fcp_rsp->ext.fr_resid;
				if (fcp_rsp->resp.fr_flags & FCP_RSP_LEN_VAL)
					rec->rsp_code = fcp_rsp_info->rsp_code;
				if (fcp_rsp->resp.fr_flags & FCP_SNS_LEN_VAL) {
					buflen = min(fcp_rsp->ext.fr_sns_len,
					   (u32)ZFCP_DBF_SCSI_MAX_FCP_SNS_INFO);
					rec->sns_info_len = buflen;
					memcpy(rec->sns_info, fcp_sns_info,
					       min(buflen,
						   ZFCP_DBF_SCSI_FCP_SNS_INFO));
					offset += min(buflen,
						      ZFCP_DBF_SCSI_FCP_SNS_INFO);
				}

				rec->fsf_reqid = fsf_req->req_id;
				rec->fsf_seqno = fsf_req->seq_no;
				rec->fsf_issued = fsf_req->issued;
			}
			rec->old_fsf_reqid = old_req_id;
		} else {
			strncpy(dump->tag, "dump", ZFCP_DBF_TAG_SIZE);
			dump->total_size = buflen;
			dump->offset = offset;
			dump->size = min(buflen - offset,
					 (int)sizeof(struct
						     zfcp_dbf_scsi_record) -
					 (int)sizeof(struct zfcp_dbf_dump));
			memcpy(dump->data, fcp_sns_info + offset, dump->size);
			offset += dump->size;
		}
		debug_event(dbf->scsi, level, rec, sizeof(*rec));
	} while (offset < buflen);
	spin_unlock_irqrestore(&dbf->scsi_lock, flags);
}

static int zfcp_dbf_scsi_view_format(debug_info_t *id, struct debug_view *view,
				     char *out_buf, const char *in_buf)
{
	struct zfcp_dbf_scsi_record *r = (struct zfcp_dbf_scsi_record *)in_buf;
	struct timespec t;
	char *p = out_buf;

	if (strncmp(r->tag, "dump", ZFCP_DBF_TAG_SIZE) == 0)
		return 0;

	zfcp_dbf_tag(&p, "tag", r->tag);
	zfcp_dbf_tag(&p, "tag2", r->tag2);
	zfcp_dbf_out(&p, "scsi_id", "0x%08x", r->scsi_id);
	zfcp_dbf_out(&p, "scsi_lun", "0x%08x", r->scsi_lun);
	zfcp_dbf_out(&p, "scsi_result", "0x%08x", r->scsi_result);
	zfcp_dbf_out(&p, "scsi_cmnd", "0x%0Lx", r->scsi_cmnd);
	zfcp_dbf_outd(&p, "scsi_opcode", r->scsi_opcode, ZFCP_DBF_SCSI_OPCODE,
		      0, ZFCP_DBF_SCSI_OPCODE);
	zfcp_dbf_out(&p, "scsi_retries", "0x%02x", r->scsi_retries);
	zfcp_dbf_out(&p, "scsi_allowed", "0x%02x", r->scsi_allowed);
	if (strncmp(r->tag, "abrt", ZFCP_DBF_TAG_SIZE) == 0)
		zfcp_dbf_out(&p, "old_fsf_reqid", "0x%0Lx", r->old_fsf_reqid);
	zfcp_dbf_out(&p, "fsf_reqid", "0x%0Lx", r->fsf_reqid);
	zfcp_dbf_out(&p, "fsf_seqno", "0x%08x", r->fsf_seqno);
	stck_to_timespec(r->fsf_issued, &t);
	zfcp_dbf_out(&p, "fsf_issued", "%011lu:%06lu", t.tv_sec, t.tv_nsec);

	if (strncmp(r->tag, "rslt", ZFCP_DBF_TAG_SIZE) == 0) {
		zfcp_dbf_out(&p, "fcp_rsp_validity", "0x%02x", r->rsp_validity);
		zfcp_dbf_out(&p, "fcp_rsp_scsi_status", "0x%02x",
			     r->rsp_scsi_status);
		zfcp_dbf_out(&p, "fcp_rsp_resid", "0x%08x", r->rsp_resid);
		zfcp_dbf_out(&p, "fcp_rsp_code", "0x%08x", r->rsp_code);
		zfcp_dbf_out(&p, "fcp_sns_info_len", "0x%08x", r->sns_info_len);
		zfcp_dbf_outd(&p, "fcp_sns_info", r->sns_info,
			      min((int)r->sns_info_len,
			      ZFCP_DBF_SCSI_FCP_SNS_INFO), 0,
			      r->sns_info_len);
	}
	p += sprintf(p, "\n");
	return p - out_buf;
}

static struct debug_view zfcp_dbf_scsi_view = {
	.name = "structured",
	.header_proc = zfcp_dbf_view_header,
	.format_proc = zfcp_dbf_scsi_view_format,
};

static debug_info_t *zfcp_dbf_reg(const char *name, int level,
				  struct debug_view *view, int size)
{
	struct debug_info *d;

	d = debug_register(name, dbfsize, level, size);
	if (!d)
		return NULL;

	debug_register_view(d, &debug_hex_ascii_view);
	debug_register_view(d, view);
	debug_set_level(d, level);

	return d;
}

/**
 * zfcp_adapter_debug_register - registers debug feature for an adapter
 * @adapter: pointer to adapter for which debug features should be registered
 * return: -ENOMEM on error, 0 otherwise
 */
int zfcp_dbf_adapter_register(struct zfcp_adapter *adapter)
{
	char dbf_name[DEBUG_MAX_NAME_LEN];
	struct zfcp_dbf *dbf;

	dbf = kzalloc(sizeof(struct zfcp_dbf), GFP_KERNEL);
	if (!dbf)
		return -ENOMEM;

	dbf->adapter = adapter;

	spin_lock_init(&dbf->pay_lock);
	spin_lock_init(&dbf->hba_lock);
	spin_lock_init(&dbf->san_lock);
	spin_lock_init(&dbf->scsi_lock);
	spin_lock_init(&dbf->rec_lock);

	/* debug feature area which records recovery activity */
	sprintf(dbf_name, "zfcp_%s_rec", dev_name(&adapter->ccw_device->dev));
	dbf->rec = zfcp_dbf_reg(dbf_name, 3, NULL, sizeof(struct zfcp_dbf_rec));
	if (!dbf->rec)
		goto err_out;

	/* debug feature area which records HBA (FSF and QDIO) conditions */
	sprintf(dbf_name, "zfcp_%s_hba", dev_name(&adapter->ccw_device->dev));
	dbf->hba = zfcp_dbf_reg(dbf_name, 3, NULL, sizeof(struct zfcp_dbf_hba));
	if (!dbf->hba)
		goto err_out;

	/* debug feature area which records payload info */
	sprintf(dbf_name, "zfcp_%s_pay", dev_name(&adapter->ccw_device->dev));
	dbf->pay = zfcp_dbf_reg(dbf_name, 3, NULL,
				sizeof(struct zfcp_dbf_pay));
	if (!dbf->pay)
		goto err_out;

	/* debug feature area which records SAN command failures and recovery */
	sprintf(dbf_name, "zfcp_%s_san", dev_name(&adapter->ccw_device->dev));
	dbf->san = zfcp_dbf_reg(dbf_name, 3, NULL, sizeof(struct zfcp_dbf_san));
	if (!dbf->san)
		goto err_out;

	/* debug feature area which records SCSI command failures and recovery */
	sprintf(dbf_name, "zfcp_%s_scsi", dev_name(&adapter->ccw_device->dev));
	dbf->scsi = zfcp_dbf_reg(dbf_name, 3, &zfcp_dbf_scsi_view,
				 sizeof(struct zfcp_dbf_scsi_record));
	if (!dbf->scsi)
		goto err_out;

	adapter->dbf = dbf;
	return 0;

err_out:
	zfcp_dbf_adapter_unregister(dbf);
	return -ENOMEM;
}

/**
 * zfcp_adapter_debug_unregister - unregisters debug feature for an adapter
 * @dbf: pointer to dbf for which debug features should be unregistered
 */
void zfcp_dbf_adapter_unregister(struct zfcp_dbf *dbf)
{
	if (!dbf)
		return;
	debug_unregister(dbf->scsi);
	debug_unregister(dbf->san);
	debug_unregister(dbf->hba);
	debug_unregister(dbf->pay);
	debug_unregister(dbf->rec);
	dbf->adapter->dbf = NULL;
	kfree(dbf);
}

