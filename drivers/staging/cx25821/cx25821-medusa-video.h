/*
 *  Driver for the Conexant CX25821 PCIe bridge
 *
 *  Copyright (C) 2009 Conexant Systems Inc.
 *  Authors  <shu.lin@conexant.com>, <hiep.huynh@conexant.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _MEDUSA_VIDEO_H
#define _MEDUSA_VIDEO_H

#include "cx25821-medusa-defines.h"

// Color control constants
#define VIDEO_PROCAMP_MIN                 0
#define VIDEO_PROCAMP_MAX                 10000
#define UNSIGNED_BYTE_MIN                 0
#define UNSIGNED_BYTE_MAX                 0xFF
#define SIGNED_BYTE_MIN                   -128
#define SIGNED_BYTE_MAX                   127

// Default video color settings
#define SHARPNESS_DEFAULT                 50
#define SATURATION_DEFAULT              5000
#define BRIGHTNESS_DEFAULT              6200
#define CONTRAST_DEFAULT                5000
#define HUE_DEFAULT                     5000

unsigned short _num_decoders;
unsigned short _num_cameras;

unsigned int _video_standard;
int _display_field_cnt[MAX_DECODERS];

#endif
