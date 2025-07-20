/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM x86_fpu

#if !defined(_TRACE_FPU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_FPU_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(x86_fpu,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu),

	TP_STRUCT__entry(
		__field(struct fpu *, fpu)
		__field(bool, load_fpu)
		__field(u64, xfeatures)
		__field(u64, xcomp_bv)
		),

	TP_fast_assign(
		__entry->fpu		= fpu;
		__entry->load_fpu	= test_thread_flag(TIF_NEED_FPU_LOAD);
		if (boot_cpu_has(X86_FEATURE_OSXSAVE)) {
			__entry->xfeatures = fpu->fpstate->regs.xsave.header.xfeatures;
			__entry->xcomp_bv  = fpu->fpstate->regs.xsave.header.xcomp_bv;
		}
	),
	TP_printk("x86/fpu: %p load: %d xfeatures: %llx xcomp_bv: %llx",
			__entry->fpu,
			__entry->load_fpu,
			__entry->xfeatures,
			__entry->xcomp_bv
	)
);

DEFINE_EVENT(x86_fpu, x86_fpu_before_save,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_after_save,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_regs_activated,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_regs_deactivated,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_dropped,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_copy_dst,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_xstate_check_failed,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH asm/trace/
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE fpu
#endif /* _TRACE_FPU_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
