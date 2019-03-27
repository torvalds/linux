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
* Module m_global                                                          *
* Globally used internal routines and the default menu and item structures *
***************************************************************************/

#include "menu.priv.h"

MODULE_ID("$Id: m_global.c,v 1.27 2012/06/10 00:09:15 tom Exp $")

static char mark[] = "-";
/* *INDENT-OFF* */
NCURSES_EXPORT_VAR(MENU) _nc_Default_Menu = {
  16,				  /* Nr. of chars high */
  1,				  /* Nr. of chars wide */
  16,				  /* Nr. of items high */
  1,			          /* Nr. of items wide */
  16,				  /* Nr. of formatted items high */
  1,				  /* Nr. of formatted items wide */
  16,				  /* Nr. of items high (actual) */
  0,				  /* length of widest name */
  0,				  /* length of widest description */
  1,				  /* length of mark */
  1,				  /* length of one item */
  1,                              /* Spacing for descriptor */ 
  1,                              /* Spacing for columns */
  1,                              /* Spacing for rows */
  (char *)0,			  /* buffer used to store match chars */
  0,				  /* Index into pattern buffer */
  (WINDOW *)0,			  /* Window containing entire menu */
  (WINDOW *)0,			  /* Portion of menu displayed */
  (WINDOW *)0,			  /* User's window */
  (WINDOW *)0,			  /* User's subwindow */
  (ITEM **)0,			  /* List of items */
  0,				  /* Total Nr. of items in menu */
  (ITEM *)0,			  /* Current item */
  0,				  /* Top row of menu */
  (chtype)A_REVERSE,		  /* Attribute for selection */
  (chtype)A_NORMAL,		  /* Attribute for nonselection */
  (chtype)A_UNDERLINE,		  /* Attribute for inactive */	
  ' ',  			  /* Pad character */
  (Menu_Hook)0,			  /* Menu init */
  (Menu_Hook)0,			  /* Menu term */
  (Menu_Hook)0,			  /* Item init */
  (Menu_Hook)0,			  /* Item term */
  (void *)0,			  /* userptr */
  mark,				  /* mark */
  ALL_MENU_OPTS,                  /* options */
  0			          /* status */	    
};

NCURSES_EXPORT_VAR(ITEM) _nc_Default_Item = {
  { (char *)0, 0 },		  /* name */
  { (char *)0, 0 },		  /* description */
  (MENU *)0,		          /* Pointer to parent menu */
  (char *)0,			  /* Userpointer */
  ALL_ITEM_OPTS,		  /* options */
  0,				  /* Item Nr. */
  0,				  /* y */
  0,				  /* x */
  FALSE,			  /* value */
  (ITEM *)0,		          /* left */
  (ITEM *)0,		          /* right */
  (ITEM *)0,		          /* up */
  (ITEM *)0		          /* down */
  };
