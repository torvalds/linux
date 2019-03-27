/****************************************************************************
 * Copyright (c) 1998-2010,2012 Free Software Foundation, Inc.              *
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

MODULE_ID("$Id: fld_page.c,v 1.12 2012/06/10 00:12:47 tom Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  int set_new_page(FIELD *field, bool new_page_flag)
|   
|   Description   :  Marks the field as the beginning of a new page of 
|                    the form.
|
|   Return Values :  E_OK         - success
|                    E_CONNECTED  - field is connected
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
set_new_page(FIELD *field, bool new_page_flag)
{
  T((T_CALLED("set_new_page(%p,%d)"), (void *)field, new_page_flag));

  Normalize_Field(field);
  if (field->form)
    RETURN(E_CONNECTED);

  if (new_page_flag)
    SetStatus(field, _NEWPAGE);
  else
    ClrStatus(field, _NEWPAGE);

  RETURN(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  bool new_page(const FIELD *field)
|   
|   Description   :  Retrieve the info whether or not the field starts a
|                    new page on the form.
|
|   Return Values :  TRUE  - field starts a new page
|                    FALSE - field doesn't start a new page
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(bool)
new_page(const FIELD *field)
{
  T((T_CALLED("new_page(%p)"), (const void *)field));

  returnBool((Normalize_Field(field)->status & _NEWPAGE) ? TRUE : FALSE);
}

/* fld_page.c ends here */
