/****************************************************************************
 * Copyright (c) 1998-2010,2011 Free Software Foundation, Inc.              *
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
 ****************************************************************************/

/******************************************************************************

NAME
   hashmap.c -- fill in scramble vector based on text hashes

SYNOPSIS
   void _nc_hash_map(void)

DESCRIPTION:
   This code attempts to recognize pairs of old and new lines in the physical
and virtual screens.  When a line pair is recognized, the old line index is
placed in the oldindex member of the virtual screen line, to be used by the
vertical-motion optimizer portion of the update logic (see hardscroll.c).

   Line pairs are recognized by applying a modified Heckel's algorithm,
sped up by hashing.  If a line hash is unique in both screens, those
lines must be a pair. Then if the lines just before or after the pair
are the same or similar, they are a pair too.

   We don't worry about false pairs produced by hash collisions, on the
assumption that such cases are rare and will only make the latter stages
of update less efficient, not introduce errors.

HOW TO TEST THIS:

Use the following production:

hashmap: hashmap.c
	$(CC) -g -DHASHDEBUG hashmap.c hardscroll.c ../objects/lib_trace.o -o hashmap

AUTHOR
    Eric S. Raymond <esr@snark.thyrsus.com>, May 1996
    Bug fixes and improvements by Alexander V. Lukyanov <lav@yars.free.net>, 1997

*****************************************************************************/

#include <curses.priv.h>

#ifndef CUR
#define CUR SP_TERMTYPE
#endif

MODULE_ID("$Id: hashmap.c,v 1.63 2011/10/22 16:34:50 tom Exp $")

#ifdef HASHDEBUG

# define _tracef	printf
# undef TR
# define TR(n, a)	if (_nc_tracing & (n)) { _tracef a ; putchar('\n'); }
# undef screen_lines
# define screen_lines MAXLINES
# define TEXTWIDTH	1
int oldnums[MAXLINES], reallines[MAXLINES];
static NCURSES_CH_T oldtext[MAXLINES][TEXTWIDTH];
static NCURSES_CH_T newtext[MAXLINES][TEXTWIDTH];
# define OLDNUM(sp,n)	oldnums[n]
# define OLDTEXT(sp,n)	oldtext[n]
# define NEWTEXT(sp,m)	newtext[m]
# define PENDING(sp,n)  1

#else /* !HASHDEBUG */

# define OLDNUM(sp,n)	(sp)->_oldnum_list[n]
# define OLDTEXT(sp,n)	CurScreen(sp)->_line[n].text
# define NEWTEXT(sp,m)	NewScreen(sp)->_line[m].text
# define TEXTWIDTH(sp)	(CurScreen(sp)->_maxx + 1)
# define PENDING(sp,n)  (NewScreen(sp)->_line[n].firstchar != _NOCHANGE)

#endif /* !HASHDEBUG */

#define oldhash(sp)	((sp)->oldhash)
#define newhash(sp)	((sp)->newhash)
#define hashtab(sp)	((sp)->hashtab)
#define lines_alloc(sp)	((sp)->hashtab_len)

#if USE_WIDEC_SUPPORT
#define HASH_VAL(ch) (ch.chars[0])
#else
#define HASH_VAL(ch) (ch)
#endif

static const NCURSES_CH_T blankchar = NewChar(BLANK_TEXT);

static NCURSES_INLINE unsigned long
hash(SCREEN *sp, NCURSES_CH_T * text)
{
    int i;
    NCURSES_CH_T ch;
    unsigned long result = 0;
    for (i = TEXTWIDTH(sp); i > 0; i--) {
	ch = *text++;
	result += (result << 5) + (unsigned long) HASH_VAL(ch);
    }
    return result;
}

/* approximate update cost */
static int
update_cost(SCREEN *sp, NCURSES_CH_T * from, NCURSES_CH_T * to)
{
    int cost = 0;
    int i;

    for (i = TEXTWIDTH(sp); i > 0; i--, from++, to++)
	if (!(CharEq(*from, *to)))
	    cost++;

    return cost;
}

