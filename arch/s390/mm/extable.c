#include <linux/module.h>
#include <linux/sort.h>
#include <asm/uaccess.h>

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
	const struct exception_table_entry *mid;
	unsigned long addr;

	while (first <= last) {
		mid = ((last - first) >> 1) + first;
		addr = extable_insn(mid);
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

	/* This compare is only valid after normalization. */
	return x->insn - y->insn;
}

void sort_extable(struct exception_table_entry *start,
		  struct exception_table_entry *finish)
{
	struct exception_table_entry *p;
	int i;

	/* Normalize entries to being relative to the start of the section */
	for (p = start, i = 0; p < finish; p++, i += 8) {
		p->insn += i;
		p->fixup += i + 4;
	}
	sort(start, finish - start, sizeof(*start), cmp_ex, NULL);
	/* Denormalize all entries */
	for (p = start, i = 0; p < finish; p++, i += 8) {
		p->insn -= i;
		p->fixup -= i + 4;
	}
}

#ifdef CONFIG_MODULES
/*
 * If the exception table is sorted, any referring to the module init
 * will be at the beginning or the end.
 */
void trim_init_extable(struct module *m)
{
	/* Trim the beginning */
	while (m->num_exentries &&
	       within_module_init(extable_insn(&m->extable[0]), m)) {
		m->extable++;
		m->num_exentries--;
	}
	/* Trim the end */
	while (m->num_exentries &&
	       within_module_init(extable_insn(&m->extable[m->num_exentries-1]), m))
		m->num_exentries--;
}
#endif /* CONFIG_MODULES */
