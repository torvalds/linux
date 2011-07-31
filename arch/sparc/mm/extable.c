/*
 * linux/arch/sparc/mm/extable.c
 */

#include <linux/module.h>
#include <asm/uaccess.h>

void sort_extable(struct exception_table_entry *start,
		  struct exception_table_entry *finish)
{
}

/* Caller knows they are in a range if ret->fixup == 0 */
const struct exception_table_entry *
search_extable(const struct exception_table_entry *start,
	       const struct exception_table_entry *last,
	       unsigned long value)
{
	const struct exception_table_entry *walk;

	/* Single insn entries are encoded as:
	 *	word 1:	insn address
	 *	word 2:	fixup code address
	 *
	 * Range entries are encoded as:
	 *	word 1: first insn address
	 *	word 2: 0
	 *	word 3: last insn address + 4 bytes
	 *	word 4: fixup code address
	 *
	 * Deleted entries are encoded as:
	 *	word 1: unused
	 *	word 2: -1
	 *
	 * See asm/uaccess.h for more details.
	 */

	/* 1. Try to find an exact match. */
	for (walk = start; walk <= last; walk++) {
		if (walk->fixup == 0) {
			/* A range entry, skip both parts. */
			walk++;
			continue;
		}

		/* A deleted entry; see trim_init_extable */
		if (walk->fixup == -1)
			continue;

		if (walk->insn == value)
			return walk;
	}

	/* 2. Try to find a range match. */
	for (walk = start; walk <= (last - 1); walk++) {
		if (walk->fixup)
			continue;

		if (walk[0].insn <= value && walk[1].insn > value)
			return walk;

		walk++;
	}

        return NULL;
}

#ifdef CONFIG_MODULES
/* We could memmove them around; easier to mark the trimmed ones. */
void trim_init_extable(struct module *m)
{
	unsigned int i;
	bool range;

	for (i = 0; i < m->num_exentries; i += range ? 2 : 1) {
		range = m->extable[i].fixup == 0;

		if (within_module_init(m->extable[i].insn, m)) {
			m->extable[i].fixup = -1;
			if (range)
				m->extable[i+1].fixup = -1;
		}
		if (range)
			i++;
	}
}
#endif /* CONFIG_MODULES */

/* Special extable search, which handles ranges.  Returns fixup */
unsigned long search_extables_range(unsigned long addr, unsigned long *g2)
{
	const struct exception_table_entry *entry;

	entry = search_exception_tables(addr);
	if (!entry)
		return 0;

	/* Inside range?  Fix g2 and return correct fixup */
	if (!entry->fixup) {
		*g2 = (addr - entry->insn) / 4;
		return (entry + 1)->fixup;
	}

	return entry->fixup;
}
