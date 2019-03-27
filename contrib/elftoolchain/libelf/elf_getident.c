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

#include <ar.h>
#include <assert.h>
#include <libelf.h>

#include "_libelf.h"

ELFTC_VCSID("$Id: elf_getident.c 3174 2015-03-27 17:13:41Z emaste $");

char *
elf_getident(Elf *e, size_t *sz)
{

	if (e == NULL) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		goto error;
	}

	if (e->e_cmd == ELF_C_WRITE && e->e_rawfile == NULL) {
		LIBELF_SET_ERROR(SEQUENCE, 0);
		goto error;
	}

	assert(e->e_kind != ELF_K_AR || e->e_cmd == ELF_C_READ);

	if (sz) {
		if (e->e_kind == ELF_K_AR)
			*sz = SARMAG;
		else if (e->e_kind == ELF_K_ELF)
			*sz = EI_NIDENT;
		else
			*sz = e->e_rawsize;
	}

	return ((char *) e->e_rawfile);

 error:
	if (sz)
		*sz = 0;
	return (NULL);
}
