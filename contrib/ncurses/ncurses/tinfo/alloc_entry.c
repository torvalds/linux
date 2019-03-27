/****************************************************************************
 * Copyright (c) 1998-2012,2013 Free Software Foundation, Inc.              *
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
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 *     and: Thomas E. Dickey                        1996-on                 *
 ****************************************************************************/

/*
 * alloc_entry.c -- allocation functions for terminfo entries
 *
 *	_nc_copy_entry()
 *	_nc_init_entry()
 *	_nc_merge_entry()
 *	_nc_save_str()
 *	_nc_wrap_entry()
 *
 */

#include <curses.priv.h>

#include <tic.h>

MODULE_ID("$Id: alloc_entry.c,v 1.58 2013/08/17 19:20:38 tom Exp $")

#define ABSENT_OFFSET    -1
#define CANCELLED_OFFSET -2

#define MAX_STRTAB	4096	/* documented maximum entry size */

static char *stringbuf;		/* buffer for string capabilities */
static size_t next_free;	/* next free character in stringbuf */

NCURSES_EXPORT(void)
_nc_init_entry(TERMTYPE *const tp)
/* initialize a terminal type data block */
{
#if NO_LEAKS
    if (tp == 0) {
	if (stringbuf != 0) {
	    FreeAndNull(stringbuf);
	}
	return;
    }
#endif

    if (stringbuf == 0)
	TYPE_MALLOC(char, (size_t) MAX_STRTAB, stringbuf);

    next_free = 0;

    _nc_init_termtype(tp);
}

NCURSES_EXPORT(ENTRY *)
_nc_copy_entry(ENTRY * oldp)
{
    ENTRY *newp = typeCalloc(ENTRY, 1);

    if (newp != 0) {
	*newp = *oldp;
	_nc_copy_termtype(&(newp->tterm), &(oldp->tterm));
    }
    return newp;
}

/* save a copy of string in the string buffer */
NCURSES_EXPORT(char *)
_nc_save_str(const char *const string)
{
    char *result = 0;
    size_t old_next_free = next_free;
    size_t len = strlen(string) + 1;

    if (len == 1 && next_free != 0) {
	/*
	 * Cheat a little by making an empty string point to the end of the
	 * previous string.
	 */
	if (next_free < MAX_STRTAB) {
	    result = (stringbuf + next_free - 1);
	}
    } else if (next_free + len < MAX_STRTAB) {
	_nc_STRCPY(&stringbuf[next_free], string, MAX_STRTAB);
	DEBUG(7, ("Saved string %s", _nc_visbuf(string)));
	DEBUG(7, ("at location %d", (int) next_free));
	next_free += len;
	result = (stringbuf + old_next_free);
    } else {
	_nc_warning("Too much data, some is lost: %s", string);
    }
    return result;
}

