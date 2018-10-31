/*
 * linux/arch/unicore32/kernel/setup.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/utsname.h>
#include <linux/initrd.h>
#include <linux/console.h>
#include <linux/memblock.h>
#include <linux/seq_file.h>
#include <linux/screen_info.h>
#include <linux/init.h>
#include <linux/root_dev.h>
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/elf.h>
#include <linux/io.h>

#include <asm/cputype.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/traps.h>
#include <asm/memblock.h>

#include "setup.h"

#ifndef MEM_SIZE
#define MEM_SIZE	(16*1024*1024)
#endif

struct stack {
	u32 irq[3];
	u32 abt[3];
	u32 und[3];
} ____cacheline_aligned;

static struct stack stacks[NR_CPUS];

#ifdef CONFIG_VGA_CONSOLE
struct screen_info screen_info;
#endif

char elf_platform[ELF_PLATFORM_SIZE];
EXPORT_SYMBOL(elf_platform);

static char __initdata cmd_line[COMMAND_LINE_SIZE];

static char default_command_line[COMMAND_LINE_SIZE] __initdata = CONFIG_CMDLINE;

/*
 * Standard memory resources
 */
static struct resource mem_res[] = {
	{
		.name = "Kernel code",
		.start = 0,
		.end = 0,
		.flags = IORESOURCE_SYSTEM_RAM
	},
	{
		.name = "Kernel data",
		.start = 0,
		.end = 0,
		.flags = IORESOURCE_SYSTEM_RAM
	}
};

#define kernel_code mem_res[0]
#define kernel_data mem_res[1]

/*
 * These functions re-use the assembly code in head.S, which
 * already provide the required functionality.
 */
static void __init setup_processor(void)
{
	printk(KERN_DEFAULT "CPU: UniCore-II [%08x] revision %d, cr=%08lx\n",
	       uc32_cpuid, (int)(uc32_cpuid >> 16) & 15, cr_alignment);

	sprintf(init_utsname()->machine, "puv3");
	sprintf(elf_platform, "ucv2");
}

/*
 * cpu_init - initialise one CPU.
 *
 * cpu_init sets up the per-CPU stacks.
 */
void cpu_init(void)
{
	unsigned int cpu = smp_processor_id();
	struct stack *stk = &stacks[cpu];

	/*
	 * setup stacks for re-entrant exception handlers
	 */
	__asm__ (
	"mov.a	asr, %1\n\t"
	"add	sp, %0, %2\n\t"
	"mov.a	asr, %3\n\t"
	"add	sp, %0, %4\n\t"
	"mov.a	asr, %5\n\t"
	"add	sp, %0, %6\n\t"
	"mov.a	asr, %7"
	    :
	    : "r" (stk),
	      "r" (PSR_R_BIT | PSR_I_BIT | INTR_MODE),
	      "I" (offsetof(struct stack, irq[0])),
	      "r" (PSR_R_BIT | PSR_I_BIT | ABRT_MODE),
	      "I" (offsetof(struct stack, abt[0])),
	      "r" (PSR_R_BIT | PSR_I_BIT | EXTN_MODE),
	      "I" (offsetof(struct stack, und[0])),
	      "r" (PSR_R_BIT | PSR_I_BIT | PRIV_MODE)
	: "r30", "cc");
}

static int __init uc32_add_memory(unsigned long start, unsigned long size)
{
	struct membank *bank = &meminfo.bank[meminfo.nr_banks];

	if (meminfo.nr_banks >= NR_BANKS) {
		printk(KERN_CRIT "NR_BANKS too low, "
			"ignoring memory at %#lx\n", start);
		return -EINVAL;
	}

	/*
	 * Ensure that start/size are aligned to a page boundary.
	 * Size is appropriately rounded down, start is rounded up.
	 */
	size -= start & ~PAGE_MASK;

	bank->start = PAGE_ALIGN(start);
	bank->size  = size & PAGE_MASK;

	/*
	 * Check whether this memory region has non-zero size or
	 * invalid node number.
	 */
	if (bank->size == 0)
		return -EINVAL;

	meminfo.nr_banks++;
	return 0;
}

/*
 * Pick out the memory size.  We look for mem=size@start,
 * where start and size are "size[KkMm]"
 */
