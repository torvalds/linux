/*
 *  $Id: inputstr.c,v 1.88 2018/06/18 22:10:54 tom Exp $
 *
 *  inputstr.c -- functions for input/display of a string
 *
 *  Copyright 2000-2017,2018	Thomas E. Dickey
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License, version 2.1
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program; if not, write to
 *	Free Software Foundation, Inc.
 *	51 Franklin St., Fifth Floor
 *	Boston, MA 02110, USA.
 */

#include <dialog.h>
#include <dlg_keys.h>

#include <errno.h>

#ifdef HAVE_SETLOCALE
#include <locale.h>
#endif

#if defined(HAVE_SEARCH_H) && defined(HAVE_TSEARCH)
#include <search.h>
#else
#undef HAVE_TSEARCH
#endif

#ifdef NEED_WCHAR_H
#include <wchar.h>
#endif

#if defined(USE_WIDE_CURSES)
#define USE_CACHING 1
#elif defined(HAVE_XDIALOG)
#define USE_CACHING 1		/* editbox really needs caching! */
#else
#define USE_CACHING 0
#endif

typedef struct _cache {
    struct _cache *next;
#if USE_CACHING
    int cache_num;		/* tells what type of data is in list[] */
    const char *string_at;	/* unique: associate caches by char* */
#endif
    size_t s_len;		/* strlen(string) - we add 1 for EOS */
    size_t i_len;		/* length(list) - we add 1 for EOS */
    char *string;		/* a copy of the last-processed string */
    int *list;			/* indices into the string */
} CACHE;

#if USE_CACHING
#define SAME_CACHE(c,s,l) (c->string != 0 && memcmp(c->string,s,l) == 0)

static CACHE *cache_list;

typedef enum {
    cInxCols
    ,cCntWideBytes
    ,cCntWideChars
    ,cInxWideChars
    ,cMAX
} CACHE_USED;

#ifdef HAVE_TSEARCH
static void *sorted_cache;
#endif

#ifdef USE_WIDE_CURSES
static int
have_locale(void)
{
    static int result = -1;
    if (result < 0) {
	char *test = setlocale(LC_ALL, 0);
	if (test == 0 || *test == 0) {
	    result = FALSE;
	} else if (strcmp(test, "C") && strcmp(test, "POSIX")) {
	    result = TRUE;
	} else {
	    result = FALSE;
	}
    }
    return result;
}
#endif

#ifdef HAVE_TSEARCH

#if 0
static void
show_tsearch(const void *nodep, const VISIT which, const int depth)
{
    const CACHE *p = *(CACHE * const *) nodep;
    (void) depth;
    if (which == postorder || which == leaf) {
	DLG_TRACE(("# cache %p %p:%s\n", p, p->string, p->string));
    }
}

static void
trace_cache(const char *fn, int ln)
{
    DLG_TRACE(("# trace_cache %s@%d\n", fn, ln));
    twalk(sorted_cache, show_tsearch);
}

#else
#define trace_cache(fn, ln)	/* nothing */
#endif

#define CMP(a,b) (((a) > (b)) ? 1 : (((a) < (b)) ? -1 : 0))

static int
compare_cache(const void *a, const void *b)
{
    const CACHE *p = (const CACHE *) a;
    const CACHE *q = (const CACHE *) b;
    int result = CMP(p->cache_num, q->cache_num);
    if (result == 0)
	result = CMP(p->string_at, q->string_at);
    return result;
}
#endif

static CACHE *
find_cache(int cache_num, const char *string)
{
    CACHE *p;

#ifdef HAVE_TSEARCH
    void *pp;
    CACHE find;

    memset(&find, 0, sizeof(find));
    find.cache_num = cache_num;
    find.string_at = string;

    if ((pp = tfind(&find, &sorted_cache, compare_cache)) != 0) {
	p = *(CACHE **) pp;
    } else {
	p = 0;
    }
#else
    for (p = cache_list; p != 0; p = p->next) {
	if (p->string_at == string) {
	    break;
	}
    }
#endif
    return p;
}

static CACHE *
make_cache(int cache_num, const char *string)
{
    CACHE *p;

    p = dlg_calloc(CACHE, 1);
    assert_ptr(p, "load_cache");
    p->next = cache_list;
    cache_list = p;

    p->cache_num = cache_num;
    p->string_at = string;

#ifdef HAVE_TSEARCH
    (void) tsearch(p, &sorted_cache, compare_cache);
#endif
    return p;
}

