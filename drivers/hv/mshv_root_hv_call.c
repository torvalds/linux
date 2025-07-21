// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, Microsoft Corporation.
 *
 * Hypercall helper functions used by the mshv_root module.
 *
 * Authors: Microsoft Linux virtualization team
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/export.h>
#include <asm/mshyperv.h>

#include "mshv_root.h"

/* Determined empirically */
#define HV_INIT_PARTITION_DEPOSIT_PAGES 208
#define HV_MAP_GPA_DEPOSIT_PAGES	256
#define HV_UMAP_GPA_PAGES		512

#define HV_PAGE_COUNT_2M_ALIGNED(pg_count) (!((pg_count) & (0x200 - 1)))

#define HV_WITHDRAW_BATCH_SIZE	(HV_HYP_PAGE_SIZE / sizeof(u64))
#define HV_MAP_GPA_BATCH_SIZE	\
	((HV_HYP_PAGE_SIZE - sizeof(struct hv_input_map_gpa_pages)) \
		/ sizeof(u64))
#define HV_GET_VP_STATE_BATCH_SIZE	\
	((HV_HYP_PAGE_SIZE - sizeof(struct hv_input_get_vp_state)) \
		/ sizeof(u64))
#define HV_SET_VP_STATE_BATCH_SIZE	\
	((HV_HYP_PAGE_SIZE - sizeof(struct hv_input_set_vp_state)) \
		/ sizeof(u64))
#define HV_GET_GPA_ACCESS_STATES_BATCH_SIZE	\
	((HV_HYP_PAGE_SIZE - sizeof(union hv_gpa_page_access_state)) \
		/ sizeof(union hv_gpa_page_access_state))
#define HV_MODIFY_SPARSE_SPA_PAGE_HOST_ACCESS_MAX_PAGE_COUNT		       \
	((HV_HYP_PAGE_SIZE -						       \
	  sizeof(struct hv_input_modify_sparse_spa_page_host_access)) /        \
	 sizeof(u64))

int hv_call_withdraw_memory(u64 count, int node, u64 partition_id)
{
	struct hv_input_withdraw_memory *input_page;
	struct hv_output_withdraw_memory *output_page;
	struct page *page;
	u16 completed;
	unsigned long remaining = count;
	u64 status;
	int i;
	unsigned long flags;

	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;
	output_page = page_address(page);

	while (remaining) {
		local_irq_save(flags);

		input_page = *this_cpu_ptr(hyperv_pcpu_input_arg);

		memset(input_page, 0, sizeof(*input_page));
		input_page->partition_id = partition_id;
		status = hv_do_rep_hypercall(HVCALL_WITHDRAW_MEMORY,
					     min(remaining, HV_WITHDRAW_BATCH_SIZE),
					     0, input_page, output_page);

		local_irq_restore(flags);

		completed = hv_repcomp(status);

		for (i = 0; i < completed; i++)
			__free_page(pfn_to_page(output_page->gpa_page_list[i]));

		if (!hv_result_success(status)) {
			if (hv_result(status) == HV_STATUS_NO_RESOURCES)
				status = HV_STATUS_SUCCESS;
			break;
		}

		remaining -= completed;
	}
	free_page((unsigned long)output_page);

	return hv_result_to_errno(status);
}

int hv_call_create_partition(u64 flags,
			     struct hv_partition_creation_properties creation_properties,
			     union hv_partition_isolation_properties isolation_properties,
			     u64 *partition_id)
{
	struct hv_input_create_partition *input;
	struct hv_output_create_partition *output;
	u64 status;
	int ret;
	unsigned long irq_flags;

	do {
		local_irq_save(irq_flags);
		input = *this_cpu_ptr(hyperv_pcpu_input_arg);
		output = *this_cpu_ptr(hyperv_pcpu_output_arg);

		memset(input, 0, sizeof(*input));
		input->flags = flags;
		input->compatibility_version = HV_COMPATIBILITY_21_H2;

		memcpy(&input->partition_creation_properties, &creation_properties,
		       sizeof(creation_properties));

		memcpy(&input->isolation_properties, &isolation_properties,
		       sizeof(isolation_properties));

		status = hv_do_hypercall(HVCALL_CREATE_PARTITION,
					 input, output);

		if (hv_result(status) != HV_STATUS_INSUFFICIENT_MEMORY) {
			if (hv_result_success(status))
				*partition_id = output->partition_id;
			local_irq_restore(irq_flags);
			ret = hv_result_to_errno(status);
			break;
		}
		local_irq_restore(irq_flags);
		ret = hv_call_deposit_pages(NUMA_NO_NODE,
					    hv_current_partition_id, 1);
	} while (!ret);

	return ret;
}

