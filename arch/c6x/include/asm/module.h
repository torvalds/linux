/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2010 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  Updated for 2.6.34 by: Mark Salter (msalter@redhat.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#ifndef _ASM_C6X_MODULE_H
#define _ASM_C6X_MODULE_H

#define Elf_Shdr	Elf32_Shdr
#define Elf_Sym		Elf32_Sym
#define Elf_Ehdr	Elf32_Ehdr
#define Elf_Addr	Elf32_Addr
#define Elf_Word	Elf32_Word

/*
 * This file contains the C6x architecture specific module code.
 */
struct mod_arch_specific {
};

struct loaded_sections {
	unsigned int new_vaddr;
	unsigned int loaded;
};

#endif /* _ASM_C6X_MODULE_H */
