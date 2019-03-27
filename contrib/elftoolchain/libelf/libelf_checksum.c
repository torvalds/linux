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

#include <gelf.h>

#include "_libelf.h"

ELFTC_VCSID("$Id: libelf_checksum.c 3174 2015-03-27 17:13:41Z emaste $");

static unsigned long
_libelf_sum(unsigned long c, const unsigned char *s, size_t size)
{
	if (s == NULL || size == 0)
		return (c);

	while (size--)
		c += *s++;

	return (c);
}

long
_libelf_checksum(Elf *e, int elfclass)
{
	size_t shn;
	Elf_Scn *scn;
	Elf_Data *d;
	unsigned long checksum;
	GElf_Ehdr eh;
	GElf_Shdr shdr;

	if (e == NULL) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (0L);
	}

	if (e->e_class != elfclass) {
		LIBELF_SET_ERROR(CLASS, 0);
		return (0L);
	}

	if (gelf_getehdr(e, &eh) == NULL)
		return (0);

	/*
	 * Iterate over all sections in the ELF file, computing the
	 * checksum along the way.
	 *
	 * The first section is always SHN_UNDEF and can be skipped.
	 * Non-allocatable sections are skipped, as are sections that
	 * could be affected by utilities such as strip(1).
	 */

	checksum = 0;
	for (shn = 1; shn < e->e_u.e_elf.e_nscn; shn++) {
		if ((scn = elf_getscn(e, shn)) == NULL)
			return (0);
		if (gelf_getshdr(scn, &shdr) == NULL)
			return (0);
		if ((shdr.sh_flags & SHF_ALLOC) == 0 ||
		    shdr.sh_type == SHT_DYNAMIC ||
		    shdr.sh_type == SHT_DYNSYM)
			continue;

		d = NULL;
		while ((d = elf_rawdata(scn, d)) != NULL)
			checksum = _libelf_sum(checksum,
			    (unsigned char *) d->d_buf, (size_t) d->d_size);
	}

	/*
	 * Return a 16-bit checksum compatible with Solaris.
	 */
	return (long) (((checksum >> 16) & 0xFFFFUL) + (checksum & 0xFFFFUL));
}
