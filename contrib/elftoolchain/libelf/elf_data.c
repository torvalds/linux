/*-
 * Copyright (c) 2006,2008,2011 Joseph Koshy
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
#include <errno.h>
#include <libelf.h>
#include <stdint.h>
#include <stdlib.h>

#include "_libelf.h"

ELFTC_VCSID("$Id: elf_data.c 3632 2018-10-10 21:12:43Z jkoshy $");

Elf_Data *
elf_getdata(Elf_Scn *s, Elf_Data *ed)
{
	Elf *e;
	unsigned int sh_type;
	int elfclass, elftype;
	size_t count, fsz, msz;
	struct _Libelf_Data *d;
	uint64_t sh_align, sh_offset, sh_size;
	_libelf_translator_function *xlate;

	d = (struct _Libelf_Data *) ed;

	if (s == NULL || (e = s->s_elf) == NULL ||
	    (d != NULL && s != d->d_scn)) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	assert(e->e_kind == ELF_K_ELF);

	if (d == NULL && (d = STAILQ_FIRST(&s->s_data)) != NULL)
		return (&d->d_data);

	if (d != NULL)
		return (&STAILQ_NEXT(d, d_next)->d_data);

	if (e->e_rawfile == NULL) {
		/*
		 * In the ELF_C_WRITE case, there is no source that
		 * can provide data for the section.
		 */
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	elfclass = e->e_class;

	assert(elfclass == ELFCLASS32 || elfclass == ELFCLASS64);

	if (elfclass == ELFCLASS32) {
		sh_type   = s->s_shdr.s_shdr32.sh_type;
		sh_offset = (uint64_t) s->s_shdr.s_shdr32.sh_offset;
		sh_size   = (uint64_t) s->s_shdr.s_shdr32.sh_size;
		sh_align  = (uint64_t) s->s_shdr.s_shdr32.sh_addralign;
	} else {
		sh_type   = s->s_shdr.s_shdr64.sh_type;
		sh_offset = s->s_shdr.s_shdr64.sh_offset;
		sh_size   = s->s_shdr.s_shdr64.sh_size;
		sh_align  = s->s_shdr.s_shdr64.sh_addralign;
	}

	if (sh_type == SHT_NULL) {
		LIBELF_SET_ERROR(SECTION, 0);
		return (NULL);
	}

	if ((elftype = _libelf_xlate_shtype(sh_type)) < ELF_T_FIRST ||
	    elftype > ELF_T_LAST || (sh_type != SHT_NOBITS &&
	    (sh_offset > e->e_rawsize || sh_size > e->e_rawsize - sh_offset))) {
		LIBELF_SET_ERROR(SECTION, 0);
		return (NULL);
	}

	if ((fsz = (elfclass == ELFCLASS32 ? elf32_fsize : elf64_fsize)
            (elftype, (size_t) 1, e->e_version)) == 0) {
		LIBELF_SET_ERROR(UNIMPL, 0);
		return (NULL);
	}

	if (sh_size % fsz) {
		LIBELF_SET_ERROR(SECTION, 0);
		return (NULL);
	}

	if (sh_size / fsz > SIZE_MAX) {
		LIBELF_SET_ERROR(RANGE, 0);
		return (NULL);
	}

	count = (size_t) (sh_size / fsz);

	msz = _libelf_msize(elftype, elfclass, e->e_version);

	if (count > 0 && msz > SIZE_MAX / count) {
		LIBELF_SET_ERROR(RANGE, 0);
		return (NULL);
	}

	assert(msz > 0);
	assert(count <= SIZE_MAX);
	assert(msz * count <= SIZE_MAX);

	if ((d = _libelf_allocate_data(s)) == NULL)
		return (NULL);

	d->d_data.d_buf     = NULL;
	d->d_data.d_off     = 0;
	d->d_data.d_align   = sh_align;
	d->d_data.d_size    = msz * count;
	d->d_data.d_type    = elftype;
	d->d_data.d_version = e->e_version;

	if (sh_type == SHT_NOBITS || sh_size == 0) {
	        STAILQ_INSERT_TAIL(&s->s_data, d, d_next);
		return (&d->d_data);
        }

	if ((d->d_data.d_buf = malloc(msz * count)) == NULL) {
		(void) _libelf_release_data(d);
		LIBELF_SET_ERROR(RESOURCE, 0);
		return (NULL);
	}

	d->d_flags  |= LIBELF_F_DATA_MALLOCED;

	xlate = _libelf_get_translator(elftype, ELF_TOMEMORY, elfclass,
	    _libelf_elfmachine(e));
	if (!(*xlate)(d->d_data.d_buf, (size_t) d->d_data.d_size,
	    e->e_rawfile + sh_offset, count,
	    e->e_byteorder != LIBELF_PRIVATE(byteorder))) {
		_libelf_release_data(d);
		LIBELF_SET_ERROR(DATA, 0);
		return (NULL);
	}

	STAILQ_INSERT_TAIL(&s->s_data, d, d_next);

	return (&d->d_data);
}

