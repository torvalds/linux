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

struct rk30_i2c_platform_data {
	char *name;
	int bus_num;
#define I2C_RK29_ADAP   0
#define I2C_RK30_ADAP   1
	int adap_type;
	int is_div_from_arm;
	u32 flags;
	int (*io_init)(void);
	int (*io_deinit)(void);
};

#ifndef _LINUX_WLAN_PLAT_H_
struct wifi_platform_data {
        int (*set_power)(int val);
        int (*set_reset)(int val);
        int (*set_carddetect)(int val);
        void *(*mem_prealloc)(int section, unsigned long size);
        int (*get_mac_addr)(unsigned char *buf);
};
#endif

extern struct rk29_sdmmc_platform_data default_sdmmc0_data;
extern struct rk29_sdmmc_platform_data default_sdmmc1_data;

void __init rk30_map_common_io(void);
void __init rk30_init_irq(void);
void __init rk30_map_io(void);
struct machine_desc;
void __init rk30_fixup(struct machine_desc *desc, struct tag *tags, char **cmdline, struct meminfo *mi);
void __init rk30_clock_data_init(unsigned long gpll,unsigned long cpll,unsigned long max_i2s_rate);
void __init board_clock_init(void);
void board_gpio_suspend(void);
void board_gpio_resume(void);
void __sramfunc board_pmu_suspend(void);
void __sramfunc board_pmu_resume(void);

extern struct sys_timer rk30_timer;

enum _periph_pll {
	periph_pll_1485mhz = 148500000,
	periph_pll_297mhz = 297000000,
	periph_pll_1188mhz = 1188000000, /* for box*/
	periph_pll_default = periph_pll_297mhz,
};
enum _codec_pll {
	codec_pll_360mhz = 360000000, /* for HDMI */
	codec_pll_408mhz = 408000000,
	codec_pll_456mhz = 456000000,
	codec_pll_504mhz = 504000000,
	codec_pll_552mhz = 552000000, /* for HDMI */
	codec_pll_600mhz = 600000000,
	codec_pll_default = codec_pll_360mhz,
};
enum _max_i2s_rate {
	max_i2s_8192khz = 8192000,
	max_i2s_11289_6khz = 11289600,
	max_i2s_12288khz = 12288000,
	max_i2s_22579_2khz = 22579200,
	max_i2s_24576khz = 24576000,//HDMI
	max_i2s_49152khz = 24576000,//HDMI
	max_i2s_default	= max_i2s_12288khz,
};


#endif
