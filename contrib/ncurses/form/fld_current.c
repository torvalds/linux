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

MODULE_ID("$Id: fld_current.c,v 1.12 2010/01/23 21:14:35 tom Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int set_current_field(FORM  * form,FIELD * field)
|
|   Description   :  Set the current field of the form to the specified one.
|
|   Return Values :  E_OK              - success
|                    E_BAD_ARGUMENT    - invalid form or field pointer
|                    E_REQUEST_DENIED  - field not selectable
|                    E_BAD_STATE       - called from a hook routine
|                    E_INVALID_FIELD   - current field can't be left
|                    E_SYSTEM_ERROR    - system error
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
set_current_field(FORM *form, FIELD *field)
{
  int err = E_OK;

  T((T_CALLED("set_current_field(%p,%p)"), (void *)form, (void *)field));
  if (form == 0 || field == 0)
    {
      RETURN(E_BAD_ARGUMENT);
    }
  else if ((form != field->form) || Field_Is_Not_Selectable(field))
    {
      RETURN(E_REQUEST_DENIED);
    }
  else if ((form->status & _POSTED) == 0)
    {
      form->current = field;
      form->curpage = field->page;
    }
  else
    {
      if ((form->status & _IN_DRIVER) != 0)
	{
	  err = E_BAD_STATE;
	}
      else
	{
	  if (form->current != field)
	    {
	      if (!_nc_Internal_Validation(form))
		{
		  err = E_INVALID_FIELD;
		}
	      else
		{
		  Call_Hook(form, fieldterm);
		  if (field->page != form->curpage)
		    {
		      Call_Hook(form, formterm);
		      err = _nc_Set_Form_Page(form, (int)field->page, field);
		      Call_Hook(form, forminit);
		    }
		  else
		    {
		      err = _nc_Set_Current_Field(form, field);
		    }
		  Call_Hook(form, fieldinit);
		  (void)_nc_Refresh_Current_Field(form);
		}
	    }
	}
    }
  RETURN(err);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  FIELD *current_field(const FORM * form)
|
|   Description   :  Return the current field.
|
|   Return Values :  Pointer to the current field.
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(FIELD *)
current_field(const FORM *form)
{
  T((T_CALLED("current_field(%p)"), (const void *)form));
  returnField(Normalize_Form(form)->current);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform
|   Function      :  int field_index(const FIELD * field)
|
|   Description   :  Return the index of the field in the field-array of
|                    the form.
|
|   Return Values :  >= 0   : field index
|                    -1     : fieldpointer invalid or field not connected
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
field_index(const FIELD *field)
{
  T((T_CALLED("field_index(%p)"), (const void *)field));
  returnCode((field != 0 && field->form != 0) ? (int)field->index : -1);
}

/* fld_current.c ends here */
