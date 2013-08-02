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

void __init rk2928_map_common_io(void);
void __init rk2928_init_irq(void);
void __init rk2928_map_io(void);
struct machine_desc;
void __init rk2928_fixup(struct machine_desc *desc, struct tag *tags, char **cmdline, struct meminfo *mi);
void __init rk2928_clock_data_init(unsigned long gpll,unsigned long cpll,u32 flags);
void __init rk2928_iomux_init(void);
extern struct sys_timer rk2928_timer;
#if defined (CONFIG_TOUCHSCREEN_I30) || defined (CONFIG_TP_760_TS)
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

enum _periph_pll {
	periph_pll_1485mhz = 148500000,
	periph_pll_297mhz = 297000000,
	periph_pll_300mhz = 300000000,
	periph_pll_768mhz = 768000000,
	periph_pll_1188mhz = 1188000000, /* for box*/
};
enum _codec_pll {
	codec_pll_360mhz = 360000000, /* for HDMI */
	codec_pll_408mhz = 408000000,
	codec_pll_456mhz = 456000000,
	codec_pll_504mhz = 504000000,
	codec_pll_552mhz = 552000000, /* for HDMI */
	codec_pll_594mhz = 594000000, /* for HDMI */
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
#if defined(CONFIG_ARCH_RK3026)
#define periph_pll_default periph_pll_768mhz
#define codec_pll_default codec_pll_594mhz
#else
#define periph_pll_default periph_pll_297mhz
#define codec_pll_default codec_pll_798mhz
#endif
//#define codec_pll_default codec_pll_1064mhz


#endif
