/*
 * This file is part of the zfcp device driver for
 * FCP adapters for IBM System z9 and zSeries.
 *
 * Copyright IBM Corp. 2008, 2009
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

#ifndef ZFCP_DBF_H
#define ZFCP_DBF_H

#include "zfcp_ext.h"
#include "zfcp_fsf.h"
#include "zfcp_def.h"

#define ZFCP_DBF_TAG_SIZE      4
#define ZFCP_DBF_ID_SIZE       7

struct zfcp_dbf_dump {
	u8 tag[ZFCP_DBF_TAG_SIZE];
	u32 total_size;		/* size of total dump data */
	u32 offset;		/* how much data has being already dumped */
	u32 size;		/* how much data comes with this record */
	u8 data[];		/* dump data */
} __attribute__ ((packed));

struct zfcp_dbf_rec_record_thread {
	u32 total;
	u32 ready;
	u32 running;
};

struct zfcp_dbf_rec_record_target {
	u64 ref;
	u32 status;
	u32 d_id;
	u64 wwpn;
	u64 fcp_lun;
	u32 erp_count;
};

struct zfcp_dbf_rec_record_trigger {
	u8 want;
	u8 need;
	u32 as;
	u32 ps;
	u32 us;
	u64 ref;
	u64 action;
	u64 wwpn;
	u64 fcp_lun;
};

struct zfcp_dbf_rec_record_action {
	u32 status;
	u32 step;
	u64 action;
	u64 fsf_req;
};

struct zfcp_dbf_rec_record {
	u8 id;
	char id2[7];
	union {
		struct zfcp_dbf_rec_record_action action;
		struct zfcp_dbf_rec_record_thread thread;
		struct zfcp_dbf_rec_record_target target;
		struct zfcp_dbf_rec_record_trigger trigger;
	} u;
};

enum {
	ZFCP_REC_DBF_ID_ACTION,
	ZFCP_REC_DBF_ID_THREAD,
	ZFCP_REC_DBF_ID_TARGET,
	ZFCP_REC_DBF_ID_TRIGGER,
};

struct zfcp_dbf_hba_record_response {
	u32 fsf_command;
	u64 fsf_reqid;
	u32 fsf_seqno;
	u64 fsf_issued;
	u32 fsf_prot_status;
	u32 fsf_status;
	u8 fsf_prot_status_qual[FSF_PROT_STATUS_QUAL_SIZE];
	u8 fsf_status_qual[FSF_STATUS_QUALIFIER_SIZE];
	u32 fsf_req_status;
	u8 sbal_first;
	u8 sbal_last;
	u8 sbal_response;
	u8 pool;
	u64 erp_action;
	union {
		struct {
			u64 cmnd;
			u64 serial;
		} fcp;
		struct {
			u64 wwpn;
			u32 d_id;
			u32 port_handle;
		} port;
		struct {
			u64 wwpn;
			u64 fcp_lun;
			u32 port_handle;
			u32 lun_handle;
		} unit;
		struct {
			u32 d_id;
			u8 ls_code;
		} els;
	} u;
} __attribute__ ((packed));

struct zfcp_dbf_hba_record_status {
	u8 failed;
	u32 status_type;
	u32 status_subtype;
	struct fsf_queue_designator
	 queue_designator;
	u32 payload_size;
#define ZFCP_DBF_UNSOL_PAYLOAD				80
#define ZFCP_DBF_UNSOL_PAYLOAD_SENSE_DATA_AVAIL		32
#define ZFCP_DBF_UNSOL_PAYLOAD_BIT_ERROR_THRESHOLD	56
#define ZFCP_DBF_UNSOL_PAYLOAD_FEATURE_UPDATE_ALERT	2 * sizeof(u32)
	u8 payload[ZFCP_DBF_UNSOL_PAYLOAD];
} __attribute__ ((packed));

struct zfcp_dbf_hba_record_qdio {
	u32 qdio_error;
	u8 sbal_index;
	u8 sbal_count;
} __attribute__ ((packed));

