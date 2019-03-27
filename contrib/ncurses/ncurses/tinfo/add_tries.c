/****************************************************************************
 * Copyright (c) 1998-2009,2010 Free Software Foundation, Inc.              *
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
 *  Author: Thomas E. Dickey            1998-on                             *
 ****************************************************************************/

/*
**	add_tries.c
**
**	Add keycode/string to tries-tree.
**
*/

#include <curses.priv.h>

MODULE_ID("$Id: add_tries.c,v 1.10 2010/12/19 01:31:14 tom Exp $")

#define SET_TRY(dst,src) if ((dst->ch = *src++) == 128) dst->ch = '\0'
#define CMP_TRY(a,b) ((a)? (a == b) : (b == 128))

NCURSES_EXPORT(int)
_nc_add_to_try(TRIES ** tree, const char *str, unsigned code)
{
    TRIES *ptr, *savedptr;
    unsigned const char *txt = (unsigned const char *) str;

    T((T_CALLED("_nc_add_to_try(%p, %s, %u)"),
       (void *) *tree, _nc_visbuf(str), code));
    if (txt == 0 || *txt == '\0' || code == 0)
	returnCode(ERR);

    if ((*tree) != 0) {
	ptr = savedptr = (*tree);

	for (;;) {
	    unsigned char cmp = *txt;

	    while (!CMP_TRY(ptr->ch, cmp)
		   && ptr->sibling != 0)
		ptr = ptr->sibling;

	    if (CMP_TRY(ptr->ch, cmp)) {
		if (*(++txt) == '\0') {
		    ptr->value = (unsigned short) code;
		    returnCode(OK);
		}
		if (ptr->child != 0)
		    ptr = ptr->child;
		else
		    break;
	    } else {
		if ((ptr->sibling = typeCalloc(TRIES, 1)) == 0) {
		    returnCode(ERR);
		}

		savedptr = ptr = ptr->sibling;
		SET_TRY(ptr, txt);
		ptr->value = 0;

		break;
	    }
	}			/* end for (;;) */
    } else {			/* (*tree) == 0 :: First sequence to be added */
	savedptr = ptr = (*tree) = typeCalloc(TRIES, 1);

	if (ptr == 0) {
	    returnCode(ERR);
	}

	SET_TRY(ptr, txt);
	ptr->value = 0;
    }

    /* at this point, we are adding to the try.  ptr->child == 0 */

    while (*txt) {
	ptr->child = typeCalloc(TRIES, 1);

	ptr = ptr->child;

	if (ptr == 0) {
	    while ((ptr = savedptr) != 0) {
		savedptr = ptr->child;
		free(ptr);
	    }
	    returnCode(ERR);
	}

	SET_TRY(ptr, txt);
	ptr->value = 0;
    }

    ptr->value = (unsigned short) code;
    returnCode(OK);
}
