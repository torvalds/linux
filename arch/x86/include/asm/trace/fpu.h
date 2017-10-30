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
		__field(bool, initialized)
		__field(u64, xfeatures)
		__field(u64, xcomp_bv)
		),

	TP_fast_assign(
		__entry->fpu		= fpu;
		__entry->initialized	= fpu->initialized;
		if (boot_cpu_has(X86_FEATURE_OSXSAVE)) {
			__entry->xfeatures = fpu->state.xsave.header.xfeatures;
			__entry->xcomp_bv  = fpu->state.xsave.header.xcomp_bv;
		}
	),
	TP_printk("x86/fpu: %p initialized: %d xfeatures: %llx xcomp_bv: %llx",
			__entry->fpu,
			__entry->initialized,
			__entry->xfeatures,
			__entry->xcomp_bv
	)
);

DEFINE_EVENT(x86_fpu, x86_fpu_state,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_before_save,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_after_save,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_before_restore,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_after_restore,
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

DEFINE_EVENT(x86_fpu, x86_fpu_activate_state,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_deactivate_state,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_init_state,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_dropped,
	TP_PROTO(struct fpu *fpu),
	TP_ARGS(fpu)
);

DEFINE_EVENT(x86_fpu, x86_fpu_copy_src,
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
