#if !defined(_TRACE_KVM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KVM_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace

/*
 * Tracepoint for guest mode entry.
 */
TRACE_EVENT(kvm_ppc_instr,
	TP_PROTO(unsigned int inst, unsigned long _pc, unsigned int emulate),
	TP_ARGS(inst, _pc, emulate),

	TP_STRUCT__entry(
		__field(	unsigned int,	inst		)
		__field(	unsigned long,	pc		)
		__field(	unsigned int,	emulate		)
	),

	TP_fast_assign(
		__entry->inst		= inst;
		__entry->pc		= _pc;
		__entry->emulate	= emulate;
	),

	TP_printk("inst %u pc 0x%lx emulate %u\n",
		  __entry->inst, __entry->pc, __entry->emulate)
);

TRACE_EVENT(kvm_stlb_inval,
	TP_PROTO(unsigned int stlb_index),
	TP_ARGS(stlb_index),

	TP_STRUCT__entry(
		__field(	unsigned int,	stlb_index	)
	),

	TP_fast_assign(
		__entry->stlb_index	= stlb_index;
	),

	TP_printk("stlb_index %u", __entry->stlb_index)
);

TRACE_EVENT(kvm_stlb_write,
	TP_PROTO(unsigned int victim, unsigned int tid, unsigned int word0,
		 unsigned int word1, unsigned int word2),
	TP_ARGS(victim, tid, word0, word1, word2),

	TP_STRUCT__entry(
		__field(	unsigned int,	victim		)
		__field(	unsigned int,	tid		)
		__field(	unsigned int,	word0		)
		__field(	unsigned int,	word1		)
		__field(	unsigned int,	word2		)
	),

	TP_fast_assign(
		__entry->victim		= victim;
		__entry->tid		= tid;
		__entry->word0		= word0;
		__entry->word1		= word1;
		__entry->word2		= word2;
	),

	TP_printk("victim %u tid %u w0 %u w1 %u w2 %u",
		__entry->victim, __entry->tid, __entry->word0,
		__entry->word1, __entry->word2)
);

TRACE_EVENT(kvm_gtlb_write,
	TP_PROTO(unsigned int gtlb_index, unsigned int tid, unsigned int word0,
		 unsigned int word1, unsigned int word2),
	TP_ARGS(gtlb_index, tid, word0, word1, word2),

	TP_STRUCT__entry(
		__field(	unsigned int,	gtlb_index	)
		__field(	unsigned int,	tid		)
		__field(	unsigned int,	word0		)
		__field(	unsigned int,	word1		)
		__field(	unsigned int,	word2		)
	),

	TP_fast_assign(
		__entry->gtlb_index	= gtlb_index;
		__entry->tid		= tid;
		__entry->word0		= word0;
		__entry->word1		= word1;
		__entry->word2		= word2;
	),

	TP_printk("gtlb_index %u tid %u w0 %u w1 %u w2 %u",
		__entry->gtlb_index, __entry->tid, __entry->word0,
		__entry->word1, __entry->word2)
);

#endif /* _TRACE_KVM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
