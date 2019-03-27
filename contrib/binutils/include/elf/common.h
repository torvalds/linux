/* ELF support for BFD.
   Copyright 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.

   Written by Fred Fish @ Cygnus Support, from information published
   in "UNIX System V Release 4, Programmers Guide: ANSI C and
   Programming Support Tools".

   This file is part of BFD, the Binary File Descriptor library.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */


/* This file is part of ELF support for BFD, and contains the portions
   that are common to both the internal and external representations.
   For example, ELFMAG0 is the byte 0x7F in both the internal (in-memory)
   and external (in-file) representations.  */

#ifndef _ELF_COMMON_H
#define _ELF_COMMON_H

/* Fields in e_ident[].  */

#define EI_MAG0		0	/* File identification byte 0 index */
#define ELFMAG0		   0x7F	/* Magic number byte 0 */

#define EI_MAG1		1	/* File identification byte 1 index */
#define ELFMAG1		    'E'	/* Magic number byte 1 */

#define EI_MAG2		2	/* File identification byte 2 index */
#define ELFMAG2		    'L'	/* Magic number byte 2 */

#define EI_MAG3		3	/* File identification byte 3 index */
#define ELFMAG3		    'F'	/* Magic number byte 3 */

#define EI_CLASS	4	/* File class */
#define ELFCLASSNONE	      0	/* Invalid class */
#define ELFCLASS32	      1	/* 32-bit objects */
#define ELFCLASS64	      2	/* 64-bit objects */

#define EI_DATA		5	/* Data encoding */
#define ELFDATANONE	      0	/* Invalid data encoding */
#define ELFDATA2LSB	      1	/* 2's complement, little endian */
#define ELFDATA2MSB	      2	/* 2's complement, big endian */

#define EI_VERSION	6	/* File version */

#define EI_OSABI	7	/* Operating System/ABI indication */
#define ELFOSABI_NONE	      0	/* UNIX System V ABI */
#define ELFOSABI_HPUX	      1	/* HP-UX operating system */
#define ELFOSABI_NETBSD	      2	/* NetBSD */
#define ELFOSABI_LINUX	      3	/* GNU/Linux */
#define ELFOSABI_HURD	      4	/* GNU/Hurd */
#define ELFOSABI_SOLARIS      6	/* Solaris */
#define ELFOSABI_AIX	      7	/* AIX */
#define ELFOSABI_IRIX	      8	/* IRIX */
#define ELFOSABI_FREEBSD      9	/* FreeBSD */
#define ELFOSABI_TRU64	     10	/* TRU64 UNIX */
#define ELFOSABI_MODESTO     11	/* Novell Modesto */
#define ELFOSABI_OPENBSD     12	/* OpenBSD */
#define ELFOSABI_OPENVMS     13	/* OpenVMS */
#define ELFOSABI_NSK	     14	/* Hewlett-Packard Non-Stop Kernel */
#define ELFOSABI_AROS	     15	/* Amiga Research OS */
#define ELFOSABI_ARM	     97	/* ARM */
#define ELFOSABI_STANDALONE 255	/* Standalone (embedded) application */

#define EI_ABIVERSION	8	/* ABI version */

#define EI_PAD		9	/* Start of padding bytes */


/* Values for e_type, which identifies the object file type.  */

#define ET_NONE		0	/* No file type */
#define ET_REL		1	/* Relocatable file */
#define ET_EXEC		2	/* Executable file */
#define ET_DYN		3	/* Shared object file */
#define ET_CORE		4	/* Core file */
#define ET_LOOS		0xFE00	/* Operating system-specific */
#define ET_HIOS		0xFEFF	/* Operating system-specific */
#define ET_LOPROC	0xFF00	/* Processor-specific */
#define ET_HIPROC	0xFFFF	/* Processor-specific */

/* Values for e_machine, which identifies the architecture.  These numbers
   are officially assigned by registry@caldera.com.  See below for a list of
   ad-hoc numbers used during initial development.  */

#define EM_NONE		  0	/* No machine */
#define EM_M32		  1	/* AT&T WE 32100 */
#define EM_SPARC	  2	/* SUN SPARC */
#define EM_386		  3	/* Intel 80386 */
#define EM_68K		  4	/* Motorola m68k family */
#define EM_88K		  5	/* Motorola m88k family */
#define EM_486		  6	/* Intel 80486 *//* Reserved for future use */
#define EM_860		  7	/* Intel 80860 */
#define EM_MIPS		  8	/* MIPS R3000 (officially, big-endian only) */
#define EM_S370		  9	/* IBM System/370 */
#define EM_MIPS_RS3_LE	 10	/* MIPS R3000 little-endian (Oct 4 1999 Draft) Deprecated */

#define EM_PARISC	 15	/* HPPA */

