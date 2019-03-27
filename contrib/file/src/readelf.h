/*
 * Copyright (c) Christos Zoulas 2003.
 * All Rights Reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * @(#)Id: readelf.h,v 1.9 2002/05/16 18:45:56 christos Exp
 *
 * Provide elf data structures for non-elf machines, allowing file
 * non-elf hosts to determine if an elf binary is stripped.
 * Note: cobbled from the linux header file, with modifications
 */
#ifndef __fake_elf_h__
#define	__fake_elf_h__

#if HAVE_STDINT_H
#include <stdint.h>
#endif

typedef uint32_t	Elf32_Addr;
typedef uint32_t	Elf32_Off;
typedef uint16_t	Elf32_Half;
typedef uint32_t	Elf32_Word;
typedef uint8_t		Elf32_Char;

typedef	uint64_t 	Elf64_Addr;
typedef	uint64_t 	Elf64_Off;
typedef uint64_t 	Elf64_Xword;
typedef uint16_t	Elf64_Half;
typedef uint32_t	Elf64_Word;
typedef uint8_t		Elf64_Char;

#define	EI_NIDENT	16

typedef struct {
	Elf32_Word	a_type;		/* 32-bit id */
	Elf32_Word	a_v;		/* 32-bit id */
} Aux32Info;

typedef struct {
	Elf64_Xword	a_type;		/* 64-bit id */
	Elf64_Xword	a_v;		/* 64-bit id */
} Aux64Info;

#define AT_NULL   0     /* end of vector */
#define AT_IGNORE 1     /* entry should be ignored */
#define AT_EXECFD 2     /* file descriptor of program */
#define AT_PHDR   3     /* program headers for program */
#define AT_PHENT  4     /* size of program header entry */
#define AT_PHNUM  5     /* number of program headers */
#define AT_PAGESZ 6     /* system page size */
#define AT_BASE   7     /* base address of interpreter */
#define AT_FLAGS  8     /* flags */
#define AT_ENTRY  9     /* entry point of program */
#define AT_LINUX_NOTELF 10    /* program is not ELF */
#define AT_LINUX_UID    11    /* real uid */
#define AT_LINUX_EUID   12    /* effective uid */
#define AT_LINUX_GID    13    /* real gid */
#define AT_LINUX_EGID   14    /* effective gid */
#define AT_LINUX_PLATFORM 15  /* string identifying CPU for optimizations */
#define AT_LINUX_HWCAP  16    /* arch dependent hints at CPU capabilities */
#define AT_LINUX_CLKTCK 17    /* frequency at which times() increments */
/* AT_* values 18 through 22 are reserved */
#define AT_LINUX_SECURE 23   /* secure mode boolean */
#define AT_LINUX_BASE_PLATFORM 24     /* string identifying real platform, may
                                 * differ from AT_PLATFORM. */
#define AT_LINUX_RANDOM 25    /* address of 16 random bytes */
#define AT_LINUX_HWCAP2 26    /* extension of AT_HWCAP */
#define AT_LINUX_EXECFN 31   /* filename of program */

typedef struct {
    Elf32_Char	e_ident[EI_NIDENT];
    Elf32_Half	e_type;
    Elf32_Half	e_machine;
    Elf32_Word	e_version;
    Elf32_Addr	e_entry;  /* Entry point */
    Elf32_Off	e_phoff;
    Elf32_Off	e_shoff;
    Elf32_Word	e_flags;
    Elf32_Half	e_ehsize;
    Elf32_Half	e_phentsize;
    Elf32_Half	e_phnum;
    Elf32_Half	e_shentsize;
    Elf32_Half	e_shnum;
    Elf32_Half	e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    Elf64_Char	e_ident[EI_NIDENT];
    Elf64_Half	e_type;
    Elf64_Half	e_machine;
    Elf64_Word	e_version;
    Elf64_Addr	e_entry;  /* Entry point */
    Elf64_Off	e_phoff;
    Elf64_Off	e_shoff;
    Elf64_Word	e_flags;
    Elf64_Half	e_ehsize;
    Elf64_Half	e_phentsize;
    Elf64_Half	e_phnum;
    Elf64_Half	e_shentsize;
    Elf64_Half	e_shnum;
    Elf64_Half	e_shstrndx;
} Elf64_Ehdr;

/* e_type */
#define	ET_REL		1
#define	ET_EXEC		2
#define	ET_DYN		3
#define	ET_CORE		4