static int
update_cost_from_blank(SCREEN *sp, NCURSES_CH_T * to)
{
    int cost = 0;
    int i;
    NCURSES_CH_T blank = blankchar;

    if (back_color_erase)
	SetPair(blank, GetPair(stdscr->_nc_bkgd));

    for (i = TEXTWIDTH(sp); i > 0; i--, to++)
	if (!(CharEq(blank, *to)))
	    cost++;

    return cost;
}

/*
 * Returns true when moving line 'from' to line 'to' seems to be cost
 * effective. 'blank' indicates whether the line 'to' would become blank.
 */
static NCURSES_INLINE bool
cost_effective(SCREEN *sp, const int from, const int to, const int blank)
{
    int new_from;

    if (from == to)
	return FALSE;

    new_from = OLDNUM(sp, from);
    if (new_from == _NEWINDEX)
	new_from = from;

    /*
     * On the left side of >= is the cost before moving;
     * on the right side -- cost after moving.
     */
    return (((blank ? update_cost_from_blank(sp, NEWTEXT(sp, to))
	      : update_cost(sp, OLDTEXT(sp, to), NEWTEXT(sp, to)))
	     + update_cost(sp, OLDTEXT(sp, new_from), NEWTEXT(sp, from)))
	    >= ((new_from == from ? update_cost_from_blank(sp, NEWTEXT(sp, from))
		 : update_cost(sp, OLDTEXT(sp, new_from), NEWTEXT(sp, from)))
		+ update_cost(sp, OLDTEXT(sp, from), NEWTEXT(sp, to))))
	? TRUE : FALSE;
}

static void
grow_hunks(SCREEN *sp)
{
    int start, end, shift;
    int back_limit, forward_limit;	/* limits for cells to fill */
    int back_ref_limit, forward_ref_limit;	/* limits for refrences */
    int i;
    int next_hunk;

    /*
     * This is tricky part.  We have unique pairs to use as anchors.
     * Use these to deduce the presence of spans of identical lines.
     */
    back_limit = 0;
    back_ref_limit = 0;

    i = 0;
    while (i < screen_lines(sp) && OLDNUM(sp, i) == _NEWINDEX)
	i++;
    for (; i < screen_lines(sp); i = next_hunk) {
	start = i;
	shift = OLDNUM(sp, i) - i;

	/* get forward limit */
	i = start + 1;
	while (i < screen_lines(sp)
	       && OLDNUM(sp, i) != _NEWINDEX
	       && OLDNUM(sp, i) - i == shift)
	    i++;
	end = i;
	while (i < screen_lines(sp) && OLDNUM(sp, i) == _NEWINDEX)
	    i++;
	next_hunk = i;
	forward_limit = i;
	if (i >= screen_lines(sp) || OLDNUM(sp, i) >= i)
	    forward_ref_limit = i;
	else
	    forward_ref_limit = OLDNUM(sp, i);

	i = start - 1;
	/* grow back */
	if (shift < 0)
	    back_limit = back_ref_limit + (-shift);
	while (i >= back_limit) {
	    if (newhash(sp)[i] == oldhash(sp)[i + shift]
		|| cost_effective(sp, i + shift, i, shift < 0)) {
		OLDNUM(sp, i) = i + shift;
		TR(TRACE_UPDATE | TRACE_MOVE,
		   ("connected new line %d to old line %d (backward continuation)",
		    i, i + shift));
	    } else {
		TR(TRACE_UPDATE | TRACE_MOVE,
		   ("not connecting new line %d to old line %d (backward continuation)",
		    i, i + shift));
		break;
	    }
	    i--;
	}

	i = end;
	/* grow forward */
	if (shift > 0)
	    forward_limit = forward_ref_limit - shift;
	while (i < forward_limit) {
	    if (newhash(sp)[i] == oldhash(sp)[i + shift]
		|| cost_effective(sp, i + shift, i, shift > 0)) {
		OLDNUM(sp, i) = i + shift;
		TR(TRACE_UPDATE | TRACE_MOVE,
		   ("connected new line %d to old line %d (forward continuation)",
		    i, i + shift));
	    } else {
		TR(TRACE_UPDATE | TRACE_MOVE,
		   ("not connecting new line %d to old line %d (forward continuation)",
		    i, i + shift));
		break;
	    }
	    i++;
	}

	back_ref_limit = back_limit = i;
	if (shift > 0)
	    back_ref_limit += shift;
    }
}

