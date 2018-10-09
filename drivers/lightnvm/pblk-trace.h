/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM pblk

#if !defined(_TRACE_PBLK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PBLK_H

#include <linux/tracepoint.h>

struct ppa_addr;

#define show_chunk_flags(state) __print_flags(state, "",	\
	{ NVM_CHK_ST_FREE,		"FREE",		},	\
	{ NVM_CHK_ST_CLOSED,		"CLOSED",	},	\
	{ NVM_CHK_ST_OPEN,		"OPEN",		},	\
	{ NVM_CHK_ST_OFFLINE,		"OFFLINE",	})

TRACE_EVENT(pblk_chunk_state,

	TP_PROTO(const char *name, struct ppa_addr *ppa, int state),

	TP_ARGS(name, ppa, state),

	TP_STRUCT__entry(
		__string(name, name)
		__field(u64, ppa)
		__field(int, state);
	),

	TP_fast_assign(
		__assign_str(name, name);
		__entry->ppa = ppa->ppa;
		__entry->state = state;
	),

	TP_printk("dev=%s grp=%llu pu=%llu chk=%llu state=%s", __get_str(name),
			(u64)(((struct ppa_addr *)(&__entry->ppa))->m.grp),
			(u64)(((struct ppa_addr *)(&__entry->ppa))->m.pu),
			(u64)(((struct ppa_addr *)(&__entry->ppa))->m.chk),
			show_chunk_flags((int)__entry->state))

);


#endif /* !defined(_TRACE_PBLK_H) || defined(TRACE_HEADER_MULTI_READ) */

/* This part must be outside protection */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../../drivers/lightnvm
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE pblk-trace
#include <trace/define_trace.h>
