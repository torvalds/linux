/* pe.h  -  PE COFF header information 

   Copyright 1999, 2000, 2001, 2003, 2004, 2006 Free Software Foundation, Inc.

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
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */
#ifndef _PE_H
#define _PE_H

/* NT specific file attributes.  */
#define IMAGE_FILE_RELOCS_STRIPPED           0x0001
#define IMAGE_FILE_EXECUTABLE_IMAGE          0x0002
#define IMAGE_FILE_LINE_NUMS_STRIPPED        0x0004
#define IMAGE_FILE_LOCAL_SYMS_STRIPPED       0x0008
#define IMAGE_FILE_AGGRESSIVE_WS_TRIM        0x0010
#define IMAGE_FILE_LARGE_ADDRESS_AWARE       0x0020
#define IMAGE_FILE_16BIT_MACHINE             0x0040
#define IMAGE_FILE_BYTES_REVERSED_LO         0x0080
#define IMAGE_FILE_32BIT_MACHINE             0x0100
#define IMAGE_FILE_DEBUG_STRIPPED            0x0200
#define IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP   0x0400
#define IMAGE_FILE_NET_RUN_FROM_SWAP         0x0800
#define IMAGE_FILE_SYSTEM                    0x1000
#define IMAGE_FILE_DLL                       0x2000
#define IMAGE_FILE_UP_SYSTEM_ONLY            0x4000
#define IMAGE_FILE_BYTES_REVERSED_HI         0x8000

/* Additional flags to be set for section headers to allow the NT loader to
   read and write to the section data (to replace the addresses of data in
   dlls for one thing); also to execute the section in .text's case.  */
#define IMAGE_SCN_MEM_DISCARDABLE 0x02000000
#define IMAGE_SCN_MEM_EXECUTE     0x20000000
#define IMAGE_SCN_MEM_READ        0x40000000
#define IMAGE_SCN_MEM_WRITE       0x80000000

/* Section characteristics added for ppc-nt.  */

#define IMAGE_SCN_TYPE_NO_PAD                0x00000008  /* Reserved. */

#define IMAGE_SCN_CNT_CODE                   0x00000020  /* Section contains code. */
#define IMAGE_SCN_CNT_INITIALIZED_DATA       0x00000040  /* Section contains initialized data. */
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA     0x00000080  /* Section contains uninitialized data. */

#define IMAGE_SCN_LNK_OTHER                  0x00000100  /* Reserved. */
#define IMAGE_SCN_LNK_INFO                   0x00000200  /* Section contains comments or some other type of information. */
#define IMAGE_SCN_LNK_REMOVE                 0x00000800  /* Section contents will not become part of image. */
#define IMAGE_SCN_LNK_COMDAT                 0x00001000  /* Section contents comdat. */

#define IMAGE_SCN_MEM_FARDATA                0x00008000

#define IMAGE_SCN_MEM_PURGEABLE              0x00020000
#define IMAGE_SCN_MEM_16BIT                  0x00020000
#define IMAGE_SCN_MEM_LOCKED                 0x00040000
#define IMAGE_SCN_MEM_PRELOAD                0x00080000

#define IMAGE_SCN_ALIGN_1BYTES               0x00100000
#define IMAGE_SCN_ALIGN_2BYTES               0x00200000
#define IMAGE_SCN_ALIGN_4BYTES               0x00300000
#define IMAGE_SCN_ALIGN_8BYTES               0x00400000
#define IMAGE_SCN_ALIGN_16BYTES              0x00500000  /* Default alignment if no others are specified. */
#define IMAGE_SCN_ALIGN_32BYTES              0x00600000
#define IMAGE_SCN_ALIGN_64BYTES              0x00700000

#define IMAGE_SCN_LNK_NRELOC_OVFL            0x01000000  /* Section contains extended relocations. */
#define IMAGE_SCN_MEM_NOT_CACHED             0x04000000  /* Section is not cachable.               */
#define IMAGE_SCN_MEM_NOT_PAGED              0x08000000  /* Section is not pageable.               */
#define IMAGE_SCN_MEM_SHARED                 0x10000000  /* Section is shareable.                  */

/* COMDAT selection codes.  */

