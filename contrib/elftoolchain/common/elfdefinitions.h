/*-
 * Copyright (c) 2010 Joseph Koshy
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: elfdefinitions.h 3515 2017-01-24 22:04:22Z emaste $
 */

/*
 * These definitions are based on:
 * - The public specification of the ELF format as defined in the
 *   October 2009 draft of System V ABI.
 *   See: http://www.sco.com/developers/gabi/latest/ch4.intro.html
 * - The May 1998 (version 1.5) draft of "The ELF-64 object format".
 * - Processor-specific ELF ABI definitions for sparc, i386, amd64, mips,
 *   ia64, and powerpc processors.
 * - The "Linkers and Libraries Guide", from Sun Microsystems.
 */

#ifndef _ELFDEFINITIONS_H_
#define _ELFDEFINITIONS_H_

#include <stdint.h>

/*
 * Types of capabilities.
 */

#define	_ELF_DEFINE_CAPABILITIES()				\
_ELF_DEFINE_CA(CA_SUNW_NULL,	0,	"ignored")		\
_ELF_DEFINE_CA(CA_SUNW_HW_1,	1,	"hardware capability")	\
_ELF_DEFINE_CA(CA_SUNW_SW_1,	2,	"software capability")

#undef	_ELF_DEFINE_CA
#define	_ELF_DEFINE_CA(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_CAPABILITIES()
	CA__LAST__
};

/*
 * Flags used with dynamic linking entries.
 */

#define	_ELF_DEFINE_DYN_FLAGS()					\
_ELF_DEFINE_DF(DF_ORIGIN,           0x1,			\
	"object being loaded may refer to $ORIGIN")		\
_ELF_DEFINE_DF(DF_SYMBOLIC,         0x2,			\
	"search library for references before executable")	\
_ELF_DEFINE_DF(DF_TEXTREL,          0x4,			\
	"relocation entries may modify text segment")		\
_ELF_DEFINE_DF(DF_BIND_NOW,         0x8,			\
	"process relocation entries at load time")		\
_ELF_DEFINE_DF(DF_STATIC_TLS,       0x10,			\
	"uses static thread-local storage")
#undef	_ELF_DEFINE_DF
#define	_ELF_DEFINE_DF(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_DYN_FLAGS()
	DF__LAST__
};


/*
 * Dynamic linking entry types.
 */

#define	_ELF_DEFINE_DYN_TYPES()						\
_ELF_DEFINE_DT(DT_NULL,             0, "end of array")			\
_ELF_DEFINE_DT(DT_NEEDED,           1, "names a needed library")	\
_ELF_DEFINE_DT(DT_PLTRELSZ,         2,					\
	"size in bytes of associated relocation entries")		\
_ELF_DEFINE_DT(DT_PLTGOT,           3,					\
	"address associated with the procedure linkage table")		\
_ELF_DEFINE_DT(DT_HASH,             4,					\
	"address of the symbol hash table")				\
_ELF_DEFINE_DT(DT_STRTAB,           5,					\
	"address of the string table")					\
_ELF_DEFINE_DT(DT_SYMTAB,           6,					\
	"address of the symbol table")					\
_ELF_DEFINE_DT(DT_RELA,             7,					\
	"address of the relocation table")				\
_ELF_DEFINE_DT(DT_RELASZ,           8, "size of the DT_RELA table")	\
_ELF_DEFINE_DT(DT_RELAENT,          9, "size of each DT_RELA entry")	\
_ELF_DEFINE_DT(DT_STRSZ,            10, "size of the string table")	\
_ELF_DEFINE_DT(DT_SYMENT,           11,					\
	"size of a symbol table entry")					\
_ELF_DEFINE_DT(DT_INIT,             12,					\
	"address of the initialization function")			\
_ELF_DEFINE_DT(DT_FINI,             13,					\
	"address of the finalization function")				\
_ELF_DEFINE_DT(DT_SONAME,           14, "names the shared object")	\
_ELF_DEFINE_DT(DT_RPATH,            15,					\
	"runtime library search path")					\
_ELF_DEFINE_DT(DT_SYMBOLIC,         16,					\
	"alter symbol resolution algorithm")				\
_ELF_DEFINE_DT(DT_REL,              17,					\
	"address of the DT_REL table")					\
_ELF_DEFINE_DT(DT_RELSZ,            18, "size of the DT_REL table")	\
_ELF_DEFINE_DT(DT_RELENT,           19, "size of each DT_REL entry")	\
_ELF_DEFINE_DT(DT_PLTREL,           20,					\
	"type of relocation entry in the procedure linkage table")	\
_ELF_DEFINE_DT(DT_DEBUG,            21, "used for debugging")		\
_ELF_DEFINE_DT(DT_TEXTREL,          22,					\
	"text segment may be written to during relocation")		\
_ELF_DEFINE_DT(DT_JMPREL,           23,					\
	"address of relocation entries associated with the procedure linkage table") \
_ELF_DEFINE_DT(DT_BIND_NOW,         24,					\
	"bind symbols at loading time")					\
_ELF_DEFINE_DT(DT_INIT_ARRAY,       25,					\
	"pointers to initialization functions")				\
_ELF_DEFINE_DT(DT_FINI_ARRAY,       26,					\
	"pointers to termination functions")				\
_ELF_DEFINE_DT(DT_INIT_ARRAYSZ,     27, "size of the DT_INIT_ARRAY")	\
_ELF_DEFINE_DT(DT_FINI_ARRAYSZ,     28, "size of the DT_FINI_ARRAY")	\
_ELF_DEFINE_DT(DT_RUNPATH,          29,					\
	"index of library search path string")				\
_ELF_DEFINE_DT(DT_FLAGS,            30,					\
	"flags specific to the object being loaded")			\
_ELF_DEFINE_DT(DT_ENCODING,         32, "standard semantics")		\
_ELF_DEFINE_DT(DT_PREINIT_ARRAY,    32,					\
	"pointers to pre-initialization functions")			\
_ELF_DEFINE_DT(DT_PREINIT_ARRAYSZ,  33,					\
	"size of pre-initialization array")				\
_ELF_DEFINE_DT(DT_MAXPOSTAGS,	    34,					\
	"the number of positive tags")					\
_ELF_DEFINE_DT(DT_LOOS,             0x6000000DUL,			\
	"start of OS-specific types")					\
_ELF_DEFINE_DT(DT_SUNW_AUXILIARY,   0x6000000DUL,			\
	"offset of string naming auxiliary filtees")			\
_ELF_DEFINE_DT(DT_SUNW_RTLDINF,     0x6000000EUL, "rtld internal use")	\
_ELF_DEFINE_DT(DT_SUNW_FILTER,      0x6000000FUL,			\
	"offset of string naming standard filtees")			\
_ELF_DEFINE_DT(DT_SUNW_CAP,         0x60000010UL,			\
	"address of hardware capabilities section")			\
_ELF_DEFINE_DT(DT_SUNW_ASLR,        0x60000023UL,			\
	"Address Space Layout Randomization flag")			\
_ELF_DEFINE_DT(DT_HIOS,             0x6FFFF000UL,			\
	"end of OS-specific types")					\
_ELF_DEFINE_DT(DT_VALRNGLO,         0x6FFFFD00UL,			\
	"start of range using the d_val field")				\
_ELF_DEFINE_DT(DT_GNU_PRELINKED,    0x6FFFFDF5UL,			\
	"prelinking timestamp")						\
_ELF_DEFINE_DT(DT_GNU_CONFLICTSZ,   0x6FFFFDF6UL,			\
	"size of conflict section")					\
_ELF_DEFINE_DT(DT_GNU_LIBLISTSZ,    0x6FFFFDF7UL,			\
	"size of library list")						\
_ELF_DEFINE_DT(DT_CHECKSUM,         0x6FFFFDF8UL,			\
	"checksum for the object")					\
_ELF_DEFINE_DT(DT_PLTPADSZ,         0x6FFFFDF9UL,			\
	"size of PLT padding")						\
_ELF_DEFINE_DT(DT_MOVEENT,          0x6FFFFDFAUL,			\
	"size of DT_MOVETAB entries")					\
_ELF_DEFINE_DT(DT_MOVESZ,           0x6FFFFDFBUL,			\
	"total size of the MOVETAB table")				\
_ELF_DEFINE_DT(DT_FEATURE,          0x6FFFFDFCUL, "feature values")	\
_ELF_DEFINE_DT(DT_POSFLAG_1,        0x6FFFFDFDUL,			\
	"dynamic position flags")					\
_ELF_DEFINE_DT(DT_SYMINSZ,          0x6FFFFDFEUL,			\
	"size of the DT_SYMINFO table")					\
_ELF_DEFINE_DT(DT_SYMINENT,         0x6FFFFDFFUL,			\
	"size of a DT_SYMINFO entry")					\
_ELF_DEFINE_DT(DT_VALRNGHI,         0x6FFFFDFFUL,			\
	"end of range using the d_val field")				\
_ELF_DEFINE_DT(DT_ADDRRNGLO,        0x6FFFFE00UL,			\
	"start of range using the d_ptr field")				\
_ELF_DEFINE_DT(DT_GNU_HASH,	    0x6FFFFEF5UL,			\
	"GNU style hash tables")					\
_ELF_DEFINE_DT(DT_TLSDESC_PLT,	    0x6FFFFEF6UL,			\
	"location of PLT entry for TLS descriptor resolver calls")	\
_ELF_DEFINE_DT(DT_TLSDESC_GOT,	    0x6FFFFEF7UL,			\
	"location of GOT entry used by TLS descriptor resolver PLT entry") \
_ELF_DEFINE_DT(DT_GNU_CONFLICT,     0x6FFFFEF8UL,			\
	"address of conflict section")					\
_ELF_DEFINE_DT(DT_GNU_LIBLIST,      0x6FFFFEF9UL,			\
	"address of conflict section")					\
_ELF_DEFINE_DT(DT_CONFIG,           0x6FFFFEFAUL,			\
	"configuration file")						\
_ELF_DEFINE_DT(DT_DEPAUDIT,         0x6FFFFEFBUL,			\
	"string defining audit libraries")				\
_ELF_DEFINE_DT(DT_AUDIT,            0x6FFFFEFCUL,			\
	"string defining audit libraries")				\
_ELF_DEFINE_DT(DT_PLTPAD,           0x6FFFFEFDUL, "PLT padding")	\
_ELF_DEFINE_DT(DT_MOVETAB,          0x6FFFFEFEUL,			\
	"address of a move table")					\
_ELF_DEFINE_DT(DT_SYMINFO,          0x6FFFFEFFUL,			\
	"address of the symbol information table")			\
_ELF_DEFINE_DT(DT_ADDRRNGHI,        0x6FFFFEFFUL,			\
	"end of range using the d_ptr field")				\
_ELF_DEFINE_DT(DT_VERSYM,	    0x6FFFFFF0UL,			\
	"address of the version section")				\
_ELF_DEFINE_DT(DT_RELACOUNT,        0x6FFFFFF9UL,			\
	"count of RELA relocations")					\
_ELF_DEFINE_DT(DT_RELCOUNT,         0x6FFFFFFAUL,			\
	"count of REL relocations")					\
_ELF_DEFINE_DT(DT_FLAGS_1,          0x6FFFFFFBUL, "flag values")	\
_ELF_DEFINE_DT(DT_VERDEF,	    0x6FFFFFFCUL,			\
	"address of the version definition segment")			\
_ELF_DEFINE_DT(DT_VERDEFNUM,	    0x6FFFFFFDUL,			\
	"the number of version definition entries")			\
_ELF_DEFINE_DT(DT_VERNEED,	    0x6FFFFFFEUL,			\
	"address of section with needed versions")			\
_ELF_DEFINE_DT(DT_VERNEEDNUM,       0x6FFFFFFFUL,			\
	"the number of version needed entries")				\
_ELF_DEFINE_DT(DT_LOPROC,           0x70000000UL,			\
	"start of processor-specific types")				\
_ELF_DEFINE_DT(DT_ARM_SYMTABSZ,	    0x70000001UL,			\
	"number of entries in the dynamic symbol table")		\
_ELF_DEFINE_DT(DT_SPARC_REGISTER,   0x70000001UL,			\
	"index of an STT_SPARC_REGISTER symbol")			\
_ELF_DEFINE_DT(DT_ARM_PREEMPTMAP,   0x70000002UL,			\
	"address of the preemption map")				\
_ELF_DEFINE_DT(DT_MIPS_RLD_VERSION, 0x70000001UL,			\
	"version ID for runtime linker interface")			\
_ELF_DEFINE_DT(DT_MIPS_TIME_STAMP,  0x70000002UL,			\
	"timestamp")							\
_ELF_DEFINE_DT(DT_MIPS_ICHECKSUM,   0x70000003UL,			\
	"checksum of all external strings and common sizes")		\
_ELF_DEFINE_DT(DT_MIPS_IVERSION,    0x70000004UL,			\
	"string table index of a version string")			\
_ELF_DEFINE_DT(DT_MIPS_FLAGS,       0x70000005UL,			\
	"MIPS-specific flags")						\
_ELF_DEFINE_DT(DT_MIPS_BASE_ADDRESS, 0x70000006UL,			\
	"base address for the executable/DSO")				\
_ELF_DEFINE_DT(DT_MIPS_CONFLICT,    0x70000008UL,			\
	"address of .conflict section")					\
_ELF_DEFINE_DT(DT_MIPS_LIBLIST,     0x70000009UL,			\
	"address of .liblist section")					\
_ELF_DEFINE_DT(DT_MIPS_LOCAL_GOTNO, 0x7000000AUL,			\
	"number of local GOT entries")					\
_ELF_DEFINE_DT(DT_MIPS_CONFLICTNO,  0x7000000BUL,			\
	"number of entries in the .conflict section")			\
_ELF_DEFINE_DT(DT_MIPS_LIBLISTNO,   0x70000010UL,			\
	"number of entries in the .liblist section")			\
_ELF_DEFINE_DT(DT_MIPS_SYMTABNO,    0x70000011UL,			\
	"number of entries in the .dynsym section")			\
_ELF_DEFINE_DT(DT_MIPS_UNREFEXTNO,  0x70000012UL,			\
	"index of first external dynamic symbol not ref'ed locally")	\
_ELF_DEFINE_DT(DT_MIPS_GOTSYM,      0x70000013UL,			\
	"index of first dynamic symbol corresponds to a GOT entry")	\
_ELF_DEFINE_DT(DT_MIPS_HIPAGENO,    0x70000014UL,			\
	"number of page table entries in GOT")				\
_ELF_DEFINE_DT(DT_MIPS_RLD_MAP,     0x70000016UL,			\
	"address of runtime linker map")				\
_ELF_DEFINE_DT(DT_MIPS_DELTA_CLASS, 0x70000017UL,			\
	"Delta C++ class definition")					\
_ELF_DEFINE_DT(DT_MIPS_DELTA_CLASS_NO, 0x70000018UL,			\
	"number of entries in DT_MIPS_DELTA_CLASS")			\
_ELF_DEFINE_DT(DT_MIPS_DELTA_INSTANCE, 0x70000019UL,			\
	"Delta C++ class instances")					\
_ELF_DEFINE_DT(DT_MIPS_DELTA_INSTANCE_NO, 0x7000001AUL,			\
	"number of entries in DT_MIPS_DELTA_INSTANCE")			\
_ELF_DEFINE_DT(DT_MIPS_DELTA_RELOC, 0x7000001BUL,			\
	"Delta relocations")						\
_ELF_DEFINE_DT(DT_MIPS_DELTA_RELOC_NO, 0x7000001CUL,			\
	"number of entries in DT_MIPS_DELTA_RELOC")			\
_ELF_DEFINE_DT(DT_MIPS_DELTA_SYM,   0x7000001DUL,			\
	"Delta symbols referred by Delta relocations")			\
_ELF_DEFINE_DT(DT_MIPS_DELTA_SYM_NO, 0x7000001EUL,			\
	"number of entries in DT_MIPS_DELTA_SYM")			\
_ELF_DEFINE_DT(DT_MIPS_DELTA_CLASSSYM, 0x70000020UL,			\
	"Delta symbols for class declarations")				\
_ELF_DEFINE_DT(DT_MIPS_DELTA_CLASSSYM_NO, 0x70000021UL,			\
	"number of entries in DT_MIPS_DELTA_CLASSSYM")			\
_ELF_DEFINE_DT(DT_MIPS_CXX_FLAGS,   0x70000022UL,			\
	"C++ flavor flags")						\
_ELF_DEFINE_DT(DT_MIPS_PIXIE_INIT,  0x70000023UL,			\
	"address of an initialization routine created by pixie")	\
_ELF_DEFINE_DT(DT_MIPS_SYMBOL_LIB,  0x70000024UL,			\
	"address of .MIPS.symlib section")				\
_ELF_DEFINE_DT(DT_MIPS_LOCALPAGE_GOTIDX, 0x70000025UL,			\
	"GOT index of first page table entry for a segment")		\
_ELF_DEFINE_DT(DT_MIPS_LOCAL_GOTIDX, 0x70000026UL,			\
	"GOT index of first page table entry for a local symbol")	\
_ELF_DEFINE_DT(DT_MIPS_HIDDEN_GOTIDX, 0x70000027UL,			\
	"GOT index of first page table entry for a hidden symbol")	\
_ELF_DEFINE_DT(DT_MIPS_PROTECTED_GOTIDX, 0x70000028UL,			\
	"GOT index of first page table entry for a protected symbol")	\
_ELF_DEFINE_DT(DT_MIPS_OPTIONS,     0x70000029UL,			\
	"address of .MIPS.options section")				\
_ELF_DEFINE_DT(DT_MIPS_INTERFACE,   0x7000002AUL,			\
	"address of .MIPS.interface section")				\
_ELF_DEFINE_DT(DT_MIPS_DYNSTR_ALIGN, 0x7000002BUL, "???")		\
_ELF_DEFINE_DT(DT_MIPS_INTERFACE_SIZE, 0x7000002CUL,			\
	"size of .MIPS.interface section")				\
_ELF_DEFINE_DT(DT_MIPS_RLD_TEXT_RESOLVE_ADDR, 0x7000002DUL,		\
	"address of _rld_text_resolve in GOT")				\
_ELF_DEFINE_DT(DT_MIPS_PERF_SUFFIX, 0x7000002EUL,			\
	"default suffix of DSO to be appended by dlopen")		\
_ELF_DEFINE_DT(DT_MIPS_COMPACT_SIZE, 0x7000002FUL,			\
	"size of a ucode compact relocation record (o32)")		\
_ELF_DEFINE_DT(DT_MIPS_GP_VALUE,    0x70000030UL,			\
	"GP value of a specified GP relative range")			\
_ELF_DEFINE_DT(DT_MIPS_AUX_DYNAMIC, 0x70000031UL,			\
	"address of an auxiliary dynamic table")			\
_ELF_DEFINE_DT(DT_MIPS_PLTGOT,      0x70000032UL,			\
	"address of the PLTGOT")					\
_ELF_DEFINE_DT(DT_MIPS_RLD_OBJ_UPDATE, 0x70000033UL,			\
	"object list update callback")					\
_ELF_DEFINE_DT(DT_MIPS_RWPLT,       0x70000034UL,			\
	"address of a writable PLT")					\
_ELF_DEFINE_DT(DT_PPC_GOT,          0x70000000UL,			\
	"value of _GLOBAL_OFFSET_TABLE_")				\
_ELF_DEFINE_DT(DT_PPC_TLSOPT,       0x70000001UL,			\
	"TLS descriptor should be optimized")				\
_ELF_DEFINE_DT(DT_PPC64_GLINK,      0x70000000UL,			\
	"address of .glink section")					\
_ELF_DEFINE_DT(DT_PPC64_OPD,        0x70000001UL,			\
	"address of .opd section")					\
_ELF_DEFINE_DT(DT_PPC64_OPDSZ,      0x70000002UL,			\
	"size of .opd section")						\
_ELF_DEFINE_DT(DT_PPC64_TLSOPT,     0x70000003UL,			\
	"TLS descriptor should be optimized")				\
_ELF_DEFINE_DT(DT_AUXILIARY,        0x7FFFFFFDUL,			\
	"offset of string naming auxiliary filtees")			\
_ELF_DEFINE_DT(DT_USED,             0x7FFFFFFEUL, "ignored")		\
_ELF_DEFINE_DT(DT_FILTER,           0x7FFFFFFFUL,			\
	"index of string naming filtees")				\
_ELF_DEFINE_DT(DT_HIPROC,           0x7FFFFFFFUL,			\
	"end of processor-specific types")

#undef	_ELF_DEFINE_DT
#define	_ELF_DEFINE_DT(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_DYN_TYPES()
	DT__LAST__ = DT_HIPROC
};

#define	DT_DEPRECATED_SPARC_REGISTER	DT_SPARC_REGISTER

/*
 * Flags used in the executable header (field: e_flags).
 */
#define	_ELF_DEFINE_EHDR_FLAGS()					\
_ELF_DEFINE_EF(EF_ARM_RELEXEC,      0x00000001UL,			\
	"dynamic segment describes only how to relocate segments")	\
_ELF_DEFINE_EF(EF_ARM_HASENTRY,     0x00000002UL,			\
	"e_entry contains a program entry point")			\
_ELF_DEFINE_EF(EF_ARM_SYMSARESORTED, 0x00000004UL,			\
	"subsection of symbol table is sorted by symbol value")		\
_ELF_DEFINE_EF(EF_ARM_DYNSYMSUSESEGIDX, 0x00000008UL,			\
	"dynamic symbol st_shndx = containing segment index + 1")	\
_ELF_DEFINE_EF(EF_ARM_MAPSYMSFIRST, 0x00000010UL,			\
	"mapping symbols precede other local symbols in symtab")	\
_ELF_DEFINE_EF(EF_ARM_BE8,          0x00800000UL,			\
	"file contains BE-8 code")					\
_ELF_DEFINE_EF(EF_ARM_LE8,          0x00400000UL,			\
	"file contains LE-8 code")					\
_ELF_DEFINE_EF(EF_ARM_EABIMASK,     0xFF000000UL,			\
	"mask for ARM EABI version number (0 denotes GNU or unknown)")	\
_ELF_DEFINE_EF(EF_ARM_EABI_UNKNOWN, 0x00000000UL,			\
	"Unknown or GNU ARM EABI version number")			\
_ELF_DEFINE_EF(EF_ARM_EABI_VER1,    0x01000000UL,			\
	"ARM EABI version 1")						\
