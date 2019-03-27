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

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "_libpe.h"

ELFTC_VCSID("$Id: libpe_rich.c 3312 2016-01-10 09:23:51Z kaiwang27 $");

static char *
memfind(char *s, const char *find, size_t slen, size_t flen)
{
	int i;

	if (slen == 0 || flen == 0 || flen > slen)
		return (NULL);

	for (i = 0; (size_t) i <= slen - flen; i++) {
		if (s[i] != find[0])
			continue;
		if (flen == 1)
			return (&s[i]);
		if (memcmp(&s[i + 1], &find[1], flen - 1) == 0)
			return (&s[i]);
	}

	return (NULL);
}

int
libpe_parse_rich_header(PE *pe)
{
	PE_RichHdr *rh;
	char *p, *r, *s;
	uint32_t x;
	int found, i;

	assert(pe->pe_stub != NULL && pe->pe_stub_ex > 0);

	/* Search for the "Rich" keyword to locate the Rich header. */
	s = pe->pe_stub + sizeof(PE_DosHdr);
	r = memfind(s, PE_RICH_TEXT, pe->pe_stub_ex, 4);
	if (r == NULL || r + 8 > s + pe->pe_stub_ex) {
		errno = ENOENT;
		return (-1);
	}

	if ((rh = calloc(1, sizeof(*rh))) == NULL) {
		errno = ENOMEM;
		return (-1);
	}

	rh->rh_xor = le32dec(r + 4); /* Retrieve the "XOR mask" */

	/*
	 * Search for the hidden keyword "DanS" by XOR the dwords before
	 * the "Rich" keyword with the XOR mask.
	 */
	found = 0;
	for (p = r - 4; p >= s; p -= 4) {
		x = le32dec(p) ^ rh->rh_xor;
		if (x == PE_RICH_HIDDEN) {
			found = 1;
			break;
		}
	}
	if (!found) {
		free(rh);
		errno = ENOENT;
		return (-1);
	}

	/*
	 * Found the "DanS" keyword, which is the start of the Rich header.
	 * The next step is to skip the first 16 bytes (DanS, XOR mask,
	 * XOR mask, XOR mask) and read the (compid,cnt) tuples.
	 */
	pe->pe_rh_start = p;
	p += 16;
	rh->rh_total = (r - p) / 8;
	if ((rh->rh_compid = malloc(rh->rh_total * sizeof(*rh->rh_compid))) ==
	    NULL) {
		free(rh);
		errno = ENOMEM;
		return (-1);
	}
	if ((rh->rh_cnt = malloc(rh->rh_total * sizeof(*rh->rh_cnt))) ==
	    NULL) {
		free(rh->rh_compid);
		free(rh);
		errno = ENOMEM;
		return (-1);
	}
	for (i = 0; (uint32_t) i < rh->rh_total; i++, p += 8) {
		rh->rh_compid[i] = le32dec(p) ^ rh->rh_xor;
		rh->rh_cnt[i] = le32dec(p + 4) ^ rh->rh_xor;
	}

	pe->pe_rh = rh;

	return (0);
}
