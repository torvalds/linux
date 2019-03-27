/****************************************************************************
 * Copyright (c) 2000-2008,2012 Free Software Foundation, Inc.              *
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

#include <ctype.h>
#include <termcap.h>

MODULE_ID("$Id: lib_tgoto.c,v 1.16 2012/02/24 02:08:08 tom Exp $")

#if !PURE_TERMINFO
static bool
is_termcap(const char *string)
{
    bool result = TRUE;

    if (string == 0 || *string == '\0') {
	result = FALSE;		/* tparm() handles empty strings */
    } else {
	while ((*string != '\0') && result) {
	    if (*string == '%') {
		switch (*++string) {
		case 'p':
		    result = FALSE;
		    break;
		case '\0':
		    string--;
		    break;
		}
	    } else if (string[0] == '$' && string[1] == '<') {
		result = FALSE;
	    }
	    string++;
	}
    }
    return result;
}

static char *
tgoto_internal(const char *string, int x, int y)
{
    static char *result;
    static size_t length;

    int swap_arg;
    int param[3];
    size_t used = 0;
    size_t need = 10;
    int *value = param;
    bool need_BC = FALSE;

    if (BC)
	need += strlen(BC);

    param[0] = y;
    param[1] = x;
    param[2] = 0;

    while (*string != 0) {
	if ((used + need) > length) {
	    length += (used + need);
	    if ((result = typeRealloc(char, length, result)) == 0) {
		length = 0;
		break;
	    }
	}
	if (*string == '%') {
	    const char *fmt = 0;

	    switch (*++string) {
	    case '\0':
		string--;
		break;
	    case 'd':
		fmt = "%d";
		break;
	    case '2':
		fmt = "%02d";
		*value %= 100;
		break;
	    case '3':
		fmt = "%03d";
		*value %= 1000;
		break;
	    case '+':
		*value += UChar(*++string);
		/* FALLTHRU */
	    case '.':
		/*
		 * Guard against tputs() seeing a truncated string.  The
		 * termcap documentation refers to a similar fixup for \n
		 * and \r, but I don't see that it could work -TD
		 */
		if (*value == 0) {
		    if (BC != 0) {
			*value += 1;
			need_BC = TRUE;
		    } else {
			*value = 0200;	/* tputs will treat this as \0 */
		    }
		}
		result[used++] = (char) *value++;
		break;
	    case '%':
		result[used++] = *string;
		break;
	    case 'r':
		swap_arg = param[0];
		param[0] = param[1];
		param[1] = swap_arg;
		break;
	    case 'i':
		param[0] += 1;
		param[1] += 1;
		break;
	    case '>':
		if (*value > string[1])
		    *value += string[2];
		string += 2;
		break;
	    case 'n':		/* Datamedia 2500 */
		param[0] ^= 0140;
		param[1] ^= 0140;
		break;
	    case 'B':		/* BCD */
		*value = 16 * (*value / 10) + (*value % 10);
		break;
	    case 'D':		/* Reverse coding (Delta Data) */
		*value -= 2 * (*value % 16);
		break;
	    }
	    if (fmt != 0) {
		_nc_SPRINTF(result + used, _nc_SLIMIT(length - used)
			    fmt, *value++);
		used += strlen(result + used);
		fmt = 0;
	    }
	    if (value - param > 2) {
		value = param + 2;
		*value = 0;
	    }
	} else {
	    result[used++] = *string;
	}
	string++;
    }
    if (result != 0) {
	if (need_BC) {
	    _nc_STRCPY(result + used, BC, length - used);
	    used += strlen(BC);
	}
	result[used] = '\0';
    }
    return result;
}
#endif

/*
 * Retained solely for upward compatibility.  Note the intentional reversing of
 * the last two arguments when invoking tparm().
 */
NCURSES_EXPORT(char *)
tgoto(const char *string, int x, int y)
{
    char *result;

    T((T_CALLED("tgoto(%s, %d, %d)"), _nc_visbuf(string), x, y));
#if !PURE_TERMINFO
    if (is_termcap(string))
	result = tgoto_internal(string, x, y);
    else
#endif
	result = TPARM_2((NCURSES_CONST char *) string, y, x);
    returnPtr(result);
}
