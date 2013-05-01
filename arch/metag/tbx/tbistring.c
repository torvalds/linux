/*
 * tbistring.c
 *
 * Copyright (C) 2001, 2002, 2003, 2005, 2007, 2012 Imagination Technologies.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 *
 * String table functions provided as part of the thread binary interface for
 * Meta processors
 */

#include <linux/export.h>
#include <linux/string.h>
#include <asm/tbx.h>

/*
 * There are not any functions to modify the string table currently, if these
 * are required at some later point I suggest having a seperate module and
 * ensuring that creating new entries does not interfere with reading old
 * entries in any way.
 */

const TBISTR *__TBIFindStr(const TBISTR *start,
			   const char *str, int match_len)
{
	const TBISTR *search = start;
	bool exact = true;
	const TBISEG *seg;

	if (match_len < 0) {
		/* Make match_len always positive for the inner loop */
		match_len = -match_len;
		exact = false;
	} else {
		/*
		 * Also support historic behaviour, which expected match_len to
		 * include null terminator
		 */
		if (match_len && str[match_len-1] == '\0')
			match_len--;
	}

	if (!search) {
		/* Find global string table segment */
		seg = __TBIFindSeg(NULL, TBID_SEG(TBID_THREAD_GLOBAL,
						  TBID_SEGSCOPE_GLOBAL,
						  TBID_SEGTYPE_STRING));

		if (!seg || seg->Bytes < sizeof(TBISTR))
			/* No string table! */
			return NULL;

		/* Start of string table */
		search = seg->pGAddr;
	}

	for (;;) {
		while (!search->Tag)
			/* Allow simple gaps which are just zero initialised */
			search = (const TBISTR *)((const char *)search + 8);

		if (search->Tag == METAG_TBI_STRE) {
			/* Reached the end of the table */
			search = NULL;
			break;
		}

		if ((search->Len >= match_len) &&
		    (!exact || (search->Len == match_len + 1)) &&
		    (search->Tag != METAG_TBI_STRG)) {
			/* Worth searching */
			if (!strncmp(str, (const char *)search->String,
				     match_len))
				break;
		}

		/* Next entry */
		search = (const TBISTR *)((const char *)search + search->Bytes);
	}

	return search;
}

const void *__TBITransStr(const char *str, int len)
{
	const TBISTR *search = NULL;
	const void *res = NULL;

	for (;;) {
		/* Search onwards */
		search = __TBIFindStr(search, str, len);

		/* No translation returns NULL */
		if (!search)
			break;

		/* Skip matching entries with no translation data */
		if (search->TransLen != METAG_TBI_STRX) {
			/* Calculate base of translation string */
			res = (const char *)search->String +
				((search->Len + 7) & ~7);
			break;
		}

		/* Next entry */
		search = (const TBISTR *)((const char *)search + search->Bytes);
	}

	/* Return base address of translation data or NULL */
	return res;
}
EXPORT_SYMBOL(__TBITransStr);
