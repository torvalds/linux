/*
 * ibmvfc.h -- driver for IBM Power Virtual Fibre Channel Adapter
 *
 * Written By: Brian King <brking@linux.vnet.ibm.com>, IBM Corporation
 *
 * Copyright (C) IBM Corporation, 2008
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef _IBMVFC_H
#define _IBMVFC_H

#include <linux/list.h>
#include <linux/types.h>
#include "viosrp.h"

#define IBMVFC_NAME	"ibmvfc"
#define IBMVFC_DRIVER_VERSION		"1.0.6"
#define IBMVFC_DRIVER_DATE		"(May 28, 2009)"

#define IBMVFC_DEFAULT_TIMEOUT	60
#define IBMVFC_ADISC_CANCEL_TIMEOUT	45
#define IBMVFC_ADISC_TIMEOUT		15
#define IBMVFC_ADISC_PLUS_CANCEL_TIMEOUT	\
		(IBMVFC_ADISC_TIMEOUT + IBMVFC_ADISC_CANCEL_TIMEOUT)
#define IBMVFC_INIT_TIMEOUT		120
#define IBMVFC_ABORT_WAIT_TIMEOUT	40
#define IBMVFC_MAX_REQUESTS_DEFAULT	100

#define IBMVFC_DEBUG			0
#define IBMVFC_MAX_TARGETS		1024
#define IBMVFC_MAX_LUN			0xffffffff
#define IBMVFC_MAX_SECTORS		0xffffu
#define IBMVFC_MAX_DISC_THREADS	4
#define IBMVFC_TGT_MEMPOOL_SZ		64
#define IBMVFC_MAX_CMDS_PER_LUN	64
#define IBMVFC_MAX_HOST_INIT_RETRIES	6
#define IBMVFC_MAX_TGT_INIT_RETRIES		3
#define IBMVFC_DEV_LOSS_TMO		(5 * 60)
#define IBMVFC_DEFAULT_LOG_LEVEL	2
#define IBMVFC_MAX_CDB_LEN		16

/*
 * Ensure we have resources for ERP and initialization:
 * 1 for ERP
 * 1 for initialization
 * 1 for NPIV Logout
 * 2 for each discovery thread
 */
#define IBMVFC_NUM_INTERNAL_REQ	(1 + 1 + 1 + (disc_threads * 2))

#define IBMVFC_MAD_SUCCESS		0x00
#define IBMVFC_MAD_NOT_SUPPORTED	0xF1
#define IBMVFC_MAD_FAILED		0xF7
#define IBMVFC_MAD_DRIVER_FAILED	0xEE
#define IBMVFC_MAD_CRQ_ERROR		0xEF

enum ibmvfc_crq_valid {
	IBMVFC_CRQ_CMD_RSP		= 0x80,
	IBMVFC_CRQ_INIT_RSP		= 0xC0,
	IBMVFC_CRQ_XPORT_EVENT		= 0xFF,
};

enum ibmvfc_crq_format {
	IBMVFC_CRQ_INIT			= 0x01,
	IBMVFC_CRQ_INIT_COMPLETE	= 0x02,
	IBMVFC_PARTITION_MIGRATED	= 0x06,
};

enum ibmvfc_cmd_status_flags {
	IBMVFC_FABRIC_MAPPED		= 0x0001,
	IBMVFC_VIOS_FAILURE		= 0x0002,
	IBMVFC_FC_FAILURE			= 0x0004,
	IBMVFC_FC_SCSI_ERROR		= 0x0008,
	IBMVFC_HW_EVENT_LOGGED		= 0x0010,
	IBMVFC_VIOS_LOGGED		= 0x0020,
};

enum ibmvfc_fabric_mapped_errors {
	IBMVFC_UNABLE_TO_ESTABLISH	= 0x0001,
	IBMVFC_XPORT_FAULT		= 0x0002,
	IBMVFC_CMD_TIMEOUT		= 0x0003,
	IBMVFC_ENETDOWN			= 0x0004,
	IBMVFC_HW_FAILURE			= 0x0005,
	IBMVFC_LINK_DOWN_ERR		= 0x0006,
	IBMVFC_LINK_DEAD_ERR		= 0x0007,
	IBMVFC_UNABLE_TO_REGISTER	= 0x0008,
	IBMVFC_XPORT_BUSY			= 0x000A,
	IBMVFC_XPORT_DEAD			= 0x000B,
	IBMVFC_CONFIG_ERROR		= 0x000C,
	IBMVFC_NAME_SERVER_FAIL		= 0x000D,
	IBMVFC_LINK_HALTED		= 0x000E,
	IBMVFC_XPORT_GENERAL		= 0x8000,
};

