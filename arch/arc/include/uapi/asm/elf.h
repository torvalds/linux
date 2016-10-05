/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _UAPI__ASM_ARC_ELF_H
#define _UAPI__ASM_ARC_ELF_H

#include <asm/ptrace.h>		/* for user_regs_struct */

/* Machine specific ELF Hdr flags */
#define EF_ARC_OSABI_MSK	0x00000f00

#define EF_ARC_OSABI_V3		0x00000300   /* v3 (no legacy syscalls) */
#define EF_ARC_OSABI_V4		0x00000400   /* v4 (64bit data any reg align) */

#if __GNUC__ < 6
#define EF_ARC_OSABI_CURRENT	EF_ARC_OSABI_V3
#else
#define EF_ARC_OSABI_CURRENT	EF_ARC_OSABI_V4
#endif

typedef unsigned long elf_greg_t;
typedef unsigned long elf_fpregset_t;

#define ELF_NGREG	(sizeof(struct user_regs_struct) / sizeof(elf_greg_t))

typedef elf_greg_t elf_gregset_t[ELF_NGREG];

#endif
