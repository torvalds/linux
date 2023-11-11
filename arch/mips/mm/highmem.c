// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/highmem.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <asm/fixmap.h>
#include <asm/tlbflush.h>

unsigned long highstart_pfn, highend_pfn;

void kmap_flush_tlb(unsigned long addr)
{
	flush_tlb_one(addr);
}
EXPORT_SYMBOL(kmap_flush_tlb);
