/*-
 * Copyright (c) 2006,2008 Joseph Koshy
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
 * $Id: elf_types.m4 321 2009-03-07 16:59:14Z jkoshy $
 */

/*
 * ELF types, defined in the "enum Elf_Type" API.
 *
 * The members of the list form a 2-tuple: (name, C-type-suffix).
 * + `name' is an Elf_Type symbol without the `ELF_T_' prefix.
 * + `C-type-suffix' is the suffix for Elf32_ and Elf64_ type names.
 */

define(`ELF_TYPE_LIST',
	``ADDR,		Addr',
	`BYTE,		Byte',
	`CAP,		Cap',
	`DYN,		Dyn',
	`EHDR,		Ehdr',
	`GNUHASH,	-',
	`HALF,		Half',
	`LWORD,		Lword',
	`MOVE,		Move',
	`MOVEP,		MoveP',
	`NOTE,		Note',
	`OFF,		Off',
	`PHDR,		Phdr',
	`REL,		Rel',
	`RELA,		Rela',
	`SHDR,		Shdr',
	`SWORD,		Sword',
	`SXWORD,	Sxword',
	`SYMINFO,	Syminfo',
	`SYM,		Sym',
	`VDEF,		Verdef',
	`VNEED,		Verneed',
	`WORD,		Word',
	`XWORD,		Xword',
	`NUM,		_'')

/*
 * DEFINE_STRUCT(NAME,MEMBERLIST...)
 *
 * Map a type name to its members.
 *
 * Each member-list element comprises of pairs of (field name, type),
 * in the sequence used in the file representation of `NAME'.
 *
 * Each member list element comprises a pair containing a field name
 * and a basic type.  Basic types include IDENT, HALF, WORD, LWORD,
 * ADDR{32,64}, OFF{32,64}, SWORD, XWORD, SXWORD.
 *
 * The last element of a member list is the null element: `_,_'.
 */

define(`DEFINE_STRUCT',`define(`$1_DEF',shift($@))dnl')

DEFINE_STRUCT(`Elf32_Cap',
	``c_tag,	WORD',
	`c_un.c_val,	WORD',
	`_,_'')

DEFINE_STRUCT(`Elf64_Cap',
	``c_tag,	XWORD',
	`c_un.c_val,	XWORD',
	`_,_'')

DEFINE_STRUCT(`Elf32_Dyn',
	``d_tag,	SWORD',
	`d_un.d_ptr,	WORD',
	`_,_'')

DEFINE_STRUCT(`Elf64_Dyn',
	``d_tag,	SXWORD',
	`d_un.d_ptr,	XWORD',
	`_,_'')

DEFINE_STRUCT(`Elf32_Ehdr',
	``e_ident,	IDENT',
	`e_type, 	HALF',
	`e_machine,	HALF',
	`e_version,	WORD',
	`e_entry,	ADDR',
	`e_phoff,	OFF',
	`e_shoff,	OFF',
	`e_flags,	WORD',
	`e_ehsize,	HALF',
	`e_phentsize,	HALF',
	`e_phnum,	HALF',
	`e_shentsize,	HALF',
	`e_shnum,	HALF',
	`e_shstrndx,	HALF',
	`_,_'')

DEFINE_STRUCT(`Elf64_Ehdr',
	``e_ident,	IDENT',
	`e_type, 	HALF',
	`e_machine,	HALF',
	`e_version,	WORD',
	`e_entry,	ADDR',
	`e_phoff,	OFF',
	`e_shoff,	OFF',
	`e_flags,	WORD',
	`e_ehsize,	HALF',
	`e_phentsize,	HALF',
	`e_phnum,	HALF',
	`e_shentsize,	HALF',
	`e_shnum,	HALF',
	`e_shstrndx,	HALF',
	`_,_'')

DEFINE_STRUCT(`Elf32_Move',
	``m_value,	LWORD',
	`m_info,	WORD',
	`m_poffset,	WORD',
	`m_repeat,	HALF',
	`m_stride,	HALF',
	`_,_'')

DEFINE_STRUCT(`Elf64_Move',
	``m_value,	LWORD',
	`m_info,	XWORD',
	`m_poffset,	XWORD',
	`m_repeat,	HALF',
	`m_stride,	HALF',
	`_,_'')

DEFINE_STRUCT(`Elf32_Phdr',
	``p_type,	WORD',
	`p_offset,	OFF',
	`p_vaddr,	ADDR',
	`p_paddr,	ADDR',
	`p_filesz,	WORD',
	`p_memsz,	WORD',
	`p_flags,	WORD',
	`p_align,	WORD',
	`_,_'')

DEFINE_STRUCT(`Elf64_Phdr',
	``p_type,	WORD',
	`p_flags,	WORD',
	`p_offset,	OFF',
	`p_vaddr,	ADDR',
	`p_paddr,	ADDR',
	`p_filesz,	XWORD',
	`p_memsz,	XWORD',
	`p_align,	XWORD',
	`_,_'')

