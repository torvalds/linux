/****************************************************************************
 * Copyright (c) 1999-2012,2013 Free Software Foundation, Inc.              *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *  Author: Thomas E. Dickey <dickey@clark.net> 1999-on                     *
 ****************************************************************************/

/*
 * align_ttype.c --  functions for TERMTYPE
 *
 *	_nc_align_termtype()
 *	_nc_copy_termtype()
 *
 */

#include <curses.priv.h>

#include <tic.h>

MODULE_ID("$Id: alloc_ttype.c,v 1.27 2013/06/08 16:54:50 tom Exp $")

#if NCURSES_XNAMES
/*
 * Merge the a/b lists into dst.  Both a/b are sorted (see _nc_extend_names()),
 * so we do not have to worry about order dependencies.
 */
static int
merge_names(char **dst, char **a, int na, char **b, int nb)
{
    int n = 0;
    while (na > 0 && nb > 0) {
	int cmp = strcmp(*a, *b);
	if (cmp < 0) {
	    dst[n++] = *a++;
	    na--;
	} else if (cmp > 0) {
	    dst[n++] = *b++;
	    nb--;
	} else if (cmp == 0) {
	    dst[n++] = *a;
	    a++, b++;
	    na--, nb--;
	}
    }
    while (na-- > 0) {
	dst[n++] = *a++;
    }
    while (nb-- > 0) {
	dst[n++] = *b++;
    }
    DEBUG(4, ("merge_names -> %d", n));
    return n;
}

static bool
find_name(char **table, int length, char *name)
{
    while (length-- > 0) {
	if (!strcmp(*table++, name)) {
	    DEBUG(4, ("found name '%s'", name));
	    return TRUE;
	}
    }
    DEBUG(4, ("did not find name '%s'", name));
    return FALSE;
}

#define EXTEND_NUM(num, ext) \
	to->num = (unsigned short) (to->num + (ext - to->ext))

static void
realign_data(TERMTYPE *to, char **ext_Names,
	     int ext_Booleans,
	     int ext_Numbers,
	     int ext_Strings)
{
    int n, m, base;
    int limit = (to->ext_Booleans + to->ext_Numbers + to->ext_Strings);

    if (to->ext_Booleans != ext_Booleans) {
	EXTEND_NUM(num_Booleans, ext_Booleans);
	TYPE_REALLOC(NCURSES_SBOOL, to->num_Booleans, to->Booleans);
	for (n = to->ext_Booleans - 1,
	     m = ext_Booleans - 1,
	     base = to->num_Booleans - (m + 1); m >= 0; m--) {
	    if (find_name(to->ext_Names, limit, ext_Names[m])) {
		to->Booleans[base + m] = to->Booleans[base + n--];
	    } else {
		to->Booleans[base + m] = FALSE;
	    }
	}
	to->ext_Booleans = UShort(ext_Booleans);
    }

    if (to->ext_Numbers != ext_Numbers) {
	EXTEND_NUM(num_Numbers, ext_Numbers);
	TYPE_REALLOC(short, to->num_Numbers, to->Numbers);
	for (n = to->ext_Numbers - 1,
	     m = ext_Numbers - 1,
	     base = to->num_Numbers - (m + 1); m >= 0; m--) {
	    if (find_name(to->ext_Names, limit, ext_Names[m + ext_Booleans])) {
		to->Numbers[base + m] = to->Numbers[base + n--];
	    } else {
		to->Numbers[base + m] = ABSENT_NUMERIC;
	    }
	}
	to->ext_Numbers = UShort(ext_Numbers);
    }
    if (to->ext_Strings != ext_Strings) {
	EXTEND_NUM(num_Strings, ext_Strings);
	TYPE_REALLOC(char *, to->num_Strings, to->Strings);
	for (n = to->ext_Strings - 1,
	     m = ext_Strings - 1,
	     base = to->num_Strings - (m + 1); m >= 0; m--) {
	    if (find_name(to->ext_Names, limit, ext_Names[m + ext_Booleans + ext_Numbers])) {
		to->Strings[base + m] = to->Strings[base + n--];
	    } else {
		to->Strings[base + m] = ABSENT_STRING;
	    }
	}
	to->ext_Strings = UShort(ext_Strings);
    }
}

