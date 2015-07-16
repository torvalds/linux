/*
 * Copyright 2015 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM cxl

#if !defined(_CXL_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _CXL_TRACE_H

#include <linux/tracepoint.h>

#include "cxl.h"

#define DSISR_FLAGS \
	{ CXL_PSL_DSISR_An_DS,	"DS" }, \
	{ CXL_PSL_DSISR_An_DM,	"DM" }, \
	{ CXL_PSL_DSISR_An_ST,	"ST" }, \
	{ CXL_PSL_DSISR_An_UR,	"UR" }, \
	{ CXL_PSL_DSISR_An_PE,	"PE" }, \
	{ CXL_PSL_DSISR_An_AE,	"AE" }, \
	{ CXL_PSL_DSISR_An_OC,	"OC" }, \
	{ CXL_PSL_DSISR_An_M,	"M" }, \
	{ CXL_PSL_DSISR_An_P,	"P" }, \
	{ CXL_PSL_DSISR_An_A,	"A" }, \
	{ CXL_PSL_DSISR_An_S,	"S" }, \
	{ CXL_PSL_DSISR_An_K,	"K" }

#define TFC_FLAGS \
	{ CXL_PSL_TFC_An_A,	"A" }, \
	{ CXL_PSL_TFC_An_C,	"C" }, \
	{ CXL_PSL_TFC_An_AE,	"AE" }, \
	{ CXL_PSL_TFC_An_R,	"R" }

#define LLCMD_NAMES \
	{ CXL_SPA_SW_CMD_TERMINATE,	"TERMINATE" }, \
	{ CXL_SPA_SW_CMD_REMOVE,	"REMOVE" }, \
	{ CXL_SPA_SW_CMD_SUSPEND,	"SUSPEND" }, \
	{ CXL_SPA_SW_CMD_RESUME,	"RESUME" }, \
	{ CXL_SPA_SW_CMD_ADD,		"ADD" }, \
	{ CXL_SPA_SW_CMD_UPDATE,	"UPDATE" }

#define AFU_COMMANDS \
	{ 0,			"DISABLE" }, \
	{ CXL_AFU_Cntl_An_E,	"ENABLE" }, \
	{ CXL_AFU_Cntl_An_RA,	"RESET" }

#define PSL_COMMANDS \
	{ CXL_PSL_SCNTL_An_Pc,	"PURGE" }, \
	{ CXL_PSL_SCNTL_An_Sc,	"SUSPEND" }


DECLARE_EVENT_CLASS(cxl_pe_class,
	TP_PROTO(struct cxl_context *ctx),

	TP_ARGS(ctx),

	TP_STRUCT__entry(
		__field(u8, card)
		__field(u8, afu)
		__field(u16, pe)
	),

	TP_fast_assign(
		__entry->card = ctx->afu->adapter->adapter_num;
		__entry->afu = ctx->afu->slice;
		__entry->pe = ctx->pe;
	),

	TP_printk("afu%i.%i pe=%i",
		__entry->card,
		__entry->afu,
		__entry->pe
	)
);


TRACE_EVENT(cxl_attach,
	TP_PROTO(struct cxl_context *ctx, u64 wed, s16 num_interrupts, u64 amr),

	TP_ARGS(ctx, wed, num_interrupts, amr),

	TP_STRUCT__entry(
		__field(u8, card)
		__field(u8, afu)
		__field(u16, pe)
		__field(pid_t, pid)
		__field(u64, wed)
		__field(u64, amr)
		__field(s16, num_interrupts)
	),

	TP_fast_assign(
		__entry->card = ctx->afu->adapter->adapter_num;
		__entry->afu = ctx->afu->slice;
		__entry->pe = ctx->pe;
		__entry->pid = pid_nr(ctx->pid);
		__entry->wed = wed;
		__entry->amr = amr;
		__entry->num_interrupts = num_interrupts;
	),

	TP_printk("afu%i.%i pid=%i pe=%i wed=0x%.16llx irqs=%i amr=0x%llx",
		__entry->card,
		__entry->afu,
		__entry->pid,
		__entry->pe,
		__entry->wed,
		__entry->num_interrupts,
		__entry->amr
	)
);

DEFINE_EVENT(cxl_pe_class, cxl_detach,
	TP_PROTO(struct cxl_context *ctx),
	TP_ARGS(ctx)
);

TRACE_EVENT(cxl_afu_irq,
	TP_PROTO(struct cxl_context *ctx, int afu_irq, int virq, irq_hw_number_t hwirq),

	TP_ARGS(ctx, afu_irq, virq, hwirq),

	TP_STRUCT__entry(
		__field(u8, card)
		__field(u8, afu)
		__field(u16, pe)
		__field(u16, afu_irq)
		__field(int, virq)
		__field(irq_hw_number_t, hwirq)
	),

	TP_fast_assign(
		__entry->card = ctx->afu->adapter->adapter_num;
		__entry->afu = ctx->afu->slice;
		__entry->pe = ctx->pe;
		__entry->afu_irq = afu_irq;
		__entry->virq = virq;
		__entry->hwirq = hwirq;
	),

	TP_printk("afu%i.%i pe=%i afu_irq=%i virq=%i hwirq=0x%lx",
		__entry->card,
		__entry->afu,
		__entry->pe,
		__entry->afu_irq,
		__entry->virq,
		__entry->hwirq
	)
);

TRACE_EVENT(cxl_psl_irq,
	TP_PROTO(struct cxl_context *ctx, int irq, u64 dsisr, u64 dar),

	TP_ARGS(ctx, irq, dsisr, dar),

	TP_STRUCT__entry(
		__field(u8, card)
		__field(u8, afu)
		__field(u16, pe)
		__field(int, irq)
		__field(u64, dsisr)
		__field(u64, dar)
	),

	TP_fast_assign(
		__entry->card = ctx->afu->adapter->adapter_num;
		__entry->afu = ctx->afu->slice;
		__entry->pe = ctx->pe;
		__entry->irq = irq;
		__entry->dsisr = dsisr;
		__entry->dar = dar;
	),

	TP_printk("afu%i.%i pe=%i irq=%i dsisr=%s dar=0x%.16llx",
		__entry->card,
		__entry->afu,
		__entry->pe,
		__entry->irq,
		__print_flags(__entry->dsisr, "|", DSISR_FLAGS),
		__entry->dar
	)
);

TRACE_EVENT(cxl_psl_irq_ack,
	TP_PROTO(struct cxl_context *ctx, u64 tfc),

	TP_ARGS(ctx, tfc),

	TP_STRUCT__entry(
		__field(u8, card)
		__field(u8, afu)
		__field(u16, pe)
		__field(u64, tfc)
	),

	TP_fast_assign(
		__entry->card = ctx->afu->adapter->adapter_num;
		__entry->afu = ctx->afu->slice;
		__entry->pe = ctx->pe;
		__entry->tfc = tfc;
	),

	TP_printk("afu%i.%i pe=%i tfc=%s",
		__entry->card,
		__entry->afu,
		__entry->pe,
		__print_flags(__entry->tfc, "|", TFC_FLAGS)
	)
);

TRACE_EVENT(cxl_ste_miss,
	TP_PROTO(struct cxl_context *ctx, u64 dar),

	TP_ARGS(ctx, dar),

	TP_STRUCT__entry(
		__field(u8, card)
		__field(u8, afu)
		__field(u16, pe)
		__field(u64, dar)
	),

	TP_fast_assign(
		__entry->card = ctx->afu->adapter->adapter_num;
		__entry->afu = ctx->afu->slice;
		__entry->pe = ctx->pe;
		__entry->dar = dar;
	),

	TP_printk("afu%i.%i pe=%i dar=0x%.16llx",
		__entry->card,
		__entry->afu,
		__entry->pe,
		__entry->dar
	)
);

TRACE_EVENT(cxl_ste_write,
	TP_PROTO(struct cxl_context *ctx, unsigned int idx, u64 e, u64 v),

	TP_ARGS(ctx, idx, e, v),

	TP_STRUCT__entry(
		__field(u8, card)
		__field(u8, afu)
		__field(u16, pe)
		__field(unsigned int, idx)
		__field(u64, e)
		__field(u64, v)
	),

	TP_fast_assign(
		__entry->card = ctx->afu->adapter->adapter_num;
		__entry->afu = ctx->afu->slice;
		__entry->pe = ctx->pe;
		__entry->idx = idx;
		__entry->e = e;
		__entry->v = v;
	),

	TP_printk("afu%i.%i pe=%i SSTE[%i] E=0x%.16llx V=0x%.16llx",
		__entry->card,
		__entry->afu,
		__entry->pe,
		__entry->idx,
		__entry->e,
		__entry->v
	)
);

TRACE_EVENT(cxl_pte_miss,
	TP_PROTO(struct cxl_context *ctx, u64 dsisr, u64 dar),

	TP_ARGS(ctx, dsisr, dar),

	TP_STRUCT__entry(
		__field(u8, card)
		__field(u8, afu)
		__field(u16, pe)
		__field(u64, dsisr)
		__field(u64, dar)
	),

	TP_fast_assign(
		__entry->card = ctx->afu->adapter->adapter_num;
		__entry->afu = ctx->afu->slice;
		__entry->pe = ctx->pe;
		__entry->dsisr = dsisr;
		__entry->dar = dar;
	),

	TP_printk("afu%i.%i pe=%i dsisr=%s dar=0x%.16llx",
		__entry->card,
		__entry->afu,
		__entry->pe,
		__print_flags(__entry->dsisr, "|", DSISR_FLAGS),
		__entry->dar
	)
);

TRACE_EVENT(cxl_llcmd,
	TP_PROTO(struct cxl_context *ctx, u64 cmd),

	TP_ARGS(ctx, cmd),

	TP_STRUCT__entry(
		__field(u8, card)
		__field(u8, afu)
		__field(u16, pe)
		__field(u64, cmd)
	),

	TP_fast_assign(
		__entry->card = ctx->afu->adapter->adapter_num;
		__entry->afu = ctx->afu->slice;
		__entry->pe = ctx->pe;
		__entry->cmd = cmd;
	),

	TP_printk("afu%i.%i pe=%i cmd=%s",
		__entry->card,
		__entry->afu,
		__entry->pe,
		__print_symbolic_u64(__entry->cmd, LLCMD_NAMES)
	)
);

TRACE_EVENT(cxl_llcmd_done,
	TP_PROTO(struct cxl_context *ctx, u64 cmd, int rc),

	TP_ARGS(ctx, cmd, rc),

	TP_STRUCT__entry(
		__field(u8, card)
		__field(u8, afu)
		__field(u16, pe)
		__field(u64, cmd)
		__field(int, rc)
	),

	TP_fast_assign(
		__entry->card = ctx->afu->adapter->adapter_num;
		__entry->afu = ctx->afu->slice;
		__entry->pe = ctx->pe;
		__entry->rc = rc;
		__entry->cmd = cmd;
	),

	TP_printk("afu%i.%i pe=%i cmd=%s rc=%i",
		__entry->card,
		__entry->afu,
		__entry->pe,
		__print_symbolic_u64(__entry->cmd, LLCMD_NAMES),
		__entry->rc
	)
);

DECLARE_EVENT_CLASS(cxl_afu_psl_ctrl,
	TP_PROTO(struct cxl_afu *afu, u64 cmd),

	TP_ARGS(afu, cmd),

	TP_STRUCT__entry(
		__field(u8, card)
		__field(u8, afu)
		__field(u64, cmd)
	),

	TP_fast_assign(
		__entry->card = afu->adapter->adapter_num;
		__entry->afu = afu->slice;
		__entry->cmd = cmd;
	),

	TP_printk("afu%i.%i cmd=%s",
		__entry->card,
		__entry->afu,
		__print_symbolic_u64(__entry->cmd, AFU_COMMANDS)
	)
);

DECLARE_EVENT_CLASS(cxl_afu_psl_ctrl_done,
	TP_PROTO(struct cxl_afu *afu, u64 cmd, int rc),

	TP_ARGS(afu, cmd, rc),

	TP_STRUCT__entry(
		__field(u8, card)
		__field(u8, afu)
		__field(u64, cmd)
		__field(int, rc)
	),

	TP_fast_assign(
		__entry->card = afu->adapter->adapter_num;
		__entry->afu = afu->slice;
		__entry->rc = rc;
		__entry->cmd = cmd;
	),

	TP_printk("afu%i.%i cmd=%s rc=%i",
		__entry->card,
		__entry->afu,
		__print_symbolic_u64(__entry->cmd, AFU_COMMANDS),
		__entry->rc
	)
);

DEFINE_EVENT(cxl_afu_psl_ctrl, cxl_afu_ctrl,
	TP_PROTO(struct cxl_afu *afu, u64 cmd),
	TP_ARGS(afu, cmd)
);

DEFINE_EVENT(cxl_afu_psl_ctrl_done, cxl_afu_ctrl_done,
	TP_PROTO(struct cxl_afu *afu, u64 cmd, int rc),
	TP_ARGS(afu, cmd, rc)
);

DEFINE_EVENT_PRINT(cxl_afu_psl_ctrl, cxl_psl_ctrl,
	TP_PROTO(struct cxl_afu *afu, u64 cmd),
	TP_ARGS(afu, cmd),

	TP_printk("psl%i.%i cmd=%s",
		__entry->card,
		__entry->afu,
		__print_symbolic_u64(__entry->cmd, PSL_COMMANDS)
	)
);

DEFINE_EVENT_PRINT(cxl_afu_psl_ctrl_done, cxl_psl_ctrl_done,
	TP_PROTO(struct cxl_afu *afu, u64 cmd, int rc),
	TP_ARGS(afu, cmd, rc),

	TP_printk("psl%i.%i cmd=%s rc=%i",
		__entry->card,
		__entry->afu,
		__print_symbolic_u64(__entry->cmd, PSL_COMMANDS),
		__entry->rc
	)
);

DEFINE_EVENT(cxl_pe_class, cxl_slbia,
	TP_PROTO(struct cxl_context *ctx),
	TP_ARGS(ctx)
);

#endif /* _CXL_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
