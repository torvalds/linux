/*-
 * Copyright (c) 2011 Joseph Koshy
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

#include <ar.h>
#include <assert.h>
#include <string.h>
#include <libelf.h>

#include "_libelf.h"

ELFTC_VCSID("$Id: libelf_memory.c 3013 2014-03-23 06:16:59Z jkoshy $");

/*
 * Create an ELF descriptor for a memory image, optionally reporting
 * parse errors.
 */

Elf *
_libelf_memory(unsigned char *image, size_t sz, int reporterror)
{
	Elf *e;
	int e_class;
	enum Elf_Error error;
	unsigned int e_byteorder, e_version;

	assert(image != NULL);
	assert(sz > 0);

	if ((e = _libelf_allocate_elf()) == NULL)
		return (NULL);

	e->e_cmd = ELF_C_READ;
	e->e_rawfile = image;
	e->e_rawsize = sz;

#undef	LIBELF_IS_ELF
#define	LIBELF_IS_ELF(P) ((P)[EI_MAG0] == ELFMAG0 && 		\
	(P)[EI_MAG1] == ELFMAG1 && (P)[EI_MAG2] == ELFMAG2 &&	\
	(P)[EI_MAG3] == ELFMAG3)

	if (sz > EI_NIDENT && LIBELF_IS_ELF(image)) {
		e_byteorder = image[EI_DATA];
		e_class     = image[EI_CLASS];
		e_version   = image[EI_VERSION];

		error = ELF_E_NONE;

		if (e_version > EV_CURRENT)
			error = ELF_E_VERSION;
		else if ((e_byteorder != ELFDATA2LSB && e_byteorder !=
 		    ELFDATA2MSB) || (e_class != ELFCLASS32 && e_class !=
		    ELFCLASS64))
			error = ELF_E_HEADER;

		if (error != ELF_E_NONE) {
			if (reporterror) {
				LIBELF_PRIVATE(error) = LIBELF_ERROR(error, 0);
				(void) _libelf_release_elf(e);
				return (NULL);
			}
		} else {
			_libelf_init_elf(e, ELF_K_ELF);

			e->e_byteorder = e_byteorder;
			e->e_class = e_class;
			e->e_version = e_version;
		}
	} else if (sz >= SARMAG &&
	    strncmp((const char *) image, ARMAG, (size_t) SARMAG) == 0)
		return (_libelf_ar_open(e, reporterror));

	return (e);
}
