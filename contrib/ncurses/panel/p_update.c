/****************************************************************************
 * Copyright (c) 1998-2009,2010 Free Software Foundation, Inc.              *
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
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1995                    *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 *     and: Juergen Pfeifer                         1997-1999,2008          *
 ****************************************************************************/

/* p_update.c
 * wnoutrefresh windows in an orderly fashion
 */
#include "panel.priv.h"

MODULE_ID("$Id: p_update.c,v 1.11 2010/01/23 21:22:16 tom Exp $")

NCURSES_EXPORT(void)
NCURSES_SP_NAME(update_panels) (NCURSES_SP_DCL0)
{
  PANEL *pan;

  T((T_CALLED("update_panels(%p)"), (void *)SP_PARM));
  dBug(("--> update_panels"));

  if (SP_PARM)
    {
      GetScreenHook(SP_PARM);

      pan = _nc_bottom_panel;
      while (pan && pan->above)
	{
	  PANEL_UPDATE(pan, pan->above);
	  pan = pan->above;
	}

      pan = _nc_bottom_panel;
      while (pan)
	{
	  Wnoutrefresh(pan);
	  pan = pan->above;
	}
    }

  returnVoid;
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(void)
update_panels(void)
{
  NCURSES_SP_NAME(update_panels) (CURRENT_SCREEN);
}
#endif
