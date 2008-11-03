#ifndef __ASMCRIS_ELF_H
#define __ASMCRIS_ELF_H

/*
 * ELF register definitions..
 */

#include <asm/user.h>

#define R_CRIS_NONE             0
#define R_CRIS_8                1
#define R_CRIS_16               2
#define R_CRIS_32               3
#define R_CRIS_8_PCREL          4
#define R_CRIS_16_PCREL         5
#define R_CRIS_32_PCREL         6
#define R_CRIS_GNU_VTINHERIT    7
#define R_CRIS_GNU_VTENTRY      8
#define R_CRIS_COPY             9
#define R_CRIS_GLOB_DAT         10
#define R_CRIS_JUMP_SLOT        11
#define R_CRIS_RELATIVE         12
#define R_CRIS_16_GOT           13
#define R_CRIS_32_GOT           14
#define R_CRIS_16_GOTPLT        15
#define R_CRIS_32_GOTPLT        16
#define R_CRIS_32_GOTREL        17
#define R_CRIS_32_PLT_GOTREL    18
#define R_CRIS_32_PLT_PCREL     19

typedef unsigned long elf_greg_t;

/* Note that NGREG is defined to ELF_NGREG in include/linux/elfcore.h, and is
   thus exposed to user-space. */
#define ELF_NGREG (sizeof (struct user_regs_struct) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

/* A placeholder; CRIS does not have any fp regs.  */
typedef unsigned long elf_fpregset_t;

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2LSB
#define ELF_ARCH	EM_CRIS

#include <arch/elf.h>

/* The master for these definitions is {binutils}/include/elf/cris.h:  */
/* User symbols in this file have a leading underscore.  */
#define EF_CRIS_UNDERSCORE		0x00000001

/* This is a mask for different incompatible machine variants.  */
#define EF_CRIS_VARIANT_MASK		0x0000000e

/* Variant 0; may contain v0..10 object.  */
#define EF_CRIS_VARIANT_ANY_V0_V10	0x00000000

/* Variant 1; contains v32 object.  */
#define EF_CRIS_VARIANT_V32		0x00000002

/* Variant 2; contains object compatible with v32 and v10.  */
#define EF_CRIS_VARIANT_COMMON_V10_V32	0x00000004
/* End of excerpt from {binutils}/include/elf/cris.h.  */

#define USE_ELF_CORE_DUMP

#define ELF_EXEC_PAGESIZE	8192

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#define ELF_ET_DYN_BASE         (2 * TASK_SIZE / 3)

/* This yields a mask that user programs can use to figure out what
   instruction set this CPU supports.  This could be done in user space,
   but it's not easy, and we've already done it here.  */

#define ELF_HWCAP       (0)

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.
*/

#define ELF_PLATFORM  (NULL)

#define SET_PERSONALITY(ex) set_personality(PER_LINUX)

#endif
