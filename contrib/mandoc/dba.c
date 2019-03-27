/*	$Id: dba.c,v 1.10 2017/02/17 14:43:54 schwarze Exp $ */
/*
 * Copyright (c) 2016, 2017 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Allocation-based version of the mandoc database, for read-write access.
 * The interface is defined in "dba.h".
 */
#include "config.h"

#include <sys/types.h>
#if HAVE_ENDIAN
#include <endian.h>
#elif HAVE_SYS_ENDIAN
#include <sys/endian.h>
#elif HAVE_NTOHL
#include <arpa/inet.h>
#endif
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mandoc_aux.h"
#include "mandoc_ohash.h"
#include "mansearch.h"
#include "dba_write.h"
#include "dba_array.h"
#include "dba.h"

struct macro_entry {
	struct dba_array	*pages;
	char			 value[];
};

static void	*prepend(const char *, char);
static void	 dba_pages_write(struct dba_array *);
static int	 compare_names(const void *, const void *);
static int	 compare_strings(const void *, const void *);

static struct macro_entry
		*get_macro_entry(struct ohash *, const char *, int32_t);
static void	 dba_macros_write(struct dba_array *);
static void	 dba_macro_write(struct ohash *);
static int	 compare_entries(const void *, const void *);


/*** top-level functions **********************************************/

struct dba *
dba_new(int32_t npages)
{
	struct dba	*dba;
	struct ohash	*macro;
	int32_t		 im;

	dba = mandoc_malloc(sizeof(*dba));
	dba->pages = dba_array_new(npages, DBA_GROW);
	dba->macros = dba_array_new(MACRO_MAX, 0);
	for (im = 0; im < MACRO_MAX; im++) {
		macro = mandoc_malloc(sizeof(*macro));
		mandoc_ohash_init(macro, 4,
		    offsetof(struct macro_entry, value));
		dba_array_set(dba->macros, im, macro);
	}
	return dba;
}

void
dba_free(struct dba *dba)
{
	struct dba_array	*page;
	struct ohash		*macro;
	struct macro_entry	*entry;
	unsigned int		 slot;

	dba_array_FOREACH(dba->macros, macro) {
		for (entry = ohash_first(macro, &slot); entry != NULL;
		     entry = ohash_next(macro, &slot)) {
			dba_array_free(entry->pages);
			free(entry);
		}
		ohash_delete(macro);
		free(macro);
	}
	dba_array_free(dba->macros);

	dba_array_undel(dba->pages);
	dba_array_FOREACH(dba->pages, page) {
		dba_array_free(dba_array_get(page, DBP_NAME));
		dba_array_free(dba_array_get(page, DBP_SECT));
		dba_array_free(dba_array_get(page, DBP_ARCH));
		free(dba_array_get(page, DBP_DESC));
		dba_array_free(dba_array_get(page, DBP_FILE));
		dba_array_free(page);
	}
	dba_array_free(dba->pages);

	free(dba);
}

/*
 * Write the complete mandoc database to disk; the format is:
 * - One integer each for magic and version.
 * - One pointer each to the macros table and to the final magic.
 * - The pages table.
 * - The macros table.
 * - And at the very end, the magic integer again.
 */
int
dba_write(const char *fname, struct dba *dba)
{
	int	 save_errno;
	int32_t	 pos_end, pos_macros, pos_macros_ptr;

	if (dba_open(fname) == -1)
		return -1;
	dba_int_write(MANDOCDB_MAGIC);
	dba_int_write(MANDOCDB_VERSION);
	pos_macros_ptr = dba_skip(1, 2);
	dba_pages_write(dba->pages);
	pos_macros = dba_tell();
	dba_macros_write(dba->macros);
	pos_end = dba_tell();
	dba_int_write(MANDOCDB_MAGIC);
	dba_seek(pos_macros_ptr);
	dba_int_write(pos_macros);
	dba_int_write(pos_end);
	if (dba_close() == -1) {
		save_errno = errno;
		unlink(fname);
		errno = save_errno;
		return -1;
	}
	return 0;
}


/*** functions for handling pages *************************************/

/*
 * Create a new page and append it to the pages table.
 */
struct dba_array *
dba_page_new(struct dba_array *pages, const char *arch,
    const char *desc, const char *file, enum form form)
{
	struct dba_array *page, *entry;

	page = dba_array_new(DBP_MAX, 0);
	entry = dba_array_new(1, DBA_STR | DBA_GROW);
	dba_array_add(page, entry);
	entry = dba_array_new(1, DBA_STR | DBA_GROW);
	dba_array_add(page, entry);
	if (arch != NULL && *arch != '\0') {
		entry = dba_array_new(1, DBA_STR | DBA_GROW);
		dba_array_add(entry, (void *)arch);
	} else
		entry = NULL;
	dba_array_add(page, entry);
	dba_array_add(page, mandoc_strdup(desc));
	entry = dba_array_new(1, DBA_STR | DBA_GROW);
	dba_array_add(entry, prepend(file, form));
	dba_array_add(page, entry);
	dba_array_add(pages, page);
	return page;
}

