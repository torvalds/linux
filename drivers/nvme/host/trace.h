/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NVM Express device driver tracepoints
 * Copyright (c) 2018 Johannes Thumshirn, SUSE Linux GmbH
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM nvme

#if !defined(_TRACE_NVME_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NVME_H

#include <linux/nvme.h>
#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

#include "nvme.h"

#define nvme_admin_opcode_name(opcode)	{ opcode, #opcode }
#define show_admin_opcode_name(val)					\
	__print_symbolic(val,						\
		nvme_admin_opcode_name(nvme_admin_delete_sq),		\
		nvme_admin_opcode_name(nvme_admin_create_sq),		\
		nvme_admin_opcode_name(nvme_admin_get_log_page),	\
		nvme_admin_opcode_name(nvme_admin_delete_cq),		\
		nvme_admin_opcode_name(nvme_admin_create_cq),		\
		nvme_admin_opcode_name(nvme_admin_identify),		\
		nvme_admin_opcode_name(nvme_admin_abort_cmd),		\
		nvme_admin_opcode_name(nvme_admin_set_features),	\
		nvme_admin_opcode_name(nvme_admin_get_features),	\
		nvme_admin_opcode_name(nvme_admin_async_event),		\
		nvme_admin_opcode_name(nvme_admin_ns_mgmt),		\
		nvme_admin_opcode_name(nvme_admin_activate_fw),		\
		nvme_admin_opcode_name(nvme_admin_download_fw),		\
		nvme_admin_opcode_name(nvme_admin_ns_attach),		\
		nvme_admin_opcode_name(nvme_admin_keep_alive),		\
		nvme_admin_opcode_name(nvme_admin_directive_send),	\
		nvme_admin_opcode_name(nvme_admin_directive_recv),	\
		nvme_admin_opcode_name(nvme_admin_dbbuf),		\
		nvme_admin_opcode_name(nvme_admin_format_nvm),		\
		nvme_admin_opcode_name(nvme_admin_security_send),	\
		nvme_admin_opcode_name(nvme_admin_security_recv),	\
		nvme_admin_opcode_name(nvme_admin_sanitize_nvm))

#define nvme_opcode_name(opcode)	{ opcode, #opcode }
#define show_nvm_opcode_name(val)				\
	__print_symbolic(val,					\
		nvme_opcode_name(nvme_cmd_flush),		\
		nvme_opcode_name(nvme_cmd_write),		\
		nvme_opcode_name(nvme_cmd_read),		\
		nvme_opcode_name(nvme_cmd_write_uncor),		\
		nvme_opcode_name(nvme_cmd_compare),		\
		nvme_opcode_name(nvme_cmd_write_zeroes),	\
		nvme_opcode_name(nvme_cmd_dsm),			\
		nvme_opcode_name(nvme_cmd_resv_register),	\
		nvme_opcode_name(nvme_cmd_resv_report),		\
		nvme_opcode_name(nvme_cmd_resv_acquire),	\
		nvme_opcode_name(nvme_cmd_resv_release))

#define show_opcode_name(qid, opcode)					\
	(qid ? show_nvm_opcode_name(opcode) : show_admin_opcode_name(opcode))

const char *nvme_trace_parse_admin_cmd(struct trace_seq *p, u8 opcode,
		u8 *cdw10);
const char *nvme_trace_parse_nvm_cmd(struct trace_seq *p, u8 opcode,
		u8 *cdw10);

#define parse_nvme_cmd(qid, opcode, cdw10) 			\
	(qid ?							\
	 nvme_trace_parse_nvm_cmd(p, opcode, cdw10) : 		\
	 nvme_trace_parse_admin_cmd(p, opcode, cdw10))

const char *nvme_trace_disk_name(struct trace_seq *p, char *name);
#define __print_disk_name(name)				\
	nvme_trace_disk_name(p, name)

#ifndef TRACE_HEADER_MULTI_READ
static inline void __assign_disk_name(char *name, struct gendisk *disk)
{
	if (disk)
		memcpy(name, disk->disk_name, DISK_NAME_LEN);
	else
		memset(name, 0, DISK_NAME_LEN);
}
#endif

