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
struct i2c_client;
struct rk2818_sdmmc_platform_data {
	unsigned int host_caps;
	unsigned int host_ocr_avail;
	unsigned int use_dma:1;
	unsigned int no_detect:1;
	char dma_name[8];
	void    (*cfg_gpio)(struct platform_device *dev);
};

struct rk2818_i2c_platform_data {
	int     bus_num;        
	unsigned int    flags;     
	unsigned int    slave_addr; 
	unsigned long   scl_rate;   
#define I2C_MODE_IRQ    0
#define I2C_MODE_POLL   1
	unsigned int    mode:1;
	void    (*cfg_gpio)(struct platform_device *dev);
};

struct rk2818_fb_gpio{
    u32 display_on;
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

struct rk2818_gpio_expander_info {
	unsigned int gpio_num;// 初始化的pin 脚宏定义 如：RK2818_PIN_PI0
	unsigned int pin_type;//初始化的pin 为输入pin还是输出pin 如：GPIO_IN
	unsigned int pin_value;//如果为 output pin 设置电平，如：GPIO_HIGH
};


struct pca9554_platform_data {
	/*  the first extern gpio number in all of gpio groups */
	unsigned gpio_base;
	unsigned	gpio_pin_num;
	/*  the first gpio irq  number in all of irq source */

	unsigned gpio_irq_start;
	unsigned irq_pin_num;        //中断的个数
	unsigned    pca9954_irq_pin;        //扩展IO的中断挂在哪个gpio
	/* initial polarity inversion setting */
	uint16_t	invert;
	struct rk2818_gpio_expander_info  *settinginfo;
	int  settinginfolen;
	void		*context;	/* param to setup/teardown */

	int		(*setup)(struct i2c_client *client,unsigned gpio, unsigned ngpio,void *context);
	int		(*teardown)(struct i2c_client *client,unsigned gpio, unsigned ngpio,void *context);
	char		**names;
};
/* common init routines for use by arch/arm/mach-msm/board-*.c */
void __init rk2818_add_devices(void);
void __init rk2818_map_common_io(void);
void __init rk2818_init_irq(void);
void __init rk2818_init_gpio(void);
void __init rk2818_clock_init(void);

#endif
