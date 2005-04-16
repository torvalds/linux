/*
 * linux/arch/frv/mm/extable.c
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>

extern const struct exception_table_entry __attribute__((aligned(8))) __start___ex_table[];
extern const struct exception_table_entry __attribute__((aligned(8))) __stop___ex_table[];
extern const void __memset_end, __memset_user_error_lr, __memset_user_error_handler;
extern const void __memcpy_end, __memcpy_user_error_lr, __memcpy_user_error_handler;
extern spinlock_t modlist_lock;

/*****************************************************************************/
/*
 *
 */
static inline unsigned long search_one_table(const struct exception_table_entry *first,
					     const struct exception_table_entry *last,
					     unsigned long value)
{
        while (first <= last) {
		const struct exception_table_entry __attribute__((aligned(8))) *mid;
		long diff;

		mid = (last - first) / 2 + first;
		diff = mid->insn - value;
                if (diff == 0)
                        return mid->fixup;
                else if (diff < 0)
                        first = mid + 1;
                else
                        last = mid - 1;
        }
        return 0;
} /* end search_one_table() */

/*****************************************************************************/
/*
 * see if there's a fixup handler available to deal with a kernel fault
 */
unsigned long search_exception_table(unsigned long pc)
{
	unsigned long ret = 0;

	/* determine if the fault lay during a memcpy_user or a memset_user */
	if (__frame->lr == (unsigned long) &__memset_user_error_lr &&
	    (unsigned long) &memset <= pc && pc < (unsigned long) &__memset_end
	    ) {
		/* the fault occurred in a protected memset
		 * - we search for the return address (in LR) instead of the program counter
		 * - it was probably during a clear_user()
		 */
		return (unsigned long) &__memset_user_error_handler;
	}
	else if (__frame->lr == (unsigned long) &__memcpy_user_error_lr &&
		 (unsigned long) &memcpy <= pc && pc < (unsigned long) &__memcpy_end
		 ) {
		/* the fault occurred in a protected memset
		 * - we search for the return address (in LR) instead of the program counter
		 * - it was probably during a copy_to/from_user()
		 */
		return (unsigned long) &__memcpy_user_error_handler;
	}

#ifndef CONFIG_MODULES
	/* there is only the kernel to search.  */
	ret = search_one_table(__start___ex_table, __stop___ex_table - 1, pc);
	return ret;

#else
	/* the kernel is the last "module" -- no need to treat it special */
	unsigned long flags;
	struct module *mp;

	spin_lock_irqsave(&modlist_lock, flags);

	for (mp = module_list; mp != NULL; mp = mp->next) {
		if (mp->ex_table_start == NULL || !(mp->flags & (MOD_RUNNING | MOD_INITIALIZING)))
			continue;
		ret = search_one_table(mp->ex_table_start, mp->ex_table_end - 1, pc);
		if (ret)
			break;
	}

	spin_unlock_irqrestore(&modlist_lock, flags);
	return ret;
#endif
} /* end search_exception_table() */
