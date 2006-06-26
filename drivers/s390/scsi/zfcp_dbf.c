/*
 * This file is part of the zfcp device driver for
 * FCP adapters for IBM System z9 and zSeries.
 *
 * (C) Copyright IBM Corp. 2002, 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <asm/debug.h>
#include <linux/ctype.h>
#include "zfcp_ext.h"

static u32 dbfsize = 4;

module_param(dbfsize, uint, 0400);
MODULE_PARM_DESC(dbfsize,
		 "number of pages for each debug feature area (default 4)");

#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER

static inline int
zfcp_dbf_stck(char *out_buf, const char *label, unsigned long long stck)
{
	unsigned long long sec;
	struct timespec xtime;
	int len = 0;

	stck -= 0x8126d60e46000000LL - (0x3c26700LL * 1000000 * 4096);
	sec = stck >> 12;
	do_div(sec, 1000000);
	xtime.tv_sec = sec;
	stck -= (sec * 1000000) << 12;
	xtime.tv_nsec = ((stck * 1000) >> 12);
	len += sprintf(out_buf + len, "%-24s%011lu:%06lu\n",
		       label, xtime.tv_sec, xtime.tv_nsec);

	return len;
}

static int zfcp_dbf_tag(char *out_buf, const char *label, const char *tag)
{
	int len = 0, i;

	len += sprintf(out_buf + len, "%-24s", label);
	for (i = 0; i < ZFCP_DBF_TAG_SIZE; i++)
		len += sprintf(out_buf + len, "%c", tag[i]);
	len += sprintf(out_buf + len, "\n");

	return len;
}

static int
zfcp_dbf_view(char *out_buf, const char *label, const char *format, ...)
{
	va_list arg;
	int len = 0;

	len += sprintf(out_buf + len, "%-24s", label);
	va_start(arg, format);
	len += vsprintf(out_buf + len, format, arg);
	va_end(arg);
	len += sprintf(out_buf + len, "\n");

	return len;
}

static int
zfcp_dbf_view_dump(char *out_buf, const char *label,
		   char *buffer, int buflen, int offset, int total_size)
{
	int len = 0;

	if (offset == 0)
		len += sprintf(out_buf + len, "%-24s  ", label);

	while (buflen--) {
		if (offset > 0) {
			if ((offset % 32) == 0)
				len += sprintf(out_buf + len, "\n%-24c  ", ' ');
			else if ((offset % 4) == 0)
				len += sprintf(out_buf + len, " ");
		}
		len += sprintf(out_buf + len, "%02x", *buffer++);
		if (++offset == total_size) {
			len += sprintf(out_buf + len, "\n");
			break;
		}
	}

	if (total_size == 0)
		len += sprintf(out_buf + len, "\n");

	return len;
}

static inline int
zfcp_dbf_view_header(debug_info_t * id, struct debug_view *view, int area,
		     debug_entry_t * entry, char *out_buf)
{
	struct zfcp_dbf_dump *dump = (struct zfcp_dbf_dump *)DEBUG_DATA(entry);
	int len = 0;

	if (strncmp(dump->tag, "dump", ZFCP_DBF_TAG_SIZE) != 0) {
		len += zfcp_dbf_stck(out_buf + len, "timestamp",
				     entry->id.stck);
		len += zfcp_dbf_view(out_buf + len, "cpu", "%02i",
				     entry->id.fields.cpuid);
	} else {
		len += zfcp_dbf_view_dump(out_buf + len, NULL,
					  dump->data,
					  dump->size,
					  dump->offset, dump->total_size);
		if ((dump->offset + dump->size) == dump->total_size)
			len += sprintf(out_buf + len, "\n");
	}

	return len;
}

inline void zfcp_hba_dbf_event_fsf_response(struct zfcp_fsf_req *fsf_req)
{
	struct zfcp_adapter *adapter = fsf_req->adapter;
	struct fsf_qtcb *qtcb = fsf_req->qtcb;
	union fsf_prot_status_qual *prot_status_qual =
	    &qtcb->prefix.prot_status_qual;
	union fsf_status_qual *fsf_status_qual = &qtcb->header.fsf_status_qual;
	struct scsi_cmnd *scsi_cmnd;
	struct zfcp_port *port;
	struct zfcp_unit *unit;
	struct zfcp_send_els *send_els;
	struct zfcp_hba_dbf_record *rec = &adapter->hba_dbf_buf;
	struct zfcp_hba_dbf_record_response *response = &rec->type.response;
	int level;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hba_dbf_lock, flags);
	memset(rec, 0, sizeof(struct zfcp_hba_dbf_record));
	strncpy(rec->tag, "resp", ZFCP_DBF_TAG_SIZE);

	if ((qtcb->prefix.prot_status != FSF_PROT_GOOD) &&
	    (qtcb->prefix.prot_status != FSF_PROT_FSF_STATUS_PRESENTED)) {
		strncpy(rec->tag2, "perr", ZFCP_DBF_TAG_SIZE);
		level = 1;
	} else if (qtcb->header.fsf_status != FSF_GOOD) {
		strncpy(rec->tag2, "ferr", ZFCP_DBF_TAG_SIZE);
		level = 1;
	} else if ((fsf_req->fsf_command == FSF_QTCB_OPEN_PORT_WITH_DID) ||
		   (fsf_req->fsf_command == FSF_QTCB_OPEN_LUN)) {
		strncpy(rec->tag2, "open", ZFCP_DBF_TAG_SIZE);
		level = 4;
	} else if ((prot_status_qual->doubleword[0] != 0) ||
		   (prot_status_qual->doubleword[1] != 0) ||
		   (fsf_status_qual->doubleword[0] != 0) ||
		   (fsf_status_qual->doubleword[1] != 0)) {
		strncpy(rec->tag2, "qual", ZFCP_DBF_TAG_SIZE);
		level = 3;
	} else {
		strncpy(rec->tag2, "norm", ZFCP_DBF_TAG_SIZE);
		level = 6;
	}

	response->fsf_command = fsf_req->fsf_command;
	response->fsf_reqid = (unsigned long)fsf_req;
	response->fsf_seqno = fsf_req->seq_no;
	response->fsf_issued = fsf_req->issued;
	response->fsf_prot_status = qtcb->prefix.prot_status;
	response->fsf_status = qtcb->header.fsf_status;
	memcpy(response->fsf_prot_status_qual,
	       prot_status_qual, FSF_PROT_STATUS_QUAL_SIZE);
	memcpy(response->fsf_status_qual,
	       fsf_status_qual, FSF_STATUS_QUALIFIER_SIZE);
	response->fsf_req_status = fsf_req->status;
	response->sbal_first = fsf_req->sbal_first;
	response->sbal_curr = fsf_req->sbal_curr;
	response->sbal_last = fsf_req->sbal_last;
	response->pool = fsf_req->pool != NULL;
	response->erp_action = (unsigned long)fsf_req->erp_action;

	switch (fsf_req->fsf_command) {
	case FSF_QTCB_FCP_CMND:
		if (fsf_req->status & ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT)
			break;
		scsi_cmnd = (struct scsi_cmnd *)fsf_req->data;
		if (scsi_cmnd != NULL) {
			response->data.send_fcp.scsi_cmnd
			    = (unsigned long)scsi_cmnd;
			response->data.send_fcp.scsi_serial
			    = scsi_cmnd->serial_number;
		}
		break;

	case FSF_QTCB_OPEN_PORT_WITH_DID:
	case FSF_QTCB_CLOSE_PORT:
	case FSF_QTCB_CLOSE_PHYSICAL_PORT:
		port = (struct zfcp_port *)fsf_req->data;
		response->data.port.wwpn = port->wwpn;
		response->data.port.d_id = port->d_id;
		response->data.port.port_handle = qtcb->header.port_handle;
		break;

	case FSF_QTCB_OPEN_LUN:
	case FSF_QTCB_CLOSE_LUN:
		unit = (struct zfcp_unit *)fsf_req->data;
		port = unit->port;
		response->data.unit.wwpn = port->wwpn;
		response->data.unit.fcp_lun = unit->fcp_lun;
		response->data.unit.port_handle = qtcb->header.port_handle;
		response->data.unit.lun_handle = qtcb->header.lun_handle;
		break;

	case FSF_QTCB_SEND_ELS:
		send_els = (struct zfcp_send_els *)fsf_req->data;
		response->data.send_els.d_id = qtcb->bottom.support.d_id;
		response->data.send_els.ls_code = send_els->ls_code >> 24;
		break;

	case FSF_QTCB_ABORT_FCP_CMND:
	case FSF_QTCB_SEND_GENERIC:
	case FSF_QTCB_EXCHANGE_CONFIG_DATA:
	case FSF_QTCB_EXCHANGE_PORT_DATA:
	case FSF_QTCB_DOWNLOAD_CONTROL_FILE:
	case FSF_QTCB_UPLOAD_CONTROL_FILE:
		break;
	}

	debug_event(adapter->hba_dbf, level,
		    rec, sizeof(struct zfcp_hba_dbf_record));
	spin_unlock_irqrestore(&adapter->hba_dbf_lock, flags);
}

inline void
zfcp_hba_dbf_event_fsf_unsol(const char *tag, struct zfcp_adapter *adapter,
			     struct fsf_status_read_buffer *status_buffer)
{
	struct zfcp_hba_dbf_record *rec = &adapter->hba_dbf_buf;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hba_dbf_lock, flags);
	memset(rec, 0, sizeof(struct zfcp_hba_dbf_record));
	strncpy(rec->tag, "stat", ZFCP_DBF_TAG_SIZE);
	strncpy(rec->tag2, tag, ZFCP_DBF_TAG_SIZE);

	rec->type.status.failed = adapter->status_read_failed;
	if (status_buffer != NULL) {
		rec->type.status.status_type = status_buffer->status_type;
		rec->type.status.status_subtype = status_buffer->status_subtype;
		memcpy(&rec->type.status.queue_designator,
		       &status_buffer->queue_designator,
		       sizeof(struct fsf_queue_designator));

		switch (status_buffer->status_type) {
		case FSF_STATUS_READ_SENSE_DATA_AVAIL:
			rec->type.status.payload_size =
			    ZFCP_DBF_UNSOL_PAYLOAD_SENSE_DATA_AVAIL;
			break;

		case FSF_STATUS_READ_BIT_ERROR_THRESHOLD:
			rec->type.status.payload_size =
			    ZFCP_DBF_UNSOL_PAYLOAD_BIT_ERROR_THRESHOLD;
			break;

		case FSF_STATUS_READ_LINK_DOWN:
			switch (status_buffer->status_subtype) {
			case FSF_STATUS_READ_SUB_NO_PHYSICAL_LINK:
			case FSF_STATUS_READ_SUB_FDISC_FAILED:
				rec->type.status.payload_size =
					sizeof(struct fsf_link_down_info);
			}
			break;

		case FSF_STATUS_READ_FEATURE_UPDATE_ALERT:
			rec->type.status.payload_size =
			    ZFCP_DBF_UNSOL_PAYLOAD_FEATURE_UPDATE_ALERT;
			break;
		}
		memcpy(&rec->type.status.payload,
		       &status_buffer->payload, rec->type.status.payload_size);
	}

	debug_event(adapter->hba_dbf, 2,
		    rec, sizeof(struct zfcp_hba_dbf_record));
	spin_unlock_irqrestore(&adapter->hba_dbf_lock, flags);
}

inline void
zfcp_hba_dbf_event_qdio(struct zfcp_adapter *adapter, unsigned int status,
			unsigned int qdio_error, unsigned int siga_error,
			int sbal_index, int sbal_count)
{
	struct zfcp_hba_dbf_record *rec = &adapter->hba_dbf_buf;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hba_dbf_lock, flags);
	memset(rec, 0, sizeof(struct zfcp_hba_dbf_record));
	strncpy(rec->tag, "qdio", ZFCP_DBF_TAG_SIZE);
	rec->type.qdio.status = status;
	rec->type.qdio.qdio_error = qdio_error;
	rec->type.qdio.siga_error = siga_error;
	rec->type.qdio.sbal_index = sbal_index;
	rec->type.qdio.sbal_count = sbal_count;
	debug_event(adapter->hba_dbf, 0,
		    rec, sizeof(struct zfcp_hba_dbf_record));
	spin_unlock_irqrestore(&adapter->hba_dbf_lock, flags);
}

static inline int
zfcp_hba_dbf_view_response(char *out_buf,
			   struct zfcp_hba_dbf_record_response *rec)
{
	int len = 0;

	len += zfcp_dbf_view(out_buf + len, "fsf_command", "0x%08x",
			     rec->fsf_command);
	len += zfcp_dbf_view(out_buf + len, "fsf_reqid", "0x%0Lx",
			     rec->fsf_reqid);
	len += zfcp_dbf_view(out_buf + len, "fsf_seqno", "0x%08x",
			     rec->fsf_seqno);
	len += zfcp_dbf_stck(out_buf + len, "fsf_issued", rec->fsf_issued);
	len += zfcp_dbf_view(out_buf + len, "fsf_prot_status", "0x%08x",
			     rec->fsf_prot_status);
	len += zfcp_dbf_view(out_buf + len, "fsf_status", "0x%08x",
			     rec->fsf_status);
	len += zfcp_dbf_view_dump(out_buf + len, "fsf_prot_status_qual",
				  rec->fsf_prot_status_qual,
				  FSF_PROT_STATUS_QUAL_SIZE,
				  0, FSF_PROT_STATUS_QUAL_SIZE);
	len += zfcp_dbf_view_dump(out_buf + len, "fsf_status_qual",
				  rec->fsf_status_qual,
				  FSF_STATUS_QUALIFIER_SIZE,
				  0, FSF_STATUS_QUALIFIER_SIZE);
	len += zfcp_dbf_view(out_buf + len, "fsf_req_status", "0x%08x",
			     rec->fsf_req_status);
	len += zfcp_dbf_view(out_buf + len, "sbal_first", "0x%02x",
			     rec->sbal_first);
	len += zfcp_dbf_view(out_buf + len, "sbal_curr", "0x%02x",
			     rec->sbal_curr);
	len += zfcp_dbf_view(out_buf + len, "sbal_last", "0x%02x",
			     rec->sbal_last);
	len += zfcp_dbf_view(out_buf + len, "pool", "0x%02x", rec->pool);

	switch (rec->fsf_command) {
	case FSF_QTCB_FCP_CMND:
		if (rec->fsf_req_status & ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT)
			break;
		len += zfcp_dbf_view(out_buf + len, "scsi_cmnd", "0x%0Lx",
				     rec->data.send_fcp.scsi_cmnd);
		len += zfcp_dbf_view(out_buf + len, "scsi_serial", "0x%016Lx",
				     rec->data.send_fcp.scsi_serial);
		break;

	case FSF_QTCB_OPEN_PORT_WITH_DID:
	case FSF_QTCB_CLOSE_PORT:
	case FSF_QTCB_CLOSE_PHYSICAL_PORT:
		len += zfcp_dbf_view(out_buf + len, "wwpn", "0x%016Lx",
				     rec->data.port.wwpn);
		len += zfcp_dbf_view(out_buf + len, "d_id", "0x%06x",
				     rec->data.port.d_id);
		len += zfcp_dbf_view(out_buf + len, "port_handle", "0x%08x",
				     rec->data.port.port_handle);
		break;

	case FSF_QTCB_OPEN_LUN:
	case FSF_QTCB_CLOSE_LUN:
		len += zfcp_dbf_view(out_buf + len, "wwpn", "0x%016Lx",
				     rec->data.unit.wwpn);
		len += zfcp_dbf_view(out_buf + len, "fcp_lun", "0x%016Lx",
				     rec->data.unit.fcp_lun);
		len += zfcp_dbf_view(out_buf + len, "port_handle", "0x%08x",
				     rec->data.unit.port_handle);
		len += zfcp_dbf_view(out_buf + len, "lun_handle", "0x%08x",
				     rec->data.unit.lun_handle);
		break;

	case FSF_QTCB_SEND_ELS:
		len += zfcp_dbf_view(out_buf + len, "d_id", "0x%06x",
				     rec->data.send_els.d_id);
		len += zfcp_dbf_view(out_buf + len, "ls_code", "0x%02x",
				     rec->data.send_els.ls_code);
		break;

	case FSF_QTCB_ABORT_FCP_CMND:
	case FSF_QTCB_SEND_GENERIC:
	case FSF_QTCB_EXCHANGE_CONFIG_DATA:
	case FSF_QTCB_EXCHANGE_PORT_DATA:
	case FSF_QTCB_DOWNLOAD_CONTROL_FILE:
	case FSF_QTCB_UPLOAD_CONTROL_FILE:
		break;
	}

	return len;
}

static inline int
zfcp_hba_dbf_view_status(char *out_buf, struct zfcp_hba_dbf_record_status *rec)
{
	int len = 0;

	len += zfcp_dbf_view(out_buf + len, "failed", "0x%02x", rec->failed);
	len += zfcp_dbf_view(out_buf + len, "status_type", "0x%08x",
			     rec->status_type);
	len += zfcp_dbf_view(out_buf + len, "status_subtype", "0x%08x",
			     rec->status_subtype);
	len += zfcp_dbf_view_dump(out_buf + len, "queue_designator",
				  (char *)&rec->queue_designator,
				  sizeof(struct fsf_queue_designator),
				  0, sizeof(struct fsf_queue_designator));
	len += zfcp_dbf_view_dump(out_buf + len, "payload",
				  (char *)&rec->payload,
				  rec->payload_size, 0, rec->payload_size);

	return len;
}

static inline int
zfcp_hba_dbf_view_qdio(char *out_buf, struct zfcp_hba_dbf_record_qdio *rec)
{
	int len = 0;

	len += zfcp_dbf_view(out_buf + len, "status", "0x%08x", rec->status);
	len += zfcp_dbf_view(out_buf + len, "qdio_error", "0x%08x",
			     rec->qdio_error);
	len += zfcp_dbf_view(out_buf + len, "siga_error", "0x%08x",
			     rec->siga_error);
	len += zfcp_dbf_view(out_buf + len, "sbal_index", "0x%02x",
			     rec->sbal_index);
	len += zfcp_dbf_view(out_buf + len, "sbal_count", "0x%02x",
			     rec->sbal_count);

	return len;
}

static int
zfcp_hba_dbf_view_format(debug_info_t * id, struct debug_view *view,
			 char *out_buf, const char *in_buf)
{
	struct zfcp_hba_dbf_record *rec = (struct zfcp_hba_dbf_record *)in_buf;
	int len = 0;

	if (strncmp(rec->tag, "dump", ZFCP_DBF_TAG_SIZE) == 0)
		return 0;

	len += zfcp_dbf_tag(out_buf + len, "tag", rec->tag);
	if (isalpha(rec->tag2[0]))
		len += zfcp_dbf_tag(out_buf + len, "tag2", rec->tag2);
	if (strncmp(rec->tag, "resp", ZFCP_DBF_TAG_SIZE) == 0)
		len += zfcp_hba_dbf_view_response(out_buf + len,
						  &rec->type.response);
	else if (strncmp(rec->tag, "stat", ZFCP_DBF_TAG_SIZE) == 0)
		len += zfcp_hba_dbf_view_status(out_buf + len,
						&rec->type.status);
	else if (strncmp(rec->tag, "qdio", ZFCP_DBF_TAG_SIZE) == 0)
		len += zfcp_hba_dbf_view_qdio(out_buf + len, &rec->type.qdio);

	len += sprintf(out_buf + len, "\n");

	return len;
}

struct debug_view zfcp_hba_dbf_view = {
	"structured",
	NULL,
	&zfcp_dbf_view_header,
	&zfcp_hba_dbf_view_format,
	NULL,
	NULL
};

inline void
_zfcp_san_dbf_event_common_ct(const char *tag, struct zfcp_fsf_req *fsf_req,
			      u32 s_id, u32 d_id, void *buffer, int buflen)
{
	struct zfcp_send_ct *send_ct = (struct zfcp_send_ct *)fsf_req->data;
	struct zfcp_port *port = send_ct->port;
	struct zfcp_adapter *adapter = port->adapter;
	struct ct_hdr *header = (struct ct_hdr *)buffer;
	struct zfcp_san_dbf_record *rec = &adapter->san_dbf_buf;
	struct zfcp_san_dbf_record_ct *ct = &rec->type.ct;
	unsigned long flags;

	spin_lock_irqsave(&adapter->san_dbf_lock, flags);
	memset(rec, 0, sizeof(struct zfcp_san_dbf_record));
	strncpy(rec->tag, tag, ZFCP_DBF_TAG_SIZE);
	rec->fsf_reqid = (unsigned long)fsf_req;
	rec->fsf_seqno = fsf_req->seq_no;
	rec->s_id = s_id;
	rec->d_id = d_id;
	if (strncmp(tag, "octc", ZFCP_DBF_TAG_SIZE) == 0) {
		ct->type.request.cmd_req_code = header->cmd_rsp_code;
		ct->type.request.revision = header->revision;
		ct->type.request.gs_type = header->gs_type;
		ct->type.request.gs_subtype = header->gs_subtype;
		ct->type.request.options = header->options;
		ct->type.request.max_res_size = header->max_res_size;
	} else if (strncmp(tag, "rctc", ZFCP_DBF_TAG_SIZE) == 0) {
		ct->type.response.cmd_rsp_code = header->cmd_rsp_code;
		ct->type.response.revision = header->revision;
		ct->type.response.reason_code = header->reason_code;
		ct->type.response.reason_code_expl = header->reason_code_expl;
		ct->type.response.vendor_unique = header->vendor_unique;
	}
	ct->payload_size =
	    min(buflen - (int)sizeof(struct ct_hdr), ZFCP_DBF_CT_PAYLOAD);
	memcpy(ct->payload, buffer + sizeof(struct ct_hdr), ct->payload_size);
	debug_event(adapter->san_dbf, 3,
		    rec, sizeof(struct zfcp_san_dbf_record));
	spin_unlock_irqrestore(&adapter->san_dbf_lock, flags);
}

inline void zfcp_san_dbf_event_ct_request(struct zfcp_fsf_req *fsf_req)
{
	struct zfcp_send_ct *ct = (struct zfcp_send_ct *)fsf_req->data;
	struct zfcp_port *port = ct->port;
	struct zfcp_adapter *adapter = port->adapter;

	_zfcp_san_dbf_event_common_ct("octc", fsf_req,
				      fc_host_port_id(adapter->scsi_host),
				      port->d_id, zfcp_sg_to_address(ct->req),
				      ct->req->length);
}

inline void zfcp_san_dbf_event_ct_response(struct zfcp_fsf_req *fsf_req)
{
	struct zfcp_send_ct *ct = (struct zfcp_send_ct *)fsf_req->data;
	struct zfcp_port *port = ct->port;
	struct zfcp_adapter *adapter = port->adapter;

	_zfcp_san_dbf_event_common_ct("rctc", fsf_req, port->d_id,
				      fc_host_port_id(adapter->scsi_host),
				      zfcp_sg_to_address(ct->resp),
				      ct->resp->length);
}

static inline void
_zfcp_san_dbf_event_common_els(const char *tag, int level,
			       struct zfcp_fsf_req *fsf_req, u32 s_id,
			       u32 d_id, u8 ls_code, void *buffer, int buflen)
{
	struct zfcp_adapter *adapter = fsf_req->adapter;
	struct zfcp_san_dbf_record *rec = &adapter->san_dbf_buf;
	struct zfcp_dbf_dump *dump = (struct zfcp_dbf_dump *)rec;
	unsigned long flags;
	int offset = 0;

	spin_lock_irqsave(&adapter->san_dbf_lock, flags);
	do {
		memset(rec, 0, sizeof(struct zfcp_san_dbf_record));
		if (offset == 0) {
			strncpy(rec->tag, tag, ZFCP_DBF_TAG_SIZE);
			rec->fsf_reqid = (unsigned long)fsf_req;
			rec->fsf_seqno = fsf_req->seq_no;
			rec->s_id = s_id;
			rec->d_id = d_id;
			rec->type.els.ls_code = ls_code;
			buflen = min(buflen, ZFCP_DBF_ELS_MAX_PAYLOAD);
			rec->type.els.payload_size = buflen;
			memcpy(rec->type.els.payload,
			       buffer, min(buflen, ZFCP_DBF_ELS_PAYLOAD));
			offset += min(buflen, ZFCP_DBF_ELS_PAYLOAD);
		} else {
			strncpy(dump->tag, "dump", ZFCP_DBF_TAG_SIZE);
			dump->total_size = buflen;
			dump->offset = offset;
			dump->size = min(buflen - offset,
					 (int)sizeof(struct zfcp_san_dbf_record)
					 - (int)sizeof(struct zfcp_dbf_dump));
			memcpy(dump->data, buffer + offset, dump->size);
			offset += dump->size;
		}
		debug_event(adapter->san_dbf, level,
			    rec, sizeof(struct zfcp_san_dbf_record));
	} while (offset < buflen);
	spin_unlock_irqrestore(&adapter->san_dbf_lock, flags);
}

inline void zfcp_san_dbf_event_els_request(struct zfcp_fsf_req *fsf_req)
{
	struct zfcp_send_els *els = (struct zfcp_send_els *)fsf_req->data;

	_zfcp_san_dbf_event_common_els("oels", 2, fsf_req,
				       fc_host_port_id(els->adapter->scsi_host),
				       els->d_id,
				       *(u8 *) zfcp_sg_to_address(els->req),
				       zfcp_sg_to_address(els->req),
				       els->req->length);
}

inline void zfcp_san_dbf_event_els_response(struct zfcp_fsf_req *fsf_req)
{
	struct zfcp_send_els *els = (struct zfcp_send_els *)fsf_req->data;

	_zfcp_san_dbf_event_common_els("rels", 2, fsf_req, els->d_id,
				       fc_host_port_id(els->adapter->scsi_host),
				       *(u8 *) zfcp_sg_to_address(els->req),
				       zfcp_sg_to_address(els->resp),
				       els->resp->length);
}

inline void zfcp_san_dbf_event_incoming_els(struct zfcp_fsf_req *fsf_req)
{
	struct zfcp_adapter *adapter = fsf_req->adapter;
	struct fsf_status_read_buffer *status_buffer =
	    (struct fsf_status_read_buffer *)fsf_req->data;
	int length = (int)status_buffer->length -
	    (int)((void *)&status_buffer->payload - (void *)status_buffer);

	_zfcp_san_dbf_event_common_els("iels", 1, fsf_req, status_buffer->d_id,
				       fc_host_port_id(adapter->scsi_host),
				       *(u8 *) status_buffer->payload,
				       (void *)status_buffer->payload, length);
}

static int
zfcp_san_dbf_view_format(debug_info_t * id, struct debug_view *view,
			 char *out_buf, const char *in_buf)
{
	struct zfcp_san_dbf_record *rec = (struct zfcp_san_dbf_record *)in_buf;
	char *buffer = NULL;
	int buflen = 0, total = 0;
	int len = 0;

	if (strncmp(rec->tag, "dump", ZFCP_DBF_TAG_SIZE) == 0)
		return 0;

	len += zfcp_dbf_tag(out_buf + len, "tag", rec->tag);
	len += zfcp_dbf_view(out_buf + len, "fsf_reqid", "0x%0Lx",
			     rec->fsf_reqid);
	len += zfcp_dbf_view(out_buf + len, "fsf_seqno", "0x%08x",
			     rec->fsf_seqno);
	len += zfcp_dbf_view(out_buf + len, "s_id", "0x%06x", rec->s_id);
	len += zfcp_dbf_view(out_buf + len, "d_id", "0x%06x", rec->d_id);

	if (strncmp(rec->tag, "octc", ZFCP_DBF_TAG_SIZE) == 0) {
		len += zfcp_dbf_view(out_buf + len, "cmd_req_code", "0x%04x",
				     rec->type.ct.type.request.cmd_req_code);
		len += zfcp_dbf_view(out_buf + len, "revision", "0x%02x",
				     rec->type.ct.type.request.revision);
		len += zfcp_dbf_view(out_buf + len, "gs_type", "0x%02x",
				     rec->type.ct.type.request.gs_type);
		len += zfcp_dbf_view(out_buf + len, "gs_subtype", "0x%02x",
				     rec->type.ct.type.request.gs_subtype);
		len += zfcp_dbf_view(out_buf + len, "options", "0x%02x",
				     rec->type.ct.type.request.options);
		len += zfcp_dbf_view(out_buf + len, "max_res_size", "0x%04x",
				     rec->type.ct.type.request.max_res_size);
		total = rec->type.ct.payload_size;
		buffer = rec->type.ct.payload;
		buflen = min(total, ZFCP_DBF_CT_PAYLOAD);
	} else if (strncmp(rec->tag, "rctc", ZFCP_DBF_TAG_SIZE) == 0) {
		len += zfcp_dbf_view(out_buf + len, "cmd_rsp_code", "0x%04x",
				     rec->type.ct.type.response.cmd_rsp_code);
		len += zfcp_dbf_view(out_buf + len, "revision", "0x%02x",
				     rec->type.ct.type.response.revision);
		len += zfcp_dbf_view(out_buf + len, "reason_code", "0x%02x",
				     rec->type.ct.type.response.reason_code);
		len +=
		    zfcp_dbf_view(out_buf + len, "reason_code_expl", "0x%02x",
				  rec->type.ct.type.response.reason_code_expl);
		len +=
		    zfcp_dbf_view(out_buf + len, "vendor_unique", "0x%02x",
				  rec->type.ct.type.response.vendor_unique);
		total = rec->type.ct.payload_size;
		buffer = rec->type.ct.payload;
		buflen = min(total, ZFCP_DBF_CT_PAYLOAD);
	} else if (strncmp(rec->tag, "oels", ZFCP_DBF_TAG_SIZE) == 0 ||
		   strncmp(rec->tag, "rels", ZFCP_DBF_TAG_SIZE) == 0 ||
		   strncmp(rec->tag, "iels", ZFCP_DBF_TAG_SIZE) == 0) {
		len += zfcp_dbf_view(out_buf + len, "ls_code", "0x%02x",
				     rec->type.els.ls_code);
		total = rec->type.els.payload_size;
		buffer = rec->type.els.payload;
		buflen = min(total, ZFCP_DBF_ELS_PAYLOAD);
	}

	len += zfcp_dbf_view_dump(out_buf + len, "payload",
				  buffer, buflen, 0, total);

	if (buflen == total)
		len += sprintf(out_buf + len, "\n");

	return len;
}

struct debug_view zfcp_san_dbf_view = {
	"structured",
	NULL,
	&zfcp_dbf_view_header,
	&zfcp_san_dbf_view_format,
	NULL,
	NULL
};

static inline void
_zfcp_scsi_dbf_event_common(const char *tag, const char *tag2, int level,
			    struct zfcp_adapter *adapter,
			    struct scsi_cmnd *scsi_cmnd,
			    struct zfcp_fsf_req *fsf_req,
			    struct zfcp_fsf_req *old_fsf_req)
{
	struct zfcp_scsi_dbf_record *rec = &adapter->scsi_dbf_buf;
	struct zfcp_dbf_dump *dump = (struct zfcp_dbf_dump *)rec;
	unsigned long flags;
	struct fcp_rsp_iu *fcp_rsp;
	char *fcp_rsp_info = NULL, *fcp_sns_info = NULL;
	int offset = 0, buflen = 0;

	spin_lock_irqsave(&adapter->scsi_dbf_lock, flags);
	do {
		memset(rec, 0, sizeof(struct zfcp_scsi_dbf_record));
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
				memcpy(rec->scsi_opcode, &scsi_cmnd->cmnd,
					min((int)scsi_cmnd->cmd_len,
						ZFCP_DBF_SCSI_OPCODE));
				rec->scsi_retries = scsi_cmnd->retries;
				rec->scsi_allowed = scsi_cmnd->allowed;
			}
			if (fsf_req != NULL) {
				fcp_rsp = (struct fcp_rsp_iu *)
				    &(fsf_req->qtcb->bottom.io.fcp_rsp);
				fcp_rsp_info =
				    zfcp_get_fcp_rsp_info_ptr(fcp_rsp);
				fcp_sns_info =
				    zfcp_get_fcp_sns_info_ptr(fcp_rsp);

				rec->type.fcp.rsp_validity =
				    fcp_rsp->validity.value;
				rec->type.fcp.rsp_scsi_status =
				    fcp_rsp->scsi_status;
				rec->type.fcp.rsp_resid = fcp_rsp->fcp_resid;
				if (fcp_rsp->validity.bits.fcp_rsp_len_valid)
					rec->type.fcp.rsp_code =
					    *(fcp_rsp_info + 3);
				if (fcp_rsp->validity.bits.fcp_sns_len_valid) {
					buflen = min((int)fcp_rsp->fcp_sns_len,
						     ZFCP_DBF_SCSI_MAX_FCP_SNS_INFO);
					rec->type.fcp.sns_info_len = buflen;
					memcpy(rec->type.fcp.sns_info,
					       fcp_sns_info,
					       min(buflen,
						   ZFCP_DBF_SCSI_FCP_SNS_INFO));
					offset += min(buflen,
						      ZFCP_DBF_SCSI_FCP_SNS_INFO);
				}

				rec->fsf_reqid = (unsigned long)fsf_req;
				rec->fsf_seqno = fsf_req->seq_no;
				rec->fsf_issued = fsf_req->issued;
			}
			rec->type.old_fsf_reqid =
				    (unsigned long) old_fsf_req;
		} else {
			strncpy(dump->tag, "dump", ZFCP_DBF_TAG_SIZE);
			dump->total_size = buflen;
			dump->offset = offset;
			dump->size = min(buflen - offset,
					 (int)sizeof(struct
						     zfcp_scsi_dbf_record) -
					 (int)sizeof(struct zfcp_dbf_dump));
			memcpy(dump->data, fcp_sns_info + offset, dump->size);
			offset += dump->size;
		}
		debug_event(adapter->scsi_dbf, level,
			    rec, sizeof(struct zfcp_scsi_dbf_record));
	} while (offset < buflen);
	spin_unlock_irqrestore(&adapter->scsi_dbf_lock, flags);
}

inline void
zfcp_scsi_dbf_event_result(const char *tag, int level,
			   struct zfcp_adapter *adapter,
			   struct scsi_cmnd *scsi_cmnd,
			   struct zfcp_fsf_req *fsf_req)
{
	_zfcp_scsi_dbf_event_common("rslt", tag, level,
			adapter, scsi_cmnd, fsf_req, NULL);
}

inline void
zfcp_scsi_dbf_event_abort(const char *tag, struct zfcp_adapter *adapter,
			  struct scsi_cmnd *scsi_cmnd,
			  struct zfcp_fsf_req *new_fsf_req,
			  struct zfcp_fsf_req *old_fsf_req)
{
	_zfcp_scsi_dbf_event_common("abrt", tag, 1,
			adapter, scsi_cmnd, new_fsf_req, old_fsf_req);
}

inline void
zfcp_scsi_dbf_event_devreset(const char *tag, u8 flag, struct zfcp_unit *unit,
			     struct scsi_cmnd *scsi_cmnd)
{
	struct zfcp_adapter *adapter = unit->port->adapter;

	_zfcp_scsi_dbf_event_common(flag == FCP_TARGET_RESET ? "trst" : "lrst",
			tag, 1, adapter, scsi_cmnd, NULL, NULL);
}

static int
zfcp_scsi_dbf_view_format(debug_info_t * id, struct debug_view *view,
			  char *out_buf, const char *in_buf)
{
	struct zfcp_scsi_dbf_record *rec =
	    (struct zfcp_scsi_dbf_record *)in_buf;
	int len = 0;

	if (strncmp(rec->tag, "dump", ZFCP_DBF_TAG_SIZE) == 0)
		return 0;

	len += zfcp_dbf_tag(out_buf + len, "tag", rec->tag);
	len += zfcp_dbf_tag(out_buf + len, "tag2", rec->tag2);
	len += zfcp_dbf_view(out_buf + len, "scsi_id", "0x%08x", rec->scsi_id);
	len += zfcp_dbf_view(out_buf + len, "scsi_lun", "0x%08x",
			     rec->scsi_lun);
	len += zfcp_dbf_view(out_buf + len, "scsi_result", "0x%08x",
			     rec->scsi_result);
	len += zfcp_dbf_view(out_buf + len, "scsi_cmnd", "0x%0Lx",
			     rec->scsi_cmnd);
	len += zfcp_dbf_view(out_buf + len, "scsi_serial", "0x%016Lx",
			     rec->scsi_serial);
	len += zfcp_dbf_view_dump(out_buf + len, "scsi_opcode",
				  rec->scsi_opcode,
				  ZFCP_DBF_SCSI_OPCODE,
				  0, ZFCP_DBF_SCSI_OPCODE);
	len += zfcp_dbf_view(out_buf + len, "scsi_retries", "0x%02x",
			     rec->scsi_retries);
	len += zfcp_dbf_view(out_buf + len, "scsi_allowed", "0x%02x",
			     rec->scsi_allowed);
	if (strncmp(rec->tag, "abrt", ZFCP_DBF_TAG_SIZE) == 0) {
		len += zfcp_dbf_view(out_buf + len, "old_fsf_reqid", "0x%0Lx",
				     rec->type.old_fsf_reqid);
	}
	len += zfcp_dbf_view(out_buf + len, "fsf_reqid", "0x%0Lx",
			     rec->fsf_reqid);
	len += zfcp_dbf_view(out_buf + len, "fsf_seqno", "0x%08x",
			     rec->fsf_seqno);
	len += zfcp_dbf_stck(out_buf + len, "fsf_issued", rec->fsf_issued);
	if (strncmp(rec->tag, "rslt", ZFCP_DBF_TAG_SIZE) == 0) {
		len +=
		    zfcp_dbf_view(out_buf + len, "fcp_rsp_validity", "0x%02x",
				  rec->type.fcp.rsp_validity);
		len +=
		    zfcp_dbf_view(out_buf + len, "fcp_rsp_scsi_status",
				  "0x%02x", rec->type.fcp.rsp_scsi_status);
		len +=
		    zfcp_dbf_view(out_buf + len, "fcp_rsp_resid", "0x%08x",
				  rec->type.fcp.rsp_resid);
		len +=
		    zfcp_dbf_view(out_buf + len, "fcp_rsp_code", "0x%08x",
				  rec->type.fcp.rsp_code);
		len +=
		    zfcp_dbf_view(out_buf + len, "fcp_sns_info_len", "0x%08x",
				  rec->type.fcp.sns_info_len);
		len +=
		    zfcp_dbf_view_dump(out_buf + len, "fcp_sns_info",
				       rec->type.fcp.sns_info,
				       min((int)rec->type.fcp.sns_info_len,
					   ZFCP_DBF_SCSI_FCP_SNS_INFO), 0,
				       rec->type.fcp.sns_info_len);
	}

	len += sprintf(out_buf + len, "\n");

	return len;
}

struct debug_view zfcp_scsi_dbf_view = {
	"structured",
	NULL,
	&zfcp_dbf_view_header,
	&zfcp_scsi_dbf_view_format,
	NULL,
	NULL
};

/**
 * zfcp_adapter_debug_register - registers debug feature for an adapter
 * @adapter: pointer to adapter for which debug features should be registered
 * return: -ENOMEM on error, 0 otherwise
 */
