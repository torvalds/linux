/****************************************************************************
 * Copyright (c) 2004-2005,2010 Free Software Foundation, Inc.              *
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
 *   Author:  Thomas E. Dickey                                              *
 ****************************************************************************/

#include "menu.priv.h"

MODULE_ID("$Id: m_trace.c,v 1.4 2010/01/23 21:20:10 tom Exp $")

NCURSES_EXPORT(ITEM *)
_nc_retrace_item(ITEM * code)
{
  T((T_RETURN("%p"), (void *)code));
  return code;
}

NCURSES_EXPORT(ITEM **)
_nc_retrace_item_ptr(ITEM ** code)
{
  T((T_RETURN("%p"), (void *)code));
  return code;
}

NCURSES_EXPORT(Item_Options)
_nc_retrace_item_opts(Item_Options code)
{
  T((T_RETURN("%d"), code));
  return code;
}

NCURSES_EXPORT(MENU *)
_nc_retrace_menu(MENU * code)
{
  T((T_RETURN("%p"), (void *)code));
  return code;
}

NCURSES_EXPORT(Menu_Hook)
_nc_retrace_menu_hook(Menu_Hook code)
{
  T((T_RETURN("%p"), code));
  return code;
}

NCURSES_EXPORT(Menu_Options)
_nc_retrace_menu_opts(Menu_Options code)
{
  T((T_RETURN("%d"), code));
  return code;
}
