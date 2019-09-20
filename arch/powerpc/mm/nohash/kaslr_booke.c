// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (C) 2019 Jason Yan <yanaijie@huawei.com>

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/memblock.h>
#include <asm/pgalloc.h>
#include <asm/prom.h>
#include <mm/mmu_decl.h>

static unsigned long __init kaslr_choose_location(void *dt_ptr, phys_addr_t size,
						  unsigned long kernel_sz)
{
	/* return a fixed offset of 64M for now */
	return SZ_64M;
}

/*
 * To see if we need to relocate the kernel to a random offset
 * void *dt_ptr - address of the device tree
 * phys_addr_t size - size of the first memory block
 */
notrace void __init kaslr_early_init(void *dt_ptr, phys_addr_t size)
{
	unsigned long tlb_virt;
	phys_addr_t tlb_phys;
	unsigned long offset;
	unsigned long kernel_sz;

	kernel_sz = (unsigned long)_end - (unsigned long)_stext;

	offset = kaslr_choose_location(dt_ptr, size, kernel_sz);
	if (offset == 0)
		return;

	kernstart_virt_addr += offset;
	kernstart_addr += offset;

	is_second_reloc = 1;

	if (offset >= SZ_64M) {
		tlb_virt = round_down(kernstart_virt_addr, SZ_64M);
		tlb_phys = round_down(kernstart_addr, SZ_64M);

		/* Create kernel map to relocate in */
		create_kaslr_tlb_entry(1, tlb_virt, tlb_phys);
	}

	/* Copy the kernel to it's new location and run */
	memcpy((void *)kernstart_virt_addr, (void *)_stext, kernel_sz);
	flush_icache_range(kernstart_virt_addr, kernstart_virt_addr + kernel_sz);

	reloc_kernel_entry(dt_ptr, kernstart_virt_addr);
}
