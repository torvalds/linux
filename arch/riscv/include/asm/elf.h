/*
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * Copyright (C) 2012 Regents of the University of California
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _ASM_RISCV_ELF_H
#define _ASM_RISCV_ELF_H

#include <uapi/asm/elf.h>
#include <asm/auxvec.h>
#include <asm/byteorder.h>

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_ARCH	EM_RISCV

#ifdef CONFIG_64BIT
#define ELF_CLASS	ELFCLASS64
#else
#define ELF_CLASS	ELFCLASS32
#endif

#if defined(__LITTLE_ENDIAN)
#define ELF_DATA	ELFDATA2LSB
#elif defined(__BIG_ENDIAN)
#define ELF_DATA	ELFDATA2MSB
#else
#error "Unknown endianness"
#endif

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) ((x)->e_machine == EM_RISCV)

#define CORE_DUMP_USE_REGSET
#define ELF_EXEC_PAGESIZE	(PAGE_SIZE)

/*
 * This is the location that an ET_DYN program is loaded if exec'ed.  Typical
 * use of this is to invoke "./ld.so someprog" to test out a new version of
 * the loader.  We need to make sure that it is out of the way of the program
 * that it will "exec", and that there is sufficient room for the brk.
 */
#define ELF_ET_DYN_BASE		((TASK_SIZE / 3) * 2)

/*
 * This yields a mask that user programs can use to figure out what
 * instruction set this CPU supports.  This could be done in user space,
 * but it's not easy, and we've already done it here.
 */
#define ELF_HWCAP	(elf_hwcap)
extern unsigned long elf_hwcap;

/*
 * This yields a string that ld.so will use to load implementation
 * specific libraries for optimization.  This is more specific in
 * intent than poking at uname or /proc/cpuinfo.
 */
#define ELF_PLATFORM	(NULL)

#define ARCH_DLINFO						\
do {								\
	NEW_AUX_ENT(AT_SYSINFO_EHDR,				\
		(elf_addr_t)current->mm->context.vdso);		\
} while (0)


#define ARCH_HAS_SETUP_ADDITIONAL_PAGES
struct linux_binprm;
extern int arch_setup_additional_pages(struct linux_binprm *bprm,
	int uses_interp);

#endif /* _ASM_RISCV_ELF_H */
