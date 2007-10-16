/*
 * Copyright (C) 2001 Chris Emerson (cemerson@chiark.greenend.org.uk)
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stddef.h>
#include "longjmp.h"

unsigned long __do_user_copy(void *to, const void *from, int n,
			     void **fault_addr, jmp_buf **fault_catcher,
			     void (*op)(void *to, const void *from,
					int n), int *faulted_out)
{
	unsigned long *faddrp = (unsigned long *) fault_addr, ret;

	jmp_buf jbuf;
	*fault_catcher = &jbuf;
	if(UML_SETJMP(&jbuf) == 0){
		(*op)(to, from, n);
		ret = 0;
		*faulted_out = 0;
	}
	else {
		ret = *faddrp;
		*faulted_out = 1;
	}
	*fault_addr = NULL;
	*fault_catcher = NULL;
	return ret;
}

