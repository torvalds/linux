/****************************************************************************
 * Copyright (c) 1998-2010,2013 Free Software Foundation, Inc.              *
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

MODULE_ID("$Id: frm_data.c,v 1.16 2013/08/24 22:44:05 tom Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  bool data_behind(const FORM *form)
|   
|   Description   :  Check for off-screen data behind. This is nearly trivial
|                    because the beginning of a field is fixed.
|
|   Return Values :  TRUE   - there are off-screen data behind
|                    FALSE  - there are no off-screen data behind
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(bool)
data_behind(const FORM *form)
{
  bool result = FALSE;

  T((T_CALLED("data_behind(%p)"), (const void *)form));

  if (form && (form->status & _POSTED) && form->current)
    {
      FIELD *field;

      field = form->current;
      if (!Single_Line_Field(field))
	{
	  result = (form->toprow == 0) ? FALSE : TRUE;
	}
      else
	{
	  result = (form->begincol == 0) ? FALSE : TRUE;
	}
    }
  returnBool(result);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  static char * Only_Padding(
|                                    WINDOW *w,
|                                    int len,
|                                    int pad)
|   
|   Description   :  Test if 'length' cells starting at the current position
|                    contain a padding character.
|
|   Return Values :  true if only padding cells are found
+--------------------------------------------------------------------------*/
NCURSES_INLINE static bool
Only_Padding(WINDOW *w, int len, int pad)
{
  bool result = TRUE;
  int y, x, j;
  FIELD_CELL cell;

  getyx(w, y, x);
  for (j = 0; j < len; ++j)
    {
      if (wmove(w, y, x + j) != ERR)
	{
#if USE_WIDEC_SUPPORT
	  if (win_wch(w, &cell) != ERR)
	    {
	      if ((chtype)CharOf(cell) != ChCharOf(pad)
		  || cell.chars[1] != 0)
		{
		  result = FALSE;
		  break;
		}
	    }
#else
	  cell = (FIELD_CELL) winch(w);
	  if (ChCharOf(cell) != ChCharOf(pad))
	    {
	      result = FALSE;
	      break;
	    }
#endif
	}
      else
	{
	  /* if an error, return true: no non-padding text found */
	  break;
	}
    }
  /* no need to reset the cursor position; caller does this */
  return result;
}

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  bool data_ahead(const FORM *form)
|   
|   Description   :  Check for off-screen data ahead. This is more difficult
|                    because a dynamic field has a variable end. 
|
|   Return Values :  TRUE   - there are off-screen data ahead
|                    FALSE  - there are no off-screen data ahead
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(bool)
data_ahead(const FORM *form)
{
  bool result = FALSE;

  T((T_CALLED("data_ahead(%p)"), (const void *)form));

  if (form && (form->status & _POSTED) && form->current)
    {
      FIELD *field;
      bool cursor_moved = FALSE;
      int pos;

      field = form->current;
      assert(form->w);

      if (Single_Line_Field(field))
	{
	  int check_len;

	  pos = form->begincol + field->cols;
	  while (pos < field->dcols)
	    {
	      check_len = field->dcols - pos;
	      if (check_len >= field->cols)
		check_len = field->cols;
	      cursor_moved = TRUE;
	      wmove(form->w, 0, pos);
	      if (Only_Padding(form->w, check_len, field->pad))
		pos += field->cols;
	      else
		{
		  result = TRUE;
		  break;
		}
	    }
	}
      else
	{
	  pos = form->toprow + field->rows;
	  while (pos < field->drows)
	    {
	      cursor_moved = TRUE;
	      wmove(form->w, pos, 0);
	      pos++;
	      if (!Only_Padding(form->w, field->cols, field->pad))
		{
		  result = TRUE;
		  break;
		}
	    }
	}

      if (cursor_moved)
	wmove(form->w, form->currow, form->curcol);
    }
  returnBool(result);
}

/* frm_data.c ends here */