#define EM_VPP550	 17	/* Fujitsu VPP500 */
#define EM_SPARC32PLUS	 18	/* Sun's "v8plus" */
#define EM_960		 19	/* Intel 80960 */
#define EM_PPC		 20	/* PowerPC */
#define EM_PPC64	 21	/* 64-bit PowerPC */
#define EM_S390		 22	/* IBM S/390 */
#define EM_SPU		 23	/* Sony/Toshiba/IBM SPU */

#define EM_V800		 36	/* NEC V800 series */
#define EM_FR20		 37	/* Fujitsu FR20 */
#define EM_RH32		 38	/* TRW RH32 */
#define EM_MCORE	 39	/* Motorola M*Core */ /* May also be taken by Fujitsu MMA */
#define EM_RCE		 39	/* Old name for MCore */
#define EM_ARM		 40	/* ARM */
#define EM_OLD_ALPHA	 41	/* Digital Alpha */
#define EM_SH		 42	/* Renesas (formerly Hitachi) / SuperH SH */
#define EM_SPARCV9	 43	/* SPARC v9 64-bit */
#define EM_TRICORE	 44	/* Siemens Tricore embedded processor */
#define EM_ARC		 45	/* ARC Cores */
#define EM_H8_300	 46	/* Renesas (formerly Hitachi) H8/300 */
#define EM_H8_300H	 47	/* Renesas (formerly Hitachi) H8/300H */
#define EM_H8S		 48	/* Renesas (formerly Hitachi) H8S */
#define EM_H8_500	 49	/* Renesas (formerly Hitachi) H8/500 */
#define EM_IA_64	 50	/* Intel IA-64 Processor */
#define EM_MIPS_X	 51	/* Stanford MIPS-X */
#define EM_COLDFIRE	 52	/* Motorola Coldfire */
#define EM_68HC12	 53	/* Motorola M68HC12 */
#define EM_MMA		 54	/* Fujitsu Multimedia Accelerator */
#define EM_PCP		 55	/* Siemens PCP */
#define EM_NCPU		 56	/* Sony nCPU embedded RISC processor */
#define EM_NDR1		 57	/* Denso NDR1 microprocesspr */
#define EM_STARCORE	 58	/* Motorola Star*Core processor */
#define EM_ME16		 59	/* Toyota ME16 processor */
#define EM_ST100	 60	/* STMicroelectronics ST100 processor */
#define EM_TINYJ	 61	/* Advanced Logic Corp. TinyJ embedded processor */
#define EM_X86_64	 62	/* Advanced Micro Devices X86-64 processor */

#define EM_PDP10	 64	/* Digital Equipment Corp. PDP-10 */
#define EM_PDP11	 65	/* Digital Equipment Corp. PDP-11 */
#define EM_FX66		 66	/* Siemens FX66 microcontroller */
#define EM_ST9PLUS	 67	/* STMicroelectronics ST9+ 8/16 bit microcontroller */
#define EM_ST7		 68	/* STMicroelectronics ST7 8-bit microcontroller */
#define EM_68HC16	 69	/* Motorola MC68HC16 Microcontroller */
#define EM_68HC11	 70	/* Motorola MC68HC11 Microcontroller */
#define EM_68HC08	 71	/* Motorola MC68HC08 Microcontroller */
#define EM_68HC05	 72	/* Motorola MC68HC05 Microcontroller */
#define EM_SVX		 73	/* Silicon Graphics SVx */
#define EM_ST19		 74	/* STMicroelectronics ST19 8-bit cpu */
#define EM_VAX		 75	/* Digital VAX */
#define EM_CRIS		 76	/* Axis Communications 32-bit embedded processor */
#define EM_JAVELIN	 77	/* Infineon Technologies 32-bit embedded cpu */
#define EM_FIREPATH	 78	/* Element 14 64-bit DSP processor */
#define EM_ZSP		 79	/* LSI Logic's 16-bit DSP processor */
#define EM_MMIX		 80	/* Donald Knuth's educational 64-bit processor */
#define EM_HUANY	 81	/* Harvard's machine-independent format */
#define EM_PRISM	 82	/* SiTera Prism */
#define EM_AVR		 83	/* Atmel AVR 8-bit microcontroller */
#define EM_FR30		 84	/* Fujitsu FR30 */
#define EM_D10V		 85	/* Mitsubishi D10V */
#define EM_D30V		 86	/* Mitsubishi D30V */
#define EM_V850		 87	/* NEC v850 */
#define EM_M32R		 88	/* Renesas M32R (formerly Mitsubishi M32R) */
#define EM_MN10300	 89	/* Matsushita MN10300 */
#define EM_MN10200	 90	/* Matsushita MN10200 */
#define EM_PJ		 91	/* picoJava */
#define EM_OPENRISC	 92	/* OpenRISC 32-bit embedded processor */
#define EM_ARC_A5	 93	/* ARC Cores Tangent-A5 */
#define EM_XTENSA	 94	/* Tensilica Xtensa Architecture */
#define EM_IP2K		101	/* Ubicom IP2022 micro controller */
#define EM_CR		103	/* National Semiconductor CompactRISC */
#define EM_MSP430	105	/* TI msp430 micro controller */
#define EM_BLACKFIN	106	/* ADI Blackfin */
#define EM_ALTERA_NIOS2	113	/* Altera Nios II soft-core processor */
#define EM_CRX		114	/* National Semiconductor CRX */
#define EM_CR16		115	/* National Semiconductor CompactRISC - CR16 */
#define EM_SCORE        135     /* Sunplus Score */ 

