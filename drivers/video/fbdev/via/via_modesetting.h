/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.
 * Copyright 2010 Florian Tobias Schandinat <FlorianSchandinat@gmx.de>
 */
/*
 * basic modesetting functions
 */

#ifndef __VIA_MODESETTING_H__
#define __VIA_MODESETTING_H__

#include <linux/types.h>


#define VIA_PITCH_SIZE	(1<<3)
#define VIA_PITCH_MAX	0x3FF8


struct via_display_timing {
	u16 hor_total;
	u16 hor_addr;
	u16 hor_blank_start;
	u16 hor_blank_end;
	u16 hor_sync_start;
	u16 hor_sync_end;
	u16 ver_total;
	u16 ver_addr;
	u16 ver_blank_start;
	u16 ver_blank_end;
	u16 ver_sync_start;
	u16 ver_sync_end;
};


void via_set_primary_timing(const struct via_display_timing *timing);
void via_set_secondary_timing(const struct via_display_timing *timing);
void via_set_primary_address(u32 addr);
void via_set_secondary_address(u32 addr);
void via_set_primary_pitch(u32 pitch);
void via_set_secondary_pitch(u32 pitch);
void via_set_primary_color_depth(u8 depth);
void via_set_secondary_color_depth(u8 depth);

#endif /* __VIA_MODESETTING_H__ */