static CACHE *
load_cache(int cache_num, const char *string)
{
    CACHE *p;

    if ((p = find_cache(cache_num, string)) == 0) {
	p = make_cache(cache_num, string);
    }
    return p;
}
#else
static CACHE my_cache;
#define SAME_CACHE(c,s,l) (c->string != 0)
#define load_cache(cache, string) &my_cache
#endif /* USE_CACHING */

/*
 * If the given string has not changed, we do not need to update the index.
 * If we need to update the index, allocate enough memory for it.
 */
static bool
same_cache2(CACHE * cache, const char *string, unsigned i_len)
{
    unsigned need;
    size_t s_len = strlen(string);
    bool result = TRUE;

    if (cache->s_len == 0
	|| cache->s_len < s_len
	|| cache->list == 0
	|| !SAME_CACHE(cache, string, (size_t) s_len)) {

	need = (i_len + 1);
	if (cache->list == 0) {
	    cache->list = dlg_malloc(int, need);
	} else if (cache->i_len < i_len) {
	    cache->list = dlg_realloc(int, need, cache->list);
	}
	assert_ptr(cache->list, "load_cache");
	cache->i_len = i_len;

	if (cache->s_len >= s_len && cache->string != 0) {
	    strcpy(cache->string, string);
	} else {
	    if (cache->string != 0)
		free(cache->string);
	    cache->string = dlg_strclone(string);
	}
	cache->s_len = s_len;

	result = FALSE;
    }
    return result;
}

#ifdef USE_WIDE_CURSES
/*
 * Like same_cache2(), but we are only concerned about caching a copy of the
 * string and its associated length.
 */
static bool
same_cache1(CACHE * cache, const char *string, size_t i_len)
{
    size_t s_len = strlen(string);
    bool result = TRUE;

    if (cache->s_len != s_len
	|| !SAME_CACHE(cache, string, (size_t) s_len)) {

	if (cache->s_len >= s_len && cache->string != 0) {
	    strcpy(cache->string, string);
	} else {
	    if (cache->string != 0)
		free(cache->string);
	    cache->string = dlg_strclone(string);
	}
	cache->s_len = s_len;
	cache->i_len = i_len;

	result = FALSE;
    }
    return result;
}
#endif /* USE_CACHING */

/*
 * Counts the number of bytes that make up complete wide-characters, up to byte
 * 'len'.  If there is no locale set, simply return the original length.
 */
#ifdef USE_WIDE_CURSES
static int
dlg_count_wcbytes(const char *string, size_t len)
{
    int result;

    if (have_locale()) {
	CACHE *cache = load_cache(cCntWideBytes, string);
	if (!same_cache1(cache, string, len)) {
	    while (len != 0) {
		size_t code = 0;
		const char *src = cache->string;
		mbstate_t state;
		char save = cache->string[len];

		cache->string[len] = '\0';
		memset(&state, 0, sizeof(state));
		code = mbsrtowcs((wchar_t *) 0, &src, len, &state);
		cache->string[len] = save;
		if ((int) code >= 0) {
		    break;
		}
		--len;
	    }
	    cache->i_len = len;
	}
	result = (int) cache->i_len;
    } else {
	result = (int) len;
    }
    return result;
}
#endif /* USE_WIDE_CURSES */

/*
 * Counts the number of wide-characters in the string.
 */
int
dlg_count_wchars(const char *string)
{
    int result;
#ifdef USE_WIDE_CURSES

    if (have_locale()) {
	size_t len = strlen(string);
	CACHE *cache = load_cache(cCntWideChars, string);

	if (!same_cache1(cache, string, len)) {
	    const char *src = cache->string;
	    mbstate_t state;
	    int part = dlg_count_wcbytes(cache->string, len);
	    char save = cache->string[part];
	    size_t code;
	    wchar_t *temp = dlg_calloc(wchar_t, len + 1);

	    if (temp != 0) {
		cache->string[part] = '\0';
		memset(&state, 0, sizeof(state));
		code = mbsrtowcs(temp, &src, (size_t) part, &state);
		cache->i_len = ((int) code >= 0) ? wcslen(temp) : 0;
		cache->string[part] = save;
		free(temp);
	    } else {
		cache->i_len = 0;
	    }
	}
	result = (int) cache->i_len;
    } else
#endif /* USE_WIDE_CURSES */
    {
	result = (int) strlen(string);
    }
    return result;
}

