/* arch/arm/mach-rk2818/include/mach/board.h
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

#ifndef __ASM_ARCH_RK2818_BOARD_H
#define __ASM_ARCH_RK2818_BOARD_H

#include <linux/types.h>
#include <linux/timer.h>
#include <linux/notifier.h>

/* platform device data structures */
struct platform_device;

struct RK2818_mddi_platform_data
{
	void (*panel_power)(int on);
	unsigned has_vsync_irq:1;
};

struct rk2818_i2c_platform_data {
	int     bus_num;        
	unsigned int    flags;     
	unsigned int    slave_addr; 
	unsigned long   scl_rate;   
	void    (*cfg_gpio)(struct platform_device *dev);
};

struct rk2818_fb_gpio{
    u32 display_on;
    u32 lcd_cs;
    u32 lcd_standby;
    u32 mcu_fmk_pin;
};

struct rk2818_fb_iomux{
    char *data16;
    char *data18;
    char *data24;
    char *den;
    char *vsync;
    char *mcu_fmk;
};

struct rk2818_fb_mach_info {
    struct rk2818_fb_gpio *gpio;
    struct rk2818_fb_iomux *iomux;
};

struct rk2818bl_info{
    u32 pwm_id;
    u32 pw_pin;
    u32 bl_ref;
    char *pw_iomux;
    struct timer_list timer;  
    struct notifier_block freq_transition;
};

/* common init routines for use by arch/arm/mach-msm/board-*.c */
void __init rk2818_add_devices(void);
void __init rk2818_map_common_io(void);
void __init rk2818_init_irq(void);
void __init rk2818_init_gpio(void);
void __init rk2818_clock_init(void);

#endif