_ELF_DEFINE_EF(EF_ARM_EABI_VER2,    0x02000000UL,			\
	"ARM EABI version 2")						\
_ELF_DEFINE_EF(EF_ARM_EABI_VER3,    0x03000000UL,			\
	"ARM EABI version 3")						\
_ELF_DEFINE_EF(EF_ARM_EABI_VER4,    0x04000000UL,			\
	"ARM EABI version 4")						\
_ELF_DEFINE_EF(EF_ARM_EABI_VER5,    0x05000000UL,			\
	"ARM EABI version 5")						\
_ELF_DEFINE_EF(EF_ARM_INTERWORK,    0x00000004UL,			\
	"GNU EABI extension")						\
_ELF_DEFINE_EF(EF_ARM_APCS_26,      0x00000008UL,			\
	"GNU EABI extension")						\
_ELF_DEFINE_EF(EF_ARM_APCS_FLOAT,   0x00000010UL,			\
	"GNU EABI extension")						\
_ELF_DEFINE_EF(EF_ARM_PIC,          0x00000020UL,			\
	"GNU EABI extension")						\
_ELF_DEFINE_EF(EF_ARM_ALIGN8,       0x00000040UL,			\
	"GNU EABI extension")						\
_ELF_DEFINE_EF(EF_ARM_NEW_ABI,      0x00000080UL,			\
	"GNU EABI extension")						\
_ELF_DEFINE_EF(EF_ARM_OLD_ABI,      0x00000100UL,			\
	"GNU EABI extension")						\
_ELF_DEFINE_EF(EF_ARM_SOFT_FLOAT,   0x00000200UL,			\
	"GNU EABI extension")						\
_ELF_DEFINE_EF(EF_ARM_VFP_FLOAT,    0x00000400UL,			\
	"GNU EABI extension")						\
_ELF_DEFINE_EF(EF_ARM_MAVERICK_FLOAT, 0x00000800UL,			\
	"GNU EABI extension")						\
_ELF_DEFINE_EF(EF_MIPS_NOREORDER,   0x00000001UL,			\
	"at least one .noreorder directive appeared in the source")	\
_ELF_DEFINE_EF(EF_MIPS_PIC,         0x00000002UL,			\
	"file contains position independent code")			\
_ELF_DEFINE_EF(EF_MIPS_CPIC,        0x00000004UL,			\
	"file's code uses standard conventions for calling PIC")	\
_ELF_DEFINE_EF(EF_MIPS_UCODE,       0x00000010UL,			\
	"file contains UCODE (obsolete)")				\
_ELF_DEFINE_EF(EF_MIPS_ABI2,        0x00000020UL,			\
	"file follows MIPS III 32-bit ABI")				\
_ELF_DEFINE_EF(EF_MIPS_OPTIONS_FIRST, 0x00000080UL,			\
	"ld(1) should process .MIPS.options section first")		\
_ELF_DEFINE_EF(EF_MIPS_ARCH_ASE,    0x0F000000UL,			\
	"file uses application-specific architectural extensions")	\
_ELF_DEFINE_EF(EF_MIPS_ARCH_ASE_MDMX, 0x08000000UL,			\
	"file uses MDMX multimedia extensions")				\
_ELF_DEFINE_EF(EF_MIPS_ARCH_ASE_M16, 0x04000000UL,			\
	"file uses MIPS-16 ISA extensions")				\
_ELF_DEFINE_EF(EF_MIPS_ARCH,         0xF0000000UL,			\
	"4-bit MIPS architecture field")				\
_ELF_DEFINE_EF(EF_PPC_EMB,          0x80000000UL,			\
	"Embedded PowerPC flag")					\
_ELF_DEFINE_EF(EF_PPC_RELOCATABLE,  0x00010000UL,			\
	"-mrelocatable flag")						\
_ELF_DEFINE_EF(EF_PPC_RELOCATABLE_LIB, 0x00008000UL,			\
	"-mrelocatable-lib flag")					\
_ELF_DEFINE_EF(EF_SPARC_EXT_MASK,   0x00ffff00UL,			\
	"Vendor Extension mask")					\
_ELF_DEFINE_EF(EF_SPARC_32PLUS,     0x00000100UL,			\
	"Generic V8+ features")						\
_ELF_DEFINE_EF(EF_SPARC_SUN_US1,    0x00000200UL,			\
	"Sun UltraSPARCTM 1 Extensions")				\
_ELF_DEFINE_EF(EF_SPARC_HAL_R1,     0x00000400UL, "HAL R1 Extensions")	\
_ELF_DEFINE_EF(EF_SPARC_SUN_US3,    0x00000800UL,			\
	"Sun UltraSPARC 3 Extensions")					\
_ELF_DEFINE_EF(EF_SPARCV9_MM,       0x00000003UL,			\
	"Mask for Memory Model")					\
_ELF_DEFINE_EF(EF_SPARCV9_TSO,      0x00000000UL,			\
	"Total Store Ordering")						\
_ELF_DEFINE_EF(EF_SPARCV9_PSO,      0x00000001UL,			\
	"Partial Store Ordering")					\
_ELF_DEFINE_EF(EF_SPARCV9_RMO,      0x00000002UL,			\
	"Relaxed Memory Ordering")

#undef	_ELF_DEFINE_EF
#define	_ELF_DEFINE_EF(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_EHDR_FLAGS()
	EF__LAST__
};

/*
 * Offsets in the `ei_ident[]` field of an ELF executable header.
 */
#define	_ELF_DEFINE_EI_OFFSETS()			\
_ELF_DEFINE_EI(EI_MAG0,     0, "magic number")		\
_ELF_DEFINE_EI(EI_MAG1,     1, "magic number")		\
_ELF_DEFINE_EI(EI_MAG2,     2, "magic number")		\
_ELF_DEFINE_EI(EI_MAG3,     3, "magic number")		\
_ELF_DEFINE_EI(EI_CLASS,    4, "file class")		\
_ELF_DEFINE_EI(EI_DATA,     5, "data encoding")		\
_ELF_DEFINE_EI(EI_VERSION,  6, "file version")		\
_ELF_DEFINE_EI(EI_OSABI,    7, "OS ABI kind")		\
_ELF_DEFINE_EI(EI_ABIVERSION, 8, "OS ABI version")	\
_ELF_DEFINE_EI(EI_PAD,	    9, "padding start")		\
_ELF_DEFINE_EI(EI_NIDENT,  16, "total size")

#undef	_ELF_DEFINE_EI
#define	_ELF_DEFINE_EI(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_EI_OFFSETS()
	EI__LAST__
};

/*
 * The ELF class of an object.
 */
#define	_ELF_DEFINE_ELFCLASS()				\
_ELF_DEFINE_EC(ELFCLASSNONE, 0, "Unknown ELF class")	\
_ELF_DEFINE_EC(ELFCLASS32,   1, "32 bit objects")	\
_ELF_DEFINE_EC(ELFCLASS64,   2, "64 bit objects")

#undef	_ELF_DEFINE_EC
#define	_ELF_DEFINE_EC(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_ELFCLASS()
	EC__LAST__
};

/*
 * Endianness of data in an ELF object.
 */

#define	_ELF_DEFINE_ELF_DATA_ENDIANNESS()			\
_ELF_DEFINE_ED(ELFDATANONE, 0, "Unknown data endianness")	\
_ELF_DEFINE_ED(ELFDATA2LSB, 1, "little endian")			\
_ELF_DEFINE_ED(ELFDATA2MSB, 2, "big endian")

#undef	_ELF_DEFINE_ED
#define	_ELF_DEFINE_ED(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_ELF_DATA_ENDIANNESS()
	ED__LAST__
};

/*
 * Values of the magic numbers used in identification array.
 */
#define	_ELF_DEFINE_ELF_MAGIC()			\
_ELF_DEFINE_EMAG(ELFMAG0, 0x7FU)		\
_ELF_DEFINE_EMAG(ELFMAG1, 'E')			\
_ELF_DEFINE_EMAG(ELFMAG2, 'L')			\
_ELF_DEFINE_EMAG(ELFMAG3, 'F')

#undef	_ELF_DEFINE_EMAG
#define	_ELF_DEFINE_EMAG(N, V)		N = V ,
enum {
	_ELF_DEFINE_ELF_MAGIC()
	ELFMAG__LAST__
};

/*
 * ELF OS ABI field.
 */
#define	_ELF_DEFINE_ELF_OSABI()						\
_ELF_DEFINE_EABI(ELFOSABI_NONE,       0,				\
	"No extensions or unspecified")					\
_ELF_DEFINE_EABI(ELFOSABI_SYSV,       0, "SYSV")			\
_ELF_DEFINE_EABI(ELFOSABI_HPUX,       1, "Hewlett-Packard HP-UX")	\
_ELF_DEFINE_EABI(ELFOSABI_NETBSD,     2, "NetBSD")			\
_ELF_DEFINE_EABI(ELFOSABI_GNU,        3, "GNU")				\
_ELF_DEFINE_EABI(ELFOSABI_HURD,       4, "GNU/HURD")			\
_ELF_DEFINE_EABI(ELFOSABI_86OPEN,     5, "86Open Common ABI")		\
_ELF_DEFINE_EABI(ELFOSABI_SOLARIS,    6, "Sun Solaris")			\
_ELF_DEFINE_EABI(ELFOSABI_AIX,        7, "AIX")				\
_ELF_DEFINE_EABI(ELFOSABI_IRIX,       8, "IRIX")			\
_ELF_DEFINE_EABI(ELFOSABI_FREEBSD,    9, "FreeBSD")			\
_ELF_DEFINE_EABI(ELFOSABI_TRU64,      10, "Compaq TRU64 UNIX")		\
_ELF_DEFINE_EABI(ELFOSABI_MODESTO,    11, "Novell Modesto")		\
_ELF_DEFINE_EABI(ELFOSABI_OPENBSD,    12, "Open BSD")			\
_ELF_DEFINE_EABI(ELFOSABI_OPENVMS,    13, "Open VMS")			\
_ELF_DEFINE_EABI(ELFOSABI_NSK,        14,				\
	"Hewlett-Packard Non-Stop Kernel")				\
_ELF_DEFINE_EABI(ELFOSABI_AROS,       15, "Amiga Research OS")		\
_ELF_DEFINE_EABI(ELFOSABI_FENIXOS,    16,				\
	"The FenixOS highly scalable multi-core OS")			\
_ELF_DEFINE_EABI(ELFOSABI_CLOUDABI,   17, "Nuxi CloudABI")		\
_ELF_DEFINE_EABI(ELFOSABI_ARM_AEABI,  64,				\
	"ARM specific symbol versioning extensions")			\
_ELF_DEFINE_EABI(ELFOSABI_ARM,        97, "ARM ABI")			\
_ELF_DEFINE_EABI(ELFOSABI_STANDALONE, 255,				\
	"Standalone (embedded) application")

#undef	_ELF_DEFINE_EABI
#define	_ELF_DEFINE_EABI(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_ELF_OSABI()
	ELFOSABI__LAST__
};

#define	ELFOSABI_LINUX			ELFOSABI_GNU

/*
 * ELF Machine types: (EM_*).
 */
#define	_ELF_DEFINE_ELF_MACHINES()					\
_ELF_DEFINE_EM(EM_NONE,             0, "No machine")			\
_ELF_DEFINE_EM(EM_M32,              1, "AT&T WE 32100")			\
_ELF_DEFINE_EM(EM_SPARC,            2, "SPARC")				\
_ELF_DEFINE_EM(EM_386,              3, "Intel 80386")			\
_ELF_DEFINE_EM(EM_68K,              4, "Motorola 68000")		\
_ELF_DEFINE_EM(EM_88K,              5, "Motorola 88000")		\
_ELF_DEFINE_EM(EM_IAMCU,            6, "Intel MCU")			\
_ELF_DEFINE_EM(EM_860,              7, "Intel 80860")			\
_ELF_DEFINE_EM(EM_MIPS,             8, "MIPS I Architecture")		\
_ELF_DEFINE_EM(EM_S370,             9, "IBM System/370 Processor")	\
_ELF_DEFINE_EM(EM_MIPS_RS3_LE,      10, "MIPS RS3000 Little-endian")	\
_ELF_DEFINE_EM(EM_PARISC,           15, "Hewlett-Packard PA-RISC")	\
_ELF_DEFINE_EM(EM_VPP500,           17, "Fujitsu VPP500")		\
_ELF_DEFINE_EM(EM_SPARC32PLUS,      18,					\
	"Enhanced instruction set SPARC")				\
_ELF_DEFINE_EM(EM_960,              19, "Intel 80960")			\
_ELF_DEFINE_EM(EM_PPC,              20, "PowerPC")			\
_ELF_DEFINE_EM(EM_PPC64,            21, "64-bit PowerPC")		\
_ELF_DEFINE_EM(EM_S390,             22, "IBM System/390 Processor")	\
_ELF_DEFINE_EM(EM_SPU,              23, "IBM SPU/SPC")			\
_ELF_DEFINE_EM(EM_V800,             36, "NEC V800")			\
_ELF_DEFINE_EM(EM_FR20,             37, "Fujitsu FR20")			\
_ELF_DEFINE_EM(EM_RH32,             38, "TRW RH-32")			\
_ELF_DEFINE_EM(EM_RCE,              39, "Motorola RCE")			\
_ELF_DEFINE_EM(EM_ARM,              40, "Advanced RISC Machines ARM")	\
_ELF_DEFINE_EM(EM_ALPHA,            41, "Digital Alpha")		\
_ELF_DEFINE_EM(EM_SH,               42, "Hitachi SH")			\
_ELF_DEFINE_EM(EM_SPARCV9,          43, "SPARC Version 9")		\
_ELF_DEFINE_EM(EM_TRICORE,          44,					\
	"Siemens TriCore embedded processor")				\
_ELF_DEFINE_EM(EM_ARC,              45,					\
	"Argonaut RISC Core, Argonaut Technologies Inc.")		\
_ELF_DEFINE_EM(EM_H8_300,           46, "Hitachi H8/300")		\
_ELF_DEFINE_EM(EM_H8_300H,          47, "Hitachi H8/300H")		\
_ELF_DEFINE_EM(EM_H8S,              48, "Hitachi H8S")			\
_ELF_DEFINE_EM(EM_H8_500,           49, "Hitachi H8/500")		\
_ELF_DEFINE_EM(EM_IA_64,            50,					\
	"Intel IA-64 processor architecture")				\
_ELF_DEFINE_EM(EM_MIPS_X,           51, "Stanford MIPS-X")		\
_ELF_DEFINE_EM(EM_COLDFIRE,         52, "Motorola ColdFire")		\
_ELF_DEFINE_EM(EM_68HC12,           53, "Motorola M68HC12")		\
_ELF_DEFINE_EM(EM_MMA,              54,					\
	"Fujitsu MMA Multimedia Accelerator")				\
_ELF_DEFINE_EM(EM_PCP,              55, "Siemens PCP")			\
_ELF_DEFINE_EM(EM_NCPU,             56,					\
	"Sony nCPU embedded RISC processor")				\
_ELF_DEFINE_EM(EM_NDR1,             57, "Denso NDR1 microprocessor")	\
_ELF_DEFINE_EM(EM_STARCORE,         58, "Motorola Star*Core processor")	\
_ELF_DEFINE_EM(EM_ME16,             59, "Toyota ME16 processor")	\
_ELF_DEFINE_EM(EM_ST100,            60,					\
	"STMicroelectronics ST100 processor")				\
_ELF_DEFINE_EM(EM_TINYJ,            61,					\
	"Advanced Logic Corp. TinyJ embedded processor family")		\
_ELF_DEFINE_EM(EM_X86_64,           62, "AMD x86-64 architecture")	\
_ELF_DEFINE_EM(EM_PDSP,             63, "Sony DSP Processor")		\
_ELF_DEFINE_EM(EM_PDP10,            64,					\
	"Digital Equipment Corp. PDP-10")				\
_ELF_DEFINE_EM(EM_PDP11,            65,					\
	"Digital Equipment Corp. PDP-11")				\
_ELF_DEFINE_EM(EM_FX66,             66, "Siemens FX66 microcontroller")	\
_ELF_DEFINE_EM(EM_ST9PLUS,          67,					\
	"STMicroelectronics ST9+ 8/16 bit microcontroller")		\
_ELF_DEFINE_EM(EM_ST7,              68,					\
	"STMicroelectronics ST7 8-bit microcontroller")			\
_ELF_DEFINE_EM(EM_68HC16,           69,					\
	"Motorola MC68HC16 Microcontroller")				\
_ELF_DEFINE_EM(EM_68HC11,           70,					\
	"Motorola MC68HC11 Microcontroller")				\
_ELF_DEFINE_EM(EM_68HC08,           71,					\
	"Motorola MC68HC08 Microcontroller")				\
_ELF_DEFINE_EM(EM_68HC05,           72,					\
	"Motorola MC68HC05 Microcontroller")				\
_ELF_DEFINE_EM(EM_SVX,              73, "Silicon Graphics SVx")		\
_ELF_DEFINE_EM(EM_ST19,             74,					\
	"STMicroelectronics ST19 8-bit microcontroller")		\
_ELF_DEFINE_EM(EM_VAX,              75, "Digital VAX")			\
_ELF_DEFINE_EM(EM_CRIS,             76,					\
	"Axis Communications 32-bit embedded processor")		\
_ELF_DEFINE_EM(EM_JAVELIN,          77,					\
	"Infineon Technologies 32-bit embedded processor")		\
_ELF_DEFINE_EM(EM_FIREPATH,         78,					\
	"Element 14 64-bit DSP Processor")				\
_ELF_DEFINE_EM(EM_ZSP,              79,					\
	"LSI Logic 16-bit DSP Processor")				\
_ELF_DEFINE_EM(EM_MMIX,             80,					\
	"Donald Knuth's educational 64-bit processor")			\
_ELF_DEFINE_EM(EM_HUANY,            81,					\
	"Harvard University machine-independent object files")		\
_ELF_DEFINE_EM(EM_PRISM,            82, "SiTera Prism")			\
_ELF_DEFINE_EM(EM_AVR,              83,					\
	"Atmel AVR 8-bit microcontroller")				\
_ELF_DEFINE_EM(EM_FR30,             84, "Fujitsu FR30")			\
_ELF_DEFINE_EM(EM_D10V,             85, "Mitsubishi D10V")		\
_ELF_DEFINE_EM(EM_D30V,             86, "Mitsubishi D30V")		\
_ELF_DEFINE_EM(EM_V850,             87, "NEC v850")			\
_ELF_DEFINE_EM(EM_M32R,             88, "Mitsubishi M32R")		\
_ELF_DEFINE_EM(EM_MN10300,          89, "Matsushita MN10300")		\
_ELF_DEFINE_EM(EM_MN10200,          90, "Matsushita MN10200")		\
_ELF_DEFINE_EM(EM_PJ,               91, "picoJava")			\
_ELF_DEFINE_EM(EM_OPENRISC,         92,					\
	"OpenRISC 32-bit embedded processor")				\
_ELF_DEFINE_EM(EM_ARC_COMPACT,      93,					\
	"ARC International ARCompact processor")			\
_ELF_DEFINE_EM(EM_XTENSA,           94,					\
	"Tensilica Xtensa Architecture")				\
_ELF_DEFINE_EM(EM_VIDEOCORE,        95,					\
	"Alphamosaic VideoCore processor")				\
_ELF_DEFINE_EM(EM_TMM_GPP,          96,					\
	"Thompson Multimedia General Purpose Processor")		\
_ELF_DEFINE_EM(EM_NS32K,            97,					\
	"National Semiconductor 32000 series")				\
_ELF_DEFINE_EM(EM_TPC,              98, "Tenor Network TPC processor")	\
_ELF_DEFINE_EM(EM_SNP1K,            99, "Trebia SNP 1000 processor")	\
_ELF_DEFINE_EM(EM_ST200,            100,				\
	"STMicroelectronics (www.st.com) ST200 microcontroller")	\
_ELF_DEFINE_EM(EM_IP2K,             101,				\
	"Ubicom IP2xxx microcontroller family")				\
_ELF_DEFINE_EM(EM_MAX,              102, "MAX Processor")		\
_ELF_DEFINE_EM(EM_CR,               103,				\
	"National Semiconductor CompactRISC microprocessor")		\
_ELF_DEFINE_EM(EM_F2MC16,           104, "Fujitsu F2MC16")		\
_ELF_DEFINE_EM(EM_MSP430,           105,				\
	"Texas Instruments embedded microcontroller msp430")		\
_ELF_DEFINE_EM(EM_BLACKFIN,         106,				\
	"Analog Devices Blackfin (DSP) processor")			\
_ELF_DEFINE_EM(EM_SE_C33,           107,				\
	"S1C33 Family of Seiko Epson processors")			\
_ELF_DEFINE_EM(EM_SEP,              108,				\
	"Sharp embedded microprocessor")				\
_ELF_DEFINE_EM(EM_ARCA,             109, "Arca RISC Microprocessor")	\
_ELF_DEFINE_EM(EM_UNICORE,          110,				\
	"Microprocessor series from PKU-Unity Ltd. and MPRC of Peking University") \
_ELF_DEFINE_EM(EM_EXCESS,           111,				\
	"eXcess: 16/32/64-bit configurable embedded CPU")		\
_ELF_DEFINE_EM(EM_DXP,              112,				\
	"Icera Semiconductor Inc. Deep Execution Processor")		\
_ELF_DEFINE_EM(EM_ALTERA_NIOS2,     113,				\
	"Altera Nios II soft-core processor")				\
_ELF_DEFINE_EM(EM_CRX,              114,				\
	"National Semiconductor CompactRISC CRX microprocessor")	\
_ELF_DEFINE_EM(EM_XGATE,            115,				\
	"Motorola XGATE embedded processor")				\
_ELF_DEFINE_EM(EM_C166,             116,				\
	"Infineon C16x/XC16x processor")				\
_ELF_DEFINE_EM(EM_M16C,             117,				\
	"Renesas M16C series microprocessors")				\
_ELF_DEFINE_EM(EM_DSPIC30F,         118,				\
	"Microchip Technology dsPIC30F Digital Signal Controller")	\
_ELF_DEFINE_EM(EM_CE,               119,				\
	"Freescale Communication Engine RISC core")			\
_ELF_DEFINE_EM(EM_M32C,             120,				\
	"Renesas M32C series microprocessors")				\
_ELF_DEFINE_EM(EM_TSK3000,          131, "Altium TSK3000 core")		\
_ELF_DEFINE_EM(EM_RS08,             132,				\
	"Freescale RS08 embedded processor")				\
