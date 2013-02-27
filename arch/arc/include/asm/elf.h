/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARC_ELF_H
#define __ASM_ARC_ELF_H

#include <asm/ptrace.h>

#define EM_ARCOMPACT		93

/* Machine specific ELF Hdr flags */
#define EF_ARC_OSABI_MSK	0x00000f00
#define EF_ARC_OSABI_ORIG	0x00000000   /* MUST be zero for back-compat */
#define EF_ARC_OSABI_V2		0x00000200

/* ARC Relocations (kernel Modules only) */
#define  R_ARC_32		0x4
#define  R_ARC_32_ME		0x1B
#define  R_ARC_S25H_PCREL	0x10
#define  R_ARC_S25W_PCREL	0x11

typedef unsigned long elf_greg_t;
typedef unsigned long elf_fpregset_t;

#define ELF_NGREG	(sizeof(struct user_regs_struct) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

/*
 * To ensure that
 *  -we don't load something for the wrong architecture.
 *  -The userspace is using the correct syscall ABI
 */
struct elf32_hdr;
extern int elf_check_arch(const struct elf32_hdr *);
#define elf_check_arch	elf_check_arch

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_ARCH		EM_ARCOMPACT
#define ELF_CLASS		ELFCLASS32

#ifdef CONFIG_CPU_BIG_ENDIAN
#define ELF_DATA		ELFDATA2MSB
#else
#define ELF_DATA		ELFDATA2LSB
#endif

#ifdef __KERNEL__

#define CORE_DUMP_USE_REGSET

#define ELF_EXEC_PAGESIZE	PAGE_SIZE

/*
 * This is the location that an ET_DYN program is loaded if exec'ed.  Typical
 * use of this is to invoke "./ld.so someprog" to test out a new version of
 * the loader.  We need to make sure that it is out of the way of the program
 * that it will "exec", and that there is sufficient room for the brk.
 */
#define ELF_ET_DYN_BASE		(2 * TASK_SIZE / 3)

/*
 * When the program starts, a1 contains a pointer to a function to be
 * registered with atexit, as per the SVR4 ABI.  A value of 0 means we
 * have no such handler.
 */
#define ELF_PLAT_INIT(_r, load_addr)	((_r)->r0 = 0)

/*
 * This yields a mask that user programs can use to figure out what
 * instruction set this cpu supports.
 */
#define ELF_HWCAP	(0)

/*
 * This yields a string that ld.so will use to load implementation
 * specific libraries for optimization.  This is more specific in
 * intent than poking at uname or /proc/cpuinfo.
 */
#define ELF_PLATFORM	(NULL)

#define SET_PERSONALITY(ex) \
	set_personality(PER_LINUX | (current->personality & (~PER_MASK)))

#endif  /* __KERNEL__ */

#endif
