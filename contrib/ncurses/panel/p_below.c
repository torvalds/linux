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

/* p_below.c
 */
#include "panel.priv.h"

MODULE_ID("$Id: p_below.c,v 1.9 2012/03/10 23:43:41 tom Exp $")

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(PANEL *)
ceiling_panel(SCREEN * sp)
{
  T((T_CALLED("ceiling_panel(%p)"), (void *)sp));
  if (sp)
    {
      struct panelhook *ph = NCURSES_SP_NAME(_nc_panelhook) (sp);

      /* if top and bottom are equal, we have no or only the pseudo panel */
      returnPanel(EMPTY_STACK()? (PANEL *) 0 : _nc_top_panel);
    }
  else
    {
      if (0 == CURRENT_SCREEN)
	returnPanel(0);
      else
	returnPanel(ceiling_panel(CURRENT_SCREEN));
    }
}
#endif

NCURSES_EXPORT(PANEL *)
panel_below(const PANEL * pan)
{
  PANEL *result;

  T((T_CALLED("panel_below(%p)"), (const void *)pan));
  if (pan)
    {
      GetHook(pan);
      /* we must not return the pseudo panel */
      result = Is_Pseudo(pan->below) ? (PANEL *) 0 : pan->below;
    }
  else
    {
#if NCURSES_SP_FUNCS
      result = ceiling_panel(CURRENT_SCREEN);
#else
      /* if top and bottom are equal, we have no or only the pseudo panel */
      result = EMPTY_STACK()? (PANEL *) 0 : _nc_top_panel;
#endif
    }
  returnPanel(result);
}
