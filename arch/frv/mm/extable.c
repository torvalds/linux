/*
 * linux/arch/frv/mm/extable.c
 */

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
	const struct exception_table_entry *extab;

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

	if (__frame->lr == (unsigned long) &__memcpy_user_error_lr &&
	    (unsigned long) &memcpy <= pc && pc < (unsigned long) &__memcpy_end
	    ) {
		/* the fault occurred in a protected memset
		 * - we search for the return address (in LR) instead of the program counter
		 * - it was probably during a copy_to/from_user()
		 */
		return (unsigned long) &__memcpy_user_error_handler;
	}

	extab = search_exception_tables(pc);
	if (extab)
		return extab->fixup;

	return 0;

} /* end search_exception_table() */
