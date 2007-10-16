/*
 * Copyright (C) 2001 Chris Emerson (cemerson@chiark.greenend.org.uk)
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

/* These are here rather than tt/uaccess.c because skas mode needs them in
 * order to do SIGBUS recovery when a tmpfs mount runs out of room.
 */

#include <linux/string.h>
#include "os.h"

void __do_copy(void *to, const void *from, int n)
{
	memcpy(to, from, n);
}


int __do_copy_to_user(void *to, const void *from, int n,
		      void **fault_addr, jmp_buf **fault_catcher)
{
	unsigned long fault;
	int faulted;

	fault = __do_user_copy(to, from, n, fault_addr, fault_catcher,
			       __do_copy, &faulted);
	if(!faulted) return(0);
	else return(n - (fault - (unsigned long) to));
}