_ELF_DEFINE_EM(EM_SHARC,            133,				\
	"Analog Devices SHARC family of 32-bit DSP processors")		\
_ELF_DEFINE_EM(EM_ECOG2,            134,				\
	"Cyan Technology eCOG2 microprocessor")				\
_ELF_DEFINE_EM(EM_SCORE7,           135,				\
	"Sunplus S+core7 RISC processor")				\
_ELF_DEFINE_EM(EM_DSP24,            136,				\
	"New Japan Radio (NJR) 24-bit DSP Processor")			\
_ELF_DEFINE_EM(EM_VIDEOCORE3,       137,				\
	"Broadcom VideoCore III processor")				\
_ELF_DEFINE_EM(EM_LATTICEMICO32,    138,				\
	"RISC processor for Lattice FPGA architecture")			\
_ELF_DEFINE_EM(EM_SE_C17,           139, "Seiko Epson C17 family")	\
_ELF_DEFINE_EM(EM_TI_C6000,         140,				\
	"The Texas Instruments TMS320C6000 DSP family")			\
_ELF_DEFINE_EM(EM_TI_C2000,         141,				\
	"The Texas Instruments TMS320C2000 DSP family")			\
_ELF_DEFINE_EM(EM_TI_C5500,         142,				\
	"The Texas Instruments TMS320C55x DSP family")			\
_ELF_DEFINE_EM(EM_MMDSP_PLUS,       160,				\
	"STMicroelectronics 64bit VLIW Data Signal Processor")		\
_ELF_DEFINE_EM(EM_CYPRESS_M8C,      161, "Cypress M8C microprocessor")	\
_ELF_DEFINE_EM(EM_R32C,             162,				\
	"Renesas R32C series microprocessors")				\
_ELF_DEFINE_EM(EM_TRIMEDIA,         163,				\
	"NXP Semiconductors TriMedia architecture family")		\
_ELF_DEFINE_EM(EM_QDSP6,            164, "QUALCOMM DSP6 Processor")	\
_ELF_DEFINE_EM(EM_8051,             165, "Intel 8051 and variants")	\
_ELF_DEFINE_EM(EM_STXP7X,           166,				\
	"STMicroelectronics STxP7x family of configurable and extensible RISC processors") \
_ELF_DEFINE_EM(EM_NDS32,            167,				\
	"Andes Technology compact code size embedded RISC processor family") \
_ELF_DEFINE_EM(EM_ECOG1,            168,				\
	"Cyan Technology eCOG1X family")				\
_ELF_DEFINE_EM(EM_ECOG1X,           168,				\
	"Cyan Technology eCOG1X family")				\
_ELF_DEFINE_EM(EM_MAXQ30,           169,				\
	"Dallas Semiconductor MAXQ30 Core Micro-controllers")		\
_ELF_DEFINE_EM(EM_XIMO16,           170,				\
	"New Japan Radio (NJR) 16-bit DSP Processor")			\
_ELF_DEFINE_EM(EM_MANIK,            171,				\
	"M2000 Reconfigurable RISC Microprocessor")			\
_ELF_DEFINE_EM(EM_CRAYNV2,          172,				\
	"Cray Inc. NV2 vector architecture")				\
_ELF_DEFINE_EM(EM_RX,               173, "Renesas RX family")		\
_ELF_DEFINE_EM(EM_METAG,            174,				\
	"Imagination Technologies META processor architecture")		\
_ELF_DEFINE_EM(EM_MCST_ELBRUS,      175,				\
	"MCST Elbrus general purpose hardware architecture")		\
_ELF_DEFINE_EM(EM_ECOG16,           176,				\
	"Cyan Technology eCOG16 family")				\
_ELF_DEFINE_EM(EM_CR16,             177,				\
	"National Semiconductor CompactRISC CR16 16-bit microprocessor") \
_ELF_DEFINE_EM(EM_ETPU,             178,				\
	"Freescale Extended Time Processing Unit")			\
_ELF_DEFINE_EM(EM_SLE9X,            179,				\
	"Infineon Technologies SLE9X core")				\
_ELF_DEFINE_EM(EM_AARCH64,          183,				\
	"AArch64 (64-bit ARM)")						\
_ELF_DEFINE_EM(EM_AVR32,            185,				\
	"Atmel Corporation 32-bit microprocessor family")		\
_ELF_DEFINE_EM(EM_STM8,             186,				\
	"STMicroeletronics STM8 8-bit microcontroller")			\
_ELF_DEFINE_EM(EM_TILE64,           187,				\
	"Tilera TILE64 multicore architecture family")			\
_ELF_DEFINE_EM(EM_TILEPRO,          188,				\
	"Tilera TILEPro multicore architecture family")			\
_ELF_DEFINE_EM(EM_MICROBLAZE,       189,				\
	"Xilinx MicroBlaze 32-bit RISC soft processor core")		\
_ELF_DEFINE_EM(EM_CUDA,             190, "NVIDIA CUDA architecture")	\
_ELF_DEFINE_EM(EM_TILEGX,           191,				\
	"Tilera TILE-Gx multicore architecture family")			\
_ELF_DEFINE_EM(EM_CLOUDSHIELD,      192,				\
	"CloudShield architecture family")				\
_ELF_DEFINE_EM(EM_COREA_1ST,        193,				\
	"KIPO-KAIST Core-A 1st generation processor family")		\
_ELF_DEFINE_EM(EM_COREA_2ND,        194,				\
	"KIPO-KAIST Core-A 2nd generation processor family")		\
_ELF_DEFINE_EM(EM_ARC_COMPACT2,     195, "Synopsys ARCompact V2")	\
_ELF_DEFINE_EM(EM_OPEN8,            196,				\
	"Open8 8-bit RISC soft processor core")				\
_ELF_DEFINE_EM(EM_RL78,             197, "Renesas RL78 family")		\
_ELF_DEFINE_EM(EM_VIDEOCORE5,       198, "Broadcom VideoCore V processor") \
_ELF_DEFINE_EM(EM_78KOR,            199, "Renesas 78KOR family")	\
_ELF_DEFINE_EM(EM_56800EX,          200,				\
	"Freescale 56800EX Digital Signal Controller")			\
_ELF_DEFINE_EM(EM_BA1,              201, "Beyond BA1 CPU architecture")	\
_ELF_DEFINE_EM(EM_BA2,              202, "Beyond BA2 CPU architecture")	\
_ELF_DEFINE_EM(EM_XCORE,            203, "XMOS xCORE processor family") \
_ELF_DEFINE_EM(EM_MCHP_PIC,         204, "Microchip 8-bit PIC(r) family") \
_ELF_DEFINE_EM(EM_INTEL205,         205, "Reserved by Intel")           \
_ELF_DEFINE_EM(EM_INTEL206,         206, "Reserved by Intel")           \
_ELF_DEFINE_EM(EM_INTEL207,         207, "Reserved by Intel")           \
_ELF_DEFINE_EM(EM_INTEL208,         208, "Reserved by Intel")           \
_ELF_DEFINE_EM(EM_INTEL209,         209, "Reserved by Intel")           \
_ELF_DEFINE_EM(EM_KM32,             210, "KM211 KM32 32-bit processor") \
_ELF_DEFINE_EM(EM_KMX32,            211, "KM211 KMX32 32-bit processor") \
_ELF_DEFINE_EM(EM_KMX16,            212, "KM211 KMX16 16-bit processor") \
_ELF_DEFINE_EM(EM_KMX8,             213, "KM211 KMX8 8-bit processor")  \
_ELF_DEFINE_EM(EM_KVARC,            214, "KM211 KMX32 KVARC processor") \
_ELF_DEFINE_EM(EM_RISCV,            243, "RISC-V")

#undef	_ELF_DEFINE_EM
#define	_ELF_DEFINE_EM(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_ELF_MACHINES()
	EM__LAST__
};

/* Other synonyms. */
#define	EM_AMD64		EM_X86_64
#define	EM_ARC_A5		EM_ARC_COMPACT

/*
 * ELF file types: (ET_*).
 */
#define	_ELF_DEFINE_ELF_TYPES()						\
_ELF_DEFINE_ET(ET_NONE,   0,	    "No file type")			\
_ELF_DEFINE_ET(ET_REL,    1, 	    "Relocatable object")		\
_ELF_DEFINE_ET(ET_EXEC,   2, 	    "Executable")			\
_ELF_DEFINE_ET(ET_DYN,    3, 	    "Shared object")			\
_ELF_DEFINE_ET(ET_CORE,   4, 	    "Core file")			\
_ELF_DEFINE_ET(ET_LOOS,   0xFE00U,  "Begin OS-specific range")		\
_ELF_DEFINE_ET(ET_HIOS,   0xFEFFU,  "End OS-specific range")		\
_ELF_DEFINE_ET(ET_LOPROC, 0xFF00U,  "Begin processor-specific range")	\
_ELF_DEFINE_ET(ET_HIPROC, 0xFFFFU,  "End processor-specific range")

#undef	_ELF_DEFINE_ET
#define	_ELF_DEFINE_ET(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_ELF_TYPES()
	ET__LAST__
};

/* ELF file format version numbers. */
#define	EV_NONE		0
#define	EV_CURRENT	1

/*
 * Flags for section groups.
 */
#define	GRP_COMDAT 	0x1		/* COMDAT semantics */
#define	GRP_MASKOS 	0x0ff00000	/* OS-specific flags */
#define	GRP_MASKPROC 	0xf0000000	/* processor-specific flags */

/*
 * Flags / mask for .gnu.versym sections.
 */
#define	VERSYM_VERSION	0x7fff
#define	VERSYM_HIDDEN	0x8000

/*
 * Flags used by program header table entries.
 */

#define	_ELF_DEFINE_PHDR_FLAGS()					\
_ELF_DEFINE_PF(PF_X,                0x1, "Execute")			\
_ELF_DEFINE_PF(PF_W,                0x2, "Write")			\
_ELF_DEFINE_PF(PF_R,                0x4, "Read")			\
_ELF_DEFINE_PF(PF_MASKOS,           0x0ff00000, "OS-specific flags")	\
_ELF_DEFINE_PF(PF_MASKPROC,         0xf0000000, "Processor-specific flags") \
_ELF_DEFINE_PF(PF_ARM_SB,           0x10000000,				\
	"segment contains the location addressed by the static base")	\
_ELF_DEFINE_PF(PF_ARM_PI,           0x20000000,				\
	"segment is position-independent")				\
_ELF_DEFINE_PF(PF_ARM_ABS,          0x40000000,				\
	"segment must be loaded at its base address")

#undef	_ELF_DEFINE_PF
#define	_ELF_DEFINE_PF(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_PHDR_FLAGS()
	PF__LAST__
};

/*
 * Types of program header table entries.
 */

#define	_ELF_DEFINE_PHDR_TYPES()				\
_ELF_DEFINE_PT(PT_NULL,             0, "ignored entry")		\
_ELF_DEFINE_PT(PT_LOAD,             1, "loadable segment")	\
_ELF_DEFINE_PT(PT_DYNAMIC,          2,				\
	"contains dynamic linking information")			\
_ELF_DEFINE_PT(PT_INTERP,           3, "names an interpreter")	\
_ELF_DEFINE_PT(PT_NOTE,             4, "auxiliary information")	\
_ELF_DEFINE_PT(PT_SHLIB,            5, "reserved")		\
_ELF_DEFINE_PT(PT_PHDR,             6,				\
	"describes the program header itself")			\
_ELF_DEFINE_PT(PT_TLS,              7, "thread local storage")	\
_ELF_DEFINE_PT(PT_LOOS,             0x60000000UL,		\
	"start of OS-specific range")				\
_ELF_DEFINE_PT(PT_SUNW_UNWIND,      0x6464E550UL,		\
	"Solaris/amd64 stack unwind tables")			\
_ELF_DEFINE_PT(PT_GNU_EH_FRAME,     0x6474E550UL,		\
	"GCC generated .eh_frame_hdr segment")			\
_ELF_DEFINE_PT(PT_GNU_STACK,	    0x6474E551UL,		\
	"Stack flags")						\
_ELF_DEFINE_PT(PT_GNU_RELRO,	    0x6474E552UL,		\
	"Segment becomes read-only after relocation")		\
_ELF_DEFINE_PT(PT_OPENBSD_RANDOMIZE,0x65A3DBE6UL,		\
	"Segment filled with random data")			\
_ELF_DEFINE_PT(PT_OPENBSD_WXNEEDED, 0x65A3DBE7UL,		\
	"Program violates W^X")					\
_ELF_DEFINE_PT(PT_OPENBSD_BOOTDATA, 0x65A41BE6UL,		\
	"Boot data")						\
_ELF_DEFINE_PT(PT_SUNWBSS,          0x6FFFFFFAUL,		\
	"A Solaris .SUNW_bss section")				\
_ELF_DEFINE_PT(PT_SUNWSTACK,        0x6FFFFFFBUL,		\
	"A Solaris process stack")				\
_ELF_DEFINE_PT(PT_SUNWDTRACE,       0x6FFFFFFCUL,		\
	"Used by dtrace(1)")					\
_ELF_DEFINE_PT(PT_SUNWCAP,          0x6FFFFFFDUL,		\
	"Special hardware capability requirements")		\
_ELF_DEFINE_PT(PT_HIOS,             0x6FFFFFFFUL,		\
	"end of OS-specific range")				\
_ELF_DEFINE_PT(PT_LOPROC,           0x70000000UL,		\
	"start of processor-specific range")			\
_ELF_DEFINE_PT(PT_ARM_ARCHEXT,      0x70000000UL,		\
	"platform architecture compatibility information")	\
_ELF_DEFINE_PT(PT_ARM_EXIDX,        0x70000001UL,		\
	"exception unwind tables")				\
_ELF_DEFINE_PT(PT_MIPS_REGINFO,     0x70000000UL,		\
	"register usage information")				\
_ELF_DEFINE_PT(PT_MIPS_RTPROC,      0x70000001UL,		\
	"runtime procedure table")				\
_ELF_DEFINE_PT(PT_MIPS_OPTIONS,     0x70000002UL,		\
	"options segment")					\
_ELF_DEFINE_PT(PT_HIPROC,           0x7FFFFFFFUL,		\
	"end of processor-specific range")

#undef	_ELF_DEFINE_PT
#define	_ELF_DEFINE_PT(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_PHDR_TYPES()
	PT__LAST__ = PT_HIPROC
};

/* synonyms. */
#define	PT_ARM_UNWIND	PT_ARM_EXIDX
#define	PT_HISUNW	PT_HIOS
#define	PT_LOSUNW	PT_SUNWBSS

/*
 * Section flags.
 */

#define	_ELF_DEFINE_SECTION_FLAGS()					\
_ELF_DEFINE_SHF(SHF_WRITE,           0x1,				\
	"writable during program execution")				\
_ELF_DEFINE_SHF(SHF_ALLOC,           0x2,				\
	"occupies memory during program execution")			\
_ELF_DEFINE_SHF(SHF_EXECINSTR,       0x4, "executable instructions")	\
_ELF_DEFINE_SHF(SHF_MERGE,           0x10,				\
	"may be merged to prevent duplication")				\
_ELF_DEFINE_SHF(SHF_STRINGS,         0x20,				\
	"NUL-terminated character strings")				\
_ELF_DEFINE_SHF(SHF_INFO_LINK,       0x40,				\
	"the sh_info field holds a link")				\
_ELF_DEFINE_SHF(SHF_LINK_ORDER,      0x80,				\
	"special ordering requirements during linking")			\
_ELF_DEFINE_SHF(SHF_OS_NONCONFORMING, 0x100,				\
	"requires OS-specific processing during linking")		\
_ELF_DEFINE_SHF(SHF_GROUP,           0x200,				\
	"member of a section group")					\
_ELF_DEFINE_SHF(SHF_TLS,             0x400,				\
	"holds thread-local storage")					\
_ELF_DEFINE_SHF(SHF_COMPRESSED,      0x800,				\
	"holds compressed data")					\
_ELF_DEFINE_SHF(SHF_MASKOS,          0x0FF00000UL,			\
	"bits reserved for OS-specific semantics")			\
_ELF_DEFINE_SHF(SHF_AMD64_LARGE,     0x10000000UL,			\
	"section uses large code model")				\
_ELF_DEFINE_SHF(SHF_ENTRYSECT,       0x10000000UL,			\
	"section contains an entry point (ARM)")			\
_ELF_DEFINE_SHF(SHF_COMDEF,          0x80000000UL,			\
	"section may be multiply defined in input to link step (ARM)")	\
_ELF_DEFINE_SHF(SHF_MIPS_GPREL,      0x10000000UL,			\
	"section must be part of global data area")			\
_ELF_DEFINE_SHF(SHF_MIPS_MERGE,      0x20000000UL,			\
	"section data should be merged to eliminate duplication")	\
_ELF_DEFINE_SHF(SHF_MIPS_ADDR,       0x40000000UL,			\
	"section data is addressed by default")				\
_ELF_DEFINE_SHF(SHF_MIPS_STRING,     0x80000000UL,			\
	"section data is string data by default")			\
_ELF_DEFINE_SHF(SHF_MIPS_NOSTRIP,    0x08000000UL,			\
	"section data may not be stripped")				\
_ELF_DEFINE_SHF(SHF_MIPS_LOCAL,      0x04000000UL,			\
	"section data local to process")				\
_ELF_DEFINE_SHF(SHF_MIPS_NAMES,      0x02000000UL,			\
	"linker must generate implicit hidden weak names")		\
_ELF_DEFINE_SHF(SHF_MIPS_NODUPE,     0x01000000UL,			\
	"linker must retain only one copy")				\
_ELF_DEFINE_SHF(SHF_ORDERED,         0x40000000UL,			\
	"section is ordered with respect to other sections")		\
_ELF_DEFINE_SHF(SHF_EXCLUDE,	     0x80000000UL,			\
	"section is excluded from executables and shared objects")	\
_ELF_DEFINE_SHF(SHF_MASKPROC,        0xF0000000UL,			\
	"bits reserved for processor-specific semantics")

#undef	_ELF_DEFINE_SHF
#define	_ELF_DEFINE_SHF(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_SECTION_FLAGS()
	SHF__LAST__
};

/*
 * Special section indices.
 */
#define _ELF_DEFINE_SECTION_INDICES()					\
_ELF_DEFINE_SHN(SHN_UNDEF, 	0, 	 "undefined section")		\
_ELF_DEFINE_SHN(SHN_LORESERVE, 	0xFF00U, "start of reserved area")	\
_ELF_DEFINE_SHN(SHN_LOPROC, 	0xFF00U,				\
	"start of processor-specific range")				\
_ELF_DEFINE_SHN(SHN_BEFORE,	0xFF00U, "used for section ordering")	\
_ELF_DEFINE_SHN(SHN_AFTER,	0xFF01U, "used for section ordering")	\
_ELF_DEFINE_SHN(SHN_AMD64_LCOMMON, 0xFF02U, "large common block label") \
_ELF_DEFINE_SHN(SHN_MIPS_ACOMMON, 0xFF00U,				\
	"allocated common symbols in a DSO")				\
_ELF_DEFINE_SHN(SHN_MIPS_TEXT,	0xFF01U, "Reserved (obsolete)")		\
_ELF_DEFINE_SHN(SHN_MIPS_DATA,	0xFF02U, "Reserved (obsolete)")		\
_ELF_DEFINE_SHN(SHN_MIPS_SCOMMON, 0xFF03U,				\
	"gp-addressable common symbols")				\
_ELF_DEFINE_SHN(SHN_MIPS_SUNDEFINED, 0xFF04U,				\
	"gp-addressable undefined symbols")				\
_ELF_DEFINE_SHN(SHN_MIPS_LCOMMON, 0xFF05U, "local common symbols")	\
_ELF_DEFINE_SHN(SHN_MIPS_LUNDEFINED, 0xFF06U,				\
	"local undefined symbols")					\
_ELF_DEFINE_SHN(SHN_HIPROC, 	0xFF1FU,				\
	"end of processor-specific range")				\
_ELF_DEFINE_SHN(SHN_LOOS, 	0xFF20U,				\
	"start of OS-specific range")					\
_ELF_DEFINE_SHN(SHN_SUNW_IGNORE, 0xFF3FU, "used by dtrace")		\
_ELF_DEFINE_SHN(SHN_HIOS, 	0xFF3FU,				\
	"end of OS-specific range")					\
_ELF_DEFINE_SHN(SHN_ABS, 	0xFFF1U, "absolute references")		\
_ELF_DEFINE_SHN(SHN_COMMON, 	0xFFF2U, "references to COMMON areas")	\
_ELF_DEFINE_SHN(SHN_XINDEX, 	0xFFFFU, "extended index")		\
_ELF_DEFINE_SHN(SHN_HIRESERVE, 	0xFFFFU, "end of reserved area")

#undef	_ELF_DEFINE_SHN
#define	_ELF_DEFINE_SHN(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_SECTION_INDICES()
	SHN__LAST__
};

/*
 * Section types.
 */

#define	_ELF_DEFINE_SECTION_TYPES()					\
_ELF_DEFINE_SHT(SHT_NULL,            0, "inactive header")		\
_ELF_DEFINE_SHT(SHT_PROGBITS,        1, "program defined information")	\
_ELF_DEFINE_SHT(SHT_SYMTAB,          2, "symbol table")			\
_ELF_DEFINE_SHT(SHT_STRTAB,          3, "string table")			\
_ELF_DEFINE_SHT(SHT_RELA,            4,					\
	"relocation entries with addends")				\
_ELF_DEFINE_SHT(SHT_HASH,            5, "symbol hash table")		\
_ELF_DEFINE_SHT(SHT_DYNAMIC,         6,					\
	"information for dynamic linking")				\
_ELF_DEFINE_SHT(SHT_NOTE,            7, "additional notes")		\
_ELF_DEFINE_SHT(SHT_NOBITS,          8, "section occupying no space")	\
_ELF_DEFINE_SHT(SHT_REL,             9,					\
	"relocation entries without addends")				\
_ELF_DEFINE_SHT(SHT_SHLIB,           10, "reserved")			\
_ELF_DEFINE_SHT(SHT_DYNSYM,          11, "symbol table")		\
_ELF_DEFINE_SHT(SHT_INIT_ARRAY,      14,				\
	"pointers to initialization functions")				\
_ELF_DEFINE_SHT(SHT_FINI_ARRAY,      15,				\
	"pointers to termination functions")				\