int zfcp_adapter_debug_register(struct zfcp_adapter *adapter)
{
	char dbf_name[DEBUG_MAX_NAME_LEN];

	/* debug feature area which records recovery activity */
	sprintf(dbf_name, "zfcp_%s_erp", zfcp_get_busid_by_adapter(adapter));
	adapter->erp_dbf = debug_register(dbf_name, dbfsize, 2,
					  sizeof(struct zfcp_erp_dbf_record));
	if (!adapter->erp_dbf)
		goto failed;
	debug_register_view(adapter->erp_dbf, &debug_hex_ascii_view);
	debug_set_level(adapter->erp_dbf, 3);

	/* debug feature area which records HBA (FSF and QDIO) conditions */
	sprintf(dbf_name, "zfcp_%s_hba", zfcp_get_busid_by_adapter(adapter));
	adapter->hba_dbf = debug_register(dbf_name, dbfsize, 1,
					  sizeof(struct zfcp_hba_dbf_record));
	if (!adapter->hba_dbf)
		goto failed;
	debug_register_view(adapter->hba_dbf, &debug_hex_ascii_view);
	debug_register_view(adapter->hba_dbf, &zfcp_hba_dbf_view);
	debug_set_level(adapter->hba_dbf, 3);

	/* debug feature area which records SAN command failures and recovery */
	sprintf(dbf_name, "zfcp_%s_san", zfcp_get_busid_by_adapter(adapter));
	adapter->san_dbf = debug_register(dbf_name, dbfsize, 1,
					  sizeof(struct zfcp_san_dbf_record));
	if (!adapter->san_dbf)
		goto failed;
	debug_register_view(adapter->san_dbf, &debug_hex_ascii_view);
	debug_register_view(adapter->san_dbf, &zfcp_san_dbf_view);
	debug_set_level(adapter->san_dbf, 6);

	/* debug feature area which records SCSI command failures and recovery */
	sprintf(dbf_name, "zfcp_%s_scsi", zfcp_get_busid_by_adapter(adapter));
	adapter->scsi_dbf = debug_register(dbf_name, dbfsize, 1,
					   sizeof(struct zfcp_scsi_dbf_record));
	if (!adapter->scsi_dbf)
		goto failed;
	debug_register_view(adapter->scsi_dbf, &debug_hex_ascii_view);
	debug_register_view(adapter->scsi_dbf, &zfcp_scsi_dbf_view);
	debug_set_level(adapter->scsi_dbf, 3);

	return 0;

 failed:
	zfcp_adapter_debug_unregister(adapter);

	return -ENOMEM;
}

/**
 * zfcp_adapter_debug_unregister - unregisters debug feature for an adapter
 * @adapter: pointer to adapter for which debug features should be unregistered
 */
void zfcp_adapter_debug_unregister(struct zfcp_adapter *adapter)
{
	debug_unregister(adapter->scsi_dbf);
	debug_unregister(adapter->san_dbf);
	debug_unregister(adapter->hba_dbf);
	debug_unregister(adapter->erp_dbf);
	adapter->scsi_dbf = NULL;
	adapter->san_dbf = NULL;
	adapter->hba_dbf = NULL;
	adapter->erp_dbf = NULL;
}

#undef ZFCP_LOG_AREA
