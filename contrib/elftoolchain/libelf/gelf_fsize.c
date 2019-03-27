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

#include <gelf.h>
#include <libelf.h>

#include "_libelf.h"

ELFTC_VCSID("$Id: gelf_fsize.c 3174 2015-03-27 17:13:41Z emaste $");

size_t
elf32_fsize(Elf_Type t, size_t c, unsigned int v)
{
	return (_libelf_fsize(t, ELFCLASS32, v, c));
}

size_t
elf64_fsize(Elf_Type t, size_t c, unsigned int v)
{
	return (_libelf_fsize(t, ELFCLASS64, v, c));
}

size_t
gelf_fsize(Elf *e, Elf_Type t, size_t c, unsigned int v)
{

	if (e == NULL) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (0);
	}

	if (e->e_class == ELFCLASS32 || e->e_class == ELFCLASS64)
		return (_libelf_fsize(t, e->e_class, v, c));

	LIBELF_SET_ERROR(ARGUMENT, 0);
	return (0);
}