_ELF_DEFINE_SHT(SHT_PREINIT_ARRAY,   16,				\
	"pointers to functions called before initialization")		\
_ELF_DEFINE_SHT(SHT_GROUP,           17, "defines a section group")	\
_ELF_DEFINE_SHT(SHT_SYMTAB_SHNDX,    18,				\
	"used for extended section numbering")				\
_ELF_DEFINE_SHT(SHT_LOOS,            0x60000000UL,			\
	"start of OS-specific range")					\
_ELF_DEFINE_SHT(SHT_SUNW_dof,	     0x6FFFFFF4UL,			\
	"used by dtrace")						\
_ELF_DEFINE_SHT(SHT_SUNW_cap,	     0x6FFFFFF5UL,			\
	"capability requirements")					\
_ELF_DEFINE_SHT(SHT_GNU_ATTRIBUTES,  0x6FFFFFF5UL,			\
	"object attributes")						\
_ELF_DEFINE_SHT(SHT_SUNW_SIGNATURE,  0x6FFFFFF6UL,			\
	"module verification signature")				\
_ELF_DEFINE_SHT(SHT_GNU_HASH,	     0x6FFFFFF6UL,			\
	"GNU Hash sections")						\
_ELF_DEFINE_SHT(SHT_GNU_LIBLIST,     0x6FFFFFF7UL,			\
	"List of libraries to be prelinked")				\
_ELF_DEFINE_SHT(SHT_SUNW_ANNOTATE,   0x6FFFFFF7UL,			\
	"special section where unresolved references are allowed")	\
_ELF_DEFINE_SHT(SHT_SUNW_DEBUGSTR,   0x6FFFFFF8UL,			\
	"debugging information")					\
_ELF_DEFINE_SHT(SHT_CHECKSUM, 	     0x6FFFFFF8UL,			\
	"checksum for dynamic shared objects")				\
_ELF_DEFINE_SHT(SHT_SUNW_DEBUG,      0x6FFFFFF9UL,			\
	"debugging information")					\
_ELF_DEFINE_SHT(SHT_SUNW_move,       0x6FFFFFFAUL,			\
	"information to handle partially initialized symbols")		\
_ELF_DEFINE_SHT(SHT_SUNW_COMDAT,     0x6FFFFFFBUL,			\
	"section supporting merging of multiple copies of data")	\
_ELF_DEFINE_SHT(SHT_SUNW_syminfo,    0x6FFFFFFCUL,			\
	"additional symbol information")				\
_ELF_DEFINE_SHT(SHT_SUNW_verdef,     0x6FFFFFFDUL,			\
	"symbol versioning information")				\
_ELF_DEFINE_SHT(SHT_SUNW_verneed,    0x6FFFFFFEUL,			\
	"symbol versioning requirements")				\
_ELF_DEFINE_SHT(SHT_SUNW_versym,     0x6FFFFFFFUL,			\
	"symbol versioning table")					\
_ELF_DEFINE_SHT(SHT_HIOS,            0x6FFFFFFFUL,			\
	"end of OS-specific range")					\
_ELF_DEFINE_SHT(SHT_LOPROC,          0x70000000UL,			\
	"start of processor-specific range")				\
_ELF_DEFINE_SHT(SHT_ARM_EXIDX,       0x70000001UL,			\
	"exception index table")					\
_ELF_DEFINE_SHT(SHT_ARM_PREEMPTMAP,  0x70000002UL,			\
	"BPABI DLL dynamic linking preemption map")			\
_ELF_DEFINE_SHT(SHT_ARM_ATTRIBUTES,  0x70000003UL,			\
	"object file compatibility attributes")				\
_ELF_DEFINE_SHT(SHT_ARM_DEBUGOVERLAY, 0x70000004UL,			\
	"overlay debug information")					\
_ELF_DEFINE_SHT(SHT_ARM_OVERLAYSECTION, 0x70000005UL,			\
	"overlay debug information")					\
_ELF_DEFINE_SHT(SHT_MIPS_LIBLIST,    0x70000000UL,			\
	"DSO library information used in link")				\
_ELF_DEFINE_SHT(SHT_MIPS_MSYM,       0x70000001UL,			\
	"MIPS symbol table extension")					\
_ELF_DEFINE_SHT(SHT_MIPS_CONFLICT,   0x70000002UL,			\
	"symbol conflicting with DSO-defined symbols ")			\
_ELF_DEFINE_SHT(SHT_MIPS_GPTAB,      0x70000003UL,			\
	"global pointer table")						\
_ELF_DEFINE_SHT(SHT_MIPS_UCODE,      0x70000004UL,			\
	"reserved")							\
_ELF_DEFINE_SHT(SHT_MIPS_DEBUG,      0x70000005UL,			\
	"reserved (obsolete debug information)")			\
_ELF_DEFINE_SHT(SHT_MIPS_REGINFO,    0x70000006UL,			\
	"register usage information")					\
_ELF_DEFINE_SHT(SHT_MIPS_PACKAGE,    0x70000007UL,			\
	"OSF reserved")							\
_ELF_DEFINE_SHT(SHT_MIPS_PACKSYM,    0x70000008UL,			\
	"OSF reserved")							\
_ELF_DEFINE_SHT(SHT_MIPS_RELD,       0x70000009UL,			\
	"dynamic relocation")						\
_ELF_DEFINE_SHT(SHT_MIPS_IFACE,      0x7000000BUL,			\
	"subprogram interface information")				\
_ELF_DEFINE_SHT(SHT_MIPS_CONTENT,    0x7000000CUL,			\
	"section content classification")				\
_ELF_DEFINE_SHT(SHT_MIPS_OPTIONS,     0x7000000DUL,			\
	"general options")						\
_ELF_DEFINE_SHT(SHT_MIPS_DELTASYM,   0x7000001BUL,			\
	"Delta C++: symbol table")					\
_ELF_DEFINE_SHT(SHT_MIPS_DELTAINST,  0x7000001CUL,			\
	"Delta C++: instance table")					\
_ELF_DEFINE_SHT(SHT_MIPS_DELTACLASS, 0x7000001DUL,			\
	"Delta C++: class table")					\
_ELF_DEFINE_SHT(SHT_MIPS_DWARF,      0x7000001EUL,			\
	"DWARF debug information")					\
_ELF_DEFINE_SHT(SHT_MIPS_DELTADECL,  0x7000001FUL,			\
	"Delta C++: declarations")					\
_ELF_DEFINE_SHT(SHT_MIPS_SYMBOL_LIB, 0x70000020UL,			\
	"symbol-to-library mapping")					\
_ELF_DEFINE_SHT(SHT_MIPS_EVENTS,     0x70000021UL,			\
	"event locations")						\
_ELF_DEFINE_SHT(SHT_MIPS_TRANSLATE,  0x70000022UL,			\
	"???")								\
_ELF_DEFINE_SHT(SHT_MIPS_PIXIE,      0x70000023UL,			\
	"special pixie sections")					\
_ELF_DEFINE_SHT(SHT_MIPS_XLATE,      0x70000024UL,			\
	"address translation table")					\
_ELF_DEFINE_SHT(SHT_MIPS_XLATE_DEBUG, 0x70000025UL,			\
	"SGI internal address translation table")			\
_ELF_DEFINE_SHT(SHT_MIPS_WHIRL,      0x70000026UL,			\
	"intermediate code")						\
_ELF_DEFINE_SHT(SHT_MIPS_EH_REGION,  0x70000027UL,			\
	"C++ exception handling region info")				\
_ELF_DEFINE_SHT(SHT_MIPS_XLATE_OLD,  0x70000028UL,			\
	"obsolete")							\
_ELF_DEFINE_SHT(SHT_MIPS_PDR_EXCEPTION, 0x70000029UL,			\
	"runtime procedure descriptor table exception information")	\
_ELF_DEFINE_SHT(SHT_MIPS_ABIFLAGS,   0x7000002AUL,			\
	"ABI flags")							\
_ELF_DEFINE_SHT(SHT_SPARC_GOTDATA,   0x70000000UL,			\
	"SPARC-specific data")						\
_ELF_DEFINE_SHT(SHT_X86_64_UNWIND,   0x70000001UL,			\
	"unwind tables for the AMD64")					\
_ELF_DEFINE_SHT(SHT_ORDERED,         0x7FFFFFFFUL,			\
	"sort entries in the section")					\
_ELF_DEFINE_SHT(SHT_HIPROC,          0x7FFFFFFFUL,			\
	"end of processor-specific range")				\
_ELF_DEFINE_SHT(SHT_LOUSER,          0x80000000UL,			\
	"start of application-specific range")				\
_ELF_DEFINE_SHT(SHT_HIUSER,          0xFFFFFFFFUL,			\
	"end of application-specific range")

#undef	_ELF_DEFINE_SHT
#define	_ELF_DEFINE_SHT(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_SECTION_TYPES()
	SHT__LAST__ = SHT_HIUSER
};

/* Aliases for section types. */
#define	SHT_AMD64_UNWIND	SHT_X86_64_UNWIND
#define	SHT_GNU_verdef		SHT_SUNW_verdef
#define	SHT_GNU_verneed		SHT_SUNW_verneed
#define	SHT_GNU_versym		SHT_SUNW_versym

/*
 * Symbol binding information.
 */

#define	_ELF_DEFINE_SYMBOL_BINDING()					\
_ELF_DEFINE_STB(STB_LOCAL,           0,					\
	"not visible outside defining object file")			\
_ELF_DEFINE_STB(STB_GLOBAL,          1,					\
	"visible across all object files being combined")		\
_ELF_DEFINE_STB(STB_WEAK,            2,					\
	"visible across all object files but with low precedence")	\
_ELF_DEFINE_STB(STB_LOOS,            10, "start of OS-specific range")	\
_ELF_DEFINE_STB(STB_GNU_UNIQUE,      10, "unique symbol (GNU)")		\
_ELF_DEFINE_STB(STB_HIOS,            12, "end of OS-specific range")	\
_ELF_DEFINE_STB(STB_LOPROC,          13,				\
	"start of processor-specific range")				\
_ELF_DEFINE_STB(STB_HIPROC,          15,				\
	"end of processor-specific range")

#undef	_ELF_DEFINE_STB
#define	_ELF_DEFINE_STB(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_SYMBOL_BINDING()
	STB__LAST__
};

/*
 * Symbol types
 */

#define	_ELF_DEFINE_SYMBOL_TYPES()					\
_ELF_DEFINE_STT(STT_NOTYPE,          0, "unspecified type")		\
_ELF_DEFINE_STT(STT_OBJECT,          1, "data object")			\
_ELF_DEFINE_STT(STT_FUNC,            2, "executable code")		\
_ELF_DEFINE_STT(STT_SECTION,         3, "section")			\
_ELF_DEFINE_STT(STT_FILE,            4, "source file")			\
_ELF_DEFINE_STT(STT_COMMON,          5, "uninitialized common block")	\
_ELF_DEFINE_STT(STT_TLS,             6, "thread local storage")		\
_ELF_DEFINE_STT(STT_LOOS,            10, "start of OS-specific types")	\
_ELF_DEFINE_STT(STT_GNU_IFUNC,       10, "indirect function")	\
_ELF_DEFINE_STT(STT_HIOS,            12, "end of OS-specific types")	\
_ELF_DEFINE_STT(STT_LOPROC,          13,				\
	"start of processor-specific types")				\
_ELF_DEFINE_STT(STT_ARM_TFUNC,       13, "Thumb function (GNU)")	\
_ELF_DEFINE_STT(STT_ARM_16BIT,       15, "Thumb label (GNU)")		\
_ELF_DEFINE_STT(STT_SPARC_REGISTER,  13, "SPARC register information")	\
_ELF_DEFINE_STT(STT_HIPROC,          15,				\
	"end of processor-specific types")

#undef	_ELF_DEFINE_STT
#define	_ELF_DEFINE_STT(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_SYMBOL_TYPES()
	STT__LAST__
};

/*
 * Symbol binding.
 */

#define	_ELF_DEFINE_SYMBOL_BINDING_KINDS()		\
_ELF_DEFINE_SYB(SYMINFO_BT_SELF,	0xFFFFU,	\
	"bound to self")				\
_ELF_DEFINE_SYB(SYMINFO_BT_PARENT,	0xFFFEU,	\
	"bound to parent")				\
_ELF_DEFINE_SYB(SYMINFO_BT_NONE,	0xFFFDU,	\
	"no special binding")

#undef	_ELF_DEFINE_SYB
#define	_ELF_DEFINE_SYB(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_SYMBOL_BINDING_KINDS()
	SYMINFO__LAST__
};

/*
 * Symbol visibility.
 */

#define	_ELF_DEFINE_SYMBOL_VISIBILITY()		\
_ELF_DEFINE_STV(STV_DEFAULT,         0,		\
	"as specified by symbol type")		\
_ELF_DEFINE_STV(STV_INTERNAL,        1,		\
	"as defined by processor semantics")	\
_ELF_DEFINE_STV(STV_HIDDEN,          2,		\
	"hidden from other components")		\
_ELF_DEFINE_STV(STV_PROTECTED,       3,		\
	"local references are not preemptable")

#undef	_ELF_DEFINE_STV
#define	_ELF_DEFINE_STV(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_SYMBOL_VISIBILITY()
	STV__LAST__
};

/*
 * Symbol flags.
 */
#define	_ELF_DEFINE_SYMBOL_FLAGS()		\
_ELF_DEFINE_SYF(SYMINFO_FLG_DIRECT,	0x01,	\
	"directly assocated reference")		\
_ELF_DEFINE_SYF(SYMINFO_FLG_COPY,	0x04,	\
	"definition by copy-relocation")	\
_ELF_DEFINE_SYF(SYMINFO_FLG_LAZYLOAD,	0x08,	\
	"object should be lazily loaded")	\
_ELF_DEFINE_SYF(SYMINFO_FLG_DIRECTBIND,	0x10,	\
	"reference should be directly bound")	\
_ELF_DEFINE_SYF(SYMINFO_FLG_NOEXTDIRECT, 0x20,	\
	"external references not allowed to bind to definition")

#undef	_ELF_DEFINE_SYF
#define	_ELF_DEFINE_SYF(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_SYMBOL_FLAGS()
	SYMINFO_FLG__LAST__
};

/*
 * Version dependencies.
 */
#define	_ELF_DEFINE_VERSIONING_DEPENDENCIES()			\
_ELF_DEFINE_VERD(VER_NDX_LOCAL,		0,	"local scope")	\
_ELF_DEFINE_VERD(VER_NDX_GLOBAL,	1,	"global scope")
#undef	_ELF_DEFINE_VERD
#define	_ELF_DEFINE_VERD(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_VERSIONING_DEPENDENCIES()
	VER_NDX__LAST__
};

/*
 * Version flags.
 */
#define	_ELF_DEFINE_VERSIONING_FLAGS()				\
_ELF_DEFINE_VERF(VER_FLG_BASE,		0x1,	"file version") \
_ELF_DEFINE_VERF(VER_FLG_WEAK,		0x2,	"weak version")
#undef	_ELF_DEFINE_VERF
#define	_ELF_DEFINE_VERF(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_VERSIONING_FLAGS()
	VER_FLG__LAST__
};

/*
 * Version needs
 */
#define	_ELF_DEFINE_VERSIONING_NEEDS()					\
_ELF_DEFINE_VRN(VER_NEED_NONE,		0,	"invalid version")	\
_ELF_DEFINE_VRN(VER_NEED_CURRENT,	1,	"current version")
#undef	_ELF_DEFINE_VRN
#define	_ELF_DEFINE_VRN(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_VERSIONING_NEEDS()
	VER_NEED__LAST__
};

/*
 * Version numbers.
 */
#define	_ELF_DEFINE_VERSIONING_NUMBERS()				\
_ELF_DEFINE_VRNU(VER_DEF_NONE,		0,	"invalid version")	\
_ELF_DEFINE_VRNU(VER_DEF_CURRENT,	1, 	"current version")
#undef	_ELF_DEFINE_VRNU
#define	_ELF_DEFINE_VRNU(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_VERSIONING_NUMBERS()
	VER_DEF__LAST__
};

/**
 ** Relocation types.
 **/

#define	_ELF_DEFINE_386_RELOCATIONS()		\
_ELF_DEFINE_RELOC(R_386_NONE,		0)	\
_ELF_DEFINE_RELOC(R_386_32,		1)	\
_ELF_DEFINE_RELOC(R_386_PC32,		2)	\
_ELF_DEFINE_RELOC(R_386_GOT32,		3)	\
_ELF_DEFINE_RELOC(R_386_PLT32,		4)	\
_ELF_DEFINE_RELOC(R_386_COPY,		5)	\
_ELF_DEFINE_RELOC(R_386_GLOB_DAT,	6)	\
_ELF_DEFINE_RELOC(R_386_JUMP_SLOT,	7)	\
_ELF_DEFINE_RELOC(R_386_RELATIVE,	8)	\
_ELF_DEFINE_RELOC(R_386_GOTOFF,		9)	\
_ELF_DEFINE_RELOC(R_386_GOTPC,		10)	\
_ELF_DEFINE_RELOC(R_386_32PLT,		11)	\
_ELF_DEFINE_RELOC(R_386_TLS_TPOFF,	14)	\
_ELF_DEFINE_RELOC(R_386_TLS_IE,		15)	\
_ELF_DEFINE_RELOC(R_386_TLS_GOTIE,	16)	\
_ELF_DEFINE_RELOC(R_386_TLS_LE,		17)	\
_ELF_DEFINE_RELOC(R_386_TLS_GD,		18)	\
_ELF_DEFINE_RELOC(R_386_TLS_LDM,	19)	\
_ELF_DEFINE_RELOC(R_386_16,		20)	\
_ELF_DEFINE_RELOC(R_386_PC16,		21)	\
_ELF_DEFINE_RELOC(R_386_8,		22)	\
_ELF_DEFINE_RELOC(R_386_PC8,		23)	\
_ELF_DEFINE_RELOC(R_386_TLS_GD_32,	24)	\
_ELF_DEFINE_RELOC(R_386_TLS_GD_PUSH,	25)	\
_ELF_DEFINE_RELOC(R_386_TLS_GD_CALL,	26)	\
_ELF_DEFINE_RELOC(R_386_TLS_GD_POP,	27)	\
_ELF_DEFINE_RELOC(R_386_TLS_LDM_32,	28)	\
_ELF_DEFINE_RELOC(R_386_TLS_LDM_PUSH,	29)	\
_ELF_DEFINE_RELOC(R_386_TLS_LDM_CALL,	30)	\
_ELF_DEFINE_RELOC(R_386_TLS_LDM_POP,	31)	\
_ELF_DEFINE_RELOC(R_386_TLS_LDO_32,	32)	\
_ELF_DEFINE_RELOC(R_386_TLS_IE_32,	33)	\
_ELF_DEFINE_RELOC(R_386_TLS_LE_32,	34)	\
_ELF_DEFINE_RELOC(R_386_TLS_DTPMOD32,	35)	\
_ELF_DEFINE_RELOC(R_386_TLS_DTPOFF32,	36)	\
_ELF_DEFINE_RELOC(R_386_TLS_TPOFF32,	37)	\
_ELF_DEFINE_RELOC(R_386_SIZE32,		38)	\
_ELF_DEFINE_RELOC(R_386_TLS_GOTDESC,	39)	\
_ELF_DEFINE_RELOC(R_386_TLS_DESC_CALL,	40)	\
_ELF_DEFINE_RELOC(R_386_TLS_DESC,	41)	\
_ELF_DEFINE_RELOC(R_386_IRELATIVE,	42)	\
_ELF_DEFINE_RELOC(R_386_GOT32X,		43)


/*
 */
