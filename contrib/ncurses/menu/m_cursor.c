/****************************************************************************
 * Copyright (c) 1998-2009,2010 Free Software Foundation, Inc.              *
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
* Module m_cursor                                                          *
* Correctly position a menu's cursor                                       *
***************************************************************************/

#include "menu.priv.h"

MODULE_ID("$Id: m_cursor.c,v 1.22 2010/01/23 21:20:10 tom Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu
|   Function      :  _nc_menu_cursor_pos
|
|   Description   :  Return position of logical cursor to current item
|
|   Return Values :  E_OK            - success
|                    E_BAD_ARGUMENT  - invalid menu
|                    E_NOT_POSTED    - Menu is not posted
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
_nc_menu_cursor_pos(const MENU * menu, const ITEM * item, int *pY, int *pX)
{
  if (!menu || !pX || !pY)
    return (E_BAD_ARGUMENT);
  else
    {
      if ((ITEM *) 0 == item)
	item = menu->curitem;
      assert(item != (ITEM *) 0);

      if (!(menu->status & _POSTED))
	return (E_NOT_POSTED);

      *pX = item->x * (menu->spc_cols + menu->itemlen);
      *pY = (item->y - menu->toprow) * menu->spc_rows;
    }
  return (E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu
|   Function      :  pos_menu_cursor
|
|   Description   :  Position logical cursor to current item in menu
|
|   Return Values :  E_OK            - success
|                    E_BAD_ARGUMENT  - invalid menu
|                    E_NOT_POSTED    - Menu is not posted
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
pos_menu_cursor(const MENU * menu)
{
  WINDOW *win, *sub;
  int x = 0, y = 0;
  int err = _nc_menu_cursor_pos(menu, (ITEM *) 0, &y, &x);

  T((T_CALLED("pos_menu_cursor(%p)"), (const void *)menu));

  if (E_OK == err)
    {
      win = Get_Menu_UserWin(menu);
      sub = menu->usersub ? menu->usersub : win;
      assert(win && sub);

      if ((menu->opt & O_SHOWMATCH) && (menu->pindex > 0))
	x += (menu->pindex + menu->marklen - 1);

      wmove(sub, y, x);

      if (win != sub)
	{
	  wcursyncup(sub);
	  wsyncup(sub);
	  untouchwin(sub);
	}
    }
  RETURN(err);
}

/* m_cursor.c ends here */
