#undef TRACE_SYSTEM
#define TRACE_SYSTEM lttng_statedump

#if !defined(_TRACE_LTTNG_STATEDUMP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_LTTNG_STATEDUMP_H

#include <linux/tracepoint.h>
#include <linux/nsproxy.h>
#include <linux/pid_namespace.h>

TRACE_EVENT(lttng_statedump_start,
	TP_PROTO(struct lttng_session *session),
	TP_ARGS(session),
	TP_STRUCT__entry(
	),
	TP_fast_assign(
	),
	TP_printk("")
)

TRACE_EVENT(lttng_statedump_end,
	TP_PROTO(struct lttng_session *session),
	TP_ARGS(session),
	TP_STRUCT__entry(
	),
	TP_fast_assign(
	),
	TP_printk("")
)

TRACE_EVENT(lttng_statedump_process_state,
	TP_PROTO(struct lttng_session *session,
		struct task_struct *p,
		int type, int mode, int submode, int status,
		struct pid_namespace *pid_ns),
	TP_ARGS(session, p, type, mode, submode, status, pid_ns),
	TP_STRUCT__entry(
		__field(pid_t, tid)
		__field(pid_t, vtid)
		__field(pid_t, pid)
		__field(pid_t, vpid)
		__field(pid_t, ppid)
		__field(pid_t, vppid)
		__array_text(char, name, TASK_COMM_LEN)
		__field(int, type)
		__field(int, mode)
		__field(int, submode)
		__field(int, status)
		__field(int, ns_level)
	),
	TP_fast_assign(
		tp_assign(tid, p->pid)
		tp_assign(vtid, pid_ns ? task_pid_nr_ns(p, pid_ns) : 0)
		tp_assign(pid, p->tgid)
		tp_assign(vpid, pid_ns ? task_tgid_nr_ns(p, pid_ns) : 0)
		tp_assign(ppid,
			({
				pid_t ret;

				rcu_read_lock();
				ret = task_tgid_nr(p->real_parent);
				rcu_read_unlock();
				ret;
			}))
		tp_assign(vppid,
			({
				struct task_struct *parent;
				pid_t ret = 0;

				if (pid_ns) {
					rcu_read_lock();
					parent = rcu_dereference(p->real_parent);
					ret = task_tgid_nr_ns(parent, pid_ns);
					rcu_read_unlock();
				}
				ret;
			}))
		tp_memcpy(name, p->comm, TASK_COMM_LEN)
		tp_assign(type, type)
		tp_assign(mode, mode)
		tp_assign(submode, submode)
		tp_assign(status, status)
		tp_assign(ns_level, pid_ns ? pid_ns->level : 0)
	),
	TP_printk("")
)

TRACE_EVENT(lttng_statedump_file_descriptor,
	TP_PROTO(struct lttng_session *session,
		struct task_struct *p, int fd, const char *filename),
	TP_ARGS(session, p, fd, filename),
	TP_STRUCT__entry(
		__field(pid_t, pid)
		__field(int, fd)
		__string(filename, filename)
	),
	TP_fast_assign(
		tp_assign(pid, p->tgid)
		tp_assign(fd, fd)
		tp_strcpy(filename, filename)
	),
	TP_printk("")
)

TRACE_EVENT(lttng_statedump_vm_map,
	TP_PROTO(struct lttng_session *session,
		struct task_struct *p, struct vm_area_struct *map,
		unsigned long inode),
	TP_ARGS(session, p, map, inode),
	TP_STRUCT__entry(
		__field(pid_t, pid)
		__field_hex(unsigned long, start)
		__field_hex(unsigned long, end)
		__field_hex(unsigned long, flags)
		__field(unsigned long, inode)
		__field(unsigned long, pgoff)
	),
	TP_fast_assign(
		tp_assign(pid, p->tgid)
		tp_assign(start, map->vm_start)
		tp_assign(end, map->vm_end)
		tp_assign(flags, map->vm_flags)
		tp_assign(inode, inode)
		tp_assign(pgoff, map->vm_pgoff << PAGE_SHIFT)
	),
	TP_printk("")
)

TRACE_EVENT(lttng_statedump_network_interface,
	TP_PROTO(struct lttng_session *session,
		struct net_device *dev, struct in_ifaddr *ifa),
	TP_ARGS(session, dev, ifa),
	TP_STRUCT__entry(
		__string(name, dev->name)
		__field_network_hex(uint32_t, address_ipv4)
	),
	TP_fast_assign(
		tp_strcpy(name, dev->name)
		tp_assign(address_ipv4, ifa ? ifa->ifa_address : 0U)
	),
	TP_printk("")
)

/* Called with desc->lock held */
TRACE_EVENT(lttng_statedump_interrupt,
	TP_PROTO(struct lttng_session *session,
		unsigned int irq, const char *chip_name,
		struct irqaction *action),
	TP_ARGS(session, irq, chip_name, action),
	TP_STRUCT__entry(
		__field(unsigned int, irq)
		__string(name, chip_name)
		__string(action, action->name ? : "")
	),
	TP_fast_assign(
		tp_assign(irq, irq)
		tp_strcpy(name, chip_name)
		tp_strcpy(action, action->name ? : "")
	),
	TP_printk("")
)

#endif /*  _TRACE_LTTNG_STATEDUMP_H */

/* This part must be outside protection */
#include "../../../probes/define_trace.h"
