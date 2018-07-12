/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/arch/arm/mach-sa1100/generic.h
 *
 * Author: Nicolas Pitre
 */
#include <linux/cpufreq.h>
#include <linux/reboot.h>

extern void sa1100_timer_init(void);
extern void __init sa1100_map_io(void);
extern void __init sa1100_init_irq(void);
extern void __init sa1100_init_gpio(void);
extern void sa11x0_restart(enum reboot_mode, const char *);
extern void sa11x0_init_late(void);

#define SET_BANK(__nr,__start,__size) \
	mi->bank[__nr].start = (__start), \
	mi->bank[__nr].size = (__size)

extern void sa1110_mb_enable(void);
extern void sa1110_mb_disable(void);

extern struct cpufreq_frequency_table sa11x0_freq_table[];
extern unsigned int sa11x0_getspeed(unsigned int cpu);

struct flash_platform_data;
struct resource;

void sa11x0_register_mtd(struct flash_platform_data *flash,
			 struct resource *res, int nr);

struct irda_platform_data;
void sa11x0_register_irda(struct irda_platform_data *irda);

struct mcp_plat_data;
void sa11x0_ppc_configure_mcp(void);
void sa11x0_register_mcp(struct mcp_plat_data *data);

struct sa1100fb_mach_info;
void sa11x0_register_lcd(struct sa1100fb_mach_info *inf);

#ifdef CONFIG_PM
int sa11x0_pm_init(void);
#else
static inline int sa11x0_pm_init(void) { return 0; }
#endif

int sa11xx_clk_init(void);

struct gpiod_lookup_table;
void sa11x0_register_pcmcia(int socket, struct gpiod_lookup_table *);

struct fixed_voltage_config;
struct regulator_consumer_supply;
int sa11x0_register_fixed_regulator(int n, struct fixed_voltage_config *cfg,
	struct regulator_consumer_supply *supplies, unsigned num_supplies);
