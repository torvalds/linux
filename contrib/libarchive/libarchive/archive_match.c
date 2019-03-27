/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * Copyright (c) 2012 Michihiro NAKAJIMA
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "archive_platform.h"
__FBSDID("$FreeBSD$");

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "archive.h"
#include "archive_private.h"
#include "archive_entry.h"
#include "archive_getdate.h"
#include "archive_pathmatch.h"
#include "archive_rb.h"
#include "archive_string.h"

struct match {
	struct match		*next;
	int			 matches;
	struct archive_mstring	 pattern;
};

struct match_list {
	struct match		*first;
	struct match		**last;
	int			 count;
	int			 unmatched_count;
	struct match		*unmatched_next;
	int			 unmatched_eof;
};

struct match_file {
	struct archive_rb_node	 node;
	struct match_file	*next;
	struct archive_mstring	 pathname;
	int			 flag;
	time_t			 mtime_sec;
	long			 mtime_nsec;
	time_t			 ctime_sec;
	long			 ctime_nsec;
};

struct entry_list {
	struct match_file	*first;
	struct match_file	**last;
	int			 count;
};

struct id_array {
	size_t			 size;/* Allocated size */
	size_t			 count;
	int64_t			*ids;
};

#define PATTERN_IS_SET		1
#define TIME_IS_SET		2
#define ID_IS_SET		4

struct archive_match {
	struct archive		 archive;

	/* exclusion/inclusion set flag. */
	int			 setflag;

	/*
	 * Matching filename patterns.
	 */
	struct match_list	 exclusions;
	struct match_list	 inclusions;

	/*
	 * Matching time stamps.
	 */
	time_t			 now;
	int			 newer_mtime_filter;
	time_t			 newer_mtime_sec;
	long			 newer_mtime_nsec;
	int			 newer_ctime_filter;
	time_t			 newer_ctime_sec;
	long			 newer_ctime_nsec;
	int			 older_mtime_filter;
	time_t			 older_mtime_sec;
	long			 older_mtime_nsec;
	int			 older_ctime_filter;
	time_t			 older_ctime_sec;
	long			 older_ctime_nsec;
	/*
	 * Matching time stamps with its filename.
	 */
	struct archive_rb_tree	 exclusion_tree;
	struct entry_list 	 exclusion_entry_list;

	/*
	 * Matching file owners.
	 */
	struct id_array 	 inclusion_uids;
	struct id_array 	 inclusion_gids;
	struct match_list	 inclusion_unames;
	struct match_list	 inclusion_gnames;
};

static int	add_pattern_from_file(struct archive_match *,
		    struct match_list *, int, const void *, int);
static int	add_entry(struct archive_match *, int,
		    struct archive_entry *);
static int	add_owner_id(struct archive_match *, struct id_array *,
		    int64_t);
static int	add_owner_name(struct archive_match *, struct match_list *,
		    int, const void *);
static int	add_pattern_mbs(struct archive_match *, struct match_list *,
		    const char *);
static int	add_pattern_wcs(struct archive_match *, struct match_list *,
		    const wchar_t *);
static int	cmp_key_mbs(const struct archive_rb_node *, const void *);
static int	cmp_key_wcs(const struct archive_rb_node *, const void *);
static int	cmp_node_mbs(const struct archive_rb_node *,
		    const struct archive_rb_node *);
static int	cmp_node_wcs(const struct archive_rb_node *,
		    const struct archive_rb_node *);
static void	entry_list_add(struct entry_list *, struct match_file *);
static void	entry_list_free(struct entry_list *);
static void	entry_list_init(struct entry_list *);
static int	error_nomem(struct archive_match *);
static void	match_list_add(struct match_list *, struct match *);
static void	match_list_free(struct match_list *);
static void	match_list_init(struct match_list *);
static int	match_list_unmatched_inclusions_next(struct archive_match *,
		    struct match_list *, int, const void **);
static int	match_owner_id(struct id_array *, int64_t);
#if !defined(_WIN32) || defined(__CYGWIN__)
static int	match_owner_name_mbs(struct archive_match *,
		    struct match_list *, const char *);
#else
static int	match_owner_name_wcs(struct archive_match *,
		    struct match_list *, const wchar_t *);
#endif
static int	match_path_exclusion(struct archive_match *,
		    struct match *, int, const void *);
static int	match_path_inclusion(struct archive_match *,
		    struct match *, int, const void *);
static int	owner_excluded(struct archive_match *,
		    struct archive_entry *);
static int	path_excluded(struct archive_match *, int, const void *);
static int	set_timefilter(struct archive_match *, int, time_t, long,
		    time_t, long);
static int	set_timefilter_pathname_mbs(struct archive_match *,
		    int, const char *);
static int	set_timefilter_pathname_wcs(struct archive_match *,
		    int, const wchar_t *);
static int	set_timefilter_date(struct archive_match *, int, const char *);
static int	set_timefilter_date_w(struct archive_match *, int,
		    const wchar_t *);
static int	time_excluded(struct archive_match *,
		    struct archive_entry *);
static int	validate_time_flag(struct archive *, int, const char *);

#define get_date __archive_get_date

static const struct archive_rb_tree_ops rb_ops_mbs = {
	cmp_node_mbs, cmp_key_mbs
};

static const struct archive_rb_tree_ops rb_ops_wcs = {
	cmp_node_wcs, cmp_key_wcs
};

/*
 * The matching logic here needs to be re-thought.  I started out to
 * try to mimic gtar's matching logic, but it's not entirely
 * consistent.  In particular 'tar -t' and 'tar -x' interpret patterns
 * on the command line as anchored, but --exclude doesn't.
 */

static int
error_nomem(struct archive_match *a)
{
	archive_set_error(&(a->archive), ENOMEM, "No memory");
	a->archive.state = ARCHIVE_STATE_FATAL;
	return (ARCHIVE_FATAL);
}

/*
 * Create an ARCHIVE_MATCH object.
 */
struct archive *
archive_match_new(void)
{
	struct archive_match *a;

	a = (struct archive_match *)calloc(1, sizeof(*a));
	if (a == NULL)
		return (NULL);
	a->archive.magic = ARCHIVE_MATCH_MAGIC;
	a->archive.state = ARCHIVE_STATE_NEW;
	match_list_init(&(a->inclusions));
	match_list_init(&(a->exclusions));
	__archive_rb_tree_init(&(a->exclusion_tree), &rb_ops_mbs);
	entry_list_init(&(a->exclusion_entry_list));
	match_list_init(&(a->inclusion_unames));
	match_list_init(&(a->inclusion_gnames));
	time(&a->now);
	return (&(a->archive));
}

/*
 * Free an ARCHIVE_MATCH object.
 */
