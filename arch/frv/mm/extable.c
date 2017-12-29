// SPDX-License-Identifier: GPL-2.0
/*
 * linux/arch/frv/mm/extable.c
 */

#include <linux/extable.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>

extern const void __memset_end, __memset_user_error_lr, __memset_user_error_handler;
extern const void __memcpy_end, __memcpy_user_error_lr, __memcpy_user_error_handler;
extern spinlock_t modlist_lock;

int fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *extab;
	unsigned long pc = regs->pc;

	/* determine if the fault lay during a memcpy_user or a memset_user */
	if (regs->lr == (unsigned long) &__memset_user_error_lr &&
	    (unsigned long) &memset <= pc && pc < (unsigned long) &__memset_end
	    ) {
		/* the fault occurred in a protected memset
		 * - we search for the return address (in LR) instead of the program counter
		 * - it was probably during a clear_user()
		 */
		regs->pc = (unsigned long) &__memset_user_error_handler;
		return 1;
	}

	if (regs->lr == (unsigned long) &__memcpy_user_error_lr &&
	    (unsigned long) &memcpy <= pc && pc < (unsigned long) &__memcpy_end
	    ) {
		/* the fault occurred in a protected memset
		 * - we search for the return address (in LR) instead of the program counter
		 * - it was probably during a copy_to/from_user()
		 */
		regs->pc = (unsigned long) &__memcpy_user_error_handler;
		return 1;
	}

	extab = search_exception_tables(pc);
	if (extab) {
		regs->pc = extab->fixup;
		return 1;
	}

	return 0;
}
