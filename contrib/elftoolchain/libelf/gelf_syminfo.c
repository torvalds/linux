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

#include "_libelf.h"

ELFTC_VCSID("$Id: gelf_syminfo.c 3174 2015-03-27 17:13:41Z emaste $");

GElf_Syminfo *
gelf_getsyminfo(Elf_Data *ed, int ndx, GElf_Syminfo *dst)
{
	int ec;
	Elf *e;
	size_t msz;
	Elf_Scn *scn;
	uint32_t sh_type;
	struct _Libelf_Data *d;
	Elf32_Syminfo *syminfo32;
	Elf64_Syminfo *syminfo64;

	d = (struct _Libelf_Data *) ed;

	if (d == NULL || ndx < 0 || dst == NULL ||
	    (scn = d->d_scn) == NULL ||
	    (e = scn->s_elf) == NULL) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	ec = e->e_class;
	assert(ec == ELFCLASS32 || ec == ELFCLASS64);

	if (ec == ELFCLASS32)
		sh_type = scn->s_shdr.s_shdr32.sh_type;
	else
		sh_type = scn->s_shdr.s_shdr64.sh_type;

	if (_libelf_xlate_shtype(sh_type) != ELF_T_SYMINFO) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	msz = _libelf_msize(ELF_T_SYMINFO, ec, e->e_version);

	assert(msz > 0);
	assert(ndx >= 0);

	if (msz * (size_t) ndx >= d->d_data.d_size) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	if (ec == ELFCLASS32) {

		syminfo32 = (Elf32_Syminfo *) d->d_data.d_buf + ndx;

		dst->si_boundto = syminfo32->si_boundto;
		dst->si_flags   = syminfo32->si_flags;

	} else {

		syminfo64 = (Elf64_Syminfo *) d->d_data.d_buf + ndx;

		*dst = *syminfo64;
	}

	return (dst);
}

int
gelf_update_syminfo(Elf_Data *ed, int ndx, GElf_Syminfo *gs)
{
	int ec;
	Elf *e;
	size_t msz;
	Elf_Scn *scn;
	uint32_t sh_type;
	struct _Libelf_Data *d;
	Elf32_Syminfo *syminfo32;
	Elf64_Syminfo *syminfo64;

	d = (struct _Libelf_Data *) ed;

	if (d == NULL || ndx < 0 || gs == NULL ||
	    (scn = d->d_scn) == NULL ||
	    (e = scn->s_elf) == NULL) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (0);
	}

	ec = e->e_class;
	assert(ec == ELFCLASS32 || ec == ELFCLASS64);

	if (ec == ELFCLASS32)
		sh_type = scn->s_shdr.s_shdr32.sh_type;
	else
		sh_type = scn->s_shdr.s_shdr64.sh_type;

	if (_libelf_xlate_shtype(sh_type) != ELF_T_SYMINFO) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (0);
	}

	msz = _libelf_msize(ELF_T_SYMINFO, ec, e->e_version);

	assert(msz > 0);
	assert(ndx >= 0);

	if (msz * (size_t) ndx >= d->d_data.d_size) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (0);
	}

	if (ec == ELFCLASS32) {
		syminfo32 = (Elf32_Syminfo *) d->d_data.d_buf + ndx;

		syminfo32->si_boundto  = gs->si_boundto;
		syminfo32->si_flags  = gs->si_flags;

	} else {
		syminfo64 = (Elf64_Syminfo *) d->d_data.d_buf + ndx;

		*syminfo64 = *gs;
	}

	return (1);
}
