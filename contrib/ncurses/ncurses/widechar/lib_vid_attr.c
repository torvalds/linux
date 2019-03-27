/****************************************************************************
 * Copyright (c) 2002-2013,2014 Free Software Foundation, Inc.              *
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
 *  Author: Thomas E. Dickey                                                *
 ****************************************************************************/

#include <curses.priv.h>

#ifndef CUR
#define CUR SP_TERMTYPE
#endif

MODULE_ID("$Id: lib_vid_attr.c,v 1.22 2014/02/01 22:09:27 tom Exp $")

#define doPut(mode) \
	TPUTS_TRACE(#mode); \
	NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx mode, 1, outc)

#define TurnOn(mask, mode) \
	if ((turn_on & mask) && mode) { doPut(mode); }

#define TurnOff(mask, mode) \
	if ((turn_off & mask) && mode) { doPut(mode); turn_off &= ~mask; }

	/* if there is no current screen, assume we *can* do color */
#define SetColorsIf(why, old_attr, old_pair) \
	if (can_color && (why)) { \
		TR(TRACE_ATTRS, ("old pair = %d -- new pair = %d", old_pair, pair)); \
		if ((pair != old_pair) \
		 || (fix_pair0 && (pair == 0)) \
		 || (reverse ^ ((old_attr & A_REVERSE) != 0))) { \
		    NCURSES_SP_NAME(_nc_do_color) (NCURSES_SP_ARGx \
						   old_pair, pair, \
						   reverse, outc); \
		} \
	}

#define set_color(mode, pair) \
	mode &= ALL_BUT_COLOR; \
	mode |= (attr_t) ColorPair(pair)