TRACE_EVENT(nvme_setup_cmd,
	    TP_PROTO(struct request *req, struct nvme_command *cmd),
	    TP_ARGS(req, cmd),
	    TP_STRUCT__entry(
		__array(char, disk, DISK_NAME_LEN)
		__field(int, ctrl_id)
		__field(int, qid)
		__field(u8, opcode)
		__field(u8, flags)
		__field(u16, cid)
		__field(u32, nsid)
		__field(u64, metadata)
		__array(u8, cdw10, 24)
	    ),
	    TP_fast_assign(
		__entry->ctrl_id = nvme_req(req)->ctrl->instance;
		__entry->qid = nvme_req_qid(req);
		__entry->opcode = cmd->common.opcode;
		__entry->flags = cmd->common.flags;
		__entry->cid = cmd->common.command_id;
		__entry->nsid = le32_to_cpu(cmd->common.nsid);
		__entry->metadata = le64_to_cpu(cmd->common.metadata);
		__assign_disk_name(__entry->disk, req->rq_disk);
		memcpy(__entry->cdw10, &cmd->common.cdw10,
			6 * sizeof(__entry->cdw10));
	    ),
	    TP_printk("nvme%d: %sqid=%d, cmdid=%u, nsid=%u, flags=0x%x, meta=0x%llx, cmd=(%s %s)",
		      __entry->ctrl_id, __print_disk_name(__entry->disk),
		      __entry->qid, __entry->cid, __entry->nsid,
		      __entry->flags, __entry->metadata,
		      show_opcode_name(__entry->qid, __entry->opcode),
		      parse_nvme_cmd(__entry->qid, __entry->opcode, __entry->cdw10))
);

TRACE_EVENT(nvme_complete_rq,
	    TP_PROTO(struct request *req),
	    TP_ARGS(req),
	    TP_STRUCT__entry(
		__array(char, disk, DISK_NAME_LEN)
		__field(int, ctrl_id)
		__field(int, qid)
		__field(int, cid)
		__field(u64, result)
		__field(u8, retries)
		__field(u8, flags)
		__field(u16, status)
	    ),
	    TP_fast_assign(
		__entry->ctrl_id = nvme_req(req)->ctrl->instance;
		__entry->qid = nvme_req_qid(req);
		__entry->cid = req->tag;
		__entry->result = le64_to_cpu(nvme_req(req)->result.u64);
		__entry->retries = nvme_req(req)->retries;
		__entry->flags = nvme_req(req)->flags;
		__entry->status = nvme_req(req)->status;
		__assign_disk_name(__entry->disk, req->rq_disk);
	    ),
	    TP_printk("nvme%d: %sqid=%d, cmdid=%u, res=%llu, retries=%u, flags=0x%x, status=%u",
		      __entry->ctrl_id, __print_disk_name(__entry->disk),
		      __entry->qid, __entry->cid, __entry->result,
		      __entry->retries, __entry->flags, __entry->status)

);

#define aer_name(aer) { aer, #aer }

TRACE_EVENT(nvme_async_event,
	TP_PROTO(struct nvme_ctrl *ctrl, u32 result),
	TP_ARGS(ctrl, result),
	TP_STRUCT__entry(
		__field(int, ctrl_id)
		__field(u32, result)
	),
	TP_fast_assign(
		__entry->ctrl_id = ctrl->instance;
		__entry->result = result;
	),
	TP_printk("nvme%d: NVME_AEN=%#08x [%s]",
		__entry->ctrl_id, __entry->result,
		__print_symbolic(__entry->result,
		aer_name(NVME_AER_NOTICE_NS_CHANGED),
		aer_name(NVME_AER_NOTICE_ANA),
		aer_name(NVME_AER_NOTICE_FW_ACT_STARTING),
		aer_name(NVME_AER_ERROR),
		aer_name(NVME_AER_SMART),
		aer_name(NVME_AER_CSS),
		aer_name(NVME_AER_VS))
	)
);

#undef aer_name

TRACE_EVENT(nvme_sq,
	TP_PROTO(struct request *req, __le16 sq_head, int sq_tail),
	TP_ARGS(req, sq_head, sq_tail),
	TP_STRUCT__entry(
		__field(int, ctrl_id)
		__array(char, disk, DISK_NAME_LEN)
		__field(int, qid)
		__field(u16, sq_head)
		__field(u16, sq_tail)
	),
	TP_fast_assign(
		__entry->ctrl_id = nvme_req(req)->ctrl->instance;
		__assign_disk_name(__entry->disk, req->rq_disk);
		__entry->qid = nvme_req_qid(req);
		__entry->sq_head = le16_to_cpu(sq_head);
		__entry->sq_tail = sq_tail;
	),
	TP_printk("nvme%d: %sqid=%d, head=%u, tail=%u",
		__entry->ctrl_id, __print_disk_name(__entry->disk),
		__entry->qid, __entry->sq_head, __entry->sq_tail
	)
);

#endif /* _TRACE_NVME_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
