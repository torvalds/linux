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
#include <gelf.h>
#include <libelf.h>
#include <stdlib.h>

#include "_libelf.h"

ELFTC_VCSID("$Id: libelf_phdr.c 3632 2018-10-10 21:12:43Z jkoshy $");

void *
_libelf_getphdr(Elf *e, int ec)
{
	size_t phnum;
	size_t fsz, msz;
	uint64_t phoff;
	Elf32_Ehdr *eh32;
	Elf64_Ehdr *eh64;
	void *ehdr, *phdr;
	_libelf_translator_function *xlator;

	assert(ec == ELFCLASS32 || ec == ELFCLASS64);

	if (e == NULL) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	if ((phdr = (ec == ELFCLASS32 ?
		 (void *) e->e_u.e_elf.e_phdr.e_phdr32 :
		 (void *) e->e_u.e_elf.e_phdr.e_phdr64)) != NULL)
		return (phdr);

	/*
	 * Check the PHDR related fields in the EHDR for sanity.
	 */

	if ((ehdr = _libelf_ehdr(e, ec, 0)) == NULL)
		return (NULL);

	phnum = e->e_u.e_elf.e_nphdr;

	if (ec == ELFCLASS32) {
		eh32      = (Elf32_Ehdr *) ehdr;
		phoff     = (uint64_t) eh32->e_phoff;
	} else {
		eh64      = (Elf64_Ehdr *) ehdr;
		phoff     = (uint64_t) eh64->e_phoff;
	}

	fsz = gelf_fsize(e, ELF_T_PHDR, phnum, e->e_version);

	assert(fsz > 0);

	if ((uint64_t) e->e_rawsize < (phoff + fsz)) {
		LIBELF_SET_ERROR(HEADER, 0);
		return (NULL);
	}

	msz = _libelf_msize(ELF_T_PHDR, ec, EV_CURRENT);

	assert(msz > 0);

	if ((phdr = calloc(phnum, msz)) == NULL) {
		LIBELF_SET_ERROR(RESOURCE, 0);
		return (NULL);
	}

	if (ec == ELFCLASS32)
		e->e_u.e_elf.e_phdr.e_phdr32 = phdr;
	else
		e->e_u.e_elf.e_phdr.e_phdr64 = phdr;


	xlator = _libelf_get_translator(ELF_T_PHDR, ELF_TOMEMORY, ec,
	    _libelf_elfmachine(e));
	(*xlator)(phdr, phnum * msz, e->e_rawfile + phoff, phnum,
	    e->e_byteorder != LIBELF_PRIVATE(byteorder));

	return (phdr);
}

void *
_libelf_newphdr(Elf *e, int ec, size_t count)
{
	void *ehdr, *newphdr, *oldphdr;
	size_t msz;

	if (e == NULL) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	if ((ehdr = _libelf_ehdr(e, ec, 0)) == NULL) {
		LIBELF_SET_ERROR(SEQUENCE, 0);
		return (NULL);
	}

	assert(e->e_class == ec);
	assert(ec == ELFCLASS32 || ec == ELFCLASS64);
	assert(e->e_version == EV_CURRENT);

	msz = _libelf_msize(ELF_T_PHDR, ec, e->e_version);

	assert(msz > 0);

	newphdr = NULL;
	if (count > 0 && (newphdr = calloc(count, msz)) == NULL) {
		LIBELF_SET_ERROR(RESOURCE, 0);
		return (NULL);
	}

	if (ec == ELFCLASS32) {
		if ((oldphdr = (void *) e->e_u.e_elf.e_phdr.e_phdr32) != NULL)
			free(oldphdr);
		e->e_u.e_elf.e_phdr.e_phdr32 = (Elf32_Phdr *) newphdr;
	} else {
		if ((oldphdr = (void *) e->e_u.e_elf.e_phdr.e_phdr64) != NULL)
			free(oldphdr);
		e->e_u.e_elf.e_phdr.e_phdr64 = (Elf64_Phdr *) newphdr;
	}

	e->e_u.e_elf.e_nphdr = count;

	elf_flagphdr(e, ELF_C_SET, ELF_F_DIRTY);

	return (newphdr);
}
