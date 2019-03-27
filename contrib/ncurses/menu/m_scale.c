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
* Module m_scale                                                           *
* Menu scaling routine                                                     *
***************************************************************************/

#include "menu.priv.h"

MODULE_ID("$Id: m_scale.c,v 1.10 2010/01/23 21:20:10 tom Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu  
|   Function      :  int scale_menu(const MENU *menu)
|   
|   Description   :  Returns the minimum window size necessary for the
|                    subwindow of menu.  
|
|   Return Values :  E_OK                  - success
|                    E_BAD_ARGUMENT        - invalid menu pointer
|                    E_NOT_CONNECTED       - no items are connected to menu
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
scale_menu(const MENU * menu, int *rows, int *cols)
{
  T((T_CALLED("scale_menu(%p,%p,%p)"),
     (const void *)menu,
     (void *)rows,
     (void *)cols));

  if (!menu)
    RETURN(E_BAD_ARGUMENT);

  if (menu->items && *(menu->items))
    {
      if (rows)
	*rows = menu->height;
      if (cols)
	*cols = menu->width;
      RETURN(E_OK);
    }
  else
    RETURN(E_NOT_CONNECTED);
}

/* m_scale.c ends here */
