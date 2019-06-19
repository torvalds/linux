/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Driver for the Conexant CX25821 PCIe bridge
 *
 *  Copyright (C) 2009 Conexant Systems Inc.
 *  Authors  <shu.lin@conexant.com>, <hiep.huynh@conexant.com>
 */

#ifndef _MEDUSA_VIDEO_H
#define _MEDUSA_VIDEO_H

#include "cx25821-medusa-defines.h"

/* Color control constants */
#define VIDEO_PROCAMP_MIN                 0
#define VIDEO_PROCAMP_MAX                 10000
#define UNSIGNED_BYTE_MIN                 0
#define UNSIGNED_BYTE_MAX                 0xFF
#define SIGNED_BYTE_MIN                   -128
#define SIGNED_BYTE_MAX                   127

/* Default video color settings */
#define SHARPNESS_DEFAULT                 50
#define SATURATION_DEFAULT              5000
#define BRIGHTNESS_DEFAULT              6200
#define CONTRAST_DEFAULT                5000
#define HUE_DEFAULT                     5000

#endif