int
archive_match_free(struct archive *_a)
{
	struct archive_match *a;

	if (_a == NULL)
		return (ARCHIVE_OK);
	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_ANY | ARCHIVE_STATE_FATAL, "archive_match_free");
	a = (struct archive_match *)_a;
	match_list_free(&(a->inclusions));
	match_list_free(&(a->exclusions));
	entry_list_free(&(a->exclusion_entry_list));
	free(a->inclusion_uids.ids);
	free(a->inclusion_gids.ids);
	match_list_free(&(a->inclusion_unames));
	match_list_free(&(a->inclusion_gnames));
	free(a);
	return (ARCHIVE_OK);
}

/*
 * Convenience function to perform all exclusion tests.
 *
 * Returns 1 if archive entry is excluded.
 * Returns 0 if archive entry is not excluded.
 * Returns <0 if something error happened.
 */
int
archive_match_excluded(struct archive *_a, struct archive_entry *entry)
{
	struct archive_match *a;
	int r;

	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_match_excluded_ae");

	a = (struct archive_match *)_a;
	if (entry == NULL) {
		archive_set_error(&(a->archive), EINVAL, "entry is NULL");
		return (ARCHIVE_FAILED);
	}

	r = 0;
	if (a->setflag & PATTERN_IS_SET) {
#if defined(_WIN32) && !defined(__CYGWIN__)
		r = path_excluded(a, 0, archive_entry_pathname_w(entry));
#else
		r = path_excluded(a, 1, archive_entry_pathname(entry));
#endif
		if (r != 0)
			return (r);
	}

	if (a->setflag & TIME_IS_SET) {
		r = time_excluded(a, entry);
		if (r != 0)
			return (r);
	}

	if (a->setflag & ID_IS_SET)
		r = owner_excluded(a, entry);
	return (r);
}

/*
 * Utility functions to manage exclusion/inclusion patterns
 */

int
archive_match_exclude_pattern(struct archive *_a, const char *pattern)
{
	struct archive_match *a;
	int r;

	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_match_exclude_pattern");
	a = (struct archive_match *)_a;

	if (pattern == NULL || *pattern == '\0') {
		archive_set_error(&(a->archive), EINVAL, "pattern is empty");
		return (ARCHIVE_FAILED);
	}
	if ((r = add_pattern_mbs(a, &(a->exclusions), pattern)) != ARCHIVE_OK)
		return (r);
	return (ARCHIVE_OK);
}

int
archive_match_exclude_pattern_w(struct archive *_a, const wchar_t *pattern)
{
	struct archive_match *a;
	int r;

	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_match_exclude_pattern_w");
	a = (struct archive_match *)_a;

	if (pattern == NULL || *pattern == L'\0') {
		archive_set_error(&(a->archive), EINVAL, "pattern is empty");
		return (ARCHIVE_FAILED);
	}
	if ((r = add_pattern_wcs(a, &(a->exclusions), pattern)) != ARCHIVE_OK)
		return (r);
	return (ARCHIVE_OK);
}

int
archive_match_exclude_pattern_from_file(struct archive *_a,
    const char *pathname, int nullSeparator)
{
	struct archive_match *a;

	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_match_exclude_pattern_from_file");
	a = (struct archive_match *)_a;

	return add_pattern_from_file(a, &(a->exclusions), 1, pathname,
		nullSeparator);
}

int
archive_match_exclude_pattern_from_file_w(struct archive *_a,
    const wchar_t *pathname, int nullSeparator)
{
	struct archive_match *a;

	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_match_exclude_pattern_from_file_w");
	a = (struct archive_match *)_a;

	return add_pattern_from_file(a, &(a->exclusions), 0, pathname,
		nullSeparator);
}

int
archive_match_include_pattern(struct archive *_a, const char *pattern)
{
	struct archive_match *a;
	int r;

	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_match_include_pattern");
	a = (struct archive_match *)_a;

	if (pattern == NULL || *pattern == '\0') {
		archive_set_error(&(a->archive), EINVAL, "pattern is empty");
		return (ARCHIVE_FAILED);
	}
	if ((r = add_pattern_mbs(a, &(a->inclusions), pattern)) != ARCHIVE_OK)
		return (r);
	return (ARCHIVE_OK);
}

int
archive_match_include_pattern_w(struct archive *_a, const wchar_t *pattern)
{
	struct archive_match *a;
	int r;

	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_match_include_pattern_w");
	a = (struct archive_match *)_a;

	if (pattern == NULL || *pattern == L'\0') {
		archive_set_error(&(a->archive), EINVAL, "pattern is empty");
		return (ARCHIVE_FAILED);
	}
	if ((r = add_pattern_wcs(a, &(a->inclusions), pattern)) != ARCHIVE_OK)
		return (r);
	return (ARCHIVE_OK);
}

int
archive_match_include_pattern_from_file(struct archive *_a,
    const char *pathname, int nullSeparator)
{
	struct archive_match *a;

	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_match_include_pattern_from_file");
	a = (struct archive_match *)_a;

	return add_pattern_from_file(a, &(a->inclusions), 1, pathname,
		nullSeparator);
}

int
archive_match_include_pattern_from_file_w(struct archive *_a,
    const wchar_t *pathname, int nullSeparator)
{
	struct archive_match *a;

	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_match_include_pattern_from_file_w");
	a = (struct archive_match *)_a;

	return add_pattern_from_file(a, &(a->inclusions), 0, pathname,
		nullSeparator);
}

/*
 * Test functions for pathname patterns.
 *
 * Returns 1 if archive entry is excluded.
 * Returns 0 if archive entry is not excluded.
 * Returns <0 if something error happened.
 */
int
archive_match_path_excluded(struct archive *_a,
    struct archive_entry *entry)
{
	struct archive_match *a;

	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_match_path_excluded");

	a = (struct archive_match *)_a;
	if (entry == NULL) {
		archive_set_error(&(a->archive), EINVAL, "entry is NULL");
		return (ARCHIVE_FAILED);
	}

	/* If we don't have exclusion/inclusion pattern set at all,
	 * the entry is always not excluded. */
	if ((a->setflag & PATTERN_IS_SET) == 0)
		return (0);
#if defined(_WIN32) && !defined(__CYGWIN__)
	return (path_excluded(a, 0, archive_entry_pathname_w(entry)));
#else
	return (path_excluded(a, 1, archive_entry_pathname(entry)));
#endif
}

/*
 * Utility functions to get statistic information for inclusion patterns.
 */
int
archive_match_path_unmatched_inclusions(struct archive *_a)
{
	struct archive_match *a;

	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_match_unmatched_inclusions");
	a = (struct archive_match *)_a;

	return (a->inclusions.unmatched_count);
}

int
archive_match_path_unmatched_inclusions_next(struct archive *_a,
    const char **_p)
{
	struct archive_match *a;
	const void *v;
	int r;

	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_match_unmatched_inclusions_next");
	a = (struct archive_match *)_a;

	r = match_list_unmatched_inclusions_next(a, &(a->inclusions), 1, &v);
	*_p = (const char *)v;
	return (r);
}

