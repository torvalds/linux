/*
 * iW7023 Driver for LCD Panel Backlight
 *
 * Copyright (C) 2012 AMLOGIC, INC.
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef __IW7023_HW_H
#define __IW7023_HW_H

#define CHECK_INIT_DONE_MAX_COUNT	10

#define BRIGHTNESS_2D			0x7FF
#define BRIGHTNESS_3D			0x333

#define BRIGHTNESS_2D_MAX		0xFFF
#define BRIGHTNESS_3D_MAX		0x500

/* skyworht 39" */
#define ISET_VALUE_2D_SKY39		0xB43C
#define ISET_VALUE_3D_SKY39		0xB4B4
#define VDAC_VALUE_2D_SKY39		0xE5
#define VDAC_VALUE_3D_SKY39		0x85

#define VDAC_MIN_2D_SKY39		0xB0
#define VDAC_MAX_2D_SKY39		0xF1
#define VDAC_MIN_3D_SKY39		0x00
#define VDAC_MAX_3D_SKY39		0x96

/* skyworht 42" */
#define ISET_VALUE_2D_SKY42		0xA537
#define ISET_VALUE_3D_SKY42		0xA5A5
#define VDAC_VALUE_2D_SKY42		0xE6
#define VDAC_VALUE_3D_SKY42		0x84

#define VDAC_MIN_2D_SKY42		0xB3
#define VDAC_MAX_2D_SKY42		0xF4
#define VDAC_MIN_3D_SKY42		0x3A
#define VDAC_MAX_3D_SKY42		0x97

/* skyworht 50" */
#define ISET_VALUE_2D_SKY50		0xB43C
#define ISET_VALUE_3D_SKY50		0xB4B4
#define VDAC_VALUE_2D_SKY50		0xE1
#define VDAC_VALUE_3D_SKY50		0x77

#define VDAC_MIN_2D_SKY50		0xC0
#define VDAC_MAX_2D_SKY50		0xF5
#define VDAC_MIN_3D_SKY50		0x47
#define VDAC_MAX_3D_SKY50		0xA3

#define EEPROM_ADDR_VDAC_2D		3504	// 0xDB0
#define EEPROM_ADDR_VDAC_3D		3506	// 0xDB2

#define VSYNC_CNT_2D_3D			64
#define VSYNC_CNT_3D_2D			64
#define VSYNC_CNT_SET_BRI_ZERO		49
#define VSYNC_CNT_RAMP			45
#define VSYNC_CNT_SET_BRI_2D		15
#define VSYNC_CNT_SET_BRI_3D		15
#define VSYNC_CNT_WAIT_EN_PROT		0


/* scan timing parameters for 42"*/
#define DEFAULT_TD0_2D		333 // 0.333ms
#define DEFAULT_DG1_2D		720 // 0.720ms
#define DEFAULT_DELTAT_2D	790 // 0.790ms

#define DEFAULT_TD0_3D		333 // 0.333ms
#define DEFAULT_DG1_3D		720 // 0.720ms
#define DEFAULT_DELTAT_3D	790 // 0.790ms

/* scan timing parameters for 39"*/
#define DEFAULT_TD0_2D_SKY39		333  // 0.333ms
#define DEFAULT_DG1_2D_SKY39		700  // 0.700ms
#define DEFAULT_DELTAT_2D_SKY39		1104 // 1.104ms

#define DEFAULT_TD0_3D_SKY39		333  // 0.333ms
#define DEFAULT_DG1_3D_SKY39		700  // 0.700ms
#define DEFAULT_DELTAT_3D_SKY39		1104 // 1.104ms

/* scan timing parameters for 50"*/
#define DEFAULT_TD0_2D_SKY50		333  // 0.333ms
#define DEFAULT_DG1_2D_SKY50		720  // 0.720ms
#define DEFAULT_DELTAT_2D_SKY50		790  // 1.120ms

#define DEFAULT_TD0_3D_SKY50		333  // 0.333ms
#define DEFAULT_DG1_3D_SKY50		720  // 0.720ms
#define DEFAULT_DELTAT_3D_SKY50		790  // 1.120ms


#define EEPROM_ADDR_PANEL	3463

struct iwatt_reg_map
{
    u16 addr;
    u16 val_2d;
    u16 val_3d;
};


#endif /* __IW7023_HW_H */



