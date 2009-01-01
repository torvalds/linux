/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Much of this is taken from binutils and GNU libc ...
 */
#ifndef _ASM_ELF_H
#define _ASM_ELF_H


/* ELF header e_flags defines. */
/* MIPS architecture level. */
#define EF_MIPS_ARCH_1		0x00000000	/* -mips1 code.  */
#define EF_MIPS_ARCH_2		0x10000000	/* -mips2 code.  */
#define EF_MIPS_ARCH_3		0x20000000	/* -mips3 code.  */
#define EF_MIPS_ARCH_4		0x30000000	/* -mips4 code.  */
#define EF_MIPS_ARCH_5		0x40000000	/* -mips5 code.  */
#define EF_MIPS_ARCH_32		0x50000000	/* MIPS32 code.  */
#define EF_MIPS_ARCH_64		0x60000000	/* MIPS64 code.  */
#define EF_MIPS_ARCH_32R2	0x70000000	/* MIPS32 R2 code.  */
#define EF_MIPS_ARCH_64R2	0x80000000	/* MIPS64 R2 code.  */

/* The ABI of a file. */
#define EF_MIPS_ABI_O32		0x00001000	/* O32 ABI.  */
#define EF_MIPS_ABI_O64		0x00002000	/* O32 extended for 64 bit.  */

#define PT_MIPS_REGINFO		0x70000000
#define PT_MIPS_RTPROC		0x70000001
#define PT_MIPS_OPTIONS		0x70000002

/* Flags in the e_flags field of the header */
#define EF_MIPS_NOREORDER	0x00000001
#define EF_MIPS_PIC		0x00000002
#define EF_MIPS_CPIC		0x00000004
#define EF_MIPS_ABI2		0x00000020
#define EF_MIPS_OPTIONS_FIRST	0x00000080
#define EF_MIPS_32BITMODE	0x00000100
#define EF_MIPS_ABI		0x0000f000
#define EF_MIPS_ARCH		0xf0000000

#define DT_MIPS_RLD_VERSION	0x70000001
#define DT_MIPS_TIME_STAMP	0x70000002
#define DT_MIPS_ICHECKSUM	0x70000003
#define DT_MIPS_IVERSION	0x70000004
#define DT_MIPS_FLAGS		0x70000005
	#define RHF_NONE	0x00000000
	#define RHF_HARDWAY	0x00000001
	#define RHF_NOTPOT	0x00000002
	#define RHF_SGI_ONLY	0x00000010
#define DT_MIPS_BASE_ADDRESS	0x70000006
#define DT_MIPS_CONFLICT	0x70000008
#define DT_MIPS_LIBLIST		0x70000009
#define DT_MIPS_LOCAL_GOTNO	0x7000000a
#define DT_MIPS_CONFLICTNO	0x7000000b
#define DT_MIPS_LIBLISTNO	0x70000010
#define DT_MIPS_SYMTABNO	0x70000011
#define DT_MIPS_UNREFEXTNO	0x70000012
#define DT_MIPS_GOTSYM		0x70000013
#define DT_MIPS_HIPAGENO	0x70000014
#define DT_MIPS_RLD_MAP		0x70000016

#define R_MIPS_NONE		0
#define R_MIPS_16		1
#define R_MIPS_32		2
#define R_MIPS_REL32		3
#define R_MIPS_26		4
#define R_MIPS_HI16		5
#define R_MIPS_LO16		6
#define R_MIPS_GPREL16		7
#define R_MIPS_LITERAL		8
#define R_MIPS_GOT16		9
#define R_MIPS_PC16		10
#define R_MIPS_CALL16		11
#define R_MIPS_GPREL32		12
/* The remaining relocs are defined on Irix, although they are not
   in the MIPS ELF ABI.  */
#define R_MIPS_UNUSED1		13
#define R_MIPS_UNUSED2		14
#define R_MIPS_UNUSED3		15
#define R_MIPS_SHIFT5		16
#define R_MIPS_SHIFT6		17
#define R_MIPS_64		18
#define R_MIPS_GOT_DISP		19
#define R_MIPS_GOT_PAGE		20
#define R_MIPS_GOT_OFST		21
/*
 * The following two relocation types are specified in the MIPS ABI
 * conformance guide version 1.2 but not yet in the psABI.
 */
#define R_MIPS_GOTHI16		22
#define R_MIPS_GOTLO16		23
#define R_MIPS_SUB		24
#define R_MIPS_INSERT_A		25
#define R_MIPS_INSERT_B		26
#define R_MIPS_DELETE		27
#define R_MIPS_HIGHER		28
#define R_MIPS_HIGHEST		29
/*
 * The following two relocation types are specified in the MIPS ABI
 * conformance guide version 1.2 but not yet in the psABI.
 */
#define R_MIPS_CALLHI16		30
#define R_MIPS_CALLLO16		31
/*
 * This range is reserved for vendor specific relocations.
 */
