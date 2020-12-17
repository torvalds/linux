/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Google, Inc.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM binder

#if !defined(_BINDER_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _BINDER_TRACE_H

#include <linux/tracepoint.h>

struct binder_buffer;
struct binder_node;
struct binder_proc;
struct binder_alloc;
struct binder_ref_data;
struct binder_thread;
struct binder_transaction;

TRACE_EVENT(binder_ioctl,
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

DECLARE_EVENT_CLASS(binder_lock_class,
	TP_PROTO(const char *tag),
	TP_ARGS(tag),
	TP_STRUCT__entry(
		__field(const char *, tag)
	),
	TP_fast_assign(
		__entry->tag = tag;
	),
	TP_printk("tag=%s", __entry->tag)
);

#define DEFINE_BINDER_LOCK_EVENT(name)	\
DEFINE_EVENT(binder_lock_class, name,	\
	TP_PROTO(const char *func), \
	TP_ARGS(func))

DEFINE_BINDER_LOCK_EVENT(binder_lock);
DEFINE_BINDER_LOCK_EVENT(binder_locked);
DEFINE_BINDER_LOCK_EVENT(binder_unlock);

DECLARE_EVENT_CLASS(binder_function_return_class,
	TP_PROTO(int ret),
	TP_ARGS(ret),
	TP_STRUCT__entry(
		__field(int, ret)
	),
	TP_fast_assign(
		__entry->ret = ret;
	),
	TP_printk("ret=%d", __entry->ret)
);

#define DEFINE_BINDER_FUNCTION_RETURN_EVENT(name)	\
DEFINE_EVENT(binder_function_return_class, name,	\
	TP_PROTO(int ret), \
	TP_ARGS(ret))

DEFINE_BINDER_FUNCTION_RETURN_EVENT(binder_ioctl_done);
DEFINE_BINDER_FUNCTION_RETURN_EVENT(binder_write_done);
DEFINE_BINDER_FUNCTION_RETURN_EVENT(binder_read_done);

TRACE_EVENT(binder_set_priority,
	TP_PROTO(int proc, int thread, unsigned int old_prio,
		 unsigned int desired_prio, unsigned int new_prio),
	TP_ARGS(proc, thread, old_prio, new_prio, desired_prio),

	TP_STRUCT__entry(
		__field(int, proc)
		__field(int, thread)
		__field(unsigned int, old_prio)
		__field(unsigned int, new_prio)
		__field(unsigned int, desired_prio)
	),
	TP_fast_assign(
		__entry->proc = proc;
		__entry->thread = thread;
		__entry->old_prio = old_prio;
		__entry->new_prio = new_prio;
		__entry->desired_prio = desired_prio;
	),
	TP_printk("proc=%d thread=%d old=%d => new=%d desired=%d",
		  __entry->proc, __entry->thread, __entry->old_prio,
		  __entry->new_prio, __entry->desired_prio)
);

TRACE_EVENT(binder_wait_for_work,
	TP_PROTO(bool proc_work, bool transaction_stack, bool thread_todo),
	TP_ARGS(proc_work, transaction_stack, thread_todo),

	TP_STRUCT__entry(
		__field(bool, proc_work)
		__field(bool, transaction_stack)
		__field(bool, thread_todo)
	),
	TP_fast_assign(
		__entry->proc_work = proc_work;
		__entry->transaction_stack = transaction_stack;
		__entry->thread_todo = thread_todo;
	),
	TP_printk("proc_work=%d transaction_stack=%d thread_todo=%d",
		  __entry->proc_work, __entry->transaction_stack,
		  __entry->thread_todo)
);

TRACE_EVENT(binder_txn_latency_free,
	TP_PROTO(struct binder_transaction *t,
		 int from_proc, int from_thread,
		 int to_proc, int to_thread),
	TP_ARGS(t, from_proc, from_thread, to_proc, to_thread),
	TP_STRUCT__entry(
		__field(int, debug_id)
		__field(int, from_proc)
		__field(int, from_thread)
		__field(int, to_proc)
		__field(int, to_thread)
		__field(unsigned int, code)
		__field(unsigned int, flags)
	),
	TP_fast_assign(
		__entry->debug_id = t->debug_id;
		__entry->from_proc = from_proc;
		__entry->from_thread = from_thread;
		__entry->to_proc = to_proc;
		__entry->to_thread = to_thread;
		__entry->code = t->code;
		__entry->flags = t->flags;
	),
	TP_printk("transaction=%d from %d:%d to %d:%d flags=0x%x code=0x%x",
		  __entry->debug_id, __entry->from_proc, __entry->from_thread,
		  __entry->to_proc, __entry->to_thread, __entry->code,
		  __entry->flags)
);

TRACE_EVENT(binder_transaction,
	TP_PROTO(bool reply, struct binder_transaction *t,
		 struct binder_node *target_node),
	TP_ARGS(reply, t, target_node),
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
		__entry->debug_id = t->debug_id;
		__entry->target_node = target_node ? target_node->debug_id : 0;
		__entry->to_proc = t->to_proc->pid;
		__entry->to_thread = t->to_thread ? t->to_thread->pid : 0;
		__entry->reply = reply;
		__entry->code = t->code;
		__entry->flags = t->flags;
	),
	TP_printk("transaction=%d dest_node=%d dest_proc=%d dest_thread=%d reply=%d flags=0x%x code=0x%x",
		  __entry->debug_id, __entry->target_node,
		  __entry->to_proc, __entry->to_thread,
		  __entry->reply, __entry->flags, __entry->code)
);

