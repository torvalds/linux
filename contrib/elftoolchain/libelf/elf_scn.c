/*-
 * Copyright (c) 2006,2008-2010 Joseph Koshy
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

#include <sys/queue.h>

#include <assert.h>
#include <errno.h>
#include <gelf.h>
#include <libelf.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "_libelf.h"

ELFTC_VCSID("$Id: elf_scn.c 3632 2018-10-10 21:12:43Z jkoshy $");

/*
 * Load an ELF section table and create a list of Elf_Scn structures.
 */
int
_libelf_load_section_headers(Elf *e, void *ehdr)
{
	Elf_Scn *scn;
	uint64_t shoff;
	Elf32_Ehdr *eh32;
	Elf64_Ehdr *eh64;
	int ec, swapbytes;
	unsigned char *src;
	size_t fsz, i, shnum;
	_libelf_translator_function *xlator;

	assert(e != NULL);
	assert(ehdr != NULL);
	assert((e->e_flags & LIBELF_F_SHDRS_LOADED) == 0);

#define	CHECK_EHDR(E,EH)	do {				\
		if (shoff > e->e_rawsize ||			\
		    fsz != (EH)->e_shentsize ||			\
		    shnum > SIZE_MAX / fsz ||			\
		    fsz * shnum > e->e_rawsize - shoff) {	\
			LIBELF_SET_ERROR(HEADER, 0);		\
			return (0);				\
		}						\
	} while (0)

	ec = e->e_class;
	fsz = _libelf_fsize(ELF_T_SHDR, ec, e->e_version, (size_t) 1);
	assert(fsz > 0);

	shnum = e->e_u.e_elf.e_nscn;

	if (ec == ELFCLASS32) {
		eh32 = (Elf32_Ehdr *) ehdr;
		shoff = (uint64_t) eh32->e_shoff;
		CHECK_EHDR(e, eh32);
	} else {
		eh64 = (Elf64_Ehdr *) ehdr;
		shoff = eh64->e_shoff;
		CHECK_EHDR(e, eh64);
	}

	xlator = _libelf_get_translator(ELF_T_SHDR, ELF_TOMEMORY, ec,
	    _libelf_elfmachine(e));

	swapbytes = e->e_byteorder != LIBELF_PRIVATE(byteorder);
	src = e->e_rawfile + shoff;

	/*
	 * If the file is using extended numbering then section #0
	 * would have already been read in.
	 */

	i = 0;
	if (!STAILQ_EMPTY(&e->e_u.e_elf.e_scn)) {
		assert(STAILQ_FIRST(&e->e_u.e_elf.e_scn) ==
		    STAILQ_LAST(&e->e_u.e_elf.e_scn, _Elf_Scn, s_next));

		i = 1;
		src += fsz;
	}

	for (; i < shnum; i++, src += fsz) {
		if ((scn = _libelf_allocate_scn(e, i)) == NULL)
			return (0);

		(*xlator)((unsigned char *) &scn->s_shdr, sizeof(scn->s_shdr),
		    src, (size_t) 1, swapbytes);

		if (ec == ELFCLASS32) {
			scn->s_offset = scn->s_rawoff =
			    scn->s_shdr.s_shdr32.sh_offset;
			scn->s_size = scn->s_shdr.s_shdr32.sh_size;
		} else {
			scn->s_offset = scn->s_rawoff =
			    scn->s_shdr.s_shdr64.sh_offset;
			scn->s_size = scn->s_shdr.s_shdr64.sh_size;
		}
	}

	e->e_flags |= LIBELF_F_SHDRS_LOADED;

	return (1);
}


Elf_Scn *
elf_getscn(Elf *e, size_t index)
{
	int ec;
	void *ehdr;
	Elf_Scn *s;

	if (e == NULL || e->e_kind != ELF_K_ELF ||
	    ((ec = e->e_class) != ELFCLASS32 && ec != ELFCLASS64)) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	if ((ehdr = _libelf_ehdr(e, ec, 0)) == NULL)
		return (NULL);

	if (e->e_cmd != ELF_C_WRITE &&
	    (e->e_flags & LIBELF_F_SHDRS_LOADED) == 0 &&
	    _libelf_load_section_headers(e, ehdr) == 0)
		return (NULL);

	STAILQ_FOREACH(s, &e->e_u.e_elf.e_scn, s_next)
		if (s->s_ndx == index)
			return (s);

	LIBELF_SET_ERROR(ARGUMENT, 0);
	return (NULL);
}

size_t
elf_ndxscn(Elf_Scn *s)
{
	if (s == NULL) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (SHN_UNDEF);
	}
	return (s->s_ndx);
}

Elf_Scn *
elf_newscn(Elf *e)
{
	int ec;
	void *ehdr;
	Elf_Scn *scn;

	if (e == NULL || e->e_kind != ELF_K_ELF) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	if ((ec = e->e_class) != ELFCLASS32 && ec != ELFCLASS64) {
		LIBELF_SET_ERROR(CLASS, 0);
		return (NULL);
	}

	if ((ehdr = _libelf_ehdr(e, ec, 0)) == NULL)
		return (NULL);

	/*
	 * The application may be asking for a new section descriptor
	 * on an ELF object opened with ELF_C_RDWR or ELF_C_READ.  We
	 * need to bring in the existing section information before
	 * appending a new one to the list.
	 *
	 * Per the ELF(3) API, an application is allowed to open a
	 * file using ELF_C_READ, mess with its internal structure and
	 * use elf_update(...,ELF_C_NULL) to compute its new layout.
	 */
	if (e->e_cmd != ELF_C_WRITE &&
	    (e->e_flags & LIBELF_F_SHDRS_LOADED) == 0 &&
	    _libelf_load_section_headers(e, ehdr) == 0)
		return (NULL);

	if (STAILQ_EMPTY(&e->e_u.e_elf.e_scn)) {
		assert(e->e_u.e_elf.e_nscn == 0);
		if ((scn = _libelf_allocate_scn(e, (size_t) SHN_UNDEF)) ==
		    NULL)
			return (NULL);
		e->e_u.e_elf.e_nscn++;
	}

	assert(e->e_u.e_elf.e_nscn > 0);

	if ((scn = _libelf_allocate_scn(e, e->e_u.e_elf.e_nscn)) == NULL)
		return (NULL);

	e->e_u.e_elf.e_nscn++;

	(void) elf_flagscn(scn, ELF_C_SET, ELF_F_DIRTY);

	return (scn);
}

Elf_Scn *
elf_nextscn(Elf *e, Elf_Scn *s)
{
	if (e == NULL || (e->e_kind != ELF_K_ELF) ||
	    (s && s->s_elf != e)) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	return (s == NULL ? elf_getscn(e, (size_t) 1) :
	    STAILQ_NEXT(s, s_next));
}