/* If it is necessary to assign new unofficial EM_* values, please pick large
   random numbers (0x8523, 0xa7f2, etc.) to minimize the chances of collision
   with official or non-GNU unofficial values.

   NOTE: Do not just increment the most recent number by one.
   Somebody else somewhere will do exactly the same thing, and you
   will have a collision.  Instead, pick a random number.

   Normally, each entity or maintainer responsible for a machine with an
   unofficial e_machine number should eventually ask registry@caldera.com for
   an officially blessed number to be added to the list above.	*/

/* Old version of Sparc v9, from before the ABI;
   This should be removed shortly.  */
#define EM_OLD_SPARCV9		11

/* Old version of PowerPC, this should be removed shortly. */
#define EM_PPC_OLD		17

/* picoJava */
#define EM_PJ_OLD      		99

/* AVR magic number.  Written in the absense of an ABI.  */
#define EM_AVR_OLD		0x1057

/* MSP430 magic number.  Written in the absense of everything.  */
#define EM_MSP430_OLD		0x1059

/* Morpho MT.   Written in the absense of an ABI.  */
#define EM_MT                   0x2530

/* FR30 magic number - no EABI available.  */
#define EM_CYGNUS_FR30		0x3330

/* OpenRISC magic number.  Written in the absense of an ABI.  */
#define EM_OPENRISC_OLD		0x3426

/* DLX magic number.  Written in the absense of an ABI.  */
#define EM_DLX			0x5aa5

/* FRV magic number - no EABI available??.  */
#define EM_CYGNUS_FRV		0x5441

/* Infineon Technologies 16-bit microcontroller with C166-V2 core.  */
#define EM_XC16X   		0x4688

/* D10V backend magic number.  Written in the absence of an ABI.  */
#define EM_CYGNUS_D10V		0x7650

/* D30V backend magic number.  Written in the absence of an ABI.  */
#define EM_CYGNUS_D30V		0x7676

/* Ubicom IP2xxx;   Written in the absense of an ABI.  */
#define EM_IP2K_OLD		0x8217

/* (Deprecated) Temporary number for the OpenRISC processor.  */
#define EM_OR32			0x8472

/* Cygnus PowerPC ELF backend.  Written in the absence of an ABI.  */
#define EM_CYGNUS_POWERPC 	0x9025

/* Alpha backend magic number.  Written in the absence of an ABI.  */
#define EM_ALPHA		0x9026

/* Cygnus M32R ELF backend.  Written in the absence of an ABI.  */
#define EM_CYGNUS_M32R		0x9041

/* V850 backend magic number.  Written in the absense of an ABI.  */
#define EM_CYGNUS_V850		0x9080

/* old S/390 backend magic number. Written in the absence of an ABI.  */
#define EM_S390_OLD		0xa390

/* Old, unofficial value for Xtensa.  */
#define EM_XTENSA_OLD		0xabc7

#define EM_XSTORMY16		0xad45

/* mn10200 and mn10300 backend magic numbers.
   Written in the absense of an ABI.  */
#define EM_CYGNUS_MN10300	0xbeef
#define EM_CYGNUS_MN10200	0xdead

/* Renesas M32C and M16C.  */
#define EM_M32C			0xFEB0

/* Vitesse IQ2000.  */
#define EM_IQ2000		0xFEBA

/* NIOS magic number - no EABI available.  */
#define EM_NIOS32		0xFEBB

#define EM_CYGNUS_MEP		0xF00D  /* Toshiba MeP */

/* See the above comment before you add a new EM_* value here.  */

/* Values for e_version.  */

#define EV_NONE		0		/* Invalid ELF version */
#define EV_CURRENT	1		/* Current version */

/* Values for program header, p_type field.  */

#define PT_NULL		0		/* Program header table entry unused */
#define PT_LOAD		1		/* Loadable program segment */
#define PT_DYNAMIC	2		/* Dynamic linking information */
#define PT_INTERP	3		/* Program interpreter */
#define PT_NOTE		4		/* Auxiliary information */
#define PT_SHLIB	5		/* Reserved, unspecified semantics */
#define PT_PHDR		6		/* Entry for header table itself */
#define PT_TLS		7		/* Thread local storage segment */
#define PT_LOOS		0x60000000	/* OS-specific */
#define PT_HIOS		0x6fffffff	/* OS-specific */
#define PT_LOPROC	0x70000000	/* Processor-specific */
#define PT_HIPROC	0x7FFFFFFF	/* Processor-specific */

