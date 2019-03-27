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

ELFTC_VCSID("$Id: pe_buffer.c 3312 2016-01-10 09:23:51Z kaiwang27 $");

PE_Buffer *
pe_getbuffer(PE_Scn *ps, PE_Buffer *pb)
{
	PE *pe;
	PE_SecBuf *sb;

	if (ps == NULL) {
		errno = EINVAL;
		return (NULL);
	}

	pe = ps->ps_pe;

	if ((ps->ps_flags & LIBPE_F_LOAD_SECTION) == 0) {
		if (pe->pe_flags & LIBPE_F_FD_DONE) {
			errno = EACCES;
			return (NULL);
		}
		if (pe->pe_flags & LIBPE_F_SPECIAL_FILE) {
			if (libpe_load_all_sections(pe) < 0)
				return (NULL);
		} else {
			if (libpe_load_section(pe, ps) < 0)
				return (NULL);
		}
	}

	sb = (PE_SecBuf *) pb;

	if (sb == NULL)
		sb = STAILQ_FIRST(&ps->ps_b);
	else
		sb = STAILQ_NEXT(sb, sb_next);

	return ((PE_Buffer *) sb);
}

PE_Buffer *
pe_newbuffer(PE_Scn *ps)
{
	PE *pe;
	PE_SecBuf *sb;

	if (ps == NULL) {
		errno = EINVAL;
		return (NULL);
	}

	pe = ps->ps_pe;

	if (pe->pe_flags & LIBPE_F_FD_DONE) {
		errno = EACCES;
		return (NULL);
	}

	if ((ps->ps_flags & LIBPE_F_LOAD_SECTION) == 0) {
		if (libpe_load_section(pe, ps) < 0)
			return (NULL);
	}

	if ((sb = libpe_alloc_buffer(ps, 0)) == NULL)
		return (NULL);

	sb->sb_flags |= PE_F_DIRTY;
	ps->ps_flags |= PE_F_DIRTY;

	return ((PE_Buffer *) sb);
}