Elf_Data *
elf_newdata(Elf_Scn *s)
{
	Elf *e;
	struct _Libelf_Data *d;

	if (s == NULL || (e = s->s_elf) == NULL) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	assert(e->e_kind == ELF_K_ELF);

	/*
	 * elf_newdata() has to append a data descriptor, so
	 * bring in existing section data if not already present.
	 */
	if (e->e_rawfile && s->s_size > 0 && STAILQ_EMPTY(&s->s_data))
		if (elf_getdata(s, NULL) == NULL)
			return (NULL);

	if ((d = _libelf_allocate_data(s)) == NULL)
		return (NULL);

	STAILQ_INSERT_TAIL(&s->s_data, d, d_next);

	d->d_data.d_align = 1;
	d->d_data.d_buf = NULL;
	d->d_data.d_off = (uint64_t) ~0;
	d->d_data.d_size = 0;
	d->d_data.d_type = ELF_T_BYTE;
	d->d_data.d_version = LIBELF_PRIVATE(version);

	(void) elf_flagscn(s, ELF_C_SET, ELF_F_DIRTY);

	return (&d->d_data);
}

/*
 * Retrieve a data descriptor for raw (untranslated) data for section
 * `s'.
 */

Elf_Data *
elf_rawdata(Elf_Scn *s, Elf_Data *ed)
{
	Elf *e;
	int elf_class;
	uint32_t sh_type;
	struct _Libelf_Data *d;
	uint64_t sh_align, sh_offset, sh_size;

	if (s == NULL || (e = s->s_elf) == NULL || e->e_rawfile == NULL) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	assert(e->e_kind == ELF_K_ELF);

	d = (struct _Libelf_Data *) ed;

	if (d == NULL && (d = STAILQ_FIRST(&s->s_rawdata)) != NULL)
		return (&d->d_data);

	if (d != NULL)
		return (&STAILQ_NEXT(d, d_next)->d_data);

	elf_class = e->e_class;

	assert(elf_class == ELFCLASS32 || elf_class == ELFCLASS64);

	if (elf_class == ELFCLASS32) {
		sh_type   = s->s_shdr.s_shdr32.sh_type;
		sh_offset = (uint64_t) s->s_shdr.s_shdr32.sh_offset;
		sh_size   = (uint64_t) s->s_shdr.s_shdr32.sh_size;
		sh_align  = (uint64_t) s->s_shdr.s_shdr32.sh_addralign;
	} else {
		sh_type   = s->s_shdr.s_shdr64.sh_type;
		sh_offset = s->s_shdr.s_shdr64.sh_offset;
		sh_size   = s->s_shdr.s_shdr64.sh_size;
		sh_align  = s->s_shdr.s_shdr64.sh_addralign;
	}

	if (sh_type == SHT_NULL) {
		LIBELF_SET_ERROR(SECTION, 0);
		return (NULL);
	}

	if (sh_type != SHT_NOBITS &&
	    (sh_offset > e->e_rawsize || sh_size > e->e_rawsize - sh_offset)) {
		LIBELF_SET_ERROR(SECTION, 0);
		return (NULL);
	}

	if ((d = _libelf_allocate_data(s)) == NULL)
		return (NULL);

	d->d_data.d_buf = (sh_type == SHT_NOBITS || sh_size == 0) ? NULL :
	    e->e_rawfile + sh_offset;
	d->d_data.d_off     = 0;
	d->d_data.d_align   = sh_align;
	d->d_data.d_size    = sh_size;
	d->d_data.d_type    = ELF_T_BYTE;
	d->d_data.d_version = e->e_version;

	STAILQ_INSERT_TAIL(&s->s_rawdata, d, d_next);

	return (&d->d_data);
}
