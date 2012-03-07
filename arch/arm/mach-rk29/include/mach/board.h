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

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/notifier.h>
#include <plat/board.h>

struct led_newton_pwm {
	const char	*name;
	unsigned int	pwm_id;
	unsigned	 	pwm_gpio;
	char*			pwm_iomux_name;
	unsigned int 	pwm_iomux_pwm;
	unsigned int 	pwm_iomux_gpio;
	unsigned int	freq;/**/
	unsigned int	period;/*1-100*/
};

struct led_newton_pwm_platform_data {
	int			num_leds;
	struct led_newton_pwm* leds;
};

struct hdmi_platform_data {
	u32 hdmi_on_pin;
	u32 hdmi_on_level;
	int (*io_init)(void);
	int (*io_deinit)(void);
};

/* adc battery */
struct rk29_adc_battery_platform_data {
	int (*io_init)(void);
	int (*io_deinit)(void);

	int dc_det_pin;
	int batt_low_pin;
	int charge_ok_pin;
	int charge_set_pin;

	int dc_det_level;
	int batt_low_level;
	int charge_ok_level;
	int charge_set_level;
};


struct rk29_button_light_info{
	u32 led_on_pin;
	u32 led_on_level;
	int (*io_init)(void);
	int (*io_deinit)(void);
};

/*vmac*/
struct rk29_vmac_platform_data {
	int (*vmac_register_set)(void);
	int (*rmii_io_init)(void);
	int (*rmii_io_deinit)(void);
    int (*rmii_power_control)(int enable);
};

#define INVALID_GPIO        -1


#ifndef _LINUX_WLAN_PLAT_H_
struct wifi_platform_data {
        int (*set_power)(int val);
        int (*set_reset)(int val);
        int (*set_carddetect)(int val);
        void *(*mem_prealloc)(int section, unsigned long size);
        int (*get_mac_addr)(unsigned char *buf);
};
#endif

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

struct bq27510_platform_data {	
	int (*init_dc_check_pin)(void);	
	unsigned int dc_check_pin;	
	unsigned int bat_num;
};

struct bq27541_platform_data {	
	int (*init_dc_check_pin)(void);	
	unsigned int dc_check_pin;
	unsigned int bat_check_pin;
	unsigned int chgok_check_pin;
	unsigned int bat_num;
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
    int     standby_pin;
    int     standby_value;
    int     disp_on_pin;
    int     disp_on_value;
 
};
//added by zyw
struct atmel_1386_platform_data {
	u8    numtouch;	/* Number of touches to report	*/
	int  (*init_platform_hw)(struct device *dev);
	void  (*exit_platform_hw)(struct device *dev);
	int   max_x;    /* The default reported X range   */  
	int   max_y;    /* The default reported Y range   */
	u8    (*valid_interrupt) (void);
	u8    (*read_chg) (void);
};

/*sintex touch*/
struct sintek_platform_data {
	u16 	model;

	int 	(*get_pendown_state)(void);
	int 	(*init_platform_hw)(void);
	int 	(*sintek_platform_sleep)(void);
	int 	(*sintek_platform_wakeup)(void);
	void	(*exit_platform_hw)(void);
};

/*synaptics  touch*/
struct synaptics_platform_data {
	u16 	model;
	
	int 	(*get_pendown_state)(void);
	int 	(*init_platform_hw)(void);
	int 	(*sintek_platform_sleep)(void);
	int 	(*sintek_platform_wakeup)(void);
	void	(*exit_platform_hw)(void);
};


struct bma023_platform_data {
    u16     model;
	u16     swap_xy;
	u16		swap_xyz;
	signed char orientation[9];
    int     (*get_pendown_state)(void);
    int     (*init_platform_hw)(void);
    int     (*mma8452_platform_sleep)(void);
    int     (*mma8452_platform_wakeup)(void);
    void    (*exit_platform_hw)(void);
};