/* e_machine (used only for SunOS 5.x hardware capabilities) */
#define	EM_SPARC	2
#define	EM_386		3
#define	EM_SPARC32PLUS	18
#define	EM_SPARCV9	43
#define	EM_IA_64	50
#define	EM_AMD64	62

/* sh_type */
#define	SHT_SYMTAB	2
#define	SHT_NOTE	7
#define	SHT_DYNSYM	11
#define	SHT_SUNW_cap	0x6ffffff5	/* SunOS 5.x hw/sw capabilities */

/* elf type */
#define	ELFDATANONE	0		/* e_ident[EI_DATA] */
#define	ELFDATA2LSB	1
#define	ELFDATA2MSB	2

/* elf class */
#define	ELFCLASSNONE	0
#define	ELFCLASS32	1
#define	ELFCLASS64	2

/* magic number */
#define	EI_MAG0		0		/* e_ident[] indexes */
#define	EI_MAG1		1
#define	EI_MAG2		2
#define	EI_MAG3		3
#define	EI_CLASS	4
#define	EI_DATA		5
#define	EI_VERSION	6
#define	EI_PAD		7

#define	ELFMAG0		0x7f		/* EI_MAG */
#define	ELFMAG1		'E'
#define	ELFMAG2		'L'
#define	ELFMAG3		'F'
#define	ELFMAG		"\177ELF"

#define	OLFMAG1		'O'
#define	OLFMAG		"\177OLF"

typedef struct {
    Elf32_Word	p_type;
    Elf32_Off	p_offset;
    Elf32_Addr	p_vaddr;
    Elf32_Addr	p_paddr;
    Elf32_Word	p_filesz;
    Elf32_Word	p_memsz;
    Elf32_Word	p_flags;
    Elf32_Word	p_align;
} Elf32_Phdr;

typedef struct {
    Elf64_Word	p_type;
    Elf64_Word	p_flags;
    Elf64_Off	p_offset;
    Elf64_Addr	p_vaddr;
    Elf64_Addr	p_paddr;
    Elf64_Xword	p_filesz;
    Elf64_Xword	p_memsz;
    Elf64_Xword	p_align;
} Elf64_Phdr;

#define	PT_NULL		0		/* p_type */
#define	PT_LOAD		1
#define	PT_DYNAMIC	2
#define	PT_INTERP	3
#define	PT_NOTE		4
#define	PT_SHLIB	5
#define	PT_PHDR		6
#define	PT_NUM		7

typedef struct {
    Elf32_Word	sh_name;
    Elf32_Word	sh_type;
    Elf32_Word	sh_flags;
    Elf32_Addr	sh_addr;
    Elf32_Off	sh_offset;
    Elf32_Word	sh_size;
    Elf32_Word	sh_link;
    Elf32_Word	sh_info;
    Elf32_Word	sh_addralign;
    Elf32_Word	sh_entsize;
} Elf32_Shdr;

typedef struct {
    Elf64_Word	sh_name;
    Elf64_Word	sh_type;
    Elf64_Off	sh_flags;
    Elf64_Addr	sh_addr;
    Elf64_Off	sh_offset;
    Elf64_Off	sh_size;
    Elf64_Word	sh_link;
    Elf64_Word	sh_info;
    Elf64_Off	sh_addralign;
    Elf64_Off	sh_entsize;
} Elf64_Shdr;

#define	NT_NETBSD_CORE_PROCINFO		1
#define	NT_NETBSD_CORE_AUXV		2

struct NetBSD_elfcore_procinfo {
	/* Version 1 fields start here. */
	uint32_t	cpi_version;		/* our version */
	uint32_t	cpi_cpisize;		/* sizeof(this struct) */
	uint32_t	cpi_signo;		/* killing signal */
	uint32_t	cpi_sigcode;		/* signal code */
	uint32_t	cpi_sigpend[4];		/* pending signals */
	uint32_t	cpi_sigmask[4];		/* blocked signals */
	uint32_t	cpi_sigignore[4];	/* ignored signals */
	uint32_t	cpi_sigcatch[4];	/* caught signals */
	int32_t		cpi_pid;		/* process ID */
	int32_t		cpi_ppid;		/* parent process ID */
	int32_t		cpi_pgrp;		/* process group ID */
	int32_t		cpi_sid;		/* session ID */
	uint32_t	cpi_ruid;		/* real user ID */
	uint32_t	cpi_euid;		/* effective user ID */
	uint32_t	cpi_svuid;		/* saved user ID */
	uint32_t	cpi_rgid;		/* real group ID */
	uint32_t	cpi_egid;		/* effective group ID */
	uint32_t	cpi_svgid;		/* saved group ID */
	uint32_t	cpi_nlwps;		/* number of LWPs */
	int8_t		cpi_name[32];		/* copy of p->p_comm */
	/* Add version 2 fields below here. */
	int32_t		cpi_siglwp;	/* LWP target of killing signal */
};

