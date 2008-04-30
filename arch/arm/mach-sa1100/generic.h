/*
 * linux/arch/arm/mach-sa1100/generic.h
 *
 * Author: Nicolas Pitre
 */

struct sys_timer;

extern struct sys_timer sa1100_timer;
extern void __init sa1100_map_io(void);
extern void __init sa1100_init_irq(void);
extern void __init sa1100_init_gpio(void);

#define SET_BANK(__nr,__start,__size) \
	mi->bank[__nr].start = (__start), \
	mi->bank[__nr].size = (__size), \
	mi->bank[__nr].node = (((unsigned)(__start) - PHYS_OFFSET) >> 27)

extern void (*sa1100fb_backlight_power)(int on);
extern void (*sa1100fb_lcd_power)(int on);

extern void sa1110_mb_enable(void);
extern void sa1110_mb_disable(void);

struct cpufreq_policy;

extern unsigned int sa11x0_freq_to_ppcr(unsigned int khz);
extern int sa11x0_verify_speed(struct cpufreq_policy *policy);
extern unsigned int sa11x0_getspeed(unsigned int cpu);
extern unsigned int sa11x0_ppcr_to_freq(unsigned int idx);

struct flash_platform_data;
struct resource;

extern void sa11x0_set_flash_data(struct flash_platform_data *flash,
				  struct resource *res, int nr);

struct sa11x0_ssp_plat_ops;
extern void sa11x0_set_ssp_data(struct sa11x0_ssp_plat_ops *ops);

struct irda_platform_data;
void sa11x0_set_irda_data(struct irda_platform_data *irda);

struct mcp_plat_data;
void sa11x0_set_mcp_data(struct mcp_plat_data *data);
