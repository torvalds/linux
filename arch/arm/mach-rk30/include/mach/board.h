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

struct rk30_i2c_platform_data {
	char *name;
	int bus_num;
#define I2C_RK29_ADAP   0
#define I2C_RK30_ADAP   1
	int adap_type:1;
	int is_div_from_arm:1;
	u32 flags;
	int (*io_init)(void);
	int (*io_deinit)(void);
};

extern struct rk29_sdmmc_platform_data default_sdmmc0_data;
extern struct rk29_sdmmc_platform_data default_sdmmc1_data;

void __init rk30_map_common_io(void);
void __init rk30_init_irq(void);
void __init rk30_map_io(void);
struct machine_desc;
void __init rk30_fixup(struct machine_desc *desc, struct tag *tags, char **cmdline, struct meminfo *mi);
void __init rk30_clock_init(void);

extern struct sys_timer rk30_timer;

#endif
