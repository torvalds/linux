#ifndef __ASM_METAG_ELF_H
#define __ASM_METAG_ELF_H

#define EM_METAG      174

/* Meta relocations */
#define R_METAG_HIADDR16                 0
#define R_METAG_LOADDR16                 1
#define R_METAG_ADDR32                   2
#define R_METAG_NONE                     3
#define R_METAG_RELBRANCH                4
#define R_METAG_GETSETOFF                5

/* Backward compatability */
#define R_METAG_REG32OP1                 6
#define R_METAG_REG32OP2                 7
#define R_METAG_REG32OP3                 8
#define R_METAG_REG16OP1                 9
#define R_METAG_REG16OP2                10
#define R_METAG_REG16OP3                11
#define R_METAG_REG32OP4                12

#define R_METAG_HIOG                    13
#define R_METAG_LOOG                    14

/* GNU */
#define R_METAG_GNU_VTINHERIT           30
#define R_METAG_GNU_VTENTRY             31

/* PIC relocations */
#define R_METAG_HI16_GOTOFF             32
#define R_METAG_LO16_GOTOFF             33
#define R_METAG_GETSET_GOTOFF           34
#define R_METAG_GETSET_GOT              35
#define R_METAG_HI16_GOTPC              36
#define R_METAG_LO16_GOTPC              37
#define R_METAG_HI16_PLT                38
#define R_METAG_LO16_PLT                39
#define R_METAG_RELBRANCH_PLT           40
#define R_METAG_GOTOFF                  41
#define R_METAG_PLT                     42
#define R_METAG_COPY                    43
#define R_METAG_JMP_SLOT                44
#define R_METAG_RELATIVE                45
#define R_METAG_GLOB_DAT                46

/*
 * ELF register definitions.
 */

#include <asm/page.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/user.h>

typedef unsigned long elf_greg_t;

#define ELF_NGREG (sizeof(struct user_gp_regs) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef unsigned long elf_fpregset_t;

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) ((x)->e_machine == EM_METAG)

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2LSB
#define ELF_ARCH	EM_METAG

#define ELF_PLAT_INIT(_r, load_addr)	\
	do { _r->ctx.AX[0].U0 = 0; } while (0)

#define USE_ELF_CORE_DUMP
#define CORE_DUMP_USE_REGSET
#define ELF_EXEC_PAGESIZE	PAGE_SIZE

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#define ELF_ET_DYN_BASE         0x08000000UL

#define ELF_CORE_COPY_REGS(_dest, _regs)			\
	memcpy((char *)&_dest, (char *)_regs, sizeof(struct pt_regs));

/* This yields a mask that user programs can use to figure out what
   instruction set this cpu supports.  */

#define ELF_HWCAP	(0)

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.  */

#define ELF_PLATFORM  (NULL)

#define STACK_RND_MASK (0)

#ifdef CONFIG_METAG_USER_TCM

struct elf32_phdr;
struct file;

unsigned long __metag_elf_map(struct file *filep, unsigned long addr,
			      struct elf32_phdr *eppnt, int prot, int type,
			      unsigned long total_size);

static inline unsigned long metag_elf_map(struct file *filep,
					  unsigned long addr,
					  struct elf32_phdr *eppnt, int prot,
					  int type, unsigned long total_size)
{
	return __metag_elf_map(filep, addr, eppnt, prot, type, total_size);
}
#define elf_map metag_elf_map

#endif

#endif
