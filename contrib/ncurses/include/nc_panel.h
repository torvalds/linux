/****************************************************************************
 * Copyright (c) 1998-2008,2009 Free Software Foundation, Inc.              *
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
 *  Author: Juergen Pfeifer                        1997                     *
 *     and: Thomas E. Dickey                                                *
 ****************************************************************************/

/*
 * $Id: nc_panel.h,v 1.7 2009/07/04 18:20:02 tom Exp $
 *
 *	nc_panel.h
 *
 *	Headerfile to provide an interface for the panel layer into
 *      the SCREEN structure of the ncurses core.
 */

#ifndef NC_PANEL_H
#define NC_PANEL_H 1

#ifdef __cplusplus
extern "C"
{
#endif

  struct panel;			/* Forward Declaration */

  struct panelhook
    {
      struct panel *top_panel;
      struct panel *bottom_panel;
      struct panel *stdscr_pseudo_panel;
#if NO_LEAKS
      int (*destroy) (struct panel *);
#endif
    };

  struct screen;		/* Forward declaration */
/* Retrieve the panelhook of the specified screen */
  extern NCURSES_EXPORT(struct panelhook *)
    _nc_panelhook (void);
#if NCURSES_SP_FUNCS
  extern NCURSES_EXPORT(struct panelhook *)
    NCURSES_SP_NAME(_nc_panelhook) (SCREEN *);
#endif

#ifdef __cplusplus
}
#endif

#endif				/* NC_PANEL_H */