NCURSES_EXPORT(void)
_nc_wrap_entry(ENTRY * const ep, bool copy_strings)
/* copy the string parts to allocated storage, preserving pointers to it */
{
    int offsets[MAX_ENTRY_SIZE / sizeof(short)];
    int useoffsets[MAX_USES];
    unsigned i, n;
    unsigned nuses = ep->nuses;
    TERMTYPE *tp = &(ep->tterm);

    if (copy_strings) {
	next_free = 0;		/* clear static storage */

	/* copy term_names, Strings, uses */
	tp->term_names = _nc_save_str(tp->term_names);
	for_each_string(i, tp) {
	    if (tp->Strings[i] != ABSENT_STRING &&
		tp->Strings[i] != CANCELLED_STRING) {
		tp->Strings[i] = _nc_save_str(tp->Strings[i]);
	    }
	}

	for (i = 0; i < nuses; i++) {
	    if (ep->uses[i].name == 0) {
		ep->uses[i].name = _nc_save_str(ep->uses[i].name);
	    }
	}

	free(tp->str_table);
    }

    assert(tp->term_names >= stringbuf);
    n = (unsigned) (tp->term_names - stringbuf);
    for_each_string(i, &(ep->tterm)) {
	if (i < SIZEOF(offsets)) {
	    if (tp->Strings[i] == ABSENT_STRING) {
		offsets[i] = ABSENT_OFFSET;
	    } else if (tp->Strings[i] == CANCELLED_STRING) {
		offsets[i] = CANCELLED_OFFSET;
	    } else {
		offsets[i] = (int) (tp->Strings[i] - stringbuf);
	    }
	}
    }

    for (i = 0; i < nuses; i++) {
	if (ep->uses[i].name == 0)
	    useoffsets[i] = ABSENT_OFFSET;
	else
	    useoffsets[i] = (int) (ep->uses[i].name - stringbuf);
    }

    TYPE_MALLOC(char, next_free, tp->str_table);
    (void) memcpy(tp->str_table, stringbuf, next_free);

    tp->term_names = tp->str_table + n;
    for_each_string(i, &(ep->tterm)) {
	if (i < SIZEOF(offsets)) {
	    if (offsets[i] == ABSENT_OFFSET) {
		tp->Strings[i] = ABSENT_STRING;
	    } else if (offsets[i] == CANCELLED_OFFSET) {
		tp->Strings[i] = CANCELLED_STRING;
	    } else {
		tp->Strings[i] = tp->str_table + offsets[i];
	    }
	}
    }

#if NCURSES_XNAMES
    if (!copy_strings) {
	if ((n = (unsigned) NUM_EXT_NAMES(tp)) != 0) {
	    if (n < SIZEOF(offsets)) {
		size_t length = 0;
		size_t offset;
		for (i = 0; i < n; i++) {
		    length += strlen(tp->ext_Names[i]) + 1;
		    offsets[i] = (int) (tp->ext_Names[i] - stringbuf);
		}
		TYPE_MALLOC(char, length, tp->ext_str_table);
		for (i = 0, offset = 0; i < n; i++) {
		    tp->ext_Names[i] = tp->ext_str_table + offset;
		    _nc_STRCPY(tp->ext_Names[i],
			       stringbuf + offsets[i],
			       length - offset);
		    offset += strlen(tp->ext_Names[i]) + 1;
		}
	    }
	}
    }
#endif

    for (i = 0; i < nuses; i++) {
	if (useoffsets[i] == ABSENT_OFFSET)
	    ep->uses[i].name = 0;
	else
	    ep->uses[i].name = (tp->str_table + useoffsets[i]);
    }
}

NCURSES_EXPORT(void)
_nc_merge_entry(TERMTYPE *const to, TERMTYPE *const from)
/* merge capabilities from `from' entry into `to' entry */
{
    unsigned i;

#if NCURSES_XNAMES
    _nc_align_termtype(to, from);
#endif
    for_each_boolean(i, from) {
	if (to->Booleans[i] != (char) CANCELLED_BOOLEAN) {
	    int mergebool = from->Booleans[i];

	    if (mergebool == CANCELLED_BOOLEAN)
		to->Booleans[i] = FALSE;
	    else if (mergebool == TRUE)
		to->Booleans[i] = (char) mergebool;
	}
    }

    for_each_number(i, from) {
	if (to->Numbers[i] != CANCELLED_NUMERIC) {
	    short mergenum = from->Numbers[i];

	    if (mergenum == CANCELLED_NUMERIC)
		to->Numbers[i] = ABSENT_NUMERIC;
	    else if (mergenum != ABSENT_NUMERIC)
		to->Numbers[i] = mergenum;
	}
    }

    /*
     * Note: the copies of strings this makes don't have their own
     * storage.  This is OK right now, but will be a problem if we
     * we ever want to deallocate entries.
     */
    for_each_string(i, from) {
	if (to->Strings[i] != CANCELLED_STRING) {
	    char *mergestring = from->Strings[i];

	    if (mergestring == CANCELLED_STRING)
		to->Strings[i] = ABSENT_STRING;
	    else if (mergestring != ABSENT_STRING)
		to->Strings[i] = mergestring;
	}
    }
}

#if NO_LEAKS
NCURSES_EXPORT(void)
_nc_alloc_entry_leaks(void)
{
    if (stringbuf != 0) {
	FreeAndNull(stringbuf);
    }
    next_free = 0;
}
#endif
