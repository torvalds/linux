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

MODULE_ID("$Id: frm_post.c,v 1.11 2012/06/10 00:27:49 tom Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  int post_form(FORM * form)
|   
|   Description   :  Writes the form into its associated subwindow.
|
|   Return Values :  E_OK              - success
|                    E_BAD_ARGUMENT    - invalid form pointer
|                    E_POSTED          - form already posted
|                    E_NOT_CONNECTED   - no fields connected to form
|                    E_NO_ROOM         - form doesn't fit into subwindow
|                    E_SYSTEM_ERROR    - system error
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
post_form(FORM *form)
{
  WINDOW *formwin;
  int err;
  int page;

  T((T_CALLED("post_form(%p)"), (void *)form));

  if (!form)
    RETURN(E_BAD_ARGUMENT);

  if (form->status & _POSTED)
    RETURN(E_POSTED);

  if (!(form->field))
    RETURN(E_NOT_CONNECTED);

  formwin = Get_Form_Window(form);
  if ((form->cols > getmaxx(formwin)) || (form->rows > getmaxy(formwin)))
    RETURN(E_NO_ROOM);

  /* reset form->curpage to an invald value. This forces Set_Form_Page
     to do the page initialization which is required by post_form.
   */
  page = form->curpage;
  form->curpage = -1;
  if ((err = _nc_Set_Form_Page(form, page, form->current)) != E_OK)
    RETURN(err);

  SetStatus(form, _POSTED);

  Call_Hook(form, forminit);
  Call_Hook(form, fieldinit);

  _nc_Refresh_Current_Field(form);
  RETURN(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  int unpost_form(FORM * form)
|   
|   Description   :  Erase form from its associated subwindow.
|
|   Return Values :  E_OK            - success
|                    E_BAD_ARGUMENT  - invalid form pointer
|                    E_NOT_POSTED    - form isn't posted
|                    E_BAD_STATE     - called from a hook routine
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
unpost_form(FORM *form)
{
  T((T_CALLED("unpost_form(%p)"), (void *)form));

  if (!form)
    RETURN(E_BAD_ARGUMENT);

  if (!(form->status & _POSTED))
    RETURN(E_NOT_POSTED);

  if (form->status & _IN_DRIVER)
    RETURN(E_BAD_STATE);

  Call_Hook(form, fieldterm);
  Call_Hook(form, formterm);

  werase(Get_Form_Window(form));
  delwin(form->w);
  form->w = (WINDOW *)0;
  ClrStatus(form, _POSTED);
  RETURN(E_OK);
}

/* frm_post.c ends here */