struct zfcp_dbf_hba_record {
	u8 tag[ZFCP_DBF_TAG_SIZE];
	u8 tag2[ZFCP_DBF_TAG_SIZE];
	union {
		struct zfcp_dbf_hba_record_response response;
		struct zfcp_dbf_hba_record_status status;
		struct zfcp_dbf_hba_record_qdio qdio;
		struct fsf_bit_error_payload berr;
	} u;
} __attribute__ ((packed));

struct zfcp_dbf_san_record_ct_request {
	u16 cmd_req_code;
	u8 revision;
	u8 gs_type;
	u8 gs_subtype;
	u8 options;
	u16 max_res_size;
	u32 len;
} __attribute__ ((packed));

struct zfcp_dbf_san_record_ct_response {
	u16 cmd_rsp_code;
	u8 revision;
	u8 reason_code;
	u8 expl;
	u8 vendor_unique;
	u16 max_res_size;
	u32 len;
} __attribute__ ((packed));

struct zfcp_dbf_san_record_els {
	u8 ls_code;
	u32 len;
} __attribute__ ((packed));

struct zfcp_dbf_san_record {
	u8 tag[ZFCP_DBF_TAG_SIZE];
	u64 fsf_reqid;
	u32 fsf_seqno;
	u32 s_id;
	u32 d_id;
	union {
		struct zfcp_dbf_san_record_ct_request ct_req;
		struct zfcp_dbf_san_record_ct_response ct_resp;
		struct zfcp_dbf_san_record_els els;
	} u;
#define ZFCP_DBF_SAN_MAX_PAYLOAD 1024
	u8 payload[32];
} __attribute__ ((packed));

struct zfcp_dbf_scsi_record {
	u8 tag[ZFCP_DBF_TAG_SIZE];
	u8 tag2[ZFCP_DBF_TAG_SIZE];
	u32 scsi_id;
	u32 scsi_lun;
	u32 scsi_result;
	u64 scsi_cmnd;
	u64 scsi_serial;
#define ZFCP_DBF_SCSI_OPCODE	16
	u8 scsi_opcode[ZFCP_DBF_SCSI_OPCODE];
	u8 scsi_retries;
	u8 scsi_allowed;
	u64 fsf_reqid;
	u32 fsf_seqno;
	u64 fsf_issued;
	u64 old_fsf_reqid;
	u8 rsp_validity;
	u8 rsp_scsi_status;
	u32 rsp_resid;
	u8 rsp_code;
#define ZFCP_DBF_SCSI_FCP_SNS_INFO	16
#define ZFCP_DBF_SCSI_MAX_FCP_SNS_INFO	256
	u32 sns_info_len;
	u8 sns_info[ZFCP_DBF_SCSI_FCP_SNS_INFO];
} __attribute__ ((packed));

struct zfcp_dbf {
	debug_info_t			*rec;
	debug_info_t			*hba;
	debug_info_t			*san;
	debug_info_t			*scsi;
	spinlock_t			rec_lock;
	spinlock_t			hba_lock;
	spinlock_t			san_lock;
	spinlock_t			scsi_lock;
	struct zfcp_dbf_rec_record	rec_buf;
	struct zfcp_dbf_hba_record	hba_buf;
	struct zfcp_dbf_san_record	san_buf;
	struct zfcp_dbf_scsi_record	scsi_buf;
	struct zfcp_adapter		*adapter;
};

static inline
void zfcp_dbf_hba_fsf_resp(const char *tag2, int level,
			   struct zfcp_fsf_req *req, struct zfcp_dbf *dbf)
{
	if (level <= dbf->hba->level)
		_zfcp_dbf_hba_fsf_response(tag2, level, req, dbf);
}

/**
 * zfcp_dbf_hba_fsf_response - trace event for request completion
 * @fsf_req: request that has been completed
 */
