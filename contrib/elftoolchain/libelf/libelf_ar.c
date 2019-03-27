/*-
 * Copyright (c) 2006,2008,2010 Joseph Koshy
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS `AS IS' AND
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
#include <ctype.h>
#include <libelf.h>
#include <stdlib.h>
#include <string.h>

#include "_libelf.h"
#include "_libelf_ar.h"

ELFTC_VCSID("$Id: libelf_ar.c 3446 2016-05-03 01:31:17Z emaste $");

#define	LIBELF_NALLOC_SIZE	16

/*
 * `ar' archive handling.
 *
 * `ar' archives start with signature `ARMAG'.  Each archive member is
 * preceded by a header containing meta-data for the member.  This
 * header is described in <ar.h> (struct ar_hdr).  The header always
 * starts on an even address.  File data is padded with "\n"
 * characters to keep this invariant.
 *
 * Special considerations for `ar' archives:
 *
 * There are two variants of the `ar' archive format: traditional BSD
 * and SVR4.  These differ in the way long file names are treated, and
 * in the layout of the archive symbol table.
 *
 * The `ar' header only has space for a 16 character file name.
 *
 * In the SVR4 format, file names are terminated with a '/', so this
 * effectively leaves 15 characters for the actual file name.  Longer
 * file names stored in a separate 'string table' and referenced
 * indirectly from the name field.  The string table itself appears as
 * an archive member with name "// ".  An `indirect' file name in an
 * `ar' header matches the pattern "/[0-9]*". The digits form a
 * decimal number that corresponds to a byte offset into the string
 * table where the actual file name of the object starts.  Strings in
 * the string table are padded to start on even addresses.
 *
 * In the BSD format, file names can be up to 16 characters.  File
 * names shorter than 16 characters are padded to 16 characters using
 * (ASCII) space characters.  File names with embedded spaces and file
 * names longer than 16 characters are stored immediately after the
 * archive header and the name field set to a special indirect name
 * matching the pattern "#1/[0-9]+".  The digits form a decimal number
 * that corresponds to the actual length of the file name following
 * the archive header.  The content of the archive member immediately
 * follows the file name, and the size field of the archive member
 * holds the sum of the sizes of the member and of the appended file
 * name.
 *
 * Archives may also have a symbol table (see ranlib(1)), mapping
 * program symbols to object files inside the archive.
 *
 * In the SVR4 format, a symbol table uses a file name of "/ " in its
 * archive header.  The symbol table is structured as:
 *  - a 4-byte count of entries stored as a binary value, MSB first
 *  - 'n' 4-byte offsets, stored as binary values, MSB first
 *  - 'n' NUL-terminated strings, for ELF symbol names, stored unpadded.
 *
 * In the BSD format, the symbol table uses a file name of "__.SYMDEF".
 * It is structured as two parts:
 *  - The first part is an array of "ranlib" structures preceded by
 *    the size of the array in bytes.  Each "ranlib" structure
 *    describes one symbol.  Each structure contains an offset into
 *    the string table for the symbol name, and a file offset into the
 *    archive for the member defining the symbol.
 *  - The second part is a string table containing NUL-terminated
 *    strings, preceded by the size of the string table in bytes.
 *
 * If the symbol table and string table are is present in an archive
 * they must be the very first objects and in that order.
 */


/*
 * Retrieve an archive header descriptor.
 */

