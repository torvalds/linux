// SPDX-License-Identifier: GPL-2.0

/*
 * Hyper-V nested virtualization code.
 *
 * Copyright (C) 2018, Microsoft, Inc.
 *
 * Author : Lan Tianyu <Tianyu.Lan@microsoft.com>
 */
#define pr_fmt(fmt)  "Hyper-V: " fmt


#include <linux/types.h>
#include <asm/hyperv-tlfs.h>
#include <asm/mshyperv.h>
#include <asm/tlbflush.h>

#include <asm/trace/hyperv.h>

int hyperv_flush_guest_mapping(u64 as)
{
	struct hv_guest_mapping_flush *flush;
	u64 status;
	unsigned long flags;
	int ret = -ENOTSUPP;

	if (!hv_hypercall_pg)
		goto fault;

	local_irq_save(flags);

	flush = *this_cpu_ptr(hyperv_pcpu_input_arg);

	if (unlikely(!flush)) {
		local_irq_restore(flags);
		goto fault;
	}

	flush->address_space = as;
	flush->flags = 0;

	status = hv_do_hypercall(HVCALL_FLUSH_GUEST_PHYSICAL_ADDRESS_SPACE,
				 flush, NULL);
	local_irq_restore(flags);

	if (hv_result_success(status))
		ret = 0;

fault:
	trace_hyperv_nested_flush_guest_mapping(as, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(hyperv_flush_guest_mapping);

int hyperv_fill_flush_guest_mapping_list(
		struct hv_guest_mapping_flush_list *flush,
		u64 start_gfn, u64 pages)
{
	u64 cur = start_gfn;
	u64 additional_pages;
	int gpa_n = 0;

	do {
		/*
		 * If flush requests exceed max flush count, go back to
		 * flush tlbs without range.
		 */
		if (gpa_n >= HV_MAX_FLUSH_REP_COUNT)
			return -ENOSPC;

		additional_pages = min_t(u64, pages, HV_MAX_FLUSH_PAGES) - 1;

		flush->gpa_list[gpa_n].page.additional_pages = additional_pages;
		flush->gpa_list[gpa_n].page.largepage = false;
		flush->gpa_list[gpa_n].page.basepfn = cur;

		pages -= additional_pages + 1;
		cur += additional_pages + 1;
		gpa_n++;
	} while (pages > 0);

	return gpa_n;
}
EXPORT_SYMBOL_GPL(hyperv_fill_flush_guest_mapping_list);

int hyperv_flush_guest_mapping_range(u64 as,
		hyperv_fill_flush_list_func fill_flush_list_func, void *data)
{
	struct hv_guest_mapping_flush_list *flush;
	u64 status;
	unsigned long flags;
	int ret = -ENOTSUPP;
	int gpa_n = 0;

	if (!hv_hypercall_pg || !fill_flush_list_func)
		goto fault;

	local_irq_save(flags);

	flush = *this_cpu_ptr(hyperv_pcpu_input_arg);

	if (unlikely(!flush)) {
		local_irq_restore(flags);
		goto fault;
	}

	flush->address_space = as;
	flush->flags = 0;

	gpa_n = fill_flush_list_func(flush, data);
	if (gpa_n < 0) {
		local_irq_restore(flags);
		goto fault;
	}

	status = hv_do_rep_hypercall(HVCALL_FLUSH_GUEST_PHYSICAL_ADDRESS_LIST,
				     gpa_n, 0, flush, NULL);

	local_irq_restore(flags);

	if (hv_result_success(status))
		ret = 0;
	else
		ret = hv_result(status);
fault:
	trace_hyperv_nested_flush_guest_mapping_range(as, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(hyperv_flush_guest_mapping_range);
