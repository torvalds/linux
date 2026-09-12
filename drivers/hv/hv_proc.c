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

#define HV_DEPOSIT_MAX 512
#define HV_DEPOSIT_INP_MAX ((HV_HYP_PAGE_SIZE -  \
	offsetof(struct hv_deposit_memory, gpa_page_list)) / sizeof(u64))

/*
 * Allocate free pages for deposit to hypervisor. pfna[] must be large enough
 * to hold HV_DEPOSIT_INP_MAX (511) pages. If num_pages is 512, return last
 * pfn in lastpfn. If @single, then it must be a single allocation (not split
 * over multiple contiguous ranges).
 *
 * Returns: number of pages allocated or -ENOMEM
 */
static int hv_alloc_dep_pages(int node, u64 *pfna, u64 *lastpfnp, int num_pages,
			      bool single)
{
	struct page *page;
	int num_allocd, count = 0;

	/* Published ABI, enforce its immutability. */
	BUILD_BUG_ON(HV_DEPOSIT_INP_MAX != 511);

	if (num_pages > HV_DEPOSIT_MAX ||
	    (num_pages == HV_DEPOSIT_MAX && lastpfnp == NULL))
		return -EINVAL;

	while (num_pages) {
		/* Find highest order we can actually allocate */
		int order = 31 - __builtin_clz(num_pages);
		gfp_t gfp_flags = GFP_KERNEL;

		if (!single)
			gfp_flags |= __GFP_NOWARN;

		while (1) {
			page = alloc_pages_node(node, gfp_flags, order);
			if (page || order == 0 || single)
				break;

			order--;
		}

		if (page == NULL)
			break;

		split_page(page, order);
		num_allocd = 1 << order;
		num_pages -= num_allocd;

		while (num_allocd && count < HV_DEPOSIT_INP_MAX) {
			pfna[count++] = page_to_pfn(page++);
			num_allocd--;
		}

		if (num_allocd-- && count == HV_DEPOSIT_INP_MAX) {
			*lastpfnp = page_to_pfn(page);
			count++;
			break;
		}
	}

	return count ? count : -ENOMEM;
}

/*
 * Deposit memory in the hypervisor. Even if @contiguous is false, a contiguous
 * 2M worth of pfns is utmost desired for performance reasons. But short of
 * that, we deposit whatever contiguous chunks we can get. If @contiguous is
 * true, then the entire range has to be physically contiguous. Note, in that
 * case, upon withdrawl, hypervisor could return any page in between the range,
 * so we must split that also. Lastly, HV_MAX_CONTIGUOUS_ALLOCATION_PAGES is
 * not guaranteed to always be power of 2.
 */
static int hv_call_deposit_pages(int node, u64 partition_id, bool contiguous)
{
	struct hv_deposit_memory *hc_input;
	int i, rc, num_pages;
	u64 status, *pfna, lastpfn = 0;
	bool trunc_extra = false;

	BUILD_BUG_ON(HV_MAX_CONTIGUOUS_ALLOCATION_PAGES > HV_DEPOSIT_MAX);

	if (contiguous) {
		num_pages = roundup_pow_of_two(
					  HV_MAX_CONTIGUOUS_ALLOCATION_PAGES);
		trunc_extra = num_pages != HV_MAX_CONTIGUOUS_ALLOCATION_PAGES;
	} else {
		num_pages = HV_DEPOSIT_MAX;
	}

	hc_input = (struct hv_deposit_memory *)get_zeroed_page(GFP_KERNEL);
	if (hc_input == NULL)
		return -ENOMEM;

	hc_input->partition_id = partition_id;
	pfna = hc_input->gpa_page_list;

	rc = hv_alloc_dep_pages(node, pfna, &lastpfn, num_pages, contiguous);
	if (rc < 0)
		goto out_free;

	num_pages = rc;
	if (num_pages > HV_DEPOSIT_INP_MAX)
		num_pages = HV_DEPOSIT_INP_MAX;

	if (contiguous && trunc_extra) {
		for (i = HV_MAX_CONTIGUOUS_ALLOCATION_PAGES; i < num_pages; i++)
			__free_page(pfn_to_page(pfna[i]));

		if (lastpfn) {
			__free_page(pfn_to_page(lastpfn));
			lastpfn = 0;
		}

		num_pages = HV_MAX_CONTIGUOUS_ALLOCATION_PAGES;
	}

	/* Not using hyperv_pcpu_input_arg, so no need to disable interrupts */

	status = hv_do_rep_hypercall(HVCALL_DEPOSIT_MEMORY, num_pages, 0,
				     hc_input, NULL);
	if (!hv_result_success(status))
		goto err_free_dep_pages;

	if (lastpfn) {
		hc_input->gpa_page_list[0] = lastpfn;
		status = hv_do_rep_hypercall(HVCALL_DEPOSIT_MEMORY, 1, 0,
					     hc_input, NULL);

		if (!hv_result_success(status) && hv_repcomp(status) == 0)
			/* We deposited lot earlier, so give it a go */
			__free_page(pfn_to_page(lastpfn));
	}

	free_page((unsigned long)hc_input);
	return 0;

err_free_dep_pages:
	hv_status_err(status, "\n");
	rc = hv_result_to_errno(status);

	for (i = hv_repcomp(status); i < num_pages; i++)
		__free_page(pfn_to_page(pfna[i]));
	if (lastpfn)
		__free_page(pfn_to_page(lastpfn));

out_free:
	free_page((unsigned long)hc_input);
	return rc;
}

