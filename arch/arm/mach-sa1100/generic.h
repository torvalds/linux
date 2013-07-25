/*
 * linux/arch/arm/mach-sa1100/generic.h
 *
 * Author: Nicolas Pitre
 */
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

struct cpufreq_policy;

extern unsigned int sa11x0_freq_to_ppcr(unsigned int khz);
extern int sa11x0_verify_speed(struct cpufreq_policy *policy);
extern unsigned int sa11x0_getspeed(unsigned int cpu);
extern unsigned int sa11x0_ppcr_to_freq(unsigned int idx);

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
