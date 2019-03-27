/****************************************************************************
 * Copyright (c) 1998-2005,2010 Free Software Foundation, Inc.              *
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
* Module m_items                                                           *
* Connect and disconnect items to and from menus                           *
***************************************************************************/

#include "menu.priv.h"

MODULE_ID("$Id: m_items.c,v 1.17 2010/01/23 21:20:10 tom Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  int set_menu_items(MENU *menu, ITEM **items)
|   
|   Description   :  Sets the item pointer array connected to menu.
|
|   Return Values :  E_OK           - success
|                    E_POSTED       - menu is already posted
|                    E_CONNECTED    - one or more items are already connected
|                                     to another menu.
|                    E_BAD_ARGUMENT - An incorrect menu or item array was
|                                     passed to the function
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
set_menu_items(MENU * menu, ITEM ** items)
{
  T((T_CALLED("set_menu_items(%p,%p)"), (void *)menu, (void *)items));

  if (!menu || (items && !(*items)))
    RETURN(E_BAD_ARGUMENT);

  if (menu->status & _POSTED)
    RETURN(E_POSTED);

  if (menu->items)
    _nc_Disconnect_Items(menu);

  if (items)
    {
      if (!_nc_Connect_Items(menu, items))
	RETURN(E_CONNECTED);
    }

  menu->items = items;
  RETURN(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  ITEM **menu_items(const MENU *menu)
|   
|   Description   :  Returns a pointer to the item pointer array of the menu
|
|   Return Values :  NULL on error
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(ITEM **)
menu_items(const MENU * menu)
{
  T((T_CALLED("menu_items(%p)"), (const void *)menu));
  returnItemPtr(menu ? menu->items : (ITEM **) 0);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  int item_count(const MENU *menu)
|   
|   Description   :  Get the number of items connected to the menu. If the
|                    menu pointer is NULL we return -1.         
|
|   Return Values :  Number of items or -1 to indicate error.
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
item_count(const MENU * menu)
{
  T((T_CALLED("item_count(%p)"), (const void *)menu));
  returnCode(menu ? menu->nitems : -1);
}

/* m_items.c ends here */