#define IMAGE_COMDAT_SELECT_NODUPLICATES     (1) /* Warn if duplicates.  */
#define IMAGE_COMDAT_SELECT_ANY		     (2) /* No warning.  */
#define IMAGE_COMDAT_SELECT_SAME_SIZE	     (3) /* Warn if different size.  */
#define IMAGE_COMDAT_SELECT_EXACT_MATCH	     (4) /* Warn if different.  */
#define IMAGE_COMDAT_SELECT_ASSOCIATIVE	     (5) /* Base on other section.  */

/* Machine numbers.  */

#define IMAGE_FILE_MACHINE_UNKNOWN           0x0000
#define IMAGE_FILE_MACHINE_ALPHA             0x0184
#define IMAGE_FILE_MACHINE_ALPHA64           0x0284
#define IMAGE_FILE_MACHINE_AM33              0x01d3
#define IMAGE_FILE_MACHINE_AMD64             0x8664
#define IMAGE_FILE_MACHINE_ARM               0x01c0
#define IMAGE_FILE_MACHINE_AXP64             IMAGE_FILE_MACHINE_ALPHA64
#define IMAGE_FILE_MACHINE_CEE               0xc0ee
#define IMAGE_FILE_MACHINE_CEF               0x0cef
#define IMAGE_FILE_MACHINE_EBC               0x0ebc
#define IMAGE_FILE_MACHINE_I386              0x014c
#define IMAGE_FILE_MACHINE_IA64              0x0200
#define IMAGE_FILE_MACHINE_M32R              0x9041
#define IMAGE_FILE_MACHINE_M68K              0x0268
#define IMAGE_FILE_MACHINE_MIPS16            0x0266
#define IMAGE_FILE_MACHINE_MIPSFPU           0x0366
#define IMAGE_FILE_MACHINE_MIPSFPU16         0x0466
#define IMAGE_FILE_MACHINE_POWERPC           0x01f0
#define IMAGE_FILE_MACHINE_POWERPCFP         0x01f1
#define IMAGE_FILE_MACHINE_R10000            0x0168
#define IMAGE_FILE_MACHINE_R3000             0x0162
#define IMAGE_FILE_MACHINE_R4000             0x0166
#define IMAGE_FILE_MACHINE_SH3               0x01a2
#define IMAGE_FILE_MACHINE_SH3DSP            0x01a3
#define IMAGE_FILE_MACHINE_SH3E              0x01a4
#define IMAGE_FILE_MACHINE_SH4               0x01a6
#define IMAGE_FILE_MACHINE_SH5               0x01a8
#define IMAGE_FILE_MACHINE_THUMB             0x01c2
#define IMAGE_FILE_MACHINE_TRICORE           0x0520
#define IMAGE_FILE_MACHINE_WCEMIPSV2         0x0169
#define IMAGE_FILE_MACHINE_AMD64             0x8664

#define IMAGE_SUBSYSTEM_UNKNOWN			 0
#define IMAGE_SUBSYSTEM_NATIVE			 1
#define IMAGE_SUBSYSTEM_WINDOWS_GUI		 2
#define IMAGE_SUBSYSTEM_WINDOWS_CUI		 3
#define IMAGE_SUBSYSTEM_POSIX_CUI		 7
#define IMAGE_SUBSYSTEM_WINDOWS_CE_GUI		 9
#define IMAGE_SUBSYSTEM_EFI_APPLICATION		10
#define IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER	11
#define IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER	12
#define IMAGE_SUBSYSTEM_EFI_ROM			13
#define IMAGE_SUBSYSTEM_XBOX			14
  
/* Magic values that are true for all dos/nt implementations.  */
#define DOSMAGIC       0x5a4d  
#define NT_SIGNATURE   0x00004550

/* NT allows long filenames, we want to accommodate this.
   This may break some of the bfd functions.  */
#undef  FILNMLEN
#define FILNMLEN	18	/* # characters in a file name.  */

