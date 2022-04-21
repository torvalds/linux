/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019, 2021 The Linux Foundation. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM secure_buffer

#if !defined(_TRACE_SECURE_BUFFER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SECURE_BUFFER_H
#include <linux/types.h>
#include <linux/tracepoint.h>
#include <soc/qcom/secure_buffer.h>

TRACE_EVENT(hyp_assign_info,

	TP_PROTO(u32 *source_vm_list,
		 int source_nelems, int *dest_vmids, int *dest_perms,
		 int dest_nelems),

	TP_ARGS(source_vm_list, source_nelems, dest_vmids,
		dest_perms, dest_nelems),

	TP_STRUCT__entry(
		__field(int, source_nelems)
		__field(int, dest_nelems)
		__dynamic_array(u32, source_vm_list, source_nelems)
		__dynamic_array(int, dest_vmids, dest_nelems)
		__dynamic_array(int, dest_perms, dest_nelems)
	),

	TP_fast_assign(
		__entry->source_nelems = source_nelems;
		__entry->dest_nelems = dest_nelems;
		memcpy(__get_dynamic_array(source_vm_list), source_vm_list,
		       source_nelems * sizeof(*source_vm_list));
		memcpy(__get_dynamic_array(dest_vmids), dest_vmids,
		       dest_nelems * sizeof(*dest_vmids));
		memcpy(__get_dynamic_array(dest_perms), dest_perms,
		       dest_nelems * sizeof(*dest_perms));
	),

	TP_printk("srcVMIDs: %s dstVMIDs: %s dstPerms: %s",
		  __print_array(__get_dynamic_array(source_vm_list),
				__entry->source_nelems, sizeof(u32)),
		  __print_array(__get_dynamic_array(dest_vmids),
				__entry->dest_nelems, sizeof(int)),
		  __print_array(__get_dynamic_array(dest_perms),
				__entry->dest_nelems, sizeof(int))
	)
);

TRACE_EVENT(hyp_assign_batch_start,

	TP_PROTO(struct qcom_scm_mem_map_info *info, int info_nelems),

	TP_ARGS(info, info_nelems),

	TP_STRUCT__entry(
		__field(int, info_nelems)
		__field(u64, batch_size)
		__dynamic_array(u64, addrs, info_nelems)
		__dynamic_array(u64, sizes, info_nelems)
	),

	TP_fast_assign(
		unsigned int i;
		u64 *addr_arr_ptr = __get_dynamic_array(addrs);
		u64 *sizes_arr_ptr = __get_dynamic_array(sizes);

		__entry->info_nelems = info_nelems;
		__entry->batch_size = 0;

		for (i = 0; i < info_nelems; i++) {
			addr_arr_ptr[i] = le64_to_cpu(info[i].mem_addr);
			sizes_arr_ptr[i] = le64_to_cpu(info[i].mem_size);
			__entry->batch_size += le64_to_cpu(info[i].mem_size);
		}
	),

	TP_printk("num entries: %d batch size: %llu phys addrs: %s sizes: %s",
		  __entry->info_nelems, __entry->batch_size,
		  __print_array(__get_dynamic_array(addrs),
				__entry->info_nelems, sizeof(u64)),
		  __print_array(__get_dynamic_array(sizes),
				__entry->info_nelems, sizeof(u64))
	)
);

TRACE_EVENT(hyp_assign_batch_end,

	TP_PROTO(int ret, u64 delta),

	TP_ARGS(ret, delta),

	TP_STRUCT__entry(
		__field(int, ret)
		__field(u64, delta)
	),

	TP_fast_assign(
		__entry->ret = ret;
		__entry->delta = delta;
	),

	TP_printk("ret: %d time delta: %lld us",
		  __entry->ret, __entry->delta
	)
);

TRACE_EVENT(hyp_assign_end,

	TP_PROTO(u64 tot_delta, u64 avg_delta),

	TP_ARGS(tot_delta, avg_delta),

	TP_STRUCT__entry(
		__field(u64, tot_delta)
		__field(u64, avg_delta)
	),

	TP_fast_assign(
		__entry->tot_delta = tot_delta;
		__entry->avg_delta = avg_delta;
	),

	TP_printk("total time delta: %lld us avg batch delta: %lld us",
		  __entry->tot_delta, __entry->avg_delta
	)
);
#endif /* _TRACE_SECURE_BUFFER_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/soc/qcom/

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace_secure_buffer

/* This part must be outside protection */
#include <trace/define_trace.h>