int hv_call_initialize_partition(u64 partition_id)
{
	struct hv_input_initialize_partition input;
	u64 status;
	int ret;

	input.partition_id = partition_id;

	ret = hv_call_deposit_pages(NUMA_NO_NODE, partition_id,
				    HV_INIT_PARTITION_DEPOSIT_PAGES);
	if (ret)
		return ret;

	do {
		status = hv_do_fast_hypercall8(HVCALL_INITIALIZE_PARTITION,
					       *(u64 *)&input);

		if (hv_result(status) != HV_STATUS_INSUFFICIENT_MEMORY) {
			ret = hv_result_to_errno(status);
			break;
		}
		ret = hv_call_deposit_pages(NUMA_NO_NODE, partition_id, 1);
	} while (!ret);

	return ret;
}

int hv_call_finalize_partition(u64 partition_id)
{
	struct hv_input_finalize_partition input;
	u64 status;

	input.partition_id = partition_id;
	status = hv_do_fast_hypercall8(HVCALL_FINALIZE_PARTITION,
				       *(u64 *)&input);

	return hv_result_to_errno(status);
}

int hv_call_delete_partition(u64 partition_id)
{
	struct hv_input_delete_partition input;
	u64 status;

	input.partition_id = partition_id;
	status = hv_do_fast_hypercall8(HVCALL_DELETE_PARTITION, *(u64 *)&input);

	return hv_result_to_errno(status);
}

/* Ask the hypervisor to map guest ram pages or the guest mmio space */
static int hv_do_map_gpa_hcall(u64 partition_id, u64 gfn, u64 page_struct_count,
			       u32 flags, struct page **pages, u64 mmio_spa)
{
	struct hv_input_map_gpa_pages *input_page;
	u64 status, *pfnlist;
	unsigned long irq_flags, large_shift = 0;
	int ret = 0, done = 0;
	u64 page_count = page_struct_count;

	if (page_count == 0 || (pages && mmio_spa))
		return -EINVAL;

	if (flags & HV_MAP_GPA_LARGE_PAGE) {
		if (mmio_spa)
			return -EINVAL;

		if (!HV_PAGE_COUNT_2M_ALIGNED(page_count))
			return -EINVAL;

		large_shift = HV_HYP_LARGE_PAGE_SHIFT - HV_HYP_PAGE_SHIFT;
		page_count >>= large_shift;
	}

	while (done < page_count) {
		ulong i, completed, remain = page_count - done;
		int rep_count = min(remain, HV_MAP_GPA_BATCH_SIZE);

		local_irq_save(irq_flags);
		input_page = *this_cpu_ptr(hyperv_pcpu_input_arg);

		input_page->target_partition_id = partition_id;
		input_page->target_gpa_base = gfn + (done << large_shift);
		input_page->map_flags = flags;
		pfnlist = input_page->source_gpa_page_list;

		for (i = 0; i < rep_count; i++)
			if (flags & HV_MAP_GPA_NO_ACCESS) {
				pfnlist[i] = 0;
			} else if (pages) {
				u64 index = (done + i) << large_shift;

				if (index >= page_struct_count) {
					ret = -EINVAL;
					break;
				}
				pfnlist[i] = page_to_pfn(pages[index]);
			} else {
				pfnlist[i] = mmio_spa + done + i;
			}
		if (ret)
			break;

		status = hv_do_rep_hypercall(HVCALL_MAP_GPA_PAGES, rep_count, 0,
					     input_page, NULL);
		local_irq_restore(irq_flags);

		completed = hv_repcomp(status);

		if (hv_result(status) == HV_STATUS_INSUFFICIENT_MEMORY) {
			ret = hv_call_deposit_pages(NUMA_NO_NODE, partition_id,
						    HV_MAP_GPA_DEPOSIT_PAGES);
			if (ret)
				break;

		} else if (!hv_result_success(status)) {
			ret = hv_result_to_errno(status);
			break;
		}

		done += completed;
	}

	if (ret && done) {
		u32 unmap_flags = 0;

		if (flags & HV_MAP_GPA_LARGE_PAGE)
			unmap_flags |= HV_UNMAP_GPA_LARGE_PAGE;
		hv_call_unmap_gpa_pages(partition_id, gfn, done, unmap_flags);
	}

	return ret;
}

