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
#include <limits.h>
#include <stdint.h>

#include "_libelf.h"

ELFTC_VCSID("$Id: gelf_shdr.c 3177 2015-03-30 18:19:41Z emaste $");

Elf32_Shdr *
elf32_getshdr(Elf_Scn *s)
{
	return (_libelf_getshdr(s, ELFCLASS32));
}

Elf64_Shdr *
elf64_getshdr(Elf_Scn *s)
{
	return (_libelf_getshdr(s, ELFCLASS64));
}

GElf_Shdr *
gelf_getshdr(Elf_Scn *s, GElf_Shdr *d)
{
	int ec;
	void *sh;
	Elf32_Shdr *sh32;
	Elf64_Shdr *sh64;

	if (d == NULL) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	if ((sh = _libelf_getshdr(s, ELFCLASSNONE)) == NULL)
		return (NULL);

	ec = s->s_elf->e_class;
	assert(ec == ELFCLASS32 || ec == ELFCLASS64);

	if (ec == ELFCLASS32) {
		sh32 = (Elf32_Shdr *) sh;

		d->sh_name      = sh32->sh_name;
		d->sh_type      = sh32->sh_type;
		d->sh_flags     = (Elf64_Xword) sh32->sh_flags;
		d->sh_addr      = (Elf64_Addr) sh32->sh_addr;
		d->sh_offset    = (Elf64_Off) sh32->sh_offset;
		d->sh_size      = (Elf64_Xword) sh32->sh_size;
		d->sh_link      = sh32->sh_link;
		d->sh_info      = sh32->sh_info;
		d->sh_addralign = (Elf64_Xword) sh32->sh_addralign;
		d->sh_entsize   = (Elf64_Xword) sh32->sh_entsize;
	} else {
		sh64 = (Elf64_Shdr *) sh;
		*d = *sh64;
	}

	return (d);
}

int
gelf_update_shdr(Elf_Scn *scn, GElf_Shdr *s)
{
	int ec;
	Elf *e;
	Elf32_Shdr *sh32;


	if (s == NULL || scn == NULL || (e = scn->s_elf) == NULL ||
	    e->e_kind != ELF_K_ELF ||
	    ((ec = e->e_class) != ELFCLASS32 && ec != ELFCLASS64)) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (0);
	}

	if (e->e_cmd == ELF_C_READ) {
		LIBELF_SET_ERROR(MODE, 0);
		return (0);
	}

	(void) elf_flagscn(scn, ELF_C_SET, ELF_F_DIRTY);

	if (ec == ELFCLASS64) {
		scn->s_shdr.s_shdr64 = *s;
		return (1);
	}

	sh32 = &scn->s_shdr.s_shdr32;

	sh32->sh_name	 =  s->sh_name;
	sh32->sh_type	 =  s->sh_type;
	LIBELF_COPY_U32(sh32, s, sh_flags);
	LIBELF_COPY_U32(sh32, s, sh_addr);
	LIBELF_COPY_U32(sh32, s, sh_offset);
	LIBELF_COPY_U32(sh32, s, sh_size);
	sh32->sh_link	 =  s->sh_link;
	sh32->sh_info	 =  s->sh_info;
	LIBELF_COPY_U32(sh32, s, sh_addralign);
	LIBELF_COPY_U32(sh32, s, sh_entsize);

	return (1);
}
