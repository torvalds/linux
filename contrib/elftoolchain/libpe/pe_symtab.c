/*-
 * Copyright (c) 2016 Kai Wang
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

#include <errno.h>

#include "_libpe.h"

ELFTC_VCSID("$Id: pe_symtab.c 3312 2016-01-10 09:23:51Z kaiwang27 $");

int
pe_update_symtab(PE *pe, char *symtab, size_t sz, unsigned int nsym)
{
	PE_Scn *ps;
	PE_SecBuf *sb;
	PE_SecHdr *sh;

	if (pe == NULL || symtab == NULL || sz == 0) {
		errno = EINVAL;
		return (-1);
	}

	if (pe->pe_cmd == PE_C_READ || pe->pe_flags & LIBPE_F_FD_DONE) {
		errno = EACCES;
		return (-1);
	}

	/* Remove the old symbol table. */
	STAILQ_FOREACH(ps, &pe->pe_scn, ps_next) {
		if (ps->ps_ndx == 0xFFFFFFFFU)
			libpe_release_scn(ps);
	}

	/*
	 * Insert the new symbol table.
	 */

	if ((ps = libpe_alloc_scn(pe)) == NULL)
		return (-1);

	STAILQ_INSERT_TAIL(&pe->pe_scn, ps, ps_next);
	ps->ps_ndx = 0xFFFFFFFFU;
	ps->ps_flags |= PE_F_DIRTY;

	/*
	 * Set the symbol table section offset to the maximum to make sure
	 * that it will be placed in the end of the file during section
	 * layout.
	 */
	sh = &ps->ps_sh;
	sh->sh_rawptr = 0xFFFFFFFFU;
	sh->sh_rawsize = sz;

	/* Allocate the buffer. */
	if ((sb = libpe_alloc_buffer(ps, 0)) == NULL)
		return (-1);
	sb->sb_flags |= PE_F_DIRTY;
	sb->sb_pb.pb_size = sz;
	sb->sb_pb.pb_buf = symtab;

	pe->pe_nsym = nsym;

	return (0);
}
