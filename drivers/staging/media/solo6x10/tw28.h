/*
 * Copyright (C) 2010 Bluecherry, LLC www.bluecherrydvr.com
 * Copyright (C) 2010 Ben Collins <bcollins@bluecherry.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __SOLO6X10_TW28_H
#define __SOLO6X10_TW28_H

#include "solo6x10.h"

#define TW_NUM_CHIP				4
#define TW_BASE_ADDR				0x28
#define TW_CHIP_OFFSET_ADDR(n)			(TW_BASE_ADDR + (n))

/* tw2815 */
#define TW_AV_STAT_ADDR				0x5a
#define TW_HUE_ADDR(n)				(0x07 | ((n) << 4))
#define TW_SATURATION_ADDR(n)			(0x08 | ((n) << 4))
#define TW_CONTRAST_ADDR(n)			(0x09 | ((n) << 4))
#define TW_BRIGHTNESS_ADDR(n)			(0x0a | ((n) << 4))
#define TW_AUDIO_OUTPUT_VOL_ADDR		0x70
#define TW_AUDIO_INPUT_GAIN_ADDR(n)		(0x60 + ((n > 1) ? 1 : 0))

/* tw286x */
#define TW286X_AV_STAT_ADDR			0xfd
#define TW286x_HUE_ADDR(n)			(0x06 | ((n) << 4))
#define TW286x_SATURATIONU_ADDR(n)		(0x04 | ((n) << 4))
#define TW286x_SATURATIONV_ADDR(n)		(0x05 | ((n) << 4))
#define TW286x_CONTRAST_ADDR(n)			(0x02 | ((n) << 4))
#define TW286x_BRIGHTNESS_ADDR(n)		(0x01 | ((n) << 4))
#define TW286x_SHARPNESS(n)			(0x03 | ((n) << 4))
#define TW286x_AUDIO_OUTPUT_VOL_ADDR		0xdf
#define TW286x_AUDIO_INPUT_GAIN_ADDR(n)		(0xD0 + ((n > 1) ? 1 : 0))

int solo_tw28_init(struct solo_dev *solo_dev);

int tw28_set_ctrl_val(struct solo_dev *solo_dev, u32 ctrl, u8 ch, s32 val);
int tw28_get_ctrl_val(struct solo_dev *solo_dev, u32 ctrl, u8 ch, s32 *val);

u8 tw28_get_audio_gain(struct solo_dev *solo_dev, u8 ch);
void tw28_set_audio_gain(struct solo_dev *solo_dev, u8 ch, u8 val);
int tw28_get_video_status(struct solo_dev *solo_dev, u8 ch);

#if 0
unsigned int tw2815_get_audio_status(struct SOLO *solo);
void tw2815_Set_AudioOutVol(struct SOLO *solo, unsigned int u_val);
#endif

#endif /* __SOLO6X10_TW28_H */