/* Note header in a PT_NOTE section */
typedef struct elf_note {
    Elf32_Word	n_namesz;	/* Name size */
    Elf32_Word	n_descsz;	/* Content size */
    Elf32_Word	n_type;		/* Content type */
} Elf32_Nhdr;

typedef struct {
    Elf64_Word	n_namesz;
    Elf64_Word	n_descsz;
    Elf64_Word	n_type;
} Elf64_Nhdr;

/* Notes used in ET_CORE */
#define	NT_PRSTATUS	1
#define	NT_PRFPREG	2
#define	NT_PRPSINFO	3
#define	NT_PRXREG	4
#define	NT_TASKSTRUCT	4
#define	NT_PLATFORM	5
#define	NT_AUXV		6

/* Note types used in executables */
/* NetBSD executables (name = "NetBSD") */
#define	NT_NETBSD_VERSION	1
#define	NT_NETBSD_EMULATION	2
#define	NT_FREEBSD_VERSION	1
#define	NT_OPENBSD_VERSION	1
#define	NT_DRAGONFLY_VERSION	1
/*
 * GNU executables (name = "GNU")
 * word[0]: GNU OS tags
 * word[1]: major version
 * word[2]: minor version
 * word[3]: tiny version
 */
#define	NT_GNU_VERSION		1

/* GNU OS tags */
#define	GNU_OS_LINUX	0
#define	GNU_OS_HURD	1
#define	GNU_OS_SOLARIS	2
#define	GNU_OS_KFREEBSD	3
#define	GNU_OS_KNETBSD	4

/*
 * GNU Hardware capability information 
 * word[0]: Number of entries
 * word[1]: Bitmask of enabled entries
 * Followed by a byte id, and a NUL terminated string per entry
 */
#define	NT_GNU_HWCAP		2

/*
 * GNU Build ID generated by ld
 * 160 bit SHA1 [default] 
 * 128 bit md5 or uuid
 */
#define	NT_GNU_BUILD_ID		3

/*
 * NetBSD-specific note type: PaX.
 * There should be 1 NOTE per executable.
 * name: PaX\0
 * namesz: 4
 * desc:
 *	word[0]: capability bitmask
 * descsz: 4
 */
#define NT_NETBSD_PAX		3
#define NT_NETBSD_PAX_MPROTECT		0x01	/* Force enable Mprotect */
#define NT_NETBSD_PAX_NOMPROTECT	0x02	/* Force disable Mprotect */
#define NT_NETBSD_PAX_GUARD		0x04	/* Force enable Segvguard */
#define NT_NETBSD_PAX_NOGUARD		0x08	/* Force disable Servguard */
#define NT_NETBSD_PAX_ASLR		0x10	/* Force enable ASLR */
#define NT_NETBSD_PAX_NOASLR		0x20	/* Force disable ASLR */

/*
 * NetBSD-specific note type: MACHINE_ARCH.
 * There should be 1 NOTE per executable.
 * name:	NetBSD\0
 * namesz:	7
 * desc:	string
 * descsz:	variable
 */
#define NT_NETBSD_MARCH		5

/*
 * NetBSD-specific note type: COMPILER MODEL.
 * There should be 1 NOTE per executable.
 * name:	NetBSD\0
 * namesz:	7
 * desc:	string
 * descsz:	variable
 */
#define NT_NETBSD_CMODEL	6

/*
 * Golang-specific note type
 * name: Go\0\0
 * namesz: 4
 * desc: base-64 build id.
 * descsz: < 128
 */
#define NT_GO_BUILD_ID	4

/*
 * FreeBSD specific notes
 */
#define NT_FREEBSD_PROCSTAT_AUXV	16

#if !defined(ELFSIZE) && defined(ARCH_ELFSIZE)
#define ELFSIZE ARCH_ELFSIZE
#endif
/* SunOS 5.x hardware/software capabilities */
typedef struct {
	Elf32_Word	c_tag;
	union {
		Elf32_Word	c_val;
		Elf32_Addr	c_ptr;
	} c_un;
} Elf32_Cap;

typedef struct {
	Elf64_Xword	c_tag;
	union {
		Elf64_Xword	c_val;
		Elf64_Addr	c_ptr;
	} c_un;
} Elf64_Cap;

