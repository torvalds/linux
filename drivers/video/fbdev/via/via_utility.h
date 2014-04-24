/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTIES OR REPRESENTATIONS; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.See the GNU General Public License
 * for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#ifndef __VIAUTILITY_H__
#define __VIAUTILITY_H__

/* These functions are used to get information about device's state */
void viafb_get_device_support_state(u32 *support_state);
void viafb_get_device_connect_state(u32 *connect_state);
bool viafb_lcd_get_support_expand_state(u32 xres, u32 yres);

/* These function are used to access gamma table */
void viafb_set_gamma_table(int bpp, unsigned int *gamma_table);
void viafb_get_gamma_table(unsigned int *gamma_table);
void viafb_get_gamma_support_state(int bpp, unsigned int *support_state);

#endif /* __VIAUTILITY_H__ */