/* Ask the hypervisor to map guest ram pages */
int hv_call_map_gpa_pages(u64 partition_id, u64 gpa_target, u64 page_count,
			  u32 flags, struct page **pages)
{
	return hv_do_map_gpa_hcall(partition_id, gpa_target, page_count,
				   flags, pages, 0);
}

/* Ask the hypervisor to map guest mmio space */
int hv_call_map_mmio_pages(u64 partition_id, u64 gfn, u64 mmio_spa, u64 numpgs)
{
	int i;
	u32 flags = HV_MAP_GPA_READABLE | HV_MAP_GPA_WRITABLE |
		    HV_MAP_GPA_NOT_CACHED;

	for (i = 0; i < numpgs; i++)
		if (page_is_ram(mmio_spa + i))
			return -EINVAL;

	return hv_do_map_gpa_hcall(partition_id, gfn, numpgs, flags, NULL,
				   mmio_spa);
}

int hv_call_unmap_gpa_pages(u64 partition_id, u64 gfn, u64 page_count_4k,
			    u32 flags)
{
	struct hv_input_unmap_gpa_pages *input_page;
	u64 status, page_count = page_count_4k;
	unsigned long irq_flags, large_shift = 0;
	int ret = 0, done = 0;

	if (page_count == 0)
		return -EINVAL;

	if (flags & HV_UNMAP_GPA_LARGE_PAGE) {
		if (!HV_PAGE_COUNT_2M_ALIGNED(page_count))
			return -EINVAL;

		large_shift = HV_HYP_LARGE_PAGE_SHIFT - HV_HYP_PAGE_SHIFT;
		page_count >>= large_shift;
	}

	while (done < page_count) {
		ulong completed, remain = page_count - done;
		int rep_count = min(remain, HV_UMAP_GPA_PAGES);

		local_irq_save(irq_flags);
		input_page = *this_cpu_ptr(hyperv_pcpu_input_arg);

		input_page->target_partition_id = partition_id;
		input_page->target_gpa_base = gfn + (done << large_shift);
		input_page->unmap_flags = flags;
		status = hv_do_rep_hypercall(HVCALL_UNMAP_GPA_PAGES, rep_count,
					     0, input_page, NULL);
		local_irq_restore(irq_flags);

		completed = hv_repcomp(status);
		if (!hv_result_success(status)) {
			ret = hv_result_to_errno(status);
			break;
		}

		done += completed;
	}

	return ret;
}

int hv_call_get_gpa_access_states(u64 partition_id, u32 count, u64 gpa_base_pfn,
				  union hv_gpa_page_access_state_flags state_flags,
				  int *written_total,
				  union hv_gpa_page_access_state *states)
{
	struct hv_input_get_gpa_pages_access_state *input_page;
	union hv_gpa_page_access_state *output_page;
	int completed = 0;
	unsigned long remaining = count;
	int rep_count, i;
	u64 status = 0;
	unsigned long flags;

