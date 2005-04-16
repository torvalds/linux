/*
 * Copyright (C) 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __UML_UACCESS_H__
#define __UML_UACCESS_H__

extern int __do_copy_to_user(void *to, const void *from, int n,
			     void **fault_addr, void **fault_catcher);
extern unsigned long __do_user_copy(void *to, const void *from, int n,
				    void **fault_addr, void **fault_catcher,
				    void (*op)(void *to, const void *from,
					       int n), int *faulted_out);
void __do_copy(void *to, const void *from, int n);

#endif

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
