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
#include <linux/libfdt.h>
#include <linux/crash_core.h>
#include <asm/cacheflush.h>
#include <asm/prom.h>
#include <asm/kdump.h>
#include <mm/mmu_decl.h>
#include <generated/compile.h>
#include <generated/utsrelease.h>

struct regions {
	unsigned long pa_start;
	unsigned long pa_end;
	unsigned long kernel_size;
	unsigned long dtb_start;
	unsigned long dtb_end;
	unsigned long initrd_start;
	unsigned long initrd_end;
	unsigned long crash_start;
	unsigned long crash_end;
	int reserved_mem;
	int reserved_mem_addr_cells;
	int reserved_mem_size_cells;
};

/* Simplified build-specific string for starting entropy. */
static const char build_str[] = UTS_RELEASE " (" LINUX_COMPILE_BY "@"
		LINUX_COMPILE_HOST ") (" LINUX_COMPILER ") " UTS_VERSION;

struct regions __initdata regions;

static __init void kaslr_get_cmdline(void *fdt)
{
	int node = fdt_path_offset(fdt, "/chosen");

	early_init_dt_scan_chosen(node, "chosen", 1, boot_command_line);
}

static unsigned long __init rotate_xor(unsigned long hash, const void *area,
				       size_t size)
{
	size_t i;
	const unsigned long *ptr = area;

	for (i = 0; i < size / sizeof(hash); i++) {
		/* Rotate by odd number of bits and XOR. */
		hash = (hash << ((sizeof(hash) * 8) - 7)) | (hash >> 7);
		hash ^= ptr[i];
	}

	return hash;
}

/* Attempt to create a simple starting entropy. This can make it defferent for
 * every build but it is still not enough. Stronger entropy should
 * be added to make it change for every boot.
 */
static unsigned long __init get_boot_seed(void *fdt)
{
	unsigned long hash = 0;

	hash = rotate_xor(hash, build_str, sizeof(build_str));
	hash = rotate_xor(hash, fdt, fdt_totalsize(fdt));

	return hash;
}

static __init u64 get_kaslr_seed(void *fdt)
{
	int node, len;
	fdt64_t *prop;
	u64 ret;

	node = fdt_path_offset(fdt, "/chosen");
	if (node < 0)
		return 0;

	prop = fdt_getprop_w(fdt, node, "kaslr-seed", &len);
	if (!prop || len != sizeof(u64))
		return 0;

	ret = fdt64_to_cpu(*prop);
	*prop = 0;
	return ret;
}

static __init bool regions_overlap(u32 s1, u32 e1, u32 s2, u32 e2)
{
	return e1 >= s2 && e2 >= s1;
}

static __init bool overlaps_reserved_region(const void *fdt, u32 start,
					    u32 end)
{
	int subnode, len, i;
	u64 base, size;

	/* check for overlap with /memreserve/ entries */
	for (i = 0; i < fdt_num_mem_rsv(fdt); i++) {
		if (fdt_get_mem_rsv(fdt, i, &base, &size) < 0)
			continue;
		if (regions_overlap(start, end, base, base + size))
			return true;
	}

	if (regions.reserved_mem < 0)
		return false;

	/* check for overlap with static reservations in /reserved-memory */
	for (subnode = fdt_first_subnode(fdt, regions.reserved_mem);
	     subnode >= 0;
	     subnode = fdt_next_subnode(fdt, subnode)) {
		const fdt32_t *reg;
		u64 rsv_end;

		len = 0;
		reg = fdt_getprop(fdt, subnode, "reg", &len);
		while (len >= (regions.reserved_mem_addr_cells +
			       regions.reserved_mem_size_cells)) {
			base = fdt32_to_cpu(reg[0]);
			if (regions.reserved_mem_addr_cells == 2)
				base = (base << 32) | fdt32_to_cpu(reg[1]);

			reg += regions.reserved_mem_addr_cells;
			len -= 4 * regions.reserved_mem_addr_cells;

			size = fdt32_to_cpu(reg[0]);
			if (regions.reserved_mem_size_cells == 2)
				size = (size << 32) | fdt32_to_cpu(reg[1]);

			reg += regions.reserved_mem_size_cells;
			len -= 4 * regions.reserved_mem_size_cells;

			if (base >= regions.pa_end)
				continue;

			rsv_end = min(base + size, (u64)U32_MAX);

			if (regions_overlap(start, end, base, rsv_end))
				return true;
		}
	}
	return false;
}

static __init bool overlaps_region(const void *fdt, u32 start,
				   u32 end)
{
	if (regions_overlap(start, end, __pa(_stext), __pa(_end)))
		return true;

	if (regions_overlap(start, end, regions.dtb_start,
			    regions.dtb_end))
		return true;

	if (regions_overlap(start, end, regions.initrd_start,
			    regions.initrd_end))
		return true;

	if (regions_overlap(start, end, regions.crash_start,
			    regions.crash_end))
		return true;

	return overlaps_reserved_region(fdt, start, end);
}

static void __init get_crash_kernel(void *fdt, unsigned long size)
{
#ifdef CONFIG_CRASH_CORE
	unsigned long long crash_size, crash_base;
	int ret;

	ret = parse_crashkernel(boot_command_line, size, &crash_size,
				&crash_base);
	if (ret != 0 || crash_size == 0)
		return;
	if (crash_base == 0)
		crash_base = KDUMP_KERNELBASE;

	regions.crash_start = (unsigned long)crash_base;
	regions.crash_end = (unsigned long)(crash_base + crash_size);

	pr_debug("crash_base=0x%llx crash_size=0x%llx\n", crash_base, crash_size);
#endif
}

