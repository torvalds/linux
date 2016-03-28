/*
 * Copyright © 2011-2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Jike Song <jike.song@intel.com>
 *
 * Contributors:
 *    Zhi Wang <zhi.a.wang@intel.com>
 *
 */

#if !defined(_GVT_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _GVT_TRACE_H_

#include <linux/types.h>
#include <linux/stringify.h>
#include <linux/tracepoint.h>
#include <asm/tsc.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM gvt

TRACE_EVENT(spt_alloc,
	TP_PROTO(int id, void *spt, int type, unsigned long mfn,
		unsigned long gpt_gfn),

	TP_ARGS(id, spt, type, mfn, gpt_gfn),

	TP_STRUCT__entry(
		__field(int, id)
		__field(void *, spt)
		__field(int, type)
		__field(unsigned long, mfn)
		__field(unsigned long, gpt_gfn)
		),

	TP_fast_assign(
		__entry->id = id;
		__entry->spt = spt;
		__entry->type = type;
		__entry->mfn = mfn;
		__entry->gpt_gfn = gpt_gfn;
	),

	TP_printk("VM%d [alloc] spt %p type %d mfn 0x%lx gfn 0x%lx\n",
		__entry->id,
		__entry->spt,
		__entry->type,
		__entry->mfn,
		__entry->gpt_gfn)
);

TRACE_EVENT(spt_free,
	TP_PROTO(int id, void *spt, int type),

	TP_ARGS(id, spt, type),

	TP_STRUCT__entry(
		__field(int, id)
		__field(void *, spt)
		__field(int, type)
		),

	TP_fast_assign(
		__entry->id = id;
		__entry->spt = spt;
		__entry->type = type;
	),

	TP_printk("VM%u [free] spt %p type %d\n",
		__entry->id,
		__entry->spt,
		__entry->type)
);

#define MAX_BUF_LEN 256

TRACE_EVENT(gma_index,
	TP_PROTO(const char *prefix, unsigned long gma,
		unsigned long index),

	TP_ARGS(prefix, gma, index),

	TP_STRUCT__entry(
		__array(char, buf, MAX_BUF_LEN)
	),

	TP_fast_assign(
		snprintf(__entry->buf, MAX_BUF_LEN,
			"%s gma 0x%lx index 0x%lx\n", prefix, gma, index);
	),

	TP_printk("%s", __entry->buf)
);

TRACE_EVENT(gma_translate,
	TP_PROTO(int id, char *type, int ring_id, int pt_level,
		unsigned long gma, unsigned long gpa),

	TP_ARGS(id, type, ring_id, pt_level, gma, gpa),

	TP_STRUCT__entry(
		__array(char, buf, MAX_BUF_LEN)
	),

	TP_fast_assign(
		snprintf(__entry->buf, MAX_BUF_LEN,
			"VM%d %s ring %d pt_level %d gma 0x%lx -> gpa 0x%lx\n",
				id, type, ring_id, pt_level, gma, gpa);
	),

	TP_printk("%s", __entry->buf)
);

TRACE_EVENT(spt_refcount,
	TP_PROTO(int id, char *action, void *spt, int before, int after),

	TP_ARGS(id, action, spt, before, after),

	TP_STRUCT__entry(
		__array(char, buf, MAX_BUF_LEN)
	),

	TP_fast_assign(
		snprintf(__entry->buf, MAX_BUF_LEN,
			"VM%d [%s] spt %p before %d -> after %d\n",
				id, action, spt, before, after);
	),

	TP_printk("%s", __entry->buf)
);

TRACE_EVENT(spt_change,
	TP_PROTO(int id, char *action, void *spt, unsigned long gfn,
		int type),

	TP_ARGS(id, action, spt, gfn, type),

	TP_STRUCT__entry(
		__array(char, buf, MAX_BUF_LEN)
	),

	TP_fast_assign(
		snprintf(__entry->buf, MAX_BUF_LEN,
			"VM%d [%s] spt %p gfn 0x%lx type %d\n",
				id, action, spt, gfn, type);
	),

	TP_printk("%s", __entry->buf)
);

TRACE_EVENT(gpt_change,
	TP_PROTO(int id, const char *tag, void *spt, int type, u64 v,
		unsigned long index),

	TP_ARGS(id, tag, spt, type, v, index),

	TP_STRUCT__entry(
		__array(char, buf, MAX_BUF_LEN)
	),

	TP_fast_assign(
		snprintf(__entry->buf, MAX_BUF_LEN,
		"VM%d [%s] spt %p type %d entry 0x%llx index 0x%lx\n",
			id, tag, spt, type, v, index);
	),

	TP_printk("%s", __entry->buf)
);

TRACE_EVENT(oos_change,
	TP_PROTO(int id, const char *tag, int page_id, void *gpt, int type),

	TP_ARGS(id, tag, page_id, gpt, type),

	TP_STRUCT__entry(
		__array(char, buf, MAX_BUF_LEN)
	),

	TP_fast_assign(
		snprintf(__entry->buf, MAX_BUF_LEN,
		"VM%d [oos %s] page id %d gpt %p type %d\n",
			id, tag, page_id, gpt, type);
	),

	TP_printk("%s", __entry->buf)
);

TRACE_EVENT(oos_sync,
	TP_PROTO(int id, int page_id, void *gpt, int type, u64 v,
		unsigned long index),

	TP_ARGS(id, page_id, gpt, type, v, index),

	TP_STRUCT__entry(
		__array(char, buf, MAX_BUF_LEN)
	),

	TP_fast_assign(
	snprintf(__entry->buf, MAX_BUF_LEN,
	"VM%d [oos sync] page id %d gpt %p type %d entry 0x%llx index 0x%lx\n",
				id, page_id, gpt, type, v, index);
	),

	TP_printk("%s", __entry->buf)
);

#endif /* _GVT_TRACE_H_ */

/* This part must be out of protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
