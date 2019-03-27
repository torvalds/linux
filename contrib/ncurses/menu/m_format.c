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

/***************************************************************************
* Module m_format                                                          *
* Set and get maximum numbers of rows and columns in menus                 *
***************************************************************************/

#include "menu.priv.h"

MODULE_ID("$Id: m_format.c,v 1.18 2012/06/09 23:54:02 tom Exp $")

#define minimum(a,b) ((a)<(b) ? (a): (b))

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu
|   Function      :  int set_menu_format(MENU *menu, int rows, int cols)
|
|   Description   :  Sets the maximum number of rows and columns of items
|                    that may be displayed at one time on a menu. If the
|                    menu contains more items than can be displayed at
|                    once, the menu will be scrollable.
|
|   Return Values :  E_OK                   - success
|                    E_BAD_ARGUMENT         - invalid values passed
|                    E_NOT_CONNECTED        - there are no items connected
|                    E_POSTED               - the menu is already posted
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
set_menu_format(MENU * menu, int rows, int cols)
{
  int total_rows, total_cols;

  T((T_CALLED("set_menu_format(%p,%d,%d)"), (void *)menu, rows, cols));

  if (rows < 0 || cols < 0)
    RETURN(E_BAD_ARGUMENT);

  if (menu)
    {
      if (menu->status & _POSTED)
	RETURN(E_POSTED);

      if (!(menu->items))
	RETURN(E_NOT_CONNECTED);

      if (rows == 0)
	rows = menu->frows;
      if (cols == 0)
	cols = menu->fcols;

      if (menu->pattern)
	Reset_Pattern(menu);

      menu->frows = (short)rows;
      menu->fcols = (short)cols;

      assert(rows > 0 && cols > 0);
      total_rows = (menu->nitems - 1) / cols + 1;
      total_cols = (menu->opt & O_ROWMAJOR) ?
	minimum(menu->nitems, cols) :
	(menu->nitems - 1) / total_rows + 1;

      menu->rows = (short)total_rows;
      menu->cols = (short)total_cols;
      menu->arows = (short)minimum(total_rows, rows);
      menu->toprow = 0;
      menu->curitem = *(menu->items);
      assert(menu->curitem);
      SetStatus(menu, _LINK_NEEDED);
      _nc_Calculate_Item_Length_and_Width(menu);
    }
  else
    {
      if (rows > 0)
	_nc_Default_Menu.frows = (short)rows;
      if (cols > 0)
	_nc_Default_Menu.fcols = (short)cols;
    }

  RETURN(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu
|   Function      :  void menu_format(const MENU *menu, int *rows, int *cols)
|
|   Description   :  Returns the maximum number of rows and columns that may
|                    be displayed at one time on menu.
|
|   Return Values :  -
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(void)
menu_format(const MENU * menu, int *rows, int *cols)
{
  if (rows)
    *rows = Normalize_Menu(menu)->frows;
  if (cols)
    *cols = Normalize_Menu(menu)->fcols;
}

/* m_format.c ends here */
