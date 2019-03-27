/*-
 * Copyright (c) 2018 John Baldwin
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
 */

#include <gelf.h>

#include "_libelf.h"

ELFTC_VCSID("$Id$");

int
_libelf_is_mips64el(Elf *e)
{

	return (e->e_kind == ELF_K_ELF && e->e_class == ELFCLASS64 &&
	    e->e_u.e_elf.e_ehdr.e_ehdr64->e_machine == EM_MIPS &&
	    e->e_u.e_elf.e_ehdr.e_ehdr64->e_ident[EI_DATA] == ELFDATA2LSB);
}

/*
 * For MIPS64, the r_info field is actually stored as a 32-bit symbol
 * index (r_sym) followed by four single-byte fields (r_ssym, r_type3,
 * r_type2, and r_type).  The byte-swap for the little-endian case
 * jumbles this incorrectly so compensate.
 */
Elf64_Xword
_libelf_mips64el_r_info_tof(Elf64_Xword r_info)
{
	Elf64_Xword new_info;
	uint8_t ssym, type3, type2, type;

	ssym = r_info >> 24;
	type3 = r_info >> 16;
	type2 = r_info >> 8;
	type = r_info;
	new_info = r_info >> 32;
	new_info |= (Elf64_Xword)ssym << 32;
	new_info |= (Elf64_Xword)type3 << 40;
	new_info |= (Elf64_Xword)type2 << 48;
	new_info |= (Elf64_Xword)type << 56;
	return (new_info);
}

Elf64_Xword
_libelf_mips64el_r_info_tom(Elf64_Xword r_info)
{
	Elf64_Xword new_info;
	uint8_t ssym, type3, type2, type;

	ssym = r_info >> 32;
	type3 = r_info >> 40;
	type2 = r_info >> 48;
	type = r_info >> 56;
	new_info = (r_info & 0xffffffff) << 32;
	new_info |= (Elf64_Xword)ssym << 24;
	new_info |= (Elf64_Xword)type3 << 16;
	new_info |= (Elf64_Xword)type2 << 8;
	new_info |= (Elf64_Xword)type;
	return (new_info);
}