#define PT_GNU_EH_FRAME	(PT_LOOS + 0x474e550) /* Frame unwind information */
#define PT_SUNW_EH_FRAME PT_GNU_EH_FRAME      /* Solaris uses the same value */
#define PT_GNU_STACK	(PT_LOOS + 0x474e551) /* Stack flags */
#define PT_GNU_RELRO	(PT_LOOS + 0x474e552) /* Read-only after relocation */

/* Program segment permissions, in program header p_flags field.  */

#define PF_X		(1 << 0)	/* Segment is executable */
#define PF_W		(1 << 1)	/* Segment is writable */
#define PF_R		(1 << 2)	/* Segment is readable */
/* #define PF_MASKOS	0x0F000000    *//* OS-specific reserved bits */
#define PF_MASKOS	0x0FF00000	/* New value, Oct 4, 1999 Draft */
#define PF_MASKPROC	0xF0000000	/* Processor-specific reserved bits */

/* Values for section header, sh_type field.  */

#define SHT_NULL	0		/* Section header table entry unused */
#define SHT_PROGBITS	1		/* Program specific (private) data */
#define SHT_SYMTAB	2		/* Link editing symbol table */
#define SHT_STRTAB	3		/* A string table */
#define SHT_RELA	4		/* Relocation entries with addends */
#define SHT_HASH	5		/* A symbol hash table */
#define SHT_DYNAMIC	6		/* Information for dynamic linking */
#define SHT_NOTE	7		/* Information that marks file */
#define SHT_NOBITS	8		/* Section occupies no space in file */
#define SHT_REL		9		/* Relocation entries, no addends */
#define SHT_SHLIB	10		/* Reserved, unspecified semantics */
#define SHT_DYNSYM	11		/* Dynamic linking symbol table */

#define SHT_INIT_ARRAY	  14		/* Array of ptrs to init functions */
#define SHT_FINI_ARRAY	  15		/* Array of ptrs to finish functions */
#define SHT_PREINIT_ARRAY 16		/* Array of ptrs to pre-init funcs */
#define SHT_GROUP	  17		/* Section contains a section group */
#define SHT_SYMTAB_SHNDX  18		/* Indicies for SHN_XINDEX entries */

#define SHT_LOOS	0x60000000	/* First of OS specific semantics */
#define SHT_HIOS	0x6fffffff	/* Last of OS specific semantics */

#define SHT_GNU_ATTRIBUTES 0x6ffffff5	/* Object attributes */
#define SHT_GNU_HASH	0x6ffffff6	/* GNU style symbol hash table */
#define SHT_GNU_LIBLIST	0x6ffffff7	/* List of prelink dependencies */

/* The next three section types are defined by Solaris, and are named
   SHT_SUNW*.  We use them in GNU code, so we also define SHT_GNU*
   versions.  */
#define SHT_SUNW_verdef	0x6ffffffd	/* Versions defined by file */
#define SHT_SUNW_verneed 0x6ffffffe	/* Versions needed by file */
#define SHT_SUNW_versym	0x6fffffff	/* Symbol versions */

#define SHT_GNU_verdef	SHT_SUNW_verdef
#define SHT_GNU_verneed	SHT_SUNW_verneed
#define SHT_GNU_versym	SHT_SUNW_versym

#define SHT_LOPROC	0x70000000	/* Processor-specific semantics, lo */
#define SHT_HIPROC	0x7FFFFFFF	/* Processor-specific semantics, hi */
#define SHT_LOUSER	0x80000000	/* Application-specific semantics */
/* #define SHT_HIUSER	0x8FFFFFFF    *//* Application-specific semantics */
#define SHT_HIUSER	0xFFFFFFFF	/* New value, defined in Oct 4, 1999 Draft */

/* Values for section header, sh_flags field.  */

#define SHF_WRITE	(1 << 0)	/* Writable data during execution */
#define SHF_ALLOC	(1 << 1)	/* Occupies memory during execution */
#define SHF_EXECINSTR	(1 << 2)	/* Executable machine instructions */
#define SHF_MERGE	(1 << 4)	/* Data in this section can be merged */
#define SHF_STRINGS	(1 << 5)	/* Contains null terminated character strings */
#define SHF_INFO_LINK	(1 << 6)	/* sh_info holds section header table index */
#define SHF_LINK_ORDER	(1 << 7)	/* Preserve section ordering when linking */
#define SHF_OS_NONCONFORMING (1 << 8)	/* OS specific processing required */
#define SHF_GROUP	(1 << 9)	/* Member of a section group */
#define SHF_TLS		(1 << 10)	/* Thread local storage section */

/* #define SHF_MASKOS	0x0F000000    *//* OS-specific semantics */
#define SHF_MASKOS	0x0FF00000	/* New value, Oct 4, 1999 Draft */
#define SHF_MASKPROC	0xF0000000	/* Processor-specific semantics */

