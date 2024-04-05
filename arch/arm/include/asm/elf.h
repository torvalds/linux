/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASMARM_ELF_H
#define __ASMARM_ELF_H

#include <asm/auxvec.h>
#include <asm/hwcap.h>

/*
 * ELF register definitions..
 */
#include <asm/ptrace.h>
#include <asm/user.h>

struct task_struct;

typedef unsigned long elf_greg_t;
typedef unsigned long elf_freg_t[3];

#define ELF_NGREG (sizeof (struct pt_regs) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef struct user_fp elf_fpregset_t;

#define EF_ARM_EABI_MASK	0xff000000
#define EF_ARM_EABI_UNKNOWN	0x00000000
#define EF_ARM_EABI_VER1	0x01000000
#define EF_ARM_EABI_VER2	0x02000000
#define EF_ARM_EABI_VER3	0x03000000
#define EF_ARM_EABI_VER4	0x04000000
#define EF_ARM_EABI_VER5	0x05000000

#define EF_ARM_BE8		0x00800000	/* ABI 4,5 */
#define EF_ARM_LE8		0x00400000	/* ABI 4,5 */
#define EF_ARM_MAVERICK_FLOAT	0x00000800	/* ABI 0 */
#define EF_ARM_VFP_FLOAT	0x00000400	/* ABI 0 */
#define EF_ARM_SOFT_FLOAT	0x00000200	/* ABI 0 */
#define EF_ARM_OLD_ABI		0x00000100	/* ABI 0 */
#define EF_ARM_NEW_ABI		0x00000080	/* ABI 0 */
#define EF_ARM_ALIGN8		0x00000040	/* ABI 0 */
#define EF_ARM_PIC		0x00000020	/* ABI 0 */
#define EF_ARM_MAPSYMSFIRST	0x00000010	/* ABI 2 */
#define EF_ARM_APCS_FLOAT	0x00000010	/* ABI 0, floats in fp regs */
#define EF_ARM_DYNSYMSUSESEGIDX	0x00000008	/* ABI 2 */
#define EF_ARM_APCS_26		0x00000008	/* ABI 0 */
#define EF_ARM_SYMSARESORTED	0x00000004	/* ABI 1,2 */
#define EF_ARM_INTERWORK	0x00000004	/* ABI 0 */
#define EF_ARM_HASENTRY		0x00000002	/* All */
#define EF_ARM_RELEXEC		0x00000001	/* All */

#define R_ARM_NONE		0
#define R_ARM_PC24		1
#define R_ARM_ABS32		2
#define R_ARM_REL32		3
#define R_ARM_CALL		28
#define R_ARM_JUMP24		29
#define R_ARM_TARGET1		38
#define R_ARM_V4BX		40
#define R_ARM_PREL31		42
#define R_ARM_MOVW_ABS_NC	43
#define R_ARM_MOVT_ABS		44
#define R_ARM_MOVW_PREL_NC	45
#define R_ARM_MOVT_PREL		46
#define R_ARM_ALU_PC_G0_NC	57
#define R_ARM_ALU_PC_G1_NC	59
#define R_ARM_LDR_PC_G2		63

#define R_ARM_THM_CALL		10
#define R_ARM_THM_JUMP24	30
#define R_ARM_THM_MOVW_ABS_NC	47
#define R_ARM_THM_MOVT_ABS	48
#define R_ARM_THM_MOVW_PREL_NC	49
#define R_ARM_THM_MOVT_PREL	50

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32
#ifdef __ARMEB__
#define ELF_DATA	ELFDATA2MSB
#else
#define ELF_DATA	ELFDATA2LSB
#endif
#define ELF_ARCH	EM_ARM

/*
 * This yields a string that ld.so will use to load implementation
 * specific libraries for optimization.  This is more specific in
 * intent than poking at uname or /proc/cpuinfo.
 *
 * For now we just provide a fairly general string that describes the
 * processor family.  This could be made more specific later if someone
 * implemented optimisations that require it.  26-bit CPUs give you
 * "v1l" for ARM2 (no SWP) and "v2l" for anything else (ARM1 isn't
 * supported).  32-bit CPUs give you "v3[lb]" for anything based on an
 * ARM6 or ARM7 core and "armv4[lb]" for anything based on a StrongARM-1
 * core.
 */
#define ELF_PLATFORM_SIZE 8
#define ELF_PLATFORM	(elf_platform)

extern char elf_platform[];

struct elf32_hdr;

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
extern int elf_check_arch(const struct elf32_hdr *);
#define elf_check_arch elf_check_arch

#define ELFOSABI_ARM_FDPIC  65	/* ARM FDPIC platform */
#define elf_check_fdpic(x)  ((x)->e_ident[EI_OSABI] == ELFOSABI_ARM_FDPIC)
#define elf_check_const_displacement(x)  ((x)->e_flags & EF_ARM_PIC)
#define ELF_FDPIC_CORE_EFLAGS  0

#define vmcore_elf64_check_arch(x) (0)

extern int arm_elf_read_implies_exec(int);
#define elf_read_implies_exec(ex,stk) arm_elf_read_implies_exec(stk)

#define CORE_DUMP_USE_REGSET
#define ELF_EXEC_PAGESIZE	4096

/* This is the base location for PIE (ET_DYN with INTERP) loads. */
#define ELF_ET_DYN_BASE		0x400000UL

/* When the program starts, a1 contains a pointer to a function to be 
   registered with atexit, as per the SVR4 ABI.  A value of 0 means we 
   have no such handler.  */
#define ELF_PLAT_INIT(_r, load_addr)	(_r)->ARM_r0 = 0

#define ELF_FDPIC_PLAT_INIT(_r, _exec_map_addr, _interp_map_addr, dynamic_addr) \
	do { \
		(_r)->ARM_r7 = _exec_map_addr; \
		(_r)->ARM_r8 = _interp_map_addr; \
		(_r)->ARM_r9 = dynamic_addr; \
	} while(0)

extern void elf_set_personality(const struct elf32_hdr *);
#define SET_PERSONALITY(ex)	elf_set_personality(&(ex))

#ifdef CONFIG_MMU
#ifdef CONFIG_VDSO
#define ARCH_DLINFO						\
do {								\
	NEW_AUX_ENT(AT_SYSINFO_EHDR,				\
		    (elf_addr_t)current->mm->context.vdso);	\
} while (0)
#endif
#define ARCH_HAS_SETUP_ADDITIONAL_PAGES 1
struct linux_binprm;
int arch_setup_additional_pages(struct linux_binprm *, int);
#endif

#endif
