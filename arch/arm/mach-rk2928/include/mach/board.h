#ifndef __MACH_BOARD_H
#define __MACH_BOARD_H

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/notifier.h>
#include <asm/setup.h>
#include <plat/board.h>
#include <mach/sram.h>
#include <linux/i2c-gpio.h>

#define GPIO_SWPORTA_DR  0x0000
#define GPIO_SWPORTA_DDR 0x0004

#define PMIC_TYPE_TPS65910	2
#define PMIC_TYPE_ACT8931	3

extern struct rk29_sdmmc_platform_data default_sdmmc0_data;
extern struct rk29_sdmmc_platform_data default_sdmmc1_data;

extern struct i2c_gpio_platform_data default_i2c_gpio_data; 

void __init rk2928_map_common_io(void);
void __init rk2928_init_irq(void);
void __init rk2928_map_io(void);
struct machine_desc;
void __init rk2928_fixup(struct machine_desc *desc, struct tag *tags, char **cmdline, struct meminfo *mi);
void __init rk2928_clock_data_init(unsigned long gpll,unsigned long cpll,u32 flags);
void __init board_clock_init(void);
void __init rk2928_iomux_init(void);
void board_gpio_suspend(void);
void board_gpio_resume(void);
void __sramfunc board_pmu_suspend(void);
void __sramfunc board_pmu_resume(void);

extern struct sys_timer rk2928_timer;

//#if defined  CONFIG_BATTERY_RK30_ADC_FAC 
/* adc battery */
struct rk30_adc_battery_platform_data {
	int (*io_init)(void);
	int (*io_deinit)(void);
	int (*is_dc_charging)(void);
	int (*charging_ok)(void);

	int (*is_usb_charging)(void);
	int spport_usb_charging ;

	int dc_det_pin;
	int batt_low_pin;
	int charge_ok_pin;
	int charge_set_pin;

	int dc_det_level;
	int batt_low_level;
	int charge_ok_level;
	int charge_set_level;
	
      	int adc_channel;

	int dc_det_pin_pull;    //pull up/down enable/disbale
	int batt_low_pin_pull;
	int charge_ok_pin_pull;
	int charge_set_pin_pull;

	int low_voltage_protection; // low voltage protection

	int charging_sleep; // don't have lock,if chargeing_sleep = 0;else have lock
	

	int save_capacity;  //save capacity to /data/bat_last_capacity.dat,  suggested use

	int reference_voltage; // the rK2928 is 3300;RK3066 and rk29 are 2500;rk3066B is 1800;
	int pull_up_res;      //divider resistance ,  pull-up resistor
	int pull_down_res; //divider resistance , pull-down resistor

	int time_down_discharge; //the time of capactiy drop 1% --discharge
	int time_up_charge; //the time of capacity up 1% ---charging 


};
//#endif

#ifndef _LINUX_WLAN_PLAT_H_
struct wifi_platform_data {
        int (*set_power)(int val);
        int (*set_reset)(int val);
        int (*set_carddetect)(int val);
        void *(*mem_prealloc)(int section, unsigned long size);
        int (*get_mac_addr)(unsigned char *buf);
};
#endif
#if defined (CONFIG_EETI_EGALAX)
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
#endif
#if defined (CONFIG_TOUCHSCREEN_SITRONIX_A720)
struct ft5x0x_platform_data{
	  u16     model;
    int     (*get_pendown_state)(void);
    int     (*init_platform_hw)(void);
    int     (*ft5x0x_platform_sleep)(void);
    int     (*ft5x0x_platform_wakeup)(void);
    void    (*exit_platform_hw)(void);
};
#endif

#if defined (CONFIG_TOUCHSCREEN_I30)
struct ft5306_platform_data {
    int rest_pin;
    int irq_pin ;
    int     (*get_pendown_state)(void);
    int     (*init_platform_hw)(void);
    int     (*platform_sleep)(void);
    int     (*platform_wakeup)(void);
    void    (*exit_platform_hw)(void);
};
#endif
#if defined(CONFIG_TOUCHSCREEN_BYD693X)
struct byd_platform_data {
	u16     model;
	int     pwr_pin;
	int	  int_pin;
	int     rst_pin;
	int		pwr_on_value;
	int 	*tp_flag;

	uint16_t screen_max_x;
	uint16_t screen_max_y;
	u8 swap_xy :1;
	u8 xpol :1;
	u8 ypol :1;	
};
#endif


#if defined (CONFIG_GPIOEXP_AW9523B)
struct gpio_exp_platform_data {
    int     (*init_platform_hw)(void);
    void    (*exit_platform_hw)(void);
};
#endif

#ifdef CONFIG_INPUT_AP321XX
struct ap321xx_platform_data {
	int (*init_platform_hw)(void);
	void (*exit_platform_hw)(void);
};
#endif

enum _periph_pll {
	periph_pll_1485mhz = 148500000,
	periph_pll_297mhz = 297000000,
	periph_pll_300mhz = 300000000,
	periph_pll_1188mhz = 1188000000, /* for box*/
};
enum _codec_pll {
	codec_pll_360mhz = 360000000, /* for HDMI */
	codec_pll_408mhz = 408000000,
	codec_pll_456mhz = 456000000,
	codec_pll_504mhz = 504000000,
	codec_pll_552mhz = 552000000, /* for HDMI */
	codec_pll_600mhz = 600000000,
	codec_pll_742_5khz = 742500000,
	codec_pll_798mhz = 798000000,
	codec_pll_1064mhz = 1064000000,
	codec_pll_1188mhz = 1188000000,
};

//max i2s rate
#define CLK_FLG_MAX_I2S_12288KHZ 	(1<<1)
#define CLK_FLG_MAX_I2S_22579_2KHZ 	(1<<2)
#define CLK_FLG_MAX_I2S_24576KHZ 	(1<<3)
#define CLK_FLG_MAX_I2S_49152KHZ 	(1<<4)

#define RK30_CLOCKS_DEFAULT_FLAGS (CLK_FLG_MAX_I2S_12288KHZ/*|CLK_FLG_EXT_27MHZ*/)
#define periph_pll_default periph_pll_297mhz
#define codec_pll_default codec_pll_798mhz
//#define codec_pll_default codec_pll_1064mhz


#endif