Elf_Arhdr *
_libelf_ar_gethdr(Elf *e)
{
	Elf *parent;
	Elf_Arhdr *eh;
	char *namelen;
	size_t n, nlen;
	struct ar_hdr *arh;

	if ((parent = e->e_parent) == NULL) {
		LIBELF_SET_ERROR(ARGUMENT, 0);
		return (NULL);
	}

	assert((e->e_flags & LIBELF_F_AR_HEADER) == 0);

	arh = (struct ar_hdr *) (uintptr_t) e->e_hdr.e_rawhdr;

	assert((uintptr_t) arh >= (uintptr_t) parent->e_rawfile + SARMAG);
	assert((uintptr_t) arh <= (uintptr_t) parent->e_rawfile +
	    parent->e_rawsize - sizeof(struct ar_hdr));

	if ((eh = malloc(sizeof(Elf_Arhdr))) == NULL) {
		LIBELF_SET_ERROR(RESOURCE, 0);
		return (NULL);
	}

	e->e_hdr.e_arhdr = eh;
	e->e_flags |= LIBELF_F_AR_HEADER;

	eh->ar_name = eh->ar_rawname = NULL;

	if ((eh->ar_name = _libelf_ar_get_translated_name(arh, parent)) ==
	    NULL)
		goto error;

	if (_libelf_ar_get_number(arh->ar_uid, sizeof(arh->ar_uid), 10,
	    &n) == 0)
		goto error;
	eh->ar_uid = (uid_t) n;

	if (_libelf_ar_get_number(arh->ar_gid, sizeof(arh->ar_gid), 10,
	    &n) == 0)
		goto error;
	eh->ar_gid = (gid_t) n;

	if (_libelf_ar_get_number(arh->ar_mode, sizeof(arh->ar_mode), 8,
	    &n) == 0)
		goto error;
	eh->ar_mode = (mode_t) n;

	if (_libelf_ar_get_number(arh->ar_size, sizeof(arh->ar_size), 10,
	    &n) == 0)
		goto error;

	/*
	 * Get the true size of the member if extended naming is being used.
	 */
	if (IS_EXTENDED_BSD_NAME(arh->ar_name)) {
		namelen = arh->ar_name +
		    LIBELF_AR_BSD_EXTENDED_NAME_PREFIX_SIZE;
		if (_libelf_ar_get_number(namelen, sizeof(arh->ar_name) -
		    LIBELF_AR_BSD_EXTENDED_NAME_PREFIX_SIZE, 10, &nlen) == 0)
			goto error;
		n -= nlen;
	}

	eh->ar_size = n;

	if ((eh->ar_rawname = _libelf_ar_get_raw_name(arh)) == NULL)
		goto error;

	eh->ar_flags = 0;

	return (eh);

 error:
	if (eh) {
		if (eh->ar_name)
			free(eh->ar_name);
		if (eh->ar_rawname)
			free(eh->ar_rawname);
		free(eh);
	}

	e->e_flags &= ~LIBELF_F_AR_HEADER;
	e->e_hdr.e_rawhdr = (unsigned char *) arh;

	return (NULL);
}

Elf *
_libelf_ar_open_member(int fd, Elf_Cmd c, Elf *elf)
{
	Elf *e;
	off_t next;
	size_t nsz, sz;
	struct ar_hdr *arh;
	char *member, *namelen;

	assert(elf->e_kind == ELF_K_AR);

	next = elf->e_u.e_ar.e_next;

	/*
	 * `next' is only set to zero by elf_next() when the last
	 * member of an archive is processed.
	 */
	if (next == (off_t) 0)
		return (NULL);

	assert((next & 1) == 0);

	arh = (struct ar_hdr *) (elf->e_rawfile + next);

	/*
	 * Retrieve the size of the member.
	 */
	if (_libelf_ar_get_number(arh->ar_size, sizeof(arh->ar_size), 10,
	    &sz) == 0) {
		LIBELF_SET_ERROR(ARCHIVE, 0);
		return (NULL);
	}

	/*
	 * Adjust the size field for members in BSD archives using
	 * extended naming.
	 */
	if (IS_EXTENDED_BSD_NAME(arh->ar_name)) {
		namelen = arh->ar_name +
		    LIBELF_AR_BSD_EXTENDED_NAME_PREFIX_SIZE;
		if (_libelf_ar_get_number(namelen, sizeof(arh->ar_name) -
		    LIBELF_AR_BSD_EXTENDED_NAME_PREFIX_SIZE, 10, &nsz) == 0) {
			LIBELF_SET_ERROR(ARCHIVE, 0);
			return (NULL);
		}

		member = (char *) (arh + 1) + nsz;
		sz -= nsz;
	} else
		member = (char *) (arh + 1);


	if ((e = elf_memory(member, sz)) == NULL)
		return (NULL);

	e->e_fd = fd;
	e->e_cmd = c;
	e->e_hdr.e_rawhdr = (unsigned char *) arh;

	elf->e_u.e_ar.e_nchildren++;
	e->e_parent = elf;

	return (e);
}

/*
 * A BSD-style ar(1) symbol table has the following layout:
 *
 * - A count of bytes used by the following array of 'ranlib'
 *   structures, stored as a 'long'.
 * - An array of 'ranlib' structures.  Each array element is
 *   two 'long's in size.
 * - A count of bytes used for the following symbol table.
 * - The symbol table itself.
 */

/*
 * A helper macro to read in a 'long' value from the archive.
 *
 * We use memcpy() since the source pointer may be misaligned with
 * respect to the natural alignment for a C 'long'.
 */
#define	GET_LONG(P, V)do {				\
		memcpy(&(V), (P), sizeof(long));	\
		(P) += sizeof(long);			\
	} while (0)

