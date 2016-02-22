/*
 * Joshua Henderson <joshua.henderson@microchip.com>
 * Copyright (C) 2015 Microchip Technology Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 */
#ifndef PIC32MZDA_COMMON_H
#define PIC32MZDA_COMMON_H

/* early clock */
u32 pic32_get_pbclk(int bus);
u32 pic32_get_sysclk(void);

/* Device configuration */
void __init pic32_config_init(void);
int pic32_set_lcd_mode(int mode);
int pic32_set_sdhci_adma_fifo_threshold(u32 rthrs, u32 wthrs);
u32 pic32_get_boot_status(void);
int pic32_disable_lcd(void);
int pic32_enable_lcd(void);

#endif
