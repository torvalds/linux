/****************************************************************************
 * Copyright (c) 1998-2006,2010 Free Software Foundation, Inc.              *
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
* Module m_pattern                                                         *
* Pattern matching handling                                                *
***************************************************************************/

#include "menu.priv.h"

MODULE_ID("$Id: m_pattern.c,v 1.16 2010/01/23 21:20:10 tom Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  char *menu_pattern(const MENU *menu)
|   
|   Description   :  Return the value of the pattern buffer.
|
|   Return Values :  NULL          - if there is no pattern buffer allocated
|                    EmptyString   - if there is a pattern buffer but no
|                                    pattern is stored
|                    PatternString - as expected
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(char *)
menu_pattern(const MENU * menu)
{
  static char empty[] = "";

  T((T_CALLED("menu_pattern(%p)"), (const void *)menu));
  returnPtr(menu ? (menu->pattern ? menu->pattern : empty) : 0);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  int set_menu_pattern(MENU *menu, const char *p)
|   
|   Description   :  Set the match pattern for a menu and position to the
|                    first item that matches.
|
|   Return Values :  E_OK              - success
|                    E_BAD_ARGUMENT    - invalid menu or pattern pointer
|                    E_BAD_STATE       - menu in user hook routine
|                    E_NOT_CONNECTED   - no items connected to menu
|                    E_NO_MATCH        - no item matches pattern
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
set_menu_pattern(MENU * menu, const char *p)
{
  ITEM *matchitem;
  int matchpos;

  T((T_CALLED("set_menu_pattern(%p,%s)"), (void *)menu, _nc_visbuf(p)));

  if (!menu || !p)
    RETURN(E_BAD_ARGUMENT);

  if (!(menu->items))
    RETURN(E_NOT_CONNECTED);

  if (menu->status & _IN_DRIVER)
    RETURN(E_BAD_STATE);

  Reset_Pattern(menu);

  if (!(*p))
    {
      pos_menu_cursor(menu);
      RETURN(E_OK);
    }

  if (menu->status & _LINK_NEEDED)
    _nc_Link_Items(menu);

  matchpos = menu->toprow;
  matchitem = menu->curitem;
  assert(matchitem);

  while (*p)
    {
      if (!isprint(UChar(*p)) ||
	  (_nc_Match_Next_Character_In_Item_Name(menu, *p, &matchitem) != E_OK))
	{
	  Reset_Pattern(menu);
	  pos_menu_cursor(menu);
	  RETURN(E_NO_MATCH);
	}
      p++;
    }

  /* This is reached if there was a match. So we position to the new item */
  Adjust_Current_Item(menu, matchpos, matchitem);
  RETURN(E_OK);
}

/* m_pattern.c ends here */
