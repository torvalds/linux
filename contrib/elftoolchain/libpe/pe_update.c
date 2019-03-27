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

#include <assert.h>
#include <errno.h>
#include <unistd.h>

#include "_libpe.h"

ELFTC_VCSID("$Id: pe_update.c 3312 2016-01-10 09:23:51Z kaiwang27 $");

off_t
pe_update(PE *pe)
{
	off_t off;

	if (pe == NULL) {
		errno = EINVAL;
		return (-1);
	}

	if (pe->pe_cmd == PE_C_READ || pe->pe_flags & LIBPE_F_FD_DONE) {
		errno = EACCES;
		return (-1);
	}

	if (pe->pe_cmd == PE_C_RDWR || (pe->pe_cmd == PE_C_WRITE &&
		(pe->pe_flags & LIBPE_F_SPECIAL_FILE) == 0)) {
		if (lseek(pe->pe_fd, 0, SEEK_SET) < 0) {
			errno = EIO;
			return (-1);
		}
	}

	off = 0;

	if (pe->pe_obj == PE_O_PE32 || pe->pe_obj == PE_O_PE32P) {
		if ((off = libpe_write_msdos_stub(pe, off)) < 0)
			return (-1);

		if ((off = libpe_write_pe_header(pe, off)) < 0)
			return (-1);
	}

	if (libpe_resync_sections(pe, off) < 0)
		return (-1);

	if ((off = libpe_write_coff_header(pe, off)) < 0)
		return (-1);

	if ((off = libpe_write_section_headers(pe, off)) < 0)
		return (-1);

	if ((off = libpe_write_sections(pe, off)) < 0)
		return (-1);

	if (ftruncate(pe->pe_fd, off) < 0) {
		errno = EIO;
		return (-1);
	}

	return (off);
}
