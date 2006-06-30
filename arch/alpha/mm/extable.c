/*
 * linux/arch/alpha/mm/extable.c
 */

#include <linux/module.h>
#include <asm/uaccess.h>

void sort_extable(struct exception_table_entry *start,
		  struct exception_table_entry *finish)
{
}

const struct exception_table_entry *
search_extable(const struct exception_table_entry *first,
	       const struct exception_table_entry *last,
	       unsigned long value)
{
        while (first <= last) {
		const struct exception_table_entry *mid;
		unsigned long mid_value;

		mid = (last - first) / 2 + first;
		mid_value = (unsigned long)&mid->insn + mid->insn;
                if (mid_value == value)
                        return mid;
                else if (mid_value < value)
                        first = mid+1;
                else
                        last = mid-1;
        }

        return NULL;
}