int
archive_match_path_unmatched_inclusions_next_w(struct archive *_a,
    const wchar_t **_p)
{
	struct archive_match *a;
	const void *v;
	int r;

	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_match_unmatched_inclusions_next_w");
	a = (struct archive_match *)_a;

	r = match_list_unmatched_inclusions_next(a, &(a->inclusions), 0, &v);
	*_p = (const wchar_t *)v;
	return (r);
}

/*
 * Add inclusion/exclusion patterns.
 */
static int
add_pattern_mbs(struct archive_match *a, struct match_list *list,
    const char *pattern)
{
	struct match *match;
	size_t len;

	match = calloc(1, sizeof(*match));
	if (match == NULL)
		return (error_nomem(a));
	/* Both "foo/" and "foo" should match "foo/bar". */
	len = strlen(pattern);
	if (len && pattern[len - 1] == '/')
		--len;
	archive_mstring_copy_mbs_len(&(match->pattern), pattern, len);
	match_list_add(list, match);
	a->setflag |= PATTERN_IS_SET;
	return (ARCHIVE_OK);
}

static int
add_pattern_wcs(struct archive_match *a, struct match_list *list,
    const wchar_t *pattern)
{
	struct match *match;
	size_t len;

	match = calloc(1, sizeof(*match));
	if (match == NULL)
		return (error_nomem(a));
	/* Both "foo/" and "foo" should match "foo/bar". */
	len = wcslen(pattern);
	if (len && pattern[len - 1] == L'/')
		--len;
	archive_mstring_copy_wcs_len(&(match->pattern), pattern, len);
	match_list_add(list, match);
	a->setflag |= PATTERN_IS_SET;
	return (ARCHIVE_OK);
}

static int
add_pattern_from_file(struct archive_match *a, struct match_list *mlist,
    int mbs, const void *pathname, int nullSeparator)
{
	struct archive *ar;
	struct archive_entry *ae;
	struct archive_string as;
	const void *buff;
	size_t size;
	int64_t offset;
	int r;

	ar = archive_read_new(); 
	if (ar == NULL) {
		archive_set_error(&(a->archive), ENOMEM, "No memory");
		return (ARCHIVE_FATAL);
	}
	r = archive_read_support_format_raw(ar);
	r = archive_read_support_format_empty(ar);
	if (r != ARCHIVE_OK) {
		archive_copy_error(&(a->archive), ar);
		archive_read_free(ar);
		return (r);
	}
	if (mbs)
		r = archive_read_open_filename(ar, pathname, 512*20);
	else
		r = archive_read_open_filename_w(ar, pathname, 512*20);
	if (r != ARCHIVE_OK) {
		archive_copy_error(&(a->archive), ar);
		archive_read_free(ar);
		return (r);
	}
	r = archive_read_next_header(ar, &ae);
	if (r != ARCHIVE_OK) {
		archive_read_free(ar);
		if (r == ARCHIVE_EOF) {
			return (ARCHIVE_OK);
		} else {
			archive_copy_error(&(a->archive), ar);
			return (r);
		}
	}

	archive_string_init(&as);

	while ((r = archive_read_data_block(ar, &buff, &size, &offset))
	    == ARCHIVE_OK) {
		const char *b = (const char *)buff;

		while (size) {
			const char *s = (const char *)b;
			size_t length = 0;
			int found_separator = 0;

			while (length < size) {
				if (nullSeparator) {
					if (*b == '\0') {
						found_separator = 1;
						break;
					}
				} else {
			            	if (*b == 0x0d || *b == 0x0a) {
						found_separator = 1;
						break;
					}
				}
				b++;
				length++;
			}
			if (!found_separator) {
				archive_strncat(&as, s, length);
				/* Read next data block. */
				break;
			}
			b++;
			size -= length + 1;
			archive_strncat(&as, s, length);

			/* If the line is not empty, add the pattern. */
			if (archive_strlen(&as) > 0) {
				/* Add pattern. */
				r = add_pattern_mbs(a, mlist, as.s);
				if (r != ARCHIVE_OK) {
					archive_read_free(ar);
					archive_string_free(&as);
					return (r);
				}
				archive_string_empty(&as);
			}
		}
	}

	/* If an error occurred, report it immediately. */
	if (r < ARCHIVE_OK) {
		archive_copy_error(&(a->archive), ar);
		archive_read_free(ar);
		archive_string_free(&as);
		return (r);
	}

	/* If the line is not empty, add the pattern. */
	if (r == ARCHIVE_EOF && archive_strlen(&as) > 0) {
		/* Add pattern. */
		r = add_pattern_mbs(a, mlist, as.s);
		if (r != ARCHIVE_OK) {
			archive_read_free(ar);
			archive_string_free(&as);
			return (r);
		}
	}
	archive_read_free(ar);
	archive_string_free(&as);
	return (ARCHIVE_OK);
}

/*
 * Test if pathname is excluded by inclusion/exclusion patterns.
 */
static int
path_excluded(struct archive_match *a, int mbs, const void *pathname)
{
	struct match *match;
	struct match *matched;
	int r;

	if (a == NULL)
		return (0);

	/* Mark off any unmatched inclusions. */
	/* In particular, if a filename does appear in the archive and
	 * is explicitly included and excluded, then we don't report
	 * it as missing even though we don't extract it.
	 */
	matched = NULL;
	for (match = a->inclusions.first; match != NULL;
	    match = match->next){
		if (match->matches == 0 &&
		    (r = match_path_inclusion(a, match, mbs, pathname)) != 0) {
			if (r < 0)
				return (r);
			a->inclusions.unmatched_count--;
			match->matches++;
			matched = match;
		}
	}

	/* Exclusions take priority */
	for (match = a->exclusions.first; match != NULL;
	    match = match->next){
		r = match_path_exclusion(a, match, mbs, pathname);
		if (r)
			return (r);
	}

	/* It's not excluded and we found an inclusion above, so it's
	 * included. */
	if (matched != NULL)
		return (0);


	/* We didn't find an unmatched inclusion, check the remaining ones. */
	for (match = a->inclusions.first; match != NULL;
	    match = match->next){
		/* We looked at previously-unmatched inclusions already. */
		if (match->matches > 0 &&
		    (r = match_path_inclusion(a, match, mbs, pathname)) != 0) {
			if (r < 0)
				return (r);
			match->matches++;
			return (0);
		}
	}

	/* If there were inclusions, default is to exclude. */
	if (a->inclusions.first != NULL)
	    return (1);

	/* No explicit inclusions, default is to match. */
	return (0);
}

/*
 * This is a little odd, but it matches the default behavior of
 * gtar.  In particular, 'a*b' will match 'foo/a1111/222b/bar'
 *
 */
