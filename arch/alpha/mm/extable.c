/*
 * linux/arch/alpha/mm/extable.c
 */

#include <linux/module.h>
#include <linux/sort.h>
#include <asm/uaccess.h>

static inline unsigned long ex_to_addr(const struct exception_table_entry *x)
{
	return (unsigned long)&x->insn + x->insn;
}

static void swap_ex(void *a, void *b, int size)
{
	struct exception_table_entry *ex_a = a, *ex_b = b;
	unsigned long addr_a = ex_to_addr(ex_a), addr_b = ex_to_addr(ex_b);
	unsigned int t = ex_a->fixup.unit;

	ex_a->fixup.unit = ex_b->fixup.unit;
	ex_b->fixup.unit = t;
	ex_a->insn = (int)(addr_b - (unsigned long)&ex_a->insn);
	ex_b->insn = (int)(addr_a - (unsigned long)&ex_b->insn);
}

/*
 * The exception table needs to be sorted so that the binary
 * search that we use to find entries in it works properly.
 * This is used both for the kernel exception table and for
 * the exception tables of modules that get loaded.
 */
static int cmp_ex(const void *a, const void *b)
{
	const struct exception_table_entry *x = a, *y = b;

	/* avoid overflow */
	if (ex_to_addr(x) > ex_to_addr(y))
		return 1;
	if (ex_to_addr(x) < ex_to_addr(y))
		return -1;
	return 0;
}

void sort_extable(struct exception_table_entry *start,
		  struct exception_table_entry *finish)
{
	sort(start, finish - start, sizeof(struct exception_table_entry),
	     cmp_ex, swap_ex);
}

#ifdef CONFIG_MODULES
/*
 * Any entry referring to the module init will be at the beginning or
 * the end.
 */
void trim_init_extable(struct module *m)
{
	/*trim the beginning*/
	while (m->num_exentries &&
	       within_module_init(ex_to_addr(&m->extable[0]), m)) {
		m->extable++;
		m->num_exentries--;
	}
	/*trim the end*/
	while (m->num_exentries &&
	       within_module_init(ex_to_addr(&m->extable[m->num_exentries-1]),
				  m))
		m->num_exentries--;
}
#endif /* CONFIG_MODULES */

const struct exception_table_entry *
search_extable(const struct exception_table_entry *first,
	       const struct exception_table_entry *last,
	       unsigned long value)
{
        while (first <= last) {
		const struct exception_table_entry *mid;
		unsigned long mid_value;

		mid = (last - first) / 2 + first;
		mid_value = ex_to_addr(mid);
                if (mid_value == value)
                        return mid;
                else if (mid_value < value)
                        first = mid+1;
                else
                        last = mid-1;
        }

        return NULL;
}