int hv_deposit_memory_node(int node, u64 pt_id, u64 hv_status)
{
	int result = hv_result(hv_status);
	bool contiguous = false;

	if (result == HV_STATUS_INSUFFICIENT_ROOT_MEMORY ||
	    result == HV_STATUS_INSUFFICIENT_CONTIGUOUS_ROOT_MEMORY) {
		if (!hv_root_partition()) {
			hv_status_err(hv_status,
				      "Unexpected root memory deposit\n");
			return -EINVAL;
		}

		pt_id = HV_PARTITION_ID_SELF;
	}

	if (result == HV_STATUS_INSUFFICIENT_CONTIGUOUS_MEMORY ||
	    result == HV_STATUS_INSUFFICIENT_CONTIGUOUS_ROOT_MEMORY)
		contiguous = true;

	return hv_call_deposit_pages(node, pt_id, contiguous);
}
EXPORT_SYMBOL_GPL(hv_deposit_memory_node);

/*
 * See struct hv_deposit_memory. The first u64 is partition ID, the rest
 * are GPAs.
 */
#define HV_DEPOSIT_MAX_OLD (HV_HYP_PAGE_SIZE / sizeof(u64) - 1)

/* Deposits exact number of pages. Must be called with interrupts enabled.  */
static int hv_call_deposit_pages_old(int node, u64 partition_id, u32 num_pages)
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

	if (num_pages > HV_DEPOSIT_MAX_OLD)
		return -E2BIG;
	if (!num_pages)
		return 0;

	/* One buffer for page pointers and counts */
	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;
	pages = page_address(page);

	counts = kzalloc_objs(int, HV_DEPOSIT_MAX_OLD);
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

static int __maybe_unused hv_deposit_memory_node_old(int node, u64 partition_id, u64 hv_status)
{
	u32 num_pages = 1;

	switch (hv_result(hv_status)) {
	case HV_STATUS_INSUFFICIENT_MEMORY:
		break;
	case HV_STATUS_INSUFFICIENT_CONTIGUOUS_MEMORY:
		num_pages = HV_MAX_CONTIGUOUS_ALLOCATION_PAGES;
		break;

	case HV_STATUS_INSUFFICIENT_CONTIGUOUS_ROOT_MEMORY:
		num_pages = HV_MAX_CONTIGUOUS_ALLOCATION_PAGES;
		fallthrough;
	case HV_STATUS_INSUFFICIENT_ROOT_MEMORY:
		if (!hv_root_partition()) {
			hv_status_err(hv_status, "Unexpected root memory deposit\n");
			return -ENOMEM;
		}
		partition_id = HV_PARTITION_ID_SELF;
		break;

	default:
		hv_status_err(hv_status, "Unexpected!\n");
		return -ENOMEM;
	}
	return hv_call_deposit_pages_old(node, partition_id, num_pages);
}

bool hv_result_needs_memory(u64 status)
{
	switch (hv_result(status)) {
	case HV_STATUS_INSUFFICIENT_MEMORY:
	case HV_STATUS_INSUFFICIENT_CONTIGUOUS_MEMORY:
	case HV_STATUS_INSUFFICIENT_ROOT_MEMORY:
	case HV_STATUS_INSUFFICIENT_CONTIGUOUS_ROOT_MEMORY:
		return true;
	}
	return false;
}
EXPORT_SYMBOL_GPL(hv_result_needs_memory);

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

		if (!hv_result_needs_memory(status)) {
			if (!hv_result_success(status)) {
				hv_status_err(status, "cpu %u apic ID: %u\n",
					      lp_index, apic_id);
				ret = hv_result_to_errno(status);
			}
			break;
		}
		ret = hv_deposit_memory_node(node, hv_current_partition_id,
					     status);
	} while (!ret);

	return ret;
}

int hv_call_create_vp(int node, u64 partition_id, u32 vp_index, u32 flags)
{
	struct hv_create_vp *input;
	u64 status;
	unsigned long irq_flags;
	int ret = 0;

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

		if (!hv_result_needs_memory(status)) {
			if (!hv_result_success(status)) {
				hv_status_err(status, "vcpu: %u, lp: %u\n",
					      vp_index, flags);
				ret = hv_result_to_errno(status);
			}
			break;
		}
		ret = hv_deposit_memory_node(node, partition_id, status);

	} while (!ret);

	return ret;
}
EXPORT_SYMBOL_GPL(hv_call_create_vp);

int hv_call_notify_all_processors_started(void)
{
	struct hv_input_notify_partition_event *input;
	u64 status;
	unsigned long irq_flags;
	int ret = 0;

	local_irq_save(irq_flags);
	input = *this_cpu_ptr(hyperv_pcpu_input_arg);
	memset(input, 0, sizeof(*input));
	input->event = HV_PARTITION_ALL_LOGICAL_PROCESSORS_STARTED;
	status = hv_do_hypercall(HVCALL_NOTIFY_PARTITION_EVENT,
				 input, NULL);
	local_irq_restore(irq_flags);

	if (!hv_result_success(status)) {
		hv_status_err(status, "\n");
		ret = hv_result_to_errno(status);
	}
	return ret;
}

bool hv_lp_exists(u32 lp_index)
{
	struct hv_input_get_logical_processor_run_time *input;
	struct hv_output_get_logical_processor_run_time *output;
	unsigned long flags;
	u64 status;

	local_irq_save(flags);
	input = *this_cpu_ptr(hyperv_pcpu_input_arg);
	output = *this_cpu_ptr(hyperv_pcpu_output_arg);

	input->lp_index = lp_index;
	status = hv_do_hypercall(HVCALL_GET_LOGICAL_PROCESSOR_RUN_TIME,
				 input, output);
	local_irq_restore(flags);

	if (!hv_result_success(status) &&
	    hv_result(status) != HV_STATUS_INVALID_LP_INDEX) {
		hv_status_err(status, "\n");
		BUG();
	}

	return hv_result_success(status);
}