enum ibmvfc_vios_errors {
	IBMVFC_CRQ_FAILURE			= 0x0001,
	IBMVFC_SW_FAILURE				= 0x0002,
	IBMVFC_INVALID_PARAMETER		= 0x0003,
	IBMVFC_MISSING_PARAMETER		= 0x0004,
	IBMVFC_HOST_IO_BUS			= 0x0005,
	IBMVFC_TRANS_CANCELLED			= 0x0006,
	IBMVFC_TRANS_CANCELLED_IMPLICIT	= 0x0007,
	IBMVFC_INSUFFICIENT_RESOURCE		= 0x0008,
	IBMVFC_PLOGI_REQUIRED			= 0x0010,
	IBMVFC_COMMAND_FAILED			= 0x8000,
};

enum ibmvfc_mad_types {
	IBMVFC_NPIV_LOGIN		= 0x0001,
	IBMVFC_DISC_TARGETS	= 0x0002,
	IBMVFC_PORT_LOGIN		= 0x0004,
	IBMVFC_PROCESS_LOGIN	= 0x0008,
	IBMVFC_QUERY_TARGET	= 0x0010,
	IBMVFC_IMPLICIT_LOGOUT	= 0x0040,
	IBMVFC_PASSTHRU		= 0x0200,
	IBMVFC_TMF_MAD		= 0x0100,
	IBMVFC_NPIV_LOGOUT	= 0x0800,
};

struct ibmvfc_mad_common {
	u32 version;
	u32 reserved;
	u32 opcode;
	u16 status;
	u16 length;
	u64 tag;
}__attribute__((packed, aligned (8)));

struct ibmvfc_npiv_login_mad {
	struct ibmvfc_mad_common common;
	struct srp_direct_buf buffer;
}__attribute__((packed, aligned (8)));

struct ibmvfc_npiv_logout_mad {
	struct ibmvfc_mad_common common;
}__attribute__((packed, aligned (8)));

#define IBMVFC_MAX_NAME 256

struct ibmvfc_npiv_login {
	u32 ostype;
#define IBMVFC_OS_LINUX	0x02
	u32 pad;
	u64 max_dma_len;
	u32 max_payload;
	u32 max_response;
	u32 partition_num;
	u32 vfc_frame_version;
	u16 fcp_version;
	u16 flags;
#define IBMVFC_CLIENT_MIGRATED	0x01
#define IBMVFC_FLUSH_ON_HALT		0x02
	u32 max_cmds;
	u64 capabilities;
#define IBMVFC_CAN_MIGRATE		0x01
	u64 node_name;
	struct srp_direct_buf async;
	u8 partition_name[IBMVFC_MAX_NAME];
	u8 device_name[IBMVFC_MAX_NAME];
	u8 drc_name[IBMVFC_MAX_NAME];
	u64 reserved2[2];
}__attribute__((packed, aligned (8)));

struct ibmvfc_common_svc_parms {
	u16 fcph_version;
	u16 b2b_credit;
	u16 features;
	u16 bb_rcv_sz; /* upper nibble is BB_SC_N */
	u32 ratov;
	u32 edtov;
}__attribute__((packed, aligned (4)));

struct ibmvfc_service_parms {
	struct ibmvfc_common_svc_parms common;
	u8 port_name[8];
	u8 node_name[8];
	u32 class1_parms[4];
	u32 class2_parms[4];
	u32 class3_parms[4];
	u32 obsolete[4];
	u32 vendor_version[4];
	u32 services_avail[2];
	u32 ext_len;
	u32 reserved[30];
	u32 clk_sync_qos[2];
}__attribute__((packed, aligned (4)));