#define	_ELF_DEFINE_AARCH64_RELOCATIONS()				\
_ELF_DEFINE_RELOC(R_AARCH64_NONE,				0)	\
_ELF_DEFINE_RELOC(R_AARCH64_ABS64,				257)	\
_ELF_DEFINE_RELOC(R_AARCH64_ABS32,				258)	\
_ELF_DEFINE_RELOC(R_AARCH64_ABS16,				259)	\
_ELF_DEFINE_RELOC(R_AARCH64_PREL64,				260)	\
_ELF_DEFINE_RELOC(R_AARCH64_PREL32,				261)	\
_ELF_DEFINE_RELOC(R_AARCH64_PREL16,				262)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_UABS_G0,			263)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_UABS_G0_NC,			264)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_UABS_G1,			265)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_UABS_G1_NC,			266)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_UABS_G2,			267)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_UABS_G2_NC,			268)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_UABS_G3,			269)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_SABS_G0,			270)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_SABS_G1,			271)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_SABS_G2,			272)	\
_ELF_DEFINE_RELOC(R_AARCH64_LD_PREL_LO19,			273)	\
_ELF_DEFINE_RELOC(R_AARCH64_ADR_PREL_LO21,			274)	\
_ELF_DEFINE_RELOC(R_AARCH64_ADR_PREL_PG_HI21,			275)	\
_ELF_DEFINE_RELOC(R_AARCH64_ADR_PREL_PG_HI21_NC,		276)	\
_ELF_DEFINE_RELOC(R_AARCH64_ADD_ABS_LO12_NC,			277)	\
_ELF_DEFINE_RELOC(R_AARCH64_LDST8_ABS_LO12_NC,			278)	\
_ELF_DEFINE_RELOC(R_AARCH64_TSTBR14,				279)	\
_ELF_DEFINE_RELOC(R_AARCH64_CONDBR19,				280)	\
_ELF_DEFINE_RELOC(R_AARCH64_JUMP26,				282)	\
_ELF_DEFINE_RELOC(R_AARCH64_CALL26,				283)	\
_ELF_DEFINE_RELOC(R_AARCH64_LDST16_ABS_LO12_NC,			284)	\
_ELF_DEFINE_RELOC(R_AARCH64_LDST32_ABS_LO12_NC,			285)	\
_ELF_DEFINE_RELOC(R_AARCH64_LDST64_ABS_LO12_NC,			286)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_PREL_G0,			287)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_PREL_G0_NC,			288)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_PREL_G1,			289)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_PREL_G1_NC,			290)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_PREL_G2,			291)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_PREL_G2_NC,			292)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_PREL_G3,			293)	\
_ELF_DEFINE_RELOC(R_AARCH64_LDST128_ABS_LO12_NC,		299)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_GOTOFF_G0,			300)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_GOTOFF_G0_NC,			301)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_GOTOFF_G1,			302)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_GOTOFF_G1_NC,			303)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_GOTOFF_G2,			304)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_GOTOFF_G2_NC,			305)	\
_ELF_DEFINE_RELOC(R_AARCH64_MOVW_GOTOFF_G3,			306)	\
_ELF_DEFINE_RELOC(R_AARCH64_GOTREL64,				307)	\
_ELF_DEFINE_RELOC(R_AARCH64_GOTREL32,				308)	\
_ELF_DEFINE_RELOC(R_AARCH64_GOT_LD_PREL19,			309)	\
_ELF_DEFINE_RELOC(R_AARCH64_LD64_GOTOFF_LO15,			310)	\
_ELF_DEFINE_RELOC(R_AARCH64_ADR_GOT_PAGE,			311)	\
_ELF_DEFINE_RELOC(R_AARCH64_LD64_GOT_LO12_NC,			312)	\
_ELF_DEFINE_RELOC(R_AARCH64_LD64_GOTPAGE_LO15,			313)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSGD_ADR_PREL21,			512)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSGD_ADR_PAGE21,			513)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSGD_ADD_LO12_NC,			514)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSGD_MOVW_G1,			515)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSGD_MOVW_G0_NC,			516)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLD_ADR_PREL21,			517)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLD_ADR_PAGE21,			518)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLD_ADD_LO12_NC,			519)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLD_MOVW_G1,			520)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLD_MOVW_G0_NC,			521)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLD_LD_PREL19,			522)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLD_MOVW_DTPREL_G2,		523)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLD_MOVW_DTPREL_G1,		524)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLD_MOVW_DTPREL_G1_NC,		525)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLD_MOVW_DTPREL_G0,		526)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLD_MOVW_DTPREL_G0_NC,		527)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLD_ADD_DTPREL_HI12,		529)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLD_ADD_DTPREL_LO12_NC,		530)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLD_LDST8_DTPREL_LO12,		531)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLD_LDST8_DTPREL_LO12_NC,		532)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLD_LDST16_DTPREL_LO12,		533)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLD_LDST16_DTPREL_LO12_NC,	534)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLD_LDST32_DTPREL_LO12,		535)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLD_LDST32_DTPREL_LO12_NC,	536)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLD_LDST64_DTPREL_LO12,		537)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLD_LDST64_DTPREL_LO12_NC,	538)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSIE_MOVW_GOTTPREL_G1,		539)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSIE_MOVW_GOTTPREL_G0_NC,		540)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21,		541)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC,	542)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSIE_LD_GOTTPREL_PREL19,		543)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLE_MOVW_TPREL_G2,		544)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLE_MOVW_TPREL_G1,		545)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLE_MOVW_TPREL_G1_NC,		546)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLE_MOVW_TPREL_G0,		547)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLE_MOVW_TPREL_G0_NC,		548)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLE_ADD_TPREL_HI12,		549)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLE_ADD_TPREL_LO12,		550)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLE_ADD_TPREL_LO12_NC,		551)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLE_LDST8_TPREL_LO12,		552)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLE_LDST8_TPREL_LO12_NC,		553)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLE_LDST16_TPREL_LO12,		554)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLE_LDST16_TPREL_LO12_NC,		555)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLE_LDST32_TPREL_LO12,		556)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLE_LDST32_TPREL_LO12_NC,		557)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLE_LDST64_TPREL_LO12,		558)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC,		559)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSDESC_LD_PREL19,			560)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSDESC_ADR_PREL21,			561)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSDESC_ADR_PAGE21,			562)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSDESC_LD64_LO12,			563)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSDESC_ADD_LO12,			564)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSDESC_OFF_G1,			565)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSDESC_OFF_G0_NC,			566)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSDESC_LDR,			567)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSDESC_ADD,			568)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSDESC_CALL,			569)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLE_LDST128_TPREL_LO12,		570)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLE_LDST128_TPREL_LO12_NC,	571)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLD_LDST128_DTPREL_LO12,		572)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSLD_LDST128_DTPREL_LO12_NC,	573)	\
_ELF_DEFINE_RELOC(R_AARCH64_COPY,				1024)	\
_ELF_DEFINE_RELOC(R_AARCH64_GLOB_DAT,				1025)	\
_ELF_DEFINE_RELOC(R_AARCH64_JUMP_SLOT,				1026)	\
_ELF_DEFINE_RELOC(R_AARCH64_RELATIVE,				1027)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLS_DTPREL64,			1028)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLS_DTPMOD64,			1029)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLS_TPREL64,			1030)	\
_ELF_DEFINE_RELOC(R_AARCH64_TLSDESC,				1031)	\
_ELF_DEFINE_RELOC(R_AARCH64_IRELATIVE,				1032)

/*
 * These are the symbols used in the Sun ``Linkers and Loaders
 * Guide'', Document No: 817-1984-17.  See the X86_64 relocations list
 * below for the spellings used in the ELF specification.
 */
#define	_ELF_DEFINE_AMD64_RELOCATIONS()		\
_ELF_DEFINE_RELOC(R_AMD64_NONE,		0)	\
_ELF_DEFINE_RELOC(R_AMD64_64,		1)	\
_ELF_DEFINE_RELOC(R_AMD64_PC32,		2)	\
_ELF_DEFINE_RELOC(R_AMD64_GOT32,	3)	\
_ELF_DEFINE_RELOC(R_AMD64_PLT32,	4)	\
_ELF_DEFINE_RELOC(R_AMD64_COPY,		5)	\
_ELF_DEFINE_RELOC(R_AMD64_GLOB_DAT,	6)	\
_ELF_DEFINE_RELOC(R_AMD64_JUMP_SLOT,	7)	\
_ELF_DEFINE_RELOC(R_AMD64_RELATIVE,	8)	\
_ELF_DEFINE_RELOC(R_AMD64_GOTPCREL,	9)	\
_ELF_DEFINE_RELOC(R_AMD64_32,		10)	\
_ELF_DEFINE_RELOC(R_AMD64_32S,		11)	\
_ELF_DEFINE_RELOC(R_AMD64_16,		12)	\
_ELF_DEFINE_RELOC(R_AMD64_PC16,		13)	\
_ELF_DEFINE_RELOC(R_AMD64_8,		14)	\
_ELF_DEFINE_RELOC(R_AMD64_PC8,		15)	\
_ELF_DEFINE_RELOC(R_AMD64_PC64,		24)	\
_ELF_DEFINE_RELOC(R_AMD64_GOTOFF64,	25)	\
_ELF_DEFINE_RELOC(R_AMD64_GOTPC32,	26)

/*
 * Relocation definitions from the ARM ELF ABI, version "ARM IHI
 * 0044E" released on 30th November 2012.
 */
#define	_ELF_DEFINE_ARM_RELOCATIONS()			\
_ELF_DEFINE_RELOC(R_ARM_NONE,			0)	\
_ELF_DEFINE_RELOC(R_ARM_PC24,			1)	\
_ELF_DEFINE_RELOC(R_ARM_ABS32,			2)	\
_ELF_DEFINE_RELOC(R_ARM_REL32,			3)	\
_ELF_DEFINE_RELOC(R_ARM_LDR_PC_G0,		4)	\
_ELF_DEFINE_RELOC(R_ARM_ABS16,			5)	\
_ELF_DEFINE_RELOC(R_ARM_ABS12,			6)	\
_ELF_DEFINE_RELOC(R_ARM_THM_ABS5,		7)	\
_ELF_DEFINE_RELOC(R_ARM_ABS8,			8)	\
_ELF_DEFINE_RELOC(R_ARM_SBREL32,		9)	\
_ELF_DEFINE_RELOC(R_ARM_THM_CALL,		10)	\
_ELF_DEFINE_RELOC(R_ARM_THM_PC8,		11)	\
_ELF_DEFINE_RELOC(R_ARM_BREL_ADJ,		12)	\
_ELF_DEFINE_RELOC(R_ARM_SWI24,			13)	\
_ELF_DEFINE_RELOC(R_ARM_TLS_DESC,		13)	\
_ELF_DEFINE_RELOC(R_ARM_THM_SWI8,		14)	\
_ELF_DEFINE_RELOC(R_ARM_XPC25,			15)	\
_ELF_DEFINE_RELOC(R_ARM_THM_XPC22,		16)	\
_ELF_DEFINE_RELOC(R_ARM_TLS_DTPMOD32,		17)	\
_ELF_DEFINE_RELOC(R_ARM_TLS_DTPOFF32,		18)	\
_ELF_DEFINE_RELOC(R_ARM_TLS_TPOFF32,		19)	\
_ELF_DEFINE_RELOC(R_ARM_COPY,			20)	\
_ELF_DEFINE_RELOC(R_ARM_GLOB_DAT,		21)	\
_ELF_DEFINE_RELOC(R_ARM_JUMP_SLOT,		22)	\
_ELF_DEFINE_RELOC(R_ARM_RELATIVE,		23)	\
_ELF_DEFINE_RELOC(R_ARM_GOTOFF32,		24)	\
_ELF_DEFINE_RELOC(R_ARM_BASE_PREL,		25)	\
_ELF_DEFINE_RELOC(R_ARM_GOT_BREL,		26)	\
_ELF_DEFINE_RELOC(R_ARM_PLT32,			27)	\
_ELF_DEFINE_RELOC(R_ARM_CALL,			28)	\
_ELF_DEFINE_RELOC(R_ARM_JUMP24,			29)	\
_ELF_DEFINE_RELOC(R_ARM_THM_JUMP24,		30)	\
_ELF_DEFINE_RELOC(R_ARM_BASE_ABS,		31)	\
_ELF_DEFINE_RELOC(R_ARM_ALU_PCREL_7_0,		32)	\
_ELF_DEFINE_RELOC(R_ARM_ALU_PCREL_15_8,		33)	\
_ELF_DEFINE_RELOC(R_ARM_ALU_PCREL_23_15,	34)	\
_ELF_DEFINE_RELOC(R_ARM_LDR_SBREL_11_0_NC,	35)	\
_ELF_DEFINE_RELOC(R_ARM_ALU_SBREL_19_12_NC,	36)	\
_ELF_DEFINE_RELOC(R_ARM_ALU_SBREL_27_20_CK,	37)	\
_ELF_DEFINE_RELOC(R_ARM_TARGET1,		38)	\
_ELF_DEFINE_RELOC(R_ARM_SBREL31,		39)	\
_ELF_DEFINE_RELOC(R_ARM_V4BX,			40)	\
_ELF_DEFINE_RELOC(R_ARM_TARGET2,		41)	\
_ELF_DEFINE_RELOC(R_ARM_PREL31,			42)	\
_ELF_DEFINE_RELOC(R_ARM_MOVW_ABS_NC,		43)	\
_ELF_DEFINE_RELOC(R_ARM_MOVT_ABS,		44)	\
_ELF_DEFINE_RELOC(R_ARM_MOVW_PREL_NC,		45)	\
_ELF_DEFINE_RELOC(R_ARM_MOVT_PREL,		46)	\
_ELF_DEFINE_RELOC(R_ARM_THM_MOVW_ABS_NC,	47)	\
_ELF_DEFINE_RELOC(R_ARM_THM_MOVT_ABS,		48)	\
_ELF_DEFINE_RELOC(R_ARM_THM_MOVW_PREL_NC,	49)	\
_ELF_DEFINE_RELOC(R_ARM_THM_MOVT_PREL,		50)	\
_ELF_DEFINE_RELOC(R_ARM_THM_JUMP19,		51)	\
_ELF_DEFINE_RELOC(R_ARM_THM_JUMP6,		52)	\
_ELF_DEFINE_RELOC(R_ARM_THM_ALU_PREL_11_0,	53)	\
_ELF_DEFINE_RELOC(R_ARM_THM_PC12,		54)	\
_ELF_DEFINE_RELOC(R_ARM_ABS32_NOI,		55)	\
_ELF_DEFINE_RELOC(R_ARM_REL32_NOI,		56)	\
_ELF_DEFINE_RELOC(R_ARM_ALU_PC_G0_NC,		57)	\
_ELF_DEFINE_RELOC(R_ARM_ALU_PC_G0,		58)	\
_ELF_DEFINE_RELOC(R_ARM_ALU_PC_G1_NC,		59)	\
_ELF_DEFINE_RELOC(R_ARM_ALU_PC_G1,		60)	\
_ELF_DEFINE_RELOC(R_ARM_ALU_PC_G2,		61)	\
_ELF_DEFINE_RELOC(R_ARM_LDR_PC_G1,		62)	\
_ELF_DEFINE_RELOC(R_ARM_LDR_PC_G2,		63)	\
_ELF_DEFINE_RELOC(R_ARM_LDRS_PC_G0,		64)	\
_ELF_DEFINE_RELOC(R_ARM_LDRS_PC_G1,		65)	\
_ELF_DEFINE_RELOC(R_ARM_LDRS_PC_G2,		66)	\
_ELF_DEFINE_RELOC(R_ARM_LDC_PC_G0,		67)	\
_ELF_DEFINE_RELOC(R_ARM_LDC_PC_G1,		68)	\
_ELF_DEFINE_RELOC(R_ARM_LDC_PC_G2,		69)	\
_ELF_DEFINE_RELOC(R_ARM_ALU_SB_G0_NC,		70)	\
_ELF_DEFINE_RELOC(R_ARM_ALU_SB_G0,		71)	\
_ELF_DEFINE_RELOC(R_ARM_ALU_SB_G1_NC,		72)	\
_ELF_DEFINE_RELOC(R_ARM_ALU_SB_G1,		73)	\
_ELF_DEFINE_RELOC(R_ARM_ALU_SB_G2,		74)	\
_ELF_DEFINE_RELOC(R_ARM_LDR_SB_G0,		75)	\
_ELF_DEFINE_RELOC(R_ARM_LDR_SB_G1,		76)	\
_ELF_DEFINE_RELOC(R_ARM_LDR_SB_G2,		77)	\
_ELF_DEFINE_RELOC(R_ARM_LDRS_SB_G0,		78)	\
_ELF_DEFINE_RELOC(R_ARM_LDRS_SB_G1,		79)	\
_ELF_DEFINE_RELOC(R_ARM_LDRS_SB_G2,		80)	\
_ELF_DEFINE_RELOC(R_ARM_LDC_SB_G0,		81)	\
_ELF_DEFINE_RELOC(R_ARM_LDC_SB_G1,		82)	\
_ELF_DEFINE_RELOC(R_ARM_LDC_SB_G2,		83)	\
_ELF_DEFINE_RELOC(R_ARM_MOVW_BREL_NC,		84)	\
_ELF_DEFINE_RELOC(R_ARM_MOVT_BREL,		85)	\
_ELF_DEFINE_RELOC(R_ARM_MOVW_BREL,		86)	\
_ELF_DEFINE_RELOC(R_ARM_THM_MOVW_BREL_NC,	87)	\
_ELF_DEFINE_RELOC(R_ARM_THM_MOVT_BREL,		88)	\
_ELF_DEFINE_RELOC(R_ARM_THM_MOVW_BREL,		89)	\
_ELF_DEFINE_RELOC(R_ARM_TLS_GOTDESC,		90)	\
_ELF_DEFINE_RELOC(R_ARM_TLS_CALL,		91)	\
_ELF_DEFINE_RELOC(R_ARM_TLS_DESCSEQ,		92)	\
_ELF_DEFINE_RELOC(R_ARM_THM_TLS_CALL,		93)	\
_ELF_DEFINE_RELOC(R_ARM_PLT32_ABS,		94)	\
_ELF_DEFINE_RELOC(R_ARM_GOT_ABS,		95)	\
_ELF_DEFINE_RELOC(R_ARM_GOT_PREL,		96)	\
_ELF_DEFINE_RELOC(R_ARM_GOT_BREL12,		97)	\
_ELF_DEFINE_RELOC(R_ARM_GOTOFF12,		98)	\
_ELF_DEFINE_RELOC(R_ARM_GOTRELAX,		99)	\
_ELF_DEFINE_RELOC(R_ARM_GNU_VTENTRY,		100)	\
_ELF_DEFINE_RELOC(R_ARM_GNU_VTINHERIT,		101)	\
_ELF_DEFINE_RELOC(R_ARM_THM_JUMP11,		102)	\
_ELF_DEFINE_RELOC(R_ARM_THM_JUMP8,		103)	\
_ELF_DEFINE_RELOC(R_ARM_TLS_GD32,		104)	\
_ELF_DEFINE_RELOC(R_ARM_TLS_LDM32,		105)	\
_ELF_DEFINE_RELOC(R_ARM_TLS_LDO32,		106)	\
_ELF_DEFINE_RELOC(R_ARM_TLS_IE32,		107)	\
_ELF_DEFINE_RELOC(R_ARM_TLS_LE32,		108)	\
_ELF_DEFINE_RELOC(R_ARM_TLS_LDO12,		109)	\
_ELF_DEFINE_RELOC(R_ARM_TLS_LE12,		110)	\
_ELF_DEFINE_RELOC(R_ARM_TLS_IE12GP,		111)	\
_ELF_DEFINE_RELOC(R_ARM_PRIVATE_0,		112)	\
_ELF_DEFINE_RELOC(R_ARM_PRIVATE_1,		113)	\
_ELF_DEFINE_RELOC(R_ARM_PRIVATE_2,		114)	\
_ELF_DEFINE_RELOC(R_ARM_PRIVATE_3,		115)	\
_ELF_DEFINE_RELOC(R_ARM_PRIVATE_4,		116)	\
_ELF_DEFINE_RELOC(R_ARM_PRIVATE_5,		117)	\
_ELF_DEFINE_RELOC(R_ARM_PRIVATE_6,		118)	\
_ELF_DEFINE_RELOC(R_ARM_PRIVATE_7,		119)	\
_ELF_DEFINE_RELOC(R_ARM_PRIVATE_8,		120)	\
_ELF_DEFINE_RELOC(R_ARM_PRIVATE_9,		121)	\
_ELF_DEFINE_RELOC(R_ARM_PRIVATE_10,		122)	\
_ELF_DEFINE_RELOC(R_ARM_PRIVATE_11,		123)	\
_ELF_DEFINE_RELOC(R_ARM_PRIVATE_12,		124)	\
_ELF_DEFINE_RELOC(R_ARM_PRIVATE_13,		125)	\
_ELF_DEFINE_RELOC(R_ARM_PRIVATE_14,		126)	\
_ELF_DEFINE_RELOC(R_ARM_PRIVATE_15,		127)	\
_ELF_DEFINE_RELOC(R_ARM_ME_TOO,			128)	\
_ELF_DEFINE_RELOC(R_ARM_THM_TLS_DESCSEQ16,	129)	\
_ELF_DEFINE_RELOC(R_ARM_THM_TLS_DESCSEQ32,	130)	\
_ELF_DEFINE_RELOC(R_ARM_THM_GOT_BREL12,		131)	\
_ELF_DEFINE_RELOC(R_ARM_IRELATIVE,		140)

