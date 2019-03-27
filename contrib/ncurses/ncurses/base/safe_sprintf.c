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
 *  Author: Thomas E. Dickey        1997-on                                 *
 ****************************************************************************/

#include <curses.priv.h>
#include <ctype.h>

MODULE_ID("$Id: safe_sprintf.c,v 1.27 2013/01/20 01:04:32 tom Exp $")

#if USE_SAFE_SPRINTF

typedef enum {
    Flags, Width, Prec, Type, Format
} PRINTF;

#define VA_INTGR(type) ival = va_arg(ap, type)
#define VA_FLOAT(type) fval = va_arg(ap, type)
#define VA_POINT(type) pval = (void *)va_arg(ap, type)

/*
 * Scan a variable-argument list for printf to determine the number of
 * characters that would be emitted.
 */
static int
_nc_printf_length(const char *fmt, va_list ap)
{
    size_t length = BUFSIZ;
    char *buffer;
    char *format;
    int len = 0;
    size_t fmt_len;
    char fmt_arg[BUFSIZ];

    if (fmt == 0 || *fmt == '\0')
	return 0;
    fmt_len = strlen(fmt) + 1;
    if ((format = typeMalloc(char, fmt_len)) == 0)
	  return -1;
    if ((buffer = typeMalloc(char, length)) == 0) {
	free(format);
	return -1;
    }

    while (*fmt != '\0') {
	if (*fmt == '%') {
	    static char dummy[] = "";
	    PRINTF state = Flags;
	    char *pval = dummy;	/* avoid const-cast */
	    double fval = 0.0;
	    int done = FALSE;
	    int ival = 0;
	    int prec = -1;
	    int type = 0;
	    int used = 0;
	    int width = -1;
	    size_t f = 0;

	    format[f++] = *fmt;
	    while (*++fmt != '\0' && len >= 0 && !done) {
		format[f++] = *fmt;

		if (isdigit(UChar(*fmt))) {
		    int num = *fmt - '0';
		    if (state == Flags && num != 0)
			state = Width;
		    if (state == Width) {
			if (width < 0)
			    width = 0;
			width = (width * 10) + num;
		    } else if (state == Prec) {
			if (prec < 0)
			    prec = 0;
			prec = (prec * 10) + num;
		    }
		} else if (*fmt == '*') {
		    VA_INTGR(int);
		    if (state == Flags)
			state = Width;
		    if (state == Width) {
			width = ival;
		    } else if (state == Prec) {
			prec = ival;
		    }
		    _nc_SPRINTF(fmt_arg,
				_nc_SLIMIT(sizeof(fmt_arg))
				"%d", ival);
		    fmt_len += strlen(fmt_arg);
		    if ((format = _nc_doalloc(format, fmt_len)) == 0) {
			free(buffer);
			return -1;
		    }
		    --f;
		    _nc_STRCPY(&format[f], fmt_arg, fmt_len - f);
		    f = strlen(format);
		} else if (isalpha(UChar(*fmt))) {
		    done = TRUE;
		    switch (*fmt) {
		    case 'Z':	/* FALLTHRU */
		    case 'h':	/* FALLTHRU */
		    case 'l':	/* FALLTHRU */
			done = FALSE;
			type = *fmt;
			break;
		    case 'i':	/* FALLTHRU */
		    case 'd':	/* FALLTHRU */
		    case 'u':	/* FALLTHRU */
		    case 'x':	/* FALLTHRU */
		    case 'X':	/* FALLTHRU */
			if (type == 'l')
			    VA_INTGR(long);
			else if (type == 'Z')
			    VA_INTGR(size_t);
			else
			    VA_INTGR(int);
			used = 'i';
			break;
		    case 'f':	/* FALLTHRU */
		    case 'e':	/* FALLTHRU */
		    case 'E':	/* FALLTHRU */
		    case 'g':	/* FALLTHRU */
		    case 'G':	/* FALLTHRU */
			VA_FLOAT(double);
			used = 'f';
			break;
		    case 'c':
			VA_INTGR(int);
			used = 'i';
			break;
		    case 's':
			VA_POINT(char *);
			if (prec < 0)
			    prec = strlen(pval);
			if (prec > (int) length) {
			    length = length + prec;
			    buffer = typeRealloc(char, length, buffer);
			    if (buffer == 0) {
				free(format);
				return -1;
			    }
			}
			used = 'p';
			break;
		    case 'p':
			VA_POINT(void *);
			used = 'p';
			break;
		    case 'n':
			VA_POINT(int *);
			used = 0;
			break;
		    default:
			break;
		    }
		} else if (*fmt == '.') {
		    state = Prec;
		} else if (*fmt == '%') {
		    done = TRUE;
		    used = 'p';
		}
	    }
	    format[f] = '\0';
	    switch (used) {
	    case 'i':
		_nc_SPRINTF(buffer, _nc_SLIMIT(length) format, ival);
		break;
	    case 'f':
		_nc_SPRINTF(buffer, _nc_SLIMIT(length) format, fval);
		break;
	    default:
		_nc_SPRINTF(buffer, _nc_SLIMIT(length) format, pval);
		break;
	    }
	    len += (int) strlen(buffer);
	} else {
	    fmt++;
	    len++;
	}
    }

    free(buffer);
    free(format);
    return len;
}
#endif

#define my_buffer _nc_globals.safeprint_buf
#define my_length _nc_globals.safeprint_used

/*
 * Wrapper for vsprintf that allocates a buffer big enough to hold the result.
 */
NCURSES_EXPORT(char *)
NCURSES_SP_NAME(_nc_printf_string) (NCURSES_SP_DCLx
				    const char *fmt,
				    va_list ap)
{
    char *result = 0;

    if (fmt != 0) {
#if USE_SAFE_SPRINTF
	va_list ap2;
	int len;

	begin_va_copy(ap2, ap);
	len = _nc_printf_length(fmt, ap2);
	end_va_copy(ap2);

	if ((int) my_length < len + 1) {
	    my_length = 2 * (len + 1);
	    my_buffer = typeRealloc(char, my_length, my_buffer);
	}
	if (my_buffer != 0) {
	    *my_buffer = '\0';
	    if (len >= 0) {
		vsprintf(my_buffer, fmt, ap);
	    }
	    result = my_buffer;
	}
#else
#define MyCols _nc_globals.safeprint_cols
#define MyRows _nc_globals.safeprint_rows

	if (screen_lines(SP_PARM) > MyRows || screen_columns(SP_PARM) > MyCols) {
	    if (screen_lines(SP_PARM) > MyRows)
		MyRows = screen_lines(SP_PARM);
	    if (screen_columns(SP_PARM) > MyCols)
		MyCols = screen_columns(SP_PARM);
	    my_length = (size_t) (MyRows * (MyCols + 1)) + 1;
	    my_buffer = typeRealloc(char, my_length, my_buffer);
	}

	if (my_buffer != 0) {
# if HAVE_VSNPRINTF
	    vsnprintf(my_buffer, my_length, fmt, ap);	/* GNU extension */
# else
	    vsprintf(my_buffer, fmt, ap);	/* ANSI */
# endif
	    result = my_buffer;
	}
#endif
    } else if (my_buffer != 0) {	/* see _nc_freeall() */
	free(my_buffer);
	my_buffer = 0;
	my_length = 0;
    }
    return result;
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(char *)
_nc_printf_string(const char *fmt, va_list ap)
{
    return NCURSES_SP_NAME(_nc_printf_string) (CURRENT_SCREEN, fmt, ap);
}
#endif
