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

#include <sys/param.h>

#include <assert.h>
#include <gelf.h>

#include "_libelf.h"

ELFTC_VCSID("$Id: elf_strptr.c 2990 2014-03-17 09:56:58Z jkoshy $");

/*
 * Convert an ELF section#,offset pair to a string pointer.
 */

char *
elf_strptr(Elf *e, size_t scndx, size_t offset)
{
	Elf_Scn *s;
	Elf_Data *d;
	GElf_Shdr shdr;
	uint64_t alignment, count;

	if (e == NULL || e->e_kind != ELF_K_ELF) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	if ((s = elf_getscn(e, scndx)) == NULL ||
	    gelf_getshdr(s, &shdr) == NULL)
		return (NULL);

	if (shdr.sh_type != SHT_STRTAB ||
	    offset >= shdr.sh_size) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	d = NULL;
	if (e->e_flags & ELF_F_LAYOUT) {

		/*
		 * The application is taking responsibility for the
		 * ELF object's layout, so we can directly translate
		 * an offset to a `char *' address using the `d_off'
		 * members of Elf_Data descriptors.
		 */
		while ((d = elf_getdata(s, d)) != NULL) {

			if (d->d_buf == 0 || d->d_size == 0)
				continue;

			if (d->d_type != ELF_T_BYTE) {
				LIBELF_SET_ERROR(DATA, 0);
				return (NULL);
			}

			if (offset >= d->d_off &&
			    offset < d->d_off + d->d_size)
				return ((char *) d->d_buf + offset - d->d_off);
		}
	} else {
		/*
		 * Otherwise, the `d_off' members are not useable and
		 * we need to compute offsets ourselves, taking into
		 * account 'holes' in coverage of the section introduced
		 * by alignment requirements.
		 */
		count = (uint64_t) 0;	/* cumulative count of bytes seen */
		while ((d = elf_getdata(s, d)) != NULL && count <= offset) {

			if (d->d_buf == NULL || d->d_size == 0)
				continue;

			if (d->d_type != ELF_T_BYTE) {
				LIBELF_SET_ERROR(DATA, 0);
				return (NULL);
			}

			if ((alignment = d->d_align) > 1) {
				if ((alignment & (alignment - 1)) != 0) {
					LIBELF_SET_ERROR(DATA, 0);
					return (NULL);
				}
				count = roundup2(count, alignment);
			}

			if (offset < count) {
				/* offset starts in the 'hole' */
				LIBELF_SET_ERROR(ARGUMENT, 0);
				return (NULL);
			}

			if (offset < count + d->d_size) {
				if (d->d_buf != NULL)
					return ((char *) d->d_buf +
					    offset - count);
				LIBELF_SET_ERROR(DATA, 0);
				return (NULL);
			}

			count += d->d_size;
		}
	}

	LIBELF_SET_ERROR(ARGUMENT, 0);
	return (NULL);
}
