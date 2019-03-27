/*
 * $Id: mousewget.c,v 1.24 2017/01/31 00:27:21 tom Exp $
 *
 * mousewget.c -- mouse/wgetch support for dialog
 *
 * Copyright 2000-2016,2017   Thomas E. Dickey
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License, version 2.1
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program; if not, write to
 *	Free Software Foundation, Inc.
 *	51 Franklin St., Fifth Floor
 *	Boston, MA 02110, USA.
 */

#include <dialog.h>
#include <dlg_keys.h>

static int
mouse_wgetch(WINDOW *win, int *fkey, bool ignore_errs)
{
    int mouse_err = FALSE;
    int key;

    do {

	key = dlg_getc(win, fkey);

#if USE_MOUSE

	mouse_err = FALSE;
	if (key == KEY_MOUSE) {
	    MEVENT event;
	    mseRegion *p;

	    if (getmouse(&event) != ERR) {
		DLG_TRACE(("# mouse-click abs %d,%d (rel %d,%d)\n",
			   event.y, event.x,
			   event.y - getbegy(win),
			   event.x - getbegx(win)));
		if ((p = dlg_mouse_region(event.y, event.x)) != 0) {
		    key = DLGK_MOUSE(p->code);
		} else if ((p = dlg_mouse_bigregion(event.y, event.x)) != 0) {
		    int x = event.x - p->x;
		    int y = event.y - p->y;
		    int row = (p->X - p->x) / p->step_x;

		    key = -(p->code);
		    switch (p->mode) {
		    case 1:	/* index by lines */
			key += y;
			break;
		    case 2:	/* index by columns */
			key += (x / p->step_x);
			break;
		    default:
		    case 3:	/* index by cells */
			key += (x / p->step_x) + (y * row);
			break;
		    }
		} else {
		    (void) beep();
		    mouse_err = TRUE;
		}
	    } else {
		(void) beep();
		mouse_err = TRUE;
	    }
	}
#endif

    } while (ignore_errs && mouse_err);

    return key;
}

int
dlg_mouse_wgetch(WINDOW *win, int *fkey)
{
    return mouse_wgetch(win, fkey, TRUE);
}

int
dlg_mouse_wgetch_nowait(WINDOW *win, int *fkey)
{
    return mouse_wgetch(win, fkey, FALSE);
}