	*written_total = 0;
	while (remaining) {
		local_irq_save(flags);
		input_page = *this_cpu_ptr(hyperv_pcpu_input_arg);
		output_page = *this_cpu_ptr(hyperv_pcpu_output_arg);

		input_page->partition_id = partition_id;
		input_page->hv_gpa_page_number = gpa_base_pfn + *written_total;
		input_page->flags = state_flags;
		rep_count = min(remaining, HV_GET_GPA_ACCESS_STATES_BATCH_SIZE);

		status = hv_do_rep_hypercall(HVCALL_GET_GPA_PAGES_ACCESS_STATES, rep_count,
					     0, input_page, output_page);
		if (!hv_result_success(status)) {
			local_irq_restore(flags);
			break;
		}
		completed = hv_repcomp(status);
		for (i = 0; i < completed; ++i)
			states[i].as_uint8 = output_page[i].as_uint8;

		local_irq_restore(flags);
		states += completed;
		*written_total += completed;
		remaining -= completed;
	}

	return hv_result_to_errno(status);
}

int hv_call_assert_virtual_interrupt(u64 partition_id, u32 vector,
				     u64 dest_addr,
				     union hv_interrupt_control control)
{
	struct hv_input_assert_virtual_interrupt *input;
	unsigned long flags;
	u64 status;

	local_irq_save(flags);
	input = *this_cpu_ptr(hyperv_pcpu_input_arg);
	memset(input, 0, sizeof(*input));
	input->partition_id = partition_id;
	input->vector = vector;
	input->dest_addr = dest_addr;
	input->control = control;
	status = hv_do_hypercall(HVCALL_ASSERT_VIRTUAL_INTERRUPT, input, NULL);
	local_irq_restore(flags);

	return hv_result_to_errno(status);
}

int hv_call_delete_vp(u64 partition_id, u32 vp_index)
{
	union hv_input_delete_vp input = {};
	u64 status;

	input.partition_id = partition_id;
	input.vp_index = vp_index;

	status = hv_do_fast_hypercall16(HVCALL_DELETE_VP,
					input.as_uint64[0], input.as_uint64[1]);

	return hv_result_to_errno(status);
}
EXPORT_SYMBOL_GPL(hv_call_delete_vp);

int hv_call_get_vp_state(u32 vp_index, u64 partition_id,
			 struct hv_vp_state_data state_data,
			 /* Choose between pages and ret_output */
			 u64 page_count, struct page **pages,
			 union hv_output_get_vp_state *ret_output)
{
	struct hv_input_get_vp_state *input;
	union hv_output_get_vp_state *output;
	u64 status;
	int i;
	u64 control;
	unsigned long flags;
	int ret = 0;

	if (page_count > HV_GET_VP_STATE_BATCH_SIZE)
		return -EINVAL;

	if (!page_count && !ret_output)
		return -EINVAL;

	do {
		local_irq_save(flags);
		input = *this_cpu_ptr(hyperv_pcpu_input_arg);
		output = *this_cpu_ptr(hyperv_pcpu_output_arg);
		memset(input, 0, sizeof(*input));
		memset(output, 0, sizeof(*output));

		input->partition_id = partition_id;
		input->vp_index = vp_index;
		input->state_data = state_data;
		for (i = 0; i < page_count; i++)
			input->output_data_pfns[i] = page_to_pfn(pages[i]);

		control = (HVCALL_GET_VP_STATE) |
			  (page_count << HV_HYPERCALL_VARHEAD_OFFSET);

		status = hv_do_hypercall(control, input, output);

		if (hv_result(status) != HV_STATUS_INSUFFICIENT_MEMORY) {
			if (hv_result_success(status) && ret_output)
				memcpy(ret_output, output, sizeof(*output));

			local_irq_restore(flags);
			ret = hv_result_to_errno(status);
			break;
		}
		local_irq_restore(flags);

		ret = hv_call_deposit_pages(NUMA_NO_NODE,
					    partition_id, 1);
	} while (!ret);

	return ret;
}