static int __init early_mem(char *p)
{
	static int usermem __initdata = 1;
	unsigned long size, start;
	char *endp;

	/*
	 * If the user specifies memory size, we
	 * blow away any automatically generated
	 * size.
	 */
	if (usermem) {
		usermem = 0;
		meminfo.nr_banks = 0;
	}

	start = PHYS_OFFSET;
	size  = memparse(p, &endp);
	if (*endp == '@')
		start = memparse(endp + 1, NULL);

	uc32_add_memory(start, size);

	return 0;
}
early_param("mem", early_mem);

static void __init
request_standard_resources(struct meminfo *mi)
{
	struct resource *res;
	int i;

	kernel_code.start   = virt_to_phys(_stext);
	kernel_code.end     = virt_to_phys(_etext - 1);
	kernel_data.start   = virt_to_phys(_sdata);
	kernel_data.end     = virt_to_phys(_end - 1);

	for (i = 0; i < mi->nr_banks; i++) {
		if (mi->bank[i].size == 0)
			continue;

		res = memblock_alloc_low(sizeof(*res), SMP_CACHE_BYTES);
		res->name  = "System RAM";
		res->start = mi->bank[i].start;
		res->end   = mi->bank[i].start + mi->bank[i].size - 1;
		res->flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;

		request_resource(&iomem_resource, res);

		if (kernel_code.start >= res->start &&
		    kernel_code.end <= res->end)
			request_resource(res, &kernel_code);
		if (kernel_data.start >= res->start &&
		    kernel_data.end <= res->end)
			request_resource(res, &kernel_data);
	}
}

static void (*init_machine)(void) __initdata;

static int __init customize_machine(void)
{
	/* customizes platform devices, or adds new ones */
	if (init_machine)
		init_machine();
	return 0;
}
arch_initcall(customize_machine);

void __init setup_arch(char **cmdline_p)
{
	char *from = default_command_line;

	setup_processor();

	init_mm.start_code = (unsigned long) _stext;
	init_mm.end_code   = (unsigned long) _etext;
	init_mm.end_data   = (unsigned long) _edata;
	init_mm.brk	   = (unsigned long) _end;

	/* parse_early_param needs a boot_command_line */
	strlcpy(boot_command_line, from, COMMAND_LINE_SIZE);

	/* populate cmd_line too for later use, preserving boot_command_line */
	strlcpy(cmd_line, boot_command_line, COMMAND_LINE_SIZE);
	*cmdline_p = cmd_line;

	parse_early_param();

	uc32_memblock_init(&meminfo);

	paging_init();
	request_standard_resources(&meminfo);

	cpu_init();

	/*
	 * Set up various architecture-specific pointers
	 */
	init_machine = puv3_core_init;

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif
	early_trap_init();
}

static struct cpu cpuinfo_unicore;

static int __init topology_init(void)
{
	int i;

	for_each_possible_cpu(i)
		register_cpu(&cpuinfo_unicore, i);

	return 0;
}
subsys_initcall(topology_init);

#ifdef CONFIG_HAVE_PROC_CPU
static int __init proc_cpu_init(void)
{
	struct proc_dir_entry *res;

	res = proc_mkdir("cpu", NULL);
	if (!res)
		return -ENOMEM;
	return 0;
}
fs_initcall(proc_cpu_init);
#endif

static int c_show(struct seq_file *m, void *v)
{
	seq_printf(m, "Processor\t: UniCore-II rev %d (%s)\n",
		   (int)(uc32_cpuid >> 16) & 15, elf_platform);

	seq_printf(m, "BogoMIPS\t: %lu.%02lu\n",
		   loops_per_jiffy / (500000/HZ),
		   (loops_per_jiffy / (5000/HZ)) % 100);

	/* dump out the processor features */
	seq_puts(m, "Features\t: CMOV UC-F64");

	seq_printf(m, "\nCPU implementer\t: 0x%02x\n", uc32_cpuid >> 24);
	seq_printf(m, "CPU architecture: 2\n");
	seq_printf(m, "CPU revision\t: %d\n", (uc32_cpuid >> 16) & 15);

	seq_printf(m, "Cache type\t: write-back\n"
			"Cache clean\t: cp0 c5 ops\n"
			"Cache lockdown\t: not support\n"
			"Cache format\t: Harvard\n");

	seq_puts(m, "\n");

	seq_printf(m, "Hardware\t: PKUnity v3\n");

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < 1 ? (void *)1 : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}

static void c_stop(struct seq_file *m, void *v)
{
}

const struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= c_show
};