struct external_PEI_DOS_hdr
{
  /* DOS header fields - always at offset zero in the EXE file.  */
  char e_magic[2];		/* Magic number, 0x5a4d.  */
  char e_cblp[2];		/* Bytes on last page of file, 0x90.  */
  char e_cp[2];			/* Pages in file, 0x3.  */
  char e_crlc[2];		/* Relocations, 0x0.  */
  char e_cparhdr[2];		/* Size of header in paragraphs, 0x4.  */
  char e_minalloc[2];		/* Minimum extra paragraphs needed, 0x0.  */
  char e_maxalloc[2];		/* Maximum extra paragraphs needed, 0xFFFF.  */
  char e_ss[2];			/* Initial (relative) SS value, 0x0.  */
  char e_sp[2];			/* Initial SP value, 0xb8.  */
  char e_csum[2];		/* Checksum, 0x0.  */
  char e_ip[2];			/* Initial IP value, 0x0.  */
  char e_cs[2];			/* Initial (relative) CS value, 0x0.  */
  char e_lfarlc[2];		/* File address of relocation table, 0x40.  */
  char e_ovno[2];		/* Overlay number, 0x0.  */
  char e_res[4][2];		/* Reserved words, all 0x0.  */
  char e_oemid[2];		/* OEM identifier (for e_oeminfo), 0x0.  */
  char e_oeminfo[2];		/* OEM information; e_oemid specific, 0x0.  */
  char e_res2[10][2];		/* Reserved words, all 0x0.  */
  char e_lfanew[4];		/* File address of new exe header, usually 0x80.  */
  char dos_message[16][4];	/* Other stuff, always follow DOS header.  */
};

struct external_PEI_IMAGE_hdr
{
  char nt_signature[4];		/* required NT signature, 0x4550.  */

  /* From standard header.  */
  char f_magic[2];		/* Magic number.		*/
  char f_nscns[2];		/* Number of sections.		*/
  char f_timdat[4];		/* Time & date stamp.		*/
  char f_symptr[4];		/* File pointer to symtab.	*/
  char f_nsyms[4];		/* Number of symtab entries.	*/
  char f_opthdr[2];		/* Sizeof(optional hdr).	*/
  char f_flags[2];		/* Flags.			*/
};

struct external_PEI_filehdr
{
  /* DOS header fields - always at offset zero in the EXE file.  */
  char e_magic[2];		/* Magic number, 0x5a4d.  */
  char e_cblp[2];		/* Bytes on last page of file, 0x90.  */
  char e_cp[2];			/* Pages in file, 0x3.  */
  char e_crlc[2];		/* Relocations, 0x0.  */
  char e_cparhdr[2];		/* Size of header in paragraphs, 0x4.  */
  char e_minalloc[2];		/* Minimum extra paragraphs needed, 0x0.  */
  char e_maxalloc[2];		/* Maximum extra paragraphs needed, 0xFFFF.  */
  char e_ss[2];			/* Initial (relative) SS value, 0x0.  */
  char e_sp[2];			/* Initial SP value, 0xb8.  */
  char e_csum[2];		/* Checksum, 0x0.  */
  char e_ip[2];			/* Initial IP value, 0x0.  */
  char e_cs[2];			/* Initial (relative) CS value, 0x0.  */
  char e_lfarlc[2];		/* File address of relocation table, 0x40.  */
  char e_ovno[2];		/* Overlay number, 0x0.  */
  char e_res[4][2];		/* Reserved words, all 0x0.  */
  char e_oemid[2];		/* OEM identifier (for e_oeminfo), 0x0.  */
  char e_oeminfo[2];		/* OEM information; e_oemid specific, 0x0.  */
  char e_res2[10][2];		/* Reserved words, all 0x0.  */
  char e_lfanew[4];		/* File address of new exe header, usually 0x80.  */
  char dos_message[16][4];	/* Other stuff, always follow DOS header.  */

  /* Note: additional bytes may be inserted before the signature.  Use
   the e_lfanew field to find the actual location of the NT signature.  */

  char nt_signature[4];		/* required NT signature, 0x4550.  */

  /* From standard header.  */
  char f_magic[2];		/* Magic number.		*/
  char f_nscns[2];		/* Number of sections.		*/
  char f_timdat[4];		/* Time & date stamp.		*/
  char f_symptr[4];		/* File pointer to symtab.	*/
  char f_nsyms[4];		/* Number of symtab entries.	*/
  char f_opthdr[2];		/* Sizeof(optional hdr).	*/
  char f_flags[2];		/* Flags.			*/
};

