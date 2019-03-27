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

/* $Id: panel.priv.h,v 1.25 2012/12/15 23:57:43 tom Exp $ */

#ifndef NCURSES_PANEL_PRIV_H
#define NCURSES_PANEL_PRIV_H 1

#if HAVE_CONFIG_H
#  include <ncurses_cfg.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct screen;              /* forward declaration */

#include "curses.priv.h"    /* includes nc_panel.h */
#include "panel.h"


#if USE_RCS_IDS
#  define MODULE_ID(id) static const char Ident[] = id;
#else
#  define MODULE_ID(id) /*nothing*/
#endif


#ifdef TRACE
   extern NCURSES_EXPORT(const char *) _nc_my_visbuf (const void *);
#  ifdef TRACE_TXT
#    define USER_PTR(ptr) _nc_visbuf((const char *)ptr)
#  else
#    define USER_PTR(ptr) _nc_my_visbuf((const char *)ptr)
#  endif

#  define returnPanel(code)	TRACE_RETURN(code,panel)

   extern NCURSES_EXPORT(PANEL *) _nc_retrace_panel (PANEL *);
   extern NCURSES_EXPORT(void) _nc_dPanel (const char*, const PANEL*);
   extern NCURSES_EXPORT(void) _nc_dStack (const char*, int, const PANEL*);
   extern NCURSES_EXPORT(void) _nc_Wnoutrefresh (const PANEL*);
   extern NCURSES_EXPORT(void) _nc_Touchpan (const PANEL*);
   extern NCURSES_EXPORT(void) _nc_Touchline (const PANEL*, int, int);

#  define dBug(x) _tracef x
#  define dPanel(text,pan) _nc_dPanel(text,pan)
#  define dStack(fmt,num,pan) _nc_dStack(fmt,num,pan)
#  define Wnoutrefresh(pan) _nc_Wnoutrefresh(pan)
#  define Touchpan(pan) _nc_Touchpan(pan)
#  define Touchline(pan,start,count) _nc_Touchline(pan,start,count)
#else /* !TRACE */
#  define returnPanel(code)	return code
#  define dBug(x)
#  define dPanel(text,pan)
#  define dStack(fmt,num,pan)
#  define Wnoutrefresh(pan) wnoutrefresh((pan)->win)
#  define Touchpan(pan) touchwin((pan)->win)
#  define Touchline(pan,start,count) touchline((pan)->win,start,count)
#endif

#if NCURSES_SP_FUNCS
#define GetScreenHook(sp) \
			struct panelhook* ph = NCURSES_SP_NAME(_nc_panelhook)(sp)
#define GetPanelHook(pan) \
			GetScreenHook(pan ? _nc_screen_of((pan)->win) : 0)
#define GetWindowHook(win) \
			SCREEN* sp = _nc_screen_of(win); \
			GetScreenHook(sp)
#define GetHook(pan)	SCREEN* sp = _nc_screen_of(pan->win); \
			GetScreenHook(sp)

#define _nc_stdscr_pseudo_panel ((ph)->stdscr_pseudo_panel)
#define _nc_top_panel           ((ph)->top_panel)
#define _nc_bottom_panel        ((ph)->bottom_panel)

#else	/* !NCURSES_SP_FUNCS */

#define GetScreenHook(sp) /* nothing */
#define GetPanelHook(pan) /* nothing */
#define GetWindowHook(win) /* nothing */
#define GetHook(pan) /* nothing */

#define _nc_stdscr_pseudo_panel _nc_panelhook()->stdscr_pseudo_panel
#define _nc_top_panel           _nc_panelhook()->top_panel
#define _nc_bottom_panel        _nc_panelhook()->bottom_panel

#endif	/* NCURSES_SP_FUNCS */

#define EMPTY_STACK() (_nc_top_panel == _nc_bottom_panel)
#define Is_Bottom(p)  (((p) != (PANEL*)0) && !EMPTY_STACK() && (_nc_bottom_panel->above == (p)))
#define Is_Top(p)     (((p) != (PANEL*)0) && !EMPTY_STACK() && (_nc_top_panel == (p)))
#define Is_Pseudo(p)  (((p) != (PANEL*)0) && ((p) == _nc_bottom_panel))

/*+-------------------------------------------------------------------------
	IS_LINKED(pan) - check to see if panel is in the stack
--------------------------------------------------------------------------*/
/* This works! The only case where it would fail is, when the list has
   only one element. But this could only be the pseudo panel at the bottom */
