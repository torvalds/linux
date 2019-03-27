/****************************************************************************
 * Copyright (c) 1998-2008,2009 Free Software Foundation, Inc.              *
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
 *	comp_hash.c --- Routines to deal with the hashtable of capability
 *			names.
 *
 */

#define USE_TERMLIB 1
#include <curses.priv.h>

#include <tic.h>
#include <hashsize.h>

MODULE_ID("$Id: comp_hash.c,v 1.48 2009/08/08 17:36:21 tom Exp $")

/*
 * Finds the entry for the given string in the hash table if present.
 * Returns a pointer to the entry in the table or 0 if not found.
 */
/* entrypoint used by tack (do not alter) */
NCURSES_EXPORT(struct name_table_entry const *)
_nc_find_entry(const char *string,
	       const HashValue * hash_table)
{
    bool termcap = (hash_table != _nc_get_hash_table(FALSE));
    const HashData *data = _nc_get_hash_info(termcap);
    int hashvalue;
    struct name_table_entry const *ptr = 0;
    struct name_table_entry const *real_table;

    hashvalue = data->hash_of(string);

    if (data->table_data[hashvalue] >= 0) {

	real_table = _nc_get_table(termcap);
	ptr = real_table + data->table_data[hashvalue];
	while (!data->compare_names(ptr->nte_name, string)) {
	    if (ptr->nte_link < 0) {
		ptr = 0;
		break;
	    }
	    ptr = real_table + (ptr->nte_link
				+ data->table_data[data->table_size]);
	}
    }

    return (ptr);
}

/*
 * Finds the entry for the given name with the given type in the given table if
 * present (as distinct from _nc_find_entry, which finds the last entry
 * regardless of type).
 *
 * Returns a pointer to the entry in the table or 0 if not found.
 */
NCURSES_EXPORT(struct name_table_entry const *)
_nc_find_type_entry(const char *string,
		    int type,
		    bool termcap)
{
    struct name_table_entry const *ptr = NULL;
    const HashData *data = _nc_get_hash_info(termcap);
    int hashvalue = data->hash_of(string);

    if (data->table_data[hashvalue] >= 0) {
	const struct name_table_entry *const table = _nc_get_table(termcap);

	ptr = table + data->table_data[hashvalue];
	while (ptr->nte_type != type
	       || !data->compare_names(ptr->nte_name, string)) {
	    if (ptr->nte_link < 0) {
		ptr = 0;
		break;
	    }
	    ptr = table + (ptr->nte_link + data->table_data[data->table_size]);
	}
    }

    return ptr;
}