/* SunOS 5.x hardware/software capability tags */
#define	CA_SUNW_NULL	0
#define	CA_SUNW_HW_1	1
#define	CA_SUNW_SF_1	2

/* SunOS 5.x software capabilities */
#define	SF1_SUNW_FPKNWN	0x01
#define	SF1_SUNW_FPUSED	0x02
#define	SF1_SUNW_MASK	0x03

/* SunOS 5.x hardware capabilities: sparc */
#define	AV_SPARC_MUL32		0x0001
#define	AV_SPARC_DIV32		0x0002
#define	AV_SPARC_FSMULD		0x0004
#define	AV_SPARC_V8PLUS		0x0008
#define	AV_SPARC_POPC		0x0010
#define	AV_SPARC_VIS		0x0020
#define	AV_SPARC_VIS2		0x0040
#define	AV_SPARC_ASI_BLK_INIT	0x0080
#define	AV_SPARC_FMAF		0x0100
#define	AV_SPARC_FJFMAU		0x4000
#define	AV_SPARC_IMA		0x8000

/* SunOS 5.x hardware capabilities: 386 */
#define	AV_386_FPU		0x00000001
#define	AV_386_TSC		0x00000002
#define	AV_386_CX8		0x00000004
#define	AV_386_SEP		0x00000008
#define	AV_386_AMD_SYSC		0x00000010
#define	AV_386_CMOV		0x00000020
#define	AV_386_MMX		0x00000040
#define	AV_386_AMD_MMX		0x00000080
#define	AV_386_AMD_3DNow	0x00000100
#define	AV_386_AMD_3DNowx	0x00000200
#define	AV_386_FXSR		0x00000400
#define	AV_386_SSE		0x00000800
#define	AV_386_SSE2		0x00001000
#define	AV_386_PAUSE		0x00002000
#define	AV_386_SSE3		0x00004000
#define	AV_386_MON		0x00008000
#define	AV_386_CX16		0x00010000
#define	AV_386_AHF		0x00020000
#define	AV_386_TSCP		0x00040000
#define	AV_386_AMD_SSE4A	0x00080000
#define	AV_386_POPCNT		0x00100000
#define	AV_386_AMD_LZCNT	0x00200000
#define	AV_386_SSSE3		0x00400000
#define	AV_386_SSE4_1		0x00800000
#define	AV_386_SSE4_2		0x01000000

/*
 * Dynamic Section structure array
 */
typedef struct {
	Elf32_Word		d_tag;	/* entry tag value */
	union {
		Elf32_Addr	d_ptr;
		Elf32_Word	d_val;
	} d_un;
} Elf32_Dyn;

typedef struct {
	Elf64_Xword		d_tag;	/* entry tag value */
	union {
		Elf64_Addr	d_ptr;
		Elf64_Xword	d_val;
	} d_un;
} Elf64_Dyn;

/* d_tag */
#define DT_NULL		0	/* Marks end of dynamic array */
#define DT_NEEDED	1	/* Name of needed library (DT_STRTAB offset) */
#define DT_PLTRELSZ	2	/* Size, in bytes, of relocations in PLT */
#define DT_PLTGOT	3	/* Address of PLT and/or GOT */
#define DT_HASH		4	/* Address of symbol hash table */
#define DT_STRTAB	5	/* Address of string table */
#define DT_SYMTAB	6	/* Address of symbol table */
#define DT_RELA		7	/* Address of Rela relocation table */
#define DT_RELASZ	8	/* Size, in bytes, of DT_RELA table */
#define DT_RELAENT	9	/* Size, in bytes, of one DT_RELA entry */
#define DT_STRSZ	10	/* Size, in bytes, of DT_STRTAB table */
#define DT_SYMENT	11	/* Size, in bytes, of one DT_SYMTAB entry */
#define DT_INIT		12	/* Address of initialization function */
#define DT_FINI		13	/* Address of termination function */
#define DT_SONAME	14	/* Shared object name (DT_STRTAB offset) */
#define DT_RPATH	15	/* Library search path (DT_STRTAB offset) */
#define DT_SYMBOLIC	16	/* Start symbol search within local object */
#define DT_REL		17	/* Address of Rel relocation table */
#define DT_RELSZ	18	/* Size, in bytes, of DT_REL table */
#define DT_RELENT	19	/* Size, in bytes, of one DT_REL entry */
#define DT_PLTREL	20	/* Type of PLT relocation entries */
#define DT_DEBUG	21	/* Used for debugging; unspecified */
#define DT_TEXTREL	22	/* Relocations might modify non-writable seg */
#define DT_JMPREL	23	/* Address of relocations associated with PLT */
#define DT_BIND_NOW	24	/* Process all relocations at load-time */
#define DT_INIT_ARRAY	25	/* Address of initialization function array */
#define DT_FINI_ARRAY	26	/* Size, in bytes, of DT_INIT_ARRAY array */
#define DT_INIT_ARRAYSZ 27	/* Address of termination function array */
#define DT_FINI_ARRAYSZ 28	/* Size, in bytes, of DT_FINI_ARRAY array*/
#define DT_RUNPATH	29	/* overrides DT_RPATH */
#define DT_FLAGS	30	/* Encodes ORIGIN, SYMBOLIC, TEXTREL, BIND_NOW, STATIC_TLS */
#define DT_ENCODING	31	/* ??? */
#define DT_PREINIT_ARRAY 32	/* Address of pre-init function array */
#define DT_PREINIT_ARRAYSZ 33	/* Size, in bytes, of DT_PREINIT_ARRAY array */
#define DT_NUM		34

