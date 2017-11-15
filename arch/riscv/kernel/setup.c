/*
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Chen Liqin <liqin.chen@sunplusct.com>
 *  Lennox Wu <lennox.wu@sunplusct.com>
 * Copyright (C) 2012 Regents of the University of California
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 */

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/sched.h>
#include <linux/initrd.h>
#include <linux/console.h>
#include <linux/screen_info.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/sched/task.h>

#include <asm/setup.h>
#include <asm/sections.h>
#include <asm/pgtable.h>
#include <asm/smp.h>
#include <asm/sbi.h>
#include <asm/tlbflush.h>
#include <asm/thread_info.h>

#ifdef CONFIG_HVC_RISCV_SBI
#include <asm/hvc_riscv_sbi.h>
#endif

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

#ifdef CONFIG_CMDLINE_BOOL
static char __initdata builtin_cmdline[COMMAND_LINE_SIZE] = CONFIG_CMDLINE;
#endif /* CONFIG_CMDLINE_BOOL */

unsigned long va_pa_offset;
unsigned long pfn_base;

/* The lucky hart to first increment this variable will boot the other cores */
atomic_t hart_lottery;

#ifdef CONFIG_BLK_DEV_INITRD
static void __init setup_initrd(void)
{
	extern char __initramfs_start[];
	extern unsigned long __initramfs_size;
	unsigned long size;

	if (__initramfs_size > 0) {
		initrd_start = (unsigned long)(&__initramfs_start);
		initrd_end = initrd_start + __initramfs_size;
	}

	if (initrd_start >= initrd_end) {
		printk(KERN_INFO "initrd not found or empty");
		goto disable;
	}
	if (__pa(initrd_end) > PFN_PHYS(max_low_pfn)) {
		printk(KERN_ERR "initrd extends beyond end of memory");
		goto disable;
	}

	size =  initrd_end - initrd_start;
	memblock_reserve(__pa(initrd_start), size);
	initrd_below_start_ok = 1;

	printk(KERN_INFO "Initial ramdisk at: 0x%p (%lu bytes)\n",
		(void *)(initrd_start), size);
	return;
disable:
	pr_cont(" - disabling initrd\n");
	initrd_start = 0;
	initrd_end = 0;
}
#endif /* CONFIG_BLK_DEV_INITRD */

pgd_t swapper_pg_dir[PTRS_PER_PGD] __page_aligned_bss;
pgd_t trampoline_pg_dir[PTRS_PER_PGD] __initdata __aligned(PAGE_SIZE);

#ifndef __PAGETABLE_PMD_FOLDED
#define NUM_SWAPPER_PMDS ((uintptr_t)-PAGE_OFFSET >> PGDIR_SHIFT)
pmd_t swapper_pmd[PTRS_PER_PMD*((-PAGE_OFFSET)/PGDIR_SIZE)] __page_aligned_bss;
pmd_t trampoline_pmd[PTRS_PER_PGD] __initdata __aligned(PAGE_SIZE);
#endif

asmlinkage void __init setup_vm(void)
{
	extern char _start;
	uintptr_t i;
	uintptr_t pa = (uintptr_t) &_start;
	pgprot_t prot = __pgprot(pgprot_val(PAGE_KERNEL) | _PAGE_EXEC);

	va_pa_offset = PAGE_OFFSET - pa;
	pfn_base = PFN_DOWN(pa);

	/* Sanity check alignment and size */
	BUG_ON((PAGE_OFFSET % PGDIR_SIZE) != 0);
	BUG_ON((pa % (PAGE_SIZE * PTRS_PER_PTE)) != 0);

#ifndef __PAGETABLE_PMD_FOLDED
	trampoline_pg_dir[(PAGE_OFFSET >> PGDIR_SHIFT) % PTRS_PER_PGD] =
		pfn_pgd(PFN_DOWN((uintptr_t)trampoline_pmd),
			__pgprot(_PAGE_TABLE));
	trampoline_pmd[0] = pfn_pmd(PFN_DOWN(pa), prot);

	for (i = 0; i < (-PAGE_OFFSET)/PGDIR_SIZE; ++i) {
		size_t o = (PAGE_OFFSET >> PGDIR_SHIFT) % PTRS_PER_PGD + i;
		swapper_pg_dir[o] =
			pfn_pgd(PFN_DOWN((uintptr_t)swapper_pmd) + i,
				__pgprot(_PAGE_TABLE));
	}
	for (i = 0; i < ARRAY_SIZE(swapper_pmd); i++)
		swapper_pmd[i] = pfn_pmd(PFN_DOWN(pa + i * PMD_SIZE), prot);
#else
	trampoline_pg_dir[(PAGE_OFFSET >> PGDIR_SHIFT) % PTRS_PER_PGD] =
		pfn_pgd(PFN_DOWN(pa), prot);

	for (i = 0; i < (-PAGE_OFFSET)/PGDIR_SIZE; ++i) {
		size_t o = (PAGE_OFFSET >> PGDIR_SHIFT) % PTRS_PER_PGD + i;
		swapper_pg_dir[o] =
			pfn_pgd(PFN_DOWN(pa + i * PGDIR_SIZE), prot);
	}
#endif
}