struct ibmvfc_npiv_login_resp {
	u32 version;
	u16 status;
	u16 error;
	u32 flags;
#define IBMVFC_NATIVE_FC		0x01
#define IBMVFC_CAN_FLUSH_ON_HALT	0x08
	u32 reserved;
	u64 capabilities;
#define IBMVFC_CAN_FLUSH_ON_HALT	0x08
	u32 max_cmds;
	u32 scsi_id_sz;
	u64 max_dma_len;
	u64 scsi_id;
	u64 port_name;
	u64 node_name;
	u64 link_speed;
	u8 partition_name[IBMVFC_MAX_NAME];
	u8 device_name[IBMVFC_MAX_NAME];
	u8 port_loc_code[IBMVFC_MAX_NAME];
	u8 drc_name[IBMVFC_MAX_NAME];
	struct ibmvfc_service_parms service_parms;
	u64 reserved2;
}__attribute__((packed, aligned (8)));

union ibmvfc_npiv_login_data {
	struct ibmvfc_npiv_login login;
	struct ibmvfc_npiv_login_resp resp;
}__attribute__((packed, aligned (8)));

struct ibmvfc_discover_targets_buf {
	u32 scsi_id[1];
#define IBMVFC_DISC_TGT_SCSI_ID_MASK	0x00ffffff
};

struct ibmvfc_discover_targets {
	struct ibmvfc_mad_common common;
	struct srp_direct_buf buffer;
	u32 flags;
	u16 status;
	u16 error;
	u32 bufflen;
	u32 num_avail;
	u32 num_written;
	u64 reserved[2];
}__attribute__((packed, aligned (8)));

enum ibmvfc_fc_reason {
	IBMVFC_INVALID_ELS_CMD_CODE	= 0x01,
	IBMVFC_INVALID_VERSION		= 0x02,
	IBMVFC_LOGICAL_ERROR		= 0x03,
	IBMVFC_INVALID_CT_IU_SIZE	= 0x04,
	IBMVFC_LOGICAL_BUSY		= 0x05,
	IBMVFC_PROTOCOL_ERROR		= 0x07,
	IBMVFC_UNABLE_TO_PERFORM_REQ	= 0x09,
	IBMVFC_CMD_NOT_SUPPORTED	= 0x0B,
	IBMVFC_SERVER_NOT_AVAIL		= 0x0D,
	IBMVFC_CMD_IN_PROGRESS		= 0x0E,
	IBMVFC_VENDOR_SPECIFIC		= 0xFF,
};

enum ibmvfc_fc_type {
	IBMVFC_FABRIC_REJECT	= 0x01,
	IBMVFC_PORT_REJECT	= 0x02,
	IBMVFC_LS_REJECT		= 0x03,
	IBMVFC_FABRIC_BUSY	= 0x04,
	IBMVFC_PORT_BUSY		= 0x05,
	IBMVFC_BASIC_REJECT	= 0x06,
};

enum ibmvfc_gs_explain {
	IBMVFC_PORT_NAME_NOT_REG	= 0x02,
};

struct ibmvfc_port_login {
	struct ibmvfc_mad_common common;
	u64 scsi_id;
	u16 reserved;
	u16 fc_service_class;
	u32 blksz;
	u32 hdr_per_blk;
	u16 status;
	u16 error;		/* also fc_reason */
	u16 fc_explain;
	u16 fc_type;
	u32 reserved2;
	struct ibmvfc_service_parms service_parms;
	struct ibmvfc_service_parms service_parms_change;
	u64 reserved3[2];
}__attribute__((packed, aligned (8)));

struct ibmvfc_prli_svc_parms {
	u8 type;
#define IBMVFC_SCSI_FCP_TYPE		0x08
	u8 type_ext;
	u16 flags;
#define IBMVFC_PRLI_ORIG_PA_VALID			0x8000
#define IBMVFC_PRLI_RESP_PA_VALID			0x4000
#define IBMVFC_PRLI_EST_IMG_PAIR			0x2000
	u32 orig_pa;
	u32 resp_pa;
	u32 service_parms;
#define IBMVFC_PRLI_TASK_RETRY			0x00000200
#define IBMVFC_PRLI_RETRY				0x00000100
#define IBMVFC_PRLI_DATA_OVERLAY			0x00000040
#define IBMVFC_PRLI_INITIATOR_FUNC			0x00000020
#define IBMVFC_PRLI_TARGET_FUNC			0x00000010
#define IBMVFC_PRLI_READ_FCP_XFER_RDY_DISABLED	0x00000002
#define IBMVFC_PRLI_WR_FCP_XFER_RDY_DISABLED	0x00000001
}__attribute__((packed, aligned (4)));