NCURSES_EXPORT(void)
NCURSES_SP_NAME(_nc_hash_map) (NCURSES_SP_DCL0)
{
    HASHMAP *hsp;
    register int i;
    int start, shift, size;

    if (screen_lines(SP_PARM) > lines_alloc(SP_PARM)) {
	if (hashtab(SP_PARM))
	    free(hashtab(SP_PARM));
	hashtab(SP_PARM) = typeMalloc(HASHMAP,
				      ((size_t) screen_lines(SP_PARM) + 1) * 2);
	if (!hashtab(SP_PARM)) {
	    if (oldhash(SP_PARM)) {
		FreeAndNull(oldhash(SP_PARM));
	    }
	    lines_alloc(SP_PARM) = 0;
	    return;
	}
	lines_alloc(SP_PARM) = screen_lines(SP_PARM);
    }

    if (oldhash(SP_PARM) && newhash(SP_PARM)) {
	/* re-hash only changed lines */
	for (i = 0; i < screen_lines(SP_PARM); i++) {
	    if (PENDING(SP_PARM, i))
		newhash(SP_PARM)[i] = hash(SP_PARM, NEWTEXT(SP_PARM, i));
	}
    } else {
	/* re-hash all */
	if (oldhash(SP_PARM) == 0)
	    oldhash(SP_PARM) = typeCalloc(unsigned long,
					    (size_t) screen_lines(SP_PARM));
	if (newhash(SP_PARM) == 0)
	    newhash(SP_PARM) = typeCalloc(unsigned long,
					    (size_t) screen_lines(SP_PARM));
	if (!oldhash(SP_PARM) || !newhash(SP_PARM))
	    return;		/* malloc failure */
	for (i = 0; i < screen_lines(SP_PARM); i++) {
	    newhash(SP_PARM)[i] = hash(SP_PARM, NEWTEXT(SP_PARM, i));
	    oldhash(SP_PARM)[i] = hash(SP_PARM, OLDTEXT(SP_PARM, i));
	}
    }

#ifdef HASH_VERIFY
    for (i = 0; i < screen_lines(SP_PARM); i++) {
	if (newhash(SP_PARM)[i] != hash(SP_PARM, NEWTEXT(SP_PARM, i)))
	    fprintf(stderr, "error in newhash[%d]\n", i);
	if (oldhash(SP_PARM)[i] != hash(SP_PARM, OLDTEXT(SP_PARM, i)))
	    fprintf(stderr, "error in oldhash[%d]\n", i);
    }
#endif

    /*
     * Set up and count line-hash values.
     */
    memset(hashtab(SP_PARM), '\0',
	   sizeof(*(hashtab(SP_PARM)))
	   * ((size_t) screen_lines(SP_PARM) + 1) * 2);
    for (i = 0; i < screen_lines(SP_PARM); i++) {
	unsigned long hashval = oldhash(SP_PARM)[i];

	for (hsp = hashtab(SP_PARM); hsp->hashval; hsp++)
	    if (hsp->hashval == hashval)
		break;
	hsp->hashval = hashval;	/* in case this is a new entry */
	hsp->oldcount++;
	hsp->oldindex = i;
    }
    for (i = 0; i < screen_lines(SP_PARM); i++) {
	unsigned long hashval = newhash(SP_PARM)[i];

	for (hsp = hashtab(SP_PARM); hsp->hashval; hsp++)
	    if (hsp->hashval == hashval)
		break;
	hsp->hashval = hashval;	/* in case this is a new entry */
	hsp->newcount++;
	hsp->newindex = i;

	OLDNUM(SP_PARM, i) = _NEWINDEX;		/* initialize old indices array */
    }

    /*
     * Mark line pairs corresponding to unique hash pairs.
     *
     * We don't mark lines with offset 0, because it can make fail
     * extending hunks by cost_effective. Otherwise, it does not
     * have any side effects.
     */
    for (hsp = hashtab(SP_PARM); hsp->hashval; hsp++)
	if (hsp->oldcount == 1 && hsp->newcount == 1
	    && hsp->oldindex != hsp->newindex) {
	    TR(TRACE_UPDATE | TRACE_MOVE,
	       ("new line %d is hash-identical to old line %d (unique)",
		hsp->newindex, hsp->oldindex));
	    OLDNUM(SP_PARM, hsp->newindex) = hsp->oldindex;
	}

    grow_hunks(SP_PARM);

    /*
     * Eliminate bad or impossible shifts -- this includes removing
     * those hunks which could not grow because of conflicts, as well
     * those which are to be moved too far, they are likely to destroy
     * more than carry.
     */
    for (i = 0; i < screen_lines(SP_PARM);) {
	while (i < screen_lines(SP_PARM) && OLDNUM(SP_PARM, i) == _NEWINDEX)
	    i++;
	if (i >= screen_lines(SP_PARM))
	    break;
	start = i;
	shift = OLDNUM(SP_PARM, i) - i;
	i++;
	while (i < screen_lines(SP_PARM)
	       && OLDNUM(SP_PARM, i) != _NEWINDEX
	       && OLDNUM(SP_PARM, i) - i == shift)
	    i++;
	size = i - start;
	if (size < 3 || size + min(size / 8, 2) < abs(shift)) {
	    while (start < i) {
		OLDNUM(SP_PARM, start) = _NEWINDEX;
		start++;
	    }
	}
    }

    /* After clearing invalid hunks, try grow the rest. */
    grow_hunks(SP_PARM);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(void)
_nc_hash_map(void)
{
    NCURSES_SP_NAME(_nc_hash_map) (CURRENT_SCREEN);
}
#endif

NCURSES_EXPORT(void)
NCURSES_SP_NAME(_nc_make_oldhash) (NCURSES_SP_DCLx int i)
{
    if (oldhash(SP_PARM))
	oldhash(SP_PARM)[i] = hash(SP_PARM, OLDTEXT(SP_PARM, i));
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(void)
_nc_make_oldhash(int i)
{
    NCURSES_SP_NAME(_nc_make_oldhash) (CURRENT_SCREEN, i);
}
#endif

NCURSES_EXPORT(void)
NCURSES_SP_NAME(_nc_scroll_oldhash) (NCURSES_SP_DCLx int n, int top, int bot)
{
    size_t size;
    int i;

    if (!oldhash(SP_PARM))
	return;

    size = sizeof(*(oldhash(SP_PARM))) * (size_t) (bot - top + 1 - abs(n));
    if (n > 0) {
	memmove(oldhash(SP_PARM) + top, oldhash(SP_PARM) + top + n, size);
	for (i = bot; i > bot - n; i--)
	    oldhash(SP_PARM)[i] = hash(SP_PARM, OLDTEXT(SP_PARM, i));
    } else {
	memmove(oldhash(SP_PARM) + top - n, oldhash(SP_PARM) + top, size);
	for (i = top; i < top - n; i++)
	    oldhash(SP_PARM)[i] = hash(SP_PARM, OLDTEXT(SP_PARM, i));
    }
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(void)
_nc_scroll_oldhash(int n, int top, int bot)
{
    NCURSES_SP_NAME(_nc_scroll_oldhash) (CURRENT_SCREEN, n, top, bot);
}
#endif

#ifdef HASHDEBUG
static void
usage(void)
{
    static const char *table[] =
    {
	"hashmap test-driver",
	"",
	"#  comment",
	"l  get initial line number vector",
	"n  use following letters as text of new lines",
	"o  use following letters as text of old lines",
	"d  dump state of test arrays",
	"h  apply hash mapper and see scroll optimization",
	"?  this message"
    };
    size_t n;
    for (n = 0; n < sizeof(table) / sizeof(table[0]); n++)
	fprintf(stderr, "%s\n", table[n]);
}

int
main(int argc GCC_UNUSED, char *argv[]GCC_UNUSED)
{
    char line[BUFSIZ], *st;
    int n;

    if (setupterm(NULL, fileno(stdout), (int *) 0) == ERR)
	return EXIT_FAILURE;
    (void) _nc_alloc_screen();

    for (n = 0; n < screen_lines; n++) {
	reallines[n] = n;
	oldnums[n] = _NEWINDEX;
	CharOf(oldtext[n][0]) = CharOf(newtext[n][0]) = '.';
    }

    if (isatty(fileno(stdin)))
	usage();

#ifdef TRACE
    _nc_tracing = TRACE_MOVE;
#endif
    for (;;) {
	/* grab a test command */
	if (fgets(line, sizeof(line), stdin) == (char *) NULL)
	    break;

	switch (line[0]) {
	case '#':		/* comment */
	    (void) fputs(line, stderr);
	    break;

	case 'l':		/* get initial line number vector */
	    for (n = 0; n < screen_lines; n++) {
		reallines[n] = n;
		oldnums[n] = _NEWINDEX;
	    }
	    n = 0;
	    st = strtok(line, " ");
	    do {
		oldnums[n++] = atoi(st);
	    } while
		((st = strtok((char *) NULL, " ")) != 0);
	    break;

	case 'n':		/* use following letters as text of new lines */
	    for (n = 0; n < screen_lines; n++)
		CharOf(newtext[n][0]) = '.';
	    for (n = 0; n < screen_lines; n++)
		if (line[n + 1] == '\n')
		    break;
		else
		    CharOf(newtext[n][0]) = line[n + 1];
	    break;

	case 'o':		/* use following letters as text of old lines */
	    for (n = 0; n < screen_lines; n++)
		CharOf(oldtext[n][0]) = '.';
	    for (n = 0; n < screen_lines; n++)
		if (line[n + 1] == '\n')
		    break;
		else
		    CharOf(oldtext[n][0]) = line[n + 1];
	    break;

	case 'd':		/* dump state of test arrays */
#ifdef TRACE
	    _nc_linedump();
#endif
	    (void) fputs("Old lines: [", stdout);
	    for (n = 0; n < screen_lines; n++)
		putchar(CharOf(oldtext[n][0]));
	    putchar(']');
	    putchar('\n');
	    (void) fputs("New lines: [", stdout);
	    for (n = 0; n < screen_lines; n++)
		putchar(CharOf(newtext[n][0]));
	    putchar(']');
	    putchar('\n');
	    break;

	case 'h':		/* apply hash mapper and see scroll optimization */
	    _nc_hash_map();
	    (void) fputs("Result:\n", stderr);
#ifdef TRACE
	    _nc_linedump();
#endif
	    _nc_scroll_optimize();
	    (void) fputs("Done.\n", stderr);
	    break;
	default:
	case '?':
	    usage();
	    break;
	}
    }
#if NO_LEAKS
    _nc_free_and_exit(EXIT_SUCCESS);
#else
    return EXIT_SUCCESS;
#endif
}

#endif /* HASHDEBUG */

/* hashmap.c ends here */
