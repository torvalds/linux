/**************************************************************************
 *
 * Copyright (c) 2017 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Authors: Tom St Denis <tom.stdenis@amd.com>
 */
#if !defined(_TTM_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TTM_TRACE_H_

#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

#include <drm/drmP.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM ttm
#define TRACE_INCLUDE_FILE ttm_trace

TRACE_EVENT(ttm_dma_map,
	    TP_PROTO(struct device *dev, struct page *page, dma_addr_t dma_address),
	    TP_ARGS(dev, page, dma_address),
	    TP_STRUCT__entry(
				__string(device, dev_name(dev))
				__field(dma_addr_t, dma)
				__field(phys_addr_t, phys)
			    ),
	    TP_fast_assign(
			   __assign_str(device, dev_name(dev));
			   __entry->dma = dma_address;
			   __entry->phys = page_to_phys(page);
			   ),
	    TP_printk("%s: %pad => %pa",
		      __get_str(device),
		      &__entry->dma,
		      &__entry->phys)
);

TRACE_EVENT(ttm_dma_unmap,
	    TP_PROTO(struct device *dev, struct page *page, dma_addr_t dma_address),
	    TP_ARGS(dev, page, dma_address),
	    TP_STRUCT__entry(
				__string(device, dev_name(dev))
				__field(dma_addr_t, dma)
				__field(phys_addr_t, phys)
			    ),
	    TP_fast_assign(
			   __assign_str(device, dev_name(dev));
			   __entry->dma = dma_address;
			   __entry->phys = page_to_phys(page);
			   ),
	    TP_printk("%s: %pad => %pa",
		      __get_str(device),
		      &__entry->dma,
		      &__entry->phys)
);

#endif

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#include <trace/define_trace.h>

