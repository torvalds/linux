/*
 * Nios2-specific parts of system setup
 *
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 * Copyright (C) 2001 Vic Phillips <vic@microtronix.com>
 *
 * based on kernel/setup.c from m68knommu
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/console.h>
#include <linux/bootmem.h>
#include <linux/initrd.h>
#include <linux/of_fdt.h>

#include <asm/mmu_context.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/prom.h>
#include <asm/cpuinfo.h>

unsigned long memory_start;
EXPORT_SYMBOL(memory_start);

unsigned long memory_end;
EXPORT_SYMBOL(memory_end);

unsigned long memory_size;

char cmd_line[COMMAND_LINE_SIZE] = { 0, };

/*				r1  r2  r3  r4  r5  r6  r7  r8  r9 r10 r11*/
/*				r12 r13 r14 r15 or2 ra  fp  sp  gp es  ste  ea*/
static struct pt_regs fake_regs = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
					0, (unsigned long)cpu_idle, 0, 0, 0, 0,
					0};

/* Copy a short hook instruction sequence to the exception address */
static inline void copy_exception_handler(unsigned int addr)
{
	unsigned int start = (unsigned int) exception_handler_hook;
	volatile unsigned int tmp = 0;

	/* FIXME: check overlap of source and destination address here? */

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
		: /* no output registers */
		: "r" (start), "r" (addr), "r" (tmp)
		: "memory"
	);
}

#ifdef CONFIG_MMU
/* Copy the fast TLB miss handler */
static inline void copy_fast_tlb_miss_handler(unsigned int addr)
{
	unsigned int start = (unsigned int) fast_handler;
	unsigned int end = (unsigned int) fast_handler_end;
	volatile unsigned int tmp = 0;

	/* FIXME: check overlap of source and destination address here? */

	__asm__ __volatile__ (
		"1:\n"
		"	ldw	%3,0(%0)\n"
		"	stw	%3,0(%1)\n"
		"	flushd	0(%1)\n"
		"	flushi	%1\n"
		"	addi	%0,%0,4\n"
		"	addi	%1,%1,4\n"
		"	bne	%0,%2,1b\n"
		: /* no output registers */
		: "r" (start), "r" (addr), "r" (end), "r" (tmp)
		: "memory"
	);
}
#endif /* CONFIG_MMU */

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
	char cmdline_passed[COMMAND_LINE_SIZE] = { 0, };

#ifdef CONFIG_MMU
	mmu_init();
#endif

#if defined(CONFIG_PASS_CMDLINE)
	if (r4 == 0x534f494e) { /* r4 is magic NIOS */
#if defined(CONFIG_BLK_DEV_INITRD)
		if (r5) { /* initramfs */
			initrd_start = r5;
			initrd_end = r6;
		}
#endif /* CONFIG_BLK_DEV_INITRD */
		dtb_passed = r6;

		if (r7)
			strncpy(cmdline_passed, (char *)r7, COMMAND_LINE_SIZE);
	}
#endif

	early_init_devtree((void *)dtb_passed);

#ifndef CONFIG_CMDLINE_FORCE
	if (cmdline_passed[0])
		strncpy(cmd_line, cmdline_passed, COMMAND_LINE_SIZE);
#ifdef CONFIG_CMDLINE_IGNORE_DTB
	else
		strncpy(cmd_line, CONFIG_CMDLINE, COMMAND_LINE_SIZE);
#endif
#endif
}

void __init setup_arch(char **cmdline_p)
{
	int bootmap_size;

	console_verbose();

#ifdef CONFIG_EARLY_PRINTK
	setup_early_printk();
#endif

	memory_start = PAGE_ALIGN((unsigned long)__pa(_end));
	memory_end = (unsigned long) CONFIG_MEM_BASE + memory_size;

	init_mm.start_code = (unsigned long) _stext;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;
	init_mm.brk = (unsigned long) _end;
	init_task.thread.kregs = &fake_regs;

	/* Keep a copy of command line */
	*cmdline_p = &cmd_line[0];

	memcpy(boot_command_line, cmd_line, COMMAND_LINE_SIZE);
	boot_command_line[COMMAND_LINE_SIZE-1] = 0;

	/*
	 * give all the memory to the bootmap allocator,  tell it to put the
	 * boot mem_map at the start of memory
	 */
	pr_debug("init_bootmem_node(?,%#lx, %#x, %#lx)\n",
		PFN_UP(memory_start), PFN_DOWN(PHYS_OFFSET),
		PFN_DOWN(memory_end));
	bootmap_size = init_bootmem_node(NODE_DATA(0),
					 PFN_UP(memory_start),
					 PFN_DOWN(PHYS_OFFSET),
					 PFN_DOWN(memory_end));

	/*
	 * free the usable memory,  we have to make sure we do not free
	 * the bootmem bitmap so we then reserve it after freeing it :-)
	 */
	pr_debug("free_bootmem(%#lx, %#lx)\n",
		memory_start, memory_end - memory_start);
	free_bootmem(memory_start, memory_end - memory_start);

	/*
	 * Reserve the bootmem bitmap itself as well. We do this in two
	 * steps (first step was init_bootmem()) because this catches
	 * the (very unlikely) case of us accidentally initializing the
	 * bootmem allocator with an invalid RAM area.
	 *
	 * Arguments are start, size
	 */
	pr_debug("reserve_bootmem(%#lx, %#x)\n", memory_start, bootmap_size);
	reserve_bootmem(memory_start, bootmap_size, BOOTMEM_DEFAULT);

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start) {
		reserve_bootmem(virt_to_phys((void *)initrd_start),
				initrd_end - initrd_start, BOOTMEM_DEFAULT);
	}
#endif /* CONFIG_BLK_DEV_INITRD */

	device_tree_init();

	setup_cpuinfo();

	copy_exception_handler(cpuinfo.exception_addr);

#ifdef CONFIG_MMU
	copy_fast_tlb_miss_handler(cpuinfo.fast_tlb_miss_exc_addr);

	/*
	 * Initialize MMU context handling here because data from cpuinfo is
	 * needed for this.
	 */
	mmu_context_init();
#endif

	/*
	 * get kmalloc into gear
	 */
	paging_init();

#if defined(CONFIG_VT) && defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
}
