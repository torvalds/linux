/*-
 * Copyright (c) 2006,2009,2010 Joseph Koshy
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
#include <libelf.h>
#include <stdlib.h>
#include <string.h>

#include "_libelf.h"
#include "_libelf_ar.h"

ELFTC_VCSID("$Id: libelf_ar_util.c 3174 2015-03-27 17:13:41Z emaste $");

/*
 * Convert a string bounded by `start' and `start+sz' (exclusive) to a
 * number in the specified base.
 */
int
_libelf_ar_get_number(const char *src, size_t sz, unsigned int base,
    size_t *ret)
{
	size_t r;
	unsigned int c, v;
	const unsigned char *e, *s;

	assert(base <= 10);

	s = (const unsigned char *) src;
	e = s + sz;

	/* skip leading blanks */
	for (;s < e && (c = *s) == ' '; s++)
		;

	r = 0L;
	for (;s < e; s++) {
		if ((c = *s) == ' ')
			break;
		if (c < '0' || c > '9')
			return (0);
		v = c - '0';
		if (v >= base)		/* Illegal digit. */
			break;
		r *= base;
		r += v;
	}

	*ret = r;

	return (1);
}

/*
 * Return the translated name for an archive member.
 */
char *
_libelf_ar_get_translated_name(const struct ar_hdr *arh, Elf *ar)
{
	char *s;
	unsigned char c;
	size_t len, offset;
	const unsigned char *buf, *p, *q, *r;
	const size_t bufsize = sizeof(arh->ar_name);

	assert(arh != NULL);
	assert(ar->e_kind == ELF_K_AR);
	assert((const unsigned char *) arh >= ar->e_rawfile &&
	    (const unsigned char *) arh < ar->e_rawfile + ar->e_rawsize);

	buf = (const unsigned char *) arh->ar_name;

	/*
	 * Check for extended naming.
	 *
	 * If the name matches the pattern "^/[0-9]+", it is an
	 * SVR4-style extended name.  If the name matches the pattern
	 * "#1/[0-9]+", the entry uses BSD style extended naming.
	 */
	if (buf[0] == '/' && (c = buf[1]) >= '0' && c <= '9') {
		/*
		 * The value in field ar_name is a decimal offset into
		 * the archive string table where the actual name
		 * resides.
		 */
		if (_libelf_ar_get_number((const char *) (buf + 1),
			bufsize - 1, 10, &offset) == 0) {
			LIBELF_SET_ERROR(ARCHIVE, 0);
			return (NULL);
		}

		if (offset > ar->e_u.e_ar.e_rawstrtabsz) {
			LIBELF_SET_ERROR(ARCHIVE, 0);
			return (NULL);
		}

		p = q = ar->e_u.e_ar.e_rawstrtab + offset;
		r = ar->e_u.e_ar.e_rawstrtab + ar->e_u.e_ar.e_rawstrtabsz;

		for (; p < r && *p != '/'; p++)
			;
		len = (size_t) (p - q + 1); /* space for the trailing NUL */

		if ((s = malloc(len)) == NULL) {
			LIBELF_SET_ERROR(RESOURCE, 0);
			return (NULL);
		}

		(void) strncpy(s, (const char *) q, len - 1);
		s[len - 1] = '\0';

		return (s);
	} else if (IS_EXTENDED_BSD_NAME(buf)) {
		r = buf + LIBELF_AR_BSD_EXTENDED_NAME_PREFIX_SIZE;

		if (_libelf_ar_get_number((const char *) r, bufsize -
			LIBELF_AR_BSD_EXTENDED_NAME_PREFIX_SIZE, 10,
			&len) == 0) {
			LIBELF_SET_ERROR(ARCHIVE, 0);
			return (NULL);
		}

		/*
		 * Allocate space for the file name plus a
		 * trailing NUL.
		 */
		if ((s = malloc(len + 1)) == NULL) {
			LIBELF_SET_ERROR(RESOURCE, 0);
			return (NULL);
		}

		/*
		 * The file name follows the archive header.
		 */
		q = (const unsigned char *) (arh + 1);

		(void) strncpy(s, (const char *) q, len);
		s[len] = '\0';

		return (s);
	}

	/*
	 * A 'normal' name.
	 *
	 * Skip back over trailing blanks from the end of the field.
	 * In the SVR4 format, a '/' is used as a terminator for
	 * non-special names.
	 */
	for (q = buf + bufsize - 1; q >= buf && *q == ' '; --q)
		;

	if (q >= buf) {
		if (*q == '/') {
			/*
			 * SVR4 style names: ignore the trailing
			 * character '/', but only if the name is not
			 * one of the special names "/" and "//".
			 */
			if (q > buf + 1 ||
			    (q == (buf + 1) && *buf != '/'))
				q--;
		}

		len = (size_t) (q - buf + 2); /* Space for a trailing NUL. */
	} else {
		/* The buffer only had blanks. */
		buf = (const unsigned char *) "";
		len = 1;
	}

	if ((s = malloc(len)) == NULL) {
		LIBELF_SET_ERROR(RESOURCE, 0);
		return (NULL);
	}

	(void) strncpy(s, (const char *) buf, len - 1);
	s[len - 1] = '\0';

	return (s);
}

