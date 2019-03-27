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
#include <libelf.h>

#include "_libelf.h"

ELFTC_VCSID("$Id: elf_rand.c 3174 2015-03-27 17:13:41Z emaste $");

off_t
elf_rand(Elf *ar, off_t offset)
{
	struct ar_hdr *arh;

	if (ar == NULL || ar->e_kind != ELF_K_AR ||
	    (offset & 1) || offset < SARMAG ||
	    (size_t) offset + sizeof(struct ar_hdr) >= ar->e_rawsize) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return 0;
	}

	arh = (struct ar_hdr *) (ar->e_rawfile + offset);

	/* a too simple sanity check */
	if (arh->ar_fmag[0] != '`' || arh->ar_fmag[1] != '\n') {
		LIBELF_SET_ERROR(ARCHIVE, 0);
		return 0;
	}

	ar->e_u.e_ar.e_next = offset;

	return (offset);
}
