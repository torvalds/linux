/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#ifndef __UM_MODULE_X86_64_H
#define __UM_MODULE_X86_64_H

/* UML is simple */
struct mod_arch_specific
{
};

#define Elf_Shdr Elf64_Shdr
#define Elf_Sym Elf64_Sym
#define Elf_Ehdr Elf64_Ehdr

#endif

