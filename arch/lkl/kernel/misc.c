#include <linux/kallsyms.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <asm/ptrace.h>
#include <asm/host_ops.h>

void dump_stack(void)
{
	unsigned long dummy;
	unsigned long *stack = &dummy;
	unsigned long addr;

	pr_info("Call Trace:\n");
	while (((long)stack & (THREAD_SIZE - 1)) != 0) {
		addr = *stack;
		if (__kernel_text_address(addr)) {
			pr_info("%p:  [<%08lx>]", stack, addr);
			print_symbol(KERN_CONT " %s", addr);
			pr_cont("\n");
		}
		stack++;
	}
	pr_info("\n");
}

void show_regs(struct pt_regs *regs)
{
}

#ifdef CONFIG_PROC_FS
static void *cpuinfo_start(struct seq_file *m, loff_t *pos)
{
	return NULL;
}

static void *cpuinfo_next(struct seq_file *m, void *v, loff_t *pos)
{
	return NULL;
}

static void cpuinfo_stop(struct seq_file *m, void *v)
{
}

static int show_cpuinfo(struct seq_file *m, void *v)
{
	return 0;
}

const struct seq_operations cpuinfo_op = {
	.start	= cpuinfo_start,
	.next	= cpuinfo_next,
	.stop	= cpuinfo_stop,
	.show	= show_cpuinfo,
};
#endif
