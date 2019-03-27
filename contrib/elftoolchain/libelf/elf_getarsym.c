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

#include <libelf.h>

#include "_libelf.h"

ELFTC_VCSID("$Id: elf_getarsym.c 3174 2015-03-27 17:13:41Z emaste $");

Elf_Arsym *
elf_getarsym(Elf *ar, size_t *ptr)
{
	size_t n;
	Elf_Arsym *symtab;

	n = 0;
	symtab = NULL;

	if (ar == NULL || ar->e_kind != ELF_K_AR)
		LIBELF_SET_ERROR(ARGUMENT, 0);
	else if ((symtab = ar->e_u.e_ar.e_symtab) != NULL)
		n = ar->e_u.e_ar.e_symtabsz;
	else if (ar->e_u.e_ar.e_rawsymtab)
		symtab = (ar->e_flags & LIBELF_F_AR_VARIANT_SVR4) ?
		    _libelf_ar_process_svr4_symtab(ar, &n) :
		    _libelf_ar_process_bsd_symtab(ar, &n);
	else
		LIBELF_SET_ERROR(ARCHIVE, 0);

	if (ptr)
		*ptr = n;
	return (symtab);
}