struct ibmvfc_process_login {
	struct ibmvfc_mad_common common;
	u64 scsi_id;
	struct ibmvfc_prli_svc_parms parms;
	u8 reserved[48];
	u16 status;
	u16 error;			/* also fc_reason */
	u32 reserved2;
	u64 reserved3[2];
}__attribute__((packed, aligned (8)));

struct ibmvfc_query_tgt {
	struct ibmvfc_mad_common common;
	u64 wwpn;
	u64 scsi_id;
	u16 status;
	u16 error;
	u16 fc_explain;
	u16 fc_type;
	u64 reserved[2];
}__attribute__((packed, aligned (8)));

struct ibmvfc_implicit_logout {
	struct ibmvfc_mad_common common;
	u64 old_scsi_id;
	u64 reserved[2];
}__attribute__((packed, aligned (8)));

struct ibmvfc_tmf {
	struct ibmvfc_mad_common common;
	u64 scsi_id;
	struct scsi_lun lun;
	u32 flags;
#define IBMVFC_TMF_ABORT_TASK		0x02
#define IBMVFC_TMF_ABORT_TASK_SET	0x04
#define IBMVFC_TMF_LUN_RESET		0x10
#define IBMVFC_TMF_TGT_RESET		0x20
#define IBMVFC_TMF_LUA_VALID		0x40
	u32 cancel_key;
	u32 my_cancel_key;
	u32 pad;
	u64 reserved[2];
}__attribute__((packed, aligned (8)));

enum ibmvfc_fcp_rsp_info_codes {
	RSP_NO_FAILURE		= 0x00,
	RSP_TMF_REJECTED		= 0x04,
	RSP_TMF_FAILED		= 0x05,
	RSP_TMF_INVALID_LUN	= 0x09,
};

struct ibmvfc_fcp_rsp_info {
	u16 reserved;
	u8 rsp_code;
	u8 reserved2[4];
}__attribute__((packed, aligned (2)));

enum ibmvfc_fcp_rsp_flags {
	FCP_BIDI_RSP			= 0x80,
	FCP_BIDI_READ_RESID_UNDER	= 0x40,
	FCP_BIDI_READ_RESID_OVER	= 0x20,
	FCP_CONF_REQ			= 0x10,
	FCP_RESID_UNDER			= 0x08,
	FCP_RESID_OVER			= 0x04,
	FCP_SNS_LEN_VALID			= 0x02,
	FCP_RSP_LEN_VALID			= 0x01,
};

union ibmvfc_fcp_rsp_data {
	struct ibmvfc_fcp_rsp_info info;
	u8 sense[SCSI_SENSE_BUFFERSIZE + sizeof(struct ibmvfc_fcp_rsp_info)];
}__attribute__((packed, aligned (8)));

struct ibmvfc_fcp_rsp {
	u64 reserved;
	u16 retry_delay_timer;
	u8 flags;
	u8 scsi_status;
	u32 fcp_resid;
	u32 fcp_sense_len;
	u32 fcp_rsp_len;
	union ibmvfc_fcp_rsp_data data;
}__attribute__((packed, aligned (8)));

enum ibmvfc_cmd_flags {
	IBMVFC_SCATTERLIST	= 0x0001,
	IBMVFC_NO_MEM_DESC	= 0x0002,
	IBMVFC_READ			= 0x0004,
	IBMVFC_WRITE		= 0x0008,
	IBMVFC_TMF			= 0x0080,
	IBMVFC_CLASS_3_ERR	= 0x0100,
};

enum ibmvfc_fc_task_attr {
	IBMVFC_SIMPLE_TASK	= 0x00,
	IBMVFC_HEAD_OF_QUEUE	= 0x01,
	IBMVFC_ORDERED_TASK	= 0x02,
	IBMVFC_ACA_TASK		= 0x04,
};

enum ibmvfc_fc_tmf_flags {
	IBMVFC_ABORT_TASK_SET	= 0x02,
	IBMVFC_LUN_RESET		= 0x10,
	IBMVFC_TARGET_RESET	= 0x20,
};