#define R_MIPS_LOVENDOR		100
#define R_MIPS_HIVENDOR		127

#define SHN_MIPS_ACCOMON	0xff00		/* Allocated common symbols */
#define SHN_MIPS_TEXT		0xff01		/* Allocated test symbols.  */
#define SHN_MIPS_DATA		0xff02		/* Allocated data symbols.  */
#define SHN_MIPS_SCOMMON	0xff03		/* Small common symbols */
#define SHN_MIPS_SUNDEFINED	0xff04		/* Small undefined symbols */

#define SHT_MIPS_LIST		0x70000000
#define SHT_MIPS_CONFLICT	0x70000002
#define SHT_MIPS_GPTAB		0x70000003
#define SHT_MIPS_UCODE		0x70000004
#define SHT_MIPS_DEBUG		0x70000005
#define SHT_MIPS_REGINFO	0x70000006
#define SHT_MIPS_PACKAGE	0x70000007
#define SHT_MIPS_PACKSYM	0x70000008
#define SHT_MIPS_RELD		0x70000009
#define SHT_MIPS_IFACE		0x7000000b
#define SHT_MIPS_CONTENT	0x7000000c
#define SHT_MIPS_OPTIONS	0x7000000d
#define SHT_MIPS_SHDR		0x70000010
#define SHT_MIPS_FDESC		0x70000011
#define SHT_MIPS_EXTSYM		0x70000012
#define SHT_MIPS_DENSE		0x70000013
#define SHT_MIPS_PDESC		0x70000014
#define SHT_MIPS_LOCSYM		0x70000015
#define SHT_MIPS_AUXSYM		0x70000016
#define SHT_MIPS_OPTSYM		0x70000017
#define SHT_MIPS_LOCSTR		0x70000018
#define SHT_MIPS_LINE		0x70000019
#define SHT_MIPS_RFDESC		0x7000001a
#define SHT_MIPS_DELTASYM	0x7000001b
#define SHT_MIPS_DELTAINST	0x7000001c
#define SHT_MIPS_DELTACLASS	0x7000001d
#define SHT_MIPS_DWARF		0x7000001e
#define SHT_MIPS_DELTADECL	0x7000001f
#define SHT_MIPS_SYMBOL_LIB	0x70000020
#define SHT_MIPS_EVENTS		0x70000021
#define SHT_MIPS_TRANSLATE	0x70000022
#define SHT_MIPS_PIXIE		0x70000023
#define SHT_MIPS_XLATE		0x70000024
#define SHT_MIPS_XLATE_DEBUG	0x70000025
#define SHT_MIPS_WHIRL		0x70000026
#define SHT_MIPS_EH_REGION	0x70000027
#define SHT_MIPS_XLATE_OLD	0x70000028
#define SHT_MIPS_PDR_EXCEPTION	0x70000029

#define SHF_MIPS_GPREL		0x10000000
#define SHF_MIPS_MERGE		0x20000000
#define SHF_MIPS_ADDR		0x40000000
#define SHF_MIPS_STRING		0x80000000
#define SHF_MIPS_NOSTRIP	0x08000000
#define SHF_MIPS_LOCAL		0x04000000
#define SHF_MIPS_NAMES		0x02000000
#define SHF_MIPS_NODUPES	0x01000000

#ifndef ELF_ARCH
/* ELF register definitions */
#define ELF_NGREG	45
#define ELF_NFPREG	33

typedef unsigned long elf_greg_t;
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef double elf_fpreg_t;
typedef elf_fpreg_t elf_fpregset_t[ELF_NFPREG];

#ifdef CONFIG_32BIT

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(hdr)						\
({									\
	int __res = 1;							\
	struct elfhdr *__h = (hdr);					\
									\
	if (__h->e_machine != EM_MIPS)					\
		__res = 0;						\
	if (__h->e_ident[EI_CLASS] != ELFCLASS32)			\
		__res = 0;						\
	if ((__h->e_flags & EF_MIPS_ABI2) != 0)				\
		__res = 0;						\
	if (((__h->e_flags & EF_MIPS_ABI) != 0) &&			\
	    ((__h->e_flags & EF_MIPS_ABI) != EF_MIPS_ABI_O32))		\
		__res = 0;						\
									\
	__res;								\
})

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32

#endif /* CONFIG_32BIT */

#ifdef CONFIG_64BIT
/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(hdr)						\
({									\
	int __res = 1;							\
	struct elfhdr *__h = (hdr);					\
									\
	if (__h->e_machine != EM_MIPS)					\
		__res = 0;						\
	if (__h->e_ident[EI_CLASS] != ELFCLASS64) 			\
		__res = 0;						\
									\
	__res;								\
})

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS64

#endif /* CONFIG_64BIT */

