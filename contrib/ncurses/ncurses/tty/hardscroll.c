/****************************************************************************
 * Copyright (c) 1998-2010,2012 Free Software Foundation, Inc.              *
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
 *     and: Alexander V Lukyanov                    1997-1998               *
 ****************************************************************************/

/******************************************************************************

NAME
   hardscroll.c -- hardware-scrolling optimization for ncurses

SYNOPSIS
   void _nc_scroll_optimize(void)

DESCRIPTION
			OVERVIEW

This algorithm for computes optimum hardware scrolling to transform an
old screen (curscr) into a new screen (newscr) via vertical line moves.

Because the screen has a `grain' (there are insert/delete/scroll line
operations but no insert/delete/scroll column operations), it is efficient
break the update algorithm into two pieces: a first stage that does only line
moves, optimizing the end product of user-invoked insertions, deletions, and
scrolls; and a second phase (corresponding to the present doupdate code in
ncurses) that does only line transformations.

The common case we want hardware scrolling for is to handle line insertions
and deletions in screen-oriented text-editors.  This two-stage approach will
accomplish that at a low computation and code-size cost.

			LINE-MOVE COMPUTATION

Now, to a discussion of the line-move computation.

For expository purposes, consider the screen lines to be represented by
integers 0..23 (with the understanding that the value of 23 may vary).
Let a new line introduced by insertion, scrolling, or at the bottom of
the screen following a line delete be given the index -1.

Assume that the real screen starts with lines 0..23.  Now, we have
the following possible line-oriented operations on the screen:

Insertion: inserts a line at a given screen row, forcing all lines below
to scroll forward.  The last screen line is lost.  For example, an insertion
at line 5 would produce: 0..4 -1 5..23.

Deletion: deletes a line at a given screen row, forcing all lines below
to scroll forward.  The last screen line is made new.  For example, a deletion
at line 7 would produce: 0..6 8..23 -1.

Scroll up: move a range of lines up 1.  The bottom line of the range
becomes new.  For example, scrolling up the region from 9 to 14 will
produce 0..8 10..14 -1 15..23.

Scroll down: move a range of lines down 1.  The top line of the range
becomes new.  For example, scrolling down the region from 12 to 16 will produce
0..11 -1 12..15 17..23.

Now, an obvious property of all these operations is that they preserve the
order of old lines, though not their position in the sequence.

The key trick of this algorithm is that the original line indices described
above are actually maintained as _line[].oldindex fields in the window
structure, and stick to each line through scroll and insert/delete operations.

Thus, it is possible at update time to look at the oldnum fields and compute
an optimal set of il/dl/scroll operations that will take the real screen
lines to the virtual screen lines.  Once these vertical moves have been done,
we can hand off to the second stage of the update algorithm, which does line
transformations.

Note that the move computation does not need to have the full generality
of a diff algorithm (which it superficially resembles) because lines cannot
be moved out of order.

			THE ALGORITHM

The scrolling is done in two passes. The first pass is from top to bottom
scroling hunks UP. The second one is from bottom to top scrolling hunks DOWN.
Obviously enough, no lines to be scrolled will be destroyed. (lav)

HOW TO TEST THIS:

Use the following production:

hardscroll: hardscroll.c
	$(CC) -g -DSCROLLDEBUG hardscroll.c -o hardscroll

Then just type scramble vectors and watch.  The following test loads are
a representative sample of cases:

-----------------------------  CUT HERE ------------------------------------
# No lines moved
 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23
#
# A scroll up
 1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 -1
#
# A scroll down
-1  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22
#
# An insertion (after line 12)
 0  1  2  3  4  5  6  7  8  9 10 11 12 -1 13 14 15 16 17 18 19 20 21 22
#
# A simple deletion (line 10)
 0  1  2  3  4  5  6  7  8  9  11 12 13 14 15 16 17 18 19 20 21 22 23 -1
#
# A more complex case
-1 -1 -1 -1 -1  3  4  5  6  7  -1 -1  8  9 10 11 12 13 14 15 16 17 -1 -1
-----------------------------  CUT HERE ------------------------------------

AUTHOR
    Eric S. Raymond <esr@snark.thyrsus.com>, November 1994
    New algorithm by Alexander V. Lukyanov <lav@yars.free.net>, Aug 1997

*****************************************************************************/