struct ibmvfc_fcp_cmd_iu {
	struct scsi_lun lun;
	u8 crn;
	u8 pri_task_attr;
	u8 tmf_flags;
	u8 add_cdb_len;
#define IBMVFC_RDDATA		0x02
#define IBMVFC_WRDATA		0x01
	u8 cdb[IBMVFC_MAX_CDB_LEN];
	u32 xfer_len;
}__attribute__((packed, aligned (4)));

struct ibmvfc_cmd {
	u64 task_tag;
	u32 frame_type;
	u32 payload_len;
	u32 resp_len;
	u32 adapter_resid;
	u16 status;
	u16 error;
	u16 flags;
	u16 response_flags;
#define IBMVFC_ADAPTER_RESID_VALID	0x01
	u32 cancel_key;
	u32 exchange_id;
	struct srp_direct_buf ext_func;
	struct srp_direct_buf ioba;
	struct srp_direct_buf resp;
	u64 correlation;
	u64 tgt_scsi_id;
	u64 tag;
	u64 reserved3[2];
	struct ibmvfc_fcp_cmd_iu iu;
	struct ibmvfc_fcp_rsp rsp;
}__attribute__((packed, aligned (8)));

struct ibmvfc_passthru_fc_iu {
	u32 payload[7];
#define IBMVFC_ADISC	0x52000000
	u32 response[7];
};

struct ibmvfc_passthru_iu {
	u64 task_tag;
	u32 cmd_len;
	u32 rsp_len;
	u16 status;
	u16 error;
	u32 flags;
#define IBMVFC_FC_ELS		0x01
	u32 cancel_key;
	u32 reserved;
	struct srp_direct_buf cmd;
	struct srp_direct_buf rsp;
	u64 correlation;
	u64 scsi_id;
	u64 tag;
	u64 reserved2[2];
}__attribute__((packed, aligned (8)));

struct ibmvfc_passthru_mad {
	struct ibmvfc_mad_common common;
	struct srp_direct_buf cmd_ioba;
	struct ibmvfc_passthru_iu iu;
	struct ibmvfc_passthru_fc_iu fc_iu;
}__attribute__((packed, aligned (8)));

struct ibmvfc_trace_start_entry {
	u32 xfer_len;
}__attribute__((packed));

struct ibmvfc_trace_end_entry {
	u16 status;
	u16 error;
	u8 fcp_rsp_flags;
	u8 rsp_code;
	u8 scsi_status;
	u8 reserved;
}__attribute__((packed));

struct ibmvfc_trace_entry {
	struct ibmvfc_event *evt;
	u32 time;
	u32 scsi_id;
	u32 lun;
	u8 fmt;
	u8 op_code;
	u8 tmf_flags;
	u8 type;
#define IBMVFC_TRC_START	0x00
#define IBMVFC_TRC_END		0xff
	union {
		struct ibmvfc_trace_start_entry start;
		struct ibmvfc_trace_end_entry end;
	} u;
}__attribute__((packed, aligned (8)));

enum ibmvfc_crq_formats {
	IBMVFC_CMD_FORMAT		= 0x01,
	IBMVFC_ASYNC_EVENT	= 0x02,
	IBMVFC_MAD_FORMAT		= 0x04,
};

enum ibmvfc_async_event {
	IBMVFC_AE_ELS_PLOGI		= 0x0001,
	IBMVFC_AE_ELS_LOGO		= 0x0002,
	IBMVFC_AE_ELS_PRLO		= 0x0004,
	IBMVFC_AE_SCN_NPORT		= 0x0008,
	IBMVFC_AE_SCN_GROUP		= 0x0010,
	IBMVFC_AE_SCN_DOMAIN		= 0x0020,
	IBMVFC_AE_SCN_FABRIC		= 0x0040,
	IBMVFC_AE_LINK_UP			= 0x0080,
	IBMVFC_AE_LINK_DOWN		= 0x0100,
	IBMVFC_AE_LINK_DEAD		= 0x0200,
	IBMVFC_AE_HALT			= 0x0400,
	IBMVFC_AE_RESUME			= 0x0800,
	IBMVFC_AE_ADAPTER_FAILED	= 0x1000,
};

struct ibmvfc_crq {
	volatile u8 valid;
	volatile u8 format;
	u8 reserved[6];
	volatile u64 ioba;
}__attribute__((packed, aligned (8)));

