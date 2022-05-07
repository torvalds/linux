/* SPDX-License-Identifier: GPL-2.0+ */
// Copyright 2017 IBM Corp.
#undef TRACE_SYSTEM
#define TRACE_SYSTEM ocxl

#if !defined(_TRACE_OCXL_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_OCXL_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(ocxl_context,
	TP_PROTO(pid_t pid, void *spa, int pasid, u32 pidr, u32 tidr),
	TP_ARGS(pid, spa, pasid, pidr, tidr),

	TP_STRUCT__entry(
		__field(pid_t, pid)
		__field(void*, spa)
		__field(int, pasid)
		__field(u32, pidr)
		__field(u32, tidr)
	),

	TP_fast_assign(
		__entry->pid = pid;
		__entry->spa = spa;
		__entry->pasid = pasid;
		__entry->pidr = pidr;
		__entry->tidr = tidr;
	),

	TP_printk("linux pid=%d spa=0x%p pasid=0x%x pidr=0x%x tidr=0x%x",
		__entry->pid,
		__entry->spa,
		__entry->pasid,
		__entry->pidr,
		__entry->tidr
	)
);

DEFINE_EVENT(ocxl_context, ocxl_context_add,
	TP_PROTO(pid_t pid, void *spa, int pasid, u32 pidr, u32 tidr),
	TP_ARGS(pid, spa, pasid, pidr, tidr)
);

DEFINE_EVENT(ocxl_context, ocxl_context_remove,
	TP_PROTO(pid_t pid, void *spa, int pasid, u32 pidr, u32 tidr),
	TP_ARGS(pid, spa, pasid, pidr, tidr)
);

TRACE_EVENT(ocxl_terminate_pasid,
	TP_PROTO(int pasid, int rc),
	TP_ARGS(pasid, rc),

	TP_STRUCT__entry(
		__field(int, pasid)
		__field(int, rc)
	),

	TP_fast_assign(
		__entry->pasid = pasid;
		__entry->rc = rc;
	),

	TP_printk("pasid=0x%x rc=%d",
		__entry->pasid,
		__entry->rc
	)
);

DECLARE_EVENT_CLASS(ocxl_fault_handler,
	TP_PROTO(void *spa, u64 pe, u64 dsisr, u64 dar, u64 tfc),
	TP_ARGS(spa, pe, dsisr, dar, tfc),

	TP_STRUCT__entry(
		__field(void *, spa)
		__field(u64, pe)
		__field(u64, dsisr)
		__field(u64, dar)
		__field(u64, tfc)
	),

	TP_fast_assign(
		__entry->spa = spa;
		__entry->pe = pe;
		__entry->dsisr = dsisr;
		__entry->dar = dar;
		__entry->tfc = tfc;
	),

	TP_printk("spa=%p pe=0x%llx dsisr=0x%llx dar=0x%llx tfc=0x%llx",
		__entry->spa,
		__entry->pe,
		__entry->dsisr,
		__entry->dar,
		__entry->tfc
	)
);

DEFINE_EVENT(ocxl_fault_handler, ocxl_fault,
	TP_PROTO(void *spa, u64 pe, u64 dsisr, u64 dar, u64 tfc),
	TP_ARGS(spa, pe, dsisr, dar, tfc)
);

DEFINE_EVENT(ocxl_fault_handler, ocxl_fault_ack,
	TP_PROTO(void *spa, u64 pe, u64 dsisr, u64 dar, u64 tfc),
	TP_ARGS(spa, pe, dsisr, dar, tfc)
);

TRACE_EVENT(ocxl_afu_irq_alloc,
	TP_PROTO(int pasid, int irq_id, unsigned int virq, int hw_irq),
	TP_ARGS(pasid, irq_id, virq, hw_irq),

	TP_STRUCT__entry(
		__field(int, pasid)
		__field(int, irq_id)
		__field(unsigned int, virq)
		__field(int, hw_irq)
	),

	TP_fast_assign(
		__entry->pasid = pasid;
		__entry->irq_id = irq_id;
		__entry->virq = virq;
		__entry->hw_irq = hw_irq;
	),

	TP_printk("pasid=0x%x irq_id=%d virq=%u hw_irq=%d",
		__entry->pasid,
		__entry->irq_id,
		__entry->virq,
		__entry->hw_irq
	)
);

TRACE_EVENT(ocxl_afu_irq_free,
	TP_PROTO(int pasid, int irq_id),
	TP_ARGS(pasid, irq_id),

	TP_STRUCT__entry(
		__field(int, pasid)
		__field(int, irq_id)
	),

	TP_fast_assign(
		__entry->pasid = pasid;
		__entry->irq_id = irq_id;
	),

	TP_printk("pasid=0x%x irq_id=%d",
		__entry->pasid,
		__entry->irq_id
	)
);

TRACE_EVENT(ocxl_afu_irq_receive,
	TP_PROTO(int virq),
	TP_ARGS(virq),

	TP_STRUCT__entry(
		__field(int, virq)
	),

	TP_fast_assign(
		__entry->virq = virq;
	),

	TP_printk("virq=%d",
		__entry->virq
	)
);

#endif /* _TRACE_OCXL_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
