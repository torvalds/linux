// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/bitops.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include <asm/asid.h>
#include <asm/mmu_context.h>
#include <asm/smp.h>
#include <asm/tlbflush.h>

static DEFINE_PER_CPU(atomic64_t, active_asids);
static DEFINE_PER_CPU(u64, reserved_asids);

struct asid_info asid_info;

void check_and_switch_context(struct mm_struct *mm, unsigned int cpu)
{
	asid_check_context(&asid_info, &mm->context.asid, cpu, mm);
}

static void asid_flush_cpu_ctxt(void)
{
	local_tlb_invalid_all();
}

static int asids_init(void)
{
	BUG_ON(((1 << CONFIG_CPU_ASID_BITS) - 1) <= num_possible_cpus());

	if (asid_allocator_init(&asid_info, CONFIG_CPU_ASID_BITS, 1,
				asid_flush_cpu_ctxt))
		panic("Unable to initialize ASID allocator for %lu ASIDs\n",
		      NUM_ASIDS(&asid_info));

	asid_info.active = &active_asids;
	asid_info.reserved = &reserved_asids;

	pr_info("ASID allocator initialised with %lu entries\n",
		NUM_CTXT_ASIDS(&asid_info));

	return 0;
}
early_initcall(asids_init);
