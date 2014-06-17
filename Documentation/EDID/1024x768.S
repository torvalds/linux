/*
   1024x768.S: EDID data set for standard 1024x768 60 Hz monitor

   Copyright (C) 2011 Carsten Emde <C.Emde@osadl.org>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.
*/

/* EDID */
#define VERSION 1
#define REVISION 3

/* Display */
#define CLOCK 65000 /* kHz */
#define XPIX 1024
#define YPIX 768
#define XY_RATIO XY_RATIO_4_3
#define XBLANK 320
#define YBLANK 38
#define XOFFSET 8
#define XPULSE 144
#define YOFFSET (63+3)
#define YPULSE (63+6)
#define DPI 72
#define VFREQ 60 /* Hz */
#define TIMING_NAME "Linux XGA"
#define ESTABLISHED_TIMING2_BITS 0x08 /* Bit 3 -> 1024x768 @60 Hz */
#define HSYNC_POL 0
#define VSYNC_POL 0
#define CRC 0x55

#include "edid.S"
