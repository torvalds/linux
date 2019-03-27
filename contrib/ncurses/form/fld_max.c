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
 *   Author:  Juergen Pfeifer, 1995,1997                                    *
 ****************************************************************************/

#include "form.priv.h"

MODULE_ID("$Id: fld_max.c,v 1.13 2013/08/24 22:59:28 tom Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  int set_max_field(FIELD *field, int maxgrow)
|   
|   Description   :  Set the maximum growth for a dynamic field. If maxgrow=0
|                    the field may grow to any possible size.
|
|   Return Values :  E_OK           - success
|                    E_BAD_ARGUMENT - invalid argument
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
set_max_field(FIELD *field, int maxgrow)
{
  T((T_CALLED("set_max_field(%p,%d)"), (void *)field, maxgrow));

  if (!field || (maxgrow < 0))
    RETURN(E_BAD_ARGUMENT);
  else
    {
      bool single_line_field = Single_Line_Field(field);

      if (maxgrow > 0)
	{
	  if ((single_line_field && (maxgrow < field->dcols)) ||
	      (!single_line_field && (maxgrow < field->drows)))
	    RETURN(E_BAD_ARGUMENT);
	}
      field->maxgrow = maxgrow;
      ClrStatus(field, _MAY_GROW);
      if (!((unsigned)field->opts & O_STATIC))
	{
	  if ((maxgrow == 0) ||
	      (single_line_field && (field->dcols < maxgrow)) ||
	      (!single_line_field && (field->drows < maxgrow)))
	    SetStatus(field, _MAY_GROW);
	}
    }
  RETURN(E_OK);
}

/* fld_max.c ends here */
