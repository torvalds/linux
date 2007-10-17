/*
 * binfmt_elf32.c: Support 32-bit PPC ELF binaries on Power3 and followons.
 * based on the SPARC64 version.
 * Copyright (C) 1995, 1996, 1997, 1998 David S. Miller	(davem@redhat.com)
 * Copyright (C) 1995, 1996, 1997, 1998 Jakub Jelinek	(jj@ultra.linux.cz)
 *
 * Copyright (C) 2000,2001 Ken Aaker (kdaaker@rchland.vnet.ibm.com), IBM Corp
 * Copyright (C) 2001 Anton Blanchard (anton@au.ibm.com), IBM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/processor.h>
#include <linux/module.h>
#include <linux/compat.h>
#include <linux/elfcore-compat.h>

#undef	ELF_ARCH
#undef	ELF_CLASS
#define ELF_CLASS	ELFCLASS32
#define ELF_ARCH	EM_PPC

#undef	elfhdr
#undef	elf_phdr
#undef	elf_note
#undef	elf_addr_t
#define elfhdr		elf32_hdr
#define elf_phdr	elf32_phdr
#define elf_note	elf32_note
#define elf_addr_t	Elf32_Off

#define elf_prstatus	compat_elf_prstatus
#define elf_prpsinfo	compat_elf_prpsinfo

#define elf_core_copy_regs compat_elf_core_copy_regs
static inline void compat_elf_core_copy_regs(compat_elf_gregset_t *elf_regs,
					     struct pt_regs *regs)
{
	PPC_ELF_CORE_COPY_REGS((*elf_regs), regs);
}

#define elf_core_copy_task_regs compat_elf_core_copy_task_regs
static int compat_elf_core_copy_task_regs(struct task_struct *tsk,
					  compat_elf_gregset_t *elf_regs)
{
	struct pt_regs *regs = tsk->thread.regs;
	if (regs)
		compat_elf_core_copy_regs(elf_regs, regs);
	return 1;
}

#include <linux/time.h>

#undef cputime_to_timeval
#define cputime_to_timeval cputime_to_compat_timeval
static __inline__ void
cputime_to_compat_timeval(const cputime_t cputime, struct compat_timeval *value)
{
	unsigned long jiffies = cputime_to_jiffies(cputime);
	value->tv_usec = (jiffies % HZ) * (1000000L / HZ);
	value->tv_sec = jiffies / HZ;
}

#define init_elf_binfmt init_elf32_binfmt

#include "../../../fs/binfmt_elf.c"