/*
 * Returns the first index in ext_Names[] for the given token-type
 */
static unsigned
_nc_first_ext_name(TERMTYPE *tp, int token_type)
{
    unsigned first;

    switch (token_type) {
    case BOOLEAN:
	first = 0;
	break;
    case NUMBER:
	first = tp->ext_Booleans;
	break;
    case STRING:
	first = (unsigned) (tp->ext_Booleans + tp->ext_Numbers);
	break;
    default:
	first = 0;
	break;
    }
    return first;
}

/*
 * Returns the last index in ext_Names[] for the given token-type
 */
static unsigned
_nc_last_ext_name(TERMTYPE *tp, int token_type)
{
    unsigned last;

    switch (token_type) {
    case BOOLEAN:
	last = tp->ext_Booleans;
	break;
    case NUMBER:
	last = (unsigned) (tp->ext_Booleans + tp->ext_Numbers);
	break;
    default:
    case STRING:
	last = NUM_EXT_NAMES(tp);
	break;
    }
    return last;
}

/*
 * Lookup an entry from extended-names, returning -1 if not found
 */
static int
_nc_find_ext_name(TERMTYPE *tp, char *name, int token_type)
{
    unsigned j;
    unsigned first = _nc_first_ext_name(tp, token_type);
    unsigned last = _nc_last_ext_name(tp, token_type);

    for (j = first; j < last; j++) {
	if (!strcmp(name, tp->ext_Names[j])) {
	    return (int) j;
	}
    }
    return -1;
}

/*
 * Translate an index into ext_Names[] into the corresponding index into data
 * (e.g., Booleans[]).
 */
static int
_nc_ext_data_index(TERMTYPE *tp, int n, int token_type)
{
    switch (token_type) {
    case BOOLEAN:
	n += (tp->num_Booleans - tp->ext_Booleans);
	break;
    case NUMBER:
	n += (tp->num_Numbers - tp->ext_Numbers)
	    - (tp->ext_Booleans);
	break;
    default:
    case STRING:
	n += (tp->num_Strings - tp->ext_Strings)
	    - (tp->ext_Booleans + tp->ext_Numbers);
    }
    return n;
}

/*
 * Adjust tables to remove (not free) an extended name and its corresponding
 * data.
 */
static bool
_nc_del_ext_name(TERMTYPE *tp, char *name, int token_type)
{
    int j;
    int first, last;

    if ((first = _nc_find_ext_name(tp, name, token_type)) >= 0) {
	last = (int) NUM_EXT_NAMES(tp) - 1;
	for (j = first; j < last; j++) {
	    tp->ext_Names[j] = tp->ext_Names[j + 1];
	}
	first = _nc_ext_data_index(tp, first, token_type);
	switch (token_type) {
	case BOOLEAN:
	    last = tp->num_Booleans - 1;
	    for (j = first; j < last; j++)
		tp->Booleans[j] = tp->Booleans[j + 1];
	    tp->ext_Booleans--;
	    tp->num_Booleans--;
	    break;
	case NUMBER:
	    last = tp->num_Numbers - 1;
	    for (j = first; j < last; j++)
		tp->Numbers[j] = tp->Numbers[j + 1];
	    tp->ext_Numbers--;
	    tp->num_Numbers--;
	    break;
	case STRING:
	    last = tp->num_Strings - 1;
	    for (j = first; j < last; j++)
		tp->Strings[j] = tp->Strings[j + 1];
	    tp->ext_Strings--;
	    tp->num_Strings--;
	    break;
	}
	return TRUE;
    }
    return FALSE;
}

/*
 * Adjust tables to insert an extended name, making room for new data.  The
 * index into the corresponding data array is returned.
 */
