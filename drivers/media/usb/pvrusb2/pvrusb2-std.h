/*
 *
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#ifndef __PVRUSB2_STD_H
#define __PVRUSB2_STD_H

#include <linux/videodev2.h>

// Convert string describing one or more video standards into a mask of V4L
// standard bits.  Return true if conversion succeeds otherwise return
// false.  String is expected to be of the form: C1-x/y;C2-a/b where C1 and
// C2 are color system names (e.g. "PAL", "NTSC") and x, y, a, and b are
// modulation schemes (e.g. "M", "B", "G", etc).
int pvr2_std_str_to_id(v4l2_std_id *idPtr,const char *bufPtr,
		       unsigned int bufSize);

// Convert any arbitrary set of video standard bits into an unambiguous
// readable string.  Return value is the number of bytes consumed in the
// buffer.  The formatted string is of a form that can be parsed by our
// sibling std_std_to_id() function.
unsigned int pvr2_std_id_to_str(char *bufPtr, unsigned int bufSize,
				v4l2_std_id id);

// Create an array of suitable v4l2_standard structures given a bit mask of
// video standards to support.  The array is allocated from the heap, and
// the number of elements is returned in the first argument.
struct v4l2_standard *pvr2_std_create_enum(unsigned int *countptr,
					   v4l2_std_id id);

// Return mask of which video standard bits are valid
v4l2_std_id pvr2_std_get_usable(void);

#endif /* __PVRUSB2_STD_H */