/* Values of note segment descriptor types for core files.  */

#define NT_PRSTATUS	1		/* Contains copy of prstatus struct */
#define NT_FPREGSET	2		/* Contains copy of fpregset struct */
#define NT_PRPSINFO	3		/* Contains copy of prpsinfo struct */
#define NT_TASKSTRUCT	4		/* Contains copy of task struct */
#define NT_AUXV		6		/* Contains copy of Elfxx_auxv_t */
#define NT_FILE		0x46494c45
#define NT_PRXFPREG	0x46e62b7f	/* Contains a user_xfpregs_struct; */
					/*   note name must be "LINUX".  */
#define NT_SIGINFO	0x53494749

/* Note segments for core files on dir-style procfs systems.  */

#define NT_PSTATUS	10		/* Has a struct pstatus */
#define NT_FPREGS	12		/* Has a struct fpregset */
#define NT_PSINFO	13		/* Has a struct psinfo */
#define NT_LWPSTATUS	16		/* Has a struct lwpstatus_t */
#define NT_LWPSINFO	17		/* Has a struct lwpsinfo_t */
#define NT_WIN32PSTATUS	18		/* Has a struct win32_pstatus */

/* Note segments for core files on FreeBSD systems.  Note name
   must start with "FreeBSD".  */
#define NT_THRMISC		7	/* Contains copy of thrmisc struct */
#define NT_PROCSTAT_PROC	8
#define NT_PROCSTAT_FILES	9
#define NT_PROCSTAT_VMMAP	10
#define NT_PROCSTAT_GROUPS	11
#define NT_PROCSTAT_UMASK	12
#define NT_PROCSTAT_RLIMIT	13
#define NT_PROCSTAT_OSREL	14
#define NT_PROCSTAT_PSSTRINGS	15
#define NT_PROCSTAT_AUXV	16
#define	NT_X86_XSTATE		0x202


/* Note segments for core files on NetBSD systems.  Note name
   must start with "NetBSD-CORE".  */

#define NT_NETBSDCORE_PROCINFO	1	/* Has a struct procinfo */
#define NT_NETBSDCORE_FIRSTMACH	32	/* start of machdep note types */


/* Values of note segment descriptor types for object files.  */

#define NT_VERSION	1		/* Contains a version string.  */
#define NT_ARCH		2		/* Contains an architecture string.  */

/* Values for GNU .note.ABI-tag notes.  Note name is "GNU".  */

#define NT_GNU_ABI_TAG		1
#define GNU_ABI_TAG_LINUX	0
#define GNU_ABI_TAG_HURD	1
#define GNU_ABI_TAG_SOLARIS	2
#define GNU_ABI_TAG_FREEBSD	3
#define GNU_ABI_TAG_NETBSD	4

/* Values for GNU .note.gnu.build-id notes.  Note name is "GNU"." */
#define NT_GNU_BUILD_ID		3

/* Values for NetBSD .note.netbsd.ident notes.  Note name is "NetBSD".  */

#define NT_NETBSD_IDENT		1

/* Values for OpenBSD .note.openbsd.ident notes.  Note name is "OpenBSD".  */

#define NT_OPENBSD_IDENT	1

/* Values for FreeBSD .note.ABI-tag notes.  Note name is "FreeBSD".  */

#define NT_FREEBSD_ABI_TAG	1

/* Values for FreeBSD .note.tag notes.  Note name is "FreeBSD".  */

#define NT_FREEBSD_TAG		1
#define NT_FREEBSD_NOINIT_TAG	2
#define NT_FREEBSD_ARCH_TAG	3

/* These three macros disassemble and assemble a symbol table st_info field,
   which contains the symbol binding and symbol type.  The STB_ and STT_
   defines identify the binding and type.  */

#define ELF_ST_BIND(val)		(((unsigned int)(val)) >> 4)
#define ELF_ST_TYPE(val)		((val) & 0xF)
#define ELF_ST_INFO(bind,type)		(((bind) << 4) + ((type) & 0xF))

/* The 64bit and 32bit versions of these macros are identical, but
   the ELF spec defines them, so here they are.  */
#define ELF32_ST_BIND  ELF_ST_BIND
#define ELF32_ST_TYPE  ELF_ST_TYPE
#define ELF32_ST_INFO  ELF_ST_INFO
#define ELF64_ST_BIND  ELF_ST_BIND
#define ELF64_ST_TYPE  ELF_ST_TYPE
#define ELF64_ST_INFO  ELF_ST_INFO

/* This macro disassembles and assembles a symbol's visibility into
   the st_other field.  The STV_ defines specify the actual visibility.  */

#define ELF_ST_VISIBILITY(v)		((v) & 0x3)
/* The remaining bits in the st_other field are not currently used.
   They should be set to zero.  */

