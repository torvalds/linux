/****************************************************************************
 * Copyright (c) 2006 Free Software Foundation, Inc.                        *
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
 *  Author: Thomas E. Dickey                        2006                    *
 ****************************************************************************/

/*
 * $Id: hashed_db.h,v 1.5 2006/08/19 15:58:34 tom Exp $
 */

#ifndef HASHED_DB_H
#define HASHED_DB_H 1

#include <curses.h>

#if USE_HASHED_DB

#include <db.h>

#ifndef DBN_SUFFIX
#define DBM_SUFFIX ".db"
#endif

#ifdef DB_VERSION_MAJOR
#define HASHED_DB_API DB_VERSION_MAJOR
#else
#define HASHED_DB_API 1		/* e.g., db 1.8.5 */
#endif

extern NCURSES_EXPORT(DB *) _nc_db_open(const char * /* path */, bool /* modify */);
extern NCURSES_EXPORT(bool) _nc_db_have_data(DBT * /* key */, DBT * /* data */, char ** /* buffer */, int * /* size */);
extern NCURSES_EXPORT(bool) _nc_db_have_index(DBT * /* key */, DBT * /* data */, char ** /* buffer */, int * /* size */);
extern NCURSES_EXPORT(int) _nc_db_close(DB * /* db */);
extern NCURSES_EXPORT(int) _nc_db_first(DB * /* db */, DBT * /* key */, DBT * /* data */);
extern NCURSES_EXPORT(int) _nc_db_next(DB * /* db */, DBT * /* key */, DBT * /* data */);
extern NCURSES_EXPORT(int) _nc_db_get(DB * /* db */, DBT * /* key */, DBT * /* data */);
extern NCURSES_EXPORT(int) _nc_db_put(DB * /* db */, DBT * /* key */, DBT * /* data */);

#endif

#endif /* HASHED_DB_H */
