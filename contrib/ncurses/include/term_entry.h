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
 *     and: Thomas E. Dickey                        1998-on                 *
 ****************************************************************************/

/* $Id: term_entry.h,v 1.44 2013/05/25 20:13:38 tom Exp $ */

/*
 *	term_entry.h -- interface to entry-manipulation code
 */

#ifndef NCURSES_TERM_ENTRY_H_incl
#define NCURSES_TERM_ENTRY_H_incl 1

#ifdef __cplusplus
extern "C" {
#endif

#include <term.h>

    /*
     * see db_iterator.c - this enumeration lists the places searched for a
     * terminal description and defines the order in which they are searched.
     */
    typedef enum {
	dbdTIC = 0,		/* special, used by tic when writing entry */
#if NCURSES_USE_DATABASE
	dbdEnvOnce,		/* the $TERMINFO environment variable */
	dbdHome,		/* $HOME/.terminfo */
	dbdEnvList,		/* the $TERMINFO_DIRS environment variable */
	dbdCfgList,		/* the compiled-in TERMINFO_DIRS value */
	dbdCfgOnce,		/* the compiled-in TERMINFO value */
#endif
#if NCURSES_USE_TERMCAP
	dbdEnvOnce2,		/* the $TERMCAP environment variable */
	dbdEnvList2,		/* the $TERMPATH environment variable */
	dbdCfgList2,		/* the compiled-in TERMPATH */
#endif
	dbdLAST
    } DBDIRS;

#define MAX_USES	32
#define MAX_CROSSLINKS	16

    typedef struct entry {
	TERMTYPE tterm;
	unsigned nuses;
	struct {
	    char *name;
	    struct entry *link;
	    long line;
	} uses[MAX_USES];
	int ncrosslinks;
	struct entry *crosslinks[MAX_CROSSLINKS];
	long cstart, cend;
	long startline;
	struct entry *next;
	struct entry *last;
    } ENTRY;
/* *INDENT-OFF* */
#if NCURSES_XNAMES
#define NUM_BOOLEANS(tp) (tp)->num_Booleans
#define NUM_NUMBERS(tp)  (tp)->num_Numbers
#define NUM_STRINGS(tp)  (tp)->num_Strings
#define EXT_NAMES(tp,i,limit,index,table) (i >= limit) ? tp->ext_Names[index] : table[i]
#else
#define NUM_BOOLEANS(tp) BOOLCOUNT
#define NUM_NUMBERS(tp)  NUMCOUNT
#define NUM_STRINGS(tp)  STRCOUNT
#define EXT_NAMES(tp,i,limit,index,table) table[i]
#endif

#define NUM_EXT_NAMES(tp) (unsigned) ((tp)->ext_Booleans + (tp)->ext_Numbers + (tp)->ext_Strings)

#define for_each_boolean(n,tp) for(n = 0; n < NUM_BOOLEANS(tp); n++)
#define for_each_number(n,tp)  for(n = 0; n < NUM_NUMBERS(tp);  n++)
#define for_each_string(n,tp)  for(n = 0; n < NUM_STRINGS(tp);  n++)

#if NCURSES_XNAMES
#define for_each_ext_boolean(n,tp) for(n = BOOLCOUNT; n < NUM_BOOLEANS(tp); n++)
#define for_each_ext_number(n,tp)  for(n = NUMCOUNT; n < NUM_NUMBERS(tp);  n++)
#define for_each_ext_string(n,tp)  for(n = STRCOUNT; n < NUM_STRINGS(tp);  n++)
#endif

#define ExtBoolname(tp,i,names) EXT_NAMES(tp, i, BOOLCOUNT, (i - (tp->num_Booleans - tp->ext_Booleans)), names)
#define ExtNumname(tp,i,names)  EXT_NAMES(tp, i, NUMCOUNT, (i - (tp->num_Numbers - tp->ext_Numbers)) + tp->ext_Booleans, names)
#define ExtStrname(tp,i,names)  EXT_NAMES(tp, i, STRCOUNT, (i - (tp->num_Strings - tp->ext_Strings)) + (tp->ext_Numbers + tp->ext_Booleans), names)

extern NCURSES_EXPORT_VAR(ENTRY *) _nc_head;
extern NCURSES_EXPORT_VAR(ENTRY *) _nc_tail;
#define for_entry_list(qp)	for (qp = _nc_head; qp; qp = qp->next)

#define MAX_LINE	132

#define NULLHOOK        (bool(*)(ENTRY *))0

/*
 * Note that WANTED and PRESENT are not simple inverses!  If a capability
 * has been explicitly cancelled, it's not considered WANTED.
 */
#define WANTED(s)	((s) == ABSENT_STRING)
#define PRESENT(s)	(((s) != ABSENT_STRING) && ((s) != CANCELLED_STRING))

#define ANDMISSING(p,q) \
		{if (PRESENT(p) && !PRESENT(q)) _nc_warning(#p " but no " #q);}

#define PAIRED(p,q) \
		{ \
		if (PRESENT(q) && !PRESENT(p)) \
			_nc_warning(#q " but no " #p); \
		if (PRESENT(p) && !PRESENT(q)) \
			_nc_warning(#p " but no " #q); \
		}

/* alloc_entry.c: elementary allocation code */
extern NCURSES_EXPORT(ENTRY *) _nc_copy_entry (ENTRY *oldp);
extern NCURSES_EXPORT(char *) _nc_save_str (const char *const);
extern NCURSES_EXPORT(void) _nc_init_entry (TERMTYPE *const);
extern NCURSES_EXPORT(void) _nc_merge_entry (TERMTYPE *const, TERMTYPE *const);
extern NCURSES_EXPORT(void) _nc_wrap_entry (ENTRY *const, bool);

/* alloc_ttype.c: elementary allocation code */
extern NCURSES_EXPORT(void) _nc_align_termtype (TERMTYPE *, TERMTYPE *);
extern NCURSES_EXPORT(void) _nc_copy_termtype (TERMTYPE *, const TERMTYPE *);

/* free_ttype.c: elementary allocation code */
extern NCURSES_EXPORT(void) _nc_free_termtype (TERMTYPE *);

/* lib_acs.c */
extern NCURSES_EXPORT(void) _nc_init_acs (void);	/* corresponds to traditional 'init_acs()' */

/* lib_termcap.c: trim sgr0 string for termcap users */
extern NCURSES_EXPORT(char *) _nc_trim_sgr0 (TERMTYPE *);

/* parse_entry.c: entry-parsing code */
#if NCURSES_XNAMES
extern NCURSES_EXPORT_VAR(bool) _nc_user_definable;
extern NCURSES_EXPORT_VAR(bool) _nc_disable_period;
#endif
extern NCURSES_EXPORT(int) _nc_parse_entry (ENTRY *, int, bool);
extern NCURSES_EXPORT(int) _nc_capcmp (const char *, const char *);

/* write_entry.c: writing an entry to the file system */
extern NCURSES_EXPORT(void) _nc_set_writedir (char *);
extern NCURSES_EXPORT(void) _nc_write_entry (TERMTYPE *const);

/* comp_parse.c: entry list handling */
extern NCURSES_EXPORT(void) _nc_read_entry_source (FILE*, char*, int, bool, bool (*)(ENTRY*));
extern NCURSES_EXPORT(bool) _nc_entry_match (char *, char *);
extern NCURSES_EXPORT(int) _nc_resolve_uses (bool); /* obs 20040705 */
extern NCURSES_EXPORT(int) _nc_resolve_uses2 (bool, bool);
extern NCURSES_EXPORT(void) _nc_free_entries (ENTRY *);
extern NCURSES_IMPEXP void NCURSES_API (*_nc_check_termtype)(TERMTYPE *); /* obs 20040705 */
extern NCURSES_IMPEXP void NCURSES_API (*_nc_check_termtype2)(TERMTYPE *, bool);

/* trace_xnames.c */
extern NCURSES_EXPORT(void) _nc_trace_xnames (TERMTYPE *);
/* *INDENT-ON* */

#ifdef __cplusplus
}
#endif
#endif				/* NCURSES_TERM_ENTRY_H_incl */
