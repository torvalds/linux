/****************************************************************************
 * Copyright (c) 1998-2011,2012 Free Software Foundation, Inc.              *
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
 *  Author: Juergen Pfeifer                                                 *
 *     and: Thomas E. Dickey                                                *
 ****************************************************************************/

/*
 *	lib_slkset.c
 *      Set soft label text.
 */
#include <curses.priv.h>
#include <ctype.h>

#if USE_WIDEC_SUPPORT
#if HAVE_WCTYPE_H
#include <wctype.h>
#endif
#endif

MODULE_ID("$Id: lib_slkset.c,v 1.24 2012/12/08 23:09:25 tom Exp $")

NCURSES_EXPORT(int)
NCURSES_SP_NAME(slk_set) (NCURSES_SP_DCLx int i, const char *astr, int format)
{
    SLK *slk;
    int offset = 0;
    int numchrs;
    int numcols;
    int limit;
    const char *str = astr;
    const char *p;

    T((T_CALLED("slk_set(%p, %d, \"%s\", %d)"), (void *) SP_PARM, i, str, format));

    if (SP_PARM == 0
	|| (slk = SP_PARM->_slk) == 0
	|| i < 1
	|| i > slk->labcnt
	|| format < 0
	|| format > 2)
	returnCode(ERR);
    if (str == 0)
	str = "";
    --i;			/* Adjust numbering of labels */

    limit = MAX_SKEY_LEN(SP_PARM->slk_format);
    while (isspace(UChar(*str)))
	str++;			/* skip over leading spaces  */
    p = str;

#if USE_WIDEC_SUPPORT
    numcols = 0;
    while (*p != 0) {
	mbstate_t state;
	wchar_t wc;
	size_t need;

	init_mb(state);
	need = mbrtowc(0, p, strlen(p), &state);
	if (need == (size_t) -1)
	    break;
	mbrtowc(&wc, p, need, &state);
	if (!iswprint((wint_t) wc))
	    break;
	if (wcwidth(wc) + numcols > limit)
	    break;
	numcols += wcwidth(wc);
	p += need;
    }
    numchrs = (int) (p - str);
#else
    while (isprint(UChar(*p)))
	p++;			/* The first non-print stops */

    numcols = (int) (p - str);
    if (numcols > limit)
	numcols = limit;
    numchrs = numcols;
#endif

    FreeIfNeeded(slk->ent[i].ent_text);
    if ((slk->ent[i].ent_text = strdup(str)) == 0)
	returnCode(ERR);
    slk->ent[i].ent_text[numchrs] = '\0';

    if ((slk->ent[i].form_text = (char *) _nc_doalloc(slk->ent[i].form_text,
						      (size_t) (limit +
								numchrs + 1))
	) == 0)
	returnCode(ERR);

    switch (format) {
    case 0:			/* left-justified */
	offset = 0;
	break;
    case 1:			/* centered */
	offset = (limit - numcols) / 2;
	break;
    case 2:			/* right-justified */
	offset = limit - numcols;
	break;
    }
    if (offset <= 0)
	offset = 0;
    else
	memset(slk->ent[i].form_text, ' ', (size_t) offset);

    memcpy(slk->ent[i].form_text + offset,
	   slk->ent[i].ent_text,
	   (size_t) numchrs);

    if (offset < limit) {
	memset(slk->ent[i].form_text + offset + numchrs,
	       ' ',
	       (size_t) (limit - (offset + numcols)));
    }

    slk->ent[i].form_text[numchrs - numcols + limit] = 0;
    slk->ent[i].dirty = TRUE;
    returnCode(OK);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
slk_set(int i, const char *astr, int format)
{
    return NCURSES_SP_NAME(slk_set) (CURRENT_SCREEN, i, astr, format);
}
#endif