#define	_ELF_DEFINE_IA64_RELOCATIONS()			\
_ELF_DEFINE_RELOC(R_IA_64_NONE,			0)	\
_ELF_DEFINE_RELOC(R_IA_64_IMM14,		0x21)	\
_ELF_DEFINE_RELOC(R_IA_64_IMM22,		0x22)	\
_ELF_DEFINE_RELOC(R_IA_64_IMM64,		0x23)	\
_ELF_DEFINE_RELOC(R_IA_64_DIR32MSB,		0x24)	\
_ELF_DEFINE_RELOC(R_IA_64_DIR32LSB,		0x25)	\
_ELF_DEFINE_RELOC(R_IA_64_DIR64MSB,		0x26)	\
_ELF_DEFINE_RELOC(R_IA_64_DIR64LSB,		0x27)	\
_ELF_DEFINE_RELOC(R_IA_64_GPREL22,		0x2a)	\
_ELF_DEFINE_RELOC(R_IA_64_GPREL64I,		0x2b)	\
_ELF_DEFINE_RELOC(R_IA_64_GPREL32MSB,		0x2c)	\
_ELF_DEFINE_RELOC(R_IA_64_GPREL32LSB,		0x2d)	\
_ELF_DEFINE_RELOC(R_IA_64_GPREL64MSB,		0x2e)	\
_ELF_DEFINE_RELOC(R_IA_64_GPREL64LSB,		0x2f)	\
_ELF_DEFINE_RELOC(R_IA_64_LTOFF22,		0x32)	\
_ELF_DEFINE_RELOC(R_IA_64_LTOFF64I,		0x33)	\
_ELF_DEFINE_RELOC(R_IA_64_PLTOFF22,		0x3a)	\
_ELF_DEFINE_RELOC(R_IA_64_PLTOFF64I,		0x3b)	\
_ELF_DEFINE_RELOC(R_IA_64_PLTOFF64MSB,		0x3e)	\
_ELF_DEFINE_RELOC(R_IA_64_PLTOFF64LSB,		0x3f)	\
_ELF_DEFINE_RELOC(R_IA_64_FPTR64I,		0x43)	\
_ELF_DEFINE_RELOC(R_IA_64_FPTR32MSB,		0x44)	\
_ELF_DEFINE_RELOC(R_IA_64_FPTR32LSB,		0x45)	\
_ELF_DEFINE_RELOC(R_IA_64_FPTR64MSB,		0x46)	\
_ELF_DEFINE_RELOC(R_IA_64_FPTR64LSB,		0x47)	\
_ELF_DEFINE_RELOC(R_IA_64_PCREL60B,		0x48)	\
_ELF_DEFINE_RELOC(R_IA_64_PCREL21B,		0x49)	\
_ELF_DEFINE_RELOC(R_IA_64_PCREL21M,		0x4a)	\
_ELF_DEFINE_RELOC(R_IA_64_PCREL21F,		0x4b)	\
_ELF_DEFINE_RELOC(R_IA_64_PCREL32MSB,		0x4c)	\
_ELF_DEFINE_RELOC(R_IA_64_PCREL32LSB,		0x4d)	\
_ELF_DEFINE_RELOC(R_IA_64_PCREL64MSB,		0x4e)	\
_ELF_DEFINE_RELOC(R_IA_64_PCREL64LSB,		0x4f)	\
_ELF_DEFINE_RELOC(R_IA_64_LTOFF_FPTR22,		0x52)	\
_ELF_DEFINE_RELOC(R_IA_64_LTOFF_FPTR64I,	0x53)	\
_ELF_DEFINE_RELOC(R_IA_64_LTOFF_FPTR32MSB,	0x54)	\
_ELF_DEFINE_RELOC(R_IA_64_LTOFF_FPTR32LSB,	0x55)	\
_ELF_DEFINE_RELOC(R_IA_64_LTOFF_FPTR64MSB,	0x56)	\
_ELF_DEFINE_RELOC(R_IA_64_LTOFF_FPTR64LSB,	0x57)	\
_ELF_DEFINE_RELOC(R_IA_64_SEGREL32MSB,		0x5c)	\
_ELF_DEFINE_RELOC(R_IA_64_SEGREL32LSB,		0x5d)	\
_ELF_DEFINE_RELOC(R_IA_64_SEGREL64MSB,		0x5e)	\
_ELF_DEFINE_RELOC(R_IA_64_SEGREL64LSB,		0x5f)	\
_ELF_DEFINE_RELOC(R_IA_64_SECREL32MSB,		0x64)	\
_ELF_DEFINE_RELOC(R_IA_64_SECREL32LSB,		0x65)	\
_ELF_DEFINE_RELOC(R_IA_64_SECREL64MSB,		0x66)	\
_ELF_DEFINE_RELOC(R_IA_64_SECREL64LSB,		0x67)	\
_ELF_DEFINE_RELOC(R_IA_64_REL32MSB,		0x6c)	\
_ELF_DEFINE_RELOC(R_IA_64_REL32LSB,		0x6d)	\
_ELF_DEFINE_RELOC(R_IA_64_REL64MSB,		0x6e)	\
_ELF_DEFINE_RELOC(R_IA_64_REL64LSB,		0x6f)	\
_ELF_DEFINE_RELOC(R_IA_64_LTV32MSB,		0x74)	\
_ELF_DEFINE_RELOC(R_IA_64_LTV32LSB,		0x75)	\
_ELF_DEFINE_RELOC(R_IA_64_LTV64MSB,		0x76)	\
_ELF_DEFINE_RELOC(R_IA_64_LTV64LSB,		0x77)	\
_ELF_DEFINE_RELOC(R_IA_64_PCREL21BI,		0x79)	\
_ELF_DEFINE_RELOC(R_IA_64_PCREL22,		0x7A)	\
_ELF_DEFINE_RELOC(R_IA_64_PCREL64I,		0x7B)	\
_ELF_DEFINE_RELOC(R_IA_64_IPLTMSB,		0x80)	\
_ELF_DEFINE_RELOC(R_IA_64_IPLTLSB,		0x81)	\
_ELF_DEFINE_RELOC(R_IA_64_SUB,			0x85)	\
_ELF_DEFINE_RELOC(R_IA_64_LTOFF22X,		0x86)	\
_ELF_DEFINE_RELOC(R_IA_64_LDXMOV,		0x87)	\
_ELF_DEFINE_RELOC(R_IA_64_TPREL14,		0x91)	\
_ELF_DEFINE_RELOC(R_IA_64_TPREL22,		0x92)	\
_ELF_DEFINE_RELOC(R_IA_64_TPREL64I,		0x93)	\
_ELF_DEFINE_RELOC(R_IA_64_TPREL64MSB,		0x96)	\
_ELF_DEFINE_RELOC(R_IA_64_TPREL64LSB,		0x97)	\
_ELF_DEFINE_RELOC(R_IA_64_LTOFF_TPREL22,	0x9A)	\
_ELF_DEFINE_RELOC(R_IA_64_DTPMOD64MSB,		0xA6)	\
_ELF_DEFINE_RELOC(R_IA_64_DTPMOD64LSB,		0xA7)	\
_ELF_DEFINE_RELOC(R_IA_64_LTOFF_DTPMOD22,	0xAA)	\
_ELF_DEFINE_RELOC(R_IA_64_DTPREL14,		0xB1)	\
_ELF_DEFINE_RELOC(R_IA_64_DTPREL22,		0xB2)	\
_ELF_DEFINE_RELOC(R_IA_64_DTPREL64I,		0xB3)	\
_ELF_DEFINE_RELOC(R_IA_64_DTPREL32MSB,		0xB4)	\
_ELF_DEFINE_RELOC(R_IA_64_DTPREL32LSB,		0xB5)	\
_ELF_DEFINE_RELOC(R_IA_64_DTPREL64MSB,		0xB6)	\
_ELF_DEFINE_RELOC(R_IA_64_DTPREL64LSB,		0xB7)	\
_ELF_DEFINE_RELOC(R_IA_64_LTOFF_DTPREL22,	0xBA)

#define	_ELF_DEFINE_MIPS_RELOCATIONS()			\
_ELF_DEFINE_RELOC(R_MIPS_NONE,			0)	\
_ELF_DEFINE_RELOC(R_MIPS_16,			1)	\
_ELF_DEFINE_RELOC(R_MIPS_32,			2)	\
_ELF_DEFINE_RELOC(R_MIPS_REL32,			3)	\
_ELF_DEFINE_RELOC(R_MIPS_26,			4)	\
_ELF_DEFINE_RELOC(R_MIPS_HI16,			5)	\
_ELF_DEFINE_RELOC(R_MIPS_LO16,			6)	\
_ELF_DEFINE_RELOC(R_MIPS_GPREL16,		7)	\
_ELF_DEFINE_RELOC(R_MIPS_LITERAL, 		8)	\
_ELF_DEFINE_RELOC(R_MIPS_GOT16,			9)	\
_ELF_DEFINE_RELOC(R_MIPS_PC16,			10)	\
_ELF_DEFINE_RELOC(R_MIPS_CALL16,		11)	\
_ELF_DEFINE_RELOC(R_MIPS_GPREL32,		12)	\
_ELF_DEFINE_RELOC(R_MIPS_SHIFT5,		16)	\
_ELF_DEFINE_RELOC(R_MIPS_SHIFT6,		17)	\
_ELF_DEFINE_RELOC(R_MIPS_64,			18)	\
_ELF_DEFINE_RELOC(R_MIPS_GOT_DISP,		19)	\
_ELF_DEFINE_RELOC(R_MIPS_GOT_PAGE,		20)	\
_ELF_DEFINE_RELOC(R_MIPS_GOT_OFST,		21)	\
_ELF_DEFINE_RELOC(R_MIPS_GOT_HI16,		22)	\
_ELF_DEFINE_RELOC(R_MIPS_GOT_LO16,		23)	\
_ELF_DEFINE_RELOC(R_MIPS_SUB,			24)	\
_ELF_DEFINE_RELOC(R_MIPS_CALLHI16,		30)	\
_ELF_DEFINE_RELOC(R_MIPS_CALLLO16,		31)	\
_ELF_DEFINE_RELOC(R_MIPS_JALR,			37)	\
_ELF_DEFINE_RELOC(R_MIPS_TLS_DTPMOD32,		38)	\
_ELF_DEFINE_RELOC(R_MIPS_TLS_DTPREL32,		39)	\
_ELF_DEFINE_RELOC(R_MIPS_TLS_DTPMOD64,		40)	\
_ELF_DEFINE_RELOC(R_MIPS_TLS_DTPREL64,		41)	\
_ELF_DEFINE_RELOC(R_MIPS_TLS_GD,		42)	\
_ELF_DEFINE_RELOC(R_MIPS_TLS_LDM,		43)	\
_ELF_DEFINE_RELOC(R_MIPS_TLS_DTPREL_HI16,	44)	\
_ELF_DEFINE_RELOC(R_MIPS_TLS_DTPREL_LO16,	45)	\
_ELF_DEFINE_RELOC(R_MIPS_TLS_GOTTPREL,		46)	\
_ELF_DEFINE_RELOC(R_MIPS_TLS_TPREL32,		47)	\
_ELF_DEFINE_RELOC(R_MIPS_TLS_TPREL64,		48)	\
_ELF_DEFINE_RELOC(R_MIPS_TLS_TPREL_HI16,	49)	\
_ELF_DEFINE_RELOC(R_MIPS_TLS_TPREL_LO16,	50)

#define	_ELF_DEFINE_PPC32_RELOCATIONS()		\
_ELF_DEFINE_RELOC(R_PPC_NONE,		0)	\
_ELF_DEFINE_RELOC(R_PPC_ADDR32,		1)	\
_ELF_DEFINE_RELOC(R_PPC_ADDR24,		2)	\
_ELF_DEFINE_RELOC(R_PPC_ADDR16,		3)	\
_ELF_DEFINE_RELOC(R_PPC_ADDR16_LO,	4)	\
_ELF_DEFINE_RELOC(R_PPC_ADDR16_HI,	5)	\
_ELF_DEFINE_RELOC(R_PPC_ADDR16_HA,	6)	\
_ELF_DEFINE_RELOC(R_PPC_ADDR14,		7)	\
_ELF_DEFINE_RELOC(R_PPC_ADDR14_BRTAKEN,	8)	\
_ELF_DEFINE_RELOC(R_PPC_ADDR14_BRNTAKEN, 9)	\
_ELF_DEFINE_RELOC(R_PPC_REL24,		10)	\
_ELF_DEFINE_RELOC(R_PPC_REL14,		11)	\
_ELF_DEFINE_RELOC(R_PPC_REL14_BRTAKEN,	12)	\
_ELF_DEFINE_RELOC(R_PPC_REL14_BRNTAKEN,	13)	\
_ELF_DEFINE_RELOC(R_PPC_GOT16,		14)	\
_ELF_DEFINE_RELOC(R_PPC_GOT16_LO,	15)	\
_ELF_DEFINE_RELOC(R_PPC_GOT16_HI,	16)	\
_ELF_DEFINE_RELOC(R_PPC_GOT16_HA,	17)	\
_ELF_DEFINE_RELOC(R_PPC_PLTREL24,	18)	\
_ELF_DEFINE_RELOC(R_PPC_COPY,		19)	\
_ELF_DEFINE_RELOC(R_PPC_GLOB_DAT,	20)	\
_ELF_DEFINE_RELOC(R_PPC_JMP_SLOT,	21)	\
_ELF_DEFINE_RELOC(R_PPC_RELATIVE,	22)	\
_ELF_DEFINE_RELOC(R_PPC_LOCAL24PC,	23)	\
_ELF_DEFINE_RELOC(R_PPC_UADDR32,	24)	\
_ELF_DEFINE_RELOC(R_PPC_UADDR16,	25)	\
_ELF_DEFINE_RELOC(R_PPC_REL32,		26)	\
_ELF_DEFINE_RELOC(R_PPC_PLT32,		27)	\
_ELF_DEFINE_RELOC(R_PPC_PLTREL32,	28)	\
_ELF_DEFINE_RELOC(R_PPC_PLT16_LO,	29)	\
_ELF_DEFINE_RELOC(R_PPC_PLT16_HI,	30)	\
_ELF_DEFINE_RELOC(R_PPC_PLT16_HA,	31)	\
_ELF_DEFINE_RELOC(R_PPC_SDAREL16,	32)	\
_ELF_DEFINE_RELOC(R_PPC_SECTOFF,	33)	\
_ELF_DEFINE_RELOC(R_PPC_SECTOFF_LO,	34)	\
_ELF_DEFINE_RELOC(R_PPC_SECTOFF_HI,	35)	\
_ELF_DEFINE_RELOC(R_PPC_SECTOFF_HA,	36)	\
_ELF_DEFINE_RELOC(R_PPC_ADDR30,		37)	\
_ELF_DEFINE_RELOC(R_PPC_TLS,		67)	\
_ELF_DEFINE_RELOC(R_PPC_DTPMOD32,	68)	\
_ELF_DEFINE_RELOC(R_PPC_TPREL16,	69)	\
_ELF_DEFINE_RELOC(R_PPC_TPREL16_LO,	70)	\
_ELF_DEFINE_RELOC(R_PPC_TPREL16_HI,	71)	\
_ELF_DEFINE_RELOC(R_PPC_TPREL16_HA,	72)	\
_ELF_DEFINE_RELOC(R_PPC_TPREL32,	73)	\
_ELF_DEFINE_RELOC(R_PPC_DTPREL16,	74)	\
_ELF_DEFINE_RELOC(R_PPC_DTPREL16_LO,	75)	\
_ELF_DEFINE_RELOC(R_PPC_DTPREL16_HI,	76)	\
_ELF_DEFINE_RELOC(R_PPC_DTPREL16_HA,	77)	\
_ELF_DEFINE_RELOC(R_PPC_DTPREL32,	78)	\
_ELF_DEFINE_RELOC(R_PPC_GOT_TLSGD16,	79)	\
_ELF_DEFINE_RELOC(R_PPC_GOT_TLSGD16_LO,	80)	\
_ELF_DEFINE_RELOC(R_PPC_GOT_TLSGD16_HI,	81)	\
_ELF_DEFINE_RELOC(R_PPC_GOT_TLSGD16_HA,	82)	\
_ELF_DEFINE_RELOC(R_PPC_GOT_TLSLD16,	83)	\
_ELF_DEFINE_RELOC(R_PPC_GOT_TLSLD16_LO,	84)	\
_ELF_DEFINE_RELOC(R_PPC_GOT_TLSLD16_HI,	85)	\
_ELF_DEFINE_RELOC(R_PPC_GOT_TLSLD16_HA,	86)	\
_ELF_DEFINE_RELOC(R_PPC_GOT_TPREL16,	87)	\
_ELF_DEFINE_RELOC(R_PPC_GOT_TPREL16_LO,	88)	\
_ELF_DEFINE_RELOC(R_PPC_GOT_TPREL16_HI,	89)	\
_ELF_DEFINE_RELOC(R_PPC_GOT_TPREL16_HA,	90)	\
_ELF_DEFINE_RELOC(R_PPC_GOT_DTPREL16,	91)	\
_ELF_DEFINE_RELOC(R_PPC_GOT_DTPREL16_LO, 92)	\
_ELF_DEFINE_RELOC(R_PPC_GOT_DTPREL16_HI, 93)	\
_ELF_DEFINE_RELOC(R_PPC_GOT_DTPREL16_HA, 94)	\
_ELF_DEFINE_RELOC(R_PPC_TLSGD,		95)	\
_ELF_DEFINE_RELOC(R_PPC_TLSLD,		96)	\
_ELF_DEFINE_RELOC(R_PPC_EMB_NADDR32,	101)	\
_ELF_DEFINE_RELOC(R_PPC_EMB_NADDR16,	102)	\
_ELF_DEFINE_RELOC(R_PPC_EMB_NADDR16_LO,	103)	\
_ELF_DEFINE_RELOC(R_PPC_EMB_NADDR16_HI,	104)	\
_ELF_DEFINE_RELOC(R_PPC_EMB_NADDR16_HA,	105)	\
_ELF_DEFINE_RELOC(R_PPC_EMB_SDAI16,	106)	\
_ELF_DEFINE_RELOC(R_PPC_EMB_SDA2I16,	107)	\
_ELF_DEFINE_RELOC(R_PPC_EMB_SDA2REL,	108)	\
_ELF_DEFINE_RELOC(R_PPC_EMB_SDA21,	109)	\
_ELF_DEFINE_RELOC(R_PPC_EMB_MRKREF,	110)	\
_ELF_DEFINE_RELOC(R_PPC_EMB_RELSEC16,	111)	\
_ELF_DEFINE_RELOC(R_PPC_EMB_RELST_LO,	112)	\
_ELF_DEFINE_RELOC(R_PPC_EMB_RELST_HI,	113)	\
_ELF_DEFINE_RELOC(R_PPC_EMB_RELST_HA,	114)	\
_ELF_DEFINE_RELOC(R_PPC_EMB_BIT_FLD,	115)	\
_ELF_DEFINE_RELOC(R_PPC_EMB_RELSDA,	116)	\

#define	_ELF_DEFINE_PPC64_RELOCATIONS()			\
_ELF_DEFINE_RELOC(R_PPC64_NONE,			0)	\
_ELF_DEFINE_RELOC(R_PPC64_ADDR32,		1)	\
_ELF_DEFINE_RELOC(R_PPC64_ADDR24,		2)	\
_ELF_DEFINE_RELOC(R_PPC64_ADDR16,		3)	\
_ELF_DEFINE_RELOC(R_PPC64_ADDR16_LO,		4)	\
_ELF_DEFINE_RELOC(R_PPC64_ADDR16_HI,		5)	\
_ELF_DEFINE_RELOC(R_PPC64_ADDR16_HA,		6)	\
_ELF_DEFINE_RELOC(R_PPC64_ADDR14,		7)	\
_ELF_DEFINE_RELOC(R_PPC64_ADDR14_BRTAKEN,	8)	\
_ELF_DEFINE_RELOC(R_PPC64_ADDR14_BRNTAKEN,	9)	\
_ELF_DEFINE_RELOC(R_PPC64_REL24,		10)	\
_ELF_DEFINE_RELOC(R_PPC64_REL14,		11)	\
_ELF_DEFINE_RELOC(R_PPC64_REL14_BRTAKEN,	12)	\
_ELF_DEFINE_RELOC(R_PPC64_REL14_BRNTAKEN,	13)	\
_ELF_DEFINE_RELOC(R_PPC64_GOT16,		14)	\
_ELF_DEFINE_RELOC(R_PPC64_GOT16_LO,		15)	\
_ELF_DEFINE_RELOC(R_PPC64_GOT16_HI,		16)	\
_ELF_DEFINE_RELOC(R_PPC64_GOT16_HA,		17)	\
_ELF_DEFINE_RELOC(R_PPC64_COPY,			19)	\
_ELF_DEFINE_RELOC(R_PPC64_GLOB_DAT,		20)	\
_ELF_DEFINE_RELOC(R_PPC64_JMP_SLOT,		21)	\
_ELF_DEFINE_RELOC(R_PPC64_RELATIVE,		22)	\
_ELF_DEFINE_RELOC(R_PPC64_UADDR32,		24)	\
_ELF_DEFINE_RELOC(R_PPC64_UADDR16,		25)	\
_ELF_DEFINE_RELOC(R_PPC64_REL32,		26)	\
_ELF_DEFINE_RELOC(R_PPC64_PLT32,		27)	\
_ELF_DEFINE_RELOC(R_PPC64_PLTREL32,		28)	\
_ELF_DEFINE_RELOC(R_PPC64_PLT16_LO,		29)	\
_ELF_DEFINE_RELOC(R_PPC64_PLT16_HI,		30)	\
_ELF_DEFINE_RELOC(R_PPC64_PLT16_HA,		31)	\
_ELF_DEFINE_RELOC(R_PPC64_SECTOFF,		33)	\
_ELF_DEFINE_RELOC(R_PPC64_SECTOFF_LO,		34)	\
_ELF_DEFINE_RELOC(R_PPC64_SECTOFF_HI,		35)	\
_ELF_DEFINE_RELOC(R_PPC64_SECTOFF_HA,		36)	\
_ELF_DEFINE_RELOC(R_PPC64_ADDR30,		37)	\
_ELF_DEFINE_RELOC(R_PPC64_ADDR64,		38)	\
_ELF_DEFINE_RELOC(R_PPC64_ADDR16_HIGHER,	39)	\
_ELF_DEFINE_RELOC(R_PPC64_ADDR16_HIGHERA,	40)	\
_ELF_DEFINE_RELOC(R_PPC64_ADDR16_HIGHEST,	41)	\
_ELF_DEFINE_RELOC(R_PPC64_ADDR16_HIGHESTA,	42)	\
_ELF_DEFINE_RELOC(R_PPC64_UADDR64,		43)	\
_ELF_DEFINE_RELOC(R_PPC64_REL64,		44)	\
_ELF_DEFINE_RELOC(R_PPC64_PLT64,		45)	\
_ELF_DEFINE_RELOC(R_PPC64_PLTREL64,		46)	\
_ELF_DEFINE_RELOC(R_PPC64_TOC16,		47)	\
_ELF_DEFINE_RELOC(R_PPC64_TOC16_LO,		48)	\
_ELF_DEFINE_RELOC(R_PPC64_TOC16_HI,		49)	\
_ELF_DEFINE_RELOC(R_PPC64_TOC16_HA,		50)	\
_ELF_DEFINE_RELOC(R_PPC64_TOC,			51)	\
_ELF_DEFINE_RELOC(R_PPC64_PLTGOT16,		52)	\
_ELF_DEFINE_RELOC(R_PPC64_PLTGOT16_LO,		53)	\
_ELF_DEFINE_RELOC(R_PPC64_PLTGOT16_HI,		54)	\
_ELF_DEFINE_RELOC(R_PPC64_PLTGOT16_HA,		55)	\
_ELF_DEFINE_RELOC(R_PPC64_ADDR16_DS,		56)	\
_ELF_DEFINE_RELOC(R_PPC64_ADDR16_LO_DS,		57)	\
_ELF_DEFINE_RELOC(R_PPC64_GOT16_DS,		58)	\
_ELF_DEFINE_RELOC(R_PPC64_GOT16_LO_DS,		59)	\
_ELF_DEFINE_RELOC(R_PPC64_PLT16_LO_DS,		60)	\
_ELF_DEFINE_RELOC(R_PPC64_SECTOFF_DS,		61)	\
_ELF_DEFINE_RELOC(R_PPC64_SECTOFF_LO_DS,	62)	\
_ELF_DEFINE_RELOC(R_PPC64_TOC16_DS,		63)	\
_ELF_DEFINE_RELOC(R_PPC64_TOC16_LO_DS,		64)	\
_ELF_DEFINE_RELOC(R_PPC64_PLTGOT16_DS,		65)	\
_ELF_DEFINE_RELOC(R_PPC64_PLTGOT16_LO_DS,	66)	\
_ELF_DEFINE_RELOC(R_PPC64_TLS,			67)	\
_ELF_DEFINE_RELOC(R_PPC64_DTPMOD64,		68)	\
_ELF_DEFINE_RELOC(R_PPC64_TPREL16,		69)	\
_ELF_DEFINE_RELOC(R_PPC64_TPREL16_LO,		60)	\
_ELF_DEFINE_RELOC(R_PPC64_TPREL16_HI,		71)	\
_ELF_DEFINE_RELOC(R_PPC64_TPREL16_HA,		72)	\
_ELF_DEFINE_RELOC(R_PPC64_TPREL64,		73)	\
_ELF_DEFINE_RELOC(R_PPC64_DTPREL16,		74)	\
_ELF_DEFINE_RELOC(R_PPC64_DTPREL16_LO,		75)	\
_ELF_DEFINE_RELOC(R_PPC64_DTPREL16_HI,		76)	\
_ELF_DEFINE_RELOC(R_PPC64_DTPREL16_HA,		77)	\
_ELF_DEFINE_RELOC(R_PPC64_DTPREL64,		78)	\
_ELF_DEFINE_RELOC(R_PPC64_GOT_TLSGD16,		79)	\
_ELF_DEFINE_RELOC(R_PPC64_GOT_TLSGD16_LO,	80)	\
_ELF_DEFINE_RELOC(R_PPC64_GOT_TLSGD16_HI,	81)	\
_ELF_DEFINE_RELOC(R_PPC64_GOT_TLSGD16_HA,	82)	\
_ELF_DEFINE_RELOC(R_PPC64_GOT_TLSLD16,		83)	\
_ELF_DEFINE_RELOC(R_PPC64_GOT_TLSLD16_LO,	84)	\
_ELF_DEFINE_RELOC(R_PPC64_GOT_TLSLD16_HI,	85)	\
_ELF_DEFINE_RELOC(R_PPC64_GOT_TLSLD16_HA,	86)	\
_ELF_DEFINE_RELOC(R_PPC64_GOT_TPREL16_DS,	87)	\
_ELF_DEFINE_RELOC(R_PPC64_GOT_TPREL16_LO_DS,	88)	\
_ELF_DEFINE_RELOC(R_PPC64_GOT_TPREL16_HI,	89)	\
_ELF_DEFINE_RELOC(R_PPC64_GOT_TPREL16_HA,	90)	\
_ELF_DEFINE_RELOC(R_PPC64_GOT_DTPREL16_DS,	91)	\
_ELF_DEFINE_RELOC(R_PPC64_GOT_DTPREL16_LO_DS,	92)	\
_ELF_DEFINE_RELOC(R_PPC64_GOT_DTPREL16_HI,	93)	\
_ELF_DEFINE_RELOC(R_PPC64_GOT_DTPREL16_HA,	94)	\
_ELF_DEFINE_RELOC(R_PPC64_TPREL16_DS,		95)	\
_ELF_DEFINE_RELOC(R_PPC64_TPREL16_LO_DS,	96)	\
_ELF_DEFINE_RELOC(R_PPC64_TPREL16_HIGHER,	97)	\
_ELF_DEFINE_RELOC(R_PPC64_TPREL16_HIGHERA,	98)	\
_ELF_DEFINE_RELOC(R_PPC64_TPREL16_HIGHEST,	99)	\
_ELF_DEFINE_RELOC(R_PPC64_TPREL16_HIGHESTA,	100)	\
_ELF_DEFINE_RELOC(R_PPC64_DTPREL16_DS,		101)	\
_ELF_DEFINE_RELOC(R_PPC64_DTPREL16_LO_DS,	102)	\
_ELF_DEFINE_RELOC(R_PPC64_DTPREL16_HIGHER,	103)	\
_ELF_DEFINE_RELOC(R_PPC64_DTPREL16_HIGHERA,	104)	\
_ELF_DEFINE_RELOC(R_PPC64_DTPREL16_HIGHEST,	105)	\
_ELF_DEFINE_RELOC(R_PPC64_DTPREL16_HIGHESTA,	106)	\
_ELF_DEFINE_RELOC(R_PPC64_TLSGD,		107)	\
_ELF_DEFINE_RELOC(R_PPC64_TLSLD,		108)