#define ELF32_ST_VISIBILITY  ELF_ST_VISIBILITY
#define ELF64_ST_VISIBILITY  ELF_ST_VISIBILITY


#define STN_UNDEF	0		/* Undefined symbol index */

#define STB_LOCAL	0		/* Symbol not visible outside obj */
#define STB_GLOBAL	1		/* Symbol visible outside obj */
#define STB_WEAK	2		/* Like globals, lower precedence */
#define STB_LOOS	10		/* OS-specific semantics */
#define STB_HIOS	12		/* OS-specific semantics */
#define STB_LOPROC	13		/* Application-specific semantics */
#define STB_HIPROC	15		/* Application-specific semantics */

#define STT_NOTYPE	0		/* Symbol type is unspecified */
#define STT_OBJECT	1		/* Symbol is a data object */
#define STT_FUNC	2		/* Symbol is a code object */
#define STT_SECTION	3		/* Symbol associated with a section */
#define STT_FILE	4		/* Symbol gives a file name */
#define STT_COMMON	5		/* An uninitialised common block */
#define STT_TLS		6		/* Thread local data object */
#define STT_RELC        8               /* Complex relocation expression */
#define STT_SRELC       9               /* Signed Complex relocation expression */
#define STT_LOOS	10		/* OS-specific semantics */
#define STT_HIOS	12		/* OS-specific semantics */
#define STT_LOPROC	13		/* Application-specific semantics */
#define STT_HIPROC	15		/* Application-specific semantics */

/* Special section indices, which may show up in st_shndx fields, among
   other places.  */

#define SHN_UNDEF	0		/* Undefined section reference */
#define SHN_LORESERVE	0xFF00		/* Begin range of reserved indices */
#define SHN_LOPROC	0xFF00		/* Begin range of appl-specific */
#define SHN_HIPROC	0xFF1F		/* End range of appl-specific */
#define SHN_LOOS	0xFF20		/* OS specific semantics, lo */
#define SHN_HIOS	0xFF3F		/* OS specific semantics, hi */
#define SHN_ABS		0xFFF1		/* Associated symbol is absolute */
#define SHN_COMMON	0xFFF2		/* Associated symbol is in common */
#define SHN_XINDEX	0xFFFF		/* Section index is held elsewhere */
#define SHN_HIRESERVE	0xFFFF		/* End range of reserved indices */
#define SHN_BAD		((unsigned) -1) /* Used internally by bfd */

/* The following constants control how a symbol may be accessed once it has
   become part of an executable or shared library.  */

#define STV_DEFAULT	0		/* Visibility is specified by binding type */
#define STV_INTERNAL	1		/* OS specific version of STV_HIDDEN */
#define STV_HIDDEN	2		/* Can only be seen inside currect component */
#define STV_PROTECTED	3		/* Treat as STB_LOCAL inside current component */

/* Relocation info handling macros.  */

#define ELF32_R_SYM(i)		((i) >> 8)
#define ELF32_R_TYPE(i)		((i) & 0xff)
#define ELF32_R_INFO(s,t)	(((s) << 8) + ((t) & 0xff))

#define ELF64_R_SYM(i)		((i) >> 32)
#define ELF64_R_TYPE(i)		((i) & 0xffffffff)
#define ELF64_R_INFO(s,t)	(((bfd_vma) (s) << 31 << 1) + (bfd_vma) (t))

/* Dynamic section tags.  */

#define DT_NULL		0
#define DT_NEEDED	1
#define DT_PLTRELSZ	2
#define DT_PLTGOT	3
#define DT_HASH		4
#define DT_STRTAB	5
#define DT_SYMTAB	6
#define DT_RELA		7
#define DT_RELASZ	8
#define DT_RELAENT	9
#define DT_STRSZ	10
#define DT_SYMENT	11
#define DT_INIT		12
#define DT_FINI		13
#define DT_SONAME	14
#define DT_RPATH	15
#define DT_SYMBOLIC	16
#define DT_REL		17
#define DT_RELSZ	18
#define DT_RELENT	19
#define DT_PLTREL	20
#define DT_DEBUG	21
#define DT_TEXTREL	22
#define DT_JMPREL	23
#define DT_BIND_NOW	24
#define DT_INIT_ARRAY	25
#define DT_FINI_ARRAY	26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28
#define DT_RUNPATH	29
#define DT_FLAGS	30
#define DT_ENCODING	32
#define DT_PREINIT_ARRAY   32
#define DT_PREINIT_ARRAYSZ 33

/* Note, the Oct 4, 1999 draft of the ELF ABI changed the values
   for DT_LOOS and DT_HIOS.  Some implementations however, use
   values outside of the new range (see below).	 */
#define OLD_DT_LOOS	0x60000000
#define DT_LOOS		0x6000000d
#define DT_HIOS		0x6ffff000
#define OLD_DT_HIOS	0x6fffffff

#define DT_LOPROC	0x70000000
#define DT_HIPROC	0x7fffffff

