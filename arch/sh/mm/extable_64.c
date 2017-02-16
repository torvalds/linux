/*
 * arch/sh/mm/extable_64.c
 *
 * Copyright (C) 2003 Richard Curnow
 * Copyright (C) 2003, 2004  Paul Mundt
 *
 * Cloned from the 2.5 SH version..
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/rwsem.h>
#include <linux/extable.h>
#include <linux/uaccess.h>

extern unsigned long copy_user_memcpy, copy_user_memcpy_end;
extern void __copy_user_fixup(void);

static const struct exception_table_entry __copy_user_fixup_ex = {
	.fixup = (unsigned long)&__copy_user_fixup,
};

/*
 * Some functions that may trap due to a bad user-mode address have too
 * many loads and stores in them to make it at all practical to label
 * each one and put them all in the main exception table.
 *
 * In particular, the fast memcpy routine is like this.  It's fix-up is
 * just to fall back to a slow byte-at-a-time copy, which is handled the
 * conventional way.  So it's functionally OK to just handle any trap
 * occurring in the fast memcpy with that fixup.
 */
static const struct exception_table_entry *check_exception_ranges(unsigned long addr)
{
	if ((addr >= (unsigned long)&copy_user_memcpy) &&
	    (addr <= (unsigned long)&copy_user_memcpy_end))
		return &__copy_user_fixup_ex;

	return NULL;
}

/* Simple binary search */
const struct exception_table_entry *
search_extable(const struct exception_table_entry *first,
		 const struct exception_table_entry *last,
		 unsigned long value)
{
	const struct exception_table_entry *mid;

	mid = check_exception_ranges(value);
	if (mid)
		return mid;

        while (first <= last) {
		long diff;

		mid = (last - first) / 2 + first;
		diff = mid->insn - value;
                if (diff == 0)
                        return mid;
                else if (diff < 0)
                        first = mid+1;
                else
                        last = mid-1;
        }

        return NULL;
}

int fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *fixup;

	fixup = search_exception_tables(regs->pc);
	if (fixup) {
		regs->pc = fixup->fixup;
		return 1;
	}

	return 0;
}
