/****************************************************************************
 * Copyright (c) 1998-2010,2013 Free Software Foundation, Inc.              *
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
 *     and: Juergen Pfeifer                         2008                    *
 ****************************************************************************/

#include <curses.priv.h>

#ifndef CUR
#define CUR SP_TERMTYPE
#endif

MODULE_ID("$Id: lib_acs.c,v 1.44 2013/01/12 17:24:42 tom Exp $")

#if BROKEN_LINKER || USE_REENTRANT
#define MyBuffer _nc_prescreen.real_acs_map
NCURSES_EXPORT(chtype *)
NCURSES_PUBLIC_VAR(acs_map) (void)
{
    if (MyBuffer == 0)
	MyBuffer = typeCalloc(chtype, ACS_LEN);
    return MyBuffer;
}
#undef MyBuffer
#else
NCURSES_EXPORT_VAR (chtype) acs_map[ACS_LEN] =
{
    0
};
#endif

#ifdef USE_TERM_DRIVER
NCURSES_EXPORT(chtype)
NCURSES_SP_NAME(_nc_acs_char) (NCURSES_SP_DCLx int c)
{
    chtype *map;
    if (c < 0 || c >= ACS_LEN)
	return (chtype) 0;
    map = (SP_PARM != 0) ? SP_PARM->_acs_map :
#if BROKEN_LINKER || USE_REENTRANT
	_nc_prescreen.real_acs_map
#else
	acs_map
#endif
	;
    return map[c];
}
#endif /* USE_TERM_DRIVER */

