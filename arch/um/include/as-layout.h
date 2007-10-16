/*
 * Copyright (C) 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __START_H__
#define __START_H__

#include "uml-config.h"
#include "kern_constants.h"

/*
 * Assembly doesn't want any casting, but C does, so define these
 * without casts here, and define new symbols with casts inside the C
 * section.
 */
#define ASM_STUB_CODE (UML_CONFIG_TOP_ADDR - 2 * UM_KERN_PAGE_SIZE)
#define ASM_STUB_DATA (UML_CONFIG_TOP_ADDR - UM_KERN_PAGE_SIZE)
#define ASM_STUB_START ASM_STUB_CODE

/*
 * This file is included by the assembly stubs, which just want the
 * definitions above.
 */
#ifndef __ASSEMBLY__

#define STUB_CODE ((unsigned long) ASM_STUB_CODE)
#define STUB_DATA ((unsigned long) ASM_STUB_DATA)
#define STUB_START ((unsigned long) ASM_STUB_START)

#include "sysdep/ptrace.h"

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

extern int linux_main(int argc, char **argv);

extern void (*sig_info[])(int, struct uml_pt_regs *);

#endif

#endif