static int
_nc_ins_ext_name(TERMTYPE *tp, char *name, int token_type)
{
    unsigned first = _nc_first_ext_name(tp, token_type);
    unsigned last = _nc_last_ext_name(tp, token_type);
    unsigned total = NUM_EXT_NAMES(tp) + 1;
    unsigned j, k;

    for (j = first; j < last; j++) {
	int cmp = strcmp(name, tp->ext_Names[j]);
	if (cmp == 0)
	    /* already present */
	    return _nc_ext_data_index(tp, (int) j, token_type);
	if (cmp < 0) {
	    break;
	}
    }

    TYPE_REALLOC(char *, total, tp->ext_Names);
    for (k = total - 1; k > j; k--)
	tp->ext_Names[k] = tp->ext_Names[k - 1];
    tp->ext_Names[j] = name;
    j = (unsigned) _nc_ext_data_index(tp, (int) j, token_type);

    switch (token_type) {
    case BOOLEAN:
	tp->ext_Booleans++;
	tp->num_Booleans++;
	TYPE_REALLOC(NCURSES_SBOOL, tp->num_Booleans, tp->Booleans);
	for (k = (unsigned) (tp->num_Booleans - 1); k > j; k--)
	    tp->Booleans[k] = tp->Booleans[k - 1];
	break;
    case NUMBER:
	tp->ext_Numbers++;
	tp->num_Numbers++;
	TYPE_REALLOC(short, tp->num_Numbers, tp->Numbers);
	for (k = (unsigned) (tp->num_Numbers - 1); k > j; k--)
	    tp->Numbers[k] = tp->Numbers[k - 1];
	break;
    case STRING:
	tp->ext_Strings++;
	tp->num_Strings++;
	TYPE_REALLOC(char *, tp->num_Strings, tp->Strings);
	for (k = (unsigned) (tp->num_Strings - 1); k > j; k--)
	    tp->Strings[k] = tp->Strings[k - 1];
	break;
    }
    return (int) j;
}

/*
 * Look for strings that are marked cancelled, which happen to be the same name
 * as a boolean or number.  We'll get this as a special case when we get a
 * cancellation of a name that is inherited from another entry.
 */
static void
adjust_cancels(TERMTYPE *to, TERMTYPE *from)
{
    int first = to->ext_Booleans + to->ext_Numbers;
    int last = first + to->ext_Strings;
    int j, k;

    for (j = first; j < last;) {
	char *name = to->ext_Names[j];
	int j_str = to->num_Strings - first - to->ext_Strings;

	if (to->Strings[j + j_str] == CANCELLED_STRING) {
	    if (_nc_find_ext_name(from, to->ext_Names[j], BOOLEAN) >= 0) {
		if (_nc_del_ext_name(to, name, STRING)
		    || _nc_del_ext_name(to, name, NUMBER)) {
		    k = _nc_ins_ext_name(to, name, BOOLEAN);
		    to->Booleans[k] = FALSE;
		} else {
		    j++;
		}
	    } else if (_nc_find_ext_name(from, to->ext_Names[j], NUMBER) >= 0) {
		if (_nc_del_ext_name(to, name, STRING)
		    || _nc_del_ext_name(to, name, BOOLEAN)) {
		    k = _nc_ins_ext_name(to, name, NUMBER);
		    to->Numbers[k] = CANCELLED_NUMERIC;
		} else {
		    j++;
		}
	    } else if (_nc_find_ext_name(from, to->ext_Names[j], STRING) >= 0) {
		if (_nc_del_ext_name(to, name, NUMBER)
		    || _nc_del_ext_name(to, name, BOOLEAN)) {
		    k = _nc_ins_ext_name(to, name, STRING);
		    to->Strings[k] = CANCELLED_STRING;
		} else {
		    j++;
		}
	    } else {
		j++;
	    }
	} else {
	    j++;
	}
    }
}

