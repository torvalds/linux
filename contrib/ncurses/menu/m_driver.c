/****************************************************************************
 * Copyright (c) 1998-2011,2012 Free Software Foundation, Inc.              *
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
* Module m_driver                                                          *
* Central dispatching routine                                              *
***************************************************************************/

#include "menu.priv.h"

MODULE_ID("$Id: m_driver.c,v 1.31 2012/03/10 23:43:41 tom Exp $")

/* Macros */

/* Remove the last character from the match pattern buffer */
#define Remove_Character_From_Pattern(menu) \
  (menu)->pattern[--((menu)->pindex)] = '\0'

/* Add a new character to the match pattern buffer */
#define Add_Character_To_Pattern(menu,ch) \
  { (menu)->pattern[((menu)->pindex)++] = (char) (ch);\
    (menu)->pattern[(menu)->pindex] = '\0'; }

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu
|   Function      :  static bool Is_Sub_String(
|                           bool IgnoreCaseFlag,
|                           const char *part,
|                           const char *string)
|
|   Description   :  Checks whether or not part is a substring of string.
|
|   Return Values :  TRUE   - if it is a substring
|                    FALSE  - if it is not a substring
+--------------------------------------------------------------------------*/
static bool
Is_Sub_String(
	       bool IgnoreCaseFlag,
	       const char *part,
	       const char *string
)
{
  assert(part && string);
  if (IgnoreCaseFlag)
    {
      while (*string && *part)
	{
	  if (toupper(UChar(*string++)) != toupper(UChar(*part)))
	    break;
	  part++;
	}
    }
  else
    {
      while (*string && *part)
	if (*part != *string++)
	  break;
      part++;
    }
  return ((*part) ? FALSE : TRUE);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu
|   Function      :  int _nc_Match_Next_Character_In_Item_Name(
|                           MENU *menu,
|                           int  ch,
|                           ITEM **item)
|
|   Description   :  This internal routine is called for a menu positioned
|                    at an item with three different classes of characters:
|                       - a printable character; the character is added to
|                         the current pattern and the next item matching
|                         this pattern is searched.
|                       - NUL; the pattern stays as it is and the next item
|                         matching the pattern is searched
|                       - BS; the pattern stays as it is and the previous
|                         item matching the pattern is searched
|
|                       The item parameter contains on call a pointer to
|                       the item where the search starts. On return - if
|                       a match was found - it contains a pointer to the
|                       matching item.
|
|   Return Values :  E_OK        - an item matching the pattern was found
|                    E_NO_MATCH  - nothing found
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
_nc_Match_Next_Character_In_Item_Name
(MENU * menu, int ch, ITEM ** item)
{
  bool found = FALSE, passed = FALSE;
  int idx, last;

  T((T_CALLED("_nc_Match_Next_Character(%p,%d,%p)"),
     (void *)menu, ch, (void *)item));

  assert(menu && item && *item);
  idx = (*item)->index;

  if (ch && ch != BS)
    {
      /* if we become to long, we need no further checking : there can't be
         a match ! */
      if ((menu->pindex + 1) > menu->namelen)
	RETURN(E_NO_MATCH);

      Add_Character_To_Pattern(menu, ch);
      /* we artificially position one item back, because in the do...while
         loop we start with the next item. This means, that with a new
         pattern search we always start the scan with the actual item. If
         we do a NEXT_PATTERN oder PREV_PATTERN search, we start with the
         one after or before the actual item. */
      if (--idx < 0)
	idx = menu->nitems - 1;
    }

  last = idx;			/* this closes the cycle */

  do
    {
      if (ch == BS)
	{			/* we have to go backward */
	  if (--idx < 0)
	    idx = menu->nitems - 1;
	}
      else
	{			/* otherwise we always go forward */
	  if (++idx >= menu->nitems)
	    idx = 0;
	}
      if (Is_Sub_String((bool)((menu->opt & O_IGNORECASE) != 0),
			menu->pattern,
			menu->items[idx]->name.str)
	)
	found = TRUE;
      else
	passed = TRUE;
    }
  while (!found && (idx != last));

  if (found)
    {
      if (!((idx == (*item)->index) && passed))
	{
	  *item = menu->items[idx];
	  RETURN(E_OK);
	}
      /* This point is reached, if we fully cycled through the item list
         and the only match we found is the starting item. With a NEXT_PATTERN
         or PREV_PATTERN scan this means, that there was no additional match.
         If we searched with an expanded new pattern, we should never reach
         this point, because if the expanded pattern matches also the actual
         item we will find it in the first attempt (passed==FALSE) and we
         will never cycle through the whole item array.
       */
      assert(ch == 0 || ch == BS);
    }
  else
    {
      if (ch && ch != BS && menu->pindex > 0)
	{
	  /* if we had no match with a new pattern, we have to restore it */
	  Remove_Character_From_Pattern(menu);
	}
    }
  RETURN(E_NO_MATCH);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu
|   Function      :  int menu_driver(MENU* menu, int c)
|
|   Description   :  Central dispatcher for the menu. Translates the logical
|                    request 'c' into a menu action.
|
|   Return Values :  E_OK            - success
|                    E_BAD_ARGUMENT  - invalid menu pointer
|                    E_BAD_STATE     - menu is in user hook routine
|                    E_NOT_POSTED    - menu is not posted
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
menu_driver(MENU * menu, int c)
{
#define NAVIGATE(dir) \
  if (!item->dir)\
     result = E_REQUEST_DENIED;\
  else\
     item = item->dir

  int result = E_OK;
  ITEM *item;
  int my_top_row, rdiff;

  T((T_CALLED("menu_driver(%p,%d)"), (void *)menu, c));

  if (!menu)
    RETURN(E_BAD_ARGUMENT);

  if (menu->status & _IN_DRIVER)
    RETURN(E_BAD_STATE);
  if (!(menu->status & _POSTED))
    RETURN(E_NOT_POSTED);

  item = menu->curitem;

  my_top_row = menu->toprow;
  assert(item);

  if ((c > KEY_MAX) && (c <= MAX_MENU_COMMAND))
    {
      if (!((c == REQ_BACK_PATTERN)
	    || (c == REQ_NEXT_MATCH) || (c == REQ_PREV_MATCH)))
	{
	  assert(menu->pattern);
	  Reset_Pattern(menu);
	}

      switch (c)
	{
	case REQ_LEFT_ITEM:
	    /*=================*/
	  NAVIGATE(left);
	  break;

	case REQ_RIGHT_ITEM:
	    /*==================*/
	  NAVIGATE(right);
	  break;

	case REQ_UP_ITEM:
	    /*===============*/
	  NAVIGATE(up);
	  break;

	case REQ_DOWN_ITEM:
	    /*=================*/
	  NAVIGATE(down);
	  break;

	case REQ_SCR_ULINE:
	    /*=================*/
	  if (my_top_row == 0 || !(item->up))
	    result = E_REQUEST_DENIED;
	  else
	    {
	      --my_top_row;
	      item = item->up;
	    }
	  break;

	case REQ_SCR_DLINE:
	    /*=================*/
	  if ((my_top_row + menu->arows >= menu->rows) || !(item->down))
	    {
	      /* only if the menu has less items than rows, we can deny the
	         request. Otherwise the epilogue of this routine adjusts the
	         top row if necessary */
	      result = E_REQUEST_DENIED;
	    }
	  else
	    {
	      my_top_row++;
	      item = item->down;
	    }
	  break;

	case REQ_SCR_DPAGE:
	    /*=================*/
	  rdiff = menu->rows - (menu->arows + my_top_row);
	  if (rdiff > menu->arows)
	    rdiff = menu->arows;
	  if (rdiff <= 0)
	    result = E_REQUEST_DENIED;
	  else
	    {
	      my_top_row += rdiff;
	      while (rdiff-- > 0 && item != 0 && item->down != 0)
		item = item->down;
	    }
	  break;

	case REQ_SCR_UPAGE:
	    /*=================*/
	  rdiff = (menu->arows < my_top_row) ? menu->arows : my_top_row;
	  if (rdiff <= 0)
	    result = E_REQUEST_DENIED;
	  else
	    {
	      my_top_row -= rdiff;
	      while (rdiff-- > 0 && item != 0 && item->up != 0)
		item = item->up;
	    }
	  break;

	case REQ_FIRST_ITEM:
	    /*==================*/
	  item = menu->items[0];
	  break;

	case REQ_LAST_ITEM:
	    /*=================*/
	  item = menu->items[menu->nitems - 1];
	  break;

	case REQ_NEXT_ITEM:
	    /*=================*/
	  if ((item->index + 1) >= menu->nitems)
	    {
	      if (menu->opt & O_NONCYCLIC)
		result = E_REQUEST_DENIED;
	      else
		item = menu->items[0];
	    }
	  else
	    item = menu->items[item->index + 1];
	  break;

	case REQ_PREV_ITEM:
	    /*=================*/
	  if (item->index <= 0)
	    {
	      if (menu->opt & O_NONCYCLIC)
		result = E_REQUEST_DENIED;
	      else
		item = menu->items[menu->nitems - 1];
	    }
	  else
	    item = menu->items[item->index - 1];
	  break;

	case REQ_TOGGLE_ITEM:
	    /*===================*/
	  if (menu->opt & O_ONEVALUE)
	    {
	      result = E_REQUEST_DENIED;
	    }
	  else
	    {
	      if (menu->curitem->opt & O_SELECTABLE)
		{
		  menu->curitem->value = !menu->curitem->value;
		  Move_And_Post_Item(menu, menu->curitem);
		  _nc_Show_Menu(menu);
		}
	      else
		result = E_NOT_SELECTABLE;
	    }
	  break;

	case REQ_CLEAR_PATTERN:
	    /*=====================*/
	  /* already cleared in prologue */
	  break;

	case REQ_BACK_PATTERN:
	    /*====================*/
	  if (menu->pindex > 0)
	    {
	      assert(menu->pattern);
	      Remove_Character_From_Pattern(menu);
	      pos_menu_cursor(menu);
	    }
	  else
	    result = E_REQUEST_DENIED;
	  break;

	case REQ_NEXT_MATCH:
	    /*==================*/
	  assert(menu->pattern);
	  if (menu->pattern[0])
	    result = _nc_Match_Next_Character_In_Item_Name(menu, 0, &item);
	  else
	    {
	      if ((item->index + 1) < menu->nitems)
		item = menu->items[item->index + 1];
	      else
		{
		  if (menu->opt & O_NONCYCLIC)
		    result = E_REQUEST_DENIED;
		  else
		    item = menu->items[0];
		}
	    }
	  break;

	case REQ_PREV_MATCH:
	    /*==================*/
	  assert(menu->pattern);
	  if (menu->pattern[0])
	    result = _nc_Match_Next_Character_In_Item_Name(menu, BS, &item);
	  else
	    {
	      if (item->index)
		item = menu->items[item->index - 1];
	      else
		{
		  if (menu->opt & O_NONCYCLIC)
		    result = E_REQUEST_DENIED;
		  else
		    item = menu->items[menu->nitems - 1];
		}
	    }
	  break;

	default:
	    /*======*/
	  result = E_UNKNOWN_COMMAND;
	  break;
	}
    }
  else
    {				/* not a command */
      if (!(c & ~((int)MAX_REGULAR_CHARACTER)) && isprint(UChar(c)))
	result = _nc_Match_Next_Character_In_Item_Name(menu, c, &item);
#ifdef NCURSES_MOUSE_VERSION
      else if (KEY_MOUSE == c)
	{
	  MEVENT event;
	  WINDOW *uwin = Get_Menu_UserWin(menu);

	  getmouse(&event);
	  if ((event.bstate & (BUTTON1_CLICKED |
			       BUTTON1_DOUBLE_CLICKED |
			       BUTTON1_TRIPLE_CLICKED))
	      && wenclose(uwin, event.y, event.x))
	    {			/* we react only if the click was in the userwin, that means
				 * inside the menu display area or at the decoration window.
				 */
	      WINDOW *sub = Get_Menu_Window(menu);
	      int ry = event.y, rx = event.x;	/* screen coordinates */

	      result = E_REQUEST_DENIED;
	      if (mouse_trafo(&ry, &rx, FALSE))
		{		/* rx, ry are now "curses" coordinates */
		  if (ry < sub->_begy)
		    {		/* we clicked above the display region; this is
				 * interpreted as "scroll up" request
				 */
		      if (event.bstate & BUTTON1_CLICKED)
			result = menu_driver(menu, REQ_SCR_ULINE);
		      else if (event.bstate & BUTTON1_DOUBLE_CLICKED)
			result = menu_driver(menu, REQ_SCR_UPAGE);
		      else if (event.bstate & BUTTON1_TRIPLE_CLICKED)
			result = menu_driver(menu, REQ_FIRST_ITEM);
		      RETURN(result);
		    }
		  else if (ry > sub->_begy + sub->_maxy)
		    {		/* we clicked below the display region; this is
				 * interpreted as "scroll down" request
				 */
		      if (event.bstate & BUTTON1_CLICKED)
			result = menu_driver(menu, REQ_SCR_DLINE);
		      else if (event.bstate & BUTTON1_DOUBLE_CLICKED)
			result = menu_driver(menu, REQ_SCR_DPAGE);
		      else if (event.bstate & BUTTON1_TRIPLE_CLICKED)
			result = menu_driver(menu, REQ_LAST_ITEM);
		      RETURN(result);
		    }
		  else if (wenclose(sub, event.y, event.x))
		    {		/* Inside the area we try to find the hit item */
		      int i, x, y, err;

		      ry = event.y;
		      rx = event.x;
		      if (wmouse_trafo(sub, &ry, &rx, FALSE))
			{
			  for (i = 0; i < menu->nitems; i++)
			    {
			      err = _nc_menu_cursor_pos(menu, menu->items[i],
							&y, &x);
			      if (E_OK == err)
				{
				  if ((ry == y) &&
				      (rx >= x) &&
				      (rx < x + menu->itemlen))
				    {
				      item = menu->items[i];
				      result = E_OK;
				      break;
				    }
				}
			    }
			  if (E_OK == result)
			    {	/* We found an item, now we can handle the click.
				 * A single click just positions the menu cursor
				 * to the clicked item. A double click toggles
				 * the item.
				 */
			      if (event.bstate & BUTTON1_DOUBLE_CLICKED)
				{
				  _nc_New_TopRow_and_CurrentItem(menu,
								 my_top_row,
								 item);
				  menu_driver(menu, REQ_TOGGLE_ITEM);
				  result = E_UNKNOWN_COMMAND;
				}
			    }
			}
		    }
		}
	    }
	  else
	    result = E_REQUEST_DENIED;
	}
#endif /* NCURSES_MOUSE_VERSION */
      else
	result = E_UNKNOWN_COMMAND;
    }

  if (item == 0)
    {
      result = E_BAD_STATE;
    }
  else if (E_OK == result)
    {
      /* Adjust the top row if it turns out that the current item unfortunately
         doesn't appear in the menu window */
      if (item->y < my_top_row)
	my_top_row = item->y;
      else if (item->y >= (my_top_row + menu->arows))
	my_top_row = item->y - menu->arows + 1;

      _nc_New_TopRow_and_CurrentItem(menu, my_top_row, item);

    }

  RETURN(result);
}

/* m_driver.c ends here */