#define DT_LOOS		0x60000000	/* Operating system specific range */
#define DT_VERSYM	0x6ffffff0	/* Symbol versions */
#define DT_FLAGS_1	0x6ffffffb	/* ELF dynamic flags */
#define DT_VERDEF	0x6ffffffc	/* Versions defined by file */
#define DT_VERDEFNUM	0x6ffffffd	/* Number of versions defined by file */
#define DT_VERNEED	0x6ffffffe	/* Versions needed by file */
#define DT_VERNEEDNUM	0x6fffffff	/* Number of versions needed by file */
#define DT_HIOS		0x6fffffff
#define DT_LOPROC	0x70000000	/* Processor-specific range */
#define DT_HIPROC	0x7fffffff

/* Flag values for DT_FLAGS */
#define DF_ORIGIN	0x00000001	/* uses $ORIGIN */
#define DF_SYMBOLIC	0x00000002	/* */
#define DF_TEXTREL	0x00000004	/* */
#define DF_BIND_NOW	0x00000008	/* */
#define DF_STATIC_TLS	0x00000010	/* */

/* Flag values for DT_FLAGS_1 */
#define	DF_1_NOW	0x00000001	/* Same as DF_BIND_NOW */
#define	DF_1_GLOBAL	0x00000002	/* Unused */
#define	DF_1_GROUP	0x00000004	/* Is member of group */
#define	DF_1_NODELETE	0x00000008	/* Cannot be deleted from process */
#define	DF_1_LOADFLTR	0x00000010	/* Immediate loading of filters */
#define	DF_1_INITFIRST	0x00000020	/* init/fini takes priority */
#define	DF_1_NOOPEN	0x00000040	/* Do not allow loading on dlopen() */
#define	DF_1_ORIGIN	0x00000080 	/* Require $ORIGIN processing */
#define	DF_1_DIRECT	0x00000100	/* Enable direct bindings */
#define	DF_1_INTERPOSE 	0x00000400	/* Is an interposer */
#define	DF_1_NODEFLIB	0x00000800 	/* Ignore default library search path */
#define	DF_1_NODUMP	0x00001000 	/* Cannot be dumped with dldump(3C) */
#define	DF_1_CONFALT	0x00002000 	/* Configuration alternative */
#define	DF_1_ENDFILTEE	0x00004000	/* Filtee ends filter's search */
#define	DF_1_DISPRELDNE	0x00008000	/* Did displacement relocation */
#define	DF_1_DISPRELPND 0x00010000	/* Pending displacement relocation */
#define	DF_1_NODIRECT	0x00020000 	/* Has non-direct bindings */
#define	DF_1_IGNMULDEF	0x00040000	/* Used internally */
#define	DF_1_NOKSYMS	0x00080000	/* Used internally */
#define	DF_1_NOHDR	0x00100000	/* Used internally */
#define	DF_1_EDITED	0x00200000	/* Has been modified since build */
#define	DF_1_NORELOC	0x00400000 	/* Used internally */
#define	DF_1_SYMINTPOSE 0x00800000 	/* Has individual symbol interposers */
#define	DF_1_GLOBAUDIT	0x01000000	/* Require global auditing */
#define	DF_1_SINGLETON	0x02000000	/* Has singleton symbols */
#define	DF_1_STUB	0x04000000	/* Stub */
#define	DF_1_PIE	0x08000000	/* Position Independent Executable */

#endif
