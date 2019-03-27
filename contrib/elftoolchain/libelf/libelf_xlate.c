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

#include <assert.h>
#include <libelf.h>

#include "_libelf.h"

ELFTC_VCSID("$Id: libelf_xlate.c 3632 2018-10-10 21:12:43Z jkoshy $");

/*
 * Translate to/from the file representation of ELF objects.
 *
 * Translation could potentially involve the following
 * transformations:
 *
 * - an endianness conversion,
 * - a change of layout, as the file representation of ELF objects
 *   can differ from their in-memory representation.
 * - a change in representation due to a layout version change.
 */

Elf_Data *
_libelf_xlate(Elf_Data *dst, const Elf_Data *src, unsigned int encoding,
    int elfclass, int elfmachine, int direction)
{
	int byteswap;
	size_t cnt, dsz, fsz, msz;
	uintptr_t sb, se, db, de;
	_libelf_translator_function *xlator;

	if (encoding == ELFDATANONE)
		encoding = LIBELF_PRIVATE(byteorder);

	if ((encoding != ELFDATA2LSB && encoding != ELFDATA2MSB) ||
	    dst == NULL || src == NULL || dst == src)	{
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	assert(elfclass == ELFCLASS32 || elfclass == ELFCLASS64);
	assert(direction == ELF_TOFILE || direction == ELF_TOMEMORY);

	if (dst->d_version != src->d_version) {
		LIBELF_SET_ERROR(UNIMPL, 0);
		return (NULL);
	}

	if  (src->d_buf == NULL || dst->d_buf == NULL) {
		LIBELF_SET_ERROR(DATA, 0);
		return (NULL);
	}

	if ((int) src->d_type < 0 || src->d_type >= ELF_T_NUM) {
		LIBELF_SET_ERROR(DATA, 0);
		return (NULL);
	}

	if ((fsz = (elfclass == ELFCLASS32 ? elf32_fsize : elf64_fsize)
	    (src->d_type, (size_t) 1, src->d_version)) == 0)
		return (NULL);

	msz = _libelf_msize(src->d_type, elfclass, src->d_version);

	assert(msz > 0);

	if (src->d_size % (direction == ELF_TOMEMORY ? fsz : msz)) {
		LIBELF_SET_ERROR(DATA, 0);
		return (NULL);
	}

	/*
	 * Determine the number of objects that need to be converted, and
	 * the space required for the converted objects in the destination
	 * buffer.
	 */
	if (direction == ELF_TOMEMORY) {
		cnt = (size_t) src->d_size / fsz;
		dsz = cnt * msz;
	} else {
		cnt = (size_t) src->d_size / msz;
		dsz = cnt * fsz;
	}

	if (dst->d_size  <  dsz) {
		LIBELF_SET_ERROR(DATA, 0);
		return (NULL);
	}

	sb = (uintptr_t) src->d_buf;
	se = sb + (size_t) src->d_size;
	db = (uintptr_t) dst->d_buf;
	de = db + (size_t) dst->d_size;

	/*
	 * Check for overlapping buffers.  Note that db == sb is
	 * allowed.
	 */
	if (db != sb && de > sb && se > db) {
		LIBELF_SET_ERROR(DATA, 0);
		return (NULL);
	}

	if ((direction == ELF_TOMEMORY ? db : sb) %
	    _libelf_malign(src->d_type, elfclass)) {
		LIBELF_SET_ERROR(DATA, 0);
		return (NULL);
	}

	dst->d_type = src->d_type;
	dst->d_size = dsz;

	byteswap = encoding != LIBELF_PRIVATE(byteorder);

	if (src->d_size == 0 ||
	    (db == sb && !byteswap && fsz == msz))
		return (dst);	/* nothing more to do */

	xlator = _libelf_get_translator(src->d_type, direction, elfclass,
	    elfmachine);
	if (!xlator(dst->d_buf, dsz, src->d_buf, cnt, byteswap)) {
		LIBELF_SET_ERROR(DATA, 0);
		return (NULL);
	}

	return (dst);
}