/*
 * Build an index of the wide-characters in the string, so we can easily tell
 * which byte-offset begins a given wide-character.
 */
const int *
dlg_index_wchars(const char *string)
{
    unsigned len = (unsigned) dlg_count_wchars(string);
    unsigned inx;
    CACHE *cache = load_cache(cInxWideChars, string);

    if (!same_cache2(cache, string, len)) {
	const char *current = string;

	cache->list[0] = 0;
	for (inx = 1; inx <= len; ++inx) {
#ifdef USE_WIDE_CURSES
	    if (have_locale()) {
		mbstate_t state;
		int width;
		memset(&state, 0, sizeof(state));
		width = (int) mbrlen(current, strlen(current), &state);
		if (width <= 0)
		    width = 1;	/* FIXME: what if we have a control-char? */
		current += width;
		cache->list[inx] = cache->list[inx - 1] + width;
	    } else
#endif /* USE_WIDE_CURSES */
	    {
		(void) current;
		cache->list[inx] = (int) inx;
	    }
	}
    }
    return cache->list;
}

/*
 * Given the character-offset to find in the list, return the corresponding
 * array index.
 */
int
dlg_find_index(const int *list, int limit, int to_find)
{
    int result;
    for (result = 0; result <= limit; ++result) {
	if (to_find == list[result]
	    || result == limit
	    || ((result < limit) && (to_find < list[result + 1]))) {
	    break;
	}
    }
    return result;
}

/*
 * Build a list of the display-columns for the given string's characters.
 */
const int *
dlg_index_columns(const char *string)
{
    unsigned len = (unsigned) dlg_count_wchars(string);
    unsigned inx;
    CACHE *cache = load_cache(cInxCols, string);

    if (!same_cache2(cache, string, len)) {
	cache->list[0] = 0;
#ifdef USE_WIDE_CURSES
	if (have_locale()) {
	    size_t num_bytes = strlen(string);
	    const int *inx_wchars = dlg_index_wchars(string);
	    mbstate_t state;

	    for (inx = 0; inx < len; ++inx) {
		wchar_t temp[2];
		size_t check;
		int result;

		if (string[inx_wchars[inx]] == TAB) {
		    result = ((cache->list[inx] | 7) + 1) - cache->list[inx];
		} else {
		    memset(&state, 0, sizeof(state));
		    memset(temp, 0, sizeof(temp));
		    check = mbrtowc(temp,
				    string + inx_wchars[inx],
				    num_bytes - (size_t) inx_wchars[inx],
				    &state);
		    if ((int) check <= 0) {
			result = 1;
		    } else {
			result = wcwidth(temp[0]);
		    }
		    if (result < 0) {
			const wchar_t *printable;
			cchar_t temp2, *temp2p = &temp2;
			setcchar(temp2p, temp, 0, 0, 0);
			printable = wunctrl(temp2p);
			result = printable ? (int) wcslen(printable) : 1;
		    }
		}
		cache->list[inx + 1] = result;
		if (inx != 0)
		    cache->list[inx + 1] += cache->list[inx];
	    }
	} else
#endif /* USE_WIDE_CURSES */
	{
	    for (inx = 0; inx < len; ++inx) {
		chtype ch = UCH(string[inx]);

		if (ch == TAB)
		    cache->list[inx + 1] =
			((cache->list[inx] | 7) + 1) - cache->list[inx];
		else if (isprint(UCH(ch)))
		    cache->list[inx + 1] = 1;
		else {
		    const char *printable;
		    printable = unctrl(ch);
		    cache->list[inx + 1] = (printable
					    ? (int) strlen(printable)
					    : 1);
		}
		if (inx != 0)
		    cache->list[inx + 1] += cache->list[inx];
	    }
	}
    }
    return cache->list;
}

/*
 * Returns the number of columns used for a string.  That happens to be the
 * end-value of the cols[] array.
 */
