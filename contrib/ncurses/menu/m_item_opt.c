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
* Module m_item_opt                                                        *
* Menus item option routines                                               *
***************************************************************************/

#include "menu.priv.h"

MODULE_ID("$Id: m_item_opt.c,v 1.18 2010/01/23 21:20:10 tom Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  int set_item_opts(ITEM *item, Item_Options opts)  
|   
|   Description   :  Set the options of the item. If there are relevant
|                    changes, the item is connected and the menu is posted,
|                    the menu will be redisplayed.
|
|   Return Values :  E_OK            - success
|                    E_BAD_ARGUMENT  - invalid item options
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
set_item_opts(ITEM * item, Item_Options opts)
{
  T((T_CALLED("set_menu_opts(%p,%d)"), (void *)item, opts));

  opts &= ALL_ITEM_OPTS;

  if (opts & ~ALL_ITEM_OPTS)
    RETURN(E_BAD_ARGUMENT);

  if (item)
    {
      if (item->opt != opts)
	{
	  MENU *menu = item->imenu;

	  item->opt = opts;

	  if ((!(opts & O_SELECTABLE)) && item->value)
	    item->value = FALSE;

	  if (menu && (menu->status & _POSTED))
	    {
	      Move_And_Post_Item(menu, item);
	      _nc_Show_Menu(menu);
	    }
	}
    }
  else
    _nc_Default_Item.opt = opts;

  RETURN(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  int item_opts_off(ITEM *item, Item_Options opts)   
|   
|   Description   :  Switch of the options for this item.
|
|   Return Values :  E_OK            - success
|                    E_BAD_ARGUMENT  - invalid options
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
item_opts_off(ITEM * item, Item_Options opts)
{
  ITEM *citem = item;		/* use a copy because set_item_opts must detect

				   NULL item itself to adjust its behavior */

  T((T_CALLED("item_opts_off(%p,%d)"), (void *)item, opts));

  if (opts & ~ALL_ITEM_OPTS)
    RETURN(E_BAD_ARGUMENT);
  else
    {
      Normalize_Item(citem);
      opts = citem->opt & ~(opts & ALL_ITEM_OPTS);
      returnCode(set_item_opts(item, opts));
    }
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  int item_opts_on(ITEM *item, Item_Options opts)   
|   
|   Description   :  Switch on the options for this item.
|
|   Return Values :  E_OK            - success
|                    E_BAD_ARGUMENT  - invalid options
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
item_opts_on(ITEM * item, Item_Options opts)
{
  ITEM *citem = item;		/* use a copy because set_item_opts must detect

				   NULL item itself to adjust its behavior */

  T((T_CALLED("item_opts_on(%p,%d)"), (void *)item, opts));

  opts &= ALL_ITEM_OPTS;
  if (opts & ~ALL_ITEM_OPTS)
    RETURN(E_BAD_ARGUMENT);
  else
    {
      Normalize_Item(citem);
      opts = citem->opt | opts;
      returnCode(set_item_opts(item, opts));
    }
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  Item_Options item_opts(const ITEM *item)   
|   
|   Description   :  Switch of the options for this item.
|
|   Return Values :  Items options
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(Item_Options)
item_opts(const ITEM * item)
{
  T((T_CALLED("item_opts(%p)"), (const void *)item));
  returnItemOpts(ALL_ITEM_OPTS & Normalize_Item(item)->opt);
}

/* m_item_opt.c ends here */