#define	_ELF_DEFINE_RISCV_RELOCATIONS()			\
_ELF_DEFINE_RELOC(R_RISCV_NONE,			0)	\
_ELF_DEFINE_RELOC(R_RISCV_32,			1)	\
_ELF_DEFINE_RELOC(R_RISCV_64,			2)	\
_ELF_DEFINE_RELOC(R_RISCV_RELATIVE,		3)	\
_ELF_DEFINE_RELOC(R_RISCV_COPY,			4)	\
_ELF_DEFINE_RELOC(R_RISCV_JUMP_SLOT,		5)	\
_ELF_DEFINE_RELOC(R_RISCV_TLS_DTPMOD32,		6)	\
_ELF_DEFINE_RELOC(R_RISCV_TLS_DTPMOD64,		7)	\
_ELF_DEFINE_RELOC(R_RISCV_TLS_DTPREL32,		8)	\
_ELF_DEFINE_RELOC(R_RISCV_TLS_DTPREL64,		9)	\
_ELF_DEFINE_RELOC(R_RISCV_TLS_TPREL32,		10)	\
_ELF_DEFINE_RELOC(R_RISCV_TLS_TPREL64,		11)	\
_ELF_DEFINE_RELOC(R_RISCV_BRANCH,		16)	\
_ELF_DEFINE_RELOC(R_RISCV_JAL,			17)	\
_ELF_DEFINE_RELOC(R_RISCV_CALL,			18)	\
_ELF_DEFINE_RELOC(R_RISCV_CALL_PLT,		19)	\
_ELF_DEFINE_RELOC(R_RISCV_GOT_HI20,		20)	\
_ELF_DEFINE_RELOC(R_RISCV_TLS_GOT_HI20,		21)	\
_ELF_DEFINE_RELOC(R_RISCV_TLS_GD_HI20,		22)	\
_ELF_DEFINE_RELOC(R_RISCV_PCREL_HI20,		23)	\
_ELF_DEFINE_RELOC(R_RISCV_PCREL_LO12_I,		24)	\
_ELF_DEFINE_RELOC(R_RISCV_PCREL_LO12_S,		25)	\
_ELF_DEFINE_RELOC(R_RISCV_HI20,			26)	\
_ELF_DEFINE_RELOC(R_RISCV_LO12_I,		27)	\
_ELF_DEFINE_RELOC(R_RISCV_LO12_S,		28)	\
_ELF_DEFINE_RELOC(R_RISCV_TPREL_HI20,		29)	\
_ELF_DEFINE_RELOC(R_RISCV_TPREL_LO12_I,		30)	\
_ELF_DEFINE_RELOC(R_RISCV_TPREL_LO12_S,		31)	\
_ELF_DEFINE_RELOC(R_RISCV_TPREL_ADD,		32)	\
_ELF_DEFINE_RELOC(R_RISCV_ADD8,			33)	\
_ELF_DEFINE_RELOC(R_RISCV_ADD16,		34)	\
_ELF_DEFINE_RELOC(R_RISCV_ADD32,		35)	\
_ELF_DEFINE_RELOC(R_RISCV_ADD64,		36)	\
_ELF_DEFINE_RELOC(R_RISCV_SUB8,			37)	\
_ELF_DEFINE_RELOC(R_RISCV_SUB16,		38)	\
_ELF_DEFINE_RELOC(R_RISCV_SUB32,		39)	\
_ELF_DEFINE_RELOC(R_RISCV_SUB64,		40)	\
_ELF_DEFINE_RELOC(R_RISCV_GNU_VTINHERIT,	41)	\
_ELF_DEFINE_RELOC(R_RISCV_GNU_VTENTRY,		42)	\
_ELF_DEFINE_RELOC(R_RISCV_ALIGN,		43)	\
_ELF_DEFINE_RELOC(R_RISCV_RVC_BRANCH,		44)	\
_ELF_DEFINE_RELOC(R_RISCV_RVC_JUMP,		45)	\
_ELF_DEFINE_RELOC(R_RISCV_RVC_LUI,		46)	\
_ELF_DEFINE_RELOC(R_RISCV_GPREL_I,		47)	\
_ELF_DEFINE_RELOC(R_RISCV_GPREL_S,		48)

#define	_ELF_DEFINE_SPARC_RELOCATIONS()		\
_ELF_DEFINE_RELOC(R_SPARC_NONE,		0)	\
_ELF_DEFINE_RELOC(R_SPARC_8,		1)	\
_ELF_DEFINE_RELOC(R_SPARC_16,		2)	\
_ELF_DEFINE_RELOC(R_SPARC_32, 		3)	\
_ELF_DEFINE_RELOC(R_SPARC_DISP8,	4)	\
_ELF_DEFINE_RELOC(R_SPARC_DISP16,	5)	\
_ELF_DEFINE_RELOC(R_SPARC_DISP32,	6)	\
_ELF_DEFINE_RELOC(R_SPARC_WDISP30,	7)	\
_ELF_DEFINE_RELOC(R_SPARC_WDISP22,	8)	\
_ELF_DEFINE_RELOC(R_SPARC_HI22,		9)	\
_ELF_DEFINE_RELOC(R_SPARC_22,		10)	\
_ELF_DEFINE_RELOC(R_SPARC_13,		11)	\
_ELF_DEFINE_RELOC(R_SPARC_LO10,		12)	\
_ELF_DEFINE_RELOC(R_SPARC_GOT10,	13)	\
_ELF_DEFINE_RELOC(R_SPARC_GOT13,	14)	\
_ELF_DEFINE_RELOC(R_SPARC_GOT22,	15)	\
_ELF_DEFINE_RELOC(R_SPARC_PC10,		16)	\
_ELF_DEFINE_RELOC(R_SPARC_PC22,		17)	\
_ELF_DEFINE_RELOC(R_SPARC_WPLT30,	18)	\
_ELF_DEFINE_RELOC(R_SPARC_COPY,		19)	\
_ELF_DEFINE_RELOC(R_SPARC_GLOB_DAT,	20)	\
_ELF_DEFINE_RELOC(R_SPARC_JMP_SLOT,	21)	\
_ELF_DEFINE_RELOC(R_SPARC_RELATIVE,	22)	\
_ELF_DEFINE_RELOC(R_SPARC_UA32,		23)	\
_ELF_DEFINE_RELOC(R_SPARC_PLT32,	24)	\
_ELF_DEFINE_RELOC(R_SPARC_HIPLT22,	25)	\
_ELF_DEFINE_RELOC(R_SPARC_LOPLT10,	26)	\
_ELF_DEFINE_RELOC(R_SPARC_PCPLT32,	27)	\
_ELF_DEFINE_RELOC(R_SPARC_PCPLT22,	28)	\
_ELF_DEFINE_RELOC(R_SPARC_PCPLT10,	29)	\
_ELF_DEFINE_RELOC(R_SPARC_10,		30)	\
_ELF_DEFINE_RELOC(R_SPARC_11,		31)	\
_ELF_DEFINE_RELOC(R_SPARC_64,		32)	\
_ELF_DEFINE_RELOC(R_SPARC_OLO10,	33)	\
_ELF_DEFINE_RELOC(R_SPARC_HH22,		34)	\
_ELF_DEFINE_RELOC(R_SPARC_HM10,		35)	\
_ELF_DEFINE_RELOC(R_SPARC_LM22,		36)	\
_ELF_DEFINE_RELOC(R_SPARC_PC_HH22,	37)	\
_ELF_DEFINE_RELOC(R_SPARC_PC_HM10,	38)	\
_ELF_DEFINE_RELOC(R_SPARC_PC_LM22,	39)	\
_ELF_DEFINE_RELOC(R_SPARC_WDISP16,	40)	\
_ELF_DEFINE_RELOC(R_SPARC_WDISP19,	41)	\
_ELF_DEFINE_RELOC(R_SPARC_GLOB_JMP,	42)	\
_ELF_DEFINE_RELOC(R_SPARC_7,		43)	\
_ELF_DEFINE_RELOC(R_SPARC_5,		44)	\
_ELF_DEFINE_RELOC(R_SPARC_6,		45)	\
_ELF_DEFINE_RELOC(R_SPARC_DISP64,	46)	\
_ELF_DEFINE_RELOC(R_SPARC_PLT64,	47)	\
_ELF_DEFINE_RELOC(R_SPARC_HIX22,	48)	\
_ELF_DEFINE_RELOC(R_SPARC_LOX10,	49)	\
_ELF_DEFINE_RELOC(R_SPARC_H44,		50)	\
_ELF_DEFINE_RELOC(R_SPARC_M44,		51)	\
_ELF_DEFINE_RELOC(R_SPARC_L44,		52)	\
_ELF_DEFINE_RELOC(R_SPARC_REGISTER,	53)	\
_ELF_DEFINE_RELOC(R_SPARC_UA64,		54)	\
_ELF_DEFINE_RELOC(R_SPARC_UA16,		55)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_GD_HI22,	56)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_GD_LO10,	57)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_GD_ADD,	58)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_GD_CALL,	59)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_LDM_HI22,	60)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_LDM_LO10,	61)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_LDM_ADD,	62)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_LDM_CALL,	63)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_LDO_HIX22, 64)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_LDO_LOX10, 65)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_LDO_ADD,	66)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_IE_HI22,	67)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_IE_LO10,	68)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_IE_LD,	69)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_IE_LDX,	70)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_IE_ADD,	71)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_LE_HIX22,	72)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_LE_LOX10,	73)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_DTPMOD32,	74)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_DTPMOD64,	75)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_DTPOFF32,	76)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_DTPOFF64,	77)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_TPOFF32,	78)	\
_ELF_DEFINE_RELOC(R_SPARC_TLS_TPOFF64,	79)	\
_ELF_DEFINE_RELOC(R_SPARC_GOTDATA_HIX22, 80)	\
_ELF_DEFINE_RELOC(R_SPARC_GOTDATA_LOX10, 81)	\
_ELF_DEFINE_RELOC(R_SPARC_GOTDATA_OP_HIX22, 82)	\
_ELF_DEFINE_RELOC(R_SPARC_GOTDATA_OP_LOX10, 83)	\
_ELF_DEFINE_RELOC(R_SPARC_GOTDATA_OP,	84)	\
_ELF_DEFINE_RELOC(R_SPARC_H34,		85)

#define	_ELF_DEFINE_X86_64_RELOCATIONS()	\
_ELF_DEFINE_RELOC(R_X86_64_NONE,	0)	\
_ELF_DEFINE_RELOC(R_X86_64_64,		1)	\
_ELF_DEFINE_RELOC(R_X86_64_PC32,	2)	\
_ELF_DEFINE_RELOC(R_X86_64_GOT32,	3)	\
_ELF_DEFINE_RELOC(R_X86_64_PLT32,	4)	\
_ELF_DEFINE_RELOC(R_X86_64_COPY,	5)	\
_ELF_DEFINE_RELOC(R_X86_64_GLOB_DAT,	6)	\
_ELF_DEFINE_RELOC(R_X86_64_JUMP_SLOT,	7)	\
_ELF_DEFINE_RELOC(R_X86_64_RELATIVE,	8)	\
_ELF_DEFINE_RELOC(R_X86_64_GOTPCREL,	9)	\
_ELF_DEFINE_RELOC(R_X86_64_32,		10)	\
_ELF_DEFINE_RELOC(R_X86_64_32S,		11)	\
_ELF_DEFINE_RELOC(R_X86_64_16,		12)	\
_ELF_DEFINE_RELOC(R_X86_64_PC16,	13)	\
_ELF_DEFINE_RELOC(R_X86_64_8,		14)	\
_ELF_DEFINE_RELOC(R_X86_64_PC8,		15)	\
_ELF_DEFINE_RELOC(R_X86_64_DTPMOD64,	16)	\
_ELF_DEFINE_RELOC(R_X86_64_DTPOFF64,	17)	\
_ELF_DEFINE_RELOC(R_X86_64_TPOFF64,	18)	\
_ELF_DEFINE_RELOC(R_X86_64_TLSGD,	19)	\
_ELF_DEFINE_RELOC(R_X86_64_TLSLD,	20)	\
_ELF_DEFINE_RELOC(R_X86_64_DTPOFF32,	21)	\
_ELF_DEFINE_RELOC(R_X86_64_GOTTPOFF,	22)	\
_ELF_DEFINE_RELOC(R_X86_64_TPOFF32,	23)	\
_ELF_DEFINE_RELOC(R_X86_64_PC64,	24)	\
_ELF_DEFINE_RELOC(R_X86_64_GOTOFF64,	25)	\
_ELF_DEFINE_RELOC(R_X86_64_GOTPC32,	26)	\
_ELF_DEFINE_RELOC(R_X86_64_GOT64,	27)	\
_ELF_DEFINE_RELOC(R_X86_64_GOTPCREL64,	28)	\
_ELF_DEFINE_RELOC(R_X86_64_GOTPC64,	29)	\
_ELF_DEFINE_RELOC(R_X86_64_GOTPLT64,	30)	\
_ELF_DEFINE_RELOC(R_X86_64_PLTOFF64,	31)	\
_ELF_DEFINE_RELOC(R_X86_64_SIZE32,	32)	\
_ELF_DEFINE_RELOC(R_X86_64_SIZE64,	33)	\
_ELF_DEFINE_RELOC(R_X86_64_GOTPC32_TLSDESC, 34)	\
_ELF_DEFINE_RELOC(R_X86_64_TLSDESC_CALL, 35)	\
_ELF_DEFINE_RELOC(R_X86_64_TLSDESC,	36)	\
_ELF_DEFINE_RELOC(R_X86_64_IRELATIVE,	37)	\
_ELF_DEFINE_RELOC(R_X86_64_RELATIVE64,	38)	\
_ELF_DEFINE_RELOC(R_X86_64_GOTPCRELX,	41)	\
_ELF_DEFINE_RELOC(R_X86_64_REX_GOTPCRELX, 42)

#define	_ELF_DEFINE_RELOCATIONS()		\
_ELF_DEFINE_386_RELOCATIONS()			\
_ELF_DEFINE_AARCH64_RELOCATIONS()		\
_ELF_DEFINE_AMD64_RELOCATIONS()			\
_ELF_DEFINE_ARM_RELOCATIONS()			\
_ELF_DEFINE_IA64_RELOCATIONS()			\
_ELF_DEFINE_MIPS_RELOCATIONS()			\
_ELF_DEFINE_PPC32_RELOCATIONS()			\
_ELF_DEFINE_PPC64_RELOCATIONS()			\
_ELF_DEFINE_RISCV_RELOCATIONS()			\
_ELF_DEFINE_SPARC_RELOCATIONS()			\
_ELF_DEFINE_X86_64_RELOCATIONS()

#undef	_ELF_DEFINE_RELOC
#define	_ELF_DEFINE_RELOC(N, V)		N = V ,
enum {
	_ELF_DEFINE_RELOCATIONS()
	R__LAST__
};

#define	PN_XNUM			0xFFFFU /* Use extended section numbering. */

/**
 ** ELF Types.
 **/

typedef uint32_t	Elf32_Addr;	/* Program address. */
typedef uint8_t		Elf32_Byte;	/* Unsigned tiny integer. */
typedef uint16_t	Elf32_Half;	/* Unsigned medium integer. */
typedef uint32_t	Elf32_Off;	/* File offset. */
typedef uint16_t	Elf32_Section;	/* Section index. */
typedef int32_t		Elf32_Sword;	/* Signed integer. */
typedef uint32_t	Elf32_Word;	/* Unsigned integer. */
typedef uint64_t	Elf32_Lword;	/* Unsigned long integer. */

typedef uint64_t	Elf64_Addr;	/* Program address. */
typedef uint8_t		Elf64_Byte;	/* Unsigned tiny integer. */
typedef uint16_t	Elf64_Half;	/* Unsigned medium integer. */
typedef uint64_t	Elf64_Off;	/* File offset. */
typedef uint16_t	Elf64_Section;	/* Section index. */
typedef int32_t		Elf64_Sword;	/* Signed integer. */
typedef uint32_t	Elf64_Word;	/* Unsigned integer. */
typedef uint64_t	Elf64_Lword;	/* Unsigned long integer. */
typedef uint64_t	Elf64_Xword;	/* Unsigned long integer. */
typedef int64_t		Elf64_Sxword;	/* Signed long integer. */


/*
 * Capability descriptors.
 */

/* 32-bit capability descriptor. */
typedef struct {
	Elf32_Word	c_tag;	     /* Type of entry. */
	union {
		Elf32_Word	c_val; /* Integer value. */
		Elf32_Addr	c_ptr; /* Pointer value. */
	} c_un;
} Elf32_Cap;

/* 64-bit capability descriptor. */
typedef struct {
	Elf64_Xword	c_tag;	     /* Type of entry. */
	union {
		Elf64_Xword	c_val; /* Integer value. */
		Elf64_Addr	c_ptr; /* Pointer value. */
	} c_un;
} Elf64_Cap;

/*
 * MIPS .conflict section entries.
 */

/* 32-bit entry. */
typedef struct {
	Elf32_Addr	c_index;
} Elf32_Conflict;

/* 64-bit entry. */
typedef struct {
	Elf64_Addr	c_index;
} Elf64_Conflict;

/*
 * Dynamic section entries.
 */

/* 32-bit entry. */
typedef struct {
	Elf32_Sword	d_tag;	     /* Type of entry. */
	union {
		Elf32_Word	d_val; /* Integer value. */
		Elf32_Addr	d_ptr; /* Pointer value. */
	} d_un;
} Elf32_Dyn;

/* 64-bit entry. */
typedef struct {
	Elf64_Sxword	d_tag;	     /* Type of entry. */
	union {
		Elf64_Xword	d_val; /* Integer value. */
		Elf64_Addr	d_ptr; /* Pointer value; */
	} d_un;
} Elf64_Dyn;


/*
 * The executable header (EHDR).
 */

/* 32 bit EHDR. */
typedef struct {
	unsigned char   e_ident[EI_NIDENT]; /* ELF identification. */
	Elf32_Half      e_type;	     /* Object file type (ET_*). */
	Elf32_Half      e_machine;   /* Machine type (EM_*). */
	Elf32_Word      e_version;   /* File format version (EV_*). */
	Elf32_Addr      e_entry;     /* Start address. */
	Elf32_Off       e_phoff;     /* File offset to the PHDR table. */
	Elf32_Off       e_shoff;     /* File offset to the SHDRheader. */
	Elf32_Word      e_flags;     /* Flags (EF_*). */
	Elf32_Half      e_ehsize;    /* Elf header size in bytes. */
	Elf32_Half      e_phentsize; /* PHDR table entry size in bytes. */
	Elf32_Half      e_phnum;     /* Number of PHDR entries. */
	Elf32_Half      e_shentsize; /* SHDR table entry size in bytes. */
	Elf32_Half      e_shnum;     /* Number of SHDR entries. */
	Elf32_Half      e_shstrndx;  /* Index of section name string table. */
} Elf32_Ehdr;


/* 64 bit EHDR. */
typedef struct {
	unsigned char   e_ident[EI_NIDENT]; /* ELF identification. */
	Elf64_Half      e_type;	     /* Object file type (ET_*). */
	Elf64_Half      e_machine;   /* Machine type (EM_*). */
	Elf64_Word      e_version;   /* File format version (EV_*). */
	Elf64_Addr      e_entry;     /* Start address. */
	Elf64_Off       e_phoff;     /* File offset to the PHDR table. */
	Elf64_Off       e_shoff;     /* File offset to the SHDRheader. */
	Elf64_Word      e_flags;     /* Flags (EF_*). */
	Elf64_Half      e_ehsize;    /* Elf header size in bytes. */
	Elf64_Half      e_phentsize; /* PHDR table entry size in bytes. */
	Elf64_Half      e_phnum;     /* Number of PHDR entries. */
	Elf64_Half      e_shentsize; /* SHDR table entry size in bytes. */
	Elf64_Half      e_shnum;     /* Number of SHDR entries. */
	Elf64_Half      e_shstrndx;  /* Index of section name string table. */
} Elf64_Ehdr;


