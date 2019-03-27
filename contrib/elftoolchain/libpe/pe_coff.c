/*-
 * Copyright (c) 2015 Kai Wang
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
#include <stdlib.h>
#include <string.h>

#include "_libpe.h"

ELFTC_VCSID("$Id: pe_coff.c 3312 2016-01-10 09:23:51Z kaiwang27 $");

PE_CoffHdr *
pe_coff_header(PE *pe)
{

	if (pe->pe_ch == NULL) {
		errno = ENOENT;
		return (NULL);
	}

	return (pe->pe_ch);
}

PE_OptHdr *
pe_opt_header(PE *pe)
{

	if (pe->pe_oh == NULL) {
		errno = ENOENT;
		return (NULL);
	}

	return (pe->pe_oh);
}

PE_DataDir *
pe_data_dir(PE *pe)
{

	if (pe->pe_dd == NULL) {
		errno = ENOENT;
		return (NULL);
	}

	return (pe->pe_dd);
}

int
pe_update_coff_header(PE *pe, PE_CoffHdr *ch)
{

	if (pe == NULL || ch == NULL) {
		errno = EINVAL;
		return (-1);
	}

	if (pe->pe_cmd == PE_C_READ || pe->pe_flags & LIBPE_F_FD_DONE) {
		errno = EACCES;
		return (-1);
	}

	if (pe->pe_ch == NULL) {
		if ((pe->pe_ch = malloc(sizeof(PE_CoffHdr))) == NULL) {
			errno = ENOMEM;
			return (-1);
		}
	} else {
		/* Rewrite optional header if `optsize' field changed. */
		if (pe->pe_ch->ch_optsize != ch->ch_optsize)
			pe->pe_flags |= LIBPE_F_DIRTY_OPT_HEADER;
	}

	*pe->pe_ch = *ch;

	pe->pe_flags |= LIBPE_F_DIRTY_COFF_HEADER;

	return (0);
}

int
pe_update_opt_header(PE *pe, PE_OptHdr *oh)
{

	if (pe == NULL || oh == NULL) {
		errno = EINVAL;
		return (-1);
	}

	if (pe->pe_cmd == PE_C_READ || pe->pe_flags & LIBPE_F_FD_DONE) {
		errno = EACCES;
		return (-1);
	}

	if (pe->pe_oh == NULL) {
		if ((pe->pe_oh = malloc(sizeof(PE_OptHdr))) == NULL) {
			errno = ENOMEM;
			return (-1);
		}
	}

	*pe->pe_oh = *oh;

	pe->pe_flags |= LIBPE_F_DIRTY_OPT_HEADER;

	return (0);
}

int
pe_update_data_dir(PE *pe, PE_DataDir *dd)
{

	if (pe == NULL || dd == NULL) {
		errno = EINVAL;
		return (-1);
	}

	if (pe->pe_cmd == PE_C_READ || pe->pe_flags & LIBPE_F_FD_DONE) {
		errno = EACCES;
		return (-1);
	}

	if (pe->pe_dd == NULL) {
		if ((pe->pe_dd = malloc(sizeof(PE_DataDir))) == NULL) {
			errno = ENOMEM;
			return (-1);
		}
	}

	*pe->pe_dd = *dd;

	pe->pe_flags |= LIBPE_F_DIRTY_OPT_HEADER;

	return (0);
}