static int
match_path_exclusion(struct archive_match *a, struct match *m,
    int mbs, const void *pn)
{
	int flag = PATHMATCH_NO_ANCHOR_START | PATHMATCH_NO_ANCHOR_END;
	int r;

	if (mbs) {
		const char *p;
		r = archive_mstring_get_mbs(&(a->archive), &(m->pattern), &p);
		if (r == 0)
			return (archive_pathmatch(p, (const char *)pn, flag));
	} else {
		const wchar_t *p;
		r = archive_mstring_get_wcs(&(a->archive), &(m->pattern), &p);
		if (r == 0)
			return (archive_pathmatch_w(p, (const wchar_t *)pn,
				flag));
	}
	if (errno == ENOMEM)
		return (error_nomem(a));
	return (0);
}

/*
 * Again, mimic gtar:  inclusions are always anchored (have to match
 * the beginning of the path) even though exclusions are not anchored.
 */
static int
match_path_inclusion(struct archive_match *a, struct match *m,
    int mbs, const void *pn)
{
	int flag = PATHMATCH_NO_ANCHOR_END;
	int r;

	if (mbs) {
		const char *p;
		r = archive_mstring_get_mbs(&(a->archive), &(m->pattern), &p);
		if (r == 0)
			return (archive_pathmatch(p, (const char *)pn, flag));
	} else {
		const wchar_t *p;
		r = archive_mstring_get_wcs(&(a->archive), &(m->pattern), &p);
		if (r == 0)
			return (archive_pathmatch_w(p, (const wchar_t *)pn,
				flag));
	}
	if (errno == ENOMEM)
		return (error_nomem(a));
	return (0);
}

static void
match_list_init(struct match_list *list)
{
	list->first = NULL;
	list->last = &(list->first);
	list->count = 0;
}

static void
match_list_free(struct match_list *list)
{
	struct match *p, *q;

	for (p = list->first; p != NULL; ) {
		q = p;
		p = p->next;
		archive_mstring_clean(&(q->pattern));
		free(q);
	}
}

static void
match_list_add(struct match_list *list, struct match *m)
{
	*list->last = m;
	list->last = &(m->next);
	list->count++;
	list->unmatched_count++;
}

static int
match_list_unmatched_inclusions_next(struct archive_match *a,
    struct match_list *list, int mbs, const void **vp)
{
	struct match *m;

	*vp = NULL;
	if (list->unmatched_eof) {
		list->unmatched_eof = 0;
		return (ARCHIVE_EOF);
	}
	if (list->unmatched_next == NULL) {
		if (list->unmatched_count == 0)
			return (ARCHIVE_EOF);
		list->unmatched_next = list->first;
	}

	for (m = list->unmatched_next; m != NULL; m = m->next) {
		int r;

		if (m->matches)
			continue;
		if (mbs) {
			const char *p;
			r = archive_mstring_get_mbs(&(a->archive),
				&(m->pattern), &p);
			if (r < 0 && errno == ENOMEM)
				return (error_nomem(a));
			if (p == NULL)
				p = "";
			*vp = p;
		} else {
			const wchar_t *p;
			r = archive_mstring_get_wcs(&(a->archive),
				&(m->pattern), &p);
			if (r < 0 && errno == ENOMEM)
				return (error_nomem(a));
			if (p == NULL)
				p = L"";
			*vp = p;
		}
		list->unmatched_next = m->next;
		if (list->unmatched_next == NULL)
			/* To return EOF next time. */
			list->unmatched_eof = 1;
		return (ARCHIVE_OK);
	}
	list->unmatched_next = NULL;
	return (ARCHIVE_EOF);
}

/*
 * Utility functions to manage inclusion timestamps.
 */
int
archive_match_include_time(struct archive *_a, int flag, time_t sec,
    long nsec)
{
	int r;

	r = validate_time_flag(_a, flag, "archive_match_include_time");
	if (r != ARCHIVE_OK)
		return (r);
	return set_timefilter((struct archive_match *)_a, flag,
			sec, nsec, sec, nsec);
}

int
archive_match_include_date(struct archive *_a, int flag,
    const char *datestr)
{
	int r;

	r = validate_time_flag(_a, flag, "archive_match_include_date");
	if (r != ARCHIVE_OK)
		return (r);
	return set_timefilter_date((struct archive_match *)_a, flag, datestr);
}

int
archive_match_include_date_w(struct archive *_a, int flag,
    const wchar_t *datestr)
{
	int r;

	r = validate_time_flag(_a, flag, "archive_match_include_date_w");
	if (r != ARCHIVE_OK)
		return (r);

	return set_timefilter_date_w((struct archive_match *)_a, flag, datestr);
}

int
archive_match_include_file_time(struct archive *_a, int flag,
    const char *pathname)
{
	int r;

	r = validate_time_flag(_a, flag, "archive_match_include_file_time");
	if (r != ARCHIVE_OK)
		return (r);
	return set_timefilter_pathname_mbs((struct archive_match *)_a,
			flag, pathname);
}

int
archive_match_include_file_time_w(struct archive *_a, int flag,
    const wchar_t *pathname)
{
	int r;

	r = validate_time_flag(_a, flag, "archive_match_include_file_time_w");
	if (r != ARCHIVE_OK)
		return (r);
	return set_timefilter_pathname_wcs((struct archive_match *)_a,
			flag, pathname);
}

int
archive_match_exclude_entry(struct archive *_a, int flag,
    struct archive_entry *entry)
{
	struct archive_match *a;
	int r;

	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_match_time_include_entry");
	a = (struct archive_match *)_a;

	if (entry == NULL) {
		archive_set_error(&(a->archive), EINVAL, "entry is NULL");
		return (ARCHIVE_FAILED);
	}
	r = validate_time_flag(_a, flag, "archive_match_exclude_entry");
	if (r != ARCHIVE_OK)
		return (r);
	return (add_entry(a, flag, entry));
}

/*
 * Test function for time stamps.
 *
 * Returns 1 if archive entry is excluded.
 * Returns 0 if archive entry is not excluded.
 * Returns <0 if something error happened.
 */
int
archive_match_time_excluded(struct archive *_a,
    struct archive_entry *entry)
{
	struct archive_match *a;

	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_match_time_excluded_ae");

	a = (struct archive_match *)_a;
	if (entry == NULL) {
		archive_set_error(&(a->archive), EINVAL, "entry is NULL");
		return (ARCHIVE_FAILED);
	}

	/* If we don't have inclusion time set at all, the entry is always
	 * not excluded. */
	if ((a->setflag & TIME_IS_SET) == 0)
		return (0);
	return (time_excluded(a, entry));
}