/*
 * Shared object information.
 */

/* 32-bit entry. */
typedef struct {
	Elf32_Word l_name;	     /* The name of a shared object. */
	Elf32_Word l_time_stamp;     /* 32-bit timestamp. */
	Elf32_Word l_checksum;	     /* Checksum of visible symbols, sizes. */
	Elf32_Word l_version;	     /* Interface version string index. */
	Elf32_Word l_flags;	     /* Flags (LL_*). */
} Elf32_Lib;

/* 64-bit entry. */
typedef struct {
	Elf64_Word l_name;	     /* The name of a shared object. */
	Elf64_Word l_time_stamp;     /* 32-bit timestamp. */
	Elf64_Word l_checksum;	     /* Checksum of visible symbols, sizes. */
	Elf64_Word l_version;	     /* Interface version string index. */
	Elf64_Word l_flags;	     /* Flags (LL_*). */
} Elf64_Lib;

#define	_ELF_DEFINE_LL_FLAGS()			\
_ELF_DEFINE_LL(LL_NONE,			0,	\
	"no flags")				\
_ELF_DEFINE_LL(LL_EXACT_MATCH,		0x1,	\
	"require an exact match")		\
_ELF_DEFINE_LL(LL_IGNORE_INT_VER,	0x2,	\
	"ignore version incompatibilities")	\
_ELF_DEFINE_LL(LL_REQUIRE_MINOR,	0x4,	\
	"")					\
_ELF_DEFINE_LL(LL_EXPORTS,		0x8,	\
	"")					\
_ELF_DEFINE_LL(LL_DELAY_LOAD,		0x10,	\
	"")					\
_ELF_DEFINE_LL(LL_DELTA,		0x20,	\
	"")

#undef	_ELF_DEFINE_LL
#define	_ELF_DEFINE_LL(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_LL_FLAGS()
	LL__LAST__
};

/*
 * Note tags
 */

#define	_ELF_DEFINE_NOTE_ENTRY_TYPES()					\
_ELF_DEFINE_NT(NT_ABI_TAG,	1,	"Tag indicating the ABI")	\
_ELF_DEFINE_NT(NT_GNU_HWCAP,	2,	"Hardware capabilities")	\
_ELF_DEFINE_NT(NT_GNU_BUILD_ID,	3,	"Build id, set by ld(1)")	\
_ELF_DEFINE_NT(NT_GNU_GOLD_VERSION, 4,					\
	"Version number of the GNU gold linker")			\
_ELF_DEFINE_NT(NT_PRSTATUS,	1,	"Process status")		\
_ELF_DEFINE_NT(NT_FPREGSET,	2,	"Floating point information")	\
_ELF_DEFINE_NT(NT_PRPSINFO,	3,	"Process information")		\
_ELF_DEFINE_NT(NT_AUXV,		6,	"Auxiliary vector")		\
_ELF_DEFINE_NT(NT_PRXFPREG,	0x46E62B7FUL,				\
	"Linux user_xfpregs structure")					\
_ELF_DEFINE_NT(NT_PSTATUS,	10,	"Linux process status")		\
_ELF_DEFINE_NT(NT_FPREGS,	12,	"Linux floating point regset")	\
_ELF_DEFINE_NT(NT_PSINFO,	13,	"Linux process information")	\
_ELF_DEFINE_NT(NT_LWPSTATUS,	16,	"Linux lwpstatus_t type")	\
_ELF_DEFINE_NT(NT_LWPSINFO,	17,	"Linux lwpinfo_t type")

#undef	_ELF_DEFINE_NT
#define	_ELF_DEFINE_NT(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_NOTE_ENTRY_TYPES()
	NT__LAST__
};

/* Aliases for the ABI tag. */
#define	NT_FREEBSD_ABI_TAG	NT_ABI_TAG
#define	NT_GNU_ABI_TAG		NT_ABI_TAG
#define	NT_NETBSD_IDENT		NT_ABI_TAG
#define	NT_OPENBSD_IDENT	NT_ABI_TAG

/*
 * Note descriptors.
 */

typedef	struct {
	uint32_t	n_namesz;    /* Length of note's name. */
	uint32_t	n_descsz;    /* Length of note's value. */
	uint32_t	n_type;	     /* Type of note. */
} Elf_Note;

typedef Elf_Note Elf32_Nhdr;	     /* 32-bit note header. */
typedef Elf_Note Elf64_Nhdr;	     /* 64-bit note header. */

/*
 * MIPS ELF options descriptor header.
 */

typedef struct {
	Elf64_Byte	kind;        /* Type of options. */
	Elf64_Byte     	size;	     /* Size of option descriptor. */
	Elf64_Half	section;     /* Index of section affected. */
	Elf64_Word	info;        /* Kind-specific information. */
} Elf_Options;

/*
 * Option kinds.
 */

#define	_ELF_DEFINE_OPTION_KINDS()					\
_ELF_DEFINE_ODK(ODK_NULL,       0,      "undefined")			\
_ELF_DEFINE_ODK(ODK_REGINFO,    1,      "register usage info")		\
_ELF_DEFINE_ODK(ODK_EXCEPTIONS, 2,      "exception processing info")	\
_ELF_DEFINE_ODK(ODK_PAD,        3,      "section padding")		\
_ELF_DEFINE_ODK(ODK_HWPATCH,    4,      "hardware patch applied")	\
_ELF_DEFINE_ODK(ODK_FILL,       5,      "fill value used by linker")	\
_ELF_DEFINE_ODK(ODK_TAGS,       6,      "reserved space for tools")	\
_ELF_DEFINE_ODK(ODK_HWAND,      7,      "hardware AND patch applied")	\
_ELF_DEFINE_ODK(ODK_HWOR,       8,      "hardware OR patch applied")	\
_ELF_DEFINE_ODK(ODK_GP_GROUP,   9,					\
	"GP group to use for text/data sections")			\
_ELF_DEFINE_ODK(ODK_IDENT,      10,     "ID information")		\
_ELF_DEFINE_ODK(ODK_PAGESIZE,   11,     "page size information")

#undef	_ELF_DEFINE_ODK
#define	_ELF_DEFINE_ODK(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_OPTION_KINDS()
	ODK__LAST__
};

/*
 * ODK_EXCEPTIONS info field masks.
 */

#define	_ELF_DEFINE_ODK_EXCEPTIONS_MASK()				\
_ELF_DEFINE_OEX(OEX_FPU_MIN,    0x0000001FUL,				\
	"minimum FPU exception which must be enabled")			\
_ELF_DEFINE_OEX(OEX_FPU_MAX,    0x00001F00UL,				\
	"maximum FPU exception which can be enabled")			\
_ELF_DEFINE_OEX(OEX_PAGE0,      0x00010000UL,				\
	"page zero must be mapped")					\
_ELF_DEFINE_OEX(OEX_SMM,        0x00020000UL,				\
	"run in sequential memory mode")				\
_ELF_DEFINE_OEX(OEX_PRECISEFP,  0x00040000UL,				\
	"run in precise FP exception mode")				\
_ELF_DEFINE_OEX(OEX_DISMISS,    0x00080000UL,				\
	"dismiss invalid address traps")

#undef	_ELF_DEFINE_OEX
#define	_ELF_DEFINE_OEX(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_ODK_EXCEPTIONS_MASK()
	OEX__LAST__
};

/*
 * ODK_PAD info field masks.
 */

#define	_ELF_DEFINE_ODK_PAD_MASK()					\
_ELF_DEFINE_OPAD(OPAD_PREFIX,   0x0001)					\
_ELF_DEFINE_OPAD(OPAD_POSTFIX,  0x0002)					\
_ELF_DEFINE_OPAD(OPAD_SYMBOL,   0x0004)

#undef	_ELF_DEFINE_OPAD
#define	_ELF_DEFINE_OPAD(N, V)		N = V ,
enum {
	_ELF_DEFINE_ODK_PAD_MASK()
	OPAD__LAST__
};

/*
 * ODK_HWPATCH info field masks.
 */

#define	_ELF_DEFINE_ODK_HWPATCH_MASK()					\
_ELF_DEFINE_OHW(OHW_R4KEOP,     0x00000001UL,				\
	"patch for R4000 branch at end-of-page bug")			\
_ELF_DEFINE_OHW(OHW_R8KPFETCH,  0x00000002UL,				\
	"R8000 prefetch bug may occur")					\
_ELF_DEFINE_OHW(OHW_R5KEOP,     0x00000004UL,				\
	"patch for R5000 branch at end-of-page bug")			\
_ELF_DEFINE_OHW(OHW_R5KCVTL,    0x00000008UL,				\
	"R5000 cvt.[ds].l bug: clean == 1")				\
_ELF_DEFINE_OHW(OHW_R10KLDL,    0x00000010UL,				\
	"needd patch for R10000 misaligned load")

#undef	_ELF_DEFINE_OHW
#define	_ELF_DEFINE_OHW(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_ODK_HWPATCH_MASK()
	OHW__LAST__
};

/*
 * ODK_HWAND/ODK_HWOR info field and hwp_flags[12] masks.
 */

#define	_ELF_DEFINE_ODK_HWP_MASK()					\
_ELF_DEFINE_HWP(OHWA0_R4KEOP_CHECKED, 0x00000001UL,			\
	"object checked for R4000 end-of-page bug")			\
_ELF_DEFINE_HWP(OHWA0_R4KEOP_CLEAN, 0x00000002UL,			\
	"object verified clean for R4000 end-of-page bug")		\
_ELF_DEFINE_HWP(OHWO0_FIXADE,   0x00000001UL,				\
	"object requires call to fixade")

#undef	_ELF_DEFINE_HWP
#define	_ELF_DEFINE_HWP(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_ODK_HWP_MASK()
	OHWX0__LAST__
};

/*
 * ODK_IDENT/ODK_GP_GROUP info field masks.
 */

#define	_ELF_DEFINE_ODK_GP_MASK()					\
_ELF_DEFINE_OGP(OGP_GROUP,      0x0000FFFFUL, "GP group number")	\
_ELF_DEFINE_OGP(OGP_SELF,       0x00010000UL,				\
	"GP group is self-contained")

#undef	_ELF_DEFINE_OGP
#define	_ELF_DEFINE_OGP(N, V, DESCR)	N = V ,
enum {
	_ELF_DEFINE_ODK_GP_MASK()
	OGP__LAST__
};

/*
 * MIPS ELF register info descriptor.
 */

/* 32 bit RegInfo entry. */
typedef struct {
	Elf32_Word	ri_gprmask;  /* Mask of general register used. */
	Elf32_Word	ri_cprmask[4]; /* Mask of coprocessor register used. */
	Elf32_Addr	ri_gp_value; /* GP register value. */
} Elf32_RegInfo;

/* 64 bit RegInfo entry. */
typedef struct {
	Elf64_Word	ri_gprmask;  /* Mask of general register used. */
	Elf64_Word	ri_pad;	     /* Padding. */
	Elf64_Word	ri_cprmask[4]; /* Mask of coprocessor register used. */
	Elf64_Addr	ri_gp_value; /* GP register value. */
} Elf64_RegInfo;

/*
 * Program Header Table (PHDR) entries.
 */

/* 32 bit PHDR entry. */
typedef struct {
	Elf32_Word	p_type;	     /* Type of segment. */
	Elf32_Off	p_offset;    /* File offset to segment. */
	Elf32_Addr	p_vaddr;     /* Virtual address in memory. */
	Elf32_Addr	p_paddr;     /* Physical address (if relevant). */
	Elf32_Word	p_filesz;    /* Size of segment in file. */
	Elf32_Word	p_memsz;     /* Size of segment in memory. */
	Elf32_Word	p_flags;     /* Segment flags. */
	Elf32_Word	p_align;     /* Alignment constraints. */
} Elf32_Phdr;

/* 64 bit PHDR entry. */
typedef struct {
	Elf64_Word	p_type;	     /* Type of segment. */
	Elf64_Word	p_flags;     /* Segment flags. */
	Elf64_Off	p_offset;    /* File offset to segment. */
	Elf64_Addr	p_vaddr;     /* Virtual address in memory. */
	Elf64_Addr	p_paddr;     /* Physical address (if relevant). */
	Elf64_Xword	p_filesz;    /* Size of segment in file. */
	Elf64_Xword	p_memsz;     /* Size of segment in memory. */
	Elf64_Xword	p_align;     /* Alignment constraints. */
} Elf64_Phdr;


/*
 * Move entries, for describing data in COMMON blocks in a compact
 * manner.
 */

/* 32-bit move entry. */
typedef struct {
	Elf32_Lword	m_value;     /* Initialization value. */
	Elf32_Word 	m_info;	     /* Encoded size and index. */
	Elf32_Word	m_poffset;   /* Offset relative to symbol. */
	Elf32_Half	m_repeat;    /* Repeat count. */
	Elf32_Half	m_stride;    /* Number of units to skip. */
} Elf32_Move;

/* 64-bit move entry. */
typedef struct {
	Elf64_Lword	m_value;     /* Initialization value. */
	Elf64_Xword 	m_info;	     /* Encoded size and index. */
	Elf64_Xword	m_poffset;   /* Offset relative to symbol. */
	Elf64_Half	m_repeat;    /* Repeat count. */
	Elf64_Half	m_stride;    /* Number of units to skip. */
} Elf64_Move;

#define ELF32_M_SYM(I)		((I) >> 8)
#define ELF32_M_SIZE(I)		((unsigned char) (I))
#define ELF32_M_INFO(M, S)	(((M) << 8) + (unsigned char) (S))

#define ELF64_M_SYM(I)		((I) >> 8)
#define ELF64_M_SIZE(I)		((unsigned char) (I))
#define ELF64_M_INFO(M, S)	(((M) << 8) + (unsigned char) (S))

/*
 * Section Header Table (SHDR) entries.
 */

/* 32 bit SHDR */
typedef struct {
	Elf32_Word	sh_name;     /* index of section name */
	Elf32_Word	sh_type;     /* section type */
	Elf32_Word	sh_flags;    /* section flags */
	Elf32_Addr	sh_addr;     /* in-memory address of section */
	Elf32_Off	sh_offset;   /* file offset of section */
	Elf32_Word	sh_size;     /* section size in bytes */
	Elf32_Word	sh_link;     /* section header table link */
	Elf32_Word	sh_info;     /* extra information */
	Elf32_Word	sh_addralign; /* alignment constraint */
	Elf32_Word	sh_entsize;   /* size for fixed-size entries */
} Elf32_Shdr;

/* 64 bit SHDR */
typedef struct {
	Elf64_Word	sh_name;     /* index of section name */
	Elf64_Word	sh_type;     /* section type */
	Elf64_Xword	sh_flags;    /* section flags */
	Elf64_Addr	sh_addr;     /* in-memory address of section */
	Elf64_Off	sh_offset;   /* file offset of section */
	Elf64_Xword	sh_size;     /* section size in bytes */
	Elf64_Word	sh_link;     /* section header table link */
	Elf64_Word	sh_info;     /* extra information */
	Elf64_Xword	sh_addralign; /* alignment constraint */
	Elf64_Xword	sh_entsize;  /* size for fixed-size entries */
} Elf64_Shdr;


/*
 * Symbol table entries.
 */

typedef struct {
	Elf32_Word	st_name;     /* index of symbol's name */
	Elf32_Addr	st_value;    /* value for the symbol */
	Elf32_Word	st_size;     /* size of associated data */
	unsigned char	st_info;     /* type and binding attributes */
	unsigned char	st_other;    /* visibility */
	Elf32_Half	st_shndx;    /* index of related section */
} Elf32_Sym;

typedef struct {
	Elf64_Word	st_name;     /* index of symbol's name */
	unsigned char	st_info;     /* type and binding attributes */
	unsigned char	st_other;    /* visibility */
	Elf64_Half	st_shndx;    /* index of related section */
	Elf64_Addr	st_value;    /* value for the symbol */
	Elf64_Xword	st_size;     /* size of associated data */
} Elf64_Sym;

#define ELF32_ST_BIND(I)	((I) >> 4)
#define ELF32_ST_TYPE(I)	((I) & 0xFU)
#define ELF32_ST_INFO(B,T)	(((B) << 4) + ((T) & 0xF))

#define ELF64_ST_BIND(I)	((I) >> 4)
#define ELF64_ST_TYPE(I)	((I) & 0xFU)
#define ELF64_ST_INFO(B,T)	(((B) << 4) + ((T) & 0xF))

#define ELF32_ST_VISIBILITY(O)	((O) & 0x3)
#define ELF64_ST_VISIBILITY(O)	((O) & 0x3)

/*
 * Syminfo descriptors, containing additional symbol information.
 */

/* 32-bit entry. */
typedef struct {
	Elf32_Half	si_boundto;  /* Entry index with additional flags. */
	Elf32_Half	si_flags;    /* Flags. */
} Elf32_Syminfo;

/* 64-bit entry. */
typedef struct {
	Elf64_Half	si_boundto;  /* Entry index with additional flags. */
	Elf64_Half	si_flags;    /* Flags. */
} Elf64_Syminfo;

/*
 * Relocation descriptors.
 */

typedef struct {
	Elf32_Addr	r_offset;    /* location to apply relocation to */
	Elf32_Word	r_info;	     /* type+section for relocation */
} Elf32_Rel;

typedef struct {
	Elf32_Addr	r_offset;    /* location to apply relocation to */
	Elf32_Word	r_info;      /* type+section for relocation */
	Elf32_Sword	r_addend;    /* constant addend */
} Elf32_Rela;

typedef struct {
	Elf64_Addr	r_offset;    /* location to apply relocation to */
	Elf64_Xword	r_info;      /* type+section for relocation */
} Elf64_Rel;

typedef struct {
	Elf64_Addr	r_offset;    /* location to apply relocation to */
	Elf64_Xword	r_info;      /* type+section for relocation */
	Elf64_Sxword	r_addend;    /* constant addend */
} Elf64_Rela;


#define ELF32_R_SYM(I)		((I) >> 8)
#define ELF32_R_TYPE(I)		((unsigned char) (I))
#define ELF32_R_INFO(S,T)	(((S) << 8) + (unsigned char) (T))

#define ELF64_R_SYM(I)		((I) >> 32)
#define ELF64_R_TYPE(I)		((I) & 0xFFFFFFFFUL)
#define ELF64_R_INFO(S,T)	(((S) << 32) + ((T) & 0xFFFFFFFFUL))

/*
 * Symbol versioning structures.
 */

/* 32-bit structures. */
typedef struct
{
	Elf32_Word	vda_name;    /* Index to name. */
	Elf32_Word	vda_next;    /* Offset to next entry. */
} Elf32_Verdaux;

typedef struct
{
	Elf32_Word	vna_hash;    /* Hash value of dependency name. */
	Elf32_Half	vna_flags;   /* Flags. */
	Elf32_Half	vna_other;   /* Unused. */
	Elf32_Word	vna_name;    /* Offset to dependency name. */
	Elf32_Word	vna_next;    /* Offset to next vernaux entry. */
} Elf32_Vernaux;

typedef struct
{
	Elf32_Half	vd_version;  /* Version information. */
	Elf32_Half	vd_flags;    /* Flags. */
	Elf32_Half	vd_ndx;	     /* Index into the versym section. */
	Elf32_Half	vd_cnt;	     /* Number of aux entries. */
	Elf32_Word	vd_hash;     /* Hash value of name. */
	Elf32_Word	vd_aux;	     /* Offset to aux entries. */
	Elf32_Word	vd_next;     /* Offset to next version definition. */
} Elf32_Verdef;

typedef struct
{
	Elf32_Half	vn_version;  /* Version number. */
	Elf32_Half	vn_cnt;	     /* Number of aux entries. */
	Elf32_Word	vn_file;     /* Offset of associated file name. */
	Elf32_Word	vn_aux;	     /* Offset of vernaux array. */
	Elf32_Word	vn_next;     /* Offset of next verneed entry. */
} Elf32_Verneed;

typedef Elf32_Half	Elf32_Versym;

/* 64-bit structures. */

typedef struct {
	Elf64_Word	vda_name;    /* Index to name. */
	Elf64_Word	vda_next;    /* Offset to next entry. */
} Elf64_Verdaux;

typedef struct {
	Elf64_Word	vna_hash;    /* Hash value of dependency name. */
	Elf64_Half	vna_flags;   /* Flags. */
	Elf64_Half	vna_other;   /* Unused. */
	Elf64_Word	vna_name;    /* Offset to dependency name. */
	Elf64_Word	vna_next;    /* Offset to next vernaux entry. */
} Elf64_Vernaux;

typedef struct {
	Elf64_Half	vd_version;  /* Version information. */
	Elf64_Half	vd_flags;    /* Flags. */
	Elf64_Half	vd_ndx;	     /* Index into the versym section. */
	Elf64_Half	vd_cnt;	     /* Number of aux entries. */
	Elf64_Word	vd_hash;     /* Hash value of name. */
	Elf64_Word	vd_aux;	     /* Offset to aux entries. */
	Elf64_Word	vd_next;     /* Offset to next version definition. */
} Elf64_Verdef;

typedef struct {
	Elf64_Half	vn_version;  /* Version number. */
	Elf64_Half	vn_cnt;	     /* Number of aux entries. */
	Elf64_Word	vn_file;     /* Offset of associated file name. */
	Elf64_Word	vn_aux;	     /* Offset of vernaux array. */
	Elf64_Word	vn_next;     /* Offset of next verneed entry. */
} Elf64_Verneed;

typedef Elf64_Half	Elf64_Versym;


/*
 * The header for GNU-style hash sections.
 */

typedef struct {
	uint32_t	gh_nbuckets;	/* Number of hash buckets. */
	uint32_t	gh_symndx;	/* First visible symbol in .dynsym. */
	uint32_t	gh_maskwords;	/* #maskwords used in bloom filter. */
	uint32_t	gh_shift2;	/* Bloom filter shift count. */
} Elf_GNU_Hash_Header;

#endif	/* _ELFDEFINITIONS_H_ */
