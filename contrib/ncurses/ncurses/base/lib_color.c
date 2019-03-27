/****************************************************************************
 * Copyright (c) 1998-2013,2014 Free Software Foundation, Inc.              *
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
 *     and: Juergen Pfeifer                         2009                    *
 ****************************************************************************/

/* lib_color.c
 *
 * Handles color emulation of SYS V curses
 */

#include <curses.priv.h>
#include <tic.h>

#ifndef CUR
#define CUR SP_TERMTYPE
#endif

MODULE_ID("$Id: lib_color.c,v 1.109 2014/02/01 22:22:30 tom Exp $")

#ifdef USE_TERM_DRIVER
#define CanChange      InfoOf(SP_PARM).canchange
#define DefaultPalette InfoOf(SP_PARM).defaultPalette
#define HasColor       InfoOf(SP_PARM).hascolor
#define InitColor      InfoOf(SP_PARM).initcolor
#define MaxColors      InfoOf(SP_PARM).maxcolors
#define MaxPairs       InfoOf(SP_PARM).maxpairs
#define UseHlsPalette  (DefaultPalette == _nc_hls_palette)
#else
#define CanChange      can_change
#define DefaultPalette (hue_lightness_saturation ? hls_palette : cga_palette)
#define HasColor       has_color
#define InitColor      initialize_color
#define MaxColors      max_colors
#define MaxPairs       max_pairs
#define UseHlsPalette  (hue_lightness_saturation)
#endif

#ifndef USE_TERM_DRIVER
/*
 * These should be screen structure members.  They need to be globals for
 * historical reasons.  So we assign them in start_color() and also in
 * set_term()'s screen-switching logic.
 */
#if USE_REENTRANT
NCURSES_EXPORT(int)
NCURSES_PUBLIC_VAR(COLOR_PAIRS) (void)
{
    return SP ? SP->_pair_count : -1;
}
NCURSES_EXPORT(int)
NCURSES_PUBLIC_VAR(COLORS) (void)
{
    return SP ? SP->_color_count : -1;
}
#else
NCURSES_EXPORT_VAR(int) COLOR_PAIRS = 0;
NCURSES_EXPORT_VAR(int) COLORS = 0;
#endif
#endif /* !USE_TERM_DRIVER */

#define DATA(r,g,b) {r,g,b, 0,0,0, 0}

#define TYPE_CALLOC(type,elts) typeCalloc(type, (unsigned)(elts))

#define MAX_PALETTE	8

#define OkColorHi(n)	(((n) < COLORS) && ((n) < maxcolors))
#define InPalette(n)	((n) >= 0 && (n) < MAX_PALETTE)

/*
 * Given a RGB range of 0..1000, we'll normally set the individual values
 * to about 2/3 of the maximum, leaving full-range for bold/bright colors.
 */
#define RGB_ON  680
#define RGB_OFF 0
/* *INDENT-OFF* */
static const color_t cga_palette[] =
{
    /*  R               G               B */
    DATA(RGB_OFF,	RGB_OFF,	RGB_OFF),	/* COLOR_BLACK */
    DATA(RGB_ON,	RGB_OFF,	RGB_OFF),	/* COLOR_RED */
    DATA(RGB_OFF,	RGB_ON,		RGB_OFF),	/* COLOR_GREEN */
    DATA(RGB_ON,	RGB_ON,		RGB_OFF),	/* COLOR_YELLOW */
    DATA(RGB_OFF,	RGB_OFF,	RGB_ON),	/* COLOR_BLUE */
    DATA(RGB_ON,	RGB_OFF,	RGB_ON),	/* COLOR_MAGENTA */
    DATA(RGB_OFF,	RGB_ON,		RGB_ON),	/* COLOR_CYAN */
    DATA(RGB_ON,	RGB_ON,		RGB_ON),	/* COLOR_WHITE */
};

