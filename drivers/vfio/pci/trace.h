/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * VFIO PCI mmap/mmap_fault tracepoints
 *
 * Copyright (C) 2018 IBM Corp.  All rights reserved.
 *     Author: Alexey Kardashevskiy <aik@ozlabs.ru>
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM vfio_pci

#if !defined(_TRACE_VFIO_PCI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_VFIO_PCI_H

#include <linux/tracepoint.h>

TRACE_EVENT(vfio_pci_nvgpu_mmap_fault,
	TP_PROTO(struct pci_dev *pdev, unsigned long hpa, unsigned long ua,
			vm_fault_t ret),
	TP_ARGS(pdev, hpa, ua, ret),

	TP_STRUCT__entry(
		__field(const char *, name)
		__field(unsigned long, hpa)
		__field(unsigned long, ua)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->name = dev_name(&pdev->dev),
		__entry->hpa = hpa;
		__entry->ua = ua;
		__entry->ret = ret;
	),

	TP_printk("%s: %lx -> %lx ret=%d", __entry->name, __entry->hpa,
			__entry->ua, __entry->ret)
);

TRACE_EVENT(vfio_pci_nvgpu_mmap,
	TP_PROTO(struct pci_dev *pdev, unsigned long hpa, unsigned long ua,
			unsigned long size, int ret),
	TP_ARGS(pdev, hpa, ua, size, ret),

	TP_STRUCT__entry(
		__field(const char *, name)
		__field(unsigned long, hpa)
		__field(unsigned long, ua)
		__field(unsigned long, size)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->name = dev_name(&pdev->dev),
		__entry->hpa = hpa;
		__entry->ua = ua;
		__entry->size = size;
		__entry->ret = ret;
	),

	TP_printk("%s: %lx -> %lx size=%lx ret=%d", __entry->name, __entry->hpa,
			__entry->ua, __entry->size, __entry->ret)
);

TRACE_EVENT(vfio_pci_npu2_mmap,
	TP_PROTO(struct pci_dev *pdev, unsigned long hpa, unsigned long ua,
			unsigned long size, int ret),
	TP_ARGS(pdev, hpa, ua, size, ret),

	TP_STRUCT__entry(
		__field(const char *, name)
		__field(unsigned long, hpa)
		__field(unsigned long, ua)
		__field(unsigned long, size)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->name = dev_name(&pdev->dev),
		__entry->hpa = hpa;
		__entry->ua = ua;
		__entry->size = size;
		__entry->ret = ret;
	),

	TP_printk("%s: %lx -> %lx size=%lx ret=%d", __entry->name, __entry->hpa,
			__entry->ua, __entry->size, __entry->ret)
);

#endif /* _TRACE_VFIO_PCI_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/vfio/pci
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
