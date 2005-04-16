/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __SKAS_PROC_MM_H
#define __SKAS_PROC_MM_H

#define MM_MMAP 54
#define MM_MUNMAP 55
#define MM_MPROTECT 56
#define MM_COPY_SEGMENTS 57

struct mm_mmap {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
	unsigned long fd;
	unsigned long offset;
};

struct mm_munmap {
	unsigned long addr;
	unsigned long len;	
};

struct mm_mprotect {
	unsigned long addr;
	unsigned long len;
        unsigned int prot;
};

struct proc_mm_op {
	int op;
	union {
		struct mm_mmap mmap;
		struct mm_munmap munmap;
	        struct mm_mprotect mprotect;
		int copy_segments;
	} u;
};

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
