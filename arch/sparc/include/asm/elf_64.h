#ifndef __ASM_SPARC64_ELF_H
#define __ASM_SPARC64_ELF_H

/*
 * ELF register definitions..
 */

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/spitfire.h>

/*
 * Sparc section types
 */
#define STT_REGISTER		13

/*
 * Sparc ELF relocation types
 */
#define	R_SPARC_NONE		0
#define	R_SPARC_8		1
#define	R_SPARC_16		2
#define	R_SPARC_32		3
#define	R_SPARC_DISP8		4
#define	R_SPARC_DISP16		5
#define	R_SPARC_DISP32		6
#define	R_SPARC_WDISP30		7
#define	R_SPARC_WDISP22		8
#define	R_SPARC_HI22		9
#define	R_SPARC_22		10
#define	R_SPARC_13		11
#define	R_SPARC_LO10		12
#define	R_SPARC_GOT10		13
#define	R_SPARC_GOT13		14
#define	R_SPARC_GOT22		15
#define	R_SPARC_PC10		16
#define	R_SPARC_PC22		17
#define	R_SPARC_WPLT30		18
#define	R_SPARC_COPY		19
#define	R_SPARC_GLOB_DAT	20
#define	R_SPARC_JMP_SLOT	21
#define	R_SPARC_RELATIVE	22
#define	R_SPARC_UA32		23
#define R_SPARC_PLT32		24
#define R_SPARC_HIPLT22		25
#define R_SPARC_LOPLT10		26
#define R_SPARC_PCPLT32		27
#define R_SPARC_PCPLT22		28
#define R_SPARC_PCPLT10		29
#define R_SPARC_10		30
#define R_SPARC_11		31
#define R_SPARC_64		32
#define R_SPARC_OLO10		33
#define R_SPARC_WDISP16		40
#define R_SPARC_WDISP19		41
#define R_SPARC_7		43
#define R_SPARC_5		44
#define R_SPARC_6		45

/* Bits present in AT_HWCAP, primarily for Sparc32.  */
#define HWCAP_SPARC_FLUSH       0x00000001
#define HWCAP_SPARC_STBAR       0x00000002
#define HWCAP_SPARC_SWAP        0x00000004
#define HWCAP_SPARC_MULDIV      0x00000008
#define HWCAP_SPARC_V9		0x00000010
#define HWCAP_SPARC_ULTRA3	0x00000020
#define HWCAP_SPARC_BLKINIT	0x00000040
#define HWCAP_SPARC_N2		0x00000080

/* Solaris compatible AT_HWCAP bits. */
#define AV_SPARC_MUL32		0x00000100 /* 32x32 multiply is efficient */
#define AV_SPARC_DIV32		0x00000200 /* 32x32 divide is efficient */
#define AV_SPARC_FSMULD		0x00000400 /* 'fsmuld' is efficient */
#define AV_SPARC_V8PLUS		0x00000800 /* v9 insn available to 32bit */
#define AV_SPARC_POPC		0x00001000 /* 'popc' is efficient */
#define AV_SPARC_VIS		0x00002000 /* VIS insns available */
#define AV_SPARC_VIS2		0x00004000 /* VIS2 insns available */
#define AV_SPARC_ASI_BLK_INIT	0x00008000 /* block init ASIs available */
#define AV_SPARC_FMAF		0x00010000 /* fused multiply-add */
#define AV_SPARC_VIS3		0x00020000 /* VIS3 insns available */
#define AV_SPARC_HPC		0x00040000 /* HPC insns available */
#define AV_SPARC_RANDOM		0x00080000 /* 'random' insn available */
#define AV_SPARC_TRANS		0x00100000 /* transaction insns available */
#define AV_SPARC_FJFMAU		0x00200000 /* unfused multiply-add */
#define AV_SPARC_IMA		0x00400000 /* integer multiply-add */
#define AV_SPARC_ASI_CACHE_SPARING \
				0x00800000 /* cache sparing ASIs available */

#define CORE_DUMP_USE_REGSET

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_ARCH		EM_SPARCV9
#define ELF_CLASS		ELFCLASS64
#define ELF_DATA		ELFDATA2MSB

/* Format of 64-bit elf_gregset_t is:
 * 	G0 --> G7
 * 	O0 --> O7
 * 	L0 --> L7
 * 	I0 --> I7
 *	TSTATE
 *	TPC
 *	TNPC
 *	Y
 */
typedef unsigned long elf_greg_t;
#define ELF_NGREG 36
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef struct {
	unsigned long	pr_regs[32];
	unsigned long	pr_fsr;
	unsigned long	pr_gsr;
	unsigned long	pr_fprs;
} elf_fpregset_t;

/* Format of 32-bit elf_gregset_t is:
 * 	G0 --> G7
 *	O0 --> O7
 *	L0 --> L7
 *	I0 --> I7
 *	PSR, PC, nPC, Y, WIM, TBR
 */
typedef unsigned int compat_elf_greg_t;
#define COMPAT_ELF_NGREG 38
typedef compat_elf_greg_t compat_elf_gregset_t[COMPAT_ELF_NGREG];

typedef struct {
	union {
		unsigned int	pr_regs[32];
		unsigned long	pr_dregs[16];
	} pr_fr;
	unsigned int __unused;
	unsigned int	pr_fsr;
	unsigned char	pr_qcnt;
	unsigned char	pr_q_entrysize;
	unsigned char	pr_en;
	unsigned int	pr_q[64];
} compat_elf_fpregset_t;

/* UltraSparc extensions.  Still unused, but will be eventually.  */
typedef struct {
	unsigned int pr_type;
	unsigned int pr_align;
	union {
		struct {
			union {
				unsigned int	pr_regs[32];
				unsigned long	pr_dregs[16];
				long double	pr_qregs[8];
			} pr_xfr;
		} pr_v8p;
		unsigned int	pr_xfsr;
		unsigned int	pr_fprs;
		unsigned int	pr_xg[8];
		unsigned int	pr_xo[8];
		unsigned long	pr_tstate;
		unsigned int	pr_filler[8];
	} pr_un;
} elf_xregset_t;

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x)		((x)->e_machine == ELF_ARCH)
#define compat_elf_check_arch(x)	((x)->e_machine == EM_SPARC || \
					 (x)->e_machine == EM_SPARC32PLUS)
#define compat_start_thread		start_thread32

#define ELF_EXEC_PAGESIZE	PAGE_SIZE

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#define ELF_ET_DYN_BASE		0x0000010000000000UL
#define COMPAT_ELF_ET_DYN_BASE	0x0000000070000000UL

extern unsigned long sparc64_elf_hwcap;
#define ELF_HWCAP	sparc64_elf_hwcap

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.  */

#define ELF_PLATFORM	(NULL)

#define SET_PERSONALITY(ex)				\
do {	if ((ex).e_ident[EI_CLASS] == ELFCLASS32)	\
		set_thread_flag(TIF_32BIT);		\
	else						\
		clear_thread_flag(TIF_32BIT);		\
	/* flush_thread will update pgd cache */	\
	if (personality(current->personality) != PER_LINUX32)	\
		set_personality(PER_LINUX |		\
			(current->personality & (~PER_MASK)));	\
} while (0)

#endif /* !(__ASM_SPARC64_ELF_H) */