static const color_t hls_palette[] =
{
    /*  	H       L       S */
    DATA(	0,	0,	0),		/* COLOR_BLACK */
    DATA(	120,	50,	100),		/* COLOR_RED */
    DATA(	240,	50,	100),		/* COLOR_GREEN */
    DATA(	180,	50,	100),		/* COLOR_YELLOW */
    DATA(	330,	50,	100),		/* COLOR_BLUE */
    DATA(	60,	50,	100),		/* COLOR_MAGENTA */
    DATA(	300,	50,	100),		/* COLOR_CYAN */
    DATA(	0,	50,	100),		/* COLOR_WHITE */
};

#ifdef USE_TERM_DRIVER
NCURSES_EXPORT_VAR(const color_t*) _nc_cga_palette = cga_palette;
NCURSES_EXPORT_VAR(const color_t*) _nc_hls_palette = hls_palette;
#endif

/* *INDENT-ON* */

/*
 * Ensure that we use color pairs only when colors have been started, and also
 * that the index is within the limits of the table which we allocated.
 */
#define ValidPair(pair) \
    ((SP_PARM != 0) && (pair >= 0) && (pair < SP_PARM->_pair_limit) && SP_PARM->_coloron)

#if NCURSES_EXT_FUNCS
/*
 * These are called from _nc_do_color(), which in turn is called from
 * vidattr - so we have to assume that sp may be null.
 */
static int
default_fg(NCURSES_SP_DCL0)
{
    return (SP_PARM != 0) ? SP_PARM->_default_fg : COLOR_WHITE;
}

static int
default_bg(NCURSES_SP_DCL0)
{
    return SP_PARM != 0 ? SP_PARM->_default_bg : COLOR_BLACK;
}
#else
#define default_fg(sp) COLOR_WHITE
#define default_bg(sp) COLOR_BLACK
#endif

#ifndef USE_TERM_DRIVER
/*
 * SVr4 curses is known to interchange color codes (1,4) and (3,6), possibly
 * to maintain compatibility with a pre-ANSI scheme.  The same scheme is
 * also used in the FreeBSD syscons.
 */
static int
toggled_colors(int c)
{
    if (c < 16) {
	static const int table[] =
	{0, 4, 2, 6, 1, 5, 3, 7,
	 8, 12, 10, 14, 9, 13, 11, 15};
	c = table[c];
    }
    return c;
}
#endif

static void
set_background_color(NCURSES_SP_DCLx int bg, NCURSES_SP_OUTC outc)
{
#ifdef USE_TERM_DRIVER
    CallDriver_3(SP_PARM, color, FALSE, bg, outc);
#else
    if (set_a_background) {
	TPUTS_TRACE("set_a_background");
	NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				TPARM_1(set_a_background, bg),
				1, outc);
    } else {
	TPUTS_TRACE("set_background");
	NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				TPARM_1(set_background, toggled_colors(bg)),
				1, outc);
    }
#endif
}

static void
set_foreground_color(NCURSES_SP_DCLx int fg, NCURSES_SP_OUTC outc)
{
#ifdef USE_TERM_DRIVER
    CallDriver_3(SP_PARM, color, TRUE, fg, outc);
#else
    if (set_a_foreground) {
	TPUTS_TRACE("set_a_foreground");
	NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				TPARM_1(set_a_foreground, fg),
				1, outc);
    } else {
	TPUTS_TRACE("set_foreground");
	NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				TPARM_1(set_foreground, toggled_colors(fg)),
				1, outc);
    }
#endif
}

static void
init_color_table(NCURSES_SP_DCL0)
{
    const color_t *tp = DefaultPalette;
    int n;

    assert(tp != 0);

    for (n = 0; n < COLORS; n++) {
	if (InPalette(n)) {
	    SP_PARM->_color_table[n] = tp[n];
	} else {
	    SP_PARM->_color_table[n] = tp[n % MAX_PALETTE];
	    if (UseHlsPalette) {
		SP_PARM->_color_table[n].green = 100;
	    } else {
		if (SP_PARM->_color_table[n].red)
		    SP_PARM->_color_table[n].red = 1000;
		if (SP_PARM->_color_table[n].green)
		    SP_PARM->_color_table[n].green = 1000;
		if (SP_PARM->_color_table[n].blue)
		    SP_PARM->_color_table[n].blue = 1000;
	    }
	}
    }
}

/*
 * Reset the color pair, e.g., to whatever color pair 0 is.
 */
