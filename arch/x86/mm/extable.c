#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/sort.h>
#include <asm/uaccess.h>

typedef bool (*ex_handler_t)(const struct exception_table_entry *,
			    struct pt_regs *, int);

static inline unsigned long
ex_insn_addr(const struct exception_table_entry *x)
{
	return (unsigned long)&x->insn + x->insn;
}
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

/* Restricted version used during very early boot */
int __init early_fixup_exception(unsigned long *ip)
{
	const struct exception_table_entry *e;
	unsigned long new_ip;
	ex_handler_t handler;

	e = search_exception_tables(*ip);
	if (!e)
		return 0;

	new_ip  = ex_fixup_addr(e);
	handler = ex_fixup_handler(e);

	/* special handling not supported during early boot */
	if (handler != ex_handler_default)
		return 0;

	*ip = new_ip;
	return 1;
}

/*
 * Search one exception table for an entry corresponding to the
 * given instruction address, and return the address of the entry,
 * or NULL if none is found.
 * We use a binary search, and thus we assume that the table is
 * already sorted.
 */
const struct exception_table_entry *
search_extable(const struct exception_table_entry *first,
	       const struct exception_table_entry *last,
	       unsigned long value)
{
	while (first <= last) {
		const struct exception_table_entry *mid;
		unsigned long addr;

		mid = ((last - first) >> 1) + first;
		addr = ex_insn_addr(mid);
		if (addr < value)
			first = mid + 1;
		else if (addr > value)
			last = mid - 1;
		else
			return mid;
        }
        return NULL;
}

/*
 * The exception table needs to be sorted so that the binary
 * search that we use to find entries in it works properly.
 * This is used both for the kernel exception table and for
 * the exception tables of modules that get loaded.
 *
 */
static int cmp_ex(const void *a, const void *b)
{
	const struct exception_table_entry *x = a, *y = b;

	/*
	 * This value will always end up fittin in an int, because on
	 * both i386 and x86-64 the kernel symbol-reachable address
	 * space is < 2 GiB.
	 *
	 * This compare is only valid after normalization.
	 */
	return x->insn - y->insn;
}

void sort_extable(struct exception_table_entry *start,
		  struct exception_table_entry *finish)
{
	struct exception_table_entry *p;
	int i;

	/* Convert all entries to being relative to the start of the section */
	i = 0;
	for (p = start; p < finish; p++) {
		p->insn += i;
		i += 4;
		p->fixup += i;
		i += 4;
		p->handler += i;
		i += 4;
	}

	sort(start, finish - start, sizeof(struct exception_table_entry),
	     cmp_ex, NULL);

	/* Denormalize all entries */
	i = 0;
	for (p = start; p < finish; p++) {
		p->insn -= i;
		i += 4;
		p->fixup -= i;
		i += 4;
		p->handler -= i;
		i += 4;
	}
}

#ifdef CONFIG_MODULES
/*
 * If the exception table is sorted, any referring to the module init
 * will be at the beginning or the end.
 */
void trim_init_extable(struct module *m)
{
	/*trim the beginning*/
	while (m->num_exentries &&
	       within_module_init(ex_insn_addr(&m->extable[0]), m)) {
		m->extable++;
		m->num_exentries--;
	}
	/*trim the end*/
	while (m->num_exentries &&
	       within_module_init(ex_insn_addr(&m->extable[m->num_exentries-1]), m))
		m->num_exentries--;
}
#endif /* CONFIG_MODULES */
