/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __SKAS_PTRACE_H
#define __SKAS_PTRACE_H

struct ptrace_faultinfo {
	int is_write;
	unsigned long addr;
};

struct ptrace_ldt {
	int func;
  	void *ptr;
	unsigned long bytecount;
};

#define PTRACE_FAULTINFO 52
#define PTRACE_SIGPENDING 53
#define PTRACE_LDT 54
#define PTRACE_SWITCH_MM 55

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
