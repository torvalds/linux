/*
 *  linux/arch/arm26/mm/extable.c
 */

#include <linux/module.h>
#include <asm/uaccess.h>

int fixup_exception(struct pt_regs *regs)
{
        const struct exception_table_entry *fixup;

        fixup = search_exception_tables(instruction_pointer(regs));

	/*
	 * The kernel runs in SVC mode - make sure we keep running in SVC mode
	 * by frobbing the PSR appropriately (PSR and PC are in the same reg.
	 * on ARM26)
	 */
        if (fixup)
                regs->ARM_pc = fixup->fixup | PSR_I_BIT | MODE_SVC26;

        return fixup != NULL;
}

