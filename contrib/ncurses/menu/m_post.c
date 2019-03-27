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
* Module m_post                                                            *
* Write or erase menus from associated subwindows                          *
***************************************************************************/

#include "menu.priv.h"

MODULE_ID("$Id: m_post.c,v 1.31 2012/06/09 23:54:35 tom Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu
|   Function      :  void _nc_Post_Item(MENU *menu, ITEM *item)
|
|   Description   :  Draw the item in the menus window at the current
|                    window position
|
|   Return Values :  -
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(void)
_nc_Post_Item(const MENU * menu, const ITEM * item)
{
  int i;
  chtype ch;
  int item_x, item_y;
  int count = 0;
  bool isfore = FALSE, isback = FALSE, isgrey = FALSE;
  int name_len;
  int desc_len;

  assert(menu->win);

  getyx(menu->win, item_y, item_x);

  /* We need a marker iff
     - it is a onevalued menu and it is the current item
     - or it has a selection value
   */
  wattron(menu->win, (int)menu->back);
  if (item->value || (item == menu->curitem))
    {
      if (menu->marklen)
	{
	  /* In a multi selection menu we use the fore attribute
	     for a selected marker that is not the current one.
	     This improves visualization of the menu, because now
	     always the 'normal' marker denotes the current
	     item. */
	  if (!(menu->opt & O_ONEVALUE) && item->value && item != menu->curitem)
	    {
	      wattron(menu->win, (int)menu->fore);
	      isfore = TRUE;
	    }
	  waddstr(menu->win, menu->mark);
	  if (isfore)
	    {
	      wattron(menu->win, (int)menu->fore);
	      isfore = FALSE;
	    }
	}
    }
  else				/* otherwise we have to wipe out the marker area */
    for (ch = ' ', i = menu->marklen; i > 0; i--)
      waddch(menu->win, ch);
  wattroff(menu->win, (int)menu->back);
  count += menu->marklen;

  /* First we have to calculate the attribute depending on selectability
     and selection status
   */
  if (!(item->opt & O_SELECTABLE))
    {
      wattron(menu->win, (int)menu->grey);
      isgrey = TRUE;
    }
  else
    {
      if (item->value || item == menu->curitem)
	{
	  wattron(menu->win, (int)menu->fore);
	  isfore = TRUE;
	}
      else
	{
	  wattron(menu->win, (int)menu->back);
	  isback = TRUE;
	}
    }

  waddnstr(menu->win, item->name.str, item->name.length);
  name_len = _nc_Calculate_Text_Width(&(item->name));
  for (ch = ' ', i = menu->namelen - name_len; i > 0; i--)
    {
      waddch(menu->win, ch);
    }
  count += menu->namelen;

  /* Show description if required and available */
  if ((menu->opt & O_SHOWDESC) && menu->desclen > 0)
    {
      int m = menu->spc_desc / 2;
      int cy = -1, cx = -1;

      for (ch = ' ', i = 0; i < menu->spc_desc; i++)
	{
	  if (i == m)
	    {
	      waddch(menu->win, menu->pad);
	      getyx(menu->win, cy, cx);
	    }
	  else
	    waddch(menu->win, ch);
	}
      if (item->description.length)
	waddnstr(menu->win, item->description.str, item->description.length);
      desc_len = _nc_Calculate_Text_Width(&(item->description));
      for (ch = ' ', i = menu->desclen - desc_len; i > 0; i--)
	{
	  waddch(menu->win, ch);
	}
      count += menu->desclen + menu->spc_desc;

      if (menu->spc_rows > 1)
	{
	  int j, k, ncy, ncx;

	  assert(cx >= 0 && cy >= 0);
	  getyx(menu->win, ncy, ncx);
	  if (isgrey)
	    wattroff(menu->win, (int)menu->grey);
	  else if (isfore)
	    wattroff(menu->win, (int)menu->fore);
	  wattron(menu->win, (int)menu->back);
	  for (j = 1; j < menu->spc_rows; j++)
	    {
	      if ((item_y + j) < getmaxy(menu->win))
		{
		  wmove(menu->win, item_y + j, item_x);
		  for (k = 0; k < count; k++)
		    waddch(menu->win, ' ');
		}
	      if ((cy + j) < getmaxy(menu->win))
		(void)mvwaddch(menu->win, cy + j, cx - 1, menu->pad);
	    }
	  wmove(menu->win, ncy, ncx);
	  if (!isback)
	    wattroff(menu->win, (int)menu->back);
	}
    }

  /* Remove attributes */
  if (isfore)
    wattroff(menu->win, (int)menu->fore);
  if (isback)
    wattroff(menu->win, (int)menu->back);
  if (isgrey)
    wattroff(menu->win, (int)menu->grey);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu
|   Function      :  void _nc_Draw_Menu(const MENU *)
|
|   Description   :  Display the menu in its windows
|
|   Return Values :  -
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(void)
_nc_Draw_Menu(const MENU * menu)
{
  ITEM *item = menu->items[0];
  ITEM *lasthor, *lastvert;
  ITEM *hitem;
  int y = 0;
  chtype s_bkgd;

  assert(item && menu->win);

  s_bkgd = getbkgd(menu->win);
  wbkgdset(menu->win, menu->back);
  werase(menu->win);
  wbkgdset(menu->win, s_bkgd);

  lastvert = (menu->opt & O_NONCYCLIC) ? (ITEM *) 0 : item;

  do
    {
      wmove(menu->win, y, 0);

      hitem = item;
      lasthor = (menu->opt & O_NONCYCLIC) ? (ITEM *) 0 : hitem;

      do
	{
	  _nc_Post_Item(menu, hitem);

	  wattron(menu->win, (int)menu->back);
	  if (((hitem = hitem->right) != lasthor) && hitem)
	    {
	      int i, j, cy, cx;
	      chtype ch = ' ';

	      getyx(menu->win, cy, cx);
	      for (j = 0; j < menu->spc_rows; j++)
		{
		  wmove(menu->win, cy + j, cx);
		  for (i = 0; i < menu->spc_cols; i++)
		    {
		      waddch(menu->win, ch);
		    }
		}
	      wmove(menu->win, cy, cx + menu->spc_cols);
	    }
	}
      while (hitem && (hitem != lasthor));
      wattroff(menu->win, (int)menu->back);

      item = item->down;
      y += menu->spc_rows;

    }
  while (item && (item != lastvert));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu
|   Function      :  int post_menu(MENU* menu)
|
|   Description   :  Post a menu to the screen. This makes it visible.
|
|   Return Values :  E_OK                - success
|                    E_BAD_ARGUMENT      - not a valid menu pointer
|                    E_SYSTEM_ERROR      - error in lower layers
|                    E_NOT_CONNECTED     - No items connected to menu
|                    E_BAD_STATE         - Menu in userexit routine
|                    E_POSTED            - Menu already posted
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
post_menu(MENU * menu)
{
  T((T_CALLED("post_menu(%p)"), (void *)menu));

  if (!menu)
    RETURN(E_BAD_ARGUMENT);

  if (menu->status & _IN_DRIVER)
    RETURN(E_BAD_STATE);

  if (menu->status & _POSTED)
    RETURN(E_POSTED);

  if (menu->items && *(menu->items))
    {
      int y;
      int h = 1 + menu->spc_rows * (menu->rows - 1);

      WINDOW *win = Get_Menu_Window(menu);
      int maxy = getmaxy(win);

      if ((menu->win = newpad(h, menu->width)))
	{
	  y = (maxy >= h) ? h : maxy;
	  if (y >= menu->height)
	    y = menu->height;
	  if (!(menu->sub = subpad(menu->win, y, menu->width, 0, 0)))
	    RETURN(E_SYSTEM_ERROR);
	}
      else
	RETURN(E_SYSTEM_ERROR);

      if (menu->status & _LINK_NEEDED)
	_nc_Link_Items(menu);
    }
  else
    RETURN(E_NOT_CONNECTED);

  SetStatus(menu, _POSTED);

  if (!(menu->opt & O_ONEVALUE))
    {
      ITEM **items;

      for (items = menu->items; *items; items++)
	{
	  (*items)->value = FALSE;
	}
    }

  _nc_Draw_Menu(menu);

  Call_Hook(menu, menuinit);
  Call_Hook(menu, iteminit);

  _nc_Show_Menu(menu);

  RETURN(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu
|   Function      :  int unpost_menu(MENU*)
|
|   Description   :  Detach menu from screen
|
|   Return Values :  E_OK              - success
|                    E_BAD_ARGUMENT    - not a valid menu pointer
|                    E_BAD_STATE       - menu in userexit routine
|                    E_NOT_POSTED      - menu is not posted
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
unpost_menu(MENU * menu)
{
  WINDOW *win;

  T((T_CALLED("unpost_menu(%p)"), (void *)menu));

  if (!menu)
    RETURN(E_BAD_ARGUMENT);

  if (menu->status & _IN_DRIVER)
    RETURN(E_BAD_STATE);

  if (!(menu->status & _POSTED))
    RETURN(E_NOT_POSTED);

  Call_Hook(menu, itemterm);
  Call_Hook(menu, menuterm);

  win = Get_Menu_Window(menu);
  werase(win);
  wsyncup(win);

  assert(menu->sub);
  delwin(menu->sub);
  menu->sub = (WINDOW *)0;

  assert(menu->win);
  delwin(menu->win);
  menu->win = (WINDOW *)0;

  ClrStatus(menu, _POSTED);

  RETURN(E_OK);
}

/* m_post.c ends here */