NCURSES_EXPORT(int)
NCURSES_SP_NAME(vid_puts) (NCURSES_SP_DCLx
			   attr_t newmode,
			   NCURSES_PAIRS_T pair,
			   void *opts GCC_UNUSED,
			   NCURSES_SP_OUTC outc)
{
#if NCURSES_EXT_COLORS
    static attr_t previous_attr = A_NORMAL;
    static int previous_pair = 0;

    attr_t turn_on, turn_off;
    bool reverse = FALSE;
    bool can_color = (SP_PARM == 0 || SP_PARM->_coloron);
#if NCURSES_EXT_FUNCS
    bool fix_pair0 = (SP_PARM != 0 && SP_PARM->_coloron && !SP_PARM->_default_color);
#else
#define fix_pair0 FALSE
#endif

    newmode &= A_ATTRIBUTES;
    T((T_CALLED("vid_puts(%s,%d)"), _traceattr(newmode), pair));

    /* this allows us to go on whether or not newterm() has been called */
    if (SP_PARM) {
	previous_attr = AttrOf(SCREEN_ATTRS(SP_PARM));
	previous_pair = GetPair(SCREEN_ATTRS(SP_PARM));
    }

    TR(TRACE_ATTRS, ("previous attribute was %s, %d",
		     _traceattr(previous_attr), previous_pair));

#if !USE_XMC_SUPPORT
    if ((SP_PARM != 0)
	&& (magic_cookie_glitch > 0))
	newmode &= ~(SP_PARM->_xmc_suppress);
#endif

    /*
     * If we have a terminal that cannot combine color with video
     * attributes, use the colors in preference.
     */
    if ((pair != 0
	 || fix_pair0)
	&& (no_color_video > 0)) {
	/*
	 * If we had chosen the A_xxx definitions to correspond to the
	 * no_color_video mask, we could simply shift it up and mask off the
	 * attributes.  But we did not (actually copied Solaris' definitions).
	 * However, this is still simpler/faster than a lookup table.
	 *
	 * The 63 corresponds to A_STANDOUT, A_UNDERLINE, A_REVERSE, A_BLINK,
	 * A_DIM, A_BOLD which are 1:1 with no_color_video.  The bits that
	 * correspond to A_INVIS, A_PROTECT (192) must be shifted up 1 and
	 * A_ALTCHARSET (256) down 2 to line up.  We use the NCURSES_BITS
	 * macro so this will work properly for the wide-character layout.
	 */
	unsigned value = (unsigned) no_color_video;
	attr_t mask = NCURSES_BITS((value & 63)
				   | ((value & 192) << 1)
				   | ((value & 256) >> 2), 8);

	if ((mask & A_REVERSE) != 0
	    && (newmode & A_REVERSE) != 0) {
	    reverse = TRUE;
	    mask &= ~A_REVERSE;
	}
	newmode &= ~mask;
    }

    if (newmode == previous_attr
	&& pair == previous_pair)
	returnCode(OK);

    if (reverse) {
	newmode &= ~A_REVERSE;
    }

    turn_off = (~newmode & previous_attr) & ALL_BUT_COLOR;
    turn_on = (newmode & ~previous_attr) & ALL_BUT_COLOR;

    SetColorsIf(((pair == 0) && !fix_pair0), previous_attr, previous_pair);

    if (newmode == A_NORMAL) {
	if ((previous_attr & A_ALTCHARSET) && exit_alt_charset_mode) {
	    doPut(exit_alt_charset_mode);
	    previous_attr &= ~A_ALTCHARSET;
	}
	if (previous_attr) {
	    if (exit_attribute_mode) {
		doPut(exit_attribute_mode);
	    } else {
		if (!SP_PARM || SP_PARM->_use_rmul) {
		    TurnOff(A_UNDERLINE, exit_underline_mode);
		}
		if (!SP_PARM || SP_PARM->_use_rmso) {
		    TurnOff(A_STANDOUT, exit_standout_mode);
		}
#if USE_ITALIC
		if (!SP_PARM || SP_PARM->_use_ritm) {
		    TurnOff(A_ITALIC, exit_italics_mode);
		}
#endif
	    }
	    previous_attr &= ALL_BUT_COLOR;
	    previous_pair = 0;
	}

	SetColorsIf((pair != 0) || fix_pair0, previous_attr, previous_pair);
    } else if (set_attributes) {
	if (turn_on || turn_off) {
	    TPUTS_TRACE("set_attributes");
	    NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				    TPARM_9(set_attributes,
					    (newmode & A_STANDOUT) != 0,
					    (newmode & A_UNDERLINE) != 0,
					    (newmode & A_REVERSE) != 0,
					    (newmode & A_BLINK) != 0,
					    (newmode & A_DIM) != 0,
					    (newmode & A_BOLD) != 0,
					    (newmode & A_INVIS) != 0,
					    (newmode & A_PROTECT) != 0,
					    (newmode & A_ALTCHARSET) != 0),
				    1, outc);
	    previous_attr &= ALL_BUT_COLOR;
	    previous_pair = 0;
	}
#if USE_ITALIC
	if (!SP_PARM || SP_PARM->_use_ritm) {
	    if (turn_on & A_ITALIC) {
		TurnOn(A_ITALIC, enter_italics_mode);
	    } else if (turn_off & A_ITALIC) {
		TurnOff(A_ITALIC, exit_italics_mode);
	    }
	}
#endif
	SetColorsIf((pair != 0) || fix_pair0, previous_attr, previous_pair);
    } else {

	TR(TRACE_ATTRS, ("turning %s off", _traceattr(turn_off)));

	TurnOff(A_ALTCHARSET, exit_alt_charset_mode);

	if (!SP_PARM || SP_PARM->_use_rmul) {
	    TurnOff(A_UNDERLINE, exit_underline_mode);
	}

	if (!SP_PARM || SP_PARM->_use_rmso) {
	    TurnOff(A_STANDOUT, exit_standout_mode);
	}
#if USE_ITALIC
	if (!SP_PARM || SP_PARM->_use_ritm) {
	    TurnOff(A_ITALIC, exit_italics_mode);
	}
#endif
	if (turn_off && exit_attribute_mode) {
	    doPut(exit_attribute_mode);
	    turn_on |= (newmode & ALL_BUT_COLOR);
	    previous_attr &= ALL_BUT_COLOR;
	    previous_pair = 0;
	}
	SetColorsIf((pair != 0) || fix_pair0, previous_attr, previous_pair);

	TR(TRACE_ATTRS, ("turning %s on", _traceattr(turn_on)));
	/* *INDENT-OFF* */
	TurnOn(A_ALTCHARSET,	enter_alt_charset_mode);
	TurnOn(A_BLINK,		enter_blink_mode);
	TurnOn(A_BOLD,		enter_bold_mode);
	TurnOn(A_DIM,		enter_dim_mode);
	TurnOn(A_REVERSE,	enter_reverse_mode);
	TurnOn(A_STANDOUT,	enter_standout_mode);
	TurnOn(A_PROTECT,	enter_protected_mode);
	TurnOn(A_INVIS,		enter_secure_mode);
	TurnOn(A_UNDERLINE,	enter_underline_mode);
#if USE_ITALIC
	TurnOn(A_ITALIC,	enter_italics_mode);
#endif
#if USE_WIDEC_SUPPORT
	TurnOn(A_HORIZONTAL,	enter_horizontal_hl_mode);
	TurnOn(A_LEFT,		enter_left_hl_mode);
	TurnOn(A_LOW,		enter_low_hl_mode);
	TurnOn(A_RIGHT,		enter_right_hl_mode);
	TurnOn(A_TOP,		enter_top_hl_mode);
	TurnOn(A_VERTICAL,	enter_vertical_hl_mode);
#endif
	/* *INDENT-ON* */

    }

    if (reverse)
	newmode |= A_REVERSE;

    if (SP_PARM) {
	SetAttr(SCREEN_ATTRS(SP_PARM), newmode);
	SetPair(SCREEN_ATTRS(SP_PARM), pair);
    } else {
	previous_attr = newmode;
	previous_pair = pair;
    }

    returnCode(OK);