static int
validate_time_flag(struct archive *_a, int flag, const char *_fn)
{
	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_NEW, _fn);

	/* Check a type of time. */
	if (flag &
	   ((~(ARCHIVE_MATCH_MTIME | ARCHIVE_MATCH_CTIME)) & 0xff00)) {
		archive_set_error(_a, EINVAL, "Invalid time flag");
		return (ARCHIVE_FAILED);
	}
	if ((flag & (ARCHIVE_MATCH_MTIME | ARCHIVE_MATCH_CTIME)) == 0) {
		archive_set_error(_a, EINVAL, "No time flag");
		return (ARCHIVE_FAILED);
	}

	/* Check a type of comparison. */
	if (flag &
	   ((~(ARCHIVE_MATCH_NEWER | ARCHIVE_MATCH_OLDER
			| ARCHIVE_MATCH_EQUAL)) & 0x00ff)) {
		archive_set_error(_a, EINVAL, "Invalid comparison flag");
		return (ARCHIVE_FAILED);
	}
	if ((flag & (ARCHIVE_MATCH_NEWER | ARCHIVE_MATCH_OLDER
	    | ARCHIVE_MATCH_EQUAL)) == 0) {
		archive_set_error(_a, EINVAL, "No comparison flag");
		return (ARCHIVE_FAILED);
	}

	return (ARCHIVE_OK);
}

#define JUST_EQUAL(t) (((t) &  (ARCHIVE_MATCH_EQUAL |\
	ARCHIVE_MATCH_NEWER | ARCHIVE_MATCH_OLDER)) == ARCHIVE_MATCH_EQUAL)
static int
set_timefilter(struct archive_match *a, int timetype,
    time_t mtime_sec, long mtime_nsec, time_t ctime_sec, long ctime_nsec)
{
	if (timetype & ARCHIVE_MATCH_MTIME) {
		if ((timetype & ARCHIVE_MATCH_NEWER) || JUST_EQUAL(timetype)) {
			a->newer_mtime_filter = timetype;
			a->newer_mtime_sec = mtime_sec;
			a->newer_mtime_nsec = mtime_nsec;
			a->setflag |= TIME_IS_SET;
		}
		if ((timetype & ARCHIVE_MATCH_OLDER) || JUST_EQUAL(timetype)) {
			a->older_mtime_filter = timetype;
			a->older_mtime_sec = mtime_sec;
			a->older_mtime_nsec = mtime_nsec;
			a->setflag |= TIME_IS_SET;
		}
	}
	if (timetype & ARCHIVE_MATCH_CTIME) {
		if ((timetype & ARCHIVE_MATCH_NEWER) || JUST_EQUAL(timetype)) {
			a->newer_ctime_filter = timetype;
			a->newer_ctime_sec = ctime_sec;
			a->newer_ctime_nsec = ctime_nsec;
			a->setflag |= TIME_IS_SET;
		}
		if ((timetype & ARCHIVE_MATCH_OLDER) || JUST_EQUAL(timetype)) {
			a->older_ctime_filter = timetype;
			a->older_ctime_sec = ctime_sec;
			a->older_ctime_nsec = ctime_nsec;
			a->setflag |= TIME_IS_SET;
		}
	}
	return (ARCHIVE_OK);
}

static int
set_timefilter_date(struct archive_match *a, int timetype, const char *datestr)
{
	time_t t;

	if (datestr == NULL || *datestr == '\0') {
		archive_set_error(&(a->archive), EINVAL, "date is empty");
		return (ARCHIVE_FAILED);
	}
	t = get_date(a->now, datestr);
	if (t == (time_t)-1) {
		archive_set_error(&(a->archive), EINVAL, "invalid date string");
		return (ARCHIVE_FAILED);
	}
	return set_timefilter(a, timetype, t, 0, t, 0);
}

static int
set_timefilter_date_w(struct archive_match *a, int timetype,
    const wchar_t *datestr)
{
	struct archive_string as;
	time_t t;

	if (datestr == NULL || *datestr == L'\0') {
		archive_set_error(&(a->archive), EINVAL, "date is empty");
		return (ARCHIVE_FAILED);
	}

	archive_string_init(&as);
	if (archive_string_append_from_wcs(&as, datestr, wcslen(datestr)) < 0) {
		archive_string_free(&as);
		if (errno == ENOMEM)
			return (error_nomem(a));
		archive_set_error(&(a->archive), -1,
		    "Failed to convert WCS to MBS");
		return (ARCHIVE_FAILED);
	}
	t = get_date(a->now, as.s);
	archive_string_free(&as);
	if (t == (time_t)-1) {
		archive_set_error(&(a->archive), EINVAL, "invalid date string");
		return (ARCHIVE_FAILED);
	}
	return set_timefilter(a, timetype, t, 0, t, 0);
}

#if defined(_WIN32) && !defined(__CYGWIN__)
#define EPOC_TIME ARCHIVE_LITERAL_ULL(116444736000000000)
static int
set_timefilter_find_data(struct archive_match *a, int timetype,
    DWORD ftLastWriteTime_dwHighDateTime, DWORD ftLastWriteTime_dwLowDateTime,
    DWORD ftCreationTime_dwHighDateTime, DWORD ftCreationTime_dwLowDateTime)
{
	ULARGE_INTEGER utc;
	time_t ctime_sec, mtime_sec;
	long ctime_ns, mtime_ns;

	utc.HighPart = ftCreationTime_dwHighDateTime;
	utc.LowPart = ftCreationTime_dwLowDateTime;
	if (utc.QuadPart >= EPOC_TIME) {
		utc.QuadPart -= EPOC_TIME;
		ctime_sec = (time_t)(utc.QuadPart / 10000000);
		ctime_ns = (long)(utc.QuadPart % 10000000) * 100;
	} else {
		ctime_sec = 0;
		ctime_ns = 0;
	}
	utc.HighPart = ftLastWriteTime_dwHighDateTime;
	utc.LowPart = ftLastWriteTime_dwLowDateTime;
	if (utc.QuadPart >= EPOC_TIME) {
		utc.QuadPart -= EPOC_TIME;
		mtime_sec = (time_t)(utc.QuadPart / 10000000);
		mtime_ns = (long)(utc.QuadPart % 10000000) * 100;
	} else {
		mtime_sec = 0;
		mtime_ns = 0;
	}
	return set_timefilter(a, timetype,
			mtime_sec, mtime_ns, ctime_sec, ctime_ns);
}

static int
set_timefilter_pathname_mbs(struct archive_match *a, int timetype,
    const char *path)
{
	/* NOTE: stat() on Windows cannot handle nano seconds. */
	HANDLE h;
	WIN32_FIND_DATAA d;

	if (path == NULL || *path == '\0') {
		archive_set_error(&(a->archive), EINVAL, "pathname is empty");
		return (ARCHIVE_FAILED);
	}
	h = FindFirstFileA(path, &d);
	if (h == INVALID_HANDLE_VALUE) {
		la_dosmaperr(GetLastError());
		archive_set_error(&(a->archive), errno,
		    "Failed to FindFirstFileA");
		return (ARCHIVE_FAILED);
	}
	FindClose(h);
	return set_timefilter_find_data(a, timetype,
	    d.ftLastWriteTime.dwHighDateTime, d.ftLastWriteTime.dwLowDateTime,
	    d.ftCreationTime.dwHighDateTime, d.ftCreationTime.dwLowDateTime);
}