struct cm3202_platform_data {
	int CM3202_SD_IOPIN;
	int DATA_ADC_CHN;
	int     (*init_platform_hw)(void);
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

struct ft5406_platform_data {
    int     (*get_pendown_state)(void);
    int     (*init_platform_hw)(void);
    int     (*platform_sleep)(void);
    int     (*platform_wakeup)(void);
    void    (*exit_platform_hw)(void);
};


struct cs42l52_platform_data {
    int     (*get_pendown_state)(void);
    int     (*init_platform_hw)(void);
    int     (*platform_sleep)(void);
    int     (*platform_wakeup)(void);
    void    (*exit_platform_hw)(void);
};

//tcl miaozh add
/*nas touch */
struct nas_platform_data {
    u16     model;

    int     (*get_pendown_state)(void);
    int     (*init_platform_hw)(void);
    int     (*nas_platform_sleep)(void);
    int     (*nas_platform_wakeup)(void);
    void    (*exit_platform_hw)(void);
};


struct laibao_platform_data {
    u16     model;

    int     (*get_pendown_state)(void);
    int     (*init_platform_hw)(void);
    int     (*laibao_platform_sleep)(void);
    int     (*laibao_platform_wakeup)(void);
    void    (*exit_platform_hw)(void);
    int     pwr_pin;
	int		pwr_on_value;
    int     reset_pin;
	int		reset_value;
};


struct rk29_gpio_expander_info {
	unsigned int gpio_num;
	unsigned int pin_type;//GPIO_IN or GPIO_OUT
	unsigned int pin_value;//GPIO_HIGH or GPIO_LOW
};
struct rk29_newton_data {
};

struct tca6424_platform_data {
	/*  the first extern gpio number in all of gpio groups */
	unsigned int gpio_base;
	unsigned int gpio_pin_num;
	/*  the first gpio irq  number in all of irq source */

	unsigned int gpio_irq_start;
	unsigned int irq_pin_num;        //number of interrupt
	unsigned int tca6424_irq_pin;     //rk29 gpio
	unsigned int expand_port_group;
	unsigned int expand_port_pinnum;
	unsigned int rk_irq_mode;
	unsigned int rk_irq_gpio_pull_up_down;
	
	/* initial polarity inversion setting */
	uint16_t	invert;
	struct rk29_gpio_expander_info  *settinginfo;
	int  settinginfolen;
	void	*context;	/* param to setup/teardown */

	int		(*setup)(struct i2c_client *client,unsigned gpio, unsigned ngpio,void *context);
	int		(*teardown)(struct i2c_client *client,unsigned gpio, unsigned ngpio,void *context);
	char	**names;
	void    (*reseti2cpin)(void);
};

void __init rk29_setup_early_printk(void);
void __init rk29_map_common_io(void);
void __init board_power_init(void);

enum periph_pll {
	periph_pll_96mhz = 96000000, /* save more power */
	periph_pll_144mhz = 144000000,
	periph_pll_288mhz = 288000000, /* for USB 1.1 */
	periph_pll_300mhz = 300000000, /* for Ethernet */
#if defined(CONFIG_RK29_VMAC) && defined(CONFIG_USB20_HOST_EN)
	periph_pll_default = periph_pll_300mhz,
#else
	periph_pll_default = periph_pll_288mhz,
#endif
};

enum codec_pll {
	codec_pll_297mhz = 297000000, /* for HDMI */
	codec_pll_300mhz = 300000000,
	codec_pll_504mhz = 504000000,
	codec_pll_552mhz = 552000000,
	codec_pll_594mhz = 594000000, /* for HDMI */
	codec_pll_600mhz = 600000000,
};

void __init rk29_clock_init(enum periph_pll ppll_rate); /* codec pll is 297MHz, has xin27m */
void __init rk29_clock_init2(enum periph_pll ppll_rate, enum codec_pll cpll_rate, bool has_xin27m);

#endif