static inline void zfcp_dbf_hba_fsf_response(struct zfcp_fsf_req *req)
{
	struct zfcp_dbf *dbf = req->adapter->dbf;
	struct fsf_qtcb *qtcb = req->qtcb;

	if ((qtcb->prefix.prot_status != FSF_PROT_GOOD) &&
	    (qtcb->prefix.prot_status != FSF_PROT_FSF_STATUS_PRESENTED)) {
		zfcp_dbf_hba_fsf_resp("perr", 1, req, dbf);

	} else if (qtcb->header.fsf_status != FSF_GOOD) {
		zfcp_dbf_hba_fsf_resp("ferr", 1, req, dbf);

	} else if ((req->fsf_command == FSF_QTCB_OPEN_PORT_WITH_DID) ||
		   (req->fsf_command == FSF_QTCB_OPEN_LUN)) {
		zfcp_dbf_hba_fsf_resp("open", 4, req, dbf);

	} else if (qtcb->header.log_length) {
		zfcp_dbf_hba_fsf_resp("qtcb", 5, req, dbf);

	} else {
		zfcp_dbf_hba_fsf_resp("norm", 6, req, dbf);
	}
 }

/**
 * zfcp_dbf_hba_fsf_unsol - trace event for an unsolicited status buffer
 * @tag: tag indicating which kind of unsolicited status has been received
 * @dbf: reference to dbf structure
 * @status_buffer: buffer containing payload of unsolicited status
 */
static inline
void zfcp_dbf_hba_fsf_unsol(const char *tag, struct zfcp_dbf *dbf,
			    struct fsf_status_read_buffer *buf)
{
	int level = 2;

	if (level <= dbf->hba->level)
		_zfcp_dbf_hba_fsf_unsol(tag, level, dbf, buf);
}

static inline
void zfcp_dbf_scsi(const char *tag, const char *tag2, int level,
		   struct zfcp_dbf *dbf, struct scsi_cmnd *scmd,
		   struct zfcp_fsf_req *req, unsigned long old_id)
{
	if (level <= dbf->scsi->level)
		_zfcp_dbf_scsi(tag, tag2, level, dbf, scmd, req, old_id);
}

/**
 * zfcp_dbf_scsi_result - trace event for SCSI command completion
 * @tag: tag indicating success or failure of SCSI command
 * @level: trace level applicable for this event
 * @adapter: adapter that has been used to issue the SCSI command
 * @scmd: SCSI command pointer
 * @fsf_req: request used to issue SCSI command (might be NULL)
 */
static inline
void zfcp_dbf_scsi_result(const char *tag, int level, struct zfcp_dbf *dbf,
			  struct scsi_cmnd *scmd, struct zfcp_fsf_req *fsf_req)
{
	zfcp_dbf_scsi("rslt", tag, level, dbf, scmd, fsf_req, 0);
}

/**
 * zfcp_dbf_scsi_abort - trace event for SCSI command abort
 * @tag: tag indicating success or failure of abort operation
 * @adapter: adapter thas has been used to issue SCSI command to be aborted
 * @scmd: SCSI command to be aborted
 * @new_req: request containing abort (might be NULL)
 * @old_id: identifier of request containg SCSI command to be aborted
 */
static inline
void zfcp_dbf_scsi_abort(const char *tag, struct zfcp_dbf *dbf,
			 struct scsi_cmnd *scmd, struct zfcp_fsf_req *new_req,
			 unsigned long old_id)
{
	zfcp_dbf_scsi("abrt", tag, 1, dbf, scmd, new_req, old_id);
}

/**
 * zfcp_dbf_scsi_devreset - trace event for Logical Unit or Target Reset
 * @tag: tag indicating success or failure of reset operation
 * @flag: indicates type of reset (Target Reset, Logical Unit Reset)
 * @unit: unit that needs reset
 * @scsi_cmnd: SCSI command which caused this error recovery
 */
static inline
void zfcp_dbf_scsi_devreset(const char *tag, u8 flag, struct zfcp_unit *unit,
			    struct scsi_cmnd *scsi_cmnd)
{
	zfcp_dbf_scsi(flag == FCP_TARGET_RESET ? "trst" : "lrst", tag, 1,
			    unit->port->adapter->dbf, scsi_cmnd, NULL, 0);
}

#endif /* ZFCP_DBF_H */