void __init sbi_save(unsigned int hartid, void *dtb)
{
	early_init_dt_scan(__va(dtb));
}

/*
 * Allow the user to manually add a memory region (in case DTS is broken);
 * "mem_end=nn[KkMmGg]"
 */
static int __init mem_end_override(char *p)
{
	resource_size_t base, end;

	if (!p)
		return -EINVAL;
	base = (uintptr_t) __pa(PAGE_OFFSET);
	end = memparse(p, &p) & PMD_MASK;
	if (end == 0)
		return -EINVAL;
	memblock_add(base, end - base);
	return 0;
}
early_param("mem_end", mem_end_override);

static void __init setup_bootmem(void)
{
	struct memblock_region *reg;
	phys_addr_t mem_size = 0;

	/* Find the memory region containing the kernel */
	for_each_memblock(memory, reg) {
		phys_addr_t vmlinux_end = __pa(_end);
		phys_addr_t end = reg->base + reg->size;

		if (reg->base <= vmlinux_end && vmlinux_end <= end) {
			/*
			 * Reserve from the start of the region to the end of
			 * the kernel
			 */
			memblock_reserve(reg->base, vmlinux_end - reg->base);
			mem_size = min(reg->size, (phys_addr_t)-PAGE_OFFSET);
		}
	}
	BUG_ON(mem_size == 0);

	set_max_mapnr(PFN_DOWN(mem_size));
	max_low_pfn = pfn_base + PFN_DOWN(mem_size);

#ifdef CONFIG_BLK_DEV_INITRD
	setup_initrd();
#endif /* CONFIG_BLK_DEV_INITRD */

	early_init_fdt_reserve_self();
	early_init_fdt_scan_reserved_mem();
	memblock_allow_resize();
	memblock_dump_all();
}

void __init setup_arch(char **cmdline_p)
{
#if defined(CONFIG_HVC_RISCV_SBI)
	if (likely(early_console == NULL)) {
		early_console = &riscv_sbi_early_console_dev;
		register_console(early_console);
	}
#endif

#ifdef CONFIG_CMDLINE_BOOL
#ifdef CONFIG_CMDLINE_OVERRIDE
	strlcpy(boot_command_line, builtin_cmdline, COMMAND_LINE_SIZE);
#else
	if (builtin_cmdline[0] != '\0') {
		/* Append bootloader command line to built-in */
		strlcat(builtin_cmdline, " ", COMMAND_LINE_SIZE);
		strlcat(builtin_cmdline, boot_command_line, COMMAND_LINE_SIZE);
		strlcpy(boot_command_line, builtin_cmdline, COMMAND_LINE_SIZE);
	}
#endif /* CONFIG_CMDLINE_OVERRIDE */
#endif /* CONFIG_CMDLINE_BOOL */
	*cmdline_p = boot_command_line;

	parse_early_param();

	init_mm.start_code = (unsigned long) _stext;
	init_mm.end_code   = (unsigned long) _etext;
	init_mm.end_data   = (unsigned long) _edata;
	init_mm.brk        = (unsigned long) _end;

	setup_bootmem();
	paging_init();
	unflatten_device_tree();

#ifdef CONFIG_SMP
	setup_smp();
#endif

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	riscv_fill_hwcap();
}

static int __init riscv_device_init(void)
{
	return of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}
subsys_initcall_sync(riscv_device_init);