TRACE_EVENT(binder_transaction_received,
	TP_PROTO(struct binder_transaction *t),
	TP_ARGS(t),

	TP_STRUCT__entry(
		__field(int, debug_id)
	),
	TP_fast_assign(
		__entry->debug_id = t->debug_id;
	),
	TP_printk("transaction=%d", __entry->debug_id)
);

TRACE_EVENT(binder_transaction_node_to_ref,
	TP_PROTO(struct binder_transaction *t, struct binder_node *node,
		 struct binder_ref_data *rdata),
	TP_ARGS(t, node, rdata),

	TP_STRUCT__entry(
		__field(int, debug_id)
		__field(int, node_debug_id)
		__field(binder_uintptr_t, node_ptr)
		__field(int, ref_debug_id)
		__field(uint32_t, ref_desc)
	),
	TP_fast_assign(
		__entry->debug_id = t->debug_id;
		__entry->node_debug_id = node->debug_id;
		__entry->node_ptr = node->ptr;
		__entry->ref_debug_id = rdata->debug_id;
		__entry->ref_desc = rdata->desc;
	),
	TP_printk("transaction=%d node=%d src_ptr=0x%016llx ==> dest_ref=%d dest_desc=%d",
		  __entry->debug_id, __entry->node_debug_id,
		  (u64)__entry->node_ptr,
		  __entry->ref_debug_id, __entry->ref_desc)
);

TRACE_EVENT(binder_transaction_ref_to_node,
	TP_PROTO(struct binder_transaction *t, struct binder_node *node,
		 struct binder_ref_data *rdata),
	TP_ARGS(t, node, rdata),

	TP_STRUCT__entry(
		__field(int, debug_id)
		__field(int, ref_debug_id)
		__field(uint32_t, ref_desc)
		__field(int, node_debug_id)
		__field(binder_uintptr_t, node_ptr)
	),
	TP_fast_assign(
		__entry->debug_id = t->debug_id;
		__entry->ref_debug_id = rdata->debug_id;
		__entry->ref_desc = rdata->desc;
		__entry->node_debug_id = node->debug_id;
		__entry->node_ptr = node->ptr;
	),
	TP_printk("transaction=%d node=%d src_ref=%d src_desc=%d ==> dest_ptr=0x%016llx",
		  __entry->debug_id, __entry->node_debug_id,
		  __entry->ref_debug_id, __entry->ref_desc,
		  (u64)__entry->node_ptr)
);

