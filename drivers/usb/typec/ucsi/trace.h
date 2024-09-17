/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM ucsi

#if !defined(__UCSI_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __UCSI_TRACE_H

#include <linux/tracepoint.h>
#include <linux/usb/typec_altmode.h>

const char *ucsi_cmd_str(u64 raw_cmd);
const char *ucsi_cci_str(u32 cci);
const char *ucsi_recipient_str(u8 recipient);

DECLARE_EVENT_CLASS(ucsi_log_command,
	TP_PROTO(u64 command, int ret),
	TP_ARGS(command, ret),
	TP_STRUCT__entry(
		__field(u64, ctrl)
		__field(int, ret)
	),
	TP_fast_assign(
		__entry->ctrl = command;
		__entry->ret = ret;
	),
	TP_printk("%s -> %s (err=%d)", ucsi_cmd_str(__entry->ctrl),
		__entry->ret < 0 ? "FAIL" : "OK",
		__entry->ret < 0 ? __entry->ret : 0)
);

DEFINE_EVENT(ucsi_log_command, ucsi_run_command,
	TP_PROTO(u64 command, int ret),
	TP_ARGS(command, ret)
);

DEFINE_EVENT(ucsi_log_command, ucsi_reset_ppm,
	TP_PROTO(u64 command, int ret),
	TP_ARGS(command, ret)
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
		__entry->opmode = UCSI_CONSTAT_PWR_OPMODE(status->flags);
		__entry->connected = !!(status->flags & UCSI_CONSTAT_CONNECTED);
		__entry->pwr_dir = !!(status->flags & UCSI_CONSTAT_PWR_DIR);
		__entry->partner_flags = UCSI_CONSTAT_PARTNER_FLAGS(status->flags);
		__entry->partner_type = UCSI_CONSTAT_PARTNER_TYPE(status->flags);
		__entry->request_data_obj = status->request_data_obj;
		__entry->bc_status = UCSI_CONSTAT_BC_STATUS(status->pwr_status);
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

DECLARE_EVENT_CLASS(ucsi_log_register_altmode,
	TP_PROTO(u8 recipient, struct typec_altmode *alt),
	TP_ARGS(recipient, alt),
	TP_STRUCT__entry(
		__field(u8, recipient)
		__field(u16, svid)
		__field(u8, mode)
		__field(u32, vdo)
	),
	TP_fast_assign(
		__entry->recipient = recipient;
		__entry->svid = alt->svid;
		__entry->mode = alt->mode;
		__entry->vdo = alt->vdo;
	),
	TP_printk("%s alt mode: svid %04x, mode %d vdo %x",
		  ucsi_recipient_str(__entry->recipient), __entry->svid,
		  __entry->mode, __entry->vdo)
);

DEFINE_EVENT(ucsi_log_register_altmode, ucsi_register_altmode,
	TP_PROTO(u8 recipient, struct typec_altmode *alt),
	TP_ARGS(recipient, alt)
);

#endif /* __UCSI_TRACE_H */

/* This part must be outside protection */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
