#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/init_task.h>
#include <linux/fs.h>

#include <linux/uaccess.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/desc.h>

#ifdef CONFIG_X86_32

#define DOUBLEFAULT_STACKSIZE (1024)
static unsigned long doublefault_stack[DOUBLEFAULT_STACKSIZE];
#define STACK_START (unsigned long)(doublefault_stack+DOUBLEFAULT_STACKSIZE)

#define ptr_ok(x) ((x) > PAGE_OFFSET && (x) < PAGE_OFFSET + MAXMEM)

static void doublefault_fn(void)
{
	struct desc_ptr gdt_desc = {0, 0};
	unsigned long gdt, tss;

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

struct tss_struct doublefault_tss __cacheline_aligned = {
	.x86_tss = {
		.sp0		= STACK_START,
		.ss0		= __KERNEL_DS,
		.ldt		= 0,
		.io_bitmap_base	= INVALID_IO_BITMAP_OFFSET,

		.ip		= (unsigned long) doublefault_fn,
		/* 0x2 bit is always set */
		.flags		= X86_EFLAGS_SF | 0x2,
		.sp		= STACK_START,
		.es		= __USER_DS,
		.cs		= __KERNEL_CS,
		.ss		= __KERNEL_DS,
		.ds		= __USER_DS,
		.fs		= __KERNEL_PERCPU,

		.__cr3		= __pa_nodebug(swapper_pg_dir),
	}
};

/* dummy for do_double_fault() call */
void df_debug(struct pt_regs *regs, long error_code) {}

#else /* !CONFIG_X86_32 */

void df_debug(struct pt_regs *regs, long error_code)
{
	pr_emerg("PANIC: double fault, error_code: 0x%lx\n", error_code);
	show_regs(regs);
	panic("Machine halted.");
}
#endif