NCURSES_EXPORT(void)
_nc_align_termtype(TERMTYPE *to, TERMTYPE *from)
{
    int na = (int) NUM_EXT_NAMES(to);
    int nb = (int) NUM_EXT_NAMES(from);
    int n;
    bool same;
    char **ext_Names;
    int ext_Booleans, ext_Numbers, ext_Strings;
    bool used_ext_Names = FALSE;

    DEBUG(2, ("align_termtype to(%d:%s), from(%d:%s)", na, to->term_names,
	      nb, from->term_names));

    if (na != 0 || nb != 0) {
	if ((na == nb)		/* check if the arrays are equivalent */
	    &&(to->ext_Booleans == from->ext_Booleans)
	    && (to->ext_Numbers == from->ext_Numbers)
	    && (to->ext_Strings == from->ext_Strings)) {
	    for (n = 0, same = TRUE; n < na; n++) {
		if (strcmp(to->ext_Names[n], from->ext_Names[n])) {
		    same = FALSE;
		    break;
		}
	    }
	    if (same)
		return;
	}
	/*
	 * This is where we pay for having a simple extension representation. 
	 * Allocate a new ext_Names array and merge the two ext_Names arrays
	 * into it, updating to's counts for booleans, etc.  Fortunately we do
	 * this only for the terminfo compiler (tic) and comparer (infocmp).
	 */
	TYPE_MALLOC(char *, (size_t)(na + nb), ext_Names);

	if (to->ext_Strings && (from->ext_Booleans + from->ext_Numbers))
	    adjust_cancels(to, from);

	if (from->ext_Strings && (to->ext_Booleans + to->ext_Numbers))
	    adjust_cancels(from, to);

	ext_Booleans = merge_names(ext_Names,
				   to->ext_Names,
				   to->ext_Booleans,
				   from->ext_Names,
				   from->ext_Booleans);
	ext_Numbers = merge_names(ext_Names + ext_Booleans,
				  to->ext_Names
				  + to->ext_Booleans,
				  to->ext_Numbers,
				  from->ext_Names
				  + from->ext_Booleans,
				  from->ext_Numbers);
	ext_Strings = merge_names(ext_Names + ext_Numbers + ext_Booleans,
				  to->ext_Names
				  + to->ext_Booleans
				  + to->ext_Numbers,
				  to->ext_Strings,
				  from->ext_Names
				  + from->ext_Booleans
				  + from->ext_Numbers,
				  from->ext_Strings);
	/*
	 * Now we must reallocate the Booleans, etc., to allow the data to be
	 * overlaid.
	 */
	if (na != (ext_Booleans + ext_Numbers + ext_Strings)) {
	    realign_data(to, ext_Names, ext_Booleans, ext_Numbers, ext_Strings);
	    FreeIfNeeded(to->ext_Names);
	    to->ext_Names = ext_Names;
	    DEBUG(2, ("realigned %d extended names for '%s' (to)",
		      NUM_EXT_NAMES(to), to->term_names));
	    used_ext_Names = TRUE;
	}
	if (nb != (ext_Booleans + ext_Numbers + ext_Strings)) {
	    nb = (ext_Booleans + ext_Numbers + ext_Strings);
	    realign_data(from, ext_Names, ext_Booleans, ext_Numbers, ext_Strings);
	    TYPE_REALLOC(char *, (size_t) nb, from->ext_Names);
	    memcpy(from->ext_Names, ext_Names, sizeof(char *) * (size_t) nb);
	    DEBUG(2, ("realigned %d extended names for '%s' (from)",
		      NUM_EXT_NAMES(from), from->term_names));
	}
	if (!used_ext_Names)
	    free(ext_Names);
    }
}
#endif

NCURSES_EXPORT(void)
_nc_copy_termtype(TERMTYPE *dst, const TERMTYPE *src)
{
#if NCURSES_XNAMES
    unsigned i;
#endif

    *dst = *src;		/* ...to copy the sizes and string-tables */

    TYPE_MALLOC(NCURSES_SBOOL, NUM_BOOLEANS(dst), dst->Booleans);
    TYPE_MALLOC(short, NUM_NUMBERS(dst), dst->Numbers);
    TYPE_MALLOC(char *, NUM_STRINGS(dst), dst->Strings);

    memcpy(dst->Booleans,
	   src->Booleans,
	   NUM_BOOLEANS(dst) * sizeof(dst->Booleans[0]));
    memcpy(dst->Numbers,
	   src->Numbers,
	   NUM_NUMBERS(dst) * sizeof(dst->Numbers[0]));
    memcpy(dst->Strings,
	   src->Strings,
	   NUM_STRINGS(dst) * sizeof(dst->Strings[0]));

    /* FIXME: we probably should also copy str_table and ext_str_table,
     * but tic and infocmp are not written to exploit that (yet).
     */

#if NCURSES_XNAMES
    if ((i = NUM_EXT_NAMES(src)) != 0) {
	TYPE_MALLOC(char *, i, dst->ext_Names);
	memcpy(dst->ext_Names, src->ext_Names, i * sizeof(char *));
    } else {
	dst->ext_Names = 0;
    }
#endif
}