#else
    T((T_CALLED("vid_puts(%s,%d)"), _traceattr(newmode), (int) pair));
    set_color(newmode, pair);
    returnCode(NCURSES_SP_NAME(vidputs) (NCURSES_SP_ARGx newmode, outc));
#endif
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
vid_puts(attr_t newmode,
	 NCURSES_PAIRS_T pair,
	 void *opts GCC_UNUSED,
	 NCURSES_OUTC outc)
{
    SetSafeOutcWrapper(outc);
    return NCURSES_SP_NAME(vid_puts) (CURRENT_SCREEN,
				      newmode,
				      pair,
				      opts,
				      _nc_outc_wrapper);
}
#endif

#undef vid_attr
NCURSES_EXPORT(int)
NCURSES_SP_NAME(vid_attr) (NCURSES_SP_DCLx
			   attr_t newmode,
			   NCURSES_PAIRS_T pair,
			   void *opts)
{
    T((T_CALLED("vid_attr(%s,%d)"), _traceattr(newmode), (int) pair));
    returnCode(NCURSES_SP_NAME(vid_puts) (NCURSES_SP_ARGx
					  newmode,
					  pair,
					  opts,
					  NCURSES_SP_NAME(_nc_putchar)));
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
vid_attr(attr_t newmode, NCURSES_PAIRS_T pair, void *opts)
{
    return NCURSES_SP_NAME(vid_attr) (CURRENT_SCREEN, newmode, pair, opts);
}
#endif

/*
 * This implementation uses the same mask values for A_xxx and WA_xxx, so
 * we can use termattrs() for part of the logic.
 */
NCURSES_EXPORT(attr_t)
NCURSES_SP_NAME(term_attrs) (NCURSES_SP_DCL0)
{
    attr_t attrs = 0;

    T((T_CALLED("term_attrs()")));
    if (SP_PARM) {
	attrs = NCURSES_SP_NAME(termattrs) (NCURSES_SP_ARG);

	/* these are only supported for wide-character mode */
	if (enter_horizontal_hl_mode)
	    attrs |= WA_HORIZONTAL;
	if (enter_left_hl_mode)
	    attrs |= WA_LEFT;
	if (enter_low_hl_mode)
	    attrs |= WA_LOW;
	if (enter_right_hl_mode)
	    attrs |= WA_RIGHT;
	if (enter_top_hl_mode)
	    attrs |= WA_TOP;
	if (enter_vertical_hl_mode)
	    attrs |= WA_VERTICAL;
    }

    returnAttr(attrs);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(attr_t)
term_attrs(void)
{
    return NCURSES_SP_NAME(term_attrs) (CURRENT_SCREEN);
}
#endif