/* The next four dynamic tags are used on Solaris.  We support them
   everywhere.	Note these values lie outside of the (new) range for
   OS specific values.	This is a deliberate special case and we
   maintain it for backwards compatability.  */
#define DT_VALRNGLO	0x6ffffd00
#define DT_GNU_PRELINKED 0x6ffffdf5
#define DT_GNU_CONFLICTSZ 0x6ffffdf6
#define DT_GNU_LIBLISTSZ 0x6ffffdf7
#define DT_CHECKSUM	0x6ffffdf8
#define DT_PLTPADSZ	0x6ffffdf9
#define DT_MOVEENT	0x6ffffdfa
#define DT_MOVESZ	0x6ffffdfb
#define DT_FEATURE	0x6ffffdfc
#define DT_POSFLAG_1	0x6ffffdfd
#define DT_SYMINSZ	0x6ffffdfe
#define DT_SYMINENT	0x6ffffdff
#define DT_VALRNGHI	0x6ffffdff

#define DT_ADDRRNGLO	0x6ffffe00
#define DT_GNU_HASH	0x6ffffef5
#define DT_TLSDESC_PLT	0x6ffffef6
#define DT_TLSDESC_GOT	0x6ffffef7
#define DT_GNU_CONFLICT	0x6ffffef8
#define DT_GNU_LIBLIST	0x6ffffef9
#define DT_CONFIG	0x6ffffefa
#define DT_DEPAUDIT	0x6ffffefb
#define DT_AUDIT	0x6ffffefc
#define DT_PLTPAD	0x6ffffefd
#define DT_MOVETAB	0x6ffffefe
#define DT_SYMINFO	0x6ffffeff
#define DT_ADDRRNGHI	0x6ffffeff

#define DT_RELACOUNT	0x6ffffff9
#define DT_RELCOUNT	0x6ffffffa
#define DT_FLAGS_1	0x6ffffffb
#define DT_VERDEF	0x6ffffffc
#define DT_VERDEFNUM	0x6ffffffd
#define DT_VERNEED	0x6ffffffe
#define DT_VERNEEDNUM	0x6fffffff

/* This tag is a GNU extension to the Solaris version scheme.  */
#define DT_VERSYM	0x6ffffff0

#define DT_LOPROC	0x70000000
#define DT_HIPROC	0x7fffffff

/* These section tags are used on Solaris.  We support them
   everywhere, and hope they do not conflict.  */

#define DT_AUXILIARY	0x7ffffffd
#define DT_USED		0x7ffffffe
#define DT_FILTER	0x7fffffff


/* Values used in DT_FEATURE .dynamic entry.  */
#define DTF_1_PARINIT	0x00000001
/* From

   http://docs.sun.com:80/ab2/coll.45.13/LLM/@Ab2PageView/21165?Ab2Lang=C&Ab2Enc=iso-8859-1

   DTF_1_CONFEXP is the same as DTF_1_PARINIT. It is a typo. The value
   defined here is the same as the one in <sys/link.h> on Solaris 8.  */
#define DTF_1_CONFEXP	0x00000002

/* Flag values used in the DT_POSFLAG_1 .dynamic entry.	 */
#define DF_P1_LAZYLOAD	0x00000001
#define DF_P1_GROUPPERM	0x00000002

/* Flag value in in the DT_FLAGS_1 .dynamic entry.  */
#define DF_1_NOW	0x00000001
#define DF_1_GLOBAL	0x00000002
#define DF_1_GROUP	0x00000004
#define DF_1_NODELETE	0x00000008
#define DF_1_LOADFLTR	0x00000010
#define DF_1_INITFIRST	0x00000020
#define DF_1_NOOPEN	0x00000040
#define DF_1_ORIGIN	0x00000080
#define DF_1_DIRECT	0x00000100
#define DF_1_TRANS	0x00000200
#define DF_1_INTERPOSE	0x00000400
#define DF_1_NODEFLIB	0x00000800
#define DF_1_NODUMP	0x00001000
#define DF_1_CONLFAT	0x00002000

/* Flag values for the DT_FLAGS entry.	*/
#define DF_ORIGIN	(1 << 0)
#define DF_SYMBOLIC	(1 << 1)
#define DF_TEXTREL	(1 << 2)
#define DF_BIND_NOW	(1 << 3)
#define DF_STATIC_TLS	(1 << 4)

/* These constants are used for the version number of a Elf32_Verdef
   structure.  */

#define VER_DEF_NONE		0
#define VER_DEF_CURRENT		1

/* These constants appear in the vd_flags field of a Elf32_Verdef
   structure.  */

#define VER_FLG_BASE		0x1
#define VER_FLG_WEAK		0x2

/* These special constants can be found in an Elf32_Versym field.  */

#define VER_NDX_LOCAL		0
#define VER_NDX_GLOBAL		1

/* These constants are used for the version number of a Elf32_Verneed
   structure.  */

