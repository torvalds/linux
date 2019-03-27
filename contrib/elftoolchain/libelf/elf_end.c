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

#include <assert.h>
#include <libelf.h>
#include <stdlib.h>

#include "_libelf.h"

#if	ELFTC_HAVE_MMAP
#include <sys/mman.h>
#endif

ELFTC_VCSID("$Id: elf_end.c 3174 2015-03-27 17:13:41Z emaste $");

int
elf_end(Elf *e)
{
	Elf *sv;
	Elf_Scn *scn, *tscn;

	if (e == NULL || e->e_activations == 0)
		return (0);

	if (--e->e_activations > 0)
		return (e->e_activations);

	assert(e->e_activations == 0);

	while (e && e->e_activations == 0) {
		switch (e->e_kind) {
		case ELF_K_AR:
			/*
			 * If we still have open child descriptors, we
			 * need to defer reclaiming resources till all
			 * the child descriptors for the archive are
			 * closed.
			 */
			if (e->e_u.e_ar.e_nchildren > 0)
				return (0);
			break;
		case ELF_K_ELF:
			/*
			 * Reclaim all section descriptors.
			 */
			STAILQ_FOREACH_SAFE(scn, &e->e_u.e_elf.e_scn, s_next,
			    tscn)
 				scn = _libelf_release_scn(scn);
			break;
		case ELF_K_NUM:
			assert(0);
		default:
			break;
		}

		if (e->e_rawfile) {
			if (e->e_flags & LIBELF_F_RAWFILE_MALLOC)
				free(e->e_rawfile);
#if	ELFTC_HAVE_MMAP
			else if (e->e_flags & LIBELF_F_RAWFILE_MMAP)
				(void) munmap(e->e_rawfile, e->e_rawsize);
#endif
		}

		sv = e;
		if ((e = e->e_parent) != NULL)
			e->e_u.e_ar.e_nchildren--;
		sv = _libelf_release_elf(sv);
	}

	return (0);
}