/*
 * Add a section, architecture, or file name to an existing page.
 * Passing the NULL pointer for the architecture makes the page MI.
 * In that case, any earlier or later architectures are ignored.
 */
void
dba_page_add(struct dba_array *page, int32_t ie, const char *str)
{
	struct dba_array	*entries;
	char			*entry;

	entries = dba_array_get(page, ie);
	if (ie == DBP_ARCH) {
		if (entries == NULL)
			return;
		if (str == NULL || *str == '\0') {
			dba_array_free(entries);
			dba_array_set(page, DBP_ARCH, NULL);
			return;
		}
	}
	if (*str == '\0')
		return;
	dba_array_FOREACH(entries, entry) {
		if (ie == DBP_FILE && *entry < ' ')
			entry++;
		if (strcmp(entry, str) == 0)
			return;
	}
	dba_array_add(entries, (void *)str);
}

/*
 * Add an additional name to an existing page.
 */
void
dba_page_alias(struct dba_array *page, const char *name, uint64_t mask)
{
	struct dba_array	*entries;
	char			*entry;
	char			 maskbyte;

	if (*name == '\0')
		return;
	maskbyte = mask & NAME_MASK;
	entries = dba_array_get(page, DBP_NAME);
	dba_array_FOREACH(entries, entry) {
		if (strcmp(entry + 1, name) == 0) {
			*entry |= maskbyte;
			return;
		}
	}
	dba_array_add(entries, prepend(name, maskbyte));
}

/*
 * Return a pointer to a temporary copy of instr with inbyte prepended.
 */
static void *
prepend(const char *instr, char inbyte)
{
	static char	*outstr = NULL;
	static size_t	 outlen = 0;
	size_t		 newlen;

	newlen = strlen(instr) + 1;
	if (newlen > outlen) {
		outstr = mandoc_realloc(outstr, newlen + 1);
		outlen = newlen;
	}
	*outstr = inbyte;
	memcpy(outstr + 1, instr, newlen);
	return outstr;
}

/*
 * Write the pages table to disk; the format is:
 * - One integer containing the number of pages.
 * - For each page, five pointers to the names, sections,
 *   architectures, description, and file names of the page.
 *   MI pages write 0 instead of the architecture pointer.
 * - One list each for names, sections, architectures, descriptions and
 *   file names.  The description for each page ends with a NUL byte.
 *   For all the other lists, each string ends with a NUL byte,
 *   and the last string for a page ends with two NUL bytes.
 * - To assure alignment of following integers,
 *   the end is padded with NUL bytes up to a multiple of four bytes.
 */
static void
dba_pages_write(struct dba_array *pages)
{
	struct dba_array	*page, *entry;
	int32_t			 pos_pages, pos_end;

	pos_pages = dba_array_writelen(pages, 5);
	dba_array_FOREACH(pages, page) {
		dba_array_setpos(page, DBP_NAME, dba_tell());
		entry = dba_array_get(page, DBP_NAME);
		dba_array_sort(entry, compare_names);
		dba_array_writelst(entry);
	}
	dba_array_FOREACH(pages, page) {
		dba_array_setpos(page, DBP_SECT, dba_tell());
		entry = dba_array_get(page, DBP_SECT);
		dba_array_sort(entry, compare_strings);
		dba_array_writelst(entry);
	}
	dba_array_FOREACH(pages, page) {
		if ((entry = dba_array_get(page, DBP_ARCH)) != NULL) {
			dba_array_setpos(page, DBP_ARCH, dba_tell());
			dba_array_sort(entry, compare_strings);
			dba_array_writelst(entry);
		} else
			dba_array_setpos(page, DBP_ARCH, 0);
	}
	dba_array_FOREACH(pages, page) {
		dba_array_setpos(page, DBP_DESC, dba_tell());
		dba_str_write(dba_array_get(page, DBP_DESC));
	}
	dba_array_FOREACH(pages, page) {
		dba_array_setpos(page, DBP_FILE, dba_tell());
		dba_array_writelst(dba_array_get(page, DBP_FILE));
	}
	pos_end = dba_align();
	dba_seek(pos_pages);
	dba_array_FOREACH(pages, page)
		dba_array_writepos(page);
	dba_seek(pos_end);
}

static int
compare_names(const void *vp1, const void *vp2)
{
	const char	*cp1, *cp2;
	int		 diff;

	cp1 = *(const char * const *)vp1;
	cp2 = *(const char * const *)vp2;
	return (diff = *cp2 - *cp1) ? diff :
	    strcasecmp(cp1 + 1, cp2 + 1);
}

static int
compare_strings(const void *vp1, const void *vp2)
{
	const char	*cp1, *cp2;

	cp1 = *(const char * const *)vp1;
	cp2 = *(const char * const *)vp2;
	return strcmp(cp1, cp2);
}

/*** functions for handling macros ************************************/

/*
 * In the hash table for a single macro, look up an entry by
 * the macro value or add an empty one if it doesn't exist yet.
 */
