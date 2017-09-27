/*
   800x600.S: EDID data set for standard 800x600 60 Hz monitor

   Copyright (C) 2011 Carsten Emde <C.Emde@osadl.org>
   Copyright (C) 2014 Linaro Limited

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
*/

/* EDID */
#define VERSION 1
#define REVISION 3

/* Display */
#define CLOCK 40000 /* kHz */
#define XPIX 800
#define YPIX 600
#define XY_RATIO XY_RATIO_4_3
#define XBLANK 256
#define YBLANK 28
#define XOFFSET 40
#define XPULSE 128
#define YOFFSET (63+1)
#define YPULSE (63+4)
#define DPI 72
#define VFREQ 60 /* Hz */
#define TIMING_NAME "Linux SVGA"
#define ESTABLISHED_TIMING1_BITS 0x01 /* Bit 0: 800x600 @ 60Hz */
#define HSYNC_POL 1
#define VSYNC_POL 1
#define CRC 0xc2

#include "edid.S"
