/*
 * $Id: mouse.c,v 1.24 2017/01/31 00:27:21 tom Exp $
 *
 * mouse.c -- mouse support for dialog
 *
 * Copyright 2002-2016,2017	Thomas E. Dickey
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

#if USE_MOUSE

static int basex, basey, basecode;

static mseRegion *regionList = NULL;

/*=========== region related functions =============*/

static mseRegion *
find_region_by_code(int code)
{
    mseRegion *butPtr;

    for (butPtr = regionList; butPtr; butPtr = butPtr->next) {
	if (code == butPtr->code)
	    break;
    }
    return butPtr;
}

void
dlg_mouse_setbase(int x, int y)
{
    basex = x;
    basey = y;
}

void
dlg_mouse_setcode(int code)
{
    basecode = code;
    DLG_TRACE(("# mouse_setcode %d\n", code));
}

void
dlg_mouse_mkbigregion(int y, int x,
		      int height, int width,
		      int code,
		      int step_y, int step_x,
		      int mode)
{
    mseRegion *butPtr = dlg_mouse_mkregion(y, x, height, width, -DLGK_MOUSE(code));
    butPtr->mode = mode;
    butPtr->step_x = MAX(1, step_x);
    butPtr->step_y = MAX(1, step_y);
}

void
dlg_mouse_free_regions(void)
{
    while (regionList != 0) {
	mseRegion *butPtr = regionList->next;
	free(regionList);
	regionList = butPtr;
    }
}

mseRegion *
dlg_mouse_mkregion(int y, int x, int height, int width, int code)
{
    mseRegion *butPtr;

    if ((butPtr = find_region_by_code(basecode + code)) == 0) {
	butPtr = dlg_calloc(mseRegion, 1);
	assert_ptr(butPtr, "dlg_mouse_mkregion");
	butPtr->next = regionList;
	regionList = butPtr;
    }

    if ((butPtr->mode != -1) ||
	(butPtr->step_x != 0) ||
	(butPtr->step_y != 0) ||
	(butPtr->y != (basey + y)) ||
	(butPtr->Y != (basey + y + height)) ||
	(butPtr->x != (basex + x)) ||
	(butPtr->X != (basex + x + width)) ||
	(butPtr->code != basecode + code)) {
	DLG_TRACE(("# mouse_mkregion %d,%d %dx%d %d (%d)\n",
		   y, x, height, width,
		   butPtr->code, code));
    }

    butPtr->mode = -1;
    butPtr->step_x = 0;
    butPtr->step_y = 0;
    butPtr->y = basey + y;
    butPtr->Y = basey + y + height;
    butPtr->x = basex + x;
    butPtr->X = basex + x + width;
    butPtr->code = basecode + code;

    return butPtr;
}

/* retrieve the frame under the pointer */
static mseRegion *
any_mouse_region(int y, int x, int small)
{
    mseRegion *butPtr;

    for (butPtr = regionList; butPtr; butPtr = butPtr->next) {
	if (small ^ (butPtr->code >= 0)) {
	    continue;
	}
	if (y < butPtr->y || y >= butPtr->Y) {
	    continue;
	}
	if (x < butPtr->x || x >= butPtr->X) {
	    continue;
	}
	break;			/* found */
    }
    return butPtr;
}

/* retrieve the frame under the pointer */
mseRegion *
dlg_mouse_region(int y, int x)
{
    return any_mouse_region(y, x, TRUE);
}

/* retrieve the bigframe under the pointer */
mseRegion *
dlg_mouse_bigregion(int y, int x)
{
    return any_mouse_region(y, x, FALSE);
}

#else
void mouse_dummy(void);
void
mouse_dummy(void)
{
}
#endif /* USE_MOUSE */