static int
set_timefilter_pathname_wcs(struct archive_match *a, int timetype,
    const wchar_t *path)
{
	HANDLE h;
	WIN32_FIND_DATAW d;

	if (path == NULL || *path == L'\0') {
		archive_set_error(&(a->archive), EINVAL, "pathname is empty");
		return (ARCHIVE_FAILED);
	}
	h = FindFirstFileW(path, &d);
	if (h == INVALID_HANDLE_VALUE) {
		la_dosmaperr(GetLastError());
		archive_set_error(&(a->archive), errno,
		    "Failed to FindFirstFile");
		return (ARCHIVE_FAILED);
	}
	FindClose(h);
	return set_timefilter_find_data(a, timetype,
	    d.ftLastWriteTime.dwHighDateTime, d.ftLastWriteTime.dwLowDateTime,
	    d.ftCreationTime.dwHighDateTime, d.ftCreationTime.dwLowDateTime);
}

#else /* _WIN32 && !__CYGWIN__ */

static int
set_timefilter_stat(struct archive_match *a, int timetype, struct stat *st)
{
	struct archive_entry *ae;
	time_t ctime_sec, mtime_sec;
	long ctime_ns, mtime_ns;

	ae = archive_entry_new();
	if (ae == NULL)
		return (error_nomem(a));
	archive_entry_copy_stat(ae, st);
	ctime_sec = archive_entry_ctime(ae);
	ctime_ns = archive_entry_ctime_nsec(ae);
	mtime_sec = archive_entry_mtime(ae);
	mtime_ns = archive_entry_mtime_nsec(ae);
	archive_entry_free(ae);
	return set_timefilter(a, timetype, mtime_sec, mtime_ns,
			ctime_sec, ctime_ns);
}

static int
set_timefilter_pathname_mbs(struct archive_match *a, int timetype,
    const char *path)
{
	struct stat st;

	if (path == NULL || *path == '\0') {
		archive_set_error(&(a->archive), EINVAL, "pathname is empty");
		return (ARCHIVE_FAILED);
	}
	if (stat(path, &st) != 0) {
		archive_set_error(&(a->archive), errno, "Failed to stat()");
		return (ARCHIVE_FAILED);
	}
	return (set_timefilter_stat(a, timetype, &st));
}

static int
set_timefilter_pathname_wcs(struct archive_match *a, int timetype,
    const wchar_t *path)
{
	struct archive_string as;
	int r;

	if (path == NULL || *path == L'\0') {
		archive_set_error(&(a->archive), EINVAL, "pathname is empty");
		return (ARCHIVE_FAILED);
	}

	/* Convert WCS filename to MBS filename. */
	archive_string_init(&as);
	if (archive_string_append_from_wcs(&as, path, wcslen(path)) < 0) {
		archive_string_free(&as);
		if (errno == ENOMEM)
			return (error_nomem(a));
		archive_set_error(&(a->archive), -1,
		    "Failed to convert WCS to MBS");
		return (ARCHIVE_FAILED);
	}

	r = set_timefilter_pathname_mbs(a, timetype, as.s);
	archive_string_free(&as);

	return (r);
}
#endif /* _WIN32 && !__CYGWIN__ */

/*
 * Call back functions for archive_rb.
 */
static int
cmp_node_mbs(const struct archive_rb_node *n1,
    const struct archive_rb_node *n2)
{
	struct match_file *f1 = (struct match_file *)(uintptr_t)n1;
	struct match_file *f2 = (struct match_file *)(uintptr_t)n2;
	const char *p1, *p2;

	archive_mstring_get_mbs(NULL, &(f1->pathname), &p1);
	archive_mstring_get_mbs(NULL, &(f2->pathname), &p2);
	if (p1 == NULL)
		return (1);
	if (p2 == NULL)
		return (-1);
	return (strcmp(p1, p2));
}
        
static int
cmp_key_mbs(const struct archive_rb_node *n, const void *key)
{
	struct match_file *f = (struct match_file *)(uintptr_t)n;
	const char *p;

	archive_mstring_get_mbs(NULL, &(f->pathname), &p);
	if (p == NULL)
		return (-1);
	return (strcmp(p, (const char *)key));
}

static int
cmp_node_wcs(const struct archive_rb_node *n1,
    const struct archive_rb_node *n2)
{
	struct match_file *f1 = (struct match_file *)(uintptr_t)n1;
	struct match_file *f2 = (struct match_file *)(uintptr_t)n2;
	const wchar_t *p1, *p2;

	archive_mstring_get_wcs(NULL, &(f1->pathname), &p1);
	archive_mstring_get_wcs(NULL, &(f2->pathname), &p2);
	if (p1 == NULL)
		return (1);
	if (p2 == NULL)
		return (-1);
	return (wcscmp(p1, p2));
}
        
static int
cmp_key_wcs(const struct archive_rb_node *n, const void *key)
{
	struct match_file *f = (struct match_file *)(uintptr_t)n;
	const wchar_t *p;

	archive_mstring_get_wcs(NULL, &(f->pathname), &p);
	if (p == NULL)
		return (-1);
	return (wcscmp(p, (const wchar_t *)key));
}

static void
entry_list_init(struct entry_list *list)
{
	list->first = NULL;
	list->last = &(list->first);
	list->count = 0;
}

static void
entry_list_free(struct entry_list *list)
{
	struct match_file *p, *q;

	for (p = list->first; p != NULL; ) {
		q = p;
		p = p->next;
		archive_mstring_clean(&(q->pathname));
		free(q);
	}
}

static void
entry_list_add(struct entry_list *list, struct match_file *file)
{
	*list->last = file;
	list->last = &(file->next);
	list->count++;
}

static int
add_entry(struct archive_match *a, int flag,
    struct archive_entry *entry)
{
	struct match_file *f;
	const void *pathname;
	int r;

