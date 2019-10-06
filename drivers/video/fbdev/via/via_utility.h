/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

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
