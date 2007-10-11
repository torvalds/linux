/*
 * linux/arch/x86_64/mm/extable.c
 */

#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <asm/uaccess.h>

/* Simple binary search */
const struct exception_table_entry *
search_extable(const struct exception_table_entry *first,
	       const struct exception_table_entry *last,
	       unsigned long value)
{
	/* Work around a B stepping K8 bug */
	if ((value >> 32) == 0)
		value |= 0xffffffffUL << 32; 

        while (first <= last) {
		const struct exception_table_entry *mid;
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