/* *INDENT-ON* */

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  static void ComputeMaximum_NameDesc_Lenths(MENU *menu)
|   
|   Description   :  Calculates the maximum name and description lengths
|                    of the items connected to the menu
|
|   Return Values :  -
+--------------------------------------------------------------------------*/
NCURSES_INLINE static void
ComputeMaximum_NameDesc_Lengths(MENU * menu)
{
  unsigned MaximumNameLength = 0;
  unsigned MaximumDescriptionLength = 0;
  ITEM **items;
  unsigned check;

  assert(menu && menu->items);
  for (items = menu->items; *items; items++)
    {
      check = (unsigned)_nc_Calculate_Text_Width(&((*items)->name));
      if (check > MaximumNameLength)
	MaximumNameLength = check;

      check = (unsigned)_nc_Calculate_Text_Width(&((*items)->description));
      if (check > MaximumDescriptionLength)
	MaximumDescriptionLength = check;
    }

  menu->namelen = (short)MaximumNameLength;
  menu->desclen = (short)MaximumDescriptionLength;
  T(("ComputeMaximum_NameDesc_Lengths %d,%d", menu->namelen, menu->desclen));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  static void ResetConnectionInfo(MENU *, ITEM **)
|   
|   Description   :  Reset all informations in the menu and the items in
|                    the item array that indicates a connection
|
|   Return Values :  -
+--------------------------------------------------------------------------*/
NCURSES_INLINE static void
ResetConnectionInfo(MENU * menu, ITEM ** items)
{
  ITEM **item;

  assert(menu && items);
  for (item = items; *item; item++)
    {
      (*item)->index = 0;
      (*item)->imenu = (MENU *) 0;
    }
  if (menu->pattern)
    free(menu->pattern);
  menu->pattern = (char *)0;
  menu->pindex = 0;
  menu->items = (ITEM **) 0;
  menu->nitems = 0;
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  bool _nc_Connect_Items(MENU *menu, ITEM **items)
|
|   Description   :  Connect the items in the item array to the menu.
|                    Decorate all the items with a number and a backward
|                    pointer to the menu.
|
|   Return Values :  TRUE       - successful connection
|                    FALSE      - connection failed
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(bool)
_nc_Connect_Items(MENU * menu, ITEM ** items)
{
  ITEM **item;
  unsigned int ItemCount = 0;

  if (menu && items)
    {
      for (item = items; *item; item++)
	{
	  if ((*item)->imenu)
	    {
	      /* if a item is already connected, reject connection */
	      break;
	    }
	}
      if (!(*item))
	/* we reached the end, so there was no connected item */
	{
	  for (item = items; *item; item++)
	    {
	      if (menu->opt & O_ONEVALUE)
		{
		  (*item)->value = FALSE;
		}
	      (*item)->index = (short)ItemCount++;
	      (*item)->imenu = menu;
	    }
	}
    }
  else
    return (FALSE);

  if (ItemCount != 0)
    {
      menu->items = items;
      menu->nitems = (short)ItemCount;
      ComputeMaximum_NameDesc_Lengths(menu);
      if ((menu->pattern = typeMalloc(char, (unsigned)(1 + menu->namelen))))
	{
	  Reset_Pattern(menu);
	  set_menu_format(menu, menu->frows, menu->fcols);
	  menu->curitem = *items;
	  menu->toprow = 0;
	  return (TRUE);
	}
    }

  /* If we fall through to this point, we have to reset all items connection 
     and inform about a reject connection */
  ResetConnectionInfo(menu, items);
  return (FALSE);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  void _nc_Disconnect_Items(MENU *menu)
|   
|   Description   :  Disconnect the menus item array from the menu
|
|   Return Values :  -
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(void)
_nc_Disconnect_Items(MENU * menu)
{
  if (menu && menu->items)
    ResetConnectionInfo(menu, menu->items);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  int _nc_Calculate_Text_Width(const TEXT * item)
|   
|   Description   :  Calculate the number of columns for a TEXT.
|
|   Return Values :  the width
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
_nc_Calculate_Text_Width(const TEXT * item /*FIXME: limit length */ )
{
#if USE_WIDEC_SUPPORT
  int result = item->length;

  T((T_CALLED("_nc_menu_text_width(%p)"), (const void *)item));
  if (result != 0 && item->str != 0)
    {
      int count = (int)mbstowcs(0, item->str, 0);
      wchar_t *temp = 0;

      if (count > 0
	  && (temp = typeMalloc(wchar_t, 2 + count)) != 0)
	{
	  int n;

	  result = 0;
	  mbstowcs(temp, item->str, (unsigned)count);
	  for (n = 0; n < count; ++n)
	    {
	      int test = wcwidth(temp[n]);

	      if (test <= 0)
		test = 1;
	      result += test;
	    }
	  free(temp);
	}
    }
  returnCode(result);
#else
  return item->length;
#endif
}

/*
 * Calculate the actual width of a menu entry for wide-characters.
 */
#if USE_WIDEC_SUPPORT
static int
calculate_actual_width(MENU * menu, bool name)
{
  int width = 0;
  int check = 0;
  ITEM **items;

  assert(menu && menu->items);

  if (menu->items != 0)
    {
      for (items = menu->items; *items; items++)
	{
	  if (name)
	    {
	      check = _nc_Calculate_Text_Width(&((*items)->name));
	    }
	  else
	    {
	      check = _nc_Calculate_Text_Width(&((*items)->description));
	    }
	  if (check > width)
	    width = check;
	}
    }
  else
    {
      width = (name ? menu->namelen : menu->desclen);
    }

  T(("calculate_actual_width %s = %d/%d",
     name ? "name" : "desc",
     width,
     name ? menu->namelen : menu->desclen));
  return width;
}
#else
#define calculate_actual_width(menu, name) (name ? menu->namelen : menu->desclen)
#endif

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  void _nc_Calculate_Item_Length_and_Width(MENU *menu)
|   
|   Description   :  Calculate the length of an item and the width of the
|                    whole menu.
|
|   Return Values :  -
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(void)
_nc_Calculate_Item_Length_and_Width(MENU * menu)
{
  int l;

  assert(menu);

  menu->height = (short)(1 + menu->spc_rows * (menu->arows - 1));

  l = calculate_actual_width(menu, TRUE);
  l += menu->marklen;

  if ((menu->opt & O_SHOWDESC) && (menu->desclen > 0))
    {
      l += calculate_actual_width(menu, FALSE);
      l += menu->spc_desc;
    }

  menu->itemlen = (short)l;
  l *= menu->cols;
  l += (menu->cols - 1) * menu->spc_cols;	/* for the padding between the columns */
  menu->width = (short)l;

  T(("_nc_CalculateItem_Length_and_Width columns %d, item %d, width %d",
     menu->cols,
     menu->itemlen,
     menu->width));
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  void _nc_Link_Item(MENU *menu)
|   
|   Description   :  Statically calculate for every item its four neighbors.
|                    This depends on the orientation of the menu. This
|                    static approach simplifies navigation in the menu a lot.
|
|   Return Values :  -
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(void)
_nc_Link_Items(MENU * menu)
{
  if (menu && menu->items && *(menu->items))
    {
      int i, j;
      ITEM *item;
      int Number_Of_Items = menu->nitems;
      int col = 0, row = 0;
      int Last_in_Row;
      int Last_in_Column;
      bool cycle = (menu->opt & O_NONCYCLIC) ? FALSE : TRUE;

      ClrStatus(menu, _LINK_NEEDED);

      if (menu->opt & O_ROWMAJOR)
	{
	  int Number_Of_Columns = menu->cols;

	  for (i = 0; i < Number_Of_Items; i++)
	    {
	      item = menu->items[i];

	      Last_in_Row = row * Number_Of_Columns + (Number_Of_Columns - 1);

	      item->left = (col) ?
	      /* if we are not in the leftmost column, we can use the
	         predecessor in the items array */
		menu->items[i - 1] :
		(cycle ? menu->items[(Last_in_Row >= Number_Of_Items) ?
				     Number_Of_Items - 1 :
				     Last_in_Row] :
		 (ITEM *) 0);

	      item->right = ((col < (Number_Of_Columns - 1)) &&
			     ((i + 1) < Number_Of_Items)
		)?
		menu->items[i + 1] :
		(cycle ? menu->items[row * Number_Of_Columns] :
		 (ITEM *) 0
		);

	      Last_in_Column = (menu->rows - 1) * Number_Of_Columns + col;

	      item->up = (row) ? menu->items[i - Number_Of_Columns] :
		(cycle ? menu->items[(Last_in_Column >= Number_Of_Items) ?
				     Number_Of_Items - 1 :
				     Last_in_Column] :
		 (ITEM *) 0);

	      item->down = ((i + Number_Of_Columns) < Number_Of_Items)
		?
		menu->items[i + Number_Of_Columns] :
		(cycle ? menu->items[(row + 1) < menu->rows ?
				     Number_Of_Items - 1 : col] :
		 (ITEM *) 0);
	      item->x = (short)col;
	      item->y = (short)row;
	      if (++col == Number_Of_Columns)
		{
		  row++;
		  col = 0;
		}
	    }
	}
      else
	{
	  int Number_Of_Rows = menu->rows;

	  for (j = 0; j < Number_Of_Items; j++)
	    {
	      item = menu->items[i = (col * Number_Of_Rows + row)];

	      Last_in_Column = (menu->cols - 1) * Number_Of_Rows + row;

	      item->left = (col) ?
		menu->items[i - Number_Of_Rows] :
		(cycle ? (Last_in_Column >= Number_Of_Items) ?
		 menu->items[Last_in_Column - Number_Of_Rows] :
		 menu->items[Last_in_Column] :
		 (ITEM *) 0);

	      item->right = ((i + Number_Of_Rows) < Number_Of_Items)
		?
		menu->items[i + Number_Of_Rows] :
		(cycle ? menu->items[row] : (ITEM *) 0);

	      Last_in_Row = col * Number_Of_Rows + (Number_Of_Rows - 1);

	      item->up = (row) ?
		menu->items[i - 1] :
		(cycle ?
		 menu->items[(Last_in_Row >= Number_Of_Items) ?
			     Number_Of_Items - 1 :
			     Last_in_Row] :
		 (ITEM *) 0);

	      item->down = (row < (Number_Of_Rows - 1))
		?
		(menu->items[((i + 1) < Number_Of_Items) ?
			     i + 1 :
			     (col - 1) * Number_Of_Rows + row + 1]) :
		(cycle ?
		 menu->items[col * Number_Of_Rows] :
		 (ITEM *) 0
		);

	      item->x = (short)col;
	      item->y = (short)row;
	      if ((++row) == Number_Of_Rows)
		{
		  col++;
		  row = 0;
		}
	    }
	}
    }
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  void _nc_Show_Menu(const MENU* menu)
|   
|   Description   :  Update the window that is associated with the menu
|
|   Return Values :  -
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(void)
_nc_Show_Menu(const MENU * menu)
{
  WINDOW *win;
  int maxy, maxx;

  assert(menu);
  if ((menu->status & _POSTED) && !(menu->status & _IN_DRIVER))
    {
      /* adjust the internal subwindow to start on the current top */
      assert(menu->sub);
      mvderwin(menu->sub, menu->spc_rows * menu->toprow, 0);
      win = Get_Menu_Window(menu);

      maxy = getmaxy(win);
      maxx = getmaxx(win);

      if (menu->height < maxy)
	maxy = menu->height;
      if (menu->width < maxx)
	maxx = menu->width;

      copywin(menu->sub, win, 0, 0, 0, 0, maxy - 1, maxx - 1, 0);
      pos_menu_cursor(menu);
    }
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  void _nc_New_TopRow_and_CurrentItem(
|                            MENU *menu, 
|                            int new_toprow, 
|                            ITEM *new_current_item)
|   
|   Description   :  Redisplay the menu so that the given row becomes the
|                    top row and the given item becomes the new current
|                    item.
|
|   Return Values :  -
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(void)
_nc_New_TopRow_and_CurrentItem(
				MENU * menu,
				int new_toprow,
				ITEM * new_current_item)
{
  ITEM *cur_item;
  bool mterm_called = FALSE;
  bool iterm_called = FALSE;

  assert(menu);
  if (menu->status & _POSTED)
    {
      if (new_current_item != menu->curitem)
	{
	  Call_Hook(menu, itemterm);
	  iterm_called = TRUE;
	}
      if (new_toprow != menu->toprow)
	{
	  Call_Hook(menu, menuterm);
	  mterm_called = TRUE;
	}

      cur_item = menu->curitem;
      assert(cur_item);
      menu->toprow = (short)new_toprow;
      menu->curitem = new_current_item;

      if (mterm_called)
	{
	  Call_Hook(menu, menuinit);
	}
      if (iterm_called)
	{
	  /* this means, move from the old current_item to the new one... */
	  Move_To_Current_Item(menu, cur_item);
	  Call_Hook(menu, iteminit);
	}
      if (mterm_called || iterm_called)
	{
	  _nc_Show_Menu(menu);
	}
      else
	pos_menu_cursor(menu);
    }
  else
    {				/* if we are not posted, this is quite simple */
      menu->toprow = (short)new_toprow;
      menu->curitem = new_current_item;
    }
}

/* m_global.c ends here */
