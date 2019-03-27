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

#include "_libpe.h"

ELFTC_VCSID("$Id: pe_rich.c 3312 2016-01-10 09:23:51Z kaiwang27 $");

PE_RichHdr *
pe_rich_header(PE *pe)
{

	if (pe == NULL) {
		errno = EINVAL;
		return (NULL);
	}

	if (pe->pe_rh == NULL && pe->pe_stub_ex > 0 &&
	    (pe->pe_flags & LIBPE_F_LOAD_DOS_STUB) == 0) {
		assert((pe->pe_flags & LIBPE_F_SPECIAL_FILE) == 0);
		(void) libpe_read_msdos_stub(pe);
	}

	if (pe->pe_rh == NULL) {
		errno = ENOENT;
		return (NULL);
	}

	return (pe->pe_rh);
}

static uint32_t
rol32(uint32_t n, int c)
{

	c &= 0x1f;

	return ((n << c) | (n >> (0x20 - c)));
}

int
pe_rich_header_validate(PE *pe)
{
	PE_RichHdr *rh;
	uint32_t cksum;
	char *p;
	int i, off;

	if (pe_rich_header(pe) == NULL)
		return (-1);

	assert(pe->pe_rh_start != NULL);

	/*
	 * Initial value of the checksum is the offset to the begin of
	 * the Rich header.
	 */
	cksum = pe->pe_rh_start - pe->pe_stub;

	/*
	 * Add the bytes before the Rich header to the checksum, rotated
	 * left by the offset.
	 */
	for (p = pe->pe_stub; p < pe->pe_rh_start; p++) {
		/* Skip dh_lfanew. */
		off = p - pe->pe_stub;
		if (off >= 0x3c && off < 0x40)
			continue;
		cksum += rol32((unsigned char) *p, off);
	}

	/* Add each compid rotated left by its count to the checksum. */
	rh = pe->pe_rh;
	for (i = 0; (uint32_t) i < rh->rh_total; i++)
		cksum += rol32(rh->rh_compid[i], rh->rh_cnt[i]);

	/* Validate the checksum with the XOR mask stored after "Rich". */
	if (cksum == rh->rh_xor)
		return (1);

	return (0);
}
