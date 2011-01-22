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
#include <linux/timer.h>
#include <linux/notifier.h>

/*spi*/
struct spi_cs_gpio {
	const char *name;
	unsigned int cs_gpio;
	char *cs_iomux_name;
	unsigned int cs_iomux_mode;
};

struct rk29xx_spi_platform_data {
	int (*io_init)(struct spi_cs_gpio*, int);
	int (*io_deinit)(struct spi_cs_gpio*, int);
	int (*io_fix_leakage_bug)(void);
	int (*io_resume_leakage_bug)(void);
	struct spi_cs_gpio *chipselect_gpios;	
	u16 num_chipselect;
};

/*vmac*/
struct rk29_vmac_platform_data {
	int (*vmac_register_set)(void);
	int (*rmii_io_init)(void);
	int (*rmii_io_deinit)(void);
    int (*rmii_power_control)(int enable);
};

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

struct rk29_bl_info{
    u32 pwm_id;
    u32 bl_ref;
    int (*io_init)(void);
    int (*io_deinit)(void);
    struct timer_list timer;  
    struct notifier_block freq_transition;
};

struct wifi_platform_data {
        int (*set_power)(int val);
        int (*set_reset)(int val);
        int (*set_carddetect)(int val);
        void *(*mem_prealloc)(int section, unsigned long size);
};

struct rk29_sdmmc_platform_data {
	unsigned int host_caps;
	unsigned int host_ocr_avail;
	unsigned int use_dma:1;
	char dma_name[8];
	int (*io_init)(void);
	int (*io_deinit)(void);
	int (*status)(struct device *);
	int (*register_status_notify)(void (*callback)(int card_present, void *dev_id), void *dev_id);
        int detect_irq;
};
struct rk29_i2c_platform_data {
	int     bus_num;        
	unsigned int    flags;     
	unsigned int    slave_addr; 
	unsigned long   scl_rate;   
#define I2C_MODE_IRQ    0
#define I2C_MODE_POLL   1
	unsigned int    mode:1;
	int (*io_init)(void);
	int (*io_deinit)(void);
};

/*i2s*/
struct rk29_i2s_platform_data {
	int (*io_init)(void);
	int (*io_deinit)(void);
};

/*p1003 touch */
struct p1003_platform_data {
    u16     model;

    int     (*get_pendown_state)(void);
    int     (*init_platform_hw)(void);
    int     (*p1003_platform_sleep)(void);
    int     (*p1003_platform_wakeup)(void);
    void    (*exit_platform_hw)(void);
};
struct eeti_egalax_platform_data{
	u16     model;

    int     (*get_pendown_state)(void);
    int     (*init_platform_hw)(void);
    int     (*eeti_egalax_platform_sleep)(void);
    int     (*eeti_egalax_platform_wakeup)(void);
    void    (*exit_platform_hw)(void);
};
struct mma8452_platform_data {
    u16     model;
	u16     swap_xy;
    int     (*get_pendown_state)(void);
    int     (*init_platform_hw)(void);
    int     (*mma8452_platform_sleep)(void);
    int     (*mma8452_platform_wakeup)(void);
    void    (*exit_platform_hw)(void);
};
/*it7260 touch */
struct it7260_platform_data {
    int     (*get_pendown_state)(void);
    int     (*init_platform_hw)(void);
    int     (*it7260_platform_sleep)(void);
    int     (*it7260_platform_wakeup)(void);
    void    (*exit_platform_hw)(void);
};


struct akm8975_platform_data {
	char layouts[3][3];
	char project_name[64];
	int gpio_DRDY;
};


void __init rk29_map_common_io(void);
void __init rk29_clock_init(void);

#define BOOT_MODE_NORMAL		0
#define BOOT_MODE_FACTORY2		1
#define BOOT_MODE_RECOVERY		2
#define BOOT_MODE_CHARGE		3
#define BOOT_MODE_POWER_TEST		4
#define BOOT_MODE_OFFMODE_CHARGING	5
int board_boot_mode(void);

#endif