/*
 * These are used to set parameters in the core dumps.
 */
#ifdef __MIPSEB__
#define ELF_DATA	ELFDATA2MSB
#elif defined(__MIPSEL__)
#define ELF_DATA	ELFDATA2LSB
#endif
#define ELF_ARCH	EM_MIPS

#endif /* !defined(ELF_ARCH) */

struct mips_abi;

extern struct mips_abi mips_abi;
extern struct mips_abi mips_abi_32;
extern struct mips_abi mips_abi_n32;

#ifdef CONFIG_32BIT

#define SET_PERSONALITY(ex)						\
do {									\
	set_personality(PER_LINUX);					\
									\
	current->thread.abi = &mips_abi;				\
} while (0)

#endif /* CONFIG_32BIT */

#ifdef CONFIG_64BIT

#ifdef CONFIG_MIPS32_N32
#define __SET_PERSONALITY32_N32()					\
	do {								\
		set_thread_flag(TIF_32BIT_ADDR);			\
		current->thread.abi = &mips_abi_n32;			\
	} while (0)
#else
#define __SET_PERSONALITY32_N32()					\
	do { } while (0)
#endif

#ifdef CONFIG_MIPS32_O32
#define __SET_PERSONALITY32_O32()					\
	do {								\
		set_thread_flag(TIF_32BIT_REGS);			\
		set_thread_flag(TIF_32BIT_ADDR);			\
		current->thread.abi = &mips_abi_32;			\
	} while (0)
#else
#define __SET_PERSONALITY32_O32()					\
	do { } while (0)
#endif

#ifdef CONFIG_MIPS32_COMPAT
#define __SET_PERSONALITY32(ex)						\
do {									\
	if ((((ex).e_flags & EF_MIPS_ABI2) != 0) &&			\
	     ((ex).e_flags & EF_MIPS_ABI) == 0)				\
		__SET_PERSONALITY32_N32();				\
	else								\
		__SET_PERSONALITY32_O32();				\
} while (0)
#else
#define __SET_PERSONALITY32(ex)	do { } while (0)
#endif

#define SET_PERSONALITY(ex)						\
do {									\
	clear_thread_flag(TIF_32BIT_REGS);				\
	clear_thread_flag(TIF_32BIT_ADDR);				\
									\
	if ((ex).e_ident[EI_CLASS] == ELFCLASS32)			\
		__SET_PERSONALITY32(ex);				\
	else								\
		current->thread.abi = &mips_abi;			\
									\
	if (current->personality != PER_LINUX32)			\
		set_personality(PER_LINUX);				\
} while (0)

#endif /* CONFIG_64BIT */

struct task_struct;

extern void elf_dump_regs(elf_greg_t *, struct pt_regs *regs);
extern int dump_task_regs(struct task_struct *, elf_gregset_t *);
extern int dump_task_fpu(struct task_struct *, elf_fpregset_t *);

#define ELF_CORE_COPY_REGS(elf_regs, regs)			\
	elf_dump_regs((elf_greg_t *)&(elf_regs), regs);
#define ELF_CORE_COPY_TASK_REGS(tsk, elf_regs) dump_task_regs(tsk, elf_regs)
#define ELF_CORE_COPY_FPREGS(tsk, elf_fpregs)			\
	dump_task_fpu(tsk, elf_fpregs)

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	PAGE_SIZE

/* This yields a mask that user programs can use to figure out what
   instruction set this cpu supports.  This could be done in userspace,
   but it's not easy, and we've already done it here.  */

#define ELF_HWCAP       (0)

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.

   For the moment, we have only optimizations for the Intel generations,
   but that could change... */

#define ELF_PLATFORM  (NULL)

/*
 * See comments in asm-alpha/elf.h, this is the same thing
 * on the MIPS.
 */
#define ELF_PLAT_INIT(_r, load_addr)	do { \
	_r->regs[1] = _r->regs[2] = _r->regs[3] = _r->regs[4] = 0;	\
	_r->regs[5] = _r->regs[6] = _r->regs[7] = _r->regs[8] = 0;	\
	_r->regs[9] = _r->regs[10] = _r->regs[11] = _r->regs[12] = 0;	\
	_r->regs[13] = _r->regs[14] = _r->regs[15] = _r->regs[16] = 0;	\
	_r->regs[17] = _r->regs[18] = _r->regs[19] = _r->regs[20] = 0;	\
	_r->regs[21] = _r->regs[22] = _r->regs[23] = _r->regs[24] = 0;	\
	_r->regs[25] = _r->regs[26] = _r->regs[27] = _r->regs[28] = 0;	\
	_r->regs[30] = _r->regs[31] = 0;				\
} while (0)

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#ifndef ELF_ET_DYN_BASE
#define ELF_ET_DYN_BASE         (TASK_SIZE / 3 * 2)
#endif

#endif /* _ASM_ELF_H */
