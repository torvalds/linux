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

/***************************************************************************
* Module m_item_val                                                        *
* Set and get menus item values                                            *
***************************************************************************/

#include "menu.priv.h"

MODULE_ID("$Id: m_item_val.c,v 1.15 2010/01/23 21:20:10 tom Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  int set_item_value(ITEM *item, int value)
|   
|   Description   :  Programmatically set the item's selection value. This is
|                    only allowed if the item is selectable at all and if
|                    it is not connected to a single-valued menu.
|                    If the item is connected to a posted menu, the menu
|                    will be redisplayed.  
|
|   Return Values :  E_OK              - success
|                    E_REQUEST_DENIED  - not selectable or single valued menu
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
set_item_value(ITEM * item, bool value)
{
  MENU *menu;

  T((T_CALLED("set_item_value(%p,%d)"), (void *)item, value));
  if (item)
    {
      menu = item->imenu;

      if ((!(item->opt & O_SELECTABLE)) ||
	  (menu && (menu->opt & O_ONEVALUE)))
	RETURN(E_REQUEST_DENIED);

      if (item->value ^ value)
	{
	  item->value = value ? TRUE : FALSE;
	  if (menu)
	    {
	      if (menu->status & _POSTED)
		{
		  Move_And_Post_Item(menu, item);
		  _nc_Show_Menu(menu);
		}
	    }
	}
    }
  else
    _nc_Default_Item.value = value;

  RETURN(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  bool item_value(const ITEM *item)
|   
|   Description   :  Return the selection value of the item
|
|   Return Values :  TRUE   - if item is selected
|                    FALSE  - if item is not selected
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(bool)
item_value(const ITEM * item)
{
  T((T_CALLED("item_value(%p)"), (const void *)item));
  returnBool((Normalize_Item(item)->value) ? TRUE : FALSE);
}

/* m_item_val.c ends here */