TRACE_EVENT(binder_transaction_ref_to_ref,
	TP_PROTO(struct binder_transaction *t, struct binder_node *node,
		 struct binder_ref_data *src_ref,
		 struct binder_ref_data *dest_ref),
	TP_ARGS(t, node, src_ref, dest_ref),

	TP_STRUCT__entry(
		__field(int, debug_id)
		__field(int, node_debug_id)
		__field(int, src_ref_debug_id)
		__field(uint32_t, src_ref_desc)
		__field(int, dest_ref_debug_id)
		__field(uint32_t, dest_ref_desc)
	),
	TP_fast_assign(
		__entry->debug_id = t->debug_id;
		__entry->node_debug_id = node->debug_id;
		__entry->src_ref_debug_id = src_ref->debug_id;
		__entry->src_ref_desc = src_ref->desc;
		__entry->dest_ref_debug_id = dest_ref->debug_id;
		__entry->dest_ref_desc = dest_ref->desc;
	),
	TP_printk("transaction=%d node=%d src_ref=%d src_desc=%d ==> dest_ref=%d dest_desc=%d",
		  __entry->debug_id, __entry->node_debug_id,
		  __entry->src_ref_debug_id, __entry->src_ref_desc,
		  __entry->dest_ref_debug_id, __entry->dest_ref_desc)
);

TRACE_EVENT(binder_transaction_fd_send,
	TP_PROTO(struct binder_transaction *t, int fd, size_t offset),
	TP_ARGS(t, fd, offset),

	TP_STRUCT__entry(
		__field(int, debug_id)
		__field(int, fd)
		__field(size_t, offset)
	),
	TP_fast_assign(
		__entry->debug_id = t->debug_id;
		__entry->fd = fd;
		__entry->offset = offset;
	),
	TP_printk("transaction=%d src_fd=%d offset=%zu",
		  __entry->debug_id, __entry->fd, __entry->offset)
);

TRACE_EVENT(binder_transaction_fd_recv,
	TP_PROTO(struct binder_transaction *t, int fd, size_t offset),
	TP_ARGS(t, fd, offset),

	TP_STRUCT__entry(
		__field(int, debug_id)
		__field(int, fd)
		__field(size_t, offset)
	),
	TP_fast_assign(
		__entry->debug_id = t->debug_id;
		__entry->fd = fd;
		__entry->offset = offset;
	),
	TP_printk("transaction=%d dest_fd=%d offset=%zu",
		  __entry->debug_id, __entry->fd, __entry->offset)
);

DECLARE_EVENT_CLASS(binder_buffer_class,
	TP_PROTO(struct binder_buffer *buf),
	TP_ARGS(buf),
	TP_STRUCT__entry(
		__field(int, debug_id)
		__field(size_t, data_size)
		__field(size_t, offsets_size)
		__field(size_t, extra_buffers_size)
	),
	TP_fast_assign(
		__entry->debug_id = buf->debug_id;
		__entry->data_size = buf->data_size;
		__entry->offsets_size = buf->offsets_size;
		__entry->extra_buffers_size = buf->extra_buffers_size;
	),
	TP_printk("transaction=%d data_size=%zd offsets_size=%zd extra_buffers_size=%zd",
		  __entry->debug_id, __entry->data_size, __entry->offsets_size,
		  __entry->extra_buffers_size)
);

DEFINE_EVENT(binder_buffer_class, binder_transaction_alloc_buf,
	TP_PROTO(struct binder_buffer *buffer),
	TP_ARGS(buffer));

DEFINE_EVENT(binder_buffer_class, binder_transaction_buffer_release,
	TP_PROTO(struct binder_buffer *buffer),
	TP_ARGS(buffer));

DEFINE_EVENT(binder_buffer_class, binder_transaction_failed_buffer_release,
	TP_PROTO(struct binder_buffer *buffer),
	TP_ARGS(buffer));

TRACE_EVENT(binder_update_page_range,
	TP_PROTO(struct binder_alloc *alloc, bool allocate,
		 void __user *start, void __user *end),
	TP_ARGS(alloc, allocate, start, end),
	TP_STRUCT__entry(
		__field(int, proc)
		__field(bool, allocate)
		__field(size_t, offset)
		__field(size_t, size)
	),
	TP_fast_assign(
		__entry->proc = alloc->pid;
		__entry->allocate = allocate;
		__entry->offset = start - alloc->buffer;
		__entry->size = end - start;
	),
	TP_printk("proc=%d allocate=%d offset=%zu size=%zu",
		  __entry->proc, __entry->allocate,
		  __entry->offset, __entry->size)
);

