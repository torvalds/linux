/****************************************************************************
 * Copyright (c) 1998-2004,2010 Free Software Foundation, Inc.              *
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
 *   Author:  Juergen Pfeifer, 1995,1997                                    *
 ****************************************************************************/

#include "form.priv.h"

MODULE_ID("$Id: frm_cursor.c,v 1.10 2010/01/23 21:14:36 tom Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  int pos_form_cursor(FORM * form)
|   
|   Description   :  Moves the form window cursor to the location required
|                    by the form driver to resume form processing. This may
|                    be needed after the application calls a curses library
|                    I/O routine that modifies the cursor position.
|
|   Return Values :  E_OK                      - Success
|                    E_SYSTEM_ERROR            - System error.
|                    E_BAD_ARGUMENT            - Invalid form pointer
|                    E_NOT_POSTED              - Form is not posted
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
pos_form_cursor(FORM *form)
{
  int res;

  T((T_CALLED("pos_form_cursor(%p)"), (void *)form));

  if (!form)
    res = E_BAD_ARGUMENT;
  else
    {
      if (!(form->status & _POSTED))
	res = E_NOT_POSTED;
      else
	res = _nc_Position_Form_Cursor(form);
    }
  RETURN(res);
}

/* frm_cursor.c ends here */