#define VER_NEED_NONE		0
#define VER_NEED_CURRENT	1

/* This flag appears in a Versym structure.  It means that the symbol
   is hidden, and is only visible with an explicit version number.
   This is a GNU extension.  */

#define VERSYM_HIDDEN		0x8000

/* This is the mask for the rest of the Versym information.  */

#define VERSYM_VERSION		0x7fff

/* This is a special token which appears as part of a symbol name.  It
   indictes that the rest of the name is actually the name of a
   version node, and is not part of the actual name.  This is a GNU
   extension.  For example, the symbol name `stat@ver2' is taken to
   mean the symbol `stat' in version `ver2'.  */

#define ELF_VER_CHR	'@'

/* Possible values for si_boundto.  */

#define SYMINFO_BT_SELF		0xffff	/* Symbol bound to self */
#define SYMINFO_BT_PARENT	0xfffe	/* Symbol bound to parent */
#define SYMINFO_BT_LOWRESERVE	0xff00	/* Beginning of reserved entries */

/* Possible bitmasks for si_flags.  */

#define SYMINFO_FLG_DIRECT	0x0001	/* Direct bound symbol */
#define SYMINFO_FLG_PASSTHRU	0x0002	/* Pass-thru symbol for translator */
#define SYMINFO_FLG_COPY	0x0004	/* Symbol is a copy-reloc */
#define SYMINFO_FLG_LAZYLOAD	0x0008	/* Symbol bound to object to be lazy loaded */

/* Syminfo version values.  */

#define SYMINFO_NONE		0
#define SYMINFO_CURRENT		1
#define SYMINFO_NUM		2

/* Section Group Flags.	 */

#define GRP_COMDAT		0x1	/* A COMDAT group */

/* Auxv a_type values.  */

#define AT_NULL		0		/* End of vector */
#define AT_IGNORE	1		/* Entry should be ignored */
#define AT_EXECFD	2		/* File descriptor of program */
#define AT_PHDR		3		/* Program headers for program */
#define AT_PHENT	4		/* Size of program header entry */
#define AT_PHNUM	5		/* Number of program headers */
#define AT_PAGESZ	6		/* System page size */
#define AT_BASE		7		/* Base address of interpreter */
#define AT_FLAGS	8		/* Flags */
#define AT_ENTRY	9		/* Entry point of program */
#define AT_NOTELF	10		/* Program is not ELF */
#define AT_UID		11		/* Real uid */
#define AT_EUID		12		/* Effective uid */
#define AT_GID		13		/* Real gid */
#define AT_EGID		14		/* Effective gid */
#define AT_CLKTCK	17		/* Frequency of times() */
#define AT_PLATFORM	15		/* String identifying platform.  */
#define AT_HWCAP	16		/* Machine dependent hints about
					   processor capabilities.  */
#define AT_FPUCW	18		/* Used FPU control word.  */
#define AT_DCACHEBSIZE	19		/* Data cache block size.  */
#define AT_ICACHEBSIZE	20		/* Instruction cache block size.  */
#define AT_UCACHEBSIZE	21		/* Unified cache block size.  */
#define AT_IGNOREPPC	22		/* Entry should be ignored */
#define	AT_SECURE	23		/* Boolean, was exec setuid-like?  */
/* Pointer to the global system page used for system calls and other
   nice things.  */
#define AT_SYSINFO	32
#define AT_SYSINFO_EHDR	33 /* Pointer to ELF header of system-supplied DSO.  */

#define AT_SUN_UID      2000    /* Effective user ID.  */
#define AT_SUN_RUID     2001    /* Real user ID.  */
#define AT_SUN_GID      2002    /* Effective group ID.  */
#define AT_SUN_RGID     2003    /* Real group ID.  */
#define AT_SUN_LDELF    2004    /* Dynamic linker's ELF header.  */
#define AT_SUN_LDSHDR   2005    /* Dynamic linker's section headers.  */
#define AT_SUN_LDNAME   2006    /* String giving name of dynamic linker.  */
#define AT_SUN_LPAGESZ  2007    /* Large pagesize.   */
#define AT_SUN_PLATFORM 2008    /* Platform name string.  */
#define AT_SUN_HWCAP    2009	/* Machine dependent hints about
				   processor capabilities.  */
#define AT_SUN_IFLUSH   2010    /* Should flush icache? */
#define AT_SUN_CPU      2011    /* CPU name string.  */
#define AT_SUN_EMUL_ENTRY 2012	/* COFF entry point address.  */
#define AT_SUN_EMUL_EXECFD 2013	/* COFF executable file descriptor.  */
#define AT_SUN_EXECNAME 2014    /* Canonicalized file name given to execve.  */
#define AT_SUN_MMU      2015    /* String for name of MMU module.   */
#define AT_SUN_LDDATA   2016    /* Dynamic linker's data segment address.  */


#endif /* _ELF_COMMON_H */
