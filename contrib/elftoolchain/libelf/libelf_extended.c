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
 */

#include <assert.h>
#include <libelf.h>

#include "_libelf.h"

ELFTC_VCSID("$Id: libelf_extended.c 3174 2015-03-27 17:13:41Z emaste $");

/*
 * Retrieve section #0, allocating a new section if needed.
 */
static Elf_Scn *
_libelf_getscn0(Elf *e)
{
	Elf_Scn *s;

	if ((s = STAILQ_FIRST(&e->e_u.e_elf.e_scn)) != NULL)
		return (s);

	return (_libelf_allocate_scn(e, (size_t) SHN_UNDEF));
}

int
_libelf_setshnum(Elf *e, void *eh, int ec, size_t shnum)
{
	Elf_Scn *scn;

	if (shnum >= SHN_LORESERVE) {
		if ((scn = _libelf_getscn0(e)) == NULL)
			return (0);

		assert(scn->s_ndx == SHN_UNDEF);

		if (ec == ELFCLASS32)
			scn->s_shdr.s_shdr32.sh_size = shnum;
		else
			scn->s_shdr.s_shdr64.sh_size = shnum;

		(void) elf_flagshdr(scn, ELF_C_SET, ELF_F_DIRTY);

		shnum = 0;
	}

	if (ec == ELFCLASS32)
		((Elf32_Ehdr *) eh)->e_shnum = shnum & 0xFFFFU;
	else
		((Elf64_Ehdr *) eh)->e_shnum = shnum & 0xFFFFU;


	return (1);
}

int
_libelf_setshstrndx(Elf *e, void *eh, int ec, size_t shstrndx)
{
	Elf_Scn *scn;

	if (shstrndx >= SHN_LORESERVE) {
		if ((scn = _libelf_getscn0(e)) == NULL)
			return (0);

		assert(scn->s_ndx == SHN_UNDEF);

		if (ec == ELFCLASS32)
			scn->s_shdr.s_shdr32.sh_link = shstrndx;
		else
			scn->s_shdr.s_shdr64.sh_link = shstrndx;

		(void) elf_flagshdr(scn, ELF_C_SET, ELF_F_DIRTY);

		shstrndx = SHN_XINDEX;
	}

	if (ec == ELFCLASS32)
		((Elf32_Ehdr *) eh)->e_shstrndx = shstrndx & 0xFFFFU;
	else
		((Elf64_Ehdr *) eh)->e_shstrndx = shstrndx & 0xFFFFU;

	return (1);
}

int
_libelf_setphnum(Elf *e, void *eh, int ec, size_t phnum)
{
	Elf_Scn *scn;

	if (phnum >= PN_XNUM) {
		if ((scn = _libelf_getscn0(e)) == NULL)
			return (0);

		assert(scn->s_ndx == SHN_UNDEF);

		if (ec == ELFCLASS32)
			scn->s_shdr.s_shdr32.sh_info = phnum;
		else
			scn->s_shdr.s_shdr64.sh_info = phnum;

		(void) elf_flagshdr(scn, ELF_C_SET, ELF_F_DIRTY);

		phnum = PN_XNUM;
	}

	if (ec == ELFCLASS32)
		((Elf32_Ehdr *) eh)->e_phnum = phnum & 0xFFFFU;
	else
		((Elf64_Ehdr *) eh)->e_phnum = phnum & 0xFFFFU;

	return (1);
}
