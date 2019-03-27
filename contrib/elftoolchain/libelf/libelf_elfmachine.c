/*-
 * Copyright (c) 2018 Joseph Koshy
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

ELFTC_VCSID("$Id$");

/*
 * A convenience helper that returns the ELF machine architecture for
 * a ELF descriptor.
 */
int
_libelf_elfmachine(Elf *e)
{
	Elf32_Ehdr *eh32;
	Elf64_Ehdr *eh64;

	if (!e)
		return EM_NONE;

	assert(e->e_kind == ELF_K_ELF);
	assert(e->e_class != ELFCLASSNONE);

	eh32 = NULL;
	eh64 = NULL;

	switch (e->e_class) {
	case ELFCLASS32:
		eh32 = e->e_u.e_elf.e_ehdr.e_ehdr32;
		return eh32 ? eh32->e_machine : EM_NONE;
	case ELFCLASS64:
		eh64 = e->e_u.e_elf.e_ehdr.e_ehdr64;
		return eh64 ? eh64->e_machine : EM_NONE;
	}

	return (EM_NONE);
}
