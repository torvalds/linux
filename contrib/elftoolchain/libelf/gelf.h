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
 * $Id: gelf.h 3174 2015-03-27 17:13:41Z emaste $
 */

#ifndef	_GELF_H_
#define	_GELF_H_

#include <libelf.h>

typedef Elf64_Addr	GElf_Addr;	/* Addresses */
typedef Elf64_Half	GElf_Half;	/* Half words (16 bit) */
typedef Elf64_Off	GElf_Off;	/* Offsets */
typedef Elf64_Sword	GElf_Sword;	/* Signed words (32 bit) */
typedef Elf64_Sxword	GElf_Sxword;	/* Signed long words (64 bit) */
typedef Elf64_Word	GElf_Word;	/* Unsigned words (32 bit) */
typedef Elf64_Xword	GElf_Xword;	/* Unsigned long words (64 bit) */

typedef Elf64_Dyn	GElf_Dyn;	/* ".dynamic" section entries */
typedef Elf64_Ehdr	GElf_Ehdr;	/* ELF header */
typedef Elf64_Phdr	GElf_Phdr;	/* Program header */
typedef Elf64_Shdr	GElf_Shdr;	/* Section header */
typedef Elf64_Sym	GElf_Sym;	/* Symbol table entries */
typedef Elf64_Rel	GElf_Rel;	/* Relocation entries */
typedef Elf64_Rela	GElf_Rela;	/* Relocation entries with addend */

typedef	Elf64_Cap	GElf_Cap;	/* SW/HW capabilities */
typedef Elf64_Move	GElf_Move;	/* Move entries */
typedef Elf64_Syminfo	GElf_Syminfo;	/* Symbol information */

#define	GELF_M_INFO			ELF64_M_INFO
#define	GELF_M_SIZE			ELF64_M_SIZE
#define	GELF_M_SYM			ELF64_M_SYM

#define	GELF_R_INFO			ELF64_R_INFO
#define	GELF_R_SYM			ELF64_R_SYM
#define	GELF_R_TYPE			ELF64_R_TYPE
#define	GELF_R_TYPE_DATA		ELF64_R_TYPE_DATA
#define	GELF_R_TYPE_ID			ELF64_R_TYPE_ID
#define	GELF_R_TYPE_INFO		ELF64_R_TYPE_INFO

#define	GELF_ST_BIND			ELF64_ST_BIND
#define	GELF_ST_INFO			ELF64_ST_INFO
#define	GELF_ST_TYPE			ELF64_ST_TYPE
#define	GELF_ST_VISIBILITY		ELF64_ST_VISIBILITY

#ifdef __cplusplus
extern "C" {
#endif
long		gelf_checksum(Elf *_elf);
size_t		gelf_fsize(Elf *_elf, Elf_Type _type, size_t _count,
			unsigned int _version);
int		gelf_getclass(Elf *_elf);
GElf_Dyn	*gelf_getdyn(Elf_Data *_data, int _index, GElf_Dyn *_dst);
GElf_Ehdr	*gelf_getehdr(Elf *_elf, GElf_Ehdr *_dst);
GElf_Phdr	*gelf_getphdr(Elf *_elf, int _index, GElf_Phdr *_dst);
GElf_Rel	*gelf_getrel(Elf_Data *_src, int _index, GElf_Rel *_dst);
GElf_Rela	*gelf_getrela(Elf_Data *_src, int _index, GElf_Rela *_dst);
GElf_Shdr	*gelf_getshdr(Elf_Scn *_scn, GElf_Shdr *_dst);
GElf_Sym	*gelf_getsym(Elf_Data *_src, int _index, GElf_Sym *_dst);
GElf_Sym	*gelf_getsymshndx(Elf_Data *_src, Elf_Data *_shindexsrc,
			int _index, GElf_Sym *_dst, Elf32_Word *_shindexdst);
void *		gelf_newehdr(Elf *_elf, int _class);
void *		gelf_newphdr(Elf *_elf, size_t _phnum);
int		gelf_update_dyn(Elf_Data *_dst, int _index, GElf_Dyn *_src);
int		gelf_update_ehdr(Elf *_elf, GElf_Ehdr *_src);
int		gelf_update_phdr(Elf *_elf, int _index, GElf_Phdr *_src);
int		gelf_update_rel(Elf_Data *_dst, int _index, GElf_Rel *_src);
int		gelf_update_rela(Elf_Data *_dst, int _index, GElf_Rela *_src);
int		gelf_update_shdr(Elf_Scn *_dst, GElf_Shdr *_src);
int		gelf_update_sym(Elf_Data *_dst, int _index, GElf_Sym *_src);
int		gelf_update_symshndx(Elf_Data *_symdst, Elf_Data *_shindexdst,
			int _index, GElf_Sym *_symsrc, Elf32_Word _shindexsrc);
Elf_Data 	*gelf_xlatetof(Elf *_elf, Elf_Data *_dst, const Elf_Data *_src, unsigned int _encode);
Elf_Data 	*gelf_xlatetom(Elf *_elf, Elf_Data *_dst, const Elf_Data *_src, unsigned int _encode);

GElf_Cap	*gelf_getcap(Elf_Data *_data, int _index, GElf_Cap *_cap);
GElf_Move	*gelf_getmove(Elf_Data *_src, int _index, GElf_Move *_dst);
GElf_Syminfo	*gelf_getsyminfo(Elf_Data *_src, int _index, GElf_Syminfo *_dst);
int		gelf_update_cap(Elf_Data *_dst, int _index, GElf_Cap *_src);
int		gelf_update_move(Elf_Data *_dst, int _index, GElf_Move *_src);
int		gelf_update_syminfo(Elf_Data *_dst, int _index, GElf_Syminfo *_src);
#ifdef __cplusplus
}
#endif

#endif	/* _GELF_H_ */