NCURSES_EXPORT(void)
NCURSES_SP_NAME(_nc_init_acs) (NCURSES_SP_DCL0)
{
    chtype *fake_map = acs_map;
    chtype *real_map = SP_PARM != 0 ? SP_PARM->_acs_map : fake_map;
    int j;

    T(("initializing ACS map"));

    /*
     * If we're using this from curses (rather than terminfo), we are storing
     * the mapping information in the SCREEN struct so we can decide how to
     * render it.
     */
    if (real_map != fake_map) {
	for (j = 1; j < ACS_LEN; ++j) {
	    real_map[j] = 0;
	    fake_map[j] = A_ALTCHARSET | (chtype) j;
	    if (SP_PARM)
		SP_PARM->_screen_acs_map[j] = FALSE;
	}
    } else {
	for (j = 1; j < ACS_LEN; ++j) {
	    real_map[j] = 0;
	}
    }

    /*
     * Initializations for a UNIX-like multi-terminal environment.  Use
     * ASCII chars and count on the terminfo description to do better.
     */
    real_map['l'] = '+';	/* should be upper left corner */
    real_map['m'] = '+';	/* should be lower left corner */
    real_map['k'] = '+';	/* should be upper right corner */
    real_map['j'] = '+';	/* should be lower right corner */
    real_map['u'] = '+';	/* should be tee pointing left */
    real_map['t'] = '+';	/* should be tee pointing right */
    real_map['v'] = '+';	/* should be tee pointing up */
    real_map['w'] = '+';	/* should be tee pointing down */
    real_map['q'] = '-';	/* should be horizontal line */
    real_map['x'] = '|';	/* should be vertical line */
    real_map['n'] = '+';	/* should be large plus or crossover */
    real_map['o'] = '~';	/* should be scan line 1 */
    real_map['s'] = '_';	/* should be scan line 9 */
    real_map['`'] = '+';	/* should be diamond */
    real_map['a'] = ':';	/* should be checker board (stipple) */
    real_map['f'] = '\'';	/* should be degree symbol */
    real_map['g'] = '#';	/* should be plus/minus */
    real_map['~'] = 'o';	/* should be bullet */
    real_map[','] = '<';	/* should be arrow pointing left */
    real_map['+'] = '>';	/* should be arrow pointing right */
    real_map['.'] = 'v';	/* should be arrow pointing down */
    real_map['-'] = '^';	/* should be arrow pointing up */
    real_map['h'] = '#';	/* should be board of squares */
    real_map['i'] = '#';	/* should be lantern symbol */
    real_map['0'] = '#';	/* should be solid square block */
    /* these defaults were invented for ncurses */
    real_map['p'] = '-';	/* should be scan line 3 */
    real_map['r'] = '-';	/* should be scan line 7 */
    real_map['y'] = '<';	/* should be less-than-or-equal-to */
    real_map['z'] = '>';	/* should be greater-than-or-equal-to */
    real_map['{'] = '*';	/* should be greek pi */
    real_map['|'] = '!';	/* should be not-equal */
    real_map['}'] = 'f';	/* should be pound-sterling symbol */
    /* thick-line-drawing */
    real_map['L'] = '+';	/* upper left corner */
    real_map['M'] = '+';	/* lower left corner */
    real_map['K'] = '+';	/* upper right corner */
    real_map['J'] = '+';	/* lower right corner */
    real_map['T'] = '+';	/* tee pointing left */
    real_map['U'] = '+';	/* tee pointing right */
    real_map['V'] = '+';	/* tee pointing up */
    real_map['W'] = '+';	/* tee pointing down */
    real_map['Q'] = '-';	/* horizontal line */
    real_map['X'] = '|';	/* vertical line */
    real_map['N'] = '+';	/* large plus or crossover */
    /* double-line-drawing */
    real_map['C'] = '+';	/* upper left corner */
    real_map['D'] = '+';	/* lower left corner */
    real_map['B'] = '+';	/* upper right corner */
    real_map['A'] = '+';	/* lower right corner */
    real_map['G'] = '+';	/* tee pointing left */
    real_map['F'] = '+';	/* tee pointing right */
    real_map['H'] = '+';	/* tee pointing up */
    real_map['I'] = '+';	/* tee pointing down */
    real_map['R'] = '-';	/* horizontal line */
    real_map['Y'] = '|';	/* vertical line */
    real_map['E'] = '+';	/* large plus or crossover */

#ifdef USE_TERM_DRIVER
    CallDriver_2(SP_PARM, initacs, real_map, fake_map);
#else
    if (ena_acs != NULL) {
	NCURSES_PUTP2("ena_acs", ena_acs);
    }
#if NCURSES_EXT_FUNCS
    /*
     * Linux console "supports" the "PC ROM" character set by the coincidence
     * that smpch/rmpch and smacs/rmacs have the same values.  ncurses has
     * no codepage support (see SCO Merge for an example).  Outside of the
     * values defined in acsc, there are no definitions for the "PC ROM"
     * character set (assumed by some applications to be codepage 437), but we
     * allow those applications to use those codepoints.
     *
     * test/blue.c uses this feature.
     */
#define PCH_KLUDGE(a,b) (a != 0 && b != 0 && !strcmp(a,b))
    if (PCH_KLUDGE(enter_pc_charset_mode, enter_alt_charset_mode) &&
	PCH_KLUDGE(exit_pc_charset_mode, exit_alt_charset_mode)) {
	size_t i;
	for (i = 1; i < ACS_LEN; ++i) {
	    if (real_map[i] == 0) {
		real_map[i] = (chtype) i;
		if (real_map != fake_map) {
		    if (SP != 0)
			SP->_screen_acs_map[i] = TRUE;
		}
	    }
	}
    }
#endif

    if (acs_chars != NULL) {
	size_t i = 0;
	size_t length = strlen(acs_chars);

	while (i + 1 < length) {
	    if (acs_chars[i] != 0 && UChar(acs_chars[i]) < ACS_LEN) {
		real_map[UChar(acs_chars[i])] = UChar(acs_chars[i + 1]) | A_ALTCHARSET;
		if (SP != 0)
		    SP->_screen_acs_map[UChar(acs_chars[i])] = TRUE;
	    }
	    i += 2;
	}
    }
#ifdef TRACE
    /* Show the equivalent mapping, noting if it does not match the
     * given attribute, whether by re-ordering or duplication.
     */
    if (USE_TRACEF(TRACE_CALLS)) {
	size_t n, m;
	char show[ACS_LEN * 2 + 1];
	for (n = 1, m = 0; n < ACS_LEN; n++) {
	    if (real_map[n] != 0) {
		show[m++] = (char) n;
		show[m++] = (char) ChCharOf(real_map[n]);
	    }
	}
	show[m] = 0;
	if (acs_chars == NULL || strcmp(acs_chars, show))
	    _tracef("%s acs_chars %s",
		    (acs_chars == NULL) ? "NULL" : "READ",
		    _nc_visbuf(acs_chars));
	_tracef("%s acs_chars %s",
		(acs_chars == NULL)
		? "NULL"
		: (strcmp(acs_chars, show)
		   ? "DIFF"
		   : "SAME"),
		_nc_visbuf(show));
	_nc_unlock_global(tracef);
    }
#endif /* TRACE */
#endif
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(void)
_nc_init_acs(void)
{
    NCURSES_SP_NAME(_nc_init_acs) (CURRENT_SCREEN);
}
#endif
