#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/init_task.h>
#include <linux/fs.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/desc.h>

#define DOUBLEFAULT_STACKSIZE (1024)
static unsigned long doublefault_stack[DOUBLEFAULT_STACKSIZE];
#define STACK_START (unsigned long)(doublefault_stack+DOUBLEFAULT_STACKSIZE)

#define ptr_ok(x) ((x) > PAGE_OFFSET && (x) < PAGE_OFFSET + MAXMEM)

static void doublefault_fn(void)
{
	struct Xgt_desc_struct gdt_desc = {0, 0};
	unsigned long gdt, tss;

	store_gdt(&gdt_desc);
	gdt = gdt_desc.address;

	printk(KERN_EMERG "PANIC: double fault, gdt at %08lx [%d bytes]\n", gdt, gdt_desc.size);

	if (ptr_ok(gdt)) {
		gdt += GDT_ENTRY_TSS << 3;
		tss = *(u16 *)(gdt+2);
		tss += *(u8 *)(gdt+4) << 16;
		tss += *(u8 *)(gdt+7) << 24;
		printk(KERN_EMERG "double fault, tss at %08lx\n", tss);

		if (ptr_ok(tss)) {
			struct i386_hw_tss *t = (struct i386_hw_tss *)tss;

			printk(KERN_EMERG "eip = %08lx, esp = %08lx\n", t->eip, t->esp);

			printk(KERN_EMERG "eax = %08lx, ebx = %08lx, ecx = %08lx, edx = %08lx\n",
				t->eax, t->ebx, t->ecx, t->edx);
			printk(KERN_EMERG "esi = %08lx, edi = %08lx\n",
				t->esi, t->edi);
		}
	}

	for (;;)
		cpu_relax();
}

struct tss_struct doublefault_tss __cacheline_aligned = {
	.x86_tss = {
		.esp0		= STACK_START,
		.ss0		= __KERNEL_DS,
		.ldt		= 0,
		.io_bitmap_base	= INVALID_IO_BITMAP_OFFSET,

		.eip		= (unsigned long) doublefault_fn,
		/* 0x2 bit is always set */
		.eflags		= X86_EFLAGS_SF | 0x2,
		.esp		= STACK_START,
		.es		= __USER_DS,
		.cs		= __KERNEL_CS,
		.ss		= __KERNEL_DS,
		.ds		= __USER_DS,
		.fs		= __KERNEL_PERCPU,

		.__cr3		= __pa(swapper_pg_dir)
	}
};
