/* ELF support for BFD.
   Copyright 1991, 1992, 1993, 1995, 1997, 1998, 1999, 2001, 2003, 2005
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
   that describe how ELF is represented externally by the BFD library.
   I.E. it describes the in-file representation of ELF.  It requires
   the elf/common.h file which contains the portions that are common to
   both the internal and external representations. */

/* The 64-bit stuff is kind of random.  Perhaps someone will publish a
   spec someday.  */

#ifndef _ELF_EXTERNAL_H
#define _ELF_EXTERNAL_H

/* ELF Header (32-bit implementations) */

typedef struct {
  unsigned char	e_ident[16];		/* ELF "magic number" */
  unsigned char	e_type[2];		/* Identifies object file type */
  unsigned char	e_machine[2];		/* Specifies required architecture */
  unsigned char	e_version[4];		/* Identifies object file version */
  unsigned char	e_entry[4];		/* Entry point virtual address */
  unsigned char	e_phoff[4];		/* Program header table file offset */
  unsigned char	e_shoff[4];		/* Section header table file offset */
  unsigned char	e_flags[4];		/* Processor-specific flags */
  unsigned char	e_ehsize[2];		/* ELF header size in bytes */
  unsigned char	e_phentsize[2];		/* Program header table entry size */
  unsigned char	e_phnum[2];		/* Program header table entry count */
  unsigned char	e_shentsize[2];		/* Section header table entry size */
  unsigned char	e_shnum[2];		/* Section header table entry count */
  unsigned char	e_shstrndx[2];		/* Section header string table index */
} Elf32_External_Ehdr;

typedef struct {
  unsigned char	e_ident[16];		/* ELF "magic number" */
  unsigned char	e_type[2];		/* Identifies object file type */
  unsigned char	e_machine[2];		/* Specifies required architecture */
  unsigned char	e_version[4];		/* Identifies object file version */
  unsigned char	e_entry[8];		/* Entry point virtual address */
  unsigned char	e_phoff[8];		/* Program header table file offset */
  unsigned char	e_shoff[8];		/* Section header table file offset */
  unsigned char	e_flags[4];		/* Processor-specific flags */
  unsigned char	e_ehsize[2];		/* ELF header size in bytes */
  unsigned char	e_phentsize[2];		/* Program header table entry size */
  unsigned char	e_phnum[2];		/* Program header table entry count */
  unsigned char	e_shentsize[2];		/* Section header table entry size */
  unsigned char	e_shnum[2];		/* Section header table entry count */
  unsigned char	e_shstrndx[2];		/* Section header string table index */
} Elf64_External_Ehdr;

/* Program header */

typedef struct {
  unsigned char	p_type[4];		/* Identifies program segment type */
  unsigned char	p_offset[4];		/* Segment file offset */
  unsigned char	p_vaddr[4];		/* Segment virtual address */
  unsigned char	p_paddr[4];		/* Segment physical address */
  unsigned char	p_filesz[4];		/* Segment size in file */
  unsigned char	p_memsz[4];		/* Segment size in memory */
  unsigned char	p_flags[4];		/* Segment flags */
  unsigned char	p_align[4];		/* Segment alignment, file & memory */
} Elf32_External_Phdr;

typedef struct {
  unsigned char	p_type[4];		/* Identifies program segment type */
  unsigned char	p_flags[4];		/* Segment flags */
  unsigned char	p_offset[8];		/* Segment file offset */
  unsigned char	p_vaddr[8];		/* Segment virtual address */
  unsigned char	p_paddr[8];		/* Segment physical address */
  unsigned char	p_filesz[8];		/* Segment size in file */
  unsigned char	p_memsz[8];		/* Segment size in memory */
  unsigned char	p_align[8];		/* Segment alignment, file & memory */
} Elf64_External_Phdr;

/* Section header */

typedef struct {
  unsigned char	sh_name[4];		/* Section name, index in string tbl */
  unsigned char	sh_type[4];		/* Type of section */
  unsigned char	sh_flags[4];		/* Miscellaneous section attributes */
  unsigned char	sh_addr[4];		/* Section virtual addr at execution */
  unsigned char	sh_offset[4];		/* Section file offset */
  unsigned char	sh_size[4];		/* Size of section in bytes */
  unsigned char	sh_link[4];		/* Index of another section */
  unsigned char	sh_info[4];		/* Additional section information */
  unsigned char	sh_addralign[4];	/* Section alignment */
  unsigned char	sh_entsize[4];		/* Entry size if section holds table */
} Elf32_External_Shdr;

typedef struct {
  unsigned char	sh_name[4];		/* Section name, index in string tbl */
  unsigned char	sh_type[4];		/* Type of section */
  unsigned char	sh_flags[8];		/* Miscellaneous section attributes */
  unsigned char	sh_addr[8];		/* Section virtual addr at execution */
  unsigned char	sh_offset[8];		/* Section file offset */
  unsigned char	sh_size[8];		/* Size of section in bytes */
  unsigned char	sh_link[4];		/* Index of another section */
  unsigned char	sh_info[4];		/* Additional section information */
  unsigned char	sh_addralign[8];	/* Section alignment */
  unsigned char	sh_entsize[8];		/* Entry size if section holds table */
} Elf64_External_Shdr;

