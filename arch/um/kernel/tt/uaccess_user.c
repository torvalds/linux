/* 
 * Copyright (C) 2001 Chris Emerson (cemerson@chiark.greenend.org.uk)
 * Copyright (C) 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <setjmp.h>
#include <string.h>
#include "user_util.h"
#include "uml_uaccess.h"
#include "task.h"
#include "kern_util.h"

int __do_copy_from_user(void *to, const void *from, int n,
			void **fault_addr, void **fault_catcher)
{
	struct tt_regs save = TASK_REGS(get_current())->tt;
	unsigned long fault;
	int faulted;

	fault = __do_user_copy(to, from, n, fault_addr, fault_catcher,
			       __do_copy, &faulted);
	TASK_REGS(get_current())->tt = save;

	if(!faulted)
		return 0;
	else if (fault)
		return n - (fault - (unsigned long) from);
	else
		/* In case of a general protection fault, we don't have the
		 * fault address, so NULL is used instead. Pretend we didn't
		 * copy anything. */
		return n;
}

static void __do_strncpy(void *dst, const void *src, int count)
{
	strncpy(dst, src, count);
}	

int __do_strncpy_from_user(char *dst, const char *src, unsigned long count,
			   void **fault_addr, void **fault_catcher)
{
	struct tt_regs save = TASK_REGS(get_current())->tt;
	unsigned long fault;
	int faulted;

	fault = __do_user_copy(dst, src, count, fault_addr, fault_catcher,
			       __do_strncpy, &faulted);
	TASK_REGS(get_current())->tt = save;

	if(!faulted) return(strlen(dst));
	else return(-1);
}

static void __do_clear(void *to, const void *from, int n)
{
	memset(to, 0, n);
}	

int __do_clear_user(void *mem, unsigned long len,
		    void **fault_addr, void **fault_catcher)
{
	struct tt_regs save = TASK_REGS(get_current())->tt;
	unsigned long fault;
	int faulted;

	fault = __do_user_copy(mem, NULL, len, fault_addr, fault_catcher,
			       __do_clear, &faulted);
	TASK_REGS(get_current())->tt = save;

	if(!faulted) return(0);
	else return(len - (fault - (unsigned long) mem));
}

int __do_strnlen_user(const char *str, unsigned long n,
		      void **fault_addr, void **fault_catcher)
{
	struct tt_regs save = TASK_REGS(get_current())->tt;
	int ret;
	unsigned long *faddrp = (unsigned long *)fault_addr;
	sigjmp_buf jbuf;

	*fault_catcher = &jbuf;
	if(sigsetjmp(jbuf, 1) == 0)
		ret = strlen(str) + 1;
	else ret = *faddrp - (unsigned long) str;

	*fault_addr = NULL;
	*fault_catcher = NULL;

	TASK_REGS(get_current())->tt = save;
	return ret;
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
