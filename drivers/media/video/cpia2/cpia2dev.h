/****************************************************************************
 *
 *  Filename: cpia2dev.h
 *
 *  Copyright 2001, STMicrolectronics, Inc.
 *
 *  Contact:  steve.miller@st.com
 *
 *  Description:
 *     This file provides definitions for applications wanting to use the
 *     cpia2 driver beyond the generic v4l capabilities.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 ****************************************************************************/

#ifndef CPIA2_DEV_HEADER
#define CPIA2_DEV_HEADER

#include <linux/videodev.h>

/***
 * The following defines are ioctl numbers based on video4linux private ioctls,
 * which can range from 192 (BASE_VIDIOCPRIVATE) to 255. All of these take int
 * args
 */
#define CPIA2_IOC_SET_GPIO         _IOW('v', BASE_VIDIOCPRIVATE + 17, __u32)

/* V4L2 driver specific controls */
#define CPIA2_CID_TARGET_KB     (V4L2_CID_PRIVATE_BASE+0)
#define CPIA2_CID_GPIO          (V4L2_CID_PRIVATE_BASE+1)
#define CPIA2_CID_FLICKER_MODE  (V4L2_CID_PRIVATE_BASE+2)
#define CPIA2_CID_FRAMERATE     (V4L2_CID_PRIVATE_BASE+3)
#define CPIA2_CID_USB_ALT       (V4L2_CID_PRIVATE_BASE+4)
#define CPIA2_CID_LIGHTS        (V4L2_CID_PRIVATE_BASE+5)
#define CPIA2_CID_RESET_CAMERA  (V4L2_CID_PRIVATE_BASE+6)

#endif
