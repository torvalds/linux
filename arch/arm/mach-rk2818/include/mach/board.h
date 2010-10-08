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



#define INVALID_GPIO        -1


struct rk2818_io_cfg {
	int (*io_init)(void *);
	int (*io_deinit)(void *);
};

/* platform device data structures */
struct platform_device;
struct i2c_client;
struct rk2818_sdmmc_platform_data {
	unsigned int host_caps;
	unsigned int host_ocr_avail;
	unsigned int use_dma:1;
	unsigned int no_detect:1;
	char dma_name[8];
	int (*io_init)(void);
	int (*io_deinit)(void);
	int (*status)(struct device *);
	int (*register_status_notify)(void (*callback)(int card_present, void *dev_id), void *dev_id);
};

struct wifi_platform_data {
        int (*set_power)(int val);
        int (*set_reset)(int val);
        int (*set_carddetect)(int val);
        void *(*mem_prealloc)(int section, unsigned long size);
};

struct wifi_power_gpio_control_data {
        unsigned int use_gpio;                    /* If uses GPIO to control wifi power supply. 0 - no, 1 - yes. */
        unsigned int gpio_iomux;                  /* If the GPIO is iomux. 0 - no, 1 - yes. */
        char *iomux_name;                         /* IOMUX name */
        unsigned int   iomux_value;               /* IOMUX value - which function is choosen. */
        unsigned int   gpio_id;                   /* GPIO number */
        unsigned int   sensi_level;               /* GPIO sensitive level. */
};

struct rk2818_i2c_spi_data {
	int     bus_num;        
	unsigned int    flags;     
	unsigned int    slave_addr; 
	unsigned long   scl_rate;   
};
struct rk2818_i2c_platform_data {
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

struct rk2818lcd_info{
    u32 lcd_id;
    u32 txd_pin;
    u32 clk_pin;
    u32 cs_pin;
    int (*io_init)(void);
    int (*io_deinit)(void);
};

struct rk2818_fb_setting_info{
    u8 data_num;
    u8 vsync_en;
    u8 den_en;
    u8 mcu_fmk_en;
    u8 disp_on_en;
    u8 standby_en;
};

struct rk2818fb_info{
    u32 fb_id;
    u32 disp_on_pin;
    u8 disp_on_value;
    u32 standby_pin;
    u8 standby_value;
    u32 mcu_fmk_pin;
    struct rk2818lcd_info *lcd_info;
    int (*io_init)(struct rk2818_fb_setting_info *fb_setting);
    int (*io_deinit)(void);
};

struct rk2818_bl_info{
    u32 pwm_id;
    u32 bl_ref;
    int (*io_init)(void);
    int (*io_deinit)(void);
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
	unsigned int gpio_base;
	unsigned int gpio_pin_num;
	/*  the first gpio irq  number in all of irq source */

	unsigned int gpio_irq_start;
	unsigned int irq_pin_num;        //中断的个数
	unsigned int pca9954_irq_pin;        //扩展IO的中断挂在哪个gpio
	/* initial polarity inversion setting */
	uint16_t invert;
	struct rk2818_gpio_expander_info  *settinginfo;
	int  settinginfolen;
	void	*context;	/* param to setup/teardown */

	int		(*setup)(struct i2c_client *client,unsigned gpio, unsigned ngpio,void *context);
	int		(*teardown)(struct i2c_client *client,unsigned gpio, unsigned ngpio,void *context);
	char	**names;
};

struct tca6424_platform_data {
	/*  the first extern gpio number in all of gpio groups */
	unsigned int gpio_base;
	unsigned int gpio_pin_num;
	/*  the first gpio irq  number in all of irq source */

	unsigned int gpio_irq_start;
	unsigned int irq_pin_num;        //中断的个数
	unsigned int tca6424_irq_pin;     //扩展IO的中断挂在哪个gpio
	unsigned int expand_port_group;
	unsigned int expand_port_pinnum;
	unsigned int rk_irq_mode;
	unsigned int rk_irq_gpio_pull_up_down;
	
