/* 
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <sysdep/ptrace.h>

/* These two are from asm-um/uaccess.h and linux/module.h, check them. */
struct exception_table_entry
{
	unsigned long insn;
	unsigned long fixup;
};

const struct exception_table_entry *search_exception_tables(unsigned long add);

/* Compare this to arch/i386/mm/extable.c:fixup_exception() */
int arch_fixup(unsigned long address, struct uml_pt_regs *regs)
{
	const struct exception_table_entry *fixup;

	fixup = search_exception_tables(address);
	if (fixup != 0) {
		UPT_IP(regs) = fixup->fixup;
		return 1;
	}
	return 0;
}
