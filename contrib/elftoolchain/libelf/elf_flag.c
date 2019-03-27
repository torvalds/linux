/*-
 * Copyright (c) 2006,2008-2009,2011 Joseph Koshy
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

#include <libelf.h>

#include "_libelf.h"

ELFTC_VCSID("$Id: elf_flag.c 3174 2015-03-27 17:13:41Z emaste $");

unsigned int
elf_flagarhdr(Elf_Arhdr *a, Elf_Cmd c, unsigned int flags)
{
	unsigned int r;

	if (a == NULL)
		return (0);

	if ((c != ELF_C_SET && c != ELF_C_CLR) ||
	    (flags & ~ELF_F_DIRTY) != 0) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (0);
	}

	if (c == ELF_C_SET)
		r = a->ar_flags |= flags;
	else
		r = a->ar_flags &= ~flags;

	return (r & LIBELF_F_API_MASK);
}

unsigned int
elf_flagdata(Elf_Data *d, Elf_Cmd c, unsigned int flags)
{
	unsigned int r;
	struct _Libelf_Data *ld;

	if (d == NULL)
		return (0);

	if ((c != ELF_C_SET && c != ELF_C_CLR) ||
	    (flags & ~ELF_F_DIRTY) != 0) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (0);
	}

	ld = (struct _Libelf_Data *) d;

	if (c == ELF_C_SET)
		r = ld->d_flags |= flags;
	else
		r = ld->d_flags &= ~flags;

	return (r & LIBELF_F_API_MASK);
}

unsigned int
elf_flagehdr(Elf *e, Elf_Cmd c, unsigned int flags)
{
	int ec;
	void *ehdr;

	if (e == NULL)
		return (0);

	if ((c != ELF_C_SET && c != ELF_C_CLR) ||
	    (e->e_kind != ELF_K_ELF) || (flags & ~ELF_F_DIRTY) != 0 ||
	    ((ec = e->e_class) != ELFCLASS32 && ec != ELFCLASS64)) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (0);
	}

	if (ec == ELFCLASS32)
		ehdr = e->e_u.e_elf.e_ehdr.e_ehdr32;
	else
		ehdr = e->e_u.e_elf.e_ehdr.e_ehdr64;

	if (ehdr == NULL) {
		LIBELF_SET_ERROR(SEQUENCE, 0);
		return (0);
	}

	return (elf_flagelf(e, c, flags));
}

unsigned int
elf_flagelf(Elf *e, Elf_Cmd c, unsigned int flags)
{
	unsigned int r;

	if (e == NULL)
		return (0);

	if ((c != ELF_C_SET && c != ELF_C_CLR) ||
	    (e->e_kind != ELF_K_ELF) ||
	    (flags & ~(ELF_F_ARCHIVE | ELF_F_ARCHIVE_SYSV |
	    ELF_F_DIRTY | ELF_F_LAYOUT)) != 0) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (0);
	}

	if ((flags & ELF_F_ARCHIVE_SYSV) && (flags & ELF_F_ARCHIVE) == 0) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (0);
	}

	if ((flags & ELF_F_ARCHIVE) && e->e_cmd != ELF_C_WRITE) {
		LIBELF_SET_ERROR(MODE, 0);
		return (0);
	}

	if (c == ELF_C_SET)
		r = e->e_flags |= flags;
	else
		r = e->e_flags &= ~flags;
	return (r & LIBELF_F_API_MASK);
}

unsigned int
elf_flagphdr(Elf *e, Elf_Cmd c, unsigned int flags)
{
	int ec;
	void *phdr;

	if (e == NULL)
		return (0);

	if ((c != ELF_C_SET && c != ELF_C_CLR) ||
	    (e->e_kind != ELF_K_ELF) || (flags & ~ELF_F_DIRTY) != 0 ||
	    ((ec = e->e_class) != ELFCLASS32 && ec != ELFCLASS64)) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (0);
	}

	if (ec == ELFCLASS32)
		phdr = e->e_u.e_elf.e_phdr.e_phdr32;
	else
		phdr = e->e_u.e_elf.e_phdr.e_phdr64;

	if (phdr == NULL) {
		LIBELF_SET_ERROR(SEQUENCE, 0);
		return (0);
	}

	return (elf_flagelf(e, c, flags));
}

unsigned int
elf_flagscn(Elf_Scn *s, Elf_Cmd c, unsigned int flags)
{
	unsigned int r;

	if (s == NULL)
		return (0);

	if ((c != ELF_C_SET && c != ELF_C_CLR) ||
	    (flags & ~ELF_F_DIRTY) != 0) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (0);
	}

	if (c == ELF_C_SET)
		r = s->s_flags |= flags;
	else
		r = s->s_flags &= ~flags;
	return (r & LIBELF_F_API_MASK);
}

unsigned int
elf_flagshdr(Elf_Scn *s, Elf_Cmd c, unsigned int flags)
{
	return (elf_flagscn(s, c, flags));
}
