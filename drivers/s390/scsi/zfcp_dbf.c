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
#include <asm/debug.h>
#include "zfcp_dbf.h"
#include "zfcp_ext.h"
#include "zfcp_fc.h"

static u32 dbfsize = 4;

module_param(dbfsize, uint, 0400);
MODULE_PARM_DESC(dbfsize,
		 "number of pages for each debug feature area (default 4)");

static void zfcp_dbf_hexdump(debug_info_t *dbf, void *to, int to_len,
			     int level, char *from, int from_len)
{
	int offset;
	struct zfcp_dbf_dump *dump = to;
	int room = to_len - sizeof(*dump);

	for (offset = 0; offset < from_len; offset += dump->size) {
		memset(to, 0, to_len);
		strncpy(dump->tag, "dump", ZFCP_DBF_TAG_SIZE);
		dump->total_size = from_len;
		dump->offset = offset;
		dump->size = min(from_len - offset, room);
		memcpy(dump->data, from + offset, dump->size);
		debug_event(dbf, level, dump, dump->size + sizeof(*dump));
	}
}

static void zfcp_dbf_tag(char **p, const char *label, const char *tag)
{
	int i;

	*p += sprintf(*p, "%-24s", label);
	for (i = 0; i < ZFCP_DBF_TAG_SIZE; i++)
		*p += sprintf(*p, "%c", tag[i]);
	*p += sprintf(*p, "\n");
}

