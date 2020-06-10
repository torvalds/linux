// SPDX-License-Identifier: GPL-2.0
/*
 * Support for n32 Linux/MIPS ELF binaries.
 * Author: Ralf Baechle (ralf@linux-mips.org)
 *
 * Copyright (C) 1999, 2001 Ralf Baechle
 * Copyright (C) 1999, 2001 Silicon Graphics, Inc.
 *
 * Heavily inspired by the 32-bit Sparc compat code which is
 * Copyright (C) 1995, 1996, 1997, 1998 David S. Miller (davem@redhat.com)
 * Copyright (C) 1995, 1996, 1997, 1998 Jakub Jelinek	(jj@ultra.linux.cz)
 */

#define ELF_ARCH		EM_MIPS
#define ELF_CLASS		ELFCLASS32
#ifdef __MIPSEB__
#define ELF_DATA		ELFDATA2MSB;
#else /* __MIPSEL__ */
#define ELF_DATA		ELFDATA2LSB;
#endif

/* ELF register definitions */
#define ELF_NGREG	45
#define ELF_NFPREG	33

typedef unsigned long elf_greg_t;
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef double elf_fpreg_t;
typedef elf_fpreg_t elf_fpregset_t[ELF_NFPREG];

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch elfn32_check_arch

#define TASK32_SIZE		0x7fff8000UL
#undef ELF_ET_DYN_BASE
#define ELF_ET_DYN_BASE		(TASK32_SIZE / 3 * 2)

#include <asm/processor.h>
#include <linux/elfcore.h>
#include <linux/compat.h>
#include <linux/math64.h>
#include <linux/elfcore-compat.h>

#define elf_prstatus elf_prstatus32
#define elf_prstatus_common compat_elf_prstatus_common
struct elf_prstatus32
{
	struct compat_elf_prstatus_common common;
	elf_gregset_t pr_reg;	/* GP registers */
	int pr_fpvalid;		/* True if math co-processor being used.  */
};
#define elf_prpsinfo compat_elf_prpsinfo

#define init_elf_binfmt init_elfn32_binfmt

#define ELF_CORE_EFLAGS EF_MIPS_ABI2

#undef ns_to_kernel_old_timeval
#define ns_to_kernel_old_timeval ns_to_old_timeval32

/*
 * Some data types as stored in coredump.
 */
#define user_long_t             compat_long_t
#define user_siginfo_t          compat_siginfo_t
#define copy_siginfo_to_external        copy_siginfo_to_external32

#include "../../../fs/binfmt_elf.c"