	f = calloc(1, sizeof(*f));
	if (f == NULL)
		return (error_nomem(a));

#if defined(_WIN32) && !defined(__CYGWIN__)
	pathname = archive_entry_pathname_w(entry);
	if (pathname == NULL) {
		free(f);
		archive_set_error(&(a->archive), EINVAL, "pathname is NULL");
		return (ARCHIVE_FAILED);
	}
	archive_mstring_copy_wcs(&(f->pathname), pathname);
	a->exclusion_tree.rbt_ops = &rb_ops_wcs;
#else
	(void)rb_ops_wcs;
	pathname = archive_entry_pathname(entry);
	if (pathname == NULL) {
		free(f);
		archive_set_error(&(a->archive), EINVAL, "pathname is NULL");
		return (ARCHIVE_FAILED);
	}
	archive_mstring_copy_mbs(&(f->pathname), pathname);
	a->exclusion_tree.rbt_ops = &rb_ops_mbs;
#endif
	f->flag = flag;
	f->mtime_sec = archive_entry_mtime(entry);
	f->mtime_nsec = archive_entry_mtime_nsec(entry);
	f->ctime_sec = archive_entry_ctime(entry);
	f->ctime_nsec = archive_entry_ctime_nsec(entry);
	r = __archive_rb_tree_insert_node(&(a->exclusion_tree), &(f->node));
	if (!r) {
		struct match_file *f2;

		/* Get the duplicated file. */
		f2 = (struct match_file *)__archive_rb_tree_find_node(
			&(a->exclusion_tree), pathname);

		/*
		 * We always overwrite comparison condition.
		 * If you do not want to overwrite it, you should not
		 * call archive_match_exclude_entry(). We cannot know
		 * what behavior you really expect since overwriting
		 * condition might be different with the flag.
		 */
		if (f2 != NULL) {
			f2->flag = f->flag;
			f2->mtime_sec = f->mtime_sec;
			f2->mtime_nsec = f->mtime_nsec;
			f2->ctime_sec = f->ctime_sec;
			f2->ctime_nsec = f->ctime_nsec;
		}
		/* Release the duplicated file. */
		archive_mstring_clean(&(f->pathname));
		free(f);
		return (ARCHIVE_OK);
	}
	entry_list_add(&(a->exclusion_entry_list), f);
	a->setflag |= TIME_IS_SET;
	return (ARCHIVE_OK);
}

/*
 * Test if entry is excluded by its timestamp.
 */
static int
time_excluded(struct archive_match *a, struct archive_entry *entry)
{
	struct match_file *f;
	const void *pathname;
	time_t sec;
	long nsec;

	/*
	 * If this file/dir is excluded by a time comparison, skip it.
	 */
	if (a->newer_ctime_filter) {
		/* If ctime is not set, use mtime instead. */
		if (archive_entry_ctime_is_set(entry))
			sec = archive_entry_ctime(entry);
		else
			sec = archive_entry_mtime(entry);
		if (sec < a->newer_ctime_sec)
			return (1); /* Too old, skip it. */
		if (sec == a->newer_ctime_sec) {
			if (archive_entry_ctime_is_set(entry))
				nsec = archive_entry_ctime_nsec(entry);
			else
				nsec = archive_entry_mtime_nsec(entry);
			if (nsec < a->newer_ctime_nsec)
				return (1); /* Too old, skip it. */
			if (nsec == a->newer_ctime_nsec &&
			    (a->newer_ctime_filter & ARCHIVE_MATCH_EQUAL)
			      == 0)
				return (1); /* Equal, skip it. */
		}
	}
	if (a->older_ctime_filter) {
		/* If ctime is not set, use mtime instead. */
		if (archive_entry_ctime_is_set(entry))
			sec = archive_entry_ctime(entry);
		else
			sec = archive_entry_mtime(entry);
		if (sec > a->older_ctime_sec)
			return (1); /* Too new, skip it. */
		if (sec == a->older_ctime_sec) {
			if (archive_entry_ctime_is_set(entry))
				nsec = archive_entry_ctime_nsec(entry);
			else
				nsec = archive_entry_mtime_nsec(entry);
			if (nsec > a->older_ctime_nsec)
				return (1); /* Too new, skip it. */
			if (nsec == a->older_ctime_nsec &&
			    (a->older_ctime_filter & ARCHIVE_MATCH_EQUAL)
			      == 0)
				return (1); /* Equal, skip it. */
		}
	}
	if (a->newer_mtime_filter) {
		sec = archive_entry_mtime(entry);
		if (sec < a->newer_mtime_sec)
			return (1); /* Too old, skip it. */
		if (sec == a->newer_mtime_sec) {
			nsec = archive_entry_mtime_nsec(entry);
			if (nsec < a->newer_mtime_nsec)
				return (1); /* Too old, skip it. */
			if (nsec == a->newer_mtime_nsec &&
			    (a->newer_mtime_filter & ARCHIVE_MATCH_EQUAL)
			       == 0)
				return (1); /* Equal, skip it. */
		}
	}
	if (a->older_mtime_filter) {
		sec = archive_entry_mtime(entry);
		if (sec > a->older_mtime_sec)
			return (1); /* Too new, skip it. */
		nsec = archive_entry_mtime_nsec(entry);
		if (sec == a->older_mtime_sec) {
			if (nsec > a->older_mtime_nsec)
				return (1); /* Too new, skip it. */
			if (nsec == a->older_mtime_nsec &&
			    (a->older_mtime_filter & ARCHIVE_MATCH_EQUAL)
			       == 0)
				return (1); /* Equal, skip it. */
		}
	}

	/* If there is no exclusion list, include the file. */
	if (a->exclusion_entry_list.count == 0)
		return (0);

#if defined(_WIN32) && !defined(__CYGWIN__)
	pathname = archive_entry_pathname_w(entry);
	a->exclusion_tree.rbt_ops = &rb_ops_wcs;
#else
	(void)rb_ops_wcs;
	pathname = archive_entry_pathname(entry);
	a->exclusion_tree.rbt_ops = &rb_ops_mbs;
#endif
	if (pathname == NULL)
		return (0);

	f = (struct match_file *)__archive_rb_tree_find_node(
		&(a->exclusion_tree), pathname);
	/* If the file wasn't rejected, include it. */
	if (f == NULL)
		return (0);

	if (f->flag & ARCHIVE_MATCH_CTIME) {
		sec = archive_entry_ctime(entry);
		if (f->ctime_sec > sec) {
			if (f->flag & ARCHIVE_MATCH_OLDER)
				return (1);
		} else if (f->ctime_sec < sec) {
			if (f->flag & ARCHIVE_MATCH_NEWER)
				return (1);
		} else {
			nsec = archive_entry_ctime_nsec(entry);
			if (f->ctime_nsec > nsec) {
				if (f->flag & ARCHIVE_MATCH_OLDER)
					return (1);
			} else if (f->ctime_nsec < nsec) {
				if (f->flag & ARCHIVE_MATCH_NEWER)
					return (1);
			} else if (f->flag & ARCHIVE_MATCH_EQUAL)
				return (1);
		}
	}
	if (f->flag & ARCHIVE_MATCH_MTIME) {
		sec = archive_entry_mtime(entry);
		if (f->mtime_sec > sec) {
			if (f->flag & ARCHIVE_MATCH_OLDER)
				return (1);
		} else if (f->mtime_sec < sec) {
			if (f->flag & ARCHIVE_MATCH_NEWER)
				return (1);
		} else {
			nsec = archive_entry_mtime_nsec(entry);
			if (f->mtime_nsec > nsec) {
				if (f->flag & ARCHIVE_MATCH_OLDER)
					return (1);
			} else if (f->mtime_nsec < nsec) {
				if (f->flag & ARCHIVE_MATCH_NEWER)
					return (1);
			} else if (f->flag & ARCHIVE_MATCH_EQUAL)
				return (1);
		}
	}
	return (0);
}