static void zfcp_dbf_outs(char **buf, const char *s1, const char *s2)
{
	*buf += sprintf(*buf, "%-24s%s\n", s1, s2);
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

void _zfcp_dbf_hba_fsf_response(const char *tag2, int level,
				struct zfcp_fsf_req *fsf_req,
				struct zfcp_dbf *dbf)
{
	struct fsf_qtcb *qtcb = fsf_req->qtcb;
	union fsf_prot_status_qual *prot_status_qual =
					&qtcb->prefix.prot_status_qual;
	union fsf_status_qual *fsf_status_qual = &qtcb->header.fsf_status_qual;
	struct scsi_cmnd *scsi_cmnd;
	struct zfcp_port *port;
	struct zfcp_unit *unit;
	struct zfcp_send_els *send_els;
	struct zfcp_dbf_hba_record *rec = &dbf->hba_buf;
	struct zfcp_dbf_hba_record_response *response = &rec->u.response;
	unsigned long flags;

	spin_lock_irqsave(&dbf->hba_lock, flags);
	memset(rec, 0, sizeof(*rec));
	strncpy(rec->tag, "resp", ZFCP_DBF_TAG_SIZE);
	strncpy(rec->tag2, tag2, ZFCP_DBF_TAG_SIZE);

	response->fsf_command = fsf_req->fsf_command;
	response->fsf_reqid = fsf_req->req_id;
	response->fsf_seqno = fsf_req->seq_no;
	response->fsf_issued = fsf_req->issued;
	response->fsf_prot_status = qtcb->prefix.prot_status;
	response->fsf_status = qtcb->header.fsf_status;
	memcpy(response->fsf_prot_status_qual,
	       prot_status_qual, FSF_PROT_STATUS_QUAL_SIZE);
	memcpy(response->fsf_status_qual,
	       fsf_status_qual, FSF_STATUS_QUALIFIER_SIZE);
	response->fsf_req_status = fsf_req->status;
	response->sbal_first = fsf_req->qdio_req.sbal_first;
	response->sbal_last = fsf_req->qdio_req.sbal_last;
	response->sbal_response = fsf_req->qdio_req.sbal_response;
	response->pool = fsf_req->pool != NULL;
	response->erp_action = (unsigned long)fsf_req->erp_action;

	switch (fsf_req->fsf_command) {
	case FSF_QTCB_FCP_CMND:
		if (fsf_req->status & ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT)
			break;
		scsi_cmnd = (struct scsi_cmnd *)fsf_req->data;
		if (scsi_cmnd) {
			response->u.fcp.cmnd = (unsigned long)scsi_cmnd;
			response->u.fcp.serial = scsi_cmnd->serial_number;
		}
		break;

	case FSF_QTCB_OPEN_PORT_WITH_DID:
	case FSF_QTCB_CLOSE_PORT:
	case FSF_QTCB_CLOSE_PHYSICAL_PORT:
		port = (struct zfcp_port *)fsf_req->data;
		response->u.port.wwpn = port->wwpn;
		response->u.port.d_id = port->d_id;
		response->u.port.port_handle = qtcb->header.port_handle;
		break;

	case FSF_QTCB_OPEN_LUN:
	case FSF_QTCB_CLOSE_LUN:
		unit = (struct zfcp_unit *)fsf_req->data;
		port = unit->port;
		response->u.unit.wwpn = port->wwpn;
		response->u.unit.fcp_lun = unit->fcp_lun;
		response->u.unit.port_handle = qtcb->header.port_handle;
		response->u.unit.lun_handle = qtcb->header.lun_handle;
		break;

	case FSF_QTCB_SEND_ELS:
		send_els = (struct zfcp_send_els *)fsf_req->data;
		response->u.els.d_id = ntoh24(qtcb->bottom.support.d_id);
		break;

	case FSF_QTCB_ABORT_FCP_CMND:
	case FSF_QTCB_SEND_GENERIC:
	case FSF_QTCB_EXCHANGE_CONFIG_DATA:
	case FSF_QTCB_EXCHANGE_PORT_DATA:
	case FSF_QTCB_DOWNLOAD_CONTROL_FILE:
	case FSF_QTCB_UPLOAD_CONTROL_FILE:
		break;
	}

	debug_event(dbf->hba, level, rec, sizeof(*rec));

	/* have fcp channel microcode fixed to use as little as possible */
	if (fsf_req->fsf_command != FSF_QTCB_FCP_CMND) {
		/* adjust length skipping trailing zeros */
		char *buf = (char *)qtcb + qtcb->header.log_start;
		int len = qtcb->header.log_length;
		for (; len && !buf[len - 1]; len--);
		zfcp_dbf_hexdump(dbf->hba, rec, sizeof(*rec), level, buf,
				 len);
	}

	spin_unlock_irqrestore(&dbf->hba_lock, flags);
}

void _zfcp_dbf_hba_fsf_unsol(const char *tag, int level, struct zfcp_dbf *dbf,
			     struct fsf_status_read_buffer *status_buffer)
{
	struct zfcp_dbf_hba_record *rec = &dbf->hba_buf;
	unsigned long flags;

	spin_lock_irqsave(&dbf->hba_lock, flags);
	memset(rec, 0, sizeof(*rec));
	strncpy(rec->tag, "stat", ZFCP_DBF_TAG_SIZE);
	strncpy(rec->tag2, tag, ZFCP_DBF_TAG_SIZE);

	rec->u.status.failed = atomic_read(&dbf->adapter->stat_miss);
	if (status_buffer != NULL) {
		rec->u.status.status_type = status_buffer->status_type;
		rec->u.status.status_subtype = status_buffer->status_subtype;
		memcpy(&rec->u.status.queue_designator,
		       &status_buffer->queue_designator,
		       sizeof(struct fsf_queue_designator));

		switch (status_buffer->status_type) {
		case FSF_STATUS_READ_SENSE_DATA_AVAIL:
			rec->u.status.payload_size =
			    ZFCP_DBF_UNSOL_PAYLOAD_SENSE_DATA_AVAIL;
			break;

		case FSF_STATUS_READ_BIT_ERROR_THRESHOLD:
			rec->u.status.payload_size =
			    ZFCP_DBF_UNSOL_PAYLOAD_BIT_ERROR_THRESHOLD;
			break;

		case FSF_STATUS_READ_LINK_DOWN:
			switch (status_buffer->status_subtype) {
			case FSF_STATUS_READ_SUB_NO_PHYSICAL_LINK:
			case FSF_STATUS_READ_SUB_FDISC_FAILED:
				rec->u.status.payload_size =
					sizeof(struct fsf_link_down_info);
			}
			break;

		case FSF_STATUS_READ_FEATURE_UPDATE_ALERT:
			rec->u.status.payload_size =
			    ZFCP_DBF_UNSOL_PAYLOAD_FEATURE_UPDATE_ALERT;
			break;
		}
		memcpy(&rec->u.status.payload,
		       &status_buffer->payload, rec->u.status.payload_size);
	}

	debug_event(dbf->hba, level, rec, sizeof(*rec));
	spin_unlock_irqrestore(&dbf->hba_lock, flags);
}

/**
 * zfcp_dbf_hba_qdio - trace event for QDIO related failure
 * @qdio: qdio structure affected by this QDIO related event
 * @qdio_error: as passed by qdio module
 * @sbal_index: first buffer with error condition, as passed by qdio module
 * @sbal_count: number of buffers affected, as passed by qdio module
 */
void zfcp_dbf_hba_qdio(struct zfcp_dbf *dbf, unsigned int qdio_error,
		       int sbal_index, int sbal_count)
{
	struct zfcp_dbf_hba_record *r = &dbf->hba_buf;
	unsigned long flags;

	spin_lock_irqsave(&dbf->hba_lock, flags);
	memset(r, 0, sizeof(*r));
	strncpy(r->tag, "qdio", ZFCP_DBF_TAG_SIZE);
	r->u.qdio.qdio_error = qdio_error;
	r->u.qdio.sbal_index = sbal_index;
	r->u.qdio.sbal_count = sbal_count;
	debug_event(dbf->hba, 0, r, sizeof(*r));
	spin_unlock_irqrestore(&dbf->hba_lock, flags);
}

/**
 * zfcp_dbf_hba_berr - trace event for bit error threshold
 * @dbf: dbf structure affected by this QDIO related event
 * @req: fsf request
 */
void zfcp_dbf_hba_berr(struct zfcp_dbf *dbf, struct zfcp_fsf_req *req)
{
	struct zfcp_dbf_hba_record *r = &dbf->hba_buf;
	struct fsf_status_read_buffer *sr_buf = req->data;
	struct fsf_bit_error_payload *err = &sr_buf->payload.bit_error;
	unsigned long flags;

	spin_lock_irqsave(&dbf->hba_lock, flags);
	memset(r, 0, sizeof(*r));
	strncpy(r->tag, "berr", ZFCP_DBF_TAG_SIZE);
	memcpy(&r->u.berr, err, sizeof(struct fsf_bit_error_payload));
	debug_event(dbf->hba, 0, r, sizeof(*r));
	spin_unlock_irqrestore(&dbf->hba_lock, flags);
}
static void zfcp_dbf_hba_view_response(char **p,
				       struct zfcp_dbf_hba_record_response *r)
{
	struct timespec t;

	zfcp_dbf_out(p, "fsf_command", "0x%08x", r->fsf_command);
	zfcp_dbf_out(p, "fsf_reqid", "0x%0Lx", r->fsf_reqid);
	zfcp_dbf_out(p, "fsf_seqno", "0x%08x", r->fsf_seqno);
	stck_to_timespec(r->fsf_issued, &t);
	zfcp_dbf_out(p, "fsf_issued", "%011lu:%06lu", t.tv_sec, t.tv_nsec);
	zfcp_dbf_out(p, "fsf_prot_status", "0x%08x", r->fsf_prot_status);
	zfcp_dbf_out(p, "fsf_status", "0x%08x", r->fsf_status);
	zfcp_dbf_outd(p, "fsf_prot_status_qual", r->fsf_prot_status_qual,
		      FSF_PROT_STATUS_QUAL_SIZE, 0, FSF_PROT_STATUS_QUAL_SIZE);
	zfcp_dbf_outd(p, "fsf_status_qual", r->fsf_status_qual,
		      FSF_STATUS_QUALIFIER_SIZE, 0, FSF_STATUS_QUALIFIER_SIZE);
	zfcp_dbf_out(p, "fsf_req_status", "0x%08x", r->fsf_req_status);
	zfcp_dbf_out(p, "sbal_first", "0x%02x", r->sbal_first);
	zfcp_dbf_out(p, "sbal_last", "0x%02x", r->sbal_last);
	zfcp_dbf_out(p, "sbal_response", "0x%02x", r->sbal_response);
	zfcp_dbf_out(p, "pool", "0x%02x", r->pool);

	switch (r->fsf_command) {
	case FSF_QTCB_FCP_CMND:
		if (r->fsf_req_status & ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT)
			break;
		zfcp_dbf_out(p, "scsi_cmnd", "0x%0Lx", r->u.fcp.cmnd);
		zfcp_dbf_out(p, "scsi_serial", "0x%016Lx", r->u.fcp.serial);
		*p += sprintf(*p, "\n");
		break;

	case FSF_QTCB_OPEN_PORT_WITH_DID:
	case FSF_QTCB_CLOSE_PORT:
	case FSF_QTCB_CLOSE_PHYSICAL_PORT:
		zfcp_dbf_out(p, "wwpn", "0x%016Lx", r->u.port.wwpn);
		zfcp_dbf_out(p, "d_id", "0x%06x", r->u.port.d_id);
		zfcp_dbf_out(p, "port_handle", "0x%08x", r->u.port.port_handle);
		break;

	case FSF_QTCB_OPEN_LUN:
	case FSF_QTCB_CLOSE_LUN:
		zfcp_dbf_out(p, "wwpn", "0x%016Lx", r->u.unit.wwpn);
		zfcp_dbf_out(p, "fcp_lun", "0x%016Lx", r->u.unit.fcp_lun);
		zfcp_dbf_out(p, "port_handle", "0x%08x", r->u.unit.port_handle);
		zfcp_dbf_out(p, "lun_handle", "0x%08x", r->u.unit.lun_handle);
		break;

	case FSF_QTCB_SEND_ELS:
		zfcp_dbf_out(p, "d_id", "0x%06x", r->u.els.d_id);
		break;

	case FSF_QTCB_ABORT_FCP_CMND:
	case FSF_QTCB_SEND_GENERIC:
	case FSF_QTCB_EXCHANGE_CONFIG_DATA:
	case FSF_QTCB_EXCHANGE_PORT_DATA:
	case FSF_QTCB_DOWNLOAD_CONTROL_FILE:
	case FSF_QTCB_UPLOAD_CONTROL_FILE:
		break;
	}
}

static void zfcp_dbf_hba_view_status(char **p,
				     struct zfcp_dbf_hba_record_status *r)
{
	zfcp_dbf_out(p, "failed", "0x%02x", r->failed);
	zfcp_dbf_out(p, "status_type", "0x%08x", r->status_type);
	zfcp_dbf_out(p, "status_subtype", "0x%08x", r->status_subtype);
	zfcp_dbf_outd(p, "queue_designator", (char *)&r->queue_designator,
		      sizeof(struct fsf_queue_designator), 0,
		      sizeof(struct fsf_queue_designator));
	zfcp_dbf_outd(p, "payload", (char *)&r->payload, r->payload_size, 0,
		      r->payload_size);
}

static void zfcp_dbf_hba_view_qdio(char **p, struct zfcp_dbf_hba_record_qdio *r)
{
	zfcp_dbf_out(p, "qdio_error", "0x%08x", r->qdio_error);
	zfcp_dbf_out(p, "sbal_index", "0x%02x", r->sbal_index);
	zfcp_dbf_out(p, "sbal_count", "0x%02x", r->sbal_count);
}

static void zfcp_dbf_hba_view_berr(char **p, struct fsf_bit_error_payload *r)
{
	zfcp_dbf_out(p, "link_failures", "%d", r->link_failure_error_count);
	zfcp_dbf_out(p, "loss_of_sync_err", "%d", r->loss_of_sync_error_count);
	zfcp_dbf_out(p, "loss_of_sig_err", "%d", r->loss_of_signal_error_count);
	zfcp_dbf_out(p, "prim_seq_err", "%d",
		     r->primitive_sequence_error_count);
	zfcp_dbf_out(p, "inval_trans_word_err", "%d",
		     r->invalid_transmission_word_error_count);
	zfcp_dbf_out(p, "CRC_errors", "%d", r->crc_error_count);
	zfcp_dbf_out(p, "prim_seq_event_to", "%d",
		     r->primitive_sequence_event_timeout_count);
	zfcp_dbf_out(p, "elast_buf_overrun_err", "%d",
		     r->elastic_buffer_overrun_error_count);
	zfcp_dbf_out(p, "adv_rec_buf2buf_cred", "%d",
		     r->advertised_receive_b2b_credit);
	zfcp_dbf_out(p, "curr_rec_buf2buf_cred", "%d",
		     r->current_receive_b2b_credit);
	zfcp_dbf_out(p, "adv_trans_buf2buf_cred", "%d",
		     r->advertised_transmit_b2b_credit);
	zfcp_dbf_out(p, "curr_trans_buf2buf_cred", "%d",
		     r->current_transmit_b2b_credit);
}

static int zfcp_dbf_hba_view_format(debug_info_t *id, struct debug_view *view,
				    char *out_buf, const char *in_buf)
{
	struct zfcp_dbf_hba_record *r = (struct zfcp_dbf_hba_record *)in_buf;
	char *p = out_buf;

	if (strncmp(r->tag, "dump", ZFCP_DBF_TAG_SIZE) == 0)
		return 0;

	zfcp_dbf_tag(&p, "tag", r->tag);
	if (isalpha(r->tag2[0]))
		zfcp_dbf_tag(&p, "tag2", r->tag2);

	if (strncmp(r->tag, "resp", ZFCP_DBF_TAG_SIZE) == 0)
		zfcp_dbf_hba_view_response(&p, &r->u.response);
	else if (strncmp(r->tag, "stat", ZFCP_DBF_TAG_SIZE) == 0)
		zfcp_dbf_hba_view_status(&p, &r->u.status);
	else if (strncmp(r->tag, "qdio", ZFCP_DBF_TAG_SIZE) == 0)
		zfcp_dbf_hba_view_qdio(&p, &r->u.qdio);
	else if (strncmp(r->tag, "berr", ZFCP_DBF_TAG_SIZE) == 0)
		zfcp_dbf_hba_view_berr(&p, &r->u.berr);

	if (strncmp(r->tag, "resp", ZFCP_DBF_TAG_SIZE) != 0)
		p += sprintf(p, "\n");
	return p - out_buf;
}

static struct debug_view zfcp_dbf_hba_view = {
	.name = "structured",
	.header_proc = zfcp_dbf_view_header,
	.format_proc = zfcp_dbf_hba_view_format,
};

static const char *zfcp_dbf_rec_tags[] = {
	[ZFCP_REC_DBF_ID_THREAD] = "thread",
	[ZFCP_REC_DBF_ID_TARGET] = "target",
	[ZFCP_REC_DBF_ID_TRIGGER] = "trigger",
	[ZFCP_REC_DBF_ID_ACTION] = "action",
};

static int zfcp_dbf_rec_view_format(debug_info_t *id, struct debug_view *view,
				    char *buf, const char *_rec)
{
	struct zfcp_dbf_rec_record *r = (struct zfcp_dbf_rec_record *)_rec;
	char *p = buf;
	char hint[ZFCP_DBF_ID_SIZE + 1];

	memcpy(hint, r->id2, ZFCP_DBF_ID_SIZE);
	hint[ZFCP_DBF_ID_SIZE] = 0;
	zfcp_dbf_outs(&p, "tag", zfcp_dbf_rec_tags[r->id]);
	zfcp_dbf_outs(&p, "hint", hint);
	switch (r->id) {
	case ZFCP_REC_DBF_ID_THREAD:
		zfcp_dbf_out(&p, "total", "%d", r->u.thread.total);
		zfcp_dbf_out(&p, "ready", "%d", r->u.thread.ready);
		zfcp_dbf_out(&p, "running", "%d", r->u.thread.running);
		break;
	case ZFCP_REC_DBF_ID_TARGET:
		zfcp_dbf_out(&p, "reference", "0x%016Lx", r->u.target.ref);
		zfcp_dbf_out(&p, "status", "0x%08x", r->u.target.status);
		zfcp_dbf_out(&p, "erp_count", "%d", r->u.target.erp_count);
		zfcp_dbf_out(&p, "d_id", "0x%06x", r->u.target.d_id);
		zfcp_dbf_out(&p, "wwpn", "0x%016Lx", r->u.target.wwpn);
		zfcp_dbf_out(&p, "fcp_lun", "0x%016Lx", r->u.target.fcp_lun);
		break;
	case ZFCP_REC_DBF_ID_TRIGGER:
		zfcp_dbf_out(&p, "reference", "0x%016Lx", r->u.trigger.ref);
		zfcp_dbf_out(&p, "erp_action", "0x%016Lx", r->u.trigger.action);
		zfcp_dbf_out(&p, "requested", "%d", r->u.trigger.want);
		zfcp_dbf_out(&p, "executed", "%d", r->u.trigger.need);
		zfcp_dbf_out(&p, "wwpn", "0x%016Lx", r->u.trigger.wwpn);
		zfcp_dbf_out(&p, "fcp_lun", "0x%016Lx", r->u.trigger.fcp_lun);
		zfcp_dbf_out(&p, "adapter_status", "0x%08x", r->u.trigger.as);
		zfcp_dbf_out(&p, "port_status", "0x%08x", r->u.trigger.ps);
		zfcp_dbf_out(&p, "unit_status", "0x%08x", r->u.trigger.us);
		break;
	case ZFCP_REC_DBF_ID_ACTION:
		zfcp_dbf_out(&p, "erp_action", "0x%016Lx", r->u.action.action);
		zfcp_dbf_out(&p, "fsf_req", "0x%016Lx", r->u.action.fsf_req);
		zfcp_dbf_out(&p, "status", "0x%08Lx", r->u.action.status);
		zfcp_dbf_out(&p, "step", "0x%08Lx", r->u.action.step);
		break;
	}
	p += sprintf(p, "\n");
	return p - buf;
}

static struct debug_view zfcp_dbf_rec_view = {
	.name = "structured",
	.header_proc = zfcp_dbf_view_header,
	.format_proc = zfcp_dbf_rec_view_format,
};

/**
 * zfcp_dbf_rec_thread - trace event related to recovery thread operation
 * @id2: identifier for event
 * @dbf: reference to dbf structure
 * This function assumes that the caller is holding erp_lock.
 */
void zfcp_dbf_rec_thread(char *id2, struct zfcp_dbf *dbf)
{
	struct zfcp_adapter *adapter = dbf->adapter;
	struct zfcp_dbf_rec_record *r = &dbf->rec_buf;
	unsigned long flags = 0;
	struct list_head *entry;
	unsigned ready = 0, running = 0, total;

	list_for_each(entry, &adapter->erp_ready_head)
		ready++;
	list_for_each(entry, &adapter->erp_running_head)
		running++;
	total = adapter->erp_total_count;

	spin_lock_irqsave(&dbf->rec_lock, flags);
	memset(r, 0, sizeof(*r));
	r->id = ZFCP_REC_DBF_ID_THREAD;
	memcpy(r->id2, id2, ZFCP_DBF_ID_SIZE);
	r->u.thread.total = total;
	r->u.thread.ready = ready;
	r->u.thread.running = running;
	debug_event(dbf->rec, 6, r, sizeof(*r));
	spin_unlock_irqrestore(&dbf->rec_lock, flags);
}

/**
 * zfcp_dbf_rec_thread - trace event related to recovery thread operation
 * @id2: identifier for event
 * @adapter: adapter
 * This function assumes that the caller does not hold erp_lock.
 */
void zfcp_dbf_rec_thread_lock(char *id2, struct zfcp_dbf *dbf)
{
	struct zfcp_adapter *adapter = dbf->adapter;
	unsigned long flags;

	read_lock_irqsave(&adapter->erp_lock, flags);
	zfcp_dbf_rec_thread(id2, dbf);
	read_unlock_irqrestore(&adapter->erp_lock, flags);
}

static void zfcp_dbf_rec_target(char *id2, void *ref, struct zfcp_dbf *dbf,
				atomic_t *status, atomic_t *erp_count, u64 wwpn,
				u32 d_id, u64 fcp_lun)
{
	struct zfcp_dbf_rec_record *r = &dbf->rec_buf;
	unsigned long flags;

	spin_lock_irqsave(&dbf->rec_lock, flags);
	memset(r, 0, sizeof(*r));
	r->id = ZFCP_REC_DBF_ID_TARGET;
	memcpy(r->id2, id2, ZFCP_DBF_ID_SIZE);
	r->u.target.ref = (unsigned long)ref;
	r->u.target.status = atomic_read(status);
	r->u.target.wwpn = wwpn;
	r->u.target.d_id = d_id;
	r->u.target.fcp_lun = fcp_lun;
	r->u.target.erp_count = atomic_read(erp_count);
	debug_event(dbf->rec, 3, r, sizeof(*r));
	spin_unlock_irqrestore(&dbf->rec_lock, flags);
}

/**
 * zfcp_dbf_rec_adapter - trace event for adapter state change
 * @id: identifier for trigger of state change
 * @ref: additional reference (e.g. request)
 * @dbf: reference to dbf structure
 */
void zfcp_dbf_rec_adapter(char *id, void *ref, struct zfcp_dbf *dbf)
{
	struct zfcp_adapter *adapter = dbf->adapter;

	zfcp_dbf_rec_target(id, ref, dbf, &adapter->status,
			    &adapter->erp_counter, 0, 0,
			    ZFCP_DBF_INVALID_LUN);
}

/**
 * zfcp_dbf_rec_port - trace event for port state change
 * @id: identifier for trigger of state change
 * @ref: additional reference (e.g. request)
 * @port: port
 */
void zfcp_dbf_rec_port(char *id, void *ref, struct zfcp_port *port)
{
	struct zfcp_dbf *dbf = port->adapter->dbf;

	zfcp_dbf_rec_target(id, ref, dbf, &port->status,
			    &port->erp_counter, port->wwpn, port->d_id,
			    ZFCP_DBF_INVALID_LUN);
}

/**
 * zfcp_dbf_rec_unit - trace event for unit state change
 * @id: identifier for trigger of state change
 * @ref: additional reference (e.g. request)
 * @unit: unit
 */
void zfcp_dbf_rec_unit(char *id, void *ref, struct zfcp_unit *unit)
{
	struct zfcp_port *port = unit->port;
	struct zfcp_dbf *dbf = port->adapter->dbf;

	zfcp_dbf_rec_target(id, ref, dbf, &unit->status,
				  &unit->erp_counter, port->wwpn, port->d_id,
				  unit->fcp_lun);
}

/**
 * zfcp_dbf_rec_trigger - trace event for triggered error recovery
 * @id2: identifier for error recovery trigger
 * @ref: additional reference (e.g. request)
 * @want: originally requested error recovery action
 * @need: error recovery action actually initiated
 * @action: address of error recovery action struct
 * @adapter: adapter
 * @port: port
 * @unit: unit
 */
void zfcp_dbf_rec_trigger(char *id2, void *ref, u8 want, u8 need, void *action,
			  struct zfcp_adapter *adapter, struct zfcp_port *port,
			  struct zfcp_unit *unit)
{
	struct zfcp_dbf *dbf = adapter->dbf;
	struct zfcp_dbf_rec_record *r = &dbf->rec_buf;
	unsigned long flags;

	spin_lock_irqsave(&dbf->rec_lock, flags);
	memset(r, 0, sizeof(*r));
	r->id = ZFCP_REC_DBF_ID_TRIGGER;
	memcpy(r->id2, id2, ZFCP_DBF_ID_SIZE);
	r->u.trigger.ref = (unsigned long)ref;
	r->u.trigger.want = want;
	r->u.trigger.need = need;
	r->u.trigger.action = (unsigned long)action;
	r->u.trigger.as = atomic_read(&adapter->status);
	if (port) {
		r->u.trigger.ps = atomic_read(&port->status);
		r->u.trigger.wwpn = port->wwpn;
	}
	if (unit)
		r->u.trigger.us = atomic_read(&unit->status);
	r->u.trigger.fcp_lun = unit ? unit->fcp_lun : ZFCP_DBF_INVALID_LUN;
	debug_event(dbf->rec, action ? 1 : 4, r, sizeof(*r));
	spin_unlock_irqrestore(&dbf->rec_lock, flags);
}

/**
 * zfcp_dbf_rec_action - trace event showing progress of recovery action
 * @id2: identifier
 * @erp_action: error recovery action struct pointer
 */
void zfcp_dbf_rec_action(char *id2, struct zfcp_erp_action *erp_action)
{
	struct zfcp_dbf *dbf = erp_action->adapter->dbf;
	struct zfcp_dbf_rec_record *r = &dbf->rec_buf;
	unsigned long flags;

	spin_lock_irqsave(&dbf->rec_lock, flags);
	memset(r, 0, sizeof(*r));
	r->id = ZFCP_REC_DBF_ID_ACTION;
	memcpy(r->id2, id2, ZFCP_DBF_ID_SIZE);
	r->u.action.action = (unsigned long)erp_action;
	r->u.action.status = erp_action->status;
	r->u.action.step = erp_action->step;
	r->u.action.fsf_req = erp_action->fsf_req_id;
	debug_event(dbf->rec, 5, r, sizeof(*r));
	spin_unlock_irqrestore(&dbf->rec_lock, flags);
}

/**
 * zfcp_dbf_san_ct_request - trace event for issued CT request
 * @fsf_req: request containing issued CT data
 * @d_id: destination id where ct request is sent to
 */
void zfcp_dbf_san_ct_request(struct zfcp_fsf_req *fsf_req, u32 d_id)
{
	struct zfcp_fsf_ct_els *ct = (struct zfcp_fsf_ct_els *)fsf_req->data;
	struct zfcp_adapter *adapter = fsf_req->adapter;
	struct zfcp_dbf *dbf = adapter->dbf;
	struct fc_ct_hdr *hdr = sg_virt(ct->req);
	struct zfcp_dbf_san_record *r = &dbf->san_buf;
	struct zfcp_dbf_san_record_ct_request *oct = &r->u.ct_req;
	int level = 3;
	unsigned long flags;

	spin_lock_irqsave(&dbf->san_lock, flags);
	memset(r, 0, sizeof(*r));
	strncpy(r->tag, "octc", ZFCP_DBF_TAG_SIZE);
	r->fsf_reqid = fsf_req->req_id;
	r->fsf_seqno = fsf_req->seq_no;
	oct->d_id = d_id;
	oct->cmd_req_code = hdr->ct_cmd;
	oct->revision = hdr->ct_rev;
	oct->gs_type = hdr->ct_fs_type;
	oct->gs_subtype = hdr->ct_fs_subtype;
	oct->options = hdr->ct_options;
	oct->max_res_size = hdr->ct_mr_size;
	oct->len = min((int)ct->req->length - (int)sizeof(struct fc_ct_hdr),
		       ZFCP_DBF_SAN_MAX_PAYLOAD);
	debug_event(dbf->san, level, r, sizeof(*r));
	zfcp_dbf_hexdump(dbf->san, r, sizeof(*r), level,
			 (void *)hdr + sizeof(struct fc_ct_hdr), oct->len);
	spin_unlock_irqrestore(&dbf->san_lock, flags);
}

/**
 * zfcp_dbf_san_ct_response - trace event for completion of CT request
 * @fsf_req: request containing CT response
 */
void zfcp_dbf_san_ct_response(struct zfcp_fsf_req *fsf_req)
{
	struct zfcp_fsf_ct_els *ct = (struct zfcp_fsf_ct_els *)fsf_req->data;
	struct zfcp_adapter *adapter = fsf_req->adapter;
	struct fc_ct_hdr *hdr = sg_virt(ct->resp);
	struct zfcp_dbf *dbf = adapter->dbf;
	struct zfcp_dbf_san_record *r = &dbf->san_buf;
	struct zfcp_dbf_san_record_ct_response *rct = &r->u.ct_resp;
	int level = 3;
	unsigned long flags;

	spin_lock_irqsave(&dbf->san_lock, flags);
	memset(r, 0, sizeof(*r));
	strncpy(r->tag, "rctc", ZFCP_DBF_TAG_SIZE);
	r->fsf_reqid = fsf_req->req_id;
	r->fsf_seqno = fsf_req->seq_no;
	rct->cmd_rsp_code = hdr->ct_cmd;
	rct->revision = hdr->ct_rev;
	rct->reason_code = hdr->ct_reason;
	rct->expl = hdr->ct_explan;
	rct->vendor_unique = hdr->ct_vendor;
	rct->max_res_size = hdr->ct_mr_size;
	rct->len = min((int)ct->resp->length - (int)sizeof(struct fc_ct_hdr),
		       ZFCP_DBF_SAN_MAX_PAYLOAD);
	debug_event(dbf->san, level, r, sizeof(*r));
	zfcp_dbf_hexdump(dbf->san, r, sizeof(*r), level,
			 (void *)hdr + sizeof(struct fc_ct_hdr), rct->len);
	spin_unlock_irqrestore(&dbf->san_lock, flags);
}

static void zfcp_dbf_san_els(const char *tag, int level,
			     struct zfcp_fsf_req *fsf_req, u32 d_id,
			     void *buffer, int buflen)
{
	struct zfcp_adapter *adapter = fsf_req->adapter;
	struct zfcp_dbf *dbf = adapter->dbf;
	struct zfcp_dbf_san_record *rec = &dbf->san_buf;
	unsigned long flags;

	spin_lock_irqsave(&dbf->san_lock, flags);
	memset(rec, 0, sizeof(*rec));
	strncpy(rec->tag, tag, ZFCP_DBF_TAG_SIZE);
	rec->fsf_reqid = fsf_req->req_id;
	rec->fsf_seqno = fsf_req->seq_no;
	rec->u.els.d_id = d_id;
	debug_event(dbf->san, level, rec, sizeof(*rec));
	zfcp_dbf_hexdump(dbf->san, rec, sizeof(*rec), level,
			 buffer, min(buflen, ZFCP_DBF_SAN_MAX_PAYLOAD));
	spin_unlock_irqrestore(&dbf->san_lock, flags);
}

/**
 * zfcp_dbf_san_els_request - trace event for issued ELS
 * @fsf_req: request containing issued ELS
 */
void zfcp_dbf_san_els_request(struct zfcp_fsf_req *fsf_req)
{
	struct zfcp_fsf_ct_els *els = (struct zfcp_fsf_ct_els *)fsf_req->data;
	u32 d_id = ntoh24(fsf_req->qtcb->bottom.support.d_id);

	zfcp_dbf_san_els("oels", 2, fsf_req, d_id,
			 sg_virt(els->req), els->req->length);
}

/**
 * zfcp_dbf_san_els_response - trace event for completed ELS
 * @fsf_req: request containing ELS response
 */
void zfcp_dbf_san_els_response(struct zfcp_fsf_req *fsf_req)
{
	struct zfcp_fsf_ct_els *els = (struct zfcp_fsf_ct_els *)fsf_req->data;
	u32 d_id = ntoh24(fsf_req->qtcb->bottom.support.d_id);

	zfcp_dbf_san_els("rels", 2, fsf_req, d_id,
			       sg_virt(els->resp), els->resp->length);
}

/**
 * zfcp_dbf_san_incoming_els - trace event for incomig ELS
 * @fsf_req: request containing unsolicited status buffer with incoming ELS
 */
void zfcp_dbf_san_incoming_els(struct zfcp_fsf_req *fsf_req)
{
	struct fsf_status_read_buffer *buf =
			(struct fsf_status_read_buffer *)fsf_req->data;
	int length = (int)buf->length -
		     (int)((void *)&buf->payload - (void *)buf);

	zfcp_dbf_san_els("iels", 1, fsf_req, ntoh24(buf->d_id),
			       (void *)buf->payload.data, length);
}

static int zfcp_dbf_san_view_format(debug_info_t *id, struct debug_view *view,
				    char *out_buf, const char *in_buf)
{
	struct zfcp_dbf_san_record *r = (struct zfcp_dbf_san_record *)in_buf;
	char *p = out_buf;

	if (strncmp(r->tag, "dump", ZFCP_DBF_TAG_SIZE) == 0)
		return 0;

	zfcp_dbf_tag(&p, "tag", r->tag);
	zfcp_dbf_out(&p, "fsf_reqid", "0x%0Lx", r->fsf_reqid);
	zfcp_dbf_out(&p, "fsf_seqno", "0x%08x", r->fsf_seqno);

	if (strncmp(r->tag, "octc", ZFCP_DBF_TAG_SIZE) == 0) {
		struct zfcp_dbf_san_record_ct_request *ct = &r->u.ct_req;
		zfcp_dbf_out(&p, "d_id", "0x%06x", ct->d_id);
		zfcp_dbf_out(&p, "cmd_req_code", "0x%04x", ct->cmd_req_code);
		zfcp_dbf_out(&p, "revision", "0x%02x", ct->revision);
		zfcp_dbf_out(&p, "gs_type", "0x%02x", ct->gs_type);
		zfcp_dbf_out(&p, "gs_subtype", "0x%02x", ct->gs_subtype);
		zfcp_dbf_out(&p, "options", "0x%02x", ct->options);
		zfcp_dbf_out(&p, "max_res_size", "0x%04x", ct->max_res_size);
	} else if (strncmp(r->tag, "rctc", ZFCP_DBF_TAG_SIZE) == 0) {
		struct zfcp_dbf_san_record_ct_response *ct = &r->u.ct_resp;
		zfcp_dbf_out(&p, "cmd_rsp_code", "0x%04x", ct->cmd_rsp_code);
		zfcp_dbf_out(&p, "revision", "0x%02x", ct->revision);
		zfcp_dbf_out(&p, "reason_code", "0x%02x", ct->reason_code);
		zfcp_dbf_out(&p, "reason_code_expl", "0x%02x", ct->expl);
		zfcp_dbf_out(&p, "vendor_unique", "0x%02x", ct->vendor_unique);
		zfcp_dbf_out(&p, "max_res_size", "0x%04x", ct->max_res_size);
	} else if (strncmp(r->tag, "oels", ZFCP_DBF_TAG_SIZE) == 0 ||
		   strncmp(r->tag, "rels", ZFCP_DBF_TAG_SIZE) == 0 ||
		   strncmp(r->tag, "iels", ZFCP_DBF_TAG_SIZE) == 0) {
		struct zfcp_dbf_san_record_els *els = &r->u.els;
		zfcp_dbf_out(&p, "d_id", "0x%06x", els->d_id);
	}
	return p - out_buf;
}

static struct debug_view zfcp_dbf_san_view = {
	.name = "structured",
	.header_proc = zfcp_dbf_view_header,
	.format_proc = zfcp_dbf_san_view_format,
};

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
				rec->scsi_serial = scsi_cmnd->serial_number;
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
	zfcp_dbf_out(&p, "scsi_serial", "0x%016Lx", r->scsi_serial);
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

	dbf = kmalloc(sizeof(struct zfcp_dbf), GFP_KERNEL);
	if (!dbf)
		return -ENOMEM;

	dbf->adapter = adapter;

	spin_lock_init(&dbf->hba_lock);
	spin_lock_init(&dbf->san_lock);
	spin_lock_init(&dbf->scsi_lock);
	spin_lock_init(&dbf->rec_lock);

	/* debug feature area which records recovery activity */
	sprintf(dbf_name, "zfcp_%s_rec", dev_name(&adapter->ccw_device->dev));
	dbf->rec = zfcp_dbf_reg(dbf_name, 3, &zfcp_dbf_rec_view,
				sizeof(struct zfcp_dbf_rec_record));
	if (!dbf->rec)
		goto err_out;

	/* debug feature area which records HBA (FSF and QDIO) conditions */
	sprintf(dbf_name, "zfcp_%s_hba", dev_name(&adapter->ccw_device->dev));
	dbf->hba = zfcp_dbf_reg(dbf_name, 3, &zfcp_dbf_hba_view,
				sizeof(struct zfcp_dbf_hba_record));
	if (!dbf->hba)
		goto err_out;

	/* debug feature area which records SAN command failures and recovery */
	sprintf(dbf_name, "zfcp_%s_san", dev_name(&adapter->ccw_device->dev));
	dbf->san = zfcp_dbf_reg(dbf_name, 6, &zfcp_dbf_san_view,
				sizeof(struct zfcp_dbf_san_record));
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
	debug_unregister(dbf->rec);
	dbf->adapter->dbf = NULL;
	kfree(dbf);
}