/*
 * Return the raw name for an archive member, inclusive of any
 * formatting characters.
 */
char *
_libelf_ar_get_raw_name(const struct ar_hdr *arh)
{
	char *rawname;
	const size_t namesz = sizeof(arh->ar_name);

	if ((rawname = malloc(namesz + 1)) == NULL) {
		LIBELF_SET_ERROR(RESOURCE, 0);
		return (NULL);
	}

	(void) strncpy(rawname, arh->ar_name, namesz);
	rawname[namesz] = '\0';
	return (rawname);
}

/*
 * Open an 'ar' archive.
 */
Elf *
_libelf_ar_open(Elf *e, int reporterror)
{
	size_t sz;
	int scanahead;
	struct ar_hdr arh;
	unsigned char *s, *end;

	_libelf_init_elf(e, ELF_K_AR);

	e->e_u.e_ar.e_nchildren = 0;
	e->e_u.e_ar.e_next = (off_t) -1;

	/*
	 * Look for special members.
	 */

	s = e->e_rawfile + SARMAG;
	end = e->e_rawfile + e->e_rawsize;

	assert(e->e_rawsize > 0);

	/*
	 * We use heuristics to determine the flavor of the archive we
	 * are examining.
	 *
	 * SVR4 flavor archives use the name "/ " and "// " for
	 * special members.
	 *
	 * In BSD flavor archives the symbol table, if present, is the
	 * first archive with name "__.SYMDEF".
	 */

#define	READ_AR_HEADER(S, ARH, SZ, END)					\
	do {								\
		if ((S) + sizeof((ARH)) > (END))			\
		        goto error;					\
		(void) memcpy(&(ARH), (S), sizeof((ARH)));		\
		if ((ARH).ar_fmag[0] != '`' || (ARH).ar_fmag[1] != '\n') \
			goto error;					\
		if (_libelf_ar_get_number((char *) (ARH).ar_size,	\
		    sizeof((ARH).ar_size), 10, &(SZ)) == 0)		\
			goto error;					\
	} while (0)

	READ_AR_HEADER(s, arh, sz, end);

	/*
	 * Handle special archive members for the SVR4 format.
	 */
	if (arh.ar_name[0] == '/') {
		if (sz == 0)
			goto error;

		e->e_flags |= LIBELF_F_AR_VARIANT_SVR4;

		scanahead = 0;

		/*
		 * The symbol table (file name "/ ") always comes before the
		 * string table (file name "// ").
		 */
		if (arh.ar_name[1] == ' ') {
			/* "/ " => symbol table. */
			scanahead = 1;	/* The string table to follow. */

			s += sizeof(arh);
			e->e_u.e_ar.e_rawsymtab = s;
			e->e_u.e_ar.e_rawsymtabsz = sz;

			sz = LIBELF_ADJUST_AR_SIZE(sz);
			s += sz;

		} else if (arh.ar_name[1] == '/' && arh.ar_name[2] == ' ') {
			/* "// " => string table for long file names. */
			s += sizeof(arh);
			e->e_u.e_ar.e_rawstrtab = s;
			e->e_u.e_ar.e_rawstrtabsz = sz;

			sz = LIBELF_ADJUST_AR_SIZE(sz);
			s += sz;
		}

		/*
		 * If the string table hasn't been seen yet, look for
		 * it in the next member.
		 */
		if (scanahead) {
			READ_AR_HEADER(s, arh, sz, end);

			/* "// " => string table for long file names. */
			if (arh.ar_name[0] == '/' && arh.ar_name[1] == '/' &&
			    arh.ar_name[2] == ' ') {

				s += sizeof(arh);

				e->e_u.e_ar.e_rawstrtab = s;
				e->e_u.e_ar.e_rawstrtabsz = sz;

				sz = LIBELF_ADJUST_AR_SIZE(sz);
				s += sz;
			}
		}
	} else if (strncmp(arh.ar_name, LIBELF_AR_BSD_SYMTAB_NAME,
		sizeof(LIBELF_AR_BSD_SYMTAB_NAME) - 1) == 0) {
		/*
		 * BSD style archive symbol table.
		 */
		s += sizeof(arh);
		e->e_u.e_ar.e_rawsymtab = s;
		e->e_u.e_ar.e_rawsymtabsz = sz;

		sz = LIBELF_ADJUST_AR_SIZE(sz);
		s += sz;
	}

	/*
	 * Update the 'next' offset, so that a subsequent elf_begin()
	 * works as expected.
	 */
	e->e_u.e_ar.e_next = (off_t) (s - e->e_rawfile);

	return (e);

error:
	if (!reporterror) {
		e->e_kind = ELF_K_NONE;
		return (e);
	}

	LIBELF_SET_ERROR(ARCHIVE, 0);
	return (NULL);
}