DECLARE_EVENT_CLASS(binder_lru_page_class,
	TP_PROTO(const struct binder_alloc *alloc, size_t page_index),
	TP_ARGS(alloc, page_index),
	TP_STRUCT__entry(
		__field(int, proc)
		__field(size_t, page_index)
	),
	TP_fast_assign(
		__entry->proc = alloc->pid;
		__entry->page_index = page_index;
	),
	TP_printk("proc=%d page_index=%zu",
		  __entry->proc, __entry->page_index)
);

DEFINE_EVENT(binder_lru_page_class, binder_alloc_lru_start,
	TP_PROTO(const struct binder_alloc *alloc, size_t page_index),
	TP_ARGS(alloc, page_index));

DEFINE_EVENT(binder_lru_page_class, binder_alloc_lru_end,
	TP_PROTO(const struct binder_alloc *alloc, size_t page_index),
	TP_ARGS(alloc, page_index));

DEFINE_EVENT(binder_lru_page_class, binder_free_lru_start,
	TP_PROTO(const struct binder_alloc *alloc, size_t page_index),
	TP_ARGS(alloc, page_index));

DEFINE_EVENT(binder_lru_page_class, binder_free_lru_end,
	TP_PROTO(const struct binder_alloc *alloc, size_t page_index),
	TP_ARGS(alloc, page_index));

DEFINE_EVENT(binder_lru_page_class, binder_alloc_page_start,
	TP_PROTO(const struct binder_alloc *alloc, size_t page_index),
	TP_ARGS(alloc, page_index));

DEFINE_EVENT(binder_lru_page_class, binder_alloc_page_end,
	TP_PROTO(const struct binder_alloc *alloc, size_t page_index),
	TP_ARGS(alloc, page_index));

DEFINE_EVENT(binder_lru_page_class, binder_unmap_user_start,
	TP_PROTO(const struct binder_alloc *alloc, size_t page_index),
	TP_ARGS(alloc, page_index));

DEFINE_EVENT(binder_lru_page_class, binder_unmap_user_end,
	TP_PROTO(const struct binder_alloc *alloc, size_t page_index),
	TP_ARGS(alloc, page_index));

DEFINE_EVENT(binder_lru_page_class, binder_unmap_kernel_start,
	TP_PROTO(const struct binder_alloc *alloc, size_t page_index),
	TP_ARGS(alloc, page_index));

DEFINE_EVENT(binder_lru_page_class, binder_unmap_kernel_end,
	TP_PROTO(const struct binder_alloc *alloc, size_t page_index),
	TP_ARGS(alloc, page_index));

TRACE_EVENT(binder_command,
	TP_PROTO(uint32_t cmd),
	TP_ARGS(cmd),
	TP_STRUCT__entry(
		__field(uint32_t, cmd)
	),
	TP_fast_assign(
		__entry->cmd = cmd;
	),
	TP_printk("cmd=0x%x %s",
		  __entry->cmd,
		  _IOC_NR(__entry->cmd) < ARRAY_SIZE(binder_command_strings) ?
			  binder_command_strings[_IOC_NR(__entry->cmd)] :
			  "unknown")
);

TRACE_EVENT(binder_return,
	TP_PROTO(uint32_t cmd),
	TP_ARGS(cmd),
	TP_STRUCT__entry(
		__field(uint32_t, cmd)
	),
	TP_fast_assign(
		__entry->cmd = cmd;
	),
	TP_printk("cmd=0x%x %s",
		  __entry->cmd,
		  _IOC_NR(__entry->cmd) < ARRAY_SIZE(binder_return_strings) ?
			  binder_return_strings[_IOC_NR(__entry->cmd)] :
			  "unknown")
);

#endif /* _BINDER_TRACE_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE binder_trace
#include <trace/define_trace.h>