#include <curses.priv.h>

MODULE_ID("$Id: hardscroll.c,v 1.51 2012/10/17 09:01:10 tom Exp $")

#if defined(SCROLLDEBUG) || defined(HASHDEBUG)

# undef screen_lines
# define screen_lines(sp) MAXLINES
NCURSES_EXPORT_VAR (int)
  oldnums[MAXLINES];
# define OLDNUM(sp,n)	oldnums[n]
# define _tracef	printf
# undef TR
# define TR(n, a)	if (_nc_tracing & (n)) { _tracef a ; putchar('\n'); }

extern				NCURSES_EXPORT_VAR(unsigned) _nc_tracing;

#else /* no debug */

/* OLDNUM(n) indicates which line will be shifted to the position n.
   if OLDNUM(n) == _NEWINDEX, then the line n in new, not shifted from
   somewhere. */
NCURSES_EXPORT_VAR (int *)
  _nc_oldnums = 0;		/* obsolete: keep for ABI compat */

# if USE_HASHMAP
#  define oldnums(sp)   (sp)->_oldnum_list
#  define OLDNUM(sp,n)	oldnums(sp)[n]
# else /* !USE_HASHMAP */
#  define OLDNUM(sp,n)	NewScreen(sp)->_line[n].oldindex
# endif	/* !USE_HASHMAP */

#define OLDNUM_SIZE(sp) (sp)->_oldnum_size

#endif /* defined(SCROLLDEBUG) || defined(HASHDEBUG) */

