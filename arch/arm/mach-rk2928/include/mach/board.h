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
#ifdef CONFIG_MACH_RK2928_A720
void rk2928_usb_wifi_on(void);
void rk2928_usb_wifi_off(void);
#endif

extern struct sys_timer rk2928_timer;

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