int
dlg_count_columns(const char *string)
{
    int result = 0;
    int limit = dlg_count_wchars(string);
    if (limit > 0) {
	const int *cols = dlg_index_columns(string);
	result = cols[limit];
    } else {
	result = (int) strlen(string);
    }
    dlg_finish_string(string);
    return result;
}

/*
 * Given a column limit, count the number of wide characters that can fit
 * into that limit.  The offset is used to skip over a leading character
 * that was already written.
 */
int
dlg_limit_columns(const char *string, int limit, int offset)
{
    const int *cols = dlg_index_columns(string);
    int result = dlg_count_wchars(string);

    while (result > 0 && (cols[result] - cols[offset]) > limit)
	--result;
    return result;
}

/*
 * Updates the string and character-offset, given various editing characters
 * or literal characters which are inserted at the character-offset.
 */
bool
dlg_edit_string(char *string, int *chr_offset, int key, int fkey, bool force)
{
    int i;
    int len = (int) strlen(string);
    int limit = dlg_count_wchars(string);
    const int *indx = dlg_index_wchars(string);
    int offset = dlg_find_index(indx, limit, *chr_offset);
    int max_len = dlg_max_input(MAX_LEN);
    bool edit = TRUE;

    /* transform editing characters into equivalent function-keys */
    if (!fkey) {
	fkey = TRUE;		/* assume we transform */
	switch (key) {
	case 0:
	    break;
	case ESC:
	case TAB:
	    fkey = FALSE;	/* this is used for navigation */
	    break;
	default:
	    fkey = FALSE;	/* ...no, we did not transform */
	    break;
	}
    }

    if (fkey) {
	switch (key) {
	case 0:		/* special case for loop entry */
	    edit = force;
	    break;
	case DLGK_GRID_LEFT:
	    if (*chr_offset && offset > 0)
		*chr_offset = indx[offset - 1];
	    break;
	case DLGK_GRID_RIGHT:
	    if (offset < limit)
		*chr_offset = indx[offset + 1];
	    break;
	case DLGK_BEGIN:
	    if (*chr_offset)
		*chr_offset = 0;
	    break;
	case DLGK_FINAL:
	    if (offset < limit)
		*chr_offset = indx[limit];
	    break;
	case DLGK_DELETE_LEFT:
	    if (offset) {
		int gap = indx[offset] - indx[offset - 1];
		*chr_offset = indx[offset - 1];
		if (gap > 0) {
		    for (i = *chr_offset;
			 (string[i] = string[i + gap]) != '\0';
			 i++) {
			;
		    }
		}
	    }
	    break;
	case DLGK_DELETE_RIGHT:
	    if (limit) {
		if (--limit == 0) {
		    string[*chr_offset = 0] = '\0';
		} else {
		    int gap = ((offset <= limit)
			       ? (indx[offset + 1] - indx[offset])
			       : 0);
		    if (gap > 0) {
			for (i = indx[offset];
			     (string[i] = string[i + gap]) != '\0';
			     i++) {
			    ;
			}
		    } else if (offset > 0) {
			string[indx[offset - 1]] = '\0';
		    }
		    if (*chr_offset > indx[limit])
			*chr_offset = indx[limit];
		}
	    }
	    break;
	case DLGK_DELETE_ALL:
	    string[*chr_offset = 0] = '\0';
	    break;
	case DLGK_ENTER:
	    edit = 0;
	    break;
#ifdef KEY_RESIZE
	case KEY_RESIZE:
	    edit = 0;
	    break;
#endif
	case DLGK_GRID_UP:
	case DLGK_GRID_DOWN:
	case DLGK_FIELD_NEXT:
	case DLGK_FIELD_PREV:
	    edit = 0;
	    break;
	case ERR:
	    edit = 0;
	    break;
	default:
	    beep();
	    break;
	}
    } else {
	if (key == ESC || key == ERR) {
	    edit = 0;
	} else {
	    if (len < max_len) {
		for (i = ++len; i > *chr_offset; i--)
		    string[i] = string[i - 1];
		string[*chr_offset] = (char) key;
		*chr_offset += 1;
	    } else {
		(void) beep();
	    }
	}
    }
    return edit;
}

