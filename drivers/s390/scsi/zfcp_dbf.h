/*
 * This file is part of the zfcp device driver for
 * FCP adapters for IBM System z9 and zSeries.
 *
 * Copyright IBM Corp. 2008, 2008
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

#include "zfcp_fsf.h"

#define ZFCP_DBF_TAG_SIZE      4
#define ZFCP_DBF_ID_SIZE       7

struct zfcp_dbf_dump {
	u8 tag[ZFCP_DBF_TAG_SIZE];
	u32 total_size;		/* size of total dump data */
	u32 offset;		/* how much data has being already dumped */
	u32 size;		/* how much data comes with this record */
	u8 data[];		/* dump data */
} __attribute__ ((packed));

struct zfcp_rec_dbf_record_thread {
	u32 total;
	u32 ready;
	u32 running;
};

struct zfcp_rec_dbf_record_target {
	u64 ref;
	u32 status;
	u32 d_id;
	u64 wwpn;
	u64 fcp_lun;
	u32 erp_count;
};

struct zfcp_rec_dbf_record_trigger {
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

struct zfcp_rec_dbf_record_action {
	u32 status;
	u32 step;
	u64 action;
	u64 fsf_req;
};

struct zfcp_rec_dbf_record {
	u8 id;
	char id2[7];
	union {
		struct zfcp_rec_dbf_record_action action;
		struct zfcp_rec_dbf_record_thread thread;
		struct zfcp_rec_dbf_record_target target;
		struct zfcp_rec_dbf_record_trigger trigger;
	} u;
};

enum {
	ZFCP_REC_DBF_ID_ACTION,
	ZFCP_REC_DBF_ID_THREAD,
	ZFCP_REC_DBF_ID_TARGET,
	ZFCP_REC_DBF_ID_TRIGGER,
};

struct zfcp_hba_dbf_record_response {
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

struct zfcp_hba_dbf_record_status {
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

struct zfcp_hba_dbf_record_qdio {
	u32 qdio_error;
	u8 sbal_index;
	u8 sbal_count;
} __attribute__ ((packed));

struct zfcp_hba_dbf_record {
	u8 tag[ZFCP_DBF_TAG_SIZE];
	u8 tag2[ZFCP_DBF_TAG_SIZE];
	union {
		struct zfcp_hba_dbf_record_response response;
		struct zfcp_hba_dbf_record_status status;
		struct zfcp_hba_dbf_record_qdio qdio;
		struct fsf_bit_error_payload berr;
	} u;
} __attribute__ ((packed));

struct zfcp_san_dbf_record_ct_request {
	u16 cmd_req_code;
	u8 revision;
	u8 gs_type;
	u8 gs_subtype;
	u8 options;
	u16 max_res_size;
	u32 len;
} __attribute__ ((packed));

struct zfcp_san_dbf_record_ct_response {
	u16 cmd_rsp_code;
	u8 revision;
	u8 reason_code;
	u8 expl;
	u8 vendor_unique;
	u16 max_res_size;
	u32 len;
} __attribute__ ((packed));

struct zfcp_san_dbf_record_els {
	u8 ls_code;
	u32 len;
} __attribute__ ((packed));

struct zfcp_san_dbf_record {
	u8 tag[ZFCP_DBF_TAG_SIZE];
	u64 fsf_reqid;
	u32 fsf_seqno;
	u32 s_id;
	u32 d_id;
	union {
		struct zfcp_san_dbf_record_ct_request ct_req;
		struct zfcp_san_dbf_record_ct_response ct_resp;
		struct zfcp_san_dbf_record_els els;
	} u;
#define ZFCP_DBF_SAN_MAX_PAYLOAD 1024
	u8 payload[32];
} __attribute__ ((packed));

struct zfcp_scsi_dbf_record {
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

#endif /* ZFCP_DBF_H */
