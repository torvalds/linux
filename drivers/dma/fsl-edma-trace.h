/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2023 NXP.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM fsl_edma

#if !defined(__LINUX_FSL_EDMA_TRACE) || defined(TRACE_HEADER_MULTI_READ)
#define __LINUX_FSL_EDMA_TRACE

#include <linux/types.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(edma_log_io,
	TP_PROTO(struct fsl_edma_engine *edma, void __iomem *addr, u32 value),
	TP_ARGS(edma, addr, value),
	TP_STRUCT__entry(
		__field(struct fsl_edma_engine *, edma)
		__field(void __iomem *, addr)
		__field(u32, value)
	),
	TP_fast_assign(
		__entry->edma = edma;
		__entry->addr = addr;
		__entry->value = value;
	),
	TP_printk("offset %08x: value %08x",
		(u32)(__entry->addr - __entry->edma->membase), __entry->value)
);

DEFINE_EVENT(edma_log_io, edma_readl,
	TP_PROTO(struct fsl_edma_engine *edma, void __iomem *addr, u32 value),
	TP_ARGS(edma, addr, value)
);

DEFINE_EVENT(edma_log_io, edma_writel,
	TP_PROTO(struct fsl_edma_engine *edma, void __iomem *addr,  u32 value),
	TP_ARGS(edma, addr, value)
);

DEFINE_EVENT(edma_log_io, edma_readw,
	TP_PROTO(struct fsl_edma_engine *edma, void __iomem *addr, u32 value),
	TP_ARGS(edma, addr, value)
);

DEFINE_EVENT(edma_log_io, edma_writew,
	TP_PROTO(struct fsl_edma_engine *edma, void __iomem *addr,  u32 value),
	TP_ARGS(edma, addr, value)
);

DEFINE_EVENT(edma_log_io, edma_readb,
	TP_PROTO(struct fsl_edma_engine *edma, void __iomem *addr, u32 value),
	TP_ARGS(edma, addr, value)
);

DEFINE_EVENT(edma_log_io, edma_writeb,
	TP_PROTO(struct fsl_edma_engine *edma, void __iomem *addr,  u32 value),
	TP_ARGS(edma, addr, value)
);

DECLARE_EVENT_CLASS(edma_log_tcd,
	TP_PROTO(struct fsl_edma_chan *chan, void *tcd),
	TP_ARGS(chan, tcd),
	TP_STRUCT__entry(
		__field(u64, saddr)
		__field(u16, soff)
		__field(u16, attr)
		__field(u32, nbytes)
		__field(u64, slast)
		__field(u64, daddr)
		__field(u16, doff)
		__field(u16, citer)
		__field(u64, dlast_sga)
		__field(u16, csr)
		__field(u16, biter)

	),
	TP_fast_assign(
		__entry->saddr = fsl_edma_get_tcd_to_cpu(chan, tcd, saddr),
		__entry->soff = fsl_edma_get_tcd_to_cpu(chan, tcd, soff),
		__entry->attr = fsl_edma_get_tcd_to_cpu(chan, tcd, attr),
		__entry->nbytes = fsl_edma_get_tcd_to_cpu(chan, tcd, nbytes),
		__entry->slast = fsl_edma_get_tcd_to_cpu(chan, tcd, slast),
		__entry->daddr = fsl_edma_get_tcd_to_cpu(chan, tcd, daddr),
		__entry->doff = fsl_edma_get_tcd_to_cpu(chan, tcd, doff),
		__entry->citer = fsl_edma_get_tcd_to_cpu(chan, tcd, citer),
		__entry->dlast_sga = fsl_edma_get_tcd_to_cpu(chan, tcd, dlast_sga),
		__entry->csr = fsl_edma_get_tcd_to_cpu(chan, tcd, csr),
		__entry->biter = fsl_edma_get_tcd_to_cpu(chan, tcd, biter);
	),
	TP_printk("\n==== TCD =====\n"
		  "  saddr:  0x%016llx\n"
		  "  soff:               0x%04x\n"
		  "  attr:               0x%04x\n"
		  "  nbytes:         0x%08x\n"
		  "  slast:  0x%016llx\n"
		  "  daddr:  0x%016llx\n"
		  "  doff:               0x%04x\n"
		  "  citer:              0x%04x\n"
		  "  dlast:  0x%016llx\n"
		  "  csr:                0x%04x\n"
		  "  biter:              0x%04x\n",
		__entry->saddr,
		__entry->soff,
		__entry->attr,
		__entry->nbytes,
		__entry->slast,
		__entry->daddr,
		__entry->doff,
		__entry->citer,
		__entry->dlast_sga,
		__entry->csr,
		__entry->biter)
);

DEFINE_EVENT(edma_log_tcd, edma_fill_tcd,
	TP_PROTO(struct fsl_edma_chan *chan, void *tcd),
	TP_ARGS(chan, tcd)
);

#endif

/* this part must be outside header guard */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE fsl-edma-trace

#include <trace/define_trace.h>
