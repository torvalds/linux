/*
 * Nios2-specific parts of system setup
 *
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 * Copyright (C) 2001 Vic Phillips <vic@microtronix.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/console.h>
#include <linux/memblock.h>
#include <linux/initrd.h>
#include <linux/of_fdt.h>

#include <asm/mmu_context.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/cpuinfo.h>

unsigned long memory_start;
EXPORT_SYMBOL(memory_start);

unsigned long memory_end;
EXPORT_SYMBOL(memory_end);

static struct pt_regs fake_regs = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
					0, 0, 0, 0, 0, 0,
					0};

/* Copy a short hook instruction sequence to the exception address */
static inline void copy_exception_handler(unsigned int addr)
{
	unsigned int start = (unsigned int) exception_handler_hook;
	volatile unsigned int tmp = 0;

	if (start == addr) {
		/* The CPU exception address already points to the handler. */
		return;
	}

	__asm__ __volatile__ (
		"ldw	%2,0(%0)\n"
		"stw	%2,0(%1)\n"
		"ldw	%2,4(%0)\n"
		"stw	%2,4(%1)\n"
		"ldw	%2,8(%0)\n"
		"stw	%2,8(%1)\n"
		"flushd	0(%1)\n"
		"flushd	4(%1)\n"
		"flushd	8(%1)\n"
		"flushi %1\n"
		"addi	%1,%1,4\n"
		"flushi %1\n"
		"addi	%1,%1,4\n"
		"flushi %1\n"
		"flushp\n"
		: /* no output registers */
		: "r" (start), "r" (addr), "r" (tmp)
		: "memory"
	);
}

/* Copy the fast TLB miss handler */
static inline void copy_fast_tlb_miss_handler(unsigned int addr)
{
	unsigned int start = (unsigned int) fast_handler;
	unsigned int end = (unsigned int) fast_handler_end;
	volatile unsigned int tmp = 0;

	__asm__ __volatile__ (
		"1:\n"
		"	ldw	%3,0(%0)\n"
		"	stw	%3,0(%1)\n"
		"	flushd	0(%1)\n"
		"	flushi	%1\n"
		"	flushp\n"
		"	addi	%0,%0,4\n"
		"	addi	%1,%1,4\n"
		"	bne	%0,%2,1b\n"
		: /* no output registers */
		: "r" (start), "r" (addr), "r" (end), "r" (tmp)
		: "memory"
	);
}

/*
 * save args passed from u-boot, called from head.S
 *
 * @r4: NIOS magic
 * @r5: initrd start
 * @r6: initrd end or fdt
 * @r7: kernel command line
 */
asmlinkage void __init nios2_boot_init(unsigned r4, unsigned r5, unsigned r6,
				       unsigned r7)
{
	unsigned dtb_passed = 0;
	char cmdline_passed[COMMAND_LINE_SIZE] __maybe_unused = { 0, };

#if defined(CONFIG_NIOS2_PASS_CMDLINE)
	if (r4 == 0x534f494e) { /* r4 is magic NIOS */
#if defined(CONFIG_BLK_DEV_INITRD)
		if (r5) { /* initramfs */
			initrd_start = r5;
			initrd_end = r6;
		}
#endif /* CONFIG_BLK_DEV_INITRD */
		dtb_passed = r6;

		if (r7)
			strscpy(cmdline_passed, (char *)r7, COMMAND_LINE_SIZE);
	}
#endif

	early_init_devtree((void *)dtb_passed);

#ifndef CONFIG_CMDLINE_FORCE
	if (cmdline_passed[0])
		strscpy(boot_command_line, cmdline_passed, COMMAND_LINE_SIZE);
#ifdef CONFIG_NIOS2_CMDLINE_IGNORE_DTB
	else
		strscpy(boot_command_line, CONFIG_CMDLINE, COMMAND_LINE_SIZE);
#endif
#endif

	parse_early_param();
}

static void __init find_limits(unsigned long *min, unsigned long *max_low,
			       unsigned long *max_high)
{
	*max_low = PFN_DOWN(memblock_get_current_limit());
	*min = PFN_UP(memblock_start_of_DRAM());
	*max_high = PFN_DOWN(memblock_end_of_DRAM());
}

static void __init adjust_lowmem_bounds(void)
{
	phys_addr_t block_start, block_end;
	u64 i;
	phys_addr_t memblock_limit = 0;

	for_each_mem_range(i, &block_start, &block_end) {
		if (block_end > memblock_limit)
			memblock_limit = block_end;
	}

	memblock_set_current_limit(memblock_limit);
}

void __init setup_arch(char **cmdline_p)
{
	console_verbose();

	memory_start = memblock_start_of_DRAM();
	memory_end = memblock_end_of_DRAM();

	pr_debug("%s: start=%lx, end=%lx\n", __func__, memory_start, memory_end);

	setup_initial_init_mm(_stext, _etext, _edata, _end);
	init_task.thread.kregs = &fake_regs;

	/* Keep a copy of command line */
	*cmdline_p = boot_command_line;

	adjust_lowmem_bounds();
	find_limits(&min_low_pfn, &max_low_pfn, &max_pfn);

	memblock_reserve(__pa_symbol(_stext), _end - _stext);
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start) {
		memblock_reserve(virt_to_phys((void *)initrd_start),
				initrd_end - initrd_start);
	}
#endif /* CONFIG_BLK_DEV_INITRD */

	early_init_fdt_reserve_self();
	early_init_fdt_scan_reserved_mem();

	unflatten_and_copy_device_tree();

	setup_cpuinfo();

	copy_exception_handler(cpuinfo.exception_addr);

	mmu_init();

	copy_fast_tlb_miss_handler(cpuinfo.fast_tlb_miss_exc_addr);

	/*
	 * Initialize MMU context handling here because data from cpuinfo is
	 * needed for this.
	 */
	mmu_context_init();

	/*
	 * get kmalloc into gear
	 */
	paging_init();
}
