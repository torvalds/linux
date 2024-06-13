/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
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