/* Symbol table entry */

typedef struct {
  unsigned char	st_name[4];		/* Symbol name, index in string tbl */
  unsigned char	st_value[4];		/* Value of the symbol */
  unsigned char	st_size[4];		/* Associated symbol size */
  unsigned char	st_info[1];		/* Type and binding attributes */
  unsigned char	st_other[1];		/* No defined meaning, 0 */
  unsigned char	st_shndx[2];		/* Associated section index */
} Elf32_External_Sym;

typedef struct {
  unsigned char	st_name[4];		/* Symbol name, index in string tbl */
  unsigned char	st_info[1];		/* Type and binding attributes */
  unsigned char	st_other[1];		/* No defined meaning, 0 */
  unsigned char	st_shndx[2];		/* Associated section index */
  unsigned char	st_value[8];		/* Value of the symbol */
  unsigned char	st_size[8];		/* Associated symbol size */
} Elf64_External_Sym;

typedef struct {
  unsigned char est_shndx[4];		/* Section index */
} Elf_External_Sym_Shndx;

/* Note segments */

typedef struct {
  unsigned char	namesz[4];		/* Size of entry's owner string */
  unsigned char	descsz[4];		/* Size of the note descriptor */
  unsigned char	type[4];		/* Interpretation of the descriptor */
  char		name[1];		/* Start of the name+desc data */
} Elf_External_Note;

/* Relocation Entries */
typedef struct {
  unsigned char r_offset[4];	/* Location at which to apply the action */
  unsigned char	r_info[4];	/* index and type of relocation */
} Elf32_External_Rel;

typedef struct {
  unsigned char r_offset[4];	/* Location at which to apply the action */
  unsigned char	r_info[4];	/* index and type of relocation */
  unsigned char	r_addend[4];	/* Constant addend used to compute value */
} Elf32_External_Rela;

typedef struct {
  unsigned char r_offset[8];	/* Location at which to apply the action */
  unsigned char	r_info[8];	/* index and type of relocation */
} Elf64_External_Rel;

typedef struct {
  unsigned char r_offset[8];	/* Location at which to apply the action */
  unsigned char	r_info[8];	/* index and type of relocation */
  unsigned char	r_addend[8];	/* Constant addend used to compute value */
} Elf64_External_Rela;

/* dynamic section structure */

typedef struct {
  unsigned char	d_tag[4];		/* entry tag value */
  union {
    unsigned char	d_val[4];
    unsigned char	d_ptr[4];
  } d_un;
} Elf32_External_Dyn;

typedef struct {
  unsigned char	d_tag[8];		/* entry tag value */
  union {
    unsigned char	d_val[8];
    unsigned char	d_ptr[8];
  } d_un;
} Elf64_External_Dyn;

/* The version structures are currently size independent.  They are
   named without a 32 or 64.  If that ever changes, these structures
   will need to be renamed.  */

/* This structure appears in a SHT_GNU_verdef section.  */

typedef struct {
  unsigned char		vd_version[2];
  unsigned char		vd_flags[2];
  unsigned char		vd_ndx[2];
  unsigned char		vd_cnt[2];
  unsigned char		vd_hash[4];
  unsigned char		vd_aux[4];
  unsigned char		vd_next[4];
} Elf_External_Verdef;

/* This structure appears in a SHT_GNU_verdef section.  */

typedef struct {
  unsigned char		vda_name[4];
  unsigned char		vda_next[4];
} Elf_External_Verdaux;

/* This structure appears in a SHT_GNU_verneed section.  */

typedef struct {
  unsigned char		vn_version[2];
  unsigned char		vn_cnt[2];
  unsigned char		vn_file[4];
  unsigned char		vn_aux[4];
  unsigned char		vn_next[4];
} Elf_External_Verneed;

/* This structure appears in a SHT_GNU_verneed section.  */

typedef struct {
  unsigned char		vna_hash[4];
  unsigned char		vna_flags[2];
  unsigned char		vna_other[2];
  unsigned char		vna_name[4];
  unsigned char		vna_next[4];
} Elf_External_Vernaux;

/* This structure appears in a SHT_GNU_versym section.  This is not a
   standard ELF structure; ELF just uses Elf32_Half.  */

typedef struct {
  unsigned char		vs_vers[2];
} ATTRIBUTE_PACKED  Elf_External_Versym;

/* Structure for syminfo section.  */
typedef struct
{
  unsigned char		si_boundto[2];
  unsigned char		si_flags[2];
} Elf_External_Syminfo;


/* This structure appears on the stack and in NT_AUXV core file notes.  */
typedef struct
{
  unsigned char		a_type[4];
  unsigned char		a_val[4];
} Elf32_External_Auxv;

typedef struct
{
  unsigned char		a_type[8];
  unsigned char		a_val[8];
} Elf64_External_Auxv;

/* Size of SHT_GROUP section entry.  */

#define GRP_ENTRY_SIZE		4

#endif /* _ELF_EXTERNAL_H */
