/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google, Inc.
 */

#undef TRACE_SYSTEM
#undef TRACE_INCLUDE_FILE
#undef TRACE_INCLUDE_PATH
#define TRACE_SYSTEM rust_binder
#define TRACE_INCLUDE_FILE rust_binder_events
#define TRACE_INCLUDE_PATH ../drivers/android/binder

#if !defined(_RUST_BINDER_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _RUST_BINDER_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(rust_binder_ioctl,
	TP_PROTO(unsigned int cmd, unsigned long arg),
	TP_ARGS(cmd, arg),

	TP_STRUCT__entry(
		__field(unsigned int, cmd)
		__field(unsigned long, arg)
	),
	TP_fast_assign(
		__entry->cmd = cmd;
		__entry->arg = arg;
	),
	TP_printk("cmd=0x%x arg=0x%lx", __entry->cmd, __entry->arg)
);

TRACE_EVENT(rust_binder_transaction,
	TP_PROTO(bool reply, rust_binder_transaction t, struct task_struct *thread),
	TP_ARGS(reply, t, thread),
	TP_STRUCT__entry(
		__field(int, debug_id)
		__field(int, target_node)
		__field(int, to_proc)
		__field(int, to_thread)
		__field(int, reply)
		__field(unsigned int, code)
		__field(unsigned int, flags)
	),
	TP_fast_assign(
		rust_binder_process to = rust_binder_transaction_to_proc(t);
		rust_binder_node target_node = rust_binder_transaction_target_node(t);

		__entry->debug_id = rust_binder_transaction_debug_id(t);
		__entry->target_node = target_node ? rust_binder_node_debug_id(target_node) : 0;
		__entry->to_proc = rust_binder_process_task(to)->pid;
		__entry->to_thread = thread ? thread->pid : 0;
		__entry->reply = reply;
		__entry->code = rust_binder_transaction_code(t);
		__entry->flags = rust_binder_transaction_flags(t);
	),
	TP_printk("transaction=%d dest_node=%d dest_proc=%d dest_thread=%d reply=%d flags=0x%x code=0x%x",
		  __entry->debug_id, __entry->target_node,
		  __entry->to_proc, __entry->to_thread,
		  __entry->reply, __entry->flags, __entry->code)
);

#endif /* _RUST_BINDER_TRACE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
