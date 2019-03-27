/*-
 * Copyright (c) 2006,2008,2018 Joseph Koshy
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
#include <string.h>

#include "_libelf.h"

ELFTC_VCSID("$Id: gelf_xlate.c 3632 2018-10-10 21:12:43Z jkoshy $");

Elf_Data *
elf32_xlatetof(Elf_Data *dst, const Elf_Data *src, unsigned int encoding)
{
	return _libelf_xlate(dst, src, encoding, ELFCLASS32, EM_NONE,
	    ELF_TOFILE);
}

Elf_Data *
elf64_xlatetof(Elf_Data *dst, const Elf_Data *src, unsigned int encoding)
{
	return _libelf_xlate(dst, src, encoding, ELFCLASS64, EM_NONE,
	    ELF_TOFILE);
}

Elf_Data *
elf32_xlatetom(Elf_Data *dst, const Elf_Data *src, unsigned int encoding)
{
	return _libelf_xlate(dst, src, encoding, ELFCLASS32, EM_NONE,
	    ELF_TOMEMORY);
}

Elf_Data *
elf64_xlatetom(Elf_Data *dst, const Elf_Data *src, unsigned int encoding)
{
	return _libelf_xlate(dst, src, encoding, ELFCLASS64, EM_NONE,
	    ELF_TOMEMORY);
}

Elf_Data *
gelf_xlatetom(Elf *e, Elf_Data *dst, const Elf_Data *src,
    unsigned int encoding)
{
	if (e != NULL)
		return (_libelf_xlate(dst, src, encoding, e->e_class,
		    _libelf_elfmachine(e), ELF_TOMEMORY));
	LIBELF_SET_ERROR(ARGUMENT, 0);
	return (NULL);
}

Elf_Data *
gelf_xlatetof(Elf *e, Elf_Data *dst, const Elf_Data *src,
    unsigned int encoding)
{
	if (e != NULL)
		return (_libelf_xlate(dst, src, encoding, e->e_class,
		    _libelf_elfmachine(e), ELF_TOFILE));
	LIBELF_SET_ERROR(ARGUMENT, 0);
	return (NULL);
}
