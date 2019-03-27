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
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1995                    *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 *     and: Juergen Pfeifer                         1997-1999,2008          *
 ****************************************************************************/

/* p_above.c
 */
#include "panel.priv.h"

MODULE_ID("$Id: p_above.c,v 1.9 2012/03/10 23:43:41 tom Exp $")

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(PANEL *)
ground_panel(SCREEN * sp)
{
  T((T_CALLED("ground_panel(%p)"), (void *)sp));
  if (sp)
    {
      struct panelhook *ph = NCURSES_SP_NAME(_nc_panelhook) (sp);

      if (_nc_bottom_panel)	/* this is the pseudo panel */
	returnPanel(_nc_bottom_panel->above);
      else
	returnPanel(0);
    }
  else
    {
      if (0 == CURRENT_SCREEN)
	returnPanel(0);
      else
	returnPanel(ground_panel(CURRENT_SCREEN));
    }
}
#endif

NCURSES_EXPORT(PANEL *)
panel_above(const PANEL * pan)
{
  PANEL *result;

  T((T_CALLED("panel_above(%p)"), (const void *)pan));
  if (pan)
    result = pan->above;
  else
    {
#if NCURSES_SP_FUNCS
      result = ground_panel(CURRENT_SCREEN);
#else
      /* if top and bottom are equal, we have no or only the pseudo panel;
         if not, we return the panel above the pseudo panel */
      result = EMPTY_STACK()? (PANEL *) 0 : _nc_bottom_panel->above;
#endif
    }
  returnPanel(result);
}