struct ibmvfc_crq_queue {
	struct ibmvfc_crq *msgs;
	int size, cur;
	dma_addr_t msg_token;
};

enum ibmvfc_ae_link_state {
	IBMVFC_AE_LS_LINK_UP		= 0x01,
	IBMVFC_AE_LS_LINK_BOUNCED	= 0x02,
	IBMVFC_AE_LS_LINK_DOWN		= 0x04,
	IBMVFC_AE_LS_LINK_DEAD		= 0x08,
};

struct ibmvfc_async_crq {
	volatile u8 valid;
	u8 link_state;
	u8 pad[2];
	u32 pad2;
	volatile u64 event;
	volatile u64 scsi_id;
	volatile u64 wwpn;
	volatile u64 node_name;
	u64 reserved;
}__attribute__((packed, aligned (8)));

struct ibmvfc_async_crq_queue {
	struct ibmvfc_async_crq *msgs;
	int size, cur;
	dma_addr_t msg_token;
};

union ibmvfc_iu {
	struct ibmvfc_mad_common mad_common;
	struct ibmvfc_npiv_login_mad npiv_login;
	struct ibmvfc_npiv_logout_mad npiv_logout;
	struct ibmvfc_discover_targets discover_targets;
	struct ibmvfc_port_login plogi;
	struct ibmvfc_process_login prli;
	struct ibmvfc_query_tgt query_tgt;
	struct ibmvfc_implicit_logout implicit_logout;
	struct ibmvfc_tmf tmf;
	struct ibmvfc_cmd cmd;
	struct ibmvfc_passthru_mad passthru;
}__attribute__((packed, aligned (8)));

enum ibmvfc_target_action {
	IBMVFC_TGT_ACTION_NONE = 0,
	IBMVFC_TGT_ACTION_INIT,
	IBMVFC_TGT_ACTION_INIT_WAIT,
	IBMVFC_TGT_ACTION_DEL_RPORT,
};

struct ibmvfc_target {
	struct list_head queue;
	struct ibmvfc_host *vhost;
	u64 scsi_id;
	u64 new_scsi_id;
	struct fc_rport *rport;
	int target_id;
	enum ibmvfc_target_action action;
	int need_login;
	int add_rport;
	int init_retries;
	int logo_rcvd;
	u32 cancel_key;
	struct ibmvfc_service_parms service_parms;
	struct ibmvfc_service_parms service_parms_change;
	struct fc_rport_identifiers ids;
	void (*job_step) (struct ibmvfc_target *);
	struct timer_list timer;
	struct kref kref;
};

/* a unit of work for the hosting partition */
struct ibmvfc_event {
	struct list_head queue;
	struct ibmvfc_host *vhost;
	struct ibmvfc_target *tgt;
	struct scsi_cmnd *cmnd;
	atomic_t free;
	union ibmvfc_iu *xfer_iu;
	void (*done) (struct ibmvfc_event *);
	struct ibmvfc_crq crq;
	union ibmvfc_iu iu;
	union ibmvfc_iu *sync_iu;
	struct srp_direct_buf *ext_list;
	dma_addr_t ext_list_token;
	struct completion comp;
	struct completion *eh_comp;
	struct timer_list timer;
};

/* a pool of event structs for use */
struct ibmvfc_event_pool {
	struct ibmvfc_event *events;
	u32 size;
	union ibmvfc_iu *iu_storage;
	dma_addr_t iu_token;
};

enum ibmvfc_host_action {
	IBMVFC_HOST_ACTION_NONE = 0,
	IBMVFC_HOST_ACTION_LOGO,
	IBMVFC_HOST_ACTION_LOGO_WAIT,
	IBMVFC_HOST_ACTION_INIT,
	IBMVFC_HOST_ACTION_INIT_WAIT,
	IBMVFC_HOST_ACTION_QUERY,
	IBMVFC_HOST_ACTION_QUERY_TGTS,
	IBMVFC_HOST_ACTION_TGT_DEL,
	IBMVFC_HOST_ACTION_ALLOC_TGTS,
	IBMVFC_HOST_ACTION_TGT_INIT,
	IBMVFC_HOST_ACTION_TGT_DEL_FAILED,
};

