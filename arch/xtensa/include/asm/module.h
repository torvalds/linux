/*
 * include/asm-xtensa/module.h
 *
 * This file contains the module code specific to the Xtensa architecture.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_MODULE_H
#define _XTENSA_MODULE_H

struct mod_arch_specific
{
	/* No special elements, yet. */
};

#define MODULE_ARCH_VERMAGIC "xtensa-" __stringify(XCHAL_CORE_ID) " "

#define Elf_Shdr Elf32_Shdr
#define Elf_Sym Elf32_Sym
#define Elf_Ehdr Elf32_Ehdr

#endif	/* _XTENSA_MODULE_H */
