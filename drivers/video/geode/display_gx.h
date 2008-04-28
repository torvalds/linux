/*
 * Geode GX display controller
 *
 * Copyright (C) 2006 Arcom Control Systems Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __DISPLAY_GX_H__
#define __DISPLAY_GX_H__

unsigned int gx_frame_buffer_size(void);
int gx_line_delta(int xres, int bpp);

extern struct geode_dc_ops gx_dc_ops;

/* MSR that tells us if a TFT or CRT is attached */
#define GLD_MSR_CONFIG_DM_FP 0x40

#endif /* !__DISPLAY_GX1_H__ */
