/*
 * Because linux/module.h has tracepoints in the header, and ftrace.h
 * eventually includes this file, define_trace.h includes linux/module.h
 * But we do not want the module.h to override the TRACE_SYSTEM macro
 * variable that define_trace.h is processing, so we only set it
 * when module events are being processed, which would happen when
 * CREATE_TRACE_POINTS is defined.
 */
#ifdef CREATE_TRACE_POINTS
#undef TRACE_SYSTEM
#define TRACE_SYSTEM module
#endif

#if !defined(_TRACE_MODULE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MODULE_H

#include <linux/tracepoint.h>
#include <linux/version.h>

#ifdef CONFIG_MODULES

#ifndef _TRACE_MODULE_DEF
#define _TRACE_MODULE_DEF
struct module;

#define show_module_flags(flags) __print_flags(flags, "",	\
	{ (1UL << TAINT_PROPRIETARY_MODULE),	"P" },		\
	{ (1UL << TAINT_FORCED_MODULE),		"F" },		\
	{ (1UL << TAINT_CRAP),			"C" })
#endif

TRACE_EVENT(module_load,

	TP_PROTO(struct module *mod),

	TP_ARGS(mod),

	TP_STRUCT__entry(
		__field(	unsigned int,	taints		)
		__string(	name,		mod->name	)
	),

	TP_fast_assign(
		tp_assign(taints, mod->taints)
		tp_strcpy(name, mod->name)
	),

	TP_printk("%s %s", __get_str(name), show_module_flags(__entry->taints))
)

TRACE_EVENT(module_free,

	TP_PROTO(struct module *mod),

	TP_ARGS(mod),

	TP_STRUCT__entry(
		__string(	name,		mod->name	)
	),

	TP_fast_assign(
		tp_strcpy(name, mod->name)
	),

	TP_printk("%s", __get_str(name))
)

#ifdef CONFIG_MODULE_UNLOAD
/* trace_module_get/put are only used if CONFIG_MODULE_UNLOAD is defined */

DECLARE_EVENT_CLASS(module_refcnt,

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
	TP_PROTO(struct module *mod, unsigned long ip),

	TP_ARGS(mod, ip),
#else
	TP_PROTO(struct module *mod, unsigned long ip, int refcnt),

	TP_ARGS(mod, ip, refcnt),
#endif

	TP_STRUCT__entry(
		__field(	unsigned long,	ip		)
		__field(	int,		refcnt		)
		__string(	name,		mod->name	)
	),

	TP_fast_assign(
		tp_assign(ip, ip)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
		tp_assign(refcnt, __this_cpu_read(mod->refptr->incs) + __this_cpu_read(mod->refptr->decs))
#else
		tp_assign(refcnt, refcnt)
#endif
		tp_strcpy(name, mod->name)
	),

	TP_printk("%s call_site=%pf refcnt=%d",
		  __get_str(name), (void *)__entry->ip, __entry->refcnt)
)

DEFINE_EVENT(module_refcnt, module_get,

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
	TP_PROTO(struct module *mod, unsigned long ip),

	TP_ARGS(mod, ip)
#else
	TP_PROTO(struct module *mod, unsigned long ip, int refcnt),

	TP_ARGS(mod, ip, refcnt)
#endif
)

DEFINE_EVENT(module_refcnt, module_put,

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
	TP_PROTO(struct module *mod, unsigned long ip),

	TP_ARGS(mod, ip)
#else
	TP_PROTO(struct module *mod, unsigned long ip, int refcnt),

	TP_ARGS(mod, ip, refcnt)
#endif
)
#endif /* CONFIG_MODULE_UNLOAD */

TRACE_EVENT(module_request,

	TP_PROTO(char *name, bool wait, unsigned long ip),

	TP_ARGS(name, wait, ip),

	TP_STRUCT__entry(
		__field(	unsigned long,	ip		)
		__field(	bool,		wait		)
		__string(	name,		name		)
	),

	TP_fast_assign(
		tp_assign(ip, ip)
		tp_assign(wait, wait)
		tp_strcpy(name, name)
	),

	TP_printk("%s wait=%d call_site=%pf",
		  __get_str(name), (int)__entry->wait, (void *)__entry->ip)
)

#endif /* CONFIG_MODULES */

#endif /* _TRACE_MODULE_H */

/* This part must be outside protection */
#include "../../../probes/define_trace.h"