static void __init get_initrd_range(void *fdt)
{
	u64 start, end;
	int node, len;
	const __be32 *prop;

	node = fdt_path_offset(fdt, "/chosen");
	if (node < 0)
		return;

	prop = fdt_getprop(fdt, node, "linux,initrd-start", &len);
	if (!prop)
		return;
	start = of_read_number(prop, len / 4);

	prop = fdt_getprop(fdt, node, "linux,initrd-end", &len);
	if (!prop)
		return;
	end = of_read_number(prop, len / 4);

	regions.initrd_start = (unsigned long)start;
	regions.initrd_end = (unsigned long)end;

	pr_debug("initrd_start=0x%llx  initrd_end=0x%llx\n", start, end);
}

static __init unsigned long get_usable_address(const void *fdt,
					       unsigned long start,
					       unsigned long offset)
{
	unsigned long pa;
	unsigned long pa_end;

	for (pa = offset; (long)pa > (long)start; pa -= SZ_16K) {
		pa_end = pa + regions.kernel_size;
		if (overlaps_region(fdt, pa, pa_end))
			continue;

		return pa;
	}
	return 0;
}

static __init void get_cell_sizes(const void *fdt, int node, int *addr_cells,
				  int *size_cells)
{
	const int *prop;
	int len;

	/*
	 * Retrieve the #address-cells and #size-cells properties
	 * from the 'node', or use the default if not provided.
	 */
	*addr_cells = *size_cells = 1;

	prop = fdt_getprop(fdt, node, "#address-cells", &len);
	if (len == 4)
		*addr_cells = fdt32_to_cpu(*prop);
	prop = fdt_getprop(fdt, node, "#size-cells", &len);
	if (len == 4)
		*size_cells = fdt32_to_cpu(*prop);
}

static unsigned long __init kaslr_legal_offset(void *dt_ptr, unsigned long index,
					       unsigned long offset)
{
	unsigned long koffset = 0;
	unsigned long start;

	while ((long)index >= 0) {
		offset = memstart_addr + index * SZ_64M + offset;
		start = memstart_addr + index * SZ_64M;
		koffset = get_usable_address(dt_ptr, start, offset);
		if (koffset)
			break;
		index--;
	}

	if (koffset != 0)
		koffset -= memstart_addr;

	return koffset;
}

static inline __init bool kaslr_disabled(void)
{
	return strstr(boot_command_line, "nokaslr") != NULL;
}

static unsigned long __init kaslr_choose_location(void *dt_ptr, phys_addr_t size,
						  unsigned long kernel_sz)
{
	unsigned long offset, random;
	unsigned long ram, linear_sz;
	u64 seed;
	unsigned long index;

	kaslr_get_cmdline(dt_ptr);
	if (kaslr_disabled())
		return 0;

	random = get_boot_seed(dt_ptr);

	seed = get_tb() << 32;
	seed ^= get_tb();
	random = rotate_xor(random, &seed, sizeof(seed));

	/*
	 * Retrieve (and wipe) the seed from the FDT
	 */
	seed = get_kaslr_seed(dt_ptr);
	if (seed)
		random = rotate_xor(random, &seed, sizeof(seed));
	else
		pr_warn("KASLR: No safe seed for randomizing the kernel base.\n");

	ram = min_t(phys_addr_t, __max_low_memory, size);
	ram = map_mem_in_cams(ram, CONFIG_LOWMEM_CAM_NUM, true);
	linear_sz = min_t(unsigned long, ram, SZ_512M);

	/* If the linear size is smaller than 64M, do not randmize */
	if (linear_sz < SZ_64M)
		return 0;

	/* check for a reserved-memory node and record its cell sizes */
	regions.reserved_mem = fdt_path_offset(dt_ptr, "/reserved-memory");
	if (regions.reserved_mem >= 0)
		get_cell_sizes(dt_ptr, regions.reserved_mem,
			       &regions.reserved_mem_addr_cells,
			       &regions.reserved_mem_size_cells);

	regions.pa_start = memstart_addr;
	regions.pa_end = memstart_addr + linear_sz;
	regions.dtb_start = __pa(dt_ptr);
	regions.dtb_end = __pa(dt_ptr) + fdt_totalsize(dt_ptr);
	regions.kernel_size = kernel_sz;

	get_initrd_range(dt_ptr);
	get_crash_kernel(dt_ptr, ram);

	/*
	 * Decide which 64M we want to start
	 * Only use the low 8 bits of the random seed
	 */
	index = random & 0xFF;
	index %= linear_sz / SZ_64M;

	/* Decide offset inside 64M */
	offset = random % (SZ_64M - kernel_sz);
	offset = round_down(offset, SZ_16K);

	return kaslr_legal_offset(dt_ptr, index, offset);
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

void __init kaslr_late_init(void)
{
	/* If randomized, clear the original kernel */
	if (kernstart_virt_addr != KERNELBASE) {
		unsigned long kernel_sz;

		kernel_sz = (unsigned long)_end - kernstart_virt_addr;
		memzero_explicit((void *)KERNELBASE, kernel_sz);
	}
}
