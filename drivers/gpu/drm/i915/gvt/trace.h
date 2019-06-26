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
	TP_PROTO(int id, char *type, int ring_id, int root_entry_type,
		unsigned long gma, unsigned long gpa),

	TP_ARGS(id, type, ring_id, root_entry_type, gma, gpa),

	TP_STRUCT__entry(
		__array(char, buf, MAX_BUF_LEN)
	),

	TP_fast_assign(
		snprintf(__entry->buf, MAX_BUF_LEN,
			"VM%d %s ring %d root_entry_type %d gma 0x%lx -> gpa 0x%lx\n",
			id, type, ring_id, root_entry_type, gma, gpa);
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

TRACE_EVENT(spt_guest_change,
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

#define GVT_CMD_STR_LEN 40
TRACE_EVENT(gvt_command,
	TP_PROTO(u8 vgpu_id, u8 ring_id, u32 ip_gma, u32 *cmd_va,
		u32 cmd_len,  u32 buf_type, u32 buf_addr_type,
		void *workload, const char *cmd_name),

	TP_ARGS(vgpu_id, ring_id, ip_gma, cmd_va, cmd_len, buf_type,
		buf_addr_type, workload, cmd_name),

	TP_STRUCT__entry(
		__field(u8, vgpu_id)
		__field(u8, ring_id)
		__field(u32, ip_gma)
		__field(u32, buf_type)
		__field(u32, buf_addr_type)
		__field(u32, cmd_len)
		__field(void*, workload)
		__dynamic_array(u32, raw_cmd, cmd_len)
		__array(char, cmd_name, GVT_CMD_STR_LEN)
	),

	TP_fast_assign(
		__entry->vgpu_id = vgpu_id;
		__entry->ring_id = ring_id;
		__entry->ip_gma = ip_gma;
		__entry->buf_type = buf_type;
		__entry->buf_addr_type = buf_addr_type;
		__entry->cmd_len = cmd_len;
		__entry->workload = workload;
		snprintf(__entry->cmd_name, GVT_CMD_STR_LEN, "%s", cmd_name);
		memcpy(__get_dynamic_array(raw_cmd), cmd_va, cmd_len * sizeof(*cmd_va));
	),


	TP_printk("vgpu%d ring %d: address_type %u, buf_type %u, ip_gma %08x,cmd (name=%s,len=%u,raw cmd=%s), workload=%p\n",
		__entry->vgpu_id,
		__entry->ring_id,
		__entry->buf_addr_type,
		__entry->buf_type,
		__entry->ip_gma,
		__entry->cmd_name,
		__entry->cmd_len,
		__print_array(__get_dynamic_array(raw_cmd),
			__entry->cmd_len, 4),
		__entry->workload)
);

#define GVT_TEMP_STR_LEN 10
TRACE_EVENT(write_ir,
	TP_PROTO(int id, char *reg_name, unsigned int reg, unsigned int new_val,
		 unsigned int old_val, bool changed),

	TP_ARGS(id, reg_name, reg, new_val, old_val, changed),

	TP_STRUCT__entry(
		__field(int, id)
		__array(char, buf, GVT_TEMP_STR_LEN)
		__field(unsigned int, reg)
		__field(unsigned int, new_val)
		__field(unsigned int, old_val)
		__field(bool, changed)
	),

	TP_fast_assign(
		__entry->id = id;
		snprintf(__entry->buf, GVT_TEMP_STR_LEN, "%s", reg_name);
		__entry->reg = reg;
		__entry->new_val = new_val;
		__entry->old_val = old_val;
		__entry->changed = changed;
	),

	TP_printk("VM%u write [%s] %x, new %08x, old %08x, changed %08x\n",
		  __entry->id, __entry->buf, __entry->reg, __entry->new_val,
		  __entry->old_val, __entry->changed)
);

TRACE_EVENT(propagate_event,
	TP_PROTO(int id, const char *irq_name, int bit),

	TP_ARGS(id, irq_name, bit),

	TP_STRUCT__entry(
		__field(int, id)
		__array(char, buf, GVT_TEMP_STR_LEN)
		__field(int, bit)
	),

	TP_fast_assign(
		__entry->id = id;
		snprintf(__entry->buf, GVT_TEMP_STR_LEN, "%s", irq_name);
		__entry->bit = bit;
	),

	TP_printk("Set bit (%d) for (%s) for vgpu (%d)\n",
		  __entry->bit, __entry->buf, __entry->id)
);

TRACE_EVENT(inject_msi,
	TP_PROTO(int id, unsigned int address, unsigned int data),

	TP_ARGS(id, address, data),

	TP_STRUCT__entry(
		__field(int, id)
		__field(unsigned int, address)
		__field(unsigned int, data)
	),

	TP_fast_assign(
		__entry->id = id;
		__entry->address = address;
		__entry->data = data;
	),

	TP_printk("vgpu%d:inject msi address %x data %x\n",
		  __entry->id, __entry->address, __entry->data)
);

TRACE_EVENT(render_mmio,
	TP_PROTO(int old_id, int new_id, char *action, unsigned int reg,
		 unsigned int old_val, unsigned int new_val),

	TP_ARGS(old_id, new_id, action, reg, old_val, new_val),

	TP_STRUCT__entry(
		__field(int, old_id)
		__field(int, new_id)
		__array(char, buf, GVT_TEMP_STR_LEN)
		__field(unsigned int, reg)
		__field(unsigned int, old_val)
		__field(unsigned int, new_val)
	),

	TP_fast_assign(
		__entry->old_id = old_id;
		__entry->new_id = new_id;
		snprintf(__entry->buf, GVT_TEMP_STR_LEN, "%s", action);
		__entry->reg = reg;
		__entry->old_val = old_val;
		__entry->new_val = new_val;
	),

	TP_printk("VM%u -> VM%u %s reg %x, old %08x new %08x\n",
		  __entry->old_id, __entry->new_id,
		  __entry->buf, __entry->reg,
		  __entry->old_val, __entry->new_val)
);

#endif /* _GVT_TRACE_H_ */

/* This part must be out of protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
