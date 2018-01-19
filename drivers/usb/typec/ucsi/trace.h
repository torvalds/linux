/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM ucsi

#if !defined(__UCSI_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __UCSI_TRACE_H

#include <linux/tracepoint.h>
#include "ucsi.h"
#include "debug.h"

DECLARE_EVENT_CLASS(ucsi_log_ack,
	TP_PROTO(u8 ack),
	TP_ARGS(ack),
	TP_STRUCT__entry(
		__field(u8, ack)
	),
	TP_fast_assign(
		__entry->ack = ack;
	),
	TP_printk("ACK %s", ucsi_ack_str(__entry->ack))
);

DEFINE_EVENT(ucsi_log_ack, ucsi_ack,
	TP_PROTO(u8 ack),
	TP_ARGS(ack)
);

DECLARE_EVENT_CLASS(ucsi_log_control,
	TP_PROTO(struct ucsi_control *ctrl),
	TP_ARGS(ctrl),
	TP_STRUCT__entry(
		__field(u64, ctrl)
	),
	TP_fast_assign(
		__entry->ctrl = ctrl->raw_cmd;
	),
	TP_printk("control=%08llx (%s)", __entry->ctrl,
		ucsi_cmd_str(__entry->ctrl))
);

DEFINE_EVENT(ucsi_log_control, ucsi_command,
	TP_PROTO(struct ucsi_control *ctrl),
	TP_ARGS(ctrl)
);

DECLARE_EVENT_CLASS(ucsi_log_command,
	TP_PROTO(struct ucsi_control *ctrl, int ret),
	TP_ARGS(ctrl, ret),
	TP_STRUCT__entry(
		__field(u64, ctrl)
		__field(int, ret)
	),
	TP_fast_assign(
		__entry->ctrl = ctrl->raw_cmd;
		__entry->ret = ret;
	),
	TP_printk("%s -> %s (err=%d)", ucsi_cmd_str(__entry->ctrl),
		__entry->ret < 0 ? "FAIL" : "OK",
		__entry->ret < 0 ? __entry->ret : 0)
);

DEFINE_EVENT(ucsi_log_command, ucsi_run_command,
	TP_PROTO(struct ucsi_control *ctrl, int ret),
	TP_ARGS(ctrl, ret)
);

DEFINE_EVENT(ucsi_log_command, ucsi_reset_ppm,
	TP_PROTO(struct ucsi_control *ctrl, int ret),
	TP_ARGS(ctrl, ret)
);

DECLARE_EVENT_CLASS(ucsi_log_cci,
	TP_PROTO(u32 cci),
	TP_ARGS(cci),
	TP_STRUCT__entry(
		__field(u32, cci)
	),
	TP_fast_assign(
		__entry->cci = cci;
	),
	TP_printk("CCI=%08x %s", __entry->cci, ucsi_cci_str(__entry->cci))
);

DEFINE_EVENT(ucsi_log_cci, ucsi_notify,
	TP_PROTO(u32 cci),
	TP_ARGS(cci)
);

DECLARE_EVENT_CLASS(ucsi_log_connector_status,
	TP_PROTO(int port, struct ucsi_connector_status *status),
	TP_ARGS(port, status),
	TP_STRUCT__entry(
		__field(int, port)
		__field(u16, change)
		__field(u8, opmode)
		__field(u8, connected)
		__field(u8, pwr_dir)
		__field(u8, partner_flags)
		__field(u8, partner_type)
		__field(u32, request_data_obj)
		__field(u8, bc_status)
	),
	TP_fast_assign(
		__entry->port = port - 1;
		__entry->change = status->change;
		__entry->opmode = status->pwr_op_mode;
		__entry->connected = status->connected;
		__entry->pwr_dir = status->pwr_dir;
		__entry->partner_flags = status->partner_flags;
		__entry->partner_type = status->partner_type;
		__entry->request_data_obj = status->request_data_obj;
		__entry->bc_status = status->bc_status;
	),
	TP_printk("port%d status: change=%04x, opmode=%x, connected=%d, "
		"sourcing=%d, partner_flags=%x, partner_type=%x, "
		"request_data_obj=%08x, BC status=%x", __entry->port,
		__entry->change, __entry->opmode, __entry->connected,
		__entry->pwr_dir, __entry->partner_flags, __entry->partner_type,
		__entry->request_data_obj, __entry->bc_status)
);

DEFINE_EVENT(ucsi_log_connector_status, ucsi_connector_change,
	TP_PROTO(int port, struct ucsi_connector_status *status),
	TP_ARGS(port, status)
);

DEFINE_EVENT(ucsi_log_connector_status, ucsi_register_port,
	TP_PROTO(int port, struct ucsi_connector_status *status),
	TP_ARGS(port, status)
);

#endif /* __UCSI_TRACE_H */

/* This part must be outside protection */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