	/* initial polarity inversion setting */
	uint16_t	invert;
	struct rk2818_gpio_expander_info  *settinginfo;
	int  settinginfolen;
	void	*context;	/* param to setup/teardown */

	int		(*setup)(struct i2c_client *client,unsigned gpio, unsigned ngpio,void *context);
	int		(*teardown)(struct i2c_client *client,unsigned gpio, unsigned ngpio,void *context);
	char	**names;
	void    (*reseti2cpin)(void);
};

/*battery*/
struct rk2818_battery_platform_data {
	int (*io_init)(void);
	int (*io_deinit)(void);
	int charge_ok_pin;
	int charge_ok_level;
};

/*g_sensor*/
struct rk2818_gs_platform_data {
	int (*io_init)(void);
	int (*io_deinit)(void);
	int gsensor_irq_pin;
	bool	swap_xy;	/* swap x and y axes  add swj */
};

/*serial*/
struct rk2818_serial_platform_data {
	int (*io_init)(void);
	int (*io_deinit)(void);
};

/*i2s*/
struct rk2818_i2s_platform_data {
	int (*io_init)(void);
	int (*io_deinit)(void);
};

/*spi*/
struct spi_cs_gpio {
	const char *name;
	unsigned int cs_gpio;
	char *cs_iomux_name;
	unsigned int cs_iomux_mode;
};

struct rk2818_spi_platform_data {
	int (*io_init)(struct spi_cs_gpio*, int);
	int (*io_deinit)(struct spi_cs_gpio*, int);
	int (*io_fix_leakage_bug)(void);
	int (*io_resume_leakage_bug)(void);
	struct spi_cs_gpio *chipselect_gpios;	
	u16 num_chipselect;
};

/*rtc*/
struct rk2818_rtc_platform_data {
	u8 irq_type;
	int (*io_init)(void);
	int (*io_deinit)(void);
};

//ROCKCHIP AD KEY CODE ,for demo board
//      key		--->	EV	
#define AD2KEY1                 114   ///VOLUME_DOWN
#define AD2KEY2                 115   ///VOLUME_UP
#define AD2KEY3                 59    ///MENU
#define AD2KEY4                 102   ///HOME
#define AD2KEY5                 158   ///BACK
#define AD2KEY6                 61    ///CALL
#define AD2KEY7                 127   ///SEARCH
#define ENDCALL					62
#define	KEYSTART				232  ///DPAD_CENTER  28			//ENTER
#define KEYMENU					AD2KEY6		///CALL
#define	KEY_PLAY_SHORT_PRESS	KEYSTART	//code for short press the play key
#define	KEY_PLAY_LONG_PRESS		ENDCALL		//code for long press the play key

//ADC Registers
typedef  struct tagADC_keyst
{
	unsigned int adc_value;
	unsigned int adc_keycode;
}ADC_keyst,*pADC_keyst;

/*ad key*/
struct adc_key_data{
    u32 pin_playon;
    u32 playon_level;
    u32 adc_empty;
    u32 adc_invalid;
    u32 adc_drift;
    u32 adc_chn;
    ADC_keyst * adc_key_table;
    unsigned char *initKeyCode;
    u32 adc_key_cnt;
};

struct rk2818_adckey_platform_data {
	int (*io_init)(void);
	int (*io_deinit)(void);
	struct adc_key_data *adc_key;
};

struct  jgball_data {
	u32 pin_up;
	u32 pin_down;
	u32 pin_left;
	u32 pin_right;
};

struct rk2818_jogball_paltform_data {
	struct jgball_data *jogball_key;
};


/* common init routines for use by arch/arm/mach-msm/board-*.c */
void __init rk2818_add_devices(void);
void __init rk2818_map_common_io(void);
void __init rk2818_init_irq(void);
void __init rk2818_init_gpio(void);
void __init rk2818_clock_init(void);

#endif
