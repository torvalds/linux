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
#include "init.h"
#include "elf_user.h"

#if ELF_CLASS == ELFCLASS32
typedef Elf32_auxv_t elf_auxv_t;
#else
typedef Elf64_auxv_t elf_auxv_t;
#endif

char * elf_aux_platform;
long elf_aux_hwcap;

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
				break;
			case AT_SYSINFO_EHDR:
				vsyscall_ehdr = auxv->a_un.a_val;
				break;
			case AT_HWCAP:
				elf_aux_hwcap = auxv->a_un.a_val;
				break;
			case AT_PLATFORM:
				elf_aux_platform = auxv->a_un.a_ptr;
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
