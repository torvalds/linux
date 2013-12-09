#include <linux/version.h>

#undef TRACE_SYSTEM
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33))
#define TRACE_SYSTEM lock
#else
#define TRACE_SYSTEM lockdep
#define TRACE_INCLUDE_FILE lock
#if defined(_TRACE_LOCKDEP_H)
#define _TRACE_LOCK_H
#endif
#endif

#if !defined(_TRACE_LOCK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_LOCK_H

#include <linux/lockdep.h>
#include <linux/tracepoint.h>

#ifdef CONFIG_LOCKDEP

TRACE_EVENT(lock_acquire,

	TP_PROTO(struct lockdep_map *lock, unsigned int subclass,
		int trylock, int read, int check,
		struct lockdep_map *next_lock, unsigned long ip),

	TP_ARGS(lock, subclass, trylock, read, check, next_lock, ip),

	TP_STRUCT__entry(
		__field(unsigned int, flags)
		__string(name, lock->name)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,34))
		__field(void *, lockdep_addr)
#endif
	),

	TP_fast_assign(
		tp_assign(flags, (trylock ? 1 : 0) | (read ? 2 : 0))
		tp_strcpy(name, lock->name)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,34))
		tp_assign(lockdep_addr, lock)
#endif
	),

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,34))
	TP_printk("%p %s%s%s", __entry->lockdep_addr,
#else
	TP_printk("%s%s%s",
#endif
		  (__entry->flags & 1) ? "try " : "",
		  (__entry->flags & 2) ? "read " : "",
		  __get_str(name))
)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))

DECLARE_EVENT_CLASS(lock,

	TP_PROTO(struct lockdep_map *lock, unsigned long ip),

	TP_ARGS(lock, ip),

	TP_STRUCT__entry(
		__string(	name, 	lock->name	)
		__field(	void *, lockdep_addr	)
	),

	TP_fast_assign(
		tp_strcpy(name, lock->name)
		tp_assign(lockdep_addr, lock)
	),

	TP_printk("%p %s",  __entry->lockdep_addr, __get_str(name))
)

DEFINE_EVENT(lock, lock_release,

	TP_PROTO(struct lockdep_map *lock, unsigned long ip),

	TP_ARGS(lock, ip)
)

#ifdef CONFIG_LOCK_STAT

DEFINE_EVENT(lock, lock_contended,

	TP_PROTO(struct lockdep_map *lock, unsigned long ip),

	TP_ARGS(lock, ip)
)

DEFINE_EVENT(lock, lock_acquired,

	TP_PROTO(struct lockdep_map *lock, unsigned long ip),

	TP_ARGS(lock, ip)
)

#endif

#else /* #if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)) */

TRACE_EVENT(lock_release,

	TP_PROTO(struct lockdep_map *lock, int nested, unsigned long ip),

	TP_ARGS(lock, nested, ip),

	TP_STRUCT__entry(
		__string(	name, 	lock->name	)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,34))
		__field(	void *, lockdep_addr	)
#endif
	),

	TP_fast_assign(
		tp_strcpy(name, lock->name)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,34))
		tp_assign(lockdep_addr, lock)
#endif
	),

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,34))
	TP_printk("%p %s",  __entry->lockdep_addr, __get_str(name))
#else
	TP_printk("%s",  __get_str(name))
#endif
)

#ifdef CONFIG_LOCK_STAT

TRACE_EVENT(lock_contended,

	TP_PROTO(struct lockdep_map *lock, unsigned long ip),

	TP_ARGS(lock, ip),

	TP_STRUCT__entry(
		__string(	name, 	lock->name	)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,34))
		__field(	void *, lockdep_addr	)
#endif
	),

	TP_fast_assign(
		tp_strcpy(name, lock->name)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,34))
		tp_assign(lockdep_addr, lock)
#endif
	),

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,34))
	TP_printk("%p %s",  __entry->lockdep_addr, __get_str(name))
#else
	TP_printk("%s",  __get_str(name))
#endif
)

TRACE_EVENT(lock_acquired,

	TP_PROTO(struct lockdep_map *lock, unsigned long ip, s64 waittime),

	TP_ARGS(lock, ip, waittime),

	TP_STRUCT__entry(
		__string(	name, 	lock->name	)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,34))
		__field(	s64,    wait_nsec	)
		__field(	void *, lockdep_addr	)
#else
		__field(unsigned long, wait_usec)
		__field(unsigned long, wait_nsec_rem)
#endif
	),

	TP_fast_assign(
		tp_strcpy(name, lock->name)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,34))
		tp_assign(wait_nsec, waittime)
		tp_assign(lockdep_addr, lock)
#else
		tp_assign(wait_usec, (unsigned long)waittime)
		tp_assign(wait_nsec_rem, do_div(waittime, NSEC_PER_USEC))
#endif
	),

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,34))
	TP_printk("%p %s (%llu ns)",  __entry->lockdep_addr,
		  __get_str(name), __entry->wait_nsec)
#else
	TP_printk("%s (%lu.%03lu us)",
		  __get_str(name),
		  __entry->wait_usec, __entry->wait_nsec_rem)
#endif
)

#endif

#endif /* #if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)) */

#endif

#endif /* _TRACE_LOCK_H */

/* This part must be outside protection */
#include "../../../probes/define_trace.h"
