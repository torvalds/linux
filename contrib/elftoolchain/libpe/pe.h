/*-
 * Copyright (c) 2015 Kai Wang
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
 * $Id: pe.h 3441 2016-04-07 15:04:20Z emaste $
 */

#ifndef	_PE_H_
#define	_PE_H_

#include <stdint.h>

/*
 * MS-DOS header.
 */

typedef struct _PE_DosHdr {
	char dh_magic[2];
	uint16_t dh_lastsize;
	uint16_t dh_nblock;
	uint16_t dh_nreloc;
	uint16_t dh_hdrsize;
	uint16_t dh_minalloc;
	uint16_t dh_maxalloc;
	uint16_t dh_ss;
	uint16_t dh_sp;
	uint16_t dh_checksum;
	uint16_t dh_ip;
	uint16_t dh_cs;
	uint16_t dh_relocpos;
	uint16_t dh_noverlay;
	uint16_t dh_reserved1[4];
	uint16_t dh_oemid;
	uint16_t dh_oeminfo;
	uint16_t dh_reserved2[10];
	uint32_t dh_lfanew;
} PE_DosHdr;

/*
 * Rich header.
 */

typedef struct _PE_RichHdr {
	uint32_t rh_xor;
	uint32_t rh_total;
	uint32_t *rh_compid;
	uint32_t *rh_cnt;
} PE_RichHdr;

/*
 * COFF header: Machine Types.
 */

#define	IMAGE_FILE_MACHINE_UNKNOWN	0x0	/* not specified */
#define	IMAGE_FILE_MACHINE_AM33		0x1d3	/* Matsushita AM33 */
#define	IMAGE_FILE_MACHINE_AMD64	0x8664	/* x86-64 */
#define	IMAGE_FILE_MACHINE_ARM		0x1c0	/* ARM LE */
#define	IMAGE_FILE_MACHINE_ARMNT	0x1c4	/* ARMv7(or higher) Thumb */
#define	IMAGE_FILE_MACHINE_ARM64	0xaa64	/* ARMv8 64-bit */
#define	IMAGE_FILE_MACHINE_EBC		0xebc	/* EFI byte code */
#define	IMAGE_FILE_MACHINE_I386		0x14c	/* x86 */
#define	IMAGE_FILE_MACHINE_IA64		0x200	/* IA64 */
#define	IMAGE_FILE_MACHINE_M32R		0x9041	/* Mitsubishi M32R LE */
#define	IMAGE_FILE_MACHINE_MIPS16	0x266	/* MIPS16 */
#define	IMAGE_FILE_MACHINE_MIPSFPU	0x366	/* MIPS with FPU */
#define	IMAGE_FILE_MACHINE_MIPSFPU16	0x466	/* MIPS16 with FPU */
#define	IMAGE_FILE_MACHINE_POWERPC	0x1f0	/* Power PC LE */
#define	IMAGE_FILE_MACHINE_POWERPCFP	0x1f1	/* Power PC floating point */
#define	IMAGE_FILE_MACHINE_R4000	0x166	/* MIPS R4000 LE */
#define	IMAGE_FILE_MACHINE_RISCV32	0x5032	/* RISC-V 32-bit */
#define	IMAGE_FILE_MACHINE_RISCV64	0x5064	/* RISC-V 64-bit */
#define	IMAGE_FILE_MACHINE_RISCV128	0x5128	/* RISC-V 128-bit */
#define	IMAGE_FILE_MACHINE_SH3		0x1a2	/* Hitachi SH3 */
#define	IMAGE_FILE_MACHINE_SH3DSP	0x1a3	/* Hitachi SH3 DSP */
#define	IMAGE_FILE_MACHINE_SH4		0x1a6	/* Hitachi SH4 */
#define	IMAGE_FILE_MACHINE_SH5		0x1a8	/* Hitachi SH5 */
#define	IMAGE_FILE_MACHINE_THUMB	0x1c2	/* ARM or Thumb interworking */
#define	IMAGE_FILE_MACHINE_WCEMIPSV2	0x169	/* MIPS LE WCE v2 */

/*
 * COFF header: Characteristics
 */