enum ibmvfc_host_state {
	IBMVFC_NO_CRQ = 0,
	IBMVFC_INITIALIZING,
	IBMVFC_ACTIVE,
	IBMVFC_HALTED,
	IBMVFC_LINK_DOWN,
	IBMVFC_LINK_DEAD,
	IBMVFC_HOST_OFFLINE,
};

struct ibmvfc_host {
	char name[8];
	struct list_head queue;
	struct Scsi_Host *host;
	enum ibmvfc_host_state state;
	enum ibmvfc_host_action action;
#define IBMVFC_NUM_TRACE_INDEX_BITS		8
#define IBMVFC_NUM_TRACE_ENTRIES		(1 << IBMVFC_NUM_TRACE_INDEX_BITS)
#define IBMVFC_TRACE_SIZE	(sizeof(struct ibmvfc_trace_entry) * IBMVFC_NUM_TRACE_ENTRIES)
	struct ibmvfc_trace_entry *trace;
	u32 trace_index:IBMVFC_NUM_TRACE_INDEX_BITS;
	int num_targets;
	struct list_head targets;
	struct list_head sent;
	struct list_head free;
	struct device *dev;
	struct ibmvfc_event_pool pool;
	struct dma_pool *sg_pool;
	mempool_t *tgt_pool;
	struct ibmvfc_crq_queue crq;
	struct ibmvfc_async_crq_queue async_crq;
	struct ibmvfc_npiv_login login_info;
	union ibmvfc_npiv_login_data *login_buf;
	dma_addr_t login_buf_dma;
	int disc_buf_sz;
	int log_level;
	struct ibmvfc_discover_targets_buf *disc_buf;
	int task_set;
	int init_retries;
	int discovery_threads;
	int abort_threads;
	int client_migrated;
	int reinit;
	int delay_init;
	int scan_complete;
	int logged_in;
	int events_to_log;
#define IBMVFC_AE_LINKUP	0x0001
#define IBMVFC_AE_LINKDOWN	0x0002
#define IBMVFC_AE_RSCN		0x0004
	dma_addr_t disc_buf_dma;
	unsigned int partition_number;
	char partition_name[97];
	void (*job_step) (struct ibmvfc_host *);
	struct task_struct *work_thread;
	struct tasklet_struct tasklet;
	struct work_struct rport_add_work_q;
	wait_queue_head_t init_wait_q;
	wait_queue_head_t work_wait_q;
};

#define DBG_CMD(CMD) do { if (ibmvfc_debug) CMD; } while (0)

#define tgt_dbg(t, fmt, ...)			\
	DBG_CMD(dev_info((t)->vhost->dev, "%llX: " fmt, (t)->scsi_id, ##__VA_ARGS__))

#define tgt_info(t, fmt, ...)		\
	dev_info((t)->vhost->dev, "%llX: " fmt, (t)->scsi_id, ##__VA_ARGS__)

#define tgt_err(t, fmt, ...)		\
	dev_err((t)->vhost->dev, "%llX: " fmt, (t)->scsi_id, ##__VA_ARGS__)

#define tgt_log(t, level, fmt, ...) \
	do { \
		if ((t)->vhost->log_level >= level) \
			tgt_err(t, fmt, ##__VA_ARGS__); \
	} while (0)

#define ibmvfc_dbg(vhost, ...) \
	DBG_CMD(dev_info((vhost)->dev, ##__VA_ARGS__))

#define ibmvfc_log(vhost, level, ...) \
	do { \
		if ((vhost)->log_level >= level) \
			dev_err((vhost)->dev, ##__VA_ARGS__); \
	} while (0)

#define ENTER DBG_CMD(printk(KERN_INFO IBMVFC_NAME": Entering %s\n", __func__))
#define LEAVE DBG_CMD(printk(KERN_INFO IBMVFC_NAME": Leaving %s\n", __func__))

#ifdef CONFIG_SCSI_IBMVFC_TRACE
#define ibmvfc_create_trace_file(kobj, attr) sysfs_create_bin_file(kobj, attr)
#define ibmvfc_remove_trace_file(kobj, attr) sysfs_remove_bin_file(kobj, attr)
#else
#define ibmvfc_create_trace_file(kobj, attr) 0
#define ibmvfc_remove_trace_file(kobj, attr) do { } while (0)
#endif

#endif
