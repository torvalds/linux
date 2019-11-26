// SPDX-License-Identifier: GPL-2.0
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/init_task.h>
#include <linux/fs.h>

#include <linux/uaccess.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/desc.h>

#define ptr_ok(x) ((x) > PAGE_OFFSET && (x) < PAGE_OFFSET + MAXMEM)

static void doublefault_fn(void)
{
	struct desc_ptr gdt_desc = {0, 0};
	unsigned long gdt, tss;

	BUILD_BUG_ON(sizeof(struct doublefault_stack) != PAGE_SIZE);

	native_store_gdt(&gdt_desc);
	gdt = gdt_desc.address;

	printk(KERN_EMERG "PANIC: double fault, gdt at %08lx [%d bytes]\n", gdt, gdt_desc.size);

	if (ptr_ok(gdt)) {
		gdt += GDT_ENTRY_TSS << 3;
		tss = get_desc_base((struct desc_struct *)gdt);
		printk(KERN_EMERG "double fault, tss at %08lx\n", tss);

		if (ptr_ok(tss)) {
			struct x86_hw_tss *t = (struct x86_hw_tss *)tss;

			printk(KERN_EMERG "eip = %08lx, esp = %08lx\n",
			       t->ip, t->sp);

			printk(KERN_EMERG "eax = %08lx, ebx = %08lx, ecx = %08lx, edx = %08lx\n",
				t->ax, t->bx, t->cx, t->dx);
			printk(KERN_EMERG "esi = %08lx, edi = %08lx\n",
				t->si, t->di);
		}
	}

	for (;;)
		cpu_relax();
}

DEFINE_PER_CPU_PAGE_ALIGNED(struct doublefault_stack, doublefault_stack) = {
	.tss = {
                /*
                 * No sp0 or ss0 -- we never run CPL != 0 with this TSS
                 * active.  sp is filled in later.
                 */
		.ldt		= 0,
	.io_bitmap_base	= IO_BITMAP_OFFSET_INVALID,

		.ip		= (unsigned long) doublefault_fn,
		/* 0x2 bit is always set */
		.flags		= X86_EFLAGS_SF | 0x2,
		.es		= __USER_DS,
		.cs		= __KERNEL_CS,
		.ss		= __KERNEL_DS,
		.ds		= __USER_DS,
		.fs		= __KERNEL_PERCPU,
#ifndef CONFIG_X86_32_LAZY_GS
		.gs		= __KERNEL_STACK_CANARY,
#endif

		.__cr3		= __pa_nodebug(swapper_pg_dir),
	},
};

void doublefault_init_cpu_tss(void)
{
	unsigned int cpu = smp_processor_id();
	struct cpu_entry_area *cea = get_cpu_entry_area(cpu);

	/*
	 * The linker isn't smart enough to initialize percpu variables that
	 * point to other places in percpu space.
	 */
        this_cpu_write(doublefault_stack.tss.sp,
                       (unsigned long)&cea->doublefault_stack.stack +
                       sizeof(doublefault_stack.stack));

	/* Set up doublefault TSS pointer in the GDT */
	__set_tss_desc(cpu, GDT_ENTRY_DOUBLEFAULT_TSS,
		       &get_cpu_entry_area(cpu)->doublefault_stack.tss);

}
