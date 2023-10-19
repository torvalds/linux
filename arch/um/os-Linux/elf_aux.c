// SPDX-License-Identifier: GPL-2.0
/*
 *  arch/um/kernel/elf_aux.c
 *
 *  Scan the Elf auxiliary vector provided by the host to extract
 *  information about vsyscall-page, etc.
 *
 *  Copyright (C) 2004 Fujitsu Siemens Computers GmbH
 *  Author: Bodo Stroesser (bodo.stroesser@fujitsu-siemens.com)
 */
#include <elf.h>
#include <stddef.h>
#include <init.h>
#include <elf_user.h>
#include <mem_user.h>

typedef Elf32_auxv_t elf_auxv_t;

/* These are initialized very early in boot and never changed */
char * elf_aux_platform;
extern long elf_aux_hwcap;
unsigned long vsyscall_ehdr;
unsigned long vsyscall_end;
unsigned long __kernel_vsyscall;

__init void scan_elf_aux( char **envp)
{
	long page_size = 0;
	elf_auxv_t * auxv;

	while ( *envp++ != NULL) ;

	for ( auxv = (elf_auxv_t *)envp; auxv->a_type != AT_NULL; auxv++) {
		switch ( auxv->a_type ) {
			case AT_SYSINFO:
				__kernel_vsyscall = auxv->a_un.a_val;
				/* See if the page is under TASK_SIZE */
				if (__kernel_vsyscall < (unsigned long) envp)
					__kernel_vsyscall = 0;
				break;
			case AT_SYSINFO_EHDR:
				vsyscall_ehdr = auxv->a_un.a_val;
				/* See if the page is under TASK_SIZE */
				if (vsyscall_ehdr < (unsigned long) envp)
					vsyscall_ehdr = 0;
				break;
			case AT_HWCAP:
				elf_aux_hwcap = auxv->a_un.a_val;
				break;
			case AT_PLATFORM:
                                /* elf.h removed the pointer elements from
                                 * a_un, so we have to use a_val, which is
                                 * all that's left.
                                 */
				elf_aux_platform =
					(char *) (long) auxv->a_un.a_val;
				break;
			case AT_PAGESZ:
				page_size = auxv->a_un.a_val;
				break;
		}
	}
	if ( ! __kernel_vsyscall || ! vsyscall_ehdr ||
	     ! elf_aux_hwcap || ! elf_aux_platform ||
	     ! page_size || (vsyscall_ehdr % page_size) ) {
		__kernel_vsyscall = 0;
		vsyscall_ehdr = 0;
		elf_aux_hwcap = 0;
		elf_aux_platform = "i586";
	}
	else {
		vsyscall_end = vsyscall_ehdr + page_size;
	}
}
