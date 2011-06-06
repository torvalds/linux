/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.
 * Copyright 2010 Florian Tobias Schandinat <FlorianSchandinat@gmx.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTIES OR REPRESENTATIONS; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
/*
 * basic modesetting functions
 */

#ifndef __VIA_MODESETTING_H__
#define __VIA_MODESETTING_H__

#include <linux/types.h>


#define VIA_PITCH_SIZE	(1<<3)
#define VIA_PITCH_MAX	0x3FF8


void via_set_primary_address(u32 addr);
void via_set_secondary_address(u32 addr);
void via_set_primary_pitch(u32 pitch);
void via_set_secondary_pitch(u32 pitch);
void via_set_primary_color_depth(u8 depth);
void via_set_secondary_color_depth(u8 depth);

#endif /* __VIA_MODESETTING_H__ */
