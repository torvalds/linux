/* 
 * Copyright (C) 2001 Chris Emerson (cemerson@chiark.greenend.org.uk)
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <setjmp.h>
#include <string.h>

/* These are here rather than tt/uaccess.c because skas mode needs them in
 * order to do SIGBUS recovery when a tmpfs mount runs out of room.
 */

unsigned long __do_user_copy(void *to, const void *from, int n,
			     void **fault_addr, void **fault_catcher,
			     void (*op)(void *to, const void *from,
					int n), int *faulted_out)
{
	unsigned long *faddrp = (unsigned long *) fault_addr, ret;

	sigjmp_buf jbuf;
	*fault_catcher = &jbuf;
	if(sigsetjmp(jbuf, 1) == 0){
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

void __do_copy(void *to, const void *from, int n)
{
	memcpy(to, from, n);
}	


int __do_copy_to_user(void *to, const void *from, int n,
		      void **fault_addr, void **fault_catcher)
{
	unsigned long fault;
	int faulted;

	fault = __do_user_copy(to, from, n, fault_addr, fault_catcher,
			       __do_copy, &faulted);
	if(!faulted) return(0);
	else return(n - (fault - (unsigned long) to));
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
