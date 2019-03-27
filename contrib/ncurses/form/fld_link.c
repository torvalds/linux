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

MODULE_ID("$Id: fld_link.c,v 1.13 2012/03/11 00:37:16 tom Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  FIELD *link_field(FIELD *field, int frow, int fcol)  
|   
|   Description   :  Duplicates the field at the specified position. The
|                    new field shares its buffers with the original one,
|                    the attributes are independent.
|                    If an error occurs, errno is set to
|                    
|                    E_BAD_ARGUMENT - invalid argument
|                    E_SYSTEM_ERROR - system error
|
|   Return Values :  Pointer to the new field or NULL if failure
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(FIELD *)
link_field(FIELD *field, int frow, int fcol)
{
  FIELD *New_Field = (FIELD *)0;
  int err = E_BAD_ARGUMENT;

  T((T_CALLED("link_field(%p,%d,%d)"), (void *)field, frow, fcol));
  if (field && (frow >= 0) && (fcol >= 0) &&
      ((err = E_SYSTEM_ERROR) != 0) &&	/* trick: this resets the default error */
      (New_Field = typeMalloc(FIELD, 1)))
    {
      T((T_CREATE("field %p"), (void *)New_Field));
      *New_Field = *_nc_Default_Field;
      New_Field->frow = (short) frow;
      New_Field->fcol = (short) fcol;

      New_Field->link = field->link;
      field->link = New_Field;

      New_Field->buf = field->buf;
      New_Field->rows = field->rows;
      New_Field->cols = field->cols;
      New_Field->nrow = field->nrow;
      New_Field->nbuf = field->nbuf;
      New_Field->drows = field->drows;
      New_Field->dcols = field->dcols;
      New_Field->maxgrow = field->maxgrow;
      New_Field->just = field->just;
      New_Field->fore = field->fore;
      New_Field->back = field->back;
      New_Field->pad = field->pad;
      New_Field->opts = field->opts;
      New_Field->usrptr = field->usrptr;

      if (_nc_Copy_Type(New_Field, field))
	returnField(New_Field);
    }

  if (New_Field)
    free_field(New_Field);

  SET_ERROR(err);
  returnField((FIELD *)0);
}

/* fld_link.c ends here */
