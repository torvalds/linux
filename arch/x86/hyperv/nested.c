// SPDX-License-Identifier: GPL-2.0

/*
 * Hyper-V nested virtualization code.
 *
 * Copyright (C) 2018, Microsoft, Inc.
 *
 * Author : Lan Tianyu <Tianyu.Lan@microsoft.com>
 */


#include <linux/types.h>
#include <asm/hyperv-tlfs.h>
#include <asm/mshyperv.h>
#include <asm/tlbflush.h>

#include <asm/trace/hyperv.h>

int hyperv_flush_guest_mapping(u64 as)
{
	struct hv_guest_mapping_flush **flush_pcpu;
	struct hv_guest_mapping_flush *flush;
	u64 status;
	unsigned long flags;
	int ret = -ENOTSUPP;

	if (!hv_hypercall_pg)
		goto fault;

	local_irq_save(flags);

	flush_pcpu = (struct hv_guest_mapping_flush **)
		this_cpu_ptr(hyperv_pcpu_input_arg);

	flush = *flush_pcpu;

	if (unlikely(!flush)) {
		local_irq_restore(flags);
		goto fault;
	}

	flush->address_space = as;
	flush->flags = 0;

	status = hv_do_hypercall(HVCALL_FLUSH_GUEST_PHYSICAL_ADDRESS_SPACE,
				 flush, NULL);
	local_irq_restore(flags);

	if (!(status & HV_HYPERCALL_RESULT_MASK))
		ret = 0;

fault:
	trace_hyperv_nested_flush_guest_mapping(as, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(hyperv_flush_guest_mapping);
