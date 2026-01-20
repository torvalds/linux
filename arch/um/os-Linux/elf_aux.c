// SPDX-License-Identifier: GPL-2.0
/*
 *  arch/um/kernel/elf_aux.c
 *
 *  Scan the ELF auxiliary vector provided by the host to extract
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
#include "internal.h"
#include <linux/swab.h>

#if __BITS_PER_LONG == 64
typedef Elf64_auxv_t elf_auxv_t;
#else
typedef Elf32_auxv_t elf_auxv_t;
#endif

/* These are initialized very early in boot and never changed */
char * elf_aux_platform;
long elf_aux_hwcap;

__init void scan_elf_aux( char **envp)
{
	elf_auxv_t * auxv;

	while ( *envp++ != NULL) ;

	for ( auxv = (elf_auxv_t *)envp; auxv->a_type != AT_NULL; auxv++) {
		switch ( auxv->a_type ) {
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
		}
	}
}