int hv_call_set_vp_state(u32 vp_index, u64 partition_id,
			 /* Choose between pages and bytes */
			 struct hv_vp_state_data state_data, u64 page_count,
			 struct page **pages, u32 num_bytes, u8 *bytes)
{
	struct hv_input_set_vp_state *input;
	u64 status;
	int i;
	u64 control;
	unsigned long flags;
	int ret = 0;
	u16 varhead_sz;

	if (page_count > HV_SET_VP_STATE_BATCH_SIZE)
		return -EINVAL;
	if (sizeof(*input) + num_bytes > HV_HYP_PAGE_SIZE)
		return -EINVAL;

	if (num_bytes)
		/* round up to 8 and divide by 8 */
		varhead_sz = (num_bytes + 7) >> 3;
	else if (page_count)
		varhead_sz = page_count;
	else
		return -EINVAL;

	do {
		local_irq_save(flags);
		input = *this_cpu_ptr(hyperv_pcpu_input_arg);
		memset(input, 0, sizeof(*input));

		input->partition_id = partition_id;
		input->vp_index = vp_index;
		input->state_data = state_data;
		if (num_bytes) {
			memcpy((u8 *)input->data, bytes, num_bytes);
		} else {
			for (i = 0; i < page_count; i++)
				input->data[i].pfns = page_to_pfn(pages[i]);
		}

		control = (HVCALL_SET_VP_STATE) |
			  (varhead_sz << HV_HYPERCALL_VARHEAD_OFFSET);

		status = hv_do_hypercall(control, input, NULL);

		if (hv_result(status) != HV_STATUS_INSUFFICIENT_MEMORY) {
			local_irq_restore(flags);
			ret = hv_result_to_errno(status);
			break;
		}
		local_irq_restore(flags);

		ret = hv_call_deposit_pages(NUMA_NO_NODE,
					    partition_id, 1);
	} while (!ret);

	return ret;
}

int hv_call_map_vp_state_page(u64 partition_id, u32 vp_index, u32 type,
			      union hv_input_vtl input_vtl,
			      struct page **state_page)
{
	struct hv_input_map_vp_state_page *input;
	struct hv_output_map_vp_state_page *output;
	u64 status;
	int ret;
	unsigned long flags;

	do {
		local_irq_save(flags);

		input = *this_cpu_ptr(hyperv_pcpu_input_arg);
		output = *this_cpu_ptr(hyperv_pcpu_output_arg);

		input->partition_id = partition_id;
		input->vp_index = vp_index;
		input->type = type;
		input->input_vtl = input_vtl;

		status = hv_do_hypercall(HVCALL_MAP_VP_STATE_PAGE, input, output);

		if (hv_result(status) != HV_STATUS_INSUFFICIENT_MEMORY) {
			if (hv_result_success(status))
				*state_page = pfn_to_page(output->map_location);
			local_irq_restore(flags);
			ret = hv_result_to_errno(status);
			break;
		}

		local_irq_restore(flags);

		ret = hv_call_deposit_pages(NUMA_NO_NODE, partition_id, 1);
	} while (!ret);

	return ret;
}

int hv_call_unmap_vp_state_page(u64 partition_id, u32 vp_index, u32 type,
				union hv_input_vtl input_vtl)
{
	unsigned long flags;
	u64 status;
	struct hv_input_unmap_vp_state_page *input;

	local_irq_save(flags);

	input = *this_cpu_ptr(hyperv_pcpu_input_arg);

	memset(input, 0, sizeof(*input));

	input->partition_id = partition_id;
	input->vp_index = vp_index;
	input->type = type;
	input->input_vtl = input_vtl;

	status = hv_do_hypercall(HVCALL_UNMAP_VP_STATE_PAGE, input, NULL);

	local_irq_restore(flags);

	return hv_result_to_errno(status);
}

int
hv_call_clear_virtual_interrupt(u64 partition_id)
{
	int status;

	status = hv_do_fast_hypercall8(HVCALL_CLEAR_VIRTUAL_INTERRUPT,
				       partition_id);

	return hv_result_to_errno(status);
}

