#include <linux/extable.h>
#include <asm/uaccess.h>
#include <asm/traps.h>
#include <asm/kdebug.h>

typedef bool (*ex_handler_t)(const struct exception_table_entry *,
			    struct pt_regs *, int);

static inline unsigned long
ex_fixup_addr(const struct exception_table_entry *x)
{
	return (unsigned long)&x->fixup + x->fixup;
}
static inline ex_handler_t
ex_fixup_handler(const struct exception_table_entry *x)
{
	return (ex_handler_t)((unsigned long)&x->handler + x->handler);
}

bool ex_handler_default(const struct exception_table_entry *fixup,
		       struct pt_regs *regs, int trapnr)
{
	regs->ip = ex_fixup_addr(fixup);
	return true;
}
EXPORT_SYMBOL(ex_handler_default);

bool ex_handler_fault(const struct exception_table_entry *fixup,
		     struct pt_regs *regs, int trapnr)
{
	regs->ip = ex_fixup_addr(fixup);
	regs->ax = trapnr;
	return true;
}
EXPORT_SYMBOL_GPL(ex_handler_fault);

bool ex_handler_ext(const struct exception_table_entry *fixup,
		   struct pt_regs *regs, int trapnr)
{
	/* Special hack for uaccess_err */
	current->thread.uaccess_err = 1;
	regs->ip = ex_fixup_addr(fixup);
	return true;
}
EXPORT_SYMBOL(ex_handler_ext);

bool ex_handler_rdmsr_unsafe(const struct exception_table_entry *fixup,
			     struct pt_regs *regs, int trapnr)
{
	if (pr_warn_once("unchecked MSR access error: RDMSR from 0x%x at rIP: 0x%lx (%pF)\n",
			 (unsigned int)regs->cx, regs->ip, (void *)regs->ip))
		show_stack_regs(regs);

	/* Pretend that the read succeeded and returned 0. */
	regs->ip = ex_fixup_addr(fixup);
	regs->ax = 0;
	regs->dx = 0;
	return true;
}
EXPORT_SYMBOL(ex_handler_rdmsr_unsafe);

bool ex_handler_wrmsr_unsafe(const struct exception_table_entry *fixup,
			     struct pt_regs *regs, int trapnr)
{
	if (pr_warn_once("unchecked MSR access error: WRMSR to 0x%x (tried to write 0x%08x%08x) at rIP: 0x%lx (%pF)\n",
			 (unsigned int)regs->cx, (unsigned int)regs->dx,
			 (unsigned int)regs->ax,  regs->ip, (void *)regs->ip))
		show_stack_regs(regs);

	/* Pretend that the write succeeded. */
	regs->ip = ex_fixup_addr(fixup);
	return true;
}
EXPORT_SYMBOL(ex_handler_wrmsr_unsafe);

bool ex_handler_clear_fs(const struct exception_table_entry *fixup,
			 struct pt_regs *regs, int trapnr)
{
	if (static_cpu_has(X86_BUG_NULL_SEG))
		asm volatile ("mov %0, %%fs" : : "rm" (__USER_DS));
	asm volatile ("mov %0, %%fs" : : "rm" (0));
	return ex_handler_default(fixup, regs, trapnr);
}
EXPORT_SYMBOL(ex_handler_clear_fs);

bool ex_has_fault_handler(unsigned long ip)
{
	const struct exception_table_entry *e;
	ex_handler_t handler;

	e = search_exception_tables(ip);
	if (!e)
		return false;
	handler = ex_fixup_handler(e);

	return handler == ex_handler_fault;
}

int fixup_exception(struct pt_regs *regs, int trapnr)
{
	const struct exception_table_entry *e;
	ex_handler_t handler;

#ifdef CONFIG_PNPBIOS
	if (unlikely(SEGMENT_IS_PNP_CODE(regs->cs))) {
		extern u32 pnp_bios_fault_eip, pnp_bios_fault_esp;
		extern u32 pnp_bios_is_utter_crap;
		pnp_bios_is_utter_crap = 1;
		printk(KERN_CRIT "PNPBIOS fault.. attempting recovery.\n");
		__asm__ volatile(
			"movl %0, %%esp\n\t"
			"jmp *%1\n\t"
			: : "g" (pnp_bios_fault_esp), "g" (pnp_bios_fault_eip));
		panic("do_trap: can't hit this");
	}
#endif

	e = search_exception_tables(regs->ip);
	if (!e)
		return 0;

	handler = ex_fixup_handler(e);
	return handler(e, regs, trapnr);
}

extern unsigned int early_recursion_flag;

/* Restricted version used during very early boot */
void __init early_fixup_exception(struct pt_regs *regs, int trapnr)
{
	/* Ignore early NMIs. */
	if (trapnr == X86_TRAP_NMI)
		return;

	if (early_recursion_flag > 2)
		goto halt_loop;

	if (regs->cs != __KERNEL_CS)
		goto fail;

	/*
	 * The full exception fixup machinery is available as soon as
	 * the early IDT is loaded.  This means that it is the
	 * responsibility of extable users to either function correctly
	 * when handlers are invoked early or to simply avoid causing
	 * exceptions before they're ready to handle them.
	 *
	 * This is better than filtering which handlers can be used,
	 * because refusing to call a handler here is guaranteed to
	 * result in a hard-to-debug panic.
	 *
	 * Keep in mind that not all vectors actually get here.  Early
	 * fage faults, for example, are special.
	 */
	if (fixup_exception(regs, trapnr))
		return;

fail:
	early_printk("PANIC: early exception 0x%02x IP %lx:%lx error %lx cr2 0x%lx\n",
		     (unsigned)trapnr, (unsigned long)regs->cs, regs->ip,
		     regs->orig_ax, read_cr2());

	show_regs(regs);

halt_loop:
	while (true)
		halt();
}