Elf_Arsym *
_libelf_ar_process_bsd_symtab(Elf *e, size_t *count)
{
	Elf_Arsym *symtab, *sym;
	unsigned int n, nentries;
	unsigned char *end, *p, *p0, *s, *s0;
	const size_t entrysize = 2 * sizeof(long);
	long arraysize, fileoffset, stroffset, strtabsize;

	assert(e != NULL);
	assert(count != NULL);
	assert(e->e_u.e_ar.e_symtab == NULL);

	symtab = NULL;

	/*
	 * The BSD symbol table always contains the count fields even
	 * if there are no entries in it.
	 */
	if (e->e_u.e_ar.e_rawsymtabsz < 2 * sizeof(long))
		goto symtaberror;

	p = p0 = (unsigned char *) e->e_u.e_ar.e_rawsymtab;
	end = p0 + e->e_u.e_ar.e_rawsymtabsz;

	/*
	 * Retrieve the size of the array of ranlib descriptors and
	 * check it for validity.
	 */
	GET_LONG(p, arraysize);

	if (arraysize < 0 || p0 + arraysize >= end ||
	    ((size_t) arraysize % entrysize != 0))
		goto symtaberror;

	/*
	 * Check the value of the string table size.
	 */
	s = p + arraysize;
	GET_LONG(s, strtabsize);

	s0 = s;			/* Start of string table. */
	if (strtabsize < 0 || s0 + strtabsize > end)
		goto symtaberror;

	nentries = (size_t) arraysize / entrysize;

	/*
	 * Allocate space for the returned Elf_Arsym array.
	 */
	if ((symtab = malloc(sizeof(Elf_Arsym) * (nentries + 1))) == NULL) {
		LIBELF_SET_ERROR(RESOURCE, 0);
		return (NULL);
	}

	/* Read in symbol table entries. */
	for (n = 0, sym = symtab; n < nentries; n++, sym++) {
		GET_LONG(p, stroffset);
		GET_LONG(p, fileoffset);

		if (stroffset < 0 || fileoffset <  0 ||
		    (size_t) fileoffset >= e->e_rawsize)
			goto symtaberror;

		s = s0 + stroffset;

		if (s >= end)
			goto symtaberror;

		sym->as_off = (off_t) fileoffset;
		sym->as_hash = elf_hash((char *) s);
		sym->as_name = (char *) s;
	}

	/* Fill up the sentinel entry. */
	sym->as_name = NULL;
	sym->as_hash = ~0UL;
	sym->as_off = (off_t) 0;

	/* Remember the processed symbol table. */
	e->e_u.e_ar.e_symtab = symtab;

	*count = e->e_u.e_ar.e_symtabsz = nentries + 1;

	return (symtab);

symtaberror:
	if (symtab)
		free(symtab);
	LIBELF_SET_ERROR(ARCHIVE, 0);
	return (NULL);
}

/*
 * An SVR4-style ar(1) symbol table has the following layout:
 *
 * - The first 4 bytes are a binary count of the number of entries in the
 *   symbol table, stored MSB-first.
 * - Then there are 'n' 4-byte binary offsets, also stored MSB first.
 * - Following this, there are 'n' null-terminated strings.
 */

#define	GET_WORD(P, V) do {			\
		(V) = 0;			\
		(V) = (P)[0]; (V) <<= 8;	\
		(V) += (P)[1]; (V) <<= 8;	\
		(V) += (P)[2]; (V) <<= 8;	\
		(V) += (P)[3];			\
	} while (0)

#define	INTSZ	4


Elf_Arsym *
_libelf_ar_process_svr4_symtab(Elf *e, size_t *count)
{
	uint32_t off;
	size_t n, nentries;
	Elf_Arsym *symtab, *sym;
	unsigned char *p, *s, *end;

	assert(e != NULL);
	assert(count != NULL);
	assert(e->e_u.e_ar.e_symtab == NULL);

	symtab = NULL;

	if (e->e_u.e_ar.e_rawsymtabsz < INTSZ)
		goto symtaberror;

	p = (unsigned char *) e->e_u.e_ar.e_rawsymtab;
	end = p + e->e_u.e_ar.e_rawsymtabsz;

	GET_WORD(p, nentries);
	p += INTSZ;

	if (nentries == 0 || p + nentries * INTSZ >= end)
		goto symtaberror;

	/* Allocate space for a nentries + a sentinel. */
	if ((symtab = malloc(sizeof(Elf_Arsym) * (nentries+1))) == NULL) {
		LIBELF_SET_ERROR(RESOURCE, 0);
		return (NULL);
	}

	s = p + (nentries * INTSZ); /* start of the string table. */

	for (n = nentries, sym = symtab; n > 0; n--) {
		if (s >= end)
			goto symtaberror;

		GET_WORD(p, off);
		if (off >= e->e_rawsize)
			goto symtaberror;

		sym->as_off = (off_t) off;
		sym->as_hash = elf_hash((char *) s);
		sym->as_name = (char *) s;

		p += INTSZ;
		sym++;

		for (; s < end && *s++ != '\0';) /* skip to next string */
			;
	}

	/* Fill up the sentinel entry. */
	sym->as_name = NULL;
	sym->as_hash = ~0UL;
	sym->as_off = (off_t) 0;

	*count = e->e_u.e_ar.e_symtabsz = nentries + 1;
	e->e_u.e_ar.e_symtab = symtab;

	return (symtab);

symtaberror:
	if (symtab)
		free(symtab);
	LIBELF_SET_ERROR(ARCHIVE, 0);
	return (NULL);
}