static struct macro_entry *
get_macro_entry(struct ohash *macro, const char *value, int32_t np)
{
	struct macro_entry	*entry;
	size_t			 len;
	unsigned int		 slot;

	slot = ohash_qlookup(macro, value);
	if ((entry = ohash_find(macro, slot)) == NULL) {
		len = strlen(value) + 1;
		entry = mandoc_malloc(sizeof(*entry) + len);
		memcpy(&entry->value, value, len);
		entry->pages = dba_array_new(np, DBA_GROW);
		ohash_insert(macro, slot, entry);
	}
	return entry;
}

/*
 * In addition to get_macro_entry(), add multiple page references,
 * converting them from the on-disk format (byte offsets in the file)
 * to page pointers in memory.
 */
void
dba_macro_new(struct dba *dba, int32_t im, const char *value,
    const int32_t *pp)
{
	struct macro_entry	*entry;
	const int32_t		*ip;
	int32_t			 np;

	np = 0;
	for (ip = pp; *ip; ip++)
		np++;

	entry = get_macro_entry(dba_array_get(dba->macros, im), value, np);
	for (ip = pp; *ip; ip++)
		dba_array_add(entry->pages, dba_array_get(dba->pages,
		    be32toh(*ip) / 5 / sizeof(*ip) - 1));
}

/*
 * In addition to get_macro_entry(), add one page reference,
 * directly taking the in-memory page pointer as an argument.
 */
void
dba_macro_add(struct dba_array *macros, int32_t im, const char *value,
    struct dba_array *page)
{
	struct macro_entry	*entry;

	if (*value == '\0')
		return;
	entry = get_macro_entry(dba_array_get(macros, im), value, 1);
	dba_array_add(entry->pages, page);
}

/*
 * Write the macros table to disk; the format is:
 * - The number of macro tables (actually, MACRO_MAX).
 * - That number of pointers to the individual macro tables.
 * - The individual macro tables.
 */
static void
dba_macros_write(struct dba_array *macros)
{
	struct ohash		*macro;
	int32_t			 im, pos_macros, pos_end;

	pos_macros = dba_array_writelen(macros, 1);
	im = 0;
	dba_array_FOREACH(macros, macro) {
		dba_array_setpos(macros, im++, dba_tell());
		dba_macro_write(macro);
	}
	pos_end = dba_tell();
	dba_seek(pos_macros);
	dba_array_writepos(macros);
	dba_seek(pos_end);
}

/*
 * Write one individual macro table to disk; the format is:
 * - The number of entries in the table.
 * - For each entry, two pointers, the first one to the value
 *   and the second one to the list of pages.
 * - A list of values, each ending in a NUL byte.
 * - To assure alignment of following integers,
 *   padding with NUL bytes up to a multiple of four bytes.
 * - A list of pointers to pages, each list ending in a 0 integer.
 */
static void
dba_macro_write(struct ohash *macro)
{
	struct macro_entry	**entries, *entry;
	struct dba_array	 *page;
	int32_t			 *kpos, *dpos;
	unsigned int		  ie, ne, slot;
	int			  use;
	int32_t			  addr, pos_macro, pos_end;

	/* Temporary storage for filtering and sorting. */

	ne = ohash_entries(macro);
	entries = mandoc_reallocarray(NULL, ne, sizeof(*entries));
	kpos = mandoc_reallocarray(NULL, ne, sizeof(*kpos));
	dpos = mandoc_reallocarray(NULL, ne, sizeof(*dpos));

	/* Build a list of non-empty entries and sort it. */

	ne = 0;
	for (entry = ohash_first(macro, &slot); entry != NULL;
	     entry = ohash_next(macro, &slot)) {
		use = 0;
		dba_array_FOREACH(entry->pages, page)
			if (dba_array_getpos(page))
				use = 1;
		if (use)
			entries[ne++] = entry;
	}
	qsort(entries, ne, sizeof(*entries), compare_entries);

	/* Number of entries, and space for the pointer pairs. */

	dba_int_write(ne);
	pos_macro = dba_skip(2, ne);

	/* String table. */

	for (ie = 0; ie < ne; ie++) {
		kpos[ie] = dba_tell();
		dba_str_write(entries[ie]->value);
	}
	dba_align();

	/* Pages table. */

	for (ie = 0; ie < ne; ie++) {
		dpos[ie] = dba_tell();
		dba_array_FOREACH(entries[ie]->pages, page)
			if ((addr = dba_array_getpos(page)))
				dba_int_write(addr);
		dba_int_write(0);
	}
	pos_end = dba_tell();

	/* Fill in the pointer pairs. */

	dba_seek(pos_macro);
	for (ie = 0; ie < ne; ie++) {
		dba_int_write(kpos[ie]);
		dba_int_write(dpos[ie]);
	}
	dba_seek(pos_end);

	free(entries);
	free(kpos);
	free(dpos);
}

static int
compare_entries(const void *vp1, const void *vp2)
{
	const struct macro_entry *ep1, *ep2;

	ep1 = *(const struct macro_entry * const *)vp1;
	ep2 = *(const struct macro_entry * const *)vp2;
	return strcmp(ep1->value, ep2->value);
}
