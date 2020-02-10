// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/console.h>
#include <linux/memblock.h>
#include <linux/initrd.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/start_kernel.h>
#include <linux/dma-contiguous.h>
#include <linux/screen_info.h>
#include <asm/sections.h>
#include <asm/mmu_context.h>
#include <asm/pgalloc.h>

#ifdef CONFIG_DUMMY_CONSOLE
struct screen_info screen_info = {
	.orig_video_lines	= 30,
	.orig_video_cols	= 80,
	.orig_video_mode	= 0,
	.orig_video_ega_bx	= 0,
	.orig_video_isVGA	= 1,
	.orig_video_points	= 8
};
#endif

phys_addr_t __init_memblock memblock_end_of_REG0(void)
{
	return (memblock.memory.regions[0].base +
		memblock.memory.regions[0].size);
}

phys_addr_t __init_memblock memblock_start_of_REG1(void)
{
	return memblock.memory.regions[1].base;
}

size_t __init_memblock memblock_size_of_REG1(void)
{
	return memblock.memory.regions[1].size;
}

static void __init csky_memblock_init(void)
{
	unsigned long zone_size[MAX_NR_ZONES];
	unsigned long zhole_size[MAX_NR_ZONES];
	signed long size;

	memblock_reserve(__pa(_stext), _end - _stext);
#ifdef CONFIG_BLK_DEV_INITRD
	memblock_reserve(__pa(initrd_start), initrd_end - initrd_start);
#endif

	early_init_fdt_reserve_self();
	early_init_fdt_scan_reserved_mem();

	memblock_dump_all();

	memset(zone_size, 0, sizeof(zone_size));
	memset(zhole_size, 0, sizeof(zhole_size));

	min_low_pfn = PFN_UP(memblock_start_of_DRAM());
	max_pfn	    = PFN_DOWN(memblock_end_of_DRAM());

	max_low_pfn = PFN_UP(memblock_end_of_REG0());
	if (max_low_pfn == 0)
		max_low_pfn = max_pfn;

	size = max_pfn - min_low_pfn;

	if (memblock.memory.cnt > 1) {
		zone_size[ZONE_NORMAL]  =
			PFN_DOWN(memblock_start_of_REG1()) - min_low_pfn;
		zhole_size[ZONE_NORMAL] =
			PFN_DOWN(memblock_start_of_REG1()) - max_low_pfn;
	} else {
		if (size <= PFN_DOWN(LOWMEM_LIMIT - PHYS_OFFSET_OFFSET))
			zone_size[ZONE_NORMAL] = max_pfn - min_low_pfn;
		else {
			zone_size[ZONE_NORMAL] =
				PFN_DOWN(LOWMEM_LIMIT - PHYS_OFFSET_OFFSET);
			max_low_pfn = min_low_pfn + zone_size[ZONE_NORMAL];
		}
	}

#ifdef CONFIG_HIGHMEM
	size = 0;
	if (memblock.memory.cnt > 1) {
		size = PFN_DOWN(memblock_size_of_REG1());
		highstart_pfn = PFN_DOWN(memblock_start_of_REG1());
	} else {
		size = max_pfn - min_low_pfn -
			PFN_DOWN(LOWMEM_LIMIT - PHYS_OFFSET_OFFSET);
		highstart_pfn =  min_low_pfn +
			PFN_DOWN(LOWMEM_LIMIT - PHYS_OFFSET_OFFSET);
	}

	if (size > 0)
		zone_size[ZONE_HIGHMEM] = size;

	highend_pfn = max_pfn;
#endif
	memblock_set_current_limit(PFN_PHYS(max_low_pfn));

	dma_contiguous_reserve(0);

	free_area_init_node(0, zone_size, min_low_pfn, zhole_size);
}

void __init setup_arch(char **cmdline_p)
{
	*cmdline_p = boot_command_line;

	console_verbose();

	pr_info("Phys. mem: %ldMB\n",
		(unsigned long) memblock_phys_mem_size()/1024/1024);

	init_mm.start_code = (unsigned long) _stext;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;
	init_mm.brk = (unsigned long) _end;

	parse_early_param();

	csky_memblock_init();

	unflatten_and_copy_device_tree();

#ifdef CONFIG_SMP
	setup_smp();
#endif

	sparse_init();

#ifdef CONFIG_HIGHMEM
	kmap_init();
#endif
}

unsigned long va_pa_offset;
EXPORT_SYMBOL(va_pa_offset);

asmlinkage __visible void __init csky_start(unsigned int unused,
					    void *dtb_start)
{
	/* Clean up bss section */
	memset(__bss_start, 0, __bss_stop - __bss_start);

	va_pa_offset = read_mmu_msa0() & ~(SSEG_SIZE - 1);

	pre_trap_init();
	pre_mmu_init();

	if (dtb_start == NULL)
		early_init_dt_scan(__dtb_start);
	else
		early_init_dt_scan(dtb_start);

	start_kernel();

	asm volatile("br .\n");
}
