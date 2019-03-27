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

MODULE_ID("$Id: frm_user.c,v 1.15 2010/01/23 21:14:36 tom Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  int set_form_userptr(FORM *form, void *usrptr)
|   
|   Description   :  Set the pointer that is reserved in any form to store
|                    application relevant informations
|
|   Return Values :  E_OK         - on success
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
set_form_userptr(FORM *form, void *usrptr)
{
  T((T_CALLED("set_form_userptr(%p,%p)"), (void *)form, (void *)usrptr));

  Normalize_Form(form)->usrptr = usrptr;
  RETURN(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  void *form_userptr(const FORM *form)
|   
|   Description   :  Return the pointer that is reserved in any form to
|                    store application relevant informations.
|
|   Return Values :  Value of pointer. If no such pointer has been set,
|                    NULL is returned
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(void *)
form_userptr(const FORM *form)
{
  T((T_CALLED("form_userptr(%p)"), (const void *)form));
  returnVoidPtr(Normalize_Form(form)->usrptr);
}

/* frm_user.c ends here */
