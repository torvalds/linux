/*
 * Copyright (C) 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __START_H__
#define __START_H__

#include <generated/asm-offsets.h>

/*
 * Stolen from linux/const.h, which can't be directly included since
 * this is used in userspace code, which has no access to the kernel
 * headers.  Changed to be suitable for adding casts to the start,
 * rather than "UL" to the end.
 */

/* Some constant macros are used in both assembler and
 * C code.  Therefore we cannot annotate them always with
 * 'UL' and other type specifiers unilaterally.  We
 * use the following macros to deal with this.
 */

#ifdef __ASSEMBLY__
#define _UML_AC(X, Y)	(Y)
#else
#define __UML_AC(X, Y)	(X(Y))
#define _UML_AC(X, Y)	__UML_AC(X, Y)
#endif

#define STUB_START _UML_AC(, 0x100000)
#define STUB_CODE _UML_AC((unsigned long), STUB_START)
#define STUB_DATA _UML_AC((unsigned long), STUB_CODE + UM_KERN_PAGE_SIZE)
#define STUB_END _UML_AC((unsigned long), STUB_DATA + UM_KERN_PAGE_SIZE)

#ifndef __ASSEMBLY__

#include <sysdep/ptrace.h>

struct cpu_task {
	int pid;
	void *task;
};

extern struct cpu_task cpu_tasks[];

extern unsigned long low_physmem;
extern unsigned long high_physmem;
extern unsigned long uml_physmem;
extern unsigned long uml_reserved;
extern unsigned long end_vm;
extern unsigned long start_vm;
extern unsigned long long highmem;

extern unsigned long _stext, _etext, _sdata, _edata, __bss_start, _end;
extern unsigned long _unprotected_end;
extern unsigned long brk_start;

extern unsigned long host_task_size;

extern int linux_main(int argc, char **argv);

struct siginfo;
extern void (*sig_info[])(int, struct siginfo *si, struct uml_pt_regs *);

#endif

#endif