#ifdef COFF_IMAGE_WITH_PE

/* The filehdr is only weird in images.  */

#undef  FILHDR
#define FILHDR struct external_PEI_filehdr
#undef  FILHSZ
#define FILHSZ 152

#endif /* COFF_IMAGE_WITH_PE */

/* 32-bit PE a.out header: */

typedef struct 
{
  AOUTHDR standard;

  /* NT extra fields; see internal.h for descriptions.  */
  char  ImageBase[4];
  char  SectionAlignment[4];
  char  FileAlignment[4];
  char  MajorOperatingSystemVersion[2];
  char  MinorOperatingSystemVersion[2];
  char  MajorImageVersion[2];
  char  MinorImageVersion[2];
  char  MajorSubsystemVersion[2];
  char  MinorSubsystemVersion[2];
  char  Reserved1[4];
  char  SizeOfImage[4];
  char  SizeOfHeaders[4];
  char  CheckSum[4];
  char  Subsystem[2];
  char  DllCharacteristics[2];
  char  SizeOfStackReserve[4];
  char  SizeOfStackCommit[4];
  char  SizeOfHeapReserve[4];
  char  SizeOfHeapCommit[4];
  char  LoaderFlags[4];
  char  NumberOfRvaAndSizes[4];
  /* IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];  */
  char  DataDirectory[16][2][4]; /* 16 entries, 2 elements/entry, 4 chars.  */
} PEAOUTHDR;

#undef AOUTSZ
#define AOUTSZ (AOUTHDRSZ + 196)

/* Like PEAOUTHDR, except that the "standard" member has no BaseOfData
   (aka data_start) member and that some of the members are 8 instead
   of just 4 bytes long.  */
typedef struct 
{
#ifdef AOUTHDRSZ64
  AOUTHDR64 standard;
#else
  AOUTHDR standard;
#endif
  /* NT extra fields; see internal.h for descriptions.  */
  char  ImageBase[8];
  char  SectionAlignment[4];
  char  FileAlignment[4];
  char  MajorOperatingSystemVersion[2];
  char  MinorOperatingSystemVersion[2];
  char  MajorImageVersion[2];
  char  MinorImageVersion[2];
  char  MajorSubsystemVersion[2];
  char  MinorSubsystemVersion[2];
  char  Reserved1[4];
  char  SizeOfImage[4];
  char  SizeOfHeaders[4];
  char  CheckSum[4];
  char  Subsystem[2];
  char  DllCharacteristics[2];
  char  SizeOfStackReserve[8];
  char  SizeOfStackCommit[8];
  char  SizeOfHeapReserve[8];
  char  SizeOfHeapCommit[8];
  char  LoaderFlags[4];
  char  NumberOfRvaAndSizes[4];
  /* IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];  */
  char  DataDirectory[16][2][4]; /* 16 entries, 2 elements/entry, 4 chars.  */
} PEPAOUTHDR;

#ifdef AOUTHDRSZ64
#define PEPAOUTSZ	(AOUTHDRSZ64 + 196 + 5 * 4) /* = 240 */
#else
#define PEPAOUTSZ	240
#endif
  
#undef  E_FILNMLEN
#define E_FILNMLEN	18	/* # characters in a file name.  */

/* Import Tyoes fot ILF format object files..  */
#define IMPORT_CODE	0
#define IMPORT_DATA	1
#define IMPORT_CONST	2

/* Import Name Tyoes for ILF format object files.  */
#define IMPORT_ORDINAL		0
#define IMPORT_NAME		1
#define IMPORT_NAME_NOPREFIX	2
#define IMPORT_NAME_UNDECORATE	3

/* Weak external characteristics.  */
#define IMAGE_WEAK_EXTERN_SEARCH_NOLIBRARY	1
#define IMAGE_WEAK_EXTERN_SEARCH_LIBRARY	2
#define IMAGE_WEAK_EXTERN_SEARCH_ALIAS		3

#endif /* _PE_H */