NCURSES_EXPORT(void)
NCURSES_SP_NAME(_nc_scroll_optimize) (NCURSES_SP_DCL0)
/* scroll optimization to transform curscr to newscr */
{
    int i;
    int start, end, shift;

    TR(TRACE_ICALLS, (T_CALLED("_nc_scroll_optimize(%p)"), (void *) SP_PARM));

#if !defined(SCROLLDEBUG) && !defined(HASHDEBUG)
#if USE_HASHMAP
    /* get enough storage */
    assert(OLDNUM_SIZE(SP_PARM) >= 0);
    assert(screen_lines(SP_PARM) > 0);
    if ((oldnums(SP_PARM) == 0)
	|| (OLDNUM_SIZE(SP_PARM) < screen_lines(SP_PARM))) {
	int need_lines = ((OLDNUM_SIZE(SP_PARM) < screen_lines(SP_PARM))
			  ? screen_lines(SP_PARM)
			  : OLDNUM_SIZE(SP_PARM));
	int *new_oldnums = typeRealloc(int,
				       (size_t) need_lines,
				       oldnums(SP_PARM));
	if (!new_oldnums)
	    return;
	oldnums(SP_PARM) = new_oldnums;
	OLDNUM_SIZE(SP_PARM) = need_lines;
    }
    /* calculate the indices */
    NCURSES_SP_NAME(_nc_hash_map) (NCURSES_SP_ARG);
#endif
#endif /* !defined(SCROLLDEBUG) && !defined(HASHDEBUG) */

#ifdef TRACE
    if (USE_TRACEF(TRACE_UPDATE | TRACE_MOVE)) {
	NCURSES_SP_NAME(_nc_linedump) (NCURSES_SP_ARG);
	_nc_unlock_global(tracef);
    }
#endif /* TRACE */

    /* pass 1 - from top to bottom scrolling up */
    for (i = 0; i < screen_lines(SP_PARM);) {
	while (i < screen_lines(SP_PARM)
	       && (OLDNUM(SP_PARM, i) == _NEWINDEX || OLDNUM(SP_PARM, i) <= i))
	    i++;
	if (i >= screen_lines(SP_PARM))
	    break;

	shift = OLDNUM(SP_PARM, i) - i;		/* shift > 0 */
	start = i;

	i++;
	while (i < screen_lines(SP_PARM)
	       && OLDNUM(SP_PARM, i) != _NEWINDEX
	       && OLDNUM(SP_PARM, i) - i == shift)
	    i++;
	end = i - 1 + shift;

	TR(TRACE_UPDATE | TRACE_MOVE, ("scroll [%d, %d] by %d", start, end, shift));
#if !defined(SCROLLDEBUG) && !defined(HASHDEBUG)
	if (NCURSES_SP_NAME(_nc_scrolln) (NCURSES_SP_ARGx
					  shift,
					  start,
					  end,
					  screen_lines(SP_PARM) - 1) == ERR) {
	    TR(TRACE_UPDATE | TRACE_MOVE, ("unable to scroll"));
	    continue;
	}
#endif /* !defined(SCROLLDEBUG) && !defined(HASHDEBUG) */
    }

    /* pass 2 - from bottom to top scrolling down */
    for (i = screen_lines(SP_PARM) - 1; i >= 0;) {
	while (i >= 0
	       && (OLDNUM(SP_PARM, i) == _NEWINDEX
		   || OLDNUM(SP_PARM, i) >= i)) {
	    i--;
	}
	if (i < 0)
	    break;

	shift = OLDNUM(SP_PARM, i) - i;		/* shift < 0 */
	end = i;

	i--;
	while (i >= 0
	       && OLDNUM(SP_PARM, i) != _NEWINDEX
	       && OLDNUM(SP_PARM, i) - i == shift) {
	    i--;
	}
	start = i + 1 - (-shift);

	TR(TRACE_UPDATE | TRACE_MOVE, ("scroll [%d, %d] by %d", start, end, shift));
#if !defined(SCROLLDEBUG) && !defined(HASHDEBUG)
	if (NCURSES_SP_NAME(_nc_scrolln) (NCURSES_SP_ARGx
					  shift,
					  start,
					  end,
					  screen_lines(SP_PARM) - 1) == ERR) {
	    TR(TRACE_UPDATE | TRACE_MOVE, ("unable to scroll"));
	    continue;
	}
#endif /* !defined(SCROLLDEBUG) && !defined(HASHDEBUG) */
    }
    TR(TRACE_ICALLS, (T_RETURN("")));
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(void)
_nc_scroll_optimize(void)
{
    NCURSES_SP_NAME(_nc_scroll_optimize) (CURRENT_SCREEN);
}
#endif

#if defined(TRACE) || defined(SCROLLDEBUG) || defined(HASHDEBUG)
NCURSES_EXPORT(void)
NCURSES_SP_NAME(_nc_linedump) (NCURSES_SP_DCL0)
/* dump the state of the real and virtual oldnum fields */
{
    int n;
    char *buf = 0;
    size_t want = ((size_t) screen_lines(SP_PARM) + 1) * 4;

    if ((buf = typeMalloc(char, want)) != 0) {

	*buf = '\0';
	for (n = 0; n < screen_lines(SP_PARM); n++)
	    _nc_SPRINTF(buf + strlen(buf),
			_nc_SLIMIT(want - strlen(buf))
			" %02d", OLDNUM(SP_PARM, n));
	TR(TRACE_UPDATE | TRACE_MOVE, ("virt %s", buf));
	free(buf);
    }
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(void)
_nc_linedump(void)
{
    NCURSES_SP_NAME(_nc_linedump) (CURRENT_SCREEN);
}
#endif

#endif /* defined(TRACE) || defined(SCROLLDEBUG) */

#ifdef SCROLLDEBUG

int
main(int argc GCC_UNUSED, char *argv[]GCC_UNUSED)
{
    char line[BUFSIZ], *st;

#ifdef TRACE
    _nc_tracing = TRACE_MOVE;
#endif
    for (;;) {
	int n;

	for (n = 0; n < screen_lines; n++)
	    oldnums[n] = _NEWINDEX;

	/* grab the test vector */
	if (fgets(line, sizeof(line), stdin) == (char *) NULL)
	    exit(EXIT_SUCCESS);

	/* parse it */
	n = 0;
	if (line[0] == '#') {
	    (void) fputs(line, stderr);
	    continue;
	}
	st = strtok(line, " ");
	do {
	    oldnums[n++] = atoi(st);
	} while
	    ((st = strtok((char *) NULL, " ")) != 0);

	/* display it */
	(void) fputs("Initial input:\n", stderr);
	_nc_linedump();

	_nc_scroll_optimize();
    }
}

#endif /* SCROLLDEBUG */

/* hardscroll.c ends here */