#define IS_LINKED(p) (((p)->above || (p)->below ||((p)==_nc_bottom_panel)) ? TRUE : FALSE)

#define PSTARTX(pan) ((pan)->win->_begx)
#define PENDX(pan)   ((pan)->win->_begx + getmaxx((pan)->win) - 1)
#define PSTARTY(pan) ((pan)->win->_begy)
#define PENDY(pan)   ((pan)->win->_begy + getmaxy((pan)->win) - 1)

/*+-------------------------------------------------------------------------
	PANELS_OVERLAPPED(pan1,pan2) - check panel overlapped
---------------------------------------------------------------------------*/
#define PANELS_OVERLAPPED(pan1,pan2) \
(( !(pan1) || !(pan2) || \
       PSTARTY(pan1) > PENDY(pan2) || PENDY(pan1) < PSTARTY(pan2) ||\
       PSTARTX(pan1) > PENDX(pan2) || PENDX(pan1) < PSTARTX(pan2) ) \
     ? FALSE : TRUE)


/*+-------------------------------------------------------------------------
	Compute the intersection rectangle of two overlapping rectangles
---------------------------------------------------------------------------*/
#define COMPUTE_INTERSECTION(pan1,pan2,ix1,ix2,iy1,iy2)\
   ix1 = (PSTARTX(pan1) < PSTARTX(pan2)) ? PSTARTX(pan2) : PSTARTX(pan1);\
   ix2 = (PENDX(pan1)   < PENDX(pan2))   ? PENDX(pan1)   : PENDX(pan2);\
   iy1 = (PSTARTY(pan1) < PSTARTY(pan2)) ? PSTARTY(pan2) : PSTARTY(pan1);\
   iy2 = (PENDY(pan1)   < PENDY(pan2))   ? PENDY(pan1)   : PENDY(pan2);\
   assert((ix1<=ix2) && (iy1<=iy2))


/*+-------------------------------------------------------------------------
	Walk through the panel stack starting at the given location and
        check for intersections; overlapping panels are "touched", so they
        are incrementally overwriting cells that should be hidden.
        If the "touch" flag is set, the panel gets touched before it is
        updated.
---------------------------------------------------------------------------*/
#define PANEL_UPDATE(pan,panstart)\
{  PANEL* pan2 = ((panstart) ? (panstart) : _nc_bottom_panel);\
   while(pan2 && pan2->win) {\
      if ((pan2 != pan) && PANELS_OVERLAPPED(pan,pan2)) {\
        int y, ix1, ix2, iy1, iy2;\
        COMPUTE_INTERSECTION(pan, pan2, ix1, ix2, iy1, iy2);\
	for(y = iy1; y <= iy2; y++) {\
	  if (is_linetouched(pan->win,y - PSTARTY(pan))) {\
            struct ldat* line = &(pan2->win->_line[y - PSTARTY(pan2)]);\
            CHANGED_RANGE(line, ix1 - PSTARTX(pan2), ix2 - PSTARTX(pan2));\
          }\
	}\
      }\
      pan2 = pan2->above;\
   }\
}

/*+-------------------------------------------------------------------------
	Remove panel from stack.
---------------------------------------------------------------------------*/
#define PANEL_UNLINK(pan,err) \
{  err = ERR;\
   if (pan) {\
     if (IS_LINKED(pan)) {\
       if ((pan)->below)\
         (pan)->below->above = (pan)->above;\
       if ((pan)->above)\
         (pan)->above->below = (pan)->below;\
       if ((pan) == _nc_bottom_panel) \
         _nc_bottom_panel = (pan)->above;\
       if ((pan) == _nc_top_panel) \
         _nc_top_panel = (pan)->below;\
       err = OK;\
     }\
     (pan)->above = (pan)->below = (PANEL*)0;\
   }\
}

#define HIDE_PANEL(pan,err,err_if_unlinked)\
  if (IS_LINKED(pan)) {\
    Touchpan(pan);\
    PANEL_UPDATE(pan,(PANEL*)0);\
    PANEL_UNLINK(pan,err);\
  } \
  else {\
      err = err_if_unlinked;\
  }

#if NCURSES_SP_FUNCS
/* These may become later renamed and part of panel.h and the public API */
extern NCURSES_EXPORT(void) NCURSES_SP_NAME(_nc_update_panels)(SCREEN*);
#endif

#endif /* NCURSES_PANEL_PRIV_H */
