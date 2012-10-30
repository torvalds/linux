extern long sys_clone(unsigned long clone_flags, unsigned long newsp,
	       void __user *parent_tid, void __user *child_tid);
#ifdef __i386__
#include "syscalls_32.h"
#else
#include "syscalls_64.h"
#endif
