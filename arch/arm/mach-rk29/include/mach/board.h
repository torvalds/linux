/* arch/arm/mach-rk29/include/mach/board.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __ASM_ARCH_RK29_BOARD_H
#define __ASM_ARCH_RK29_BOARD_H

#include <linux/types.h>

#define INVALID_GPIO        -1

struct rk29lcd_info{
    u32 lcd_id;
    u32 txd_pin;
    u32 clk_pin;
    u32 cs_pin;
    int (*io_init)(void);
    int (*io_deinit)(void);
};

struct rk29_fb_setting_info{
    u8 data_num;
    u8 vsync_en;
    u8 den_en;
    u8 mcu_fmk_en;
    u8 disp_on_en;
    u8 standby_en;
};

struct rk29fb_info{
    u32 fb_id;
    u32 disp_on_pin;
    u8 disp_on_value;
    u32 standby_pin;
    u8 standby_value;
    u32 mcu_fmk_pin;
    struct rk29lcd_info *lcd_info;
    int (*io_init)(struct rk29_fb_setting_info *fb_setting);
    int (*io_deinit)(void);
};

struct rk29_sdmmc_platform_data {
	unsigned int num_slots;
	unsigned int host_caps;
	unsigned int host_ocr_avail;
	unsigned int use_dma:1;
	char dma_name[8];
	int (*io_init)(void);
	int (*io_deinit)(void);
	int (*status)(struct device *);
	int (*register_status_notify)(void (*callback)(int card_present, void *dev_id), void *dev_id);
};

void __init rk29_map_common_io(void);
void __init rk29_clock_init(void);

#endif
