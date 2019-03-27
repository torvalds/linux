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

ELFTC_VCSID("$Id: pe_flag.c 3312 2016-01-10 09:23:51Z kaiwang27 $");

int
pe_flag(PE *pe, PE_Cmd c, unsigned int flags)
{

	if (pe == NULL || (c != PE_C_SET && c != PE_C_CLR)) {
		errno = EINVAL;
		return (-1);
	}

	if ((flags & ~(PE_F_STRIP_DOS_STUB | PE_F_STRIP_RICH_HEADER |
	    PE_F_STRIP_SYMTAB | PE_F_STRIP_DEBUG)) != 0) {
		errno = EINVAL;
		return (-1);
	}

	if (c == PE_C_SET)
		pe->pe_flags |= flags;
	else
		pe->pe_flags &= ~flags;

	return (0);
}

int
pe_flag_dos_header(PE *pe, PE_Cmd c, unsigned int flags)
{

	if (pe == NULL || (c != PE_C_SET && c != PE_C_CLR) ||
	    (flags & ~PE_F_DIRTY) != 0) {
		errno = EINVAL;
		return (-1);
	}

	if (c == PE_C_SET)
		pe->pe_flags |= LIBPE_F_DIRTY_DOS_HEADER;
	else
		pe->pe_flags &= ~LIBPE_F_DIRTY_DOS_HEADER;

	return (0);
}

int
pe_flag_coff_header(PE *pe, PE_Cmd c, unsigned int flags)
{

	if (pe == NULL || (c != PE_C_SET && c != PE_C_CLR) ||
	    (flags & ~PE_F_DIRTY) != 0) {
		errno = EINVAL;
		return (-1);
	}

	if (c == PE_C_SET)
		pe->pe_flags |= LIBPE_F_DIRTY_COFF_HEADER;
	else
		pe->pe_flags &= ~LIBPE_F_DIRTY_COFF_HEADER;

	return (0);
}

int
pe_flag_opt_header(PE *pe, PE_Cmd c, unsigned int flags)
{

	if (pe == NULL || (c != PE_C_SET && c != PE_C_CLR) ||
	    (flags & ~PE_F_DIRTY) != 0) {
		errno = EINVAL;
		return (-1);
	}

	if (c == PE_C_SET)
		pe->pe_flags |= LIBPE_F_DIRTY_OPT_HEADER;
	else
		pe->pe_flags &= ~LIBPE_F_DIRTY_OPT_HEADER;

	return (0);
}

int
pe_flag_data_dir(PE *pe, PE_Cmd c, unsigned int flags)
{

	if (pe == NULL || (c != PE_C_SET && c != PE_C_CLR) ||
	    (flags & ~PE_F_DIRTY) != 0) {
		errno = EINVAL;
		return (-1);
	}

	if (c == PE_C_SET)
		pe->pe_flags |= LIBPE_F_DIRTY_OPT_HEADER;
	else
		pe->pe_flags &= ~LIBPE_F_DIRTY_OPT_HEADER;

	return (0);
}

int
pe_flag_scn(PE_Scn *ps, PE_Cmd c, unsigned int flags)
{

	if (ps == NULL || (c != PE_C_SET && c != PE_C_CLR) ||
	    (flags & ~(PE_F_DIRTY | PE_F_STRIP_SECTION)) == 0) {
		errno = EINVAL;
		return (-1);
	}

	if (c == PE_C_SET)
		ps->ps_flags |= flags;
	else
		ps->ps_flags &= ~flags;
	
	return (0);
}

int
pe_flag_section_header(PE_Scn *ps, PE_Cmd c, unsigned int flags)
{
	PE *pe;

	if (ps == NULL || (c != PE_C_SET && c != PE_C_CLR) ||
	    (flags & ~PE_F_DIRTY) != 0) {
		errno = EINVAL;
		return (-1);
	}

	pe = ps->ps_pe;

	/* The library doesn't support per section header dirty flag. */
	if (c == PE_C_SET)
		pe->pe_flags |= LIBPE_F_DIRTY_SEC_HEADER;
	else
		pe->pe_flags &= ~LIBPE_F_DIRTY_SEC_HEADER;

	return (0);
}

int
pe_flag_buffer(PE_Buffer *pb, PE_Cmd c, unsigned int flags)
{
	PE_SecBuf *sb;

	if (pb == NULL || (c != PE_C_SET && c != PE_C_CLR) ||
	    (flags & ~PE_F_DIRTY) != 0) {
		errno = EINVAL;
		return (-1);
	}

	sb = (PE_SecBuf *) pb;

	if (c == PE_C_SET)
		sb->sb_flags |= flags;
	else
		sb->sb_flags &= ~flags;

	return (0);
}
