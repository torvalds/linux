/****************************************************************************
 * Copyright (c) 1998-2000,2008 Free Software Foundation, Inc.              *
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

/* $Id: capdefaults.c,v 1.14 2008/11/16 00:19:59 juergen Exp $ */

    /*
     * Compute obsolete capabilities.  The reason this is an include file is
     * that the two places where it's needed want the macros to generate
     * offsets to different structures.  See the file Caps for explanations of
     * these conversions.
     *
     * Note:  This code is the functional inverse of the first part of
     * postprocess_termcap().
     */
{
    char *strp;
    short capval;

#define EXTRACT_DELAY(str) \
    	(short) (strp = strchr(str, '*'), strp ? atoi(strp+1) : 0)

    /* current (4.4BSD) capabilities marked obsolete */
    if (VALID_STRING(carriage_return)
	&& (capval = EXTRACT_DELAY(carriage_return)))
	carriage_return_delay = capval;
    if (VALID_STRING(newline) && (capval = EXTRACT_DELAY(newline)))
	new_line_delay = capval;

    /* current (4.4BSD) capabilities not obsolete */
    if (!VALID_STRING(termcap_init2) && VALID_STRING(init_3string)) {
	termcap_init2 = init_3string;
	init_3string = ABSENT_STRING;
    }
    if (!VALID_STRING(termcap_reset)
     && VALID_STRING(reset_2string)
     && !VALID_STRING(reset_1string)
     && !VALID_STRING(reset_3string)) {
	termcap_reset = reset_2string;
	reset_2string = ABSENT_STRING;
    }
    if (magic_cookie_glitch_ul == ABSENT_NUMERIC
	&& magic_cookie_glitch != ABSENT_NUMERIC
	&& VALID_STRING(enter_underline_mode))
	magic_cookie_glitch_ul = magic_cookie_glitch;

    /* totally obsolete capabilities */
    linefeed_is_newline = (char) (VALID_STRING(newline)
				  && (strcmp("\n", newline) == 0));
    if (VALID_STRING(cursor_left)
	&& (capval = EXTRACT_DELAY(cursor_left)))
	backspace_delay = capval;
    if (VALID_STRING(tab) && (capval = EXTRACT_DELAY(tab)))
	horizontal_tab_delay = capval;
#undef EXTRACT_DELAY
}
