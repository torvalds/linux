/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NVM Express target device driver tracepoints
 * Copyright (c) 2018 Johannes Thumshirn, SUSE Linux GmbH
 *
 * This is entirely based on drivers/nvme/host/trace.h
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM nvmet

#if !defined(_TRACE_NVMET_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NVMET_H

#include <linux/nvme.h>
#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

#include "nvmet.h"

const char *nvmet_trace_parse_admin_cmd(struct trace_seq *p, u8 opcode,
		u8 *cdw10);
const char *nvmet_trace_parse_nvm_cmd(struct trace_seq *p, u8 opcode,
		u8 *cdw10);
const char *nvmet_trace_parse_fabrics_cmd(struct trace_seq *p, u8 fctype,
		u8 *spc);

#define parse_nvme_cmd(qid, opcode, fctype, cdw10)			\
	((opcode) == nvme_fabrics_command ?				\
	 nvmet_trace_parse_fabrics_cmd(p, fctype, cdw10) :		\
	(qid ?								\
	 nvmet_trace_parse_nvm_cmd(p, opcode, cdw10) :			\
	 nvmet_trace_parse_admin_cmd(p, opcode, cdw10)))

const char *nvmet_trace_ctrl_name(struct trace_seq *p, struct nvmet_ctrl *ctrl);
#define __print_ctrl_name(ctrl)				\
	nvmet_trace_ctrl_name(p, ctrl)

const char *nvmet_trace_disk_name(struct trace_seq *p, char *name);
#define __print_disk_name(name)				\
	nvmet_trace_disk_name(p, name)

#ifndef TRACE_HEADER_MULTI_READ
static inline struct nvmet_ctrl *nvmet_req_to_ctrl(struct nvmet_req *req)
{
	return req->sq->ctrl;
}

static inline void __assign_disk_name(char *name, struct nvmet_req *req,
		bool init)
{
	struct nvmet_ctrl *ctrl = nvmet_req_to_ctrl(req);
	struct nvmet_ns *ns;

	if ((init && req->sq->qid) || (!init && req->cq->qid)) {
		ns = nvmet_find_namespace(ctrl, req->cmd->rw.nsid);
		strncpy(name, ns->device_path, DISK_NAME_LEN);
		return;
	}

	memset(name, 0, DISK_NAME_LEN);
}
#endif

TRACE_EVENT(nvmet_req_init,
	TP_PROTO(struct nvmet_req *req, struct nvme_command *cmd),
	TP_ARGS(req, cmd),
	TP_STRUCT__entry(
		__field(struct nvme_command *, cmd)
		__field(struct nvmet_ctrl *, ctrl)
		__array(char, disk, DISK_NAME_LEN)
		__field(int, qid)
		__field(u16, cid)
		__field(u8, opcode)
		__field(u8, fctype)
		__field(u8, flags)
		__field(u32, nsid)
		__field(u64, metadata)
		__array(u8, cdw10, 24)
	),
	TP_fast_assign(
		__entry->cmd = cmd;
		__entry->ctrl = nvmet_req_to_ctrl(req);
		__assign_disk_name(__entry->disk, req, true);
		__entry->qid = req->sq->qid;
		__entry->cid = cmd->common.command_id;
		__entry->opcode = cmd->common.opcode;
		__entry->fctype = cmd->fabrics.fctype;
		__entry->flags = cmd->common.flags;
		__entry->nsid = le32_to_cpu(cmd->common.nsid);
		__entry->metadata = le64_to_cpu(cmd->common.metadata);
		memcpy(__entry->cdw10, &cmd->common.cdw10,
			sizeof(__entry->cdw10));
	),
	TP_printk("nvmet%s: %sqid=%d, cmdid=%u, nsid=%u, flags=%#x, "
		  "meta=%#llx, cmd=(%s, %s)",
		__print_ctrl_name(__entry->ctrl),
		__print_disk_name(__entry->disk),
		__entry->qid, __entry->cid, __entry->nsid,
		__entry->flags, __entry->metadata,
		show_opcode_name(__entry->qid, __entry->opcode,
				__entry->fctype),
		parse_nvme_cmd(__entry->qid, __entry->opcode,
				__entry->fctype, __entry->cdw10))
);

TRACE_EVENT(nvmet_req_complete,
	TP_PROTO(struct nvmet_req *req),
	TP_ARGS(req),
	TP_STRUCT__entry(
		__field(struct nvmet_ctrl *, ctrl)
		__array(char, disk, DISK_NAME_LEN)
		__field(int, qid)
		__field(int, cid)
		__field(u64, result)
		__field(u16, status)
	),
	TP_fast_assign(
		__entry->ctrl = nvmet_req_to_ctrl(req);
		__entry->qid = req->cq->qid;
		__entry->cid = req->cqe->command_id;
		__entry->result = le64_to_cpu(req->cqe->result.u64);
		__entry->status = le16_to_cpu(req->cqe->status) >> 1;
		__assign_disk_name(__entry->disk, req, false);
	),
	TP_printk("nvmet%s: %sqid=%d, cmdid=%u, res=%#llx, status=%#x",
		__print_ctrl_name(__entry->ctrl),
		__print_disk_name(__entry->disk),
		__entry->qid, __entry->cid, __entry->result, __entry->status)

);

#define aer_name(aer) { aer, #aer }

TRACE_EVENT(nvmet_async_event,
	TP_PROTO(struct nvmet_ctrl *ctrl, __le32 result),
	TP_ARGS(ctrl, result),
	TP_STRUCT__entry(
		__field(int, ctrl_id)
		__field(u32, result)
	),
	TP_fast_assign(
		__entry->ctrl_id = ctrl->cntlid;
		__entry->result = (le32_to_cpu(result) & 0xff00) >> 8;
	),
	TP_printk("nvmet%d: NVME_AEN=%#08x [%s]",
		__entry->ctrl_id, __entry->result,
		__print_symbolic(__entry->result,
		aer_name(NVME_AER_NOTICE_NS_CHANGED),
		aer_name(NVME_AER_NOTICE_ANA),
		aer_name(NVME_AER_NOTICE_FW_ACT_STARTING),
		aer_name(NVME_AER_NOTICE_DISC_CHANGED),
		aer_name(NVME_AER_ERROR),
		aer_name(NVME_AER_SMART),
		aer_name(NVME_AER_CSS),
		aer_name(NVME_AER_VS))
	)
);
#undef aer_name

#endif /* _TRACE_NVMET_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
