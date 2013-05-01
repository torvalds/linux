/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __ASM_OPENRISC_ELF_H
#define __ASM_OPENRISC_ELF_H


#include <linux/types.h>
#include <uapi/asm/elf.h>

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */

#define elf_check_arch(x) \
	(((x)->e_machine == EM_OR32) || ((x)->e_machine == EM_OPENRISC))

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#define ELF_ET_DYN_BASE         (0x08000000)

/*
 * Enable dump using regset.
 * This covers all of general/DSP/FPU regs.
 */
#define CORE_DUMP_USE_REGSET

#define ELF_EXEC_PAGESIZE	8192

extern void dump_elf_thread(elf_greg_t *dest, struct pt_regs *pt);
#define ELF_CORE_COPY_REGS(dest, regs) dump_elf_thread(dest, regs);

/* This yields a mask that user programs can use to figure out what
   instruction set this cpu supports.  This could be done in userspace,
   but it's not easy, and we've already done it here.  */

#define ELF_HWCAP	(0)

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.

   For the moment, we have only optimizations for the Intel generations,
   but that could change... */

#define ELF_PLATFORM	(NULL)

#endif
