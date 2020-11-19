/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2018 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#ifndef __PLATFORM_ZTE_ZX296716_SDIO_H__
#define __PLATFORM_ZTE_ZX296716_SDIO_H__

extern void sdio_reinit(void);
extern void extern_wifi_set_enable(int val);
#ifdef CONFIG_A16T03_BOARD
extern int sdio_host_is_null(void);
extern void remove_card(void);
#endif /* CONFIG_A16T03_BOARD */

#endif /* __PLATFORM_ZTE_ZX296716_SDIO_H__ */