static bool
reset_color_pair(NCURSES_SP_DCL0)
{
#ifdef USE_TERM_DRIVER
    return CallDriver(SP_PARM, rescol);
#else
    bool result = FALSE;

    (void) SP_PARM;
    if (orig_pair != 0) {
	(void) NCURSES_PUTP2("orig_pair", orig_pair);
	result = TRUE;
    }
    return result;
#endif
}

/*
 * Reset color pairs and definitions.  Actually we do both more to accommodate
 * badly-written terminal descriptions than for the relatively rare case where
 * someone has changed the color definitions.
 */
NCURSES_EXPORT(bool)
NCURSES_SP_NAME(_nc_reset_colors) (NCURSES_SP_DCL0)
{
    int result = FALSE;

    T((T_CALLED("_nc_reset_colors(%p)"), (void *) SP_PARM));
    if (SP_PARM->_color_defs > 0)
	SP_PARM->_color_defs = -(SP_PARM->_color_defs);
    if (reset_color_pair(NCURSES_SP_ARG))
	result = TRUE;

#ifdef USE_TERM_DRIVER
    result = CallDriver(SP_PARM, rescolors);
#else
    if (orig_colors != 0) {
	NCURSES_PUTP2("orig_colors", orig_colors);
	result = TRUE;
    }
#endif
    returnBool(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(bool)
_nc_reset_colors(void)
{
    return NCURSES_SP_NAME(_nc_reset_colors) (CURRENT_SCREEN);
}
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(start_color) (NCURSES_SP_DCL0)
{
    int result = ERR;
    int maxpairs = 0, maxcolors = 0;

    T((T_CALLED("start_color(%p)"), (void *) SP_PARM));

    if (SP_PARM == 0) {
	result = ERR;
    } else if (SP_PARM->_coloron) {
	result = OK;
    } else {
	maxpairs = MaxPairs;
	maxcolors = MaxColors;
	if (reset_color_pair(NCURSES_SP_ARG) != TRUE) {
	    set_foreground_color(NCURSES_SP_ARGx
				 default_fg(NCURSES_SP_ARG),
				 NCURSES_SP_NAME(_nc_outch));
	    set_background_color(NCURSES_SP_ARGx
				 default_bg(NCURSES_SP_ARG),
				 NCURSES_SP_NAME(_nc_outch));
	}
#if !NCURSES_EXT_COLORS
	/*
	 * Without ext-colors, we cannot represent more than 256 color pairs.
	 */
	if (maxpairs > 256)
	    maxpairs = 256;
#endif

	if (maxpairs > 0 && maxcolors > 0) {
	    SP_PARM->_pair_limit = maxpairs;

#if NCURSES_EXT_FUNCS
	    /*
	     * If using default colors, allocate extra space in table to
	     * allow for default-color as a component of a color-pair.
	     */
	    SP_PARM->_pair_limit += (1 + (2 * maxcolors));
#endif
	    SP_PARM->_pair_count = maxpairs;
	    SP_PARM->_color_count = maxcolors;
#if !USE_REENTRANT
	    COLOR_PAIRS = maxpairs;
	    COLORS = maxcolors;
#endif

	    SP_PARM->_color_pairs = TYPE_CALLOC(colorpair_t, SP_PARM->_pair_limit);
	    if (SP_PARM->_color_pairs != 0) {
		SP_PARM->_color_table = TYPE_CALLOC(color_t, maxcolors);
		if (SP_PARM->_color_table != 0) {
		    SP_PARM->_color_pairs[0] = PAIR_OF(default_fg(NCURSES_SP_ARG),
						       default_bg(NCURSES_SP_ARG));
		    init_color_table(NCURSES_SP_ARG);

		    T(("started color: COLORS = %d, COLOR_PAIRS = %d",
		       COLORS, COLOR_PAIRS));

		    SP_PARM->_coloron = 1;
		    result = OK;
		} else if (SP_PARM->_color_pairs != 0) {
		    FreeAndNull(SP_PARM->_color_pairs);
		}
	    }
	} else {
	    result = OK;
	}
    }
    returnCode(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
start_color(void)
{
    return NCURSES_SP_NAME(start_color) (CURRENT_SCREEN);
}
#endif

/* This function was originally written by Daniel Weaver <danw@znyx.com> */
static void
rgb2hls(int r, int g, int b, NCURSES_COLOR_T *h, NCURSES_COLOR_T *l, NCURSES_COLOR_T *s)
/* convert RGB to HLS system */
{
    int min, max, t;

    if ((min = g < r ? g : r) > b)
	min = b;
    if ((max = g > r ? g : r) < b)
	max = b;

    /* calculate lightness */
    *l = (NCURSES_COLOR_T) ((min + max) / 20);

    if (min == max) {		/* black, white and all shades of gray */
	*h = 0;
	*s = 0;
	return;
    }

    /* calculate saturation */
    if (*l < 50)
	*s = (NCURSES_COLOR_T) (((max - min) * 100) / (max + min));
    else
	*s = (NCURSES_COLOR_T) (((max - min) * 100) / (2000 - max - min));

    /* calculate hue */
    if (r == max)
	t = (NCURSES_COLOR_T) (120 + ((g - b) * 60) / (max - min));
    else if (g == max)
	t = (NCURSES_COLOR_T) (240 + ((b - r) * 60) / (max - min));
    else
	t = (NCURSES_COLOR_T) (360 + ((r - g) * 60) / (max - min));

    *h = (NCURSES_COLOR_T) (t % 360);
}

/*
 * Extension (1997/1/18) - Allow negative f/b values to set default color
 * values.
 */
NCURSES_EXPORT(int)
NCURSES_SP_NAME(init_pair) (NCURSES_SP_DCLx
			    NCURSES_PAIRS_T pair,
			    NCURSES_COLOR_T f,
			    NCURSES_COLOR_T b)
{
    colorpair_t result;
    colorpair_t previous;
    int maxcolors;

    T((T_CALLED("init_pair(%p,%d,%d,%d)"),
       (void *) SP_PARM,
       (int) pair,
       (int) f,
       (int) b));

    if (!ValidPair(pair))
	returnCode(ERR);

    maxcolors = MaxColors;

    previous = SP_PARM->_color_pairs[pair];
#if NCURSES_EXT_FUNCS
    if (SP_PARM->_default_color || SP_PARM->_assumed_color) {
	bool isDefault = FALSE;
	bool wasDefault = FALSE;
	int default_pairs = SP_PARM->_default_pairs;

	/*
	 * Map caller's color number, e.g., -1, 0, 1, .., 7, etc., into
	 * internal unsigned values which we will store in the _color_pairs[]
	 * table.
	 */
	if (isDefaultColor(f)) {
	    f = COLOR_DEFAULT;
	    isDefault = TRUE;
	} else if (!OkColorHi(f)) {
	    returnCode(ERR);
	}

	if (isDefaultColor(b)) {
	    b = COLOR_DEFAULT;
	    isDefault = TRUE;
	} else if (!OkColorHi(b)) {
	    returnCode(ERR);
	}

	/*
	 * Check if the table entry that we are going to init/update used
	 * default colors.
	 */
	if ((FORE_OF(previous) == COLOR_DEFAULT)
	    || (BACK_OF(previous) == COLOR_DEFAULT))
	    wasDefault = TRUE;

	/*
	 * Keep track of the number of entries in the color pair table which
	 * used a default color.
	 */
	if (isDefault && !wasDefault) {
	    ++default_pairs;
	} else if (wasDefault && !isDefault) {
	    --default_pairs;
	}

	/*
	 * As an extension, ncurses allows the pair number to exceed the
	 * terminal's color_pairs value for pairs using a default color.
	 *
	 * Note that updating a pair which used a default color with one
	 * that does not will decrement the count - and possibly interfere
	 * with sequentially adding new pairs.
	 */
	if (pair > (SP_PARM->_pair_count + default_pairs)) {
	    returnCode(ERR);
	}
	SP_PARM->_default_pairs = default_pairs;
    } else
#endif
    {
	if ((f < 0) || !OkColorHi(f)
	    || (b < 0) || !OkColorHi(b)
	    || (pair < 1)) {
	    returnCode(ERR);
	}
    }

    /*
     * When a pair's content is changed, replace its colors (if pair was
     * initialized before a screen update is performed replacing original
     * pair colors with the new ones).
     */
    result = PAIR_OF(f, b);
    if (previous != 0
	&& previous != result) {
	int y, x;

	for (y = 0; y <= CurScreen(SP_PARM)->_maxy; y++) {
	    struct ldat *ptr = &(CurScreen(SP_PARM)->_line[y]);
	    bool changed = FALSE;
	    for (x = 0; x <= CurScreen(SP_PARM)->_maxx; x++) {
		if (GetPair(ptr->text[x]) == pair) {
		    /* Set the old cell to zero to ensure it will be
		       updated on the next doupdate() */
		    SetChar(ptr->text[x], 0, 0);
		    CHANGED_CELL(ptr, x);
		    changed = TRUE;
		}
	    }
	    if (changed)
		NCURSES_SP_NAME(_nc_make_oldhash) (NCURSES_SP_ARGx y);
	}
    }

    SP_PARM->_color_pairs[pair] = result;
    if (GET_SCREEN_PAIR(SP_PARM) == pair)
	SET_SCREEN_PAIR(SP_PARM, (chtype) (~0));	/* force attribute update */

#ifdef USE_TERM_DRIVER
    CallDriver_3(SP_PARM, initpair, pair, f, b);
#else
    if (initialize_pair && InPalette(f) && InPalette(b)) {
	const color_t *tp = DefaultPalette;

	TR(TRACE_ATTRS,
	   ("initializing pair: pair = %d, fg=(%d,%d,%d), bg=(%d,%d,%d)",
	    (int) pair,
	    (int) tp[f].red, (int) tp[f].green, (int) tp[f].blue,
	    (int) tp[b].red, (int) tp[b].green, (int) tp[b].blue));

	NCURSES_PUTP2("initialize_pair",
		      TPARM_7(initialize_pair,
			      pair,
			      (int) tp[f].red,
			      (int) tp[f].green,
			      (int) tp[f].blue,
			      (int) tp[b].red,
			      (int) tp[b].green,
			      (int) tp[b].blue));
    }
#endif

    returnCode(OK);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
init_pair(NCURSES_COLOR_T pair, NCURSES_COLOR_T f, NCURSES_COLOR_T b)
{
    return NCURSES_SP_NAME(init_pair) (CURRENT_SCREEN, pair, f, b);
}
#endif

#define okRGB(n) ((n) >= 0 && (n) <= 1000)

NCURSES_EXPORT(int)
NCURSES_SP_NAME(init_color) (NCURSES_SP_DCLx
			     NCURSES_COLOR_T color,
			     NCURSES_COLOR_T r,
			     NCURSES_COLOR_T g,
			     NCURSES_COLOR_T b)
{
    int result = ERR;
    int maxcolors;

    T((T_CALLED("init_color(%p,%d,%d,%d,%d)"),
       (void *) SP_PARM,
       color,
       r, g, b));

    if (SP_PARM == 0)
	returnCode(result);

    maxcolors = MaxColors;

    if (InitColor
	&& SP_PARM->_coloron
	&& (color >= 0 && OkColorHi(color))
	&& (okRGB(r) && okRGB(g) && okRGB(b))) {

	SP_PARM->_color_table[color].init = 1;
	SP_PARM->_color_table[color].r = r;
	SP_PARM->_color_table[color].g = g;
	SP_PARM->_color_table[color].b = b;

	if (UseHlsPalette) {
	    rgb2hls(r, g, b,
		    &SP_PARM->_color_table[color].red,
		    &SP_PARM->_color_table[color].green,
		    &SP_PARM->_color_table[color].blue);
	} else {
	    SP_PARM->_color_table[color].red = r;
	    SP_PARM->_color_table[color].green = g;
	    SP_PARM->_color_table[color].blue = b;
	}

#ifdef USE_TERM_DRIVER
	CallDriver_4(SP_PARM, initcolor, color, r, g, b);
#else
	NCURSES_PUTP2("initialize_color",
		      TPARM_4(initialize_color, color, r, g, b));
#endif
	SP_PARM->_color_defs = max(color + 1, SP_PARM->_color_defs);

	result = OK;
    }
    returnCode(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
init_color(NCURSES_COLOR_T color,
	   NCURSES_COLOR_T r,
	   NCURSES_COLOR_T g,
	   NCURSES_COLOR_T b)
{
    return NCURSES_SP_NAME(init_color) (CURRENT_SCREEN, color, r, g, b);
}
#endif

NCURSES_EXPORT(bool)
NCURSES_SP_NAME(can_change_color) (NCURSES_SP_DCL)
{
    int result = FALSE;

    T((T_CALLED("can_change_color(%p)"), (void *) SP_PARM));

    if (HasTerminal(SP_PARM) && (CanChange != 0)) {
	result = TRUE;
    }

    returnCode(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(bool)
can_change_color(void)
{
    return NCURSES_SP_NAME(can_change_color) (CURRENT_SCREEN);
}
#endif

NCURSES_EXPORT(bool)
NCURSES_SP_NAME(has_colors) (NCURSES_SP_DCL0)
{
    int code = FALSE;

    (void) SP_PARM;
    T((T_CALLED("has_colors()")));
    if (HasTerminal(SP_PARM)) {
#ifdef USE_TERM_DRIVER
	code = HasColor;
#else
	code = ((VALID_NUMERIC(max_colors) && VALID_NUMERIC(max_pairs)
		 && (((set_foreground != NULL)
		      && (set_background != NULL))
		     || ((set_a_foreground != NULL)
			 && (set_a_background != NULL))
		     || set_color_pair)) ? TRUE : FALSE);
#endif
    }
    returnCode(code);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(bool)
has_colors(void)
{
    return NCURSES_SP_NAME(has_colors) (CURRENT_SCREEN);
}
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(color_content) (NCURSES_SP_DCLx
				NCURSES_COLOR_T color,
				NCURSES_COLOR_T *r,
				NCURSES_COLOR_T *g,
				NCURSES_COLOR_T *b)
{
    int result = ERR;
    int maxcolors;

    T((T_CALLED("color_content(%p,%d,%p,%p,%p)"),
       (void *) SP_PARM,
       color,
       (void *) r,
       (void *) g,
       (void *) b));

    if (SP_PARM == 0)
	returnCode(result);

    maxcolors = MaxColors;

    if (color < 0 || !OkColorHi(color) || !SP_PARM->_coloron) {
	result = ERR;
    } else {
	NCURSES_COLOR_T c_r = SP_PARM->_color_table[color].red;
	NCURSES_COLOR_T c_g = SP_PARM->_color_table[color].green;
	NCURSES_COLOR_T c_b = SP_PARM->_color_table[color].blue;

	if (r)
	    *r = c_r;
	if (g)
	    *g = c_g;
	if (b)
	    *b = c_b;

	TR(TRACE_ATTRS, ("...color_content(%d,%d,%d,%d)",
			 color, c_r, c_g, c_b));
	result = OK;
    }
    returnCode(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
color_content(NCURSES_COLOR_T color,
	      NCURSES_COLOR_T *r,
	      NCURSES_COLOR_T *g,
	      NCURSES_COLOR_T *b)
{
    return NCURSES_SP_NAME(color_content) (CURRENT_SCREEN, color, r, g, b);
}
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(pair_content) (NCURSES_SP_DCLx
			       NCURSES_PAIRS_T pair,
			       NCURSES_COLOR_T *f,
			       NCURSES_COLOR_T *b)
{
    int result;

    T((T_CALLED("pair_content(%p,%d,%p,%p)"),
       (void *) SP_PARM,
       (int) pair,
       (void *) f,
       (void *) b));

    if (!ValidPair(pair)) {
	result = ERR;
    } else {
	NCURSES_COLOR_T fg = (NCURSES_COLOR_T) FORE_OF(SP_PARM->_color_pairs[pair]);
	NCURSES_COLOR_T bg = (NCURSES_COLOR_T) BACK_OF(SP_PARM->_color_pairs[pair]);

#if NCURSES_EXT_FUNCS
	if (fg == COLOR_DEFAULT)
	    fg = -1;
	if (bg == COLOR_DEFAULT)
	    bg = -1;
#endif

	if (f)
	    *f = fg;
	if (b)
	    *b = bg;

	TR(TRACE_ATTRS, ("...pair_content(%p,%d,%d,%d)",
			 (void *) SP_PARM,
			 (int) pair,
			 (int) fg, (int) bg));
	result = OK;
    }
    returnCode(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
pair_content(NCURSES_COLOR_T pair, NCURSES_COLOR_T *f, NCURSES_COLOR_T *b)
{
    return NCURSES_SP_NAME(pair_content) (CURRENT_SCREEN, pair, f, b);
}
#endif

NCURSES_EXPORT(void)
NCURSES_SP_NAME(_nc_do_color) (NCURSES_SP_DCLx
			       int old_pair,
			       int pair,
			       int reverse,
			       NCURSES_SP_OUTC outc)
{
#ifdef USE_TERM_DRIVER
    CallDriver_4(SP_PARM, docolor, old_pair, pair, reverse, outc);
#else
    NCURSES_COLOR_T fg = COLOR_DEFAULT;
    NCURSES_COLOR_T bg = COLOR_DEFAULT;
    NCURSES_COLOR_T old_fg = -1;
    NCURSES_COLOR_T old_bg = -1;

    if (!ValidPair(pair)) {
	return;
    } else if (pair != 0) {
	if (set_color_pair) {
	    TPUTS_TRACE("set_color_pair");
	    NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				    TPARM_1(set_color_pair, pair),
				    1, outc);
	    return;
	} else if (SP_PARM != 0) {
	    if (pair_content((NCURSES_COLOR_T) pair, &fg, &bg) == ERR)
		return;
	}
    }

    if (old_pair >= 0
	&& SP_PARM != 0
	&& pair_content((NCURSES_COLOR_T) old_pair, &old_fg, &old_bg) != ERR) {
	if ((isDefaultColor(fg) && !isDefaultColor(old_fg))
	    || (isDefaultColor(bg) && !isDefaultColor(old_bg))) {
#if NCURSES_EXT_FUNCS
	    /*
	     * A minor optimization - but extension.  If "AX" is specified in
	     * the terminal description, treat it as screen's indicator of ECMA
	     * SGR 39 and SGR 49, and assume the two sequences are independent.
	     */
	    if (SP_PARM->_has_sgr_39_49
		&& isDefaultColor(old_bg)
		&& !isDefaultColor(old_fg)) {
		NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx "\033[39m", 1, outc);
	    } else if (SP_PARM->_has_sgr_39_49
		       && isDefaultColor(old_fg)
		       && !isDefaultColor(old_bg)) {
		NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx "\033[49m", 1, outc);
	    } else
#endif
		reset_color_pair(NCURSES_SP_ARG);
	}
    } else {
	reset_color_pair(NCURSES_SP_ARG);
	if (old_pair < 0)
	    return;
    }

#if NCURSES_EXT_FUNCS
    if (isDefaultColor(fg))
	fg = (NCURSES_COLOR_T) default_fg(NCURSES_SP_ARG);
    if (isDefaultColor(bg))
	bg = (NCURSES_COLOR_T) default_bg(NCURSES_SP_ARG);
#endif

    if (reverse) {
	NCURSES_COLOR_T xx = fg;
	fg = bg;
	bg = xx;
    }

    TR(TRACE_ATTRS, ("setting colors: pair = %d, fg = %d, bg = %d", pair,
		     fg, bg));

    if (!isDefaultColor(fg)) {
	set_foreground_color(NCURSES_SP_ARGx fg, outc);
    }
    if (!isDefaultColor(bg)) {
	set_background_color(NCURSES_SP_ARGx bg, outc);
    }
#endif
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(void)
_nc_do_color(int old_pair, int pair, int reverse, NCURSES_OUTC outc)
{
    SetSafeOutcWrapper(outc);
    NCURSES_SP_NAME(_nc_do_color) (CURRENT_SCREEN,
				   old_pair,
				   pair,
				   reverse,
				   _nc_outc_wrapper);
}
#endif