#define	IMAGE_FILE_RELOCS_STRIPPED		0x0001
#define	IMAGE_FILE_EXECUTABLE_IMAGE		0x0002
#define	IMAGE_FILE_LINE_NUMS_STRIPPED		0x0004
#define	IMAGE_FILE_LOCAL_SYMS_STRIPPED		0x0008
#define	IMAGE_FILE_AGGRESSIVE_WS_TRIM		0x0010
#define	IMAGE_FILE_LARGE_ADDRESS_AWARE		0x0020
#define	IMAGE_FILE_BYTES_REVERSED_LO		0x0080
#define	IMAGE_FILE_32BIT_MACHINE		0x0100
#define	IMAGE_FILE_DEBUG_STRIPPED		0x0200
#define	IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP	0x0400
#define	IMAGE_FILE_NET_RUN_FROM_SWAP		0x0800
#define	IMAGE_FILE_SYSTEM			0x1000
#define	IMAGE_FILE_DLL				0x2000
#define	IMAGE_FILE_UP_SYSTEM_ONLY		0x4000
#define	IMAGE_FILE_BYTES_REVERSED_HI		0x8000

/*
 * COFF Header.
 */

typedef struct _PE_CoffHdr {
	uint16_t ch_machine;
	uint16_t ch_nsec;
	uint32_t ch_timestamp;
	uint32_t ch_symptr;
	uint32_t ch_nsym;
	uint16_t ch_optsize;
	uint16_t ch_char;
} PE_CoffHdr;


/*
 * Optional Header: Subsystem.
 */

#define	IMAGE_SUBSYSTEM_UNKNOWN			0
#define	IMAGE_SUBSYSTEM_NATIVE			1
#define	IMAGE_SUBSYSTEM_WINDOWS_GUI		2
#define	IMAGE_SUBSYSTEM_WINDOWS_CUI		3
#define	IMAGE_SUBSYSTEM_POSIX_CUI		7
#define	IMAGE_SUBSYSTEM_WINDOWS_CE_GUI		9
#define	IMAGE_SUBSYSTEM_EFI_APPLICATION		10
#define	IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER	11
#define	IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER	12
#define	IMAGE_SUBSYSTEM_EFI_ROM			13
#define	IMAGE_SUBSYSTEM_XBOX			14

/*
 * Optional Header: DLL Characteristics
 */

#define	IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE		0x0040
#define	IMAGE_DLL_CHARACTERISTICS_FORCE_INTEGRITY	0x0080
#define	IMAGE_DLL_CHARACTERISTICS_NX_COMPAT		0x0100
#define	IMAGE_DLL_CHARACTERISTICS_NO_ISOLATION		0x0200
#define	IMAGE_DLL_CHARACTERISTICS_NO_SEH		0x0400
#define	IMAGE_DLL_CHARACTERISTICS_NO_BIND		0x0800
#define	IMAGE_DLL_CHARACTERISTICS_WDM_DRIVER		0x2000
#define	IMAGE_DLL_CHARACTERISTICS_TERMINAL_SERVER_AWARE	0x8000

/*
 * Optional Header.
 */

#define	PE_FORMAT_ROM		0x107
#define	PE_FORMAT_32		0x10b
#define	PE_FORMAT_32P		0x20b

typedef struct _PE_OptHdr {
	uint16_t oh_magic;
	uint8_t oh_ldvermajor;
	uint8_t oh_ldverminor;
	uint32_t oh_textsize;
	uint32_t oh_datasize;
	uint32_t oh_bsssize;
	uint32_t oh_entry;
	uint32_t oh_textbase;
	uint32_t oh_database;
	uint64_t oh_imgbase;
	uint32_t oh_secalign;
	uint32_t oh_filealign;
	uint16_t oh_osvermajor;
	uint16_t oh_osverminor;
	uint16_t oh_imgvermajor;
	uint16_t oh_imgverminor;
	uint16_t oh_subvermajor;
	uint16_t oh_subverminor;
	uint32_t oh_win32ver;
	uint32_t oh_imgsize;
	uint32_t oh_hdrsize;
	uint32_t oh_checksum;
	uint16_t oh_subsystem;
	uint16_t oh_dllchar;
	uint64_t oh_stacksizer;
	uint64_t oh_stacksizec;
	uint64_t oh_heapsizer;
	uint64_t oh_heapsizec;
	uint32_t oh_ldrflags;
	uint32_t oh_ndatadir;
} PE_OptHdr;

/*
 * Optional Header: Data Directories.
 */

