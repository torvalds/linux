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

#include <libelf.h>

#include "_libelf.h"

ELFTC_VCSID("$Id$");

/*
 * Extension API: open a file for reading, ignoring parse errors.
 */

Elf *
elf_open(int fd)
{
	if (LIBELF_PRIVATE(version) == EV_NONE) {
		LIBELF_SET_ERROR(SEQUENCE, 0);
		return (NULL);
	}

	return (_libelf_open_object(fd, ELF_C_READ, 0));
}

/*
 * Extension API: create an ELF descriptor for an in-memory object,
 * ignoring parse errors.
 */

Elf *
elf_openmemory(char *image, size_t sz)
{
	if (LIBELF_PRIVATE(version) == EV_NONE) {
		LIBELF_SET_ERROR(SEQUENCE, 0);
		return (NULL);
	}

	if (image == NULL || sz == 0) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	return (_libelf_memory((unsigned char *) image, sz, 0));
}