static void
compute_edit_offset(const char *string,
		    int chr_offset,
		    int x_last,
		    int *p_dpy_column,
		    int *p_scroll_amt)
{
    const int *cols = dlg_index_columns(string);
    const int *indx = dlg_index_wchars(string);
    int limit = dlg_count_wchars(string);
    int offset = dlg_find_index(indx, limit, chr_offset);
    int offset2;
    int dpy_column;
    int n;

    for (n = offset2 = 0; n <= offset; ++n) {
	if ((cols[offset] - cols[n]) < x_last
	    && (offset == limit || (cols[offset + 1] - cols[n]) < x_last)) {
	    offset2 = n;
	    break;
	}
    }

    dpy_column = cols[offset] - cols[offset2];

    if (p_dpy_column != 0)
	*p_dpy_column = dpy_column;
    if (p_scroll_amt != 0)
	*p_scroll_amt = offset2;
}

/*
 * Given the character-offset in the string, returns the display-offset where
 * we will position the cursor.
 */
int
dlg_edit_offset(char *string, int chr_offset, int x_last)
{
    int result;

    compute_edit_offset(string, chr_offset, x_last, &result, 0);

    return result;
}

/*
 * Displays the string, shifted as necessary, to fit within the box and show
 * the current character-offset.
 */
void
dlg_show_string(WINDOW *win,
		const char *string,	/* string to display (may be multibyte) */
		int chr_offset,	/* character (not bytes) offset */
		chtype attr,	/* window-attributes */
		int y_base,	/* beginning row on screen */
		int x_base,	/* beginning column on screen */
		int x_last,	/* number of columns on screen */
		bool hidden,	/* if true, do not echo */
		bool force)	/* if true, force repaint */
{
    x_last = MIN(x_last + x_base, getmaxx(win)) - x_base;

    if (hidden && !dialog_vars.insecure) {
	if (force) {
	    (void) wmove(win, y_base, x_base);
	    wrefresh(win);
	}
    } else {
	const int *cols = dlg_index_columns(string);
	const int *indx = dlg_index_wchars(string);
	int limit = dlg_count_wchars(string);

	int i, j, k;
	int input_x;
	int scrollamt;

	compute_edit_offset(string, chr_offset, x_last, &input_x, &scrollamt);

	dlg_attrset(win, attr);
	(void) wmove(win, y_base, x_base);
	for (i = scrollamt, k = 0; i < limit && k < x_last; ++i) {
	    int check = cols[i + 1] - cols[scrollamt];
	    if (check <= x_last) {
		for (j = indx[i]; j < indx[i + 1]; ++j) {
		    chtype ch = UCH(string[j]);
		    if (hidden && dialog_vars.insecure) {
			waddch(win, '*');
		    } else if (ch == TAB) {
			int count = cols[i + 1] - cols[i];
			while (--count >= 0)
			    waddch(win, ' ');
		    } else {
			waddch(win, ch);
		    }
		}
		k = check;
	    } else {
		break;
	    }
	}
	while (k++ < x_last)
	    waddch(win, ' ');
	(void) wmove(win, y_base, x_base + input_x);
	wrefresh(win);
    }
}

/*
 * Discard cached data for the given string.
 */
void
dlg_finish_string(const char *string)
{
#if USE_CACHING
    if ((string != 0) && dialog_state.finish_string) {
	CACHE *p = cache_list;
	CACHE *q = 0;
	CACHE *r;

	while (p != 0) {
	    if (p->string_at == string) {
#ifdef HAVE_TSEARCH
		if (tdelete(p, &sorted_cache, compare_cache) == 0) {
		    continue;
		}
		trace_cache(__FILE__, __LINE__);
#endif
		if (p->string != 0)
		    free(p->string);
		if (p->list != 0)
		    free(p->list);
		if (p == cache_list) {
		    cache_list = p->next;
		    r = cache_list;
		} else {
		    q->next = p->next;
		    r = q;
		}
		free(p);
		p = r;
	    } else {
		q = p;
		p = p->next;
	    }
	}
    }
#else
    (void) string;
#endif
}

#ifdef NO_LEAKS
void
_dlg_inputstr_leaks(void)
{
#if USE_CACHING
    dialog_state.finish_string = TRUE;
    trace_cache(__FILE__, __LINE__);
    while (cache_list != 0) {
	dlg_finish_string(cache_list->string_at);
    }
#endif /* USE_CACHING */
}
#endif /* NO_LEAKS */