int
hv_call_create_port(u64 port_partition_id, union hv_port_id port_id,
		    u64 connection_partition_id,
		    struct hv_port_info *port_info,
		    u8 port_vtl, u8 min_connection_vtl, int node)
{
	struct hv_input_create_port *input;
	unsigned long flags;
	int ret = 0;
	int status;

	do {
		local_irq_save(flags);
		input = *this_cpu_ptr(hyperv_pcpu_input_arg);
		memset(input, 0, sizeof(*input));

		input->port_partition_id = port_partition_id;
		input->port_id = port_id;
		input->connection_partition_id = connection_partition_id;
		input->port_info = *port_info;
		input->port_vtl = port_vtl;
		input->min_connection_vtl = min_connection_vtl;
		input->proximity_domain_info = hv_numa_node_to_pxm_info(node);
		status = hv_do_hypercall(HVCALL_CREATE_PORT, input, NULL);
		local_irq_restore(flags);
		if (hv_result_success(status))
			break;

		if (hv_result(status) != HV_STATUS_INSUFFICIENT_MEMORY) {
			ret = hv_result_to_errno(status);
			break;
		}
		ret = hv_call_deposit_pages(NUMA_NO_NODE, port_partition_id, 1);

	} while (!ret);

	return ret;
}

int
hv_call_delete_port(u64 port_partition_id, union hv_port_id port_id)
{
	union hv_input_delete_port input = { 0 };
	int status;

	input.port_partition_id = port_partition_id;
	input.port_id = port_id;
	status = hv_do_fast_hypercall16(HVCALL_DELETE_PORT,
					input.as_uint64[0],
					input.as_uint64[1]);

	return hv_result_to_errno(status);
}

int
hv_call_connect_port(u64 port_partition_id, union hv_port_id port_id,
		     u64 connection_partition_id,
		     union hv_connection_id connection_id,
		     struct hv_connection_info *connection_info,
		     u8 connection_vtl, int node)
{
	struct hv_input_connect_port *input;
	unsigned long flags;
	int ret = 0, status;

	do {
		local_irq_save(flags);
		input = *this_cpu_ptr(hyperv_pcpu_input_arg);
		memset(input, 0, sizeof(*input));
		input->port_partition_id = port_partition_id;
		input->port_id = port_id;
		input->connection_partition_id = connection_partition_id;
		input->connection_id = connection_id;
		input->connection_info = *connection_info;
		input->connection_vtl = connection_vtl;
		input->proximity_domain_info = hv_numa_node_to_pxm_info(node);
		status = hv_do_hypercall(HVCALL_CONNECT_PORT, input, NULL);

		local_irq_restore(flags);
		if (hv_result_success(status))
			break;

		if (hv_result(status) != HV_STATUS_INSUFFICIENT_MEMORY) {
			ret = hv_result_to_errno(status);
			break;
		}
		ret = hv_call_deposit_pages(NUMA_NO_NODE,
					    connection_partition_id, 1);
	} while (!ret);

	return ret;
}

int
hv_call_disconnect_port(u64 connection_partition_id,
			union hv_connection_id connection_id)
{
	union hv_input_disconnect_port input = { 0 };
	int status;

	input.connection_partition_id = connection_partition_id;
	input.connection_id = connection_id;
	input.is_doorbell = 1;
	status = hv_do_fast_hypercall16(HVCALL_DISCONNECT_PORT,
					input.as_uint64[0],
					input.as_uint64[1]);

	return hv_result_to_errno(status);
}

int
hv_call_notify_port_ring_empty(u32 sint_index)
{
	union hv_input_notify_port_ring_empty input = { 0 };
	int status;

	input.sint_index = sint_index;
	status = hv_do_fast_hypercall8(HVCALL_NOTIFY_PORT_RING_EMPTY,
				       input.as_uint64);

	return hv_result_to_errno(status);
}

