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
* Module m_spacing                                                         *
* Routines to handle spacing between entries                               *
***************************************************************************/

#include "menu.priv.h"

MODULE_ID("$Id: m_spacing.c,v 1.19 2012/03/10 23:43:41 tom Exp $")

#define MAX_SPC_DESC ((TABSIZE) ? (TABSIZE) : 8)
#define MAX_SPC_COLS ((TABSIZE) ? (TABSIZE) : 8)
#define MAX_SPC_ROWS (3)

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu
|   Function      :  int set_menu_spacing(MENU *menu,int desc, int r, int c);
|
|   Description   :  Set the spacing between entries
|
|   Return Values :  E_OK                 - on success
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
set_menu_spacing(MENU * menu, int s_desc, int s_row, int s_col)
{
  MENU *m;			/* split for ATAC workaround */

  T((T_CALLED("set_menu_spacing(%p,%d,%d,%d)"),
     (void *)menu, s_desc, s_row, s_col));

  m = Normalize_Menu(menu);

  assert(m);
  if (m->status & _POSTED)
    RETURN(E_POSTED);

  if (((s_desc < 0) || (s_desc > MAX_SPC_DESC)) ||
      ((s_row < 0) || (s_row > MAX_SPC_ROWS)) ||
      ((s_col < 0) || (s_col > MAX_SPC_COLS)))
    RETURN(E_BAD_ARGUMENT);

  m->spc_desc = (short)(s_desc ? s_desc : 1);
  m->spc_rows = (short)(s_row ? s_row : 1);
  m->spc_cols = (short)(s_col ? s_col : 1);
  _nc_Calculate_Item_Length_and_Width(m);

  RETURN(E_OK);
}

/*---------------------------------------------------------------------------
|   Facility      :  libnmenu
|   Function      :  int menu_spacing (const MENU *,int *,int *,int *);
|
|   Description   :  Retrieve info about spacing between the entries
|
|   Return Values :  E_OK             - on success
+--------------------------------------------------------------------------*/
NCURSES_EXPORT(int)
menu_spacing(const MENU * menu, int *s_desc, int *s_row, int *s_col)
{
  const MENU *m;		/* split for ATAC workaround */

  T((T_CALLED("menu_spacing(%p,%p,%p,%p)"),
     (const void *)menu,
     (void *)s_desc,
     (void *)s_row,
     (void *)s_col));

  m = Normalize_Menu(menu);

  assert(m);
  if (s_desc)
    *s_desc = m->spc_desc;
  if (s_row)
    *s_row = m->spc_rows;
  if (s_col)
    *s_col = m->spc_cols;

  RETURN(E_OK);
}

/* m_spacing.c ends here */
