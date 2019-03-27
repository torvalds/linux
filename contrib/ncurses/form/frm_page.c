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

MODULE_ID("$Id: frm_page.c,v 1.12 2012/06/10 00:28:04 tom Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  int set_form_page(FORM * form,int  page)
|   
|   Description   :  Set the page number of the form.
|
|   Return Values :  E_OK              - success
|                    E_BAD_ARGUMENT    - invalid form pointer or page number
|                    E_BAD_STATE       - called from a hook routine
|                    E_INVALID_FIELD   - current field can't be left
|                    E_SYSTEM_ERROR    - system error
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
set_form_page(FORM *form, int page)
{
  int err = E_OK;

  T((T_CALLED("set_form_page(%p,%d)"), (void *)form, page));

  if (!form || (page < 0) || (page >= form->maxpage))
    RETURN(E_BAD_ARGUMENT);

  if (!(form->status & _POSTED))
    {
      form->curpage = (short)page;
      form->current = _nc_First_Active_Field(form);
    }
  else
    {
      if (form->status & _IN_DRIVER)
	err = E_BAD_STATE;
      else
	{
	  if (form->curpage != page)
	    {
	      if (!_nc_Internal_Validation(form))
		err = E_INVALID_FIELD;
	      else
		{
		  Call_Hook(form, fieldterm);
		  Call_Hook(form, formterm);
		  err = _nc_Set_Form_Page(form, page, (FIELD *)0);
		  Call_Hook(form, forminit);
		  Call_Hook(form, fieldinit);
		  _nc_Refresh_Current_Field(form);
		}
	    }
	}
    }
  RETURN(err);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  int form_page(const FORM * form)
|   
|   Description   :  Return the current page of the form.
|
|   Return Values :  >= 0  : current page number
|                    -1    : invalid form pointer
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
form_page(const FORM *form)
{
  T((T_CALLED("form_page(%p)"), (const void *)form));

  returnCode(Normalize_Form(form)->curpage);
}

/* frm_page.c ends here */
