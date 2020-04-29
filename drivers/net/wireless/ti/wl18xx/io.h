/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of wl18xx
 *
 * Copyright (C) 2011 Texas Instruments
 */

#ifndef __WL18XX_IO_H__
#define __WL18XX_IO_H__

int __must_check wl18xx_top_reg_write(struct wl1271 *wl, int addr, u16 val);
int __must_check wl18xx_top_reg_read(struct wl1271 *wl, int addr, u16 *out);

#endif /* __WL18XX_IO_H__ */
