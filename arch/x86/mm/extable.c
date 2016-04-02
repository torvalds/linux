#include <linux/module.h>
#include <asm/uaccess.h>
#include <asm/traps.h>

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
	current_thread_info()->uaccess_err = 1;
	regs->ip = ex_fixup_addr(fixup);
	return true;
}
EXPORT_SYMBOL(ex_handler_ext);

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
