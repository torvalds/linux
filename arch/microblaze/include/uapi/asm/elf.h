/*
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _UAPI_ASM_MICROBLAZE_ELF_H
#define _UAPI_ASM_MICROBLAZE_ELF_H

/*
 * Note there is no "official" ELF designation for Microblaze.
 * I've snaffled the value from the microblaze binutils source code
 * /binutils/microblaze/include/elf/microblaze.h
 */
#define EM_MICROBLAZE		189
#define EM_MICROBLAZE_OLD	0xbaab
#define ELF_ARCH		EM_MICROBLAZE

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x)	((x)->e_machine == EM_MICROBLAZE \
				 || (x)->e_machine == EM_MICROBLAZE_OLD)

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32

#ifndef __uClinux__

/*
 * ELF register definitions..
 */

#include <asm/ptrace.h>
#include <asm/byteorder.h>

#ifndef ELF_GREG_T
#define ELF_GREG_T
typedef unsigned long elf_greg_t;
#endif

#ifndef ELF_NGREG
#define ELF_NGREG (sizeof(struct pt_regs) / sizeof(elf_greg_t))
#endif

#ifndef ELF_GREGSET_T
#define ELF_GREGSET_T
typedef elf_greg_t elf_gregset_t[ELF_NGREG];
#endif

#ifndef ELF_FPREGSET_T
#define ELF_FPREGSET_T

/* TBD */
#define ELF_NFPREG	33	/* includes fsr */
typedef unsigned long elf_fpreg_t;
typedef elf_fpreg_t elf_fpregset_t[ELF_NFPREG];

/* typedef struct user_fpu_struct elf_fpregset_t; */
#endif

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
 * use of this is to invoke "./ld.so someprog" to test out a new version of
 * the loader.  We need to make sure that it is out of the way of the program
 * that it will "exec", and that there is sufficient room for the brk.
 */

#define ELF_ET_DYN_BASE         (0x08000000)

#ifdef __MICROBLAZEEL__
#define ELF_DATA	ELFDATA2LSB
#else
#define ELF_DATA	ELFDATA2MSB
#endif

#define ELF_EXEC_PAGESIZE	PAGE_SIZE


#define ELF_CORE_COPY_REGS(_dest, _regs)			\
	memcpy((char *) &_dest, (char *) _regs,		\
	sizeof(struct pt_regs));

/* This yields a mask that user programs can use to figure out what
 * instruction set this CPU supports.  This could be done in user space,
 * but it's not easy, and we've already done it here.
 */
#define ELF_HWCAP	(0)

/* This yields a string that ld.so will use to load implementation
 * specific libraries for optimization.  This is more specific in
 * intent than poking at uname or /proc/cpuinfo.

 * For the moment, we have only optimizations for the Intel generations,
 * but that could change...
 */
#define ELF_PLATFORM  (NULL)

/* Added _f parameter. Is this definition correct: TBD */
#define ELF_PLAT_INIT(_r, _f)				\
do {							\
	_r->r0 =  _r->r1 =  _r->r2 =  _r->r3 =		\
	_r->r4 =  _r->r5 =  _r->r6 =  _r->r7 =		\
	_r->r8 =  _r->r9 =  _r->r10 = _r->r11 =		\
	_r->r12 = _r->r13 = _r->r14 = _r->r15 =		\
	_r->r16 = _r->r17 = _r->r18 = _r->r19 =		\
	_r->r20 = _r->r21 = _r->r22 = _r->r23 =		\
	_r->r24 = _r->r25 = _r->r26 = _r->r27 =		\
	_r->r28 = _r->r29 = _r->r30 = _r->r31 =		\
	0;						\
} while (0)


#endif /* __uClinux__ */

#endif /* _UAPI_ASM_MICROBLAZE_ELF_H */