int hv_call_map_stat_page(enum hv_stats_object_type type,
			  const union hv_stats_object_identity *identity,
			  void **addr)
{
	unsigned long flags;
	struct hv_input_map_stats_page *input;
	struct hv_output_map_stats_page *output;
	u64 status, pfn;
	int ret = 0;

	do {
		local_irq_save(flags);
		input = *this_cpu_ptr(hyperv_pcpu_input_arg);
		output = *this_cpu_ptr(hyperv_pcpu_output_arg);

		memset(input, 0, sizeof(*input));
		input->type = type;
		input->identity = *identity;

		status = hv_do_hypercall(HVCALL_MAP_STATS_PAGE, input, output);
		pfn = output->map_location;

		local_irq_restore(flags);
		if (hv_result(status) != HV_STATUS_INSUFFICIENT_MEMORY) {
			ret = hv_result_to_errno(status);
			if (hv_result_success(status))
				break;
			return ret;
		}

		ret = hv_call_deposit_pages(NUMA_NO_NODE,
					    hv_current_partition_id, 1);
		if (ret)
			return ret;
	} while (!ret);

	*addr = page_address(pfn_to_page(pfn));

	return ret;
}

int hv_call_unmap_stat_page(enum hv_stats_object_type type,
			    const union hv_stats_object_identity *identity)
{
	unsigned long flags;
	struct hv_input_unmap_stats_page *input;
	u64 status;

	local_irq_save(flags);
	input = *this_cpu_ptr(hyperv_pcpu_input_arg);

	memset(input, 0, sizeof(*input));
	input->type = type;
	input->identity = *identity;

	status = hv_do_hypercall(HVCALL_UNMAP_STATS_PAGE, input, NULL);
	local_irq_restore(flags);

	return hv_result_to_errno(status);
}

int hv_call_modify_spa_host_access(u64 partition_id, struct page **pages,
				   u64 page_struct_count, u32 host_access,
				   u32 flags, u8 acquire)
{
	struct hv_input_modify_sparse_spa_page_host_access *input_page;
	u64 status;
	int done = 0;
	unsigned long irq_flags, large_shift = 0;
	u64 page_count = page_struct_count;
	u16 code = acquire ? HVCALL_ACQUIRE_SPARSE_SPA_PAGE_HOST_ACCESS :
			     HVCALL_RELEASE_SPARSE_SPA_PAGE_HOST_ACCESS;

	if (page_count == 0)
		return -EINVAL;

	if (flags & HV_MODIFY_SPA_PAGE_HOST_ACCESS_LARGE_PAGE) {
		if (!HV_PAGE_COUNT_2M_ALIGNED(page_count))
			return -EINVAL;
		large_shift = HV_HYP_LARGE_PAGE_SHIFT - HV_HYP_PAGE_SHIFT;
		page_count >>= large_shift;
	}

	while (done < page_count) {
		ulong i, completed, remain = page_count - done;
		int rep_count = min(remain,
				    HV_MODIFY_SPARSE_SPA_PAGE_HOST_ACCESS_MAX_PAGE_COUNT);

		local_irq_save(irq_flags);
		input_page = *this_cpu_ptr(hyperv_pcpu_input_arg);

		memset(input_page, 0, sizeof(*input_page));
		/* Only set the partition id if you are making the pages
		 * exclusive
		 */
		if (flags & HV_MODIFY_SPA_PAGE_HOST_ACCESS_MAKE_EXCLUSIVE)
			input_page->partition_id = partition_id;
		input_page->flags = flags;
		input_page->host_access = host_access;

		for (i = 0; i < rep_count; i++) {
			u64 index = (done + i) << large_shift;

			if (index >= page_struct_count)
				return -EINVAL;

			input_page->spa_page_list[i] =
						page_to_pfn(pages[index]);
		}

		status = hv_do_rep_hypercall(code, rep_count, 0, input_page,
					     NULL);
		local_irq_restore(irq_flags);

		completed = hv_repcomp(status);

		if (!hv_result_success(status))
			return hv_result_to_errno(status);

		done += completed;
	}

	return 0;
}