DEFINE_STRUCT(`Elf32_Rel',
	``r_offset,	ADDR',
	`r_info,	WORD',
	`_,_'')

DEFINE_STRUCT(`Elf64_Rel',
	``r_offset,	ADDR',
	`r_info,	XWORD',
	`_,_'')

DEFINE_STRUCT(`Elf32_Rela',
	``r_offset,	ADDR',
	`r_info,	WORD',
	`r_addend,	SWORD',
	`_,_'')

DEFINE_STRUCT(`Elf64_Rela',
	``r_offset,	ADDR',
	`r_info,	XWORD',
	`r_addend,	SXWORD',
	`_,_'')

DEFINE_STRUCT(`Elf32_Shdr',
	``sh_name,	WORD',
	`sh_type,	WORD',
	`sh_flags,	WORD',
	`sh_addr,	ADDR',
	`sh_offset,	OFF',
	`sh_size,	WORD',
	`sh_link,	WORD',
	`sh_info,	WORD',
	`sh_addralign,	WORD',
	`sh_entsize,	WORD',
	`_,_'')

DEFINE_STRUCT(`Elf64_Shdr',
	``sh_name,	WORD',
	`sh_type,	WORD',
	`sh_flags,	XWORD',
	`sh_addr,	ADDR',
	`sh_offset,	OFF',
	`sh_size,	XWORD',
	`sh_link,	WORD',
	`sh_info,	WORD',
	`sh_addralign,	XWORD',
	`sh_entsize,	XWORD',
	`_,_'')

DEFINE_STRUCT(`Elf32_Sym',
	``st_name,	WORD',
	`st_value,	ADDR',
	`st_size,	WORD',
	`st_info,	BYTE',
	`st_other,	BYTE',
	`st_shndx,	HALF',
	`_,_'')

DEFINE_STRUCT(`Elf64_Sym',
	``st_name,	WORD',
	`st_info,	BYTE',
	`st_other,	BYTE',
	`st_shndx,	HALF',
	`st_value,	ADDR',
	`st_size,	XWORD',
	`_,_'')

DEFINE_STRUCT(`Elf32_Syminfo',
	``si_boundto,	HALF',
	`si_flags,	HALF',
	`_,_'')

DEFINE_STRUCT(`Elf64_Syminfo',
	``si_boundto,	HALF',
	`si_flags,	HALF',
	`_,_'')

DEFINE_STRUCT(`Elf32_Verdaux',
	``vda_name,	WORD',
	`vda_next,	WORD',
	`_,_'')

DEFINE_STRUCT(`Elf64_Verdaux',
	``vda_name,	WORD',
	`vda_next,	WORD',
	`_,_'')

DEFINE_STRUCT(`Elf32_Verdef',
	``vd_version,	HALF',
	`vd_flags,	HALF',
	`vd_ndx,	HALF',
	`vd_cnt,	HALF',
	`vd_hash,	WORD',
	`vd_aux,	WORD',
	`vd_next,	WORD',
	`_,_'')

DEFINE_STRUCT(`Elf64_Verdef',
	``vd_version,	HALF',
	`vd_flags,	HALF',
	`vd_ndx,	HALF',
	`vd_cnt,	HALF',
	`vd_hash,	WORD',
	`vd_aux,	WORD',
	`vd_next,	WORD',
	`_,_'')

DEFINE_STRUCT(`Elf32_Verneed',
	``vn_version,	HALF',
	`vn_cnt,	HALF',
	`vn_file,	WORD',
	`vn_aux,	WORD',
	`vn_next,	WORD',
	`_,_'')

DEFINE_STRUCT(`Elf64_Verneed',
	``vn_version,	HALF',
	`vn_cnt,	HALF',
	`vn_file,	WORD',
	`vn_aux,	WORD',
	`vn_next,	WORD',
	`_,_'')

DEFINE_STRUCT(`Elf32_Vernaux',
	``vna_hash,	WORD',
	`vna_flags,	HALF',
	`vna_other,	HALF',
	`vna_name,	WORD',
	`vna_next,	WORD',
	`_,_'')

DEFINE_STRUCT(`Elf64_Vernaux',
	``vna_hash,	WORD',
	`vna_flags,	HALF',
	`vna_other,	HALF',
	`vna_name,	WORD',
	`vna_next,	WORD',
	`_,_'')
