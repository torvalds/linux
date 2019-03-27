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
* Module m_pad                                                             *
* Control menus padding character                                          *
***************************************************************************/

#include "menu.priv.h"

MODULE_ID("$Id: m_pad.c,v 1.13 2012/03/10 23:43:41 tom Exp $")

/* Macro to redraw menu if it is posted and changed */
#define Refresh_Menu(menu) \
   if ( (menu) && ((menu)->status & _POSTED) )\
   {\
      _nc_Draw_Menu( menu );\
      _nc_Show_Menu( menu ); \
   }

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  int set_menu_pad(MENU* menu, int pad)
|   
|   Description   :  Set the character to be used to separate the item name
|                    from its description. This must be a printable 
|                    character.
|
|   Return Values :  E_OK              - success
|                    E_BAD_ARGUMENT    - an invalid value has been passed
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
set_menu_pad(MENU * menu, int pad)
{
  bool do_refresh = (menu != (MENU *) 0);

  T((T_CALLED("set_menu_pad(%p,%d)"), (void *)menu, pad));

  if (!isprint(UChar(pad)))
    RETURN(E_BAD_ARGUMENT);

  Normalize_Menu(menu);
  menu->pad = (unsigned char)pad;

  if (do_refresh)
    Refresh_Menu(menu);

  RETURN(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  int menu_pad(const MENU *menu)
|   
|   Description   :  Return the value of the padding character
|
|   Return Values :  The pad character
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
menu_pad(const MENU * menu)
{
  T((T_CALLED("menu_pad(%p)"), (const void *)menu));
  returnCode(Normalize_Menu(menu)->pad);
}

/* m_pad.c ends here */
