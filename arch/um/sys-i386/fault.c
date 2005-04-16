/* 
 * Copyright (C) 2002 - 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <signal.h>
#include "sysdep/ptrace.h"
#include "sysdep/sigcontext.h"

/* These two are from asm-um/uaccess.h and linux/module.h, check them. */
struct exception_table_entry
{
	unsigned long insn;
	unsigned long fixup;
};

const struct exception_table_entry *search_exception_tables(unsigned long add);

/* Compare this to arch/i386/mm/extable.c:fixup_exception() */
int arch_fixup(unsigned long address, void *sc_ptr)
{
	struct sigcontext *sc = sc_ptr;
	const struct exception_table_entry *fixup;

	fixup = search_exception_tables(address);
	if(fixup != 0){
		sc->eip = fixup->fixup;
		return(1);
	}
	return(0);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
