// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/clockchips.h>
#include <linux/slab.h>
#include <linux/cpuhotplug.h>
#include <linux/minmax.h>
#include <linux/export.h>
#include <asm/mshyperv.h>

/*
 * See struct hv_deposit_memory. The first u64 is partition ID, the rest
 * are GPAs.
 */
#define HV_DEPOSIT_MAX (HV_HYP_PAGE_SIZE / sizeof(u64) - 1)

/* Deposits exact number of pages. Must be called with interrupts enabled.  */
int hv_call_deposit_pages(int node, u64 partition_id, u32 num_pages)
{
	struct page **pages, *page;
	int *counts;
	int num_allocations;
	int i, j, page_count;
	int order;
	u64 status;
	int ret;
	u64 base_pfn;
	struct hv_deposit_memory *input_page;
	unsigned long flags;

	if (num_pages > HV_DEPOSIT_MAX)
		return -E2BIG;
	if (!num_pages)
		return 0;

	/* One buffer for page pointers and counts */
	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;
	pages = page_address(page);

	counts = kcalloc(HV_DEPOSIT_MAX, sizeof(int), GFP_KERNEL);
	if (!counts) {
		free_page((unsigned long)pages);
		return -ENOMEM;
	}

	/* Allocate all the pages before disabling interrupts */
	i = 0;

	while (num_pages) {
		/* Find highest order we can actually allocate */
		order = 31 - __builtin_clz(num_pages);

		while (1) {
			pages[i] = alloc_pages_node(node, GFP_KERNEL, order);
			if (pages[i])
				break;
			if (!order) {
				ret = -ENOMEM;
				num_allocations = i;
				goto err_free_allocations;
			}
			--order;
		}

		split_page(pages[i], order);
		counts[i] = 1 << order;
		num_pages -= counts[i];
		i++;
	}
	num_allocations = i;

	local_irq_save(flags);

	input_page = *this_cpu_ptr(hyperv_pcpu_input_arg);

	input_page->partition_id = partition_id;

	/* Populate gpa_page_list - these will fit on the input page */
	for (i = 0, page_count = 0; i < num_allocations; ++i) {
		base_pfn = page_to_pfn(pages[i]);
		for (j = 0; j < counts[i]; ++j, ++page_count)
			input_page->gpa_page_list[page_count] = base_pfn + j;
	}
	status = hv_do_rep_hypercall(HVCALL_DEPOSIT_MEMORY,
				     page_count, 0, input_page, NULL);
	local_irq_restore(flags);
	if (!hv_result_success(status)) {
		hv_status_err(status, "\n");
		ret = hv_result_to_errno(status);
		goto err_free_allocations;
	}

	ret = 0;
	goto free_buf;

err_free_allocations:
	for (i = 0; i < num_allocations; ++i) {
		base_pfn = page_to_pfn(pages[i]);
		for (j = 0; j < counts[i]; ++j)
			__free_page(pfn_to_page(base_pfn + j));
	}

free_buf:
	free_page((unsigned long)pages);
	kfree(counts);
	return ret;
}
EXPORT_SYMBOL_GPL(hv_call_deposit_pages);

int hv_call_add_logical_proc(int node, u32 lp_index, u32 apic_id)
{
	struct hv_input_add_logical_processor *input;
	struct hv_output_add_logical_processor *output;
	u64 status;
	unsigned long flags;
	int ret = 0;

	/*
	 * When adding a logical processor, the hypervisor may return
	 * HV_STATUS_INSUFFICIENT_MEMORY. When that happens, we deposit more
	 * pages and retry.
	 */
	do {
		local_irq_save(flags);

		input = *this_cpu_ptr(hyperv_pcpu_input_arg);
		/* We don't do anything with the output right now */
		output = *this_cpu_ptr(hyperv_pcpu_output_arg);

		input->lp_index = lp_index;
		input->apic_id = apic_id;
		input->proximity_domain_info = hv_numa_node_to_pxm_info(node);
		status = hv_do_hypercall(HVCALL_ADD_LOGICAL_PROCESSOR,
					 input, output);
		local_irq_restore(flags);

		if (hv_result(status) != HV_STATUS_INSUFFICIENT_MEMORY) {
			if (!hv_result_success(status)) {
				hv_status_err(status, "cpu %u apic ID: %u\n",
					      lp_index, apic_id);
				ret = hv_result_to_errno(status);
			}
			break;
		}
		ret = hv_call_deposit_pages(node, hv_current_partition_id, 1);
	} while (!ret);

	return ret;
}

int hv_call_create_vp(int node, u64 partition_id, u32 vp_index, u32 flags)
{
	struct hv_create_vp *input;
	u64 status;
	unsigned long irq_flags;
	int ret = 0;

	/* Root VPs don't seem to need pages deposited */
	if (partition_id != hv_current_partition_id) {
		/* The value 90 is empirically determined. It may change. */
		ret = hv_call_deposit_pages(node, partition_id, 90);
		if (ret)
			return ret;
	}

	do {
		local_irq_save(irq_flags);

		input = *this_cpu_ptr(hyperv_pcpu_input_arg);

		input->partition_id = partition_id;
		input->vp_index = vp_index;
		input->flags = flags;
		input->subnode_type = HV_SUBNODE_ANY;
		input->proximity_domain_info = hv_numa_node_to_pxm_info(node);
		status = hv_do_hypercall(HVCALL_CREATE_VP, input, NULL);
		local_irq_restore(irq_flags);

		if (hv_result(status) != HV_STATUS_INSUFFICIENT_MEMORY) {
			if (!hv_result_success(status)) {
				hv_status_err(status, "vcpu: %u, lp: %u\n",
					      vp_index, flags);
				ret = hv_result_to_errno(status);
			}
			break;
		}
		ret = hv_call_deposit_pages(node, partition_id, 1);

	} while (!ret);

	return ret;
}
EXPORT_SYMBOL_GPL(hv_call_create_vp);