#define	PE_DD_EXPORT		0
#define	PE_DD_IMPORT		1
#define	PE_DD_RESROUCE		2
#define	PE_DD_EXCEPTION		3
#define	PE_DD_CERTIFICATE	4
#define	PE_DD_BASERELOC		5
#define	PE_DD_DEBUG		6
#define	PE_DD_ARCH		7
#define	PE_DD_GLOBALPTR		8
#define	PE_DD_TLS		9
#define	PE_DD_LOADCONFIG	10
#define	PE_DD_BOUNDIMPORT	11
#define	PE_DD_IAT		12
#define	PE_DD_DELAYIMPORT	13
#define	PE_DD_CLRRUNTIME	14
#define	PE_DD_RESERVED		15
#define	PE_DD_MAX		16

typedef struct _PE_DataDirEntry {
	uint32_t de_addr;
	uint32_t de_size;
} PE_DataDirEntry;

typedef struct _PE_DataDir {
	PE_DataDirEntry dd_e[PE_DD_MAX];
	uint32_t dd_total;
} PE_DataDir;

/*
 * Section Headers: Section flags.
 */

#define	IMAGE_SCN_TYPE_NO_PAD			0x00000008
#define	IMAGE_SCN_CNT_CODE			0x00000020
#define	IMAGE_SCN_CNT_INITIALIZED_DATA		0x00000040
#define	IMAGE_SCN_CNT_UNINITIALIZED_DATA	0x00000080
#define	IMAGE_SCN_LNK_OTHER			0x00000100
#define	IMAGE_SCN_LNK_INFO			0x00000200
#define	IMAGE_SCN_LNK_REMOVE			0x00000800
#define	IMAGE_SCN_LNK_COMDAT			0x00001000
#define	IMAGE_SCN_GPREL				0x00008000
#define	IMAGE_SCN_MEM_PURGEABLE			0x00020000
#define	IMAGE_SCN_MEM_16BIT			0x00020000
#define	IMAGE_SCN_MEM_LOCKED			0x00040000
#define	IMAGE_SCN_MEM_PRELOAD			0x00080000
#define	IMAGE_SCN_ALIGN_1BYTES			0x00100000
#define	IMAGE_SCN_ALIGN_2BYTES			0x00200000
#define	IMAGE_SCN_ALIGN_4BYTES			0x00300000
#define	IMAGE_SCN_ALIGN_8BYTES			0x00400000
#define	IMAGE_SCN_ALIGN_16BYTES			0x00500000
#define	IMAGE_SCN_ALIGN_32BYTES			0x00600000
#define	IMAGE_SCN_ALIGN_64BYTES			0x00700000
#define	IMAGE_SCN_ALIGN_128BYTES		0x00800000
#define	IMAGE_SCN_ALIGN_256BYTES		0x00900000
#define	IMAGE_SCN_ALIGN_512BYTES		0x00A00000
#define	IMAGE_SCN_ALIGN_1024BYTES		0x00B00000
#define	IMAGE_SCN_ALIGN_2048BYTES		0x00C00000
#define	IMAGE_SCN_ALIGN_4096BYTES		0x00D00000
#define	IMAGE_SCN_ALIGN_8192BYTES		0x00E00000
#define	IMAGE_SCN_LNK_NRELOC_OVFL		0x01000000
#define	IMAGE_SCN_MEM_DISCARDABLE		0x02000000
#define	IMAGE_SCN_MEM_NOT_CACHED		0x04000000
#define	IMAGE_SCN_MEM_NOT_PAGED			0x08000000
#define	IMAGE_SCN_MEM_SHARED			0x10000000
#define	IMAGE_SCN_MEM_EXECUTE			0x20000000
#define	IMAGE_SCN_MEM_READ			0x40000000
#define	IMAGE_SCN_MEM_WRITE			0x80000000

/*
 * Section Headers.
 */

typedef struct _PE_SecHdr {
	char sh_name[8];
	uint32_t sh_virtsize;
	uint32_t sh_addr;
	uint32_t sh_rawsize;
	uint32_t sh_rawptr;
	uint32_t sh_relocptr;
	uint32_t sh_lineptr;
	uint16_t sh_nreloc;
	uint16_t sh_nline;
	uint32_t sh_char;
} PE_SecHdr;

#endif	/* !_PE_H_ */