/*
 * Utility functions to manage inclusion owners
 */

int
archive_match_include_uid(struct archive *_a, la_int64_t uid)
{
	struct archive_match *a;

	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_match_include_uid");
	a = (struct archive_match *)_a;
	return (add_owner_id(a, &(a->inclusion_uids), uid));
}

int
archive_match_include_gid(struct archive *_a, la_int64_t gid)
{
	struct archive_match *a;

	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_match_include_gid");
	a = (struct archive_match *)_a;
	return (add_owner_id(a, &(a->inclusion_gids), gid));
}

int
archive_match_include_uname(struct archive *_a, const char *uname)
{
	struct archive_match *a;

	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_match_include_uname");
	a = (struct archive_match *)_a;
	return (add_owner_name(a, &(a->inclusion_unames), 1, uname));
}

int
archive_match_include_uname_w(struct archive *_a, const wchar_t *uname)
{
	struct archive_match *a;

	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_match_include_uname_w");
	a = (struct archive_match *)_a;
	return (add_owner_name(a, &(a->inclusion_unames), 0, uname));
}

int
archive_match_include_gname(struct archive *_a, const char *gname)
{
	struct archive_match *a;

	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_match_include_gname");
	a = (struct archive_match *)_a;
	return (add_owner_name(a, &(a->inclusion_gnames), 1, gname));
}

int
archive_match_include_gname_w(struct archive *_a, const wchar_t *gname)
{
	struct archive_match *a;

	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_match_include_gname_w");
	a = (struct archive_match *)_a;
	return (add_owner_name(a, &(a->inclusion_gnames), 0, gname));
}

/*
 * Test function for owner(uid, gid, uname, gname).
 *
 * Returns 1 if archive entry is excluded.
 * Returns 0 if archive entry is not excluded.
 * Returns <0 if something error happened.
 */
int
archive_match_owner_excluded(struct archive *_a,
    struct archive_entry *entry)
{
	struct archive_match *a;

	archive_check_magic(_a, ARCHIVE_MATCH_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_match_id_excluded_ae");

	a = (struct archive_match *)_a;
	if (entry == NULL) {
		archive_set_error(&(a->archive), EINVAL, "entry is NULL");
		return (ARCHIVE_FAILED);
	}

	/* If we don't have inclusion id set at all, the entry is always
	 * not excluded. */
	if ((a->setflag & ID_IS_SET) == 0)
		return (0);
	return (owner_excluded(a, entry));
}

static int
add_owner_id(struct archive_match *a, struct id_array *ids, int64_t id)
{
	unsigned i;

	if (ids->count + 1 >= ids->size) {
		void *p;

		if (ids->size == 0)
			ids->size = 8;
		else
			ids->size *= 2;
		p = realloc(ids->ids, sizeof(*ids->ids) * ids->size);
		if (p == NULL)
			return (error_nomem(a));
		ids->ids = (int64_t *)p;
	}

	/* Find an insert point. */
	for (i = 0; i < ids->count; i++) {
		if (ids->ids[i] >= id)
			break;
	}

	/* Add owner id. */
	if (i == ids->count)
		ids->ids[ids->count++] = id;
	else if (ids->ids[i] != id) {
		memmove(&(ids->ids[i+1]), &(ids->ids[i]),
		    (ids->count - i) * sizeof(ids->ids[0]));
		ids->ids[i] = id;
		ids->count++;
	}
	a->setflag |= ID_IS_SET;
	return (ARCHIVE_OK);
}

static int
match_owner_id(struct id_array *ids, int64_t id)
{
	unsigned b, m, t;

	t = 0;
	b = (unsigned)ids->count;
	while (t < b) {
		m = (t + b)>>1;
		if (ids->ids[m] == id)
			return (1);
		if (ids->ids[m] < id)
			t = m + 1;
		else
			b = m;
	}
	return (0);
}

static int
add_owner_name(struct archive_match *a, struct match_list *list,
    int mbs, const void *name)
{
	struct match *match;

	match = calloc(1, sizeof(*match));
	if (match == NULL)
		return (error_nomem(a));
	if (mbs)
		archive_mstring_copy_mbs(&(match->pattern), name);
	else
		archive_mstring_copy_wcs(&(match->pattern), name);
	match_list_add(list, match);
	a->setflag |= ID_IS_SET;
	return (ARCHIVE_OK);
}

#if !defined(_WIN32) || defined(__CYGWIN__)
static int
match_owner_name_mbs(struct archive_match *a, struct match_list *list,
    const char *name)
{
	struct match *m;
	const char *p;

	if (name == NULL || *name == '\0')
		return (0);
	for (m = list->first; m; m = m->next) {
		if (archive_mstring_get_mbs(&(a->archive), &(m->pattern), &p)
		    < 0 && errno == ENOMEM)
			return (error_nomem(a));
		if (p != NULL && strcmp(p, name) == 0) {
			m->matches++;
			return (1);
		}
	}
	return (0);
}
#else
static int
match_owner_name_wcs(struct archive_match *a, struct match_list *list,
    const wchar_t *name)
{
	struct match *m;
	const wchar_t *p;

	if (name == NULL || *name == L'\0')
		return (0);
	for (m = list->first; m; m = m->next) {
		if (archive_mstring_get_wcs(&(a->archive), &(m->pattern), &p)
		    < 0 && errno == ENOMEM)
			return (error_nomem(a));
		if (p != NULL && wcscmp(p, name) == 0) {
			m->matches++;
			return (1);
		}
	}
	return (0);
}
#endif

/*
 * Test if entry is excluded by uid, gid, uname or gname.
 */
static int
owner_excluded(struct archive_match *a, struct archive_entry *entry)
{
	int r;

	if (a->inclusion_uids.count) {
		if (!match_owner_id(&(a->inclusion_uids),
		    archive_entry_uid(entry)))
			return (1);
	}

	if (a->inclusion_gids.count) {
		if (!match_owner_id(&(a->inclusion_gids),
		    archive_entry_gid(entry)))
			return (1);
	}

	if (a->inclusion_unames.count) {
#if defined(_WIN32) && !defined(__CYGWIN__)
		r = match_owner_name_wcs(a, &(a->inclusion_unames),
			archive_entry_uname_w(entry));
#else
		r = match_owner_name_mbs(a, &(a->inclusion_unames),
			archive_entry_uname(entry));
#endif
		if (!r)
			return (1);
		else if (r < 0)
			return (r);
	}

	if (a->inclusion_gnames.count) {
#if defined(_WIN32) && !defined(__CYGWIN__)
		r = match_owner_name_wcs(a, &(a->inclusion_gnames),
			archive_entry_gname_w(entry));
#else
		r = match_owner_name_mbs(a, &(a->inclusion_gnames),
			archive_entry_gname(entry));
#endif
		if (!r)
			return (1);
		else if (r < 0)
			return (r);
	}
	return (0);
}

