/*
 * arch/arm/mach-exynos/px.h
 */

#ifndef __PX_H__
#define __PX_H__

extern int s3c_gpio_slp_cfgpin(unsigned int pin, unsigned int config);
extern int s3c_gpio_slp_setpull_updown(unsigned int pin, unsigned int config);
#if defined(CONFIG_MACH_P2)
extern void p2_config_gpio_table(void);
extern void p2_config_sleep_gpio_table(void);
#elif defined(CONFIG_MACH_P8)
extern void p8_config_gpio_table(void);
extern void p8_config_sleep_gpio_table(void);
#elif defined(CONFIG_MACH_P8LTE)
extern void modem_p8ltevzw_init(void);
extern void p8lte_config_gpio_table(void);
extern void p8lte_config_sleep_gpio_table(void);
#else /* CONFIG_MACH_P4) */
extern void p4_config_gpio_table(void);
extern void p4_config_sleep_gpio_table(void);
#endif

extern int brcm_wlan_init(void);
extern void set_gps_uart_op(int onoff);

#endif
